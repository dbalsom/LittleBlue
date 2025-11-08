#pragma once

#include "DebuggerWindow.h"
#include "../core/Machine.h"

class CpuStatusWindow final : public DebuggerWindow {
public:
    explicit CpuStatusWindow(Machine* m) : _machine(m) {}
    ~CpuStatusWindow() override = default;

    void show(bool *open) override;
    [[nodiscard]] const char* name() const override { return "CPU Status"; }

private:
    Machine* _machine;
};
