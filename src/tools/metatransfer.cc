// SPDX-License-Identifier: Apache-2.0

#include "cli_parse.h"
#include "openmeta/build_info.h"
#include "openmeta/console_format.h"
#include "openmeta/dng_sdk_adapter.h"
#include "openmeta/exr_adapter.h"
#include "openmeta/mapped_file.h"
#include "openmeta/metadata_transfer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <fcntl.h>
#    include <io.h>
#    include <process.h>
#    include <sys/stat.h>
#    include <windows.h>
#else
#    include <cerrno>
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <sys/types.h>
#    include <unistd.h>
#endif

namespace openmeta {
namespace {

    static std::string console_path(std::string_view path)
    {
        std::string escaped;
        (void)append_console_escaped_ascii(path, 4096U, &escaped);
        return escaped;
    }

    struct PendingTimePatch final {
        TimePatchField field = TimePatchField::DateTime;
        std::string value;
    };

    static void usage(const char* argv0)
    {
        std::printf(
            "Usage: %s [options] <file> [file...]\n"
            "\n"
            "Transfer smoke tool (thin wrapper):\n"
            "  read/decode -> prepare_metadata_for_target_file -> execute_prepared_transfer_file\n"
            "\n"
            "Options:\n"
            "  --help                 Show this help\n"
            "  --version              Print OpenMeta build info\n"
            "  --no-build-info        Hide build info header\n"
            "  -i, --input <path>     Input file (repeatable)\n"
            "  --format <portable|lossless>\n"
            "                         XMP mode for transfer-prepared APP1 XMP\n"
            "  --portable             Alias for --format portable\n"
            "  --lossless             Alias for --format lossless\n"
            "  --xmp-include-existing Include existing decoded XMP in generated XMP\n"
            "  --xmp-existing-namespace-policy <known_portable_only|preserve_custom>\n"
            "                         Existing XMP namespace writeback policy\n"
            "                         for generated portable XMP\n"
            "  --xmp-existing-standard-namespace-policy <preserve_all|canonicalize_managed>\n"
            "                         Reconcile existing managed standard portable\n"
            "                         XMP properties against canonical EXIF/IPTC mappings\n"
            "  --xmp-include-existing-sidecar\n"
            "                         Include an existing sibling .xmp sidecar from\n"
            "                         the output/edit target path in generated XMP\n"
            "  --xmp-existing-sidecar-precedence <sidecar_wins|source_wins>\n"
            "                         Conflict precedence between an existing output\n"
            "                         sidecar and source-embedded existing XMP\n"
            "  --xmp-include-existing-destination-embedded\n"
            "                         Include existing embedded XMP from the edit target\n"
            "                         in generated transfer XMP\n"
            "  --xmp-existing-destination-embedded-precedence <destination_wins|source_wins>\n"
            "                         Conflict precedence between existing destination\n"
            "                         embedded XMP and source-embedded existing XMP\n"
            "  --xmp-existing-destination-carrier-precedence <sidecar_wins|embedded_wins>\n"
            "                         Conflict precedence between existing destination\n"
            "                         sidecar XMP and existing destination embedded XMP\n"
            "  --xmp-no-exif-projection\n"
            "                         Do not mirror EXIF-derived properties into generated XMP\n"
            "  --xmp-no-iptc-projection\n"
            "                         Do not mirror IPTC-derived properties into generated XMP\n"
            "  --xmp-conflict-policy <current|existing_wins|generated_wins>\n"
            "                         Conflict policy between existing decoded XMP and\n"
            "                         generated portable EXIF/IPTC XMP properties\n"
            "  --xmp-writeback <embedded|sidecar|embedded_and_sidecar>\n"
            "                         Keep generated XMP embedded, or strip embedded XMP\n"
            "                         carriers and persist a sibling .xmp sidecar when --output is used,\n"
            "                         or keep both embedded and sidecar XMP carriers\n"
            "  --xmp-destination-embedded <preserve_existing|strip_existing>\n"
            "                         Keep or remove destination embedded XMP during sidecar writeback;\n"
            "                         strip_existing is currently supported only for JPEG,\n"
            "                         TIFF, PNG, WebP, JP2, and JXL sidecar-only writeback\n"
            "  --xmp-destination-sidecar <preserve_existing|strip_existing>\n"
            "                         Keep or remove an existing sibling .xmp sidecar when\n"
            "                         writeback stays embedded-only\n"
            "  --xmp-exiftool-gpsdatetime-alias\n"
            "                         Emit exif:GPSDateTime alias in portable mode\n"
            "  --no-exif              Skip EXIF APP1 preparation\n"
            "  --no-xmp               Skip XMP APP1 preparation\n"
            "  --no-icc               Skip ICC APP2 preparation\n"
            "  --no-iptc              Skip IPTC APP13 preparation\n"
            "  --makernotes           Enable best-effort MakerNote decode in read phase\n"
            "  --makernote-policy <keep|drop|invalidate|rewrite>\n"
            "                         Transfer policy for MakerNote payloads\n"
            "  --jumbf-policy <keep|drop|invalidate|rewrite>\n"
            "                         Transfer policy for JUMBF payloads\n"
            "  --c2pa-policy <keep|drop|invalidate|rewrite>\n"
            "                         Transfer policy for C2PA payloads\n"
            "  --transfer-safety <compatible|rendered>\n"
            "                         compatible keeps source color/camera data for\n"
            "                         compatible repackage; rendered drops source raw\n"
            "                         color/correction/ICC/MakerNote/JUMBF data\n"
            "  --jpeg-jumbf <path>   Append one logical raw JUMBF payload to a JPEG bundle\n"
            "  --replace-jpeg-jumbf  Remove prepared jpeg:app11-jumbf blocks before append\n"
            "  --jpeg-c2pa-signed <path>\n"
            "                         Stage one externally signed logical C2PA payload\n"
            "                         for JPEG, JXL, or bounded BMFF targets\n"
            "  --c2pa-manifest-output <path>\n"
            "                         External manifest-builder output bytes for signed C2PA staging\n"
            "  --c2pa-certificate-chain <path>\n"
            "                         External certificate chain bytes for signed C2PA staging\n"
            "  --c2pa-key-ref <text> Private-key reference string for signed C2PA staging\n"
            "  --c2pa-signing-time <text>\n"
            "                         Signing time for signed C2PA staging\n"
            "  --dump-c2pa-binding <path>\n"
            "                         Write exact C2PA content-binding bytes for external signing\n"
            "                         on JPEG, JXL, or bounded BMFF targets\n"
            "  --dump-c2pa-handoff <path>\n"
            "                         Write one persisted C2PA handoff package\n"
            "                         for JPEG, JXL, or bounded BMFF targets\n"
            "  --dump-c2pa-signed-package <path>\n"
            "                         Write one persisted signed C2PA package from current signer inputs\n"
            "                         for JPEG, JXL, or bounded BMFF targets\n"
            "  --load-c2pa-signed-package <path>\n"
            "                         Load one persisted signed C2PA package for staging\n"
            "                         on JPEG, JXL, or bounded BMFF targets\n"
            "  --dump-transfer-payload-batch <path>\n"
            "                         Write one persisted semantic transfer payload batch\n"
            "  --load-transfer-payload-batch <path>\n"
            "                         Load and inspect one persisted semantic transfer payload batch\n"
            "  --dump-transfer-package-batch <path>\n"
            "                         Write one persisted final transfer package batch\n"
            "  --dump-exr-attribute-batch <path>\n"
            "                         Write one human-readable EXR attribute batch dump\n"
            "                         via the direct EXR file helper\n"
            "  --update-dng-sdk-file <path>\n"
            "                         Update one existing DNG file in place via the\n"
            "                         optional Adobe DNG SDK file helper\n"
            "  --load-transfer-package-batch <path>\n"
            "                         Load and inspect one persisted final transfer package batch\n"
            "  --dump-jxl-encoder-handoff <path>\n"
            "                         Write one persisted JXL encoder ICC handoff\n"
            "  --load-jxl-encoder-handoff <path>\n"
            "                         Load and inspect one persisted JXL encoder ICC handoff\n"
            "  --load-transfer-artifact <path>\n"
            "                         Load and inspect one persisted transfer artifact of any supported kind\n"
            "  --no-decompress        Disable payload decompression in read phase\n"
            "  --unsafe-write-payloads\n"
            "                         Write prepared raw block payload bytes to files\n"
            "  --write-payloads       Deprecated alias for --unsafe-write-payloads\n"
            "  --out-dir <dir>        Output directory for --write-payloads\n"
            "  --source-meta <path>   Metadata source for prepare phase\n"
            "  --target-jpeg <path>   Target JPEG stream for edit/apply phase\n"
            "  --target-tiff <path>   Target TIFF stream for edit/apply phase\n"
            "  --target-dng <path>    Target DNG stream for edit/apply phase\n"
            "  --dng-target-mode <existing_target|template_target|minimal_fresh_scaffold>\n"
            "                         Public DNG transfer contract for --target-dng\n"
            "  --target-width N       Host-supplied output image width\n"
            "  --target-height N      Host-supplied output image height\n"
            "  --target-orientation N Host-supplied EXIF orientation (1..8)\n"
            "  --target-samples-per-pixel N\n"
            "                         Host-supplied output samples per pixel\n"
            "  --target-bits-per-sample N[,N...]\n"
            "                         Host-supplied output bits per sample\n"
            "  --target-sample-format N[,N...]\n"
            "                         Host-supplied TIFF sample format values\n"
            "  --target-photometric N Host-supplied TIFF photometric interpretation\n"
            "  --target-planar-configuration N\n"
            "                         Host-supplied TIFF planar configuration (1 or 2)\n"
            "  --target-compression N Host-supplied TIFF compression value\n"
            "  --target-exif-color-space N\n"
            "                         Host-supplied EXIF ColorSpace value\n"
            "  --target-exr           Target EXR metadata transfer\n"
            "  --target-png           Target PNG metadata transfer\n"
            "  --target-jp2           Target JP2 metadata transfer\n"
            "  --target-jxl           Target JPEG XL metadata emit summary\n"
            "  --target-webp          Target WebP metadata transfer\n"
            "  --target-heif          Target HEIF metadata transfer\n"
            "  --target-avif          Target AVIF metadata transfer\n"
            "  --target-cr3           Target CR3 metadata transfer\n"
            "  -o, --output <path>    Write edited output file\n"
            "  --force                Overwrite existing payload files\n"
            "  --dry-run              Plan edit only; do not write output\n"
            "  --mode <auto|in_place|metadata_rewrite>\n"
            "                         JPEG-only edit mode selection for output writer\n"
            "  --require-in-place     Fail if selected plan is not in_place\n"
            "  --emit-repeat N        Emit same prepared bundle N times (default: 1)\n"
            "  --time-patch F=V       Apply time patch update (repeatable)\n"
            "                         F: DateTime|DateTimeOriginal|DateTimeDigitized|\n"
            "                            SubSecTime|SubSecTimeOriginal|SubSecTimeDigitized|\n"
            "                            OffsetTime|OffsetTimeOriginal|OffsetTimeDigitized|\n"
            "                            GpsDateStamp|GpsTimeStamp\n"
            "  --time-patch-lax-width Disable strict patch width checks\n"
            "  --time-patch-require-slot\n"
            "                         Fail if requested patch field is not present\n"
            "  --time-patch-no-auto-nul\n"
            "                         Disable auto NUL append for text patch values\n"
            "  --max-file-bytes N     Optional mapped file cap in bytes (0=unlimited)\n"
            "  --max-payload-bytes N  Max reassembled/decompressed payload bytes\n"
            "  --max-payload-parts N  Max payload part count\n"
            "  --max-exif-ifds N      Max EXIF/TIFF IFD count\n"
            "  --max-exif-entries N   Max EXIF/TIFF entries per IFD\n"
            "  --max-exif-total N     Max EXIF/TIFF total entries\n",
            argv0 ? argv0 : "metatransfer");
    }


    static std::string canonical_time_patch_name(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (std::isalnum(c) == 0) {
                continue;
            }
            const int lower = std::tolower(c);
            out.push_back(static_cast<char>(lower));
        }
        return out;
    }


    static bool parse_time_patch_field(std::string_view name,
                                       TimePatchField* out)
    {
        if (!out) {
            return false;
        }
        const std::string n = canonical_time_patch_name(name);
        if (n == "datetime") {
            *out = TimePatchField::DateTime;
            return true;
        }
        if (n == "datetimeoriginal") {
            *out = TimePatchField::DateTimeOriginal;
            return true;
        }
        if (n == "datetimedigitized") {
            *out = TimePatchField::DateTimeDigitized;
            return true;
        }
        if (n == "subsectime") {
            *out = TimePatchField::SubSecTime;
            return true;
        }
        if (n == "subsectimeoriginal") {
            *out = TimePatchField::SubSecTimeOriginal;
            return true;
        }
        if (n == "subsectimedigitized") {
            *out = TimePatchField::SubSecTimeDigitized;
            return true;
        }
        if (n == "offsettime") {
            *out = TimePatchField::OffsetTime;
            return true;
        }
        if (n == "offsettimeoriginal") {
            *out = TimePatchField::OffsetTimeOriginal;
            return true;
        }
        if (n == "offsettimedigitized") {
            *out = TimePatchField::OffsetTimeDigitized;
            return true;
        }
        if (n == "gpsdatestamp") {
            *out = TimePatchField::GpsDateStamp;
            return true;
        }
        if (n == "gpstimestamp") {
            *out = TimePatchField::GpsTimeStamp;
            return true;
        }
        return false;
    }


    static bool parse_time_patch_arg(const char* arg, PendingTimePatch* out,
                                     std::string* err)
    {
        if (!arg || !out) {
            if (err) {
                *err = "invalid --time-patch argument";
            }
            return false;
        }
        const std::string_view s(arg);
        const size_t eq = s.find('=');
        if (eq == std::string_view::npos || eq == 0U) {
            if (err) {
                *err = "expected --time-patch Field=Value";
            }
            return false;
        }

        const std::string_view f = s.substr(0U, eq);
        const std::string_view v = s.substr(eq + 1U);
        TimePatchField field     = TimePatchField::DateTime;
        if (!parse_time_patch_field(f, &field)) {
            if (err) {
                *err = "unknown --time-patch field";
            }
            return false;
        }

        out->field = field;
        out->value.assign(v.data(), v.size());
        return true;
    }

    static std::vector<TransferTimePatchInput> build_transfer_time_patch_inputs(
        const std::vector<PendingTimePatch>& pending)
    {
        std::vector<TransferTimePatchInput> updates;
        updates.reserve(pending.size());
        for (size_t i = 0; i < pending.size(); ++i) {
            TransferTimePatchInput one;
            one.field      = pending[i].field;
            one.text_value = true;
            one.value.resize(pending[i].value.size());
            for (size_t bi = 0; bi < pending[i].value.size(); ++bi) {
                const unsigned char c = static_cast<unsigned char>(
                    pending[i].value[bi]);
                one.value[bi] = static_cast<std::byte>(c);
            }
            updates.push_back(std::move(one));
        }
        return updates;
    }


    static bool parse_u64_arg(const char* s, uint64_t* out)
    {
        return tool_parse::parse_decimal_u64(s, out);
    }


    static bool parse_u32_arg(const char* s, uint32_t* out)
    {
        return tool_parse::parse_decimal_u32(s, out);
    }

    static bool parse_u16_arg(const char* s, uint16_t* out)
    {
        if (!out) {
            return false;
        }
        uint32_t v = 0U;
        if (!parse_u32_arg(s, &v) || v > 0xFFFFU) {
            return false;
        }
        *out = static_cast<uint16_t>(v);
        return true;
    }

    static bool parse_u16_list_arg(
        const char* s,
        std::array<uint16_t, kTransferTargetImageSpecMaxSamples>* out,
        uint16_t* out_count)
    {
        if (!s || !*s || !out || !out_count) {
            return false;
        }
        out->fill(0U);
        *out_count = 0U;
        std::string_view text(s);
        size_t start = 0U;
        while (start <= text.size()) {
            const size_t comma = text.find(',', start);
            const size_t end = comma == std::string_view::npos ? text.size()
                                                               : comma;
            if (end == start
                || *out_count >= kTransferTargetImageSpecMaxSamples) {
                return false;
            }
            const std::string item(text.substr(start, end - start));
            uint16_t v = 0U;
            if (!parse_u16_arg(item.c_str(), &v) || v == 0U) {
                return false;
            }
            (*out)[*out_count] = v;
            *out_count = static_cast<uint16_t>(*out_count + 1U);
            if (comma == std::string_view::npos) {
                break;
            }
            start = comma + 1U;
        }
        return *out_count > 0U;
    }

    static const char* jpeg_edit_mode_name(JpegEditMode mode) noexcept
    {
        switch (mode) {
        case JpegEditMode::Auto: return "auto";
        case JpegEditMode::InPlace: return "in_place";
        case JpegEditMode::MetadataRewrite: return "metadata_rewrite";
        }
        return "unknown";
    }

    static bool parse_jpeg_edit_mode(const char* s, JpegEditMode* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "auto") == 0) {
            *out = JpegEditMode::Auto;
            return true;
        }
        if (std::strcmp(s, "in_place") == 0) {
            *out = JpegEditMode::InPlace;
            return true;
        }
        if (std::strcmp(s, "metadata_rewrite") == 0) {
            *out = JpegEditMode::MetadataRewrite;
            return true;
        }
        return false;
    }


    static bool parse_transfer_policy_action(const char* s,
                                             TransferPolicyAction* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "keep") == 0) {
            *out = TransferPolicyAction::Keep;
            return true;
        }
        if (std::strcmp(s, "drop") == 0) {
            *out = TransferPolicyAction::Drop;
            return true;
        }
        if (std::strcmp(s, "invalidate") == 0) {
            *out = TransferPolicyAction::Invalidate;
            return true;
        }
        if (std::strcmp(s, "rewrite") == 0) {
            *out = TransferPolicyAction::Rewrite;
            return true;
        }
        return false;
    }

    static bool parse_transfer_safety_mode(const char* s,
                                           TransferSafetyMode* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "compatible") == 0
            || std::strcmp(s, "compatible_file") == 0) {
            *out = TransferSafetyMode::CompatibleFile;
            return true;
        }
        if (std::strcmp(s, "rendered") == 0
            || std::strcmp(s, "rendered_image") == 0) {
            *out = TransferSafetyMode::RenderedImage;
            return true;
        }
        return false;
    }


    static bool parse_xmp_conflict_policy(const char* s,
                                          XmpConflictPolicy* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "current") == 0) {
            *out = XmpConflictPolicy::CurrentBehavior;
            return true;
        }
        if (std::strcmp(s, "existing_wins") == 0) {
            *out = XmpConflictPolicy::ExistingWins;
            return true;
        }
        if (std::strcmp(s, "generated_wins") == 0) {
            *out = XmpConflictPolicy::GeneratedWins;
            return true;
        }
        return false;
    }

    static bool parse_xmp_existing_namespace_policy(
        const char* s, XmpExistingNamespacePolicy* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "known_portable_only") == 0) {
            *out = XmpExistingNamespacePolicy::KnownPortableOnly;
            return true;
        }
        if (std::strcmp(s, "preserve_custom") == 0) {
            *out = XmpExistingNamespacePolicy::PreserveCustom;
            return true;
        }
        return false;
    }

    static bool parse_xmp_existing_standard_namespace_policy(
        const char* s,
        XmpExistingStandardNamespacePolicy* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "preserve_all") == 0) {
            *out = XmpExistingStandardNamespacePolicy::PreserveAll;
            return true;
        }
        if (std::strcmp(s, "canonicalize_managed") == 0) {
            *out = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
            return true;
        }
        return false;
    }

    static bool
    parse_xmp_writeback_mode(const char* s, XmpWritebackMode* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "embedded") == 0) {
            *out = XmpWritebackMode::EmbeddedOnly;
            return true;
        }
        if (std::strcmp(s, "sidecar") == 0) {
            *out = XmpWritebackMode::SidecarOnly;
            return true;
        }
        if (std::strcmp(s, "embedded_and_sidecar") == 0) {
            *out = XmpWritebackMode::EmbeddedAndSidecar;
            return true;
        }
        return false;
    }

    static bool parse_xmp_destination_embedded_mode(
        const char* s, XmpDestinationEmbeddedMode* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "preserve_existing") == 0) {
            *out = XmpDestinationEmbeddedMode::PreserveExisting;
            return true;
        }
        if (std::strcmp(s, "strip_existing") == 0) {
            *out = XmpDestinationEmbeddedMode::StripExisting;
            return true;
        }
        return false;
    }

    static bool parse_xmp_destination_sidecar_mode(
        const char* s, XmpDestinationSidecarMode* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "preserve_existing") == 0) {
            *out = XmpDestinationSidecarMode::PreserveExisting;
            return true;
        }
        if (std::strcmp(s, "strip_existing") == 0) {
            *out = XmpDestinationSidecarMode::StripExisting;
            return true;
        }
        return false;
    }

    static bool parse_xmp_existing_sidecar_precedence(
        const char* s, XmpExistingSidecarPrecedence* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "sidecar_wins") == 0) {
            *out = XmpExistingSidecarPrecedence::SidecarWins;
            return true;
        }
        if (std::strcmp(s, "source_wins") == 0) {
            *out = XmpExistingSidecarPrecedence::SourceWins;
            return true;
        }
        return false;
    }

    static bool parse_xmp_existing_destination_embedded_precedence(
        const char* s,
        XmpExistingDestinationEmbeddedPrecedence* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "destination_wins") == 0) {
            *out = XmpExistingDestinationEmbeddedPrecedence::DestinationWins;
            return true;
        }
        if (std::strcmp(s, "source_wins") == 0) {
            *out = XmpExistingDestinationEmbeddedPrecedence::SourceWins;
            return true;
        }
        return false;
    }

    static bool parse_xmp_existing_destination_carrier_precedence(
        const char* s,
        XmpExistingDestinationCarrierPrecedence* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "sidecar_wins") == 0) {
            *out = XmpExistingDestinationCarrierPrecedence::SidecarWins;
            return true;
        }
        if (std::strcmp(s, "embedded_wins") == 0) {
            *out = XmpExistingDestinationCarrierPrecedence::EmbeddedWins;
            return true;
        }
        return false;
    }

    static bool parse_dng_target_mode(const char* s,
                                      DngTargetMode* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "existing_target") == 0) {
            *out = DngTargetMode::ExistingTarget;
            return true;
        }
        if (std::strcmp(s, "template_target") == 0) {
            *out = DngTargetMode::TemplateTarget;
            return true;
        }
        if (std::strcmp(s, "minimal_fresh_scaffold") == 0) {
            *out = DngTargetMode::MinimalFreshScaffold;
            return true;
        }
        return false;
    }

    static void xmp_sidecar_candidates(const char* path, std::string* out_a,
                                       std::string* out_b) noexcept
    {
        if (out_a) {
            out_a->clear();
        }
        if (out_b) {
            out_b->clear();
        }
        if (!path || !*path || !out_a || !out_b) {
            return;
        }

        const std::string s(path);
        *out_b = s;
        out_b->append(".xmp");

        const size_t sep = s.find_last_of("/\\");
        const size_t dot = s.find_last_of('.');
        if (dot != std::string::npos
            && (sep == std::string::npos || dot > sep)) {
            *out_a = s.substr(0, dot);
            out_a->append(".xmp");
        } else {
            *out_a = *out_b;
        }

        if (*out_a == *out_b) {
            out_b->clear();
        }
    }

    static uint32_t remove_prepared_blocks_by_kind(PreparedTransferBundle* bundle,
                                                   TransferBlockKind kind)
    {
        if (!bundle || bundle->blocks.empty()) {
            return 0U;
        }

        size_t write     = 0U;
        uint32_t removed = 0U;
        for (size_t read = 0U; read < bundle->blocks.size(); ++read) {
            if (bundle->blocks[read].kind == kind) {
                removed += 1U;
                continue;
            }
            if (write != read) {
                bundle->blocks[write] = std::move(bundle->blocks[read]);
            }
            write += 1U;
        }
        bundle->blocks.resize(write);
        return removed;
    }


    static const char*
    transfer_policy_subject_name(TransferPolicySubject subject) noexcept
    {
        switch (subject) {
        case TransferPolicySubject::MakerNote: return "makernote";
        case TransferPolicySubject::Jumbf: return "jumbf";
        case TransferPolicySubject::C2pa: return "c2pa";
        case TransferPolicySubject::XmpExifProjection:
            return "xmp_exif_projection";
        case TransferPolicySubject::XmpIptcProjection:
            return "xmp_iptc_projection";
        case TransferPolicySubject::ImageProperties:
            return "image_properties";
        case TransferPolicySubject::IccProfile: return "icc_profile";
        case TransferPolicySubject::RawColorCalibration:
            return "raw_color_calibration";
        case TransferPolicySubject::CameraRawSettings:
            return "camera_raw_settings";
        }
        return "unknown";
    }


    static const char*
    transfer_policy_action_name(TransferPolicyAction action) noexcept
    {
        switch (action) {
        case TransferPolicyAction::Keep: return "keep";
        case TransferPolicyAction::Drop: return "drop";
        case TransferPolicyAction::Invalidate: return "invalidate";
        case TransferPolicyAction::Rewrite: return "rewrite";
        }
        return "unknown";
    }


    static const char*
    transfer_policy_reason_name(TransferPolicyReason reason) noexcept
    {
        switch (reason) {
        case TransferPolicyReason::Default: return "default";
        case TransferPolicyReason::NotPresent: return "not_present";
        case TransferPolicyReason::ExplicitDrop: return "explicit_drop";
        case TransferPolicyReason::CarrierDisabled: return "carrier_disabled";
        case TransferPolicyReason::ProjectedPayload: return "projected_payload";
        case TransferPolicyReason::DraftInvalidationPayload:
            return "draft_invalidation_payload";
        case TransferPolicyReason::ExternalSignedPayload:
            return "external_signed_payload";
        case TransferPolicyReason::ContentBoundTransferUnavailable:
            return "content_bound_transfer_unavailable";
        case TransferPolicyReason::SignedRewriteUnavailable:
            return "signed_rewrite_unavailable";
        case TransferPolicyReason::PortableInvalidationUnavailable:
            return "portable_invalidation_unavailable";
        case TransferPolicyReason::RewriteUnavailablePreservedRaw:
            return "rewrite_unavailable_preserved_raw";
        case TransferPolicyReason::TargetSerializationUnavailable:
            return "target_serialization_unavailable";
        case TransferPolicyReason::TargetImageProperties:
            return "target_image_properties";
        case TransferPolicyReason::SafetyModeFiltered:
            return "safety_mode_filtered";
        case TransferPolicyReason::RawDataDescriptorFiltered:
            return "raw_data_descriptor_filtered";
        case TransferPolicyReason::OpaquePayloadPreservedUnverified:
            return "opaque_payload_preserved_unverified";
        case TransferPolicyReason::RewriteUnavailableDropped:
            return "rewrite_unavailable_dropped";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_mode_name(TransferC2paMode mode) noexcept
    {
        switch (mode) {
        case TransferC2paMode::NotApplicable: return "not_applicable";
        case TransferC2paMode::NotPresent: return "not_present";
        case TransferC2paMode::Drop: return "drop";
        case TransferC2paMode::DraftUnsignedInvalidation:
            return "draft_unsigned_invalidation";
        case TransferC2paMode::PreserveRaw: return "preserve_raw";
        case TransferC2paMode::SignedRewrite: return "signed_rewrite";
        }
        return "unknown";
    }

    static const char*
    transfer_c2pa_source_kind_name(TransferC2paSourceKind kind) noexcept
    {
        switch (kind) {
        case TransferC2paSourceKind::NotApplicable: return "not_applicable";
        case TransferC2paSourceKind::NotPresent: return "not_present";
        case TransferC2paSourceKind::DecodedOnly: return "decoded_only";
        case TransferC2paSourceKind::ContentBound: return "content_bound";
        case TransferC2paSourceKind::DraftUnsignedInvalidation:
            return "draft_unsigned_invalidation";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_prepared_output_name(
        TransferC2paPreparedOutput output) noexcept
    {
        switch (output) {
        case TransferC2paPreparedOutput::NotApplicable: return "not_applicable";
        case TransferC2paPreparedOutput::NotPresent: return "not_present";
        case TransferC2paPreparedOutput::Dropped: return "dropped";
        case TransferC2paPreparedOutput::PreservedRaw: return "preserved_raw";
        case TransferC2paPreparedOutput::GeneratedDraftUnsignedInvalidation:
            return "generated_draft_unsigned_invalidation";
        case TransferC2paPreparedOutput::SignedRewrite: return "signed_rewrite";
        }
        return "unknown";
    }

    static const char*
    transfer_c2pa_rewrite_state_name(TransferC2paRewriteState state) noexcept
    {
        switch (state) {
        case TransferC2paRewriteState::NotApplicable: return "not_applicable";
        case TransferC2paRewriteState::NotRequested: return "not_requested";
        case TransferC2paRewriteState::SigningMaterialRequired:
            return "signing_material_required";
        case TransferC2paRewriteState::Ready: return "ready";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_rewrite_chunk_kind_name(
        TransferC2paRewriteChunkKind kind) noexcept
    {
        switch (kind) {
        case TransferC2paRewriteChunkKind::SourceRange: return "source_range";
        case TransferC2paRewriteChunkKind::PreparedJpegSegment:
            return "prepared_jpeg_segment";
        case TransferC2paRewriteChunkKind::PreparedJxlBox:
            return "prepared_jxl_box";
        case TransferC2paRewriteChunkKind::PreparedBmffMetaBox:
            return "prepared_bmff_meta_box";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_signed_payload_kind_name(
        TransferC2paSignedPayloadKind kind) noexcept
    {
        switch (kind) {
        case TransferC2paSignedPayloadKind::NotApplicable:
            return "not_applicable";
        case TransferC2paSignedPayloadKind::GenericJumbf:
            return "generic_jumbf";
        case TransferC2paSignedPayloadKind::DraftUnsignedInvalidation:
            return "draft_unsigned_invalidation";
        case TransferC2paSignedPayloadKind::ContentBound:
            return "content_bound";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_semantic_status_name(
        TransferC2paSemanticStatus status) noexcept
    {
        switch (status) {
        case TransferC2paSemanticStatus::NotChecked: return "not_checked";
        case TransferC2paSemanticStatus::Ok: return "ok";
        case TransferC2paSemanticStatus::Invalid: return "invalid";
        }
        return "unknown";
    }

    static const char*
    transfer_target_format_name(TransferTargetFormat format) noexcept
    {
        switch (format) {
        case TransferTargetFormat::Jpeg: return "jpeg";
        case TransferTargetFormat::Tiff: return "tiff";
        case TransferTargetFormat::Dng: return "dng";
        case TransferTargetFormat::Jxl: return "jxl";
        case TransferTargetFormat::Webp: return "webp";
        case TransferTargetFormat::Heif: return "heif";
        case TransferTargetFormat::Avif: return "avif";
        case TransferTargetFormat::Cr3: return "cr3";
        case TransferTargetFormat::Exr: return "exr";
        case TransferTargetFormat::Png: return "png";
        case TransferTargetFormat::Jp2: return "jp2";
        }
        return "unknown";
    }

    static const char*
    transfer_adapter_op_kind_name(TransferAdapterOpKind kind) noexcept
    {
        switch (kind) {
        case TransferAdapterOpKind::JpegMarker: return "jpeg_marker";
        case TransferAdapterOpKind::TiffTagBytes: return "tiff_tag";
        case TransferAdapterOpKind::JxlBox: return "jxl_box";
        case TransferAdapterOpKind::JxlIccProfile: return "jxl_icc_profile";
        case TransferAdapterOpKind::WebpChunk: return "webp_chunk";
        case TransferAdapterOpKind::PngChunk: return "png_chunk";
        case TransferAdapterOpKind::Jp2Box: return "jp2_box";
        case TransferAdapterOpKind::ExrAttribute: return "exr_attribute";
        case TransferAdapterOpKind::BmffItem: return "bmff_item";
        case TransferAdapterOpKind::BmffProperty: return "bmff_property";
        }
        return "unknown";
    }

    static void print_build_info_header()
    {
        std::string line1;
        std::string line2;
        format_build_info_lines(&line1, &line2);
        std::printf("%s\n%s\n", line1.c_str(), line2.c_str());
    }


    static const char* transfer_status_name(TransferStatus status) noexcept
    {
        switch (status) {
        case TransferStatus::Ok: return "ok";
        case TransferStatus::InvalidArgument: return "invalid_argument";
        case TransferStatus::Unsupported: return "unsupported";
        case TransferStatus::LimitExceeded: return "limit_exceeded";
        case TransferStatus::Malformed: return "malformed";
        case TransferStatus::UnsafeData: return "unsafe_data";
        case TransferStatus::InternalError: return "internal_error";
        }
        return "unknown";
    }


    static const char*
    transfer_file_status_name(TransferFileStatus status) noexcept
    {
        switch (status) {
        case TransferFileStatus::Ok: return "ok";
        case TransferFileStatus::InvalidArgument: return "invalid_argument";
        case TransferFileStatus::OpenFailed: return "open_failed";
        case TransferFileStatus::StatFailed: return "stat_failed";
        case TransferFileStatus::TooLarge: return "too_large";
        case TransferFileStatus::MapFailed: return "map_failed";
        case TransferFileStatus::ReadFailed: return "read_failed";
        }
        return "unknown";
    }

    static const char*
    prepare_transfer_code_name(PrepareTransferCode code) noexcept
    {
        switch (code) {
        case PrepareTransferCode::None: return "none";
        case PrepareTransferCode::NullOutBundle: return "null_out_bundle";
        case PrepareTransferCode::UnsupportedTargetFormat:
            return "unsupported_target_format";
        case PrepareTransferCode::ExifPackFailed: return "exif_pack_failed";
        case PrepareTransferCode::XmpPackFailed: return "xmp_pack_failed";
        case PrepareTransferCode::IccPackFailed: return "icc_pack_failed";
        case PrepareTransferCode::IptcPackFailed: return "iptc_pack_failed";
        case PrepareTransferCode::RequestedMetadataNotSerializable:
            return "requested_metadata_not_serializable";
        }
        return "unknown";
    }

    static const char* emit_transfer_code_name(EmitTransferCode code) noexcept
    {
        switch (code) {
        case EmitTransferCode::None: return "none";
        case EmitTransferCode::InvalidArgument: return "invalid_argument";
        case EmitTransferCode::BundleTargetNotJpeg:
            return "bundle_target_not_jpeg";
        case EmitTransferCode::UnsupportedRoute: return "unsupported_route";
        case EmitTransferCode::InvalidPayload: return "invalid_payload";
        case EmitTransferCode::ContentBoundPayloadUnsupported:
            return "content_bound_payload_unsupported";
        case EmitTransferCode::BackendWriteFailed:
            return "backend_write_failed";
        case EmitTransferCode::PlanMismatch: return "plan_mismatch";
        }
        return "unknown";
    }

    static const char*
    prepare_transfer_file_code_name(PrepareTransferFileCode code) noexcept
    {
        switch (code) {
        case PrepareTransferFileCode::None: return "none";
        case PrepareTransferFileCode::EmptyPath: return "empty_path";
        case PrepareTransferFileCode::MapFailed: return "map_failed";
        case PrepareTransferFileCode::PayloadBufferPlatformLimit:
            return "payload_buffer_platform_limit";
        case PrepareTransferFileCode::DecodeFailed: return "decode_failed";
        }
        return "unknown";
    }


    static const char* exr_adapter_status_name(ExrAdapterStatus status) noexcept
    {
        switch (status) {
        case ExrAdapterStatus::Ok: return "ok";
        case ExrAdapterStatus::InvalidArgument: return "invalid_argument";
        case ExrAdapterStatus::Unsupported: return "unsupported";
        }
        return "unknown";
    }


    static const char*
    dng_sdk_adapter_status_name(DngSdkAdapterStatus status) noexcept
    {
        switch (status) {
        case DngSdkAdapterStatus::Ok: return "ok";
        case DngSdkAdapterStatus::InvalidArgument:
            return "invalid_argument";
        case DngSdkAdapterStatus::Unsupported: return "unsupported";
        case DngSdkAdapterStatus::Malformed: return "malformed";
        case DngSdkAdapterStatus::InternalError: return "internal_error";
        }
        return "unknown";
    }


    static const char* transfer_kind_name(TransferBlockKind kind) noexcept
    {
        switch (kind) {
        case TransferBlockKind::Exif: return "exif";
        case TransferBlockKind::Xmp: return "xmp";
        case TransferBlockKind::IptcIim: return "iptc_iim";
        case TransferBlockKind::PhotoshopIrb: return "photoshop_irb";
        case TransferBlockKind::Icc: return "icc";
        case TransferBlockKind::Jumbf: return "jumbf";
        case TransferBlockKind::C2pa: return "c2pa";
        case TransferBlockKind::ExrAttribute: return "exr_attribute";
        case TransferBlockKind::Other: return "other";
        }
        return "unknown";
    }


    static const char* scan_status_name(ScanStatus status) noexcept
    {
        switch (status) {
        case ScanStatus::Ok: return "ok";
        case ScanStatus::OutputTruncated: return "output_truncated";
        case ScanStatus::Unsupported: return "unsupported";
        case ScanStatus::Malformed: return "malformed";
        }
        return "unknown";
    }


    static const char* payload_status_name(PayloadStatus status) noexcept
    {
        switch (status) {
        case PayloadStatus::Ok: return "ok";
        case PayloadStatus::OutputTruncated: return "output_truncated";
        case PayloadStatus::Unsupported: return "unsupported";
        case PayloadStatus::Malformed: return "malformed";
        case PayloadStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static const char* exif_status_name(ExifDecodeStatus status) noexcept
    {
        switch (status) {
        case ExifDecodeStatus::Ok: return "ok";
        case ExifDecodeStatus::OutputTruncated: return "output_truncated";
        case ExifDecodeStatus::Unsupported: return "unsupported";
        case ExifDecodeStatus::Malformed: return "malformed";
        case ExifDecodeStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static const char* xmp_status_name(XmpDecodeStatus status) noexcept
    {
        switch (status) {
        case XmpDecodeStatus::Ok: return "ok";
        case XmpDecodeStatus::OutputTruncated: return "output_truncated";
        case XmpDecodeStatus::Unsupported: return "unsupported";
        case XmpDecodeStatus::Malformed: return "malformed";
        case XmpDecodeStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static const char* jumbf_status_name(JumbfDecodeStatus status) noexcept
    {
        switch (status) {
        case JumbfDecodeStatus::Ok: return "ok";
        case JumbfDecodeStatus::Unsupported: return "unsupported";
        case JumbfDecodeStatus::Malformed: return "malformed";
        case JumbfDecodeStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static std::string basename_only(const std::string& path)
    {
        const size_t sep = path.find_last_of("/\\");
        if (sep == std::string::npos) {
            return path;
        }
        return path.substr(sep + 1U);
    }


    static std::string join_path(const std::string& dir,
                                 const std::string& name)
    {
        if (dir.empty()) {
            return name;
        }
        const char back = dir.back();
        if (back == '/' || back == '\\') {
            return dir + name;
        }
        return dir + "/" + name;
    }


    static std::string sanitize_filename(std::string s)
    {
        for (size_t i = 0; i < s.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            const bool ok         = std::isalnum(c) != 0 || c == '.' || c == '_'
                            || c == '-';
            if (!ok) {
                s[i] = '_';
            }
        }
        if (s.empty()) {
            return "file";
        }
        return s;
    }


    static std::string format_index(uint32_t idx)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%03u", idx);
        return std::string(buf);
    }


    static bool file_exists(const std::string& path)
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        std::fclose(f);
        return true;
    }


    static bool paths_refer_to_same_file(const std::string& a,
                                         const std::string& b) noexcept
    {
        if (a.empty() || b.empty()) {
            return false;
        }
#if defined(_WIN32)
        const DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
        HANDLE ah         = CreateFileA(a.c_str(), 0,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE
                                            | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, flags, nullptr);
        if (ah == INVALID_HANDLE_VALUE) {
            return false;
        }
        HANDLE bh = CreateFileA(b.c_str(), 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE
                                    | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, flags, nullptr);
        if (bh == INVALID_HANDLE_VALUE) {
            CloseHandle(ah);
            return false;
        }
        BY_HANDLE_FILE_INFORMATION ai {};
        BY_HANDLE_FILE_INFORMATION bi {};
        const bool ok = GetFileInformationByHandle(ah, &ai) != 0
                        && GetFileInformationByHandle(bh, &bi) != 0;
        CloseHandle(ah);
        CloseHandle(bh);
        return ok && ai.dwVolumeSerialNumber == bi.dwVolumeSerialNumber
               && ai.nFileIndexHigh == bi.nFileIndexHigh
               && ai.nFileIndexLow == bi.nFileIndexLow;
#else
        struct stat as {};
        struct stat bs {};
        return ::stat(a.c_str(), &as) == 0 && ::stat(b.c_str(), &bs) == 0
               && as.st_dev == bs.st_dev && as.st_ino == bs.st_ino;
#endif
    }


    static bool write_file_bytes(const std::string& path,
                                 std::span<const std::byte> bytes)
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            return false;
        }
        size_t written = 0;
        if (!bytes.empty()) {
            written = std::fwrite(bytes.data(), 1, bytes.size(), f);
        }
        std::fclose(f);
        return written == bytes.size();
    }

    struct CliPreparedTransferFileState final {
        bool xmp_sidecar_requested = false;
        TransferStatus xmp_sidecar_status = TransferStatus::Unsupported;
        std::string xmp_sidecar_message;
        std::string xmp_sidecar_path;
        std::vector<std::byte> xmp_sidecar_output;
        bool xmp_sidecar_cleanup_requested = false;
        TransferStatus xmp_sidecar_cleanup_status
            = TransferStatus::Unsupported;
        std::string xmp_sidecar_cleanup_message;
        std::string xmp_sidecar_cleanup_path;
    };

    static CliPreparedTransferFileState make_cli_prepared_transfer_file_state(
        bool xmp_sidecar_requested, TransferStatus xmp_sidecar_status,
        const std::string& xmp_sidecar_message,
        const std::string& xmp_sidecar_path,
        const std::vector<std::byte>& xmp_sidecar_output,
        bool xmp_sidecar_cleanup_requested,
        TransferStatus xmp_sidecar_cleanup_status,
        const std::string& xmp_sidecar_cleanup_message,
        const std::string& xmp_sidecar_cleanup_path)
    {
        CliPreparedTransferFileState out;
        out.xmp_sidecar_requested = xmp_sidecar_requested;
        out.xmp_sidecar_status    = xmp_sidecar_status;
        out.xmp_sidecar_message   = xmp_sidecar_message;
        out.xmp_sidecar_path      = xmp_sidecar_path;
        out.xmp_sidecar_output    = xmp_sidecar_output;
        out.xmp_sidecar_cleanup_requested = xmp_sidecar_cleanup_requested;
        out.xmp_sidecar_cleanup_status    = xmp_sidecar_cleanup_status;
        out.xmp_sidecar_cleanup_message   = xmp_sidecar_cleanup_message;
        out.xmp_sidecar_cleanup_path      = xmp_sidecar_cleanup_path;
        return out;
    }

    static bool persist_cli_edit_output_via_core(
        const char* edit_label, const std::string& output_path, bool force,
        bool output_already_written, uint64_t prewritten_output_bytes,
        const PrepareTransferFileResult& prepared,
        const ExecutePreparedTransferResult& exec,
        const CliPreparedTransferFileState& file_state)
    {
        ExecutePreparedTransferFileResult executed;
        executed.prepared                    = prepared;
        executed.execute                     = exec;
        executed.xmp_sidecar_requested       = file_state.xmp_sidecar_requested;
        executed.xmp_sidecar_status          = file_state.xmp_sidecar_status;
        executed.xmp_sidecar_message         = file_state.xmp_sidecar_message;
        executed.xmp_sidecar_path            = file_state.xmp_sidecar_path;
        executed.xmp_sidecar_output          = file_state.xmp_sidecar_output;
        executed.xmp_sidecar_cleanup_requested
            = file_state.xmp_sidecar_cleanup_requested;
        executed.xmp_sidecar_cleanup_status
            = file_state.xmp_sidecar_cleanup_status;
        executed.xmp_sidecar_cleanup_message
            = file_state.xmp_sidecar_cleanup_message;
        executed.xmp_sidecar_cleanup_path
            = file_state.xmp_sidecar_cleanup_path;

        PersistPreparedTransferFileOptions persist_options;
        persist_options.output_path                    = output_path;
        persist_options.write_output                   = !output_already_written;
        persist_options.overwrite_output               = force;
        persist_options.prewritten_output_bytes        = prewritten_output_bytes;
        persist_options.overwrite_xmp_sidecar          = force;
        persist_options.remove_destination_xmp_sidecar = true;

        const PersistPreparedTransferFileResult persisted
            = persist_prepared_transfer_file_result(executed, persist_options);
        if (persisted.output_status != TransferStatus::Ok) {
            std::fprintf(stderr, "  %s: persist_failed: %s\n", edit_label,
                         persisted.output_message.c_str());
            return false;
        }

        std::printf("  output=%s bytes=%llu\n",
                    console_path(persisted.output_path).c_str(),
                    static_cast<unsigned long long>(persisted.output_bytes));

        if (file_state.xmp_sidecar_requested) {
            if (persisted.xmp_sidecar_status != TransferStatus::Ok) {
                std::fprintf(stderr, "  xmp_sidecar_write: %s\n",
                             persisted.xmp_sidecar_message.c_str());
                return false;
            }
            if (persisted.xmp_sidecar_bytes != 0U) {
                std::printf("  xmp_sidecar_output=%s bytes=%llu\n",
                            console_path(persisted.xmp_sidecar_path).c_str(),
                            static_cast<unsigned long long>(
                                persisted.xmp_sidecar_bytes));
            }
        }

        if (file_state.xmp_sidecar_cleanup_requested) {
            if (persisted.xmp_sidecar_cleanup_status != TransferStatus::Ok) {
                std::fprintf(stderr, "  xmp_sidecar_remove: %s\n",
                             persisted.xmp_sidecar_cleanup_message.c_str());
                return false;
            }
            if (persisted.xmp_sidecar_cleanup_removed) {
                std::printf(
                    "  xmp_sidecar_removed=%s\n",
                    console_path(persisted.xmp_sidecar_cleanup_path).c_str());
            }
        }

        return true;
    }


    static bool read_file_bytes(const std::string& path,
                                std::vector<std::byte>* out)
    {
        if (!out) {
            return false;
        }
        out->clear();
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        if (std::fseek(f, 0, SEEK_END) != 0) {
            std::fclose(f);
            return false;
        }
        const long end = std::ftell(f);
        if (end < 0) {
            std::fclose(f);
            return false;
        }
        if (std::fseek(f, 0, SEEK_SET) != 0) {
            std::fclose(f);
            return false;
        }
        out->resize(static_cast<size_t>(end));
        size_t read = 0U;
        if (!out->empty()) {
            read = std::fread(out->data(), 1, out->size(), f);
        }
        std::fclose(f);
        return read == out->size();
    }

    static std::string_view
    byte_vector_as_string_view(const std::vector<std::byte>& bytes) noexcept
    {
        if (bytes.empty()) {
            return std::string_view();
        }
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static uint32_t count_exr_batch_parts(const ExrAdapterBatch& batch) noexcept
    {
        if (batch.attributes.empty()) {
            return 0U;
        }
        uint32_t count     = 1U;
        uint32_t prev_part = batch.attributes[0].part_index;
        for (size_t i = 1; i < batch.attributes.size(); ++i) {
            if (batch.attributes[i].part_index != prev_part) {
                count += 1U;
                prev_part = batch.attributes[i].part_index;
            }
        }
        return count;
    }

    static void append_exr_attribute_batch_dump(
        const char* source_path, const BuildExrAttributeBatchFileResult& result,
        const ExrAdapterBatch& batch, std::string* out)
    {
        if (!out) {
            return;
        }

        out->clear();
        out->append("source=");
        if (source_path) {
            out->append(source_path);
        } else {
            out->append("-");
        }
        out->push_back('\n');

        out->append("file: status=");
        out->append(transfer_file_status_name(result.prepared.file_status));
        out->append(" code=");
        out->append(prepare_transfer_file_code_name(result.prepared.code));
        out->append(" size=");
        out->append(std::to_string(result.prepared.file_size));
        out->append(" entries=");
        out->append(std::to_string(result.prepared.entry_count));
        out->push_back('\n');
        if (!result.prepared.xmp_existing_sidecar_path.empty()) {
            out->append("file_xmp_existing_sidecar: loaded=");
            out->append(result.prepared.xmp_existing_sidecar_loaded ? "yes"
                                                                    : "no");
            out->append(" status=");
            out->append(
                transfer_status_name(result.prepared.xmp_existing_sidecar_status));
            out->append(" path=");
            out->append(result.prepared.xmp_existing_sidecar_path);
            out->push_back('\n');
        }
        if (!result.prepared.xmp_existing_sidecar_message.empty()) {
            out->append("file_xmp_existing_sidecar_message=");
            out->append(result.prepared.xmp_existing_sidecar_message);
            out->push_back('\n');
        }
        if (!result.prepared.xmp_existing_destination_embedded_path.empty()) {
            out->append("file_xmp_existing_destination_embedded: loaded=");
            out->append(
                result.prepared.xmp_existing_destination_embedded_loaded
                    ? "yes"
                    : "no");
            out->append(" status=");
            out->append(transfer_status_name(
                result.prepared.xmp_existing_destination_embedded_status));
            out->append(" path=");
            out->append(
                result.prepared.xmp_existing_destination_embedded_path);
            out->push_back('\n');
        }
        if (!result.prepared.xmp_existing_destination_embedded_message.empty()) {
            out->append("file_xmp_existing_destination_embedded_message=");
            out->append(
                result.prepared.xmp_existing_destination_embedded_message);
            out->push_back('\n');
        }

        out->append("prepare: status=");
        out->append(transfer_status_name(result.prepared.prepare.status));
        out->append(" code=");
        out->append(prepare_transfer_code_name(result.prepared.prepare.code));
        out->append(" warnings=");
        out->append(std::to_string(result.prepared.prepare.warnings));
        out->append(" errors=");
        out->append(std::to_string(result.prepared.prepare.errors));
        out->append(" blocks=");
        out->append(std::to_string(result.prepared.bundle.blocks.size()));
        out->push_back('\n');
        if (!result.prepared.prepare.message.empty()) {
            out->append("prepare_message=");
            out->append(result.prepared.prepare.message);
            out->push_back('\n');
        }

        out->append("exr_attribute_batch: status=");
        out->append(exr_adapter_status_name(result.adapter.status));
        out->append(" exported=");
        out->append(std::to_string(result.adapter.exported));
        out->append(" skipped=");
        out->append(std::to_string(result.adapter.skipped));
        out->append(" errors=");
        out->append(std::to_string(result.adapter.errors));
        out->append(" parts=");
        out->append(std::to_string(count_exr_batch_parts(batch)));
        out->append(" entries=");
        out->append(std::to_string(batch.attributes.size()));
        out->push_back('\n');
        if (!result.adapter.message.empty()) {
            out->append("exr_attribute_batch_message=");
            out->append(result.adapter.message);
            out->push_back('\n');
        }

        for (size_t i = 0; i < batch.attributes.size(); ++i) {
            const ExrAdapterAttribute& attr = batch.attributes[i];
            out->push_back('[');
            out->append(std::to_string(i));
            out->append("] part=");
            out->append(std::to_string(attr.part_index));
            out->append(" name=");
            out->append(attr.name);
            out->append(" type=");
            out->append(attr.type_name);
            out->append(" opaque=");
            out->append(attr.is_opaque ? "yes" : "no");
            out->append(" bytes=");
            out->append(std::to_string(attr.value.size()));
            out->push_back('\n');

            if (!attr.is_opaque && attr.type_name == "string") {
                out->append("    value_ascii=");
                const std::string_view text = byte_vector_as_string_view(
                    attr.value);
                (void)append_console_escaped_ascii(text, 256U, out);
                out->push_back('\n');
            }

            out->append("    value_hex=");
            append_hex_bytes(std::span<const std::byte>(attr.value.data(),
                                                        attr.value.size()),
                             256U, out);
            out->push_back('\n');
        }
    }


    class FileByteWriter final : public TransferByteWriter {
    public:
        explicit FileByteWriter(std::string path, bool overwrite)
            : path_(std::move(path))
            , overwrite_(overwrite)
        {
        }

        ~FileByteWriter() override { close_handle(); }

        TransferStatus write(std::span<const std::byte> bytes) noexcept override
        {
            if (bytes.empty()) {
                return TransferStatus::Ok;
            }
            if (!ensure_open()) {
                return status_;
            }
            const size_t written = std::fwrite(bytes.data(), 1, bytes.size(),
                                               file_);
            if (written != bytes.size()) {
                status_ = TransferStatus::InternalError;
                return status_;
            }
            bytes_written_ += static_cast<uint64_t>(written);
            return TransferStatus::Ok;
        }


        TransferStatus finish() noexcept
        {
            if (!file_) {
                return status_;
            }
            bool ok = std::fflush(file_) == 0;
#if defined(_WIN32)
            if (ok && _commit(_fileno(file_)) != 0) {
                ok = false;
            }
#else
            if (ok && ::fsync(fileno(file_)) != 0) {
                ok = false;
            }
#endif
            if (std::fclose(file_) != 0) {
                ok = false;
            }
            file_ = nullptr;
            if (!ok || !publish()) {
                if (!temp_path_.empty()) {
                    std::remove(temp_path_.c_str());
                }
                file_   = nullptr;
                status_ = TransferStatus::InternalError;
                return status_;
            }
            status_ = TransferStatus::Ok;
            return status_;
        }


        uint64_t bytes_written() const noexcept { return bytes_written_; }

    private:
        bool ensure_open() noexcept
        {
            if (file_) {
                return true;
            }
            static std::atomic<uint64_t> nonce { 1U };
            for (uint32_t attempt = 0; attempt < 128U && !file_; ++attempt) {
                temp_path_ = path_;
                temp_path_.append(".openmeta-tmp-");
#if defined(_WIN32)
                temp_path_.append(
                    std::to_string(static_cast<unsigned long>(_getpid())));
#else
                temp_path_.append(
                    std::to_string(static_cast<unsigned long>(::getpid())));
#endif
                temp_path_.push_back('-');
                temp_path_.append(
                    std::to_string(static_cast<unsigned long long>(
                        nonce.fetch_add(1U, std::memory_order_relaxed))));
#if defined(_WIN32)
                const int fd = _open(temp_path_.c_str(),
                                     _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY
                                         | _O_NOINHERIT,
                                     _S_IREAD | _S_IWRITE);
                if (fd >= 0) {
                    file_ = _fdopen(fd, "wb");
                    if (!file_) {
                        _close(fd);
                    }
                }
#else
                const int fd = ::open(temp_path_.c_str(),
                                      O_WRONLY | O_CREAT | O_EXCL
#    if defined(O_CLOEXEC)
                                          | O_CLOEXEC
#    endif
#    if defined(O_NOFOLLOW)
                                          | O_NOFOLLOW
#    endif
                                      ,
                                      0666);
                if (fd >= 0) {
                    file_ = fdopen(fd, "wb");
                    if (!file_) {
                        ::close(fd);
                    }
                }
#endif
            }
            if (!file_) {
                status_ = TransferStatus::InternalError;
                return false;
            }
            return true;
        }


        void close_handle() noexcept
        {
            if (file_) {
                std::fclose(file_);
                file_ = nullptr;
            }
            if (!temp_path_.empty()) {
                std::remove(temp_path_.c_str());
            }
        }


        bool publish() noexcept
        {
#if defined(_WIN32)
            const DWORD flags = MOVEFILE_WRITE_THROUGH
                                | (overwrite_ ? MOVEFILE_REPLACE_EXISTING : 0U);
            return MoveFileExA(temp_path_.c_str(), path_.c_str(), flags) != 0;
#else
            if (overwrite_) {
                return ::rename(temp_path_.c_str(), path_.c_str()) == 0;
            }
            if (::link(temp_path_.c_str(), path_.c_str()) != 0) {
                return false;
            }
            return ::unlink(temp_path_.c_str()) == 0;
#endif
        }


        std::string path_;
        std::string temp_path_;
        std::FILE* file_        = nullptr;
        TransferStatus status_  = TransferStatus::Ok;
        uint64_t bytes_written_ = 0;
        bool overwrite_         = false;
    };


    static std::string payload_dump_path(const std::string& in_path,
                                         const std::string& out_dir,
                                         const PreparedTransferBlock& block,
                                         uint32_t idx)
    {
        std::string base  = sanitize_filename(basename_only(in_path));
        std::string route = sanitize_filename(block.route);
        std::string out   = base;
        out.append(".meta.");
        out.append(format_index(idx));
        out.push_back('.');
        out.append(route);
        out.append(".bin");
        return join_path(out_dir, out);
    }


    static std::string marker_name(uint8_t marker)
    {
        if (marker >= 0xE0U && marker <= 0xEFU) {
            const uint32_t n = static_cast<uint32_t>(marker - 0xE0U);
            char b[24];
            std::snprintf(b, sizeof(b), "APP%u(0x%02X)", n,
                          static_cast<unsigned>(marker));
            return std::string(b);
        }
        if (marker == 0xFEU) {
            return std::string("COM(0xFE)");
        }
        char b[24];
        std::snprintf(b, sizeof(b), "0x%02X", static_cast<unsigned>(marker));
        return std::string(b);
    }

    static std::string jxl_box_name(const std::array<char, 4>& type)
    {
        return std::string(type.data(), type.size());
    }

    static std::string webp_chunk_name(const std::array<char, 4>& type)
    {
        return std::string(type.data(), type.size());
    }

    static std::string png_chunk_name(const std::array<char, 4>& type)
    {
        return std::string(type.data(), type.size());
    }

    static std::string bmff_fourcc_name(uint32_t v) noexcept
    {
        char out[5];
        out[0] = static_cast<char>((v >> 24) & 0xFFU);
        out[1] = static_cast<char>((v >> 16) & 0xFFU);
        out[2] = static_cast<char>((v >> 8) & 0xFFU);
        out[3] = static_cast<char>(v & 0xFFU);
        out[4] = '\0';
        return std::string(out, 4U);
    }

    static std::string bmff_item_name(uint32_t item_type,
                                      bool mime_xmp) noexcept
    {
        if (mime_xmp) {
            return "mime/xmp";
        }
        return bmff_fourcc_name(item_type);
    }

    static std::string bmff_property_name(uint32_t property_type,
                                          uint32_t property_subtype) noexcept
    {
        return bmff_fourcc_name(property_type) + "/"
               + bmff_fourcc_name(property_subtype);
    }

    static bool target_format_is_bmff(TransferTargetFormat format) noexcept
    {
        return format == TransferTargetFormat::Heif
               || format == TransferTargetFormat::Avif
               || format == TransferTargetFormat::Cr3;
    }

    static bool
    target_format_is_tiff_family(TransferTargetFormat format) noexcept
    {
        return format == TransferTargetFormat::Tiff
               || format == TransferTargetFormat::Dng;
    }

    static void
    print_transfer_payload_view_summary(const PreparedTransferPayloadView& view,
                                        uint32_t index)
    {
        std::printf("  [%u] semantic=%s route=%s size=%llu op=%s", index,
                    view.semantic_name.data(), view.route.data(),
                    static_cast<unsigned long long>(view.payload.size()),
                    transfer_adapter_op_kind_name(view.op.kind));
        switch (view.op.kind) {
        case TransferAdapterOpKind::JpegMarker:
            std::printf(" marker=%s",
                        marker_name(view.op.jpeg_marker_code).c_str());
            break;
        case TransferAdapterOpKind::TiffTagBytes:
            std::printf(" tag=0x%04X", static_cast<unsigned>(view.op.tiff_tag));
            break;
        case TransferAdapterOpKind::JxlBox:
            std::printf(" type=%s", jxl_box_name(view.op.box_type).c_str());
            break;
        case TransferAdapterOpKind::JxlIccProfile: break;
        case TransferAdapterOpKind::WebpChunk:
            std::printf(" type=%s",
                        webp_chunk_name(view.op.chunk_type).c_str());
            break;
        case TransferAdapterOpKind::PngChunk:
            std::printf(" type=%s",
                        png_chunk_name(view.op.chunk_type).c_str());
            break;
        case TransferAdapterOpKind::Jp2Box:
            std::printf(" type=%s", jxl_box_name(view.op.box_type).c_str());
            break;
        case TransferAdapterOpKind::ExrAttribute: break;
        case TransferAdapterOpKind::BmffItem:
            std::printf(" type=%s", bmff_item_name(view.op.bmff_item_type,
                                                   view.op.bmff_mime_xmp)
                                        .c_str());
            break;
        case TransferAdapterOpKind::BmffProperty:
            std::printf(" type=%s",
                        bmff_property_name(view.op.bmff_property_type,
                                           view.op.bmff_property_subtype)
                            .c_str());
            break;
        }
        std::printf("\n");
    }

    static const char*
    transfer_package_chunk_kind_name(TransferPackageChunkKind kind) noexcept
    {
        switch (kind) {
        case TransferPackageChunkKind::SourceRange: return "source_range";
        case TransferPackageChunkKind::InlineBytes: return "inline_bytes";
        case TransferPackageChunkKind::PreparedTransferBlock:
            return "prepared_transfer_block";
        case TransferPackageChunkKind::PreparedJpegSegment:
            return "prepared_jpeg_segment";
        }
        return "unknown";
    }

    static void
    print_transfer_package_view_summary(const PreparedTransferPackageView& view,
                                        uint32_t index)
    {
        const std::string_view semantic = transfer_semantic_name(
            view.semantic_kind);
        const char* route = view.route.empty() ? "-" : view.route.data();
        std::printf(
            "  [%u] semantic=%.*s route=%s size=%llu chunk=%s offset=%llu",
            index, static_cast<int>(semantic.size()), semantic.data(), route,
            static_cast<unsigned long long>(view.bytes.size()),
            transfer_package_chunk_kind_name(view.package_kind),
            static_cast<unsigned long long>(view.output_offset));
        if (view.jpeg_marker_code != 0U) {
            std::printf(" marker=%s",
                        marker_name(view.jpeg_marker_code).c_str());
        }
        std::printf("\n");
    }

}  // namespace
}  // namespace openmeta


int
main(int argc, char** argv)
{
    using namespace openmeta;

    bool show_build_info      = true;
    bool force                = false;
    bool write_payloads       = false;
    bool dry_run              = false;
    bool replace_jpeg_jumbf   = false;
    bool c2pa_stage_requested = false;
    uint32_t emit_repeat      = 1U;
    bool time_patch_auto_nul  = true;
    std::string out_dir;
    std::string source_meta_path;
    std::string target_jpeg_path;
    std::string target_tiff_path;
    std::string target_dng_path;
    bool target_exr  = false;
    bool target_png  = false;
    bool target_jp2  = false;
    bool target_jxl  = false;
    bool target_webp = false;
    bool target_heif = false;
    bool target_avif = false;
    bool target_cr3  = false;
    bool target_width_set = false;
    bool target_height_set = false;
    bool target_image_spec_requested = false;
    std::string jpeg_jumbf_path;
    std::string jpeg_c2pa_signed_path;
    std::string c2pa_manifest_output_path;
    std::string c2pa_certificate_chain_path;
    std::string c2pa_key_ref;
    std::string c2pa_signing_time;
    std::string c2pa_binding_output_path;
    std::string c2pa_handoff_output_path;
    std::string c2pa_signed_package_output_path;
    std::string c2pa_signed_package_input_path;
    std::string transfer_payload_batch_output_path;
    std::string transfer_payload_batch_input_path;
    std::string transfer_package_batch_output_path;
    std::string transfer_package_batch_input_path;
    std::string exr_attribute_batch_output_path;
    std::string dng_sdk_update_target_path;
    std::string jxl_encoder_handoff_output_path;
    std::string jxl_encoder_handoff_input_path;
    std::string transfer_artifact_input_path;
    std::string output_path;
    std::vector<std::string> input_paths;
    std::vector<PendingTimePatch> pending_time_patches;
    ApplyTimePatchOptions time_patch_opts;
    PlanJpegEditOptions edit_plan_opts;

    PrepareTransferFileOptions options;
    options.prepare.target_format = TransferTargetFormat::Jpeg;
    options.prepare.xmp_portable  = true;
    bool xmp_include_existing_sidecar = false;
    bool xmp_include_existing_destination_embedded = false;
    XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence
        = XmpExistingSidecarPrecedence::SidecarWins;
    XmpExistingDestinationEmbeddedPrecedence
        xmp_existing_destination_embedded_precedence
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins;
    XmpExistingDestinationCarrierPrecedence
        xmp_existing_destination_carrier_precedence
        = XmpExistingDestinationCarrierPrecedence::SidecarWins;
    DngTargetMode dng_target_mode = DngTargetMode::MinimalFreshScaffold;
    XmpWritebackMode xmp_writeback_mode = XmpWritebackMode::EmbeddedOnly;
    XmpDestinationEmbeddedMode xmp_destination_embedded_mode
        = XmpDestinationEmbeddedMode::PreserveExisting;
    XmpDestinationSidecarMode xmp_destination_sidecar_mode
        = XmpDestinationSidecarMode::PreserveExisting;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!arg) {
            continue;
        }
        if (std::strcmp(arg, "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (std::strcmp(arg, "--version") == 0) {
            print_build_info_header();
            return 0;
        }
        if (std::strcmp(arg, "--no-build-info") == 0) {
            show_build_info = false;
            continue;
        }
        if ((std::strcmp(arg, "-i") == 0 || std::strcmp(arg, "--input") == 0)
            && i + 1 < argc) {
            input_paths.emplace_back(argv[i + 1]);
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--format") == 0 && i + 1 < argc) {
            const char* v = argv[i + 1];
            if (!v) {
                std::fprintf(stderr, "invalid --format value\n");
                return 2;
            }
            if (std::strcmp(v, "portable") == 0) {
                options.prepare.xmp_portable = true;
            } else if (std::strcmp(v, "lossless") == 0) {
                options.prepare.xmp_portable = false;
            } else {
                std::fprintf(
                    stderr,
                    "invalid --format value (expected portable|lossless)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--portable") == 0) {
            options.prepare.xmp_portable = true;
            continue;
        }
        if (std::strcmp(arg, "--lossless") == 0) {
            options.prepare.xmp_portable = false;
            continue;
        }
        if (std::strcmp(arg, "--xmp-include-existing") == 0) {
            options.prepare.xmp_include_existing = true;
            continue;
        }
        if (std::strcmp(arg, "--xmp-existing-namespace-policy") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(
                    stderr,
                    "missing value for --xmp-existing-namespace-policy\n");
                return 2;
            }
            XmpExistingNamespacePolicy policy
                = XmpExistingNamespacePolicy::KnownPortableOnly;
            if (!parse_xmp_existing_namespace_policy(argv[i + 1], &policy)) {
                std::fprintf(
                    stderr,
                    "invalid --xmp-existing-namespace-policy value "
                    "(expected known_portable_only|preserve_custom)\n");
                return 2;
            }
            options.prepare.xmp_existing_namespace_policy = policy;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--xmp-existing-standard-namespace-policy")
            == 0) {
            if (i + 1 >= argc) {
                std::fprintf(
                    stderr,
                    "missing value for "
                    "--xmp-existing-standard-namespace-policy\n");
                return 2;
            }
            XmpExistingStandardNamespacePolicy policy
                = XmpExistingStandardNamespacePolicy::PreserveAll;
            if (!parse_xmp_existing_standard_namespace_policy(argv[i + 1],
                                                              &policy)) {
                std::fprintf(
                    stderr,
                    "invalid --xmp-existing-standard-namespace-policy value "
                    "(expected preserve_all|canonicalize_managed)\n");
                return 2;
            }
            options.prepare.xmp_existing_standard_namespace_policy = policy;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--xmp-include-existing-sidecar") == 0) {
            xmp_include_existing_sidecar = true;
            continue;
        }
        if (std::strcmp(arg,
                        "--xmp-include-existing-destination-embedded")
            == 0) {
            xmp_include_existing_destination_embedded = true;
            continue;
        }
        if (std::strcmp(arg, "--xmp-existing-sidecar-precedence") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(
                    stderr,
                    "missing value for --xmp-existing-sidecar-precedence\n");
                return 2;
            }
            XmpExistingSidecarPrecedence precedence
                = XmpExistingSidecarPrecedence::SidecarWins;
            if (!parse_xmp_existing_sidecar_precedence(argv[i + 1],
                                                       &precedence)) {
                std::fprintf(
                    stderr,
                    "invalid --xmp-existing-sidecar-precedence value "
                    "(expected sidecar_wins|source_wins)\n");
                return 2;
            }
            xmp_existing_sidecar_precedence = precedence;
            i += 1;
            continue;
        }
        if (std::strcmp(
                arg,
                "--xmp-existing-destination-embedded-precedence")
            == 0) {
            if (i + 1 >= argc) {
                std::fprintf(
                    stderr,
                    "missing value for "
                    "--xmp-existing-destination-embedded-precedence\n");
                return 2;
            }
            XmpExistingDestinationEmbeddedPrecedence precedence
                = XmpExistingDestinationEmbeddedPrecedence::DestinationWins;
            if (!parse_xmp_existing_destination_embedded_precedence(
                    argv[i + 1], &precedence)) {
                std::fprintf(
                    stderr,
                    "invalid "
                    "--xmp-existing-destination-embedded-precedence value "
                    "(expected destination_wins|source_wins)\n");
                return 2;
            }
            xmp_existing_destination_embedded_precedence = precedence;
            i += 1;
            continue;
        }
        if (std::strcmp(arg,
                        "--xmp-existing-destination-carrier-precedence")
            == 0) {
            if (i + 1 >= argc) {
                std::fprintf(
                    stderr,
                    "missing value for "
                    "--xmp-existing-destination-carrier-precedence\n");
                return 2;
            }
            XmpExistingDestinationCarrierPrecedence precedence
                = XmpExistingDestinationCarrierPrecedence::SidecarWins;
            if (!parse_xmp_existing_destination_carrier_precedence(
                    argv[i + 1], &precedence)) {
                std::fprintf(
                    stderr,
                    "invalid "
                    "--xmp-existing-destination-carrier-precedence value "
                    "(expected sidecar_wins|embedded_wins)\n");
                return 2;
            }
            xmp_existing_destination_carrier_precedence = precedence;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--dng-target-mode") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for --dng-target-mode\n");
                return 2;
            }
            if (!parse_dng_target_mode(argv[i + 1], &dng_target_mode)) {
                std::fprintf(
                    stderr,
                    "invalid --dng-target-mode value "
                    "(expected existing_target|template_target|minimal_fresh_scaffold)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--xmp-no-exif-projection") == 0) {
            options.prepare.xmp_project_exif = false;
            continue;
        }
        if (std::strcmp(arg, "--xmp-no-iptc-projection") == 0) {
            options.prepare.xmp_project_iptc = false;
            continue;
        }
        if (std::strcmp(arg, "--xmp-conflict-policy") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for --xmp-conflict-policy\n");
                return 2;
            }
            XmpConflictPolicy policy = XmpConflictPolicy::CurrentBehavior;
            if (!parse_xmp_conflict_policy(argv[i + 1], &policy)) {
                std::fprintf(
                    stderr,
                    "invalid --xmp-conflict-policy value "
                    "(expected current|existing_wins|generated_wins)\n");
                return 2;
            }
            options.prepare.xmp_conflict_policy = policy;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--xmp-writeback") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for --xmp-writeback\n");
                return 2;
            }
            if (!parse_xmp_writeback_mode(argv[i + 1],
                                          &xmp_writeback_mode)) {
                std::fprintf(stderr,
                             "invalid --xmp-writeback value "
                             "(expected embedded|sidecar|embedded_and_sidecar)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--xmp-destination-embedded") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(
                    stderr,
                    "missing value for --xmp-destination-embedded\n");
                return 2;
            }
            if (!parse_xmp_destination_embedded_mode(
                    argv[i + 1], &xmp_destination_embedded_mode)) {
                std::fprintf(
                    stderr,
                    "invalid --xmp-destination-embedded value "
                    "(expected preserve_existing|strip_existing)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--xmp-destination-sidecar") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(
                    stderr,
                    "missing value for --xmp-destination-sidecar\n");
                return 2;
            }
            if (!parse_xmp_destination_sidecar_mode(
                    argv[i + 1], &xmp_destination_sidecar_mode)) {
                std::fprintf(
                    stderr,
                    "invalid --xmp-destination-sidecar value "
                    "(expected preserve_existing|strip_existing)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--xmp-exiftool-gpsdatetime-alias") == 0) {
            options.prepare.xmp_exiftool_gpsdatetime_alias = true;
            continue;
        }
        if (std::strcmp(arg, "--no-exif") == 0) {
            options.prepare.include_exif_app1 = false;
            continue;
        }
        if (std::strcmp(arg, "--no-xmp") == 0) {
            options.prepare.include_xmp_app1 = false;
            continue;
        }
        if (std::strcmp(arg, "--no-icc") == 0) {
            options.prepare.include_icc_app2 = false;
            continue;
        }
        if (std::strcmp(arg, "--no-iptc") == 0) {
            options.prepare.include_iptc_app13 = false;
            continue;
        }
        if (std::strcmp(arg, "--makernotes") == 0) {
            options.decode_makernote = true;
            continue;
        }
        if (std::strcmp(arg, "--makernote-policy") == 0 && i + 1 < argc) {
            TransferPolicyAction action = TransferPolicyAction::Keep;
            if (!parse_transfer_policy_action(argv[i + 1], &action)) {
                std::fprintf(
                    stderr,
                    "invalid --makernote-policy value (expected keep|drop|invalidate|rewrite)\n");
                return 2;
            }
            options.prepare.profile.makernote = action;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--jumbf-policy") == 0 && i + 1 < argc) {
            TransferPolicyAction action = TransferPolicyAction::Keep;
            if (!parse_transfer_policy_action(argv[i + 1], &action)) {
                std::fprintf(
                    stderr,
                    "invalid --jumbf-policy value (expected keep|drop|invalidate|rewrite)\n");
                return 2;
            }
            options.prepare.profile.jumbf = action;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-policy") == 0 && i + 1 < argc) {
            TransferPolicyAction action = TransferPolicyAction::Keep;
            if (!parse_transfer_policy_action(argv[i + 1], &action)) {
                std::fprintf(
                    stderr,
                    "invalid --c2pa-policy value (expected keep|drop|invalidate|rewrite)\n");
                return 2;
            }
            options.prepare.profile.c2pa = action;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--transfer-safety") == 0 && i + 1 < argc) {
            TransferSafetyMode safety = TransferSafetyMode::CompatibleFile;
            if (!parse_transfer_safety_mode(argv[i + 1], &safety)) {
                std::fprintf(
                    stderr,
                    "invalid --transfer-safety value "
                    "(expected compatible|rendered)\n");
                return 2;
            }
            options.prepare.profile.safety = safety;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--jpeg-jumbf") == 0 && i + 1 < argc) {
            jpeg_jumbf_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--replace-jpeg-jumbf") == 0) {
            replace_jpeg_jumbf = true;
            continue;
        }
        if (std::strcmp(arg, "--jpeg-c2pa-signed") == 0 && i + 1 < argc) {
            jpeg_c2pa_signed_path = argv[i + 1];
            c2pa_stage_requested  = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-manifest-output") == 0 && i + 1 < argc) {
            c2pa_manifest_output_path = argv[i + 1];
            c2pa_stage_requested      = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-certificate-chain") == 0 && i + 1 < argc) {
            c2pa_certificate_chain_path = argv[i + 1];
            c2pa_stage_requested        = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-key-ref") == 0 && i + 1 < argc) {
            c2pa_key_ref         = argv[i + 1];
            c2pa_stage_requested = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-signing-time") == 0 && i + 1 < argc) {
            c2pa_signing_time    = argv[i + 1];
            c2pa_stage_requested = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--dump-c2pa-binding") == 0 && i + 1 < argc) {
            c2pa_binding_output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--dump-c2pa-handoff") == 0 && i + 1 < argc) {
            c2pa_handoff_output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--dump-c2pa-signed-package") == 0
            && i + 1 < argc) {
            c2pa_signed_package_output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--load-c2pa-signed-package") == 0
            && i + 1 < argc) {
            c2pa_signed_package_input_path = argv[i + 1];
            c2pa_stage_requested           = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--dump-transfer-payload-batch") == 0
            && i + 1 < argc) {
            transfer_payload_batch_output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--load-transfer-payload-batch") == 0
            && i + 1 < argc) {
            transfer_payload_batch_input_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--dump-transfer-package-batch") == 0
            && i + 1 < argc) {
            transfer_package_batch_output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--dump-exr-attribute-batch") == 0
            && i + 1 < argc) {
            exr_attribute_batch_output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--update-dng-sdk-file") == 0
            && i + 1 < argc) {
            dng_sdk_update_target_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--load-transfer-package-batch") == 0
            && i + 1 < argc) {
            transfer_package_batch_input_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--dump-jxl-encoder-handoff") == 0
            && i + 1 < argc) {
            jxl_encoder_handoff_output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--load-jxl-encoder-handoff") == 0
            && i + 1 < argc) {
            jxl_encoder_handoff_input_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--load-transfer-artifact") == 0 && i + 1 < argc) {
            transfer_artifact_input_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--no-decompress") == 0) {
            options.decompress = false;
            continue;
        }
        if (std::strcmp(arg, "--unsafe-write-payloads") == 0
            || std::strcmp(arg, "--write-payloads") == 0) {
            write_payloads = true;
            continue;
        }
        if (std::strcmp(arg, "--out-dir") == 0 && i + 1 < argc) {
            out_dir = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--source-meta") == 0 && i + 1 < argc) {
            source_meta_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-jpeg") == 0 && i + 1 < argc) {
            target_jpeg_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-tiff") == 0 && i + 1 < argc) {
            target_tiff_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-dng") == 0 && i + 1 < argc) {
            target_dng_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-exr") == 0) {
            target_exr = true;
            continue;
        }
        if (std::strcmp(arg, "--target-png") == 0) {
            target_png = true;
            continue;
        }
        if (std::strcmp(arg, "--target-jp2") == 0) {
            target_jp2 = true;
            continue;
        }
        if (std::strcmp(arg, "--target-jxl") == 0) {
            target_jxl = true;
            continue;
        }
        if (std::strcmp(arg, "--target-webp") == 0) {
            target_webp = true;
            continue;
        }
        if (std::strcmp(arg, "--target-heif") == 0) {
            target_heif = true;
            continue;
        }
        if (std::strcmp(arg, "--target-avif") == 0) {
            target_avif = true;
            continue;
        }
        if (std::strcmp(arg, "--target-cr3") == 0) {
            target_cr3 = true;
            continue;
        }
        if (std::strcmp(arg, "--target-width") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for --target-width\n");
                return 2;
            }
            uint32_t v = 0U;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --target-width value\n");
                return 2;
            }
            options.prepare.target_image_spec.has_dimensions = true;
            options.prepare.target_image_spec.width          = v;
            target_width_set                                = true;
            target_image_spec_requested                     = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-height") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for --target-height\n");
                return 2;
            }
            uint32_t v = 0U;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --target-height value\n");
                return 2;
            }
            options.prepare.target_image_spec.has_dimensions = true;
            options.prepare.target_image_spec.height         = v;
            target_height_set                               = true;
            target_image_spec_requested                     = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-orientation") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr,
                             "missing value for --target-orientation\n");
                return 2;
            }
            uint16_t v = 0U;
            if (!parse_u16_arg(argv[i + 1], &v) || v < 1U || v > 8U) {
                std::fprintf(stderr, "invalid --target-orientation value\n");
                return 2;
            }
            options.prepare.target_image_spec.has_orientation = true;
            options.prepare.target_image_spec.orientation     = v;
            target_image_spec_requested = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-samples-per-pixel") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(
                    stderr,
                    "missing value for --target-samples-per-pixel\n");
                return 2;
            }
            uint16_t v = 0U;
            if (!parse_u16_arg(argv[i + 1], &v) || v == 0U
                || v > kTransferTargetImageSpecMaxSamples) {
                std::fprintf(
                    stderr,
                    "invalid --target-samples-per-pixel value\n");
                return 2;
            }
            options.prepare.target_image_spec.has_samples_per_pixel = true;
            options.prepare.target_image_spec.samples_per_pixel     = v;
            target_image_spec_requested = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-bits-per-sample") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(
                    stderr,
                    "missing value for --target-bits-per-sample\n");
                return 2;
            }
            if (!parse_u16_list_arg(
                    argv[i + 1],
                    &options.prepare.target_image_spec.bits_per_sample,
                    &options.prepare.target_image_spec.bits_per_sample_count)) {
                std::fprintf(stderr,
                             "invalid --target-bits-per-sample value\n");
                return 2;
            }
            target_image_spec_requested = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-sample-format") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr,
                             "missing value for --target-sample-format\n");
                return 2;
            }
            if (!parse_u16_list_arg(
                    argv[i + 1],
                    &options.prepare.target_image_spec.sample_format,
                    &options.prepare.target_image_spec.sample_format_count)) {
                std::fprintf(stderr, "invalid --target-sample-format value\n");
                return 2;
            }
            target_image_spec_requested = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-photometric") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr,
                             "missing value for --target-photometric\n");
                return 2;
            }
            uint16_t v = 0U;
            if (!parse_u16_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --target-photometric value\n");
                return 2;
            }
            options.prepare.target_image_spec.has_photometric_interpretation
                = true;
            options.prepare.target_image_spec.photometric_interpretation = v;
            target_image_spec_requested = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-planar-configuration") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(
                    stderr,
                    "missing value for --target-planar-configuration\n");
                return 2;
            }
            uint16_t v = 0U;
            if (!parse_u16_arg(argv[i + 1], &v) || (v != 1U && v != 2U)) {
                std::fprintf(
                    stderr,
                    "invalid --target-planar-configuration value\n");
                return 2;
            }
            options.prepare.target_image_spec.has_planar_configuration = true;
            options.prepare.target_image_spec.planar_configuration     = v;
            target_image_spec_requested = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-compression") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr,
                             "missing value for --target-compression\n");
                return 2;
            }
            uint16_t v = 0U;
            if (!parse_u16_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --target-compression value\n");
                return 2;
            }
            options.prepare.target_image_spec.has_compression = true;
            options.prepare.target_image_spec.compression     = v;
            target_image_spec_requested = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-exif-color-space") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(
                    stderr,
                    "missing value for --target-exif-color-space\n");
                return 2;
            }
            uint16_t v = 0U;
            if (!parse_u16_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr,
                             "invalid --target-exif-color-space value\n");
                return 2;
            }
            options.prepare.target_image_spec.has_exif_color_space = true;
            options.prepare.target_image_spec.exif_color_space     = v;
            target_image_spec_requested = true;
            i += 1;
            continue;
        }
        if ((std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0)
            && i + 1 < argc) {
            output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--force") == 0) {
            force = true;
            continue;
        }
        if (std::strcmp(arg, "--dry-run") == 0) {
            dry_run = true;
            continue;
        }
        if (std::strcmp(arg, "--mode") == 0 && i + 1 < argc) {
            JpegEditMode m = JpegEditMode::Auto;
            if (!parse_jpeg_edit_mode(argv[i + 1], &m)) {
                std::fprintf(
                    stderr,
                    "invalid --mode value (expected auto|in_place|metadata_rewrite)\n");
                return 2;
            }
            edit_plan_opts.mode = m;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--require-in-place") == 0) {
            edit_plan_opts.require_in_place = true;
            continue;
        }
        if (std::strcmp(arg, "--emit-repeat") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --emit-repeat value\n");
                return 2;
            }
            emit_repeat = v;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--time-patch") == 0 && i + 1 < argc) {
            PendingTimePatch p;
            std::string err;
            if (!parse_time_patch_arg(argv[i + 1], &p, &err)) {
                std::fprintf(stderr, "%s: %s\n", err.c_str(),
                             console_path(argv[i + 1]).c_str());
                return 2;
            }
            pending_time_patches.push_back(std::move(p));
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--time-patch-lax-width") == 0) {
            time_patch_opts.strict_width = false;
            continue;
        }
        if (std::strcmp(arg, "--time-patch-require-slot") == 0) {
            time_patch_opts.require_slot = true;
            continue;
        }
        if (std::strcmp(arg, "--time-patch-no-auto-nul") == 0) {
            time_patch_auto_nul = false;
            continue;
        }
        if (std::strcmp(arg, "--max-file-bytes") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (!parse_u64_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --max-file-bytes value\n");
                return 2;
            }
            options.policy.max_file_bytes = v;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-payload-bytes") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (!parse_u64_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-payload-bytes value\n");
                return 2;
            }
            options.policy.payload_limits.max_output_bytes = v;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-payload-parts") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-payload-parts value\n");
                return 2;
            }
            options.policy.payload_limits.max_parts = v;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-exif-ifds") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-exif-ifds value\n");
                return 2;
            }
            options.policy.exif_limits.max_ifds = v;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-exif-entries") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-exif-entries value\n");
                return 2;
            }
            options.policy.exif_limits.max_entries_per_ifd = v;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-exif-total") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-exif-total value\n");
                return 2;
            }
            options.policy.exif_limits.max_total_entries = v;
            i += 1;
            continue;
        }
        if (arg[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", arg);
            return 2;
        }
        input_paths.emplace_back(arg);
    }

    if (target_width_set != target_height_set) {
        std::fprintf(
            stderr,
            "--target-width and --target-height must be used together\n");
        return 2;
    }
    if (target_image_spec_requested
        && (!transfer_payload_batch_input_path.empty()
            || !transfer_package_batch_input_path.empty()
            || !jxl_encoder_handoff_input_path.empty()
            || !transfer_artifact_input_path.empty())) {
        std::fprintf(
            stderr,
            "target image spec options are transfer/edit options\n");
        return 2;
    }

    if (input_paths.empty()) {
        if (!source_meta_path.empty()) {
            input_paths.push_back(source_meta_path);
        } else if (!target_jpeg_path.empty()) {
            input_paths.push_back(target_jpeg_path);
        } else if (!target_tiff_path.empty()) {
            input_paths.push_back(target_tiff_path);
        } else if (!target_dng_path.empty()) {
            input_paths.push_back(target_dng_path);
        }
    }
    if (input_paths.empty() && transfer_payload_batch_input_path.empty()
        && transfer_package_batch_input_path.empty()
        && jxl_encoder_handoff_input_path.empty()
        && transfer_artifact_input_path.empty()) {
        usage(argv[0]);
        return 2;
    }
    if ((!source_meta_path.empty() || !target_jpeg_path.empty()
         || !target_tiff_path.empty() || !target_dng_path.empty())
        && input_paths.size() != 1U) {
        std::fprintf(
            stderr,
            "--source-meta/--target-jpeg/--target-tiff/--target-dng support exactly one job per run\n");
        return 2;
    }
    if (!output_path.empty() && input_paths.size() != 1U) {
        std::fprintf(stderr,
                     "--output supports exactly one input path per run\n");
        return 2;
    }
    if (!c2pa_binding_output_path.empty() && input_paths.size() != 1U) {
        std::fprintf(
            stderr,
            "--dump-c2pa-binding supports exactly one input path per run\n");
        return 2;
    }
    if ((!c2pa_handoff_output_path.empty()
         || !c2pa_signed_package_output_path.empty()
         || !c2pa_signed_package_input_path.empty())
        && input_paths.size() != 1U) {
        std::fprintf(
            stderr,
            "C2PA package options support exactly one input path per run\n");
        return 2;
    }
    if (!transfer_payload_batch_output_path.empty()
        && input_paths.size() != 1U) {
        std::fprintf(
            stderr,
            "--dump-transfer-payload-batch supports exactly one input path per run\n");
        return 2;
    }
    if (!transfer_package_batch_output_path.empty()
        && input_paths.size() != 1U) {
        std::fprintf(
            stderr,
            "--dump-transfer-package-batch supports exactly one input path per run\n");
        return 2;
    }
    if (!exr_attribute_batch_output_path.empty() && input_paths.size() != 1U) {
        std::fprintf(
            stderr,
            "--dump-exr-attribute-batch supports exactly one input path per run\n");
        return 2;
    }
    if (!dng_sdk_update_target_path.empty() && input_paths.size() != 1U) {
        std::fprintf(
            stderr,
            "--update-dng-sdk-file supports exactly one input path per run\n");
        return 2;
    }
    if (!jxl_encoder_handoff_output_path.empty() && input_paths.size() != 1U) {
        std::fprintf(
            stderr,
            "--dump-jxl-encoder-handoff supports exactly one input path per run\n");
        return 2;
    }
    const uint32_t target_count
        = static_cast<uint32_t>(!target_jpeg_path.empty())
          + static_cast<uint32_t>(!target_tiff_path.empty())
          + static_cast<uint32_t>(!target_dng_path.empty())
          + static_cast<uint32_t>(target_exr)
          + static_cast<uint32_t>(target_png)
          + static_cast<uint32_t>(target_jp2)
          + static_cast<uint32_t>(target_jxl)
          + static_cast<uint32_t>(target_webp)
          + static_cast<uint32_t>(target_heif)
          + static_cast<uint32_t>(target_avif)
          + static_cast<uint32_t>(target_cr3);
    const uint32_t inspect_input_count
        = static_cast<uint32_t>(!transfer_payload_batch_input_path.empty())
          + static_cast<uint32_t>(!transfer_package_batch_input_path.empty())
          + static_cast<uint32_t>(!jxl_encoder_handoff_input_path.empty())
          + static_cast<uint32_t>(!transfer_artifact_input_path.empty())
          + static_cast<uint32_t>(!exr_attribute_batch_output_path.empty())
          + static_cast<uint32_t>(!dng_sdk_update_target_path.empty());
    if (inspect_input_count > 1U) {
        std::fprintf(
            stderr,
            "--dump-exr-attribute-batch, --update-dng-sdk-file, --load-transfer-payload-batch, --load-transfer-package-batch, --load-jxl-encoder-handoff, and --load-transfer-artifact are mutually exclusive\n");
        return 2;
    }
    if (!transfer_payload_batch_input_path.empty()) {
        if (!input_paths.empty() || !source_meta_path.empty()
            || !target_jpeg_path.empty() || !target_tiff_path.empty()
            || !target_dng_path.empty()
            || target_count != 0U || !jpeg_jumbf_path.empty()
            || c2pa_stage_requested || !c2pa_binding_output_path.empty()
            || !c2pa_handoff_output_path.empty()
            || !c2pa_signed_package_output_path.empty()
            || !c2pa_signed_package_input_path.empty()
            || !transfer_payload_batch_output_path.empty()
            || !transfer_package_batch_output_path.empty()
            || !exr_attribute_batch_output_path.empty()
            || !dng_sdk_update_target_path.empty()
            || !output_path.empty() || dry_run || write_payloads) {
            std::fprintf(
                stderr,
                "--load-transfer-payload-batch is an inspect-only mode and is mutually exclusive with source transfer/edit options\n");
            return 2;
        }

        if (show_build_info) {
            print_build_info_header();
        }

        std::vector<std::byte> batch_bytes;
        if (!read_file_bytes(transfer_payload_batch_input_path, &batch_bytes)) {
            std::fprintf(
                stderr, "failed to read transfer payload batch: %s\n",
                console_path(transfer_payload_batch_input_path).c_str());
            return 1;
        }

        PreparedTransferPayloadBatch batch;
        const PreparedTransferPayloadIoResult batch_io
            = deserialize_prepared_transfer_payload_batch(
                std::span<const std::byte>(batch_bytes.data(),
                                           batch_bytes.size()),
                &batch);
        std::vector<PreparedTransferPayloadView> payload_views;
        EmitTransferResult inspect;
        if (batch_io.status == TransferStatus::Ok) {
            inspect = collect_prepared_transfer_payload_views(batch,
                                                              &payload_views);
        } else {
            inspect.status  = batch_io.status;
            inspect.code    = batch_io.code;
            inspect.errors  = batch_io.errors;
            inspect.message = batch_io.message;
        }

        std::printf("== transfer_payload_batch=%s\n",
                    console_path(transfer_payload_batch_input_path).c_str());
        std::printf(
            "  transfer_payload_batch: status=%s code=%s bytes=%llu payloads=%u target=%s\n",
            transfer_status_name(batch_io.status),
            emit_transfer_code_name(batch_io.code),
            static_cast<unsigned long long>(batch_io.bytes),
            static_cast<unsigned>(payload_views.size()),
            transfer_target_format_name(batch.target_format));
        if (!batch_io.message.empty()) {
            std::printf("  transfer_payload_batch_message=%s\n",
                        batch_io.message.c_str());
        }
        if (inspect.status != TransferStatus::Ok) {
            std::printf("  inspect: status=%s code=%s errors=%u\n",
                        transfer_status_name(inspect.status),
                        emit_transfer_code_name(inspect.code),
                        static_cast<unsigned>(inspect.errors));
            if (!inspect.message.empty()) {
                std::printf("  inspect_message=%s\n", inspect.message.c_str());
            }
            return 1;
        }
        for (size_t i = 0; i < payload_views.size(); ++i) {
            print_transfer_payload_view_summary(payload_views[i],
                                                static_cast<uint32_t>(i));
        }
        return 0;
    }
    if (!jxl_encoder_handoff_input_path.empty()) {
        if (!input_paths.empty() || !source_meta_path.empty()
            || !target_jpeg_path.empty() || !target_tiff_path.empty()
            || !target_dng_path.empty()
            || target_count != 0U || !jpeg_jumbf_path.empty()
            || c2pa_stage_requested || !c2pa_binding_output_path.empty()
            || !c2pa_handoff_output_path.empty()
            || !c2pa_signed_package_output_path.empty()
            || !c2pa_signed_package_input_path.empty()
            || !transfer_payload_batch_output_path.empty()
            || !transfer_package_batch_output_path.empty()
            || !exr_attribute_batch_output_path.empty()
            || !dng_sdk_update_target_path.empty()
            || !jxl_encoder_handoff_output_path.empty() || !output_path.empty()
            || dry_run || write_payloads) {
            std::fprintf(
                stderr,
                "--load-jxl-encoder-handoff is an inspect-only mode and is mutually exclusive with source transfer/edit options\n");
            return 2;
        }

        if (show_build_info) {
            print_build_info_header();
        }

        std::vector<std::byte> handoff_bytes;
        if (!read_file_bytes(jxl_encoder_handoff_input_path, &handoff_bytes)) {
            std::fprintf(stderr, "failed to read jxl encoder handoff: %s\n",
                         console_path(jxl_encoder_handoff_input_path).c_str());
            return 1;
        }

        PreparedJxlEncoderHandoff handoff;
        const PreparedJxlEncoderHandoffIoResult handoff_io
            = deserialize_prepared_jxl_encoder_handoff(
                std::span<const std::byte>(handoff_bytes.data(),
                                           handoff_bytes.size()),
                &handoff);
        std::printf("== jxl_encoder_handoff=%s\n",
                    console_path(jxl_encoder_handoff_input_path).c_str());
        std::printf(
            "  jxl_encoder_handoff: status=%s code=%s bytes=%llu box_count=%u box_payload_bytes=%llu\n",
            transfer_status_name(handoff_io.status),
            emit_transfer_code_name(handoff_io.code),
            static_cast<unsigned long long>(handoff_io.bytes),
            handoff.box_count,
            static_cast<unsigned long long>(handoff.box_payload_bytes));
        if (!handoff_io.message.empty()) {
            std::printf("  jxl_encoder_handoff_message=%s\n",
                        handoff_io.message.c_str());
        }
        if (handoff_io.status != TransferStatus::Ok) {
            return 1;
        }
        if (handoff.has_icc_profile) {
            std::printf("  jxl_icc_profile bytes=%llu\n",
                        static_cast<unsigned long long>(
                            handoff.icc_profile.size()));
        }
        return 0;
    }
    if (!transfer_artifact_input_path.empty()) {
        if (!input_paths.empty() || !source_meta_path.empty()
            || !target_jpeg_path.empty() || !target_tiff_path.empty()
            || !target_dng_path.empty()
            || target_count != 0U || !jpeg_jumbf_path.empty()
            || c2pa_stage_requested || !c2pa_binding_output_path.empty()
            || !c2pa_handoff_output_path.empty()
            || !c2pa_signed_package_output_path.empty()
            || !c2pa_signed_package_input_path.empty()
            || !transfer_payload_batch_output_path.empty()
            || !transfer_package_batch_output_path.empty()
            || !exr_attribute_batch_output_path.empty()
            || !dng_sdk_update_target_path.empty()
            || !jxl_encoder_handoff_output_path.empty() || !output_path.empty()
            || dry_run || write_payloads) {
            std::fprintf(
                stderr,
                "--load-transfer-artifact is an inspect-only mode and is mutually exclusive with source transfer/edit options\n");
            return 2;
        }

        if (show_build_info) {
            print_build_info_header();
        }

        std::vector<std::byte> artifact_bytes;
        if (!read_file_bytes(transfer_artifact_input_path, &artifact_bytes)) {
            std::fprintf(stderr, "failed to read transfer artifact: %s\n",
                         console_path(transfer_artifact_input_path).c_str());
            return 1;
        }

        PreparedTransferArtifactInfo info;
        const PreparedTransferArtifactIoResult artifact_io
            = inspect_prepared_transfer_artifact(
                std::span<const std::byte>(artifact_bytes.data(),
                                           artifact_bytes.size()),
                &info);
        std::printf("== transfer_artifact=%s\n",
                    console_path(transfer_artifact_input_path).c_str());
        std::printf(
            "  transfer_artifact: status=%s code=%s kind=%s bytes=%llu target=%s\n",
            transfer_status_name(artifact_io.status),
            emit_transfer_code_name(artifact_io.code),
            prepared_transfer_artifact_kind_name(info.kind).data(),
            static_cast<unsigned long long>(artifact_io.bytes),
            info.has_target_format
                ? transfer_target_format_name(info.target_format)
                : "-");
        if (!artifact_io.message.empty()) {
            std::printf("  transfer_artifact_message=%s\n",
                        artifact_io.message.c_str());
        }
        if (artifact_io.status != TransferStatus::Ok) {
            return 1;
        }
        switch (info.kind) {
        case PreparedTransferArtifactKind::TransferPayloadBatch:
            std::printf(
                "  transfer_payload_batch: payloads=%u payload_bytes=%llu\n",
                static_cast<unsigned>(info.entry_count),
                static_cast<unsigned long long>(info.payload_bytes));
            break;
        case PreparedTransferArtifactKind::TransferPackageBatch:
            std::printf(
                "  transfer_package_batch: chunks=%u payload_bytes=%llu\n",
                static_cast<unsigned>(info.entry_count),
                static_cast<unsigned long long>(info.payload_bytes));
            break;
        case PreparedTransferArtifactKind::C2paHandoffPackage:
            std::printf(
                "  c2pa_handoff: carrier=%s manifest_label=%s binding_bytes=%llu chunks=%u\n",
                info.carrier_route.empty() ? "-" : info.carrier_route.c_str(),
                info.manifest_label.empty() ? "-" : info.manifest_label.c_str(),
                static_cast<unsigned long long>(info.binding_bytes),
                static_cast<unsigned>(info.entry_count));
            break;
        case PreparedTransferArtifactKind::C2paSignedPackage:
            std::printf(
                "  c2pa_signed_package: carrier=%s manifest_label=%s signed_payload_bytes=%llu chunks=%u\n",
                info.carrier_route.empty() ? "-" : info.carrier_route.c_str(),
                info.manifest_label.empty() ? "-" : info.manifest_label.c_str(),
                static_cast<unsigned long long>(info.signed_payload_bytes),
                static_cast<unsigned>(info.entry_count));
            break;
        case PreparedTransferArtifactKind::JxlEncoderHandoff:
            std::printf(
                "  jxl_encoder_handoff: box_count=%u box_payload_bytes=%llu\n",
                static_cast<unsigned>(info.entry_count),
                static_cast<unsigned long long>(info.box_payload_bytes));
            if (info.has_icc_profile) {
                std::printf("  jxl_icc_profile bytes=%llu\n",
                            static_cast<unsigned long long>(
                                info.icc_profile_bytes));
            }
            break;
        case PreparedTransferArtifactKind::Unknown: break;
        }
        return 0;
    }
    if (!transfer_package_batch_input_path.empty()) {
        if (!input_paths.empty() || !source_meta_path.empty()
            || !target_jpeg_path.empty() || !target_tiff_path.empty()
            || !target_dng_path.empty()
            || target_count != 0U || !jpeg_jumbf_path.empty()
            || c2pa_stage_requested || !c2pa_binding_output_path.empty()
            || !c2pa_handoff_output_path.empty()
            || !c2pa_signed_package_output_path.empty()
            || !c2pa_signed_package_input_path.empty()
            || !transfer_payload_batch_output_path.empty()
            || !transfer_package_batch_output_path.empty()
            || !exr_attribute_batch_output_path.empty()
            || !dng_sdk_update_target_path.empty()
            || !output_path.empty() || dry_run || write_payloads) {
            std::fprintf(
                stderr,
                "--load-transfer-package-batch is an inspect-only mode and is mutually exclusive with source transfer/edit options\n");
            return 2;
        }

        if (show_build_info) {
            print_build_info_header();
        }

        std::vector<std::byte> batch_bytes;
        if (!read_file_bytes(transfer_package_batch_input_path, &batch_bytes)) {
            std::fprintf(
                stderr, "failed to read transfer package batch: %s\n",
                console_path(transfer_package_batch_input_path).c_str());
            return 1;
        }

        PreparedTransferPackageBatch batch;
        const PreparedTransferPackageIoResult batch_io
            = deserialize_prepared_transfer_package_batch(
                std::span<const std::byte>(batch_bytes.data(),
                                           batch_bytes.size()),
                &batch);
        std::vector<PreparedTransferPackageView> chunk_views;
        EmitTransferResult inspect;
        if (batch_io.status == TransferStatus::Ok) {
            inspect = collect_prepared_transfer_package_views(batch,
                                                              &chunk_views);
        } else {
            inspect.status  = batch_io.status;
            inspect.code    = batch_io.code;
            inspect.errors  = batch_io.errors;
            inspect.message = batch_io.message;
        }

        std::printf("== transfer_package_batch=%s\n",
                    console_path(transfer_package_batch_input_path).c_str());
        std::printf(
            "  transfer_package_batch: status=%s code=%s bytes=%llu chunks=%u target=%s\n",
            transfer_status_name(batch_io.status),
            emit_transfer_code_name(batch_io.code),
            static_cast<unsigned long long>(batch_io.bytes),
            static_cast<unsigned>(chunk_views.size()),
            transfer_target_format_name(batch.target_format));
        if (!batch_io.message.empty()) {
            std::printf("  transfer_package_batch_message=%s\n",
                        batch_io.message.c_str());
        }
        if (inspect.status != TransferStatus::Ok) {
            std::printf("  inspect: status=%s code=%s errors=%u\n",
                        transfer_status_name(inspect.status),
                        emit_transfer_code_name(inspect.code),
                        static_cast<unsigned>(inspect.errors));
            if (!inspect.message.empty()) {
                std::printf("  inspect_message=%s\n", inspect.message.c_str());
            }
            return 1;
        }
        for (size_t i = 0; i < chunk_views.size(); ++i) {
            print_transfer_package_view_summary(chunk_views[i],
                                                static_cast<uint32_t>(i));
        }
        return 0;
    }
    if (!exr_attribute_batch_output_path.empty()) {
        if (!source_meta_path.empty() || target_count != 0U
            || !jpeg_jumbf_path.empty() || c2pa_stage_requested
            || !c2pa_binding_output_path.empty()
            || !c2pa_handoff_output_path.empty()
            || !c2pa_signed_package_output_path.empty()
            || !c2pa_signed_package_input_path.empty()
            || !transfer_payload_batch_output_path.empty()
            || !transfer_payload_batch_input_path.empty()
            || !transfer_package_batch_output_path.empty()
            || !transfer_package_batch_input_path.empty()
            || !dng_sdk_update_target_path.empty()
            || !jxl_encoder_handoff_output_path.empty()
            || !jxl_encoder_handoff_input_path.empty()
            || !transfer_artifact_input_path.empty() || !output_path.empty()
            || !out_dir.empty() || dry_run || write_payloads
            || !pending_time_patches.empty() || emit_repeat != 1U
            || edit_plan_opts.mode != JpegEditMode::Auto
            || edit_plan_opts.require_in_place
            || xmp_include_existing_destination_embedded
            || xmp_writeback_mode != XmpWritebackMode::EmbeddedOnly
            || xmp_destination_embedded_mode
                   != XmpDestinationEmbeddedMode::PreserveExisting
            || xmp_destination_sidecar_mode
                   != XmpDestinationSidecarMode::PreserveExisting) {
            std::fprintf(
                stderr,
                "--dump-exr-attribute-batch is an inspect-only mode and is mutually exclusive with transfer/edit options\n");
            return 2;
        }

        if (show_build_info) {
            print_build_info_header();
        }

        if (xmp_include_existing_sidecar) {
            options.xmp_existing_sidecar_mode
                = XmpExistingSidecarMode::MergeIfPresent;
            options.xmp_existing_sidecar_precedence
                = xmp_existing_sidecar_precedence;
            options.xmp_existing_sidecar_base_path = input_paths[0];
        } else {
            options.xmp_existing_sidecar_mode = XmpExistingSidecarMode::Ignore;
            options.xmp_existing_sidecar_precedence
                = XmpExistingSidecarPrecedence::SidecarWins;
            options.xmp_existing_sidecar_base_path.clear();
        }
        if (xmp_include_existing_destination_embedded) {
            std::fprintf(
                stderr,
                "--dump-exr-attribute-batch is an inspect-only mode and is mutually exclusive with transfer/edit options\n");
            return 2;
        }

        BuildExrAttributeBatchFileOptions exr_options;
        exr_options.prepare = options;

        ExrAdapterBatch batch;
        const BuildExrAttributeBatchFileResult result
            = build_exr_attribute_batch_from_file(input_paths[0].c_str(),
                                                  &batch, exr_options);

        std::string dump_text;
        append_exr_attribute_batch_dump(input_paths[0].c_str(), result, batch,
                                        &dump_text);

        if (!force && file_exists(exr_attribute_batch_output_path)) {
            std::fprintf(stderr, "dump path already exists (use --force): %s\n",
                         console_path(exr_attribute_batch_output_path).c_str());
            return 1;
        }
        if (!write_file_bytes(
                exr_attribute_batch_output_path,
                std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(dump_text.data()),
                    dump_text.size()))) {
            std::fprintf(stderr,
                         "failed to write exr attribute batch dump: %s\n",
                         console_path(exr_attribute_batch_output_path).c_str());
            return 1;
        }

        std::printf(
            "exr_attribute_batch_dump: status=%s path=%s entries=%u exported=%u errors=%u\n",
            exr_adapter_status_name(result.adapter.status),
            console_path(exr_attribute_batch_output_path).c_str(),
            static_cast<unsigned>(batch.attributes.size()),
            static_cast<unsigned>(result.adapter.exported),
            static_cast<unsigned>(result.adapter.errors));

        if (result.prepared.file_status != TransferFileStatus::Ok) {
            return 1;
        }
        if (result.prepared.prepare.status != TransferStatus::Ok) {
            return 1;
        }
        return result.adapter.status == ExrAdapterStatus::Ok ? 0 : 1;
    }
    if (!dng_sdk_update_target_path.empty()) {
        if (target_count != 0U || !jpeg_jumbf_path.empty()
            || c2pa_stage_requested || !c2pa_binding_output_path.empty()
            || !c2pa_handoff_output_path.empty()
            || !c2pa_signed_package_output_path.empty()
            || !c2pa_signed_package_input_path.empty()
            || !transfer_payload_batch_output_path.empty()
            || !transfer_payload_batch_input_path.empty()
            || !transfer_package_batch_output_path.empty()
            || !transfer_package_batch_input_path.empty()
            || !exr_attribute_batch_output_path.empty()
            || !jxl_encoder_handoff_output_path.empty()
            || !jxl_encoder_handoff_input_path.empty()
            || !transfer_artifact_input_path.empty() || !output_path.empty()
            || !out_dir.empty() || dry_run || write_payloads
            || !pending_time_patches.empty() || emit_repeat != 1U
            || edit_plan_opts.mode != JpegEditMode::Auto
            || edit_plan_opts.require_in_place
            || xmp_include_existing_destination_embedded
            || xmp_writeback_mode != XmpWritebackMode::EmbeddedOnly
            || xmp_destination_embedded_mode
                   != XmpDestinationEmbeddedMode::PreserveExisting
            || xmp_destination_sidecar_mode
                   != XmpDestinationSidecarMode::PreserveExisting) {
            std::fprintf(
                stderr,
                "--update-dng-sdk-file is a thin file-helper mode and is mutually exclusive with transfer/edit options\n");
            return 2;
        }

        if (show_build_info) {
            print_build_info_header();
        }

        if (xmp_include_existing_sidecar) {
            options.xmp_existing_sidecar_mode
                = XmpExistingSidecarMode::MergeIfPresent;
            options.xmp_existing_sidecar_precedence
                = xmp_existing_sidecar_precedence;
            options.xmp_existing_sidecar_base_path = input_paths[0];
        } else {
            options.xmp_existing_sidecar_mode = XmpExistingSidecarMode::Ignore;
            options.xmp_existing_sidecar_precedence
                = XmpExistingSidecarPrecedence::SidecarWins;
            options.xmp_existing_sidecar_base_path.clear();
        }

        ApplyDngSdkMetadataFileOptions dng_options;
        dng_options.prepare = options;

        const ApplyDngSdkMetadataFileResult result
            = update_dng_sdk_file_from_file(input_paths[0].c_str(),
                                            dng_sdk_update_target_path.c_str(),
                                            dng_options);

        std::printf(
            "dng_sdk_file_update: status=%s source=%s target=%s available=%s file_status=%s prepare=%s applied_blocks=%u skipped_blocks=%u updated_stream=%s\n",
            dng_sdk_adapter_status_name(result.adapter.status),
            console_path(input_paths[0]).c_str(),
            console_path(dng_sdk_update_target_path).c_str(),
            dng_sdk_adapter_available() ? "true" : "false",
            transfer_file_status_name(result.prepared.file_status),
            transfer_status_name(result.prepared.prepare.status),
            static_cast<unsigned>(result.adapter.applied_blocks),
            static_cast<unsigned>(result.adapter.skipped_blocks),
            result.adapter.updated_stream ? "true" : "false");
        std::printf(
            "  dng_sdk_apply: exif=%s xmp=%s iptc=%s synchronized=%s cleaned_for_update=%s failed_kind=%s\n",
            result.adapter.exif_applied ? "true" : "false",
            result.adapter.xmp_applied ? "true" : "false",
            result.adapter.iptc_applied ? "true" : "false",
            result.adapter.synchronized_metadata ? "true" : "false",
            result.adapter.cleaned_for_update ? "true" : "false",
            transfer_kind_name(result.adapter.failed_kind));
        if (!result.adapter.message.empty()) {
            std::printf("  dng_sdk_message=%s\n",
                        result.adapter.message.c_str());
        }

        if (result.prepared.file_status != TransferFileStatus::Ok) {
            return 1;
        }
        if (result.prepared.prepare.status != TransferStatus::Ok) {
            return 1;
        }
        return result.adapter.status == DngSdkAdapterStatus::Ok ? 0 : 1;
    }
    if (target_count > 1U) {
        std::fprintf(
            stderr,
            "--target-jpeg, --target-tiff, --target-dng, --target-exr, --target-png, --target-jp2, --target-jxl, --target-webp, --target-heif, --target-avif, and --target-cr3 are mutually exclusive\n");
        return 2;
    }
    if (target_exr) {
        options.prepare.target_format = TransferTargetFormat::Exr;
    } else if (target_png) {
        options.prepare.target_format = TransferTargetFormat::Png;
    } else if (target_jp2) {
        options.prepare.target_format = TransferTargetFormat::Jp2;
    } else if (target_jxl) {
        options.prepare.target_format = TransferTargetFormat::Jxl;
    } else if (target_webp) {
        options.prepare.target_format = TransferTargetFormat::Webp;
    } else if (target_heif) {
        options.prepare.target_format = TransferTargetFormat::Heif;
    } else if (target_avif) {
        options.prepare.target_format = TransferTargetFormat::Avif;
    } else if (target_cr3) {
        options.prepare.target_format = TransferTargetFormat::Cr3;
    } else if (!target_tiff_path.empty()) {
        options.prepare.target_format = TransferTargetFormat::Tiff;
    } else if (!target_dng_path.empty()) {
        options.prepare.target_format = TransferTargetFormat::Dng;
        options.prepare.dng_target_mode = dng_target_mode;
    } else {
        options.prepare.target_format = TransferTargetFormat::Jpeg;
    }
    options.prepare.dng_target_mode = dng_target_mode;
    if (!jpeg_jumbf_path.empty()
        && options.prepare.target_format != TransferTargetFormat::Jpeg) {
        std::fprintf(stderr,
                     "--jpeg-jumbf is only supported for JPEG targets\n");
        return 2;
    }
    if (target_exr && !output_path.empty()) {
        std::fprintf(stderr, "--output is not supported for --target-exr\n");
        return 2;
    }
    if (xmp_writeback_mode != XmpWritebackMode::EmbeddedOnly
        && output_path.empty()) {
        std::fprintf(stderr,
                     "--xmp-writeback sidecar or embedded_and_sidecar "
                     "requires --output\n");
        return 2;
    }
    if (xmp_destination_embedded_mode == XmpDestinationEmbeddedMode::StripExisting
        && (xmp_writeback_mode != XmpWritebackMode::SidecarOnly
            || (options.prepare.target_format != TransferTargetFormat::Jpeg
                && !target_format_is_tiff_family(
                    options.prepare.target_format)
                && options.prepare.target_format != TransferTargetFormat::Png
                && options.prepare.target_format != TransferTargetFormat::Webp
                && options.prepare.target_format != TransferTargetFormat::Jp2
                && options.prepare.target_format != TransferTargetFormat::Jxl))) {
        std::fprintf(
            stderr,
            "--xmp-destination-embedded strip_existing is currently "
            "supported only for --target-jpeg, --target-tiff, --target-dng, --target-png, "
            "--target-webp, --target-jp2, and --target-jxl with "
            "--xmp-writeback sidecar\n");
        return 2;
    }
    if (xmp_destination_sidecar_mode == XmpDestinationSidecarMode::StripExisting
        && xmp_writeback_mode != XmpWritebackMode::EmbeddedOnly) {
        std::fprintf(
            stderr,
            "--xmp-destination-sidecar strip_existing is currently "
            "supported only with --xmp-writeback embedded\n");
        return 2;
    }
    const bool c2pa_wrapper_target_ok
        = options.prepare.target_format == TransferTargetFormat::Jpeg
          || options.prepare.target_format == TransferTargetFormat::Jxl
          || options.prepare.target_format == TransferTargetFormat::Heif
          || options.prepare.target_format == TransferTargetFormat::Avif
          || options.prepare.target_format == TransferTargetFormat::Cr3;
    if (c2pa_stage_requested && !c2pa_wrapper_target_ok) {
        std::fprintf(
            stderr,
            "signed c2pa staging is only supported for JPEG, JXL, and BMFF targets\n");
        return 2;
    }
    if (!c2pa_binding_output_path.empty() && !c2pa_wrapper_target_ok) {
        std::fprintf(
            stderr,
            "--dump-c2pa-binding is only supported for JPEG, JXL, and BMFF targets\n");
        return 2;
    }
    if ((!c2pa_handoff_output_path.empty()
         || !c2pa_signed_package_output_path.empty()
         || !c2pa_signed_package_input_path.empty())
        && !c2pa_wrapper_target_ok) {
        std::fprintf(
            stderr,
            "C2PA package options are only supported for JPEG, JXL, and BMFF targets\n");
        return 2;
    }
    if (!c2pa_signed_package_input_path.empty()
        && (!jpeg_c2pa_signed_path.empty() || !c2pa_manifest_output_path.empty()
            || !c2pa_certificate_chain_path.empty() || !c2pa_key_ref.empty()
            || !c2pa_signing_time.empty())) {
        std::fprintf(
            stderr,
            "--load-c2pa-signed-package is mutually exclusive with individual signed C2PA inputs\n");
        return 2;
    }
    if (c2pa_stage_requested) {
        options.prepare.profile.c2pa = TransferPolicyAction::Rewrite;
    }
    if (target_format_is_tiff_family(options.prepare.target_format)
        && (edit_plan_opts.mode != JpegEditMode::Auto
            || edit_plan_opts.require_in_place)) {
        std::fprintf(
            stderr,
            "--mode/--require-in-place are only valid for JPEG targets\n");
        return 2;
    }
    if (show_build_info) {
        print_build_info_header();
    }

    bool any_failed = false;
    for (size_t i = 0; i < input_paths.size(); ++i) {
        const std::string& job_path   = input_paths[i];
        const std::string source_path = source_meta_path.empty()
                                            ? job_path
                                            : source_meta_path;
        std::string target_path;
        if (!target_jpeg_path.empty()) {
            target_path = target_jpeg_path;
        } else if (!target_tiff_path.empty()) {
            target_path = target_tiff_path;
        } else if (!target_dng_path.empty()) {
            target_path = target_dng_path;
        } else {
            target_path = job_path;
        }

        const std::string display_source = console_path(source_path);
        const std::string display_target = console_path(target_path);
        const std::string display_output = console_path(output_path);

        std::printf("== source=%s\n", display_source.c_str());
        if (target_path != source_path) {
            std::printf("   target=%s\n", display_target.c_str());
        }

        const bool need_jpeg_edit
            = options.prepare.target_format == TransferTargetFormat::Jpeg
              && (dry_run || !output_path.empty()
                  || edit_plan_opts.mode != JpegEditMode::Auto
                  || edit_plan_opts.require_in_place
                  || !target_jpeg_path.empty());
        const bool need_tiff_edit
            = target_format_is_tiff_family(options.prepare.target_format)
              && (dry_run || !output_path.empty() || !target_tiff_path.empty()
                  || !target_dng_path.empty());
        const bool need_webp_edit = options.prepare.target_format
                                        == TransferTargetFormat::Webp
                                    && (dry_run || !output_path.empty());
        const bool need_png_edit = options.prepare.target_format
                                       == TransferTargetFormat::Png
                                   && (dry_run || !output_path.empty());
        const bool need_jp2_edit = options.prepare.target_format
                                       == TransferTargetFormat::Jp2
                                   && (dry_run || !output_path.empty());
        const bool need_jxl_edit = options.prepare.target_format
                                       == TransferTargetFormat::Jxl
                                   && (dry_run || !output_path.empty());
        const bool need_bmff_edit = target_format_is_bmff(
                                        options.prepare.target_format)
                                    && (dry_run || !output_path.empty());

        const bool output_exists = !output_path.empty()
                                   && file_exists(output_path);
        if (!output_path.empty()
            && (paths_refer_to_same_file(output_path, source_path)
                || paths_refer_to_same_file(output_path, target_path))) {
            std::fprintf(stderr,
                         "  output aliases an input file; refusing transfer\n");
            any_failed = true;
            continue;
        }
        FileByteWriter output_writer(output_path, force);
        const bool use_output_writer = !output_path.empty() && !dry_run
                                       && (!output_exists || force);
        if (xmp_include_existing_sidecar) {
            options.xmp_existing_sidecar_mode
                = XmpExistingSidecarMode::MergeIfPresent;
            options.xmp_existing_sidecar_precedence
                = xmp_existing_sidecar_precedence;
            options.xmp_existing_destination_carrier_precedence
                = xmp_existing_destination_carrier_precedence;
            if (!output_path.empty()) {
                options.xmp_existing_sidecar_base_path = output_path;
            } else if (!target_path.empty()) {
                options.xmp_existing_sidecar_base_path = target_path;
            } else {
                options.xmp_existing_sidecar_base_path = source_path;
            }
        } else {
            options.xmp_existing_sidecar_mode = XmpExistingSidecarMode::Ignore;
            options.xmp_existing_sidecar_precedence
                = XmpExistingSidecarPrecedence::SidecarWins;
            options.xmp_existing_destination_carrier_precedence
                = XmpExistingDestinationCarrierPrecedence::SidecarWins;
            options.xmp_existing_sidecar_base_path.clear();
        }
        if (xmp_include_existing_destination_embedded) {
            const bool destination_embedded_edit_context
                = need_jpeg_edit || need_tiff_edit || need_webp_edit
                  || need_png_edit || need_jp2_edit || need_jxl_edit
                  || need_bmff_edit;
            options.xmp_existing_destination_embedded_mode
                = XmpExistingDestinationEmbeddedMode::MergeIfPresent;
            options.xmp_existing_destination_embedded_precedence
                = xmp_existing_destination_embedded_precedence;
            options.xmp_existing_destination_carrier_precedence
                = xmp_existing_destination_carrier_precedence;
            if (destination_embedded_edit_context) {
                options.xmp_existing_destination_embedded_path = target_path;
            } else {
                options.xmp_existing_destination_embedded_path.clear();
            }
        } else {
            options.xmp_existing_destination_embedded_mode
                = XmpExistingDestinationEmbeddedMode::Ignore;
            options.xmp_existing_destination_embedded_precedence
                = XmpExistingDestinationEmbeddedPrecedence::DestinationWins;
            options.xmp_existing_destination_carrier_precedence
                = XmpExistingDestinationCarrierPrecedence::SidecarWins;
            options.xmp_existing_destination_embedded_path.clear();
        }
        PrepareTransferFileResult prepared
            = prepare_metadata_for_target_file(source_path.c_str(), options);
        EmitTransferResult append_jumbf_result;
        EmitTransferResult c2pa_stage_result;
        ValidatePreparedC2paSignResult c2pa_stage_validation;
        PreparedTransferC2paHandoffPackage c2pa_handoff;
        PreparedTransferC2paSignedPackage c2pa_signed_package;
        PreparedTransferC2paPackageIoResult c2pa_handoff_io;
        PreparedTransferC2paPackageIoResult c2pa_signed_package_io;
        PreparedTransferPayloadBatch transfer_payload_batch;
        PreparedTransferPayloadIoResult transfer_payload_batch_io;
        PreparedTransferPackageBatch transfer_package_batch;
        PreparedTransferPackageIoResult transfer_package_batch_io;
        PreparedJxlEncoderHandoff jxl_encoder_handoff;
        PreparedJxlEncoderHandoffIoResult jxl_encoder_handoff_io;
        bool did_append_jumbf                = false;
        bool did_stage_c2pa                  = false;
        bool did_dump_c2pa_binding           = false;
        bool did_dump_c2pa_handoff           = false;
        bool did_dump_c2pa_signed_package    = false;
        bool did_dump_transfer_payload_batch = false;
        bool did_dump_transfer_package_batch = false;
        bool did_dump_jxl_encoder_handoff    = false;
        if (prepared.file_status == TransferFileStatus::Ok
            && prepared.prepare.status == TransferStatus::Ok
            && !jpeg_jumbf_path.empty()) {
            std::vector<std::byte> jpeg_jumbf_bytes;
            if (!read_file_bytes(jpeg_jumbf_path, &jpeg_jumbf_bytes)) {
                std::fprintf(stderr, "  append_jumbf: read_failed: %s\n",
                             console_path(jpeg_jumbf_path).c_str());
                any_failed = true;
                continue;
            }
            AppendPreparedJpegJumbfOptions append_options;
            append_options.replace_existing = replace_jpeg_jumbf;
            append_jumbf_result             = append_prepared_bundle_jpeg_jumbf(
                &prepared.bundle,
                std::span<const std::byte>(jpeg_jumbf_bytes.data(),
                                                       jpeg_jumbf_bytes.size()),
                append_options);
            did_append_jumbf = true;
        }
        if (prepared.file_status == TransferFileStatus::Ok
            && c2pa_stage_requested) {
            if (!c2pa_signed_package_input_path.empty()) {
                std::vector<std::byte> signed_package_bytes;
                if (!read_file_bytes(c2pa_signed_package_input_path,
                                     &signed_package_bytes)) {
                    std::fprintf(
                        stderr, "  c2pa_stage: read_failed: %s\n",
                        console_path(c2pa_signed_package_input_path).c_str());
                    any_failed = true;
                    continue;
                }
                c2pa_signed_package_io
                    = deserialize_prepared_c2pa_signed_package(
                        std::span<const std::byte>(signed_package_bytes.data(),
                                                   signed_package_bytes.size()),
                        &c2pa_signed_package);
                if (c2pa_signed_package_io.status != TransferStatus::Ok) {
                    c2pa_stage_result.status  = c2pa_signed_package_io.status;
                    c2pa_stage_result.code    = c2pa_signed_package_io.code;
                    c2pa_stage_result.errors  = c2pa_signed_package_io.errors;
                    c2pa_stage_result.message = c2pa_signed_package_io.message;
                } else {
                    c2pa_stage_validation
                        = validate_prepared_c2pa_signed_package(
                            prepared.bundle, c2pa_signed_package);
                    c2pa_stage_result = apply_prepared_c2pa_signed_package(
                        &prepared.bundle, c2pa_signed_package);
                }
            } else {
                std::vector<std::byte> signed_bytes;
                std::vector<std::byte> manifest_bytes;
                std::vector<std::byte> certificate_bytes;
                if (!jpeg_c2pa_signed_path.empty()
                    && !read_file_bytes(jpeg_c2pa_signed_path, &signed_bytes)) {
                    std::fprintf(stderr, "  c2pa_stage: read_failed: %s\n",
                                 console_path(jpeg_c2pa_signed_path).c_str());
                    any_failed = true;
                    continue;
                }
                if (!c2pa_manifest_output_path.empty()
                    && !read_file_bytes(c2pa_manifest_output_path,
                                        &manifest_bytes)) {
                    std::fprintf(
                        stderr, "  c2pa_stage: read_failed: %s\n",
                        console_path(c2pa_manifest_output_path).c_str());
                    any_failed = true;
                    continue;
                }
                if (!c2pa_certificate_chain_path.empty()
                    && !read_file_bytes(c2pa_certificate_chain_path,
                                        &certificate_bytes)) {
                    std::fprintf(
                        stderr, "  c2pa_stage: read_failed: %s\n",
                        console_path(c2pa_certificate_chain_path).c_str());
                    any_failed = true;
                    continue;
                }

                PreparedTransferC2paSignerInput signer_input;
                signer_input.signing_time            = c2pa_signing_time;
                signer_input.certificate_chain_bytes = std::move(
                    certificate_bytes);
                signer_input.private_key_reference   = c2pa_key_ref;
                signer_input.manifest_builder_output = std::move(
                    manifest_bytes);
                signer_input.signed_c2pa_logical_payload = std::move(
                    signed_bytes);

                const TransferStatus signed_package_status
                    = build_prepared_c2pa_signed_package(prepared.bundle,
                                                         signer_input,
                                                         &c2pa_signed_package);
                if (signed_package_status != TransferStatus::Ok) {
                    c2pa_stage_result.status = signed_package_status;
                    c2pa_stage_result.code = EmitTransferCode::InvalidArgument;
                    c2pa_stage_result.errors = 1U;
                    c2pa_stage_result.message
                        = c2pa_signed_package.request.message.empty()
                              ? "failed to build c2pa sign request"
                              : c2pa_signed_package.request.message;
                } else {
                    c2pa_stage_validation
                        = validate_prepared_c2pa_signed_package(
                            prepared.bundle, c2pa_signed_package);
                    c2pa_stage_result = apply_prepared_c2pa_signed_package(
                        &prepared.bundle, c2pa_signed_package);
                }
            }
            did_stage_c2pa = true;
        }

        const bool xmp_sidecar_requested
            = xmp_writeback_mode != XmpWritebackMode::EmbeddedOnly;
        TransferStatus xmp_sidecar_status = TransferStatus::Unsupported;
        std::string xmp_sidecar_message;
        std::string xmp_sidecar_path;
        std::vector<std::byte> xmp_sidecar_output;
        bool xmp_sidecar_cleanup_requested = false;
        TransferStatus xmp_sidecar_cleanup_status = TransferStatus::Unsupported;
        std::string xmp_sidecar_cleanup_message;
        std::string xmp_sidecar_cleanup_path;
        if (prepared.file_status == TransferFileStatus::Ok
            && xmp_sidecar_requested) {
            if (output_path.empty()) {
                xmp_sidecar_message
                    = "xmp sidecar writeback requires --output";
            } else {
                std::string sidecar_a;
                std::string sidecar_b;
                xmp_sidecar_candidates(output_path.c_str(), &sidecar_a,
                                       &sidecar_b);
                xmp_sidecar_path = sidecar_a;
                if (!prepared.bundle.generated_xmp_sidecar.empty()) {
                    xmp_sidecar_output = prepared.bundle.generated_xmp_sidecar;
                    xmp_sidecar_status = TransferStatus::Ok;
                    if (xmp_writeback_mode
                        == XmpWritebackMode::SidecarOnly) {
                        (void)remove_prepared_blocks_by_kind(
                            &prepared.bundle, TransferBlockKind::Xmp);
                    } else {
                        xmp_sidecar_message
                            = "prepared xmp sidecar bytes will be written "
                              "alongside embedded xmp carriers";
                    }
                } else {
                    xmp_sidecar_status  = TransferStatus::Ok;
                    xmp_sidecar_message = "prepared bundle did not produce xmp "
                                          "sidecar bytes";
                }
            }
        }
        if (prepared.file_status == TransferFileStatus::Ok
            && xmp_destination_sidecar_mode
                   == XmpDestinationSidecarMode::StripExisting) {
            std::string sidecar_a;
            std::string sidecar_b;
            xmp_sidecar_candidates(output_path.c_str(), &sidecar_a, &sidecar_b);
            xmp_sidecar_cleanup_path = sidecar_a;
            xmp_sidecar_cleanup_status = TransferStatus::Ok;
            if (file_exists(sidecar_a)) {
                xmp_sidecar_cleanup_requested = true;
                xmp_sidecar_cleanup_message
                    = "existing destination xmp sidecar should be removed";
            } else if (!sidecar_b.empty() && file_exists(sidecar_b)) {
                xmp_sidecar_cleanup_requested = true;
                xmp_sidecar_cleanup_path      = sidecar_b;
                xmp_sidecar_cleanup_message
                    = "existing destination xmp sidecar should be removed";
            } else {
                xmp_sidecar_cleanup_message
                    = "no existing destination xmp sidecar detected";
            }
        }

        PreparedTransferC2paSignRequest sign_request;
        const TransferStatus sign_request_status
            = build_prepared_c2pa_sign_request(prepared.bundle, &sign_request);
        if (prepared.file_status == TransferFileStatus::Ok
            && (!c2pa_binding_output_path.empty()
                || !c2pa_handoff_output_path.empty())) {
            did_dump_c2pa_binding = !c2pa_binding_output_path.empty();
            did_dump_c2pa_handoff = !c2pa_handoff_output_path.empty();
            std::vector<std::byte> binding_input;
            if (!read_file_bytes(target_path, &binding_input)) {
                c2pa_handoff.binding.status = TransferStatus::InvalidArgument;
                c2pa_handoff.binding.code   = EmitTransferCode::InvalidArgument;
                c2pa_handoff.binding.errors = 1U;
                c2pa_handoff.binding.message
                    = "failed to read c2pa binding target bytes";
            } else {
                build_prepared_c2pa_handoff_package(
                    prepared.bundle,
                    std::span<const std::byte>(binding_input.data(),
                                               binding_input.size()),
                    &c2pa_handoff);
                if (c2pa_handoff.binding.status == TransferStatus::Ok
                    && did_dump_c2pa_binding) {
                    if (!force && file_exists(c2pa_binding_output_path)) {
                        c2pa_handoff.binding.status
                            = TransferStatus::InvalidArgument;
                        c2pa_handoff.binding.code
                            = EmitTransferCode::InvalidArgument;
                        c2pa_handoff.binding.errors = 1U;
                        c2pa_handoff.binding.message
                            = "c2pa binding output exists (use --force)";
                        c2pa_handoff.binding.written = 0U;
                    } else if (!write_file_bytes(
                                   c2pa_binding_output_path,
                                   std::span<const std::byte>(
                                       c2pa_handoff.binding_bytes.data(),
                                       c2pa_handoff.binding_bytes.size()))) {
                        c2pa_handoff.binding.status
                            = TransferStatus::InternalError;
                        c2pa_handoff.binding.code
                            = EmitTransferCode::BackendWriteFailed;
                        c2pa_handoff.binding.errors = 1U;
                        c2pa_handoff.binding.message
                            = "failed to write c2pa binding output";
                        c2pa_handoff.binding.written = 0U;
                    }
                }
                if (c2pa_handoff.binding.status == TransferStatus::Ok
                    && did_dump_c2pa_handoff) {
                    std::vector<std::byte> handoff_bytes;
                    c2pa_handoff_io = serialize_prepared_c2pa_handoff_package(
                        c2pa_handoff, &handoff_bytes);
                    if (c2pa_handoff_io.status == TransferStatus::Ok) {
                        if (!force && file_exists(c2pa_handoff_output_path)) {
                            c2pa_handoff_io.status
                                = TransferStatus::InvalidArgument;
                            c2pa_handoff_io.code
                                = EmitTransferCode::InvalidArgument;
                            c2pa_handoff_io.errors = 1U;
                            c2pa_handoff_io.message
                                = "c2pa handoff output exists (use --force)";
                            c2pa_handoff_io.bytes = 0U;
                        } else if (!write_file_bytes(c2pa_handoff_output_path,
                                                     std::span<const std::byte>(
                                                         handoff_bytes.data(),
                                                         handoff_bytes.size()))) {
                            c2pa_handoff_io.status
                                = TransferStatus::InternalError;
                            c2pa_handoff_io.code
                                = EmitTransferCode::BackendWriteFailed;
                            c2pa_handoff_io.errors = 1U;
                            c2pa_handoff_io.message
                                = "failed to write c2pa handoff output";
                            c2pa_handoff_io.bytes = 0U;
                        }
                    }
                }
            }
        }
        if (prepared.file_status == TransferFileStatus::Ok
            && !c2pa_signed_package_output_path.empty()
            && c2pa_signed_package.request.status == TransferStatus::Ok) {
            did_dump_c2pa_signed_package = true;
            std::vector<std::byte> signed_package_bytes;
            c2pa_signed_package_io
                = serialize_prepared_c2pa_signed_package(c2pa_signed_package,
                                                         &signed_package_bytes);
            if (c2pa_signed_package_io.status == TransferStatus::Ok) {
                if (!force && file_exists(c2pa_signed_package_output_path)) {
                    c2pa_signed_package_io.status
                        = TransferStatus::InvalidArgument;
                    c2pa_signed_package_io.code
                        = EmitTransferCode::InvalidArgument;
                    c2pa_signed_package_io.errors = 1U;
                    c2pa_signed_package_io.message
                        = "signed c2pa package output exists (use --force)";
                    c2pa_signed_package_io.bytes = 0U;
                } else if (!write_file_bytes(c2pa_signed_package_output_path,
                                             std::span<const std::byte>(
                                                 signed_package_bytes.data(),
                                                 signed_package_bytes.size()))) {
                    c2pa_signed_package_io.status
                        = TransferStatus::InternalError;
                    c2pa_signed_package_io.code
                        = EmitTransferCode::BackendWriteFailed;
                    c2pa_signed_package_io.errors = 1U;
                    c2pa_signed_package_io.message
                        = "failed to write signed c2pa package output";
                    c2pa_signed_package_io.bytes = 0U;
                }
            }
        }
        if (prepared.file_status == TransferFileStatus::Ok
            && !transfer_payload_batch_output_path.empty()) {
            did_dump_transfer_payload_batch = true;
            const EmitTransferResult payload_batch_status
                = build_prepared_transfer_payload_batch(prepared.bundle,
                                                        &transfer_payload_batch);
            if (payload_batch_status.status != TransferStatus::Ok) {
                transfer_payload_batch_io.status = payload_batch_status.status;
                transfer_payload_batch_io.code   = payload_batch_status.code;
                transfer_payload_batch_io.errors = payload_batch_status.errors;
                transfer_payload_batch_io.message = payload_batch_status.message;
            } else {
                std::vector<std::byte> payload_batch_bytes;
                transfer_payload_batch_io
                    = serialize_prepared_transfer_payload_batch(
                        transfer_payload_batch, &payload_batch_bytes);
                if (transfer_payload_batch_io.status == TransferStatus::Ok) {
                    if (!force
                        && file_exists(transfer_payload_batch_output_path)) {
                        transfer_payload_batch_io.status
                            = TransferStatus::InvalidArgument;
                        transfer_payload_batch_io.code
                            = EmitTransferCode::InvalidArgument;
                        transfer_payload_batch_io.errors = 1U;
                        transfer_payload_batch_io.message
                            = "transfer payload batch output exists (use --force)";
                        transfer_payload_batch_io.bytes = 0U;
                    } else if (!write_file_bytes(
                                   transfer_payload_batch_output_path,
                                   std::span<const std::byte>(
                                       payload_batch_bytes.data(),
                                       payload_batch_bytes.size()))) {
                        transfer_payload_batch_io.status
                            = TransferStatus::InternalError;
                        transfer_payload_batch_io.code
                            = EmitTransferCode::BackendWriteFailed;
                        transfer_payload_batch_io.errors = 1U;
                        transfer_payload_batch_io.message
                            = "failed to write transfer payload batch output";
                        transfer_payload_batch_io.bytes = 0U;
                    }
                }
            }
        }
        if (prepared.file_status == TransferFileStatus::Ok
            && !jxl_encoder_handoff_output_path.empty()) {
            did_dump_jxl_encoder_handoff = true;
            if (prepared.bundle.target_format != TransferTargetFormat::Jxl) {
                jxl_encoder_handoff_io.status = TransferStatus::InvalidArgument;
                jxl_encoder_handoff_io.code = EmitTransferCode::InvalidArgument;
                jxl_encoder_handoff_io.errors = 1U;
                jxl_encoder_handoff_io.message
                    = "jxl encoder handoff dump requires --target-jxl";
            } else {
                const EmitTransferResult handoff_status
                    = build_prepared_jxl_encoder_handoff(prepared.bundle,
                                                         &jxl_encoder_handoff);
                if (handoff_status.status != TransferStatus::Ok) {
                    jxl_encoder_handoff_io.status  = handoff_status.status;
                    jxl_encoder_handoff_io.code    = handoff_status.code;
                    jxl_encoder_handoff_io.errors  = handoff_status.errors;
                    jxl_encoder_handoff_io.message = handoff_status.message;
                } else {
                    std::vector<std::byte> handoff_bytes;
                    jxl_encoder_handoff_io
                        = serialize_prepared_jxl_encoder_handoff(
                            jxl_encoder_handoff, &handoff_bytes);
                    if (jxl_encoder_handoff_io.status == TransferStatus::Ok) {
                        if (!force
                            && file_exists(jxl_encoder_handoff_output_path)) {
                            jxl_encoder_handoff_io.status
                                = TransferStatus::InvalidArgument;
                            jxl_encoder_handoff_io.code
                                = EmitTransferCode::InvalidArgument;
                            jxl_encoder_handoff_io.errors = 1U;
                            jxl_encoder_handoff_io.message
                                = "jxl encoder handoff output exists (use --force)";
                            jxl_encoder_handoff_io.bytes = 0U;
                        } else if (!write_file_bytes(
                                       jxl_encoder_handoff_output_path,
                                       std::span<const std::byte>(
                                           handoff_bytes.data(),
                                           handoff_bytes.size()))) {
                            jxl_encoder_handoff_io.status
                                = TransferStatus::InternalError;
                            jxl_encoder_handoff_io.code
                                = EmitTransferCode::BackendWriteFailed;
                            jxl_encoder_handoff_io.errors = 1U;
                            jxl_encoder_handoff_io.message
                                = "failed to write jxl encoder handoff output";
                            jxl_encoder_handoff_io.bytes = 0U;
                        }
                    }
                }
            }
        }

        ExecutePreparedTransferOptions exec_options;
        exec_options.time_patches = build_transfer_time_patch_inputs(
            pending_time_patches);
        exec_options.time_patch          = time_patch_opts;
        exec_options.time_patch_auto_nul = time_patch_auto_nul;
        exec_options.emit_repeat         = emit_repeat;
        exec_options.jpeg_edit           = edit_plan_opts;
        exec_options.tiff_edit.strip_existing_xmp
            = xmp_destination_embedded_mode
              == XmpDestinationEmbeddedMode::StripExisting;
        exec_options.strip_existing_xmp
            = xmp_destination_embedded_mode
              == XmpDestinationEmbeddedMode::StripExisting;
        if (xmp_destination_embedded_mode
                == XmpDestinationEmbeddedMode::StripExisting
            && xmp_writeback_mode == XmpWritebackMode::SidecarOnly) {
            exec_options.jpeg_edit.strip_existing_xmp = true;
        }
        exec_options.edit_requested      = need_jpeg_edit || need_tiff_edit
                                      || need_webp_edit || need_png_edit
                                      || need_jp2_edit || need_jxl_edit
                                      || need_bmff_edit;
        exec_options.edit_apply = !dry_run && !output_path.empty();
        if (use_output_writer) {
            exec_options.edit_output_writer = &output_writer;
        }

        ExecutePreparedTransferResult exec;
        MappedFile mapped_edit_file;
        bool did_execute = false;
        if (prepared.file_status == TransferFileStatus::Ok
            && (prepared.prepare.status == TransferStatus::Ok
                || (did_stage_c2pa
                    && c2pa_stage_result.status == TransferStatus::Ok))
            && (!did_append_jumbf
                || append_jumbf_result.status == TransferStatus::Ok)) {
            std::span<const std::byte> edit_input;
            if (exec_options.edit_requested) {
                const MappedFileStatus map_status
                    = mapped_edit_file.open(target_path.c_str(),
                                            options.policy.max_file_bytes);
                if (map_status != MappedFileStatus::Ok) {
                    std::fprintf(stderr, "  edit_input: map_failed: %s\n",
                                 display_target.c_str());
                    any_failed = true;
                    continue;
                }
                edit_input = mapped_edit_file.bytes();
            }
            exec = execute_prepared_transfer(&prepared.bundle, edit_input,
                                             exec_options);
            did_execute = true;
        }
        if (prepared.file_status == TransferFileStatus::Ok
            && !transfer_package_batch_output_path.empty()) {
            did_dump_transfer_package_batch = true;
            if (!did_execute) {
                transfer_package_batch_io.status
                    = TransferStatus::InvalidArgument;
                transfer_package_batch_io.code
                    = EmitTransferCode::InvalidArgument;
                transfer_package_batch_io.errors = 1U;
                transfer_package_batch_io.message
                    = "transfer execution is not available for package batch dump";
            } else {
                std::span<const std::byte> package_input;
                if (exec.edit_requested) {
                    package_input = mapped_edit_file.bytes();
                }
                const EmitTransferResult package_batch_status
                    = build_executed_transfer_package_batch(
                        package_input, prepared.bundle, exec,
                        &transfer_package_batch);
                if (package_batch_status.status != TransferStatus::Ok) {
                    transfer_package_batch_io.status
                        = package_batch_status.status;
                    transfer_package_batch_io.code = package_batch_status.code;
                    transfer_package_batch_io.errors
                        = package_batch_status.errors;
                    transfer_package_batch_io.message
                        = package_batch_status.message;
                } else {
                    std::vector<std::byte> package_batch_bytes;
                    transfer_package_batch_io
                        = serialize_prepared_transfer_package_batch(
                            transfer_package_batch, &package_batch_bytes);
                    if (transfer_package_batch_io.status
                        == TransferStatus::Ok) {
                        if (!force
                            && file_exists(transfer_package_batch_output_path)) {
                            transfer_package_batch_io.status
                                = TransferStatus::InvalidArgument;
                            transfer_package_batch_io.code
                                = EmitTransferCode::InvalidArgument;
                            transfer_package_batch_io.errors = 1U;
                            transfer_package_batch_io.message
                                = "transfer package batch output exists (use --force)";
                            transfer_package_batch_io.bytes = 0U;
                        } else if (!write_file_bytes(
                                       transfer_package_batch_output_path,
                                       std::span<const std::byte>(
                                           package_batch_bytes.data(),
                                           package_batch_bytes.size()))) {
                            transfer_package_batch_io.status
                                = TransferStatus::InternalError;
                            transfer_package_batch_io.code
                                = EmitTransferCode::BackendWriteFailed;
                            transfer_package_batch_io.errors = 1U;
                            transfer_package_batch_io.message
                                = "failed to write transfer package batch output";
                            transfer_package_batch_io.bytes = 0U;
                        }
                    }
                }
            }
        }

        std::printf("  file_status=%s code=%s size=%llu entries=%u\n",
                    transfer_file_status_name(prepared.file_status),
                    prepare_transfer_file_code_name(prepared.code),
                    static_cast<unsigned long long>(prepared.file_size),
                    prepared.entry_count);
        std::printf("  read: scan=%s payload=%s exif=%s xmp=%s jumbf=%s\n",
                    scan_status_name(prepared.read.scan.status),
                    payload_status_name(prepared.read.payload.status),
                    exif_status_name(prepared.read.exif.status),
                    xmp_status_name(prepared.read.xmp.status),
                    jumbf_status_name(prepared.read.jumbf.status));
        std::printf(
            "  prepare: status=%s code=%s blocks=%u warnings=%u errors=%u\n",
            transfer_status_name(prepared.prepare.status),
            prepare_transfer_code_name(prepared.prepare.code),
            static_cast<unsigned>(prepared.bundle.blocks.size()),
            prepared.prepare.warnings, prepared.prepare.errors);
        if (!prepared.prepare.message.empty()) {
            std::printf("  prepare_message=%s\n",
                        prepared.prepare.message.c_str());
        }
        if (xmp_include_existing_sidecar) {
            std::printf("  xmp_existing_sidecar: status=%s loaded=%s",
                        transfer_status_name(
                            prepared.xmp_existing_sidecar_status),
                        prepared.xmp_existing_sidecar_loaded ? "yes" : "no");
            if (!prepared.xmp_existing_sidecar_path.empty()) {
                std::printf(
                    " path=%s",
                    console_path(prepared.xmp_existing_sidecar_path).c_str());
            }
            std::printf("\n");
            if (!prepared.xmp_existing_sidecar_message.empty()) {
                std::printf("  xmp_existing_sidecar_message=%s\n",
                            prepared.xmp_existing_sidecar_message.c_str());
            }
        }
        if (xmp_include_existing_destination_embedded) {
            std::printf(
                "  xmp_existing_destination_embedded: status=%s loaded=%s",
                transfer_status_name(
                    prepared.xmp_existing_destination_embedded_status),
                prepared.xmp_existing_destination_embedded_loaded ? "yes"
                                                                  : "no");
            if (!prepared.xmp_existing_destination_embedded_path.empty()) {
                std::printf(" path=%s",
                            console_path(
                                prepared.xmp_existing_destination_embedded_path)
                                .c_str());
            }
            std::printf("\n");
            if (!prepared.xmp_existing_destination_embedded_message.empty()) {
                std::printf(
                    "  xmp_existing_destination_embedded_message=%s\n",
                    prepared.xmp_existing_destination_embedded_message
                        .c_str());
            }
        }
        if (xmp_sidecar_requested) {
            std::printf("  xmp_sidecar: status=%s",
                        transfer_status_name(xmp_sidecar_status));
            if (!xmp_sidecar_path.empty()) {
                std::printf(" path=%s", console_path(xmp_sidecar_path).c_str());
            }
            std::printf(" bytes=%llu\n",
                        static_cast<unsigned long long>(
                            xmp_sidecar_output.size()));
            if (!xmp_sidecar_message.empty()) {
                std::printf("  xmp_sidecar_message=%s\n",
                            xmp_sidecar_message.c_str());
            }
            if (xmp_sidecar_status != TransferStatus::Ok) {
                any_failed = true;
            }
        }
        if (xmp_destination_sidecar_mode
            == XmpDestinationSidecarMode::StripExisting) {
            std::printf("  xmp_sidecar_cleanup: status=%s requested=%s",
                        transfer_status_name(xmp_sidecar_cleanup_status),
                        xmp_sidecar_cleanup_requested ? "yes" : "no");
            if (!xmp_sidecar_cleanup_path.empty()) {
                std::printf(" path=%s",
                            console_path(xmp_sidecar_cleanup_path).c_str());
            }
            std::printf("\n");
            if (!xmp_sidecar_cleanup_message.empty()) {
                std::printf("  xmp_sidecar_cleanup_message=%s\n",
                            xmp_sidecar_cleanup_message.c_str());
            }
            if (xmp_sidecar_cleanup_status != TransferStatus::Ok) {
                any_failed = true;
            }
        }
        if (did_append_jumbf) {
            std::printf(
                "  append_jumbf: status=%s code=%s emitted=%u removed=%u errors=%u\n",
                transfer_status_name(append_jumbf_result.status),
                emit_transfer_code_name(append_jumbf_result.code),
                append_jumbf_result.emitted, append_jumbf_result.skipped,
                append_jumbf_result.errors);
            if (!append_jumbf_result.message.empty()) {
                std::printf("  append_jumbf_message=%s\n",
                            append_jumbf_result.message.c_str());
            }
        }
        if (did_stage_c2pa) {
            std::printf(
                "  c2pa_stage_validate: status=%s code=%s kind=%s payload_bytes=%llu carrier_bytes=%llu segments=%u errors=%u\n",
                transfer_status_name(c2pa_stage_validation.status),
                emit_transfer_code_name(c2pa_stage_validation.code),
                transfer_c2pa_signed_payload_kind_name(
                    c2pa_stage_validation.payload_kind),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.logical_payload_bytes),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.staged_payload_bytes),
                c2pa_stage_validation.staged_segments,
                c2pa_stage_validation.errors);
            std::printf(
                "  c2pa_stage_semantics: status=%s reason=%s manifest=%llu manifests=%llu claim_generator=%llu assertions=%llu claims=%llu signatures=%llu linked=%llu orphan=%llu explicit_refs=%llu unresolved=%llu ambiguous=%llu\n",
                transfer_c2pa_semantic_status_name(
                    c2pa_stage_validation.semantic_status),
                c2pa_stage_validation.semantic_reason.c_str(),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_manifest_present),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_manifest_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_claim_generator_present),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_assertion_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_claim_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_signature_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_signature_linked),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_signature_orphan),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_explicit_reference_signature_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_explicit_reference_unresolved_signature_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_explicit_reference_ambiguous_signature_count));
            std::printf(
                "  c2pa_stage_linkage: claim0_assertions=%llu claim0_refs=%llu sig0_links=%llu\n",
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_primary_claim_assertion_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_primary_claim_referenced_by_signature_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_primary_signature_linked_claim_count));
            std::printf(
                "  c2pa_stage_references: sig0_keys=%llu sig0_present=%llu sig0_resolved=%llu\n",
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_primary_signature_reference_key_hits),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_primary_signature_explicit_reference_present),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_primary_signature_explicit_reference_resolved_claim_count));
            if (!c2pa_stage_validation.message.empty()) {
                std::printf("  c2pa_stage_validate_message=%s\n",
                            c2pa_stage_validation.message.c_str());
            }
            std::printf(
                "  c2pa_stage: status=%s code=%s emitted=%u removed=%u errors=%u\n",
                transfer_status_name(c2pa_stage_result.status),
                emit_transfer_code_name(c2pa_stage_result.code),
                c2pa_stage_result.emitted, c2pa_stage_result.skipped,
                c2pa_stage_result.errors);
            if (!c2pa_stage_result.message.empty()) {
                std::printf("  c2pa_stage_message=%s\n",
                            c2pa_stage_result.message.c_str());
            }
        }
        for (size_t pi = 0; pi < prepared.bundle.policy_decisions.size();
             ++pi) {
            const PreparedTransferPolicyDecision& decision
                = prepared.bundle.policy_decisions[pi];
            if (decision.subject == TransferPolicySubject::C2pa) {
                std::printf(
                    "  policy[%s]: requested=%s effective=%s reason=%s mode=%s source=%s output=%s matched=%u\n",
                    transfer_policy_subject_name(decision.subject),
                    transfer_policy_action_name(decision.requested),
                    transfer_policy_action_name(decision.effective),
                    transfer_policy_reason_name(decision.reason),
                    transfer_c2pa_mode_name(decision.c2pa_mode),
                    transfer_c2pa_source_kind_name(decision.c2pa_source_kind),
                    transfer_c2pa_prepared_output_name(
                        decision.c2pa_prepared_output),
                    decision.matched_entries);
            } else {
                std::printf(
                    "  policy[%s]: requested=%s effective=%s reason=%s matched=%u\n",
                    transfer_policy_subject_name(decision.subject),
                    transfer_policy_action_name(decision.requested),
                    transfer_policy_action_name(decision.effective),
                    transfer_policy_reason_name(decision.reason),
                    decision.matched_entries);
            }
            if (!decision.message.empty()) {
                std::printf("  policy[%s]_message=%s\n",
                            transfer_policy_subject_name(decision.subject),
                            decision.message.c_str());
            }
        }
        if (prepared.bundle.c2pa_rewrite.state
                != TransferC2paRewriteState::NotApplicable
            || prepared.bundle.c2pa_rewrite.matched_entries > 0U) {
            const PreparedTransferC2paRewriteRequirements& rewrite
                = prepared.bundle.c2pa_rewrite;
            std::printf(
                "  c2pa_rewrite: state=%s target=%s source=%s matched=%u existing_segments=%u carrier_available=%s invalidates_existing=%s\n",
                transfer_c2pa_rewrite_state_name(rewrite.state),
                transfer_target_format_name(rewrite.target_format),
                transfer_c2pa_source_kind_name(rewrite.source_kind),
                rewrite.matched_entries, rewrite.existing_carrier_segments,
                rewrite.target_carrier_available ? "yes" : "no",
                rewrite.content_change_invalidates_existing ? "yes" : "no");
            if (rewrite.requires_manifest_builder
                || rewrite.requires_content_binding
                || rewrite.requires_certificate_chain
                || rewrite.requires_private_key
                || rewrite.requires_signing_time) {
                std::printf(
                    "  c2pa_rewrite_requirements: manifest_builder=%s content_binding=%s certificate_chain=%s private_key=%s signing_time=%s\n",
                    rewrite.requires_manifest_builder ? "yes" : "no",
                    rewrite.requires_content_binding ? "yes" : "no",
                    rewrite.requires_certificate_chain ? "yes" : "no",
                    rewrite.requires_private_key ? "yes" : "no",
                    rewrite.requires_signing_time ? "yes" : "no");
            }
            if (!rewrite.message.empty()) {
                std::printf("  c2pa_rewrite_message=%s\n",
                            rewrite.message.c_str());
            }
            if (!rewrite.content_binding_chunks.empty()) {
                std::printf("  c2pa_rewrite_binding: chunks=%u bytes=%llu\n",
                            static_cast<unsigned>(
                                rewrite.content_binding_chunks.size()),
                            static_cast<unsigned long long>(
                                rewrite.content_binding_bytes));
                for (size_t ci = 0; ci < rewrite.content_binding_chunks.size();
                     ++ci) {
                    const PreparedTransferC2paRewriteChunk& chunk
                        = rewrite.content_binding_chunks[ci];
                    if (chunk.kind
                        == TransferC2paRewriteChunkKind::SourceRange) {
                        std::printf(
                            "  c2pa_rewrite_chunk[%u]: kind=%s offset=%llu size=%llu\n",
                            static_cast<unsigned>(ci),
                            transfer_c2pa_rewrite_chunk_kind_name(chunk.kind),
                            static_cast<unsigned long long>(chunk.source_offset),
                            static_cast<unsigned long long>(chunk.size));
                    } else if (chunk.kind
                               == TransferC2paRewriteChunkKind::
                                   PreparedBmffMetaBox) {
                        std::printf(
                            "  c2pa_rewrite_chunk[%u]: kind=%s size=%llu\n",
                            static_cast<unsigned>(ci),
                            transfer_c2pa_rewrite_chunk_kind_name(chunk.kind),
                            static_cast<unsigned long long>(chunk.size));
                    } else {
                        std::printf(
                            "  c2pa_rewrite_chunk[%u]: kind=%s block=%u marker=0x%02X size=%llu route=%s\n",
                            static_cast<unsigned>(ci),
                            transfer_c2pa_rewrite_chunk_kind_name(chunk.kind),
                            chunk.block_index, chunk.jpeg_marker_code,
                            static_cast<unsigned long long>(chunk.size),
                            chunk.block_index < prepared.bundle.blocks.size()
                                ? prepared.bundle.blocks[chunk.block_index]
                                      .route.c_str()
                                : "invalid");
                    }
                }
            }
        }
        if (prepared.bundle.c2pa_rewrite.state
                != TransferC2paRewriteState::NotApplicable
            || prepared.bundle.c2pa_rewrite.matched_entries > 0U) {
            std::printf(
                "  c2pa_sign_request: status=%s carrier=%s manifest_label=%s source_ranges=%u prepared_segments=%u bytes=%llu\n",
                transfer_status_name(sign_request_status),
                sign_request.carrier_route.empty()
                    ? "-"
                    : sign_request.carrier_route.c_str(),
                sign_request.manifest_label.empty()
                    ? "-"
                    : sign_request.manifest_label.c_str(),
                sign_request.source_range_chunks,
                sign_request.prepared_segment_chunks,
                static_cast<unsigned long long>(
                    sign_request.content_binding_bytes));
            if (!sign_request.message.empty()) {
                std::printf("  c2pa_sign_request_message=%s\n",
                            sign_request.message.c_str());
            }
        }
        if (did_dump_c2pa_binding) {
            std::printf(
                "  c2pa_binding: status=%s code=%s bytes=%llu errors=%u path=%s\n",
                transfer_status_name(c2pa_handoff.binding.status),
                emit_transfer_code_name(c2pa_handoff.binding.code),
                static_cast<unsigned long long>(c2pa_handoff.binding.written),
                c2pa_handoff.binding.errors,
                console_path(c2pa_binding_output_path).c_str());
            if (!c2pa_handoff.request.carrier_route.empty()) {
                std::printf(
                    "  c2pa_handoff: carrier=%s manifest_label=%s bytes=%llu source_ranges=%u prepared_segments=%u\n",
                    c2pa_handoff.request.carrier_route.c_str(),
                    c2pa_handoff.request.manifest_label.c_str(),
                    static_cast<unsigned long long>(
                        c2pa_handoff.binding.written),
                    c2pa_handoff.request.source_range_chunks,
                    c2pa_handoff.request.prepared_segment_chunks);
            }
            if (!c2pa_handoff.binding.message.empty()) {
                std::printf("  c2pa_binding_message=%s\n",
                            c2pa_handoff.binding.message.c_str());
            }
        }
        if (did_dump_c2pa_handoff) {
            std::printf(
                "  c2pa_handoff_package: status=%s code=%s bytes=%llu errors=%u path=%s\n",
                transfer_status_name(c2pa_handoff_io.status),
                emit_transfer_code_name(c2pa_handoff_io.code),
                static_cast<unsigned long long>(c2pa_handoff_io.bytes),
                c2pa_handoff_io.errors,
                console_path(c2pa_handoff_output_path).c_str());
            if (!c2pa_handoff_io.message.empty()) {
                std::printf("  c2pa_handoff_package_message=%s\n",
                            c2pa_handoff_io.message.c_str());
            }
        }
        if (!c2pa_signed_package_input_path.empty()) {
            std::printf(
                "  c2pa_signed_package_input: status=%s code=%s bytes=%llu errors=%u path=%s\n",
                transfer_status_name(c2pa_signed_package_io.status),
                emit_transfer_code_name(c2pa_signed_package_io.code),
                static_cast<unsigned long long>(c2pa_signed_package_io.bytes),
                c2pa_signed_package_io.errors,
                console_path(c2pa_signed_package_input_path).c_str());
            if (!c2pa_signed_package_io.message.empty()) {
                std::printf("  c2pa_signed_package_input_message=%s\n",
                            c2pa_signed_package_io.message.c_str());
            }
        }
        if (did_dump_c2pa_signed_package) {
            std::printf(
                "  c2pa_signed_package: status=%s code=%s bytes=%llu errors=%u path=%s\n",
                transfer_status_name(c2pa_signed_package_io.status),
                emit_transfer_code_name(c2pa_signed_package_io.code),
                static_cast<unsigned long long>(c2pa_signed_package_io.bytes),
                c2pa_signed_package_io.errors,
                console_path(c2pa_signed_package_output_path).c_str());
            if (!c2pa_signed_package_io.message.empty()) {
                std::printf("  c2pa_signed_package_message=%s\n",
                            c2pa_signed_package_io.message.c_str());
            }
        }
        if (did_dump_transfer_payload_batch) {
            std::printf(
                "  transfer_payload_batch: status=%s code=%s bytes=%llu errors=%u path=%s\n",
                transfer_status_name(transfer_payload_batch_io.status),
                emit_transfer_code_name(transfer_payload_batch_io.code),
                static_cast<unsigned long long>(transfer_payload_batch_io.bytes),
                transfer_payload_batch_io.errors,
                console_path(transfer_payload_batch_output_path).c_str());
            if (!transfer_payload_batch_io.message.empty()) {
                std::printf("  transfer_payload_batch_message=%s\n",
                            transfer_payload_batch_io.message.c_str());
            }
        }
        if (did_dump_transfer_package_batch) {
            std::printf(
                "  transfer_package_batch: status=%s code=%s bytes=%llu errors=%u path=%s\n",
                transfer_status_name(transfer_package_batch_io.status),
                emit_transfer_code_name(transfer_package_batch_io.code),
                static_cast<unsigned long long>(transfer_package_batch_io.bytes),
                transfer_package_batch_io.errors,
                console_path(transfer_package_batch_output_path).c_str());
            if (!transfer_package_batch_io.message.empty()) {
                std::printf("  transfer_package_batch_message=%s\n",
                            transfer_package_batch_io.message.c_str());
            }
        }
        if (did_dump_jxl_encoder_handoff) {
            std::printf(
                "  jxl_encoder_handoff: status=%s code=%s bytes=%llu errors=%u path=%s\n",
                transfer_status_name(jxl_encoder_handoff_io.status),
                emit_transfer_code_name(jxl_encoder_handoff_io.code),
                static_cast<unsigned long long>(jxl_encoder_handoff_io.bytes),
                jxl_encoder_handoff_io.errors,
                console_path(jxl_encoder_handoff_output_path).c_str());
            if (!jxl_encoder_handoff_io.message.empty()) {
                std::printf("  jxl_encoder_handoff_message=%s\n",
                            jxl_encoder_handoff_io.message.c_str());
            }
        }

        if (prepared.file_status != TransferFileStatus::Ok
            || (prepared.prepare.status != TransferStatus::Ok
                && (!did_stage_c2pa
                    || c2pa_stage_result.status != TransferStatus::Ok)
                && (!did_dump_c2pa_binding
                    || c2pa_handoff.binding.status != TransferStatus::Ok)
                && (!did_dump_c2pa_handoff
                    || c2pa_handoff_io.status != TransferStatus::Ok)
                && (!did_dump_transfer_payload_batch
                    || transfer_payload_batch_io.status != TransferStatus::Ok)
                && (!did_dump_transfer_package_batch
                    || transfer_package_batch_io.status != TransferStatus::Ok)
                && (!did_dump_jxl_encoder_handoff
                    || jxl_encoder_handoff_io.status != TransferStatus::Ok))) {
            any_failed = true;
            continue;
        }
        if (did_append_jumbf
            && append_jumbf_result.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (did_stage_c2pa && c2pa_stage_result.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (did_dump_c2pa_binding
            && c2pa_handoff.binding.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (did_dump_c2pa_handoff
            && c2pa_handoff_io.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (!c2pa_signed_package_input_path.empty()
            && c2pa_signed_package_io.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (did_dump_c2pa_signed_package
            && c2pa_signed_package_io.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (did_dump_transfer_payload_batch
            && transfer_payload_batch_io.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (did_dump_transfer_package_batch
            && transfer_package_batch_io.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (did_dump_jxl_encoder_handoff
            && jxl_encoder_handoff_io.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (prepared.prepare.status != TransferStatus::Ok
            && (!did_stage_c2pa
                || c2pa_stage_result.status != TransferStatus::Ok)
            && ((did_dump_c2pa_binding
                 && c2pa_handoff.binding.status == TransferStatus::Ok)
                || (did_dump_c2pa_handoff
                    && c2pa_handoff_io.status == TransferStatus::Ok)
                || (did_dump_transfer_payload_batch
                    && transfer_payload_batch_io.status == TransferStatus::Ok)
                || (did_dump_jxl_encoder_handoff
                    && jxl_encoder_handoff_io.status == TransferStatus::Ok))
            && !exec_options.edit_requested) {
            continue;
        }

        std::printf("  time_patch: status=%s patched=%u skipped=%u errors=%u\n",
                    transfer_status_name(exec.time_patch.status),
                    exec.time_patch.patched_slots,
                    exec.time_patch.skipped_slots, exec.time_patch.errors);
        if (!exec.time_patch.message.empty()) {
            std::printf("  time_patch_message=%s\n",
                        exec.time_patch.message.c_str());
        }
        if (exec.time_patch.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }

        for (uint32_t bi = 0; bi < prepared.bundle.blocks.size(); ++bi) {
            const PreparedTransferBlock& block = prepared.bundle.blocks[bi];
            std::printf("  [%u] route=%s kind=%s size=%u\n", bi,
                        block.route.c_str(), transfer_kind_name(block.kind),
                        static_cast<unsigned>(block.payload.size()));
            if (!write_payloads) {
                continue;
            }
            const std::string out_path = payload_dump_path(source_path, out_dir,
                                                           block, bi);
            if (!force && file_exists(out_path)) {
                std::fprintf(stderr, "  [%u] exists: %s (use --force)\n", bi,
                             console_path(out_path).c_str());
                any_failed = true;
                continue;
            }
            if (!write_file_bytes(out_path, std::span<const std::byte>(
                                                block.payload.data(),
                                                block.payload.size()))) {
                std::fprintf(stderr, "  [%u] write_failed: %s\n", bi,
                             console_path(out_path).c_str());
                any_failed = true;
                continue;
            }
            std::printf("  [%u] wrote=%s\n", bi,
                        console_path(out_path).c_str());
        }

        if (prepared.bundle.target_format == TransferTargetFormat::Jpeg) {
            if (exec.edit_requested) {
                if (exec.edit_plan_status == TransferStatus::Ok) {
                    std::printf(
                        "  edit_plan: status=%s requested=%s selected=%s "
                        "in_place_possible=%s emitted=%u replaced=%u appended=%u "
                        "removed=%u removed_jumbf=%u removed_c2pa=%u "
                        "input=%llu output=%llu scan_end=%llu\n",
                        transfer_status_name(exec.edit_plan_status),
                        jpeg_edit_mode_name(exec.jpeg_edit_plan.requested_mode),
                        jpeg_edit_mode_name(exec.jpeg_edit_plan.selected_mode),
                        exec.jpeg_edit_plan.in_place_possible ? "true"
                                                              : "false",
                        exec.jpeg_edit_plan.emitted_segments,
                        exec.jpeg_edit_plan.replaced_segments,
                        exec.jpeg_edit_plan.appended_segments,
                        exec.jpeg_edit_plan.removed_existing_segments,
                        exec.jpeg_edit_plan.removed_existing_jumbf_segments,
                        exec.jpeg_edit_plan.removed_existing_c2pa_segments,
                        static_cast<unsigned long long>(exec.edit_input_size),
                        static_cast<unsigned long long>(exec.edit_output_size),
                        static_cast<unsigned long long>(
                            exec.jpeg_edit_plan.leading_scan_end));
                } else {
                    std::printf("  edit_plan: status=%s\n",
                                transfer_status_name(exec.edit_plan_status));
                }
                if (!exec.edit_plan_message.empty()) {
                    std::printf("  edit_plan_message=%s\n",
                                exec.edit_plan_message.c_str());
                }
                if (exec.edit_plan_status != TransferStatus::Ok) {
                    any_failed = true;
                }
            }

            if (!output_path.empty()) {
                if (!force && output_exists) {
                    std::fprintf(stderr,
                                 "  edit_apply: exists: %s (use --force)\n",
                                 display_output.c_str());
                    any_failed = true;
                    continue;
                }
                if (!dry_run) {
                    std::printf(
                        "  edit_apply: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                        transfer_status_name(exec.edit_apply.status),
                        emit_transfer_code_name(exec.edit_apply.code),
                        exec.edit_apply.emitted, exec.edit_apply.skipped,
                        exec.edit_apply.errors);
                    if (!exec.edit_apply.message.empty()) {
                        std::printf("  edit_apply_message=%s\n",
                                    exec.edit_apply.message.c_str());
                    }
                    if (exec.edit_apply.status != TransferStatus::Ok) {
                        any_failed = true;
                        continue;
                    }
                    if (use_output_writer
                        && output_writer.finish() != TransferStatus::Ok) {
                        std::fprintf(stderr, "  edit_apply: write_failed: %s\n",
                                     display_output.c_str());
                        any_failed = true;
                        continue;
                    }
                    const CliPreparedTransferFileState file_state
                        = make_cli_prepared_transfer_file_state(
                            xmp_sidecar_requested, xmp_sidecar_status,
                            xmp_sidecar_message, xmp_sidecar_path,
                            xmp_sidecar_output, xmp_sidecar_cleanup_requested,
                            xmp_sidecar_cleanup_status,
                            xmp_sidecar_cleanup_message,
                            xmp_sidecar_cleanup_path);
                    if (!persist_cli_edit_output_via_core(
                            "edit_apply", output_path, force,
                            use_output_writer,
                            use_output_writer
                                ? output_writer.bytes_written()
                                : uint64_t{0},
                            prepared, exec, file_state)) {
                        any_failed = true;
                        continue;
                    }
                }
            }

            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t mi = 0; mi < exec.marker_summary.size(); ++mi) {
                const EmittedJpegMarkerSummary& one = exec.marker_summary[mi];
                std::printf("  marker %s count=%u bytes=%llu\n",
                            marker_name(one.marker).c_str(), one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            continue;
        }

        if (target_format_is_tiff_family(prepared.bundle.target_format)) {
            const bool is_dng_target
                = prepared.bundle.target_format == TransferTargetFormat::Dng;
            const char* const edit_prefix
                = is_dng_target ? "dng_edit" : "tiff_edit";
            if (exec.edit_requested) {
                if (exec.edit_plan_status == TransferStatus::Ok) {
                    std::printf(
                        "  %s: status=%s updates=%u exif=%s input=%llu output=%llu\n",
                        edit_prefix,
                        transfer_status_name(exec.edit_plan_status),
                        static_cast<unsigned>(exec.tiff_edit_plan.tag_updates),
                        exec.tiff_edit_plan.has_exif_ifd ? "on" : "off",
                        static_cast<unsigned long long>(exec.edit_input_size),
                        static_cast<unsigned long long>(exec.edit_output_size));
                } else {
                    std::printf("  %s: status=%s\n", edit_prefix,
                                transfer_status_name(exec.edit_plan_status));
                }
                if (!exec.edit_plan_message.empty()) {
                    std::printf("  %s_message=%s\n", edit_prefix,
                                exec.edit_plan_message.c_str());
                }
                if (exec.edit_plan_status != TransferStatus::Ok) {
                    any_failed = true;
                }
            }

            if (!output_path.empty()) {
                if (!force && output_exists) {
                    std::fprintf(stderr,
                                 "  %s_apply: exists: %s (use --force)\n",
                                 edit_prefix, display_output.c_str());
                    any_failed = true;
                    continue;
                }
                if (!dry_run) {
                    std::printf(
                        "  %s_apply: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                        edit_prefix,
                        transfer_status_name(exec.edit_apply.status),
                        emit_transfer_code_name(exec.edit_apply.code),
                        exec.edit_apply.emitted, exec.edit_apply.skipped,
                        exec.edit_apply.errors);
                    if (!exec.edit_apply.message.empty()) {
                        std::printf("  %s_apply_message=%s\n", edit_prefix,
                                    exec.edit_apply.message.c_str());
                    }
                    if (exec.edit_apply.status != TransferStatus::Ok) {
                        any_failed = true;
                        continue;
                    }
                    if (use_output_writer
                        && output_writer.finish() != TransferStatus::Ok) {
                        std::fprintf(stderr, "  %s_apply: write_failed: %s\n",
                                     edit_prefix, display_output.c_str());
                        any_failed = true;
                        continue;
                    }
                    const CliPreparedTransferFileState file_state
                        = make_cli_prepared_transfer_file_state(
                            xmp_sidecar_requested, xmp_sidecar_status,
                            xmp_sidecar_message, xmp_sidecar_path,
                            xmp_sidecar_output, xmp_sidecar_cleanup_requested,
                            xmp_sidecar_cleanup_status,
                            xmp_sidecar_cleanup_message,
                            xmp_sidecar_cleanup_path);
                    if (!persist_cli_edit_output_via_core(
                            is_dng_target ? "dng_edit_apply"
                                          : "tiff_edit_apply",
                            output_path, force,
                            use_output_writer,
                            use_output_writer
                                ? output_writer.bytes_written()
                                : uint64_t{0},
                            prepared, exec, file_state)) {
                        any_failed = true;
                        continue;
                    }
                }
            }

            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            std::printf("  tiff_commit=%s\n",
                        exec.tiff_commit ? "true" : "false");
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t si = 0; si < exec.tiff_tag_summary.size(); ++si) {
                const EmittedTiffTagSummary& one = exec.tiff_tag_summary[si];
                std::printf("  tiff_tag %u count=%u bytes=%llu\n",
                            static_cast<unsigned>(one.tag), one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            continue;
        }

        if (prepared.bundle.target_format == TransferTargetFormat::Jxl) {
            if (exec.edit_requested) {
                if (exec.edit_plan_status == TransferStatus::Ok) {
                    std::printf(
                        "  edit_plan: status=%s input=%llu output=%llu\n",
                        transfer_status_name(exec.edit_plan_status),
                        static_cast<unsigned long long>(exec.edit_input_size),
                        static_cast<unsigned long long>(exec.edit_output_size));
                } else {
                    std::printf("  edit_plan: status=%s\n",
                                transfer_status_name(exec.edit_plan_status));
                }
                if (!exec.edit_plan_message.empty()) {
                    std::printf("  edit_plan_message=%s\n",
                                exec.edit_plan_message.c_str());
                }
                if (exec.edit_plan_status != TransferStatus::Ok) {
                    any_failed = true;
                }
            }

            if (!output_path.empty()) {
                if (!force && output_exists) {
                    std::fprintf(stderr,
                                 "  edit_apply: exists: %s (use --force)\n",
                                 display_output.c_str());
                    any_failed = true;
                    continue;
                }
                if (!dry_run) {
                    std::printf(
                        "  edit_apply: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                        transfer_status_name(exec.edit_apply.status),
                        emit_transfer_code_name(exec.edit_apply.code),
                        exec.edit_apply.emitted, exec.edit_apply.skipped,
                        exec.edit_apply.errors);
                    if (!exec.edit_apply.message.empty()) {
                        std::printf("  edit_apply_message=%s\n",
                                    exec.edit_apply.message.c_str());
                    }
                    if (exec.edit_apply.status != TransferStatus::Ok) {
                        any_failed = true;
                        continue;
                    }
                    if (use_output_writer
                        && output_writer.finish() != TransferStatus::Ok) {
                        std::fprintf(stderr, "  edit_apply: write_failed: %s\n",
                                     display_output.c_str());
                        any_failed = true;
                        continue;
                    }
                    const CliPreparedTransferFileState file_state
                        = make_cli_prepared_transfer_file_state(
                            xmp_sidecar_requested, xmp_sidecar_status,
                            xmp_sidecar_message, xmp_sidecar_path,
                            xmp_sidecar_output, xmp_sidecar_cleanup_requested,
                            xmp_sidecar_cleanup_status,
                            xmp_sidecar_cleanup_message,
                            xmp_sidecar_cleanup_path);
                    if (!persist_cli_edit_output_via_core(
                            "edit_apply", output_path, force,
                            use_output_writer,
                            use_output_writer
                                ? output_writer.bytes_written()
                                : uint64_t{0},
                            prepared, exec, file_state)) {
                        any_failed = true;
                        continue;
                    }
                }
            }

            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t bi = 0; bi < exec.jxl_box_summary.size(); ++bi) {
                const EmittedJxlBoxSummary& one = exec.jxl_box_summary[bi];
                std::printf("  jxl_box %s count=%u bytes=%llu\n",
                            jxl_box_name(one.type).c_str(), one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            PreparedJxlEncoderHandoffView handoff;
            const EmitTransferResult handoff_result
                = build_prepared_jxl_encoder_handoff_view(prepared.bundle,
                                                          &handoff);
            if (handoff_result.status != TransferStatus::Ok) {
                std::printf(
                    "  jxl_encoder_handoff: status=%s code=%s errors=%u\n",
                    transfer_status_name(handoff_result.status),
                    emit_transfer_code_name(handoff_result.code),
                    handoff_result.errors);
                if (!handoff_result.message.empty()) {
                    std::printf("  jxl_encoder_handoff_message=%s\n",
                                handoff_result.message.c_str());
                }
                any_failed = true;
                continue;
            }
            if (handoff.has_icc_profile) {
                std::printf("  jxl_icc_profile bytes=%llu\n",
                            static_cast<unsigned long long>(
                                handoff.icc_profile_bytes));
            }
            continue;
        }

        if (prepared.bundle.target_format == TransferTargetFormat::Webp) {
            if (exec.edit_requested) {
                if (exec.edit_plan_status == TransferStatus::Ok) {
                    std::printf(
                        "  webp_edit: status=%s input=%llu output=%llu\n",
                        transfer_status_name(exec.edit_plan_status),
                        static_cast<unsigned long long>(exec.edit_input_size),
                        static_cast<unsigned long long>(exec.edit_output_size));
                } else {
                    std::printf("  webp_edit: status=%s\n",
                                transfer_status_name(exec.edit_plan_status));
                }
                if (!exec.edit_plan_message.empty()) {
                    std::printf("  webp_edit_message=%s\n",
                                exec.edit_plan_message.c_str());
                }
                if (exec.edit_plan_status != TransferStatus::Ok) {
                    any_failed = true;
                }
            }

            if (!output_path.empty()) {
                if (!force && output_exists) {
                    std::fprintf(stderr,
                                 "  webp_edit_apply: exists: %s (use --force)\n",
                                 display_output.c_str());
                    any_failed = true;
                    continue;
                }
                if (!dry_run) {
                    std::printf(
                        "  webp_edit_apply: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                        transfer_status_name(exec.edit_apply.status),
                        emit_transfer_code_name(exec.edit_apply.code),
                        exec.edit_apply.emitted, exec.edit_apply.skipped,
                        exec.edit_apply.errors);
                    if (!exec.edit_apply.message.empty()) {
                        std::printf("  webp_edit_apply_message=%s\n",
                                    exec.edit_apply.message.c_str());
                    }
                    if (exec.edit_apply.status != TransferStatus::Ok) {
                        any_failed = true;
                        continue;
                    }
                    if (use_output_writer
                        && output_writer.finish() != TransferStatus::Ok) {
                        std::fprintf(stderr,
                                     "  webp_edit_apply: write_failed: %s\n",
                                     display_output.c_str());
                        any_failed = true;
                        continue;
                    }
                    const CliPreparedTransferFileState file_state
                        = make_cli_prepared_transfer_file_state(
                            xmp_sidecar_requested, xmp_sidecar_status,
                            xmp_sidecar_message, xmp_sidecar_path,
                            xmp_sidecar_output, xmp_sidecar_cleanup_requested,
                            xmp_sidecar_cleanup_status,
                            xmp_sidecar_cleanup_message,
                            xmp_sidecar_cleanup_path);
                    if (!persist_cli_edit_output_via_core(
                            "webp_edit_apply", output_path, force,
                            use_output_writer,
                            use_output_writer
                                ? output_writer.bytes_written()
                                : uint64_t{0},
                            prepared, exec, file_state)) {
                        any_failed = true;
                        continue;
                    }
                }
            }

            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t ci = 0; ci < exec.webp_chunk_summary.size(); ++ci) {
                const EmittedWebpChunkSummary& one = exec.webp_chunk_summary[ci];
                std::printf("  webp_chunk %s count=%u bytes=%llu\n",
                            webp_chunk_name(one.type).c_str(), one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            continue;
        }

        if (prepared.bundle.target_format == TransferTargetFormat::Png) {
            if (exec.edit_requested) {
                if (exec.edit_plan_status == TransferStatus::Ok) {
                    std::printf(
                        "  png_edit: status=%s input=%llu output=%llu\n",
                        transfer_status_name(exec.edit_plan_status),
                        static_cast<unsigned long long>(exec.edit_input_size),
                        static_cast<unsigned long long>(exec.edit_output_size));
                } else {
                    std::printf("  png_edit: status=%s\n",
                                transfer_status_name(exec.edit_plan_status));
                }
                if (!exec.edit_plan_message.empty()) {
                    std::printf("  png_edit_message=%s\n",
                                exec.edit_plan_message.c_str());
                }
                if (exec.edit_plan_status != TransferStatus::Ok) {
                    any_failed = true;
                }
            }

            if (!output_path.empty()) {
                if (!force && output_exists) {
                    std::fprintf(stderr,
                                 "  png_edit_apply: exists: %s (use --force)\n",
                                 display_output.c_str());
                    any_failed = true;
                    continue;
                }
                if (!dry_run) {
                    std::printf(
                        "  png_edit_apply: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                        transfer_status_name(exec.edit_apply.status),
                        emit_transfer_code_name(exec.edit_apply.code),
                        exec.edit_apply.emitted, exec.edit_apply.skipped,
                        exec.edit_apply.errors);
                    if (!exec.edit_apply.message.empty()) {
                        std::printf("  png_edit_apply_message=%s\n",
                                    exec.edit_apply.message.c_str());
                    }
                    if (exec.edit_apply.status != TransferStatus::Ok) {
                        any_failed = true;
                        continue;
                    }
                    if (use_output_writer
                        && output_writer.finish() != TransferStatus::Ok) {
                        std::fprintf(stderr,
                                     "  png_edit_apply: write_failed: %s\n",
                                     display_output.c_str());
                        any_failed = true;
                        continue;
                    }
                    const CliPreparedTransferFileState file_state
                        = make_cli_prepared_transfer_file_state(
                            xmp_sidecar_requested, xmp_sidecar_status,
                            xmp_sidecar_message, xmp_sidecar_path,
                            xmp_sidecar_output, xmp_sidecar_cleanup_requested,
                            xmp_sidecar_cleanup_status,
                            xmp_sidecar_cleanup_message,
                            xmp_sidecar_cleanup_path);
                    if (!persist_cli_edit_output_via_core(
                            "png_edit_apply", output_path, force,
                            use_output_writer,
                            use_output_writer
                                ? output_writer.bytes_written()
                                : uint64_t{0},
                            prepared, exec, file_state)) {
                        any_failed = true;
                        continue;
                    }
                }
            }

            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t ci = 0; ci < exec.png_chunk_summary.size(); ++ci) {
                const EmittedPngChunkSummary& one = exec.png_chunk_summary[ci];
                std::printf("  png_chunk %s count=%u bytes=%llu\n",
                            png_chunk_name(one.type).c_str(), one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            continue;
        }

        if (prepared.bundle.target_format == TransferTargetFormat::Jp2) {
            if (exec.edit_requested) {
                if (exec.edit_plan_status == TransferStatus::Ok) {
                    std::printf(
                        "  jp2_edit: status=%s input=%llu output=%llu\n",
                        transfer_status_name(exec.edit_plan_status),
                        static_cast<unsigned long long>(exec.edit_input_size),
                        static_cast<unsigned long long>(exec.edit_output_size));
                } else {
                    std::printf("  jp2_edit: status=%s\n",
                                transfer_status_name(exec.edit_plan_status));
                }
                if (!exec.edit_plan_message.empty()) {
                    std::printf("  jp2_edit_message=%s\n",
                                exec.edit_plan_message.c_str());
                }
                if (exec.edit_plan_status != TransferStatus::Ok) {
                    any_failed = true;
                }
            }

            if (!output_path.empty()) {
                if (!force && output_exists) {
                    std::fprintf(stderr,
                                 "  jp2_edit_apply: exists: %s (use --force)\n",
                                 display_output.c_str());
                    any_failed = true;
                    continue;
                }
                if (!dry_run) {
                    std::printf(
                        "  jp2_edit_apply: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                        transfer_status_name(exec.edit_apply.status),
                        emit_transfer_code_name(exec.edit_apply.code),
                        exec.edit_apply.emitted, exec.edit_apply.skipped,
                        exec.edit_apply.errors);
                    if (!exec.edit_apply.message.empty()) {
                        std::printf("  jp2_edit_apply_message=%s\n",
                                    exec.edit_apply.message.c_str());
                    }
                    if (exec.edit_apply.status != TransferStatus::Ok) {
                        any_failed = true;
                        continue;
                    }
                    if (use_output_writer
                        && output_writer.finish() != TransferStatus::Ok) {
                        std::fprintf(stderr,
                                     "  jp2_edit_apply: write_failed: %s\n",
                                     display_output.c_str());
                        any_failed = true;
                        continue;
                    }
                    const CliPreparedTransferFileState file_state
                        = make_cli_prepared_transfer_file_state(
                            xmp_sidecar_requested, xmp_sidecar_status,
                            xmp_sidecar_message, xmp_sidecar_path,
                            xmp_sidecar_output, xmp_sidecar_cleanup_requested,
                            xmp_sidecar_cleanup_status,
                            xmp_sidecar_cleanup_message,
                            xmp_sidecar_cleanup_path);
                    if (!persist_cli_edit_output_via_core(
                            "jp2_edit_apply", output_path, force,
                            use_output_writer,
                            use_output_writer
                                ? output_writer.bytes_written()
                                : uint64_t{0},
                            prepared, exec, file_state)) {
                        any_failed = true;
                        continue;
                    }
                }
            }

            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t bi = 0; bi < exec.jp2_box_summary.size(); ++bi) {
                const EmittedJp2BoxSummary& one = exec.jp2_box_summary[bi];
                std::printf("  jp2_box %s count=%u bytes=%llu\n",
                            jxl_box_name(one.type).c_str(), one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            continue;
        }

        if (prepared.bundle.target_format == TransferTargetFormat::Exr) {
            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t ai = 0; ai < exec.exr_attribute_summary.size(); ++ai) {
                const EmittedExrAttributeSummary& one
                    = exec.exr_attribute_summary[ai];
                std::printf("  exr_attribute %s type=%s count=%u bytes=%llu\n",
                            one.name.c_str(), one.type_name.c_str(),
                            one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            continue;
        }

        if (target_format_is_bmff(prepared.bundle.target_format)) {
            if (exec.edit_requested) {
                if (exec.edit_plan_status == TransferStatus::Ok) {
                    std::printf(
                        "  bmff_edit: status=%s input=%llu output=%llu\n",
                        transfer_status_name(exec.edit_plan_status),
                        static_cast<unsigned long long>(exec.edit_input_size),
                        static_cast<unsigned long long>(exec.edit_output_size));
                } else {
                    std::printf("  bmff_edit: status=%s\n",
                                transfer_status_name(exec.edit_plan_status));
                }
                if (!exec.edit_plan_message.empty()) {
                    std::printf("  bmff_edit_message=%s\n",
                                exec.edit_plan_message.c_str());
                }
                if (exec.edit_plan_status != TransferStatus::Ok) {
                    any_failed = true;
                }
            }

            if (!output_path.empty()) {
                if (!force && output_exists) {
                    std::fprintf(stderr,
                                 "  bmff_edit_apply: exists: %s (use --force)\n",
                                 display_output.c_str());
                    any_failed = true;
                    continue;
                }
                if (!dry_run) {
                    std::printf(
                        "  bmff_edit_apply: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                        transfer_status_name(exec.edit_apply.status),
                        emit_transfer_code_name(exec.edit_apply.code),
                        exec.edit_apply.emitted, exec.edit_apply.skipped,
                        exec.edit_apply.errors);
                    if (!exec.edit_apply.message.empty()) {
                        std::printf("  bmff_edit_apply_message=%s\n",
                                    exec.edit_apply.message.c_str());
                    }
                    if (exec.edit_apply.status != TransferStatus::Ok) {
                        any_failed = true;
                        continue;
                    }
                    if (use_output_writer
                        && output_writer.finish() != TransferStatus::Ok) {
                        std::fprintf(stderr,
                                     "  bmff_edit_apply: write_failed: %s\n",
                                     display_output.c_str());
                        any_failed = true;
                        continue;
                    }
                    const CliPreparedTransferFileState file_state
                        = make_cli_prepared_transfer_file_state(
                            xmp_sidecar_requested, xmp_sidecar_status,
                            xmp_sidecar_message, xmp_sidecar_path,
                            xmp_sidecar_output, xmp_sidecar_cleanup_requested,
                            xmp_sidecar_cleanup_status,
                            xmp_sidecar_cleanup_message,
                            xmp_sidecar_cleanup_path);
                    if (!persist_cli_edit_output_via_core(
                            "bmff_edit_apply", output_path, force,
                            use_output_writer,
                            use_output_writer
                                ? output_writer.bytes_written()
                                : uint64_t{0},
                            prepared, exec, file_state)) {
                        any_failed = true;
                        continue;
                    }
                }
            }

            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t bi = 0; bi < exec.bmff_item_summary.size(); ++bi) {
                const EmittedBmffItemSummary& one = exec.bmff_item_summary[bi];
                std::printf("  bmff_item %s count=%u bytes=%llu\n",
                            bmff_item_name(one.item_type, one.mime_xmp).c_str(),
                            one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            for (size_t bi = 0; bi < exec.bmff_property_summary.size(); ++bi) {
                const EmittedBmffPropertySummary& one
                    = exec.bmff_property_summary[bi];
                std::printf("  bmff_property %s count=%u bytes=%llu\n",
                            bmff_property_name(one.property_type,
                                               one.property_subtype)
                                .c_str(),
                            one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            continue;
        }

        std::fprintf(stderr, "  emit: unsupported bundle target format\n");
        any_failed = true;
    }

    return any_failed ? 1 : 0;
}
