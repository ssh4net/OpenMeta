// SPDX-License-Identifier: Apache-2.0

#include "cli_parse.h"
#include "openmeta/build_info.h"
#include "openmeta/console_format.h"
#include "openmeta/validate.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static std::string console_path(std::string_view path)
    {
        std::string escaped;
        (void)append_console_escaped_ascii(path, 4096U, &escaped);
        return escaped;
    }

    struct ValidationSummary final {
        uint32_t files_checked = 0;
        uint32_t files_failed  = 0;
        uint32_t warnings      = 0;
        uint32_t errors        = 0;
    };


    struct JsonFileResult final {
        std::string path;
        ValidateResult result;
    };


    static void usage(const char* argv0)
    {
        std::printf(
            "Usage: %s [options] <file> [file...]\n"
            "\n"
            "Validate metadata decode health and DNG/CCM coherency.\n"
            "\n"
            "Options:\n"
            "  --help                   Show this help\n"
            "  --version                Print OpenMeta build info\n"
            "  --no-build-info          Hide build info header\n"
            "  --json                   Print machine-readable JSON output\n"
            "  --xmp-sidecar            Also read sidecar XMP (<file>.xmp, <basename>.xmp)\n"
            "  --no-pointer-tags        Do not store pointer tags\n"
            "  --makernotes             Attempt MakerNote decode (best-effort)\n"
            "  --no-printim             Do not decode PrintIM fields\n"
            "  --no-decompress          Do not decompress payloads\n"
            "  --warnings-as-errors     Fail files that have warnings\n"
            "  --strict                 Alias for --warnings-as-errors\n"
            "  --c2pa-verify            Request draft C2PA verify scaffold evaluation\n"
            "  --c2pa-verify-backend <none|auto|native|openssl>\n"
            "                           Verification backend preference\n"
            "  --c2pa-verify-require-trusted-chain\n"
            "                           Fail C2PA verify when the certificate chain is untrusted or missing\n"
            "  --c2pa-verify-require-resolved-references\n"
            "                           Fail C2PA verify when explicit references are unresolved or ambiguous\n"
            "\n"
            "DNG/CCM validation:\n"
            "  --ccm-validation <none|dng-warnings>\n"
            "                           CCM validation mode (default: dng-warnings)\n"
            "  --ccm-no-require-dng-context\n"
            "                           Allow CCM query outside explicit DNG context\n"
            "  --ccm-no-reduction       Skip ReductionMatrix* fields\n"
            "  --max-ccm-fields N       Max CCM fields to collect (default: 128)\n"
            "  --max-ccm-values N       Max values per CCM field (default: 256)\n"
            "\n"
            "Resource limits:\n"
            "  --max-file-bytes N       Optional file mapping cap in bytes (default: 0=unlimited)\n"
            "  --max-payload-bytes N    Max reassembled/decompressed payload bytes\n"
            "  --max-payload-parts N    Max payload part count\n"
            "  --max-exif-ifds N        Max EXIF/TIFF IFD count\n"
            "  --max-exif-entries N     Max EXIF/TIFF entries per IFD\n"
            "  --max-exif-total N       Max total EXIF/TIFF entries\n"
            "  --max-exif-value-bytes N Max EXIF value bytes per tag\n"
            "  --max-xmp-input-bytes N  Max XMP packet bytes\n"
            "\n"
            "Capability legend:\n"
            "  scan   container/block discovery in file bytes\n"
            "  decode structured metadata decode into MetaStore entries\n"
            "  names  tag/key name mapping for human-readable output\n"
            "  dump   sidecar/preview export support via metadump/thumdump\n"
            "  details: docs/metadata_support.md (draft)\n",
            argv0 ? argv0 : "metavalidate");
    }


    static bool parse_u64_arg(const char* s, uint64_t* out)
    {
        return tool_parse::parse_decimal_u64(s, out);
    }


    static bool parse_u32_arg(const char* s, uint32_t* out)
    {
        return tool_parse::parse_decimal_u32(s, out);
    }


    static bool parse_c2pa_verify_backend_arg(const char* s,
                                              C2paVerifyBackend* out)
    {
        if (!s || !*s || !out) {
            return false;
        }
        if (std::strcmp(s, "none") == 0) {
            *out = C2paVerifyBackend::None;
            return true;
        }
        if (std::strcmp(s, "auto") == 0) {
            *out = C2paVerifyBackend::Auto;
            return true;
        }
        if (std::strcmp(s, "native") == 0) {
            *out = C2paVerifyBackend::Native;
            return true;
        }
        if (std::strcmp(s, "openssl") == 0) {
            *out = C2paVerifyBackend::OpenSsl;
            return true;
        }
        return false;
    }


    static bool parse_ccm_validation_mode(const char* s, CcmValidationMode* out)
    {
        if (!s || !*s || !out) {
            return false;
        }
        if (std::strcmp(s, "none") == 0) {
            *out = CcmValidationMode::None;
            return true;
        }
        if (std::strcmp(s, "dng-warnings") == 0) {
            *out = CcmValidationMode::DngSpecWarnings;
            return true;
        }
        return false;
    }


    static const char* validate_status_name(ValidateStatus status) noexcept
    {
        switch (status) {
        case ValidateStatus::Ok: return "ok";
        case ValidateStatus::OpenFailed: return "open_failed";
        case ValidateStatus::TooLarge: return "too_large";
        case ValidateStatus::ReadFailed: return "read_failed";
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


    static const char* exr_status_name(ExrDecodeStatus status) noexcept
    {
        switch (status) {
        case ExrDecodeStatus::Ok: return "ok";
        case ExrDecodeStatus::Unsupported: return "unsupported";
        case ExrDecodeStatus::Malformed: return "malformed";
        case ExrDecodeStatus::LimitExceeded: return "limit_exceeded";
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


    static const char* c2pa_verify_status_name(C2paVerifyStatus status) noexcept
    {
        switch (status) {
        case C2paVerifyStatus::NotRequested: return "not_requested";
        case C2paVerifyStatus::DisabledByBuild: return "disabled_by_build";
        case C2paVerifyStatus::BackendUnavailable: return "backend_unavailable";
        case C2paVerifyStatus::NoSignatures: return "no_signatures";
        case C2paVerifyStatus::InvalidSignature: return "invalid_signature";
        case C2paVerifyStatus::VerificationFailed: return "verification_failed";
        case C2paVerifyStatus::Verified: return "verified";
        case C2paVerifyStatus::NotImplemented: return "not_implemented";
        case C2paVerifyStatus::SignatureVerifiedOnly:
            return "signature_verified_only";
        }
        return "unknown";
    }


    static const char*
    c2pa_verify_backend_name(C2paVerifyBackend backend) noexcept
    {
        switch (backend) {
        case C2paVerifyBackend::None: return "none";
        case C2paVerifyBackend::Auto: return "auto";
        case C2paVerifyBackend::Native: return "native";
        case C2paVerifyBackend::OpenSsl: return "openssl";
        }
        return "unknown";
    }


    static const char* ccm_status_name(CcmQueryStatus status) noexcept
    {
        switch (status) {
        case CcmQueryStatus::Ok: return "ok";
        case CcmQueryStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static const char* ccm_validation_mode_name(CcmValidationMode mode) noexcept
    {
        switch (mode) {
        case CcmValidationMode::None: return "none";
        case CcmValidationMode::DngSpecWarnings: return "dng_warnings";
        }
        return "unknown";
    }


    static const char*
    issue_severity_name(ValidateIssueSeverity severity) noexcept
    {
        switch (severity) {
        case ValidateIssueSeverity::Warning: return "warning";
        case ValidateIssueSeverity::Error: return "error";
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


    static void append_json_escaped(std::string_view s, std::string* out)
    {
        if (!out) {
            return;
        }
        static constexpr char kHex[] = "0123456789ABCDEF";
        out->push_back('"');
        for (size_t i = 0; i < s.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            switch (c) {
            case '"': out->append("\\\""); break;
            case '\\': out->append("\\\\"); break;
            case '\b': out->append("\\b"); break;
            case '\f': out->append("\\f"); break;
            case '\n': out->append("\\n"); break;
            case '\r': out->append("\\r"); break;
            case '\t': out->append("\\t"); break;
            default:
                if (c < 0x20U) {
                    out->append("\\u00");
                    out->push_back(kHex[(c >> 4U) & 0x0FU]);
                    out->push_back(kHex[c & 0x0FU]);
                } else {
                    out->push_back(static_cast<char>(c));
                }
                break;
            }
        }
        out->push_back('"');
    }


    static const char* result_name(const ValidateResult& result) noexcept
    {
        if (result.failed) {
            return "fail";
        }
        if (result.warning_count != 0U) {
            return "warn";
        }
        return "ok";
    }


    static std::string
    build_json_report(const std::vector<JsonFileResult>& rows,
                      const ValidationSummary& summary,
                      const ValidateOptions& options, bool include_build_info)
    {
        std::string out;
        out.reserve(4096);
        out.push_back('{');

        if (include_build_info) {
            std::string line1;
            std::string line2;
            format_build_info_lines(&line1, &line2);
            out.append("\"build_info\":{");
            out.append("\"line1\":");
            append_json_escaped(line1, &out);
            out.append(",\"line2\":");
            append_json_escaped(line2, &out);
            out.append("},");
        }

        out.append("\"strict\":");
        out.append(options.warnings_as_errors ? "true" : "false");
        out.append(",\"files\":[");

        for (size_t i = 0; i < rows.size(); ++i) {
            if (i != 0U) {
                out.push_back(',');
            }
            const JsonFileResult& row    = rows[i];
            const ValidateResult& result = row.result;
            out.push_back('{');

            out.append("\"path\":");
            append_json_escaped(row.path, &out);
            out.append(",\"file_size\":");
            out.append(std::to_string(result.file_size));
            out.append(",\"status\":");
            append_json_escaped(validate_status_name(result.status), &out);
            out.append(",\"result\":");
            append_json_escaped(result_name(result), &out);
            out.append(",\"failed\":");
            out.append(result.failed ? "true" : "false");
            out.append(",\"error_count\":");
            out.append(std::to_string(result.error_count));
            out.append(",\"warning_count\":");
            out.append(std::to_string(result.warning_count));
            out.append(",\"entries\":");
            out.append(std::to_string(result.entries));

            if (result.status == ValidateStatus::Ok) {
                out.append(",\"scan_status\":");
                append_json_escaped(scan_status_name(result.read.scan.status),
                                    &out);
                out.append(",\"payload_status\":");
                append_json_escaped(payload_status_name(
                                        result.read.payload.status),
                                    &out);
                out.append(",\"exif_status\":");
                append_json_escaped(exif_status_name(result.read.exif.status),
                                    &out);
                out.append(",\"xmp_status\":");
                append_json_escaped(xmp_status_name(result.read.xmp.status),
                                    &out);
                out.append(",\"exr_status\":");
                append_json_escaped(exr_status_name(result.read.exr.status),
                                    &out);
                out.append(",\"jumbf_status\":");
                append_json_escaped(jumbf_status_name(result.read.jumbf.status),
                                    &out);
                out.append(",\"c2pa_verify_status\":");
                append_json_escaped(c2pa_verify_status_name(
                                        result.read.jumbf.verify_status),
                                    &out);
                out.append(",\"c2pa_verify_backend\":");
                append_json_escaped(
                    c2pa_verify_backend_name(
                        result.read.jumbf.verify_backend_selected),
                    &out);
                out.append(",\"c2pa_verify_require_trusted_chain\":");
                out.append(options.verify_require_trusted_chain ? "true"
                                                                : "false");
                out.append(",\"c2pa_verify_require_resolved_references\":");
                out.append(options.verify_require_resolved_references
                               ? "true"
                               : "false");
                out.append(",\"ccm_status\":");
                append_json_escaped(ccm_status_name(result.ccm.status), &out);
                out.append(",\"ccm_mode\":");
                append_json_escaped(ccm_validation_mode_name(
                                        options.ccm.validation_mode),
                                    &out);
                out.append(",\"ccm_require_dng\":");
                out.append(options.ccm.require_dng_context ? "true" : "false");
                out.append(",\"ccm_include_reduction\":");
                out.append(options.ccm.include_reduction_matrices ? "true"
                                                                  : "false");
                out.append(",\"ccm_fields_found\":");
                out.append(std::to_string(result.ccm.fields_found));
                out.append(",\"ccm_fields_dropped\":");
                out.append(std::to_string(result.ccm.fields_dropped));
                out.append(",\"ccm_issues_reported\":");
                out.append(std::to_string(result.ccm.issues_reported));
            }

            out.append(",\"issues\":[");
            for (size_t j = 0; j < result.issues.size(); ++j) {
                if (j != 0U) {
                    out.push_back(',');
                }
                const ValidateIssue& issue = result.issues[j];
                out.append("{\"severity\":");
                append_json_escaped(issue_severity_name(issue.severity), &out);
                out.append(",\"category\":");
                append_json_escaped(issue.category, &out);
                out.append(",\"code\":");
                append_json_escaped(issue.code, &out);
                out.append(",\"ifd\":");
                append_json_escaped(issue.ifd, &out);
                out.append(",\"name\":");
                append_json_escaped(issue.name, &out);
                out.append(",\"tag\":");
                out.append(std::to_string(static_cast<uint32_t>(issue.tag)));
                out.append(",\"message\":");
                append_json_escaped(issue.message, &out);
                out.push_back('}');
            }
            out.append("]}");
        }

        out.append("],\"summary\":{");
        out.append("\"files\":");
        out.append(std::to_string(summary.files_checked));
        out.append(",\"failed\":");
        out.append(std::to_string(summary.files_failed));
        out.append(",\"errors\":");
        out.append(std::to_string(summary.errors));
        out.append(",\"warnings\":");
        out.append(std::to_string(summary.warnings));
        out.append(",\"strict\":");
        out.append(options.warnings_as_errors ? "true" : "false");
        out.append("}}");
        return out;
    }


    static void print_issue(const ValidateIssue& issue)
    {
        const char* sev = issue_severity_name(issue.severity);
        if (issue.category == "ccm") {
            std::printf("  %s[ccm] code=%s ifd=%s tag=0x%04X name=%s msg=%s\n",
                        sev, issue.code.c_str(), issue.ifd.c_str(),
                        static_cast<unsigned>(issue.tag), issue.name.c_str(),
                        issue.message.c_str());
            return;
        }

        if (!issue.message.empty() && issue.message != issue.code) {
            std::printf("  %s[%s] %s msg=%s\n", sev, issue.category.c_str(),
                        issue.code.c_str(), issue.message.c_str());
        } else {
            std::printf("  %s[%s] %s\n", sev, issue.category.c_str(),
                        issue.code.c_str());
        }
    }


    static void print_result_header(const ValidateResult& result,
                                    const ValidateOptions& options)
    {
        std::printf("size=%llu\n",
                    static_cast<unsigned long long>(result.file_size));

        if (result.status != ValidateStatus::Ok) {
            std::printf("status=%s\n", validate_status_name(result.status));
            return;
        }

        std::printf(
            "scan=%s payload=%s exif=%s xmp=%s exr=%s jumbf=%s c2pa_verify=%s c2pa_backend=%s c2pa_require_trusted_chain=%s c2pa_require_resolved_refs=%s entries=%u\n",
            scan_status_name(result.read.scan.status),
            payload_status_name(result.read.payload.status),
            exif_status_name(result.read.exif.status),
            xmp_status_name(result.read.xmp.status),
            exr_status_name(result.read.exr.status),
            jumbf_status_name(result.read.jumbf.status),
            c2pa_verify_status_name(result.read.jumbf.verify_status),
            c2pa_verify_backend_name(result.read.jumbf.verify_backend_selected),
            options.verify_require_trusted_chain ? "on" : "off",
            options.verify_require_resolved_references ? "on" : "off",
            static_cast<unsigned>(result.entries));

        std::printf(
            "ccm=status=%s mode=%s require_dng=%s include_reduction=%s fields=%u dropped=%u issues=%u\n",
            ccm_status_name(result.ccm.status),
            ccm_validation_mode_name(options.ccm.validation_mode),
            options.ccm.require_dng_context ? "on" : "off",
            options.ccm.include_reduction_matrices ? "on" : "off",
            static_cast<unsigned>(result.ccm.fields_found),
            static_cast<unsigned>(result.ccm.fields_dropped),
            static_cast<unsigned>(result.ccm.issues_reported));
    }

}  // namespace
}  // namespace openmeta


int
main(int argc, char** argv)
{
    using namespace openmeta;

    bool show_build_info = true;
    bool json_output     = false;
    ValidateOptions options;
    options.policy.max_file_bytes = 0;

    int first_path = 1;
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
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--json") == 0) {
            json_output = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--xmp-sidecar") == 0) {
            options.include_xmp_sidecar = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--no-pointer-tags") == 0) {
            options.include_pointer_tags = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--makernotes") == 0) {
            options.decode_makernote = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--no-printim") == 0) {
            options.decode_printim = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--no-decompress") == 0) {
            options.decompress = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--warnings-as-errors") == 0
            || std::strcmp(arg, "--strict") == 0) {
            options.warnings_as_errors = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-verify") == 0) {
            options.verify_c2pa = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-verify-backend") == 0 && i + 1 < argc) {
            C2paVerifyBackend backend = C2paVerifyBackend::Auto;
            if (!parse_c2pa_verify_backend_arg(argv[i + 1], &backend)) {
                std::fprintf(stderr, "invalid --c2pa-verify-backend value\n");
                return 2;
            }
            options.verify_backend = backend;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-verify-require-trusted-chain") == 0) {
            options.verify_require_trusted_chain = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-verify-require-resolved-references")
            == 0) {
            options.verify_require_resolved_references = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--ccm-validation") == 0 && i + 1 < argc) {
            CcmValidationMode mode = CcmValidationMode::None;
            if (!parse_ccm_validation_mode(argv[i + 1], &mode)) {
                std::fprintf(stderr, "invalid --ccm-validation value\n");
                return 2;
            }
            options.ccm.validation_mode = mode;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--ccm-no-require-dng-context") == 0) {
            options.ccm.require_dng_context = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--ccm-no-reduction") == 0) {
            options.ccm.include_reduction_matrices = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-ccm-fields") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-ccm-fields value\n");
                return 2;
            }
            options.ccm.limits.max_fields = v;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-ccm-values") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-ccm-values value\n");
                return 2;
            }
            options.ccm.limits.max_values_per_field = v;
            i += 1;
            first_path += 2;
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
            first_path += 2;
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
            first_path += 2;
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
            first_path += 2;
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
            first_path += 2;
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
            first_path += 2;
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
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-exif-value-bytes") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (!parse_u64_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-exif-value-bytes value\n");
                return 2;
            }
            options.policy.exif_limits.max_value_bytes = v;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-xmp-input-bytes") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (!parse_u64_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-xmp-input-bytes value\n");
                return 2;
            }
            options.policy.xmp_limits.max_input_bytes = v;
            i += 1;
            first_path += 2;
            continue;
        }
        break;
    }

    if (argc <= first_path) {
        usage(argv[0]);
        return 2;
    }

    if (show_build_info && !json_output) {
        print_build_info_header();
    }

    ValidationSummary summary;
    std::vector<JsonFileResult> json_rows;
    if (json_output) {
        json_rows.reserve(static_cast<size_t>(argc - first_path));
    }
    int exit_code = 0;

    for (int argi = first_path; argi < argc; ++argi) {
        const char* path = argv[argi];
        if (!path || !*path) {
            continue;
        }

        summary.files_checked += 1U;

        ValidateResult result = validate_file(path, options);

        if (result.failed) {
            summary.files_failed += 1U;
            exit_code = 1;
        }

        summary.errors += result.error_count;
        summary.warnings += result.warning_count;

        if (json_output) {
            JsonFileResult row;
            row.path   = path;
            row.result = std::move(result);
            json_rows.push_back(std::move(row));
            continue;
        }

        std::printf("== %s\n", console_path(path).c_str());
        print_result_header(result, options);

        for (size_t i = 0; i < result.issues.size(); ++i) {
            print_issue(result.issues[i]);
        }

        if (result.failed) {
            std::printf("result=fail errors=%u warnings=%u\n",
                        static_cast<unsigned>(result.error_count),
                        static_cast<unsigned>(result.warning_count));
        } else if (result.warning_count != 0U) {
            std::printf("result=warn errors=%u warnings=%u\n",
                        static_cast<unsigned>(result.error_count),
                        static_cast<unsigned>(result.warning_count));
        } else {
            std::printf("result=ok errors=0 warnings=0\n");
        }
    }

    if (json_output) {
        const std::string json = build_json_report(json_rows, summary, options,
                                                   show_build_info);
        std::printf("%s\n", json.c_str());
        return exit_code;
    }

    std::printf("summary files=%u failed=%u errors=%u warnings=%u strict=%s\n",
                summary.files_checked, summary.files_failed, summary.errors,
                summary.warnings, options.warnings_as_errors ? "on" : "off");

    return exit_code;
}
