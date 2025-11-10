#pragma once

#include <cstring>
#include <iostream>

#include "Crtc.h"
#include "font.h"

#define VRAM_SIZE 0x4000
#define CGA_APERTURE_MASK 0x3FFF

#define HCHAR_CLOCK_MASK 0x07
#define HCHAR_ODD_CLOCK_MASK 0x0F
#define LCHAR_CLOCK_MASK 0x0F
#define LCHAR_ODD_CLOCK_MASK 0x1F

static constexpr std::array<uint64_t, 16> CGA_COLORS_U64 = {
    0x0000000000000000,
    0x0101010101010101,
    0x0202020202020202,
    0x0303030303030303,
    0x0404040404040404,
    0x0505050505050505,
    0x0606060606060606,
    0x0707070707070707,
    0x0808080808080808,
    0x0909090909090909,
    0x0A0A0A0A0A0A0A0A,
    0x0B0B0B0B0B0B0B0B,
    0x0C0C0C0C0C0C0C0C,
    0x0D0D0D0D0D0D0D0D,
    0x0E0E0E0E0E0E0E0E,
    0x0F0F0F0F0F0F0F0F,
};

static constexpr std::array<uint64_t, 256> makeCga8BitTable() {
    std::array<uint64_t, 256> table{};

    for (uint16_t glyph = 0; glyph < 256; ++glyph) {
        uint64_t glyph_u64 = 0;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            bool bit_val = glyph & (0x01 << (7 - bit));
            glyph_u64 |= (static_cast<uint64_t>(bit_val ? 0xFF : 0x00) << (bit * 8));
        }
        table[glyph] = glyph_u64;
    }

    return table;
}

constexpr std::array<std::array<uint64_t, 8>, 256> makeCgaHiresGlyphTable() {
    std::array<std::array<uint64_t, 8>, 256> table{};

    for (size_t glyph = 0; glyph < 256; ++glyph) {
        for (size_t row = 0; row < 8; ++row) {
            uint64_t glyph_u64 = 0;

            for (int bit = 0; bit < 8; ++bit) {
                size_t glyph_offset = CGA_NORMAL_FONT_OFFSET + glyph * 8 + row;
                bool bit_val = (CGA_FONT_ROM[glyph_offset] & (0x01 << (7 - bit))) != 0;

                if (bit_val) {
                    glyph_u64 |= (0xFFull) << (bit * 8);
                }
            }

            table[glyph][row] = glyph_u64;
        }
    }

    return table;
}

class CGA
{
    static constexpr size_t CGA_CURSOR_MAX = 32;

    static constexpr uint32_t CGA_DEFAULT_CURSOR_FRAME_CYCLE = 8;
    static constexpr uint32_t CGA_MONITOR_VSYNC_MIN = 64;

    static constexpr uint32_t HCHAR_CLOCK = 8;
    static constexpr uint32_t LCHAR_CLOCK = 16;
    static constexpr uint32_t CRTC_R0_HORIZONTAL_MAX = 113;
    static constexpr uint32_t CRTC_SCANLINE_MAX = 262;
    static constexpr uint32_t CGA_XRES_MAX = (CRTC_R0_HORIZONTAL_MAX + 1) * HCHAR_CLOCK;
    static constexpr uint32_t CGA_YRES_MAX = CRTC_SCANLINE_MAX;
    static constexpr size_t CGA_MAX_CLOCK = (CGA_XRES_MAX * CGA_YRES_MAX); // Should be 238944

    const uint8_t MODE_MATCH_MASK = 0b0001'1111;
    const uint8_t MODE_HIRES_TEXT = 0b0000'0001;
    const uint8_t MODE_GRAPHICS = 0b0000'0010;
    const uint8_t MODE_BW = 0b0000'0100;
    const uint8_t MODE_ENABLE = 0b0000'1000;
    const uint8_t MODE_HIRES_GRAPHICS = 0b0001'0000;
    const uint8_t MODE_BLINKING = 0b0010'0000;

    const uint8_t STATUS_DISPLAY_ENABLE = 0b0000'0001;
    const uint8_t STATUS_LIGHTPEN_TRIGGER_SET = 0b0000'0010;
    const uint8_t STATUS_LIGHTPEN_SWITCH_STATUS = 0b0000'0100;
    const uint8_t STATUS_VERTICAL_RETRACE = 0b0000'1000;

    const uint8_t CC_ALT_COLOR_MASK = 0b0000'0111;
    const uint8_t CC_ALT_INTENSITY = 0b0000'1000;
    const uint8_t CC_BRIGHT_BIT = 0b0001'0000; // Controls whether palette is high intensity
    const uint8_t CC_PALETTE_BIT = 0b0010'0000; // Controls primary palette between magenta/cyan and red/green

    const size_t CGA_TEXT_MODE_WRAP{0x1FFF};
    const size_t CGA_GFX_MODE_WRAP{0x3FFF};

public:
    CGA();
    void reset();
    void tick();

    uint8_t* getMem();
    [[nodiscard]] size_t getMemSize() const;

    uint8_t* getBackBuffer();
    size_t getBackBufferSize() const;
    // Front buffer accessors (visible rasterized indexed buffer)
    uint8_t* getFrontBuffer();
    size_t getFrontBufferSize() const;

    // Expose the internal CRTC for debugging/inspection
    Crtc6845* crtc();
    const Crtc6845* crtc() const;

    uint8_t readMem(uint16_t address) const;
    void writeMem(uint16_t address, uint8_t data);

    uint8_t readIo(uint16_t address);
    void writeIo(uint16_t address, uint8_t data);
    [[nodiscard]] uint8_t readStatusRegister() const;
    void writeModeRegister(uint8_t data);
    void writeColorControlRegister(uint8_t data);
    void clearLPLatch() {
        lpLatch_ = false;
    }
    void setLPLatch() {
        lpLatch_ = true;
    };

private:

    void tick_hchar();
    void tick_lchar();
    void updateMode();
    void updatePalette();
    void updateClock();

    alignas(8) uint8_t vram_[VRAM_SIZE]{};
    bool cursorData_[CGA_CURSOR_MAX]{};
    uint8_t buf_[2][CGA_MAX_CLOCK]{};
    size_t backBuf_{0};
    size_t frontBuf_{1};

    Crtc6845 crtc_{};
    uint8_t cgaPhase_{0};
    uint64_t ticks_{0};

    int clockDivisor_{1};
    uint64_t charClock_{HCHAR_CLOCK};
    uint64_t charClockMask_{HCHAR_CLOCK_MASK};
    uint64_t charClockOddMask_{LCHAR_CLOCK_MASK};

    size_t vma_{0};
    size_t rba_{0}; // raster buffer address

    // Light pen state
    bool lpLatch_{false};
    bool lpSwitch_{false};

    // Cursor state
    bool cursorBlink_{false};
    bool cursorStatus_{false};
    bool blinkState_{false};

    // Mode register bits
    uint8_t modeByte_{0};
    bool modePending_{false};
    bool clockPending_{false};
    bool modeEnable_{false};
    bool modeBW_{false};
    bool modeGraphics_{false};
    bool modeBlinking_{false};
    bool modeHiresGfx_{false};
    bool modeHiresText_{false};

    // Monitor simulation
    bool monitorHSync_{false};
    bool monitorVSync_{false};
    uint32_t beamX_{0};
    uint32_t beamY_{0};
    uint32_t scanline_{0};
    // Colors, character and attribute

    // Current foreground color
    uint8_t curFg_{0};
    // Current background color
    uint8_t curBg_{0};
    uint8_t ccRegisterByte_{0};
    uint8_t ccOverscanColor_{0};
    uint8_t ccAltColor_{0};
    uint8_t ccPalette_{0};
    uint8_t curChar_{0};
    uint8_t curAttr_{0};

    uint64_t frameCount_{0};

    static constexpr std::array<uint64_t, 256> CGA_8BIT_TABLE = makeCga8BitTable();
    static constexpr std::array<std::array<uint64_t, 8>, 256> CGA_HIRES_GLYPH_TABLE = makeCgaHiresGlyphTable();

    // ReSharper disable once CppMemberFunctionMayBeConst
    void draw_solid_hchar(uint8_t color) {
        const auto frame_u64 = reinterpret_cast<uint64_t *>(buf_[backBuf_]);
        frame_u64[rba_ >> 3] = CGA_COLORS_U64[color & 0x0F];
    }

    [[nodiscard]] uint64_t get_hchar_glyph_row(uint8_t glyph, uint8_t row) const {
        if (cursorBlink_ && !cursorStatus_) {
            return CGA_COLORS_U64[curBg_];
        }

        const auto glyph_row_base = CGA_HIRES_GLYPH_TABLE[glyph & 0xFF][row & 0x07];

        // Combine glyph mask with foreground and background colors.
        return glyph_row_base & CGA_COLORS_U64[curFg_] | ~glyph_row_base & CGA_COLORS_U64[curBg_];
    }

    /// Draw an entire character row in high resolution text mode (8 pixels)
    void draw_text_mode_hchar() {
        // Do cursor if visible, enabled and defined
        if (vma_ == crtc_.cursor_address()
            && cursorStatus_
            && blinkState_
            && cursorData_[(crtc_.vlc() & 0x1F)])
            {
                draw_solid_hchar(curFg_);
            }
        else if (modeEnable_) {
            // Get the u64 glyph row to draw for the current fg and bg colors and character row (vlc)
            const auto glyph_row = get_hchar_glyph_row(curChar_, crtc_.vlc());
            const auto frame_u64 = reinterpret_cast<uint64_t *>(buf_[backBuf_]);
            frame_u64[rba_ >> 3] = glyph_row;
        }
        else {
            // When mode bit is disabled in text mode, the CGA acts like VRAM is all 0.
            draw_solid_hchar(0);
        }
    }

    void fetch_char() {
        // Address from CRTC is masked by 0x1FFF by the CGA card (bit 13 ignored) and doubled.
        const auto addr = (vma_ & CGA_TEXT_MODE_WRAP) << 1;

        curChar_ = vram_[addr];
        curAttr_ = vram_[addr + 1];
        curFg_ = curAttr_ & 0x0F;

        // If blinking is enabled, the bg attribute is only 3 bits and only low-intensity colors are available.
        // If blinking is disabled, all 16 colors are available as background attributes.
        if (modeBlinking_) {
            curBg_ = (curAttr_ >> 4) & 0x07;
            cursorBlink_ = (curAttr_ & 0x80) != 0;
        }
        else {
            curBg_ = (curAttr_ >> 4);
            cursorBlink_ = false;
        }
    }

    void hsync() {
        scanline_ += 1;
        // Reset beam to left of screen if we haven't already
        if (beamX_ > 0) {
            beamY_++;
        }
        beamX_ = 0;
        rba_ = CGA_XRES_MAX * beamY_;
    }

    void vsync() {
        // Only do a vsync if we are past the minimum scanline #.
        // A monitor will refuse to vsync too quickly.
        if (beamY_ > CGA_MONITOR_VSYNC_MIN) {
            beamX_ = 0;
            beamY_ = 0;
            rba_ = 0;
            scanline_ = 0;
            frameCount_++;

            // Toggle blink state. This is toggled every 8 frames by default.
            if ((frameCount_ % CGA_DEFAULT_CURSOR_FRAME_CYCLE) == 0) {
                cursorBlink_ = !cursorBlink_;
            }

            // Swap the display buffers
            //std::cout << "vsync: swapping buffers\n";
            swap();
        }
    }

    void swap() {
        if (backBuf_ == 0) {
            frontBuf_ = 0;
            backBuf_ = 1;
        }
        else {
            frontBuf_ = 1;
            backBuf_ = 0;
        }
        // Clear new back buffer.
        memset(&buf_[backBuf_], 0, sizeof(buf_[0]));
    }
};
