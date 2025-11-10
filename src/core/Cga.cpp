//
// Created by Daniel on 11/7/2025.
//

#include "Cga.h"

#include <iostream>

CGA::CGA()
{
    reset();
}

void CGA::reset()
{
    cgaPhase_ = 0;
}



void CGA::tick()
{
    ticks_++;
    if ((ticks_ & charClockMask_) == 0) {

        if (clockDivisor_ == 2) {
            tick_lchar();
        } else {
            tick_hchar();
        }

        // Provide an HBlankCallback that returns the required value (5).
        // crtc_.tick expects a std::function<uint8_t(void)>.
        auto [status, vma] = crtc_.tick([]() -> uint8_t { return static_cast<uint8_t>(5); });
        vma_ = vma;
        if (status->vsync) {
            //std::cout << "CGA: VSYNC asserted at beamX=" << beamX_ << " beamY=" << beamY_ << "\n";
            vsync();
        }
        if (status->hsync) {
            hsync();
        }


        fetch_char();
    }
    cgaPhase_ = (cgaPhase_ + 3) & 0x0f;
}

void CGA::tick_lchar() {

}

void CGA::tick_hchar() {
    //std::cout << "CGA: tick_hchar at beamX=" << beamX_ << " beamY=" << beamY_ << " rba=" << rba_ << "\n";
    // Only render if within display extents
    if (rba_ < (CGA_MAX_CLOCK - 8)) {
        if (crtc_.den()) {
            // Draw current character row
            if (!modeGraphics_) {
                //std::cout << "CGA: Drawing text mode char at rba=" << rba_ << "\n";
                draw_text_mode_hchar();
            }
            else if (modeHiresGfx_) {
                //draw_hires_gfx_mode_char();
            }
            else {
                draw_solid_hchar(ccOverscanColor_);
            }
        }
        else {
            draw_solid_hchar(7);
        }

        // Update position to next pixel and character column.
        beamX_ += 8 * clockDivisor_;
        rba_ += 8 * clockDivisor_;

        // If we have reached the right edge of the 'monitor', return the raster position
        // to the left side of the screen.
        if (beamX_ >= CGA_XRES_MAX) {
            beamX_ = 0;
            beamY_ += 1;
            monitorVSync_ = false;
            rba_ = CGA_XRES_MAX * beamY_;
        }
    }
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
    switch (address) {
        case 0:
        case 2:
        case 4:
            return crtc_.read(0);
        case 1:
        case 3:
        case 5:
            return crtc_.read(1);
        case 8:
            return 0xFF; // Mode register is write-only
        case 0x0A:
            return readStatusRegister();
        case 0x0B:
            clearLPLatch();
            break;
        case 0x0C:
            setLPLatch();
            break;

        default:
            break;
    }
    return 0xFF;
}

void CGA::writeIo(uint16_t address, uint8_t data)
{
    switch (address) {
        case 0:
        case 2:
        case 4:
            crtc_.write(0, data);
            break;
        case 1:
        case 3:
        case 5:
            crtc_.write(1, data);
            break;
        case 8:
            writeModeRegister(data);
            break;
        case 9:
            writeColorControlRegister(data);
            break;
        case 0x0B:
            clearLPLatch();
            break;
        case 0x0C:
            setLPLatch();
            break;
        default:
            break;
    }
    // Placeholder for CGA I/O write logic
}

bool is_deferred_mode_change(uint8_t mode_byte) {
    return false;
}

void CGA::writeModeRegister(uint8_t mode_byte) {
    std::cout << "Write to CGA mode register: " << std::hex << mode_byte << std::dec << "\n";
    if (is_deferred_mode_change(mode_byte)) {
        // Latch the mode change and mark it pending. We will change the mode on next hsync.
        modePending_ = true;
        modeByte_ = mode_byte;
    }
    else {
        // We're not changing from text to graphics or vice versa, so we do not have to
        // defer the update.
        modeByte_ = mode_byte;
        updateMode();
    }
}

void CGA::updateMode() {
    // Will this mode change the character clock?
    auto clock_changed = modeHiresText_ != ((modeByte_ & MODE_HIRES_TEXT) != 0);
    if (clock_changed) {
        // Flag the clock for pending change.  The clock can only be changed in phase with
        // LCHAR due to our dynamic clocking logic.
        std::cout << "CGA: Clock change pending";
        clockPending_ = true;
    }

    modeHiresText_ = (modeByte_ & MODE_HIRES_TEXT) != 0;
    modeGraphics_ = (modeByte_ & MODE_GRAPHICS) != 0;
    modeBW_ = (modeByte_ & MODE_BW) != 0;
    modeEnable_ = (modeByte_ & MODE_ENABLE) != 0;
    modeHiresGfx_ = (modeByte_ & MODE_HIRES_GRAPHICS) != 0;
    modeBlinking_ = (modeByte_ & MODE_BLINKING) != 0;

    // Use color control register value for overscan unless high-res graphics mode,
    // in which case overscan must be black (0).
    if (modeHiresGfx_) {
        ccOverscanColor_ = 0;
    }
    else {
        ccOverscanColor_ = ccAltColor_;
    };

    // Reinterpret the CC register based on new mode.
    updatePalette();

    // Attempt to update clock.
    updateClock();

    // Updated mask to exclude enable bit in mode calculation.
    // "Disabled" isn't really a video mode, it just controls whether
    // the CGA card outputs video at a given moment. This can be toggled on
    // and off during a single frame, such as done in VileR's fontcmp.com

    // self.display_mode = match self.mode_byte & CGA_MODE_ENABLE_MASK {
    //     0b0_0100 => DisplayMode::Mode0TextBw40,
    //     0b0_0000 => DisplayMode::Mode1TextCo40,
    //     0b0_0101 => DisplayMode::Mode2TextBw80,
    //     0b0_0001 => DisplayMode::Mode3TextCo80,
    //     0b0_0011 => DisplayMode::ModeTextAndGraphicsHack,
    //     0b0_0010 => DisplayMode::Mode4LowResGraphics,
    //     0b0_0110 => DisplayMode::Mode5LowResAltPalette,
    //     0b1_0110 => DisplayMode::Mode6HiResGraphics,
    //     _ => {
    //         trace!(self, "Invalid display mode selected: {:02X}", self.mode_byte & 0x1F);
    //         log::warn!("CGA: Invalid display mode selected: {:02X}", self.mode_byte & 0x1F);
    //         DisplayMode::Mode3TextCo80
    //     }
    // };

}

void CGA::writeColorControlRegister(uint8_t data) {
    ccRegisterByte_ = data;
    updatePalette();
}

void CGA::updateClock() {
    if (clockPending_ && (ticks_ & 0x0F) == 0) {
        // Clock divisor is 1 in high-res text mode, 2 in all other modes
        // We draw pixels twice when clock divisor is 2 to simulate slower scanning.
        if (modeHiresText_) {
            clockDivisor_ = 1;
            charClock_ = HCHAR_CLOCK;
            charClockMask_ = HCHAR_CLOCK_MASK;
            charClockOddMask_ = HCHAR_ODD_CLOCK_MASK;
        }
        else {
            clockDivisor_ = 2;
            charClock_ = LCHAR_CLOCK;
            charClockMask_ = LCHAR_CLOCK_MASK;
            charClockOddMask_ = LCHAR_ODD_CLOCK_MASK;
        }
        clockPending_ = false;
    }
}

void CGA::updatePalette() {
    if (modeBW_ && modeGraphics_ && !modeHiresGfx_) {
        ccPalette_ = 4; // Select Red, Cyan and White palette (undocumented)
    }
    else if ((ccRegisterByte_ & CC_PALETTE_BIT) != 0) {
        ccPalette_ = 2; // Select Magenta, Cyan, White palette
    }
    else {
        ccPalette_ = 0; // Select Red, Green, 'Yellow' palette
    }

    if ((ccRegisterByte_ & CC_BRIGHT_BIT) != 0) {
        ccPalette_++; // Switch to high-intensity palette
    }

    ccAltColor_ = ccRegisterByte_ & 0x0F;
    if (!modeHiresGfx_) {
        ccOverscanColor_ = ccAltColor_;
    }
}

uint8_t CGA::readStatusRegister() const {
    auto byte = 0xF0;

    if (crtc_.vblank()) {
        byte |= STATUS_VERTICAL_RETRACE;
    }
    if (!crtc_.den()) {
        byte |= STATUS_DISPLAY_ENABLE;
    }
    if (lpLatch_) {
        byte |= STATUS_LIGHTPEN_TRIGGER_SET;
    }
    if (lpSwitch_) {
        byte |= STATUS_LIGHTPEN_SWITCH_STATUS;
    }
    return byte;
}

uint8_t* CGA::getBackBuffer() {
    return &buf_[backBuf_][0];
}

size_t CGA::getBackBufferSize() const {
    return sizeof(buf_[0]);
}

uint8_t* CGA::getFrontBuffer() {
    return &buf_[frontBuf_][0];
}

size_t CGA::getFrontBufferSize() const {
    return sizeof(buf_[0]);
}

Crtc6845* CGA::crtc() {
    return &crtc_;
}

const Crtc6845* CGA::crtc() const {
    return &crtc_;
}

