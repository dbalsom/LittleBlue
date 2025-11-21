#include "InstructionHistoryWindow.h"
#include <imgui/imgui.h>
#include <cmath>

void InstructionHistoryWindow::show(bool* open) {
    // ReSharper disable once CppDFAConstantConditions
    if (!_machine) {
        ImGui::Begin("Instruction History", open);
        ImGui::TextUnformatted("No Machine instance");
        ImGui::End();
        return;
    }

    // ReSharper disable once CppDFAUnreachableCode
    auto* cpu = _machine->getCpu();
    if (!_historyEnabledInitialized) {
        _historyEnabled = cpu->isLogInstructions();
        _historyEnabledInitialized = true;
    }

    ImGui::Begin("Instruction History", open);

    // Controls row
    if (ImGui::Checkbox("Enable Instruction History", &_historyEnabled)) {
        cpu->setLogInstructions(_historyEnabled);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &_autoScroll);
    ImGui::Separator();

    ImGui::BeginChild("##instr_hist_child", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 avail = ImGui::GetContentRegionAvail();

    // Estimate visible lines; fetch extra (2x) so user can scroll up some distance without refetching.
    size_t visibleLines = static_cast<size_t>(avail.y / line_h);
    if (visibleLines == 0)
        visibleLines = 1;
    size_t fetchLines = std::min<size_t>(visibleLines * 2 + 10, 1000); // cap to buffer capacity

    std::vector<Cpu<Bus>::InstructionHistoryEntry> entries = cpu->getHistory(fetchLines);

    // Display bottom-up: oldest at top, newest at bottom.
    for (int i = static_cast<int>(entries.size()) - 1; i >= 0; --i) {
        const auto& e = entries[static_cast<size_t>(i)];
        ImGui::Text("%04X:%04X  %s", e.cs, e.ip, e.disassembly.c_str());
    }

    // Auto-scroll to bottom when new entries arrive.
    if (_autoScroll && entries.size() != _lastDisplayedCount) {
        ImGui::SetScrollHereY(1.0f);
        _lastDisplayedCount = entries.size();
    }

    ImGui::EndChild();
    ImGui::End();
}
