#pragma once

#include "DebuggerWindow.h"

class Machine;

class PicStatusWindow : public DebuggerWindow
{
public:
    explicit PicStatusWindow(Machine* m) :
        _machine(m) {
    }

    ~PicStatusWindow() override = default;

    void show(bool* open) override;
    [[nodiscard]] const char* name() const override { return "PIC Status"; }

private:
    Machine* _machine{nullptr};
};

