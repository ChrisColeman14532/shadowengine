#include "editor_ui.h"
#include "editor_state.h"
#include "editor_asset.h"
#include "core/engine.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <cstdio>
#include <cstring>
#include <functional>
#include <cmath>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace Editor {

    // Generate a colored checkerboard texture (128x128)
    static CoreEngine::TexturePtr GenerateCheckerboardTexture() {
        const int size = 128;
        const int checkerSize = 16;  // 8x8 checkerboard
        unsigned char* pixels = new unsigned char[size * size * 4];

        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                int ci = (x / checkerSize) % 2;
                int cj = (y / checkerSize) % 2;
                bool white = (ci != cj);
                int idx = (y * size + x) * 4;

                if (white) {
                    // White squares with slight warm tint
                    pixels[idx + 0] = 240;
                    pixels[idx + 1] = 235;
                    pixels[idx + 2] = 220;
                } else {
                    // Blue-gray squares
                    pixels[idx + 0] = 80;
                    pixels[idx + 1] = 100;
                    pixels[idx + 2] = 160;
                }
                pixels[idx + 3] = 255;  // Full alpha
            }
        }

        CoreEngine::TexturePtr tex = CoreEngine::LoadTextureFromMemory(pixels, size, size, 4);
        delete[] pixels;
        return tex;
    }

    void InitImGui(GLFWwindow* window) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForOpenGL(window, false);
        ImGui_ImplOpenGL3_Init("#version 330");
    }

    void ShutdownImGui() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
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
                auto& scene = CoreEngine::GetSceneObjects();
                uint32_t nextId = CoreEngine::GetNextSceneObjectId();

                switch (selectedItem) {
                    case 0: { // Add Cube
                        if (meshCube) {
                            auto mat = CoreEngine::CreateDefaultMaterial();
                            mat.name = "cube_material";
                            uint32_t objId = CoreEngine::AddToScene("cube_" + std::to_string(nextId), meshCube, mat);
                            if (CoreEngine::SceneObject* obj = CoreEngine::GetSceneObject(objId)) {
                                obj->position = {0, 0, 0};
                                obj->scale = {1, 1, 1};
                            }
                            ConsoleLog("Added cube");
                        }
                        break;
                    }
                    case 1: { // Add Plane
                        if (meshPlane) {
                            auto mat = CoreEngine::CreateDefaultMaterial();
                            mat.name = "plane_material";
                            uint32_t objId = CoreEngine::AddToScene("plane_" + std::to_string(nextId), meshPlane, mat);
                            if (CoreEngine::SceneObject* obj = CoreEngine::GetSceneObject(objId)) {
                                obj->position = {0, -1.0f, 0};
                                obj->scale = {10, 1, 10};
                            }
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

            auto& scene = CoreEngine::GetSceneObjects();
            uint32_t selectedId = CoreEngine::GetSelectedObjectId();

            if (scene.empty()) {
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "No objects in scene");
            } else {
                // Track which object to rename (popup must be handled OUTSIDE the loop)
                // Store ID instead of raw pointer to avoid use-after-free if scene vector reallocates
                static uint32_t g_renameObjectId = 0;

                // Draw the scene as a tree: top-level objects, then their
                // children indented. Model roots (created on FBX import) show
                // their parts as children; objects whose parent is missing
                // fall back to top level.
                std::function<void(int)> drawNode = [&](int idx) {
                    auto& obj = scene[idx];
                    ImGui::Indent(14.0f);

                    ImGui::PushID((int)obj.id);
                    const char* label = obj.name.c_str();
                    bool was_selected = ImGui::Selectable(label, (obj.id == selectedId));

                    if (was_selected) {
                        CoreEngine::SelectObject(obj.id);
                    }

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        g_renameObjectId = obj.id;
                        ImGui::OpenPopup("Rename");
                        // Select object on double-click too
                        CoreEngine::SelectObject(obj.id);
                    }

                    ImGui::PopID();

                    for (size_t k = 0; k < scene.size(); ++k) {
                        if (scene[k].parentId == obj.id) drawNode((int)k);
                    }
                    ImGui::Unindent(14.0f);
                };

                for (size_t i = 0; i < scene.size(); ++i) {
                    bool topLevel = (scene[i].parentId == 0);
                    if (!topLevel) {
                        // Parent was removed out-of-band? Show at top level
                        // rather than swallowing the object.
                        bool parentGone = true;
                        for (auto& o : scene) {
                            if (o.id == scene[i].parentId) { parentGone = false; break; }
                        }
                        if (parentGone) topLevel = true;
                    }
                    if (topLevel) drawNode((int)i);
                }

                // Handle rename popup ONCE per frame (must be outside the loop)
                if (ImGui::BeginPopup("Rename") && g_renameObjectId != 0) {
                    // Look up object by ID — safe even if scene vector reallocates
                    CoreEngine::SceneObject* obj = nullptr;
                    for (auto& o : scene) {
                        if (o.id == g_renameObjectId) { obj = &o; break; }
                    }
                    if (obj) {
                        static char buf[256];
                        strncpy(buf, obj->name.c_str(), sizeof(buf) - 1);
                        buf[sizeof(buf) - 1] = '\0';
                        if (ImGui::InputText("##name", buf, sizeof(buf))) {
                            obj->name = buf;
                        }
                    } else {
                        g_renameObjectId = 0;
                    }
                    ImGui::EndPopup();
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Clear Scene", ImVec2(-1, 0))) {
                CoreEngine::ClearScene();
                CoreEngine::Animator::Get().Reset();
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
            // Scrollable body so long inspectors (tall object lists of
            // properties) aren't clipped at the window bottom.
            ImGui::BeginChild("##inspectorScroll", ImVec2(0, 0), false);
            auto& scene = CoreEngine::GetSceneObjects();
            uint32_t selectedId = CoreEngine::GetSelectedObjectId();
            CoreEngine::SceneObject* selected = nullptr;

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
                // Regular object inspector (also used for model roots / groups)
                char name_buf[256];
                strncpy(name_buf, selected->name.c_str(), sizeof(name_buf) - 1);
                name_buf[sizeof(name_buf) - 1] = '\0';
                if (ImGui::InputText("Object Name", name_buf, sizeof(name_buf))) {
                    selected->name = name_buf;
                }

                ImGui::Separator();
                ImGui::Text("Transform");
                if (selected->isGroup) {
                    ImGui::TextColored(ImVec4(0.6f, 0.85f, 0.4f, 1.0f),
                        "Model root — transforms apply to all child parts");
                }
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
                    if (selected->material.diffuseTexture) {
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Loaded (%dx%d)",
                            selected->material.diffuseTexture->width,
                            selected->material.diffuseTexture->height);
                        if (ImGui::SmallButton("Unload Texture")) {
                            // Dropping the shared_ptr releases the GL texture
                            selected->material.diffuseTexture = nullptr;
                        }
                    } else {
                        // ImGui::Button() is only true for ONE frame (the click frame),
                        // so the path input must be driven by a persistent flag, not
                        // nested inside the button check.
                        static bool showTexPathInput = false;
                        static uint32_t texInputForObject = 0;

                        // Discard a half-typed path if the selection changed
                        if (texInputForObject != selected->id) {
                            showTexPathInput = false;
                            texInputForObject = selected->id;
                        }

                        if (ImGui::Button("Load Texture...", ImVec2(-1, 0))) {
                            showTexPathInput = true;
                        }

                        if (showTexPathInput) {
                            static char texPath[MAX_PATH] = {0};
                            ImGui::Text("Texture path (e.g. assets/textures/checker.png)");
                            ImGui::InputText("##texPath", texPath, sizeof(texPath));
                            if (ImGui::IsItemDeactivatedAfterEdit()) {
                                // Commit on Enter or focus loss
                                CoreEngine::TexturePtr loaded = CoreEngine::LoadTexture(texPath);
                                if (loaded) {
                                    // Replaces any previous texture; the old GL object
                                    // is released when its refcount hits zero
                                    selected->material.diffuseTexture = loaded;
                                    showTexPathInput = false;
                                    texPath[0] = '\0';
                                } else {
                                    ConsoleLog(("Failed to load texture: " + std::string(texPath)).c_str());
                                    fprintf(stderr, "[Editor] Failed to load texture: %s\n", texPath);
                                }
                            }
                            if (ImGui::SmallButton("Cancel##texCancel")) {
                                showTexPathInput = false;
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
                    // Texture cleanup is automatic: the erased object's material
                    // holds a shared reference, and the GL texture is released
                    // when the last reference goes away.
                    CoreEngine::RemoveFromScene(selected->id);
                }
                ImGui::PopStyleColor(2);
            }
            ImGui::EndChild();
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

    // ── Animation Panel ───────────────────────────────────────────────────
    static void RenderAnimationPanel() {
        ImVec2 mainSize = ImGui::GetMainViewport()->Size;
        ImVec2 mainPos = ImGui::GetMainViewport()->Pos;

        const float menuBarH = 28.0f;
        const float consoleH = 150.0f;
        const float panelH = mainSize.y - menuBarH - consoleH;

        // Above the shadow settings panel (bottom-right stack)
        const float panelW = 320.0f;
        const float panelSize = 150.0f;
        ImVec2 animPos = ImVec2(mainPos.x + mainSize.x - panelW, mainPos.y + menuBarH + panelH - 130.0f - panelSize);
        ImGui::SetNextWindowPos(animPos);
        ImGui::SetNextWindowSize(ImVec2(panelW, panelSize));
        if (ImGui::Begin("Animation", nullptr, ImGuiWindowFlags_NoCollapse)) {
            auto& anim = CoreEngine::Animator::Get();
            const auto* clip = anim.ActiveClip();

            if (!anim.IsBound()) {
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                                   "No animation bound");
                ImGui::TextWrapped("Import a rigged model (File > Load FBX),\n"
                                   "then File > Load Animation...");
            } else {
                ImGui::Text("Clip: %s", clip->name.c_str());
                ImGui::Text("%.3f s   %.1f tps", clip->duration, clip->fps);

                ImGui::Spacing();

                // Play / pause
                if (anim.IsPlaying()) {
                    if (ImGui::Button("Pause##animPlayPause", ImVec2(80, 0))) {
                        anim.SetPlaying(false);
                    }
                } else {
                    if (ImGui::Button("Play##animPlayPause", ImVec2(80, 0))) {
                        anim.SetPlaying(true);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop##animStop")) {
                    anim.SetPlaying(false);
                    anim.SetTime(0.0f);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("loops");

                // Time scrubber
                float t = anim.GetTime();
                if (clip->duration > 0.0f &&
                    ImGui::SliderFloat("Time##animTime", &t, 0.0f, clip->duration)) {
                    anim.SetTime(t);
                }
                ImGui::Text("%.3f / %.3f s", anim.GetTime(), clip->duration);
            }
        }
        ImGui::End();
    }

    // ── Shadow Settings Panel ──────────────────────────────────────
    static void RenderShadowSettingsPanel() {
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

    void RenderImGui(GLFWwindow* window) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Menu bar
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Load FBX", "L")) {
                    g_triggerFileDialog = true;
                }
                if (ImGui::MenuItem("Load Animation...")) {
                    g_triggerAnimFileDialog = true;
                }
                ImGui::Separator();
                ImGui::MenuItem("Smooth Normals", nullptr, &g_smoothNormals);
                if (ImGui::IsItemActivated() && !g_lastLoadedFBX.empty()) {
                    // Re-import the current model in place (old parts removed,
                    // everything else in the scene untouched) so the toggle
                    // takes effect immediately.
                    ReloadLastFBX();
                    ConsoleLog(g_smoothNormals ? "Smooth normals: ON (model reloaded)" : "Smooth normals: OFF (model reloaded)");
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
                ImGui::MenuItem("Animation", nullptr, &g_showAnimationPanel);
                ImGui::MenuItem("Shadows", nullptr, &g_showShadows);
                if (ImGui::MenuItem("Reset Camera", "Home")) {
                    CoreEngine::ResetCamera();
                    ConsoleLog("Camera reset to default position");
                }
                if (ImGui::MenuItem("Test Texture (Colored Cube)")) {
                    // Create a cube with a checkerboard texture to verify texture rendering works
                    auto meshCube = CoreEngine::GetPrimitiveMesh("cube");
                    if (meshCube) {
                        auto mat = CoreEngine::CreateDefaultMaterial();
                        mat.name = "test_texture_material";
                        mat.baseColor = glm::vec3(1.0f);  // White base so texture colors show through
                        mat.useMaterial = true;
                        CoreEngine::TexturePtr tex = GenerateCheckerboardTexture();
                        mat.diffuseTexture = tex;
                        auto& scene = CoreEngine::GetSceneObjects();
                        uint32_t nextId = CoreEngine::GetNextSceneObjectId();
                        uint32_t objId = CoreEngine::AddToScene("test_checkerboard", meshCube, mat);
                        if (CoreEngine::SceneObject* obj = CoreEngine::GetSceneObject(objId)) {
                            obj->position = {0, 0, 0};
                            obj->scale = {1, 1, 1};
                        }
                        ConsoleLog("Added test cube with checkerboard texture (verify textures are working!)");
                        if (tex) printf("[Editor] Test texture loaded: %dx%d\n", tex->width, tex->height);
                    }
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

        if (g_showAnimationPanel) {
            RenderAnimationPanel();
        }

        RenderShadowSettingsPanel();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

}  // namespace Editor
