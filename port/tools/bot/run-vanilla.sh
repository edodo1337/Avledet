#!/usr/bin/env bash
# Run a bot scenario against the vanilla 1.0 server (fresh world, tcp-listen plugin).
# Usage: tools/bot/run-vanilla.sh <scenario> <out dir> [seed]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCEN="$1"
WORK="$(realpath -m "$2")"
SEED="${3:-HrrlYr05vV}"
PORT=2459

GAME="$WORK/game"
rm -rf "$WORK" && mkdir -p "$WORK/save" "$WORK/home"
cp -al "$ROOT/re/server-1.0" "$GAME"
cp -r "$ROOT/re/bepinex/BepInExPack_Valheim/." "$GAME/"
mkdir -p "$GAME/BepInEx/plugins" && cp "$ROOT/tools/tcp-listen/bin/Release/net462/TcpListen.dll" "$GAME/BepInEx/plugins/"

# ADMIN=<host>: that peer is admin (adminlist.txt in the save dir)
[ -n "${ADMIN:-}" ] && echo "$ADMIN" > "$WORK/save/adminlist.txt"
docker rm -f vbot-vanilla >/dev/null 2>&1 || true
docker run -d --name vbot-vanilla -u "$(id -u):$(id -g)" -e HOME=/work/home -e SteamAppId=892970 \
    -e TCP_PORT=$PORT -e BOT_SEED="$SEED" -e BOT_SEEDED_ROTATION=1 -p $PORT:$PORT \
    -v "$WORK:/work" -v "$ROOT/re/redist:/redist:ro" -w /work/game vrun:1 bash -c '
        mkdir -p /work/home/.steam/sdk64 && ln -sf /redist/linux64/steamclient.so /work/home/.steam/sdk64/steamclient.so
        export DOORSTOP_ENABLED=1 DOORSTOP_TARGET_ASSEMBLY=./BepInEx/core/BepInEx.Preloader.dll
        export LD_LIBRARY_PATH=./doorstop_libs:./linux64 LD_PRELOAD=libdoorstop_x64.so
        exec ./valheim_server.x86_64 -nographics -batchmode -name BotTest -port 2456 -world BotTest \
            -password secret123 -public 0 -savedir /work/save -logfile /work/server.log' >/dev/null

# ready when the world is loaded
for _ in $(seq 180); do
    grep -q 'Game server connected' "$WORK/server.log" 2>/dev/null && break
    sleep 1
done
python3 -I "$ROOT/tools/bot/vbot.py" 127.0.0.1 $PORT "$SCEN" "$WORK/bot.jsonl" || true
docker kill -s INT vbot-vanilla >/dev/null; docker wait vbot-vanilla >/dev/null || true
docker rm vbot-vanilla >/dev/null
grep -E 'TcpListen|Exception|Error' "$GAME/BepInEx/LogOutput.log" | head -20 || true
