#version 330 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;

out vec4 FragColor;

// ── Shadow PCF ────────────────────────────────────────────────────
// `bias` is computed per-fragment in main() (see the shadow-bias block
// there): a slope-scaled term plus a texel-depth term that scales with the
// shadow map's world-space texel size, so flat surfaces never shadow
// themselves regardless of scene scale.
float ShadowCalculation(vec4 fragPosLightSpace, sampler2D depthMap,
                        mat4 lightSpaceMatrix, float bias,
                        float shadowMapSize) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    // PCF with configurable resolution (avoids hardcoded 2048)
    vec2 texelSize = 1.0 / vec2(shadowMapSize);
    float shadow = 0.0;
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
uniform int    uHasNormalMap;
uniform vec3   uBaseColor;
uniform vec3   uEmissiveColor;
uniform float  uMetallic;
uniform float  uRoughness;
uniform float  uAO;

uniform sampler2D uDiffuseTex;
uniform sampler2D uNormalTex;
uniform int       uHasDiffuse;

uniform sampler2D uShadowMap;
uniform mat4      uLightSpaceMatrix;
uniform vec3      uLightDirection;
uniform float     uShadowNear;
uniform float     uShadowFar;
uniform float     uShadowMapSize;  // resolution (e.g., 2048.0)
uniform vec3      uCameraPos;      // world-space camera position
uniform vec3      uLightRight;     // world-space right of the light view
uniform vec3      uLightUp;        // world-space up of the light view
uniform vec2      uShadowTexelWorld; // world units per shadow texel (light X, Y)

void main() {
    vec3 baseColor = uBaseColor;
    vec3 normal = normalize(vNormal);

    // Apply diffuse texture if present
    if (uHasDiffuse == 1) {
        baseColor *= texture(uDiffuseTex, vUV).rgb;
    }

    // Apply normal map if present (using tangent frame from UV derivatives)
    if (uHasNormalMap == 1) {
        // Compute tangent space basis from screen-space UV derivatives.
        // Solves the linear system:
        //   dp1 = T * duv1.x + B * duv1.y
        //   dp2 = T * duv2.x + B * duv2.y
        // where dp = position derivative, duv = UV derivative
        vec3 dp1 = dFdx(vWorldPos);
        vec2 duv1 = dFdx(vUV);
        vec3 dp2 = dFdy(vWorldPos);
        vec2 duv2 = dFdy(vUV);

        float det = duv1.x * duv2.y - duv2.x * duv1.y;
        float f = 1.0 / det;

        vec3 T = normalize((duv2.y * dp1 - duv1.y * dp2) * f);
        vec3 B = normalize((duv1.x * dp2 - duv2.x * dp1) * f);

        // Build TBN matrix (tangent, bitangent, normal)
        mat3 TBN = mat3(T, B, normal);

        // Sample normal map and transform from tangent to world space
        vec3 normalMap = texture(uNormalTex, vUV).rgb * 2.0 - 1.0;
        normal = normalize(TBN * normalMap);
    }

    vec3 lightDir = normalize(uLightDirection);
    // World-space view direction from the fragment toward the camera
    vec3 viewDir = normalize(uCameraPos - vWorldPos);

    vec4 fragPosLightSpace = uLightSpaceMatrix * vec4(vWorldPos, 1.0);

    // ── Shadow bias (two terms) ────────────────────────────────
    // 1) Slope-scaled term: shrink bias where the surface is nearly
    //    parallel to the light to limit peter-panning, with a small floor
    //    to avoid acne on surfaces facing the light.
    float slopeBias = max(0.005 * (1.0 - max(dot(normal, lightDir), 0.0)),
                          0.0005);

    // 2) Texel-depth term: the shadow map stores ONE depth per texel
    //    (sampled at the texel's sample point). If this surface's
    //    light-space depth varies across one texel by more than the bias,
    //    the surface compares against its OWN binned depth and shadows
    //    itself in a repeating pattern — the "swirl" moire that appears
    //    on large flat planes once the shadow map's texels cover enough
    //    world distance. Bias by the actual depth variation across one
    //    texel for this surface's normal. Uses the GEOMETRIC normal:
    //    the depth gradient follows the real surface, not the shaded one.
    vec3 geoNormal = normalize(vNormal);
    vec3 dirTan = lightDir - dot(lightDir, geoNormal) * geoNormal;
    float depthRange = max(uShadowFar - uShadowNear, 1.0e-4);
    float texelBias = (uShadowTexelWorld.x * abs(dot(uLightRight, dirTan))
                      + uShadowTexelWorld.y * abs(dot(uLightUp,   dirTan)))
                      / depthRange;

    float bias = max(slopeBias, texelBias) + 0.0005;

    float shadow = 0.0;
    vec3 litColor = ComputeLighting(baseColor, normal, vWorldPos,
                                    lightDir, viewDir,
                                    uMetallic, uRoughness, uAO, uEmissiveColor);

    if (uHasShadowMap == 1) {
        shadow = ShadowCalculation(fragPosLightSpace, uShadowMap,
                                   uLightSpaceMatrix, bias,
                                   uShadowMapSize);
        vec3 shadowColor = litColor * mix(1.0, 0.5, shadow);
        FragColor = vec4(shadowColor, 1.0);
    } else {
        FragColor = vec4(litColor, 1.0);
    }
}