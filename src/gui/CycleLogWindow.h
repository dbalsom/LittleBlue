#pragma once

#include "DebuggerWindow.h"
#include "../core/Machine.h"
#include <string>

class CycleLogWindow : public DebuggerWindow {
public:
    explicit CycleLogWindow(Machine* machine) : _machine(machine) {
        if (_machine) {
            _capacityUI = static_cast<int>(_machine->getCycleLogCapacity());
            _lastSeenSize = _machine->getCycleLogSize();
        }
    }
    ~CycleLogWindow() override = default;

    void show(bool *open) override;

    [[nodiscard]] const char* name() const override { return "Cycle Log"; }

private:
    Machine* _machine{nullptr};
    bool _autoScroll{true};
    int _capacityUI{10000};
    size_t _lastSeenSize{0};
};

