#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <deque>
#include <unordered_map>
#include <algorithm>
#include <cassert>
#include <format>
#include <iostream>

#include "Dmac.h"

// -------------------------------- I/O ports ---------------------------------
static constexpr uint16_t PORT_DOR = 2; // Digital Output Register (write)
static constexpr uint16_t PORT_MSR = 4; // Main Status Register (read)
static constexpr uint16_t PORT_DATA = 5; // Data Register (r/w)

// ------------------------------ MSR bitfields -------------------------------
static constexpr uint8_t MSR_DRIVE0_BUSY = 0x01;
static constexpr uint8_t MSR_DRIVE1_BUSY = 0x02;
static constexpr uint8_t MSR_DRIVE2_BUSY = 0x04;
static constexpr uint8_t MSR_DRIVE3_BUSY = 0x08;
static constexpr uint8_t MSR_FDC_BUSY = 0x10;
static constexpr uint8_t MSR_NON_DMA = 0x20; // 1=non-DMA; we keep 0 (DMA only)
static constexpr uint8_t MSR_DIO = 0x40; // 1=to CPU (result phase)
static constexpr uint8_t MSR_RQM = 0x80; // 1=data reg ready

// ------------------------------- DOR bits -----------------------------------
static constexpr uint8_t DOR_DRIVE_SEL_MASK = 0x03; // 0..3
static constexpr uint8_t DOR_DMA_ENABLE = 0x08; // typical PC meaning
static constexpr uint8_t DOR_RESET_NOT = 0x04; // bit 4 (1=out of reset)
static constexpr uint8_t DOR_MOTOR0 = 0x10; // per-drive motor enables
static constexpr uint8_t DOR_MOTOR1 = 0x20;
static constexpr uint8_t DOR_MOTOR2 = 0x40;
static constexpr uint8_t DOR_MOTOR3 = 0x80;

// ---------------------------- Result registers ------------------------------
static constexpr uint8_t ST0_IC_SHIFT = 6; // interrupt code
static constexpr uint8_t ST0_HEAD_ADDRESS = 0x04;

static constexpr uint8_t ST1_NO_DATA = 0x01;
static constexpr uint8_t ST1_NOT_WRITABLE = 0x02;
static constexpr uint8_t ST1_DATA_ERROR = 0x20;
static constexpr uint8_t ST2_BAD_CYL = 0x02;
static constexpr uint8_t ST3_TRACK0 = 0x10;
static constexpr uint8_t ST3_HEAD = 0x04;

// ------------------------------ Opcodes (base) ------------------------------
static constexpr uint8_t OPC_SPECIFY = 0x03;
static constexpr uint8_t OPC_SENSE_INT = 0x08;
static constexpr uint8_t OPC_SEEK = 0x0F;
static constexpr uint8_t OPC_CALIBRATE = 0x07;
static constexpr uint8_t OPC_CHECK_STATUS = 0x04;
static constexpr uint8_t OPC_READ_DATA = 0x06;
static constexpr uint8_t OPC_WRITE_DATA = 0x05;
static constexpr uint8_t OPC_FORMAT_TRACK = 0x0D;
static constexpr uint8_t OPC_READ_ID = 0x0A;

static inline uint32_t sectorSizeFromN(const uint8_t n) { return (n > 6) ? 512u : (128u << n); }

struct DiskFormat
{
    uint8_t cylinders{};
    uint8_t heads{};
    uint8_t sectors{};
};

// Common PC image sizes
static const std::unordered_map<size_t, DiskFormat>& kFormatTable() {
    static const std::unordered_map<size_t, DiskFormat> k = {
        {163'840, {40, 1, 8}},
        {184'320, {40, 1, 9}},
        {327'680, {40, 2, 8}},
        {368'640, {40, 2, 9}},
        {737'280, {80, 2, 9}},
        {1'228'800, {80, 2, 15}},
        {1'474'560, {80, 2, 18}},
    };
    return k;
}

enum class InterruptCode : uint8_t
{
    Normal = 0,
    Abnormal = 1,
    Invalid = 2,
    Polling = 3
};

enum class Command : uint8_t
{
    None,
    ReadData,
    WriteData,
    FormatTrack,
    CheckDriveStatus,
    Specify,
    Calibrate,
    SenseInt,
    Seek,
    ReadId,
};

enum class OpKind : uint8_t
{
    None,
    Reset,
    ReadDMA,
    WriteDMA,
    FormatDMA,
    Seek
};

typedef uint8_t DriveIndex;

struct Drive
{
    bool error_signal = false;
    uint8_t cylinder = 0;
    uint8_t head = 0;
    uint8_t sector = 1;
    uint8_t max_cylinders = 80;
    uint8_t max_heads = 2;
    uint8_t max_sectors = 9;
    bool ready = false;
    bool motor_on = false;
    bool positioning = false;
    bool have_disk = false;
    bool write_protected = false;
    std::vector<uint8_t> image;
};

class FDC
{
    static constexpr uint64_t RESET_TICKS = 22; // ~25ms

public:
    // ------------------------------- Lifecycle ------------------------------
    FDC() {
        resetPowerOn();
    }

    void attachDMAC(DMAC* d) {
        dmac_ = d;
    }

    void attachPIC(PIC* p) {
        pic_ = p;
    }

    // Insert/eject a disk image (raw CHS-linear).
    bool loadDisk(DriveIndex drv, const std::vector<uint8_t>& bytes, bool writeProtected = false) {
        if (drv >= 4) {
            return false;
        }
        auto& d = drives_[drv];
        d.image = bytes;
        d.have_disk = !bytes.empty();
        d.write_protected = writeProtected;
        inferGeometry(d);
        d.ready = d.have_disk;

        std::cout << std::format("FDC: Loaded disk into drive {} ({} bytes, {}C/{}H/{}S, {}-protected)\n",
                                 drv, bytes.size(),
                                 d.max_cylinders, d.max_heads, d.max_sectors,
                                 writeProtected ? "write" : "read-write");
        return true;
    }

    // ------------------------------- Bus glue -------------------------------
    uint8_t readIO(const uint16_t port) {
        //std::cout << "FDC: Read from port " << std::hex << port << std::dec << "\n";
        switch (port) {
            case PORT_DOR:
                // DOR is write-only
                return 0xFF;
            case PORT_MSR:
            {
                auto b = readStatusRegister();
                //std::cout << std::format("FDC: Read MSR: {:02X}", b) << std::endl;
                return b;
            }
            case PORT_DATA:
            {
                auto b = readDataRegister();
                //std::cout << std::format("FDC: Read DATA: {:02X}", b) << std::endl;
                return b;
            }
            default:
                break;
        }
        return 0xFF;
    }

    void writeIO(const uint16_t port, const uint8_t val) {
        //std::cout << std::format("FDC: Write to port: {:0X} value: {:02X}", port, val) << std::endl;
        switch (port) {
            case PORT_DOR:
                std::cout << std::format("FDC: Write DOR: {:02X}", val) << std::endl;
                return writeDOR(val);
            case PORT_DATA:
                std::cout << std::format("FDC: Write DATA: {:02X}", val) << std::endl;
                return writeDATA(val);
            default:
                break;
        }
    }

    // ------------------------------- DMA side -------------------------------
    // For DMA channel 2 cycles:
    //  * In Read Data op (device->mem), DMAC must call this to get next byte.
    uint8_t dmaDeviceRead() {
        if (!op_.dma_mode && bytes_left_ == 0) {
            return 0xFF;
        }
        if (op_.kind != OpKind::ReadDMA) {
            return 0xFF;
        }
        const auto& d = drives_[sel_];
        const uint32_t bps = sectorSizeFromN(op_.N);
        const size_t off = chsToOffset(d, op_.C, op_.H, op_.S, bps);
        if (off == SIZE_MAX) {
            return 0xFF;
        }

        const size_t addr = off + dma_byte_index_;
        //std::cout << "offset: " << addr;
        const uint8_t v = (addr < d.image.size()) ? d.image[addr] : 0xFF;
        advanceByte();
        return v;
    }

    //  * In Write Data op (mem->device), DMAC must call this to supply next byte.
    void dmaDeviceWrite(uint8_t v) {
        if (op_.kind != OpKind::WriteDMA || bytes_left_ == 0)
            return;
        auto& d = drives_[sel_];
        uint32_t bps = sectorSizeFromN(op_.N);
        size_t off = chsToOffset(d, op_.C, op_.H, op_.S, bps);
        if (off != SIZE_MAX) {
            size_t addr = off + dma_byte_index_;
            if (addr < d.image.size()) {
                d.image[addr] = v;
            }
        }
        advanceByte();
    }

    //  * Signal End Of Process (Terminal Count) for channel 2.
    void dmaDeviceEOP() {

        if (bytes_left_ > 0) {
            std::cerr << std::format("FDC: DMA EOP signaled but {} bytes still left in operation!\n", bytes_left_);
        }

        // Complete immediately if we're mid-op
        if (op_.kind == OpKind::ReadDMA || op_.kind == OpKind::WriteDMA || op_.kind == OpKind::FormatDMA) {
            finalizeDataOp(true);
        }
    }

    // Has the controller asserted DRQ for DMA ch2?
    [[nodiscard]] bool isDrqAsserted() const {
        return drq_;
    }

    // ------------------------------ Interrupts ------------------------------
    [[nodiscard]] bool pollIrq() const {
        return irq_pending_;
    }

    void ackIrq() {
        setIRQ(false);
    }

    // ------------------------------- Ticking --------------------------------
    void tick() {

        if (op_.kind != OpKind::None) {
            // Tick ongoing operation.
            op_.ticks++;
        }

        // We can simulate certain operations taking time here. For now, we complete immediately.
        switch (op_.kind) {
            case OpKind::Reset:
                if (op_.ticks >= RESET_TICKS) {
                    completeReset();
                }
                break;
            case OpKind::Seek:
                completeSeek();
            default:
                break;
        }
    }

    // ------------------------------ Public reset ----------------------------
    void resetPowerOn() {
        resetInternal(true);
    }

    void reset() {
        resetInternal(false);
    }

private:
    struct Op
    {
        OpKind kind = OpKind::None;
        bool dma_mode = true;
        uint64_t ticks = 0;
        uint8_t C = 0, H = 0, S = 1, N = 2, EOT = 0;
    };

    std::array<Drive, 4> drives_{};
    DMAC* dmac_{};
    PIC* pic_{};

    // Controller regs/state
    uint8_t dor_ = 0;
    bool busy_ = false;
    bool mrq_ = false;
    bool dio_result_ = false; // result phase
    uint8_t sel_ = 0; // selected drive

    // Command parsing
    Command cur_cmd_ = Command::None;
    uint32_t expected_bytes_{0};
    std::deque<uint8_t> cmd_in_{};

    // Result FIFO (read via DATA)
    std::deque<uint8_t> fifo_out_{};

    // Ongoing op
    Op op_{};
    size_t bytes_left_{0};
    size_t bytes_transferred_{0};
    size_t dma_byte_index_{0};
    uint32_t dma_start_address_{0};
    uint16_t dma_word_count_{0};
    bool drq_{false};

    // Sense / IRQ
    uint8_t st0_{0};
    uint8_t st1_{0};
    uint8_t st2_{0};
    uint8_t pcn_{0};
    bool irq_pending_{false};
    bool resetting_{false};

    // ------------------------------ Utilities -------------------------------
    void resetInternal(bool powerOn) {
        dor_ = 0;
        busy_ = false;
        mrq_ = false;
        dio_result_ = false;
        sel_ = 0;
        cur_cmd_ = Command::None;
        expected_bytes_ = 0;
        cmd_in_.clear();
        fifo_out_.clear();
        op_ = Op{};
        bytes_left_ = 0;
        bytes_transferred_ = 0;
        dma_byte_index_ = 0;
        drq_ = false;
        irq_pending_ = false;
        if (pic_) {
            pic_->setIRQLine(6, false);
        }
        st0_ = st1_ = st2_ = pcn_ = 0;

        if (powerOn) {
            for (auto& d : drives_) {
                d = Drive{};
            }
        }
        // After reset, BIOS will issue Sense Interrupts
        setSenseResult(InterruptCode::Polling, sel_, 0);
    }

    static void inferGeometry(Drive& d) {
        if (const auto it = kFormatTable().find(d.image.size()); it != kFormatTable().end()) {
            d.max_cylinders = it->second.cylinders;
            d.max_heads = it->second.heads;
            d.max_sectors = it->second.sectors;
        }
        else if (!d.image.empty() && (d.image.size() % 512 == 0)) {
            if (const size_t sectors = d.image.size() / 512; sectors == 2880) {
                d.max_cylinders = 80;
                d.max_heads = 2;
                d.max_sectors = 18;
            }
            else if (sectors == 2400) {
                d.max_cylinders = 80;
                d.max_heads = 2;
                d.max_sectors = 15;
            }
            else if (sectors == 1440) {
                d.max_cylinders = 80;
                d.max_heads = 2;
                d.max_sectors = 9;
            }
            else if (sectors == 720) {
                d.max_cylinders = 40;
                d.max_heads = 2;
                d.max_sectors = 9;
            }
        }
        if (d.sector == 0) {
            d.sector = 1;
        }
    }

    static size_t chsToOffset(const Drive& d, const uint8_t C, const uint8_t H, const uint8_t S, const uint32_t bps) {
        if (C >= d.max_cylinders || H >= d.max_heads || S < 1 || S > d.max_sectors) {
            return SIZE_MAX;
        }
        const size_t track = static_cast<size_t>(C) * d.max_heads + H;
        const size_t lba = track * d.max_sectors + (S - 1);
        const size_t off = lba * static_cast<size_t>(bps);

        if (off + bps > d.image.size()) {
            return SIZE_MAX;
        }
        return off;
    }

    // --------------------------- Port implementations -----------------------
    [[nodiscard]] uint8_t readStatusRegister() const {
        uint8_t msr = 0;

        if (busy_ || op_.kind != OpKind::None) {
            msr |= MSR_FDC_BUSY; // no per-drive busy map
        }
        // NON-DMA=0 because we only support DMA
        if (mrq_) {
            msr |= MSR_RQM;
        }
        if (dio_result_) {
            msr |= MSR_DIO;
        }
        return msr;
    }

    uint8_t readDataRegister() {
        if (!mrq_ || !dio_result_ || fifo_out_.empty()) {
            return 0xFF;
        }
        const uint8_t v = fifo_out_.front();
        fifo_out_.pop_front();
        if (fifo_out_.empty()) {
            //mrq_ = false;
            dio_result_ = false;
            busy_ = false;
            cur_cmd_ = Command::None;
        }
        std::cout << std::format("FDC: Data register read -> {:02X}, {} bytes left", v, fifo_out_.size()) << std::endl;
        return v;
    }

    void writeDOR(uint8_t v) {
        dor_ = v;
        if ((v & DOR_RESET_NOT) == 0) {
            // Reset when bit 2 is 0
            std::cout << "FDC: Reset triggered via DOR. Beginning reset operation." << std::endl;

            op_ = Op{OpKind::Reset, 0};
            resetting_ = true;

            setIRQ(false);

            // Ignore all other bits
            return;
        }

        sel_ = (v & DOR_DRIVE_SEL_MASK);
        // Motor bits -> ready flag
        for (int i = 0; i < 4; ++i) {
            const bool on = (v & (DOR_MOTOR0 << i)) != 0;
            drives_[i].motor_on = on;
            if (on && drives_[i].have_disk) {
                drives_[i].ready = true;
            }
        }
    }

    void writeDATA(const uint8_t v) {
        // Command vs parameter bytes
        cmd_in_.push_back(v);
        if (cmd_in_.size() == 1) {
            decodeOpcode(v);
        }
        if (expected_bytes_ && cmd_in_.size() == expected_bytes_) {
            dispatchCommand();
        }
    }

    void decodeOpcode(const uint8_t op) {
        switch (op & 0x1F) {
            case OPC_SPECIFY:
                std::cout << "FDC: Command SPECIFY\n";
                cur_cmd_ = Command::Specify;
                expected_bytes_ = 3;
                break;
            case OPC_SENSE_INT:
                std::cout << "FDC: Command SENSE INTERRUPT\n";
                cur_cmd_ = Command::SenseInt;
                expected_bytes_ = 1;
                break;
            case OPC_CHECK_STATUS:
                std::cout << "FDC: Command CHECK DRIVE STATUS\n";
                cur_cmd_ = Command::CheckDriveStatus;
                expected_bytes_ = 2;
                break;
            case OPC_CALIBRATE:
                std::cout << "FDC: Command CALIBRATE\n";
                cur_cmd_ = Command::Calibrate;
                expected_bytes_ = 2;
                break;
            case OPC_SEEK:
                std::cout << "FDC: Command SEEK\n";
                cur_cmd_ = Command::Seek;
                expected_bytes_ = 3;
                break;
            case OPC_READ_DATA:
                std::cout << "FDC: Command READ DATA\n";
                cur_cmd_ = Command::ReadData;
                expected_bytes_ = 9;
                break;
            case OPC_WRITE_DATA:
                std::cout << "FDC: Command WRITE DATA\n";
                cur_cmd_ = Command::WriteData;
                expected_bytes_ = 9;
                break;
            case OPC_FORMAT_TRACK:
                cur_cmd_ = Command::FormatTrack;
                expected_bytes_ = 6;
                break;
            case OPC_READ_ID:
                cur_cmd_ = Command::ReadId;
                expected_bytes_ = 2;
                break;
            default:
                cur_cmd_ = Command::None;
                expected_bytes_ = 1;
                break;
        }
    }

    void setSenseResult(InterruptCode ic, uint8_t drv, uint8_t pcnVal) {
        st0_ = (static_cast<uint8_t>(ic) << ST0_IC_SHIFT)
            | ((drives_[drv].head & 1) ? ST0_HEAD_ADDRESS : 0)
            | (drv & 3);

        pcn_ = pcnVal;
    }

    void pushResult(const std::vector<uint8_t>& v) {
        for (auto b : v) {
            fifo_out_.push_back(b);
        }
        dio_result_ = true;
        mrq_ = true;
        busy_ = true;
    }

    void dispatchCommand() {
        busy_ = true;
        mrq_ = false;
        dio_result_ = false;
        // I'm not sure if this is proper logic, but if there was an unacknowledged IRQ, not much we can do about it now.
        setIRQ(false);
        switch (cur_cmd_) {
            case Command::Specify:
                handleSpecify();
                break;
            case Command::SenseInt:
                handleSenseInt();
                break;
            case Command::CheckDriveStatus:
                handleCheckDriveStatus();
                break;
            case Command::Calibrate:
                handleCalibrate();
                break;
            case Command::Seek:
                handleSeek();
                break;
            case Command::ReadData:
                handleReadData();
                break;
            case Command::WriteData:
                handleWriteData();
                break;
            case Command::FormatTrack:
                handleFormatTrack();
                break;
            case Command::ReadId:
                handleReadId();
                break;
            default:
                setSenseResult(InterruptCode::Invalid, sel_, drives_[sel_].cylinder);
                pushResult({st0_, pcn_});
                break;
        }
        cmd_in_.clear();
        expected_bytes_ = 0;
    }

    // ------------------------------ Handlers --------------------------------
    void handleSpecify() {
        busy_ = false;
        mrq_ = true;
    }

    void setIRQ(const bool state) {
        irq_pending_ = state;
        if (pic_) {
            pic_->setIRQLine(6, state);
        }
    }

    void handleSenseInt() {
        std::cout << "FDC: Handling Sense Interrupt. Returning ST0=" << std::hex
            << static_cast<int>(st0_) << " PCN=" << static_cast<int>(pcn_) << std::dec << "\n";

        setSenseResult(InterruptCode::Polling, sel_, drives_[sel_].cylinder);
        setIRQ(false);
        pushResult({st0_, pcn_});
    }

    void handleCheckDriveStatus() {
        const uint8_t dh = cmd_in_[1];
        const uint8_t drv = (dh & 3);
        sel_ = drv;
        uint8_t st3 = (drv & 3) | ((dh & 4) ? ST3_HEAD : 0);
        if (drives_[drv].cylinder == 0) {
            st3 |= ST3_TRACK0;
        }
        pushResult({st3});
    }

    void handleCalibrate() {
        const uint8_t drv = (cmd_in_[1] & 3);
        sel_ = drv;
        drives_[drv].cylinder = 0;
        setSenseResult(InterruptCode::Normal, drv, 0);
        std::cout << "FDC: Calibrate drive " << static_cast<int>(drv) << " to cylinder 0, raising IRQ\n";
        setIRQ(true);
        busy_ = false;
        mrq_ = true;
    }

    void handleSeek() {
        const uint8_t dh = cmd_in_[1];
        const uint8_t C = cmd_in_[2];
        const uint8_t drv = (dh & 3);
        const uint8_t H = (dh >> 2) & 1;
        sel_ = drv;
        drives_[drv].head = H;
        op_ = {OpKind::Seek, true, 0, C, H, 1, 2, 0};
        busy_ = true;
    }

    void completeSeek() {
        auto& d = drives_[sel_];
        d.cylinder = op_.C;
        setSenseResult(InterruptCode::Normal, sel_, d.cylinder);
        std::cout << "FDC: Seek complete on drive " << static_cast<int>(sel_)
            << " to cylinder " << static_cast<int>(d.cylinder) << ", raising IRQ\n";
        setIRQ(true);
        op_ = Op{};
        busy_ = false;
        mrq_ = true;
    }

    void completeReset() {
        reset();
        std::cout << "FDC: Reset complete. Raising IRQ" << std::endl;
        setIRQ(true);
        op_ = Op{};
        busy_ = false;
        mrq_ = true;
    }

    void handleReadData() {
        const uint8_t DH = cmd_in_[1];
        const uint8_t C = cmd_in_[2];
        const uint8_t H = cmd_in_[3];
        const uint8_t S = cmd_in_[4];
        const uint8_t N = cmd_in_[5];
        const uint8_t EOT = cmd_in_[6];
        const uint8_t drv = DH & 3;
        const uint8_t headReq = (DH >> 2) & 1;

        std::cout << std::format(
            "FDC: Read Data cmd for drive {}, C={}, H={}, S={}, N={}, EOT={}\n",
            static_cast<int>(drv),
            static_cast<int>(C),
            static_cast<int>(H),
            static_cast<int>(S),
            static_cast<int>(N),
            static_cast<int>(EOT));

        sel_ = drv;
        const auto& d = drives_[drv];
        if (!d.have_disk || !d.ready || !d.motor_on) {
            endError(C, H, S, N, false);
            return;
        }
        if (H != headReq) {
            /* tolerate */
        }
        const uint32_t bps = sectorSizeFromN(N);
        if (chsToOffset(d, C, H, S, bps) == SIZE_MAX) {
            endError(C, H, S, N, false);
            return;
        }
        startDma(OpKind::ReadDMA, C, H, S, N, EOT, bps);
    }

    void handleWriteData() {
        const uint8_t DH = cmd_in_[1];
        const uint8_t C = cmd_in_[2];
        const uint8_t H = cmd_in_[3];
        const uint8_t S = cmd_in_[4];
        const uint8_t N = cmd_in_[5];
        const uint8_t EOT = cmd_in_[6];
        const uint8_t drv = DH & 3;
        const uint8_t headReq = (DH >> 2) & 1;
        sel_ = drv;

        std::cout << std::format(
            "FDC: Write Data cmd for drive {}, C={}, H={}, S={}, N={}, EOT={}\n",
            static_cast<int>(drv),
            static_cast<int>(C),
            static_cast<int>(H),
            static_cast<int>(S),
            static_cast<int>(N),
            static_cast<int>(EOT));

        const auto& d = drives_[drv];
        if (!d.have_disk || !d.ready || !d.motor_on) {
            endError(C, H, S, N, true);
            return;
        }
        if (d.write_protected) {
            std::cout << "FDC: Write Data error: disk is write-protected\n";
            endError(C, H, S, N, true, true);
            return;
        }
        if (H != headReq) {
            /* tolerate */
        }
        const uint32_t bps = sectorSizeFromN(N);
        if (chsToOffset(d, C, H, S, bps) == SIZE_MAX) {
            endError(C, H, S, N, true);
            return;
        }
        startDma(OpKind::WriteDMA, C, H, S, N, EOT, bps);
    }

    void handleFormatTrack() {
        const uint8_t DH = cmd_in_[1];
        const uint8_t N = cmd_in_[2];
        const uint8_t SC = cmd_in_[3];
        (void)SC;
        const uint8_t drv = (DH & 3);
        const uint8_t H = (DH >> 2) & 1;
        sel_ = drv;
        const auto& d = drives_[drv];
        if (!d.have_disk || !d.ready || !d.motor_on || d.write_protected) {
            endError(d.cylinder, H, 1, N, true);
            return;
        }
        // treat format as DMA write of one track worth of data
        const uint32_t bps = sectorSizeFromN(N);
        startDma(OpKind::FormatDMA, d.cylinder, H, 1, N, d.max_sectors, bps);
    }

    void handleReadId() {
        const uint8_t DH = cmd_in_[1];
        const uint8_t drv = (DH & 3);
        uint8_t H = (DH >> 2) & 1;
        sel_ = drv;
        const auto& d = drives_[drv];
        if (!d.have_disk || !d.ready || !d.motor_on) {
            st0_ = (static_cast<uint8_t>(InterruptCode::Abnormal) << ST0_IC_SHIFT) | (drv & 3);
            pushResult({st0_, 0, 0, 0, 0, 0, 2});
            return;
        }
        uint8_t C = d.cylinder;
        uint8_t S = d.sector;
        uint8_t N = 2;
        st0_ = (static_cast<uint8_t>(InterruptCode::Normal) << ST0_IC_SHIFT)
            | (drv & 3)
            | ((H & 1) ? ST0_HEAD_ADDRESS : 0);
        pushResult({st0_, 0, 0, C, H, S, N});
    }

    void endError(uint8_t C, uint8_t H, uint8_t S, uint8_t N, const bool write, const bool write_protect = false) {
        st0_ = (static_cast<uint8_t>(InterruptCode::Abnormal) << ST0_IC_SHIFT)
            | (sel_ & 3)
            | ((H & 1) ? ST0_HEAD_ADDRESS : 0);

        if (write_protect) {
            st1_ = ST1_NOT_WRITABLE | ST1_NO_DATA;
        }
        else {
            st1_ = write ? ST1_DATA_ERROR : ST1_NO_DATA;
        }
        st2_ = write ? 0 : ST2_BAD_CYL;
        pushResult({st0_, st1_, st2_, C, H, S, N});

        setIRQ(true);
        mrq_ = true;
    }

    // ------------------------------ DMA helpers ------------------------------
    void startDma(const OpKind kind, const uint8_t C, const uint8_t H, const uint8_t S, const uint8_t N,
                  const uint8_t EOT, const uint32_t bps) {
        op_ = {kind, true, 0, C, H, S, N, EOT};
        dma_byte_index_ = 0;

        dma_start_address_ = dmac_->getAddress(2);
        dma_word_count_ = dmac_->getWordCount(2) + 1; // +1 because count is words-1

        std::cout << std::format("FDC: Starting DMA operation: address {:08X}, word count: {}\n",
                                 dma_start_address_,
                                 dma_word_count_);

        bytes_left_ = static_cast<size_t>(bps) * static_cast<size_t>((EOT >= S) ? (EOT - S + 1) : 1);
        bytes_transferred_ = 0;
        busy_ = true;
        setDrq(true);
    }

    void advanceByte() {
        ++dma_byte_index_;
        if (bytes_left_ > 0) {
            --bytes_left_;
            ++bytes_transferred_;
        }

        if (bytes_left_ == 0) {
            // In DMA mode, only TC signals completion, we ignore EOT
            //finalizeDataOp();
        }
        else {
            setDrq(true);
        }
    }

    void finalizeDataOp(bool eop) {
        setDrq(false);
        auto& d = drives_[sel_];
        // Advance CHS to next sector
        const uint8_t C = op_.C;
        const uint8_t H = op_.H;
        uint8_t S = op_.S;
        const uint8_t N = op_.N;
        const uint8_t lastR = (op_.EOT >= S) ? op_.EOT : S;

        // Advance one sector; if over EOT, we already finished
        if (S < lastR) {
            ++S;
        }
        else {
            // keep R as last sector
        }
        // Queue result bytes: ST0,ST1,ST2,C,H,LastR,N (success)
        st0_ = (static_cast<uint8_t>(InterruptCode::Normal) << ST0_IC_SHIFT)
            | (sel_ & 3)
            | ((H & 1) ? ST0_HEAD_ADDRESS : 0);
        st1_ = 0;
        st2_ = 0;
        fifo_out_.push_back(st0_);
        fifo_out_.push_back(st1_);
        fifo_out_.push_back(st2_);
        fifo_out_.push_back(C);
        fifo_out_.push_back(H);
        fifo_out_.push_back(lastR);
        fifo_out_.push_back(N);
        dio_result_ = true;
        mrq_ = true;
        busy_ = false;
        op_ = Op{};
        d.sector = std::min<uint8_t>(lastR, d.max_sectors);
        // Signal completion via IRQ6
        std::cout << std::format("FDC: DMA operation complete. EOP: {} Transferred {} bytes. Raising IRQ\n",
                                 eop,
                                 bytes_transferred_);

        if (irq_pending_) {
            std::cerr << "FDC: ERROR: IRQ already pending when raising IRQ\n";
        }
        setIRQ(true);
    }

    void setDrq(const bool state) {
        drq_ = state;
        if (dmac_) {
            //std::cout << "FDC: Setting DREQ2 to " << (state ? "ON" : "OFF") << std::endl;
            dmac_->setDMARequestLine(2, state); /* DREQ2 */
        }
    }
};

