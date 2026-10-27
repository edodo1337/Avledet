#!/usr/bin/env python3
"""Extract per-location metadata Avledet needs (ZoneLocation.m_name, alt biome parent)
from the JSON dump. Usage: make_location_meta.py <locations.json> <out.json>"""
import json
import sys

locs = json.load(open(sys.argv[1]))
meta = {}
for l in locs:
    z = l["zoneLocation"]
    meta[z["m_prefabName"]] = {"name": z["m_name"], "altBiomeParent": z.get("AltBiomeParent") or ""}
json.dump(meta, open(sys.argv[2], "w"), indent=0)
print(f"{len(meta)} locations")
