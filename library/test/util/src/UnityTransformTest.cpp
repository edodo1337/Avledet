#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "ServerOptions.h"
#include "UnityTransform.h"

// Ground truth dumped from the game (tools/avldump-server JsonDump.TransformTest/TransformSetTest), in
// AVL_TRANSFORM_TESTS. The dump prints -0 as 0, so zeros compare without their sign.
namespace {
    nlohmann::json Load(char const *file)
    {
        auto dir = std::getenv("AVL_TRANSFORM_TESTS");
        if (!dir || !std::filesystem::exists(std::filesystem::path(dir) / file))
            return {};
        std::ifstream in(std::filesystem::path(dir) / file);
        return nlohmann::json::parse(in);
    }

    struct Row
    {
        nlohmann::json const &r;
        int i = 0;
        float f() { return r[i++].get<float>(); }
        Vector3f v() { float x = f(), y = f(), z = f(); return Vector3f(x, y, z); }
        Quaternion q() { float x = f(), y = f(), z = f(), w = f(); return Quaternion(x, y, z, w); }
    };

    bool Same(float a, float b) { return std::bit_cast<std::uint32_t>(a + 0.0f) == std::bit_cast<std::uint32_t>(b + 0.0f); }
    bool Same(Vector3f a, Vector3f b) { return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z); }
    bool Same(Quaternion a, Quaternion b) { return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z) && Same(a.w, b.w); }
}// namespace

TEST(UnityTransform, WorldPose)
{
    auto rows = Load("transform_test.json");
    if (rows.empty())
        GTEST_SKIP() << "AVL_TRANSFORM_TESTS not set";
    using namespace avledet::unity;
    int bad = 0;
    for (auto &&r : rows) {
        Row row {r};
        auto rp = row.v(); auto rq = row.q(); float rs = row.f();
        auto cp = row.v(); auto cq = row.q(); auto cs = row.v();
        auto gp = row.v(); auto gq = row.q();
        auto rootq = row.q();
        auto cpos  = row.v();
        auto crot  = row.q();
        row.v();
        auto gpos = row.v();
        auto grot = row.q();
        Vector3f rsv(rs, rs, rs);
        auto root = Normalize(rq), child = Normalize(cq), grand = Normalize(gq);
        bad += !Same(root, rootq);
        bad += !Same(ToWorld(rp, root, rsv, cp), cpos);
        bad += !Same(ToWorld(root, rsv, child), crot);
        bad += !Same(ToWorld(rp, root, rsv, ToWorld(cp, child, cs, gp)), gpos);
        bad += !Same(ToWorld(root, rsv, ToWorld(child, cs, grand)), grot);
    }
    EXPECT_EQ(bad, 0);
}

TEST(UnityTransform, LocalPose)
{
    auto rows = Load("transform_set_test.json");
    if (rows.empty())
        GTEST_SKIP() << "AVL_TRANSFORM_TESTS not set";
    using namespace avledet::unity;
    int bad = 0;
    for (auto &&r : rows) {
        Row row {r};
        auto rp = row.v(); auto rq = row.q(); float rs = row.f();
        auto wp = row.v(); auto wq = row.q();
        row.q();
        auto lp = row.v(); auto lq = row.q();
        Vector3f rsv(rs, rs, rs);
        auto root = Normalize(rq);
        bad += !Same(ToLocal(rp, root, rsv, wp), lp);
        bad += !Same(ToLocal(root, rsv, wq), lq);
    }
    EXPECT_EQ(bad, 0);
}

TEST(UnityTransform, LookRotation)
{
    auto rows = Load("look_test.json");
    if (rows.empty())
        GTEST_SKIP() << "AVL_TRANSFORM_TESTS not set";
    int bad = 0;
    for (auto &&r : rows) {
        Row row {r};
        auto forward = row.v(); auto up = row.v(); auto q = row.q();
        bad += !Same(Quaternion::look_rotation(forward, up), q);
    }
    EXPECT_EQ(bad, 0);
}

// UnityEngine C# operators under the game's Mono (double intermediates)
TEST(UnityTransform, ManagedMath)
{
    auto rows = Load("managed_test.json");
    if (rows.empty())
        GTEST_SKIP() << "AVL_TRANSFORM_TESTS not set";
    int bad = 0;
    for (auto &&r : rows) {
        Row row {r};
        auto q = row.q(); auto rq = row.q(); auto a = row.v(); auto b = row.v();
        auto qa = row.v(); auto qr = row.q(); row.v(); auto sum = row.v();
        bad += !Same(q * a, qa);
        bad += !Same(q * rq, qr);
        bad += !Same(b + q * a, sum);
    }
    EXPECT_EQ(bad, 0);
}

// the server browser "m" tag against vanilla ServerOptionsGUI + StringUtils (constants.json)
TEST(UnityTransform, ModifiersTag)
{
    auto data = Load("constants.json");
    if (data.empty())
        GTEST_SKIP() << "AVL_TRANSFORM_TESTS not set";
    int bad = 0;
    for (auto &&c : data.at("modifierTags")) {
        auto keys = c.at("keys").get<std::vector<std::string>>();
        auto tag  = ServerOptions::ModifiersTag(keys);
        bool same = c.at("m").is_null() ? !tag : (tag && *tag == c.at("m").get<std::string>());
        if (!same)
            std::printf("keys %s: %s, vanilla %s\n", c.at("keys").dump().c_str(), tag ? tag->c_str() : "none",
                        c.at("m").dump().c_str());
        bad += !same;
        bad += ServerOptions::SteamTags(keys) != c.at("tags").get<std::string>();
    }
    EXPECT_EQ(bad, 0);
}
