#!/usr/bin/env bash
# Round trip of a vanilla 1.0 save through Avledet: load a copy, stop with SIGTERM (it saves),
# then diff the canonical dumps of the save before and after.
# Usage: tools/avl-save-test.sh <worlds_local/<world> dir> [seconds to run]
# A missing dir starts a new world of that name, then only the summary is printed.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$(realpath -m "$1")"
WORLD="$(basename "$SRC")"
WAIT="${2:-20}"
RUN="$ROOT/run/save-test"

rm -rf "$RUN" && mkdir -p "$RUN/home/.steam/sdk64" "$RUN/save/worlds_local"
tar -C "$ROOT/avledet/build-Release/bin" --exclude='data/worlds/*' -cf - . | tar -C "$RUN" -xf -
cp "$ROOT/re/redist/linux64/steamclient.so" "$RUN/home/.steam/sdk64/"
for f in prefabs features vegetation randomEvents dungeons colliders; do cp "$ROOT/re/pkg-1.0.21/pkg/$f.pkg" "$RUN/data/"; done
for f in altbiomes locations_meta awake prefab_terrainmods loot; do cp "$ROOT/re/pkg-1.0.21/json/$f.json" "$RUN/data/"; done
sed -i "s/^  world: .*/  world: $WORLD\n  savedir: \/srv\/save/" "$RUN/data/server.yml"
if [ -d "$SRC" ]; then
    cp -r "$SRC" "$RUN/save/worlds_local/$WORLD"
    cp -r "$SRC" "$RUN/before"
fi

docker rm -f avl-save-test >/dev/null 2>&1 || true
docker run -d --name avl-save-test -u "$(id -u):$(id -g)" -e HOME=/srv/home -e LD_LIBRARY_PATH=/srv \
    -e TRACY_NO_INVARIANT_CHECK=1 -v "$RUN:/srv" -w /srv vrun:1 ./avledet_server > /dev/null
sleep "$WAIT"
docker stop -t 120 avl-save-test > /dev/null
docker logs avl-save-test > "$RUN/server.log" 2>&1
docker rm avl-save-test > /dev/null
grep -E 'Loaded world|Loaded .* zdos|Dropped|Connected|World save|save failed|Failed|ERROR|terminate' "$RUN/server.log" | cut -c1-200 || true

python3 -I "$ROOT/tools/save_dump.py" "$RUN/save/worlds_local/$WORLD" --summary
[ -d "$RUN/before" ] || exit 0
python3 -I "$ROOT/tools/save_dump.py" "$RUN/before" > "$RUN/before.txt"
python3 -I "$ROOT/tools/save_dump.py" "$RUN/save/worlds_local/$WORLD" > "$RUN/after.txt"
if diff -q "$RUN/before.txt" "$RUN/after.txt" > /dev/null; then
    echo "round trip: identical"
else
    echo "round trip: $(diff "$RUN/before.txt" "$RUN/after.txt" | grep -c '^[<>]') differing lines (diff $RUN/before.txt $RUN/after.txt)"
fi
