# Avledet for Valheim 1.0.20

This branch (`port-1.0`) ports Avledet to Valheim 1.0.20 (network 41, world 42) and checks it against the
vanilla dedicated server. Unmodded 1.0.20 clients join over Steam.

What matches vanilla, checked by the tools here:

- terrain and biomes: 20000 random points bit for bit;
- locations, dungeons, vegetation and chest loot of generated zones (43008 objects: 134 heights and 46
  rotations differ in the last bits, mostly plants on rock mesh colliders that vanilla places with PhysX);
- network traffic of a scripted client against a live vanilla server (13212 ZDOs, 1 position differs in the
  last bit); admin commands, raids, events, sleep, saves.

Known gaps: no crossplay (PlayFab), no `-preset`/`-modifier` launch options, no backup before upgrading a v40/41
world. Development notes (Russian): `NOTES.ru.md`.

## Workspace

The scripts expect this layout (paths are relative to the workspace root):

```
workspace/
  avledet/                 this repo
  build -> avledet/port/build
  tools -> avledet/port/tools
  third_party/steamsdk/sdk Steamworks SDK (not redistributable: steamworks-sys 0.13.0 crate or partner site)
  re/server-1.0/           Valheim dedicated server 1.0.20 (SteamCMD app 896660)
  re/bepinex/              BepInExPack_Valheim, for the dumper and the vanilla side of the tests
  re/redist/linux64/       steamclient.so
  re/pkg-1.0.20/{pkg,json} game data dumped by tools/avldump-server
```

Game files and dumped data are not in this repo, each user dumps them from their own copy of the game.

## Build and run

```sh
docker build -t avledet-build:1 -f build/avledet-build.Dockerfile build
docker build -t vrun:1 -f build/vrun.Dockerfile build
build/build-avledet.sh Release

# game data: build tools/avldump-server (dotnet, references the game's Managed dlls and BepInEx core),
# run it once in the vanilla server, copy run/dump/game/dumped/{pkg,json} to re/pkg-1.0.20,
# then tools/make_location_meta.py writes json/locations_meta.json
tools/run-vanilla-plugin.sh tools/avldump-server/bin/Release/net462/AvlDumpServer.dll run/dump

tools/run-live.sh          # server on UDP 2456, password secret123
```

## Tests against vanilla

- `tools/bot`: a scripted client over TCP (`vbot.py`), the same scenario against vanilla (`run-vanilla.sh`, with
  `tools/tcp-listen`) and Avledet (`run-avledet.sh`), then `compare_bot.py`.
- `tools/avl-probe.sh`: generates zones in vanilla (`tools/worldgen-probe`) and Avledet, compares objects bit for
  bit (`compare_bits.py`).
- `tools/avl-save-test.sh`, `tools/vanilla-save-test.sh`: save round trips both ways.
- Unit tests: `avledet_util_test`, with `AVL_TRANSFORM_TESTS` pointing at the dumper's test json.
