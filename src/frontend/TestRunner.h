#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_map>

#include <zlib.h>

#include "Cpu.h"
#include "StubBus.h"
#include "mooreader.h"

class TestRunner
{
public:
    TestRunner() = default;

    // Add all files matching .MOO or .MOO.gz from the provided path.
    // If `path` is a directory, it will be traversed recursively.
    // If `path` is a file and matches the extensions, it will be added.
    void addFiles(const std::string& pathStr) {
        namespace fs = std::filesystem;
        const fs::path p(pathStr);
        if (!fs::exists(p)) {
            return;
        }

        if (fs::is_regular_file(p)) {
            if (matchesExtension(p.filename().string())) {
                files_.push_back(fs::absolute(p));
            }
            return;
        }

        if (fs::is_directory(p)) {
            for (auto const& entry : fs::recursive_directory_iterator(p)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                const auto name = entry.path().filename().string();
                if (matchesExtension(name)) {
                    files_.push_back(fs::absolute(entry.path()));
                }
            }
        }
    }

    void listFiles() const {
        for (const auto& f : files_) {
            std::cout << f.string() << "\n";
        }
    }

    bool runAllTests(size_t max_tests = 0);
    // Access collected files
    const std::vector<std::filesystem::path>& files() const { return files_; }

private:
    struct TestContext
    {
        Cpu<StubBus> cpu;
        size_t max_tests;
    };

    bool runTestFile(const std::filesystem::path& filepath, size_t max_tests = 0);
    bool runTest(TestContext& ctx, const Moo::Reader::Test& test, const std::filesystem::path& filepath);

    // Summary reporting
    size_t total_files_run_ = 0;
    size_t total_tests_run_ = 0;
    size_t per_file_tests_run_ = 0;
    size_t total_passed_ = 0;
    size_t total_failed_ = 0;
    size_t total_flag_failed_ = 0;

    struct FailureDetail
    {
        std::string file; // base filename
        size_t test_index; // test index
        std::string test_name; // test name
        std::string message; // human-readable failure message
        std::vector<uint16_t> regs; // snapshot of REG16 registers in Moo::REG16 order
    };

    std::vector<FailureDetail> failure_details_;

    struct FileSummary
    {
        size_t total = 0;
        size_t passed = 0;
        size_t failed = 0;
        size_t reg_failed = 0;
        size_t mem_failed = 0;
        size_t flag_failed = 0; // special-case register failures for FLAGS
    };

    std::unordered_map<std::string, FileSummary> file_summaries_;

    void printSummary() const;

    static bool matchesExtension(const std::string& filename) {
        // Case-insensitive check for suffixes .moo and .moo.gz
        std::string s = filename;
        std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
        if (s.size() >= 4 && s.substr(s.size() - 4) == ".moo") {
            return true;
        }
        if (s.size() >= 7 && s.substr(s.size() - 7) == ".moo.gz") {
            return true;
        }
        return false;
    }

    static bool isGzipFile(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }

        unsigned char m0 = 0, m1 = 0;
        in.read(reinterpret_cast<char*>(&m0), 1);
        in.read(reinterpret_cast<char*>(&m1), 1);
        return in && m0 == 0x1F && m1 == 0x8B;
    }

    static std::vector<std::uint8_t> readGzipFile(const std::string& path) {
        const gzFile f = gzopen(path.c_str(), "rb");
        if (!f) {
            throw std::runtime_error("Failed to open gzip file: " + path);
        }

        std::vector<std::uint8_t> data;
        constexpr std::size_t kChunkSize = 4096;
        std::uint8_t buf[kChunkSize];

        while (true) {
            const int n = gzread(f, buf, static_cast<unsigned>(kChunkSize));
            if (n < 0) {
                int err_num = 0;
                const char* err_str = gzerror(f, &err_num);
                gzclose(f);
                throw std::runtime_error(
                    std::string("gzread() failed: ") + (err_str ? err_str : "unknown error")
                    );
            }
            if (n == 0) {
                break; // EOF
            }
            data.insert(data.end(), buf, buf + n);
        }

        gzclose(f);
        return data;
    }

    static std::vector<std::uint8_t> readFileMaybeGzipped(const std::string& path) {
        if (isGzipFile(path)) {
            return readGzipFile(path);
        }

        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Failed to open file: " + path);
        }

        std::vector<std::uint8_t> data(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
            );
        return data;
    }

    std::vector<std::filesystem::path> files_;
};
