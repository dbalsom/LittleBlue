#include "StackViewerWindow.h"
#include "../core/Machine.h"
#include "../core/Cpu.h"
#include <cmath>

void StackViewerWindow::show(bool *open) {
    IM_ASSERT(ImGui::GetCurrentContext() != nullptr);

    // Compute a typical line height and set a default window size to show ~16 rows on first use
    float line_h = ImGui::GetTextLineHeightWithSpacing();
    ImGui::SetNextWindowSize(ImVec2(420.0f, line_h * 16.0f + 60.0f), ImGuiCond_FirstUseEver);

    // ReSharper disable once CppDFAConstantConditions
    if (!_machine) {
        ImGui::Begin("Stack Viewer", open);
        ImGui::Text("No Machine instance");
        ImGui::End();
        return;
    }

    // Static analysis is failing here for some reason, override so the entire function isn't dimmed.
    // ReSharper disable once CppDFAUnreachableCode
    ImGui::Begin("Stack Viewer", open);

    const uint16_t* regs = _machine->registers();
    if (!regs) {
        ImGui::Text("CPU registers unavailable");
        ImGui::End();
        return;
    }

    // Compute linear address from SS:SP (20-bit addressing)
    const uint16_t ss = regs[reg_to_idx(Register::SS)];
    const uint16_t sp = regs[reg_to_idx(Register::SP)];
    const uint32_t base = (static_cast<uint32_t>(ss) << 4) & 0xFFFFF;
    const uint32_t linear_sp = (base + static_cast<uint32_t>(sp)) & 0xFFFFF; // 20-bit address for SP

    ImGui::Text("SS: %04X SP: %04X [%05X]", static_cast<unsigned>(ss), static_cast<unsigned>(sp), static_cast<unsigned>(linear_sp));
    ImGui::Separator();

    // Stack segment offsets go from 0x0000 .. 0xFFFE. The stack grows downward, so addresses above SP
    // (larger offsets) are older stack entries. Compute how many 16-bit words exist from SP up to 0x10000.
    constexpr uint32_t max_offset = 0x10000;
    const uint32_t words_above_sp = ((max_offset - sp) / 2);

    // UI: provide a scrollable area that represents the entire stack region above SP.
    // Auto-follow behaviour: start in follow mode by default (_autoFollow==true). If the user scrolls up,
    // detect that and disable auto-follow until they scroll to the bottom again.
    ImGui::BeginChild("##stack_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    // Detect current scroll state before rendering.
    const float currentScrollY = ImGui::GetScrollY();
    const float currentScrollMax = ImGui::GetScrollMaxY();
    const float eps = line_h * 0.5f;
    const bool is_at_bottom_now = ((currentScrollMax - currentScrollY) < eps);
    // persisted _wasAtBottom remembers if we were at bottom from previous frames

    const float deltaY = currentScrollY - _lastScrollY;
    const ImGuiIO &io = ImGui::GetIO();
    const bool wheel_used = (io.MouseWheel != 0.0f);
    const bool dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Right);

    // Consider the user to have moved the scroll if they used the wheel, dragged the scrollbar, or there
    // was a meaningful change in scrollY since last frame.
    const bool userMoved = wheel_used || dragging || (std::abs(deltaY) > 0.01f);

    if (userMoved) {
        // reset cooldown
        _interactionCooldownFrames = 6;
        // If the user moved the view, and they are not at bottom, treat this as manual inspection and lock follow.
        if (!is_at_bottom_now) {
            _userScrolledUp = true;
        } else {
            // If user moved and reached bottom, clear manual lock so follow resumes.
            _userScrolledUp = false;
        }
    } else {
        // decrement cooldown if set
        if (_interactionCooldownFrames > 0) _interactionCooldownFrames -= 1;
        // No user move this frame: if we're at bottom, ensure follow is enabled; otherwise leave manual state alone.
        if (is_at_bottom_now) _userScrolledUp = false;
    }

    // Auto-follow enabled when not in manual inspection mode and cooldown expired
    _autoFollow = (!_userScrolledUp) && (_interactionCooldownFrames == 0);

    // Use ImGuiListClipper to render only visible items. Clipper expects the number of rows.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(words_above_sp), line_h);

    // We'll render top-to-bottom where index 0 is the top-of-stack (highest offset), and the last index is SP.
    while (clipper.Step()) {
        for (int display_i = clipper.DisplayStart; display_i < clipper.DisplayEnd; ++display_i) {
            uint32_t offset_from_sp_words = words_above_sp - 1 - display_i;
            int64_t offset = static_cast<int64_t>(sp) + static_cast<int64_t>(offset_from_sp_words) * 2;
            if (offset < 0 || offset > static_cast<int64_t>(max_offset)) {
                ImGui::Text(
                    "%04X:%04X [%05X]: ----",
                    static_cast<unsigned>(ss),
                    static_cast<unsigned>(sp),
                    static_cast<unsigned>(offset & 0xFFFFF));
                continue;
            }
            uint32_t addr = (base + static_cast<uint32_t>(offset)) & 0xFFFFF;

            uint8_t lo = _machine->peekPhysical(addr);
            uint8_t hi = _machine->peekPhysical((addr + 1U) & 0xFFFFF);
            uint16_t val = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));

            if (display_i == static_cast<int>(words_above_sp - 1)) {
                ImGui::TextColored(
                    ImVec4(0.8f, 0.9f, 1.0f, 1.0f),
                    "%04X:%04X [%05X]: %04X <- SP",
                    static_cast<unsigned>(ss),
                    static_cast<unsigned>(sp),
                    static_cast<unsigned>(addr),
                    val);
            } else {
                ImGui::Text(
                    "%04X:%04X [%05X]: %04X",
                    static_cast<unsigned>(addr),
                    val);
            }
        }
    }

    clipper.End();

    // Only auto-follow if the auto-follow flag is enabled and the user isn't actively interacting.
    if (_autoFollow && !userMoved) {
        ImGui::SetScrollHereY(1.0f);
    }

    // Save scroll state for next frame to detect manual scrolls
    _lastScrollY = ImGui::GetScrollY();
    _lastScrollMax = ImGui::GetScrollMaxY();
    // Update persisted bottom state
    _wasAtBottom = is_at_bottom_now;

    ImGui::EndChild();

    ImGui::End();
}
