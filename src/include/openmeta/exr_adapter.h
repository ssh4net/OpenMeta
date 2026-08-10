// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/exr_decode.h"
#include "openmeta/meta_store.h"
#include "openmeta/metadata_transfer.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

/**
 * \file exr_adapter.h
 * \brief EXR-native attribute bridge for OpenEXR-style host integrations.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Result status for EXR adapter export.
enum class ExrAdapterStatus : uint8_t {
    Ok,
    InvalidArgument,
    Unsupported,
};

/// Options for EXR attribute batch export.
struct ExrAdapterOptions final {
    /// Include opaque/custom EXR attributes when type name is preserved.
    bool include_opaque = true;
    /// If false, unencodable entries are skipped instead of failing.
    bool fail_on_unencodable = true;
};

/// One owned EXR-native attribute payload.
struct ExrAdapterAttribute final {
    uint32_t part_index = 0;
    std::string name;
    std::string type_name;
    std::vector<std::byte> value;
    bool is_opaque = false;
};

/// One owned EXR-native attribute batch.
struct ExrAdapterBatch final {
    uint32_t encoding_version = kExrCanonicalEncodingVersion;
    ExrAdapterOptions options;
    std::vector<ExrAdapterAttribute> attributes;
};

/// One contiguous per-part span inside \ref ExrAdapterBatch::attributes.
struct ExrAdapterPartSpan final {
    uint32_t part_index      = 0;
    uint32_t first_attribute = 0;
    uint32_t attribute_count = 0;
};

/// Result for EXR adapter batch export.
struct ExrAdapterResult final {
    ExrAdapterStatus status = ExrAdapterStatus::Ok;
    uint32_t exported       = 0;
    uint32_t skipped        = 0;
    uint32_t errors         = 0;
    EntryId failed_entry    = kInvalidEntryId;
    std::string message;
};

/// Replay callbacks for \ref replay_exr_attribute_batch.
struct ExrAdapterReplayCallbacks final {
    ExrAdapterStatus (*begin_part)(void* user, uint32_t part_index,
                                   uint32_t attribute_count) noexcept
        = nullptr;
    ExrAdapterStatus (*emit_attribute)(
        void* user, uint32_t part_index,
        const ExrAdapterAttribute* attribute) noexcept
        = nullptr;
    ExrAdapterStatus (*end_part)(void* user, uint32_t part_index) noexcept
        = nullptr;
    void* user = nullptr;
};

/// One zero-copy per-part attribute view over \ref ExrAdapterBatch.
struct ExrAdapterPartView final {
    uint32_t part_index = 0;
    std::span<const ExrAdapterAttribute> attributes;
};

/// Result for EXR adapter batch replay.
struct ExrAdapterReplayResult final {
    ExrAdapterStatus status         = ExrAdapterStatus::Ok;
    uint32_t replayed_parts         = 0;
    uint32_t replayed_attributes    = 0;
    uint32_t failed_part_index      = std::numeric_limits<uint32_t>::max();
    uint32_t failed_attribute_index = std::numeric_limits<uint32_t>::max();
    std::string message;
};

/// File-helper options for \ref build_exr_attribute_batch_from_file.
struct BuildExrAttributeBatchFileOptions final {
    PrepareTransferFileOptions prepare;
    ExrAdapterOptions adapter;
};

/// File-helper result for \ref build_exr_attribute_batch_from_file.
struct BuildExrAttributeBatchFileResult final {
    PrepareTransferFileResult prepared;
    ExrAdapterResult adapter;
};

/**
 * \brief Builds one owned EXR-native attribute batch from a \ref MetaStore.
 *
 * Only \ref MetaKeyKind::ExrAttribute entries are exported. Known scalar and
 * vector EXR types are encoded back into EXR little-endian attribute bytes.
 * Unknown/custom attributes are preserved as opaque raw bytes when the
 * original type name is available in \ref Origin::wire_type_name.
 */
ExrAdapterResult
build_exr_attribute_batch(const MetaStore& store, ExrAdapterBatch* out,
                          const ExrAdapterOptions& options
                          = ExrAdapterOptions {}) noexcept;

/**
 * \brief Builds one EXR-native attribute batch from a prepared EXR transfer
 * bundle.
 *
 * This bridges the transfer pipeline with EXR host integrations: callers can
 * prepare metadata once with \ref prepare_metadata_for_target using
 * \ref TransferTargetFormat::Exr, then materialize a host-facing
 * \ref ExrAdapterBatch without re-projecting from the source \ref MetaStore.
 *
 * Current EXR prepared transfer blocks serialize one logical `string`
 * attribute per block and target part `0`.
 */
ExrAdapterResult
build_prepared_exr_attribute_batch(const PreparedTransferBundle& bundle,
                                   ExrAdapterBatch* out,
                                   const ExrAdapterOptions& options
                                   = ExrAdapterOptions {}) noexcept;

/**
 * \brief Reads one file, prepares EXR transfer metadata, then materializes a
 * host-facing \ref ExrAdapterBatch.
 *
 * This is the direct public file-helper for thin bindings and host tools.
 * Internally it wraps \ref prepare_metadata_for_target_file, forces the
 * prepared target format to \ref TransferTargetFormat::Exr, then calls
 * \ref build_prepared_exr_attribute_batch.
 */
BuildExrAttributeBatchFileResult
build_exr_attribute_batch_from_file(
    const char* path, ExrAdapterBatch* out,
    const BuildExrAttributeBatchFileOptions& options
    = BuildExrAttributeBatchFileOptions {}) noexcept;

/**
 * \brief Builds contiguous per-part spans over one \ref ExrAdapterBatch.
 *
 * The batch must keep attributes grouped by nondecreasing part index. Batches
 * built by \ref build_exr_attribute_batch satisfy this contract.
 */
ExrAdapterStatus
build_exr_attribute_part_spans(const ExrAdapterBatch& batch,
                               std::vector<ExrAdapterPartSpan>* out) noexcept;

/**
 * \brief Builds zero-copy per-part views over one \ref ExrAdapterBatch.
 */
ExrAdapterStatus
build_exr_attribute_part_views(const ExrAdapterBatch& batch,
                               std::vector<ExrAdapterPartView>* out) noexcept;

/**
 * \brief Replays one \ref ExrAdapterBatch through explicit host callbacks.
 *
 * \ref ExrAdapterReplayCallbacks::emit_attribute must be non-null.
 * \ref ExrAdapterReplayCallbacks::begin_part and
 * \ref ExrAdapterReplayCallbacks::end_part are optional.
 */
ExrAdapterReplayResult
replay_exr_attribute_batch(const ExrAdapterBatch& batch,
                           const ExrAdapterReplayCallbacks& callbacks) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
