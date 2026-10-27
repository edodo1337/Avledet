#!/usr/bin/env python3
"""Pick test zones from a vanilla probe: one zone per interesting location type plus
location-free zones per biome. Usage: pick_zones.py <probe_out_dir> <out.txt> [per_biome]"""
import json
import math
import struct
import sys

out_dir, out_file = sys.argv[1], sys.argv[2]
per_biome = int(sys.argv[3]) if len(sys.argv) > 3 else 3
locs = json.load(open(f"{out_dir}/locations.json"))
zone = lambda x, z: (math.floor((x + 32) / 64), math.floor((z + 32) / 64))

picked = {}
for l in sorted(locs, key=lambda l: l["name"]):
    picked.setdefault(l["name"], zone(l["pos"][0], l["pos"][2]))
zones = set(picked.values())

# location-free zones per biome from the 64 m sample grid
data = open(f"{out_dir}/samples_biome.i32", "rb").read()
biomes = struct.unpack(f"<{len(data) // 4}i", data)
n = math.isqrt(len(biomes))
loc_zones = {zone(l["pos"][0], l["pos"][2]) for l in locs}
count = {}
for i in range(0, len(biomes), 97):  # spread over the map
    x, y = -10560 + (i % n) * 64, -10560 + (i // n) * 64
    if x * x + y * y > 9800 ** 2:
        continue
    z = zone(x, y)
    b = biomes[i]
    if z in loc_zones or count.get(b, 0) >= per_biome:
        continue
    count[b] = count.get(b, 0) + 1
    zones.add(z)

with open(out_file, "w") as f:
    for z in sorted(zones):
        f.write(f"{z[0]} {z[1]}\n")
print(f"{len(zones)} zones ({len(picked)} location types, {sum(count.values())} vegetation zones)")
