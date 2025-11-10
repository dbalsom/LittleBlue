#include "VideoCardStatusWindow.h"
#include <imgui/imgui.h>
#include "../core/Machine.h"
#include "../core/Cga.h"

void VideoCardStatusWindow::show(bool *open) {

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
    const Crtc6845* crtc = cga->crtc();
    if (crtc) {
        const auto &regs = crtc->get_registers();
        if (ImGui::BeginTable("crtc_regs", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Reg");
            ImGui::TableSetupColumn("Value");
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < regs.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", i);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("0x%02X", regs[i]);
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::Text("Failed to get CRTC instance");
    }

    ImGui::End();
}

