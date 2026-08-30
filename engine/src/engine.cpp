#include "core/engine.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <cstdlib>

// ── Default (solid color) shader source ────────────────────────────

static const char* default_vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vNormal;
out vec2 vUV;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
}
)";

static const char* default_fs = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() { FragColor = vec4(uColor, 1.0); }
)";

// ── Material (textured) shader source ───────────────────────────────

static const char* material_vs = R"(
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
out vec3 vViewDir;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
    vWorldPos = (uModel * vec4(aPos, 1.0)).xyz;
    vViewDir = -(uView * uModel * vec4(aPos, 1.0)).xyz;
}
)";

static const char* material_fs = R"(
#version 330 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;
in vec3 vViewDir;

out vec4 FragColor;
uniform int uHasShadowMap;

// ── Shadow PCF function ────────────────────────────────────────────
float ShadowCalculation(vec4 fragPosLightSpace, sampler2D depthMap,
                        mat4 lightSpaceMatrix, vec3 lightDir,
                        vec3 worldPos, vec3 normal, float near, float far) {
    // Transform to shadow map space
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    // Convert from [-1, 1] to [0, 1]
    projCoords = projCoords * 0.5 + 0.5;
    
    // Bias to prevent shadow acne
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.005);
    
    // PCF filtering with 3x3 kernel
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(2048.0);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(depthMap, projCoords.xy + vec2(float(x), float(y)) * texelSize).r;
            shadow += (projCoords.z - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;
    
    return shadow;
}

// ── Lighting function ──────────────────────────────────────────────
vec3 ComputeLighting(vec3 baseColor, vec3 normal, vec3 worldPos,
                     vec3 lightDir, vec3 viewDir, float metallic, float roughness,
                     float ao, vec3 emissiveColor) {

    // Bright ambient
    vec3 ambient = 0.45 * baseColor * ao;

    // Diffuse
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = NdotL * baseColor;

    // Specular
    vec3 halfDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float spec = pow(NdotH, mix(128.0, 8.0, roughness)) * metallic;
    vec3 specular = spec * vec3(1.0, 0.98, 0.95) * (0.3 + 0.7 * metallic);

    return (ambient + diffuse + specular) + emissiveColor;
}

uniform vec3 uBaseColor;
uniform vec3 uEmissiveColor;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAO;

uniform sampler2D uDiffuseTex;
uniform sampler2D uNormalTex;
uniform int uHasDiffuse;
uniform int uHasNormal;

// Shadow mapping uniforms
uniform sampler2D uShadowMap;
uniform mat4 uLightSpaceMatrix;
uniform vec3 uLightDirection;
uniform float uShadowNear;
uniform float uShadowFar;

void main() {
    vec3 baseColor = uBaseColor;
    vec3 normal = normalize(vNormal);

    // Sample diffuse texture if available
    if (uHasDiffuse == 1) {
        baseColor *= texture(uDiffuseTex, vUV).rgb;
    }

    // Compute lighting
    vec3 lightDir = normalize(uLightDirection);
    vec3 viewDir = normalize(vViewDir);
    
    // Shadow calculation
    vec4 fragPosLightSpace = uLightSpaceMatrix * vec4(vWorldPos, 1.0);
    float shadow = 0.0;
    vec3 litColor = ComputeLighting(baseColor, normal, vWorldPos,
                                    lightDir, viewDir, uMetallic, uRoughness,
                                    uAO, uEmissiveColor);
    
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
)";

// ── Shadow map depth shader ───────────────────────────────────────

static const char* shadow_depth_vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uLightSpaceMatrix;
uniform mat4 uModel;

void main() {
    gl_Position = uLightSpaceMatrix * uModel * vec4(aPos, 1.0);
}
)";

static const char* shadow_depth_fs = R"(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(1.0);
}
)";

// ── Shadow PCF sampling function (used by material shader) ─────────

static const char* shadow_pcf_func = R"(
// Percentage-Closer Filtering for shadow map sampling
float ShadowCalculation(vec4 fragPosLightSpace, sampler2D depthMap,
                        mat4 lightSpaceMatrix, vec3 lightDir,
                        vec3 worldPos, vec3 normal, float near, float far) {
    // Transform to shadow map space
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    // Convert from [-1, 1] to [0, 1]
    projCoords = projCoords * 0.5 + 0.5;
    
    // Get depth from shadow map at this texel
    float currentDepth = texture(depthMap, projCoords.xy).r;
    
    // Bias to prevent shadow acne
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.005);
    
    // PCF filtering with 4-tap
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(2048.0);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(depthMap, projCoords.xy + vec2(float(x), float(y)) * texelSize).r;
            shadow += (projCoords.z - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;
    
    return shadow;
}
)";

// ── Skybox shader source ────────────────────────────────────────────

static const char* skybox_vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vDirection;

void main() {
    // Remove translation from view matrix (skybox doesn't move)
    mat4 viewNoTranslate = uView;
    viewNoTranslate[3] = vec4(0.0, 0.0, 0.0, 1.0);
    
    vDirection = aPos;
    gl_Position = (uProjection * viewNoTranslate * vec4(aPos, 1.0)).xyww;
}
)";

static const char* skybox_fs = R"(
#version 330 core
in vec3 vDirection;
out vec4 FragColor;

void main() {
    vec3 dir = normalize(vDirection);
    
    // Sky gradient parameters
    float sunAngle = 0.4; // sun position in radians from horizon
    vec3 sunDir = normalize(vec3(0.8, sin(sunAngle), 0.6));
    
    // Sky colors
    vec3 zenith = vec3(0.15, 0.3, 0.85);    // deep blue zenith
    vec3 midday = vec3(0.4, 0.65, 0.95);    // light blue mid-sky
    vec3 horizon = vec3(0.85, 0.65, 0.45);   // warm orange horizon
    vec3 below = vec3(0.5, 0.35, 0.25);      // warm brown below
    
    float y = dir.y;
    vec3 skyColor;
    
    if (y > 0.1) {
        // Upper sky: blend from horizon to zenith
        float t = smoothstep(0.1, 0.8, y);
        skyColor = mix(horizon, zenith, t);
        skyColor = mix(skyColor, midday, smoothstep(0.0, 0.4, y));
    } else if (y > -0.05) {
        // Horizon band
        skyColor = horizon;
    } else {
        // Below horizon
        skyColor = mix(horizon, below, smoothstep(-0.05, -0.5, y));
    }
    
    // Sun disc
    float sunDot = max(dot(dir, sunDir), 0.0);
    float sun = pow(sunDot, 500.0) * 2.0;
    float sunGlow = pow(sunDot, 20.0) * 0.6;
    float sunHalo = pow(sunDot, 3.0) * 0.2;
    
    vec3 sunColor = vec3(1.0, 0.95, 0.8);
    skyColor += sunColor * (sun + sunGlow + sunHalo);
    
    // Horizon glow
    float horizonGlow = exp(-abs(y) * 4.0) * 0.3;
    skyColor += vec3(1.0, 0.7, 0.4) * horizonGlow;
    
    // Atmospheric scattering near horizon
    float horizonFactor = exp(-abs(y) * 3.0);
    skyColor = mix(skyColor, horizon, horizonFactor * 0.4);
    
    FragColor = vec4(skyColor, 1.0);
}
)";

// ── Static state ────────────────────────────────────────────────────

static GLFWwindow* s_window     = nullptr;
static int         s_width      = 0;
static int         s_height     = 0;
static GLuint      s_shaderProg = 0;
static GLuint      s_vao        = 0;
static GLuint      s_vbo        = 0;

static CoreEngine::Vector3 s_cameraPos    = {15, 12, 15};
static CoreEngine::Vector3 s_cameraTarget = {0, 0, 0};
static CoreEngine::Vector3 s_cameraOffset = {0, -1.5f, -5};

static std::vector<CoreEngine::SceneObject> s_sceneObjects;
static std::vector<std::shared_ptr<CoreEngine::PrimitiveMesh>> s_primitiveMeshes;
static uint32_t s_nextSceneObjectId = 1;
static uint32_t s_selectedObjectId = 0;

// ── Camera object ───────────────────────────────────────────────
static uint32_t s_cameraObjectId = 0;

static bool s_engineInited = false;

// Material-based scene objects
static std::vector<CoreEngine::SceneObjectWithMaterial> s_sceneObjectsWithMat;

// Skybox state
static GLuint s_skyboxVBO = 0;
static GLuint s_skyboxVAO = 0;
static GLuint s_skyboxProg = 0;
static bool   s_skyboxInited = false;

// ── Shadow mapping state ──────────────────────────────────────────
static GLuint s_shadowFBO = 0;
static GLuint s_shadowDepthTex = 0;
static GLuint s_shadowDepthRB = 0;
static GLuint s_shadowDepthProg = 0;
static bool   s_shadowInited = false;
static int    s_shadowWidth  = 2048;
static int    s_shadowHeight = 2048;
static CoreEngine::Vector3 s_shadowLightDir = {0.5f, 1.0f, 0.3f};  // Sun-like direction
static const float SHADOW_NEAR = 0.5f;
static const float SHADOW_FAR  = 50.0f;
static const float SHADOW_PLANE_HALF = 15.0f;  // Half extent of shadow frustum
static const float SHADOW_NEAR_PLANE = 5.0f;  // Distance from light to near plane (so camera is behind light)

// ── Helpers ─────────────────────────────────────────────────────────

static void compileDefaultShader() {
    // Use the material shader (with shadow mapping & PBR lighting)
    s_shaderProg = CoreEngine::CreateShaderProgram(material_vs, material_fs);
    glUseProgram(s_shaderProg);

    // Set initial camera uniforms
    auto view = glm::lookAt(
        glm::vec3(s_cameraPos.x, s_cameraPos.y, s_cameraPos.z),
        glm::vec3(s_cameraTarget.x, s_cameraTarget.y, s_cameraTarget.z),
        glm::vec3(0, 1, 0));
    GLint viewLoc = glGetUniformLocation(s_shaderProg, "uView");
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
}

static bool s_primitivesBuilt = false;

static void buildPrimitiveVAOs() {
    if (s_primitivesBuilt) return;  // Build once — don't clear, or we destroy VAOs!
    s_primitivesBuilt = true;

    // Build cube mesh data manually (not via CreateBox which is in namespace)
    CoreEngine::PrimitiveMesh cube;
    cube.name = "cube";

    // 6 faces * 2 triangles per face * 3 vertices per triangle = 36 vertices
    // Each vertex: pos(3) + normal(3) + uv(2) = 8 floats
    static const float cubeVerts[] = {
        // Front face (+Z): 2 triangles = 6 vertices
        -0.5f, -0.5f,  0.5f,   0,0,1,  0,0,
         0.5f, -0.5f,  0.5f,   0,0,1,  1,0,
         0.5f,  0.5f,  0.5f,   0,0,1,  1,1,
        -0.5f, -0.5f,  0.5f,   0,0,1,  0,0,
         0.5f,  0.5f,  0.5f,   0,0,1,  1,1,
        -0.5f,  0.5f,  0.5f,   0,0,1,  0,1,
        // Back face (-Z): 2 triangles = 6 vertices
         0.5f, -0.5f, -0.5f,   0,0,-1,  0,0,
        -0.5f, -0.5f, -0.5f,   0,0,-1,  1,0,
        -0.5f,  0.5f, -0.5f,   0,0,-1,  1,1,
         0.5f, -0.5f, -0.5f,   0,0,-1,  0,0,
        -0.5f,  0.5f, -0.5f,   0,0,-1,  1,1,
         0.5f,  0.5f, -0.5f,   0,0,-1,  0,1,
        // Top face (+Y): 2 triangles = 6 vertices
        -0.5f,  0.5f,  0.5f,   0,1,0,  0,0,
         0.5f,  0.5f,  0.5f,   0,1,0,  1,0,
         0.5f,  0.5f, -0.5f,   0,1,0,  1,1,
        -0.5f,  0.5f,  0.5f,   0,1,0,  0,0,
         0.5f,  0.5f, -0.5f,   0,1,0,  1,1,
        -0.5f,  0.5f, -0.5f,   0,1,0,  0,1,
        // Bottom face (-Y): 2 triangles = 6 vertices
        -0.5f, -0.5f, -0.5f,   0,-1,0,  0,0,
         0.5f, -0.5f, -0.5f,   0,-1,0,  1,0,
         0.5f, -0.5f,  0.5f,   0,-1,0,  1,1,
        -0.5f, -0.5f, -0.5f,   0,-1,0,  0,0,
         0.5f, -0.5f,  0.5f,   0,-1,0,  1,1,
        -0.5f, -0.5f,  0.5f,   0,-1,0,  0,1,
        // Right face (+X): 2 triangles = 6 vertices
         0.5f, -0.5f,  0.5f,   1,0,0,  0,0,
         0.5f, -0.5f, -0.5f,   1,0,0,  1,0,
         0.5f,  0.5f, -0.5f,   1,0,0,  1,1,
         0.5f, -0.5f,  0.5f,   1,0,0,  0,0,
         0.5f,  0.5f, -0.5f,   1,0,0,  1,1,
         0.5f,  0.5f,  0.5f,   1,0,0,  0,1,
        // Left face (-X): 2 triangles = 6 vertices
        -0.5f, -0.5f, -0.5f,  -1,0,0,  0,0,
        -0.5f, -0.5f,  0.5f,  -1,0,0,  1,0,
        -0.5f,  0.5f,  0.5f,  -1,0,0,  1,1,
        -0.5f, -0.5f, -0.5f,  -1,0,0,  0,0,
        -0.5f,  0.5f,  0.5f,  -1,0,0,  1,1,
        -0.5f,  0.5f, -0.5f,  -1,0,0,  0,1,
    };

    glGenVertexArrays(1, &cube.VAO);
    glGenBuffers(1, &cube.VBO);
    glBindVertexArray(cube.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, cube.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // No EBO — use glDrawArrays
    cube.EBO = 0;
    cube.indexCount = sizeof(cubeVerts) / (sizeof(float) * 8);
    cube.halfExtent = {0.5f, 0.5f, 0.5f};  // vertices go from -0.5 to +0.5
    s_primitiveMeshes.push_back(CoreEngine::CreateMesh(std::move(cube)));

    // Build plane mesh - 2 triangles = 6 vertices
    CoreEngine::PrimitiveMesh plane;
    plane.name = "plane";

    static const float planeVerts[] = {
        -5.0f, 0, -5.0f,   0,1,0,  0,0,
         5.0f, 0, -5.0f,   0,1,0,  1,0,
         5.0f, 0,  5.0f,   0,1,0,  1,1,
        -5.0f, 0, -5.0f,   0,1,0,  0,0,
         5.0f, 0,  5.0f,   0,1,0,  1,1,
        -5.0f, 0,  5.0f,   0,1,0,  0,1,
    };

    glGenVertexArrays(1, &plane.VAO);
    glGenBuffers(1, &plane.VBO);
    glBindVertexArray(plane.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, plane.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(planeVerts), planeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // No EBO — use glDrawArrays
    plane.EBO = 0;
    plane.indexCount = sizeof(planeVerts) / (sizeof(float) * 8);
    plane.halfExtent = {5.0f, 0.01f, 5.0f};  // vertices go from -5 to +5
    s_primitiveMeshes.push_back(CoreEngine::CreateMesh(std::move(plane)));
}

// ── Engine ──────────────────────────────────────────────────────────

namespace CoreEngine {

void Init() {
    if (s_engineInited) return;
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    s_engineInited = true;
}

void Shutdown() {
    if (!s_engineInited) return;

    // Clean up shadow map (must be before context destruction)
    CleanupShadowMap();

    glDeleteVertexArrays(1, &s_vao);
    glDeleteBuffers(1, &s_vbo);
    if (s_shaderProg) glDeleteProgram(s_shaderProg);

    // Release GPU meshes while the GL context is still alive;
    // shared refs (scene objects, templates) free their VAOs/VBOs/EBOs here
    s_sceneObjects.clear();
    s_primitiveMeshes.clear();

    if (s_window) glfwDestroyWindow(s_window);
    s_window = nullptr;
    glfwTerminate();
    s_engineInited = false;
}

std::string GetEngineName() { return ENGINE_NAME; }

void GetVersion(int& major, int& minor) { major = VERSION_MAJOR; minor = VERSION_MINOR; }

EngineInfo GetEngineInfo(int width, int height) {
    EngineInfo info;
    info.name = ENGINE_NAME;
    info.majorVersion = VERSION_MAJOR;
    info.minorVersion = VERSION_MINOR;
    return info;
}

// ── Renderer ────────────────────────────────────────────────────────

bool InitRenderer(const char* title, int width, int height) {
    s_width = width;
    s_height = height;

    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_SAMPLES, 1);

    s_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!s_window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        return false;
    }
    glfwMakeContextCurrent(s_window);
    glfwSetFramebufferSizeCallback(s_window, [](GLFWwindow* win, int w, int h) {
        s_width = w;
        s_height = h;
        glViewport(0, 0, w, h);
    });

        if (glewInit() != GLEW_OK) {
            fprintf(stderr, "Failed to initialize GLEW\n");
            return false;
        }

    printf("OpenGL version: %s\n", glGetString(GL_VERSION));
    printf("GLSL version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearDepthf(1.0f);
    glClearDepth(1.0);

    compileDefaultShader();
    buildPrimitiveVAOs();

    // Initialize offset from default camera position to target
    s_cameraOffset = s_cameraPos - s_cameraTarget;

    glfwShowWindow(s_window);
    return true;
}

GLFWwindow* GetWindow() { return s_window; }

void RenderBegin() {
    glViewport(0, 0, s_width, s_height);
    glClearColor(0.2f, 0.2f, 0.25f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthMask(GL_TRUE);
}

void RenderEnd() {
    glfwSwapBuffers(s_window);
}

bool ShouldClose() {
    return glfwWindowShouldClose(s_window) != 0;
}

// ── Shaders ─────────────────────────────────────────────────────────

GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint len;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        char* log = (char*)alloca(len);
        glGetShaderInfoLog(shader, len, nullptr, log);
        fprintf(stderr, "Shader compile error:\n%s\n", log);
    }
    return shader;
}

GLuint CreateShaderProgram(const char* vsSource, const char* fsSource) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsSource, nullptr);
    glCompileShader(vs);
    GLint vsSuccess;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &vsSuccess);
    if (!vsSuccess) {
        GLint logLen;
        glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &logLen);
        if (logLen > 1) {
            char* log = new char[logLen];
            glGetShaderInfoLog(vs, logLen, nullptr, log);
            fprintf(stderr, "[SHADER ERR] Vertex shader compile error:\n%s\n", log);
            delete[] log;
        }
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSource, nullptr);
    glCompileShader(fs);
    GLint fsSuccess;
    glGetShaderiv(fs, GL_COMPILE_STATUS, &fsSuccess);
    if (!fsSuccess) {
        GLint logLen;
        glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &logLen);
        if (logLen > 1) {
            char* log = new char[logLen];
            glGetShaderInfoLog(fs, logLen, nullptr, log);
            fprintf(stderr, "[SHADER ERR] Fragment shader compile error:\n%s\n", log);
            delete[] log;
        }
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        GLint len;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        char* log = new char[len];
        glGetProgramInfoLog(prog, len, nullptr, log);
        fprintf(stderr, "[SHADER ERR] Program link error:\n%s\n", log);
        delete[] log;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

void UseShader(GLuint program) {
    s_shaderProg = program;
    glUseProgram(program);
}

void SetUniformMat4(GLuint program, const char* name, const glm::mat4& m) {
    glUniformMatrix4fv(glGetUniformLocation(program, name), 1, GL_FALSE, glm::value_ptr(m));
}

void SetUniformVec3(GLuint program, const char* name, const glm::vec3& v) {
    glUniform3fv(glGetUniformLocation(program, name), 1, glm::value_ptr(v));
}

// ── Mesh primitives ─────────────────────────────────────────────────

MeshPtr CreateMesh(PrimitiveMesh mesh) {
    return MeshPtr(new PrimitiveMesh(std::move(mesh)), [](PrimitiveMesh* m) {
        if (m->VAO) glDeleteVertexArrays(1, &m->VAO);
        if (m->VBO) glDeleteBuffers(1, &m->VBO);
        if (m->EBO) glDeleteBuffers(1, &m->EBO);
        delete m;
    });
}

void InitPrimitiveMeshes() {
    buildPrimitiveVAOs();
}

PrimitiveMesh CreateBox(const Vector3& size) {
    PrimitiveMesh mesh;
    mesh.name = "box";

    // 8 vertices, 36 indices (6 faces x 2 tris x 3 verts)
    static const float verts[] = {
        // position (3) + normal (3) + uv (2)
        // Front face
        -size.x/2, -size.y/2,  size.z/2,   0,0,1,   0,0,
         size.x/2, -size.y/2,  size.z/2,   0,0,1,   1,0,
         size.x/2,  size.y/2,  size.z/2,   0,0,1,   1,1,
        -size.x/2,  size.y/2,  size.z/2,   0,0,1,   0,1,
        // Back face
         size.x/2, -size.y/2, -size.z/2,   0,0,-1,  0,0,
        -size.x/2, -size.y/2, -size.z/2,   0,0,-1,  1,0,
        -size.x/2,  size.y/2, -size.z/2,   0,0,-1,  1,1,
         size.x/2,  size.y/2, -size.z/2,   0,0,-1,  0,1,
        // Top face
        -size.x/2,  size.y/2,  size.z/2,   0,1,0,   0,0,
        -size.x/2,  size.y/2, -size.z/2,   0,1,0,   0,1,
         size.x/2,  size.y/2, -size.z/2,   0,1,0,   1,1,
         size.x/2,  size.y/2,  size.z/2,   0,1,0,   1,0,
        // Bottom face
        -size.x/2, -size.y/2, -size.z/2,   0,-1,0,  0,0,
         size.x/2, -size.y/2, -size.z/2,   0,-1,0,  1,0,
         size.x/2, -size.y/2,  size.z/2,   0,-1,0,  1,1,
        -size.x/2, -size.y/2,  size.z/2,   0,-1,0,  0,1,
        // Right face
         size.x/2, -size.y/2,  size.z/2,   1,0,0,   0,0,
         size.x/2, -size.y/2, -size.z/2,   1,0,0,   1,0,
         size.x/2,  size.y/2, -size.z/2,   1,0,0,   1,1,
         size.x/2,  size.y/2,  size.z/2,   1,0,0,   0,1,
        // Left face
        -size.x/2, -size.y/2, -size.z/2,  -1,0,0,   0,0,
        -size.x/2, -size.y/2,  size.z/2,  -1,0,0,   1,0,
        -size.x/2,  size.y/2,  size.z/2,  -1,0,0,   1,1,
        -size.x/2,  size.y/2, -size.z/2,  -1,0,0,   0,1,
    };

    static const GLuint indices[] = {
        0,1,2, 0,2,3,       // front
        4,5,6, 4,6,7,       // back
        8,9,10, 8,10,11,    // top
        12,13,14, 12,14,15, // bottom
        16,17,18, 16,18,19, // right
        20,21,22, 20,22,23  // left
    };

    mesh.indexCount = 36;

    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);
    glGenBuffers(1, &mesh.EBO);

    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    // normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    // uv
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Box vertices go from -size/2 to +size/2
    mesh.halfExtent = {size.x / 2.0f, size.y / 2.0f, size.z / 2.0f};

    return mesh;
}

PrimitiveMesh CreatePlane(float width, float height) {
    PrimitiveMesh mesh;
    mesh.name = "plane";

    static const float verts[] = {
        // position (3) + normal (3) + uv (2)
        -width/2, 0, -height/2,   0,1,0,  0,0,
         width/2, 0, -height/2,   0,1,0,  1,0,
         width/2, 0,  height/2,   0,1,0,  1,1,
        -width/2, 0,  height/2,   0,1,0,  0,1,
    };

    static const GLuint indices[] = { 0, 1, 2, 0, 2, 3 };

    mesh.indexCount = 6;

    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);
    glGenBuffers(1, &mesh.EBO);

    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Plane vertices go from -width/2 to +width/2 and -height/2 to +height/2
    mesh.halfExtent = {width / 2.0f, 0.01f, height / 2.0f};

    return mesh;
}

void DestroyMesh(PrimitiveMesh& mesh) {
    if (mesh.VAO) { glDeleteVertexArrays(1, &mesh.VAO); mesh.VAO = 0; }
    if (mesh.VBO) { glDeleteBuffers(1, &mesh.VBO); mesh.VBO = 0; }
    if (mesh.EBO) { glDeleteBuffers(1, &mesh.EBO); mesh.EBO = 0; }
    mesh.indexCount = 0;
}

// ── Scene management ────────────────────────────────────────────────

std::vector<SceneObject>& GetSceneObjects() { return s_sceneObjects; }

SceneObject& AddToScene(const std::string& name, MeshPtr mesh) {
    SceneObject obj;
    obj.id = s_nextSceneObjectId++;
    obj.name = name;
    obj.mesh = std::move(mesh);
    s_sceneObjects.push_back(std::move(obj));
    return s_sceneObjects.back();
}

void ClearScene() {
    s_selectedObjectId = 0;
    s_cameraObjectId = 0;  // Reset so camera gets recreated on next CreateCameraObject()
    s_sceneObjects.clear();
    s_sceneObjectsWithMat.clear();
}

void ClearSceneWithMaterials() {
    s_selectedObjectId = 0;
    s_cameraObjectId = 0;  // Reset so camera gets recreated on next CreateCameraObject()
    s_sceneObjectsWithMat.clear();
}

void RemoveFromScene(uint32_t id) {
    for (auto it = s_sceneObjects.begin(); it != s_sceneObjects.end(); ++it) {
        if (it->id == id) {
            if (s_selectedObjectId == id) s_selectedObjectId = 0;
            s_sceneObjects.erase(it);
            return;
        }
    }
}

void RemoveFromSceneWithMaterials(uint32_t id) {
    for (auto it = s_sceneObjectsWithMat.begin(); it != s_sceneObjectsWithMat.end(); ++it) {
        if (it->id == id) {
            if (s_selectedObjectId == id) s_selectedObjectId = 0;
            s_sceneObjectsWithMat.erase(it);
            return;
        }
    }
}

void SelectObject(uint32_t id) {
    s_selectedObjectId = id;
}

SceneObject* GetSelectedObject() {
    for (auto& obj : s_sceneObjects) {
        if (obj.id == s_selectedObjectId) {
            return &obj;
        }
    }
    return nullptr;
}

uint32_t GetSelectedObjectId() { return s_selectedObjectId; }

uint32_t GetNextSceneObjectId() { return s_nextSceneObjectId; }

// ── Camera ──────────────────────────────────────────────────────────

void SetCameraPosition(Vector3 pos) {
    s_cameraPos = pos;
    s_cameraOffset = s_cameraPos - s_cameraTarget;
}
void SetCameraTarget(Vector3 target) {
    s_cameraTarget = target;
    s_cameraOffset = s_cameraPos - s_cameraTarget;
}
void SetCameraDirection(Vector3 dir) { s_cameraOffset = dir; }
Vector3 GetCameraPosition() { return s_cameraPos; }
Vector3 GetCameraTarget() { return s_cameraTarget; }
Vector3 GetCameraDirection() { return s_cameraOffset; }
Vector3 GetCameraOffset() { return s_cameraOffset; }
void SetCameraOffset(Vector3 offset) {
    s_cameraOffset = offset;
    s_cameraPos = s_cameraTarget + offset;
}
void ResetCamera() {
    s_cameraPos = {15, 12, 15};
    s_cameraTarget = {0, 0, 0};
    s_cameraOffset = s_cameraPos - s_cameraTarget;
}

glm::mat4 GetProjectionMatrix(float fov, float aspect) {
    return glm::perspective(glm::radians(fov), aspect, 0.1f, 100.0f);
}

GLuint GetModelUniformLocation(GLuint prog, bool& found) {
    GLint loc = glGetUniformLocation(prog, "uModel");
    found = (loc != -1);
    return (GLuint)loc;
}

GLuint GetShaderProgram() { return s_shaderProg; }

MeshPtr GetPrimitiveMesh(const char* name) {
    for (const auto& m : s_primitiveMeshes) {
        if (m->name == name) return m;
    }
    return nullptr;
}

// ── 3D Grid ─────────────────────────────────────────────────────────

void DrawGrid(int divisions, float unit, float halfExtent, const glm::mat4& view, const glm::mat4& projection) {
    const float step = halfExtent * 2.0f / divisions;
    const float lineWidth = 0.02f;  // half-width of each grid line

    static std::vector<float> gridVerts;
    static std::vector<GLuint> gridIndices;
    static GLuint gridVBO = 0;
    static GLuint gridEBO = 0;
    static GLuint gridVAO = 0;
    static GLuint gridShaderProg = 0;
    static bool gridInited = false;
    static int cachedDivisions = 0;

    if (!gridInited || cachedDivisions != divisions) {
        gridVerts.clear();
        gridIndices.clear();

        // Lines along X axis at each Z division
        for (int i = 0; i <= divisions; ++i) {
            float pos = -halfExtent + i * step;
            float z1 = pos - lineWidth;
            float z2 = pos + lineWidth;

            gridVerts.push_back(-halfExtent); gridVerts.push_back(0.01f); gridVerts.push_back(z1);
            gridVerts.push_back( halfExtent); gridVerts.push_back(0.01f); gridVerts.push_back(z1);
            gridVerts.push_back( halfExtent); gridVerts.push_back(0.01f); gridVerts.push_back(z2);
            gridVerts.push_back(-halfExtent); gridVerts.push_back(0.01f); gridVerts.push_back(z2);

            GLuint base = (GLuint)gridVerts.size() / 3 - 4;
            gridIndices.push_back(base + 0);
            gridIndices.push_back(base + 1);
            gridIndices.push_back(base + 2);
            gridIndices.push_back(base + 0);
            gridIndices.push_back(base + 2);
            gridIndices.push_back(base + 3);
        }

        // Lines along Z axis at each X division
        for (int i = 0; i <= divisions; ++i) {
            float pos = -halfExtent + i * step;
            float x1 = pos - lineWidth;
            float x2 = pos + lineWidth;

            gridVerts.push_back(x1); gridVerts.push_back(0.01f); gridVerts.push_back(-halfExtent);
            gridVerts.push_back(x2); gridVerts.push_back(0.01f); gridVerts.push_back(-halfExtent);
            gridVerts.push_back(x2); gridVerts.push_back(0.01f); gridVerts.push_back( halfExtent);
            gridVerts.push_back(x1); gridVerts.push_back(0.01f); gridVerts.push_back( halfExtent);

            GLuint base = (GLuint)gridVerts.size() / 3 - 4;
            gridIndices.push_back(base + 0);
            gridIndices.push_back(base + 1);
            gridIndices.push_back(base + 2);
            gridIndices.push_back(base + 0);
            gridIndices.push_back(base + 2);
            gridIndices.push_back(base + 3);
        }

        if (gridVAO) glDeleteVertexArrays(1, &gridVAO);
        if (gridVBO) glDeleteBuffers(1, &gridVBO);
        if (gridEBO) glDeleteBuffers(1, &gridEBO);

        glGenVertexArrays(1, &gridVAO);
        glGenBuffers(1, &gridVBO);
        glGenBuffers(1, &gridEBO);
        glBindVertexArray(gridVAO);
        glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, gridVerts.size() * sizeof(float), gridVerts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, gridIndices.size() * sizeof(GLuint), gridIndices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, (void*)0);
        glBindVertexArray(0);
        cachedDivisions = divisions;

        if (gridShaderProg) glDeleteProgram(gridShaderProg);
        const char* g_vs = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            uniform mat4 uView;
            uniform mat4 uProjection;
            uniform vec3 uColor;
            out vec3 vColor;
            void main() {
                gl_Position = uProjection * uView * vec4(aPos, 1.0);
                vColor = uColor;
            }
        )";
        const char* g_fs = R"(
            #version 330 core
            in vec3 vColor;
            out vec4 FragColor;
            void main() {
                FragColor = vec4(vColor, 1.0);
            }
        )";
        gridShaderProg = CoreEngine::CreateShaderProgram(g_vs, g_fs);
        gridInited = true;
    }

    if (gridVAO && gridShaderProg && !gridIndices.empty()) {
        GLuint savedShader = s_shaderProg;
        glUseProgram(gridShaderProg);

        GLint viewLoc = glGetUniformLocation(gridShaderProg, "uView");
        GLint projLoc = glGetUniformLocation(gridShaderProg, "uProjection");
        GLint colorLoc = glGetUniformLocation(gridShaderProg, "uColor");

        if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
        if (colorLoc != -1) glUniform3f(colorLoc, 0.85f, 0.85f, 0.85f);

        glBindVertexArray(gridVAO);
        glDisable(GL_DEPTH_TEST);
        glDrawElements(GL_TRIANGLES, (GLsizei)gridIndices.size(), GL_UNSIGNED_INT, 0);
        glEnable(GL_DEPTH_TEST);
        glBindVertexArray(0);

        if (savedShader) glUseProgram(savedShader);
    }
}

// ── Selected Object Bounds ──────────────────────────────────────────

void DrawSelectedObjectBounds(const glm::mat4& view, const glm::mat4& projection) {
    // Find the selected object in either scene vector
    SceneObject* sel = nullptr;
    uint32_t selectedId = s_selectedObjectId;

    // Try s_sceneObjects first (regular objects)
    for (auto& obj : s_sceneObjects) {
        if (obj.id == selectedId) { sel = &obj; break; }
    }

    // If not found, try s_sceneObjectsWithMat (includes camera)
    if (!sel) {
        for (auto& obj : s_sceneObjectsWithMat) {
            if (obj.id == selectedId) { sel = &obj; break; }
        }
    }

    if (!sel) return;
    if (!sel->mesh) return;
    if (sel->mesh->indexCount == 0) return;

    // Use the mesh's stored half-extents (set at creation time)
    Vector3 he = sel->mesh->halfExtent;
    float extents[3] = {he.x, he.y, he.z};

    // Define a unit cube (local space) and scale it via the model matrix.
    // The 8 corners of a unit cube centered at origin (from -1 to +1).
    static const glm::vec3 localCorners[8] = {
        glm::vec3(-1, -1, -1),  // 0
        glm::vec3( 1, -1, -1),  // 1
        glm::vec3( 1,  1, -1),  // 2
        glm::vec3(-1,  1, -1),  // 3
        glm::vec3(-1, -1,  1),  // 4
        glm::vec3( 1, -1,  1),  // 5
        glm::vec3( 1,  1,  1),  // 6
        glm::vec3(-1,  1,  1),  // 7
    };

    // 12 edges (2 vertices per edge = 24 verts)
    const int edgePairs[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0}, // bottom
        {4,5}, {5,6}, {6,7}, {7,4}, // top
        {0,4}, {1,5}, {2,6}, {3,7}  // verticals
    };

    // Build edge vertex data: scale local corners by halfExtent to get mesh-space bounds
    float edgeVerts[24 * 3];
    for (int i = 0; i < 12; ++i) {
        glm::vec3 p0 = localCorners[edgePairs[i][0]] * glm::vec3(extents[0], extents[1], extents[2]);
        glm::vec3 p1 = localCorners[edgePairs[i][1]] * glm::vec3(extents[0], extents[1], extents[2]);
        edgeVerts[(i*6+0)] = p0.x;
        edgeVerts[(i*6+1)] = p0.y;
        edgeVerts[(i*6+2)] = p0.z;
        edgeVerts[(i*6+3)] = p1.x;
        edgeVerts[(i*6+4)] = p1.y;
        edgeVerts[(i*6+5)] = p1.z;
    }

    // Build model matrix from object transform (position + rotation + scale)
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(sel->position.x, sel->position.y, sel->position.z));
    model = glm::rotate(model, (float)sel->rotation.x, glm::vec3(1, 0, 0));
    model = glm::rotate(model, (float)sel->rotation.y, glm::vec3(0, 1, 0));
    model = glm::rotate(model, (float)sel->rotation.z, glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(sel->scale.x, sel->scale.y, sel->scale.z));

    static GLuint boundsVBO = 0;
    static GLuint boundsVAO = 0;
    if (boundsVAO == 0) {
        glGenVertexArrays(1, &boundsVAO);
        glGenBuffers(1, &boundsVBO);
        glBindVertexArray(boundsVAO);
        glBindBuffer(GL_ARRAY_BUFFER, boundsVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(edgeVerts), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, (void*)0);
        glBindVertexArray(0);
    }

    // Update vertex data each frame (bounds change with selection)
    glBindVertexArray(boundsVAO);
    glBindBuffer(GL_ARRAY_BUFFER, boundsVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(edgeVerts), edgeVerts);

    // Use a yellow color shader for the bounds
    static GLuint boundsShaderProg = 0;
    static bool boundsShaderInited = false;
    if (!boundsShaderInited) {
        const char* b_vs = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            uniform mat4 uModel;
            uniform mat4 uView;
            uniform mat4 uProjection;
            void main() {
                gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
            }
        )";
        const char* b_fs = R"(
            #version 330 core
            out vec4 FragColor;
            void main() {
                FragColor = vec4(1.0, 0.8, 0.0, 1.0);
            }
        )";
        boundsShaderProg = CoreEngine::CreateShaderProgram(b_vs, b_fs);
        boundsShaderInited = true;
    }

    // Save the original program before switching
    GLuint savedShader = s_shaderProg;
    glUseProgram(boundsShaderProg);
    GLint viewLoc = glGetUniformLocation(boundsShaderProg, "uView");
    GLint projLoc = glGetUniformLocation(boundsShaderProg, "uProjection");
    GLint modelLoc = glGetUniformLocation(boundsShaderProg, "uModel");

    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
    // Pass the actual model matrix: the shader transforms local-space vertices.
    if (modelLoc != -1) glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

    glLineWidth(2.0f);
    glBindVertexArray(boundsVAO);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_LINES, 0, 24);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Restore original shader
    if (savedShader) glUseProgram(savedShader);
}

// ── Skybox ──────────────────────────────────────────────────────────

void CoreEngine::InitSkybox() {
    if (s_skyboxInited) return;

    // Skybox is a cube (6 faces, 24 vertices, 36 indices)
    const float cubeVerts[] = {
        // Front face
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        // Back face
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
        // Top face
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
        // Bottom face
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,
        // Right face
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        // Left face
        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,
    };

    const GLuint cubeIndices[] = {
        0,1,2, 0,2,3,       // front
        4,5,6, 4,6,7,       // back
        8,9,10, 8,10,11,    // top
        12,13,14, 12,14,15, // bottom
        16,17,18, 16,18,19, // right
        20,21,22, 20,22,23  // left
    };

    GLuint skyboxVBO, skyboxEBO;
    glGenVertexArrays(1, &s_skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glGenBuffers(1, &skyboxEBO);

    glBindVertexArray(s_skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, (void*)0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skyboxEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);
    glBindVertexArray(0);

    // Create skybox shader
    s_skyboxProg = CoreEngine::CreateShaderProgram(skybox_vs, skybox_fs);

    s_skyboxInited = true;
}

void CoreEngine::DrawSkybox(float aspect) {
    if (!s_skyboxInited || !s_window) return;

    // Get camera position and projection
    auto camPos = s_cameraPos;
    auto camTarget = s_cameraTarget;
    glm::mat4 view = glm::lookAt(
        glm::vec3(camPos.x, camPos.y, camPos.z),
        glm::vec3(camTarget.x, camTarget.y, camTarget.z),
        glm::vec3(0, 1, 0));

    glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

    // Use skybox shader
    glUseProgram(s_skyboxProg);
    GLint viewLoc = glGetUniformLocation(s_skyboxProg, "uView");
    GLint projLoc = glGetUniformLocation(s_skyboxProg, "uProjection");
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

    // Draw skybox (disable depth write, use depth equal)
    glBindVertexArray(s_skyboxVAO);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_LEQUAL);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glUseProgram(s_shaderProg);
}

// ── Textures ────────────────────────────────────────────────────────

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "core/stb_image.h"

Texture CoreEngine::LoadTexture(const std::string& path) {
    Texture tex;
    tex.width = 0;
    tex.height = 0;
    tex.channels = 0;

    // stb_image loads with flipped Y by default; flip horizontally for GL
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path.c_str(), &tex.width, &tex.height, &tex.channels, 4);
    if (!data) {
        fprintf(stderr, "[Texture] Failed to load: %s (stb error: %s)\n", 
                path.c_str(), stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        stbi_image_free(data);
        return tex;
    }

    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);

    return tex;
}

Texture CoreEngine::LoadTextureFromMemory(const unsigned char* data, int width, int height, int channels) {
    Texture tex;
    tex.width = width;
    tex.height = height;
    tex.channels = channels;

    printf("[LoadTextureFromMemory] width=%d height=%d channels=%d data=%p\n", width, height, channels, data);

    glGenTextures(1, &tex.id);
    printf("[LoadTextureFromMemory] glGenTextures returned id=%u\n", tex.id);
    
    if (tex.id == 0) {
        printf("[LoadTextureFromMemory] ERROR: glGenTextures failed! OpenGL error: %u\n", glGetError());
        return tex;
    }
    
    glBindTexture(GL_TEXTURE_2D, tex.id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Determine GL format from channel count
    GLenum format = GL_RGBA;
    if (channels == 3) format = GL_RGB;
    else if (channels == 1) format = GL_RED;

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    GLenum texErr = glGetError();
    if (texErr != GL_NO_ERROR) {
        printf("[LoadTextureFromMemory] glTexImage2D error: %u\n", texErr);
    }
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void CoreEngine::DestroyTexture(Texture& tex) {
    if (tex.id) {
        glDeleteTextures(1, &tex.id);
        tex.id = 0;
    }
    tex.width = 0;
    tex.height = 0;
    tex.channels = 0;
}

void CoreEngine::BindTexture(Texture& tex, GLuint unit) {
    if (tex.id == 0) return;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex.id);
}

// ── Materials ───────────────────────────────────────────────────────

Material CoreEngine::CreateDefaultMaterial() {
    return Material{
        "default",
        glm::vec3(0.7f, 0.7f, 0.7f),  // base color - mid gray
        glm::vec3(0.0f, 0.0f, 0.0f),  // emissive
        0.0f,             // metallic
        1.0f,             // roughness
        1.0f,             // AO
        {}, {},           // diffuseTexture, normalTexture (id=0 = no texture)
        false
    };
}

// ── Static state for material-based scene ───────────────────────────

static GLuint s_materialShaderProg = 0;
static bool s_materialShaderInited = false;

// ── Get scene objects with materials ───────────────────────────────

std::vector<CoreEngine::SceneObjectWithMaterial>& GetSceneObjectsWithMaterials() {
    return s_sceneObjectsWithMat;
}

CoreEngine::SceneObjectWithMaterial& AddToSceneWithMaterial(const std::string& name, MeshPtr mesh, Material mat) {
    SceneObjectWithMaterial obj;
    obj.id = s_nextSceneObjectId++;
    obj.name = name;
    obj.mesh = std::move(mesh);
    obj.material = mat;
    s_sceneObjectsWithMat.push_back(std::move(obj));
    return s_sceneObjectsWithMat.back();
}

// ── Shadow Mapping Implementation ─────────────────────────────────

void CoreEngine::SetShadowLightDirection(Vector3 dir) {
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len > 0.001f) {
        s_shadowLightDir = {dir.x / len, dir.y / len, dir.z / len};
    }
}

Vector3 CoreEngine::GetShadowLightDirection() {
    return s_shadowLightDir;
}

glm::mat4 CoreEngine::GetLightViewMatrix() {
    // Light looks down its direction
    auto dir = s_shadowLightDir;
    glm::vec3 lightPos(
        dir.x * SHADOW_PLANE_HALF + SHADOW_NEAR_PLANE,
        dir.y * SHADOW_PLANE_HALF + SHADOW_NEAR_PLANE,
        dir.z * SHADOW_PLANE_HALF + SHADOW_NEAR_PLANE
    );
    glm::vec3 target(0.0f, 0.0f, 0.0f);
    return glm::lookAt(lightPos, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 CoreEngine::GetLightProjectionMatrix() {
    // Use an orthographic projection for directional light shadow map
    float half = SHADOW_PLANE_HALF;
    return glm::ortho(-half, half, -half, half, SHADOW_NEAR, SHADOW_FAR);
}

glm::mat4 CoreEngine::GetLightSpaceMatrix() {
    return GetLightProjectionMatrix() * GetLightViewMatrix();
}

void CoreEngine::InitShadowMap(int width, int height) {
    if (s_shadowInited) {
        // Recreate if dimensions changed
        if (s_shadowWidth != width || s_shadowHeight != height) {
            CleanupShadowMap();
        } else {
            return;
        }
    }

    s_shadowWidth = width;
    s_shadowHeight = height;

    // Create depth texture
    glGenTextures(1, &s_shadowDepthTex);
    glBindTexture(GL_TEXTURE_2D, s_shadowDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    
    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create FBO with depth attachment
    glGenFramebuffers(1, &s_shadowFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s_shadowDepthTex, 0);
    
    // Use GL_NONE - we only need depth, no color attachments
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[ShadowMap] FBO incomplete!\n");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Compile shadow depth shader
    s_shadowDepthProg = CoreEngine::CreateShaderProgram(shadow_depth_vs, shadow_depth_fs);

    s_shadowInited = true;
    printf("[ShadowMap] Initialized %dx%d shadow map\n", width, height);
}

void CoreEngine::DrawShadowPass() {
    if (!s_shadowInited) return;

    // Bind shadow FBO for depth rendering
    glBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
    glViewport(0, 0, s_shadowWidth, s_shadowHeight);
    glClear(GL_DEPTH_BUFFER_BIT);
    
    // Use shadow shader
    glUseProgram(s_shadowDepthProg);
    
    // Compute light space matrix
    glm::mat4 lightSpaceMat = GetLightSpaceMatrix();
    GLint lsmLoc = glGetUniformLocation(s_shadowDepthProg, "uLightSpaceMatrix");
    if (lsmLoc != -1) {
        glUniformMatrix4fv(lsmLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMat));
    }

    // Render all scene objects with materials
    auto& scene = GetSceneObjectsWithMaterials();
    for (auto& obj : scene) {
        auto& mesh = obj.mesh;
        if (!mesh || !mesh->VAO || mesh->indexCount == 0) continue;

        // Build model matrix
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(obj.position.x, obj.position.y, obj.position.z));
        model = glm::rotate(model, (float)obj.rotation.x, glm::vec3(1, 0, 0));
        model = glm::rotate(model, (float)obj.rotation.y, glm::vec3(0, 1, 0));
        model = glm::rotate(model, (float)obj.rotation.z, glm::vec3(0, 0, 1));
        model = glm::scale(model, glm::vec3(obj.scale.x, obj.scale.y, obj.scale.z));

        GLint modelLoc = glGetUniformLocation(s_shadowDepthProg, "uModel");
        if (modelLoc != -1) {
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        }

        glBindVertexArray(mesh->VAO);
        if (mesh->EBO) {
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->indexCount, GL_UNSIGNED_INT, 0);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)mesh->indexCount);
        }
        glBindVertexArray(0);
    }

    // Restore default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CoreEngine::DrawShadowPassWithMaterials() {
    DrawShadowPass();
}

void CoreEngine::CleanupShadowMap() {
    if (s_shadowDepthTex) {
        glDeleteTextures(1, &s_shadowDepthTex);
        s_shadowDepthTex = 0;
    }
    if (s_shadowDepthRB) {
        glDeleteRenderbuffers(1, &s_shadowDepthRB);
        s_shadowDepthRB = 0;
    }
    if (s_shadowFBO) {
        glDeleteFramebuffers(1, &s_shadowFBO);
        s_shadowFBO = 0;
    }
    if (s_shadowDepthProg) {
        glDeleteProgram(s_shadowDepthProg);
        s_shadowDepthProg = 0;
    }
    s_shadowInited = false;
}

GLuint CoreEngine::GetShadowMapTexture() {
    return s_shadowDepthTex;
}

GLuint CoreEngine::GetShadowMapFBO() {
    return s_shadowFBO;
}

// ── Camera as scene object ────────────────────────────────────────

static CoreEngine::SceneObjectWithMaterial* GetCameraObject() {
    for (auto& obj : s_sceneObjectsWithMat) {
        if (obj.id == s_cameraObjectId) return &obj;
    }
    return nullptr;
}

void CoreEngine::SetCameraId(uint32_t id) { s_cameraObjectId = id; }
uint32_t CoreEngine::GetCameraObjectId() { return s_cameraObjectId; }
bool CoreEngine::IsCameraObjectId(uint32_t id) { return id == s_cameraObjectId; }

// Called from editor to create the camera object
void CoreEngine::CreateCameraObject() {
    if (s_cameraObjectId != 0) return; // already created
    
    auto cubeMesh = CreateBox({1, 1, 1});
    cubeMesh.name = "cube";
    s_cameraObjectId = s_nextSceneObjectId++;

    SceneObjectWithMaterial cam;
    cam.id = s_cameraObjectId;
    cam.name = "Camera";
    cam.mesh = CreateMesh(std::move(cubeMesh));
    cam.position = s_cameraPos;
    cam.scale = {0.3f, 0.3f, 0.3f};
    cam.material.name = "camera_material";
    cam.material.baseColor = glm::vec3(0.2f, 0.6f, 1.0f); // blue
    cam.material.useMaterial = true;
    s_sceneObjectsWithMat.push_back(std::move(cam));
}

// Called when camera object is moved via inspector
void CoreEngine::SyncSceneToCameraObject() {
    auto* cam = GetCameraObject();
    if (!cam) return;
    s_cameraPos = cam->position;
    s_cameraOffset = s_cameraPos - s_cameraTarget;
}

} // namespace CoreEngine
