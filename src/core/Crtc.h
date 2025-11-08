#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <functional>
#include <utility>
#include <algorithm>
#include <sstream>

class Crtc6845 {
public:
    // --- Public types and constants -------------------------------------------------------------

    enum class CursorStatus : uint8_t {
        Solid,
        Hidden,
        Blink,
        SlowBlink,
    };

    enum class CrtcRegister : uint8_t {
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

    struct CrtcStatusBits {
        bool hblank = false;
        bool vblank = false;
        bool den    = false; // Display Enable within active area
        bool hborder = false;
        bool vborder = false;
        bool cursor  = false;
        bool hsync   = false; // one-tick pulse when HSYNC occurs
        bool vsync   = false; // one-tick pulse when VSYNC occurs
    };

    static constexpr uint8_t CURSOR_LINE_MASK  = 0b0000'1111;
    static constexpr uint8_t CURSOR_ATTR_MASK  = 0b0011'0000;

    static constexpr uint8_t BLINK_FAST_RATE   = 8;
    static constexpr uint8_t BLINK_SLOW_RATE   = 16;

    static constexpr uint8_t CRTC_VBLANK_HEIGHT = 16;
    static constexpr size_t  CRTC_ROW_MAX       = 32;

    static constexpr size_t  REGISTER_MAX  = 17;
    static constexpr uint8_t REGISTER_UNREADABLE_VALUE = 0xFF;

    using HBlankCallback = std::function<uint8_t(void)>;

public:
    // --- Construction --------------------------------------------------------------------------

    explicit Crtc6845();

    // --- I/O interface (relative ports: 0 = address/select, 1 = data) --------------------------

    void     write(uint16_t rel_port, uint8_t data);
    uint8_t  read(uint16_t rel_port) const;

    // --- Core stepping -------------------------------------------------------------------------

    // Step one character time. Returns (status_ptr, current_vma).
    std::pair<const CrtcStatusBits*, uint16_t> tick(HBlankCallback hblank_cb);

    // --- Introspection -------------------------------------------------------------------------

    uint16_t start_address() const { return start_address_latch_; }
    uint16_t address() const { return vma_; }
    uint8_t  vlc() const { return vlc_c9_; } // vertical line counter (scanline within char row)
    const CrtcStatusBits& status() const { return status_; }
    bool hblank() const { return status_.hblank; }
    bool vblank() const { return status_.vblank; }
    bool den()    const { return status_.den; }
    bool border() const { return status_.hborder | status_.vborder; }

    uint16_t cursor_address() const { return cursor_address_; }
    std::pair<uint8_t,uint8_t> cursor_extents() const { return {cursor_start_line_, cursor_end_line_}; }
    bool cursor_immediate() const;  // current cursor output (includes blink gating)
    bool cursor_enabled() const { return cursor_enabled_; }

private:
    // --- Helpers used by the I/O paths ---------------------------------------------------------

    void select_register(uint8_t idx);
    void write_register(uint8_t byte);
    uint8_t read_register() const;

    void update_start_address();
    void update_cursor_address();
    void update_cursor_data();

    // --- Tiny trace helpers --------------------------------------------------------------------
    static void trace_regs_() { return; }

private:
    // --- Register file and derived regs --------------------------------------------------------

    std::array<uint8_t, 18> reg_{}; // externally accessible CRTC register file
    CrtcRegister reg_select_ = CrtcRegister::HorizontalTotal;

    uint16_t start_address_ = 0;         // from R12/R13
    uint16_t start_address_latch_ = 0;   // latched per frame
    uint16_t lightpen_position_ = 0;     // from R16/R17 (read-only)

    std::array<bool, CRTC_ROW_MAX> cursor_data_{}; // per scanline row mask
    uint16_t cursor_address_ = 0;       // from R14/R15
    bool     cursor_enabled_ = false;
    uint8_t  cursor_start_line_ = 0;
    uint8_t  cursor_end_line_   = 0;
    bool     blink_state_ = false;
    uint8_t  cursor_blink_ct_ = 0;
    // None = no blink gating. Some = blink at given rate.
    // Using uint8_t where 0 means "no blink" would be ambiguous; keep separate Optional via pointer-like.
    bool     has_cursor_blink_rate_ = true;
    uint8_t  cursor_blink_rate_ = BLINK_FAST_RATE;

    // --- CRTC counters ------------------------------------------------------------------------

    uint8_t  hcc_c0_ = 0;   // Horizontal character counter
    uint8_t  char_col_ = 0; // bit column within glyph (host card can depend on this)
    uint8_t  vlc_c9_ = 0;   // Vertical line counter within character row
    uint8_t  vcc_c4_ = 0;   // Vertical character row counter
    uint8_t  vsc_c3h_ = 0;  // Vertical sync counter (counts in vblank)
    uint8_t  hsc_c3l_ = 0;  // Horizontal sync counter (counts in hblank)
    uint8_t  vtac_c5_ = 0;  // Vertical total adjust counter
    bool     in_vta_   = false;

    uint16_t vma_   = 0;    // current video memory address
    uint16_t vma_t_ = 0;    // temporary holding VMA' for next row start

    uint8_t  hsync_target_ = 0;

    CrtcStatusBits status_{};
    bool in_last_vblank_line_ = false;
};
