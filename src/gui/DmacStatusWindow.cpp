#include "DmacStatusWindow.h"

#include <bitset>
#include <imgui/imgui.h>
#include "../core/Machine.h"

void DmacStatusWindow::show(bool* open) {
    // ReSharper disable once CppDFAConstantConditions
    if (!_machine) {
        ImGui::Begin("DMAC Status", open);
        ImGui::Text("No Machine instance available");
        ImGui::End();
        return;
    }

    // ReSharper disable once CppDFAUnreachableCode
    auto* bus = _machine->getBus();
    if (!bus) {
        ImGui::Begin("DMAC Status", open);
        ImGui::Text("No Bus available");
        ImGui::End();
        return;
    }

    const auto* dmac = bus->dmac();
    if (!dmac) {
        ImGui::Begin("DMAC Status", open);
        ImGui::Text("No DMAC present");
        ImGui::End();
        return;
    }

    const auto [channels, status, command, request, mask, ack] = dmac->getDMADebugStatus();

    ImGui::Begin("DMAC Status", open);

    if (ImGui::BeginTable("dmac_regs", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Status");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s", std::bitset<8>(status).to_string().c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Command");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%02X", command);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Request");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s", std::bitset<4>(request).to_string().c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Mask");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s", std::bitset<4>(mask).to_string().c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("DACK");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s", std::bitset<4>(ack).to_string().c_str());
        ImGui::EndTable();
    }

    ImGui::Separator();

    const char* items[] = {"Channel 0", "Channel 1", "Channel 2", "Channel 3"};
    ImGui::Combo("##dmac_channel_combo", &_selectedChannel, items, IM_ARRAYSIZE(items));

    ImGui::Separator();
    auto& ch = channels[_selectedChannel];

    // Channel info table (2 columns)
    if (ImGui::BeginTable("dmac_channel", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Base Address");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("0x%04X", ch.baseAddress);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Base Word Count");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%04X", ch.baseWordCount);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Current Address");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%04X", ch.currentAddress);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Current Word Count");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%04X", ch.currentWordCount);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Mode");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s", std::bitset<8>(ch.mode).to_string().c_str());
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("Mode Breakdown");

    if (ImGui::BeginTable("dmac_channel_mode", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Transfer Type");
        ImGui::TableSetColumnIndex(1);

        switch (ch.mode & 0x0c) {
            case 0:
                ImGui::Text("Verify");
                break;
            case 4:
                ImGui::Text("Write");
                break;
            case 8:
                ImGui::Text("Read");
                break;
            default:
                ImGui::Text("Illegal");
                break;
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Autoinitialize");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s", (ch.mode & 0x10) ? "On" : "Off");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Address Inc/Dec");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s", (ch.mode & 0x20) ? "Decrement" : "Increment");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Operation Mode");
        ImGui::TableSetColumnIndex(1);
        switch (ch.mode & 0xC0) {
            case 0x00:
                ImGui::Text("Demand");
                break;
            case 0x40:
                ImGui::Text("Single");
                break;
            case 0x80:
                ImGui::Text("Block");
                break;
            default:
                ImGui::Text("Cascade");
                break;
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

