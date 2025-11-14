#include "VideoCardStatusWindow.h"

#include <bitset>
#include <imgui/imgui.h>
#include "../core/Machine.h"
#include "../core/Cga.h"

void VideoCardStatusWindow::show(bool* open) {

    // ReSharper disable once CppDFAConstantConditions
    if (!_machine) {
        ImGui::Begin("Video Card Status", open);
        ImGui::Text("No Machine instance");
        ImGui::End();
        return;
    }

    // ReSharper disable once CppDFAUnreachableCode
    auto* cga = _machine->getBus()->cga();
    if (!cga) {
        ImGui::Begin("Video Card Status", open);
        ImGui::Text("CGA not present");
        ImGui::End();
        return;
    }

    // The window should present a simple table of CRTC registers (0..16)
    ImGui::Begin("Video Card Status", open);

    auto cga_state = cga->getDebugState();

    ImGui::Text("CGA Registers");
    ImGui::Separator();
    ImGui::Columns(2, nullptr, false);
    ImGui::Text("Mode Register:");
    ImGui::NextColumn();
    ImGui::Text("%s", std::bitset<8>(cga_state.mode_byte).to_string().c_str());
    if (ImGui::BeginTable("cga_mode_flags", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Flag");
        ImGui::TableSetupColumn("V");
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Hi-res Text");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(cga_state.mode_hires_text ? "1" : "0");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Graphics Mode");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(cga_state.mode_graphics ? "1" : "0");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("B/W Palette");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(cga_state.mode_bw ? "1" : "0");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Display Enable");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(cga_state.mode_enable ? "1" : "0");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Hi-res Graphics");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(cga_state.mode_hires_gfx ? "1" : "0");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Blinking Enabled");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(cga_state.mode_blinking ? "1" : "0");

        ImGui::EndTable();
    }
    ImGui::NextColumn();

    // Show individual decoded mode bits in a table
    ImGui::Separator();
    ImGui::Text("Mode Flags:");
    ImGui::Separator();


    ImGui::Text("Clock Divisor:");
    ImGui::NextColumn();
    ImGui::Text("%d", cga_state.clock_divisor);
    ImGui::NextColumn();
    ImGui::Separator();
    ImGui::Columns(1, nullptr, false);
    ImGui::Text("CRTC Registers");
    ImGui::Separator();
    if (const Crtc6845* crtc = cga->crtc()) {
        const auto& regs = crtc->get_registers();
        ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg;
        if (ImGui::BeginTable("crtc_regs", 3, flags)) {
            ImGui::TableSetupColumn("Reg");
            ImGui::TableSetupColumn("Hex");
            ImGui::TableSetupColumn("Dec");
            ImGui::TableHeadersRow();
            for (int i = 0; i < regs.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%02X %s", i, cga->getRegisterName(i).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%02X", regs[i]);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", regs[i]);
            }
            ImGui::EndTable();
        }
    }
    else {
        ImGui::Text("Failed to get CRTC instance");
    }

    ImGui::End();
}
