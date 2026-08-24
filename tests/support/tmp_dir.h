#pragma once

// Scratch file helpers. Tests that touch the filesystem (config, riff, wav)
// write into the build tree so a test run never pollutes the source tree and
// parallel CTest jobs don't collide on a shared name.

#include <atomic>
#include <filesystem>
#include <string>
#include <system_error>

#ifndef SDRPP_TEST_TMP_DIR
#define SDRPP_TEST_TMP_DIR "."
#endif

namespace sdrpp_test {

    inline const std::string& tmpDir() {
        static const std::string dir = []() {
            std::string d = SDRPP_TEST_TMP_DIR;
            std::error_code ec;
            std::filesystem::create_directories(d, ec);
            return d;
        }();
        return dir;
    }

    // Unique path inside the scratch directory. `prefix` only exists to make
    // leftovers identifiable when a test fails and the file isn't cleaned up.
    inline std::string tmpPath(const std::string& prefix) {
        static std::atomic<unsigned> counter{ 0 };
        return tmpDir() + "/" + prefix + "_" + std::to_string(counter++);
    }

    // RAII scratch file: removed when the test case ends, pass or fail.
    class ScopedTmpFile {
    public:
        explicit ScopedTmpFile(const std::string& prefix) : _path(tmpPath(prefix)) {}
        ~ScopedTmpFile() {
            std::error_code ec;
            std::filesystem::remove(_path, ec);
        }

        ScopedTmpFile(const ScopedTmpFile&) = delete;
        ScopedTmpFile& operator=(const ScopedTmpFile&) = delete;

        const std::string& path() const { return _path; }
        bool exists() const {
            std::error_code ec;
            return std::filesystem::exists(_path, ec);
        }
        std::uintmax_t size() const {
            std::error_code ec;
            auto s = std::filesystem::file_size(_path, ec);
            return ec ? 0 : s;
        }

    private:
        std::string _path;
    };

} // namespace sdrpp_test
