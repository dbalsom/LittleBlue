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
            const auto& c = channels_[i];
            s.channels[i].baseAddress = c.base_address;
            s.channels[i].baseWordCount = c.base_word_count;
            s.channels[i].currentAddress = c.current_address;
            s.channels[i].currentWordCount = c.current_word_count;
            s.channels[i].mode = c.mode;
        }
        s.status = status_;
        s.command = command_;
        s.request = request_;
        s.mask = mask_;
        s.ack = channel_ == -1 ? 0x00 : 0x01 << channel_;
        return s;
    }

    void reset() {
        for (auto& channel : channels_) {
            channel.reset();
        }
        temporary_address_ = 0;
        temporary_word_count_ = 0;
        status_ = 0;
        command_ = 0;
        temporary_ = 0;
        mask_ = 0xf;
        request_ = 0;
        ack_ = 0;
        flip_flop_ = false;
        channel_ = -1;
        need_high_address_ = true;
    }

    void write(const uint32_t address, const uint8_t data) {
        switch (address) {
            case 0x00:
            case 0x02:
            case 0x04:
            case 0x06:
                channels_[(address & 6) >> 1].setAddress(flip_flop_, data);
                flip_flop_ = !flip_flop_;
                break;
            case 0x01:
            case 0x03:
            case 0x05:
            case 0x07:
                channels_[(address & 6) >> 1].setCount(flip_flop_, data);
                flip_flop_ = !flip_flop_;
                break;
            case 0x08:
                // Write Command Register
                command_ = data;
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
                    mask_ |= b;
                else
                    mask_ &= ~b;
            }
            break;
            case 0x0b:
                // Write Mode Register
                channels_[data & 3].mode = data;
                break;
            case 0x0c:
                // Clear Byte Pointer Flip/Flop
                flip_flop_ = false;
                break;
            case 0x0d:
                // Master Clear
                reset();
                break;
            case 0x0e:
                // Clear Mask Register
                mask_ = 0;
                break;
            case 0x0f:
                // Write All Mask Register Bits
                mask_ = data;
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
                flip_flop_ = !flip_flop_;
                return channels_[(address & 6) >> 1].getAddress(!flip_flop_);
            case 0x01:
            case 0x03:
            case 0x05:
            case 0x07:
                flip_flop_ = !flip_flop_;
                return channels_[(address & 6) >> 1].getCount(!flip_flop_);
            case 0x08:
                // Read Status Register
                return status_;
            case 0x0d:
                // Read Temporary Register
                return temporary_;
            default:
                // Illegal
                return 0xff;
        }
    }

    void setDMARequestLine(int line, bool state) {
        setRequest(line, state != dreqSenseActiveLow());
    }

    [[nodiscard]] uint8_t getRequestLines() const {
        return request_;
    }

    bool getHoldRequestLine() {
        if (channel_ != -1) {
            return true;
        }
        if (disabled()) {
            return false;
        }
        for (int i = 0; i < 4; ++i) {
            int channel = i;
            if (rotatingPriority()) {
                channel = (channel + priority_channel_) & 3;
            }
            if ((request_ & (1 << channel)) != 0) {
                channel_ = channel;
                priority_channel_ = (channel + 1) & 3;
                return true;
            }
        }
        return false;
    }

    void dmaCompleted() {
        channel_ = -1;
    }

    uint8_t dmaRead() {
        if (memoryToMemory() && channel_ == 1) {
            return temporary_;
        }
        return 0xff;
    }

    void dmaWrite(const uint8_t data) {
        if (memoryToMemory() && channel_ == 0) {
            temporary_ = data;
        }
    }

    [[nodiscard]]
    bool isReading(int channel = -1) const {
        if (channel == -1) {
            channel = channel_;
        }
        if (channel == -1) {
            return false;
        }
        auto& c = channels_[channel_ & 3];

        return c.isReadMode();
    }

    [[nodiscard]]
    bool isWriting(int channel = -1) const {
        if (channel == -1) {
            channel = channel_;
        }
        if (channel == -1) {
            return false;
        }
        auto& c = channels_[channel_ & 3];
        return c.isWriteMode();
    }

    [[nodiscard]]
    uint16_t getAddress(int channel = -1) const {
        if (channel == -1) {
            channel = channel_;
        }
        if (channel == -1) {
            return false;
        }
        const uint16_t address = channels_[channel & 3].current_address;
        return address;
    }

    [[nodiscard]]
    uint16_t getWordCount(int channel = -1) const {
        if (channel == -1) {
            channel = channel_;
        }
        if (channel == -1) {
            return false;
        }
        const uint16_t count = channels_[channel & 3].current_word_count;
        return count;
    }

    uint16_t service() {
        if (channel_ == -1) {
            return 0;
        }
        auto& c = channels_[channel_ & 3];

        if (!c.isAtTerminalCount() || c.isAutoinitialize()) {
            c.incrementAddress();
        }
        if (c.isAtTerminalCount()) {
            status_ |= 0x01 << channel_;
        }

        return c.current_address;
    }

    [[nodiscard]]
    bool isAtTerminalCount(int channel = -1) const {
        if (channel == -1) {
            channel = channel_;
        }
        if (channel == -1) {
            return false;
        }
        return channels_[channel & 3].isAtTerminalCount();
    }

    [[nodiscard]]
    int getActiveChannel() const {
        return channel_;
    }

private:
    struct Channel
    {
        void setAddress(const bool high, const uint8_t data) {
            if (!high) {
                base_address = (base_address & 0xff00) + data;
                current_address = (current_address & 0xff00) + data;
            }
            else {
                base_address = (base_address & 0xff) + (data << 8);
                current_address = (current_address & 0xff) + (data << 8);
            }
        }

        void setCount(const bool high, uint8_t data) {
            tc = false;
            if (!high) {
                base_word_count = (base_word_count & 0xff00) + data;
                current_word_count = (current_word_count & 0xff00) + data;
            }
            else {
                base_word_count = (base_word_count & 0xff) + (data << 8);
                current_word_count = (current_word_count & 0xff) + (data << 8);
            }
        }

        [[nodiscard]] uint8_t getAddress(const bool high) const {
            if (high) {
                return current_address >> 8;
            }
            return current_address & 0xff;
        }

        [[nodiscard]] uint8_t getCount(const bool high) const {
            if (high) {
                return current_word_count >> 8;
            }
            return current_word_count & 0xff;
        }

        void reset() {
            base_address = 0;
            base_word_count = 0;
            current_address = 0;
            current_word_count = 0;
            mode = 0;
        }

        void incrementAddress() {
            if (!isAddressDecrement()) {
                ++current_address;
            }
            else {
                --current_address;
            }
            --current_word_count;
            if (current_word_count == 0xFFFF) {
                // We allow the word count to roll over because we do a transfer on a 0 count.
                // We've hit terminal count at this point.
                tc = true;

                // Now we just need to handle autoinitialization, or reset the word count to 0.
                if (isAutoinitialize()) {
                    // It may seem counterintuitive, but the TC flag is not reset by auto-initialization.
                    current_address = base_address;
                    current_word_count = base_word_count;
                }
                else {
                    current_word_count = 0;
                }
            }
        }

        [[nodiscard]] bool isWriteMode() const { return (mode & 0x0c) == 4; }
        [[nodiscard]] bool isReadMode() const { return (mode & 0x0c) == 8; }
        [[nodiscard]] bool isVerifyMode() const { return (mode & 0x0c) == 0; }
        [[nodiscard]] bool isAutoinitialize() const { return (mode & 0x10) != 0; }
        [[nodiscard]] bool isAddressDecrement() const { return (mode & 0x20) != 0; }
        [[nodiscard]] bool isDemand() const { return (mode & 0xc0) == 0x00; }
        [[nodiscard]] bool isSingle() const { return (mode & 0xc0) == 0x40; }
        [[nodiscard]] bool isBlock() const { return (mode & 0xc0) == 0x80; }
        [[nodiscard]] bool isCascade() const { return (mode & 0xc0) == 0xc0; }
        [[nodiscard]] bool isAtTerminalCount() const { return tc; }

        uint16_t base_address{};
        uint16_t base_word_count{};
        uint16_t current_address{};
        uint16_t current_word_count{};
        uint8_t mode{}; // Only 6 bits used
        bool tc = false;
    };

    [[nodiscard]]
    bool memoryToMemory() const { return (command_ & 1) != 0; }

    bool channel0AddressHold() { return (command_ & 2) != 0; }
    bool disabled() { return (command_ & 4) != 0; }
    bool compressedTiming() { return (command_ & 8) != 0; }
    bool rotatingPriority() { return (command_ & 0x10) != 0; }
    bool extendedWriteSelection() { return (command_ & 0x20) != 0; }


    bool dreqSenseActiveLow() { return (command_ & 0x40) != 0; }
    bool dackSenseActiveHigh() { return (command_ & 0x80) != 0; }

    void setRequest(const int line, const bool active) {
        const uint8_t b = 1 << line;
        const uint8_t s = 0x10 << line;
        if (active) {
            request_ |= b;
            status_ |= s;
        }
        else {
            request_ &= ~b;
            status_ &= ~s;
        }
    }

    Channel channels_[4] = {};
    uint16_t temporary_address_ = 0;
    uint16_t temporary_word_count_ = 0;
    uint8_t status_ = 0;
    uint8_t command_ = 0;
    uint8_t temporary_ = 0;
    uint8_t mask_ = 0; // Only 4 bits used
    uint8_t request_ = 0; // Only 4 bits used
    uint8_t ack_ = 0; // Only 4 bits used
    bool flip_flop_ = false;
    int channel_ = 0;
    int priority_channel_ = 0;
    bool need_high_address_ = false;
};
