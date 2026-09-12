// ============================================================================
// test_animator_eval.cpp
//
// Headless validation of the ENGINE's animation path (no GL window needed):
//
//   AssetLoader::LoadFBX       -> model + extracted skin data
//   Animator::RegisterObject   -> skinned object
//   AssetLoader::LoadFBXAnimation -> AnimationFile (own node tree + clips)
//   Animator::Bind / SetTime   -> per-bone palettes J(b, t)
//
// Runs the same gates as test_skin_eval.cpp, but through the engine code
// that the editor actually uses:
//
//   G1. registration: palette count == number of model bones
//   G2. API sanity: unregistered object id returns a 0-matrix palette
//   G3. rigidity: for single-bone vertices, |skinned - joint| is constant
//      over time (the scale-independent skinning-correctness gate)
//   G4. motion: vertices actually move across the clip
//   G5. scrubbing: SetTime(t) is respected (time readback)
//
// Usage:  test_animator_eval.exe <model.fbx> <animation.fbx>
// ============================================================================

#include "core/engine.h"
#include "core/animator.h"
#include "core/asset_loader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace CoreEngine;

namespace {

    // Must match the loader's base vertex layout: pos(3)+normal(3)+uv(2).
    constexpr int kVertexFloatStride = 8;


    // Translation column of an affine matrix.
    inline glm::vec3 T3(const glm::mat4& m) {
        return glm::vec3(m[3].x, m[3].y, m[3].z);
    }
    // xyz of a homogeneous transformed point.
    inline glm::vec3 T3(const glm::vec4& v) {
        return glm::vec3(v.x, v.y, v.z);
    }

    float VDist(const glm::vec3& a, const glm::vec3& b) {
        glm::vec3 d = b - a;
        return glm::length(d);
    }

    // Skinned position of mesh-local vertex p under palette J (blended).
    glm::vec3 SkinVertex(const RawMeshData& raw, size_t v, const std::vector<glm::mat4>& J) {
        const float* base = &raw.vertices[v * kVertexFloatStride];
        glm::vec3 p(base[0], base[1], base[2]);
        const VertexSkin& sk = raw.skins[v];
        glm::mat4 S = glm::mat4(0.0f);
        for (int k = 0; k < 4; ++k) {
            if (sk.weights[k] <= 0.0f) continue;
            int bi = sk.boneIndices[k];
            S += J[bi] * sk.weights[k];
        }
        return T3(S * glm::vec4(p, 1.0f));
    }

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <model.fbx> <animation.fbx>\n", argv[0]);
        return 2;
    }

    // ---- 1. load the model (engine loader: nodes + meshes + skins) ----
    printf("== engine-path animator eval ==\n");
    printf("model:     %s\n", argv[1]);
    printf("animation: %s\n\n", argv[2]);

    FBXModel model = AssetLoader::LoadFBX(argv[1]);
    if (model.rawMeshes.empty()) {
        fprintf(stderr, "FAIL: model has no meshes\n");
        return 1;
    }

    // Find the skinned raw mesh (largest vertex count among skinned).
    int bodyIdx = -1;
    size_t bodyVerts = 0;
    for (size_t i = 0; i < model.rawMeshes.size(); ++i) {
        if (!model.rawMeshes[i].isSkinned()) continue;
        size_t n = model.rawMeshes[i].vertices.size() / kVertexFloatStride;
        if (n > bodyVerts) { bodyVerts = n; bodyIdx = (int)i; }
    }
    if (bodyIdx < 0) {
        fprintf(stderr, "FAIL: no skinned mesh in model\n");
        return 1;
    }
    RawMeshData& body = model.rawMeshes[bodyIdx];
    printf("skinned mesh: '%s' (%zu verts, %zu bones)\n\n",
           body.name.c_str(), bodyVerts, body.meshBones.size());

    // ---- 2. register it with the Animator (same call the editor makes) ----
    MeshSkinBinding bind;
    bind.meshName = body.name;
    bind.Wmesh = body.meshWorldRest;
    for (const auto& mb : body.meshBones) {
        bind.boneNames.push_back(mb.name);
        bind.IB.push_back(mb.IB);
        bind.boneRestWorld.push_back(mb.restWorld);
    }
    const uint32_t OBJ_ID = 1;
    Animator::Get().RegisterObject(OBJ_ID, bind);

    // ---- 3. load the animation file (engine loader) and bind ----
    AnimationFile file = AssetLoader::LoadFBXAnimation(argv[2]);
    if (!file.success) {
        fprintf(stderr, "FAIL: could not load animation file\n");
        return 1;
    }
    if (!Animator::Get().Bind(file)) {
        fprintf(stderr, "FAIL: Animator::Bind failed\n");
        return 1;
    }
    const auto* clip = Animator::Get().ActiveClip();
    if (!clip || clip->duration <= 0.0f) {
        fprintf(stderr, "FAIL: no active clip\n");
        return 1;
    }

    // ---- G1: palette count ----
    {
        int n = Animator::Get().BoneCount(OBJ_ID);
        bool ok = (n == (int)body.meshBones.size());
        printf("G1 palette count: %d (expected %zu): %s\n", n,
               body.meshBones.size(), ok ? "PASS" : "FAIL");
        if (!ok) return 1;
    }

    // ---- G2: API sanity (unregistered object -> empty palette) ----
    {
        glm::mat4 dummy[16];
        int n = Animator::Get().GetBonePalette(0xDEAD, dummy);
        bool ok = (n == 0);
        printf("G2 unregistered id -> %d matrices: %s\n", n, ok ? "PASS" : "FAIL");
        if (!ok) return 1;
    }

    // ---- time samples ----
    std::vector<float> ts;
    const int N = 9;
    for (int i = 0; i < N; ++i) ts.push_back(clip->duration * (float)i / (float)(N - 1));

    // ---- G5: scrubbing (SetTime respected) ----
    Animator::Get().SetTime(ts[3]);
    bool scrubOk = std::abs(Animator::Get().GetTime() - ts[3]) < 1e-4f;
    printf("G5 SetTime(%.3f) -> GetTime(%.3f): %s\n", ts[3], Animator::Get().GetTime(),
           scrubOk ? "PASS" : "FAIL");


    // ---- G3: rigidity (single-bone verts: |skinned - joint| const over t) ----
    printf("\n--- rigidity (single-bone verts, |v-joint| variation over time) ---\n");
    struct Rigid { size_t vi; int bone; };
    std::vector<Rigid> rigids;
    {
        size_t step = std::max<size_t>(1, bodyVerts / 3000);
        for (size_t v = 0; v < bodyVerts; v += step) {
            const VertexSkin& sk = body.skins[v];
            // EXACTLY single-bone vertex with full weight (matches the
            // reference tool: multi-bone verts must NOT be measured against
            // one joint — their blend legitimately moves between joints).
            int b = -1; int nB = 0;
            for (int k = 0; k < 4; ++k) {
                if (sk.weights[k] > 1e-6f) { b = sk.boneIndices[k]; nB++; }
            }
            // (weights[] is slot-aligned; find the sole non-zero slot's weight)
            float w0 = 0.0f;
            for (int k = 0; k < 4; ++k) if (sk.weights[k] > 1e-6f) w0 = sk.weights[k];
            if (nB != 1 || w0 < 0.99f) continue;
            rigids.push_back({v, b});
        }
    }
    printf("  %zu single-bone vertices sampled\n", rigids.size());

    // Joint position in mesh space at time t for bone b:
    //   joint = J * IB^-1 * (0,0,0,1)   (= WmeshInv2 * Wa(b,t) translation)
    std::vector<glm::vec3> jointOff(body.meshBones.size());
    for (size_t b = 0; b < body.meshBones.size(); ++b) {
        glm::mat4 ibInv = glm::inverse(body.meshBones[b].IB);
        jointOff[b] = T3(ibInv);
    }

    std::vector<float> mn(rigids.size(), 1e30f), mx(rigids.size(), 0.0f);
    std::vector<glm::mat4> palette(body.meshBones.size());
    for (float t : ts) {
        Animator::Get().SetTime(t);
        Animator::Get().GetBonePalette(OBJ_ID, palette.data());
        float worst = 0.0f;
        for (size_t ri = 0; ri < rigids.size(); ++ri) {
            const Rigid& r = rigids[ri];
            const glm::mat4& J = palette[r.bone];
            glm::vec3 sv = SkinVertex(body, r.vi, palette);
            glm::vec3 j = T3(J * glm::vec4(jointOff[r.bone], 1.0f));
            float d = VDist(sv, j);
            mn[ri] = std::min(mn[ri], d);
            mx[ri] = std::max(mx[ri], d);
        }
        for (size_t ri = 0; ri < rigids.size(); ++ri)
            worst = std::max(worst, mx[ri] - mn[ri]);
        printf("  (t=%.3f) running max per-vertex variation = %.3e\n", t, worst);
    }
    float finalWorst = 0.0f;
    size_t worstRi = 0;
    for (size_t ri = 0; ri < rigids.size(); ++ri)
        if (mx[ri] - mn[ri] > finalWorst) { finalWorst = mx[ri] - mn[ri]; worstRi = ri; }
    bool rigOk = finalWorst < 1e-3f;
    printf("  rigidity: %s (max per-vertex variation = %.3e)\n",
           rigOk ? "PASS (constant over time)" : "FAIL (stretch)", finalWorst);


    // ---- G4: motion ----
    {
        Animator::Get().SetTime(0.0f);
        Animator::Get().GetBonePalette(OBJ_ID, palette.data());
        std::vector<glm::vec3> p0(bodyVerts);
        for (size_t v = 0; v < bodyVerts; v += 4) p0[v] = SkinVertex(body, v, palette);

        Animator::Get().SetTime(clip->duration * 0.5f);
        Animator::Get().GetBonePalette(OBJ_ID, palette.data());
        float sum = 0.0f; size_t n = 0;
        for (size_t v = 0; v < bodyVerts; v += 4) {
            sum += VDist(p0[v], SkinVertex(body, v, palette));
            ++n;
        }
        float avg = n ? sum / (float)n : 0.0f;
        bool motionOk = avg > 1e-4f;
        printf("  motion: avg |v(0.5d) - v(0)| = %.5f: %s\n", avg,
               motionOk ? "PASS" : "FAIL (static)");
        if (!rigOk || !motionOk || !scrubOk) return 1;
    }

    printf("\nALL GATES PASSED — engine animation path OK\n");
    return 0;
}
