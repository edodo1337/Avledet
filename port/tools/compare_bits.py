#!/usr/bin/env python3
"""Bit for bit diff of two probe zones.txt (vanilla, avledet): objects paired by prefab and nearest
position (stacks of the same block share x/z), then y, x/z and rotation compared as float32 bits.
Usage: compare_bits.py <vanilla zones.txt> <avledet zones.txt>"""
import struct
import sys
from collections import Counter, defaultdict


def f32(x):
    return struct.unpack('<f', struct.pack('<f', float(x)))[0]


def load(path):
    d = defaultdict(list)
    for line in open(path):
        q = line.split()
        d[(q[0], q[1], q[2])].append(q)
    return d


def dist(a, b):
    # rotation breaks the tie of objects in the same point
    return (sum(abs(float(s) - float(t)) for s, t in zip(a[3:6], b[3:6]))
            + 1e-3 * sum(abs(float(s) - float(t)) for s, t in zip(a[6:10], b[6:10])))


def main():
    van, avl = load(sys.argv[1]), load(sys.argv[2])
    c, by_y, by_rot = Counter(), Counter(), Counter()
    for k in van.keys() & avl.keys():
        pool = list(avl[k])
        for x in van[k]:
            if not pool:
                break
            y = min(pool, key=lambda o: dist(x, o))
            if dist(x, y) > 0.05:
                continue
            pool.remove(y)
            c['pairs'] += 1
            if f32(x[4]) != f32(y[4]):
                c['y'] += 1
                by_y[k[2]] += 1
            if f32(x[3]) != f32(y[3]) or f32(x[5]) != f32(y[5]):
                c['xz'] += 1
            if [f32(t) for t in x[6:10]] != [f32(t) for t in y[6:10]]:
                c['rot'] += 1
                by_rot[k[2]] += 1
    print('bit for bit: %d pairs, y differs %d, x/z %d, rotation %d' % (c['pairs'], c['y'], c['xz'], c['rot']))
    print('  y:', by_y.most_common(6))
    print('  rotation:', by_rot.most_common(6))


if __name__ == '__main__':
    main()
