#include <charconv>
#include <ranges>
#include <quill/LogMacros.h>
#include <string_view>

#include "Avledet.h"
#include "DiscordManager.h"
#include "GeoManager.h"
#include "Hashes.h"
#include "NetManager.h"
#include "Prefab.h"
#include "RandomEventManager.h"
#include "RouteManager.h"
#include "VUtilsResource.h"
#include "ZDOManager.h"
#include "ZoneManager.h"

void RaidManager::Init()
{
    // interval: 46
    // chance: 20
    // range: 96

    LOG_NOTICE(AVL_LOGGER, "Initializing EventManager");

    {
        // load Foliage:
        auto opt = VUtils::Resource::ReadFile<avledet::util::Bytes>("randomEvents.pkg");
        if (!opt)
            throw std::runtime_error("randomEvents.pkg missing");

        DataReader pkg(*opt);

        auto comment = pkg.read<std::string_view>();// comment
        LOG_DEBUG(AVL_LOGGER, "pkg comment: {}", comment);

        auto ver = pkg.read<std::string_view>();
        if (ver != VConstants::GAME) {
            LOG_WARNING(AVL_LOGGER, "randomEvents.pkg uses different game version than server ({})", ver);
        }

        auto count = pkg.read<std::int32_t>();
        for (int i = 0; i < count; i++) {
            auto e = std::make_unique<Event>();

            e->m_name     = pkg.read<std::string>();
            e->m_duration = duration_cast<std::chrono::nanoseconds>(
                    std::chrono::seconds((std::int64_t) pkg.read<float>()));
            e->m_nearBaseOnly          = pkg.read<bool>();
            e->m_pauseIfNoPlayerInArea = pkg.read<bool>();
            e->m_biome                 = (avledet::util::Biome) pkg.read<std::int32_t>();

            e->m_presentGlobalKeys = pkg.read<decltype(Event::m_presentGlobalKeys)>();
            e->m_absentGlobalKeys  = pkg.read<decltype(Event::m_absentGlobalKeys)>();
            e->m_eventRange        = pkg.read<float>();
            e->m_playerConditions  = pkg.read<bool>();

            auto &&sv    = e->m_name;// Make a temp key because assign eval order is not guaranteed
            m_events[sv] = std::move(e);
        }

        LOG_NOTICE(AVL_LOGGER, "Loaded {} random events", count);
    }
}

// RandEventSystem.UpdateRandomEvent and FixedUpdate (server)
void RaidManager::Update()
{
    ZoneScoped;

    float dt = Avledet::instance().delta();

    // the EventRate world modifier scales the interval and divides the chance, 0 turns raids off
    float rate     = ZoneManager::instance().ScalarKey("eventrate");
    float interval = std::chrono::duration<float>(AVL_CONFIG.m_raids_interval).count() * rate;
    m_eventIntervalTimer += dt;
    if (rate > 0 && interval > 0 && m_eventIntervalTimer > interval) {
        m_eventIntervalTimer = 0;
        if (VUtils::Random::State().next_float() <= AVL_CONFIG.m_raids_chance / rate) {
            if (auto opt = GetPossibleRandomEvent()) {
                auto &&[e, pos] = *opt;
                SetCurrentRandomEvent(e.get(), pos, e.get().m_duration);
            }
        }
    }

    if (m_activeEvent && !(m_activeEvent->m_pauseIfNoPlayerInArea && !AnyPlayerInArea())) {
        m_activeTime += dt;
        if (m_activeDuration > 0 && m_activeTime > m_activeDuration) {
            AVL_DISPATCH_WEBHOOK("Random event stopped: `" + this->m_activeEvent->m_name + "`");
            m_activeEvent    = nullptr;
            m_activeEventPos = Vector3f::ZERO;
            SendCurrentRandomEvent();
        }
    }

    if (VUtils::run_periodic<struct send_events>(2s))
        SendCurrentRandomEvent();
}

void RaidManager::SetCurrentRandomEvent(Event const &e, Vector3f pos, std::chrono::nanoseconds duration)
{
    m_activeEvent    = &e;
    m_activeEventPos = pos;
    m_activeTime     = 0;
    m_activeDuration = std::chrono::duration<float>(duration).count();

    LOG_INFO(AVL_LOGGER, "Set current random event: {}", e.m_name);
    AVL_DISPATCH_WEBHOOK("Random event started in world `" + e.m_name + "`");
    SendCurrentRandomEvent();
}

// IsAnyPlayerInEventArea: character ZDOs within the event range on XZ, not in a dungeon
bool RaidManager::AnyPlayerInArea() const
{
    for (auto &&peer : NetManager::instance().GetPeers()) {
        auto &&zdo = peer->find_zdo();
        if (!zdo)
            continue;
        auto p = zdo->get_position();
        float dx = p.x - m_activeEventPos.x, dz = p.z - m_activeEventPos.z;
        if (p.y <= 3000 && std::sqrt(dx * dx + dz * dz) < m_activeEvent->m_eventRange)
            return true;
    }
    return false;
}

// GetPossibleRandomEvents: the players come from ServerSyncedPlayerData (refPos, possibleEvents, baseValue)
std::optional<std::pair<std::reference_wrapper<RaidManager::Event const>, Vector3f>>
RaidManager::GetPossibleRandomEvent()
{
    struct Player
    {
        Vector3f m_pos;
        int m_baseValue;
        std::vector<std::string> m_possible;
    };
    std::vector<Player> players;
    for (auto &&peer : NetManager::instance().GetPeers()) {
        auto possible = peer->m_syncData.find("possibleEvents");
        auto base     = peer->m_syncData.find("baseValue");
        if (possible == peer->m_syncData.end() || base == peer->m_syncData.end())
            continue;
        Player p {peer->m_pos, 0, {}};
        if (std::from_chars(base->second.data(), base->second.data() + base->second.size(), p.m_baseValue).ec
            != std::errc())
            continue;
        for (auto &&part : std::views::split(possible->second, ','))
            p.m_possible.emplace_back(part.begin(), part.end());
        players.push_back(std::move(p));
    }
    auto can = [](Player const &p, Event const &e) { return std::ranges::find(p.m_possible, e.m_name) != p.m_possible.end(); };

    bool playerEvents = ZoneManager::instance().GetGlobalKey("playerevents");
    std::vector<std::pair<std::reference_wrapper<Event const>, Vector3f>> result;
    for (auto &&pair : m_events) {
        auto &&e = *pair.second;

        // HaveGlobalKeys
        if (playerEvents && e.m_playerConditions) {
            if (!std::ranges::any_of(players, [&](Player const &p) { return can(p, e); }))
                continue;
        } else if (AVL_CONFIG.m_raids_require_keys) {
            if (!std::ranges::all_of(e.m_presentGlobalKeys, [](auto &&k) { return ZoneManager::instance().GetGlobalKey(k); })
                || std::ranges::any_of(e.m_absentGlobalKeys, [](auto &&k) { return ZoneManager::instance().GetGlobalKey(k); }))
                continue;
        }

        // GetValidEventPoints
        std::vector<Vector3f> points;
        for (auto &&p : players) {
            bool biome = e.m_biome == avledet::util::Biome::None
                      || (std::to_underlying(GeoManager::instance().GetBiome(p.m_pos)) & std::to_underlying(e.m_biome));
            if (biome && (!e.m_nearBaseOnly || p.m_baseValue >= 3) && !(p.m_pos.y > 3000) && (!playerEvents || can(p, e)))
                points.push_back(p.m_pos);
        }
        if (!points.empty())
            result.push_back({e, points[VUtils::Random::State().range(0, (int) points.size())]});
    }

    if (!result.empty())
        return result[VUtils::Random::State().range(0, (int) result.size())];
    return std::nullopt;
}

void RaidManager::Save(DataWriter &writer)
{
    writer.write(m_eventIntervalTimer);
    writer.write(m_activeEvent ? std::string_view(m_activeEvent->m_name) : "");
    writer.write(m_activeEvent ? m_activeTime : 0.f);
    writer.write(m_activeEventPos);
}

void RaidManager::Load(DataReader &reader, int version)
{
    m_eventIntervalTimer = reader.read<float>();
    auto name            = reader.read<std::string_view>();
    auto time            = reader.read<float>();
    auto pos             = reader.read<Vector3f>();
    // SetRandomEventByName, then the saved elapsed time
    if (auto e = find_event(name)) {
        m_activeEvent    = e;
        m_activeEventPos = pos;
        m_activeTime     = time;
        m_activeDuration = std::chrono::duration<float>(e->m_duration).count();
    }
}

void RaidManager::SendCurrentRandomEvent()
{
    if (m_activeEvent) {
        RouteManager::instance().InvokeAll(
                avledet::util::hashes::Routed::S2C_SetEvent, std::string_view(m_activeEvent->m_name),
                m_activeTime,
                m_activeEventPos);
    } else {
        RouteManager::instance().InvokeAll(avledet::util::hashes::Routed::S2C_SetEvent, std::string_view(""), 0.f,
                                  Vector3f::ZERO);
    }
}
