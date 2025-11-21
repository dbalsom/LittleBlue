#pragma once

#include <cstdint>
#include <vector>

#include "bios.h"
#include "Cga.h"
#include "Dmac.h"
#include "Pic.h"
#include "Pit.h"
#include "Ppi.h"
#include "Fdc.h"
#include "Keyboard.h"

#define ROM_BASE_ADDRESS 0xFE000
#define CONVENTIONAL_RAM_SIZE 0xB8000
#define CGA_ADDRESS 0xB8000

class Bus
{
public:
    Bus() :
        ram_(CONVENTIONAL_RAM_SIZE), rom_(0x2000) {
        rom_.assign(U18, U18 + sizeof(U18));
        pit_.setGate(0, true);
        pit_.setGate(1, true);
        pit_.setGate(2, true);
        // Wire DMAC & PIC to the FDC so it can perform DMA and execute interrupts.
        fdc_.attachDMAC(&dmac_);
        fdc_.attachPIC(&pic_);
    }

    uint8_t* ram() { return &ram_[0]; }
    [[nodiscard]] size_t ramSize() const { return ram_.size(); }

    // Device accessors
    CGA* cga() { return &cga_; }
    PIC* pic() { return &pic_; }
    PIT* pit() { return &pit_; }
    PPI* ppi() { return &ppi_; }
    FDC* fdc() { return &fdc_; }
    DMAC* dmac() { return &dmac_; }

    // Read a byte from a physical address without changing bus state.
    // This allows tools (disassembler/UI) to inspect memory (RAM or ROM) directly.
    [[nodiscard]] uint8_t peek(const uint32_t address) const {
        // Real-mode uses 20-bit physical addressing; mask to 20 bits in callers if needed.
        if (address >= ROM_BASE_ADDRESS) {
            uint32_t rom_index = address - ROM_BASE_ADDRESS;
            if (rom_index < rom_.size()) {
                return rom_[rom_index];
            }
            return 0xff;
        }
        if (address >= CGA_ADDRESS && address < CGA_ADDRESS + 0x4000) {
            return cga_.readMem(address - CGA_ADDRESS);
        }
        if (address >= CONVENTIONAL_RAM_SIZE) {
            return 0xff; // open bus / video area not resident here
        }
        if (address < ram_.size()) {
            return ram_[address];
        }
        return 0xff;
    }

    [[nodiscard]] size_t romSize() const { return rom_.size(); }

    void reset() {
        std::ranges::fill(ram_, 0);
        dmac_.reset();
        pic_.reset();
        pit_.reset();
        ppi_.reset();
        fdc_.reset();
        kb_.reset();
        cga_.reset();
        pit_phase_ = 2;
        last_counter0_output_ = false;
        last_counter1_output_ = true;
        counter2_output_ = false;
        counter2_gate_ = false;
        speaker_mask_ = false;
        speaker_output_ = false;
        dma_state_ = sIdle;
        passive_or_halt_ = true;
        lock_ = false;
        previous_passive_or_halt_ = true;
        last_non_dma_ready_ = true;
        cga_phase_ = 0;
        last_kb_disabled_ = false;
        last_kb_cleared_ = false;
        last_irq6_ = false;
    }

    void stubInit() {
        pic_.stubInit();
        pit_.stubInit();
        pit_phase_ = 2;
        last_counter0_output_ = true;
    }

    void setSpeakerCallback(PcSpeakerCallback callback) {
        speaker_callback_ = std::move(callback);
    }

    void startAccess(const uint32_t address, const int type) {
        address_ = address;
        type_ = type;
        cycle_ = 0;
    }

    void tick() {
        _ticks++;
        cga_.tick();
        cga_phase_ = (cga_phase_ + 3) & 0x0f;
        pit_phase_++;

        // Handle PIT updates every 4 ticks
        if (pit_phase_ == 4) {
            pit_phase_ = 0;
            pit_.tick();
            bool counter0Output = pit_.getOutput(0);
            if (last_counter0_output_ != counter0Output) {
                pic_.setIRQLine(0, counter0Output);
            }
            last_counter0_output_ = counter0Output;

            bool counter1Output = pit_.getOutput(1);
            if (counter1Output && !last_counter1_output_ && !dack0()) {
                dmac_.setDMARequestLine(0, true);
            }
            last_counter1_output_ = counter1Output;

            bool counter2Output = pit_.getOutput(2);
            if (counter2_output_ != counter2Output) {
                counter2_output_ = counter2Output;
                setSpeakerOutput();
                ppi_.setC(5, counter2Output);
                updatePPI();
            }
        }

        if (speaker_cycle_ != 0) {
            --speaker_cycle_;
            if (speaker_cycle_ == 0) {
                speaker_output_ = next_speaker_output_;
                ppi_.setC(4, speaker_output_);
                updatePPI();
            }
        }

        if ((_ticks & 0xF) == 0) {
            // Check and clear the keyboard
            const auto kb_cleared = ppi_.getB(7);
            const auto kb_disabled = !ppi_.getB(6);
            if (kb_disabled && !last_kb_disabled_) {
                // Keyboard was just disabled.
                std::cout << "Bus: Disabling keyboard" << std::endl;
                kb_.setClockLineState(false);
            }
            else if (!kb_disabled && last_kb_disabled_) {
                // Keyboard was just enabled.
                std::cout << "Bus: Enabling keyboard" << std::endl;
                kb_.setClockLineState(true);
            }

            if (kb_cleared && !last_kb_cleared_) {
                // KSR was just cleared.
                std::cout << "Bus: Clearing KSR & Interrupt" << std::endl;
                // Clear any pending IRQ 1.
                pic_.setIRQLine(1, false);
                // Clear the KSR attached to PPI port A.
                for (int i = 0; i < 8; ++i) {
                    ppi_.setA(i, false);
                }
            }
            else if (!kb_disabled && last_kb_disabled_) {
                // Keyboard was just enabled.
                std::cout << "Bus: Re-enabling keyboard" << std::endl;
            }
            last_kb_disabled_ = kb_disabled;
            last_kb_cleared_ = kb_cleared;
        }

        if ((_ticks & 0x3FFF) == 0) {
            // Slow tick = ~1.144ms. Good for ticking ms-scale delays.

            // Tick the keyboard. The keyboard needs to be ticked to produce reset bytes after a delay when reset,
            // and to produce type-matic repeat keys.
            kb_.tick();
            if (uint8_t b = 0; kb_.getScanCode(b)) {
                // Keyboard-originated scancode (reset byte or type-matic key)
                std::cout << std::format("Keyboard generated scancode: {:02X}", b) << std::endl;
                for (int i = 0; i < 8; ++i) {
                    const auto bit = (b >> i) & 1;
                    ppi_.setA(i, bit != 0);
                }
                pic_.setIRQLine(1, true);
            }

            // Tick the FDC. The FDC needs to be ticked to simulate operational delays.
            fdc_.tick();
        }

        // // Handle FDC interrupts
        // if (_fdc.pollIrq()) {
        //     //std::cout << "FDC IRQ 6 detected\n" << std::flush;
        //     if (!_lastIRQ6) {
        //         std::cout << "FDC IRQ 6 asserted\n" << std::flush;
        //     }
        //     _pic.setIRQLine(6, true);
        //     _lastIRQ6 = true;
        // }
        // else {
        //     _pic.setIRQLine(6, false);
        //     _lastIRQ6 = false;
        // }


        // Set to false to implement 5160s without the U90 fix and 5150s
        // without the U101 fix as described in
        // http://www.vcfed.org/forum/showthread.php?29211-Purpose-of-U90-in-XT-second-revision-board
        bool hasDMACFix = true;

        if (type_ != 2 || (address_ & 0x3e0) != 0x000 || !hasDMACFix) {
            last_non_dma_ready_ = nonDMAReady();
        }
        //if (_previousLock && !_lock)
        //    _previousLock = false;
        //_previousLock = _lock;
        switch (dma_state_) {
            case sIdle:
                if (dmac_.getHoldRequestLine()) {
                    dma_state_ = sDREQ;
                }
                break;
            case sDREQ:
                dma_state_ = sHRQ; //(_passiveOrHalt && !_previousLock) ? sHRQ : sHoldWait;
                break;
            case sHRQ:
                //_dmaState = _lastNonDMAReady ? sAEN : sPreAEN;
                if ((passive_or_halt_ || previous_passive_or_halt_) && !lock_ && last_non_dma_ready_) {
                    dma_state_ = sAEN;
                }
                break;
            //case sHoldWait:
            //    if (_passiveOrHalt && !_previousLock)
            //        _dmaState = _lastNonDMAReady ? sAEN : sPreAEN;
            //    break;
            //case sPreAEN:
            //    if (_lastNonDMAReady)
            //        _dmaState = sAEN;
            //    break;
            case sAEN:
                dma_state_ = s0;
                break;
            case s0:
                dmac_.setDMARequestLine(0, false);
                dma_state_ = s1;
                break;
            case s1:
                dma_state_ = s2;
                break;
            case s2:
                // Device read/write occurs on S2
                if (dmac_.getActiveChannel() == 2) {
                    // Servicing FDC
                    auto addr = dmaAddressHigh(2) + static_cast<uint32_t>(dmac_.getAddress());

                    if (dmac_.isReading()) {
                        std::cout << std::format("DMAC Channel 2 READ from address {:05X}\n", addr);
                    }
                    else if (dmac_.isWriting()) {
                        const auto b = fdc_.dmaDeviceRead();
                        //std::cout << std::format("DMAC Channel 2 WRITE to address {:02X}->{:05X}\n", b, addr);
                        ram_[addr & 0xFFFFF] = b;
                    }
                    dmac_.service();
                    if (dmac_.isAtTerminalCount()) {
                        // Notify FDC that DMA operation is complete
                        std::cout << std::format(
                            "DMAC Channel 2 terminal count reached at address [{:05X}], page [{:02X}], notifying FDC\n",
                            addr, dma_pages_[2]);
                        fdc_.dmaDeviceEOP();
                    }

                }
                else {
                    dmac_.service();
                }

                dma_state_ = s3;
                break;
            case s3:
                dma_state_ = s4;
                break;
            case s4:
                dma_state_ = sDelayedT1;
                dmac_.dmaCompleted();
                break;
            case sDelayedT1:
                dma_state_ = sDelayedT2;
                cycle_ = 0;
                break;
            case sDelayedT2:
                dma_state_ = sDelayedT3;
                break;
            case sDelayedT3:
                dma_state_ = sIdle;
                break;
            default:
                break;
        }
        previous_passive_or_halt_ = passive_or_halt_;

        last_non_dma_ready_ = nonDMAReady();
        ++cycle_;
    }

    bool ready() {
        return dmaReady() && nonDMAReady();
    }

    void write(uint8_t data) {
        if (type_ == 2) {
            // IO write
            switch (address_ & 0x3e0) {
                case 0x00:
                    dmac_.write(address_ & 0x0f, data);
                    break;
                case 0x20:
                    pic_.write(address_ & 1, data);
                    break;
                case 0x40:
                    pit_.write(address_ & 3, data);
                    break;
                case 0x60:
                    ppi_.write(address_ & 3, data);
                    updatePPI();
                    break;
                case 0x80:
                    // Don't ask me why the DMA page registers are ordered this way.
                    switch (address_) {
                        case 0x87:
                            dma_pages_[0] = data;
                            break;
                        case 0x83:
                            dma_pages_[1] = data;
                            break;
                        case 0x81:
                            std::cout << std::format("Write to DMA page register 2: {:02X}\n", data);
                            dma_pages_[2] = data;
                            break;
                        case 0x82:
                            dma_pages_[3] = data;
                            break;
                        default:
                            break;
                    }
                    break;
                case 0xa0:
                    nmi_enabled_ = (data & 0x80) != 0;
                    break;
                case 0x3C0:
                    cga_.writeIO(address_ & 0x0F, data);
                    break;
                case 0x3E0:
                    fdc_.writeIO(address_ & 7, data);
                    break;

                default:
                    break;
            }
        }

        else {
            // Memory write
            if (address_ < CONVENTIONAL_RAM_SIZE) {
                ram_[address_] = data;
            }
            else if (address_ >= CGA_ADDRESS && address_ < CGA_ADDRESS + 0x4000) {
                cga_.writeMem(address_ - CGA_ADDRESS, data);
            }
        }
    }

    uint8_t read() {
        if (type_ == 0) {
            // Interrupt acknowledge
            auto i = pic_.interruptAcknowledge();
            if (i != 0xFF && i != 0x08) {
                std::cout << "Interrupt acknowledge: vector " << std::hex << static_cast<int>(i) << std::dec << "\n" <<
                    std::flush;
            }
            return i;
        }
        if (type_ == 1) {
            // IO read

            // noisy trace debu
            //std::cout << "IO read from port " << std::hex << _address << std::dec << "\n" << std::flush;

            // Read from IO port
            switch (address_ & 0x3e0) {
                case 0x00:
                    return dmac_.read(address_ & 0x0f);
                case 0x20:
                    return pic_.read(address_ & 1);
                case 0x40:
                {
                    const uint8_t b = pit_.read(address_ & 3);
                    // std::cout << "PIT read from port " << std::hex << (_address & 3)
                    //     << ": " << std::hex << static_cast<int>(b) << std::dec << "\n";
                    return b;
                }
                case 0x60:
                {
                    //std::cout << "PPI read from port " << std::hex << (_address & 3) << std::dec << "\n";
                    const uint8_t b = ppi_.read(address_ & 3);
                    updatePPI();
                    return b;
                }
                case 0x80:
                    switch (address_) {
                        case 0x87:
                            return dma_pages_[0];
                        case 0x83:
                            return dma_pages_[1];
                        case 0x81:
                            return dma_pages_[2];
                        case 0x82:
                            return dma_pages_[3];
                        default:
                            break;
                    }
                    break;
                case 0x3C0:
                    return cga_.readIO(address_ & 0x0F);
                case 0x3E0:
                    return fdc_.readIO(address_ & 7);
                default:
                    //std::cout << "Unhandled IO read from port " << std::hex << _address << std::dec << "\n";
                    return 0xFF;
            }
        }

        if (address_ < CONVENTIONAL_RAM_SIZE) {
            return ram_[address_];
        }
        if (address_ >= ROM_BASE_ADDRESS) {
            // Read from ROM.
            return rom_[address_ - ROM_BASE_ADDRESS];
        }
        if (address_ >= CGA_ADDRESS && address_ < CGA_ADDRESS + 0x4000) {
            return cga_.readMem(address_ - CGA_ADDRESS);
        }
        // No match? Return open bus.
        return 0xFF;
    }

    bool interruptPending() { return pic_.interruptPending(); }

    int pitBits() {
        return (pit_phase_ == 1 || pit_phase_ == 2 ? 1 : 0) +
            (counter2_gate_ ? 2 : 0) + (pit_.getOutput(2) ? 4 : 0);
    }

    void setPassiveOrHalt(bool v) { passive_or_halt_ = v; }

    [[nodiscard]] bool getAEN() const {
        return dma_state_ == sAEN || dma_state_ == s0 || dma_state_ == s1 ||
            dma_state_ == s2 || dma_state_ == s3 || dma_state_ == sWait ||
            dma_state_ == s4;
    }

    uint8_t getDMA() {
        return dmac_.getRequestLines() | (dack0() ? 0x10 : 0);
    }

    std::string snifferExtra() {
        return ""; //hex(_pit.getMode(1), 4, false) + " ";
    }

    [[nodiscard]] int getBusOperation() const {
        switch (dma_state_) {
            case s2:
                return 5; // memr
            case s3:
                return 2; // iow
            default:
                return 0;
        }
    }

    bool getDMAS3() { return dma_state_ == s3; }
    bool getDMADelayedT2() { return dma_state_ == sDelayedT2; }

    uint32_t getDMAAddress() {
        return dmaAddressHigh(dmac_.getActiveChannel()) + dmac_.getAddress();
    }

    void setLock(bool lock) { lock_ = lock; }
    uint8_t getIRQLines() { return pic_.getIRQLines(); }

    uint8_t getDMAS() {
        if (dma_state_ == sAEN || dma_state_ == s0 || dma_state_ == s1 ||
            dma_state_ == s2 || dma_state_ == s3 || dma_state_ == sWait)
            return 3;
        if (dma_state_ == sHRQ || dma_state_ == sHoldWait ||
            dma_state_ == sPreAEN)
            return 1;
        return 0;
    }

    uint8_t getCGA() {
        return cga_phase_ >> 2;
    }

private
:
    bool dmaReady() {
        if (dma_state_ == s1 || dma_state_ == s2 || dma_state_ == s3 ||
            dma_state_ == sWait || dma_state_ == s4 || dma_state_ == sDelayedT1 ||
            dma_state_ == sDelayedT2 /*|| _dmaState == sDelayedT3*/)
            return false;
        return true;
    }

    bool nonDMAReady() {
        if (type_ == 1 || type_ == 2) // Read port, write port
            return cycle_ > 2; // System board adds a wait state for onboard IO devices
        return true;
    }

    bool dack0() {
        return dma_state_ == s1 || dma_state_ == s2 || dma_state_ == s3 ||
            dma_state_ == sWait;
    }

    void setSpeakerOutput() {
        bool o = !(counter2_output_ && speaker_mask_);

        const auto pit_ticks = pit_.getTicks();
        speaker_callback_(pit_ticks, counter2_output_, speaker_mask_);

        if (next_speaker_output_ != o) {
            if (speaker_output_ == o) {
                speaker_cycle_ = 0;
            }
            else {
                speaker_cycle_ = o ? 3 : 2;
            }
            next_speaker_output_ = o;
        }
    }

    void updatePPI() {
        bool speakerMask = ppi_.getB(1);
        if (speakerMask != speaker_mask_) {
            speaker_mask_ = speakerMask;
            setSpeakerOutput();
        }
        counter2_gate_ = ppi_.getB(0);
        pit_.setGate(2, counter2_gate_);

        if (!ppi_.getB(3)) {
            // Present switches 1 to 4
            ppi_.setC(0, (dip_switch1_ & 0x01) != 0);
            ppi_.setC(1, (dip_switch1_ & 0x02) != 0);
            ppi_.setC(2, (dip_switch1_ & 0x04) != 0);
            ppi_.setC(3, (dip_switch1_ & 0x08) != 0);
        }
        else {
            // Present switches 5 to 8
            ppi_.setC(0, (dip_switch1_ & 0x10) != 0);
            ppi_.setC(1, (dip_switch1_ & 0x20) != 0);
            ppi_.setC(2, (dip_switch1_ & 0x40) != 0);
            ppi_.setC(3, (dip_switch1_ & 0x80) != 0);
        }

    }

    uint32_t dmaAddressHigh(const int channel) {
        //static const int pageRegister[4] = {0x83, 0x83, 0x81, 0x82};
        return static_cast<uint32_t>(dma_pages_[channel & 3]) << 16;
    }

    enum DMAState
    {
        sIdle,
        sDREQ,
        sHRQ,
        sHoldWait,
        sPreAEN,
        sAEN,
        s0,
        s1,
        s2,
        s3,
        sWait,
        s4,
        sDelayedT1,
        sDelayedT2,
        sDelayedT3,
    };

    std::vector<uint8_t> ram_;
    std::vector<uint8_t> rom_;
    uint32_t address_;
    int type_;
    int cycle_;
    DMAC dmac_;
    PIC pic_;
    PIT pit_;
    PPI ppi_;
    CGA cga_;
    FDC fdc_;
    Keyboard kb_;
    uint8_t dip_switch1_{0b0110'1101};
    int pit_phase_;
    bool last_counter0_output_;
    bool last_irq6_{false};
    bool last_counter1_output_;
    bool counter2_output_;
    bool counter2_gate_;
    bool speaker_mask_;
    bool speaker_output_;
    bool next_speaker_output_;
    uint16_t dma_address_;
    int dma_cycles_;
    int dma_type_;
    int speaker_cycle_;
    uint8_t dma_pages_[4];
    bool nmi_enabled_;
    bool passive_or_halt_;
    DMAState dma_state_;
    bool lock_;
    bool previous_passive_or_halt_;
    bool last_non_dma_ready_;
    uint8_t cga_phase_;
    bool last_kb_disabled_{false};
    bool last_kb_cleared_{false};
    PcSpeakerCallback speaker_callback_{nullptr};
    uint64_t
    _ticks{0};
};
