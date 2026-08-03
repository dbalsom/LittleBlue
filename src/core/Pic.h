#ifndef PIC_H
#define PIC_H

struct PicDebugState
{
    uint8_t irr;
    uint8_t imr;
    uint8_t isr;
    uint8_t lines;
};

class PIC
{
public:
    PIC() { // NOLINT(*-pro-type-member-init)
        reset();
    }

    void reset() {
        interrupt_pending_ = false;
        interrupt_ = 0;
        irr_ = 0;
        imr_ = 0;
        isr_ = 0;
        icw1_ = 0;
        icw2_ = 0;
        icw3_ = 0;
        icw4_ = 0;
        ocw3_ = 0;
        lines_ = 0;
        special_mask_mode_ = false;
        acknowledged_bytes_ = 0;
        priority_ = 0;
        rotate_in_automatic_eoi_mode_ = false;
        initialization_state_ = initializationStateNone;
    }

    void stubInit() {
        icw1_ = 0x13;
        icw2_ = 0x08;
        icw4_ = 0x0f;
        imr_ = 0xbc;
    }

    void write(const uint32_t address, const uint8_t data) {
        if (address == 0) {
            if ((data & 0x10) != 0) {
                icw1_ = data;
                if (levelTriggered()) {
                    irr_ = lines_;
                }
                else {
                    irr_ = 0;
                }
                initialization_state_ = initializationStateICW2;
                imr_ = 0;
                isr_ = 0;
                icw2_ = 0;
                icw3_ = 0;
                icw4_ = 0;
                ocw3_ = 0;
                acknowledged_bytes_ = 0;
                priority_ = 0;
                rotate_in_automatic_eoi_mode_ = false;
                special_mask_mode_ = false;
                interrupt_ = 0;
                interrupt_pending_ = false;
            }
            else {
                if ((data & 8) == 0) {
                    const uint8_t b = 1 << (data & 7);
                    switch (data & 0xe0) {
                        case 0x00: // Rotate in automatic EOI mode (clear) (Automatic Rotation)
                            rotate_in_automatic_eoi_mode_ = false;
                            break;
                        case 0x20: // Non-specific EOI command (End of Interrupt)
                            nonSpecificEOI(false);
                            break;
                        case 0x40: // No operation
                            break;
                        case 0x60: // Specific EOI command (End of Interrupt)
                            isr_ &= ~b;
                            break;
                        case 0x80: // Rotate in automatic EOI mode (set) (Automatic Rotation)
                            rotate_in_automatic_eoi_mode_ = true;
                            break;
                        case 0xa0: // Rotate on non-specific EOI command (Automatic Rotation)
                            nonSpecificEOI(true);
                            break;
                        case 0xc0: // Set priority command (Specific Rotation)
                            priority_ = (data + 1) & 7;
                            break;
                        case 0xe0: // Rotate on specific EOI command (Specific Rotation)
                            if ((isr_ & b) != 0) {
                                isr_ &= ~b;
                                priority_ = (data + 1) & 7;
                            }
                            break;
                        default:
                            break;
                    }
                }
                else {
                    ocw3_ = data;
                    if ((ocw3_ & 0x40) != 0) {
                        special_mask_mode_ = (ocw3_ & 0x20) != 0;
                    }
                }
            }
        }
        else {
            switch (initialization_state_) {
                case initializationStateICW2:
                    icw2_ = data;
                    if (cascadeMode()) {
                        initialization_state_ = initializationStateICW3;
                    }
                    else {
                        checkICW4Needed();
                    }
                    break;
                case initializationStateICW3:
                    icw3_ = data;
                    checkICW4Needed();
                    break;
                case initializationStateICW4:
                    icw4_ = data;
                    initialization_state_ = initializationStateNone;
                    break;
                case initializationStateNone:
                    imr_ = data;
                    break;
            }
        }
    }

    uint8_t read(const uint32_t address) {
        if ((ocw3_ & 4) != 0) {
            // Poll mode
            acknowledge();
            return (interruptPending() ? 0x80 : 0) + interrupt_;
        }
        if (address == 0) {
            if ((ocw3_ & 1) != 0) {
                return isr_;
            }
            return irr_;
        }

        return imr_;
    }

    uint8_t interruptAcknowledge() {
        if (acknowledged_bytes_ == 0) {
            acknowledge();
            acknowledged_bytes_ = 1;
            if (i86Mode()) {
                return 0xFF;
            }
            else {
                return 0xCD;
            }
        }
        if (i86Mode()) {
            acknowledged_bytes_ = 0;
            if (autoEOI()) {
                nonSpecificEOI(rotate_in_automatic_eoi_mode_);
            }
            interrupt_pending_ = false;
            if (slaveOn(interrupt_)) {
                return 0xFF; // Filled in by slave PIC
            }
            return interrupt_ + (icw2_ & 0xF8);
        }
        if (acknowledged_bytes_ == 1) {
            acknowledged_bytes_ = 2;
            if (slaveOn(interrupt_)) {
                return 0xff; // Filled in by slave PIC
            }
            if ((icw1_ & 4) != 0) {
                // Call address interval 4
                return (interrupt_ << 2) + (icw1_ & 0xE0);
            }
            return (interrupt_ << 3) + (icw1_ & 0xc0);
        }
        acknowledged_bytes_ = 0;
        if (autoEOI()) {
            nonSpecificEOI(rotate_in_automatic_eoi_mode_);
        }
        interrupt_pending_ = false;
        if (slaveOn(interrupt_)) {
            return 0xff; // Filled in by slave PIC
        }
        return icw2_;
    }

    void setIRQLine(const int line, const bool state) {
        const uint8_t b = 1 << line;
        if (state) {
            if (levelTriggered() || (lines_ & b) == 0) {
                irr_ |= b;
            }
            lines_ |= b;
        }
        else {
            irr_ &= ~b;
            lines_ &= ~b;
        }
    }

    [[nodiscard]]
    bool interruptPending() {
        auto i = findBestInterrupt();
        if (i != -1) {
            if (i != 0) {
                //std::cout << "PIC interrupt pending on line " << i << "\n";
            }
            return true;
        }
        return false;
    }

    [[nodiscard]] uint8_t getIRQLines() const { return lines_; }

    // Return a snapshot of internal PIC state for debugging UI
    [[nodiscard]] PicDebugState getDebugState() const {
        const PicDebugState s{
            .irr = irr_,
            .imr = imr_,
            .isr = isr_,
            .lines = lines_
        };
        return s;
    }

    // Return whether in cascade mode (connected to master PIC) - should be false on XT
    [[nodiscard]] bool cascadeMode() const { return (icw1_ & 2) == 0; }
    // Return whether level-triggered mode is enabled - should be true on XT
    [[nodiscard]] bool levelTriggered() const { return (icw1_ & 8) != 0; }
    // Return whether in 8086 mode - should be true on XT
    [[nodiscard]] bool i86Mode() const { return (icw4_ & 1) != 0; }
    // Return whether auto EOI mode is enabled
    [[nodiscard]] bool autoEOI() const { return (icw4_ & 2) != 0; }

private:
    [[nodiscard]]
    bool slaveOn(const int channel) const {
        return cascadeMode() && (icw4_ & 0xc0) == 0xc0 && (icw3_ & (1 << channel)) != 0;
    }

    [[nodiscard]]
    int findBestInterrupt() {
        int n = priority_;
        for (int i = 0; i < 8; ++i) {
            const uint8_t b = 1 << n;
            const bool s = (icw4_ & 0x10) != 0 && slaveOn(n);
            if ((isr_ & b) != 0 && !special_mask_mode_ && !s) {
                break;
            }
            if ((irr_ & b) != 0 && (imr_ & b) == 0 && ((isr_ & b) == 0 || s)) {
                return n;
            }
            if ((isr_ & b) != 0 && !special_mask_mode_ && s) {
                break;
            }
            n = (n + 1) & 7;
            interrupt_ = n;
        }
        return -1;
    }

    void acknowledge() {
        const int i = findBestInterrupt();
        if (i == -1) {
            interrupt_ = 7;
            return;
        }
        const uint8_t b = 1 << i;
        isr_ |= b;
        if (!levelTriggered()) {
            irr_ &= ~b;
        }
    }

    void nonSpecificEOI(bool rotatePriority = false) {
        int n = priority_;
        for (int i = 0; i < 8; ++i) {
            const uint8_t b = 1 << n;
            n = (n + 1) & 7;
            if ((isr_ & b) != 0) {
                isr_ &= ~b;
                if (rotatePriority) {
                    priority_ = n & 7;
                }
                break;
            }
        }
    }

    void checkICW4Needed() {
        if ((icw1_ & 1) != 0) {
            initialization_state_ = initializationStateICW4;
        }
        else {
            initialization_state_ = initializationStateNone;
        }
    }

    enum InitializationState
    {
        initializationStateNone,
        initializationStateICW2,
        initializationStateICW3,
        initializationStateICW4
    };

    bool interrupt_pending_;
    int interrupt_;
    uint8_t irr_;
    uint8_t imr_;
    uint8_t isr_;
    uint8_t icw1_;
    uint8_t icw2_;
    uint8_t icw3_;
    uint8_t icw4_;
    uint8_t ocw3_;
    uint8_t lines_;
    int acknowledged_bytes_;
    int priority_;
    bool special_mask_mode_;
    bool rotate_in_automatic_eoi_mode_;
    InitializationState initialization_state_;
};

#endif //PIC_H
