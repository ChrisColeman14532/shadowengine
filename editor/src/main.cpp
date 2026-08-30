#include "editor.h"
#include <iostream>

int main() {
    printf("[MAIN] Editor starting...\n");
    fflush(stdout);
    
    GLFWwindow* window = Editor::Init();
    if (!window) {
        std::cerr << "[Main] Fatal: failed to initialize editor" << std::endl;
        return -1;
    }

    while (Editor::IsRunning(window)) {
        Editor::PollEvents(window);
        Editor::RenderFrame(window);
    }

    Editor::ShutDown(window);
    return 0;
}
