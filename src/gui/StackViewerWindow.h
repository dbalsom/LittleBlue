#pragma once

#include "DebuggerWindow.h"
#include <imgui/imgui.h>

class Machine;

class StackViewerWindow : public DebuggerWindow {
public:
    explicit StackViewerWindow(Machine* machine) : _machine(machine) {}
    ~StackViewerWindow() override = default;

    void show(bool *open) override;

    [[nodiscard]] const char* name() const override { return "Stack Viewer"; }

private:
    Machine* _machine{nullptr};
    bool _autoFollow{true};            // follow SP when true; defaults to true per requirement
    float _lastScrollY{0.0f};
    float _lastScrollMax{0.0f};
    bool _userScrolledUp{false};
    bool _wasAtBottom{true};
    int _interactionCooldownFrames{0};
};
