#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Editor {

    // ── Shared editor state (defined in editor_state.cpp) ───────────

    // Last FBX that was loaded (used to re-apply menu options to it)
    extern std::string g_lastLoadedFBX;

    // Scene object IDs created by the most recent LoadFBXAtPath() call.
    // Used to re-import the model in place (e.g. smooth-normal toggle)
    // without duplicating it.
    extern std::vector<uint32_t> g_lastModelObjectIds;

    // Panel visibility (toggled from the View menu)
    extern bool g_showSceneHierarchy;
    extern bool g_showAssetBrowser;
    extern bool g_showInspector;
    extern bool g_showStatusBar;
    extern bool g_showAnimationPanel;

    // Whether shadow maps are rendered (the "Enable Shadows" checkbox in
    // the Shadow Settings panel) — rendering state, not panel visibility
    extern bool g_showShadows;

    // Shadow Settings panel visibility (View → Shadows), kept separate
    // from g_showShadows so the menu toggles the panel like every other
    // View item while the checkbox controls actual shadow rendering
    extern bool g_showShadowSettings;

    // Set by the File menu / L key, consumed by RenderFrame
    extern bool g_triggerFileDialog;

    // Set by File → Load Animation, consumed by RenderFrame (opens the
    // native file dialog, then loads + binds the animation)
    extern bool g_triggerAnimFileDialog;

    // Set by File → Open Scene... / Ctrl+O, consumed by RenderFrame
    extern bool g_triggerOpenSceneFile;

    // Set by File → Save Scene / Ctrl+S, consumed by RenderFrame. Saves to
    // the current scene path, or opens the Save-As dialog when none yet.
    extern bool g_triggerSaveSceneFile;

    // Set by File → Save Scene As..., consumed by RenderFrame
    extern bool g_triggerSaveSceneAsFile;

    // Recompute smooth normals when loading FBX (File menu)
    extern bool g_smoothNormals;

    // Console log storage
    extern std::vector<std::string> g_consoleLog;
    extern std::string   g_consoleText; // assembled text for copy

    // Append a line to the console log (ring buffer, max 1000 lines)
    void ConsoleLog(const std::string& msg);

}  // namespace Editor
