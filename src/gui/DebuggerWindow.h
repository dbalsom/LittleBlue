#pragma once

// Minimal abstract base class for ImGui-powered debug windows.
// Individual windows should inherit from this and implement show(bool* open).
// The bool pointer allows ImGui::Begin to control the open/close state of the window.

#include <imgui/imgui.h>

class DebuggerWindow {
public:
    DebuggerWindow() = default;
    virtual ~DebuggerWindow() = default;

    // Draw the window contents. If 'open' is non-null, the implementation should
    // pass it to ImGui::Begin so the window's close button can modify visibility.
    virtual void show(bool *open) = 0;

    // Optional helper for derived classes to provide a stable name.
    [[nodiscard]] virtual const char* name() const { return "DebuggerWindow"; }
};
