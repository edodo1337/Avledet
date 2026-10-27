#include <condition_variable>
#include <cstddef>
#include <iterator>
#include <quill/LogMacros.h>
#include <quill/sinks/ConsoleSink.h>

#include "Avledet.h"
#include "Types.h"
#include "ZoneManager.h"
#include "HeightmapBuilder.h"

#if AVL_IS_ON(AVL_ZONE_GENERATION)

    #include <future>
    #include <mutex>

    #include "AltBiomes.h"
    #include "GeoManager.h"
    #include "TerrainModifier.h"
    #include "VUtilsMathf.h"
    #include "VUtilsMath.h"

// public
void HMBuildManager::PostGeoInit()
{
    //int TC = std::max(1, (int)std::thread::hardware_concurrency() - 2);

    LOG_NOTICE(AVL_LOGGER, "Initializing HeightmapBuilder");

    for (unsigned int i = 0; i < AVL_CONFIG.m_world_heightmap_threads; i++) {
        auto &&insert = m_builders.insert(std::end(m_builders), std::make_unique<Shared>());

        Shared *shared = insert->get();

        shared->m_thread = std::jthread([this, i, shared](std::stop_token token) {
            std::string name = "HMBuilder" + std::to_string(i);

            tracy::SetThreadName(name.c_str());
            //el::Helpers::setThreadName(name);

            std::vector<ZoneID> next;

            std::vector<std::unique_ptr<Heightmap>> baked;


            LOG_DEBUG(AVL_LOGGER, "Builder thread started");
            while (!token.stop_requested()) {
                // Reassign pending heightmaps
                if (next.empty()) {
                    std::scoped_lock<std::mutex> scoped(shared->m_mux);
                    next = std::move(shared->m_waiting);
                }

                //sizeof(std::condition_variable); = 48

                baked.clear();

                // Bake any pending heightmaps (FILO)
                for (auto it = next.rbegin(); it != next.rend(); ++it) {
                    const auto& zone = *it;

                    auto base(std::make_unique<BaseHeightmap>());
                    Build(base.get(), zone);
                    baked.push_back(std::make_unique<Heightmap>(zone, std::move(base)));
                    //if (baked.size() > next.size() / 10)
                    if (baked.size() > 10)
                        break;

                    // early stop
                    if (token.stop_requested()) {
                        return;
                    }
                }

                // remove last x (10) newly baked elements from next
                next.resize(next.size() - baked.size());

                // Add to the pool of ready heightmaps
                {
                    std::scoped_lock<std::mutex> scoped(m_mux);
                    for (auto &&heightmap : baked) {
                        m_ready[heightmap->get_zone()] = std::move(heightmap);
                    }
                }

                std::this_thread::sleep_for(1ms);
            }
        });
    }

    m_nextBuilder = m_builders.begin();
}

void HMBuildManager::Uninit()
{
    // First request all to stop
    for (auto &&shared : m_builders) {
        shared->m_thread.request_stop();
    }

    // Then join each
    for (auto &&builder : m_builders) {
        if (builder->m_thread.joinable())
            builder->m_thread.join();
    }
}

void HMBuildManager::Update()
{
    // drop what nobody took for a minute; m_building forgets it too, so a later poll builds it again
    // (left there, the zone was never built nor generated until a restart)
    if (VUtils::run_periodic<struct clear_heightmaps>(1min)) {
        std::scoped_lock<std::mutex> scoped(m_mux);
        for (auto &&[zone, heightmap] : m_ready)
            m_building.erase(zone);
        m_ready.clear();
    }
}

// private
void HMBuildManager::Build(BaseHeightmap *base, ZoneID zone)
{
    auto baseWorldPos = ZoneManager::ZoneToWorldPos(zone)
                        + Vector3f((float) ZoneManager::UNITS_PER_ZONE * -0.5f, 0.,
                                   (float) ZoneManager::UNITS_PER_ZONE * -0.5f);

    auto &&GEO = GeoManager::instance();

    // vanilla 1.0: corner biomes (HaveBiome, Heightmap.GetBiome) come from the biome sector grid,
    // the heights still blend the WorldGenerator.GetBiome corners
    float const x0 = baseWorldPos.x, z0 = baseWorldPos.z;
    float const x1 = (float)((double)x0 + (double)ZoneManager::UNITS_PER_ZONE);
    float const z1 = (float)((double)z0 + (double)ZoneManager::UNITS_PER_ZONE);

    auto &&alt = AltBiomeManager::instance();
    auto sector = [&](float x, float z) { return (avledet::util::Biome) alt.GetBiomeSector(x, z).m_biome; };
    base->m_cornerBiomes[0] = sector(x0, z0);
    base->m_cornerBiomes[1] = sector(x1, z0);
    base->m_cornerBiomes[2] = sector(x0, z1);
    base->m_cornerBiomes[3] = sector(x1, z1);

    auto const biome1 = GEO.GetBiome(x0, z0, 0.02f, false);
    auto const biome2 = GEO.GetBiome(x1, z0, 0.02f, false);
    auto const biome3 = GEO.GetBiome(x0, z1, 0.02f, false);
    auto const biome4 = GEO.GetBiome(x1, z1, 0.02f, false);

    base->m_baseHeights.resize(Heightmap::E_WIDTH * Heightmap::E_WIDTH);
    base->m_base_mask.resize(Heightmap::E_WIDTH * Heightmap::E_WIDTH);

    for (int ry = 0; ry < Heightmap::E_WIDTH; ry++) {
        float const world_y = (float)((double)baseWorldPos.z + (double)ry);
        float const ty      = VUtils::Math::SmoothStep(0.0f, 1.0f, (float)((double)ry / (double)ZoneManager::UNITS_PER_ZONE));

        for (int rx = 0; rx < Heightmap::E_WIDTH; rx++) {
            float const world_x = (float)((double)baseWorldPos.x + (float)rx);
            float const tx      = VUtils::Math::SmoothStep(0.0f, 1.0f, (float)((double)rx / (double)ZoneManager::UNITS_PER_ZONE));

            auto colorMask = avledet::util::Colors::BLACK;
            float height;

            assert(tx >= 0 && tx <= 1);
            assert(ty >= 0 && ty <= 1);

            // slight optimization case
            if (biome1 == biome2 && biome1 == biome3 && biome1 == biome4) {
                height = GEO.GetBiomeHeight(biome1, world_x, world_y, colorMask, false);
            } else {
                std::array<VUtils::Color, 4> array;
                float biomeHeight = GEO.GetBiomeHeight(biome1, world_x, world_y, array[0], false);
                float biomeHeight2 = GEO.GetBiomeHeight(biome2, world_x, world_y, array[1], false);
                float biomeHeight3 = GEO.GetBiomeHeight(biome3, world_x, world_y, array[2], false);
                float biomeHeight4 = GEO.GetBiomeHeight(biome4, world_x, world_y, array[3], false);
                
                float num9 = VUtils::Math::Lerp(biomeHeight, biomeHeight2, tx);
                float num10 = VUtils::Math::Lerp(biomeHeight3, biomeHeight4, tx);
                height = VUtils::Math::Lerp(num9, num10, ty);

                VUtils::Color color2 = avledet::util::Color::Lerp(array[0], array[1], tx);
                VUtils::Color color3 = avledet::util::Color::Lerp(array[2], array[3], tx);
                colorMask = avledet::util::Color::Lerp(color2, color3, ty);
            }

            base->m_baseHeights[(std::size_t)(ry * Heightmap::E_WIDTH + rx)] = height;

            // vanilla keeps the mask in an RGBA32 texture of the same 65x65 size, so 8 bit per channel
            auto q = [](float v) { return std::round(std::clamp(v, 0.f, 1.f) * 255.f) / 255.f; };
            base->m_base_mask[(std::size_t)(ry * Heightmap::E_WIDTH + rx)]
                    = avledet::util::Color(q(colorMask.r), q(colorMask.g), q(colorMask.b), q(colorMask.a));
        }
    }
}

/*
// public
std::unique_ptr<HMBuildData> HMBuildManager::RequestTerrainBlocking(const ZoneID& zone) {
    std::unique_ptr<HMBuildData> hmbuildData;
    do {
        hmbuildData = RequestTerrain(zone);
        if (!hmbuildData) std::this_thread::sleep_for(1ms); // sleep instead of spinlock
    } while (!hmbuildData);
    return hmbuildData;
}*/

/*
void HMBuildManager::QueueBatch(const ZoneID& zone) {
    for (auto&& shared : m_builders) {
        std::scoped_lock<std::mutex> scoped(shared.m_mux);

        for (int i=0; i < )
        shared.m_waiting.push_back
    }
}*/

std::unique_ptr<Heightmap> HMBuildManager::PollHeightmap(ZoneID zone)
{
    {
        std::unique_ptr<Heightmap> result;
        {
            // TODO use shared if possible
            std::scoped_lock<std::mutex> scoped(m_mux);
            auto &&find = m_ready.find(zone);
            if (find != m_ready.end()) {
                result = std::move(find->second);
                m_ready.erase(find);
            }
        }

        // TODO should a mutex be used here?
        if (result) {
            m_building.erase(zone);
            return result;
        }
    }

    auto &&insert = m_building.insert(zone);
    if (insert.second) {
        // we attach tasks to each thread in round robin for now
        if (m_nextBuilder == m_builders.end()) {
            m_nextBuilder = m_builders.begin();
        }

        // Give the next builder a job
        //  Ideally, the builder with the least work should be doing this job...
        //  solve problems first
        {
            std::scoped_lock<std::mutex> scoped((*m_nextBuilder)->m_mux);

            (*m_nextBuilder)->m_waiting.push_back(zone);
        }

        ++m_nextBuilder;
    } else {
        // prioritize the element, because it was obviously called on more than once
        //  (hint: We already polled for the element... we determined it was missing... so we added it to the queue... we came back later... and its still in the awaiting to build queue... Now, we expedite the process)

    }

    return nullptr;
}

// public
// This is never externally wtf?
// This entire multithreaded (single thread really) chunkbuilder is not even used
// for its intended purpose
/*
std::unique_ptr<HMBuildData> HMBuildManager::RequestTerrain(const ZoneID& zone) {
    std::scoped_lock<std::mutex> scoped(m_lock);
    auto&& find = m_ready.find(zone);
    if (find != m_ready.end()) {
        std::unique_ptr<HMBuildData> data = std::move(find->second);
        m_ready.erase(find);
        return data;
    }

    // Will not insert if absent, which is intended
    m_toBuild.insert(zone);
        
    return nullptr;
}*/

/*
// public
bool HMBuildManager::IsTerrainReady(const ZoneID& zone) {
    std::scoped_lock<std::mutex> scoped(m_lock);

    if (m_ready.contains(zone))
        return true;

    // Will not insert if absent, which is intended
    m_toBuild.insert(zone);

    return false;
}
*/
#endif