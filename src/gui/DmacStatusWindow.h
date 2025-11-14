#pragma once

#include "DebuggerWindow.h"

class Machine;

class DmacStatusWindow : public DebuggerWindow {
public:
    explicit DmacStatusWindow(Machine* m) : _machine(m) {}
    ~DmacStatusWindow() override = default;

    void show(bool *open) override;
    [[nodiscard]] const char* name() const override { return "DMAC Status"; }

private:
    Machine* _machine{nullptr};
    int _selectedChannel{0};
};

