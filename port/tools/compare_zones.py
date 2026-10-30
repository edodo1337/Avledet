#!/usr/bin/env python3
"""Compare zone objects (zones.txt) of a vanilla and an Avledet probe.

Line format: zx zy prefab x y z qx qy qz qw
Objects match by zone, prefab and xz within 5 cm (closest y wins). Non-persistent objects are left out.
Vanilla rotates locations with an unseeded random, so objects left unmatched in a
zone with a LocationProxy are matched again in the location's local frame.
Usage: compare_zones.py <vanilla_zones.txt> <avledet_zones.txt> [top]
"""
import sys
from collections import Counter, defaultdict

XZ_EPS = 0.05


def _transient():
    """Non-persistent prefabs: never saved, destroyed with their zone. The vanilla probe has none left
    (its reference point is far from the zones), so they are left out on both sides."""
    import json, os
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "re", "pkg-1.0.21", "json", "prefabs.json")
    return {p['name'] for p in json.load(open(path)) if p.get('view') and not p['view']['m_persistent']}


TRANSIENT = _transient()


def load(path):
    objs = defaultdict(list)
    for line in open(path):
        p = line.split()
        if len(p) < 10 or p[2] in TRANSIENT:
            continue
        objs[(int(p[0]), int(p[1]), p[2])].append(tuple(map(float, p[3:10])))
    return objs


def _cost(v, a):
    """Closest y first, then the closest rotation (objects stacked at one spot, like the vegetation
    entries sharing a prefab and so a seed, would pair crosswise otherwise)."""
    dot = abs(sum(x * y for x, y in zip(v[3:7], a[3:7])))
    return (abs(a[1] - v[1]), 1 - dot)


def match(va, aa):
    """Greedy match by xz, closest y and rotation first. Returns (matched pairs, unmatched vanilla, unmatched avledet)."""
    left = list(aa)
    pairs, miss = [], []
    for v in va:
        best = None
        for i, a in enumerate(left):
            if abs(a[0] - v[0]) < XZ_EPS and abs(a[2] - v[2]) < XZ_EPS:
                if best is None or _cost(v, a) < _cost(v, left[best]):
                    best = i
        if best is None:
            miss.append(v)
        else:
            pairs.append((v, left.pop(best)))
    return pairs, miss, left


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def to_local(o, proxy):
    """Object (x y z q) in the frame of a LocationProxy."""
    inv = (-proxy[3], -proxy[4], -proxy[5], proxy[6])
    d = (o[0] - proxy[0], o[1] - proxy[1], o[2] - proxy[2], 0.0)
    p = qmul(qmul(inv, d), proxy[3:])
    return (p[0], p[1], p[2]) + qmul(inv, o[3:])


def main():
    van, avl = load(sys.argv[1]), load(sys.argv[2])
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    zones = {k[:2] for k in van}
    avl = {k: v for k, v in avl.items() if k[:2] in zones}

    proxies = lambda objs: {k[:2]: v[0] for k, v in objs.items() if k[2] == "LocationProxy" and len(v) == 1}
    van_proxy, avl_proxy = proxies(van), proxies(avl)

    per_prefab = defaultdict(lambda: [0, 0, 0, 0])  # vanilla, avledet, matched, bad y/rot
    per_zone = Counter()
    local_matched = 0
    for key in set(van) | set(avl):
        pairs, miss, extra = match(van.get(key, []), avl.get(key, []))
        z = key[:2]
        if miss and extra and z in van_proxy and z in avl_proxy:
            lp, miss, extra = match([to_local(o, van_proxy[z]) for o in miss],
                                    [to_local(o, avl_proxy[z]) for o in extra])
            pairs += lp
            local_matched += len(lp)
        st = per_prefab[key[2]]
        st[0] += len(van.get(key, []))
        st[1] += len(avl.get(key, []))
        st[2] += len(pairs)
        for v, a in pairs:
            # quaternions q and -q are the same rotation
            dot = abs(sum(x * y for x, y in zip(v[3:], a[3:])))
            if abs(v[1] - a[1]) > XZ_EPS or dot < 0.9999:
                st[3] += 1
        per_zone[key[:2]] += len(miss) + len(extra)

    tv = sum(s[0] for s in per_prefab.values())
    ta = sum(s[1] for s in per_prefab.values())
    tm = sum(s[2] for s in per_prefab.values())
    tb = sum(s[3] for s in per_prefab.values())
    print(f"\n== zone objects in {len(zones)} zones: vanilla {tv}, avledet {ta}, "
          f"matched xz {tm} ({100.0 * tm / max(1, tv):.1f}% of vanilla), of them bad y/rot {tb}")
    print(f"of them matched in location frame (random location rotation): {local_matched}")
    print(f"zones without differences: {len(zones) - sum(1 for z in zones if per_zone[z])}/{len(zones)}")

    rows = [(n, *s) for n, s in per_prefab.items() if s[0] != s[2] or s[1] != s[2] or s[3]]
    rows.sort(key=lambda r: -(r[1] - r[3] + r[2] - r[3] + r[4]))
    print(f"{len(rows)} prefabs with differences (top {top}):")
    print(f"  {'prefab':<32} {'vanilla':>7} {'avledet':>7} {'matched':>7} {'bad y/rot':>9}")
    for n, v, a, m, b in rows[:top]:
        print(f"  {n:<32} {v:>7} {a:>7} {m:>7} {b:>9}")

    print("zones with most differences:")
    for z, c in per_zone.most_common(10):
        if c:
            print(f"  {z[0]:>4} {z[1]:>4}  {c}")


if __name__ == "__main__":
    main()
