#include "editor.h"
#include "core/engine.h"
#include "core/asset_loader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <iostream>
#include <cmath>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#endif

static std::string g_lastLoadedFBX;
static bool g_showSceneHierarchy = true;
static bool g_showInspector = true;
static bool g_showStatusBar = true;
static bool g_triggerFileDialog = false;

// Orbit camera state
static double g_prevMouseX = 0;
static double g_prevMouseY = 0;
static bool g_isOrbiting = false;
static bool g_isPanning = false;

namespace Editor {

    static void InitImGui(GLFWwindow* window) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForOpenGL(window, false);
        ImGui_ImplOpenGL3_Init("#version 330");
    }

    static void ShutdownImGui() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

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
        CoreEngine::SetCameraTarget({modelCenter.x, modelCenter.y, modelCenter.z});
    }

    static void RenderSceneHierarchy() {
        ImVec2 mainSize = ImGui::GetMainViewport()->Size;
        ImVec2 mainPos = ImGui::GetMainViewport()->Pos;
        float frameH = ImGui::GetFrameHeight();
        ImVec2 workPos(mainPos.x, mainPos.y + frameH * 3);
        ImVec2 workSize(mainSize.x, mainSize.y - frameH * 4);
        ImVec2 panelSize(280, workSize.y * 0.55f);

        ImGui::SetNextWindowPos(workPos);
        ImGui::SetNextWindowSize(panelSize);
        if (ImGui::Begin("Scene Hierarchy", nullptr)) {
            if (ImGui::Button("Add Cube", ImVec2(-1, 0))) {
                auto* mesh = CoreEngine::GetPrimitiveMesh("cube");
                if (mesh) {
                    auto& obj = CoreEngine::AddToScene("cube_" + std::to_string(CoreEngine::GetNextSceneObjectId()), *mesh);
                    obj.position = {0, 0.5f, 0};
                    obj.scale = {1, 1, 1};
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Plane", ImVec2(-1, 0))) {
                auto* mesh = CoreEngine::GetPrimitiveMesh("plane");
                if (mesh) {
                    auto& obj = CoreEngine::AddToScene("plane_" + std::to_string(CoreEngine::GetNextSceneObjectId()), *mesh);
                    obj.position = {0, -1.0f, 0};
                    obj.scale = {10, 1, 10};
                }
            }

            ImGui::Separator();
            ImGui::Text("Scene Objects");

            auto& scene = CoreEngine::GetSceneObjects();
            uint32_t selectedId = CoreEngine::GetSelectedObjectId();

            if (scene.empty()) {
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "No objects in scene");
            } else {
                for (auto& obj : scene) {
                    bool isSelected = (obj.id == selectedId);
                    
                    ImGui::PushID((int)obj.id);
                    const char* label = obj.name.c_str();
                    bool was_selected = ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_AllowItemOverlap);
                    
                    if (was_selected) {
                        CoreEngine::SelectObject(obj.id);
                    }

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        static char rename_buf[256];
                        strncpy(rename_buf, obj.name.c_str(), sizeof(rename_buf) - 1);
                        rename_buf[sizeof(rename_buf) - 1] = '\0';
                        ImGui::OpenPopup("Rename");

                        // Center camera on double-clicked object
                        CoreEngine::SetCameraTarget({obj.position.x, obj.position.y, obj.position.z});
                    }

                    if (ImGui::BeginPopup("Rename")) {
                        static char buf[256];
                        strncpy(buf, obj.name.c_str(), sizeof(buf) - 1);
                        buf[sizeof(buf) - 1] = '\0';
                        if (ImGui::InputText("##name", buf, sizeof(buf))) {
                            obj.name = buf;
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Clear Scene", ImVec2(-1, 0))) {
                CoreEngine::ClearScene();
            }
        }
        ImGui::End();
    }

    static void RenderInspector() {
        ImVec2 mainSize = ImGui::GetMainViewport()->Size;
        ImVec2 mainPos = ImGui::GetMainViewport()->Pos;
        float frameH = ImGui::GetFrameHeight();
        ImVec2 workPos(mainPos.x, mainPos.y + frameH * 3);
        ImVec2 workSize(mainSize.x, mainSize.y - frameH * 4);
        ImVec2 panelSize(320, workSize.y * 0.55f);

        ImVec2 inspectorPos(workPos.x + 280, workPos.y);
        ImGui::SetNextWindowPos(inspectorPos);
        ImGui::SetNextWindowSize(panelSize);
        if (ImGui::Begin("Inspector", nullptr)) {
            auto& scene = CoreEngine::GetSceneObjects();
            uint32_t selectedId = CoreEngine::GetSelectedObjectId();
            CoreEngine::SceneObject* selected = nullptr;

            for (auto& obj : scene) {
                if (obj.id == selectedId) {
                    selected = &obj;
                    break;
                }
            }

            if (!selected) {
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "No object selected");
                ImGui::Separator();
            } else {
                char name_buf[256];
                strncpy(name_buf, selected->name.c_str(), sizeof(name_buf) - 1);
                name_buf[sizeof(name_buf) - 1] = '\0';
                if (ImGui::InputText("Object Name", name_buf, sizeof(name_buf))) {
                    selected->name = name_buf;
                }

                ImGui::Separator();
                ImGui::Text("Transform");
                ImGui::PushID((int)selected->id);

                ImGui::InputFloat3("Position", &selected->position.x);
                ImGui::Spacing();

                float rotDeg[3] = {
                    selected->rotation.x * 180.0f / 3.14159265f,
                    selected->rotation.y * 180.0f / 3.14159265f,
                    selected->rotation.z * 180.0f / 3.14159265f
                };
                if (ImGui::InputFloat3("Rotation (deg)", rotDeg, "%0.1f")) {
                    selected->rotation.x = rotDeg[0] * 3.14159265f / 180.0f;
                    selected->rotation.y = rotDeg[1] * 3.14159265f / 180.0f;
                    selected->rotation.z = rotDeg[2] * 3.14159265f / 180.0f;
                }

                ImGui::Spacing();

                if (ImGui::InputFloat3("Scale", &selected->scale.x)) {
                    float* vals[] = {&selected->scale.x, &selected->scale.y, &selected->scale.z};
                    for (int i = 0; i < 3; ++i) {
                        if (fabsf(*vals[i]) < 0.001f) {
                            *vals[i] = 0.001f;
                        }
                    }
                }

                ImGui::PopID();
                ImGui::Separator();

                ImGui::Text("Mesh: %s", selected->mesh.name.c_str());
                ImGui::Text("Indices: %d", selected->mesh.indexCount);
                ImGui::Text("VAO: %u", selected->mesh.VAO);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
                if (ImGui::Button("Delete Object", ImVec2(-1, 0))) {
                    CoreEngine::RemoveFromScene(selected->id);
                }
                ImGui::PopStyleColor(2);
            }
        }
        ImGui::End();
    }

    static void RenderStatusBar() {
        ImVec2 mainSize = ImGui::GetMainViewport()->Size;
        ImVec2 mainPos = ImGui::GetMainViewport()->Pos;
        float barH = 30;
        ImVec2 barPos(mainPos.x, mainPos.y + mainSize.y - barH);
        ImVec2 barSize(mainSize.x, barH);

        ImGui::SetNextWindowPos(barPos);
        ImGui::SetNextWindowSize(barSize);
        if (ImGui::Begin("StatusBar", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoInputs)) {
            auto& scene = CoreEngine::GetSceneObjects();
            char fpsText[128];
            snprintf(fpsText, sizeof(fpsText), "ShadowEngine v2.0 | Objects: %d | FPS: %.1f",
                (int)scene.size(), ImGui::GetIO().Framerate);
            ImGui::Text(fpsText);
            ImGui::SameLine(ImGui::GetWindowWidth() - 200);
            ImGui::Text("L - Load FBX | W/A/S/D - Rotate selected");
        }
        ImGui::End();
    }

    static void RenderImGui(GLFWwindow* window) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Menu bar
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Load FBX", "L")) {
                    g_triggerFileDialog = true;
                }
                if (ImGui::MenuItem("Exit")) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Scene Hierarchy", nullptr, &g_showSceneHierarchy);
                ImGui::MenuItem("Inspector", nullptr, &g_showInspector);
                ImGui::MenuItem("Status Bar", nullptr, &g_showStatusBar);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (g_showSceneHierarchy) {
            RenderSceneHierarchy();
        }
        if (g_showInspector) {
            RenderInspector();
        }
        if (g_showStatusBar) {
            RenderStatusBar();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    static void LoadFBXFromFileDialog(GLFWwindow* window) {
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

        InitImGui(CoreEngine::GetWindow());

        glfwSetInputMode(CoreEngine::GetWindow(), GLFW_REPEAT, GLFW_TRUE);
        glfwFocusWindow(CoreEngine::GetWindow());

        glfwSetKeyCallback(CoreEngine::GetWindow(), [](GLFWwindow* w, int key, int scancode, int action, int mods) {
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                float speed = 0.05f;
                auto& scene = CoreEngine::GetSceneObjects();
                uint32_t selectedId = CoreEngine::GetSelectedObjectId();
                for (auto& obj : scene) {
                    if (obj.id == selectedId && obj.name.find("cube") != std::string::npos) {
                        if (key == GLFW_KEY_A) obj.rotation.y += speed;
                        if (key == GLFW_KEY_D) obj.rotation.y -= speed;
                        if (key == GLFW_KEY_S) obj.rotation.x += speed;
                        if (key == GLFW_KEY_W) obj.rotation.x -= speed;
                    }
                }
            }
            if ((key == GLFW_KEY_L) && action == GLFW_PRESS) {
                g_triggerFileDialog = true;
            }
        });

        auto win = CoreEngine::GetWindow();
        glfwGetCursorPos(win, &g_prevMouseX, &g_prevMouseY);
        glfwSetMouseButtonCallback(win, [](GLFWwindow* w, int btn, int action, int mods) {
            ImGui_ImplGlfw_MouseButtonCallback(w, btn, action, mods);
            if (action == GLFW_PRESS) {
                double mx, my;
                glfwGetCursorPos(w, &mx, &my);
                g_prevMouseX = mx;
                g_prevMouseY = my;
                if (btn == GLFW_MOUSE_BUTTON_LEFT) g_isOrbiting = true;
                if (btn == GLFW_MOUSE_BUTTON_RIGHT) g_isPanning = true;
            } else {
                if (btn == GLFW_MOUSE_BUTTON_LEFT) g_isOrbiting = false;
                if (btn == GLFW_MOUSE_BUTTON_RIGHT) g_isPanning = false;
            }
        });

        glfwSetScrollCallback(win, [](GLFWwindow* w, double dx, double dy) {
            ImGui_ImplGlfw_ScrollCallback(w, dx, dy);
            auto offset = CoreEngine::GetCameraOffset();
            float zoom = 1.0f + (float)dy * 0.05f;
            if (zoom < 0.1f) zoom = 0.1f;
            if (zoom > 50.0f) zoom = 50.0f;
            offset.x *= zoom;
            offset.y *= zoom;
            offset.z *= zoom;
            CoreEngine::SetCameraOffset(offset);
        });

        return CoreEngine::GetWindow();
    }

    void ShutDown(GLFWwindow* window) {
        CoreEngine::ClearScene();
        ShutdownImGui();
    }

    bool IsRunning(GLFWwindow* window) {
        return !CoreEngine::ShouldClose();
    }

    void RenderFrame(GLFWwindow* window) {
        if (g_triggerFileDialog) {
            g_triggerFileDialog = false;
            LoadFBXFromFileDialog(window);
        }

        CoreEngine::RenderBegin();

        auto& sceneObjs = CoreEngine::GetSceneObjects();

        bool hasGround = false;
        for (const auto& obj : sceneObjs) {
            if (obj.name == "ground") { hasGround = true; break; }
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
        auto cameraTarget = CoreEngine::GetCameraTarget();

        // Apply orbit rotation from mouse delta (skip when ImGui has mouse)
        double mx, my;
        glfwGetCursorPos(CoreEngine::GetWindow(), &mx, &my);
        bool wantCaptureMouse = ImGui::GetIO().WantCaptureMouse;
        if (g_isOrbiting && !wantCaptureMouse) {
            float dx = (float)(mx - g_prevMouseX);
            float dy = (float)(my - g_prevMouseY);
            if (dx != 0.0f || dy != 0.0f) {
                auto offset = CoreEngine::GetCameraOffset();
                glm::vec3 off(offset.x, offset.y, offset.z);
                float dist = glm::length(off);
                if (dist < 0.001f) dist = 0.001f;

                float theta = atan2f(off.x, off.z);
                float phi = acosf(glm::clamp(off.y / dist, -1.0f, 1.0f));

                theta -= dx * 0.003f;
                phi -= dy * 0.003f;
                phi = glm::clamp(phi, 0.01f, glm::pi<float>() - 0.01f);

                glm::vec3 newOffset(dist * sinf(phi) * sinf(theta),
                                    dist * cosf(phi),
                                    dist * sinf(phi) * cosf(theta));
                CoreEngine::SetCameraOffset(newOffset);
            }
            g_prevMouseX = mx;
            g_prevMouseY = my;
        }

        // Compute camera position from target + offset
        glm::vec3 camPos(cameraTarget.x - (float)CoreEngine::GetCameraOffset().x,
                         cameraTarget.y - (float)CoreEngine::GetCameraOffset().y,
                         cameraTarget.z - (float)CoreEngine::GetCameraOffset().z);
        glm::vec3 camTarget(cameraTarget.x, cameraTarget.y, cameraTarget.z);
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

        RenderImGui(window);
        CoreEngine::RenderEnd();
    }

    void PollEvents(GLFWwindow* window) {
        glfwPollEvents();
    }

}  // namespace Editor
