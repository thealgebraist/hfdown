#pragma once
#include <string>
#include <expected>
#include <filesystem>
#include <vector>
#include <mutex>

#ifdef __linux__
#include <liburing.h>
#define HAS_IO_URING
#endif

namespace hfdown {

struct FileWriteError {
    std::string message;
    int error_code;
};

// High-performance asynchronous file writer abstraction
// Uses io_uring on Linux and pwrite fallback on other POSIX systems
class AsyncFileWriter {
public:
    AsyncFileWriter(const std::filesystem::path& path, size_t file_size);
    ~AsyncFileWriter();

    // Writes data at specified offset. 
    // In io_uring mode, this is asynchronous.
    // In fallback mode, this uses synchronous pwrite.
    std::expected<void, FileWriteError> write_at(const void* data, size_t size, size_t offset);

    // Flushes all pending writes and ensures data is on disk
    std::expected<void, FileWriteError> sync();

    void close();

private:
    int fd_ = -1;
    std::filesystem::path path_;
    std::mutex mutex_;

#ifdef HAS_IO_URING
    struct io_uring ring_;
    bool ring_initialized_ = false;
    // We maintain a small pool of buffers for async writes if needed
    // but typically we can use the buffers passed from curl
#endif

    std::expected<void, FileWriteError> fallback_write(const void* data, size_t size, size_t offset);
};

} // namespace hfdown
