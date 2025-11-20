#pragma once

#include <cstdint>
#include <string>
#include <format>

// Simple 8086 disassembler.
class Disassembler
{
public:
    constexpr static size_t MAX_INSTRUCTION_BYTES = 15;

    Disassembler() { reset(); }
    void reset() { byte_count_ = 0; }

    // Feed one byte to the disassembler.
    // Set the firstByte flag to true for the first byte of an instruction.
    bool disassemble(const uint8_t byte, const bool firstByte, std::string& disassembly) {
        auto hex = [](int value, int width, const bool showPrefix = false) -> std::string
        {
            return std::format("{}{:0{}X}", showPrefix ? "0x" : "", value, width);
        };

        std::string bytes;
        if (firstByte) {
            if (byte_count_ != 0) {
                bytes = "!a";
            }
            byte_count_ = 0;
        }
        code_[byte_count_] = byte;
        ++byte_count_;

        last_offset_ = 0;
        std::string instruction = disassembleInstruction();
        if (last_offset_ >= byte_count_) {
            return false; // We don't have the complete instruction yet
        }
        byte_count_ = 0;
        for (int i = 0; i <= last_offset_; ++i) {
            bytes += hex(code_[i], 2, false);
        }
        disassembly = std::format("{:<12} {}", bytes, instruction);
        return true;
    }

private:
    static std::string hex(int value, int width, bool showPrefix = false) {
        return std::format("{}{:0{}X}", showPrefix ? "0x" : "", value, width);
    };

    std::string disassembleInstruction() {
        word_size_ = (opcode() & 1) != 0;
        dword_ = false;
        offset_ = 1;
        if ((opcode() & 0xc4) == 0)
            return alu(op1()) + regMemPair();
        if ((opcode() & 0xc6) == 4)
            return alu(op1()) + accum() + ", " + imm();
        if ((opcode() & 0xe7) == 6)
            return "PUSH " + segreg(op1());
        if ((opcode() & 0xe7) == 7)
            return "POP " + segreg(op1());
        if ((opcode() & 0xe7) == 0x26) {
            // Segment override prefix
            return segreg(op1() & 3) + ":";
        }
        if ((opcode() & 0xf8) == 0x40)
            return "INC " + rwo();
        if ((opcode() & 0xf8) == 0x48)
            return "DEC " + rwo();
        if ((opcode() & 0xf8) == 0x50)
            return "PUSH " + rwo();
        if ((opcode() & 0xf8) == 0x58)
            return "POP " + rwo();
        if ((opcode() & 0xfc) == 0x80)
            return alu(reg()) + ea() + ", " + (opcode() == 0x81 ? iw(true) : sb(true));
        if ((opcode() & 0xfc) == 0x88)
            return "MOV " + regMemPair();
        if ((opcode() & 0xf8) == 0x90) {
            if (opcode() == 0x90) {
                // NOP is a special case of XCHG AX, AX
                return "NOP";
            }
            return "XCHG AX, " + rwo();
        }
        if ((opcode() & 0xf8) == 0xb0)
            return "MOV " + rbo() + ", " + ib();
        if ((opcode() & 0xf8) == 0xb8)
            return "MOV " + rwo() + ", " + iw();
        if ((opcode() & 0xfc) == 0xd0) {
            static std::string shifts[8] =
                {"ROL", "ROR", "RCL", "RCR", "SHL", "SHR", "SHL", "SAR"};
            return shifts[reg()] + " " + ea() + ", " +
                ((op0() & 2) == 0 ? std::string("1") : byteRegs(1));
        }
        if ((opcode() & 0xf8) == 0xd8) {
            word_size_ = false;
            dword_ = true;
            return std::string("ESC ") + std::to_string(op0()) + ", " + std::to_string(reg()) + ", " + ea();
        }
        if ((opcode() & 0xf6) == 0xe4)
            return "IN " + accum() + ", " + port();
        if ((opcode() & 0xf6) == 0xe6)
            return "OUT " + port() + ", " + accum();
        if ((opcode() & 0xe0) == 0x60) {
            static std::string conds[16] = {
                "O", "NO", "B", "AE", "E", "NE", "BE", "A",
                "S", "NS", "P", "NP", "L", "GE", "LE", "G"};
            return "J" + conds[opcode() & 0xf] + " " + cb();
        }
        switch (opcode()) {
            case 0x27:
                return "DAA";
            case 0x2f:
                return "DAS";
            case 0x37:
                return "AAA";
            case 0x3f:
                return "AAS";
            case 0x84:
            case 0x85:
                return "TEST " + regMemPair();
            case 0x86:
            case 0x87:
                return "XCHG " + regMemPair();
            case 0x8c:
                word_size_ = true;
                return "MOV " + ea() + ", " + segreg(reg());
            case 0x8d:
                dword_ = true;
                word_size_ = false;
                return "LEA " + rw() + ", " + ea();
            case 0x8e:
                word_size_ = true;
                return "MOV " + segreg(reg()) + ", " + ea();
            case 0x8f:
                return "POP " + ea();
            case 0x98:
                return "CBW";
            case 0x99:
                return "CWD";
            case 0x9a:
                return "CALL " + cp();
            case 0x9b:
                return "WAIT";
            case 0x9c:
                return "PUSHF";
            case 0x9d:
                return "POPF";
            case 0x9e:
                return "SAHF";
            case 0x9f:
                return "LAHF";
            case 0xa0:
            case 0xa1:
                return "MOV " + accum() + ", " + size() + "[" + iw() + "]";
            case 0xa2:
            case 0xa3:
                return "MOV " + size() + "[" + iw() + "], " + accum();
            case 0xa4:
            case 0xa5:
                return "MOVS" + size();
            case 0xa6:
            case 0xa7:
                return "CMPS" + size();
            case 0xa8:
            case 0xa9:
                return "TEST " + accum() + ", " + imm();
            case 0xaa:
            case 0xab:
                return "STOS" + size();
            case 0xac:
            case 0xad:
                return "LODS" + size();
            case 0xae:
            case 0xaf:
                return "SCAS" + size();
            case 0xc0:
            case 0xc2:
                return "RET " + iw();
            case 0xc1:
            case 0xc3:
                return "RET";
            case 0xc4:
                dword_ = true;
                return "LES " + rw() + ", " + ea();
            case 0xc5:
                dword_ = true;
                word_size_ = false;
                return "LDS " + rw() + ", " + ea();
            case 0xc6:
            case 0xc7:
                return "MOV " + ea() + ", " + imm(true);
            case 0xc8:
            case 0xca:
                return "RETF " + iw();
            case 0xc9:
            case 0xcb:
                return "RETF";
            case 0xcc:
                return "INT 3";
            case 0xcd:
                return "INT " + ib();
            case 0xce:
                return "INTO";
            case 0xcf:
                return "IRET";
            case 0xd4:
                return "AAM " + ib();
            case 0xd5:
                return "AAD " + ib();
            case 0xd6:
                return "SALC";
            case 0xd7:
                return "XLATB";
            case 0xe0:
                return "LOOPNE " + cb();
            case 0xe1:
                return "LOOPE " + cb();
            case 0xe2:
                return "LOOP " + cb();
            case 0xe3:
                return "JCXZ " + cb();
            case 0xe8:
                return "CALL " + cw();
            case 0xe9:
                return "JMP " + cw();
            case 0xea:
                return "JMP " + cp();
            case 0xeb:
                return "JMP " + cb();
            case 0xf0:
            case 0xf1:
                return "LOCK";
            case 0xf2:
                return "REPNE ";
            case 0xf3:
                return "REP ";
            case 0xf4:
                return "HLT";
            case 0xf5:
                return "CMC";
            case 0xf6:
            case 0xf7:
                switch (reg()) {
                    case 0:
                    case 1:
                        return "TEST " + ea() + ", " + imm(true);
                    case 2:
                        return "NOT " + ea();
                    case 3:
                        return "NEG " + ea();
                    case 4:
                        return "MUL " + ea();
                    case 5:
                        return "IMUL " + ea();
                    case 6:
                        return "DIV " + ea();
                    default:
                        return "IDIV " + ea();
                }
            case 0xf8:
                return "CLC";
            case 0xf9:
                return "STC";
            case 0xfa:
                return "CLI";
            case 0xfb:
                return "STI";
            case 0xfc:
                return "CLD";
            case 0xfd:
                return "STD";
            case 0xfe:
            case 0xff:
                switch (reg()) {
                    case 0:
                        return "INC " + ea();
                    case 1:
                        return "DEC " + ea();
                    case 2:
                        return "CALL " + ea();
                    case 3:
                        dword_ = true;
                        return "CALL " + ea();
                    case 4:
                        return "JMP " + ea();
                    case 5:
                        dword_ = true;
                        return "JMP " + ea();
                    case 6:
                        return "PUSH " + ea();
                    default:
                        return "??? " + ea();
                }
            default:
                break;
        }
        return "!b";
    }

    static bool isPrefix(const uint8_t byte) {
        return (byte == 0x26 || byte == 0x2e || byte == 0x36 || byte == 0x3e ||
            byte == 0xf0 || byte == 0xf1 || byte == 0xf2 || byte == 0xf3);
    }

    uint8_t getByte(const int offset) {
        last_offset_ = std::max(last_offset_, offset);
        return code_[offset];
    }

    uint16_t getWord(const int offset) {
        return getByte(offset) | (getByte(offset + 1) << 8);
    }

    std::string regMemPair() {
        if ((op0() & 2) == 0) {
            return ea() + ", " + r();
        }
        return r() + ", " + ea();
    }

    std::string r() { return !word_size_ ? rb() : rw(); }
    std::string rb() { return byteRegs(reg()); }
    std::string rw() { return wordRegs(reg()); }
    std::string rbo() { return byteRegs(op0()); }
    std::string rwo() { return wordRegs(op0()); }

    static std::string byteRegs(const int r) {
        static std::string b[8] = {"AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"};
        return b[r];
    }

    static std::string wordRegs(const int r) {
        static std::string w[8] = {"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"};
        return w[r];
    }

    std::string ea() {
        std::string s;
        switch (mod()) {
            case 0:
                s = disp();
                break;
            case 1:
                s = disp() + sb();
                offset_ = 3;
                break;
            case 2:
                s = disp() + "+" + iw();
                offset_ = 4;
                break;
            default: // 3
                return !word_size_ ? byteRegs(rm()) : wordRegs(rm());
        }
        return size() + "[" + s + "]";
    }

    std::string size() const {
        if (!dword_) {
            return (!word_size_ ? "B" : "W");
        }
        return (!word_size_ ? "" : "D");
    }

    std::string disp() {
        static std::string d[8] = {
            "BX+SI", "BX+DI", "BP+SI", "BP+DI", "SI", "DI", "BP", "BX"};
        if (mod() == 0 && rm() == 6) {
            std::string s = iw();
            offset_ = 4;
            return s;
        }
        return d[rm()];
    }

    static std::string alu(const int op) {
        static std::string o[8] = {
            "ADD ", "OR ", "ADC ", "SBB ", "AND ", "SUB ", "XOR ", "CMP "};
        return o[op];
    }

    uint8_t opcode() { return getByte(0); }
    int op0() { return opcode() & 7; }
    int op1() { return (opcode() >> 3) & 7; }

    uint8_t modRM() {
        offset_ = 2;
        return getByte(1);
    }

    int mod() { return modRM() >> 6; }
    int reg() { return (modRM() >> 3) & 7; }
    int rm() { return modRM() & 7; }
    std::string imm(const bool m = false) { return !word_size_ ? ib(m) : iw(m); }

    std::string iw(const bool m = false) {
        if (m) {
            ea();
        }
        return hex(getWord(offset_), 4, false);
    }

    std::string ib(const bool m = false) {
        if (m) {
            ea();
        }
        return hex(getByte(offset_), 2, false);
    }

    std::string sb(const bool m = false) {
        if (m) {
            ea();
        }
        const uint8_t byte = getByte(offset_);
        if ((byte & 0x80) == 0) {
            return "+" + hex(byte, 2, false);
        }
        return "-" + hex(-byte, 2, false);
    }

    std::string accum() const { return !word_size_ ? "AL" : "AX"; }

    static std::string segreg(const int r) {
        static std::string sr[8] = {"ES", "CS", "SS", "DS", "??", "??", "??", "??"};
        return sr[r];
    }

    std::string cb() {
        return "IP" + sb();
        //hex(_address + static_cast<SInt8>(getByte(_offset)), 4, false);
    }

    std::string cw() {
        return "IP+" + iw();
        //return hex(_address + getWord(_offset), 4, false);
    }

    std::string cp() {
        return hex(getWord(offset_ + 2), 4, false) + ":" +
            hex(getWord(offset_), 4, false);
    }

    std::string port() { return ((op1() & 1) == 0 ? ib() : wordRegs(2)); }

    uint16_t ip_{};
    uint8_t code_[MAX_INSTRUCTION_BYTES]{};
    int byte_count_{};
    bool word_size_{};
    bool dword_{};
    int offset_{};
    int last_offset_{};
};

