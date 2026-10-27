#include "AltBiomes.h"

#if AVL_IS_ON(AVL_ZONE_GENERATION)

    #include <algorithm>
    #include <cmath>
    #include <fstream>
    #include <nlohmann/json.hpp>
    #include <quill/LogMacros.h>
    #include <stack>

    #include "Avledet.h"
    #include "GeoManager.h"
    #include "Hashes.h"
    #include "VUtils.h"
    #include "VUtilsRandom.h"
    #include "WorldManager.h"

namespace {
    // Heightmap.BiomeIndex <-> Heightmap.Biome
    constexpr std::int32_t INDEX_TO_BIOME[10] = {0, 1, 2, 4, 8, 16, 32, 64, 256, 512};

    std::uint8_t to_index(std::int32_t biome)
    {
        for (std::uint8_t i = 0; i < 10; i++)
            if (INDEX_TO_BIOME[i] == biome)
                return i;
        throw std::runtime_error("biome has no index");
    }

    bool has_flag(std::int32_t value, std::int32_t flag) { return (value & flag) == flag; }

    // Enum.GetValues(Heightmap.Biome), sorted by value, includes Land and All
    constexpr std::int32_t BIOME_ENUM_VALUES[] = {0, 1, 2, 4, 8, 16, 32, 64, 256, 512, 0x27F, 0x37F};

    // ZoneSystem.GetZone(Vector3)
    std::int16_t zone_coord(float v) { return (std::int16_t) std::floor((v + 32.0f) / 64.0f); }

    std::vector<std::string> names_of(nlohmann::json const &list, char const *key)
    {
        std::vector<std::string> out;
        for (auto &&e : list)
            out.push_back(e.is_string() ? e.get<std::string>() : e.value(key, ""));
        return out;
    }
}// namespace

bool BiomeSector::CanAddModifier(AltBiome const &modifier) const
{
    if (m_distanceFromCenter < modifier.m_minDistanceFromCenter)
        return false;
    if (m_edgeCount < modifier.m_minEdgeSize)
        return false;
    if (m_edgeCount >= modifier.m_maxEdgeSize)
        return false;
    if (m_heightAvg < modifier.m_minAvgHeight || m_heightAvg >= modifier.m_maxAvgHeight)
        return false;
    if ((modifier.m_aboveWorldX != 0.0f && m_center.x < modifier.m_aboveWorldX)
        || (modifier.m_belowWorldX != 0.0f && m_center.x > modifier.m_belowWorldX)
        || (modifier.m_aboveWorldY != 0.0f && m_center.y < modifier.m_aboveWorldY)
        || (modifier.m_belowWorldY != 0.0f && m_center.y > modifier.m_belowWorldY))
        return false;

    for (auto &&alt : m_altBiomes)
        for (auto &&name : alt->m_incompatibleAltBiomes)
            if (name == modifier.m_name)
                return false;

    for (auto &&name : modifier.m_incompatibleAltBiomes)
        for (auto &&alt : m_altBiomes)
            if (alt->m_name == name)
                return false;

    // Vanilla quirk kept as is: it compares the neighbor biome with the index
    // cast to a biome, and index 0 (None) always matches HasFlag, so any
    // required neighbor makes the modifier impossible.
    if (modifier.m_requireNeighbor != 0) {
        for (int i = 0; i < 10; i++) {
            if (!has_flag(modifier.m_requireNeighbor, INDEX_TO_BIOME[i]))
                continue;
            bool found = false;
            for (auto &&n : m_neighbors) {
                if (n->m_biome == i) {
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
    }

    if (modifier.m_notNeighbor != 0) {
        for (int j = 0; j < 10; j++) {
            if (!has_flag(modifier.m_notNeighbor, INDEX_TO_BIOME[j]))
                continue;
            for (auto &&n : m_neighbors)
                if (n->m_biome == j)
                    return false;
        }
    }

    return true;
}

void AltBiomeManager::LoadAltBiomes()
{
    std::ifstream in("altbiomes.json");
    if (!in)
        throw std::runtime_error("altbiomes.json missing (dumped from the 1.0 server)");

    auto list = nlohmann::json::parse(in);
    for (auto &&j : list) {
        AltBiome a;
        a.m_name                  = j.at("m_name");
        a.m_enabled               = j.at("m_enabled");
        a.m_biome                 = j.at("m_biome");
        a.m_minDistanceFromCenter = j.at("m_minDistanceFromCenter");
        a.m_minAmountSpawned      = j.at("m_minAmountSpawned");
        a.m_maxAmountSpawned      = j.at("m_maxAmountSpawned");
        a.m_chance                = j.at("m_chance");
        a.m_requireNeighbor       = j.at("m_requireNeighbor");
        a.m_notNeighbor           = j.at("m_notNeighbor");
        a.m_incompatibleAltBiomes = names_of(j.at("m_incompatibleAltBiomes"), "");
        a.m_minEdgeSize           = j.at("m_minEdgeSize");
        a.m_maxEdgeSize           = j.at("m_maxEdgeSize");
        a.m_minAvgHeight          = j.at("m_minAvgHeight");
        a.m_maxAvgHeight          = j.at("m_maxAvgHeight");
        a.m_belowWorldX           = j.at("m_belowWorldX");
        a.m_aboveWorldX           = j.at("m_aboveWorldX");
        a.m_belowWorldY           = j.at("m_belowWorldY");
        a.m_aboveWorldY           = j.at("m_aboveWorldY");
        a.m_addLocations          = names_of(j.at("m_addLocations"), "m_prefabName");
        a.m_addVegetation         = names_of(j.at("m_addVegetation"), "m_name");
        a.m_blockLocationNames    = names_of(j.at("m_blockLocationNames"), "");
        a.m_blockVegetationNames  = names_of(j.at("m_blockVegetationNames"), "");
        m_altBiomes.push_back(std::move(a));
    }
    LOG_INFO(AVL_LOGGER, "Loaded {} alt biomes", m_altBiomes.size());

    std::ifstream metaIn("locations_meta.json");
    if (!metaIn)
        throw std::runtime_error("locations_meta.json missing (tools/make_location_meta.py)");
    auto meta = nlohmann::json::parse(metaIn);// keep alive, items() only references it
    for (auto &&[prefab, j] : meta.items())
        m_locationMeta.emplace_back(prefab, LocationMeta {j.at("name"), j.at("altBiomeParent")});
}

void AltBiomeManager::PostGeoInit()
{
    LoadAltBiomes();

    for (auto biome : BIOME_ENUM_VALUES)
        m_biomes.push_back(BiomeType {biome, {}, {}, {}});

    auto &&geo = GeoManager::instance();

    // GenerateBiomePoints: [x, y] layout like vanilla PointBiomes[j, i]
    std::vector<float> heights(SIZE * SIZE);
    std::vector<std::uint8_t> biomes(SIZE * SIZE);
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            float x   = MapSpaceToWorldSpace((float) j);
            float y   = MapSpaceToWorldSpace((float) i);
            auto &&idx = (std::size_t) j * SIZE + i;
            if (x * x + y * y > 110250000.0f) {
                biomes[idx]  = to_index((std::int32_t) VUtils::Biome::Ocean);
                heights[idx] = -1000.0f;
            } else {
                auto biome = geo.GetBiome(x, y, 0.02f, false);
                VUtils::Color mask;
                biomes[idx]  = to_index((std::int32_t) biome);
                heights[idx] = geo.GetBiomeHeight(biome, x, y, mask);
            }
        }
    }

    GenerateSectors(heights, biomes);
    LOG_INFO(AVL_LOGGER, "Generated {} biome sectors", m_sectors.size());
}

AltBiomeManager::BiomeType &AltBiomeManager::GetBiomeType(std::int32_t biome)
{
    for (auto &&t : m_biomes)
        if (t.m_biome == biome)
            return t;
    throw std::runtime_error("unknown biome");
}

void AltBiomeManager::GenerateSectors(std::vector<float> const &heights, std::vector<std::uint8_t> const &biomes)
{
    constexpr std::uint16_t NONE = 0xFFFF;
    m_pointSectors.assign(SIZE * SIZE, NONE);
    std::vector<bool> visited(SIZE * SIZE);

    auto at       = [](int x, int y) { return (std::size_t) x * SIZE + y; };
    auto add_sector = [this](std::int32_t biome) {
        m_sectors.push_back(std::make_unique<BiomeSector>(biome));
        GetBiomeType(biome).m_sectors.push_back(m_sectors.back().get());
        return (std::uint16_t) (m_sectors.size() - 1);
    };
    auto add_point = [&](std::uint16_t sector, std::int16_t x, std::int16_t y) {
        auto &&type = GetBiomeType(m_sectors[sector]->m_biome);
        type.m_allPoints.push_back({x, y});
        if (heights[at(x, y)] >= 30.0f)
            type.m_allPointsAboveSeaLevel.push_back({x, y});
    };

    // AshLands, DeepNorth and Ocean are one sector each
    std::uint16_t fixed[10];
    std::fill(std::begin(fixed), std::end(fixed), NONE);
    fixed[6] = add_sector((std::int32_t) VUtils::Biome::AshLands);
    fixed[7] = add_sector((std::int32_t) VUtils::Biome::DeepNorth);
    fixed[8] = add_sector((std::int32_t) VUtils::Biome::Ocean);
    for (std::int16_t y = 0; y < SIZE; y++) {
        for (std::int16_t x = 0; x < SIZE; x++) {
            auto s = fixed[biomes[at(x, y)]];
            if (s != NONE) {
                m_pointSectors[at(x, y)] = s;
                visited[at(x, y)]        = true;
                add_point(s, x, y);
            }
        }
    }

    // flood fill the rest, LIFO like vanilla Stack<>
    std::stack<Point, std::vector<Point>> open;
    auto try_fill = [&](std::uint8_t biome, std::uint16_t sector, std::int16_t x, std::int16_t y) {
        if (x >= 0 && y >= 0 && x < SIZE && y < SIZE && !visited[at(x, y)] && biomes[at(x, y)] == biome) {
            visited[at(x, y)]        = true;
            m_pointSectors[at(x, y)] = sector;
            add_point(sector, x, y);
            open.push({x, y});
        }
    };
    for (std::int16_t y = 0; y < SIZE; y++) {
        for (std::int16_t x = 0; x < SIZE; x++) {
            if (visited[at(x, y)])
                continue;
            visited[at(x, y)]        = true;
            auto sector              = add_sector(INDEX_TO_BIOME[biomes[at(x, y)]]);
            m_pointSectors[at(x, y)] = sector;
            open.push({x, y});
            while (!open.empty()) {
                auto p = open.top();
                open.pop();
                auto biome = biomes[at(p.x, p.y)];
                auto s     = m_pointSectors[at(p.x, p.y)];
                try_fill(biome, s, (std::int16_t) (p.x + 1), p.y);
                try_fill(biome, s, (std::int16_t) (p.x - 1), p.y);
                try_fill(biome, s, p.x, (std::int16_t) (p.y + 1));
                try_fill(biome, s, p.x, (std::int16_t) (p.y - 1));
            }
        }
    }

    // edges, neighbors, bounds and heights
    for (int i = 1; i < SIZE - 1; i++) {
        for (int j = 1; j < SIZE - 1; j++) {
            float h     = heights[at(j, i)];
            auto &&self = m_pointSectors[at(j, i)];
            std::uint16_t item;
            if ((item = m_pointSectors[at(j - 1, i)]) != self || (item = m_pointSectors[at(j + 1, i)]) != self
                || (item = m_pointSectors[at(j, i - 1)]) != self || (item = m_pointSectors[at(j, i + 1)]) != self) {
                auto &&s = *m_sectors[self];
                s.m_edgeCount++;
                s.m_center += Vector2f((float) j, (float) i);
                if ((float) j < s.m_min.x)
                    s.m_min.x = (float) j;
                if ((float) j > s.m_max.x)
                    s.m_max.x = (float) j;
                if ((float) i < s.m_min.y)
                    s.m_min.y = (float) i;
                if ((float) i > s.m_max.y)
                    s.m_max.y = (float) i;
                auto *n = m_sectors[item].get();
                if (std::find(s.m_neighbors.begin(), s.m_neighbors.end(), n) == s.m_neighbors.end())
                    s.m_neighbors.push_back(n);
                if (h < s.m_heightMin)
                    s.m_heightMin = h;
                if (h > s.m_heightMax)
                    s.m_heightMax = h;
            }
        }
    }

    for (auto &&type : m_biomes) {
        for (auto *s : type.m_sectors) {
            if (s->m_edgeCount > 0) {
                s->m_center = Vector2f(MapSpaceToWorldSpace(s->m_center.x / (float) s->m_edgeCount),
                                       MapSpaceToWorldSpace(s->m_center.y / (float) s->m_edgeCount));
                s->m_min    = Vector2f(MapSpaceToWorldSpace(s->m_min.x), MapSpaceToWorldSpace(s->m_min.y));
                s->m_max    = Vector2f(MapSpaceToWorldSpace(s->m_max.x), MapSpaceToWorldSpace(s->m_max.y));
                // vanilla builds both from Min with z = 0 (new Vector3(x, y))
                s->m_minZone   = Vector2s(zone_coord(s->m_min.x), zone_coord(0.0f));
                s->m_maxZone   = s->m_minZone;
                s->m_heightAvg = (s->m_heightMin + s->m_heightMax) / 2.0f;
            }
        }
    }

    // IsDiscovered never triggers in vanilla (MinZone == MaxZone), skipped

    for (auto &&type : m_biomes) {
        for (auto *s : type.m_sectors) {
            float d2               = s->m_center.x * s->m_center.x + s->m_center.y * s->m_center.y;
            s->m_distanceFromCenter = (float) std::sqrt((double) d2);
        }
    }

    GenerateAltBiomes();
}

void AltBiomeManager::GenerateAltBiomes()
{
    std::int32_t seed = WorldManager::instance().GetWorld()->m_seed;
    std::vector<AltBiome *> valid;

    for (auto &&type : m_biomes) {
        valid.clear();
        for (auto &&alt : m_altBiomes)
            if (alt.m_enabled && has_flag(alt.m_biome, type.m_biome))
                valid.push_back(&alt);

        for (auto *alt : valid) {
            // C# int math wraps
            auto stateSeed = (std::int32_t) ((std::uint32_t) type.m_biome
                                             + (std::uint32_t) avledet::util::get_stable_hash(alt->m_name)
                                             + (std::uint32_t) seed);
            VUtils::Random::State random(stateSeed);

            // Utils.Shuffle, the shuffled order persists for the next modifier
            auto &&list = type.m_sectors;
            for (int k = (int) list.size() - 1; k > 0; k--) {
                int index = random.range(0, k + 1);
                std::swap(list[k], list[index]);
            }

            for (auto *sector : list) {
                if ((std::int32_t) alt->m_sectors.size() < alt->m_maxAmountSpawned) {
                    float r = random.range(0.0f, 1.0f);
                    if (((std::int32_t) alt->m_sectors.size() < alt->m_minAmountSpawned || alt->m_chance >= r)
                        && sector->CanAddModifier(*alt)) {
                        alt->m_sectors.push_back(sector);
                        sector->m_altBiomes.push_back(alt);
                    }
                }
            }
        }
    }

    for (auto &&alt : m_altBiomes) {
        LOG_INFO(AVL_LOGGER, "Placed {}/{}-{} of alt biome '{}'", (std::size_t) alt.m_sectors.size(), alt.m_minAmountSpawned,
                 alt.m_maxAmountSpawned, alt.m_name);
    }
}

BiomeSector const &AltBiomeManager::GetBiomeSector(int gridx, int gridy) const
{
    gridx = std::clamp(gridx, 0, SIZE - 1);
    gridy = std::clamp(gridy, 0, SIZE - 1);
    return *m_sectors[m_pointSectors[(std::size_t) gridx * SIZE + gridy]];
}

BiomeSector const &AltBiomeManager::GetBiomeSector(float wx, float wy) const
{
    return GetBiomeSector(WorldSpaceToMapSpace(wx), WorldSpaceToMapSpace(wy));
}

AltBiomeManager::LocationMeta const *AltBiomeManager::GetLocationMeta(std::string const &prefab) const
{
    for (auto &&[name, meta] : m_locationMeta)
        if (name == prefab)
            return &meta;
    return nullptr;
}

// Vanilla quirks kept: Range(0, num - 1) never picks the last biome, Plains
// returns BlackForest and the Ocean branch tests the Meadows flag.
std::int32_t AltBiomeManager::RandomBiomeFromBiomes(VUtils::Random::State &random, std::int32_t biome)
{
    if ((biome & (biome - 1)) == 0)
        return biome;

    constexpr std::int32_t Meadows = 1, Swamp = 2, Mountain = 4, BlackForest = 8, Plains = 16, AshLands = 32,
                           DeepNorth = 64, Ocean = 256, Mistlands = 512;
    int num = 0;
    for (auto b : {Meadows, Swamp, Mountain, BlackForest, Plains, AshLands, DeepNorth, Ocean, Mistlands})
        if (biome & b)
            num++;

    int num2 = random.range(0, num - 1);
    if ((biome & Meadows) && num2-- == 0)
        return Meadows;
    if ((biome & Swamp) && num2-- == 0)
        return Swamp;
    if ((biome & Mountain) && num2-- == 0)
        return Mountain;
    if ((biome & BlackForest) && num2-- == 0)
        return BlackForest;
    if ((biome & Plains) && num2-- == 0)
        return BlackForest;
    if ((biome & AshLands) && num2-- == 0)
        return AshLands;
    if ((biome & DeepNorth) && num2-- == 0)
        return DeepNorth;
    if ((biome & Meadows) && num2-- == 0)
        return Ocean;
    return Mistlands;
}

AltBiomeManager::Point AltBiomeManager::GetRandomPointByBiomes(VUtils::Random::State &random, std::int32_t biome)
{
    auto &&points = GetBiomeType(RandomBiomeFromBiomes(random, biome)).m_allPoints;
    if (points.empty())
        throw std::runtime_error("biome has no grid points");
    return points[random.range(0, (std::int32_t) points.size())];
}

AltBiomeManager::Point AltBiomeManager::GetRandomPointByBiomesAboveSeaLevel(VUtils::Random::State &random,
                                                                           std::int32_t biome)
{
    auto &&type = GetBiomeType(RandomBiomeFromBiomes(random, biome));
    if (type.m_allPointsAboveSeaLevel.empty()) {
        if (type.m_allPoints.empty())
            throw std::runtime_error("biome has no grid points");
        return type.m_allPoints[random.range(0, (std::int32_t) type.m_allPoints.size())];
    }
    return type.m_allPointsAboveSeaLevel[random.range(0, (std::int32_t) type.m_allPointsAboveSeaLevel.size())];
}

AltBiome const *AltBiomeManager::FindAltBiome(std::string_view name) const
{
    for (auto &&a : m_altBiomes)
        if (a.m_name == name)
            return &a;
    return nullptr;
}

#endif
