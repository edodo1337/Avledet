#include <cmath>
#include <limits>
#include <stdexcept>

#include "Quaternion.h"
#include "Vector.h"
#include "VUtils.h"

namespace avledet::util::CSU {

    Quaternion const Quaternion::IDENTITY = Quaternion(0, 0, 0, 1);

    //constexpr Quaternion Quaternion::IDENTITY(0.f, 0.f, 0.f, 1.f);

    //constexpr Quaternion::Quaternion() :
    //    Quaternion(IDENTITY)
    //{
    //}

    //constexpr Quaternion::Quaternion(float x, float y, float z, float w) :
    //    x(x),
    //    y(y),
    //    z(z),
    //    w(w)
    //{
    //}

    //constexpr Quaternion::Quaternion(Vector3f const &v, float w)
    //{
    //    this->x = v.x;
    //    this->y = v.y;
    //    this->z = v.z;
    //    this->w = w;
    //}

    float Quaternion::length_squared() const
    {
        return x * x + y * y + z * z + w * w;
    }

    Vector3f Quaternion::xyz() const
    {
        return Vector3f(x, y, z);
    }

    // Unity eulerAngles: Internal_MakePositive(Internal_ToEulerRad(q) * Rad2Deg)
    Vector3f Quaternion::euler_angles() const
    {
        Vector3f e = Internal_ToEulerRad(*this);
        e          = Vector3f(e.x * 57.29578f, e.y * 57.29578f, e.z * 57.29578f);
        float negativeFlip = -0.0001f * 57.29578f;
        float positiveFlip = 360.0f + negativeFlip;
        for (float *v : {&e.x, &e.y, &e.z}) {
            if (*v < negativeFlip)
                *v += 360.0f;
            else if (*v > positiveFlip)
                *v -= 360.0f;
        }
        return e;
    }

    // UnityPlayer.so Quaternion::Internal_ToEulerRad: normalize, then QuaternionToEuler.
    // Ported from the disassembly, bit exact with the game (checked on 20000 samples).
    Vector3f Quaternion::Internal_ToEulerRad(Quaternion q)
    {
        float len = std::sqrt(q.w * q.w + ((q.y * q.y + q.x * q.x) + q.z * q.z));
        if (len < 9.99999974737875e-06f)
            q = IDENTITY;
        else
            q = Quaternion(q.x / len, q.y / len, q.z / len, q.w / len);

        float x = q.x, y = q.y, z = q.z, w = q.w;
        float xx = x * x, xy = x * y, xw = x * w, yy = y * y, yz = y * z, zz = z * z, zw = z * w, ww = w * w;
        float t = yz - xw;
        bool singular = !(std::fabs(t) < 0.4999989867210388f);
        float a, b;
        if (!singular) {
            a = (x * z + y * w) + (x * z + y * w);
            b = ww + ((zz - xx) - yy);
        } else {
            float p = xw - yz, r = xy - zw, s = yz + xw;
            a = p * r + (xy + zw) * s;
            b = s * p - r * (xy + zw);
        }
        float t2  = t + t;
        float arg = t2 < -1.0f ? -1.0f : std::fmin(1.0f, t2);

        Vector3f v;
        v.x = -std::asin(arg);
        v.y = std::atan2(a, b);
        v.z = singular ? 0.0f : std::atan2((xy + zw) + (xy + zw), ((yy - zz) - xx) + ww);
        return v;
    }

    Vector3f Quaternion::normalize_angles(Vector3f angles)
    {
        angles.x = normalize_angles(angles.x);
        angles.y = normalize_angles(angles.y);
        angles.z = normalize_angles(angles.z);
        return angles;
    }

    float Quaternion::normalize_angles(float angle)
    {
        float modAngle = std::fmod(angle, 360.0f);

        if (modAngle < 0.0f)
            return modAngle + 360.0f;
        else
            return modAngle;
    }

    Vector3f Quaternion::operator*(Vector3f point) const
    {
        float x2 = x * 2.f;
        float y2 = y * 2.f;
        float z2 = z * 2.f;

        float x2s = x * x2;
        float y2s = y * y2;
        float z2s = z * z2;

        float xy2 = x * y2;
        float xz2 = x * z2;
        float yz2 = y * z2;

        float wx2 = w * x2;
        float wy2 = w * y2;
        float wz2 = w * z2;

        return {(1.f - (y2s + z2s)) * point.x + (xy2 - wz2) * point.y + (xz2 + wy2) * point.z,
                (xy2 + wz2) * point.x + (1.f - (x2s + z2s)) * point.y + (yz2 - wx2) * point.z,
                (xz2 - wy2) * point.x + (yz2 + wx2) * point.y + (1.f - (x2s + y2s)) * point.z};
    }

    Quaternion Quaternion::operator*(Quaternion rhs) const
    {
        return Quaternion(
                w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y, w * rhs.y + y * rhs.w + z * rhs.x - x * rhs.z,
                w * rhs.z + z * rhs.w + x * rhs.y - y * rhs.x, w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z);
    }

    void Quaternion::operator*=(Quaternion rhs)
    {
        *this = *this * rhs;
    }

    float Quaternion::dot(Quaternion b) const
    {
        return x * b.x + y * b.y + z * b.z + w * b.w;
    }

    Quaternion Quaternion::normalized() const {
        float sq = this->length_squared();
        if (sq < std::numeric_limits<float>::epsilon()) {
            return IDENTITY;
        }
        
        sq = std::sqrt(sq);
        return Quaternion(x / sq, y / sq, z / sq, w / sq);
    }

    bool Quaternion::operator==(Quaternion other) const
    {
        return Quaternion::dot(other) > 0.999999f;
    }

    bool Quaternion::operator!=(Quaternion other) const
    {
        return !(*this == other);
    }

    // Originally based on:
    //  https://gist.github.com/HelloKitty/91b7af87aac6796c3da9#file-quaternion-cs-L644
    // Further research finds that the above link is not an equivalent implementation.
    // Unity Quaternion.Euler: Internal_FromEulerRad(v * Deg2Rad). Ported from UnityPlayer.so,
    // the zero terms are kept as the game computes them (bit exact on 20000 samples).
    Quaternion Quaternion::euler(float ex, float ey, float ez)
    {
        float const k = 0.5f, o = 0.0f;
        ex *= 0.0174532924f;
        ey *= 0.0174532924f;
        ez *= 0.0174532924f;
        float cx = std::cos(ex * k), sx = std::sin(ex * k);
        float cy = std::cos(ey * k), sy = std::sin(ey * k);
        float cz = std::cos(ez * k), sz = std::sin(k * ez);
        float A = (sx * cy + cx * o) + sy * o;
        float B = (cx * sy + cy * o) + sx * o;
        float C = ((cy * o + cx * o) + o) - sx * sy;
        float D = (cx * cy - sx * o) - sy * o;
        return Quaternion(((B * sz + (A * cz + D * o)) - C * o), (((cz * B + D * o) + C * o) - A * sz),
                          (((cz * C + D * sz) + A * o) - B * o), (((D * cz - A * o) - B * o) - C * sz));
    }

    // https://web.archive.org/web/20221126145919/https://gist.github.com/aeroson/043001ca12fe29ee911e
    Quaternion Quaternion::look_rotation(Vector3f forward, Vector3f up)
    {
        // TODO lowercase migrate
        forward        = forward.normal();
        Vector3f right = up.cross(forward).normal();
        up             = forward.cross(right);

        auto m00 = right.x;
        auto m01 = right.y;
        auto m02 = right.z;
        auto m10 = up.x;
        auto m11 = up.y;
        auto m12 = up.z;
        auto m20 = forward.x;
        auto m21 = forward.y;
        auto m22 = forward.z;

        auto num8 = (m00 + m11) + m22;
        Quaternion quaternion;
        if (num8 > 0) {
            auto num     = std::sqrt(num8 + 1.f);
            quaternion.w = num * 0.5f;
            num          = 0.5f / num;
            quaternion.x = (m12 - m21) * num;
            quaternion.y = (m20 - m02) * num;
            quaternion.z = (m01 - m10) * num;
            return quaternion;
        }

        if ((m00 >= m11) && (m00 >= m22)) {
            auto num7    = std::sqrt(((1.f + m00) - m11) - m22);
            auto num4    = 0.5f / num7;
            quaternion.x = 0.5f * num7;
            quaternion.y = (m01 + m10) * num4;
            quaternion.z = (m02 + m20) * num4;
            quaternion.w = (m12 - m21) * num4;
            return quaternion;
        }

        if (m11 > m22) {
            auto num6    = std::sqrt(((1.f + m11) - m00) - m22);
            auto num3    = 0.5f / num6;
            quaternion.x = (m10 + m01) * num3;
            quaternion.y = 0.5f * num6;
            quaternion.z = (m21 + m12) * num3;
            quaternion.w = (m20 - m02) * num3;
            return quaternion;
        }

        auto num5 = std::sqrt(((1.f + m22) - m00) - m11);
        auto num2 = 0.5f / num5;

        quaternion.x = (m20 + m02) * num2;
        quaternion.y = (m21 + m12) * num2;
        quaternion.z = 0.5f * num5;
        quaternion.w = (m01 - m10) * num2;

        return quaternion;
    }

    Quaternion Quaternion::inverse(Quaternion rotation)
    {
        float lengthSq = rotation.length_squared();
        if (lengthSq != 0.0) {
            float i = 1.0f / lengthSq;
            return Quaternion(rotation.xyz() * -i, rotation.w * i);
        }
        return rotation;
    }

}// namespace avledet::util::CSU

std::ostream &operator<<(std::ostream &st, avledet::util::CSU::Quaternion const &quat)
{
    return st << "(" << quat.x << ", " << quat.y << ", " << quat.z << ", " << quat.w << ")";
}