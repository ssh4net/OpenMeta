// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/exif_tiff_serialize.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file exif_tiff_patch.h
 * \brief Prepared target-neutral fixed-width TIFF/EXIF patching.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Experimental prepared canonical TIFF/EXIF patch contract version.
inline constexpr uint32_t kExifTiffPatchContractVersion    = 1U;
inline constexpr uint32_t kMaxPreparedExifTiffPatchHandles = 65534U;

/// Stable failure reason for canonical TIFF/EXIF preparation and patching.
enum class ExifTiffPatchCode : uint16_t {
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
};

/// Exact logical shape required by a compiled fixed-width patch slot.
struct ExifTiffPatchValueSpec final {
    MetaValueKind kind         = MetaValueKind::Scalar;
    MetaElementType elem_type  = MetaElementType::U8;
    TextEncoding text_encoding = TextEncoding::Unknown;
    uint32_t count             = 1U;
};

/// One exact key occurrence and expected value shape compiled during preparation.
struct ExifTiffPatchRequest final {
    MetaKeyView key;
    uint32_t occurrence = 0U;
    ExifTiffPatchValueSpec expected;
};

/**
 * \brief Opaque plan-scoped small-integer patch handle.
 *
 * Handles are produced only by \ref prepare_exif_tiff_patch_plan. They do not
 * expose TIFF offsets and are rejected by instances compiled from a different
 * plan.
 */
class ExifTiffPatchHandle final {
public:
    constexpr ExifTiffPatchHandle() noexcept = default;
    bool valid() const noexcept { return token_ != 0U; }

private:
    uint64_t token_ = 0U;

    friend struct ExifTiffPatchHandleAccess;
};

/// One borrowed typed update for a compiled handle.
struct ExifTiffPatchUpdate final {
    ExifTiffPatchHandle handle;
    MetaValueView value;
};

/// Preparation policy and resource limits.
struct ExifTiffPatchPlanOptions final {
    ExifTiffSerializeOptions serialization;
    uint32_t max_patch_requests = 4096U;
};

/// Structured result for preparation, instance creation, and patch batches.
struct ExifTiffPatchResult final {
    ExifTiffPatchCode code                   = ExifTiffPatchCode::None;
    ExifTiffSerializeStatus serialize_status = ExifTiffSerializeStatus::Ok;
    uint64_t payload_size                    = 0U;
    uint32_t handle_count                    = 0U;
    uint32_t patched_handles                 = 0U;
    uint32_t failed_index                    = 0xFFFFFFFFU;

    bool ok() const noexcept { return code == ExifTiffPatchCode::None; }
};

class PreparedExifTiffPatchInstance;

/**
 * \brief Move-only immutable canonical TIFF payload and compiled patch plan.
 *
 * Preparation performs all serialization and allocation. Const payload access
 * performs no allocation and may be used concurrently. The plan is independent
 * of any destination container or transfer target.
 */
class PreparedExifTiffPatchPlan final {
public:
    PreparedExifTiffPatchPlan() noexcept;
    ~PreparedExifTiffPatchPlan() noexcept;

    PreparedExifTiffPatchPlan(PreparedExifTiffPatchPlan&& other) noexcept;
    PreparedExifTiffPatchPlan&
    operator=(PreparedExifTiffPatchPlan&& other) noexcept;

    PreparedExifTiffPatchPlan(const PreparedExifTiffPatchPlan&) = delete;
    PreparedExifTiffPatchPlan& operator=(const PreparedExifTiffPatchPlan&)
        = delete;

    bool valid() const noexcept;
    uint32_t handle_count() const noexcept;
    std::span<const std::byte> payload() const noexcept;
    void reset() noexcept;

private:
    void* state_ = nullptr;

    friend ExifTiffPatchResult prepare_exif_tiff_patch_plan(
        const MetaStore&, std::span<const ExifTiffPatchRequest>,
        const ExifTiffPatchPlanOptions&, std::span<ExifTiffPatchHandle>,
        PreparedExifTiffPatchPlan*) noexcept;
    friend ExifTiffPatchResult create_prepared_exif_tiff_patch_instance(
        const PreparedExifTiffPatchPlan&,
        PreparedExifTiffPatchInstance*) noexcept;
};

/**
 * \brief Move-only mutable worker copy of one prepared canonical TIFF payload.
 *
 * Instance creation may allocate. Fixed-width patch batches and payload access
 * perform no allocation. One instance requires exclusive ownership while
 * patching; independent instances may be patched concurrently.
 */
class PreparedExifTiffPatchInstance final {
public:
    PreparedExifTiffPatchInstance() noexcept;
    ~PreparedExifTiffPatchInstance() noexcept;

    PreparedExifTiffPatchInstance(
        PreparedExifTiffPatchInstance&& other) noexcept;
    PreparedExifTiffPatchInstance&
    operator=(PreparedExifTiffPatchInstance&& other) noexcept;

    PreparedExifTiffPatchInstance(const PreparedExifTiffPatchInstance&) = delete;
    PreparedExifTiffPatchInstance&
    operator=(const PreparedExifTiffPatchInstance&)
        = delete;

    bool valid() const noexcept;
    uint32_t handle_count() const noexcept;
    std::span<const std::byte> payload() const noexcept;
    void reset() noexcept;

private:
    void* state_ = nullptr;

    friend ExifTiffPatchResult create_prepared_exif_tiff_patch_instance(
        const PreparedExifTiffPatchPlan&,
        PreparedExifTiffPatchInstance*) noexcept;
    friend ExifTiffPatchResult patch_prepared_exif_tiff_instance(
        PreparedExifTiffPatchInstance*,
        std::span<const ExifTiffPatchUpdate>) noexcept;
};

uint32_t
exif_tiff_patch_contract_version() noexcept;

const char*
exif_tiff_patch_code_name(ExifTiffPatchCode code) noexcept;

/**
 * \brief Serialize canonical TIFF bytes and compile exact patch requests.
 *
 * `handles` must contain exactly one element per request. On failure, the
 * output plan and handle span are unchanged. Preparation may allocate.
 */
ExifTiffPatchResult
prepare_exif_tiff_patch_plan(const MetaStore& store,
                             std::span<const ExifTiffPatchRequest> requests,
                             const ExifTiffPatchPlanOptions& options,
                             std::span<ExifTiffPatchHandle> handles,
                             PreparedExifTiffPatchPlan* out_plan) noexcept;

/// Clone one independently owned mutable worker instance. Creation may allocate.
ExifTiffPatchResult
create_prepared_exif_tiff_patch_instance(
    const PreparedExifTiffPatchPlan& plan,
    PreparedExifTiffPatchInstance* out_instance) noexcept;

/**
 * \brief Apply one allocation-free fixed-width patch batch transactionally.
 *
 * All handles, value shapes, widths, rational denominators, duplicates, and
 * payload aliases are checked before any byte changes.
 */
ExifTiffPatchResult
patch_prepared_exif_tiff_instance(
    PreparedExifTiffPatchInstance* instance,
    std::span<const ExifTiffPatchUpdate> updates) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
