// Standalone FBX structure dumper (assimp).
// Usage: dump_fbx <file.fbx>
//
// aiMatrix4x4 layout (verified against this assimp build's headers/inl):
//   row 0 = a1..a4, row 1 = b1..b4, row 2 = c1..c4, row 3 = d1..d4
//   M[i][j] (row i, col j). Column-vector convention (v' = M*v):
//   translation = (a4, b4, c4) [M[0][3], M[1][3], M[2][3]]
//   last row = (0,0,0,1).
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

static void PrintMat4(const char* label, const aiMatrix4x4& M) {
    const float R[4][4] = {
        {M.a1, M.a2, M.a3, M.a4},
        {M.b1, M.b2, M.b3, M.b4},
        {M.c1, M.c2, M.c3, M.c4},
        {M.d1, M.d2, M.d3, M.d4}
    };
    printf("%s\n", label);
    for (int r = 0; r < 4; ++r)
        printf("    (%+.4f %+.4f %+.4f %+.4f)\n", R[r][0], R[r][1], R[r][2], R[r][3]);
}

static aiMatrix4x4 MMul(const aiMatrix4x4& A, const aiMatrix4x4& B) {
    const float Ar[4][4] = {{A.a1,A.a2,A.a3,A.a4},{A.b1,A.b2,A.b3,A.b4},{A.c1,A.c2,A.c3,A.c4},{A.d1,A.d2,A.d3,A.d4}};
    const float Br[4][4] = {{B.a1,B.a2,B.a3,B.a4},{B.b1,B.b2,B.b3,B.b4},{B.c1,B.c2,B.c3,B.c4},{B.d1,B.d2,B.d3,B.d4}};
    aiMatrix4x4 W;
    float tmp[4][4];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float acc = 0.f;
            for (int k = 0; k < 4; ++k) acc += Ar[r][k] * Br[k][c];
            tmp[r][c] = acc;
        }
    W.a1=tmp[0][0]; W.a2=tmp[0][1]; W.a3=tmp[0][2]; W.a4=tmp[0][3];
    W.b1=tmp[1][0]; W.b2=tmp[1][1]; W.b3=tmp[1][2]; W.b4=tmp[1][3];
    W.c1=tmp[2][0]; W.c2=tmp[2][1]; W.c3=tmp[2][2]; W.c4=tmp[2][3];
    W.d1=tmp[3][0]; W.d2=tmp[3][1]; W.d3=tmp[3][2]; W.d4=tmp[3][3];
    return W;
}

static bool IsBoneName(const std::string& n) { return n.rfind("mixamorig:", 0) == 0; }

static void PrintNode(const aiNode* n, int depth, const aiMatrix4x4* parentWorld, bool verbose) {
    const aiMatrix4x4& L = n->mTransformation;
    aiMatrix4x4 W = parentWorld ? MMul(*parentWorld, L) : L;
    for (int i = 0; i < depth; ++i) fputs("  ", stdout);
    printf("node '%s'  t=(%.4f,%.4f,%.4f)  diag=(%.4f,%.4f,%.4f)%s%s\n",
           n->mName.C_Str(), L.a4, L.b4, L.c4, L.a1, L.b2, L.c3,
           verbose && IsBoneName(n->mName.C_Str()) ? "  [BONE]" : "",
           n->mNumMeshes > 0 ? "  [HAS_MESH]" : "");
    if (verbose && IsBoneName(n->mName.C_Str())) {
        PrintMat4("    world:", W);
    }
    if (verbose && n->mNumMeshes > 0) {
        PrintMat4("    local:", L);
        PrintMat4("    world:", W);
    }
    for (unsigned i = 0; i < n->mNumMeshes; ++i) printf("    mesh[%u]\n", n->mMeshes[i]);
    for (unsigned i = 0; i < n->mNumChildren; ++i) PrintNode(n->mChildren[i], depth + 1, &W, verbose);
}

static void PrintMeshBones(const aiMesh* mesh) {
    for (unsigned b = 0; b < mesh->mNumBones; ++b) {
        const aiBone* bone = mesh->mBones[b];
        printf("      bone '%s' (%u vertex weights)\n",
               bone->mName.C_Str(), bone->mNumWeights);
    }
    if (mesh->mNumBones > 0 && mesh->mNumVertices > 0) {
        std::vector<int> perVert(mesh->mNumVertices, 0);
        for (unsigned b = 0; b < mesh->mNumBones; ++b)
            for (unsigned w = 0; w < mesh->mBones[b]->mNumWeights; ++w)
                ++perVert[mesh->mBones[b]->mWeights[w].mVertexId];
        int maxW = 0, sumW = 0;
        for (int c : perVert) { maxW = std::max(maxW, c); sumW += c; }
        printf("      per-vertex weights: max=%d  avg=%.2f\n",
               maxW, (float)sumW / (float)mesh->mNumVertices);
    }
}

static void PrintMeshBounds(const aiMesh* mesh) {
    if (mesh->mNumVertices == 0) return;
    float mnx=1e30f, mny=1e30f, mnz=1e30f, mxx=-1e30f, mxy=-1e30f, mxz=-1e30f;
    for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
        const aiVector3D& p = mesh->mVertices[v];
        mnx=std::min(mnx,p.x); mny=std::min(mny,p.y); mnz=std::min(mnz,p.z);
        mxx=std::max(mxx,p.x); mxy=std::max(mxy,p.y); mxz=std::max(mxz,p.z);
    }
    printf("      bounds: (%.3f,%.3f,%.3f)..(%.3f,%.3f,%.3f)\n", mnx,mny,mnz, mxx,mxy,mxz);
    const aiVector3D& p0 = mesh->mVertices[0];
    printf("      v0=(%.3f,%.3f,%.3f)\n", p0.x, p0.y, p0.z);
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: dump_fbx <file.fbx>\n"); return 1; }
    bool verbose = argc > 2 && std::string(argv[2]) == "-v";

    Assimp::Importer imp;
    const aiScene* s = imp.ReadFile(argv[1], aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (!s) { printf("LOAD FAILED: %s\n", imp.GetErrorString()); return 1; }

    printf("=== %s ===\n", argv[1]);
    printf("flags=0x%x  meshes=%u  materials=%u  textures=%u  animations=%u\n",
           s->mFlags, s->mNumMeshes, s->mNumMaterials, s->mNumTextures, s->mNumAnimations);

    printf("--- node hierarchy ---\n");
    if (s->mRootNode) PrintNode(s->mRootNode, 0, nullptr, verbose);

    for (unsigned m = 0; m < s->mNumMeshes; ++m) {
        const aiMesh* mesh = s->mMeshes[m];
        printf("--- mesh[%u] '%s': %u verts, %u faces, bones=%u, matIdx=%u, uv0=%d normals=%d\n",
               m, mesh->mName.C_Str(), mesh->mNumVertices, mesh->mNumFaces,
               mesh->mNumBones, mesh->mMaterialIndex,
               mesh->mTextureCoords[0] ? 1 : 0, mesh->HasNormals());
        PrintMeshBounds(mesh);
        if (mesh->mNumBones > 0) {
            printf("      bones:\n");
            PrintMeshBones(mesh);
        }
    }

    for (unsigned a = 0; a < s->mNumAnimations; ++a) {
        const aiAnimation* an = s->mAnimations[a];
        float tps = (float)(an->mTicksPerSecond > 0.0 ? an->mTicksPerSecond : 30.0);
        float dur = tps > 0.f ? (float)(an->mDuration / an->mTicksPerSecond) : 0.f;
        printf("--- animation[%u] '%s': tps=%.3f duration=%.3fs channels=%u\n",
               a, an->mName.C_Str(), tps, dur, an->mNumChannels);
        for (unsigned c = 0; c < an->mNumChannels; ++c) {
            const aiNodeAnim* ch = an->mChannels[c];
            printf("      ch[%u] node='%s'  posKeys=%u rotKeys=%u scaleKeys=%u\n",
                   c, ch->mNodeName.C_Str(),
                   ch->mNumPositionKeys, ch->mNumRotationKeys, ch->mNumScalingKeys);
        }
    }
    return 0;
}
