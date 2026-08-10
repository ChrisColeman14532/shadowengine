#include "editor.h"
#include "core/engine.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

static CoreEngine::SceneObject* g_cube = nullptr;

static GLFWwindow* s_window = nullptr;

namespace Editor {

    GLFWwindow* Init() {
        CoreEngine::Init();

        std::string name = CoreEngine::GetEngineName();
        int major, minor;
        CoreEngine::GetVersion(major, minor);
        std::cout << "[Editor] Starting " << name << " v" << major << "." << minor << std::endl;

        const char* title = name.c_str();
        int width = 1280;
        int height = 720;

        if (!CoreEngine::InitRenderer(title, width, height)) {
            return nullptr;
        }

        std::cout << "[Editor] OpenGL window created (" << width << "x" << height << ")" << std::endl;

        CoreEngine::EngineInfo info = CoreEngine::GetEngineInfo(width, height);
        std::cout << "[Editor] Engine communication OK - \"" << info.name 
                  << "\" v" << info.majorVersion << "." << info.minorVersion << std::endl;

        // Set up camera orbit controls
        auto* win = CoreEngine::GetWindow();
        // Enable key repeat for continuous rotation
        glfwSetInputMode(win, GLFW_REPEAT, GLFW_TRUE);

        std::cout << "[Editor] Key callback registered" << std::endl;
        glfwFocusWindow(win);

        glfwSetKeyCallback(win, [](GLFWwindow* w, int key, int scancode, int action, int mods) {
            std::cout << "[Editor] Key " << key << " action " << action << std::endl;
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                float speed = 0.05f;
                auto& scene = CoreEngine::GetSceneObjects();
                for (auto& obj : scene) {
                    if (obj.mesh.name == "default_cube") {
                        if (key == GLFW_KEY_A) obj.rotation.y += speed;
                        if (key == GLFW_KEY_D) obj.rotation.y -= speed;
                        if (key == GLFW_KEY_S) obj.rotation.x += speed;
                        if (key == GLFW_KEY_W) obj.rotation.x -= speed;
                    }
                }
            }
        });

        return CoreEngine::GetWindow();
    }

    void ShutDown(GLFWwindow* window) {
        CoreEngine::Shutdown();
    }

    bool IsRunning(GLFWwindow* window) {
        return !CoreEngine::ShouldClose();
    }

    void RenderFrame(GLFWwindow* window) {
        static bool s_defaultSceneAdded = false;

        CoreEngine::RenderBegin();

        auto& scene = CoreEngine::GetSceneObjects();
        if (!s_defaultSceneAdded && scene.empty()) {
            auto* cubeMesh = CoreEngine::GetPrimitiveMesh("cube");
            auto* planeMesh = CoreEngine::GetPrimitiveMesh("plane");
            if (cubeMesh && planeMesh) {
                CoreEngine::AddToScene("default_cube", *cubeMesh);
                auto& cube = CoreEngine::GetSceneObjects().back();
                cube.position = {0, 0.5f, 0};
                cube.scale = {1, 1, 1};  // Non-symmetric so rotation is visible
                cube.rotation = {0.5f, 0.3f, 0};  // Initial rotation to make rotation visible

                CoreEngine::AddToScene("ground", *planeMesh);
                auto& plane = CoreEngine::GetSceneObjects().back();
                plane.position = {0, -1.0f, 0};
                plane.scale = {10, 1, 10};
            }
            s_defaultSceneAdded = true;
        }

        auto& sceneObjs = CoreEngine::GetSceneObjects();
        auto cameraPos = CoreEngine::GetCameraPosition();
        auto cameraDir = CoreEngine::GetCameraDirection();

        glm::vec3 camPos(cameraPos.x, cameraPos.y, cameraPos.z);
        glm::vec3 camTarget = camPos + glm::vec3(cameraDir.x, cameraDir.y, cameraDir.z);
        glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0, 1, 0));

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glm::mat4 projection = CoreEngine::GetProjectionMatrix(60.0f, (float)w / (float)h);

        GLuint prog = CoreEngine::GetShaderProgram();
        GLint viewLoc = glGetUniformLocation(prog, "uView");
        GLint projLoc = glGetUniformLocation(prog, "uProjection");
        if (viewLoc != -1) CoreEngine::SetUniformMat4(prog, "uView", view);
        if (projLoc != -1) CoreEngine::SetUniformMat4(prog, "uProjection", projection);

        for (auto& obj : sceneObjs) {
            auto& mesh = obj.mesh;
            if (!mesh.VAO || mesh.indexCount == 0) continue;

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(obj.position.x, obj.position.y, obj.position.z));
            model = glm::rotate(model, (float)obj.rotation.x, glm::vec3(1, 0, 0));
            model = glm::rotate(model, (float)obj.rotation.y, glm::vec3(0, 1, 0));
            model = glm::rotate(model, (float)obj.rotation.z, glm::vec3(0, 0, 1));
            model = glm::scale(model, glm::vec3(obj.scale.x, obj.scale.y, obj.scale.z));

            CoreEngine::SetUniformMat4(prog, "uModel", model);
            glm::vec3 color(0.4f + obj.position.x * 0.05f,
                            0.4f + obj.position.y * 0.05f,
                            0.4f + obj.position.z * 0.05f);
            CoreEngine::SetUniformVec3(prog, "uColor", color);

            glBindVertexArray(mesh.VAO);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
        }

        CoreEngine::RenderEnd();
    }

    void PollEvents(GLFWwindow* window) {
        glfwPollEvents();
    }

}  // namespace Editor
