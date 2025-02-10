#ifndef DMAC_H
#define DMAC_H

class DMAC
{
public:
    void reset()
    {
        for (int i = 0; i < 4; ++i)
            _channels[i].reset();
        _temporaryAddress = 0;
        _temporaryWordCount = 0;
        _status = 0;
        _command = 0;
        _temporary = 0;
        _mask = 0xf;
        _request = 0;
        _high = false;
        _channel = -1;
        _needHighAddress = true;
    }
    void write(int address, uint8_t data)
    {
        switch (address) {
            case 0x00: case 0x02: case 0x04: case 0x06:
                _channels[(address & 6) >> 1].setAddress(_high, data);
                _high = !_high;
                break;
            case 0x01: case 0x03: case 0x05: case 0x07:
                _channels[(address & 6) >> 1].setCount(_high, data);
                _high = !_high;
                break;
            case 0x08:  // Write Command Register
                _command = data;
                break;
            case 0x09:  // Write Request Register
                setRequest(data & 3, (data & 4) != 0);
                break;
            case 0x0a:  // Write Single Mask Register Bit
                {
                    uint8_t b = 1 << (data & 3);
                    if ((data & 4) != 0)
                        _mask |= b;
                    else
                        _mask &= ~b;
                }
                break;
            case 0x0b:  // Write Mode Register
                _channels[data & 3]._mode = data;
                break;
            case 0x0c:  // Clear Byte Pointer Flip/Flop
                _high = false;
                break;
            case 0x0d:  // Master Clear
                reset();
                break;
            case 0x0e:  // Clear Mask Register
                _mask = 0;
                break;
            case 0x0f:  // Write All Mask Register Bits
                _mask = data;
                break;
        }
    }
    uint8_t read(int address)
    {
        switch (address) {
            case 0x00: case 0x02: case 0x04: case 0x06:
                _high = !_high;
                return _channels[(address & 6) >> 1].getAddress(!_high);
            case 0x01: case 0x03: case 0x05: case 0x07:
                _high = !_high;
                return _channels[(address & 6) >> 1].getCount(!_high);
            case 0x08:  // Read Status Register
                return _status;
                break;
            case 0x0d:  // Read Temporary Register
                return _temporary;
                break;
            default:  // Illegal
                return 0xff;
        }
    }
    void setDMARequestLine(int line, bool state)
    {
        setRequest(line, state != dreqSenseActiveLow());
    }
    bool getHoldRequestLine()
    {
        if (_channel != -1)
            return true;
        if (disabled())
            return false;
        for (int i = 0; i < 4; ++i) {
            int channel = i;
            if (rotatingPriority())
                channel = (channel + _priorityChannel) & 3;
            if ((_request & (1 << channel)) != 0) {
                _channel = channel;
                _priorityChannel = (channel + 1) & 3;
                return true;
            }
        }
        return false;
    }
    void dmaCompleted() { _channel = -1; }
    uint8_t dmaRead()
    {
        if (memoryToMemory() && _channel == 1)
            return _temporary;
        return 0xff;
    }
    void dmaWrite(uint8_t data)
    {
        if (memoryToMemory() && _channel == 0)
            _temporary = data;
    }
    uint16_t address()
    {
        uint16_t address = _channels[_channel]._currentAddress;
        _channels[_channel].incrementAddress();
        return address;
    }
    int channel() { return _channel; }
private:
    struct Channel
    {
        void setAddress(bool high, uint8_t data)
        {
            if (high) {
                _baseAddress = (_baseAddress & 0xff00) + data;
                _currentAddress = (_currentAddress & 0xff00) + data;
            }
            else {
                _baseAddress = (_baseAddress & 0xff) + (data << 8);
                _currentAddress = (_currentAddress & 0xff) + (data << 8);
            }
        }
        void setCount(bool high, uint8_t data)
        {
            if (high) {
                _baseWordCount = (_baseWordCount & 0xff00) + data;
                _currentWordCount = (_currentWordCount & 0xff00) + data;
            }
            else {
                _baseWordCount = (_baseWordCount & 0xff) + (data << 8);
                _currentWordCount = (_currentWordCount & 0xff) + (data << 8);
            }
        }
        uint8_t getAddress(bool high)
        {
            if (high)
                return _currentAddress >> 8;
            else
                return _currentAddress & 0xff;
        }
        uint8_t getCount(bool high)
        {
            if (high)
                return _currentWordCount >> 8;
            else
                return _currentWordCount & 0xff;
        }
        void reset()
        {
            _baseAddress = 0;
            _baseWordCount = 0;
            _currentAddress = 0;
            _currentWordCount = 0;
            _mode = 0;
        }
        void incrementAddress()
        {
            if (!addressDecrement())
                ++_currentAddress;
            else
                --_currentAddress;
            --_currentWordCount;
        }
        bool write() { return (_mode & 0x0c) == 4; }
        bool read() { return (_mode & 0x0c) == 8; }
        bool verify() { return (_mode & 0x0c) == 0; }
        bool autoinitialization() { return (_mode & 0x10) != 0; }
        bool addressDecrement() { return (_mode & 0x20) != 0; }
        bool demand() { return (_mode & 0xc0) == 0x00; }
        bool single() { return (_mode & 0xc0) == 0x40; }
        bool block() { return (_mode & 0xc0) == 0x80; }
        bool cascade() { return (_mode & 0xc0) == 0xc0; }

        uint16_t _baseAddress;
        uint16_t _baseWordCount;
        uint16_t _currentAddress;
        uint16_t _currentWordCount;
        uint8_t _mode;  // Only 6 bits used
    };

    bool memoryToMemory() { return (_command & 1) != 0; }
    bool channel0AddressHold() { return (_command & 2) != 0; }
    bool disabled() { return (_command & 4) != 0; }
    bool compressedTiming() { return (_command & 8) != 0; }
    bool rotatingPriority() { return (_command & 0x10) != 0; }
    bool extendedWriteSelection() { return (_command & 0x20) != 0; }
    bool dreqSenseActiveLow() { return (_command & 0x40) != 0; }
    bool dackSenseActiveHigh() { return (_command & 0x80) != 0; }
    void setRequest(int line, bool active)
    {
        uint8_t b = 1 << line;
        uint8_t s = 0x10 << line;
        if (active) {
            _request |= b;
            _status |= s;
        }
        else {
            _request &= ~b;
            _status &= ~s;
        }
    }

    Channel _channels[4];
    uint16_t _temporaryAddress;
    uint16_t _temporaryWordCount;
    uint8_t _status;
    uint8_t _command;
    uint8_t _temporary;
    uint8_t _mask;  // Only 4 bits used
    uint8_t _request;  // Only 4 bits used
    bool _high;
    int _channel;
    int _priorityChannel;
    bool _needHighAddress;
};

#endif //DMAC_H
