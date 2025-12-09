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

namespace fastbotx {

    ModelReusableAgent::ModelReusableAgent(const ModelPtr &model)
            : AbstractAgent(model), _alpha(SarsaRLDefaultAlpha), _epsilon(SarsaRLDefaultEpsilon),
              _modelSavePath(DefaultModelSavePath), _defaultModelSavePath(DefaultModelSavePath) {
        this->_algorithmType = AlgorithmType::Reuse;
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
                        // collect up to _guidance_history_len historical actions: last N from _previousActions plus _newAction
                        std::vector<ActionPtr> history;
                        int need = this->_guidance_history_len - 1; // reserve one slot for _newAction
                        for (int i = (int)this->_previousActions.size() - 1; i >= 0 && need > 0; --i, --need) {
                            history.push_back(this->_previousActions[i]);
                        }
                        if (this->_newAction != nullptr) history.push_back(this->_newAction);
                        // record each action's success count for this precondition page
                        for (auto &a : history) {
                            if (a == nullptr) continue;
                            uint64_t ah = static_cast<uint64_t>(a->hash());
                            info.actionList[ah] += 1;
                        }
                        // decay score (long-term importance)
                        info.score = std::max(info.score * this->_hit_decay, this->_min_score);
                        BLOG("Recorded %zu history actions for precondition page %lu", history.size(), static_cast<unsigned long>(currentPage));
                    }
                    // mark covered this episode so we don't repeatedly guide it
                    this->_coveredPreconditionsThisEpisode.insert(currentPage);
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
        // quick checks
        {
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            (void)guard;
            if (this->_preconditionPages.empty()) return nullptr;
            // if we've already covered all preconditions this episode, stop guidance
            if (this->_coveredPreconditionsThisEpisode.size() >= this->_preconditionPages.size()) return nullptr;
        }

        // New behavior: consider any action on current page that appears in any precondition's actionList.
        // Map actionHash -> ActionPtr for actions present on current page
        std::unordered_map<uint64_t, ActionPtr> pageActions;
        for (auto &action : this->_newState->getActions()) {
            uint64_t ah = static_cast<uint64_t>(action->hash());
            pageActions[ah] = action;
        }

        // For each action on the page, find which precondition pages it can reach (from _preconditionPages)
        // and compute the best probability for that action (best over target preconditions).
        double bestOverallP = -1.0;
        struct Candidate { uint64_t actionHash; ActionPtr action; uintptr_t targetPage; double p; };
        std::vector<Candidate> candidatesList;
        const double EPS = 1e-12;

        // iterate precondition pages and match their actionLists against pageActions
        for (const auto &entry : this->_preconditionPages) {
            uintptr_t prePage = entry.first;
            const PreconditionInfo &info = entry.second;
            if (info.actionList.empty()) continue; // no data for this precondition
            // Skip prePage if already covered this episode
            if (this->_coveredPreconditionsThisEpisode.find(prePage) != this->_coveredPreconditionsThisEpisode.end()) continue;
            for (const auto &ac : info.actionList) {
                uint64_t ah = ac.first;
                auto pit = pageActions.find(ah);
                if (pit == pageActions.end()) continue; // this action not available in current page
                ActionPtr actionPtr = pit->second;
                // check attempt limit for this page-action
                if (isActionOverAttemptLimit(prePage, ah)) continue;
                // compute probability to reach this precondition via this action
                double p = computePreconditionActionProbability(prePage, actionPtr);
                if (p <= 0.0) continue;
                // record candidate: action may reach this prePage with prob p
                candidatesList.push_back(Candidate{ah, actionPtr, prePage, p});
                if (p > bestOverallP + EPS) bestOverallP = p;
            }
        }

        if (candidatesList.empty() || bestOverallP < 0.0) {
            // no usable guidance candidates found on this page -> fallback
            return nullptr;
        }

        // collect actions that achieve the bestOverallP (within EPS), but we must pick per action the best target page
        // group by actionHash and choose the target page with max p for that action
        std::unordered_map<uint64_t, Candidate> bestPerAction;
        for (auto &c : candidatesList) {
            auto itb = bestPerAction.find(c.actionHash);
            if (itb == bestPerAction.end() || c.p > itb->second.p + EPS) {
                bestPerAction[c.actionHash] = c;
            }
        }

        // Now find actions whose bestPerAction.p equals bestOverallP (within EPS)
        std::vector<Candidate> bestActions;
        for (const auto &kv : bestPerAction) {
            const Candidate &c = kv.second;
            if (std::fabs(c.p - bestOverallP) <= EPS) bestActions.push_back(c);
        }

        if (bestActions.empty()) return nullptr;

        // tie-break among bestActions uniformly
        int pickIdx = 0;
        if (bestActions.size() == 1) pickIdx = 0;
        else pickIdx = randomInt(0, static_cast<int>(bestActions.size() - 1));
        Candidate chosen = bestActions[pickIdx];

        // increment attempt count for the chosen page-action pair
        this->_guidanceAttemptCounts[chosen.targetPage][chosen.actionHash] += 1;
        BLOG("Guidance chosen action %llu for target precondition page %lu with p=%f attempts=%d", static_cast<unsigned long long>(chosen.actionHash), static_cast<unsigned long>(chosen.targetPage), chosen.p, this->_guidanceAttemptCounts[chosen.targetPage][chosen.actionHash]);
        return chosen.action;
    }

    // Compute guidance probability P(A) for action A to reach a precondition page
    // Formula: P(A) = (count(A->Pre) / total_clicks(A)) * scorePre * Rmulti(A)
    double ModelReusableAgent::computePreconditionActionProbability(uintptr_t pageHash, const ActionPtr &action) const {
        if (action == nullptr) return 0.0;
        auto it = this->_preconditionPages.find(pageHash);
        if (it == this->_preconditionPages.end()) return 0.0;
        const PreconditionInfo &info = it->second;
        uint64_t ah = static_cast<uint64_t>(action->hash());
        auto iter = info.actionList.find(ah);
        if (iter == info.actionList.end()) return 0.0; // no recorded successes
        int countToPre = iter->second;
        int totalClicks = std::max(1, action->getVisitedCount()); // avoid divide by zero
        double scorePre = info.score;
        // compute k (number of preconditions this action can reach)
        int k = 0;
        for (const auto &p : this->_preconditionPages) {
            if (p.second.actionList.find(ah) != p.second.actionList.end()) ++k;
        }
        double Rmulti = 1.0 + _guidance_gamma * sqrt((double)k);
        double p = (static_cast<double>(countToPre) / static_cast<double>(totalClicks)) * scorePre * Rmulti;
        return p;
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
        srand((uint32_t) (int) time(nullptr)); // @TODO the random range
        auto r = static_cast<double>(static_cast<double>(rand() % 100) / 100.0L);
        if (r < this->_epsilon)
            return false;
        return true;
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
                this->_reuseModel.insert(std::make_pair(actionHash, entryPtr));
            }
        }
        BLOG("loaded model contains actions: %zu", this->_reuseModel.size());

        // read precondition pages (score and action_counts persisted in flatbuffer schema)
        {
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            (void)guard;
            this->_preconditionPages.clear();
            if (reuseFBModel->precondition_pages() != nullptr) {
                auto preconditionPages = reuseFBModel->precondition_pages();
                for (int i = 0; i < preconditionPages->size(); ++i) {
                    auto page = preconditionPages->Get(i);
                    uintptr_t pageName = (uintptr_t) page->hashcode();
                    // NOTE: The persisted schema contains score and action_counts for precondition pages.
                    double persistedScore = page->score();
                    PreconditionInfo info;
                    // use persisted score if > 0, otherwise default to 1.0
                    info.score = (persistedScore > 0.0) ? persistedScore : 1.0;
                    // load action_counts if present
                    if (page->action_counts() != nullptr) {
                        auto actionCountsVec = page->action_counts();
                        for (int j = 0; j < actionCountsVec->size(); ++j) {
                            auto ac = actionCountsVec->Get(j);
                            uint64_t ah = ac->action();
                            int times = ac->times();
                            info.actionList[ah] = times;
                        }
                    }
                    this->_preconditionPages[pageName] = info;
                    BLOG("Loaded precondition page: %lu, score: %f", static_cast<unsigned long>(pageName), info.score);
                }
            } else {
                BLOG("No precondition pages found in the model.");
            }
        }
        // end read precondition pages

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
            BLOG("Precondition pages count ended: %zu", this->_preconditionPages.size());
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
                auto pageOffset = CreatePreconditionPage(builder, entry.first, entry.second.score, actionCountsOffset);
                BLOG("Saved precondition page: %lu, score: %f, actions=%zu", static_cast<unsigned long>(entry.first), entry.second.score, entry.second.actionList.size());
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
            this->_preconditionPages[currentPage] = newInfo;
            BLOG("Added current page (from external state) as a precondition page: %lu", static_cast<unsigned long>(currentPage));
        } else {
            BLOG("addCurrentPageAsPrecondition(state): page already present %lu", static_cast<unsigned long>(currentPage));
        }
        this->_coveredPreconditionsThisEpisode.insert(currentPage);
    }

    void ModelReusableAgent::beginNewEpisode() {
        std::lock_guard<std::mutex> guard(this->_preconditionLock);
        (void)guard;
        this->_coveredPreconditionsThisEpisode.clear();
         // reset guidance attempt counts so attempts are per-episode
         this->_guidanceAttemptCounts.clear();
    }

    // Check whether an action has exceeded guidance attempt limit for a given page
    bool ModelReusableAgent::isActionOverAttemptLimit(uintptr_t pageHash, uint64_t actionHash) const {
        auto pit = this->_guidanceAttemptCounts.find(pageHash);
        if (pit == this->_guidanceAttemptCounts.end()) return false;
        auto ait = pit->second.find(actionHash);
        if (ait == pit->second.end()) return false;
        return ait->second >= this->_guidance_action_attempt_limit;
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


} // namespace fastbotx

#endif /* fastbotx_ModelReusableAgent_CPP_ */
