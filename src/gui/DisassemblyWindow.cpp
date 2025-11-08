#include "DisassemblyWindow.h"
#include <imgui/imgui.h>
#include <cmath>

void DisassemblyWindow::show(bool *open) {
    if (!_machine) {
        ImGui::Begin("Disassembly", open);
        ImGui::Text("No machine instance");
        ImGui::End();
        return;
    }

    ImGui::Begin("Disassembly", open);
    const uint16_t* s_regs = _machine->segmentRegisters();
    if (!s_regs) {
        ImGui::Text("Disassembly unavailable");
        ImGui::End();
        return;
    }

    const uint16_t cs = s_regs[1];
    const uint16_t ip = _machine->getRealIP();
    const uint32_t phys = (static_cast<uint32_t>(cs) << 4) + static_cast<uint32_t>(ip);
    Disassembler disasm;
    disasm.reset();

    ImGui::BeginChild("##disasm_child", ImVec2(0,0), true, ImGuiWindowFlags_HorizontalScrollbar);
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 child_avail = ImGui::GetContentRegionAvail();
    const int max_lines = std::max(1, static_cast<int>(std::floor(child_avail.y / line_h)));
    uint32_t phys_addr = phys & 0xFFFFF; // 20-bit real-mode address
    uint16_t cur_ip = ip;
    for (int line = 0; line < max_lines; ++line) {
        bool first = true;
        const uint32_t startAddr = phys_addr;
        const uint16_t start_ip = cur_ip;
        std::string out;
        for (int b_count = 0; b_count < 16; ++b_count) {
            const uint8_t byte = _machine->peekPhysical(phys_addr);
            out = disasm.disassemble(byte, first);
            first = false;
            phys_addr++;
            phys_addr &= 0xFFFFF;
            cur_ip = static_cast<uint16_t>(cur_ip + 1);
            if (!out.empty()) {
                if (startAddr >= 0xF0000) // ROM base address heuristic
                    ImGui::Text("%04X:%04X (ROM)  %s", cs, start_ip, out.c_str());
                else
                    ImGui::Text("%04X:%04X  %s", cs, start_ip, out.c_str());
                break;
            }
        }
        if (out.empty()) {
            std::string bytes_remaining;
            for (uint32_t addr_iter = startAddr; addr_iter != phys_addr; addr_iter = ((addr_iter + 1) & 0xFFFFF)) {
                const uint8_t b = _machine->peekPhysical(addr_iter);
                char buf[8];
                snprintf(buf, sizeof(buf), "%02X", b);
                if (!bytes_remaining.empty()) bytes_remaining += " ";
                bytes_remaining += buf;
                if (addr_iter == ((phys_addr - 1) & 0xFFFFF)) break;
            }
            ImGui::Text("%04X:%04X  %s", cs, start_ip, bytes_remaining.c_str());
            break;
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
