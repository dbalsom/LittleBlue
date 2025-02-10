#ifndef CPU_H
#define CPU_H

#include <cstdint>
#include <iostream>
#include <ostream>
#include <string>
#include <SDL3/SDL_log.h>

#include "../littleblue.h"
#include "Bus.h"
#include "SnifferDecoder.h"

#include "microcode.h"

class Cpu
{
    enum {
        groupMemory                     = 1,
        groupInitialEARead              = 2,
        groupMicrocodePointerFromOpcode = 4,
        groupNonPrefix                  = 8,
        groupEffectiveAddress           = 0x10,
        groupAddSubBooleanRotate        = 0x20,
        groupNonFlagSet                 = 0x40,
        groupMNotAccumulator            = 0x80,
        groupNonSegregEA                = 0x100,
        groupNoDirectionBit             = 0x200,
        groupMicrocoded                 = 0x400,
        groupNoWidthInOpcodeBit0        = 0x800,
        groupByteOrWordAccess           = 0x1000,
        groupF1ZZFromPrefix             = 0x2000,
        groupIncDec                     = 0x4000,

        groupLoadRegisterImmediate      = 0x10000,
        groupWidthInOpcodeBit3          = 0x20000,
        groupCMC                        = 0x40000,
        groupHLT                        = 0x80000,
        groupREP                        = 0x100000,
        groupSegmentOverride            = 0x200000,
        groupLOCK                       = 0x400000,
        groupCLI                        = 0x800000,
        groupLoadSegmentRegister        = 0x1000000,
    };
public:
    Cpu() : _consoleLogging(false), _bus()
    {
        _logStartCycle = 0;
        _logEndCycle = 100;
        _registers[24] = 0x100;
        auto byteData = reinterpret_cast<uint8_t*>(&_registers[24]);
        int bigEndian = *byteData;
        int byteNumbers[8] = {0, 2, 4, 6, 1, 3, 5, 7};
        for (int i = 0 ; i < 8; ++i)
            _byteRegisters[i] = &byteData[byteNumbers[i] ^ bigEndian];
        _registers[21] = 0xffff;
        _registers[23] = 0;

        // Initialize microcode data and put it in a format more suitable for
        // software emulation.
        static const bool use8086 = false;

        // Microcode has 512 instruction words.
        uint32_t instructions[512];
        // Initialize all instructions to 0 prior to decoding.
        for (int i = 0; i < 512; ++i)
            instructions[i] = 0;

        for (int y = 0; y < 4; ++y) {
            int h = (y < 3 ? 24 : 12);
            for (int half = 0; half < 2; ++half) {
                std::string filename = (half == 1 ? "l" : "r") +
                    decimal(y) + (use8086 ? "a" : "") + ".txt";

                std::string_view s = get_file(filename);
                for (int yy = 0; yy < h; ++yy) {
                    int ib = y * 24 + yy;
                    for (int xx = 0; xx < 64; ++xx) {
                        int b = (s[yy * 66 + (63 - xx)] == '0' ? 1 : 0);
                        instructions[xx * 8 + half * 4 + yy % 4] |=
                            b << (20 - (ib >> 2));
                    }
                }
            }
        }
        for (int i = 0; i < 512; ++i) {
            int d = instructions[i];
            int s = ((d >> 13) & 1) + ((d >> 10) & 6) + ((d >> 11) & 0x18);
            int dd = ((d >> 20) & 1) + ((d >> 18) & 2) + ((d >> 16) & 4) +
                ((d >> 14) & 8) + ((d >> 12) & 0x10);
            int typ = (d >> 7) & 7;
            if ((typ & 4) == 0)
                typ >>= 1;
            int f = (d >> 10) & 1;
            _microcode[i * 4] = dd;
            _microcode[i * 4 + 1] = s;
            _microcode[i * 4 + 2] = (f << 3) + typ;
            _microcode[i * 4 + 3] = d & 0xff;
        }

        int stage1[128];
        for (int x = 0; x < 128; ++x)
            stage1[x] = 0;
        for (int g = 0; g < 9; ++g) {
            int n = 16;
            if (g == 0 || g == 8)
                n = 8;
            int xx[9] = { 0, 8, 24, 40, 56, 72, 88, 104, 120 };
            int xp = xx[g];
            for (int h = 0; h < 2; ++h) {
                std::string filename = decimal(g) + (h == 0 ? "t" : "b") + ".txt";
                std::string_view s = get_file(filename);
                for (int y = 0; y < 11; ++y) {
                    for (int x = 0; x < n; ++x) {
                        int b = (s[y * (n + 2) + x] == '0' ? 1 : 0);
                        if (b != 0)
                            stage1[127 - (x + xp)] |= 1 << (y * 2 + (h ^ (y <= 2 ? 1 : 0)));
                    }
                }
            }
        }
        for (int i = 0; i < 2048; ++i) {
            static const int ba[] = { 7, 2, 1, 0, 5, 6, 8, 9, 10, 3, 4 };
            for (int j = 0; j < 128; ++j) {
                int s1 = stage1[j];
                if (s1 == 0)
                    continue;
                bool found = true;
                for (int b = 0; b < 11; ++b) {
                    int x = (s1 >> (ba[b] * 2)) & 3;
                    int ib = (i >> (10 - b)) & 1;
                    if (!(x == 0 || (x == 1 && ib == 1) || (x == 2 && ib == 0))) {
                        found = false;
                        break;
                    }
                }
                if (found) {
                    _microcodeIndex[i] = j;
                    break;
                }
            }
        }

        std::string translationFile = use8086 ? "translation_8086.txt" : "translation_8088.txt";
        std::string_view translationString = get_file(translationFile);
        int tsp = 0;
        char c = translationString[0];
        for (int i = 0; i < 33; ++i) {
            int mask = 0;
            int bits = 0;
            int output = 0;
            for (int j = 0; j < 8; ++j) {
                if (c != '?')
                    mask |= 128 >> j;
                if (c == '1')
                    bits |= 128 >> j;
                ++tsp;
                c = translationString[tsp];
            }
            for (int j = 0; j < 14; ++j) {
                while (c != '0' && c != '1') {
                    ++tsp;
                    c = translationString[tsp];
                }
                if (c == '1')
                    output |= 8192 >> j;
                ++tsp;
                c = translationString[tsp];
            }
            while (c != 10 && c != 13) {
                ++tsp;
                c = translationString[tsp];
            }
            while (c == 10 || c == 13) {
                ++tsp;
                c = translationString[tsp];
            }
            for (int j = 0; j < 256; ++j) {
                if ((j & mask) == bits)
                    _translation[j] = output;
            }
        }

        int groupInput[38 * 18];
        int groupOutput[38 * 15];
        std::string_view groupString = get_file("group.txt");
        for (int x = 0; x < 38; ++x) {
            for (int y = 0; y < 15; ++y) {
                groupOutput[y * 38 + x] =
                    (groupString[y * 40 + x] == '0' ? 0 : 1);
            }
            for (int y = 0; y < 18; ++y) {
                int c = groupString[((y / 2) + 15) * 40 + x];
                if ((y & 1) == 0)
                    groupInput[y * 38 + x] = ((c == '*' || c == '0') ? 1 : 0);
                else
                    groupInput[y * 38 + x] = ((c == '*' || c == '1') ? 1 : 0);
            }
        }
        static const int groupYY[18] = { 1, 0,  3, 2,  4, 6,  5, 7,  11, 10,  12, 13,  8, 9,  15, 14,  16, 17 };
        for (int x = 0; x < 34; ++x) {
            if (x == 11)
                continue;
            for (int i = 0; i < 0x101; ++i) {
                bool found = true;
                for (int j = 0; j < 9; ++j) {
                    int y0 = groupInput[groupYY[j*2] * 38 + x];
                    int y1 = groupInput[groupYY[j*2 + 1] * 38 + x];
                    int b = (i >> j) & 1;
                    if ((y0 == 1 && b == 1) || (y1 == 1 && b == 0)) {
                        found = false;
                        break;
                    }
                }
                if (found) {
                    int g = 0;
                    for (int j = 0; j < 15; ++j)
                        g |= groupOutput[j * 38 + x] << j;
                    if (x == 10)
                        g |= groupLoadRegisterImmediate;
                    if (x == 12)
                        g |= groupWidthInOpcodeBit3;
                    if (x == 13)
                        g |= groupCMC;
                    if (x == 14)
                        g |= groupHLT;
                    if (x == 31)
                        g |= groupREP;
                    if (x == 32)
                        g |= groupSegmentOverride;
                    if (x == 33)
                        g |= groupLOCK;
                    if (i == 0xfa)
                        g |= groupCLI;
                    if (i == 0x8e || (i & 0xe7) == 0x07)
                        g |= groupLoadSegmentRegister;
                    _groups[i] = g;
                }
            }
        }

        _microcodePointer = 0;
        _microcodeReturn = 0;
    }
    uint8_t* getRAM() { return _bus.ram(); }
    uint16_t* getRegisters() { return &_registers[24]; }
    uint16_t* getSegmentRegisters() { return &_registers[0]; }
    void stubInit() { _bus.stubInit(); }
    void setExtents(int logStartCycle, int logEndCycle, int executeEndCycle,
        int stopIP, int stopSeg)
    {
        _logStartCycle = logStartCycle + 4;
        _logEndCycle = logEndCycle;
        _executeEndCycle = executeEndCycle;
        _stopIP = stopIP;
        _stopSeg = stopSeg;
    }
    void setInitialIP(int v) { ip() = v; }
    int cycle() const { return _cycle - 11; }
    std::string log() const { return _log; }
    void reset()
    {
        _bus.reset();

        for (int i = 0; i < 0x20; ++i)
            _registers[i] = 0;
        _registers[1] = 0xffff;  // RC (CS)
        _registers[21] = 0xffff; // ONES
        _registers[15] = 2; // FLAGS

        _cycle = 0;
        _microcodePointer = 0x1800;
        _busState = tIdle;
        _ioType = ioPassive;
        _snifferDecoder.reset();
        _prefetching = true;
        _log = "";
        ip() = 0;
        _nmiRequested = false;
        _queueBytes = 0;
        _queue = 0;
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
    }
    void run_for(int cycleCt) {
        for (int i = 0 ; i < cycleCt; i++) {
            simulateCycle();
        }
    }
    void run()
    {
        do {
            simulateCycle();
        } while ((getRealIP() != _stopIP + 2 || cs() != _stopSeg) &&
            _cycle < _executeEndCycle);
    }
    void setConsoleLogging() { _consoleLogging = true; }
private:
    uint16_t getRealIP() { return ip() - _queueBytes; }
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
    uint8_t queueRead()
    {
        uint8_t uint8_t = _queue & 0xff;
        _dequeueing = true;
        _snifferDecoder.queueOperation(3);
        return uint8_t;
    }
    int modRMReg() { return (_modRM >> 3) & 7; }
    int modRMReg2() { return _modRM & 7; }
    uint16_t& rw(int r) { return _registers[24 + r]; }
    uint16_t& rw() { return rw(_opcode & 7); }
    uint16_t& ax() { return rw(0); }
    uint8_t& rb(int r) { return *_byteRegisters[r]; }
    uint8_t& al() { return rb(0); }
    uint16_t& sr(int r) { return _registers[r & 3]; }
    uint16_t& cs() { return sr(1); }
    bool cf() { return lowBit(flags()); }
    void setCF(bool v) { flags() = (flags() & ~1) | (v ? 1 : 0); }
    bool pf() { return (flags() & 4) != 0; }
    bool af() { return (flags() & 0x10) != 0; }
    bool zf() { return (flags() & 0x40) != 0; }
    bool sf() { return (flags() & 0x80) != 0; }
    bool intf() { return (flags() & 0x200) != 0; }
    void setIF(bool v) { flags() = (flags() & ~0x200) | (v ? 0x200 : 0); }
    bool df() { return (flags() & 0x400) != 0; }
    void setDF(bool v) { flags() = (flags() & ~0x400) | (v ? 0x400 : 0); }
    bool of() { return (flags() & 0x800) != 0; }
    void setOF(bool v) { flags() = (flags() & ~0x800) | (v ? 0x800 : 0); }
    uint16_t& ip() { return _registers[4]; }
    uint16_t& ind() { return _registers[5]; }
    uint16_t& opr() { return _registers[6]; }
    uint16_t& tmpa() { return _registers[12]; }
    uint16_t& tmpb() { return _registers[13]; }
    uint16_t& flags() { return _registers[15]; }
    uint16_t& modRMRW() { return rw(modRMReg()); }
    uint8_t& modRMRB() { return rb(modRMReg()); }
    uint16_t& modRMRW2() { return rw(modRMReg2()); }
    uint8_t& modRMRB2() { return rb(modRMReg2()); }
    uint16_t getMemOrReg(bool mem)
    {
        if (mem) {
            if (_useMemory)
                return opr();
            if (!_wordSize)
                return modRMRB2();
            return modRMRW2();
        }
        if ((_group & groupNonSegregEA) == 0)
            return sr(modRMReg());
        if (!_wordSize) {
            int n = modRMReg();
            uint16_t r = rw(n & 3);
            if ((n & 4) != 0)
                r = (r >> 8) + (r << 8);
            return r;
        }
        return modRMRW();
    }
    void setMemOrReg(bool mem, uint16_t v)
    {
        if (mem) {
            if (_useMemory)
                opr() = v;
            else {
                if (!_wordSize)
                    modRMRB2() = static_cast<uint8_t>(v);
                else
                    modRMRW2() = v;
            }
        }
        else {
            if ((_group & groupNonSegregEA) == 0)
                sr(modRMReg()) = v;
            else {
                if (!_wordSize)
                    modRMRB() = static_cast<uint8_t>(v);
                else
                    modRMRW() = v;
            }
        }
    }
    void startInstruction()
    {
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
    void startMicrocodeInstruction()
    {
        _loaderState = 2;
        startInstruction();
        _microcodePointer = _nextMicrocodePointer;
        _wordSize = true;
        if ((_group & groupNoWidthInOpcodeBit0) == 0 && !lowBit(_opcode))
            _wordSize = false;
        if ((_group & groupByteOrWordAccess) == 0)
            _wordSize = false;  // Just for XLAT
        _carry = cf();  // Just for SALC
        _overflow = of(); // Not sure if the other flags work the same
        _parity = pf() ? 0x40 : 0;
        _sign = sf();
        _zero = zf();
        _auxiliary = af();
        _alu = 0; // default is ADD tmpa (assumed in EA calculations)
        _mIsM = ((_group & groupNoDirectionBit) != 0 || (_opcode & 2) == 0);
        _rni = false;
        _nx = false;
        _skipRNI = false;
        _state = stateRunning;

        if ((_group & groupEffectiveAddress) != 0) {
            // EALOAD and EADONE finish with RTN
            _modRM = _nextModRM;
            if ((_group & groupMicrocodePointerFromOpcode) == 0) {
                _microcodePointer = ((_modRM << 1) & 0x70) | 0xf00 |
                    ((_opcode & 1) << 12) | ((_opcode & 8) << 4);
                _state = stateSingleCycleWait;
            }
            _useMemory = ((_modRM & 0xc0) != 0xc0);
            if (_useMemory) {
                int t = _translation[2 + ((_modRM & 7) << 3) + (_modRM & 0xc0)];
                _segment = (lowBit(t) ? 2 : 3);
                _microcodeReturn = _microcodePointer;
                _microcodePointer = t >> 1;
                _state = stateSingleCycleWait;
            }
        }
    }
    void startNonMicrocodeInstruction()
    {
        _loaderState = 0;
        startInstruction();
        if ((_group & groupLOCK) != 0) {
            _locking = true;
            return;
        }
        if ((_group & groupREP) != 0) {
            _f1 = true;
            _repne = !lowBit(_opcode);
            return;
        }
        if ((_group & groupHLT) != 0) {
            _loaderState = 2;
            _rni = false;
            _nx = false;
            _state = stateHaltingStart;
            _extraHaltDelay = !((_busState == tIdle && !_t5 && !_t6 && _ioType == ioPassive) || (_t5 && _lastIOType != ioPrefetch));
            return;
        }
        if ((_group & groupCMC) != 0) {
            flags() ^= 1;
            return;
        }
        if ((_group & groupNonFlagSet) == 0) {
            switch (_opcode & 0x06) {
                case 0: // CLCSTC
                    setCF(_opcode & 1);
                    break;
                case 2: // CLISTI
                    setIF(_opcode & 1);
                    break;
                case 4: // CLDSTD
                    setDF(_opcode & 1);
                    break;
            }
            return;
        }
        if ((_group & groupSegmentOverride) != 0) {
            _segmentOverride = (_opcode >> 3) & 3;
            return;
        }
    }
    uint16_t doRotate(uint16_t v, uint16_t a, bool carry)
    {
        _carry = carry;
        _overflow = topBit(v ^ a);
        return v;
    }
    uint16_t doShift(uint16_t v, uint16_t a, bool carry, bool auxiliary)
    {
        _auxiliary = auxiliary;
        doPZS(v);
        return doRotate(v, a, carry);
    }
    uint16_t doALU()
    {
        uint16_t t;
        bool oldAF;
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
                return doRotate((a << 1) | (topBit(a) ? 1 : 0), a, topBit(a));
            case 0x09: // ROR
                return doRotate(((a & wordMask()) >> 1) | topBit(lowBit(a)), a, lowBit(a));
            case 0x0a: // LRCY
                return doRotate((a << 1) | (_carry ? 1 : 0), a, topBit(a));
            case 0x0b: // RRCY
                return doRotate(((a & wordMask()) >> 1) | topBit(_carry), a, lowBit(a));
            case 0x0c: // SHL
                return doShift(a << 1, a, topBit(a), (a & 8) != 0);
            case 0x0d: // SHR
                return doShift((a & wordMask()) >> 1, a, lowBit(a), (a & 0x20) != 0);
            case 0x0e: // SETMO
                return doShift(0xffff, a, false, false);
            case 0x0f: // SAR
                return doShift(((a & wordMask()) >> 1) | topBit(topBit(a)), a, lowBit(a), (a & 0x20) != 0);
            case 0x10: // PASS
                return doShift(a, a, false, false);
            case 0x14: // DAA
                oldAF = _auxiliary;
                t = a;
                if (oldAF || (a & 0x0f) > 9) {
                    t = a + 6;
                    _overflow = topBit(t & (t ^ a));
                    _auxiliary = true;
                }
                if (_carry || a > (oldAF ? 0x9fU : 0x99U)) {
                    v = t + 0x60;
                    _overflow = topBit(v & (v ^ t));
                    _carry = true;
                }
                else
                    v = t;
                doPZS(v);
                break;
            case 0x15: // DAS
                oldAF = _auxiliary;
                t = a;
                if (oldAF || (a & 0x0f) > 9) {
                    t = a - 6;
                    _overflow = topBit(a & (t ^ a));
                    _auxiliary = true;
                }
                if (_carry || a > (oldAF ? 0x9fU : 0x99U)) {
                    v = t - 0x60;
                    _overflow = topBit(t & (v ^ t));
                    _carry = true;
                }
                else
                    v = t;
                doPZS(v);
                break;
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
    uint32_t readSource()
    {
        uint32_t v;
        switch (_source) {
            case 7:  // Q
                if (_queueBytes == 0) {
                    _state = stateWaitingForQueueData;
                    return 0;
                }
                return queueRead();
            case 8:  // A (AL)
            case 9:  // C (CL)? - not used
            case 10: // E (DL)? - not used
            case 11: // L (BL)? - not used
                return rb(_source & 3);
            case 16: // X (AH)
            case 17: // B (CH)? - not used
                return rb((_source & 3) + 4);
            case 18: // M
                if ((_group & groupMNotAccumulator) == 0)
                    return (_wordSize ? ax() : al());
                if ((_group & groupEffectiveAddress) == 0)
                    return rw();
                return getMemOrReg(_mIsM);
            case 19: // R
                if ((_group & groupEffectiveAddress) == 0)
                    return sr((_opcode >> 3) & 7);
                return getMemOrReg(!_mIsM);
            case 20: // SIGMA
                v = doALU();
                if (_updateFlags) {
                    flags() = (flags() & 0xf702) | (_overflow ? 0x800 : 0)
                        | (_sign ? 0x80 : 0) | (_zero ? 0x40 : 0)
                        | (_auxiliary ? 0x10 : 0) | _parity
                        | (_carry ? 1 : 0);
                }
                return v;
            case 22: // CR
                _wordSize = true; // HACK: not sure how this happens for INT0 on the hardware
                return _microcodePointer & 0xf;
        }
        return _registers[_source];
    }
    void writeDestination(uint32_t v)
    {
        switch (_destination) {
            case 8:  // A (AL)
            case 9:  // C (CL)? - not used
            case 10: // E (DL)? - not used
            case 11: // L (BL)? - not used
                rb(_destination & 3) = v;
                break;
            case 15: // F
                flags() = (v & 0xfd5) | 2;
                break;
            case 16: // X (AH)
            case 17: // B (CH)? - not used
                rb((_destination & 3) + 4) = v;
                break;
            case 18: // M
                if (_alu == 7)
                    break;
                if ((_group & groupMNotAccumulator) == 0) {
                    if (!_wordSize)
                        al() = static_cast<uint8_t>(v);
                    else
                        ax() = v;
                    break;
                }
                if ((_group & groupEffectiveAddress) == 0) {
                    if ((_group & groupWidthInOpcodeBit3) != 0 &&
                        (_opcode & 8) != 0)
                        rb(_opcode & 7) = v;
                    else
                        rw() = v;
                }
                else {
                    setMemOrReg(_mIsM, v);
                    _skipRNI = (_mIsM && _useMemory);
                }
                break;
            case 19: // R
                if ((_group & groupEffectiveAddress) == 0)
                    sr((_opcode >> 3) & 7) = v;
                else
                    setMemOrReg(!_mIsM, v);
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
                _registers[_destination] = v;
        }
    }
    void busAccessDone(uint8_t high)
    {
        opr() |= high << 8;
        if ((_operands & 0x10) != 0)
            _rni = true;
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
        }
        _state = stateRunning;
    }
    void doSecondMisc()
    {
        switch (_operands & 7) {
            case 0: // RNI
                if (!_skipRNI)
                    _rni = true;
                break;
            case 1: // WB,NX
                if (!_mIsM || !_useMemory || _alu == 7)
                    _nx = true;
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
                _microcodePointer = _microcodeReturn;
                _state = stateSingleCycleWait;
                break;
            case 5: // NX
                _nx = true;
                break;
        }
    }
    void startIO()
    {
        _state = stateIODelay1;
        if ((_busState == t3 || _busState == tWait) || canStartPrefetch())
            _state = stateIODelay2;
        if (_busState == t4 || _busState == tIdle)
            _ioType = ioPassive;
        _ioRequested = true;
    }
    void doSecondHalf()
    {
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
                _nx = lowBit(_operands);
                if (_mIsM && _useMemory && _alu != 7 &&
                    (_group & groupEffectiveAddress) != 0)
                    _nx = false;
                _aluInput = (_operands >> 1) & 3;
                if (_alu == 0x11) { // XI
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
                        setCF(false);
                        break;
                    case 6: // CCOF
                        setCF(false);
                        setOF(false);
                        break;
                    case 7: // SCOF
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
                if (!condition(_operands >> 4))
                    break;
                if (_type == 7)
                    _microcodeReturn = _microcodePointer;
                _microcodePointer = _translation[
                    ((_type & 2) << 6) +
                        ((_operands << 3) & 0x78) +
                        ((_group & groupInitialEARead) == 0 ? 4 : 0) +
                        ((_modRM & 0xc0) == 0 ? 1 : 0)] >> 1;
                _state = stateSingleCycleWait;
                break;
        }
    }
    void busStart()
    {
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
        }
    }
    void executeMicrocode()
    {
        uint8_t* m;
        uint32_t v;
        switch (_state) {
            case stateRunning:
                _lastMicrocodePointer = _microcodePointer;
                m = &_microcode[
                    ((_microcodeIndex[_microcodePointer >> 2] << 2) +
                        (_microcodePointer & 3)) << 2];
                _microcodePointer = (_microcodePointer & 0xfff0) |
                    ((_microcodePointer + 1) & 0xf);
                _destination = m[0];
                _source = m[1];
                _type = m[2] & 7;
                _updateFlags = ((m[2] & 8) != 0);
                _operands = m[3];
                v = readSource();
                if (_state == stateWaitingForQueueData)
                    break;
                writeDestination(v);
                doSecondHalf();
                break;
            case stateWaitingForQueueData:
                if (_queueBytes == 0)
                    break;
                _state = stateRunning;
                writeDestination(readSource());
                doSecondHalf();
                break;

            case stateSuspending:
                if (_busState != t4)
                    break;
                _state = stateRunning;
                break;

            case stateWaitingForQueueIdle:
                if (_ioType != ioPassive || _busState == t4 || _t4)
                    break;
                ip() -= _queueBytes;
                _queueBytes = 0; // so that realIP() is correct
                _state = stateRunning;
                break;

            case stateIODelay2:
                if (!_ready && (_busState == t3 || _busState == tWait))
                    break;
                _state = stateIODelay1;
                break;
            case stateIODelay1:
                if (_busState == t4)
                    break;
                _state = stateWaitingUntilFirstByteCanStart;
                break;
            case stateWaitingUntilFirstByteCanStart:
                if (_ioType != ioPassive)
                    break;
                _ioWriteData = opr() & 0xff;
                _ioSegment = (_operands >> 2) & 3;
                if (_ioSegment == 3)
                    _ioSegment = _segmentOverride != -1 ? _segmentOverride : _segment;
                else {
                    // 9 because it's a register slot that stays as all-zero
                    // bits, and has the same low two bits (so that the logs
                    // show the right segment).
                    if (_ioSegment == 1)
                        _ioSegment = 9;
                }
                _ioAddress = physicalAddress(_ioSegment, ind());
                _state = stateWaitingUntilFirstByteDone;
                busStart();
                break;
            case stateWaitingUntilFirstByteDone:
                if (!_wordSize)
                    _ioRequested = false;
                if (_ioType != ioPassive)
                    break;
                _ioWriteData = opr() >> 8;
                opr() = _ioReadData;
                if (!_wordSize)
                    busAccessDone(0xff);
                else {
                    _ioAddress = physicalAddress(_ioSegment, ind() + 1);
                    _state = stateWaitingUntilSecondByteDone;
                    busStart();
                }
                break;
            case stateWaitingUntilSecondByteDone:
                _ioRequested = false;
                if (_ioType != ioPassive)
                    break;
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
                _snifferDecoder.setStatus((int)ioHalt);
                _state = stateHalting1;
                break;
            case stateHalting1:
                _state = stateHalted;
                break;
            case stateHalted:
                _snifferDecoder.setStatus((int)ioPassive);
                if (!interruptPending())
                    break;
                if (_extraHaltDelay) {
                    _extraHaltDelay = false;
                    break;
                }
                _rni = true;
                _state = stateRunning;
                break;
        }
    }
    void setNextMicrocode(int nextState, int nextMicrocode)
    {
        _nextMicrocodePointer = nextMicrocode;
        _loaderState = nextState | 1;
        _nextGroup = _groups[nextMicrocode >> 4];
    }
    void readOpcode(int nextState)
    {
        if ((flags() & 0x100) != 0) {
            setNextMicrocode(nextState, 0x1000);
            return;
        }
        if (_nmiRequested) {
            _nmiRequested = false;
            setNextMicrocode(nextState, 0x1001);
            return;
        }
        if (interruptPending()) {
            setNextMicrocode(nextState, 0x1002);
            return;
        }
        if (_queueBytes != 0) {
            setNextMicrocode(nextState, queueRead() << 4);
            _snifferDecoder.queueOperation(1);
            return;
        }
        _loaderState = nextState & 2;
    }
    bool canStartPrefetch()
    {
        if (!_prefetching) // prefetching turned off for a jump?
            return false;
        if (_ioRequested || _ioType != ioPassive || _t4)  // bus busy?
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
    BusState completeIO(bool write)
    {
        if (!_ready)
            return tWait;
        if (!write) {
            _ioReadData = _bus.read();
            _snifferDecoder.setData(_ioReadData);
        }
        else
            _ioReadData = _ioWriteData;
        _bus.setPassiveOrHalt(true);
        _snifferDecoder.setStatus((int)ioPassive);
        _lastIOType = _ioType;
        _ioType = ioPassive;
        return t4;
    }
    void simulateCycle()
    {
        BusState nextState = _busState;
        bool write = _ioType == ioWriteMemory || _ioType == ioWritePort;
        _t6 = _t5;
        _t5 = _t4;
        _t4 = false;
        _ready = _bus.ready();
        bool prefetchCompleting = false;
        switch (_busState) {
            case t1:
                _snifferDecoder.setAddress(_ioAddress);
                _bus.startAccess(_ioAddress, (int)_ioType);
                nextState = t2;
                break;
            case t2:
                _snifferDecoder.setStatusHigh(_ioSegment);
                _snifferDecoder.setBusOperation((int)_ioType);
                if (write)
                    _snifferDecoder.setData(_ioWriteData);
                if (_ioType == ioInterruptAcknowledge)
                    _bus.setLock(_state == stateWaitingUntilFirstByteDone);
                nextState = t3;
                break;
            case t3:
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
                if (_lastIOType == ioWriteMemory || _lastIOType == ioWritePort)
                    _bus.write(_ioReadData);
                if (_lastIOType == ioPrefetch)
                    prefetchCompleting = true;
                nextState = tIdle;
                _t4 = true;
                break;
        }
        if (_dequeueing) {
            _dequeueing = false;
            _queue >>= 8;
            --_queueBytes;
        }
        //if (_cyclesUntilCanLowerQueueFilled == 0 || (_cyclesUntilCanLowerQueueFilled == 1 && _queueBytes < 3)) {
            //_cyclesUntilCanLowerQueueFilled = 0;
        if ((_queueBytes < 3 || (_busState == tIdle && (_lastIOType != ioPrefetch || (!_t4 && !_t5))))) {
            if (_busState == tIdle && !(_t4 && _lastIOType == ioPrefetch) && _queueBytes < 4)
                _queueFilled = false;
        }
        //}
        //else
        //    --_cyclesUntilCanLowerQueueFilled;
        if ((_loaderState & 2) != 0)
            executeMicrocode();
        if (_locking) {
            _locking = false;
            _lock = true;
            _bus.setLock(true);
        }
        switch (_loaderState) {
            case 0:
                readOpcode(0);
                break;
            case 1:
            case 3:
                if ((_group & groupNonPrefix) != 0) {
                    _segmentOverride = -1;
                    _f1 = false;
                    _repne = false;
                    if (_lock) {
                        _lock = false;
                        _bus.setLock(false);
                    }
                }
                if ((_nextGroup & groupMicrocoded) == 0)  // 1BL
                    startNonMicrocodeInstruction();
                else {
                    if ((_nextGroup & groupEffectiveAddress) == 0)  // SC
                        startMicrocodeInstruction();
                    else {
                        _loaderState = 1;
                        if (_queueBytes != 0) {  // SC
                            _nextModRM = queueRead();
                            startMicrocodeInstruction();
                        }
                    }
                }
                break;
            case 2:
                if (_rni)
                    readOpcode(0);
                else {
                    if (_nx)
                        readOpcode(2);
                }
                break;
        }

        if (prefetchCompleting) {
            _queue |= (_ioReadData << (_queueBytes << 3));
            ++_queueBytes;
            ++ip();
        }
        if (nextState == tIdle && _ioType != ioPassive) {
            nextState = t1;
            if (_ioType == ioPrefetch)
                _ioAddress = physicalAddress(_ioSegment, ip());

            _bus.setPassiveOrHalt(_ioType == ioHalt);
            _snifferDecoder.setStatus((int)_ioType);
        }
        if (canStartPrefetch()) {
            _ioType = ioPrefetch;
            _ioSegment = 1;
        }
        if (_queueFlushing) {
            _queueFlushing = false;
            _prefetching = true;
        }
        if (_cycle < _logEndCycle) {
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
                    _snifferDecoder.setBusOperation((int)_ioType);
                }
            }
            _snifferDecoder.setReady(_ready);
            _snifferDecoder.setLock(_lock);
            _snifferDecoder.setDMAS(_bus.getDMAS());
            _snifferDecoder.setIRQs(_bus.getIRQLines());
            _snifferDecoder.setINT(_bus.interruptPending());
            _snifferDecoder.setCGA(_bus.getCGA());
            std::string l = _bus.snifferExtra() + _snifferDecoder.getLine();
            l = pad(l, 103) + microcodeString() + "\n";
            if (_cycle >= _logStartCycle) {
                if (_consoleLogging)
                    //std::cout << l << std::endl;
                    SDL_Log("%s", l.c_str());
                else
                    _log += l;
            }
        }
        _busState = nextState;
        ++_cycle;
        _interruptPending = _bus.interruptPending();
        _bus.wait();
    }
    std::string pad(const std::string& s, int n) {  return s + std::string(std::max(0, n - static_cast<int>(s.length())), ' '); }
    std::string microcodeString()
    {
        if (_lastMicrocodePointer == -1)
            return "";
        static const char* regNames[] = {
            "RA",  // ES
            "RC",  // CS
            "RS",  // SS - presumably, to fit pattern. Only used in RESET
            "RD",  // DS
            "PC",
            "IND",
            "OPR",
            "no dest",  // as dest only - source is Q
            "A",   // AL
            "C",   // CL? - not used
            "E",   // DL? - not used
            "L",   // BL? - not used
            "tmpa",
            "tmpb",
            "tmpc",
            "F",   // flags register
            "X",   // AH
            "B",   // CH? - not used
            "M",
            "R",   // source specified by modrm and direction, destination specified by r field of modrm
            "tmpaL",    // as dest only - source is SIGNA
            "tmpbL",    // as dest only - source is ONES
            "tmpaH",    // as dest only - source is CR
            "tmpbH",    // as dest only - source is ZERO
            "XA",  // AX
            "BC",  // CX
            "DE",  // DX
            "HL",  // BX
            "SP",  // SP
            "MP",  // BP
            "IJ",  // SI
            "IK",  // DI
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
            "XC  ",  // jump if condition based on low 4 bits of opcode
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
            "IMULCOF ", // clear carry and overflow flags if product of signed multiply fits in low part, otherwise set them
            "MULCOF  ", // clear carry and overflow flags if product of unsigned multiply fits in low part, otherwise set them
            "PREIDIV ", // abs tmpa:tmpc and tmpb, invert F1 if one or the other but not both were negative
            "POSTIDIV", // negate ~tmpc if F1 set
        };
        uint8_t* m = &_microcode[
            ((_microcodeIndex[_lastMicrocodePointer >> 2] << 2) +
                (_lastMicrocodePointer & 3)) << 2];
        int d = m[0];
        int s = m[1];
        int t = m[2] & 7;
        bool f = ((m[2] & 8) != 0);
        int o = m[3];
        std::string r;
        if (d == 0 && s == 0 && t == 0 && !f && o == 0) {
            r += "null instruction executed!";
            s = 0x15;
            d = 0x07;
            t = 4;
            o = 0xfe;
        }
        if (s == 0x15 && d == 0x07)  // "ONES  -> Q" used as no-op move
            r = "                ";
        else {
            const char* source = regNames[s];
            switch (s) {
                case 0x07: source = "Q"; break;
                case 0x14: source = "SIGMA"; break;
                case 0x15: source = "ONES"; break;
                case 0x16: source = "CR"; break;  // low 3 bits of microcode address Counting Register + 1? Used as interrupt number at 0x198 (1), 0x199 (2), 0x1a7 (0), 0x1af (4), and 0x1b2 (3)
                case 0x17: source = "ZERO"; break;
            }
            r = pad(std::string(source), 5) + " -> " + pad(std::string(regNames[d]), 7);
        }
        r += "   ";
        if ((o & 0x7f) == 0x7f) {
            r += "                  ";
            t = -1;
        }
        else
            r += decimal(t) + "   ";
        switch (t) {  // TYP bits
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
                    case 0x00: r += "MAXC "; break;
                    case 0x01: r += "FLUSH"; break;
                    case 0x02: r += "CF1  "; break;
                    case 0x03: r += "CITF "; break;  // clear interrupt and trap flags
                    case 0x04: r += "RCY  "; break;  // reset carry
                    case 0x06: r += "CCOF "; break;  // clear carry and overflow
                    case 0x07: r += "SCOF "; break;  // set carry and overflow
                    case 0x08: r += "WAIT "; break;  // not sure what this does
                    case 0x0f: r += "none "; break;
                }
                r += " ";
                switch (o & 7) {
                    case 0: r += "RNI     "; break;
                    case 1: r += "WB,NX   "; break;
                    case 2: r += "CORR    "; break;
                    case 3: r += "SUSP    "; break;
                    case 4: r += "RTN     "; break;
                    case 5: r += "NX      "; break;
                    case 7: r += "none    "; break;
                }
                break;
            case 1:
                switch ((o >> 3) & 0x1f) {
                    case 0x00: r += "ADD "; break;
                    case 0x02: r += "ADC "; break;
                    case 0x04: r += "AND "; break;
                    case 0x05: r += "SUBT"; break;
                    case 0x0a: r += "LRCY"; break;
                    case 0x0b: r += "RRCY"; break;
                    case 0x10: r += "PASS"; break;
                    case 0x11: r += "XI  "; break;
                    case 0x18: r += "INC "; break;
                    case 0x19: r += "DEC "; break;
                    case 0x1a: r += "COM1"; break;
                    case 0x1b: r += "NEG "; break;
                    case 0x1c: r += "INC2"; break;
                    case 0x1d: r += "DEC2"; break;
                }
                r += "  ";
                switch (o & 7) {
                    case 0: r += "tmpa    "; break;
                    case 1: r += "tmpa, NX"; break;
                    case 2: r += "tmpb    "; break;
                    case 3: r += "tmpb, NX"; break;
                    case 4: r += "tmpc    "; break;
                }
                break;
            case 6:
                switch ((o >> 4) & 7) {
                    case 0: r += "R    "; break;
                    case 2: r += "IRQ  "; break;
                    case 4: r += "w    "; break;
                    case 5: r += "W,RNI"; break;
                }
                r += " ";
                switch ((o >> 2) & 3) {  // Bits 0 and 1 are segment
                    case 0: r += "DA,"; break;  // ES
                    case 1: r += "D0,"; break;  // segment 0
                    case 2: r += "DS,"; break;  // SS
                    case 3: r += "DD,"; break;  // DS
                }
                switch (o & 3) {  // bits 2 and 3 are IND update
                    case 0: r += "P2"; break;  // Increment IND by 2
                    case 1: r += "BL"; break;  // Adjust IND according to word size and DF
                    case 2: r += "M2"; break;  // Decrement IND by 2
                    case 3: r += "P0"; break;  // Don't adjust IND
                }
                r += "   ";
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
        return r;
    }
    bool condition(int n)
    {
        switch (n) {
            case 0x00: // F1ZZ
                if ((_group & groupF1ZZFromPrefix) != 0)
                    return zf() == (_f1 && _repne);
                return zf() != lowBit(_opcode);
            case 0x01: // MOD1
                return (_modRM & 0x40) != 0;
            case 0x02: // L8 - not sure if there's a better way to compute this
                if ((_group & groupLoadRegisterImmediate) != 0)
                    return (_opcode & 8) == 0;
                return !lowBit(_opcode) || (_opcode & 6) == 2;
            case 0x03: // Z
                return _zero;
            case 0x04: // NCZ
                --_counter;
                return _counter != -1;
            case 0x05: // TEST - no 8087 emulated yet
                return true;
            case 0x06: // OF
                return of();  // only used in INTO
            case 0x07: // CY
                return _carry;
            case 0x08: // UNC
                return true;
            case 0x09: // NF1
                return !_f1;
            case 0x0a: // NZ
                return !_zero;
            case 0x0b: // X0
                if ((_group & groupMicrocodePointerFromOpcode) == 0)
                    return (_modRM & 8) != 0;
                return (_opcode & 8) != 0;
            case 0x0c: // NCY
                return !_carry;
            case 0x0d: // F1
                return _f1;
            case 0x0e: // INT
                return interruptPending();
        }
        bool jump; // XC
        switch (_opcode & 0x0e) {
            case 0x00: jump = of(); break;  // O
            case 0x02: jump = cf(); break;  // C
            case 0x04: jump = zf(); break;  // Z
            case 0x06: jump = cf() || zf(); break;  // BE
            case 0x08: jump = sf(); break;  // S
            case 0x0a: jump = pf(); break;  // P
            case 0x0c: jump = (sf() != of()); break;  // L
            case 0x0e: jump = (sf() != of()) || zf(); break;  // LE
        }
        if (lowBit(_opcode))
            jump = !jump;
        return jump;
    }
    bool interruptPending()
    {
        return _nmiRequested || (intf() && _interruptPending);
    }
    uint16_t wordMask() { return _wordSize ? 0xffff : 0xff; }
    void doPZS(uint16_t v)
    {
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
            4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4 };
        _parity = table[v & 0xff];
        _zero = ((v & wordMask()) == 0);
        _sign = topBit(v);
    }
    void doFlags(uint32_t result, bool of, bool af)
    {
        doPZS(result);
        _overflow = of;
        _auxiliary = af;
    }
    uint16_t bitwise(uint16_t data)
    {
        doFlags(data, false, false);
        _carry = false;
        return data;
    }
    void doAddSubFlags(uint32_t result, uint32_t x, bool of, bool af)
    {
        doFlags(result, of, af);
        _carry = (((result ^ x) & (_wordSize ? 0x10000 : 0x100)) != 0);
    }
    bool lowBit(uint32_t v) { return (v & 1) != 0; }
    bool topBit(int w) { return (w & (_wordSize ? 0x8000 : 0x80)) != 0; }
    bool topBit(uint32_t w) { return (w & (_wordSize ? 0x8000 : 0x80)) != 0; }
    uint16_t topBit(bool v) { return v ? (_wordSize ? 0x8000 : 0x80) : 0; }
    uint16_t add(uint32_t a, uint32_t b, bool c)
    {
        uint32_t r = a + b + (c ? 1 : 0);
        doAddSubFlags(r, a ^ b, topBit((r ^ a) & (r ^ b)), ((a ^ b ^ r) & 0x10) != 0);
        return r;
    }
    uint16_t sub(uint32_t a, uint32_t b, bool c)
    {
        uint32_t r = a - (b + (c ? 1 : 0));
        doAddSubFlags(r, a ^ b, topBit((a ^ b) & (r ^ a)), ((a ^ b ^ r) & 0x10) != 0);
        return r;
    }
    uint32_t physicalAddress(int segment, uint16_t offset)
    {
        return ((sr(segment) << 4) + offset) & 0xfffff;
    }

    std::string _log;
    Bus _bus;

    int _stopIP;
    int _stopSeg;

    int _cycle;
    int _logStartCycle;
    int _logEndCycle;
    int _executeEndCycle;
    bool _consoleLogging;

    bool _nmiRequested;  // Not actually set anywhere yet

    BusState _busState;
    bool _prefetching;

    IOType _ioType;
    uint32_t _ioAddress;
    uint8_t _ioReadData;
    uint8_t _ioWriteData;
    int _ioSegment;
    IOType _lastIOType;

    SnifferDecoder _snifferDecoder;

    uint16_t _registers[32];
    uint32_t _queue;
    int _queueBytes;
    uint8_t* _byteRegisters[8];
    uint8_t _microcode[4*512];
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
    bool _carry;
    bool _zero;
    bool _auxiliary;
    bool _sign;
    uint8_t _parity;
    bool _overflow;
    int _aluInput;
    uint8_t _nextModRM;
    int _loaderState;
    bool _rni;
    bool _nx;
    MicrocodeState _state;
    int _source;
    int _destination;
    int _type;
    bool _updateFlags;
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
    bool _ready;
    //int _cyclesUntilCanLowerQueueFilled;
    bool _locking;
};

#endif