#version 330 core

// Unity-style flat studio sky.
//
// The reference look is a flat warm-gray backdrop: the sky is a single
// color (with only a very soft darkening toward the top) and the ground
// plane fades into exactly that same color at the horizon, so there is
// no visible seam between ground and sky.
in vec3 vDirection;
out vec4 FragColor;

// Flat backdrop colors (linear-ish, matched by the ground plane's fog).
vec3 uSkyColor = vec3(0.4157, 0.3882, 0.3725);  // (106, 99, 95) / 255
vec3 uSkyTop   = vec3(0.365, 0.345, 0.33);      // slightly darker at zenith

void main() {
    vec3 dir = normalize(vDirection);

    // Almost flat: only a gentle darkening toward the zenith.
    float t = smoothstep(0.0, 0.6, max(dir.y, 0.0));
    vec3 sky = mix(uSkyColor, uSkyTop, t);

    // Below the horizon the skybox is the "below ground" color; the ground
    // plane covers this region in the normal view, so just match it to the
    // horizon color to avoid any band if it ever shows through.
    if (dir.y < 0.0) sky = uSkyColor;

    FragColor = vec4(sky, 1.0);
}
