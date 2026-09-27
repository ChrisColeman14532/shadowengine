#version 330 core

// Unity-style ground grid, drawn as thin filled quads.
//
// Each grid line is a 4-corner quad (two triangles) of constant pixel
// width. Geometry lives in "grid space": integer cell coordinates on the
// XZ plane. The grid is fixed at the world origin (±512 cells) so the
// lines stay locked to world coordinates.
//
// Line width is constant in screen pixels: each corner vertex is pushed
// perpendicular to the line's view-space direction by aSide * halfWidth,
// so the quad stays 1px wide at any distance. Filled triangles render as a
// single solid anti-aliased line (no double-edge artifact).
layout(location = 0) in vec2 aPos;    // cell coordinates (x, z)
layout(location = 1) in vec2 aDir;    // line direction in grid space: (1,0) or (0,1)
layout(location = 2) in float aSide;  // +1 or -1 (which edge of the line band)
layout(location = 3) in float aMajor; // 1.0 = major line (every 10 cells)

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uOffset;       // world-space grid offset (cell-snapped; y = height)
uniform float uSpacing;     // world distance between grid lines (1.0 = one cube)
uniform vec3 uCameraPos;    // world-space camera position (for distance fade)
uniform vec3 uMinorColor;
uniform vec3 uMajorColor;
uniform float uWorldPerPixel; // world units per screen pixel at unit depth
uniform float uLineHalfPx;    // half line width in pixels
uniform vec3 uFogColor;       // color to fade the lines into (sky horizon color)
uniform float uFadeExp;       // exponential fog density for the line color

out vec3 vColor;
out float vAlpha;

void main() {
    // World position of this grid vertex
    // aPos is vec2 (cellX, cellZ)
    vec3 world = vec3(aPos.x * uSpacing + uOffset.x,
                      uOffset.y,
                      aPos.y * uSpacing + uOffset.z);

    // Distance fade: lines melt away toward the horizon.
    //
    // The line color blends toward the horizon (fog) color with distance,
    // mirroring the exponential fog the ground receives. Far lines become
    // indistinguishable from the sky they sit in, so the grid appears to
    // end softly at the horizon instead of fading as a visible band. The
    // alpha follows the same blend so the lines thin out as they match.
    float dist = distance(world.xz, uCameraPos.xz);
    float fogF = 1.0 - exp(-uFadeExp * dist);
    vColor = mix(mix(uMinorColor, uMajorColor, aMajor), uFogColor, fogF);
    vAlpha = 1.0 - fogF;

    vec4 viewPos = uView * vec4(world, 1.0);

    // Constant screen-space width: offset along the screen-space
    // perpendicular of the line's view-space direction. After the
    // perspective divide this lands exactly uLineHalfPx pixels from the
    // line's center at any depth.
    vec3 vdir = (uView * vec4(aDir.x, 0.0, aDir.y, 0.0)).xyz;
    vec2 d2 = vdir.xy;
    float dl = length(d2);
    if (dl > 1e-5) {
        vec2 perp = vec2(-d2.y, d2.x) / dl;
        float depth = max(-viewPos.z, 1e-4);
        viewPos.xy += perp * (aSide * uLineHalfPx * uWorldPerPixel * depth);
    }

    gl_Position = uProjection * viewPos;
}
