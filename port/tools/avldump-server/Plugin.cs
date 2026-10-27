using BepInEx;
using UnityEngine;

namespace AvlDumpServer
{
    // Runs on the headless dedicated server: waits for the world to load,
    // dumps Avledet .pkg files, then quits.
    [BepInPlugin("avledet.dumpserver", "AvlDumpServer", "0.1.0")]
    internal class Plugin : BaseUnityPlugin
    {
        public const string OutPath = "dumped/";
        public const string PkgOutPath = OutPath + "pkg/";
        public const string DocOutPath = OutPath + "doc/";

        bool m_done;

        void Update()
        {
            if (m_done || !Ready())
                return;
            m_done = true;

            // only the math tests, the packages stay as they are
            if (System.Environment.GetEnvironmentVariable("AVL_DUMP_TESTS") != null)
            {
                try
                {
                    JsonDump.RunTests(OutPath + "json/");
                }
                finally
                {
                    Application.Quit();
                }
                return;
            }

            Logger.LogInfo("World loaded, dumping");
            AwakeSteps.Patch();
            try
            {
                JsonDump.Run(OutPath + "json/");
                Logger.LogInfo("Json dump done");
            }
            catch (System.Exception e)
            {
                Logger.LogError("Json dump failed: " + e);
            }
            try
            {
                AvlDumper.DumpDocs();
                AvlDumper.DumpPackages();
                Logger.LogInfo("Dump done");
            }
            catch (System.Exception e)
            {
                Logger.LogError("Dump failed: " + e);
            }
            Application.Quit();
        }

        static bool Ready()
        {
            return ZNet.instance && ZNetScene.instance && DungeonDB.instance
                && ZoneSystem.instance && ZoneSystem.instance.LocationsGenerated;
        }
    }
}
