#include "DisplayDebugWindow.h"
#include <imgui.h>

void DisplayDebugWindow::show(bool* open) {
    // The window simply shows the SDL texture if available.
    ImGui::Begin("Display Debug", open);
    if (!_texPtr || !*_texPtr) {
        ImGui::Text("No display texture available");
    }
    else {
        auto* tex = *_texPtr;
        ImGui::Image(tex, ImVec2(912, 262));
    }
    ImGui::End();
}

