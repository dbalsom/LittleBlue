#ifndef PIC_H
#define PIC_H

class PIC
{
public:
    void reset()
    {
        _interruptPending = false;
        _interrupt = 0;
        _irr = 0;
        _imr = 0;
        _isr = 0;
        _icw1 = 0;
        _icw2 = 0;
        _icw3 = 0;
        _icw4 = 0;
        _ocw3 = 0;
        _lines = 0;
        _specialMaskMode = false;
        _acknowledgedBytes = 0;
        _priority = 0;
        _rotateInAutomaticEOIMode = false;
        _initializationState = initializationStateNone;
    }
    void stubInit()
    {
        _icw1 = 0x13;
        _icw2 = 0x08;
        _icw4 = 0x0f;
        _imr = 0xbc;
    }
    void write(int address, uint8_t data)
    {
        if (address == 0) {
            if ((data & 0x10) != 0) {
                _icw1 = data;
                if (levelTriggered())
                    _irr = _lines;
                else
                    _irr = 0;
                _initializationState = initializationStateICW2;
                _imr = 0;
                _isr = 0;
                _icw2 = 0;
                _icw3 = 0;
                _icw4 = 0;
                _ocw3 = 0;
                _acknowledgedBytes = 0;
                _priority = 0;
                _rotateInAutomaticEOIMode = false;
                _specialMaskMode = false;
                _interrupt = 0;
                _interruptPending = false;
            }
            else {
                if ((data & 8) == 0) {
                    uint8_t b = 1 << (data & 7);
                    switch (data & 0xe0) {
                        case 0x00:  // Rotate in automatic EOI mode (clear) (Automatic Rotation)
                            _rotateInAutomaticEOIMode = false;
                            break;
                        case 0x20:  // Non-specific EOI command (End of Interrupt)
                            nonSpecificEOI(false);
                            break;
                        case 0x40:  // No operation
                            break;
                        case 0x60:  // Specific EOI command (End of Interrupt)
                            _isr &= ~b;
                            break;
                        case 0x80:  // Rotate in automatic EOI mode (set) (Automatic Rotation)
                            _rotateInAutomaticEOIMode = true;
                            break;
                        case 0xa0:  // Rotate on non-specific EOI command (Automatic Rotation)
                            nonSpecificEOI(true);
                            break;
                        case 0xc0:  // Set priority command (Specific Rotation)
                            _priority = (data + 1) & 7;
                            break;
                        case 0xe0:  // Rotate on specific EOI command (Specific Rotation)
                            if ((_isr & b) != 0) {
                                _isr &= ~b;
                                _priority = (data + 1) & 7;
                            }
                            break;
                    }
                }
                else {
                    _ocw3 = data;
                    if ((_ocw3 & 0x40) != 0)
                        _specialMaskMode = (_ocw3 & 0x20) != 0;
                }
            }
        }
        else {
            switch (_initializationState) {
                case initializationStateICW2:
                    _icw2 = data;
                    if (cascadeMode())
                        _initializationState = initializationStateICW3;
                    else
                        checkICW4Needed();
                    break;
                case initializationStateICW3:
                    _icw3 = data;
                    checkICW4Needed();
                    break;
                case initializationStateICW4:
                    _icw4 = data;
                    _initializationState = initializationStateNone;
                    break;
                case initializationStateNone:
                    _imr = data;
                    break;
            }
        }
    }
    uint8_t read(int address)
    {
        if ((_ocw3 & 4) != 0) {  // Poll mode
            acknowledge();
            return (_interruptPending ? 0x80 : 0) + _interrupt;
        }
        if (address == 0) {
            if ((_ocw3 & 1) != 0)
                return _isr;
            return _irr;
        }
        else
            return _imr;
    }
    uint8_t interruptAcknowledge()
    {
        if (_acknowledgedBytes == 0) {
            acknowledge();
            _acknowledgedBytes = 1;
            if (i86Mode())
                return 0xff;
            else
                return 0xcd;
        }
        if (i86Mode()) {
            _acknowledgedBytes = 0;
            if (autoEOI())
                nonSpecificEOI(_rotateInAutomaticEOIMode);
            _interruptPending = false;
            if (slaveOn(_interrupt))
                return 0xff;  // Filled in by slave PIC
            return _interrupt + (_icw2 & 0xf8);
        }
        if (_acknowledgedBytes == 1) {
            _acknowledgedBytes = 2;
            if (slaveOn(_interrupt))
                return 0xff;  // Filled in by slave PIC
            if ((_icw1 & 4) != 0)  // Call address interval 4
                return (_interrupt << 2) + (_icw1 & 0xe0);
            return (_interrupt << 3) + (_icw1 & 0xc0);
        }
        _acknowledgedBytes = 0;
        if (autoEOI())
            nonSpecificEOI(_rotateInAutomaticEOIMode);
        _interruptPending = false;
        if (slaveOn(_interrupt))
            return 0xff;  // Filled in by slave PIC
        return _icw2;
    }
    void setIRQLine(int line, bool state)
    {
        uint8_t b = 1 << line;
        if (state) {
            if (levelTriggered() || (_lines & b) == 0)
                _irr |= b;
            _lines |= b;
        }
        else {
            _irr &= ~b;
            _lines &= ~b;
        }
        if (findBestInterrupt() != -1)
            _interruptPending = true;
    }
    bool interruptPending() const { return _interruptPending; }
    uint8_t getIRQLines() { return _lines; }
private:
    bool cascadeMode() { return (_icw1 & 2) == 0; }
    bool levelTriggered() { return (_icw1 & 8) != 0; }
    bool i86Mode() { return (_icw4 & 1) != 0; }
    bool autoEOI() { return (_icw4 & 2) != 0; }
    bool slaveOn(int channel)
    {
        return cascadeMode() && (_icw4 & 0xc0) == 0xc0 &&
            (_icw3 & (1 << channel)) != 0;
    }
    int findBestInterrupt()
    {
        int n = _priority;
        for (int i = 0; i < 8; ++i) {
            uint8_t b = 1 << n;
            bool s = (_icw4 & 0x10) != 0 && slaveOn(n);
            if ((_isr & b) != 0 && !_specialMaskMode && !s)
                break;
            if ((_irr & b) != 0 && (_imr & b) == 0 && ((_isr & b) == 0 || s))
                return n;
            if ((_isr & b) != 0 && !_specialMaskMode && s)
                break;
            n = (n + 1) & 7;
        }
        return -1;
    }
    void acknowledge()
    {
        int i = findBestInterrupt();
        if (i == -1) {
            _interrupt = 7;
            return;
        }
        uint8_t b = 1 << i;
        _isr |= b;
        if (!levelTriggered())
            _irr &= ~b;
    }
    void nonSpecificEOI(bool rotatePriority = false)
    {
        int n = _priority;
        for (int i = 0; i < 8; ++i) {
            uint8_t b = 1 << n;
            n = (n + 1) & 7;
            if ((_isr & b) != 0 && (_imr & b) == 0) {
                _isr &= ~b;
                if (rotatePriority)
                    _priority = n & 7;
                break;
            }
        }
    }
    void checkICW4Needed()
    {
        if ((_icw1 & 1) != 0)
            _initializationState = initializationStateICW4;
        else
            _initializationState = initializationStateNone;
    }

    enum InitializationState
    {
        initializationStateNone,
        initializationStateICW2,
        initializationStateICW3,
        initializationStateICW4
    };
    bool _interruptPending;
    int _interrupt;
    uint8_t _irr;
    uint8_t _imr;
    uint8_t _isr;
    uint8_t _icw1;
    uint8_t _icw2;
    uint8_t _icw3;
    uint8_t _icw4;
    uint8_t _ocw3;
    uint8_t _lines;
    int _acknowledgedBytes;
    int _priority;
    bool _specialMaskMode;
    bool _rotateInAutomaticEOIMode;
    InitializationState _initializationState;
};

#endif //PIC_H
