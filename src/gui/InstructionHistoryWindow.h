#pragma once

#include "DebuggerWindow.h"
#include "../core/Machine.h"
#include <string>
#include <vector>

// InstructionHistoryWindow displays the recent instruction history captured by the CPU.
// It estimates how many lines fit vertically and requests that many entries from Cpu->getHistory().
// Newest instructions are shown at the bottom (bottom-up view). An auto-scroll option keeps the
// view pinned to the newest entry when enabled.
class InstructionHistoryWindow : public DebuggerWindow
{
public:
    explicit InstructionHistoryWindow(Machine* machine) :
        _machine(machine) {
    }

    ~InstructionHistoryWindow() override = default;

    void show(bool* open) override;
    [[nodiscard]] const char* name() const override { return "Instruction History"; }

private:
    Machine* _machine{nullptr};
    bool _autoScroll{true};
    size_t _lastDisplayedCount{0};
    bool _historyEnabledInitialized{false};
    bool _historyEnabled{false};
};
