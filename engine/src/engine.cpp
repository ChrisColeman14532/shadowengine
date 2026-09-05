// Engine core: lifecycle (Init/Shutdown), engine info, and the
// window/renderer (GLFW window, GLEW, per-frame clear/swap).

#include "core/engine.h"

#include <GLFW/glfw3.h>
#include <cstdio>

#include "engine_internal.h"

namespace CoreEngine {

// ── Engine lifecycle ────────────────────────────────────────────────

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

} // namespace CoreEngine
