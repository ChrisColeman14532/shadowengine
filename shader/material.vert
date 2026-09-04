#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vNormal;
out vec2 vUV;
out vec3 vWorldPos;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
    vWorldPos = (uModel * vec4(aPos, 1.0)).xyz;
}
