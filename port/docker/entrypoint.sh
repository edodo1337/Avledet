#!/bin/sh
# Data from the image and the game data dump on every start, so new versions replace old files.
# server.yml and worlds_local/ in /srv/data are kept. A missing server.yml is created by the server.
set -e

if [ ! -f /srv/game-data/prefabs.pkg ]; then
    echo "No game data in game-data/out, run: docker compose run --rm dump" >&2
    exit 1
fi
cp -r /opt/avledet/data/. /srv/game-data/. /srv/data/

# set <section> <key> <value>: replaces the key in its server.yml section or adds it
Y=/srv/data/server.yml
set_key() {
    [ -n "$3" ] || return 0
    touch $Y
    SEC="$1" KEY="$2" VAL="$3" awk '
        BEGIN { sec = ENVIRON["SEC"]; line = "  " ENVIRON["KEY"] ": " ENVIRON["VAL"] }
        /^[^ #]/ { if (cur == sec && !done) { print line; done = 1 } cur = $0; sub(/:.*/, "", cur) }
        cur == sec && index($0, "  " ENVIRON["KEY"] ":") == 1 { if (!done) print line; done = 1; next }
        { print }
        END { if (!done) { if (cur != sec) print sec ":"; print line } }
    ' $Y > $Y.tmp && mv $Y.tmp $Y
}
quote() {
    [ -n "$1" ] && printf '"%s"' "$(printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g')"
}
set_key server name "$(quote "$SERVER_NAME")"
set_key server password "$(quote "$SERVER_PASSWORD")"
set_key server public "$SERVER_PUBLIC"
set_key players max-online "$MAX_PLAYERS"
set_key world world "$(quote "$WORLD_NAME")"
set_key world seed "$(quote "$WORLD_SEED")"

mkdir -p "$HOME/.steam/sdk64"
ln -sf /opt/avledet/steamclient.so "$HOME/.steam/sdk64/steamclient.so"

exec ./avledet_server
