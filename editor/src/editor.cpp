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
static bool g_showStatusBar = false;
static bool g_triggerFileDialog = false;
static bool g_showShadows = false;  // DISABLED for debugging

// Console log storage
static std::vector<std::string> g_consoleLog;
static std::string   g_consoleText; // assembled text for copy

static void ConsoleLog(const std::string& msg) {
    g_consoleLog.push_back(msg);
    if (g_consoleLog.size() > 1000) g_consoleLog.erase(g_consoleLog.begin());
}



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

        CoreEngine::ClearSceneWithMaterials();
        CoreEngine::CreateCameraObject();  // Restore camera after clearing scene

        auto loadedMat = CoreEngine::CreateDefaultMaterial();
        loadedMat.name = "loaded_model_material";
        loadedMat.baseColor = glm::vec3(0.7f, 0.7f, 0.7f);
        loadedMat.roughness = 0.8f;
        loadedMat.metallic = 0.2f;
        CoreEngine::AddToSceneWithMaterial("loaded_model", CoreEngine::CreateMesh(AssetLoader::MergeFromModel(model)), loadedMat);

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
    }

    static void RenderSceneHierarchy() {
        ImVec2 mainSize = ImGui::GetMainViewport()->Size;
        ImVec2 mainPos = ImGui::GetMainViewport()->Pos;

        const float menuBarH = 28.0f;
        const float consoleH = 150.0f;
        const float panelH = mainSize.y - menuBarH - consoleH;

        ImGui::SetNextWindowPos(ImVec2(mainPos.x + 0, mainPos.y + menuBarH));
        ImGui::SetNextWindowSize(ImVec2(280, panelH));
        if (ImGui::Begin("Scene Hierarchy", nullptr)) {
            // ── Add Object Dropdown ─────────────────────────────────────
            static const char* addItemOptions[] = {"Add Cube", "Add Plane", "Add Camera"};
            static int selectedItem = -1;

            if (ImGui::Combo("##addItem", &selectedItem, addItemOptions, IM_ARRAYSIZE(addItemOptions))) {
                auto meshCube = CoreEngine::GetPrimitiveMesh("cube");
                auto meshPlane = CoreEngine::GetPrimitiveMesh("plane");
                auto& scene = CoreEngine::GetSceneObjectsWithMaterials();
                uint32_t nextId = CoreEngine::GetNextSceneObjectId();

                switch (selectedItem) {
                    case 0: { // Add Cube
                        if (meshCube) {
                            auto mat = CoreEngine::CreateDefaultMaterial();
                            mat.name = "cube_material";
                            auto& obj = CoreEngine::AddToSceneWithMaterial("cube_" + std::to_string(nextId), meshCube, mat);
                            obj.position = {0, 0, 0};
                            obj.scale = {1, 1, 1};
                            ConsoleLog("Added cube");
                        }
                        break;
                    }
                    case 1: { // Add Plane
                        if (meshPlane) {
                            auto mat = CoreEngine::CreateDefaultMaterial();
                            mat.name = "plane_material";
                            auto& obj = CoreEngine::AddToSceneWithMaterial("plane_" + std::to_string(nextId), meshPlane, mat);
                            obj.position = {0, -1.0f, 0};
                            obj.scale = {10, 1, 10};
                            ConsoleLog("Added plane");
                        }
                        break;
                    }
                    case 2: { // Add Camera
                        CoreEngine::CreateCameraObject();
                        ConsoleLog("Added camera");
                        break;
                    }
                }
                selectedItem = -1; // Reset dropdown
            }

            ImGui::Separator();
            ImGui::Text("Scene Objects");

            auto& scene = CoreEngine::GetSceneObjectsWithMaterials();
            uint32_t selectedId = CoreEngine::GetSelectedObjectId();

            if (scene.empty()) {
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "No objects in scene");
            } else {
                // Track which object to rename (popup must be handled OUTSIDE the loop)
                static CoreEngine::SceneObjectWithMaterial* g_renameObj = nullptr;

                for (auto& obj : scene) {
                    bool isSelected = (obj.id == selectedId);

                    ImGui::PushID((int)obj.id);
                    const char* label = obj.name.c_str();
                    bool was_selected = ImGui::Selectable(label, isSelected);

                    if (was_selected) {
                        CoreEngine::SelectObject(obj.id);
                    }

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        g_renameObj = &obj;
                        ImGui::OpenPopup("Rename");
                        // Select object on double-click too
                        CoreEngine::SelectObject(obj.id);
                    }

                    ImGui::PopID();
                }

                // Handle rename popup ONCE per frame (must be outside the loop)
                if (ImGui::BeginPopup("Rename") && g_renameObj) {
                    static char buf[256];
                    strncpy(buf, g_renameObj->name.c_str(), sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                    if (ImGui::InputText("##name", buf, sizeof(buf))) {
                        g_renameObj->name = buf;
                    }
                    ImGui::EndPopup();
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Clear Scene", ImVec2(-1, 0))) {
                CoreEngine::ClearSceneWithMaterials();
                CoreEngine::GetSceneObjectsWithMaterials().clear();
                ConsoleLog("Scene cleared");
            }
        }
        ImGui::End();
    }

    static void RenderInspector() {
        ImVec2 mainSize = ImGui::GetMainViewport()->Size;
        ImVec2 mainPos = ImGui::GetMainViewport()->Pos;

        const float menuBarH = 28.0f;
        const float consoleH = 150.0f;
        const float panelH = mainSize.y - menuBarH - consoleH;

        ImVec2 inspectorPos = ImVec2(mainPos.x + mainSize.x - 320, mainPos.y + menuBarH);
        ImGui::SetNextWindowPos(inspectorPos);
        ImGui::SetNextWindowSize(ImVec2(320, panelH));
        if (ImGui::Begin("Inspector", nullptr)) {
            auto& scene = CoreEngine::GetSceneObjectsWithMaterials();
            uint32_t selectedId = CoreEngine::GetSelectedObjectId();
            CoreEngine::SceneObjectWithMaterial* selected = nullptr;

            for (auto& obj : scene) {
                if (obj.id == selectedId) {
                    selected = &obj;
                    break;
                }
            }

            // ── Camera object selected ──────────────────────────────────
            bool isCamera = CoreEngine::IsCameraObjectId(selectedId);

            if (!selected) {
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "No object selected");
                ImGui::Separator();
            } else if (isCamera) {
                // Camera-specific inspector
                char name_buf[256];
                strncpy(name_buf, selected->name.c_str(), sizeof(name_buf) - 1);
                name_buf[sizeof(name_buf) - 1] = '\0';
                ImGui::TextColored(ImVec4(0.2f, 0.7f, 1.0f, 1.0f), "Camera");
                ImGui::InputText("##name", name_buf, sizeof(name_buf));
                if (ImGui::IsItemDeactivatedAfterEdit()) selected->name = name_buf;

                ImGui::Separator();
                ImGui::Text("Position");
                static float camPos[3] = {0, 1.5f, 5};
                // Keep input in sync with object position
                camPos[0] = selected->position.x;
                camPos[1] = selected->position.y;
                camPos[2] = selected->position.z;
                if (ImGui::InputFloat3("##pos", camPos, "%0.2f")) {
                    if (camPos[0] != selected->position.x || camPos[1] != selected->position.y || camPos[2] != selected->position.z) {
                        selected->position = {camPos[0], camPos[1], camPos[2]};
                        // Update orbit camera to follow
                        CoreEngine::SyncSceneToCameraObject();
                    }
                }

                ImGui::Separator();
                ImGui::Text("Scale");
                static float camScale[3] = {0.3f, 0.3f, 0.3f};
                camScale[0] = selected->scale.x;
                camScale[1] = selected->scale.y;
                camScale[2] = selected->scale.z;
                if (ImGui::InputFloat3("##scale", camScale, "%0.2f")) {
                    selected->scale = {camScale[0], camScale[1], camScale[2]};
                }

                ImGui::Separator();
                ImGui::Text("Light Dir");
                static float lightDir[3] = {0.5f, 1.0f, 0.3f};
                if (ImGui::SliderFloat3("##lightDir", lightDir, -2.0f, 2.0f)) {
                    float len = sqrtf(lightDir[0]*lightDir[0] + lightDir[1]*lightDir[1] + lightDir[2]*lightDir[2]);
                    if (len > 0.001f) CoreEngine::SetShadowLightDirection({lightDir[0]/len, lightDir[1]/len, lightDir[2]/len});
                }

                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button("Delete Camera", ImVec2(-1, 0))) {
                    // Can't delete camera, just deselect
                    CoreEngine::SelectObject(0);
                }
                ImGui::PopStyleColor();

            } else {
                // Regular object inspector
                char name_buf[256];
                strncpy(name_buf, selected->name.c_str(), sizeof(name_buf) - 1);
                name_buf[sizeof(name_buf) - 1] = '\0';
                if (ImGui::InputText("Object Name", name_buf, sizeof(name_buf))) {
                    selected->name = name_buf;
                }

                ImGui::Separator();
                ImGui::Text("Transform");
                ImGui::PushID((int)selected->id);

                // When camera object moved via inspector, update orbit camera
                if (isCamera) CoreEngine::SyncSceneToCameraObject();

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

                // ── Material section ──
                ImGui::Text("Material");
                ImGui::Checkbox("Use Material", &selected->material.useMaterial);

                if (selected->material.useMaterial) {
                    char matNameBuf[256];
                    strncpy(matNameBuf, selected->material.name.c_str(), sizeof(matNameBuf) - 1);
                    matNameBuf[sizeof(matNameBuf) - 1] = '\0';
                    ImGui::InputText("Material Name", matNameBuf, sizeof(matNameBuf));
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        selected->material.name = matNameBuf;
                    }

                    ImGui::Separator();
                    ImGui::Text("Base Color");
                    float bc[3] = {selected->material.baseColor.r, selected->material.baseColor.g, selected->material.baseColor.b};
                    if (ImGui::ColorEdit3("##baseColor", bc, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf)) {
                        selected->material.baseColor = glm::vec3(bc[0], bc[1], bc[2]);
                    }

                    ImGui::Separator();
                    ImGui::Text("Diffuse Texture");
                    if (selected->material.diffuseTexture && selected->material.diffuseTexture->id) {
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Loaded (%dx%d)", 
                            selected->material.diffuseTexture->width, 
                            selected->material.diffuseTexture->height);
                        if (ImGui::SmallButton("Unload Texture")) {
                            CoreEngine::DestroyTexture(*selected->material.diffuseTexture);
                            selected->material.diffuseTexture = nullptr;
                        }
                    } else {
                        if (ImGui::Button("Load Texture...", ImVec2(-1, 0))) {
                            // Simple file picker - try common paths
                            static char texPath[MAX_PATH] = {0};
                            if (ImGui::InputText("##texPath", texPath, sizeof(texPath))) {
                                if (selected->material.diffuseTexture && selected->material.diffuseTexture->id) {
                                    // Already has a texture, destroy it
                                    CoreEngine::DestroyTexture(*selected->material.diffuseTexture);
                                    selected->material.diffuseTexture = nullptr;
                                }
                                selected->material.diffuseTexture = new CoreEngine::Texture();
                                *selected->material.diffuseTexture = CoreEngine::LoadTexture(texPath);
                                if (!selected->material.diffuseTexture->id) {
                                    delete selected->material.diffuseTexture;
                                    selected->material.diffuseTexture = nullptr;
                                }
                                texPath[0] = '\0';
                            }
                        }
                    }

                    ImGui::Separator();
                    ImGui::Text("PBR Properties");
                    ImGui::SliderFloat("Metallic", &selected->material.metallic, 0.0f, 1.0f);
                    ImGui::SliderFloat("Roughness", &selected->material.roughness, 0.0f, 1.0f);
                    ImGui::SliderFloat("AO", &selected->material.ao, 0.0f, 1.0f);

                    ImGui::Separator();
                    ImGui::Text("Emissive");
                    float ec[3] = {selected->material.emissiveColor.r, selected->material.emissiveColor.g, selected->material.emissiveColor.b};
                    if (ImGui::ColorEdit3("##emissive", ec, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf)) {
                        selected->material.emissiveColor = glm::vec3(ec[0], ec[1], ec[2]);
                    }
                }

                ImGui::Separator();

                // Mesh info
                if (selected->mesh) {
                    ImGui::Text("Mesh: %s", selected->mesh->name.c_str());
                    ImGui::Text("Indices: %d", selected->mesh->indexCount);
                    ImGui::Text("VAO: %u", selected->mesh->VAO);
                } else {
                    ImGui::Text("Mesh: (none)");
                }

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
                if (ImGui::Button("Delete Object", ImVec2(-1, 0))) {
                    CoreEngine::RemoveFromSceneWithMaterials(selected->id);
                    // Clean up texture if loaded
                    if (selected->material.diffuseTexture) {
                        CoreEngine::DestroyTexture(*selected->material.diffuseTexture);
                        delete selected->material.diffuseTexture;
                    }
                }
                ImGui::PopStyleColor(2);
            }
        }
        ImGui::End();
    }

    static void RenderConsole() {
        ImVec2 mainSize = ImGui::GetMainViewport()->Size;
        ImVec2 mainPos = ImGui::GetMainViewport()->Pos;

        const float consoleH = 150.0f;
        static char commandInput[256] = "";
        static bool atTop = true;
        static float copyTime = 0.0f;

        // Build full text for clipboard copy
        g_consoleText.clear();
        for (const auto& log : g_consoleLog) {
            g_consoleText += log + "\n";
        }

        ImVec2 consolePos = ImVec2(mainPos.x, mainPos.y + mainSize.y - consoleH);
        ImGui::SetNextWindowPos(consolePos);
        ImGui::SetNextWindowSize(ImVec2(mainSize.x, consoleH));
        if (ImGui::Begin("Console", nullptr,
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings)) {

            // Copy button + status
            if (ImGui::Button("Copy", ImVec2(70, 0))) {
                if (!g_consoleText.empty() && ImGui::GetClipboardText()) {
                    ImGui::SetClipboardText(g_consoleText.c_str());
                    copyTime = ImGui::GetTime();
                }
            }
            float elapsed = ImGui::GetTime() - copyTime;
            if (elapsed < 1.5f) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f - elapsed * 0.5f), "Copied!");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Ctrl+C");
            ImGui::SameLine();
            ImGui::TextDisabled("Scroll: free");

            // Scrollable log area
            ImGui::BeginChild("##logArea", ImVec2(0, -30), true);

            for (const auto& log : g_consoleLog) {
                ImGui::TextUnformatted(log.c_str());
            }

            // Stay at top by default; if user scrolls away, let them scroll freely
            if (atTop) {
                ImGui::SetScrollY(0.0f);
            }
            // Detect user scrolling away from top
            if (ImGui::GetScrollY() > 3.0f) {
                atTop = false;
            }

            ImGui::EndChild();

            // Command input at bottom
            if (ImGui::InputText("##command", commandInput, sizeof(commandInput), ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (strlen(commandInput) > 0) {
                    ConsoleLog(">> " + std::string(commandInput));
                    commandInput[0] = '\0';
                }
            }
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
                ImGui::MenuItem("Console", nullptr, &g_showStatusBar);
                ImGui::MenuItem("Shadows", nullptr, &g_showShadows);
                if (ImGui::MenuItem("Reset Camera", "Home")) {
                    CoreEngine::ResetCamera();
                    ConsoleLog("Camera reset to default position");
                }
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
            RenderConsole();
        }

        // ── Shadow Settings Panel ──────────────────────────────────────
        {
            ImVec2 sMainSize = ImGui::GetMainViewport()->Size;
            ImVec2 sMainPos  = ImGui::GetMainViewport()->Pos;
            const float sMenuBarH = 28.0f;
            const float sConsoleH = 150.0f;
            const float sPanelH = sMainSize.y - sMenuBarH - sConsoleH;

            ImVec2 shadowPos = ImVec2(sMainPos.x + sMainSize.x - 320, sMainPos.y + sMenuBarH + sPanelH - 130);
            ImGui::SetNextWindowPos(shadowPos);
            ImGui::SetNextWindowSize(ImVec2(320, 130));
            if (ImGui::Begin("Shadow Settings", nullptr, ImGuiWindowFlags_NoCollapse)) {
                ImGui::Checkbox("Enable Shadows", &g_showShadows);
                ImGui::Separator();
                ImGui::Text("Light Direction");
                static float lightDir[3] = {0.5f, 1.0f, 0.3f};
                if (ImGui::SliderFloat3("##lightDir", lightDir, -2.0f, 2.0f)) {
                    float len = sqrtf(lightDir[0]*lightDir[0] + lightDir[1]*lightDir[1] + lightDir[2]*lightDir[2]);
                    if (len > 0.001f) {
                        CoreEngine::SetShadowLightDirection({lightDir[0]/len, lightDir[1]/len, lightDir[2]/len});
                    }
                }
                ImGui::End();
            }
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
        // Redirect stdout/stderr into the console window


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
        ConsoleLog("OpenGL window created (" + std::to_string(width) + "x" + std::to_string(height) + ")");

        CoreEngine::EngineInfo info = CoreEngine::GetEngineInfo(width, height);
        std::cout << "[Editor] Engine communication OK - \"" << info.name 
                  << "\" v" << info.majorVersion << "." << info.minorVersion << std::endl;

        InitImGui(CoreEngine::GetWindow());

        // Initialize skybox
        CoreEngine::InitSkybox();

        // Initialize shadow mapping
        CoreEngine::InitShadowMap(2048, 2048);

        // Create camera as a scene object
        CoreEngine::CreateCameraObject();

        glfwSetInputMode(CoreEngine::GetWindow(), GLFW_REPEAT, GLFW_TRUE);
        glfwFocusWindow(CoreEngine::GetWindow());

        glfwSetCharCallback(CoreEngine::GetWindow(), [](GLFWwindow* w, unsigned int codepoint) {
            ImGui_ImplGlfw_CharCallback(w, codepoint);
        });

        glfwSetKeyCallback(CoreEngine::GetWindow(), [](GLFWwindow* w, int key, int scancode, int action, int mods) {
            // Feed keys to ImGui first
            ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);

            // Ctrl+C: copy console text to clipboard
            if (action == GLFW_PRESS && key == GLFW_KEY_C && (mods & GLFW_MOD_CONTROL)) {
                if (!g_consoleText.empty()) {
                    ImGui::SetClipboardText(g_consoleText.c_str());
                    ConsoleLog("Console copied to clipboard");
                }
            }

            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                if (!ImGui::GetIO().WantCaptureKeyboard) {
                    float speed = 0.05f;
                    auto& scene = CoreEngine::GetSceneObjectsWithMaterials();
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
                if (key == GLFW_KEY_L) {
                    g_triggerFileDialog = true;
                }
                if (key == GLFW_KEY_HOME) {
                    CoreEngine::ResetCamera();
                    ConsoleLog("Camera reset to default position");
                }
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
            // Don't zoom camera if ImGui is using the scroll (e.g. console scroll)
            if (ImGui::GetIO().WantCaptureMouse) return;
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
        CoreEngine::CleanupShadowMap();
        CoreEngine::ClearSceneWithMaterials();
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



        // Get window size for viewport
        int windowW = 1280, windowH = 720;
        glfwGetFramebufferSize(window, &windowW, &windowH);

        const float menuBarH = 28.0f;
        const float consoleH = 150.0f;
        const float leftPanelW = 280.0f;
        const float rightPanelW = 320.0f;

        // Only reserve console space when it's visible
        int panelBottomH = g_showStatusBar ? (int)consoleH : 0;

        int vpX = (int)leftPanelW;
        int vpY = (int)menuBarH;
        int vpW = windowW - (int)leftPanelW - (int)rightPanelW;
        int vpH = windowH - (int)menuBarH - panelBottomH;
        if (vpW < 1) vpW = windowW;
        if (vpH < 1) vpH = windowH;
        // Prevent zero aspect ratio
        float aspect = (float)vpW / (float)vpH;
        if (aspect <= 0.0f) aspect = 1.333f; // 16:10 fallback

        CoreEngine::RenderBegin();

        // Set viewport for 3D rendering (center area only)
        glViewport(vpX, vpY, vpW, vpH);

        // Draw skybox first (background) - pass viewport aspect ratio
        CoreEngine::DrawSkybox(aspect);

        auto& sceneObjs = CoreEngine::GetSceneObjectsWithMaterials();

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
        glm::vec3 camPos(cameraTarget.x + (float)CoreEngine::GetCameraOffset().x,
                         cameraTarget.y + (float)CoreEngine::GetCameraOffset().y,
                         cameraTarget.z + (float)CoreEngine::GetCameraOffset().z);
        glm::vec3 camTarget(cameraTarget.x, cameraTarget.y, cameraTarget.z);
        glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0, 1, 0));

        // Get projection matrix using actual viewport dimensions
        glm::mat4 projection = CoreEngine::GetProjectionMatrix(60.0f, aspect);



        // DISABLED shadow pass for debugging
        // CoreEngine::DrawShadowPass();

        // ── Main Pass: Render with shadow mapping ──────────────────────
        GLuint prog = CoreEngine::GetShaderProgram();
        glUseProgram(prog);
        GLint viewLoc = glGetUniformLocation(prog, "uView");
        GLint projLoc = glGetUniformLocation(prog, "uProjection");
        if (viewLoc != -1) CoreEngine::SetUniformMat4(prog, "uView", view);
        if (projLoc != -1) CoreEngine::SetUniformMat4(prog, "uProjection", projection);

        // Set shadow mapping uniforms (only when shadows are enabled)
        if (g_showShadows) {
            GLuint shadowTex = CoreEngine::GetShadowMapTexture();
            GLint shadowMapLoc = glGetUniformLocation(prog, "uShadowMap");
            GLint hasShadowLoc = glGetUniformLocation(prog, "uHasShadowMap");
            if (hasShadowLoc != -1) glUniform1i(hasShadowLoc, 1);
            if (shadowMapLoc != -1) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, shadowTex);
                glUniform1i(shadowMapLoc, 0);
            }

            // Light space matrix
            glm::mat4 lightSpaceMat = CoreEngine::GetLightSpaceMatrix();
            GLint lsmLoc = glGetUniformLocation(prog, "uLightSpaceMatrix");
            if (lsmLoc != -1) {
                glUniformMatrix4fv(lsmLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMat));
            }

            // Light direction
            auto lightDir = CoreEngine::GetShadowLightDirection();
            GLint ldLoc = glGetUniformLocation(prog, "uLightDirection");
            if (ldLoc != -1) {
                glUniform3f(ldLoc, lightDir.x, lightDir.y, lightDir.z);
            }

            // Shadow near/far
            GLint snLoc = glGetUniformLocation(prog, "uShadowNear");
            GLint sfLoc = glGetUniformLocation(prog, "uShadowFar");
            if (snLoc != -1) glUniform1f(snLoc, 0.5f);
            if (sfLoc != -1) glUniform1f(sfLoc, 50.0f);
        } else {
            // Shadows disabled — tell shader to skip shadow calc
            GLint hasShadowLoc = glGetUniformLocation(prog, "uHasShadowMap");
            if (hasShadowLoc != -1) glUniform1i(hasShadowLoc, 0);
        }

        // Draw grid on the ground FIRST (before scene objects)
        CoreEngine::DrawGrid(40, 1.0f, 20.0f, view, projection);

        for (auto& obj : sceneObjs) {
            auto& mesh = obj.mesh;
            if (!mesh || !mesh->VAO || mesh->indexCount == 0) continue;

            glUseProgram(prog);

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(obj.position.x, obj.position.y, obj.position.z));
            model = glm::rotate(model, (float)obj.rotation.x, glm::vec3(1, 0, 0));
            model = glm::rotate(model, (float)obj.rotation.y, glm::vec3(0, 1, 0));
            model = glm::rotate(model, (float)obj.rotation.z, glm::vec3(0, 0, 1));
            model = glm::scale(model, glm::vec3(obj.scale.x, obj.scale.y, obj.scale.z));

            CoreEngine::SetUniformMat4(prog, "uModel", model);
            CoreEngine::SetUniformVec3(prog, "uBaseColor", obj.material.baseColor);

            // Pass material properties
            GLint metallicLoc = glGetUniformLocation(prog, "uMetallic");
            if (metallicLoc != -1) glUniform1f(metallicLoc, obj.material.metallic);
            GLint roughnessLoc = glGetUniformLocation(prog, "uRoughness");
            if (roughnessLoc != -1) glUniform1f(roughnessLoc, obj.material.roughness);
            GLint aoLoc = glGetUniformLocation(prog, "uAO");
            if (aoLoc != -1) glUniform1f(aoLoc, obj.material.ao);
            GLint emissiveLoc = glGetUniformLocation(prog, "uEmissiveColor");
            if (emissiveLoc != -1) CoreEngine::SetUniformVec3(prog, "uEmissiveColor", obj.material.emissiveColor);

            // Textures
            GLint hasDiffuseLoc = glGetUniformLocation(prog, "uHasDiffuse");
            GLint hasNormalLoc = glGetUniformLocation(prog, "uHasNormal");
            GLint diffuseLoc = glGetUniformLocation(prog, "uDiffuseTex");
            GLint normalLoc = glGetUniformLocation(prog, "uNormalTex");

            if (obj.material.diffuseTexture && obj.material.diffuseTexture->id) {
                if (hasDiffuseLoc != -1) glUniform1i(hasDiffuseLoc, 1);
                if (diffuseLoc != -1) {
                    glActiveTexture(GL_TEXTURE1);
                    CoreEngine::BindTexture(*obj.material.diffuseTexture, 1);
                    glUniform1i(diffuseLoc, 1);
                }
            } else {
                if (hasDiffuseLoc != -1) glUniform1i(hasDiffuseLoc, 0);
            }
            if (hasNormalLoc != -1) glUniform1i(hasNormalLoc, 0);

            glBindVertexArray(mesh->VAO);
            if (mesh->EBO) {
                glDrawElements(GL_TRIANGLES, (GLsizei)mesh->indexCount, GL_UNSIGNED_INT, 0);
            } else {
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)mesh->indexCount);
            }
            glBindVertexArray(0);
        }

        // Draw selected object bounds wireframe
        CoreEngine::DrawSelectedObjectBounds(view, projection);

        // Reset viewport for full-window ImGui rendering
        glViewport(0, 0, windowW, windowH);

        RenderImGui(window);
        CoreEngine::RenderEnd();
    }

    void PollEvents(GLFWwindow* window) {
        glfwPollEvents();
    }

}  // namespace Editor
