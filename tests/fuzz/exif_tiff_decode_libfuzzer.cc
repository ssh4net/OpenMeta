// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

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
    return 0;
}
