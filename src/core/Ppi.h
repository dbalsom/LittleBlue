#ifndef PPI_H
#define PPI_H

#include <cstdint>

class PPI
{
public:
    void reset() {
        mode_ = 0x99; // XT normal operation: mode 0, A and C input, B output
        //_mode = 0x9b; // Default: all mode 0, all inputs
        a_ = 0;
        b_ = 0;
        c_ = 0;
        a_lines_ = 0xff;
        b_lines_ = 0xff;
        c_lines_ = 0xff;
    }

    void write(const uint32_t address, const uint8_t data) {
        switch (address) {
            case 0:
                a_ = data;
                if (aStrobedOutput()) {
                    c_ &= 0x77; // Clear -OFB and INTR
                }
                break;
            case 1:
                //std::cout << "PPI write port B: " << std::hex << static_cast<int>(data) << std::dec << "\n";
                b_ = data;
                if (bStrobedOutput()) {
                    c_ &= 0xfc; // Clear -OFB and INTR
                }
                break;
            case 2:
                c_ = data;
                break;
            case 3:
                if ((data & 0x80) != 0) {
                    // Mode set
                    mode_ = data;
                    a_ = 0;
                    b_ = 0;
                    c_ = 0;
                    break;
                }
                if ((data & 1) == 0) {
                    // Port C bit reset
                    c_ &= ~(1 << ((data & 0xe) >> 1));
                }
                else {
                    c_ |= 1 << ((data & 0xe) >> 1);
                }
            default:
                break;
        }
    }

    uint8_t read(const uint32_t address) {
        switch (address) {
            case 0:
                if (aStrobedInput()) {
                    c_ &= 0xd7; // Clear IBF and INTR
                }
                if (aMode() == 0 && aInput()) {
                    return a_lines_;
                }
                return a_;
            case 1:
                if (bMode() != 0) {
                    c_ &= 0xfc; // Clear IBF and INTR
                }
                if (bMode() == 0 && bInput()) {
                    return b_lines_;
                }
                return b_;
            case 2:
            {
                uint8_t c = c_;
                if (aMode() == 0) {
                    if (cUpperInput()) {
                        c = (c & 0x0f) + (c_lines_ & 0xf0);
                    }
                }
                else {
                    if (aMode() == 0x20 && cUpperInput()) {
                        if (aInput()) {
                            c = (c & 0x3f) + (c_lines_ & 0xc0);
                        }
                        else {
                            c = (c & 0xcf) + (c_lines_ & 0x30);
                        }
                    }
                }
                if (bMode() == 0 && cLowerInput()) {
                    c = (c & 0xf0) + (c_lines_ & 0x0f);
                }

                c &= 0x3F; // Clear parity bits
                //std::cout << "PPI read port C: " << std::hex << static_cast<int>(c) << std::dec << "\n";
                return c;
            }
            default:
                break;
        }
        return mode_;
    }

    void setA(const int line, const bool state) {
        if (aStrobedInput() && aStrobe()) {
            b_ = b_lines_;
        }
        a_lines_ = (a_lines_ & ~(1 << line)) | (state ? (1 << line) : 0);
    }

    void setB(const int line, const bool state) {
        if (bStrobedInput() && bStrobe()) {
            b_ = b_lines_;
        }
        b_lines_ = (b_lines_ & ~(1 << line)) | (state ? (1 << line) : 0);
    }

    void setC(const int line, const bool state) {
        if (aStrobedInput() && line == 4 && (!state || aStrobe())) {
            a_ = a_lines_;
            c_ |= 0x20; // Set IBF
            if (aInputInterruptEnable() && state) {
                c_ |= 8; // Set INTR on rising edge
            }
        }
        if (aStrobedOutput() && line == 6 && (!state || aAcknowledge())) {
            c_ |= 0x80; // Set -OBF
            if (aOutputInterruptEnable() && state) {
                c_ |= 8; // Set INTR on rising edge
            }
        }
        if (bStrobedInput() && line == 2 && (!state || bStrobe())) {
            b_ = b_lines_;
            c_ |= 2; // Set IBF
            if (bInterruptEnable() && state) {
                c_ |= 1; // Set INTR on rising edge
            }
        }
        if (bStrobedOutput() && line == 2 && (!state || bStrobe())) {
            c_ |= 2; // Set -OBF
            if (bInterruptEnable() && state) {
                c_ |= 1; // Set INTR on rising edge
            }
        }
        c_lines_ = (c_lines_ & ~(1 << line)) | (state ? (1 << line) : 0);
    }

    bool getA(const int line) {
        const uint8_t m = 1 << line;
        if (aMode() == 0) {
            if (aInput()) {
                return (a_lines_ & m) != 0;
            }
            return (a_ & a_lines_ & m) != 0;
        }
        return (a_ & m) != 0;
    }

    bool getB(const int line) {
        uint8_t m = 1 << line;
        if (bMode() == 0) {
            if (bInput()) {
                return (b_lines_ & m) != 0;
            }
            return (b_ & b_lines_ & m) != 0;
        }
        return (b_ & m) != 0;
    }

    bool getC(const int line) {
        // 0 bit means output enabled, so _c bit low drives output low
        // 1 bit means tristate from PPI so return _cLine bit.
        static const uint8_t m[] = {
            0x00, 0x0f, 0x00, 0x0f, 0x04, 0x0c, 0x04, 0x0c, // A mode 0
            0xf0, 0xff, 0xf0, 0xff, 0xf4, 0xfc, 0xf4, 0xfc,
            0x00, 0x0f, 0x00, 0x0f, 0x04, 0x0c, 0x04, 0x0c,
            0xf0, 0xff, 0xf0, 0xff, 0xf4, 0xfc, 0xf4, 0xfc,

            0x40, 0x47, 0x40, 0x47, 0x44, 0x44, 0x44, 0x44, // A mode 1 output
            0x70, 0x77, 0x70, 0x77, 0x74, 0x74, 0x74, 0x74,
            0x10, 0x17, 0x10, 0x17, 0x14, 0x14, 0x14, 0x14, // A mode 1 input
            0xd0, 0xd7, 0xd0, 0xd7, 0xd4, 0xd4, 0xd4, 0xd4,

            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54, // A mode 2
            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,
            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,
            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,

            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54, // A mode 2
            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,
            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,
            0x50, 0x57, 0x50, 0x57, 0x54, 0x54, 0x54, 0x54,
        };
        return (c_lines_ & (c_ | m[mode_ & 0x7f]) & (1 << line)) != 0;
    }

private:
    uint8_t aMode() const { return mode_ & 0x60; }
    uint8_t bMode() const { return mode_ & 4; }
    bool aInput() const { return (mode_ & 0x10) != 0; }
    bool cUpperInput() const { return (mode_ & 8) != 0; }
    bool bInput() const { return (mode_ & 2) != 0; }
    bool cLowerInput() const { return (mode_ & 1) != 0; }
    bool aStrobe() const { return (c_lines_ & 0x10) == 0; }
    bool bStrobe() const { return (c_lines_ & 4) == 0; }
    bool aAcknowledge() const { return (c_lines_ & 0x40) == 0; }
    bool bAcknowledge() const { return (c_lines_ & 4) == 0; }

    bool aStrobedInput() const {
        return (aMode() == 0x20 && aInput()) || aMode() == 0x40;
    }

    bool aStrobedOutput() const {
        return (aMode() == 0x20 && !aInput()) || aMode() == 0x40;
    }

    bool bStrobedInput() const { return bMode() != 0 && bInput(); }
    bool bStrobedOutput() const { return bMode() != 0 && !bInput(); }
    bool aInputInterruptEnable() const { return (c_ & 0x10) != 0; }
    bool aOutputInterruptEnable() const { return (c_ & 0x40) != 0; }
    bool bInterruptEnable() const { return (c_ & 4) != 0; }

    uint8_t a_{};
    uint8_t b_{};
    uint8_t c_{};
    uint8_t a_lines_{};
    uint8_t b_lines_{};
    uint8_t c_lines_{};
    uint8_t mode_{};
};


#endif //PPI_H
