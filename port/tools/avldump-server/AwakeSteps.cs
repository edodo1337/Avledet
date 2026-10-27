using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using HarmonyLib;
using UnityEngine;

namespace AvlDumpServer
{
    // What the Awake of a generated prefab does to the random stream and its ZDO, so Avledet can replay it:
    // which DropTable (Container), PickableItem and WearNTear run in what order, and the fixed draws between them.
    // Also the loot data: drop tables, random pickable items, item defaults, start fuel and the like.
    internal static class AwakeSteps
    {
        static UnityEngine.Random.State? s_before;
        static readonly List<string> s_events = new List<string>();

        public static void Patch()
        {
            new Harmony("avledet.dumpserver.awake").PatchAll(typeof(AwakeSteps));
        }

        // random steps since s_before, the state is kept
        static int Steps()
        {
            var now = UnityEngine.Random.state;
            UnityEngine.Random.state = s_before.Value;
            int k = 0;
            while (k < 100000 && !UnityEngine.Random.state.Equals(now)) { UnityEngine.Random.value.GetHashCode(); k++; }
            UnityEngine.Random.state = now;
            return k;
        }

        // index among the clone's components, the same order as on the prefab
        static int Index<T>(T c) where T : Component
        {
            return System.Array.IndexOf(c.transform.root.GetComponentsInChildren<T>(true), c);
        }

        [HarmonyPatch(typeof(Container), "AddDefaultItems"), HarmonyPrefix]
        static void ContainerPre(Container __instance) { if (s_before != null) s_events.Add("droptable " + Index<Container>(__instance) + " " + Steps()); }

        [HarmonyPatch(typeof(Container), "AddDefaultItems"), HarmonyPostfix]
        static void ContainerPost() { if (s_before != null) s_events.Add("end " + Steps()); }

        [HarmonyPatch(typeof(PickableItem), "SetupRandomPrefab"), HarmonyPrefix]
        static void PickablePre(PickableItem __instance) { if (s_before != null) s_events.Add("pickable " + Index<PickableItem>(__instance) + " " + Steps()); }

        [HarmonyPatch(typeof(PickableItem), "SetupRandomPrefab"), HarmonyPostfix]
        static void PickablePost() { if (s_before != null) s_events.Add("end " + Steps()); }

        [HarmonyPatch(typeof(WearNTear), "Awake"), HarmonyPrefix]
        static void WearPre(WearNTear __instance) { if (s_before != null) s_events.Add("wnt " + Index<WearNTear>(__instance) + " " + Steps()); }

        [HarmonyPatch(typeof(WearNTear), "Awake"), HarmonyPostfix]
        static void WearPost() { if (s_before != null) s_events.Add("end " + Steps()); }

        // events of one Instantiate as [kind, component index, gap before, steps inside], plus the tail
        static string Measure(GameObject prefab, bool damage, int seed, out int total)
        {
            WearNTear.m_randomInitialDamage = damage;
            UnityEngine.Random.InitState(seed);
            s_before = UnityEngine.Random.state;
            s_events.Clear();
            var obj = UnityEngine.Object.Instantiate(prefab, new Vector3(0, -5000, 0), Quaternion.identity);
            total = Steps();
            s_before = null;
            WearNTear.m_randomInitialDamage = false;
            var nview = obj.GetComponent<ZNetView>();
            if (nview && nview.GetZDO() != null) ZNetScene.instance.Destroy(obj); else UnityEngine.Object.Destroy(obj);

            var w = new StringBuilder("[");
            int pos = 0;
            for (int i = 0; i + 1 < s_events.Count; i += 2)
            {
                var a = s_events[i].Split(' ');
                var b = s_events[i + 1].Split(' ');
                int start = int.Parse(a[2]), end = int.Parse(b[1]);
                if (i > 0) w.Append(",");
                w.Append("[\"").Append(a[0]).Append("\",").Append(a[1]).Append(",").Append(start - pos).Append(",").Append(end - start).Append("]");
                pos = end;
            }
            w.Append("],").Append(total - pos);
            return w.ToString();
        }

        // prefabs that PersistentEventSystem instantiates (ObjectSpawnSettings.prefabs)
        public static IEnumerable<GameObject> EventPrefabs()
        {
            foreach (var e in PersistentEventSystem.instance.m_possibleEvents)
                foreach (var o in e.objectsToSpawn)
                    foreach (var p in (List<GameObject>)AccessTools.Field(typeof(PersistentEventSystem.ObjectSpawnSettings), "prefabs").GetValue(o))
                        if (p) yield return p;
        }

        // the views an Instantiate of an event prefab creates ZDOs for, posed with the root at the origin
        public static void EventObjects(StringBuilder w)
        {
            w.Append("{");
            bool first = true;
            foreach (var prefab in EventPrefabs().Distinct())
            {
                var pos = prefab.transform.position;
                var rot = prefab.transform.rotation;
                prefab.transform.position = Vector3.zero;
                prefab.transform.rotation = Quaternion.identity;
                if (!first) w.Append(",\n");
                first = false;
                w.Append(S(prefab.name)).Append(":[");
                bool f2 = true;
                foreach (var view in prefab.GetComponentsInChildren<ZNetView>())
                {
                    var t = view.transform;
                    if (!f2) w.Append(",");
                    f2 = false;
                    w.Append("[").Append(S(view.GetPrefabName())).Append(",").Append(F(t.position.x)).Append(",").Append(F(t.position.y)).Append(",").Append(F(t.position.z))
                        .Append(",").Append(F(t.rotation.x)).Append(",").Append(F(t.rotation.y)).Append(",").Append(F(t.rotation.z)).Append(",").Append(F(t.rotation.w))
                        .Append(",").Append(F(t.localScale.x)).Append(",").Append(F(t.localScale.y)).Append(",").Append(F(t.localScale.z)).Append("]");
                }
                w.Append("]");
                prefab.transform.position = pos;
                prefab.transform.rotation = rot;
            }
            w.Append("}");
        }

        // world modifier GUI data the remote commands setkey/setworldmodifier/setworldpreset go through
        public static void ServerOptions(StringBuilder w)
        {
            var gui = ServerOptionsGUI.m_instance;
            var modifiers = (KeyUI[])AccessTools.Field(typeof(ServerOptionsGUI), "m_modifiers").GetValue(null);
            var presets = (KeyButton[])AccessTools.Field(typeof(ServerOptionsGUI), "m_presets").GetValue(null);
            if (!gui || modifiers == null || presets == null)
            {
                w.Append("null");
                return;
            }
            string L(IEnumerable<string> keys) => "[" + string.Join(",", keys.Select(S).ToArray()) + "]";
            w.Append("{\"sliders\":[");
            w.Append(string.Join(",\n", modifiers.OfType<KeySlider>().Select(k => "[" + S(k.m_modifier.ToString()) + ",[" +
                string.Join(",", k.m_settings.Select(s => "[" + S(s.m_modifierValue.ToString()) + "," + L(s.m_keys) + "]").ToArray()) + "]]").ToArray()));
            w.Append("],\n\"toggles\":").Append(L(modifiers.OfType<KeyToggle>().Select(t => t.m_enabledKey)));
            w.Append(",\n\"presets\":[");
            w.Append(string.Join(",\n", presets.Select(p => "[" + S(p.m_preset.ToString()) + "," + L(p.m_keys) + "]").ToArray()));
            w.Append("],\n\"order\":").Append(L(modifiers.Select(m => m is KeySlider ks ? "slider " + ks.m_modifier : m is KeyToggle kt ? "toggle " + kt.m_enabledKey : m.GetType().Name)));
            w.Append("}");
        }

        static readonly CultureInfo Ci = CultureInfo.InvariantCulture;
        static string F(float f) => f.ToString("R", Ci);
        static string S(string s) => "\"" + s.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"";

        public static void Dump(StringBuilder w, IEnumerable<string> names)
        {
            var saved = UnityEngine.Random.state;
            var items = new HashSet<ItemDrop>();
            w.Append("{\"steps\":{");
            bool first = true;
            foreach (var name in names.OrderBy(n => n))
            {
                var prefab = ZNetScene.instance.GetPrefab(name);
                if (!prefab) continue;
                if (!prefab.GetComponentInChildren<Container>(true) && !prefab.GetComponentInChildren<PickableItem>(true) && !prefab.GetComponentInChildren<WearNTear>(true))
                    continue;
                // the structure must not depend on the seed: only the steps inside a DropTable vary
                var shapes = new List<string>();
                string[] modes = new string[2];
                foreach (bool damage in new[] { false, true })
                {
                    var forSeeds = new HashSet<string>();
                    foreach (int seed in new[] { 1, 12345, -777 })
                    {
                        var m = Measure(prefab, damage, seed, out _);
                        // drop the variable inner count of droptables for the comparison
                        forSeeds.Add(System.Text.RegularExpressions.Regex.Replace(m, "\\[\"droptable\",(-?\\d+),(\\d+),\\d+\\]", "[\"droptable\",$1,$2,0]"));
                        modes[damage ? 1 : 0] = m;
                    }
                    if (forSeeds.Count != 1)
                        Debug.LogWarning("[AvlDumpServer] awake steps of " + name + " depend on the seed: " + string.Join(" | ", forSeeds.ToArray()));
                    shapes.Add(forSeeds.First());
                }
                if (!first) w.Append(",\n");
                first = false;
                w.Append(S(name)).Append(":[[").Append(shapes[0]).Append("],[").Append(shapes[1]).Append("]]");
            }
            UnityEngine.Random.state = saved;

            w.Append("},\n\"containers\":{");
            first = true;
            foreach (var prefab in ZNetScene.instance.m_prefabs.Where(p => p).OrderBy(p => p.name))
            {
                var containers = prefab.GetComponentsInChildren<Container>(true);
                if (containers.Length == 0) continue;
                if (!first) w.Append(",\n");
                first = false;
                w.Append(S(prefab.name)).Append(":[");
                for (int i = 0; i < containers.Length; i++)
                {
                    var c = containers[i];
                    var t = c.m_defaultItems;
                    if (i > 0) w.Append(",");
                    w.Append("{\"name\":").Append(S(c.m_name)).Append(",\"w\":").Append(c.m_width).Append(",\"h\":").Append(c.m_height)
                        .Append(",\"chance\":").Append(F(t.m_dropChance)).Append(",\"min\":").Append(t.m_dropMin).Append(",\"max\":").Append(t.m_dropMax)
                        .Append(",\"oneOfEach\":").Append(t.m_oneOfEach ? "true" : "false").Append(",\"drops\":[");
                    for (int j = 0; j < t.m_drops.Count; j++)
                    {
                        var d = t.m_drops[j];
                        if (j > 0) w.Append(",");
                        w.Append("[").Append(S(d.m_item ? d.m_item.name : "")).Append(",").Append(F(d.m_weight)).Append(",").Append(d.m_stackMin)
                            .Append(",").Append(d.m_stackMax).Append(",").Append(d.m_dontScale ? "true" : "false").Append("]");
                        if (d.m_item && d.m_item.GetComponent<ItemDrop>()) items.Add(d.m_item.GetComponent<ItemDrop>());
                    }
                    w.Append("]}");
                }
                w.Append("]");
            }

            w.Append("},\n\"pickables\":{");
            first = true;
            foreach (var prefab in ZNetScene.instance.m_prefabs.Where(p => p).OrderBy(p => p.name))
            {
                var pickables = prefab.GetComponentsInChildren<PickableItem>(true);
                if (pickables.Length == 0) continue;
                if (!first) w.Append(",\n");
                first = false;
                w.Append(S(prefab.name)).Append(":[");
                for (int i = 0; i < pickables.Length; i++)
                {
                    var p = pickables[i];
                    if (i > 0) w.Append(",");
                    // a fixed item needs no random pick
                    w.Append("{\"fixed\":").Append(p.m_itemPrefab ? "true" : "false").Append(",\"items\":[");
                    for (int j = 0; j < p.m_randomItemPrefabs.Length; j++)
                    {
                        var r = p.m_randomItemPrefabs[j];
                        if (j > 0) w.Append(",");
                        w.Append("[").Append(S(r.m_itemPrefab ? r.m_itemPrefab.name : "")).Append(",").Append(r.m_stackMin).Append(",").Append(r.m_stackMax).Append("]");
                        if (r.m_itemPrefab) items.Add(r.m_itemPrefab);
                    }
                    w.Append("]}");
                }
                w.Append("]");
            }

            // ItemDrop.ItemData defaults of the items above: shared name, max stack, type, durability, quality, variant
            w.Append("},\n\"items\":{");
            first = true;
            foreach (var item in items.OrderBy(i => i.name))
            {
                var d = item.m_itemData;
                if (!first) w.Append(",\n");
                first = false;
                w.Append(S(item.name)).Append(":[").Append(S(d.m_shared.m_name)).Append(",").Append(d.m_shared.m_maxStackSize).Append(",")
                    .Append((int)d.m_shared.m_itemType).Append(",").Append(F(d.m_durability)).Append(",").Append(d.m_quality).Append(",")
                    .Append(d.m_variant).Append(",").Append(d.m_customData.Count).Append("]");
            }

            // other Awake writes of a new owned ZDO
            w.Append("},\n\"startFuel\":{");
            first = true;
            foreach (var prefab in ZNetScene.instance.m_prefabs.Where(p => p).OrderBy(p => p.name))
            {
                var f = prefab.GetComponent<Fireplace>();
                if (!f) continue;
                if (!first) w.Append(",");
                first = false;
                w.Append(S(prefab.name)).Append(":").Append(F(f.m_startFuel));
            }
            w.Append("},\n\"enabledByDefault\":[");
            w.Append(string.Join(",", ZNetScene.instance.m_prefabs.Where(p => p && p.GetComponent<PrivateArea>() && p.GetComponent<PrivateArea>().m_enabledByDefault)
                .Select(p => S(p.name)).OrderBy(s => s).ToArray()));
            w.Append("],\n\"spawnTime\":[");
            w.Append(string.Join(",", ZNetScene.instance.m_prefabs.Where(p => p && p.GetComponent<ItemDrop>() && p.GetComponent<ZNetView>())
                .Select(p => S(p.name)).OrderBy(s => s).ToArray()));
            w.Append("],\n\"plantTime\":[");
            w.Append(string.Join(",", ZNetScene.instance.m_prefabs.Where(p => p && p.GetComponent<Plant>())
                .Select(p => S(p.name)).OrderBy(s => s).ToArray()));
            // Game and RandEventSystem settings the server uses with world modifiers
            w.Append("],\n\"nonScaledDropTypes\":[");
            w.Append(string.Join(",", Game.instance.m_nonScaledDropTypes.Select(t => ((int)t).ToString()).ToArray()));
            w.Append("],\n\"eventIntervalMin\":").Append(F(RandEventSystem.instance.m_eventIntervalMin));
            w.Append(",\"eventChance\":").Append(F(RandEventSystem.instance.m_eventChance));
            w.Append("}");
        }
    }
}
