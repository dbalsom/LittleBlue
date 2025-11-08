#include "CpuStatusWindow.h"
#include <imgui/imgui.h>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

void CpuStatusWindow::show(bool *open) {
    // Ensure ImGui context is available (helps static analyzers and prevents null deref)
    IM_ASSERT(ImGui::GetCurrentContext() != nullptr);

    if (!_machine) {
        ImGui::Begin("Disassembly", open);
        ImGui::Text("No machine instance");
        ImGui::End();
        return;
    }

    ImGui::Begin("CPU Status", open);
    {
        uint16_t* regs = _machine->registers();
        uint16_t* s_regs = _machine->segmentRegisters();
        if (regs && s_regs) {

            uint64_t cycles_now = _machine->cycleCount();

            // CPU control buttons (integrated into CPU Status window)
            static double lastStepTime = 0.0;
            static uint64_t lastStepCycles = 0;
            if (_machine->isRunning()) {
                if (ImGui::Button("Stop")) {
                    _machine->stop();
                }
            } else {
                if (ImGui::Button("Run")) {
                    _machine->run();
                }
            }
            ImGui::SameLine();
            // 'Cycle' advances a single CPU cycle (3 crystal ticks)
            if (ImGui::Button("Cycle")) {
                _machine->run_for(3);
            }
            ImGui::SameLine();
            // 'Step' advances to the next instruction boundary (runs cycles until _rni is true)
            if (ImGui::Button("Step")) {
                lastStepCycles = _machine->stepInstruction();
                lastStepTime = ImGui::GetTime();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) { _machine->reset(); }
            ImGui::SameLine();
            ImGui::Text("State: %s", _machine->getStateString().c_str());
            // Show last step feedback for 2 seconds
            if (lastStepTime != 0.0 && (ImGui::GetTime() - lastStepTime) < 2.0) {
                ImGui::SameLine();
                ImGui::Text("(Last step: %llu cycles)", static_cast<unsigned long long>(lastStepCycles));
            }

            ImGui::Separator();
            ImGui::Text("Cycles: %llu", static_cast<unsigned long long>(cycles_now));
            //ImGui::SameLine();
            //ImGui::Text("Effective: %.3f MHz", app->smoothedMhz);
            ImGui::Separator();

            ImGui::Text("Registers:");
            ImGui::Columns(4, nullptr, false);
            ImGui::Text("AX: %04X", (unsigned)regs[0]); ImGui::NextColumn();
            ImGui::Text("BX: %04X", (unsigned)regs[3]); ImGui::NextColumn();
            ImGui::Text("CX: %04X", (unsigned)regs[1]); ImGui::NextColumn();
            ImGui::Text("DX: %04X", (unsigned)regs[2]); ImGui::NextColumn();

            ImGui::Text("SP: %04X", (unsigned)regs[4]); ImGui::NextColumn();
            ImGui::Text("BP: %04X", (unsigned)regs[5]); ImGui::NextColumn();
            ImGui::Text("SI: %04X", (unsigned)regs[6]); ImGui::NextColumn();
            ImGui::Text("DI: %04X", (unsigned)regs[7]); ImGui::NextColumn();
            ImGui::Columns(1);

            ImGui::Separator();
            ImGui::Columns(4, nullptr, false);
            ImGui::Text("CS: %04X", static_cast<unsigned>(s_regs[1])); ImGui::NextColumn();
            ImGui::Text("DS: %04X", static_cast<unsigned>(s_regs[3])); ImGui::NextColumn();
            ImGui::Text("SS: %04X", static_cast<unsigned>(s_regs[2])); ImGui::NextColumn();
            ImGui::Text("ES: %04X", static_cast<unsigned>(s_regs[0])); ImGui::NextColumn();

            ImGui::Columns(1);
            ImGui::Separator();
            ImGui::Columns(4, nullptr, false);
            ImGui::Text("IP: %04X", (unsigned)s_regs[4]); ImGui::NextColumn();

            // FLAGS display: show hex value and a styled 16-bit flag breakdown
            uint16_t flags = s_regs[15];
            ImGui::Text("FLAGS: %04X", static_cast<unsigned>(flags)); ImGui::NextColumn();
            ImGui::Columns(1);

            ImGui::Separator();

            // Flag names for 8088/8086 (bits 0..15). Bits not defined on 8088 are labeled RES (reserved).
            static constexpr const char* flag_names[16] = {
                "CF",    // 0
                "1",     // 1 (reserved/always 1 on some implementations)
                "PF",    // 2
                "RES",   // 3
                "AF",    // 4
                "RES",   // 5
                "ZF",    // 6
                "SF",    // 7
                "TF",    // 8
                "IF",    // 9
                "DF",    // 10
                "OF",    // 11
                "RES",   // 12
                "RES",   // 13
                "RES",   // 14
                "RES"    // 15
            };

            // Prepare a stable bool array for the ImGui checkboxes to avoid analyzer warnings
            bool ledvals[16];
            for (int i = 0; i < 16; ++i) ledvals[i] = ((flags >> i) & 1) != 0;

            // Build a list of visible (non-reserved) bits in order 15..0
            std::vector<int> visibleBits;
            visibleBits.reserve(16);
            for (int b = 15; b >= 0; --b) {
                // Omit reserved entries and the bit-1 "1" pseudo-flag
                if (std::strcmp(flag_names[b], "RES") != 0 && std::strcmp(flag_names[b], "1") != 0) visibleBits.push_back(b);
            }

            ImGui::Text("Flags:");

            if (!visibleBits.empty()) {
                const int cols = static_cast<int>(visibleBits.size());
                const float squareSize = 18.0f;
                ImDrawList* draw_list = ImGui::GetWindowDrawList();

                // Use an ImGui table so columns size/flow is handled by the layout system
                if (ImGui::BeginTable("flags_table", cols, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoHostExtendX)) {
                    // First row: labels (centered)
                    ImGui::TableNextRow();
                    for (int i = 0; i < cols; ++i) {
                        ImGui::TableNextColumn();
                        int b = visibleBits[i];
                        ImGui::PushID(b);
                        ImVec2 txtSz = ImGui::CalcTextSize(flag_names[b]);
                        float avail = ImGui::GetContentRegionAvail().x;
                        float curX = ImGui::GetCursorPosX();
                        ImGui::SetCursorPosX(curX + std::max(0.0f, (avail - txtSz.x) * 0.5f));
                        ImGui::TextUnformatted(flag_names[b]);
                        ImGui::PopID();
                    }

                    // Second row: squares (centered under labels)
                    ImGui::TableNextRow();
                    for (int i = 0; i < cols; ++i) {
                        ImGui::TableNextColumn();
                        int b = visibleBits[i];
                        ImGui::PushID(b);
                        float avail = ImGui::GetContentRegionAvail().x;
                        float curX = ImGui::GetCursorPosX();
                        ImGui::SetCursorPosX(curX + std::max(0.0f, (avail - squareSize) * 0.5f));
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        ImGui::Dummy(ImVec2(squareSize, squareSize));
                        // Use a fixed slightly desaturated blue for ON state so we don't
                        // do dynamic theme probing at render time.
                        const ImVec4 chosen = ImVec4(0.0f, 0.55f, 0.92f, 1.0f);
                        ImU32 onColor = ImGui::GetColorU32(chosen);
                        ImU32 offColor = ImGui::GetColorU32(ImGuiCol_FrameBg);
                        ImU32 color = ledvals[b] ? onColor : offColor;
                        draw_list->AddRectFilled(p, ImVec2(p.x + squareSize, p.y + squareSize), color, 2.0f);
                        draw_list->AddRect(p, ImVec2(p.x + squareSize, p.y + squareSize), ImGui::GetColorU32(ImVec4(0,0,0,0.6f)), 2.0f, 0, 1.0f);
                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }
            }

            ImGui::Separator();
            // Breakpoint controls (code breakpoint in form XXXX:XXXX)
            static char bp_input[16] = "";
            ImGui::Text("Code Breakpoint (CS:IP):");
            ImGui::SameLine();
            ImGui::PushItemWidth(200);
            ImGui::InputText("##bpaddr", bp_input, sizeof(bp_input));
            ImGui::PopItemWidth();

            if (ImGui::Button("Set Breakpoint")) {
                // Parse format XXXX:XXXX (hex)
                std::string s(bp_input);
                auto pos = s.find(':');
                if (pos != std::string::npos) {
                    std::string cs_str = s.substr(0, pos);
                    std::string ip_str = s.substr(pos + 1);
                    try {
                        auto cs_val = std::stoul(cs_str, nullptr, 16);
                        auto ip_val = std::stoul(ip_str, nullptr, 16);
                        auto cs = static_cast<uint16_t>(cs_val);
                        auto ip = static_cast<uint16_t>(ip_val);
                        _machine->setBreakpoint(cs, ip);
                    } catch (...) {
                        // ignore parse errors for now
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Breakpoint")) {
                _machine->clearBreakpoint();
            }
            // Show current breakpoint status (from Machine/Cpu breakpoint storage)
            if (_machine->hasBreakpoint()) {
                uint16_t bcs = _machine->breakpointCS();
                uint16_t bip = _machine->breakpointIP();
                ImGui::SameLine();
                ImGui::Text("Breakpoint set: %04X:%04X", (unsigned)bcs, (unsigned)bip);
            } else {
                ImGui::SameLine();
                ImGui::Text("No breakpoint set");
            }
            // If a breakpoint was hit, show a message and pause CPU
            if (_machine->breakpointHit()) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1,0,0,1), "Breakpoint hit!");
                // leave the breakpoint hit state until user clears it
            }
            ImGui::Separator();
        }
        else {
            ImGui::Text("CPU registers unavailable");
        }
    }
    ImGui::End();
}
