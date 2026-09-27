#version 330 core

// Ground plane fragment: a solid warm-gray surface that fades into the
// sky's horizon color with distance (exponential fog), so the ground
// melts into the backdrop at the horizon with no visible seam. The grid
// lines are drawn on top and use the same fog color, so they dissolve
// into the sky along with the ground.
in vec3 vWorldPos;
uniform vec3 uCameraPos;
uniform float uFogDensity;
uniform vec3 uGroundColor;  // solid ground color (near)
uniform vec3 uFogColor;     // sky horizon color (far) — must match skybox

out vec4 FragColor;

void main() {
    float dist = distance(vWorldPos.xz, uCameraPos.xz);
    float fogF = 1.0 - exp(-uFogDensity * dist);
    vec3 col = mix(uGroundColor, uFogColor, fogF);
    FragColor = vec4(col, 1.0);
}
