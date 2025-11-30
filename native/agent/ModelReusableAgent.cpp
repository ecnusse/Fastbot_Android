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
#include <unordered_set>
#include <string>

namespace fastbotx {

    static inline double sigmoidMap(double x, double k, double b) {
        return 1.0 / (1.0 + std::exp(-k * (x - b)));
    }

    ModelReusableAgent::ModelReusableAgent(const ModelPtr &model)
            : AbstractAgent(model), _alpha(SarsaRLDefaultAlpha), _epsilon(SarsaRLDefaultEpsilon),
              _modelSavePath(DefaultModelSavePath), _defaultModelSavePath(DefaultModelSavePath),
              _precond_alpha(0.2), _hit_decay(0.6), _min_score(0.2), _precond_lambda(0.7), _sigmoid_k(4.0), _sigmoid_b(0.5) {
        this->_algorithmType = AlgorithmType::Reuse;
    }

    ModelReusableAgent::~ModelReusableAgent() {
        BLOG("save model in destruct");
        this->saveReuseModel(this->_modelSavePath);
        this->_reuseModel.clear();
    }

    void ModelReusableAgent::beginNewEpisode() {
        std::lock_guard<std::mutex> guard(this->_preconditionLock);
        (void)guard;
        this->_coveredPreconditionsThisEpisode.clear();
        // reset per-episode ema while keeping long-term score and visited flags
        for (auto &p : this->_preconditionPages) {
            p.second.ema = 0.0;
        }
    }

    void ModelReusableAgent::computeAlphaValue() {
        if (nullptr != this->_newState) {
            const GraphPtr &graphRef = this->_model.lock()->getGraph();
            long totalVisitCount = graphRef->getTotalDistri();
            double movingAlpha = 0.5;
            if (totalVisitCount > 20000) movingAlpha -= 0.1;
            if (totalVisitCount > 50000) movingAlpha -= 0.1;
            if (totalVisitCount > 100000) movingAlpha -= 0.1;
            if (totalVisitCount > 250000) movingAlpha -= 0.1;
            this->_alpha = std::max(SarsaRLDefaultAlpha, movingAlpha);
        }
    }

#define SarsaNStep 5

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
            // Add the state expectation value part to the reward
            rewardValue += (this->getStateActionExpectationValue(this->_newState,
                                                                 visitedActivities) /
                            sqrt(this->_newState->getVisitedCount() + 1.0));

            // ------ precondition logic v2.0 ------
            uintptr_t currentPage = this->_newState->hash();
            double preconditionReward = 0.0;

            {
                std::lock_guard<std::mutex> guard(this->_preconditionLock);
                // update EMA for all known precondition pages (x_t = 1 if current page else 0)
                for (auto &entry : this->_preconditionPages) {
                    uintptr_t pageHash = entry.first;
                    PreconditionInfo &info = entry.second;
                    int x = (pageHash == currentPage) ? 1 : 0;
                    info.ema = _precond_alpha * static_cast<double>(x) + (1.0 - _precond_alpha) * info.ema;
                }

                auto it = this->_preconditionPages.find(currentPage);
                if (it != this->_preconditionPages.end()) {
                    PreconditionInfo &info = it->second;
                    // decay long-term score on arrival to avoid starvation
                    info.score = std::max(info.score * _hit_decay, _min_score);

                    double mappedFreq = sigmoidMap(info.ema, _sigmoid_k, _sigmoid_b);
                    double urgency = info.score * (1.0 - mappedFreq);

                    // Only give precondition reward if globally unvisited and not covered this episode
                    if (!info.visited && this->_coveredPreconditionsThisEpisode.find(currentPage) == this->_coveredPreconditionsThisEpisode.end()) {
                        preconditionReward = _precond_lambda * urgency;
                        // mark as covered in this episode and set global visited
                        this->_coveredPreconditionsThisEpisode.insert(currentPage);
                        info.visited = true;
                        BLOG("Precondition covered page %lu, urgency=%f, reward=%f", static_cast<unsigned long>(currentPage), urgency, preconditionReward);
                    }
                }
            }

            rewardValue += preconditionReward;
            // ------ end precondition logic ------

            BLOG("total visited " ACTIVITY_VC_STR " count is %zu", visitedActivities.size());
        }
        BDLOG("reuse-cov-opti action reward=%f", rewardValue);
        this->_rewardCache.emplace_back(rewardValue);
        // Make sure the length of reward cache is not over SarsaNStep
        if (this->_rewardCache.size() > SarsaNStep) {
            this->_rewardCache.erase(this->_rewardCache.begin());
        }
        return rewardValue;
    }

    double ModelReusableAgent::probabilityOfVisitingNewActivities(const ActivityStateActionPtr &action, const stringPtrSet &visitedActivities) const {
        double value = .0;
        int total = 0;
        int unvisited = 0;
        auto actionMapIterator = this->_reuseModel.find(action->hash());
        if (actionMapIterator != this->_reuseModel.end()) {
            for (const auto &activityCountMapIterator : (*actionMapIterator).second) {
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

    double ModelReusableAgent::getStateActionExpectationValue(const StatePtr &state, const stringPtrSet &visitedActivities) const {
        double value = 0.0;
        for (const auto &action : state->getActions()) {
            uintptr_t actionHash = action->hash();
            if (this->_reuseModel.find(actionHash) == this->_reuseModel.end()) {
                value += 1.0;
            } else if (action->getVisitedCount() >= 1) {
                value += 0.5;
            }
            if (action->getTarget() != nullptr) {
                value += probabilityOfVisitingNewActivities(action, visitedActivities);
            }
        }
        return value;
    }

    void ModelReusableAgent::updateStrategy() {
        if (nullptr == this->_newAction)
            return;
        if (!this->_previousActions.empty()) {
            this->computeRewardOfLatestAction();
            this->updateReuseModel();
//            this->replanPath();
            double value = getQValue(_newAction);
            for (int i = static_cast<int>(this->_previousActions.size()) - 1; i >= 0; i--) {
                double currentQValue = getQValue(_previousActions[i]);
                double currentRewardValue = this->_rewardCache[i];
                value = currentRewardValue + SarsaRLDefaultGamma * value;
                if (i == 0)
                    setQValue(this->_previousActions[i], currentQValue + this->_alpha * (value - currentQValue));
            }
        } else {
            BDLOG("%s", "get action value failed!");
        }
        this->_previousActions.emplace_back(this->_newAction);
        if (this->_previousActions.size() > 5) { // SarsaNStep defined as 5
            this->_previousActions.erase(this->_previousActions.begin());
        }
    }

    void ModelReusableAgent::updateReuseModel() {
        if (this->_previousActions.empty())
            return;
        ActionPtr lastAction = this->_previousActions.back();
        ActivityNameActionPtr modelAction = std::dynamic_pointer_cast<ActivityNameAction>(lastAction);
        if (nullptr == modelAction || nullptr == this->_newState)
            return;
        auto hash = (uint64_t) modelAction->hash();
        stringPtr activity = this->_newState->getActivityString();
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

    ActivityStateActionPtr ModelReusableAgent::selectNewActionEpsilonGreedyRandomly() const {
        if (this->eGreedy()) {
            BDLOG("%s", "Try to select the max value action");
            return this->_newState->greedyPickMaxQValue(enableValidValuePriorityFilter);
        }
        BDLOG("%s", "Try to randomly select a value action.");
        return this->_newState->randomPickAction(enableValidValuePriorityFilter);
    }

    bool ModelReusableAgent::eGreedy() const {
        srand(static_cast<uint32_t>(time(nullptr)));
        auto r = static_cast<double>(rand() % 100) / 100.0L;
        return r >= this->_epsilon;
    }

    ActionPtr ModelReusableAgent::selectNewAction() {
        ActionPtr action = nullptr;
        action = this->selectUnperformedActionNotInReuseModel();
        if (nullptr != action) {
            BLOG("%s", "select action not in reuse model");
            return action;
        }

//        action = this->selectActionByProbabilityModel();
//        if (nullptr != action) {
//            BLOG("%s", "select action by probability model");
//            return action;
//        }

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
//            this->_preconditionPages[this->_newState->hash()]++;
//            BLOG("Page: %lu, visited: %d", (unsigned long) this->_newState->hash(), this->_preconditionPages[this->_newState->hash()]);
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

    ActionPtr ModelReusableAgent::selectActionByProbabilityModel() {
        ActionPtr selectedAction = nullptr;
        double maxProbability = -1.0;
        const GraphPtr &graphRef = this->_model.lock()->getGraph();
        auto visitedActivities = graphRef->getVisitedActivities();

        for (const auto &action : this->_newState->getActions()) {
            uintptr_t actionHash = action->hash();
            double probability = this->probabilityOfVisitingNewActivities(action, visitedActivities);

            if (this->_reuseModel.find(actionHash) == this->_reuseModel.end()) {
                probability = 0.8;
            }

            if (probability > maxProbability) {
                maxProbability = probability;
                selectedAction = action;
            }
        }
        return selectedAction;
    }

    void ModelReusableAgent::replanPath() {
        if (nullptr != this->_newState) {
            const GraphPtr &graphRef = this->_model.lock()->getGraph();
            auto visitedActivities = graphRef->getVisitedActivities();

            for (const auto &action : this->_newState->getActions()) {
                uintptr_t actionHash = action->hash();
                if (this->_reuseModel.find(actionHash) != this->_reuseModel.end()) {
                    double prob = this->probabilityOfVisitingNewActivities(action, visitedActivities);
                    this->_actionProbabilities[actionHash] = prob;
                } else {
                    this->_actionProbabilities[actionHash] = 1.0;
                }
            }
        }
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

        // read precondition pages (visited and score persisted in flatbuffer schema)
        {
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            (void)guard;
            this->_preconditionPages.clear();
            if (reuseFBModel->precondition_pages() != nullptr) {
                auto preconditionPages = reuseFBModel->precondition_pages();
                for (int i = 0; i < preconditionPages->size(); ++i) {
                    auto page = preconditionPages->Get(i);
                    uintptr_t pageName = (uintptr_t) page->hashcode();
                    bool visited = page->visited();
                    double persistedScore = page->score();
                    PreconditionInfo info;
                    info.visited = visited;
                    // use persisted score if > 0, otherwise default to 1.0
                    info.score = (persistedScore > 0.0) ? persistedScore : 1.0;
                    info.ema = 0.0;   // per-episode initialized
                    this->_preconditionPages[pageName] = info;
                    BLOG("Loaded precondition page: %lu, visited: %d, score: %f", static_cast<unsigned long>(pageName), visited, info.score);
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

        // save precondition pages (persist visited flag and score)
        std::vector<flatbuffers::Offset<fastbotx::PreconditionPage>> preconditionPagesVector;
        {
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            (void)guard;
            BLOG("Precondition pages count ended: %zu", this->_preconditionPages.size());
            for (const auto &entry : this->_preconditionPages) {
                // persist score along with visited flag
                auto pageOffset = CreatePreconditionPage(builder, entry.first, entry.second.visited, entry.second.score);
                BLOG("Saved precondition page: %lu, visited: %d, score: %f", static_cast<unsigned long>(entry.first), entry.second.visited, entry.second.score);
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

    void ModelReusableAgent::addCurrentPageAsPrecondition() {
        if (nullptr != this->_newState) {
            uintptr_t currentPage = this->_newState->hash();
            std::lock_guard<std::mutex> guard(this->_preconditionLock);
            (void)guard;
            auto it = this->_preconditionPages.find(currentPage);
            if (it == this->_preconditionPages.end()) {
                PreconditionInfo info;
                info.visited = false;
                info.score = 1.0;
                info.ema = 0.0;
                this->_preconditionPages[currentPage] = info;
                BLOG("Added current page as a precondition page: %lu", static_cast<unsigned long>(currentPage));
            }
        }
        BLOG("Precondition pages count after add: %zu", this->_preconditionPages.size());
    }

    // Restore original behavior: delegate Q storage to Action object
    double ModelReusableAgent::getQValue(const ActionPtr &action) {
        if (action == nullptr) return 0.0;
        return action->getQValue();
    }

    void ModelReusableAgent::setQValue(const ActionPtr &action, double qValue) {
        if (action == nullptr) return;
        action->setQValue(qValue);
    }

} // namespace fastbotx

#endif /* fastbotx_ModelReusableAgent_CPP_ */
