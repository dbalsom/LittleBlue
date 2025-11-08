//
// Created by Daniel on 1/29/25.
//

#ifndef PPI_H
#define PPI_H
#include <cstdint>

class PPI
{
public:
    void reset()
    {
        _mode = 0x99;  // XT normal operation: mode 0, A and C input, B output
        //_mode = 0x9b; // Default: all mode 0, all inputs
        _a = 0;
        _b = 0;
        _c = 0;
        _aLines = 0xff;
        _bLines = 0xff;
        _cLines = 0xff;
    }
    void write(int address, uint8_t data)
    {
        switch (address) {
            case 0:
                _a = data;
                if (aStrobedOutput())
                    _c &= 0x77;  // Clear -OFB and INTR
                break;
            case 1:
                _b = data;
                if (bStrobedOutput())
                    _c &= 0xfc;  // Clear -OFB and INTR
                break;
            case 2: _c = data; break;
            case 3:
                if ((data & 0x80) != 0) {  // Mode set
                    _mode = data;
                    _a = 0;
                    _b = 0;
                    _c = 0;
                    break;
                }
                if ((data & 1) == 0)  // Port C bit reset
                    _c &= ~(1 << ((data & 0xe) >> 1));
                else
                    _c |= 1 << ((data & 0xe) >> 1);
        }
    }
    uint8_t read(int address)
    {
        switch (address) {
            case 0:
                if (aStrobedInput())
                    _c &= 0xd7;  // Clear IBF and INTR
                if (aMode() == 0 && aInput())
                    return _aLines;
                return _a;
            case 1:
                if (bMode() != 0)
                    _c &= 0xfc;  // Clear IBF and INTR
                if (bMode() == 0 && bInput())
                    return _bLines;
                return _b;
            case 2:
                {
                    uint8_t c = _c;
                    if (aMode() == 0) {
                        if (cUpperInput())
                            c = (c & 0x0f) + (_cLines & 0xf0);
                    }
                    else {
                        if (aMode() == 0x20 && cUpperInput()) {
                            if (aInput())
                                c = (c & 0x3f) + (_cLines & 0xc0);
                            else
                                c = (c & 0xcf) + (_cLines & 0x30);
                        }
                    }
                    if (bMode() == 0 && cLowerInput()) {
                        c = (c & 0xf0) + (_cLines & 0x0f);
                    }

                    c &= 0x3F; // Clear parity bits
                    std::cout << "PPI read port C: " << std::hex << static_cast<int>(c) << std::dec << "\n";
                    return c;
                }
        }
        return _mode;
    }
    void setA(int line, bool state)
    {
        if (aStrobedInput() && aStrobe())
            _b = _bLines;
        _aLines = (_aLines & ~(1 << line)) | (state ? (1 << line) : 0);
    }
    void setB(int line, bool state)
    {
        if (bStrobedInput() && bStrobe())
            _b = _bLines;
        _bLines = (_bLines & ~(1 << line)) | (state ? (1 << line) : 0);
    }
    void setC(int line, bool state)
    {
        if (aStrobedInput() && line == 4 && (!state || aStrobe())) {
            _a = _aLines;
            _c |= 0x20;  // Set IBF
            if (aInputInterruptEnable() && state)
                _c |= 8;  // Set INTR on rising edge
        }
        if (aStrobedOutput() && line == 6 && (!state || aAcknowledge())) {
            _c |= 0x80;  // Set -OBF
            if (aOutputInterruptEnable() && state)
                _c |= 8;  // Set INTR on rising edge
        }
        if (bStrobedInput() && line == 2 && (!state || bStrobe())) {
            _b = _bLines;
            _c |= 2;  // Set IBF
            if (bInterruptEnable() && state)
                _c |= 1;  // Set INTR on rising edge
        }
        if (bStrobedOutput() && line == 2 && (!state || bStrobe())) {
            _c |= 2;  // Set -OBF
            if (bInterruptEnable() && state)
                _c |= 1;  // Set INTR on rising edge
        }
        _cLines = (_cLines & ~(1 << line)) | (state ? (1 << line) : 0);
    }
    bool getA(int line)
    {
        uint8_t m = 1 << line;
        if (aMode() == 0) {
            if (aInput())
                return (_aLines & m) != 0;
            return (_a & _aLines & m) != 0;
        }
        return (_a & m) != 0;
    }
    bool getB(int line)
    {
        uint8_t m = 1 << line;
        if (bMode() == 0) {
            if (bInput())
                return (_bLines & m) != 0;
            return (_b & _bLines & m) != 0;
        }
        return (_b & m) != 0;
    }
    bool getC(int line)
    {
        // 0 bit means output enabled, so _c bit low drives output low
        // 1 bit means tristate from PPI so return _cLine bit.
        static const uint8_t m[] = {
            0x00, 0x0f, 0x00, 0x0f, 0x04, 0x0c, 0x04, 0x0c,  // A mode 0
            0xf0, 0xff, 0xf0, 0xff, 0xf4, 0xfc, 0xf4, 0xfc,
            0x00, 0x0f, 0x00, 0x0f, 0x04, 0x0c, 0x04, 0x0c,
            0xf0, 0xff, 0xf0, 0xff, 0xf4, 0xfc, 0xf4, 0xfc,

            0x40, 0x47, 0x40, 0x47, 0x44, 0x44, 0x44, 0x44,  // A mode 1 output
            0x70, 0x77, 0x70, 0x77, 0x74, 0x74, 0x74, 0x74,
            0x10, 0x17, 0x10, 0x17, 0x14, 0x14, 0x14, 0x14,  // A mode 1 input
            0xd0, 0xd7, 0xd0, 0xd7, 0xd4, 0xd4, 0xd4, 0xd4,

            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,  // A mode 2
            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,
            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,
            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,

            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,  // A mode 2
            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,
            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,
            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,
        };
        return (_cLines & (_c | m[_mode & 0x7f]) & (1 << line)) != 0;
    }
private:
    uint8_t aMode() { return _mode & 0x60; }
    uint8_t bMode() { return _mode & 4; }
    bool aInput() { return (_mode & 0x10) != 0; }
    bool cUpperInput() { return (_mode & 8) != 0; }
    bool bInput() { return (_mode & 2) != 0; }
    bool cLowerInput() { return (_mode & 1) != 0; }
    bool aStrobe() { return (_cLines & 0x10) == 0; }
    bool bStrobe() { return (_cLines & 4) == 0; }
    bool aAcknowledge() { return (_cLines & 0x40) == 0; }
    bool bAcknowledge() { return (_cLines & 4) == 0; }
    bool aStrobedInput()
    {
        return (aMode() == 0x20 && aInput()) || aMode() == 0x40;
    }
    bool aStrobedOutput()
    {
        return (aMode() == 0x20 && !aInput()) || aMode() == 0x40;
    }
    bool bStrobedInput() { return bMode() != 0 && bInput(); }
    bool bStrobedOutput() { return bMode() != 0 && !bInput(); }
    bool aInputInterruptEnable() { return (_c & 0x10) != 0; }
    bool aOutputInterruptEnable() { return (_c & 0x40) != 0; }
    bool bInterruptEnable() { return (_c & 4) != 0; }

    uint8_t _a;
    uint8_t _b;
    uint8_t _c;
    uint8_t _aLines;
    uint8_t _bLines;
    uint8_t _cLines;
    uint8_t _mode;
};


#endif //PPI_H
