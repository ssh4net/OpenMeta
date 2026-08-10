// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstdint>
#include <span>
#include <string_view>

/**
 * \file metadata_creation.h
 * \brief Bounded construction of fresh portable metadata stores.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable metadata creation contract version.
inline constexpr uint32_t kMetadataCreationContractVersion = 1U;

inline constexpr uint32_t kMetadataCreationMaxFields            = 1024U;
inline constexpr uint32_t kMetadataCreationMaxTextBytesPerField = 1024U * 1024U;
inline constexpr uint64_t kMetadataCreationMaxTotalTextBytes    = 8ULL * 1024ULL
                                                               * 1024ULL;
inline constexpr uint32_t kInvalidMetadataCreationFieldIndex = 0xffffffffU;

/// Logical portable field accepted by `create_metadata(...)`.
enum class MetadataCreationFieldKind : uint8_t {
    Title,
    Description,
    Creator,
    Keyword,
    Copyright,
    RightsUsageTerms,
    Credit,
    Source,
    CreateDate,
    ModifyDate,
    Rating,
    Label,
    CameraMake,
    CameraModel,
    Software,
    DateTimeOriginal,
    Orientation,
    PixelWidth,
    PixelHeight,
    ColorSpace,
    ExposureTime,
    FNumber,
    IsoSensitivity,
    FocalLength,
};

/// Input value representation for one creation field.
enum class MetadataCreationValueKind : uint8_t {
    Text,
    UnsignedInteger,
    SignedInteger,
    UnsignedRational,
};

/// One logical field supplied by a host application.
struct MetadataCreationField final {
    MetadataCreationFieldKind kind       = MetadataCreationFieldKind::Title;
    MetadataCreationValueKind value_kind = MetadataCreationValueKind::Text;
    std::string_view text;
    uint32_t unsigned_value = 0U;
    int32_t signed_value    = 0;
    URational rational;
};

/// Creates a text-valued field without copying \p value.
MetadataCreationField
make_metadata_creation_text(MetadataCreationFieldKind kind,
                            std::string_view value) noexcept;

/// Creates an unsigned-integer-valued field.
MetadataCreationField
make_metadata_creation_u32(MetadataCreationFieldKind kind,
                           uint32_t value) noexcept;

/// Creates a signed-integer-valued field.
MetadataCreationField
make_metadata_creation_i32(MetadataCreationFieldKind kind,
                           int32_t value) noexcept;

/// Creates an unsigned-rational-valued field.
MetadataCreationField
make_metadata_creation_urational(MetadataCreationFieldKind kind, uint32_t numer,
                                 uint32_t denom) noexcept;

/// Caller-selected limits, bounded by the public hard maxima above.
struct MetadataCreationLimits final {
    uint32_t max_fields               = kMetadataCreationMaxFields;
    uint32_t max_text_bytes_per_field = kMetadataCreationMaxTextBytesPerField;
    uint64_t max_total_text_bytes     = kMetadataCreationMaxTotalTextBytes;
};

/// Stable creation request.
struct MetadataCreationRequest final {
    std::span<const MetadataCreationField> fields;
    MetadataCreationLimits limits;
};

/// Status returned by `create_metadata(...)`.
enum class MetadataCreationStatus : uint8_t {
    Ok,
    NullOutput,
    InvalidLimits,
    TooManyFields,
    WrongValueKind,
    EmptyText,
    TextTooLong,
    TotalTextTooLong,
    InvalidText,
    InvalidValue,
    DuplicateSingleton,
    InternalError,
};

/// Result details for one creation request.
struct MetadataCreationResult final {
    MetadataCreationStatus status = MetadataCreationStatus::Ok;
    uint32_t failed_field_index   = kInvalidMetadataCreationFieldIndex;
    uint32_t field_count          = 0U;
    uint32_t entries_created      = 0U;
};

/**
 * \brief Builds a finalized store containing canonical portable-XMP entries.
 *
 * Validation is transactional: on any non-Ok result, \p out_store is not
 * modified. Creator and Keyword fields are additive and preserve request order;
 * every other field is a singleton. Text must be non-empty, valid UTF-8, and
 * valid XML 1.0 character data.
 *
 * The output representation is intentionally portable XMP. Projecting these
 * logical values into EXIF or IPTC wire fields belongs to the Translation
 * stage. Entries are marked \ref EntryFlags::Dirty and can be passed directly
 * to existing portable-XMP and metadata-transfer APIs.
 *
 * The function keeps no global state. Concurrent calls are safe when each call
 * uses a distinct output store.
 */
MetadataCreationResult
create_metadata(const MetadataCreationRequest& request, MetaStore* out_store);

const char*
metadata_creation_field_kind_name(MetadataCreationFieldKind kind) noexcept;

const char*
metadata_creation_status_name(MetadataCreationStatus status) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
