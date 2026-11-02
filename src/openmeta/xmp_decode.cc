// SPDX-License-Identifier: Apache-2.0

#include "openmeta/xmp_decode.h"

#include "xmp_structure_internal.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT
#    include <expat.h>
#endif

namespace openmeta {
namespace {

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT

#    if defined(XML_MAJOR_VERSION)  \
        && ((XML_MAJOR_VERSION > 2) \
            || (XML_MAJOR_VERSION == 2 && XML_MINOR_VERSION >= 6))
#        define OPENMETA_EXPAT_HAS_REPARSE_DEFERRAL 1
#    else
#        define OPENMETA_EXPAT_HAS_REPARSE_DEFERRAL 0
#    endif

    static constexpr std::string_view kRdfNs
        = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    static constexpr std::string_view kXmlNs
        = "http://www.w3.org/XML/1998/namespace";
    static constexpr std::string_view kXmpMetaNs = "adobe:ns:meta/";
    static constexpr std::string_view kXmpNsXmp = "http://ns.adobe.com/xap/1.0/";
    static constexpr std::string_view kXmpNsTiff
        = "http://ns.adobe.com/tiff/1.0/";
    static constexpr std::string_view kXmpNsExif
        = "http://ns.adobe.com/exif/1.0/";
    static constexpr std::string_view kXmpNsExifAux
        = "http://ns.adobe.com/exif/1.0/aux/";
    static constexpr std::string_view kXmpNsDc
        = "http://purl.org/dc/elements/1.1/";
    static constexpr std::string_view kXmpNsPdf = "http://ns.adobe.com/pdf/1.3/";
    static constexpr std::string_view kXmpNsXmpBJ
        = "http://ns.adobe.com/xap/1.0/bj/";
    static constexpr std::string_view kXmpNsPlus
        = "http://ns.useplus.org/ldf/xmp/1.0/";
    static constexpr std::string_view kXmpNsCrs
        = "http://ns.adobe.com/camera-raw-settings/1.0/";
    static constexpr std::string_view kXmpNsLr
        = "http://ns.adobe.com/lightroom/1.0/";
    static constexpr std::string_view kXmpNsXmpDM
        = "http://ns.adobe.com/xmp/1.0/DynamicMedia/";
    static constexpr std::string_view kXmpNsXmpMM
        = "http://ns.adobe.com/xap/1.0/mm/";
    static constexpr std::string_view kXmpNsXmpTPg
        = "http://ns.adobe.com/xap/1.0/t/pg/";
    static constexpr std::string_view kXmpNsXmpRights
        = "http://ns.adobe.com/xap/1.0/rights/";
    static constexpr std::string_view kXmpNsStDim
        = "http://ns.adobe.com/xap/1.0/sType/Dimensions#";
    static constexpr std::string_view kXmpNsStEvt
        = "http://ns.adobe.com/xap/1.0/sType/ResourceEvent#";
    static constexpr std::string_view kXmpNsStFnt
        = "http://ns.adobe.com/xap/1.0/sType/Font#";
    static constexpr std::string_view kXmpNsStJob
        = "http://ns.adobe.com/xap/1.0/sType/Job#";
    static constexpr std::string_view kXmpNsStMfs
        = "http://ns.adobe.com/xap/1.0/sType/ManifestItem#";
    static constexpr std::string_view kXmpNsStRef
        = "http://ns.adobe.com/xap/1.0/sType/ResourceRef#";
    static constexpr std::string_view kXmpNsStVer
        = "http://ns.adobe.com/xap/1.0/sType/Version#";
    static constexpr std::string_view kXmpNsXmpG
        = "http://ns.adobe.com/xap/1.0/g/";
    static constexpr std::string_view kXmpNsPhotoshop
        = "http://ns.adobe.com/photoshop/1.0/";
    static constexpr std::string_view kXmpNsIptc4xmpCore
        = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
    static constexpr std::string_view kXmpNsIptc4xmpExt
        = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
    static constexpr uint32_t kDecodeStructureMaxNodes        = 4096U;
    static constexpr uint32_t kDecodeStructureMaxNamespaces   = 64U;
    static constexpr uint32_t kDecodeStructureMaxPathBytes    = 4096U;
    static constexpr uint32_t kDecodeStructureMaxUriBytes     = 4096U;
    static constexpr uint8_t kDecodeStructureMaxDepth         = 8U;
    static constexpr uint32_t kDecodeStructureMaxArrayIndex   = 65535U;
    static constexpr uint64_t kDecodeStructureMaxRequestBytes = 16ULL * 1024ULL
                                                                * 1024ULL;

    struct NameParts final {
        std::string_view uri;
        std::string_view local;
    };

    static NameParts split_name(std::string_view name) noexcept
    {
        const size_t sep = name.find('|');
        if (sep == std::string_view::npos) {
            return NameParts { std::string_view {}, name };
        }
        return NameParts { name.substr(0, sep), name.substr(sep + 1) };
    }

    static std::string_view
    portable_nested_prefix_for_xmp_ns(std::string_view ns) noexcept
    {
        if (ns == kXmpNsXmp) {
            return "xmp";
        }
        if (ns == kXmpNsTiff) {
            return "tiff";
        }
        if (ns == kXmpNsExif) {
            return "exif";
        }
        if (ns == kXmpNsExifAux) {
            return "aux";
        }
        if (ns == kXmpNsDc) {
            return "dc";
        }
        if (ns == kXmpNsPdf) {
            return "pdf";
        }
        if (ns == kXmpNsXmpBJ) {
            return "xmpBJ";
        }
        if (ns == kXmpNsPlus) {
            return "plus";
        }
        if (ns == kXmpNsCrs) {
            return "crs";
        }
        if (ns == kXmpNsLr) {
            return "lr";
        }
        if (ns == kXmpNsXmpDM) {
            return "xmpDM";
        }
        if (ns == kXmpNsXmpMM) {
            return "xmpMM";
        }
        if (ns == kXmpNsXmpTPg) {
            return "xmpTPg";
        }
        if (ns == kXmpNsXmpRights) {
            return "xmpRights";
        }
        if (ns == kXmpNsStDim) {
            return "stDim";
        }
        if (ns == kXmpNsStEvt) {
            return "stEvt";
        }
        if (ns == kXmpNsStFnt) {
            return "stFnt";
        }
        if (ns == kXmpNsStJob) {
            return "stJob";
        }
        if (ns == kXmpNsStMfs) {
            return "stMfs";
        }
        if (ns == kXmpNsStRef) {
            return "stRef";
        }
        if (ns == kXmpNsStVer) {
            return "stVer";
        }
        if (ns == kXmpNsXmpG) {
            return "xmpG";
        }
        if (ns == kXmpNsPhotoshop) {
            return "photoshop";
        }
        if (ns == kXmpNsIptc4xmpCore) {
            return "Iptc4xmpCore";
        }
        if (ns == kXmpNsIptc4xmpExt) {
            return "Iptc4xmpExt";
        }
        return {};
    }


    static bool is_ascii_ws(char c) noexcept
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }


    static std::string_view trim_ascii_ws(std::string_view s) noexcept
    {
        size_t b = 0;
        while (b < s.size() && is_ascii_ws(s[b])) {
            b += 1;
        }
        size_t e = s.size();
        while (e > b && is_ascii_ws(s[e - 1])) {
            e -= 1;
        }
        return s.substr(b, e - b);
    }

    static std::string_view property_text_value(std::string_view ns,
                                                std::string_view path,
                                                std::string_view value) noexcept
    {
        if (ns == kXmpNsExif
            && (path == "ExifVersion" || path == "FlashpixVersion"
                || path == "UserComment"
                || path.starts_with("UserComment[@xml:lang=")))
            return value;
        if (ns == "http://cipa.jp/exif/1.0/"
            && (path == "ImageTitle" || path == "Photographer"
                || path == "ImageEditor" || path == "CameraFirmware"
                || path == "RAWDevelopingSoftware"
                || path == "ImageEditingSoftware"
                || path == "MetadataEditingSoftware"))
            return value;
        if (ns == kXmpNsExif) {
            constexpr std::array<std::string_view, 3> roots
                = { "OECF/", "SpatialFrequencyResponse/",
                    "DeviceSettingDescription/" };
            for (const std::string_view root : roots) {
                if (!path.starts_with(root))
                    continue;
                std::string_view tail = path.substr(root.size());
                if (tail.starts_with("exif:"))
                    tail.remove_prefix(5U);
                const std::string_view member = root == roots[2] ? "Values["
                                                                 : "Names[";
                if (tail.starts_with(member) && tail.back() == ']')
                    return value;
            }
        }
        if ((ns == kXmpNsExif
             && (path == "SpectralSensitivity" || path == "ImageUniqueID"))
            || ((ns == kXmpNsExif || ns == "http://cipa.jp/exif/1.0/")
                && (path == "CameraOwnerName" || path == "BodySerialNumber"
                    || path == "LensMake" || path == "LensModel"
                    || path == "LensSerialNumber")))
            return value;
        return trim_ascii_ws(value);
    }

    static bool is_ascii_ws_or_nul(char c) noexcept
    {
        return c == '\0' || is_ascii_ws(c);
    }


    static size_t find_xmpmeta_close_end(std::string_view s) noexcept
    {
        size_t search = 0;
        while (search < s.size()) {
            const size_t open = s.find("</", search);
            if (open == std::string_view::npos) {
                return std::string_view::npos;
            }
            const size_t name_begin = open + 2U;
            const size_t gt         = s.find('>', name_begin);
            if (gt == std::string_view::npos) {
                return std::string_view::npos;
            }

            const std::string_view qname = s.substr(name_begin,
                                                    gt - name_begin);
            const size_t colon           = qname.rfind(':');
            const std::string_view local = (colon == std::string_view::npos)
                                               ? qname
                                               : qname.substr(colon + 1U);
            if (local == "xmpmeta") {
                return gt + 1U;
            }
            search = gt + 1U;
        }
        return std::string_view::npos;
    }


    static std::span<const std::byte>
    normalize_xmp_packet(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.empty()) {
            return {};
        }

        const std::string_view s(reinterpret_cast<const char*>(bytes.data()),
                                 bytes.size());

        const size_t begin = s.find('<');
        if (begin == std::string_view::npos) {
            return {};
        }

        size_t end = s.size();

        // Prefer cutting at a known close tag to avoid trailing binary/padding
        // from container-specific storage (e.g. ISO-BMFF `mime` items).
        const std::string_view tail = s.substr(begin);
        {
            static constexpr std::string_view kCloseRdf = "</rdf:RDF>";

            size_t pos = find_xmpmeta_close_end(tail);
            if (pos != std::string_view::npos) {
                end = begin + pos;
            } else {
                pos = tail.find(kCloseRdf);
                if (pos != std::string_view::npos) {
                    end = begin + pos + kCloseRdf.size();
                } else {
                    const size_t last_gt = s.rfind('>');
                    if (last_gt == std::string_view::npos || last_gt < begin) {
                        return {};
                    }
                    end = last_gt + 1;
                }
            }
        }

        while (end > begin && is_ascii_ws_or_nul(s[end - 1])) {
            end -= 1;
        }

        if (end <= begin) {
            return {};
        }

        return bytes.subspan(begin, end - begin);
    }


    static void merge_status(XmpDecodeResult* out, XmpDecodeStatus in) noexcept
    {
        if (!out || in == XmpDecodeStatus::Ok) {
            return;
        }
        if (out->status == XmpDecodeStatus::LimitExceeded) {
            return;
        }
        if (in == XmpDecodeStatus::LimitExceeded) {
            out->status = in;
            return;
        }
        if (out->status == XmpDecodeStatus::Malformed) {
            return;
        }
        if (in == XmpDecodeStatus::Malformed) {
            out->status = in;
            return;
        }
        if (out->status == XmpDecodeStatus::OutputTruncated) {
            return;
        }
        if (in == XmpDecodeStatus::OutputTruncated) {
            out->status = in;
            return;
        }
        if (out->status == XmpDecodeStatus::Unsupported) {
            return;
        }
        if (in == XmpDecodeStatus::Unsupported) {
            out->status = in;
            return;
        }
        if (out->status == XmpDecodeStatus::Ok) {
            out->status = in;
            return;
        }
    }


    struct Frame final {
        bool is_rdf                        = false;
        bool is_description                = false;
        bool is_array_container            = false;
        bool is_alt_container              = false;
        bool is_li                         = false;
        bool is_nonrdf                     = false;
        bool contributed_to_path           = false;
        bool had_child_element             = false;
        bool emitted_resource_val          = false;
        bool text_truncated                = false;
        bool structure_declared            = false;
        bool nonwhite_text                 = false;
        bool text_exceeds_structure_budget = false;
        bool compressed_whitespace         = false;
        uint32_t whitespace_count          = 0U;
        std::vector<uint8_t> whitespace_bits;
        uint32_t path_len_after         = 0U;
        XmpContainerKind structure_kind = XmpContainerKind::None;
        uint32_t path_len_before        = 0;
        uint32_t li_counter             = 0;  // used for Seq/Bag/Alt
        std::string text;
    };

    struct OwnedStructureNamespace final {
        std::string uri;
        uint64_t hash = 0U;
    };

    struct Ctx final {
        MetaStore* store = nullptr;
        BlockId block    = kInvalidBlockId;
        EntryFlags flags = EntryFlags::None;
        XmpDecodeOptions options;
        XmpDecodeResult result;

        XML_Parser parser = nullptr;

        uint32_t description_depth = 0;

        uint64_t total_value_bytes = 0;
        uint32_t order_in_block    = 0;

        std::string path;
        std::string root_schema_ns;
        bool root_schema_is_custom       = false;
        uint32_t structure_nodes         = 0U;
        uint64_t structure_request_bytes = 0U;
        std::vector<OwnedStructureNamespace> structure_namespaces;

        std::vector<Frame> stack;
    };

    static bool should_stop(const Ctx* ctx) noexcept
    {
        return !ctx || !ctx->parser
               || ctx->result.status == XmpDecodeStatus::LimitExceeded
               || ctx->result.status == XmpDecodeStatus::Malformed
               || ctx->result.status == XmpDecodeStatus::Unsupported;
    }

    static bool custom_structure_scope(const Ctx* ctx) noexcept;
    static uint32_t custom_structure_namespace_limit(const Ctx* ctx) noexcept;
    static uint32_t custom_structure_path_limit(const Ctx* ctx) noexcept;
    static bool register_structure_namespace(Ctx* ctx,
                                             std::string_view uri) noexcept;


    static void stop_parser(Ctx* ctx, XmpDecodeStatus status) noexcept
    {
        if (!ctx) {
            return;
        }
        merge_status(&ctx->result, status);
        if (ctx->parser) {
            XML_StopParser(ctx->parser, XML_FALSE);
        }
    }

    static bool assign_unknown_nested_segment(Ctx* ctx,
                                              std::string_view namespace_uri,
                                              std::string_view local,
                                              std::string* out) noexcept
    {
        if (!ctx || !out) {
            return false;
        }

        if (custom_structure_scope(ctx)) {
            if (namespace_uri.size() > custom_structure_namespace_limit(ctx)) {
                stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
                return false;
            }
            if (!register_structure_namespace(ctx, namespace_uri))
                return false;
        }

        constexpr char kHex[]           = "0123456789abcdef";
        constexpr uint64_t kMarkerBytes = 4U;
        const uint64_t uri_bytes = static_cast<uint64_t>(namespace_uri.size());
        const uint64_t local_bytes = static_cast<uint64_t>(local.size());
        if (local_bytes > UINT64_MAX - kMarkerBytes - 1U) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        const uint64_t fixed_bytes = kMarkerBytes + 1U + local_bytes;
        if (uri_bytes > (UINT64_MAX - fixed_bytes) / 2U) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        const uint64_t segment_bytes = fixed_bytes + uri_bytes * 2U;
        uint64_t path_bytes          = static_cast<uint64_t>(ctx->path.size());
        if (!ctx->path.empty()) {
            if (path_bytes == UINT64_MAX) {
                stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
                return false;
            }
            path_bytes += 1U;
        }
        const uint32_t max_path = custom_structure_scope(ctx)
                                      ? custom_structure_path_limit(ctx)
                                      : ctx->options.limits.max_path_bytes;
        if (segment_bytes > static_cast<uint64_t>(SIZE_MAX)
            || path_bytes > UINT64_MAX - segment_bytes
            || (max_path != 0U
                && path_bytes + segment_bytes
                       > static_cast<uint64_t>(max_path))) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }

        out->clear();
        out->reserve(static_cast<size_t>(segment_bytes));
        out->assign("nsu_");
        for (size_t i = 0; i < namespace_uri.size(); ++i) {
            const uint8_t c = static_cast<uint8_t>(namespace_uri[i]);
            out->push_back(kHex[(c >> 4U) & 0x0FU]);
            out->push_back(kHex[c & 0x0FU]);
        }
        out->push_back(':');
        out->append(local.data(), local.size());
        return true;
    }


    static void XMLCALL start_doctype_decl(void* user_data,
                                           const XML_Char* /*doctype_name*/,
                                           const XML_Char* /*sysid*/,
                                           const XML_Char* /*pubid*/,
                                           int /*has_internal_subset*/)
    {
        Ctx* ctx = reinterpret_cast<Ctx*>(user_data);
        stop_parser(ctx, XmpDecodeStatus::Malformed);
    }


    static void XMLCALL
    entity_decl(void* user_data, const XML_Char* /*entity_name*/,
                int /*is_parameter_entity*/, const XML_Char* /*value*/,
                int /*value_length*/, const XML_Char* /*base*/,
                const XML_Char* /*system_id*/, const XML_Char* /*public_id*/,
                const XML_Char* /*notation_name*/)
    {
        Ctx* ctx = reinterpret_cast<Ctx*>(user_data);
        stop_parser(ctx, XmpDecodeStatus::Malformed);
    }


    static int XMLCALL external_entity_ref_handler(
        XML_Parser parser, const XML_Char* /*context*/,
        const XML_Char* /*base*/, const XML_Char* /*system_id*/,
        const XML_Char* /*public_id*/)
    {
        Ctx* ctx = reinterpret_cast<Ctx*>(XML_GetUserData(parser));
        stop_parser(ctx, XmpDecodeStatus::Malformed);
        return XML_STATUS_ERROR;
    }


    static bool path_append_segment(Ctx* ctx, std::string_view seg,
                                    bool use_slash) noexcept
    {
        if (!ctx) {
            return false;
        }
        if (seg.empty()) {
            return true;
        }

        const uint32_t max_path = custom_structure_scope(ctx)
                                      ? custom_structure_path_limit(ctx)
                                      : ctx->options.limits.max_path_bytes;
        uint64_t needed         = ctx->path.size();
        if (use_slash && !ctx->path.empty()) {
            needed += 1;
        }
        needed += seg.size();
        if (max_path != 0U && needed > max_path) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }

        if (use_slash && !ctx->path.empty()) {
            ctx->path.push_back('/');
        }
        ctx->path.append(seg.data(), seg.size());
        return true;
    }


    static bool path_append_index(Ctx* ctx, uint32_t index) noexcept
    {
        if (!ctx) {
            return false;
        }

        char buf[32];
        std::snprintf(buf, sizeof(buf), "[%u]", static_cast<unsigned>(index));
        const std::string_view s(buf, std::strlen(buf));

        const uint32_t max_path = custom_structure_scope(ctx)
                                      ? custom_structure_path_limit(ctx)
                                      : ctx->options.limits.max_path_bytes;
        const uint64_t needed   = static_cast<uint64_t>(ctx->path.size())
                                + static_cast<uint64_t>(s.size());
        if (max_path != 0U && needed > max_path) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        ctx->path.append(s.data(), s.size());
        return true;
    }

    static bool path_append_lang_alt(Ctx* ctx, std::string_view lang) noexcept
    {
        if (!ctx || lang.empty()) {
            return false;
        }

        static constexpr std::string_view kPrefix = "[@xml:lang=";
        const uint32_t max_path                   = custom_structure_scope(ctx)
                                                        ? custom_structure_path_limit(ctx)
                                                        : ctx->options.limits.max_path_bytes;
        uint64_t needed = static_cast<uint64_t>(ctx->path.size())
                          + static_cast<uint64_t>(kPrefix.size())
                          + static_cast<uint64_t>(lang.size()) + 1U;
        if (max_path != 0U && needed > max_path) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }

        for (size_t i = 0; i < lang.size(); ++i) {
            const char c  = lang[i];
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                            || (c >= '0' && c <= '9') || c == '-';
            if (!ok) {
                return false;
            }
        }

        ctx->path.append(kPrefix.data(), kPrefix.size());
        ctx->path.append(lang.data(), lang.size());
        ctx->path.push_back(']');
        return true;
    }

    static bool find_xml_lang_attr(const XML_Char** atts,
                                   std::string_view* out_lang) noexcept
    {
        if (!atts || !out_lang) {
            return false;
        }
        *out_lang = {};
        for (int i = 0; atts[i] && atts[i + 1]; i += 2) {
            const std::string_view an(atts[i], std::strlen(atts[i]));
            const NameParts ap = split_name(an);
            if (ap.uri != kXmlNs || ap.local != "lang") {
                continue;
            }
            *out_lang = trim_ascii_ws(
                std::string_view(atts[i + 1], std::strlen(atts[i + 1])));
            return !out_lang->empty();
        }
        return false;
    }


    static Frame* find_nearest_array_container(Ctx* ctx) noexcept
    {
        if (!ctx) {
            return nullptr;
        }
        for (size_t i = ctx->stack.size(); i > 0; --i) {
            Frame& f = ctx->stack[i - 1];
            if (f.is_array_container) {
                return &f;
            }
        }
        return nullptr;
    }


    static bool custom_structure_scope(const Ctx* ctx) noexcept
    {
        return ctx && ctx->options.preserve_custom_structures
               && ctx->root_schema_is_custom;
    }


    static uint32_t custom_structure_namespace_limit(const Ctx* ctx) noexcept
    {
        if (!ctx)
            return 0U;
        return std::min(ctx->options.limits.max_namespace_bytes,
                        kDecodeStructureMaxUriBytes);
    }


    static uint32_t custom_structure_path_limit(const Ctx* ctx) noexcept
    {
        if (!ctx)
            return 0U;
        if (ctx->options.limits.max_path_bytes == 0U)
            return kDecodeStructureMaxPathBytes;
        return std::min(ctx->options.limits.max_path_bytes,
                        kDecodeStructureMaxPathBytes);
    }


    static uint64_t structure_namespace_hash(std::string_view uri) noexcept
    {
        uint64_t hash = 14695981039346656037ULL;
        for (const char value : uri) {
            const auto c = static_cast<unsigned char>(value);
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }


    static bool structure_namespace_registered(const Ctx* ctx,
                                               std::string_view uri) noexcept
    {
        if (!ctx || uri.empty())
            return false;
        const uint64_t hash = structure_namespace_hash(uri);
        for (const OwnedStructureNamespace& existing :
             ctx->structure_namespaces) {
            if (existing.hash == hash && existing.uri.size() == uri.size()
                && existing.uri == uri) {
                return true;
            }
        }
        return false;
    }


    static bool add_structure_namespace(Ctx* ctx, std::string_view uri) noexcept
    {
        if (!ctx || !custom_structure_scope(ctx) || uri.empty())
            return false;
        const uint32_t max_uri = custom_structure_namespace_limit(ctx);
        if (uri.size() > max_uri) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        if (structure_namespace_registered(ctx, uri))
            return true;
        if (ctx->structure_namespaces.size() >= kDecodeStructureMaxNamespaces) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        OwnedStructureNamespace name_space;
        name_space.uri.assign(uri.data(), uri.size());
        name_space.hash = structure_namespace_hash(uri);
        ctx->structure_namespaces.push_back(std::move(name_space));
        return true;
    }


    static bool register_structure_namespace(Ctx* ctx,
                                             std::string_view uri) noexcept
    {
        if (!ctx || !custom_structure_scope(ctx) || uri.empty())
            return false;
        if (uri.size() > custom_structure_namespace_limit(ctx)) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        if (structure_namespace_registered(ctx, uri))
            return true;
        if (ctx->structure_namespaces.size() >= kDecodeStructureMaxNamespaces) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        if (!detail::xmp_structure_is_standard_namespace(uri)
            && !detail::xmp_structure_namespace_is_custom(uri)) {
            stop_parser(ctx, XmpDecodeStatus::Unsupported);
            return false;
        }
        return add_structure_namespace(ctx, uri);
    }


    static bool reserve_custom_structure_record(Ctx* ctx, std::string_view path,
                                                uint64_t text_bytes,
                                                uint64_t* charged_bytes) noexcept
    {
        if (charged_bytes)
            *charged_bytes = 0U;
        if (!ctx || !custom_structure_scope(ctx))
            return true;
        if (ctx->structure_nodes >= kDecodeStructureMaxNodes) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }

        const uint32_t max_path = custom_structure_path_limit(ctx);
        const uint32_t max_uri  = custom_structure_namespace_limit(ctx);
        if (path.empty() || path.size() > max_path
            || ctx->root_schema_ns.size() > max_uri) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }

        uint64_t cost = static_cast<uint64_t>(ctx->root_schema_ns.size());
        if (path.size() > UINT64_MAX - cost) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        cost += static_cast<uint64_t>(path.size());
        if (text_bytes > UINT64_MAX - cost) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        cost += text_bytes;
        if (cost > kDecodeStructureMaxRequestBytes
            || ctx->structure_request_bytes
                   > kDecodeStructureMaxRequestBytes - cost) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }

        detail::XmpStructurePath parsed;
        if (!detail::xmp_structure_parse_path(path, kDecodeStructureMaxDepth,
                                              kDecodeStructureMaxArrayIndex,
                                              &parsed)) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        const uint32_t max_depth
            = std::min<uint32_t>(ctx->options.limits.max_depth,
                                 kDecodeStructureMaxDepth);
        if (parsed.expanded_depth > max_depth) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }

        char uri_storage[kDecodeStructureMaxUriBytes + 1U];
        for (uint8_t i = 0U; i < parsed.depth; ++i) {
            const detail::XmpStructureNamespaceRef& ref
                = parsed.segments[i].name_space;
            if (ref.uri.empty())
                continue;
            std::string_view decoded_uri;
            if (!detail::xmp_structure_namespace_uri(ref, uri_storage,
                                                     sizeof(uri_storage),
                                                     &decoded_uri)
                || !register_structure_namespace(ctx, decoded_uri)) {
                if (ctx->result.status == XmpDecodeStatus::Ok)
                    stop_parser(ctx, XmpDecodeStatus::Unsupported);
                return false;
            }
        }
        if (charged_bytes)
            *charged_bytes = cost;
        return true;
    }


    static void commit_custom_structure_record(Ctx* ctx,
                                               uint64_t charged_bytes) noexcept
    {
        if (!ctx || !custom_structure_scope(ctx))
            return;
        ++ctx->structure_nodes;
        ctx->structure_request_bytes += charged_bytes;
    }


    static bool xml_whitespace_only(std::string_view text) noexcept
    {
        for (const char c : text) {
            if (!is_ascii_ws(c))
                return false;
        }
        return true;
    }


    static uint64_t frame_text_size(const Frame& frame) noexcept
    {
        return static_cast<uint64_t>(frame.text.size())
               + static_cast<uint64_t>(frame.whitespace_count);
    }


    static void append_packed_whitespace(Frame* frame, std::string_view text)
    {
        if (!frame)
            return;
        frame->compressed_whitespace = true;
        for (const char c : text) {
            const uint8_t code  = c == ' '    ? 0U
                                  : c == '\t' ? 1U
                                  : c == '\n' ? 2U
                                              : 3U;
            const uint32_t slot = frame->whitespace_count % 4U;
            if (slot == 0U)
                frame->whitespace_bits.push_back(0U);
            frame->whitespace_bits.back() |= static_cast<uint8_t>(
                code << (slot * 2U));
            ++frame->whitespace_count;
        }
    }


    static bool materialize_frame_text(Frame* frame)
    {
        if (!frame || !frame->compressed_whitespace)
            return true;
        std::string decoded;
        decoded.reserve(static_cast<size_t>(frame_text_size(*frame)));
        static constexpr char kWhitespace[4] = { ' ', '\t', '\n', '\r' };
        for (uint32_t i = 0U; i < frame->whitespace_count; ++i) {
            const uint32_t slot = i % 4U;
            const uint8_t code  = static_cast<uint8_t>(
                (frame->whitespace_bits[i / 4U] >> (slot * 2U)) & 0x03U);
            decoded.push_back(kWhitespace[code]);
        }
        decoded.append(frame->text);
        frame->text.swap(decoded);
        std::vector<uint8_t> empty;
        frame->whitespace_bits.swap(empty);
        frame->whitespace_count      = 0U;
        frame->compressed_whitespace = false;
        return true;
    }


    static void discard_frame_text(Frame* frame) noexcept
    {
        if (!frame)
            return;
        std::string empty_text;
        std::vector<uint8_t> empty_bits;
        frame->text.swap(empty_text);
        frame->whitespace_bits.swap(empty_bits);
        frame->whitespace_count      = 0U;
        frame->compressed_whitespace = false;
    }


    static bool frame_is_custom_structure(const Ctx* ctx,
                                          const Frame& frame) noexcept
    {
        if (!custom_structure_scope(ctx))
            return false;
        return frame.structure_declared || frame.is_array_container
               || (frame.is_description && !ctx->path.empty())
               || (frame.contributed_to_path && frame.had_child_element);
    }


    static Frame* find_current_path_owner(Ctx* ctx) noexcept
    {
        if (!ctx || ctx->path.empty())
            return nullptr;
        const uint32_t path_size = static_cast<uint32_t>(ctx->path.size());
        for (size_t i = ctx->stack.size(); i > 0U; --i) {
            Frame& frame = ctx->stack[i - 1U];
            if (frame.contributed_to_path
                && frame.path_len_after == path_size) {
                return &frame;
            }
        }
        return nullptr;
    }


    static bool emit_structure_declaration(Ctx* ctx, std::string_view path,
                                           XmpContainerKind kind,
                                           Frame* owner) noexcept
    {
        if (!ctx || !ctx->store || !custom_structure_scope(ctx)
            || path.empty()) {
            return true;
        }
        if (kind < XmpContainerKind::Struct
            || kind > XmpContainerKind::LangAlt) {
            stop_parser(ctx, XmpDecodeStatus::Malformed);
            return false;
        }
        if (owner && owner->structure_declared) {
            if (owner->structure_kind != kind)
                stop_parser(ctx, XmpDecodeStatus::Malformed);
            return owner->structure_kind == kind;
        }
        if (owner) {
            if (owner->nonwhite_text) {
                stop_parser(ctx, XmpDecodeStatus::Unsupported);
                return false;
            }
            discard_frame_text(owner);
        }
        if (ctx->result.entries_decoded >= ctx->options.limits.max_properties
            || ctx->order_in_block == UINT32_MAX) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        uint64_t charged_bytes = 0U;
        if (!reserve_custom_structure_record(ctx, path, 0U, &charged_bytes)) {
            return false;
        }

        Entry entry;
        entry.key                   = make_xmp_property_key(ctx->store->arena(),
                                                            ctx->root_schema_ns, path);
        entry.origin.block          = ctx->block;
        entry.origin.order_in_block = ctx->order_in_block;
        entry.origin.wire_type
            = WireType { WireFamily::Other,
                         static_cast<uint16_t>(detail::kXmpStructureWireBase
                                               + static_cast<uint8_t>(kind)) };
        entry.origin.wire_count = 0U;
        entry.flags             = ctx->flags;
        if (ctx->store->add_entry(entry) == kInvalidEntryId) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        commit_custom_structure_record(ctx, charged_bytes);
        ++ctx->result.entries_decoded;
        ++ctx->order_in_block;
        if (owner) {
            owner->structure_declared = true;
            owner->structure_kind     = kind;
        }
        return true;
    }


    static bool mark_current_path_as_struct(Ctx* ctx) noexcept
    {
        if (!custom_structure_scope(ctx) || ctx->path.empty())
            return true;
        Frame* owner = find_current_path_owner(ctx);
        if (!owner) {
            stop_parser(ctx, XmpDecodeStatus::Malformed);
            return false;
        }
        return emit_structure_declaration(ctx, ctx->path,
                                          XmpContainerKind::Struct, owner);
    }


    static bool
    custom_property_attributes_supported(Ctx* ctx, const XML_Char** atts,
                                         bool* parse_type_resource) noexcept
    {
        if (!parse_type_resource)
            return false;
        *parse_type_resource = false;
        if (!custom_structure_scope(ctx) || !atts)
            return true;
        for (int i = 0; atts[i] && atts[i + 1]; i += 2) {
            const NameParts name = split_name(
                std::string_view(atts[i], std::strlen(atts[i])));
            const std::string_view value(atts[i + 1], std::strlen(atts[i + 1]));
            if (name.uri == kRdfNs && name.local == "parseType"
                && value == "Resource" && !*parse_type_resource) {
                *parse_type_resource = true;
                continue;
            }
            stop_parser(ctx, XmpDecodeStatus::Unsupported);
            return false;
        }
        return true;
    }


    static bool
    custom_container_attributes_supported(Ctx* ctx,
                                          const XML_Char** atts) noexcept
    {
        if (!custom_structure_scope(ctx) || !atts)
            return true;
        if (atts[0]) {
            stop_parser(ctx, XmpDecodeStatus::Unsupported);
            return false;
        }
        return true;
    }


    static bool
    custom_li_attributes_supported(Ctx* ctx, const XML_Char** atts,
                                   bool in_lang_alt, std::string_view* language,
                                   bool* parse_type_resource) noexcept
    {
        if (!language || !parse_type_resource)
            return false;
        *language            = {};
        *parse_type_resource = false;
        if (!custom_structure_scope(ctx))
            return true;
        const bool has_language = find_xml_lang_attr(atts, language);
        if (in_lang_alt && !has_language) {
            stop_parser(ctx, XmpDecodeStatus::Unsupported);
            return false;
        }
        if (!in_lang_alt && has_language) {
            stop_parser(ctx, XmpDecodeStatus::Unsupported);
            return false;
        }
        if (!atts)
            return true;
        for (int i = 0; atts[i] && atts[i + 1]; i += 2) {
            const NameParts name = split_name(
                std::string_view(atts[i], std::strlen(atts[i])));
            const std::string_view value(atts[i + 1], std::strlen(atts[i + 1]));
            if (name.uri == kXmlNs && name.local == "lang" && in_lang_alt)
                continue;
            if (name.uri == kRdfNs && name.local == "parseType"
                && value == "Resource" && !in_lang_alt
                && !*parse_type_resource) {
                *parse_type_resource = true;
                continue;
            }
            stop_parser(ctx, XmpDecodeStatus::Unsupported);
            return false;
        }
        return true;
    }


    static bool emit_property_text(Ctx* ctx, std::string_view schema_ns,
                                   std::string_view property_path,
                                   std::string_view value) noexcept
    {
        if (!ctx || !ctx->store) {
            return false;
        }
        if (schema_ns.empty() || property_path.empty()) {
            return false;
        }
        if (schema_ns.size() > ctx->options.limits.max_namespace_bytes) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        if (ctx->result.entries_decoded >= ctx->options.limits.max_properties) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }

        const uint64_t max_total = ctx->options.limits.max_total_value_bytes;
        if (max_total != 0U) {
            const uint64_t add = static_cast<uint64_t>(value.size());
            if (add > (UINT64_MAX - ctx->total_value_bytes)
                || ctx->total_value_bytes + add > max_total) {
                stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
                return false;
            }
        }

        if (custom_structure_scope(ctx)
            && (ctx->order_in_block == UINT32_MAX
                || ctx->root_schema_ns != schema_ns)) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        uint64_t charged_bytes = 0U;
        if (custom_structure_scope(ctx)
            && !reserve_custom_structure_record(ctx, property_path,
                                                value.size(), &charged_bytes)) {
            return false;
        }

        Entry entry;
        entry.key   = make_xmp_property_key(ctx->store->arena(), schema_ns,
                                            property_path);
        entry.value = make_text(ctx->store->arena(), value, TextEncoding::Utf8);
        entry.origin.block          = ctx->block;
        entry.origin.order_in_block = ctx->order_in_block;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = static_cast<uint32_t>(value.size());
        entry.flags                 = ctx->flags;

        if (ctx->store->add_entry(entry) == kInvalidEntryId) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        commit_custom_structure_record(ctx, charged_bytes);
        ctx->result.entries_decoded += 1;
        ctx->order_in_block += 1;
        ctx->total_value_bytes += static_cast<uint64_t>(value.size());
        return true;
    }


    static void XMLCALL start_element(void* user_data, const XML_Char* name_c,
                                      const XML_Char** atts)
    {
        Ctx* ctx = reinterpret_cast<Ctx*>(user_data);
        if (should_stop(ctx) || !name_c) {
            return;
        }

        if (ctx->stack.size() >= ctx->options.limits.max_depth) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return;
        }

        if (!ctx->stack.empty()) {
            ctx->stack.back().had_child_element = true;
        }

        const std::string_view name(name_c, std::strlen(name_c));
        const NameParts parts = split_name(name);
        const bool is_rdf     = (parts.uri == kRdfNs);
        const bool is_xml     = (parts.uri == kXmlNs);
        const bool is_desc    = is_rdf && (parts.local == "Description");
        const bool is_seq     = is_rdf
                            && (parts.local == "Seq" || parts.local == "Bag"
                                || parts.local == "Alt");
        const bool is_li = is_rdf && (parts.local == "li");

        if (ctx->description_depth > 0U && !ctx->path.empty()
            && custom_structure_scope(ctx) && is_rdf && !is_desc && !is_seq
            && !is_li) {
            stop_parser(ctx, XmpDecodeStatus::Unsupported);
            return;
        }
        if (ctx->description_depth > 0U && !ctx->path.empty()
            && custom_structure_scope(ctx) && is_xml) {
            stop_parser(ctx, XmpDecodeStatus::Unsupported);
            return;
        }

        Frame frame;
        frame.is_rdf             = is_rdf;
        frame.is_description     = is_desc;
        frame.is_array_container = is_seq;
        frame.is_alt_container   = is_rdf && (parts.local == "Alt");
        frame.is_li              = is_li;
        frame.is_nonrdf          = (!is_rdf && !is_xml);
        frame.path_len_before    = static_cast<uint32_t>(ctx->path.size());

        // ExifTool exposes the `x:xmptk` attribute on the `<x:xmpmeta>` root as
        // XMP-x:XMPToolkit. This is outside `rdf:RDF`, but is still useful
        // metadata, and some files contain only this attribute.
        if (parts.uri == kXmpMetaNs && parts.local == "xmpmeta" && atts) {
            for (int i = 0; atts[i] && atts[i + 1]; i += 2) {
                const std::string_view an(atts[i], std::strlen(atts[i]));
                const NameParts ap = split_name(an);
                if (ap.uri == kXmpMetaNs && ap.local == "xmptk") {
                    const std::string_view av(atts[i + 1],
                                              std::strlen(atts[i + 1]));
                    const std::string_view trimmed = trim_ascii_ws(av);
                    (void)emit_property_text(ctx, kXmpMetaNs, "XMPToolkit",
                                             trimmed);
                }
            }
        }

        // Enter rdf:Description scope.
        if (frame.is_description) {
            ctx->description_depth += 1;
            if (!ctx->path.empty() && custom_structure_scope(ctx)) {
                if (atts && atts[0]) {
                    stop_parser(ctx, XmpDecodeStatus::Unsupported);
                    return;
                }
                if (!mark_current_path_as_struct(ctx))
                    return;
            }
        }

        // If inside an rdf:Description and we see a non-rdf element, treat it as a
        // property path component.
        if (ctx->description_depth > 0 && frame.is_nonrdf) {
            const bool is_root = ctx->path.empty();
            if (!is_root && custom_structure_scope(ctx)) {
                if (parts.uri.empty() || parts.uri == kXmpMetaNs) {
                    stop_parser(ctx, XmpDecodeStatus::Unsupported);
                    return;
                }
                Frame* array = find_nearest_array_container(ctx);
                if (array && array->is_alt_container) {
                    stop_parser(ctx, XmpDecodeStatus::Unsupported);
                    return;
                }
                if (!mark_current_path_as_struct(ctx))
                    return;
            }
            std::string qualified_seg;
            std::string_view seg = parts.local;
            if (is_root) {
                ctx->root_schema_is_custom = false;
                if (ctx->options.preserve_custom_structures) {
                    const uint32_t max_uri = custom_structure_namespace_limit(
                        ctx);
                    if (parts.uri.empty()) {
                        stop_parser(ctx, XmpDecodeStatus::Unsupported);
                        return;
                    }
                    if (parts.uri.size() > max_uri) {
                        stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
                        return;
                    }
                    const bool is_standard
                        = detail::xmp_structure_is_standard_namespace(
                            parts.uri);
                    if (!is_standard
                        && !structure_namespace_registered(ctx, parts.uri)
                        && ctx->structure_namespaces.size()
                               >= kDecodeStructureMaxNamespaces) {
                        stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
                        return;
                    }
                    const bool is_custom
                        = detail::xmp_structure_namespace_is_custom(parts.uri);
                    if (!is_standard && !is_custom) {
                        stop_parser(ctx, XmpDecodeStatus::Unsupported);
                        return;
                    }
                    ctx->root_schema_is_custom = is_custom;
                }
                if (custom_structure_scope(ctx)
                    && !add_structure_namespace(ctx, parts.uri)) {
                    return;
                }
                ctx->root_schema_ns.assign(parts.uri.data(), parts.uri.size());
            } else if (parts.uri != ctx->root_schema_ns) {
                if (custom_structure_scope(ctx)
                    && !register_structure_namespace(ctx, parts.uri)) {
                    return;
                }
                std::string_view nested_prefix
                    = portable_nested_prefix_for_xmp_ns(parts.uri);
                if (nested_prefix.empty() && !parts.uri.empty()) {
                    if (!assign_unknown_nested_segment(ctx, parts.uri,
                                                       parts.local,
                                                       &qualified_seg)) {
                        return;
                    }
                    seg = qualified_seg;
                }
                if (!nested_prefix.empty()) {
                    if (custom_structure_scope(ctx)) {
                        const uint64_t prefix_bytes
                            = static_cast<uint64_t>(nested_prefix.size()) + 1U;
                        const uint64_t path_bytes
                            = static_cast<uint64_t>(ctx->path.size())
                              + (ctx->path.empty() ? 0U : 1U);
                        const uint64_t local_bytes = static_cast<uint64_t>(
                            parts.local.size());
                        const uint32_t max_path = custom_structure_path_limit(
                            ctx);
                        if (prefix_bytes > UINT64_MAX - path_bytes
                            || local_bytes
                                   > UINT64_MAX - path_bytes - prefix_bytes
                            || path_bytes + prefix_bytes + local_bytes
                                   > max_path) {
                            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
                            return;
                        }
                    }
                    qualified_seg.reserve(nested_prefix.size() + 1U
                                          + parts.local.size());
                    qualified_seg.assign(nested_prefix.data(),
                                         nested_prefix.size());
                    qualified_seg.push_back(':');
                    qualified_seg.append(parts.local.data(),
                                         parts.local.size());
                    seg = qualified_seg;
                }
            }
            if (!path_append_segment(ctx, seg, true)) {
                return;
            }
            frame.contributed_to_path = true;
            frame.path_len_after      = static_cast<uint32_t>(ctx->path.size());

            bool parse_type_resource = false;
            if (!custom_property_attributes_supported(ctx, atts,
                                                      &parse_type_resource)) {
                return;
            }
            if (parse_type_resource
                && !emit_structure_declaration(ctx, ctx->path,
                                               XmpContainerKind::Struct,
                                               &frame)) {
                return;
            }

            // If the property uses rdf:resource, emit it as the value immediately.
            if (!custom_structure_scope(ctx) && atts) {
                for (int i = 0; atts[i] && atts[i + 1]; i += 2) {
                    const std::string_view an(atts[i], std::strlen(atts[i]));
                    const NameParts ap = split_name(an);
                    if (ap.uri == kRdfNs && ap.local == "resource") {
                        const std::string_view av(atts[i + 1],
                                                  std::strlen(atts[i + 1]));
                        const std::string_view trimmed
                            = property_text_value(ctx->root_schema_ns,
                                                  ctx->path, av);
                        (void)emit_property_text(ctx, ctx->root_schema_ns,
                                                 ctx->path, trimmed);
                        frame.emitted_resource_val = true;
                        break;
                    }
                }
            }
        }

        if (ctx->description_depth > 0 && frame.is_array_container
            && !ctx->path.empty() && custom_structure_scope(ctx)) {
            if (!custom_container_attributes_supported(ctx, atts))
                return;
            Frame* owner = find_current_path_owner(ctx);
            if (!owner) {
                stop_parser(ctx, XmpDecodeStatus::Malformed);
                return;
            }
            const XmpContainerKind kind = frame.is_alt_container
                                              ? XmpContainerKind::LangAlt
                                          : parts.local == "Bag"
                                              ? XmpContainerKind::Bag
                                              : XmpContainerKind::Seq;
            if (!emit_structure_declaration(ctx, ctx->path, kind, owner))
                return;
            frame.structure_declared = true;
            frame.structure_kind     = kind;
        }

        // Array item: append an index to the current property path.
        if (ctx->description_depth > 0 && frame.is_li && !ctx->path.empty()) {
            Frame* container = find_nearest_array_container(ctx);
            if (container) {
                if (container->li_counter == UINT32_MAX) {
                    stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
                    return;
                }
                container->li_counter += 1;
                frame.path_len_before = static_cast<uint32_t>(ctx->path.size());
                frame.contributed_to_path = true;
                std::string_view lang;
                bool parse_type_resource = false;
                if (!custom_li_attributes_supported(ctx, atts,
                                                    container->is_alt_container,
                                                    &lang,
                                                    &parse_type_resource)) {
                    return;
                }
                if (container->is_alt_container) {
                    if (custom_structure_scope(ctx)) {
                        if (!path_append_lang_alt(ctx, lang)) {
                            if (ctx->result.status == XmpDecodeStatus::Ok)
                                stop_parser(ctx, XmpDecodeStatus::Unsupported);
                            return;
                        }
                    } else if (find_xml_lang_attr(atts, &lang)
                               && path_append_lang_alt(ctx, lang)) {
                        // Keep the historical indexed fallback for non-language Alt items.
                    } else if (!path_append_index(ctx, container->li_counter)) {
                        return;
                    }
                } else if (!path_append_index(ctx, container->li_counter)) {
                    return;
                }
                frame.path_len_after = static_cast<uint32_t>(ctx->path.size());
                if (parse_type_resource
                    && !emit_structure_declaration(ctx, ctx->path,
                                                   XmpContainerKind::Struct,
                                                   &frame)) {
                    return;
                }
            } else if (custom_structure_scope(ctx)) {
                stop_parser(ctx, XmpDecodeStatus::Unsupported);
                return;
            }
        }

        ctx->stack.push_back(std::move(frame));

        // Decode attributes on rdf:Description as top-level properties.
        if (ctx->description_depth > 0 && is_desc
            && ctx->options.decode_description_attributes && atts) {
            for (int i = 0; atts[i] && atts[i + 1]; i += 2) {
                const std::string_view an(atts[i], std::strlen(atts[i]));
                const NameParts ap = split_name(an);
                if (ap.uri.empty() || ap.local.empty()) {
                    continue;
                }
                const std::string_view av(atts[i + 1],
                                          std::strlen(atts[i + 1]));
                const std::string_view trimmed
                    = property_text_value(ap.uri, ap.local, av);
                if (ap.uri == kRdfNs) {
                    if (ap.local == "about" && !trimmed.empty()) {
                        (void)emit_property_text(ctx, kRdfNs, "About", trimmed);
                    }
                    continue;
                }
                if (ap.uri == kXmlNs) {
                    continue;
                }
                (void)emit_property_text(ctx, ap.uri, ap.local, trimmed);
            }
        }
    }


    static void XMLCALL end_element(void* user_data, const XML_Char* /*name_c*/)
    {
        Ctx* ctx = reinterpret_cast<Ctx*>(user_data);
        if (should_stop(ctx)) {
            return;
        }
        if (ctx->stack.empty()) {
            stop_parser(ctx, XmpDecodeStatus::Malformed);
            return;
        }

        Frame frame = std::move(ctx->stack.back());
        ctx->stack.pop_back();

        if (custom_structure_scope(ctx) && frame.text_exceeds_structure_budget
            && !frame_is_custom_structure(ctx, frame)) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return;
        }
        if (custom_structure_scope(ctx)
            && !frame_is_custom_structure(ctx, frame) && frame.text_truncated) {
            merge_status(&ctx->result, XmpDecodeStatus::OutputTruncated);
        }
        if (custom_structure_scope(ctx)
            && !frame_is_custom_structure(ctx, frame)) {
            (void)materialize_frame_text(&frame);
        }

        // Emit element/li text values (leaf-only). Preserve explicit empty
        // values so XMP->XMP round-trip doesn't drop empty properties.
        if (ctx->description_depth > 0 && !ctx->path.empty()
            && !frame.emitted_resource_val && !frame.had_child_element) {
            if (frame.structure_declared && custom_structure_scope(ctx)) {
                // The explicit marker represents an empty struct/array node.
            } else if (frame.is_li || frame.is_nonrdf) {
                const std::string_view text
                    = custom_structure_scope(ctx)
                          ? std::string_view(frame.text)
                          : property_text_value(ctx->root_schema_ns, ctx->path,
                                                frame.text);
                (void)emit_property_text(ctx, ctx->root_schema_ns, ctx->path,
                                         text);
            } else if (frame.is_array_container && frame.li_counter == 0U) {
                (void)emit_property_text(ctx, ctx->root_schema_ns, ctx->path,
                                         std::string_view {});
            }
        }

        // Restore the property path.
        if (frame.contributed_to_path) {
            if (frame.path_len_before <= ctx->path.size()) {
                ctx->path.resize(frame.path_len_before);
            } else {
                stop_parser(ctx, XmpDecodeStatus::Malformed);
                return;
            }
            if (ctx->path.empty()) {
                ctx->root_schema_ns.clear();
                ctx->root_schema_is_custom = false;
            }
        }

        if (frame.is_description) {
            if (ctx->description_depth == 0) {
                stop_parser(ctx, XmpDecodeStatus::Malformed);
                return;
            }
            ctx->description_depth -= 1;
        }
    }


    static void XMLCALL char_data(void* user_data, const XML_Char* s, int len)
    {
        Ctx* ctx = reinterpret_cast<Ctx*>(user_data);
        if (should_stop(ctx) || !s || len <= 0) {
            return;
        }
        if (ctx->stack.empty()) {
            return;
        }

        Frame& frame = ctx->stack.back();
        if (ctx->description_depth == 0 || ctx->path.empty()) {
            return;
        }
        const std::string_view chunk(s, static_cast<size_t>(len));
        if (frame_is_custom_structure(ctx, frame)) {
            if (!xml_whitespace_only(chunk)) {
                frame.nonwhite_text = true;
                stop_parser(ctx, XmpDecodeStatus::Unsupported);
            }
            return;
        }
        if (!frame.is_li && !frame.is_nonrdf) {
            return;
        }
        if (frame.emitted_resource_val) {
            return;
        }

        const uint32_t max_val   = ctx->options.limits.max_value_bytes;
        const uint64_t max_total = ctx->options.limits.max_total_value_bytes;
        if (custom_structure_scope(ctx) && !xml_whitespace_only(chunk))
            frame.nonwhite_text = true;

        const uint32_t want   = static_cast<uint32_t>(len);
        const uint64_t have64 = frame_text_size(frame);
        if (have64 > UINT32_MAX) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return;
        }
        const uint32_t have = static_cast<uint32_t>(have64);
        uint32_t avail      = 0;
        if (max_val == 0U || have < max_val) {
            avail = (max_val == 0U) ? want : (max_val - have);
        }
        uint32_t take = want;
        if (avail < take) {
            take                 = avail;
            frame.text_truncated = true;
            if (!custom_structure_scope(ctx))
                merge_status(&ctx->result, XmpDecodeStatus::OutputTruncated);
        }

        if (take == 0) {
            return;
        }

        const uint64_t retained_size = static_cast<uint64_t>(
                                           frame_text_size(frame))
                                       + static_cast<uint64_t>(take);
        if (custom_structure_scope(ctx)
            && retained_size > kDecodeStructureMaxRequestBytes) {
            frame.text_exceeds_structure_budget = true;
        }

        if (max_total != 0U && !custom_structure_scope(ctx)) {
            const uint64_t add = static_cast<uint64_t>(take);
            if (add > (UINT64_MAX - ctx->total_value_bytes)
                || ctx->total_value_bytes + add > max_total) {
                stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
                return;
            }
        }

        const std::string_view retained(s, static_cast<size_t>(take));
        if (custom_structure_scope(ctx)
            && frame.text_exceeds_structure_budget) {
            return;
        }
        if (custom_structure_scope(ctx) && frame.text.empty()
            && !frame.nonwhite_text && xml_whitespace_only(retained)) {
            append_packed_whitespace(&frame, retained);
        } else {
            if (custom_structure_scope(ctx))
                (void)materialize_frame_text(&frame);
            frame.text.append(s, static_cast<size_t>(take));
        }
    }

#endif  // OPENMETA_HAS_EXPAT

}  // namespace

XmpDecodeResult
decode_xmp_packet(std::span<const std::byte> xmp_bytes, MetaStore& store,
                  EntryFlags flags, const XmpDecodeOptions& options) noexcept
{
    XmpDecodeResult result;

    store.constrain_resources(options.limits.max_properties,
                              options.limits.max_arena_bytes);
    if (store.resource_limit_exceeded()) {
        result.status = XmpDecodeStatus::LimitExceeded;
        return result;
    }

    const uint64_t max_in = options.limits.max_input_bytes;
    if (max_in != 0U && xmp_bytes.size() > max_in) {
        result.status = XmpDecodeStatus::LimitExceeded;
        return result;
    }

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT
    const std::span<const std::byte> pkt = normalize_xmp_packet(xmp_bytes);
    if (pkt.empty()) {
        result.status = XmpDecodeStatus::Unsupported;
        return result;
    }

    Ctx ctx;
    ctx.store = &store;
    ctx.block = store.add_block(BlockInfo {});
    if (ctx.block == kInvalidBlockId) {
        result.status = XmpDecodeStatus::LimitExceeded;
        return result;
    }
    ctx.flags                   = flags;
    ctx.options                 = options;
    ctx.result.status           = XmpDecodeStatus::Ok;
    ctx.result.entries_decoded  = 0;
    const uint32_t path_reserve = options.preserve_custom_structures
                                      ? std::min(options.limits.max_path_bytes,
                                                 kDecodeStructureMaxPathBytes)
                                      : options.limits.max_path_bytes;
    ctx.path.reserve(path_reserve);
    ctx.stack.reserve(options.limits.max_depth);
    if (options.preserve_custom_structures)
        ctx.structure_namespaces.reserve(kDecodeStructureMaxNamespaces);

    ctx.parser = XML_ParserCreateNS(nullptr, '|');
    if (!ctx.parser) {
        result.status = XmpDecodeStatus::Malformed;
        return result;
    }

    XML_SetUserData(ctx.parser, &ctx);
    XML_SetElementHandler(ctx.parser, &start_element, &end_element);
    XML_SetCharacterDataHandler(ctx.parser, &char_data);
    XML_SetStartDoctypeDeclHandler(ctx.parser, &start_doctype_decl);
    XML_SetEntityDeclHandler(ctx.parser, &entity_decl);
    XML_SetExternalEntityRefHandler(ctx.parser, &external_entity_ref_handler);
    (void)XML_SetParamEntityParsing(ctx.parser, XML_PARAM_ENTITY_PARSING_NEVER);
#    if defined(XML_DTD) || (defined(XML_GE) && XML_GE == 1)
    (void)XML_SetBillionLaughsAttackProtectionMaximumAmplification(ctx.parser,
                                                                   32.0f);
    (void)XML_SetBillionLaughsAttackProtectionActivationThreshold(
        ctx.parser, 8ULL * 1024ULL);
#    endif
#    if OPENMETA_EXPAT_HAS_REPARSE_DEFERRAL
    (void)XML_SetReparseDeferralEnabled(ctx.parser, XML_TRUE);
#    endif

    const char* data = reinterpret_cast<const char*>(pkt.data());
    const int size   = (pkt.size() > static_cast<size_t>(INT32_MAX))
                           ? INT32_MAX
                           : static_cast<int>(pkt.size());

    if (pkt.size() > static_cast<size_t>(INT32_MAX)) {
        stop_parser(&ctx, XmpDecodeStatus::LimitExceeded);
    } else {
        const XML_Status st = XML_Parse(ctx.parser, data, size, XML_TRUE);
        if (st == XML_STATUS_ERROR
            && (ctx.result.status == XmpDecodeStatus::Ok
                || ctx.result.status == XmpDecodeStatus::OutputTruncated)) {
            // Treat "not XML" as Unsupported, otherwise Malformed.
            const enum XML_Error err = XML_GetErrorCode(ctx.parser);
            if (err == XML_ERROR_SYNTAX || err == XML_ERROR_NO_ELEMENTS) {
                merge_status(&ctx.result, XmpDecodeStatus::Unsupported);
            } else {
                merge_status(&ctx.result, XmpDecodeStatus::Malformed);
            }
        }
    }

    XML_ParserFree(ctx.parser);
    ctx.parser = nullptr;

    if (options.preserve_custom_structures
        && ctx.result.status == XmpDecodeStatus::Ok) {
        detail::XmpStructureTree tree;
        const detail::XmpStructureBuildResult structure_result
            = detail::prepare_xmp_structure_tree(store, XmpStructureLimits {},
                                                 &tree);
        if (structure_result.status
            == detail::XmpStructureStatus::InvalidMetadata)
            merge_status(&ctx.result, XmpDecodeStatus::Malformed);
        else if (structure_result.status
                 == detail::XmpStructureStatus::LimitExceeded)
            merge_status(&ctx.result, XmpDecodeStatus::LimitExceeded);
        else if (structure_result.status
                 == detail::XmpStructureStatus::InvalidOptions)
            merge_status(&ctx.result, XmpDecodeStatus::Unsupported);
    }

    result = ctx.result;
    if (store.resource_limit_exceeded()) {
        merge_status(&result, XmpDecodeStatus::LimitExceeded);
    }
    if (result.status == XmpDecodeStatus::Malformed
        && options.malformed_mode == XmpDecodeMalformedMode::OutputTruncated) {
        result.status = XmpDecodeStatus::OutputTruncated;
    }
    return result;
#else
    (void)store;
    (void)flags;
    (void)options;
    result.status = XmpDecodeStatus::Unsupported;
    return result;
#endif
}

XmpDecodeResult
measure_xmp_packet(std::span<const std::byte> xmp_bytes,
                   const XmpDecodeOptions& options) noexcept
{
    MetaStore scratch;
    return decode_xmp_packet(xmp_bytes, scratch, EntryFlags::None, options);
}

}  // namespace openmeta
