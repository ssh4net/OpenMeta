// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

/**
 * \file byte_arena.h
 * \brief Append-only byte arena used to store metadata payloads and strings.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// A span (offset,size) into a \ref ByteArena buffer.
struct ByteSpan final {
    uint32_t offset = 0;
    uint32_t size   = 0;
};

/**
 * \brief Append-only storage for bytes and strings.
 *
 * \note \ref ByteSpan values remain meaningful as long as the arena content is
 * not cleared. However, any pointer/span returned by \ref span() may be
 * invalidated by subsequent arena growth (vector reallocation). Do not retain
 * the returned span across arena mutations.
 */
class ByteArena final {
public:
    ByteArena() = default;

    /// Discards all stored bytes.
    void clear() noexcept;
    /// Reserves at least \p size_bytes capacity (may allocate).
    void reserve(size_t size_bytes);

    /// Appends raw bytes and returns a \ref ByteSpan to the stored copy.
    /// Returns an empty span if the payload would exceed \ref ByteSpan limits.
    /// Source bytes may alias the current arena buffer.
    ByteSpan append(std::span<const std::byte> bytes);
    /// Appends the raw bytes of \p text (no terminator) and returns a span.
    /// Returns an empty span if the payload would exceed \ref ByteSpan limits.
    ByteSpan append_string(std::string_view text);
    /// Allocates \p size_bytes with \p alignment and returns the written span.
    /// Returns an empty span if the allocation cannot be represented.
    ByteSpan allocate(uint32_t size_bytes, uint32_t alignment);

    /// Returns a view of the full buffer.
    std::span<const std::byte> bytes() const noexcept;
    /// Returns a mutable view of the full buffer.
    std::span<std::byte> bytes_mut() noexcept;
    /// Returns a view for \p view, or an empty span if out of range.
    std::span<const std::byte> span(ByteSpan view) const noexcept;
    /// Returns a mutable view for \p view, or an empty span if out of range.
    std::span<std::byte> span_mut(ByteSpan view) noexcept;

private:
    std::vector<std::byte> buffer_;
};

}  // namespace openmeta
OPENMETA_PUBLIC_END
