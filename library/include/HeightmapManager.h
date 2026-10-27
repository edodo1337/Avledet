#pragma once

#include "HeightMap.h"
#include "Manager.h"
#include "TerrainModifier.h"
#include "Types.h"
#include "VUtils.h"

#if AVL_IS_ON(AVL_ZONE_GENERATION)

class HeightmapManager : public avledet::util::IManager<HeightmapManager>
{
    //avledet::util::Set<ZoneID> m_population;
    avledet::util::Map<ZoneID, std::unique_ptr<Heightmap>> m_heightmaps;

    // live terrain modifiers (vanilla TerrainModifier instances), applied on regeneration
    std::vector<TerrainMod> m_mods;
    std::int64_t m_modCounter = 0;

    void Regenerate(Heightmap &heightmap);
    Heightmap *Adopt(ZoneID zone, std::unique_ptr<Heightmap> heightmap);

  public:
    // vanilla TerrainModifier.Awake: registers and pokes the heightmaps (regenerated later).
    // Modifiers with a ZNetView sort by creation time, pass created = true for them.
    void AddModifier(TerrainMod mod, bool created);
    // vanilla Heightmap.ForceGenerateAll: regenerate the poked heightmaps
    void ForceGenerateAll();
    // a ghost zone is destroyed with its TerrainModifiers in the same frame: drop them, the
    // heightmaps they changed go back to the plain terrain
    void ClearModifiers();

    //void ForceGenerateAll();
    void ForceQueuedRegeneration();

    float GetOceanDepthAll(Vector3f worldPos);

    bool AtMaxLevelDepth(Vector3f worldPos);

    //Vector3f GetNormal(const Vector3f& pos);

    // Get the heightmap height at position
    //	Will only succeed given the heightmap exists
    // TODO make this return the height, or throw if out of bounds of entire square world
    //bool GetHeight(const Vector3f& worldPos, float& height);
    //bool GetAverageHeight(const Vector3f& worldPos, float radius, float &height);


    // Get the heightmap height at position,
    //	The heightmap will be created if it does not exist
    //float GetHeight(const Vector3f& worldPos);

    //static std::vector<Heightmap> GetAllHeightmaps();
    avledet::util::Map<ZoneID, std::unique_ptr<Heightmap>> &GetAllHeightmaps();

    //Heightmap* GetOrCreateHeightmap(const Vector2i& zoneID);

    Heightmap *PollHeightmap(ZoneID zone);

    Heightmap &GetHeightmap(Vector3f point);
    Heightmap &GetHeightmap(ZoneID zone);
    std::vector<Heightmap *> GetHeightmaps(Vector3f point, float radius);
    //avledet::util::Biome FindBiome(const Vector3f& point);

    bool IsRegenerateQueued(Vector3f point, float radius);

    //Heightmap* CreateHeightmap(const Vector2i& zone);
};

#endif// AVL_IS_ON(AVL_ZONE_GENERATION)
