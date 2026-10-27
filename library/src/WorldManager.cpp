#include <format>
#include <future>
#include <regex>
#include <quill/std/FilesystemPath.h>
#include <string>

#include "Avledet.h"
#include "CompileSettings.h"
#include "DataStream.h"
#include "NetManager.h"
#include "PersistentEventManager.h"
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
    if (version > VConstants::WORLD)
        throw std::runtime_error("world meta of a newer game: " + std::to_string(version));

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
    // an unreadable dir is not "no save": a new world would sweep the chunks of the real one
    if (err && err != std::errc::no_such_file_or_directory)
        throw std::runtime_error("cannot read " + dir.string() + ": " + err.message());
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

    auto db      = read("db2");
    auto version = db.read<std::int32_t>();
    if (version < 40)
        throw std::runtime_error("world data predates the chunked save");
    // Version.IsWorldVersionCompatible: a newer game wrote it, its layout is unknown here
    if (version > VConstants::WORLD)
        throw std::runtime_error("world data of a newer game: " + std::to_string(version));
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
        PersistentEventManager::instance().Load(db.read<avledet::util::Bytes>());

    ZdoManager::instance().LoadChunks(dir, save);

    m_saveNumber = save;

    LOG_NOTICE(AVL_LOGGER, "Loaded world '{}' save {} in {}ms", m_world->m_name, save,
               std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                       .count());
}

void WorldManager::Save(std::filesystem::path const &dir)
{
    WaitSave();

    auto start = std::chrono::steady_clock::now();
    m_lastSave = start;
    SaveJob job {dir, m_saveNumber + 1, m_saveNumber, {}};
    auto main  = [&](std::string_view ext) { return std::format("_main.{}.{}", job.m_save, ext); };

    // vanilla order: chunks and their index, .db2, .fwl2, then the .ok marker
    job.m_files = ZdoManager::instance().SaveChunks(job.m_save);

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
    db.write(PersistentEventManager::instance().Save());
    job.m_files.emplace_back(main("db2"), db.release());
    job.m_files.emplace_back(main("fwl2"), m_world->SaveMeta());
    DataWriter ok;
    ok.write(VConstants::WORLD);
    job.m_files.emplace_back(main("ok"), ok.release());

    LOG_INFO(AVL_LOGGER, "World save {} serialized in {}ms", job.m_save,
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());

    m_savingNumber         = job.m_save;
    m_saveOk               = false;
    m_saveDone             = false;
    m_saveThreadStart      = std::chrono::steady_clock::now();
    m_saveSerializeSeconds = std::chrono::duration<float>(m_saveThreadStart - start).count();
    // ZNet.ConsiderAutoBackup: not before 20 minutes of the session
    job.m_backup           = Avledet::instance().Elapsed() >= std::chrono::seconds(1200);
    m_saveThread           = std::thread([this, job = std::move(job)] {
        // an exception here must not end the server (std::terminate): the previous save stays
        try {
            m_saveOk = WriteSave(job);
        } catch (std::exception const &e) {
            LOG_ERROR(AVL_LOGGER, "World save {} failed: {}", job.m_save, e.what());
            m_saveOk = false;
        }
        m_saveDone = true;
    });
}

// SaveSystem.ConsiderBackup: copies of the world dir, <world>_backup_auto-yyyyMMdd-HHmmss, before the new save.
// A new one when the newest is older than backup-short; the newest is replaced unless the second newest is
// older than backup-long, then the oldest goes (or a new one is added below the count).
void WorldManager::ConsiderBackup(std::filesystem::path const &dir)
{
    namespace fs = std::filesystem;
    int count = AVL_CONFIG.m_world_backups == 1 ? 0 : AVL_CONFIG.m_world_backups;
    if (count <= 0 || !fs::exists(dir))
        return;
    std::error_code err;
    auto prefix   = dir.filename().string() + "_backup_auto-";
    auto modified = [&](fs::path const &p) {
        auto newest = fs::file_time_type::min();
        for (auto &&f : fs::directory_iterator(p, err))
            newest = std::max(newest, fs::last_write_time(f.path(), err));
        return newest;
    };
    std::vector<std::pair<fs::file_time_type, fs::path>> backups;
    for (auto &&entry : fs::directory_iterator(dir.parent_path(), err))
        if (entry.is_directory() && entry.path().filename().string().starts_with(prefix))
            backups.emplace_back(modified(entry.path()), entry.path());
    std::ranges::sort(backups, std::greater<> {});
    if (backups.size() > (std::size_t) count)
        backups.resize(count);

    auto now = fs::file_time_type::clock::now();
    std::optional<fs::path> replace;
    if (!backups.empty()) {
        if (!(now - AVL_CONFIG.m_world_backup_short > backups[0].first))
            return;
        if (backups.size() >= 2) {
            if (now - AVL_CONFIG.m_world_backup_long > backups[1].first) {
                if (backups.size() >= (std::size_t) count)
                    replace = backups.back().second;
            } else
                replace = backups[0].second;
        }
    }
    if (replace)
        fs::remove_all(*replace, err);

    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local {};
    localtime_r(&t, &local);
    char date[32];
    std::strftime(date, sizeof(date), "%Y%m%d-%H%M%S", &local);
    auto target = dir.parent_path() / (prefix + date);
    fs::create_directories(target, err);
    for (auto &&f : fs::directory_iterator(dir, err)) {
        auto ext = f.path().extension().string();
        if (ext == ".fwl2" || ext == ".db2" || ext == ".chunks" || ext == ".ok" || ext == ".chunk")
            fs::copy_file(f.path(), target / f.path().filename(), fs::copy_options::overwrite_existing, err);
    }
    LOG_INFO(AVL_LOGGER, "Backup created in {}", target.filename().string());
}

bool WorldManager::WriteSave(SaveJob const &job)
{
    auto start = std::chrono::steady_clock::now();
    std::error_code err;
    if (job.m_backup)
        ConsiderBackup(job.m_dir);
    std::filesystem::create_directories(job.m_dir, err);

    for (auto &&[name, bytes] : job.m_files) {
        if (!VUtils::Resource::WriteFile(job.m_dir / name, bytes)) {
            LOG_ERROR(AVL_LOGGER, "World save {} failed writing {}", job.m_save, name);
            // the previous save stays the newest one with an .ok
            for (auto &&[written, _] : job.m_files)
                std::filesystem::remove(job.m_dir / written, err);
            return false;
        }
    }

    // the new save is complete: drop the previous one and chunk files nothing lists
    for (auto ext : {"fwl2", "db2", "chunks", "ok"})
        std::filesystem::remove(job.m_dir / std::format("_main.{}.{}", job.m_previous, ext), err);
    for (auto &&entry : std::filesystem::directory_iterator(job.m_dir, err)) {
        auto name = entry.path().filename().string();
        if (name.ends_with(".chunk")
            && std::ranges::none_of(job.m_files, [&](auto &&file) { return file.first == name; }))
            std::filesystem::remove(entry.path(), err);
    }

    LOG_INFO(AVL_LOGGER, "World save {} ({} files) written in {}ms", job.m_save, job.m_files.size(),
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    return true;
}

void WorldManager::WaitSave()
{
    if (!m_saveThread.joinable())
        return;
    m_saveThread.join();
    if (m_saveOk)
        m_saveNumber = m_savingNumber;
}

void WorldManager::Update()
{
    if (!m_saveThread.joinable() || !m_saveDone)
        return;
    float written = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_saveThreadStart).count();
    WaitSave();
    if (m_saveOk)
        Avledet::instance().Broadcast(UIMsgType::TopLeft, std::format("$msg_worldsaved ( {:.2f}+{:.2f}s )",
                                                                      m_saveSerializeSeconds, written));
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
    } else if (auto meta = VUtils::Resource::ReadFile<avledet::util::Bytes>(dir / "_main.0.fwl2")) {
        // a new vanilla world is only its meta (needsDB false): its seed, uid and world modifiers
        m_world = std::make_unique<World>(DataReader(std::move(*meta)));
        for (auto &&key : m_world->m_startingGlobalKeys)
            ZoneManager::instance().GlobalKeyAdd(key, false);
        LOG_INFO(AVL_LOGGER, "New vanilla world from {}", (dir / "_main.0.fwl2").string());
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
