#pragma once

// 1.0 biome sectors and alternate biomes (AltBiomeWorldData in vanilla).
// Clients compute the same grid from the seed on connect, so this must match
// vanilla exactly, including its quirks.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CompileSettings.h"
#include "Manager.h"
#include "Types.h"
#include "Vector.h"
#include "VUtilsRandom.h"

#if AVL_IS_ON(AVL_ZONE_GENERATION)

struct BiomeSector;

struct AltBiome
{
    std::string m_name;
    bool m_enabled{};
    std::int32_t m_biome{};// biome flags this modifier can apply to

    float m_minDistanceFromCenter{};
    std::int32_t m_minAmountSpawned{};
    std::int32_t m_maxAmountSpawned{};
    float m_chance{};
    std::int32_t m_requireNeighbor{};
    std::int32_t m_notNeighbor{};
    std::vector<std::string> m_incompatibleAltBiomes;
    std::int32_t m_minEdgeSize{};
    std::int32_t m_maxEdgeSize{};
    float m_minAvgHeight{};
    float m_maxAvgHeight{};
    float m_belowWorldX{};
    float m_aboveWorldX{};
    float m_belowWorldY{};
    float m_aboveWorldY{};

    std::vector<std::string> m_addLocations;// prefab names
    std::vector<std::string> m_addVegetation;
    std::vector<std::string> m_blockLocationNames;
    std::vector<std::string> m_blockVegetationNames;

    // runtime
    std::vector<BiomeSector *> m_sectors;
};

struct BiomeSector
{
    std::int32_t m_biome{};// biome flags
    std::int32_t m_edgeCount{};
    std::vector<BiomeSector *> m_neighbors;
    Vector2f m_center{};
    Vector2f m_min{};// vanilla never lowers it below 0, kept as is
    Vector2f m_max{};
    Vector2s m_minZone{};
    Vector2s m_maxZone{};
    float m_heightMin = 99999.0f;
    float m_heightMax = -99999.0f;
    float m_heightAvg{};
    float m_distanceFromCenter{};
    std::vector<AltBiome const *> m_altBiomes;

    explicit BiomeSector(std::int32_t biome) : m_biome(biome) {}

    bool CanAddModifier(AltBiome const &modifier) const;
};

class AltBiomeManager : public avledet::util::IManager<AltBiomeManager>
{
  public:
    static constexpr int SIZE        = 2048;
    static constexpr float PIXEL     = 12.0f;
    static constexpr int HALF_WIDTH  = 1024;

    struct Point
    {
        std::int16_t x, y;
    };

    // BiomeTypeInfo
    struct BiomeType
    {
        std::int32_t m_biome{};
        std::vector<BiomeSector *> m_sectors;
        std::vector<Point> m_allPoints;
        std::vector<Point> m_allPointsAboveSeaLevel;
    };

    std::vector<AltBiome> m_altBiomes;
    std::vector<std::unique_ptr<BiomeSector>> m_sectors;
    std::vector<BiomeType> m_biomes;// same order as Enum.GetValues(Heightmap.Biome)
    std::vector<std::uint16_t> m_pointSectors;// index into m_sectors, [x * SIZE + y]

    void PostGeoInit();

    BiomeSector const &GetBiomeSector(float wx, float wy) const;
    BiomeSector const &GetBiomeSector(int gridx, int gridy) const;

    BiomeType &GetBiomeType(std::int32_t biome);

    AltBiome const *FindAltBiome(std::string_view name) const;

    // ZoneLocation fields not in features.pkg, by prefab name
    struct LocationMeta
    {
        std::string m_name;
        std::string m_altBiomeParent;
    };
    LocationMeta const *GetLocationMeta(std::string const &prefab) const;

    // used by location generation, consume the Unity random like vanilla
    Point GetRandomPointByBiomesAboveSeaLevel(VUtils::Random::State &random, std::int32_t biome);
    Point GetRandomPointByBiomes(VUtils::Random::State &random, std::int32_t biome);

    static float MapSpaceToWorldSpace(float x) { return (x - 1024.0f) * 12.0f + 6.0f; }
    static int WorldSpaceToMapSpace(float x) { return (int) ((x - 6.0f) / 12.0f + 1024.0f); }

  private:
    std::vector<std::pair<std::string, LocationMeta>> m_locationMeta;

    void LoadAltBiomes();
    static std::int32_t RandomBiomeFromBiomes(VUtils::Random::State &random, std::int32_t biome);
    void GenerateSectors(std::vector<float> const &heights, std::vector<std::uint8_t> const &biomes);
    void GenerateAltBiomes();
};

#endif
