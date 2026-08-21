// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"
#include "openmeta/metadata_concepts.h"
#include "openmeta/resource_policy.h"
#include "openmeta/simple_meta.h"
#include "openmeta/vendor_raw_processing.h"
#include "openmeta/xmp_dump.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * \file metadata_transfer.h
 * \brief Draft metadata transfer bundle and backend emitter contracts.
 *
 * This header defines a stable draft contract for "prepare once, emit many"
 * metadata transfer workflows.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable metadata transfer contract version.
inline constexpr uint32_t kMetadataTransferContractVersion = 1U;

/// Version of the codec-facing typed adapter operation contract.
inline constexpr uint32_t kPreparedTransferAdapterContractVersion = 1U;

/// Version of the persisted target-neutral source snapshot wire format.
inline constexpr uint32_t kTransferSourceSnapshotSerializationVersion = 1U;

/// Target container family for prepared transfer bundles.
enum class TransferTargetFormat : uint8_t {
    Jpeg,
    Tiff,
    Jxl,
    Webp,
    Heif,
    Avif,
    Cr3,
    Exr,
    Png,
    Jp2,
    Dng,
};

/// Public DNG target contract for metadata-only transfer workflows.
enum class DngTargetMode : uint8_t {
    ExistingTarget,
    TemplateTarget,
    MinimalFreshScaffold,
};

/// Prepared payload block category.
enum class TransferBlockKind : uint8_t {
    Exif,
    Xmp,
    IptcIim,
    PhotoshopIrb,
    Icc,
    Jumbf,
    C2pa,
    ExrAttribute,
    Other,
};

/// Status for transfer preparation and emit APIs.
enum class TransferStatus : uint8_t {
    Ok,
    InvalidArgument,
    Unsupported,
    LimitExceeded,
    Malformed,
    UnsafeData,
    InternalError,
};

/// Stable preparation error code for \ref PrepareTransferResult.
enum class PrepareTransferCode : uint16_t {
    None = 0,
    NullOutBundle,
    UnsupportedTargetFormat,
    ExifPackFailed,
    XmpPackFailed,
    IccPackFailed,
    IptcPackFailed,
    RequestedMetadataNotSerializable,
};

/// Stable emit error code for \ref EmitTransferResult.
enum class EmitTransferCode : uint16_t {
    None = 0,
    InvalidArgument,
    BundleTargetNotJpeg,
    UnsupportedRoute,
    InvalidPayload,
    ContentBoundPayloadUnsupported,
    BackendWriteFailed,
    PlanMismatch,
};

/// Stable file/read/prepare error code for \ref PrepareTransferFileResult.
enum class PrepareTransferFileCode : uint16_t {
    None = 0,
    EmptyPath,
    MapFailed,
    PayloadBufferPlatformLimit,
    DecodeFailed,
};

/// Stable file/read error code for \ref ReadTransferSourceSnapshotFileResult.
enum class ReadTransferSourceSnapshotFileCode : uint16_t {
    None = 0,
    EmptyPath,
    MapFailed,
    PayloadBufferPlatformLimit,
    DecodeFailed,
};

/// Stable bytes/read error code for \ref ReadTransferSourceSnapshotBytesResult.
enum class ReadTransferSourceSnapshotBytesCode : uint16_t {
    None = 0,
    PayloadBufferPlatformLimit,
    DecodeFailed,
};

/// Stable result code for positional source snapshot assembly.
enum class ReadTransferSourceSnapshotRandomAccessCode : uint16_t {
    None = 0,
    InvalidArgument,
    UnsupportedFormat,
    ScanFailed,
    ScratchTooSmall,
    DecodeFailed,
    /// A usable snapshot was assembled, but explicitly reported source-wide or
    /// container-specific metadata paths were not decoded.
    ResidualMetadataPaths,
};

/// Severity of one structured positional snapshot read diagnostic.
enum class ReadTransferSourceDiagnosticSeverity : uint8_t {
    Info,
    Warning,
    Error,
};

/// Decode or input domain associated with a structured read diagnostic.
enum class ReadTransferSourceDiagnosticDomain : uint8_t {
    Input,
    Container,
    Payload,
    Exif,
    Xmp,
    Jumbf,
    Exr,
    MakerNote,
    ResidualPath,
};

/// Stable machine-readable positional snapshot read diagnostic code.
enum class ReadTransferSourceDiagnosticCode : uint16_t {
    InputFailure,
    MalformedContainer,
    MalformedPayload,
    MalformedExif,
    MalformedXmp,
    MalformedJumbf,
    MalformedExr,
    ResourceLimit,
    ScratchTooSmall,
    IncompleteMakerNote,
    ResidualMetadataPath,
    RawCarrierTruncated,
};

/// Status for high-level file-to-bundle transfer preparation.
enum class TransferFileStatus : uint8_t {
    Ok,
    InvalidArgument,
    OpenFailed,
    StatFailed,
    TooLarge,
    MapFailed,
    ReadFailed,
};

/// Transfer-policy subject handled during bundle preparation.
enum class TransferPolicySubject : uint8_t {
    MakerNote,
    Jumbf,
    C2pa,
    XmpExifProjection,
    XmpIptcProjection,
    ImageProperties,
    IccProfile,
    RawColorCalibration,
    CameraRawSettings,
};

/// Requested/effective action for a metadata family during transfer.
enum class TransferPolicyAction : uint8_t {
    Keep,
    Drop,
    Invalidate,
    Rewrite,
};

/// Reason attached to one prepared transfer policy decision.
enum class TransferPolicyReason : uint8_t {
    Default,
    NotPresent,
    ExplicitDrop,
    CarrierDisabled,
    ProjectedPayload,
    DraftInvalidationPayload,
    ExternalSignedPayload,
    ContentBoundTransferUnavailable,
    SignedRewriteUnavailable,
    PortableInvalidationUnavailable,
    RewriteUnavailablePreservedRaw,
    TargetSerializationUnavailable,
    TargetImageProperties,
    SafetyModeFiltered,
    RawDataDescriptorFiltered,
    /// Opaque bytes are retained, but nested offsets, vendor checksums, and
    /// semantic readability after relocation are not verified.
    OpaquePayloadPreservedUnverified,
    /// Rewrite was requested, but no safe serializer is available; payloads
    /// are dropped rather than silently treated as preserved rewrites.
    RewriteUnavailableDropped,
};

/// Explicit current C2PA transfer mode resolved during prepare.
enum class TransferC2paMode : uint8_t {
    NotApplicable,
    NotPresent,
    Drop,
    DraftUnsignedInvalidation,
    PreserveRaw,
    SignedRewrite,
};

/// Classified C2PA source state observed during prepare.
enum class TransferC2paSourceKind : uint8_t {
    NotApplicable,
    NotPresent,
    DecodedOnly,
    ContentBound,
    DraftUnsignedInvalidation,
};

/// Prepared C2PA output contract selected during prepare.
enum class TransferC2paPreparedOutput : uint8_t {
    NotApplicable,
    NotPresent,
    Dropped,
    PreservedRaw,
    GeneratedDraftUnsignedInvalidation,
    SignedRewrite,
};

/// Current rewrite-signing readiness for C2PA transfer.
enum class TransferC2paRewriteState : uint8_t {
    NotApplicable,
    NotRequested,
    SigningMaterialRequired,
    Ready,
};

/// One deterministic chunk in the future C2PA rewrite binding sequence.
enum class TransferC2paRewriteChunkKind : uint8_t {
    SourceRange,
    PreparedJpegSegment,
    PreparedJxlBox,
    PreparedBmffMetaBox,
};

/// Coarse transfer safety mode for source metadata reuse.
enum class TransferSafetyMode : uint8_t {
    /// Compatible metadata repackage/recompression; preserves source color and
    /// camera-specific metadata except target-owned image-layout fields.
    CompatibleFile,
    /// Rendered/exported pixels; drops source color-calibration, raw pipeline,
    /// lens-correction, raw settings, source ICC, MakerNote, and non-C2PA
    /// JUMBF data unless host code supplies target-correct replacements.
    RenderedImage,
};

/// One deterministic chunk in the rewrite-without-C2PA byte stream.
struct PreparedTransferC2paRewriteChunk final {
    TransferC2paRewriteChunkKind kind
        = TransferC2paRewriteChunkKind::SourceRange;
    uint64_t source_offset   = 0;
    uint64_t size            = 0;
    uint32_t block_index     = 0xFFFFFFFFU;
    uint8_t jpeg_marker_code = 0U;
};

/// Future-facing signer prerequisites for C2PA rewrite.
struct PreparedTransferC2paRewriteRequirements final {
    TransferC2paRewriteState state = TransferC2paRewriteState::NotApplicable;
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    TransferC2paSourceKind source_kind = TransferC2paSourceKind::NotApplicable;
    uint32_t matched_entries           = 0;
    uint32_t existing_carrier_segments = 0;
    bool target_carrier_available      = false;
    bool content_change_invalidates_existing = false;
    bool requires_manifest_builder           = false;
    bool requires_content_binding            = false;
    bool requires_certificate_chain          = false;
    bool requires_private_key                = false;
    bool requires_signing_time               = false;
    uint64_t content_binding_bytes           = 0;
    std::vector<PreparedTransferC2paRewriteChunk> content_binding_chunks;
    std::string message;
};

/// Derived external signer input request for prepared C2PA rewrite.
struct PreparedTransferC2paSignRequest final {
    TransferStatus status = TransferStatus::Unsupported;
    TransferC2paRewriteState rewrite_state
        = TransferC2paRewriteState::NotApplicable;
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    TransferC2paSourceKind source_kind = TransferC2paSourceKind::NotApplicable;
    std::string carrier_route;
    std::string manifest_label;
    uint32_t existing_carrier_segments = 0;
    uint32_t source_range_chunks       = 0;
    uint32_t prepared_segment_chunks   = 0;
    uint64_t content_binding_bytes     = 0;
    std::vector<PreparedTransferC2paRewriteChunk> content_binding_chunks;
    bool requires_manifest_builder  = false;
    bool requires_content_binding   = false;
    bool requires_certificate_chain = false;
    bool requires_private_key       = false;
    bool requires_signing_time      = false;
    std::string message;
};

/// External signer material returned for one prepared C2PA rewrite request.
struct PreparedTransferC2paSignerInput final {
    std::string signing_time;
    std::vector<std::byte> certificate_chain_bytes;
    std::string private_key_reference;
    std::vector<std::byte> manifest_builder_output;
    std::vector<std::byte> signed_c2pa_logical_payload;
};

/// Classified logical C2PA payload kind supplied by an external signer.
enum class TransferC2paSignedPayloadKind : uint8_t {
    NotApplicable,
    GenericJumbf,
    DraftUnsignedInvalidation,
    ContentBound,
};

/// Semantic validation status for a staged logical C2PA payload.
enum class TransferC2paSemanticStatus : uint8_t {
    NotChecked,
    Ok,
    Invalid,
};

/// Result for materializing the exact content-binding byte stream.
struct BuildPreparedC2paBindingResult final {
    TransferStatus status = TransferStatus::Unsupported;
    EmitTransferCode code = EmitTransferCode::None;
    uint64_t written      = 0;
    uint32_t errors       = 0;
    std::string message;
};

/// Structured external-signer handoff package for one prepared rewrite.
struct PreparedTransferC2paHandoffPackage final {
    PreparedTransferC2paSignRequest request;
    BuildPreparedC2paBindingResult binding;
    std::vector<std::byte> binding_bytes;
};

/// Persistable external-signer result package for one prepared rewrite.
struct PreparedTransferC2paSignedPackage final {
    PreparedTransferC2paSignRequest request;
    PreparedTransferC2paSignerInput signer_input;
};

/// Result for serializing or parsing persisted C2PA transfer packages.
struct PreparedTransferC2paPackageIoResult final {
    TransferStatus status = TransferStatus::Unsupported;
    EmitTransferCode code = EmitTransferCode::None;
    uint64_t bytes        = 0;
    uint32_t errors       = 0;
    std::string message;
};

/// Result for validating one externally signed C2PA payload before staging.
struct ValidatePreparedC2paSignResult final {
    TransferStatus status = TransferStatus::Unsupported;
    EmitTransferCode code = EmitTransferCode::None;
    TransferC2paSignedPayloadKind payload_kind
        = TransferC2paSignedPayloadKind::NotApplicable;
    TransferC2paSemanticStatus semantic_status
        = TransferC2paSemanticStatus::NotChecked;
    uint64_t logical_payload_bytes                                 = 0;
    uint64_t staged_payload_bytes                                  = 0;
    uint64_t semantic_manifest_present                             = 0;
    uint64_t semantic_manifest_count                               = 0;
    uint64_t semantic_claim_generator_present                      = 0;
    uint64_t semantic_assertion_count                              = 0;
    uint64_t semantic_primary_claim_assertion_count                = 0;
    uint64_t semantic_primary_claim_referenced_by_signature_count  = 0;
    uint64_t semantic_primary_signature_linked_claim_count         = 0;
    uint64_t semantic_primary_signature_reference_key_hits         = 0;
    uint64_t semantic_primary_signature_explicit_reference_present = 0;
    uint64_t semantic_primary_signature_explicit_reference_resolved_claim_count
        = 0;
    uint64_t semantic_claim_count                                   = 0;
    uint64_t semantic_signature_count                               = 0;
    uint64_t semantic_signature_linked                              = 0;
    uint64_t semantic_signature_orphan                              = 0;
    uint64_t semantic_explicit_reference_signature_count            = 0;
    uint64_t semantic_explicit_reference_unresolved_signature_count = 0;
    uint64_t semantic_explicit_reference_ambiguous_signature_count  = 0;
    uint32_t staged_segments                                        = 0;
    uint32_t errors                                                 = 0;
    std::string semantic_reason;
    std::string message;
};

/// Transfer policy profile for initial no-edits workflows.
struct TransferProfile final {
    TransferPolicyAction makernote = TransferPolicyAction::Keep;
    TransferPolicyAction jumbf     = TransferPolicyAction::Keep;
    TransferPolicyAction c2pa      = TransferPolicyAction::Keep;
    bool allow_time_patch          = true;
    TransferSafetyMode safety      = TransferSafetyMode::CompatibleFile;
};

/// Source metadata and filtered-entry counts for one transfer safety mode.
struct TransferSafetyAudit final {
    TransferSafetyMode safety = TransferSafetyMode::CompatibleFile;

    uint32_t source_image_properties      = 0U;
    uint32_t source_raw_color_calibration = 0U;
    uint32_t source_camera_raw_settings   = 0U;
    uint32_t source_icc_profiles          = 0U;
    uint32_t source_makernotes            = 0U;
    uint32_t source_non_c2pa_jumbf        = 0U;
    uint32_t source_c2pa                  = 0U;

    uint32_t filtered_image_properties      = 0U;
    uint32_t filtered_raw_color_calibration = 0U;
    uint32_t filtered_camera_raw_settings   = 0U;
    uint32_t filtered_icc_profiles          = 0U;
    uint32_t filtered_makernotes            = 0U;
    uint32_t filtered_non_c2pa_jumbf        = 0U;
    uint32_t invalidated_c2pa               = 0U;

    VendorRawProcessingSummary sony_raw_processing;
    VendorRawProcessingSummary canon_raw_processing;
    VendorRawProcessingSummary nikon_raw_processing;
    VendorRawProcessingSummary fujifilm_raw_processing;
    VendorRawProcessingSummary pentax_raw_processing;
    VendorRawProcessingSummary panasonic_raw_processing;
    VendorRawProcessingSummary olympus_raw_processing;
    VendorRawProcessingSummary kodak_raw_processing;
    VendorRawProcessingSummary minolta_raw_processing;
    VendorRawProcessingSummary sigma_raw_processing;
    VendorRawProcessingSummary samsung_raw_processing;
    VendorRawProcessingSummary ricoh_raw_processing;
    VendorRawProcessingSummary apple_raw_processing;
    VendorRawProcessingSummary dji_raw_processing;
    VendorRawProcessingSummary google_raw_processing;
    VendorRawProcessingSummary flir_raw_processing;
    VendorRawProcessingSummary casio_raw_processing;
    VendorRawProcessingSummary sanyo_raw_processing;
    VendorRawProcessingSummary kyocera_raw_processing;
    VendorRawProcessingSummary reconyx_raw_processing;
    VendorRawProcessingSummary hp_raw_processing;
    VendorRawProcessingSummary jvc_raw_processing;
    VendorRawProcessingSummary ge_raw_processing;
    VendorRawProcessingSummary motorola_raw_processing;
    VendorRawProcessingSummary nintendo_raw_processing;
    VendorRawProcessingSummary microsoft_raw_processing;
};

/// Trust state for vendor MakerNote data observed in a decoded store.
enum class TransferMakerNoteTrust : uint8_t {
    NotPresent,
    DecodedOnlyNotSerializable,
    OpaquePreservationUnverified,
};

/// Source evidence and current writer capabilities for MakerNote transfer.
struct TransferMakerNoteAudit final {
    TransferMakerNoteTrust trust      = TransferMakerNoteTrust::NotPresent;
    uint32_t raw_payload_count        = 0U;
    uint32_t decoded_only_entry_count = 0U;
    bool opaque_payload_available     = false;
    bool decoded_rewrite_available    = false;
    bool internal_offset_relocation_available    = false;
    bool vendor_checksum_recalculation_available = false;
    bool semantic_validation_available           = false;
    bool raw_carrier_passthrough_available       = false;
};

/// Vendor identified from a raw MakerNote payload layout.
enum class TransferMakerNoteVendor : uint8_t {
    Unknown,
    Nikon,
};

/// Raw MakerNote layout relevant to offset-safe transfer.
enum class TransferMakerNoteLayout : uint8_t {
    UnknownOrMixed,
    NikonType1OuterTiff,
    NikonType3EmbeddedTiff,
};

/// Structural trust established for a raw MakerNote layout.
enum class TransferMakerNoteLayoutTrust : uint8_t {
    NotPresent,
    UnrecognizedOrMixed,
    OuterTiffOffsetsUnsafe,
    EmbeddedTiffStructureUnverified,
    EmbeddedTiffStructureVerified,
};

/// Vendor-layout evidence that can be established without rewriting a note.
struct TransferMakerNoteLayoutAudit final {
    TransferMakerNoteLayoutTrust trust
        = TransferMakerNoteLayoutTrust::NotPresent;
    TransferMakerNoteVendor vendor    = TransferMakerNoteVendor::Unknown;
    TransferMakerNoteLayout layout    = TransferMakerNoteLayout::UnknownOrMixed;
    uint32_t raw_payload_count        = 0U;
    uint32_t recognized_payload_count = 0U;
    uint32_t structurally_valid_payload_count    = 0U;
    bool offset_basis_known                      = false;
    bool embedded_tiff_validation_available      = false;
    bool embedded_tiff_validation_passed         = false;
    bool embedded_tiff_offsets_self_contained    = false;
    bool outer_tiff_offset_relocation_required   = false;
    bool vendor_private_offsets_verified         = false;
    bool vendor_checksum_validation_available    = false;
    bool semantic_roundtrip_validation_available = false;
};

enum class TransferConceptDiagnosticAction : uint8_t {
    Keep,
    Drop,
    RequiresTargetImageSpec,
};

enum class TransferConceptDiagnosticReason : uint8_t {
    Unknown,
    Safe,
    SourceBound,
    RenderedUnsafe,
    TargetImageSpecRequired,
    RawApplicabilityConditional,
    RawApplicabilityNotApplicable,
};

enum class TransferConceptDiagnosticSeverity : uint8_t {
    Info,
    Warning,
};

struct TransferConceptDiagnostic final {
    MetadataConceptKind kind = MetadataConceptKind::Orientation;
    MetadataConceptRole role = MetadataConceptRole::Primary;
    /// Structured-record type copied from the concept candidate.
    MetadataConceptRecordKind record_kind = MetadataConceptRecordKind::None;
    /// Host policy signal; independent from `hint` and `action`.
    MetadataConceptSensitivity sensitivity = MetadataConceptSensitivity::None;
    MetadataConceptTransferHint hint = MetadataConceptTransferHint::Unknown;
    TransferConceptDiagnosticAction action
        = TransferConceptDiagnosticAction::Drop;
    TransferConceptDiagnosticReason reason
        = TransferConceptDiagnosticReason::Unknown;
    TransferConceptDiagnosticSeverity severity
        = TransferConceptDiagnosticSeverity::Warning;
    EntryId entry_id = kInvalidEntryId;
    std::vector<EntryId> source_entries;
    bool preferred                  = false;
    bool conflict                   = false;
    bool compatible_file_safe       = false;
    bool rendered_image_safe        = false;
    bool requires_target_image_spec = false;
    bool source_bound               = false;
    MetadataRawApplicabilityState raw_applicability
        = MetadataRawApplicabilityState::Unknown;
    bool raw_applicability_requires_storage_context = false;
    bool raw_applicability_can_affect_decode        = false;
    bool has_gps_altitude_reference                 = false;
    bool gps_altitude_below_sea_level               = false;
    uint8_t gps_altitude_reference_code             = 0U;
    std::string location_scope;
    std::string record_scope;
    std::string language;
};

struct TransferConceptDiagnostics final {
    TransferSafetyMode safety = TransferSafetyMode::CompatibleFile;
    uint32_t candidate_count  = 0U;
    uint32_t kept_count       = 0U;
    uint32_t dropped_count    = 0U;
    uint32_t requires_target_image_spec_count = 0U;
    uint32_t rendered_unsafe_count            = 0U;
    uint32_t source_bound_count               = 0U;
    uint32_t conflict_count                   = 0U;
    std::vector<TransferConceptDiagnostic> diagnostics;
};

/// Effective policy decision captured during bundle preparation.
struct PreparedTransferPolicyDecision final {
    TransferPolicySubject subject  = TransferPolicySubject::MakerNote;
    TransferPolicyAction requested = TransferPolicyAction::Keep;
    TransferPolicyAction effective = TransferPolicyAction::Keep;
    TransferPolicyReason reason    = TransferPolicyReason::Default;
    TransferC2paMode c2pa_mode     = TransferC2paMode::NotApplicable;
    TransferC2paSourceKind c2pa_source_kind
        = TransferC2paSourceKind::NotApplicable;
    TransferC2paPreparedOutput c2pa_prepared_output
        = TransferC2paPreparedOutput::NotApplicable;
    uint32_t matched_entries = 0;
    std::string message;
};

/// Optional fixed-width patch field identifiers for per-frame updates.
enum class TimePatchField : uint8_t {
    DateTime,
    DateTimeOriginal,
    DateTimeDigitized,
    SubSecTime,
    SubSecTimeOriginal,
    SubSecTimeDigitized,
    OffsetTime,
    OffsetTimeOriginal,
    OffsetTimeDigitized,
    GpsDateStamp,
    GpsTimeStamp,
};

/// One fixed-width patch location in a prepared payload block.
struct TimePatchSlot final {
    TimePatchField field = TimePatchField::DateTime;
    uint32_t block_index = 0;
    uint32_t byte_offset = 0;
    uint16_t width       = 0;
};

/// One container-ready payload in emit order.
struct PreparedTransferBlock final {
    TransferBlockKind kind = TransferBlockKind::Other;
    uint32_t order         = 0;
    /// Route token for backend dispatch, for example: `jpeg:app1-exif`.
    std::string route;
    /// Optional 4CC when route is box-based (`Exif`, `xml `, `jumb`...).
    std::array<char, 4> box_type = { '\0', '\0', '\0', '\0' };
    /// Payload bytes as prepared by the transfer packager.
    std::vector<std::byte> payload;
};

/// Immutable prepared metadata transfer artifact.
struct PreparedTransferBundle final {
    uint32_t contract_version          = kMetadataTransferContractVersion;
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    DngTargetMode dng_target_mode      = DngTargetMode::MinimalFreshScaffold;
    TransferProfile profile;
    PreparedTransferC2paRewriteRequirements c2pa_rewrite;
    std::vector<PreparedTransferPolicyDecision> policy_decisions;
    std::vector<PreparedTransferBlock> blocks;
    std::vector<TimePatchSlot> time_patch_map;
    std::vector<std::byte> generated_xmp_sidecar;
};

/// One opt-in raw source carrier captured next to a decoded snapshot.
struct TransferSourceRawCarrier final {
    /// Original container block reference, including byte ranges and chunking.
    ContainerBlockRef block;
    /// Transfer semantic family inferred from the scanned block kind.
    TransferBlockKind semantic_kind = TransferBlockKind::Other;
    /// Scan order among preserved source carriers.
    uint32_t order = 0U;
    /// Stable route hint for passthrough and decoded re-emission policy.
    std::string route;
    /// True when \ref payload contains the original block payload bytes.
    bool payload_preserved = false;
    /// Original metadata payload bytes, bounded by read options.
    std::vector<std::byte> payload;
    /// Decoded \ref MetaStore entry ids attributed to this source carrier.
    std::vector<EntryId> decoded_entry_ids;
};

/// Primary passthrough eligibility result for one preserved raw carrier.
enum class TransferRawCarrierPassthroughReason : uint8_t {
    Candidate,
    MissingPayload,
    TargetIncompatible,
    SafetyFiltered,
    ContentBoundMetadata,
    PolicyBlocked,
    DecodeLinkUnavailable,
    UnsupportedKind,
};

/// Opt-in raw-carrier use during snapshot transfer preparation.
enum class TransferRawCarrierPassthroughMode : uint8_t {
    /// Do not reuse preserved raw carriers while preparing from a snapshot.
    Disabled,
    /// Reuse only bounded carrier families that pass the active policy checks.
    WhenSafe,
};

/// Options for auditing raw-carrier passthrough eligibility.
struct TransferRawCarrierPassthroughAuditOptions final {
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    TransferProfile profile;
    bool require_decoded_entry_links = true;
};

/// One raw-carrier passthrough eligibility decision.
struct TransferRawCarrierPassthroughDecision final {
    uint32_t carrier_index          = 0U;
    uint32_t decoded_entry_count    = 0U;
    TransferBlockKind semantic_kind = TransferBlockKind::Other;
    bool eligible                   = false;
    TransferRawCarrierPassthroughReason reason
        = TransferRawCarrierPassthroughReason::UnsupportedKind;
    std::string source_route;
    std::string target_route;
};

/// Diagnostic summary for raw-carrier passthrough eligibility.
struct TransferRawCarrierPassthroughAudit final {
    TransferTargetFormat target_format   = TransferTargetFormat::Jpeg;
    TransferSafetyMode safety            = TransferSafetyMode::CompatibleFile;
    uint32_t carrier_count               = 0U;
    uint32_t eligible_count              = 0U;
    uint32_t blocked_missing_payload     = 0U;
    uint32_t blocked_target_incompatible = 0U;
    uint32_t blocked_safety_filtered     = 0U;
    uint32_t blocked_content_bound_metadata  = 0U;
    uint32_t blocked_policy                  = 0U;
    uint32_t blocked_decode_link_unavailable = 0U;
    uint32_t blocked_unsupported_kind        = 0U;
    std::vector<TransferRawCarrierPassthroughDecision> decisions;
};

/**
 * \brief Reusable source snapshot for later transfer preparation.
 *
 * \par API Stability
 * Experimental host-facing API. The default snapshot remains decoded-store
 * backed. Raw carrier provenance and payload bytes are kept only when read
 * options explicitly request them; transfer preparation still uses decoded
 * re-emission unless the request enables an explicit bounded passthrough mode.
 *
 * Const reuse is safe when callers do not mutate the snapshot and do not share
 * returned result objects across writers.
 */
struct TransferSourceSnapshot final {
    MetaStore store;
    std::vector<TransferSourceRawCarrier> raw_carriers;
    uint64_t raw_carrier_bytes       = 0U;
    bool raw_carrier_bytes_truncated = false;
};

/// Stable result code for source snapshot serialization and parsing.
enum class TransferSourceSnapshotIoCode : uint16_t {
    None = 0,
    InvalidArgument,
    SnapshotNotFinalized,
    InvalidSnapshot,
    InvalidMagic,
    UnsupportedVersion,
    LimitExceeded,
    Malformed,
};

/// Resource limits for persisted source snapshot serialization and parsing.
struct TransferSourceSnapshotIoOptions final {
    uint64_t max_serialized_bytes    = 256ULL * 1024ULL * 1024ULL;
    uint64_t max_arena_bytes         = 64ULL * 1024ULL * 1024ULL;
    uint64_t max_raw_carrier_bytes   = 64ULL * 1024ULL * 1024ULL;
    uint64_t max_decoded_entry_links = 1000000ULL;
    uint32_t max_blocks              = 65536U;
    uint32_t max_entries             = 200000U;
    uint32_t max_raw_carriers        = 65536U;
    uint32_t max_route_bytes         = 4096U;
};

/// Result for persisted target-neutral source snapshot I/O.
struct TransferSourceSnapshotIoResult final {
    TransferStatus status             = TransferStatus::Unsupported;
    TransferSourceSnapshotIoCode code = TransferSourceSnapshotIoCode::None;
    uint64_t bytes                    = 0U;
    uint32_t errors                   = 0U;
    std::string message;
};

/// Options for explicit raw JUMBF append into a prepared JPEG bundle.
struct AppendPreparedJpegJumbfOptions final {
    /// If true, remove existing prepared `jpeg:app11-jumbf` blocks first.
    bool replace_existing = false;
};

/// Maximum channel/sample count represented by \ref TransferTargetImageSpec.
inline constexpr uint16_t kTransferTargetImageSpecMaxSamples = 8U;

/**
 * \brief Host-supplied target image facts for content-changing transfer.
 *
 * These values describe the actual output image buffer/container, not the
 * source metadata. When present, prepared transfer uses these target facts for
 * generated EXIF/XMP image-layout fields after filtering stale source fields.
 * Leave fields unset when the host cannot guarantee that the value matches the
 * target pixels.
 */
struct TransferTargetImageSpec final {
    bool has_dimensions = false;
    uint32_t width      = 0U;
    uint32_t height     = 0U;

    bool has_orientation = false;
    uint16_t orientation = 1U;

    bool has_samples_per_pixel = false;
    uint16_t samples_per_pixel = 0U;

    uint16_t bits_per_sample_count = 0U;
    std::array<uint16_t, kTransferTargetImageSpecMaxSamples> bits_per_sample {};

    uint16_t sample_format_count = 0U;
    std::array<uint16_t, kTransferTargetImageSpecMaxSamples> sample_format {};

    bool has_photometric_interpretation = false;
    uint16_t photometric_interpretation = 0U;

    bool has_planar_configuration = false;
    uint16_t planar_configuration = 1U;

    bool has_compression = false;
    uint16_t compression = 0U;

    bool has_exif_color_space = false;
    uint16_t exif_color_space = 0U;
};

/// Request options for preparation.
struct PrepareTransferRequest final {
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    DngTargetMode dng_target_mode      = DngTargetMode::MinimalFreshScaffold;
    TransferProfile profile;
    TransferTargetImageSpec target_image_spec;
    /// Optional source pixel-storage context. When this says source pixels are
    /// rendered, prepare drops source RAW-processing metadata even for
    /// compatible-file transfer because it no longer applies to stored pixels.
    bool has_source_raw_data_descriptor = false;
    MetadataRawDataDescriptor source_raw_data_descriptor;
    /// Disabled by default. Currently used only for snapshot preparation.
    TransferRawCarrierPassthroughMode raw_carrier_passthrough_mode
        = TransferRawCarrierPassthroughMode::Disabled;
    bool include_exif_app1    = true;
    bool include_xmp_app1     = true;
    bool include_icc_app2     = true;
    bool include_iptc_app13   = true;
    bool xmp_portable         = true;
    bool xmp_project_exif     = true;
    bool xmp_project_iptc     = true;
    bool xmp_include_existing = true;
    XmpExistingNamespacePolicy xmp_existing_namespace_policy
        = XmpExistingNamespacePolicy::KnownPortableOnly;
    XmpExistingStandardNamespacePolicy xmp_existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::PreserveAll;
    XmpConflictPolicy xmp_conflict_policy = XmpConflictPolicy::CurrentBehavior;
    bool xmp_exiftool_gpsdatetime_alias   = false;
};

/// Result details for preparation.
struct PrepareTransferResult final {
    TransferStatus status    = TransferStatus::Ok;
    PrepareTransferCode code = PrepareTransferCode::None;
    uint32_t warnings        = 0;
    uint32_t errors          = 0;
    std::string message;
};

/// Result details for bundle emission.
struct EmitTransferResult final {
    TransferStatus status       = TransferStatus::Ok;
    EmitTransferCode code       = EmitTransferCode::None;
    uint32_t emitted            = 0;
    uint32_t skipped            = 0;
    uint32_t errors             = 0;
    uint32_t failed_block_index = 0xFFFFFFFFU;
    std::string message;
};

/// Draft JPEG edit strategy selection.
enum class JpegEditMode : uint8_t {
    Auto,
    InPlace,
    MetadataRewrite,
};

/// Options for JPEG edit planning.
struct PlanJpegEditOptions final {
    JpegEditMode mode        = JpegEditMode::Auto;
    bool require_in_place    = false;
    bool skip_empty_payloads = true;
    bool strip_existing_xmp  = false;
};

/// Planned JPEG edit summary (draft API).
struct JpegEditPlan final {
    TransferStatus status                    = TransferStatus::Ok;
    JpegEditMode requested_mode              = JpegEditMode::Auto;
    JpegEditMode selected_mode               = JpegEditMode::MetadataRewrite;
    bool in_place_possible                   = false;
    bool strip_existing_xmp                  = false;
    uint32_t emitted_segments                = 0;
    uint32_t replaced_segments               = 0;
    uint32_t appended_segments               = 0;
    uint32_t removed_existing_segments       = 0;
    uint32_t removed_existing_jumbf_segments = 0;
    uint32_t removed_existing_c2pa_segments  = 0;
    uint64_t input_size                      = 0;
    uint64_t output_size                     = 0;
    uint64_t leading_scan_end                = 0;
    std::string message;
};

/// Options for TIFF edit planning.
struct PlanTiffEditOptions final {
    /// If true, fail planning when the bundle has no TIFF-applicable updates.
    bool require_updates    = true;
    bool strip_existing_xmp = false;
};

/// Planned TIFF edit summary (draft API).
struct TiffEditPlan final {
    TransferStatus status   = TransferStatus::Ok;
    uint32_t tag_updates    = 0;
    bool has_exif_ifd       = false;
    bool strip_existing_xmp = false;
    uint64_t input_size     = 0;
    uint64_t output_size    = 0;
    std::string message;
};

/// One chunk in a packaged transfer output plan.
enum class TransferPackageChunkKind : uint8_t {
    SourceRange,
    PreparedTransferBlock,
    PreparedJpegSegment,
    InlineBytes,
};

/// Semantic metadata family carried by one transfer payload or package chunk.
enum class TransferSemanticKind : uint8_t {
    Unknown = 0,
    Exif,
    Xmp,
    Icc,
    Iptc,
    Jumbf,
    C2pa,
};

/// One deterministic output chunk for a packaged transfer write.
struct PreparedTransferPackageChunk final {
    TransferPackageChunkKind kind = TransferPackageChunkKind::SourceRange;
    uint64_t output_offset        = 0;
    uint64_t source_offset        = 0;
    uint64_t size                 = 0;
    uint32_t block_index          = 0xFFFFFFFFU;
    uint8_t jpeg_marker_code      = 0U;
    std::vector<std::byte> inline_bytes;
};

/// Deterministic chunk plan for a final transfer output.
struct PreparedTransferPackagePlan final {
    uint32_t contract_version          = kMetadataTransferContractVersion;
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    uint64_t input_size                = 0;
    uint64_t output_size               = 0;
    std::vector<PreparedTransferPackageChunk> chunks;
};

/// One owned output chunk materialized from a package plan.
struct PreparedTransferPackageBlob final {
    TransferPackageChunkKind kind = TransferPackageChunkKind::SourceRange;
    uint64_t output_offset        = 0;
    uint64_t source_offset        = 0;
    uint32_t block_index          = 0xFFFFFFFFU;
    uint8_t jpeg_marker_code      = 0U;
    std::string route;
    std::vector<std::byte> bytes;
};

/// One owned final-output batch independent from input bytes or bundle storage.
struct PreparedTransferPackageBatch final {
    uint32_t contract_version          = kMetadataTransferContractVersion;
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    uint64_t input_size                = 0;
    uint64_t output_size               = 0;
    std::vector<PreparedTransferPackageBlob> chunks;
};

/// One zero-copy semantic view over a persisted transfer package chunk.
struct PreparedTransferPackageView final {
    TransferSemanticKind semantic_kind = TransferSemanticKind::Unknown;
    std::string_view route;
    TransferPackageChunkKind package_kind
        = TransferPackageChunkKind::SourceRange;
    uint64_t output_offset   = 0;
    uint8_t jpeg_marker_code = 0U;
    std::span<const std::byte> bytes;
};

/// Replay callbacks for \ref replay_prepared_transfer_package_batch.
struct PreparedTransferPackageReplayCallbacks final {
    TransferStatus (*begin_batch)(void* user,
                                  TransferTargetFormat target_format,
                                  uint32_t chunk_count) noexcept
        = nullptr;
    TransferStatus (*emit_chunk)(
        void* user, const PreparedTransferPackageView* view) noexcept
        = nullptr;
    TransferStatus (*end_batch)(void* user,
                                TransferTargetFormat target_format) noexcept
        = nullptr;
    void* user = nullptr;
};

/// Result for target-neutral package-batch replay.
struct PreparedTransferPackageReplayResult final {
    TransferStatus status       = TransferStatus::Ok;
    EmitTransferCode code       = EmitTransferCode::None;
    uint32_t replayed           = 0;
    uint32_t failed_chunk_index = 0xFFFFFFFFU;
    std::string message;
};

/// Result for serializing or parsing persisted transfer package batches.
struct PreparedTransferPackageIoResult final {
    TransferStatus status = TransferStatus::Unsupported;
    EmitTransferCode code = EmitTransferCode::None;
    uint64_t bytes        = 0;
    uint32_t errors       = 0;
    std::string message;
};

/// One precompiled JPEG emit operation (route -> marker mapping).
struct PreparedJpegEmitOp final {
    uint32_t block_index = 0;
    uint8_t marker_code  = 0;
};

/// Reusable precompiled JPEG emit plan for a prepared transfer bundle.
struct PreparedJpegEmitPlan final {
    uint32_t contract_version = kMetadataTransferContractVersion;
    std::vector<PreparedJpegEmitOp> ops;
};

/// One precompiled TIFF emit operation (route -> TIFF tag mapping).
struct PreparedTiffEmitOp final {
    uint32_t block_index = 0;
    uint16_t tiff_tag    = 0;
};

/// Reusable precompiled TIFF emit plan for a prepared transfer bundle.
struct PreparedTiffEmitPlan final {
    uint32_t contract_version = kMetadataTransferContractVersion;
    std::vector<PreparedTiffEmitOp> ops;
};

/// Kind of precompiled JPEG XL emit operation.
enum class PreparedJxlEmitKind : uint8_t {
    Box,
    IccProfile,
};

/// One precompiled JPEG XL emit operation (route -> backend mapping).
struct PreparedJxlEmitOp final {
    PreparedJxlEmitKind kind     = PreparedJxlEmitKind::Box;
    uint32_t block_index         = 0;
    std::array<char, 4> box_type = { '\0', '\0', '\0', '\0' };
    bool compress                = false;
};

/// Reusable precompiled JPEG XL emit plan for a prepared transfer bundle.
struct PreparedJxlEmitPlan final {
    uint32_t contract_version = kMetadataTransferContractVersion;
    std::vector<PreparedJxlEmitOp> ops;
};

/// Encoder-side handoff view for prepared JPEG XL metadata.
struct PreparedJxlEncoderHandoffView final {
    uint32_t contract_version  = kMetadataTransferContractVersion;
    bool has_icc_profile       = false;
    uint32_t icc_block_index   = 0xFFFFFFFFU;
    uint64_t icc_profile_bytes = 0U;
    std::span<const std::byte> icc_profile;
    uint32_t box_count         = 0U;
    uint64_t box_payload_bytes = 0U;
};

/// Owned JXL encoder-side handoff independent from bundle payload storage.
struct PreparedJxlEncoderHandoff final {
    uint32_t contract_version  = kMetadataTransferContractVersion;
    bool has_icc_profile       = false;
    uint32_t icc_block_index   = 0xFFFFFFFFU;
    uint32_t box_count         = 0U;
    uint64_t box_payload_bytes = 0U;
    std::vector<std::byte> icc_profile;
};

/// Result for serializing or parsing persisted JXL encoder handoff data.
struct PreparedJxlEncoderHandoffIoResult final {
    TransferStatus status = TransferStatus::Unsupported;
    EmitTransferCode code = EmitTransferCode::None;
    uint64_t bytes        = 0U;
    uint32_t errors       = 0U;
    std::string message;
};

/// Persisted transfer artifact family recognized by the generic inspect path.
enum class PreparedTransferArtifactKind : uint8_t {
    Unknown = 0,
    TransferPayloadBatch,
    TransferPackageBatch,
    C2paHandoffPackage,
    C2paSignedPackage,
    JxlEncoderHandoff,
};

/// Common summary for one persisted transfer artifact.
struct PreparedTransferArtifactInfo final {
    PreparedTransferArtifactKind kind  = PreparedTransferArtifactKind::Unknown;
    bool has_contract_version          = false;
    uint32_t contract_version          = 0U;
    bool has_target_format             = false;
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    uint32_t entry_count               = 0U;
    uint64_t payload_bytes             = 0U;
    uint64_t binding_bytes             = 0U;
    uint64_t signed_payload_bytes      = 0U;
    bool has_icc_profile               = false;
    uint32_t icc_block_index           = 0xFFFFFFFFU;
    uint64_t icc_profile_bytes         = 0U;
    uint64_t box_payload_bytes         = 0U;
    std::string carrier_route;
    std::string manifest_label;
};

/// Result for identifying and inspecting one persisted transfer artifact.
struct PreparedTransferArtifactIoResult final {
    TransferStatus status = TransferStatus::Unsupported;
    EmitTransferCode code = EmitTransferCode::None;
    uint64_t bytes        = 0U;
    uint32_t errors       = 0U;
    std::string message;
};

/// One precompiled WebP emit operation (route -> RIFF chunk mapping).
struct PreparedWebpEmitOp final {
    uint32_t block_index           = 0;
    std::array<char, 4> chunk_type = { '\0', '\0', '\0', '\0' };
};

/// Reusable precompiled WebP emit plan for a prepared transfer bundle.
struct PreparedWebpEmitPlan final {
    uint32_t contract_version = kMetadataTransferContractVersion;
    std::vector<PreparedWebpEmitOp> ops;
};

/// One precompiled PNG emit operation (route -> PNG chunk mapping).
struct PreparedPngEmitOp final {
    uint32_t block_index           = 0;
    std::array<char, 4> chunk_type = { '\0', '\0', '\0', '\0' };
};

/// Reusable precompiled PNG emit plan for a prepared transfer bundle.
struct PreparedPngEmitPlan final {
    uint32_t contract_version = kMetadataTransferContractVersion;
    std::vector<PreparedPngEmitOp> ops;
};

/// One precompiled JP2 emit operation (route -> JP2 box mapping).
struct PreparedJp2EmitOp final {
    uint32_t block_index         = 0;
    std::array<char, 4> box_type = { '\0', '\0', '\0', '\0' };
};

/// Reusable precompiled JP2 emit plan for a prepared transfer bundle.
struct PreparedJp2EmitPlan final {
    uint32_t contract_version = kMetadataTransferContractVersion;
    std::vector<PreparedJp2EmitOp> ops;
};

/// One precompiled EXR emit operation (prepared block -> EXR attribute).
struct PreparedExrEmitOp final {
    uint32_t block_index = 0;
};

/// Reusable precompiled EXR emit plan for a prepared transfer bundle.
struct PreparedExrEmitPlan final {
    uint32_t contract_version = kMetadataTransferContractVersion;
    std::vector<PreparedExrEmitOp> ops;
};

/// Kind of precompiled ISO-BMFF metadata emit operation.
enum class PreparedBmffEmitKind : uint8_t {
    Item,
    MimeXmp,
    Property,
};

/// One precompiled ISO-BMFF metadata emit operation.
struct PreparedBmffEmitOp final {
    PreparedBmffEmitKind kind = PreparedBmffEmitKind::Item;
    uint32_t block_index      = 0;
    uint32_t item_type        = 0U;
    uint32_t property_type    = 0U;
    uint32_t property_subtype = 0U;
};

/// Reusable precompiled ISO-BMFF metadata emit plan.
struct PreparedBmffEmitPlan final {
    uint32_t contract_version = kMetadataTransferContractVersion;
    std::vector<PreparedBmffEmitOp> ops;
};

/// Emission options shared by backend emit entry points.
struct EmitTransferOptions final {
    bool skip_empty_payloads = true;
    bool stop_on_error       = true;
};

/// Reusable compiled execution plan for high-throughput transfer workflows.
struct PreparedTransferExecutionPlan final {
    uint32_t contract_version          = kMetadataTransferContractVersion;
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    EmitTransferOptions emit;
    PreparedJpegEmitPlan jpeg_emit;
    PreparedTiffEmitPlan tiff_emit;
    PreparedJxlEmitPlan jxl_emit;
    PreparedWebpEmitPlan webp_emit;
    PreparedPngEmitPlan png_emit;
    PreparedJp2EmitPlan jp2_emit;
    PreparedExrEmitPlan exr_emit;
    PreparedBmffEmitPlan bmff_emit;
};

/// One normalized adapter operation for external encoder or host integration.
enum class TransferAdapterOpKind : uint8_t {
    JpegMarker,
    TiffTagBytes,
    JxlBox,
    JxlIccProfile,
    WebpChunk,
    PngChunk,
    Jp2Box,
    ExrAttribute,
    BmffItem,
    BmffProperty,
};

/// One compiled adapter-facing operation derived from a prepared bundle.
struct PreparedTransferAdapterOp final {
    TransferAdapterOpKind kind     = TransferAdapterOpKind::JpegMarker;
    uint32_t block_index           = 0U;
    uint64_t payload_size          = 0U;
    uint64_t serialized_size       = 0U;
    uint8_t jpeg_marker_code       = 0U;
    uint16_t tiff_tag              = 0U;
    std::array<char, 4> box_type   = { '\0', '\0', '\0', '\0' };
    std::array<char, 4> chunk_type = { '\0', '\0', '\0', '\0' };
    uint32_t bmff_item_type        = 0U;
    uint32_t bmff_property_type    = 0U;
    uint32_t bmff_property_subtype = 0U;
    bool bmff_mime_xmp             = false;
    bool compress                  = false;
};

/// One target-neutral adapter view over prepared transfer operations.
struct PreparedTransferAdapterView final {
    uint32_t contract_version = kPreparedTransferAdapterContractVersion;
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    EmitTransferOptions emit;
    std::vector<PreparedTransferAdapterOp> ops;
};

/// One zero-copy semantic payload view over a prepared transfer bundle.
struct PreparedTransferPayloadView final {
    TransferSemanticKind semantic_kind = TransferSemanticKind::Unknown;
    std::string_view semantic_name;
    std::string_view route;
    PreparedTransferAdapterOp op;
    std::span<const std::byte> payload;
};

/// One owned semantic payload copied from a prepared transfer bundle.
struct PreparedTransferPayload final {
    TransferSemanticKind semantic_kind = TransferSemanticKind::Unknown;
    std::string semantic_name;
    std::string route;
    PreparedTransferAdapterOp op;
    std::vector<std::byte> payload;
};

/// One owned semantic payload batch independent from bundle payload storage.
struct PreparedTransferPayloadBatch final {
    uint32_t contract_version          = kMetadataTransferContractVersion;
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    EmitTransferOptions emit;
    std::vector<PreparedTransferPayload> payloads;
};

/// Result for serializing or parsing persisted transfer payload batches.
struct PreparedTransferPayloadIoResult final {
    TransferStatus status = TransferStatus::Unsupported;
    EmitTransferCode code = EmitTransferCode::None;
    uint64_t bytes        = 0;
    uint32_t errors       = 0;
    std::string message;
};

/// Replay callbacks for \ref replay_prepared_transfer_payload_batch.
struct PreparedTransferPayloadReplayCallbacks final {
    TransferStatus (*begin_batch)(void* user,
                                  TransferTargetFormat target_format,
                                  uint32_t payload_count) noexcept
        = nullptr;
    TransferStatus (*emit_payload)(
        void* user, const PreparedTransferPayloadView* view) noexcept
        = nullptr;
    TransferStatus (*end_batch)(void* user,
                                TransferTargetFormat target_format) noexcept
        = nullptr;
    void* user = nullptr;
};

/// Result for target-neutral payload-batch replay.
struct PreparedTransferPayloadReplayResult final {
    TransferStatus status         = TransferStatus::Ok;
    EmitTransferCode code         = EmitTransferCode::None;
    uint32_t replayed             = 0;
    uint32_t failed_payload_index = 0xFFFFFFFFU;
    std::string message;
};

/// Generic host-side sink for adapter-view transfer operations.
class TransferAdapterSink {
public:
    virtual ~TransferAdapterSink() = default;
    virtual TransferStatus emit_op(const PreparedTransferAdapterOp& op,
                                   std::span<const std::byte> payload) noexcept
        = 0;
};

/// Existing sibling XMP sidecar handling for transfer preparation.
enum class XmpExistingSidecarMode : uint8_t {
    Ignore,
    MergeIfPresent,
};

/// Conflict precedence between a merged destination-side `.xmp` and
/// source-embedded existing XMP.
enum class XmpExistingSidecarPrecedence : uint8_t {
    SidecarWins,
    SourceWins,
};

/// Existing destination embedded-XMP handling for file-helper transfer
/// execution.
enum class XmpExistingDestinationEmbeddedMode : uint8_t {
    Ignore,
    MergeIfPresent,
};

/// Conflict precedence between merged destination embedded XMP and
/// source-embedded existing XMP.
enum class XmpExistingDestinationEmbeddedPrecedence : uint8_t {
    DestinationWins,
    SourceWins,
};

/// Conflict precedence between existing destination sidecar XMP and existing
/// destination embedded XMP when both are merged during transfer preparation.
enum class XmpExistingDestinationCarrierPrecedence : uint8_t {
    SidecarWins,
    EmbeddedWins,
};

/**
 * \brief File-read + decode options for \ref read_transfer_source_snapshot_file.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
struct ReadTransferSourceSnapshotFileOptions final {
    bool include_pointer_tags       = true;
    bool decode_makernote           = false;
    bool decode_embedded_containers = true;
    bool decompress                 = true;
    bool preserve_raw_carriers      = false;
    uint64_t max_raw_carrier_bytes  = 64ULL * 1024ULL * 1024ULL;
    OpenMetaResourcePolicy policy;
};

/// Generic decode options for transfer source snapshot reads.
using ReadTransferSourceSnapshotOptions = ReadTransferSourceSnapshotFileOptions;

/**
 * \brief High-level file read result for \ref read_transfer_source_snapshot_file.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
struct ReadTransferSourceSnapshotFileResult final {
    TransferFileStatus file_status = TransferFileStatus::Ok;
    ReadTransferSourceSnapshotFileCode code
        = ReadTransferSourceSnapshotFileCode::None;
    uint64_t file_size               = 0;
    uint32_t entry_count             = 0;
    uint32_t raw_carrier_count       = 0U;
    uint64_t raw_carrier_bytes       = 0U;
    bool raw_carrier_bytes_truncated = false;
    SimpleMetaResult read;
    TransferSourceSnapshot snapshot;
};

/**
 * \brief High-level in-memory read result for \ref read_transfer_source_snapshot_bytes.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
struct ReadTransferSourceSnapshotBytesResult final {
    TransferStatus status = TransferStatus::Ok;
    ReadTransferSourceSnapshotBytesCode code
        = ReadTransferSourceSnapshotBytesCode::None;
    uint64_t input_size              = 0;
    uint32_t entry_count             = 0;
    uint32_t raw_carrier_count       = 0U;
    uint64_t raw_carrier_bytes       = 0U;
    bool raw_carrier_bytes_truncated = false;
    SimpleMetaResult read;
    TransferSourceSnapshot snapshot;
};

/// Caller-owned transient storage for positional source snapshot assembly.
struct ReadTransferSourceSnapshotRandomAccessScratch final {
    std::span<ContainerBlockRef> blocks;
    std::span<ExifIfdRef> ifds;
    std::span<uint32_t> payload_indices;
    /// Structural scanner/decoder read-ahead cache.
    std::span<std::byte> read_window;
    /// One reassembled or directly fetched logical metadata payload.
    std::span<std::byte> payload;
    /// One compressed logical stream before decompression.
    std::span<std::byte> compressed_payload;
    /// One EXR/TIFF attribute or out-of-line metadata value.
    std::span<std::byte> value;
    RandomAccessReadWindowOptions window_options;
};

/// Options for bounded positional source snapshot assembly.
struct ReadTransferSourceSnapshotRandomAccessOptions final {
    bool include_pointer_tags = true;
    bool decode_makernote     = false;
    /// Embedded-container recursion is opt-in for positional workflows. Paths
    /// not yet converted are counted as explicit residuals.
    bool decode_embedded_containers = false;
    bool decompress                 = true;
    bool preserve_raw_carriers      = false;
    uint64_t max_raw_carrier_bytes  = 64ULL * 1024ULL * 1024ULL;
    OpenMetaResourcePolicy policy;
};

/// Result of bounded positional source snapshot assembly.
struct ReadTransferSourceSnapshotRandomAccessResult final {
    TransferStatus status = TransferStatus::Ok;
    ReadTransferSourceSnapshotRandomAccessCode code
        = ReadTransferSourceSnapshotRandomAccessCode::None;
    ContainerFormat format           = ContainerFormat::Unknown;
    uint64_t input_size              = 0U;
    uint32_t entry_count             = 0U;
    uint32_t raw_carrier_count       = 0U;
    uint64_t raw_carrier_bytes       = 0U;
    bool raw_carrier_bytes_truncated = false;
    /// Source-wide enrichment or container-specific decode paths that were
    /// intentionally not run by this bounded payload-backed assembly path.
    uint32_t residual_metadata_paths   = 0U;
    uint64_t payload_scratch_needed    = 0U;
    uint64_t compressed_scratch_needed = 0U;
    uint64_t value_scratch_needed      = 0U;
    SimpleMetaResult read;
    RandomAccessReadState input;
    TransferSourceSnapshot snapshot;

    bool complete() const noexcept
    {
        return status == TransferStatus::Ok
               && code == ReadTransferSourceSnapshotRandomAccessCode::None
               && input.ok() && residual_metadata_paths == 0U
               && payload_scratch_needed == 0U
               && compressed_scratch_needed == 0U && value_scratch_needed == 0U;
    }
};

/// Requested optional lanes used to classify positional read residuals.
struct ReadTransferSourceDiagnosticOptions final {
    bool decode_makernote_requested           = false;
    bool decode_embedded_containers_requested = false;
};

/// One allocation-free diagnostic projected from a positional read result.
struct ReadTransferSourceDiagnostic final {
    ReadTransferSourceDiagnosticSeverity severity
        = ReadTransferSourceDiagnosticSeverity::Error;
    ReadTransferSourceDiagnosticDomain domain
        = ReadTransferSourceDiagnosticDomain::Input;
    ReadTransferSourceDiagnosticCode code
        = ReadTransferSourceDiagnosticCode::InputFailure;
    ContainerFormat format          = ContainerFormat::Unknown;
    uint64_t offset                 = 0U;
    uint64_t required_bytes         = 0U;
    uint32_t count                  = 0U;
    uint16_t tag                    = 0U;
    RandomAccessReadCode input_code = RandomAccessReadCode::Ok;
};

/// Caller-buffer result for structured positional read diagnostics.
struct ReadTransferSourceDiagnosticsResult final {
    uint32_t written = 0U;
    uint32_t needed  = 0U;

    bool complete() const noexcept { return written == needed; }
};

/// File-read + decode options for \ref prepare_metadata_for_target_file.
struct PrepareTransferFileOptions final {
    bool include_pointer_tags       = true;
    bool decode_makernote           = false;
    bool decode_embedded_containers = true;
    bool decompress                 = true;
    std::string xmp_existing_sidecar_base_path;
    std::string xmp_existing_destination_embedded_path;
    XmpExistingSidecarMode xmp_existing_sidecar_mode
        = XmpExistingSidecarMode::Ignore;
    XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence
        = XmpExistingSidecarPrecedence::SidecarWins;
    XmpExistingDestinationEmbeddedMode xmp_existing_destination_embedded_mode
        = XmpExistingDestinationEmbeddedMode::Ignore;
    XmpExistingDestinationEmbeddedPrecedence
        xmp_existing_destination_embedded_precedence
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins;
    XmpExistingDestinationCarrierPrecedence
        xmp_existing_destination_carrier_precedence
        = XmpExistingDestinationCarrierPrecedence::SidecarWins;

    OpenMetaResourcePolicy policy;
    PrepareTransferRequest prepare;
};

/// High-level file read/prepare result.
struct PrepareTransferFileResult final {
    TransferFileStatus file_status             = TransferFileStatus::Ok;
    PrepareTransferFileCode code               = PrepareTransferFileCode::None;
    uint64_t file_size                         = 0;
    uint32_t entry_count                       = 0;
    bool xmp_existing_sidecar_loaded           = false;
    TransferStatus xmp_existing_sidecar_status = TransferStatus::Unsupported;
    std::string xmp_existing_sidecar_message;
    std::string xmp_existing_sidecar_path;
    bool xmp_existing_destination_embedded_loaded = false;
    TransferStatus xmp_existing_destination_embedded_status
        = TransferStatus::Unsupported;
    std::string xmp_existing_destination_embedded_message;
    std::string xmp_existing_destination_embedded_path;

    SimpleMetaResult read;
    PrepareTransferResult prepare;
    PreparedTransferBundle bundle;
};

/// One time patch update payload for \ref apply_time_patches.
struct TimePatchUpdate final {
    TimePatchField field = TimePatchField::DateTime;
    std::vector<std::byte> value;
};

/// Non-owning time patch view for hot-path transfer execution.
struct TimePatchView final {
    TimePatchField field = TimePatchField::DateTime;
    std::span<const std::byte> value;
};

/// Options for \ref apply_time_patches.
struct ApplyTimePatchOptions final {
    /// If true, update value size must match slot width exactly.
    bool strict_width = true;
    /// If true, each requested field must have at least one slot.
    bool require_slot = false;
};

/// Result for \ref apply_time_patches.
struct ApplyTimePatchResult final {
    TransferStatus status  = TransferStatus::Ok;
    uint32_t patched_slots = 0;
    uint32_t skipped_slots = 0;
    uint32_t errors        = 0;
    std::string message;
};

/// One high-level time patch input for transfer execution helpers.
struct TransferTimePatchInput final {
    TimePatchField field = TimePatchField::DateTime;
    std::vector<std::byte> value;
    bool text_value = false;
};

/// One emitted JPEG marker summary entry.
struct EmittedJpegMarkerSummary final {
    uint8_t marker = 0;
    uint32_t count = 0;
    uint64_t bytes = 0;
};

/// One emitted TIFF tag summary entry.
struct EmittedTiffTagSummary final {
    uint16_t tag   = 0;
    uint32_t count = 0;
    uint64_t bytes = 0;
};

/// One emitted JPEG XL box summary entry.
struct EmittedJxlBoxSummary final {
    std::array<char, 4> type = { '\0', '\0', '\0', '\0' };
    uint32_t count           = 0;
    uint64_t bytes           = 0;
};

/// One emitted WebP chunk summary entry.
struct EmittedWebpChunkSummary final {
    std::array<char, 4> type = { '\0', '\0', '\0', '\0' };
    uint32_t count           = 0;
    uint64_t bytes           = 0;
};

/// One emitted PNG chunk summary entry.
struct EmittedPngChunkSummary final {
    std::array<char, 4> type = { '\0', '\0', '\0', '\0' };
    uint32_t count           = 0;
    uint64_t bytes           = 0;
};

/// One emitted JP2 box summary entry.
struct EmittedJp2BoxSummary final {
    std::array<char, 4> type = { '\0', '\0', '\0', '\0' };
    uint32_t count           = 0;
    uint64_t bytes           = 0;
};

/// One emitted EXR attribute summary entry.
struct EmittedExrAttributeSummary final {
    std::string name;
    std::string type_name;
    uint32_t count = 0;
    uint64_t bytes = 0;
};

/// One emitted ISO-BMFF metadata item summary entry.
struct EmittedBmffItemSummary final {
    uint32_t item_type = 0U;
    uint32_t count     = 0U;
    uint64_t bytes     = 0U;
    bool mime_xmp      = false;
};

/// One emitted ISO-BMFF metadata property summary entry.
struct EmittedBmffPropertySummary final {
    uint32_t property_type    = 0U;
    uint32_t property_subtype = 0U;
    uint32_t count            = 0U;
    uint64_t bytes            = 0U;
};

/// Streaming byte sink for edit/write transfer paths.
class TransferByteWriter {
public:
    virtual ~TransferByteWriter() = default;
    virtual TransferStatus write(std::span<const std::byte> bytes) noexcept = 0;
    virtual uint64_t remaining_capacity_hint() const noexcept
    {
        return UINT64_MAX;
    }
};

/// Fixed-buffer writer for encoder integrations with preallocated memory.
class SpanTransferByteWriter final : public TransferByteWriter {
public:
    explicit SpanTransferByteWriter(std::span<std::byte> buffer) noexcept
        : buffer_(buffer)
    {
    }

    TransferStatus write(std::span<const std::byte> bytes) noexcept override
    {
        if (status_ != TransferStatus::Ok) {
            return status_;
        }
        if (bytes.empty()) {
            return TransferStatus::Ok;
        }
        if (bytes.size() > remaining()) {
            status_ = TransferStatus::LimitExceeded;
            return status_;
        }
        std::memcpy(buffer_.data() + written_, bytes.data(), bytes.size());
        written_ += bytes.size();
        return TransferStatus::Ok;
    }

    void reset() noexcept
    {
        written_ = 0U;
        status_  = TransferStatus::Ok;
    }

    size_t capacity() const noexcept { return buffer_.size(); }
    size_t bytes_written() const noexcept { return written_; }
    size_t remaining() const noexcept { return buffer_.size() - written_; }
    TransferStatus status() const noexcept { return status_; }
    uint64_t remaining_capacity_hint() const noexcept override
    {
        return static_cast<uint64_t>(remaining());
    }

    std::span<const std::byte> written_bytes() const noexcept
    {
        return std::span<const std::byte>(buffer_.data(), written_);
    }

private:
    std::span<std::byte> buffer_;
    size_t written_        = 0U;
    TransferStatus status_ = TransferStatus::Ok;
};

/// Options for \ref execute_prepared_transfer.
struct ExecutePreparedTransferOptions final {
    std::vector<TransferTimePatchInput> time_patches;
    ApplyTimePatchOptions time_patch;
    bool time_patch_auto_nul = true;

    EmitTransferOptions emit;
    uint32_t emit_repeat = 1;
    /// Optional metadata-only sink. BMFF targets write an ordered
    /// item/property payload handoff, not a standalone BMFF container.
    TransferByteWriter* emit_output_writer = nullptr;

    bool edit_requested                    = false;
    bool edit_apply                        = false;
    TransferByteWriter* edit_output_writer = nullptr;
    bool strip_existing_xmp                = false;
    PlanJpegEditOptions jpeg_edit;
    PlanTiffEditOptions tiff_edit;
};

/// Result for \ref execute_prepared_transfer.
struct ExecutePreparedTransferResult final {
    bool c2pa_stage_requested = false;
    ValidatePreparedC2paSignResult c2pa_stage_validation;
    EmitTransferResult c2pa_stage;

    ApplyTimePatchResult time_patch;

    EmitTransferResult compile;
    uint32_t compiled_ops = 0;

    EmitTransferResult emit;
    std::vector<EmittedJpegMarkerSummary> marker_summary;
    std::vector<EmittedTiffTagSummary> tiff_tag_summary;
    std::vector<EmittedJxlBoxSummary> jxl_box_summary;
    std::vector<EmittedWebpChunkSummary> webp_chunk_summary;
    std::vector<EmittedPngChunkSummary> png_chunk_summary;
    std::vector<EmittedJp2BoxSummary> jp2_box_summary;
    std::vector<EmittedExrAttributeSummary> exr_attribute_summary;
    std::vector<EmittedBmffItemSummary> bmff_item_summary;
    std::vector<EmittedBmffPropertySummary> bmff_property_summary;
    bool tiff_commit          = false;
    uint64_t emit_output_size = 0;
    bool strip_existing_xmp   = false;

    bool edit_requested             = false;
    TransferStatus edit_plan_status = TransferStatus::Unsupported;
    std::string edit_plan_message;
    JpegEditPlan jpeg_edit_plan;
    TiffEditPlan tiff_edit_plan;
    EmitTransferResult edit_apply;
    uint64_t edit_input_size  = 0;
    uint64_t edit_output_size = 0;
    std::vector<std::byte> edited_output;
};

/// XMP carrier preference for file-helper transfer execution.
enum class XmpWritebackMode : uint8_t {
    /// Keep generated XMP only in the managed embedded carrier.
    EmbeddedOnly,
    /// Return generated XMP only as sibling `.xmp` output.
    ///
    /// Existing embedded XMP stays in the edited file unless
    /// \ref XmpDestinationEmbeddedMode::StripExisting is requested.
    SidecarOnly,
    /// Keep generated XMP in the managed embedded carrier and also return the
    /// same generated XMP packet for sibling `.xmp` writeback.
    EmbeddedAndSidecar,
};

/// Destination embedded-XMP handling for file-helper transfer execution.
enum class XmpDestinationEmbeddedMode : uint8_t {
    /// Leave existing embedded XMP in the edited file when sidecar-only
    /// writeback suppresses generated embedded XMP.
    PreserveExisting,
    /// Remove managed embedded XMP from the edited file when sidecar-only
    /// writeback is selected.
    StripExisting,
};

/// Destination sibling XMP sidecar handling for file-helper transfer
/// execution.
enum class XmpDestinationSidecarMode : uint8_t {
    /// Leave an existing sibling `.xmp` untouched when embedded-only
    /// writeback is selected.
    PreserveExisting,
    /// Request removal of an existing sibling `.xmp` when embedded-only
    /// writeback is selected.
    StripExisting,
};

/// Host-reported presence of an existing destination sibling `.xmp` sidecar.
enum class XmpExistingDestinationSidecarState : uint8_t {
    /// Use path-based detection when a sidecar base path is available.
    Unknown,
    /// Host knows there is no existing destination sidecar.
    NotPresent,
    /// Host knows there is an existing destination sidecar to remove.
    Present,
};

/// Options for \ref execute_prepared_transfer_file.
struct ExecutePreparedTransferFileOptions final {
    PrepareTransferFileOptions prepare;
    ExecutePreparedTransferOptions execute;
    std::string edit_target_path;
    /// Base path used to derive sibling `.xmp` output or cleanup paths.
    ///
    /// When empty, the file-helper derives the sidecar path from
    /// \ref edit_target_path.
    std::string xmp_sidecar_base_path;
    XmpExistingDestinationEmbeddedMode xmp_existing_destination_embedded_mode
        = XmpExistingDestinationEmbeddedMode::Ignore;
    XmpExistingDestinationEmbeddedPrecedence
        xmp_existing_destination_embedded_precedence
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins;
    XmpWritebackMode xmp_writeback_mode = XmpWritebackMode::EmbeddedOnly;
    XmpDestinationEmbeddedMode xmp_destination_embedded_mode
        = XmpDestinationEmbeddedMode::PreserveExisting;
    XmpDestinationSidecarMode xmp_destination_sidecar_mode
        = XmpDestinationSidecarMode::PreserveExisting;
    XmpExistingDestinationSidecarState xmp_existing_destination_sidecar_state
        = XmpExistingDestinationSidecarState::Unknown;
    bool c2pa_stage_requested = false;
    PreparedTransferC2paSignerInput c2pa_signer_input;
    bool c2pa_signed_package_provided = false;
    PreparedTransferC2paSignedPackage c2pa_signed_package;
};

/// Options for \ref execute_prepared_transfer_snapshot.
struct ExecutePreparedTransferSnapshotOptions final {
    PrepareTransferRequest prepare;
    ExecutePreparedTransferOptions execute;
    OpenMetaResourcePolicy policy;
    std::string edit_target_path;
    /// Base path used to load one existing sibling `.xmp` sidecar for XMP
    /// merge during bundle preparation.
    ///
    /// When empty, the helper uses \ref edit_target_path.
    std::string xmp_existing_sidecar_base_path;
    /// Base path used to derive sibling `.xmp` output or cleanup paths.
    ///
    /// When empty, the helper derives the sidecar path from
    /// \ref edit_target_path.
    std::string xmp_sidecar_base_path;
    XmpExistingSidecarMode xmp_existing_sidecar_mode
        = XmpExistingSidecarMode::Ignore;
    XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence
        = XmpExistingSidecarPrecedence::SidecarWins;
    /// Optional path used to load existing destination embedded XMP before
    /// bundle preparation.
    ///
    /// When empty and destination embedded merge is requested, the helper uses
    /// \ref edit_target_path.
    std::string xmp_existing_destination_embedded_path;
    XmpExistingDestinationEmbeddedMode xmp_existing_destination_embedded_mode
        = XmpExistingDestinationEmbeddedMode::Ignore;
    XmpExistingDestinationEmbeddedPrecedence
        xmp_existing_destination_embedded_precedence
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins;
    XmpExistingDestinationCarrierPrecedence
        xmp_existing_destination_carrier_precedence
        = XmpExistingDestinationCarrierPrecedence::SidecarWins;
    XmpWritebackMode xmp_writeback_mode = XmpWritebackMode::EmbeddedOnly;
    XmpDestinationEmbeddedMode xmp_destination_embedded_mode
        = XmpDestinationEmbeddedMode::PreserveExisting;
    XmpDestinationSidecarMode xmp_destination_sidecar_mode
        = XmpDestinationSidecarMode::PreserveExisting;
    XmpExistingDestinationSidecarState xmp_existing_destination_sidecar_state
        = XmpExistingDestinationSidecarState::Unknown;
    bool c2pa_stage_requested = false;
    PreparedTransferC2paSignerInput c2pa_signer_input;
    bool c2pa_signed_package_provided = false;
    PreparedTransferC2paSignedPackage c2pa_signed_package;
};

/// Options for \ref execute_prepared_transfer_bundle.
struct ExecutePreparedTransferBundleOptions final {
    ExecutePreparedTransferOptions execute;
    OpenMetaResourcePolicy policy;
    std::string edit_target_path;
    std::string xmp_sidecar_base_path;
    XmpWritebackMode xmp_writeback_mode = XmpWritebackMode::EmbeddedOnly;
    XmpDestinationEmbeddedMode xmp_destination_embedded_mode
        = XmpDestinationEmbeddedMode::PreserveExisting;
    XmpDestinationSidecarMode xmp_destination_sidecar_mode
        = XmpDestinationSidecarMode::PreserveExisting;
    XmpExistingDestinationSidecarState xmp_existing_destination_sidecar_state
        = XmpExistingDestinationSidecarState::Unknown;
    bool c2pa_stage_requested = false;
    PreparedTransferC2paSignerInput c2pa_signer_input;
    bool c2pa_signed_package_provided = false;
    PreparedTransferC2paSignedPackage c2pa_signed_package;
};

/// Result for \ref execute_prepared_transfer_file.
struct ExecutePreparedTransferFileResult final {
    PrepareTransferFileResult prepared;
    ExecutePreparedTransferResult execute;
    bool xmp_existing_destination_embedded_loaded = false;
    TransferStatus xmp_existing_destination_embedded_status
        = TransferStatus::Unsupported;
    std::string xmp_existing_destination_embedded_message;
    std::string xmp_existing_destination_embedded_path;
    bool xmp_sidecar_requested        = false;
    TransferStatus xmp_sidecar_status = TransferStatus::Unsupported;
    std::string xmp_sidecar_message;
    std::string xmp_sidecar_path;
    std::vector<std::byte> xmp_sidecar_output;
    bool xmp_sidecar_cleanup_requested        = false;
    TransferStatus xmp_sidecar_cleanup_status = TransferStatus::Unsupported;
    std::string xmp_sidecar_cleanup_message;
    std::string xmp_sidecar_cleanup_path;
};

/// Options for \ref persist_prepared_transfer_file_result.
struct PersistPreparedTransferFileOptions final {
    std::string output_path;
    bool write_output                   = true;
    bool overwrite_output               = false;
    uint64_t prewritten_output_bytes    = 0;
    bool overwrite_xmp_sidecar          = false;
    bool remove_destination_xmp_sidecar = true;
};

/// Result for \ref persist_prepared_transfer_file_result.
struct PersistPreparedTransferFileResult final {
    TransferStatus status = TransferStatus::Unsupported;
    std::string message;

    TransferStatus output_status = TransferStatus::Unsupported;
    std::string output_message;
    std::string output_path;
    uint64_t output_bytes = 0;

    TransferStatus xmp_sidecar_status = TransferStatus::Unsupported;
    std::string xmp_sidecar_message;
    std::string xmp_sidecar_path;
    uint64_t xmp_sidecar_bytes = 0;

    TransferStatus xmp_sidecar_cleanup_status = TransferStatus::Unsupported;
    std::string xmp_sidecar_cleanup_message;
    std::string xmp_sidecar_cleanup_path;
    bool xmp_sidecar_cleanup_removed = false;
};

/**
 * \brief Persists edited output, generated XMP sidecar output, and any
 * requested destination-sidecar cleanup from
 * \ref execute_prepared_transfer_file.
 *
 * This is a bounded file helper for host applications that want the same
 * output/sidecar behavior as the CLI or Python wrapper without reimplementing
 * write and cleanup logic.
 */
PersistPreparedTransferFileResult
persist_prepared_transfer_file_result(
    const ExecutePreparedTransferFileResult& prepared,
    const PersistPreparedTransferFileOptions& options) noexcept;

/**
 * \brief Materializes the final persisted package batch for one executed
 * transfer state.
 *
 * For direct emit targets this builds the target-neutral emit package batch.
 * For JPEG/TIFF/BMFF edit flows this builds the rewrite package batch from the
 * selected edit state and input bytes.
 */
EmitTransferResult
build_executed_transfer_package_batch(
    std::span<const std::byte> edit_input, const PreparedTransferBundle& bundle,
    const ExecutePreparedTransferResult& execute,
    PreparedTransferPackageBatch* out_batch) noexcept;

/**
 * \brief Backend contract for JPEG metadata emission.
 */
class JpegTransferEmitter {
public:
    virtual ~JpegTransferEmitter() = default;
    virtual TransferStatus
    write_app_marker(uint8_t marker_code,
                     std::span<const std::byte> payload) noexcept
        = 0;
};

/**
 * \brief Backend contract for TIFF metadata emission.
 */
class TiffTransferEmitter {
public:
    virtual ~TiffTransferEmitter() = default;
    virtual TransferStatus set_tag_u32(uint16_t tag, uint32_t value) noexcept
        = 0;
    virtual TransferStatus
    set_tag_bytes(uint16_t tag, std::span<const std::byte> payload) noexcept
        = 0;
    virtual TransferStatus
    commit_exif_directory(uint64_t* out_ifd_offset) noexcept
        = 0;
};

/**
 * \brief Backend contract for JPEG XL metadata box emission.
 */
class JxlTransferEmitter {
public:
    virtual ~JxlTransferEmitter() = default;
    virtual TransferStatus
    set_icc_profile(std::span<const std::byte> payload) noexcept
        = 0;
    virtual TransferStatus add_box(std::array<char, 4> type,
                                   std::span<const std::byte> payload,
                                   bool compress) noexcept
        = 0;
    virtual TransferStatus close_boxes() noexcept = 0;
};

/**
 * \brief Backend contract for WebP metadata chunk emission.
 */
class WebpTransferEmitter {
public:
    virtual ~WebpTransferEmitter() = default;
    virtual TransferStatus
    add_chunk(std::array<char, 4> type,
              std::span<const std::byte> payload) noexcept
        = 0;
    virtual TransferStatus close_chunks() noexcept = 0;
};

/**
 * \brief Backend contract for PNG metadata chunk emission.
 */
class PngTransferEmitter {
public:
    virtual ~PngTransferEmitter() = default;
    virtual TransferStatus
    add_chunk(std::array<char, 4> type,
              std::span<const std::byte> payload) noexcept
        = 0;
    virtual TransferStatus close_chunks() noexcept = 0;
};

/**
 * \brief Backend contract for JP2 metadata box emission.
 */
class Jp2TransferEmitter {
public:
    virtual ~Jp2TransferEmitter() = default;
    virtual TransferStatus add_box(std::array<char, 4> type,
                                   std::span<const std::byte> payload) noexcept
        = 0;
    virtual TransferStatus close_boxes() noexcept = 0;
};

/**
 * \brief Backend contract for ISO-BMFF metadata item emission.
 */
class BmffTransferEmitter {
public:
    virtual ~BmffTransferEmitter() = default;
    virtual TransferStatus add_item(uint32_t item_type,
                                    std::span<const std::byte> payload) noexcept
        = 0;
    virtual TransferStatus
    add_mime_xmp_item(std::span<const std::byte> payload) noexcept
        = 0;
    virtual TransferStatus
    add_property(uint32_t property_type,
                 std::span<const std::byte> payload) noexcept
        = 0;
    virtual TransferStatus close_items() noexcept = 0;
};

/// EXR attribute payload for transfer emission.
struct ExrPreparedAttribute final {
    std::string name;
    std::string type_name;
    std::vector<std::byte> value;
    bool is_opaque = false;
};

/// Zero-copy EXR attribute view for prepared transfer emission.
struct ExrPreparedAttributeView final {
    std::string_view name;
    std::string_view type_name;
    std::span<const std::byte> value;
    bool is_opaque = false;
};

/**
 * \brief Backend contract for OpenEXR header attribute emission.
 */
class ExrTransferEmitter {
public:
    virtual ~ExrTransferEmitter() = default;
    virtual TransferStatus
    set_attribute(const ExrPreparedAttribute& attr) noexcept
        = 0;
    virtual TransferStatus
    set_attribute_view(const ExrPreparedAttributeView& attr) noexcept
    {
        ExrPreparedAttribute owned;
        owned.name.assign(attr.name.data(), attr.name.size());
        owned.type_name.assign(attr.type_name.data(), attr.type_name.size());
        owned.value.assign(attr.value.begin(), attr.value.end());
        owned.is_opaque = attr.is_opaque;
        return set_attribute(owned);
    }
};

/// \brief Draft bundle preparation entry point.
PrepareTransferResult
prepare_metadata_for_target(const MetaStore&, const PrepareTransferRequest&,
                            PreparedTransferBundle* out_bundle) noexcept;

/**
 * \brief Prepare a target-specific transfer bundle from a decoded source snapshot.
 *
 * This is the fileless counterpart to \ref prepare_metadata_for_target_file
 * for host applications that already hold a reusable decoded source snapshot
 * from an earlier read.
 *
 * Current snapshot execution is decoded-store-backed. Opt-in raw carrier
 * payloads can be retained in the snapshot for host diagnostics and future
 * passthrough policy, but are not consumed by this preparation entry point yet.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
PrepareTransferResult
prepare_metadata_for_target_snapshot(
    const TransferSourceSnapshot& snapshot,
    const PrepareTransferRequest& request,
    PreparedTransferBundle* out_bundle) noexcept;

/**
 * \brief Build a reusable decoded source snapshot from an existing \ref MetaStore.
 *
 * The snapshot stores its own finalized copy of the input store so later
 * transfer preparation or execution does not depend on the caller keeping the
 * original store alive or immutable.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
TransferSourceSnapshot
build_transfer_source_snapshot(const MetaStore& store) noexcept;

/**
 * \brief Summarize metadata that a transfer safety mode will keep, filter, or
 * invalidate before writing a target.
 *
 * \par API Stability
 * Experimental host-facing API for diagnostics, UI, and preflight decisions.
 */
TransferSafetyAudit
transfer_safety_audit_from_store(const MetaStore& store,
                                 TransferSafetyMode safety) noexcept;

/**
 * \brief Report MakerNote source evidence and the current writer trust bound.
 *
 * A raw `ExifIFD:MakerNote` value can be carried forward as opaque bytes, but
 * OpenMeta does not currently relocate vendor-private offsets, rebuild a note
 * from decoded sub-IFDs, recalculate vendor checksums, or verify that a moved
 * note remains semantically readable. Generic raw-carrier passthrough is also
 * intentionally unavailable for MakerNotes.
 *
 * \par API Stability
 * Experimental host-facing API for diagnostics, UI, and preflight decisions.
 */
TransferMakerNoteAudit
makernote_transfer_audit_from_store(const MetaStore& store) noexcept;

/**
 * \brief Classify raw MakerNote layouts that affect offset-safe transfer.
 *
 * The first bounded implementation recognizes canonical Nikon type 1 notes,
 * whose offsets depend on the outer TIFF, and Nikon type 3 notes containing an
 * embedded TIFF at byte 10. A valid embedded TIFF proves only that standard
 * TIFF directory/value offsets remain inside that payload. Vendor-private
 * binary offsets, checksums, and semantic readability are not validated.
 *
 * \par API Stability
 * Experimental host-facing API for diagnostics, UI, and preflight decisions.
 */
TransferMakerNoteLayoutAudit
makernote_layout_transfer_audit_from_store(const MetaStore& store) noexcept;

TransferConceptDiagnostics
transfer_concept_diagnostics_from_store(const MetaStore& store,
                                        TransferSafetyMode safety);

TransferConceptDiagnostics
transfer_concept_diagnostics_from_store(
    const MetaStore& store, TransferSafetyMode safety,
    const MetadataRawDataDescriptor& raw_descriptor);

const char*
transfer_concept_diagnostic_action_name(
    TransferConceptDiagnosticAction action) noexcept;

const char*
transfer_concept_diagnostic_reason_name(
    TransferConceptDiagnosticReason reason) noexcept;

TransferConceptDiagnosticSeverity
transfer_concept_diagnostic_severity(
    const TransferConceptDiagnostic& diagnostic) noexcept;

const char*
transfer_concept_diagnostic_severity_name(
    TransferConceptDiagnosticSeverity severity) noexcept;

const char*
transfer_concept_diagnostic_message(
    const TransferConceptDiagnostic& diagnostic) noexcept;

std::string
transfer_concept_diagnostic_token(const TransferConceptDiagnostic& diagnostic);

std::string
transfer_concept_diagnostic_message_token(
    const TransferConceptDiagnostic& diagnostic);

std::vector<std::string>
transfer_concept_diagnostic_message_arguments(
    const TransferConceptDiagnostic& diagnostic);

/**
 * \brief Report which opt-in raw source carriers are passthrough candidates.
 *
 * This helper is diagnostic only. It checks preserved payload availability,
 * target carrier compatibility, decoded-entry safety filtering, content-bound
 * C2PA rules, and explicit profile drops. It does not enable passthrough by
 * itself; callers must opt in through \ref PrepareTransferRequest.
 *
 * \par API Stability
 * Experimental host-facing API for diagnostics, UI, and preflight decisions.
 */
TransferRawCarrierPassthroughAudit
raw_carrier_passthrough_audit_from_snapshot(
    const TransferSourceSnapshot& snapshot,
    const TransferRawCarrierPassthroughAuditOptions& options
    = TransferRawCarrierPassthroughAuditOptions {}) noexcept;

/**
 * \brief Append one logical raw JUMBF payload as JPEG APP11 transfer blocks.
 *
 * The input must be a logical JUMBF BMFF payload (`jumb`/`jumd`...), not an
 * already wrapped APP11 marker payload. C2PA content-bound payloads are
 * rejected by this helper; they require a dedicated invalidation/re-sign path.
 *
 * On success, the bundle policy decision for JUMBF is updated to explicit
 * `Keep`, and one or more `jpeg:app11-jumbf` prepared blocks are appended.
 */
EmitTransferResult
append_prepared_bundle_jpeg_jumbf(PreparedTransferBundle* bundle,
                                  std::span<const std::byte> logical_payload,
                                  const AppendPreparedJpegJumbfOptions& options
                                  = AppendPreparedJpegJumbfOptions {}) noexcept;

/**
 * \brief Emit prepared metadata blocks into a JPEG backend.
 *
 * Route mapping:
 * - `jpeg:app1-exif` -> APP1 (0xE1)
 * - `jpeg:app1-xmp` -> APP1 (0xE1)
 * - `jpeg:app2-icc` -> APP2 (0xE2)
 * - `jpeg:app13-iptc` -> APP13 (0xED)
 * - `jpeg:appN` / `jpeg:appN-*` where `N` in [0,15]
 * - `jpeg:com` -> COM (0xFE)
 */
EmitTransferResult
emit_prepared_bundle_jpeg(const PreparedTransferBundle& bundle,
                          JpegTransferEmitter& emitter,
                          const EmitTransferOptions& options
                          = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit prepared metadata blocks into a TIFF backend.
 *
 * Route mapping:
 * - `tiff:tag-700-xmp` -> TIFF tag 700 (XMP packet)
 * - `tiff:ifd-exif-app1` -> serialized EXIF APP1 input for ExifIFD materialization
 * - `tiff:tag-34675-icc` -> TIFF tag 34675 (ICC profile)
 * - `tiff:tag-33723-iptc` -> TIFF tag 33723 (IPTC IIM stream)
 */
EmitTransferResult
emit_prepared_bundle_tiff(const PreparedTransferBundle& bundle,
                          TiffTransferEmitter& emitter,
                          const EmitTransferOptions& options
                          = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit prepared metadata blocks into a JPEG XL backend.
 *
 * Route mapping:
 * - `jxl:icc-profile` -> encoder ICC profile
 * - `jxl:box-exif` -> `Exif`
 * - `jxl:box-xml` -> `xml `
 * - `jxl:box-jumb` -> `jumb`
 * - `jxl:box-c2pa` -> `c2pa`
 */
EmitTransferResult
emit_prepared_bundle_jxl(const PreparedTransferBundle& bundle,
                         JxlTransferEmitter& emitter,
                         const EmitTransferOptions& options
                         = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit prepared metadata blocks into a WebP backend.
 *
 * Route mapping:
 * - `webp:chunk-exif` -> `EXIF`
 * - `webp:chunk-xmp` -> `XMP `
 * - `webp:chunk-iccp` -> `ICCP`
 * - `webp:chunk-c2pa` -> `C2PA`
 */
EmitTransferResult
emit_prepared_bundle_webp(const PreparedTransferBundle& bundle,
                          WebpTransferEmitter& emitter,
                          const EmitTransferOptions& options
                          = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit prepared metadata blocks into a PNG backend.
 *
 * Route mapping:
 * - `png:chunk-exif` -> `eXIf`
 * - `png:chunk-xmp` -> `iTXt`
 * - `png:chunk-iccp` -> `iCCP`
 */
EmitTransferResult
emit_prepared_bundle_png(const PreparedTransferBundle& bundle,
                         PngTransferEmitter& emitter,
                         const EmitTransferOptions& options
                         = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit prepared metadata blocks into a JP2 backend.
 *
 * Route mapping:
 * - `jp2:box-exif` -> `Exif`
 * - `jp2:box-xml` -> `xml `
 * - `jp2:box-jp2h-colr` -> `jp2h` carrying one `colr` ICC child box
 */
EmitTransferResult
emit_prepared_bundle_jp2(const PreparedTransferBundle& bundle,
                         Jp2TransferEmitter& emitter,
                         const EmitTransferOptions& options
                         = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit prepared metadata blocks into an EXR backend.
 *
 * Route mapping:
 * - `exr:attribute-string` -> EXR `string` header attribute
 *
 * This first-class EXR target is intentionally bounded to safe flattened
 * string attributes. It does not rewrite complete EXR files.
 */
EmitTransferResult
emit_prepared_bundle_exr(const PreparedTransferBundle& bundle,
                         ExrTransferEmitter& emitter,
                         const EmitTransferOptions& options
                         = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit prepared metadata blocks into an ISO-BMFF metadata backend.
 *
 * Route mapping:
 * - `bmff:item-exif` -> `Exif` item payload
 * - `bmff:item-xmp` -> `mime` item carrying XMP
 * - `bmff:item-jumb` -> `jumb` item payload
 * - `bmff:item-c2pa` -> `c2pa` item payload
 * - `bmff:property-colr-icc` -> `colr` property payload with `prof` ICC data
 */
EmitTransferResult
emit_prepared_bundle_bmff(const PreparedTransferBundle& bundle,
                          BmffTransferEmitter& emitter,
                          const EmitTransferOptions& options
                          = EmitTransferOptions {}) noexcept;

/**
 * \brief Compile a reusable TIFF emit plan from a prepared bundle.
 *
 * This maps route strings to TIFF tags once for high-throughput
 * "prepare once, emit many" workflows.
 */
EmitTransferResult
compile_prepared_bundle_tiff(const PreparedTransferBundle& bundle,
                             PreparedTiffEmitPlan* out_plan,
                             const EmitTransferOptions& options
                             = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit a prepared bundle using a precompiled TIFF emit plan.
 */
EmitTransferResult
emit_prepared_bundle_tiff_compiled(const PreparedTransferBundle& bundle,
                                   const PreparedTiffEmitPlan& plan,
                                   TiffTransferEmitter& emitter,
                                   const EmitTransferOptions& options
                                   = EmitTransferOptions {}) noexcept;

/**
 * \brief Compile a reusable JPEG XL emit plan from a prepared bundle.
 *
 * This maps route strings to JPEG XL box types once for high-throughput
 * "prepare once, emit many" workflows.
 */
EmitTransferResult
compile_prepared_bundle_jxl(const PreparedTransferBundle& bundle,
                            PreparedJxlEmitPlan* out_plan,
                            const EmitTransferOptions& options
                            = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit a prepared bundle using a precompiled JPEG XL emit plan.
 */
EmitTransferResult
emit_prepared_bundle_jxl_compiled(const PreparedTransferBundle& bundle,
                                  const PreparedJxlEmitPlan& plan,
                                  JxlTransferEmitter& emitter,
                                  const EmitTransferOptions& options
                                  = EmitTransferOptions {}) noexcept;

/**
 * \brief Builds one encoder-side handoff view for prepared JPEG XL metadata.
 *
 * This exposes the single encoder ICC profile separately from the normal JXL
 * box path. Boxes remain on the regular `jxl:box-*` route contract.
 */
EmitTransferResult
build_prepared_jxl_encoder_handoff_view(const PreparedTransferBundle& bundle,
                                        PreparedJxlEncoderHandoffView* out_view,
                                        const EmitTransferOptions& options
                                        = EmitTransferOptions {}) noexcept;

EmitTransferResult
build_prepared_jxl_encoder_handoff(const PreparedTransferBundle& bundle,
                                   PreparedJxlEncoderHandoff* out_handoff,
                                   const EmitTransferOptions& options
                                   = EmitTransferOptions {}) noexcept;

PreparedJxlEncoderHandoffIoResult
serialize_prepared_jxl_encoder_handoff(
    const PreparedJxlEncoderHandoff& handoff,
    std::vector<std::byte>* out_bytes) noexcept;

PreparedJxlEncoderHandoffIoResult
deserialize_prepared_jxl_encoder_handoff(
    std::span<const std::byte> bytes,
    PreparedJxlEncoderHandoff* out_handoff) noexcept;

/**
 * \brief Compile a reusable WebP emit plan from a prepared bundle.
 */
EmitTransferResult
compile_prepared_bundle_webp(const PreparedTransferBundle& bundle,
                             PreparedWebpEmitPlan* out_plan,
                             const EmitTransferOptions& options
                             = EmitTransferOptions {}) noexcept;

/**
 * \brief Compile a reusable PNG emit plan from a prepared bundle.
 */
EmitTransferResult
compile_prepared_bundle_png(const PreparedTransferBundle& bundle,
                            PreparedPngEmitPlan* out_plan,
                            const EmitTransferOptions& options
                            = EmitTransferOptions {}) noexcept;

/**
 * \brief Compile a reusable JP2 emit plan from a prepared bundle.
 */
EmitTransferResult
compile_prepared_bundle_jp2(const PreparedTransferBundle& bundle,
                            PreparedJp2EmitPlan* out_plan,
                            const EmitTransferOptions& options
                            = EmitTransferOptions {}) noexcept;

/**
 * \brief Compile a reusable EXR emit plan from a prepared bundle.
 */
EmitTransferResult
compile_prepared_bundle_exr(const PreparedTransferBundle& bundle,
                            PreparedExrEmitPlan* out_plan,
                            const EmitTransferOptions& options
                            = EmitTransferOptions {}) noexcept;

/**
 * \brief Compile a reusable ISO-BMFF metadata emit plan from a prepared
 * bundle.
 */
EmitTransferResult
compile_prepared_bundle_bmff(const PreparedTransferBundle& bundle,
                             PreparedBmffEmitPlan* out_plan,
                             const EmitTransferOptions& options
                             = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit a prepared bundle using a precompiled WebP emit plan.
 */
EmitTransferResult
emit_prepared_bundle_webp_compiled(const PreparedTransferBundle& bundle,
                                   const PreparedWebpEmitPlan& plan,
                                   WebpTransferEmitter& emitter,
                                   const EmitTransferOptions& options
                                   = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit a prepared bundle using a precompiled PNG emit plan.
 */
EmitTransferResult
emit_prepared_bundle_png_compiled(const PreparedTransferBundle& bundle,
                                  const PreparedPngEmitPlan& plan,
                                  PngTransferEmitter& emitter,
                                  const EmitTransferOptions& options
                                  = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit a prepared bundle using a precompiled JP2 emit plan.
 */
EmitTransferResult
emit_prepared_bundle_jp2_compiled(const PreparedTransferBundle& bundle,
                                  const PreparedJp2EmitPlan& plan,
                                  Jp2TransferEmitter& emitter,
                                  const EmitTransferOptions& options
                                  = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit a prepared bundle using a precompiled EXR emit plan.
 */
EmitTransferResult
emit_prepared_bundle_exr_compiled(const PreparedTransferBundle& bundle,
                                  const PreparedExrEmitPlan& plan,
                                  ExrTransferEmitter& emitter,
                                  const EmitTransferOptions& options
                                  = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit a prepared bundle using a precompiled ISO-BMFF metadata emit
 * plan.
 */
EmitTransferResult
emit_prepared_bundle_bmff_compiled(const PreparedTransferBundle& bundle,
                                   const PreparedBmffEmitPlan& plan,
                                   BmffTransferEmitter& emitter,
                                   const EmitTransferOptions& options
                                   = EmitTransferOptions {}) noexcept;

/**
 * \brief Compile a reusable JPEG emit plan from a prepared bundle.
 *
 * This maps route strings to marker codes once for high-throughput
 * "prepare once, emit many" workflows.
 */
EmitTransferResult
compile_prepared_bundle_jpeg(const PreparedTransferBundle& bundle,
                             PreparedJpegEmitPlan* out_plan,
                             const EmitTransferOptions& options
                             = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit a prepared bundle using a precompiled JPEG emit plan.
 */
EmitTransferResult
emit_prepared_bundle_jpeg_compiled(const PreparedTransferBundle& bundle,
                                   const PreparedJpegEmitPlan& plan,
                                   JpegTransferEmitter& emitter,
                                   const EmitTransferOptions& options
                                   = EmitTransferOptions {}) noexcept;

/**
 * \brief Write prepared JPEG metadata marker bytes directly to a byte sink.
 *
 * This emits serialized APP/COM marker records only. It does not write SOI,
 * SOS/image data, or EOI.
 */
EmitTransferResult
write_prepared_bundle_jpeg(const PreparedTransferBundle& bundle,
                           TransferByteWriter& writer,
                           const EmitTransferOptions& options
                           = EmitTransferOptions {}) noexcept;

/**
 * \brief Write prepared JPEG metadata marker bytes using a precompiled plan.
 */
EmitTransferResult
write_prepared_bundle_jpeg_compiled(const PreparedTransferBundle& bundle,
                                    const PreparedJpegEmitPlan& plan,
                                    TransferByteWriter& writer,
                                    const EmitTransferOptions& options
                                    = EmitTransferOptions {}) noexcept;

/**
 * \brief Read a file into a reusable decoded source snapshot.
 *
 * This helper hides the scratch-buffer management needed by
 * \ref simple_meta_read and returns an owned \ref TransferSourceSnapshot that
 * can be reused later with \ref prepare_metadata_for_target_snapshot without
 * reopening the source file.
 *
 * Current snapshot contract is decoded-store-backed. Raw carrier provenance and
 * payload bytes are preserved only when read options request them; transfer
 * execution still uses decoded re-emission.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
ReadTransferSourceSnapshotFileResult
read_transfer_source_snapshot_file(
    const char* path, const ReadTransferSourceSnapshotFileOptions& options
                      = ReadTransferSourceSnapshotFileOptions {}) noexcept;

/**
 * \brief Read host-owned bytes into a reusable decoded source snapshot.
 *
 * This is the in-memory counterpart to \ref read_transfer_source_snapshot_file
 * for hosts that already own the source bytes and do not want a file-path API.
 *
 * Current snapshot contract is decoded-store-backed. Raw carrier provenance and
 * payload bytes are preserved only when read options request them; transfer
 * execution still uses decoded re-emission.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
ReadTransferSourceSnapshotBytesResult
read_transfer_source_snapshot_bytes(
    std::span<const std::byte> bytes,
    const ReadTransferSourceSnapshotOptions& options
    = ReadTransferSourceSnapshotOptions {}) noexcept;

/**
 * \brief Assemble a reusable source snapshot through bounded positional reads.
 *
 * \p format must identify the supplied source range. The function scans only
 * structural container data, fetches discovered metadata payloads into
 * caller-owned scratch, and never materializes the complete source. TIFF/DNG
 * and EXR use their native positional decoders. The returned snapshot owns its
 * finalized decoded store and any explicitly requested bounded raw carriers.
 *
 * `residual_metadata_paths` reports requested or format-specific enrichment
 * paths that are not represented by discovered payload blocks. A nonzero value
 * keeps the usable result but makes `complete()` false.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
ReadTransferSourceSnapshotRandomAccessResult
read_transfer_source_snapshot_random_access(
    const RandomAccessSourceRange& source, ContainerFormat format,
    const ReadTransferSourceSnapshotRandomAccessScratch& scratch,
    const ReadTransferSourceSnapshotRandomAccessOptions& options
    = ReadTransferSourceSnapshotRandomAccessOptions {},
    const RandomAccessReadLimits& read_limits
    = RandomAccessReadLimits {}) noexcept;

/**
 * \brief Project structured diagnostics from a positional snapshot result.
 *
 * The function does not allocate. It reports the total required count in
 * `needed` and writes the prefix that fits in \p out. Pass the same optional
 * lane requests used for the read so residual paths can be classified as an
 * incomplete requested MakerNote or embedded-container lane where applicable.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
ReadTransferSourceDiagnosticsResult
collect_read_transfer_source_diagnostics(
    const ReadTransferSourceSnapshotRandomAccessResult& result,
    std::span<ReadTransferSourceDiagnostic> out,
    const ReadTransferSourceDiagnosticOptions& options
    = ReadTransferSourceDiagnosticOptions {}) noexcept;

const char*
read_transfer_source_diagnostic_severity_name(
    ReadTransferSourceDiagnosticSeverity severity) noexcept;
const char*
read_transfer_source_diagnostic_domain_name(
    ReadTransferSourceDiagnosticDomain domain) noexcept;
const char*
read_transfer_source_diagnostic_code_name(
    ReadTransferSourceDiagnosticCode code) noexcept;
const char*
read_transfer_source_diagnostic_message(
    ReadTransferSourceDiagnosticCode code) noexcept;

/**
 * \brief Serialize a target-neutral source snapshot into owned bytes.
 *
 * The versioned representation preserves the decoded store, duplicate entry
 * order, typed values, provenance, flags, and any opt-in raw carriers. It does
 * not make a raw carrier safe to relocate or write into another container.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
TransferSourceSnapshotIoResult
serialize_transfer_source_snapshot(
    const TransferSourceSnapshot& snapshot, std::vector<std::byte>* out_bytes,
    const TransferSourceSnapshotIoOptions& options
    = TransferSourceSnapshotIoOptions {}) noexcept;

/**
 * \brief Parse a versioned target-neutral source snapshot.
 *
 * Parsing is bounded by \p options and replaces \p out_snapshot only after the
 * complete representation and every arena reference have been validated.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
TransferSourceSnapshotIoResult
deserialize_transfer_source_snapshot(
    std::span<const std::byte> bytes, TransferSourceSnapshot* out_snapshot,
    const TransferSourceSnapshotIoOptions& options
    = TransferSourceSnapshotIoOptions {}) noexcept;

/**
 * \brief High-level helper: read file, decode metadata, and prepare transfer bundle.
 *
 * This helper is intended for thin wrappers (CLI/Python) that need
 * `read -> prepare` with consistent resource policies.
 */
PrepareTransferFileResult
prepare_metadata_for_target_file(const char* path,
                                 const PrepareTransferFileOptions& options
                                 = PrepareTransferFileOptions {}) noexcept;

/**
 * \brief Derive an external signer request from a prepared C2PA rewrite bundle.
 *
 * The returned request is preparation-only. It does not build a manifest or
 * perform signing. For supported targets, it exposes the deterministic
 * rewrite-without-C2PA byte sequence as preserved source ranges plus prepared
 * target carrier chunks.
 */
TransferStatus
build_prepared_c2pa_sign_request(
    const PreparedTransferBundle& bundle,
    PreparedTransferC2paSignRequest* out_request) noexcept;

/**
 * \brief Build a structured external-signer handoff package.
 *
 * This wraps \ref build_prepared_c2pa_sign_request and
 * \ref build_prepared_c2pa_sign_request_binding into one reusable core result.
 */
TransferStatus
build_prepared_c2pa_handoff_package(
    const PreparedTransferBundle& bundle,
    std::span<const std::byte> target_input,
    PreparedTransferC2paHandoffPackage* out_package) noexcept;

/**
 * \brief Build a persistable signed-result package from a prepared bundle.
 *
 * This wraps \ref build_prepared_c2pa_sign_request and copies the external
 * signer material into one reusable core object.
 */
TransferStatus
build_prepared_c2pa_signed_package(
    const PreparedTransferBundle& bundle,
    const PreparedTransferC2paSignerInput& input,
    PreparedTransferC2paSignedPackage* out_package) noexcept;

/**
 * \brief Stage externally signed C2PA rewrite output into a prepared bundle.
 *
 * This consumes the request built by \ref build_prepared_c2pa_sign_request and
 * replaces prepared target C2PA carrier blocks with the external signed
 * logical payload. OpenMeta does not perform signing here; it only validates
 * the input contract and re-packs the logical payload into the prepared target
 * carrier.
 */
EmitTransferResult
apply_prepared_c2pa_sign_result(
    PreparedTransferBundle* bundle,
    const PreparedTransferC2paSignRequest& request,
    const PreparedTransferC2paSignerInput& input) noexcept;

/**
 * \brief Stage one persisted signed C2PA package into a prepared bundle.
 */
EmitTransferResult
apply_prepared_c2pa_signed_package(
    PreparedTransferBundle* bundle,
    const PreparedTransferC2paSignedPackage& package) noexcept;

/**
 * \brief Materialize the exact content-binding byte stream for a C2PA sign request.
 *
 * This reconstructs the deterministic rewrite-without-C2PA byte sequence
 * described by \p request by combining preserved source ranges from
 * \p target_input with prepared target carrier chunks from \p bundle.
 */
BuildPreparedC2paBindingResult
build_prepared_c2pa_sign_request_binding(
    const PreparedTransferBundle& bundle,
    std::span<const std::byte> target_input,
    const PreparedTransferC2paSignRequest& request,
    std::vector<std::byte>* out_bytes) noexcept;

/**
 * \brief Validate one externally signed C2PA payload before staging it.
 *
 * This performs the same input and payload checks used by
 * \ref apply_prepared_c2pa_sign_result, but does not mutate \p bundle.
 */
ValidatePreparedC2paSignResult
validate_prepared_c2pa_sign_result(
    const PreparedTransferBundle& bundle,
    const PreparedTransferC2paSignRequest& request,
    const PreparedTransferC2paSignerInput& input) noexcept;

/**
 * \brief Validate one persisted signed C2PA package before staging it.
 */
ValidatePreparedC2paSignResult
validate_prepared_c2pa_signed_package(
    const PreparedTransferBundle& bundle,
    const PreparedTransferC2paSignedPackage& package) noexcept;

/**
 * \brief Serialize one C2PA handoff package into a stable binary transport.
 */
PreparedTransferC2paPackageIoResult
serialize_prepared_c2pa_handoff_package(
    const PreparedTransferC2paHandoffPackage& package,
    std::vector<std::byte>* out_bytes) noexcept;

/**
 * \brief Parse one serialized C2PA handoff package.
 */
PreparedTransferC2paPackageIoResult
deserialize_prepared_c2pa_handoff_package(
    std::span<const std::byte> bytes,
    PreparedTransferC2paHandoffPackage* out_package) noexcept;

/**
 * \brief Serialize one persisted signed C2PA package into a stable binary transport.
 */
PreparedTransferC2paPackageIoResult
serialize_prepared_c2pa_signed_package(
    const PreparedTransferC2paSignedPackage& package,
    std::vector<std::byte>* out_bytes) noexcept;

/**
 * \brief Parse one serialized signed C2PA package.
 */
PreparedTransferC2paPackageIoResult
deserialize_prepared_c2pa_signed_package(
    std::span<const std::byte> bytes,
    PreparedTransferC2paSignedPackage* out_package) noexcept;

/**
 * \brief Compile a reusable execution plan for \ref execute_prepared_transfer_compiled.
 *
 * This performs route-to-backend compilation once and stores the selected
 * emit options inside the plan for repeated runtime execution.
 */
EmitTransferResult
compile_prepared_transfer_execution(
    const PreparedTransferBundle& bundle, const EmitTransferOptions& options,
    PreparedTransferExecutionPlan* out_plan) noexcept;

/**
 * \brief Build one target-neutral adapter view over prepared transfer ops.
 *
 * This compiles the same target-specific route mappings used by emit
 * execution, then flattens them into one explicit operation list for host
 * integrations that want to consume prepared blocks without parsing routes.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
EmitTransferResult
build_prepared_transfer_adapter_view(const PreparedTransferBundle& bundle,
                                     PreparedTransferAdapterView* out_view,
                                     const EmitTransferOptions& options
                                     = {}) noexcept;

/**
 * \brief Validate a codec-facing adapter view against its source bundle.
 *
 * Validation rebuilds the canonical operation list and compares every
 * kind-specific field, including marker/tag/box/chunk/BMFF identifiers. Hosts
 * may use this as a preflight before handing operations to a codec.
 *
 * \par API Stability
 * Experimental host-facing API; candidate for the narrow adoption profile.
 */
EmitTransferResult
validate_prepared_transfer_adapter_view(
    const PreparedTransferBundle& bundle,
    const PreparedTransferAdapterView& view) noexcept;

/**
 * \brief Resolve one EXR adapter operation into typed insertion values.
 *
 * The returned name, type, and value borrow storage from \p bundle and remain
 * valid only while that bundle and its block payloads are unchanged. This
 * avoids exposing the internal EXR route and payload framing to host codecs.
 *
 * \par API Stability
 * Experimental host-facing API; candidate for the narrow adoption profile.
 */
EmitTransferResult
get_prepared_transfer_adapter_exr_attribute_view(
    const PreparedTransferBundle& bundle, const PreparedTransferAdapterOp& op,
    ExrPreparedAttributeView* out_attribute) noexcept;

/**
 * \brief Emit one prepared adapter view into a generic host-side sink.
 *
 * This lets host integrations consume the flattened adapter operation list
 * without re-parsing routes or target-specific dispatch tokens.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
EmitTransferResult
emit_prepared_transfer_adapter_view(const PreparedTransferBundle& bundle,
                                    const PreparedTransferAdapterView& view,
                                    TransferAdapterSink& sink) noexcept;

/**
 * \brief Apply fixed-width time patch updates in-place on a prepared bundle.
 *
 * Patches are applied to byte ranges listed in `bundle.time_patch_map`
 * (typically filled for EXIF APP1 during prepare).
 */
ApplyTimePatchResult
apply_time_patches(PreparedTransferBundle* bundle,
                   std::span<const TimePatchUpdate> updates,
                   const ApplyTimePatchOptions& options
                   = ApplyTimePatchOptions {}) noexcept;

/**
 * \brief Apply fixed-width time patch views in-place on a prepared bundle.
 *
 * This overload avoids owned patch buffers and is intended for high-throughput
 * `prepare once -> compile once -> patch/emit many` integrations.
 */
ApplyTimePatchResult
apply_time_patches_view(PreparedTransferBundle* bundle,
                        std::span<const TimePatchView> updates,
                        const ApplyTimePatchOptions& options
                        = ApplyTimePatchOptions {}) noexcept;

/**
 * \brief Execute the high-level transfer flow on a prepared bundle.
 *
 * This helper applies optional time patches, compiles/emits prepared blocks,
 * and optionally plans/applies a target-stream edit using \p edit_input.
 * It is intended for thin wrappers and high-throughput "prepare once, reuse"
 * integrations.
 */
ExecutePreparedTransferResult
execute_prepared_transfer(PreparedTransferBundle* bundle,
                          std::span<const std::byte> edit_input = {},
                          const ExecutePreparedTransferOptions& options
                          = ExecutePreparedTransferOptions {}) noexcept;

/**
 * \brief Execute the transfer flow using a precompiled execution plan.
 *
 * This is intended for high-throughput integrations that want
 * `prepare once -> compile once -> patch/emit many`.
 */
ExecutePreparedTransferResult
execute_prepared_transfer_compiled(PreparedTransferBundle* bundle,
                                   const PreparedTransferExecutionPlan& plan,
                                   std::span<const std::byte> edit_input = {},
                                   const ExecutePreparedTransferOptions& options
                                   = ExecutePreparedTransferOptions {}) noexcept;

/**
 * \brief Hot-path helper: apply non-owning time patches and stream emit bytes.
 *
 * This is a thin encoder-integration helper for
 * `prepare once -> compile once -> patch -> write` workflows. It reuses
 * \ref execute_prepared_transfer_compiled for plan validation and emission,
 * but accepts non-owning patch views to avoid per-call owned patch buffers.
 */
ExecutePreparedTransferResult
write_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    TransferByteWriter& writer,
    std::span<const TimePatchView> time_patches = {},
    const ApplyTimePatchOptions& time_patch     = ApplyTimePatchOptions {},
    uint32_t emit_repeat                        = 1U) noexcept;

/**
 * \brief Hot-path helper: apply non-owning time patches and emit through a
 * JPEG backend.
 *
 * This is the direct backend-emitter path for `prepare once -> compile once ->
 * patch -> emit` integrations that already own a JPEG encoder/backend.
 */
ExecutePreparedTransferResult
emit_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    JpegTransferEmitter& emitter,
    std::span<const TimePatchView> time_patches = {},
    const ApplyTimePatchOptions& time_patch     = ApplyTimePatchOptions {},
    uint32_t emit_repeat                        = 1U) noexcept;

/**
 * \brief Hot-path helper: apply non-owning time patches and emit through a
 * TIFF backend.
 *
 * TIFF intentionally uses backend-emitter or rewrite/edit paths instead of a
 * metadata-only byte-writer emit contract.
 */
ExecutePreparedTransferResult
emit_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    TiffTransferEmitter& emitter,
    std::span<const TimePatchView> time_patches = {},
    const ApplyTimePatchOptions& time_patch     = ApplyTimePatchOptions {},
    uint32_t emit_repeat                        = 1U) noexcept;

/**
 * \brief Hot-path helper: apply non-owning time patches and emit through a
 * JPEG XL backend.
 */
ExecutePreparedTransferResult
emit_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    JxlTransferEmitter& emitter,
    std::span<const TimePatchView> time_patches = {},
    const ApplyTimePatchOptions& time_patch     = ApplyTimePatchOptions {},
    uint32_t emit_repeat                        = 1U) noexcept;

/**
 * \brief Hot-path helper: apply non-owning time patches and emit through a
 * WebP backend.
 */
ExecutePreparedTransferResult
emit_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    WebpTransferEmitter& emitter,
    std::span<const TimePatchView> time_patches = {},
    const ApplyTimePatchOptions& time_patch     = ApplyTimePatchOptions {},
    uint32_t emit_repeat                        = 1U) noexcept;

/**
 * \brief Hot-path helper: apply non-owning time patches and emit through a
 * PNG backend.
 */
ExecutePreparedTransferResult
emit_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    PngTransferEmitter& emitter,
    std::span<const TimePatchView> time_patches = {},
    const ApplyTimePatchOptions& time_patch     = ApplyTimePatchOptions {},
    uint32_t emit_repeat                        = 1U) noexcept;

/**
 * \brief Hot-path helper: apply non-owning time patches and emit through a
 * JP2 backend.
 */
ExecutePreparedTransferResult
emit_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    Jp2TransferEmitter& emitter,
    std::span<const TimePatchView> time_patches = {},
    const ApplyTimePatchOptions& time_patch     = ApplyTimePatchOptions {},
    uint32_t emit_repeat                        = 1U) noexcept;

/**
 * \brief Hot-path helper: apply non-owning time patches and emit through an
 * EXR backend.
 */
ExecutePreparedTransferResult
emit_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    ExrTransferEmitter& emitter,
    std::span<const TimePatchView> time_patches = {},
    const ApplyTimePatchOptions& time_patch     = ApplyTimePatchOptions {},
    uint32_t emit_repeat                        = 1U) noexcept;

/**
 * \brief Hot-path helper: apply non-owning time patches and emit through an
 * ISO-BMFF metadata backend.
 */
ExecutePreparedTransferResult
emit_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    BmffTransferEmitter& emitter,
    std::span<const TimePatchView> time_patches = {},
    const ApplyTimePatchOptions& time_patch     = ApplyTimePatchOptions {},
    uint32_t emit_repeat                        = 1U) noexcept;

/**
 * \brief High-level helper: read file, prepare a bundle, then execute transfer.
 *
 * This wraps \ref prepare_metadata_for_target_file plus
 * \ref execute_prepared_transfer for CLI/Python entry points that want a single
 * file-based execution path.
 */
ExecutePreparedTransferFileResult
execute_prepared_transfer_file(const char* path,
                               const ExecutePreparedTransferFileOptions& options
                               = ExecutePreparedTransferFileOptions {}) noexcept;

/**
 * \brief High-level helper: prepare from a decoded source snapshot, then
 * execute transfer.
 *
 * This mirrors \ref execute_prepared_transfer_file for hosts that already keep
 * a reusable decoded source snapshot from an earlier read.
 *
 * The input snapshot is not mutated. The helper copies the decoded store into
 * a fresh local bundle-preparation state, so the same snapshot can be reused
 * safely by concurrent callers that do not share the returned result object.
 *
 * Current snapshot contract is decoded-store-backed. Raw carrier provenance and
 * payload bytes are preserved only when read options request them; transfer
 * execution still uses decoded re-emission.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
ExecutePreparedTransferFileResult
execute_prepared_transfer_snapshot(
    const TransferSourceSnapshot& snapshot,
    const ExecutePreparedTransferSnapshotOptions& options
    = ExecutePreparedTransferSnapshotOptions {}) noexcept;

/**
 * \brief High-level helper: prepare from a decoded source snapshot, then edit
 * a host-owned target byte buffer.
 *
 * This is the fileless destination counterpart to
 * \ref execute_prepared_transfer_snapshot for hosts that already own the
 * target bytes in memory and do not want OpenMeta to reopen the destination
 * file.
 *
 * The input snapshot and target byte span are not mutated. The helper copies
 * decoded source state into a fresh local preparation store before executing
 * the edit path.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
ExecutePreparedTransferFileResult
execute_prepared_transfer_snapshot(
    const TransferSourceSnapshot& snapshot,
    std::span<const std::byte> target_bytes,
    const ExecutePreparedTransferSnapshotOptions& options
    = ExecutePreparedTransferSnapshotOptions {}) noexcept;

/**
 * \brief High-level helper: execute a previously prepared bundle against
 * host-owned target bytes.
 *
 * This wraps \ref execute_prepared_transfer with the same bounded sidecar and
 * cleanup contract used by the file/snapshot helpers, but it starts from an
 * already prepared bundle instead of rebuilding source state first.
 *
 * \par API Stability
 * Experimental host-facing API. Treat the bundle as an immutable prepared
 * input unless using documented patch helpers.
 */
ExecutePreparedTransferFileResult
execute_prepared_transfer_bundle(
    const PreparedTransferBundle& bundle,
    std::span<const std::byte> target_bytes,
    const ExecutePreparedTransferBundleOptions& options
    = ExecutePreparedTransferBundleOptions {}) noexcept;

/**
 * \brief Plan JPEG metadata injection/edit strategy for a prepared bundle.
 *
 * `Auto` selects `InPlace` when all emitted payloads can replace existing
 * leading JPEG metadata segments with exact size match; otherwise it selects
 * `MetadataRewrite`.
 */
JpegEditPlan
plan_prepared_bundle_jpeg_edit(std::span<const std::byte> input_jpeg,
                               const PreparedTransferBundle& bundle,
                               const PlanJpegEditOptions& options
                               = PlanJpegEditOptions {}) noexcept;

/**
 * \brief Apply a planned JPEG metadata edit and produce edited output bytes.
 *
 * For `InPlace`, this replaces matched existing segment payload bytes in a
 * copy of the input stream. For `MetadataRewrite`, this rewrites leading
 * metadata segments and preserves the remaining codestream bytes unchanged.
 */
EmitTransferResult
apply_prepared_bundle_jpeg_edit(std::span<const std::byte> input_jpeg,
                                const PreparedTransferBundle& bundle,
                                const JpegEditPlan& plan,
                                std::vector<std::byte>* out_jpeg) noexcept;

/**
 * \brief Apply a planned JPEG metadata edit and stream output bytes to a writer.
 */
EmitTransferResult
write_prepared_bundle_jpeg_edit(std::span<const std::byte> input_jpeg,
                                const PreparedTransferBundle& bundle,
                                const JpegEditPlan& plan,
                                TransferByteWriter& writer) noexcept;

/**
 * \brief Plan TIFF metadata rewrite for a prepared bundle.
 *
 * This computes the expected output size and validates that TIFF-applicable
 * prepared blocks can be materialized on the target stream.
 */
TiffEditPlan
plan_prepared_bundle_tiff_edit(std::span<const std::byte> input_tiff,
                               const PreparedTransferBundle& bundle,
                               const PlanTiffEditOptions& options
                               = PlanTiffEditOptions {}) noexcept;

/**
 * \brief Apply a planned TIFF metadata rewrite and produce edited output bytes.
 */
EmitTransferResult
apply_prepared_bundle_tiff_edit(std::span<const std::byte> input_tiff,
                                const PreparedTransferBundle& bundle,
                                const TiffEditPlan& plan,
                                std::vector<std::byte>* out_tiff) noexcept;

/**
 * \brief Apply a planned TIFF metadata rewrite and stream output bytes to a writer.
 *
 * Current implementation uses the same rewrite path as
 * \ref apply_prepared_bundle_tiff_edit and then writes the final bytes to the
 * supplied sink.
 */
EmitTransferResult
write_prepared_bundle_tiff_edit(std::span<const std::byte> input_tiff,
                                const PreparedTransferBundle& bundle,
                                const TiffEditPlan& plan,
                                TransferByteWriter& writer) noexcept;

/**
 * \brief Build one deterministic direct-emit package plan for a prepared bundle.
 *
 * Current implementation supports direct JPEG marker, JXL/WebP/PNG/JP2
 * carrier, and BMFF item/property payload packaging without requiring an input
 * container byte stream.
 */
EmitTransferResult
build_prepared_transfer_emit_package(const PreparedTransferBundle& bundle,
                                     PreparedTransferPackagePlan* out_plan,
                                     const EmitTransferOptions& options
                                     = {}) noexcept;

EmitTransferResult
build_prepared_bundle_jpeg_package(
    std::span<const std::byte> input_jpeg, const PreparedTransferBundle& bundle,
    const JpegEditPlan& plan, PreparedTransferPackagePlan* out_plan) noexcept;

EmitTransferResult
build_prepared_bundle_tiff_package(
    std::span<const std::byte> input_tiff, const PreparedTransferBundle& bundle,
    const TiffEditPlan& plan, PreparedTransferPackagePlan* out_plan) noexcept;

EmitTransferResult
write_prepared_transfer_package(std::span<const std::byte> input,
                                const PreparedTransferBundle& bundle,
                                const PreparedTransferPackagePlan& plan,
                                TransferByteWriter& writer) noexcept;

EmitTransferResult
build_prepared_transfer_package_batch(
    std::span<const std::byte> input, const PreparedTransferBundle& bundle,
    const PreparedTransferPackagePlan& plan,
    PreparedTransferPackageBatch* out_batch) noexcept;

EmitTransferResult
write_prepared_transfer_package_batch(const PreparedTransferPackageBatch& batch,
                                      TransferByteWriter& writer) noexcept;

/// Classifies one prepared transfer route into a target-neutral semantic kind.
TransferSemanticKind
classify_transfer_route_semantic_kind(std::string_view route) noexcept;

/// Stable display name for one semantic transfer kind.
std::string_view
transfer_semantic_name(TransferSemanticKind kind) noexcept;

/// Stable display name for one persisted transfer artifact kind.
std::string_view
prepared_transfer_artifact_kind_name(PreparedTransferArtifactKind kind) noexcept;

/// Builds zero-copy semantic payload views over one prepared transfer bundle.
EmitTransferResult
collect_prepared_transfer_payload_views(
    const PreparedTransferBundle& bundle,
    std::vector<PreparedTransferPayloadView>* out,
    const EmitTransferOptions& options = EmitTransferOptions {}) noexcept;

/// Builds zero-copy semantic payload views over one persisted payload batch.
EmitTransferResult
collect_prepared_transfer_payload_views(
    const PreparedTransferPayloadBatch& batch,
    std::vector<PreparedTransferPayloadView>* out) noexcept;

/// Builds one owned semantic payload batch from one prepared transfer bundle.
EmitTransferResult
build_prepared_transfer_payload_batch(const PreparedTransferBundle& bundle,
                                      PreparedTransferPayloadBatch* out,
                                      const EmitTransferOptions& options
                                      = EmitTransferOptions {}) noexcept;

PreparedTransferPayloadIoResult
serialize_prepared_transfer_payload_batch(
    const PreparedTransferPayloadBatch& batch,
    std::vector<std::byte>* out_bytes) noexcept;

PreparedTransferArtifactIoResult
inspect_prepared_transfer_artifact(
    std::span<const std::byte> bytes,
    PreparedTransferArtifactInfo* out_info) noexcept;

PreparedTransferPayloadIoResult
deserialize_prepared_transfer_payload_batch(
    std::span<const std::byte> bytes,
    PreparedTransferPayloadBatch* out_batch) noexcept;

PreparedTransferPayloadReplayResult
replay_prepared_transfer_payload_batch(
    const PreparedTransferPayloadBatch& batch,
    const PreparedTransferPayloadReplayCallbacks& callbacks) noexcept;

/// Builds zero-copy semantic package views over one persisted package batch.
EmitTransferResult
collect_prepared_transfer_package_views(
    const PreparedTransferPackageBatch& batch,
    std::vector<PreparedTransferPackageView>* out) noexcept;

PreparedTransferPackageReplayResult
replay_prepared_transfer_package_batch(
    const PreparedTransferPackageBatch& batch,
    const PreparedTransferPackageReplayCallbacks& callbacks) noexcept;

PreparedTransferPackageIoResult
serialize_prepared_transfer_package_batch(
    const PreparedTransferPackageBatch& batch,
    std::vector<std::byte>* out_bytes) noexcept;

PreparedTransferPackageIoResult
deserialize_prepared_transfer_package_batch(
    std::span<const std::byte> bytes,
    PreparedTransferPackageBatch* out_batch) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
