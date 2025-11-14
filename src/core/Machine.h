#pragma once

#include <string>
#include "Cpu.h"

enum class MachineState { Running, Stopped, BreakpointHit };

class Machine
{

public:
    Machine() {
        //_cpu.setConsoleLogging();
        _cpu.reset();
        _cpu.getBus()->reset();
        // _cpu.setExtents(
        //     -4,
        //     1000,
        //     1000,
        //     0,
        //     0
        //     );
        std::cout << "Initialized and reset cpu!" << std::endl;
    }

    void run_for(const uint64_t ticks) {
        // The CPU core's run_for takes a number of CPU cycles (ticks/3 -> CPU cycles)
        switch (_cpu.run_for(static_cast<int>(ticks / 3))) {
            case Cpu::RunResult::BreakpointHit:
                _state = MachineState::BreakpointHit;
                break;
            default:
                break;
        }
    }

    void resetCpu() {
        _cpu.reset();
    }

    void resetMachine() {
        _cpu.reset();
        _cpu.getBus()->reset();
    }

    void setState(const MachineState state) { _state = state; }
    [[nodiscard]] MachineState getState() const { return _state; }

    [[nodiscard]] std::string getStateString() const {
        switch (_state) {
            case MachineState::Running:
                return "Running";
            case MachineState::Stopped:
                return "Stopped";
            case MachineState::BreakpointHit:
                return "Breakpoint Hit";
            default:
                return "Unknown";
        }
    }

    [[nodiscard]] bool isRunning() const { return _state == MachineState::Running; }
    void stop() { _state = MachineState::Stopped; }
    void run() { _state = MachineState::Running; }

    uint8_t* ram() { return _cpu.getBus()->ram(); }
    [[nodiscard]] size_t ramSize() { return _cpu.getBus()->ramSize(); }
    // Expose the underlying bus for tools needing direct access (e.g., CGA/VRAM)
    Bus* getBus() { return _cpu.getBus(); }
    Cpu* getCpu() { return &_cpu; }
    uint8_t getALU() { return _cpu.getALU(); }
    // Read a byte from physical address space (RAM or ROM). Does not modify bus state.
    uint8_t peekPhysical(uint32_t address) { return _cpu.getBus()->peek(address); }
    [[nodiscard]] size_t romSize() { return _cpu.getBus()->romSize(); }

    // Expose CPU register arrays for GUI inspection
    uint16_t* getMainRegisters() { return _cpu.getMainRegisters(); }
    uint16_t* registers() { return _cpu.getRegisters(); }
    uint16_t getRealIP() { return _cpu.getRealIP(); }
    std::string getQueueString() const { return _cpu.getQueueString(); }

    // Breakpoint control: forward to CPU
    void setBreakpoint(uint16_t cs, uint16_t ip) { _cpu.setBreakpoint(cs, ip); }
    void clearBreakpoint() { _cpu.clearBreakpoint(); }
    bool hasBreakpoint() const { return _cpu.hasBreakpoint(); }
    bool breakpointHit() const { return _cpu.breakpointHit(); }
    void clearBreakpointHit() { _cpu.clearBreakpointHit(); }
    uint16_t breakpointCS() const { return _cpu.breakpointCS(); }
    uint16_t breakpointIP() const { return _cpu.breakpointIP(); }

    // Return the CPU's cycle count (for display/diagnostics)
    [[nodiscard]] uint64_t cycleCount() { return _cpu.cycle(); }

    // Cycle log control
    void setCycleLogging(bool v) { _cpu.setCycleLogging(v); }
    bool isCycleLogging() const { return _cpu.isCycleLogging(); }
    void clearCycleLog() { _cpu.clearCycleLog(); }
    void setCycleLogCapacity(size_t c) { _cpu.setCycleLogCapacity(c); }
    [[nodiscard]] const std::deque<std::string>& getCycleLogBuffer() const { return _cpu.getCycleLogBuffer(); }
    [[nodiscard]] size_t getCycleLogSize() const { return _cpu.getCycleLogSize(); }
    [[nodiscard]] size_t getCycleLogCapacity() const { return _cpu.getCycleLogCapacity(); }
    // Append a line directly to the CPU's cycle log buffer (diagnostic helper)
    void appendCycleLogLine(const std::string& line) { _cpu.appendCycleLogLine(line); }

    // Step the CPU to the next instruction boundary. Returns the number of CPU cycles executed.
    uint64_t stepInstruction() {
        const auto cycles = static_cast<uint64_t>(_cpu.stepToNextInstruction());
        if (_state == MachineState::Running) {
            _state = MachineState::Stopped;
        }
        return cycles;
    }

    void sendScanCode(uint8_t scancode) {
        const auto ppi = _cpu.getBus()->ppi();

        // PB6 LOW output disables the clock line to the keyboard, so only read in a keyboard byte if it is high.
        if (!ppi->getB(6)) {
            return;
        }

        for (int i = 0; i < 8; ++i) {
            const bool bit = (scancode & (1 << i)) != 0;
            ppi->setA(i, bit);
        }

        // Request a keyboard interrupt.
        _cpu.getBus()->pic()->setIRQLine(1, true);
    }

private:
    MachineState _state{MachineState::Stopped};
    Cpu _cpu{};
};
