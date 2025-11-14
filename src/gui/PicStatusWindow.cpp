#include "PicStatusWindow.h"
#include <imgui/imgui.h>
#include "../core/Machine.h"
#include "../core/Pic.h"

static void draw_led_table_row(const char* label, uint8_t value) {
    // First column: label
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);

    // Colors for on/off states
    const ImVec4 onColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive); // bright themed color
    const ImVec4 offColor = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);

    // Draw 8 LED columns for bits 7..0
    for (int col = 1; col <= 8; ++col) {
        ImGui::TableSetColumnIndex(col);
        int bit = 8 - col; // col=1 -> bit=7, col=8 -> bit=0
        bool on = ((value >> bit) & 1) != 0;

        // Center a small square button in the cell
        const float btnSize = 18.0f;
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float pad = (avail.x - btnSize) * 0.5f;
        if (pad > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);

        ImGui::PushID((label[0] << 8) ^ (col));
        ImGui::PushStyleColor(ImGuiCol_Button, on ? onColor : offColor);
        ImGui::Button("##led", ImVec2(btnSize, btnSize));
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

void PicStatusWindow::show(bool* open) {
    // ReSharper disable once CppDFAConstantConditions
    if (!_machine) {
        ImGui::Begin("PIC Status", open);
        ImGui::Text("No Machine instance");
        ImGui::End();
        return;
    }

    // ReSharper disable once CppDFAUnreachableCode
    auto* bus = _machine->getBus();
    if (!bus) {
        ImGui::Begin("PIC Status", open);
        ImGui::Text("No Bus available");
        ImGui::End();
        return;
    }

    auto* pic = bus->pic();
    if (!pic) {
        ImGui::Begin("PIC Status", open);
        ImGui::Text("PIC not present");
        ImGui::End();
        return;
    }

    ImGui::Begin("PIC Status", open);
    PicDebugState s = pic->getDebugState();

    // Create a table: first column for row labels, then 8 columns for bits 7..0
    if (ImGui::BeginTable("pic_table", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        // Setup columns: label + 8 bit columns
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        for (int i = 7; i >= 0; --i) {
            // Use the digit as the column name so TableHeadersRow would show them, but we'll draw our own header
            ImGui::TableSetupColumn(std::to_string(i).c_str());
        }

        // Header row: center 7..0 above LED columns
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        // empty label cell
        ImGui::TableNextColumn();
        for (int i = 7; i >= 0; --i) {
            ImGui::TableSetColumnIndex(8 - i);
            // Center the header label
            ImVec2 txtSize = ImGui::CalcTextSize(std::to_string(i).c_str());
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float pad = (avail.x - txtSize.x) * 0.5f;
            if (pad > 0.0f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
            ImGui::TextUnformatted(std::to_string(i).c_str());
        }

        // Rows: ISR, IMR, IRR, IRQ Lines
        ImGui::TableNextRow();
        draw_led_table_row("ISR", s.isr);
        ImGui::TableNextRow();
        draw_led_table_row("IMR", s.imr);
        ImGui::TableNextRow();
        draw_led_table_row("IRR", s.irr);
        ImGui::TableNextRow();
        draw_led_table_row("IRQ Lines", s.lines);

        ImGui::EndTable();
    }

    ImGui::End();
}
