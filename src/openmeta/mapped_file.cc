// SPDX-License-Identifier: Apache-2.0

#include "openmeta/mapped_file.h"

#include <cstddef>
#include <cstdint>
#include <limits>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <sys/mman.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

namespace openmeta {

MappedFile::MappedFile() noexcept = default;


MappedFile::~MappedFile() noexcept
{
    close();
}


MappedFile::MappedFile(MappedFile&& other) noexcept
{
    *this = std::move(other);
}


MappedFile&
MappedFile::operator=(MappedFile&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    close();

#if defined(_WIN32)
    file_handle_       = other.file_handle_;
    map_handle_        = other.map_handle_;
    other.file_handle_ = nullptr;
    other.map_handle_  = nullptr;
#else
    fd_       = other.fd_;
    other.fd_ = -1;
#endif

    data_       = other.data_;
    size_       = other.size_;
    other.data_ = nullptr;
    other.size_ = 0;
    return *this;
}


MappedFileStatus
MappedFile::open(const char* path, uint64_t max_file_bytes) noexcept
{
    close();

    if (!path || !*path) {
        return MappedFileStatus::OpenFailed;
    }

#if defined(_WIN32)
    HANDLE h = ::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return MappedFileStatus::OpenFailed;
    }
    if (::GetFileType(h) != FILE_TYPE_DISK) {
        ::CloseHandle(h);
        return MappedFileStatus::OpenFailed;
    }

    LARGE_INTEGER sz;
    if (!::GetFileSizeEx(h, &sz) || sz.QuadPart < 0) {
        ::CloseHandle(h);
        return MappedFileStatus::StatFailed;
    }

    const uint64_t size_u64 = static_cast<uint64_t>(sz.QuadPart);
    if (max_file_bytes != 0U && size_u64 > max_file_bytes) {
        ::CloseHandle(h);
        return MappedFileStatus::TooLarge;
    }

    if (size_u64 > static_cast<uint64_t>(
                       std::numeric_limits<size_t>::max())) {
        ::CloseHandle(h);
        return MappedFileStatus::TooLarge;
    }

    HANDLE map = nullptr;
    const DWORD size_high = static_cast<DWORD>((size_u64 >> 32) & 0xFFFFFFFFu);
    const DWORD size_low  = static_cast<DWORD>((size_u64 >> 0) & 0xFFFFFFFFu);
    if (size_u64 != 0U) {
        map = ::CreateFileMappingA(h, nullptr, PAGE_READONLY, size_high,
                                   size_low, nullptr);
        if (!map) {
            ::CloseHandle(h);
            return MappedFileStatus::MapFailed;
        }
        void* p = ::MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
        if (!p) {
            ::CloseHandle(map);
            ::CloseHandle(h);
            return MappedFileStatus::MapFailed;
        }
        data_ = static_cast<const std::byte*>(p);
    }

    file_handle_ = static_cast<void*>(h);
    map_handle_  = static_cast<void*>(map);
    size_        = size_u64;
    return MappedFileStatus::Ok;
#else
    const int fd = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return MappedFileStatus::OpenFailed;
    }

    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        return MappedFileStatus::StatFailed;
    }
    if (!S_ISREG(st.st_mode)) {
        ::close(fd);
        return MappedFileStatus::OpenFailed;
    }
    if (st.st_size < 0) {
        ::close(fd);
        return MappedFileStatus::StatFailed;
    }

    const uint64_t size_u64 = static_cast<uint64_t>(st.st_size);
    if (max_file_bytes != 0U && size_u64 > max_file_bytes) {
        ::close(fd);
        return MappedFileStatus::TooLarge;
    }

    if (size_u64 > static_cast<uint64_t>(
                       std::numeric_limits<size_t>::max())) {
        ::close(fd);
        return MappedFileStatus::TooLarge;
    }

    if (size_u64 != 0U) {
        void* p = ::mmap(nullptr, static_cast<size_t>(size_u64), PROT_READ,
                         MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) {
            ::close(fd);
            return MappedFileStatus::MapFailed;
        }
        data_ = static_cast<const std::byte*>(p);
    }

    fd_   = fd;
    size_ = size_u64;
    return MappedFileStatus::Ok;
#endif
}


void
MappedFile::close() noexcept
{
#if defined(_WIN32)
    if (data_) {
        ::UnmapViewOfFile(const_cast<void*>(
            static_cast<const void*>(data_)));
    }
    if (map_handle_) {
        ::CloseHandle(static_cast<HANDLE>(map_handle_));
    }
    if (file_handle_) {
        ::CloseHandle(static_cast<HANDLE>(file_handle_));
    }
    file_handle_ = nullptr;
    map_handle_  = nullptr;
#else
    if (data_ && size_ != 0U) {
        (void)::munmap(const_cast<void*>(
                           static_cast<const void*>(data_)),
                       static_cast<size_t>(size_));
    }
    if (fd_ >= 0) {
        (void)::close(fd_);
    }
    fd_ = -1;
#endif

    data_ = nullptr;
    size_ = 0;
}


bool
MappedFile::is_open() const noexcept
{
#if defined(_WIN32)
    return file_handle_ != nullptr;
#else
    return fd_ >= 0;
#endif
}


uint64_t
MappedFile::size() const noexcept
{
    return size_;
}


std::span<const std::byte>
MappedFile::bytes() const noexcept
{
    if (size_ == 0U) {
        return {};
    }
    return std::span<const std::byte>(
        data_, static_cast<size_t>(size_));
}

}  // namespace openmeta

