#!/bin/bash
# Dumps the game data Avledet needs from the official dedicated server into /game-data/out.
# The server (SteamCMD app 896660) and BepInEx stay in /game-data for the next run.
set -euo pipefail

BEPINEX=5.4.2351
NETWORK=41
D=/game-data

# SteamCMD sometimes fails with "Missing configuration" on the first try
for try in 1 2 3; do
    steamcmd +force_install_dir $D/server +login anonymous +app_info_update 1 +app_update 896660 validate +quit \
        && [ -x $D/server/valheim_server.x86_64 ] && break
    [ $try = 3 ] && { echo "SteamCMD failed to download the server" >&2; exit 1; }
done

if [ ! -d $D/bepinex/BepInExPack_Valheim ]; then
    curl -fsSL -o /tmp/bepinex.zip https://thunderstore.io/package/download/denikson/BepInExPack_Valheim/$BEPINEX/
    unzip -q -o /tmp/bepinex.zip -d $D/bepinex
fi

dotnet build /dumper -c Release -o /tmp/dumper -v quiet -nologo -p:MSBuildWarningsAsMessages=MSB3277 -p:Managed=$D/server/valheim_server_Data/Managed \
    -p:BepCore=$D/bepinex/BepInExPack_Valheim/BepInEx/core

# a separate copy of the server with BepInEx and only the dumper plugin
GAME=$D/run
rm -rf $GAME
cp -al $D/server $GAME
cp -r $D/bepinex/BepInExPack_Valheim/. $GAME/
mkdir -p $GAME/BepInEx/plugins /tmp/save "$HOME/.steam/sdk64"
cp /tmp/dumper/AvlDumpServer.dll $GAME/BepInEx/plugins/
[ -e "$HOME/.steam/sdk64/steamclient.so" ] || ln -s "$HOME/.local/share/Steam/steamcmd/linux64/steamclient.so" "$HOME/.steam/sdk64/"

echo "Running the dedicated server with the dumper, this takes a few minutes"
cd $GAME
export SteamAppId=892970 DOORSTOP_ENABLED=1 DOORSTOP_TARGET_ASSEMBLY=./BepInEx/core/BepInEx.Preloader.dll
LD_LIBRARY_PATH=./doorstop_libs:./linux64 LD_PRELOAD=libdoorstop_x64.so timeout 900 \
    ./valheim_server.x86_64 -nographics -batchmode -name Dump -port 2456 -world Dump -password secret123 \
    -public 0 -savedir /tmp/save -logfile /tmp/server.log >/dev/null 2>&1 || true
grep -E '\[(Info|Error|Warning) *:AvlDumpServer\]' BepInEx/LogOutput.log || true

J=$GAME/dumped/json
OUT=$D/out
rm -rf $OUT && mkdir -p $OUT
for f in prefabs features vegetation randomEvents dungeons colliders; do cp $GAME/dumped/pkg/$f.pkg $OUT/; done
for f in altbiomes awake event_objects loot persistentEvents prefab_terrainmods server_options version; do
    cp $J/$f.json $OUT/
done
python3 -I /opt/make_location_meta.py $J/locations.json $OUT/locations_meta.json
chown -R "${PUID:-1000}:${PGID:-1000}" $D

cat $OUT/version.json; echo
python3 -I -c "import json,sys; v=json.load(open('$OUT/version.json')); sys.exit(v['network'] != $NETWORK)" || {
    echo "This server has network version $NETWORK, the downloaded game has another one. Clients of this game version cannot join." >&2
    exit 1
}
echo "Game data written to game-data/out"
