#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aBoneIndices;
layout(location = 4) in vec4 aBoneWeights;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

// Skinning: uSkinCount > 0 enables bone blending for this draw.
// uBoneMatrices[i] = J(bone i, t) — maps mesh-local vertex positions to
// skinned mesh-local positions (computed by the engine's Animator).
uniform int  uSkinCount;
uniform mat4 uBoneMatrices[64];

out vec3 vNormal;
out vec2 vUV;
out vec3 vWorldPos;

void main() {
    vec3 pos = aPos;
    vec3 nrm = aNormal;

    if (uSkinCount > 0) {
        mat4 skel = mat4(0.0);
        if (aBoneWeights.x > 0.0) skel += uBoneMatrices[int(aBoneIndices.x + 0.5)] * aBoneWeights.x;
        if (aBoneWeights.y > 0.0) skel += uBoneMatrices[int(aBoneIndices.y + 0.5)] * aBoneWeights.y;
        if (aBoneWeights.z > 0.0) skel += uBoneMatrices[int(aBoneIndices.z + 0.5)] * aBoneWeights.z;
        if (aBoneWeights.w > 0.0) skel += uBoneMatrices[int(aBoneIndices.w + 0.5)] * aBoneWeights.w;
        pos = (skel * vec4(aPos, 1.0)).xyz;
        nrm = mat3(skel) * aNormal;
    }

    gl_Position = uProjection * uView * uModel * vec4(pos, 1.0);
    vNormal = mat3(uModel) * nrm;
    vUV = aUV;
    vWorldPos = (uModel * vec4(pos, 1.0)).xyz;
}
