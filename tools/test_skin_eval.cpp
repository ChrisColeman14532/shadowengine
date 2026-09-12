// CPU simulation of the full character skinning pipeline (no engine deps).
//
// v2 — corrected binding design (v1 bound anim channels to the MODEL tree by
// exact name; test_anim_bind proved that fails 18/21 for elf.fbx+Idle.fbx
// because assimp's $AssimpFbx$ wrapper decomposition differs between files).
//
// Design under test (this is what the engine will implement):
//
//   * The clip is evaluated on the ANIM file's own node tree. Within one
//     file, assimp names animation channels exactly after the tree nodes it
//     created, so every channel binds (asserted below).
//   * The two files' skeleton spaces differ by one constant affine map
//     (root wrappers/axis conversion in the model export). We estimate it
//     from the Hips bone:  D = Wm(Hips_rest) * Wa(Hips_rest)^-1
//     and fold it into the mesh transform:  WmeshInv' = WmeshInv * D.
//   * Bone palette per mesh:  J(b,t) = WmeshInv' * Wa(b,t) * IB
//     where Wa(b,t) = bone world in anim root space at time t, and
//     IB = BrMesh^-1  (formula N, validated by v1 — offset matrix ignored;
//     BrMesh = WmeshInv * Wm(bone_rest), i.e. bone rest world in mesh space).
//
// Gates (exit code != 0 on failure):
//   G1. every channel binds to the ANIM tree (asserted at startup);
//   G2. BrMesh*OF ~ I for every bone (catches bind-matrix convention bugs);
//   G3. rigidity, SCALE-INDEPENDENT: for single-bone vertices the distance
//      to the joint must be CONSTANT OVER TIME (max-min across samples).
//      (v1 compared against |u| in bone space, which breaks when the mesh
//      node chain carries a scale — elf.fbx carries 1.638 — and produced a
//      false STRETCH even though the deviation was identical at every t.)
//   G4. motion: vertices actually move over the clip.
// Informational: t=0 |skinned-rest| (model-rest vs anim-start pose gap) and
// a joint-position report for Hips/Head/feet in model root space.
//
// Build: tools\build_test_skin_eval.bat
// Usage: test_skin_eval <model.fbx> <anim.fbx> [clipIndex]
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>

// ---------------------------------------------------------------- minimal math
struct Vec3 { float x, y, z; };
static Vec3 VAdd(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3 VSub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 VMul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static float VDot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static float VLen(Vec3 a) { return std::sqrt(VDot(a, a)); }
static float VDist(Vec3 a, Vec3 b) { return VLen(VSub(a, b)); }

struct Quat { float w, x, y, z; };
static Quat QIdent() { return {1, 0, 0, 0}; }
static float QDot(Quat a, Quat b) { return a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z; }
static Quat QScale(Quat q, float s) { return {q.w*s, q.x*s, q.y*s, q.z*s}; }
static Quat QNeg(Quat q) { return {-q.w, -q.x, -q.y, -q.z}; }
static Quat QAdd(Quat a, Quat b) { return {a.w+b.w, a.x+b.x, a.y+b.y, a.z+b.z}; }
static Quat QNorm(Quat q) {
    float l = std::sqrt(QDot(q, q));
    return (l < 1e-12f) ? QIdent() : QScale(q, 1.f / l);
}
static Quat QSlerp(Quat a, Quat b, float f) {
    float d = QDot(a, b); Quat bb = b;
    if (d < 0) { d = -d; bb = QNeg(b); }
    if (d > 0.9995f) return QNorm(QAdd(QScale(a, 1.f - f), QScale(bb, f)));
    float th = std::acos(d > 1.f ? 1.f : (d < -1.f ? -1.f : d));
    float s = std::sin(th);
    return QNorm(QAdd(QScale(a, std::sin((1.f - f)*th) / s),
                      QScale(bb, std::sin(f*th) / s)));
}
static Quat QFromMat(const float r[3][3]) {
    float tr = r[0][0] + r[1][1] + r[2][2];
    Quat q;
    if (tr > 0) {
        float s = std::sqrt(tr + 1) * 2;
        q = {0.25f*s, (r[2][1]-r[1][2])/s, (r[0][2]-r[2][0])/s, (r[1][0]-r[0][1])/s};
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        float s = std::sqrt(1.f + r[0][0] - r[1][1] - r[2][2]) * 2;
        q = {(r[2][1]-r[1][2])/s, 0.25f*s, (r[0][1]+r[1][0])/s, (r[0][2]+r[2][0])/s};
    } else if (r[1][1] > r[2][2]) {
        float s = std::sqrt(1.f + r[1][1] - r[0][0] - r[2][2]) * 2;
        q = {(r[0][2]-r[2][0])/s, (r[0][1]-r[2][0])/s, 0.25f*s, (r[1][2]-r[2][1])/s};
    } else {
        float s = std::sqrt(1.f + r[2][2] - r[0][0] - r[1][1]) * 2;
        q = {(r[1][0]-r[0][1])/s, (r[0][2]-r[2][0])/s, (r[1][2]-r[2][1])/s, 0.25f*s};
    }
    return QNorm(q);
}
static void QToMat(Quat q, float r[3][3]) {
    float w = q.w, x = q.x, y = q.y, z = q.z;
    r[0][0] = 1-2*(y*y+z*z); r[0][1] = 2*(x*y-z*w);   r[0][2] = 2*(x*z+y*w);
    r[1][0] = 2*(x*y+z*w);   r[1][1] = 1-2*(x*x+z*z); r[1][2] = 2*(y*z-x*w);
    r[2][0] = 2*(x*z-y*w);   r[2][1] = 2*(y*z+x*w);   r[2][2] = 1-2*(x*x+y*y);
}

struct M4 { float m[4][4]; };
static M4 M4I() {
    M4 r;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) r.m[i][j] = (i == j) ? 1 : 0;
    return r;
}
static M4 FromAI(const aiMatrix4x4& A) {
    M4 r;
    r.m[0][0]=A.a1; r.m[0][1]=A.a2; r.m[0][2]=A.a3; r.m[0][3]=A.a4;
    r.m[1][0]=A.b1; r.m[1][1]=A.b2; r.m[1][2]=A.b3; r.m[1][3]=A.b4;
    r.m[2][0]=A.c1; r.m[2][1]=A.c2; r.m[2][2]=A.c3; r.m[2][3]=A.c4;
    r.m[3][0]=A.d1; r.m[3][1]=A.d2; r.m[3][2]=A.d3; r.m[3][3]=A.d4;
    return r;
}
static M4 Mul(const M4& A, const M4& B) {
    M4 W;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float acc = 0;
            for (int k = 0; k < 4; ++k) acc += A.m[r][k] * B.m[k][c];
            W.m[r][c] = acc;
        }
    return W;
}
// Inverse of an affine 4x4 (any invertible 3x3 linear part + translation).
static M4 InvAffine(const M4& M) {
    M4 I = M4I();
    float a=M.m[0][0], b=M.m[0][1], c=M.m[0][2];
    float d=M.m[1][0], e=M.m[1][1], f=M.m[1][2];
    float g=M.m[2][0], h=M.m[2][1], i=M.m[2][2];
    const float det = a*(e*i-f*h) - b*(d*i-f*g) + c*(d*h-e*g);
    if (std::fabs(det) < 1e-12f) return I;
    const float id = 1.f / det;
    I.m[0][0] = (e*i-f*h)*id; I.m[0][1] = (c*h-b*i)*id; I.m[0][2] = (b*f-c*e)*id;
    I.m[1][0] = (f*g-d*i)*id; I.m[1][1] = (a*i-c*g)*id; I.m[1][2] = (c*e-a*f)*id;
    I.m[2][0] = (d*h-e*g)*id; I.m[2][1] = (b*g-a*h)*id; I.m[2][2] = (a*e-b*d)*id;
    float tx=M.m[0][3], ty=M.m[1][3], tz=M.m[2][3];
    I.m[0][3] = -(I.m[0][0]*tx + I.m[0][1]*ty + I.m[0][2]*tz);
    I.m[1][3] = -(I.m[1][0]*tx + I.m[1][1]*ty + I.m[1][2]*tz);
    I.m[2][3] = -(I.m[2][0]*tx + I.m[2][1]*ty + I.m[2][2]*tz);
    return I;
}
static Vec3 Tfm(const M4& M, Vec3 v) {
    return { M.m[0][0]*v.x + M.m[0][1]*v.y + M.m[0][2]*v.z + M.m[0][3],
             M.m[1][0]*v.x + M.m[1][1]*v.y + M.m[1][2]*v.z + M.m[1][3],
             M.m[2][0]*v.x + M.m[2][1]*v.y + M.m[2][2]*v.z + M.m[2][3] };
}
static Vec3 T3(const M4& M) { return {M.m[0][3], M.m[1][3], M.m[2][3]}; }
static M4 ComposeTRS(Vec3 t, Quat q, Vec3 s) {
    float r[3][3]; QToMat(q, r);
    float sc[3] = {s.x, s.y, s.z};
    M4 M = M4I();
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 3; ++c)
            M.m[i][c] = r[i][c] * sc[c];
    M.m[0][3] = t.x; M.m[1][3] = t.y; M.m[2][3] = t.z;
    return M;
}
// Decompose an affine matrix into T/R/S. Handles negative scale (reflection).
static void Decompose(const M4& M, Vec3& t, Quat& q, Vec3& s) {
    t = T3(M);
    float col[3][3] = {
        {M.m[0][0], M.m[1][0], M.m[2][0]},
        {M.m[0][1], M.m[1][1], M.m[2][1]},
        {M.m[0][2], M.m[1][2], M.m[2][2]}
    };
    float sl[3] = { VLen({col[0][0], col[1][0], col[2][0]}),
                    VLen({col[0][1], col[1][1], col[2][1]}),
                    VLen({col[0][2], col[1][2], col[2][2]}) };
    s = {sl[0], sl[1], sl[2]};
    float r[3][3];
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 3; ++c)
            r[i][c] = (sl[c] > 1e-9f) ? col[i][c] / sl[c] : 0.f;
    float det = r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])
              - r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])
              + r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]);
    if (det < 0) {
        s.z = -s.z;
        r[0][2] = -r[0][2]; r[1][2] = -r[1][2]; r[2][2] = -r[2][2];
    }
    q = QFromMat(r);
}
static void PrintT3(const char* l, Vec3 v) {
    printf("%s (%+.4f, %+.4f, %+.4f)\n", l, v.x, v.y, v.z);
}

// ---------------------------------------------------------------- node trees
struct Node {
    std::string name;
    int parent = -1;
    M4 local;   // file local transform
    M4 world;   // rest-pose world transform (computed)
    Vec3 rT{0,0,0}; Quat rQ = QIdent(); Vec3 rS{1,1,1};  // decomposed rest local
};

static void Flatten(const aiNode* n, int parent, std::vector<Node>& out) {
    int idx = (int)out.size();
    Node nd; nd.name = n->mName.C_Str(); nd.parent = parent; nd.local = FromAI(n->mTransformation);
    out.push_back(nd);
    for (unsigned i = 0; i < n->mNumChildren; ++i) Flatten(n->mChildren[i], idx, out);
}

// Decompose locals into rT/rQ/rS and compute rest-pose world transforms.
static void ComputeRests(std::vector<Node>& nodes) {
    for (size_t i = 0; i < nodes.size(); ++i)
        Decompose(nodes[i].local, nodes[i].rT, nodes[i].rQ, nodes[i].rS);
    for (size_t i = 0; i < nodes.size(); ++i)
        nodes[i].world = (nodes[i].parent >= 0) ? Mul(nodes[nodes[i].parent].world, nodes[i].local) : nodes[i].local;
}

static int FindMeshNode(const aiNode* n, unsigned meshIdx, std::unordered_map<std::string,int>& names) {
    for (unsigned mi = 0; mi < n->mNumMeshes; ++mi)
        if (n->mMeshes[mi] == meshIdx) return names[n->mName.C_Str()];
    for (unsigned i = 0; i < n->mNumChildren; ++i) {
        int r = FindMeshNode(n->mChildren[i], meshIdx, names);
        if (r >= 0) return r;
    }
    return -1;
}

// ---------------------------------------------------------------- anim side
struct Track {
    std::string name;
    std::vector<float> pt, rt, st;
    std::vector<Vec3> pv, sv;
    std::vector<Quat> rv;
    bool Any() const { return !pt.empty() || !rt.empty() || !st.empty(); }
};

static Vec3 SampleV(const std::vector<float>& t, const std::vector<Vec3>& v, float x) {
    if (x <= t.front()) return v.front();
    if (x >= t.back()) return v.back();
    size_t lo = 0, hi = t.size() - 1;
    while (hi - lo > 1) { size_t mid = (lo + hi) / 2; if (t[mid] <= x) lo = mid; else hi = mid; }
    float f = (x - t[lo]) / ((t[hi] - t[lo]) + 1e-6f);
    return VAdd(v[lo], VMul(VSub(v[hi], v[lo]), f));
}
static Quat SampleQ(const std::vector<float>& t, const std::vector<Quat>& q, float x) {
    if (x <= t.front()) return q.front();
    if (x >= t.back()) return q.back();
    size_t lo = 0, hi = t.size() - 1;
    while (hi - lo > 1) { size_t mid = (lo + hi) / 2; if (t[mid] <= x) lo = mid; else hi = mid; }
    float f = (x - t[lo]) / ((t[hi] - t[lo]) + 1e-6f);
    return QSlerp(q[lo], q[hi], f);
}

struct Clip {
    std::string name;
    float duration = 0.f, tps = 30.f;
    std::vector<Track> tracks;
    std::unordered_map<std::string, int> byName;
};

static Clip ParseClip(const aiAnimation* an) {
    Clip c;
    c.name = an->mName.C_Str();
    c.tps = (an->mTicksPerSecond > 0.f) ? (float)an->mTicksPerSecond : 30.f;
    c.duration = (float)(an->mDuration) / c.tps;
    for (unsigned ch = 0; ch < an->mNumChannels; ++ch) {
        const aiNodeAnim* n = an->mChannels[ch];
        Track tr; tr.name = n->mNodeName.C_Str();
        for (unsigned i = 0; i < n->mNumPositionKeys; ++i) { tr.pt.push_back((float)n->mPositionKeys[i].mTime / c.tps); tr.pv.push_back({n->mPositionKeys[i].mValue.x, n->mPositionKeys[i].mValue.y, n->mPositionKeys[i].mValue.z}); }
        for (unsigned i = 0; i < n->mNumRotationKeys; ++i)  { tr.rt.push_back((float)n->mRotationKeys[i].mTime / c.tps);  tr.rv.push_back({n->mRotationKeys[i].mValue.w, n->mRotationKeys[i].mValue.x, n->mRotationKeys[i].mValue.y, n->mRotationKeys[i].mValue.z}); }
        for (unsigned i = 0; i < n->mNumScalingKeys; ++i)   { tr.st.push_back((float)n->mScalingKeys[i].mTime / c.tps);   tr.sv.push_back({n->mScalingKeys[i].mValue.x, n->mScalingKeys[i].mValue.y, n->mScalingKeys[i].mValue.z}); }
        c.byName[tr.name] = (int)c.tracks.size();
        c.tracks.push_back(std::move(tr));
    }
    return c;
}

// Evaluate the pose at time t: world matrix for every node of the given tree.
// Nodes with a channel use evaluated components; empty sub-tracks fall back
// to the node's decomposed rest component.
static void BuildPose(const std::vector<Node>& nodes, const Clip& clip, float t,
                      std::vector<M4>& world) {
    world.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        M4 local = nodes[i].local;
        auto it = clip.byName.find(nodes[i].name);
        if (it != clip.byName.end()) {
            const Track& tr = clip.tracks[it->second];
            if (tr.Any()) {
                Vec3 oT = nodes[i].rT; Quat oR = nodes[i].rQ; Vec3 oS = nodes[i].rS;
                if (!tr.pt.empty()) oT = SampleV(tr.pt, tr.pv, t);
                if (!tr.rt.empty()) oR = SampleQ(tr.rt, tr.rv, t);
                if (!tr.st.empty()) oS = SampleV(tr.st, tr.sv, t);
                local = ComposeTRS(oT, oR, oS);
            }
        }
        world[i] = (nodes[i].parent >= 0) ? Mul(world[nodes[i].parent], local) : local;
    }
}

// ---------------------------------------------------------------- skin data
struct BoneSkin {
    std::string name;
    int modelNode = -1;   // index into model nodes (rest world, for BrMesh)
    int animNode = -1;    // index into anim nodes (animated world at runtime)
    M4 BrMesh;   // bone rest world in mesh (vertex) space
    M4 OF;       // mOffsetMatrix (kept for the formula comparison)
    M4 IB;       // inverse bind (set per formula)
};
struct MeshSkin {
    unsigned meshIdx = 0;
    int nodeIdx = -1;
    M4 Wmesh, WmeshInv2;  // WmeshInv2 = WmeshInv * D (D = anim->model root align)
    std::vector<BoneSkin> bones;
    std::vector<std::vector<std::pair<int, float>>> weights;  // per vertex: (boneIdx, w)
};

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: test_skin_eval <model.fbx> <anim.fbx> [clipIndex]\n"); return 1; }
    int clipIdx = (argc > 3) ? std::atoi(argv[3]) : 0;

    Assimp::Importer mimp;
    const aiScene* model = mimp.ReadFile(argv[1], aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (!model) { printf("LOAD FAILED (model): %s\n", mimp.GetErrorString()); return 1; }
    Assimp::Importer aimp;
    const aiScene* anim = aimp.ReadFile(argv[2], aiProcess_Triangulate | aiProcess_GenUVCoords);
    if (!anim) { printf("LOAD FAILED (anim): %s\n", aimp.GetErrorString()); return 1; }
    if (anim->mNumAnimations == 0) { printf("anim file has no clips\n"); return 1; }
    if (clipIdx < 0 || (size_t)clipIdx >= anim->mNumAnimations) { printf("bad clip index\n"); return 1; }

    // ---- model node tree (rest pose) ----
    std::vector<Node> nodes;
    Flatten(model->mRootNode, -1, nodes);
    ComputeRests(nodes);
    std::unordered_map<std::string, int> nmap;
    for (size_t i = 0; i < nodes.size(); ++i) nmap[nodes[i].name] = (int)i;
    printf("=== model: %s (%zu nodes, %u meshes) ===\n", argv[1], nodes.size(), model->mNumMeshes);

    // ---- anim node tree + clip ----
    std::vector<Node> anodes;
    Flatten(anim->mRootNode, -1, anodes);
    ComputeRests(anodes);
    std::unordered_map<std::string, int> amap;
    for (size_t i = 0; i < anodes.size(); ++i) amap[anodes[i].name] = (int)i;

    Clip clip = ParseClip(anim->mAnimations[clipIdx]);
    printf("=== anim : %s  clip[%d] '%s'  %.3fs @ %.1f tps, %zu channels, %zu tree nodes ===\n",
           argv[2], clipIdx, clip.name.c_str(), clip.duration, clip.tps, clip.tracks.size(), anodes.size());

    // Every channel must bind to the ANIM tree (same importer, same file).
    {
        int miss = 0;
        for (const auto& tr : clip.tracks)
            if (!amap.count(tr.name)) { ++miss; printf("  UNBOUND channel: '%s'\n", tr.name.c_str()); }
        if (miss) { printf("FATAL: %d channels do not bind to the anim tree\n", miss); return 1; }
        printf("  all %zu channels bind to anim-tree nodes\n", clip.tracks.size());
    }

    // ---- D: anim-root-space -> model-root-space (estimated from Hips) ----
    M4 D = M4I();
    {
        auto mh = nmap.find("mixamorig:Hips");
        auto ah = amap.find("mixamorig:Hips");
        if (mh == nmap.end() || ah == amap.end()) {
            printf("FATAL: mixamorig:Hips not found in both files (D alignment needs it)\n");
            return 1;
        }
        D = Mul(nodes[mh->second].world, InvAffine(anodes[ah->second].world));  // Wm(Hips) * Wa(Hips_rest)^-1
    }
    printf("=== D (anim->model root align, from Hips) ===\n");
    for (int r = 0; r < 4; ++r)
        printf("    (%+.4f %+.4f %+.4f %+.4f)\n", D.m[r][0], D.m[r][1], D.m[r][2], D.m[r][3]);

    // ---- build skin data per mesh ----
    std::vector<MeshSkin> skins;
    float maxCerr = 0.f;
    for (unsigned m = 0; m < model->mNumMeshes; ++m) {
        const aiMesh* mesh = model->mMeshes[m];
        if (mesh->mNumBones == 0) continue;
        MeshSkin ms;
        float meshCerr = 0.f;
        ms.meshIdx = m;
        ms.nodeIdx = FindMeshNode(model->mRootNode, m, nmap);
        if (ms.nodeIdx < 0) { printf("  mesh[%u]: node not found, skipped\n", m); continue; }
        ms.Wmesh = nodes[ms.nodeIdx].world;
        ms.WmeshInv2 = Mul(InvAffine(ms.Wmesh), D);
        ms.weights.resize(mesh->mNumVertices);
        for (unsigned b = 0; b < mesh->mNumBones; ++b) {
            BoneSkin bs;
            bs.name = mesh->mBones[b]->mName.C_Str();
            bs.modelNode = nmap.count(bs.name) ? nmap[bs.name] : -1;
            bs.animNode = amap.count(bs.name) ? amap[bs.name] : -1;
            if (bs.modelNode < 0) {
                printf("  mesh[%u]: bone '%s' NOT in model node tree!\n", m, bs.name.c_str());
                continue;
            }
            if (bs.animNode < 0) {
                printf("  mesh[%u]: bone '%s' NOT in anim node tree!\n", m, bs.name.c_str());
                continue;
            }
            bs.BrMesh = Mul(InvAffine(ms.Wmesh), nodes[bs.modelNode].world);
            bs.OF = FromAI(mesh->mBones[b]->mOffsetMatrix);
            // Sanity: BrMesh*OF should be ~identity for well-formed bones
            // (test_skin.cpp finding; the REye mesh of elf.fbx is an exception —
            // it is bound to mixamorig:RightToeBase and its OF carries an extra
            // reflection. Broken source data; formula N still applies).
            M4 C = Mul(bs.BrMesh, bs.OF);
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) {
                    float e = std::fabs(C.m[r][c] - ((r == c) ? 1.f : 0.f));
                    if (e > maxCerr) maxCerr = e;
                    if (e > meshCerr) meshCerr = e;
                }
            ms.bones.push_back(std::move(bs));
        }
        for (unsigned b = 0; b < mesh->mNumBones; ++b)
            for (unsigned w = 0; w < mesh->mBones[b]->mNumWeights; ++w)
                ms.weights[mesh->mBones[b]->mWeights[w].mVertexId]
                    .push_back({(int)b, (float)mesh->mBones[b]->mWeights[w].mWeight});
        printf("  mesh[%u] '%s'  node='%s'  %u verts, %zu bones  (BrMesh*OF err %.2e)\n",
               m, mesh->mName.C_Str(), nodes[ms.nodeIdx].name.c_str(),
               mesh->mNumVertices, ms.bones.size(), meshCerr);
        skins.push_back(std::move(ms));
    }
    if (skins.empty()) { printf("no skinned meshes\n"); return 1; }
    printf("  max |BrMesh*OF - I| = %.2e  (test_skin finding: should be ~0)\n", maxCerr);

    // ---- inverse-bind candidates ----
    // S: IB = (BrMesh*OF)^-1   (standard assimp convention)
    // N: IB = BrMesh^-1        (offset ignored)
    auto setIB = [&](std::vector<MeshSkin>& sk, bool standard) {
        for (auto& ms : sk)
            for (auto& b : ms.bones)
                b.IB = standard ? InvAffine(Mul(b.BrMesh, b.OF)) : InvAffine(b.BrMesh);
    };

    // ---- sampled times + poses (evaluated on the ANIM tree) ----
    std::vector<float> ts = {0.f, clip.duration*0.25f, clip.duration*0.5f,
                             clip.duration*0.75f, std::max(0.f, clip.duration - 0.01f)};
    std::vector<std::vector<M4>> poses(ts.size());
    for (size_t i = 0; i < ts.size(); ++i) BuildPose(anodes, clip, ts[i], poses[i]);

    // Skinned vertex (mesh space) for mesh/bone palette J at a given pose.
    auto skinVertex = [](const MeshSkin& ms, const std::vector<M4>& J, unsigned vi,
                         const aiMesh* mesh, Vec3& out) {
        out = {0, 0, 0};
        for (auto& wb : ms.weights[vi]) {
            Vec3 p = Tfm(J[wb.first], {mesh->mVertices[vi].x, mesh->mVertices[vi].y, mesh->mVertices[vi].z});
            out = VAdd(out, VMul(p, wb.second));
        }
    };
    // Bone palette in MESH space: J = WmeshInv2 * Wa(b,t) * IB   (v2 design).
    auto buildJ = [&](const MeshSkin& ms, const std::vector<M4>& world, std::vector<M4>& J) {
        J.assign(ms.bones.size(), M4I());
        for (size_t b = 0; b < ms.bones.size(); ++b)
            J[b] = Mul(Mul(ms.WmeshInv2, world[ms.bones[b].animNode]), ms.bones[b].IB);
    };

    // ---- 1. t=0 displacement: |skinned(0) - rest| per formula (informational) ----
    // Adopted formula is N: IB = BrMesh^-1 (anchor = the model file's exported
    // rest pose, which is the pose the vertex data was authored in). Its t=0
    // displacement is the REAL pose difference between the model's exported
    // A-pose and the animation's first frame — expected to be non-zero for most
    // asset pairs (mixamo clips do not start exactly in the exported A-pose).
    printf("\n--- t=0 displacement |skinned(0) - rest| per formula ---\n");
    struct Res { float sum = 0.f, mx = 0.f; unsigned n = 0; };
    Res res[2];
    for (int f = 0; f < 2; ++f) {
        setIB(skins, f == 0);
        for (auto& ms : skins) {
            const aiMesh* mesh = model->mMeshes[ms.meshIdx];
            std::vector<M4> J; buildJ(ms, poses[0], J);
            for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
                Vec3 sv; skinVertex(ms, J, v, mesh, sv);
                float d = VDist(sv, {mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z});
                res[f].sum += d; if (d > res[f].mx) res[f].mx = d; res[f].n++;
            }
        }
        printf("  formula %c: avg=%.5f  max=%.5f  (char height ~1.638 units)\n",
               f == 0 ? 'S' : 'N', res[f].sum / (float)res[f].n, res[f].mx);
    }
    // Formula N is adopted. Formula S (IB = (BrMesh*OF)^-1) is the textbook
    // assimp convention; for elf.fbx it collapses to IB ~ identity and loses.
    // If S ever beats N, the OF convention assumption broke — re-check.
    bool nWins = res[1].sum < res[0].sum;
    printf("  -> adopted: N (IB = BrMesh^-1, anchor = model rest pose)%s\n",
           nWins ? "  [beats S as expected]" : "  !! S beat N — re-check the OF convention");
    setIB(skins, false);  // formula N

    // ---- 2. rigidity (scale-independent): d(t) = |skinned - joint| const over t ----
    printf("\n--- rigidity (single-bone verts, |v-joint| variation over time) ---\n");
    struct Rigid { unsigned mesh; unsigned vi; int bone; };
    std::vector<Rigid> rigids;
    for (size_t mi = 0; mi < skins.size(); ++mi) {
        const MeshSkin& ms = skins[mi];
        const aiMesh* mesh = model->mMeshes[ms.meshIdx];
        unsigned step = std::max(1u, (unsigned)(mesh->mNumVertices / 300));
        for (unsigned v = 0; v < mesh->mNumVertices && rigids.size() < 2400; v += step) {
            if (ms.weights[v].size() != 1 || ms.weights[v][0].second < 0.99f) continue;
            rigids.push_back({(unsigned)mi, v, ms.weights[v][0].first});
        }
    }
    bool rigOk = true;
    std::vector<float> mn(rigids.size(), 1e30f), mx(rigids.size(), 0.f);
    for (size_t ti = 0; ti < ts.size(); ++ti) {
        for (size_t ri = 0; ri < rigids.size(); ++ri) {
            const MeshSkin& ms = skins[rigids[ri].mesh];
            const aiMesh* mesh = model->mMeshes[ms.meshIdx];
            std::vector<M4> J; buildJ(ms, poses[ti], J);
            Vec3 o = T3(Mul(ms.WmeshInv2, poses[ti][ms.bones[rigids[ri].bone].animNode]));
            Vec3 sv = Tfm(J[rigids[ri].bone],
                          {mesh->mVertices[rigids[ri].vi].x, mesh->mVertices[rigids[ri].vi].y, mesh->mVertices[rigids[ri].vi].z});
            float d = VDist(sv, o);
            mn[ri] = std::min(mn[ri], d); mx[ri] = std::max(mx[ri], d);
        }
        float worst = 0.f;
        for (size_t ri = 0; ri < rigids.size(); ++ri)
            worst = std::max(worst, mx[ri] - mn[ri]);
        printf("  (sampled t=%.3fs) running max per-vertex variation = %.3e\n", ts[ti], worst);
    }
    float finalWorst = 0.f;
    for (size_t ri = 0; ri < rigids.size(); ++ri)
        finalWorst = std::max(finalWorst, mx[ri] - mn[ri]);
    rigOk = finalWorst < 1e-3f;
    printf("  rigidity: %s (max per-vertex variation = %.3e)\n",
           rigOk ? "PASS (constant over time)" : "FAIL (stretch)", finalWorst);

    // ---- 3. motion ----
    {
        float sum = 0.f; unsigned n = 0;
        for (auto& ms : skins) {
            const aiMesh* mesh = model->mMeshes[ms.meshIdx];
            std::vector<M4> J0, J2; buildJ(ms, poses[0], J0); buildJ(ms, poses[2], J2);
            for (unsigned v = 0; v < mesh->mNumVertices; v += 8) {
                Vec3 s0, s2; skinVertex(ms, J0, v, mesh, s0); skinVertex(ms, J2, v, mesh, s2);
                sum += VDist(s0, s2); n++;
            }
        }
        float avg = sum / (float)std::max(1u, n);
        printf("\n--- motion ---\n  avg |v(0.5d) - v(0)| = %.5f   %s\n", avg,
               avg > 1e-4f ? "ok (character moves)" : "NO MOTION");
    }

    // ---- 4. root report: Hips / Head / feet in MODEL root space over time ----
    printf("\n--- model-space joint positions over time ---\n");
    const char* watch[] = {"mixamorig:Hips", "mixamorig:Head", "mixamorig:LeftFoot", "mixamorig:RightFoot"};
    for (const char* wn : watch) {
        auto it = amap.find(wn);
        if (it == amap.end()) { printf("  %-22s (not in anim tree)\n", wn); continue; }
        printf("  %-22s rest: ", wn); PrintT3("", T3(Mul(D, anodes[it->second].world)));
        for (size_t ti = 0; ti < ts.size(); ++ti) {
            printf("    t=%7.3f  ", ts[ti]);
            PrintT3("", T3(Mul(D, poses[ti][it->second])));
        }
    }

    // ---- summary ----
    bool ofOk = maxCerr < 1e-4f;
    bool motionOk = true;
    {
        float sum = 0.f; unsigned n = 0;
        for (auto& ms : skins) {
            const aiMesh* mesh = model->mMeshes[ms.meshIdx];
            std::vector<M4> J0, J2; buildJ(ms, poses[0], J0); buildJ(ms, poses[2], J2);
            for (unsigned v = 0; v < mesh->mNumVertices; v += 32) {
                Vec3 s0, s2; skinVertex(ms, J0, v, mesh, s0); skinVertex(ms, J2, v, mesh, s2);
                sum += VDist(s0, s2); n++;
            }
        }
        motionOk = (sum / (float)std::max(1u, n)) > 1e-4f;
    }
    printf("--- summary ---\n");
    printf("  channel binding (anim tree, exact name):  PASS (asserted at startup)\n");
    printf("  BrMesh*OF ~ I (all bones):                %s  (max err %.2e)\n",
           ofOk ? "PASS" : "FAIL", maxCerr);
    printf("  rigidity (per-vertex variation < 1e-3):   %s\n", rigOk ? "PASS" : "FAIL");
    printf("  motion:                                   %s\n", motionOk ? "PASS" : "FAIL");
    printf("  t=0 |skinned-rest| (model-rest vs anim-start pose gap, informational):\n");
    printf("    adopted N: avg=%.5f max=%.5f\n", res[1].sum / (float)res[1].n, res[1].mx);
    printf("  engine design: eval on anim tree; J = WmeshInv*D*Wa*IB; IB = BrMesh^-1\n");
    return (ofOk && rigOk && motionOk) ? 0 : 1;
}
