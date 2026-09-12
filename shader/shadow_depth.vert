#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 3) in vec4 aBoneIndices;
layout(location = 4) in vec4 aBoneWeights;

uniform mat4 uLightSpaceMatrix;
uniform mat4 uModel;

// Skinning (must match material.vert): same J matrices from the Animator,
// so cast shadows track the animated pose.
uniform int  uSkinCount;
uniform mat4 uBoneMatrices[64];

void main() {
    vec3 pos = aPos;

    if (uSkinCount > 0) {
        mat4 skel = mat4(0.0);
        if (aBoneWeights.x > 0.0) skel += uBoneMatrices[int(aBoneIndices.x + 0.5)] * aBoneWeights.x;
        if (aBoneWeights.y > 0.0) skel += uBoneMatrices[int(aBoneIndices.y + 0.5)] * aBoneWeights.y;
        if (aBoneWeights.z > 0.0) skel += uBoneMatrices[int(aBoneIndices.z + 0.5)] * aBoneWeights.z;
        if (aBoneWeights.w > 0.0) skel += uBoneMatrices[int(aBoneIndices.w + 0.5)] * aBoneWeights.w;
        pos = (skel * vec4(aPos, 1.0)).xyz;
    }

    gl_Position = uLightSpaceMatrix * uModel * vec4(pos, 1.0);
}
