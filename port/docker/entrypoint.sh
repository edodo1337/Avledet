#!/bin/sh
# Game data from the image on every start, so a new image updates it.
# server.yml and worlds_local/ in /srv/data are kept. A missing server.yml is created by the server.
set -e

cp -r /opt/avledet/data/. /srv/data/
mkdir -p "$HOME/.steam/sdk64"
ln -sf /opt/avledet/steamclient.so "$HOME/.steam/sdk64/steamclient.so"

exec ./avledet_server
