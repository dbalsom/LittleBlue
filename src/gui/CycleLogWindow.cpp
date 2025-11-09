#include "CycleLogWindow.h"
#include "../core/Machine.h"

void CycleLogWindow::show(bool *open) {
    IM_ASSERT(ImGui::GetCurrentContext() != nullptr);
    if (!_machine) {
        ImGui::Begin("Cycle Log", open);
        ImGui::Text("No Machine instance");
        ImGui::End();
        return;
    }

    ImGui::Begin("Cycle Log", open);

    // Controls: enable logging checkbox, clear, capacity
    bool logging = _machine->isCycleLogging();
    if (ImGui::Checkbox("Enable Logging", &logging)) {
        _machine->setCycleLogging(logging);
        if (logging) {
            _machine->clearCycleLog();
            _machine->appendCycleLogLine("--- Cycle logging enabled ---");
            _lastSeenSize = _machine->getCycleLogSize();
            SDL_Log("Cycle logging enabled");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        _machine->clearCycleLog();
        _lastSeenSize = 0;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &_autoScroll);

    ImGui::SameLine();
    ImGui::Text("Capacity:"); ImGui::SameLine();
    if (ImGui::InputInt("##capacity", &_capacityUI, 0, 1000)) {
        if (_capacityUI < 0) _capacityUI = 0;
        _machine->setCycleLogCapacity(static_cast<size_t>(_capacityUI));
    }

    ImGui::Separator();
    ImGui::Text("Lines: %llu", static_cast<unsigned long long>(_machine->getCycleLogSize()));
    const auto &bufPreview = _machine->getCycleLogBuffer();
    if (!bufPreview.empty()) {
        ImGui::TextWrapped("Last: %s", bufPreview.back().c_str());
    }
    ImGui::Separator();

    // Log contents
    ImGui::BeginChild("##cyclelog_child", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    const auto &buf = _machine->getCycleLogBuffer();
    for (const auto &line : buf) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (_autoScroll && buf.size() != _lastSeenSize) {
        ImGui::SetScrollHereY(1.0f);
        _lastSeenSize = buf.size();
    }
    ImGui::EndChild();

    ImGui::End();
}

