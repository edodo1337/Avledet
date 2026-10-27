// Worldgen probe: dumps heights, biomes and placed locations in the same layout
// as tools/worldgen-probe (vanilla side), so both can be diffed.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "CompileSettings.h"

#if AVL_IS_ON(AVL_ZONE_GENERATION)
    #include "AltBiomes.h"
    #include "GeoManager.h"
    #include "VUtilsMath.h"
    #include "ZoneManager.h"
    #include "WorldManager.h"
    #include "ZDOManager.h"
    #include <set>

static void write_json_str(std::ofstream &out, std::string_view s)
{
    out << '"';
    for (char c : s) {
        if (c == '"' || c == '\\')
            out << '\\';
        out << c;
    }
    out << '"';
}

void probe_dump(char const *dir)
{
    std::filesystem::path root(dir);
    std::filesystem::create_directories(root);

    auto &&geo   = GeoManager::instance();
    auto &&world = *WorldManager::instance().GetWorld();
    int step     = 64;
    if (auto s = std::getenv("AVL_PROBE_STEP"))
        step = std::atoi(s);

    {
        std::ofstream out(root / "world.json");
        out << "{\"seedName\":";
        write_json_str(out, world.m_seedName);
        out << ",\"seed\":" << world.m_seed << ",\"worldGen\":" << world.m_worldGenVersion << ",\"step\":" << step
            << "}";
    }

    {
        std::ofstream h(root / "samples_height.f32", std::ios::binary);
        std::ofstream b(root / "samples_biome.i32", std::ios::binary);
        for (int y = -10560; y <= 10560; y += step) {
            for (int x = -10560; x <= 10560; x += step) {
                float height  = geo.GetHeight((float) x, (float) y);
                std::int32_t biome = (std::int32_t) geo.GetBiome((float) x, (float) y, 0.02f, false);
                h.write((char const *) &height, sizeof(height));
                b.write((char const *) &biome, sizeof(biome));
            }
        }
    }

    {
        auto &&alt = AltBiomeManager::instance();
        std::ofstream s(root / "grid_sectors.i32", std::ios::binary);
        for (int i = 0; i < AltBiomeManager::SIZE; i++)
            for (int j = 0; j < AltBiomeManager::SIZE; j++) {
                std::int32_t idx = alt.m_pointSectors[(std::size_t) i * AltBiomeManager::SIZE + j];
                s.write((char const *) &idx, sizeof(idx));
            }

        std::ofstream out(root / "sectors.json");
        out.precision(9);
        out << "[";
        for (std::size_t k = 0; k < alt.m_sectors.size(); k++) {
            auto &&sec = *alt.m_sectors[k];
            if (k)
                out << ",\n";
            out << "{\"biome\":" << sec.m_biome << ",\"center\":[" << sec.m_center.x << "," << sec.m_center.y
                << "],\"edgeCount\":" << sec.m_edgeCount << ",\"heightMin\":" << sec.m_heightMin
                << ",\"heightMax\":" << sec.m_heightMax << ",\"distanceFromCenter\":" << sec.m_distanceFromCenter
                << ",\"altBiomes\":[";
            for (std::size_t a = 0; a < sec.m_altBiomes.size(); a++) {
                if (a)
                    out << ",";
                write_json_str(out, sec.m_altBiomes[a]->m_name);
            }
            out << "]}";
        }
        out << "]";
    }

    // optional list of points ("x y" per line) for detailed checks
    if (auto pointsFile = std::getenv("AVL_PROBE_POINTS")) {
        std::ifstream in(pointsFile);
        std::ofstream out(root / "points.txt");
        out.precision(9);
        float x, y;
        while (in >> x >> y) {
            out << x << " " << y << " " << geo.GetHeight(x, y) << " "
                << (std::int32_t) geo.GetBiome(x, y, 0.02f, false) << " |";
            // GetBiome inputs as raw float bits, same order as the vanilla probe
            auto perlin = [&](float off) {
                return VUtils::Math::PerlinNoise((double) ((float) ((double) off + (double) x)) * 0.0010000000474974513,
                                                 (double) ((float) ((double) off + (double) y)) * 0.0010000000474974513);
            };
            float vals[] = {geo.GetBaseHeight(x, y), VUtils::Math::Length(x, y),
                            (float) ((double) GeoManager::WorldAngle(x, y) * 100.0), perlin(geo.m_offset0),
                            perlin(geo.m_offset4), perlin(geo.m_offset1), perlin(geo.m_offset2)};
            for (float v : vals) {
                std::uint32_t bits;
                std::memcpy(&bits, &v, 4);
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%08x", bits);
                out << " " << buf << "(" << v << ")";
            }
            out << "\n";
        }
    }

    {
        std::ofstream out(root / "locations.json");
        out.precision(9);
        out << "[";
        bool first = true;
        for (auto &&[zone, inst] : ZoneManager::instance().generated_features()) {
            if (!first)
                out << ",\n";
            first = false;
            out << "{\"name\":";
            write_json_str(out, inst->m_feature.get().m_name);
            out << ",\"pos\":[" << inst->m_pos.x << "," << inst->m_pos.y << "," << inst->m_pos.z << "]}";
        }
        out << "]";
    }

    // optional zones ("x y" per line): generate them and dump their objects
    if (auto zonesFile = std::getenv("AVL_PROBE_ZONES")) {
        std::ifstream in(zonesFile);
        std::set<std::pair<int, int>> zones;
        int zx, zy;
        while (in >> zx >> zy) {
            zones.insert({zx, zy});
            ZoneManager::instance().GenerateZoneBlocking(ZoneID((std::int16_t) zx, (std::int16_t) zy));
        }

        std::ofstream out(root / "zones.txt");
        out.precision(9);
        int count = 0;
        for (auto &&zdo : ZdoManager::instance().GetZDOs()) {
            auto pos  = zdo->get_position();
            auto zone = ZoneManager::WorldToZonePos(pos);
            if (!zones.contains({zone.x, zone.y}))
                continue;
            auto rot = zdo->get_rotation();
            out << zone.x << " " << zone.y << " " << zdo->get_prefab().get().m_name << " " << pos.x << " " << pos.y
                << " " << pos.z << " " << rot.x << " " << rot.y << " " << rot.z << " " << rot.w << "\n";
            count++;
        }
        std::printf("zones dumped: %d objects in %zu zones\n", count, zones.size());
    }

    std::printf("probe written to %s\n", dir);
}
#else
void probe_dump(char const *dir)
{
    std::printf("probe needs AVL_ZONE_GENERATION\n");
}
#endif
