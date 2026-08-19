// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file random_access_source.h
 * \brief Allocation-free positional byte-source contract for bounded readers.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Status returned directly by a host-provided positional read callback.
enum class RandomAccessIoCode : uint8_t {
    Ok,
    IoError,
    SourceChanged,
    Cancelled,
};

/// Result returned by a host-provided positional read callback.
struct RandomAccessIoResult final {
    RandomAccessIoCode code = RandomAccessIoCode::Ok;
    uint64_t bytes_read     = 0U;
};

/**
 * \brief Host callback for one synchronous positional read.
 *
 * `Ok` requires exactly `destination.size()` bytes. A callback that reaches a
 * premature end returns `Ok` with the actual shorter count; OpenMeta reports a
 * short read. If the source no longer has the size captured in
 * \ref RandomAccessSource, return `SourceChanged`.
 *
 * The callback must not throw. It may partially overwrite `destination` on
 * failure. OpenMeta never invokes one callback concurrently within a single
 * read operation.
 */
using RandomAccessReadAt
    = RandomAccessIoResult (*)(void* context, uint64_t offset,
                               std::span<std::byte> destination) noexcept;

/**
 * \brief Borrowed immutable random-access byte source.
 *
 * Exactly one backing is used for a non-empty source: `contiguous_data` or
 * `read_at`. OpenMeta does not own either backing or the callback context.
 * `concurrent_reads` is descriptive; callers may share a source between
 * independent OpenMeta operations only when it is true and the backing remains
 * immutable for the complete operation.
 */
struct RandomAccessSource final {
    uint64_t size                    = 0U;
    const std::byte* contiguous_data = nullptr;
    void* context                    = nullptr;
    RandomAccessReadAt read_at       = nullptr;
    bool concurrent_reads            = false;
};

/// OpenMeta result for an exact bounded source read.
enum class RandomAccessReadCode : uint8_t {
    Ok,
    InvalidArgument,
    OutOfRange,
    RequestTooLarge,
    RequestLimitExceeded,
    ByteLimitExceeded,
    ShortRead,
    IoError,
    SourceChanged,
    Cancelled,
    ContractViolation,
};

/// Per-operation source-I/O ceilings. A zero ceiling means unlimited.
struct RandomAccessReadLimits final {
    uint32_t max_requests          = 65536U;
    uint64_t max_total_bytes       = 64ULL * 1024ULL * 1024ULL;
    uint64_t max_single_read_bytes = 16ULL * 1024ULL * 1024ULL;
};

/**
 * \brief Per-operation accounting and first-failure diagnostics.
 *
 * State is sticky after failure. Use one state object per independent read or
 * decode operation; this keeps OpenMeta state local and permits concurrent
 * operations against a host-declared concurrent source.
 */
struct RandomAccessReadState final {
    RandomAccessReadCode code      = RandomAccessReadCode::Ok;
    RandomAccessIoCode io_code     = RandomAccessIoCode::Ok;
    uint32_t requests_issued       = 0U;
    uint64_t bytes_requested       = 0U;
    uint64_t bytes_completed       = 0U;
    uint64_t failure_offset        = 0U;
    uint64_t failure_request_bytes = 0U;
    uint64_t failure_bytes_read    = 0U;

    bool ok() const noexcept { return code == RandomAccessReadCode::Ok; }
};

/// Creates a non-owning source descriptor for caller-owned contiguous bytes.
RandomAccessSource
make_memory_random_access_source(std::span<const std::byte> bytes) noexcept;

/// Creates a source descriptor for a caller-owned positional read callback.
RandomAccessSource
make_callback_random_access_source(uint64_t size, void* context,
                                   RandomAccessReadAt read_at,
                                   bool concurrent_reads = false) noexcept;

/// Validates backing-selection and representability invariants.
bool
random_access_source_valid(const RandomAccessSource& source) noexcept;

/**
 * \brief Performs one exact, synchronous, range-checked source read.
 *
 * Limits are checked before touching the source. Request counters account for
 * issued memory copies and callback calls. The function performs no allocation
 * and does not retry a short read, because retry semantics belong to the host
 * source implementation.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
RandomAccessReadCode
random_access_read_exact(const RandomAccessSource& source, uint64_t offset,
                         std::span<std::byte> destination,
                         RandomAccessReadState* state,
                         const RandomAccessReadLimits& limits
                         = RandomAccessReadLimits {}) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
