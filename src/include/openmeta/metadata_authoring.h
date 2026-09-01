// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"
#include "openmeta/validate.h"

#include <cstdint>
#include <span>

/**
 * \file metadata_authoring.h
 * \brief Transactional construction of generic typed metadata stores.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable generic typed authoring contract version.
inline constexpr uint32_t kMetadataAuthoringContractVersion = 1U;
inline constexpr uint32_t kInvalidMetadataAuthoringEntry    = 0xffffffffU;

/// Handling of repeated explicit keys during generic store construction.
enum class MetadataAuthoringDuplicatePolicy : uint8_t {
    Allow,
    RejectExactKeys,
};

/// One borrowed, ordered entry supplied to `create_metadata_store()`.
struct MetadataAuthoringEntry final {
    MetaKeyView key;
    MetaValueView value;
    WireType wire_type;
    uint32_t wire_count = 0U;
};

/// Resource and validation policy for generic metadata construction.
struct MetadataAuthoringOptions final {
    uint32_t max_entries     = 200000U;
    uint64_t max_arena_bytes = 64ULL * 1024ULL * 1024ULL;
    uint64_t max_value_bytes = 64ULL * 1024ULL * 1024ULL;
    uint32_t max_key_bytes   = 4096U;
    MetadataAuthoringDuplicatePolicy duplicate_policy
        = MetadataAuthoringDuplicatePolicy::Allow;
    bool validate = true;
    MetadataValidationOptions validation;
    BlockInfo block;
};

/// Stable result status for `create_metadata_store()`.
enum class MetadataAuthoringStatus : uint8_t {
    Ok,
    NullOutput,
    InvalidOptions,
    TooManyEntries,
    UnsupportedKeyKind,
    InvalidKey,
    InvalidValue,
    LimitExceeded,
    DuplicateKey,
    ValidationFailed,
};

/// Transactional generic metadata construction result.
struct MetadataAuthoringResult final {
    MetadataAuthoringStatus status = MetadataAuthoringStatus::Ok;
    uint32_t entries_created       = 0U;
    uint32_t failed_entry          = kInvalidMetadataAuthoringEntry;
    MetadataValidationIssueCode validation_issue
        = MetadataValidationIssueCode::None;

    bool ok() const noexcept { return status == MetadataAuthoringStatus::Ok; }
};

/**
 * \brief Build a finalized detached store from borrowed typed entries.
 *
 * Authorable key families are EXIF/TIFF tags, XMP properties, and IPTC-IIM
 * datasets. Unknown/private EXIF tags and custom XMP namespace URIs are
 * accepted without registry changes. All borrowed keys and values are copied.
 * The output store is replaced only after the complete request passes resource,
 * structural, and enabled schema validation.
 */
MetadataAuthoringResult
create_metadata_store(std::span<const MetadataAuthoringEntry> entries,
                      MetaStore* output,
                      const MetadataAuthoringOptions& options
                      = MetadataAuthoringOptions {}) noexcept;

const char*
metadata_authoring_status_name(MetadataAuthoringStatus status) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
