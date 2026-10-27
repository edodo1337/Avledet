#pragma once

#include <functional>
#include <vector>

#include <quill/Logger.h>
#include <range/v3/all.hpp>

#include "Manager.h"
#include "Peer.h"
#include "PrefabManager.h"
#include "Types.h"
#include "Vector.h"
#include "VUtils.h"
#include "VUtilsRandom.h"
#include "ZDO.h"
#include "ZoneManager.h"

class ZdoManager : public avledet::util::IManager<ZdoManager>
{
    // vanilla SendZDOToPeers2 state, stepped in frames of the vanilla server
    std::chrono::steady_clock::time_point m_sendClock = std::chrono::steady_clock::now();
    double m_sendFrames = 0;
    float m_sendTimer   = 0;
    int m_nextSendPeer  = -1;

    friend class NetManager;
    friend class Avledet;
    friend class ZDO;

    // Predicate for whether a zdo is a prefab with or without given flags
    //	prefabHash: if 0, then prefabHash check is skipped
    static bool PREFAB_CHECK_FUNCTION(ZDO::reference zdo, avledet::util::Hash prefabHash,
                                      Prefab::Flag flagsPresent, Prefab::Flag flagsAbsent)
    {
        auto &&prefab = zdo->get_prefab();

        return prefab.get().AllFlagsAbsent(flagsAbsent) && (prefabHash == 0 || prefab.get().m_hash == prefabHash)
               && prefab.get().AllFlagsPresent(flagsPresent);
    }

    // these are just here for easy access
    //using ZDOContainer = ZDO::set;
    //using ZDO_map = ZDO::map;
    //using ZDO_iterator = ZDO::ZDO_iterator;

  private:
    // Contains ZDOs according to Zone
    //	takes up around 5MB; could be around 72 bytes (initial) with map
    std::array<ZDO::reference_set,
               (ZoneManager::WORLD_INNER_ZDIAMETER * ZoneManager::WORLD_INNER_ZDIAMETER)>
            m_objectsBySector;
    avledet::util::Map<ZoneID, ZDO::reference_set> m_objectsBySectorOuter;

    // Contains ZDOs according to prefab
    //	TODO is this necessary?
    avledet::util::Map<avledet::util::Hash, ZDO::reference_set> m_objectsByPrefab;

    // Responsible for managing ZDOs lifetimes
    //	A segmented map is used instead of a vector map
    ZDO::smart_set m_objectsByID;

    // Container of retired ZDOs
    //	TODO benchmark storage here vs keeping ZDOIDs in m_objectsByID map (but setting values to null to diffreenciate between alive/dead)
    ZDO::soft_set m_erasedZDOs;

    // Contains recently destroyed ZDOs to be sent
    ZDO::soft_list m_destroySendList;

    //std::vector<ZDO> m_zdoInsertQueue; // TODO create all zdos before sendZdos in Update to sync
    //std::vector<ZDO> m_zdoEraseQueue; // TODO use zdoid

    // Increments over the course of the game as ZDOs are created
    std::uint32_t m_nextUid {};

  private:
    // Called when an authenticated peer joins (internal)
    void OnNewPeer(Peer::Ptr peer);
    // Called when an authenticated peer leaves (internal)
    void OnPeerQuit(Peer::Ptr peer);

    // Retrieve a zone container for storing zdos
    [[nodiscard]] std::reference_wrapper<ZDO::reference_set> _GetZDOContainer(ZoneID zone)
    {
        int num = SectorToIndex(zone);
        if (num != -1) {
            return m_objectsBySector[num];
        }
        return m_objectsBySectorOuter[zone];
    }

    [[nodiscard]] ZDO::reference_set *_FindZDOContainer(ZoneID zone)
    {
        int num = SectorToIndex(zone);
        if (num != -1) {
            return &m_objectsBySector[num];
        }
        // like vanilla FindObjects (TryGetValue): a lookup does not add an empty zone; it did for every
        // zone around a peer far out, a map a spoofed position grew without end
        auto &&find = m_objectsBySectorOuter.find(zone);
        return find != m_objectsBySectorOuter.end() ? &find->second : nullptr;
    }

    // Insert a ZDO into zone (internal)
    void _AddZDOToZone(ZDO::reference zdo);
    // Remove a zdo from a zone (internal)
    void _RemoveFromSector(ZDO::reference zdo);
    // Relay a ZDO zone change to clients (internal)
    void _InvalidateZDOZone(ZDO::reference zdo);

    void AssignOrReleaseZDOs(Peer::Ptr peer);
    //void SmartAssignZDOs();

    // Frees a ZDO from memory by a valid iterator
    [[maybe_unused]] ZDO::smart_set::iterator _EraseZDO(ZDO::smart_set::iterator itr);

    void EraseZDO(ZDOID zdoid)
    {
        //m_zdoEraseQueue.push_back()

        auto &&find = m_objectsByID.find(zdoid);
        if (find != m_objectsByID.end())
            _EraseZDO(find);
    }

    // Destroys a ZDO globally
    // The ZDO is freed from memory
    // Returns an iterator to the next ZDO
    [[maybe_unused]] ZDO::smart_set::iterator _DestroyZDO(ZDO::smart_set::iterator itr)
    {
        m_destroySendList.push_back((*itr)->get_id());
        return _EraseZDO(itr);
    }

    void SendAllZDOs(Peer::Ptr peer)
    {
        while (SendZDOs(peer, true));
    }

    [[maybe_unused]] bool SendZDOs(Peer::Ptr peer, bool flush);
    [[nodiscard]] std::list<std::pair<ZDO::reference, float>> CreateSyncList(Peer::Ptr peer);


    // Instantiate a ZDO by id if it does not exist
    // Returns the ZDO or the previously mapped ZDO
    [[nodiscard]] std::pair<ZDO::smart_set::iterator, bool> _Instantiate(ZDOID zdoid) noexcept;

    // Instantiate a ZDO by id if it does not exist
    // Returns the ZDO or the previously mapped ZDO
    [[nodiscard]] std::pair<ZDO::smart_set::iterator, bool> _Instantiate(ZDOID zdoid,
                                                                         Vector3f position) noexcept;

    // Instantiate a ZDO with the next available ID
    [[nodiscard]] ZDO::reference _Instantiate(Vector3f position) noexcept;

    // Instantiate a ZDO with the specified id
    // Throws if the ZDO exists
    //[[nodiscard]] ZDO::unsafe_value _TryInstantiateBounded(ZDOID uid, Vector3f position);

    //[[nodiscard]] ZDO::unsafe_value _TryInstantiate(ZDOID zdoid) {
    //	auto&& insert = _Instantiate(zdoid);
    //	// if inserted, then set pos
    //	if (!insert.second) {
    //		throw std::runtime_error("zdo already exists");
    //	}
    //	return ZDO::make_unsafe_value(insert.first);
    //}


    // Get a ZDO by id
    // UBF/crash if the ZDO is not found
    //[[nodiscard]] ZDO::unsafe_value _GetZDO(ZDOID id) noexcept {
    //	auto&& find = m_objectsByID.find(id);
    //	if (find != m_objectsByID.end())
    //		return ZDO::ref(find);
    //		//return ZDO(*find);
    //	std::unreachable();
    //}

    // Get a ZDO by id
    // The ZDO will be created if its ID does not exist
    // Returns the ZDO and a bool if newly created
    //[[nodiscard]] std::pair<ZDO_iterator, bool> _GetOrInstantiate(ZDOID id, Vector3f def);

    // Returns
    // saved connection hashes back to ZDO links, after a load
    void _ConnectAll();

    // portals being linked by their owners (Game.m_currentlyConnectingPortals)
    std::vector<std::pair<ZDOID, ZDOID>> m_connectingPortals;
    VUtils::Random::State m_portalRandom;
    void _SetPortalConnection(ZDO::reference portal, ZDOID connection, bool force);
    // Game.ConnectPortals
    void ConnectPortals();

    ZDO::optional _find_zdo(ZDOID const &v)
    {
        auto &&find = m_objectsByID.find(v);
        if (find != m_objectsByID.end())
            return ZDO::make_optional(find);
        return ZDO::nullopt;
    }

    [[nodiscard]] int SectorToIndex(ZoneID zone) const
    {
        int x = zone.x + ZoneManager::WORLD_INNER_ZRADIUS;
        int y = zone.y + ZoneManager::WORLD_INNER_ZRADIUS;
        if (x < 0 || y < 0 || x >= ZoneManager::WORLD_INNER_ZDIAMETER
            || y >= ZoneManager::WORLD_INNER_ZDIAMETER) {
            return -1;
        }

        assert(x >= 0 && y >= 0 && x < ZoneManager::WORLD_INNER_ZDIAMETER
               && y < ZoneManager::WORLD_INNER_ZDIAMETER && "sector exceeds world radius");

        return y * ZoneManager::WORLD_INNER_ZDIAMETER + x;
    }

  public:
    void Init();
    void Update();

    std::size_t GetZDOCount() const { return m_objectsByID.size(); }

    // Vanilla 1.0 chunked save: persistent ZDOs in YY_XX__0_<save>.chunk files (8x8 zones each,
    // portals in their own file), the index _main.<save>.chunks last. Serialized only, no file io.
    std::vector<std::pair<std::string, avledet::util::Bytes>> SaveChunks(std::uint32_t save);

    void LoadChunks(std::filesystem::path const &dir, std::uint32_t save);

    [[maybe_unused]] ZDO::reference Instantiate(Prefab::Reference prefab, Vector3f pos);

    //[[maybe_unused]] ZDO::unsafe_value InstantiateBounded(avledet::util::Hash hash, Vector3f pos, const Prefab** outPrefab);

    // vanilla ZNetView.Awake: with m_syncInitialScale a localScale other than one goes to the ZDO
    ZDO::reference Instantiate(avledet::util::Hash hash, Vector3f pos, Vector3f localScale);

    [[maybe_unused]] ZDO::reference Instantiate(avledet::util::Hash hash, Vector3f pos)
    {
        //return InstantiateBounded(hash, pos, nullptr);

        return Instantiate(PrefabManager::instance().get_prefab(hash), pos);
    }

    // TODO either correctly implement or?
    //	intended to instantiate an object based on another
    //[[maybe_unused]] ZDO Instantiate(const ZDO& zdo);

    auto GetZDOs()
    {
        return ranges::views::all(m_objectsByID);
    }

    // Get a ZDO by id
    //	TODO use optional<reference>
    [[nodiscard]] ZDO::optional find_zdo(ZDOID id);

    // Get all ZDOs strictly within a zone
    void GetZDOs_Zone(ZoneID zone, ZDO::reference_list &out);
    // Get all ZDOs strictly within neighboring zones
    void GetZDOs_NeighborZones(ZoneID zone, ZDO::reference_list &out);
    // Get the ZDOs a peer with this simulation distance syncs: near zones, distant flagged ones further out
    void GetZDOs_ActiveZones(ZoneID zone, SimulationDistance const &sd, ZDO::reference_list &out,
                             ZDO::reference_list &outDistant);
    // Get all ZDOs strictly within a zone that are distant flagged
    void GetZDOs_Distant(ZoneID sector, ZDO::reference_list &objects);


    // Get a capped number of ZDOs within a radius matching an optional predicate
    [[nodiscard]] ZDO::reference_list SomeZDOs(Vector3f const &pos, float radius, std::size_t max,
                                               ZDO::Filter const &pred);

    // Get a capped number of ZDOs within a radius
    [[nodiscard]] ZDO::reference_list SomeZDOs(Vector3f const &pos, float radius, std::size_t max)
    {
        return SomeZDOs(pos, radius, max, nullptr);
    }

    // Get a capped number of ZDOs with prefab and/or flag
    //	*Note: Prefab or Flag must be non-zero for anything to be returned
    [[nodiscard]] ZDO::reference_list SomeZDOs(Vector3f const &pos, float radius, std::size_t max,
                                               avledet::util::Hash prefab, Prefab::Flag flagsPresent,
                                               Prefab::Flag flagsAbsent)
    {
        return SomeZDOs(pos, radius, max, [&](ZDO::reference zdo) {
            return PREFAB_CHECK_FUNCTION(zdo, prefab, flagsPresent, flagsAbsent);
        });
    }

    // Get a capped number of ZDOs within a zone matching an optional predicate
    [[nodiscard]] ZDO::reference_list SomeZDOs(ZoneID const &zone, std::size_t max, ZDO::Filter const &pred);

    // Get a capped number of ZDOs within a zone
    [[nodiscard]] ZDO::reference_list SomeZDOs(ZoneID const &zone, std::size_t max)
    {
        return SomeZDOs(zone, max, nullptr);
    }

    // Get a capped number of ZDOs within a radius in zone with prefab and/or flag
    [[nodiscard]] ZDO::reference_list SomeZDOs(ZoneID const &zone, std::size_t max, Vector3f const &pos,
                                               float radius, avledet::util::Hash prefab,
                                               Prefab::Flag flagsPresent, Prefab::Flag flagsAbsent)
    {
        float sqRadius = radius * radius;
        return SomeZDOs(zone, max, [&](ZDO::reference zdo) {
            return zdo->get_position().sq_distance_to(pos) <= sqRadius
                   && PREFAB_CHECK_FUNCTION(zdo, prefab, flagsPresent, flagsAbsent);
        });
    }

    // Get a capped number of ZDOs within a zone with prefab and/or flag
    [[nodiscard]] ZDO::reference_list SomeZDOs(ZoneID const &zone, std::size_t max,
                                               avledet::util::Hash prefab, Prefab::Flag flagsPresent,
                                               Prefab::Flag flagsAbsent)
    {
        return SomeZDOs(zone, max, Vector3f::ZERO, std::numeric_limits<float>::max(), prefab, flagsPresent,
                        flagsAbsent);
    }

    // Get a capped number of ZDOs within a zone with prefab and/or flag
    [[nodiscard]] ZDO::reference_list SomeZDOs(ZoneID const &zone, std::size_t max, Vector3f const &pos,
                                               float radius)
    {
        return SomeZDOs(zone, max, pos, radius, 0, Prefab::Flag::NONE, Prefab::Flag::NONE);
    }

    // Get all ZDOs with prefab
    //	This method is optimized assuming AVL_STANDARD_PREFABS is on
    [[nodiscard]] ZDO::reference_list GetZDOs(avledet::util::Hash prefab);

    // Get all ZDOs fulfilling a given predicate
    //	Try to avoid using this method too frequently (it iterates all ZDOs in the world, which is *very* slow)
    [[nodiscard]] ZDO::reference_list GetZDOs(ZDO::Filter const &pred);

    // Get all ZDOs matching the given prefab flags
    //	Try to avoid using this method too frequently (it iterates all ZDOs in the world, which is *very* slow)
    [[nodiscard]] ZDO::reference_list GetZDOs(Prefab::Flag flagsPresent, Prefab::Flag flagsAbsent)
    {
        return GetZDOs(
                [&](ZDO::reference zdo) { return PREFAB_CHECK_FUNCTION(zdo, 0, flagsPresent, flagsAbsent); });
    }

    // Get all ZDOs within a radius matching an optional predicate
    [[nodiscard]] ZDO::reference_list GetZDOs(Vector3f const &pos, float radius, ZDO::Filter const &pred)
    {
        return SomeZDOs(pos, radius, -1, pred);
    }

    // Get all ZDOs within a radius
    [[nodiscard]] ZDO::reference_list GetZDOs(Vector3f const &pos, float radius)
    {
        return SomeZDOs(pos, radius, -1, nullptr);
    }

    // Get all ZDOs within a radius with prefab and/or flag
    [[nodiscard]] ZDO::reference_list GetZDOs(Vector3f const &pos, float radius, avledet::util::Hash prefab,
                                              Prefab::Flag flagsPresent, Prefab::Flag flagsAbsent)
    {
        return SomeZDOs(pos, radius, -1, prefab, flagsPresent, flagsAbsent);
    }

    // Get all ZDOs within a zone matching an optional predicate
    [[nodiscard]] ZDO::reference_list GetZDOs(ZoneID const &zone, ZDO::Filter const &pred)
    {
        return SomeZDOs(zone, -1, pred);
    }

    // Get all ZDOs within a zone
    [[nodiscard]] ZDO::reference_list GetZDOs(ZoneID const &zone)
    {
        return SomeZDOs(zone, -1, nullptr);
    }

    // Get all ZDOs within a zone of prefab and/or flag
    [[nodiscard]] ZDO::reference_list GetZDOs(ZoneID const &zone, avledet::util::Hash prefab,
                                              Prefab::Flag flagsPresent, Prefab::Flag flagsAbsent)
    {
        return SomeZDOs(zone, -1, [&](ZDO::reference zdo) {
            return PREFAB_CHECK_FUNCTION(zdo, prefab, flagsPresent, flagsAbsent);
        });
    }

    // Get all ZDOs within a radius in zone
    [[nodiscard]] ZDO::reference_list GetZDOs(ZoneID const &zone, Vector3f const &pos, float radius,
                                              avledet::util::Hash prefab, Prefab::Flag flagsPresent,
                                              Prefab::Flag flagsAbsent)
    {
        auto const sqRadius = radius * radius;
        return SomeZDOs(zone, -1, [&](ZDO::reference zdo) {
            return zdo->get_position().sq_distance_to(pos) <= sqRadius
                   && PREFAB_CHECK_FUNCTION(zdo, prefab, flagsPresent, flagsAbsent);
        });
    }

    // Get all ZDOs within a radius in zone
    [[nodiscard]] ZDO::reference_list GetZDOs(ZoneID const &zone, Vector3f const &pos, float radius)
    {
        return GetZDOs(zone, pos, radius, 0, Prefab::Flag::NONE, Prefab::Flag::NONE);
    }

    // Get any ZDO within a radius with prefab and/or flag
    [[nodiscard]] ZDO::optional AnyZDO(Vector3f const &pos, float radius, avledet::util::Hash prefabHash,
                                       Prefab::Flag flagsPresent, Prefab::Flag flagsAbsent)
    {
        auto &&zdos = SomeZDOs(pos, radius, 1, prefabHash, flagsPresent, flagsAbsent);
        if (zdos.empty())
            return ZDO::nullopt;
        return zdos.front();
    }

    // Get any ZDO within a zone with prefab and/or flag
    [[nodiscard]] ZDO::optional AnyZDO(ZoneID const &zone, avledet::util::Hash prefabHash,
                                       Prefab::Flag flagsPresent, Prefab::Flag flagsAbsent)
    {
        auto &&zdos = SomeZDOs(zone, 1, prefabHash, flagsPresent, flagsAbsent);
        if (zdos.empty())
            return ZDO::nullopt;
        return zdos.front();
    }

    // Get the nearest ZDO within a radius matching an optional predicate
    // TODO this is not best-optimized
    [[nodiscard]] ZDO::optional NearestZDO(Vector3f const &pos, float radius, ZDO::Filter const &pred);

    // Get the nearest ZDO within a radius with prefab and/or flag
    // TODO this is not best-optimized
    [[nodiscard]] ZDO::optional NearestZDO(Vector3f const &pos, float radius, avledet::util::Hash prefabHash,
                                           Prefab::Flag flagsPresent, Prefab::Flag flagsAbsent)
    {
        return NearestZDO(pos, radius, [&](ZDO::reference zdo) {
            return PREFAB_CHECK_FUNCTION(zdo, prefabHash, flagsPresent, flagsAbsent);
        });
    }

    void ForceSendZDO(ZDOID const &id);

    // Erases a ZDO on clients and server
    //	Warning: the target ZDO is freed from memory and will become no longer accessible
    void DestroyZDO(ZDOID const &zdoid)
    {
        m_destroySendList.push_back(zdoid);
        EraseZDO(zdoid);
    }

    void DestroyZDO(ZDO::reference zdo)
    {
        DestroyZDO(zdo->get_id());
    }

    [[nodiscard]] std::size_t GetSumZDOMembers();
    [[nodiscard]] float GetMeanZDOMembers();
    [[nodiscard]] float GetStDevZDOMembers();
    [[nodiscard]] std::size_t GetTotalZDOAlloc();

    [[nodiscard]] std::size_t GetCountEmptyZDOs();
};
