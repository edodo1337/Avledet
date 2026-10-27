#!/usr/bin/env python3
"""Compare vegetation raycast logs (phys.txt) of the vanilla probe and Avledet, zone by zone.
Lines: blk zx zy x z hit [y collider] | sol zx zy x y z hit [h nx ny nz [collider]]
Reports the first divergence per zone: the calls before it are the same on both sides.
Usage: compare_phys.py <vanilla phys.txt> <avledet phys.txt> [shown]"""
import sys
from collections import defaultdict, Counter


def load(path):
    """Calls per zone, each with the vegetation running it appended (veg lines)."""
    zones, veg = defaultdict(list), {}
    for line in open(path):
        p = line.split()
        z = (int(p[1]), int(p[2]))
        if p[0] == 'veg':
            veg[z] = p[3]
        else:
            zones[z].append(p + ['<' + veg.get(z, '?') + '>'])
    return zones


van, avl = load(sys.argv[1]), load(sys.argv[2])
shown = int(sys.argv[3]) if len(sys.argv) > 3 else 20
same, kinds, firsts = 0, Counter(), []
hdiff = []
for z in sorted(avl):
    v, a = van.get(z, []), avl[z]
    for i, (lv, la) in enumerate(zip(v, a)):
        if lv[0] != la[0] or abs(float(lv[3]) - float(la[3])) > 1e-3 or abs(float(lv[-1 if False else 4]) - float(la[4])) > 1e-3:
            kinds['call order'] += 1
            firsts.append((z, i, lv, la, 'order'))
            break
        hit = 5 if lv[0] == 'blk' else 6
        if lv[hit] != la[hit]:
            kinds[lv[0] + ' verdict'] += 1
            firsts.append((z, i, lv, la, 'verdict'))
            break
        if lv[0] == 'sol' and lv[hit] == '1':
            d = abs(float(lv[7]) - float(la[7]))
            hdiff.append(d)
            if d > 1e-3:
                kinds['sol height'] += 1
                firsts.append((z, i, lv, la, 'height'))
                break
    else:
        if len(v) != len(a):
            kinds['length'] += 1
            firsts.append((z, min(len(v), len(a)), v[len(a)] if len(v) > len(a) else None, a[len(v)] if len(a) > len(v) else None, 'length'))
        else:
            same += 1
print('zones identical: %d/%d' % (same, len(avl)), dict(kinds))
if hdiff:
    hdiff.sort()
    print('sol height |diff|: exact %d of %d, median %.3g, max %.3g' % (sum(1 for d in hdiff if d == 0), len(hdiff), hdiff[len(hdiff)//2], hdiff[-1]))
for z, i, lv, la, why in firsts[:shown]:
    print('%s zone %s call %d\n  van %s\n  avl %s' % (why, z, i, ' '.join(lv) if lv else '-', ' '.join(la) if la else '-'))
