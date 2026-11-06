// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/container_scan.h"
#include "openmeta/meta_store.h"
#include "openmeta/random_access_source.h"

#include <cstddef>
#include <span>

namespace openmeta {

// Internal-only (non-installed) helpers for ISO-BMFF (HEIF/AVIF/CR3) container
// derived fields. This intentionally does not expose a public API yet; the
// values are surfaced via simple_meta_read().

namespace bmff_internal {

    void decode_bmff_derived_fields(std::span<const std::byte> file_bytes,
                                    MetaStore& store) noexcept;

    // Shared CR3 payload interpretation. CTBO validation reads only declared
    // target box headers (at most 32 bytes each), never media/preview bodies.
    // False means opaque/invalid; a source failure remains in input.
    bool decode_cr3_metadata_block(
        const RandomAccessSourceRange& source, const ContainerBlockRef& block,
        std::span<const std::byte> payload, MetaStore& store,
        RandomAccessReadState* input,
        const RandomAccessReadLimits& limits = {}) noexcept;

}  // namespace bmff_internal

}  // namespace openmeta

