#include "Physics.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "VUtilsResource.h"

namespace avledet::phys {

    Collider Collider::read(util::Reader &reader)
    {
        Collider c;
        c.m_type   = (Type) reader.read<std::uint8_t>();
        c.m_layers = reader.read<std::uint8_t>();
        c.m_pos    = reader.read<Vector3f>();
        c.m_rot    = reader.read<Quaternion>();
        c.m_scale  = reader.read<Vector3f>();
        switch (c.m_type) {
            case Type::Box:
                c.m_center = reader.read<Vector3f>();
                c.m_size   = reader.read<Vector3f>();
                break;
            case Type::Sphere:
                c.m_center = reader.read<Vector3f>();
                c.m_radius = reader.read<float>();
                break;
            case Type::Capsule:
                c.m_center    = reader.read<Vector3f>();
                c.m_radius    = reader.read<float>();
                c.m_height    = reader.read<float>();
                c.m_direction = reader.read<std::int32_t>();
                break;
            default:
                c.m_mesh = reader.read<std::int32_t>();
                break;
        }
        return c;
    }

    World &World::instance()
    {
        static World world;
        return world;
    }

    void World::LoadMeshes()
    {
        auto opt = VUtils::Resource::ReadFile<util::Bytes>("colliders.pkg");
        if (!opt)
            throw std::runtime_error("colliders.pkg missing");
        DataReader pkg(std::move(*opt));
        pkg.read<std::string_view>();// game version
        auto count = pkg.read<std::int32_t>();
        m_meshes.resize(count);
        for (auto &&mesh : m_meshes) {
            mesh.m_verts.resize(pkg.read<std::int32_t>());
            for (auto &&v : mesh.m_verts)
                v = pkg.read<Vector3f>();
            mesh.m_tris.resize(pkg.read<std::int32_t>());
            for (auto &&i : mesh.m_tris)
                i = pkg.read<std::int32_t>();
        }
        if (pkg.get_pos() != pkg.size())
            throw std::runtime_error("colliders.pkg has unread bytes, format mismatch");
    }

    void World::Reset(Vector3f zoneCenter)
    {
        m_shapes.clear();
        m_tris.clear();
        m_capsules.clear();
        for (auto &&cell : m_cells)
            cell.clear();
        m_minX = zoneCenter.x - CELLS * .5f;
        m_minZ = zoneCenter.z - CELLS * .5f;
    }

    namespace {
        struct D3
        {
            double x, y, z;
            D3 operator-(D3 o) const { return {x - o.x, y - o.y, z - o.z}; }
            D3 cross(D3 o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
            double dot(D3 o) const { return x * o.x + y * o.y + z * o.z; }
        };

        D3 d3(Vector3f v) { return {v.x, v.y, v.z}; }

        // Incremental 3D convex hull, outward triangles. Empty when the points are flat.
        std::vector<std::int32_t> ConvexHull(std::vector<Vector3f> const &pts)
        {
            std::size_t n = pts.size();
            if (n < 4)
                return {};
            std::vector<D3> p(n);
            for (std::size_t i = 0; i < n; i++)
                p[i] = d3(pts[i]);

            auto far = [&](auto dist) {
                std::size_t best = 0;
                double bestD    = -1;
                for (std::size_t i = 0; i < n; i++)
                    if (dist(p[i]) > bestD) {
                        bestD = dist(p[i]);
                        best  = i;
                    }
                return std::pair(best, bestD);
            };
            auto [i0, _] = far([](D3 v) { return -v.x; });
            auto [i1, d1] = far([&](D3 v) { return (v - p[i0]).dot(v - p[i0]); });
            auto axis    = p[i1] - p[i0];
            auto [i2, d2] = far([&](D3 v) { auto c = axis.cross(v - p[i0]); return c.dot(c); });
            auto normal  = axis.cross(p[i2] - p[i0]);
            auto [i3, d3v] = far([&](D3 v) { return std::abs(normal.dot(v - p[i0])); });
            double extent = std::sqrt(d1);
            double eps    = 1e-7 * extent;
            if (extent == 0 || std::sqrt(d2) < eps * extent || d3v / std::sqrt(normal.dot(normal)) < eps)
                return {};

            struct Face
            {
                int a, b, c;
                D3 n;
                double d;
                bool alive;
            };
            std::vector<Face> faces;
            D3 inner = {(p[i0].x + p[i1].x + p[i2].x + p[i3].x) / 4, (p[i0].y + p[i1].y + p[i2].y + p[i3].y) / 4,
                        (p[i0].z + p[i1].z + p[i2].z + p[i3].z) / 4};
            auto add = [&](int a, int b, int c) {
                D3 nn  = (p[b] - p[a]).cross(p[c] - p[a]);
                double l = std::sqrt(nn.dot(nn));
                if (l == 0)
                    return;
                nn = {nn.x / l, nn.y / l, nn.z / l};
                if (nn.dot(inner) - nn.dot(p[a]) > 0) {
                    std::swap(b, c);
                    nn = {-nn.x, -nn.y, -nn.z};
                }
                faces.push_back({a, b, c, nn, nn.dot(p[a]), true});
            };
            int t[4] = {(int) i0, (int) i1, (int) i2, (int) i3};
            add(t[0], t[1], t[2]);
            add(t[0], t[1], t[3]);
            add(t[0], t[2], t[3]);
            add(t[1], t[2], t[3]);

            for (int i = 0; i < (int) n; i++) {
                if (i == t[0] || i == t[1] || i == t[2] || i == t[3])
                    continue;
                std::map<std::pair<int, int>, int> edges;
                bool any = false;
                for (auto &&f : faces) {
                    if (!f.alive || f.n.dot(p[i]) - f.d <= eps)
                        continue;
                    any     = true;
                    f.alive = false;
                    edges[{f.a, f.b}]++;
                    edges[{f.b, f.c}]++;
                    edges[{f.c, f.a}]++;
                }
                if (!any)
                    continue;
                for (auto &&[e, count] : edges)
                    if (!edges.contains({e.second, e.first}))
                        add(e.first, e.second, i);
                std::erase_if(faces, [](Face const &f) { return !f.alive; });
            }

            std::vector<std::int32_t> tris;
            for (auto &&f : faces)
                tris.insert(tris.end(), {f.a, f.b, f.c});
            return tris;
        }
    }// namespace

    std::vector<std::int32_t> const &World::Hull(std::int32_t mesh)
    {
        auto &&m = m_meshes.at(mesh);
        if (!m.m_hullDone) {
            m.m_hull     = ConvexHull(m.m_verts);
            m.m_hullDone = true;
        }
        return m.m_hull;
    }

    void World::AddTri(Vector3f a, Vector3f b, Vector3f c)
    {
        float ny = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
        if (!(ny > 0))
            return;
        auto &&at = m_shapes.back().m_pos;
        float x0 = std::min({a.x, b.x, c.x}) + (at.x - m_minX), x1 = std::max({a.x, b.x, c.x}) + (at.x - m_minX);
        float z0 = std::min({a.z, b.z, c.z}) + (at.z - m_minZ), z1 = std::max({a.z, b.z, c.z}) + (at.z - m_minZ);
        if (x1 < 0 || z1 < 0 || x0 >= CELLS || z0 >= CELLS)
            return;
        auto index = (std::uint32_t) m_tris.size();
        m_tris.push_back({a, b, c, (std::uint32_t) m_shapes.size() - 1});
        int cx0 = std::max(0, (int) std::floor(x0)), cx1 = std::min(CELLS - 1, (int) std::floor(x1));
        int cz0 = std::max(0, (int) std::floor(z0)), cz1 = std::min(CELLS - 1, (int) std::floor(z1));
        for (int z = cz0; z <= cz1; z++)
            for (int x = cx0; x <= cx1; x++)
                m_cells[z * CELLS + x].push_back(index);
    }

    // closed shape: each triangle is turned to face away from the center
    void World::AddSolid(std::vector<Vector3f> const &verts, std::vector<std::int32_t> const &tris)
    {
        Vector3f center;
        for (auto &&v : verts)
            center += v;
        center /= (float) verts.size();
        for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
            auto a = verts[tris[i]], b = verts[tris[i + 1]], c = verts[tris[i + 2]];
            if ((b - a).cross(c - a).dot((a + b + c) / 3.f - center) < 0)
                std::swap(b, c);
            AddTri(a, b, c);
        }
    }

    void World::Add(Collider const &c, Vector3f pos, Quaternion rot, float scale)
    {
        Vector3f wp = pos + rot * (c.m_pos * scale);
        Quaternion wr = rot * c.m_rot;
        Vector3f ws = c.m_scale * scale;
        m_shapes.push_back({wp, c.m_layers});
        auto world  = [&](Vector3f v) { return wr * (v * ws); };// relative to wp
        float sx = std::abs(ws.x), sy = std::abs(ws.y), sz = std::abs(ws.z);

        switch (c.m_type) {
            case Collider::Type::Box: {
                std::vector<Vector3f> corners;
                for (int i = 0; i < 8; i++)
                    corners.push_back(world(c.m_center + Vector3f(i & 1 ? .5f : -.5f, i & 2 ? .5f : -.5f, i & 4 ? .5f : -.5f) * c.m_size));
                static std::vector<std::int32_t> const box = {0, 2, 6, 0, 6, 4, 1, 3, 7, 1, 7, 5, 0, 1, 5, 0, 5, 4,
                                                              2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 5, 7, 4, 7, 6};
                AddSolid(corners, box);
                break;
            }
            case Collider::Type::Sphere: {
                auto center = world(c.m_center);
                m_capsules.push_back({center, center, c.m_radius * std::max({sx, sy, sz}), (std::uint32_t) m_shapes.size() - 1});
                break;
            }
            case Collider::Type::Capsule: {
                float axisScale = c.m_direction == 0 ? sx : c.m_direction == 1 ? sy : sz;
                float radial    = c.m_direction == 0 ? std::max(sy, sz) : c.m_direction == 1 ? std::max(sx, sz) : std::max(sx, sy);
                float r         = c.m_radius * radial;
                float half      = std::max(c.m_height * axisScale * .5f - r, 0.f);
                Vector3f axis(c.m_direction == 0, c.m_direction == 1, c.m_direction == 2);
                auto center = world(c.m_center);
                auto dir    = wr * axis;
                m_capsules.push_back({center - dir * half, center + dir * half, r, (std::uint32_t) m_shapes.size() - 1});
                break;
            }
            case Collider::Type::Mesh: {
                auto &&mesh = m_meshes.at(c.m_mesh);
                std::vector<Vector3f> verts;
                verts.reserve(mesh.m_verts.size());
                for (auto &&v : mesh.m_verts)
                    verts.push_back(world(v));
                // Unity front faces: cross(b - a, c - a), a mirroring scale keeps them outward
                bool flip = ws.x * ws.y * ws.z < 0;
                for (std::size_t i = 0; i + 2 < mesh.m_tris.size(); i += 3) {
                    auto a = verts[mesh.m_tris[i]], b = verts[mesh.m_tris[i + 1]], cc = verts[mesh.m_tris[i + 2]];
                    if (flip)
                        std::swap(b, cc);
                    AddTri(a, b, cc);
                }
                break;
            }
            case Collider::Type::Convex: {
                auto &&hull = Hull(c.m_mesh);
                std::vector<Vector3f> verts;
                for (auto &&v : m_meshes[c.m_mesh].m_verts)
                    verts.push_back(world(v));
                AddSolid(verts, hull);
                break;
            }
        }
    }

    std::optional<Hit> World::RaycastDown(Vector3f origin, float maxDist, std::uint8_t layers) const
    {
        std::optional<Hit> best;
        double lo = (double) origin.y - maxDist;
        auto take = [&](float y, Vector3f normal) {
            if (y <= origin.y && y >= lo && (!best || y > best->m_y))
                best = Hit {y, normal};
        };

        int cx = (int) std::floor(origin.x - m_minX), cz = (int) std::floor(origin.z - m_minZ);
        if (cx >= 0 && cz >= 0 && cx < CELLS && cz < CELLS) {
            for (auto i : m_cells[cz * CELLS + cx]) {
                auto &&t     = m_tris[i];
                auto &&shape = m_shapes[t.m_shape];
                if (!(shape.m_layers & layers))
                    continue;
                // in shape space
                double x = origin.x - shape.m_pos.x, z = origin.z - shape.m_pos.z;
                D3 a = d3(t.a), b = d3(t.b), c = d3(t.c);
                // upward faces wind clockwise seen from above: every edge has the point on its right
                auto side = [&](D3 u, D3 v) { return (v.x - u.x) * (z - u.z) - (v.z - u.z) * (x - u.x); };
                if (side(a, b) > 0 || side(b, c) > 0 || side(c, a) > 0)
                    continue;
                D3 n = (b - a).cross(c - a);
                double y = a.y + (n.x * (a.x - x) + n.z * (a.z - z)) / n.y;
                double l = std::sqrt(n.dot(n));
                take(shape.m_pos.y + (float) y, Vector3f((float) (n.x / l), (float) (n.y / l), (float) (n.z / l)));
            }
        }

        for (auto &&cap : m_capsules) {
            auto &&shape = m_shapes[cap.m_shape];
            if (!(shape.m_layers & layers))
                continue;
            double x = origin.x - shape.m_pos.x, z = origin.z - shape.m_pos.z;
            D3 a = d3(cap.a), b = d3(cap.b);
            double r2 = (double) cap.r * cap.r;
            // origin inside the shape: Unity does not report it
            D3 ab = b - a, ao = D3 {x, origin.y - (double) shape.m_pos.y, z} - a;
            double t0 = ab.dot(ab) > 0 ? std::clamp(ao.dot(ab) / ab.dot(ab), 0.0, 1.0) : 0;
            D3 o  = {ao.x - ab.x * t0, ao.y - ab.y * t0, ao.z - ab.z * t0};
            if (o.dot(o) < r2)
                continue;
            // horizontal distance to the axis is quadratic in t, the top of the surface is concave in t
            double Ax = a.x - x, Az = a.z - z, Bx = ab.x, Bz = ab.z;
            double qa = Bx * Bx + Bz * Bz, qb = 2 * (Ax * Bx + Az * Bz), qc = Ax * Ax + Az * Az - r2;
            double lo_t = 0, hi_t = 1;
            if (qa < 1e-12) {
                if (qc > 0)
                    continue;
            } else {
                double disc = qb * qb - 4 * qa * qc;
                if (disc < 0)
                    continue;
                double s = std::sqrt(disc);
                lo_t = std::max(lo_t, (-qb - s) / (2 * qa));
                hi_t = std::min(hi_t, (-qb + s) / (2 * qa));
                if (lo_t > hi_t)
                    continue;
            }
            auto top = [&](double t) {
                double dx = Ax + Bx * t, dz = Az + Bz * t;
                return a.y + ab.y * t + std::sqrt(std::max(0.0, r2 - dx * dx - dz * dz));
            };
            for (int k = 0; k < 100; k++) {
                double m1 = lo_t + (hi_t - lo_t) / 3, m2 = hi_t - (hi_t - lo_t) / 3;
                if (top(m1) < top(m2))
                    lo_t = m1;
                else
                    hi_t = m2;
            }
            double t = (lo_t + hi_t) / 2, y = top(t);
            Vector3f n((float) (x - (a.x + Bx * t)), (float) (y - (a.y + ab.y * t)), (float) (z - (a.z + Bz * t)));
            take(shape.m_pos.y + (float) y, n.normal());
        }
        return best;
    }

}// namespace avledet::phys
