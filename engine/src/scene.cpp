// Scene management: scene objects, selection, and id allocation.

#include "core/engine.h"

#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "engine_internal.h"

namespace CoreEngine {

std::vector<SceneObject>& GetSceneObjects() { return s_sceneObjects; }

// Local transform of a scene object, in the exact composition order
// used by the render passes: translate * rotX * rotY * rotZ * scale.
static glm::mat4 ObjectLocalMatrix(const SceneObject& o) {
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(o.position.x, o.position.y, o.position.z));
    m = glm::rotate(m, (float)o.rotation.x, glm::vec3(1, 0, 0));
    m = glm::rotate(m, (float)o.rotation.y, glm::vec3(0, 1, 0));
    m = glm::rotate(m, (float)o.rotation.z, glm::vec3(0, 0, 1));
    m = glm::scale(m, glm::vec3(o.scale.x, o.scale.y, o.scale.z));
    return m;
}

const SceneObject* FindSceneObject(uint32_t id) {
    for (auto& o : s_sceneObjects) {
        if (o.id == id) return &o;
    }
    return nullptr;
}

glm::mat4 ComputeObjectWorldMatrix(uint32_t id) {
    // Walk the parent chain (with a cap so a malformed cycle can never
    // hang the frame) and prepend each ancestor's local matrix.
    glm::mat4 world = glm::mat4(1.0f);
    uint32_t cur = id;
    for (int guard = 0; guard < 64; ++guard) {
        const SceneObject* obj = FindSceneObject(cur);
        if (!obj) break;  // top level or dangling parent
        world = ObjectLocalMatrix(*obj) * world;
        if (obj->parentId == 0) break;
        cur = obj->parentId;
    }
    return world;
}

bool DecomposeTRS(const glm::mat4& m, Vector3& p, Vector3& r, Vector3& s, float& err) {
    // Exact inverse of the composition order used by ObjectLocalMatrix()
    // and the render passes: M = T * rotX * rotY * rotZ * S.
    p = Vector3(m[3].x, m[3].y, m[3].z);
    glm::vec3 c0(m[0].x, m[0].y, m[0].z);
    glm::vec3 c1(m[1].x, m[1].y, m[1].z);
    glm::vec3 c2(m[2].x, m[2].y, m[2].z);
    float sx = glm::length(c0), sy = glm::length(c1), sz = glm::length(c2);
    if (sx < 1e-9f || sy < 1e-9f || sz < 1e-9f) { err = 1e9f; return false; }

    glm::mat3 lin(c0, c1, c2);
    if (glm::determinant(lin) < 0.0f) sz = -sz;  // reflection → −z scale
    s = Vector3(sx, sy, sz);
    glm::mat3 R(c0 / sx, c1 / sy, c2 / sz);      // pure rotation now

    const float PI = 3.14159265358979323846f;
    auto rotXYZ = [](float ax, float ay, float az) -> glm::mat3 {
        glm::mat4 t = glm::mat4(1.0f);
        t = glm::rotate(t, ax, glm::vec3(1, 0, 0));
        t = glm::rotate(t, ay, glm::vec3(0, 1, 0));
        t = glm::rotate(t, az, glm::vec3(0, 0, 1));
        return glm::mat3(t);
    };
    auto maxDiff = [](const glm::mat3& a, const glm::mat3& b) {
        float e = 0.0f;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                e = fmaxf(e, fabsf(a[i][j] - b[i][j]));
        return e;
    };

    // R = Rx*Ry*Rz, math indices [row][col]:
    //   R02=sin(ry)  R12=−sin(rx)cos(ry)  R22=cos(rx)cos(ry)
    //   R01=−cos(ry)sin(rz)  R00=cos(ry)cos(rz)
    // NOTE: glm's R[i][j] is COLUMN i, component j — the transpose of math
    // indices — so R02 reads as R[2][0], R12 as R[2][1], R01 as R[1][0].
    float syR = glm::clamp(R[2][0], -1.0f, 1.0f);   // R02 = sin(ry)
    if (fabsf(syR) < 1.0f) {
        float rx1 = atan2f(-R[2][1], R[2][2]);      // atan2(sin rx·cy, cos rx·cy)
        float ry1 = asinf(syR);
        float rz1 = atan2f(-R[1][0], R[0][0]);      // atan2(cos ry·sin rz, cos ry·cos rz)
        if (maxDiff(rotXYZ(rx1, ry1, rz1), R) < 1e-4f) {
            r = Vector3(rx1, ry1, rz1);            // cos(ry) > 0 branch
        } else {
            // cos(ry) < 0 branch: asin() can't see it, but the
            // (rx+π, π−ry, rz+π) triple produces the same rotation.
            r = Vector3(rx1 + PI, PI - ry1, rz1 + PI);
        }
        // The atan2 ratios cancel the cos(ry) factor, so this stays accurate
        // arbitrarily close to ±90° (float32 elements keep ~1e-7 relative
        // precision) and only hits 0/0 when cos(ry) is EXACTLY zero — the
        // special case below.
    } else {
        // Exact gimbal lock: sin(ry)=±1 ⇒ cos(ry)=0, so (math indices)
        //   ry=+90°: R10=sin(rx+rz)  R11=cos(rx+rz)
        //   ry=−90°: R10=sin(rz−rx)  R11=cos(rz−rx)
        // Only the angle combination is determined, so one angle is fixed
        // to zero; the reconstructed rotation is still exact.
        float th = atan2f(R[0][1], R[1][1]);        // R10, R11 (math)
        if (syR > 0)
            r = Vector3(th, PI * 0.5f, 0.0f);      // (rx+rz, 90°, 0)
        else
            r = Vector3(-th, -PI * 0.5f, 0.0f);    // (−(rz−rx), −90°, 0)
    }

    // Rebuild from the TRS and measure the reconstruction error as a
    // RELATIVE element difference (scale-independent, so a cm- or m-scale
    // model with pure TRS still reports ~0; shear shows up as O(1)).
    glm::mat4 recon = glm::mat4(1.0f);
    recon = glm::translate(recon, glm::vec3(p.x, p.y, p.z));
    recon = glm::rotate(recon, r.x, glm::vec3(1, 0, 0));
    recon = glm::rotate(recon, r.y, glm::vec3(0, 1, 0));
    recon = glm::rotate(recon, r.z, glm::vec3(0, 0, 1));
    recon = glm::scale(recon, glm::vec3(s.x, s.y, s.z));
    err = 0.0f;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float a = recon[i][j], b = m[i][j];
            float sc = fmaxf(1.0f, fmaxf(fabsf(a), fabsf(b)));
            err = fmaxf(err, fabsf(a - b) / sc);
        }
    return true;
}

uint32_t AddToScene(const std::string& name, MeshPtr mesh, Material mat) {
    SceneObject obj;
    obj.id = s_nextSceneObjectId++;
    obj.name = name;
    obj.mesh = std::move(mesh);
    obj.material = std::move(mat);
    s_sceneObjects.push_back(std::move(obj));
    return s_sceneObjects.back().id;
}

SceneObject* GetSceneObject(uint32_t id) {
    for (auto& o : s_sceneObjects)
        if (o.id == id) return &o;
    return nullptr;
}

void ClearScene() {
    s_selectedObjectId = 0;
    s_cameraObjectId = 0;  // Reset so camera gets recreated on next CreateCameraObject()
    s_sceneObjects.clear();
}

void RemoveFromScene(uint32_t id) {
    // Collect the target plus every descendant, then remove them all —
    // deleting a model root must take its parts with it.
    std::vector<uint32_t> doomed;
    std::vector<uint32_t> queue;
    queue.push_back(id);
    while (!queue.empty()) {
        uint32_t cur = queue.back();
        queue.pop_back();
        const SceneObject* obj = FindSceneObject(cur);
        if (!obj) continue;
        doomed.push_back(cur);
        for (auto& o : s_sceneObjects) {
            if (o.parentId == cur) queue.push_back(o.id);
        }
    }
    if (doomed.empty()) return;
    for (uint32_t did : doomed) {
        for (auto it = s_sceneObjects.begin(); it != s_sceneObjects.end(); ++it) {
            if (it->id == did) {
                s_sceneObjects.erase(it);
                break;
            }
        }
    }
    for (uint32_t did : doomed) {
        if (s_selectedObjectId == did) { s_selectedObjectId = 0; break; }
    }
}

void SelectObject(uint32_t id) {
    s_selectedObjectId = id;
}

SceneObject* GetSelectedObject() {
    for (auto& obj : s_sceneObjects) {
        if (obj.id == s_selectedObjectId) {
            return &obj;
        }
    }
    return nullptr;
}

uint32_t GetSelectedObjectId() { return s_selectedObjectId; }

uint32_t GetNextSceneObjectId() { return s_nextSceneObjectId; }

} // namespace CoreEngine
