using BepInEx;
using HarmonyLib;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnityEngine;




namespace AvlDumpServer
{
    internal class AvlDumper
    {
        static void LogInfo(object message)
        {
            //var fmt = $"<color=#AA2266>[AVL]</color><color=#777777>{message}</color>";
            var fmt = "[AVL-info] " + message;
            ZLog.Log(fmt);
            //Chat.print(fmt);
        }

        static void LogWarning(object message)
        {
            //var fmt = $"<color=#AA2266>[AVL]</color><color=#CC2222>{message}</color>";
            var fmt = "[AVL-warn] " + message;
            ZLog.Log(fmt);
            //Chat.print(fmt);
        }

        static void LogError(object message)
        {
            //var fmt = $"<color=#AA2266>[AVL]</color><color=#AA0000>{message}</color>";
            var fmt = "[AVL-erro] " + message;
            ZLog.Log(fmt);
            //Chat.print(fmt);
        }

        public static void DumpDocs()
        {
            // Dump documentation
            Directory.CreateDirectory(Plugin.DocOutPath);

            {
                /* * * * * * * * * * * * * * * * * * * * * 
                 * 
                 *
                 *      PREFAB DOCUMENTATION DUMPING
                 * 
                 * 
                 * * * * * * * * * * * * * * * * * * * * */

                StringBuilder builder = new StringBuilder();

                builder.Append("Documentation automatically generated from Valheim ").Append(Version.CurrentVersion.ToString())
                    .AppendLine(",,");
                foreach (var pair in ZNetScene.instance.m_namedPrefabs)
                {
                    var obj = pair.Value;
                    ItemDrop item = obj.GetComponent<ItemDrop>();
                    HoverText hover = obj.GetComponent<HoverText>();
                    // name, hash
                    builder
                        .Append("\"")
                            .Append((obj.name)
                                + (item != null ? " (" + item.m_itemData.m_shared.m_name + ") (" + Localization.instance.Localize(item.m_itemData.m_shared.m_name) + ")" : ""))
                            .Append(hover != null ? (" (" + hover.m_text + ") (" + hover.GetHoverName() + ")") : "")
                        .Append("\", ")
                        .Append(pair.Key).Append(", ");

                    var components = obj.GetComponents<MonoBehaviour>();
                    if (components.Length > 0)
                    {
                        builder.Append("\"");
                        //foreach (var comp in components)
                        for (int i = 0; i < components.Length; i++)
                        {
                            var comp = components[i];
                            builder.Append(comp.GetType().ToString());
                            if (i < components.Length - 1)
                            {
                                builder.Append(", ");
                            }
                        }
                        builder.Append("\"");
                    }

                    // now components, followed by blank commas
                    builder.AppendLine();
                }

                File.WriteAllText(Plugin.DocOutPath + "prefabs.csv", builder.ToString());
            }

            {
                /* * * * * * * * * * * * * * * * * * * * * 
                 * 
                 *
                 *      VERSION DOCUMENTATION DUMPING
                 * 
                 * 
                 * * * * * * * * * * * * * * * * * * * * */

                StringBuilder builder = new StringBuilder();

                builder.Append("Documentation automatically generated from Valheim ").Append(Version.CurrentVersion.ToString())
                    .AppendLine(",,");

                builder.Append("Current version,").Append(Version.CurrentVersion.ToString()).AppendLine();
                builder.Append("Network version,").Append(Version.c_networkVersion).AppendLine();
                builder.Append("World version,").Append((int)Version.c_WorldVersion).AppendLine();
                builder.Append("Worldgen version,").Append(Version.c_WorldGenVersion).AppendLine();
                builder.Append("Location version,").Append(ZoneSystem.instance.m_locationVersion).AppendLine();
                builder.Append("Player version,").Append((int)Version.c_PlayerVersion).AppendLine();
                builder.Append("Player data version,").Append((int)Version.c_PlayerDataVersion).AppendLine();
                builder.Append("Item data version,").Append((int)Version.c_ItemDataVersion).AppendLine();

                File.WriteAllText(Plugin.DocOutPath + "version.csv", builder.ToString());
            }

            {
                // dump vegetation

            }

        }

        // Indices into netViews of the views under obj, inactive included (RandomObject.SetSpawned)
        private static List<ushort> ViewIndices(ZNetView[] netViews, GameObject obj)
        {
            var list = new List<ushort>();
            foreach (var view in obj.GetComponentsInChildren<ZNetView>(true))
            {
                var index = Array.IndexOf(netViews, view);
                if (index != -1)
                    list.Add((ushort)index);
            }
            return list;
        }

        private static void WriteIndices(List<ushort> indices, ref ZPackage pkg)
        {
            pkg.Write(indices.Count);
            foreach (var index in indices)
                pkg.Write(index);
        }

        // RandomSpawn and RandomObject of a location or room, views are referenced by index in netViews.
        // Order matters: vanilla draws for every RandomSpawn, then for every RandomObject.
        private static void WriteRandom(GameObject root, ZNetView[] netViews, ref ZPackage pkg)
        {
            if (netViews.Length > 0xFFFF)
                throw new Exception("netviews limited to 65535; change this limit otherwise");

            Quaternion inv = Quaternion.Inverse(root.transform.rotation);

            // vanilla collects all lists before randomizing; RandomSpawn.Reset below changes active states
            var randomSpawns = Utils.GetEnabledComponentsInChildren<RandomSpawn>(root);
            var randomObjects = Utils.GetEnabledComponentsInChildren<RandomObject>(root);
            pkg.Write(randomSpawns.Length);
            foreach (var spawn in randomSpawns)
            {
                spawn.Prepare();

                pkg.Write(spawn.m_chanceToSpawn);
                pkg.Write((int)spawn.m_dungeonRequireTheme);
                pkg.Write((ushort)spawn.m_requireBiome);
                pkg.Write(spawn.m_notInLava);
                pkg.Write(spawn.m_minElevation);
                pkg.Write(spawn.m_maxElevation);

                // views turned off when not spawned (self included)
                var indices = new List<ushort>();
                foreach (var childView in spawn.m_childNetViews)
                {
                    var childIndex = Array.IndexOf(netViews, childView);
                    if (childIndex == -1)
                        throw new Exception("unable to find matching child in netview table for given randomspawn: " + childView.GetPrefabName());
                    indices.Add((ushort)childIndex);
                }
                WriteIndices(indices, ref pkg);

                // 1.0
                pkg.Write(inv * (spawn.transform.position - root.transform.position));
                // m_OffObject is toggled by SetActive, it only matters when it is a view itself
                var off = spawn.m_OffObject ? spawn.m_OffObject.GetComponent<ZNetView>() : null;
                int offIndex = off ? Array.IndexOf(netViews, off) : -1;
                pkg.Write(offIndex);
                // an inactive view as m_OffObject is not spawned with the views, but the proxy/room
                // instantiate turns it on and its ZNetView.Awake creates a ZDO
                bool offProxy = off && offIndex == -1;
                // only when it ends up active in hierarchy: the views are turned off before that
                // instantiate, and an inactive parent keeps it off too
                // under the RandomSpawn itself it goes off with it
                if (offProxy && off.transform.IsChildOf(spawn.transform))
                    offProxy = false;
                for (var t = off ? off.transform.parent : null; offProxy && t && t != root.transform; t = t.parent)
                    if (!t.gameObject.activeSelf || Array.IndexOf(netViews, t.GetComponent<ZNetView>()) != -1)
                        offProxy = false;
                pkg.Write(offProxy ? off.GetPrefabName().GetStableHashCode() : 0);
                if (offProxy)
                {
                    pkg.Write(inv * (off.transform.position - root.transform.position));
                    pkg.Write(inv * off.transform.rotation);
                }

                // no Reset: it turns m_OffObject off in the prefab and later spawns would see it off
            }

            // views nested in other views: Instantiate of the parent clones them too, so vanilla
            // creates their ZDO twice (once with the parent, once on their own)
            var nested = new List<KeyValuePair<int, List<ushort>>>();
            for (int i = 0; i < netViews.Length; i++)
            {
                var children = netViews[i].GetComponentsInChildren<ZNetView>(true)
                    .Where(v => v != netViews[i]).Select(v => Array.IndexOf(netViews, v)).Where(k => k != -1)
                    .Select(k => (ushort)k).ToList();
                if (children.Count > 0)
                    nested.Add(new KeyValuePair<int, List<ushort>>(i, children));
            }
            pkg.Write(nested.Count);
            foreach (var n in nested)
            {
                pkg.Write(n.Key);
                WriteIndices(n.Value, ref pkg);
            }

            pkg.Write(randomObjects.Length);
            foreach (var obj in randomObjects)
            {
                pkg.Write((int)obj.m_dungeonRequireTheme);
                pkg.Write((ushort)obj.m_requireBiome);
                pkg.Write(obj.m_notInLava);
                pkg.Write(obj.m_minElevation);
                pkg.Write(obj.m_maxElevation);
                pkg.Write(inv * (obj.transform.position - root.transform.position));
                WriteIndices(ViewIndices(netViews, obj.gameObject), ref pkg); // all off when nothing is picked

                pkg.Write(obj.m_objects.Count);
                foreach (var entry in obj.m_objects)
                {
                    pkg.Write(entry.m_weight);
                    pkg.Write(entry.m_object != null);
                    WriteIndices(entry.m_object ? ViewIndices(netViews, entry.m_object) : new List<ushort>(), ref pkg);
                    pkg.Write(entry.m_object ? entry.m_object.GetInstanceID() : 0); // entries compare by object
                }
            }

            // views with SnapToGround: SnappAll puts them on the (modified) ground + offset
            var snaps = new List<KeyValuePair<int, float>>();
            for (int i = 0; i < netViews.Length; i++)
            {
                var snap = netViews[i].GetComponent<SnapToGround>();
                // Awake (m_allSnappers.Add) runs for a disabled component too
                if (snap)
                    snaps.Add(new KeyValuePair<int, float>(i, snap.m_offset));
            }
            pkg.Write(snaps.Count);
            foreach (var sn in snaps)
            {
                pkg.Write(sn.Key);
                pkg.Write(sn.Value);
            }

            // Visuals (proxy or room instantiate, views are off there): TerrainModifiers and SnapToGround.
            // Each is live when all its toggled ancestors are on after the randomization.
            bool Visual(UnityEngine.Component c)
            {
                for (var t = c.transform; t && t != root.transform; t = t.parent)
                {
                    if (Array.IndexOf(netViews, t.GetComponent<ZNetView>()) != -1) return false;      // views are off
                    if (!t.gameObject.activeSelf && !randomSpawns.Any(r => r.m_OffObject == t.gameObject)) return false;
                }
                return true;
            }
            var pk = pkg; // ZPackage is a class, a local can be captured
            void WriteToggles(UnityEngine.Component c)
            {
                var rs = new List<ushort>(); var offs = new List<ushort>(); var ros = new List<ushort>();
                var entries = new List<KeyValuePair<int, int>>();
                for (var t = c.transform; t && t != root.transform; t = t.parent)
                {
                    for (int k = 0; k < randomSpawns.Length; k++)
                    {
                        if (randomSpawns[k].transform == t) rs.Add((ushort)k);
                        if (randomSpawns[k].m_OffObject == t.gameObject) offs.Add((ushort)k);
                    }
                    for (int k = 0; k < randomObjects.Length; k++)
                    {
                        if (randomObjects[k].transform == t) ros.Add((ushort)k);
                        foreach (var e in randomObjects[k].m_objects)
                            if (e.m_object == t.gameObject) entries.Add(new KeyValuePair<int, int>(k, e.m_object.GetInstanceID()));
                    }
                }
                WriteIndices(rs, ref pk);
                WriteIndices(offs, ref pk);
                WriteIndices(ros, ref pk);
                pk.Write(entries.Count);
                foreach (var e in entries) { pk.Write(e.Key); pk.Write(e.Value); }
            }

            var mods = root.GetComponentsInChildren<TerrainModifier>(true).Where(t => t.enabled && Visual(t)).ToList();
            pkg.Write(mods.Count);
            foreach (var tm in mods)
            {
                pkg.Write(tm.m_sortOrder);
                pkg.Write(tm.m_playerModifiction);
                pkg.Write(tm.m_levelOffset);
                pkg.Write(tm.m_level);
                pkg.Write(tm.m_levelRadius);
                pkg.Write(tm.m_square);
                pkg.Write(tm.m_smooth);
                pkg.Write(tm.m_smoothRadius);
                pkg.Write(tm.m_smoothPower);
                pkg.Write(tm.m_paintCleared);
                pkg.Write((int)tm.m_paintType);
                pkg.Write(tm.m_paintRadius);
                pkg.Write(tm.m_paintHeightCheck);
                pkg.Write(tm.m_paintStrength);
                pkg.Write(inv * (tm.transform.position - root.transform.position));
                WriteToggles(tm);
            }

            // only whether any is live matters: SnappAll regenerates the heightmaps only with snappers
            var visualSnaps = root.GetComponentsInChildren<SnapToGround>(true).Where(t => Visual(t)).ToList();
            pkg.Write(visualSnaps.Count);
            foreach (var sn in visualSnaps)
                WriteToggles(sn);

            // colliders for the vegetation raycasts: owner view (-1 visual), toggles, shape
            var colliders = Colliders.Find(root, netViews, randomSpawns);
            pkg.Write(colliders.Count);
            foreach (var c in colliders)
            {
                pkg.Write(c.Value);
                WriteToggles(c.Key);
                Colliders.Write(c.Key, root.transform, pkg);
            }

            // ConditionalObject.Awake turns m_enableObject off unless its global key condition holds,
            // the views under it then never wake up and get no ZDO
            var conditionals = Utils.GetEnabledComponentsInChildren<ConditionalObject>(root)
                .Where(co => co.m_enableObject).ToList();
            var hidden = conditionals.Select(co => co.m_enableObject.GetComponentsInChildren<ZNetView>(true)
                .Select(v => Array.IndexOf(netViews, v)).Where(k => k != -1).Select(k => (ushort)k).ToList()).ToList();
            pkg.Write(conditionals.Count(co => hidden[conditionals.IndexOf(co)].Count > 0));
            for (int i = 0; i < conditionals.Count; i++)
            {
                if (hidden[i].Count == 0)
                    continue;
                pkg.Write(conditionals[i].m_globalKeyCondition ?? "");
                pkg.Write(conditionals[i].m_disableOnCondition);
                WriteIndices(hidden[i], ref pkg);
            }
        }

        public static void DumpPackages()
        {
            /*
            foreach (var pair in __instance.m_locationsByHash)
            {
                var zoneLocation = pair.Value;

                Location componentInChildren = zoneLocation.m_location;
                var interiorTransform = componentInChildren.m_interiorTransform;
                if (interiorTransform && componentInChildren.m_generator) {
                    LogWarning(zoneLocation.m_prefabName);
                    LogWarning(" - m_generatorPosition: " + zoneLocation.m_generatorPosition);
                    LogWarning(" - m_interiorTransform: ");

                    LogWarning("   - .name: " + interiorTransform.name);
                    LogWarning("   - .parent.name: " + interiorTransform.parent.name);

                    LogWarning("   - .localPosition: " + interiorTransform.localPosition);
                    LogWarning("   - .localRotation: " + interiorTransform.localRotation);

                    int count = interiorTransform.childCount;
                    if (count > 0)
                    {
                        LogWarning("   - .children: ");
                        for (int i=0; i < count; i++)
                        {
                            LogWarning("     - " + interiorTransform.GetChild(i).name);
                        }
                    }
                }
            }*/

            LogInfo("Starting game data dump...");
            LogInfo("This might take a few minutes depending on hardware!");

            Directory.CreateDirectory(Plugin.PkgOutPath);

            string comment = string.Format("{0:MM/dd/yyyy hh:mm.ss}", DateTime.Now);

            {
                LogInfo("------------------- Dumping Prefabs -------------------");

                int idx = 0;

                // Prepare prefabs
                List<GameObject> prefabs = new List<GameObject>();
                foreach (var prefab in ZNetScene.instance.m_prefabs)
                //foreach (var pair in ZNetScene.instance.m_namedPrefabs)
                {
                    //var prefab = pair.Value;
                    if (!prefabs.Contains(prefab))
                    {
                        if (prefab.GetComponent<ZNetView>())
                        {
                            LogInfo("+fab " + prefab.name + " (" + idx + ")");
                            prefabs.Add(prefab);
                        }
                        else
                        {
                            LogError("-fab " + prefab.name + " (no nview, " + idx + ")");
                        }
                    }
                    else
                    {
                        LogError("-fab " + prefab.name + " (dup, " + idx + ")");
                    }

                    idx++;
                }

                var staticSolidRayMask = LayerMask.GetMask(new string[]
                {
                        "static_solid",
                        "terrain"
                });

                // Dump prefabs
                ZPackage pkg = new ZPackage();

                pkg.Write(comment);
                pkg.Write(Version.CurrentVersion.ToString()); // write version for reference purposes
                pkg.Write(prefabs.Count);
                foreach (var prefab in prefabs)
                {
                    var view = prefab.GetComponent<ZNetView>();

                    // full name, the ZNetScene key; "SnowFirTree 2" and "SnowFirTree" are two prefabs
                    var prefabName = prefab.name;

                    //if ((prefab.layer & staticSolidRayMask) > 0)
                    //LogWarning("staticSolid: " + prefab.name);

                    pkg.Write(prefabName); // Required
                    pkg.Write(prefabName.GetStableHashCode()); // Optional for debug
                    pkg.Write(view.gameObject.transform.localScale);

                    List<bool> flags = new List<bool>();

                    int type = (int)view.m_type;

                    flags.Add(view.m_syncInitialScale);
                    flags.Add(view.m_distant);
                    flags.Add(view.m_persistent);
                    flags.Add((type & 0b01) == 0b01);
                    flags.Add((type & 0b10) == 0b10);

                    flags.Add(prefab.GetComponent<Piece>() != null);
                    flags.Add(prefab.GetComponent<Bed>() != null);
                    flags.Add(prefab.GetComponent<Door>() != null);
                    flags.Add(prefab.GetComponent<Chair>() != null);
                    flags.Add(prefab.GetComponent<Ship>() != null);
                    flags.Add(prefab.GetComponent<Fish>() != null);   // Fish is also ItemDrop...
                    flags.Add(prefab.GetComponent<Plant>() != null);
                    flags.Add(prefab.GetComponent<ArmorStand>() != null);

                    flags.Add(prefab.GetComponent<Projectile>() != null);
                    flags.Add(prefab.GetComponent<ItemDrop>() != null);
                    flags.Add(prefab.GetComponent<Pickable>() != null);
                    flags.Add(prefab.GetComponent<PickableItem>() != null);

                    flags.Add(prefab.GetComponent<Container>() != null);
                    flags.Add(prefab.GetComponent<CookingStation>() != null);
                    flags.Add(prefab.GetComponent<CraftingStation>() != null);
                    flags.Add(prefab.GetComponent<Smelter>() != null);
                    flags.Add(prefab.GetComponent<Fireplace>() != null);

                    flags.Add(prefab.GetComponent<WearNTear>() != null);
                    flags.Add(prefab.GetComponent<Destructible>() != null);
                    flags.Add(prefab.GetComponent<ItemStand>() != null);

                    flags.Add(prefab.GetComponent<AnimalAI>() != null);
                    flags.Add(prefab.GetComponent<MonsterAI>() != null);
                    flags.Add(prefab.GetComponent<Tameable>() != null);
                    flags.Add(prefab.GetComponent<Procreation>() != null);

                    flags.Add(prefab.GetComponent<MineRock5>() != null);
                    flags.Add(prefab.GetComponent<TreeBase>() != null);   // natural tree
                    flags.Add(prefab.GetComponent<TreeLog>() != null);   // chopped down tree

                    flags.Add(prefab.GetComponent<DungeonGenerator>() != null);
                    flags.Add(prefab.GetComponent<TerrainModifier>() != null);
                    flags.Add(prefab.GetComponent<CreatureSpawner>() != null);
                    flags.Add(prefab.GetComponent<ZSyncTransform>() != null);

                    ulong mask = 0;
                    for (int i = 0; i < flags.Count; i++)
                    {
                        mask |= flags[i] ? (ulong)1 << i : 0;
                    }

                    pkg.Write(mask);

                }

                File.WriteAllBytes(Plugin.PkgOutPath + "prefabs.pkg", pkg.GetArray());

                var message = "Dumped " + prefabs.Count + "/" + ZNetScene.instance.m_prefabs.Count + " prefabs";
                LogWarning(message);
            }



            //List<DungeonGenerator> dungeons = new List<DungeonGenerator>();
            //List<int> dungeons = new List<int>();

            //HashSet<int> dungeons = new HashSet<int>(); // Contains prefab hashes

            Dictionary<int, KeyValuePair<ZoneSystem.ZoneLocation, DungeonGenerator>> dungeons
                = new Dictionary<int, KeyValuePair<ZoneSystem.ZoneLocation, DungeonGenerator>>();

            {
                LogInfo("------------------- Dumping ZoneLocations -------------------");

                int idx = 0;

                // Prepare locations
                List<ZoneSystem.ZoneLocation> locations = new List<ZoneSystem.ZoneLocation>();
                foreach (var loc in from a in ZoneSystem.instance.m_locations
                                    orderby a.m_prioritized descending
                                    select a
                )
                {
                    if (loc.m_enable)
                    {
                        if (loc.m_quantity > 0)
                        {
                            LogInfo("+loc " + loc.m_prefabName + " (" + idx + ")");

                            loc.m_prefab.Load();

                            locations.Add(loc);
                        }
                        else
                        {
                            LogWarning("-loc " + loc.m_prefabName + " (0qty, " + idx + ")");
                        }
                    }
                    else
                    {
                        LogInfo("-loc " + loc.m_prefabName + " (disabled, " + idx + ")");
                    }

                    idx++;
                }



                // Dump locations
                ZPackage pkg = new ZPackage();

                pkg.Write(comment);
                pkg.Write(Version.CurrentVersion.ToString()); // write version for reference purposes
                pkg.Write(locations.Count);
                foreach (var zloc in locations)
                {
                    zloc.m_prefab.Asset.transform.position = Vector3.zero;
                    zloc.m_prefab.Asset.transform.rotation = Quaternion.identity;

                    var loc = zloc.m_prefab.Asset.GetComponent<Location>();

                    pkg.Write(zloc.m_prefabName);
                    pkg.Write((int)zloc.m_biome);
                    pkg.Write((int)zloc.m_biomeArea);
                    pkg.Write(loc.m_applyRandomDamage); // Yes, this is still used, just not embedded inside ZoneLocation
                    pkg.Write(zloc.m_centerFirst);
                    pkg.Write(zloc.m_clearArea); // vanilla PlaceLocations reads the ZoneLocation flag, not Location's
                    pkg.Write(zloc.m_exteriorRadius);
                    pkg.Write(zloc.m_interiorRadius);
                    pkg.Write(zloc.m_forestTresholdMin);
                    pkg.Write(zloc.m_forestTresholdMax);
                    pkg.Write(zloc.m_minDistanceFromCenter);                            // TODO ash
                    pkg.Write(zloc.m_maxDistanceFromCenter);                            // TODO ash
                    pkg.Write(zloc.m_group);
                    pkg.Write(zloc.m_iconAlways);
                    pkg.Write(zloc.m_iconPlaced);
                    pkg.Write(zloc.m_inForest);
                    pkg.Write(zloc.m_minAltitude);
                    pkg.Write(zloc.m_maxAltitude);
                    pkg.Write(zloc.m_minDistance);
                    pkg.Write(zloc.m_maxDistance);
                    pkg.Write(zloc.m_minTerrainDelta);
                    pkg.Write(zloc.m_maxTerrainDelta);
                    pkg.Write(zloc.m_minimumVegetation);                                // TODO ash
                    pkg.Write(zloc.m_maximumVegetation);                                // TODO ash
                    pkg.Write(zloc.m_surroundCheckVegetation);                          // TODO ash
                    pkg.Write(zloc.m_surroundCheckDistance);                            // TODO ash
                    pkg.Write(zloc.m_surroundCheckLayers);                              // TODO ash
                    pkg.Write(zloc.m_surroundBetterThanAverage);                        // TODO ash
                    pkg.Write(zloc.m_minDistanceFromSimilar);
                    pkg.Write(zloc.m_groupMax);                                         // TODO ash
                    pkg.Write(zloc.m_maxDistanceFromSimilar);                           // TODO ash
                    pkg.Write(zloc.m_prioritized ? 200000 : 100000); // spawnAttempts
                    pkg.Write(zloc.m_quantity);
                    pkg.Write(zloc.m_randomRotation);
                    pkg.Write(zloc.m_slopeRotation);
                    pkg.Write(zloc.m_snapToWater);
                    pkg.Write(zloc.m_unique);



                    /*
                     * 
                     * Gather dungeon names for later easy access
                     * 
                     **/
                    var netViews = Utils.GetEnabledComponentsInChildren<ZNetView>(zloc.m_prefab.Asset);

                    foreach (var view in netViews)
                    {
                        int hash = view.GetPrefabName().GetStableHashCode();

                        var dungeon = view.gameObject.GetComponent<DungeonGenerator>();
                        if (dungeon && !dungeons.ContainsKey(hash))
                        {
                            dungeons[hash] = new KeyValuePair<ZoneSystem.ZoneLocation, DungeonGenerator>(zloc, dungeon);
                        }
                    }



                    /*
                     * 
                     * Dump Location netviews
                     * 
                     **/

                    pkg.Write(netViews.Length);
                    foreach (var view in netViews)
                    {
                        var viewName = Utils.GetPrefabName(view.gameObject);

                        pkg.Write(viewName); //debug
                        pkg.Write(viewName.GetStableHashCode());
                        pkg.Write(view.gameObject.transform.position);
                        pkg.Write(view.gameObject.transform.rotation);
                        pkg.Write(view.transform.localScale); // the clone keeps it, ZNetView syncs it
                    }



                    /*
                     *
                     *  RandomSpawn correspondence with Views
                     *
                     **/

                    WriteRandom(zloc.m_prefab.Asset, netViews, ref pkg);

                    LogInfo("  NetViews...");
                }

                File.WriteAllBytes(Plugin.PkgOutPath + "features.pkg", pkg.GetArray());

                LogInfo("Dumped " + locations.Count + "/" + ZoneSystem.instance.m_locations.Count + " ZoneLocations");
            }



            {

                LogInfo("------------------- Dumping RandomEvents -------------------");

                var events = new List<RandomEvent>();
                foreach (var e in RandEventSystem.instance.m_events)
                {
                    if (e.m_enabled && e.m_random)
                    {
                        events.Add(e);
                        LogInfo("+evt " + e.m_name);
                    }
                    else
                    {
                        LogWarning("-" + e.m_name + " (disabled)");
                    }
                }

                //LogWarning(" - m_eventIntervalMin: " + RandEventSystem.instance.m_eventIntervalMin);
                //LogWarning(" - m_eventChance: " + RandEventSystem.instance.m_eventChance);
                //LogWarning(" - m_randomEventRange: " + RandEventSystem.instance.evm_randomEventRange);

                // Dump dungeons
                ZPackage pkg = new ZPackage();

                pkg.Write(comment);
                pkg.Write(Version.CurrentVersion.ToString()); // write version for reference purposes
                pkg.Write(events.Count);

                foreach (var e in events)
                {
                    pkg.Write(e.m_name);
                    //pkg.Write(e.m_random); // redundant?
                    pkg.Write(e.m_duration);
                    pkg.Write(e.m_nearBaseOnly);
                    pkg.Write(e.m_pauseIfNoPlayerInArea);
                    pkg.Write((int)e.m_biome);

                    pkg.Write(e.m_requiredGlobalKeys.Count);
                    foreach (var key in e.m_requiredGlobalKeys)
                    {
                        pkg.Write(key);
                    }

                    pkg.Write(e.m_notRequiredGlobalKeys.Count);
                    foreach (var key in e.m_notRequiredGlobalKeys)
                    {
                        pkg.Write(key);
                    }

                    // 1.0: area for "player in event", and whether PlayerEvents decides by the players' possibleEvents
                    pkg.Write(e.m_eventRange);
                    pkg.Write(e.m_altRequiredKnownItems.Count > 0 || e.m_altRequiredNotKnownItems.Count > 0 || e.m_altNotRequiredPlayerKeys.Count > 0
                        || e.m_altRequiredPlayerKeysAny.Count > 0 || e.m_altRequiredPlayerKeysAll.Count > 0);
                }

                File.WriteAllBytes(Plugin.PkgOutPath + "randomEvents.pkg", pkg.GetArray());
                LogInfo("Dumped " + events.Count + "/" + RandEventSystem.instance.m_events.Count + " RandomEvents");
            }



            // To disable saving this broken world
            ZoneSystem.instance.m_didZoneTest = true;

            {
                LogInfo("------------------- Dumping Dungeons -------------------");

                // Dump dungeons
                ZPackage pkg = new ZPackage();

                pkg.Write(comment);
                pkg.Write(Version.CurrentVersion.ToString()); // write version for validation purposes
                pkg.Write(dungeons.Count);

                int dungeonIdx = 0;
                foreach (var pair in dungeons)
                {
                    var zoneLocation = pair.Value.Key;

                    var prefab = zoneLocation.m_prefab;
                    prefab.Load();
                    //ZNetView[] enabledComponentsInChildren = Utils.GetEnabledComponentsInChildren<ZNetView>(prefab.Asset);

                    var loc = prefab.Asset.GetComponent<Location>()!;

                    var dungeon = pair.Value.Value!; // we use the instanced generator, not the template one
                    var dungeonName = dungeon.name;

                    if (!ZNetScene.instance.m_namedPrefabs.ContainsKey(dungeonName.GetStableHashCode()))
                    {
                        throw new Exception("instanced dungeon might be used instead of the template one");
                    }

                    LogInfo(": " + dungeonName + " (" + dungeonIdx + ")");

                    // Debug anchor
                    pkg.Write("dungeon".GetStableHashCode());

                    pkg.Write(dungeonName);

                    bool useTransform = loc.m_useCustomInteriorTransform && loc.m_interiorTransform && loc.m_generator;
                    pkg.Write(useTransform);
                    if (useTransform)
                    {
                        pkg.Write(loc.m_interiorTransform.localPosition);
                        pkg.Write(loc.m_interiorTransform.localRotation);
                        pkg.Write(loc.m_generator.transform.localPosition);
                    }

                    pkg.Write((int)dungeon.m_algorithm);
                    pkg.Write(dungeon.m_alternativeFunctionality);
                    pkg.Write(dungeon.m_campRadiusMax);
                    pkg.Write(dungeon.m_campRadiusMin);
                    pkg.Write(dungeon.m_doorChance);

                    pkg.Write(dungeon.m_doorTypes.Count);

                    int doorIdx = 0;
                    foreach (var doorDef in dungeon.m_doorTypes)
                    {
                        var doorPrefab = doorDef.m_prefab;

                        // 1.0: a door can be a plain object with networked children
                        // (DG_Hole), so every door is written as a list of views
                        // the root is a view too for plain doors (GetEnabledComponentsInChildren skips it)
                        var doorViews = doorPrefab.GetComponentsInChildren<ZNetView>()
                            .Where(v => v.transform == doorPrefab.transform || Utils.IsEnabledInheirarcy(v.gameObject, doorPrefab)).ToArray();
                        LogInfo("door: " + doorPrefab.name + " (" + doorIdx++ + ", views=" + doorViews.Length + ")");

                        // Debug anchor (positional anchor)
                        pkg.Write("dungeonDoor".GetStableHashCode());

                        pkg.Write(doorPrefab.name); // auxillary for debug
                        pkg.Write(doorDef.m_connectionType);
                        pkg.Write(doorDef.m_chance);

                        Quaternion doorInv = Quaternion.Inverse(doorPrefab.transform.rotation);
                        pkg.Write(doorViews.Length);
                        foreach (var view in doorViews)
                        {
                            var viewName = view.GetPrefabName();
                            if (!ZNetScene.instance.m_namedPrefabs.ContainsKey(viewName.GetStableHashCode()))
                                throw new Exception("Door view prefab: " + viewName + " not found");

                            pkg.Write(viewName.GetStableHashCode());
                            pkg.Write(doorInv * (view.transform.position - doorPrefab.transform.position)); // local pos
                            pkg.Write(doorInv * view.transform.rotation); // local rot
                        }
                    }

                    //LogWarning("dungeon.m_gridSize: " + dungeon.m_gridSize);

                    pkg.Write(dungeon.m_gridSize); // redundant? (only used for grid/meadows camp)
                    pkg.Write(dungeon.m_maxRooms);
                    pkg.Write(dungeon.m_maxTilt);
                    pkg.Write(dungeon.m_minAltitude);
                    pkg.Write(dungeon.m_minRequiredRooms);
                    pkg.Write(dungeon.m_minRooms);
                    //pkg.Write(dungeon.m_originalPosition); // TODO fix
                    pkg.Write(dungeon.m_perimeterBuffer);
                    pkg.Write(dungeon.m_perimeterSections);

                    if (dungeon.m_requiredRooms == null) LogError("m_requiredRooms null: " + dungeonName);
                    pkg.Write(dungeon.m_requiredRooms?.Count ?? 0);
                    if (dungeon.m_requiredRooms != null)
                    foreach (var roomName in dungeon.m_requiredRooms)
                    {
                        pkg.Write(roomName);
                    }

                    pkg.Write(dungeon.m_spawnChance);
                    pkg.Write((int)dungeon.m_themes);
                    pkg.Write(dungeon.m_tileWidth);
                    //pkg.Write(dungeon.m_useCustomInteriorTransform); // TODO fix
                    // 1.0: rooms must fit the zone bounds, centered at y - m_originalPosition.y
                    pkg.Write(dungeon.m_zoneSize);
                    pkg.Write(dungeon.m_originalPosition);
                    pkg.Write(dungeon.m_addBaseSeedToRandomSpawn);
                    pkg.Write(dungeon.m_useCustomInteriorTransform); // room seeds relative to the generator

                    // Force dungeon to collect its rooms
                    LogInfo("rooms setup: " + dungeonName + " required=" + (dungeon.m_requiredRooms == null ? "null" : dungeon.m_requiredRooms.Count.ToString()));
                    dungeon.SetupAvailableRooms();
                    foreach (var rd in DungeonGenerator.m_availableRooms)
                    {
                        rd.m_prefab.Load();
                        if (rd.RoomInPrefab == null)
                            LogError("room without Room component: " + rd.m_prefab.Name);
                    }

                    pkg.Write(DungeonGenerator.m_availableRooms.Count);
                    foreach (var roomData in DungeonGenerator.m_availableRooms)
                    {
                        roomData.m_prefab.Load();

                        var room = roomData.RoomInPrefab;

                        // BASIC "CHECKSUM" (because im a dumbass) (positional anchor)
                        pkg.Write("dungeonRoom".GetStableHashCode());

                        pkg.Write(Utils.GetPrefabName(room.gameObject));
                        pkg.Write(room.m_divider);
                        //pkg.Write(room.m_enabled);
                        pkg.Write(room.m_endCap);
                        pkg.Write(room.m_endCapPrio);
                        pkg.Write(room.m_entrance);
                        pkg.Write(room.m_faceCenter);
                        pkg.Write(room.m_minPlaceOrder);
                        //pkg.Write(room.m_musicPrefab)
                        pkg.Write(room.m_perimeter);
                        pkg.Write(room.m_size);
                        pkg.Write((int)room.m_theme);
                        pkg.Write(room.m_weight);
                        pkg.Write(room.transform.position);
                        pkg.Write(room.transform.rotation);

                        var connections = room.GetConnections();
                        pkg.Write(connections.Length);
                        foreach (var conn in connections)
                        {
                            pkg.Write(conn.m_type);
                            pkg.Write(conn.m_entrance);
                            pkg.Write(conn.m_allowDoor);
                            pkg.Write(conn.m_doorOnlyIfOtherAlsoAllowsDoor);

                            pkg.Write(conn.transform.localPosition);
                            pkg.Write(conn.transform.localRotation);
                        }

                        Quaternion quat = Quaternion.Inverse(room.transform.rotation);

                        var netViews = Utils.GetEnabledComponentsInChildren<ZNetView>(roomData.m_prefab.Asset);

                        pkg.Write(netViews.Length);
                        foreach (var view in netViews)
                        {
                            // BASIC "CHECKSUM" (positional anchor)
                            pkg.Write("dungeonView".GetStableHashCode());

                            var prefabName = view.GetPrefabName();

                            pkg.Write(prefabName); // auxillary for debug
                            pkg.Write(prefabName.GetStableHashCode());
                            pkg.Write(quat * (view.gameObject.transform.position - room.transform.position)); // local pos
                            pkg.Write(quat * view.gameObject.transform.rotation); // local rot
                            pkg.Write(view.transform.localScale); // the clone keeps it, ZNetView syncs it
                        }

                        // Dump RandomSpawns
                        WriteRandom(roomData.m_prefab.Asset, netViews, ref pkg);
                    }

                }

                File.WriteAllBytes(Plugin.PkgOutPath + "dungeons.pkg", pkg.GetArray());

                LogInfo("Dumped " + dungeons.Count + " dungeons");
            }



            {
                LogInfo("------------------- Dumping Vegetation -------------------");

                ZPackage pkg = new ZPackage();

                List<ZoneSystem.ZoneVegetation> vegetation = new List<ZoneSystem.ZoneVegetation>();
                foreach (var veg in ZoneSystem.instance.m_vegetation)
                {
                    if (veg.m_enable)
                    {
                        if (veg.m_prefab && veg.m_prefab.GetComponent<ZNetView>())
                        {
                            vegetation.Add(veg);
                            LogInfo("+veg " + veg.m_prefab.name);
                        }
                        else
                        {
                            LogError("-veg " + veg.m_prefab.name + " (no prefab or nv)");
                        }
                    }
                    else
                    {
                        LogWarning("-veg " + veg.m_prefab.name + " (disabled)");
                    }
                }

                var layers = Enumerable.Range(0, 31).Select(index => index + ": " + LayerMask.LayerToName(index)).Where(l => !string.IsNullOrEmpty(l)).ToList();

                int BLOCK_MASK = ZoneSystem.instance.m_blockRayMask;
                int SOLID_MASK = ZoneSystem.instance.m_solidRayMask;
                int STATIC_MASK = ZoneSystem.instance.m_staticSolidRayMask;
                int TERRAIN_MASK = ZoneSystem.instance.m_terrainRayMask;

                //LogWarning("Layers: \n - " + string.Join("\n - ", layers));
                //
                //LogWarning("blockRayMask: " + Convert.ToString(BLOCK_MASK, 2));
                //LogWarning("solidRayMask: " + Convert.ToString(SOLID_MASK, 2));
                //LogWarning("staticSolidRayMask: " + Convert.ToString(STATIC_MASK, 2));
                //LogWarning("terrainRayMask: " + Convert.ToString(TERRAIN_MASK, 2));



                // nano-sized tidbits (pickable mushrooms, seeds...)
                // 0.28
                HashSet<string> nanoRadius = new HashSet<string>(
                    new string[] {"Pickable_Thistle",
                            "Pickable_Mushroom",
                            "Pickable_Dandelion", "Pickable_Flint", "Pickable_Stone",
                            "Pickable_SeedTurnip", "Pickable_SeedCarrot",
                            "Pickable_Mushroom_Magecap", "Pickable_Mushroom_JotunPuffs",
                            "CloudberryBush", "Pickable_Branch",
                    }
                );

                // small bushes and small trees
                // 0.70
                HashSet<string> tinyRadius = new HashSet<string>(
                    new string[] {
                            "Beech_small1", "Beech_small2", "Bush01", "YggaShoot_small1", "shrub_2",
                            "Bush01_heath"
                    }
                );

                // 0.95
                HashSet<string> smallRadius = new HashSet<string>(
                    new string[] {
                            "Beech1", "Birch1", "Birch1_aut", "Birch2",  "Birch2_aut",
                            "Pinetree_01", "MineRock_Obsidian"
                    }
                );

                // 1.10
                HashSet<string> mildRadius = new HashSet<string>(
                    new string[] {
                            "FirTree", "Bush02_en", "FirTree_small_dead",
                            "FirTree_small",
                            "RaspberryBush", "BlueberryBush", "SwampTree1",
                            "StatueEvil", "MineRock_Tin"
                    }
                );

                // 2.20
                HashSet<string> bigRadius = new HashSet<string>(
                    new string[] {
                             "Oak1", "SwampTree2",
                             "YggaShoot1", "YggaShoot2", "YggaShoot3", "stubbe"
                    }
                );

                Dictionary<string, float> customRadius = new Dictionary<string, float>
                {
                    { "cliff_mistlands1", 11 },
                    { "cliff_mistlands1_creep", 11 },
                    { "cliff_mistlands2", 11 },
                    { "rock4_forest", 12 },
                    { "rock4_copper", 12 },
                    { "YggdrasilRoot", 9 },
                    { "giant_helmet1", 7 },
                    { "giant_helmet2", 5 }, // only front of helmet1
                    { "rock4_coast", 7 }
                };

                // these are not heavily tested or measured
                /*
                customRadius.Add("rock4_coast", 13);
                customRadius.Add("rock4_heath", 13);
                customRadius.Add("HeathRockPillar", 4);
                customRadius.Add("rock2_heath", 6);
                customRadius.Add("rock3_mountain", 7);
                customRadius.Add("rock1_mountain", 8);
                customRadius.Add("rock2_mountain", 7);
                customRadius.Add("SwampTree2", 1);
                customRadius.Add("SwampTree2_log", 12); // very rectangular
                customRadius.Add("FirTree_oldLog", 2);
                customRadius.Add("stubbe", 1.2f);
                customRadius.Add("StatueEvil", 1.2f);
                customRadius.Add("shrub_2_heath", .7f);
                customRadius.Add("shrub_2", .7f);
                customRadius.Add("Leviathan", 30); // redundant since ocean mostly empty anyways
                customRadius.Add("YggaShoot1", 3);
                customRadius.Add("YggaShoot2", 4);
                customRadius.Add("YggaShoot3", 3);
                customRadius.Add("cliff_mistlands2", 9);
                customRadius.Add("giant_helmet1", 6);
                customRadius.Add("giant_helmet2", 4);
                customRadius.Add("giant_sword1", 2);
                customRadius.Add("giant_sword2", 1);
                customRadius.Add("giant_ribs", 6);

                customRadius.Add("silvervein", 0);*/



                pkg.Write(comment);
                pkg.Write(Version.CurrentVersion.ToString());
                pkg.Write(vegetation.Count);
                foreach (var veg in vegetation)
                {
                    // test scale, confirm all are 1,1,1 (base scale)
                    //if (veg.m_prefab.transform.localScale != new Vector3(1, 1, 1))
                    //LogWarning("Vegetation prefab localScale is not (1, 1, 1): " + veg.m_name + " " + veg.m_prefab.transform.localScale);

                    // full name ("SnowFirTree 2"): vanilla seeds the vegetation with it,
                    // the spawned ZDO gets the stripped GetPrefabName
                    var prefabName = veg.m_prefab.GetComponent<ZNetView>().GetPrefabName();
                    pkg.Write(veg.m_prefab.name);
                    pkg.Write((int)veg.m_biome);
                    pkg.Write((int)veg.m_biomeArea);

                    // TODO do I have to instantiate?
                    GameObject obj = UnityEngine.Object.Instantiate(veg.m_prefab);

                    var collider = obj.GetComponent<Collider>() ?? obj.GetComponentInChildren<Collider>();

                    if (collider)
                    {
                        // test whether layer has
                        //  "Default",
                        //  "static_solid",
                        //  "Default_small",
                        //  "piece"
                        var extents3 = collider.bounds.extents;
                        float maxr = Math.Max(extents3.x, extents3.z);
                        //var extents2 = new Vector2(extents3.x, extents3.z);
                        var extents2 = new Vector2(maxr, maxr);

                        float radius = extents2.magnitude / 2.0f; // = extents2.magnitude;

                        // TODO simpler way to handle collision would be to 
                        //  create an n-gon, and trim angles that are 
                        //  close in value to connected segment angles
                        //  - This would allow for rather simple collision, while
                        //      improving detection accuracy by A LOT
                        //  - the alternative is to create n-elipses
                        //      2d-space is *somewhat* trivial, while 3d is not trivial
                        //      2d binary shaping allows for a 'ray marching' approach

                        if ((collider.gameObject.layer & BLOCK_MASK) == 0)
                            radius = 0;

                        float temp = 0;
                        if (customRadius.TryGetValue(prefabName, out temp))
                            radius = temp;
                        else if (nanoRadius.Contains(prefabName))
                            radius = 0.28f;
                        else if (tinyRadius.Contains(prefabName))
                            radius = 0.70f;
                        else if (smallRadius.Contains(prefabName))
                            radius = 0.95f;
                        else if (mildRadius.Contains(prefabName))
                            radius = 1.10f;
                        else if (bigRadius.Contains(prefabName))
                            radius = 2.20f;
                        else
                        {
                            LogWarning("Vegetation missing defined radius: " + prefabName + " (is this a new version?)");
                        }

                        LogWarning("Dumping vegetation " + prefabName + ", radius: " + radius);

                        pkg.Write(radius);

                        /*
                        if ((collider.gameObject.layer & BLOCK_MASK) > 0)
                        {

                            //radiusMap["mudpile_beacon"] = 0;
                            //radiusMap["MineRock_Tin"]

                            // "ice1" is part of deep north (NYI)

                            // .2 or less


                            pkg.Write((float)extents2.magnitude); // hopefully most vegetation is circular
                            LogWarning("Dumping vegetation block layer " + veg.m_prefab.name + ", radius: " + extents2.magnitude);
                        }
                        else
                        {
                            LogWarning("Vegetation has no block mask " + veg.m_prefab.name + "(" + Convert.ToString(collider.gameObject.layer, 2) + "), radius: " + extents2.magnitude);
                            //var extents3 = collider.bounds.extents;
                            //var extents2 = new Vector2(extents3.x, extents3.z);
                            //pkg.Write((float)extents2.magnitude); // hopefully most vegetation is circular
                            pkg.Write(0.0f);
                        }*/
                    }
                    else
                    {
                        LogWarning("Vegetation has no collider " + veg.m_prefab.name);
                        pkg.Write(0.0f);
                    }

                    UnityEngine.Object.Destroy(obj);

                    /*else
                    {
                        LogWarning("Vegetation missing collider: " + veg.m_prefab.name);

                        var filter = veg.m_prefab.GetComponent<MeshFilter>();
                        if (!filter) filter = veg.m_prefab.GetComponentInChildren<MeshFilter>();
                        if (filter)
                        {
                            var mesh = filter.mesh;
                            var extents3 = mesh.bounds.extents;
                            var extents2 = new Vector2(extents3.x, extents3.z);
                            pkg.Write((float)extents2.magnitude); // hopefully most vegetation is circular
                        } else
                        {
                            LogWarning("Vegetation missing meshfilter");
                            var renderer = veg.m_prefab.GetComponent<Renderer>();
                            if (!renderer) renderer = veg.m_prefab.GetComponentInChildren<Renderer>();
                            if (renderer)
                            {
                                var extents3 = renderer.bounds.extents;
                                var extents2 = new Vector2(extents3.x, extents3.z);
                                pkg.Write((float)extents2.magnitude); // hopefully most vegetation is circular
                            } else
                            {
                                LogError("Vegetation missing renderer: " + veg.m_prefab.name);
                                pkg.Write(0.15f);

                                //LogWarning("Vegetation missing renderer: " + veg.m_prefab.name);

                                //var lod = veg.m_prefab.GetComponent<LODGroup>();
                                //if (lod)
                                //{
                                //    
                                //}
                                //else
                                //{
                                //    LogError("Vegetation complete fail: ")
                                //    pkg.Write(0.15f);
                                //}

                            }
                        }
                    }*/

                    pkg.Write(veg.m_min);
                    pkg.Write(veg.m_max);
                    pkg.Write(veg.m_minTilt);
                    pkg.Write(veg.m_maxTilt);
                    pkg.Write(veg.m_groupRadius);
                    pkg.Write(veg.m_forcePlacement);
                    pkg.Write(veg.m_groupSizeMin);
                    pkg.Write(veg.m_groupSizeMax);
                    pkg.Write(veg.m_scaleMin);
                    pkg.Write(veg.m_scaleMax);
                    pkg.Write(veg.m_randTilt);
                    pkg.Write(veg.m_blockCheck);
                    pkg.Write(veg.m_minAltitude);
                    pkg.Write(veg.m_maxAltitude);
                    pkg.Write(veg.m_minOceanDepth);
                    pkg.Write(veg.m_maxOceanDepth);
                    pkg.Write(veg.m_terrainDeltaRadius);
                    pkg.Write(veg.m_minTerrainDelta);
                    pkg.Write(veg.m_maxTerrainDelta);
                    pkg.Write(veg.m_inForest);
                    pkg.Write(veg.m_forestTresholdMin);
                    pkg.Write(veg.m_forestTresholdMax);
                    pkg.Write(veg.m_snapToWater);
                    pkg.Write(veg.m_snapToStaticSolid);
                    pkg.Write(veg.m_groundOffset);
                    pkg.Write(veg.m_chanceToUseGroundTilt);
                    pkg.Write(veg.m_minVegetation);
                    pkg.Write(veg.m_maxVegetation);
                    pkg.Write(veg.m_surroundCheckVegetation);       // TODO
                    pkg.Write(veg.m_surroundCheckDistance);         // TODO
                    pkg.Write(veg.m_surroundCheckLayers);           // TODO
                    pkg.Write(veg.m_surroundBetterThanAverage);     // TODO
                    // 1.0
                    pkg.Write(veg.m_minDistanceFromCenter);
                    pkg.Write(veg.m_maxDistanceFromCenter);
                    pkg.Write(veg.m_name);
                    pkg.Write(veg.AltBiomeParent ?? "");
                    var colliders = Colliders.Find(veg.m_prefab, null, null);
                    pkg.Write(colliders.Count);
                    foreach (var c in colliders)
                        Colliders.Write(c.Key, veg.m_prefab.transform, pkg);
                    // PlaceVegetation re-enables every collider after SetLocalScale, the disabled ones too
                    var disabled = Colliders.Find(veg.m_prefab, null, null, true);
                    pkg.Write(disabled.Count);
                    foreach (var c in disabled)
                        Colliders.Write(c.Key, veg.m_prefab.transform, pkg);

                    // offset by final transforms:
                    //var obj = UnityEngine.GameObject.Instantiate<GameObject>(veg.m_prefab)

                    //pkg.Write(v)
                }

                File.WriteAllBytes(Plugin.PkgOutPath + "vegetation.pkg", pkg.GetArray());
                Colliders.WriteMeshes(Plugin.PkgOutPath + "colliders.pkg");

                LogWarning("Dumped " + vegetation.Count + "/" + ZoneSystem.instance.m_vegetation.Count + " ZoneVegetation");
            }

            // now dump starting keys?




        }



        //public static bool loaded = false;

        public static void RecurseObjectPrint(GameObject gameObject, uint depth)
        {
            String currentDepth = new String(' ', (int)depth * 2) + " - ";
            String nestedDepth = new String(' ', (int)(depth + 1) * 2) + " - ";

            LogWarning(currentDepth + "name: " + gameObject.name);
            var monos = gameObject.GetComponents(typeof(MonoBehaviour));
            if (monos.Length > 0)
            {
                LogWarning(currentDepth + "Monos: ");
                foreach (var mono in monos)
                {
                    LogWarning(nestedDepth + mono.GetType().Name);
                }
            }

            int childCount = gameObject.transform.childCount;
            if (childCount > 0)
            {
                LogWarning(currentDepth + "children: ");
                for (int i = 0; i < childCount; i++)
                {
                    var child = gameObject.transform.GetChild(i).gameObject;
                    RecurseObjectPrint(child, depth + 1);
                }
            }

        }

        /*
        [HarmonyPatch(typeof(ZoneSystem))]
        class ZoneSystemPatch
        {
            [HarmonyPrefix]
            [HarmonyPatch(nameof(ZoneSystem.Update))]
            static void UpdatePrefix(ref ZoneSystem __instance)
            {
                if (loaded || !DungeonDB.instance)
                    return;

                loaded = true;

                DumpDocs(ref __instance);

                // we all love a lack of error messages dont we?
                //try
                //{
                DumpPackages(ref __instance);
                //} catch (Exception e)
                //{
                //    LogError("Error while dumping: ");
                //    LogError(e.StackTrace.ToString());
                //    LogWarning("L: " + new StackTrace(e, true).GetFrame(0).GetFileLineNumber());
                //}
            }
        }
        */

    }
}
