#!/usr/bin/env bash
# Build Avledet, run its worldgen probe for a seed and compare with the vanilla probe.
# Usage: tools/avl-probe.sh [seed]   (vanilla probe must exist in re/probe-<seed>/out)
# With re/zones-<seed>.txt present, those zones are generated and compared too.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SEED="${1:-HrrlYr05vV}"
VAN="$ROOT/re/probe-$SEED/out"
RUN="$ROOT/run/avl-probe-$SEED-$(date +%s)"

if ! "$ROOT/build/build-avledet.sh" Release > "$ROOT/run/build.log" 2>&1; then
    grep -E 'error|FAILED' "$ROOT/run/build.log" | head -20
    exit 1
fi

mkdir -p "$RUN/home/.steam/sdk64"
tar -C "$ROOT/avledet/build-Release/bin" --exclude='data/worlds/*' -cf - . | tar -C "$RUN" -xf -
cp "$ROOT/re/redist/linux64/steamclient.so" "$RUN/home/.steam/sdk64/"
for f in prefabs features vegetation randomEvents dungeons colliders; do cp "$ROOT/re/pkg-1.0.21/pkg/$f.pkg" "$RUN/data/"; done
cp "$ROOT/re/pkg-1.0.21/json/altbiomes.json" "$ROOT/re/pkg-1.0.21/json/locations_meta.json" "$ROOT/re/pkg-1.0.21/json/awake.json" "$ROOT/re/pkg-1.0.21/json/prefab_terrainmods.json" "$ROOT/re/pkg-1.0.21/json/loot.json" "$RUN/data/"
sed -i "s/^  seed: .*/  seed: $SEED/" "$RUN/data/server.yml"
ZONES=()
if [ -f "$ROOT/re/zones-$SEED.txt" ]; then
    cp "$ROOT/re/zones-$SEED.txt" "$RUN/zones.txt"
    ZONES=(-e AVL_PROBE_ZONES=/srv/zones.txt)
fi

docker run --rm -u "$(id -u):$(id -g)" -e HOME=/srv/home -e LD_LIBRARY_PATH=/srv -e TRACY_NO_INVARIANT_CHECK=1 \
    -e AVL_PROBE_OUT=/srv/probe -e AVL_PROBE_PHYS=/srv/phys.txt "${ZONES[@]}" -v "$RUN:/srv" -w /srv vrun:1 ./avledet_server > "$RUN/probe.log" 2>&1 || true
grep -q 'probe written' "$RUN/probe.log" || { tail -20 "$RUN/probe.log"; exit 1; }

echo "$RUN" > "$ROOT/run/LAST_PROBE"
python3 -I "$ROOT/tools/compare_probe.py" "$VAN" "$RUN/probe"
# zones reference: ghost zones one at a time with the vegetation raycasts logged (PROBE_PHYS=1)
# the live dedicated server spawns zones in SpawnMode.Ghost (PROBE_GHOST=1 reference)
ZREF="$ROOT/re/probe-ghost-$SEED/out"
if [ ${#ZONES[@]} -gt 0 ] && [ -f "$ZREF/zones.txt" ]; then
    python3 -I "$ROOT/tools/compare_zones.py" "$ZREF/zones.txt" "$RUN/probe/zones.txt"
    python3 -I "$ROOT/tools/compare_location_parts.py" "$ZREF/zones.txt" "$RUN/probe/zones.txt" \
        "$VAN/locations.json"
    python3 -I "$ROOT/tools/compare_bits.py" "$ZREF/zones.txt" "$RUN/probe/zones.txt"
fi
