#!/usr/bin/env python3
"""Match rate of location parts (location views, dungeon room views, interiors above 1000 m)
apart from vegetation, per location type, location parts compared in the location frame.
Usage: compare_location_parts.py <vanilla zones.txt> <avledet zones.txt> <vanilla locations.json> [top]"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # -I drops the script dir
from compare_zones import load, match, to_local

JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "re", "pkg-1.0.20", "json")
van, avl = load(sys.argv[1]), load(sys.argv[2])
zone = lambda x, z: (math.floor((x + 32) / 64), math.floor((z + 32) / 64))
locz = {}
for l in json.load(open(sys.argv[3])):
    locz.setdefault(zone(l['pos'][0], l['pos'][2]), l['name'])
views = {l['zoneLocation']['m_prefabName']: {v['prefab'] for v in l.get('views', [])} for l in json.load(open(os.path.join(JSON, 'locations.json')))}
rooms = set()
for r in json.load(open(os.path.join(JSON, 'rooms.json'))):
    rooms |= {v['prefab'] for v in r.get('views', [])}
# prefabs that vegetation also spawns count as location parts only inside interiors
veg = {v['m_prefab'] for v in json.load(open(os.path.join(JSON, 'vegetation.json')))}
rooms -= veg
views = {k: v - veg for k, v in views.items()}
proxies = lambda objs: {k[:2]: v[0] for k, v in objs.items() if k[2] == "LocationProxy" and len(v) == 1}
vp, ap = proxies(van), proxies(avl)
zones = {k[:2] for k in van}
stats, other = {}, [0, 0, 0]
for key in set(van) | set(avl):
    z = key[:2]
    if z not in zones: continue
    loc = locz.get(z) if z in vp else None
    part = lambda o: loc and (key[2] in views.get(loc, ()) or key[2] in rooms or o[1] > 1000)
    for is_part in (True, False):
        va = [o for o in van.get(key, []) if bool(part(o)) == is_part]
        aa = [o for o in avl.get(key, []) if bool(part(o)) == is_part]
        p, m, e = match(va, aa)
        if is_part and m and e and z in ap:
            lp, m, e = match([to_local(o, vp[z]) for o in m], [to_local(o, ap[z]) for o in e])
            p += lp
        s = stats.setdefault(loc, [0, 0, 0]) if is_part else other
        s[0] += len(va); s[1] += len(aa); s[2] += len(p)
print('vegetation and the rest: vanilla %d avledet %d matched %d (%.1f%%)' % (other[0], other[1], other[2], 100*other[2]/other[0]))
tv = sum(s[0] for s in stats.values()); ta = sum(s[1] for s in stats.values()); tm = sum(s[2] for s in stats.values())
print('location parts: vanilla %d avledet %d matched %d (%.1f%%), locations 100%%: %d/%d' % (tv, ta, tm, 100*tm/tv,
      sum(1 for s in stats.values() if s[0] == s[1] == s[2]), len(stats)))
rows = sorted(stats.items(), key=lambda r: -(r[1][0] + r[1][1] - 2*r[1][2]))
for n, s in rows[:int(sys.argv[4]) if len(sys.argv) > 4 else 30]:
    if s[0] == s[1] == s[2]: break
    print('%-32s %6d %6d %6d %5.1f%%' % (n, s[0], s[1], s[2], 100*s[2]/max(1,s[0])))
