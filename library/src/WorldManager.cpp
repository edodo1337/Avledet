#include <format>
#include <future>
#include <regex>
#include <quill/std/FilesystemPath.h>
#include <string>

#include "Avledet.h"
#include "CompileSettings.h"
#include "DataStream.h"
#include "NetManager.h"
#include "RandomEventManager.h"
#include "VUtils.h"
#include "VUtilsRandom.h"
#include "VUtilsResource.h"
#include "VUtilsString.h"
#include "WorldManager.h"
#include "ZDOManager.h"
#include "ZoneManager.h"

// ZoneSystem.m_locationVersion and Version.World of 1.0.20
static_assert(VConstants::WORLD == 42);

// PersistentEventSystem with no events: Brotli of {"list":[]}
static avledet::util::Bytes const NO_PERSISTENT_EVENTS
        = {0x0b, 0x05, (char) 0x80, '{', '"', 'l', 'i', 's', 't', '"', ':', '[', ']', '}', 0x03};

World::World(std::string name, std::string seedName)
{
    m_name            = std::move(name);
    m_seedName        = std::move(seedName);
    m_seed            = avledet::util::get_stable_hash(m_seedName);
    m_uid             = VUtils::Random::GenerateUID();
    m_worldGenVersion = VConstants::WORLDGEN;
}

World::World(DataReader reader)
{
    reader = DataReader(reader.read<avledet::util::Bytes>());

    auto version = reader.read<std::int32_t>();
    if (version < 40)
        throw std::runtime_error("world meta predates the chunked save: " + std::to_string(version));

    m_name            = reader.read<std::string>();
    m_seedName        = reader.read<std::string>();
    m_seed            = reader.read<avledet::util::Hash>();
    m_uid             = reader.read<std::int64_t>();
    m_worldGenVersion = reader.read<std::int32_t>();
    reader.read<bool>();// needsDB
    auto keys = reader.read<std::int32_t>();
    for (int i = 0; i < keys; i++)
        m_startingGlobalKeys.push_back(reader.read<std::string>());
    if (version >= 41) {
        auto users = reader.read<std::int32_t>();
        for (int i = 0; i < users; i++) {
            auto &&user = m_playerHistory.emplace_back();
            for (auto &&field : user)
                field = reader.read<std::string>();
        }
    }
}

avledet::util::Bytes World::SaveMeta() const
{
    DataWriter pkg;
    pkg.write(VConstants::WORLD);
    pkg.write(m_name);
    pkg.write(m_seedName);
    pkg.write(m_seed);
    pkg.write(m_uid);
    pkg.write(m_worldGenVersion);
    pkg.write(true);// needsDB
    pkg.write((std::int32_t) m_startingGlobalKeys.size());
    for (auto &&key : m_startingGlobalKeys)
        pkg.write(key);
    pkg.write((std::int32_t) m_playerHistory.size());
    for (auto &&user : m_playerHistory) {
        for (auto &&field : user)
            pkg.write(field);
    }

    DataWriter file;
    file.write(pkg.get_buf());
    return file.get_buf();
}

World *WorldManager::GetWorld()
{
    return m_world.get();
}

std::filesystem::path WorldManager::GetWorldDir() const
{
    return std::filesystem::path(AVL_CONFIG.m_world_savedir) / "worlds_local" / AVL_CONFIG.m_world_name;
}

std::optional<std::uint32_t> WorldManager::FindSave(std::filesystem::path const &dir)
{
    std::optional<std::uint32_t> save;
    std::error_code err;
    static std::regex const ok(R"(_main\.(\d+)\.ok)");
    for (auto &&entry : std::filesystem::directory_iterator(dir, err)) {
        std::smatch match;
        auto name = entry.path().filename().string();
        if (std::regex_match(name, match, ok)) {
            auto n = (std::uint32_t) std::stoul(match[1]);
            if (!save || n > *save)
                save = n;
        }
    }
    return save;
}

void WorldManager::Load(std::filesystem::path const &dir, std::uint32_t save)
{
    auto read = [&](std::string const &ext) {
        auto path = dir / std::format("_main.{}.{}", save, ext);
        auto opt  = VUtils::Resource::ReadFile<avledet::util::Bytes>(path);
        if (!opt)
            throw std::runtime_error("missing " + path.string());
        return DataReader(std::move(*opt));
    };

    auto start = std::chrono::steady_clock::now();
    m_world    = std::make_unique<World>(read("fwl2"));

    auto db = read("db2");
    if (db.read<std::int32_t>() < 40)
        throw std::runtime_error("world data predates the chunked save");
    Avledet::instance().m_worldTime = db.read<double>();
    ZoneManager::instance().Load(db, m_world->m_startingGlobalKeys);
#if AVL_IS_ON(AVL_RANDOM_EVENTS)
    RaidManager::instance().Load(db, VConstants::WORLD);
#else
    db.read<float>();
    db.read<std::string_view>();
    db.read<float>();
    db.read<Vector3f>();
#endif
    // vanilla reads this section only when 4 bytes are left
    if (db.size() - db.get_pos() >= 4)
        m_persistentEvents = db.read<avledet::util::Bytes>();

    ZdoManager::instance().LoadChunks(dir, save);

    m_saveNumber = save;

    LOG_NOTICE(AVL_LOGGER, "Loaded world '{}' save {} in {}ms", m_world->m_name, save,
               std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                       .count());
}

void WorldManager::Save(std::filesystem::path const &dir)
{
    auto start = std::chrono::steady_clock::now();
    std::filesystem::create_directories(dir);
    auto const save = m_saveNumber + 1;
    auto file       = [&](std::uint32_t n, std::string_view ext) { return dir / std::format("_main.{}.{}", n, ext); };
    auto write      = [&](std::filesystem::path const &path, avledet::util::Bytes const &bytes) {
        if (!VUtils::Resource::WriteFile(path, bytes))
            throw std::runtime_error("failed to write " + path.string());
    };

    // vanilla order: chunks and their index, .db2, .fwl2, then the .ok marker
    auto chunks = ZdoManager::instance().SaveChunks(dir, save);

    DataWriter db;
    db.write(VConstants::WORLD);
    db.write(Avledet::instance().GetWorldTime());
    ZoneManager::instance().Save(db);
#if AVL_IS_ON(AVL_RANDOM_EVENTS)
    RaidManager::instance().Save(db);
#else
    db.write(0.f);
    db.write("");
    db.write(0.f);
    db.write(Vector3f::ZERO);
#endif
    db.write(m_persistentEvents.empty() ? NO_PERSISTENT_EVENTS : m_persistentEvents);
    write(file(save, "db2"), db.get_buf());
    write(file(save, "fwl2"), m_world->SaveMeta());

    DataWriter ok;
    ok.write(VConstants::WORLD);
    write(file(save, "ok"), ok.get_buf());

    // the new save is complete: drop the previous one and chunk files nothing lists
    std::error_code err;
    for (auto ext : {"fwl2", "db2", "chunks", "ok"})
        std::filesystem::remove(file(m_saveNumber, ext), err);
    for (auto &&entry : std::filesystem::directory_iterator(dir, err)) {
        auto name = entry.path().filename().string();
        if (name.ends_with(".chunk") && std::ranges::find(chunks, name) == chunks.end())
            std::filesystem::remove(entry.path(), err);
    }
    m_saveNumber = save;

    LOG_INFO(AVL_LOGGER, "World save {} ({} chunks) took {}ms", save, chunks.size(),
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

void WorldManager::Save()
{
    try {
        Save(GetWorldDir());
    } catch (std::exception const &e) {
        LOG_ERROR(AVL_LOGGER, "World save failed: {}", e.what());
    }
}

void WorldManager::PostZoneInit()
{
    LOG_NOTICE(AVL_LOGGER, "Initializing WorldManager");

    auto dir = GetWorldDir();
    if (auto save = FindSave(dir)) {
        // a broken save must not be overwritten by a fresh world
        Load(dir, *save);
    } else {
        LOG_INFO(AVL_LOGGER, "No save in {}, creating a new world", dir.string());
        m_world = std::make_unique<World>(AVL_CONFIG.m_world_name, AVL_CONFIG.m_world_seed);
    }

    LOG_NOTICE(AVL_LOGGER, "World '{}' seed {} ({})", m_world->m_name, m_world->m_seedName, m_world->m_seed);
}

void WorldManager::PostInit()
{
#ifdef AVL_OPTION_ENABLE_CAPTURE
    if (AVL_CONFIG.packetMode == PacketMode::CAPTURE) {
        // save world as a copy to captures
        Save(std::filesystem::path(AVL_CAPTURE_PATH) / m_world->m_name
             / std::to_string(AVL_CONFIG.packetCaptureSessionIndex));
    }
#endif//AVL_OPTION_ENABLE_CAPTURE
}
