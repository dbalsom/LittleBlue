#pragma once

#include <cstdio>

#include "Cpu.h"

class Machine {

Cpu _cpu{};

public:
    Machine() {
        _cpu = Cpu();
        _cpu.setConsoleLogging();

        _cpu.reset();
        _cpu.setExtents(
            -4,
            1000,
            1000,
            0,
            0
            );
        printf("Initialized and reset cpu!\n");
    }

    void run_for(int ticks) {
        _cpu.run_for(ticks / 3);
    }

    void reset() {
        _cpu.reset();
    }
};
