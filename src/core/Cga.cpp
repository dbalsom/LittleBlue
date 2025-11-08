//
// Created by Daniel on 11/7/2025.
//

#include "Cga.h"

CGA::CGA()
{
    reset();
}

void CGA::reset()
{
    cgaPhase_ = 0;
}

void CGA::wait()
{
    cgaPhase_ = (cgaPhase_ + 3) & 0x0f;
}

uint8_t* CGA::getMem() {
    return vram_;
}

size_t CGA::getMemSize() const {
    return sizeof(vram_);
}

uint8_t CGA::readMem(const uint16_t address) const {
    return vram_[address & CGA_APERTURE_MASK];
}

void CGA::writeMem(const uint16_t address, const uint8_t data)
{
    vram_[address & CGA_APERTURE_MASK] = data;
}

uint8_t CGA::readIo(uint16_t address)
{
    // Placeholder for CGA I/O read logic
    return 0xFF; // Dummy data
}

void CGA::writeIo(uint16_t address, uint8_t data)
{
    // Placeholder for CGA I/O write logic
}