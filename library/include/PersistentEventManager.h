#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Manager.h"
#include "Quaternion.h"
#include "Types.h"
#include "Vector.h"
#include "VUtilsRandom.h"

// Vanilla PersistentEventSystem (server side): events a client starts or stops by destroying an object
// (TriggerPersistentEventOnDestroy, e.g. jotun_invasion), their objects, the list sent to clients
// and the .db2 section (Brotli of the JsonUtility list).
class PersistentEventManager : public avledet::util::IManager<PersistentEventManager>
{
    // ObjectSpawnSettings
    struct Spawn
    {
        std::vector<std::string> m_prefabs;
        bool m_important;
        bool m_randomRotation;
        int m_minCount, m_maxCount;
        float m_minDistance;// from other event objects
        float m_maxDistance;// from the event center, <= 0.01: the event radius
        float m_maxSlope;
        int m_water;// 0 can, 1 can not, 2 only in water
    };
    struct Event
    {
        std::string m_name;
        float m_minRadius, m_maxRadius;
        int m_maxConcurrent;
        std::string m_location;
        std::vector<Spawn> m_spawns;
        int m_biomes;
        float m_minCenter, m_maxCenter;
        float m_minHeight, m_maxHeight;
        float m_maxSlope;
        float m_minSimilar;
    };
    // ActivePersistentEvent
    struct Active
    {
        int m_source;
        int m_id;
        Vector3f m_pos;
        float m_radius;
        double m_start = 0;
        double m_duration = -1;
    };
    // a view an Instantiate of an event prefab creates
    struct View
    {
        avledet::util::Hash m_prefab;
        Vector3f m_pos;
        Quaternion m_rot;
        Vector3f m_scale;
    };

    std::vector<Event> m_events;
    std::vector<Active> m_active;
    int m_nextId = 0;
    std::unordered_map<std::string, std::vector<View>> m_objects;
    VUtils::Random::State m_random;

    bool PlaceEvent(Event const &e, Vector3f &pos);
    void CreateObjects(Spawn const &s, Active const &a, std::vector<Vector3f> &spawned);
    void Instantiate(std::string const &prefab, Vector3f pos, Quaternion rot);
    std::string ListJson() const;
    void SendList(avledet::util::UserID target);

  public:
    void Init();
    void Update();

    avledet::util::Bytes Save() const;
    void Load(avledet::util::Bytes const &brotli);
};
