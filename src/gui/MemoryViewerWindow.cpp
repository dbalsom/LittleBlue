#include "MemoryViewerWindow.h"
#include "../core/Cga.h"
#include "../core/Machine.h"

void MemoryViewerWindow::show(bool *open) {
    if (!_machine) {
        ImGui::Begin("Memory Viewer", open);
        ImGui::Text("No Machine instance available");
        ImGui::End();
        return;
    }

    if (_vramMode) {
        // Resolve CGA via machine->getBus()->cga() to avoid storing a direct CGA pointer
        auto* cga = _machine->getBus()->cga();
        if (!cga) {
            ImGui::Begin("VRAM Viewer", open);
            ImGui::Text("CGA unavailable");
            ImGui::End();
            return;
        }
        // DrawWindow manages its own ImGui window and its own Open flag.
        _memEditor.DrawWindow("VRAM Viewer", cga->getMem(), cga->getMemSize());
        // If the memory editor's internal Open was closed via the window close button,
        // propagate that to the external visibility pointer so DebuggerManager hides this window.
        if (! _memEditor.Open) {
            if (open) *open = false;
            // Reset internal flag so the editor is ready next time it's shown.
            _memEditor.Open = true;
        }
        return;
    }

    // Conventional RAM
    _memEditor.DrawWindow("Memory Viewer", _machine->ram(), _machine->ramSize());
    if (! _memEditor.Open) {
        if (open) *open = false;
        _memEditor.Open = true;
    }
}
