/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Zhao Zhang, Zhengwei Lv, Jianqiang Guo, Yuhui Su
 */
#ifndef fastbotx_ModelReusableAgent_CPP_
#define fastbotx_ModelReusableAgent_CPP_

#include "ModelReusableAgent.h"
#include "Model.h"
#include <cmath>
#include "ActivityNameAction.h"
#include "ReuseModel_generated.h"
#include <iostream>
#include <fstream>
#include <limits>
#include <mutex>
#include <utility>
#include <unordered_map>
#include <type_traits>

// Trait to detect presence of templates() on PreconditionPage pointer type
namespace {
    template<typename T>
    class has_templates_method {
        template<typename U>
        static auto test(int) -> decltype(std::declval<U>()->templates(), std::true_type()) { return std::true_type(); }
        template<typename>
        static std::false_type test(...) { return std::false_type(); }
    public:
        static constexpr bool value = decltype(test<T>(0))::value;
    };
}

// Persistence helpers for precondition templates (companion .precond binary file)
namespace {
    bool savePreconditionTemplatesToFile(const std::string &filePath,
                                         const std::unordered_map<uintptr_t, fastbotx::PreconditionInfo> &pages) {
        std::ofstream out(filePath, std::ios::binary | std::ios::out);
        if (!out.is_open()) return false;
        out.write("PCTL", 4);
        uint32_t version = 2; // version 2: supports variable length templates
        out.write(reinterpret_cast<const char *>(&version), sizeof(version));
        uint64_t entryCount = static_cast<uint64_t>(pages.size());
        out.write(reinterpret_cast<const char *>(&entryCount), sizeof(entryCount));
        for (const auto &kv : pages) {
            uint64_t pageHash = static_cast<uint64_t>(kv.first);
            double score = kv.second.score;
            uint32_t tcount = static_cast<uint32_t>(kv.second.templateCount);
            out.write(reinterpret_cast<const char *>(&pageHash), sizeof(pageHash));
            out.write(reinterpret_cast<const char *>(&score), sizeof(score));
            out.write(reinterpret_cast<const char *>(&tcount), sizeof(tcount));
            for (int t = 0; t < 5; ++t) {
                const fastbotx::GuidancePathTemplate &pt = kv.second.templates[t];
                // Write template length
                uint32_t tlen = static_cast<uint32_t>(pt.length);
                out.write(reinterpret_cast<const char *>(&tlen), sizeof(tlen));
                // Write sequence and reliability (only up to MAX_TEMPLATE_SEQUENCE_LEN)
                for (int i = 0; i < fastbotx::MAX_TEMPLATE_SEQUENCE_LEN; ++i) {
                    uint64_t seq = static_cast<uint64_t>(pt.sequence[i]);
                    out.write(reinterpret_cast<const char *>(&seq), sizeof(seq));
                }
                for (int i = 0; i < fastbotx::MAX_TEMPLATE_SEQUENCE_LEN; ++i) {
                    double rel = pt.reliability[i];
                    out.write(reinterpret_cast<const char *>(&rel), sizeof(rel));
                }
            }
        }
        out.close();
        return true;
    }

    bool loadPreconditionTemplatesFromFile(const std::string &filePath,
                                           std::unordered_map<uintptr_t, fastbotx::PreconditionInfo> &pages) {
        std::ifstream in(filePath, std::ios::binary | std::ios::in);
        if (!in.is_open()) return false;
        char magic[4];
        in.read(magic, 4);
        if (in.gcount() != 4 || std::string(magic, 4) != "PCTL") {
            in.close();
            return false;
        }
        uint32_t version = 0;
        in.read(reinterpret_cast<char *>(&version), sizeof(version));
        if (version != 1 && version != 2) { in.close(); return false; }
        uint64_t entryCount = 0;
        in.read(reinterpret_cast<char *>(&entryCount), sizeof(entryCount));
        BLOG("[GUIDE] loadPreconditionTemplatesFromFile: version=%u, entryCount=%llu", version, static_cast<unsigned long long>(entryCount));
        for (uint64_t e = 0; e < entryCount; ++e) {
            uint64_t pageHash = 0;
            double score = 1.0;
            uint32_t tcount = 0;
            in.read(reinterpret_cast<char *>(&pageHash), sizeof(pageHash));
            in.read(reinterpret_cast<char *>(&score), sizeof(score));
            in.read(reinterpret_cast<char *>(&tcount), sizeof(tcount));
            fastbotx::PreconditionInfo info;
            info.score = score;
            info.templateCount = static_cast<int>(std::min<uint32_t>(tcount, 5u));
            for (int t = 0; t < 5; ++t) {
                // version 2: read template length
                if (version >= 2) {
                    uint32_t tlen = 0;
                    in.read(reinterpret_cast<char *>(&tlen), sizeof(tlen));
                    info.templates[t].length = static_cast<int>(std::min<uint32_t>(tlen, fastbotx::MAX_TEMPLATE_SEQUENCE_LEN));
                }
                // Read sequence (version 1 has 6 elements, version 2 has 5)
                int seqCount = (version == 1) ? 6 : fastbotx::MAX_TEMPLATE_SEQUENCE_LEN;
                for (int i = 0; i < seqCount; ++i) {
                    uint64_t seq = 0;
                    in.read(reinterpret_cast<char *>(&seq), sizeof(seq));
                    if (i < fastbotx::MAX_TEMPLATE_SEQUENCE_LEN) {
                        info.templates[t].sequence[i] = static_cast<uint64_t>(seq);
                    }
                }
                // Read reliability
                int relCount = (version == 1) ? 6 : fastbotx::MAX_TEMPLATE_SEQUENCE_LEN;
                for (int i = 0; i < relCount; ++i) {
                    double rel = 0.0;
                    in.read(reinterpret_cast<char *>(&rel), sizeof(rel));
                    if (i < fastbotx::MAX_TEMPLATE_SEQUENCE_LEN) {
                        info.templates[t].reliability[i] = rel;
                    }
                }
                // For version 1, compute length from non-zero sequence entries
                if (version == 1) {
                    info.templates[t].length = 0;
                    for (int i = 0; i < fastbotx::MAX_TEMPLATE_SEQUENCE_LEN; ++i) {
                        if (info.templates[t].sequence[i] != 0) {
                            info.templates[t].length = i + 1;
                        }
                    }
                }
            }
            pages[static_cast<uintptr_t>(pageHash)] = info;
            BLOG("[GUIDE] Loaded page %llu: score=%.2f, templateCount=%d",
                 static_cast<unsigned long long>(pageHash), score, info.templateCount);
        }
        in.close();
        return true;
    }

    // loadTemplatesFromFBPage: when PreconditionPage has templates(), read into PreconditionInfo
    template<typename PagePtr>
    typename std::enable_if<has_templates_method<PagePtr>::value, void>::type
    loadTemplatesFromFBPage(PagePtr page, fastbotx::PreconditionInfo &info) {
        if (!page) return;
        auto fbTemplates = page->templates();
        if (!fbTemplates) return;
        int available = static_cast<int>(fbTemplates->size());
        info.templateCount = std::min(available, 5);
        BLOG("[GUIDE] loadTemplatesFromFBPage: loading %d templates", info.templateCount);
        for (int t = 0; t < info.templateCount; ++t) {
            auto fbT = fbTemplates->Get(t);
            if (!fbT) continue;
            // sequence
            info.templates[t].length = 0;
            if (fbT->sequence() != nullptr) {
                auto seqVec = fbT->sequence();
                int seqLen = static_cast<int>(seqVec->size());
                for (int i = 0; i < fastbotx::MAX_TEMPLATE_SEQUENCE_LEN; ++i) {
                    if (i < seqLen && seqVec->Get(i) != 0) {
                        info.templates[t].sequence[i] = seqVec->Get(i);
                        info.templates[t].length = i + 1;
                    } else {
                        info.templates[t].sequence[i] = 0;
                    }
                }
            } else {
                for (int i = 0; i < fastbotx::MAX_TEMPLATE_SEQUENCE_LEN; ++i) info.templates[t].sequence[i] = 0;
            }
            // reliability
            if (fbT->reliability() != nullptr) {
                auto relVec = fbT->reliability();
                for (int i = 0; i < fastbotx::MAX_TEMPLATE_SEQUENCE_LEN; ++i) {
                    if (i < static_cast<int>(relVec->size())) info.templates[t].reliability[i] = relVec->Get(i);
                    else info.templates[t].reliability[i] = 0.5;
                }
            } else {
                for (int i = 0; i < fastbotx::MAX_TEMPLATE_SEQUENCE_LEN; ++i) info.templates[t].reliability[i] = 0.5;
            }
            BLOG("[GUIDE] Template[%d]: length=%d, seq=[%llu,%llu,%llu,%llu,%llu]", t, info.templates[t].length,
                 static_cast<unsigned long long>(info.templates[t].sequence[0]),
                 static_cast<unsigned long long>(info.templates[t].sequence[1]),
                 static_cast<unsigned long long>(info.templates[t].sequence[2]),
                 static_cast<unsigned long long>(info.templates[t].sequence[3]),
                 static_cast<unsigned long long>(info.templates[t].sequence[4]));
        }
        // initialize unused template slots
        for (int t = info.templateCount; t < 5; ++t) {
            info.templates[t].length = 0;
            for (int i = 0; i < fastbotx::MAX_TEMPLATE_SEQUENCE_LEN; ++i) {
                info.templates[t].sequence[i] = 0;
                info.templates[t].reliability[i] = 0.0;
            }
        }
    }

    // fallback when templates() doesn't exist: no-op
    template<typename PagePtr>
    typename std::enable_if<!has_templates_method<PagePtr>::value, void>::type
    loadTemplatesFromFBPage(PagePtr, fastbotx::PreconditionInfo &) {
        // no templates in schema
    }
} // anonymous namespace

namespace fastbotx {

    ModelReusableAgent::ModelReusableAgent(const ModelPtr &model)
            : AbstractAgent(model), _alpha(SarsaRLDefaultAlpha), _epsilon(SarsaRLDefaultEpsilon),
              _modelSavePath(DefaultModelSavePath), _defaultModelSavePath(DefaultModelSavePath),
              _precond_alpha(0.15), _hit_decay(0.95), _min_score(0.1), _precond_lambda(1.0),
              _sigmoid_k(1.0), _sigmoid_b(0.5), _guidance_gamma(0.1), _guidance_action_attempt_limit(10),
              _guidance_history_len(6), _attempt_fail_decay(0.8) {
        this->_algorithmType = AlgorithmType::Reuse;
        // guidance hyperparams defaults
    }

    ModelReusableAgent::~ModelReusableAgent() {
        BLOG("save model in destruct");
        this->saveReuseModel(this->_modelSavePath);
        this->_reuseModel.clear();
    }

    void ModelReusableAgent::computeAlphaValue() {
        if (nullptr != this->_newState) {
            /// actually, the following part of computing alpha could be extracted and treated as a single method
            const GraphPtr &graphRef = this->_model.lock()->getGraph(); // won't this cause null pointer issue? since the lock of weak_ptr could be null?
            long totalVisitCount = graphRef->getTotalDistri(); // get the total count of visited states
            double movingAlpha = 0.5;
            if (totalVisitCount >
                20000)  // if the total count of visited states is too much, reduce the alpha.
            {
                movingAlpha -= 0.1;
            }
            if (totalVisitCount > 50000) {
                movingAlpha -= 0.1;
            }
            if (totalVisitCount > 100000) {
                movingAlpha -= 0.1;
            }
            if (totalVisitCount > 250000) {
                movingAlpha -= 0.1;
            }
            // after reducing, the possible minimal alpha is 0.1
            // but the actually possible minimal alpha is 0.2, the same as SarsaRLDefaultAlpha
            this->_alpha = std::max(SarsaRLDefaultAlpha, movingAlpha);
        }
    }

#define SarsaNStep 5

    /// Based on the lastSelectedAction (newly selected action), compute its reward value
    /// \return the reward value
    double ModelReusableAgent::computeRewardOfLatestAction() {
        double rewardValue = 0.0;
        if (nullptr != this->_newState) {
            this->computeAlphaValue();
            const GraphPtr &graphRef = this->_model.lock()->getGraph();
            auto visitedActivities = graphRef->getVisitedActivities(); // get the set of visited activities
            // get the last, or previous, action in the vector containing previous actions.
            ActivityStateActionPtr lastSelectedAction = std::dynamic_pointer_cast<ActivityStateAction>(
                    this->_previousActions.back());
            if (nullptr != lastSelectedAction) {
                // Get the expectation of this action for accessing unvisited new activity.
                rewardValue = this->probabilityOfVisitingNewActivities(lastSelectedAction,
                                                                       visitedActivities);
                // If this is an action not in reuse model, this action is new and should definitely be used
                if (std::abs(rewardValue - 0.0) < 0.0001)
                    rewardValue = 1.0; // Set the expectation of this action to 1
                rewardValue = (rewardValue / sqrt(lastSelectedAction->getVisitedCount() + 1.0));
            }
            rewardValue = rewardValue + (this->getStateActionExpectationValue(this->_newState,
                                                                              visitedActivities) /
                                         sqrt(this->_newState->getVisitedCount() + 1.0));
            BLOG("total visited " ACTIVITY_VC_STR " count is %zu", visitedActivities.size());
        }
        BDLOG("reuse-cov-opti action reward=%f", rewardValue);
        this->_rewardCache.emplace_back(rewardValue);
        // Make sure the length of reward cache is not over SarsaNStep
        if (this->_rewardCache.size() > SarsaNStep) {
            this->_rewardCache.erase(this->_rewardCache.begin());
        }
        return rewardValue;// + this->_newState->getTheta();
    }

    /// Based on the reuse model, compute the probability of this current action visiting a unvisited activity,
    /// which not in visitedActivities set. This value is the percentage of count of
    /// activities that this state has not reached compared with the visitedActivities set.
    /// \param action The chosen action in this state.
    /// \param visitedActivities A string set, containing already visited activities.
    /// \return percentage of count of activities that this state has not reached compared with the visitedActivities set.
    double
    ModelReusableAgent::probabilityOfVisitingNewActivities(const ActivityStateActionPtr &action,
                                                           const stringPtrSet &visitedActivities) const {
        double value = .0;
        int total = 0;
        int unvisited = 0;
        // find this action in this model according to its int hash
        // according to the given action, get the activities that this action could reach in reuse model.
        auto actionMapIterator = this->_reuseModel.find(action->hash());
        if (actionMapIterator != this->_reuseModel.end()) {
            // Iterate the map containing entry of activity name and visited count
            // to ascertain the unvisited activity count according to the pre-saved reuse model
            for (const auto &activityCountMapIterator: (*actionMapIterator).second) {
                total += activityCountMapIterator.second;
                stringPtr activity = activityCountMapIterator.first;
                if (visitedActivities.find(activity) == visitedActivities.end()) {
                    unvisited += activityCountMapIterator.second;
                }
            }
            if (total > 0 && unvisited > 0) {
                value = static_cast<double>(unvisited) / total;
            }
        }
        return value;
    }

    /// Return the expectation of reaching an unvisited activity after executing one of the action
    /// from this state. It estimate the expectation from the perspective of the whole state.
    /// @param state the newly reached state
    /// @param visitedActivities the visited activity set AFTER reaching this state(the activity of this
    ///         state is included)
    /// @return the expectation of this state reaching an unvisited activity after executing one of the action
    double ModelReusableAgent::getStateActionExpectationValue(const StatePtr &state,
                                                              const stringPtrSet &visitedActivities) const {
        double value = 0.0;
        for (const auto &action: state->getActions()) {
            uintptr_t actionHash = action->hash();
            // if this action is new, increment the value by 1, else by 0.5
            // If this action has not been visited yet.
            if (this->_reuseModel.find(actionHash) == this->_reuseModel.end()) {
                value += 1.0;
            }                // If this action is been performed in current testing.
            else if (action->getVisitedCount() >= 1) {
                value += 0.5;
            }
            // regardless of the back action
            // Expectation of reaching an unvisited activity.
            if (action->getTarget() != nullptr) {
                value += probabilityOfVisitingNewActivities(action, visitedActivities);
            }
        }
        return value;
    }

    double ModelReusableAgent::getQValue(const ActionPtr &action) {
        return action->getQValue();
    }

    void ModelReusableAgent::setQValue(const ActionPtr &action, double qValue) {
        action->setQValue(qValue);
    }

    /// If the new action is generated,
    void ModelReusableAgent::updateStrategy() {
        if (nullptr == this->_newAction) // need to call resolveNewAction to update _newAction
            return;
        // _previousActions is a vector storing certain amount of actions, of which length equals to SarsaNStep.
        if (!this->_previousActions.empty()) {
            this->computeRewardOfLatestAction();
            this->updateReuseModel();
            double value = getQValue(_newAction);
            for (int i = static_cast<int>(this->_previousActions.size()) - 1; i >= 0; i--) {
                double currentQValue = getQValue(_previousActions[i]);
                double currentRewardValue = this->_rewardCache[i];
                // accumulated reward from the newest actions
                value = currentRewardValue + SarsaRLDefaultGamma * value;
                // Should not update the q value during step (action edge) between i+1 to i+n-1
                // The following statement is slightly different from the original sarsa RL paper.
                // Considering to move the next statement outside of this block.
                // Since only the oldest action should be updated.
                if (i == 0)
                    setQValue(this->_previousActions[i],
                              currentQValue + this->_alpha * (value - currentQValue));
            }

            // NOTE: Pending guide check (reward/penalty) is now done in selectNewAction() at the beginning
            // This ensures the check happens AFTER addCurrentPageAsPrecondition has a chance to be called

            // NOTE: Recording actionList, template creation, and marking covered are all handled by
            // addCurrentPageAsPrecondition() which is called externally when a precondition page is reached.
            // No guide-related logic needed here anymore.
        } else {
            BDLOG("%s", "get action value failed!");
        }
        // add the new action to the back of the cache.
        this->_previousActions.emplace_back(this->_newAction);
        if (this->_previousActions.size() > SarsaNStep) {
            // if the cached length is over SarsaNStep, erase the first action from cache.
            this->_previousActions.erase(this->_previousActions.begin());
        }
    }

    // Guidance: select an action guided by precondition action-success probabilities
    ActionPtr ModelReusableAgent::selectGuidedActionForPrecondition() {
        if (nullptr == this->_newState) return nullptr;

        BLOG("[GUIDE] selectGuidedActionForPrecondition: enter, currentState activity=%s",
             this->_newState->getActivityString() ? this->_newState->getActivityString()->c_str() : "null");

        // 1. Build candidates: pages not covered this episode, score>=0.3, have templates, not over selection limit
        std::vector<std::pair<uintptr_t, PreconditionInfo*>> candidates;
        {
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            BLOG("[GUIDE] selectGuidedActionForPrecondition: total preconditionPages=%zu, coveredThisEpisode=%zu",
                 this->_preconditionPages.size(), this->_coveredPreconditionsThisEpisode.size());
            for (auto &kv : this->_preconditionPages) {
                auto &pageHash = kv.first;
                auto &info = kv.second;
                if (this->_coveredPreconditionsThisEpisode.find(pageHash) != this->_coveredPreconditionsThisEpisode.end()) {
                    BDLOG("[GUIDE] skip page %lu: already covered", static_cast<unsigned long>(pageHash));
                    continue;
                }
                if (info.score < 0.3) {
                    BDLOG("[GUIDE] skip page %lu: score %.2f < 0.3", static_cast<unsigned long>(pageHash), info.score);
                    continue;
                }
                if (info.templateCount == 0) {
                    BDLOG("[GUIDE] skip page %lu: templateCount=0", static_cast<unsigned long>(pageHash));
                    continue;
                }
                // Check page selection limit
                if (this->_pageSelectionCounts[pageHash] >= MAX_PAGE_SELECTIONS_PER_EPISODE) {
                    BDLOG("[GUIDE] skip page %lu: reached max selections (%d)",
                          static_cast<unsigned long>(pageHash), MAX_PAGE_SELECTIONS_PER_EPISODE);
                    continue;
                }
                candidates.emplace_back(pageHash, &info);
                BLOG("[GUIDE] candidate page %lu: score=%.2f, templateCount=%d, selectionCount=%d",
                     static_cast<unsigned long>(pageHash), info.score, info.templateCount,
                     this->_pageSelectionCounts[pageHash]);
            }
        }

        if (candidates.empty()) {
            BLOG("[GUIDE] selectGuidedActionForPrecondition: no candidates, returning null");
            return nullptr;
        }
        BLOG("[GUIDE] selectGuidedActionForPrecondition: %zu candidate pages", candidates.size());

        // NOTE: Global decay is now only applied on FAILURE in checkPendingGuideResult()
        // Not decaying here avoids punishing templates just for being considered

        // 2. Prepare map of actions on current page
        std::unordered_map<uint64_t, ActionPtr> pageActions;
        for (auto &a : this->_newState->getActions()) pageActions[static_cast<uint64_t>(a->hash())] = a;
        BLOG("[GUIDE] selectGuidedActionForPrecondition: current page has %zu actions", pageActions.size());

        // 3. Evaluate templates: reversed positions, with selection limit check
        ActionPtr bestAction = nullptr;
        double bestScore = -1.0;
        uintptr_t bestPage = 0;
        int bestPos = -1;
        int bestTemplateIdx = -1;

        for (auto &entry : candidates) {
            uintptr_t pageHash = entry.first;
            PreconditionInfo *info = entry.second;
            BLOG("[GUIDE] Evaluating page %lu with %d templates", static_cast<unsigned long>(pageHash), info->templateCount);
            for (int t = 0; t < info->templateCount; ++t) {
                // Check template selection limit
                if (this->_templateSelectionCounts[pageHash][t] >= MAX_TEMPLATE_SELECTIONS_PER_EPISODE) {
                    BLOG("[GUIDE] skip page %lu template %d: reached max selections (%d)",
                          static_cast<unsigned long>(pageHash), t, MAX_TEMPLATE_SELECTIONS_PER_EPISODE);
                    continue;
                }

                GuidancePathTemplate &templ = info->templates[t];
                int templateLen = templ.length > 0 ? templ.length : MAX_TEMPLATE_SEQUENCE_LEN;
                BLOG("[GUIDE] Evaluating template[%d]: length=%d, seq=[%llu,%llu,%llu,%llu,%llu]",
                     t, templ.length,
                     static_cast<unsigned long long>(templ.sequence[0]),
                     static_cast<unsigned long long>(templ.sequence[1]),
                     static_cast<unsigned long long>(templ.sequence[2]),
                     static_cast<unsigned long long>(templ.sequence[3]),
                     static_cast<unsigned long long>(templ.sequence[4]));

                // Search from end (closest to target) to start
                for (int pos = templateLen - 1; pos >= 0; --pos) {
                    uint64_t actionHash = templ.sequence[pos];
                    if (actionHash == 0) continue;
                    auto it = pageActions.find(actionHash);
                    if (it == pageActions.end()) {
                        BLOG("[GUIDE] Template[%d] pos=%d action %llu not found on current page",
                             t, pos, static_cast<unsigned long long>(actionHash));
                        continue;
                    }
                    ActionPtr action = it->second;
                    // Check consecutive fails for this (page, action) pair
                    int cf = this->_consecutiveFails[pageHash][actionHash];
                    if (cf >= 3) {
                        templ.reliability[pos] = 0.0;
                        BLOG("[GUIDE] Template[%d] pos=%d action %llu blacklisted (cf=%d)",
                             t, pos, static_cast<unsigned long long>(actionHash), cf);
                        continue;
                    }
                    // Calculate score: position normalized to [0,1], higher position = closer to end
                    double P = (templateLen > 1) ? static_cast<double>(pos) / (templateLen - 1) : 1.0;
                    double R = templ.reliability[pos];
                    double C = info->score / 2.0;
                    double score = 0.5 * P + 0.3 * R + 0.2 * C;
                    BLOG("[GUIDE] Template[%d] pos=%d action %llu MATCH: P=%.2f R=%.2f C=%.2f score=%.4f",
                         t, pos, static_cast<unsigned long long>(actionHash), P, R, C, score);
                    if (score > bestScore) {
                        bestScore = score;
                        bestAction = action;
                        bestPage = pageHash;
                        bestPos = pos;
                        bestTemplateIdx = t;
                    }
                    break; // stop after first valid position in this template
                }
            }
        }

        if (!bestAction) {
            BLOG("[GUIDE] selectGuidedActionForPrecondition: no valid action found, returning null");
            return nullptr;
        }

        // Increment selection counts
        this->_pageSelectionCounts[bestPage]++;
        this->_templateSelectionCounts[bestPage][bestTemplateIdx]++;

        this->_lastPosition[bestPage] = bestPos;
        this->_guidanceAttemptCounts[bestPage][static_cast<uint64_t>(bestAction->hash())]++;

        // IMPORTANT: Reset _preconditionReachedSinceLastGuide BEFORE setting pending
        // This prevents residual true value from previous non-guided actions causing false success
        // Scenario to avoid:
        //   Step N: random action accidentally reaches precondition -> flag set to true
        //   Step N+1: guided action selected, but fails
        //   Step N+2: checkPendingGuideResult sees residual true, wrongly rewards the failed action
        this->_preconditionReachedSinceLastGuide = false;

        // Set pending guide check - will be evaluated in NEXT checkPendingGuideResult call
        this->_hasPendingGuideCheck = true;
        this->_pendingGuideActionHash = static_cast<uint64_t>(bestAction->hash());
        this->_pendingGuideTargetPage = bestPage;

        BLOG("[GUIDE] exit selectGuidedActionForPrecondition: chosen_hash=%llu page=%lu template=%d pos=%d bestScore=%.4f, pageSelections=%d, templateSelections=%d",
             static_cast<unsigned long long>(bestAction->hash()), static_cast<unsigned long>(bestPage),
             bestTemplateIdx, bestPos, bestScore,
             this->_pageSelectionCounts[bestPage], this->_templateSelectionCounts[bestPage][bestTemplateIdx]);
        return bestAction;
    }

    uintptr_t ModelReusableAgent::popPendingGuidedTarget(uint64_t actionHash) {
        std::lock_guard<std::mutex> guard(this->_preconditionLock);
        auto it = this->_pendingGuidedTargets.find(actionHash);
        if (it == this->_pendingGuidedTargets.end()) return 0;
        uintptr_t p = it->second;
        this->_pendingGuidedTargets.erase(it);
        return p;
    }

    void ModelReusableAgent::setPendingGuidedTarget(uint64_t actionHash, uintptr_t pageHash) {
        std::lock_guard<std::mutex> guard(this->_preconditionLock);
        this->_pendingGuidedTargets[actionHash] = pageHash;
    }

    // Check pending guide result from previous step and apply reward/penalty
    void ModelReusableAgent::checkPendingGuideResult() {
        if (!this->_hasPendingGuideCheck) {
            return;
        }

        BLOG("[GUIDE] checkPendingGuideResult: checking pending guide action %llu -> target page %lu, preconditionReached=%d",
             static_cast<unsigned long long>(this->_pendingGuideActionHash),
             static_cast<unsigned long>(this->_pendingGuideTargetPage),
             this->_preconditionReachedSinceLastGuide);

        if (this->_preconditionReachedSinceLastGuide) {
            // SUCCESS: guide action led to a precondition page
            BLOG("[GUIDE] Success: guided action %llu led to precondition page (reward already applied in addCurrentPageAsPrecondition)",
                 static_cast<unsigned long long>(this->_pendingGuideActionHash));
            // Reset failure counters for this (page, action) pair
            this->_consecutiveFails[this->_pendingGuideTargetPage][this->_pendingGuideActionHash] = 0;
            this->_noProgressCount[this->_pendingGuideTargetPage] = 0;
        } else {
            // FAILURE: guide action did NOT lead to any precondition page
            BLOG("[GUIDE] Penalty: guided action %llu did not lead to any precondition page",
                 static_cast<unsigned long long>(this->_pendingGuideActionHash));

            uintptr_t targetPage = this->_pendingGuideTargetPage;
            if (targetPage != 0) {
                std::lock_guard<std::mutex> guard(this->_preconditionLock);
                auto it = this->_preconditionPages.find(targetPage);
                if (it != this->_preconditionPages.end()) {
                    PreconditionInfo &info = it->second;

                    // Decrease reliability for templates containing the failed action
                    for (int t = 0; t < info.templateCount; ++t) {
                        for (int i = 0; i < MAX_TEMPLATE_SEQUENCE_LEN; ++i) {
                            if (info.templates[t].sequence[i] == this->_pendingGuideActionHash) {
                                double oldRel = info.templates[t].reliability[i];
                                info.templates[t].reliability[i] *= 0.5;
                                BLOG("[GUIDE] Decreased reliability for action %llu in template %d pos %d: %.4f -> %.4f",
                                     static_cast<unsigned long long>(this->_pendingGuideActionHash), t, i,
                                     oldRel, info.templates[t].reliability[i]);
                            }
                        }
                    }

                    // Global decay for all templates of this page
                    for (int t = 0; t < info.templateCount; ++t) {
                        for (int i = 0; i < MAX_TEMPLATE_SEQUENCE_LEN; ++i) {
                            info.templates[t].reliability[i] *= 0.95;
                        }
                    }
                    BLOG("[GUIDE] Applied global decay (0.95) to all templates of page %lu", static_cast<unsigned long>(targetPage));

                    // Increment failure counters (per-page noProgressCount and per-page-action consecutiveFails)
                    this->_consecutiveFails[targetPage][this->_pendingGuideActionHash]++;
                    this->_noProgressCount[targetPage]++;

                    BLOG("[GUIDE] Page %lu noProgressCount=%d, consecutiveFails[page][%llu]=%d",
                         static_cast<unsigned long>(targetPage),
                         this->_noProgressCount[targetPage],
                         static_cast<unsigned long long>(this->_pendingGuideActionHash),
                         this->_consecutiveFails[targetPage][this->_pendingGuideActionHash]);

                    // Page-level stop if noProgressCount >= 3 for this page
                    if (this->_noProgressCount[targetPage] >= 3) {
                        info.score = std::max(info.score * 0.3, 0.1);
                        this->_coveredPreconditionsThisEpisode.insert(targetPage);
                        BLOG("[GUIDE] Page %lu penalized (score=%.4f) and marked covered due to 3 consecutive failures",
                             static_cast<unsigned long>(targetPage), info.score);
                    }

                    // Action-level blacklist if consecutiveFails >= 3 for this (page, action) pair
                    if (this->_consecutiveFails[targetPage][this->_pendingGuideActionHash] >= 3) {
                        for (int t = 0; t < info.templateCount; ++t) {
                            for (int i = 0; i < MAX_TEMPLATE_SEQUENCE_LEN; ++i) {
                                if (info.templates[t].sequence[i] == this->_pendingGuideActionHash) {
                                    info.templates[t].reliability[i] = 0.0;
                                    BLOG("[GUIDE] Blacklisted action %llu in template %d pos %d",
                                         static_cast<unsigned long long>(this->_pendingGuideActionHash), t, i);
                                }
                            }
                        }
                        BLOG("[GUIDE] Action %llu fully blacklisted (reliability=0) due to 3 consecutive failures",
                             static_cast<unsigned long long>(this->_pendingGuideActionHash));
                    }
                }
            }
        }

        // Clear the pending check
        BLOG("[GUIDE] checkPendingGuideResult: clearing pending state");
        this->_hasPendingGuideCheck = false;
        this->_pendingGuideActionHash = 0;
        this->_pendingGuideTargetPage = 0;
        this->_preconditionReachedSinceLastGuide = false;
    }

    /// If the new action is generated,
    ActionPtr ModelReusableAgent::selectNewAction() {
        // Check pending guided action from PREVIOUS step
        this->checkPendingGuideResult();

        ActionPtr action = nullptr;
        // First try guided precondition action
        action = this->selectGuidedActionForPrecondition();
        if (nullptr != action) {
            BLOG("select action by precondition guidance");
            return action;
        }
        action = this->selectUnperformedActionNotInReuseModel();
        if (nullptr != action) {
            BLOG("%s", "select action not in reuse model");
            return action;
        }

        action = this->selectUnperformedActionInReuseModel();
        if (nullptr != action) {
            BLOG("%s", "select action in reuse model");
            return action;
        }

        action = this->_newState->randomPickUnvisitedAction();
        if (nullptr != action) {
            BLOG("%s", "select action in unvisited action");
            return action;
        }

        action = this->selectActionByQValue();
        if (nullptr != action) {
            BLOG("%s", "select action by qvalue");
            return action;
        }

        action = this->selectNewActionEpsilonGreedyRandomly();
        if (nullptr != action) {
            BLOG("%s", "select action by EpsilonGreedyRandom");
            return action;
        }
        BLOGE("null action happened , handle null action");
        return handleNullAction();
    }

    ActivityStateActionPtr ModelReusableAgent::selectNewActionEpsilonGreedyRandomly() const {
        if (this->eGreedy()) {
            BDLOG("%s", "Try to select the max value action");
            return this->_newState->greedyPickMaxQValue(enableValidValuePriorityFilter);
        }
        BDLOG("%s", "Try to randomly select a value action.");
        return this->_newState->randomPickAction(enableValidValuePriorityFilter);
    }

    bool ModelReusableAgent::eGreedy() const {
        // use existing randomInt helper to avoid reseeding global RNG repeatedly
        int v = randomInt(0, 100); // returns 0..99
        double r = static_cast<double>(v) / 100.0;
        return !(r < this->_epsilon);
    }

    ActionPtr ModelReusableAgent::selectUnperformedActionNotInReuseModel() const {
         std::vector<ActionPtr> actionsNotInModel;
        for (const auto &action : this->_newState->getActions()) {
            bool matched = action->isModelAct() &&
                           (this->_reuseModel.find(action->hash()) == this->_reuseModel.end()) &&
                           action->getVisitedCount() <= 0;
            if (matched) {
                actionsNotInModel.emplace_back(action);
            }
        }
        int totalWeight = 0;
        for (const auto &action : actionsNotInModel) {
            totalWeight += action->getPriority();
        }
        if (totalWeight <= 0) {
            BDLOGE("%s", " total weights is 0");
            return nullptr;
        }
        int randI = randomInt(0, totalWeight);
        for (auto action : actionsNotInModel) {
            if (randI < action->getPriority()) {
                return action;
            }
            randI -= action->getPriority();
        }
        BDLOGE("%s", " rand a null action");
        return nullptr;
    }

    ActionPtr ModelReusableAgent::selectUnperformedActionInReuseModel() const {
        float maxValue = -MAXFLOAT;
        ActionPtr nextAction = nullptr;
        for (const auto &action : this->_newState->targetActions()) {
            uintptr_t actionHash = action->hash();
            if (this->_reuseModel.find(actionHash) != this->_reuseModel.end()) {
                if (action->getVisitedCount() > 0) {
                    BDLOG("%s", "action has been visited");
                    continue;
                }
                auto modelPointer = this->_model.lock();
                if (modelPointer) {
                    const GraphPtr &graphRef = modelPointer->getGraph();
                    auto visitedActivities = graphRef->getVisitedActivities();
                    auto qualityValue = static_cast<float>(this->probabilityOfVisitingNewActivities(action, visitedActivities));
                    if (qualityValue > 1e-4) {
                        qualityValue = 10.0f * qualityValue;
                        auto uniform = static_cast<float>(static_cast<float>(randomInt(0, 10)) / 10.0f);
                        if (uniform < std::numeric_limits<float>::min())
                            uniform = std::numeric_limits<float>::min();
                        qualityValue -= log(-log(uniform));
                        if (qualityValue > maxValue) {
                            maxValue = qualityValue;
                            nextAction = action;
                        }
                    }
                }
            }
        }
        return nextAction;
    }

    ActionPtr ModelReusableAgent::selectActionByQValue() {
        ActionPtr returnAction = nullptr;
        float maxQ = -MAXFLOAT;
        const GraphPtr &graphRef = this->_model.lock()->getGraph();
        auto visitedActivities = graphRef->getVisitedActivities();
        for (auto action : this->_newState->getActions()) {
            double qv = 0.0;
            uintptr_t actionHash = action->hash();
            if (action->getVisitedCount() <= 0) {
                auto iterator = this->_reuseModel.find(actionHash);
                if (iterator != this->_reuseModel.end()) {
                    qv += this->probabilityOfVisitingNewActivities(action, visitedActivities);
                } else {
                    BDLOG("qvalue pick return a action: %s", action->toString().c_str());
                    return action;
                }
            }
            qv += getQValue(action);
            qv /= 0.1;
            float uniform = static_cast<float>(randomInt(0, 10)) / 10.0f;
            if (uniform < std::numeric_limits<float>::min())
                uniform = std::numeric_limits<float>::min();
            qv -= log(-log(uniform));
            if (qv > maxQ) {
                maxQ = static_cast<float>(qv);
                returnAction = action;
            }
        }
        return returnAction;
    }

    void ModelReusableAgent::adjustActions() {
        AbstractAgent::adjustActions();
    }

    void ModelReusableAgent::threadModelStorage(const std::weak_ptr<ModelReusableAgent> &agent) {
        int saveInterval = 1000 * 60 * 10; // save the model every 10 minutes
        while (!agent.expired()) {
            agent.lock()->saveReuseModel(agent.lock()->_modelSavePath);
            std::this_thread::sleep_for(std::chrono::milliseconds(saveInterval));
        }
    }

    void ModelReusableAgent::loadReuseModel(const std::string &packageName) {
        std::string modelFilePath = "/sdcard/fastbot_" + packageName + ".fbm";
        this->_modelSavePath = modelFilePath;
        if (!this->_modelSavePath.empty()) {
            this->_defaultModelSavePath = "/sdcard/fastbot_" + packageName + ".tmp.fbm";
        }
        BLOG("begin load model: %s", this->_modelSavePath.c_str());

        std::ifstream modelFile(modelFilePath, std::ios::binary | std::ios::in);
        if (modelFile.fail()) {
            BLOG("read model file %s failed, check if file exists!", modelFilePath.c_str());
            return;
        }

        std::filebuf *fileBuffer = modelFile.rdbuf();
        std::size_t filesize = fileBuffer->pubseekoff(0, modelFile.end, modelFile.in);
        fileBuffer->pubseekpos(0, modelFile.in);
        char *modelFileData = new char[filesize];
        fileBuffer->sgetn(modelFileData, static_cast<int>(filesize));
        auto reuseFBModel = GetReuseModel(modelFileData);

        {
            std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
            (void)reuseGuard;
            this->_reuseModel.clear();
            this->_reuseQValue.clear();
        }
        auto reusedModelDataPtr = reuseFBModel->model();
        if (!reusedModelDataPtr) {
            BLOG("%s", "model data is null");
            delete[] modelFileData;
            return;
        }
        for (int entryIndex = 0; entryIndex < reusedModelDataPtr->size(); entryIndex++) {
             auto reuseEntryInReuseModel = reusedModelDataPtr->Get(entryIndex);
             uint64_t actionHash = reuseEntryInReuseModel->action();
             auto activityEntry = reuseEntryInReuseModel->targets();
             ReuseEntryM entryPtr;
             for (int targetIndex = 0; targetIndex < activityEntry->size(); targetIndex++) {
                 auto targetEntry = activityEntry->Get(targetIndex);
                 BDLOG("load model hash: %llu %s %d", static_cast<unsigned long long>(actionHash), targetEntry->activity()->str().c_str(), static_cast<int>(targetEntry->times()));
                 entryPtr.insert(std::make_pair(std::make_shared<std::string>(targetEntry->activity()->str()), static_cast<int>(targetEntry->times())));
             }
             if (!entryPtr.empty()) {
                std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
                (void)reuseGuard;
                this->_reuseModel.insert(std::make_pair(actionHash, entryPtr));
             }
         }
        BLOG("loaded model contains actions: %zu", this->_reuseModel.size());

        // [DATA SEPARATION] Do NOT read precondition_pages from .fbm; they are stored only in companion .precond file
        {
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            (void)guard;
            this->_preconditionPages.clear();
            BLOG("[GUIDE] Precondition pages will be loaded from separate .precond file only (skipping .fbm)");
        }

        // --- Load ALL precondition data from companion .precond file ---
        {
            std::string precondFilePath = modelFilePath + ".precond";
            BLOG("[GUIDE] Loading precondition pages from companion file: %s", precondFilePath.c_str());
            std::unordered_map<uintptr_t, PreconditionInfo> filePages;
            if (loadPreconditionTemplatesFromFile(precondFilePath, filePages)) {
                BLOG("[GUIDE] Successfully loaded %zu pages from companion file", filePages.size());
                std::lock_guard<std::mutex> guard(this->_preconditionLock);
                for (const auto &kv : filePages) {
                    this->_preconditionPages[kv.first] = kv.second;
                    BLOG("[GUIDE] Loaded page %lu: templateCount=%d, score=%f",
                         static_cast<unsigned long>(kv.first), kv.second.templateCount, kv.second.score);
                    // Log each template
                    for (int t = 0; t < kv.second.templateCount; ++t) {
                        BLOG("[GUIDE] Page %lu template[%d]: seq=[%llu,%llu,%llu,%llu,%llu] rel=[%.2f,%.2f,%.2f,%.2f,%.2f]",
                             static_cast<unsigned long>(kv.first), t,
                             static_cast<unsigned long long>(kv.second.templates[t].sequence[0]),
                             static_cast<unsigned long long>(kv.second.templates[t].sequence[1]),
                             static_cast<unsigned long long>(kv.second.templates[t].sequence[2]),
                             static_cast<unsigned long long>(kv.second.templates[t].sequence[3]),
                             static_cast<unsigned long long>(kv.second.templates[t].sequence[4]),
                             kv.second.templates[t].reliability[0], kv.second.templates[t].reliability[1],
                             kv.second.templates[t].reliability[2], kv.second.templates[t].reliability[3],
                             kv.second.templates[t].reliability[4]);
                    }
                }
                BLOG("[GUIDE] Total precondition pages loaded: %zu", this->_preconditionPages.size());
            } else {
                BLOG("[GUIDE] Companion file not found or failed to load: %s (this is normal for first run)", precondFilePath.c_str());
            }
        }

        delete[] modelFileData;
    }

    std::string ModelReusableAgent::DefaultModelSavePath = "/sdcard/fastbot.model.fbm";

    void ModelReusableAgent::saveReuseModel(const std::string &modelFilepath) {
        flatbuffers::FlatBufferBuilder builder;
        std::vector<flatbuffers::Offset<fastbotx::ReuseEntry>> actionActivityVector;

        {
            std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
            (void)reuseGuard;
            for (const auto &actionIterator : this->_reuseModel) {
                uint64_t actionHash = actionIterator.first;
                ReuseEntryM activityCountEntryMap = actionIterator.second;
                std::vector<flatbuffers::Offset<fastbotx::ActivityTimes>> activityCountEntryVector;
                for (const auto &activityCountEntry : activityCountEntryMap) {
                    auto sentryActT = CreateActivityTimes(builder, builder.CreateString(*activityCountEntry.first), activityCountEntry.second);
                    activityCountEntryVector.push_back(sentryActT);
                }
                if (!activityCountEntryVector.empty()) {
                    auto savedActivityCountEntries = CreateReuseEntry(builder, actionHash, builder.CreateVector(activityCountEntryVector.data(), activityCountEntryVector.size()));
                    actionActivityVector.push_back(savedActivityCountEntries);
                }
            }
        }

        // [DATA SEPARATION] Only save reuse model to .fbm, precondition_pages are saved to companion .precond file
        // Create empty/null preconditionPagesOffset to avoid writing precondition_pages to .fbm
        flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<fastbotx::PreconditionPage>>> preconditionPagesOffset = 0;
        BLOG("[GUIDE] Precondition pages will be saved to separate .precond file only (not in .fbm)");

        flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<fastbotx::ReuseEntry>>> actionActivityOffset = 0;
        if (!actionActivityVector.empty()) {
            actionActivityOffset = builder.CreateVector(actionActivityVector.data(), actionActivityVector.size());
        }

        auto savedReuseModelOffset = CreateReuseModel(
                builder,
                actionActivityOffset,
                preconditionPagesOffset
        );
        builder.Finish(savedReuseModelOffset);

        std::string outputFilePath = modelFilepath;
        if (outputFilePath.empty()) {
            outputFilePath = this->_defaultModelSavePath;
        }
        BLOG("save model to path: %s (reuse model only, no precondition pages)", outputFilePath.c_str());
        std::ofstream outputFile(outputFilePath, std::ios::binary);
        outputFile.write((char *) builder.GetBufferPointer(), static_cast<int>(builder.GetSize()));
        outputFile.close();
        BLOG("[GUIDE] Saved .fbm file: %zu actions in reuse model", this->_reuseModel.size());

        // --- Save all precondition pages to companion .precond file (complete separation) ---
        {
            std::string precondFilePath = outputFilePath + ".precond";
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            BLOG("[GUIDE] Saving %zu precondition pages to companion file: %s", this->_preconditionPages.size(), precondFilePath.c_str());
            bool saved = savePreconditionTemplatesToFile(precondFilePath, this->_preconditionPages);
            if (saved) {
                BLOG("[GUIDE] Successfully saved companion file: %s (%zu pages)", precondFilePath.c_str(), this->_preconditionPages.size());
            } else {
                BLOG("[GUIDE] Failed to save companion file: %s", precondFilePath.c_str());
            }
         }
     }

    //    void ModelReusableAgent::addCurrentPageAsPrecondition() {
    //        if (nullptr == this->_newState) {
    //            BLOG("addCurrentPageAsPrecondition: _newState is null");
    //            return;
    //        }
    //        uintptr_t currentPage = this->_newState->hash();
    //        std::lock_guard<std::mutex> guard(this->_preconditionLock);
    //        (void)guard;
    //        auto it = this->_preconditionPages.find(currentPage);
    //        if (it == this->_preconditionPages.end()) {
    //            PreconditionInfo newInfo;
    //            newInfo.score = 1.0;
    //            this->_preconditionPages[currentPage] = newInfo;
    //            BLOG("Added current page as a precondition page: %lu", static_cast<unsigned long>(currentPage));
    //        }
    //        // mark as covered for this episode to avoid immediate guidance
    //        this->_coveredPreconditionsThisEpisode.insert(currentPage);
    //        BLOG("Precondition pages count after add: %zu", this->_preconditionPages.size());
    //    }

    void ModelReusableAgent::addCurrentPageAsPrecondition(const StatePtr &state) {
        if (state == nullptr) {
            BLOG("addCurrentPageAsPrecondition(state): state is null");
            return;
        }
        uintptr_t currentPage = state->hash();
        std::lock_guard<std::mutex> guard(this->_preconditionLock);
        (void)guard;

        // Check if there's a pending guide action waiting for result
        bool hasPendingGuide = this->_hasPendingGuideCheck;
        uintptr_t guidedTargetPage = this->_pendingGuideTargetPage;

        BLOG("[GUIDE] addCurrentPageAsPrecondition: page=%lu, hasPendingGuide=%d, guidedTargetPage=%lu",
             static_cast<unsigned long>(currentPage), hasPendingGuide, static_cast<unsigned long>(guidedTargetPage));

        // --- Reward logic for guided action ---
        // Only process if there's a pending guide check
        if (hasPendingGuide && guidedTargetPage != 0) {
            // Mark that precondition was reached - this will be checked in checkPendingGuideResult
            this->_preconditionReachedSinceLastGuide = true;

            // Find the target page's templates and reward them
            auto targetIt = this->_preconditionPages.find(guidedTargetPage);
            if (targetIt != this->_preconditionPages.end()) {
                PreconditionInfo &targetInfo = targetIt->second;

                // Reward all templates of the target page (we reached a precondition page!)
                for (int t = 0; t < targetInfo.templateCount; ++t) {
                    // Check reward count limit
                    int &rewardCount = this->_templateRewardCounts[guidedTargetPage][t];
                    if (rewardCount >= MAX_TEMPLATE_REWARDS_PER_EPISODE) {
                        BLOG("[GUIDE] Template %d of page %lu already reached max rewards (%d), skipping reward",
                             t, static_cast<unsigned long>(guidedTargetPage), MAX_TEMPLATE_REWARDS_PER_EPISODE);
                        continue;
                    }
                    rewardCount++;

                    // Increase reliability for all non-zero positions in this template
                    for (int i = 0; i < MAX_TEMPLATE_SEQUENCE_LEN; ++i) {
                        if (targetInfo.templates[t].sequence[i] != 0) {
                            double oldRel = targetInfo.templates[t].reliability[i];
                            targetInfo.templates[t].reliability[i] = std::min(oldRel * 1.2, 1.0);
                            BLOG("[GUIDE] Rewarded template[%d] pos %d: reliability %.4f -> %.4f",
                                 t, i, oldRel, targetInfo.templates[t].reliability[i]);
                        }
                    }
                    BLOG("[GUIDE] Rewarded template %d of page %lu (rewardCount=%d)",
                         t, static_cast<unsigned long>(guidedTargetPage), rewardCount);
                }

                // Increase page score
                double oldScore = targetInfo.score;
                targetInfo.score = std::min(targetInfo.score * 1.5, 2.0);
                BLOG("[GUIDE] Increased score of page %lu: %.4f -> %.4f", static_cast<unsigned long>(guidedTargetPage), oldScore, targetInfo.score);

                // Reset failure counters using the pending guide action hash and target page
                this->_consecutiveFails[guidedTargetPage][this->_pendingGuideActionHash] = 0;
                this->_noProgressCount[guidedTargetPage] = 0;
                BLOG("[GUIDE] Reset failure counters for page %lu action %llu",
                     static_cast<unsigned long>(guidedTargetPage),
                     static_cast<unsigned long long>(this->_pendingGuideActionHash));
            }

            // Mark the guided target page as covered this episode (whether we reached it or not)
            // This prevents repeated guidance attempts to the same target in this episode
            if (this->_coveredPreconditionsThisEpisode.find(guidedTargetPage) == this->_coveredPreconditionsThisEpisode.end()) {
                this->_coveredPreconditionsThisEpisode.insert(guidedTargetPage);
                BLOG("[GUIDE] Marked guided target page %lu as covered (guide attempted)",
                     static_cast<unsigned long>(guidedTargetPage));
            }
            // NOTE: Don't clear _hasPendingGuideCheck here - it will be cleared in checkPendingGuideResult
        }

        // --- Normal logic: add/update precondition page ---
        auto it = this->_preconditionPages.find(currentPage);
        if (it == this->_preconditionPages.end()) {
            PreconditionInfo newInfo;
            newInfo.score = 1.0;
            newInfo.templateCount = 0;
            // initialize template slots
            for (int t = 0; t < 5; ++t) {
                newInfo.templates[t].length = 0;
                for (int j = 0; j < MAX_TEMPLATE_SEQUENCE_LEN; ++j) {
                    newInfo.templates[t].sequence[j] = 0;
                    newInfo.templates[t].reliability[j] = 0.5;
                }
            }
            this->_preconditionPages[currentPage] = newInfo;
            BLOG("[GUIDE] Added new precondition page: %lu", static_cast<unsigned long>(currentPage));
        }

        // --- Generate template from history for the current page ---
        PreconditionInfo &info = this->_preconditionPages[currentPage];

        std::vector<ActionPtr> history;
        // collect all previous actions (oldest first for template sequence)
        for (int i = 0; i < (int)this->_previousActions.size(); ++i) {
            history.push_back(this->_previousActions[i]);
        }
        BLOG("[GUIDE] addCurrentPageAsPrecondition: collected %zu history actions for page %lu",
             history.size(), static_cast<unsigned long>(currentPage));

        // record each action's success count
        for (auto &a : history) {
            if (a == nullptr) continue;
            uint64_t ah = 0;
            auto ana = std::dynamic_pointer_cast<ActivityNameAction>(a);
            if (ana) {
                ah = static_cast<uint64_t>(ana->hash());
            } else {
                ah = static_cast<uint64_t>(a->hash());
            }
            if (ah != 0) {
                info.actionList[ah] += 1;
            }
        }

        // Build sequence from history - only include non-zero action hashes
        std::vector<uint64_t> sequence;
        for (auto &a : history) {
            if (a == nullptr) continue;
            uint64_t ah = 0;
            auto ana = std::dynamic_pointer_cast<ActivityNameAction>(a);
            if (ana) {
                ah = static_cast<uint64_t>(ana->hash());
            } else {
                ah = static_cast<uint64_t>(a->hash());
            }
            // Only add non-zero action hashes
            if (ah != 0) {
                sequence.push_back(ah);
                if (sequence.size() >= MAX_TEMPLATE_SEQUENCE_LEN) break;
            }
        }

        if (!sequence.empty()) {
            BLOG("[GUIDE] Built sequence with %zu actions: [%llu,%llu,%llu,%llu,%llu]",
                 sequence.size(),
                 sequence.size() > 0 ? sequence[0] : 0,
                 sequence.size() > 1 ? sequence[1] : 0,
                 sequence.size() > 2 ? sequence[2] : 0,
                 sequence.size() > 3 ? sequence[3] : 0,
                 sequence.size() > 4 ? sequence[4] : 0);

            // Check if matches existing template
            bool matched = false;
            for (int t = 0; t < info.templateCount; ++t) {
                if (info.templates[t].length != static_cast<int>(sequence.size())) continue;
                bool eq = true;
                for (size_t i = 0; i < sequence.size(); ++i) {
                    if (info.templates[t].sequence[i] != sequence[i]) { eq = false; break; }
                }
                if (eq) {
                    matched = true;
                    BLOG("[GUIDE] Sequence matches existing template[%d]", t);
                    break;
                }
            }

            // Create new template if not matched
            if (!matched) {
                GuidancePathTemplate newT;
                newT.length = static_cast<int>(sequence.size());
                for (int i = 0; i < MAX_TEMPLATE_SEQUENCE_LEN; ++i) {
                    newT.sequence[i] = 0;
                    newT.reliability[i] = 1.0; // start with high reliability
                }
                for (size_t i = 0; i < sequence.size() && i < MAX_TEMPLATE_SEQUENCE_LEN; ++i) {
                    newT.sequence[i] = sequence[i];
                }

                // FIFO insert
                if (info.templateCount < 5) {
                    for (int t = info.templateCount; t > 0; --t) {
                        info.templates[t] = info.templates[t-1];
                    }
                    info.templates[0] = newT;
                    info.templateCount++;
                    BLOG("[GUIDE] Created new template[0] for page %lu: length=%d, seq=[%llu,%llu,%llu,%llu,%llu], templateCount=%d",
                         static_cast<unsigned long>(currentPage), newT.length,
                         static_cast<unsigned long long>(newT.sequence[0]),
                         static_cast<unsigned long long>(newT.sequence[1]),
                         static_cast<unsigned long long>(newT.sequence[2]),
                         static_cast<unsigned long long>(newT.sequence[3]),
                         static_cast<unsigned long long>(newT.sequence[4]),
                         info.templateCount);
                } else {
                    for (int t = 4; t > 0; --t) {
                        info.templates[t] = info.templates[t-1];
                    }
                    info.templates[0] = newT;
                    BLOG("[GUIDE] Replaced oldest template (FIFO) for page %lu: length=%d, seq=[%llu,%llu,%llu,%llu,%llu]",
                         static_cast<unsigned long>(currentPage), newT.length,
                         static_cast<unsigned long long>(newT.sequence[0]),
                         static_cast<unsigned long long>(newT.sequence[1]),
                         static_cast<unsigned long long>(newT.sequence[2]),
                         static_cast<unsigned long long>(newT.sequence[3]),
                         static_cast<unsigned long long>(newT.sequence[4]));
                }
            }
        } else {
            BLOG("[GUIDE] No valid actions in history to create template for page %lu", static_cast<unsigned long>(currentPage));
        }

        this->_coveredPreconditionsThisEpisode.insert(currentPage);
        this->_guidanceAttemptCounts.erase(currentPage);
        BLOG("[GUIDE] addCurrentPageAsPrecondition completed for page %lu, marked as covered", static_cast<unsigned long>(currentPage));
    }

    void ModelReusableAgent::beginNewEpisode() {
        std::lock_guard<std::mutex> guard(this->_preconditionLock);
        (void)guard;
        this->_coveredPreconditionsThisEpisode.clear();
        // reset guidance attempt counts so attempts are per-episode
        this->_guidanceAttemptCounts.clear();
        // reset pending guide check
        this->_hasPendingGuideCheck = false;
        this->_pendingGuideActionHash = 0;
        this->_pendingGuideTargetPage = 0;
        this->_preconditionReachedSinceLastGuide = false;
        // reset failure counters (per-action and per-page)
        this->_consecutiveFails.clear();
        this->_noProgressCount.clear();
        this->_pendingGuidedTargets.clear();
        this->_pendingGuidedAges.clear();
        // reset selection and reward counts per episode
        this->_templateRewardCounts.clear();
        this->_templateSelectionCounts.clear();
        this->_pageSelectionCounts.clear();
        BLOG("[GUIDE] beginNewEpisode: all guided state reset");
    }

    void ModelReusableAgent::processGuidedActionResult(const ActionPtr &action, uintptr_t targetPageHash, bool reached, const std::vector<uint64_t> &episodePath) {
        if (action == nullptr) return;
        uint64_t ah = static_cast<uint64_t>(action->hash());
        if (reached) {
            // success: reset consecutive fail for (page, action) and noProgress counter for this page
            this->_consecutiveFails[targetPageHash][ah] = 0;
            this->_noProgressCount[targetPageHash] = 0;
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            auto it = this->_preconditionPages.find(targetPageHash);
            if (it == this->_preconditionPages.end()) return;
            PreconditionInfo &info = it->second;
            // increase reliability for matching slots
            for (int t = 0; t < info.templateCount; ++t) {
                for (int i = 0; i < 6; ++i) {
                    if (info.templates[t].sequence[i] == ah) {
                        info.templates[t].reliability[i] = std::min(info.templates[t].reliability[i] * 1.2, 1.0);
                    }
                }
            }
            // increase score
            info.score = std::min(info.score * 1.5, 2.0);
            // record new template if episodePath doesn't match any
            bool matched = false;
            if (!episodePath.empty()) {
                for (int t = 0; t < info.templateCount; ++t) {
                    bool eq = true;
                    for (size_t i = 0; i < episodePath.size() && i < 6; ++i) {
                        if (info.templates[t].sequence[i] != episodePath[i]) { eq = false; break; }
                    }
                    if (eq) { matched = true; break; }
                }
                if (!matched) {
                    GuidancePathTemplate newT;
                    for (size_t i = 0; i < episodePath.size() && i < 6; ++i) newT.sequence[i] = episodePath[i];
                    for (size_t i = episodePath.size(); i < 6; ++i) newT.sequence[i] = 0;
                    if (info.templateCount < 5) {
                        for (int t = info.templateCount; t > 0; --t) info.templates[t] = info.templates[t-1];
                        info.templates[0] = newT;
                        info.templateCount++;
                    } else {
                        for (int t = 4; t > 0; --t) info.templates[t] = info.templates[t-1];
                        info.templates[0] = newT;
                    }
                }
            }
        } else {
            // failure
            this->_consecutiveFails[targetPageHash][ah]++;
            this->_noProgressCount[targetPageHash]++;
            // decrease reliability for templates containing this action
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            for (auto &kv : this->_preconditionPages) {
                PreconditionInfo &info = kv.second;
                for (int t = 0; t < info.templateCount; ++t) {
                    for (int i = 0; i < 6; ++i) {
                        if (info.templates[t].sequence[i] == ah) {
                            info.templates[t].reliability[i] *= 0.5;
                        }
                    }
                }
            }
            // global decay
            for (auto &kv : this->_preconditionPages) {
                PreconditionInfo &info = kv.second;
                for (int t = 0; t < info.templateCount; ++t) for (int i = 0; i < 6; ++i) info.templates[t].reliability[i] *= 0.95;
            }
            // page-level stop if noProgressCount >=3 for this page
            auto pit = this->_preconditionPages.find(targetPageHash);
            if (pit != this->_preconditionPages.end()) {
                PreconditionInfo &targetInfo = pit->second;
                if (this->_noProgressCount[targetPageHash] >= 3) {
                    targetInfo.score = std::max(targetInfo.score * 0.3, 0.1);
                    this->_coveredPreconditionsThisEpisode.insert(targetPageHash);
                }
            }
            // action-level blacklist if consecutiveFails >=3 for this (page, action)
            if (this->_consecutiveFails[targetPageHash][ah] >= 3) {
                // Only blacklist in the target page's templates
                auto pit2 = this->_preconditionPages.find(targetPageHash);
                if (pit2 != this->_preconditionPages.end()) {
                    PreconditionInfo &info = pit2->second;
                    for (int t = 0; t < info.templateCount; ++t) {
                        for (int i = 0; i < 6; ++i) {
                            if (info.templates[t].sequence[i] == ah) info.templates[t].reliability[i] = 0.0;
                        }
                    }
                }
            }
        }
        // summary log for diagnostics
        BLOG("[GUIDE] exit processGuidedActionResult: action_hash=%llu targetPage=%lu reached=%d noProgressCount[page]=%d consecutiveFails[page][action]=%d",
             static_cast<unsigned long long>(ah), static_cast<unsigned long>(targetPageHash), reached, this->_noProgressCount[targetPageHash],
             this->_consecutiveFails[targetPageHash][ah]);
    }

    void ModelReusableAgent::updateReuseModel() {
        if (this->_previousActions.empty())
            return;
        ActionPtr lastAction = this->_previousActions.back();
        ActivityNameActionPtr modelAction = std::dynamic_pointer_cast<ActivityNameAction>(
                lastAction);
        if (nullptr == modelAction || nullptr == this->_newState)
            return;
        auto hash = (uint64_t) modelAction->hash();
        stringPtr activity = this->_newState->getActivityString(); // mark: use the _newstate as last selected action's target
        if (activity == nullptr)
            return;
        {
            std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
            (void)reuseGuard;
            auto iter = this->_reuseModel.find(hash);
            if (iter == this->_reuseModel.end()) {
                BDLOG("can not find action %s in reuse map", modelAction->getId().c_str());
                ReuseEntryM entryMap;
                entryMap.emplace(std::make_pair(activity, 1));
                this->_reuseModel[hash] = entryMap;
            } else {
                ((*iter).second)[activity] += 1;
            }
            this->_reuseQValue[hash] = modelAction->getQValue();
        }
    }


    void ModelReusableAgent::processGuidedActionResultByHash(uint64_t actionHash, uintptr_t targetPageHash, bool reached) {
        // If we can find an ActionPtr, delegate to the main handler which also handles template insertion logic.
        ActionPtr found = nullptr;
        for (auto &a : this->_previousActions) { if (a && static_cast<uint64_t>(a->hash()) == actionHash) { found = a; break; } }
        if (!found && this->_newAction && static_cast<uint64_t>(this->_newAction->hash()) == actionHash) found = this->_newAction;
        if (found) {
            std::vector<uint64_t> emptyPath;
            this->processGuidedActionResult(found, targetPageHash, reached, emptyPath);
            // cleanup pending entries
            this->_pendingGuidedTargets.erase(actionHash);
            this->_pendingGuidedAges.erase(actionHash);
            return;
        }

        // Fallback: no ActionPtr available — apply updates directly using actionHash.
        if (reached) {
            this->_consecutiveFails[targetPageHash][actionHash] = 0;
            this->_noProgressCount[targetPageHash] = 0;
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            auto it = this->_preconditionPages.find(targetPageHash);
            if (it == this->_preconditionPages.end()) {
                // nothing to update
                this->_pendingGuidedTargets.erase(actionHash);
                this->_pendingGuidedAges.erase(actionHash);
                return;
            }
            PreconditionInfo &info = it->second;
            for (int t = 0; t < info.templateCount; ++t) {
                for (int i = 0; i < 6; ++i) {
                    if (info.templates[t].sequence[i] == actionHash) {
                        info.templates[t].reliability[i] = std::min(info.templates[t].reliability[i] * 1.2, 1.0);
                    }
                }
            }
            info.score = std::min(info.score * 1.5, 2.0);
            // cleanup pending
            this->_pendingGuidedTargets.erase(actionHash);
            this->_pendingGuidedAges.erase(actionHash);
            return;
        }

        // Failure case when no ActionPtr: apply penalties
        this->_consecutiveFails[targetPageHash][actionHash]++;
        this->_noProgressCount[targetPageHash]++;
        {
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            // Only decrease reliability in target page's templates
            auto pit = this->_preconditionPages.find(targetPageHash);
            if (pit != this->_preconditionPages.end()) {
                PreconditionInfo &info = pit->second;
                for (int t = 0; t < info.templateCount; ++t) for (int i = 0; i < 6; ++i) {
                    if (info.templates[t].sequence[i] == actionHash) info.templates[t].reliability[i] *= 0.5;
                }
                // global decay for this page
                for (int t = 0; t < info.templateCount; ++t) for (int i = 0; i < 6; ++i) info.templates[t].reliability[i] *= 0.95;
            }
            // page-level stop if noProgressCount >=3 for this page
            if (pit != this->_preconditionPages.end()) {
                PreconditionInfo &targetInfo = pit->second;
                if (this->_noProgressCount[targetPageHash] >= 3) {
                    targetInfo.score = std::max(targetInfo.score * 0.3, 0.1);
                    this->_coveredPreconditionsThisEpisode.insert(targetPageHash);
                }
            }
            // action-level blacklist if consecutiveFails >=3 for this (page, action)
            if (this->_consecutiveFails[targetPageHash][actionHash] >= 3) {
                if (pit != this->_preconditionPages.end()) {
                    PreconditionInfo &info = pit->second;
                    for (int t = 0; t < info.templateCount; ++t) {
                        for (int i = 0; i < 6; ++i) {
                            if (info.templates[t].sequence[i] == actionHash) info.templates[t].reliability[i] = 0.0;
                        }
                    }
                }
            }
        }
        // cleanup pending
        this->_pendingGuidedTargets.erase(actionHash);
        this->_pendingGuidedAges.erase(actionHash);
        BLOG("[GUIDE] exit processGuidedActionResultByHash: action_hash=%llu targetPage=%lu reached=%d noProgressCount[page]=%d consecutiveFails[page][action]=%d",
             static_cast<unsigned long long>(actionHash), static_cast<unsigned long>(targetPageHash), reached, this->_noProgressCount[targetPageHash],
             this->_consecutiveFails[targetPageHash][actionHash]);
    }


} // namespace fastbotx

#endif /* fastbotx_ModelReusableAgent_CPP_ */

