#!/usr/bin/env python3
"""Compare a vanilla worldgen probe with an Avledet probe.

Usage: compare_probe.py <vanilla_dir> <avledet_dir>
"""
import json
import math
import struct
import sys
from collections import Counter

BIOMES = {1: "Meadows", 2: "Swamp", 4: "Mountain", 8: "BlackForest", 16: "Plains",
          32: "AshLands", 64: "DeepNorth", 256: "Ocean", 512: "Mistlands"}


def floats(path):
    data = open(path, "rb").read()
    return struct.unpack(f"<{len(data) // 4}f", data)


def ints(path):
    data = open(path, "rb").read()
    return struct.unpack(f"<{len(data) // 4}i", data)


def compare_samples(van, avl, step):
    vh, ah = floats(f"{van}/samples_height.f32"), floats(f"{avl}/samples_height.f32")
    vb, ab = ints(f"{van}/samples_biome.i32"), ints(f"{avl}/samples_biome.i32")
    n = int(math.isqrt(len(vh)))
    assert len(vh) == len(ah) == n * n, "sample grids differ in size"

    biome_bad = Counter()
    biome_total = Counter()
    height_bad = Counter()
    worst = Counter()
    for i in range(len(vh)):
        x = -10560 + (i % n) * step
        y = -10560 + (i // n) * step
        if x * x + y * y > 10500 * 10500:
            continue
        name = BIOMES.get(vb[i], str(vb[i]))
        biome_total[name] += 1
        if vb[i] != ab[i]:
            biome_bad[name] += 1
        d = abs(vh[i] - ah[i])
        if d > 0.01:
            height_bad[name] += 1
            worst[name] = max(worst[name], d)

    print("== samples (inside world radius), per vanilla biome")
    print(f"{'biome':<12} {'samples':>8} {'biome diff':>11} {'height diff':>12} {'max dh':>8}")
    for name, total in biome_total.most_common():
        print(f"{name:<12} {total:>8} {biome_bad[name]:>11} {height_bad[name]:>12} {worst[name]:>8.2f}")
    t = sum(biome_total.values())
    print(f"{'TOTAL':<12} {t:>8} {sum(biome_bad.values()):>11} {sum(height_bad.values()):>12}")


def compare_locations(van, avl):
    vl = json.load(open(f"{van}/locations.json"))
    al = json.load(open(f"{avl}/locations.json"))
    vc = Counter(l["name"] for l in vl)
    ac = Counter(l["name"] for l in al)
    # match by name and xz within 1 cm (files print floats with different precision)
    by_name = {}
    for l in al:
        by_name.setdefault(l["name"], []).append(l["pos"])
    same = 0
    for l in vl:
        for p in by_name.get(l["name"], []):
            if abs(p[0] - l["pos"][0]) < 0.01 and abs(p[2] - l["pos"][2]) < 0.01:
                same += 1
                break
    print(f"\n== locations: vanilla {len(vl)}, avledet {len(al)}, same name+xz {same} "
          f"({100.0 * same / max(1, len(vl)):.1f}% of vanilla)")
    diffs = [(n, vc[n], ac[n]) for n in set(vc) | set(ac) if vc[n] != ac[n]]
    diffs.sort(key=lambda d: -abs(d[1] - d[2]))
    print(f"{len(diffs)} location types with different counts (top 25):")
    for n, v, a in diffs[:25]:
        print(f"  {n:<32} vanilla {v:>5}  avledet {a:>5}")


def compare_sectors(van, avl):
    import os
    if not os.path.exists(f"{avl}/sectors.json"):
        return
    vs = json.load(open(f"{van}/sectors.json"))
    av = json.load(open(f"{avl}/sectors.json"))
    vg, ag = ints(f"{van}/grid_sectors.i32"), ints(f"{avl}/grid_sectors.i32")
    grid_bad = sum(1 for a, b in zip(vg, ag) if a != b)
    print(f"\n== biome sectors: vanilla {len(vs)}, avledet {len(av)}, grid cells with other sector index {grid_bad}")
    bad = Counter()
    for i, (a, b) in enumerate(zip(vs, av)):
        if a["biome"] != b["biome"]:
            bad["biome"] += 1
        if a["edgeCount"] != b["edgeCount"]:
            bad["edgeCount"] += 1
        if any(abs(x - y) > 0.01 for x, y in zip(a["center"], b["center"])):
            bad["center"] += 1
        if a["altBiomes"] != b["altBiomes"]:
            bad["altBiomes"] += 1
            if bad["altBiomes"] <= 5:
                print(f"  sector {i}: vanilla {a['altBiomes']} avledet {b['altBiomes']}")
    print(f"  mismatching fields: {dict(bad) or 'none'}")
    va = Counter(n for s in vs for n in s["altBiomes"])
    aa = Counter(n for s in av for n in s["altBiomes"])
    print(f"  alt biome placements: vanilla {sum(va.values())}, avledet {sum(aa.values())}, same counts per name: {va == aa}")


def main():
    van, avl = sys.argv[1], sys.argv[2]
    vw = json.load(open(f"{van}/world.json"))
    aw = json.load(open(f"{avl}/world.json"))
    print(f"seed vanilla {vw['seedName']}/{vw['seed']}  avledet {aw['seedName']}/{aw['seed']}")
    assert vw["seed"] == aw["seed"], "different seeds"
    compare_samples(van, avl, vw["step"])
    compare_sectors(van, avl)
    compare_locations(van, avl)


if __name__ == "__main__":
    main()
