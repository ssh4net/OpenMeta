// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"
#include "openmeta/validate.h"

#include <array>
#include <cstdint>

/**
 * \file metadata_reconcile.h
 * \brief Bounded reconciliation for standard descriptive metadata families.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

inline constexpr uint32_t kMetadataReconcileContractVersion = 1U;

inline constexpr uint32_t kMetadataReconcileMaxSourceEntries = 200000U;
inline constexpr uint32_t kMetadataReconcileMaxKeyBytes      = 4096U;
inline constexpr uint64_t kMetadataReconcileMaxSourceArenaBytes
    = 64ULL * 1024ULL * 1024ULL;
inline constexpr uint32_t kMetadataReconcileMaxSourceProperties = 1024U;
inline constexpr uint64_t kMetadataReconcileMaxTotalTextBytes   = 8ULL * 1024ULL
                                                                * 1024ULL;
inline constexpr uint64_t kMetadataReconcileMaxTotalParticipatingBytes
    = 16ULL * 1024ULL * 1024ULL;
inline constexpr uint32_t kMetadataReconcileMaxOperations = 4096U;

enum class MetadataReconcileField : uint8_t {
    None,
    Title,
    Description,
    Creator,
    Keywords,
    Rights,
};

enum class MetadataReconcileFamily : uint8_t {
    None,
    Xmp,
    Iptc,
    Exif,
};

enum class MetadataReconcileStage : uint8_t {
    None,
    Preflight,
    ResolveAuthority,
    ProjectXmp,
    ProjectIptc,
    ProjectExif,
    Publish,
};

enum class MetadataReconcileConflictPolicy : uint8_t {
    FailOnConflict,
    PreferSourceOrder,
};

enum class MetadataReconcileDeletionPolicy : uint8_t {
    Ignore,
    PropagateDirty,
};

enum class MetadataReconcileStatus : uint8_t {
    Ok,
    NullOutput,
    SourceNotFinalized,
    InvalidOptions,
    SourceLimitExceeded,
    InvalidSourceValue,
    ValueTooLong,
    UnsupportedSourceShape,
    AmbiguousSource,
    Conflict,
    SourceEncodingConflict,
    EntryLimitExceeded,
    OperationLimitExceeded,
    ProjectionFailure,
    InternalError,
};

/// Caller-selected participation, family priority, policy, and hard-bounded work.
struct MetadataReconcileOptions final {
    /// XMP always participates. Native IPTC participates by default.
    bool participate_iptc = true;
    /// Enable the IFD0 Description/Artist/Copyright and TIFF-XMP bridge.
    bool enable_exif_bridge = false;
    bool title              = true;
    bool description        = true;
    bool creators           = true;
    bool keywords           = true;
    bool copyright          = true;

    /// Must contain each of Xmp, Iptc, and Exif exactly once.
    std::array<MetadataReconcileFamily, 3U> family_priority
        = { MetadataReconcileFamily::Xmp, MetadataReconcileFamily::Iptc,
            MetadataReconcileFamily::Exif };
    MetadataReconcileConflictPolicy conflict_policy
        = MetadataReconcileConflictPolicy::FailOnConflict;
    MetadataReconcileDeletionPolicy deletion_policy
        = MetadataReconcileDeletionPolicy::Ignore;

    uint32_t max_source_entries     = kMetadataReconcileMaxSourceEntries;
    uint64_t max_source_arena_bytes = kMetadataReconcileMaxSourceArenaBytes;
    uint32_t max_source_properties  = kMetadataReconcileMaxSourceProperties;
    uint64_t max_total_text_bytes   = kMetadataReconcileMaxTotalTextBytes;
    uint64_t max_total_participating_bytes
        = kMetadataReconcileMaxTotalParticipatingBytes;
    uint32_t max_operations = kMetadataReconcileMaxOperations;
    MetadataValidationOptions validation;
};

/// Transaction result. Mutation counts are zero whenever status is not Ok.
struct MetadataReconcileResult final {
    MetadataReconcileStatus status        = MetadataReconcileStatus::Ok;
    MetadataReconcileField failed_field   = MetadataReconcileField::None;
    MetadataReconcileFamily failed_family = MetadataReconcileFamily::None;
    MetadataReconcileStage failed_stage   = MetadataReconcileStage::None;
    EntryId failed_entry                  = kInvalidEntryId;
    MetadataValidationIssueCode validation_issue
        = MetadataValidationIssueCode::None;

    uint32_t source_properties = 0U;
    uint32_t groups_reconciled = 0U;
    uint32_t translator_passes = 0U;
    uint32_t entries_added     = 0U;
    uint32_t entries_updated   = 0U;
    uint32_t entries_removed   = 0U;
};

/**
 * \brief Reconcile DC descriptive values with IPTC-IIM and optional IFD0 text.
 *
 * The managed mappings are DC title/description/creator/subject/rights with
 * IPTC-IIM 2:5/120/80/25/116. The optional profile bridge covers only
 * Description/ImageDescription, a single Creator/Artist, and scalar
 * Rights/Copyright; it does not join or split multiple creators or legacy
 * two-part native copyright. Non-default language siblings and unrelated
 * metadata are retained. Dirty tombstones are deletion intents only when the
 * selected deletion policy is PropagateDirty; an active group supersedes its
 * item tombstones. Preparation may allocate and use bounded translator passes.
 * Each store represents one logical document. The output is published only
 * after all selected work succeeds. Source/output aliasing is supported, and
 * callers synchronize shared stores themselves.
 */
MetadataReconcileResult
reconcile_descriptive_metadata(const MetaStore& source,
                               const MetadataReconcileOptions& options,
                               MetaStore* out_store) noexcept;

const char*
metadata_reconcile_status_name(MetadataReconcileStatus status) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
