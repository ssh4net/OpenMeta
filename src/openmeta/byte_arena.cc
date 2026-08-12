// SPDX-License-Identifier: Apache-2.0

#include "openmeta/byte_arena.h"

#include <cstring>

namespace openmeta {

static bool
checked_add_size(size_t a, size_t b, size_t* out) noexcept
{
    if (!out) {
        return false;
    }
    if (a > (SIZE_MAX - b)) {
        return false;
    }
    *out = a + b;
    return true;
}


static bool
align_up_size(size_t value, uint32_t alignment, size_t* out) noexcept
{
    if (!out) {
        return false;
    }
    if (alignment <= 1U) {
        *out = value;
        return true;
    }
    const size_t mask = static_cast<size_t>(alignment - 1U);
    size_t aligned    = 0;
    if (!checked_add_size(value, mask, &aligned)) {
        return false;
    }
    aligned = aligned & ~mask;
    *out    = aligned;
    return true;
}


void
ByteArena::clear() noexcept
{
    buffer_.clear();
    limit_exceeded_ = false;
}


void
ByteArena::reserve(size_t size_bytes)
{
    if (size_bytes > max_size_bytes_) {
        limit_exceeded_ = true;
        return;
    }
    buffer_.reserve(size_bytes);
}


void
ByteArena::constrain_max_size(uint64_t max_size_bytes) noexcept
{
    if (max_size_bytes != 0U && max_size_bytes < max_size_bytes_) {
        max_size_bytes_ = max_size_bytes;
    }
    if (buffer_.size() > max_size_bytes_) {
        limit_exceeded_ = true;
    }
}


bool
ByteArena::limit_exceeded() const noexcept
{
    return limit_exceeded_;
}


ByteSpan
ByteArena::append(std::span<const std::byte> bytes)
{
    const size_t old_size = buffer_.size();
    if (old_size > static_cast<size_t>(UINT32_MAX)
        || bytes.size() > static_cast<size_t>(UINT32_MAX)) {
        limit_exceeded_ = true;
        return ByteSpan {};
    }
    size_t new_size = 0;
    if (!checked_add_size(old_size, bytes.size(), &new_size)
        || new_size > static_cast<size_t>(UINT32_MAX)
        || new_size > max_size_bytes_) {
        limit_exceeded_ = true;
        return ByteSpan {};
    }

    const uint32_t offset       = static_cast<uint32_t>(old_size);
    const std::byte* const base = buffer_.empty() ? nullptr : buffer_.data();

    bool aliases_buffer = false;
    size_t src_offset   = 0;
    if (!bytes.empty() && base) {
        const std::byte* const src = bytes.data();
        if (src >= base && src < (base + old_size)
            && bytes.size() <= static_cast<size_t>((base + old_size) - src)) {
            aliases_buffer = true;
            src_offset     = static_cast<size_t>(src - base);
        }
    }

    buffer_.resize(new_size);
    if (!bytes.empty()) {
        const std::byte* const src = aliases_buffer
                                         ? (buffer_.data() + src_offset)
                                         : bytes.data();
        std::memmove(buffer_.data() + offset, src, bytes.size());
    }
    return ByteSpan { offset, static_cast<uint32_t>(bytes.size()) };
}


ByteSpan
ByteArena::append_string(std::string_view text)
{
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               text.data()),
                                           text.size());
    return append(bytes);
}


ByteSpan
ByteArena::allocate(uint32_t size_bytes, uint32_t alignment)
{
    const size_t old_size = buffer_.size();
    if (old_size > static_cast<size_t>(UINT32_MAX)) {
        limit_exceeded_ = true;
        return ByteSpan {};
    }
    size_t start = 0;
    if (!align_up_size(old_size, alignment, &start)
        || start > static_cast<size_t>(UINT32_MAX)) {
        limit_exceeded_ = true;
        return ByteSpan {};
    }
    size_t total_size = 0;
    if (!checked_add_size(start, static_cast<size_t>(size_bytes), &total_size)
        || total_size > static_cast<size_t>(UINT32_MAX)
        || total_size > max_size_bytes_) {
        limit_exceeded_ = true;
        return ByteSpan {};
    }
    const uint32_t offset = static_cast<uint32_t>(start);
    buffer_.resize(total_size, std::byte { 0 });
    return ByteSpan { offset, size_bytes };
}


std::span<const std::byte>
ByteArena::bytes() const noexcept
{
    return std::span<const std::byte>(buffer_.data(), buffer_.size());
}


std::span<std::byte>
ByteArena::bytes_mut() noexcept
{
    return std::span<std::byte>(buffer_.data(), buffer_.size());
}


std::span<const std::byte>
ByteArena::span(ByteSpan view) const noexcept
{
    const std::span<const std::byte> all = bytes();
    if (view.offset > all.size()) {
        return std::span<const std::byte>();
    }
    const size_t end = static_cast<size_t>(view.offset) + view.size;
    if (end > all.size()) {
        return std::span<const std::byte>();
    }
    return all.subspan(view.offset, view.size);
}


std::span<std::byte>
ByteArena::span_mut(ByteSpan view) noexcept
{
    const std::span<std::byte> all = bytes_mut();
    if (view.offset > all.size()) {
        return std::span<std::byte>();
    }
    const size_t end = static_cast<size_t>(view.offset) + view.size;
    if (end > all.size()) {
        return std::span<std::byte>();
    }
    return all.subspan(view.offset, view.size);
}

}  // namespace openmeta
