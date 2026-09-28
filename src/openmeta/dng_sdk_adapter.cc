// SPDX-License-Identifier: Apache-2.0

#include "openmeta/dng_sdk_adapter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

#if defined(OPENMETA_HAS_DNG_SDK) && OPENMETA_HAS_DNG_SDK
#    include "dng_auto_ptr.h"
#    include "dng_exceptions.h"
#    include "dng_file_stream.h"
#    include "dng_host.h"
#    include "dng_info.h"
#    include "dng_negative.h"
#    include "dng_stream.h"
#    include "dng_update_meta.h"
#endif

#include <cstdio>

namespace openmeta {
namespace {

#if !defined(OPENMETA_HAS_DNG_SDK) || !OPENMETA_HAS_DNG_SDK
    static DngSdkAdapterResult
    unsupported_result() noexcept
    {
        DngSdkAdapterResult result;
        result.status  = DngSdkAdapterStatus::Unsupported;
        result.message = "OpenMeta was built without DNG SDK adapter support";
        return result;
    }
#endif

#if defined(OPENMETA_HAS_DNG_SDK) && OPENMETA_HAS_DNG_SDK

    static DngSdkAdapterResult
    invalid_argument_result(const char* message) noexcept
    {
        DngSdkAdapterResult result;
        result.status  = DngSdkAdapterStatus::InvalidArgument;
        result.message = message ? message : "invalid argument";
        return result;
    }

    static DngSdkAdapterResult
    malformed_result(TransferBlockKind kind, const char* message) noexcept
    {
        DngSdkAdapterResult result;
        result.status      = DngSdkAdapterStatus::Malformed;
        result.failed_kind = kind;
        result.message     = message ? message : "malformed payload";
        return result;
    }

    static DngSdkAdapterResult
    internal_error_result(TransferBlockKind kind, const char* message) noexcept
    {
        DngSdkAdapterResult result;
        result.status      = DngSdkAdapterStatus::InternalError;
        result.failed_kind = kind;
        result.message     = message ? message : "internal error";
        return result;
    }

    static bool
    exif_payload_span(std::span<const std::byte> payload,
                      std::span<const std::byte>* out) noexcept
    {
        if (!out) {
            return false;
        }
        static constexpr std::array<std::byte, 6> kExifPrefix = {
            std::byte { 'E' }, std::byte { 'x' }, std::byte { 'i' },
            std::byte { 'f' }, std::byte { 0x00 }, std::byte { 0x00 },
        };
        if (payload.size() >= kExifPrefix.size()
            && std::memcmp(payload.data(), kExifPrefix.data(),
                           kExifPrefix.size())
                   == 0) {
            payload = payload.subspan(kExifPrefix.size());
        }
        if (payload.size() < 8U
            || payload.size()
                   > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            return false;
        }
        *out = payload;
        return true;
    }

    static bool
    parse_exif_block_into_negative(std::span<const std::byte> payload,
                                   ::dng_host* host,
                                   ::dng_negative* negative,
                                   DngSdkAdapterResult* result) noexcept
    {
        if (!host || !negative || !result) {
            return false;
        }

        std::span<const std::byte> tiff_bytes;
        if (!exif_payload_span(payload, &tiff_bytes)) {
            *result = malformed_result(TransferBlockKind::Exif,
                                       "prepared EXIF payload is not a TIFF block");
            return false;
        }

        try {
            ::dng_stream stream(tiff_bytes.data(),
                                static_cast<uint32_t>(tiff_bytes.size()));
            ::dng_info info;
            info.Parse(*host, stream);
            info.PostParse(*host);
            if (!info.fExif.Get()) {
                *result = malformed_result(TransferBlockKind::Exif,
                                           "DNG SDK did not produce EXIF metadata");
                return false;
            }
            negative->ResetExif(info.fExif.Release());
            return true;
        } catch (const ::dng_exception&) {
            *result = malformed_result(TransferBlockKind::Exif,
                                       "DNG SDK rejected the prepared EXIF payload");
            return false;
        } catch (...) {
            *result = internal_error_result(TransferBlockKind::Exif,
                                            "unexpected DNG SDK EXIF parse failure");
            return false;
        }
    }

    static bool
    apply_xmp_block(std::span<const std::byte> payload, ::dng_host* host,
                    ::dng_negative* negative,
                    DngSdkAdapterResult* result) noexcept
    {
        if (!host || !negative || !result) {
            return false;
        }
        if (payload.empty()
            || payload.size()
                   > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            *result = malformed_result(TransferBlockKind::Xmp,
                                       "prepared XMP payload is empty or too large");
            return false;
        }
        try {
            if (!negative->SetXMP(*host, payload.data(),
                                  static_cast<uint32_t>(payload.size()))) {
                *result = malformed_result(TransferBlockKind::Xmp,
                                           "DNG SDK rejected the prepared XMP payload");
                return false;
            }
            return true;
        } catch (const ::dng_exception&) {
            *result = internal_error_result(TransferBlockKind::Xmp,
                                            "DNG SDK XMP apply failed");
            return false;
        } catch (...) {
            *result = internal_error_result(TransferBlockKind::Xmp,
                                            "unexpected DNG SDK XMP apply failure");
            return false;
        }
    }

    static bool
    apply_iptc_block(std::span<const std::byte> payload, ::dng_host* host,
                     ::dng_negative* negative,
                     DngSdkAdapterResult* result) noexcept
    {
        if (!host || !negative || !result) {
            return false;
        }
        if (payload.size()
            > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            *result = malformed_result(TransferBlockKind::IptcIim,
                                       "prepared IPTC payload is too large");
            return false;
        }
        try {
            AutoPtr<dng_memory_block> block(
                host->Allocate(static_cast<uint32_t>(payload.size())));
            if (payload.size() > 0U) {
                std::memcpy(block->Buffer(), payload.data(), payload.size());
            }
            negative->SetIPTC(block);
            return true;
        } catch (const ::dng_exception&) {
            *result = internal_error_result(TransferBlockKind::IptcIim,
                                            "DNG SDK IPTC apply failed");
            return false;
        } catch (...) {
            *result = internal_error_result(TransferBlockKind::IptcIim,
                                            "unexpected DNG SDK IPTC apply failure");
            return false;
        }
    }

#endif

}  // namespace

bool
dng_sdk_adapter_available() noexcept
{
#if defined(OPENMETA_HAS_DNG_SDK) && OPENMETA_HAS_DNG_SDK
    return true;
#else
    return false;
#endif
}

DngSdkAdapterResult
apply_prepared_dng_sdk_metadata(const PreparedTransferBundle& bundle,
                                ::dng_host* host,
                                ::dng_negative* negative,
                                const DngSdkAdapterOptions& options) noexcept
{
    if (!bundle.tiff_ifd0_removals.empty()) {
        DngSdkAdapterResult result;
        result.status = DngSdkAdapterStatus::Unsupported;
        result.message
            = "DNG SDK adapter cannot represent native TIFF IFD0 removals";
        return result;
    }
#if !defined(OPENMETA_HAS_DNG_SDK) || !OPENMETA_HAS_DNG_SDK
    (void)bundle;
    (void)host;
    (void)negative;
    (void)options;
    return unsupported_result();
#else
    if (!host) {
        return invalid_argument_result("null dng_host");
    }
    if (!negative) {
        return invalid_argument_result("null dng_negative");
    }
    if (bundle.target_format != TransferTargetFormat::Dng) {
        return invalid_argument_result("prepared bundle target is not Dng");
    }

    DngSdkAdapterResult result;
    bool saw_supported_block = false;

    for (size_t i = 0; i < bundle.blocks.size(); ++i) {
        const PreparedTransferBlock& block = bundle.blocks[i];
        switch (block.kind) {
        case TransferBlockKind::Exif:
            if (!options.apply_exif || result.exif_applied) {
                result.skipped_blocks += 1U;
                break;
            }
            if (!parse_exif_block_into_negative(block.payload, host, negative,
                                                &result)) {
                return result;
            }
            result.exif_applied = true;
            result.applied_blocks += 1U;
            saw_supported_block = true;
            break;
        case TransferBlockKind::Xmp:
            if (!options.apply_xmp || result.xmp_applied) {
                result.skipped_blocks += 1U;
                break;
            }
            if (!apply_xmp_block(block.payload, host, negative, &result)) {
                return result;
            }
            result.xmp_applied = true;
            result.applied_blocks += 1U;
            saw_supported_block = true;
            break;
        case TransferBlockKind::IptcIim:
            if (!options.apply_iptc || result.iptc_applied) {
                result.skipped_blocks += 1U;
                break;
            }
            if (!apply_iptc_block(block.payload, host, negative, &result)) {
                return result;
            }
            result.iptc_applied = true;
            result.applied_blocks += 1U;
            saw_supported_block = true;
            break;
        default:
            result.skipped_blocks += 1U;
            break;
        }
    }

    if (options.synchronize_metadata) {
        try {
            negative->SynchronizeMetadata();
            result.synchronized_metadata = true;
        } catch (const ::dng_exception&) {
            return internal_error_result(
                TransferBlockKind::Other,
                "DNG SDK metadata synchronization failed");
        } catch (...) {
            return internal_error_result(
                TransferBlockKind::Other,
                "unexpected DNG SDK metadata synchronization failure");
        }
    }

    result.status = DngSdkAdapterStatus::Ok;
    if (!saw_supported_block) {
        result.message = "no supported DNG SDK metadata payloads in bundle";
    }
    return result;
#endif
}

DngSdkAdapterResult
update_prepared_dng_sdk_stream_metadata(
    const PreparedTransferBundle& bundle, ::dng_host* host,
    ::dng_negative* negative, ::dng_stream* stream,
    const DngSdkAdapterOptions& options) noexcept
{
#if !defined(OPENMETA_HAS_DNG_SDK) || !OPENMETA_HAS_DNG_SDK
    (void)bundle;
    (void)host;
    (void)negative;
    (void)stream;
    (void)options;
    return unsupported_result();
#else
    if (!stream) {
        return invalid_argument_result("null dng_stream");
    }

    DngSdkAdapterResult result
        = apply_prepared_dng_sdk_metadata(bundle, host, negative, options);
    if (result.status != DngSdkAdapterStatus::Ok) {
        return result;
    }

    try {
        if (options.cleanup_for_update) {
            CleanUpMetadataForUpdate(*host, negative->Metadata(),
                                     negative->IPTCLength() > 0U);
            result.cleaned_for_update = true;
        }
        DNGUpdateMetadata(*host, *stream, *negative, negative->Metadata());
        result.updated_stream = true;
        return result;
    } catch (const ::dng_exception&) {
        return internal_error_result(TransferBlockKind::Other,
                                     "DNG SDK metadata update failed");
    } catch (...) {
        return internal_error_result(TransferBlockKind::Other,
                                     "unexpected DNG SDK metadata update failure");
    }
#endif
}

ApplyDngSdkMetadataFileResult
apply_dng_sdk_metadata_from_file(
    const char* path, ::dng_host* host, ::dng_negative* negative,
    const ApplyDngSdkMetadataFileOptions& options) noexcept
{
    ApplyDngSdkMetadataFileResult out;
    PrepareTransferFileOptions prepare = options.prepare;
    prepare.prepare.target_format      = TransferTargetFormat::Dng;
    out.prepared = prepare_metadata_for_target_file(path, prepare);
    if (out.prepared.file_status != TransferFileStatus::Ok) {
        out.adapter.status = DngSdkAdapterStatus::InvalidArgument;
        out.adapter.message
            = "prepare_metadata_for_target_file failed before DNG apply";
        return out;
    }
    if (out.prepared.prepare.status != TransferStatus::Ok) {
        out.adapter.status = DngSdkAdapterStatus::Malformed;
        out.adapter.message
            = "OpenMeta failed to prepare a DNG-target transfer bundle";
        return out;
    }
    out.adapter = apply_prepared_dng_sdk_metadata(out.prepared.bundle, host,
                                                  negative, options.adapter);
    return out;
}

ApplyDngSdkMetadataFileResult
update_dng_sdk_stream_metadata_from_file(
    const char* path, ::dng_host* host, ::dng_negative* negative,
    ::dng_stream* stream,
    const ApplyDngSdkMetadataFileOptions& options) noexcept
{
    ApplyDngSdkMetadataFileResult out;
    PrepareTransferFileOptions prepare = options.prepare;
    prepare.prepare.target_format      = TransferTargetFormat::Dng;
    if (prepare.prepare.dng_target_mode
        == DngTargetMode::MinimalFreshScaffold) {
        prepare.prepare.dng_target_mode = DngTargetMode::ExistingTarget;
    }
    out.prepared = prepare_metadata_for_target_file(path, prepare);
    if (out.prepared.file_status != TransferFileStatus::Ok) {
        out.adapter.status = DngSdkAdapterStatus::InvalidArgument;
        out.adapter.message
            = "prepare_metadata_for_target_file failed before DNG update";
        return out;
    }
    if (out.prepared.prepare.status != TransferStatus::Ok) {
        out.adapter.status = DngSdkAdapterStatus::Malformed;
        out.adapter.message
            = "OpenMeta failed to prepare a DNG-target transfer bundle";
        return out;
    }
    out.adapter = update_prepared_dng_sdk_stream_metadata(
        out.prepared.bundle, host, negative, stream, options.adapter);
    return out;
}

ApplyDngSdkMetadataFileResult
update_dng_sdk_file_from_file(
    const char* source_path, const char* target_path,
    const ApplyDngSdkMetadataFileOptions& options) noexcept
{
    ApplyDngSdkMetadataFileResult out;
#if !defined(OPENMETA_HAS_DNG_SDK) || !OPENMETA_HAS_DNG_SDK
    (void)source_path;
    (void)target_path;
    (void)options;
    out.adapter = unsupported_result();
    return out;
#else
    if (!target_path || !*target_path) {
        out.adapter.status = DngSdkAdapterStatus::InvalidArgument;
        out.adapter.message = "empty target path";
        return out;
    }

    std::FILE* file = std::fopen(target_path, "r+b");
    if (!file) {
        out.adapter.status = DngSdkAdapterStatus::InvalidArgument;
        out.adapter.message = "failed to open target DNG for update";
        return out;
    }

    try {
        ::dng_host host;
        AutoPtr<dng_negative> negative(host.Make_dng_negative());
        if (!negative.Get()) {
            std::fclose(file);
            out.adapter.status = DngSdkAdapterStatus::InternalError;
            out.adapter.message = "DNG SDK failed to create dng_negative";
            return out;
        }

        ::dng_file_stream stream(file);
        file = nullptr;

        out = update_dng_sdk_stream_metadata_from_file(
            source_path, &host, negative.Get(), &stream, options);
        if (out.adapter.status == DngSdkAdapterStatus::Ok
            && out.adapter.updated_stream) {
            stream.Flush();
        }
        return out;
    } catch (const ::dng_exception&) {
        if (file) {
            std::fclose(file);
        }
        out.adapter.status = DngSdkAdapterStatus::InternalError;
        out.adapter.message
            = "DNG SDK failed to open or update the target DNG file";
        return out;
    } catch (...) {
        if (file) {
            std::fclose(file);
        }
        out.adapter.status = DngSdkAdapterStatus::InternalError;
        out.adapter.message
            = "unexpected failure while opening or updating target DNG file";
        return out;
    }
#endif
}

}  // namespace openmeta
