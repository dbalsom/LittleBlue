#ifndef CPU_H
#define CPU_H

#include <iostream>
#include <iomanip>
#include <string>
#include <format>
#include <SDL3/SDL_log.h>
#include <deque>

#include "../xtce_blue.h"
#include "Bus.h"
#include "SnifferDecoder.h"

#include "microcode.h"

#define LINE_ENDING_SIZE 1
#define DEBUG_MC 0

enum class Register : size_t
{
    ES,
    CS,
    SS,
    DS,
    PC,
    IND,
    OPR,
    R7,
    R8,
    R9,
    R10,
    R11,
    TMPA,
    TMPB,
    TMPC,
    FLAGS,
    R16,
    R17,
    M,
    R,
    SIGMA,
    ONES,
    R22,
    R23,
    AX,
    CX,
    DX,
    BX,
    SP,
    BP,
    SI,
    DI,
};

constexpr size_t reg_to_idx(Register r) {
    return static_cast<size_t>(r);
}

template <typename BusType = Bus, typename WordT = uint8_t, std::size_t QueueLen = 4>
class Cpu
{
    static_assert(
        std::is_same<WordT, uint8_t>::value ||
        std::is_same<WordT, uint16_t>::value,
        "Cpu WordT must be uint8_t or uint16_t"
        );
    static_assert(QueueLen > 0, "QueueLen must be > 0");

    enum GroupDecodeFlags
    {
        // The original 15 outputs of the Group decode PLA
        groupMemory = 1, // This signal controls the M/IO (S2) status line
        groupInitialEARead = 2, // This signal indicates that the effective address is read by the instruction.
        groupMicrocodePointerFromOpcode = 4, // Group 3/4/5 opcode - microcode address will be determined by ModR/M.
        groupNonPrefix = 8, // This line controls whether the instruction is a prefix or not.
        groupEffectiveAddress = 0x10, // This signal indicates whether the instruction has a ModR/M byte.
        groupAddSubBooleanRotate = 0x20, // This signal specifies the top bit for an ALU operation.
        groupNonFlagSet = 0x40, // This signal determines whether the instruction directly updates flags (not CMC).
        groupMNotAccumulator = 0x80, // This signal controls whether the instruction uses the AL or AX register for M.
        groupNonSegregEA = 0x100, // This signal controls whether an instruction indexes a segment register.
        groupNoDirectionBit = 0x200, // This signal controls whether an opcode encodes a direction bit.
        groupMicrocoded = 0x400, // This signal indicates whether the instruction is microcoded or not.
        groupNoWidthInOpcodeBit0 = 0x800, // This signal indicates whether the opcode encodes width in bit 0
        groupByteOrWordAccess = 0x1000, // This signal indicates whether the instruction accesses byte or word data.
        groupF1ZZFromPrefix = 0x2000,
        groupIncDec = 0x4000, // This signal indicates whether carry is allowed to be updated.

        // Direct column outputs
        groupLoadRegisterImmediate = 0x10000,
        groupWidthInOpcodeBit3 = 0x20000,
        groupCMC = 0x40000,
        groupHLT = 0x80000,
        groupREP = 0x100000,
        groupSegmentOverride = 0x200000,
        groupLOCK = 0x400000,
        groupCLI = 0x800000,

        // This is not part of the PLA outputs, but we derive it here for convenience
        groupLoadSegmentRegister = 0x1000000,
    };

public:
    enum class RunResult { Ok, Halt, BreakpointHit };

    enum MicrocodeState
    {
        stateRunning,
        stateWaitingForQueueData,
        stateWaitingForQueueIdle,
        stateIODelay2,
        stateIODelay1,
        stateWaitingUntilFirstByteCanStart,
        stateWaitingUntilFirstByteDone,
        stateWaitingUntilSecondByteDone,
        stateSingleCycleWait,
        stateHaltingStart,
        stateHalting3,
        stateHalting2,
        stateHalting1,
        stateHalted,
        stateSuspending,
    };

    // Prefetch queue entry
    struct QueueEntry
    {
        uint8_t data; // fetched byte
        uint16_t address; // ind at which byte was fetched
    };

    // Default constructor: default-construct the bus and initialize CPU state
    Cpu() :
        _consoleLogging(false), _bus() {
        initializeCommon();
    }

    // Forwarding constructor: allow callers to construct CpuT with arguments forwarded to BusType's constructor.
    template <typename... Args>
    explicit Cpu(Args&&... args) :
        _consoleLogging(false), _bus(std::forward<Args>(args)...) {
        initializeCommon();
    }

    MicrocodeState getMCState() const { return _state; }
    BusType* getBus() { return &_bus; }
    uint8_t getALU() const { return _alu; }
    uint8_t* getRAM() { return _bus.ram(); }
    uint16_t* getMainRegisters() { return &_registers[24]; }
    uint16_t* getRegisters() { return &_registers[0]; }
    void stubInit() { _bus.stubInit(); }

    void setExtents(int logStartCycle, int logEndCycle, int executeEndCycle, int stopIP, int stopSeg) {
        _logStartCycle = logStartCycle + 4;
        _logEndCycle = logEndCycle;
        _executeEndCycle = executeEndCycle;
        _stopIP = stopIP;
        _stopSeg = stopSeg;
    }

    uint16_t getInstructionPointer() const {
        return _inst_address;
    }

    void setInitialIP(int v) { pc() = v; }
    uint64_t cycle() const { return _cycle >= 11 ? _cycle - 11 : 0; }

    std::string log() const {
        // Assemble buffered lines into a single string on request
        std::string out;
        for (const auto& s : _logBuffer) {
            out += s;
        }
        return out;
    }

    void reset() {
        //_bus.reset();
        for (unsigned short& _register : _registers) {
            _register = 0;
        }
        setBytePointers();
        _inst_boundary = false;
        _registers[1] = 0xFFFF; // RC (CS)
        _registers[21] = 0xFFFF; // ONES
        _registers[15] = 2; // FLAGS

        _cycle = 0;
        _microcodePointer = 0x1800;
        _busState = tIdle;
        _ioType = ioPassive;
        _snifferDecoder.reset();
        _prefetching = true;
        _logBuffer.clear();
        pc() = 0;
        _nmiRequested = false;
        _alu = 0;
        _aluInput = 0;
        _queueBytes = 0;
        _queue = 0;
        _newQueue = {};
        _segmentOverride = -1;
        _f1 = false;
        _repne = false;
        _lock = false;
        _loaderState = 0;
        _lastMicrocodePointer = -1;
        _dequeueing = false;
        _ioCancelling = 0;
        _ioRequested = false;
        _t4 = false;
        _prefetchDelayed = false;
        _queueFlushing = false;
        _lastIOType = ioPassive;
        _t5 = false;
        _interruptPending = false;
        _ready = true;
        //_cyclesUntilCanLowerQueueFilled = 0;
        _locking = false;
        _breakpointHit = false;

        // Reset flags
        _carry = false;
        _carryLatch = false;
        _zero = false;
        _superZero = false;
        _auxiliary = false;
        _sign = false;
        _parity = false;
        _overflow = false;
    }

    RunResult run_for(const int cycleCt) {

        _inst_boundary = false;
        sanity_check();
        // Clear the breakpoint status if we're being asked to run again
        _breakpointHit = false;

        for (int i = 0; i < cycleCt; i++) {
            simulateCycle();

            // If we've reached an instruction boundary this cycle, check the breakpoint
            if (_inst_boundary) {
                _inst_boundary = false;
                if (_hasBreakpoint) {
                    uint16_t realIP = getRealIP();
                    if (cs() == _breakpoint_cs && realIP == _breakpoint_ip) {
                        _breakpointHit = true;
                        return RunResult::BreakpointHit;
                    }
                }
            }

            if (_breakpointHit) {
                // Stop executing further cycles if a breakpoint was hit by other means
                return RunResult::BreakpointHit;
            }
        }

        return RunResult::Ok;
    }

    void run() {
        do {
            simulateCycle();
        }
        while ((getRealIP() != _stopIP + 2 || cs() != _stopSeg) && _cycle < _executeEndCycle);
    }

    void setConsoleLogging() {
        _consoleLogging = true;
    }

    // Run CPU cycles until the next instruction boundary is reached.
    // Returns the number of CPU cycles executed.
    int stepToNextInstruction() {
        _inst_boundary = false;
        int cycles = 0;
        // if _rni is true, clear it first by cycling the CPU until it becomes false.
        while (_rni && _state != stateHalted) {
            simulateCycle();
        }
        //_rni = false;
        // Run cycles until _rni becomes true or the CPU halts.
        while (!_inst_boundary && _state != stateHalted) {
            simulateCycle();
            ++cycles;
            // Prevent infinite loop in funky situations by limiting cycles
            if (cycles > 1000000) {
                break;
            }
        }
        return cycles;
    }

    void setRegister(const Register r, const uint16_t v) {
        _registers[reg_to_idx(r)] = v;
    }

    uint16_t getRegister(const Register r) const {
        return _registers[reg_to_idx(r)];
    }

    uint16_t getRealIP() {
        return pc() - _queueBytes;
    }

    void setTestNumber(uint32_t n) {
        _testNumber = n;
    }

    // Breakpoint API
    void setBreakpoint(const uint16_t cs, const uint16_t ip) {
        _breakpoint_cs = cs;
        _breakpoint_ip = ip;
        _hasBreakpoint = true;
        _breakpointHit = false;
    }

    void clearBreakpoint() {
        _hasBreakpoint = false;
        _breakpointHit = false;
    }

    bool hasBreakpoint() const { return _hasBreakpoint; }
    bool breakpointHit() const { return _breakpointHit; }
    uint16_t breakpointCS() const { return _breakpoint_cs; }
    uint16_t breakpointIP() const { return _breakpoint_ip; }
    void clearBreakpointHit() { _breakpointHit = false; }

private:
    enum IOType
    {
        ioInterruptAcknowledge = 0,
        ioReadPort = 1,
        ioWritePort = 2,
        ioHalt = 3,
        ioPrefetch = 4,
        ioReadMemory = 5,
        ioWriteMemory = 6,
        ioPassive = 7
    };

    void setBytePointers() {
        // Initialize _byteRegisters as byte pointers into _registers.
        // Endianness is checked by setting a known value and examining the first byte.
        // This is clever, but it makes Cpu not object-safe for assignment/relocation.
        _registers[24] = 0x100;
        const auto byteData = reinterpret_cast<uint8_t*>(&_registers[24]);
        const int bigEndian = *byteData;
        for (int i = 0; i < 8; ++i) {
            const int byteNumbers[8] = {0, 2, 4, 6, 1, 3, 5, 7};
            // The index will be XOR'd with either 0 (little-endian) or 1 (big-endian) to flip the byte order
            // if required.
            _byteRegisters[i] = &byteData[byteNumbers[i] ^ bigEndian];
        }

    }

    // Extracted common initialization logic so we can reuse it in all constructors.
    void initializeCommon() {
        _logStartCycle = 0;
        _logEndCycle = 100;

        setBytePointers();

        _registers[21] = 0xffff;
        _registers[23] = 0;

        std::cout << "CPU initializing." << std::endl;

        // Initialize the microcode data and put it in a format more suitable for interpreting

        // Select 8086 based on template WordT parameter.
        static const bool use8086 = std::is_same<WordT, uint16_t>::value;

        // Initialize an array to hold the 512 21-bit microcode instruction words.
        uint32_t instructions[512];
        for (unsigned int& instruction : instructions) {
            instruction = 0;
        }

        // Load the main microcode ROM
        // Iterate through 84 rows of microcode data.
        for (int y = 0; y < 84; ++y) {
            // Iterate through left and right columns.
            for (int half = 0; half < 2; ++half) {
                std::string filename = (half == 1 ? "l" : "r");
                filename += (use8086 ? "a" : "");
                std::string_view s = get_file(filename);
                // Iterate through the width of the ROM block (64).
                for (int x = 0; x < 64; ++x) {
                    int b = s[y * (64 + LINE_ENDING_SIZE) + (63 - x)] == '0' ? 1 : 0;
                    instructions[x * 8 + half * 4 + y % 4] |= (b << (20 - (y >> 2)));
                }
            }
        }

        for (int i = 0; i < 512; ++i) {
            int w = instructions[i];
#if DEBUG_MC
            std::cout << std::format("{:03X}: {:021b}\n", i, static_cast<unsigned int>(w));
#endif

            // The microcode word is somewhat scrambled compared to the word layout diagram you may have seen in online
            // documentation. We unscramble it here and store the decoded fields in the _microcode array.

            // See https://www.righto.com/2022/11/how-8086-processors-microcode-engine.html for an in-depth description
            // of the microcode format.

            // Decode Type. There are 6 different types of micro-instruction, implying a different word format.
            // All word formats share a source field, destination field, and 'update flags' flag, so we can decode the
            // common fields here.

            // The type field is either 2 or 3 bits. If 3 bits, the high bit is set.
            int typ = (w >> 7) & 7;
            if ((typ & 4) == 0) {
                // High bit is not set, so this is a 2-bit type field. Shift right to discard the low bit.
                typ >>= 1;
            }

            // Decode Source field
            int s = ((w >> 13) & 1) + ((w >> 10) & 6) + ((w >> 11) & 0x18);

            // Decode Destination field
            int d = ((w >> 20) & 1) + ((w >> 18) & 2) + ((w >> 16) & 4) + ((w >> 14) & 8) + ((w >> 12) & 0x10);

            // Decode 'update flags' flag
            int f = (w >> 10) & 1;

            // _microcode has four bytes for each decoded word - Destination, Source, (F + type), Operands
            _microcode[i * 4] = d;
            _microcode[i * 4 + 1] = s;
            _microcode[i * 4 + 2] = (f << 3) + typ; // Pack F and type together
            _microcode[i * 4 + 3] = w & 0xff; // Low 8 bits are operands - they require further unpacking.
        }

#if DEBUG_MC
        std::cout << "Instruction words loaded." << std::endl;
#endif

        // Read in the stage1 decoder PLA ROM logic.
        // The 8088 utilizes a "match decoder" which takes an 11-bit input and activates one column of the microcode
        // ROM. This decoder is implemented as a programmable logic array (PLA) with 128 columns. The match logic
        // allows a match on 0, 1, or "don't care" for each of the 11 input bits.

        // To implement efficient microcode lookups, we expand the PLA data into a full 11-bit (2048-entry) lookup
        // table, _microcodeIndex. This LUT is by nature sparse, but we shouldn't ever attempt to access a non-mapped
        // entry during normal operation.
        int stage1[128];
        for (int x = 0; x < 128; ++x) {
            stage1[x] = 0;
        }
        for (int g = 0; g < 9; ++g) {
            // Width of each ROM file is 16, except for groups 0 and 8 which are 8.
            int n = 16;
            if (g == 0 || g == 8) {
                n = 8;
            }

            // This array provides the X bit position offset for each group.
            int xx[9] = {0, 8, 24, 40, 56, 72, 88, 104, 120};
            int xp = xx[g];

            // Iterate through top and bottom halves of each ROM file.
            for (int h = 0; h < 2; ++h) {
                std::string filename = decimal(g) + (h == 0 ? "t" : "b") + ".txt";
#if DEBUG_MC
                std::cout << "Loading microcode file: " << filename << std::endl;
#endif
                std::string_view s = get_file(filename);

                // Iterate through the height of each ROM file (11 rows).
                for (int y = 0; y < 11; ++y) {
                    for (int x = 0; x < n; ++x) {
                        int b = s[y * (n + LINE_ENDING_SIZE) + x] == '0' ? 1 : 0;
                        if (b != 0) {
                            // Stage1 is written in reverse-order.
                            stage1[127 - (x + xp)] |= 1 << (y * 2 + (h ^ (y <= 2 ? 1 : 0)));
                        }
                    }
                }
            }
        }

        // Iterate through all possible 11-bit input combinations (2048), simulating all possible inputs into the
        // decoder PLA.
        for (int i = 0; i < 2048; ++i) {
            static const int ba[] = {7, 2, 1, 0, 5, 6, 8, 9, 10, 3, 4};

            // Scan through the decoder. There is one decoder column for every 4 microcode words.
            for (int j = 0; j < 128; ++j) {
                int s1 = stage1[j];
                // Skip empty decoder slots as they cannot match anything
                if (s1 == 0) {
                    continue;
                }
                bool matches = true;
                // The decoder uses 11-bit matches. Increment a bit index from 0..10.
                for (int b = 0; b < 11; ++b) {
                    int x = (s1 >> (ba[b] * 2)) & 3;
                    int ib = (i >> (10 - b)) & 1;
                    if (!(x == 0 || (x == 1 && ib == 1) || (x == 2 && ib == 0))) {
                        matches = false;
                        break;
                    }
                }

                if (matches) {
                    // This 11-bit input matches this decoder column, save it to the LUT.
                    _microcodeIndex[i] = j;
                    break;
                }
            }
        }

        // Decode the translation PLA.
        // The translation PLA takes 8 bits of input and outputs a full 13-bit microcode address and a 1-bit segment
        // specifier.

        // The inputs vary depending on the type of lookup.
        // For looking up an EA calculation microcode address, the inputs are 5 bits from the ModRM byte.

        std::string translationFile = use8086 ? "translation_8086.txt" : "translation_8088.txt";
#if DEBUG_MC
        std::cout << "Loading translation ROM: " << translationFile << std::endl;
#endif
        std::string_view translationString = get_file(translationFile);
        int tsp = 0;
        char c = translationString[0];

        // Iterate through the first 33 rows of the translation file (empty lines will be skipped)
        for (int i = 0; i < 33; ++i) {
            int mask = 0;
            int bits = 0;
            int output = 0;

            // Iterate through the 8-character match mask that starts each line, constructing a bit mask
            // for matching.
            for (int j = 0; j < 8; ++j) {
                if (c != '?') {
                    mask |= 128 >> j;
                }
                if (c == '1') {
                    bits |= 128 >> j;
                }
                c = translationString[++tsp];
            }

            // Iterate through the 14-character addresses corresponding to each match mask
            for (int j = 0; j < 14; ++j) {
                while (c != '0' && c != '1') {
                    // Skip any whitespace
                    c = translationString[++tsp];
                }
                if (c == '1') {
                    // Set the corresponding output bit
                    output |= 8192 >> j;
                }
                c = translationString[++tsp];
            }
            // Consume rest of line
            while (c != 0x0A) {
                c = translationString[++tsp];
            }
            // Consume newlines
            while (c == 0x0A) {
                c = translationString[++tsp];
            }

            for (int j = 0; j < 256; ++j) {
                if ((j & mask) == bits) {
#if DEBUG_MC
                    std::cout << "Translation output: " << std::format("{:02X}: {:014b}\n", j, output);
#endif
                    _translation[j] = output;
                }
            }
        }

        // Decode the group decode PLA.
        // The group decode PLA takes 9 bits of input (from opcode and prefixes) and outputs 15 control bits.

        // For a detailed examination of the group decode PLA, see
        // https://www.righto.com/2023/05/8086-processor-group-decode-rom.html

        int groupInput[38 * 18];
        int groupOutput[38 * 15];
        std::string_view groupString = get_file("group.txt");

        // Iterate through each column of the group decode ROM.
        for (int x = 0; x < 38; ++x) {
            // Iterate through the first 15 rows of the group decode ROM.
            for (int y = 0; y < 15; ++y) {
                groupOutput[y * 38 + x] = groupString[y * (38 + LINE_ENDING_SIZE) + x] == '0' ? 0 : 1;
            }
            for (int y = 0; y < 18; ++y) {
                c = groupString[((y / 2) + 15) * (38 + LINE_ENDING_SIZE) + x];
                if ((y & 1) == 0) {
                    groupInput[y * 38 + x] = (c == '*' || c == '0') ? 1 : 0;
                }
                else {
                    groupInput[y * 38 + x] = (c == '*' || c == '1') ? 1 : 0;
                }
            }
        }
        static const int groupYY[18] = {1, 0, 3, 2, 4, 6, 5, 7, 11, 10, 12, 13, 8, 9, 15, 14, 16, 17};
        for (int x = 0; x < 34; ++x) {
            if (x == 11) {
                // Column 11 doesn't activate any outputs.
                continue;
            }

            for (int i = 0; i < 0x101; ++i) {
                bool found = true;
                for (int j = 0; j < 9; ++j) {
                    int y0 = groupInput[groupYY[j * 2] * 38 + x];
                    int y1 = groupInput[groupYY[j * 2 + 1] * 38 + x];
                    int b = (i >> j) & 1;
                    if ((y0 == 1 && b == 1) || (y1 == 1 && b == 0)) {
                        found = false;
                        break;
                    }
                }
                if (found) {
                    int g = 0;
                    for (int j = 0; j < 15; ++j) {
                        g |= groupOutput[j * 38 + x] << j;
                    }
                    if (x == 10) {
                        g |= groupLoadRegisterImmediate;
                    }
                    if (x == 12) {
                        g |= groupWidthInOpcodeBit3;
                    }
                    if (x == 13) {
                        g |= groupCMC;
                    }
                    if (x == 14) {
                        g |= groupHLT;
                    }
                    if (x == 31) {
                        g |= groupREP;
                    }
                    if (x == 32) {
                        g |= groupSegmentOverride;
                    }
                    if (x == 33) {
                        g |= groupLOCK;
                    }
                    if (i == 0xFA) {
                        g |= groupCLI;
                    }
                    if (i == 0x8E || (i & 0xE7) == 0x07) {
                        g |= groupLoadSegmentRegister;
                    }
                    _groups[i] = g;
                }
            }
        }

#if DEBUG_MC
        for (int i = 0; i < 256; i++) {
            std::cout << std::format("{:02X}:{:08X}\n", i, _groups[i]);
        }
#endif

        _microcodePointer = 0;
        _microcodeReturn = 0;
    }

    void sanity_check() {
        if (_byteRegisters[0] != reinterpret_cast<uint8_t*>(&_registers[24])) {
            std::cerr << "byte register pointers invalidated!" << std::endl;
        }
    }

    uint8_t queueRead() {
        const uint8_t q1 = _queue & 0xff;
        uint8_t q2 = 0;
        _newQueue.pop(q2);
        assert(q1 == q2);
        _dequeueing = true;
        _snifferDecoder.queueOperation(3);
        return q1;
    }

    int modRMReg() const { return (_modRM >> 3) & 7; }
    int modRMReg2() const { return _modRM & 7; }
    uint16_t& rw(const int r) { return _registers[24 + r]; }
    uint16_t& rw() { return rw(_opcode & 7); }
    uint16_t& ax() { return rw(0); }
    uint8_t& rb(const int r) const { return *_byteRegisters[r]; }
    uint8_t& al() const { return rb(0); }
    uint16_t& sr(const int r) { return _registers[r & 3]; }
    uint16_t& cs() { return sr(1); }

    // The carry flag is special and is gated by a latch to prevent it from being updated during certain instructions.
    // The group decode ROM output groupIncDec forces this latch low.
    // The carry latch starts low at microcode execution start so that EA calculations do not update the carry flag.
    // The RCY microcode operation can also set the carry latch low.
    void setAluCF(const bool v) {
        if (_carryLatch && (_group & groupIncDec) == 0) {
            _carry = v;
        }
    }

    bool getAluCF() const { return _carry; }
    bool cf() { return lowBit(flags()); }
    void setCF(const bool v) { flags() = (flags() & ~1) | (v ? 1 : 0); }
    bool pf() { return (flags() & 4) != 0; }
    bool af() { return (flags() & 0x10) != 0; }
    bool zf() { return (flags() & 0x40) != 0; }
    bool sf() { return (flags() & 0x80) != 0; }
    bool intf() { return (flags() & 0x200) != 0; }
    void setIF(const bool v) { flags() = (flags() & ~0x200) | (v ? 0x200 : 0); }
    bool df() { return (flags() & 0x400) != 0; }
    void setDF(const bool v) { flags() = (flags() & ~0x400) | (v ? 0x400 : 0); }
    bool of() { return (flags() & 0x800) != 0; }
    void setOF(const bool v) { flags() = (flags() & ~0x800) | (v ? 0x800 : 0); }
    uint16_t& pc() { return _registers[4]; }
    uint16_t& ind() { return _registers[5]; }
    uint16_t& opr() { return _registers[6]; }
    uint16_t& tmpa() { return _registers[12]; }
    uint16_t& tmpb() { return _registers[13]; }
    uint16_t& flags() { return _registers[15]; }
    uint16_t& modRMRW() { return rw(modRMReg()); }
    uint8_t& modRMRB() { return rb(modRMReg()); }
    uint16_t& modRMRW2() { return rw(modRMReg2()); }
    uint8_t& modRMRB2() { return rb(modRMReg2()); }

    uint16_t getMemOrReg(bool mem) {
        if (mem) {
            if (_useMemory) {
                return opr();
            }
            if (!_wordSize) {
                return modRMRB2();
            }
            return modRMRW2();
        }
        if ((_group & groupNonSegregEA) == 0) {
            return sr(modRMReg());
        }
        if (!_wordSize) {
            int n = modRMReg();
            uint16_t r = rw(n & 3);
            if ((n & 4) != 0) {
                r = (r >> 8) + (r << 8);
            }
            return r;
        }
        return modRMRW();
    }

    void setMemOrReg(bool mem, uint16_t v) {
        if (mem) {
            if (_useMemory) {
                opr() = v;
            }
            else {
                if (!_wordSize) {
                    modRMRB2() = static_cast<uint8_t>(v);
                }
                else {
                    modRMRW2() = v;
                }
            }
        }
        else {
            if ((_group & groupNonSegregEA) == 0) {
                sr(modRMReg()) = v;
            }
            else {
                if (!_wordSize) {
                    modRMRB() = static_cast<uint8_t>(v);
                }
                else {
                    modRMRW() = v;
                }
            }
        }
    }

    void startInstruction() {
        if ((_group & groupNonPrefix) != 0) {
            _segmentOverride = -1;
            _f1 = false;
            _repne = false;
            if (_lock) {
                _lock = false;
                _bus.setLock(false);
            }
        }
        _opcode = _nextMicrocodePointer >> 4;
        _group = _nextGroup;
    }

    void readFlags() {
        _carry = cf();
        _overflow = of();
        _parity = pf() ? 0x04 : 0;
        _sign = sf();
        _zero = zf();
        _auxiliary = af();
    }

    // Resolve the effective address calculation microcode address via the translation PLA, and set the effective base
    // segment accordingly.
    void lookupEACalculationAddress() {
        // The input to the translation PLA is 8 bits, 5 of which are from the ModR/M byte.
        // Bit 0 is don't care.
        // Bit 1 is always set for an EA lookup.
        // Bit 2 is don't care.
        // Bits 3-5 are ModRM bits 0-3 (RM field).
        // Bits 6-7 are ModRM bits 6-7 (Mod field).
        const int t = _translation[2 + ((_modRM & 7) << 3) + (_modRM & 0xc0)];
        // Bit 0 of the translation PLA output for an EA calculation address selects either DS or SS.
        _segment = (lowBit(t) ? 2 : 3);
        // Save the return address and jump to EA calculation microcode. This takes one cycle.
        _microcodeReturn = _microcodePointer;
        _microcodePointer = t >> 1;
    }

    void startMicrocodeInstruction() {
        _loaderState = 2;
        startInstruction();
        _microcodePointer = _nextMicrocodePointer;
        _wordSize = true;
        if ((_group & groupNoWidthInOpcodeBit0) == 0 && !lowBit(_opcode)) {
            _wordSize = false;
        }
        if ((_group & groupByteOrWordAccess) == 0) {
            _wordSize = false; // Just for XLAT
        }
        if (_testNumber == 77) {
            std::cout << "Foo";
        }
        readFlags();
        // Default is ADD tmpa (12) (assumed in EA calculations)
        _alu = 0;
        _aluInput = 0;
        _mIsM = ((_group & groupNoDirectionBit) != 0 || (_opcode & 2) == 0);
        _rni = false;
        _nx = false;
        _skipRNI = false;
        _state = stateRunning;

        if ((_group & groupEffectiveAddress) != 0) {
            // This opcode has a ModR/M.

            // Disable the carry latch for EA calculation
            _carryLatch = false;
            // EALOAD and EADONE finish with RTN
            _modRM = _nextModRM;
            if ((_group & groupMicrocodePointerFromOpcode) == 0) {
                _microcodePointer = ((_modRM << 1) & 0x70) | 0xf00 |
                    ((_opcode & 1) << 12) | ((_opcode & 8) << 4);
                _state = stateSingleCycleWait;
            }
            // Check that RM != 11 (which would indicate a register-only operation)
            _useMemory = ((_modRM & 0xc0) != 0xc0);
            if (_useMemory) {
                // We have a memory operand - resolve the correct microcode address for EA calculation via the
                // translation PLA.
                lookupEACalculationAddress();
                _state = stateSingleCycleWait;
            }
        }
    }

    // Not every instruction on the 8088 is microcoded - there are several small one-byte instructions that are
    // implemented in discrete logic. These include prefixes, the flag-twiddling opcodes, as most notoriously HLT.
    // We handle the logic for these here.
    // Since non-microcoded instructions don't have an RNI flag, we set the 'inst_boundary' flag when they complete.
    void startNonMicrocodeInstruction() {
        _loaderState = 0;
        startInstruction();
        // LOCK prefix
        if ((_group & groupLOCK) != 0) {
            _locking = true;
            return;
        }
        // REP prefix
        if ((_group & groupREP) != 0) {
            // REP prefix - set the F1 flag.

            // Internally, the F1 flag is overloaded to indicate both the presence of a REP prefix and the need to run
            // NEGATE for MUL/DIV instructions. This causes the interesting effect of inverting the product/quotient
            // when a REP prefix is used with MUL/DIV!
            _f1 = true;
            _repne = !lowBit(_opcode);
            return;
        }
        if ((_group & groupHLT) != 0) {
            _loaderState = 2;
            _rni = false;
            _nx = false;
            _state = stateHaltingStart;
            _extraHaltDelay =
                !((_busState == tIdle && !_t5 && !_t6 && _ioType == ioPassive)
                    || (_t5 && _lastIOType != ioPrefetch));
            return;
        }
        if ((_group & groupCMC) != 0) {
            flags() ^= 1;
            _inst_boundary = true;
            return;
        }
        if ((_group & groupNonFlagSet) == 0) {
            switch (_opcode & 0x06) {
                case 0: // CLCSTC
                    setCF(_opcode & 1);
                    _inst_boundary = true;
                    break;
                case 2: // CLISTI
                    setIF(_opcode & 1);
                    _inst_boundary = true;
                    break;
                case 4: // CLDSTD
                    setDF(_opcode & 1);
                    _inst_boundary = true;
                    break;
                default:
                    break;
            }
            return;
        }
        if ((_group & groupSegmentOverride) != 0) {
            _segmentOverride = (_opcode >> 3) & 3;
        }
    }

    uint16_t doRotate(uint16_t v, uint16_t a, bool carry) {
        _carry = carry;
        _overflow = topBit(v ^ a);
        return v;
    }

    uint16_t doShift(uint16_t v, uint16_t a, bool carry, bool auxiliary) {
        _auxiliary = auxiliary;
        doPZS(v);
        return doRotate(v, a, carry);
    }

    uint16_t doPass(uint16_t v) {
        _auxiliary = false;
        doPZS(v);
        return v;
    }

    uint16_t doSetmo() {
        _carry = false;
        _overflow = false;
        _auxiliary = false;
        doPZS(0xFFFF);
        return 0xFFFF;
    }

    uint16_t doALU() {
        uint16_t t;
        uint32_t a = _registers[_aluInput + 12], v;
        switch (_alu) {
            case 0x00: // ADD
            case 0x02: // ADC
                return add(a, tmpb(), _carry && _alu != 0);
            case 0x01: // OR
                return bitwise(a | tmpb());
            case 0x03: // SBB
            case 0x05: // SUBT
            case 0x07: // CMP
                return sub(a, tmpb(), _carry && _alu == 3);
            case 0x04: // AND
                return bitwise(a & tmpb());
            case 0x06: // XOR
                return bitwise(a ^ tmpb());
            case 0x08: // ROL
                if (_testNumber == 77) {
                    std::cout << "Foo";
                }
                return doRotate((a << 1) | (topBit(a) ? 1 : 0), a, topBit(a));
            case 0x09: // ROR
                return doRotate(((a & wordMask()) >> 1) | topBit(lowBit(a)), a, lowBit(a));
            case 0x0a: // LRCY

                if (a == 0xAD61) {
                    // break here
                    //std::cout << " have 0xAD61\n";
                }
                return doRotate((a << 1) | (_carry ? 1 : 0), a, topBit(a));
            case 0x0b: // RRCY
                return doRotate(((a & wordMask()) >> 1) | topBit(_carry), a, lowBit(a));
            case 0x0c: // SHL
                return doShift(a << 1, a, topBit(a), (a & 0x08) != 0);
            case 0x0d: // SHR
                return doShift((a & wordMask()) >> 1, a, lowBit(a), false);
            case 0x0e: // SETMO
                return doSetmo();
            case 0x0f: // SAR
                return doShift(((a & wordMask()) >> 1) | topBit(topBit(a)), a, lowBit(a), false);
            case 0x10: // PASS
                //return doShift(a, a, false, false);
                return doPass(a);
            case 0x14: // DAA
            {
                const bool old_af = _auxiliary;
                const bool old_cf = _carry;
                t = a;
                auto adj = 0;

                // Extremely funky undefined OF behavior (from MartyPC)
                _overflow = (a <= 0x7f) && ((!old_cf && a >= 0x7A) || (old_cf && a >= 0x1A));

                if (old_af || (a & 0x0f) > 9) {
                    adj = 6;
                    t = a + adj;
                    //_overflow = topBit(t & (t ^ a));
                    _auxiliary = true;
                }
                if (_carry || a > (old_af ? 0x9fU : 0x99U)) {
                    adj = 0x60;
                    v = t + adj;
                    //_overflow = topBit(v & (v ^ t));
                    _carry = true;
                }
                else {
                    v = t;
                }
                //_overflow = (a ^ v) & (adj ^ v) & 0x80 != 0;
                doPZS(v);
                break;
            }
            case 0x15: // DAS
            {
                bool old_af = _auxiliary;
                t = a;
                auto adj = 0;
                if (old_af || (a & 0x0f) > 9) {
                    t = a - 6;
                    adj = 6;
                    //_overflow = topBit(a & (t ^ a));
                    _auxiliary = true;
                }
                if (_carry || a > (old_af ? 0x9fU : 0x99U)) {
                    v = t - 0x60;
                    adj = 0x60;
                    //_overflow = topBit(t & (v ^ t));
                    _carry = true;
                }
                else {
                    v = t;
                }
                // More undefined overflow flag fun!
                _overflow = ((a ^ adj) & (a ^ v) & 0x80) != 0;
                doPZS(v);
                break;
            }
            case 0x16: // AAA
                _carry = (_auxiliary || (a & 0xf) > 9);
                _auxiliary = _carry;
                v = a + (_carry ? 6 : 0);
                _overflow = topBit(v & (v ^ a));
                doPZS(v);
                return v & 0x0f;
            case 0x17: // AAS
                _carry = (_auxiliary || (a & 0xf) > 9);
                _auxiliary = _carry;
                v = a - (_carry ? 6 : 0);
                _overflow = topBit(a & (v ^ a));
                doPZS(v);
                return v & 0x0f;
            case 0x18: // INC
                v = a + 1;
                doPZS(v);
                _overflow = topBit((v ^ a) & (v ^ 1));
                _auxiliary = (((v ^ a ^ 1) & 0x10) != 0);
                break;
            case 0x19: // DEC
                v = a - 1;
                doPZS(v);
                _overflow = topBit((a ^ 1) & (v ^ a));
                _auxiliary = (((v ^ a ^ 1) & 0x10) != 0);
                break;
            case 0x1a: // COM1
                _carry = false;
                _overflow = false;
                return ~a;
            case 0x1b: // NEG
                return sub(0, a, false);
            case 0x1c: // INC2
                return a + 2; // flags never updated
            case 0x1d: // DEC2
                return a - 2; // flags never updated
        }
        return v;
    }

    void updateFlags() {
        flags() = (flags() & 0xf702)
            | (_overflow ? 0x800 : 0)
            | (_sign ? 0x80 : 0)
            | (_zero ? 0x40 : 0)
            | (_auxiliary ? 0x10 : 0)
            | _parity // Already shifted
            | (_carry ? 1 : 0);
    }

    uint32_t readSource() {
        uint32_t v;
        switch (_source) {
            case 7: // Q
                if (_queueBytes == 0) {
                    _state = stateWaitingForQueueData;
                    return 0;
                }
                return queueRead();
            case 8: // A (AL)
            case 9: // C (CL)? - not used
            case 10: // E (DL)? - not used
            case 11: // L (BL)? - not used
                return rb(_source & 3);
            case 16: // X (AH)
            case 17: // B (CH)? - not used
                return rb((_source & 3) + 4);
            case 18: // M
                if ((_group & groupMNotAccumulator) == 0) {
                    // This group decode ROM output forces M to use AL/AX.
                    return (_wordSize ? ax() : al());
                }
                if ((_group & groupEffectiveAddress) == 0) {
                    return rw();
                }
                return getMemOrReg(_mIsM);
            case 19: // R
                if ((_group & groupEffectiveAddress) == 0) {
                    // This opcode has no ModR/M, so R is encoded in the opcode.
                    // This is used by PUSH/POP sreg instructions 06, 0E, 16, & 1E
                    return sr((_opcode >> 3) & 7);
                }
                return getMemOrReg(!_mIsM);
            case 20: // SIGMA
                v = doALU();
                if (_updateFlags) {
                    updateFlags();
                }
                return v;
            case 22: // CR
                _wordSize = true; // HACK: not sure how this happens for INT0 on the hardware
                return _microcodePointer & 0xf;
        }
        return _registers[_source];
    }

    void writeDestination(uint32_t v) {
        switch (_destination) {
            case 8: // A (AL)
            case 9: // C (CL)? - not used
            case 10: // E (DL)? - not used
            case 11: // L (BL)? - not used
                rb(_destination & 3) = v;
                break;
            case 15: // F
                // Ensure our reserved flags are always set correctly
                flags() = (v & 0xFFD5) | 0xF002;
                break;
            case 16: // X (AH)
            case 17: // B (CH)? - not used
                rb((_destination & 3) + 4) = v;
                break;
            case 18: // M
                if (_alu == 7) {
                    // If ALU operation is CMP, we don't write back to M.
                    break;
                }
                if ((_group & groupMNotAccumulator) == 0) {
                    // This opcode is fixed to use AL or AX.
                    if (!_wordSize) {
                        al() = static_cast<uint8_t>(v);
                    }
                    else {
                        ax() = v;
                    }
                    break;
                }
                if ((_group & groupEffectiveAddress) == 0) {
                    // Opcode has no ModR/M
                    if ((_group & groupLoadRegisterImmediate) != 0 && (_opcode & 8) == 0) {
                        // Write to a byte register
                        rb(_opcode & 7) = v;
                    }
                    else if ((_group & groupWidthInOpcodeBit3) != 0 && ((_opcode & 8) != 0)) {
                        // Write to a byte register
                        rb(_opcode & 7) = v;
                    }
                    else {
                        // Write to a word register
                        rw() = v;
                    }
                }
                else {
                    // Opcode has a ModR/M
                    setMemOrReg(_mIsM, v);
                    _skipRNI = _mIsM && _useMemory;
                }
                break;
            case 19: // R
                if ((_group & groupEffectiveAddress) == 0) {
                    sr((_opcode >> 3) & 7) = v;
                }
                else {
                    setMemOrReg(!_mIsM, v);
                }
                break;
            case 20: // tmpaL
                tmpa() = (tmpa() & 0xff00) | (v & 0xff);
                break;
            case 21: // tmpbL - sign extend to tmpb
                tmpb() = ((v & 0x80) != 0 ? 0xff00 : 0) | (v & 0xff);
                break;
            case 22: // tmpaH
                tmpa() = (tmpa() & 0xff) | (v << 8);
                break;
            case 23: // tmpbH
                tmpb() = (tmpb() & 0xff) | (v << 8);
                break;
            default:
                if (_destination < 32) {
                    _registers[_destination] = v;
                }
                else {
                    std::cerr << "Unknown destination: " << _destination << std::endl;
                }

        }
    }

    void busAccessDone(const uint8_t high) {
        opr() |= high << 8;
        if ((_operands & 0x10) != 0) {
            _rni = true;
            _in_instruction = false;
        }
        switch (_operands & 3) {
            case 0: // Increment IND by 2
                ind() += 2;
                break;
            case 1: // Adjust IND according to word size and DF
                ind() += _wordSize ? (df() ? -2 : 2) : (df() ? -1 : 1);
                break;
            case 2: // Decrement IND by 2
                ind() -= 2;
                break;
            default:
                break;
        }
        _state = stateRunning;
    }

    void doSecondMisc() {
        switch (_operands & 7) {
            case 0: // RNI
                if (!_skipRNI) {
                    _rni = true;
                    _in_instruction = false;
                }
                break;
            case 1: // WB,NX
                if (!_mIsM || !_useMemory || _alu == 7) {
                    // Honor NX if M is R or ALU is CMP (no writeback)
                    _nx = true;
                }
                break;
            case 2: // CORR
                _state = stateWaitingForQueueIdle;
                break;
            case 3: // SUSP
                _prefetching = false;
                if (_busState != t4 && _busState != tIdle) {
                    _state = stateSuspending;
                    break;
                }
                _ioType = ioPassive;
                break;
            case 4: // RTN
                // Normal RTN updates carry?
                //std::cout << "Setting carry to " << _carry << std::endl;
                setCF(_carry);

                _microcodePointer = _microcodeReturn;
                _state = stateSingleCycleWait;
                break;
            case 5: // NX
                _nx = true;
                break;
            default:
                break;
        }
    }

    void startIO() {
        _state = stateIODelay1;
        if ((_busState == t3 || _busState == tWait) || canStartPrefetch()) {
            _state = stateIODelay2;
        }
        if (_busState == t4 || _busState == tIdle) {
            _ioType = ioPassive;
        }
        _ioRequested = true;
    }

    void doSecondHalf() {
        switch (_type) {
            case 0: // short jump
                if (!condition(_operands >> 4))
                    break;
                _microcodePointer =
                    (_microcodePointer & 0x1ff0) + (_operands & 0xf);
                _state = stateSingleCycleWait;
                break;
            case 1: // precondition ALU
                _alu = _operands >> 3;
                // Preconditioning the ALU unlocks the carry latch.
                _carryLatch = true;
                _nx = lowBit(_operands);
                if (_mIsM && _useMemory && _alu != 7 && (_group & groupEffectiveAddress) != 0) {
                    _nx = false;
                }
                _aluInput = (_operands >> 1) & 3;
                if (_alu == 0x11) {
                    // XI
                    readFlags();
                    _alu = ((((_opcode & 0x80) != 0 ? _modRM : _opcode) >> 3) & 7) |
                        ((_opcode >> 3) & 8) |
                        ((_group & groupAddSubBooleanRotate) != 0 ? 0 : 0x10);
                }
                break;
            case 4:
                switch ((_operands >> 3) & 0x0f) {
                    case 0: // MAXC
                        _counter = _wordSize ? 15 : 7;
                        break;
                    case 1: // FLUSH
                        _queueBytes = 0;
                        _queue = 0;
                        _newQueue.clear();
                        _snifferDecoder.queueOperation(2);
                        _queueFlushing = true;
                        break;
                    case 2: // CF1
                        _f1 = !_f1;
                        break;
                    case 3: // CITF
                        setIF(false);
                        flags() &= ~0x100;
                        break;
                    case 4: // RCY
                        _carry = false;
                        _carryLatch = false;
                        break;
                    case 6: // CCOF
                        _carry = false;
                        setCF(false);
                        setOF(false);
                        break;
                    case 7: // SCOF
                        _carry = true;
                        setCF(true);
                        setOF(true);
                        break;
                    case 8: // WAIT
                        // Don't know what this does!
                        break;
                }
                doSecondMisc();
                break;
            case 6:
                startIO();
                break;
            case 5: // long jump or call
            case 7:
            {
                int mc_ptr =
                (((_microcodeIndex[_microcodePointer >> 2] << 2) +
                    (_microcodePointer & 3)) << 2) >> 2;

                if (mc_ptr == 0x1c5) {
                    std::cout << "INT0: CF is " << (flags() & 1) << std::endl;
                }

                if (!condition(_operands >> 4)) {
                    break;
                }
                _skipRNI = false;
                if (_type == 7) {
                    _microcodeReturn = _microcodePointer;
                }
                _microcodePointer = _translation[
                    ((_type & 2) << 6) +
                    ((_operands << 3) & 0x78) +
                    ((_group & groupInitialEARead) == 0 ? 4 : 0) +
                    ((_modRM & 0xc0) == 0 ? 1 : 0)] >> 1;

                // int mc_ptr_dst =
                // (((_microcodeIndex[_microcodePointer >> 2] << 2) +
                //     (_microcodePointer & 3)) << 2) >> 2;
                //std::cout << std::format("Long jump/call from {:03X} to {:03X}\n", mc_ptr, mc_ptr_dst);
                _state = stateSingleCycleWait;
                break;
            }
            default:
                break;
        }
    }

    void busStart() {
        bool memory = (_group & groupMemory) != 0;
        switch ((_operands >> 5) & 3) {
            case 0:
                _ioType = memory ? ioReadMemory : ioReadPort;
                break;
            case 1:
                _ioType = ioInterruptAcknowledge;
                break;
            case 2:
                _ioType = memory ? ioWriteMemory : ioWritePort;
                break;
            case 3:
                _ioType = ioHalt;
                break;
            default:
                break;
        }
    }


    // Main microcode execution function. Represents one cycle of the Execution Unit (EU).
    void executeMicrocode() {
        uint8_t* m;
        uint32_t v;

        // Sanity check of newQueue implementation
        _newQueue.check(_queue);

        switch (_state) {
            case stateRunning:
                _lastMicrocodePointer = _microcodePointer;
                m = &_microcode[
                    ((_microcodeIndex[_microcodePointer >> 2] << 2) +
                        (_microcodePointer & 3)) << 2];
                advanceMicrocodePointer();
                _destination = m[0];
                _source = m[1];
                // Mask off the F bit from the type field.
                _type = m[2] & 7;
                // Unpack the F bit from the type field.
                _updateFlags = ((m[2] & 8) != 0);
                _operands = m[3];
                v = readSource();
                if (_state == stateWaitingForQueueData) {
                    // Empty queue prevents further execution.
                    break;
                }
                writeDestination(v);
                doSecondHalf();
                break;

            case stateWaitingForQueueData:
                if (_queueBytes == 0) {
                    // Still no data. Try again next cycle.
                    break;
                }
                // We have a byte in the queue, so resume running.
                _state = stateRunning;
                // We can read the source now, so we can read it and immediately write back.
                writeDestination(readSource());
                // Process the operands part of the microcode word.
                doSecondHalf();
                break;

            case stateSuspending:
                if (_busState != t4) {
                    // Wait until any pending bus cycle is complete.
                    break;
                }
                _state = stateRunning;
                break;

            case stateWaitingForQueueIdle:
                // This state is used by the CORR microcode operation.
                if (_ioType != ioPassive || _busState == t4 || _t4) {
                    break;
                }
                pc() -= _queueBytes;
                _queueBytes = 0; // so that realIP() is correct
                _state = stateRunning;
                break;

            case stateIODelay2:
                if (!_ready && (_busState == t3 || _busState == tWait)) {
                    break;
                }
                _state = stateIODelay1;
                break;

            case stateIODelay1:
                if (_busState == t4) {
                    break;
                }
                _state = stateWaitingUntilFirstByteCanStart;
                break;

            case stateWaitingUntilFirstByteCanStart:
                if (_ioType != ioPassive) {
                    // Bus is busy
                    break;
                }
                _ioWriteData = opr() & 0xff;
                _ioSegment = (_operands >> 2) & 3;
                if (_ioSegment == 3) {
                    // Use the segment override if present
                    _ioSegment = _segmentOverride != -1 ? _segmentOverride : _segment;
                }
                else {
                    // This is a no-segment access (IVT read or I/O).
                    // We use register slot 9 because it's a register slot that stays as all-zero bits, and has the
                    // same low two bits (so that the logs show the right segment).
                    if (_ioSegment == 1) {
                        _ioSegment = 9;
                    }
                }
                _ioIndex = ind();
                _ioAddress = physicalAddress(_ioSegment, _ioIndex);

                _state = stateWaitingUntilFirstByteDone;
                busStart();
                break;

            case stateWaitingUntilFirstByteDone:
                if (!_wordSize) {
                    // 8-bit access, disable BIU request flag as we will complete this bus cycle.
                    _ioRequested = false;
                }
                if (_ioType != ioPassive) {
                    // Bus is busy
                    break;
                }
                // Perform read or write
                _ioWriteData = opr() >> 8;
                opr() = _ioReadData;

                if (!_wordSize) {
                    // 8-bit access - all done this state.
                    // When reading from the bus, the high byte is set to 0xFF on byte access.

                    //busAccessDone(0xFF);
                    busAccessDone(0x00);
                }
                else {
                    // 16-bit access - increment the address, start the second byte.
                    _ioIndex = ind() + 1;
                    _ioAddress = physicalAddress(_ioSegment, _ioIndex);
                    _state = stateWaitingUntilSecondByteDone;
                    busStart();
                }
                break;

            case stateWaitingUntilSecondByteDone:
                // Second half of a word-size transfer.
                _ioRequested = false;
                if (_ioType != ioPassive) {
                    // Bus is busy
                    break;
                }
                busAccessDone(_ioReadData);
                break;

            case stateSingleCycleWait:
                _state = stateRunning;
                break;

            case stateHaltingStart:
                _prefetching = false;
                _state = stateHalting3;
                if (_ioType != ioPassive)
                    break;
                _state = stateHalting2;
                break;

            case stateHalting3:
                if (_ioType != ioPassive)
                    break;
                _state = stateHalting2;
                break;

            case stateHalting2:
                _snifferDecoder.setStatus(static_cast<int>(ioHalt));
                _state = stateHalting1;
                break;

            case stateHalting1:
                _state = stateHalted;
                break;

            case stateHalted:
                // The CPU is halted!
                _snifferDecoder.setStatus(static_cast<int>(ioPassive));
                // Stay halted until an interrupt occurs
                if (!interruptPending()) {
                    break;
                }
                // Try to account for a mysterious extra halt cycle
                if (_extraHaltDelay) {
                    _extraHaltDelay = false;
                    break;
                }
                // Waking up now - read the next instruction
                _rni = true;
                _in_instruction = false;
                _state = stateRunning;
                break;
        }
    }

    void setNextMicrocode(const int nextState, const int nextMicrocode) {
        _nextMicrocodePointer = nextMicrocode;
        _loaderState = nextState | 1;
        _nextGroup = _groups[nextMicrocode >> 4];
    }

    // Handles reading the next opcode byte.
    // The 8088 processes interrupts, traps and NMI at this point, so any of these states will prevent normal opcode
    // fetching.
    void readOpcode(const int nextState) {
        // Priority of servicing is NMI, interrupt, trap flag.
        // See https://www.righto.com/2023/02/8086-interrupt.html
        if (_nmiRequested) {
            _nmiRequested = false;
            setNextMicrocode(nextState, 0x1001);
            return;
        }
        if (interruptPending()) {
            setNextMicrocode(nextState, 0x1002);
            return;
        }
        if ((flags() & 0x100) != 0) {
            // Trap flag is set.
            setNextMicrocode(nextState, 0x1000);
            return;
        }

        // No interrupt conditions, so load the next opcode byte from the prefetch queue.
        if (_queueBytes != 0) {
            if (!_in_instruction) {
                // Starting a new instruction
                _inst_boundary = true;
                _in_instruction = true;
            }
            QueueEntry qe = {};
            _newQueue.peekEntry(qe);
            _inst_address = qe.address;
            setNextMicrocode(nextState, queueRead() << 4);
            _snifferDecoder.queueOperation(1);
            return;
        }
        _loaderState = nextState & 2;
    }

    bool canStartPrefetch() {
        if (!_prefetching) // prefetching turned off for a jump?
            return false;
        if (_ioRequested || _ioType != ioPassive || _t4) // bus busy?
            return false;
        if (_queueFilled)
            return false;
        return true;
    }

    enum BusState
    {
        t1,
        t2,
        t3,
        tWait,
        t4,
        tIdle,
    };

    // Attempt to complete the current bus cycle. If the bus is not ready, return tWait to indicate we need to continue
    // waiting. Otherwise, complete the bus operation and return t4 to indicate the cycle is done.
    BusState completeIO(const bool write) {
        if (!_ready) {
            return tWait;
        }
        if (!write) {
            // Perform a bus read
            _ioReadData = _bus.read();
            _snifferDecoder.setData(_ioReadData);
        }
        else {
            // Perform a bus write
            _ioReadData = _ioWriteData;
        }
        _bus.setPassiveOrHalt(true);
        _snifferDecoder.setStatus(static_cast<int>(ioPassive));
        _lastIOType = _ioType;
        _ioType = ioPassive;
        return t4;
    }

    // Execute one CPU cycle.
    void simulateCycle() {
        BusState nextState = _busState;
        const bool write = _ioType == ioWriteMemory || _ioType == ioWritePort;
        _t6 = _t5;
        _t5 = _t4;
        _t4 = false;
        _ready = _bus.ready();
        bool prefetchCompleting = false;
        switch (_busState) {
            case t1:
                _snifferDecoder.setAddress(_ioAddress);
                _bus.startAccess(_ioAddress, static_cast<int>(_ioType));
                nextState = t2;
                break;
            case t2:
                _snifferDecoder.setStatusHigh(_ioSegment);
                _snifferDecoder.setBusOperation(static_cast<int>(_ioType));
                if (write) {
                    _snifferDecoder.setData(_ioWriteData);
                }
                if (_ioType == ioInterruptAcknowledge) {
                    _bus.setLock(_state == stateWaitingUntilFirstByteDone);
                }
                nextState = t3;
                break;
            case t3:
                // This is a check for the prefetch delay policy.
                if (_ioType == ioPrefetch && _queueBytes == 3 && !_dequeueing) {
                    _queueFilled = true;
                    //_cyclesUntilCanLowerQueueFilled = 3;
                }
                nextState = completeIO(write);
                break;
            case tWait:
                nextState = completeIO(write);
                break;
            case t4:
                if (_lastIOType == ioWriteMemory || _lastIOType == ioWritePort) {
                    _bus.write(_ioReadData);
                }
                if (_lastIOType == ioPrefetch) {
                    prefetchCompleting = true;
                }
                nextState = tIdle;
                _t4 = true;
                break;
        }
        if (_dequeueing) {
            _dequeueing = false;
            _queue >>= 8;
            --_queueBytes;
            // Don't need to adjust newQueue here as we did that when popping.
        }
        //if (_cyclesUntilCanLowerQueueFilled == 0 || (_cyclesUntilCanLowerQueueFilled == 1 && _queueBytes < 3)) {
        //_cyclesUntilCanLowerQueueFilled = 0;
        if ((_queueBytes < 3 || (_busState == tIdle && (_lastIOType != ioPrefetch || (!_t4 && !_t5))))) {
            if (_busState == tIdle && !(_t4 && _lastIOType == ioPrefetch) && _queueBytes < 4) {
                _queueFilled = false;
            }
        }
        //}
        //else
        //    --_cyclesUntilCanLowerQueueFilled;

        // We can execute microcode in loader states 2 & 3.
        if ((_loaderState & 2) != 0) {
            executeMicrocode();
        }

        // Handle LOCK prefix.
        if (_locking) {
            _locking = false;
            _lock = true;
            _bus.setLock(true);
        }

        // Handle the instruction loader state machine.
        switch (_loaderState) {
            case 0:
                readOpcode(0);
                break;
            case 1:
            case 3:
                if ((_group & groupNonPrefix) != 0) {
                    // A non-prefix byte was loaded.
                    // Clear any segment override, REP/REPE/REPNE, and LOCK state.
                    _segmentOverride = -1;
                    _f1 = false;
                    _repne = false;
                    // Clear any previous lock condition.
                    if (_lock) {
                        _lock = false;
                        _bus.setLock(false);
                    }
                }
                if ((_nextGroup & groupMicrocoded) == 0) {
                    // 1BL
                    startNonMicrocodeInstruction();
                }
                else {
                    if ((_nextGroup & groupEffectiveAddress) == 0) {
                        // SC
                        startMicrocodeInstruction();
                    }
                    else {
                        _loaderState = 1;
                        if (_queueBytes != 0) {
                            // SC
                            _nextModRM = queueRead();
                            startMicrocodeInstruction();
                        }
                    }
                }
                break;
            case 2:
                if (_rni) {
                    readOpcode(0);
                }
                else if (_nx) {
                    readOpcode(2);
                }
                break;
            default:
                break;
        }

        // If we just completed a prefetch, add the byte to the queue.
        if (prefetchCompleting) {
            _newQueue.push_word(static_cast<WordT>(_ioReadData), pc());
            _queue |= (_ioReadData << (_queueBytes << 3));
            ++_queueBytes;
            ++pc();
        }

        if (nextState == tIdle && _ioType != ioPassive) {
            nextState = t1;
            if (_ioType == ioPrefetch) {
                _ioIndex = pc();
                _ioAddress = physicalAddress(_ioSegment, pc());
            }

            _bus.setPassiveOrHalt(_ioType == ioHalt);
            _snifferDecoder.setStatus(static_cast<int>(_ioType));
        }
        if (canStartPrefetch()) {
            _ioType = ioPrefetch;
            _ioSegment = 1;
        }
        if (_queueFlushing) {
            _queueFlushing = false;
            _prefetching = true;
        }
        // If cycle logging is enabled we want to capture logs regardless of the configured end cycle.
        if (_cycleLogging && _cycle < _logEndCycle) {
            _snifferDecoder.setAEN(_bus.getAEN());
            _snifferDecoder.setDMA(_bus.getDMA());
            _snifferDecoder.setPITBits(_bus.pitBits());
            _snifferDecoder.setBusOperation(_bus.getBusOperation());
            _snifferDecoder.setInterruptFlag(intf());
            if (_bus.getDMAS3()) {
                _savedAddress = _ioAddress;
                _snifferDecoder.setAddress(_bus.getDMAAddress());
            }
            else {
                if (_bus.getDMADelayedT2()) {
                    _snifferDecoder.setAddress(_savedAddress);
                    //_snifferDecoder.setStatus((int)_ioType);
                    _snifferDecoder.setBusOperation(static_cast<int>(_ioType));
                }
            }
            _snifferDecoder.setReady(_ready);
            _snifferDecoder.setLock(_lock);
            _snifferDecoder.setDMAS(_bus.getDMAS());
            _snifferDecoder.setIRQs(_bus.getIRQLines());
            _snifferDecoder.setINT(_bus.interruptPending());
            //_snifferDecoder.setCGA(_bus.getCGA());

            std::string l = _bus.snifferExtra() + _snifferDecoder.getLine();
            l = pad(l, 103) + microcodeString();
            if (_cycle >= _logStartCycle) {
                // Always respect console logging
                if (_consoleLogging) {
                    std::cout << l << std::endl;
                }
                // Also append into the ring-buffer when cycle logging is enabled
                if (_cycleLogging) {
                    _logBuffer.push_back(l);
                    if (_logBuffer.size() > _logCapacity) {
                        _logBuffer.pop_front();
                    }
                }
            }
        }
        _busState = nextState;
        ++_cycle;
        _interruptPending = _bus.interruptPending();
        _bus.tick();
    }

    static std::string pad(const std::string& s, int n) {
        return s + std::string(std::max(0, n - static_cast<int>(s.length())), ' ');
    }

    std::string microcodeString() {
        if (_lastMicrocodePointer == -1) {
            return "";
        }

        static const char* regNames[] = {
            "RA", // ES
            "RC", // CS
            "RS", // SS - presumably, to fit pattern. Only used in RESET
            "RD", // DS
            "PC",
            "IND",
            "OPR",
            "no dest", // as dest only - source is Q
            "A", // AL
            "C", // CL? - not used
            "E", // DL? - not used
            "L", // BL? - not used
            "tmpa",
            "tmpb",
            "tmpc",
            "F", // flags register
            "X", // AH
            "B", // CH? - not used
            "M",
            "R", // source specified by modrm and direction, destination specified by r field of modrm
            "tmpaL", // as dest only - source is SIGNA
            "tmpbL", // as dest only - source is ONES
            "tmpaH", // as dest only - source is CR
            "tmpbH", // as dest only - source is ZERO
            "XA", // AX
            "BC", // CX
            "DE", // DX
            "HL", // BX
            "SP", // SP
            "MP", // BP
            "IJ", // SI
            "IK", // DI
        };
        static const char* condNames[] = {
            "F1ZZ",
            "MOD1", // jump if short offset in effective address
            "L8  ", // jump if short immediate (skip 2nd uint8_t from Q)
            "Z   ", // jump if zero (used in IMULCOF/MULCOF)
            "NCZ ",
            "TEST", // jump if overflow flag is set
            "OF  ", // jump if -TEST pin not asserted
            "CY  ",
            "UNC ",
            "NF1 ",
            "NZ  ", // jump if not zero (used in JCXZ and LOOP)
            "X0  ", // jump if bit 3 of opcode is 1
            "NCY ",
            "F1  ",
            "INT ", // jump if interrupt is pending
            "XC  ", // jump if condition based on low 4 bits of opcode
        };
        static const char* destNames[] = {
            "FARCALL ",
            "NEARCALL",
            "RELJMP  ",
            "EAOFFSET",
            "EAFINISH",
            "FARCALL2",
            "INTR    ",
            "INT0    ",
            "RPTI    ",
            "AAEND   ",
            "FARRET  ",
            "RPTS    ",
            "CORX    ", // unsigned multiply tmpc and tmpb, result in tmpa:tmpc
            "CORD    ", // unsigned divide tmpa:tmpc by tmpb, quotient in ~tmpc, remainder in tmpa
            "PREIMUL ", // abs tmpc and tmpb, invert F1 if product negative
            "NEGATE  ", // negate product tmpa:tmpc
            "IMULCOF ",
            // clear carry and overflow flags if product of signed multiply fits in low part, otherwise set them
            "MULCOF  ",
            // clear carry and overflow flags if product of unsigned multiply fits in low part, otherwise set them
            "PREIDIV ", // abs tmpa:tmpc and tmpb, invert F1 if one or the other but not both were negative
            "POSTIDIV", // negate ~tmpc if F1 set
        };

        int mcIndex =
        ((_microcodeIndex[_lastMicrocodePointer >> 2] << 2) +
            (_lastMicrocodePointer & 3)) << 2;
        int mcLineNumber = mcIndex >> 2;
        uint8_t* m = &_microcode[mcIndex];
        int d = m[0];
        int s = m[1];
        int t = m[2] & 7;
        bool f = ((m[2] & 8) != 0);
        int o = m[3];

        std::string r;
        r += std::format("{:03X}: ", mcLineNumber);

        if (d == 0 && s == 0 && t == 0 && !f && o == 0) {
            r += "null instruction executed!";
            s = 0x15;
            d = 0x07;
            t = 4;
            o = 0xfe;
        }
        if (s == 0x15 && d == 0x07) // "ONES  -> Q" used as no-op move
            r += "                ";
        else {
            const char* source = regNames[s];
            switch (s) {
                case 0x07:
                    source = "Q";
                    break;
                case 0x14:
                    source = "SIGMA";
                    break;
                case 0x15:
                    source = "ONES";
                    break;
                case 0x16:
                    source = "CR";
                    break;
                // low 3 bits of microcode address Counting Register + 1? Used as interrupt number at 0x198 (1), 0x199 (2), 0x1a7 (0), 0x1af (4), and 0x1b2 (3)
                case 0x17:
                    source = "ZERO";
                    break;
                default:
                    break;
            }
            r += pad(std::string(source), 5) + " -> " + pad(std::string(regNames[d]), 7);
        }
        r += "   ";
        if ((o & 0x7f) == 0x7f) {
            r += "                  ";
            t = -1;
        }
        else
            r += decimal(t) + "   ";
        switch (t) {
            // TYP bits
            case 0:
            case 5:
            case 7:
                r += condNames[(o >> 4) & 0x0f] + std::string("  ");
                if (t == 5)
                    r += destNames[o & 0xf];
                else {
                    if (t == 7)
                        r += destNames[10 + (o & 0xf)];
                    else {
                        std::string s = decimal(o & 0xf);
                        r += pad(s, 4) + "    ";
                    }
                }
                break;
            case 4:
                switch ((o >> 3) & 0x0f) {
                    case 0x00:
                        r += "MAXC ";
                        break;
                    case 0x01:
                        r += "FLUSH";
                        break;
                    case 0x02:
                        r += "CF1  ";
                        break;
                    case 0x03:
                        r += "CITF ";
                        break; // clear interrupt and trap flags
                    case 0x04:
                        r += "RCY  ";
                        break; // reset carry
                    case 0x06:
                        r += "CCOF ";
                        break; // clear carry and overflow
                    case 0x07:
                        r += "SCOF ";
                        break; // set carry and overflow
                    case 0x08:
                        r += "WAIT ";
                        break; // not sure what this does
                    case 0x0f:
                        r += "none ";
                        break;
                    default:
                        break;
                }
                r += " ";
                switch (o & 7) {
                    case 0:
                        r += "RNI     ";
                        break;
                    case 1:
                        r += "WB,NX   ";
                        break;
                    case 2:
                        r += "CORR    ";
                        break;
                    case 3:
                        r += "SUSP    ";
                        break;
                    case 4:
                        r += "RTN     ";
                        break;
                    case 5:
                        r += "NX      ";
                        break;
                    case 7:
                        r += "none    ";
                        break;
                    default:
                        break;
                }
                break;
            case 1:
                switch ((o >> 3) & 0x1f) {
                    case 0x00:
                        r += "ADD ";
                        break;
                    case 0x02:
                        r += "ADC ";
                        break;
                    case 0x04:
                        r += "AND ";
                        break;
                    case 0x05:
                        r += "SUBT";
                        break;
                    case 0x0a:
                        r += "LRCY";
                        break;
                    case 0x0b:
                        r += "RRCY";
                        break;
                    case 0x10:
                        r += "PASS";
                        break;
                    case 0x11:
                        r += "XI  ";
                        break;
                    case 0x18:
                        r += "INC ";
                        break;
                    case 0x19:
                        r += "DEC ";
                        break;
                    case 0x1a:
                        r += "COM1";
                        break;
                    case 0x1b:
                        r += "NEG ";
                        break;
                    case 0x1c:
                        r += "INC2";
                        break;
                    case 0x1d:
                        r += "DEC2";
                        break;
                    default:
                        break;
                }
                r += "  ";
                switch (o & 7) {
                    case 0:
                        r += "tmpa    ";
                        break;
                    case 1:
                        r += "tmpa, NX";
                        break;
                    case 2:
                        r += "tmpb    ";
                        break;
                    case 3:
                        r += "tmpb, NX";
                        break;
                    case 4:
                        r += "tmpc    ";
                        break;
                    default:
                        break;
                }
                break;
            case 6:
                switch ((o >> 4) & 7) {
                    case 0:
                        r += "R    ";
                        break;
                    case 2:
                        r += "IRQ  ";
                        break;
                    case 4:
                        r += "w    ";
                        break;
                    case 5:
                        r += "W,RNI";
                        break;
                    default:
                        break;
                }
                r += " ";
                switch ((o >> 2) & 3) {
                    // Bits 0 and 1 are segment
                    case 0:
                        r += "DA,";
                        break; // ES
                    case 1:
                        r += "D0,";
                        break; // segment 0
                    case 2:
                        r += "DS,";
                        break; // SS
                    case 3:
                        r += "DD,";
                        break; // DS
                    default:
                        break;
                }
                switch (o & 3) {
                    // bits 2 and 3 are IND update
                    case 0:
                        r += "P2";
                        break; // Increment IND by 2
                    case 1:
                        r += "BL";
                        break; // Adjust IND according to word size and DF
                    case 2:
                        r += "M2";
                        break; // Decrement IND by 2
                    case 3:
                        r += "P0";
                        break; // Don't adjust IND
                    default:
                        break;
                }
                r += "   ";
                break;
            default:
                break;
        }
        r += " ";
        if (f)
            r += "F";
        else
            r += " ";
        r += "  ";
        for (int i = 0; i < 13; ++i) {
            if ((_lastMicrocodePointer & (1 << (12 - i))) != 0)
                r += "1";
            else
                r += "0";
            if (i == 8)
                r += ".";
        }
        _lastMicrocodePointer = -1;

        r += " alu_CF: " + std::to_string(getAluCF() ? 1 : 0) + " CF: " + std::to_string(cf() ? 1 : 0);

        r += std::format(" tA: {:04X} tB: {:04X} tC: {:04X}", _registers[12], _registers[13], _registers[14]);

        r += std::format(" wordSize: {}", _wordSize ? "16" : "8");
        return r;
    }

    // Evaluate a microcode condition field. Usually these are used to determine whether to take a jump/call.
    bool condition(const int n) {
        switch (n) {
            case 0x00: // F1ZZ - compare F1 to ZF
                if ((_group & groupF1ZZFromPrefix) != 0) {
                    return zf() == (_f1 && _repne);
                }
                return zf() != lowBit(_opcode);
            case 0x01: // MOD1 - If one byte displacement (skips second queue read).
                return (_modRM & 0x40) != 0;
            case 0x02: // L8 - not sure if there's a better way to compute this
                if ((_group & groupLoadRegisterImmediate) != 0) {
                    return (_opcode & 8) == 0;
                }
                return !lowBit(_opcode) || (_opcode & 6) == 2;
            case 0x03: // Z - if zero flag set
                return _superZero;
            case 0x04: // NCZ - Decrement count register, true if not zero
                --_counter;
                return _counter != -1;
            case 0x05: // TEST - no 8087 emulated yet
                return true;
            case 0x06: // OF - if overflow flag set
                return of(); // only used in INTO
            case 0x07: // CY - if carry set
                return _carry;
            case 0x08: // UNC - Unconditional. Always true.
                return true;
            case 0x09: // NF1 - if F1 flag not set
                return !_f1;
            case 0x0a: // NZ - if zero flag not set
                return !_superZero;
            case 0x0b: // X0 - if bit 3 of opcode/extension is set
                if ((_group & groupMicrocodePointerFromOpcode) == 0) {
                    // Extension; use ModR/M instead of opcode.
                    return (_modRM & 8) != 0;
                }
                // Non-extension; use opcode.
                return (_opcode & 8) != 0;
            case 0x0c: // NCY - if carry not set
                return !_carry;
            case 0x0d: // F1 - if F1 flag is set (REP prefix or signed multiply/divide)
                return _f1;
            case 0x0e: // INT - if interrupt pending
                return interruptPending();
            default:
                break;
        }
        bool jump = false; // XC
        switch (_opcode & 0x0e) {
            case 0x00:
                jump = of();
                break; // O
            case 0x02:
                jump = cf();
                break; // C
            case 0x04:
                jump = zf();
                break; // Z
            case 0x06:
                jump = cf() || zf();
                break; // BE
            case 0x08:
                jump = sf();
                break; // S
            case 0x0a:
                jump = pf();
                break; // P
            case 0x0c:
                jump = (sf() != of());
                break; // L
            case 0x0e:
                jump = (sf() != of()) || zf();
                break; // LE
            default:
                break;
        }
        if (lowBit(_opcode)) {
            jump = !jump;
        }
        return jump;
    }

    bool interruptPending() {
        return _nmiRequested || (intf() && _interruptPending);
    }

    uint16_t wordMask() const { return _wordSize ? 0xffff : 0xff; }

    void doPZS(const uint16_t v) {
        static uint8_t table[0x100] = {
            4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
            0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
            0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
            4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
            0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
            4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
            4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
            0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
            0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
            4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
            4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
            0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
            4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
            0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
            0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
            4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4};
        _parity = table[v & 0xff];
        _zero = ((v & wordMask()) == 0);
        _superZero = (v == 0);
        _sign = topBit(v);
    }

    void doFlags(const uint32_t result, const bool of, const bool af) {
        doPZS(result);
        _overflow = of;
        _auxiliary = af;
    }

    uint16_t bitwise(const uint16_t data) {
        doFlags(data, false, false);
        _carry = false;
        return data;
    }

    void doAddSubFlags(const uint32_t result, const uint32_t x, const bool of, const bool af) {
        doFlags(result, of, af);
        setAluCF(((result ^ x) & (_wordSize ? 0x10000 : 0x100)) != 0);
    }

    static bool lowBit(uint32_t v) { return (v & 1) != 0; }
    bool topBit(const int w) const { return (w & (_wordSize ? 0x8000 : 0x80)) != 0; }
    bool topBit(const uint32_t w) const { return (w & (_wordSize ? 0x8000 : 0x80)) != 0; }
    uint16_t topBit(const bool v) const { return v ? (_wordSize ? 0x8000 : 0x80) : 0; }

    uint16_t add(const uint32_t a, const uint32_t b, const bool c) {
        const uint32_t r = a + b + (c ? 1 : 0);
        doAddSubFlags(r, a ^ b, topBit((r ^ a) & (r ^ b)), ((a ^ b ^ r) & 0x10) != 0);
        return r;
    }

    uint16_t sub(const uint32_t a, const uint32_t b, const bool c) {
        const uint32_t r = a - (b + (c ? 1 : 0));
        doAddSubFlags(r, a ^ b, topBit((a ^ b) & (r ^ a)), ((a ^ b ^ r) & 0x10) != 0);
        return r;
    }

    // Calculate physical address from segment and offset, masking to 20 bits
    uint32_t physicalAddress(const int segment, const uint16_t offset) const {
        return ((_registers[segment] << 4) + offset) & 0xFFFFF;
    }

    std::string _log;
    // Ring buffer of recent log lines for cycle logging
    std::deque<std::string> _logBuffer;
    size_t _logCapacity = 10000; // default capacity (lines)
    bool _cycleLogging = false; // enabled via GUI
    BusType _bus;

public:
    // Cycle log API
    void setCycleLogging(bool v) { _cycleLogging = v; }
    bool isCycleLogging() const { return _cycleLogging; }
    void clearCycleLog() { _logBuffer.clear(); }

    void setCycleLogCapacity(size_t c) {
        _logCapacity = c;
        while (_logBuffer.size() > _logCapacity)
            _logBuffer.pop_front();
    }

    const std::deque<std::string>& getCycleLogBuffer() const { return _logBuffer; }
    size_t getCycleLogSize() const { return _logBuffer.size(); }
    size_t getCycleLogCapacity() const { return _logCapacity; }
    // Append a single line directly into the cycle log buffer (for diagnostics/UI)
    void appendCycleLogLine(const std::string& line) {
        _logBuffer.push_back(line);
        if (_logBuffer.size() > _logCapacity) {
            _logBuffer.pop_front();
        }
    }

    std::string getQueueString() const {
        return _newQueue.getQueueString();
        // // Return the bytes in the queue as hex bytes from index 0 .. _queueBytes-1
        // if (_queueBytes <= 0)
        //     return std::string();
        // std::string out;
        // for (int i = 0; i < _queueBytes; ++i) {
        //     const uint8_t b = static_cast<uint8_t>((_queue >> (i * 8)) & 0xFF);
        //     if (!out.empty())
        //         out += ' ';
        //     out += std::format("{:02X}", static_cast<unsigned>(b));
        // }
        // return out;
    }

    std::string getQueueDebugString() const {
        const auto debug = _newQueue.getDebug();
        std::ostringstream oss;
        for (const auto& entry : debug) {
            oss << std::format("{:02X}[{:04X}] ", static_cast<unsigned>(entry.data), entry.address);
        }
        return oss.str();
    }

private:
    class InstructionQueue
    {
    public:
        typedef std::size_t size_type;

        InstructionQueue() :
            head_(0)
            , tail_(0)
            , size_(0) {
        }

        void clear() {
            head_ = 0;
            tail_ = 0;
            size_ = 0;
        }

        size_type size() const {
            return size_;
        }

        static size_type capacity() {
            return QueueLen;
        }

        bool isEmpty() const {
            return size_ == 0;
        }

        bool full() const {
            return size_ == QueueLen;
        }

        size_type freeSpace() const {
            return QueueLen - size_;
        }

        bool hasRoom() const {
            return freeSpace() >= sizeof(WordT);
        }

        // Push a single entry (byte + address). Returns false if full.
        bool push(uint8_t data, uint16_t address) {
            if (full()) {
                return false;
            }
            buffer_[head_].data = data;
            buffer_[head_].address = address;
            advanceHead();
            return true;
        }

        // Pop oldest entry. Returns false if empty.
        bool popEntry(QueueEntry& out) {
            if (isEmpty()) {
                return false;
            }
            out = buffer_[tail_];
            advanceTail();
            return true;
        }

        bool pop(uint8_t& out) {
            if (isEmpty()) {
                return false;
            }
            out = buffer_[tail_].data;
            advanceTail();
            return true;
        }

        // Peek the oldest entry without removing it. Returns false if empty.
        bool peekEntry(QueueEntry& out) const {
            if (isEmpty()) {
                return false;
            }
            out = buffer_[tail_];
            return true;
        }

        // Push a "native word".
        // For uint8_t: pushes one byte at address.
        // For uint16_t: pushes two bytes (little-endian) at address, address+1.
        // Returns false if not enough room for all bytes.
        bool push_word(WordT word, const uint16_t address) {
            if (const size_type bytes_needed = sizeof(WordT); freeSpace() < bytes_needed) {
                return false;
            }

            if (std::is_same<WordT, uint8_t>::value) {
                uint8_t b = static_cast<uint8_t>(word);
                return push(b, address);
            }

            const uint16_t w = static_cast<uint16_t>(word);
            const uint8_t lo = static_cast<uint8_t>(w & 0x00FFu);
            const uint8_t hi = static_cast<uint8_t>((w >> 8) & 0x00FFu);

            // We already know we have room.
            push(lo, address);
            push(hi, address + 1);
            return true;
        }

        std::string getQueueString() const {
            std::ostringstream oss;

            // Print each byte from oldest (tail) to newest (head)
            size_type index = tail_;
            for (size_type count = 0; count < size_; ++count) {
                const uint8_t b = buffer_[index].data;

                // Two-digit upper-case hex
                oss << std::uppercase
                    << std::hex
                    << std::setw(2)
                    << std::setfill('0')
                    << static_cast<unsigned>(b);

                index = (index + 1) % QueueLen;
            }

            return oss.str();
        }

        std::vector<QueueEntry> getDebug() const {
            std::vector<QueueEntry> out;
            out.reserve(size_); // avoid reallocations

            size_type index = tail_;
            for (size_type count = 0; count < size_; ++count) {
                out.push_back(buffer_[index]);
                index = (index + 1) % QueueLen;
            }

            return out;
        }

        bool isAtPolicyLen() const {
            const size_type sz = size_;
            const size_type cap = QueueLen;

            if (std::is_same<WordT, uint8_t>::value) {
                // 8088: queue is "almost full" when it has capacity-1 bytes
                return (sz == cap - 1);
            }
            else {
                // 8086: queue is "almost full" when size is cap-1 or cap-2
                // (since fetch requires 2-byte room)
                return (sz == cap - 1) || (sz == cap - 2);
            }
        }

        void check(const uint32_t expected) const {
            uint32_t packed = 0;
            size_type index = tail_;

            // Only pack up to 4 bytes in FIFO order
            const size_type limit = (size_ < 4) ? size_ : 4;

            for (size_type i = 0; i < limit; ++i) {
                const uint8_t b = buffer_[index].data;
                packed |= (static_cast<uint32_t>(b) << (i * 8)); // little-endian
                index = (index + 1) % QueueLen;
            }

            assert(packed == expected);
        }

    private:
        std::array<QueueEntry, QueueLen> buffer_;
        size_type head_; // next write
        size_type tail_; // next read
        size_type size_; // number of valid entries

        void advanceHead() {
            head_ = (head_ + 1) % QueueLen;
            ++size_;
        }

        void advanceTail() {
            tail_ = (tail_ + 1) % QueueLen;
            --size_;
        }
    };

    void advanceMicrocodePointer() {
        _microcodePointer =
            (_microcodePointer & 0xFFF0) | ((_microcodePointer + 1) & 0xf);
    }

    int _stopIP;
    int _stopSeg;

    uint64_t _cycle;
    uint64_t _logStartCycle;
    uint64_t _logEndCycle;
    uint64_t _executeEndCycle;
    bool _consoleLogging;

    bool _nmiRequested; // Not actually set anywhere yet

    BusState _busState;
    bool _prefetching;

    IOType _ioType;
    uint32_t _ioAddress;
    uint16_t _ioIndex;
    uint8_t _ioReadData;
    uint8_t _ioWriteData;
    int _ioSegment;
    IOType _lastIOType;

    SnifferDecoder _snifferDecoder;

    uint16_t _registers[32];
    uint32_t _queue;
    InstructionQueue _newQueue{};
    int _queueBytes;
    uint8_t* _byteRegisters[8];
    uint8_t _microcode[4 * 512];
    uint8_t _microcodeIndex[2048];
    uint16_t _translation[256];
    uint32_t _groups[257];
    uint32_t _group;
    uint32_t _nextGroup;
    uint16_t _microcodePointer;
    uint16_t _nextMicrocodePointer;
    uint16_t _microcodeReturn;
    int _counter; // only 4 bits on the CPU
    uint8_t _alu;
    int _segmentOverride;
    bool _f1;
    bool _repne;
    bool _lock;
    uint8_t _opcode;
    uint8_t _modRM;
    bool _carry{false};
    bool _carryLatch{false}; // This latch enables updating the carry flag.
    bool _zero{false};
    bool _superZero{false};
    bool _auxiliary{false};
    bool _sign{false};
    uint8_t _parity{0};
    bool _overflow{false};
    int _aluInput;
    uint8_t _nextModRM;
    int _loaderState;
    bool _rni{false}; // Microcode signal to read next instruction
    uint16_t _inst_address{0x0000};
    bool _inst_boundary{false};
    bool _in_instruction{false};
    bool _nx{false}; // Microcode signal to read next instruction early (1-cycle pipeline)
    MicrocodeState _state;
    int _source;
    int _destination;
    int _type;
    bool _updateFlags{false};
    uint8_t _operands;
    bool _mIsM;
    bool _skipRNI;
    bool _useMemory;
    bool _wordSize;
    int _segment;
    int _lastMicrocodePointer;
    bool _dequeueing;
    int _ioCancelling;
    bool _ioRequested;
    bool _t4;
    bool _t5;
    bool _t6;
    bool _prefetchDelayed;
    bool _queueFlushing;
    bool _queueFilled;
    bool _interruptPending;
    bool _extraHaltDelay;
    uint32_t _savedAddress;
    bool _ready = true;
    //int _cyclesUntilCanLowerQueueFilled;
    bool _locking = false;
    // Breakpoint state
    bool _hasBreakpoint = false;
    bool _breakpointHit = false;
    uint16_t _breakpoint_cs = 0;
    uint16_t _breakpoint_ip = 0;
    uint32_t _testNumber = 0;
};

#endif
