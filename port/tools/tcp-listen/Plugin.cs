using System;
using System.Net;
using System.Net.Sockets;
using BepInEx;
using HarmonyLib;
using UnityEngine;

namespace TcpListen
{
    // Test only: the vanilla server also accepts plain TCP peers (ZSocket2, the same framing Avledet uses with
    // experimental.server-tcp), so a scripted bot can join without Steam. Steam peers are untouched.
    // Env: TCP_PORT (default 2459), BOT_SEED (seed of a new world), BOT_SEEDED_ROTATION=1 (location rotation
    // seeded per zone as in Avledet; vanilla takes it from the unseeded UnityEngine.Random).
    [BepInPlugin("avledet.tcplisten", "TcpListen", "0.1.0")]
    internal class Plugin : BaseUnityPlugin
    {
        static TcpListener s_listener;
        static OnlineBackendType? s_saved;

        void Awake()
        {
            int port = int.Parse(Environment.GetEnvironmentVariable("TCP_PORT") ?? "2459");
            s_listener = new TcpListener(IPAddress.Any, port);
            s_listener.Start();
            new Harmony("avledet.tcplisten").PatchAll(typeof(Plugin));
            Debug.Log("[TcpListen] listening on " + port);
        }

        void Update()
        {
            if (ZNet.instance == null || !ZNet.instance.IsServer() || !s_listener.Pending())
                return;
            TcpClient client = s_listener.AcceptTcpClient();
            client.NoDelay = true;
            Debug.Log("[TcpListen] peer " + client.Client.RemoteEndPoint);
            ZNet.instance.OnNewConnection(new ZNetPeer(new ZSocket2(client), false));
        }

        [HarmonyPatch(typeof(World), nameof(World.GenerateSeed)), HarmonyPrefix]
        static bool GenerateSeed(ref string __result)
        {
            string seed = Environment.GetEnvironmentVariable("BOT_SEED");
            if (string.IsNullOrEmpty(seed))
                return true;
            __result = seed;
            return false;
        }

        [HarmonyPatch(typeof(ZoneSystem), "PlaceLocations"), HarmonyPrefix]
        static void SeedLocationRotation(Vector2s zoneID)
        {
            if (Environment.GetEnvironmentVariable("BOT_SEEDED_ROTATION") == "1")
                UnityEngine.Random.InitState(WorldGenerator.instance.GetSeed() + zoneID.x * 4271 + zoneID.y * 9187);
        }

        // a TCP peer has no Steam ticket: check it like a custom socket backend
        [HarmonyPatch(typeof(ZNet), "RPC_PeerInfo"), HarmonyPrefix]
        static void PeerInfoPre(ZRpc rpc)
        {
            ZNetPeer peer = ZNet.instance.GetPeer(rpc);
            if (peer != null && peer.m_socket is ZSocket2)
            {
                s_saved = ZNet.m_onlineBackend;
                ZNet.m_onlineBackend = OnlineBackendType.CustomSocket;
            }
        }

        [HarmonyPatch(typeof(ZNet), "RPC_PeerInfo"), HarmonyFinalizer]
        static void PeerInfoPost()
        {
            if (s_saved.HasValue)
                ZNet.m_onlineBackend = s_saved.Value;
            s_saved = null;
        }
    }
}
