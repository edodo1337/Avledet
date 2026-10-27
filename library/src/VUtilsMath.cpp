#include <cmath>
#include <stdexcept>

#include "Vector.h"
#include "VUtils.h"
#include "VUtilsMath.h"
#include "VUtilsMath2.h"
#include "VUtilsMathf.h"

namespace VUtils::Math {
    float sq_magnitude(float x, float y)
    {
        return (double)x * (double)x + (double)y * (double)y;
    }

    double sq_magnitude(double x, double y)
    {
        return x * x + y * y;
    }

    float magnitude(float x, float y)
    {
        return std::sqrt(sq_magnitude(x, y));
    }

    float Length(float x, float y)
    {
        return (float) std::sqrt((double) x * (double) x + (double) y * (double) y);
    }

    double magnitude(double x, double y)
    {
        return std::sqrt(sq_magnitude(x, y));
    }

    float sq_distance_to(float x1, float y1, float x2, float y2)
    {
        return sq_magnitude(x1 - x2, y1 - y2);
    }

    float distance_to(float x1, float y1, float x2, float y2)
    {
        return std::sqrt(sq_distance_to(x1, y1, x2, y2));
    }

    float sq_magnitude(float x, float y, float z)
    {
        return x * x + y * y + z * z;
    }

    float magnitude(float x, float y, float z)
    {
        return std::sqrt(sq_magnitude(x, y, z));
    }

    float sq_distance_to(float x1, float y1, float z1, float x2, float y2, float z2)
    {
        return sq_magnitude(x1 - x2, y1 - y2, z1 - z2);
    }

    float distance_to(float x1, float y1, float z1, float x2, float y2, float z2)
    {
        return std::sqrt(sq_distance_to(x1, y1, z1, x2, y2, z2));
    }

    double BlendOverlay(double a, double b)
	{
		double num = 2.0 * a * b;
		double num2 = 1.0 - 2.0 * (1.0 - a) * (1.0 - b);
		if (a >= 0.5)
		{
			return num2;
		}
		return num;
	}

    float Lerp(float a, float b, float t) {
        return Lerp((double)a, (double)b, (double)t);
        //if (t <= 0.0f)
		//{
		//	return a;
		//}
		//if (t >= 1.0f)
		//{
		//	return b;
		//}
		//return (float)((double)a * (1.0 - (double)t) + (double)b * (double)t);
    }

    double Lerp(double a, double b, double t) {
        if (t <= 0.0)
		{
			return a;
		}
		if (t >= 1.0)
		{
			return b;
		}
		return a * (1.0 - t) + b * t;
    }

    float Clamp(float value, float min, float max)
    {
        return std::min(std::max(value, min), max);
    }

    float LerpStep(float l, float h, float v)
    {
        //return Mathf::Clamp01((v - l) / (h - l));
        return LerpStep((double)l, (double)h, (double)v);
    }

    double LerpStep(double l, double h, double v) {
        return Clamp01((v - l) / (h - l));
    }

    float SmoothStep(float p_Min, float p_Max, float p_X)
    {
        float num = (float)Clamp01(((double)p_X - (double)p_Min) / ((double)p_Max - (double)p_Min));
		return (float)((double)num * (double)num * (3.0 - 2.0 * (double)num));
    }

	double MathfLikeSmoothStep(double from, double to, double t)
	{
		t = Clamp01(t);
		t = -2.0 * t * t * t + 3.0 * t * t;
		return (double)((float)(to * t + from * (1.0 - t)));
	}

    double Clamp01(double value) {
        return value > 1.0 ? 1.0 : value < 0.0 ? 0.0 : value;
    }

    //double LerpStep(double l, double h, double v)
    //{
    //    return Mathf::Clamp01((float) ((v - l) / (h - l)));
    //}

    float Fbm(Vector3f p, int octaves, float lacunarity, float gain)
    {
        return Fbm(Vector2f(p.x, p.z), octaves, lacunarity, gain);
    }

    //float FbmMaxValue(int octaves, float gain)
    //{
    //    float num  = 0;
    //    float num2 = 1;
    //    for (int i = 0; i < octaves; i++) {
    //        num += num2;
    //        num2 *= gain;
    //    }
    //    return num;
    //}

    float Fbm(Vector2f p, int octaves, float lacunarity, float gain)
    {
        float num  = 0;
        float num2 = 1;
        for (int i = 0; i < octaves; i++) {
            num = (float)((double)num + (double)num2 * (double)VUtils::Math::PerlinNoise(p.x, p.y));
            num2 = (float)((double)num2 * (double)gain);
            p *= lacunarity;
        }
        return num;
    }

    double Fbm(Vector2f p, int octaves, double lacunarity, double gain)
    {
        double num = 0, num2 = 1, x = p.x, y = p.y;
        for (int i = 0; i < octaves; i++) {
            num += num2 * (double) VUtils::Math::PerlinNoise((float) x, (float) y);
            num2 *= gain;
            x *= lacunarity;
            y *= lacunarity;
        }
        return num;
    }

    float FISQRT(float number)
    {
        (void) number;
        assert(false);
        throw std::runtime_error("nyi");
        //long i;
        //float x2, y;
        //float const threehalfs = 1.5F;

        //x2 = number * 0.5F;
        //y  = number;
        //i  = *(long *) &y;                   // evil floating point bit level hacking
        //i  = 0x5f3759df - (i >> 1);          // what the fuck?
        //y  = *(float *) &i;
        //y  = y * (threehalfs - (x2 * y * y));// 1st iteration
        //y  = y * (threehalfs - (x2 * y * y));// 2nd iteration, this can be removed

        //return y;
    }

    // Ease-in-out function
    // t: [0, 1]
    // https://www.desmos.com/calculator/tgpfii21pt

    static std::uint8_t p[] = {
            151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142,
            8, 99, 37, 240, 21, 10, 23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203,
            117, 35, 11, 32, 57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165,
            71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92,
            41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208,
            89, 18, 169, 200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217,
            226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58,
            17, 182, 189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155,
            167, 43, 172, 9, 129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218,
            246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14,
            239, 107, 49, 192, 214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150,
            254, 138, 236, 205, 93, 222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180,
            // 2nd copy
            151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142,
            8, 99, 37, 240, 21, 10, 23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203,
            117, 35, 11, 32, 57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165,
            71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92,
            41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208,
            89, 18, 169, 200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217,
            226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58,
            17, 182, 189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155,
            167, 43, 172, 9, 129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218,
            246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14,
            239, 107, 49, 192, 214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150,
            254, 138, 236, 205, 93, 222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180};

    // Mathf.PerlinNoise, rebuilt from UnityPlayer.so (Unity 6, Valheim 1.0.20):
    // everything is single precision, fade is t*(t*t) * ((t*6 - 15)*t + 10),
    // lerps are a + t*(b - a). Must stay bit exact, biomes depend on it.
    static float unity_fade(float t)
    {
        t = std::min(1.0f, t);
        float t3 = t * (t * t);
        return t3 * ((t * 6.0f + -15.0f) * t + 10.0f);
    }

    static float unity_grad(int hash, float x, float y)
    {
        int h   = hash & 15;
        float u = h < 8 ? x : y;
        float v = h < 4 ? y : (h == 12 || h == 14 ? x : 0.0f);
        return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    }

    float PerlinNoise(float x, float y)
    {
        x = std::abs(x);
        y = std::abs(y);

        int ix = (int) x;
        int iy = (int) y;
        int X  = ix & 0xFF;
        int Y  = iy & 0xFF;

        x -= (float) ix;
        y -= (float) iy;

        int A = p[X] + Y;
        int B = p[X + 1] + Y;

        float gAA = unity_grad(p[p[A]], x, y);
        float gBA = unity_grad(p[p[B]], x - 1.0f, y);
        float gAB = unity_grad(p[p[A + 1]], x, y - 1.0f);
        float gBB = unity_grad(p[p[B + 1]], x - 1.0f, y - 1.0f);

        float u = unity_fade(x);
        float v = unity_fade(y);

        float x1 = gAA + u * (gBA - gAA);
        float x2 = gAB + u * (gBB - gAB);
        float res = x1 + v * (x2 - x1);

        return (res + 0.69f) / 1.483f;
    }

    double Remap(double value, double inLow, double inHigh, double outLow, double outHigh)
	{
		return Lerp(outLow, outHigh, InverseLerp(inLow, inHigh, value));
	}

    double InverseLerp(double a, double b, double value)
	{
		if (a == b)
		{
			return 0.0;
		}
		return Clamp01((value - a) / (b - a));
	}

    /*
    float PerlinNoise(double x, double y) {
        return PerlinNoise((float)x, float(y));
    }*/

    float FixDegAngle(float p_Angle)
    {
        while (p_Angle >= 360) p_Angle -= 360;

        while (p_Angle < 0) p_Angle += 360;

        return p_Angle;
    }

    float YawFromDirection(Vector3f dir)
    {
        float num = std::atan2(dir.x, dir.z);
        return FixDegAngle(57.29578f * num);
    }

    bool Between(float i, float a, float b)
    {
        assert(a <= b);

        return i >= a && i <= b;
    }

}// namespace VUtils::Math