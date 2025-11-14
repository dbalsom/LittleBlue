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

struct CgaDebugState
{
    uint8_t mode_byte;
    bool mode_hires_text : 1;
    bool mode_graphics : 1;
    bool mode_bw : 1;
    bool mode_enable : 1;
    bool mode_hires_gfx : 1;
    bool mode_blinking : 1;
    uint8_t cc_register_byte;
    uint8_t clock_divisor;

};

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

constexpr std::array<std::array<std::array<uint64_t, 2>, 8>, 256> makeCgaLowresGlyphTable() {
    std::array<std::array<std::array<uint64_t, 2>, 8>, 256> table{};

    for (size_t glyph = 0; glyph < 256; ++glyph) {
        for (size_t row = 0; row < 8; ++row) {
            // Unpack left half of glyph (pixel-doubled)
            uint64_t glyph_u64 = 0;
            for (int bit = 0; bit < 4; ++bit) {
                size_t glyph_offset = CGA_NORMAL_FONT_OFFSET + glyph * 8 + row;
                if ((CGA_FONT_ROM[glyph_offset] & (0x01 << (7 - bit))) != 0) {
                    glyph_u64 |= 0xFFull << ((bit * 2) * 8);
                    glyph_u64 |= 0xFFull << (((bit * 2) + 1) * 8);
                }
            }
            table[glyph][row][0] = glyph_u64;

            // Unpack right half of glyph (pixel-doubled)
            glyph_u64 = 0;
            for (int bit = 0; bit < 4; ++bit) {
                size_t glyph_offset = CGA_NORMAL_FONT_OFFSET + glyph * 8 + row;
                if ((CGA_FONT_ROM[glyph_offset] & (0x01 << (3 - bit))) != 0) {
                    glyph_u64 |= 0xFFull << ((bit * 2) * 8);
                    glyph_u64 |= 0xFFull << (((bit * 2) + 1) * 8);
                }
            }
            table[glyph][row][1] = glyph_u64;
        }
    }

    return table;
}

static constexpr std::array<std::array<std::pair<uint64_t, uint64_t>, 256>, 6> makeCgaLowresGraphicsTable() {
    std::array<std::array<std::pair<uint64_t, uint64_t>, 256>, 6> table{};

    const std::array<std::array<uint8_t, 4>, 6> CGA_PALETTES = {{
        {{0, 2, 4, 6}}, // Red / Green / Brown
        {{0, 10, 12, 14}}, // Red / Green / Brown High Intensity
        {{0, 3, 5, 7}}, // Cyan / Magenta / White
        {{0, 11, 13, 15}}, // Cyan / Magenta / White High Intensity
        {{0, 3, 4, 7}}, // Red / Cyan / White
        {{0, 11, 12, 15}}, // Red / Cyan / White High Intensity
    }};

    for (std::size_t palette_i = 0; palette_i < 6; ++palette_i) {
        for (uint16_t glyph = 0; glyph < 256; ++glyph) {
            // Break out 8-bit pattern into four 2-bit pixels
            const uint8_t pix0 = (glyph >> 6) & 0b11;
            const uint8_t pix1 = (glyph >> 4) & 0b11;
            const uint8_t pix2 = (glyph >> 2) & 0b11;
            const uint8_t pix3 = glyph & 0b11;

            // Look up 2-bit pixel indices into palette
            uint64_t color0 = CGA_PALETTES[palette_i][pix0];
            uint64_t color1 = CGA_PALETTES[palette_i][pix1];
            uint64_t color2 = CGA_PALETTES[palette_i][pix2];
            uint64_t color3 = CGA_PALETTES[palette_i][pix3];

            // Double pixels (repeat in low and high byte)
            color0 |= color0 << 8;
            color1 |= color1 << 8;
            color2 |= color2 << 8;
            color3 |= color3 << 8;

            // Build mask: 0xFFFF if color index == 0, else 0
            const uint64_t mask0 = (pix0 == 0) ? 0xFFFFu : 0x0000u;
            const uint64_t mask1 = (pix1 == 0) ? 0xFFFFu : 0x0000u;
            const uint64_t mask2 = (pix2 == 0) ? 0xFFFFu : 0x0000u;
            const uint64_t mask3 = (pix3 == 0) ? 0xFFFFu : 0x0000u;

            // Assemble 64-bit glyph and mask values (mirroring Rust order)
            uint64_t glyph64 = (color3 << 48) | (color2 << 32) | (color1 << 16) | color0;
            uint64_t mask64 = (mask3 << 48) | (mask2 << 32) | (mask1 << 16) | mask0;

            table[palette_i][glyph] = {glyph64, mask64};
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
    CGA() {
        reset();
    }

    void reset() {
        cga_phase_ = 0;
        crtc_.reset();
        memset(vram_, 0, sizeof(vram_));
        memset(buf_, 0, sizeof(buf_));
        back_buf_ = 0;
        front_buf_ = 1;

        cga_phase_ = 0;
        ticks_ = 0;

        clock_divisor_ = 1;
        char_clock_ = HCHAR_CLOCK;
        char_clock_mask_ = HCHAR_CLOCK_MASK;
        char_clock_odd_mask_ = LCHAR_CLOCK_MASK;

        vma_ = 0;
        rba_ = 0;

        lp_latch_ = false;
        lp_switch_ = false;

        cursor_blink_ = false;
        cursor_status_ = false;
        blink_state_ = false;

        mode_byte_ = 0;
        mode_pending_ = false;
        clock_pending_ = false;
        mode_enable_ = false;
        mode_bw_ = false;
        mode_graphics_ = false;
        mode_blinking_ = false;
        mode_hires_gfx_ = false;
        mode_hires_text_ = false;

        monitor_hsync_ = false;
        monitor_vsync_ = false;
        beam_x_ = 0;
        beam_y_ = 0;
        scanline_ = 0;

        cur_fg_ = 0;
        cur_bg_ = 0;
        cc_register_byte_ = 0;
        cc_overscan_color_ = 0;
        cc_alt_color_ = 0;
        cc_palette_ = 0;
        cur_char_ = 0;
        cur_attr_ = 0;

        frame_count_ = 0;
    }

    CgaDebugState getDebugState() const {
        CgaDebugState state{};
        state.mode_byte = mode_byte_;
        state.mode_hires_text = mode_hires_text_;
        state.mode_graphics = mode_graphics_;
        state.mode_bw = mode_bw_;
        state.mode_enable = mode_enable_;
        state.mode_hires_gfx = mode_hires_gfx_;
        state.mode_blinking = mode_blinking_;
        state.cc_register_byte = cc_register_byte_;
        state.clock_divisor = static_cast<uint8_t>(clock_divisor_);
        return state;
    }

    uint8_t* getMem() {
        return vram_;
    }

    [[nodiscard]] size_t getMemSize() const {
        return sizeof(vram_);
    }

    [[nodiscard]] uint8_t readMem(const uint16_t address) const {
        return vram_[address & CGA_APERTURE_MASK];
    }

    void writeMem(const uint16_t address, const uint8_t data) {
        vram_[address & CGA_APERTURE_MASK] = data;
    }

    uint8_t* getBackBuffer();
    [[nodiscard]] size_t getBackBufferSize() const;
    // Front buffer accessors (visible rasterized indexed buffer)
    uint8_t* getFrontBuffer();
    [[nodiscard]] size_t getFrontBufferSize() const;

    // Expose the internal CRTC for debugging/inspection
    Crtc6845* crtc();
    [[nodiscard]] const Crtc6845* crtc() const;

    uint8_t readIO(uint16_t address);
    void writeIO(uint16_t address, uint8_t data);
    [[nodiscard]] uint8_t readStatusRegister() const;
    void writeModeRegister(uint8_t data);
    void writeColorControlRegister(uint8_t data);

    void clearLPLatch() {
        lp_latch_ = false;
    }

    void setLPLatch() {
        lp_latch_ = true;
    };

    void tick() {
        ticks_++;
        if ((ticks_ & char_clock_mask_) == 0) {

            if (clock_divisor_ == 2) {
                tick_lchar();
            }
            else {
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
            updateClock();
        }
        cga_phase_ = (cga_phase_ + 3) & 0x0f;
    }

    static std::string getRegisterName(const int reg) {
        switch (reg) {
            case 0:
                return "Horizontal Total";
            case 1:
                return "Horizontal Displayed";
            case 2:
                return "Horizontal Sync Position";
            case 3:
                return "Horizontal Sync Width";
            case 4:
                return "Vertical Total";
            case 5:
                return "Vertical Total Adjust";
            case 6:
                return "Vertical Displayed";
            case 7:
                return "Vertical Sync Position";
            case 8:
                return "Interlace Mode";
            case 9:
                return "Max Scan Line Address";
            case 10:
                return "Cursor Start";
            case 11:
                return "Cursor End";
            case 12:
                return "Start Address (High)";
            case 13:
                return "Start Address (Low)";
            case 14:
                return "Cursor Address (High)";
            case 15:
                return "Cursor Address (Low)";
            case 16:
                return "Light Pen Address (High)";
            case 17:
                return "Light Pen Address (Low)";
            default:
                return "Unknown Register";
        }
    }

private:
    void updateMode();
    void updatePalette();
    void updateClock();

    alignas(8) uint8_t vram_[VRAM_SIZE]{};
    bool cursor_data_[CGA_CURSOR_MAX]{};
    uint8_t buf_[2][CGA_MAX_CLOCK]{};
    size_t back_buf_{0};
    size_t front_buf_{1};

    Crtc6845 crtc_{};
    uint8_t cga_phase_{0};
    uint64_t ticks_{0};

    int clock_divisor_{1};
    uint64_t char_clock_{HCHAR_CLOCK};
    uint64_t char_clock_mask_{HCHAR_CLOCK_MASK};
    uint64_t char_clock_odd_mask_{LCHAR_CLOCK_MASK};

    size_t vma_{0};
    size_t rba_{0}; // raster buffer address

    // Light pen state
    bool lp_latch_{false};
    bool lp_switch_{false};

    // Cursor state
    bool cursor_blink_{false};
    bool cursor_status_{false};
    bool blink_state_{false};

    // Mode register bits
    uint8_t mode_byte_{0};
    bool mode_pending_{false};
    bool clock_pending_{false};
    bool mode_enable_{false};
    bool mode_bw_{false};
    bool mode_graphics_{false};
    bool mode_blinking_{false};
    bool mode_hires_gfx_{false};
    bool mode_hires_text_{false};

    // Monitor simulation
    bool monitor_hsync_{false};
    bool monitor_vsync_{false};
    uint32_t beam_x_{0};
    uint32_t beam_y_{0};
    uint32_t scanline_{0};
    // Colors, character and attribute

    // Current foreground color
    uint8_t cur_fg_{0};
    // Current background color
    uint8_t cur_bg_{0};
    uint8_t cc_register_byte_{0};
    uint8_t cc_overscan_color_{0};
    uint8_t cc_alt_color_{0};
    uint8_t cc_palette_{0};
    uint8_t cur_char_{0};
    uint8_t cur_attr_{0};

    uint64_t frame_count_{0};


    static constexpr auto CGA_LOWRES_GFX_TABLE = makeCgaLowresGraphicsTable();
    static constexpr std::array<uint64_t, 256> CGA_8BIT_TABLE = makeCga8BitTable();
    static constexpr std::array<std::array<uint64_t, 8>, 256> CGA_HIRES_GLYPH_TABLE = makeCgaHiresGlyphTable();
    static constexpr std::array<std::array<std::array<uint64_t, 2>, 8>, 256> CGA_LOWRES_GLYPH_TABLE =
        makeCgaLowresGlyphTable();

    static constexpr std::array<std::array<uint8_t, 4>, 6> CGA_PALETTES = {{
        {{0, 2, 4, 6}}, // Red / Green / Brown
        {{0, 10, 12, 14}}, // Red / Green / Brown High Intensity
        {{0, 3, 5, 7}}, // Cyan / Magenta / White
        {{0, 11, 13, 15}}, // Cyan / Magenta / White High Intensity
        {{0, 3, 4, 7}}, // Red / Cyan / White
        {{0, 11, 12, 15}}, // Red / Cyan / White High Intensity
    }};

    void tick_lchar() {
        //std::cout << "CGA: tick_lchar at beamX=" << beamX_ << " beamY=" << beamY_ << " rba=" << rba_ << "\n";

        // Only render if within display extents
        if (rba_ < (CGA_MAX_CLOCK - 16)) {
            if (crtc_.den()) {
                // Draw current character row
                if (!mode_graphics_) {
                    //std::cout << "CGA: Drawing text mode char at rba=" << rba_ << "\n";
                    draw_text_mode_lchar();
                }
                else if (mode_hires_gfx_) {
                    //draw_hires_gfx_mode_char();
                }
                else {
                    draw_lowres_gfx_mode_char();
                }
            }
            else {
                draw_solid_lchar(7);
            }

            // Update position to next pixel and character column.
            beam_x_ += 8 * clock_divisor_;
            rba_ += 8 * clock_divisor_;

            // If we have reached the right edge of the 'monitor', return the raster position
            // to the left side of the screen.
            if (beam_x_ >= CGA_XRES_MAX) {
                beam_x_ = 0;
                beam_y_ += 1;
                monitor_vsync_ = false;
                rba_ = CGA_XRES_MAX * beam_y_;
            }
        }
    }

    void tick_hchar() {
        //std::cout << "CGA: tick_hchar at beamX=" << beamX_ << " beamY=" << beamY_ << " rba=" << rba_ << "\n";
        // Only render if within display extents
        if (rba_ < (CGA_MAX_CLOCK - 8)) {
            if (crtc_.den()) {
                // Draw current character row
                if (!mode_graphics_) {
                    //std::cout << "CGA: Drawing text mode char at rba=" << rba_ << "\n";
                    draw_text_mode_hchar();
                }
                else if (mode_hires_gfx_) {
                    //draw_hires_gfx_mode_char();
                }
                else {
                    draw_solid_hchar(cc_overscan_color_);
                }
            }
            else {
                draw_solid_hchar(7);
            }

            // Update position to next pixel and character column.
            beam_x_ += 8 * clock_divisor_;
            rba_ += 8 * clock_divisor_;

            // If we have reached the right edge of the 'monitor', return the raster position
            // to the left side of the screen.
            if (beam_x_ >= CGA_XRES_MAX) {
                beam_x_ = 0;
                beam_y_ += 1;
                monitor_vsync_ = false;
                rba_ = CGA_XRES_MAX * beam_y_;
            }
        }
    }


    // ReSharper disable once CppMemberFunctionMayBeConst
    void draw_solid_hchar(const uint8_t color) {
        const auto frame_u64 = reinterpret_cast<uint64_t*>(buf_[back_buf_]);
        frame_u64[rba_ >> 3] = CGA_COLORS_U64[color & 0x0F];
    }

    // ReSharper disable once CppMemberFunctionMayBeConst
    void draw_solid_lchar(const uint8_t color) {
        const auto frame_u64 = reinterpret_cast<uint64_t*>(buf_[back_buf_]);
        frame_u64[rba_ >> 3] = CGA_COLORS_U64[(color & 0x0F)];
        frame_u64[(rba_ >> 3) + 1] = CGA_COLORS_U64[(color & 0x0F)];
    }

    [[nodiscard]] uint64_t get_hchar_glyph_row(const uint8_t glyph, const uint8_t row) const {
        if (cursor_blink_ && !cursor_status_) {
            return CGA_COLORS_U64[cur_bg_];
        }

        const auto glyph_row_base = CGA_HIRES_GLYPH_TABLE[glyph & 0xFF][row & 0x07];

        // Combine glyph mask with foreground and background colors.
        return glyph_row_base & CGA_COLORS_U64[cur_fg_] | ~glyph_row_base & CGA_COLORS_U64[cur_bg_];
    }

    [[nodiscard]] std::pair<uint64_t, uint64_t> get_lchar_glyph_rows(const uint8_t glyph, const uint8_t row) const {
        if (cursor_blink_ && !cursor_status_) {
            const uint64_t bg = CGA_COLORS_U64[cur_bg_];
            return {bg, bg};
        }

        // Table layout: [glyph][row][half]
        auto glyph_row_base_0 = CGA_LOWRES_GLYPH_TABLE[glyph & 0xFF][row & 0x07][0];
        auto glyph_row_base_1 = CGA_LOWRES_GLYPH_TABLE[glyph & 0xFF][row & 0x07][1];

        // Apply foreground/background colors to each half
        const uint64_t row0 =
            (glyph_row_base_0 & CGA_COLORS_U64[cur_fg_])
            | (~glyph_row_base_0 & CGA_COLORS_U64[cur_bg_]);
        const uint64_t row1 =
            (glyph_row_base_1 & CGA_COLORS_U64[cur_fg_])
            | (~glyph_row_base_1 & CGA_COLORS_U64[cur_bg_]);

        return {row0, row1};

    }

    /// Draw an entire character row in high resolution text mode (8 pixels)
    void draw_text_mode_hchar() {
        // Do cursor if visible, enabled and defined
        if (vma_ == crtc_.cursor_address()
            && cursor_status_
            && blink_state_
            && cursor_data_[(crtc_.vlc() & 0x1F)]) {
            draw_solid_hchar(cur_fg_);
        }
        else if (mode_enable_) {
            // Get the u64 glyph row to draw for the current fg and bg colors and character row (vlc)
            const auto glyph_row = get_hchar_glyph_row(cur_char_, crtc_.vlc());
            const auto frame_u64 = reinterpret_cast<uint64_t*>(buf_[back_buf_]);
            frame_u64[rba_ >> 3] = glyph_row;
        }
        else {
            // When mode bit is disabled in text mode, the CGA acts like VRAM is all 0.
            draw_solid_hchar(0);
        }
    }

    void draw_text_mode_lchar() {
        // Do cursor if visible, enabled and defined
        if (vma_ == crtc_.cursor_address()
            && cursor_status_
            && blink_state_
            && cursor_data_[(crtc_.vlc() & 0x1F)]) {
            draw_solid_hchar(cur_fg_);
        }
        else if (mode_enable_) {
            // Get the two u64 glyph row components to draw for the current fg and bg colors and character row (vlc)
            const auto rows = get_lchar_glyph_rows(cur_char_, crtc_.vlc());
            const auto frame_u64 = reinterpret_cast<uint64_t*>(buf_[back_buf_]);
            frame_u64[rba_ >> 3] = rows.first;
            frame_u64[(rba_ >> 3) + 1] = rows.second;
        }
        else {
            // When mode bit is disabled in text mode, the CGA acts like VRAM is all 0.
            draw_solid_hchar(0);
        }
    }


    size_t get_gfx_addr(const uint8_t row) const {
        const auto row_offset = (static_cast<size_t>(row) & 0x01) << 12;
        return (vma_ & 0x0FFF | row_offset) << 1;
    }

    void get_lowres_gfx_lchar(uint8_t row, uint64_t& c0, uint64_t& c1, uint64_t& m0, uint64_t& m1) const {

        auto base_addr = get_gfx_addr(row);
        c0 = CGA_LOWRES_GFX_TABLE[cc_palette_][vram_[base_addr]].first;
        m0 = CGA_LOWRES_GFX_TABLE[cc_palette_][vram_[base_addr]].second;
        c1 = CGA_LOWRES_GFX_TABLE[cc_palette_][vram_[base_addr + 1]].first;
        m1 = CGA_LOWRES_GFX_TABLE[cc_palette_][vram_[base_addr + 1]].second;

    }

    void draw_lowres_gfx_mode_char() {
        if (mode_enable_) {
            uint64_t c0, c1, m0, m1;
            get_lowres_gfx_lchar(crtc_.vlc(), c0, c1, m0, m1);

            const auto frame_u64 = reinterpret_cast<uint64_t*>(buf_[back_buf_]);
            frame_u64[rba_ >> 3] = c0 | (m0 & CGA_COLORS_U64[cc_alt_color_]);
            frame_u64[(rba_ >> 3) + 1] = c1 | (m1 & CGA_COLORS_U64[cc_alt_color_]);
        }
        else {
            draw_solid_lchar(cc_alt_color_);
        }
    }


    void fetch_char() {
        // Address from CRTC is masked by 0x1FFF by the CGA card (bit 13 ignored) and doubled.
        const auto addr = (vma_ & CGA_TEXT_MODE_WRAP) << 1;

        cur_char_ = vram_[addr];
        cur_attr_ = vram_[addr + 1];
        cur_fg_ = cur_attr_ & 0x0F;

        // If blinking is enabled, the bg attribute is only 3 bits and only low-intensity colors are available.
        // If blinking is disabled, all 16 colors are available as background attributes.
        if (mode_blinking_) {
            cur_bg_ = (cur_attr_ >> 4) & 0x07;
            cursor_blink_ = (cur_attr_ & 0x80) != 0;
        }
        else {
            cur_bg_ = (cur_attr_ >> 4);
            cursor_blink_ = false;
        }
    }

    void hsync() {
        scanline_ += 1;
        // Reset beam to left of screen if we haven't already
        if (beam_x_ > 0) {
            beam_y_++;
        }
        beam_x_ = 0;
        rba_ = CGA_XRES_MAX * beam_y_;
    }

    void vsync() {
        // Only do a vsync if we are past the minimum scanline #.
        // A monitor will refuse to vsync too quickly.
        if (beam_y_ > CGA_MONITOR_VSYNC_MIN) {
            beam_x_ = 0;
            beam_y_ = 0;
            rba_ = 0;
            scanline_ = 0;
            frame_count_++;

            // Toggle blink state. This is toggled every 8 frames by default.
            if ((frame_count_ % CGA_DEFAULT_CURSOR_FRAME_CYCLE) == 0) {
                cursor_blink_ = !cursor_blink_;
            }

            // Swap the display buffers
            //std::cout << "vsync: swapping buffers\n";
            swap();
        }
    }

    void swap() {
        if (back_buf_ == 0) {
            front_buf_ = 0;
            back_buf_ = 1;
        }
        else {
            front_buf_ = 1;
            back_buf_ = 0;
        }
        // Clear new back buffer.
        memset(&buf_[back_buf_], 0, sizeof(buf_[0]));
    }
};
