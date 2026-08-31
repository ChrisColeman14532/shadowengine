#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vDirection;

void main() {
    mat4 viewNoTranslate = uView;
    viewNoTranslate[3] = vec4(0.0, 0.0, 0.0, 1.0);

    vDirection = aPos;
    gl_Position = (uProjection * viewNoTranslate * vec4(aPos, 1.0)).xyww;
}
