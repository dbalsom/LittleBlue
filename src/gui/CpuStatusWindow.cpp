#include "CpuStatusWindow.h"
#include <imgui/imgui.h>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <charconv>

void CpuStatusWindow::show(bool *open) {
    // Ensure ImGui context is available (helps static analyzers and prevents null deref)
    IM_ASSERT(ImGui::GetCurrentContext() != nullptr);

    // Just bail with a tiny error dialog if no machine
    if (!_machine) {
        ImGui::Begin("CPU Status", open);
        ImGui::Text("No machine instance!");
        ImGui::End();
        return;
    }

    ImGui::Begin("CPU Status", open);
    {
        if (const uint16_t* regs = _machine->registers()) {
            const auto ip = _machine->getRealIP();
            const uint64_t cycles_now = _machine->cycleCount();

            // Do CPU control buttons
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
            // The 'Cycle' button advances a single CPU cycle (3 crystal ticks)
            // TODO: get the divisor at runtime
            if (ImGui::Button("Cycle")) {
                _machine->run_for(3);
            }
            ImGui::SameLine();
            // The 'Step' button advances to the next instruction boundary
            if (ImGui::Button("Step")) {
                lastStepCycles = _machine->stepInstruction();
                lastStepTime = ImGui::GetTime();
            }
            ImGui::SameLine();
            // The 'Reset' button resets the CPU and begins execution from the reset vector
            if (ImGui::Button("Reset")) { _machine->reset(); }
            ImGui::SameLine();
            // Show the current Machine state
            const auto machine_state = _machine->getState();
            ImGui::Text("State: %s", _machine->getStateString().c_str());


            ImGui::Separator();
            ImGui::Text("Cycles: %llu", static_cast<unsigned long long>(cycles_now));

            // Show last step feedback until we run the next step.
            if ((machine_state == MachineState::Stopped) && lastStepTime != 0.0) {
                ImGui::SameLine();
                ImGui::Text("Last step: %llu cycle(s)", static_cast<unsigned long long>(lastStepCycles));
            }
            //ImGui::Text("Effective: %.3f MHz", app->smoothedMhz);
            ImGui::Separator();

            ImGui::Text("Registers:");
            ImGui::Columns(4, nullptr, false);
            ImGui::Text("AX: %04X", regs[reg_to_idx(Register::AX)]); ImGui::NextColumn();
            ImGui::Text("BX: %04X", regs[reg_to_idx(Register::BX)]); ImGui::NextColumn();
            ImGui::Text("CX: %04X", regs[reg_to_idx(Register::CX)]); ImGui::NextColumn();
            ImGui::Text("DX: %04X", regs[reg_to_idx(Register::DX)]); ImGui::NextColumn();

            ImGui::Text("SP: %04X", regs[reg_to_idx(Register::SP)]); ImGui::NextColumn();
            ImGui::Text("BP: %04X", regs[reg_to_idx(Register::BP)]); ImGui::NextColumn();
            ImGui::Text("SI: %04X", regs[reg_to_idx(Register::SI)]); ImGui::NextColumn();
            ImGui::Text("DI: %04X", regs[reg_to_idx(Register::DI)]); ImGui::NextColumn();
            ImGui::Columns(1);

            ImGui::Separator();
            ImGui::Columns(4, nullptr, false);
            ImGui::Text("CS: %04X", regs[reg_to_idx(Register::CS)]); ImGui::NextColumn();
            ImGui::Text("DS: %04X", regs[reg_to_idx(Register::DS)]); ImGui::NextColumn();
            ImGui::Text("SS: %04X", regs[reg_to_idx(Register::SS)]); ImGui::NextColumn();
            ImGui::Text("ES: %04X", regs[reg_to_idx(Register::ES)]); ImGui::NextColumn();

            ImGui::Separator();
            ImGui::Columns(4, nullptr, false);
            ImGui::Text("PC: %04X", regs[reg_to_idx(Register::PC)]); ImGui::NextColumn();
            ImGui::Text("IP: %04X", ip); //ImGui::NextColumn();

            ImGui::Separator();
            ImGui::Columns(1, nullptr, false);
            ImGui::Text("Internal Registers:");
            ImGui::Columns(4, nullptr, false);
            ImGui::Text("TMPA: %04X", regs[reg_to_idx(Register::TMPA)]); ImGui::NextColumn();
            ImGui::Text("TMPB: %04X", regs[reg_to_idx(Register::TMPB)]); ImGui::NextColumn();
            ImGui::Text("IND : %04X", regs[reg_to_idx(Register::IND)]); ImGui::NextColumn();
            ImGui::Text("OPR : %04X", regs[reg_to_idx(Register::OPR)]); ImGui::NextColumn();

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

            // FLAGS display: show hex value and a styled 16-bit flag breakdown
            const uint16_t flags = regs[reg_to_idx(Register::FLAGS)];

            // Prepare a stable bool array for the ImGui checkboxes to avoid analyzer warnings
            bool led_vals[16];
            for (int i = 0; i < 16; ++i) led_vals[i] = ((flags >> i) & 1) != 0;

            // Build a list of visible (non-reserved) bits in order 15..0
            std::vector<int> visibleBits;
            visibleBits.reserve(16);
            for (int b = 15; b >= 0; --b) {
                // Omit reserved entries and the bit-1 "1" pseudo-flag
                if (std::strcmp(flag_names[b], "RES") != 0 && std::strcmp(flag_names[b], "1") != 0) visibleBits.push_back(b);
            }

            ImGui::Columns(1);
            ImGui::Text("FLAGS: %04X", static_cast<unsigned>(flags)); ImGui::NextColumn();

            if (!visibleBits.empty()) {
                const int cols = static_cast<int>(visibleBits.size());
                ImDrawList* draw_list = ImGui::GetWindowDrawList();

                // Use an ImGui table so columns size/flow is handled by the layout system
                if (ImGui::BeginTable("flags_table", cols, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoHostExtendX)) {
                    // First row: labels (centered)
                    ImGui::TableNextRow();
                    for (int i = 0; i < cols; ++i) {
                        ImGui::TableNextColumn();
                        const int b = visibleBits[i];
                        ImGui::PushID(b);
                        const ImVec2 txt_size = ImGui::CalcTextSize(flag_names[b]);
                        const float avail = ImGui::GetContentRegionAvail().x;
                        const float curX = ImGui::GetCursorPosX();
                        ImGui::SetCursorPosX(curX + std::max(0.0f, (avail - txt_size.x) * 0.5f));
                        ImGui::TextUnformatted(flag_names[b]);
                        ImGui::PopID();
                    }

                    // Second row: squares (centered under labels)
                    ImGui::TableNextRow();
                    for (int i = 0; i < cols; ++i) {
                        constexpr float square_size = 18.0f;
                        ImGui::TableNextColumn();
                        const int b = visibleBits[i];
                        ImGui::PushID(b);
                        const float avail = ImGui::GetContentRegionAvail().x;
                        const float curX = ImGui::GetCursorPosX();
                        ImGui::SetCursorPosX(curX + std::max(0.0f, (avail - square_size) * 0.5f));
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        ImGui::Dummy(ImVec2(square_size, square_size));
                        // Slightly desaturated blue
                        constexpr auto chosen = ImVec4(0.0f, 0.55f, 0.92f, 1.0f);
                        ImU32 onColor = ImGui::GetColorU32(chosen);
                        ImU32 offColor = ImGui::GetColorU32(ImGuiCol_FrameBg);
                        const ImU32 color = led_vals[b] ? onColor : offColor;
                        draw_list->AddRectFilled(
                            p,
                            ImVec2(p.x + square_size, p.y + square_size),
                            color,
                            2.0f);
                        draw_list->AddRect(
                            p,
                            ImVec2(p.x + square_size, p.y + square_size),
                            ImGui::GetColorU32(ImVec4(0,0,0,0.6f)),
                            2.0f,
                            0,
                            1.0f);
                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }
            }

            ImGui::Separator();
            // Breakpoint controls (code breakpoint in form XXXX:XXXX)
            static char bp_input[16] = "";
            // Persisted parse-error marker set when Set Breakpoint fails
            static bool bp_parse_error = false;

            // Helper: parse a CS:IP string (hex) from a C string buffer. Returns true on success and writes
            // parsed values into out_cs/out_ip.
            auto parse_cs_ip = [](const char* buf, uint16_t &out_cs, uint16_t &out_ip) -> bool {
                if (buf == nullptr) {
                    return false;
                }
                std::string_view sv(buf);
                const auto pos = sv.find(':');
                if (pos == std::string_view::npos) {
                    return false;
                }
                const auto cs_sv = sv.substr(0, pos);
                const auto ip_sv = sv.substr(pos + 1);
                if (cs_sv.empty() || ip_sv.empty()) {
                    return false;
                }
                // parse hex using from_chars
                unsigned long long cs_val = 0, ip_val = 0;
                if (auto [ptr, ec] =
                    std::from_chars(cs_sv.data(), cs_sv.data() + cs_sv.size(), cs_val, 16);
                    ec != std::errc() || ptr != cs_sv.data() + cs_sv.size()) {
                    return false;
                }
                if (auto [ptr2, ec2] =
                    std::from_chars(ip_sv.data(), ip_sv.data() + ip_sv.size(), ip_val, 16);
                    ec2 != std::errc() || ptr2 != ip_sv.data() + ip_sv.size()) {
                    return false;
                }
                if (cs_val > 0xFFFFULL || ip_val > 0xFFFFULL) {
                    return false;
                }
                out_cs = static_cast<uint16_t>(cs_val);
                out_ip = static_cast<uint16_t>(ip_val);
                return true;
            };

            ImGui::Text("Code Breakpoint (CS:IP):");
            ImGui::SameLine();
            ImGui::PushItemWidth(200);
            bool bp_valid = false;
            uint16_t parsed_cs = 0, parsed_ip = 0;
            if (bp_input[0] != '\0') {
                bp_valid = parse_cs_ip(bp_input, parsed_cs, parsed_ip);
            } else {
                // empty input considered neutral (not invalid)
                bp_valid = true;
            }

            // If live validation passes, clear any previous parse-error state; otherwise preserve it
            if (bp_valid) bp_parse_error = false;

            const bool show_invalid = (!bp_valid) || bp_parse_error;
            if (show_invalid) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            }
            ImGui::InputText("##bpaddr", bp_input, sizeof(bp_input));
            if (show_invalid) ImGui::PopStyleColor();
            ImGui::PopItemWidth();

            // Disable the Set button when input is invalid
            const bool set_enabled = bp_valid;
            ImGui::BeginDisabled(!set_enabled);
            if (ImGui::Button("Set Breakpoint")) {
                // If live-parse already succeeded, use parsed values; otherwise attempt to parse now.
                bool ok = false;
                uint16_t cs_v = 0, ip_v = 0;
                if (bp_valid) {
                    cs_v = parsed_cs; ip_v = parsed_ip; ok = true;
                } else if (bp_input[0] != '\0') {
                    ok = parse_cs_ip(bp_input, cs_v, ip_v);
                }
                if (ok) {
                    _machine->setBreakpoint(cs_v, ip_v);
                    bp_parse_error = false;
                } else {
                    bp_parse_error = true;
                }
            }
            // Show tooltip when disabled to explain expected format
            if (!set_enabled && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Invalid format, expected: FFFF:FFFF (hex)");
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Clear Breakpoint")) {
                _machine->clearBreakpoint();
            }
            // Show current breakpoint status (from Machine/Cpu breakpoint storage)
            if (_machine->hasBreakpoint()) {
                const uint16_t bcs = _machine->breakpointCS();
                const uint16_t bip = _machine->breakpointIP();
                ImGui::SameLine();
                ImGui::Text("Breakpoint set: %04X:%04X", static_cast<unsigned>(bcs), static_cast<unsigned>(bip));
            } else {
                ImGui::SameLine();
                ImGui::Text("No breakpoint set");
            }
            // If a breakpoint was hit, show a message and pause CPU
            if (_machine->breakpointHit()) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1,0,0,1), "Breakpoint hit!");
            }
            ImGui::Separator();
        }
        else {
            ImGui::Text("CPU registers unavailable");
        }
    }
    ImGui::End();
}
