#pragma once

#include "DataStream.h"
#include "Manager.h"
#include "Types.h"
#include "Vector.h"
#include "VUtils.h"

//formerly RandomEventManager
class RaidManager : public avledet::util::IManager<RaidManager>
{
  public:
    class Event
    {
      public:
        avledet::util::Set<std::string, ankerl::unordered_dense::string_hash, std::equal_to<>>
                m_presentGlobalKeys;
        avledet::util::Set<std::string, ankerl::unordered_dense::string_hash, std::equal_to<>>
                m_absentGlobalKeys;
        std::string m_name;
        std::chrono::nanoseconds m_duration {};
        avledet::util::Biome m_biome {};
        bool m_nearBaseOnly {};
        bool m_pauseIfNoPlayerInArea {};
        float m_eventRange {};
        bool m_playerConditions {};// with PlayerEvents, the players' possibleEvents decide

      public:
        Event() {}
    };

  public:
    avledet::util::Map<std::string_view, std::unique_ptr<Event>> m_events;

    // Event timer for next event attempt
    float m_eventIntervalTimer = 0;

  private:
    // RandEventSystem.m_randomEvent: the current event, its elapsed time and duration
    Event const *m_activeEvent {};
    Vector3f m_activeEventPos;
    float m_activeTime {};
    float m_activeDuration {};

  private:
    void SendCurrentRandomEvent();

    std::optional<std::pair<std::reference_wrapper<Event const>, Vector3f>> GetPossibleRandomEvent();

    bool AnyPlayerInArea() const;

  public:
    void Init();
    void Update();

    void SetCurrentRandomEvent(Event const &e, Vector3f pos, std::chrono::nanoseconds duration);

    // Get an event by name
    //	Returns null if not found
    Event const *find_event(std::string_view name)
    {
        auto &&find = m_events.find(name);
        if (find != m_events.end())
            return find->second.get();
        return nullptr;
    }

    void Save(DataWriter &writer);
    void Load(DataReader &reader, int version);
};
