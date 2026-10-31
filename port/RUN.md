# Avledet for Valheim 1.0.21

Forked from [Avledet](https://github.com/crazicrafter1/Avledet). Updated to Valheim 1.0.21 (network version 41)
with Claude.

Avledet is a Valheim dedicated server written in C++. Unmodded 1.0.21 clients join it over Steam. World saves use
the format of the official server, so a world can be moved from one server to the other.

## Tested against the official server

- terrain and biomes: 20000 points match bit for bit;
- generated zones (locations, dungeons, vegetation, chest loot): 43008 objects, 134 heights and 46 rotations
  differ in the last bits;
- network traffic of a test client: 13212 objects, 1 position differs in the last bit;
- admin commands, raids, events, sleep and world saves.

## Limits

- Steam only, no crossplay.
- No `-preset` and `-modifier` launch options.
- Worlds from Valheim 0.221 and older are not supported.

## Run with Docker Compose

Requirements: Linux x86-64, Docker with the Compose plugin, about 5 GB of free disk space. Only
`docker-compose.yml` and `server.yml.example` are needed, the images are on `ghcr.io/edodo1337`.

```sh
mkdir avledet && cd avledet
curl -fsSLO https://raw.githubusercontent.com/edodo1337/Avledet/port-1.0/docker-compose.yml
mkdir game-data server-data
curl -fsSL -o server-data/server.yml https://raw.githubusercontent.com/edodo1337/Avledet/port-1.0/server.yml.example
nano server-data/server.yml    # server name, password, world, see Configuration
docker compose run --rm dump   # game data from the official server, see below
docker compose up -d           # start the server
docker compose logs -f         # server log, Ctrl+C to close it
```

The containers write files as user 1000. If `id -u` shows another number, change `user:`, `PUID` and `PGID` in
`docker-compose.yml`.

After a game update, run `docker compose pull`, `docker compose run --rm dump` and `docker compose up -d`.

To build the images yourself, run in this repo `docker build -t ghcr.io/edodo1337/avledet .` and
`docker build -t ghcr.io/edodo1337/avledet-dump -f port/docker/dump.Dockerfile .`. The first build takes a long
time.

### Game data

The server needs data files from the game, the image does not include them. `dump` downloads the official
dedicated server with SteamCMD (about 2 GB) and writes the files to `game-data/out/`. The server reads them from
there on every start.

If you already have these files from Valheim 1.0.21, put them into `game-data/out/` and skip `dump`:

- `prefabs.pkg`, `features.pkg`, `vegetation.pkg`, `randomEvents.pkg`, `dungeons.pkg`, `colliders.pkg`;
- `altbiomes.json`, `awake.json`, `event_objects.json`, `loot.json`, `persistentEvents.json`,
  `prefab_terrainmods.json`, `server_options.json`, `locations_meta.json`.

These files are made by the plugin in `port/tools/avldump-server`.

The game data is not a world save. To use your own world, see the end of Configuration.

## Configuration

The settings are in `server-data/server.yml`, an example is in [server.yml.example](../server.yml.example). Missing
keys get default values. Without the file the server creates it with defaults and a random seed. To change the
settings, stop the server, edit the file and start the server again:

```sh
docker compose stop
nano server-data/server.yml
docker compose start
```

| Key | Default | Meaning |
|---|---|---|
| `server.name` | `Avledet server` | server name |
| `server.password` | empty | password for joining |
| `server.public` | `false` | `true` shows the server in the community list |
| `players.max-online` | `15` | player limit |
| `world.world` | `world` | world name and save folder name |
| `world.seed` | random | seed of a new world |
| `zdos.max-send-rate` | `153600` | send rate limit for one player, bytes per second, Steam adapts below it down to the default |
| `zdos.max-send-threshold` | `10240` | send queue limit for one player, bytes. Keep it near 70 ms of the send rate: RPCs wait in the same queue |
| `zdos.peer-send-interval` | `0ms` | longest wait between updates for one player, `0ms` is one player a frame like vanilla |

The `zdos` defaults match vanilla. Up to 25 Mbit/s for 15 players and an update every 320 ms instead of 570 ms:

```yaml
zdos:
  max-send-rate: 200000
  max-send-threshold: 14336
  peer-send-interval: 380ms
```

`peer-send-interval` costs CPU, the main thread checks every object near a player on each send. 15 test clients and
13000 objects around them, on a desktop CPU: 25-35% of one core with `0ms`, 40-60% with `380ms`, 75-80% with `250ms`
and lower.

Other files in `server-data/`:

- `worlds_local/<world>/`: the world save;
- `adminlist.txt`, `bannedlist.txt`, `permittedlist.txt`: player lists, one ID per line (`Steam_<id>`);
- `server_<date>.log`: server logs, `docker compose logs` shows the same.

Players join with the "Join IP" button and the address `<host>:2456`. The firewall must allow UDP 2456 and 2457.

To use a world from the official server, stop that server, copy its folder `worlds_local/<world>/` into
`server-data/worlds_local/` and set `world.world` to the same name.

The world is saved on `docker compose stop` and `docker compose down`.

## Resource usage

Docker, AMD Ryzen 7 9700X, new world:

| | Official server | Avledet |
|---|---|---|
| Memory (RSS) | 1.4 GB | 170 MB |
| CPU, % of one core | 17% | 4% |

## Development

The tools used to test the port against the official server are in `port/`, see [port/README.md](README.md).
Development notes in Russian: `NOTES.ru.md`.

## License

MIT, see [LICENSE](../LICENSE). Not affiliated with Iron Gate AB.
