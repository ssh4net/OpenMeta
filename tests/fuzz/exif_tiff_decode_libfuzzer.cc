// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"
#include "openmeta/metadata_query.h"
#include "../../src/openmeta/raf_decode_internal.h"
#include "../../src/openmeta/mrw_decode_internal.h"
#include <cstring>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {
struct Input { std::span<const std::byte> bytes; };
static openmeta::RandomAccessIoResult read_at(void* context, uint64_t offset,
                                             std::span<std::byte> out) noexcept
{
    const auto& input = *static_cast<const Input*>(context);
    if (offset > input.bytes.size() || out.size() > input.bytes.size() - offset)
        return {openmeta::RandomAccessIoCode::Ok, 0U};
    std::memcpy(out.data(), input.bytes.data() + offset, out.size());
    return {openmeta::RandomAccessIoCode::Ok, out.size()};
}
}
extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    using namespace openmeta;

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               data),
                                           size);

    MetaStore store;

    ExifDecodeOptions options;
    options.decode_makernote           = true;
    options.include_pointer_tags       = true;
    options.limits.max_ifds            = 64;
    options.limits.max_entries_per_ifd = 512;
    options.limits.max_total_entries   = 4096;
    options.limits.max_value_bytes     = 1ULL * 1024ULL * 1024ULL;

    std::array<ExifIfdRef, 64> ifds {};
    (void)decode_exif_tiff(bytes, store, ifds, options);
    store.finalize();
    MetadataQueryOptions query;
    query.max_values_per_entry = 256U;
    query.max_values_per_candidate = 1024U;
    query.max_total_values = 4096U;
    (void)query_raw_processing_metadata(store, query);
    (void)query_color_metadata(store, query);

    Input input { bytes };
    const auto source = make_callback_random_access_source(bytes.size(), &input, read_at);
    const auto range = make_random_access_source_range(source);
    std::array<std::byte, 64> window {};
    std::array<std::byte, 65536> values {};
    ExifRandomAccessScratch scratch;
    scratch.read_window = window;
    scratch.value = values;
    scratch.window_options.minimum_read_bytes = 0U;
    RandomAccessReadLimits reads;
    reads.max_requests = 4096U;
    reads.max_total_bytes = 2U * 1024U * 1024U;
    MetaStore callback;
    (void)decode_exif_tiff_random_access(range, callback, {}, scratch, options, reads);
    callback.finalize();
    (void)query_raw_processing_metadata(callback, query);
    if (raf_internal::looks_like_raf(bytes)) {
        MetaStore native;
        (void)raf_internal::decode_raf_native(bytes, native, options.limits);
        native.finalize();
        (void)query_raw_processing_metadata(native, query);
        MetaStore positional;
        (void)raf_internal::decode_raf_native_random_access(range, positional, scratch,
                                                           options.limits, reads);
    }
    if (mrw_internal::looks_like_mrw(bytes)) {
        MetaStore native;
        (void)mrw_internal::decode_mrw_native(bytes, native, options.limits);
        MetaStore positional;
        (void)mrw_internal::decode_mrw_native_random_access(range, positional,
            scratch, options.limits, reads);
    }
    return 0;
}
