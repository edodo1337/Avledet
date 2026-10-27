#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "DataStream.h"
#include "Quaternion.h"
#include "Vector.h"

// Colliders of the zone being generated, for the vegetation raycasts straight down
// (vanilla ZoneSystem.IsBlocked and GetStaticSolidHeight through Unity Physics).
namespace avledet::phys {

    enum Layer : std::uint8_t
    {
        BLOCK        = 1,// Default, static_solid, Default_small, piece
        STATIC_SOLID = 2,
    };

    // Collider of a template (location, room, vegetation prefab), pose local to its root
    struct Collider
    {
        enum class Type : std::uint8_t
        {
            Box,
            Sphere,
            Capsule,
            Mesh,
            Convex,
        };

        Type m_type;
        std::uint8_t m_layers;
        Vector3f m_pos;
        Quaternion m_rot;
        Vector3f m_scale;
        Vector3f m_center;
        Vector3f m_size;// box
        float m_radius           = 0;
        float m_height           = 0;
        std::int32_t m_direction = 1;
        std::int32_t m_mesh      = -1;

        static Collider read(util::Reader &reader);
    };

    struct Hit
    {
        float m_y;
        Vector3f m_normal;
    };

    class World
    {
      public:
        static World &instance();

        void LoadMeshes();

        // drop everything, the zone keeps its own colliders only
        void Reset(Vector3f zoneCenter);

        // collider of a template placed at pos/rot with a uniform scale
        void Add(Collider const &c, Vector3f pos, Quaternion rot, float scale);

        // first hit going down from origin within maxDist (origin inside a solid shape does not hit it)
        std::optional<Hit> RaycastDown(Vector3f origin, float maxDist, std::uint8_t layers) const;

      private:
        struct Mesh
        {
            std::vector<Vector3f> m_verts;
            std::vector<std::int32_t> m_tris;
            std::vector<std::int32_t> m_hull;// convex hull triangles, built on first use
            bool m_hullDone = false;
        };

        // Vertices are relative to the shape position: PhysX works in shape space, world floats
        // far from the origin would lose millimeters.
        struct Tri
        {
            Vector3f a, b, c;
            std::uint32_t m_shape;
        };

        struct Shape
        {
            Vector3f m_pos;
            std::uint8_t m_layers;
        };

        struct Capsule
        {
            Vector3f a, b;// segment relative to the shape, a == b for a sphere
            float r;
            std::uint32_t m_shape;
        };

        // 1 m cells around the zone center: with groupRadius above 32 (Ashlands, up to 80) vegetation
        // groups reach ~128 m from the center
        static constexpr int CELLS = 260;

        std::vector<Mesh> m_meshes;
        std::vector<Shape> m_shapes;
        std::vector<Tri> m_tris;
        std::vector<Capsule> m_capsules;
        std::vector<std::vector<std::uint32_t>> m_cells = std::vector<std::vector<std::uint32_t>>(CELLS * CELLS);
        float m_minX = 0, m_minZ = 0;

        std::vector<std::int32_t> const &Hull(std::int32_t mesh);
        // only front faces looking up are kept, a ray going down hits nothing else
        void AddTri(Vector3f a, Vector3f b, Vector3f c);
        void AddSolid(std::vector<Vector3f> const &verts, std::vector<std::int32_t> const &tris);
    };

}// namespace avledet::phys
