
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <quill/LogMacros.h>
#include <stdexcept>

#include <range/v3/all.hpp>
#include <utility>
#include <vector>

#include "Avledet.h"
#include "DataStream.h"
#include "Hashes.h"
#include "NetManager.h"
#include "Peer.h"
#include "RouteManager.h"
#include "ServerSettings.h"
#include "VUtils.h"
#include "VUtilsResource.h"
#include "ZDO.h"
#include "ZDOManager.h"
#include "ZoneManager.h"

void ZdoManager::Init()
{
    m_nextUid = 1;

    LOG_NOTICE(AVL_LOGGER, "Initializing ZDOManager");

    RouteManager::instance().Register("DestroyZDO", [this](Peer::Ptr, DataReader reader) {
        // TODO constraint check
        reader.read([this](ZDOID zdoid) { EraseZDO(zdoid); });
    });

    //auto&& insert = ZdoManager::instance().m_objectsByID.begin()->second->
    //m_members.insert({0, ZDO::Ord()});
    //insert.first->second.Get
    RouteManager::instance().Register("RequestZDO",
                             [this](Peer::Ptr peer, ZDOID id) { peer->ForceSendZDO(id); });
}

void ZdoManager::Update()
{
    ZoneScoped;


    //assert(std::accumulate(m_objectsByPrefab.begin(), m_objectsByPrefab.end(), (std::size_t)0,
    //	[](std::size_t value, const decltype(m_objectsByPrefab)::value_type& v) -> std::size_t {
    //		return value + v.second.size();
    //	}
    //) == m_objectsByID.size());

    if (VUtils::run_periodic<struct link_portals>(5s))
        ConnectPortals();


    auto &&peers = NetManager::instance().GetPeers();

    if (VUtils::run_periodic<struct zdos_release_assign>(AVL_CONFIG.m_zdo_assign_interval)) {
        for (auto &&peer : peers) {
            if (!peer->IsGated()) {
                AssignOrReleaseZDOs(peer);
            }
        }
    }

    // vanilla SendZDOToPeers2 runs once a frame: when the timer is over 0.05 s (send-interval), one peer a
    // frame, then the wait again. One peer gets a packet every 2 frames, more peers every n + 1
    auto now = std::chrono::steady_clock::now();
    m_sendFrames = std::min(m_sendFrames + std::chrono::duration<double>(now - m_sendClock).count(), 0.5);
    m_sendClock  = now;
    constexpr double frame = 1.0 / Avledet::VANILLA_SERVER_FPS;
    float threshold = std::chrono::duration<float>(AVL_CONFIG.m_zdo_send_interval).count();
    for (; m_sendFrames >= frame; m_sendFrames -= frame) {
        if (peers.empty())
            continue;
        m_sendTimer += (float) frame;
        if (m_nextSendPeer < 0) {
            if (m_sendTimer > threshold) {
                m_nextSendPeer = 0;
                m_sendTimer    = 0;
            }
            continue;
        }
        if (m_nextSendPeer < (int) peers.size())
            SendZDOs(peers[m_nextSendPeer], false);
        if (++m_nextSendPeer >= (int) peers.size())
            m_nextSendPeer = -1;
    }

    if (!m_destroySendList.empty()) {

        // TODO make a member variable?
        //	think about emulated zdo containers (like replaying actions to specific peers)
        //	this is a functionality I might be planning on into the future
        DataWriter writer;//.write(m_destroySendList);
        writer.write(m_destroySendList);

        m_destroySendList.clear();

        RouteManager::instance().InvokeAll(avledet::util::hashes::Routed::DestroyZDO, writer.release());
    }
}

void ZdoManager::_AddZDOToZone(ZDO::reference zdo)
{
    auto &&container = _GetZDOContainer(zdo->get_zone());

    assert(!container.get().contains(zdo)
           && "\nSomething wrong with ZDO; possibilities: \n - zoning encapsulation is broken \n - ZDOID "
              "equal operator is broken");

    auto &&insert = container.get().insert(zdo);

    assert(insert.second);//ensure newly inserted

                          //LOG_INFO(AVL_LOGGER, "zdo added to zone: {} {}", zdo->get_id(), zdo->get_zone());
}

void ZdoManager::_RemoveFromSector(ZDO::reference zdo)
{
    if (auto &&container = _FindZDOContainer(zdo->get_zone())) {
        auto &&erase = container->erase(zdo);

        // ensure zdo was actually erased
        assert(erase);

        //LOG_INFO(AVL_LOGGER, "zdo removed from zone: {} {}", zdo->get_id(), zdo->get_zone());
    } else {
        //TODO otherwise, then poll the sector map
        assert(false);
    }
}

void ZdoManager::_InvalidateZDOZone(ZDO::reference zdo)
{
    for (auto &&peer : NetManager::instance().GetPeers()) {
        peer->ZDOSectorInvalidated(zdo);
    }
}

// Game.SetConnection: the owner online links its portal itself
void ZdoManager::_SetPortalConnection(ZDO::reference portal, ZDOID connection, bool force)
{
    auto owner = portal->get_owner();
    if (!owner || !NetManager::instance().FindPeerByUserID(owner) || force) {
        portal->claim();
        portal->set_connection(ZDOConnector::Type::Portal, connection);
        ForceSendZDO(portal->get_id());
    } else {
        RouteManager::instance().Invoke(owner, "RPC_SetConnection", portal->get_id(), connection);
    }
}

// Game.ConnectPortals, every 5 s: drop broken links, then pair free portals with the same tag
void ZdoManager::ConnectPortals()
{
    using Type = ZDOConnector::Type;
    auto tag   = [](ZDO::reference zdo) { return zdo->get_string(avledet::util::hashes::ZDO::TeleportWorld::TAG); };

    // ClearCurrentlyConnectingPortals: pairs of the last run are forced now
    for (auto &&[a, b] : m_connectingPortals) {
        for (auto &&[from, to] : {std::pair(a, b), std::pair(b, a)}) {
            auto zdo = find_zdo(from);
            if (zdo && zdo->get_connection_zdoid(Type::Portal) != to)
                _SetPortalConnection(zdo, to, true);
        }
    }
    m_connectingPortals.clear();
    auto connecting = [&](ZDOID id) {
        return std::ranges::any_of(m_connectingPortals, [id](auto &&p) { return p.first == id || p.second == id; });
    };

    ZDO::reference_list portals;
    for (auto name : {"portal_wood", "portal_stone", "portal"}) {
        auto list = GetZDOs(avledet::util::get_stable_hash(name));
        portals.insert(portals.end(), list.begin(), list.end());
    }

    for (auto &&zdo : portals) {
        auto target = zdo->get_connection_zdoid(Type::Portal);
        if (!target)
            continue;
        auto other = find_zdo(target);
        if (!other || tag(other) != tag(zdo) || !other->get_connection_zdoid(Type::Portal))
            _SetPortalConnection(zdo, ZDOID::NONE, false);
    }

    int count = 0;
    for (auto &&zdo : portals) {
        if (connecting(zdo->get_id()) || zdo->get_connection_zdoid(Type::Portal))
            continue;
        // FindRandomUnconnectedPortal
        ZDO::reference_list free;
        for (auto &&other : portals) {
            if (other != zdo && tag(other) == tag(zdo) && !other->get_connection_zdoid(Type::Portal)
                && !connecting(other->get_id()))
                free.push_back(other);
        }
        if (free.empty())
            continue;
        auto other = free[m_portalRandom.range(0, (std::int32_t) free.size())];
        m_connectingPortals.emplace_back(zdo->get_id(), other->get_id());
        _SetPortalConnection(zdo, other->get_id(), false);
        _SetPortalConnection(other, zdo->get_id(), false);
        count++;
    }
    if (count)
        LOG_INFO(AVL_LOGGER, "Connected {} portals", count);
}

namespace {
    // Game.m_portalPrefabs: saved apart, in ChunkPortal
    bool IsPortal(avledet::util::Hash prefab)
    {
        static avledet::util::Hash const portals[] = {avledet::util::get_stable_hash("portal_wood"),
                                                      avledet::util::get_stable_hash("portal_stone"),
                                                      avledet::util::get_stable_hash("portal")};
        return std::ranges::find(portals, prefab) != std::end(portals);
    }

    // ZoneSystem.ChunkIndex(1, 0)
    constexpr std::uint16_t CHUNK_PORTAL = 1;

    // ZoneSystem.GetSectorIndex then the 8x8 zone base chunk; outside the 512x512 sectors is sector 0
    std::uint16_t ChunkOf(Vector3f pos)
    {
        auto x = (std::uint32_t) ((int) std::floor((pos.x + 32.f) / 64.f) + 256);
        auto y = (std::uint32_t) ((int) std::floor((pos.z + 32.f) / 64.f) + 256);
        if (x >= 512 || y >= 512)
            x = y = 0;
        return (std::uint16_t) ((x >> 3) | ((y >> 3) << 8));
    }

    // ChunkSaveMapping.GetChunkFilename
    std::string ChunkFile(std::uint16_t chunk, std::uint8_t size, std::uint32_t version)
    {
        return std::format("{:02x}_{:02x}__{}_{}.chunk", chunk >> 8, chunk & 0xFF, size, version);
    }

    ZDOConnector::Type operator|(ZDOConnector::Type a, ZDOConnector::Type b)
    {
        return ZDOConnector::Type(std::to_underlying(a) | std::to_underlying(b));
    }
}// namespace

std::vector<std::pair<std::string, avledet::util::Bytes>> ZdoManager::SaveChunks(std::uint32_t save)
{
    // ZDOExtraData.RegenerateConnectionHashData: one int per linked pair, the target gets type|Target
    avledet::util::Map<ZDOID, ZDOConnectorData> connections;
    avledet::util::Hash nextHash = 1;
    for (auto &&[id, conn] : ZDO::PAIRED_CONNECTORS) {
        auto type = conn.m_type;
        if (type == ZDOConnector::Type::None || !find_zdo(id))
            continue;
        if (type != ZDOConnector::Type::Spawned && !find_zdo(conn.m_target))
            continue;
        auto hash       = nextHash++;
        connections[id] = ZDOConnectorData(type, hash);
        if (conn.m_target)
            connections[conn.m_target] = ZDOConnectorData(type | ZDOConnector::Type::Target, hash);
    }

    std::map<std::uint16_t, std::vector<ZDO const *>> chunks;
    for (auto &&zdo : m_objectsByID) {
        if (zdo->is_persistent())
            chunks[IsPortal(zdo->get_prefab_hash()) ? CHUNK_PORTAL : ChunkOf(zdo->get_position())].push_back(&*zdo);
    }

    std::vector<std::pair<std::string, avledet::util::Bytes>> files;
    DataWriter index;
    index.write((std::int16_t) VConstants::WORLD);
    std::int32_t total = 0;
    for (auto &&[chunk, zdos] : chunks)
        total += (std::int32_t) zdos.size();
    index.write(total);
    index.write((std::int32_t) chunks.size());

    for (auto &&[chunk, zdos] : chunks) {
        DataWriter writer;
        writer.write((std::int16_t) VConstants::WORLD);
        writer.write((std::int32_t) zdos.size());
        for (auto &&zdo : zdos) {
            auto find = connections.find(zdo->get_id());
            zdo->save(writer, find != connections.end() ? &find->second : nullptr);
        }
        files.emplace_back(ChunkFile(chunk, 0, save), writer.release());

        index.write(chunk);
        index.write((std::uint8_t) 0);
        index.write(save);
        index.write((std::int32_t) zdos.size());
    }

    files.emplace_back(std::format("_main.{}.chunks", save), index.release());
    return files;
}

void ZdoManager::LoadChunks(std::filesystem::path const &dir, std::uint32_t save)
{
    auto opt = VUtils::Resource::ReadFile<avledet::util::Bytes>(dir / std::format("_main.{}.chunks", save));
    if (!opt)
        throw std::runtime_error("chunk index missing");
    DataReader index(std::move(*opt));
    if (auto version = index.read<std::uint16_t>(); version > VConstants::WORLD)
        throw std::runtime_error("chunks of a newer game: " + std::to_string(version));
    index.read<std::int32_t>();// total
    auto count = index.read<std::int32_t>();

    int unknown = 0;
    for (int c = 0; c < count; c++) {
        auto chunk   = index.read<std::uint16_t>();
        auto size    = index.read<std::uint8_t>();
        auto version = index.read<std::uint32_t>();
        index.read<std::int32_t>();// zdos

        auto file  = ChunkFile(chunk, size, version);
        auto bytes = VUtils::Resource::ReadFile<avledet::util::Bytes>(dir / file);
        if (!bytes)
            throw std::runtime_error("missing " + file);
        DataReader reader(std::move(*bytes));
        // chunks of an older 1.0 version stay as they were, the format is the same since 40
        if (reader.read<std::int16_t>() < 40)
            throw std::runtime_error(file + " predates the chunked save");
        auto num = reader.read<std::int32_t>();
        for (int i = 0; i < num; i++) {
            // vanilla: loaded ZDOs get user 1 (UnknownFormerUser), user 0 means none to clients
            auto &&insert = _Instantiate(ZDOID(1, m_nextUid++));
            auto &&zdo    = ZDO::make_reference(insert.first);
            if (!zdo->load(reader)) {
                // vanilla keeps them; this server cannot, it has no prefab to run them by
                auto id = zdo->get_id();
                ZDO::m_floats.erase(id);
                ZDO::m_vec3.erase(id);
                ZDO::m_quats.erase(id);
                ZDO::m_ints.erase(id);
                ZDO::m_longs.erase(id);
                ZDO::m_strings.erase(id);
                ZDO::m_byteArrays.erase(id);
                ZDO::TYPED_CONNECTORS.erase(id);
                m_objectsByID.erase(insert.first);
                unknown++;
                continue;
            }
            _AddZDOToZone(zdo);
            m_objectsByPrefab[zdo->get_prefab_hash()].insert(zdo);
        }
        if (reader.get_pos() != reader.size())
            throw std::runtime_error(file + " has unread bytes");
    }
    if (unknown)
        LOG_WARNING(AVL_LOGGER, "Dropped {} zdos with unknown prefabs", unknown);

    _ConnectAll();
    LOG_NOTICE(AVL_LOGGER, "Loaded {} zdos from {} chunks", m_objectsByID.size(), count);
}

// ZDOMan.ConnectPortals, ConnectSpawners, ConnectSyncTransforms: saved pair hashes back to links
void ZdoManager::_ConnectAll()
{
    using Type = ZDOConnector::Type;
    // targets by type and hash
    avledet::util::Map<std::uint64_t, ZDOID> targets;
    auto key = [](Type type, avledet::util::Hash hash) {
        return ((std::uint64_t) std::to_underlying(type) << 32) | (std::uint32_t) hash;
    };
    for (auto &&[id, data] : ZDO::TYPED_CONNECTORS) {
        if (std::to_underlying(data.m_type) & std::to_underlying(Type::Target))
            targets[key(data.m_type, data.m_hash)] = id;
    }

    int portals = 0, spawners = 0, syncs = 0;
    for (auto &&[id, data] : ZDO::TYPED_CONNECTORS) {
        auto zdo = find_zdo(id);
        if (!zdo)
            continue;
        auto find   = targets.find(key(data.m_type | Type::Target, data.m_hash));
        auto target = find != targets.end() ? find->second : ZDOID::NONE;
        switch (data.m_type) {
            case Type::Portal:
                if (auto other = target ? find_zdo(target) : ZDO::nullopt) {
                    zdo->claim();
                    other->claim();
                    zdo->_set_connection(Type::Portal, target);
                    other->_set_connection(Type::Portal, id);
                    portals++;
                }
                break;
            case Type::Spawned:
                // a spawner without its creature is 'done'
                zdo->claim();
                zdo->_set_connection(Type::Spawned, target);
                if (target)
                    targets.erase(find);
                spawners++;
                break;
            case Type::SyncTransform:
                if (target) {
                    zdo->_set_connection(Type::SyncTransform, target);
                    syncs++;
                }
                break;
            default:
                break;
        }
    }
    ZDO::TYPED_CONNECTORS.clear();
    LOG_INFO(AVL_LOGGER, "Connected {} portals, {} spawners, {} sync transforms", portals, spawners, syncs);
}

[[nodiscard]] std::pair<ZDO::smart_set::iterator, bool> ZdoManager::_Instantiate(ZDOID zdoid) noexcept
{
    //https://jguegant.github.io/blogs/tech/performing-try-emplace.html
    //auto &&insert = m_objectsByID.insert(std::make_unique<ZDO>(zdoid));
    auto contained = m_objectsByID.contains(zdoid);//TODO dbg test
    if (contained) {
        if (true) {
            volatile int x = 0;
        }
    }

    auto &&insert = m_objectsByID.insert(ZDO::make_smart(zdoid));

    // if newly inserted, but ALREADY CONTAINED???
    if (insert.second && contained) {
        assert(false);
    }

    //LOG_INFO(AVL_LOGGER, "zdo instantiated: {} {}", insert.second, zdoid);

    return insert;
}

std::pair<ZDO::smart_set::iterator, bool> ZdoManager::_Instantiate(ZDOID zdoid, Vector3f position) noexcept
{
    auto &&insert = _Instantiate(zdoid);

    // if inserted, then set pos
    if (insert.second) {
        //auto&& zdo = ZDO(*insert.first);
        auto &&zdo = ZDO::make_reference(insert.first);

        // Set zone and position of ZDO
        zdo->_set_position(position);
        _AddZDOToZone(zdo);
    }

    return insert;
}

ZDO::reference ZdoManager::_Instantiate(Vector3f position) noexcept
{
    // server ZDOs carry the server session id, as in vanilla
    ZDOID zdoid = ZDOID(AVL_ID, 0);
    for (;;) {
        zdoid.set_id(m_nextUid++);
        auto &&insert = _Instantiate(zdoid, position);
        if (insert.second) {
            // CreateNewZDO: owned by the server until ReleaseNearbyZDOS hands it to a peer
            auto &&zdo = ZDO::make_reference(insert.first);
            zdo->_set_owner(AVL_ID);
            return zdo;
        }
    }
    std::unreachable();
}

//unused
//ZDO::unsafe_value ZdoManager::_TryInstantiate(ZDOID uid, Vector3f position) {
//	// See version #2
//	// ...returns a pair object whose first element is an iterator
//	//		pointing either to the newly inserted element in the
//	//		container or to the element whose key is equivalent...
//	// https://cplusplus.com/reference/unordered_map/unordered_map/insert/
//
//	auto&& insert = _Instantiate(uid, position);
//	if (insert.second)
//		return ZDO::make_unsafe_value(insert.first);
//
//	throw std::runtime_error("zdo already exists");
//}


ZDO::optional ZdoManager::find_zdo(ZDOID id)
{
    if (id) {
        auto &&find = m_objectsByID.find(id);
        if (find != m_objectsByID.end())
            return ZDO::make_optional(find);
    }
    return ZDO::nullopt;
}

/*
std::pair<ZdoManager::ZDO_iterator, bool> ZdoManager::_GetOrInstantiate(ZDOID id, Vector3f def) {
	auto&& insert = _Instantiate(def, id);
	if (!insert.second)
		return insert;
	
	auto&& insert = m_objectsByID.insert({ id, nullptr });
	
	if (!insert.second) // if new insert failed, return it
		return insert;

	auto&& pair = insert.first;

	auto&& zdo = pair->second;

	zdo = std::make_unique<ZDO>(id, def);
	return insert;
}*/


ZDO::reference ZdoManager::Instantiate(Prefab::Reference prefab, Vector3f pos)
{
    auto &&zdo = _Instantiate(pos);
    //zdo.get().m_encoded.SetPrefabIndex(PrefabManager::instance().RequirePrefabIndexByHash(prefab.m_hash));
    //zdo.get().m_pack.Set<ZDO::PREFAB_PACK_INDEX>(PrefabManager::instance().RequirePrefabIndexByHash(prefab.m_hash));
    zdo->_set_prefab_hash(prefab.get().m_hash);
    auto &&s = prefab.get().m_localScale;
    if (prefab.get().AllFlagsPresent(Prefab::Flag::SYNC_INITIAL_SCALE) && !(s.x == 1 && s.y == 1 && s.z == 1))
        zdo->set_local_scale(s, false);

    return zdo;
}

ZDO::reference ZdoManager::Instantiate(avledet::util::Hash hash, Vector3f pos, Vector3f localScale)
{
    auto &&prefab = PrefabManager::instance().get_prefab(hash);
    auto &&zdo    = _Instantiate(pos);
    zdo->_set_prefab_hash(prefab.get().m_hash);
    if (prefab.get().AllFlagsPresent(Prefab::Flag::SYNC_INITIAL_SCALE)
        && !(localScale.x == 1 && localScale.y == 1 && localScale.z == 1))
        zdo->set_local_scale(localScale, false);
    return zdo;
}

//unused
//ZDO::unsafe_value ZdoManager::Instantiate(avledet::util::Hash hash, Vector3f pos, const Prefab** outPrefab) {
//	//auto&& zdo = Instantiate()
//	auto&& prefab = PrefabManager::instance().RequirePrefabByHash(hash);
//	if (outPrefab) *outPrefab = &prefab;
//
//	return Instantiate(prefab, pos);
//}

/*
ZDO::reference ZdoManager::Instantiate(const ZDO& zdo) {
	assert(false);

	auto&& copy = _Instantiate(zdo.get_position());
	
	//copy.get().m_encoded = zdo.m_encoded;
	//copy.get().m_pack = zdo.m_pack;
	copy.get().m_rotation = zdo.m_rotation;

	return copy;
}*/


void ZdoManager::AssignOrReleaseZDOs(Peer::Ptr peer)
{
    ZoneScoped;

    auto &&zone = ZoneManager::WorldToZonePos(peer->m_pos);

    ZDO::reference_list m_tempNearObjects;
    GetZDOs_Zone(zone, m_tempNearObjects);         // get zdos: zone, nearby
    GetZDOs_NeighborZones(zone, m_tempNearObjects);// get zdos: zone, nearby

    for (auto &&zdo : m_tempNearObjects) {
        if (zdo->is_persistent()) {
            if (zdo->is_owner(peer->GetUserID())) {
                // If peer no longer in area of zdo, unclaim zdo
                if (!ZoneManager::instance().ZonesOverlap(zdo->get_zone(), zone)) {
                    zdo->disown();
                }
            } else {
                // If ZDO no longer has owner, or the owner went far away,
                //  Then assign this new peer as owner
                if (!(zdo->has_owner() && ZoneManager::instance().IsPeerNearby(zdo->get_zone(), zdo->get_owner()))
                    && ZoneManager::instance().ZonesOverlap(zdo->get_zone(), zone)) {

                    zdo->set_owner(peer->GetUserID());
                }
            }
        }
    }

    /*
        Better algorithm:
            Would account for the peer with the lowest latency / network issues
            
            So in cases of choosing the best peer for co-op operations,
                the least laggy player becomes the primary ZDO controller

            - When 2 or more players are within same zone, apply algo:
                - The farther the players from each other, the more assignment freedom we have,
                    and not having to worry about Sync issues regarding owner tranfer
                - The closer the players are to each other, the more likely we have to rely on lowest latency player
                    for overlapping/nearby ZDOs

    */

    if (AVL_CONFIG.m_zdo_owner_algo == AssignAlgorithm::RADIUS_LATENCY) {
        // If many players around, use fastest player
        assert(false);
        (void) nullptr;
        //constexpr auto traffic_metric = [](Peer::Ptr peer) {
        //    auto [local, remote] = peer->m_socket->get_connection_quality();
        //    auto ratio           = (local + remote) * 0.5f;

        //    return ratio + (33.0f / (float) peer->m_socket->get_ping());
        //};
        //auto peers = NetManager::instance().GetPeers();
        //std::sort(peers.begin(), peers.end());
        ////auto peers_sorted = peers;
        //for (auto &&otherPeer : peers) {
        //    //otherPeer->m_socket->Q
        //}
    } else if (AVL_CONFIG.m_zdo_owner_algo == AssignAlgorithm::DYNAMIC_RADIUS) {

        static constexpr auto DIST_SMART = 12.0f;

        float minSqDist = std::numeric_limits<float>::max();
        Vector3f closestPos;

        // get the distance to the closest peer
        for (auto &&otherPeer : NetManager::instance().GetPeers()) {
            // skip self
            if (otherPeer == peer)
                continue;

            if (!ZoneManager::instance().IsPeerNearby(ZoneManager::WorldToZonePos(otherPeer->m_pos),
                                             peer->GetUserID()))
                continue;

            float sqDist = otherPeer->m_pos.sq_distance_to(peer->m_pos);
            if (sqDist < minSqDist) {
                minSqDist  = sqDist;
                closestPos = otherPeer->m_pos;

                if (minSqDist <= DIST_SMART * DIST_SMART) {
                    break;
                }
            }
        }

        // If other "possible owner" peer is FAR
        if (minSqDist < std::numeric_limits<float>::max() && minSqDist > DIST_SMART * DIST_SMART) {
            // Get zdos immediate to this peer
            float const dist = std::sqrt(minSqDist);
            // weird algo, basically
            float const cutoff_dist = dist * 0.5f - 2.0f;
            auto zdos               = GetZDOs(peer->m_pos, cutoff_dist);

            // Basically reassign zdos from another owner to me instead
            for (auto &&zdo : zdos) {
                if (zdo->is_persistent()
                    && zdo->get_position().sq_distance_to(closestPos)
                               > DIST_SMART * DIST_SMART// Ensure the ZDO is far from the other player
                ) {
                    zdo->set_owner(peer->GetUserID());
                }
            }
        }
    }
}

ZDO::smart_set::iterator ZdoManager::_EraseZDO(ZDO::smart_set::iterator itr)
{
    assert(itr != m_objectsByID.end());

    auto &&zdo   = ZDO::make_reference(itr);
    auto &&zdoid = zdo->get_id();

    // update local next only if im the user who created the zdo
    if (zdoid.get_user_id() == AVL_ID) {
        this->m_nextUid = std::max(this->m_nextUid, zdoid.get_id() + 1);
    }

    //VLOG(2) << "Destroying zdo (" << zdo->get_prefab().m_name << ")";

    _RemoveFromSector(zdo);
    {
        auto &&pfind = m_objectsByPrefab.find(zdo->get_prefab_hash());
        if (pfind != m_objectsByPrefab.end())
            pfind->second.erase(zdoid);
    }

    // cleans up some zdos
    for (auto &&peer : NetManager::instance().GetPeers()) {
        peer->m_zdos.erase(zdoid);
    }

    m_erasedZDOs.insert(zdoid);

    //erase members
    ZDO::m_floats.erase(zdoid);
    ZDO::m_vec3.erase(zdoid);
    ZDO::m_quats.erase(zdoid);
    ZDO::m_ints.erase(zdoid);
    ZDO::m_longs.erase(zdoid);
    ZDO::m_strings.erase(zdoid);
    ZDO::m_byteArrays.erase(zdoid);

    ZDO::PAIRED_CONNECTORS.erase(zdoid);

    //LOG_INFO(AVL_LOGGER, "zdo erased: {}", zdoid);

    return m_objectsByID.erase(itr);
}

// ZDOMan.FindSectorObjects for a peer's simulation distance: rings of zones around the peer, squares when
// classic, else only zones whose center is within the radius (ZoneSystem.ZonesWithinRadius)
void ZdoManager::GetZDOs_ActiveZones(ZoneID zone, SimulationDistance const &sd, ZDO::reference_list &out,
                                     ZDO::reference_list &outDistant)
{
    auto within = [&](ZoneID z, int radius, bool ghost) {
        float r  = (float) radius * 64.f + (ghost ? 64.f * 0.8f : 64.f * 0.5f);
        float dx = (float) (z.x - zone.x) * 64.f, dz = (float) (z.y - zone.y) * 64.f;
        return dx * dx + dz * dz < r * r;
    };
    auto ring = [&](int k, auto &&f) {
        for (int x = zone.x - k; x <= zone.x + k; x++) {
            f(ZoneID(x, zone.y - k));
            f(ZoneID(x, zone.y + k));
        }
        for (int y = zone.y - k + 1; y <= zone.y + k - 1; y++) {
            f(ZoneID(zone.x - k, y));
            f(ZoneID(zone.x + k, y));
        }
    };
    auto nearZone = [&](ZoneID z) {
        return std::max(std::abs(z.x - zone.x), std::abs(z.y - zone.y)) <= sd.m_near
            && (sd.m_classic || within(z, sd.m_near, false));
    };

    GetZDOs_Zone(zone, out);
    for (int k = 1; k <= sd.m_near; k++)
        ring(k, [&](ZoneID z) {
            if (nearZone(z))
                GetZDOs_Zone(z, out);
        });
    int total = sd.m_near + sd.m_far;
    for (int k = sd.m_classic ? sd.m_near + 1 : 1; k <= total; k++)
        ring(k, [&](ZoneID z) {
            if ((sd.m_classic || within(z, total, true)) && !nearZone(z))
                GetZDOs_Distant(z, outDistant);
        });
}

void ZdoManager::GetZDOs_NeighborZones(ZoneID zone, ZDO::reference_list &sectorObjects)
{
    for (auto z = zone.y - ZoneManager::NEAR_ZRADIUS; z <= zone.y + ZoneManager::NEAR_ZRADIUS; z++) {
        for (auto x = zone.x - ZoneManager::NEAR_ZRADIUS; x <= zone.x + ZoneManager::NEAR_ZRADIUS; x++) {
            auto current = ZoneID(x, z);
            // Skip the center zone
            if (current == zone)
                continue;

            GetZDOs_Zone(current, sectorObjects);
        }
    }
}

std::list<std::pair<ZDO::reference, float>> ZdoManager::CreateSyncList(Peer::Ptr peer)
{
    auto zone = ZoneManager::WorldToZonePos(peer->m_pos);

    // Gather all updated ZDO's
    ZDO::reference_list zoneZDOs;
    ZDO::reference_list distantZDOs;
    GetZDOs_ActiveZones(zone, peer->m_simDistance, zoneZDOs, distantZDOs);

    std::list<std::pair<ZDO::reference, float>> result;

    // ServerSortSendZDOS: distance minus 1.5 m per second since the last send (up to 100 s)
    auto const time(Avledet::instance().Time());
    for (auto &&zdo : zoneZDOs) {
        decltype(Peer::m_zdos)::iterator outItr;
        if (peer->IsOutdatedZDO(zdo, outItr)) {
            float age = outItr != peer->m_zdos.end() ? std::clamp(time - outItr->second.second, 0.f, 100.f) : 100.f;
            result.push_back({zdo, zdo->get_position().distance_to(peer->m_pos) - age * 1.5f});
        }
    }

    // ServerSendCompare: prioritized objects owned by others first, then higher type, then the value
    result.sort([&](std::pair<ZDO::reference, float> const &first, std::pair<ZDO::reference, float> const &second) {
        auto &&a   = first.first;
        auto &&b   = second.first;
        bool flag  = a->get_type() == avledet::util::ObjectType::PRIORITIZED && a->has_owner()
                 && !a->is_owner(peer->GetUserID());
        bool flag2 = b->get_type() == avledet::util::ObjectType::PRIORITIZED && b->has_owner()
                  && !b->is_owner(peer->GetUserID());
        if (flag != flag2)
            return flag;
        if (flag || a->get_type() == b->get_type())
            return first.second < second.second;
        return a->get_type() > b->get_type();
    });

    // Add a minimum amount of ZDOs
    if (result.size() < 10) {
        for (auto &&zdo2 : distantZDOs) {
            if (peer->IsOutdatedZDO(zdo2)) {
                result.push_back({zdo2, 0});
            }
        }
    }

    // Add forcible send ZDOs
    for (auto &&itr = peer->m_forceSend.begin(); itr != peer->m_forceSend.end();) {
        auto &&zdoid = *itr;
        auto zdo     = find_zdo(zdoid);
        if (zdo && peer->IsOutdatedZDO(zdo)) {
            result.push_front({zdo, 0});
            ++itr;
        } else {
            itr = peer->m_forceSend.erase(itr);
        }
    }

    return result;
}

void ZdoManager::GetZDOs_Zone(ZoneID zone, ZDO::reference_list &objects)
{
    if (auto &&container = _FindZDOContainer(zone)) {
        objects.insert(objects.end(), container->begin(), container->end());
    }
}

void ZdoManager::GetZDOs_Distant(ZoneID zone, ZDO::reference_list &objects)
{
    if (auto &&container = _FindZDOContainer(zone)) {
        for (auto &&zdo : *container) {
            if (zdo->is_distant()) {
                objects.push_back(zdo);
            }
        }
    }
}

ZDO::reference_list ZdoManager::GetZDOs(avledet::util::Hash prefab)
{
    ZDO::reference_list out;
    auto &&find = m_objectsByPrefab.find(prefab);
    if (find != m_objectsByPrefab.end()) {
        auto &&container = find->second;
        out.insert(out.end(), container.begin(), container.end());
    }
    return out;
}

ZDO::reference_list ZdoManager::GetZDOs(ZDO::Filter const &pred)
{
    ZDO::reference_list out;
    for (auto &&v : m_objectsByID) {
        auto &&zdo = ZDO::make_reference(v);
        if (!pred || pred(zdo)) {
            out.push_back(zdo);
        }
    }
    return out;
}

ZDO::reference_list ZdoManager::SomeZDOs(Vector3f const &pos, float radius, std::size_t max,
                                          ZDO::Filter const &pred)
{
    ZDO::reference_list out;

    float const sqRadius = radius * radius;

    auto minZone = ZoneManager::WorldToZonePos(Vector3f(pos.x - radius, 0, pos.z - radius));
    auto maxZone = ZoneManager::WorldToZonePos(Vector3f(pos.x + radius, 0, pos.z + radius));

    for (auto z = minZone.y; z <= maxZone.y; z++) {
        for (auto x = minZone.x; x <= maxZone.x; x++) {
            if (auto &&container = _FindZDOContainer(ZoneID(x, z))) {
                for (auto &&zdo : *container) {
                    if (zdo->get_position().sq_distance_to(pos) <= sqRadius && (!pred || pred(zdo))) {
                        if (max--)
                            out.push_back(zdo);
                        else
                            return out;
                    }
                }
            }
        }
    }

    return out;
}

ZDO::reference_list ZdoManager::SomeZDOs(ZoneID const &zone, std::size_t max, ZDO::Filter const &pred)
{
    ZDO::reference_list out;

    if (auto &&container = _FindZDOContainer(zone)) {
        for (auto &&zdo : *container) {
            if (!pred || pred(zdo)) {
                if (max--)
                    out.push_back(zdo);
                else
                    return out;
            }
        }
    }

    return out;
}

ZDO::optional ZdoManager::NearestZDO(Vector3f const &pos, float radius, ZDO::Filter const &pred)
{
    float minSqDist = radius * radius;

    ZDO::optional out;
    //float minSqDist = std::numeric_limits<float>::max();

    auto minZone = ZoneManager::WorldToZonePos(Vector3f(pos.x - radius, 0, pos.z - radius));
    auto maxZone = ZoneManager::WorldToZonePos(Vector3f(pos.x + radius, 0, pos.z + radius));

    for (auto z = minZone.y; z <= maxZone.y; z++) {
        for (auto x = minZone.x; x <= maxZone.x; x++) {
            if (auto &&container = _FindZDOContainer(ZoneID(x, z))) {
                for (auto &&zdo : *container) {
                    float sqDist = zdo->get_position().sq_distance_to(pos);
                    if (sqDist < minSqDist// Filter to closest ZDO
                        && (!pred || pred(zdo))) {
                        out       = zdo;
                        minSqDist = sqDist;
                    }
                }
            }
        }
    }

    return out;
}

// Global send
void ZdoManager::ForceSendZDO(ZDOID const &id)
{
    for (auto &&peer : NetManager::instance().GetPeers()) {
        peer->ForceSendZDO(id);
    }
}

bool ZdoManager::SendZDOs(Peer::Ptr peer, bool flush)
{
    ZoneScoped;

    auto sendQueueSize = (std::uint32_t) peer->m_socket->get_send_queue_size();

    // flushing forces a packet send
    auto const threshold = AVL_CONFIG.m_zdo_max_congestion;
    if (!flush && sendQueueSize > threshold)
        return false;

    // if very little space remaining, skip
    auto availableSpace = threshold - sendQueueSize;
    if (availableSpace < AVL_CONFIG.m_zdo_min_congestion)
        return false;

    auto syncList = CreateSyncList(peer);

    // continue only if there are updated/invalid ZDOs to send
    if (syncList.empty() && peer->m_invalidSector.empty())
        return false;

    avledet::util::Writer writer;
    {
        //avledet::util::WriterScopedEncap scoped(writer);

        writer.write(peer->m_invalidSector);

        auto const time = Avledet::instance().Time();

        for (auto &&itr = syncList.begin();
             itr != syncList.end() && writer.size() <= availableSpace /* (1) */; itr++) {

            // if size exceeded, break now
            //  HEY DUMMY! look above (1), I already did this
            //if (writer.size() > availableSpace) {
            //    break;
            //}

            // copy is intentional
            //  TODO copy is solely for LUA
            //      - I've already decided that copies are tacky as fuck
            //          the better alternative would be to use shared ptr zdos
            //          the problem is shared_ptr has a lot of overhead
            //          so use boost intrusive ptr, where bits will have to be used for refcount
            auto zdo = itr->first;

            peer->m_forceSend.erase(zdo->get_id());

            if (!AVL_SCRIPT_EVENT(ScriptManager::Events::SendingZDO, peer, zdo)) {
                continue;
            }

            writer.write(zdo->get_id());
            writer.write(zdo->get_owner_rev());
            writer.write(zdo->get_data_rev());
            writer.write(zdo->get_owner());
            writer.write(zdo->get_position());

            {
                avledet::util::WriterScopedEncap scoped1(writer);

                zdo->pack(writer, true);
            }

            peer->m_zdos[zdo->get_id()] = {zdo->m_rev, time};
        }
        writer.write(ZDOID::NONE);// null terminator
    }

    peer->Invoke(avledet::util::hashes::Rpc::ZDOData, writer.release());

    if (!peer->m_invalidSector.empty() || !syncList.empty()) {
        peer->m_invalidSector.clear();

        return true;
    }

    return false;
}

void ZdoManager::OnNewPeer(Peer::Ptr peer)
{
    peer->Register("ZDOData", [this](Peer::Ptr peer, DataReader reader) {
        ZoneScoped;

        // Only allow if normal mode
        if (peer->IsGated())
            return;

        // TODO
        //  - I think sector invalidate is an overlooked remnant
        //      sector invalidation is easy for the server to track between states
        //      without the need for explicit notification from the client
        //  - The server can simply compare the last ZDO sector with the newly received sector
        reader.read([this](ZDOID zdoid) {
            if (auto zdo = find_zdo(zdoid)) {
                _InvalidateZDOZone(zdo);
            }
        });

        auto time = Avledet::instance().Time();

        while (auto zdoid = reader.read<ZDOID>()) {
            auto owner_rev = reader.read<std::uint16_t>();          // owner revision
            auto data_rev  = reader.read<std::uint32_t>();          // data revision
            auto owner     = reader.read<std::int64_t>();           // owner
            auto pos       = reader.read<Vector3f>();               // position

            auto des = DataReader(reader.read<std::vector<char>>());// dont move this

            /*
			ZDO::Rev rev = { 
				.m_dataRev = dataRev, 
				.m_ownerRev = ownerRev, 
				.m_syncTime = time 
			};*/

            auto &&[zdo_itr, created] = this->_Instantiate(zdoid);
            auto &&zdo                = ZDO::make_reference(zdo_itr);

            assert(zdoid == zdo->get_id());

            if (!created) {
                // If the incoming data revision is at most older or equal to this revision, we do NOT need to deserialize
                //	(because the data will be the same, or at the worst case, it will be outdated)
                if (data_rev <= zdo->get_data_rev()) {

                    // If the owner has changed, keep a copy
                    if (owner_rev > zdo->get_owner_rev()) {
                        zdo->_set_owner(owner);
                        zdo->m_rev.set_owner_rev(owner_rev);
                        peer->m_zdos[zdoid] = {ZDO::Rev(data_rev, owner_rev), time};
                    }
                    continue;
                }
            } else {
                assert((_FindZDOContainer(zdo->get_zone()))
                               ? !_FindZDOContainer(zdo->get_zone())->contains(zdoid)
                               : true);

                if (m_erasedZDOs.contains(zdoid)) {
                    m_destroySendList.push_back(zdoid);

                    m_objectsByID.erase(zdo_itr);
                    continue;
                }
            }

            // Also used as restore point if this ZDO breaks during deserialization
            //ZDO copy(zdo);

            //try {
            zdo->_set_owner(owner);
            zdo->m_rev.set_data_rev(data_rev);
            zdo->m_rev.set_owner_rev(owner_rev);

            // Unpack the ZDOs primary data. A new ZDO that fails (unknown prefab, short data) is dropped:
            // left in the map without a prefab it crashed the next save or disconnect
            try {
                zdo->unpack(des, 0);
            } catch (std::exception const &e) {
                LOG_WARNING(AVL_LOGGER, "Bad ZDO {} from {}: {}", zdoid, peer->m_name, e.what());
                if (created)
                    m_objectsByID.erase(zdo_itr);
                continue;
            }

            AVL_SCRIPT_EVENT(ScriptManager::Events::ZDOUnpacked, peer, zdo);

            // Only disperse through world if ZDO is new
            if (created) {
                //if (!AVL_SCRIPT_EVENT(ScriptManager::Events::ZDOCreated, peer, zdo)) {
                //	_EraseZDO(pair.first);
                //	continue;
                //}

                // do NOT comment this out and do NOT 'zdo->set_position()'
                zdo->_set_position(pos);//unrevised because is fresh zdo
                _AddZDOToZone(zdo);

                m_objectsByPrefab[zdo->get_prefab_hash()].insert(zdo);
            } else {
                //if (!AVL_SCRIPT_EVENT(ScriptManager::Events::ZDOModified, peer, zdo, copy, pos)) {
                //	zdo = std::move(copy);
                //	continue;
                //}

                // do NOT move this after
                //  see https://github.com/ricosolana/Valhalla/blob/e9520909bb61d559f3815d1454ca8327760f6586/library/src/ZDOManager.cpp#L994
                zdo->set_position(pos);
            }

            assert(_FindZDOContainer(zdo->get_zone()));

            assert(_FindZDOContainer(zdo->get_zone())->contains(zdo));

            peer->m_zdos[zdoid] = {zdo->m_rev, time};
            /*
			}
			catch (const std::runtime_error& e) {
				// erase the zdo from map
				if (created) // if the zdo was just created, throw it away
					EraseZDO(pair.first);
				else zdo = copy; // else, restore the ZDO to the prior revision

				// This will kick the malicious peer
				std::rethrow_exception(std::make_exception_ptr(e));
			}*/
        }
    });
}

void ZdoManager::OnPeerQuit(Peer::Ptr peer)
{
    for (auto &&itr = m_objectsByID.begin(); itr != m_objectsByID.end();) {
        auto &&zdo = ZDO::make_reference(itr);

        if (!zdo->is_persistent()
            && (!zdo->has_owner() || zdo->is_owner(peer->GetUserID())
                || (!zdo->is_owner(AVL_ID) && !NetManager::instance().FindPeerByUserID(zdo->get_owner())))) {
            itr = _DestroyZDO(itr);
        } else
            ++itr;
    }
}

std::size_t ZdoManager::GetSumZDOMembers()
{
    std::size_t res = 0;
    for (auto &&zdo : m_objectsByID) {
        //res += zdo.second->m_members.size();
    }
    return res;
}

float ZdoManager::GetMeanZDOMembers()
{
    return (float) GetSumZDOMembers() / m_objectsByID.size();
}

float ZdoManager::GetStDevZDOMembers()
{
    float const mean = GetMeanZDOMembers();
    float const n    = m_objectsByID.size();

    float res = 0;
    for (auto &&zdo : m_objectsByID) {
        //res += std::pow((float)zdo.second->m_members.size() - mean, 2.f);
    }

    return std::sqrt(res / n);
}

std::size_t ZdoManager::GetTotalZDOAlloc()
{
    //std::size_t bytes = m_objectsByID.size() * sizeof(ZDO);
    //for (auto&& zdo : m_objectsByID) bytes += zdo->get_memory();
    //return bytes;
    return ZDO::get_memory(false);
}

std::size_t ZdoManager::GetCountEmptyZDOs()
{
    // so gather each ZDO member, and write how many of them are empty
    std::size_t count = 0;
    for (auto &&zdo : m_objectsByID) {
        auto alloc = zdo->get_memory(false);
        if (alloc == 0)
            count++;
    }
    return count;
}
