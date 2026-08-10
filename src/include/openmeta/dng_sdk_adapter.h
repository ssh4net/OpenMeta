// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/metadata_transfer.h"

#include <cstdint>
#include <string>

/**
 * \file dng_sdk_adapter.h
 * \brief Optional Adobe DNG SDK bridge for prepared OpenMeta DNG transfers.
 */

class dng_host;
class dng_negative;
class dng_stream;

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Result status for the optional DNG SDK adapter.
enum class DngSdkAdapterStatus : uint8_t {
    Ok,
    InvalidArgument,
    Unsupported,
    Malformed,
    InternalError,
};

/// Options for applying prepared OpenMeta metadata onto DNG SDK objects.
struct DngSdkAdapterOptions final {
    bool apply_exif            = true;
    bool apply_xmp             = true;
    bool apply_iptc            = true;
    bool synchronize_metadata  = true;
    bool cleanup_for_update    = true;
};

/// Result for DNG SDK adapter apply/update operations.
struct DngSdkAdapterResult final {
    DngSdkAdapterStatus status = DngSdkAdapterStatus::Ok;
    uint32_t applied_blocks    = 0;
    uint32_t skipped_blocks    = 0;
    bool exif_applied          = false;
    bool xmp_applied           = false;
    bool iptc_applied          = false;
    bool synchronized_metadata = false;
    bool cleaned_for_update    = false;
    bool updated_stream        = false;
    TransferBlockKind failed_kind = TransferBlockKind::Other;
    std::string message;
};

/// File-helper options for DNG SDK adapter entry points.
struct ApplyDngSdkMetadataFileOptions final {
    PrepareTransferFileOptions prepare;
    DngSdkAdapterOptions adapter;
};

/// File-helper result for DNG SDK adapter entry points.
struct ApplyDngSdkMetadataFileResult final {
    PrepareTransferFileResult prepared;
    DngSdkAdapterResult adapter;
};

/**
 * \brief Returns true when OpenMeta was built with the optional DNG SDK
 * adapter enabled.
 */
bool
dng_sdk_adapter_available() noexcept;

/**
 * \brief Applies prepared DNG-target metadata onto a DNG SDK negative.
 *
 * Current v1 coverage is bounded to EXIF, XMP, and IPTC payloads emitted by
 * OpenMeta's prepared DNG transfer bundle. This API does not encode pixels or
 * synthesize raw-image structure.
 */
DngSdkAdapterResult
apply_prepared_dng_sdk_metadata(const PreparedTransferBundle& bundle,
                                ::dng_host* host,
                                ::dng_negative* negative,
                                const DngSdkAdapterOptions& options
                                = DngSdkAdapterOptions {}) noexcept;

/**
 * \brief Applies prepared metadata onto a DNG SDK negative, then runs the
 * SDK's in-place metadata update path on an existing DNG stream.
 */
DngSdkAdapterResult
update_prepared_dng_sdk_stream_metadata(
    const PreparedTransferBundle& bundle, ::dng_host* host,
    ::dng_negative* negative, ::dng_stream* stream,
    const DngSdkAdapterOptions& options = DngSdkAdapterOptions {}) noexcept;

/**
 * \brief Reads one source file, prepares a DNG-target bundle, then applies it
 * onto a DNG SDK negative.
 */
ApplyDngSdkMetadataFileResult
apply_dng_sdk_metadata_from_file(
    const char* path, ::dng_host* host, ::dng_negative* negative,
    const ApplyDngSdkMetadataFileOptions& options
    = ApplyDngSdkMetadataFileOptions {}) noexcept;

/**
 * \brief Reads one source file, prepares a DNG-target bundle, applies it onto
 * a DNG SDK negative, then updates an existing DNG stream in place.
 */
ApplyDngSdkMetadataFileResult
update_dng_sdk_stream_metadata_from_file(
    const char* path, ::dng_host* host, ::dng_negative* negative,
    ::dng_stream* stream,
    const ApplyDngSdkMetadataFileOptions& options
    = ApplyDngSdkMetadataFileOptions {}) noexcept;

/**
 * \brief Reads one source file, prepares a DNG-target bundle, parses one
 * existing target DNG file through the Adobe DNG SDK, then updates that
 * target file's metadata in place.
 *
 * This is the thin public file-helper used by bindings and host tools that
 * want an end-to-end "source file -> existing DNG file" metadata update path
 * without directly managing SDK object lifetimes.
 */
ApplyDngSdkMetadataFileResult
update_dng_sdk_file_from_file(
    const char* source_path, const char* target_path,
    const ApplyDngSdkMetadataFileOptions& options
    = ApplyDngSdkMetadataFileOptions {}) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
