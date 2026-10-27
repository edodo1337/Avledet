#!/usr/bin/env bash
# Avledet for a live client: Steam sockets on UDP 2456-2457 published by Docker Desktop, so the Windows
# Valheim client joins 127.0.0.1:2456 (password secret123). The world stays in run/live/save between runs.
# Usage: tools/run-live.sh [seed]     stop: docker stop avl-live (it saves)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SEED="${1:-HrrlYr05vV}"
RUN="$ROOT/run/live"

mkdir -p "$RUN/home/.steam/sdk64" "$RUN/save/worlds_local"
tar -C "$ROOT/avledet/build-Release/bin" --exclude='data/worlds/*' --exclude='data/server.yml' -cf - . | tar -C "$RUN" -xf -
[ -f "$RUN/data/server.yml" ] || cp "$ROOT/avledet/build-Release/bin/data/server.yml" "$RUN/data/server.yml"
cp "$ROOT/re/redist/linux64/steamclient.so" "$RUN/home/.steam/sdk64/"
Y="$RUN/data/server.yml"
sed -i "s/^  world: .*/  world: Live\n  savedir: \/srv\/save/; s/^  seed: .*/  seed: $SEED/; s/^  password: .*/  password: secret123/" "$Y"
awk '!seen[$0]++ || !/savedir/' "$Y" > "$Y.tmp" && mv "$Y.tmp" "$Y"

docker rm -f avl-live >/dev/null 2>&1 || true
docker run -d --name avl-live -u "$(id -u):$(id -g)" -e HOME=/srv/home -e LD_LIBRARY_PATH=/srv \
    -e TRACY_NO_INVARIANT_CHECK=1 -p 2456:2456/udp -p 2457:2457/udp -v "$RUN:/srv" -w /srv vrun:1 ./avledet_server >/dev/null
echo "started; logs: docker logs -f avl-live; log file: $RUN/logs"
