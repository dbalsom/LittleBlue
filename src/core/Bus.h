#ifndef BUS_H
#define BUS_H

#include <cstdint>
#include <vector>

#include "bios.h"
#include "Dmac.h"
#include "Pic.h"
#include "Pit.h"
#include "Ppi.h"

#define ROM_BASE_ADDRESS 0xFE000

class Bus
{
public:
    Bus() : _ram(0xa0000), _rom(0x2000)
    {
        _rom.assign(U18, U18 + sizeof(U18));
        _pit.setGate(0, true);
        _pit.setGate(1, true);
        _pit.setGate(2, true);
    }
    uint8_t* ram() { return &_ram[0]; }
    void reset()
    {
        _dmac.reset();
        _pic.reset();
        _pit.reset();
        _ppi.reset();
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
    }
    void stubInit()
    {
        _pic.stubInit();
        _pit.stubInit();
        _pitPhase = 2;
        _lastCounter0Output = true;
    }
    void startAccess(uint32_t address, int type)
    {
        _address = address;
        _type = type;
        _cycle = 0;
    }
    void wait()
    {
        _cgaPhase = (_cgaPhase + 3) & 0x0f;
        ++_pitPhase;
        if (_pitPhase == 4) {
            _pitPhase = 0;
            _pit.wait();
            bool counter0Output = _pit.getOutput(0);
            if (_lastCounter0Output != counter0Output)
                _pic.setIRQLine(0, counter0Output);
            _lastCounter0Output = counter0Output;
            bool counter1Output = _pit.getOutput(1);
            if (counter1Output && !_lastCounter1Output && !dack0()) {
                _dmaRequests |= 1;
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
                if (_dmac.getHoldRequestLine())
                    _dmaState = sDREQ;
                break;
            case sDREQ:
                _dmaState = sHRQ; //(_passiveOrHalt && !_previousLock) ? sHRQ : sHoldWait;
                break;
            case sHRQ:
                //_dmaState = _lastNonDMAReady ? sAEN : sPreAEN;
                if ((_passiveOrHalt || _previousPassiveOrHalt) && !_lock && _lastNonDMAReady)
                    _dmaState = sAEN;
                break;
            //case sHoldWait:
            //    if (_passiveOrHalt && !_previousLock)
            //        _dmaState = _lastNonDMAReady ? sAEN : sPreAEN;
            //    break;
            //case sPreAEN:
            //    if (_lastNonDMAReady)
            //        _dmaState = sAEN;
            //    break;
            case sAEN: _dmaState = s0; break;
            case s0:
                if ((_dmaRequests & 1) != 0) {
                    _dmaRequests &= 0xfe;
                    _dmac.setDMARequestLine(0, false);
                }
                _dmaState = s1; break;
            case s1: _dmaState = s2; break;
            case s2: _dmaState = s3; break;
            case s3: _dmaState = s4; break;
            case s4: _dmaState = sDelayedT1; _dmac.dmaCompleted(); break;
            case sDelayedT1: _dmaState = sDelayedT2; _cycle = 0; break;
            case sDelayedT2: _dmaState = sDelayedT3; break;
            case sDelayedT3: _dmaState = sIdle; break;
        }
        _previousPassiveOrHalt = _passiveOrHalt;

        _lastNonDMAReady = nonDMAReady();
        ++_cycle;
    }
    bool ready()
    {
        return dmaReady() && nonDMAReady();
    }
    void write(uint8_t data)
    {
        if (_type == 2) {
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
            }
        }
        else
            if (_address < 0xa0000)
                _ram[_address] = data;
    }
    uint8_t read()
    {
        if (_type == 0) {
            // Interrupt acknowledge
            return _pic.interruptAcknowledge();
        }
        if (_type == 1) {
            // Read from IO port
            switch (_address & 0x3e0) {
                case 0x00: return _dmac.read(_address & 0x0f);
                case 0x20: return _pic.read(_address & 1);
                case 0x40: return _pit.read(_address & 3);
                case 0x60:
                    {
                        uint8_t b = _ppi.read(_address & 3);
                        updatePPI();
                        return b;
                    }

            }
            // Default: return open bus.
            return 0xff;
        }
        if (_address >= ROM_BASE_ADDRESS) {
            // Read from ROM.
            return _rom[_address - ROM_BASE_ADDRESS];
        }
        if (_address >= 0xa0000) {
            // Return open bus.
            return 0xff;
        }
        // Default state: return RAM contents.
        return _ram[_address];
    }
    bool interruptPending() { return _pic.interruptPending(); }
    int pitBits()
    {
        return (_pitPhase == 1 || _pitPhase == 2 ? 1 : 0) +
            (_counter2Gate ? 2 : 0) + (_pit.getOutput(2) ? 4 : 0);
    }
    void setPassiveOrHalt(bool v) { _passiveOrHalt = v; }
    bool getAEN()
    {
        return _dmaState == sAEN || _dmaState == s0 || _dmaState == s1 ||
            _dmaState == s2 || _dmaState == s3 || _dmaState == sWait ||
            _dmaState == s4;
    }
    uint8_t getDMA()
    {
        return _dmaRequests | (dack0() ? 0x10 : 0);
    }
    std::string snifferExtra()
    {
        return ""; //hex(_pit.getMode(1), 4, false) + " ";
    }
    int getBusOperation()
    {
        switch (_dmaState) {
            case s2: return 5;  // memr
            case s3: return 2;  // iow
        }
        return 0;
    }
    bool getDMAS3() { return _dmaState == s3; }
    bool getDMADelayedT2() { return _dmaState == sDelayedT2; }
    uint32_t getDMAAddress()
    {
        return dmaAddressHigh(_dmac.channel()) + _dmac.address();
    }
    void setLock(bool lock) { _lock = lock; }
    uint8_t getIRQLines() { return _pic.getIRQLines(); }
    uint8_t getDMAS()
    {
        if (_dmaState == sAEN || _dmaState == s0 || _dmaState == s1 ||
            _dmaState == s2 || _dmaState == s3 || _dmaState == sWait)
            return 3;
        if (_dmaState == sHRQ || _dmaState == sHoldWait ||
            _dmaState == sPreAEN)
            return 1;
        return 0;
    }
    uint8_t getCGA()
    {
        return _cgaPhase >> 2;
    }
private:
    bool dmaReady()
    {
        if (_dmaState == s1 || _dmaState == s2 || _dmaState == s3 ||
            _dmaState == sWait || _dmaState == s4 || _dmaState == sDelayedT1 ||
            _dmaState == sDelayedT2 /*|| _dmaState == sDelayedT3*/)
            return false;
        return true;
    }
    bool nonDMAReady()
    {
        if (_type == 1 || _type == 2)  // Read port, write port
            return _cycle > 2;  // System board adds a wait state for onboard IO devices
        return true;
    }
    bool dack0()
    {
        return _dmaState == s1 || _dmaState == s2 || _dmaState == s3 ||
            _dmaState == sWait;
    }
    void setSpeakerOutput()
    {
        bool o = !(_counter2Output && _speakerMask);
        if (_nextSpeakerOutput != o) {
            if (_speakerOutput == o)
                _speakerCycle = 0;
            else
                _speakerCycle = o ? 3 : 2;
            _nextSpeakerOutput = o;
        }
    }
    void updatePPI()
    {
        bool speakerMask = _ppi.getB(1);
        if (speakerMask != _speakerMask) {
            _speakerMask = speakerMask;
            setSpeakerOutput();
        }
        _counter2Gate = _ppi.getB(0);
        _pit.setGate(2, _counter2Gate);
    }
    uint32_t dmaAddressHigh(int channel)
    {
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
    int _pitPhase;
    bool _lastCounter0Output;
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
    uint8_t _dmaRequests;
    bool _lock;
    bool _previousPassiveOrHalt;
    bool _lastNonDMAReady;
    uint8_t _cgaPhase;
};

#endif