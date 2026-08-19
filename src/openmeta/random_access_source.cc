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

}  // namespace openmeta
