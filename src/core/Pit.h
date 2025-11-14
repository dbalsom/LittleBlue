#ifndef PIT_H
#define PIT_H
#include <cstdint>

class PIT
{
public:
    void reset() {
        for (int i = 0; i < 3; ++i) {
            counters_[i].reset();
        }
    }

    void stubInit() {
        for (int i = 0; i < 3; ++i) {
            counters_[i].stubInit();
        }
    }

    void write(const uint32_t address, const uint8_t data) {
        if (address == 3) {
            const int counter = data >> 6;
            if (counter == 3) {
                return;
            }
            counters_[counter].control(data & 0x3f);
        }
        else
            counters_[address].write(data);
    }

    uint8_t read(const uint32_t address) {
        if (address == 3) {
            return 0xff;
        }
        return counters_[address].read();
    }

    void wait() {
        for (int i = 0; i < 3; ++i) {
            counters_[i].wait();
        }
    }

    void setGate(const int counter, const bool gate) {
        counters_[counter].setGate(gate);
    }

    [[nodiscard]] bool getOutput(const int counter) const {
        return counters_[counter].output;
    }

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
        void reset() {
            value = 0;
            count = 0;
            first_byte = true;
            latched = false;
            output = true;
            control_byte = 0x30;
            state = stateWaitingForCount;
        }

        void stubInit() {
            value = 0xffff;
            count = 0xffff;
            first_byte = true;
            latched = false;
            output = true;
            control_byte = 0x34;
            state = stateCounting;
            gate = true;
        }

        void write(const uint8_t data) {
            write_byte = data;
            have_write_byte = true;
        }

        uint8_t read() {
            if (!latched) {
                latch = count;
            }
            switch (control_byte & 0x30) {
                case 0x10:
                    latched = false;
                    return latch & 0xff;
                case 0x20:
                    latched = false;
                    return latch >> 8;
                case 0x30:
                    if (first_byte) {
                        first_byte = false;
                        return latch & 0xff;
                    }
                    first_byte = true;
                    latched = false;
                    return latch >> 8;
                default:
                    break;
            }
            // This should never happen.
            return 0;
        }

        void wait() {
            switch (control_byte & 0x0e) {
                case 0x00: // Interrupt on Terminal Count
                    if (state == stateLoadDelay) {
                        state = stateCounting;
                        value = count;
                        break;
                    }
                    if (gate && state == stateCounting) {
                        countDown();
                        if (value == 0) {
                            output = true;
                        }
                    }
                    break;
                case 0x02: // Programmable One-Shot
                    if (state == stateLoadDelay) {
                        state = stateWaitingForGate;
                        break;
                    }
                    if (state == stateGateRose) {
                        output = false;
                        value = count;
                        state = stateCounting;
                    }
                    countDown();
                    if (value == 0) {
                        output = true;
                        state = stateWaitingForGate;
                    }
                    break;
                case 0x04:
                case 0x0c: // Rate Generator
                    if (state == stateLoadDelay) {
                        state = stateCounting;
                        value = count;
                        break;
                    }
                    if (gate && state == stateCounting) {
                        countDown();
                        if (value == 1) {
                            output = false;
                        }
                        if (value == 0) {
                            output = true;
                            value = count;
                        }
                    }
                    break;
                case 0x06:
                case 0x0e: // Square Wave Rate Generator
                    if (state == stateLoadDelay) {
                        state = stateCounting;
                        value = count;
                        break;
                    }
                    if (gate && state == stateCounting) {
                        if ((value & 1) != 0) {
                            if (!output) {
                                countDown();
                                countDown();
                            }
                        }
                        else {
                            countDown();
                        }
                        countDown();
                        if (value == 0) {
                            output = !output;
                            value = count;
                        }
                    }
                    break;
                case 0x08: // Software Triggered Strobe
                    if (state == stateLoadDelay) {
                        state = stateCounting;
                        value = count;
                        break;
                    }
                    if (state == statePulsing) {
                        output = true;
                        state = stateWaitingForCount;
                    }
                    if (gate && state == stateCounting) {
                        countDown();
                        if (value == 0) {
                            output = false;
                            state = statePulsing;
                        }
                    }
                    break;
                case 0x0a: // Hardware Triggered Strobe
                    if (state == stateLoadDelay) {
                        state = stateWaitingForGate;
                        break;
                    }
                    if (state == statePulsing) {
                        output = true;
                        state = stateWaitingForCount;
                    }
                    if (state == stateGateRose) {
                        output = false;
                        value = count;
                        state = stateCounting;
                    }
                    if (state == stateCounting) {
                        countDown();
                        if (value == 1) {
                            output = false;
                        }
                        if (value == 0) {
                            output = true;
                            state = stateWaitingForGate;
                        }
                    }
                    break;
                default:
                    break;
            }
            if (have_write_byte) {
                have_write_byte = false;
                switch (control_byte & 0x30) {
                    case 0x10:
                        load(write_byte);
                        break;
                    case 0x20:
                        load(write_byte << 8);
                        break;
                    case 0x30:
                        if (first_byte) {
                            low_byte = write_byte;
                            first_byte = false;
                        }
                        else {
                            load((write_byte << 8) + low_byte);
                            first_byte = true;
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        void countDown() {
            if ((control_byte & 1) == 0) {
                --value;
                return;
            }
            if ((value & 0xf) != 0) {
                --value;
                return;
            }
            if ((value & 0xf0) != 0) {
                value -= (0x10 - 9);
                return;
            }
            if ((value & 0xf00) != 0) {
                value -= (0x100 - 0x99);
                return;
            }
            value -= (0x1000 - 0x999);
        }

        void load(const uint16_t new_count) {
            count = new_count;
            switch (control_byte & 0x0e) {
                case 0x00: // Interrupt on Terminal Count
                    if (state == stateWaitingForCount) {
                        state = stateLoadDelay;
                    }
                    output = false;
                    break;
                case 0x02: // Programmable One-Shot
                    if (state != stateCounting) {
                        state = stateLoadDelay;
                    }
                    break;
                case 0x04:
                case 0x0c: // Rate Generator
                    if (state == stateWaitingForCount) {
                        state = stateLoadDelay;
                    }
                    break;
                case 0x06:
                case 0x0e: // Square Wave Rate Generator
                    if (state == stateWaitingForCount) {
                        state = stateLoadDelay;
                    }

                    break;
                case 0x08: // Software Triggered Strobe
                    if (state == stateWaitingForCount) {
                        state = stateLoadDelay;
                    }
                    break;
                case 0x0a: // Hardware Triggered Strobe
                    if (state != stateCounting) {
                        state = stateLoadDelay;
                    }
                    break;
                default:
                    break;
            }
        }

        void control(const uint8_t control) {
            int command = control & 0x30;
            if (command == 0) {
                latch = value;
                latched = true;
                return;
            }
            control_byte = control;
            first_byte = true;
            latched = false;
            state = stateWaitingForCount;
            switch (control_byte & 0x0e) {
                case 0x00: // Interrupt on Terminal Count
                    output = false;
                    break;
                case 0x02: // Programmable One-Shot
                    output = true;
                    break;
                case 0x04:
                case 0x0c: // Rate Generator
                    output = true;
                    break;
                case 0x06:
                case 0x0e: // Square Wave Rate Generator
                    output = true;
                    break;
                case 0x08: // Software Triggered Strobe
                    output = true;
                    break;
                case 0x0a: // Hardware Triggered Strobe
                    output = true;
                    break;
                default:
                    break;
            }
        }

        void setGate(const bool new_gate) {
            if (gate == new_gate) {
                // No change
                return;
            }

            switch (control_byte & 0x0e) {
                case 0x00: // Interrupt on Terminal Count
                    break;
                case 0x02: // Programmable One-Shot
                    if (new_gate) {
                        state = stateGateRose;
                    }
                    break;
                case 0x04:
                case 0x0c: // Rate Generator
                    if (!new_gate) {
                        output = true;
                    }
                    else {
                        value = count;
                    }
                    break;
                case 0x06:
                case 0x0e: // Square Wave Rate Generator
                    if (!new_gate) {
                        output = true;
                    }
                    else {
                        value = count;
                    }
                    break;
                case 0x08: // Software Triggered Strobe
                    break;
                case 0x0a: // Hardware Triggered Strobe
                    if (new_gate) {
                        state = stateGateRose;
                    }
                    break;
                default:
                    break;
            }
            gate = new_gate;
        }

        uint16_t count;
        uint16_t value;
        uint16_t latch;
        uint8_t control_byte;
        uint8_t low_byte;
        bool gate;
        bool output;
        bool first_byte;
        bool latched;
        State state;
        uint8_t write_byte;
        bool have_write_byte;
    };

    Counter counters_[3] = {};
};

#endif //PIT_H
