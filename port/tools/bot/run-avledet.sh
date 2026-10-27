#!/usr/bin/env bash
# Run a bot scenario against Avledet (fresh world, experimental.server-tcp).
# Usage: tools/bot/run-avledet.sh <scenario> <out dir> [seed]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCEN="$1"
RUN="$(realpath -m "$2")"
SEED="${3:-HrrlYr05vV}"
PORT=2459

rm -rf "$RUN" && mkdir -p "$RUN/home/.steam/sdk64" "$RUN/save/worlds_local"
tar -C "$ROOT/avledet/build-Release/bin" --exclude='data/worlds/*' -cf - . | tar -C "$RUN" -xf -
cp "$ROOT/re/redist/linux64/steamclient.so" "$RUN/home/.steam/sdk64/"
for f in prefabs features vegetation randomEvents dungeons colliders; do cp "$ROOT/re/pkg-1.0.20/pkg/$f.pkg" "$RUN/data/"; done
for f in altbiomes locations_meta awake prefab_terrainmods loot; do cp "$ROOT/re/pkg-1.0.20/json/$f.json" "$RUN/data/"; done
Y="$RUN/data/server.yml"
sed -i "s/^  world: .*/  world: BotTest\n  savedir: \/srv\/save/; s/^  seed: .*/  seed: $SEED/; s/^  password: .*/  password: secret123/" "$Y"
sed -i "s/^  server-tcp: .*/  server-tcp: true/" "$Y"
# extra server.yml edits for a scenario, e.g. YML_SED="s/^  interval: .*/  interval: 10s/"
[ -n "${YML_SED:-}" ] && sed -i "$YML_SED" "$Y"

# ADMIN=<host>: that peer is admin (adminlist.txt in the save dir)
[ -n "${ADMIN:-}" ] && echo "$ADMIN" > "$RUN/save/adminlist.txt"
docker rm -f vbot-avledet >/dev/null 2>&1 || true
docker run -d --name vbot-avledet -u "$(id -u):$(id -g)" -e HOME=/srv/home -e LD_LIBRARY_PATH=/srv \
    -e TRACY_NO_INVARIANT_CHECK=1 -p $PORT:2456 -v "$RUN:/srv" -w /srv vrun:1 ./avledet_server >/dev/null

for _ in $(seq 60); do
    docker logs vbot-avledet 2>&1 | grep -q -E 'Server started|Listening|started on' && break
    sleep 1
done
sleep 3
python3 -I "$ROOT/tools/bot/vbot.py" 127.0.0.1 $PORT "$SCEN" "$RUN/bot.jsonl" || true
docker stop -t 60 vbot-avledet >/dev/null
docker logs vbot-avledet > "$RUN/server.log" 2>&1
docker rm vbot-avledet >/dev/null
grep -E 'ERROR|WARN|terminate|Failed' "$RUN/server.log" | cut -c1-200 | head -20 || true
