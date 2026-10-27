#include "UnityTransform.h"

#include <immintrin.h>

// The shuffles and the order of mul/sub/add follow the game's disassembly; checked bit for bit on 20000
// hierarchies dumped from the game (transform_test.json, transform_set_test.json).
namespace avledet::unity {

    namespace {
        using v4 = __m128;

        v4 Load(Quaternion q) { return _mm_setr_ps(q.x, q.y, q.z, q.w); }
        v4 Load(Vector3f v) { return _mm_setr_ps(v.x, v.y, v.z, 0); }

        Quaternion ToQuat(v4 v)
        {
            alignas(16) float f[4];
            _mm_store_ps(f, v);
            return Quaternion(f[0], f[1], f[2], f[3]);
        }

        Vector3f ToVec(v4 v)
        {
            alignas(16) float f[4];
            _mm_store_ps(f, v);
            return Vector3f(f[0], f[1], f[2]);
        }

        v4 Bits(unsigned a, unsigned b, unsigned c, unsigned d)
        {
            return _mm_castsi128_ps(_mm_setr_epi32((int) a, (int) b, (int) c, (int) d));
        }

        template<int I>
        v4 Shuffle(v4 a)
        {
            return _mm_shuffle_ps(a, a, I);
        }

        v4 SignXYZ() { return Bits(0x80000000, 0x80000000, 0x80000000, 0); }

        // p * q of the GetRotation loop and of the parent inverse in SetRotation
        v4 Mul(v4 p, v4 q)
        {
            v4 r = _mm_mul_ps(Shuffle<0x9c>(q), Shuffle<0x2d>(p));
            r    = _mm_sub_ps(r, _mm_mul_ps(Shuffle<0x22>(q), Shuffle<0x93>(p)));
            r    = _mm_sub_ps(r, _mm_mul_ps(Shuffle<0x4b>(q), _mm_unpackhi_ps(p, p)));
            r    = _mm_sub_ps(r, _mm_mul_ps(Shuffle<0xf5>(q), _mm_movelh_ps(p, p)));
            return _mm_xor_ps(Shuffle<0x4e>(r), SignXYZ());
        }

        // a mirroring scale flips the rotation axes: sign bits of (sz*sy, sz*sx, sy*sx, 0)
        v4 MirrorSigns(v4 scale)
        {
            v4 s = _mm_xor_ps(_mm_and_ps(scale, SignXYZ()), _mm_setr_ps(1, 1, 1, 0));
            v4 p = _mm_mul_ps(Shuffle<0xda>(s), Shuffle<0xc1>(s));
            v4 z = _mm_shuffle_ps(_mm_setzero_ps(), p, 0x20);
            p    = _mm_shuffle_ps(p, z, 0x24);
            return _mm_and_ps(p, Bits(0x80000000, 0x80000000, 0x80000000, 0x80000000));
        }

        // vector rotated by q (the GetPosition shuffle network)
        v4 Rotate(v4 q, v4 v)
        {
            v4 c8 = _mm_setr_ps(-2, 2, -2, 0), c9 = _mm_setr_ps(2, -2, -2, 0), c10 = _mm_setr_ps(-2, -2, 2, 0);
            v4 q3 = Shuffle<0xf1>(q), q2 = Shuffle<0x4e>(q), q11 = Shuffle<0xdb>(q), q6 = Shuffle<0xd5>(q);
            v4 q7 = Shuffle<0xea>(q), q4 = Shuffle<0xc0>(q);
            v4 a  = _mm_sub_ps(_mm_mul_ps(_mm_mul_ps(q6, c8), q3), _mm_mul_ps(_mm_mul_ps(q7, c9), q2));
            v4 b  = _mm_sub_ps(_mm_mul_ps(_mm_mul_ps(q7, c10), q11), _mm_mul_ps(_mm_mul_ps(q4, c8), q3));
            v4 c  = _mm_sub_ps(_mm_mul_ps(_mm_mul_ps(q4, c9), q2), _mm_mul_ps(_mm_mul_ps(q6, c10), q11));
            v4 x  = _mm_add_ps(_mm_mul_ps(Shuffle<0xc0>(v), a), v);
            v4 r  = _mm_add_ps(_mm_mul_ps(Shuffle<0xea>(v), c), _mm_mul_ps(Shuffle<0xd5>(v), b));
            return _mm_add_ps(r, x);
        }

        // 1 / scale: rcpps and two Newton steps, 0 below 1e-9
        v4 Reciprocal(v4 s)
        {
            v4 y0   = _mm_rcp_ps(s);
            v4 y1   = _mm_mul_ps(_mm_sub_ps(Bits(0x40000002, 0x40000002, 0x40000002, 0x40000002), _mm_mul_ps(s, y0)), y0);
            v4 y2   = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(2), _mm_mul_ps(s, y1)), y1);
            v4 nan  = _mm_cmpunord_ps(_mm_setzero_ps(), y2);
            v4 y    = _mm_or_ps(_mm_andnot_ps(nan, y2), _mm_and_ps(nan, y0));
            v4 abs  = _mm_and_ps(s, Bits(0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff));
            v4 tiny = _mm_cmplt_ps(abs, Bits(0x3089705f, 0x3089705f, 0x3089705f, 0));
            v4 keep = _mm_castsi128_ps(_mm_cmpgt_epi32(_mm_castps_si128(tiny), _mm_set1_epi32(-1)));
            return _mm_and_ps(keep, y);
        }
    }// namespace

    Quaternion Normalize(Quaternion q)
    {
        v4 v     = Load(q);
        v4 sq    = _mm_mul_ps(v, v);
        v4 pairs = _mm_add_ps(Shuffle<0x39>(sq), sq);
        v4 dot   = _mm_add_ps(Shuffle<0x4e>(pairs), pairs);
        v4 r     = _mm_div_ps(v, _mm_sqrt_ps(dot));
        v4 small = _mm_cmpnlt_ps(Bits(0x0da24260, 0x0da24260, 0x0da24260, 0x0da24260), dot);
        return ToQuat(_mm_or_ps(_mm_andnot_ps(small, r), _mm_and_ps(small, _mm_setr_ps(0, 0, 0, 1))));
    }

    Quaternion ToWorld(Quaternion parent, Vector3f parentScale, Quaternion local)
    {
        return ToQuat(Mul(Load(parent), _mm_xor_ps(Load(local), MirrorSigns(Load(parentScale)))));
    }

    Vector3f ToWorld(Vector3f parentPos, Quaternion parentRot, Vector3f parentScale, Vector3f local)
    {
        v4 scaled = _mm_mul_ps(Load(local), Load(parentScale));
        return ToVec(_mm_add_ps(Rotate(Load(parentRot), scaled), Load(parentPos)));
    }

    Quaternion ToLocal(Quaternion parent, Vector3f parentScale, Quaternion world)
    {
        v4 r = Mul(_mm_xor_ps(Load(parent), SignXYZ()), Load(world));
        return Normalize(ToQuat(_mm_xor_ps(r, MirrorSigns(Load(parentScale)))));
    }

    Vector3f ToLocal(Vector3f parentPos, Quaternion parentRot, Vector3f parentScale, Vector3f world)
    {
        v4 rel     = _mm_sub_ps(Load(world), Load(parentPos));
        v4 rotated = Rotate(_mm_xor_ps(Load(parentRot), SignXYZ()), rel);
        return ToVec(_mm_mul_ps(Reciprocal(Load(parentScale)), rotated));
    }

}// namespace avledet::unity
