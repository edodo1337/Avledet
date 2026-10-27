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

    // The UnityEngine C# operators as the game's Mono runs them: a float expression is evaluated in
    // double and rounded when stored (checked on managed_test.json dumped from the game)
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

        double px = point.x, py = point.y, pz = point.z;
        return {(float) ((1.0 - ((double) y2s + z2s)) * px + ((double) xy2 - wz2) * py + ((double) xz2 + wy2) * pz),
                (float) (((double) xy2 + wz2) * px + (1.0 - ((double) x2s + z2s)) * py + ((double) yz2 - wx2) * pz),
                (float) (((double) xz2 - wy2) * px + ((double) yz2 + wx2) * py + (1.0 - ((double) x2s + y2s)) * pz)};
    }

    Quaternion Quaternion::operator*(Quaternion rhs) const
    {
        double lx = x, ly = y, lz = z, lw = w;
        return Quaternion((float) (lw * rhs.x + lx * rhs.w + ly * rhs.z - lz * rhs.y),
                          (float) (lw * rhs.y + ly * rhs.w + lz * rhs.x - lx * rhs.z),
                          (float) (lw * rhs.z + lz * rhs.w + lx * rhs.y - ly * rhs.x),
                          (float) (lw * rhs.w - lx * rhs.x - ly * rhs.y - lz * rhs.z));
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

    // UnityPlayer.so Quaternion.LookRotation: forward / |forward|, right = up x forward normalized,
    // up = forward x right, then MatrixToQuaternion and a normalize. Unity takes FromToRotation(z, forward)
    // or the identity for zero or parallel vectors; the generation never gets those, the identity here.
    Quaternion Quaternion::look_rotation(Vector3f forward, Vector3f up)
    {
        float fm = std::sqrt((forward.x * forward.x + forward.y * forward.y) + forward.z * forward.z);
        if (1e-5f > fm)
            return IDENTITY;
        Vector3f f = forward / fm;
        Vector3f r = up.cross(f);
        float rm   = std::sqrt((r.x * r.x + r.y * r.y) + r.z * r.z);
        if (1e-5f > rm)
            return IDENTITY;
        r          = r / rm;
        Vector3f u = f.cross(r);
        if (std::abs(((u.x * u.x + u.y * u.y) + u.z * u.z) - 1.f) > 1e-6f)
            return IDENTITY;

        // MatrixToQuaternion, columns right, up, forward: M(i, j) = d[i + 3 * j]
        float const d[9] = {r.x, r.y, r.z, u.x, u.y, u.z, f.x, f.y, f.z};
        float q[4];
        float trace = (d[0] + d[4]) + d[8];
        if (trace > 0) {
            float s = std::sqrt(trace + 1.f);
            q[3]    = s * .5f;
            float c = .5f / s;
            q[0]    = (d[5] - d[7]) * c;
            q[1]    = (d[6] - d[2]) * c;
            q[2]    = (d[1] - d[3]) * c;
        } else {
            int i = d[4] > d[0] ? 1 : 0;
            if (d[8] > d[4 * i])
                i = 2;
            int j = (i + 1) % 3, k = (j + 1) % 3;
            float s = std::sqrt(((d[4 * i] - d[4 * j]) - d[4 * k]) + 1.f);
            q[i]    = s * .5f;
            float c = .5f / s;
            q[3]    = (d[3 * j + k] - d[3 * k + j]) * c;
            q[j]    = (d[3 * i + j] + d[3 * j + i]) * c;
            q[k]    = (d[3 * i + k] + d[3 * k + i]) * c;
        }
        float n = std::sqrt(((q[0] * q[0] + q[1] * q[1]) + q[2] * q[2]) + q[3] * q[3]);
        return Quaternion(q[0] / n, q[1] / n, q[2] / n, q[3] / n);
    }

    // UnityPlayer.so Quaternion.Inverse: the conjugate, no division by the norm
    Quaternion Quaternion::inverse(Quaternion rotation)
    {
        return Quaternion(-rotation.x, -rotation.y, -rotation.z, rotation.w);
    }

}// namespace avledet::util::CSU

std::ostream &operator<<(std::ostream &st, avledet::util::CSU::Quaternion const &quat)
{
    return st << "(" << quat.x << ", " << quat.y << ", " << quat.z << ", " << quat.w << ")";
}