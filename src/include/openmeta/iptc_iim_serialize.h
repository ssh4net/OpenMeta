// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file iptc_iim_serialize.h
 * \brief Deterministic target-neutral IPTC-IIM serialization.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable canonical IPTC-IIM serialization contract version.
inline constexpr uint32_t kIptcIimSerializeContractVersion = 1U;

/// Stable result status for `serialize_iptc_iim()`.
enum class IptcIimSerializeStatus : uint8_t {
    Ok,
    OutputTruncated,
    InvalidOptions,
    StoreNotFinalized,
    InvalidMetadata,
    NoIptcData,
    LimitExceeded,
};

/// Resource limits for canonical IPTC-IIM serialization.
struct IptcIimSerializeOptions final {
    /// Maximum active IPTC datasets, including internally skipped entries.
    uint32_t max_datasets = 200000U;
    /// Maximum raw payload bytes in one dataset.
    uint32_t max_dataset_bytes = 8U * 1024U * 1024U;
    /// Maximum complete serialized stream bytes.
    uint64_t max_output_bytes = 64ULL * 1024ULL * 1024ULL;
};

/// Size, coverage, and failure details from canonical IPTC-IIM serialization.
struct IptcIimSerializeResult final {
    IptcIimSerializeStatus status = IptcIimSerializeStatus::Ok;
    uint64_t written              = 0U;
    uint64_t needed               = 0U;
    uint32_t entries_serialized   = 0U;
    uint32_t entries_skipped      = 0U;
    /// Store entry ordinal for a selected invalid or oversized dataset.
    EntryId failed_entry = kInvalidEntryId;

    bool ok() const noexcept { return status == IptcIimSerializeStatus::Ok; }
};

/**
 * \brief Serialize active IPTC-IIM datasets into an unwrapped byte stream.
 *
 * The store must be finalized and remain unchanged during the call. Only active
 * IPTC-IIM entries are selected; deleted entries and other key families are
 * ignored. Duplicates are preserved. The character-set dataset (1:90) sorts
 * first, followed by origin order, record, dataset, and original entry order
 * for ties.
 *
 * Record and dataset keys must fit in 8 bits. Byte and text values serialize
 * their exact stored bytes. Well-formed arrays also serialize their exact raw
 * payload bytes; they are not numerically encoded or endian-converted. Text
 * encoding hints do not transcode bytes or add a character-set marker. This
 * function does not validate IPTC field schemas, text semantics, or charsets.
 * Truncated or unreadable entries, invalid spans/counts/array types,
 * unsupported scalar/empty values, out-of-range keys, and limit violations
 * fail before output is changed. Output overlap with the store arena is an
 * invalid option.
 *
 * The default limits are 200,000 active datasets, 8 MiB per dataset, and 64 MiB
 * for the complete output. Options must be nonzero; `max_output_bytes` must
 * also fit in `uint32_t` and `size_t`.
 *
 * Call with an empty span to measure; a successful measurement returns
 * `OutputTruncated` with exact `needed` bytes. A short output span receives the
 * canonical prefix only after complete preflight. Bytes beyond `written` in a
 * larger output span stay unchanged. Other failures leave output unchanged and report zero `written`
 * and `needed`. Preparation can allocate bounded scratch and payload storage;
 * allocation failure is not a recoverable serializer status. The host must
 * synchronize all store mutation.
 */
IptcIimSerializeResult
serialize_iptc_iim(const MetaStore& store, std::span<std::byte> output,
                   const IptcIimSerializeOptions& options
                   = IptcIimSerializeOptions {}) noexcept;

const char*
iptc_iim_serialize_status_name(IptcIimSerializeStatus status) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
