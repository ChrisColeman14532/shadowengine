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
in vec3 vNormal;
in vec2 vUV;

out vec4 FragColor;

uniform vec3 uColor;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 normal = normalize(vNormal);
    float diff = max(dot(normal, lightDir), 0.0);
    
    vec3 ambient = vec3(0.5f) * uColor;
    vec3 diffuse = diff * 0.5f * uColor;
    
    vec3 result = ambient + diffuse;
    FragColor = vec4(result, 1.0);
}
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

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
    vWorldPos = (uModel * vec4(aPos, 1.0)).xyz;
}
)";

static const char* material_fs = R"(
#version 330 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;

out vec4 FragColor;

uniform vec3 uBaseColor;
uniform vec3 uEmissiveColor;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAO;

uniform sampler2D uDiffuseTex;
uniform sampler2D uNormalTex;
uniform int uHasDiffuse;
uniform int uHasNormal;

void main() {
    vec3 baseColor = uBaseColor;
    vec3 normal = normalize(vNormal);

    // Sample diffuse texture if available
    if (uHasDiffuse == 1) {
        baseColor *= texture(uDiffuseTex, vUV).rgb;
    }

    // Compute lighting
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 viewDir = normalize(-vWorldPos);

    float NdotL = max(dot(normal, lightDir), 0.0);
    
    // Simple specular with GGX approximation
    vec3 halfDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float spec = pow(NdotH, mix(128.0, 8.0, uRoughness)) * uMetallic;
    
    vec3 ambient = 0.15 * baseColor * uAO;
    vec3 diffuse = NdotL * baseColor;
    vec3 specular = spec * vec3(1.0, 0.98, 0.95) * (0.3 + 0.7 * uMetallic);
    
    vec3 result = (ambient + diffuse + specular) + uEmissiveColor;
    FragColor = vec4(result, 1.0);
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

static CoreEngine::Vector3 s_cameraPos    = {0, 1.5f, 5};
static CoreEngine::Vector3 s_cameraTarget = {0, 0, 0};
static CoreEngine::Vector3 s_cameraOffset = {0, -1.5f, -5};

static std::vector<CoreEngine::SceneObject> s_sceneObjects;
static std::vector<std::shared_ptr<CoreEngine::PrimitiveMesh>> s_primitiveMeshes;
static uint32_t s_nextSceneObjectId = 1;
static uint32_t s_selectedObjectId = 0;

static bool s_engineInited = false;

// Material-based scene objects
static std::vector<CoreEngine::SceneObjectWithMaterial> s_sceneObjectsWithMat;

// Skybox state
static GLuint s_skyboxVBO = 0;
static GLuint s_skyboxVAO = 0;
static GLuint s_skyboxProg = 0;
static bool   s_skyboxInited = false;

// ── Helpers ─────────────────────────────────────────────────────────

static void compileDefaultShader() {
    s_shaderProg = CoreEngine::CreateShaderProgram(default_vs, default_fs);
    glUseProgram(s_shaderProg);

    // Set initial camera uniforms
    auto view = glm::lookAt(
        glm::vec3(s_cameraPos.x, s_cameraPos.y, s_cameraPos.z),
        glm::vec3(s_cameraTarget.x, s_cameraTarget.y, s_cameraTarget.z),
        glm::vec3(0, 1, 0));
    GLint viewLoc = glGetUniformLocation(s_shaderProg, "uView");
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
}

static void buildPrimitiveVAOs() {
    // Drop template refs; GPU handles are freed when the last ref goes away
    s_primitiveMeshes.clear();

    // Build cube mesh data manually (not via CreateBox which is in namespace)
    CoreEngine::PrimitiveMesh cube;
    cube.name = "cube";
    cube.indexCount = 36;

    static const float cubeVerts[] = {
        // Front face (+Z)
        -0.5f, -0.5f,  0.5f,   0,0,1,  0,0,
         0.5f, -0.5f,  0.5f,   0,0,1,  1,0,
         0.5f,  0.5f,  0.5f,   0,0,1,  1,1,
        -0.5f,  0.5f,  0.5f,   0,0,1,  0,1,
        // Back face (-Z)
        -0.5f, -0.5f, -0.5f,   0,0,-1,  0,0,
        -0.5f,  0.5f, -0.5f,   0,0,-1,  1,0,
         0.5f,  0.5f, -0.5f,   0,0,-1,  1,1,
         0.5f, -0.5f, -0.5f,   0,0,-1,  0,1,
        // Top face (+Y)
        -0.5f,  0.5f,  0.5f,   0,1,0,  0,0,
         0.5f,  0.5f,  0.5f,   0,1,0,  1,0,
         0.5f,  0.5f, -0.5f,   0,1,0,  1,1,
        -0.5f,  0.5f, -0.5f,   0,1,0,  0,1,
        // Bottom face (-Y)
        -0.5f, -0.5f, -0.5f,   0,-1,0,  0,0,
         0.5f, -0.5f, -0.5f,   0,-1,0,  1,0,
         0.5f, -0.5f,  0.5f,   0,-1,0,  1,1,
        -0.5f, -0.5f,  0.5f,   0,-1,0,  0,1,
        // Right face (+X)
         0.5f, -0.5f,  0.5f,   1,0,0,  0,0,
         0.5f, -0.5f, -0.5f,   1,0,0,  1,0,
         0.5f,  0.5f, -0.5f,   1,0,0,  1,1,
         0.5f,  0.5f,  0.5f,   1,0,0,  0,1,
        // Left face (-X)
        -0.5f, -0.5f, -0.5f,  -1,0,0,  0,0,
        -0.5f, -0.5f,  0.5f,  -1,0,0,  1,0,
        -0.5f,  0.5f,  0.5f,  -1,0,0,  1,1,
        -0.5f,  0.5f, -0.5f,  -1,0,0,  0,1,
    };
    static const GLuint cubeIndices[] = {
        // Front (+Z): CCW from +Z
        0,1,2, 0,2,3,
        // Back (-Z): CCW from -Z
        4,6,5, 4,7,6,
        // Top (+Y): CCW from +Y
        8,9,10, 8,10,11,
        // Bottom (-Y): CCW from -Y
        12,13,14, 12,14,15,
        // Right (+X): CCW from +X
        16,17,18, 16,18,19,
        // Left (-X): CCW from -X
        20,22,21, 20,23,22
    };

    glGenVertexArrays(1, &cube.VAO);
    glGenBuffers(1, &cube.VBO);
    glGenBuffers(1, &cube.EBO);
    glBindVertexArray(cube.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, cube.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    s_primitiveMeshes.push_back(CoreEngine::CreateMesh(std::move(cube)));

    // Build plane mesh
    CoreEngine::PrimitiveMesh plane;
    plane.name = "plane";
    plane.indexCount = 6;

    static const float planeVerts[] = {
        -5.0f, 0, -5.0f,   0,1,0,  0,0,
         5.0f, 0, -5.0f,   0,1,0,  1,0,
         5.0f, 0,  5.0f,   0,1,0,  1,1,
        -5.0f, 0,  5.0f,   0,1,0,  0,1,
    };
    static const GLuint planeIndices[] = { 0, 1, 2, 0, 2, 3 };

    glGenVertexArrays(1, &plane.VAO);
    glGenBuffers(1, &plane.VBO);
    glGenBuffers(1, &plane.EBO);
    glBindVertexArray(plane.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, plane.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(planeVerts), planeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, plane.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(planeIndices), planeIndices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
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

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSource, nullptr);
    glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        GLint len;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        char* log = (char*)alloca(len);
        glGetProgramInfoLog(prog, len, nullptr, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
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

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

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

    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*8, (void*)(sizeof(float)*6));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

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
    s_sceneObjects.clear();
    s_sceneObjectsWithMat.clear();
}

void ClearSceneWithMaterials() {
    s_selectedObjectId = 0;
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

void SelectObject(uint32_t id) { s_selectedObjectId = id; }

SceneObject* GetSelectedObject() {
    for (auto& obj : s_sceneObjects) {
        if (obj.id == s_selectedObjectId) return &obj;
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

void DrawGrid(int divisions, float unit, float halfExtent) {
    const float step = halfExtent * 2.0f / divisions;
    const float half = divisions * step / 2.0f;
    const int lineCount = (divisions + 1) * 2;

    static std::vector<float> gridVerts;
    static std::vector<float> gridColors;
    static GLuint gridVBO = 0;
    static GLuint gridVAO = 0;
    static int cachedDivisions = 0;

    if (gridVAO == 0 || cachedDivisions != divisions) {
        gridVerts.clear();
        gridColors.clear();

        for (int i = 0; i <= divisions; ++i) {
            float pos = -half + i * step;
            bool isMajor = (i % (divisions / 10)) == 0;
            float color0 = isMajor ? 0.5f : 0.3f;
            float color1 = isMajor ? 0.5f : 0.3f;
            float color2 = isMajor ? 0.5f : 0.3f;

            // X-direction lines
            gridVerts.push_back(-halfExtent); gridVerts.push_back(0.0f); gridVerts.push_back(pos);
            gridVerts.push_back( halfExtent); gridVerts.push_back(0.0f); gridVerts.push_back(pos);
            gridColors.push_back(color0); gridColors.push_back(color1); gridColors.push_back(color2);
            gridColors.push_back(color0); gridColors.push_back(color1); gridColors.push_back(color2);

            // Z-direction lines
            gridVerts.push_back(pos); gridVerts.push_back(0.0f); gridVerts.push_back(-halfExtent);
            gridVerts.push_back(pos); gridVerts.push_back(0.0f); gridVerts.push_back( halfExtent);
            gridColors.push_back(color0); gridColors.push_back(color1); gridColors.push_back(color2);
            gridColors.push_back(color0); gridColors.push_back(color1); gridColors.push_back(color2);
        }

        if (gridVAO) glDeleteVertexArrays(1, &gridVAO);
        if (gridVBO) glDeleteBuffers(1, &gridVBO);
        if (gridColors.size() > 2) {
            glGenVertexArrays(1, &gridVAO);
            glGenBuffers(1, &gridVBO);
            glBindVertexArray(gridVAO);
            glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
            glBufferData(GL_ARRAY_BUFFER,
                gridVerts.size() * sizeof(float) + gridColors.size() * sizeof(float),
                nullptr, GL_STATIC_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, gridVerts.size() * sizeof(float), gridVerts.data());
            glBufferSubData(GL_ARRAY_BUFFER, gridVerts.size() * sizeof(float), gridColors.size() * sizeof(float), gridColors.data());

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)0);
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)(sizeof(float) * 3));
            glBindVertexArray(0);
        }
        cachedDivisions = divisions;
    }

    if (gridVAO) {
        // Use a simple color shader for the grid
        static GLuint gridShaderProg = 0;
        static bool gridShaderInited = false;

        if (!gridShaderInited) {
            const char* g_vs = R"(
                #version 330 core
                layout(location = 0) in vec3 aPos;
                layout(location = 3) in vec3 aColor;
                uniform mat4 uModel;
                uniform mat4 uView;
                uniform mat4 uProjection;
                out vec3 vColor;
                void main() {
                    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
                    vColor = aColor;
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
            gridShaderInited = true;
        }

        CoreEngine::UseShader(gridShaderProg);
        GLint viewLoc = glGetUniformLocation(gridShaderProg, "uView");
        GLint projLoc = glGetUniformLocation(gridShaderProg, "uProjection");
        GLint modelLoc = glGetUniformLocation(gridShaderProg, "uModel");

        auto camPos = s_cameraPos;
        auto camTarget = s_cameraTarget;
        auto view = glm::lookAt(
            glm::vec3(camPos.x, camPos.y, camPos.z),
            glm::vec3(camTarget.x, camTarget.y, camTarget.z),
            glm::vec3(0, 1, 0));
        if (viewLoc != -1) CoreEngine::SetUniformMat4(gridShaderProg, "uView", view);
        if (projLoc != -1) {
            int w = 1280, h = 720;
            GLFWwindow* win = s_window;
            if (win) glfwGetFramebufferSize(win, &w, &h);
            CoreEngine::SetUniformMat4(gridShaderProg, "uProjection",
                glm::perspective(glm::radians(60.0f), (float)w / (float)h, 0.1f, 100.0f));
        }
        if (modelLoc != -1) CoreEngine::SetUniformMat4(gridShaderProg, "uModel", glm::mat4(1.0f));

        glLineWidth(1.0f);
        glBindVertexArray(gridVAO);
        glDisable(GL_DEPTH_TEST);
        glDrawArrays(GL_LINES, 0, (divisions + 1) * 2 * 2);
        glEnable(GL_DEPTH_TEST);
        glBindVertexArray(0);

        // Restore original shader
        if (s_shaderProg) {
            glUseProgram(s_shaderProg);
        }
    }
}

// ── Selected Object Bounds ──────────────────────────────────────────

void DrawSelectedObjectBounds() {
    SceneObject* sel = GetSelectedObject();
    if (!sel || !sel->mesh || sel->mesh->indexCount == 0) return;

    // Determine mesh extents based on mesh name
    float extents[3];
    if (sel->mesh->name == "cube") {
        extents[0] = 0.5f; extents[1] = 0.5f; extents[2] = 0.5f;
    } else if (sel->mesh->name == "plane") {
        extents[0] = 5.0f; extents[1] = 0.01f; extents[2] = 5.0f;
    } else {
        extents[0] = 1.0f; extents[1] = 1.0f; extents[2] = 1.0f;
    }

    // Build model matrix from object transform
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(sel->position.x, sel->position.y, sel->position.z));
    model = glm::rotate(model, (float)sel->rotation.x, glm::vec3(1, 0, 0));
    model = glm::rotate(model, (float)sel->rotation.y, glm::vec3(0, 1, 0));
    model = glm::rotate(model, (float)sel->rotation.z, glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(sel->scale.x, sel->scale.y, sel->scale.z));

    // Compute 8 corners of transformed bounding box
    glm::vec3 boxMin(-extents[0], -extents[1], -extents[2]);
    glm::vec3 boxMax(extents[0], extents[1], extents[2]);
    
    glm::vec3 corners[8] = {
        model * glm::vec4(boxMin, 1.0f),
        model * glm::vec4(boxMax.x, boxMin.y, boxMin.z, 1.0f),
        model * glm::vec4(boxMax.x, boxMax.y, boxMin.z, 1.0f),
        model * glm::vec4(boxMin.x, boxMax.y, boxMin.z, 1.0f),
        model * glm::vec4(boxMin.x, boxMin.y, boxMax.z, 1.0f),
        model * glm::vec4(boxMax.x, boxMin.y, boxMax.z, 1.0f),
        model * glm::vec4(boxMax.x, boxMax.y, boxMax.z, 1.0f),
        model * glm::vec4(boxMin.x, boxMax.y, boxMax.z, 1.0f),
    };

    // 12 edges (2 vertices per edge = 24 verts)
    const int edgePairs[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0}, // bottom
        {4,5}, {5,6}, {6,7}, {7,4}, // top
        {0,4}, {1,5}, {2,6}, {3,7}  // verticals
    };

    float edgeVerts[24 * 3];
    for (int i = 0; i < 12; ++i) {
        edgeVerts[(i*6+0)] = corners[edgePairs[i][0]].x;
        edgeVerts[(i*6+1)] = corners[edgePairs[i][0]].y;
        edgeVerts[(i*6+2)] = corners[edgePairs[i][0]].z;
        edgeVerts[(i*6+3)] = corners[edgePairs[i][1]].x;
        edgeVerts[(i*6+4)] = corners[edgePairs[i][1]].y;
        edgeVerts[(i*6+5)] = corners[edgePairs[i][1]].z;
    }

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

    CoreEngine::UseShader(boundsShaderProg);
    GLint viewLoc = glGetUniformLocation(boundsShaderProg, "uView");
    GLint projLoc = glGetUniformLocation(boundsShaderProg, "uProjection");
    GLint modelLoc = glGetUniformLocation(boundsShaderProg, "uModel");

    auto camPos = s_cameraPos;
    auto camTarget = s_cameraTarget;
    auto view = glm::lookAt(
        glm::vec3(camPos.x, camPos.y, camPos.z),
        glm::vec3(camTarget.x, camTarget.y, camTarget.z),
        glm::vec3(0, 1, 0));
    if (viewLoc != -1) CoreEngine::SetUniformMat4(boundsShaderProg, "uView", view);
    if (projLoc != -1) {
        int w = 1280, h = 720;
        GLFWwindow* win = s_window;
        if (win) glfwGetFramebufferSize(win, &w, &h);
        CoreEngine::SetUniformMat4(boundsShaderProg, "uProjection",
            glm::perspective(glm::radians(60.0f), (float)w / (float)h, 0.1f, 100.0f));
    }
    if (modelLoc != -1) CoreEngine::SetUniformMat4(boundsShaderProg, "uModel", glm::mat4(1.0f));

    glLineWidth(2.0f);
    glBindVertexArray(boundsVAO);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_LINES, 0, 24);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (s_shaderProg) glUseProgram(s_shaderProg);
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

void CoreEngine::DrawSkybox() {
    if (!s_skyboxInited || !s_window) return;

    // Get camera position and projection
    auto camPos = s_cameraPos;
    auto camTarget = s_cameraTarget;
    glm::mat4 view = glm::lookAt(
        glm::vec3(camPos.x, camPos.y, camPos.z),
        glm::vec3(camTarget.x, camTarget.y, camTarget.z),
        glm::vec3(0, 1, 0));

    int w = 1280, h = 720;
    glfwGetFramebufferSize(s_window, &w, &h);
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)w / (float)h, 0.1f, 100.0f);

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
        nullptr, nullptr,
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

} // namespace CoreEngine
