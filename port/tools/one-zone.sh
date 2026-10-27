#!/usr/bin/env bash
# Generate one zone in vanilla (ghost mode, the live server's) and in Avledet (already built), print the
# placed rooms side by side and the bit for bit object diff.
# Usage: tools/one-zone.sh <zx> <zy> [seed]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZX=$1; ZY=$2; SEED=${3:-HrrlYr05vV}
W=$ROOT/run/one-$ZX-$ZY
mkdir -p $W; printf "%s %s\n" $ZX $ZY > $W/zones.txt
"$ROOT/tools/run-vanilla-plugin.sh" "$ROOT/tools/worldgen-probe/bin/Release/net462/WorldgenProbe.dll" $W/van \
    -e PROBE_SEED=$SEED -e PROBE_SEEDED_ROTATION=1 -e PROBE_GHOST=1 -e PROBE_ROOMS=1 -e PROBE_OUT=/work/out \
    -e PROBE_ZONES=/zones.txt -v "$W/zones.txt:/zones.txt:ro" > /dev/null
grep -a -E "\] (room|endcaps|try) " $W/van/game/BepInEx/LogOutput.log | sed -E "s/.*\] //" > $W/van-rooms.txt
RUN=$W/avl; rm -rf $RUN; mkdir -p $RUN/home/.steam/sdk64
tar -C "$ROOT/avledet/build-Release/bin" --exclude='data/worlds/*' -cf - . | tar -C $RUN -xf -
cp "$ROOT/re/redist/linux64/steamclient.so" $RUN/home/.steam/sdk64/
for f in prefabs features vegetation randomEvents dungeons colliders; do cp "$ROOT/re/pkg-1.0.20/pkg/$f.pkg" $RUN/data/; done
for f in altbiomes locations_meta awake prefab_terrainmods loot; do cp "$ROOT/re/pkg-1.0.20/json/$f.json" $RUN/data/; done
sed -i "s/^  seed: .*/  seed: $SEED/; s/^  log-level: .*/  log-level: debug/" $RUN/data/server.yml
cp $W/zones.txt $RUN/zones.txt
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/srv/home -e LD_LIBRARY_PATH=/srv -e TRACY_NO_INVARIANT_CHECK=1 \
    -e AVL_PROBE_OUT=/srv/probe -e AVL_PROBE_ZONES=/srv/zones.txt -v "$RUN:/srv" -w /srv vrun:1 ./avledet_server > $RUN/probe.log 2>&1 || true
grep -a -o -E "(room DG_|endcaps |try ).*" $RUN/probe.log > $W/avl-rooms.txt || true
wc -l $W/van-rooms.txt $W/avl-rooms.txt
python3 -I "$ROOT/tools/compare_bits.py" $W/van/out/zones.txt $RUN/probe/zones.txt
