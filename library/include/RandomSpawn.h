#pragma once

#include "DataStream.h"
#include "Prefab.h"
#include "TerrainModifier.h"
#include "Hashes.h"
#include "Quaternion.h"
#include "Vector.h"
#include "VUtilsRandom.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

// maybe create new ns
namespace avledet::gen {

    class RandomSpawn
    {
      public:
        float m_chance;
        avledet::util::Theme m_dungeon_require_theme;
        avledet::util::Biome m_biome;
        bool m_not_in_lava;
        int m_min_elevation;
        int m_max_elevation;
        std::vector<std::uint16_t> m_indexed;// views turned off when not spawned, values up 65535 max
        Vector3f m_pos;                      // local to the location/room
        std::int32_t m_off = -1;             // m_OffObject view, on when not spawned
        // m_OffObject that is an inactive view: its Awake creates a ZDO when the proxy/room
        // instantiate turns it on (vanilla quirk), m_prefabHash 0 when none
        Prefab::Instance m_offProxy {};

      public:
        RandomSpawn(util::Reader &reader);
    };

    // 1.0: picks one weighted entry, views of the other entries are turned off
    class RandomObject
    {
      public:
        struct Entry
        {
            float m_weight;
            bool m_hasObject;
            std::vector<std::uint16_t> m_views;
            std::int32_t m_objectId;// vanilla compares entries by object
        };

        avledet::util::Theme m_dungeon_require_theme;
        avledet::util::Biome m_biome;
        bool m_not_in_lava;
        int m_min_elevation;
        int m_max_elevation;
        Vector3f m_pos;
        std::vector<std::uint16_t> m_views;// all off when nothing is picked
        std::vector<Entry> m_entries;

      public:
        RandomObject(util::Reader &reader);
    };

    // Which RandomSpawn spawned and which object each RandomObject picked (0 none)
    struct RollResult
    {
        std::vector<bool> m_spawned;
        std::vector<std::int32_t> m_picked;
    };

    // A visual part (not a view) that is on only when its RandomSpawn/RandomObject ancestors are
    struct Toggles
    {
        std::vector<std::uint16_t> m_spawns;  // RandomSpawn object above: on when spawned
        std::vector<std::uint16_t> m_offs;    // RandomSpawn m_OffObject above: on when not spawned
        std::vector<std::uint16_t> m_objects; // RandomObject object above: on when something picked
        std::vector<std::pair<std::int32_t, std::int32_t>> m_entries;// RandomObject entry above: on when picked

        bool Live(RollResult const &roll) const;
    };

    // RandomSpawn and RandomObject of a location or a dungeon room
    class Randoms
    {
      public:
        std::vector<RandomSpawn> m_spawns;
        std::vector<RandomObject> m_objects;
        // views nested in another view, created again with it: parent index -> child indices
        std::vector<std::pair<std::int32_t, std::vector<std::uint16_t>>> m_nested;
        // views with SnapToGround: index, offset
        std::vector<std::pair<std::int32_t, float>> m_snaps;
        // TerrainModifiers and SnapToGround of the visuals (proxy or room instantiate)
        std::vector<std::pair<TerrainMod, Toggles>> m_terrainMods;
        std::vector<Toggles> m_visualSnaps;

        static Randoms parse(util::Reader &reader);

        // Vanilla Randomize of all RandomSpawn then all RandomObject, returns which views get spawned.
        // Dungeon rooms pass the dungeon themes, locations pass nullopt (then biome is checked).
        // offProxies gets the m_OffObject views to create as ZDOs (local to the location/room)
        std::vector<bool> roll(VUtils::Random::State &state, std::size_t viewCount, Vector3f pos, Quaternion rot,
                               std::optional<avledet::util::Theme> themes,
                               std::vector<Prefab::Instance> *offProxies = nullptr, RollResult *result = nullptr) const;
    };

}// namespace avledet::gen
