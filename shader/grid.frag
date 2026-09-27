#version 330 core

// Grid line fragment: flat color with distance-fade alpha (blended in).
in vec3 vColor;
in float vAlpha;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, vAlpha);
}
