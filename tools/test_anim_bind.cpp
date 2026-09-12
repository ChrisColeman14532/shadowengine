// Verify that an animation FBX's channels can bind by name to a model FBX's
// node tree, and compare rest-pose world transforms of the shared bones.
//
// The engine binds animation tracks to model nodes by EXACT node name
// (including assimp's decomposed wrapper names like
// 'mixamorig:Hips_$AssimpFbx$_Translation'). A channel whose node does not
// exist in the model silently animates nothing, so this tool checks every
// channel against the model tree, flags misses, and prints where the two
// files disagree in rest pose (root rotations/scales, Hips placement).
//
// Both files are loaded with the SAME importer flags the engine uses
// (model: LoadFBX, anim: LoadFBXAnimation) so the node names seen here are
// exactly the names the engine will bind against.
//
// Build: tools\build_test_anim_bind.bat
// Usage: test_anim_bind <model.fbx> <anim.fbx>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <unordered_map>

struct M4 { float m[4][4]; };

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

static void PrintM4(const char* label, const M4& M) {
    printf("%s\n", label);
    for (int r = 0; r < 4; ++r)
        printf("    (%+.4f %+.4f %+.4f %+.4f)\n", M.m[r][0], M.m[r][1], M.m[r][2], M.m[r][3]);
}

static void PrintT(const M4& M, char* buf, size_t n) {
    snprintf(buf, n, "(%+.4f, %+.4f, %+.4f)", M.m[0][3], M.m[1][3], M.m[2][3]);
}

// Scale carried in a matrix column (column length).
static float ColLen(const M4& M, int c) {
    return std::sqrt(M.m[0][c]*M.m[0][c] + M.m[1][c]*M.m[1][c] + M.m[2][c]*M.m[2][c]);
}

struct FlatNode { std::string name; int parent = -1; M4 local, world; };

static void Flatten(const aiNode* n, int parent, std::vector<FlatNode>& out) {
    int idx = (int)out.size();
    FlatNode fn; fn.name = n->mName.C_Str(); fn.parent = parent; fn.local = FromAI(n->mTransformation);
    out.push_back(fn);
    for (unsigned i = 0; i < n->mNumChildren; ++i) Flatten(n->mChildren[i], idx, out);
}

static void ComputeWorlds(std::vector<FlatNode>& nodes) {
    for (size_t i = 0; i < nodes.size(); ++i)
        nodes[i].world = (nodes[i].parent >= 0)
            ? Mul(nodes[nodes[i].parent].world, nodes[i].local)
            : nodes[i].local;
}

static std::unordered_map<std::string, int> NameMap(const std::vector<FlatNode>& nodes) {
    std::unordered_map<std::string, int> m;
    for (size_t i = 0; i < nodes.size(); ++i) m[nodes[i].name] = (int)i;
    return m;
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: test_anim_bind <model.fbx> <anim.fbx>\n"); return 1; }

    Assimp::Importer mimp;
    const aiScene* model = mimp.ReadFile(argv[1], aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (!model) { printf("LOAD FAILED (model): %s\n", mimp.GetErrorString()); return 1; }
    Assimp::Importer aimp;
    const aiScene* anim = aimp.ReadFile(argv[2], aiProcess_Triangulate | aiProcess_GenUVCoords);
    if (!anim) { printf("LOAD FAILED (anim): %s\n", aimp.GetErrorString()); return 1; }

    std::vector<FlatNode> mnodes, anodes;
    Flatten(model->mRootNode, -1, mnodes);
    Flatten(anim->mRootNode, -1, anodes);
    ComputeWorlds(mnodes);
    ComputeWorlds(anodes);
    auto mname = NameMap(mnodes);
    auto aname = NameMap(anodes);

    // All channel names + key counts across all clips of the anim file.
    std::set<std::string> channelNames;
    std::unordered_map<std::string, int> keyPos, keyRot, keyScale;
    for (unsigned a = 0; a < anim->mNumAnimations; ++a)
        for (unsigned c = 0; c < anim->mAnimations[a]->mNumChannels; ++c) {
            const aiNodeAnim* ch = anim->mAnimations[a]->mChannels[c];
            const std::string nm = ch->mNodeName.C_Str();
            channelNames.insert(nm);
            keyPos[nm] += (int)ch->mNumPositionKeys;
            keyRot[nm] += (int)ch->mNumRotationKeys;
            keyScale[nm] += (int)ch->mNumScalingKeys;
        }

    printf("=== %s  (nodes=%zu, meshes=%u)  +  %s  (nodes=%zu, clips=%u, channels=%zu) ===\n",
           argv[1], mnodes.size(), model->mNumMeshes,
           argv[2], anodes.size(), anim->mNumAnimations, channelNames.size());

    // ---- 1. channel -> model node binding ----
    printf("--- channel -> model node binding ---\n");
    int bound = 0, missed = 0;
    std::vector<std::string> misses;
    for (unsigned a = 0; a < anim->mNumAnimations; ++a) {
        const aiAnimation* an = anim->mAnimations[a];
        for (unsigned c = 0; c < an->mNumChannels; ++c) {
            const std::string nm = an->mChannels[c]->mNodeName.C_Str();
            if (mname.count(nm)) { ++bound; }
            else {
                ++missed;
                std::string base = nm;
                size_t p = base.find("_$AssimpFbx$_");
                if (p != std::string::npos) base.erase(p);
                misses.push_back(mname.count(base)
                    ? nm + "   (bare bone '" + base + "' exists in model)"
                    : nm + "   (no matching node in model)");
            }
        }
    }
    printf("  %d/%d channels bind; %d do not:\n", bound, bound + missed, missed);
    for (const auto& s : misses) printf("  MISS '%s'\n", s.c_str());
    printf("\n");

    // ---- 2. per-bone coverage: does each bone have nodes AND tracks to move? ----
    printf("--- bone coverage  (slot: node,track  Y=present) ---\n");
    std::vector<std::string> bones;
    for (unsigned m = 0; m < model->mNumMeshes; ++m)
        for (unsigned b = 0; b < model->mMeshes[m]->mNumBones; ++b) {
            std::string nm = model->mMeshes[m]->mBones[b]->mName.C_Str();
            if (std::find(bones.begin(), bones.end(), nm) == bones.end()) bones.push_back(nm);
        }
    std::sort(bones.begin(), bones.end());
    const char* suffix[4] = {"", "_$AssimpFbx$_Translation", "_$AssimpFbx$_PreRotation", "_$AssimpFbx$_Rotation"};
    const char* label[4] = {"bare", "T", "PreR", "R"};
    int unboundSlots = 0;
    std::vector<std::string> noRot, noPos;
    for (const auto& bn : bones) {
        printf("%-22s", bn.c_str());
        bool rot = false, pos = false;
        for (int s = 0; s < 4; ++s) {
            std::string full = bn + suffix[s];
            bool node = mname.count(full) > 0;
            bool track = channelNames.count(full) > 0;
            printf("  %s:%c%c", label[s], node ? 'Y' : '-', track ? 'Y' : '-');
            if (track && !node) ++unboundSlots;
            if (keyRot.count(full) && keyRot[full] > 0) rot = true;
            if (keyPos.count(full) && keyPos[full] > 0 && suffix[s] != "") pos = true;
        }
        if (!rot) noRot.push_back(bn);
        if (!pos) noPos.push_back(bn);
        printf("\n");
    }
    printf("  slots with a track but NO model node: %d\n", unboundSlots);
    printf("  bones with NO rotation keys reaching them:");
    for (const auto& b : noRot) printf("  %s", b.c_str());
    printf("\n");
    printf("  bones with NO position keys reaching them:");
    for (const auto& b : noPos) printf("  %s", b.c_str());
    printf("\n\n");

    // ---- 3. rest-pose comparison (each file in its own root space) ----
    printf("--- rest pose world of key bones (model file vs anim file) ---\n");
    const char* keys[] = {
        "mixamorig:Hips", "mixamorig:Spine", "mixamorig:Spine1", "mixamorig:Spine2",
        "mixamorig:Neck", "mixamorig:Head",
        "mixamorig:LeftShoulder", "mixamorig:RightShoulder",
        "mixamorig:LeftArm", "mixamorig:RightArm",
        "mixamorig:LeftUpLeg", "mixamorig:RightUpLeg",
        "mixamorig:LeftLeg", "mixamorig:RightLeg",
        "mixamorig:LeftFoot", "mixamorig:RightFoot"
    };
    printf("%-24s %-26s %-26s %s\n", "bone", "model t", "anim t", "scale (model/anim)");
    for (const char* k : keys) {
        auto mi = mname.find(k), ai = aname.find(k);
        if (mi == mname.end() && ai == aname.end()) continue;
        char tm[48], ta[48];
        if (mi != mname.end()) PrintT(mnodes[mi->second].world, tm, sizeof tm);
        else snprintf(tm, sizeof tm, "          (absent)");
        if (ai != aname.end()) PrintT(anodes[ai->second].world, ta, sizeof ta);
        else snprintf(ta, sizeof ta, "          (absent)");
        float sm = (mi != mname.end()) ? ColLen(mnodes[mi->second].world, 0) : -1.f;
        float sa = (ai != aname.end()) ? ColLen(anodes[ai->second].world, 0) : -1.f;
        printf("%-24s %-26s %-26s (%.3f / %.3f)\n", k, tm, ta, sm, sa);
    }

    printf("\n--- root / Hips local transforms (where the files structurally differ) ---\n");
    PrintM4("model RootNode local:", mnodes[0].local);
    PrintM4("anim  RootNode local:", anodes[0].local);
    auto mh = mname.find("mixamorig:Hips");
    auto ah = aname.find("mixamorig:Hips");
    if (mh != mname.end()) PrintM4("model mixamorig:Hips local:", mnodes[mh->second].local);
    if (ah != aname.end()) PrintM4("anim  mixamorig:Hips local:", anodes[ah->second].local);

    return 0;
}
