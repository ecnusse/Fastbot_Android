/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef  Graph_H_
#define  Graph_H_

#include "State.h"
#include "Base.h"
#include "Action.h"
#include <map>
#include <functional>
#include <vector>
#include <mutex>

namespace fastbotx {

    // Callback invoked when a new Action is created/added to the Graph
    // Accepts a generic ActionPtr (ActivityStateActionPtr can be passed as it derives from Action)
    typedef std::function<void(const ActionPtr&)> ActionCreatedCallback;

    typedef std::map<WidgetPtr, ActivityStateActionPtrSet, Comparator<Widget>> ModelActionPtrWidgetMap;
    typedef std::map<std::string, StatePtrSet> StatePtrStrMap;

    struct ActionCounter {
    private:

        // Enum Act count
        long actCount[ActionType::ActTypeSize];
        long total;

    public:
        ActionCounter()
                : actCount{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, total(0) {
        }

        void countAction(const ActivityStateActionPtr &action) {
            actCount[action->getActionType()]++;
            total++;
        }

        long getTotal() const { return total; }
    };


    class GraphListener {
    public:
        virtual void onAddNode(StatePtr node) = 0;
    };


    typedef std::shared_ptr<GraphListener> GraphListenerPtr;
    typedef std::vector<GraphListenerPtr> GraphListenerPtrVec;

    class Graph : Node {
    public:
        // Register a callback to be invoked whenever the graph creates a new Action
        void registerActionCreatedCallback(ActionCreatedCallback cb);

        Graph();

        inline size_t stateSize() const { return this->_states.size(); }

        time_t getTimestamp() const { return this->_timeStamp; }

        void addListener(const GraphListenerPtr &listener);

        // add state to graph, adjust the state or return a exists state
        StatePtr addState(StatePtr state);

        long getTotalDistri() const { return this->_totalDistri; }

        stringPtrSet getVisitedActivities() const { return this->_visitedActivities; };

        virtual ~Graph();

    protected:
        void notifyNewStateEvents(const StatePtr &node);


    private:
        void addActionFromState(const StatePtr &node);

        // Action-created callback storage
        std::vector<ActionCreatedCallback> _actionCreatedCallbacks;
        std::mutex _actionCallbackMutex;


        StatePtrSet _states;      // all of the states in the graph
        stringPtrSet _visitedActivities; // a string set containing all the visited activities
        std::map<std::string, std::pair<int, double>> _activityDistri;
        long _totalDistri; // the count of reaching or accessing states, which could be new states or a state accessed before
        ModelActionPtrWidgetMap _widgetActions; //  query actions based on widget info

        ActivityStateActionPtrSet _unvisitedActions;
        ActivityStateActionPtrSet _visitedActions;

        ActionCounter _actionCounter;
        GraphListenerPtrVec _listeners;
        time_t _timeStamp;

        const static std::pair<int, double> _defaultDistri;

    public:
        // Return a set containing both visited and unvisited actions (copy)
        ActivityStateActionPtrSet getAllActions() const;

    };

    typedef std::shared_ptr<Graph> GraphPtr;

}

#endif  // Graph_H_
