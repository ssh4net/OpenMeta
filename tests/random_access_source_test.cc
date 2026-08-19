// SPDX-License-Identifier: Apache-2.0

#include "openmeta/random_access_source.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <span>

namespace {

using namespace openmeta;

struct CallbackState final {
    std::span<const std::byte> bytes;
    RandomAccessIoCode code = RandomAccessIoCode::Ok;
    uint64_t max_bytes      = UINT64_MAX;
    uint64_t reported_extra = 0U;
    uint32_t calls          = 0U;
    uint64_t last_offset    = 0U;
    uint64_t last_size      = 0U;
};

static RandomAccessIoResult
test_read_at(void* context, uint64_t offset,
             std::span<std::byte> destination) noexcept
{
    CallbackState* state = static_cast<CallbackState*>(context);
    state->calls += 1U;
    state->last_offset = offset;
    state->last_size   = static_cast<uint64_t>(destination.size());

    uint64_t available = 0U;
    if (offset <= state->bytes.size()) {
        available = static_cast<uint64_t>(state->bytes.size()) - offset;
    }
    uint64_t count = static_cast<uint64_t>(destination.size());
    if (count > available) {
        count = available;
    }
    if (count > state->max_bytes) {
        count = state->max_bytes;
    }
    if (count != 0U) {
        std::memcpy(destination.data(),
                    state->bytes.data() + static_cast<size_t>(offset),
                    static_cast<size_t>(count));
    }

    RandomAccessIoResult result;
    result.code       = state->code;
    result.bytes_read = count + state->reported_extra;
    return result;
}

TEST(RandomAccessSource, MemorySourceReadsExactlyWithoutCallback)
{
    const std::array<std::byte, 8> bytes {
        std::byte { 0x10 }, std::byte { 0x11 }, std::byte { 0x12 },
        std::byte { 0x13 }, std::byte { 0x14 }, std::byte { 0x15 },
        std::byte { 0x16 }, std::byte { 0x17 },
    };
    const RandomAccessSource source = make_memory_random_access_source(bytes);
    ASSERT_TRUE(random_access_source_valid(source));
    EXPECT_TRUE(source.concurrent_reads);

    std::array<std::byte, 4> output {};
    RandomAccessReadState state;
    EXPECT_EQ(random_access_read_exact(source, 2U, output, &state),
              RandomAccessReadCode::Ok);
    EXPECT_EQ(output[0], std::byte { 0x12 });
    EXPECT_EQ(output[3], std::byte { 0x15 });
    EXPECT_EQ(state.requests_issued, 1U);
    EXPECT_EQ(state.bytes_requested, 4U);
    EXPECT_EQ(state.bytes_completed, 4U);

    std::span<std::byte> empty(output.data(), 0U);
    EXPECT_EQ(random_access_read_exact(source, bytes.size(), empty, &state),
              RandomAccessReadCode::Ok);
    EXPECT_EQ(state.requests_issued, 1U);
}

TEST(RandomAccessSource, RejectsInvalidBackingAndOutOfRangeBeforeRead)
{
    std::array<std::byte, 4> bytes {};
    CallbackState callback { bytes };

    RandomAccessSource invalid
        = make_callback_random_access_source(bytes.size(), &callback,
                                             test_read_at);
    invalid.contiguous_data = bytes.data();
    EXPECT_FALSE(random_access_source_valid(invalid));

    std::array<std::byte, 2> output {};
    RandomAccessReadState invalid_state;
    EXPECT_EQ(random_access_read_exact(invalid, 0U, output, &invalid_state),
              RandomAccessReadCode::InvalidArgument);
    EXPECT_EQ(callback.calls, 0U);

    const RandomAccessSource source
        = make_callback_random_access_source(bytes.size(), &callback,
                                             test_read_at);
    RandomAccessReadState range_state;
    EXPECT_EQ(random_access_read_exact(source, 3U, output, &range_state),
              RandomAccessReadCode::OutOfRange);
    EXPECT_EQ(range_state.failure_offset, 3U);
    EXPECT_EQ(range_state.failure_request_bytes, 2U);
    EXPECT_EQ(callback.calls, 0U);
}

TEST(RandomAccessSource, AcceptsEmptySourceAndRejectsMalformedAccounting)
{
    const RandomAccessSource empty;
    ASSERT_TRUE(random_access_source_valid(empty));

    std::array<std::byte, 1> output {};
    std::span<std::byte> no_bytes(output.data(), 0U);
    RandomAccessReadState empty_state;
    EXPECT_EQ(random_access_read_exact(empty, 0U, no_bytes, &empty_state),
              RandomAccessReadCode::Ok);
    EXPECT_EQ(empty_state.requests_issued, 0U);
    EXPECT_EQ(random_access_read_exact(empty, 0U, output, &empty_state),
              RandomAccessReadCode::OutOfRange);

    std::array<std::byte, 4> bytes {};
    const RandomAccessSource source = make_memory_random_access_source(bytes);
    EXPECT_EQ(random_access_read_exact(source, 0U, output, nullptr),
              RandomAccessReadCode::InvalidArgument);
    RandomAccessReadState malformed;
    malformed.bytes_completed = 1U;
    EXPECT_EQ(random_access_read_exact(source, 0U, output, &malformed),
              RandomAccessReadCode::InvalidArgument);

    RandomAccessReadLimits unlimited;
    unlimited.max_requests          = 0U;
    unlimited.max_total_bytes       = 0U;
    unlimited.max_single_read_bytes = 0U;
    RandomAccessReadState overflow;
    overflow.bytes_requested = UINT64_MAX;
    overflow.bytes_completed = UINT64_MAX;
    EXPECT_EQ(random_access_read_exact(source, 0U, output, &overflow, unlimited),
              RandomAccessReadCode::ByteLimitExceeded);
}

TEST(RandomAccessSource, DetectsShortReadAndKeepsFailureSticky)
{
    std::array<std::byte, 8> bytes {};
    CallbackState callback { bytes };
    callback.max_bytes = 2U;
    const RandomAccessSource source
        = make_callback_random_access_source(bytes.size(), &callback,
                                             test_read_at, true);

    std::array<std::byte, 4> output {};
    RandomAccessReadState state;
    EXPECT_EQ(random_access_read_exact(source, 1U, output, &state),
              RandomAccessReadCode::ShortRead);
    EXPECT_EQ(state.requests_issued, 1U);
    EXPECT_EQ(state.bytes_requested, 4U);
    EXPECT_EQ(state.bytes_completed, 2U);
    EXPECT_EQ(state.failure_bytes_read, 2U);
    EXPECT_EQ(callback.calls, 1U);

    callback.max_bytes = UINT64_MAX;
    EXPECT_EQ(random_access_read_exact(source, 1U, output, &state),
              RandomAccessReadCode::ShortRead);
    EXPECT_EQ(callback.calls, 1U);
}

TEST(RandomAccessSource, EnforcesRequestAndByteBudgetsBeforeCallbacks)
{
    std::array<std::byte, 16> bytes {};
    CallbackState callback { bytes };
    const RandomAccessSource source
        = make_callback_random_access_source(bytes.size(), &callback,
                                             test_read_at);
    std::array<std::byte, 4> output {};

    RandomAccessReadLimits limits;
    limits.max_requests          = 1U;
    limits.max_total_bytes       = 6U;
    limits.max_single_read_bytes = 4U;

    RandomAccessReadState request_state;
    ASSERT_EQ(random_access_read_exact(source, 0U, output, &request_state,
                                       limits),
              RandomAccessReadCode::Ok);
    EXPECT_EQ(random_access_read_exact(source, 4U, output, &request_state,
                                       limits),
              RandomAccessReadCode::RequestLimitExceeded);
    EXPECT_EQ(callback.calls, 1U);

    callback.calls = 0U;
    RandomAccessReadState byte_state;
    limits.max_requests = 4U;
    ASSERT_EQ(random_access_read_exact(source, 0U, output, &byte_state, limits),
              RandomAccessReadCode::Ok);
    EXPECT_EQ(random_access_read_exact(source, 4U, output, &byte_state, limits),
              RandomAccessReadCode::ByteLimitExceeded);
    EXPECT_EQ(callback.calls, 1U);

    callback.calls = 0U;
    RandomAccessReadState single_state;
    limits.max_total_bytes       = 16U;
    limits.max_single_read_bytes = 3U;
    EXPECT_EQ(random_access_read_exact(source, 0U, output, &single_state,
                                       limits),
              RandomAccessReadCode::RequestTooLarge);
    EXPECT_EQ(callback.calls, 0U);
}

TEST(RandomAccessSource, MapsIoFailuresAndRejectsCallbackContractViolations)
{
    std::array<std::byte, 8> bytes {};
    std::array<std::byte, 4> output {};
    const std::array<RandomAccessIoCode, 3> io_codes {
        RandomAccessIoCode::IoError,
        RandomAccessIoCode::SourceChanged,
        RandomAccessIoCode::Cancelled,
    };
    const std::array<RandomAccessReadCode, 3> read_codes {
        RandomAccessReadCode::IoError,
        RandomAccessReadCode::SourceChanged,
        RandomAccessReadCode::Cancelled,
    };

    for (size_t i = 0; i < io_codes.size(); ++i) {
        CallbackState callback { bytes };
        callback.code = io_codes[i];
        const RandomAccessSource source
            = make_callback_random_access_source(bytes.size(), &callback,
                                                 test_read_at);
        RandomAccessReadState state;
        EXPECT_EQ(random_access_read_exact(source, 0U, output, &state),
                  read_codes[i]);
        EXPECT_EQ(state.io_code, io_codes[i]);
        EXPECT_EQ(callback.calls, 1U);
    }

    CallbackState invalid_count { bytes };
    invalid_count.reported_extra = 1U;
    const RandomAccessSource invalid_source
        = make_callback_random_access_source(bytes.size(), &invalid_count,
                                             test_read_at);
    RandomAccessReadState invalid_state;
    EXPECT_EQ(random_access_read_exact(invalid_source, 0U, output,
                                       &invalid_state),
              RandomAccessReadCode::ContractViolation);
    EXPECT_EQ(invalid_state.failure_bytes_read, 5U);

    CallbackState invalid_code { bytes };
    invalid_code.code = static_cast<RandomAccessIoCode>(0xffU);
    const RandomAccessSource invalid_code_source
        = make_callback_random_access_source(bytes.size(), &invalid_code,
                                             test_read_at);
    RandomAccessReadState invalid_code_state;
    EXPECT_EQ(random_access_read_exact(invalid_code_source, 0U, output,
                                       &invalid_code_state),
              RandomAccessReadCode::ContractViolation);
}

TEST(RandomAccessSource, KeepsAccountingIndependentAcrossOperations)
{
    std::array<std::byte, 8> bytes {};
    CallbackState callback { bytes };
    const RandomAccessSource source
        = make_callback_random_access_source(bytes.size(), &callback,
                                             test_read_at, true);
    std::array<std::byte, 2> first {};
    std::array<std::byte, 3> second {};
    RandomAccessReadState first_state;
    RandomAccessReadState second_state;

    EXPECT_EQ(random_access_read_exact(source, 0U, first, &first_state),
              RandomAccessReadCode::Ok);
    EXPECT_EQ(random_access_read_exact(source, 2U, second, &second_state),
              RandomAccessReadCode::Ok);
    EXPECT_EQ(first_state.bytes_requested, 2U);
    EXPECT_EQ(second_state.bytes_requested, 3U);
    EXPECT_EQ(callback.calls, 2U);
}

TEST(RandomAccessSource, RangeTranslatesOffsetsAndRejectsInvalidBounds)
{
    std::array<std::byte, 16> bytes {};
    for (uint8_t i = 0U; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::byte>(i);
    }
    CallbackState callback { bytes };
    const RandomAccessSource source
        = make_callback_random_access_source(bytes.size(), &callback,
                                             test_read_at);
    const RandomAccessSourceRange range
        = make_random_access_source_range(source, 4U, 8U);
    ASSERT_TRUE(random_access_source_range_valid(range));

    std::array<std::byte, 3> output {};
    RandomAccessReadState state;
    EXPECT_EQ(random_access_read_exact(range, 2U, output, &state),
              RandomAccessReadCode::Ok);
    EXPECT_EQ(callback.last_offset, 6U);
    EXPECT_EQ(callback.last_size, 3U);
    EXPECT_EQ(output[0], std::byte { 6U });
    EXPECT_EQ(output[2], std::byte { 8U });

    const RandomAccessSourceRange invalid
        = make_random_access_source_range(source, 12U, 8U);
    EXPECT_FALSE(random_access_source_range_valid(invalid));
}

TEST(RandomAccessSource, WindowReadsAheadAndReturnsCacheHits)
{
    std::array<std::byte, 32> bytes {};
    for (uint8_t i = 0U; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::byte>(i);
    }
    CallbackState callback { bytes };
    const RandomAccessSource source
        = make_callback_random_access_source(bytes.size(), &callback,
                                             test_read_at);
    const RandomAccessSourceRange range
        = make_random_access_source_range(source, 4U, 20U);
    std::array<std::byte, 8> storage {};
    RandomAccessReadWindow window;
    window.storage = storage;
    RandomAccessReadWindowOptions options;
    options.minimum_read_bytes = storage.size();
    RandomAccessReadState state;

    const RandomAccessViewResult first
        = random_access_read_view(range, 2U, 2U, &window, &state,
                                  RandomAccessReadLimits {}, options);
    ASSERT_TRUE(first.ok());
    EXPECT_FALSE(first.cache_hit);
    ASSERT_EQ(first.bytes.size(), 2U);
    EXPECT_EQ(first.bytes[0], std::byte { 6U });
    EXPECT_EQ(callback.calls, 1U);
    EXPECT_EQ(callback.last_offset, 6U);
    EXPECT_EQ(callback.last_size, storage.size());

    const RandomAccessViewResult cached
        = random_access_read_view(range, 6U, 2U, &window, &state,
                                  RandomAccessReadLimits {}, options);
    ASSERT_TRUE(cached.ok());
    EXPECT_TRUE(cached.cache_hit);
    EXPECT_EQ(cached.bytes[0], std::byte { 10U });
    EXPECT_EQ(callback.calls, 1U);

    const RandomAccessViewResult refill
        = random_access_read_view(range, 12U, 2U, &window, &state,
                                  RandomAccessReadLimits {}, options);
    ASSERT_TRUE(refill.ok());
    EXPECT_FALSE(refill.cache_hit);
    EXPECT_EQ(callback.calls, 2U);
    EXPECT_EQ(callback.last_offset, 16U);
    EXPECT_EQ(callback.last_size, 8U);
}

TEST(RandomAccessSource, MemoryViewsAreDirectAndWindowFailuresAreBounded)
{
    std::array<std::byte, 12> bytes {};
    const RandomAccessSource source = make_memory_random_access_source(bytes);
    const RandomAccessSourceRange range
        = make_random_access_source_range(source, 2U, 8U);
    RandomAccessReadState memory_state;
    const RandomAccessViewResult direct
        = random_access_read_view(range, 3U, 2U, nullptr, &memory_state);
    ASSERT_TRUE(direct.ok());
    EXPECT_EQ(direct.bytes.data(), bytes.data() + 5U);
    EXPECT_EQ(memory_state.requests_issued, 0U);

    CallbackState callback { bytes };
    const RandomAccessSource callback_source
        = make_callback_random_access_source(bytes.size(), &callback,
                                             test_read_at);
    const RandomAccessSourceRange callback_range
        = make_random_access_source_range(callback_source);
    std::array<std::byte, 2> storage {};
    RandomAccessReadWindow window;
    window.storage = storage;
    RandomAccessReadState scratch_state;
    const RandomAccessViewResult too_small
        = random_access_read_view(callback_range, 0U, 3U, &window,
                                  &scratch_state);
    EXPECT_EQ(too_small.code, RandomAccessReadCode::ScratchTooSmall);
    EXPECT_EQ(callback.calls, 0U);
}

}  // namespace
