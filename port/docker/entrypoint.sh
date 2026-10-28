#!/bin/sh
# Data from the image and the game data dump on every start, so new versions replace old files.
# server.yml and worlds_local/ in /srv/data are kept. A missing server.yml is created by the server.
set -e

if [ ! -f /srv/game-data/prefabs.pkg ]; then
    echo "No game data in game-data/out, run: docker compose run --rm dump" >&2
    exit 1
fi
cp -r /opt/avledet/data/. /srv/game-data/. /srv/data/
mkdir -p "$HOME/.steam/sdk64"
ln -sf /opt/avledet/steamclient.so "$HOME/.steam/sdk64/steamclient.so"

exec ./avledet_server
