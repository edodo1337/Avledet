#include "PersistentEventManager.h"

#include <cmath>
#include <fstream>

#include <brotli/decode.h>
#include <brotli/encode.h>
#include <nlohmann/json.hpp>
#include <quill/LogMacros.h>

#include "Avledet.h"
#include "GeoManager.h"
#include "NetManager.h"
#include "RouteManager.h"
#include "ZDOManager.h"
#include "ZoneManager.h"

void PersistentEventManager::Init()
{
    std::ifstream eventsIn("persistentEvents.json");
    std::ifstream objectsIn("event_objects.json");
    if (!eventsIn || !objectsIn)
        throw std::runtime_error("persistentEvents.json or event_objects.json missing (dumped from the 1.0 server)");

    for (auto &&j : nlohmann::json::parse(eventsIn)) {
        Event e {j.at("internalName"), j.at("minRadius"), j.at("maxRadius"), j.at("maxConcurrent"),
                 j.at("locationToSpawn").is_null() ? "" : j.at("locationToSpawn").get<std::string>(), {},
                 j.at("biomes"), j.at("minDistanceFromCenter"), j.at("maxDistanceFromCenter"), j.at("minHeight"),
                 j.at("maxHeight"), j.at("maxSlope"), j.at("minDistanceFromSimilar")};
        for (auto &&o : j.at("objectsToSpawn"))
            e.m_spawns.push_back({o.at("prefabs"), o.at("important"), o.at("randomRotation"), o.at("minCount"),
                                  o.at("maxCount"), o.at("minDistanceFromOtherEventObjects"),
                                  o.at("maxDistanceFromEventCenter"), o.at("maxSlope"), o.at("waterSpawning")});
        m_events.push_back(std::move(e));
    }
    auto objects = nlohmann::json::parse(objectsIn);// items() must not outlive it
    for (auto &&[name, views] : objects.items())
        for (auto &&v : views)
            m_objects[name].push_back({avledet::util::get_stable_hash(v[0].get<std::string>()), Vector3f(v[1], v[2], v[3]),
                                       Quaternion(v[4], v[5], v[6], v[7]), Vector3f(v[8], v[9], v[10])});

    RouteManager::instance().Register("RequestStartEvent", [this](Peer::Ptr peer, std::int32_t source) {
        if (source < 0 || source >= (int) m_events.size())
            return;
        auto &&e = m_events[source];
        int count = (int) std::ranges::count_if(m_active, [&](Active const &a) { return a.m_source == source; });
        if (e.m_maxConcurrent > 0 && count >= e.m_maxConcurrent)
            return;
        Vector3f pos;
        if (!PlaceEvent(e, pos)) {
            LOG_INFO(AVL_LOGGER, "Unable to find placement for event {}", e.m_name);
            return;
        }
        Active a {source, m_nextId, pos, m_random.range(e.m_minRadius, e.m_maxRadius)};
        if (!e.m_location.empty())
            LOG_WARNING(AVL_LOGGER, "Event {} wants location {}, mid game locations are not supported", e.m_name,
                        e.m_location);
        std::vector<Vector3f> spawned;
        for (auto &&s : e.m_spawns)
            CreateObjects(s, a, spawned);
        m_nextId++;
        m_active.push_back(a);
        LOG_INFO(AVL_LOGGER, "{} started event {} at {} ({} objects)", peer->m_name, e.m_name, pos, spawned.size());
        SendList(0);
    });
    RouteManager::instance().Register("RequestStopEvent", [this](Peer::Ptr peer, std::int32_t id) {
        auto it = std::ranges::find_if(m_active, [&](Active const &a) { return a.m_id == id; });
        if (it == m_active.end())
            return;
        LOG_INFO(AVL_LOGGER, "{} stopped event {}", peer->m_name, m_events[it->m_source].m_name);
        m_active.erase(it);
        SendList(0);
    });
    RouteManager::instance().Register("RequestActiveEventsList",
                                      [this](Peer::Ptr peer) { SendList(peer->GetUserID()); });
    LOG_INFO(AVL_LOGGER, "Loaded {} persistent events", m_events.size());
}

// every 30 s: events with a duration that ran out
void PersistentEventManager::Update()
{
    if (!VUtils::run_periodic<struct persistent_events_update>(std::chrono::seconds(30)))
        return;
    double now = Avledet::instance().GetWorldTime();
    if (std::erase_if(m_active, [&](Active const &a) { return a.m_duration >= 0 && a.m_start + a.m_duration <= now; }))
        SendList(0);
}

// WorldGenerator.GetNormal (its radius is unused)
static Vector3f Normal(float x, float z)
{
    auto &&geo = GeoManager::instance();
    float r = geo.GetHeight(x + 1, z), l = geo.GetHeight(x - 1, z);
    float u = geo.GetHeight(x, z + 1), d = geo.GetHeight(x, z - 1);
    return Vector3f(2 * (r - l), 4, 2 * (d - u)).normal();
}

// PersistentEvent.GenerateEventLocation
bool PersistentEventManager::PlaceEvent(Event const &e, Vector3f &pos)
{
    float similar = e.m_minSimilar * e.m_minSimilar;
    float slope   = e.m_maxSlope / 90.f;
    float players = std::pow(e.m_maxRadius + 150.f, 2.f);
    for (int i = 0; i < 1000; i++) {
        auto c = m_random.inside_unit_circle();
        auto n = c.normal();
        c      = c * (e.m_maxCenter - e.m_minCenter) + n * e.m_minCenter;
        auto biome = std::to_underlying(GeoManager::instance().GetBiome(Vector3f(c.x, 0, c.y)));
        if ((e.m_biomes & biome) != biome)
            continue;
        bool near = std::ranges::any_of(NetManager::instance().GetPeers(), [&](Peer::Ptr const &peer) {
            auto &&zdo = peer->find_zdo();
            if (!zdo)
                return false;
            auto p = zdo->get_position();
            return (p.x - c.x) * (p.x - c.x) + (p.z - c.y) * (p.z - c.y) < players;
        });
        if (near)
            continue;
        if (std::ranges::any_of(m_active, [&](Active const &a) {
                return &m_events[a.m_source] == &e
                    && (a.m_pos.x - c.x) * (a.m_pos.x - c.x) + a.m_pos.y * a.m_pos.y + (a.m_pos.z - c.y) * (a.m_pos.z - c.y)
                               < similar;
            }))
            continue;
        float height = GeoManager::instance().GetHeight(c.x, c.y);
        if (height > e.m_maxHeight || height < e.m_minHeight)
            continue;
        if (std::abs(e.m_maxSlope - 90.f) > 1e-5f && 1.f - Normal(c.x, c.y).y > slope)
            continue;
        pos = Vector3f(c.x, height, c.y);
        return true;
    }
    return false;
}

// ObjectSpawnSettings.Create
void PersistentEventManager::CreateObjects(Spawn const &s, Active const &a, std::vector<Vector3f> &spawned)
{
    int count     = m_random.range(s.m_minCount, std::max(s.m_maxCount, s.m_minCount));
    float slope   = s.m_maxSlope / 90.f;
    float minDist = s.m_minDistance * s.m_minDistance;
    float radius  = s.m_maxDistance <= 0.01f ? a.m_radius : s.m_maxDistance;
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < (s.m_important ? 100 : 1); j++) {
            auto v = m_random.inside_unit_circle() * radius;
            Vector3f p(a.m_pos.x + v.x, a.m_pos.y, a.m_pos.z + v.y);
            if (s.m_maxSlope < 90 && 1.f - Normal(p.x, p.z).y > slope)
                continue;
            p.y = GeoManager::instance().GetHeight(p.x, p.z);
            if ((s.m_water == 2 && p.y > 29) || (s.m_water == 1 && p.y <= 30))
                continue;
            p.y = std::max(p.y, 30.f);
            if (minDist > 0 && std::ranges::any_of(spawned, [&](Vector3f o) { return o.sq_distance_to(p) < minDist; }))
                continue;
            spawned.push_back(p);
            auto &&prefab = s.m_prefabs[m_random.range(0, (int) s.m_prefabs.size())];
            Instantiate(prefab, p, s.m_randomRotation ? Quaternion::euler(0, m_random.value() * 360.f, 0) : Quaternion::IDENTITY);
            break;
        }
    }
}

// a plain Instantiate on the server: every active view of the prefab makes a ZDO in its Awake
void PersistentEventManager::Instantiate(std::string const &prefab, Vector3f pos, Quaternion rot)
{
    auto it = m_objects.find(prefab);
    if (it == m_objects.end()) {
        LOG_WARNING(AVL_LOGGER, "Event object {} unknown", prefab);
        return;
    }
    for (auto &&v : it->second) {
        auto &&zdo = ZdoManager::instance().Instantiate(v.m_prefab, pos + rot * v.m_pos, v.m_scale);
        zdo->set_rotation(rot * v.m_rot);
        ZoneManager::instance().AwakeRandom(m_random, *zdo, false);
    }
}

// JsonUtility.ToJson(ActiveEventsList)
std::string PersistentEventManager::ListJson() const
{
    auto list = nlohmann::ordered_json::array();// JsonUtility field order
    for (auto &&a : m_active)
        list.push_back({{"sourceEventId", a.m_source},
                        {"eventId", a.m_id},
                        {"position", {{"x", a.m_pos.x}, {"y", a.m_pos.y}, {"z", a.m_pos.z}}},
                        {"radius", a.m_radius},
                        {"startTime", a.m_start},
                        {"duration", a.m_duration}});
    return nlohmann::ordered_json {{"list", list}}.dump();
}

void PersistentEventManager::SendList(avledet::util::UserID target)
{
    auto json = ListJson();
    if (target)
        RouteManager::instance().Invoke(target, "UpdateClientEventsList", std::string_view(json));
    else
        RouteManager::instance().InvokeAll("UpdateClientEventsList", std::string_view(json));
}

avledet::util::Bytes PersistentEventManager::Save() const
{
    auto json = ListJson();
    std::size_t size = BrotliEncoderMaxCompressedSize(json.size());
    avledet::util::Bytes out(size);
    if (!BrotliEncoderCompress(BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_TEXT, json.size(),
                               reinterpret_cast<std::uint8_t const *>(json.data()), &size,
                               reinterpret_cast<std::uint8_t *>(out.data())))
        throw std::runtime_error("brotli compression failed");
    out.resize(size);
    return out;
}

void PersistentEventManager::Load(avledet::util::Bytes const &brotli)
{
    std::string json;
    auto *state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    std::size_t availIn = brotli.size();
    auto *in            = reinterpret_cast<std::uint8_t const *>(brotli.data());
    BrotliDecoderResult result;
    do {
        std::uint8_t buf[4096];
        std::size_t availOut = sizeof(buf);
        auto *out            = buf;
        result               = BrotliDecoderDecompressStream(state, &availIn, &in, &availOut, &out, nullptr);
        json.append(reinterpret_cast<char *>(buf), sizeof(buf) - availOut);
    } while (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT);
    BrotliDecoderDestroyInstance(state);
    if (result != BROTLI_DECODER_RESULT_SUCCESS)
        throw std::runtime_error("persistent events are not brotli");

    m_active.clear();
    auto parsed = nlohmann::json::parse(json);// at() refers into it
    for (auto &&j : parsed.at("list")) {
        auto &&p = j.at("position");
        m_active.push_back({j.at("sourceEventId"), j.at("eventId"), Vector3f(p.at("x"), p.at("y"), p.at("z")),
                            j.at("radius"), j.at("startTime"), j.at("duration")});
        m_nextId = std::max(m_nextId, m_active.back().m_id + 1);
    }
    LOG_INFO(AVL_LOGGER, "Loaded {} active persistent events", m_active.size());
}
