#version 330 core

// Ground plane vertex: a large quad on the y=0 plane that follows the
// camera (cell-snapped offset so the grid drawn on top stays locked to
// world coordinates). The fog blend is computed per-vertex here and
// interpolated across the quad.
layout(location = 0) in vec3 aPos;   // local plane coords (x, 0, z), centered on origin

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uOffset;       // camera-following world offset (x, y, z)
uniform vec3 uCameraPos;
uniform float uFogDensity;  // exponential fog density (matches sky horizon blend)

out vec3 vWorldPos;

void main() {
    vec3 world = aPos + uOffset;
    vWorldPos = world;
    gl_Position = uProjection * uView * vec4(world, 1.0);
}
