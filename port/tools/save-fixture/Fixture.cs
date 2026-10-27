using System.Linq;
using BepInEx;
using UnityEngine;

namespace SaveFixture
{
    // Vanilla 1.0 server: adds ZDOs that cover the save format (linked portals, spawners, a sync
    // transform, every value type, varint counts, session keys, small position and rotation),
    // saves the world and quits. The save is the fixture for tools/avl-save-test.sh.
    [BepInPlugin("avledet.savefixture", "SaveFixture", "0.1.0")]
    internal class Fixture : BaseUnityPlugin
    {
        bool m_done;

        void Update()
        {
            if (m_done || !ZNet.instance || !ZNetScene.instance || !ZoneSystem.instance || !ZoneSystem.instance.LocationsGenerated)
                return;
            m_done = true;

            var man = ZDOMan.instance;
            ZDO Make(string prefab, Vector3 pos)
            {
                var zdo = man.CreateNewZDO(pos, prefab.GetStableHashCode());
                zdo.Persistent = true;
                zdo.SetPrefab(prefab.GetStableHashCode());
                return zdo;
            }

            var a = Make("portal_wood", new Vector3(10.5f, 30.25f, -20.75f));
            var b = Make("portal_wood", new Vector3(-3000.5f, 41f, 2500.125f));
            a.Set("tag", "home");
            b.Set("tag", "home");
            a.SetConnection(ZDOExtraData.ConnectionType.Portal, b.m_uid);
            b.SetConnection(ZDOExtraData.ConnectionType.Portal, a.m_uid);

            // not linked yet: the server pairs free portals with the same tag (Game.ConnectPortals)
            Make("portal_stone", new Vector3(50f, 30f, 50f)).Set("tag", "far");
            Make("portal_stone", new Vector3(-50f, 30f, -50f)).Set("tag", "far");

            var spawner = Make("Spawner_Greydwarf", new Vector3(100f, 35f, 100f));
            var creature = Make("Greydwarf", new Vector3(103f, 35.5f, 98f));
            spawner.SetConnection(ZDOExtraData.ConnectionType.Spawned, creature.m_uid);
            var done = Make("Spawner_Greydwarf", new Vector3(-100f, 35f, 100f));
            done.SetConnection(ZDOExtraData.ConnectionType.Spawned, ZDOID.None);

            var chest = Make("piece_chest_wood", new Vector3(5f, 32f, 5f));
            chest.SetRotation(Quaternion.Euler(10f, 359.75f, 0.25f));
            for (int i = 0; i < 200; i++)
                chest.Set("k" + i, i * 7);
            chest.Set("f", 1.5f);
            chest.Set("v", new Vector3(1, 2, 3));
            chest.Set("q", new Quaternion(0.1f, 0.2f, 0.3f, 0.9f));
            chest.Set("l", long.MaxValue - 5);
            chest.Set("s", "строка");
            chest.Set("items", Enumerable.Range(0, 300).Select(x => (byte)x).ToArray());
            chest.Set("vel", new Vector3(9, 9, 9));// session only, never saved
            var rider = Make("Greydwarf", new Vector3(5f, 33f, 5f));
            rider.SetConnection(ZDOExtraData.ConnectionType.SyncTransform, chest.m_uid);

            var small = Make("_ZoneCtrl", new Vector3(64f, 0f, -128f));
            small.SetRotation(Quaternion.Euler(0f, 90f, 0f));
            Make("Rock_4", new Vector3(30000f, 0f, 12f));// clamped to 20000 on disk

            ZNet.instance.Save(true);
            Logger.LogInfo("fixture saved");
            Application.Quit();
        }
    }
}
