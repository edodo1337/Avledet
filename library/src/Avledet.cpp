#include "Config.h"
#include "ReplayManager.h"
#include <chrono>
#include <csignal>
#include <exception>
#include <fstream>
#include <mutex>
#include <stdlib.h>
#include <thread>
#include <type_traits>
#ifndef _WIN32
    #include <unistd.h>
#endif
#include <utility>
#ifdef _WIN32
//#include <winstring.h> //TODO getting many dumb errors 'LookuPContect' unknown override specifier'...
#endif

//#include <magic_enum.hpp> //TODO magic
#include <magic_enum/magic_enum.hpp>
#include <quill/core/LogLevel.h>
#include <quill/LogMacros.h>
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/RotatingFileSink.h>
#include <tracy/Tracy.hpp>
#include <yaml-cpp/yaml.h>

#include "Avledet.h"
#include "DiscordManager.h"
#include "DungeonManager.h"
#include "GeoManager.h"
#include "Hashes.h"
#include "HeightmapBuilder.h"
#include "ModManager.h"
#include "NetManager.h"
#include "PersistentEventManager.h"
#include "RandomEventManager.h"
#include "RouteManager.h"
#include "VUtilsResource.h"
#include "VUtilsString.h"
#include "ZDOManager.h"
#include "ZoneManager.h"
#include "AltBiomes.h"

void probe_dump(char const *dir);// Probe.cpp

// Defined
//  TODO this is very ugly
quill::Logger *AVL_LOGGER {};

void Avledet::LoadFiles(bool reloading)
{
    ConfigManager::instance().load();

    std::filesystem::path dir(AVL_CONFIG.m_world_savedir);
    m_adminList.Open(dir / "adminlist.txt", "List admin players ID  ONE per line");
    m_bannedList.Open(dir / "bannedlist.txt", "List banned players ID  ONE per line");
    m_permittedList.Open(dir / "permittedlist.txt", "List permitted players ID ONE per line");

#if AVL_IS_ON(AVL_DISCORD_INTEGRATION)
    if (AVL_CONFIG.TEST_discordAccountLinking) {
        if (auto &&opt = VUtils::Resource::ReadFile<std::string>("discord-linked.yml")) {
            try {
                auto node                           = YAML::Load(*opt);
                DiscordManager::instance().m_linked_accounts = node.as<decltype(DiscordManager::m_linked_accounts)>();
            } catch (const YAML::Exception &e) {
                LOG_ERROR(AVL_LOGGER, "{}", e.what());
            }
        }
    }
#endif

    if (reloading) {
        // then iterate players, settings active and inactive
        for (auto &&peer : NetManager::instance().GetPeers()) {
            // TODO add a 'previously gated' bit
            //  so discord integration doesnt get messed up
            peer->SetGated(AVL_CONFIG.m_discord_player_restrict);
        }
    }

    NetManager::instance().OnConfigLoad(reloading);

#ifdef _WIN32
    {
        //std::string title = AVL_CONFIG.serverName + " - " + VConstants::GAME;
        std::string title
                = "Avledet " + std::string(AVL_VERSION) + " - Valheim " + std::string(VConstants::GAME);
        SetConsoleTitle(title.c_str());
    }
#endif

    std::error_code err;
    this->m_settingsLastTime = std::filesystem::last_write_time("server.yml", err);
}

void Avledet::SaveFiles()
{
    {
        WorldManager::instance().Save();
    }

#if AVL_IS_ON(AVL_DISCORD_INTEGRATION)
    {
        YAML::Node node(DiscordManager::instance().m_linked_accounts);

        YAML::Emitter emit;
        emit.SetIndent(2);

        emit << YAML::Comment("Discord-linked accounts") << YAML::Newline
             << YAML::Comment("Entries are in the form of 'steam-id: discord-id'") << node;

        VUtils::Resource::WriteFile("linked.yml", emit.c_str());
    }
#endif
}

avledet::util::UserID Avledet::ID() const
{
    return m_serverID;
}

// Get the time since the server started
// Updated once per frame
std::chrono::nanoseconds Avledet::Elapsed() const
{
    //return m_nowUpdate - m_startTime;
    return std::chrono::nanoseconds((std::int64_t)(
            (double) std::chrono::duration_cast<std::chrono::nanoseconds>(m_nowUpdate - m_startTime).count()
            * m_serverTimeMultiplier));
}

std::chrono::nanoseconds Avledet::Nanos() const
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Elapsed());
}

// Get the time in Ticks (C# DateTime.Ticks)
//auto Ticks() {
//    return duration_cast<avledet::util::Ticks>(Nanos());
//}

// Get the time in seconds (Unity Time.time)
float Avledet::Time() const
{
    return (float) ((double) Nanos().count()
                    / (double) std::chrono::duration_cast<std::chrono::nanoseconds>(1s).count());
}

// The time in seconds since the last frame
float Avledet::delta() const
{
    auto elapsed = m_nowUpdate - m_prevUpdate;
    return (float) (((double) elapsed.count() * m_serverTimeMultiplier)
                    / (double) std::chrono::duration_cast<decltype(elapsed)>(1s).count());
}

// The time in nanoseconds since the last frame
std::chrono::nanoseconds Avledet::DeltaNanos() const
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(m_nowUpdate - m_prevUpdate);
}

void Avledet::init()
{
    assert(!m_run_state && "unexpected run state during init(), did you call init() twice?");

    tracy::SetThreadName("avl");

    {
        quill::BackendOptions options;
        options.enable_yield_when_idle = false;
        options.sleep_duration         = 1ms;

        // crash signals flush the log and name the signal; SIGINT/SIGTERM stay ours (graceful stop)
        quill::SignalHandlerOptions signals;
        signals.catchable_signals = {SIGABRT, SIGFPE, SIGILL, SIGSEGV
#ifndef _WIN32
                                     , SIGBUS
#endif
        };
        quill::Backend::start<quill::FrontendOptions>(options, signals);
    }

    {
        std::vector<std::shared_ptr<quill::Sink>> sinks {
                quill::Frontend::create_or_get_sink<quill::ConsoleSink>("server_con"),
                quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
                        "server.log",
                        []() {
                            quill::RotatingFileSinkConfig cfg;
                            cfg.set_open_mode('w');
                            cfg.set_filename_append_option(quill::FilenameAppendOption::StartDateTime);
                            // a long running server must not fill the disk
                            cfg.set_rotation_time_daily("00:00");
                            cfg.set_rotation_max_file_size(64 * 1024 * 1024);
                            cfg.set_max_backup_files(14);
                            return cfg;
                        }()),
        };

        quill::PatternFormatterOptions options {
                "%(time) [%(thread_name)] %(short_source_location:<28) %(log_level:<9) %(message)",
                "%Y-%m-%d %H:%M:%S.%Qms", quill::Timezone::LocalTime};

        auto logger = quill::Frontend::create_or_get_logger("main", std::move(sinks), options);

        logger->set_log_level(quill::LogLevel::TraceL3);
        // breadcrumbs (LOG_BACKTRACE: rpcs, zone generation) are kept in memory and written out
        // only with the next error, so a failure shows what led to it
        logger->init_backtrace(256, quill::LogLevel::Error);

        /* global assigned */ AVL_LOGGER = logger;
    }

    // an exception nobody caught: log it before the abort, the crash handler flushes
    std::set_terminate([] {
        std::string what = "unknown";
        if (auto e = std::current_exception()) {
            try {
                std::rethrow_exception(e);
            } catch (std::exception const &ex) {
                what = ex.what();
            } catch (...) {
            }
        }
        LOG_CRITICAL(AVL_LOGGER, "Uncaught exception: {}", what);
        AVL_LOGGER->flush_log();
        std::abort();
    });

    m_serverID  = VUtils::Random::GenerateUID();
    m_startTime = std::chrono::steady_clock::now();

    this->LoadFiles(false);

    LOG_NOTICE(AVL_LOGGER, "Starting Avledet {} (Valheim {}, network {}, world {})", AVL_VERSION, VConstants::GAME,
               VConstants::NETWORK, VConstants::WORLD);
    LOG_NOTICE(AVL_LOGGER, "Config: server '{}' port {} public {} password {}, max {} players, world '{}' seed '{}' in "
                           "'{}', save every {}s",
               AVL_CONFIG.m_server_name, AVL_CONFIG.m_server_port, AVL_CONFIG.m_server_public,
               AVL_CONFIG.m_server_password.empty() ? "none" : "set", AVL_CONFIG.m_player_limit,
               AVL_CONFIG.m_world_name, AVL_CONFIG.m_world_seed, AVL_CONFIG.m_world_savedir,
               AVL_CONFIG.m_world_save_interval.count());

    //m_worldTime = 2040;
    m_worldTime = GetMorning(1);

    m_serverTimeMultiplier = 1;

    ZdoManager::instance().Init();
#if AVL_IS_ON(AVL_RANDOM_EVENTS)
    RaidManager::instance().Init();
#endif
    PrefabManager::instance().Init();

//TODO add Script Init here, with callback
//ScriptManager::instance().Init()// Will initially load ALL scripts (anything that immediately runs in the body can be considered pre zdo-init [occurs in worldmanager])
#if AVL_IS_ON(AVL_ENABLE_SCRIPTING)
    ScriptManager::instance().Init();
#endif

    ZoneManager::instance().PostPrefabInit();
    PersistentEventManager::instance().Init();
#if AVL_IS_ON(AVL_DUNGEON_GENERATION)
    DungeonManager::instance().post_prefab_init();
#endif
    WorldManager::instance().PostZoneInit();
#if AVL_IS_ON(AVL_ZONE_GENERATION)
    GeoManager::instance().PostWorldInit();
    AltBiomeManager::instance().PostGeoInit();
    HMBuildManager::instance().PostGeoInit();
    ZoneManager::instance().PostGeoInit();
#endif

    WorldManager::instance().PostInit();

    // worldgen probe: dump generated world for comparison with vanilla, then quit
    if (auto out = std::getenv("AVL_PROBE_OUT")) {
        probe_dump(out);
        std::exit(0);
    }

    NetManager::instance().PostInit();

    if (AVL_CONFIG.m_replay_mode != ReplayMode::NONE) {
        avledet::replay::ReplayManager::instance().init();
    }

    AVL_SCRIPT_EVENT(ScriptManager::Events::Enable);

#if AVL_IS_ON(AVL_DISCORD_INTEGRATION)
    DiscordManager::instance().init();
#endif

    AVL_DISPATCH_WEBHOOK("Server started");

    m_prevUpdate = std::chrono::steady_clock::now();
    m_nowUpdate  = m_prevUpdate;

    m_run_state = true;

#ifdef _WIN32
    SetConsoleCtrlHandler(
            [](DWORD dwCtrlType) {
                tracy::SetThreadName("kernel");

                Avledet::instance().Stop();                 // set to true
                Avledet::instance().m_run_state.wait(false);// block until notify by uninit()
                return TRUE;
            },
            TRUE);
#else // !_WIN32
    // The handler runs on the main thread, so it must not wait for the main
    // loop (that deadlocked and the world was never saved). Only set the flag;
    // the main loop then exits and uninit() saves the world.
    auto onStop = [](int) { Avledet::instance().Stop(); };
    signal(SIGINT, onStop);
    signal(SIGTERM, onStop);
#endif// !_WIN32
}

void Avledet::uninit()
{
    AVL_DISPATCH_WEBHOOK("Server stopping");

    LOG_INFO(AVL_LOGGER, "Stopping server");

    // Cleanup
    NetManager::instance().Uninit();
#if AVL_IS_ON(AVL_ZONE_GENERATION)
    HMBuildManager::instance().Uninit();
#endif

#if AVL_IS_ON(AVL_ENABLE_SCRIPTING)
    ScriptManager::instance().Uninit();
#endif

    this->SaveFiles();
    WorldManager::instance().WaitSave();

    LOG_INFO(AVL_LOGGER, "Server gracefully stopped");

    // notify
    m_run_state = true;
}

bool Avledet::update()
{
    ZoneScoped;

    auto now     = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(m_nowUpdate - m_prevUpdate);

    m_prevUpdate = m_nowUpdate;// old state
    m_nowUpdate  = now;        // new state

    // Mutex is scoped
    {
        std::scoped_lock lock(m_taskMutex);
        for (auto itr = m_tasks.begin(); itr != m_tasks.end();) {
            auto ptr = itr->get();
            if (ptr->m_at < now) {
                if (ptr->m_period == std::chrono::milliseconds::min()) {// if task cancelled
                    itr = m_tasks.erase(itr);
                } else {
                    ptr->m_func(*ptr);
                    if (ptr->Repeats()) {
                        ptr->m_at += ptr->m_period;
                        ++itr;
                    } else
                        itr = m_tasks.erase(itr);
                }
            } else
                ++itr;
        }
    }

    // Update();
    // This is important to processing RPC remote invocations
    // ZNet.UpdateNetTime: the time runs while players are on
    if (!NetManager::instance().GetPeers().empty())
        m_worldTime += delta() * m_worldTimeMultiplier;
    // EnvMan.UpdateTimeSkip adds on top and stops exactly at the morning. Its FixedUpdate takes one
    // fixedDeltaTime step (0.0199999921) per frame; the vanilla dedicated server asks for 30 frames a second
    // and gets about 28.2 (measured with the bot, idle and generating): a sleep lasts some 20 s, not 12
    if (m_timeSkip) {
        m_worldTime += delta() * m_timeSkipSpeed * ((double) 0.0199999921f * 28.2);
        if (m_worldTime >= m_skipToTime) {
            m_worldTime = m_skipToTime;
            m_timeSkip  = false;
        }
    }

    AVL_SCRIPT_EVENT(ScriptManager::Events::Update);

    // Unity logs an exception of one MonoBehaviour.Update and runs the next frame: one manager's error
    // does not end the server (and lose everything since the last save)
    auto guarded = [](char const *what, auto &&update) {
        try {
            update();
        } catch (std::exception const &e) {
            if (VUtils::run_periodic<struct guarded_update_log>(5s))
                LOG_ERROR(AVL_LOGGER, "{} update failed: {}", what, e.what());
        }
    };
    guarded("NetManager", [] { NetManager::instance().Update(); });
    guarded("ZdoManager", [] { ZdoManager::instance().Update(); });
    guarded("ZoneManager", [] { ZoneManager::instance().Update(); });
    guarded("PersistentEventManager", [] { PersistentEventManager::instance().Update(); });
#if AVL_IS_ON(AVL_RANDOM_EVENTS)
    guarded("RaidManager", [] { RaidManager::instance().Update(); });
#endif
#if AVL_IS_ON(AVL_ZONE_GENERATION)
    guarded("HMBuildManager", [] { HMBuildManager::instance().Update(); });
#endif

#if AVL_IS_ON(AVL_ENABLE_SCRIPTING)
    ScriptManager::instance().update();
#endif

    //TODO run periodically starting from now?
    if (VUtils::run_periodic<struct server_period_update>(1s))
        guarded("PeriodUpdate", [this] { PeriodUpdate(); });

    // tick time for the status line; a long one is worth a warning (zone generation, a save)
    auto work = std::chrono::steady_clock::now() - now;
    m_tickMax = std::max(m_tickMax, work);
    m_tickSum += work;
    m_tickCount++;
    if (work > 500ms && VUtils::run_periodic<struct slow_tick_warning>(30s))
        LOG_WARNING(AVL_LOGGER, "Slow tick: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(work).count());

    std::this_thread::sleep_for(1ms);

    FrameMark;

    return m_run_state;
}

// resident memory in MB, 0 where /proc is missing
static long ResidentMB()
{
#ifdef _WIN32
    return 0;
#else
    std::ifstream statm("/proc/self/statm");
    long pages = 0, resident = 0;
    if (!(statm >> pages >> resident))
        return 0;
    return resident * sysconf(_SC_PAGESIZE) / (1024 * 1024);
#endif
}

void Avledet::PeriodUpdate()
{
    if (VUtils::run_periodic<struct periodic_status>(5min)) {
        using ms = std::chrono::duration<double, std::milli>;
        LOG_INFO(AVL_LOGGER, "Status: {} online, {} zdos, tick avg {:.1f}ms max {:.1f}ms, rss {}MB",
                 NetManager::instance().GetPeers().size(), ZdoManager::instance().GetZDOCount(),
                 m_tickCount ? ms(m_tickSum).count() / m_tickCount : 0.0, ms(m_tickMax).count(), ResidentMB());
        m_tickMax   = {};
        m_tickSum   = {};
        m_tickCount = 0;
    }

    AVL_SCRIPT_EVENT(ScriptManager::Events::PeriodicUpdate);

#if AVL_IS_ON(AVL_DISCORD_INTEGRATION)
    DiscordManager::instance().period_update();
#endif

#if AVL_IS_ON(AVL_DUNGEON_REGENERATION)
    if (AVL_CONFIG.dungeonsRegenerationInterval > 0s)
        DungeonManager::instance().TryRegenerateDungeons();
#endif


#if AVL_IS_ON(AVL_PLAYER_SLEEP)
    // Game.UpdateSleeping, every 2 s: the sleep ends when the time skip is over
    if (VUtils::run_periodic<struct game_update_sleeping>(2s)) {
        if (m_playerSleep) {
            if (!m_timeSkip) {
                // Wake up players

                if (AVL_CONFIG.playerSleepSolo) {
                    // only awake sleeping players
                    for (auto &&peer : NetManager::instance().GetPeers()) {
                        auto &&zdo = peer->find_zdo();
                        if (zdo && zdo->get_bool(avledet::util::hashes::ZDO::Player::IN_BED, false)) {
                            RouteManager::instance().Invoke(peer->GetUserID(),
                                                   avledet::util::hashes::Routed::S2C_RequestStopSleep);
                        }
                    }
                } else {
                    // wake every player
                    RouteManager::instance().InvokeAll(avledet::util::hashes::Routed::S2C_RequestStopSleep);
                }

                m_playerSleep    = false;
                m_playerSleepEnd = m_worldTime;
                LOG_INFO(AVL_LOGGER, "Sleep ended, day {}", GetDay());

                // Game.SleepStop: save if the last save is over a minute old
                if (WorldManager::instance().SinceSave() > 60s)
                    WorldManager::instance().Save();
            }
        } else {
            // Game.UpdateSleeping: EnvMan afternoon or night (day fraction >= 0.5 or <= 0.15), not within
            // 10 s of the last sleep
            auto timeOfDay = GetTimeOfDay(m_worldTime);
            if (!m_timeSkip && (timeOfDay >= 0.5 * WORLD_TIME_LENGTH || timeOfDay <= 0.15 * WORLD_TIME_LENGTH)
                && m_worldTime - m_playerSleepEnd >= 10) {
                bool allInBed = true;
                bool anyInBed = false;
                int characters = 0;

                // EverybodyIsTryingToSleep: only peers with a character count (dead or loading ones do not)
                for (auto &&peer : NetManager::instance().GetPeers()) {
                    auto &&zdo = peer->find_zdo();
                    if (!zdo)
                        continue;
                    characters++;
                    bool inBed = zdo->get_bool(avledet::util::hashes::ZDO::Player::IN_BED, false);
                    if (!inBed) {
                        allInBed = false;
                        if (!AVL_CONFIG.playerSleepSolo)// early break if special sleep mode is not enabled
                            break;
                    } else {
                        // Early break if the special sleep is enabled
                        if (AVL_CONFIG.playerSleepSolo) {
                            anyInBed = true;
                            break;
                        }
                    }
                }

                if ((allInBed || (anyInBed && AVL_CONFIG.playerSleepSolo)) && characters > 0) {
                    m_playerSleep = true;

                    SkipToMorning();
                    LOG_INFO(AVL_LOGGER, "{} players sleep, skipping {:.0f}s to the morning",
                             NetManager::instance().GetPeers().size(), m_skipToTime - m_worldTime);

                    if (AVL_CONFIG.playerSleepSolo) {
                        // Players who are ALREADY in bed, go ahead and signal them to sleep
                        for (auto &&peer : NetManager::instance().GetPeers()) {
                            auto &&zdo = peer->find_zdo();
                            if (zdo && zdo->get_bool(avledet::util::hashes::ZDO::Player::IN_BED, false)) {
                                RouteManager::instance().Invoke(peer->GetUserID(),
                                                       avledet::util::hashes::Routed::S2C_RequestSleep);
                            } else {
                                peer->CornerMessage("The world is sleeping");
                            }
                        }
                    } else {
                        // Just signal to all players to sleep
                        //  This assumes they are all already in bed
                        RouteManager::instance().InvokeAll(avledet::util::hashes::Routed::S2C_RequestSleep);
                    }
                }
            }
        }
    }
    //}
#endif


    std::error_code err;
    auto lastWriteTime = std::filesystem::last_write_time("server.yml", err);
    if (lastWriteTime != m_settingsLastTime) {
        // reload the file
        LOG_INFO(AVL_LOGGER, "Config change detected!");
        LoadFiles(true);
        LOG_INFO(AVL_LOGGER, "Config was reloaded");
    }

    WorldManager::instance().Update();

    // Game.UpdateSaving: the interval counts from the last save of any kind, a warning 30 s before
    if (AVL_CONFIG.m_world_save_interval > 0s) {
        // the warning fires when the timer crosses the mark, any save (sleep, admin) restarts it
        auto since = WorldManager::instance().SinceSave();
        if (since < AVL_CONFIG.m_world_save_interval - 30s)
            m_saveWarned = false;
        else if (!m_saveWarned) {
            m_saveWarned = true;
            LOG_INFO(AVL_LOGGER, "World saving in 30s");
            Broadcast(UIMsgType::Center, "$msg_worldsavewarning 30s");
        }
        if (since > AVL_CONFIG.m_world_save_interval)
            NetManager::instance().SaveWorld();
    }
}

// EnvMan.SkipToMorning and GetMorningStartSec. The IL rounds only (float)(day * length): Mono keeps
// length * 0.15f and the sums in double
void Avledet::SkipToMorning()
{
    double morning  = (double) (float) WORLD_TIME_LENGTH * (double) 0.15f;
    int day         = (int) ((m_worldTime - morning) / (double) WORLD_TIME_LENGTH);
    m_skipToTime    = (double) (float) ((std::int64_t) (day + 1) * WORLD_TIME_LENGTH) + morning;
    m_timeSkip      = true;
    m_timeSkipSpeed = (m_skipToTime - m_worldTime) / 12.0;
}

// Intended to be ran from ANY thread
void Avledet::Stop()
{
    m_run_state = false;
}

void Avledet::Start()
{
    this->init();

    while (this->update()) {}

    this->uninit();
}

Task &Avledet::RunTask(Task::F f)
{
    return RunTaskLater(std::move(f), 0ms);
}

Task &Avledet::RunTaskLater(Task::F f, std::chrono::milliseconds after)
{
    return RunTaskLaterRepeat(std::move(f), after, -1ms);
}

Task &Avledet::RunTaskAt(Task::F f, std::chrono::steady_clock::time_point at)
{
    return RunTaskAtRepeat(std::move(f), at, -1ms);
}

Task &Avledet::RunTaskRepeat(Task::F f, std::chrono::milliseconds period)
{
    return RunTaskLaterRepeat(std::move(f), 0ms, period);
}

Task &Avledet::RunTaskLaterRepeat(Task::F f, std::chrono::milliseconds after,
                                   std::chrono::milliseconds period)
{
    return RunTaskAtRepeat(std::move(f), std::chrono::steady_clock::now() + after, period);
}

Task &Avledet::RunTaskAtRepeat(Task::F f, std::chrono::steady_clock::time_point at,
                                std::chrono::milliseconds period)
{
    std::scoped_lock lock(m_taskMutex);
    m_tasks.push_back(std::make_unique<Task>(f, at, period));
    return *m_tasks.back();
}

void Avledet::Broadcast(UIMsgType type, std::string_view text)
{
    RouteManager::instance().InvokeAll(avledet::util::hashes::Routed::S2C_UIMessage, type, text);
}
