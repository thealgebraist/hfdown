#pragma once

#include <filesystem>
#include <expected>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional> // for std::function

#ifdef HAS_IO_URING
#include <liburing.h>
#endif

namespace hfdown {

struct FileWriteError {
    std::string message;
    int code;
};

// Represents a pending write operation
struct WriteRequest {
    std::vector<char> data; // Buffer to hold the data to write
    size_t offset;          // Offset in the file to write to
};

class AsyncFileWriter {
public:
    AsyncFileWriter(const std::filesystem::path& path, size_t file_size);
    ~AsyncFileWriter();

    // Deleted copy constructors/assignment operators to prevent accidental copying
    AsyncFileWriter(const AsyncFileWriter&) = delete;
    AsyncFileWriter& operator=(const AsyncFileWriter&) = delete;

    // Move constructors/assignment operators for efficiency
    AsyncFileWriter(AsyncFileWriter&&) noexcept;
    AsyncFileWriter& operator=(AsyncFileWriter&&) noexcept;

    std::expected<void, FileWriteError> write_at(const void* data, size_t size, size_t offset);
    std::expected<void, FileWriteError> sync();
    void close();

private:
    int fd_;
    std::filesystem::path path_;
    
    // For asynchronous writing
    std::deque<WriteRequest> write_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::jthread writer_thread_;
    std::atomic<bool> stop_thread_ = false;

    // Worker function for the writer thread
    void writer_worker_thread();
    
#ifdef HAS_IO_URING
    io_uring ring_;
    bool ring_initialized_ = false;
#endif

    std::expected<void, FileWriteError> fallback_write(const void* data, size_t size, size_t offset);
};

} // namespace hfdown
