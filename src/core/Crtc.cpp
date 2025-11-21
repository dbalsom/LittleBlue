#include "Crtc.h"
#include <iostream>

Crtc6845::Crtc6845() {
    reg_.fill(0);
    cursor_data_.fill(false);
    // Default blink rate = fast
    has_cursor_blink_rate_ = true;
    cursor_blink_rate_ = BLINK_FAST_RATE;
}

// Write to a CRTC register.
// rel_port: 0 = address/select, 1 = data
void Crtc6845::write(const uint16_t rel_port, const uint8_t data) {
    switch (rel_port & 0x01) {
        case 0:
            // address / register select
            select_register(data);
            break;
        case 1:
            // register data write
            write_register(data);
            break;
        default:
            break;
    }
}

// Read from a CRTC register.
uint8_t Crtc6845::read(const uint16_t rel_port) const {
    switch (rel_port & 0x01) {
        case 0:
            // address register not readable
            return 0xFF;
        case 1:
            // data register (partially readable)
            return read_register();
        default:
            return 0xFF;
    }
}

void Crtc6845::select_register(const uint8_t idx) {
    if (idx > REGISTER_MAX) {
        return;
    }
    switch (idx) {
        case 0:
            reg_select_ = CrtcRegister::HorizontalTotal;
            break;
        case 1:
            reg_select_ = CrtcRegister::HorizontalDisplayed;
            break;
        case 2:
            reg_select_ = CrtcRegister::HorizontalSyncPosition;
            break;
        case 3:
            reg_select_ = CrtcRegister::SyncWidth;
            break;
        case 4:
            reg_select_ = CrtcRegister::VerticalTotal;
            break;
        case 5:
            reg_select_ = CrtcRegister::VerticalTotalAdjust;
            break;
        case 6:
            reg_select_ = CrtcRegister::VerticalDisplayed;
            break;
        case 7:
            reg_select_ = CrtcRegister::VerticalSync;
            break;
        case 8:
            reg_select_ = CrtcRegister::InterlaceMode;
            break;
        case 9:
            reg_select_ = CrtcRegister::MaximumScanlineAddress;
            break;
        case 10:
            reg_select_ = CrtcRegister::CursorStartLine;
            break;
        case 11:
            reg_select_ = CrtcRegister::CursorEndLine;
            break;
        case 12:
            reg_select_ = CrtcRegister::StartAddressH;
            break;
        case 13:
            reg_select_ = CrtcRegister::StartAddressL;
            break;
        case 14:
            reg_select_ = CrtcRegister::CursorAddressH;
            break;
        case 15:
            reg_select_ = CrtcRegister::CursorAddressL;
            break;
        case 16:
            reg_select_ = CrtcRegister::LightPenPositionH;
            break;
        default:
            reg_select_ = CrtcRegister::LightPenPositionL;
            break;
    }
}

void Crtc6845::write_register(const uint8_t byte) {
    switch (reg_select_) {
        case CrtcRegister::HorizontalTotal:
            // R0: 8-bit
            reg_[0] = byte;
            break;

        case CrtcRegister::HorizontalDisplayed:
            // R1: 8-bit
            reg_[1] = byte;
            break;

        case CrtcRegister::HorizontalSyncPosition:
            // R2: 8-bit
            reg_[2] = byte;
            break;

        case CrtcRegister::SyncWidth:
            // R3: 8-bit
            reg_[3] = byte;
            break;

        case CrtcRegister::VerticalTotal:
            // R4: 7-bit
            reg_[4] = static_cast<uint8_t>(byte & 0x7F);
            //std::cout << "CRTC Register Write (04h): VerticalTotal updated: " << static_cast<unsigned>(reg_[4]);
            break;

        case CrtcRegister::VerticalTotalAdjust:
            // R5: 5-bit
            reg_[5] = static_cast<uint8_t>(byte & 0x1F);
            break;

        case CrtcRegister::VerticalDisplayed:
            // R6: 7-bit
            reg_[6] = static_cast<uint8_t>(byte & 0x7F);
            break;

        case CrtcRegister::VerticalSync:
            // R7: 7-bit
            reg_[7] = static_cast<uint8_t>(byte & 0x7F);
            trace_regs_();
            //std::cout <<  "CRTC Register Write (07h): VerticalSync updated: " << static_cast<unsigned>(reg_[7]);
            break;

        case CrtcRegister::InterlaceMode:
            // R8: 2-bit
            reg_[8] = static_cast<uint8_t>(byte & 0x03);
            break;

        case CrtcRegister::MaximumScanlineAddress:
            // R9: 5-bit
            reg_[9] = static_cast<uint8_t>(byte & 0x1F);
            update_cursor_data();
            break;

        case CrtcRegister::CursorStartLine:
        {
            // R10: 7-bit field; includes cursor attrs in upper nibble
            reg_[10] = static_cast<uint8_t>(byte & 0x7F);

            cursor_start_line_ = static_cast<uint8_t>(byte & CURSOR_LINE_MASK);

            // IMPORTANT: parentheses — we want (byte & mask) >> 4
            const uint8_t attr = static_cast<uint8_t>((byte & CURSOR_ATTR_MASK) >> 5);
            switch (attr) {
                case 0b00:
                    cursor_enabled_ = true;
                    has_cursor_blink_rate_ = false; // solid
                    break;
                case 0b01:
                    cursor_enabled_ = false; // disabled (some hardware still blinks visually, but we gate here)
                    has_cursor_blink_rate_ = false;
                    break;
                case 0b10:
                    cursor_enabled_ = true;
                    has_cursor_blink_rate_ = true;
                    cursor_blink_rate_ = BLINK_FAST_RATE;
                    break;
                default:
                    cursor_enabled_ = true;
                    has_cursor_blink_rate_ = true;
                    cursor_blink_rate_ = BLINK_SLOW_RATE;
                    break;
            }
            update_cursor_data();
            break;
        }

        case CrtcRegister::CursorEndLine:
            // R11: 5-bit
            reg_[11] = static_cast<uint8_t>(byte & CURSOR_LINE_MASK);
            update_cursor_data();
            break;

        case CrtcRegister::StartAddressH:
            // R12: 6-bit
            reg_[12] = static_cast<uint8_t>(byte & 0x3F);
            //std::cout << "CRTC Register Write (0Ch): StartAddressH updated: " << std::hex << std::uppercase << static_cast<unsigned>(byte);
            update_start_address();
            break;

        case CrtcRegister::StartAddressL:
            // R13: 8-bit
            reg_[13] = byte;
            //std::cout << "CRTC Register Write (0Dh): StartAddressL updated: " << std::hex << std::uppercase << static_cast<unsigned>(byte);
            update_start_address();
            break;

        case CrtcRegister::CursorAddressH:
            // R14: 6-bit, readable
            reg_[14] = static_cast<uint8_t>(byte & 0x3F);
            update_cursor_address();
            break;

        case CrtcRegister::CursorAddressL:
            // R15: 8-bit, readable
            reg_[15] = byte;
            update_cursor_address();
            break;

        case CrtcRegister::LightPenPositionH:
        case CrtcRegister::LightPenPositionL:
            // R16: read-only
            // R17: read-only
            break;
    }
}

uint8_t Crtc6845::read_register() const {
    switch (reg_select_) {
        case CrtcRegister::CursorAddressH:
        case CrtcRegister::CursorAddressL:
        case CrtcRegister::LightPenPositionH:
        case CrtcRegister::LightPenPositionL:
            return reg_[static_cast<size_t>(reg_select_)];
        default:
            return REGISTER_UNREADABLE_VALUE;
    }
}

void Crtc6845::update_start_address() {
    start_address_ = static_cast<uint16_t>((static_cast<uint16_t>(reg_[12]) << 8) | reg_[13]);
}

void Crtc6845::update_cursor_address() {
    cursor_address_ = static_cast<uint16_t>((static_cast<uint16_t>(reg_[14]) << 8) | reg_[15]);
}

void Crtc6845::update_cursor_data() {
    cursor_data_.fill(false);

    // If start line > max scanline, cursor never shown
    if (reg_[10] > reg_[9]) {
        return;
    }

    if (reg_[10] <= reg_[11]) {
        // normal contiguous
        for (uint8_t i = reg_[10]; i <= reg_[11]; ++i) {
            if (i < CRTC_ROW_MAX)
                cursor_data_[i] = true;
        }
        cursor_start_line_ = reg_[10];
        cursor_end_line_ = reg_[11];
    }
    else {
        // split cursor (wraps)
        for (uint8_t i = 0; i <= reg_[11] && i < CRTC_ROW_MAX; ++i) {
            cursor_data_[i] = true;
        }
        for (auto i = static_cast<size_t>(reg_[10]); i < CRTC_ROW_MAX; ++i) {
            cursor_data_[i] = true;
        }
        cursor_start_line_ = reg_[10];
        cursor_end_line_ = static_cast<uint8_t>(CRTC_ROW_MAX - 1);
    }
}

// Return the immediate status of the cursor
bool Crtc6845::cursor_immediate() const {
    bool cur = cursor_enabled_
        && (vma_ == cursor_address_)
        && cursor_data_[static_cast<size_t>(vlc_c9_ & 0x1F)];

    if (has_cursor_blink_rate_) {
        cur = cur && blink_state_;
    }
    return cur;
}

// Tick the CRTC
// Returns (status_ptr, current_vma)
std::pair<const Crtc6845::CrtcStatusBits*, uint16_t>
Crtc6845::tick(const HBlankCallback& hblank_cb) {
    // transient pulses low unless we fire them this tick
    status_.hsync = false;
    status_.vsync = false;

    if (hcc_c0_ == 0) {
        status_.hborder = false;
        if (vcc_c4_ == 0) {
            // We are at the first character of a CRTC frame. Update start address.
            vma_ = start_address_latch_;
        }
    }

    if (hcc_c0_ < 2) {
        // When C0 < 2 evaluate last_line flag status.
        // LOGON SYSTEM v1.6 pg 73
        if (vcc_c4_ == reg_[4]) {
            last_row_ = true;
            last_line_ = (vlc_c9_ == reg_[9]);
            vtac_c5_ = 0;
        }
    }

    // Update horizontal character counter
    hcc_c0_++;

    // Advance video memory address offset
    vma_++;
    char_col_ = 0;

    // Process horizontal blanking period
    if (status_.hblank) {
        // increment HSYNC counter
        hsc_c3l_++;

        // Allow the adapter to supply effective HSYNC width
        const uint8_t eff = hblank_cb ? hblank_cb() : reg_[3];
        hsync_target_ = std::min(eff, reg_[3]);

        if (hsc_c3l_ == hsync_target_) {
            // Logical end of scanline (fire HSYNC pulse)
            if (status_.vblank) {
                // Count VSYNC lines during vblank
                //vsc_c3h_++;
                if (vsc_c3h_ == CRTC_VBLANK_HEIGHT) {
                    in_last_vblank_line_ = true;
                    vsc_c3h_ = 0;
                    status_.vsync = true;
                }
            }

            char_col_ = 0;
            status_.hsync = true;
        }

        // End HBLANK when we reach R3 (sync width)
        if (hsc_c3l_ == reg_[3]) {
            status_.hblank = false;
            hsc_c3l_ = 0;
        }
    }

    if (hcc_c0_ == reg_[1]) {
        // C0 == R1. Entering right overscan.
        if (vlc_c9_ == reg_[9]) {
            // Last scanline of this character row; save VMA' for next row
            vma_t_ = vma_;
        }
        status_.den = false;
        status_.hborder = true;
    }

    if (hcc_c0_ == reg_[2]) {
        // Enter HBLANK at HorizontalSyncPosition
        const uint8_t eff = hblank_cb ? hblank_cb() : reg_[3];
        hsync_target_ = eff;
        status_.hblank = true;
        hsc_c3l_ = 0;
    }

    if ((hcc_c0_ == (reg_[0] + 1)) && in_last_vblank_line_) {
        // Right before the new frame begins, draw one char of border.
        // When we roll to +1 we clear VBLANK soon after.
        status_.hborder = true;
    }

    if (hcc_c0_ == (reg_[0] + 1)) {
        // C0 == R0: Leaving left overscan, finished scanning row

        if (status_.vblank) {
            // If we are in VBLANK, advance Vertical Sync Counter
            vsc_c3h_ += 1;
        }

        if (in_last_vblank_line_) {
            // Leave VBLANK after last line.
            in_last_vblank_line_ = false;
            status_.vblank = false;
        }

        // Reset Horizontal Character Counter and increment character row counter
        hcc_c0_ = 0;
        status_.hborder = false;
        // Wrap vertical line counter (5 bits)
        vlc_c9_ = (vlc_c9_ + 1) & 0x1F;

        // Return video memory address to starting position for next character row
        vma_ = vma_t_;
        char_col_ = 0;

        if (!status_.vblank && (vcc_c4_ < reg_[6])) {
            // Start the new row
            status_.den = true;
            status_.hborder = false;
        }

        if (vlc_c9_ == reg_[9] + 1) {
            // C9 == R9 We finished drawing this row of characters
            vlc_c9_ = 0;
            // Increment Vertical Character Counter for next row
            vcc_c4_++;
            // Set vma to starting position for next character row
            vma_ = vma_t_;

            if (vcc_c4_ == reg_[7]) {
                // C4 == R7: We've reached vertical sync
                status_.vblank = true;
                status_.den = false;

                if (has_cursor_blink_rate_) {
                    cursor_blink_ct_++;
                    if (cursor_blink_ct_ == cursor_blink_rate_) {
                        cursor_blink_ct_ = 0;
                        blink_state_ = !blink_state_;
                    }
                }
            }

            if (last_line_) {
                in_vta_ = true;
                last_row_ = false;
                last_line_ = false;
            }
        }

        if (vcc_c4_ == reg_[6]) {
            // C4 == R6: Enter lower overscan area.
            status_.den = false;
            status_.vborder = true;
        }

        // if (vcc_c4_ == reg_[4] + 1) {
        //     // We are at vertical total, start incrementing vertical total adjust counter.
        //     in_vta_ = true;
        // }

        if (in_vta_) {
            // We are in vertical total adjust.
            if (vtac_c5_ == reg_[5]) {
                // We have reached vertical total adjust. We are at the end of the top overscan.
                in_vta_ = false;
                vtac_c5_ = 0;
                hcc_c0_ = 0;
                vcc_c4_ = 0;
                vlc_c9_ = 0;
                char_col_ = 0;

                start_address_latch_ = start_address_;
                vma_ = start_address_;
                vma_t_ = vma_;

                status_.den = true;
                status_.vborder = false;
                status_.vblank = false;
            }
            else {
                vtac_c5_++;
            }
        }
    }

    // Update cursor bit based on current position
    status_.cursor = cursor_immediate();

    return {&status_, vma_};
}
