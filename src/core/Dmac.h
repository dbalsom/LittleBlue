#pragma once

#include <cstdint>
#include <array>

class DMAC
{
public:
    // Snapshot types for debugging: lightweight copies of channel state and top-level registers.
    struct ChannelSnapshot
    {
        uint16_t baseAddress{};
        uint16_t baseWordCount{};
        uint16_t currentAddress{};
        uint16_t currentWordCount{};
        uint8_t mode{};
    };

    struct DMADebugStatus
    {
        std::array<ChannelSnapshot, 4> channels{};
        uint8_t status{};
        uint8_t command{};
        uint8_t request{};
        uint8_t mask{};
        uint8_t ack{};
    };

    [[nodiscard]] DMADebugStatus getDMADebugStatus() const {
        DMADebugStatus s;
        for (int i = 0; i < 4; ++i) {
            const auto& c = _channels[i];
            s.channels[i].baseAddress = c._baseAddress;
            s.channels[i].baseWordCount = c._baseWordCount;
            s.channels[i].currentAddress = c._currentAddress;
            s.channels[i].currentWordCount = c._currentWordCount;
            s.channels[i].mode = c._mode;
        }
        s.status = _status;
        s.command = _command;
        s.request = _request;
        s.mask = _mask;
        s.ack = _channel == -1 ? 0x00 : 0x01 << _channel;
        return s;
    }

    void reset() {
        for (auto& channel : _channels) {
            channel.reset();
        }
        _temporaryAddress = 0;
        _temporaryWordCount = 0;
        _status = 0;
        _command = 0;
        _temporary = 0;
        _mask = 0xf;
        _request = 0;
        _ack = 0;
        _flipFlop = false;
        _channel = -1;
        _needHighAddress = true;
    }

    void write(const uint32_t address, const uint8_t data) {
        switch (address) {
            case 0x00:
            case 0x02:
            case 0x04:
            case 0x06:
                _channels[(address & 6) >> 1].setAddress(_flipFlop, data);
                _flipFlop = !_flipFlop;
                break;
            case 0x01:
            case 0x03:
            case 0x05:
            case 0x07:
                _channels[(address & 6) >> 1].setCount(_flipFlop, data);
                _flipFlop = !_flipFlop;
                break;
            case 0x08:
                // Write Command Register
                _command = data;
                break;
            case 0x09:
                // Write Request Register
                setRequest(data & 3, (data & 4) != 0);
                break;
            case 0x0a:
                // Write Single Mask Register Bit
            {
                uint8_t b = 1 << (data & 3);
                if ((data & 4) != 0)
                    _mask |= b;
                else
                    _mask &= ~b;
            }
            break;
            case 0x0b:
                // Write Mode Register
                _channels[data & 3]._mode = data;
                break;
            case 0x0c:
                // Clear Byte Pointer Flip/Flop
                _flipFlop = false;
                break;
            case 0x0d:
                // Master Clear
                reset();
                break;
            case 0x0e:
                // Clear Mask Register
                _mask = 0;
                break;
            case 0x0f:
                // Write All Mask Register Bits
                _mask = data;
                break;
            default:
                break;
        }
    }

    uint8_t read(const uint32_t address) {
        switch (address) {
            case 0x00:
            case 0x02:
            case 0x04:
            case 0x06:
                _flipFlop = !_flipFlop;
                return _channels[(address & 6) >> 1].getAddress(!_flipFlop);
            case 0x01:
            case 0x03:
            case 0x05:
            case 0x07:
                _flipFlop = !_flipFlop;
                return _channels[(address & 6) >> 1].getCount(!_flipFlop);
            case 0x08:
                // Read Status Register
                return _status;
            case 0x0d:
                // Read Temporary Register
                return _temporary;
            default:
                // Illegal
                return 0xff;
        }
    }

    void setDMARequestLine(int line, bool state) {
        setRequest(line, state != dreqSenseActiveLow());
    }

    [[nodiscard]] uint8_t getRequestLines() const {
        return _request;
    }

    bool getHoldRequestLine() {
        if (_channel != -1) {
            return true;
        }
        if (disabled()) {
            return false;
        }
        for (int i = 0; i < 4; ++i) {
            int channel = i;
            if (rotatingPriority()) {
                channel = (channel + _priorityChannel) & 3;
            }
            if ((_request & (1 << channel)) != 0) {
                _channel = channel;
                _priorityChannel = (channel + 1) & 3;
                return true;
            }
        }
        return false;
    }

    void dmaCompleted() {
        _channel = -1;
    }

    uint8_t dmaRead() {
        if (memoryToMemory() && _channel == 1) {
            return _temporary;
        }
        return 0xff;
    }

    void dmaWrite(const uint8_t data) {
        if (memoryToMemory() && _channel == 0) {
            _temporary = data;
        }
    }

    bool isReading() {
        if (_channel > 0) {
            auto& c = _channels[_channel & 3];

            return c.read();
        }
        return false;
    }

    bool isWriting() {
        if (_channel > 0) {
            auto& c = _channels[_channel & 3];

            return c.write();
        }
        return false;
    }

    [[nodiscard]] uint16_t address() const {
        if (_channel == -1) {
            return 0;
        }
        const uint16_t address = _channels[_channel]._currentAddress;
        return address;
    }

    uint16_t service() {
        if (_channel == -1) {
            return 0;
        }
        auto& c = _channels[_channel & 3];

        if (!c.terminalCount() || c.autoinitializate()) {
            c.incrementAddress();
        }
        if (c.terminalCount()) {
            _status |= 0x01 << _channel;
        }

        return c._currentAddress;
    }

    [[nodiscard]] bool terminalCount() const {
        if (_channel == -1) {
            return false;
        }
        return _channels[_channel & 3].terminalCount();
    }

    [[nodiscard]] int channel() const {
        return _channel;
    }

private:
    struct Channel
    {
        void setAddress(const bool high, const uint8_t data) {
            if (!high) {
                _baseAddress = (_baseAddress & 0xff00) + data;
                _currentAddress = (_currentAddress & 0xff00) + data;
            }
            else {
                _baseAddress = (_baseAddress & 0xff) + (data << 8);
                _currentAddress = (_currentAddress & 0xff) + (data << 8);
            }
        }

        void setCount(const bool high, uint8_t data) {
            _tc = false;
            if (!high) {
                _baseWordCount = (_baseWordCount & 0xff00) + data;
                _currentWordCount = (_currentWordCount & 0xff00) + data;
            }
            else {
                _baseWordCount = (_baseWordCount & 0xff) + (data << 8);
                _currentWordCount = (_currentWordCount & 0xff) + (data << 8);
            }
        }

        [[nodiscard]] uint8_t getAddress(const bool high) const {
            if (high) {
                return _currentAddress >> 8;
            }
            return _currentAddress & 0xff;
        }

        [[nodiscard]] uint8_t getCount(const bool high) const {
            if (high) {
                return _currentWordCount >> 8;
            }
            return _currentWordCount & 0xff;
        }

        void reset() {
            _baseAddress = 0;
            _baseWordCount = 0;
            _currentAddress = 0;
            _currentWordCount = 0;
            _mode = 0;
        }

        void incrementAddress() {
            if (!addressDecrement()) {
                ++_currentAddress;
            }
            else {
                --_currentAddress;
            }
            --_currentWordCount;
            if (_currentWordCount == 0xFFFF) {
                // We allow the word count to roll over because we do a transfer on a 0 count.
                // We've hit terminal count at this point.
                _tc = true;

                // Now we just need to handle autoinitialization, or reset the word count to 0.
                if (autoinitializate()) {
                    // It may seem counterintuitive, but the TC flag is not reset by auto-initialization.
                    _currentAddress = _baseAddress;
                    _currentWordCount = _baseWordCount;
                }
                else {
                    _currentWordCount = 0;
                }
            }
        }

        [[nodiscard]] bool write() const { return (_mode & 0x0c) == 4; }
        [[nodiscard]] bool read() const { return (_mode & 0x0c) == 8; }
        [[nodiscard]] bool verify() const { return (_mode & 0x0c) == 0; }
        [[nodiscard]] bool autoinitializate() const { return (_mode & 0x10) != 0; }
        [[nodiscard]] bool addressDecrement() const { return (_mode & 0x20) != 0; }
        [[nodiscard]] bool demand() const { return (_mode & 0xc0) == 0x00; }
        [[nodiscard]] bool single() const { return (_mode & 0xc0) == 0x40; }
        [[nodiscard]] bool block() const { return (_mode & 0xc0) == 0x80; }
        [[nodiscard]] bool cascade() const { return (_mode & 0xc0) == 0xc0; }
        [[nodiscard]] bool terminalCount() const { return _tc; }

        uint16_t _baseAddress;
        uint16_t _baseWordCount;
        uint16_t _currentAddress;
        uint16_t _currentWordCount;
        uint8_t _mode; // Only 6 bits used
        bool _tc = false;
    };

    bool memoryToMemory() { return (_command & 1) != 0; }
    bool channel0AddressHold() { return (_command & 2) != 0; }
    bool disabled() { return (_command & 4) != 0; }
    bool compressedTiming() { return (_command & 8) != 0; }
    bool rotatingPriority() { return (_command & 0x10) != 0; }
    bool extendedWriteSelection() { return (_command & 0x20) != 0; }


    bool dreqSenseActiveLow() { return (_command & 0x40) != 0; }
    bool dackSenseActiveHigh() { return (_command & 0x80) != 0; }

    void setRequest(const int line, const bool active) {
        const uint8_t b = 1 << line;
        const uint8_t s = 0x10 << line;
        if (active) {
            _request |= b;
            _status |= s;
        }
        else {
            _request &= ~b;
            _status &= ~s;
        }
    }

    Channel _channels[4] = {};
    uint16_t _temporaryAddress = 0;
    uint16_t _temporaryWordCount = 0;
    uint8_t _status = 0;
    uint8_t _command = 0;
    uint8_t _temporary = 0;
    uint8_t _mask = 0; // Only 4 bits used
    uint8_t _request = 0; // Only 4 bits used
    uint8_t _ack = 0; // Only 4 bits used
    bool _flipFlop = false;
    int _channel = 0;
    int _priorityChannel = 0;
    bool _needHighAddress = false;
};
