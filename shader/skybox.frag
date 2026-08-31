#version 330 core
in vec3 vDirection;
out vec4 FragColor;

void main() {
    vec3 dir = normalize(vDirection);

    float sunAngle = 0.4;
    vec3 sunDir = normalize(vec3(0.8, sin(sunAngle), 0.6));

    vec3 zenith   = vec3(0.15, 0.30, 0.85);
    vec3 midday   = vec3(0.40, 0.65, 0.95);
    vec3 horizon  = vec3(0.85, 0.65, 0.45);
    vec3 below    = vec3(0.50, 0.35, 0.25);

    float y = dir.y;
    vec3 skyColor;

    if (y > 0.1) {
        float t = smoothstep(0.1, 0.8, y);
        skyColor = mix(horizon, zenith, t);
        skyColor = mix(skyColor, midday, smoothstep(0.0, 0.4, y));
    } else if (y > -0.05) {
        skyColor = horizon;
    } else {
        skyColor = mix(horizon, below, smoothstep(-0.05, -0.5, y));
    }

    float sunDot = max(dot(dir, sunDir), 0.0);
    float sun    = pow(sunDot, 500.0) * 2.0;
    float glow   = pow(sunDot, 20.0) * 0.6;
    float halo   = pow(sunDot, 3.0) * 0.2;

    vec3 sunColor = vec3(1.0, 0.95, 0.8);
    skyColor += sunColor * (sun + glow + halo);

    float horizonGlow = exp(-abs(y) * 4.0) * 0.3;
    skyColor += vec3(1.0, 0.7, 0.4) * horizonGlow;

    float horizonFactor = exp(-abs(y) * 3.0);
    skyColor = mix(skyColor, horizon, horizonFactor * 0.4);

    FragColor = vec4(skyColor, 1.0);
}
