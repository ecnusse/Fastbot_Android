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
        uint32_t version = 1;
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
                for (int i = 0; i < 6; ++i) {
                    uint64_t seq = static_cast<uint64_t>(pt.sequence[i]);
                    out.write(reinterpret_cast<const char *>(&seq), sizeof(seq));
                }
                for (int i = 0; i < 6; ++i) {
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
        if (version != 1) { in.close(); return false; }
        uint64_t entryCount = 0;
        in.read(reinterpret_cast<char *>(&entryCount), sizeof(entryCount));
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
                for (int i = 0; i < 6; ++i) {
                    uint64_t seq = 0;
                    in.read(reinterpret_cast<char *>(&seq), sizeof(seq));
                    info.templates[t].sequence[i] = static_cast<uint64_t>(seq);
                }
                for (int i = 0; i < 6; ++i) {
                    double rel = 0.0;
                    in.read(reinterpret_cast<char *>(&rel), sizeof(rel));
                    info.templates[t].reliability[i] = rel;
                }
            }
            pages[static_cast<uintptr_t>(pageHash)] = info;
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
        for (int t = 0; t < info.templateCount; ++t) {
            auto fbT = fbTemplates->Get(t);
            if (!fbT) continue;
            // sequence
            if (fbT->sequence() != nullptr) {
                auto seqVec = fbT->sequence();
                for (int i = 0; i < 6; ++i) {
                    if (i < static_cast<int>(seqVec->size())) info.templates[t].sequence[i] = seqVec->Get(i);
                    else info.templates[t].sequence[i] = 0;
                }
            } else {
                for (int i = 0; i < 6; ++i) info.templates[t].sequence[i] = 0;
            }
            // reliability
            if (fbT->reliability() != nullptr) {
                auto relVec = fbT->reliability();
                for (int i = 0; i < 6; ++i) {
                    if (i < static_cast<int>(relVec->size())) info.templates[t].reliability[i] = relVec->Get(i);
                    else info.templates[t].reliability[i] = 0.0;
                }
            } else {
                for (int i = 0; i < 6; ++i) info.templates[t].reliability[i] = 0.0;
            }
        }
        // initialize unused template slots
        for (int t = info.templateCount; t < 5; ++t) {
            for (int i = 0; i < 6; ++i) {
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

            // --- Process pending guided action feedback ---
            // Check if the last action was a guided action and process its result
            if (!this->_previousActions.empty() && this->_newState != nullptr) {
                ActionPtr lastAction = this->_previousActions.back();
                if (lastAction != nullptr) {
                    uint64_t lastActionHash = static_cast<uint64_t>(lastAction->hash());
                    uintptr_t currentPageHash = this->_newState->hash();

                    // Check if this action had a pending guided target
                    auto pendingIt = this->_pendingGuidedTargets.find(lastActionHash);
                    if (pendingIt != this->_pendingGuidedTargets.end()) {
                        uintptr_t targetPageHash = pendingIt->second;
                        bool reached = (currentPageHash == targetPageHash);

                        BLOG("[GUIDE] Processing guided action result: action=%llu targetPage=%lu currentPage=%lu reached=%d",
                             static_cast<unsigned long long>(lastActionHash),
                             static_cast<unsigned long>(targetPageHash),
                             static_cast<unsigned long>(currentPageHash),
                             reached);

                        // Call the feedback handler
                        std::vector<uint64_t> episodePath; // empty for now
                        this->processGuidedActionResult(lastAction, targetPageHash, reached, episodePath);

                        // Remove from pending
                        this->_pendingGuidedTargets.erase(pendingIt);
                        this->_pendingGuidedAges.erase(lastActionHash);
                    }
                }
            }

            // --- Record history of actions to precondition model when arriving at a known precondition page ---
            // Skip entirely if there is no new state or if the current page is not a persisted precondition page.
            if (nullptr == this->_newState) {
                // nothing to do
            } else {
                uintptr_t currentPage = this->_newState->hash();
                uintptr_t lastPage = 0;
                if (this->_currentState) lastPage = this->_currentState->hash();
                std::lock_guard<std::mutex> guard(this->_preconditionLock);
                (void)guard;
                // Only record history if currentPage already exists in the persisted _preconditionPages map.
                auto it = this->_preconditionPages.find(currentPage);
                if (it == this->_preconditionPages.end()) {
                    // not a known precondition page => do nothing here (do NOT auto-add)
                } else {
                    // only record history when we actually just arrived (avoid duplicate records when staying on page)
                    if (currentPage != lastPage) {
                        PreconditionInfo &info = it->second;
                        BLOG("[GUIDE] updateStrategy: arrived at precondition page %lu (lastPage=%lu). Recording history...", static_cast<unsigned long>(currentPage), static_cast<unsigned long>(lastPage));
                        // collect historical actions from _previousActions only
                        // NOTE: Do NOT include _newAction - it will be added to _previousActions at the END of this function
                        std::vector<ActionPtr> history;
                        // collect all previous actions (these are already executed actions, oldest first for template)
                        for (int i = 0; i < (int)this->_previousActions.size(); ++i) {
                            history.push_back(this->_previousActions[i]);
                        }
                        BLOG("[GUIDE] updateStrategy: collected %zu history actions", history.size());

                        // record each action's success count for this precondition page
                        for (auto &a : history) {
                            if (a == nullptr) continue;
                            uint64_t ah = 0;
                            auto ana = std::dynamic_pointer_cast<ActivityNameAction>(a);
                            if (ana) {
                                ah = static_cast<uint64_t>(ana->hash());
                            } else {
                                ah = static_cast<uint64_t>(a->hash());
                            }
                            info.actionList[ah] += 1;
                        }

                        // --- Generate template from history ---
                        // Build sequence from history (up to 6 actions, oldest to newest)
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
                            sequence.push_back(ah);
                            if (sequence.size() >= 6) break; // max 6 actions in template
                        }

                        BLOG("[GUIDE] updateStrategy: built sequence with %zu actions for template", sequence.size());

                        // Check if this sequence matches any existing template
                        bool matched = false;
                        for (int t = 0; t < info.templateCount; ++t) {
                            bool eq = true;
                            for (size_t i = 0; i < sequence.size() && i < 6; ++i) {
                                if (info.templates[t].sequence[i] != sequence[i]) { eq = false; break; }
                            }
                            if (eq) { matched = true; break; }
                        }

                        // If not matched, create new template (FIFO)
                        if (!matched && !sequence.empty()) {
                            GuidancePathTemplate newT;
                            // Initialize
                            for (int i = 0; i < 6; ++i) {
                                newT.sequence[i] = 0;
                                newT.reliability[i] = 1.0; // start with high reliability
                            }
                            // Copy sequence
                            for (size_t i = 0; i < sequence.size() && i < 6; ++i) {
                                newT.sequence[i] = sequence[i];
                            }

                            // FIFO insert at head
                            if (info.templateCount < 5) {
                                // Shift existing templates
                                for (int t = info.templateCount; t > 0; --t) {
                                    info.templates[t] = info.templates[t-1];
                                }
                                info.templates[0] = newT;
                                info.templateCount++;
                                BLOG("[GUIDE] updateStrategy: created new template at index 0, templateCount=%d", info.templateCount);
                            } else {
                                // Full, replace oldest (shift and insert at head)
                                for (int t = 4; t > 0; --t) {
                                    info.templates[t] = info.templates[t-1];
                                }
                                info.templates[0] = newT;
                                BLOG("[GUIDE] updateStrategy: replaced oldest template (FIFO), templateCount=%d", info.templateCount);
                            }

                            // Log the new template
                            BLOG("[GUIDE] updateStrategy: new template sequence=[%llu,%llu,%llu,%llu,%llu,%llu]",
                                 static_cast<unsigned long long>(newT.sequence[0]),
                                 static_cast<unsigned long long>(newT.sequence[1]),
                                 static_cast<unsigned long long>(newT.sequence[2]),
                                 static_cast<unsigned long long>(newT.sequence[3]),
                                 static_cast<unsigned long long>(newT.sequence[4]),
                                 static_cast<unsigned long long>(newT.sequence[5]));
                        } else if (matched) {
                            BLOG("[GUIDE] updateStrategy: sequence matches existing template, skipping creation");
                        }

                        // decay score (long-term importance)
                        double oldScore = info.score;
                        info.score = std::max(info.score * this->_hit_decay, this->_min_score);
                        BLOG("[GUIDE] updateStrategy: precondition page %lu score: %f -> %f", static_cast<unsigned long>(currentPage), oldScore, info.score);
                    }
                    else {
                        BDLOG("[TRACE] updateStrategy: currentPage == lastPage (%lu). Skipping history recording to avoid duplicate.", static_cast<unsigned long>(currentPage));
                    }
                    // mark covered this episode so we don't repeatedly guide it
                    this->_coveredPreconditionsThisEpisode.insert(currentPage);
                    // clear per-page attempt counts for this precondition page since it's now covered
                    this->_guidanceAttemptCounts.erase(currentPage);
                    BDLOG("[TRACE] updateStrategy: marked precondition page %lu as covered in this episode (covered count=%zu)", static_cast<unsigned long>(currentPage), this->_coveredPreconditionsThisEpisode.size());
                }
            }
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

        // 1. Build candidates: pages not covered this episode, score>=0.3, have templates
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
                candidates.emplace_back(pageHash, &info);
                BLOG("[GUIDE] candidate page %lu: score=%.2f, templateCount=%d",
                     static_cast<unsigned long>(pageHash), info.score, info.templateCount);
            }
        }

        if (candidates.empty()) {
            BLOG("[GUIDE] selectGuidedActionForPrecondition: no candidates, returning null");
            return nullptr;
        }
        BLOG("[GUIDE] selectGuidedActionForPrecondition: %zu candidate pages", candidates.size());

        // 2. Global decay on candidate templates' reliabilities (spec allows decaying at selection time)
        // Note: candidates hold pointers into _preconditionPages, so no lock needed here as we already built them
        for (auto &c : candidates) {
            PreconditionInfo *info = c.second;
            for (int t = 0; t < info->templateCount; ++t) {
                for (int i = 0; i < 6; ++i) info->templates[t].reliability[i] *= 0.95;
            }
        }

        // 3. Prepare map of actions on current page
        std::unordered_map<uint64_t, ActionPtr> pageActions;
        for (auto &a : this->_newState->getActions()) pageActions[static_cast<uint64_t>(a->hash())] = a;
        BLOG("[GUIDE] selectGuidedActionForPrecondition: current page has %zu actions", pageActions.size());

        // 4. Evaluate templates: reversed positions
        ActionPtr bestAction = nullptr;
        double bestScore = -1.0;
        uintptr_t bestPage = 0;
        int bestPos = -1;

        for (auto &entry : candidates) {
            uintptr_t pageHash = entry.first;
            PreconditionInfo *info = entry.second;
            for (int t = 0; t < info->templateCount; ++t) {
                GuidancePathTemplate &templ = info->templates[t];
                for (int pos = 5; pos >= 0; --pos) {
                    uint64_t actionHash = templ.sequence[pos];
                    if (actionHash == 0) continue;
                    auto it = pageActions.find(actionHash);
                    if (it == pageActions.end()) continue;
                    ActionPtr action = it->second;
                    int cf = 0;
                    auto cfit = this->_consecutiveFails.find(actionHash);
                    if (cfit != this->_consecutiveFails.end()) cf = cfit->second;
                    if (cf >= 3) { templ.reliability[pos] = 0.0; continue; }
                    double P = static_cast<double>(pos) / 5.0;
                    double R = templ.reliability[pos];
                    double C = info->score / 2.0;
                    double score = 0.5 * P + 0.3 * R + 0.2 * C;
                    if (score > bestScore) {
                        bestScore = score;
                        bestAction = action;
                        bestPage = pageHash;
                        bestPos = pos;
                    }
                    break; // stop after first valid position in this template
                }
            }
        }

        if (!bestAction) return nullptr;
        this->_lastPosition[bestPage] = bestPos;
        this->_guidanceAttemptCounts[bestPage][static_cast<uint64_t>(bestAction->hash())]++;
        // set pending guided target so feedback can be applied when action result is observed
        this->setPendingGuidedTarget(static_cast<uint64_t>(bestAction->hash()), bestPage);
        BLOG("[GUIDE] exit selectGuidedActionForPrecondition: chosen_hash=%llu page=%lu pos=%d bestScore=%.4f attemptsForPage=%d",
             static_cast<unsigned long long>(bestAction->hash()), static_cast<unsigned long>(bestPage), bestPos, bestScore,
             this->_guidanceAttemptCounts[bestPage][static_cast<uint64_t>(bestAction->hash())]);
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

    /// If the new action is generated,
    ActionPtr ModelReusableAgent::selectNewAction() {
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

        // read precondition pages (score and action_counts persisted in flatbuffer schema)
        {
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            (void)guard;
            this->_preconditionPages.clear();
            BLOG("[GUIDE] Loading precondition pages from flatbuffer...");
            if (reuseFBModel->precondition_pages() != nullptr) {
                auto preconditionPages = reuseFBModel->precondition_pages();
                BLOG("[GUIDE] Found %d precondition pages in flatbuffer", preconditionPages->size());
                for (int i = 0; i < preconditionPages->size(); ++i) {
                    auto page = preconditionPages->Get(i);
                    uintptr_t pageName = (uintptr_t) page->hashcode();
                    // NOTE: The persisted schema contains score and action_counts for precondition pages.
                    double persistedScore = page->score();
                    PreconditionInfo info;
                    // use persisted score if > 0, otherwise default to 1.0
                    info.score = (persistedScore > 0.0) ? persistedScore : 1.0;
                    info.templateCount = 0; // initialize, will be set by loadTemplatesFromFBPage
                    // initialize template slots
                    for (int t = 0; t < 5; ++t) {
                        for (int j = 0; j < 6; ++j) {
                            info.templates[t].sequence[j] = 0;
                            info.templates[t].reliability[j] = 0.0;
                        }
                    }
                    // load action_counts if present
                    if (page->action_counts() != nullptr) {
                        auto actionCountsVec = page->action_counts();
                        for (int j = 0; j < actionCountsVec->size(); ++j) {
                            auto ac = actionCountsVec->Get(j);
                            uint64_t ah = ac->action();
                            int times = ac->times();
                            info.actionList[ah] = times;
                        }
                        BLOG("[GUIDE] Loaded page %lu: score=%f, actionList.size=%zu",
                             static_cast<unsigned long>(pageName), info.score, info.actionList.size());
                    }
                    // load templates from flatbuffer page when schema provides them (compatible)
                    loadTemplatesFromFBPage(page, info);
                    BLOG("[GUIDE] Loaded page %lu: templateCount=%d", static_cast<unsigned long>(pageName), info.templateCount);
                    // Log each template's sequence
                    for (int t = 0; t < info.templateCount; ++t) {
                        BLOG("[GUIDE] Page %lu template[%d]: seq=[%llu,%llu,%llu,%llu,%llu,%llu] rel=[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]",
                             static_cast<unsigned long>(pageName), t,
                             static_cast<unsigned long long>(info.templates[t].sequence[0]),
                             static_cast<unsigned long long>(info.templates[t].sequence[1]),
                             static_cast<unsigned long long>(info.templates[t].sequence[2]),
                             static_cast<unsigned long long>(info.templates[t].sequence[3]),
                             static_cast<unsigned long long>(info.templates[t].sequence[4]),
                             static_cast<unsigned long long>(info.templates[t].sequence[5]),
                             info.templates[t].reliability[0], info.templates[t].reliability[1],
                             info.templates[t].reliability[2], info.templates[t].reliability[3],
                             info.templates[t].reliability[4], info.templates[t].reliability[5]);
                    }
                    this->_preconditionPages[pageName] = info;
                }
                BLOG("[GUIDE] Loaded %zu precondition pages from flatbuffer", this->_preconditionPages.size());
            } else {
                BLOG("[GUIDE] No precondition pages found in flatbuffer model.");
            }
        }
        // --- Load companion precondition templates file if exists ---
        {
            std::string precondFilePath = modelFilePath + ".precond";
            BLOG("[GUIDE] Trying to load companion file: %s", precondFilePath.c_str());
            std::unordered_map<uintptr_t, PreconditionInfo> filePages;
            if (loadPreconditionTemplatesFromFile(precondFilePath, filePages)) {
                BLOG("[GUIDE] Loaded %zu pages from companion file", filePages.size());
                std::lock_guard<std::mutex> guard(this->_preconditionLock);
                for (const auto &kv : filePages) {
                    auto it = this->_preconditionPages.find(kv.first);
                    if (it != this->_preconditionPages.end()) {
                        // overwrite templates, templateCount and score from file
                        it->second.templateCount = kv.second.templateCount;
                        it->second.score = kv.second.score;
                        for (int t = 0; t < 5; ++t) it->second.templates[t] = kv.second.templates[t];
                        BLOG("[GUIDE] Merged page %lu: templateCount=%d, score=%f from companion file",
                             static_cast<unsigned long>(kv.first), kv.second.templateCount, kv.second.score);
                    } else {
                        // insert new page from companion file
                        this->_preconditionPages[kv.first] = kv.second;
                        BLOG("[GUIDE] Inserted new page %lu: templateCount=%d, score=%f from companion file",
                             static_cast<unsigned long>(kv.first), kv.second.templateCount, kv.second.score);
                    }
                }
                BLOG("[GUIDE] After merge, total precondition pages: %zu", this->_preconditionPages.size());
            } else {
                BLOG("[GUIDE] Companion file not found or failed to load: %s", precondFilePath.c_str());
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
                auto savedActivityCountEntries = CreateReuseEntry(builder, actionHash, builder.CreateVector(activityCountEntryVector.data(), activityCountEntryVector.size()));
                actionActivityVector.push_back(savedActivityCountEntries);
            }
        }

        // save precondition pages (persist score and action_counts)
        std::vector<flatbuffers::Offset<fastbotx::PreconditionPage>> preconditionPagesVector;
        {
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            (void)guard;
            BLOG("[GUIDE] Saving %zu precondition pages...", this->_preconditionPages.size());
            for (const auto &entry : this->_preconditionPages) {
                // persist score along with visited flag
                std::vector<flatbuffers::Offset<fastbotx::ActionCounts>> actionCountVec;
                for (const auto &ac : entry.second.actionList) {
                    auto acOffset = CreateActionCounts(builder, ac.first, ac.second);
                    actionCountVec.push_back(acOffset);
                }
                flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<fastbotx::ActionCounts>>> actionCountsOffset = 0;
                if (!actionCountVec.empty()) actionCountsOffset = builder.CreateVector(actionCountVec.data(), actionCountVec.size());
                // Create PreconditionPage with (hashcode, score, action_counts) according to updated schema
                // prepare templates if any
                flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<fastbotx::PathTemplate>>> templatesOffset = 0;
                if (entry.second.templateCount > 0) {
                    std::vector<flatbuffers::Offset<fastbotx::PathTemplate>> fbTemplates;
                    for (int t = 0; t < entry.second.templateCount && t < 5; ++t) {
                        const fastbotx::GuidancePathTemplate &pt = entry.second.templates[t];
                        std::vector<uint64_t> seqVec;
                        std::vector<double> relVec;
                        for (int i = 0; i < 6; ++i) {
                            seqVec.push_back(pt.sequence[i]);
                            relVec.push_back(pt.reliability[i]);
                        }
                        auto seqOff = builder.CreateVector<uint64_t>(seqVec);
                        auto relOff = builder.CreateVector<double>(relVec);
                        auto fbPt = fastbotx::CreatePathTemplate(builder, seqOff, relOff);
                        fbTemplates.push_back(fbPt);
                    }
                    templatesOffset = builder.CreateVector(fbTemplates.data(), fbTemplates.size());
                }
                auto pageOffset = CreatePreconditionPage(builder, entry.first, entry.second.score, actionCountsOffset, templatesOffset);

                // Log details for each page
                BLOG("[GUIDE] Saving page %lu: score=%f, actionList.size=%zu, templateCount=%d",
                     static_cast<unsigned long>(entry.first), entry.second.score,
                     entry.second.actionList.size(), entry.second.templateCount);
                // Log each template
                for (int t = 0; t < entry.second.templateCount; ++t) {
                    BLOG("[GUIDE] Page %lu template[%d]: seq=[%llu,%llu,%llu,%llu,%llu,%llu]",
                         static_cast<unsigned long>(entry.first), t,
                         static_cast<unsigned long long>(entry.second.templates[t].sequence[0]),
                         static_cast<unsigned long long>(entry.second.templates[t].sequence[1]),
                         static_cast<unsigned long long>(entry.second.templates[t].sequence[2]),
                         static_cast<unsigned long long>(entry.second.templates[t].sequence[3]),
                         static_cast<unsigned long long>(entry.second.templates[t].sequence[4]),
                         static_cast<unsigned long long>(entry.second.templates[t].sequence[5]));
                }
                preconditionPagesVector.push_back(pageOffset);
            }
        }
        auto preconditionPagesOffset = builder.CreateVector(preconditionPagesVector.data(), preconditionPagesVector.size());

        auto savedReuseModelOffset = CreateReuseModel(
                builder,
                builder.CreateVector(actionActivityVector.data(), actionActivityVector.size()),
                preconditionPagesOffset
        );
        builder.Finish(savedReuseModelOffset);

        std::string outputFilePath = modelFilepath;
        if (outputFilePath.empty()) {
            outputFilePath = this->_defaultModelSavePath;
        }
        BLOG("save model to path: %s", outputFilePath.c_str());
        std::ofstream outputFile(outputFilePath);
        outputFile.write((char *) builder.GetBufferPointer(), static_cast<int>(builder.GetSize()));
        outputFile.close();

        // --- Save companion precondition templates to file ---
        {
            std::string precondFilePath = outputFilePath + ".precond";
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            bool saved = savePreconditionTemplatesToFile(precondFilePath, this->_preconditionPages);
            if (saved) {
                BLOG("[GUIDE] Saved companion file: %s (%zu pages)", precondFilePath.c_str(), this->_preconditionPages.size());
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
        auto it = this->_preconditionPages.find(currentPage);
        if (it == this->_preconditionPages.end()) {
            PreconditionInfo newInfo;
            newInfo.score = 1.0;
            newInfo.templateCount = 0;
            // initialize template slots
            for (int t = 0; t < 5; ++t) {
                for (int i = 0; i < 6; ++i) {
                    newInfo.templates[t].sequence[i] = 0;
                    newInfo.templates[t].reliability[i] = 0.0;
                }
            }
            this->_preconditionPages[currentPage] = newInfo;
            BLOG("Added current page (from external state) as a precondition page: %lu", static_cast<unsigned long>(currentPage));
        } else {
            BLOG("addCurrentPageAsPrecondition(state): page already present %lu", static_cast<unsigned long>(currentPage));
        }
        // --- Record recent history actions into the precondition's actionList (no state checks) ---
        // NOTE: Only use _previousActions. _newAction is always null when called externally via JNI
        // (cleared by moveForward at the end of getOperate)
        {
            PreconditionInfo &info = this->_preconditionPages[currentPage];

            // Debug: print _previousActions
            BLOG("[GUIDE] addCurrentPageAsPrecondition: _previousActions.size()=%zu", this->_previousActions.size());
            for (int i = 0; i < (int)this->_previousActions.size(); ++i) {
                if (this->_previousActions[i] != nullptr) {
                    BLOG("[GUIDE] addCurrentPageAsPrecondition: _previousActions[%d] hash=%llu, toString=%s",
                         i,
                         static_cast<unsigned long long>(this->_previousActions[i]->hash()),
                         this->_previousActions[i]->toString().c_str());
                } else {
                    BLOG("[GUIDE] addCurrentPageAsPrecondition: _previousActions[%d] is null", i);
                }
            }

            std::vector<ActionPtr> history;
            // collect all previous actions (oldest first for template sequence)
            for (int i = 0; i < (int)this->_previousActions.size(); ++i) {
                history.push_back(this->_previousActions[i]);
            }
            BLOG("[GUIDE] addCurrentPageAsPrecondition: collected %zu history actions for page %lu", history.size(), static_cast<unsigned long>(currentPage));

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
                info.actionList[ah] += 1;
            }

            // --- Generate template from history ---
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
                sequence.push_back(ah);
                if (sequence.size() >= 6) break;
            }

            BLOG("[GUIDE] addCurrentPageAsPrecondition: built sequence with %zu actions for template", sequence.size());

            // Check if matches existing template
            bool matched = false;
            for (int t = 0; t < info.templateCount; ++t) {
                bool eq = true;
                for (size_t i = 0; i < sequence.size() && i < 6; ++i) {
                    if (info.templates[t].sequence[i] != sequence[i]) { eq = false; break; }
                }
                if (eq) { matched = true; break; }
            }

            // Create new template if not matched
            if (!matched && !sequence.empty()) {
                GuidancePathTemplate newT;
                for (int i = 0; i < 6; ++i) {
                    newT.sequence[i] = 0;
                    newT.reliability[i] = 1.0;
                }
                for (size_t i = 0; i < sequence.size() && i < 6; ++i) {
                    newT.sequence[i] = sequence[i];
                }

                // FIFO insert
                if (info.templateCount < 5) {
                    for (int t = info.templateCount; t > 0; --t) {
                        info.templates[t] = info.templates[t-1];
                    }
                    info.templates[0] = newT;
                    info.templateCount++;
                    BLOG("[GUIDE] addCurrentPageAsPrecondition: created new template, templateCount=%d", info.templateCount);
                } else {
                    for (int t = 4; t > 0; --t) {
                        info.templates[t] = info.templates[t-1];
                    }
                    info.templates[0] = newT;
                    BLOG("[GUIDE] addCurrentPageAsPrecondition: replaced oldest template (FIFO)");
                }

                BLOG("[GUIDE] addCurrentPageAsPrecondition: new template sequence=[%llu,%llu,%llu,%llu,%llu,%llu]",
                     static_cast<unsigned long long>(newT.sequence[0]),
                     static_cast<unsigned long long>(newT.sequence[1]),
                     static_cast<unsigned long long>(newT.sequence[2]),
                     static_cast<unsigned long long>(newT.sequence[3]),
                     static_cast<unsigned long long>(newT.sequence[4]),
                     static_cast<unsigned long long>(newT.sequence[5]));
            } else if (matched) {
                BLOG("[GUIDE] addCurrentPageAsPrecondition: sequence matches existing template, skipping");
            }
        }
         this->_coveredPreconditionsThisEpisode.insert(currentPage);
        // clear per-page attempt counts for this precondition page when added/covered
        this->_guidanceAttemptCounts.erase(currentPage);
     }

    void ModelReusableAgent::beginNewEpisode() {
        std::lock_guard<std::mutex> guard(this->_preconditionLock);
        (void)guard;
        this->_coveredPreconditionsThisEpisode.clear();
         // reset guidance attempt counts so attempts are per-episode
         this->_guidanceAttemptCounts.clear();
    }

    void ModelReusableAgent::processGuidedActionResult(const ActionPtr &action, uintptr_t targetPageHash, bool reached, const std::vector<uint64_t> &episodePath) {
        if (action == nullptr) return;
        uint64_t ah = static_cast<uint64_t>(action->hash());
        if (reached) {
            // success: reset consecutive fail for action and noProgress counter
            this->_consecutiveFails[ah] = 0;
            this->_noProgressCount = 0;
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
            this->_consecutiveFails[ah]++;
            this->_noProgressCount++;
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
            // page-level stop if noProgressCount >=3
            auto pit = this->_preconditionPages.find(targetPageHash);
            if (pit != this->_preconditionPages.end()) {
                PreconditionInfo &targetInfo = pit->second;
                if (this->_noProgressCount >= 3) {
                    targetInfo.score = std::max(targetInfo.score * 0.3, 0.1);
                    this->_coveredPreconditionsThisEpisode.insert(targetPageHash);
                }
            }
            // action-level blacklist if consecutiveFails >=3
            if (this->_consecutiveFails[ah] >= 3) {
                for (auto &kv : this->_preconditionPages) {
                    PreconditionInfo &info = kv.second;
                    for (int t = 0; t < info.templateCount; ++t) {
                        for (int i = 0; i < 6; ++i) {
                            if (info.templates[t].sequence[i] == ah) info.templates[t].reliability[i] = 0.0;
                        }
                    }
                }
            }
        }
        // summary log for diagnostics
        BLOG("[GUIDE] exit processGuidedActionResult: action_hash=%llu targetPage=%lu reached=%d noProgressCount=%d consecutiveFails=%d",
             static_cast<unsigned long long>(ah), static_cast<unsigned long>(targetPageHash), reached, this->_noProgressCount,
             (this->_consecutiveFails.find(ah) != this->_consecutiveFails.end() ? this->_consecutiveFails[ah] : 0));
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
            this->_consecutiveFails[actionHash] = 0;
            this->_noProgressCount = 0;
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
        this->_consecutiveFails[actionHash]++;
        this->_noProgressCount++;
        {
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            for (auto &kv : this->_preconditionPages) {
                PreconditionInfo &info = kv.second;
                for (int t = 0; t < info.templateCount; ++t) for (int i = 0; i < 6; ++i) {
                    if (info.templates[t].sequence[i] == actionHash) info.templates[t].reliability[i] *= 0.5;
                }
            }
            // global decay
            for (auto &kv : this->_preconditionPages) {
                PreconditionInfo &info = kv.second;
                for (int t = 0; t < info.templateCount; ++t) for (int i = 0; i < 6; ++i) info.templates[t].reliability[i] *= 0.95;
            }
            // page-level stop if noProgressCount >=3
            auto pit = this->_preconditionPages.find(targetPageHash);
            if (pit != this->_preconditionPages.end()) {
                PreconditionInfo &targetInfo = pit->second;
                if (this->_noProgressCount >= 3) {
                    targetInfo.score = std::max(targetInfo.score * 0.3, 0.1);
                    this->_coveredPreconditionsThisEpisode.insert(targetPageHash);
                }
            }
            // action-level blacklist if consecutiveFails >=3
            if (this->_consecutiveFails[actionHash] >= 3) {
                for (auto &kv : this->_preconditionPages) {
                    PreconditionInfo &info = kv.second;
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
        BLOG("[GUIDE] exit processGuidedActionResultByHash: action_hash=%llu targetPage=%lu reached=%d noProgressCount=%d consecutiveFails=%d",
             static_cast<unsigned long long>(actionHash), static_cast<unsigned long>(targetPageHash), reached, this->_noProgressCount,
             (this->_consecutiveFails.find(actionHash) != this->_consecutiveFails.end() ? this->_consecutiveFails[actionHash] : 0));
    }


} // namespace fastbotx

#endif /* fastbotx_ModelReusableAgent_CPP_ */

