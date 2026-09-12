#include "editor_state.h"

namespace Editor {

    std::string g_lastLoadedFBX;
    std::vector<uint32_t> g_lastModelObjectIds;
    bool g_showSceneHierarchy = true;
    bool g_showInspector = true;
    bool g_showStatusBar = false;
    bool g_showAnimationPanel = false;
    bool g_triggerFileDialog = false;
    bool g_triggerAnimFileDialog = false;
    bool g_smoothNormals = false;  // Recompute smooth normals when loading FBX
    bool g_showShadows = true;    // Shadows enabled by default

    // Console log storage
    std::vector<std::string> g_consoleLog;
    std::string   g_consoleText; // assembled text for copy

    void ConsoleLog(const std::string& msg) {
        g_consoleLog.push_back(msg);
        if (g_consoleLog.size() > 1000) g_consoleLog.erase(g_consoleLog.begin());
    }

}  // namespace Editor
