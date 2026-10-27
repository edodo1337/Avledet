#!/usr/bin/env bash
# Load a world save in the plain vanilla 1.0 server (no plugins), stop it with SIGINT so it saves,
# and dump the result. Usage: tools/vanilla-save-test.sh <worlds_local/<world> dir> [seconds]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$(realpath "$1")"
WORLD="$(basename "$SRC")"
WAIT="${2:-60}"
WORK="$ROOT/run/vanilla-save-test"

rm -rf "$WORK" && mkdir -p "$WORK/save/worlds_local" "$WORK/home"
cp -r "$SRC" "$WORK/save/worlds_local/$WORLD"
cp -r "$SRC" "$WORK/before"

docker rm -f vanilla-save-test >/dev/null 2>&1 || true
docker run -d --name vanilla-save-test -u "$(id -u):$(id -g)" -e HOME=/work/home -e SteamAppId=892970 \
    -v "$WORK:/work" -v "$ROOT/re/server-1.0:/game:ro" -v "$ROOT/re/redist:/redist:ro" -w /game vrun:1 bash -c '
        mkdir -p /work/home/.steam/sdk64 && ln -sf /redist/linux64/steamclient.so /work/home/.steam/sdk64/steamclient.so
        export LD_LIBRARY_PATH=./linux64
        exec ./valheim_server.x86_64 -nographics -batchmode -name SaveTest -port 2456 -world '"$WORLD"' \
            -password secret123 -public 0 -savedir /work/save -logfile /work/server.log' > /dev/null
sleep "$WAIT"
docker kill -s INT vanilla-save-test > /dev/null
docker wait vanilla-save-test > /dev/null || true
docker rm vanilla-save-test > /dev/null
grep -E 'LoadWorld|LoadChunks - Starting|LoadChunks done|orphan|unknown prefab|not supported|ConnectPortals|ConnectSpawners|Failed to find location|incompatible|load failed|World save \(5/5\)|Exception' "$WORK/server.log" | cut -c1-220 || true

python3 -I "$ROOT/tools/save_dump.py" "$WORK/before" > "$WORK/before.txt"
python3 -I "$ROOT/tools/save_dump.py" "$WORK/save/worlds_local/$WORLD" > "$WORK/after.txt"
python3 -I "$ROOT/tools/save_dump.py" "$WORK/save/worlds_local/$WORLD" --summary
diff "$WORK/before.txt" "$WORK/after.txt" | head -20 || true
