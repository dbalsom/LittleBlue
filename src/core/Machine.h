#ifndef MACHINE_H
#define MACHINE_H

#include <cstdio>

#include "Cpu.h"

class Machine {

private:
    Cpu _cpu{};

public:
    Machine() {
        _cpu = Cpu();
        _cpu.setConsoleLogging();

        _cpu.reset();
        _cpu.setExtents(
            -4,
            100,
            100,
            0,
            0
            );
        printf("initialized and reset cpu.\n");
    }
    void run_for(int ticks) {
        _cpu.run_for(ticks / 3);
    }
    void reset() {
        _cpu.reset();
    }
};



#endif //MACHINE_H
