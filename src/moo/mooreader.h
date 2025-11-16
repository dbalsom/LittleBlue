/*
    MIT License

    Copyright (c) 2025 Angela McEgo
    Copyright (c) 2025 Daniel Balsom

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/
#ifndef MOOREADER_H__
#define MOOREADER_H__

/*
    CHANGELOG
    v1.1
        - Refactored into class.
        - Added optional gzip support (define MOO_USE_ZLIB).
        - Added << operator for register enums. Don't mask initial registers.
        - Added enum iterator for REG16 and REG32.
        - MooHeader.GetVersion now returns std::pair<>
    v1.0 - Initial release


    USAGE

    1) Instantiate a MooReader:
    Moo::Reader reader;

    2) Then add a MOO test file:
    reader.AddFromFile("path/to/test.moo");

    3) Access tests:
    for (const auto& test : reader.tests) {
        // Access test.name, test.index, test.init_state, test.expected_state, etc.
    }

    4) Compare register states:
    for (auto r : Moo::REG16Range()) {
        if (test.GetInitialRegister
        std::cout << r << "\n";
    }

    Useful functions:

    HasTest();            - Takes in hash, returns true if found
    AddRevocationList();  - Adds a revocation list. You have to manually check using a test hash, whether it was
                            revoked.
    IsRevoked();          - Checks if a test is revoked.
*/

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef MOO_USE_ZLIB
#include <zlib.h>
#endif

namespace Moo
{

template <typename E>
class EnumIterator
{
public:
    // ReSharper disable once CppUseTypeTraitAlias
    typedef typename std::underlying_type<E>::type U;

    // ReSharper disable once CppNonExplicitConvertingConstructor
    EnumIterator(E v) :
        value_(static_cast<U>(v)) {
    }

    E operator*() const {
        return static_cast<E>(value_);
    }

    EnumIterator& operator++() {
        ++value_;
        return *this;
    }

    bool operator!=(const EnumIterator& other) const {
        return value_ != other.value_;
    }

private:
    U value_;
};

template <typename E>
class EnumRange
{
public:
    EnumRange(E first, E last) :
        first_(first), last_(last) {
    }

    EnumIterator<E> begin() const { return EnumIterator<E>(first_); }
    EnumIterator<E> end() const { return EnumIterator<E>(last_); }

private:
    E first_;
    E last_;
};

// The Intel 16-bit general register file.
enum struct REG16 : uint8_t
{
    AX = 0, BX = 1, CX = 2, DX = 3,
    CS = 4, SS = 5, DS = 6, ES = 7,
    SP = 8, BP = 9, SI = 10, DI = 11,
    IP = 12, FLAGS = 13,

    COUNT = 14
};

inline const char* GetRegister16Name(REG16 reg) {
    static const char* names[] =
        {"ax", "bx", "cx", "dx", "cs", "ss", "ds", "es", "sp", "bp", "si", "di", "ip", "flags"};

    return names[static_cast<size_t>(reg)];
}

inline std::ostream& operator<<(std::ostream& os, const REG16 reg) {
    return os << GetRegister16Name(reg);
}

inline EnumRange<REG16> REG16Range() {
    return EnumRange<REG16>(REG16::AX, REG16::COUNT);
}

// The Intel 32-bit general register file.
enum struct REG32 : uint8_t
{
    CR0 = 0, CR3 = 1,
    EAX = 2, EBX = 3, ECX = 4, EDX = 5,
    ESI = 6, EDI = 7, EBP = 8, ESP = 9,
    CS = 10, DS = 11, ES = 12, FS = 13, GS = 14, SS = 15,
    EIP = 16, EFLAGS = 17,
    DR6 = 18, DR7 = 19,

    COUNT = 20
};

inline const char* GetRegister32Name(REG32 reg) {
    static const char* names[] =
    {"cr0", "cr3", "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp",
     "cs", "ds", "es", "fs", "gs", "ss", "eip", "eflags", "dr6", "dr7"};

    return names[static_cast<size_t>(reg)];
}

inline std::ostream& operator<<(std::ostream& os, const REG32 reg) {
    return os << GetRegister32Name(reg);
}

inline EnumRange<REG32> REG32Range() {
    return EnumRange<REG32>(REG32::EAX, REG32::COUNT);
}

class Reader
{
public:
    enum CPUType
    {
        CPU8088,
        CPU8086,
        CPUV20,
        CPUV30,
        CPU286,
        CPU386E,

        CPU_COUNT
    };

    struct RegisterState
    {
        uint32_t bitmask{};
        std::vector<uint32_t> values;

        enum TYPE
        {
            REG_16,
            REG_32,
        } type{REG_16};

        bool is_populated{false};

        bool HasRegister(REG16 reg) const {
            return bitmask & (1U << static_cast<uint32_t>(reg));
        }

        uint16_t GetRegister(REG16 reg) const {
            if (type != REG_16)
                throw std::runtime_error("Registers aren't 16 bit.");
            return values[static_cast<uint8_t>(reg)];
        }

        bool HasRegister(REG32 reg) const {
            return bitmask & (1U << static_cast<uint32_t>(reg));
        }

        uint32_t GetRegister(REG32 reg) const {
            if (type != REG_32)
                throw std::runtime_error("Registers aren't 32 bit.");
            return values[static_cast<uint8_t>(reg)];
        }
    };

    struct RamEntry
    {
        uint32_t address{};
        uint8_t value{};
    };

    struct QueueData
    {
        std::vector<uint8_t> bytes;
    };

    struct CpuState
    {
        RegisterState regs, masks;
        std::vector<RamEntry> ram;
        QueueData queue;
        bool has_queue{false};
    };

    struct Cycle
    {
        struct BitField0
        {
            uint8_t ale : 1;
            uint8_t bhe : 1; // 80286/80386 only
            uint8_t ready : 1;
            uint8_t lock : 1;
            uint8_t  : 4; // unused bits

            BitField0() :
                ale(0), bhe(0), ready(0), lock(0) {
            }

            BitField0(const uint8_t data) {
                std::memcpy(this, &data, sizeof(uint8_t));
            }
        };

        struct BitField1
        {
            uint8_t bhe : 1; // 8086/V30 only
            uint8_t  : 7; // unused bits

            BitField1() :
                bhe(0) {
            }

            BitField1(const uint8_t data) {
                std::memcpy(this, &data, sizeof(uint8_t));
            }
        };

        BitField0 pin_bitfield0;
        uint32_t address_latch;
        uint8_t segment_status;
        uint8_t memory_status;
        uint8_t io_status;
        BitField1 pin_bitfield1;
        uint16_t data_bus;
        uint8_t bus_status;
        uint8_t t_state;
        uint8_t queue_op_status;
        uint8_t queue_byte_read;
    };

    struct Exception
    {
        uint8_t number;
        uint32_t flag_addr;
    };

    struct MooHeader
    {
        uint8_t version_major{};
        uint8_t version_minor{};
        uint8_t reserved[2] = {};
        uint32_t test_count{};
        std::string cpu_name; //8 characters
        CPUType cpu_type;

        std::pair<uint8_t, uint8_t> GetVersion() const {
            return {version_major, version_minor};
        }

        uint16_t GetVersionU16() const {
            return (static_cast<uint16_t>(version_major) << 8) | version_minor;
        }
    };

    struct Test
    {
        uint32_t index{};
        std::string name;
        std::vector<uint8_t> bytes;
        CpuState init_state;
        CpuState final_state;
        std::vector<Cycle> cycles;
        bool has_exception = false;
        Exception exception;
        bool has_hash = false;
        std::array<uint8_t, 20> hash = {};

        //ugly copypaste.
        //uses mask if requested and if the tests have RMSK/RM32 chunks
        uint16_t GetInitialRegister(const REG16 reg) const {
            return init_state.regs.GetRegister(reg);
        }

        uint16_t GetFinalRegister(const REG16 reg, const bool masked) const {
            if (final_state.regs.HasRegister(reg)) {
                uint16_t ret = final_state.regs.GetRegister(reg);
                if (masked && final_state.masks.HasRegister(reg)) {
                    ret &= final_state.masks.GetRegister(reg);
                }
                return ret;
            }
            return GetInitialRegister(reg);
        }

        uint32_t GetInitialRegister(const REG32 reg) const {
            return init_state.regs.GetRegister(reg);
        }

        uint32_t GetFinalRegister(const REG32 reg, bool const masked) const {
            if (final_state.regs.HasRegister(reg)) {
                uint32_t ret = final_state.regs.GetRegister(reg);
                if (masked && final_state.masks.HasRegister(reg)) {
                    ret &= final_state.masks.GetRegister(reg);
                }
                return ret;
            }
            return GetInitialRegister(reg);
        }
    };

    // Iteration interface
    // --------------------------------------------------------------
    typedef std::vector<Test>::iterator iterator;
    typedef std::vector<Test>::const_iterator const_iterator;
    typedef std::vector<Test>::size_type size_type;

    // non-const iteration
    iterator begin() { return tests_.begin(); }
    iterator end() { return tests_.end(); }

    const_iterator begin() const { return tests_.begin(); }
    const_iterator end() const { return tests_.end(); }

    size_type size() const { return tests_.size(); }
    bool empty() const { return tests_.empty(); }

    // (optionally) indexed access
    Test& operator[](const size_type i) { return tests_[i]; }
    const Test& operator[](const size_type i) const { return tests_[i]; }

    // Public general interface
    // --------------------------------------------------------------
    bool IsRevoked(const Test& test) {
        // ReSharper disable once CppUseAssociativeContains
        return revocation_list_.find(test.hash) != revocation_list_.end();
    }

    bool GetRevokedCount() const {
        return static_cast<bool>(revocation_list_.size());
    }

    bool HasTest(const std::array<uint8_t, 20>& hash) {
        // ReSharper disable once CppUseAssociativeContains
        return test_map_.find(hash) != test_map_.end();
    }

    MooHeader GetHeader() {
        return mooheader_;
    }

    // Get a pointer to the array of tests
    Test* GetTests() {
        return tests_.data();
    }

    // Attempt to get a test by its hash.
    // Throws std::out_of_range if not found.
    Test& GetTest(const std::array<uint8_t, 20>& hash) {
        return tests_[test_map_.at(hash)];
    }

    Reader() = default;

    void AddFromFile(const std::string& filename) {
#ifdef MOO_USE_ZLIB
        if (IsGzipMagic(filename)) {
            ReadGzipFile(filename);
        }
        else {
            ReadRawFile(filename);
        }
#else
        ReadRawFile(filename);
#endif
        Analyze();
    }

    void AddRevocationList(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        std::string line;
        while (std::getline(file, line)) {
            // Skip comment lines or lines with wrong length
            if (line.empty() || line[0] == '#' || line.length() != 40) {
                continue;
            }

            // Convert hex string to byte array
            std::array<uint8_t, 20> hash;
            for (size_t i = 0; i < 20; ++i) {
                hash[i] = HexToInt(line[2 * i]) * 0x10 + HexToInt(line[2 * i + 1]);
            }

            revocation_list_.insert(hash);
        }
    }

    // Helper function to get a register name string given a bit index
    const char* GetRegisterName(const int bit_position) const {
        switch (mooheader_.cpu_type) {
            case CPU8088:
            case CPU8086:
            case CPUV20:
            case CPUV30:
            case CPU286:
            {
                const char* names[] = {"ax", "bx", "cx", "dx", "cs", "ss", "ds", "es", "sp", "bp", "si", "di", "ip",
                                       "flags"};
                if (bit_position >= 0 && bit_position < 14)
                    return names[bit_position];
                break;
            }

            case CPU386E:
            {
                const char* names[] = {"cr0", "cr3", "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp",
                                       "cs", "ds", "es", "fs", "gs", "ss", "eip", "eflags", "dr6", "dr7"};
                if (bit_position >= 0 && bit_position < 20)
                    return names[bit_position];
                break;
            }
            default:
                break;
        }
        return "unknown";
    }

    // Helper function to print human-readable bus status strings
    const char* GetBusStatusName(const uint8_t status) const {
        switch (mooheader_.cpu_type) {
            case CPU8088:
            case CPU8086:
            case CPUV20:
            case CPUV30:
            {
                const char* names[] = {"INTA", "IOR", "IOW", "MEMR", "MEMW", "HALT", "CODE", "PASV"};
                if (status < 8)
                    return names[status];
                break;
            }
            case CPU286:
            {
                const char* names[] = {"INTA", "PASV", "PASV", "PASV", "HALT", "MEMR", "MEMW", "PASV",
                                       "PASV", "IOR ", "IOW ", "PASV", "PASV", "CODE", "PASV", "PASV"};
                if (status < 16)
                    return names[status];
                break;
            }
            case CPU386E:
            {
                const char* names[] = {"INTA", "PASV", "IOR", "IOW", "CODE", "HALT", "MEMR", "MEMW"};
                if (status < 8)
                    return names[status];
                break;
            }
            default:
                break;
        };
        return "UNKNOWN";
    }

    // Helper function to print T-state
    const char* GetTStateName(uint8_t t_state) const {
        switch (mooheader_.cpu_type) {
            case CPU8088:
            case CPU8086:
            case CPUV20:
            case CPUV30:
            {
                const char* names[] = {"Ti", "T1", "T2", "T3", "T4", "Tw"};
                if (t_state < 6)
                    return names[t_state];
            }
            case CPU286:
            {
                const char* names[] = {"Ti", "Ts", "Tc"};
                if (t_state < 3)
                    return names[t_state];
            }
            case CPU386E:
            {
                const char* names[] = {"Ti", "T1", "T2"};
                if (t_state < 3)
                    return names[t_state];
            }
            default:
                break;
        }
        return "unknown";
    }

    // Helper function to print queue operation
    static const char* GetQueueOpName(const uint8_t queue_op) {
        const char* names[] = {"-", "F", "E", "S"};
        return names[queue_op & 0x03];
    }

private:
    // Helper structures for reading
    struct ChunkHeader
    {
        std::string type; //4 characters
        size_t length{};
        size_t data_start{};
        size_t data_end{};
    };

    // We just xor the values, since the input is already random (a hash)
    struct ArrayHash
    {
        size_t operator()(const std::array<uint8_t, 20>& arr) const noexcept {
            size_t hash = 0;
            uint32_t chunks[5];
            std::memcpy(chunks, arr.data(), 20);

            hash ^= chunks[0];
            hash ^= chunks[1];
            hash ^= chunks[2];
            hash ^= chunks[3];
            hash ^= chunks[4];

            return hash;
        }
    };

    static uint32_t HexToInt(const char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        throw std::runtime_error("Invalid value in revocation list.");
    }

    void ReadRawFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        data_.resize(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(data_.data()), size)) {
            throw std::runtime_error("Failed to read file: " + filename);
        }
    }

#ifdef MOO_USE_ZLIB
    void ReadGzipFile(const std::string& filename) {
        const gzFile gz = gzopen(filename.c_str(), "rb");
        if (!gz) {
            throw std::runtime_error("Failed to open gzip file: " + filename);
        }

        std::vector<uint8_t> decompressed;
        decompressed.reserve(1024 * 1024); // arbitrary starting point

        uint8_t buf[4096];
        int bytes_read = 0;
        while ((bytes_read = gzread(gz, buf, static_cast<unsigned int>(sizeof(buf)))) > 0) {
            decompressed.insert(decompressed.end(), buf, buf + bytes_read);
        }

        int gzerr_no = 0;
        const char* gzerr_str = gzerror(gz, &gzerr_no);
        gzclose(gz);

        if (bytes_read < 0 || (gzerr_no != Z_OK && gzerr_no != Z_STREAM_END)) {
            std::string msg = "Failed to read gzip file: " + filename;
            if (gzerr_str) {
                msg += " (";
                msg += gzerr_str;
                msg += ")";
            }
            throw std::runtime_error(msg);
        }

        data_.swap(decompressed);
    }
#endif // MOO_USE_ZLIB

    // Returns true if the file starts with gzip magic bytes
    static bool IsGzipMagic(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        unsigned char b0 = 0, b1 = 0;
        file.read(reinterpret_cast<char*>(&b0), 1);
        file.read(reinterpret_cast<char*>(&b1), 1);
        // gzip magic bytes: 0x1F 0x8B
        return file.good() && b0 == 0x1F && b1 == 0x8B;
    }


    // Reading helper function
    template <typename DATA>
    DATA Read() {
        if (offset_ + sizeof(DATA) > data_.size()) {
            throw std::runtime_error("Read past end of data");
        }
        DATA value = DATA(data_[offset_]);
        for (int i = 1; i < sizeof(DATA); ++i) // This loop will be optimized by the compiler
        {
            value |= (DATA(data_[offset_ + i]) << DATA(i * 8));
        }
        offset_ += sizeof(DATA);
        return value;
    }

    // Read raw bytes from the file into dest
    void ReadBytes(void* dest, const size_t count) {
        if (offset_ + count > data_.size()) {
            throw std::runtime_error("Read past end of data");
        }
        std::memcpy(dest, &data_[offset_], count);
        offset_ += count;
    }

    // Read a chunk header
    ChunkHeader ReadChunkHeader() {
        ChunkHeader header;
        header.type.resize(4);
        ReadBytes(&header.type[0], 4);
        header.length = Read<uint32_t>();
        header.data_start = offset_;
        header.data_end = offset_ + header.length;
        return header;
    }

    // Read a REGS/RMSK chunk
    RegisterState ReadRegisters16() {
        RegisterState regs;
        regs.is_populated = true;
        regs.bitmask = Read<uint16_t>();
        regs.values.resize(static_cast<size_t>(REG16::COUNT));
        regs.type = RegisterState::REG_16;

        // Count set bits and read that many register values
        for (int i = 0; i < 16; i++) {
            if (regs.bitmask & (1 << i)) {
                regs.values[i] = Read<uint16_t>();
            }
        }
        return regs;
    }

    // Read a RG32/RM32 chunk
    RegisterState ReadRegisters32() {
        RegisterState regs;
        regs.is_populated = true;
        regs.bitmask = Read<uint32_t>();
        regs.values.resize(static_cast<size_t>(REG32::COUNT));
        regs.type = RegisterState::REG_32;

        // Count set bits and read that many register values
        for (int i = 0; i < 32; i++) {
            if (regs.bitmask & (1 << i)) {
                regs.values[i] = Read<uint32_t>();
            }
        }
        return regs;
    }

    // Read in a RAM chunk
    std::vector<RamEntry> ReadRam() {
        const uint32_t count = Read<uint32_t>();
        std::vector<RamEntry> entries;
        entries.reserve(count);

        for (uint32_t i = 0; i < count; i++) {
            RamEntry entry;
            entry.address = Read<uint32_t>();
            entry.value = Read<uint8_t>();
            entries.push_back(entry);
        }
        return entries;
    }

    // Read in a QUEU chunk
    QueueData ReadQueue() {
        QueueData queue;
        const uint32_t length = Read<uint32_t>();
        queue.bytes.resize(length);
        ReadBytes(queue.bytes.data(), length);
        return queue;
    }

    // Read in the sub-chunks of a state chunk (INIT/FINA)
    CpuState ReadCpuState(const size_t end_offset) {
        CpuState state;

        while (offset_ < end_offset) {
            ChunkHeader chunk = ReadChunkHeader();

            if (chunk.type == "REGS") {
                state.regs = ReadRegisters16();
            }
            else if (chunk.type == "RG32") {
                state.regs = ReadRegisters32();
            }
            else if (chunk.type == "RMSK") {
                state.masks = ReadRegisters16();
            }
            else if (chunk.type == "RM32") {
                state.masks = ReadRegisters32();
            }
            else if (chunk.type == "RAM ") {
                state.ram = ReadRam();
            }
            else if (chunk.type == "QUEU") {
                state.queue = ReadQueue();
                state.has_queue = true;
            }
            offset_ = chunk.data_end;
        }
        return state;
    }

    // Read the CYCL chunk - returns a vector of decoded Cycle entries
    std::vector<Cycle> ReadCycles() {
        const uint32_t count = Read<uint32_t>();
        std::vector<Cycle> cycles;
        cycles.reserve(count);

        for (uint32_t i = 0; i < count; i++) {
            Cycle cycle;
            cycle.pin_bitfield0 = Read<uint8_t>();
            cycle.address_latch = Read<uint32_t>();
            cycle.segment_status = Read<uint8_t>();
            cycle.memory_status = Read<uint8_t>();
            cycle.io_status = Read<uint8_t>();
            cycle.pin_bitfield1 = Read<uint8_t>();
            cycle.data_bus = Read<uint16_t>();
            cycle.bus_status = Read<uint8_t>();
            cycle.t_state = Read<uint8_t>();
            cycle.queue_op_status = Read<uint8_t>();
            cycle.queue_byte_read = Read<uint8_t>();
            cycles.push_back(cycle);
        }

        return cycles;
    }

    Test ReadTest() {
        Test test;
        ChunkHeader test_header = ReadChunkHeader();

        // Skipping non-TEST chunks
        while (test_header.type != "TEST") {
            offset_ = test_header.data_end;
            test_header = ReadChunkHeader();
        }

        test.index = Read<uint32_t>();
        while (offset_ < test_header.data_end) {
            ChunkHeader chunk = ReadChunkHeader();

            if (chunk.type == "NAME") {
                const uint32_t name_len = Read<uint32_t>();
                test.name.resize(name_len);
                ReadBytes(&test.name[0], name_len);
            }
            else if (chunk.type == "BYTS") {
                const uint32_t byte_count = Read<uint32_t>();
                test.bytes.resize(byte_count);
                ReadBytes(&test.bytes[0], byte_count);
            }
            else if (chunk.type == "INIT") {
                test.init_state = ReadCpuState(chunk.data_end);
            }
            else if (chunk.type == "FINA") {
                test.final_state = ReadCpuState(chunk.data_end);
            }
            else if (chunk.type == "CYCL") {
                test.cycles = ReadCycles();
            }
            else if (chunk.type == "EXCP") {
                test.exception.number = Read<uint8_t>();
                test.exception.flag_addr = Read<uint32_t>();
                test.has_exception = true;
            }
            else if (chunk.type == "HASH") {
                ReadBytes(test.hash.data(), 20);
                test.has_hash = true;
            }
            else if (chunk.type == "GMET") {
                // Skip generating metadata
            }
            else {
                // Skip unknown chunks
                std::cout << "Skipping unknown chunk " << chunk.type << std::endl;
            }

            // Ensure we're at the chunk boundary
            offset_ = chunk.data_end;
        }
        offset_ = test_header.data_end;

        return test;
    }

    // Reads the MOO file header chunk
    void ReadMooHeader() {
        mooheader_.version_major = Read<uint8_t>();
        mooheader_.version_minor = Read<uint8_t>();
        ReadBytes(mooheader_.reserved, 2);
        mooheader_.test_count = Read<uint32_t>();

        mooheader_.cpu_name = std::string(8, ' ');
        if (mooheader_.GetVersionU16() == 0x0100) {
            // MOO version 1.0
            ReadBytes(&mooheader_.cpu_name[0], 4);
        }
        else if (mooheader_.GetVersionU16() == 0x0101) {
            // MOO version 1.1
            ReadBytes(&mooheader_.cpu_name[0], 4);
        }
        else {
            std::stringstream err;
            err << "Unsupported MOO version: " << mooheader_.version_major << "." << mooheader_.version_minor;
            throw std::runtime_error(err.str());
        }

        // Add new CPUs here and the CPUType enum
        if ((mooheader_.cpu_name == "8088    ") || (mooheader_.cpu_name == "88      "))
            mooheader_.cpu_type = CPU8088;
        else if (mooheader_.cpu_name == "8086    ")
            mooheader_.cpu_type = CPU8086;
        else if (mooheader_.cpu_name == "V20     ")
            mooheader_.cpu_type = CPUV20;
        else if (mooheader_.cpu_name == "V30     ")
            mooheader_.cpu_type = CPUV30;
        else if (mooheader_.cpu_name == "286     ")
            mooheader_.cpu_type = CPU286;
        else if (mooheader_.cpu_name == "C286    ")
            mooheader_.cpu_type = CPU286; // TODO: should C286 have its own enum value?
        else if (mooheader_.cpu_name == "386E    ")
            mooheader_.cpu_type = CPU386E;
        else {
            throw std::runtime_error("Unsupported CPU type: " + mooheader_.cpu_name);
        }
    }

    void Analyze() {
        offset_ = 0;

        // First chunk must be "MOO "
        const ChunkHeader first_chunk_header = ReadChunkHeader();
        if (first_chunk_header.type != "MOO ") {
            throw std::runtime_error("Invalid MOO file - missing MOO header");
        }

        ReadMooHeader();
        offset_ = first_chunk_header.data_end;

        // Read all tests
        tests_.reserve(mooheader_.test_count);

        for (uint32_t i = 0; i < mooheader_.test_count; i++) {
            tests_.push_back(ReadTest());
            test_map_[tests_.back().hash] = i;
        }
    }

    MooHeader mooheader_;
    std::vector<Test> tests_;
    std::vector<uint8_t> data_;
    size_t offset_ = 0;
    std::unordered_map<std::array<uint8_t, 20>, size_t, ArrayHash> test_map_; // Maps hash to index in tests
    std::unordered_set<std::array<uint8_t, 20>, ArrayHash> revocation_list_;
};

};

#endif // MOOREADER_H__
