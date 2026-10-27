#pragma once

#include "Quaternion.h"
#include "Vector.h"

// Unity native Transform math (UnityPlayer.so, Transform::SetRotation/GetRotation/GetPosition/SetPosition),
// ported instruction for instruction with SSE. The same instructions give the same bits as the game on
// the same CPU; rcpps (world to local) differs between CPU models, so does the game.
namespace avledet::unity {

    // what a rotation setter stores (per lane SIMD normalize, identity below 1e-30)
    Quaternion Normalize(Quaternion q);

    // world rotation of a child: parent world rotation times the child's local one
    Quaternion ToWorld(Quaternion parent, Vector3f parentScale, Quaternion local);
    // world position of a child: parent position, rotation and scale applied to its local position
    Vector3f ToWorld(Vector3f parentPos, Quaternion parentRot, Vector3f parentScale, Vector3f local);

    // local pose stored when a child gets a world pose under its parent (Instantiate with a parent)
    Quaternion ToLocal(Quaternion parent, Vector3f parentScale, Quaternion world);
    Vector3f ToLocal(Vector3f parentPos, Quaternion parentRot, Vector3f parentScale, Vector3f world);

}// namespace avledet::unity
