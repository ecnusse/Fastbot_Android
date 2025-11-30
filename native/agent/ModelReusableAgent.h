/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef ReuseAgent_H_
#define ReuseAgent_H_

#include "AbstractAgent.h"
#include "State.h"
#include "Action.h"
#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <unordered_set>

namespace fastbotx {

#define SarsaRLDefaultAlpha   0.25
#define SarsaRLDefaultEpsilon 0.05
#define SarsaRLDefaultGamma   0.8

    typedef std::map<stringPtr, int> ReuseEntryM;
    typedef std::map<uint64_t, ReuseEntryM> ReuseEntryIntMap;
    typedef std::map<uint64_t, double> ReuseEntryQValueMap;

    struct PreconditionInfo {
        bool visited;
        double score;
        double ema;
        PreconditionInfo() : visited(false), score(1.0), ema(0.0) {}
        PreconditionInfo(bool v, double s, double e) : visited(v), score(s), ema(e) {}
    };

    class ModelReusableAgent : public AbstractAgent {

    public:
        explicit ModelReusableAgent(const ModelPtr &model);

        // load & save will be automatically called in construct & dealloc
        virtual void loadReuseModel(const std::string &packageName);

        // @param model filepath is "" then save to _defaultModelSavePath
        void saveReuseModel(const std::string &modelFilepath);

        static void threadModelStorage(const std::weak_ptr<ModelReusableAgent> &agent);

        ~ModelReusableAgent() override;

        void addCurrentPageAsPrecondition();

        // New: call this when an external controller starts a new episode/round.
        // It clears the per-episode covered set and resets per-episode statistics.
        void beginNewEpisode();

    protected:
        virtual double computeRewardOfLatestAction();

        void updateStrategy() override;

        virtual ActivityStateActionPtr selectNewActionEpsilonGreedyRandomly() const;

        virtual bool eGreedy() const;

        ActionPtr selectNewAction() override;

        double probabilityOfVisitingNewActivities(const ActivityStateActionPtr &action,
                                                  const stringPtrSet &visitedActivities) const;

        double getStateActionExpectationValue(const StatePtr &state,
                                              const stringPtrSet &visitedActivities) const;

        virtual void updateReuseModel();

        void adjustActions() override;

        ActionPtr selectUnperformedActionNotInReuseModel() const;

        /// Choose an unused(unvisited) action with quality value greater than zero
        /// under the influence of humble-gumbel distribution,
        /// \return The chosen action
        ActionPtr selectUnperformedActionInReuseModel() const;

        ActionPtr selectActionByQValue();

        // New: select actions based on a probability model (prioritize actions likely to reach new activities)
        ActionPtr selectActionByProbabilityModel();

        // New: replan path using updated action probabilities
        void replanPath();


    protected:
        double _alpha{};
        double _epsilon{};

        // _rewardCache[i] is the reward value of _previousActions[i+1]
        std::vector<double> _rewardCache;
        std::vector<ActionPtr> _previousActions;

    private:
        // A map containing entry of hash code of Action and map, which containing entry of name of activity that this
        // action goes to and the count of this very activity being visited.
        ReuseEntryIntMap _reuseModel;
        ReuseEntryQValueMap _reuseQValue;
        std::string _modelSavePath;
        std::string _defaultModelSavePath;
        static std::string DefaultModelSavePath; // if the saved path is not specified, use this as the default.
        std::mutex _reuseModelLock;

        void computeAlphaValue();

        double getQValue(const ActionPtr &action);

        void setQValue(const ActionPtr &action, double qValue);

        // New: mapping from page hash to PreconditionInfo (replaces previous simple int map)
        std::unordered_map<uintptr_t, PreconditionInfo> _preconditionPages;

        // New: set of precondition pages covered during the current episode
        std::unordered_set<uintptr_t> _coveredPreconditionsThisEpisode;

        // New: mutex protecting precondition data structures
        std::mutex _preconditionLock;

        // New: action probability model
        std::unordered_map<uint64_t, double> _actionProbabilities;

        // New: precondition algorithm hyper-parameters (tunable)
        double _precond_alpha;    // EMA alpha for per-step update (tunable): controls sensitivity to recent visits, range (0,1]. Larger => more sensitive to recent events.
        double _hit_decay;        // score decay factor when page is hit (tunable): in (0,1], smaller => faster decay of long-term importance.
        double _min_score;        // score floor (tunable): prevents score from dropping below this value, ensures minimum priority.
        double _precond_lambda;   // reward multiplier for precondition (tunable): scales the intrinsic reward from covering a precondition page.
        double _sigmoid_k;        // sigmoid steepness for mappedFreq (tunable): larger => sharper transition around _sigmoid_b.
        double _sigmoid_b;        // sigmoid center/bias (tunable): the EMA value mapped to 0.5 by the sigmoid.
    };

    typedef std::shared_ptr<ModelReusableAgent> ReuseAgentPtr;

}


#endif /* ReuseAgent_H_ */