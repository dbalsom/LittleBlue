#pragma once

#include "Crtc.h"

#define VRAM_SIZE 0x4000
#define CGA_APERTURE_MASK 0x3FFF

class CGA
{

public:
    CGA();
    void reset();
    void tick();

    uint8_t* getMem();
    size_t getMemSize() const;

    uint8_t readMem(uint16_t address) const;
    void writeMem(uint16_t address, uint8_t data);

    uint8_t readIo(uint16_t address);
    void writeIo(uint16_t address, uint8_t data);

private:
    uint8_t vram_[VRAM_SIZE]{};
    Crtc6845 crtc_{};
    uint8_t cgaPhase_;
};

