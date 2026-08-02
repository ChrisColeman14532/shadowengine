#pragma once

#include <string>

struct GLFWwindow;

namespace CoreEngine {

    struct Vector2 {
        float x, y;
        Vector2() : x(0), y(0) {}
        Vector2(float xx, float yy) : x(xx), y(yy) {}
    };

    struct EngineInfo {
        int majorVersion;
        int minorVersion;
        std::string name;
    };

    static constexpr const char* ENGINE_NAME = "Test3Engine";
    static constexpr int VERSION_MAJOR = 1;
    static constexpr int VERSION_MINOR = 0;

    void Init();
    void Shutdown();
    
    std::string GetEngineName();
    void GetVersion(int& major, int& minor);
    EngineInfo GetEngineInfo(int width, int height);
    
    bool InitRenderer(const char* title, int width, int height);
    GLFWwindow* GetWindow();
    void RenderBegin();
    void RenderEnd();
    bool ShouldClose();

}  // namespace CoreEngine
