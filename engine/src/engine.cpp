#include "core/engine.h"
#include <GLFW/glfw3.h>
#include <iostream>

namespace CoreEngine {

    static GLFWwindow* s_window = nullptr;
    static int s_width = 1280;
    static int s_height = 720;

    void Init() {
        if (!glfwInit()) {
            std::cerr << "[CoreEngine] Failed to initialize GLFW" << std::endl;
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }

    void Shutdown() {
        std::cout << "[CoreEngine] Shutting down " << GetEngineName() << " v" 
                  << VERSION_MAJOR << "." << VERSION_MINOR << std::endl;
        if (s_window) glfwDestroyWindow(s_window);
        glfwTerminate();
    }

    std::string GetEngineName() {
        return ENGINE_NAME;
    }

    void GetVersion(int& major, int& minor) {
        major = VERSION_MAJOR;
        minor = VERSION_MINOR;
    }

    EngineInfo GetEngineInfo(int width, int height) {
        EngineInfo info;
        info.majorVersion = VERSION_MAJOR;
        info.minorVersion = VERSION_MINOR;
        info.name = ENGINE_NAME;
        return info;
    }

    bool InitRenderer(const char* title, int width, int height) {
        s_width = width;
        s_height = height;
        
        s_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!s_window) {
            std::cerr << "[CoreEngine] Failed to create window" << std::endl;
            return false;
        }
        glfwMakeContextCurrent(s_window);

        std::cout << "[CoreEngine/Renderer] OpenGL " 
                  << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << std::endl;
        
        glEnable(GL_DEPTH_TEST);
        glViewport(0, 0, width, height);
        
        return true;
    }

    GLFWwindow* GetWindow() {
        return s_window;
    }

    void RenderBegin() {
        glClearColor(0.52f, 0.81f, 0.92f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void RenderEnd() {
        if (s_window) glfwSwapBuffers(s_window);
    }

    bool ShouldClose() {
        return s_window ? glfwWindowShouldClose(s_window) : true;
    }

}  // namespace CoreEngine
