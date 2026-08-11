#include "editor.h"
#include "core/engine.h"
#include "core/asset_loader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>

#if defined(_WIN32)
#include <windows.h>
#endif

static std::string g_lastLoadedFBX;

static GLFWwindow* s_window = nullptr;

namespace Editor {

    static void LoadFBXAtPath(const std::string& path) {
        AssetLoader::ClearAll();

        CoreEngine::FBXModel model = AssetLoader::LoadFBX(path);
        if (!model.success) {
            printf("[Editor] Failed to load FBX '%s'\n", path.c_str());
            return;
        }

        glm::vec3 modelCenter(0, 0, 0);
        glm::vec3 modelExtent(1, 1, 1);
        AssetLoader::ComputeModelAABB(model, modelCenter, modelExtent);

        auto* win = CoreEngine::GetWindow();
        int w = 1280, h = 720;
        glfwGetFramebufferSize(win, &w, &h);

        CoreEngine::ClearScene();

        CoreEngine::AddToScene("ground", *CoreEngine::GetPrimitiveMesh("plane"));
        auto& ground = CoreEngine::GetSceneObjects().back();
        ground.position = {0, -2.0f, 0};
        ground.scale = {10, 1, 10};

        CoreEngine::PrimitiveMesh merged = AssetLoader::MergeFromModel(model);
        CoreEngine::AddToScene("loaded_model", merged);

        float maxX = fmaxf(modelExtent.x, modelExtent.y);
        float maxDim = fmaxf(maxX, modelExtent.z);
        for (int i = 0; i < 3; ++i) {
            if (modelExtent[i] < 0.001f) modelExtent[i] = 1.0f;
        }
        maxDim = fmaxf(fmaxf(modelExtent.x, modelExtent.y), modelExtent.z);
        float dist = maxDim * 4.0f;
        if (dist < 5.0f) dist = 5.0f;
        glm::vec3 camPos(modelCenter.x, modelCenter.y + dist * 0.3f, modelCenter.z - dist);
        CoreEngine::SetCameraPosition({camPos.x, camPos.y, camPos.z});
        CoreEngine::SetCameraDirection({modelCenter.x, modelCenter.y, modelCenter.z});
    }

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
            if ((key == GLFW_KEY_L) && action == GLFW_PRESS) {
#if defined(_WIN32)
                char filePath[MAX_PATH] = {0};
                OPENFILENAME ofn = {0};
                ofn.lStructSize = sizeof(OPENFILENAME);
                ofn.hwndOwner = 0;
                ofn.lpstrFilter = "FBX Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = filePath;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileName(&ofn)) {
                    g_lastLoadedFBX = filePath;
                    LoadFBXAtPath(filePath);
                }
#else
                printf("[Editor] Press F1 to select an FBX file (native file dialog not implemented on this platform)\n");
#endif
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

        auto& sceneObjs = CoreEngine::GetSceneObjects();

        // Ensure ground plane exists
        bool hasGround = false;
        for (const auto& obj : sceneObjs) {
            if (obj.mesh.name == "ground") { hasGround = true; break; }
        }
        if (!hasGround) {
            auto* planeMesh = CoreEngine::GetPrimitiveMesh("plane");
            if (planeMesh) {
                CoreEngine::AddToScene("ground", *planeMesh);
                auto& plane = CoreEngine::GetSceneObjects().back();
                plane.position = {0, -1.0f, 0};
                plane.scale = {10, 1, 10};
            }
        }

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
