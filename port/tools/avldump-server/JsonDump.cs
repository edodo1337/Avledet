using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using SoftReferenceableAssets;
using UnityEngine;

namespace AvlDumpServer
{
    // Full dump of server-side game data as JSON, via reflection.
    // Binary .pkg files for Avledet are built from this offline.
    internal static class JsonDump
    {
        const int MaxDepth = 6;
        const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

        public static void Run(string dir)
        {
            Directory.CreateDirectory(dir);

            Write(dir, "version.json", w => VersionInfo(w));
            Write(dir, "prefabs.json", w => Prefabs(w));
            Write(dir, "locations.json", w => Locations(w));
            Write(dir, "vegetation.json", w => List(w, ZoneSystem.instance.m_vegetation));
            Write(dir, "altbiomes.json", w => List(w, AltBiomeList.m_altBiomes));
            Write(dir, "randomEvents.json", w => List(w, RandEventSystem.instance.m_events));
            Write(dir, "persistentEvents.json", w => List(w, PersistentEventSystem.instance.m_possibleEvents));
            Write(dir, "rooms.json", w => Rooms(w));
            Write(dir, "globalKeys.json", w => Enum(w, typeof(GlobalKeys)));
            Write(dir, "colliders_survey.json", w => ColliderSurvey(w));
            Write(dir, "awake.json", w => AwakeRandom(w));
            Write(dir, "loot.json", w => AwakeSteps.Dump(w, AwakeNames()));
            Write(dir, "event_objects.json", w => AwakeSteps.EventObjects(w));
            Write(dir, "server_options.json", w => AwakeSteps.ServerOptions(w));
            Write(dir, "euler_test.json", w => EulerTest(w));
            Write(dir, "terrainmod_survey.json", w => TerrainModSurvey(w));
            Write(dir, "prefab_terrainmods.json", w => PrefabTerrainMods(w));
            Write(dir, "scale_survey.json", w => ScaleSurvey(w));
            Write(dir, "lifetimes.json", w => Lifetimes(w));
            RunTests(dir);
        }

        // Unity native math checked bit for bit by the Avledet unit tests
        public static void RunTests(string dir)
        {
            Directory.CreateDirectory(dir);
            Write(dir, "transform_test.json", w => TransformTest(w));
            Write(dir, "transform_set_test.json", w => TransformSetTest(w));
            Write(dir, "look_test.json", w => LookTest(w));
            Write(dir, "managed_test.json", w => ManagedTest(w));
            Write(dir, "constants.json", w => Constants(w));
        }

        // values the scene sets (not the C# defaults) and the enums the server browser tags use
        static void Constants(StringBuilder w)
        {
            var ci = CultureInfo.InvariantCulture;
            Func<float, string> F = x => x.ToString("R", ci);
            var re = RandEventSystem.instance;
            w.Append("{\"eventIntervalMin\":").Append(F(re.m_eventIntervalMin))
                .Append(",\"eventChance\":").Append(F(re.m_eventChance))
                .Append(",\"dayLengthSec\":").Append(EnvMan.instance.m_dayLengthSec)
                .Append(",\"saveInterval\":").Append(F(Game.m_saveInterval))
                .Append(",\"fixedDeltaTime\":").Append(F(Time.fixedDeltaTime))
                .Append(",\"maximumDeltaTime\":").Append(F(Time.maximumDeltaTime))
                .Append(",\"targetFrameRate\":").Append(Application.targetFrameRate);
            foreach (var t in new[] { typeof(GlobalKeys), typeof(WorldModifiers), typeof(WorldModifierOption), typeof(WorldPresets) })
            {
                w.Append(",\"").Append(t.Name).Append("\":{");
                bool first = true;
                foreach (var v in System.Enum.GetValues(t))
                {
                    if (!first) w.Append(",");
                    first = false;
                    w.Append("\"").Append(v.ToString()).Append("\":").Append(Convert.ToInt32(v));
                }
                w.Append("}");
            }
            // the server browser "m" tag: vanilla packing and encoding of starting keys
            var cases = new[] {
                new[] { "playerdamage 85", "nomap" },
                new[] { "preset combat_hard:deathpenalty_casual:resources_more:raids_none:portals_casual", "enemydamage 150" },
                new[] { "preset hardcore", "nomap", "playerdamage 70" },
                new[] { "preset immersive:combat_veryhard", "fire" },
                new[] { "eventrate 200", "eventrate 50", "nobuildcost" },
                new[] { "bogus 1" },
                new[] { "NoMap", "PlayerEvents" },
                new[] { "preset combat_nosuch" },
                new string[0],
            };
            w.Append(",\"modifierTags\":[");
            for (int i = 0; i < cases.Length; i++)
            {
                if (i > 0) w.Append(",");
                // a preset part without "_" before a later one throws in vanilla: written as no tag
                string tag;
                try
                {
                    tag = ServerOptionsGUI.TryConvertModifierKeysToCompactKVP<Dictionary<string, string>>(cases[i], out var kvp)
                        ? StringUtils.EncodeDictionaryAsString(kvp, encloseInQuotes: false) : null;
                }
                catch (Exception)
                {
                    tag = null;
                }
                var outer = new Dictionary<string, string> { { "g", Version.CurrentVersion.ToString() }, { "n", "41" } };
                if (tag != null) outer.Add("m", tag);
                w.Append("{\"keys\":[").Append(string.Join(",", cases[i].Select(k => Str(k)).ToArray())).Append("],\"m\":")
                    .Append(tag == null ? "null" : Str(tag)).Append(",\"tags\":").Append(Str(StringUtils.EncodeDictionaryAsString(outer, encloseInQuotes: false))).Append("}");
            }
            w.Append("]");
            w.Append("}");
        }

        // UnityEngine C# math as the game's Mono runs it: q * v, q * q, Vector3.Cross, a + b
        static void ManagedTest(StringBuilder w)
        {
            var rnd = new System.Random(13);
            Func<float> f = () => (float)(rnd.NextDouble() * 2 - 1);
            var ci = CultureInfo.InvariantCulture;
            Func<float, string> F = x => x.ToString("R", ci);
            Func<Vector3, string> V = v => F(v.x) + "," + F(v.y) + "," + F(v.z);
            Func<Quaternion, string> Q = q => F(q.x) + "," + F(q.y) + "," + F(q.z) + "," + F(q.w);
            w.Append("[");
            for (int i = 0; i < 20000; i++)
            {
                var q = new Quaternion(f(), f(), f(), f()).normalized;
                var r = new Quaternion(f(), f(), f(), f()).normalized;
                var a = new Vector3(f(), f(), f()) * (float)Math.Pow(10, rnd.Next(0, 4));
                var b = new Vector3(f(), f(), f()) * (float)Math.Pow(10, rnd.Next(0, 4));
                if (i > 0) w.Append(",\n");
                w.Append("[").Append(Q(q)).Append(",").Append(Q(r)).Append(",").Append(V(a)).Append(",").Append(V(b))
                    .Append(",").Append(V(q * a)).Append(",").Append(Q(q * r)).Append(",").Append(V(Vector3.Cross(a, b)))
                    .Append(",").Append(V(b + q * a)).Append("]");
            }
            w.Append("]");
        }

        // Quaternion.LookRotation: half like the vegetation ground tilt (forward = normal x yaw forward),
        // half random vectors
        static void LookTest(StringBuilder w)
        {
            var rnd = new System.Random(11);
            Func<float> f = () => (float)(rnd.NextDouble() * 2 - 1);
            var ci = CultureInfo.InvariantCulture;
            Func<float, string> F = x => x.ToString("R", ci);
            w.Append("[");
            for (int i = 0; i < 20000; i++)
            {
                Vector3 forward, up;
                if (i % 2 == 0)
                {
                    up = new Vector3(f() * 0.7f, 1, f() * 0.7f).normalized;
                    forward = Vector3.Cross(up, Quaternion.Euler(0, (f() + 1) * 180, 0) * Vector3.forward);
                }
                else
                {
                    forward = new Vector3(f(), f(), f());
                    up = new Vector3(f(), f(), f());
                }
                var q = Quaternion.LookRotation(forward, up);
                if (i > 0) w.Append(",\n");
                w.Append("[").Append(F(forward.x)).Append(",").Append(F(forward.y)).Append(",").Append(F(forward.z))
                    .Append(",").Append(F(up.x)).Append(",").Append(F(up.y)).Append(",").Append(F(up.z))
                    .Append(",").Append(F(q.x)).Append(",").Append(F(q.y)).Append(",").Append(F(q.z)).Append(",").Append(F(q.w)).Append("]");
            }
            w.Append("]");
        }

        // Unity native transform math: what a root/child/grandchild hierarchy reports for given local
        // poses (rotation setter normalizing, world position and rotation, lossy scale)
        static void TransformTest(StringBuilder w)
        {
            var rnd = new System.Random(7);
            Func<float> f = () => (float)(rnd.NextDouble() * 2 - 1);
            var ci = CultureInfo.InvariantCulture;
            Func<float, string> F = x => x.ToString("R", ci);
            Func<Vector3, string> V = v => F(v.x) + "," + F(v.y) + "," + F(v.z);
            Func<Quaternion, string> Q = q => F(q.x) + "," + F(q.y) + "," + F(q.z) + "," + F(q.w);
            var root = new GameObject("tt_root").transform;
            var child = new GameObject("tt_child").transform;
            var grand = new GameObject("tt_grand").transform;
            child.SetParent(root, false);
            grand.SetParent(child, false);
            w.Append("[");
            for (int i = 0; i < 20000; i++)
            {
                // world sized root like a placed room, yaw steps or any rotation, sometimes unnormalized
                var rp = new Vector3(f() * 10000, f() * 5000 + 5000, f() * 10000);
                var rq = i % 3 == 0 ? Quaternion.Euler(0, rnd.Next(16) * 22.5f + f() * 0.001f, 0)
                    : i % 3 == 1 ? Quaternion.Euler(f() * 360, f() * 360, f() * 360)
                    : Quaternion.Euler(0, 90, 0) * Quaternion.Inverse(Quaternion.Euler(0, rnd.Next(4) * 90, 0)) * Quaternion.Euler(0, 180, 0);
                var cp = new Vector3(f() * 30, f() * 10, f() * 30);
                var cq = i % 2 == 0 ? Quaternion.Euler(0, rnd.Next(8) * 45f, 0) : Quaternion.Euler(f() * 360, f() * 360, f() * 360);
                var cs = i % 4 == 0 ? Vector3.one : new Vector3(f() + 1.5f, f() + 1.5f, f() + 1.5f);
                var gp = new Vector3(f() * 5, f() * 5, f() * 5);
                var gq = Quaternion.Euler(f() * 360, f() * 360, f() * 360);
                float rs = i % 4 == 1 ? 1f : f() * 0.6f + 1f;
                root.localScale = new Vector3(rs, rs, rs);
                root.position = rp;
                root.rotation = rq;
                child.localPosition = cp;
                child.localRotation = cq;
                child.localScale = cs;
                grand.localPosition = gp;
                grand.localRotation = gq;
                if (i > 0) w.Append(",\n");
                w.Append("[").Append(V(rp)).Append(",").Append(Q(rq)).Append(",").Append(F(rs))
                    .Append(",").Append(V(cp)).Append(",").Append(Q(cq)).Append(",").Append(V(cs))
                    .Append(",").Append(V(gp)).Append(",").Append(Q(gq))
                    .Append(",").Append(Q(root.rotation))
                    .Append(",").Append(V(child.position)).Append(",").Append(Q(child.rotation)).Append(",").Append(V(child.lossyScale))
                    .Append(",").Append(V(grand.position)).Append(",").Append(Q(grand.rotation)).Append(",").Append(V(grand.lossyScale))
                    .Append("]");
            }
            w.Append("]");
            UnityEngine.Object.Destroy(root.gameObject);
        }

        // Unity native world -> local: a child given a world pose under a posed parent (like
        // Instantiate with a parent), what it stores locally and reports back
        static void TransformSetTest(StringBuilder w)
        {
            var rnd = new System.Random(11);
            Func<float> f = () => (float)(rnd.NextDouble() * 2 - 1);
            var ci = CultureInfo.InvariantCulture;
            Func<float, string> F = x => x.ToString("R", ci);
            Func<Vector3, string> V = v => F(v.x) + "," + F(v.y) + "," + F(v.z);
            Func<Quaternion, string> Q = q => F(q.x) + "," + F(q.y) + "," + F(q.z) + "," + F(q.w);
            var root = new GameObject("ts_root").transform;
            var child = new GameObject("ts_child").transform;
            child.SetParent(root, false);
            w.Append("[");
            for (int i = 0; i < 20000; i++)
            {
                var rp = new Vector3(f() * 10000, f() * 5000 + 5000, f() * 10000);
                var rq = i % 2 == 0 ? Quaternion.Euler(0, rnd.Next(16) * 22.5f + f() * 0.001f, 0) : Quaternion.Euler(f() * 360, f() * 360, f() * 360);
                float rs = i % 3 == 0 ? 1f : f() * 0.6f + 1f;
                root.localScale = new Vector3(rs, rs, rs);
                root.position = rp;
                root.rotation = rq;
                var wp = rp + new Vector3(f() * 200, f() * 50, f() * 200);
                var wq = i % 2 == 0 ? Quaternion.Euler(0, rnd.Next(16) * 22.5f + f() * 0.001f, 0) : Quaternion.Euler(f() * 360, f() * 360, f() * 360);
                child.position = wp;
                child.rotation = wq;
                if (i > 0) w.Append(",\n");
                w.Append("[").Append(V(rp)).Append(",").Append(Q(rq)).Append(",").Append(F(rs))
                    .Append(",").Append(V(wp)).Append(",").Append(Q(wq))
                    .Append(",").Append(Q(root.rotation))
                    .Append(",").Append(V(child.localPosition)).Append(",").Append(Q(child.localRotation))
                    .Append(",").Append(V(child.position)).Append(",").Append(Q(child.rotation)).Append("]");
            }
            w.Append("]");
            UnityEngine.Object.Destroy(root.gameObject);
        }

        // Seconds a freshly spawned object lives before it destroys its own ZDO: Gibber explodes after
        // m_delay and is gone m_timeout later, TimedDestruction with m_triggerOnAwake after m_timeout
        static void Lifetimes(StringBuilder w)
        {
            var list = new List<string>();
            foreach (var prefab in ZNetScene.instance.m_prefabs)
            {
                var gibber = prefab.GetComponent<Gibber>();
                var timed = prefab.GetComponent<TimedDestruction>();
                float? life = gibber ? gibber.m_delay + gibber.m_timeout
                    : timed && timed.m_triggerOnAwake ? timed.m_timeout : (float?)null;
                if (life.HasValue)
                    list.Add(Str(prefab.name) + ":" + life.Value.ToString("R", CultureInfo.InvariantCulture));
            }
            w.Append("{").Append(string.Join(",\n", list.ToArray())).Append("}");
        }

        static void Write(string dir, string file, Action<StringBuilder> body)
        {
            var w = new StringBuilder();
            body(w);
            File.WriteAllText(Path.Combine(dir, file), w.ToString());
            Debug.Log("[AvlDumpServer] wrote " + file + " (" + w.Length + " chars)");
        }

        static void VersionInfo(StringBuilder w)
        {
            w.Append("{\"game\":").Append(Str(Version.CurrentVersion.ToString()));
            w.Append(",\"network\":").Append(Version.c_networkVersion);
            w.Append(",\"world\":").Append((int)Version.c_WorldVersion);
            w.Append(",\"worldGen\":").Append(Version.c_WorldGenVersion);
            w.Append(",\"location\":").Append(ZoneSystem.instance.m_locationVersion);
            w.Append("}");
        }

        static void Prefabs(StringBuilder w)
        {
            w.Append("[");
            bool first = true;
            foreach (var prefab in ZNetScene.instance.m_prefabs)
            {
                var view = prefab.GetComponent<ZNetView>();
                if (!first) w.Append(",\n");
                first = false;
                w.Append("{\"name\":").Append(Str(prefab.name));
                w.Append(",\"hash\":").Append(prefab.name.GetStableHashCode());
                if (view)
                {
                    w.Append(",\"view\":");
                    Value(w, view, 0);
                }
                w.Append(",\"scale\":");
                Value(w, prefab.transform.localScale, 0);
                w.Append(",\"components\":[");
                w.Append(string.Join(",", prefab.GetComponents<Component>().Where(c => c).Select(c => Str(c.GetType().Name)).ToArray()));
                w.Append("]}");
            }
            w.Append("]");
        }

        static void Locations(StringBuilder w)
        {
            w.Append("[");
            bool first = true;
            foreach (var loc in ZoneSystem.instance.m_locations)
            {
                if (!first) w.Append(",\n");
                first = false;
                w.Append("{\"zoneLocation\":");
                Value(w, loc, 0);
                if (loc.m_prefab.IsValid)
                {
                    loc.m_prefab.Load();
                    var root = loc.m_prefab.Asset;
                    w.Append(",\"location\":");
                    Value(w, root.GetComponent<Location>(), 0);
                    w.Append(",");
                    Children(w, root);
                }
                w.Append("}");
            }
            w.Append("]");
        }

        // Census of colliders that vegetation raycasts can hit, to size the physics port
        static void ColliderSurvey(StringBuilder w)
        {
            var groups = new Dictionary<string, List<GameObject>>();
            groups["vegetation"] = ZoneSystem.instance.m_vegetation.Where(v => v.m_enable && v.m_prefab).Select(v => v.m_prefab).Distinct().ToList();
            groups["netprefabs"] = ZNetScene.instance.m_prefabs.Where(p => p).ToList();
            groups["locations"] = new List<GameObject>();
            foreach (var loc in ZoneSystem.instance.m_locations)
                if (loc.m_enable && loc.m_prefab.IsValid) { loc.m_prefab.Load(); groups["locations"].Add(loc.m_prefab.Asset); }
            groups["rooms"] = new List<GameObject>();
            foreach (var rd in DungeonDB.GetRooms()) { rd.m_prefab.Load(); groups["rooms"].Add(rd.m_prefab.Asset); }

            var counts = new Dictionary<string, int>();
            var meshTris = new Dictionary<string, long>();
            foreach (var g in groups)
                foreach (var root in g.Value)
                    foreach (var c in root.GetComponentsInChildren<Collider>(true))
                    {
                        var mc = c as MeshCollider;
                        string key = g.Key + "|" + c.GetType().Name + "|" + LayerMask.LayerToName(c.gameObject.layer) +
                            "|trigger=" + c.isTrigger + "|enabled=" + (c.enabled && c.gameObject.activeInHierarchy) +
                            "|rb=" + (c.GetComponentInParent<Rigidbody>() != null) + (mc ? "|convex=" + mc.convex : "");
                        counts[key] = counts.TryGetValue(key, out var n) ? n + 1 : 1;
                        if (mc && mc.sharedMesh)
                            meshTris[key] = (meshTris.TryGetValue(key, out var t) ? t : 0) + (mc.sharedMesh.isReadable ? mc.sharedMesh.triangles.Length / 3 : -1000000);
                        if (mc && (!mc.sharedMesh || !mc.sharedMesh.isReadable))
                        {
                            var ukey = "unreadable|" + g.Key + "|" + LayerMask.LayerToName(c.gameObject.layer) + "|" + root.name + "|" + (mc.sharedMesh ? mc.sharedMesh.name : "null");
                            counts[ukey] = counts.TryGetValue(ukey, out var u) ? u + 1 : 1;
                        }
                    }
            w.Append("{\"queriesHitTriggers\":").Append(Physics.queriesHitTriggers ? "true" : "false");
            w.Append(",\"queriesHitBackfaces\":").Append(Physics.queriesHitBackfaces ? "true" : "false");
            w.Append(",\"autoSyncTransforms\":").Append(Physics.autoSyncTransforms ? "true" : "false");
            w.Append(",\"counts\":{");
            bool first = true;
            foreach (var kv in counts.OrderBy(k => k.Key))
            {
                if (!first) w.Append(",\n");
                first = false;
                w.Append(Str(kv.Key)).Append(":[").Append(kv.Value).Append(",").Append(meshTris.TryGetValue(kv.Key, out var t) ? t : 0).Append("]");
            }
            w.Append("}}");
        }

        // How many UnityEngine.Random steps Instantiate takes (Awake of all components), measured.
        // Vanilla does not restore the random around Instantiate in PlaceVegetation and PlaceDoors,
        // and WearNTear draws the initial damage of location pieces from it.
        // prefabs instantiated by generation: vegetation, location and room views, doors
        static HashSet<string> AwakeNames()
        {
            var names = new HashSet<string>();
            foreach (var v in ZoneSystem.instance.m_vegetation)
                if (v.m_enable && v.m_prefab) names.Add(v.m_prefab.name);
            void Add(GameObject root)
            {
                foreach (var view in root.GetComponentsInChildren<ZNetView>(true))
                    names.Add(view.GetPrefabName());
            }
            foreach (var loc in ZoneSystem.instance.m_locations)
                if (loc.m_enable && loc.m_prefab.IsValid) { loc.m_prefab.Load(); Add(loc.m_prefab.Asset); }
            foreach (var rd in DungeonDB.GetRooms()) { rd.m_prefab.Load(); Add(rd.m_prefab.Asset); }
            foreach (var dg in Resources.FindObjectsOfTypeAll<DungeonGenerator>())
                foreach (var d in dg.m_doorTypes)
                    if (d.m_prefab) Add(d.m_prefab);
            foreach (var go in AwakeSteps.EventPrefabs())
                Add(go);
            return names;
        }

        static void AwakeRandom(StringBuilder w)
        {
            var names = new HashSet<string>();
            foreach (var v in ZoneSystem.instance.m_vegetation)
                if (v.m_enable && v.m_prefab) names.Add(v.m_prefab.name);
            var healthOverrides = 0;
            void AddViews(GameObject root)
            {
                foreach (var view in root.GetComponentsInChildren<ZNetView>(true))
                {
                    names.Add(view.GetPrefabName());
                    var wnt = view.GetComponent<WearNTear>();
                    var prefab = ZNetScene.instance.GetPrefab(view.GetPrefabName());
                    var pw = prefab ? prefab.GetComponent<WearNTear>() : null;
                    if (wnt && pw && wnt.m_health != pw.m_health) healthOverrides++;
                }
            }
            foreach (var loc in ZoneSystem.instance.m_locations)
                if (loc.m_enable && loc.m_prefab.IsValid) { loc.m_prefab.Load(); AddViews(loc.m_prefab.Asset); }
            foreach (var rd in DungeonDB.GetRooms()) { rd.m_prefab.Load(); AddViews(rd.m_prefab.Asset); }
            foreach (var dg in Resources.FindObjectsOfTypeAll<DungeonGenerator>())
                foreach (var d in dg.m_doorTypes)
                    if (d.m_prefab) AddViews(d.m_prefab);

            var saved = UnityEngine.Random.state;
            w.Append("{\"healthOverrides\":").Append(healthOverrides).Append(",\"prefabs\":{");
            bool first = true;
            foreach (var name in names.OrderBy(n => n))
            {
                var prefab = ZNetScene.instance.GetPrefab(name);
                if (!prefab) continue;
                var counts = new List<int>();
                int damageIndex = -1;
                float health = 0;
                foreach (bool damage in new[] { false, true })
                    foreach (int seed in new[] { 1, 12345, -777 })
                    {
                        WearNTear.m_randomInitialDamage = damage;
                        UnityEngine.Random.InitState(seed);
                        var before = UnityEngine.Random.state;
                        var obj = UnityEngine.Object.Instantiate(prefab, new Vector3(0, -5000, 0), Quaternion.identity);
                        var after = UnityEngine.Random.state;
                        WearNTear.m_randomInitialDamage = false;
                        // steps: replay from before until the state matches
                        UnityEngine.Random.state = before;
                        int k = 0;
                        while (k < 1000 && !UnityEngine.Random.state.Equals(after)) { UnityEngine.Random.value.GetHashCode(); k++; }
                        counts.Add(k);
                        var wnt = obj.GetComponent<WearNTear>();
                        var nview = obj.GetComponent<ZNetView>();
                        if (damage && wnt && nview && nview.GetZDO() != null && damageIndex == -1)
                        {
                            health = wnt.m_health;
                            float hp = nview.GetZDO().GetFloat(ZDOVars.s_health, -1);
                            UnityEngine.Random.state = before;
                            for (int j = 0; j < k; j++)
                            {
                                var st = UnityEngine.Random.state;
                                if (UnityEngine.Random.Range(0.1f * health, health * 0.6f) == hp) { damageIndex = j; break; }
                                UnityEngine.Random.state = st;
                                UnityEngine.Random.value.GetHashCode();
                            }
                        }
                        if (nview && nview.GetZDO() != null) ZNetScene.instance.Destroy(obj); else UnityEngine.Object.Destroy(obj);
                    }
                if (counts.All(c => c == 0) && damageIndex == -1) continue;
                if (!first) w.Append(",\n");
                first = false;
                w.Append(Str(name)).Append(":{\"draws\":[").Append(string.Join(",", counts.Select(c => c.ToString()).ToArray()))
                    .Append("],\"damageIndex\":").Append(damageIndex).Append(",\"health\":").Append(health.ToString("R", System.Globalization.CultureInfo.InvariantCulture)).Append("}");
            }
            w.Append("}}");
            UnityEngine.Random.state = saved;
        }

        // Reference pairs for Quaternion.eulerAngles and Quaternion.Euler, to port them bit exact
        static void EulerTest(StringBuilder w)
        {
            var rnd = new System.Random(42);
            Func<float> f = () => (float)(rnd.NextDouble() * 2 - 1);
            var ci = System.Globalization.CultureInfo.InvariantCulture;
            Func<float, string> F = x => x.ToString("R", ci);
            w.Append("{\"toEuler\":[");
            for (int i = 0; i < 20000; i++)
            {
                Quaternion q;
                if (i % 4 == 0)
                    q = new Quaternion(f(), f(), f(), f()).normalized;
                else if (i % 4 == 1) // near the x = +-90 singularity
                    q = Quaternion.Euler((rnd.Next(2) == 0 ? 90f : -90f) + f() * (float)Math.Pow(10, -rnd.Next(0, 6)), f() * 360, f() * 360);
                else if (i % 4 == 2) // composed like generation does
                    q = Quaternion.Euler(0, rnd.Next(16) * 22.5f, 0) * new Quaternion(f(), f(), f(), f()).normalized;
                else
                    q = Quaternion.Euler(f() * 360, f() * 360, f() * 360);
                var e = q.eulerAngles;
                if (i > 0) w.Append(",\n");
                w.Append("[").Append(F(q.x)).Append(",").Append(F(q.y)).Append(",").Append(F(q.z)).Append(",").Append(F(q.w))
                    .Append(",").Append(F(e.x)).Append(",").Append(F(e.y)).Append(",").Append(F(e.z)).Append("]");
            }
            w.Append("],\"fromEuler\":[");
            for (int i = 0; i < 20000; i++)
            {
                var e = i % 2 == 0 ? new Vector3(f() * 400, f() * 400, f() * 400) : new Vector3(rnd.Next(16) * 22.5f, f() * 360, (float)rnd.Next(-90, 91));
                var q = Quaternion.Euler(e);
                if (i > 0) w.Append(",\n");
                w.Append("[").Append(F(e.x)).Append(",").Append(F(e.y)).Append(",").Append(F(e.z))
                    .Append(",").Append(F(q.x)).Append(",").Append(F(q.y)).Append(",").Append(F(q.z)).Append(",").Append(F(q.w)).Append("]");
            }
            w.Append("]}");
        }

        // TerrainModifiers that come with a networked prefab (rocks, roads), local to the prefab root
        static void PrefabTerrainMods(StringBuilder w)
        {
            var ci = System.Globalization.CultureInfo.InvariantCulture;
            Func<float, string> F = x => x.ToString("R", ci);
            w.Append("{");
            bool first = true;
            foreach (var p in ZNetScene.instance.m_prefabs)
            {
                if (!p) continue;
                var mods = p.GetComponentsInChildren<TerrainModifier>(false).Where(t => t.enabled).ToList();
                if (mods.Count == 0) continue;
                if (!first) w.Append(",\n");
                first = false;
                w.Append(Str(p.name)).Append(":[");
                var inv = Quaternion.Inverse(p.transform.rotation);
                for (int i = 0; i < mods.Count; i++)
                {
                    var tm = mods[i];
                    var lp = inv * (tm.transform.position - p.transform.position);
                    if (i > 0) w.Append(",");
                    w.Append("{\"sortOrder\":").Append(tm.m_sortOrder)
                        .Append(",\"player\":").Append(tm.m_playerModifiction ? "true" : "false")
                        .Append(",\"levelOffset\":").Append(F(tm.m_levelOffset))
                        .Append(",\"level\":").Append(tm.m_level ? "true" : "false")
                        .Append(",\"levelRadius\":").Append(F(tm.m_levelRadius))
                        .Append(",\"square\":").Append(tm.m_square ? "true" : "false")
                        .Append(",\"smooth\":").Append(tm.m_smooth ? "true" : "false")
                        .Append(",\"smoothRadius\":").Append(F(tm.m_smoothRadius))
                        .Append(",\"smoothPower\":").Append(F(tm.m_smoothPower))
                        .Append(",\"paint\":").Append(tm.m_paintCleared ? "true" : "false")
                        .Append(",\"paintType\":").Append((int)tm.m_paintType)
                        .Append(",\"paintRadius\":").Append(F(tm.m_paintRadius))
                        .Append(",\"paintHeightCheck\":").Append(tm.m_paintHeightCheck ? "true" : "false")
                        .Append(",\"paintStrength\":").Append(F(tm.m_paintStrength))
                        .Append(",\"pos\":[").Append(F(lp.x)).Append(",").Append(F(lp.y)).Append(",").Append(F(lp.z)).Append("]}");
                }
                w.Append("]");
            }
            w.Append("}");
        }

        // views of locations/rooms whose clone keeps a non-unit localScale that ZNetView syncs to the ZDO
        static void ScaleSurvey(StringBuilder w)
        {
            var counts = new Dictionary<string, int>();
            void Count(string group, GameObject root)
            {
                foreach (var view in Utils.GetEnabledComponentsInChildren<ZNetView>(root))
                {
                    if (view.transform.localScale == Vector3.one) continue;
                    var key = group + "|sync=" + view.m_syncInitialScale + "|" + view.GetPrefabName();
                    counts[key] = counts.TryGetValue(key, out var n) ? n + 1 : 1;
                }
            }
            foreach (var loc in ZoneSystem.instance.m_locations)
                if (loc.m_enable && loc.m_prefab.IsValid) { loc.m_prefab.Load(); Count("location", loc.m_prefab.Asset); }
            foreach (var rd in DungeonDB.GetRooms()) { rd.m_prefab.Load(); Count("room", rd.m_prefab.Asset); }
            w.Append("{");
            bool first = true;
            foreach (var kv in counts.OrderByDescending(k => k.Value))
            {
                if (!first) w.Append(",\n");
                first = false;
                w.Append(Str(kv.Key)).Append(":").Append(kv.Value);
            }
            w.Append("}");
        }

        static void TerrainModSurvey(StringBuilder w)
        {
            var counts = new Dictionary<string, int>();
            void Count(string group, GameObject root)
            {
                foreach (var tm in root.GetComponentsInChildren<TerrainModifier>(true))
                {
                    bool underView = tm.GetComponentInParent<ZNetView>(true) != null;
                    var key = group + "|underView=" + underView + "|level=" + tm.m_level + "|smooth=" + tm.m_smooth +
                        "|paint=" + (tm.m_paintCleared ? tm.m_paintType.ToString() : "no") + "|player=" + tm.m_playerModifiction +
                        "|sort=" + tm.m_sortOrder + "|compiler=" + tm.m_useTerrainCompiler + "|enabled=" + tm.enabled;
                    counts[key] = counts.TryGetValue(key, out var n) ? n + 1 : 1;
                    if (group == "netprefab") counts["netprefab:" + root.name] = 1;
                }
            }
            foreach (var p in ZNetScene.instance.m_prefabs) if (p) Count("netprefab", p);
            foreach (var loc in ZoneSystem.instance.m_locations)
                if (loc.m_enable && loc.m_prefab.IsValid) { loc.m_prefab.Load(); Count("location", loc.m_prefab.Asset); }
            foreach (var rd in DungeonDB.GetRooms()) { rd.m_prefab.Load(); Count("room", rd.m_prefab.Asset); }
            w.Append("{");
            bool first = true;
            foreach (var kv in counts.OrderBy(k => k.Key))
            {
                if (!first) w.Append(",\n");
                first = false;
                w.Append(Str(kv.Key)).Append(":").Append(kv.Value);
            }
            w.Append("}");
        }

        static void Rooms(StringBuilder w)
        {
            w.Append("[");
            bool first = true;
            foreach (var rd in DungeonDB.GetRooms())
            {
                if (!first) w.Append(",\n");
                first = false;
                rd.m_prefab.Load();
                var root = rd.m_prefab.Asset;
                w.Append("{\"name\":").Append(Str(rd.m_prefab.Name));
                w.Append(",\"enabled\":").Append(rd.m_enabled ? "true" : "false");
                w.Append(",\"theme\":").Append((int)rd.m_theme);
                w.Append(",\"room\":");
                Value(w, rd.RoomInPrefab, 0);
                w.Append(",\"connections\":[");
                var conns = rd.RoomInPrefab ? rd.RoomInPrefab.GetConnections() : new RoomConnection[0];
                for (int i = 0; i < conns.Length; i++)
                {
                    if (i > 0) w.Append(",");
                    w.Append("{\"conn\":");
                    Value(w, conns[i], 0);
                    w.Append(",\"localPos\":");
                    Value(w, conns[i].transform.localPosition, 0);
                    w.Append(",\"localRot\":");
                    Value(w, conns[i].transform.localRotation, 0);
                    w.Append("}");
                }
                w.Append("],");
                Children(w, root);
                w.Append("}");
            }
            w.Append("]");
        }

        // Networked children of a location/room prefab, with RandomSpawn/RandomObject/DungeonGenerator
        // linked to view indices. Positions are relative to the prefab root.
        static void Children(StringBuilder w, GameObject root)
        {
            var views = Utils.GetEnabledComponentsInChildren<ZNetView>(root);
            var inv = Quaternion.Inverse(root.transform.rotation);

            w.Append("\"views\":[");
            for (int i = 0; i < views.Length; i++)
            {
                var v = views[i];
                if (i > 0) w.Append(",");
                w.Append("{\"prefab\":").Append(Str(v.GetPrefabName()));
                w.Append(",\"hash\":").Append(v.GetPrefabName().GetStableHashCode());
                w.Append(",\"pos\":");
                Value(w, inv * (v.transform.position - root.transform.position), 0);
                w.Append(",\"rot\":");
                Value(w, inv * v.transform.rotation, 0);
                w.Append(",\"wearNTear\":").Append(v.GetComponent<WearNTear>() ? "true" : "false");
                w.Append(",\"dungeon\":").Append(v.GetComponent<DungeonGenerator>() ? "true" : "false");
                w.Append("}");
            }
            w.Append("]");

            w.Append(",\"randomSpawns\":[");
            var spawns = Utils.GetEnabledComponentsInChildren<RandomSpawn>(root);
            for (int i = 0; i < spawns.Length; i++)
            {
                if (i > 0) w.Append(",");
                spawns[i].Prepare();
                w.Append("{\"fields\":");
                Value(w, spawns[i], 0);
                w.Append(",\"views\":");
                Indices(w, views, spawns[i].m_childNetViews);
                w.Append("}");
            }
            w.Append("]");

            w.Append(",\"randomObjects\":[");
            var objects = Utils.GetEnabledComponentsInChildren<RandomObject>(root);
            for (int i = 0; i < objects.Length; i++)
            {
                var ro = objects[i];
                if (i > 0) w.Append(",");
                w.Append("{\"requireTheme\":").Append((int)ro.m_dungeonRequireTheme);
                w.Append(",\"requireBiome\":").Append((int)ro.m_requireBiome);
                w.Append(",\"notInLava\":").Append(ro.m_notInLava ? "true" : "false");
                w.Append(",\"minElevation\":").Append(ro.m_minElevation);
                w.Append(",\"maxElevation\":").Append(ro.m_maxElevation);
                w.Append(",\"hasView\":").Append(ro.GetComponent<ZNetView>() ? "true" : "false");
                w.Append(",\"ownViews\":");
                Indices(w, views, ro.GetComponentsInChildren<ZNetView>(true));
                w.Append(",\"entries\":[");
                for (int j = 0; j < ro.m_objects.Count; j++)
                {
                    var e = ro.m_objects[j];
                    if (j > 0) w.Append(",");
                    w.Append("{\"weight\":").Append(F(e.m_weight));
                    w.Append(",\"object\":").Append(e.m_object ? Str(e.m_object.name) : "null");
                    w.Append(",\"views\":");
                    Indices(w, views, e.m_object ? e.m_object.GetComponentsInChildren<ZNetView>(true) : new ZNetView[0]);
                    w.Append("}");
                }
                w.Append("]}");
            }
            w.Append("]");
        }

        static void Indices(StringBuilder w, ZNetView[] all, IEnumerable<ZNetView> subset)
        {
            w.Append("[");
            w.Append(string.Join(",", (subset ?? new ZNetView[0]).Select(v => Array.IndexOf(all, v).ToString()).ToArray()));
            w.Append("]");
        }

        static void List(StringBuilder w, IEnumerable items)
        {
            w.Append("[");
            bool first = true;
            foreach (var it in items)
            {
                if (!first) w.Append(",\n");
                first = false;
                Value(w, it, 0);
            }
            w.Append("]");
        }

        static void Enum(StringBuilder w, Type t)
        {
            w.Append("{");
            w.Append(string.Join(",", System.Enum.GetNames(t).Select(n => Str(n) + ":" + Convert.ToInt64(System.Enum.Parse(t, n))).ToArray()));
            w.Append("}");
        }

        // Generic serializer: Unity-serialized fields of plain classes, object refs by name.
        static void Value(StringBuilder w, object o, int depth)
        {
            if (o == null) { w.Append("null"); return; }
            var t = o.GetType();

            if (o is UnityEngine.Object uo)
            {
                if (!uo) { w.Append("null"); return; }
                // components are expanded only at the top level
                if (depth == 0 && o is Component) { Fields(w, o, t, depth); return; }
                var go = uo is Component c ? c.gameObject : uo as GameObject;
                w.Append(Str(go ? Utils.GetPrefabName(go) : uo.name));
                return;
            }
            if (o is string s) { w.Append(Str(s)); return; }
            if (o is bool b) { w.Append(b ? "true" : "false"); return; }
            if (o is float f) { w.Append(F(f)); return; }
            if (o is double d) { w.Append(d.ToString("R", System.Globalization.CultureInfo.InvariantCulture)); return; }
            if (t.IsEnum) { w.Append(Convert.ToInt64(o)); return; }
            if (t.IsPrimitive) { w.Append(Convert.ToString(o, System.Globalization.CultureInfo.InvariantCulture)); return; }
            if (o is Vector3 v3) { w.Append("[").Append(F(v3.x)).Append(",").Append(F(v3.y)).Append(",").Append(F(v3.z)).Append("]"); return; }
            if (o is Vector2 v2) { w.Append("[").Append(F(v2.x)).Append(",").Append(F(v2.y)).Append("]"); return; }
            if (o is Quaternion q) { w.Append("[").Append(F(q.x)).Append(",").Append(F(q.y)).Append(",").Append(F(q.z)).Append(",").Append(F(q.w)).Append("]"); return; }
            if (o is Color col) { w.Append("[").Append(F(col.r)).Append(",").Append(F(col.g)).Append(",").Append(F(col.b)).Append(",").Append(F(col.a)).Append("]"); return; }
            if (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(SoftReference<>))
            {
                var valid = (bool)t.GetProperty("IsValid").GetValue(o, null);
                w.Append(valid ? Str(t.GetProperty("Name").GetValue(o, null) as string) : "null");
                return;
            }
            if (o is IEnumerable en)
            {
                w.Append("[");
                bool first = true;
                foreach (var it in en)
                {
                    if (!first) w.Append(",");
                    first = false;
                    Value(w, it, depth + 1);
                }
                w.Append("]");
                return;
            }
            if (depth >= MaxDepth) { w.Append(Str("<depth>")); return; }
            Fields(w, o, t, depth);
        }

        static void Fields(StringBuilder w, object o, Type t, int depth)
        {
            w.Append("{");
            bool first = true;
            for (var ct = t; ct != null && ct != typeof(object) && ct != typeof(MonoBehaviour); ct = ct.BaseType)
            {
                foreach (var fi in ct.GetFields(Flags | BindingFlags.DeclaredOnly))
                {
                    if (!Serialized(fi)) continue;
                    if (!first) w.Append(",");
                    first = false;
                    w.Append(Str(fi.Name)).Append(":");
                    object val;
                    try { val = fi.GetValue(o); } catch { val = null; }
                    Value(w, val, depth + 1);
                }
            }
            // computed name/biome-parent used by 1.0 placement
            var alt = t.GetProperty("AltBiomeParent");
            if (alt != null)
            {
                if (!first) w.Append(",");
                w.Append("\"AltBiomeParent\":");
                Value(w, alt.GetValue(o, null), depth + 1);
            }
            w.Append("}");
        }

        static bool Serialized(FieldInfo fi)
        {
            if (fi.IsDefined(typeof(NonSerializedAttribute), false)) return false;
            if (fi.Name.Contains("<")) return false; // backing fields
            if (!fi.IsPublic && !fi.IsDefined(typeof(SerializeField), false)) return false;
            // like Unity: skip plain classes that are not [Serializable] (runtime state, cycles)
            var t = fi.FieldType;
            if (t.IsArray) t = t.GetElementType();
            else if (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(List<>)) t = t.GetGenericArguments()[0];
            if (t.IsClass && t != typeof(string) && !typeof(UnityEngine.Object).IsAssignableFrom(t)
                && !t.IsDefined(typeof(SerializableAttribute), false))
                return false;
            return true;
        }

        static string F(float f)
        {
            if (float.IsNaN(f) || float.IsInfinity(f)) return Str(f.ToString());
            return f.ToString("R", System.Globalization.CultureInfo.InvariantCulture);
        }

        static string Str(string s)
        {
            if (s == null) return "null";
            var b = new StringBuilder("\"");
            foreach (var ch in s)
            {
                if (ch == '"' || ch == '\\') b.Append('\\').Append(ch);
                else if (ch < 0x20) b.AppendFormat("\\u{0:x4}", (int)ch);
                else b.Append(ch);
            }
            return b.Append('"').ToString();
        }
    }
}
