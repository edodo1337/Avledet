#include "HeightmapManager.h"

#if AVL_IS_ON(AVL_ZONE_GENERATION)
    #include "HeightmapBuilder.h"
    #include "ZoneManager.h"
    #include <algorithm>

//static std::vector<float>
// only ever used locally
//static float[] tempBiomeWeights = new float[513]; // redundant; why not use a much smaller array?

// only ever used locally
//static std::vector<Heightmap> tempHmaps; // use map instead?

// public static
void HeightmapManager::ForceQueuedRegeneration()
{
    assert(false);
    //for (auto&& pair : m_heightmaps) {
    //    auto&& heightmap = pair.second;
    //    if (heightmap->IsRegenerateQueued()) {
    //        //LOG(INFO) << "Force generating hmap " << heightmap->m_zone.x << " " << heightmap->m_zone.y;
    //        ////LOG(INFO) << "Force generating hmap " << heightmap.transform.position.ToString();
    //        heightmap->Regenerate();
    //    }
    //}
}

// public static
float HeightmapManager::GetOceanDepthAll(Vector3f worldPos)
{
    auto &&heightmap = GetHeightmap(worldPos);
    return heightmap.GetOceanDepth(worldPos);
}

// public static
bool HeightmapManager::AtMaxLevelDepth(Vector3f worldPos)
{
    auto &&heightmap = GetHeightmap(worldPos);
    return heightmap.AtMaxWorldLevelDepth(worldPos);
}

/*
Vector3f HeightmapManager::GetNormal(const Vector3f& pos) {

}*/

/*
// public static
bool HeightmapManager::GetHeight(const Vector3f& worldPos, float& height) {
    auto&& heightmap = GetHeightmap(worldPos);
    if (heightmap.GetWorldHeight(worldPos, height)) {
        return true;
    }
    height = 0;
    return false;
}

float HeightmapManager::GetHeight(const Vector3f& worldPos) {
    auto&& heightmap = GetHeightmap(ZoneManager::WorldToZonePos(worldPos));

    float height = 0;
    if (heightmap.GetWorldHeight(worldPos, height)) {
        return height;
    }

    assert(false);

    //throw std::runtime_error("Unexpected: Failed to get guaranteed heightmap at position (should not see this)");
    //LOG(ERROR) << "Failed to get guaranteed heightmap at position";
    exit(0);
}
*/

/*
// public static
bool HeightmapManager::GetAverageHeight(const Vector3f& worldPos, float radius, float &height) {
    auto heightmaps = GetHeightmaps(worldPos, radius);

    float num = 0;
    std::int32_t num2 = 0;

    for (auto&& heightmap : heightmaps) {
        float num3;
        if (heightmap.GetAverageWorldHeight(worldPos, radius, num3)) {
            num += num3;
            num2++;
        }
    }

    if (num2 > 0) {
        height = num / (float)num2;
        return true;
    }
    height = 0;
    return false;
}
*/

// public static
avledet::util::Map<ZoneID, std::unique_ptr<Heightmap>> &HeightmapManager::GetAllHeightmaps()
{
    return m_heightmaps;
}

/*
Heightmap* HeightmapManager::GetOrCreateHeightmap(const Vector2i& zoneID) {
    //for (auto&& pair : m_heightmaps) {
    //    auto&& heightmap = pair.second;
    //    if (heightmap->IsPointInside(point, 0)) {
    //        return heightmap.get();
    //    }
    //}

    throw std::runtime_error("do not use");

    ////auto&& pair = m_heightmaps.insert({ zoneID, nullptr });
    ////if (pair.second) { // if newly inserted
    ////    pair.first->second = std::make_unique<Heightmap>(zoneID);
    ////    pair.first->second->Regenerate();
    ////}
    ////
    ////assert(pair.first->second);
    ////
    ////return pair.first->second.get();
    
    //auto&& find = m_heightmaps.find(zone);
    //if (find != m_heightmaps.end()) {
    //    return find->second.get();
    //}
    //
    //return m_heightmaps.insert()
    //
    //auto h(new Heightmap(zone));
    //m_heightmaps[zone] = std::unique_ptr<Heightmap>(h);
    //return h;
    //
    //auto heightmap = GetHeightmap(point);
    //if (heightmap)
    //    return heightmap;
    //
    //return CreateHeightmap(ZoneManager::WorldToZonePos(point));
}*/

void HeightmapManager::Regenerate(Heightmap &heightmap)
{
    std::vector<TerrainMod const *> mods;
    for (auto &&mod : m_mods)
        if (heightmap.TerrainVSModifier(mod))
            mods.push_back(&mod);
    std::sort(mods.begin(), mods.end(), [](auto a, auto b) { return TerrainMod::Less(*a, *b); });
    heightmap.Regenerate(mods);
}

// a heightmap from the builder thread: apply the live modifiers like vanilla Heightmap.Generate
Heightmap *HeightmapManager::Adopt(ZoneID zone, std::unique_ptr<Heightmap> heightmap)
{
    if (!heightmap)
        return nullptr;
    for (auto &&mod : m_mods) {
        if (heightmap->TerrainVSModifier(mod)) {
            Regenerate(*heightmap);
            break;
        }
    }
    return (m_heightmaps[zone] = std::move(heightmap)).get();
}

void HeightmapManager::AddModifier(TerrainMod mod, bool created)
{
    if (created)
        mod.m_created = ++m_modCounter;
    m_mods.push_back(mod);
    float r   = mod.GetRadius() + 4.f;
    auto from = ZoneManager::WorldToZonePos(mod.m_pos - Vector3f(r, 0, r));
    auto to   = ZoneManager::WorldToZonePos(mod.m_pos + Vector3f(r, 0, r));
    for (int y = from.y; y <= to.y; y++) {
        for (int x = from.x; x <= to.x; x++) {
            auto find = m_heightmaps.find(ZoneID((std::int16_t) x, (std::int16_t) y));
            if (find != m_heightmaps.end() && find->second && find->second->TerrainVSModifier(mod))
                find->second->m_queued = true;
        }
    }
}

void HeightmapManager::ForceGenerateAll()
{
    for (auto &&[zone, heightmap] : m_heightmaps)
        if (heightmap && heightmap->m_queued)
            Regenerate(*heightmap);
}

void HeightmapManager::ClearModifiers()
{
    for (auto &&[zone, heightmap] : m_heightmaps)
        if (heightmap && std::ranges::any_of(m_mods, [&](auto &&mod) { return heightmap->TerrainVSModifier(mod); }))
            heightmap->Regenerate({});
    m_mods.clear();
}

Heightmap *HeightmapManager::PollHeightmap(ZoneID zone)
{
    auto find = m_heightmaps.find(zone);
    if (find != m_heightmaps.end() && find->second)
        return find->second.get();
    return Adopt(zone, HMBuildManager::instance().PollHeightmap(zone));
}

// public static
Heightmap &HeightmapManager::GetHeightmap(Vector3f point)
{
    return GetHeightmap(ZoneManager::WorldToZonePos(point));
}

Heightmap &HeightmapManager::GetHeightmap(ZoneID zone)
{
    auto find = m_heightmaps.find(zone);
    if (find != m_heightmaps.end() && find->second)
        return *find->second;

    std::unique_ptr<Heightmap> heightmap;
    while (!(heightmap = HMBuildManager::instance().PollHeightmap(zone)))
        std::this_thread::sleep_for(1ms);
    return *Adopt(zone, std::move(heightmap));
}

// public static
std::vector<Heightmap *> HeightmapManager::GetHeightmaps(Vector3f point, float radius)
{
    throw std::runtime_error("not implemented");
    std::vector<Heightmap *> heightmaps;
    for (auto &&pair : m_heightmaps) {
        auto &&heightmap = pair.second;
        if (heightmap->IsPointInside(point, radius)) {
            heightmaps.push_back(heightmap.get());
        }
    }
    return heightmaps;
}

/*
// public static
avledet::util::Biome HeightmapManager::FindBiome(const Vector3f& point) {
    auto &&heightmap = GetHeightmap(point);
    if (heightmap) {
        return heightmap->GetBiome(point);
    }
    return avledet::util::Biome::None;
}*/

// public static
bool HeightmapManager::IsRegenerateQueued(Vector3f point, float radius)
{
    (void) point;
    (void) radius;
    assert(false);

    return false;
    //auto heightmaps = GetHeightmaps(point, radius);
    //for (auto&& hmap : heightmaps) {
    //    if (hmap->IsRegenerateQueued())
    //        return true;
    //}
    //return false;
}

//Heightmap* HeightmapManager::CreateHeightmap(const Vector2i& zone) {
//    assert(!m_heightmaps.contains(zone) && "fix your code dummy");
//
//    auto h(new Heightmap(zone));
//    m_heightmaps[zone] = std::unique_ptr<Heightmap>(h);
//    return h;
//}
#endif