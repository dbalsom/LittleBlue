#ifndef SDL_MIN_KEYBOARD_H
#define SDL_MIN_KEYBOARD_H

#include <cstdint>
#include <format>
#include <iostream>

class Keyboard
{
    static constexpr uint32_t kResetTicks = 10;
    static constexpr uint32_t kResetByteDelayTicks = 1;
    static constexpr uint8_t kResetByte = 0xAA;

public:
    Keyboard() {
        reset();
    }

    void setClockLineState(const bool state) {
        std::cout << std::format("Keyboard: Setting clock line state to {}\n", state ? "HIGH" : "LOW");
        if (!state && clock_line_state_) {
            // Clock line went high->low
            std::cout << "Keyboard: Clock line went low." << std::endl;
            resetting_ = true;
            clock_line_low_ticks_ = 0;
        }
        else if (state && !clock_line_state_) {
            // Clock line went low->high
            std::cout << "Keyboard: Clock line went high." << std::endl;
            if (clock_line_low_ticks_ >= kResetTicks) {
                // Clock line was held low long enough to trigger reset.
                std::cout << "Keyboard: Detected reset condition on clock line.\n";
                resetting_ = true;
            }
            clock_line_high_ticks_ = 0;
        }
        clock_line_state_ = state;
    }

    void reset() {
        clock_line_state_ = false;
        send_reset_ = false;
        resetting_ = false;
        clock_line_low_ticks_ = 0;
        clock_line_high_ticks_ = 0;
    }

    void tick() {
        if (!clock_line_state_) {
            clock_line_low_ticks_++;
            std::cout << std::format("Keyboard: Clock line low ticks: {}\n", clock_line_low_ticks_);
        }
        else {
            clock_line_high_ticks_++;
            if (resetting_ && clock_line_high_ticks_ >= kResetByteDelayTicks) {
                // Clock line has been held high long enough after reset to send reset byte.
                send_reset_ = true;
                resetting_ = false;
            }
        }
    }

    bool getScanCode(uint8_t& byte) {
        if (send_reset_) {
            byte = kResetByte;
            send_reset_ = false;
            return true;
        }
        return false;
    }

private:
    bool clock_line_state_{true};
    bool send_reset_{false};
    bool resetting_{false};
    uint32_t clock_line_low_ticks_{0};
    uint32_t clock_line_high_ticks_{0};
};


#endif //SDL_MIN_KEYBOARD_H
