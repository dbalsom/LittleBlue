#pragma once

#include "DebuggerWindow.h"
#include <imgui/imgui.h>
#include "imgui_memory_editor.h"

class Machine;

class MemoryViewerWindow : public DebuggerWindow {
public:
    // Construct to view conventional RAM via Machine (vram=false)
    explicit MemoryViewerWindow(Machine* machine, bool vram = false) : _machine(machine), _vramMode(vram) {}
    ~MemoryViewerWindow() override = default;

    void show(bool *open) override;

    [[nodiscard]] const char* name() const override { return "Memory Viewer"; }

private:
    MemoryEditor _memEditor;
    Machine* _machine{nullptr};
    bool _vramMode{false};
};
