// ============================================================================
// test_model_hier.cpp
//
// Headless validation of the FBX parent-node import feature (no GL window
// needed — only scene state, math, and the assimp import run):
//
//   G1. Import: LoadFBX builds a rest-pose node tree and attaches EVERY
//       sub-mesh to a node (nodeIndex >= 0).
//   G2. Node worlds: node.world == parentWorld * node.local for the whole
//       tree (the loader's single forward pass is self-consistent).
//   G3. TRS round-trip: each part's local transform — its node's world
//       transform relative to the file's root node, exactly what
//       Editor::LoadFBXAtPath computes — survives the engine's TRS
//       decomposition (CoreEngine::DecomposeTRS) without meaningful error.
//   G4. Hierarchy semantics (the actual scene contract the editor relies
//       on): a model root (transform-only group, parentId = 0) with the
//       imported parts as children:
//         - root at identity  -> every part's world matrix == its imported
//           local matrix (parts appear where the exporter placed them)
//         - transform root    -> every part's world == rootLocal * partLocal
//           (moving/rotating/scaling the root moves all parts with it)
//         - remove root       -> cascades: all parts are deleted too
//
// Usage:  test_model_hier.exe <model.fbx>
//         (default: tools/elf.fbx — a mesh model; NOTE tools/Idle.fbx is
//          animation-only and cannot exercise the G1 model gates)
// ============================================================================

#include "core/engine.h"
#include "core/asset_loader.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

using namespace CoreEngine;

namespace {

    int g_pass = 0, g_fail = 0;

    void Gate(bool ok, const char* what, const char* detail = "") {
        if (ok) {
            ++g_pass;
            printf("  PASS  %s\n", what);
        } else {
            ++g_fail;
            printf("  FAIL  %s%s%s\n", what, detail[0] ? " — " : "", detail);
        }
    }

    // Max relative element difference between two matrices
    // (relative to max(1, |element|) so large cm-scale models stay sane).
    float MatDiff(const glm::mat4& a, const glm::mat4& b) {
        float e = 0.0f;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float d = fabsf(a[i][j] - b[i][j]);
                float scale = fmaxf(1.0f, fmaxf(fabsf(a[i][j]), fabsf(b[i][j])));
                e = fmaxf(e, d / scale);
            }
        return e;
    }

    // Rebuild the engine's local matrix from a scene object's stored TRS —
    // the exact composition ObjectLocalMatrix() uses (T * rotX * rotY * rotZ * S).
    glm::mat4 TrsOf(const Vector3& p, const Vector3& r, const Vector3& s) {
        glm::mat4 m = glm::mat4(1.0f);
        m = glm::translate(m, glm::vec3(p.x, p.y, p.z));
        m = glm::rotate(m, r.x, glm::vec3(1, 0, 0));
        m = glm::rotate(m, r.y, glm::vec3(0, 1, 0));
        m = glm::rotate(m, r.z, glm::vec3(0, 0, 1));
        m = glm::scale(m, glm::vec3(s.x, s.y, s.z));
        return m;
    }

    const char* FailMsg = "";

} // namespace

int main(int argc, char** argv) {
    const std::string path = (argc > 1) ? argv[1] : "tools/elf.fbx";

    printf("== test_model_hier: FBX parent-node import (%s)\n\n", path.c_str());

    // ── G1: import builds the node tree and attaches every sub-mesh ────
    printf("G1: import + node attachment\n");
    FBXModel model = AssetLoader::LoadFBX(path, false);
    char detail[256];
    Gate(model.success, "FBX loaded");
    if (!model.success) return 1;
    Gate(!model.nodes.empty(), "rest-pose node tree built",
         (snprintf(detail, sizeof(detail), "nodes=%zu", model.nodes.size()), detail));
    Gate(model.rawMeshes.size() > 0, "has sub-meshes");

    int attached = 0;
    for (const auto& raw : model.rawMeshes)
        if (raw.nodeIndex >= 0 && (size_t)raw.nodeIndex < model.nodes.size()) ++attached;
    Gate(attached == (int)model.rawMeshes.size(), "every sub-mesh attached to a node",
         (snprintf(detail, sizeof(detail), "attached %d/%zu",
                   attached, model.rawMeshes.size()), detail));
    if (attached != (int)model.rawMeshes.size()) {
        printf("  (nodes without geometry are fine; sub-meshes without a node are not)\n");
    }

    // ── G2: node world transforms are self-consistent ──────────────────
    printf("\nG2: node world consistency (world == parentWorld * local)\n");
    {
        float worst = 0.0f;
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            const AnimNode& n = model.nodes[i];
            glm::mat4 parentWorld = (n.parentIndex >= 0)
                ? model.nodes[n.parentIndex].world : glm::mat4(1.0f);
            worst = fmaxf(worst, MatDiff(n.world, parentWorld * n.local));
        }
        Gate(worst < 1e-6f, "world == parentWorld * local for all nodes",
             (snprintf(detail, sizeof(detail), "worst rel diff %.3e", worst), detail));
    }

    // ── G3: per-part local TRS round-trip (the import-time math) ───────
    printf("\nG3: part local TRS round-trip (DecomposeTRS of node-world-relative-to-root)\n");
    glm::mat4 fileRoot = model.nodes.empty() ? glm::mat4(1.0f) : model.nodes[0].world;
    glm::mat4 fileRootInv = glm::inverse(fileRoot);

    struct Part {
        std::string name;
        glm::mat4 partLocal = glm::mat4(1.0f);   // node world * fileRootInv (what the editor stores)
        Vector3 p = Vector3();
        Vector3 r = Vector3();
        Vector3 s = Vector3(1.0f, 1.0f, 1.0f);
    };
    std::vector<Part> parts;
    {
        float worst = 0.0f;
        int decomposed = 0;
        for (const auto& raw : model.rawMeshes) {
            if (raw.nodeIndex < 0 || (size_t)raw.nodeIndex >= model.nodes.size()) continue;
            Part pt;
            pt.name = raw.name;
            pt.partLocal = model.nodes[raw.nodeIndex].world * fileRootInv;
            float err = 0.0f;
            if (DecomposeTRS(pt.partLocal, pt.p, pt.r, pt.s, err)) {
                ++decomposed;
                // Rebuild from the stored TRS; must match the original.
                worst = fmaxf(worst, MatDiff(TrsOf(pt.p, pt.r, pt.s), pt.partLocal));
            } else {
                worst = 1e9f;
            }
            parts.push_back(std::move(pt));
        }
        Gate(decomposed == (int)parts.size() && worst < 1e-4f,
             "every part decomposes to TRS and round-trips",
             (snprintf(detail, sizeof(detail), "%d/%zu decomposed, worst rel diff %.3e",
                       decomposed, parts.size(), worst), detail));
    }

    // ── G4: scene hierarchy semantics (mirror of LoadFBXAtPath) ────────
    printf("\nG4: hierarchy semantics (root group + child parts)\n");
    size_t baseCount = GetSceneObjects().size();

    // Model root: transform-only group, identity, like the editor creates.
    // (id-based API: SceneObject pointers/refs dangle across AddToScene
    // calls — the parts below grow the vector — so re-fetch at each use.)
    uint32_t rootId = AddToScene("Idle_root", nullptr, CreateDefaultMaterial());
    if (SceneObject* r = GetSceneObject(rootId)) r->isGroup = true;
    {
        SceneObject* root = GetSceneObject(rootId);
        Gate(root != nullptr && root->id != 0 && root->isGroup && !root->mesh,
             "model root created (group, no mesh, identity TRS)");
    }

    std::vector<uint32_t> partIds;
    for (const auto& pt : parts) {
        // Same shape the editor adds: real (here empty) mesh + imported TRS.
        uint32_t pid = AddToScene(pt.name, CreateMesh(PrimitiveMesh{}), CreateDefaultMaterial());
        SceneObject* added = GetSceneObject(pid);
        added->position = pt.p;
        added->rotation = pt.r;
        added->scale    = pt.s;
        added->parentId = rootId;
        partIds.push_back(pid);
    }
    Gate(GetSceneObjects().size() == baseCount + 1 + partIds.size(),
         "scene grew by 1 root + N parts");

    // 4a: root at identity -> each part's world == its imported local matrix.
    {
        float worst = 0.0f;
        for (size_t i = 0; i < partIds.size(); ++i)
            worst = fmaxf(worst, MatDiff(ComputeObjectWorldMatrix(partIds[i]), parts[i].partLocal));
        Gate(worst < 1e-4f, "root at identity: part world == imported local",
             (snprintf(detail, sizeof(detail), "worst rel diff %.3e", worst), detail));
    }

    // 4b: transform the root -> every part follows (world = rootLocal * partLocal).
    {
        SceneObject* root = GetSceneObject(rootId);  // fresh: vector regrew above
        root->position = Vector3(1.0f, 2.0f, 3.0f);
        root->rotation = Vector3(0.0f, 1.05f, 0.0f);
        root->scale    = Vector3(2.0f, 2.0f, 2.0f);
        glm::mat4 rootLocal = TrsOf(root->position, root->rotation, root->scale);

        float worst = 0.0f;
        for (size_t i = 0; i < partIds.size(); ++i) {
            glm::mat4 actual = ComputeObjectWorldMatrix(partIds[i]);
            glm::mat4 expect = rootLocal * TrsOf(parts[i].p, parts[i].r, parts[i].s);
            float d = MatDiff(actual, expect);
            worst = fmaxf(worst, d);
        }
        Gate(worst < 1e-4f, "transforming the root moves every part (root * part)",
             (snprintf(detail, sizeof(detail), "worst rel diff %.3e", worst), detail));
    }

    // 4c: removing the root cascades to all parts.
    RemoveFromScene(rootId);
    {
        auto& objs = GetSceneObjects();
        size_t left = objs.size();
        int partsLeft = 0;
        for (const auto& o : objs)
            for (uint32_t id : partIds)
                if (o.id == id) ++partsLeft;
        Gate(left == baseCount && partsLeft == 0, "removing the root cascades to all parts",
             (snprintf(detail, sizeof(detail), "scene size %zu (base %zu), parts left %d",
                       left, baseCount, partsLeft), detail));

        // Dangling id must be safe: world matrix of a removed part is identity.
        float worst = 0.0f;
        for (uint32_t id : partIds)
            worst = fmaxf(worst, MatDiff(ComputeObjectWorldMatrix(id), glm::mat4(1.0f)));
        Gate(worst == 0.0f, "removed part ids are safe (identity world)");
    }

    printf("\n== test_model_hier: %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
