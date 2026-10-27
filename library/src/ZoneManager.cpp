#include <array>
#include "RandomSpawn.h"
#include "Types.h"
#include "Vector.h"
#include "VUtils.h"

#include "DungeonGenerator.h"
#include "DungeonManager.h"
#include "GeoManager.h"
#include "Hashes.h"
#include "HeightMap.h"
#include "HeightmapManager.h"
#include "NetManager.h"
#include "PrefabManager.h"
#include "RouteManager.h"
#include "VUtilsResource.h"
#include "ZDOManager.h"
#include <algorithm>
#include <numeric>
#include <vector>
#include "ZoneManager.h"
#include "AltBiomes.h"
#include <fstream>
#include <optional>
#include <nlohmann/json.hpp>

// private
void ZoneManager::PostPrefabInit()
{
    LOG_NOTICE(AVL_LOGGER, "Initializing ZoneManager");

    {
#if AVL_IS_ON(AVL_ZONE_GENERATION)
        // load ZoneLocations:
        auto opt = VUtils::Resource::ReadFile<avledet::util::Bytes>("features.pkg");
        if (!opt)
            throw std::runtime_error("features.pkg missing");

        DataReader pkg(std::move(*opt));

        auto comment = pkg.read<std::string_view>();// comment
        LOG_DEBUG(AVL_LOGGER, "pkg comment: {}", comment);

        auto ver = pkg.read<std::string_view>();
        if (ver != VConstants::GAME) {
            LOG_WARNING(AVL_LOGGER, "features.pkg uses different game version than server ({})", ver);
        }

        auto count = pkg.read<std::int32_t>();
        for (int i = 0; i < count; i++) {
            // TODO read zoneLocations from file
            auto loc = std::make_unique<Feature>();

            loc->m_name                         = pkg.read<std::string>();
            loc->m_hash                         = avledet::util::get_stable_hash(loc->m_name);
            loc->m_biome                        = (avledet::util::Biome) pkg.read<std::int32_t>();
            loc->m_biomeArea                    = (avledet::util::BiomeArea) pkg.read<std::int32_t>();
            loc->m_applyRandomDamage            = pkg.read<bool>();
            loc->m_centerFirst                  = pkg.read<bool>();
            loc->m_clearArea                    = pkg.read<bool>();
            loc->m_exteriorRadius               = pkg.read<float>();
            loc->m_interiorRadius               = pkg.read<float>();
            loc->m_forestTresholdMin            = pkg.read<float>();
            loc->m_forestTresholdMax            = pkg.read<float>();
            loc->m_minDistanceFromCenter        = pkg.read<float>();                            // TODO ash
            loc->m_maxDistanceFromCenter        = pkg.read<float>();                            // TODO ash
            loc->m_group                        = pkg.read<std::string>();
            loc->m_iconAlways                   = pkg.read<bool>();
            loc->m_iconPlaced                   = pkg.read<bool>();
            loc->m_inForest                     = pkg.read<bool>();
            loc->m_minAltitude                  = pkg.read<float>();
            loc->m_maxAltitude                  = pkg.read<float>();
            loc->m_minDistance                  = pkg.read<float>();
            loc->m_maxDistance                  = pkg.read<float>();
            loc->m_minTerrainDelta              = pkg.read<float>();
            loc->m_maxTerrainDelta              = pkg.read<float>();
            loc->m_minimumVegetation            = pkg.read<float>();                            // TODO ash
            loc->m_maximumVegetation            = pkg.read<float>();                            // TODO ash
            loc->m_surroundCheckVegetation      = pkg.read<bool>();                             // TODO ash
            loc->m_surroundCheckDistance        = pkg.read<float>();                            // TODO ash
            loc->m_surroundCheckLayers          = pkg.read<std::int32_t>();                     // TODO ash
            loc->m_surroundBetterThanAverage    = pkg.read<float>();                            // TODO ash
            loc->m_minDistanceFromSimilar       = pkg.read<float>();
            loc->m_groupMax                     = pkg.read<std::string>();                      // TODO ash
            loc->m_maxDistanceFromSimilar       = pkg.read<float>();                            // TODO ash
            loc->m_spawnAttempts                = pkg.read<std::int32_t>();
            loc->m_quantity                     = pkg.read<std::int32_t>();
            loc->m_randomRotation               = pkg.read<bool>();
            loc->m_slopeRotation                = pkg.read<bool>();
            loc->m_snapToWater                  = pkg.read<bool>();
            loc->m_unique                       = pkg.read<bool>();

            // Netview game objects
            auto views = pkg.read<std::int32_t>();
            for (int j = 0; j < views; j++) {
                Prefab::Instance piece;

                auto dbgName = pkg.read<std::string_view>();//For debugging
                (void) dbgName;

                piece.m_prefabHash = pkg.read<avledet::util::Hash>();
                assert(avledet::util::get_stable_hash(dbgName)
                       == piece.m_prefabHash);//TODO dont use debug assert
                piece.m_pos = pkg.read<Vector3f>();
                piece.m_rot = pkg.read<Quaternion>();
                piece.m_scale = pkg.read<Vector3f>();

                piece.get_prefab();

                loc->m_pieces.push_back(piece);
            }

            loc->m_randoms = avledet::gen::Randoms::parse(pkg);

            //m_featuresByHash.insert({ loc->m_hash, *loc.get() });
            m_features.push_back(std::move(loc));
        }

        if (pkg.get_pos() != pkg.size())
            throw std::runtime_error("pkg has unread bytes, format mismatch");

        LOG_NOTICE(AVL_LOGGER, "Loaded {} features", count);
#endif
    }

    {
#if AVL_IS_ON(AVL_ZONE_GENERATION)
        // load Foliage:
        auto opt = VUtils::Resource::ReadFile<avledet::util::Bytes>("vegetation.pkg");
        if (!opt)
            throw std::runtime_error("vegetation.pkg missing");

        DataReader pkg(std::move(*opt));

        auto comment = pkg.read<std::string_view>();// comment
        LOG_DEBUG(AVL_LOGGER, "pkg comment: {}", comment);

        auto ver = pkg.read<std::string_view>();
        if (ver != VConstants::GAME) {
            LOG_WARNING(AVL_LOGGER, "vegetation.pkg uses different game version than server ({})", ver);
        }

        auto count = pkg.read<std::int32_t>();
        for (int i = 0; i < count; i++) {
            auto prefabName = pkg.read<std::string>();

            auto veg = std::make_unique<Foliage>(PrefabManager::instance().get_prefab(prefabName));

            //veg->m_prefab = PrefabManager::instance().get_prefab(prefabName);

            veg->m_biome                        = (avledet::util::Biome) pkg.read<std::int32_t>();
            veg->m_biomeArea                    = (avledet::util::BiomeArea) pkg.read<std::int32_t>();
            veg->m_radius                       = pkg.read<float>();
            veg->m_min                          = pkg.read<float>();
            veg->m_max                          = pkg.read<float>();
            veg->m_minTilt                      = pkg.read<float>();
            veg->m_maxTilt                      = pkg.read<float>();
            veg->m_groupRadius                  = pkg.read<float>();
            veg->m_forcePlacement               = pkg.read<bool>();
            veg->m_groupSizeMin                 = pkg.read<std::int32_t>();
            veg->m_groupSizeMax                 = pkg.read<std::int32_t>();
            veg->m_scaleMin                     = pkg.read<float>();
            veg->m_scaleMax                     = pkg.read<float>();
            veg->m_randTilt                     = pkg.read<float>();
            veg->m_blockCheck                   = pkg.read<bool>();
            veg->m_minAltitude                  = pkg.read<float>();
            veg->m_maxAltitude                  = pkg.read<float>();
            veg->m_minOceanDepth                = pkg.read<float>();
            veg->m_maxOceanDepth                = pkg.read<float>();
            veg->m_terrainDeltaRadius           = pkg.read<float>();
            veg->m_minTerrainDelta              = pkg.read<float>();
            veg->m_maxTerrainDelta              = pkg.read<float>();
            veg->m_inForest                     = pkg.read<bool>();
            veg->m_forestTresholdMin            = pkg.read<float>();
            veg->m_forestTresholdMax            = pkg.read<float>();
            veg->m_snapToWater                  = pkg.read<bool>();
            veg->m_snapToStaticSolid            = pkg.read<bool>();
            veg->m_groundOffset                 = pkg.read<float>();
            veg->m_chanceToUseGroundTilt        = pkg.read<float>();
            veg->m_minVegetation                = pkg.read<float>();
            veg->m_maxVegetation                = pkg.read<float>();
            veg->m_surroundCheckVegetation      = pkg.read<bool>();             // TODO
            veg->m_surroundCheckDistance        = pkg.read<float>();            // TODO
            veg->m_surroundCheckLayers          = pkg.read<std::int32_t>();     // TODO
            veg->m_surroundBetterThanAverage    = pkg.read<float>();            // TODO
            veg->m_minDistanceFromCenter        = pkg.read<float>();
            veg->m_maxDistanceFromCenter        = pkg.read<float>();
            veg->m_name                         = pkg.read<std::string>();
            veg->m_altBiomeParent               = pkg.read<std::string>();

            m_foliage.push_back(std::move(veg));
        }

        if (pkg.get_pos() != pkg.size())
            throw std::runtime_error("pkg has unread bytes, format mismatch");

        LOG_NOTICE(AVL_LOGGER, "Loaded {} vegetation", count);

        std::ifstream awakeIn("awake.json");
        if (!awakeIn)
            throw std::runtime_error("awake.json missing (dumped from the 1.0 server)");
        auto awake = nlohmann::json::parse(awakeIn);
        for (auto &&[name, j] : awake.at("prefabs").items()) {
            auto &&draws = j.at("draws");
            m_awakeDraws[avledet::util::get_stable_hash(name)]
                    = AwakeDraws {draws[0], draws[3], j.at("damageIndex"), j.at("health")};
        }

        std::ifstream modsIn("prefab_terrainmods.json");
        if (!modsIn)
            throw std::runtime_error("prefab_terrainmods.json missing (dumped from the 1.0 server)");
        auto mods = nlohmann::json::parse(modsIn);
        for (auto &&[name, list] : mods.items()) {
            auto &&out = m_prefabMods[avledet::util::get_stable_hash(name)];
            for (auto &&j : list) {
                TerrainMod m;
                m.m_sortOrder          = j.at("sortOrder");
                m.m_playerModification = j.at("player");
                m.m_levelOffset        = j.at("levelOffset");
                m.m_level              = j.at("level");
                m.m_levelRadius        = j.at("levelRadius");
                m.m_square             = j.at("square");
                m.m_smooth             = j.at("smooth");
                m.m_smoothRadius       = j.at("smoothRadius");
                m.m_smoothPower        = j.at("smoothPower");
                m.m_paintCleared       = j.at("paint");
                m.m_paintType          = (TerrainModifier::PaintType) j.at("paintType").get<int>();
                m.m_paintRadius        = j.at("paintRadius");
                m.m_paintHeightCheck   = j.at("paintHeightCheck");
                m.m_paintStrength      = j.at("paintStrength");
                m.m_pos = Vector3f(j.at("pos")[0].get<float>(), j.at("pos")[1].get<float>(), j.at("pos")[2].get<float>());
                out.push_back(m);
            }
        }
#endif
    }

#if AVL_IS_ON(AVL_ZONE_GENERATION)
    ZONE_CTRL_PREFAB      = PrefabManager::instance().find_prefab(avledet::util::hashes::Object::_ZoneCtrl);
    LOCATION_PROXY_PREFAB = PrefabManager::instance().find_prefab(avledet::util::hashes::Object::LocationProxy);

    if (!ZONE_CTRL_PREFAB || !LOCATION_PROXY_PREFAB)
        throw std::runtime_error("zone and location prefabs missing");
#endif

    RouteManager::instance().Register("SetGlobalKey",
                             [this](Peer::Ptr peer, std::string_view name) {
                                 (void) peer;
                                 // TODO limit keys based on peer and the creature killed
                                 //  have this as a compiler macro
                                 if (m_globalKeys.insert(name).second)
                                     SendGlobalKeys();// Notify clients
                             });

    RouteManager::instance().Register("RemoveGlobalKey",
                             [this](Peer::Ptr peer, std::string_view name) {
                                 (void) peer;
                                 // TODO limit keys based on peer and the creature killed
                                 if (m_globalKeys.erase(name))
                                     SendGlobalKeys();// Notify clients
                             });

    RouteManager::instance().Register("RPC_DiscoverClosestLocation",
                             [this](Peer::Ptr peer, std::string_view locationName, Vector3f point,
                                    std::string_view pinName, int pinType, bool showMap, bool discoverAll) {
                                    // TODO discoverAll used by certain vesvigirs (which ones?)
#if AVL_IS_ON(AVL_ZONE_GENERATION)
                                 if (auto &&instance = find_nearest_feature(locationName, point)) {
                                     LOG_INFO(AVL_LOGGER, "Found location: '{}'", locationName);
                                     RouteManager::instance().Invoke(peer->GetUserID(),
                                                            avledet::util::hashes::Routed::S2C_ResponseIcon,
                                                            pinName, pinType, instance->m_pos, showMap);
                                 } else {
                                     LOG_INFO(AVL_LOGGER, "Failed to find location: '{}'", locationName);
                                 }
#else
        Vector3f out;
        if (find_nearest_feature(locationName, point, out)) {
            LOG_INFO(AVL_LOGGER, "Found location: '{}'", locationName);
            RouteManager::instance().Invoke(peer->m_uuid,
                avledet::util::hashes::Routed::S2C_ResponseIcon,
                pinName,
                pinType,
                out,
                showMap
            );
        }
        else {
            LOG_INFO(AVL_LOGGER, "Failed to find location: '{}'", locationName);
        }
#endif
                             });

    // 1.0 renamed Ping/Pong to RPC_Ping/RPC_Pong
    RouteManager::instance().Register("RPC_Ping", [](Peer::Ptr peer, float time) {
        peer->Route(avledet::util::hashes::Routed::Pong, time);
    });
}

// private
void ZoneManager::OnNewPeer(Peer::Ptr peer)
{
    SendGlobalKeys(peer);
    SendLocationIcons(peer);
}

bool ZoneManager::ZonesOverlap(ZoneID zone, Vector3f refPoint)
{
    return ZonesOverlap(zone, WorldToZonePos(refPoint));
}

bool ZoneManager::ZonesOverlap(ZoneID zone, ZoneID refCenterZone)
{
    int num = NEAR_ZRADIUS - 1;
    return zone.x >= refCenterZone.x - num && zone.x <= refCenterZone.x + num
           && zone.y <= refCenterZone.y + num && zone.y >= refCenterZone.y - num;
}

bool ZoneManager::IsPeerNearby(ZoneID zone, avledet::util::UserID uid)
{
    auto &&peer = NetManager::instance().FindPeerByUserID(uid);
    //assert((peer && uid) || (!peer && uid)); // makes sure no peer is ever found with 0 uid
    if (peer)
        return ZonesOverlap(zone, peer->m_pos);
    return false;
}

// private
void ZoneManager::SendGlobalKeys()
{
    RouteManager::instance().InvokeAll(avledet::util::hashes::Routed::S2C_UpdateKeys, m_globalKeys);
}

void ZoneManager::SendGlobalKeys(Peer::Ptr peer)
{
    //RouteManager::instance().Invoke(peer, avledet::util::hashes::Routed::S2C_UpdateKeys, m_globalKeys);
    peer->Route(avledet::util::hashes::Routed::S2C_UpdateKeys, m_globalKeys);
}

#if AVL_IS_ON(AVL_ZONE_GENERATION)
// private
void ZoneManager::SendLocationIcons()
{
    DataWriter writer;

    auto &&icons = GetFeatureIcons();

    writer.write((std::uint32_t) icons.size());
    for (auto &&instance : icons) {
        writer.write(instance.get().m_pos);
        writer.write(std::string_view(instance.get().m_feature.get().m_name));
    }

    RouteManager::instance().InvokeAll(avledet::util::hashes::Routed::S2C_UpdateIcons, writer.release());
}
#endif

// private
void ZoneManager::SendLocationIcons(Peer::Ptr peer)
{
    LOG_NOTICE(AVL_LOGGER, "Sending location icons to {}", peer->m_name);

#if AVL_IS_ON(AVL_ZONE_GENERATION)
    DataWriter writer;

    auto &&icons = GetFeatureIcons();

    writer.write((std::int32_t) icons.size());
    for (auto &&instance : icons) {
        writer.write(instance.get().m_pos);
        writer.write(instance.get().m_feature.get().m_name);
    }

    peer->Route(avledet::util::hashes::Routed::S2C_UpdateIcons, writer.release());
#else
    peer->SubRoute(avledet::util::hashes::Routed::S2C_UpdateIcons, [this](DataWriter &writer) {
        writer.write<std::int32_t>(1);// dummy count
        for (auto &&pair : m_generatedFeatures) {
            // We only care about StartTemple
            //if (m_features[pair.second.first] == std::string_view(m_features[0])) {
            if (pair.second.first == 0) {
                writer.write(pair.second.second);           // pos
                writer.write(m_features[pair.second.first]);// name
                return;
            }
        }
        assert(false);
    });
#endif
}

// public
void ZoneManager::Save(DataWriter &pkg)
{
#if AVL_IS_ON(AVL_ZONE_GENERATION)
    pkg.write((std::int32_t) m_generatedZones.size());
    for (auto &&zone : m_generatedZones) {
        pkg.write((std::int32_t) zone.x);
        pkg.write((std::int32_t) zone.y);
    }
#else
    LOG_WARNING(AVL_LOGGER, "Saving while AVL_ZONE_GENERATION:0 is not fully portable");

    pkg.write<std::int32_t>(0);// 0 count
#endif
    pkg.write((std::int32_t) 0);// PGW
    pkg.write(VConstants::LOCATION);
    pkg.write(m_globalKeys);
    pkg.write(true);
    pkg.write((std::int32_t) m_generatedFeatures.size());
    for (auto &&pair : m_generatedFeatures) {
#if AVL_IS_ON(AVL_ZONE_GENERATION)
        auto &&inst     = pair.second;
        auto &&location = inst->m_feature.get();

        pkg.write(std::string_view(location.m_name));
        pkg.write(inst->m_pos);
        pkg.write(m_generatedZones.contains(WorldToZonePos(inst->m_pos)));
#else
        static_assert(
                std::is_same_v<Vector3f,
                               decltype(decltype(std::remove_cvref_t<decltype(pair)>::second)::second)>);

        pkg.write(m_features[pair.second.first]);
        pkg.write(pair.second.second);
        pkg.write(true);
#endif
    }
}

// public
void ZoneManager::Load(DataReader &reader, std::int32_t version)
{
    {
        auto count = reader.read<std::uint32_t>();
        for (decltype(count) i = 0; i < count; i++) {
            auto x = reader.read<std::int32_t>();
            auto y = reader.read<std::int32_t>();
#if AVL_IS_ON(AVL_ZONE_GENERATION)
            m_generatedZones.insert(ZoneID(static_cast<std::int16_t>(x), std::int16_t(y)));
#endif// AVL_ZONE_GENERATION
        }
    }

#if AVL_IS_ON(AVL_LEGACY_WORLD_LOADING)
    if (version >= 13)
#endif                              // AVL_LEGACY_WORLD_LOADING
    {
        reader.read<std::int32_t>();// PGW
        auto const locationVersion = (version >= 21) ? reader.read<std::int32_t>() : 0;// 26

#if AVL_IS_ON(AVL_LEGACY_WORLD_LOADING)
        if (version >= 14)
#endif// AVL_LEGACY_WORLD_LOADING
        {
            m_globalKeys = reader.read<decltype(m_globalKeys)>();

#if AVL_IS_ON(AVL_LEGACY_WORLD_LOADING)
            if (version >= 18)
#endif// AVL_LEGACY_WORLD_LOADING
            {
#if AVL_IS_ON(AVL_LEGACY_WORLD_LOADING)
                if (version >= 20)
#endif                                  // AVL_LEGACY_WORLD_LOADING
                {
                    reader.read<bool>();// locationsGenerated
                }

                auto count = reader.read<std::int32_t>();
                for (decltype(count) i = 0; i < count; i++) {
                    auto text = reader.read<std::string_view>();
                    auto pos  = reader.read<Vector3f>();

#if AVL_IS_ON(AVL_LEGACY_WORLD_LOADING)
                    bool generated = (version >= 19) ? reader.read<bool>() : false;
#else // !AVL_LEGACY_WORLD_LOADING
                    bool generated = reader.read<bool>();
#endif// AVL_LEGACY_WORLD_LOADING

#if AVL_IS_ON(AVL_ZONE_GENERATION)
                    auto &&location = GetFeature(text);
                    if (location) {
                        m_generatedFeatures[WorldToZonePos(pos)]
                                = std::make_unique<Feature::Instance>(*location, pos);
                    } else {
                        LOG_ERROR(AVL_LOGGER, "Unknown feature '{}'", text);
                    }
#else // !AVL_ZONE_GENERATION
                    static_assert(std::numeric_limits<std::uint8_t>::max() > m_features.size());
                    for (std::uint8_t i = 0; i < m_features.size(); i++) {
                        if (text == std::string_view(m_features[i])) {
                            // register
                            m_generatedFeatures[WorldToZonePos(pos)]
                                    = std::pair<std::uint8_t, Vector3f>(i, pos);
                            break;
                        }
                    }
#endif// AVL_ZONE_GENERATION
                }

                LOG_NOTICE(AVL_LOGGER, "Loaded {}/{} feature instances ", m_generatedFeatures.size(), count);

                if (locationVersion != VConstants::LOCATION) {
                    // regenerate features?
                    //m_generatedFeatures.clear();
                }
            }
        }
    }
}

// private
void ZoneManager::Update()
{
    ZoneScoped;

#if AVL_IS_ON(AVL_ZONE_GENERATION)
    // TODO 100ms seems a tad too frequent
    //  peers dont even move that fast
    if (VUtils::run_periodic<struct periodic_zone_generation>(100ms)) {
        for (auto &&peer : NetManager::instance().GetPeers()) {
            if (!peer->IsGated()) {
                // It turns out that zdos generated by whatever ids are different from packets...
                //  although almost everything is the same...
                //  So the best ACTUAL way to capture the world would be to pre-generate the entire world
                //  then disable the world generation during playback
                TryGenerateNearbyZones(peer->m_pos);
            }
        }
    }
#endif
}

/*
void ZoneManager::RegenerateZone(const ZoneID& zone) {
    for (auto&& zdo : ZdoManager::instance().GetZDOs(zone, 0, Prefab::Flag::NONE, Prefab::Flag::Player))
        ZdoManager::instance().DestroyZDO(zdo);

    //m_generatedZones.erase(zone);
    PopulateZone(HeightmapManager::instance().GetHeightmap(zone));
    //m_generatedZones.insert(zone);
}*/
#if AVL_IS_ON(AVL_ZONE_GENERATION)
// Rename?
void ZoneManager::TryGenerateNearbyZones(Vector3f refPoint)
{
    auto zone = WorldToZonePos(refPoint);

    // Prioritize center zone
    if (!TryPollGenerateZone(zone)) {

        // If spawning fails, spawn other neighboring zones
        auto num = NEAR_ZRADIUS + DISTANT_ZRADIUS;
        for (int z = zone.y - num; z <= zone.y + num; z++) {
            for (int x = zone.x - num; x <= zone.x + num; x++) {

                // Skip center zone
                if (x == zone.x && z == zone.y)
                    continue;

                TryPollGenerateZone(ZoneID((std::int16_t) x, (std::int16_t) z));
            }
        }
    }
}

bool ZoneManager::GenerateZoneBlocking(ZoneID zone)
{
    if (is_inside_world_radius(zone)) {
        //if ((zone.x > -WORLD_INNER_ZRADIUS && zone.y > -WORLD_INNER_ZRADIUS
        //&& zone.x < WORLD_INNER_ZRADIUS && zone.y < WORLD_INNER_ZRADIUS))
        //{
        auto &&pair = m_generatedZones.insert(zone);
        if (pair.second) {
            PopulateZone(HeightmapManager::instance().GetHeightmap(zone));
            return true;
        }
    }
    return false;
}

bool ZoneManager::TryPollGenerateZone(ZoneID zone)
{
    if (is_inside_world_radius(zone) && !IsZoneGenerated(zone)) {
        //if ((zone.x >= -WORLD_INNER_ZRADIUS && zone.y >= -WORLD_INNER_ZRADIUS
        //    && zone.x <= WORLD_INNER_ZRADIUS && zone.y <= WORLD_INNER_ZRADIUS)
        //    && !IsZoneGenerated(zone)) {
        if (auto heightmap = HeightmapManager::instance().PollHeightmap(zone)) {
            m_generatedZones.insert(zone);

            PopulateZone(*heightmap);

            return true;
        }
    }
    return false;
}

void ZoneManager::PopulateZone(Heightmap &heightmap)
{
    ZoneScoped;

    //#if AVL_IS_ON(AVL_ZONE_GENERATION)
    std::vector<ClearArea> m_tempClearAreas;

    if (AVL_CONFIG.m_world_gen_features)
        m_tempClearAreas = TryGenerateFeature(heightmap.get_zone());

    if (AVL_CONFIG.m_world_gen_vegetation)
        PopulateFoliage(heightmap, m_tempClearAreas);

    if (AVL_CONFIG.m_world_gen_creatures) {
        ZdoManager::instance().Instantiate(*ZONE_CTRL_PREFAB, ZoneToWorldPos(heightmap.get_zone()));
    }

    // vanilla regenerates the heightmaps poked by new modifiers (rocks) in a later LateUpdate
    HeightmapManager::instance().ForceGenerateAll();
    //#endif // AVL_OPTION_ENABLE_ZONE_GENERATION
}

void ZoneManager::PopulateZone(ZoneID zone)
{
    this->PopulateZone(HeightmapManager::instance().GetHeightmap(zone));
}

// private
Vector3f ZoneManager::GetRandomPointInRadius(VUtils::Random::State &state, Vector3f center, float radius)
{
    float f   = state.next_float() * (float) (VUtils::PI * 2.0);
    float num = state.range(0.f, radius);
    return center + Vector3f(std::sin(f) * num, 0.f, std::cos(f) * num);
}

// private
// vanilla 1.0: alt biome only vegetation needs its alt biome in the sector, alt biomes can block by name
static bool AltBiomesAllow(ZoneManager::Foliage const &veg, Vector3f p)
{
    bool parentFound = veg.m_altBiomeParent.empty();
    for (auto &&alt : AltBiomeManager::instance().GetBiomeSector(p.x, p.z).m_altBiomes) {
        if (alt->m_name == veg.m_altBiomeParent)
            parentFound = true;
        auto &&names = alt->m_blockVegetationNames;
        if (std::find(names.begin(), names.end(), veg.m_name) != names.end())
            return false;
    }
    return parentFound;
}

void ZoneManager::PopulateFoliage(Heightmap &heightmap, std::vector<ClearArea> const &clearAreas)
{
    auto &&zoneID = heightmap.get_zone();

    Vector3f const center = ZoneToWorldPos(zoneID);

    auto const seed = GeoManager::instance().GetSeed();

    //avledet::util::Biome biomes = GeoManager::instance().GetBiomes(center.x, center.z);

    std::vector<ClearArea> placedAreas;

    for (auto const &zoneVegetation : m_foliage) {
        // This ultimately serves as a large precheck, assuming heightmap were being used (which it no longer seems good)
        if (!heightmap.HaveBiome(zoneVegetation->m_biome))
            continue;

        // TODO make unique per vegetation instance
        // this state will be the same for all same vegetation within a given zone, in a given world
        VUtils::Random::State state(seed + zoneID.x * 4271 + zoneID.y * 9187
                                    + zoneVegetation->m_prefab.get().m_hash);

        std::int32_t num3 = 1;
        // max is used for both chance, and quantity in conjunction with min
        if (zoneVegetation->m_max < 1) {
            if (state.next_float() > zoneVegetation->m_max) {
                continue;
            }
        } else {
            num3 = state.range((std::int32_t) zoneVegetation->m_min,
                               (std::int32_t) zoneVegetation->m_max + 1);
        }

        // flag should always be true, all vegetation seem to always have a NetView
        //bool flag = zoneVegetation.m_prefab.GetComponent<ZNetView>() != null;
        float maxTilt           = std::cos(zoneVegetation->m_maxTilt * (float) (VUtils::PI / 180.0));
        float minTilt           = std::cos(zoneVegetation->m_minTilt * (float) (VUtils::PI / 180.0));
        //float num5 = 32.0f - zoneVegetation.m_groupRadius;        // NEW
        //this.s_tempVeg.Clear();                               // NEW
        std::vector<float> s_tempVeg;
        float num6              = UNITS_PER_ZONE * .5f - zoneVegetation->m_groupRadius;
        int const spawnAttempts = zoneVegetation->m_forcePlacement ? (num3 * 50) : num3;
        std::int32_t numSpawned = 0;
        for (int i = 0; i < spawnAttempts; i++) {
            float vx = state.range(center.x - num6, center.x + num6);
            float vz = state.range(center.z - num6, center.z + num6);

            Vector3f basePos(vx, 0., vz);

            if (!AltBiomesAllow(*zoneVegetation, basePos))
                continue;

            auto const groupCount
                    = state.range(zoneVegetation->m_groupSizeMin, zoneVegetation->m_groupSizeMax + 1);
            bool generated = false;
            for (std::int32_t j = 0; j < groupCount; j++) {

                Vector3f pos
                        = (j == 0) ? basePos
                                   : GetRandomPointInRadius(state, basePos, zoneVegetation->m_groupRadius);

                // random rotations

                // y rotation in degrees
                float rot_y = state.range(0, 360);
                float scale = state.range(zoneVegetation->m_scaleMin, zoneVegetation->m_scaleMax);
                // x rotation in degrees
                float rot_x = state.range(-zoneVegetation->m_randTilt, zoneVegetation->m_randTilt);
                // z rotation in degrees
                float rot_z = state.range(-zoneVegetation->m_randTilt, zoneVegetation->m_randTilt);

                // Use a method similar to clear area with rectangular regions
                //if (!zoneVegetation->m_blockCheck
                //|| !IsBlocked(vector2)) // no unity   \_(^.^)_/
                {

                    Vector3f normal;
                    avledet::util::Biome biome;
                    avledet::util::BiomeArea biomeArea;
                    Heightmap &otherHeightmap = GetGroundData(pos, normal, biome, biomeArea);

                    if (!((std::to_underlying(zoneVegetation->m_biome) & std::to_underlying(biome))
                          && (std::to_underlying(zoneVegetation->m_biomeArea)
                              & std::to_underlying(biomeArea)))) {
                        continue;
                    }

                    // so, TODO
                    //  make *some* or certain vegetation spawn really high up if a tall spire is in radius
                    // Mistlands only
                    //  A huge amount of mistlands are angled rock spires (not part of terrain),
                    //  so vegetation spanws on top of these,
                    // I do not have a way to implement this. The client, however does, which updating
                    // objects with static physics every so often while nearby
                    //
                    //float y2 = 0;
                    //Vector3f vector4;
                    //if (zoneVegetation->m_snapToStaticSolid && GetStaticSolidHeight(vector2, y2, vector4)) {
                    //    vector2.y = y2;
                    //    vector3 = vector4;
                    //}

                    float waterDiff = pos.y - WATER_LEVEL;
                    if (waterDiff < zoneVegetation->m_minAltitude
                        || waterDiff > zoneVegetation->m_maxAltitude) {
                        continue;
                    }

                    // Mistlands only
                    // TODO might be affecting mist (probably is? just a hunch)
                    if (zoneVegetation->m_minVegetation != zoneVegetation->m_maxVegetation) {
                        float vegetationMask = otherHeightmap.GetVegetationMask(pos);
                        if (vegetationMask > zoneVegetation->m_maxVegetation
                            || vegetationMask < zoneVegetation->m_minVegetation) {
                            continue;
                        }
                    }

                    if (zoneVegetation->m_minOceanDepth != zoneVegetation->m_maxOceanDepth) {
                        float oceanDepth = otherHeightmap.GetOceanDepth(pos);
                        if (oceanDepth < zoneVegetation->m_minOceanDepth
                            || oceanDepth > zoneVegetation->m_maxOceanDepth) {
                            continue;
                        }
                    }

                    if (normal.y >= maxTilt && normal.y <= minTilt) {

                        if (zoneVegetation->m_terrainDeltaRadius > 0) {
                            float num12;
                            Vector3f vector5;
                            GetTerrainDelta(state, pos, zoneVegetation->m_terrainDeltaRadius, num12, vector5);
                            if (num12 > zoneVegetation->m_maxTerrainDelta
                                || num12 < zoneVegetation->m_minTerrainDelta) {
                                continue;
                            }
                        }

                        // NEW CODE
                        if (zoneVegetation->m_minDistanceFromCenter > 0.0f || zoneVegetation->m_maxDistanceFromCenter > 0.0f)
                        {
                            //float mag =  Utils.LengthXZ(pos);
                            float mag =  Vector2f(pos.x, pos.z).magnitude();
                            if (
                                (zoneVegetation->m_minDistanceFromCenter > 0.0f && mag < zoneVegetation->m_minDistanceFromCenter) 
                                || (zoneVegetation->m_maxDistanceFromCenter > 0.0f && mag > zoneVegetation->m_maxDistanceFromCenter))
                            {
                                continue;
                            }
                        }
                        // END NEW CODE

                        if (zoneVegetation->m_inForest) {
                            float forestFactor = GeoManager::instance().GetForestFactor(pos);
                            if (forestFactor < zoneVegetation->m_forestTresholdMin
                                || forestFactor > zoneVegetation->m_forestTresholdMax) {
                                continue;
                            }
                        }

                        /**
                        TODO
                            confirm the new ashlands logic below

                            There are a BUNCH of new VEGETATION vars that need to be dumped...
                         */
                        
                        if (zoneVegetation->m_surroundCheckVegetation)
                        {
                            float num17 = 0.0f;
                            for (int k = 0; k < zoneVegetation->m_surroundCheckLayers; k++)
                            {
                                float num18 = (float)(k + 1) / (float)zoneVegetation->m_surroundCheckLayers * zoneVegetation->m_surroundCheckDistance;
                                for (int l = 0; l < 6; l++)
                                {
                                    float num19 = (float)l / 6.0f * 3.1415927f * 2.0f;
                                    float vegetationMask2 = otherHeightmap.GetVegetationMask(pos + Vector3f(std::sin(num19) * num18, 0.0f, std::cos(num19) * num18));
                                    float num20 = (1.0f - num18) / (zoneVegetation->m_surroundCheckDistance * 2.0f);
                                    num17 += vegetationMask2 * num20;
                                }
                            }
                            /* this. */s_tempVeg.push_back(num17);
                            if (s_tempVeg.size() < 10)
                            {
                                continue;
                            }
                            float num21 = *std::max_element(s_tempVeg.begin(), s_tempVeg.end());
                            //float num22 = this.s_tempVeg.Average();
                            float num22 = /* this. */std::accumulate(s_tempVeg.begin(), s_tempVeg.end(), 0.0f) / (float)s_tempVeg.size();
                            float num23 = num22 + (num21 - num22) * zoneVegetation->m_surroundBetterThanAverage;
                            if (num17 < num23)
                            {
                                continue;
                            }
                        }

                        if (!InsideClearArea(clearAreas, pos)
                            // Do NOT remove the below
                            //  These are MY CUSTOM checks to compensate for lack of unity physics...
                            && (zoneVegetation->m_radius == 0 || !zoneVegetation->m_blockCheck
                                || !OverlapsClearArea(placedAreas, pos, zoneVegetation->m_radius)))// custom, only where vanilla IsBlocked runs
                        {

                            if (zoneVegetation->m_snapToWater)
                                pos.y = WATER_LEVEL;

                            pos.y += zoneVegetation->m_groundOffset;
                            Quaternion rotation;

                            if (zoneVegetation->m_chanceToUseGroundTilt > 0
                                && state.next_float() <= zoneVegetation->m_chanceToUseGroundTilt) {
                                auto rotation2 = Quaternion::euler(0, rot_y, 0);
                                rotation       = Quaternion::look_rotation(
                                        normal.cross(rotation2 * Vector3f::FORWARD), normal);
                            } else {
                                rotation = Quaternion::euler(rot_x, rot_y, rot_z);
                            }

                            // TODO rotation during generation are not correct
                            //  this is proven because of the correct world loaded zdos,
                            //  however new generated zone zdos are not correctly rotated

                            auto &&zdo = ZdoManager::instance().Instantiate(zoneVegetation->m_prefab, pos);
                            zdo->set_rotation(rotation);

                            // basically any solid objects cannot be overlapped
                            //  the exception to this rule is mist, swamp_beacon, silvervein... basically non-physical vegetation
                            if (zoneVegetation->m_radius > 0)
                                placedAreas.push_back({pos, zoneVegetation->m_radius});

                            // vanilla SetLocalScale, synced only with m_syncInitialScale
                            if (scale != zoneVegetation->m_prefab.get().m_localScale.x
                                && zoneVegetation->m_prefab.get().AllFlagsPresent(Prefab::Flag::SYNC_INITIAL_SCALE)) {
                                zdo->set_local_scale(Vector3f(scale, scale, scale), true);
                            }
                            AwakeRandom(state, *zdo, false);
                            RegisterSpawned(*zdo, scale);

                            generated = true;
                        }
                    }
                }
            }

            if (generated) {
                numSpawned++;
            }

            if (numSpawned >= num3) {
                break;
            }
        }
    }
}

// private
bool ZoneManager::InsideClearArea(std::vector<ClearArea> const &areas, Vector3f point)
{
    for (auto &&clearArea : areas) {
        if (point.x > clearArea.m_center.x - clearArea.m_semiWidth
            && point.x < clearArea.m_center.x + clearArea.m_semiWidth
            && point.z > clearArea.m_center.z - clearArea.m_semiWidth
            && point.z < clearArea.m_center.z + clearArea.m_semiWidth) {
            return true;
        }
    }
    return false;
}

bool ZoneManager::OverlapsClearArea(std::vector<ClearArea> const &areas, Vector3f point, float radius)
{
    for (auto &&area : areas) {

        float d  = VUtils::Math::sq_distance_to(point.x, point.z, area.m_center.x, area.m_center.z);
        float rd = area.m_semiWidth + radius;

        if (d < rd * rd)
            return true;
    }
    return false;
}

// private
ZoneManager::Feature const *ZoneManager::GetFeature(std::string_view name)
{
    for (auto &&feature : m_features) {
        if (feature->m_name == name)
            return feature.get();
    }
    return nullptr;
}

// public
// call from within ZNet.init or earlier...
void ZoneManager::PostGeoInit()
{
    // Will be empty if world failed to load
    if (!m_generatedFeatures.empty())
        return;

    // Crucially important Location
    // So check that it exists period
    auto &&spawnLoc = GetFeature("StartTemple");
    if (!spawnLoc)
        throw std::runtime_error("World spawnpoint missing (StartTemple)");

    if (!AVL_CONFIG.m_world_gen_features) {
        LOG_WARNING(AVL_LOGGER, "Location generation is disabled");
        PrepareFeatures(*spawnLoc);
    } else {
        auto now(std::chrono::steady_clock::now());

        // Already presorted by priority
        for (auto &&loc : m_features) {
            PrepareFeatures(*loc.get());
        }

        LOG_INFO(AVL_LOGGER, "Location generation took {}s",
                 std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - now)
                         .count());
    }

    if (AVL_CONFIG.m_world_pregenerate && m_generatedZones.empty()) {
        auto now(std::chrono::steady_clock::now());
        int prevCount = 0;

        LOG_WARNING(AVL_LOGGER, "Pregenerating world...");
        LOG_WARNING(AVL_LOGGER,
                    "Pregeneration takes up a lot of memory and resources during and after generation!");
        LOG_WARNING(AVL_LOGGER, "This setting is experimental and unoptimized! (I dont know why :(");
        LOG_WARNING(AVL_LOGGER, "This will take a while!");
        while (m_generatedZones.size() < WORLD_INNER_ZDIAMETER * WORLD_INNER_ZDIAMETER) {
            for (std::int16_t y = -WORLD_INNER_ZRADIUS; y < WORLD_INNER_ZRADIUS; y++) {
                for (std::int16_t x = -WORLD_INNER_ZRADIUS; x < WORLD_INNER_ZRADIUS; x++) {
                    auto zone = ZoneID(x, y);
                    if (!is_inside_world_radius(zone)) {
                        continue;
                    }

                    TryPollGenerateZone(zone);

                    if (VUtils::run_periodic<struct periodic_pregen_stats>(3s)) {
                        std::string lines = COLOR_RESET;
                        // print a cool grid
                        for (std::int16_t iy = -WORLD_INNER_ZRADIUS; iy <= WORLD_INNER_ZRADIUS; iy += 6) {
                            for (std::int16_t ix = -WORLD_INNER_ZRADIUS; ix <= WORLD_INNER_ZRADIUS; ix += 6) {
                                if (std::abs(ix - x) < 3 && std::abs(iy - y) < 3) {
                                    lines += COLOR_GOLD;
                                } else if (m_generatedZones.contains({ix, iy})) {
                                    lines += COLOR_GREEN;
                                } else {
                                    lines += COLOR_GRAY;
                                }
                                lines += "O ";
                            }
                            lines += COLOR_RESET + std::string("\n");
                        }

                        std::cout << lines << "\n";

                        //LOG_INFO(AVL_LOGGER, "Zone progress: \n{}", lines);
                        LOG_WARNING(AVL_LOGGER, "{}/{} zones generated \t({} z/s)", m_generatedZones.size(),
                                    (WORLD_INNER_ZRADIUS * 2 * WORLD_INNER_ZRADIUS * 2),
                                    ((m_generatedZones.size() - prevCount) / 3));
                        prevCount = m_generatedZones.size();
                    }
                }
            }
            // TODO; sleep to avoid busy loop?
            //  because many zones will be iterated, just waiting for generation to be successful, in the meantime taking up expensive cycles...
            //std::this_thread::sleep_for(1ms);
        }

        LOG_WARNING(AVL_LOGGER, "Pregeneration took {}s",
                    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - now)
                            .count());
    }
}

// private
// Location placement as in vanilla 1.0 (ZoneSystem.GenerateLocationsTimeSliced):
// zones come from random biome grid points, 6 tries per zone, alt biome checks.
void ZoneManager::PrepareFeatures(Feature const &feature)
{
    auto &&geo = GeoManager::instance();
    auto &&alt = AltBiomeManager::instance();

    int placed = 0;
    for (auto &&inst : m_generatedFeatures)
        if (inst.second->m_feature.get() == feature)
            placed++;

    if (feature.m_unique && placed > 0)
        return;

    auto meta = alt.GetLocationMeta(feature.m_name);
    std::string_view locName   = meta ? std::string_view(meta->m_name) : std::string_view(feature.m_name);
    std::string_view altParent = meta ? std::string_view(meta->m_altBiomeParent) : std::string_view();

    VUtils::Random::State state(geo.GetSeed() + feature.m_hash);
    float const maxRadius = std::max(feature.m_exteriorRadius, feature.m_interiorRadius);
    // m_spawnAttempts holds 200000 for prioritized locations (pkg format)
    int const attempts    = feature.m_spawnAttempts == 200000 ? 60000 : 12000;
    float maxRange        = feature.m_centerFirst ? feature.m_minDistance : 10000.0f;

    std::vector<float> tempVeg;

    // reject counters, same buckets as the vanilla debug log
    enum { InZone, Generated, Area, CenterDist, Biome, Altitude, Forest, FromCenter, Delta, Similar,
           NotSimilar, Veg, AltMissing, AltBlock, Surround, COUNT };
    std::array<int, COUNT> err {};

    for (int a = 0; a < attempts && placed < feature.m_quantity; a++) {
        ZoneID zone;
        if (feature.m_centerFirst) {
            zone = GetRandomZone(state, maxRange);
            maxRange += 1.0f;
        } else {
            auto point = feature.m_minAltitude < 0.0f
                    ? alt.GetRandomPointByBiomes(state, std::to_underlying(feature.m_biome))
                    : alt.GetRandomPointByBiomesAboveSeaLevel(state, std::to_underlying(feature.m_biome));
            zone = WorldToZonePos(Vector3f(AltBiomeManager::MapSpaceToWorldSpace(point.x), 0.0f,
                                           AltBiomeManager::MapSpaceToWorldSpace(point.y)));
        }

        if (m_generatedFeatures.contains(zone)) {
            err[InZone]++;
            continue;
        }
        // IsZoneGenerated: a new world has no generated zones yet
        if (m_generatedZones.contains(zone)) {
            err[Generated]++;
            continue;
        }

        auto biomeArea = geo.GetBiomeArea(ZoneToWorldPos(zone));
        if (!(std::to_underlying(feature.m_biomeArea) & std::to_underlying(biomeArea))) {
            err[Area]++;
            continue;
        }

        for (int j = 0; j < 6; j++) {
            auto p = GetRandomPointInZone(state, zone, maxRadius);

            float magnitude = p.magnitude();
            if ((feature.m_minDistance != 0.0f && magnitude < feature.m_minDistance)
                || (feature.m_maxDistance != 0.0f && magnitude > feature.m_maxDistance)) {
                err[CenterDist]++;
                continue;
            }

            auto biome = geo.GetBiome(p);
            if (!(std::to_underlying(feature.m_biome) & std::to_underlying(biome))) {
                err[Biome]++;
                continue;
            }

            avledet::util::Color mask;
            p.y = geo.GetHeight(p.x, p.z, mask);
            float altitude = (float) ((double) p.y - 30.0);
            if (altitude < feature.m_minAltitude || altitude > feature.m_maxAltitude) {
                err[Altitude]++;
                continue;
            }

            if (feature.m_inForest) {
                float forest = geo.GetForestFactor(p);
                if (forest < feature.m_forestTresholdMin || forest > feature.m_forestTresholdMax) {
                    err[Forest]++;
                    continue;
                }
            }

            if (feature.m_minDistanceFromCenter > 0.0f || feature.m_maxDistanceFromCenter > 0.0f) {
                float d = Vector2f(p.x, p.z).magnitude();
                if ((feature.m_minDistanceFromCenter > 0.0f && d < feature.m_minDistanceFromCenter)
                    || (feature.m_maxDistanceFromCenter > 0.0f && d > feature.m_maxDistanceFromCenter)) {
                    err[FromCenter]++;
                    continue;
                }
            }

            float delta = 0;
            Vector3f slope;
            geo.GetTerrainDelta(state, p, feature.m_exteriorRadius, delta, slope);
            if (delta > feature.m_maxTerrainDelta || delta < feature.m_minTerrainDelta) {
                err[Delta]++;
                continue;
            }

            if (feature.m_minDistanceFromSimilar > 0.0f
                && HaveLocationInRange(feature, feature.m_group, p, feature.m_minDistanceFromSimilar, false)) {
                err[Similar]++;
                continue;
            }
            if (feature.m_maxDistanceFromSimilar > 0.0f
                && !HaveLocationInRange(feature, feature.m_groupMax, p, feature.m_maxDistanceFromSimilar, true)) {
                err[NotSimilar]++;
                continue;
            }

            if ((feature.m_minimumVegetation > 0.0f && mask.a <= feature.m_minimumVegetation)
                || (feature.m_maximumVegetation < 1.0f && mask.a >= feature.m_maximumVegetation)) {
                err[Veg]++;
                continue;
            }

            auto &&sector = alt.GetBiomeSector(p.x, p.z);
            if (!altParent.empty()
                && std::none_of(sector.m_altBiomes.begin(), sector.m_altBiomes.end(),
                                [&](AltBiome const *b) { return b->m_name == altParent; })) {
                err[AltMissing]++;
                continue;
            }
            if (std::any_of(sector.m_altBiomes.begin(), sector.m_altBiomes.end(), [&](AltBiome const *b) {
                    return std::find(b->m_blockLocationNames.begin(), b->m_blockLocationNames.end(), locName)
                        != b->m_blockLocationNames.end();
                })) {
                err[AltBlock]++;
                continue;
            }

            if (feature.m_surroundCheckVegetation) {
                float score = 0.0f;
                for (int k = 0; k < feature.m_surroundCheckLayers; k++) {
                    float dist = (float) (k + 1) / (float) feature.m_surroundCheckLayers * feature.m_surroundCheckDistance;
                    for (int l = 0; l < 6; l++) {
                        float f = (float) l / 6.0f * 3.1415927f * 2.0f;
                        Vector3f v = p + Vector3f(std::sin(f) * dist, 0.0f, std::cos(f) * dist);
                        avledet::util::Color mask2;
                        geo.GetHeight(v.x, v.z, mask2);
                        float w = (feature.m_surroundCheckDistance - dist) / (feature.m_surroundCheckDistance * 2.0f);
                        score += mask2.a * w;
                    }
                }
                tempVeg.push_back(score);
                if (tempVeg.size() < 10)
                    continue;
                float vmax  = *std::max_element(tempVeg.begin(), tempVeg.end());
                float vmean = std::accumulate(tempVeg.begin(), tempVeg.end(), 0.0f) / (float) tempVeg.size();
                if (score < vmean + (vmax - vmean) * feature.m_surroundBetterThanAverage)
                    continue;
            }

            m_generatedFeatures[WorldToZonePos(p)] = std::make_unique<Feature::Instance>(feature, p);
            placed++;
            break;
        }
    }

    if (placed < feature.m_quantity) {
        LOG_WARNING(AVL_LOGGER,
                    "Failed to place all {}, placed {}/{} (inZone {} area {} centerDist {} biome {} altitude {} forest {} "
                    "fromCenter {} delta {} similar {} notSimilar {} veg {} altMissing {} altBlock {})",
                    feature.m_name, placed, feature.m_quantity, err[InZone], err[Area], err[CenterDist], err[Biome],
                    err[Altitude], err[Forest], err[FromCenter], err[Delta], err[Similar], err[NotSimilar], err[Veg],
                    err[AltMissing], err[AltBlock]);
    }
}

bool ZoneManager::HaveLocationInRange(Feature const &loc, std::string const& group, Vector3f p, float radius, bool maxGroup)
{
    for (const auto& pair : m_generatedFeatures)
    {
        const auto& locationInstance             = pair.second;
        const auto& location    = locationInstance->m_feature.get();
        if ((location == loc || (!maxGroup && !group.empty() && group == location.m_group) 
        || (maxGroup && !group.empty() && group == location.m_groupMax)) 
            && locationInstance->m_pos.sq_distance_to(p) < radius * radius)
        {
            return true;
        }
    }
    return false;
}

/* private bool HaveLocationInRange(string prefabName, string group, Vector3 p, float radius, bool maxGroup = false)
{
    foreach (ZoneSystem.LocationInstance locationInstance in this.m_locationInstances.Values)
    {
        if ((locationInstance.m_location.m_prefab.Name == prefabName || (!maxGroup && group.Length > 0 && group == locationInstance.m_location.m_group) || (maxGroup && group.Length > 0 && group == locationInstance.m_location.m_groupMax)) && Vector3.Distance(locationInstance.m_position, p) < radius)
        {
            return true;
        }
    }
    return false;
} */

Vector3f ZoneManager::GetRandomPointInZone(VUtils::Random::State &state, ZoneID zone, float locationRadius)
{
    auto pos  = ZoneToWorldPos(zone);
    float num = UNITS_PER_ZONE / 2.f;
    float x   = state.range(-num + locationRadius, num - locationRadius);
    float z   = state.range(-num + locationRadius, num - locationRadius);
    return pos + Vector3f(x, 0.f, z);
}

ZoneID ZoneManager::GetRandomZone(VUtils::Random::State &state, float range)
{
    int num = (std::int32_t) range / (std::int32_t) UNITS_PER_ZONE;
    ZoneID zone;
    do {
        float x = (float) state.range(-num, num);
        float y = (float) state.range(-num, num);
        zone    = ZoneID((std::int16_t) x, (std::int16_t) y);
    } while (ZoneToWorldPos(zone).magnitude() >= 10000);
    return zone;
}

// private
std::vector<ZoneManager::ClearArea> ZoneManager::TryGenerateFeature(ZoneID zoneID)
{
    auto now(std::chrono::steady_clock::now());

    std::vector<ClearArea> clearAreas;

    auto &&find = m_generatedFeatures.find(zoneID);
    if (find != m_generatedFeatures.end()) {
        auto &&locationInstance = find->second;
        auto &&location         = locationInstance->m_feature.get();

        Vector3f position = locationInstance->m_pos;
        Vector3f normal;
        avledet::util::Biome biome;
        avledet::util::BiomeArea biomeArea;
        GetGroundData(position, normal, biome, biomeArea);

        // m_snapToWater is Mistlands only
        if (location.m_snapToWater)
            position.y = WATER_LEVEL;

        if (location.m_clearArea)
            clearAreas.push_back({position, location.m_exteriorRadius});

        // vanilla takes both from the unseeded global random; seeded here like the location,
        // the vanilla probe does the same with PROBE_SEEDED_ROTATION=1
        VUtils::Random::State random(GeoManager::instance().GetSeed() + zoneID.x * 4271 + zoneID.y * 9187);
        Quaternion rot;
        if (location.m_slopeRotation) {
            // face down the slope, snapped to 22.5 degrees
            float delta;
            Vector3f slope;
            GetTerrainDelta(random, position, location.m_exteriorRadius, delta, slope);
            float yaw = std::atan2(slope.x, slope.z) * (float) (180.0 / VUtils::PI);
            rot = Quaternion::euler(0, std::round(yaw / 22.5f) * 22.5f, 0);
        } else if (location.m_randomRotation) {
            rot = Quaternion::euler(0, random.range(0, 16) * 22.5f, 0);
        }

        avledet::util::Hash seed = GeoManager::instance().GetSeed() + zoneID.x * 4271 + zoneID.y * 9187;
        GenerateFeature(location, seed, position, rot);

        // TODO fix quill Vector find
        LOG_INFO(AVL_LOGGER, "Placed '{}' in zone {} ({})", location.m_name, zoneID, position);

        // Remove all other Haldor locations, etc...
        if (location.m_unique) {
            RemoveUngeneratedFeatures(location);
        }

        // TODO determine whether this method requires a special Peer* method
        if (location.m_iconPlaced) {
            SendLocationIcons();
        }
    }

    return clearAreas;
}

// private
void ZoneManager::RemoveUngeneratedFeatures(Feature const &feature)
{
    int count = 0;
    for (auto &&itr = m_generatedFeatures.begin(); itr != m_generatedFeatures.end();) {
        auto &&instance     = itr->second;
        auto &&otherFeature = instance->m_feature.get();
        if (!IsZoneGenerated(WorldToZonePos(instance->m_pos)) && otherFeature == feature) {
            itr = m_generatedFeatures.erase(itr);
            count++;
        } else
            ++itr;
    }

    LOG_INFO(AVL_LOGGER, "Removed {} unplaced '{}'", count, feature.m_name);
}

// private
void ZoneManager::AwakeRandom(VUtils::Random::State &state, ZDO &zdo, bool randomDamage) const
{
    auto itr = m_awakeDraws.find(zdo.get_prefab().get().m_hash);
    if (itr == m_awakeDraws.end())
        return;

    auto &&d = itr->second;
    int skip = randomDamage ? d.m_drawsDamage : d.m_draws;
    if (randomDamage && d.m_damageIndex >= 0) {
        for (int i = 0; i < d.m_damageIndex; i++)
            state.next_float();
        zdo.set(avledet::util::hashes::ZDO::WearNTear::HEALTH, state.range(0.1f * d.m_health, d.m_health * 0.6f));
        skip -= d.m_damageIndex + 1;
    }
    for (int i = 0; i < skip; i++)
        state.next_float();
}

void ZoneManager::RegisterSpawned(ZDO &zdo, float scale)
{
    auto itr = m_prefabMods.find(zdo.get_prefab().get().m_hash);
    if (itr == m_prefabMods.end())
        return;
    for (auto m : itr->second) {
        m.m_pos = zdo.get_position() + zdo.get_rotation() * (m.m_pos * scale);
        HeightmapManager::instance().AddModifier(m, true);
    }
}

void ZoneManager::RegisterVisuals(avledet::gen::Randoms const &randoms, avledet::gen::RollResult const &roll,
                                  Vector3f pos, Quaternion rot)
{
    for (auto &&[mod, toggles] : randoms.m_terrainMods) {
        if (!toggles.Live(roll))
            continue;
        auto m  = mod;
        m.m_pos = pos + rot * m.m_pos;
        HeightmapManager::instance().AddModifier(m, false);
    }
    for (auto &&toggles : randoms.m_visualSnaps)
        if (toggles.Live(roll))
            m_visualSnapper = true;
}

void ZoneManager::SnappAll()
{
    if (m_snappers.empty() && !m_visualSnapper)
        return;
    HeightmapManager::instance().ForceGenerateAll();
    for (auto &&[zdo, offset] : m_snappers) {
        auto p = zdo->get_position();
        p.y    = GetGroundHeight(p) + offset;
        zdo->set_position(p);
    }
    m_snappers.clear();
    m_visualSnapper = false;
}

void ZoneManager::GenerateFeature(Feature const &location, avledet::util::Hash seed, Vector3f pos,
                                   Quaternion rot)
{
    VUtils::Random::State state(seed);
    std::vector<Prefab::Instance> offProxies;
    avledet::gen::RollResult roll;
    auto active
            = location.m_randoms.roll(state, location.m_pieces.size(), pos, rot, std::nullopt, &offProxies, &roll);
    auto snapOffset = [&](std::size_t i) -> std::optional<float> {
        for (auto &&[view, offset] : location.m_randoms.m_snaps)
            if (view == (std::int32_t) i)
                return offset;
        return std::nullopt;
    };

    // vanilla WearNTear.m_randomInitialDamage, on for the whole spawn including the dungeon
    bool randomDamage = location.m_applyRandomDamage;

    for (std::size_t i = 0; i < location.m_pieces.size(); i++) {
        if (!active[i])
            continue;
        auto &&piece = location.m_pieces[i];
        // Dungeon hierarchy:
        //  Location
        //      Interior (InteriorTransform)
        //          DG_(dungeon)

        if (!(AVL_CONFIG.m_dng_enabled && piece.get_prefab().get().AllFlagsPresent(Prefab::Flag::DUNGEON))) {
            auto &&zdo = ZdoManager::instance().Instantiate(piece.m_prefabHash, pos + rot * piece.m_pos, piece.m_scale);
            zdo->set_rotation(rot * piece.m_rot);
            AwakeRandom(state, *zdo, randomDamage);
            RegisterSpawned(*zdo);
            if (auto offset = snapOffset(i))
                AddSnapper(zdo, *offset);
            if (zdo->get_prefab().get().AllFlagsPresent(Prefab::Flag::WEAR_N_TEAR))
                zdo->set(avledet::util::hashes::ZDO::WearNTear::PRE_SNOW, true);
            // the clone carries its nested views, their Awake creates ZDOs again
            for (auto &&[parent, children] : location.m_randoms.m_nested) {
                if (parent != (std::int32_t) i)
                    continue;
                for (auto j : children) {
                    if (!active[j])
                        continue;
                    auto &&child = location.m_pieces[j];
                    auto &&nzdo  = ZdoManager::instance().Instantiate(child.m_prefabHash, pos + rot * child.m_pos, child.m_scale);
                    nzdo->set_rotation(rot * child.m_rot);
                    AwakeRandom(state, *nzdo, randomDamage);
                    RegisterSpawned(*nzdo);
                }
            }
        } else {
            auto &&dungeon = DungeonManager::instance().get_dungeon(piece.m_prefabHash);

            // TODO not really optional, it is required through a branch
            ZDO::optional zdo;

            if (dungeon.m_use_custom_interior_transform) {
                // vanilla SpawnLocation moves the interior transform so the generator lands in the zone
                // center, through Matrix4x4.Rotate(inv) * Translate column 3 (= inv * v) and back
                Vector3f zonePos = ZoneToWorldPos(WorldToZonePos(pos));
                Quaternion inv(-rot.x, -rot.y, -rot.z, rot.w);// Unity Quaternion.Inverse
                Vector3f local = inv * (zonePos + dungeon.m_interior_position + dungeon.m_original_position - pos);
                local.y        = dungeon.m_interior_position.y;

                zdo = ZdoManager::instance().Instantiate(piece.m_prefabHash, pos + rot * local);
                zdo->set_rotation(rot * inv);
            } else {
                zdo = ZdoManager::instance().Instantiate(piece.m_prefabHash, pos + rot * piece.m_pos, piece.m_scale);
                zdo->set_rotation(rot * piece.m_rot);
            }

            assert(zdo);

            // Only add real sky dungeon
            if (zdo->get_position().y > 4000)
                DungeonManager::instance().m_dungeonInstances.push_back(zdo->get_id());

            AwakeRandom(state, *zdo, randomDamage);
            DungeonManager::instance().generate(dungeon, zdo, randomDamage);
        }
    }

    // the proxy spawns the location visuals: inactive views turned on there get a ZDO in Awake,
    // its TerrainModifiers go live, and its SpawnLocation ends with SnappAll
    for (auto &&view : offProxies) {
        auto &&zdo = ZdoManager::instance().Instantiate(view.m_prefabHash, pos + rot * view.m_pos);
        zdo->set_rotation(rot * view.m_rot);
        RegisterSpawned(*zdo);
    }
    RegisterVisuals(location.m_randoms, roll, pos, rot);
    SnappAll();

    // https://www.reddit.com/r/valheim/comments/xns70u/comment/ipv77ca/?utm_source=share&utm_medium=web2x&context=3
    // https://www.reddit.com/r/valheim/comments/r6mv1q/comment/hmutgdl/?utm_source=share&utm_medium=web2x&context=3
    // LocationProxy are client-side generated models
    GenerateLocationProxy(location, seed, pos, rot);
}

// could be inlined...
// private
void ZoneManager::GenerateLocationProxy(Feature const &location, avledet::util::Hash seed, Vector3f pos,
                                         Quaternion rot)
{
    auto &&zdo = ZdoManager::instance().Instantiate(*LOCATION_PROXY_PREFAB, pos);
    zdo->set_rotation(rot);

    zdo->set(avledet::util::hashes::ZDO::ZoneManager::LOCATION, location.m_hash);
    zdo->set(avledet::util::hashes::ZDO::ZoneManager::SEED, seed);
}

// public
// TODO make this batch update every time a new location is added or whatever
std::list<std::reference_wrapper<ZoneManager::Feature::Instance>> ZoneManager::GetFeatureIcons()
{
    std::list<std::reference_wrapper<ZoneManager::Feature::Instance>> result;

    for (auto &&pair : m_generatedFeatures) {
        auto &&instance = pair.second;
        auto &&location = instance->m_feature.get();

        auto zone = WorldToZonePos(instance->m_pos);
        if (location.m_iconAlways || (location.m_iconPlaced && m_generatedZones.contains(zone))) {
            result.push_back(*instance.get());
        }
    }

    return result;
}

// private
void ZoneManager::GetTerrainDelta(VUtils::Random::State &state, Vector3f center, float radius, float &delta,
                                   Vector3f &slopeDirection)
{
    float num2 = -999999.f;
    float num3 = 999999.f;
    Vector3f b = center;
    Vector3f a = center;
    for (int i = 0; i < 10; i++) {
        Vector2f vector    = state.inside_unit_circle() * radius;
        Vector3f vector2   = center + Vector3f(vector.x, 0.f, vector.y);
        float groundHeight = GetGroundHeight(vector2);
        if (groundHeight < num3) {
            num3 = groundHeight;
            a    = vector2;
        }
        if (groundHeight > num2) {
            num2 = groundHeight;
            b    = vector2;
        }
    }
    delta          = num2 - num3;
    slopeDirection = (a - b).normal();
}

// used importantly for snapping and location/vegetation generation
// public
float ZoneManager::GetGroundHeight(Vector3f p)
{
    // vanilla raycasts the terrain mesh, misses return p.y
    Vector3f normal;
    HeightmapManager::instance().GetHeightmap(WorldToZonePos(p)).GetGroundData(p, p.y, normal);
    return p.y;
}

// public
// if terrain is just heightmap,
// could easily create a wrapper and poll points where needed
Heightmap &ZoneManager::GetGroundData(Vector3f &p, Vector3f &normal, avledet::util::Biome &biome,
                                       avledet::util::BiomeArea &biomeArea)
{
    auto &&heightmap = HeightmapManager::instance().GetHeightmap(WorldToZonePos(p));

    heightmap.GetGroundData(p, p.y, normal);

    biome     = heightmap.GetBiome(p);
    biomeArea = heightmap.GetBiomeArea();

    return heightmap;
}

// public
ZoneManager::Feature::Instance *ZoneManager::find_nearest_feature(std::string_view name, Vector3f point)
{
    float closestDist = std::numeric_limits<float>::max();

    ZoneManager::Feature::Instance *closest = nullptr;

    for (auto &&pair : m_generatedFeatures) {
        auto &&instance = pair.second;
        auto &&location = instance->m_feature.get();

        float dist = instance->m_pos.sq_distance_to(point);
        if (location.m_name == name && dist < closestDist) {
            closestDist = dist;
            closest     = instance.get();
        }
    }

    return closest;
}

#else

// public
bool ZoneManager::find_nearest_feature(std::string_view name, Vector3f in, Vector3f &out)
{
    float sqMin = std::numeric_limits<float>::max();

    for (auto &&pair : m_generatedFeatures) {
        if (m_features[pair.second.first] == name) {
            float sq = in.sq_distance_to(pair.second.second);
            if (sq < sqMin) {
                out   = pair.second.second;
                sqMin = sq;
            }
        }
    }

    return sqMin < std::numeric_limits<float>::max();
}
#endif

// public
// this is world position to zone position
// formerly GetZone
ZoneID ZoneManager::WorldToZonePos(Vector3f point)
{
    auto x = std::floor((point.x + (float) UNITS_PER_ZONE / 2.f) / (float) UNITS_PER_ZONE);
    auto y = std::floor((point.z + (float) UNITS_PER_ZONE / 2.f) / (float) UNITS_PER_ZONE);
    return ZoneID((std::int16_t) x, (std::int16_t) y);
}

// public
// zone position to ~world position
// GetZonePos
Vector3f ZoneManager::ZoneToWorldPos(ZoneID id)
{
    return Vector3f(id.x * UNITS_PER_ZONE, 0, id.y * UNITS_PER_ZONE);
}

#if AVL_IS_ON(AVL_ZONE_GENERATION)
// private
bool ZoneManager::IsZoneGenerated(ZoneID zoneID)
{
    return m_generatedZones.contains(zoneID);
}
#endif