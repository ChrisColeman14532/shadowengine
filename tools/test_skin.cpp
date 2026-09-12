// Verify skinning math convention for elf.fbx.
// For vertices rigidly bound to a single bone, the bind-pose skinning
// transform must be identity in vertex space: P * v == v.
// Robust matrix code (no shared buffers).
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <functional>

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

static M4 Inv(const M4& M) {
    M4 I;
    float R[3][3];
    for (int r=0;r<3;r++) for(int c=0;c<3;c++) R[r][c]=M.m[r][c];
    float a=R[0][0],b=R[0][1],c=R[0][2];
    float d=R[1][0],e=R[1][1],f=R[1][2];
    float g=R[2][0],h=R[2][1],i=R[2][2];
    float det = a*(e*i-f*h) - b*(d*i-f*g) + c*(d*h-e*g);
    float id = 1.0f/det;
    float Ri[3][3];
    Ri[0][0]=(e*i-f*h)*id; Ri[0][1]=(c*h-b*i)*id; Ri[0][2]=(b*f-c*e)*id;
    Ri[1][0]=(f*g-d*i)*id; Ri[1][1]=(a*i-c*g)*id; Ri[1][2]=(c*d-a*f)*id;
    Ri[2][0]=(d*h-e*g)*id; Ri[2][1]=(b*g-a*h)*id; Ri[2][2]=(a*e-b*d)*id;
    float tx=M.m[0][3], ty=M.m[1][3], tz=M.m[2][3];
    for (int r=0;r<3;r++) for (int cc=0;cc<3;cc++) I.m[r][cc]=Ri[r][cc];
    I.m[0][3] = -(Ri[0][0]*tx + Ri[0][1]*ty + Ri[0][2]*tz);
    I.m[1][3] = -(Ri[1][0]*tx + Ri[1][1]*ty + Ri[1][2]*tz);
    I.m[2][3] = -(Ri[2][0]*tx + Ri[2][1]*ty + Ri[2][2]*tz);
    I.m[3][0]=0; I.m[3][1]=0; I.m[3][2]=0; I.m[3][3]=1;
    return I;
}

static void PrintM4(const char* l, const M4& M) {
    printf("%s\n", l);
    for (int r=0;r<4;r++)
        printf("    (%+.4f %+.4f %+.4f %+.4f)\n", M.m[r][0],M.m[r][1],M.m[r][2],M.m[r][3]);
}

static void Transform(const M4& M, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = M.m[0][0]*x + M.m[0][1]*y + M.m[0][2]*z + M.m[0][3];
    oy = M.m[1][0]*x + M.m[1][1]*y + M.m[1][2]*z + M.m[1][3];
    oz = M.m[2][0]*x + M.m[2][1]*y + M.m[2][2]*z + M.m[2][3];
}

struct FlatNode { std::string name; int parent; M4 local; M4 world; };

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    Assimp::Importer imp;
    const aiScene* s = imp.ReadFile(argv[1], aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (!s) { printf("LOAD FAILED\n"); return 1; }

    std::vector<FlatNode> nodes;
    std::function<void(const aiNode*, int)> walk = [&](const aiNode* n, int parent) {
        int idx = (int)nodes.size();
        FlatNode fn; fn.name = n->mName.C_Str(); fn.parent = parent; fn.local = FromAI(n->mTransformation);
        nodes.push_back(fn);
        for (unsigned i = 0; i < n->mNumChildren; ++i) walk(n->mChildren[i], idx);
    };
    walk(s->mRootNode, -1);
    for (size_t i = 0; i < nodes.size(); ++i)
        nodes[i].world = (nodes[i].parent >= 0) ? Mul(nodes[nodes[i].parent].world, nodes[i].local) : nodes[i].local;

    auto findNode = [&](const std::string& name) -> int {
        for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].name == name) return (int)i;
        return -1;
    };
    std::function<int(const aiNode*, int)> findMeshNode = [&](const aiNode* n, int meshIdx) -> int {
        for (unsigned mi = 0; mi < n->mNumMeshes; ++mi)
            if ((int)n->mMeshes[mi] == meshIdx) return findNode(n->mName.C_Str());
        for (unsigned i = 0; i < n->mNumChildren; ++i) {
            int r = findMeshNode(n->mChildren[i], meshIdx);
            if (r >= 0) return r;
        }
        return -1;
    };

    for (unsigned m = 0; m < s->mNumMeshes && m < 6; ++m) {
        const aiMesh* mesh = s->mMeshes[m];
        if (mesh->mNumBones == 0) continue;
        printf("=== mesh[%u] '%s' (%u verts, %u bones) ===\n", m, mesh->mName.C_Str(), mesh->mNumVertices, mesh->mNumBones);
        int meshNodeIdx = findMeshNode(s->mRootNode, (int)m);
        if (meshNodeIdx < 0) { printf("  (mesh node not found)\n"); continue; }
        const M4& Wmesh = nodes[meshNodeIdx].world;
        printf("  mesh node '%s'\n", nodes[meshNodeIdx].name.c_str());
        PrintM4("    Wmesh (mesh node world):", Wmesh);

        std::vector<std::pair<int,float>> dom(mesh->mNumVertices, {-1, 0.f});
        for (unsigned b = 0; b < mesh->mNumBones; ++b)
            for (unsigned w = 0; w < mesh->mBones[b]->mNumWeights; ++w) {
                const aiVertexWeight& vw = mesh->mBones[b]->mWeights[w];
                if (vw.mWeight > dom[vw.mVertexId].second)
                    dom[vw.mVertexId] = {(int)b, vw.mWeight};
            }
        std::vector<std::pair<unsigned,int>> cands; // (vertex, boneIdx)
        for (unsigned v = 0; v < mesh->mNumVertices && cands.size() < 6; ++v)
            if (dom[v].first >= 0 && dom[v].second >= 0.95f)
                cands.push_back({v, dom[v].first});
        if (cands.empty()) { printf("  (no rigid vertices)\n"); continue; }

        for (size_t ci = 0; ci < cands.size(); ++ci) {
            unsigned v = cands[ci].first;
            const aiBone* bone = mesh->mBones[cands[ci].second];
            int boneIdx = findNode(bone->mName.C_Str());
            if (boneIdx < 0) { printf("  bone '%s' not in tree!\n", bone->mName.C_Str()); continue; }
            const M4& BW = nodes[boneIdx].world;
            const M4 OF = FromAI(bone->mOffsetMatrix);
            M4 WmeshInv = Inv(Wmesh);
            M4 BW_mesh = Mul(WmeshInv, BW);

            M4 PA = Mul(BW, OF);        // boneWorld(root) * offset
            M4 PB = Mul(OF, BW);        // offset * boneWorld(root)
            M4 PC = Mul(BW_mesh, OF);   // boneWorld(meshspace) * offset
            M4 PD = Mul(OF, BW_mesh);   // offset * boneWorld(meshspace)

            float vx=mesh->mVertices[v].x, vy=mesh->mVertices[v].y, vz=mesh->mVertices[v].z;
            float r[4][3];
            Transform(PA, vx,vy,vz, r[0][0],r[0][1],r[0][2]);
            Transform(PB, vx,vy,vz, r[1][0],r[1][1],r[1][2]);
            Transform(PC, vx,vy,vz, r[2][0],r[2][1],r[2][2]);
            Transform(PD, vx,vy,vz, r[3][0],r[3][1],r[3][2]);
            auto dist = [](float x[3], float y[3]) {
                float dx=x[0]-y[0], dy=x[1]-y[1], dz=x[2]-y[2];
                return std::sqrt(dx*dx+dy*dy+dz*dz);
            };
            float v0[3] = {vx, vy, vz};
            const char* names[4] = {"A: BWroot*OF", "B: OF*BWroot", "C: BWmesh*OF", "D: OF*BWmesh"};
            printf("  v=(%+.4f,%+.4f,%+.4f) bone=%s\n", vx,vy,vz, bone->mName.C_Str());
            for (int i = 0; i < 4; ++i)
                printf("    %s: |P*v - v| = %.6f\n", names[i], dist(r[i], v0));
            if (ci == 0) {
                PrintM4("    BW (bone world, root space):", BW);
                PrintM4("    OF (offset):", OF);
            }
        }
    }
    return 0;
}
