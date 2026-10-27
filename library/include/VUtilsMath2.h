#pragma once

#include "Vector.h"

namespace VUtils::Math {

    // Fractional brownian noise
    float Fbm(Vector3f p, int octaves, float lacunarity, float gain);

    // Fractional brownian noise
    float FbmMaxValue(int octaves, float gain);

    // Fractional brownian noise
    float Fbm(Vector2f p, int octaves, float lacunarity, float gain);

    // DUtils.Fbm with double lacunarity and gain: the sum stays in double
    double Fbm(Vector2f p, int octaves, double lacunarity, double gain);

    float YawFromDirection(Vector3f dir);

    // Wrap degrees
    float FixDegAngle(float p_Angle);

}// namespace VUtils::Math
