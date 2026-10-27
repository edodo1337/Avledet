#!/usr/bin/env bash
# Run the vanilla 1.0 dedicated server once with a single BepInEx plugin, fresh world.
# Usage: tools/run-vanilla-plugin.sh <plugin.dll> <work-dir> [docker -e args...]
# The plugin is expected to quit the server when it is done.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DLL="$(realpath "$1")"
WORK="$(realpath -m "$2")"
shift 2

GAME="$WORK/game"
rm -rf "$WORK" && mkdir -p "$WORK/save" "$WORK/home"
# game files are hardlinked, BepInEx gets its own copy with only this plugin
cp -al "$ROOT/re/server-1.0" "$GAME"
cp -r "$ROOT/re/bepinex/BepInExPack_Valheim/." "$GAME/"
mkdir -p "$GAME/BepInEx/plugins" && cp "$DLL" "$GAME/BepInEx/plugins/"

docker run --rm -u "$(id -u):$(id -g)" -e HOME=/work/home -e SteamAppId=892970 "$@" \
    -v "$WORK:/work" -v "$ROOT/re/redist:/redist:ro" -w /work/game vrun:1 bash -c '
        mkdir -p /work/home/.steam/sdk64 && ln -sf /redist/linux64/steamclient.so /work/home/.steam/sdk64/steamclient.so
        export DOORSTOP_ENABLED=1 DOORSTOP_TARGET_ASSEMBLY=./BepInEx/core/BepInEx.Preloader.dll
        export LD_LIBRARY_PATH=./doorstop_libs:./linux64 LD_PRELOAD=libdoorstop_x64.so
        exec ./valheim_server.x86_64 -nographics -batchmode -name Probe -port 2456 -world Probe \
            -password secret123 -public 0 -savedir /work/save -logfile /work/server.log' >/dev/null 2>&1 || true

grep -E '\[(Info|Error|Warning) *:(WorldgenProbe|AvlDumpServer)\]' "$GAME/BepInEx/LogOutput.log" || true
