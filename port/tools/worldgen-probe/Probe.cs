using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using BepInEx;
using HarmonyLib;
using UnityEngine;

namespace WorldgenProbe
{
    // Reference dump of vanilla world generation for a fixed seed.
    // Env: PROBE_SEED (seed name), PROBE_OUT (output dir), PROBE_STEP (sample step in meters).
    [BepInPlugin("avledet.worldgenprobe", "WorldgenProbe", "0.1.0")]
    internal class Probe : BaseUnityPlugin
    {
        static string Seed => Env("PROBE_SEED", "HrrlYr05vV");
        // PROBE_GHOST=1: zones in SpawnMode.Ghost like the live dedicated server, not Full (PokeLocalZone)
        static bool Ghost => Env("PROBE_GHOST", "") == "1";
        static readonly MethodInfo s_spawnZone = typeof(ZoneSystem).GetMethod("SpawnZone", BindingFlags.Instance | BindingFlags.NonPublic);
        static string Out => Env("PROBE_OUT", "probe/");
        static int Step => int.Parse(Env("PROBE_STEP", "64"));

        bool m_done;

        void Awake()
        {
            new Harmony("avledet.worldgenprobe").PatchAll(typeof(Probe));
        }

        // new worlds get our seed instead of a random one
        [HarmonyPatch(typeof(World), nameof(World.GenerateSeed)), HarmonyPrefix]
        static bool GenerateSeed(ref string __result)
        {
            __result = Seed;
            return false;
        }

        // every placed dungeon/camp room, to diff the generation order with Avledet (PROBE_ROOMS=1)
        static bool LogRooms => Env("PROBE_ROOMS", "") == "1";

        [HarmonyPatch(typeof(DungeonGenerator), "PlaceRoom", typeof(DungeonDB.RoomData), typeof(Vector3), typeof(Quaternion), typeof(RoomConnection), typeof(ZoneSystem.SpawnMode)), HarmonyPostfix]
        static void PlaceRoom(DungeonGenerator __instance, DungeonDB.RoomData roomData, Vector3 pos, Quaternion rot, Room __result)
        {
            if (!LogRooms)
                return;
            Debug.Log("[WorldgenProbe] room " + __instance.name.Replace("(Clone)", "") + " " + roomData.m_prefab.Name + " " +
                F(pos.x) + " " + F(pos.y) + " " + F(pos.z) + " " + F(rot.eulerAngles.y) + " q " + F(rot.x) + " " + F(rot.y) + " " + F(rot.z) + " " + F(rot.w) + " seed " + __result.m_seed);
        }

        [HarmonyPatch(typeof(DungeonGenerator), "FindEndCaps"), HarmonyPostfix]
        static void FindEndCaps(RoomConnection connection, List<DungeonDB.RoomData> rooms)
        {
            if (!LogRooms)
                return;
            var p = connection.transform.position;
            Debug.Log("[WorldgenProbe] endcaps " + F(p.x) + " " + F(p.y) + " " + F(p.z) + " " + string.Join(",", rooms.Select(r => r.m_prefab.Name).ToArray()));
        }

        [HarmonyPatch(typeof(DungeonGenerator), "PlaceRoom", typeof(RoomConnection), typeof(DungeonDB.RoomData), typeof(ZoneSystem.SpawnMode)), HarmonyPostfix]
        static void TryRoom(DungeonDB.RoomData roomData, bool __result)
        {
            if (!LogRooms)
                return;
            Debug.Log("[WorldgenProbe] try " + roomData.m_prefab.Name + " " + __result);
        }

        // location rotation comes from the unseeded global random, seed it like Avledet does
        [HarmonyPatch(typeof(ZoneSystem), "PlaceLocations"), HarmonyPrefix]
        static void SeedLocationRotation(Vector2s zoneID)
        {
            if (Env("PROBE_SEEDED_ROTATION", "") == "1")
                UnityEngine.Random.InitState(WorldGenerator.instance.GetSeed() + zoneID.x * 4271 + zoneID.y * 9187);
        }

        // vegetation raycasts, to diff with Avledet (PROBE_PHYS=1): phys.txt in PROBE_OUT
        static StreamWriter s_phys;
        static Vector2s? s_vegZone;

        [HarmonyPatch(typeof(ZoneSystem), "PlaceVegetation"), HarmonyPrefix]
        static void PlaceVegetationPre(Vector2s zoneID)
        {
            if (Env("PROBE_PHYS", "") != "1")
                return;
            if (s_phys == null)
            {
                Directory.CreateDirectory(Out);
                s_phys = new StreamWriter(Path.Combine(Out, "phys.txt"));
            }
            s_vegZone = zoneID;
            s_lastVeg = null;
        }

        // PlaceVegetation seeds each vegetation with its prefab name hash: log which one runs
        static HashSet<string> s_vegNames;
        static string s_lastVeg;

        [HarmonyPatch(typeof(StringExtensionMethods), nameof(StringExtensionMethods.GetStableHashCode)), HarmonyPostfix]
        static void StableHash(string str)
        {
            if (s_vegZone == null || str == s_lastVeg)
                return;
            if (s_vegNames == null)
                s_vegNames = new HashSet<string>(ZoneSystem.instance.m_vegetation.Where(v => v.m_prefab).Select(v => v.m_prefab.name));
            if (!s_vegNames.Contains(str))
                return;
            s_lastVeg = str;
            s_phys.WriteLine("veg " + s_vegZone.Value.x + " " + s_vegZone.Value.y + " " + str);
        }

        [HarmonyPatch(typeof(ZoneSystem), "PlaceVegetation"), HarmonyPostfix]
        static void PlaceVegetationPost() => s_vegZone = null;

        // name of the hit collider: root object / collider object
        // plus collider details for debugging: type, mesh, root scale and position, world bounds
        static string HitName(RaycastHit hit)
        {
            var c = hit.collider;
            var root = c.transform.root;
            var mesh = c is MeshCollider mc ? (mc.convex ? "convex:" : "mesh:") + (mc.sharedMesh ? mc.sharedMesh.name.Replace(' ', '_') : "null") : c.GetType().Name;
            var b = c.bounds;
            return root.name.Replace("(Clone)", "").Replace(' ', '_') + "/" + c.name.Replace(' ', '_') + " " + mesh +
                " s=" + F(root.localScale.x) + " at=" + F(root.position.x) + "," + F(root.position.y) + "," + F(root.position.z) +
                " b=" + F(b.min.x) + "," + F(b.min.y) + "," + F(b.min.z) + ".." + F(b.max.x) + "," + F(b.max.y) + "," + F(b.max.z);
        }

        [HarmonyPatch(typeof(ZoneSystem), nameof(ZoneSystem.IsBlocked)), HarmonyPostfix]
        static void IsBlocked(Vector3 p, bool __result, int ___m_blockRayMask)
        {
            if (s_vegZone == null)
                return;
            var z = s_vegZone.Value;
            var line = "blk " + z.x + " " + z.y + " " + F(p.x) + " " + F(p.z) + " " + (__result ? 1 : 0);
            if (__result && Physics.Raycast(p, Vector3.down, out var hit, 10000f, ___m_blockRayMask))
                line += " " + F(hit.point.y) + " " + HitName(hit);
            s_phys.WriteLine(line);
        }

        // vegetation ground ray (PROBE_PHYS=1 and PROBE_GROUND=1): height and the triangle of the
        // collision mesh it hit. Avledet does not log these: not for the compare_phys.py reference
        [HarmonyPatch(typeof(ZoneSystem), nameof(ZoneSystem.GetGroundData)), HarmonyPostfix]
        static void GetGroundData(Vector3 p, int ___m_terrainRayMask)
        {
            if (s_vegZone == null || Env("PROBE_GROUND", "") != "1")
                return;
            var z = s_vegZone.Value;
            var line = "gnd " + z.x + " " + z.y + " " + F(p.x) + " " + F(p.y) + " " + F(p.z);
            if (Physics.Raycast(p + Vector3.up * 5000f, Vector3.down, out var hit, 10000f, ___m_terrainRayMask) && hit.collider is MeshCollider mc && mc.sharedMesh)
            {
                var m = mc.sharedMesh;
                var v = m.vertices;
                var t = m.triangles;
                int i = hit.triangleIndex * 3;
                line += " " + HitName(hit) + " tri " + hit.triangleIndex + " u " + F(hit.barycentricCoordinate.y) + " v " + F(hit.barycentricCoordinate.z);
                for (int k = 0; k < 3; k++)
                    line += " p" + k + "=" + F(v[t[i + k]].x) + "," + F(v[t[i + k]].y) + "," + F(v[t[i + k]].z);
            }
            s_phys.WriteLine(line);
        }

        [HarmonyPatch(typeof(ZoneSystem), nameof(ZoneSystem.GetStaticSolidHeight)), HarmonyPostfix]
        static void GetStaticSolidHeight(Vector3 p, float height, Vector3 normal, bool __result, int ___m_staticSolidRayMask)
        {
            if (s_vegZone == null)
                return;
            var z = s_vegZone.Value;
            var line = "sol " + z.x + " " + z.y + " " + F(p.x) + " " + F(p.y) + " " + F(p.z) + " " + (__result ? 1 : 0);
            if (__result)
            {
                line += " " + F(height) + " " + F(normal.x) + " " + F(normal.y) + " " + F(normal.z);
                // p is already the ray origin here, the method moved it
                if (Physics.Raycast(p, Vector3.down, out var hit, 2000f, ___m_staticSolidRayMask))
                    line += " " + HitName(hit);
            }
            s_phys.WriteLine(line);
        }

        // zones to generate ("x y" per line in PROBE_ZONES), null until loaded
        List<Vector2s> m_pendingZones;
        List<Vector2s> m_allZones;
        int m_settleFrames;
        int m_lastSpawnFrame = -100;
        float m_zonesStarted;

        void Update()
        {
            if (m_done || !ZNet.instance || !ZoneSystem.instance || !ZoneSystem.instance.LocationsGenerated)
                return;

            if (!GenerateZones())
                return;

            m_done = true;
            try
            {
                Dump();
                Logger.LogInfo("Probe done");
            }
            catch (Exception e)
            {
                Logger.LogError("Probe failed: " + e);
            }
            Application.Quit();
        }

        // Returns true when all requested zones are generated (or none requested).
        bool GenerateZones()
        {
            var file = Env("PROBE_ZONES", "");
            if (file == "")
                return true;
            if (m_pendingZones == null)
            {
                m_allZones = File.ReadAllLines(file).Select(l => l.Split(' ')).Where(p => p.Length >= 2)
                    .Select(p => new Vector2s(short.Parse(p[0]), short.Parse(p[1]))).ToList();
                m_pendingZones = new List<Vector2s>(m_allZones);
                m_zonesStarted = Time.realtimeSinceStartup;
            }

            // vanilla generates a zone only after its heightmap is built, so poke every frame.
            // HeightmapBuilder keeps only 16 ready heightmaps, so poke a few zones at a time.
            var poke = typeof(ZoneSystem).GetMethod("PokeLocalZone", BindingFlags.Instance | BindingFlags.NonPublic);
            var generated = typeof(ZoneSystem).GetMethod("IsZoneGenerated", BindingFlags.Instance | BindingFlags.NonPublic, null, new[] { typeof(Vector2s) }, null);
            // One zone at a time, a few frames apart: ZNetScene destroys the objects of the last zone
            // (far from the reference point) first, so vegetation raycasts see only the zone itself.
            var batch = m_pendingZones.Take(4).ToList();
            if (Ghost)
            {
                // as the dedicated server does for its peers (CreateGhostZones): one ghost zone per update,
                // its objects are destroyed in the same frame
                if (batch.Count > 0 && Time.frameCount - m_lastSpawnFrame >= 3)
                {
                    var z = batch[0];
                    var args = new object[] { z, ZoneSystem.SpawnMode.Ghost, null };
                    if (!(bool)generated.Invoke(ZoneSystem.instance, new object[] { z }))
                        s_spawnZone.Invoke(ZoneSystem.instance, args);
                    if ((bool)generated.Invoke(ZoneSystem.instance, new object[] { z }))
                    {
                        m_pendingZones.Remove(z);
                        m_lastSpawnFrame = Time.frameCount;
                    }
                }
            }
            else if (Time.frameCount - m_lastSpawnFrame >= 3)
                foreach (var z in batch)
                {
                    poke.Invoke(ZoneSystem.instance, new object[] { z });
                    if ((bool)generated.Invoke(ZoneSystem.instance, new object[] { z }))
                    {
                        m_pendingZones.Remove(z);
                        m_lastSpawnFrame = Time.frameCount;
                        break;
                    }
                }
            if (m_pendingZones.Count > 0)
            {
                if (Time.realtimeSinceStartup - m_zonesStarted < 1800f)
                    return false;
                // dump the rest, stuck zones are left out
                Logger.LogWarning("zones not generated in time: " + string.Join(", ", m_pendingZones.Select(z => z.x + " " + z.y).ToArray()));
                m_allZones.RemoveAll(m_pendingZones.Contains);
                m_pendingZones.Clear();
            }
            // let instantiated objects register their ZDOs
            if (m_settleFrames++ < 10)
                return false;

            DumpZones();
            s_phys?.Close();
            return true;
        }

        void DumpZones()
        {
            Directory.CreateDirectory(Out);
            var set = new HashSet<Vector2s>(m_allZones);
            var byId = (Dictionary<ZDOID, ZDO>)typeof(ZDOMan).GetField("m_objectsByID", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(ZDOMan.instance);
            var lines = new List<string>();
            foreach (var zdo in byId.Values)
            {
                var pos = zdo.GetPosition();
                var zone = ZoneSystem.GetZone(pos);
                if (!set.Contains(zone))
                    continue;
                var prefab = ZNetScene.instance.GetPrefab(zdo.GetPrefab());
                var rot = zdo.GetRotation();
                lines.Add(zone.x + " " + zone.y + " " + (prefab ? prefab.name : zdo.GetPrefab().ToString()) + " " +
                    F(pos.x) + " " + F(pos.y) + " " + F(pos.z) + " " + F(rot.x) + " " + F(rot.y) + " " + F(rot.z) + " " + F(rot.w));
            }
            lines.Sort(StringComparer.Ordinal);
            File.WriteAllLines(Path.Combine(Out, "zones.txt"), lines.ToArray());
            Debug.Log("[WorldgenProbe] zones dumped: " + lines.Count + " objects in " + m_allZones.Count + " zones");
        }

        static void Dump()
        {
            Directory.CreateDirectory(Out);
            var world = ZNet.instance.GetWorld();
            var gen = WorldGenerator.instance;
            var data = world.m_biomeData;

            File.WriteAllText(Path.Combine(Out, "world.json"),
                "{\"name\":" + Str(world.m_name) + ",\"seedName\":" + Str(world.m_seedName) +
                ",\"seed\":" + world.m_seed + ",\"worldGen\":" + world.m_worldGenVersion +
                ",\"step\":" + Step + ",\"gridSize\":" + data.Size + "}");

            // 2048x2048 biome grid, row-major as stored: [i, j]
            var sectorIndex = new Dictionary<BiomeSector, int>();
            for (int i = 0; i < data.Sectors.Count; i++)
                sectorIndex[data.Sectors[i]] = i;

            using (var h = new BinaryWriter(File.Create(Path.Combine(Out, "grid_heights.f32"))))
            using (var b = new BinaryWriter(File.Create(Path.Combine(Out, "grid_biomes.i32"))))
            using (var s = new BinaryWriter(File.Create(Path.Combine(Out, "grid_sectors.i32"))))
            {
                for (int i = 0; i < data.Size; i++)
                    for (int j = 0; j < data.Size; j++)
                    {
                        h.Write(data.PointHeights[i, j]);
                        b.Write((int)data.PointBiomes[i, j]);
                        var sec = data.PointSectors[i, j];
                        s.Write(sec != null && sectorIndex.TryGetValue(sec, out var idx) ? idx : -1);
                    }
            }

            var sb = new StringBuilder("[");
            for (int i = 0; i < data.Sectors.Count; i++)
            {
                var sec = data.Sectors[i];
                if (i > 0) sb.Append(",\n");
                sb.Append("{\"biome\":").Append((int)sec.Biome);
                sb.Append(",\"center\":").Append(V(sec.Center));
                sb.Append(",\"min\":").Append(V(sec.Min)).Append(",\"max\":").Append(V(sec.Max));
                sb.Append(",\"minZone\":[").Append(sec.MinZone.x).Append(",").Append(sec.MinZone.y).Append("]");
                sb.Append(",\"maxZone\":[").Append(sec.MaxZone.x).Append(",").Append(sec.MaxZone.y).Append("]");
                sb.Append(",\"edgeCount\":").Append(sec.EdgeCount);
                sb.Append(",\"heightMin\":").Append(F(sec.HeightMin)).Append(",\"heightMax\":").Append(F(sec.HeightMax));
                sb.Append(",\"heightAvg\":").Append(F(sec.HeightAvg));
                sb.Append(",\"distanceFromCenter\":").Append(F(sec.DistanceFromCenter));
                sb.Append(",\"altBiomes\":[").Append(string.Join(",", sec.AltBiomes.Select(a => Str(a.m_name)).ToArray())).Append("]");
                sb.Append(",\"neighbors\":[").Append(string.Join(",", sec.Neighbors.Select(n => n != null && sectorIndex.TryGetValue(n, out var ni) ? ni.ToString() : "-1").ToArray())).Append("]");
                sb.Append("}");
            }
            File.WriteAllText(Path.Combine(Out, "sectors.json"), sb.Append("]").ToString());

            // full-resolution samples of the final height/biome functions
            int step = Step;
            using (var h = new BinaryWriter(File.Create(Path.Combine(Out, "samples_height.f32"))))
            using (var b = new BinaryWriter(File.Create(Path.Combine(Out, "samples_biome.i32"))))
            {
                for (int y = -10560; y <= 10560; y += step)
                    for (int x = -10560; x <= 10560; x += step)
                    {
                        h.Write(gen.GetHeight(x, y));
                        b.Write((int)gen.GetBiome(x, y));
                    }
            }

            // optional list of points ("x y" per line) for detailed checks
            var pointsFile = Env("PROBE_POINTS", "");
            if (pointsFile != "")
            {
                var lines = new List<string>();
                foreach (var line in File.ReadAllLines(pointsFile))
                {
                    var p = line.Split(' ');
                    if (p.Length < 2) continue;
                    float x = float.Parse(p[0], CultureInfo.InvariantCulture), y = float.Parse(p[1], CultureInfo.InvariantCulture);
                    lines.Add(F(x) + " " + F(y) + " " + F(gen.GetHeight(x, y)) + " " + (int)gen.GetBiome(x, y) + " | " + Internals(gen, x, y));
                }
                File.WriteAllLines(Path.Combine(Out, "points.txt"), lines.ToArray());
            }

            sb = new StringBuilder("[");
            bool first = true;
            foreach (var li in ZoneSystem.instance.m_locationInstances.Values)
            {
                if (!first) sb.Append(",\n");
                first = false;
                sb.Append("{\"name\":").Append(Str(li.m_location.m_prefabName));
                sb.Append(",\"pos\":[").Append(F(li.m_position.x)).Append(",").Append(F(li.m_position.y)).Append(",").Append(F(li.m_position.z)).Append("]");
                sb.Append(",\"placed\":").Append(li.m_placed ? "true" : "false").Append("}");
            }
            File.WriteAllText(Path.Combine(Out, "locations.json"), sb.Append("]").ToString());
        }

        // GetBiome inputs as raw float bits, to find last-bit differences
        static string Internals(WorldGenerator gen, float wx, float wy)
        {
            var t = typeof(WorldGenerator);
            var flags = System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Public;
            float baseHeight = (float)t.GetMethod("GetBaseHeight", flags).Invoke(gen, new object[] { wx, wy, false });
            float o0 = (float)t.GetField("m_offset0", flags).GetValue(gen);
            float o1 = (float)t.GetField("m_offset1", flags).GetValue(gen);
            float o2 = (float)t.GetField("m_offset2", flags).GetValue(gen);
            float o4 = (float)t.GetField("m_offset4", flags).GetValue(gen);
            float len = DUtils.Length(wx, wy);
            float angle = (float)((double)WorldGenerator.WorldAngle(wx, wy) * 100.0);
            float p0 = DUtils.PerlinNoise((double)(float)((double)o0 + (double)wx) * 0.0010000000474974513, (double)(float)((double)o0 + (double)wy) * 0.0010000000474974513);
            float p4 = DUtils.PerlinNoise((double)(float)((double)o4 + (double)wx) * 0.0010000000474974513, (double)(float)((double)o4 + (double)wy) * 0.0010000000474974513);
            float p1 = DUtils.PerlinNoise((double)(float)((double)o1 + (double)wx) * 0.0010000000474974513, (double)(float)((double)o1 + (double)wy) * 0.0010000000474974513);
            float p2 = DUtils.PerlinNoise((double)(float)((double)o2 + (double)wx) * 0.0010000000474974513, (double)(float)((double)o2 + (double)wy) * 0.0010000000474974513);
            return string.Join(" ", new[] { baseHeight, len, angle, p0, p4, p1, p2 }.Select(v => BitConverter.ToInt32(BitConverter.GetBytes(v), 0).ToString("x8") + "(" + F(v) + ")").ToArray());
        }

        static string Env(string key, string def)
        {
            var v = Environment.GetEnvironmentVariable(key);
            return string.IsNullOrEmpty(v) ? def : v;
        }

        static string V(Vector2 v) => "[" + F(v.x) + "," + F(v.y) + "]";

        static string F(float f) => f.ToString("R", CultureInfo.InvariantCulture);

        static string Str(string s) => "\"" + (s ?? "").Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"";
    }
}
