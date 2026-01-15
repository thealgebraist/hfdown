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

    // Deleted copy constructors/assignment operators
    AsyncFileWriter(const AsyncFileWriter&) = delete;
    AsyncFileWriter& operator=(const AsyncFileWriter&) = delete;

    // Move constructors/assignment operators
    AsyncFileWriter(AsyncFileWriter&&) noexcept;
    AsyncFileWriter& operator=(AsyncFileWriter&&) noexcept;

    std::expected<void, FileWriteError> write_at(const void* data, size_t size, size_t offset);
    std::expected<void, FileWriteError> sync();
    void close();

private:
    int fd_;
    void* mmap_ptr_ = nullptr;
    size_t file_size_ = 0;
    std::filesystem::path path_;
};

} // namespace hfdown
