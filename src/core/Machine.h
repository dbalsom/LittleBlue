#pragma once

#include <string>
#include "Cpu.h"

enum class MachineState { Running, Stopped, BreakpointHit };

class Machine
{

public:
    Machine() {
        //_cpu.setConsoleLogging();
        cpu_.reset();
        cpu_.getBus()->reset();
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
        switch (cpu_.run_for(static_cast<int>(ticks / 3))) {
            case Cpu::RunResult::BreakpointHit:
                state_ = MachineState::BreakpointHit;
                break;
            default:
                break;
        }
    }

    void resetCpu() {
        cpu_.reset();
    }

    void resetMachine() {
        cpu_.reset();
        cpu_.getBus()->reset();
    }

    void setState(const MachineState state) { state_ = state; }
    [[nodiscard]] MachineState getState() const { return state_; }

    [[nodiscard]] std::string getStateString() const {
        switch (state_) {
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

    [[nodiscard]] bool isRunning() const { return state_ == MachineState::Running; }
    void stop() { state_ = MachineState::Stopped; }
    void run() { state_ = MachineState::Running; }

    uint8_t* ram() { return cpu_.getBus()->ram(); }
    [[nodiscard]] size_t ramSize() { return cpu_.getBus()->ramSize(); }
    // Expose the underlying bus for tools needing direct access (e.g., CGA/VRAM)
    Bus* getBus() { return cpu_.getBus(); }
    Cpu* getCpu() { return &cpu_; }
    uint8_t getALU() { return cpu_.getALU(); }
    // Read a byte from physical address space (RAM or ROM). Does not modify bus state.
    uint8_t peekPhysical(uint32_t address) { return cpu_.getBus()->peek(address); }
    [[nodiscard]] size_t romSize() { return cpu_.getBus()->romSize(); }

    // Expose CPU register arrays for GUI inspection
    uint16_t* getMainRegisters() { return cpu_.getMainRegisters(); }
    uint16_t* registers() { return cpu_.getRegisters(); }
    uint16_t getRealIP() { return cpu_.getRealIP(); }
    std::string getQueueString() const { return cpu_.getQueueString(); }

    // Breakpoint control: forward to CPU
    void setBreakpoint(uint16_t cs, uint16_t ip) { cpu_.setBreakpoint(cs, ip); }
    void clearBreakpoint() { cpu_.clearBreakpoint(); }
    bool hasBreakpoint() const { return cpu_.hasBreakpoint(); }
    bool breakpointHit() const { return cpu_.breakpointHit(); }
    void clearBreakpointHit() { cpu_.clearBreakpointHit(); }
    uint16_t breakpointCS() const { return cpu_.breakpointCS(); }
    uint16_t breakpointIP() const { return cpu_.breakpointIP(); }

    // Return the CPU's cycle count (for display/diagnostics)
    [[nodiscard]] uint64_t cycleCount() { return cpu_.cycle(); }

    // Cycle log control
    void setCycleLogging(bool v) { cpu_.setCycleLogging(v); }
    bool isCycleLogging() const { return cpu_.isCycleLogging(); }
    void clearCycleLog() { cpu_.clearCycleLog(); }
    void setCycleLogCapacity(size_t c) { cpu_.setCycleLogCapacity(c); }
    [[nodiscard]] const std::deque<std::string>& getCycleLogBuffer() const { return cpu_.getCycleLogBuffer(); }
    [[nodiscard]] size_t getCycleLogSize() const { return cpu_.getCycleLogSize(); }
    [[nodiscard]] size_t getCycleLogCapacity() const { return cpu_.getCycleLogCapacity(); }
    // Append a line directly to the CPU's cycle log buffer (diagnostic helper)
    void appendCycleLogLine(const std::string& line) { cpu_.appendCycleLogLine(line); }

    // Step the CPU to the next instruction boundary. Returns the number of CPU cycles executed.
    uint64_t stepInstruction() {
        const auto cycles = static_cast<uint64_t>(cpu_.stepToNextInstruction());
        if (state_ == MachineState::Running) {
            state_ = MachineState::Stopped;
        }
        return cycles;
    }

    void sendScanCode(uint8_t scancode) {
        const auto ppi = cpu_.getBus()->ppi();

        // PB6 LOW output disables the clock line to the keyboard, so only read in a keyboard byte if it is high.
        if (!ppi->getB(6)) {
            return;
        }

        for (int i = 0; i < 8; ++i) {
            const bool bit = (scancode & (1 << i)) != 0;
            ppi->setA(i, bit);
        }

        // Request a keyboard interrupt.
        cpu_.getBus()->pic()->setIRQLine(1, true);
    }

private:
    MachineState state_{MachineState::Stopped};
    Cpu cpu_{};
};
