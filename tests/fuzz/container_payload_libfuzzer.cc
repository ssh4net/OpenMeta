// SPDX-License-Identifier: Apache-2.0

#include "openmeta/container_payload.h"
#include "openmeta/container_scan.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace openmeta {

struct PayloadFuzzCallback final {
    std::span<const std::byte> bytes;
};


static RandomAccessIoResult
payload_fuzz_read_at(void* context, uint64_t offset,
                     std::span<std::byte> destination) noexcept
{
    PayloadFuzzCallback* callback = static_cast<PayloadFuzzCallback*>(context);
    if (offset > callback->bytes.size()
        || destination.size() > callback->bytes.size() - offset) {
        return { RandomAccessIoCode::Ok, 0U };
    }
    if (!destination.empty()) {
        std::memcpy(destination.data(), callback->bytes.data() + offset,
                    destination.size());
    }
    return { RandomAccessIoCode::Ok,
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
    const ScanResult scan = scan_auto(bytes, blocks);

    std::byte out_buf[4096]    = {};
    std::byte read_window[512] = {};
    std::byte compressed[4096] = {};
    uint32_t scratch_buf[256]  = {};

    PayloadOptions opts;
    opts.decompress = false;
    PayloadFuzzCallback callback { bytes };
    const RandomAccessSource source
        = make_callback_random_access_source(bytes.size(), &callback,
                                             payload_fuzz_read_at);
    const RandomAccessSourceRange range = make_random_access_source_range(
        source);
    PayloadRandomAccessScratch random_scratch;
    random_scratch.read_window = read_window;
    random_scratch.compressed  = compressed;

    const uint32_t n = (scan.written < 64U) ? scan.written : 64U;
    for (uint32_t i = 0; i < n; ++i) {
        (void)extract_payload(bytes,
                              std::span<const ContainerBlockRef>(blocks_buf, n),
                              i, std::span<std::byte>(out_buf, sizeof(out_buf)),
                              std::span<uint32_t>(scratch_buf, 256), opts);
        (void)extract_payload_random_access(
            range, std::span<const ContainerBlockRef>(blocks_buf, n), i,
            std::span<std::byte>(out_buf, sizeof(out_buf)),
            std::span<uint32_t>(scratch_buf, 256), random_scratch, opts);
    }

    return 0;
}
