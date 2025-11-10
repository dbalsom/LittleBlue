#pragma once

#include "DebuggerWindow.h"

class Machine;

class VideoCardStatusWindow : public DebuggerWindow {
public:
    explicit VideoCardStatusWindow(Machine* m) : _machine(m) {}
    ~VideoCardStatusWindow() override = default;

    void show(bool *open) override;

    [[nodiscard]] const char* name() const override { return "Video Card Status"; }

private:
    Machine* _machine{nullptr};
};

