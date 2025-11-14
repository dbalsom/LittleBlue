//
// Created by Daniel on 11/7/2025.
//

#include "Cga.h"

#include <format>
#include <iostream>


uint8_t CGA::readIO(uint16_t address) {
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

void CGA::writeIO(uint16_t address, uint8_t data) {
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
    std::cout << std::format("Write to CGA mode register: {:02X}", mode_byte) << std::endl;
    if (is_deferred_mode_change(mode_byte)) {
        // Latch the mode change and mark it pending. We will change the mode on next hsync.
        mode_pending_ = true;
        mode_byte_ = mode_byte;
    }
    else {
        // We're not changing from text to graphics or vice versa, so we do not have to
        // defer the update.
        mode_byte_ = mode_byte;
        updateMode();
    }
}

void CGA::updateMode() {
    // Will this mode change the character clock?
    auto clock_changed = mode_hires_text_ != ((mode_byte_ & MODE_HIRES_TEXT) != 0);
    if (clock_changed) {
        // Flag the clock for pending change.  The clock can only be changed in phase with
        // LCHAR due to our dynamic clocking logic.
        std::cout << "CGA: Clock change pending";
        clock_pending_ = true;
    }

    mode_hires_text_ = (mode_byte_ & MODE_HIRES_TEXT) != 0;
    mode_graphics_ = (mode_byte_ & MODE_GRAPHICS) != 0;
    mode_bw_ = (mode_byte_ & MODE_BW) != 0;
    mode_enable_ = (mode_byte_ & MODE_ENABLE) != 0;
    mode_hires_gfx_ = (mode_byte_ & MODE_HIRES_GRAPHICS) != 0;
    mode_blinking_ = (mode_byte_ & MODE_BLINKING) != 0;

    // Use color control register value for overscan unless high-res graphics mode,
    // in which case overscan must be black (0).
    if (mode_hires_gfx_) {
        cc_overscan_color_ = 0;
    }
    else {
        cc_overscan_color_ = cc_alt_color_;
    };

    // Reinterpret the CC register based on new mode.
    updatePalette();

    // Attempt to update clock.
    updateClock();
}

void CGA::writeColorControlRegister(uint8_t data) {
    cc_register_byte_ = data;
    updatePalette();
}

void CGA::updateClock() {
    if (clock_pending_ && (ticks_ & 0x0F) == 0) {
        // Clock divisor is 1 in high-res text mode, 2 in all other modes
        // We draw pixels twice when clock divisor is 2 to simulate slower scanning.
        if (mode_hires_text_) {
            clock_divisor_ = 1;
            char_clock_ = HCHAR_CLOCK;
            char_clock_mask_ = HCHAR_CLOCK_MASK;
            char_clock_odd_mask_ = HCHAR_ODD_CLOCK_MASK;
        }
        else {
            clock_divisor_ = 2;
            char_clock_ = LCHAR_CLOCK;
            char_clock_mask_ = LCHAR_CLOCK_MASK;
            char_clock_odd_mask_ = LCHAR_ODD_CLOCK_MASK;
        }
        clock_pending_ = false;
    }
}

void CGA::updatePalette() {
    if (mode_bw_ && mode_graphics_ && !mode_hires_gfx_) {
        cc_palette_ = 4; // Select Red, Cyan and White palette (undocumented)
    }
    else if ((cc_register_byte_ & CC_PALETTE_BIT) != 0) {
        cc_palette_ = 2; // Select Magenta, Cyan, White palette
    }
    else {
        cc_palette_ = 0; // Select Red, Green, 'Yellow' palette
    }

    if ((cc_register_byte_ & CC_BRIGHT_BIT) != 0) {
        cc_palette_++; // Switch to high-intensity palette
    }

    cc_alt_color_ = cc_register_byte_ & 0x0F;
    if (!mode_hires_gfx_) {
        cc_overscan_color_ = cc_alt_color_;
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
    if (lp_latch_) {
        byte |= STATUS_LIGHTPEN_TRIGGER_SET;
    }
    if (lp_switch_) {
        byte |= STATUS_LIGHTPEN_SWITCH_STATUS;
    }
    return byte;
}

uint8_t* CGA::getBackBuffer() {
    return &buf_[back_buf_][0];
}

size_t CGA::getBackBufferSize() const {
    return sizeof(buf_[0]);
}

uint8_t* CGA::getFrontBuffer() {
    return &buf_[front_buf_][0];
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
