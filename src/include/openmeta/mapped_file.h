// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file mapped_file.h
 * \brief Read-only file mapping helper.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Status code for \ref MappedFile operations.
enum class MappedFileStatus : uint8_t {
    Ok,
    OpenFailed,
    StatFailed,
    TooLarge,
    MapFailed,
};

/**
 * \brief Read-only, whole-file memory mapping.
 *
 * This is a utility used by tools/bindings to avoid copying multi-GB files
 * into memory while still exposing a `std::span<const std::byte>` view that
 * OpenMeta's decoders can operate on.
 */
class MappedFile final {
public:
    MappedFile() noexcept;
    ~MappedFile() noexcept;

    MappedFile(const MappedFile&)            = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    /// Opens and maps \p path (read-only). \p max_file_bytes is a hard cap (0 = unlimited).
    MappedFileStatus open(const char* path,
                          uint64_t max_file_bytes = 0) noexcept;

    /// Unmaps/closes the file (idempotent).
    void close() noexcept;

    bool is_open() const noexcept;
    uint64_t size() const noexcept;
    std::span<const std::byte> bytes() const noexcept;

private:
#if defined(_WIN32)
    void* file_handle_ = nullptr;
    void* map_handle_  = nullptr;
#else
    int fd_ = -1;
#endif
    const std::byte* data_ = nullptr;
    uint64_t size_         = 0;
};

}  // namespace openmeta
OPENMETA_PUBLIC_END
