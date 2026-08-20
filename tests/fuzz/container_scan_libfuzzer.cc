// SPDX-License-Identifier: Apache-2.0

#include "openmeta/container_scan.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>

namespace openmeta {

[[noreturn]] static void
fuzz_trap() noexcept
{
#if defined(__clang__) || defined(__GNUC__)
    __builtin_trap();
#else
    std::abort();
#endif
}


static void
verify_ranges(std::span<const std::byte> bytes,
              std::span<const ContainerBlockRef> blocks) noexcept
{
    const uint64_t size = static_cast<uint64_t>(bytes.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const ContainerBlockRef& b = blocks[i];
        if (b.outer_offset > size || b.outer_size > size
            || b.outer_offset + b.outer_size > size) {
            fuzz_trap();
        }
        if (b.data_offset > size || b.data_size > size
            || b.data_offset + b.data_size > size) {
            fuzz_trap();
        }
        if (b.data_offset < b.outer_offset) {
            fuzz_trap();
        }
        if (b.data_offset + b.data_size > b.outer_offset + b.outer_size) {
            fuzz_trap();
        }
    }
}


struct FuzzCallback final {
    std::span<const std::byte> bytes;
};


static RandomAccessIoResult
fuzz_read_at(void* context, uint64_t offset,
             std::span<std::byte> destination) noexcept
{
    FuzzCallback* callback = static_cast<FuzzCallback*>(context);
    if (offset > callback->bytes.size()
        || destination.size() > callback->bytes.size() - offset) {
        return RandomAccessIoResult { RandomAccessIoCode::Ok, 0U };
    }
    if (!destination.empty()) {
        std::memcpy(destination.data(), callback->bytes.data() + offset,
                    destination.size());
    }
    return RandomAccessIoResult { RandomAccessIoCode::Ok,
                                  static_cast<uint64_t>(destination.size()) };
}

}  // namespace openmeta

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    using namespace openmeta;

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               data),
                                           size);

    ContainerBlockRef blocks_buf[64] = {};
    const std::span<ContainerBlockRef> blocks(blocks_buf, 64);

    const ScanResult res = scan_auto(bytes, blocks);
    if (res.written > 64U) {
        fuzz_trap();
    }
    verify_ranges(bytes,
                  std::span<const ContainerBlockRef>(blocks_buf, res.written));

    FuzzCallback callback { bytes };
    const RandomAccessSource source
        = make_callback_random_access_source(bytes.size(), &callback,
                                             fuzz_read_at);
    const RandomAccessSourceRange range = make_random_access_source_range(
        source);
    std::byte read_window[512] = {};
    ContainerRandomAccessScratch scratch;
    scratch.read_window                   = read_window;
    ContainerBlockRef callback_blocks[64] = {};
    const ContainerRandomAccessScanResult callback_result
        = scan_jpeg_random_access(range, callback_blocks, scratch);
    if (callback_result.scan.written > 64U) {
        fuzz_trap();
    }
    verify_ranges(bytes, std::span<const ContainerBlockRef>(
                             callback_blocks, callback_result.scan.written));
    return 0;
}
