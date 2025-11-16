#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>

// Simple stub bus used for testing the Cpu core.
// - 1 MiB of addressable RAM
// - Memory reads/writes access the internal buffer
// - IO reads return 0xFF, IO writes are no-ops
// - Other control/query methods return safe defaults

struct StubBus
{
    StubBus() :
        ram_(1024 * 1024, 0), address_(0), type_(0) {
    }

    // Provide raw RAM pointer for Cpu::getRAM()
    uint8_t* ram() { return ram_.data(); }
    const uint8_t* ram() const { return ram_.data(); }
    size_t ramSize() const { return ram_.size(); }

    // Start a bus access (address, type) - record for subsequent read/write
    void startAccess(uint32_t address, int type) {
        address_ = address;
        type_ = type;
    }

    // Returns whether the bus is ready. Always ready for stub
    bool ready() const { return true; }

    // Read a byte from an I/O / data latch.
    // If the last access type denotes memory, read from RAM at address_.
    // Otherwise (IO port), return 0xFF.
    uint8_t read() {
        if (isMemoryType(type_)) {
            return ram_[address_ & 0xFFFFF];
        }
        // IO port read
        return 0xFF;
    }

    // Write a byte to an I/O / data latch
    // If the last access type denotes memory, write into RAM at address_.
    // Otherwise (IO port), do nothing.
    void write(uint8_t value) {
        if (isMemoryType(type_)) {
            ram_[address_ & 0xFFFFF] = value;
        }
        // IO writes are ignored for stub
    }

    // Control methods
    void setPassiveOrHalt(bool /*v*/) {
    }

    void setLock(bool /*v*/) {
    }

    void tick() {
    }

    // DMA/interrupt/CGA helpers -- return safe defaults
    bool getAEN() const { return false; }
    bool getDMA() const { return false; }
    int getDMAS() const { return 0; }
    bool getDMAS3() const { return false; }
    uint32_t getDMAAddress() const { return 0; }
    bool getDMADelayedT2() const { return false; }

    uint8_t pitBits() const { return 0; }
    int getBusOperation() const { return 0; }
    uint32_t getIRQLines() const { return 0; }
    bool interruptPending() const { return false; }

    // Extra textual data for sniffer logs
    std::string snifferExtra() const { return std::string(); }

    // Called by Cpu::stubInit()
    void stubInit() {
        // Optionally initialize memory with a recognizable pattern
        // but leave zeroed by default
    }

    // Reset stub state and clear RAM
    void reset() {
        std::fill(ram_.begin(), ram_.end(), 0);
        address_ = 0;
        type_ = 0;
    }

private:
    static bool isMemoryType(int type) {
        // CPU's IOType values (in Cpu.h) map to:
        // ioPrefetch = 4, ioReadMemory = 5, ioWriteMemory = 6
        // treat prefetch and memory read/write as memory accesses
        return type == 4 || type == 5 || type == 6;
    }

    std::vector<uint8_t> ram_;

    // Last access parameters recorded by startAccess
    uint32_t address_;
    int type_;
};
