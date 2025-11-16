#include <format>

#include "TestRunner.h"

Register MooRegToRegister(const Moo::REG16 reg) {
    switch (reg) {
        case Moo::REG16::AX:
            return Register::AX;
        case Moo::REG16::BX:
            return Register::BX;
        case Moo::REG16::CX:
            return Register::CX;
        case Moo::REG16::DX:
            return Register::DX;
        case Moo::REG16::CS:
            return Register::CS;
        case Moo::REG16::SS:
            return Register::SS;
        case Moo::REG16::DS:
            return Register::DS;
        case Moo::REG16::ES:
            return Register::ES;
        case Moo::REG16::SP:
            return Register::SP;
        case Moo::REG16::BP:
            return Register::BP;
        case Moo::REG16::SI:
            return Register::SI;
        case Moo::REG16::DI:
            return Register::DI;
        case Moo::REG16::IP:
            return Register::PC;
        case Moo::REG16::FLAGS:
            return Register::FLAGS;
        default:
            throw std::runtime_error("Unknown MOO REG16");
    }
}

// Convert internal Register enum to a human-readable string (for test reporting)
static std::string GetRegisterString(Register r) {
    static const char* names[] = {
        "ES", "CS", "SS", "DS", "IP", "IND", "OPR", "R7",
        "R8", "R9", "R10", "R11", "TMPA", "TMPB", "TMPC", "FLAGS",
        "R16", "R17", "M", "R", "SIGMA", "ONES", "R22", "R23",
        "AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"
    };
    const size_t idx = static_cast<size_t>(r);
    return names[idx];
}

// Print differences between two 8088 FLAGS values. Returns a short textual summary of changed flags.
static std::string printFlagDiff(uint16_t expected, uint16_t actual) {
    // Consider the 8088 flags of interest and omit reserved bits.
    // bit positions: 0 CF, 2 PF, 4 AF, 6 ZF, 7 SF, 8 TF, 9 IF, 10 DF, 11 OF
    static const std::pair<int, const char*> flagMap[] = {
        {0, "CF"}, {2, "PF"}, {4, "AF"}, {6, "ZF"}, {7, "SF"}, {8, "TF"}, {9, "IF"}, {10, "DF"}, {11, "OF"}
    };

    std::string out;
    bool first = true;
    for (const auto& p : flagMap) {
        const int bit = p.first;
        const char* name = p.second;
        const int e = (expected >> bit) & 1;
        const int a = (actual >> bit) & 1;
        if (e != a) {
            if (!first)
                out += "; ";
            out += std::format("{}:e{},a{}", name, e, a);
            first = false;
        }
    }
    if (out.empty()) {
        return std::string("(no bit-level differences detected)");
    }
    return out;
}

bool TestRunner::runAllTests(size_t max_tests) {

    bool all_ok = true;
    for (const auto& filepath : files_) {
        if (!runTestFile(filepath, max_tests)) {
            all_ok = false;
        }
    }

    // Print a summary of test results
    printSummary();

    return all_ok;
}

bool TestRunner::runTestFile(const std::filesystem::path& filepath, size_t max_tests) {
    Moo::Reader reader;

    std::cout << "Loading MOO file: " << filepath << "\n";
    reader.AddFromFile(filepath.generic_string());

    std::cout << "\n========================================\n";
    std::cout << "MOO File Information\n";
    std::cout << "========================================\n";
    const auto header = reader.GetHeader();
    const auto version = header.GetVersion();
    std::cout << "Version: " << static_cast<int>(version.first) << "." << static_cast<int>(version.second) << "\n";
    std::cout << "CPU: " << header.cpu_name << "\n";
    std::cout << "Test Count: " << header.test_count << "\n";

    auto ctx = TestContext{
        .cpu = Cpu<StubBus>(),
        .max_tests = max_tests
    };

    bool file_ok = true;
    // Run up to max_tests for this file (if max_tests == 0, run all)
    size_t per_file_count = 0;
    for (const auto& test : reader) {
        if (max_tests != 0 && per_file_count >= max_tests) {
            break; // per-file cap reached
        }
        if (!runTest(ctx, test, filepath)) {
            file_ok = false;
        }
        ++per_file_count;
    }

    ++total_files_run_;
    return file_ok;
}

bool TestRunner::runTest(TestContext& ctx, const Moo::Reader::Test& test, const std::filesystem::path& filepath) {
    std::cout << std::format("Running test [{}/{}]: {:<50}", test.index, ctx.max_tests, test.name) << "\n";

    auto& cpu = ctx.cpu;
    // Reset CPU
    cpu.reset();
    cpu.getBus()->reset();

    ++total_tests_run_;
    bool test_failed = false;
    bool reg_failed_in_test = false;
    bool mem_failed_in_test = false;
    bool flag_failed_in_test = false;

    const std::string fname = filepath.filename().string();
    auto& fsum = file_summaries_[fname];
    ++fsum.total;

    // Set up initial CPU state
    for (const auto r : Moo::REG16Range()) {
        cpu.setRegister(MooRegToRegister(r), test.GetInitialRegister(r));
    }

    // Write the initial memory state
    for (const auto m : test.init_state.ram) {
        auto [address, value] = m;
        cpu.getBus()->ram()[address & 0xFFFFF] = value;
    }

    // Run the instruction (ignore cycle count)
    cpu.stepToNextInstruction();
    // Cycle one more time to let any terminating write complete
    cpu.run_for(1);

    //std::cout << std::format("Completed in {} cycles.\n", cycles);

    // Read back final CPU register state
    for (const auto r : Moo::REG16Range()) {

        // Skip IP for now
        if (r == Moo::REG16::IP) {
            continue;
        }

        const uint16_t expected = test.GetFinalRegister(r, true);
        const uint16_t actual = cpu.getRegister(MooRegToRegister(r));

        if (actual != expected) {
            if (r == Moo::REG16::FLAGS) {
                // For FLAGS mismatches, produce a human-friendly bit-diff description.
                const auto diff = printFlagDiff(expected, actual);
                TestRunner::FailureDetail fd{};
                fd.file = fname;
                fd.test_name = test.name;
                fd.test_index = test.index;
                fd.message = std::format("FLAGS mismatch: expected {:#04X}, got {:#04X} | {}",
                                         static_cast<unsigned>(expected), static_cast<unsigned>(actual), diff);
                // capture register snapshot in REG16 order
                for (const auto rr : Moo::REG16Range()) {
                    fd.regs.push_back(cpu.getRegister(MooRegToRegister(rr)));
                }
                failure_details_.push_back(std::move(fd));
                test_failed = true;
                flag_failed_in_test = true;
            }
            else {
                TestRunner::FailureDetail fd{};
                fd.file = fname;
                fd.test_name = test.name;
                fd.test_index = test.index;
                fd.message = std::format("Register {} mismatch: expected {:04X}, got {:04X}",
                                         GetRegisterString(MooRegToRegister(r)), static_cast<unsigned>(expected),
                                         static_cast<unsigned>(actual));
                for (const auto rr : Moo::REG16Range()) {
                    fd.regs.push_back(cpu.getRegister(MooRegToRegister(rr)));
                }
                failure_details_.push_back(std::move(fd));
                test_failed = true;
                reg_failed_in_test = true;
            }
        }
    }

    // Validate final memory state
    for (const auto m : test.final_state.ram) {
        const auto [address, expected] = m;
        const auto actual = cpu.getBus()->ram()[address & 0xFFFFF];

        if (actual != expected) {
            TestRunner::FailureDetail fd{};
            fd.file = fname;
            fd.test_name = test.name;
            fd.test_index = test.index;
            fd.message = std::format("Memory[{:#05X}] mismatch: expected {:02X}, got {:02X}",
                                     static_cast<unsigned>(address), static_cast<unsigned>(expected),
                                     static_cast<unsigned>(actual));
            for (const auto rr : Moo::REG16Range()) {
                fd.regs.push_back(cpu.getRegister(MooRegToRegister(rr)));
            }
            failure_details_.push_back(std::move(fd));
            test_failed = true;
            mem_failed_in_test = true;
        }
    }

    if (test_failed) {
        ++total_failed_;
        ++fsum.failed;
        if (reg_failed_in_test) {
            ++fsum.reg_failed;
        }
        if (mem_failed_in_test) {
            ++fsum.mem_failed;
        }
        if (flag_failed_in_test) {
            ++fsum.flag_failed;
            ++total_flag_failed_;
        }
    }
    else {
        ++total_passed_;
        ++fsum.passed;
    }

    return !test_failed;
}

// Print registers in a compact grouped format from a snapshot vector taken in Moo::REG16 order.
static void printRegisters(const std::vector<uint16_t>& regs, int indent = 0, std::ostream& os = std::cout) {
    // Build a mapping from Moo::REG16 -> value using the same iteration order used when capturing the snapshot.
    std::unordered_map<int, uint16_t> map;
    size_t i = 0;
    for (const auto r : Moo::REG16Range()) {
        if (i < regs.size())
            map[static_cast<int>(r)] = regs[i++];
    }

    auto get = [&](Moo::REG16 r)-> uint16_t
    {
        auto it = map.find(static_cast<int>(r));
        return it != map.end() ? it->second : 0;
    };

    // Create padding string
    const std::string pad(indent, ' ');

    // Helper to format 16-bit hex
    auto hx = [](uint16_t v) { return std::format("{:04X}", static_cast<unsigned>(v)); };

    // Primary/general registers
    os << pad << std::format("AX: {} BX: {} CX: {} DX: {}\n", hx(get(Moo::REG16::AX)), hx(get(Moo::REG16::BX)),
                             hx(get(Moo::REG16::CX)), hx(get(Moo::REG16::DX)));
    // Index/stack regs
    os << pad << std::format("SI: {} DI: {} BP: {} SP: {}\n", hx(get(Moo::REG16::SI)), hx(get(Moo::REG16::DI)),
                             hx(get(Moo::REG16::BP)), hx(get(Moo::REG16::SP)));
    // Segment registers
    os << pad << std::format("CS: {} DS: {} ES: {} SS: {}\n", hx(get(Moo::REG16::CS)), hx(get(Moo::REG16::DS)),
                             hx(get(Moo::REG16::ES)), hx(get(Moo::REG16::SS)));
    // IP and FLAGS
    const uint16_t ip = get(Moo::REG16::IP);
    const uint16_t flags = get(Moo::REG16::FLAGS);
    os << pad << std::format("IP:*{}    FLAGS:*{} ", hx(ip), hx(flags));

    // Compact FLAGS decode: produce a compact bit+letter sequence e.g. "1o0d1i..."
    struct FlagInfo
    {
        int bit;
        char code;
        const char* name;
    };
    static const FlagInfo flagOrder[] = {
        {11, 'o', "OF"}, {10, 'd', "DF"}, {9, 'i', "IF"}, {8, 't', "TF"}, {7, 's', "SF"}, {6, 'z', "ZF"},
        {4, 'a', "AF"}, {2, 'p', "PF"}, {0, 'c', "CF"}
    };
    std::string compact;
    for (const auto& f : flagOrder) {
        int val = (flags >> f.bit) & 1;
        compact += (val ? '1' : '0');
        compact += f.code;
    }
    os << compact;

    os << '\n';
}

void TestRunner::printSummary() const {
    // Summary header
    std::cout << std::format(
        "\n====== Test Summary ======\nFiles: {}\nTests run: {}\nPassed: {}\nFailed: {}\nFlag failures: {}\n",
        files_.size(), total_tests_run_, total_passed_, total_failed_, total_flag_failed_);
    if (!failure_details_.empty()) {
        std::cout << "\nFailures:\n";

        for (const auto& fd : failure_details_) {
            std::cout << std::format("File: {} Test [{:>05}]: {:<40} {}\n", fd.file, fd.test_index, fd.test_name,
                                     fd.message);
            if (!fd.regs.empty()) {
                std::cout << "  Registers:\n";
                printRegisters(fd.regs, 2);
            }
        }
    }

    // Print per-file table
    if (!file_summaries_.empty()) {
        std::cout << "\nPer-file results:\n";
        // Header using std::format alignment
        std::cout << std::format("{:<20}{:>8}{:>8}{:>8}{:>12}{:>12}{:>12}\n", "File", "Total", "Passed", "Failed",
                                 "RegFailed", "MemFailed", "FlagFailed");
        std::cout << std::string(20 + 8 + 8 + 8 + 12 + 12 + 12, '-') << "\n";
        // Sort entries by filename for stable output
        std::vector<std::pair<std::string, FileSummary>> items;
        items.reserve(file_summaries_.size());
        for (const auto& kv : file_summaries_)
            items.emplace_back(kv.first, kv.second);
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& kv : items) {
            const auto& name = kv.first;
            const auto& s = kv.second;
            std::cout << std::format("{:<20}{:>8}{:>8}{:>8}{:>12}{:>12}{:>12}\n",
                                     name, s.total, s.passed, s.failed, s.reg_failed, s.mem_failed, s.flag_failed);
        }
    }

    std::cout << "==========================\n";
}
