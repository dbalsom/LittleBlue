#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <functional>
#include <utility>
#include <algorithm>
#include <sstream>

class Crtc6845
{
public:
    // --- Public types and constants -------------------------------------------------------------

    enum class CursorStatus : uint8_t
    {
        Solid,
        Hidden,
        Blink,
        SlowBlink,
    };

    enum class CrtcRegister : uint8_t
    {
        HorizontalTotal = 0,
        HorizontalDisplayed,
        HorizontalSyncPosition,
        SyncWidth,
        VerticalTotal,
        VerticalTotalAdjust,
        VerticalDisplayed,
        VerticalSync,
        InterlaceMode,
        MaximumScanlineAddress,
        CursorStartLine,
        CursorEndLine,
        StartAddressH,
        StartAddressL,
        CursorAddressH,
        CursorAddressL,
        LightPenPositionH,
        LightPenPositionL,
    };

    struct CrtcStatusBits
    {
        bool hblank = false;
        bool vblank = false;
        bool den = false; // Display Enable within active area
        bool hborder = false;
        bool vborder = false;
        bool cursor = false;
        bool hsync = false; // one-tick pulse when HSYNC occurs
        bool vsync = false; // one-tick pulse when VSYNC occurs
    };

    static constexpr uint8_t CURSOR_LINE_MASK = 0b0001'1111;
    static constexpr uint8_t CURSOR_ATTR_MASK = 0b0110'0000;

    static constexpr uint8_t BLINK_FAST_RATE = 8;
    static constexpr uint8_t BLINK_SLOW_RATE = 16;

    static constexpr uint8_t CRTC_VBLANK_HEIGHT = 16;
    static constexpr size_t CRTC_ROW_MAX = 32;

    static constexpr size_t REGISTER_MAX = 17;
    static constexpr uint8_t REGISTER_UNREADABLE_VALUE = 0xFF;

    using HBlankCallback = std::function<uint8_t(void)>;

public:
    explicit Crtc6845();

    void reset() {
        reg_ = {}; // externally accessible CRTC register file
        reg_select_ = CrtcRegister::HorizontalTotal;

        start_address_ = 0;
        start_address_latch_ = 0;
        lightpen_position_ = 0;

        cursor_data_ = {};
        cursor_address_ = 0;
        cursor_enabled_ = false;
        cursor_start_line_ = 0;
        cursor_end_line_ = 0;
        blink_state_ = false;
        cursor_blink_ct_ = 0;
        has_cursor_blink_rate_ = true;
        cursor_blink_rate_ = BLINK_FAST_RATE;

        hcc_c0_ = 0;
        char_col_ = 0;
        vlc_c9_ = 0;
        vcc_c4_ = 0;
        vsc_c3h_ = 0;
        hsc_c3l_ = 0;
        vtac_c5_ = 0;
        in_vta_ = false;
        vma_ = 0;
        vma_t_ = 0;
        hsync_target_ = 0;
        status_ = {};
        in_last_vblank_line_ = false;
    }

    void write(uint16_t rel_port, uint8_t data);
    [[nodiscard]] uint8_t read(uint16_t rel_port) const;

    // Step one character time. Returns (status_ptr, current_vma).
    std::pair<const CrtcStatusBits*, uint16_t> tick(const HBlankCallback& hblank_cb);

    [[nodiscard]] uint16_t start_address() const { return start_address_latch_; }
    [[nodiscard]] uint16_t address() const { return vma_; }
    [[nodiscard]] uint8_t vlc() const { return vlc_c9_; } // vertical line counter (scanline within char row)
    [[nodiscard]] const CrtcStatusBits& status() const { return status_; }
    [[nodiscard]] bool hblank() const { return status_.hblank; }
    [[nodiscard]] bool vblank() const { return status_.vblank; }
    [[nodiscard]] bool vsync() const { return status_.vsync; }
    [[nodiscard]] bool hsync() const { return status_.hsync; }
    [[nodiscard]] bool den() const { return status_.den; }
    [[nodiscard]] bool border() const { return status_.hborder | status_.vborder; }

    [[nodiscard]] uint16_t cursor_address() const { return cursor_address_; }
    [[nodiscard]] std::pair<uint8_t, uint8_t> cursor_extents() const { return {cursor_start_line_, cursor_end_line_}; }
    [[nodiscard]] bool cursor_immediate() const; // current cursor output (includes blink gating)
    [[nodiscard]] bool cursor_enabled() const { return cursor_enabled_; }

    [[nodiscard]] const std::array<uint8_t, 18>& get_registers() const { return reg_; }

private:
    void select_register(uint8_t idx);
    void write_register(uint8_t byte);
    [[nodiscard]] uint8_t read_register() const;

    void update_start_address();
    void update_cursor_address();
    void update_cursor_data();

    static void trace_regs_() {
    }

    std::array<uint8_t, 18> reg_{}; // externally accessible CRTC register file
    CrtcRegister reg_select_ = CrtcRegister::HorizontalTotal;

    uint16_t start_address_ = 0; // from R12/R13
    uint16_t start_address_latch_ = 0; // latched per frame
    uint16_t lightpen_position_ = 0; // from R16/R17 (read-only)

    std::array<bool, CRTC_ROW_MAX> cursor_data_{}; // per scanline row mask
    uint16_t cursor_address_ = 0; // from R14/R15
    bool cursor_enabled_ = false;
    uint8_t cursor_start_line_ = 0;
    uint8_t cursor_end_line_ = 0;
    bool blink_state_ = false;
    uint8_t cursor_blink_ct_ = 0;
    // None = no blink gating. Some = blink at given rate.
    // Using uint8_t where 0 means "no blink" would be ambiguous; keep separate Optional via pointer-like.
    bool has_cursor_blink_rate_ = true;
    uint8_t cursor_blink_rate_ = BLINK_FAST_RATE;

    // --- CRTC counters ------------------------------------------------------------------------
    uint8_t hcc_c0_ = 0; // Horizontal character counter
    uint8_t char_col_ = 0; // bit column within glyph (host card can depend on this)
    uint8_t vlc_c9_ = 0; // Vertical line counter within character row
    uint8_t vcc_c4_ = 0; // Vertical character row counter
    uint8_t vsc_c3h_ = 0; // Vertical sync counter (counts in vblank)
    uint8_t hsc_c3l_ = 0; // Horizontal sync counter (counts in hblank)
    uint8_t vtac_c5_ = 0; // Vertical total adjust counter
    bool in_vta_ = false;
    bool last_line_ = false;
    bool last_row_ = false;
    uint16_t vma_ = 0; // current video memory address
    uint16_t vma_t_ = 0; // temporary holding VMA' for next row start

    uint8_t hsync_target_ = 0;

    CrtcStatusBits status_{};
    bool in_last_vblank_line_ = false;
};
