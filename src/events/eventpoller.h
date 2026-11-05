#pragma once

#include <memory>
#include <unordered_map>


class EventPoll;
class World;

class EventPoller {
public:
    explicit EventPoller(World& world);
    ~EventPoller();

    void addPoll(std::shared_ptr<EventPoll> eventPoll);
    void removePoll(EventPoll* eventPoll);

    void update(float deltaSec);


protected:
    World& m_world;
    std::unordered_map<EventPoll*, std::shared_ptr<EventPoll>> m_eventPolls;
};
