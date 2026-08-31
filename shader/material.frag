#version 330 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;
in vec3 vViewDir;

out vec4 FragColor;

// ── Shadow PCF ────────────────────────────────────────────────────
float ShadowCalculation(vec4 fragPosLightSpace, sampler2D depthMap,
                        mat4 lightSpaceMatrix, vec3 lightDir,
                        vec3 worldPos, vec3 normal, float near, float far) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    float bias = max(0.005 * (1.0 - max(dot(normal, lightDir), 0.0)), 0.005);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(2048.0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(depthMap, projCoords.xy + vec2(float(x), float(y)) * texelSize).r;
            shadow += (projCoords.z - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;
    return shadow;
}

// ── Lighting ──────────────────────────────────────────────────────
vec3 ComputeLighting(vec3 baseColor, vec3 normal, vec3 worldPos,
                     vec3 lightDir, vec3 viewDir,
                     float metallic, float roughness, float ao,
                     vec3 emissiveColor) {
    vec3 ambient = 0.45 * baseColor * ao;

    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = NdotL * baseColor;

    vec3 halfDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float spec = pow(NdotH, mix(128.0, 8.0, roughness)) * metallic;
    vec3 specular = spec * vec3(1.0, 0.98, 0.95) * (0.3 + 0.7 * metallic);

    return (ambient + diffuse + specular) + emissiveColor;
}

// ── Uniforms ──────────────────────────────────────────────────────
uniform int    uHasShadowMap;
uniform vec3   uBaseColor;
uniform vec3   uEmissiveColor;
uniform float  uMetallic;
uniform float  uRoughness;
uniform float  uAO;

uniform sampler2D uDiffuseTex;
uniform sampler2D uNormalTex;
uniform int       uHasDiffuse;
uniform int       uHasNormal;

uniform sampler2D uShadowMap;
uniform mat4      uLightSpaceMatrix;
uniform vec3      uLightDirection;
uniform float     uShadowNear;
uniform float     uShadowFar;

void main() {
    vec3 baseColor = uBaseColor;
    vec3 normal = normalize(vNormal);

    if (uHasDiffuse == 1) {
        baseColor *= texture(uDiffuseTex, vUV).rgb;
    }

    vec3 lightDir = normalize(uLightDirection);
    vec3 viewDir = normalize(vViewDir);

    vec4 fragPosLightSpace = uLightSpaceMatrix * vec4(vWorldPos, 1.0);

    float shadow = 0.0;
    vec3 litColor = ComputeLighting(baseColor, normal, vWorldPos,
                                    lightDir, viewDir,
                                    uMetallic, uRoughness, uAO, uEmissiveColor);

    if (uHasShadowMap == 1) {
        shadow = ShadowCalculation(fragPosLightSpace, uShadowMap,
                                   uLightSpaceMatrix, lightDir,
                                   vWorldPos, normal,
                                   uShadowNear, uShadowFar);
        vec3 shadowColor = litColor * mix(1.0, 0.5, shadow);
        FragColor = vec4(shadowColor, 1.0);
    } else {
        FragColor = vec4(litColor, 1.0);
    }
}
