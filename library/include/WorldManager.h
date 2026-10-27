#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>
#include <future>
#include <thread>

#include "DataStream.h"
#include "Manager.h"
#include "Types.h"
#include "VUtils.h"

class WorldManager;

// .fwl2 meta of a vanilla 1.0 world
class World
{
    friend class WorldManager;

  public:
    std::string m_name;
    std::string m_seedName;
    avledet::util::Hash m_seed;
    std::int64_t m_uid;
    std::int32_t m_worldGenVersion;
    // server options (world modifiers), applied as global keys
    std::vector<std::string> m_startingGlobalKeys;
    // ZNet.CrossNetworkUserInfo: platform user id, display name, server display name, playfab id
    std::vector<std::array<std::string, 4>> m_playerHistory;

  public:
    World(std::string name, std::string seedName);
    explicit World(DataReader reader);

    avledet::util::Bytes SaveMeta() const;
};

// Vanilla 1.0 chunked world save in <savedir>/worlds_local/<world>/, so the same folder works
// for this server and the vanilla one:
//  _main.<N>.fwl2 meta, .db2 time/zones/events, .chunks index, .ok marker, YY_XX__S_V.chunk ZDOs
class WorldManager : public avledet::util::IManager<WorldManager>
{
  private:
    std::unique_ptr<World> m_world;
    std::uint32_t m_saveNumber = 0;
    // PersistentEventSystem data, passed through
    avledet::util::Bytes m_persistentEvents;

    // a serialized save, written to disk off the main thread like vanilla's save thread
    struct SaveJob
    {
        std::filesystem::path m_dir;
        std::uint32_t m_save;
        std::uint32_t m_previous;
        std::vector<std::pair<std::string, avledet::util::Bytes>> m_files;// in write order, .ok last
    };
    std::thread m_saveThread;
    std::atomic<bool> m_saveOk;
    std::uint32_t m_savingNumber = 0;

    static bool WriteSave(SaveJob const &job);

    // newest save number with an .ok marker
    static std::optional<std::uint32_t> FindSave(std::filesystem::path const &dir);
    void Load(std::filesystem::path const &dir, std::uint32_t save);

  public:
    World *GetWorld();

    std::filesystem::path GetWorldDir() const;

    // serializes save N+1 now and writes it in the background, then deletes save N and the
    // chunk files it no longer lists; a save still being written is waited for first
    void Save(std::filesystem::path const &dir);
    void Save();
    // waits for the background write
    void WaitSave();

    void PostZoneInit();

    void PostInit();
};
