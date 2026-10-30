// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/iptc_iim_serialize.h"

#include <cstdint>
#include <vector>

namespace openmeta::detail {

// Capture entries must be sorted by unique source entry_id. The writer fills
// each value offset/width and sets emitted before publishing any output.
struct IptcIimPatchCapture final {
    EntryId entry_id       = kInvalidEntryId;
    uint32_t request_index = 0U;
    uint32_t byte_offset   = 0U;
    uint32_t byte_width    = 0U;
    bool emitted           = false;
};

// Reads entry and arena storage directly; callers must prevent concurrent
// mutation. A non-null capture requires a non-null output. Output and capture
// are published only after every selected entry was emitted. Finalization is
// not required by this internal transfer path.
IptcIimSerializeResult
build_iptc_iim_payload(const MetaStore& store,
                       const IptcIimSerializeOptions& options,
                       bool skip_unsupported, std::vector<std::byte>* output,
                       std::vector<IptcIimPatchCapture>* capture
                       = nullptr) noexcept;

}  // namespace openmeta::detail
