#!/usr/bin/env python3
"""Dump a Valheim 1.0 world save (worlds_local/<world>/) as canonical text, to diff two saves.
Reads the newest _main.N set that has an .ok marker. ZDO lines are sorted, ids are not kept
(vanilla renumbers on load), connection hashes are shown by type only (any unique ints match).
Usage: save_dump.py <world dir> [--summary]"""
import gzip
import os
import re
import struct
import sys
from collections import Counter

SESSION = ['CatchID_u', 'CatchID_i', 'alert', 'animation_speed', 'body_avel', 'body_vel', 'BodyVelocity',
           'haveTarget', 'InUse', 'landed', 'LookTarget', 'noise', 'support', 'tiltrot', 'vel', 'velRel']


def stable_hash(s):
    h1, h2 = 5381, 5381
    for i in range(0, len(s), 2):
        h1 = ((h1 << 5) + h1 ^ ord(s[i])) & 0xFFFFFFFF
        if i + 1 < len(s):
            h2 = ((h2 << 5) + h2 ^ ord(s[i + 1])) & 0xFFFFFFFF
    v = (h1 + h2 * 1566083941) & 0xFFFFFFFF
    return v - (1 << 32) if v >= 1 << 31 else v


class R:
    def __init__(self, b): self.b, self.p = b, 0
    def raw(self, fmt):
        v = struct.unpack_from('<' + fmt, self.b, self.p); self.p += struct.calcsize('<' + fmt); return v
    def i8(self): return self.raw('B')[0]
    def i16(self): return self.raw('h')[0]
    def u16(self): return self.raw('H')[0]
    def i32(self): return self.raw('i')[0]
    def u32(self): return self.raw('I')[0]
    def i64(self): return self.raw('q')[0]
    def f(self): return self.raw('f')[0]
    def d(self): return self.raw('d')[0]
    def bool(self): return self.i8() != 0
    def v3(self): return self.raw('3f')
    def str(self):
        n = sh = 0
        while True:
            c = self.i8(); n |= (c & 0x7F) << sh; sh += 7
            if not c & 0x80: break
        v = self.b[self.p:self.p + n].decode('utf-8'); self.p += n; return v
    def bytes(self, n=None):
        n = self.i32() if n is None else n
        v = self.b[self.p:self.p + n]; self.p += n; return v
    def num_items(self):
        a = self.i8()
        return ((a & 0x7F) << 8) | self.i8() if a & 0x80 else a
    def small_rot(self):
        a = self.u16()
        if a & 0x8000: return (0.0, (a & 0x7FFF) * 0.5, 0.0)
        v = (a << 16) | self.u16()
        return ((v & 0x3FF) * 0.5, ((v >> 10) & 0x3FF) * 0.5, ((v >> 20) & 0x3FF) * 0.5)
    def end(self): return self.p >= len(self.b)


def fmt(v):
    return '%.9g' % v if isinstance(v, float) else str(v)


def zdo(r, session):
    flags = r.u16()
    pos = (float(r.i16()), 0.0, float(r.i16())) if flags & 0x2000 else r.v3()
    prefab = r.i32()
    rot = r.small_rot() if flags & 0x1000 else (0.0, 0.0, 0.0)
    conn = None
    data = []
    if flags & 0xFF:
        if flags & 1:
            conn = (r.i8(), r.i32())
        readers = [(2, 'f', r.f), (4, 'v', r.v3), (8, 'q', lambda: r.raw('4f')), (0x10, 'i', r.i32),
                   (0x20, 'l', r.i64), (0x40, 's', r.str), (0x80, 'b', lambda: r.bytes().hex())]
        for bit, tag, read in readers:
            if flags & bit:
                for _ in range(r.num_items()):
                    k = r.i32()
                    v = read()
                    if k not in session:
                        data.append((tag, k, v))
    head = '%d %s r%s p%d d%d t%d' % (prefab, ','.join(map(fmt, pos)), ','.join(map(fmt, rot)), flags >> 8 & 1,
                                     flags >> 9 & 1, flags >> 10 & 3)
    if conn:
        head += ' c%d' % conn[0]
    body = ' '.join('%s%d=%s' % (t, k, ','.join(map(fmt, v)) if isinstance(v, tuple) else fmt(v)) for t, k, v in sorted(data, key=lambda x: (x[0], x[1])))
    return head + (' ' + body if body else ''), conn


def load(world_dir):
    names = os.listdir(world_dir)
    oks = sorted(int(m.group(1)) for n in names if (m := re.fullmatch(r'_main\.(\d+)\.ok', n)))
    n = oks[-1] if oks else max(int(m.group(1)) for x in names if (m := re.fullmatch(r'_main\.(\d+)\.fwl2', x)))
    path = lambda ext: os.path.join(world_dir, '_main.%d.%s' % (n, ext))
    out = {'save': n}

    r = R(open(path('fwl2'), 'rb').read())
    r = R(r.bytes())
    out['fwl2'] = dict(version=r.i32(), name=r.str(), seedName=r.str(), seed=r.i32(), uid=r.i64(), worldGen=r.i32(),
                       needsDB=r.bool(), startKeys=[r.str() for _ in range(r.i32())],
                       history=[[r.str() for _ in range(4)] for _ in range(r.i32())])

    r = R(open(path('db2'), 'rb').read())
    out['db2_version'] = r.i32()
    out['netTime'] = r.d()
    z = R(gzip.decompress(r.bytes()))
    zones = sorted(z.raw('2h') for _ in range(z.i32()))
    out['locationVersion'] = z.i32()
    out['globalKeys'] = sorted(z.str() for _ in range(z.i32()))
    out['locationsGenerated'] = z.bool()
    out['locations'] = sorted((z.i32(), z.v3(), z.bool()) for _ in range(z.i32()))
    out['zones'] = zones
    out['randEvent'] = (r.f(), r.str(), r.f(), r.v3())
    out['persistent'] = r.bytes() if len(r.b) - r.p >= 4 else None
    assert r.end(), 'db2 tail %d bytes' % (len(r.b) - r.p)

    r = R(open(path('chunks'), 'rb').read())
    r.u16()
    total = r.i32()
    chunks = [(r.u16(), r.i8(), r.u32(), r.i32()) for _ in range(r.i32())]
    assert r.end()
    session = {stable_hash(s) for s in SESSION}
    zdos, conns, portal = [], Counter(), 0
    for chunk, size, ver, num in chunks:
        c = R(open(os.path.join(world_dir, '%02x_%02x__%d_%d.chunk' % (chunk >> 8, chunk & 0xFF, size, ver)), 'rb').read())
        cver, count = c.i16(), c.i32()
        assert count == num, (chunk, count, num)
        assert cver >= 40, cver
        for _ in range(count):
            line, conn = zdo(c, session)
            zdos.append(('P ' if (chunk, size) == (1, 0) else '') + line)
            if conn:
                conns[conn[0]] += 1
        assert c.end(), 'chunk %x tail' % chunk
        portal += num if (chunk, size) == (1, 0) else 0
    assert len(zdos) == total, (len(zdos), total)
    out['zdos'] = sorted(zdos)
    out['connections'] = dict(conns)
    out['chunks'] = len(chunks)
    out['portals'] = portal
    return out


def main():
    d = load(sys.argv[1])
    if '--summary' in sys.argv:
        print('save %d, %d chunks, %d zdos (%d in portal chunk), %d zones, %d locations (%d placed), %d global keys' % (
            d['save'], d['chunks'], len(d['zdos']), d['portals'], len(d['zones']), len(d['locations']),
            sum(1 for l in d['locations'] if l[2]), len(d['globalKeys'])))
        print('fwl2', d['fwl2'])
        print('netTime', d['netTime'], 'locationVersion', d['locationVersion'], 'locationsGenerated', d['locationsGenerated'])
        print('randEvent', d['randEvent'], 'persistent bytes', None if d['persistent'] is None else len(d['persistent']))
        print('connections by type', d['connections'])
        return
    for k in ('fwl2', 'netTime', 'locationVersion', 'locationsGenerated', 'globalKeys', 'randEvent'):
        print(k, d[k])
    print('persistent', None if d['persistent'] is None else d['persistent'].hex())
    for z in d['zones']:
        print('zone', *z)
    for h, p, placed in d['locations']:
        print('loc', h, ','.join(map(fmt, p)), int(placed))
    for line in d['zdos']:
        print(line)


if __name__ == '__main__':
    main()
