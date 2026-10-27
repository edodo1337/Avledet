#include "RandomSpawn.h"
#include "GeoManager.h"
#include "Types.h"
#include <type_traits>
#include <vector>

// TODO this seems finicky,
//  but we dont need this to be used anywhere outside of here (for now)
template<>
struct avledet::util::Streamer<avledet::gen::RandomSpawn>
{
    decltype(auto) operator()(Reader &reader) const
    {
        return gen::RandomSpawn(reader);
    }
};

template<>
struct avledet::util::Streamer<avledet::gen::RandomObject>
{
    decltype(auto) operator()(Reader &reader) const
    {
        return gen::RandomObject(reader);
    }
};

namespace avledet::gen {

    RandomSpawn::RandomSpawn(util::Reader &reader)
    {
        // parse an individual rs
        this->m_chance = reader.read<float>();

        static_assert(std::is_same_v<std::underlying_type_t<avledet::util::Theme>, std::int32_t>);
        this->m_dungeon_require_theme = reader.read<avledet::util::Theme>();

        static_assert(std::is_same_v<std::underlying_type_t<util::Biome>, std::uint16_t>);
        this->m_biome = reader.read<util::Biome>();

        this->m_not_in_lava = reader.read<bool>();

        this->m_min_elevation = reader.read<std::int32_t>();
        this->m_max_elevation = reader.read<std::int32_t>();

        this->m_indexed = reader.read<decltype(m_indexed)>();

        this->m_pos = reader.read<Vector3f>();
        this->m_off = reader.read<std::int32_t>();
        this->m_offProxy.m_prefabHash = reader.read<avledet::util::Hash>();
        if (this->m_offProxy.m_prefabHash != 0) {
            this->m_offProxy.m_pos = reader.read<Vector3f>();
            this->m_offProxy.m_rot = reader.read<Quaternion>();
        }
    }

    RandomObject::RandomObject(util::Reader &reader)
    {
        m_dungeon_require_theme = reader.read<avledet::util::Theme>();
        m_biome                 = reader.read<util::Biome>();
        m_not_in_lava           = reader.read<bool>();
        m_min_elevation         = reader.read<std::int32_t>();
        m_max_elevation         = reader.read<std::int32_t>();
        m_pos                   = reader.read<Vector3f>();
        m_views                 = reader.read<decltype(m_views)>();

        auto count = reader.read<std::int32_t>();
        for (int i = 0; i < count; i++) {
            Entry entry;
            entry.m_weight    = reader.read<float>();
            entry.m_hasObject = reader.read<bool>();
            entry.m_views     = reader.read<decltype(entry.m_views)>();
            entry.m_objectId  = reader.read<std::int32_t>();
            m_entries.push_back(std::move(entry));
        }
    }

    Randoms Randoms::parse(util::Reader &reader)
    {
        Randoms randoms;
        randoms.m_spawns = reader.read<std::vector<RandomSpawn>>();
        auto nested      = reader.read<std::int32_t>();
        for (int i = 0; i < nested; i++) {
            auto parent = reader.read<std::int32_t>();
            randoms.m_nested.emplace_back(parent, reader.read<std::vector<std::uint16_t>>());
        }
        randoms.m_objects = reader.read<std::vector<RandomObject>>();

        auto snaps = reader.read<std::int32_t>();
        for (int i = 0; i < snaps; i++) {
            auto view = reader.read<std::int32_t>();
            randoms.m_snaps.emplace_back(view, reader.read<float>());
        }

        auto readToggles = [&reader]() {
            Toggles t;
            t.m_spawns  = reader.read<std::vector<std::uint16_t>>();
            t.m_offs    = reader.read<std::vector<std::uint16_t>>();
            t.m_objects = reader.read<std::vector<std::uint16_t>>();
            auto count  = reader.read<std::int32_t>();
            for (int i = 0; i < count; i++) {
                auto object = reader.read<std::int32_t>();
                t.m_entries.emplace_back(object, reader.read<std::int32_t>());
            }
            return t;
        };

        auto mods = reader.read<std::int32_t>();
        for (int i = 0; i < mods; i++) {
            TerrainMod m;
            m.m_sortOrder          = reader.read<std::int32_t>();
            m.m_playerModification = reader.read<bool>();
            m.m_levelOffset        = reader.read<float>();
            m.m_level              = reader.read<bool>();
            m.m_levelRadius        = reader.read<float>();
            m.m_square             = reader.read<bool>();
            m.m_smooth             = reader.read<bool>();
            m.m_smoothRadius       = reader.read<float>();
            m.m_smoothPower        = reader.read<float>();
            m.m_paintCleared       = reader.read<bool>();
            m.m_paintType          = (TerrainModifier::PaintType) reader.read<std::int32_t>();
            m.m_paintRadius        = reader.read<float>();
            m.m_paintHeightCheck   = reader.read<bool>();
            m.m_paintStrength      = reader.read<float>();
            m.m_pos                = reader.read<Vector3f>();
            randoms.m_terrainMods.emplace_back(m, readToggles());
        }

        auto visualSnaps = reader.read<std::int32_t>();
        for (int i = 0; i < visualSnaps; i++)
            randoms.m_visualSnaps.push_back(readToggles());

        auto colliders = reader.read<std::int32_t>();
        for (int i = 0; i < colliders; i++) {
            auto owner   = reader.read<std::int32_t>();
            auto toggles = readToggles();
            randoms.m_colliders.emplace_back(owner, std::move(toggles), phys::Collider::read(reader));
        }
        return randoms;
    }

    // ZoneSystem.IsLavaPreHeightmap
    static bool IsLava(Vector3f pos)
    {
        auto &&geo = GeoManager::instance();
        if (geo.GetBiome(pos) != util::Biome::AshLands)
            return false;
        util::Color mask;
        geo.GetBiomeHeight(util::Biome::AshLands, pos.x, pos.z, mask);
        return mask.a > 0.6f;
    }

    bool Toggles::Live(RollResult const &roll) const
    {
        for (auto k : m_spawns)
            if (!roll.m_spawned[k])
                return false;
        for (auto k : m_offs)
            if (roll.m_spawned[k])
                return false;
        for (auto k : m_objects)
            if (roll.m_picked[k] == 0)
                return false;
        for (auto &&[k, id] : m_entries)
            if (roll.m_picked[k] != id)
                return false;
        return true;
    }

    std::vector<bool> Randoms::roll(VUtils::Random::State &state, std::size_t viewCount, Vector3f pos,
                                    Quaternion rot, std::optional<util::Theme> themes,
                                    std::vector<Prefab::Instance> *offProxies, RollResult *result) const
    {
        std::vector<bool> active(viewCount, true);

        // vanilla caches the location biome on the prefab, here it is per spawn
        std::optional<util::Biome> locBiome;

        // checks that force "not spawned", the random is drawn anyway
        auto blocked = [&](util::Theme theme, util::Biome biome, bool notInLava, int minElevation,
                           int maxElevation, Vector3f p) {
            auto t = std::to_underlying(theme);
            if (themes && t != 0 && (std::to_underlying(*themes) & t) != t)
                return true;
            if (!themes && biome != util::Biome::None) {
                if (!locBiome)
                    locBiome = GeoManager::instance().GetBiome(p);
                auto b = std::to_underlying(*locBiome);
                if ((std::to_underlying(biome) & b) != b)
                    return true;
            }
            if (notInLava && IsLava(p))
                return true;
            return p.y < (float) minElevation || p.y > (float) maxElevation;
        };

        for (auto &&spawn : m_spawns) {
            bool spawned = state.range(0.f, 100.f) <= spawn.m_chance;
            if (blocked(spawn.m_dungeon_require_theme, spawn.m_biome, spawn.m_not_in_lava, spawn.m_min_elevation,
                        spawn.m_max_elevation, pos + rot * spawn.m_pos))
                spawned = false;

            if (!spawned)
                for (auto i : spawn.m_indexed)
                    active[i] = false;
            if (spawn.m_off >= 0)
                active[spawn.m_off] = !spawned;
            if (!spawned && offProxies && spawn.m_offProxy.m_prefabHash != 0)
                offProxies->push_back(spawn.m_offProxy);
            if (result)
                result->m_spawned.push_back(spawned);
        }

        for (auto &&object : m_objects) {
            // GetWeightedObject, -1 is null
            float total = 0;
            for (auto &&entry : object.m_entries)
                total += entry.m_weight;
            float r      = state.range(0.f, total);
            float sum    = 0;
            int picked   = -1;
            for (int i = 0; i < (int) object.m_entries.size(); i++) {
                sum += object.m_entries[i].m_weight;
                if (r <= sum) {
                    picked = object.m_entries[i].m_hasObject ? i : -1;
                    break;
                }
            }

            if (blocked(object.m_dungeon_require_theme, object.m_biome, object.m_not_in_lava,
                        object.m_min_elevation, object.m_max_elevation, pos + rot * object.m_pos))
                picked = -1;

            // vanilla compares objects, an object can be listed in several entries
            for (int i = 0; i < (int) object.m_entries.size(); i++) {
                auto &&entry = object.m_entries[i];
                if (!entry.m_hasObject || (picked != -1 && entry.m_views == object.m_entries[picked].m_views))
                    continue;
                for (auto v : entry.m_views)
                    active[v] = false;
            }
            if (picked == -1)
                for (auto v : object.m_views)
                    active[v] = false;
            if (result)
                result->m_picked.push_back(picked == -1 ? 0 : object.m_entries[picked].m_objectId);
        }

        return active;
    }

}// namespace avledet::gen
