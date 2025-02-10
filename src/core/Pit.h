#ifndef PIT_H
#define PIT_H
#include <cstdint>

class PIT
{
public:
    void reset()
    {
        for (int i = 0; i < 3; ++i)
            _counters[i].reset();
    }
    void stubInit()
    {
        for (int i = 0; i < 3; ++i)
            _counters[i].stubInit();
    }
    void write(int address, uint8_t data)
    {
        if (address == 3) {
            int counter = data >> 6;
            if (counter == 3)
                return;
            _counters[counter].control(data & 0x3f);
        }
        else
            _counters[address].write(data);
    }
    uint8_t read(int address)
    {
        if (address == 3)
            return 0xff;
        return _counters[address].read();
    }
    void wait()
    {
        for (int i = 0; i < 3; ++i)
            _counters[i].wait();
    }
    void setGate(int counter, bool gate)
    {
        _counters[counter].setGate(gate);
    }
    bool getOutput(int counter) { return _counters[counter]._output; }
    //int getMode(int counter) { return _counters[counter]._control; }
private:
    enum State
    {
        stateWaitingForCount,
        stateCounting,
        stateWaitingForGate,
        stateGateRose,
        stateLoadDelay,
        statePulsing
    };
    struct Counter
    {
        void reset()
        {
            _value = 0;
            _count = 0;
            _firstByte = true;
            _latched = false;
            _output = true;
            _control = 0x30;
            _state = stateWaitingForCount;
        }
        void stubInit()
        {
            _value = 0xffff;
            _count = 0xffff;
            _firstByte = true;
            _latched = false;
            _output = true;
            _control = 0x34;
            _state = stateCounting;
            _gate = true;
        }
        void write(uint8_t data)
        {
            _writeByte = data;
            _haveWriteByte = true;
        }
        uint8_t read()
        {
            if (!_latched) {
                // TODO: corrupt countdown in a deterministic but
                // non-trivial way.
                _latch = _count;
            }
            switch (_control & 0x30) {
                case 0x10:
                    _latched = false;
                    return _latch & 0xff;
                case 0x20:
                    _latched = false;
                    return _latch >> 8;
                case 0x30:
                    if (_firstByte) {
                        _firstByte = false;
                        return _latch & 0xff;
                    }
                    _firstByte = true;
                    _latched = false;
                    return _latch >> 8;
            }
            // This should never happen.
            return 0;
        }
        void wait()
        {
            switch (_control & 0x0e) {
                case 0x00:  // Interrupt on Terminal Count
                    if (_state == stateLoadDelay) {
                        _state = stateCounting;
                        _value = _count;
                        break;
                    }
                    if (_gate && _state == stateCounting) {
                        countDown();
                        if (_value == 0)
                            _output = true;
                    }
                    break;
                case 0x02:  // Programmable One-Shot
                    if (_state == stateLoadDelay) {
                        _state = stateWaitingForGate;
                        break;
                    }
                    if (_state == stateGateRose) {
                        _output = false;
                        _value = _count;
                        _state = stateCounting;
                    }
                    countDown();
                    if (_value == 0) {
                        _output = true;
                        _state = stateWaitingForGate;
                    }
                    break;
                case 0x04:
                case 0x0c:  // Rate Generator
                    if (_state == stateLoadDelay) {
                        _state = stateCounting;
                        _value = _count;
                        break;
                    }
                    if (_gate && _state == stateCounting) {
                        countDown();
                        if (_value == 1)
                            _output = false;
                        if (_value == 0) {
                            _output = true;
                            _value = _count;
                        }
                    }
                    break;
                case 0x06:
                case 0x0e:  // Square Wave Rate Generator
                    if (_state == stateLoadDelay) {
                        _state = stateCounting;
                        _value = _count;
                        break;
                    }
                    if (_gate && _state == stateCounting) {
                        if ((_value & 1) != 0) {
                            if (!_output) {
                                countDown();
                                countDown();
                            }
                        }
                        else
                            countDown();
                        countDown();
                        if (_value == 0) {
                            _output = !_output;
                            _value = _count;
                        }
                    }
                    break;
                case 0x08:  // Software Triggered Strobe
                    if (_state == stateLoadDelay) {
                        _state = stateCounting;
                        _value = _count;
                        break;
                    }
                    if (_state == statePulsing) {
                        _output = true;
                        _state = stateWaitingForCount;
                    }
                    if (_gate && _state == stateCounting) {
                        countDown();
                        if (_value == 0) {
                            _output = false;
                            _state = statePulsing;
                        }
                    }
                    break;
                case 0x0a:  // Hardware Triggered Strobe
                    if (_state == stateLoadDelay) {
                        _state = stateWaitingForGate;
                        break;
                    }
                    if (_state == statePulsing) {
                        _output = true;
                        _state = stateWaitingForCount;
                    }
                    if (_state == stateGateRose) {
                        _output = false;
                        _value = _count;
                        _state = stateCounting;
                    }
                    if (_state == stateCounting) {
                        countDown();
                        if (_value == 1)
                            _output = false;
                        if (_value == 0) {
                            _output = true;
                            _state = stateWaitingForGate;
                        }
                    }
                    break;
            }
            if (_haveWriteByte) {
                _haveWriteByte = false;
                switch (_control & 0x30) {
                    case 0x10:
                        load(_writeByte);
                        break;
                    case 0x20:
                        load(_writeByte << 8);
                        break;
                    case 0x30:
                        if (_firstByte) {
                            _lowByte = _writeByte;
                            _firstByte = false;
                        }
                        else {
                            load((_writeByte << 8) + _lowByte);
                            _firstByte = true;
                        }
                        break;
                }
            }
        }
        void countDown()
        {
            if ((_control & 1) == 0) {
                --_value;
                return;
            }
            if ((_value & 0xf) != 0) {
                --_value;
                return;
            }
            if ((_value & 0xf0) != 0) {
                _value -= (0x10 - 9);
                return;
            }
            if ((_value & 0xf00) != 0) {
                _value -= (0x100 - 0x99);
                return;
            }
            _value -= (0x1000 - 0x999);
        }
        void load(uint16_t count)
        {
            _count = count;
            switch (_control & 0x0e) {
                case 0x00:  // Interrupt on Terminal Count
                    if (_state == stateWaitingForCount)
                        _state = stateLoadDelay;
                    _output = false;
                    break;
                case 0x02:  // Programmable One-Shot
                    if (_state != stateCounting)
                        _state = stateLoadDelay;
                    break;
                case 0x04:
                case 0x0c:  // Rate Generator
                    if (_state == stateWaitingForCount)
                        _state = stateLoadDelay;
                    break;
                case 0x06:
                case 0x0e:  // Square Wave Rate Generator
                    if (_state == stateWaitingForCount)
                        _state = stateLoadDelay;
                    break;
                case 0x08:  // Software Triggered Strobe
                    if (_state == stateWaitingForCount)
                        _state = stateLoadDelay;
                    break;
                case 0x0a:  // Hardware Triggered Strobe
                    if (_state != stateCounting)
                        _state = stateLoadDelay;
                    break;
            }
        }
        void control(uint8_t control)
        {
            int command = control & 0x30;
            if (command == 0) {
                _latch = _value;
                _latched = true;
                return;
            }
            _control = control;
            _firstByte = true;
            _latched = false;
            _state = stateWaitingForCount;
            switch (_control & 0x0e) {
                case 0x00:  // Interrupt on Terminal Count
                    _output = false;
                    break;
                case 0x02:  // Programmable One-Shot
                    _output = true;
                    break;
                case 0x04:
                case 0x0c:  // Rate Generator
                    _output = true;
                    break;
                case 0x06:
                case 0x0e:  // Square Wave Rate Generator
                    _output = true;
                    break;
                case 0x08:  // Software Triggered Strobe
                    _output = true;
                    break;
                case 0x0a:  // Hardware Triggered Strobe
                    _output = true;
                    break;
            }
        }
        void setGate(bool gate)
        {
            if (_gate == gate)
                return;
            switch (_control & 0x0e) {
                case 0x00:  // Interrupt on Terminal Count
                    break;
                case 0x02:  // Programmable One-Shot
                    if (gate)
                        _state = stateGateRose;
                    break;
                case 0x04:
                case 0x0c:  // Rate Generator
                    if (!gate)
                        _output = true;
                    else
                        _value = _count;
                    break;
                case 0x06:
                case 0x0e:  // Square Wave Rate Generator
                    if (!gate)
                        _output = true;
                    else
                        _value = _count;
                    break;
                case 0x08:  // Software Triggered Strobe
                    break;
                case 0x0a:  // Hardware Triggered Strobe
                    if (gate)
                        _state = stateGateRose;
                    break;
            }
            _gate = gate;
        }

        uint16_t _count;
        uint16_t _value;
        uint16_t _latch;
        uint8_t _control;
        uint8_t _lowByte;
        bool _gate;
        bool _output;
        bool _firstByte;
        bool _latched;
        State _state;
        uint8_t _writeByte;
        bool _haveWriteByte;
    };

    Counter _counters[3];
};

#endif //PIT_H
