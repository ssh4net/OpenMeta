// SPDX-License-Identifier: Apache-2.0

#include "openmeta/random_access_source.h"

#include <cstring>
#include <limits>

namespace openmeta {
namespace {

    static RandomAccessReadCode
    fail(RandomAccessReadState* state, RandomAccessReadCode code,
         uint64_t offset, uint64_t request_bytes,
         RandomAccessIoCode io_code = RandomAccessIoCode::Ok,
         uint64_t bytes_read        = 0U) noexcept
    {
        if (state != nullptr && state->code == RandomAccessReadCode::Ok) {
            state->code                  = code;
            state->io_code               = io_code;
            state->failure_offset        = offset;
            state->failure_request_bytes = request_bytes;
            state->failure_bytes_read    = bytes_read;
        }
        return code;
    }

    static bool is_known_io_code(RandomAccessIoCode code) noexcept
    {
        switch (code) {
        case RandomAccessIoCode::Ok:
        case RandomAccessIoCode::IoError:
        case RandomAccessIoCode::SourceChanged:
        case RandomAccessIoCode::Cancelled: return true;
        }
        return false;
    }

}  // namespace

RandomAccessSource
make_memory_random_access_source(std::span<const std::byte> bytes) noexcept
{
    RandomAccessSource source;
    source.size             = static_cast<uint64_t>(bytes.size());
    source.contiguous_data  = bytes.data();
    source.concurrent_reads = true;
    return source;
}

RandomAccessSource
make_callback_random_access_source(uint64_t size, void* context,
                                   RandomAccessReadAt read_at,
                                   bool concurrent_reads) noexcept
{
    RandomAccessSource source;
    source.size             = size;
    source.context          = context;
    source.read_at          = read_at;
    source.concurrent_reads = concurrent_reads;
    return source;
}

bool
random_access_source_valid(const RandomAccessSource& source) noexcept
{
    const bool has_memory   = source.contiguous_data != nullptr;
    const bool has_callback = source.read_at != nullptr;
    if (has_memory && has_callback) {
        return false;
    }
    if (source.size != 0U && !has_memory && !has_callback) {
        return false;
    }
    if (has_memory
        && source.size
               > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return false;
    }
    return true;
}

RandomAccessSourceRange
make_random_access_source_range(const RandomAccessSource& source) noexcept
{
    RandomAccessSourceRange range;
    range.source = source;
    range.size   = source.size;
    return range;
}

RandomAccessSourceRange
make_random_access_source_range(const RandomAccessSource& source,
                                uint64_t source_offset, uint64_t size) noexcept
{
    RandomAccessSourceRange range;
    range.source        = source;
    range.source_offset = source_offset;
    range.size          = size;
    return range;
}

bool
random_access_source_range_valid(const RandomAccessSourceRange& range) noexcept
{
    return random_access_source_valid(range.source)
           && range.source_offset <= range.source.size
           && range.size <= range.source.size - range.source_offset;
}

RandomAccessReadCode
random_access_read_exact(const RandomAccessSource& source, uint64_t offset,
                         std::span<std::byte> destination,
                         RandomAccessReadState* state,
                         const RandomAccessReadLimits& limits) noexcept
{
    const uint64_t request_bytes = static_cast<uint64_t>(destination.size());
    if (state == nullptr) {
        return RandomAccessReadCode::InvalidArgument;
    }
    if (state->code != RandomAccessReadCode::Ok) {
        return state->code;
    }
    if (state->bytes_completed > state->bytes_requested) {
        return fail(state, RandomAccessReadCode::InvalidArgument, offset,
                    request_bytes);
    }
    if (!random_access_source_valid(source)
        || (request_bytes != 0U && destination.data() == nullptr)) {
        return fail(state, RandomAccessReadCode::InvalidArgument, offset,
                    request_bytes);
    }
    if (offset > source.size || request_bytes > source.size - offset) {
        return fail(state, RandomAccessReadCode::OutOfRange, offset,
                    request_bytes);
    }
    if (request_bytes == 0U) {
        return RandomAccessReadCode::Ok;
    }
    if (limits.max_single_read_bytes != 0U
        && request_bytes > limits.max_single_read_bytes) {
        return fail(state, RandomAccessReadCode::RequestTooLarge, offset,
                    request_bytes);
    }
    if (limits.max_requests != 0U
        && state->requests_issued >= limits.max_requests) {
        return fail(state, RandomAccessReadCode::RequestLimitExceeded, offset,
                    request_bytes);
    }
    if (state->requests_issued == UINT32_MAX) {
        return fail(state, RandomAccessReadCode::RequestLimitExceeded, offset,
                    request_bytes);
    }
    if (state->bytes_requested > UINT64_MAX - request_bytes) {
        return fail(state, RandomAccessReadCode::ByteLimitExceeded, offset,
                    request_bytes);
    }
    const uint64_t next_requested = state->bytes_requested + request_bytes;
    if (limits.max_total_bytes != 0U
        && next_requested > limits.max_total_bytes) {
        return fail(state, RandomAccessReadCode::ByteLimitExceeded, offset,
                    request_bytes);
    }

    state->requests_issued += 1U;
    state->bytes_requested = next_requested;

    if (source.contiguous_data != nullptr) {
        const size_t memory_offset = static_cast<size_t>(offset);
        std::memmove(destination.data(), source.contiguous_data + memory_offset,
                     destination.size());
        state->bytes_completed += request_bytes;
        return RandomAccessReadCode::Ok;
    }

    const RandomAccessIoResult io = source.read_at(source.context, offset,
                                                   destination);
    if (!is_known_io_code(io.code) || io.bytes_read > request_bytes) {
        return fail(state, RandomAccessReadCode::ContractViolation, offset,
                    request_bytes, io.code, io.bytes_read);
    }
    state->bytes_completed += io.bytes_read;

    switch (io.code) {
    case RandomAccessIoCode::Ok:
        if (io.bytes_read != request_bytes) {
            return fail(state, RandomAccessReadCode::ShortRead, offset,
                        request_bytes, io.code, io.bytes_read);
        }
        return RandomAccessReadCode::Ok;
    case RandomAccessIoCode::IoError:
        return fail(state, RandomAccessReadCode::IoError, offset, request_bytes,
                    io.code, io.bytes_read);
    case RandomAccessIoCode::SourceChanged:
        return fail(state, RandomAccessReadCode::SourceChanged, offset,
                    request_bytes, io.code, io.bytes_read);
    case RandomAccessIoCode::Cancelled:
        return fail(state, RandomAccessReadCode::Cancelled, offset,
                    request_bytes, io.code, io.bytes_read);
    }

    return fail(state, RandomAccessReadCode::ContractViolation, offset,
                request_bytes, io.code, io.bytes_read);
}

RandomAccessReadCode
random_access_read_exact(const RandomAccessSourceRange& range, uint64_t offset,
                         std::span<std::byte> destination,
                         RandomAccessReadState* state,
                         const RandomAccessReadLimits& limits) noexcept
{
    const uint64_t request_bytes = static_cast<uint64_t>(destination.size());
    if (state == nullptr) {
        return RandomAccessReadCode::InvalidArgument;
    }
    if (state->code != RandomAccessReadCode::Ok) {
        return state->code;
    }
    if (!random_access_source_range_valid(range)) {
        return fail(state, RandomAccessReadCode::InvalidArgument, offset,
                    request_bytes);
    }
    if (offset > range.size || request_bytes > range.size - offset) {
        return fail(state, RandomAccessReadCode::OutOfRange, offset,
                    request_bytes);
    }
    const uint64_t absolute_offset = range.source_offset + offset;
    return random_access_read_exact(range.source, absolute_offset, destination,
                                    state, limits);
}

RandomAccessViewResult
random_access_read_view(const RandomAccessSourceRange& range, uint64_t offset,
                        uint64_t size, RandomAccessReadWindow* window,
                        RandomAccessReadState* state,
                        const RandomAccessReadLimits& limits,
                        const RandomAccessReadWindowOptions& options) noexcept
{
    RandomAccessViewResult result;
    if (state == nullptr) {
        result.code = RandomAccessReadCode::InvalidArgument;
        return result;
    }
    if (state->code != RandomAccessReadCode::Ok) {
        result.code = state->code;
        return result;
    }
    if (!random_access_source_range_valid(range)) {
        result.code = fail(state, RandomAccessReadCode::InvalidArgument, offset,
                           size);
        return result;
    }
    if (offset > range.size || size > range.size - offset
        || size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        result.code = fail(state, RandomAccessReadCode::OutOfRange, offset,
                           size);
        return result;
    }

    if (range.source.contiguous_data != nullptr) {
        const uint64_t absolute_offset = range.source_offset + offset;
        result.bytes                   = std::span<const std::byte>(
            range.source.contiguous_data + static_cast<size_t>(absolute_offset),
            static_cast<size_t>(size));
        return result;
    }

    if (size == 0U) {
        return result;
    }
    if (window == nullptr || window->storage.empty()
        || size > static_cast<uint64_t>(window->storage.size())) {
        result.code = fail(state, RandomAccessReadCode::ScratchTooSmall, offset,
                           size);
        return result;
    }

    if (window->valid && offset >= window->range_offset) {
        const uint64_t delta = offset - window->range_offset;
        if (delta <= window->valid_bytes
            && size <= window->valid_bytes - delta) {
            result.bytes
                = std::span<const std::byte>(window->storage.data()
                                                 + static_cast<size_t>(delta),
                                             static_cast<size_t>(size));
            result.cache_hit = true;
            return result;
        }
    }

    uint64_t fetch_bytes = size;
    if (fetch_bytes < options.minimum_read_bytes) {
        fetch_bytes = options.minimum_read_bytes;
    }
    const uint64_t storage_bytes = static_cast<uint64_t>(
        window->storage.size());
    if (fetch_bytes > storage_bytes) {
        fetch_bytes = storage_bytes;
    }
    const uint64_t range_remaining = range.size - offset;
    if (fetch_bytes > range_remaining) {
        fetch_bytes = range_remaining;
    }
    if (limits.max_single_read_bytes != 0U
        && fetch_bytes > limits.max_single_read_bytes) {
        fetch_bytes = limits.max_single_read_bytes;
    }
    if (limits.max_total_bytes != 0U
        && state->bytes_requested < limits.max_total_bytes) {
        const uint64_t budget_remaining = limits.max_total_bytes
                                          - state->bytes_requested;
        if (fetch_bytes > budget_remaining) {
            fetch_bytes = budget_remaining;
        }
    }
    if (fetch_bytes < size) {
        result.code = fail(state, RandomAccessReadCode::ByteLimitExceeded,
                           offset, size);
        return result;
    }

    window->reset();
    const std::span<std::byte> destination = window->storage.first(
        static_cast<size_t>(fetch_bytes));
    result.code = random_access_read_exact(range, offset, destination, state,
                                           limits);
    if (result.code != RandomAccessReadCode::Ok) {
        return result;
    }
    window->range_offset = offset;
    window->valid_bytes  = fetch_bytes;
    window->valid        = true;
    result.bytes         = std::span<const std::byte>(window->storage.data(),
                                                      static_cast<size_t>(size));
    return result;
}

}  // namespace openmeta
