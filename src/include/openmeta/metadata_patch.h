// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"
#include "openmeta/exif_tiff_serialize.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include "openmeta/xmp_dump.h"

#include <cstddef>
#include <cstdint>
#include <span>

/** \file metadata_patch.h
 *  \brief Prepared transactional TIFF/EXIF and scalar XMP payload patching.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

inline constexpr uint32_t kMetadataPatchContractVersion    = 1U;
inline constexpr uint32_t kMaxPreparedMetadataPatchHandles = 65534U;
inline constexpr uint64_t kMaxMetadataPatchPlanId = 0x0000FFFFFFFFFFFFULL;

/// Unwrapped payload selected independently of any image container.
enum class MetadataPatchPayload : uint8_t { ExifTiff, Xmp };

enum class MetadataPatchCode : uint16_t {
    None = 0,
    NullOutput,
    InvalidOptions,
    StoreNotFinalized,
    InvalidMetadata,
    NoExifData,
    LimitExceeded,
    SerializationFailed,
    EmptyRequests,
    HandleBufferSizeMismatch,
    InvalidRequest,
    KeyNotFound,
    OccurrenceOutOfRange,
    ValueTypeMismatch,
    EntryNotSerializable,
    DuplicateRequest,
    AllocationFailed,
    InvalidPlan,
    InvalidInstance,
    EmptyUpdates,
    InvalidHandle,
    ForeignHandle,
    DuplicateHandle,
    WidthMismatch,
    InvalidValue,
    ValueAliasesInstance,
    UnsupportedShape,
    AmbiguousProperty,
    NullReplayCallback,
    ReplayFailed,
};

/// Exact native TIFF value shape. XMP requests use escaped_width instead.
struct MetadataPatchValueSpec final {
    MetaValueKind kind         = MetaValueKind::Scalar;
    MetaElementType elem_type  = MetaElementType::U8;
    TextEncoding text_encoding = TextEncoding::Unknown;
    uint32_t count             = 1U;
};

/**
 * An EXIF key/occurrence with an exact native shape, or an XMP namespace URI
 * and simple property path in the final portable serializer output.
 * XMP occurrence must be zero; duplicates and structural paths are rejected.
 * escaped_width is required for XMP and includes XML escaping, not a NUL.
 * It must be zero for EXIF. Requests never add properties or pad values.
 */
struct MetadataPatchRequest final {
    MetaKeyView key;
    uint32_t occurrence = 0U;
    MetadataPatchValueSpec expected;
    uint32_t escaped_width = 0U;
};

/// Opaque identity for one slot in one preparation generation.
class MetadataPatchHandle final {
public:
    constexpr MetadataPatchHandle() noexcept = default;
    bool valid() const noexcept { return token_ != 0U; }

private:
    uint64_t token_ = 0U;
    friend struct MetadataPatchHandleAccess;
};

/// Borrowed update. XMP accepts logical Text/Utf8 or Text/Ascii only.
struct MetadataPatchUpdate final {
    MetadataPatchHandle handle;
    MetaValueView value;
};

/// TIFF serialization policy; store validation is shared by both families.
struct MetadataPatchExifOptions final {
    bool include_subifds                     = false;
    bool inject_minimal_dng_version          = false;
    bool honor_wire_type_hints               = true;
    ExifTiffMakerNotePolicy makernote_policy = ExifTiffMakerNotePolicy::Drop;
    uint64_t max_output_bytes                = 64ULL * 1024ULL * 1024ULL;
};

/// Preparation may allocate. Only requested payload families are serialized.
struct MetadataPatchPlanOptions final {
    /// Host-issued identity in [1, kMaxMetadataPatchPlanId]. Never reuse an ID
    /// while a prior plan, worker or handle with that ID may still be used.
    /// The host coordinates ID assignment across its preparation callers.
    uint64_t plan_id = 0U;
    MetadataPatchExifOptions exif;
    /// Portable XMP policy. \hideinitializer
    XmpPortableOptions xmp {
        .limits                    = { 16U * 1024U * 1024U, 65536U },
        .include_existing_xmp      = true,
        .existing_namespace_policy = XmpExistingNamespacePolicy::PreserveCustom,
        .conflict_policy           = XmpConflictPolicy::ExistingWins,
    };
    uint32_t max_patch_requests = 4096U;
    bool validate               = true;
};

struct MetadataPatchResult final {
    MetadataPatchCode code                   = MetadataPatchCode::None;
    ExifTiffSerializeStatus serialize_status = ExifTiffSerializeStatus::Ok;
    XmpDumpStatus xmp_status                 = XmpDumpStatus::Ok;
    uint64_t payload_size                    = 0U;
    uint32_t handle_count                    = 0U;
    uint32_t patched_handles                 = 0U;
    uint32_t failed_index                    = 0xFFFFFFFFU;

    bool ok() const noexcept { return code == MetadataPatchCode::None; }
};

class PreparedMetadataPatchInstance;

/// Move-only owner of immutable payloads and compiled patch slots.
class PreparedMetadataPatchPlan final {
public:
    PreparedMetadataPatchPlan() noexcept;
    ~PreparedMetadataPatchPlan() noexcept;
    PreparedMetadataPatchPlan(PreparedMetadataPatchPlan&& other) noexcept;
    PreparedMetadataPatchPlan&
    operator=(PreparedMetadataPatchPlan&& other) noexcept;
    PreparedMetadataPatchPlan(const PreparedMetadataPatchPlan&) = delete;
    PreparedMetadataPatchPlan& operator=(const PreparedMetadataPatchPlan&)
        = delete;

    bool valid() const noexcept;
    uint32_t handle_count() const noexcept;
    std::span<const std::byte>
    payload(MetadataPatchPayload family) const noexcept;
    void reset() noexcept;

private:
    void* state_ = nullptr;
    friend MetadataPatchResult prepare_metadata_patch_plan(
        const MetaStore&, std::span<const MetadataPatchRequest>,
        const MetadataPatchPlanOptions&, std::span<MetadataPatchHandle>,
        PreparedMetadataPatchPlan*) noexcept;
    friend MetadataPatchResult create_prepared_metadata_patch_instance(
        const PreparedMetadataPatchPlan&,
        PreparedMetadataPatchInstance*) noexcept;
};

/**
 * Independently owned worker payloads. Creation may allocate and the worker
 * survives destruction of its plan. Patch success/failure, payload access and
 * library replay allocate nothing. Payload addresses and lengths remain stable
 * until reset, replacement or destruction. The host must synchronize all
 * conflicting access, including reads through payload views and destruction.
 * The patch API uses no atomics, mutexes or global mutable state. It does not
 * detect concurrent misuse. Independent instances own independent writable data.
 */
class PreparedMetadataPatchInstance final {
public:
    PreparedMetadataPatchInstance() noexcept;
    ~PreparedMetadataPatchInstance() noexcept;
    PreparedMetadataPatchInstance(
        PreparedMetadataPatchInstance&& other) noexcept;
    PreparedMetadataPatchInstance&
    operator=(PreparedMetadataPatchInstance&& other) noexcept;
    PreparedMetadataPatchInstance(const PreparedMetadataPatchInstance&) = delete;
    PreparedMetadataPatchInstance&
    operator=(const PreparedMetadataPatchInstance&)
        = delete;

    bool valid() const noexcept;
    uint32_t handle_count() const noexcept;
    std::span<const std::byte>
    payload(MetadataPatchPayload family) const noexcept;
    void reset() noexcept;

private:
    void* state_ = nullptr;
    friend MetadataPatchResult create_prepared_metadata_patch_instance(
        const PreparedMetadataPatchPlan&,
        PreparedMetadataPatchInstance*) noexcept;
    friend MetadataPatchResult patch_prepared_metadata_instance(
        PreparedMetadataPatchInstance*,
        std::span<const MetadataPatchUpdate>) noexcept;
};

uint32_t
metadata_patch_contract_version() noexcept;
const char*
metadata_patch_code_name(MetadataPatchCode code) noexcept;

/// The host supplies a fresh options.plan_id for each successful preparation.
/// On failure both the output plan and every output handle remain unchanged.
MetadataPatchResult
prepare_metadata_patch_plan(const MetaStore& store,
                            std::span<const MetadataPatchRequest> requests,
                            const MetadataPatchPlanOptions& options,
                            std::span<MetadataPatchHandle> handles,
                            PreparedMetadataPatchPlan* out_plan) noexcept;

/// Creates a worker without modifying the plan or existing output on failure.
MetadataPatchResult
create_prepared_metadata_patch_instance(
    const PreparedMetadataPatchPlan& plan,
    PreparedMetadataPatchInstance* out_instance) noexcept;

/**
 * Validate the complete batch before writing either payload. All errors leave
 * every payload byte unchanged. XMP validation includes UTF-8, XML characters,
 * and the exact escaped width. Input payloads may not alias either destination.
 * Input views and values must remain immutable for the duration of this call.
 */
MetadataPatchResult
patch_prepared_metadata_instance(
    PreparedMetadataPatchInstance* instance,
    std::span<const MetadataPatchUpdate> updates) noexcept;

using MetadataPatchReplayCallback
    = bool (*)(void* user, MetadataPatchPayload family,
               std::span<const std::byte> payload) noexcept;

/**
 * Replay existing payloads in EXIF, XMP order without allocation or mutation.
 * A false callback stops replay; prior callback effects belong to the caller.
 * Callbacks must not reset, move, destroy or patch the instance during replay.
 */
MetadataPatchResult
replay_prepared_metadata_instance(const PreparedMetadataPatchInstance& instance,
                                  MetadataPatchReplayCallback callback,
                                  void* user) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
