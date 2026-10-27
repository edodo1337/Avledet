#pragma once

#include <cstdint>

#include "Vector.h"

class TerrainModifier
{

  public:
    enum class PaintType : std::uint8_t
    {
        Dirt,
        Cultivate,
        Paved,
        Reset,// use as the default for no modification
        ClearVegetation,
        DeepSnow,
    };
};

// Vanilla TerrainModifier as heightmaps apply it on regeneration (level, smooth, paint)
struct TerrainMod
{
    std::int32_t m_sortOrder = 0;
    bool m_playerModification = false;
    float m_levelOffset = 0;
    bool m_level = false;
    float m_levelRadius = 2;
    bool m_square = true;
    bool m_smooth = false;
    float m_smoothRadius = 2;
    float m_smoothPower = 3;
    bool m_paintCleared = true;
    TerrainModifier::PaintType m_paintType = TerrainModifier::PaintType::Dirt;
    float m_paintRadius = 2;
    bool m_paintHeightCheck = false;
    float m_paintStrength = 1;
    Vector3f m_pos;          // world position when live, local in templates
    std::int64_t m_created = 0;// vanilla m_creationTime, 0 without a ZNetView

    float GetRadius() const
    {
        float num = 0;
        if (m_level && m_levelRadius > num)
            num = m_levelRadius;
        if (m_smooth && m_smoothRadius > num)
            num = m_smoothRadius;
        if (m_paintCleared && m_paintRadius > num)
            num = m_paintRadius;
        return num;
    }

    // vanilla SortByModifiers
    static bool Less(TerrainMod const &a, TerrainMod const &b)
    {
        if (a.m_playerModification != b.m_playerModification)
            return a.m_playerModification < b.m_playerModification;
        if (a.m_sortOrder != b.m_sortOrder)
            return a.m_sortOrder < b.m_sortOrder;
        if (a.m_created != b.m_created)
            return a.m_created < b.m_created;
        float sa = a.m_pos.x * a.m_pos.x + a.m_pos.y * a.m_pos.y + a.m_pos.z * a.m_pos.z;
        float sb = b.m_pos.x * b.m_pos.x + b.m_pos.y * b.m_pos.y + b.m_pos.z * b.m_pos.z;
        return sa < sb;
    }
};
