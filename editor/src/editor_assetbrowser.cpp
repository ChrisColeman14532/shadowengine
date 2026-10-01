#include "editor_assetbrowser.h"
#include "editor_state.h"
#include "editor_asset.h"
#include "core/engine.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace Editor {
namespace AssetBrowser {

    static const char* kAssetsDir = "assets";
    static const char* kPayload   = "SHADOW_ASSET_BROWSER_PATH";
    static const float kRefreshInterval = 0.75f;  // seconds between auto re-scans

    const char* PayloadType() { return kPayload; }
    std::string AssetsDir()   { return kAssetsDir; }

    // ── Internal helpers ────────────────────────────────────────────

    static std::vector<AssetEntry> s_entries;

    static std::string LowerExt(const fs::path& p) {
        std::string ext = p.extension().generic_string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return ext;
    }

    // Category drives the icon color in the list.
    static bool IsModelExt(const std::string& e)  {
        return e == ".fbx" || e == ".obj" || e == ".gltf" || e == ".glb";
    }
    static bool IsTextureExt(const std::string& e) {
        return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp"
            || e == ".tga" || e == ".tiff" || e == ".tif";
    }
    static bool IsAudioExt(const std::string& e) {
        return e == ".wav" || e == ".ogg" || e == ".mp3" || e == ".flac"
            || e == ".m4a" || e == ".aac" || e == ".aiff";
    }

    static ImVec4 CategoryColor(const std::string& ext) {
        if (IsModelExt(ext))   return ImVec4(0.35f, 0.75f, 0.35f, 1.0f);  // green
        if (IsTextureExt(ext)) return ImVec4(0.75f, 0.55f, 0.95f, 1.0f);   // purple
        if (IsAudioExt(ext))   return ImVec4(0.95f, 0.65f, 0.25f, 1.0f);   // orange
        return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);                          // gray
    }

    // ── Scanning ────────────────────────────────────────────────────

    void Refresh() {
        s_entries.clear();

        std::error_code ec;
        fs::create_directories(kAssetsDir, ec);  // create on first use; ignore errors
        if (!fs::is_directory(kAssetsDir, ec))
            return;

        fs::path root(kAssetsDir);
        for (fs::recursive_directory_iterator it(root,
                 fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            const auto& e = *it;
            if (!e.is_regular_file(ec)) continue;

            AssetEntry a;
            a.relativePath = fs::relative(e.path(), root, ec).generic_string();
            a.fullPath     = e.path().generic_string();
            a.sizeBytes    = (long long)e.file_size(ec);
            if (a.relativePath.empty()) a.relativePath = e.path().filename().generic_string();
            s_entries.push_back(std::move(a));
        }

        std::sort(s_entries.begin(), s_entries.end(),
            [](const AssetEntry& a, const AssetEntry& b) {
                return a.relativePath < b.relativePath;
            });
    }

    const std::vector<AssetEntry>& Entries() { return s_entries; }

    // ── External file drops (Windows Explorer etc.) ─────────────────

    static std::vector<std::string> s_pendingDrops;

    void QueueExternalFiles(int count, const char* const* paths) {
        for (int i = 0; i < count && paths && paths[i]; ++i)
            s_pendingDrops.emplace_back(paths[i]);
    }

    static bool CopyFileTo(const fs::path& src, const fs::path& dst) {
        std::ifstream in(src, std::ios::binary);
        if (!in) return false;
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << in.rdbuf();
        out.flush();
        return out.good();
    }

    // "assets/car.fbx" collides → "assets/car (1).fbx", "car (2).fbx", ...
    static fs::path UniqueDestination(const fs::path& dir, const fs::path& file) {
        fs::path candidate = dir / file;
        int n = 1;
        std::error_code ec;
        while (fs::exists(candidate, ec))
            candidate = dir / (file.stem().generic_string() + " (" +
                               std::to_string(n++) + ")" +
                               file.extension().generic_string());
        return candidate;
    }

    static void ImportExternalFile(const std::string& srcPath) {
        std::error_code ec;
        fs::path src(srcPath);
        if (fs::is_directory(src, ec)) {
            ConsoleLog("Asset Browser: skipped folder '" + src.filename().generic_string() +
                       "' (folder import is not supported yet)");
            return;
        }
        if (!fs::is_regular_file(src, ec)) {
            ConsoleLog("Asset Browser: cannot find file '" + srcPath + "'");
            return;
        }

        fs::path dest = UniqueDestination(fs::path(kAssetsDir), src.filename());
        if (CopyFileTo(src, dest)) {
            ConsoleLog("Asset Browser: imported '" + src.filename().generic_string() +
                       "' into " + dest.generic_string());
        } else {
            ConsoleLog("Asset Browser: FAILED to import '" + srcPath + "'");
        }
    }

    void PumpExternalDrops() {
        if (s_pendingDrops.empty()) return;
        for (const auto& p : s_pendingDrops)
            ImportExternalFile(p);
        s_pendingDrops.clear();
        Refresh();
    }

    // ── Adding assets to the scene ──────────────────────────────────

    bool AddToScene(const std::string& fullPath) {
        std::error_code ec;
        fs::path p(fullPath);
        if (!fs::is_regular_file(p, ec)) {
            ConsoleLog("Asset Browser: file no longer exists: " + fullPath);
            return false;
        }
        const std::string ext = LowerExt(p);
        const std::string name = p.filename().generic_string();

        // Model → import as a scene object (additive, like File → Load FBX)
        if (ext == ".fbx") {
            LoadFBXAtPath(fullPath);
            ConsoleLog("Added to scene: " + name);
            return true;
        }

        // Texture → apply as diffuse map on the currently selected object
        if (IsTextureExt(ext)) {
            uint32_t selId = CoreEngine::GetSelectedObjectId();
            CoreEngine::SceneObject* sel = CoreEngine::GetSceneObject(selId);
            if (!sel || CoreEngine::IsCameraObjectId(selId)) {
                ConsoleLog("Asset Browser: select a scene object first, then add '" +
                           name + "' to apply it as its diffuse texture");
                return false;
            }
            CoreEngine::TexturePtr tex = CoreEngine::LoadTexture(fullPath);
            if (!tex) {
                ConsoleLog("Asset Browser: failed to load texture: " + name);
                return false;
            }
            sel->material.diffuseTexture = tex;
            sel->material.diffuseTexturePath = fullPath;
            sel->material.useMaterial = true;
            ConsoleLog("Asset Browser: applied " + name + " to '" + sel->name + "'");
            return true;
        }

        // Audio and everything else: the engine has no importer for it yet
        if (IsAudioExt(ext)) {
            ConsoleLog("Asset Browser: audio playback is not supported yet — '" +
                       name + "' is available in the browser");
            return false;
        }
        ConsoleLog("Asset Browser: no scene importer for " + ext +
                   " ('" + name + "')");
        return false;
    }

    // ── Panel ───────────────────────────────────────────────────────

    static const char* AddToSceneHelpText(const std::string& ext) {
        if (ext == ".fbx")
            return "double-click / drag to hierarchy: import model";
        if (IsTextureExt(ext))
            return "double-click / drag to hierarchy: apply as diffuse on the selected object";
        if (IsAudioExt(ext))
            return "audio (no playback yet)";
        return nullptr;
    }

    static void RenderEntry(size_t idx) {
        const AssetEntry& a = s_entries[idx];
        const std::string ext = LowerExt(fs::path(a.relativePath));
        ImVec4 col = CategoryColor(ext);

        ImVec2 rowMin = ImGui::GetCursorPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float iconX = 4.0f;
        const float iconTop = 3.0f;
        const float iconSize = 9.0f;
        dl->AddRectFilled(
            ImVec2(rowMin.x + iconX, rowMin.y + iconTop),
            ImVec2(rowMin.x + iconX + iconSize, rowMin.y + iconTop + iconSize),
            ImGui::ColorConvertFloat4ToU32(col));

        // Selection state is shared with the hierarchy's double-click pattern
        // (per-window statics are fine: one browser window exists).
        static int s_selected = -1;

        ImGui::Indent(18.0f);
        char label[512];
        snprintf(label, sizeof(label), "%s", a.relativePath.c_str());
        bool selected = (s_selected == (int)idx);
        bool clicked = ImGui::Selectable(label, selected);
        ImGui::Unindent(18.0f);

        if (clicked) s_selected = (int)idx;

        // Double-click: add to scene (same as dropping on the hierarchy)
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (AddToScene(a.fullPath)) s_selected = -1;
        }

        // Drag source: drag this asset onto the Scene Hierarchy (or any
        // future drop target).
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(kPayload,
                                      a.fullPath.data(), a.fullPath.size());
            ImGui::TextUnformatted(a.relativePath.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(a.fullPath.c_str());
            char sz[32];
            snprintf(sz, sizeof(sz), "%.1f KB", a.sizeBytes / 1024.0);
            ImGui::TextUnformatted(sz);
            if (const char* help = AddToSceneHelpText(ext))
                ImGui::TextWrapped("%s", help);
            ImGui::EndTooltip();
        }
    }

    void RenderPanel() {
        // Layout mirrors the Scene Hierarchy above: same left column width,
        // stacked just under it (constants duplicated from editor_ui.cpp on
        // purpose, matching the existing panel layout style).
        ImVec2 mainSize = ImGui::GetMainViewport()->Size;
        ImVec2 mainPos  = ImGui::GetMainViewport()->Pos;
        const float menuBarH = 28.0f;
        const float consoleH = 150.0f;
        const float panelH = mainSize.y - menuBarH - consoleH;
        const float browserH = panelH * 0.45f;
        const float browserW = 280.0f;

        // Periodic re-scan so files copied in from Explorer (outside the
        // editor) or on disk show up without a manual refresh.
        float t = ImGui::GetTime();
        static float s_nextScan = -1.0f;
        if (t >= s_nextScan) {
            Refresh();
            s_nextScan = t + kRefreshInterval;
        }

        ImGui::SetNextWindowPos(ImVec2(mainPos.x, mainPos.y + menuBarH + panelH - browserH));
        ImGui::SetNextWindowSize(ImVec2(browserW, browserH));
        if (!ImGui::Begin("Asset Browser", nullptr, ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }

        ImGui::TextDisabled("assets/");
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::SmallButton("Refresh")) Refresh();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu file(s)", s_entries.size());
        ImGui::Separator();

        ImGui::BeginChild("##assetList", ImVec2(0, 0), false);
        if (s_entries.empty()) {
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                               "No assets yet.");
            ImGui::TextWrapped("Drag files here (or from Windows Explorer) to import them.");
        } else {
            for (size_t i = 0; i < s_entries.size(); ++i)
                RenderEntry(i);
        }
        ImGui::EndChild();

        ImGui::End();
    }

}  // namespace AssetBrowser
}  // namespace Editor
