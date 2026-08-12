// SPDX-License-Identifier: Apache-2.0

#include "cli_parse.h"
#include "openmeta/build_info.h"
#include "openmeta/console_format.h"
#include "openmeta/mapped_file.h"
#include "openmeta/preview_extract.h"
#include "openmeta/resource_policy.h"
#include "openmeta/simple_meta.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
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

    static void usage(const char* argv0)
    {
        std::printf(
            "Usage: %s [options] <file> [file...]\n"
            "       %s [options] <source> <destination>\n"
            "\n"
            "Writes OpenMeta sidecar outputs.\n"
            "Default mode: XMP sidecar dump.\n"
            "Preview mode: --extract-preview.\n"
            "\n"
            "Options:\n"
            "  --help                 Show this help\n"
            "  --version              Print OpenMeta build info\n"
            "  --no-build-info        Hide build info header\n"
            "  -i, --input <path>     Input file (repeatable)\n"
            "  -o, --out <path>       Output file path (single input only;\n"
            "                         auto-suffixed as _N for multiple previews)\n"
            "  --out-dir <dir>        Output directory (for multiple inputs)\n"
            "  --force                Overwrite existing output files\n"
            "\n"
            "XMP dump mode (default):\n"
            "  --format <lossless|portable>\n"
            "                         XMP output format (default: lossless)\n"
            "  --portable             Alias for --format portable\n"
            "  --portable-no-exif     Portable mode: skip EXIF/TIFF/GPS mapped fields\n"
            "  --portable-include-existing-xmp\n"
            "                         Portable mode: include decoded standard XMP properties\n"
            "  --portable-exiftool-gpsdatetime-alias\n"
            "                         Portable mode: emit exif:GPSDateTime alias for GPS time\n"
            "  --xmp-sidecar           Also read sidecar XMP (<file>.xmp, <basename>.xmp)\n"
            "  --c2pa-verify           Request draft C2PA verify scaffold evaluation\n"
            "  --c2pa-verify-backend <none|auto|native|openssl>\n"
            "                         Verification backend preference\n"
            "  --c2pa-verify-require-trusted-chain\n"
            "                         Fail C2PA verify when the certificate chain is untrusted or missing\n"
            "  --c2pa-verify-require-resolved-references\n"
            "                         Fail C2PA verify when explicit references are unresolved or ambiguous\n"
            "  --no-pointer-tags       Do not store pointer tags\n"
            "  --makernotes            Attempt MakerNote decode (best-effort)\n"
            "  --no-decompress         Do not decompress payloads\n"
            "  --max-file-bytes N      Optional file mapping cap in bytes (default: 0=unlimited)\n"
            "  --max-payload-bytes N   Max reassembled/decompressed payload bytes\n"
            "  --max-payload-parts N   Max payload part count\n"
            "  --max-exif-ifds N       Max EXIF/TIFF IFD count\n"
            "  --max-exif-entries N    Max EXIF/TIFF entries per IFD\n"
            "  --max-exif-total N      Max total EXIF/TIFF entries\n"
            "  --max-exif-value-bytes N\n"
            "                         Max EXIF value bytes per tag\n"
            "  --max-xmp-input-bytes N Max XMP packet bytes\n"
            "  --max-output-bytes N    Refuse to generate dumps larger than N bytes (0=unlimited)\n"
            "  --max-entries N         Refuse to emit more than N entries (0=unlimited)\n"
            "\n"
            "Preview mode (--extract-preview):\n"
            "  --extract-preview       Export embedded previews/thumbnails\n"
            "  --first-only           Export only the first candidate per file\n"
            "  --require-jpeg-soi     Keep only candidates starting with JPEG SOI (FFD8)\n"
            "  --max-preview-ifds N   Max preview scan IFD count\n"
            "  --max-preview-total N  Max preview scan total entries\n"
            "  --max-preview-bytes N  Refuse preview candidates larger than N bytes\n"
            "                         (default: 134217728)\n"
            "  --max-candidates N     Max candidates written per file (default: 32)\n"
            "\n"
            "Capability legend:\n"
            "  scan   container/block discovery in file bytes\n"
            "  decode structured metadata decode into MetaStore entries\n"
            "  names  tag/key name mapping for human-readable output\n"
            "  dump   sidecar/preview export support via metadump/thumdump\n"
            "  details: docs/metadata_support.md (draft)\n",
            argv0 ? argv0 : "metadump", argv0 ? argv0 : "metadump");
    }


    static bool parse_u64_arg(const char* s, uint64_t* out)
    {
        return tool_parse::parse_decimal_u64(s, out);
    }


    static bool parse_u32_arg(const char* s, uint32_t* out)
    {
        return tool_parse::parse_decimal_u32(s, out);
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


    static bool file_exists(const std::string& path)
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        std::fclose(f);
        return true;
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


    static bool read_file_bytes(const char* path, std::vector<std::byte>* out,
                                uint64_t max_bytes)
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (!path || !*path) {
            return false;
        }

        std::FILE* f = std::fopen(path, "rb");
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
        if (max_bytes != 0U && static_cast<uint64_t>(end) > max_bytes) {
            std::fclose(f);
            return false;
        }
        if (std::fseek(f, 0, SEEK_SET) != 0) {
            std::fclose(f);
            return false;
        }

        out->resize(static_cast<size_t>(end));
        if (end > 0) {
            const size_t n = std::fread(out->data(), 1, out->size(), f);
            if (n != out->size()) {
                std::fclose(f);
                out->clear();
                return false;
            }
        }
        std::fclose(f);
        return true;
    }


    static void xmp_sidecar_candidates(const char* path, std::string* out_a,
                                       std::string* out_b)
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
        *out_b = s + ".xmp";

        const size_t sep = s.find_last_of("/\\");
        const size_t dot = s.find_last_of('.');
        if (dot != std::string::npos
            && (sep == std::string::npos || dot > sep)) {
            *out_a = s.substr(0, dot) + ".xmp";
        } else {
            *out_a = *out_b;
        }
        if (*out_a == *out_b) {
            out_b->clear();
        }
    }


    static std::string basename_only(const std::string& path)
    {
        const size_t sep = path.find_last_of("/\\");
        if (sep == std::string::npos) {
            return path;
        }
        return path.substr(sep + 1);
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

    static std::string default_out_path_for(const char* in_path,
                                            const std::string& out_dir)
    {
        if (!in_path) {
            return {};
        }
        const std::string in(in_path);
        const std::string base     = basename_only(in);
        const std::string out_name = base + ".xmp";
        if (out_dir.empty()) {
            return in + ".xmp";
        }
        return join_path(out_dir, out_name);
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

    static std::string default_preview_out_path_for(const char* in_path,
                                                    const std::string& out_dir,
                                                    uint32_t idx, bool is_jpeg)
    {
        const std::string ext = is_jpeg ? ".jpg" : ".bin";
        const std::string num = format_index(idx);
        if (out_dir.empty()) {
            std::string out = in_path ? std::string(in_path) : "file";
            out.append(".thumb.");
            out.append(num);
            out.append(ext);
            return out;
        }
        std::string base = in_path ? basename_only(in_path)
                                   : std::string("file");
        base             = sanitize_filename(base);
        std::string out  = base;
        out.append(".thumb.");
        out.append(num);
        out.append(ext);
        return join_path(out_dir, out);
    }

    static std::string with_index_suffix(const std::string& path,
                                         uint32_t one_based_index)
    {
        const size_t sep   = path.find_last_of("/\\");
        const size_t dot   = path.find_last_of('.');
        const bool has_ext = (dot != std::string::npos)
                             && (sep == std::string::npos || dot > sep);

        const std::string suffix = "_" + std::to_string(one_based_index);
        if (!has_ext) {
            return path + suffix;
        }

        std::string out;
        out.reserve(path.size() + suffix.size());
        out.append(path.data(), dot);
        out.append(suffix);
        out.append(path.data() + dot, path.size() - dot);
        return out;
    }

    static bool looks_like_output_path(std::string_view path) noexcept
    {
        if (path.size() >= 4
            && (path.substr(path.size() - 4) == ".xmp"
                || path.substr(path.size() - 4) == ".jpg"
                || path.substr(path.size() - 4) == ".bin")) {
            return true;
        }
        if (path.find('/') != std::string_view::npos
            || path.find('\\') != std::string_view::npos) {
            return true;
        }
        return false;
    }

    static bool has_known_output_extension(std::string_view path) noexcept
    {
        if (path.size() < 4) {
            return false;
        }
        const auto tail = path.substr(path.size() - 4);
        return tail == ".xmp" || tail == ".XMP" || tail == ".jpg"
               || tail == ".JPG" || tail == ".bin" || tail == ".BIN";
    }

    static void print_build_info_header()
    {
        std::string line1;
        std::string line2;
        format_build_info_lines(&line1, &line2);
        std::printf("%s\n%s\n", line1.c_str(), line2.c_str());
    }

}  // namespace
}  // namespace openmeta


int
main(int argc, char** argv)
{
    using namespace openmeta;

    bool show_build_info                     = true;
    bool xmp_sidecar                         = false;
    bool force_overwrite                     = false;
    bool extract_preview                     = false;
    bool first_only                          = false;
    bool require_jpeg_soi                    = false;
    XmpSidecarFormat format                  = XmpSidecarFormat::Lossless;
    bool portable_include_exif               = true;
    bool portable_include_existing_xmp       = false;
    bool portable_exiftool_gpsdatetime_alias = false;
    std::string out_path;
    std::string out_dir;
    std::vector<std::string> explicit_inputs;

    OpenMetaResourcePolicy policy;
    policy.max_file_bytes = 0;
    SimpleMetaDecodeOptions decode_options;
    apply_resource_policy(policy, &decode_options.exif,
                          &decode_options.payload);
    apply_resource_policy(policy, &decode_options.xmp, &decode_options.exr,
                          &decode_options.jumbf, &decode_options.icc,
                          &decode_options.iptc, &decode_options.photoshop_irb);
    ExifDecodeOptions& exif_options = decode_options.exif;
    PayloadOptions& payload_options = decode_options.payload;
    payload_options.decompress      = true;

    uint64_t max_file_bytes    = policy.max_file_bytes;
    uint64_t max_output_bytes  = 0;
    uint64_t max_preview_bytes = 128ULL * 1024ULL * 1024ULL;
    uint32_t max_preview_ifds  = policy.preview_scan_limits.max_ifds;
    uint32_t max_preview_total = policy.preview_scan_limits.max_total_entries;
    uint32_t max_entries       = 0;
    uint32_t max_candidates    = 32U;

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
        if ((std::strcmp(arg, "-i") == 0 || std::strcmp(arg, "--input") == 0)
            && i + 1 < argc) {
            explicit_inputs.emplace_back(argv[i + 1]);
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--portable") == 0) {
            format = XmpSidecarFormat::Portable;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--portable-no-exif") == 0) {
            portable_include_exif = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--portable-include-existing-xmp") == 0) {
            portable_include_existing_xmp = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--portable-exiftool-gpsdatetime-alias") == 0) {
            portable_exiftool_gpsdatetime_alias = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--format") == 0 && i + 1 < argc) {
            const char* v = argv[i + 1];
            if (!v) {
                std::fprintf(stderr, "invalid --format value\n");
                return 2;
            }
            if (std::strcmp(v, "lossless") == 0) {
                format = XmpSidecarFormat::Lossless;
            } else if (std::strcmp(v, "portable") == 0) {
                format = XmpSidecarFormat::Portable;
            } else {
                std::fprintf(
                    stderr,
                    "invalid --format value (expected lossless|portable)\n");
                return 2;
            }
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--xmp-sidecar") == 0) {
            xmp_sidecar = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-verify") == 0) {
            decode_options.jumbf.verify_c2pa = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-verify-backend") == 0 && i + 1 < argc) {
            C2paVerifyBackend backend = C2paVerifyBackend::Auto;
            if (!parse_c2pa_verify_backend_arg(argv[i + 1], &backend)) {
                std::fprintf(stderr, "invalid --c2pa-verify-backend value "
                                     "(expected none|auto|native|openssl)\n");
                return 2;
            }
            decode_options.jumbf.verify_backend = backend;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-verify-require-trusted-chain") == 0) {
            decode_options.jumbf.verify_require_trusted_chain = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-verify-require-resolved-references")
            == 0) {
            decode_options.jumbf.verify_require_resolved_references = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--force") == 0) {
            force_overwrite = true;
            first_path += 1;
            continue;
        }
        if ((std::strcmp(arg, "--out") == 0 || std::strcmp(arg, "-o") == 0)
            && i + 1 < argc) {
            out_path = argv[i + 1];
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--out-dir") == 0 && i + 1 < argc) {
            out_dir = argv[i + 1];
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--extract-preview") == 0) {
            extract_preview = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--first-only") == 0) {
            first_only = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--require-jpeg-soi") == 0) {
            require_jpeg_soi = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--no-pointer-tags") == 0) {
            exif_options.include_pointer_tags = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--makernotes") == 0) {
            exif_options.decode_makernote = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--no-decompress") == 0) {
            payload_options.decompress = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-file-bytes") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (!parse_u64_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --max-file-bytes value\n");
                return 2;
            }
            max_file_bytes = v;
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
            payload_options.limits.max_output_bytes = v;
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
            payload_options.limits.max_parts = v;
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
            exif_options.limits.max_ifds = v;
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
            exif_options.limits.max_entries_per_ifd = v;
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
            exif_options.limits.max_total_entries = v;
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
            exif_options.limits.max_value_bytes = v;
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
            decode_options.xmp.limits.max_input_bytes = v;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-output-bytes") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (!parse_u64_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --max-output-bytes value\n");
                return 2;
            }
            max_output_bytes = v;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-entries") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --max-entries value\n");
                return 2;
            }
            max_entries = v;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-preview-ifds") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-preview-ifds value\n");
                return 2;
            }
            max_preview_ifds = v;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-preview-total") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-preview-total value\n");
                return 2;
            }
            max_preview_total = v;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-preview-bytes") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (!parse_u64_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-preview-bytes value\n");
                return 2;
            }
            max_preview_bytes = v;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-candidates") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-candidates value\n");
                return 2;
            }
            max_candidates = v;
            i += 1;
            first_path += 2;
            continue;
        }
        break;
    }

    // For standard/portable outputs, treat malformed XMP as best-effort partial
    // output rather than a hard failure.
    decode_options.xmp.malformed_mode
        = (format == XmpSidecarFormat::Portable)
              ? XmpDecodeMalformedMode::OutputTruncated
              : XmpDecodeMalformedMode::Malformed;

    std::vector<std::string> input_paths = explicit_inputs;
    for (int i = first_path; i < argc; ++i) {
        if (argv[i] && argv[i][0] != '\0') {
            input_paths.emplace_back(argv[i]);
        }
    }

    if (input_paths.empty()) {
        usage(argv[0]);
        return 2;
    }

    if (input_paths.size() == 2U && out_path.empty() && out_dir.empty()
        && explicit_inputs.empty()) {
        const bool second_is_output_hint
            = has_known_output_extension(input_paths[1])
              || (!file_exists(input_paths[1])
                  && looks_like_output_path(input_paths[1]));
        if (second_is_output_hint) {
            out_path = input_paths[1];
            input_paths.resize(1U);
        }
    }

    const int file_count = static_cast<int>(input_paths.size());
    if (!out_path.empty() && file_count != 1) {
        std::fprintf(stderr,
                     "metadump: --out requires exactly one input file\n");
        return 2;
    }

    if (show_build_info) {
        print_build_info_header();
    }

    int exit_code = 0;
    for (int argi = 0; argi < static_cast<int>(input_paths.size()); ++argi) {
        const char* path = input_paths[static_cast<size_t>(argi)].c_str();
        if (!path || !*path) {
            continue;
        }
        const std::string display_path = console_path(path);

        MappedFile file;
        const MappedFileStatus st = file.open(path, max_file_bytes);
        if (st != MappedFileStatus::Ok) {
            std::fprintf(stderr, "metadump: failed to read `%s`\n",
                         display_path.c_str());
            exit_code = 1;
            continue;
        }

        if (extract_preview) {
            std::vector<ContainerBlockRef> blocks(4096U);
            std::vector<PreviewCandidate> previews(max_candidates);
            PreviewScanOptions preview_scan;
            preview_scan.require_jpeg_soi         = require_jpeg_soi;
            preview_scan.limits.max_ifds          = max_preview_ifds;
            preview_scan.limits.max_total_entries = max_preview_total;
            preview_scan.limits.max_preview_bytes = max_preview_bytes;
            const PreviewScanResult scan          = scan_preview_candidates(
                file.bytes(),
                std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
                std::span<PreviewCandidate>(previews.data(), previews.size()),
                preview_scan);

            if (scan.status == PreviewScanStatus::Unsupported) {
                std::printf("== %s\n  previews=none (unsupported)\n",
                            display_path.c_str());
                continue;
            }
            if (scan.status == PreviewScanStatus::Malformed
                || scan.status == PreviewScanStatus::LimitExceeded) {
                std::fprintf(
                    stderr,
                    "metadump: preview scan failed for `%s` (status=%u)\n",
                    display_path.c_str(), static_cast<unsigned>(scan.status));
                exit_code = 1;
                continue;
            }

            const uint32_t avail = (scan.written < previews.size())
                                       ? scan.written
                                       : static_cast<uint32_t>(previews.size());
            std::printf("== %s\n", display_path.c_str());
            std::printf("  preview_scan=%u written=%u needed=%u\n",
                        static_cast<unsigned>(scan.status), scan.written,
                        scan.needed);
            if (avail == 0U) {
                std::printf("  exported=0\n");
                continue;
            }

            uint32_t exported = 0U;
            for (uint32_t pi = 0; pi < avail; ++pi) {
                const PreviewCandidate& candidate = previews[pi];
                if (candidate.size > static_cast<uint64_t>(
                        std::numeric_limits<size_t>::max())) {
                    std::fprintf(stderr,
                                 "metadump: preview too large in `%s`\n",
                                 display_path.c_str());
                    exit_code = 1;
                    continue;
                }

                std::vector<std::byte> out_preview(
                    static_cast<size_t>(candidate.size));
                PreviewExtractOptions extract_options;
                extract_options.max_output_bytes = max_preview_bytes;
                extract_options.require_jpeg_soi = require_jpeg_soi;
                const PreviewExtractResult extracted = extract_preview_candidate(
                    file.bytes(), candidate,
                    std::span<std::byte>(out_preview.data(), out_preview.size()),
                    extract_options);
                if (extracted.status != PreviewExtractStatus::Ok) {
                    std::fprintf(
                        stderr,
                        "metadump: preview extract failed for `%s` (status=%u)\n",
                        display_path.c_str(),
                        static_cast<unsigned>(extracted.status));
                    exit_code = 1;
                    continue;
                }

                const std::string out_preview_path
                    = out_path.empty() ? default_preview_out_path_for(
                                             path, out_dir, pi,
                                             candidate.has_jpeg_soi_signature)
                      : (avail > 1U && !first_only)
                          ? with_index_suffix(out_path, pi + 1U)
                          : out_path;

                if (!force_overwrite && file_exists(out_preview_path)) {
                    const std::string display_out = console_path(
                        out_preview_path);
                    std::fprintf(
                        stderr,
                        "metadump: refusing to overwrite `%s` (use --force)\n",
                        display_out.c_str());
                    exit_code = 1;
                    continue;
                }

                if (!write_file_bytes(
                        out_preview_path,
                        std::span<const std::byte>(out_preview.data(),
                                                   static_cast<size_t>(
                                                       extracted.written)))) {
                    const std::string display_out = console_path(
                        out_preview_path);
                    std::fprintf(stderr, "metadump: failed to write `%s`\n",
                                 display_out.c_str());
                    exit_code = 1;
                    continue;
                }

                const std::string display_out = console_path(out_preview_path);
                std::printf("  [%u] wrote=%s kind=%u bytes=%llu\n", pi,
                            display_out.c_str(),
                            static_cast<unsigned>(candidate.kind),
                            static_cast<unsigned long long>(extracted.written));
                exported += 1U;
                if (first_only) {
                    break;
                }
            }
            std::printf("  exported=%u\n", exported);
            continue;
        }

        const std::string out = out_path.empty()
                                    ? default_out_path_for(path, out_dir)
                                    : out_path;
        const std::string display_out = console_path(out);

        if (!force_overwrite && file_exists(out)) {
            std::fprintf(stderr,
                         "metadump: refusing to overwrite `%s` (use --force)\n",
                         display_out.c_str());
            exit_code = 1;
            continue;
        }

        std::vector<ContainerBlockRef> blocks(128);
        std::vector<ExifIfdRef> ifd_refs(256);
        std::vector<std::byte> payload(1024 * 1024);
        std::vector<uint32_t> payload_parts(16384);

        MetaStore store;
        SimpleMetaResult read;
        for (;;) {
            store = MetaStore();
            read  = simple_meta_read(
                file.bytes(), store,
                std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
                std::span<ExifIfdRef>(ifd_refs.data(), ifd_refs.size()),
                std::span<std::byte>(payload.data(), payload.size()),
                std::span<uint32_t>(payload_parts.data(), payload_parts.size()),
                decode_options);

            if (read.scan.status == ScanStatus::OutputTruncated
                && read.scan.needed > blocks.size()) {
                blocks.resize(read.scan.needed);
                continue;
            }
            if (read.payload.status == PayloadStatus::OutputTruncated
                && read.payload.needed > payload.size()) {
                payload.resize(static_cast<size_t>(read.payload.needed));
                continue;
            }
            break;
        }

        if (xmp_sidecar) {
            std::string sidecar_a;
            std::string sidecar_b;
            xmp_sidecar_candidates(path, &sidecar_a, &sidecar_b);
            const char* candidates[2]
                = { sidecar_a.c_str(),
                    sidecar_b.empty() ? nullptr : sidecar_b.c_str() };
            for (size_t i = 0; i < 2; ++i) {
                const char* sp = candidates[i];
                if (!sp || !*sp) {
                    continue;
                }
                std::vector<std::byte> xmp_bytes;
                if (!read_file_bytes(sp, &xmp_bytes, max_file_bytes)) {
                    continue;
                }
                (void)decode_xmp_packet(xmp_bytes, store, EntryFlags::None,
                                        decode_options.xmp);
            }
        }

        store.finalize();

        XmpSidecarRequest dump_request;
        dump_request.format                  = format;
        dump_request.initial_output_bytes    = 1024ULL * 1024ULL;
        dump_request.limits.max_output_bytes = max_output_bytes;
        dump_request.limits.max_entries      = max_entries;
        dump_request.include_exif            = portable_include_exif;
        dump_request.include_existing_xmp    = portable_include_existing_xmp;
        dump_request.portable_exiftool_gpsdatetime_alias
            = portable_exiftool_gpsdatetime_alias;

        std::vector<std::byte> out_buf;
        const XmpDumpResult dump_res = dump_xmp_sidecar(store, &out_buf,
                                                        dump_request);

        if (dump_res.status != XmpDumpStatus::Ok) {
            std::fprintf(stderr, "metadump: dump failed for `%s` (status=%s)\n",
                         display_path.c_str(),
                         (dump_res.status == XmpDumpStatus::LimitExceeded)
                             ? "limit_exceeded"
                             : "output_truncated");
            exit_code = 1;
            continue;
        }

        if (!write_file_bytes(out, std::span<const std::byte>(out_buf.data(),
                                                              out_buf.size()))) {
            std::fprintf(stderr, "metadump: failed to write `%s`\n",
                         display_out.c_str());
            exit_code = 1;
            continue;
        }

        std::printf(
            "wrote=%s format=%s bytes=%llu entries=%u c2pa_verify=%s "
            "c2pa_backend=%s c2pa_require_trusted_chain=%s "
            "c2pa_require_resolved_refs=%s\n",
            display_out.c_str(),
            (format == XmpSidecarFormat::Portable) ? "portable" : "lossless",
            static_cast<unsigned long long>(dump_res.written),
            static_cast<unsigned>(dump_res.entries),
            c2pa_verify_status_name(read.jumbf.verify_status),
            c2pa_verify_backend_name(read.jumbf.verify_backend_selected),
            decode_options.jumbf.verify_require_trusted_chain ? "on" : "off",
            decode_options.jumbf.verify_require_resolved_references ? "on"
                                                                    : "off");
    }

    return exit_code;
}
