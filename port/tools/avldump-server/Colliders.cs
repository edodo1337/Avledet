using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;

namespace AvlDumpServer
{
    // Colliders that vegetation raycasts hit (ZoneSystem.IsBlocked and GetStaticSolidHeight masks).
    // Meshes are shared between templates and written once to colliders.pkg.
    internal static class Colliders
    {
        public const byte Box = 0, Sphere = 1, Capsule = 2, Mesh = 3, Convex = 4;
        public const byte LayerBlock = 1, LayerStaticSolid = 2;

        static readonly Dictionary<UnityEngine.Mesh, int> s_meshIndex = new Dictionary<UnityEngine.Mesh, int>();
        static readonly List<UnityEngine.Mesh> s_meshes = new List<UnityEngine.Mesh>();
        static readonly HashSet<string> s_unreadable = new HashSet<string>();

        static byte Layers(Collider c)
        {
            int bit = 1 << c.gameObject.layer;
            byte layers = 0;
            if ((bit & LayerMask.GetMask("Default", "static_solid", "Default_small", "piece")) != 0) layers |= LayerBlock;
            if ((bit & LayerMask.GetMask("static_solid")) != 0) layers |= LayerStaticSolid;
            return layers;
        }

        // Colliders live after the location/room instantiate: active up to the root (an m_OffObject
        // gets turned on when its RandomSpawn fails). Value is the innermost view or -1 for the visual.
        // disabled: the ones switched off in the prefab instead (vegetation turns them on after a rescale)
        public static List<KeyValuePair<Collider, int>> Find(GameObject root, ZNetView[] netViews, RandomSpawn[] spawns, bool disabled = false)
        {
            var list = new List<KeyValuePair<Collider, int>>();
            foreach (var c in root.GetComponentsInChildren<Collider>(true))
            {
                if (c.enabled == disabled || Layers(c) == 0 || !(c is BoxCollider || c is SphereCollider || c is CapsuleCollider || c is MeshCollider))
                    continue;
                int owner = -1;
                bool live = true;
                for (var t = c.transform; t && t != root.transform; t = t.parent)
                {
                    if (owner == -1 && netViews != null)
                        owner = Array.IndexOf(netViews, t.GetComponent<ZNetView>());
                    if (!t.gameObject.activeSelf && (spawns == null || !spawns.Any(r => r.m_OffObject == t.gameObject)))
                        live = false;
                }
                var mc = c as MeshCollider;
                if (mc && (!mc.sharedMesh || !mc.sharedMesh.isReadable))
                {
                    if (live)
                        s_unreadable.Add(root.name + "/" + c.name);
                    continue;
                }
                if (live)
                    list.Add(new KeyValuePair<Collider, int>(c, owner));
            }
            return list;
        }

        // Shape with its pose local to root, in root units (a vegetation root gets a new uniform scale)
        public static void Write(Collider c, Transform root, ZPackage pkg)
        {
            var inv = Quaternion.Inverse(root.rotation);
            float s = root.localScale.x;
            var t = c.transform;
            var mc = c as MeshCollider;
            byte type = c is BoxCollider ? Box : c is SphereCollider ? Sphere : c is CapsuleCollider ? Capsule : mc.convex ? Convex : Mesh;
            pkg.Write(type);
            pkg.Write(Layers(c));
            pkg.Write(inv * (t.position - root.position) / s);
            pkg.Write(inv * t.rotation);
            pkg.Write(t.lossyScale / s);
            switch (c)
            {
                case BoxCollider b:
                    pkg.Write(b.center);
                    pkg.Write(b.size);
                    break;
                case SphereCollider sp:
                    pkg.Write(sp.center);
                    pkg.Write(sp.radius);
                    break;
                case CapsuleCollider cp:
                    pkg.Write(cp.center);
                    pkg.Write(cp.radius);
                    pkg.Write(cp.height);
                    pkg.Write(cp.direction);
                    break;
                default:
                    if (!s_meshIndex.TryGetValue(mc.sharedMesh, out var index))
                    {
                        index = s_meshes.Count;
                        s_meshIndex[mc.sharedMesh] = index;
                        s_meshes.Add(mc.sharedMesh);
                    }
                    pkg.Write(index);
                    break;
            }
        }

        public static void WriteMeshes(string path)
        {
            var pkg = new ZPackage();
            pkg.Write(Version.CurrentVersion.ToString());
            pkg.Write(s_meshes.Count);
            foreach (var mesh in s_meshes)
            {
                var verts = mesh.vertices;
                pkg.Write(verts.Length);
                foreach (var v in verts)
                    pkg.Write(v);
                var tris = mesh.triangles;
                pkg.Write(tris.Length);
                foreach (var i in tris)
                    pkg.Write(i);
            }
            File.WriteAllBytes(path, pkg.GetArray());
            Debug.Log("[AvlDumpServer] colliders.pkg: " + s_meshes.Count + " meshes, unreadable skipped: " + string.Join(", ", s_unreadable.ToArray()));
        }
    }
}
