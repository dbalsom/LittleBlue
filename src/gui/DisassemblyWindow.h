#pragma once

#include "DebuggerWindow.h"
#include "../core/Machine.h"

class DisassemblyWindow final : public DebuggerWindow {
public:
    explicit DisassemblyWindow(Machine* m) : _machine(m) {}
    ~DisassemblyWindow() override = default;

    void show(bool *open) override;
    [[nodiscard]] const char* name() const override { return "Disassembly"; }

private:
    Machine* _machine;
};
