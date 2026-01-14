#include "async_file_writer.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <format>
#include <iostream>

namespace hfdown {

AsyncFileWriter::AsyncFileWriter(const std::filesystem::path& path, size_t file_size)
    : path_(path)
{
    fd_ = open(path.c_str(), O_WRONLY | O_CREAT, 0644);
    if (fd_ == -1) {
        std::cerr << std::format("Error opening file {}: {}\n", path.string(), strerror(errno));
        // Don't start the thread if file open failed
        return; 
    }

    // Start the writer thread only if the file was successfully opened
    writer_thread_ = std::jthread([this] { writer_worker_thread(); });

    if (file_size > 0) {
#ifdef __APPLE__
        fstore_t store;
        memset(&store, 0, sizeof(store));
        store.fst_flags = F_ALLOCATEALL;
        store.fst_posmode = 0;
        store.fst_offset = 0;
        store.fst_length = (off_t)file_size;
        fcntl(fd_, F_PREALLOCATE, &store);
        ftruncate(fd_, file_size);
#elif defined(__linux__)
        posix_fallocate(fd_, 0, file_size);
#endif
    }
}

AsyncFileWriter::~AsyncFileWriter() {
    close();
}

AsyncFileWriter::AsyncFileWriter(AsyncFileWriter&& other) noexcept
    : fd_(other.fd_),
      path_(std::move(other.path_)),
      write_queue_(std::move(other.write_queue_)),
      stop_thread_(other.stop_thread_.load())
#ifdef HAS_IO_URING
      , ring_(other.ring_),
      ring_initialized_(other.ring_initialized_)
#endif
{
    writer_thread_ = std::move(other.writer_thread_); 
    other.fd_ = -1; 
    other.stop_thread_ = true;
#ifdef HAS_IO_URING
    other.ring_initialized_ = false;
#endif
}

AsyncFileWriter& AsyncFileWriter::operator=(AsyncFileWriter&& other) noexcept {
    if (this != &other) {
        close(); 
        fd_ = other.fd_;
        path_ = std::move(other.path_);
        write_queue_ = std::move(other.write_queue_);
        stop_thread_ = other.stop_thread_.load();
        
        writer_thread_ = std::move(other.writer_thread_);

        other.fd_ = -1;
        other.stop_thread_ = true;
#ifdef HAS_IO_URING
        ring_ = other.ring_; 
        ring_initialized_ = other.ring_initialized_;
        other.ring_initialized_ = false;
#endif
    }
    return *this;
}

void AsyncFileWriter::close() {
    if (fd_ != -1) {
        stop_thread_ = true;
        queue_cv_.notify_all();
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }

        std::unique_lock<std::mutex> lock(queue_mutex_);
        while (!write_queue_.empty()) {
            WriteRequest request = std::move(write_queue_.front());
            write_queue_.pop_front();
            lock.unlock();
            fallback_write(request.data.data(), request.data.size(), request.offset);
            lock.lock();
        }
        lock.unlock();

        sync();
        ::close(fd_);
        fd_ = -1;
    }
#ifdef HAS_IO_URING
    if (ring_initialized_) {
        io_uring_queue_exit(&ring_);
        ring_initialized_ = false;
    }
#endif
}

std::expected<void, FileWriteError> AsyncFileWriter::write_at(const void* data, size_t size, size_t offset) {
    if (fd_ == -1) {
        return std::unexpected(FileWriteError{"File not open", -1});
    }
    if (!writer_thread_.joinable() && !stop_thread_) {
        // This means the constructor failed to open the file and writer_thread_ was not initialized.
        return std::unexpected(FileWriteError{"Writer thread not running, file not opened correctly.", -1});
    }

    // Create a WriteRequest and push to queue
    WriteRequest request;
    request.data.assign(static_cast<const char*>(data), static_cast<const char*>(data) + size);
    request.offset = offset;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        write_queue_.push_back(std::move(request));
    }
    queue_cv_.notify_one(); // Notify the writer thread that there's new data

    return {}; // Return immediately, write happens in background
}

void AsyncFileWriter::writer_worker_thread() {
#ifdef HAS_IO_URING
    // io_uring requires setup per thread for some cases, or careful submission.
    // For simplicity, we'll initialize it once per AsyncFileWriter.
    // This part is for Linux only and will use the ring_ member.
    // On macOS, HAS_IO_URING is not defined, so this block is skipped.
#endif

    while (true) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] { return stop_thread_ || !write_queue_.empty(); });

        if (stop_thread_ && write_queue_.empty()) {
            break; // Exit thread if stop signal received and queue is empty
        }

        WriteRequest request = std::move(write_queue_.front());
        write_queue_.pop_front();
        lock.unlock(); // Release lock before blocking write

        // Perform the actual write operation
#ifdef HAS_IO_URING
        if (ring_initialized_) {
            struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
            if (sqe) {
                io_uring_prep_write(sqe, fd_, request.data.data(), request.data.size(), request.offset);
                io_uring_submit(&ring_);
                io_uring_cqe* cqe;
                int ret = io_uring_wait_cqe(&ring_, &cqe); // This blocks the worker thread
                if (ret < 0) {
                    std::cerr << std::format("io_uring_wait_cqe failed: {}\n", strerror(-ret));
                } else {
                    if (cqe->res < 0) {
                         std::cerr << std::format("io_uring write failed: {}\n", strerror(-cqe->res));
                    }
                    io_uring_cqe_seen(&ring_, cqe);
                }
            } else {
                 std::cerr << "io_uring submission queue full, falling back to pwrite.\n";
                 fallback_write(request.data.data(), request.data.size(), request.offset);
            }
        } else {
            fallback_write(request.data.data(), request.data.size(), request.offset);
        }
#else
        // Fallback for non-io_uring systems (like macOS)
        fallback_write(request.data.data(), request.data.size(), request.offset);
#endif
    }
}


std::expected<void, FileWriteError> AsyncFileWriter::fallback_write(const void* data, size_t size, size_t offset) {
    ssize_t written = pwrite(fd_, data, size, offset);
    if (written == -1) {
        return std::unexpected(FileWriteError{std::format("pwrite failed: {}", strerror(errno)), errno});
    }
    if (static_cast<size_t>(written) != size) {
        return std::unexpected(FileWriteError{"Incomplete write", -1});
    }
    return {};
}

std::expected<void, FileWriteError> AsyncFileWriter::sync() {
    if (fd_ == -1) return {};

    // Wait for all pending writes to complete
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] { return write_queue_.empty(); });
    }

#ifdef HAS_IO_URING
    if (ring_initialized_) {
        // Wait for all pending completions
        io_uring_submit_and_wait(&ring_, 0);
    }
#endif

#ifdef __APPLE__
    if (fcntl(fd_, F_FULLFSYNC) == -1) {
#else
    if (fdatasync(fd_) == -1) {
#endif
        return std::unexpected(FileWriteError{std::format("sync failed: {}", strerror(errno)), errno});
    }
    return {};
}

} // namespace hfdown
