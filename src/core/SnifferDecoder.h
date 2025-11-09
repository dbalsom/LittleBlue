#ifndef SNIFFER_H
#define SNIFFER_H

#include <format>

#include "Disassembler.h"

class SnifferDecoder
{
public:
    void reset()
    {
        _cpu_rqgt0 = false;  // Used by 8087 for bus mastering, NYI
        _cpu_ready = true;   // Used for DMA and wait states
        _cpu_test = false;   // Used by 8087 for synchronization, NYI
        _cpu_lock = false;
        _bus_dma = 0;        // NYI
        _dmas = 0;
        _bus_irq = 0xfc;     // NYI
        _int = false;
        _bus_iochrdy = true; // Used for wait states, NYI
        _bus_aen = false;    // Used for DMA
        _bus_tc = false;     // Used for DMA, NYI
        _cga = 0;

        // T-Cycle
        _t = 0;
        // Next T-Cycle
        _tNext = 0;
        _d = -1;
        _queueLength = 0;
        _lastS = 0;
        _cpu_status = 7;
        _cpu_qs = 0;
        _cpu_next_qs = 0;

        _disassembler.reset();
    }

    std::string getLine()
    {
        // Character representing queue status as of last cycle.
        // '.' - No operation
        // 'F' - First byte fetched from queue
        // 'E' - Queue emptied
        // 'S' - Subsequent byte fetched from queue
        static constexpr char qsc[] = ".FES";
        static constexpr char sc[] = "ARWHCrwp";
        static constexpr char dmasc[] = " h:H";

        auto hex = [](uint32_t value, int width, bool showPrefix = false) -> std::string {
            return std::format("{}{:0{}X}", showPrefix ? "0x" : "", value, width);
        };

        std::string line;

        // Emit ALE status
        line += _bus_ale ? "A:" : "  ";
        if (_bus_ale) _bus_ale = false;

        // Emit address bus value
        line += hex(_bus_address, 5, false) + ":";
        line += hex(_cpu_address, 5, false) + ":";

        // Emit data bus value when defined
        line += _isaDataFloating ? "  " : hex(_bus_data, 2, false);

        line += " ";

        // Emit bus status
        switch (_cpu_status) {
            case 0: line += "INTA"; break;
            case 1: line += "IOR "; break;
            case 2: line += "IOW "; break;
            case 3: line += "HALT"; break;
            case 4: line += "CODE"; break;
            case 5: line += "MEMR"; break;
            case 6: line += "MEMW"; break;
            case 7: line += "PASV"; break;
            default: break;
        }

        line += " " + std::string(1, qsc[_cpu_qs]) + sc[_cpu_status]
             + (_cpu_rqgt0 ? "G" : ".") + (_cpu_ready ? "." : "z")
             + (_cpu_test ? "T" : ".") + (_cpu_lock ? "L" : ".")
             + "  ";

        line += " " + hex(_bus_dma, 2, false) + dmasc[_dmas] + " "
             + hex(_bus_irq, 2, false) + (_int ? "I" : " ") + " "
             + hex(_bus_pit, 1, false) + hex(_cga, 1, false) + " "
             + (_bus_ior ? "R" : ".") + (_bus_iow ? "W" : ".")
             + (_bus_memr ? "r" : ".") + (_bus_memw ? "w" : ".")
             + (_bus_iochrdy ? "." : "z") + (_bus_aen ? "D" : ".")
             + (_bus_tc ? "T" : ".");

        line += "  ";
        if (_cpu_status != 7 && _cpu_status != 3)
            switch (_tNext) {
                case 0:
                case 4:
                    // T1 state occurs after transition out of passive
                    _tNext = 1;
                    break;
                case 1:
                    _tNext = 2;
                    break;
                case 2:
                    _tNext = 3;
                    break;
                case 3:
                    _tNext = 5;
                    break;
            }
        else
            switch (_t) {
                case 4:
                    _d = -1;
                case 0:
                    _tNext = 0;
                    break;
                case 1:
                case 2:
                    _tNext = 6;
                    break;
                case 3:
                case 5:
                    _d = -1;
                    _tNext = 4;
                    break;
            }
        switch (_t) {
            case 0: line += "  "; break;
            case 1: line += "T1"; break;
            case 2: line += "T2"; break;
            case 3: line += "T3"; break;
            case 4: line += "T4"; break;
            case 5: line += "Tw"; break;
            default: line += "!c"; _tNext = 0; break;
        }
        line += " ";
        if (_bus_aen)
            switch (_d) {
                // This is a bit of a hack since we don't have access
                // to the right lines to determine the DMA state
                // properly. This probably breaks for memory-to-memory
                // copies.
                case -1: _d = 0; break;
                case 0: _d = 1; break;
                case 1: _d = 2; break;
                case 2: _d = 3; break;
                case 3:
                case 5:
                    if ((_bus_iow && _bus_memr) || (_bus_ior && _bus_memw))
                        _d = 4;
                    else
                        _d = 5;
                    break;
                case 4:
                    _d = -1;
            }
        switch (_d) {
            case -1: line += "  "; break;
            case 0: line += "S0"; break;
            case 1: line += "S1"; break;
            case 2: line += "S2"; break;
            case 3: line += "S3"; break;
            case 4: line += "S4"; break;
            case 5: line += "SW"; break;
            default: line += "!d"; _t = 0; break;
        }

        line += " ";

        // Emit queue contents.
        line += "[";
        for (int i = 0; i < 4; ++i) {
            if (i < _queueLength) {
                line += hex(_queue[i], 2, false);
            }
            else {
                line += "  ";
            }
        }
        line += "] ";

        // Emit instruction if applicable
        std::string instruction;
        if (_cpu_qs != 0) {
            if (_cpu_qs == 2) {
                // Queue flushed, reset queueLength.
                _queueLength = 0;
            }
            else {
                // First or subsequent byte fetched from queue.
                uint8_t b = _queue[0];
                for (int i = 0; i < 3; ++i) {
                    _queue[i] = _queue[i + 1];
                }
                --_queueLength;
                if (_queueLength < 0) {
                    // Queue underrun, shouldn't happen
                    line += "!g";
                    _queueLength = 0;
                }
                instruction = _disassembler.disassemble(b, _cpu_qs == 1);
            }
        }

        if (_tNext == 4 || _d == 4) {
            if (_tNext == 4 && _d == 4)
                line += "!e";
            std::string seg;
            switch (_cpu_address & 0x30000) {
                case 0x00000: seg = "ES "; break;
                case 0x10000: seg = "SS "; break;
                case 0x20000: seg = "CS "; break;
                case 0x30000: seg = "DS "; break;
            }
            std::string type = "-";
            if (_lastS == 0)
                line += hex(_bus_data, 2, false) + " <-i           ";
            else {
                if (_lastS == 4) {
                    type = "f";
                    seg = "   ";
                }
                if (_d == 4) {
                    type = "d";
                    seg = "   ";
                }
                line += hex(_bus_data, 2, false) + " ";
                if (_bus_ior || _bus_memr)
                    line += "<-" + type + " ";
                else
                    line += type + "-> ";
                if (_bus_memr || _bus_memw)
                    line += "[" + seg + hex(_bus_address, 5, false) + "]";
                else
                    line += "port[" + hex(_bus_address, 4, false) + "]";
                if (_lastS == 4 && _d != 4) {
                    if (_queueLength >= 4)
                        line += "!f";
                    else {
                        _queue[_queueLength] = _bus_data;
                        ++_queueLength;
                    }
                }
            }
            line += " ";
        }
        else
            line += "                  ";
        if (_cpu_qs != 0)
            line += std::string(1, qsc[_cpu_qs]);
        else
            line += " ";
        line += " " + instruction;
        _lastS = _cpu_status;
        _t = _tNext;
        if (_t == 4 || _d == 4) {
            _bus_ior = false;
            _bus_iow = false;
            _bus_memr = false;
            _bus_memw = false;
        }
        // 8086 Family Users Manual page 4-37 clock cycle 12: "remember
        // the queue status lines indicate queue activity that has occurred in
        // the previous clock cycle".
        _cpu_qs = _cpu_next_qs;
        _cpu_next_qs = 0;
        return line;
    }
    void queueOperation(int qs) { _cpu_next_qs = qs; }
    void setStatus(int s) {
        _cpu_last_status = _cpu_status;
        _cpu_status = s;
        // Bus proceeding from PASV to any other status triggers ALE signal.
        if ((_cpu_last_status == 7) && (_cpu_status < 7)) {
            _bus_ale = true;
        }
        else {
            _bus_ale = false;
        }
    }
    void setStatusHigh(int segment)
    {
        _cpu_address &= 0xcffff;
        switch (segment) {
            case 0:  // ES
                break;
            case 2:  // SS
                _cpu_address |= 0x10000;
                break;
            case 3:  // DS
                _cpu_address |= 0x30000;
                break;
            default: // CS or none
                _cpu_address |= 0x20000;
                break;
        }
        setBusFloating();
    }
    void setInterruptFlag(bool intf)
    {
        _cpu_address = (_cpu_address & 0xbffff) | (intf ? 0x40000 : 0);
    }
    void setBusOperation(int s)
    {
        switch (s) {
            case 1: _bus_ior = true; break;
            case 2: _bus_iow = true; break;
            case 4:
            case 5: _bus_memr = true; break;
            case 6: _bus_memw = true; break;
        }
    }
    void setData(uint8_t data)
    {
        _cpu_address = (_cpu_address & 0xfff00) | data;
        _bus_data = data;
        _cpuDataFloating = false;
        _isaDataFloating = false;
    }
    void setAddress(uint32_t address)
    {
        _cpu_address = address;
        _bus_address = address;
        _cpuDataFloating = false;
    }
    void setBusFloating()
    {
        _cpuDataFloating = true;
        _isaDataFloating = true;
    }
    void setPITBits(int bits) { _bus_pit = bits; }
    void setAEN(bool aen) { _bus_aen = aen; }
    void setDMA(uint8_t dma) { _bus_dma = dma; }
    void setReady(bool ready) { _cpu_ready = ready; }
    void setLock(bool lock) { _cpu_lock = lock; }
    void setDMAS(uint8_t dmas) { _dmas = dmas; }
    void setIRQs(uint8_t irq) { _bus_irq = irq; }
    void setINT(bool intrq) { _int = intrq; }
    void setCGA(uint8_t cga) { _cga = cga; }
private:
    Disassembler _disassembler;

    // Internal variables that we use to keep track of what's going on in order
    // to be able to print useful logs.
    int _t{0};  // 0 = Tidle, 1 = T1, 2 = T2, 3 = T3, 4 = T4, 5 = Tw
    int _tNext{0};
    int _d{-1};  // -1 = SI, 0 = S0, 1 = S1, 2 = S2, 3 = S3, 4 = S4, 5 = SW
    uint8_t _queue[4];
    int _queueLength{0};
    int _lastS{0};

    // These represent the CPU and ISA bus pins used to create the sniffer
    // logs.

    // A19/S6        O ADDRESS/STATUS: During T1, these are the four most significant address lines for memory operations. During I/O operations, these lines are LOW. During memory and I/O operations, status information is available on these lines during T2, T3, Tw, and T4. S6 is always low.
    // A18/S5        O The status of the interrupt enable flag bit (S5) is updated at the beginning of each clock cycle.
    // A17/S4        O  S4*2+S3 0 = Alternate Data, 1 = Stack, 2 = Code or None, 3 = Data
    // A16/S3        O
    // A15..A8       O ADDRESS BUS: These lines provide address bits 8 through 15 for the entire bus cycle (T1±T4). These lines do not have to be latched by ALE to remain valid. A15±A8 are active HIGH and float to 3-state OFF during interrupt acknowledge and local bus ``hold acknowledge''.
    // AD7..AD0     IO ADDRESS DATA BUS: These lines constitute the time multiplexed memory/IO address (T1) and data (T2, T3, Tw, T4) bus. These lines are active HIGH and float to 3-state OFF during interrupt acknowledge and local bus ``hold acknowledge''.
    uint32_t _cpu_address = 0;
    // QS0           O QUEUE STATUS: provide status to allow external tracking of the internal 8088 instruction queue. The queue status is valid during the CLK cycle after which the queue operation is performed.
    // QS1           0 = No operation, 1 = First Byte of Opcode from Queue, 2 = Empty the Queue, 3 = Subsequent Byte from Queue
    uint8_t _cpu_qs = 0;
    uint8_t _cpu_next_qs = 0;
    // -S0           O STATUS: is active during clock high of T4, T1, and T2, and is returned to the passive state (1,1,1) during T3 or during Tw when READY is HIGH. This status is used by the 8288 bus controller to generate all memory and I/O access control signals. Any change by S2, S1, or S0 during T4 is used to indicate the beginning of a bus cycle, and the return to the passive state in T3 and Tw is used to indicate the end of a bus cycle. These signals float to 3-state OFF during ``hold acknowledge''. During the first clock cycle after RESET becomes active, these signals are active HIGH. After this first clock, they float to 3-state OFF.
    // -S1           0 = Interrupt Acknowledge, 1 = Read I/O Port, 2 = Write I/O Port, 3 = Halt, 4 = Code Access, 5 = Read Memory, 6 = Write Memory, 7 = Passive
    // -S2
    uint8_t _cpu_status = 7;
    uint8_t _cpu_last_status = 7; // The last CPU status. We can derive ALE from the change from 7 (PASV) to any other status.

    bool _cpu_rqgt0{false};    // -RQ/-GT0 !87 IO REQUEST/GRANT: pins are used by other local bus masters to force the processor to release the local bus at the end of the processor's current bus cycle. Each pin is bidirectional with RQ/GT0 having higher priority than RQ/GT1. RQ/GT has an internal pull-up resistor, so may be left unconnected.
    bool _cpu_ready{false};    // READY        I  READY: is the acknowledgement from the addressed memory or I/O device that it will complete the data transfer. The RDY signal from memory or I/O is synchronized by the 8284 clock generator to form READY. This signal is active HIGH. The 8088 READY input is not synchronized. Correct operation is not guaranteed if the set up and hold times are not met.
    bool _cpu_test{false};     // -TEST        I  TEST: input is examined by the ``wait for test'' instruction. If the TEST input is LOW, execution continues, otherwise the processor waits in an ``idle'' state. This input is synchronized internally during each clock cycle on the leading edge of CLK.
    bool _cpu_lock{false};     // -LOCK    !87  O LOCK: indicates that other system bus masters are not to gain control of the system bus while LOCK is active (LOW). The LOCK signal is activated by the ``LOCK'' prefix instruction and remains active until the completion of the next instruction. This signal is active LOW, and floats to 3-state off in ``hold acknowledge''.
    // +A19..+A0      O Address bits: These lines are used to address memory and I/O devices within the system. These lines are generated by either the processor or DMA controller.
    uint32_t _bus_address{0};
    // +D7..+D0      IO Data bits: These lines provide data bus bits 0 to 7 for the processor, memory, and I/O devices.
    uint8_t _bus_data{0};
    // +DRQ0 JP6/1 == U28.19 == U73.9
    uint8_t _bus_dma{0};

    // +DRQ1..+DRQ3  I  DMA Request: These lines are asynchronous channel requests used by peripheral devices to gain DMA service. They are prioritized with DRQ3 being the lowest and DRQl being the highest. A request is generated by bringing a DRQ line to an active level (high). A DRQ line must be held high until the corresponding DACK line goes active.
    // -DACK0..-DACK3 O -DMA Acknowledge: These lines are used to acknowledge DMA requests (DRQ1-DRQ3) and to refresh system dynamic memory (DACK0). They are active low.
    uint8_t _dmas{0};        // JP9/4 HRQ DMA (bit 0), JP4/1 HOLDA (bit 1)
    // +IRQ0..+IRQ7  I  Interrupt Request lines: These lines are used to signal the processor that an I/O device requires attention. An Interrupt Request is generated by raising an IRQ line (low to high) and holding it high until it is acknowledged by the processor (interrupt service routine).
    uint8_t _bus_irq{0};
    bool _int{false};  // JP9/1 INT
    uint8_t _cga{0};         // JP7/2  CGA HCLK (bit 0), JP7/1  CGA LCLK (bit 1)
    uint8_t _bus_pit{0};     // clock, gate, output
    bool _bus_ale{false}; // ALE       Address Latch Enable
    bool _bus_ior{true};  // -IOR         O -I/O Read Command: This command line instructs an I/O device to drive its data onto the data bus. It may be driven by the processor or the DMA controller. This signal is active low.
    bool _bus_iow{true};      // -IOW         O -I/O Write Command: This command line instructs an I/O device to read the data on the data bus. It may be driven by the processor or the DMA controller. This signal is active low.
    bool _bus_memr{true};     // -MEMR        O Memory Read Command: This command line instructs the memory to drive its data onto the data bus. It may be driven by the processor or the DMA controller. This signal is active low.
    bool _bus_memw{true};     // -MEMW        O Memory Write Command: This command line instructs the memory to store the data present on the data bus. It may be driven by the processor or the DMA controller. This signal is active low.
    bool _bus_iochrdy{false};  // +I/O CH RDY  I I/O Channel Ready: This line, normally high (ready), is pulled low (not ready) by a memory or I/O device to lengthen I/O or memory cycles. It allows slower devices to attach to the I/O channel with a minimum of difficulty. Any slow device using this line should drive it low immediately upon detecting a valid address and a read or write command. This line should never be held low longer than 10 clock cycles. Machine cycles (I/O or memory) are extended by an integral number of CLK cycles (210 ns).
    bool _bus_aen{false};      // +AEN         O Address Enable: This line is used to de-gate the processor and other devices from the I/O channel to allow DMA transfers to take place. When this line is active (high), the DMA controller has control of the address bus, data bus, read command lines (memory and I/O), and the write command lines (memory and I/O).
    bool _bus_tc{false};       // +T/C         O Terminal Count: This line provides a pulse when the terminal count for any DMA channel is reached. This signal is active high.
    bool _cpuDataFloating{false};
    bool _isaDataFloating{false};
};

#endif