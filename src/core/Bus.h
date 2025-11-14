#pragma once

#include <cstdint>
#include <vector>

#include "bios.h"
#include "Cga.h"
#include "Dmac.h"
#include "Pic.h"
#include "Pit.h"
#include "Ppi.h"
#include "Fdc.h"
#include "Keyboard.h"

#define ROM_BASE_ADDRESS 0xFE000
#define CONVENTIONAL_RAM_SIZE 0xB8000
#define CGA_ADDRESS 0xB8000

class Bus
{
public:
    Bus() :
        _ram(CONVENTIONAL_RAM_SIZE), _rom(0x2000) {
        _rom.assign(U18, U18 + sizeof(U18));
        _pit.setGate(0, true);
        _pit.setGate(1, true);
        _pit.setGate(2, true);
        // Wire DMAC & PIC to the FDC so it can perform DMA and execute interrupts.
        _fdc.attachDMAC(&_dmac);
        _fdc.attachPIC(&_pic);
    }

    uint8_t* ram() { return &_ram[0]; }
    [[nodiscard]] size_t ramSize() const { return _ram.size(); }

    // Expose CGA instance for tools/UI that need direct access to video memory
    CGA* cga() { return &_cga; }

    // Expose PIC instance for debugging UI
    PIC* pic() { return &_pic; }

    // Restore PPI accessor (was accidentally removed)
    PPI* ppi() { return &_ppi; }

    // Expose FDC instance so UI/tools can load disk images
    FDC* fdc() { return &_fdc; }

    // Expose DMAC instance for debugging UI
    DMAC* dmac() { return &_dmac; }

    // Read a byte from a physical address without changing bus state.
    // This allows tools (disassembler/UI) to inspect memory (RAM or ROM) directly.
    [[nodiscard]] uint8_t peek(uint32_t address) const {
        // Real-mode uses 20-bit physical addressing; mask to 20 bits in callers if needed.
        if (address >= ROM_BASE_ADDRESS) {
            uint32_t romIndex = address - ROM_BASE_ADDRESS;
            if (romIndex < _rom.size())
                return _rom[romIndex];
            return 0xff;
        }
        if (address >= CGA_ADDRESS && address < CGA_ADDRESS + 0x4000) {
            return _cga.readMem(address - CGA_ADDRESS);
        }
        if (address >= CONVENTIONAL_RAM_SIZE) {
            return 0xff; // open bus / video area not resident here
        }
        if (address < _ram.size())
            return _ram[address];
        return 0xff;
    }

    [[nodiscard]] size_t romSize() const { return _rom.size(); }

    void reset() {
        _dmac.reset();
        _pic.reset();
        _pit.reset();
        _ppi.reset();
        _fdc.reset();
        _kb.reset();
        _cga.reset();
        _pitPhase = 2;
        _lastCounter0Output = false;
        _lastCounter1Output = true;
        _counter2Output = false;
        _counter2Gate = false;
        _speakerMask = false;
        _speakerOutput = false;
        _dmaState = sIdle;
        _passiveOrHalt = true;
        _lock = false;
        _previousPassiveOrHalt = true;
        _lastNonDMAReady = true;
        _cgaPhase = 0;
        _lastKbDisabled = false;
        _lastKbCleared = false;
        _lastIRQ6 = false;
    }

    void stubInit() {
        _pic.stubInit();
        _pit.stubInit();
        _pitPhase = 2;
        _lastCounter0Output = true;
    }

    void startAccess(uint32_t address, int type) {
        _address = address;
        _type = type;
        _cycle = 0;
    }

    void tick() {
        _ticks++;
        _cga.tick();
        _cgaPhase = (_cgaPhase + 3) & 0x0f;
        _pitPhase++;

        // Handle PIT updates every 4 ticks
        if (_pitPhase == 4) {
            _pitPhase = 0;
            _pit.wait();
            bool counter0Output = _pit.getOutput(0);
            if (_lastCounter0Output != counter0Output) {
                _pic.setIRQLine(0, counter0Output);
            }
            _lastCounter0Output = counter0Output;

            bool counter1Output = _pit.getOutput(1);
            if (counter1Output && !_lastCounter1Output && !dack0()) {
                _dmac.setDMARequestLine(0, true);
            }
            _lastCounter1Output = counter1Output;

            bool counter2Output = _pit.getOutput(2);
            if (_counter2Output != counter2Output) {
                _counter2Output = counter2Output;
                setSpeakerOutput();
                _ppi.setC(5, counter2Output);
                updatePPI();
            }
        }

        if (_speakerCycle != 0) {
            --_speakerCycle;
            if (_speakerCycle == 0) {
                _speakerOutput = _nextSpeakerOutput;
                _ppi.setC(4, _speakerOutput);
                updatePPI();
            }
        }

        if ((_ticks & 0xF) == 0) {
            // Check and clear the keyboard
            const auto kb_cleared = _ppi.getB(7);
            const auto kb_disabled = !_ppi.getB(6);
            if (kb_disabled && !_lastKbDisabled) {
                // Keyboard was just disabled.
                std::cout << "Bus: Disabling keyboard" << std::endl;
                _kb.setClockLineState(false);
            }
            else if (!kb_disabled && _lastKbDisabled) {
                // Keyboard was just enabled.
                std::cout << "Bus: Enabling keyboard" << std::endl;
                _kb.setClockLineState(true);
            }

            if (kb_cleared && !_lastKbCleared) {
                // KSR was just cleared.
                std::cout << "Bus: Clearing KSR & Interrupt" << std::endl;
                // Clear any pending IRQ 1.
                _pic.setIRQLine(1, false);
                // Clear the KSR attached to PPI port A.
                for (int i = 0; i < 8; ++i) {
                    _ppi.setA(i, false);
                }
            }
            else if (!kb_disabled && _lastKbDisabled) {
                // Keyboard was just enabled.
                std::cout << "Bus: Re-enabling keyboard" << std::endl;
            }
            _lastKbDisabled = kb_disabled;
            _lastKbCleared = kb_cleared;
        }

        if ((_ticks & 0x3FFF) == 0) {
            // Slow tick = ~1.144ms. Good for ticking ms-scale delays.

            // Tick the keyboard. The keyboard needs to be ticked to produce reset bytes after a delay when reset,
            // and to produce type-matic repeat keys.
            _kb.tick();
            if (uint8_t b = 0; _kb.getScanCode(b)) {
                // Keyboard-originated scancode (reset byte or type-matic key)
                std::cout << std::format("Keyboard generated scancode: {:02X}", b) << std::endl;
                for (int i = 0; i < 8; ++i) {
                    const auto bit = (b >> i) & 1;
                    _ppi.setA(i, bit != 0);
                }
                _pic.setIRQLine(1, true);
            }

            // Tick the FDC. The FDC needs to be ticked to simulate operational delays.
            _fdc.tick();
        }

        // // Handle FDC interrupts
        // if (_fdc.pollIrq()) {
        //     //std::cout << "FDC IRQ 6 detected\n" << std::flush;
        //     if (!_lastIRQ6) {
        //         std::cout << "FDC IRQ 6 asserted\n" << std::flush;
        //     }
        //     _pic.setIRQLine(6, true);
        //     _lastIRQ6 = true;
        // }
        // else {
        //     _pic.setIRQLine(6, false);
        //     _lastIRQ6 = false;
        // }


        // Set to false to implement 5160s without the U90 fix and 5150s
        // without the U101 fix as described in
        // http://www.vcfed.org/forum/showthread.php?29211-Purpose-of-U90-in-XT-second-revision-board
        bool hasDMACFix = true;

        if (_type != 2 || (_address & 0x3e0) != 0x000 || !hasDMACFix) {
            _lastNonDMAReady = nonDMAReady();
        }
        //if (_previousLock && !_lock)
        //    _previousLock = false;
        //_previousLock = _lock;
        switch (_dmaState) {
            case sIdle:
                if (_dmac.getHoldRequestLine()) {
                    _dmaState = sDREQ;
                }
                break;
            case sDREQ:
                _dmaState = sHRQ; //(_passiveOrHalt && !_previousLock) ? sHRQ : sHoldWait;
                break;
            case sHRQ:
                //_dmaState = _lastNonDMAReady ? sAEN : sPreAEN;
                if ((_passiveOrHalt || _previousPassiveOrHalt) && !_lock && _lastNonDMAReady) {
                    _dmaState = sAEN;
                }
                break;
            //case sHoldWait:
            //    if (_passiveOrHalt && !_previousLock)
            //        _dmaState = _lastNonDMAReady ? sAEN : sPreAEN;
            //    break;
            //case sPreAEN:
            //    if (_lastNonDMAReady)
            //        _dmaState = sAEN;
            //    break;
            case sAEN:
                _dmaState = s0;
                break;
            case s0:
                _dmac.setDMARequestLine(0, false);
                _dmaState = s1;
                break;
            case s1:
                _dmaState = s2;
                break;
            case s2:
                // Device read/write occurs on S2
                if (_dmac.channel() == 2) {
                    // Servicing FDC
                    auto addr = _dmac.address();

                    if (_dmac.isReading()) {
                        std::cout << std::format("DMAC Channel 2 READ from address {:05X}\n", addr);
                    }
                    else if (_dmac.isWriting()) {
                        const auto b = _fdc.dmaDeviceRead();
                        //std::cout << std::format("DMAC Channel 2 WRITE to address {:02X}->{:05X}\n", b, addr);
                        _ram[addr & 0xFFFFF] = b;
                    }
                    _dmac.service();
                    if (_dmac.terminalCount()) {
                        // Notify FDC that DMA operation is complete
                        std::cout << "DMAC Channel 2 terminal count reached, notifying FDC\n";
                        _fdc.dmaDeviceEOP();
                    }

                }
                else {
                    _dmac.service();
                }

                _dmaState = s3;
                break;
            case s3:
                _dmaState = s4;
                break;
            case s4:
                _dmaState = sDelayedT1;
                _dmac.dmaCompleted();
                break;
            case sDelayedT1:
                _dmaState = sDelayedT2;
                _cycle = 0;
                break;
            case sDelayedT2:
                _dmaState = sDelayedT3;
                break;
            case sDelayedT3:
                _dmaState = sIdle;
                break;
            default:
                break;
        }
        _previousPassiveOrHalt = _passiveOrHalt;

        _lastNonDMAReady = nonDMAReady();
        ++_cycle;
    }

    bool ready() {
        return dmaReady() && nonDMAReady();
    }

    void write(uint8_t data) {
        if (_type == 2) {
            // IO write
            switch (_address & 0x3e0) {
                case 0x00:
                    _dmac.write(_address & 0x0f, data);
                    break;
                case 0x20:
                    _pic.write(_address & 1, data);
                    break;
                case 0x40:
                    _pit.write(_address & 3, data);
                    break;
                case 0x60:
                    _ppi.write(_address & 3, data);
                    updatePPI();
                    break;
                case 0x80:
                    _dmaPages[_address & 3] = data;
                    break;
                case 0xa0:
                    _nmiEnabled = (data & 0x80) != 0;
                    break;
                case 0x3C0:
                    _cga.writeIO(_address & 0x0F, data);
                    break;
                case 0x3E0:
                    _fdc.writeIO(_address & 7, data);
                    break;

                default:
                    break;
            }
        }
        else {
            // Memory write
            if (_address < CONVENTIONAL_RAM_SIZE) {
                _ram[_address] = data;
            }
            else if (_address >= CGA_ADDRESS && _address < CGA_ADDRESS + 0x4000) {
                _cga.writeMem(_address - CGA_ADDRESS, data);
            }
        }
    }

    uint8_t read() {
        if (_type == 0) {
            // Interrupt acknowledge
            auto i = _pic.interruptAcknowledge();
            if (i != 0xFF && i != 0x08) {
                std::cout << "Interrupt acknowledge: vector " << std::hex << static_cast<int>(i) << std::dec << "\n" <<
                    std::flush;
            }
            return i;
        }
        if (_type == 1) {
            // IO read

            // noisy trace debu
            //std::cout << "IO read from port " << std::hex << _address << std::dec << "\n" << std::flush;

            // Read from IO port
            switch (_address & 0x3e0) {
                case 0x00:
                    return _dmac.read(_address & 0x0f);
                case 0x20:
                    return _pic.read(_address & 1);
                case 0x40:
                {
                    const uint8_t b = _pit.read(_address & 3);
                    // std::cout << "PIT read from port " << std::hex << (_address & 3)
                    //     << ": " << std::hex << static_cast<int>(b) << std::dec << "\n";
                    return b;
                }
                case 0x60:
                {
                    //std::cout << "PPI read from port " << std::hex << (_address & 3) << std::dec << "\n";
                    const uint8_t b = _ppi.read(_address & 3);
                    updatePPI();
                    return b;
                }
                case 0x3C0:
                    return _cga.readIO(_address & 0x0F);
                case 0x3E0:
                    return _fdc.readIO(_address & 7);
                default:
                    //std::cout << "Unhandled IO read from port " << std::hex << _address << std::dec << "\n";
                    return 0xFF;
            }
        }

        if (_address < CONVENTIONAL_RAM_SIZE) {
            return _ram[_address];
        }
        if (_address >= ROM_BASE_ADDRESS) {
            // Read from ROM.
            return _rom[_address - ROM_BASE_ADDRESS];
        }
        if (_address >= CGA_ADDRESS && _address < CGA_ADDRESS + 0x4000) {
            return _cga.readMem(_address - CGA_ADDRESS);
        }
        // No match? Return open bus.
        return 0xFF;
    }

    bool interruptPending() { return _pic.interruptPending(); }

    int pitBits() {
        return (_pitPhase == 1 || _pitPhase == 2 ? 1 : 0) +
            (_counter2Gate ? 2 : 0) + (_pit.getOutput(2) ? 4 : 0);
    }

    void setPassiveOrHalt(bool v) { _passiveOrHalt = v; }

    [[nodiscard]] bool getAEN() const {
        return _dmaState == sAEN || _dmaState == s0 || _dmaState == s1 ||
            _dmaState == s2 || _dmaState == s3 || _dmaState == sWait ||
            _dmaState == s4;
    }

    uint8_t getDMA() {
        return _dmac.getRequestLines() | (dack0() ? 0x10 : 0);
    }

    std::string snifferExtra() {
        return ""; //hex(_pit.getMode(1), 4, false) + " ";
    }

    [[nodiscard]] int getBusOperation() const {
        switch (_dmaState) {
            case s2:
                return 5; // memr
            case s3:
                return 2; // iow
            default:
                return 0;
        }
    }

    bool getDMAS3() { return _dmaState == s3; }
    bool getDMADelayedT2() { return _dmaState == sDelayedT2; }

    uint32_t getDMAAddress() {
        return dmaAddressHigh(_dmac.channel()) + _dmac.address();
    }

    void setLock(bool lock) { _lock = lock; }
    uint8_t getIRQLines() { return _pic.getIRQLines(); }

    uint8_t getDMAS() {
        if (_dmaState == sAEN || _dmaState == s0 || _dmaState == s1 ||
            _dmaState == s2 || _dmaState == s3 || _dmaState == sWait)
            return 3;
        if (_dmaState == sHRQ || _dmaState == sHoldWait ||
            _dmaState == sPreAEN)
            return 1;
        return 0;
    }

    uint8_t getCGA() {
        return _cgaPhase >> 2;
    }

private:
    bool dmaReady() {
        if (_dmaState == s1 || _dmaState == s2 || _dmaState == s3 ||
            _dmaState == sWait || _dmaState == s4 || _dmaState == sDelayedT1 ||
            _dmaState == sDelayedT2 /*|| _dmaState == sDelayedT3*/)
            return false;
        return true;
    }

    bool nonDMAReady() {
        if (_type == 1 || _type == 2) // Read port, write port
            return _cycle > 2; // System board adds a wait state for onboard IO devices
        return true;
    }

    bool dack0() {
        return _dmaState == s1 || _dmaState == s2 || _dmaState == s3 ||
            _dmaState == sWait;
    }

    void setSpeakerOutput() {
        bool o = !(_counter2Output && _speakerMask);
        if (_nextSpeakerOutput != o) {
            if (_speakerOutput == o)
                _speakerCycle = 0;
            else
                _speakerCycle = o ? 3 : 2;
            _nextSpeakerOutput = o;
        }
    }

    void updatePPI() {
        bool speakerMask = _ppi.getB(1);
        if (speakerMask != _speakerMask) {
            _speakerMask = speakerMask;
            setSpeakerOutput();
        }
        _counter2Gate = _ppi.getB(0);
        _pit.setGate(2, _counter2Gate);

        if (!_ppi.getB(3)) {
            // Present switches 1 to 4
            _ppi.setC(0, (_dipSwitch1 & 0x01) != 0);
            _ppi.setC(1, (_dipSwitch1 & 0x02) != 0);
            _ppi.setC(2, (_dipSwitch1 & 0x04) != 0);
            _ppi.setC(3, (_dipSwitch1 & 0x08) != 0);
        }
        else {
            // Present switches 5 to 8
            _ppi.setC(0, (_dipSwitch1 & 0x10) != 0);
            _ppi.setC(1, (_dipSwitch1 & 0x20) != 0);
            _ppi.setC(2, (_dipSwitch1 & 0x40) != 0);
            _ppi.setC(3, (_dipSwitch1 & 0x80) != 0);
        }

    }

    uint32_t dmaAddressHigh(int channel) {
        static const int pageRegister[4] = {0x83, 0x83, 0x81, 0x82};
        return _dmaPages[pageRegister[channel]] << 16;
    }

    enum DMAState
    {
        sIdle,
        sDREQ,
        sHRQ,
        sHoldWait,
        sPreAEN,
        sAEN,
        s0,
        s1,
        s2,
        s3,
        sWait,
        s4,
        sDelayedT1,
        sDelayedT2,
        sDelayedT3,
    };

    std::vector<uint8_t> _ram;
    std::vector<uint8_t> _rom;
    uint32_t _address;
    int _type;
    int _cycle;
    DMAC _dmac;
    PIC _pic;
    PIT _pit;
    PPI _ppi;
    CGA _cga;
    FDC _fdc;
    Keyboard _kb;
    uint8_t _dipSwitch1{0b0010'1101};
    int _pitPhase;
    bool _lastCounter0Output;
    bool _lastIRQ6{false};
    bool _lastCounter1Output;
    bool _counter2Output;
    bool _counter2Gate;
    bool _speakerMask;
    bool _speakerOutput;
    bool _nextSpeakerOutput;
    uint16_t _dmaAddress;
    int _dmaCycles;
    int _dmaType;
    int _speakerCycle;
    uint8_t _dmaPages[4];
    bool _nmiEnabled;
    bool _passiveOrHalt;
    DMAState _dmaState;
    bool _lock;
    bool _previousPassiveOrHalt;
    bool _lastNonDMAReady;
    uint8_t _cgaPhase;
    bool _lastKbDisabled{false};
    bool _lastKbCleared{false};
    uint64_t _ticks{0};
};
