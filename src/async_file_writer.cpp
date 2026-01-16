#include "async_file_writer.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>
#include <format>
#include <iostream>

namespace hfdown {

AsyncFileWriter::AsyncFileWriter(const std::filesystem::path& path, size_t file_size)
    : path_(path), file_size_(file_size)
{
    fd_ = open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ == -1) {
        std::cerr << std::format("Error opening file {}: \n", path.string(), strerror(errno));
        return; 
    }

    if (file_size > 0) {
        if (ftruncate(fd_, file_size) == -1) {
            std::cerr << std::format("Error truncating file {}: \n", path.string(), strerror(errno));
            ::close(fd_);
            fd_ = -1;
            return;
        }

        int mmap_flags = MAP_SHARED;
#ifdef MAP_HUGETLB
        if (file_size >= 2 * 1024 * 1024) mmap_flags |= MAP_HUGETLB;
#endif
        mmap_ptr_ = mmap(nullptr, file_size, PROT_READ | PROT_WRITE, mmap_flags, fd_, 0);
#ifdef MAP_HUGETLB
        if (mmap_ptr_ == MAP_FAILED && (mmap_flags & MAP_HUGETLB)) {
            // Fallback to regular pages if huge pages fail
            mmap_ptr_ = mmap(nullptr, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        }
#endif
        
        if (mmap_ptr_ == MAP_FAILED) {
            std::cerr << std::format("Error mmaping file {}: \n", path.string(), strerror(errno));
            mmap_ptr_ = nullptr;
            // Fallback: we could still use pwrite, but for now we follow the instruction to use mmap
        }
    }
}

AsyncFileWriter::~AsyncFileWriter() {
    close();
}

AsyncFileWriter::AsyncFileWriter(AsyncFileWriter&& other) noexcept
    : fd_(other.fd_),
      mmap_ptr_(other.mmap_ptr_),
      file_size_(other.file_size_),
      path_(std::move(other.path_))
{
    other.fd_ = -1;
    other.mmap_ptr_ = nullptr;
    other.file_size_ = 0;
}

AsyncFileWriter& AsyncFileWriter::operator=(AsyncFileWriter&& other) noexcept {
    if (this != &other) {
        close(); 
        fd_ = other.fd_;
        mmap_ptr_ = other.mmap_ptr_;
        file_size_ = other.file_size_;
        path_ = std::move(other.path_);

        other.fd_ = -1;
        other.mmap_ptr_ = nullptr;
        other.file_size_ = 0;
    }
    return *this;
}

void AsyncFileWriter::close() {
    if (mmap_ptr_) {
        munmap(mmap_ptr_, file_size_);
        mmap_ptr_ = nullptr;
    }
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
}

std::expected<void, FileWriteError> AsyncFileWriter::write_at(const void* data, size_t size, size_t offset) {
    if (mmap_ptr_) {
        if (offset + size > file_size_) {
            return std::unexpected(FileWriteError{"Write out of bounds", EINVAL});
        }
        // Hint to the kernel that we are going to write here
        // Using __builtin_assume_aligned if we could guarantee alignment
        std::memcpy(static_cast<char*>(mmap_ptr_) + offset, data, size);
        return {};
    } else if (fd_ != -1) {
        // Fallback to pwrite if mmap failed
        ssize_t written = pwrite(fd_, data, size, offset);
        if (written == -1) return std::unexpected(FileWriteError{strerror(errno), errno});
        if (static_cast<size_t>(written) != size) return std::unexpected(FileWriteError{"Incomplete write", -1});
        return {};
    }
    return std::unexpected(FileWriteError{"File not open", -1});
}

std::expected<void, FileWriteError> AsyncFileWriter::sync() {
    if (mmap_ptr_) {
        if (msync(mmap_ptr_, file_size_, MS_SYNC) == -1) {
            return std::unexpected(FileWriteError{strerror(errno), errno});
        }
    } else if (fd_ != -1) {
#ifdef __APPLE__
        if (fcntl(fd_, F_FULLFSYNC) == -1) return std::unexpected(FileWriteError{strerror(errno), errno});
#else
        if (fdatasync(fd_) == -1) return std::unexpected(FileWriteError{strerror(errno), errno});
#endif
    }
    return {};
}

} // namespace hfdown