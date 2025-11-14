//
// Created by Daniel on 11/12/2025.
//

#ifndef SDL_MIN_KEYBOARD_H
#define SDL_MIN_KEYBOARD_H
#include <cstdint>
#include <format>
#include <iostream>

class Keyboard
{
    static constexpr uint32_t RESET_TICKS = 10;
    static constexpr uint32_t RESET_BYTE_DELAY_TICKS = 1;
    static constexpr uint8_t RESET_BYTE = 0xAA;

public:
    Keyboard() {
        reset();
    }

    void setClockLineState(const bool state) {
        std::cout << std::format("Keyboard: Setting clock line state to {}\n", state ? "HIGH" : "LOW");
        if (!state && clockLineState_) {
            // Clock line went high->low
            std::cout << "Keyboard: Clock line went low." << std::endl;
            resetting_ = true;
            clockLineLowTicks_ = 0;
        }
        else if (state && !clockLineState_) {
            // Clock line went low->high
            std::cout << "Keyboard: Clock line went high." << std::endl;
            if (clockLineLowTicks_ >= RESET_TICKS) {
                // Clock line was held low long enough to trigger reset.
                std::cout << "Keyboard: Detected reset condition on clock line.\n";
                resetting_ = true;
            }
            clockLineHighTicks_ = 0;
        }
        clockLineState_ = state;
    }

    void reset() {
        clockLineState_ = false;
        sendReset_ = false;
        resetting_ = false;
        clockLineLowTicks_ = 0;
        clockLineHighTicks_ = 0;
    }

    void tick() {
        if (!clockLineState_) {
            clockLineLowTicks_++;
            std::cout << std::format("Keyboard: Clock line low ticks: {}\n", clockLineLowTicks_);
        }
        else {
            clockLineHighTicks_++;
            if (resetting_ && clockLineHighTicks_ >= RESET_BYTE_DELAY_TICKS) {
                // Clock line has been held high long enough after reset to send reset byte.
                sendReset_ = true;
                resetting_ = false;
            }
        }
    }

    bool getScanCode(uint8_t& byte) {
        if (sendReset_) {
            byte = RESET_BYTE;
            sendReset_ = false;
            return true;
        }
        return false;
    }

private:
    bool clockLineState_{true};
    bool sendReset_{false};
    bool resetting_{false};
    uint32_t clockLineLowTicks_{0};
    uint32_t clockLineHighTicks_{0};
};


#endif //SDL_MIN_KEYBOARD_H
