// SPDX-License-Identifier: Apache-2.0

#include "openmeta/xmp_dump.h"

#include "interop_safety_internal.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/geotiff_key_names.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openmeta {
namespace {

    static constexpr std::string_view kXmpNsX = "adobe:ns:meta/";
    static constexpr std::string_view kXmpNsRdf
        = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    static constexpr std::string_view kXmpNsOpenMetaDump
        = "urn:openmeta:dump:1.0";

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
    static constexpr std::string_view kXmpNsDng = "http://ns.adobe.com/dng/1.0/";
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
    static constexpr uint64_t kMaxInitialSidecarOutputBytes = 256ULL * 1024ULL
                                                              * 1024ULL;

    static constexpr const char* kIndent1 = "  ";
    static constexpr const char* kIndent2 = "    ";
    static constexpr const char* kIndent3 = "      ";
    static constexpr const char* kIndent4 = "        ";

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> b = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(b.data()),
                                b.size());
    }


    struct SpanWriter final {
        std::span<std::byte> out;
        uint64_t max_output = 0;
        uint64_t written    = 0;
        uint64_t needed     = 0;
        bool limit_hit      = false;

        explicit SpanWriter(std::span<std::byte> dst,
                            uint64_t max_output_bytes) noexcept
            : out(dst)
            , max_output(max_output_bytes)
        {
        }

        void note_bytes(uint64_t n) noexcept
        {
            if (limit_hit) {
                return;
            }
            if (n > (UINT64_MAX - needed)) {
                limit_hit = true;
                return;
            }
            const uint64_t next = needed + n;
            if (max_output != 0U && next > max_output) {
                limit_hit = true;
                return;
            }
            needed = next;
        }

        void append_bytes(const void* data, uint64_t n) noexcept
        {
            if (!data || n == 0U) {
                return;
            }
            if (limit_hit) {
                return;
            }

            note_bytes(n);
            if (limit_hit) {
                return;
            }

            const uint64_t cap  = static_cast<uint64_t>(out.size());
            const uint64_t w    = (written < cap) ? (cap - written) : 0U;
            const uint64_t take = (n < w) ? n : w;
            if (take > 0U) {
                std::memcpy(out.data() + static_cast<size_t>(written), data,
                            static_cast<size_t>(take));
                written += take;
            }
        }

        void append(std::string_view s) noexcept
        {
            append_bytes(s.data(), static_cast<uint64_t>(s.size()));
        }

        void append_char(char c) noexcept { append_bytes(&c, 1U); }
    };

    struct XmpNsDecl final {
        std::string_view prefix;
        std::string_view uri;
    };

    struct PortableCustomNsDecl final {
        std::string prefix;
        std::string uri;
    };

    static void emit_xmp_packet_begin(SpanWriter* w,
                                      std::span<const XmpNsDecl> decls) noexcept
    {
        if (!w) {
            return;
        }

        w->append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        w->append("<x:xmpmeta xmlns:x=\"");
        w->append(kXmpNsX);
        w->append("\" x:xmptk=\"OpenMeta\">\n");
        w->append(kIndent1);
        w->append("<rdf:RDF xmlns:rdf=\"");
        w->append(kXmpNsRdf);
        w->append("\"");
        for (size_t i = 0; i < decls.size(); ++i) {
            const XmpNsDecl& d = decls[i];
            if (d.prefix.empty() || d.uri.empty()) {
                continue;
            }
            w->append(" xmlns:");
            w->append(d.prefix);
            w->append("=\"");
            w->append(d.uri);
            w->append("\"");
        }
        w->append(">\n");
        w->append(kIndent2);
        w->append("<rdf:Description rdf:about=\"\">\n");
    }

    static void emit_xmp_packet_end(SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        w->append(kIndent2);
        w->append("</rdf:Description>\n");
        w->append(kIndent1);
        w->append("</rdf:RDF>\n");
        w->append("</x:xmpmeta>\n");
    }


    static void append_u64_dec(uint64_t v, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llu",
                      static_cast<unsigned long long>(v));
        w->append(buf);
    }

    static void append_u32_hex(uint32_t v, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(v));
        w->append(buf);
    }

    static void append_u16_hex(uint16_t v, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%04X", static_cast<unsigned>(v));
        w->append(buf);
    }


    static void append_xml_safe_ascii(std::string_view s,
                                      SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        for (size_t i = 0; i < s.size(); ++i) {
            const uint8_t c = static_cast<uint8_t>(s[i]);
            if (c == static_cast<uint8_t>('&')) {
                w->append("&amp;");
                continue;
            }
            if (c == static_cast<uint8_t>('<')) {
                w->append("&lt;");
                continue;
            }
            if (c == static_cast<uint8_t>('>')) {
                w->append("&gt;");
                continue;
            }
            if (c >= 0x20U && c <= 0x7EU) {
                w->append_char(static_cast<char>(c));
                continue;
            }

            // Emit a deterministic ASCII escape for anything non-printable or non-ASCII.
            char buf[5];
            static constexpr char hex[] = "0123456789ABCDEF";
            buf[0]                      = '\\';
            buf[1]                      = 'x';
            buf[2]                      = hex[(c >> 4) & 0x0F];
            buf[3]                      = hex[(c >> 0) & 0x0F];
            buf[4]                      = '\0';
            w->append(buf);
        }
    }

    static void append_xml_safe_utf8(std::string_view s, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }

        for (size_t i = 0; i < s.size(); ++i) {
            const uint8_t c = static_cast<uint8_t>(s[i]);
            if (c == static_cast<uint8_t>('&')) {
                w->append("&amp;");
                continue;
            }
            if (c == static_cast<uint8_t>('<')) {
                w->append("&lt;");
                continue;
            }
            if (c == static_cast<uint8_t>('>')) {
                w->append("&gt;");
                continue;
            }
            if (c == static_cast<uint8_t>('\"')) {
                w->append("&quot;");
                continue;
            }
            if (c == static_cast<uint8_t>('\'')) {
                w->append("&apos;");
                continue;
            }

            // XML 1.0 allows TAB/CR/LF and 0x20..; escape other control bytes.
            if (c == 0x09U || c == 0x0AU || c == 0x0DU
                || (c >= 0x20U && c != 0x7FU)) {
                w->append_char(static_cast<char>(c));
                continue;
            }

            char buf[5];
            static constexpr char hex[] = "0123456789ABCDEF";
            buf[0]                      = '\\';
            buf[1]                      = 'x';
            buf[2]                      = hex[(c >> 4) & 0x0F];
            buf[3]                      = hex[(c >> 0) & 0x0F];
            buf[4]                      = '\0';
            w->append(buf);
        }
    }


    struct Base64Encoder final {
        SpanWriter* w     = nullptr;
        uint8_t buf[3]    = { 0, 0, 0 };
        uint32_t buffered = 0;

        explicit Base64Encoder(SpanWriter* writer) noexcept
            : w(writer)
        {
        }

        void emit_triplet(uint8_t a, uint8_t b, uint8_t c) noexcept
        {
            static constexpr char kEnc[]
                = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            char out4[4];
            out4[0] = kEnc[(a >> 2) & 0x3F];
            out4[1] = kEnc[((a & 0x03) << 4) | ((b >> 4) & 0x0F)];
            out4[2] = kEnc[((b & 0x0F) << 2) | ((c >> 6) & 0x03)];
            out4[3] = kEnc[c & 0x3F];
            w->append_bytes(out4, 4);
        }

        void append_u8(uint8_t v) noexcept
        {
            if (!w || w->limit_hit) {
                return;
            }
            buf[buffered] = v;
            buffered += 1;
            if (buffered == 3U) {
                emit_triplet(buf[0], buf[1], buf[2]);
                buffered = 0;
            }
        }

        void append(std::span<const std::byte> bytes) noexcept
        {
            if (!w || w->limit_hit) {
                return;
            }
            for (size_t i = 0; i < bytes.size(); ++i) {
                append_u8(static_cast<uint8_t>(bytes[i]));
                if (w->limit_hit) {
                    return;
                }
            }
        }

        void finish() noexcept
        {
            if (!w || w->limit_hit) {
                return;
            }
            static constexpr char kEnc[]
                = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            if (buffered == 0U) {
                return;
            }
            if (buffered == 1U) {
                const uint8_t a = buf[0];
                char out4[4];
                out4[0] = kEnc[(a >> 2) & 0x3F];
                out4[1] = kEnc[(a & 0x03) << 4];
                out4[2] = '=';
                out4[3] = '=';
                w->append_bytes(out4, 4);
            } else if (buffered == 2U) {
                const uint8_t a = buf[0];
                const uint8_t b = buf[1];
                char out4[4];
                out4[0] = kEnc[(a >> 2) & 0x3F];
                out4[1] = kEnc[((a & 0x03) << 4) | ((b >> 4) & 0x0F)];
                out4[2] = kEnc[(b & 0x0F) << 2];
                out4[3] = '=';
                w->append_bytes(out4, 4);
            }
            buffered = 0;
        }
    };


    static void le16(uint16_t v, uint8_t out[2]) noexcept
    {
        out[0] = static_cast<uint8_t>((v >> 0) & 0xFF);
        out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    }
    static void le32(uint32_t v, uint8_t out[4]) noexcept
    {
        out[0] = static_cast<uint8_t>((v >> 0) & 0xFF);
        out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        out[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        out[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    }
    static void le64(uint64_t v, uint8_t out[8]) noexcept
    {
        out[0] = static_cast<uint8_t>((v >> 0) & 0xFF);
        out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        out[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        out[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
        out[4] = static_cast<uint8_t>((v >> 32) & 0xFF);
        out[5] = static_cast<uint8_t>((v >> 40) & 0xFF);
        out[6] = static_cast<uint8_t>((v >> 48) & 0xFF);
        out[7] = static_cast<uint8_t>((v >> 56) & 0xFF);
    }


    static uint32_t meta_element_size(MetaElementType type) noexcept
    {
        switch (type) {
        case MetaElementType::U8:
        case MetaElementType::I8: return 1U;
        case MetaElementType::U16:
        case MetaElementType::I16: return 2U;
        case MetaElementType::U32:
        case MetaElementType::I32:
        case MetaElementType::F32: return 4U;
        case MetaElementType::U64:
        case MetaElementType::I64:
        case MetaElementType::F64: return 8U;
        case MetaElementType::URational:
        case MetaElementType::SRational: return 8U;
        }
        return 0U;
    }


    static uint32_t safe_array_count(const ByteArena& arena,
                                     const MetaValue& value) noexcept
    {
        if (value.kind != MetaValueKind::Array) {
            return value.count;
        }
        const std::span<const std::byte> raw = arena.span(value.data.span);
        const uint32_t elem_size = meta_element_size(value.elem_type);
        if (elem_size == 0U) {
            return 0U;
        }
        const uint32_t available = static_cast<uint32_t>(raw.size()
                                                         / elem_size);
        return (value.count < available) ? value.count : available;
    }


    static const char* key_kind_name(MetaKeyKind k) noexcept
    {
        switch (k) {
        case MetaKeyKind::ExifTag: return "ExifTag";
        case MetaKeyKind::Comment: return "Comment";
        case MetaKeyKind::ExrAttribute: return "ExrAttribute";
        case MetaKeyKind::IptcDataset: return "IptcDataset";
        case MetaKeyKind::XmpProperty: return "XmpProperty";
        case MetaKeyKind::IccHeaderField: return "IccHeaderField";
        case MetaKeyKind::IccTag: return "IccTag";
        case MetaKeyKind::PhotoshopIrb: return "PhotoshopIrb";
        case MetaKeyKind::PhotoshopIrbField: return "PhotoshopIrbField";
        case MetaKeyKind::GeotiffKey: return "GeotiffKey";
        case MetaKeyKind::PrintImField: return "PrintImField";
        case MetaKeyKind::BmffField: return "BmffField";
        case MetaKeyKind::JumbfField: return "JumbfField";
        case MetaKeyKind::JumbfCborKey: return "JumbfCborKey";
        case MetaKeyKind::PngText: return "PngText";
        }
        return "Unknown";
    }

    static const char* value_kind_name(MetaValueKind k) noexcept
    {
        switch (k) {
        case MetaValueKind::Empty: return "Empty";
        case MetaValueKind::Scalar: return "Scalar";
        case MetaValueKind::Array: return "Array";
        case MetaValueKind::Bytes: return "Bytes";
        case MetaValueKind::Text: return "Text";
        }
        return "Unknown";
    }

    static const char* elem_type_name(MetaElementType t) noexcept
    {
        switch (t) {
        case MetaElementType::U8: return "U8";
        case MetaElementType::I8: return "I8";
        case MetaElementType::U16: return "U16";
        case MetaElementType::I16: return "I16";
        case MetaElementType::U32: return "U32";
        case MetaElementType::I32: return "I32";
        case MetaElementType::U64: return "U64";
        case MetaElementType::I64: return "I64";
        case MetaElementType::F32: return "F32";
        case MetaElementType::F64: return "F64";
        case MetaElementType::URational: return "URational";
        case MetaElementType::SRational: return "SRational";
        }
        return "Unknown";
    }

    static const char* text_encoding_name(TextEncoding e) noexcept
    {
        switch (e) {
        case TextEncoding::Unknown: return "Unknown";
        case TextEncoding::Ascii: return "Ascii";
        case TextEncoding::Utf8: return "Utf8";
        case TextEncoding::Utf16LE: return "Utf16LE";
        case TextEncoding::Utf16BE: return "Utf16BE";
        }
        return "Unknown";
    }

    static const char* wire_family_name(WireFamily f) noexcept
    {
        switch (f) {
        case WireFamily::None: return "None";
        case WireFamily::Tiff: return "Tiff";
        case WireFamily::Other: return "Other";
        }
        return "Unknown";
    }

    static std::string_view exr_wire_type_name(uint16_t code) noexcept
    {
        switch (code) {
        case 1: return "box2i";
        case 2: return "box2f";
        case 3: return "bytes";
        case 4: return "chlist";
        case 5: return "chromaticities";
        case 6: return "compression";
        case 7: return "double";
        case 8: return "envmap";
        case 9: return "float";
        case 10: return "floatvector";
        case 11: return "int";
        case 12: return "keycode";
        case 13: return "lineOrder";
        case 14: return "m33f";
        case 15: return "m33d";
        case 16: return "m44f";
        case 17: return "m44d";
        case 18: return "preview";
        case 19: return "rational";
        case 20: return "string";
        case 21: return "stringvector";
        case 22: return "tiledesc";
        case 23: return "timecode";
        case 24: return "v2i";
        case 25: return "v2f";
        case 26: return "v2d";
        case 27: return "v3i";
        case 28: return "v3f";
        case 29: return "v3d";
        case 30: return "deepImageState";
        default: return {};
        }
    }


    static void emit_open(SpanWriter* w, const char* indent,
                          std::string_view name) noexcept
    {
        w->append(indent);
        w->append("<");
        w->append(name);
        w->append(">");
    }

    static void emit_text_element(SpanWriter* w, const char* indent,
                                  std::string_view name,
                                  std::string_view value) noexcept
    {
        emit_open(w, indent, name);
        append_xml_safe_ascii(value, w);
        w->append("</");
        w->append(name);
        w->append(">\n");
    }

    static void emit_u64_element(SpanWriter* w, const char* indent,
                                 std::string_view name, uint64_t value) noexcept
    {
        emit_open(w, indent, name);
        append_u64_dec(value, w);
        w->append("</");
        w->append(name);
        w->append(">\n");
    }


    static void emit_value_base64(const ByteArena& arena, const MetaValue& v,
                                  SpanWriter* w, uint64_t* out_bytes) noexcept
    {
        if (out_bytes) {
            *out_bytes = 0;
        }
        if (!w || w->limit_hit) {
            return;
        }

        Base64Encoder b64(w);

        if (v.kind == MetaValueKind::Empty) {
            return;
        }

        if (v.kind == MetaValueKind::Bytes || v.kind == MetaValueKind::Text) {
            const std::span<const std::byte> raw = arena.span(v.data.span);
            if (out_bytes) {
                *out_bytes = static_cast<uint64_t>(raw.size());
            }
            b64.append(raw);
            b64.finish();
            return;
        }

        if (v.kind == MetaValueKind::Scalar) {
            switch (v.elem_type) {
            case MetaElementType::U8: {
                const uint8_t x = static_cast<uint8_t>(v.data.u64 & 0xFFU);
                if (out_bytes) {
                    *out_bytes = 1;
                }
                b64.append_u8(x);
                b64.finish();
                return;
            }
            case MetaElementType::I8: {
                const uint8_t x = static_cast<uint8_t>(
                    static_cast<int8_t>(v.data.i64));
                if (out_bytes) {
                    *out_bytes = 1;
                }
                b64.append_u8(x);
                b64.finish();
                return;
            }
            case MetaElementType::U16: {
                uint8_t tmp[2];
                le16(static_cast<uint16_t>(v.data.u64 & 0xFFFFU), tmp);
                if (out_bytes) {
                    *out_bytes = 2;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 2));
                b64.finish();
                return;
            }
            case MetaElementType::I16: {
                uint8_t tmp[2];
                le16(static_cast<uint16_t>(static_cast<int16_t>(v.data.i64)),
                     tmp);
                if (out_bytes) {
                    *out_bytes = 2;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 2));
                b64.finish();
                return;
            }
            case MetaElementType::U32: {
                uint8_t tmp[4];
                le32(static_cast<uint32_t>(v.data.u64 & 0xFFFFFFFFU), tmp);
                if (out_bytes) {
                    *out_bytes = 4;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 4));
                b64.finish();
                return;
            }
            case MetaElementType::I32: {
                uint8_t tmp[4];
                le32(static_cast<uint32_t>(static_cast<int32_t>(v.data.i64)),
                     tmp);
                if (out_bytes) {
                    *out_bytes = 4;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 4));
                b64.finish();
                return;
            }
            case MetaElementType::U64: {
                uint8_t tmp[8];
                le64(v.data.u64, tmp);
                if (out_bytes) {
                    *out_bytes = 8;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 8));
                b64.finish();
                return;
            }
            case MetaElementType::I64: {
                uint8_t tmp[8];
                le64(static_cast<uint64_t>(v.data.i64), tmp);
                if (out_bytes) {
                    *out_bytes = 8;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 8));
                b64.finish();
                return;
            }
            case MetaElementType::F32: {
                uint8_t tmp[4];
                le32(v.data.f32_bits, tmp);
                if (out_bytes) {
                    *out_bytes = 4;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 4));
                b64.finish();
                return;
            }
            case MetaElementType::F64: {
                uint8_t tmp[8];
                le64(v.data.f64_bits, tmp);
                if (out_bytes) {
                    *out_bytes = 8;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 8));
                b64.finish();
                return;
            }
            case MetaElementType::URational: {
                uint8_t tmp[8];
                le32(v.data.ur.numer, tmp + 0);
                le32(v.data.ur.denom, tmp + 4);
                if (out_bytes) {
                    *out_bytes = 8;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 8));
                b64.finish();
                return;
            }
            case MetaElementType::SRational: {
                uint8_t tmp[8];
                le32(static_cast<uint32_t>(v.data.sr.numer), tmp + 0);
                le32(static_cast<uint32_t>(v.data.sr.denom), tmp + 4);
                if (out_bytes) {
                    *out_bytes = 8;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 8));
                b64.finish();
                return;
            }
            }
        }

        if (v.kind == MetaValueKind::Array) {
            const std::span<const std::byte> raw = arena.span(v.data.span);
            const uint32_t n                     = safe_array_count(arena, v);
            const uint32_t elem_size = meta_element_size(v.elem_type);
            if (elem_size == 0U || n == 0U) {
                return;
            }
            if (out_bytes) {
                *out_bytes = static_cast<uint64_t>(n) * elem_size;
            }

            if (v.elem_type == MetaElementType::U8
                || v.elem_type == MetaElementType::I8) {
                b64.append(raw.subspan(0, static_cast<size_t>(n)));
                b64.finish();
                return;
            }

            const uint64_t take_bytes = static_cast<uint64_t>(n) * elem_size;
            if (take_bytes > raw.size()) {
                return;
            }

            for (uint32_t i = 0; i < n; ++i) {
                const size_t off = static_cast<size_t>(i) * elem_size;
                const std::span<const std::byte> elem = raw.subspan(off,
                                                                    elem_size);

                if (v.elem_type == MetaElementType::U16) {
                    uint16_t x = 0;
                    std::memcpy(&x, elem.data(), 2);
                    uint8_t tmp[2];
                    le16(x, tmp);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 2));
                } else if (v.elem_type == MetaElementType::I16) {
                    int16_t x = 0;
                    std::memcpy(&x, elem.data(), 2);
                    uint8_t tmp[2];
                    le16(static_cast<uint16_t>(x), tmp);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 2));
                } else if (v.elem_type == MetaElementType::U32
                           || v.elem_type == MetaElementType::F32) {
                    uint32_t x = 0;
                    std::memcpy(&x, elem.data(), 4);
                    uint8_t tmp[4];
                    le32(x, tmp);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 4));
                } else if (v.elem_type == MetaElementType::I32) {
                    int32_t x = 0;
                    std::memcpy(&x, elem.data(), 4);
                    uint8_t tmp[4];
                    le32(static_cast<uint32_t>(x), tmp);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 4));
                } else if (v.elem_type == MetaElementType::U64
                           || v.elem_type == MetaElementType::F64) {
                    uint64_t x = 0;
                    std::memcpy(&x, elem.data(), 8);
                    uint8_t tmp[8];
                    le64(x, tmp);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 8));
                } else if (v.elem_type == MetaElementType::I64) {
                    int64_t x = 0;
                    std::memcpy(&x, elem.data(), 8);
                    uint8_t tmp[8];
                    le64(static_cast<uint64_t>(x), tmp);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 8));
                } else if (v.elem_type == MetaElementType::URational) {
                    URational r;
                    std::memcpy(&r, elem.data(), sizeof(URational));
                    uint8_t tmp[8];
                    le32(r.numer, tmp + 0);
                    le32(r.denom, tmp + 4);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 8));
                } else if (v.elem_type == MetaElementType::SRational) {
                    SRational r;
                    std::memcpy(&r, elem.data(), sizeof(SRational));
                    uint8_t tmp[8];
                    le32(static_cast<uint32_t>(r.numer), tmp + 0);
                    le32(static_cast<uint32_t>(r.denom), tmp + 4);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 8));
                }

                if (w->limit_hit) {
                    return;
                }
            }

            b64.finish();
            return;
        }
    }


    static void emit_entry_key_fields(const MetaStore& store, const Entry& e,
                                      SpanWriter* w,
                                      const XmpDumpOptions& options) noexcept
    {
        const ByteArena& arena = store.arena();
        emit_text_element(w, kIndent4, "omd:keyKind",
                          key_kind_name(e.key.kind));

        // Canonical key text (stable, one-line, kind-specific).
        w->append(kIndent4);
        w->append("<omd:key>");
        switch (e.key.kind) {
        case MetaKeyKind::ExifTag: {
            const std::string_view ifd = arena_string(arena,
                                                      e.key.data.exif_tag.ifd);
            w->append("exif:");
            append_xml_safe_ascii(ifd, w);
            w->append(":");
            char buf[16];
            std::snprintf(buf, sizeof(buf), "0x%04X",
                          static_cast<unsigned>(e.key.data.exif_tag.tag));
            w->append(buf);
            break;
        }
        case MetaKeyKind::Comment: w->append("comment"); break;
        case MetaKeyKind::ExrAttribute: {
            const std::string_view name
                = arena_string(arena, e.key.data.exr_attribute.name);
            w->append("exr:part:");
            append_u64_dec(e.key.data.exr_attribute.part_index, w);
            w->append(":");
            append_xml_safe_ascii(name, w);
            break;
        }
        case MetaKeyKind::IptcDataset: {
            w->append("iptc:");
            append_u64_dec(e.key.data.iptc_dataset.record, w);
            w->append(":");
            append_u64_dec(e.key.data.iptc_dataset.dataset, w);
            break;
        }
        case MetaKeyKind::XmpProperty: {
            const std::string_view ns
                = arena_string(arena, e.key.data.xmp_property.schema_ns);
            const std::string_view path
                = arena_string(arena, e.key.data.xmp_property.property_path);
            w->append("xmp:");
            append_xml_safe_ascii(ns, w);
            w->append(":");
            append_xml_safe_ascii(path, w);
            break;
        }
        case MetaKeyKind::IccHeaderField:
            w->append("icc:header:");
            append_u64_dec(e.key.data.icc_header_field.offset, w);
            break;
        case MetaKeyKind::IccTag:
            w->append("icc:tag:");
            append_u32_hex(e.key.data.icc_tag.signature, w);
            break;
        case MetaKeyKind::PhotoshopIrb:
            w->append("psirb:");
            append_u16_hex(e.key.data.photoshop_irb.resource_id, w);
            break;
        case MetaKeyKind::PhotoshopIrbField:
            w->append("psirb_field:");
            append_u16_hex(e.key.data.photoshop_irb_field.resource_id, w);
            w->append(":");
            w->append(
                arena_string(arena, e.key.data.photoshop_irb_field.field));
            break;
        case MetaKeyKind::GeotiffKey:
            w->append("geotiff:");
            append_u64_dec(e.key.data.geotiff_key.key_id, w);
            break;
        case MetaKeyKind::PrintImField: {
            const std::string_view field
                = arena_string(arena, e.key.data.printim_field.field);
            w->append("printim:");
            append_xml_safe_ascii(field, w);
            break;
        }
        case MetaKeyKind::BmffField: {
            const std::string_view field
                = arena_string(arena, e.key.data.bmff_field.field);
            w->append("bmff:");
            append_xml_safe_ascii(field, w);
            break;
        }
        case MetaKeyKind::JumbfField: {
            const std::string_view field
                = arena_string(arena, e.key.data.jumbf_field.field);
            w->append("jumbf:");
            append_xml_safe_ascii(field, w);
            break;
        }
        case MetaKeyKind::JumbfCborKey: {
            const std::string_view key
                = arena_string(arena, e.key.data.jumbf_cbor_key.key);
            w->append("jumbf_cbor:");
            append_xml_safe_ascii(key, w);
            break;
        }
        case MetaKeyKind::PngText: {
            const std::string_view keyword
                = arena_string(arena, e.key.data.png_text.keyword);
            const std::string_view field
                = arena_string(arena, e.key.data.png_text.field);
            w->append("png_text:");
            append_xml_safe_ascii(keyword, w);
            w->append(":");
            append_xml_safe_ascii(field, w);
            break;
        }
        }
        w->append("</omd:key>\n");

        if (e.key.kind == MetaKeyKind::ExifTag) {
            const std::string_view ifd = arena_string(arena,
                                                      e.key.data.exif_tag.ifd);
            emit_text_element(w, kIndent4, "omd:ifd", ifd);
            w->append(kIndent4);
            w->append("<omd:tag>");
            append_u16_hex(e.key.data.exif_tag.tag, w);
            w->append("</omd:tag>\n");
            if (options.include_names) {
                const std::string_view n
                    = exif_tag_name(ifd, e.key.data.exif_tag.tag);
                if (!n.empty()) {
                    emit_text_element(w, kIndent4, "omd:tagName", n);
                }
            }
        } else if (e.key.kind == MetaKeyKind::GeotiffKey
                   && options.include_names) {
            const std::string_view n = geotiff_key_name(
                e.key.data.geotiff_key.key_id);
            if (!n.empty()) {
                emit_text_element(w, kIndent4, "omd:tagName", n);
            }
        } else if (e.key.kind == MetaKeyKind::ExrAttribute) {
            emit_u64_element(w, kIndent4, "omd:part",
                             static_cast<uint64_t>(
                                 e.key.data.exr_attribute.part_index));
            const std::string_view name
                = arena_string(arena, e.key.data.exr_attribute.name);
            emit_text_element(w, kIndent4, "omd:attrName", name);
        } else if (e.key.kind == MetaKeyKind::PngText) {
            emit_text_element(w, kIndent4, "omd:pngKeyword",
                              arena_string(arena, e.key.data.png_text.keyword));
            emit_text_element(w, kIndent4, "omd:pngField",
                              arena_string(arena, e.key.data.png_text.field));
        }
    }

}  // namespace


XmpDumpResult
dump_xmp_lossless(const MetaStore& store, std::span<std::byte> out,
                  const XmpDumpOptions& options) noexcept
{
    XmpDumpResult r;

    SpanWriter w(out, options.limits.max_output_bytes);

    static constexpr std::array<XmpNsDecl, 1> kDecls = {
        XmpNsDecl { "omd", kXmpNsOpenMetaDump },
    };
    emit_xmp_packet_begin(&w, std::span<const XmpNsDecl>(kDecls.data(),
                                                         kDecls.size()));

    emit_u64_element(&w, kIndent3, "omd:formatVersion", 1);
    emit_u64_element(&w, kIndent3, "omd:blockCount",
                     static_cast<uint64_t>(store.block_count()));

    w.append(kIndent3);
    w.append("<omd:entries>\n");
    w.append(kIndent4);
    w.append("<rdf:Seq>\n");

    const ByteArena& arena = store.arena();

    uint32_t emitted = 0;
    for (BlockId block = 0; block < store.block_count(); ++block) {
        const std::span<const EntryId> ids = store.entries_in_block(block);
        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& e = store.entry(ids[i]);
            if (any(e.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (options.limits.max_entries != 0U
                && emitted >= options.limits.max_entries) {
                w.limit_hit = true;
                break;
            }

            w.append(kIndent4);
            w.append("<rdf:li rdf:parseType=\"Resource\">\n");

            emit_entry_key_fields(store, e, &w, options);

            emit_text_element(&w, kIndent4, "omd:valueKind",
                              value_kind_name(e.value.kind));
            emit_text_element(&w, kIndent4, "omd:elemType",
                              elem_type_name(e.value.elem_type));
            emit_text_element(&w, kIndent4, "omd:textEncoding",
                              text_encoding_name(e.value.text_encoding));
            emit_u64_element(&w, kIndent4, "omd:count",
                             static_cast<uint64_t>(e.value.count));

            // Lossless payload (base64).
            {
                uint64_t value_bytes = 0;
                w.append(kIndent4);
                w.append("<omd:valueBase64>");
                emit_value_base64(arena, e.value, &w, &value_bytes);
                w.append("</omd:valueBase64>\n");
                emit_u64_element(&w, kIndent4, "omd:valueBytes", value_bytes);
                if (e.value.kind == MetaValueKind::Bytes
                    || e.value.kind == MetaValueKind::Text) {
                    emit_text_element(&w, kIndent4, "omd:valueEncoding", "raw");
                } else if (e.value.kind == MetaValueKind::Array
                           || e.value.kind == MetaValueKind::Scalar) {
                    emit_text_element(&w, kIndent4, "omd:valueEncoding", "le");
                }
            }

            if (options.include_origin) {
                emit_u64_element(&w, kIndent4, "omd:originBlock",
                                 static_cast<uint64_t>(e.origin.block));
                emit_u64_element(&w, kIndent4, "omd:originOrder",
                                 static_cast<uint64_t>(e.origin.order_in_block));
            }
            if (options.include_wire) {
                emit_text_element(&w, kIndent4, "omd:wireFamily",
                                  wire_family_name(e.origin.wire_type.family));
                emit_u64_element(&w, kIndent4, "omd:wireTypeCode",
                                 static_cast<uint64_t>(e.origin.wire_type.code));
                emit_u64_element(&w, kIndent4, "omd:wireCount",
                                 static_cast<uint64_t>(e.origin.wire_count));
                if (e.key.kind == MetaKeyKind::ExrAttribute) {
                    std::string_view type_name;
                    if (e.origin.wire_type_name.size > 0U) {
                        type_name = arena_string(arena,
                                                 e.origin.wire_type_name);
                    } else {
                        type_name = exr_wire_type_name(e.origin.wire_type.code);
                    }
                    if (!type_name.empty()) {
                        emit_text_element(&w, kIndent4, "omd:exrTypeName",
                                          type_name);
                    }
                }
            }
            if (options.include_flags) {
                emit_u64_element(&w, kIndent4, "omd:flags",
                                 static_cast<uint64_t>(
                                     static_cast<uint32_t>(e.flags)));
            }

            w.append(kIndent4);
            w.append("</rdf:li>\n");

            emitted += 1;
            if (w.limit_hit) {
                break;
            }
        }
        if (w.limit_hit) {
            break;
        }
    }

    w.append(kIndent4);
    w.append("</rdf:Seq>\n");
    w.append(kIndent3);
    w.append("</omd:entries>\n");
    emit_u64_element(&w, kIndent3, "omd:entriesWritten",
                     static_cast<uint64_t>(emitted));
    emit_xmp_packet_end(&w);

    r.entries = emitted;

    if (w.limit_hit) {
        r.status = XmpDumpStatus::LimitExceeded;
    } else if (w.needed > static_cast<uint64_t>(out.size())) {
        r.status = XmpDumpStatus::OutputTruncated;
    } else {
        r.status = XmpDumpStatus::Ok;
    }

    r.written = (w.written < w.needed) ? w.written : w.needed;
    r.needed  = w.needed;
    return r;
}


namespace {

    static bool is_simple_xmp_property_name(std::string_view s) noexcept
    {
        if (s.empty()) {
            return false;
        }
        if (s.find('/') != std::string_view::npos) {
            return false;
        }
        if (s.find('[') != std::string_view::npos
            || s.find(']') != std::string_view::npos) {
            return false;
        }
        for (size_t i = 0; i < s.size(); ++i) {
            const char c  = s[i];
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                            || (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!ok) {
                return false;
            }
        }
        return true;
    }

    static bool
    split_qualified_xmp_property_name(std::string_view s,
                                      std::string_view* out_prefix,
                                      std::string_view* out_name) noexcept
    {
        if (!out_prefix || !out_name) {
            return false;
        }
        *out_prefix = {};
        *out_name   = {};
        if (s.empty()) {
            return false;
        }

        const size_t colon = s.find(':');
        if (colon == std::string_view::npos) {
            if (!is_simple_xmp_property_name(s)) {
                return false;
            }
            *out_name = s;
            return true;
        }

        if (colon == 0U || colon + 1U >= s.size()) {
            return false;
        }
        if (s.find(':', colon + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view prefix = s.substr(0, colon);
        const std::string_view name   = s.substr(colon + 1U);
        if (!is_simple_xmp_property_name(prefix)
            || !is_simple_xmp_property_name(name)) {
            return false;
        }

        *out_prefix = prefix;
        *out_name   = name;
        return true;
    }

    static bool resolve_qualified_xmp_property_name(
        std::string_view fallback_prefix, std::string_view s,
        std::string_view* out_prefix, std::string_view* out_name) noexcept
    {
        if (!out_prefix || !out_name || fallback_prefix.empty()) {
            return false;
        }
        std::string_view prefix;
        std::string_view name;
        if (!split_qualified_xmp_property_name(s, &prefix, &name)) {
            return false;
        }
        *out_prefix = prefix.empty() ? fallback_prefix : prefix;
        *out_name   = name;
        return true;
    }


    static bool is_makernote_ifd(std::string_view ifd) noexcept
    {
        return ifd.starts_with("mk_");
    }


    static bool ifd_to_portable_prefix(std::string_view ifd,
                                       std::string_view* out_prefix) noexcept
    {
        if (!out_prefix) {
            return false;
        }
        *out_prefix = {};

        if (ifd.empty() || is_makernote_ifd(ifd)) {
            return false;
        }
        if (ifd == "exififd" || ifd.ends_with("_exififd")) {
            *out_prefix = "exif";
            return true;
        }
        if (ifd == "gpsifd" || ifd.ends_with("_gpsifd")) {
            *out_prefix = "exif";
            return true;
        }
        if (ifd == "interopifd" || ifd.ends_with("_interopifd")) {
            *out_prefix = "exif";
            return true;
        }
        if (ifd.starts_with("ifd") || ifd.starts_with("subifd")
            || ifd.starts_with("mkifd") || ifd.starts_with("mk_subifd")) {
            *out_prefix = "tiff";
            return true;
        }
        return false;
    }

    static bool exif_tag_is_nonportable_blob(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x02BC:  // XMLPacket
        case 0x83BB:  // IPTC
        case 0x8649:  // Photoshop
        case 0x8773:  // ICCProfile
        case 0x927C:  // MakerNote
        case 0xC634:  // DNGPrivateData
            return true;
        default: return false;
        }
    }

    static bool xmp_property_is_nonportable_blob(std::string_view prefix,
                                                 std::string_view name) noexcept
    {
        if (name.empty()) {
            return true;
        }
        if (name == "XMLPacket") {
            return true;
        }
        if (prefix == "exif" && name == "MakerNote") {
            return true;
        }
        if (prefix == "tiff"
            && (name == "IPTC" || name == "Photoshop" || name == "ICCProfile"
                || name == "DNGPrivateData")) {
            return true;
        }
        return false;
    }

    static std::string_view
    canonical_portable_property_name(std::string_view prefix,
                                     std::string_view name) noexcept
    {
        if (name.empty()) {
            return {};
        }

        if (prefix == "tiff") {
            if (name == "ImageLength") {
                return "ImageHeight";
            }
            return name;
        }

        if (prefix != "exif") {
            return name;
        }

        if (name == "ExposureBiasValue") {
            return "ExposureCompensation";
        }
        if (name == "ISOSpeedRatings") {
            return "ISO";
        }
        if (name == "PixelXDimension") {
            return "ExifImageWidth";
        }
        if (name == "PixelYDimension") {
            return "ExifImageHeight";
        }
        if (name == "FocalLengthIn35mmFilm") {
            return "FocalLengthIn35mmFormat";
        }
        return name;
    }

    static std::string_view
    portable_property_name_for_exif_tag(std::string_view prefix,
                                        std::string_view ifd, uint16_t tag,
                                        std::string_view fallback_name) noexcept
    {
        (void)ifd;
        (void)tag;
        return canonical_portable_property_name(prefix, fallback_name);
    }

    static std::string_view
    portable_xmp_alias_name_for_exif_tag(std::string_view ifd,
                                         uint16_t tag) noexcept
    {
        const bool is_tiff_ifd = ifd.starts_with("ifd")
                                 || ifd.starts_with("subifd")
                                 || ifd.starts_with("mkifd")
                                 || ifd.starts_with("mk_subifd");
        const bool is_exif_ifd = ifd == "exififd" || ifd.ends_with("_exififd");

        if (is_tiff_ifd && tag == 0x0132U) {
            return "ModifyDate";
        }
        if (is_exif_ifd && tag == 0x9004U) {
            return "CreateDate";
        }
        return {};
    }


    static bool xmp_ns_to_portable_prefix(std::string_view ns,
                                          std::string_view* out_prefix) noexcept
    {
        if (!out_prefix) {
            return false;
        }
        *out_prefix = {};
        if (ns == kXmpNsXmp) {
            *out_prefix = "xmp";
            return true;
        }
        if (ns == kXmpNsTiff) {
            *out_prefix = "tiff";
            return true;
        }
        if (ns == kXmpNsExif) {
            *out_prefix = "exif";
            return true;
        }
        if (ns == kXmpNsExifAux) {
            *out_prefix = "aux";
            return true;
        }
        if (ns == kXmpNsDc) {
            *out_prefix = "dc";
            return true;
        }
        if (ns == kXmpNsPdf) {
            *out_prefix = "pdf";
            return true;
        }
        if (ns == kXmpNsXmpBJ) {
            *out_prefix = "xmpBJ";
            return true;
        }
        if (ns == kXmpNsPlus) {
            *out_prefix = "plus";
            return true;
        }
        if (ns == kXmpNsCrs) {
            *out_prefix = "crs";
            return true;
        }
        if (ns == kXmpNsDng) {
            *out_prefix = "dng";
            return true;
        }
        if (ns == kXmpNsLr) {
            *out_prefix = "lr";
            return true;
        }
        if (ns == kXmpNsXmpDM) {
            *out_prefix = "xmpDM";
            return true;
        }
        if (ns == kXmpNsXmpMM) {
            *out_prefix = "xmpMM";
            return true;
        }
        if (ns == kXmpNsXmpTPg) {
            *out_prefix = "xmpTPg";
            return true;
        }
        if (ns == kXmpNsXmpRights) {
            *out_prefix = "xmpRights";
            return true;
        }
        if (ns == kXmpNsStDim) {
            *out_prefix = "stDim";
            return true;
        }
        if (ns == kXmpNsStEvt) {
            *out_prefix = "stEvt";
            return true;
        }
        if (ns == kXmpNsStFnt) {
            *out_prefix = "stFnt";
            return true;
        }
        if (ns == kXmpNsStJob) {
            *out_prefix = "stJob";
            return true;
        }
        if (ns == kXmpNsStMfs) {
            *out_prefix = "stMfs";
            return true;
        }
        if (ns == kXmpNsStRef) {
            *out_prefix = "stRef";
            return true;
        }
        if (ns == kXmpNsStVer) {
            *out_prefix = "stVer";
            return true;
        }
        if (ns == kXmpNsXmpG) {
            *out_prefix = "xmpG";
            return true;
        }
        if (ns == kXmpNsPhotoshop) {
            *out_prefix = "photoshop";
            return true;
        }
        if (ns == kXmpNsIptc4xmpCore) {
            *out_prefix = "Iptc4xmpCore";
            return true;
        }
        if (ns == kXmpNsIptc4xmpExt) {
            *out_prefix = "Iptc4xmpExt";
            return true;
        }
        return false;
    }

    static bool xmp_namespace_uri_is_xml_attr_safe(std::string_view uri) noexcept
    {
        if (uri.empty()) {
            return false;
        }
        for (size_t i = 0; i < uri.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(uri[i]);
            if (c < 0x20U || c > 0x7EU || c == '"' || c == '&' || c == '<'
                || c == '>') {
                return false;
            }
        }
        return true;
    }

    static bool portable_custom_ns_prefix_for_uri(
        std::string_view ns, std::span<const PortableCustomNsDecl> decls,
        std::string_view* out_prefix) noexcept
    {
        if (!out_prefix) {
            return false;
        }
        *out_prefix = {};
        for (size_t i = 0; i < decls.size(); ++i) {
            if (decls[i].uri == ns) {
                *out_prefix = decls[i].prefix;
                return true;
            }
        }
        return false;
    }

    static bool
    portable_ns_to_prefix(std::string_view ns,
                          std::span<const PortableCustomNsDecl> decls,
                          std::string_view* out_prefix) noexcept
    {
        if (xmp_ns_to_portable_prefix(ns, out_prefix)) {
            return true;
        }
        return portable_custom_ns_prefix_for_uri(ns, decls, out_prefix);
    }

    static std::string_view
    portable_property_name_for_existing_xmp(std::string_view prefix,
                                            std::string_view name) noexcept
    {
        return canonical_portable_property_name(prefix, name);
    }

    static bool resolve_existing_xmp_component_to_portable(
        std::string_view default_prefix, std::string_view component,
        std::string_view* out_component_prefix,
        std::string_view* out_portable_name) noexcept
    {
        if (!out_component_prefix || !out_portable_name
            || default_prefix.empty() || component.empty()) {
            return false;
        }
        *out_component_prefix = {};
        *out_portable_name    = {};

        std::string_view raw_prefix;
        std::string_view raw_name;
        if (!split_qualified_xmp_property_name(component, &raw_prefix,
                                               &raw_name)) {
            return false;
        }

        const std::string_view portable_prefix = raw_prefix.empty()
                                                     ? default_prefix
                                                     : raw_prefix;
        const std::string_view portable_name
            = portable_property_name_for_existing_xmp(portable_prefix,
                                                      raw_name);
        if (portable_name.empty()) {
            return false;
        }

        *out_component_prefix = portable_prefix;
        *out_portable_name    = portable_name;
        return true;
    }

    static bool
    standard_existing_xmp_st_ref_child_name(std::string_view child) noexcept
    {
        return child == "documentID" || child == "instanceID"
               || child == "filePath" || child == "fromPart"
               || child == "lastModifyDate" || child == "manageTo"
               || child == "manageUI" || child == "manager"
               || child == "managerVariant" || child == "maskMarkers"
               || child == "partMapping" || child == "renditionClass"
               || child == "renditionParams" || child == "toPart";
    }

    static bool
    standard_existing_xmp_st_dim_child_name(std::string_view child) noexcept
    {
        return child == "w" || child == "h" || child == "unit";
    }

    static bool
    standard_existing_xmp_st_job_child_name(std::string_view child) noexcept
    {
        return child == "id" || child == "name" || child == "url";
    }

    static bool
    standard_existing_xmp_st_fnt_child_name(std::string_view child) noexcept
    {
        return child == "fontName" || child == "childFontFiles";
    }

    static bool
    standard_existing_xmp_st_mfs_child_name(std::string_view child) noexcept
    {
        return child == "linkForm" || child == "reference";
    }

    static bool
    standard_existing_xmp_st_ver_child_name(std::string_view child) noexcept
    {
        return child == "version" || child == "comments" || child == "modifier"
               || child == "modifyDate" || child == "event";
    }

    static bool
    standard_existing_xmp_st_evt_child_name(std::string_view child) noexcept
    {
        return child == "action" || child == "changed" || child == "instanceID"
               || child == "parameters" || child == "softwareAgent"
               || child == "when";
    }

    static bool
    standard_existing_xmp_xmp_g_child_name(std::string_view child) noexcept
    {
        return child == "groupName" || child == "groupType"
               || child == "swatchName" || child == "mode" || child == "red"
               || child == "green" || child == "blue";
    }

    static std::string_view standard_existing_xmp_qualified_component_literal(
        std::string_view prefix, std::string_view child) noexcept
    {
        if (prefix == "stRef") {
            if (child == "documentID") {
                return "stRef:documentID";
            }
            if (child == "instanceID") {
                return "stRef:instanceID";
            }
            if (child == "filePath") {
                return "stRef:filePath";
            }
            if (child == "fromPart") {
                return "stRef:fromPart";
            }
            if (child == "lastModifyDate") {
                return "stRef:lastModifyDate";
            }
            if (child == "manageTo") {
                return "stRef:manageTo";
            }
            if (child == "manageUI") {
                return "stRef:manageUI";
            }
            if (child == "manager") {
                return "stRef:manager";
            }
            if (child == "managerVariant") {
                return "stRef:managerVariant";
            }
            if (child == "maskMarkers") {
                return "stRef:maskMarkers";
            }
            if (child == "partMapping") {
                return "stRef:partMapping";
            }
            if (child == "renditionClass") {
                return "stRef:renditionClass";
            }
            if (child == "renditionParams") {
                return "stRef:renditionParams";
            }
            if (child == "toPart") {
                return "stRef:toPart";
            }
        }
        if (prefix == "stDim") {
            if (child == "w") {
                return "stDim:w";
            }
            if (child == "h") {
                return "stDim:h";
            }
            if (child == "unit") {
                return "stDim:unit";
            }
        }
        if (prefix == "stJob") {
            if (child == "id") {
                return "stJob:id";
            }
            if (child == "name") {
                return "stJob:name";
            }
            if (child == "url") {
                return "stJob:url";
            }
        }
        if (prefix == "stFnt") {
            if (child == "fontName") {
                return "stFnt:fontName";
            }
            if (child == "childFontFiles") {
                return "stFnt:childFontFiles";
            }
        }
        if (prefix == "stMfs") {
            if (child == "linkForm") {
                return "stMfs:linkForm";
            }
            if (child == "reference") {
                return "stMfs:reference";
            }
        }
        if (prefix == "stVer") {
            if (child == "version") {
                return "stVer:version";
            }
            if (child == "comments") {
                return "stVer:comments";
            }
            if (child == "modifier") {
                return "stVer:modifier";
            }
            if (child == "modifyDate") {
                return "stVer:modifyDate";
            }
            if (child == "event") {
                return "stVer:event";
            }
        }
        if (prefix == "stEvt") {
            if (child == "action") {
                return "stEvt:action";
            }
            if (child == "changed") {
                return "stEvt:changed";
            }
            if (child == "instanceID") {
                return "stEvt:instanceID";
            }
            if (child == "parameters") {
                return "stEvt:parameters";
            }
            if (child == "softwareAgent") {
                return "stEvt:softwareAgent";
            }
            if (child == "when") {
                return "stEvt:when";
            }
        }
        if (prefix == "xmpG") {
            if (child == "groupName") {
                return "xmpG:groupName";
            }
            if (child == "groupType") {
                return "xmpG:groupType";
            }
            if (child == "swatchName") {
                return "xmpG:swatchName";
            }
            if (child == "mode") {
                return "xmpG:mode";
            }
            if (child == "red") {
                return "xmpG:red";
            }
            if (child == "green") {
                return "xmpG:green";
            }
            if (child == "blue") {
                return "xmpG:blue";
            }
        }
        return {};
    }

    static bool standard_existing_xmp_normalize_structured_child_prefix(
        std::string_view prefix, std::string_view base,
        std::string_view* child_prefix, std::string_view* child) noexcept
    {
        if (!child_prefix || !child || child->empty()
            || *child_prefix != prefix) {
            return false;
        }

        std::string_view canonical_prefix;
        if (prefix == "xmpBJ" && base == "JobRef"
            && standard_existing_xmp_st_job_child_name(*child)) {
            canonical_prefix = "stJob";
        } else if (prefix == "Iptc4xmpExt"
                   && (base == "LocationShown" || base == "LocationCreated")
                   && *child == "Identifier") {
            canonical_prefix = "xmp";
        } else if (prefix == "Iptc4xmpExt"
                   && (base == "LocationShown" || base == "LocationCreated")
                   && (*child == "GPSLatitude" || *child == "GPSLongitude"
                       || *child == "GPSAltitude"
                       || *child == "GPSAltitudeRef")) {
            canonical_prefix = "exif";
        } else if (((prefix == "xmpTPg" && base == "MaxPageSize")
                    || (prefix == "xmpDM" && base == "videoFrameSize"))
                   && standard_existing_xmp_st_dim_child_name(*child)) {
            canonical_prefix = "stDim";
        } else if (prefix == "xmpTPg" && base == "Fonts"
                   && standard_existing_xmp_st_fnt_child_name(*child)) {
            canonical_prefix = "stFnt";
        } else if (prefix == "xmpTPg"
                   && (base == "Colorants" || base == "SwatchGroups")
                   && standard_existing_xmp_xmp_g_child_name(*child)) {
            canonical_prefix = "xmpG";
        } else if (prefix == "xmpDM" && base == "videoAlphaPremultipleColor"
                   && standard_existing_xmp_xmp_g_child_name(*child)) {
            canonical_prefix = "xmpG";
        } else if (prefix == "xmpMM" && base == "Pantry"
                   && *child == "format") {
            canonical_prefix = "dc";
        } else if (prefix == "xmpMM"
                   && (base == "DerivedFrom" || base == "ManagedFrom"
                       || base == "RenditionOf" || base == "Ingredients")
                   && standard_existing_xmp_st_ref_child_name(*child)) {
            canonical_prefix = "stRef";
        } else if (prefix == "xmpMM" && base == "Manifest"
                   && standard_existing_xmp_st_mfs_child_name(*child)) {
            canonical_prefix = "stMfs";
        } else if (prefix == "xmpMM" && base == "Versions"
                   && standard_existing_xmp_st_ver_child_name(*child)) {
            canonical_prefix = "stVer";
        } else if (prefix == "xmpMM" && base == "History"
                   && standard_existing_xmp_st_evt_child_name(*child)) {
            canonical_prefix = "stEvt";
        } else {
            return false;
        }

        *child_prefix = canonical_prefix;
        return true;
    }

    static bool standard_existing_xmp_normalize_nested_grandchild_prefix(
        std::string_view prefix, std::string_view base,
        std::string_view child_prefix, std::string_view child,
        std::string_view* grandchild_prefix,
        std::string_view* grandchild) noexcept
    {
        if (!grandchild_prefix || !grandchild || grandchild->empty()
            || *grandchild_prefix != prefix) {
            return false;
        }

        std::string_view canonical_prefix;
        if (prefix == "xmpMM" && base == "Manifest" && child_prefix == "stMfs"
            && child == "reference"
            && standard_existing_xmp_st_ref_child_name(*grandchild)) {
            canonical_prefix = "stRef";
        } else if (prefix == "xmpMM" && base == "Versions"
                   && child_prefix == "stVer" && child == "event"
                   && standard_existing_xmp_st_evt_child_name(*grandchild)) {
            canonical_prefix = "stEvt";
        } else if (prefix == "xmpTPg" && base == "SwatchGroups"
                   && child == "Colorants"
                   && standard_existing_xmp_xmp_g_child_name(*grandchild)) {
            canonical_prefix = "xmpG";
        } else {
            return false;
        }

        *grandchild_prefix = canonical_prefix;
        return true;
    }

    static bool resolve_existing_xmp_structured_child_to_portable(
        std::string_view prefix, std::string_view base,
        std::string_view component, std::string_view* out_component_prefix,
        std::string_view* out_portable_name, bool* out_normalized) noexcept
    {
        if (!resolve_existing_xmp_component_to_portable(prefix, component,
                                                        out_component_prefix,
                                                        out_portable_name)) {
            return false;
        }

        bool normalized
            = standard_existing_xmp_normalize_structured_child_prefix(
                prefix, base, out_component_prefix, out_portable_name);
        if (out_normalized) {
            *out_normalized = normalized;
        }
        return true;
    }

    static bool resolve_existing_xmp_nested_grandchild_to_portable(
        std::string_view prefix, std::string_view base,
        std::string_view child_prefix, std::string_view child,
        std::string_view component, std::string_view* out_component_prefix,
        std::string_view* out_portable_name, bool* out_normalized) noexcept
    {
        if (!resolve_existing_xmp_component_to_portable(prefix, component,
                                                        out_component_prefix,
                                                        out_portable_name)) {
            return false;
        }

        bool normalized
            = standard_existing_xmp_normalize_nested_grandchild_prefix(
                prefix, base, child_prefix, child, out_component_prefix,
                out_portable_name);
        if (out_normalized) {
            *out_normalized = normalized;
        }
        return true;
    }

    static bool existing_standard_portable_property_is_managed(
        std::string_view prefix, std::string_view name) noexcept
    {
        if (prefix == "tiff" || prefix == "exif") {
            return !name.empty();
        }

        if (prefix == "xmp") {
            return name == "ModifyDate" || name == "CreateDate";
        }

        if (prefix == "dc") {
            return name == "title" || name == "subject" || name == "creator"
                   || name == "rights" || name == "description";
        }

        if (prefix == "photoshop") {
            return name == "DateCreated" || name == "Category"
                   || name == "SupplementalCategories" || name == "Instructions"
                   || name == "AuthorsPosition" || name == "City"
                   || name == "State" || name == "Country"
                   || name == "TransmissionReference" || name == "Headline"
                   || name == "Credit" || name == "Source"
                   || name == "CaptionWriter";
        }

        if (prefix == "Iptc4xmpCore") {
            return name == "Location" || name == "CountryCode"
                   || name == "LocationCreated";
        }

        return false;
    }

    static bool parse_indexed_xmp_property_name(std::string_view path,
                                                std::string_view* out_base,
                                                uint32_t* out_index) noexcept
    {
        if (!out_base || !out_index) {
            return false;
        }
        *out_base  = {};
        *out_index = 0U;

        const size_t lb = path.rfind('[');
        if (lb == std::string_view::npos || lb + 2U >= path.size()
            || path.back() != ']') {
            return false;
        }

        const std::string_view base = path.substr(0, lb);
        std::string_view base_prefix;
        std::string_view base_name;
        if (!split_qualified_xmp_property_name(base, &base_prefix, &base_name)) {
            return false;
        }

        const std::string_view idx = path.substr(lb + 1U,
                                                 path.size() - lb - 2U);
        uint64_t parsed_idx        = 0U;
        for (size_t i = 0; i < idx.size(); ++i) {
            const char c = idx[i];
            if (c < '0' || c > '9') {
                return false;
            }
            parsed_idx = parsed_idx * 10U + static_cast<uint64_t>(c - '0');
            if (parsed_idx > UINT32_MAX) {
                return false;
            }
        }
        if (parsed_idx == 0U) {
            return false;
        }

        *out_base  = base;
        *out_index = static_cast<uint32_t>(parsed_idx);
        return true;
    }

    static bool xmp_lang_value_is_safe(std::string_view s) noexcept
    {
        if (s.empty()) {
            return false;
        }
        for (size_t i = 0; i < s.size(); ++i) {
            const char c  = s[i];
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                            || (c >= '0' && c <= '9') || c == '-';
            if (!ok) {
                return false;
            }
        }
        return true;
    }

    static bool
    parse_lang_alt_xmp_property_name(std::string_view path,
                                     std::string_view* out_base,
                                     std::string_view* out_lang) noexcept
    {
        if (!out_base || !out_lang) {
            return false;
        }
        *out_base = {};
        *out_lang = {};

        static constexpr std::string_view kMarker = "[@xml:lang=";
        const size_t lb                           = path.rfind(kMarker);
        if (lb == std::string_view::npos || !path.ends_with(']')) {
            return false;
        }

        const std::string_view base = path.substr(0, lb);
        std::string_view base_prefix;
        std::string_view base_name;
        if (!split_qualified_xmp_property_name(base, &base_prefix, &base_name)) {
            return false;
        }

        const size_t lang_begin = lb + kMarker.size();
        if (lang_begin >= path.size() - 1U) {
            return false;
        }

        const std::string_view lang
            = path.substr(lang_begin, path.size() - lang_begin - 1U);
        if (!xmp_lang_value_is_safe(lang)) {
            return false;
        }

        *out_base = base;
        *out_lang = lang;
        return true;
    }

    static bool
    parse_structured_xmp_property_name(std::string_view path,
                                       std::string_view* out_base,
                                       std::string_view* out_child) noexcept
    {
        if (!out_base || !out_child) {
            return false;
        }
        *out_base  = {};
        *out_child = {};

        const size_t slash = path.find('/');
        if (slash == std::string_view::npos || slash == 0U
            || slash + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view base  = path.substr(0, slash);
        const std::string_view child = path.substr(slash + 1U);
        std::string_view child_prefix;
        std::string_view child_name;
        if (!is_simple_xmp_property_name(base)
            || !split_qualified_xmp_property_name(child, &child_prefix,
                                                  &child_name)) {
            return false;
        }

        *out_base  = base;
        *out_child = child;
        return true;
    }

    static bool parse_indexed_structured_xmp_property_name(
        std::string_view path, std::string_view* out_base, uint32_t* out_index,
        std::string_view* out_child) noexcept
    {
        if (!out_base || !out_index || !out_child) {
            return false;
        }
        *out_base  = {};
        *out_index = 0U;
        *out_child = {};

        const size_t slash = path.find('/');
        if (slash == std::string_view::npos || slash == 0U
            || slash + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view left  = path.substr(0, slash);
        const std::string_view child = path.substr(slash + 1U);
        std::string_view base;
        uint32_t index = 0U;
        std::string_view child_prefix;
        std::string_view child_name;
        if (!parse_indexed_xmp_property_name(left, &base, &index)
            || !split_qualified_xmp_property_name(child, &child_prefix,
                                                  &child_name)) {
            return false;
        }

        *out_base  = base;
        *out_index = index;
        *out_child = child;
        return true;
    }

    static bool parse_structured_lang_alt_xmp_property_name(
        std::string_view path, std::string_view* out_base,
        std::string_view* out_child, std::string_view* out_lang) noexcept
    {
        if (!out_base || !out_child || !out_lang) {
            return false;
        }
        *out_base  = {};
        *out_child = {};
        *out_lang  = {};

        const size_t slash = path.find('/');
        if (slash == std::string_view::npos || slash == 0U
            || slash + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view base = path.substr(0, slash);
        std::string_view child;
        std::string_view child_prefix;
        std::string_view child_name;
        std::string_view lang;
        if (!is_simple_xmp_property_name(base)
            || !parse_lang_alt_xmp_property_name(path.substr(slash + 1U),
                                                 &child, &lang)) {
            return false;
        }
        if (!split_qualified_xmp_property_name(child, &child_prefix,
                                               &child_name)) {
            return false;
        }

        *out_base  = base;
        *out_child = child;
        *out_lang  = lang;
        return true;
    }

    static bool parse_indexed_structured_lang_alt_xmp_property_name(
        std::string_view path, std::string_view* out_base, uint32_t* out_index,
        std::string_view* out_child, std::string_view* out_lang) noexcept
    {
        if (!out_base || !out_index || !out_child || !out_lang) {
            return false;
        }
        *out_base  = {};
        *out_index = 0U;
        *out_child = {};
        *out_lang  = {};

        const size_t slash = path.find('/');
        if (slash == std::string_view::npos || slash == 0U
            || slash + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view left = path.substr(0, slash);
        std::string_view base;
        uint32_t index = 0U;
        std::string_view child;
        std::string_view child_prefix;
        std::string_view child_name;
        std::string_view lang;
        if (!parse_indexed_xmp_property_name(left, &base, &index)
            || !parse_lang_alt_xmp_property_name(path.substr(slash + 1U),
                                                 &child, &lang)) {
            return false;
        }
        if (!split_qualified_xmp_property_name(child, &child_prefix,
                                               &child_name)) {
            return false;
        }

        *out_base  = base;
        *out_index = index;
        *out_child = child;
        *out_lang  = lang;
        return true;
    }

    static bool parse_structured_indexed_xmp_property_name(
        std::string_view path, std::string_view* out_base,
        std::string_view* out_child, uint32_t* out_index) noexcept
    {
        if (!out_base || !out_child || !out_index) {
            return false;
        }
        *out_base  = {};
        *out_child = {};
        *out_index = 0U;

        const size_t slash = path.find('/');
        if (slash == std::string_view::npos || slash == 0U
            || slash + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view base = path.substr(0, slash);
        std::string_view child;
        std::string_view child_prefix;
        std::string_view child_name;
        uint32_t index = 0U;
        if (!is_simple_xmp_property_name(base)
            || !parse_indexed_xmp_property_name(path.substr(slash + 1U), &child,
                                                &index)) {
            return false;
        }
        if (!split_qualified_xmp_property_name(child, &child_prefix,
                                               &child_name)) {
            return false;
        }

        *out_base  = base;
        *out_child = child;
        *out_index = index;
        return true;
    }

    static bool parse_indexed_structured_indexed_xmp_property_name(
        std::string_view path, std::string_view* out_base,
        uint32_t* out_item_index, std::string_view* out_child,
        uint32_t* out_child_index) noexcept
    {
        if (!out_base || !out_item_index || !out_child || !out_child_index) {
            return false;
        }
        *out_base        = {};
        *out_item_index  = 0U;
        *out_child       = {};
        *out_child_index = 0U;

        const size_t slash = path.find('/');
        if (slash == std::string_view::npos || slash == 0U
            || slash + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view left = path.substr(0, slash);
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child;
        std::string_view child_prefix;
        std::string_view child_name;
        uint32_t child_index = 0U;
        if (!parse_indexed_xmp_property_name(left, &base, &item_index)
            || !parse_indexed_xmp_property_name(path.substr(slash + 1U), &child,
                                                &child_index)) {
            return false;
        }
        if (!split_qualified_xmp_property_name(child, &child_prefix,
                                               &child_name)) {
            return false;
        }

        *out_base        = base;
        *out_item_index  = item_index;
        *out_child       = child;
        *out_child_index = child_index;
        return true;
    }

    static bool parse_nested_structured_xmp_property_name(
        std::string_view path, std::string_view* out_base,
        std::string_view* out_child, std::string_view* out_grandchild) noexcept
    {
        if (!out_base || !out_child || !out_grandchild) {
            return false;
        }
        *out_base       = {};
        *out_child      = {};
        *out_grandchild = {};

        const size_t slash1 = path.find('/');
        if (slash1 == std::string_view::npos || slash1 == 0U
            || slash1 + 1U >= path.size()) {
            return false;
        }
        const size_t slash2 = path.find('/', slash1 + 1U);
        if (slash2 == std::string_view::npos || slash2 == slash1 + 1U
            || slash2 + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash2 + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view base       = path.substr(0, slash1);
        const std::string_view child      = path.substr(slash1 + 1U,
                                                        slash2 - slash1 - 1U);
        const std::string_view grandchild = path.substr(slash2 + 1U);
        std::string_view child_prefix;
        std::string_view child_name;
        std::string_view grandchild_prefix;
        std::string_view grandchild_name;
        if (!is_simple_xmp_property_name(base)
            || !split_qualified_xmp_property_name(child, &child_prefix,
                                                  &child_name)
            || !split_qualified_xmp_property_name(grandchild,
                                                  &grandchild_prefix,
                                                  &grandchild_name)) {
            return false;
        }

        *out_base       = base;
        *out_child      = child;
        *out_grandchild = grandchild;
        return true;
    }

    static bool parse_nested_structured_lang_alt_xmp_property_name(
        std::string_view path, std::string_view* out_base,
        std::string_view* out_child, std::string_view* out_grandchild,
        std::string_view* out_lang) noexcept
    {
        if (!out_base || !out_child || !out_grandchild || !out_lang) {
            return false;
        }
        *out_base       = {};
        *out_child      = {};
        *out_grandchild = {};
        *out_lang       = {};

        const size_t slash1 = path.find('/');
        if (slash1 == std::string_view::npos || slash1 == 0U
            || slash1 + 1U >= path.size()) {
            return false;
        }
        const size_t slash2 = path.find('/', slash1 + 1U);
        if (slash2 == std::string_view::npos || slash2 == slash1 + 1U
            || slash2 + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash2 + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view base  = path.substr(0, slash1);
        const std::string_view child = path.substr(slash1 + 1U,
                                                   slash2 - slash1 - 1U);
        std::string_view child_prefix;
        std::string_view child_name;
        std::string_view grandchild;
        std::string_view lang;
        if (!is_simple_xmp_property_name(base)
            || !split_qualified_xmp_property_name(child, &child_prefix,
                                                  &child_name)
            || !parse_lang_alt_xmp_property_name(path.substr(slash2 + 1U),
                                                 &grandchild, &lang)) {
            return false;
        }

        *out_base       = base;
        *out_child      = child;
        *out_grandchild = grandchild;
        *out_lang       = lang;
        return true;
    }

    static bool parse_nested_structured_indexed_xmp_property_name(
        std::string_view path, std::string_view* out_base,
        std::string_view* out_child, std::string_view* out_grandchild,
        uint32_t* out_grandchild_index) noexcept
    {
        if (!out_base || !out_child || !out_grandchild
            || !out_grandchild_index) {
            return false;
        }
        *out_base             = {};
        *out_child            = {};
        *out_grandchild       = {};
        *out_grandchild_index = 0U;

        const size_t slash1 = path.find('/');
        if (slash1 == std::string_view::npos || slash1 == 0U
            || slash1 + 1U >= path.size()) {
            return false;
        }
        const size_t slash2 = path.find('/', slash1 + 1U);
        if (slash2 == std::string_view::npos || slash2 == slash1 + 1U
            || slash2 + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash2 + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view base  = path.substr(0, slash1);
        const std::string_view child = path.substr(slash1 + 1U,
                                                   slash2 - slash1 - 1U);
        std::string_view child_prefix;
        std::string_view child_name;
        std::string_view grandchild;
        uint32_t grandchild_index = 0U;
        if (!is_simple_xmp_property_name(base)
            || !split_qualified_xmp_property_name(child, &child_prefix,
                                                  &child_name)
            || !parse_indexed_xmp_property_name(path.substr(slash2 + 1U),
                                                &grandchild,
                                                &grandchild_index)) {
            return false;
        }

        *out_base             = base;
        *out_child            = child;
        *out_grandchild       = grandchild;
        *out_grandchild_index = grandchild_index;
        return true;
    }

    static bool parse_indexed_nested_structured_xmp_property_name(
        std::string_view path, std::string_view* out_base,
        uint32_t* out_item_index, std::string_view* out_child,
        std::string_view* out_grandchild) noexcept
    {
        if (!out_base || !out_item_index || !out_child || !out_grandchild) {
            return false;
        }
        *out_base       = {};
        *out_item_index = 0U;
        *out_child      = {};
        *out_grandchild = {};

        const size_t slash1 = path.find('/');
        if (slash1 == std::string_view::npos || slash1 == 0U
            || slash1 + 1U >= path.size()) {
            return false;
        }
        const size_t slash2 = path.find('/', slash1 + 1U);
        if (slash2 == std::string_view::npos || slash2 == slash1 + 1U
            || slash2 + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash2 + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view left = path.substr(0, slash1);
        std::string_view base;
        uint32_t item_index = 0U;
        if (!parse_indexed_xmp_property_name(left, &base, &item_index)) {
            return false;
        }

        const std::string_view child      = path.substr(slash1 + 1U,
                                                        slash2 - slash1 - 1U);
        const std::string_view grandchild = path.substr(slash2 + 1U);
        std::string_view child_prefix;
        std::string_view child_name;
        std::string_view grandchild_prefix;
        std::string_view grandchild_name;
        if (!split_qualified_xmp_property_name(child, &child_prefix, &child_name)
            || !split_qualified_xmp_property_name(grandchild,
                                                  &grandchild_prefix,
                                                  &grandchild_name)) {
            return false;
        }

        *out_base       = base;
        *out_item_index = item_index;
        *out_child      = child;
        *out_grandchild = grandchild;
        return true;
    }

    static bool parse_indexed_nested_structured_lang_alt_xmp_property_name(
        std::string_view path, std::string_view* out_base,
        uint32_t* out_item_index, std::string_view* out_child,
        std::string_view* out_grandchild, std::string_view* out_lang) noexcept
    {
        if (!out_base || !out_item_index || !out_child || !out_grandchild
            || !out_lang) {
            return false;
        }
        *out_base       = {};
        *out_item_index = 0U;
        *out_child      = {};
        *out_grandchild = {};
        *out_lang       = {};

        const size_t slash1 = path.find('/');
        if (slash1 == std::string_view::npos || slash1 == 0U
            || slash1 + 1U >= path.size()) {
            return false;
        }
        const size_t slash2 = path.find('/', slash1 + 1U);
        if (slash2 == std::string_view::npos || slash2 == slash1 + 1U
            || slash2 + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash2 + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view left = path.substr(0, slash1);
        std::string_view base;
        uint32_t item_index          = 0U;
        const std::string_view child = path.substr(slash1 + 1U,
                                                   slash2 - slash1 - 1U);
        std::string_view child_prefix;
        std::string_view child_name;
        std::string_view grandchild;
        std::string_view lang;
        if (!parse_indexed_xmp_property_name(left, &base, &item_index)
            || !split_qualified_xmp_property_name(child, &child_prefix,
                                                  &child_name)
            || !parse_lang_alt_xmp_property_name(path.substr(slash2 + 1U),
                                                 &grandchild, &lang)) {
            return false;
        }

        *out_base       = base;
        *out_item_index = item_index;
        *out_child      = child;
        *out_grandchild = grandchild;
        *out_lang       = lang;
        return true;
    }

    static bool parse_indexed_nested_structured_indexed_xmp_property_name(
        std::string_view path, std::string_view* out_base,
        uint32_t* out_item_index, std::string_view* out_child,
        std::string_view* out_grandchild,
        uint32_t* out_grandchild_index) noexcept
    {
        if (!out_base || !out_item_index || !out_child || !out_grandchild
            || !out_grandchild_index) {
            return false;
        }
        *out_base             = {};
        *out_item_index       = 0U;
        *out_child            = {};
        *out_grandchild       = {};
        *out_grandchild_index = 0U;

        const size_t slash1 = path.find('/');
        if (slash1 == std::string_view::npos || slash1 == 0U
            || slash1 + 1U >= path.size()) {
            return false;
        }
        const size_t slash2 = path.find('/', slash1 + 1U);
        if (slash2 == std::string_view::npos || slash2 == slash1 + 1U
            || slash2 + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash2 + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view left = path.substr(0, slash1);
        std::string_view base;
        uint32_t item_index          = 0U;
        const std::string_view child = path.substr(slash1 + 1U,
                                                   slash2 - slash1 - 1U);
        std::string_view child_prefix;
        std::string_view child_name;
        std::string_view grandchild;
        uint32_t grandchild_index = 0U;
        if (!parse_indexed_xmp_property_name(left, &base, &item_index)
            || !split_qualified_xmp_property_name(child, &child_prefix,
                                                  &child_name)
            || !parse_indexed_xmp_property_name(path.substr(slash2 + 1U),
                                                &grandchild,
                                                &grandchild_index)) {
            return false;
        }

        *out_base             = base;
        *out_item_index       = item_index;
        *out_child            = child;
        *out_grandchild       = grandchild;
        *out_grandchild_index = grandchild_index;
        return true;
    }

    static bool parse_indexed_nested_structured_deep_xmp_property_name(
        std::string_view path, std::string_view* out_base,
        uint32_t* out_item_index, std::string_view* out_child,
        std::string_view* out_grandchild, std::string_view* out_leaf) noexcept
    {
        if (!out_base || !out_item_index || !out_child || !out_grandchild
            || !out_leaf) {
            return false;
        }
        *out_base       = {};
        *out_item_index = 0U;
        *out_child      = {};
        *out_grandchild = {};
        *out_leaf       = {};

        const size_t slash1 = path.find('/');
        if (slash1 == std::string_view::npos || slash1 == 0U
            || slash1 + 1U >= path.size()) {
            return false;
        }
        const size_t slash2 = path.find('/', slash1 + 1U);
        if (slash2 == std::string_view::npos || slash2 == slash1 + 1U
            || slash2 + 1U >= path.size()) {
            return false;
        }
        const size_t slash3 = path.find('/', slash2 + 1U);
        if (slash3 == std::string_view::npos || slash3 == slash2 + 1U
            || slash3 + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash3 + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view left = path.substr(0, slash1);
        std::string_view base;
        uint32_t item_index = 0U;
        if (!parse_indexed_xmp_property_name(left, &base, &item_index)) {
            return false;
        }

        const std::string_view child      = path.substr(slash1 + 1U,
                                                        slash2 - slash1 - 1U);
        const std::string_view grandchild = path.substr(slash2 + 1U,
                                                        slash3 - slash2 - 1U);
        const std::string_view leaf       = path.substr(slash3 + 1U);
        std::string_view child_prefix;
        std::string_view child_name;
        std::string_view grandchild_prefix;
        std::string_view grandchild_name;
        std::string_view leaf_prefix;
        std::string_view leaf_name;
        if (!split_qualified_xmp_property_name(child, &child_prefix, &child_name)
            || !split_qualified_xmp_property_name(grandchild,
                                                  &grandchild_prefix,
                                                  &grandchild_name)
            || !split_qualified_xmp_property_name(leaf, &leaf_prefix,
                                                  &leaf_name)) {
            return false;
        }

        *out_base       = base;
        *out_item_index = item_index;
        *out_child      = child;
        *out_grandchild = grandchild;
        *out_leaf       = leaf;
        return true;
    }

    static bool parse_indexed_structured_indexed_nested_xmp_property_name(
        std::string_view path, std::string_view* out_base,
        uint32_t* out_item_index, std::string_view* out_child,
        uint32_t* out_child_index, std::string_view* out_grandchild) noexcept
    {
        if (!out_base || !out_item_index || !out_child || !out_child_index
            || !out_grandchild) {
            return false;
        }
        *out_base        = {};
        *out_item_index  = 0U;
        *out_child       = {};
        *out_child_index = 0U;
        *out_grandchild  = {};

        const size_t slash1 = path.find('/');
        if (slash1 == std::string_view::npos || slash1 == 0U
            || slash1 + 1U >= path.size()) {
            return false;
        }
        const size_t slash2 = path.find('/', slash1 + 1U);
        if (slash2 == std::string_view::npos || slash2 == slash1 + 1U
            || slash2 + 1U >= path.size()) {
            return false;
        }
        if (path.find('/', slash2 + 1U) != std::string_view::npos) {
            return false;
        }

        const std::string_view left = path.substr(0, slash1);
        std::string_view base;
        uint32_t item_index = 0U;
        if (!parse_indexed_xmp_property_name(left, &base, &item_index)) {
            return false;
        }

        std::string_view child;
        uint32_t child_index              = 0U;
        const std::string_view grandchild = path.substr(slash2 + 1U);
        std::string_view grandchild_prefix;
        std::string_view grandchild_name;
        if (!parse_indexed_xmp_property_name(path.substr(slash1 + 1U,
                                                         slash2 - slash1 - 1U),
                                             &child, &child_index)
            || !split_qualified_xmp_property_name(grandchild,
                                                  &grandchild_prefix,
                                                  &grandchild_name)) {
            return false;
        }

        *out_base        = base;
        *out_item_index  = item_index;
        *out_child       = child;
        *out_child_index = child_index;
        *out_grandchild  = grandchild;
        return true;
    }

    static bool bytes_are_ascii_text(std::span<const std::byte> raw) noexcept
    {
        for (size_t i = 0; i < raw.size(); ++i) {
            const uint8_t c = static_cast<uint8_t>(raw[i]);
            if (c == 0U) {
                return false;
            }
            if (c < 0x20U || c > 0x7EU) {
                return false;
            }
        }
        return true;
    }


    static void append_i64_dec(int64_t v, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        w->append(buf);
    }


    static void append_f64_dec(double v, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        if (!std::isfinite(v)) {
            w->append("0");
            return;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        w->append(buf);
    }


    static void append_rational_text(const URational& r, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }

        if (r.denom == 0U) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%u/%u",
                          static_cast<unsigned>(r.numer),
                          static_cast<unsigned>(r.denom));
            w->append(buf);
            return;
        }

        uint32_t n = r.numer;
        uint32_t d = r.denom;
        while (d != 0U) {
            const uint32_t t = n % d;
            n                = d;
            d                = t;
        }
        const uint32_t gcd = (n == 0U) ? 1U : n;
        const uint32_t rn  = r.numer / gcd;
        const uint32_t rd  = r.denom / gcd;

        if (rd == 1U) {
            append_u64_dec(static_cast<uint64_t>(rn), w);
            return;
        }

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%u/%u", static_cast<unsigned>(rn),
                      static_cast<unsigned>(rd));
        w->append(buf);
    }


    static void append_rational_text(const SRational& r, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }

        const int64_t n0 = static_cast<int64_t>(r.numer);
        const int64_t d0 = static_cast<int64_t>(r.denom);
        if (d0 == 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%lld/%lld",
                          static_cast<long long>(n0),
                          static_cast<long long>(d0));
            w->append(buf);
            return;
        }

        int64_t n = n0;
        int64_t d = d0;
        if (d < 0) {
            n = -n;
            d = -d;
        }

        uint64_t an = (n < 0) ? static_cast<uint64_t>(-n)
                              : static_cast<uint64_t>(n);
        uint64_t ad = static_cast<uint64_t>(d);
        while (ad != 0U) {
            const uint64_t t = an % ad;
            an               = ad;
            ad               = t;
        }
        const uint64_t gcd = (an == 0U) ? 1U : an;
        const int64_t rn   = n / static_cast<int64_t>(gcd);
        const int64_t rd   = d / static_cast<int64_t>(gcd);

        if (rd == 1) {
            append_i64_dec(rn, w);
            return;
        }

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%lld/%lld", static_cast<long long>(rn),
                      static_cast<long long>(rd));
        w->append(buf);
    }


    static void emit_portable_scalar_text(const MetaValue& v,
                                          SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }

        switch (v.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64: append_u64_dec(v.data.u64, w); return;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64: append_i64_dec(v.data.i64, w); return;
        case MetaElementType::F32: {
            float f = 0.0f;
            std::memcpy(&f, &v.data.f32_bits, sizeof(f));
            append_f64_dec(static_cast<double>(f), w);
            return;
        }
        case MetaElementType::F64: {
            double d = 0.0;
            std::memcpy(&d, &v.data.f64_bits, sizeof(d));
            append_f64_dec(d, w);
            return;
        }
        case MetaElementType::URational:
            append_rational_text(v.data.ur, w);
            return;
        case MetaElementType::SRational:
            append_rational_text(v.data.sr, w);
            return;
        }
    }


    static bool emit_portable_value_inline(const ByteArena& arena,
                                           const MetaValue& v,
                                           SpanWriter* w) noexcept
    {
        if (!w) {
            return false;
        }

        switch (v.kind) {
        case MetaValueKind::Empty: return false;
        case MetaValueKind::Text: {
            const std::string_view s = arena_string(arena, v.data.span);
            if (v.text_encoding == TextEncoding::Utf8) {
                append_xml_safe_utf8(s, w);
            } else {
                append_xml_safe_ascii(s, w);
            }
            return true;
        }
        case MetaValueKind::Bytes: {
            const std::span<const std::byte> raw = arena.span(v.data.span);
            if (!bytes_are_ascii_text(raw)) {
                return false;
            }
            const std::string_view s(reinterpret_cast<const char*>(raw.data()),
                                     raw.size());
            append_xml_safe_ascii(s, w);
            return true;
        }
        case MetaValueKind::Scalar:
            emit_portable_scalar_text(v, w);
            return true;
        case MetaValueKind::Array: return false;
        }
        return false;
    }


    static bool portable_scalar_like_value_supported(const ByteArena& arena,
                                                     const MetaValue& v) noexcept
    {
        if (v.kind == MetaValueKind::Text || v.kind == MetaValueKind::Scalar) {
            return true;
        }
        if (v.kind == MetaValueKind::Bytes) {
            return bytes_are_ascii_text(arena.span(v.data.span));
        }
        return false;
    }


    static void emit_portable_array_as_seq(const ByteArena& arena,
                                           const MetaValue& v,
                                           SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }

        const std::span<const std::byte> raw = arena.span(v.data.span);
        const uint32_t elem_size             = meta_element_size(v.elem_type);
        if (elem_size == 0U) {
            return;
        }
        const uint32_t count = safe_array_count(arena, v);
        if (count == 0U) {
            return;
        }

        w->append(kIndent4);
        w->append("<rdf:Seq>\n");
        for (uint32_t i = 0; i < count; ++i) {
            const size_t off = static_cast<size_t>(i) * elem_size;
            if (off + elem_size > raw.size()) {
                break;
            }
            w->append(kIndent4);
            w->append(kIndent1);
            w->append("<rdf:li>");

            switch (v.elem_type) {
            case MetaElementType::U8: {
                const uint8_t x = static_cast<uint8_t>(raw[off]);
                append_u64_dec(x, w);
                break;
            }
            case MetaElementType::I8: {
                const int8_t x = static_cast<int8_t>(
                    static_cast<uint8_t>(raw[off]));
                append_i64_dec(x, w);
                break;
            }
            case MetaElementType::U16: {
                uint16_t x = 0;
                std::memcpy(&x, raw.data() + off, sizeof(x));
                append_u64_dec(x, w);
                break;
            }
            case MetaElementType::I16: {
                int16_t x = 0;
                std::memcpy(&x, raw.data() + off, sizeof(x));
                append_i64_dec(x, w);
                break;
            }
            case MetaElementType::U32: {
                uint32_t x = 0;
                std::memcpy(&x, raw.data() + off, sizeof(x));
                append_u64_dec(x, w);
                break;
            }
            case MetaElementType::I32: {
                int32_t x = 0;
                std::memcpy(&x, raw.data() + off, sizeof(x));
                append_i64_dec(x, w);
                break;
            }
            case MetaElementType::U64: {
                uint64_t x = 0;
                std::memcpy(&x, raw.data() + off, sizeof(x));
                append_u64_dec(x, w);
                break;
            }
            case MetaElementType::I64: {
                int64_t x = 0;
                std::memcpy(&x, raw.data() + off, sizeof(x));
                append_i64_dec(x, w);
                break;
            }
            case MetaElementType::F32: {
                uint32_t bits = 0;
                std::memcpy(&bits, raw.data() + off, sizeof(bits));
                float f = 0.0f;
                std::memcpy(&f, &bits, sizeof(f));
                append_f64_dec(static_cast<double>(f), w);
                break;
            }
            case MetaElementType::F64: {
                uint64_t bits = 0;
                std::memcpy(&bits, raw.data() + off, sizeof(bits));
                double d = 0.0;
                std::memcpy(&d, &bits, sizeof(d));
                append_f64_dec(d, w);
                break;
            }
            case MetaElementType::URational: {
                URational r;
                std::memcpy(&r, raw.data() + off, sizeof(r));
                append_rational_text(r, w);
                break;
            }
            case MetaElementType::SRational: {
                SRational r;
                std::memcpy(&r, raw.data() + off, sizeof(r));
                append_rational_text(r, w);
                break;
            }
            }

            w->append("</rdf:li>\n");
        }
        w->append(kIndent4);
        w->append("</rdf:Seq>\n");
    }


    static bool emit_portable_property(SpanWriter* w, std::string_view prefix,
                                       std::string_view name,
                                       const ByteArena& arena,
                                       const MetaValue& v) noexcept
    {
        if (!w) {
            return false;
        }
        if (prefix.empty() || name.empty()) {
            return false;
        }
        if (!is_simple_xmp_property_name(name)) {
            return false;
        }

        if (v.kind == MetaValueKind::Array) {
            w->append(kIndent3);
            w->append("<");
            w->append(prefix);
            w->append(":");
            w->append(name);
            w->append(">\n");

            emit_portable_array_as_seq(arena, v, w);

            w->append(kIndent3);
            w->append("</");
            w->append(prefix);
            w->append(":");
            w->append(name);
            w->append(">\n");
            return true;
        }

        // Skip bytes that can't be represented safely as portable XMP text.
        if (v.kind == MetaValueKind::Bytes) {
            const std::span<const std::byte> raw = arena.span(v.data.span);
            if (!bytes_are_ascii_text(raw)) {
                return false;
            }
        }
        if (v.kind != MetaValueKind::Text && v.kind != MetaValueKind::Bytes
            && v.kind != MetaValueKind::Scalar) {
            return false;
        }

        w->append(kIndent3);
        w->append("<");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">");
        (void)emit_portable_value_inline(arena, v, w);
        w->append("</");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_property_text(SpanWriter* w,
                                            std::string_view prefix,
                                            std::string_view name,
                                            std::string_view value) noexcept
    {
        if (!w || prefix.empty() || name.empty()) {
            return false;
        }
        w->append(kIndent3);
        w->append("<");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">");
        append_xml_safe_ascii(value, w);
        w->append("</");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static bool
    emit_portable_property_text_utf8(SpanWriter* w, std::string_view prefix,
                                     std::string_view name,
                                     std::string_view value) noexcept
    {
        if (!w || prefix.empty() || name.empty()) {
            return false;
        }
        w->append(kIndent3);
        w->append("<");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">");
        append_xml_safe_utf8(value, w);
        w->append("</");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static bool exif_tag_is_portable_date_text(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x0132U:  // DateTime
        case 0x9003U:  // DateTimeOriginal
        case 0x9004U:  // DateTimeDigitized
        case 0xC71BU:  // PreviewDateTime
            return true;
        default: return false;
        }
    }

    static bool exif_tag_is_windows_xp_text(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x9C9BU:  // XPTitle
        case 0x9C9CU:  // XPComment
        case 0x9C9DU:  // XPAuthor
        case 0x9C9EU:  // XPKeywords
        case 0x9C9FU:  // XPSubject
            return true;
        default: return false;
        }
    }

    static bool portable_skip_invalid_rational_tag(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x011AU:  // XResolution
        case 0x011BU:  // YResolution
        case 0x920BU:  // FlashEnergy
        case 0xA20BU:  // FlashEnergy alias
        case 0xA20EU:  // FocalPlaneXResolution
        case 0xA20FU:  // FocalPlaneYResolution
        case 0xA500U:  // Gamma
        case 0xC620U:  // DefaultScale
        case 0xC62AU:  // BaselineExposure
        case 0xC793U:  // OriginalDefaultCropSize
            return true;
        default: return false;
        }
    }

    static bool trim_ascii_nuls_and_spaces(std::string_view input,
                                           std::string_view* out) noexcept
    {
        if (!out) {
            return false;
        }
        size_t begin = 0U;
        size_t end   = input.size();
        while (begin < end) {
            const char c = input[begin];
            if (c != '\0' && !std::isspace(static_cast<unsigned char>(c))) {
                break;
            }
            begin += 1U;
        }
        while (end > begin) {
            const char c = input[end - 1U];
            if (c != '\0' && !std::isspace(static_cast<unsigned char>(c))) {
                break;
            }
            end -= 1U;
        }
        *out = input.substr(begin, end - begin);
        return true;
    }

    static bool exif_date_text_to_xmp(std::string_view input,
                                      std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();

        std::string_view s;
        if (!trim_ascii_nuls_and_spaces(input, &s)) {
            return false;
        }
        if (s.size() < 19U) {
            return false;
        }

        const char date_sep         = s[4];
        const char time_sep         = s[13];
        const char date_time_sep    = s[10];
        const bool date_sep_ok      = (date_sep == ':' || date_sep == '-');
        const bool time_sep_ok      = (time_sep == ':' || time_sep == '.');
        const bool date_time_sep_ok = (date_time_sep == ' '
                                       || date_time_sep == 'T'
                                       || date_time_sep == 't');
        if (!date_sep_ok || !time_sep_ok || !date_time_sep_ok
            || s[7] != date_sep || s[16] != time_sep) {
            return false;
        }
        for (size_t i = 0U; i < 19U; ++i) {
            if (i == 4U || i == 7U || i == 10U || i == 13U || i == 16U) {
                continue;
            }
            if (s[i] < '0' || s[i] > '9') {
                return false;
            }
        }

        out->reserve(25U);
        out->append(s.substr(0U, 4U));
        out->push_back('-');
        out->append(s.substr(5U, 2U));
        out->push_back('-');
        out->append(s.substr(8U, 2U));
        out->push_back('T');
        out->append(s.substr(11U, 2U));
        out->push_back(':');
        out->append(s.substr(14U, 2U));
        out->push_back(':');
        out->append(s.substr(17U, 2U));

        std::string_view suffix;
        (void)trim_ascii_nuls_and_spaces(s.substr(19U), &suffix);
        if (suffix.empty()) {
            return true;
        }

        if (suffix == "Z" || suffix == "z" || suffix == "UTC"
            || suffix == "utc") {
            out->push_back('Z');
            return true;
        }

        if ((suffix[0] == '+' || suffix[0] == '-') && suffix.size() >= 3U) {
            if (suffix.size() == 5U
                && std::isdigit(static_cast<unsigned char>(suffix[1]))
                && std::isdigit(static_cast<unsigned char>(suffix[2]))
                && std::isdigit(static_cast<unsigned char>(suffix[3]))
                && std::isdigit(static_cast<unsigned char>(suffix[4]))) {
                out->push_back(suffix[0]);
                out->push_back(suffix[1]);
                out->push_back(suffix[2]);
                out->push_back(':');
                out->push_back(suffix[3]);
                out->push_back(suffix[4]);
                return true;
            }
            if (suffix.size() == 6U
                && std::isdigit(static_cast<unsigned char>(suffix[1]))
                && std::isdigit(static_cast<unsigned char>(suffix[2]))
                && suffix[3] == ':'
                && std::isdigit(static_cast<unsigned char>(suffix[4]))
                && std::isdigit(static_cast<unsigned char>(suffix[5]))) {
                out->append(suffix);
                return true;
            }
        }

        return false;
    }

    static bool decode_windows_xp_utf16le_text(std::span<const std::byte> raw,
                                               std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (raw.empty()) {
            return true;
        }
        size_t size = raw.size();
        while (size >= 2U && raw[size - 2U] == std::byte { 0x00 }
               && raw[size - 1U] == std::byte { 0x00 }) {
            size -= 2U;
        }
        if (size == 0U) {
            return true;
        }
        if ((size % 2U) != 0U) {
            return false;
        }

        InteropSafetyError error {};
        const interop_internal::SafeTextStatus status
            = interop_internal::decode_text_to_utf8_safe(raw.first(size),
                                                         TextEncoding::Utf16LE,
                                                         "WindowsXPText",
                                                         "tiff:xp", out,
                                                         &error);
        return status == interop_internal::SafeTextStatus::Ok
               || status == interop_internal::SafeTextStatus::Empty;
    }

    static bool scalar_u64_value(const MetaValue& v, uint64_t* out) noexcept
    {
        if (!out || v.kind != MetaValueKind::Scalar) {
            return false;
        }
        switch (v.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64: *out = v.data.u64; return true;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            if (v.data.i64 < 0) {
                return false;
            }
            *out = static_cast<uint64_t>(v.data.i64);
            return true;
        default: return false;
        }
    }

    static std::string_view portable_enum_text_override(std::string_view prefix,
                                                        uint16_t tag,
                                                        uint64_t value) noexcept
    {
        if (prefix == "tiff") {
            switch (tag) {
            case 0x0103U:  // Compression
                switch (value) {
                case 1U: return "Uncompressed";
                case 4U: return "T6/Group 4 Fax";
                case 6U: return "JPEG (old-style)";
                case 7U: return "JPEG";
                case 8U: return "Adobe Deflate";
                case 9U: return "JBIG B&W or VC-5";
                case 32770U: return "Samsung SRW Compressed";
                case 32773U: return "PackBits";
                default: return {};
                }
            case 0x011CU:  // PlanarConfiguration
                switch (value) {
                case 1U: return "Chunky";
                case 2U: return "Planar";
                default: return {};
                }
            case 0x0106U:  // PhotometricInterpretation
                switch (value) {
                case 0U: return "WhiteIsZero";
                case 1U: return "BlackIsZero";
                case 2U: return "RGB";
                case 3U: return "RGB Palette";
                case 4U: return "Transparency Mask";
                case 5U: return "CMYK";
                case 6U: return "YCbCr";
                case 8U: return "CIELab";
                case 9U: return "ICCLab";
                case 10U: return "ITULab";
                default: return {};
                }
            case 0x0213U:  // YCbCrPositioning
                switch (value) {
                case 1U: return "Centered";
                case 2U: return "Co-sited";
                default: return {};
                }
            default: return {};
            }
        }

        if (prefix == "exif") {
            switch (tag) {
            case 0xA406U:  // SceneCaptureType
                switch (value) {
                case 0U: return "Standard";
                case 1U: return "Landscape";
                case 2U: return "Portrait";
                case 3U: return "Night scene";
                default: return {};
                }
            case 0x9208U:  // LightSource
                switch (value) {
                case 0U: return "Unknown";
                case 1U: return "Daylight";
                case 2U: return "Fluorescent";
                case 3U: return "Tungsten (incandescent)";
                case 4U: return "Flash";
                case 9U: return "Fine weather";
                case 10U: return "Cloudy";
                case 11U: return "Shade";
                case 12U: return "Daylight fluorescent";
                case 13U: return "Day white fluorescent";
                case 14U: return "Cool white fluorescent";
                case 15U: return "White fluorescent";
                case 16U: return "Warm white fluorescent";
                case 17U: return "Standard light A";
                case 18U: return "Standard light B";
                case 19U: return "Standard light C";
                case 20U: return "D55";
                case 21U: return "D65";
                case 22U: return "D75";
                case 23U: return "D50";
                case 24U: return "ISO studio tungsten";
                case 25U: return "Daylight";
                case 26U: return "Day white";
                case 27U: return "Cool white";
                case 28U: return "White";
                case 29U: return "Warm white";
                case 30U: return "Daylight LED";
                case 31U: return "Day white LED";
                case 32U: return "Cool white LED";
                case 33U: return "White LED";
                case 34U: return "Warm white LED";
                case 255U: return "Other";
                default: return {};
                }
            case 0xA40AU:  // Sharpness
                switch (value) {
                case 0U: return "Normal";
                case 1U: return "Soft";
                case 2U: return "Hard";
                default: return {};
                }
            case 0xA408U:  // Contrast
                switch (value) {
                case 0U: return "Normal";
                case 1U: return "Low";
                case 2U: return "High";
                default: return {};
                }
            case 0xA409U:  // Saturation
                switch (value) {
                case 0U: return "Normal";
                case 1U: return "Low";
                case 2U: return "High";
                default: return {};
                }
            case 0xA407U:  // GainControl
                switch (value) {
                case 0U: return "None";
                case 1U: return "Low gain up";
                case 2U: return "High gain up";
                case 3U: return "Low gain down";
                case 4U: return "High gain down";
                default: return {};
                }
            case 0xA40CU:  // SubjectDistanceRange
                switch (value) {
                case 0U: return "Unknown";
                case 1U: return "Macro";
                case 2U: return "Close";
                case 3U: return "Distant";
                default: return {};
                }
            case 0xA40FU:  // DistortionCorrection
            case 0xA410U:  // ChromaticAberrationCorrection
            case 0xA411U:  // ShadingCorrection
                switch (value) {
                case 0U: return "Not applied";
                case 1U: return "Applied";
                default: return {};
                }
            case 0xA412U:  // NoiseReduction
                switch (value) {
                case 0U: return "Not applied";
                case 1U: return "Low strength";
                case 2U: return "Normal strength";
                case 3U: return "High strength";
                default: return {};
                }
            case 0xA001U:  // ColorSpace
                switch (value) {
                case 1U: return "sRGB";
                case 2U: return "Adobe RGB";
                case 0xFFFFU: return "Uncalibrated";
                default: return {};
                }
            case 0xA210U:  // FocalPlaneResolutionUnit
                switch (value) {
                case 2U: return "inches";
                case 3U: return "cm";
                case 4U: return "mm";
                case 5U: return "um";
                default: return {};
                }
            case 0xA300U:  // FileSource
                switch (value) {
                case 3U: return "Digital Camera";
                default: return {};
                }
            case 0x001EU:  // GPSDifferential
                switch (value) {
                case 0U: return "No Correction";
                case 1U: return "Differential Corrected";
                default: return {};
                }
            default: return {};
            }
        }

        return {};
    }

    static bool first_ref_char(std::string_view text, std::string_view allowed,
                               char* out_ref) noexcept;

    static bool scalar_text_value(const ByteArena& arena, const MetaValue& v,
                                  std::string_view* out_text) noexcept
    {
        if (!out_text) {
            return false;
        }
        *out_text = {};
        if (v.kind == MetaValueKind::Text) {
            *out_text = arena_string(arena, v.data.span);
            return true;
        }
        if (v.kind == MetaValueKind::Bytes) {
            const std::span<const std::byte> raw = arena.span(v.data.span);
            if (!bytes_are_ascii_text(raw)) {
                return false;
            }
            *out_text
                = std::string_view(reinterpret_cast<const char*>(raw.data()),
                                   raw.size());
            return true;
        }
        return false;
    }

    static std::string_view
    portable_gps_ref_text_override(const ByteArena& arena, uint16_t tag,
                                   const MetaValue& v) noexcept
    {
        std::string_view text;
        if (!scalar_text_value(arena, v, &text)) {
            return {};
        }

        char c = '\0';
        switch (tag) {
        case 0x0009U:  // GPSStatus
            if (first_ref_char(text, "AV", &c)) {
                return (c == 'A') ? "Measurement Active" : "Measurement Void";
            }
            return {};
        case 0x000CU:  // GPSSpeedRef
            if (first_ref_char(text, "KMN", &c)) {
                switch (c) {
                case 'K': return "km/h";
                case 'M': return "mph";
                case 'N': return "knots";
                default: return {};
                }
            }
            return {};
        case 0x000EU:  // GPSTrackRef
        case 0x0010U:  // GPSImgDirectionRef
        case 0x0017U:  // GPSDestBearingRef
            if (first_ref_char(text, "TM", &c)) {
                return (c == 'T') ? "True North" : "Magnetic North";
            }
            return {};
        case 0x0019U:  // GPSDestDistanceRef
            if (first_ref_char(text, "KMN", &c)) {
                switch (c) {
                case 'K': return "Kilometers";
                case 'M': return "Miles";
                case 'N': return "Knots";
                default: return {};
                }
            }
            return {};
        default: return {};
        }
    }

    static bool scalar_urational_value(const MetaValue& v,
                                       URational* out) noexcept
    {
        if (!out || v.kind != MetaValueKind::Scalar
            || v.elem_type != MetaElementType::URational) {
            return false;
        }
        *out = v.data.ur;
        return true;
    }

    static bool first_valid_urational_value(const ByteArena& arena,
                                            const MetaValue& v,
                                            URational* out) noexcept
    {
        if (!out) {
            return false;
        }
        URational r {};
        if (scalar_urational_value(v, &r)) {
            if (r.denom != 0U) {
                *out = r;
                return true;
            }
            return false;
        }
        if (v.kind != MetaValueKind::Array
            || v.elem_type != MetaElementType::URational) {
            return false;
        }

        const std::span<const std::byte> raw = arena.span(v.data.span);
        const uint32_t count                 = safe_array_count(arena, v);
        for (uint32_t i = 0U; i < count; ++i) {
            const size_t off = static_cast<size_t>(i) * sizeof(URational);
            if (off + sizeof(URational) > raw.size()) {
                break;
            }
            std::memcpy(&r, raw.data() + off, sizeof(r));
            if (r.denom != 0U) {
                *out = r;
                return true;
            }
        }
        return false;
    }

    static bool first_valid_srational_value(const ByteArena& arena,
                                            const MetaValue& v,
                                            SRational* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (v.kind == MetaValueKind::Scalar
            && v.elem_type == MetaElementType::SRational) {
            const SRational r = v.data.sr;
            if (r.denom != 0) {
                *out = r;
                return true;
            }
            return false;
        }
        if (v.kind != MetaValueKind::Array
            || v.elem_type != MetaElementType::SRational) {
            return false;
        }

        const std::span<const std::byte> raw = arena.span(v.data.span);
        const uint32_t count                 = safe_array_count(arena, v);
        for (uint32_t i = 0U; i < count; ++i) {
            const size_t off = static_cast<size_t>(i) * sizeof(SRational);
            if (off + sizeof(SRational) > raw.size()) {
                break;
            }
            SRational r {};
            std::memcpy(&r, raw.data() + off, sizeof(r));
            if (r.denom != 0) {
                *out = r;
                return true;
            }
        }
        return false;
    }

    static bool has_invalid_urational_value(const ByteArena& arena,
                                            const MetaValue& v) noexcept
    {
        if (v.kind == MetaValueKind::Scalar
            && v.elem_type == MetaElementType::URational) {
            return v.data.ur.denom == 0U;
        }

        if (v.kind != MetaValueKind::Array
            || v.elem_type != MetaElementType::URational) {
            return false;
        }

        const std::span<const std::byte> raw = arena.span(v.data.span);
        const uint32_t count                 = safe_array_count(arena, v);
        for (uint32_t i = 0U; i < count; ++i) {
            const size_t off = static_cast<size_t>(i) * sizeof(URational);
            if (off + sizeof(URational) > raw.size()) {
                break;
            }
            URational r {};
            std::memcpy(&r, raw.data() + off, sizeof(r));
            if (r.denom == 0U) {
                return true;
            }
        }
        return false;
    }

    static bool has_invalid_srational_value(const ByteArena& arena,
                                            const MetaValue& v) noexcept
    {
        if (v.kind == MetaValueKind::Scalar
            && v.elem_type == MetaElementType::SRational) {
            return v.data.sr.denom == 0;
        }

        if (v.kind != MetaValueKind::Array
            || v.elem_type != MetaElementType::SRational) {
            return false;
        }

        const std::span<const std::byte> raw = arena.span(v.data.span);
        const uint32_t count                 = safe_array_count(arena, v);
        for (uint32_t i = 0U; i < count; ++i) {
            const size_t off = static_cast<size_t>(i) * sizeof(SRational);
            if (off + sizeof(SRational) > raw.size()) {
                break;
            }
            SRational r {};
            std::memcpy(&r, raw.data() + off, sizeof(r));
            if (r.denom == 0) {
                return true;
            }
        }
        return false;
    }

    static bool urational_to_double(const URational& r, double* out) noexcept;
    static bool srational_to_double(const SRational& r, double* out) noexcept;

    static bool parse_gps_date_stamp(std::string_view text, uint32_t* out_year,
                                     uint32_t* out_month,
                                     uint32_t* out_day) noexcept
    {
        if (!out_year || !out_month || !out_day) {
            return false;
        }
        while (!text.empty()) {
            const char c = text.back();
            if (c == '\0' || c == ' ') {
                text.remove_suffix(1U);
                continue;
            }
            break;
        }
        if (text.size() < 10U || text[4] != ':' || text[7] != ':') {
            return false;
        }

        const char y0 = text[0];
        const char y1 = text[1];
        const char y2 = text[2];
        const char y3 = text[3];
        const char m0 = text[5];
        const char m1 = text[6];
        const char d0 = text[8];
        const char d1 = text[9];
        if (y0 < '0' || y0 > '9' || y1 < '0' || y1 > '9' || y2 < '0' || y2 > '9'
            || y3 < '0' || y3 > '9' || m0 < '0' || m0 > '9' || m1 < '0'
            || m1 > '9' || d0 < '0' || d0 > '9' || d1 < '0' || d1 > '9') {
            return false;
        }

        const uint32_t year = static_cast<uint32_t>(y0 - '0') * 1000U
                              + static_cast<uint32_t>(y1 - '0') * 100U
                              + static_cast<uint32_t>(y2 - '0') * 10U
                              + static_cast<uint32_t>(y3 - '0');
        const uint32_t month = static_cast<uint32_t>(m0 - '0') * 10U
                               + static_cast<uint32_t>(m1 - '0');
        const uint32_t day = static_cast<uint32_t>(d0 - '0') * 10U
                             + static_cast<uint32_t>(d1 - '0');
        if (year == 0U || month < 1U || month > 12U || day < 1U || day > 31U) {
            return false;
        }
        *out_year  = year;
        *out_month = month;
        *out_day   = day;
        return true;
    }

    static bool find_gps_date_stamp_for_ifd(const ByteArena& arena,
                                            std::span<const Entry> entries,
                                            std::string_view ifd,
                                            uint32_t* out_year,
                                            uint32_t* out_month,
                                            uint32_t* out_day) noexcept
    {
        if (!out_year || !out_month || !out_day || ifd.empty()) {
            return false;
        }
        for (const Entry& e : entries) {
            if (any(e.flags, EntryFlags::Deleted)
                || e.key.kind != MetaKeyKind::ExifTag
                || e.key.data.exif_tag.tag != 0x001DU
                || e.value.kind != MetaValueKind::Text) {
                continue;
            }
            const std::string_view e_ifd
                = arena_string(arena, e.key.data.exif_tag.ifd);
            if (e_ifd != ifd) {
                continue;
            }
            const std::span<const std::byte> raw = arena.span(
                e.value.data.span);
            const std::string_view text(reinterpret_cast<const char*>(
                                            raw.data()),
                                        raw.size());
            if (parse_gps_date_stamp(text, out_year, out_month, out_day)) {
                return true;
            }
        }
        return false;
    }

    static bool format_decimal_trimmed(double value, uint32_t precision,
                                       std::string* out) noexcept
    {
        if (!out || !std::isfinite(value)) {
            return false;
        }
        if (precision > 12U) {
            precision = 12U;
        }
        char fmt[16];
        std::snprintf(fmt, sizeof(fmt), "%%.%uf",
                      static_cast<unsigned>(precision));
        char buf[80];
        std::snprintf(buf, sizeof(buf), fmt, value);
        out->assign(buf);
        while (!out->empty() && out->back() == '0') {
            out->pop_back();
        }
        if (!out->empty() && out->back() == '.') {
            out->pop_back();
        }
        if (out->empty() || *out == "-0") {
            *out = "0";
        }
        return true;
    }

    static bool find_exif_text_tag_for_ifd(const ByteArena& arena,
                                           std::span<const Entry> entries,
                                           std::string_view ifd, uint16_t tag,
                                           std::string* out_text) noexcept
    {
        if (!out_text || ifd.empty()) {
            return false;
        }
        for (const Entry& e : entries) {
            if (any(e.flags, EntryFlags::Deleted)
                || e.key.kind != MetaKeyKind::ExifTag
                || e.key.data.exif_tag.tag != tag
                || e.value.kind != MetaValueKind::Text) {
                continue;
            }
            const std::string_view e_ifd
                = arena_string(arena, e.key.data.exif_tag.ifd);
            if (e_ifd != ifd) {
                continue;
            }
            std::string_view text = arena_string(arena, e.value.data.span);
            while (!text.empty()
                   && (text.back() == '\0' || text.back() == ' ')) {
                text.remove_suffix(1U);
            }
            if (text.empty()) {
                continue;
            }
            out_text->assign(text.data(), text.size());
            return true;
        }
        return false;
    }

    static bool first_ref_char(std::string_view text, std::string_view allowed,
                               char* out_ref) noexcept
    {
        if (!out_ref) {
            return false;
        }
        for (char c : text) {
            if (c == '\0' || c == ' ') {
                continue;
            }
            const unsigned char uc = static_cast<unsigned char>(c);
            const char up          = static_cast<char>(std::toupper(uc));
            if (allowed.find(up) != std::string_view::npos) {
                *out_ref = up;
                return true;
            }
            return false;
        }
        return false;
    }

    static bool read_rational_triplet_as_f64(const ByteArena& arena,
                                             const MetaValue& v, double* out0,
                                             double* out1,
                                             double* out2) noexcept
    {
        if (!out0 || !out1 || !out2 || v.kind != MetaValueKind::Array) {
            return false;
        }

        const uint32_t count = safe_array_count(arena, v);
        if (count < 3U) {
            return false;
        }

        const std::span<const std::byte> raw = arena.span(v.data.span);
        if (v.elem_type == MetaElementType::URational) {
            if (raw.size() < 3U * sizeof(URational)) {
                return false;
            }
            URational r0 {};
            URational r1 {};
            URational r2 {};
            std::memcpy(&r0, raw.data() + 0U * sizeof(URational), sizeof(r0));
            std::memcpy(&r1, raw.data() + 1U * sizeof(URational), sizeof(r1));
            std::memcpy(&r2, raw.data() + 2U * sizeof(URational), sizeof(r2));
            if (!urational_to_double(r0, out0) || !urational_to_double(r1, out1)
                || !urational_to_double(r2, out2)) {
                return false;
            }
            return true;
        }

        if (v.elem_type == MetaElementType::SRational) {
            if (raw.size() < 3U * sizeof(SRational)) {
                return false;
            }
            SRational r0 {};
            SRational r1 {};
            SRational r2 {};
            std::memcpy(&r0, raw.data() + 0U * sizeof(SRational), sizeof(r0));
            std::memcpy(&r1, raw.data() + 1U * sizeof(SRational), sizeof(r1));
            std::memcpy(&r2, raw.data() + 2U * sizeof(SRational), sizeof(r2));
            if (!srational_to_double(r0, out0) || !srational_to_double(r1, out1)
                || !srational_to_double(r2, out2)) {
                return false;
            }
            return true;
        }

        return false;
    }

    static bool exif_gps_coord_text(const ByteArena& arena,
                                    std::span<const Entry> entries,
                                    std::string_view ifd, const MetaValue& v,
                                    uint16_t ref_tag, bool is_latitude,
                                    std::string* out_text) noexcept
    {
        if (!out_text) {
            return false;
        }

        double deg = 0.0;
        double min = 0.0;
        double sec = 0.0;
        if (!read_rational_triplet_as_f64(arena, v, &deg, &min, &sec)) {
            return false;
        }
        if (!std::isfinite(deg) || !std::isfinite(min) || !std::isfinite(sec)
            || deg < 0.0 || min < 0.0 || sec < 0.0 || min >= 60.0
            || sec >= 60.0) {
            return false;
        }

        const double deg_rounded = std::round(deg);
        if (std::fabs(deg - deg_rounded) > 1e-6) {
            return false;
        }
        const uint32_t deg_i = static_cast<uint32_t>(std::llround(deg_rounded));
        if ((is_latitude && deg_i > 90U) || (!is_latitude && deg_i > 180U)) {
            return false;
        }

        std::string ref_text;
        if (!find_exif_text_tag_for_ifd(arena, entries, ifd, ref_tag,
                                        &ref_text)) {
            return false;
        }
        char ref = '\0';
        if (!first_ref_char(ref_text, is_latitude ? "NS" : "EW", &ref)) {
            return false;
        }

        const double decimal_minutes = min + (sec / 60.0);
        std::string minutes_text;
        if (!format_decimal_trimmed(decimal_minutes, 8U, &minutes_text)) {
            return false;
        }

        char deg_buf[24];
        std::snprintf(deg_buf, sizeof(deg_buf), "%u",
                      static_cast<unsigned>(deg_i));
        out_text->assign(deg_buf);
        out_text->append(",");
        out_text->append(minutes_text);
        out_text->push_back(ref);
        return true;
    }

    static bool exif_gps_time_stamp_text(const ByteArena& arena,
                                         std::span<const Entry> entries,
                                         std::string_view ifd,
                                         const MetaValue& v,
                                         std::string* out_text) noexcept
    {
        if (!out_text) {
            return false;
        }

        double hours   = 0.0;
        double minutes = 0.0;
        double seconds = 0.0;
        if (!read_rational_triplet_as_f64(arena, v, &hours, &minutes,
                                          &seconds)) {
            return false;
        }
        if (!std::isfinite(hours) || !std::isfinite(minutes)
            || !std::isfinite(seconds)) {
            return false;
        }

        const double hr_rounded = std::round(hours);
        const double mn_rounded = std::round(minutes);
        if (std::fabs(hours - hr_rounded) > 1e-6
            || std::fabs(minutes - mn_rounded) > 1e-6) {
            return false;
        }
        if (hr_rounded < 0.0 || hr_rounded >= 24.0 || mn_rounded < 0.0
            || mn_rounded >= 60.0 || seconds < 0.0 || seconds >= 61.0) {
            return false;
        }

        uint32_t year  = 0U;
        uint32_t month = 0U;
        uint32_t day   = 0U;
        if (!find_gps_date_stamp_for_ifd(arena, entries, ifd, &year, &month,
                                         &day)) {
            return false;
        }

        std::string sec_text;
        if (std::fabs(seconds - std::round(seconds)) <= 1e-6) {
            const uint32_t sec_int = static_cast<uint32_t>(
                std::llround(seconds));
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02u", sec_int);
            sec_text.assign(buf);
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.6f", seconds);
            sec_text.assign(buf);
            while (!sec_text.empty() && sec_text.back() == '0') {
                sec_text.pop_back();
            }
            if (!sec_text.empty() && sec_text.back() == '.') {
                sec_text.pop_back();
            }
            if (sec_text.size() < 2U
                || (sec_text.size() >= 2U && sec_text[1] == '.')) {
                sec_text.insert(sec_text.begin(), '0');
            }
        }
        if (sec_text.empty()) {
            return false;
        }

        char dt_prefix[48];
        std::snprintf(dt_prefix, sizeof(dt_prefix),
                      "%04u-%02u-%02uT%02u:%02u:", year, month, day,
                      static_cast<unsigned>(static_cast<uint32_t>(hr_rounded)),
                      static_cast<unsigned>(static_cast<uint32_t>(mn_rounded)));
        out_text->assign(dt_prefix);
        out_text->append(sec_text);
        out_text->append("Z");
        return true;
    }

    static bool urational_to_double(const URational& r, double* out) noexcept
    {
        if (!out || r.denom == 0U) {
            return false;
        }
        const double d = static_cast<double>(r.numer)
                         / static_cast<double>(r.denom);
        if (!std::isfinite(d)) {
            return false;
        }
        *out = d;
        return true;
    }

    static bool srational_to_double(const SRational& r, double* out) noexcept
    {
        if (!out || r.denom == 0) {
            return false;
        }
        const double d = static_cast<double>(r.numer)
                         / static_cast<double>(r.denom);
        if (!std::isfinite(d)) {
            return false;
        }
        *out = d;
        return true;
    }

    static bool emit_exif_lens_specification_decimal_seq(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena, const MetaValue& v) noexcept
    {
        if (!w || prefix.empty() || name.empty()
            || v.kind != MetaValueKind::Array
            || v.elem_type != MetaElementType::URational) {
            return false;
        }
        const std::span<const std::byte> raw = arena.span(v.data.span);
        const uint32_t count                 = safe_array_count(arena, v);
        if (count == 0U) {
            return false;
        }

        w->append(kIndent3);
        w->append("<");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        w->append(kIndent4);
        w->append("<rdf:Seq>\n");

        for (uint32_t i = 0; i < count; ++i) {
            const size_t off = static_cast<size_t>(i) * sizeof(URational);
            if (off + sizeof(URational) > raw.size()) {
                break;
            }
            URational r {};
            std::memcpy(&r, raw.data() + off, sizeof(r));
            w->append(kIndent4);
            w->append(kIndent1);
            w->append("<rdf:li>");
            double d = 0.0;
            if (urational_to_double(r, &d)) {
                append_f64_dec(d, w);
            } else {
                append_rational_text(r, w);
            }
            w->append("</rdf:li>\n");
        }

        w->append(kIndent4);
        w->append("</rdf:Seq>\n");
        w->append(kIndent3);
        w->append("</");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_exif_tag_property_override(
        SpanWriter* w, std::string_view prefix, std::string_view ifd,
        uint16_t tag, std::string_view name, const ByteArena& arena,
        std::span<const Entry> entries, const MetaValue& v) noexcept
    {
        if (!w || prefix.empty() || name.empty()) {
            return false;
        }

        uint64_t u = 0U;
        if (scalar_u64_value(v, &u)) {
            const std::string_view enum_text
                = portable_enum_text_override(prefix, tag, u);
            if (!enum_text.empty()) {
                return emit_portable_property_text(w, prefix, name, enum_text);
            }
        }

        if (prefix == "xmp" && exif_tag_is_portable_date_text(tag)
            && (v.kind == MetaValueKind::Text
                || v.kind == MetaValueKind::Bytes)) {
            std::string_view raw_text {};
            if (v.kind == MetaValueKind::Text) {
                raw_text = arena_string(arena, v.data.span);
            } else {
                const std::span<const std::byte> raw = arena.span(v.data.span);
                if (!bytes_are_ascii_text(raw)) {
                    return true;
                }
                raw_text = std::string_view(reinterpret_cast<const char*>(
                                                raw.data()),
                                            raw.size());
            }
            std::string normalized;
            if (exif_date_text_to_xmp(raw_text, &normalized)) {
                return emit_portable_property_text(w, prefix, name, normalized);
            }
            return false;
        }

        if (prefix != "exif") {
            if (prefix == "tiff" && exif_tag_is_windows_xp_text(tag)) {
                std::span<const std::byte> raw {};
                if (v.kind == MetaValueKind::Bytes
                    || v.kind == MetaValueKind::Text
                    || v.kind == MetaValueKind::Array) {
                    raw = arena.span(v.data.span);
                } else {
                    return false;
                }
                std::string decoded;
                if (!decode_windows_xp_utf16le_text(raw, &decoded)) {
                    return true;
                }
                return emit_portable_property_text_utf8(w, prefix, name,
                                                        decoded);
            }
            if (prefix == "tiff" && exif_tag_is_portable_date_text(tag)
                && (v.kind == MetaValueKind::Text
                    || v.kind == MetaValueKind::Bytes)) {
                std::string_view raw_text {};
                if (v.kind == MetaValueKind::Text) {
                    raw_text = arena_string(arena, v.data.span);
                } else {
                    const std::span<const std::byte> raw = arena.span(
                        v.data.span);
                    if (!bytes_are_ascii_text(raw)) {
                        return true;
                    }
                    raw_text = std::string_view(reinterpret_cast<const char*>(
                                                    raw.data()),
                                                raw.size());
                }
                std::string normalized;
                if (exif_date_text_to_xmp(raw_text, &normalized)) {
                    return emit_portable_property_text(w, prefix, name,
                                                       normalized);
                }
                return false;
            }
            return false;
        }

        const std::string_view gps_ref_text
            = portable_gps_ref_text_override(arena, tag, v);
        if (!gps_ref_text.empty()) {
            return emit_portable_property_text(w, prefix, name, gps_ref_text);
        }

        if (exif_tag_is_portable_date_text(tag)
            && (v.kind == MetaValueKind::Text
                || v.kind == MetaValueKind::Bytes)) {
            std::string_view raw_text {};
            if (v.kind == MetaValueKind::Text) {
                raw_text = arena_string(arena, v.data.span);
            } else {
                const std::span<const std::byte> raw = arena.span(v.data.span);
                if (!bytes_are_ascii_text(raw)) {
                    return true;
                }
                raw_text = std::string_view(reinterpret_cast<const char*>(
                                                raw.data()),
                                            raw.size());
            }
            std::string normalized;
            if (exif_date_text_to_xmp(raw_text, &normalized)) {
                return emit_portable_property_text(w, prefix, name, normalized);
            }
        }

        if ((tag == 0x9286U || tag == 0xA40BU) && v.kind == MetaValueKind::Array
            && v.elem_type != MetaElementType::U8) {
            // Portable XMP should not serialize malformed numeric-array
            // payloads for EXIF blob/text tags like UserComment and
            // DeviceSettingDescription. ExifTool effectively collapses these
            // to a meaningless first scalar on XMP re-read, so skip them.
            return true;
        }

        if (tag == 0xA432U) {  // LensSpecification
            return emit_exif_lens_specification_decimal_seq(w, prefix, name,
                                                            arena, v);
        }

        if (tag == 0x0002U || tag == 0x0004U || tag == 0x0014U
            || tag == 0x0016U) {  // GPSLatitude/GPSLongitude/GPSDest*
            std::string gps_coord;
            uint16_t ref_tag = 0U;
            bool is_latitude = false;
            if (tag == 0x0002U) {
                ref_tag     = 0x0001U;
                is_latitude = true;
            } else if (tag == 0x0004U) {
                ref_tag = 0x0003U;
            } else if (tag == 0x0014U) {
                ref_tag     = 0x0013U;
                is_latitude = true;
            } else {
                ref_tag = 0x0015U;
            }
            if (exif_gps_coord_text(arena, entries, ifd, v, ref_tag,
                                    is_latitude, &gps_coord)) {
                return emit_portable_property_text(w, prefix, name, gps_coord);
            }
            // Skip malformed/ambiguous GPS coordinates in portable output.
            return true;
        }

        if (tag == 0x0007U) {  // GPSTimeStamp
            std::string gps_dt;
            if (exif_gps_time_stamp_text(arena, entries, ifd, v, &gps_dt)) {
                return emit_portable_property_text(w, prefix, name, gps_dt);
            }
            // GPSTimeStamp in XMP is a date-time type; when EXIF lacks
            // supporting GPSDateStamp or carries invalid parts, skip it.
            return true;
        }

        if (tag == 0x0006U) {  // GPSAltitude
            URational altitude {};
            if (!first_valid_urational_value(arena, v, &altitude)) {
                return true;
            }
            double d = 0.0;
            std::string altitude_text;
            if (urational_to_double(altitude, &d) && std::isfinite(d)
                && d >= 0.0 && format_decimal_trimmed(d, 8U, &altitude_text)) {
                return emit_portable_property_text(w, prefix, name,
                                                   altitude_text);
            }
            return true;
        }

        if (tag == 0x0000U && v.kind == MetaValueKind::Array
            && v.elem_type == MetaElementType::U8 && v.count > 0U) {
            const std::span<const std::byte> raw = arena.span(v.data.span);
            if (!raw.empty()) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%u",
                              static_cast<unsigned>(
                                  static_cast<uint8_t>(raw[0])));
                return emit_portable_property_text(w, prefix, name, buf);
            }
        }

        URational ur {};
        SRational sr {};
        char buf[96];

        if (tag == 0x920AU) {  // FocalLength
            if (!first_valid_urational_value(arena, v, &ur)) {
                return true;
            }
            double d = 0.0;
            if (urational_to_double(ur, &d)) {
                std::snprintf(buf, sizeof(buf), "%.1f mm", d);
                return emit_portable_property_text(w, prefix, name, buf);
            }
            return true;
        }
        if (tag == 0x829DU) {  // FNumber
            if (!first_valid_urational_value(arena, v, &ur)) {
                return true;
            }
            double d = 0.0;
            if (urational_to_double(ur, &d)) {
                std::snprintf(buf, sizeof(buf), "%.1f", d);
                return emit_portable_property_text(w, prefix, name, buf);
            }
            return true;
        }
        if (tag == 0x9202U
            || tag == 0x9205U) {  // ApertureValue/MaxApertureValue
            if (!first_valid_urational_value(arena, v, &ur)) {
                return true;
            }
            double apex = 0.0;
            if (urational_to_double(ur, &apex)) {
                const double fnum = std::pow(2.0, apex * 0.5);
                if (std::isfinite(fnum) && fnum <= 1024.0) {
                    std::snprintf(buf, sizeof(buf), "%.1f", fnum);
                    return emit_portable_property_text(w, prefix, name, buf);
                }
            }
            return true;
        }
        if (tag == 0x9201U) {  // ShutterSpeedValue
            if (!first_valid_srational_value(arena, v, &sr)) {
                return true;
            }
            double apex = 0.0;
            if (srational_to_double(sr, &apex)) {
                const double sec = std::pow(2.0, -apex);
                if (std::isfinite(sec) && sec > 0.0) {
                    if (sec < 1.0) {
                        const double den    = 1.0 / sec;
                        const uint64_t rden = static_cast<uint64_t>(
                            std::llround(den));
                        if (rden > 0U) {
                            std::snprintf(buf, sizeof(buf), "1/%llu",
                                          static_cast<unsigned long long>(rden));
                            return emit_portable_property_text(w, prefix, name,
                                                               buf);
                        }
                    } else {
                        std::snprintf(buf, sizeof(buf), "%.1f", sec);
                        return emit_portable_property_text(w, prefix, name,
                                                           buf);
                    }
                }
            }
            return true;
        }
        if (tag == 0x9204U) {  // ExposureCompensation
            if (!first_valid_srational_value(arena, v, &sr)) {
                return true;
            }
            double d = 0.0;
            if (srational_to_double(sr, &d) && std::isfinite(d)) {
                std::snprintf(buf, sizeof(buf), "%.15g", d);
                return emit_portable_property_text(w, prefix, name, buf);
            }
            return true;
        }
        if (tag == 0x9203U) {  // BrightnessValue
            if (!first_valid_srational_value(arena, v, &sr)) {
                return true;
            }
            double d = 0.0;
            if (srational_to_double(sr, &d) && std::isfinite(d)) {
                std::snprintf(buf, sizeof(buf), "%.15g", d);
                return emit_portable_property_text(w, prefix, name, buf);
            }
            return true;
        }
        if (tag == 0xA404U) {  // DigitalZoomRatio
            if (!first_valid_urational_value(arena, v, &ur)) {
                return true;
            }
            double d = 0.0;
            if (urational_to_double(ur, &d) && std::isfinite(d)) {
                std::snprintf(buf, sizeof(buf), "%.15g", d);
                return emit_portable_property_text(w, prefix, name, buf);
            }
            return true;
        }
        if ((tag == 0xA20EU || tag == 0xA20FU)
            && scalar_urational_value(v, &ur)) {  // FocalPlaneX/YResolution
            double d = 0.0;
            if (urational_to_double(ur, &d)) {
                std::snprintf(buf, sizeof(buf), "%.15g", d);
                return emit_portable_property_text(w, prefix, name, buf);
            }
        }

        return false;
    }


    struct PortableIndexedProperty final {
        enum class Container : uint8_t {
            Seq,
            Bag,
        };

        std::string_view prefix;
        std::string_view base;
        uint32_t index         = 0U;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
        Container container    = Container::Seq;
    };

    struct PortableLangAltProperty final {
        std::string_view prefix;
        std::string_view base;
        std::string_view lang;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
    };

    struct PortableStructuredProperty final {
        std::string_view prefix;
        std::string_view base;
        std::string_view child_prefix;
        std::string_view child;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
    };

    struct PortableStructuredLangAltProperty final {
        std::string_view prefix;
        std::string_view base;
        std::string_view child_prefix;
        std::string_view child;
        std::string_view lang;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
    };

    struct PortableStructuredIndexedProperty final {
        std::string_view prefix;
        std::string_view base;
        std::string_view child_prefix;
        std::string_view child;
        uint32_t index         = 0U;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
        PortableIndexedProperty::Container container
            = PortableIndexedProperty::Container::Seq;
    };

    struct PortableStructuredNestedProperty final {
        std::string_view prefix;
        std::string_view base;
        std::string_view child;
        std::string_view grandchild;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
    };

    struct PortableStructuredNestedLangAltProperty final {
        std::string_view prefix;
        std::string_view base;
        std::string_view child;
        std::string_view grandchild;
        std::string_view lang;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
    };

    struct PortableStructuredNestedIndexedProperty final {
        std::string_view prefix;
        std::string_view base;
        std::string_view child;
        std::string_view grandchild;
        uint32_t index         = 0U;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
        PortableIndexedProperty::Container container
            = PortableIndexedProperty::Container::Seq;
    };

    struct PortableIndexedStructuredProperty final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child_prefix;
        std::string_view child;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
        PortableIndexedProperty::Container container
            = PortableIndexedProperty::Container::Seq;
    };

    struct PortableIndexedStructuredLangAltProperty final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child_prefix;
        std::string_view child;
        std::string_view lang;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
    };

    struct PortableIndexedStructuredNestedProperty final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child;
        std::string_view grandchild;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
    };

    struct PortableIndexedStructuredNestedLangAltProperty final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child;
        std::string_view grandchild;
        std::string_view lang;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
    };

    struct PortableIndexedStructuredNestedIndexedProperty final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child;
        std::string_view grandchild;
        uint32_t index         = 0U;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
        PortableIndexedProperty::Container container
            = PortableIndexedProperty::Container::Seq;
    };

    struct PortableIndexedStructuredIndexedNestedProperty final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child_prefix;
        std::string_view child;
        uint32_t child_index = 0U;
        std::string_view grandchild;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
        PortableIndexedProperty::Container child_container
            = PortableIndexedProperty::Container::Seq;
    };

    struct PortableIndexedStructuredDeepNestedProperty final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child;
        std::string_view grandchild_prefix;
        std::string_view grandchild;
        std::string_view leaf_prefix;
        std::string_view leaf;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
    };

    struct PortableIndexedStructuredIndexedProperty final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child_prefix;
        std::string_view child;
        uint32_t index         = 0U;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
        PortableIndexedProperty::Container container
            = PortableIndexedProperty::Container::Seq;
    };

    static PortableIndexedProperty::Container
    portable_existing_xmp_indexed_container(std::string_view prefix,
                                            std::string_view name) noexcept
    {
        if (prefix == "dc") {
            if (name == "subject") {
                return PortableIndexedProperty::Container::Bag;
            }
            if (name == "contributor" || name == "language"
                || name == "publisher" || name == "relation"
                || name == "type") {
                return PortableIndexedProperty::Container::Bag;
            }
            if (name == "creator") {
                return PortableIndexedProperty::Container::Seq;
            }
            if (name == "date") {
                return PortableIndexedProperty::Container::Seq;
            }
        }
        if (prefix == "xmp" && (name == "Identifier" || name == "Advisory")) {
            return PortableIndexedProperty::Container::Bag;
        }
        if (prefix == "xmpRights" && name == "Owner") {
            return PortableIndexedProperty::Container::Bag;
        }
        if (prefix == "xmpBJ" && name == "JobRef") {
            return PortableIndexedProperty::Container::Bag;
        }
        if (prefix == "xmpTPg" && name == "Fonts") {
            return PortableIndexedProperty::Container::Bag;
        }
        if (prefix == "xmpMM" && (name == "Ingredients" || name == "Pantry")) {
            return PortableIndexedProperty::Container::Bag;
        }
        if (prefix == "xmpDM" && name == "Tracks") {
            return PortableIndexedProperty::Container::Bag;
        }
        if (prefix == "xmpMM" && name == "Manifest") {
            return PortableIndexedProperty::Container::Seq;
        }
        if (prefix == "xmpMM" && name == "Versions") {
            return PortableIndexedProperty::Container::Seq;
        }
        if (prefix == "xmpTPg" && name == "Colorants") {
            return PortableIndexedProperty::Container::Seq;
        }
        if (prefix == "Iptc4xmpExt" && name == "LocationId") {
            return PortableIndexedProperty::Container::Bag;
        }
        if (prefix == "Iptc4xmpExt"
            && (name == "Role" || name == "PersonId" || name == "LinkQualifier"
                || name == "AOStylePeriod")) {
            return PortableIndexedProperty::Container::Bag;
        }
        if (prefix == "Iptc4xmpExt"
            && (name == "AOCreator" || name == "AOCreatorId")) {
            return PortableIndexedProperty::Container::Seq;
        }

        if (prefix == "photoshop" && name == "SupplementalCategories") {
            return PortableIndexedProperty::Container::Bag;
        }
        if (prefix == "plus" && name == "ImageAlterationConstraints") {
            return PortableIndexedProperty::Container::Bag;
        }
        if (prefix == "lr" && name == "hierarchicalSubject") {
            return PortableIndexedProperty::Container::Bag;
        }

        return PortableIndexedProperty::Container::Seq;
    }

    static bool
    portable_property_prefers_lang_alt(std::string_view prefix,
                                       std::string_view name) noexcept
    {
        return prefix == "dc"
               && (name == "title" || name == "description"
                   || name == "rights");
    }

    static bool portable_existing_xmp_promotes_scalar_to_lang_alt(
        std::string_view prefix, std::string_view name) noexcept
    {
        if (prefix == "dc") {
            return name == "title" || name == "description" || name == "rights";
        }
        if (prefix == "xmpRights") {
            return name == "UsageTerms";
        }
        return false;
    }

    static bool portable_existing_xmp_promotes_scalar_to_indexed(
        std::string_view prefix, std::string_view name) noexcept
    {
        if (prefix == "dc") {
            return name == "subject" || name == "creator" || name == "language"
                   || name == "contributor" || name == "publisher"
                   || name == "relation" || name == "type" || name == "date";
        }
        if (prefix == "xmp") {
            return name == "Identifier" || name == "Advisory";
        }
        if (prefix == "xmpRights") {
            return name == "Owner";
        }
        if (prefix == "photoshop") {
            return name == "SupplementalCategories";
        }
        if (prefix == "lr") {
            return name == "hierarchicalSubject";
        }
        if (prefix == "plus") {
            return name == "ImageAlterationConstraints";
        }
        return false;
    }

    struct PortablePropertyKey final {
        std::string_view prefix;
        std::string_view name;
    };

    struct PortablePropertyKeyHash final {
        size_t operator()(const PortablePropertyKey& key) const noexcept
        {
            const size_t h1 = std::hash<std::string_view> {}(key.prefix);
            const size_t h2 = std::hash<std::string_view> {}(key.name);
            return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
        }
    };

    struct PortablePropertyKeyEq final {
        bool operator()(const PortablePropertyKey& a,
                        const PortablePropertyKey& b) const noexcept
        {
            return a.prefix == b.prefix && a.name == b.name;
        }
    };

    enum class PortablePropertyOwner : uint8_t {
        Exif,
        ExistingXmp,
        Iptc,
    };

    enum class PortablePropertyShape : uint8_t {
        Scalar,
        Indexed,
        LangAlt,
        Structured,
        StructuredIndexed,
    };

    static bool standard_existing_xmp_required_base_shape(
        std::string_view prefix, std::string_view name,
        PortablePropertyShape* out_shape) noexcept
    {
        if (!out_shape) {
            return false;
        }
        *out_shape = PortablePropertyShape::Scalar;

        if (prefix == "dc"
            && (name == "title" || name == "description" || name == "rights")) {
            *out_shape = PortablePropertyShape::LangAlt;
            return true;
        }

        if ((prefix == "xmpRights" && name == "UsageTerms")) {
            *out_shape = PortablePropertyShape::LangAlt;
            return true;
        }

        if ((prefix == "dc"
             && (name == "creator" || name == "subject" || name == "language"
                 || name == "contributor" || name == "publisher"
                 || name == "relation" || name == "type" || name == "date"))
            || (prefix == "xmp" && (name == "Identifier" || name == "Advisory"))
            || (prefix == "xmpRights" && name == "Owner")
            || (prefix == "photoshop" && name == "SupplementalCategories")
            || (prefix == "lr" && name == "hierarchicalSubject")
            || (prefix == "plus" && name == "ImageAlterationConstraints")) {
            *out_shape = PortablePropertyShape::Indexed;
            return true;
        }

        if (prefix == "Iptc4xmpCore"
            && (name == "CreatorContactInfo" || name == "LocationCreated")) {
            *out_shape = PortablePropertyShape::Structured;
            return true;
        }

        if (prefix == "xmpDM"
            && (name == "altTimecode" || name == "beatSpliceParams"
                || name == "duration" || name == "introTime"
                || name == "markers" || name == "outCue" || name == "ProjectRef"
                || name == "relativeTimestamp" || name == "resampleParams"
                || name == "startTimecode" || name == "timeScaleParams"
                || name == "videoAlphaPremultipleColor"
                || name == "videoFrameSize")) {
            *out_shape = PortablePropertyShape::Structured;
            return true;
        }

        if (prefix == "xmpTPg" && name == "MaxPageSize") {
            *out_shape = PortablePropertyShape::Structured;
            return true;
        }

        if (prefix == "xmpMM"
            && (name == "DerivedFrom" || name == "ManagedFrom"
                || name == "RenditionOf")) {
            *out_shape = PortablePropertyShape::Structured;
            return true;
        }

        if ((prefix == "xmpBJ" && name == "JobRef")
            || (prefix == "plus" && name == "Licensee")
            || (prefix == "xmpMM"
                && (name == "History" || name == "Ingredients"
                    || name == "Manifest" || name == "Pantry"
                    || name == "Versions"))
            || (prefix == "xmpDM"
                && (name == "contributedMedia" || name == "Tracks"))
            || (prefix == "xmpTPg"
                && (name == "Colorants" || name == "Fonts"
                    || name == "SwatchGroups"))
            || (prefix == "Iptc4xmpExt"
                && (name == "AboutCvTerm" || name == "ArtworkOrObject"
                    || name == "Contributor" || name == "Creator"
                    || name == "DopesheetLink" || name == "LocationShown"
                    || name == "PersonHeard" || name == "PersonInImageWDetails"
                    || name == "PlanningRef" || name == "ProductInImage"
                    || name == "ShownEvent" || name == "Snapshot"
                    || name == "SupplyChainSource" || name == "TranscriptLink"
                    || name == "VideoShotType"))) {
            *out_shape = PortablePropertyShape::StructuredIndexed;
            return true;
        }

        return false;
    }

    static bool standard_existing_xmp_base_accepts_shape(
        std::string_view prefix, std::string_view name,
        PortablePropertyShape shape) noexcept
    {
        PortablePropertyShape expected = PortablePropertyShape::Scalar;
        if (!standard_existing_xmp_required_base_shape(prefix, name,
                                                       &expected)) {
            return true;
        }
        return expected == shape;
    }

    enum class PortableStructuredChildShape : uint8_t {
        Scalar,
        Indexed,
        LangAlt,
        Resource,
    };

    static bool standard_existing_xmp_required_structured_child_shape(
        std::string_view prefix, std::string_view base,
        std::string_view child_prefix, std::string_view child,
        PortableStructuredChildShape* out_shape) noexcept
    {
        if (!out_shape) {
            return false;
        }
        *out_shape = PortableStructuredChildShape::Scalar;

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "Iptc4xmpCore" && base == "CreatorContactInfo"
            && child == "CiAdrRegion") {
            *out_shape = PortableStructuredChildShape::Resource;
            return true;
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "Iptc4xmpCore" && base == "CreatorContactInfo"
            && child == "CiAdrExtadr") {
            *out_shape = PortableStructuredChildShape::Indexed;
            return true;
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "Iptc4xmpCore" && base == "CreatorContactInfo"
            && child == "CiAdrCity") {
            *out_shape = PortableStructuredChildShape::LangAlt;
            return true;
        }

        if (prefix == "xmpTPg" && base == "Fonts" && child_prefix == "stFnt"
            && child == "childFontFiles") {
            *out_shape = PortableStructuredChildShape::Indexed;
            return true;
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "xmpTPg" && base == "SwatchGroups"
            && child == "Colorants") {
            *out_shape = PortableStructuredChildShape::Indexed;
            return true;
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "Iptc4xmpExt" && base == "LocationShown"
            && child == "Address") {
            *out_shape = PortableStructuredChildShape::Resource;
            return true;
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "Iptc4xmpExt"
            && (base == "LocationShown" || base == "LocationCreated")) {
            if (child == "City" || child == "CountryCode"
                || child == "CountryName" || child == "ProvinceState"
                || child == "WorldRegion") {
                *out_shape = PortableStructuredChildShape::Scalar;
                return true;
            }
            if (child == "LocationName") {
                *out_shape = PortableStructuredChildShape::LangAlt;
                return true;
            }
            if (child == "LocationId") {
                *out_shape = PortableStructuredChildShape::Indexed;
                return true;
            }
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "Iptc4xmpExt" && base == "AboutCvTerm"
            && child == "CvTermName") {
            *out_shape = PortableStructuredChildShape::LangAlt;
            return true;
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "Iptc4xmpExt"
            && (base == "Contributor" || base == "Creator"
                || base == "PlanningRef")) {
            if (child == "Name") {
                *out_shape = PortableStructuredChildShape::LangAlt;
                return true;
            }
            if (child == "Role") {
                *out_shape = PortableStructuredChildShape::Indexed;
                return true;
            }
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "Iptc4xmpExt"
            && (base == "PersonHeard" || base == "ShownEvent"
                || base == "SupplyChainSource" || base == "VideoShotType")) {
            if (child == "Name") {
                *out_shape = PortableStructuredChildShape::LangAlt;
                return true;
            }
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "Iptc4xmpExt" && base == "ArtworkOrObject") {
            if (child == "AOTitle" || child == "AOContentDescription"
                || child == "AOContributionDescription"
                || child == "AOPhysicalDescription") {
                *out_shape = PortableStructuredChildShape::LangAlt;
                return true;
            }
            if (child == "AOCreator" || child == "AOCreatorId"
                || child == "AOStylePeriod") {
                *out_shape = PortableStructuredChildShape::Indexed;
                return true;
            }
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "Iptc4xmpExt" && base == "PersonInImageWDetails") {
            if (child == "PersonName" || child == "PersonDescription") {
                *out_shape = PortableStructuredChildShape::LangAlt;
                return true;
            }
            if (child == "PersonId") {
                *out_shape = PortableStructuredChildShape::Indexed;
                return true;
            }
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "Iptc4xmpExt" && base == "ProductInImage") {
            if (child == "ProductName" || child == "ProductDescription") {
                *out_shape = PortableStructuredChildShape::LangAlt;
                return true;
            }
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "Iptc4xmpExt"
            && (base == "DopesheetLink" || base == "Snapshot"
                || base == "TranscriptLink")
            && child == "LinkQualifier") {
            *out_shape = PortableStructuredChildShape::Indexed;
            return true;
        }

        if (prefix == "Iptc4xmpExt"
            && (base == "LocationShown" || base == "LocationCreated")
            && child_prefix == "xmp" && child == "Identifier") {
            *out_shape = PortableStructuredChildShape::Indexed;
            return true;
        }

        if (prefix == "Iptc4xmpExt"
            && (base == "LocationShown" || base == "LocationCreated")
            && child_prefix == "exif"
            && (child == "GPSLatitude" || child == "GPSLongitude"
                || child == "GPSAltitude" || child == "GPSAltitudeRef")) {
            *out_shape = PortableStructuredChildShape::Scalar;
            return true;
        }

        if (prefix == "xmpMM" && base == "Manifest" && child_prefix == "stMfs"
            && child == "reference") {
            *out_shape = PortableStructuredChildShape::Resource;
            return true;
        }

        if (prefix == "xmpMM" && base == "Versions" && child_prefix == "stVer"
            && child == "event") {
            *out_shape = PortableStructuredChildShape::Resource;
            return true;
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "xmpDM" && base == "beatSpliceParams"
            && child == "riseInTimeDuration") {
            *out_shape = PortableStructuredChildShape::Resource;
            return true;
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "xmpDM" && base == "markers"
            && child == "cuePointParams") {
            *out_shape = PortableStructuredChildShape::Resource;
            return true;
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "xmpDM" && base == "contributedMedia"
            && (child == "duration" || child == "startTime")) {
            *out_shape = PortableStructuredChildShape::Resource;
            return true;
        }

        if ((child_prefix.empty() || child_prefix == prefix)
            && prefix == "xmpDM" && base == "Tracks" && child == "markers") {
            *out_shape = PortableStructuredChildShape::Resource;
            return true;
        }

        return false;
    }

    static bool standard_existing_xmp_structured_child_accepts_shape(
        std::string_view prefix, std::string_view base,
        std::string_view child_prefix, std::string_view child,
        PortableStructuredChildShape shape) noexcept
    {
        PortableStructuredChildShape expected
            = PortableStructuredChildShape::Scalar;
        if (!standard_existing_xmp_required_structured_child_shape(
                prefix, base, child_prefix, child, &expected)) {
            return true;
        }
        return expected == shape;
    }

    static bool standard_existing_xmp_flattened_nested_alias(
        std::string_view prefix, std::string_view base, std::string_view child,
        std::string_view grandchild, std::string_view* out_child) noexcept
    {
        if (!out_child) {
            return false;
        }
        *out_child = std::string_view {};

        if (prefix != "Iptc4xmpExt") {
            return false;
        }
        if (base != "LocationShown" && base != "LocationCreated") {
            return false;
        }
        if (child != "Address") {
            return false;
        }
        if (grandchild == "City" || grandchild == "CountryCode"
            || grandchild == "CountryName" || grandchild == "ProvinceState"
            || grandchild == "WorldRegion") {
            *out_child = grandchild;
            return true;
        }

        return false;
    }

    static bool standard_existing_xmp_required_nested_child_shape(
        std::string_view prefix, std::string_view base, std::string_view child,
        std::string_view grandchild,
        PortableStructuredChildShape* out_shape) noexcept
    {
        if (!out_shape) {
            return false;
        }
        *out_shape = PortableStructuredChildShape::Scalar;

        if (prefix == "Iptc4xmpCore" && base == "CreatorContactInfo"
            && child == "CiAdrRegion") {
            if (grandchild == "ProvinceName") {
                *out_shape = PortableStructuredChildShape::LangAlt;
                return true;
            }
            if (grandchild == "ProvinceCode") {
                *out_shape = PortableStructuredChildShape::Indexed;
                return true;
            }
        }

        if (prefix == "xmpDM" && base == "Tracks" && child == "markers"
            && grandchild == "cuePointParams") {
            *out_shape = PortableStructuredChildShape::Resource;
            return true;
        }

        return false;
    }

    static bool standard_existing_xmp_nested_child_accepts_shape(
        std::string_view prefix, std::string_view base, std::string_view child,
        std::string_view grandchild,
        PortableStructuredChildShape shape) noexcept
    {
        PortableStructuredChildShape expected
            = PortableStructuredChildShape::Scalar;
        if (!standard_existing_xmp_required_nested_child_shape(
                prefix, base, child, grandchild, &expected)) {
            return true;
        }
        return expected == shape;
    }

    static bool existing_xmp_has_explicit_structured_lang_alt_child(
        const ByteArena& arena, std::span<const PortableCustomNsDecl> decls,
        std::span<const Entry> entries, std::string_view prefix,
        std::string_view base, uint32_t item_index,
        std::string_view child_prefix, std::string_view child) noexcept
    {
        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& candidate = entries[i];
            if (any(candidate.flags, EntryFlags::Deleted)
                || candidate.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }

            const std::string_view candidate_ns
                = arena_string(arena,
                               candidate.key.data.xmp_property.schema_ns);
            std::string_view candidate_prefix;
            if (!portable_ns_to_prefix(candidate_ns, decls, &candidate_prefix)
                || candidate_prefix != prefix) {
                continue;
            }

            std::string_view candidate_base_name;
            std::string_view candidate_child_name;
            std::string_view candidate_lang;
            uint32_t candidate_item_index = 0U;
            const bool parsed
                = item_index == 0U
                      ? parse_structured_lang_alt_xmp_property_name(
                            arena_string(arena, candidate.key.data.xmp_property
                                                    .property_path),
                            &candidate_base_name, &candidate_child_name,
                            &candidate_lang)
                      : parse_indexed_structured_lang_alt_xmp_property_name(
                            arena_string(arena, candidate.key.data.xmp_property
                                                    .property_path),
                            &candidate_base_name, &candidate_item_index,
                            &candidate_child_name, &candidate_lang);
            if (!parsed
                || (item_index != 0U && candidate_item_index != item_index)) {
                continue;
            }

            std::string_view raw_child_prefix;
            std::string_view raw_child_name;
            if (!split_qualified_xmp_property_name(candidate_child_name,
                                                   &raw_child_prefix,
                                                   &raw_child_name)) {
                continue;
            }

            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix,
                                                          candidate_base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            bool normalized_child = false;
            if (!resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, candidate_child_name,
                    &portable_child_prefix, &portable_child,
                    &normalized_child)) {
                continue;
            }
            if (portable_base == base && portable_child_prefix == child_prefix
                && portable_child == child) {
                return true;
            }
        }

        return false;
    }

    static bool existing_xmp_has_explicit_structured_scalar_child(
        const ByteArena& arena, std::span<const PortableCustomNsDecl> decls,
        std::span<const Entry> entries, std::string_view prefix,
        std::string_view base, size_t skip_entry_index, uint32_t item_index,
        std::string_view child_prefix, std::string_view child) noexcept
    {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i == skip_entry_index) {
                continue;
            }
            const Entry& candidate = entries[i];
            if (any(candidate.flags, EntryFlags::Deleted)
                || candidate.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }

            const std::string_view candidate_ns
                = arena_string(arena,
                               candidate.key.data.xmp_property.schema_ns);
            std::string_view candidate_prefix;
            if (!portable_ns_to_prefix(candidate_ns, decls, &candidate_prefix)
                || candidate_prefix != prefix) {
                continue;
            }

            std::string_view candidate_base_name;
            std::string_view candidate_child_name;
            uint32_t candidate_item_index = 0U;
            const bool parsed
                = item_index == 0U
                      ? parse_structured_xmp_property_name(
                            arena_string(arena, candidate.key.data.xmp_property
                                                    .property_path),
                            &candidate_base_name, &candidate_child_name)
                      : parse_indexed_structured_xmp_property_name(
                            arena_string(arena, candidate.key.data.xmp_property
                                                    .property_path),
                            &candidate_base_name, &candidate_item_index,
                            &candidate_child_name);
            if (!parsed
                || (item_index != 0U && candidate_item_index != item_index)) {
                continue;
            }

            std::string_view raw_child_prefix;
            std::string_view raw_child_name;
            if (!split_qualified_xmp_property_name(candidate_child_name,
                                                   &raw_child_prefix,
                                                   &raw_child_name)) {
                continue;
            }

            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix,
                                                          candidate_base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            bool normalized_child = false;
            if (!resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, candidate_child_name,
                    &portable_child_prefix, &portable_child,
                    &normalized_child)) {
                continue;
            }
            if (portable_base == base && portable_child_prefix == child_prefix
                && portable_child == child) {
                return true;
            }
        }

        return false;
    }

    static bool existing_xmp_has_explicit_lang_alt_base(
        const ByteArena& arena, std::span<const PortableCustomNsDecl> decls,
        std::span<const Entry> entries, std::string_view prefix,
        std::string_view base) noexcept
    {
        if (prefix.empty() || base.empty()) {
            return false;
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (e.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }
            const std::string_view ns
                = arena_string(arena, e.key.data.xmp_property.schema_ns);
            std::string_view entry_prefix;
            if (!portable_ns_to_prefix(ns, decls, &entry_prefix)
                || entry_prefix != prefix) {
                continue;
            }

            const std::string_view path
                = arena_string(arena, e.key.data.xmp_property.property_path);
            std::string_view parsed_base;
            std::string_view lang;
            if (!parse_lang_alt_xmp_property_name(path, &parsed_base, &lang)) {
                continue;
            }

            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, parsed_base);
            if (portable_base == base) {
                return true;
            }
        }

        return false;
    }

    static bool existing_xmp_has_explicit_indexed_base(
        const ByteArena& arena, std::span<const PortableCustomNsDecl> decls,
        std::span<const Entry> entries, std::string_view prefix,
        std::string_view base) noexcept
    {
        if (prefix.empty() || base.empty()) {
            return false;
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (e.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }
            const std::string_view ns
                = arena_string(arena, e.key.data.xmp_property.schema_ns);
            std::string_view entry_prefix;
            if (!portable_ns_to_prefix(ns, decls, &entry_prefix)
                || entry_prefix != prefix) {
                continue;
            }

            const std::string_view path
                = arena_string(arena, e.key.data.xmp_property.property_path);
            std::string_view parsed_base;
            uint32_t index = 0U;
            if (!parse_indexed_xmp_property_name(path, &parsed_base, &index)) {
                continue;
            }

            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, parsed_base);
            if (portable_base == base) {
                return true;
            }
        }

        return false;
    }

    static bool existing_xmp_has_explicit_structured_nested_lang_alt_child(
        const ByteArena& arena, std::span<const PortableCustomNsDecl> decls,
        std::span<const Entry> entries, std::string_view prefix,
        std::string_view base, uint32_t item_index, std::string_view child,
        std::string_view grandchild) noexcept
    {
        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& candidate = entries[i];
            if (any(candidate.flags, EntryFlags::Deleted)
                || candidate.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }

            const std::string_view candidate_ns
                = arena_string(arena,
                               candidate.key.data.xmp_property.schema_ns);
            std::string_view candidate_prefix;
            if (!portable_ns_to_prefix(candidate_ns, decls, &candidate_prefix)
                || candidate_prefix != prefix) {
                continue;
            }

            std::string_view candidate_base_name;
            std::string_view candidate_child_name;
            std::string_view candidate_grandchild_name;
            std::string_view candidate_lang;
            uint32_t candidate_item_index = 0U;
            const bool parsed
                = item_index == 0U
                      ? parse_nested_structured_lang_alt_xmp_property_name(
                            arena_string(arena, candidate.key.data.xmp_property
                                                    .property_path),
                            &candidate_base_name, &candidate_child_name,
                            &candidate_grandchild_name, &candidate_lang)
                      : parse_indexed_nested_structured_lang_alt_xmp_property_name(
                            arena_string(arena, candidate.key.data.xmp_property
                                                    .property_path),
                            &candidate_base_name, &candidate_item_index,
                            &candidate_child_name, &candidate_grandchild_name,
                            &candidate_lang);
            if (!parsed
                || (item_index != 0U && candidate_item_index != item_index)) {
                continue;
            }

            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix,
                                                          candidate_base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            std::string_view portable_grandchild_prefix;
            std::string_view portable_grandchild;
            bool normalized_child      = false;
            bool normalized_grandchild = false;
            if (!resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, candidate_child_name,
                    &portable_child_prefix, &portable_child, &normalized_child)
                || !resolve_existing_xmp_nested_grandchild_to_portable(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, candidate_grandchild_name,
                    &portable_grandchild_prefix, &portable_grandchild,
                    &normalized_grandchild)) {
                continue;
            }
            if (portable_base == base && portable_child == child
                && portable_grandchild == grandchild) {
                return true;
            }
        }

        return false;
    }

    static bool existing_xmp_has_explicit_structured_nested_indexed_child(
        const ByteArena& arena, std::span<const PortableCustomNsDecl> decls,
        std::span<const Entry> entries, std::string_view prefix,
        std::string_view base, uint32_t item_index, std::string_view child,
        std::string_view grandchild) noexcept
    {
        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& candidate = entries[i];
            if (any(candidate.flags, EntryFlags::Deleted)
                || candidate.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }

            const std::string_view candidate_ns
                = arena_string(arena,
                               candidate.key.data.xmp_property.schema_ns);
            std::string_view candidate_prefix;
            if (!portable_ns_to_prefix(candidate_ns, decls, &candidate_prefix)
                || candidate_prefix != prefix) {
                continue;
            }

            std::string_view candidate_base_name;
            std::string_view candidate_child_name;
            std::string_view candidate_grandchild_name;
            uint32_t candidate_grandchild_index = 0U;
            uint32_t candidate_item_index       = 0U;
            const bool parsed
                = item_index == 0U
                      ? parse_nested_structured_indexed_xmp_property_name(
                            arena_string(arena, candidate.key.data.xmp_property
                                                    .property_path),
                            &candidate_base_name, &candidate_child_name,
                            &candidate_grandchild_name,
                            &candidate_grandchild_index)
                      : parse_indexed_nested_structured_indexed_xmp_property_name(
                            arena_string(arena, candidate.key.data.xmp_property
                                                    .property_path),
                            &candidate_base_name, &candidate_item_index,
                            &candidate_child_name, &candidate_grandchild_name,
                            &candidate_grandchild_index);
            if (!parsed || candidate_grandchild_index == 0U
                || (item_index != 0U && candidate_item_index != item_index)) {
                continue;
            }

            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix,
                                                          candidate_base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            std::string_view portable_grandchild_prefix;
            std::string_view portable_grandchild;
            bool normalized_child      = false;
            bool normalized_grandchild = false;
            if (!resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, candidate_child_name,
                    &portable_child_prefix, &portable_child, &normalized_child)
                || !resolve_existing_xmp_nested_grandchild_to_portable(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, candidate_grandchild_name,
                    &portable_grandchild_prefix, &portable_grandchild,
                    &normalized_grandchild)) {
                continue;
            }
            if (portable_base == base && portable_child == child
                && portable_grandchild == grandchild) {
                return true;
            }
        }

        return false;
    }

    static bool existing_xmp_has_explicit_structured_indexed_child(
        const ByteArena& arena, std::span<const PortableCustomNsDecl> decls,
        std::span<const Entry> entries, std::string_view prefix,
        std::string_view base, size_t skip_entry_index, uint32_t item_index,
        std::string_view child_prefix, std::string_view child) noexcept
    {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i == skip_entry_index) {
                continue;
            }
            const Entry& candidate = entries[i];
            if (any(candidate.flags, EntryFlags::Deleted)
                || candidate.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }

            const std::string_view candidate_ns
                = arena_string(arena,
                               candidate.key.data.xmp_property.schema_ns);
            std::string_view candidate_prefix;
            if (!portable_ns_to_prefix(candidate_ns, decls, &candidate_prefix)
                || candidate_prefix != prefix) {
                continue;
            }

            std::string_view candidate_base_name;
            std::string_view candidate_child_name;
            uint32_t candidate_child_index = 0U;
            uint32_t candidate_item_index  = 0U;
            bool parsed                    = false;
            const std::string_view candidate_path
                = arena_string(arena,
                               candidate.key.data.xmp_property.property_path);
            if (item_index == 0U) {
                parsed = parse_structured_indexed_xmp_property_name(
                    candidate_path, &candidate_base_name, &candidate_child_name,
                    &candidate_child_index);
            } else {
                parsed = parse_indexed_structured_indexed_xmp_property_name(
                    candidate_path, &candidate_base_name, &candidate_item_index,
                    &candidate_child_name, &candidate_child_index);
                if (!parsed) {
                    std::string_view ignored_grandchild;
                    parsed
                        = parse_indexed_structured_indexed_nested_xmp_property_name(
                            candidate_path, &candidate_base_name,
                            &candidate_item_index, &candidate_child_name,
                            &candidate_child_index, &ignored_grandchild);
                }
            }
            if (!parsed || candidate_child_index == 0U) {
                continue;
            }
            if (item_index != 0U && candidate_item_index != item_index) {
                continue;
            }

            std::string_view raw_child_prefix;
            std::string_view raw_child_name;
            if (!split_qualified_xmp_property_name(candidate_child_name,
                                                   &raw_child_prefix,
                                                   &raw_child_name)) {
                continue;
            }

            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix,
                                                          candidate_base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            bool normalized_child = false;
            if (!resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, candidate_child_name,
                    &portable_child_prefix, &portable_child,
                    &normalized_child)) {
                continue;
            }
            if (portable_base == base && portable_child_prefix == child_prefix
                && portable_child == child) {
                return true;
            }
        }

        return false;
    }

    static bool existing_xmp_has_explicit_structured_indexed_child_item(
        const ByteArena& arena, std::span<const PortableCustomNsDecl> decls,
        std::span<const Entry> entries, std::string_view prefix,
        std::string_view base, size_t skip_entry_index, uint32_t item_index,
        std::string_view child_prefix, std::string_view child,
        uint32_t child_index) noexcept
    {
        if (child_index == 0U) {
            return false;
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            if (i == skip_entry_index) {
                continue;
            }
            const Entry& candidate = entries[i];
            if (any(candidate.flags, EntryFlags::Deleted)
                || candidate.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }

            const std::string_view candidate_ns
                = arena_string(arena,
                               candidate.key.data.xmp_property.schema_ns);
            std::string_view candidate_prefix;
            if (!portable_ns_to_prefix(candidate_ns, decls, &candidate_prefix)
                || candidate_prefix != prefix) {
                continue;
            }

            std::string_view candidate_base_name;
            std::string_view candidate_child_name;
            uint32_t candidate_child_index = 0U;
            uint32_t candidate_item_index  = 0U;
            bool parsed                    = false;
            const std::string_view candidate_path
                = arena_string(arena,
                               candidate.key.data.xmp_property.property_path);
            if (item_index == 0U) {
                parsed = parse_structured_indexed_xmp_property_name(
                    candidate_path, &candidate_base_name, &candidate_child_name,
                    &candidate_child_index);
            } else {
                parsed = parse_indexed_structured_indexed_xmp_property_name(
                    candidate_path, &candidate_base_name, &candidate_item_index,
                    &candidate_child_name, &candidate_child_index);
            }
            if (!parsed || candidate_child_index != child_index) {
                continue;
            }
            if (item_index != 0U && candidate_item_index != item_index) {
                continue;
            }

            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix,
                                                          candidate_base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            bool normalized_child = false;
            if (!resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, candidate_child_name,
                    &portable_child_prefix, &portable_child,
                    &normalized_child)) {
                continue;
            }
            if (portable_base == base && portable_child_prefix == child_prefix
                && portable_child == child) {
                return true;
            }
        }

        return false;
    }

    static bool
    existing_xmp_has_explicit_indexed_structured_indexed_nested_child(
        const ByteArena& arena, std::span<const PortableCustomNsDecl> decls,
        std::span<const Entry> entries, std::string_view prefix,
        std::string_view base, uint32_t item_index,
        std::string_view child_prefix, std::string_view child) noexcept
    {
        if (prefix.empty() || base.empty() || item_index == 0U
            || child.empty()) {
            return false;
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& candidate = entries[i];
            if (any(candidate.flags, EntryFlags::Deleted)
                || candidate.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }

            const std::string_view candidate_ns
                = arena_string(arena,
                               candidate.key.data.xmp_property.schema_ns);
            std::string_view candidate_prefix;
            if (!portable_ns_to_prefix(candidate_ns, decls, &candidate_prefix)
                || candidate_prefix != prefix) {
                continue;
            }

            std::string_view candidate_base_name;
            std::string_view candidate_child_name;
            std::string_view ignored_grandchild;
            uint32_t candidate_item_index  = 0U;
            uint32_t candidate_child_index = 0U;
            if (!parse_indexed_structured_indexed_nested_xmp_property_name(
                    arena_string(arena,
                                 candidate.key.data.xmp_property.property_path),
                    &candidate_base_name, &candidate_item_index,
                    &candidate_child_name, &candidate_child_index,
                    &ignored_grandchild)
                || candidate_item_index != item_index
                || candidate_child_index == 0U) {
                continue;
            }

            std::string_view raw_child_prefix;
            std::string_view raw_child_name;
            if (!split_qualified_xmp_property_name(candidate_child_name,
                                                   &raw_child_prefix,
                                                   &raw_child_name)) {
                continue;
            }

            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix,
                                                          candidate_base_name);
            const std::string_view portable_child_prefix
                = raw_child_prefix.empty() ? prefix : raw_child_prefix;
            const std::string_view portable_child
                = portable_property_name_for_existing_xmp(portable_child_prefix,
                                                          raw_child_name);
            if (portable_base == base && portable_child_prefix == child_prefix
                && portable_child == child) {
                return true;
            }
        }

        return false;
    }

    static bool
    existing_xmp_has_explicit_indexed_structured_indexed_nested_scalar_child(
        const ByteArena& arena, std::span<const PortableCustomNsDecl> decls,
        std::span<const Entry> entries, std::string_view prefix,
        std::string_view base, size_t skip_entry_index, uint32_t item_index,
        std::string_view child_prefix, std::string_view child,
        uint32_t child_index, std::string_view grandchild_prefix,
        std::string_view grandchild) noexcept
    {
        if (prefix.empty() || base.empty() || item_index == 0U || child.empty()
            || child_index == 0U || grandchild.empty()) {
            return false;
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            if (i == skip_entry_index) {
                continue;
            }
            const Entry& candidate = entries[i];
            if (any(candidate.flags, EntryFlags::Deleted)
                || candidate.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }

            const std::string_view candidate_ns
                = arena_string(arena,
                               candidate.key.data.xmp_property.schema_ns);
            std::string_view candidate_prefix;
            if (!portable_ns_to_prefix(candidate_ns, decls, &candidate_prefix)
                || candidate_prefix != prefix) {
                continue;
            }

            std::string_view candidate_base_name;
            std::string_view candidate_child_name;
            std::string_view candidate_grandchild_name;
            uint32_t candidate_item_index  = 0U;
            uint32_t candidate_child_index = 0U;
            if (!parse_indexed_structured_indexed_nested_xmp_property_name(
                    arena_string(arena,
                                 candidate.key.data.xmp_property.property_path),
                    &candidate_base_name, &candidate_item_index,
                    &candidate_child_name, &candidate_child_index,
                    &candidate_grandchild_name)
                || candidate_item_index != item_index
                || candidate_child_index != child_index) {
                continue;
            }

            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix,
                                                          candidate_base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            std::string_view portable_grandchild_prefix;
            std::string_view portable_grandchild;
            bool normalized_child      = false;
            bool normalized_grandchild = false;
            if (!resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, candidate_child_name,
                    &portable_child_prefix, &portable_child, &normalized_child)
                || !resolve_existing_xmp_nested_grandchild_to_portable(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, candidate_grandchild_name,
                    &portable_grandchild_prefix, &portable_grandchild,
                    &normalized_grandchild)) {
                continue;
            }
            if (portable_base == base && portable_child_prefix == child_prefix
                && portable_child == child
                && portable_grandchild_prefix == grandchild_prefix
                && portable_grandchild == grandchild) {
                return true;
            }
        }

        return false;
    }

    static bool existing_xmp_has_explicit_structured_nested_scalar_child(
        const ByteArena& arena, std::span<const PortableCustomNsDecl> decls,
        std::span<const Entry> entries, std::string_view prefix,
        std::string_view base, size_t skip_entry_index, uint32_t item_index,
        std::string_view child_prefix, std::string_view child,
        std::string_view grandchild_prefix,
        std::string_view grandchild) noexcept
    {
        if (prefix.empty() || base.empty() || child.empty()
            || grandchild.empty()) {
            return false;
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            if (i == skip_entry_index) {
                continue;
            }
            const Entry& candidate = entries[i];
            if (any(candidate.flags, EntryFlags::Deleted)
                || candidate.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }

            const std::string_view candidate_ns
                = arena_string(arena,
                               candidate.key.data.xmp_property.schema_ns);
            std::string_view candidate_prefix;
            if (!portable_ns_to_prefix(candidate_ns, decls, &candidate_prefix)
                || candidate_prefix != prefix) {
                continue;
            }

            std::string_view candidate_base_name;
            std::string_view candidate_child_name;
            std::string_view candidate_grandchild_name;
            uint32_t candidate_item_index = 0U;
            const bool parsed
                = item_index == 0U
                      ? parse_nested_structured_xmp_property_name(
                            arena_string(arena, candidate.key.data.xmp_property
                                                    .property_path),
                            &candidate_base_name, &candidate_child_name,
                            &candidate_grandchild_name)
                      : parse_indexed_nested_structured_xmp_property_name(
                            arena_string(arena, candidate.key.data.xmp_property
                                                    .property_path),
                            &candidate_base_name, &candidate_item_index,
                            &candidate_child_name, &candidate_grandchild_name);
            if (!parsed
                || (item_index != 0U && candidate_item_index != item_index)) {
                continue;
            }

            std::string_view candidate_child_prefix;
            std::string_view candidate_child;
            std::string_view candidate_grandchild_prefix;
            std::string_view candidate_grandchild;
            bool normalized_child      = false;
            bool normalized_grandchild = false;
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix,
                                                          candidate_base_name);
            if (!resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, candidate_child_name,
                    &candidate_child_prefix, &candidate_child,
                    &normalized_child)
                || !resolve_existing_xmp_nested_grandchild_to_portable(
                    prefix, portable_base, candidate_child_prefix,
                    candidate_child, candidate_grandchild_name,
                    &candidate_grandchild_prefix, &candidate_grandchild,
                    &normalized_grandchild)) {
                continue;
            }
            if (portable_base == base && candidate_child_prefix == child_prefix
                && candidate_child == child
                && candidate_grandchild_prefix == grandchild_prefix
                && candidate_grandchild == grandchild) {
                return true;
            }
        }

        return false;
    }

    struct PortablePropertyClaim final {
        PortablePropertyOwner owner = PortablePropertyOwner::Exif;
        PortablePropertyShape shape = PortablePropertyShape::Scalar;
    };

    struct PortableLangAltClaimKey final {
        std::string_view prefix;
        std::string_view name;
        std::string_view lang;
    };

    struct PortableLangAltClaimKeyHash final {
        size_t operator()(const PortableLangAltClaimKey& key) const noexcept
        {
            const size_t h1 = std::hash<std::string_view> {}(key.prefix);
            const size_t h2 = std::hash<std::string_view> {}(key.name);
            const size_t h3 = std::hash<std::string_view> {}(key.lang);
            size_t h        = h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
            h ^= h3 + 0x9e3779b9U + (h << 6U) + (h >> 2U);
            return h;
        }
    };

    struct PortableLangAltClaimKeyEq final {
        bool operator()(const PortableLangAltClaimKey& a,
                        const PortableLangAltClaimKey& b) const noexcept
        {
            return a.prefix == b.prefix && a.name == b.name && a.lang == b.lang;
        }
    };

    using PortablePropertyClaimMap
        = std::unordered_map<PortablePropertyKey, PortablePropertyClaim,
                             PortablePropertyKeyHash, PortablePropertyKeyEq>;
    using PortableLangAltClaimOwnerMap
        = std::unordered_map<PortableLangAltClaimKey, PortablePropertyOwner,
                             PortableLangAltClaimKeyHash,
                             PortableLangAltClaimKeyEq>;
    struct PortablePropertyGeneratedShape final {
        PortablePropertyKey key;
        PortablePropertyShape shape = PortablePropertyShape::Scalar;
    };

    struct PortablePropertyGeneratedShapeHash final {
        size_t operator()(const PortablePropertyGeneratedShape& v) const noexcept
        {
            const size_t h1 = PortablePropertyKeyHash {}(v.key);
            const size_t h2 = static_cast<size_t>(v.shape);
            return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
        }
    };

    struct PortablePropertyGeneratedShapeEq final {
        bool operator()(const PortablePropertyGeneratedShape& a,
                        const PortablePropertyGeneratedShape& b) const noexcept
        {
            return PortablePropertyKeyEq {}(a.key, b.key) && a.shape == b.shape;
        }
    };

    using PortablePropertyGeneratedShapeSet
        = std::unordered_set<PortablePropertyGeneratedShape,
                             PortablePropertyGeneratedShapeHash,
                             PortablePropertyGeneratedShapeEq>;

    struct PortableGeneratedLangAltKey final {
        PortablePropertyKey key;
        std::string_view lang;
    };

    struct PortableGeneratedLangAltKeyHash final {
        size_t operator()(const PortableGeneratedLangAltKey& v) const noexcept
        {
            const size_t h1 = PortablePropertyKeyHash {}(v.key);
            const size_t h2 = std::hash<std::string_view> {}(v.lang);
            return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
        }
    };

    struct PortableGeneratedLangAltKeyEq final {
        bool operator()(const PortableGeneratedLangAltKey& a,
                        const PortableGeneratedLangAltKey& b) const noexcept
        {
            return PortablePropertyKeyEq {}(a.key, b.key) && a.lang == b.lang;
        }
    };

    using PortableGeneratedLangAltKeySet
        = std::unordered_set<PortableGeneratedLangAltKey,
                             PortableGeneratedLangAltKeyHash,
                             PortableGeneratedLangAltKeyEq>;

    struct PortableStructuredChildClaimKey final {
        std::string_view prefix;
        std::string_view base;
        std::string_view child_prefix;
        std::string_view child;
    };

    struct PortableStructuredChildClaimKeyHash final {
        size_t
        operator()(const PortableStructuredChildClaimKey& v) const noexcept
        {
            const size_t h1 = std::hash<std::string_view> {}(v.prefix);
            const size_t h2 = std::hash<std::string_view> {}(v.base);
            const size_t h3 = std::hash<std::string_view> {}(v.child_prefix);
            const size_t h4 = std::hash<std::string_view> {}(v.child);
            size_t h        = h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
            h ^= h3 + 0x9e3779b9U + (h << 6U) + (h >> 2U);
            h ^= h4 + 0x9e3779b9U + (h << 6U) + (h >> 2U);
            return h;
        }
    };

    struct PortableStructuredChildClaimKeyEq final {
        bool operator()(const PortableStructuredChildClaimKey& a,
                        const PortableStructuredChildClaimKey& b) const noexcept
        {
            return a.prefix == b.prefix && a.base == b.base
                   && a.child_prefix == b.child_prefix && a.child == b.child;
        }
    };

    struct PortableIndexedStructuredChildClaimKey final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child_prefix;
        std::string_view child;
    };

    struct PortableIndexedStructuredChildClaimKeyHash final {
        size_t operator()(
            const PortableIndexedStructuredChildClaimKey& v) const noexcept
        {
            const size_t h1 = std::hash<std::string_view> {}(v.prefix);
            const size_t h2 = std::hash<std::string_view> {}(v.base);
            const size_t h3 = std::hash<uint32_t> {}(v.item_index);
            const size_t h4 = std::hash<std::string_view> {}(v.child_prefix);
            const size_t h5 = std::hash<std::string_view> {}(v.child);
            size_t h        = h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
            h ^= h3 + 0x9e3779b9U + (h << 6U) + (h >> 2U);
            h ^= h4 + 0x9e3779b9U + (h << 6U) + (h >> 2U);
            h ^= h5 + 0x9e3779b9U + (h << 6U) + (h >> 2U);
            return h;
        }
    };

    struct PortableIndexedStructuredChildClaimKeyEq final {
        bool operator()(
            const PortableIndexedStructuredChildClaimKey& a,
            const PortableIndexedStructuredChildClaimKey& b) const noexcept
        {
            return a.prefix == b.prefix && a.base == b.base
                   && a.item_index == b.item_index
                   && a.child_prefix == b.child_prefix && a.child == b.child;
        }
    };

    struct PortableStructuredLangAltClaimKey final {
        std::string_view prefix;
        std::string_view base;
        std::string_view child_prefix;
        std::string_view child;
        std::string_view lang;
    };

    struct PortableStructuredLangAltClaimKeyHash final {
        size_t
        operator()(const PortableStructuredLangAltClaimKey& v) const noexcept
        {
            const size_t h1 = PortableStructuredChildClaimKeyHash {}(
                PortableStructuredChildClaimKey { v.prefix, v.base,
                                                  v.child_prefix, v.child });
            const size_t h2 = std::hash<std::string_view> {}(v.lang);
            return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
        }
    };

    struct PortableStructuredLangAltClaimKeyEq final {
        bool
        operator()(const PortableStructuredLangAltClaimKey& a,
                   const PortableStructuredLangAltClaimKey& b) const noexcept
        {
            return a.prefix == b.prefix && a.base == b.base
                   && a.child_prefix == b.child_prefix && a.child == b.child
                   && a.lang == b.lang;
        }
    };

    struct PortableIndexedStructuredLangAltClaimKey final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child_prefix;
        std::string_view child;
        std::string_view lang;
    };

    struct PortableIndexedStructuredLangAltClaimKeyHash final {
        size_t operator()(
            const PortableIndexedStructuredLangAltClaimKey& v) const noexcept
        {
            const size_t h1 = PortableIndexedStructuredChildClaimKeyHash {}(
                PortableIndexedStructuredChildClaimKey {
                    v.prefix, v.base, v.item_index, v.child_prefix, v.child });
            const size_t h2 = std::hash<std::string_view> {}(v.lang);
            return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
        }
    };

    struct PortableIndexedStructuredLangAltClaimKeyEq final {
        bool operator()(
            const PortableIndexedStructuredLangAltClaimKey& a,
            const PortableIndexedStructuredLangAltClaimKey& b) const noexcept
        {
            return a.prefix == b.prefix && a.base == b.base
                   && a.item_index == b.item_index
                   && a.child_prefix == b.child_prefix && a.child == b.child
                   && a.lang == b.lang;
        }
    };

    using PortableStructuredChildClaimMap = std::unordered_map<
        PortableStructuredChildClaimKey, PortableStructuredChildShape,
        PortableStructuredChildClaimKeyHash, PortableStructuredChildClaimKeyEq>;
    using PortableIndexedStructuredChildClaimMap
        = std::unordered_map<PortableIndexedStructuredChildClaimKey,
                             PortableStructuredChildShape,
                             PortableIndexedStructuredChildClaimKeyHash,
                             PortableIndexedStructuredChildClaimKeyEq>;
    using PortableStructuredLangAltClaimOwnerMap
        = std::unordered_map<PortableStructuredLangAltClaimKey,
                             PortablePropertyOwner,
                             PortableStructuredLangAltClaimKeyHash,
                             PortableStructuredLangAltClaimKeyEq>;
    using PortableIndexedStructuredLangAltClaimOwnerMap
        = std::unordered_map<PortableIndexedStructuredLangAltClaimKey,
                             PortablePropertyOwner,
                             PortableIndexedStructuredLangAltClaimKeyHash,
                             PortableIndexedStructuredLangAltClaimKeyEq>;

    struct PortableStructuredNestedClaimKey final {
        std::string_view prefix;
        std::string_view base;
        std::string_view child;
        std::string_view grandchild;
    };

    struct PortableStructuredNestedClaimKeyHash final {
        size_t
        operator()(const PortableStructuredNestedClaimKey& v) const noexcept
        {
            const size_t h1 = PortableStructuredChildClaimKeyHash {}(
                PortableStructuredChildClaimKey {
                    v.prefix, v.base, std::string_view {}, v.child });
            const size_t h2 = std::hash<std::string_view> {}(v.grandchild);
            return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
        }
    };

    struct PortableStructuredNestedClaimKeyEq final {
        bool operator()(const PortableStructuredNestedClaimKey& a,
                        const PortableStructuredNestedClaimKey& b) const noexcept
        {
            return a.prefix == b.prefix && a.base == b.base
                   && a.child == b.child && a.grandchild == b.grandchild;
        }
    };

    struct PortableIndexedStructuredNestedClaimKey final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child;
        std::string_view grandchild;
    };

    struct PortableIndexedStructuredNestedClaimKeyHash final {
        size_t operator()(
            const PortableIndexedStructuredNestedClaimKey& v) const noexcept
        {
            const size_t h1 = PortableIndexedStructuredChildClaimKeyHash {}(
                PortableIndexedStructuredChildClaimKey {
                    v.prefix, v.base, v.item_index, std::string_view {},
                    v.child });
            const size_t h2 = std::hash<std::string_view> {}(v.grandchild);
            return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
        }
    };

    struct PortableIndexedStructuredNestedClaimKeyEq final {
        bool operator()(
            const PortableIndexedStructuredNestedClaimKey& a,
            const PortableIndexedStructuredNestedClaimKey& b) const noexcept
        {
            return a.prefix == b.prefix && a.base == b.base
                   && a.item_index == b.item_index && a.child == b.child
                   && a.grandchild == b.grandchild;
        }
    };

    using PortableStructuredNestedClaimOwnerMap
        = std::unordered_map<PortableStructuredNestedClaimKey,
                             PortablePropertyOwner,
                             PortableStructuredNestedClaimKeyHash,
                             PortableStructuredNestedClaimKeyEq>;
    using PortableIndexedStructuredNestedClaimOwnerMap
        = std::unordered_map<PortableIndexedStructuredNestedClaimKey,
                             PortablePropertyOwner,
                             PortableIndexedStructuredNestedClaimKeyHash,
                             PortableIndexedStructuredNestedClaimKeyEq>;

    using PortableStructuredNestedChildClaimMap
        = std::unordered_map<PortableStructuredNestedClaimKey,
                             PortableStructuredChildShape,
                             PortableStructuredNestedClaimKeyHash,
                             PortableStructuredNestedClaimKeyEq>;
    using PortableIndexedStructuredNestedChildClaimMap
        = std::unordered_map<PortableIndexedStructuredNestedClaimKey,
                             PortableStructuredChildShape,
                             PortableIndexedStructuredNestedClaimKeyHash,
                             PortableIndexedStructuredNestedClaimKeyEq>;

    struct PortableIndexedStructuredIndexedNestedClaimKey final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child_prefix;
        std::string_view child;
        uint32_t child_index = 0U;
        std::string_view grandchild;
    };

    struct PortableIndexedStructuredIndexedNestedClaimKeyHash final {
        size_t
        operator()(const PortableIndexedStructuredIndexedNestedClaimKey& v)
            const noexcept
        {
            const size_t h1 = PortableIndexedStructuredChildClaimKeyHash {}(
                PortableIndexedStructuredChildClaimKey {
                    v.prefix, v.base, v.item_index, v.child_prefix, v.child });
            const size_t h2 = std::hash<uint32_t> {}(v.child_index);
            const size_t h3 = std::hash<std::string_view> {}(v.grandchild);
            size_t h        = h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
            h ^= h3 + 0x9e3779b9U + (h << 6U) + (h >> 2U);
            return h;
        }
    };

    struct PortableIndexedStructuredIndexedNestedClaimKeyEq final {
        bool operator()(const PortableIndexedStructuredIndexedNestedClaimKey& a,
                        const PortableIndexedStructuredIndexedNestedClaimKey& b)
            const noexcept
        {
            return a.prefix == b.prefix && a.base == b.base
                   && a.item_index == b.item_index
                   && a.child_prefix == b.child_prefix && a.child == b.child
                   && a.child_index == b.child_index
                   && a.grandchild == b.grandchild;
        }
    };

    using PortableIndexedStructuredIndexedNestedClaimOwnerMap
        = std::unordered_map<PortableIndexedStructuredIndexedNestedClaimKey,
                             PortablePropertyOwner,
                             PortableIndexedStructuredIndexedNestedClaimKeyHash,
                             PortableIndexedStructuredIndexedNestedClaimKeyEq>;
    using PortableIndexedStructuredIndexedNestedChildClaimMap
        = std::unordered_map<PortableIndexedStructuredIndexedNestedClaimKey,
                             PortableStructuredChildShape,
                             PortableIndexedStructuredIndexedNestedClaimKeyHash,
                             PortableIndexedStructuredIndexedNestedClaimKeyEq>;

    struct PortableIndexedStructuredDeepNestedClaimKey final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child;
        std::string_view grandchild_prefix;
        std::string_view grandchild;
        std::string_view leaf_prefix;
        std::string_view leaf;
    };

    struct PortableIndexedStructuredDeepNestedClaimKeyHash final {
        size_t operator()(
            const PortableIndexedStructuredDeepNestedClaimKey& v) const noexcept
        {
            const size_t h1 = PortableIndexedStructuredNestedClaimKeyHash {}(
                PortableIndexedStructuredNestedClaimKey {
                    v.prefix, v.base, v.item_index, v.child, v.grandchild });
            const size_t h2 = std::hash<std::string_view> {}(
                v.grandchild_prefix);
            const size_t h3 = std::hash<std::string_view> {}(v.leaf_prefix);
            const size_t h4 = std::hash<std::string_view> {}(v.leaf);
            size_t h        = h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
            h ^= h3 + 0x9e3779b9U + (h << 6U) + (h >> 2U);
            h ^= h4 + 0x9e3779b9U + (h << 6U) + (h >> 2U);
            return h;
        }
    };

    struct PortableIndexedStructuredDeepNestedClaimKeyEq final {
        bool operator()(
            const PortableIndexedStructuredDeepNestedClaimKey& a,
            const PortableIndexedStructuredDeepNestedClaimKey& b) const noexcept
        {
            return a.prefix == b.prefix && a.base == b.base
                   && a.item_index == b.item_index && a.child == b.child
                   && a.grandchild_prefix == b.grandchild_prefix
                   && a.grandchild == b.grandchild
                   && a.leaf_prefix == b.leaf_prefix && a.leaf == b.leaf;
        }
    };

    using PortableIndexedStructuredDeepNestedClaimOwnerMap
        = std::unordered_map<PortableIndexedStructuredDeepNestedClaimKey,
                             PortablePropertyOwner,
                             PortableIndexedStructuredDeepNestedClaimKeyHash,
                             PortableIndexedStructuredDeepNestedClaimKeyEq>;

    struct PortableStructuredNestedLangAltClaimKey final {
        std::string_view prefix;
        std::string_view base;
        std::string_view child;
        std::string_view grandchild;
        std::string_view lang;
    };

    struct PortableStructuredNestedLangAltClaimKeyHash final {
        size_t operator()(
            const PortableStructuredNestedLangAltClaimKey& v) const noexcept
        {
            const size_t h1 = PortableStructuredNestedClaimKeyHash {}(
                PortableStructuredNestedClaimKey { v.prefix, v.base, v.child,
                                                   v.grandchild });
            const size_t h2 = std::hash<std::string_view> {}(v.lang);
            return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
        }
    };

    struct PortableStructuredNestedLangAltClaimKeyEq final {
        bool operator()(
            const PortableStructuredNestedLangAltClaimKey& a,
            const PortableStructuredNestedLangAltClaimKey& b) const noexcept
        {
            return a.prefix == b.prefix && a.base == b.base
                   && a.child == b.child && a.grandchild == b.grandchild
                   && a.lang == b.lang;
        }
    };

    struct PortableIndexedStructuredNestedLangAltClaimKey final {
        std::string_view prefix;
        std::string_view base;
        uint32_t item_index = 0U;
        std::string_view child;
        std::string_view grandchild;
        std::string_view lang;
    };

    struct PortableIndexedStructuredNestedLangAltClaimKeyHash final {
        size_t
        operator()(const PortableIndexedStructuredNestedLangAltClaimKey& v)
            const noexcept
        {
            const size_t h1 = PortableIndexedStructuredNestedClaimKeyHash {}(
                PortableIndexedStructuredNestedClaimKey {
                    v.prefix, v.base, v.item_index, v.child, v.grandchild });
            const size_t h2 = std::hash<std::string_view> {}(v.lang);
            return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
        }
    };

    struct PortableIndexedStructuredNestedLangAltClaimKeyEq final {
        bool operator()(const PortableIndexedStructuredNestedLangAltClaimKey& a,
                        const PortableIndexedStructuredNestedLangAltClaimKey& b)
            const noexcept
        {
            return a.prefix == b.prefix && a.base == b.base
                   && a.item_index == b.item_index && a.child == b.child
                   && a.grandchild == b.grandchild && a.lang == b.lang;
        }
    };

    using PortableStructuredNestedLangAltClaimOwnerMap
        = std::unordered_map<PortableStructuredNestedLangAltClaimKey,
                             PortablePropertyOwner,
                             PortableStructuredNestedLangAltClaimKeyHash,
                             PortableStructuredNestedLangAltClaimKeyEq>;
    using PortableIndexedStructuredNestedLangAltClaimOwnerMap
        = std::unordered_map<PortableIndexedStructuredNestedLangAltClaimKey,
                             PortablePropertyOwner,
                             PortableIndexedStructuredNestedLangAltClaimKeyHash,
                             PortableIndexedStructuredNestedLangAltClaimKeyEq>;

    static bool portable_property_shape_is_present(
        const PortablePropertyGeneratedShapeSet* keys, std::string_view prefix,
        std::string_view name, PortablePropertyShape shape) noexcept
    {
        if (!keys || prefix.empty() || name.empty()) {
            return false;
        }
        return keys->find(PortablePropertyGeneratedShape {
                   PortablePropertyKey { prefix, name }, shape })
               != keys->end();
    }

    static bool portable_generated_lang_alt_is_present(
        const PortableGeneratedLangAltKeySet* keys, std::string_view prefix,
        std::string_view name, std::string_view lang) noexcept
    {
        if (!keys || prefix.empty() || name.empty() || lang.empty()) {
            return false;
        }
        return keys->find(PortableGeneratedLangAltKey {
                   PortablePropertyKey { prefix, name }, lang })
               != keys->end();
    }

    static bool claim_portable_property_key(PortablePropertyClaimMap* claims,
                                            std::string_view prefix,
                                            std::string_view name,
                                            PortablePropertyOwner owner,
                                            PortablePropertyShape shape,
                                            bool* out_new_claim) noexcept
    {
        if (!claims || prefix.empty() || name.empty() || !out_new_claim) {
            return false;
        }

        *out_new_claim = false;
        const PortablePropertyKey key { prefix, name };
        const PortablePropertyClaimMap::const_iterator it = claims->find(key);
        if (it == claims->end()) {
            (*claims)[key] = PortablePropertyClaim { owner, shape };
            *out_new_claim = true;
            return true;
        }

        return it->second.owner == owner && it->second.shape == shape;
    }

    static bool claim_portable_lang_alt_property_key(
        PortablePropertyClaimMap* claims,
        PortableLangAltClaimOwnerMap* lang_alt_claims, std::string_view prefix,
        std::string_view name, std::string_view lang,
        PortablePropertyOwner owner, bool* out_new_claim) noexcept
    {
        if (!claims || !lang_alt_claims || prefix.empty() || name.empty()
            || lang.empty() || !out_new_claim) {
            return false;
        }

        *out_new_claim = false;
        const PortablePropertyKey key { prefix, name };
        const PortablePropertyClaimMap::const_iterator base_it = claims->find(
            key);
        if (base_it == claims->end()) {
            (*claims)[key] = PortablePropertyClaim {
                owner, PortablePropertyShape::LangAlt
            };
        } else if (base_it->second.shape != PortablePropertyShape::LangAlt) {
            return false;
        }

        const PortableLangAltClaimKey lang_key { prefix, name, lang };
        const PortableLangAltClaimOwnerMap::const_iterator it
            = lang_alt_claims->find(lang_key);
        if (it == lang_alt_claims->end()) {
            (*lang_alt_claims)[lang_key] = owner;
            *out_new_claim               = true;
            return true;
        }

        return it->second == owner;
    }

    static bool claim_portable_structured_child_key(
        PortableStructuredChildClaimMap* claims, std::string_view prefix,
        std::string_view base, std::string_view child_prefix,
        std::string_view child, PortableStructuredChildShape shape) noexcept
    {
        if (!claims || prefix.empty() || base.empty() || child.empty()) {
            return false;
        }

        const PortableStructuredChildClaimKey key { prefix, base, child_prefix,
                                                    child };
        const PortableStructuredChildClaimMap::const_iterator it = claims->find(
            key);
        if (it == claims->end()) {
            (*claims)[key] = shape;
            return true;
        }
        return it->second == shape;
    }

    static bool claim_portable_indexed_structured_child_key(
        PortableIndexedStructuredChildClaimMap* claims, std::string_view prefix,
        std::string_view base, uint32_t item_index,
        std::string_view child_prefix, std::string_view child,
        PortableStructuredChildShape shape) noexcept
    {
        if (!claims || prefix.empty() || base.empty() || item_index == 0U
            || child.empty()) {
            return false;
        }

        const PortableIndexedStructuredChildClaimKey key {
            prefix, base, item_index, child_prefix, child
        };
        const PortableIndexedStructuredChildClaimMap::const_iterator it
            = claims->find(key);
        if (it == claims->end()) {
            (*claims)[key] = shape;
            return true;
        }
        return it->second == shape;
    }

    static bool claim_portable_structured_lang_alt_property_key(
        PortableStructuredChildClaimMap* child_claims,
        PortableStructuredLangAltClaimOwnerMap* lang_alt_claims,
        std::string_view prefix, std::string_view base,
        std::string_view child_prefix, std::string_view child,
        std::string_view lang, PortablePropertyOwner owner,
        bool* out_new_claim) noexcept
    {
        if (!child_claims || !lang_alt_claims || prefix.empty() || base.empty()
            || child.empty() || lang.empty() || !out_new_claim) {
            return false;
        }

        *out_new_claim = false;
        if (!claim_portable_structured_child_key(
                child_claims, prefix, base, child_prefix, child,
                PortableStructuredChildShape::LangAlt)) {
            return false;
        }

        const PortableStructuredLangAltClaimKey key { prefix, base,
                                                      child_prefix, child,
                                                      lang };
        const PortableStructuredLangAltClaimOwnerMap::const_iterator it
            = lang_alt_claims->find(key);
        if (it == lang_alt_claims->end()) {
            (*lang_alt_claims)[key] = owner;
            *out_new_claim          = true;
            return true;
        }
        return it->second == owner;
    }

    static bool claim_portable_indexed_structured_lang_alt_property_key(
        PortableIndexedStructuredChildClaimMap* child_claims,
        PortableIndexedStructuredLangAltClaimOwnerMap* lang_alt_claims,
        std::string_view prefix, std::string_view base, uint32_t item_index,
        std::string_view child_prefix, std::string_view child,
        std::string_view lang, PortablePropertyOwner owner,
        bool* out_new_claim) noexcept
    {
        if (!child_claims || !lang_alt_claims || prefix.empty() || base.empty()
            || item_index == 0U || child.empty() || lang.empty()
            || !out_new_claim) {
            return false;
        }

        *out_new_claim = false;
        if (!claim_portable_indexed_structured_child_key(
                child_claims, prefix, base, item_index, child_prefix, child,
                PortableStructuredChildShape::LangAlt)) {
            return false;
        }

        const PortableIndexedStructuredLangAltClaimKey key {
            prefix, base, item_index, child_prefix, child, lang
        };
        const PortableIndexedStructuredLangAltClaimOwnerMap::const_iterator it
            = lang_alt_claims->find(key);
        if (it == lang_alt_claims->end()) {
            (*lang_alt_claims)[key] = owner;
            *out_new_claim          = true;
            return true;
        }
        return it->second == owner;
    }

    static bool claim_portable_structured_nested_property_key(
        PortableStructuredChildClaimMap* child_claims,
        PortableStructuredNestedChildClaimMap* nested_child_claims,
        PortableStructuredNestedClaimOwnerMap* nested_claims,
        std::string_view prefix, std::string_view base,
        std::string_view child_prefix, std::string_view child,
        std::string_view grandchild, PortablePropertyOwner owner,
        bool* out_new_claim) noexcept
    {
        if (!child_claims || !nested_child_claims || !nested_claims
            || prefix.empty() || base.empty() || child.empty()
            || grandchild.empty() || !out_new_claim) {
            return false;
        }

        *out_new_claim = false;
        if (!claim_portable_structured_child_key(
                child_claims, prefix, base, child_prefix, child,
                PortableStructuredChildShape::Resource)) {
            return false;
        }

        const PortableStructuredNestedClaimKey key { prefix, base, child,
                                                     grandchild };
        const PortableStructuredNestedChildClaimMap::const_iterator shape_it
            = nested_child_claims->find(key);
        if (shape_it == nested_child_claims->end()) {
            (*nested_child_claims)[key] = PortableStructuredChildShape::Scalar;
        } else if (shape_it->second != PortableStructuredChildShape::Scalar) {
            return false;
        }
        const PortableStructuredNestedClaimOwnerMap::const_iterator it
            = nested_claims->find(key);
        if (it == nested_claims->end()) {
            (*nested_claims)[key] = owner;
            *out_new_claim        = true;
            return true;
        }
        return it->second == owner;
    }

    static bool claim_portable_indexed_structured_nested_property_key(
        PortableIndexedStructuredChildClaimMap* child_claims,
        PortableIndexedStructuredNestedChildClaimMap* nested_child_claims,
        PortableIndexedStructuredNestedClaimOwnerMap* nested_claims,
        std::string_view prefix, std::string_view base, uint32_t item_index,
        std::string_view child_prefix, std::string_view child,
        std::string_view grandchild, PortablePropertyOwner owner,
        bool* out_new_claim) noexcept
    {
        if (!child_claims || !nested_child_claims || !nested_claims
            || prefix.empty() || base.empty() || item_index == 0U
            || child.empty() || grandchild.empty() || !out_new_claim) {
            return false;
        }

        *out_new_claim = false;
        if (!claim_portable_indexed_structured_child_key(
                child_claims, prefix, base, item_index, child_prefix, child,
                PortableStructuredChildShape::Resource)) {
            return false;
        }

        const PortableIndexedStructuredNestedClaimKey key { prefix, base,
                                                            item_index, child,
                                                            grandchild };
        const PortableIndexedStructuredNestedChildClaimMap::const_iterator
            shape_it
            = nested_child_claims->find(key);
        if (shape_it == nested_child_claims->end()) {
            (*nested_child_claims)[key] = PortableStructuredChildShape::Scalar;
        } else if (shape_it->second != PortableStructuredChildShape::Scalar) {
            return false;
        }
        const PortableIndexedStructuredNestedClaimOwnerMap::const_iterator it
            = nested_claims->find(key);
        if (it == nested_claims->end()) {
            (*nested_claims)[key] = owner;
            *out_new_claim        = true;
            return true;
        }
        return it->second == owner;
    }

    static bool claim_portable_structured_nested_lang_alt_property_key(
        PortableStructuredChildClaimMap* child_claims,
        PortableStructuredNestedChildClaimMap* nested_child_claims,
        PortableStructuredNestedLangAltClaimOwnerMap* lang_alt_claims,
        std::string_view prefix, std::string_view base,
        std::string_view child_prefix, std::string_view child,
        std::string_view grandchild, std::string_view lang,
        PortablePropertyOwner owner, bool* out_new_claim) noexcept
    {
        if (!child_claims || !nested_child_claims || !lang_alt_claims
            || prefix.empty() || base.empty() || child.empty()
            || grandchild.empty() || lang.empty() || !out_new_claim) {
            return false;
        }

        *out_new_claim = false;
        if (!claim_portable_structured_child_key(
                child_claims, prefix, base, child_prefix, child,
                PortableStructuredChildShape::Resource)) {
            return false;
        }

        const PortableStructuredNestedClaimKey nested_key { prefix, base, child,
                                                            grandchild };
        const PortableStructuredNestedChildClaimMap::const_iterator shape_it
            = nested_child_claims->find(nested_key);
        if (shape_it == nested_child_claims->end()) {
            (*nested_child_claims)[nested_key]
                = PortableStructuredChildShape::LangAlt;
        } else if (shape_it->second != PortableStructuredChildShape::LangAlt) {
            return false;
        }

        const PortableStructuredNestedLangAltClaimKey key { prefix, base, child,
                                                            grandchild, lang };
        const PortableStructuredNestedLangAltClaimOwnerMap::const_iterator it
            = lang_alt_claims->find(key);
        if (it == lang_alt_claims->end()) {
            (*lang_alt_claims)[key] = owner;
            *out_new_claim          = true;
            return true;
        }
        return it->second == owner;
    }

    static bool claim_portable_indexed_structured_nested_lang_alt_property_key(
        PortableIndexedStructuredChildClaimMap* child_claims,
        PortableIndexedStructuredNestedChildClaimMap* nested_child_claims,
        PortableIndexedStructuredNestedLangAltClaimOwnerMap* lang_alt_claims,
        std::string_view prefix, std::string_view base, uint32_t item_index,
        std::string_view child_prefix, std::string_view child,
        std::string_view grandchild, std::string_view lang,
        PortablePropertyOwner owner, bool* out_new_claim) noexcept
    {
        if (!child_claims || !nested_child_claims || !lang_alt_claims
            || prefix.empty() || base.empty() || item_index == 0U
            || child.empty() || grandchild.empty() || lang.empty()
            || !out_new_claim) {
            return false;
        }

        *out_new_claim = false;
        if (!claim_portable_indexed_structured_child_key(
                child_claims, prefix, base, item_index, child_prefix, child,
                PortableStructuredChildShape::Resource)) {
            return false;
        }

        const PortableIndexedStructuredNestedClaimKey nested_key {
            prefix, base, item_index, child, grandchild
        };
        const PortableIndexedStructuredNestedChildClaimMap::const_iterator
            shape_it
            = nested_child_claims->find(nested_key);
        if (shape_it == nested_child_claims->end()) {
            (*nested_child_claims)[nested_key]
                = PortableStructuredChildShape::LangAlt;
        } else if (shape_it->second != PortableStructuredChildShape::LangAlt) {
            return false;
        }

        const PortableIndexedStructuredNestedLangAltClaimKey key {
            prefix, base, item_index, child, grandchild, lang
        };
        const PortableIndexedStructuredNestedLangAltClaimOwnerMap::const_iterator
            it
            = lang_alt_claims->find(key);
        if (it == lang_alt_claims->end()) {
            (*lang_alt_claims)[key] = owner;
            *out_new_claim          = true;
            return true;
        }
        return it->second == owner;
    }

    static bool claim_portable_structured_nested_indexed_property_key(
        PortableStructuredChildClaimMap* child_claims,
        PortableStructuredNestedChildClaimMap* nested_child_claims,
        std::string_view prefix, std::string_view base,
        std::string_view child_prefix, std::string_view child,
        std::string_view grandchild) noexcept
    {
        if (!child_claims || !nested_child_claims || prefix.empty()
            || base.empty() || child.empty() || grandchild.empty()) {
            return false;
        }

        if (!claim_portable_structured_child_key(
                child_claims, prefix, base, child_prefix, child,
                PortableStructuredChildShape::Resource)) {
            return false;
        }

        const PortableStructuredNestedClaimKey key { prefix, base, child,
                                                     grandchild };
        const PortableStructuredNestedChildClaimMap::const_iterator it
            = nested_child_claims->find(key);
        if (it == nested_child_claims->end()) {
            (*nested_child_claims)[key] = PortableStructuredChildShape::Indexed;
            return true;
        }
        return it->second == PortableStructuredChildShape::Indexed;
    }

    static bool claim_portable_indexed_structured_nested_indexed_property_key(
        PortableIndexedStructuredChildClaimMap* child_claims,
        PortableIndexedStructuredNestedChildClaimMap* nested_child_claims,
        std::string_view prefix, std::string_view base, uint32_t item_index,
        std::string_view child_prefix, std::string_view child,
        std::string_view grandchild) noexcept
    {
        if (!child_claims || !nested_child_claims || prefix.empty()
            || base.empty() || item_index == 0U || child.empty()
            || grandchild.empty()) {
            return false;
        }

        if (!claim_portable_indexed_structured_child_key(
                child_claims, prefix, base, item_index, child_prefix, child,
                PortableStructuredChildShape::Resource)) {
            return false;
        }

        const PortableIndexedStructuredNestedClaimKey key { prefix, base,
                                                            item_index, child,
                                                            grandchild };
        const PortableIndexedStructuredNestedChildClaimMap::const_iterator it
            = nested_child_claims->find(key);
        if (it == nested_child_claims->end()) {
            (*nested_child_claims)[key] = PortableStructuredChildShape::Indexed;
            return true;
        }
        return it->second == PortableStructuredChildShape::Indexed;
    }

    static bool claim_portable_indexed_structured_indexed_nested_property_key(
        PortableIndexedStructuredChildClaimMap* child_claims,
        PortableIndexedStructuredIndexedNestedChildClaimMap* nested_child_claims,
        PortableIndexedStructuredIndexedNestedClaimOwnerMap* nested_claims,
        std::string_view prefix, std::string_view base, uint32_t item_index,
        std::string_view child_prefix, std::string_view child,
        uint32_t child_index, std::string_view grandchild,
        PortablePropertyOwner owner, bool* out_new_claim) noexcept
    {
        if (!child_claims || !nested_child_claims || !nested_claims
            || prefix.empty() || base.empty() || item_index == 0U
            || child.empty() || child_index == 0U || grandchild.empty()
            || !out_new_claim) {
            return false;
        }

        *out_new_claim = false;
        if (!claim_portable_indexed_structured_child_key(
                child_claims, prefix, base, item_index, child_prefix, child,
                PortableStructuredChildShape::Indexed)) {
            return false;
        }

        const PortableIndexedStructuredIndexedNestedClaimKey key {
            prefix, base,        item_index, child_prefix,
            child,  child_index, grandchild
        };
        const PortableIndexedStructuredIndexedNestedChildClaimMap::const_iterator
            shape_it
            = nested_child_claims->find(key);
        if (shape_it == nested_child_claims->end()) {
            (*nested_child_claims)[key] = PortableStructuredChildShape::Scalar;
        } else if (shape_it->second != PortableStructuredChildShape::Scalar) {
            return false;
        }
        const PortableIndexedStructuredIndexedNestedClaimOwnerMap::const_iterator
            it
            = nested_claims->find(key);
        if (it == nested_claims->end()) {
            (*nested_claims)[key] = owner;
            *out_new_claim        = true;
            return true;
        }
        return it->second == owner;
    }

    static bool claim_portable_indexed_structured_deep_nested_property_key(
        PortableIndexedStructuredChildClaimMap* child_claims,
        PortableIndexedStructuredNestedChildClaimMap* nested_child_claims,
        PortableIndexedStructuredDeepNestedClaimOwnerMap* deep_nested_claims,
        std::string_view prefix, std::string_view base, uint32_t item_index,
        std::string_view child_prefix, std::string_view child,
        std::string_view grandchild_prefix, std::string_view grandchild,
        std::string_view leaf_prefix, std::string_view leaf,
        PortablePropertyOwner owner, bool* out_new_claim) noexcept
    {
        if (!child_claims || !nested_child_claims || !deep_nested_claims
            || prefix.empty() || base.empty() || item_index == 0U
            || child.empty() || grandchild.empty() || leaf.empty()
            || !out_new_claim) {
            return false;
        }

        *out_new_claim = false;
        if (!claim_portable_indexed_structured_child_key(
                child_claims, prefix, base, item_index, child_prefix, child,
                PortableStructuredChildShape::Resource)) {
            return false;
        }

        const PortableIndexedStructuredNestedClaimKey nested_key {
            prefix, base, item_index, child, grandchild
        };
        const PortableIndexedStructuredNestedChildClaimMap::const_iterator
            shape_it
            = nested_child_claims->find(nested_key);
        if (shape_it == nested_child_claims->end()) {
            (*nested_child_claims)[nested_key]
                = PortableStructuredChildShape::Resource;
        } else if (shape_it->second != PortableStructuredChildShape::Resource) {
            return false;
        }

        const PortableIndexedStructuredDeepNestedClaimKey key {
            prefix,     base,        item_index, child, grandchild_prefix,
            grandchild, leaf_prefix, leaf
        };
        const PortableIndexedStructuredDeepNestedClaimOwnerMap::const_iterator it
            = deep_nested_claims->find(key);
        if (it == deep_nested_claims->end()) {
            (*deep_nested_claims)[key] = owner;
            *out_new_claim             = true;
            return true;
        }
        return it->second == owner;
    }

    static bool generated_replacement_exists_for_existing_base_property(
        const PortablePropertyGeneratedShapeSet* generated_shapes,
        const PortableGeneratedLangAltKeySet* generated_lang_alt,
        std::string_view prefix, std::string_view name) noexcept
    {
        if (portable_property_shape_is_present(generated_shapes, prefix, name,
                                               PortablePropertyShape::Scalar)
            || portable_property_shape_is_present(
                generated_shapes, prefix, name, PortablePropertyShape::Indexed)
            || portable_property_shape_is_present(
                generated_shapes, prefix, name,
                PortablePropertyShape::Structured)
            || portable_generated_lang_alt_is_present(generated_lang_alt,
                                                      prefix, name,
                                                      "x-default")) {
            return true;
        }
        return false;
    }

    static bool generated_replacement_exists_for_existing_flattened_nested_alias(
        const PortablePropertyGeneratedShapeSet* generated_shapes,
        const PortableGeneratedLangAltKeySet* generated_lang_alt,
        std::string_view prefix, std::string_view base, std::string_view child,
        std::string_view grandchild) noexcept
    {
        if (prefix == "Iptc4xmpExt" && base == "LocationCreated"
            && child == "Address"
            && (grandchild == "City" || grandchild == "CountryCode"
                || grandchild == "CountryName"
                || grandchild == "ProvinceState")) {
            return generated_replacement_exists_for_existing_base_property(
                generated_shapes, generated_lang_alt, "Iptc4xmpCore",
                "LocationCreated");
        }

        return false;
    }

    static bool generated_replacement_exists_for_existing_structured_child_alias(
        const PortablePropertyGeneratedShapeSet* generated_shapes,
        const PortableGeneratedLangAltKeySet* generated_lang_alt,
        std::string_view prefix, std::string_view base,
        std::string_view child_prefix, std::string_view child) noexcept
    {
        if (prefix == "Iptc4xmpExt" && base == "LocationCreated"
            && (child_prefix.empty() || child_prefix == prefix)
            && (child == "City" || child == "CountryCode"
                || child == "CountryName" || child == "ProvinceState")) {
            return generated_replacement_exists_for_existing_base_property(
                generated_shapes, generated_lang_alt, "Iptc4xmpCore",
                "LocationCreated");
        }

        return false;
    }

    static bool process_portable_existing_xmp_entry(
        const ByteArena& arena, std::span<const PortableCustomNsDecl> decls,
        std::span<const Entry> entries, const XmpPortableOptions& options,
        const Entry& e, uint32_t order,
        const PortablePropertyGeneratedShapeSet* generated_shapes,
        const PortableGeneratedLangAltKeySet* generated_lang_alt, SpanWriter* w,
        PortablePropertyClaimMap* claims,
        PortableLangAltClaimOwnerMap* lang_alt_claims,
        PortableStructuredChildClaimMap* structured_child_claims,
        PortableIndexedStructuredChildClaimMap* indexed_structured_child_claims,
        PortableStructuredLangAltClaimOwnerMap* structured_lang_alt_claims,
        PortableIndexedStructuredLangAltClaimOwnerMap*
            indexed_structured_lang_alt_claims,
        PortableStructuredNestedChildClaimMap* structured_nested_child_claims,
        PortableIndexedStructuredNestedChildClaimMap*
            indexed_structured_nested_child_claims,
        PortableStructuredNestedClaimOwnerMap* structured_nested_claims,
        PortableStructuredNestedLangAltClaimOwnerMap*
            structured_nested_lang_alt_claims,
        PortableIndexedStructuredNestedClaimOwnerMap*
            indexed_structured_nested_claims,
        PortableIndexedStructuredNestedLangAltClaimOwnerMap*
            indexed_structured_nested_lang_alt_claims,
        PortableIndexedStructuredDeepNestedClaimOwnerMap*
            indexed_structured_deep_nested_claims,
        PortableIndexedStructuredIndexedNestedChildClaimMap*
            indexed_structured_indexed_nested_child_claims,
        PortableIndexedStructuredIndexedNestedClaimOwnerMap*
            indexed_structured_indexed_nested_claims,
        std::vector<PortableIndexedProperty>* indexed,
        std::vector<PortableLangAltProperty>* lang_alt,
        std::vector<PortableStructuredProperty>* structured,
        std::vector<PortableStructuredLangAltProperty>* structured_lang_alt,
        std::vector<PortableStructuredIndexedProperty>* structured_indexed,
        std::vector<PortableStructuredNestedProperty>* structured_nested,
        std::vector<PortableStructuredNestedLangAltProperty>*
            structured_nested_lang_alt,
        std::vector<PortableStructuredNestedIndexedProperty>*
            structured_nested_indexed,
        std::vector<PortableIndexedStructuredProperty>* indexed_structured,
        std::vector<PortableIndexedStructuredLangAltProperty>*
            indexed_structured_lang_alt,
        std::vector<PortableIndexedStructuredNestedProperty>*
            indexed_structured_nested,
        std::vector<PortableIndexedStructuredNestedLangAltProperty>*
            indexed_structured_nested_lang_alt,
        std::vector<PortableIndexedStructuredNestedIndexedProperty>*
            indexed_structured_nested_indexed,
        std::vector<PortableIndexedStructuredDeepNestedProperty>*
            indexed_structured_deep_nested,
        std::vector<PortableIndexedStructuredIndexedNestedProperty>*
            indexed_structured_indexed_nested,
        std::vector<PortableIndexedStructuredIndexedProperty>*
            indexed_structured_indexed) noexcept
    {
        if (!w || !claims || !lang_alt_claims || !indexed || !lang_alt
            || !structured_child_claims || !indexed_structured_child_claims
            || !structured_lang_alt_claims
            || !indexed_structured_lang_alt_claims
            || !structured_nested_child_claims
            || !indexed_structured_nested_child_claims
            || !structured_nested_claims || !structured_nested_lang_alt_claims
            || !indexed_structured_nested_claims
            || !indexed_structured_nested_lang_alt_claims
            || !indexed_structured_deep_nested_claims
            || !indexed_structured_indexed_nested_child_claims
            || !indexed_structured_indexed_nested_claims || !structured
            || !structured_lang_alt || !structured_indexed || !structured_nested
            || !structured_nested_lang_alt || !structured_nested_indexed
            || !indexed_structured || !indexed_structured_lang_alt
            || !indexed_structured_nested || !indexed_structured_nested_lang_alt
            || !indexed_structured_nested_indexed
            || !indexed_structured_deep_nested
            || !indexed_structured_indexed_nested || !indexed_structured_indexed
            || e.key.kind != MetaKeyKind::XmpProperty) {
            return false;
        }
        const size_t current_entry_index = static_cast<size_t>(order);

        const std::string_view ns
            = arena_string(arena, e.key.data.xmp_property.schema_ns);
        const std::string_view name
            = arena_string(arena, e.key.data.xmp_property.property_path);
        std::string_view prefix;
        if (!portable_ns_to_prefix(ns, decls, &prefix)) {
            return false;
        }

        if (is_simple_xmp_property_name(name)) {
            const std::string_view portable_name
                = portable_property_name_for_existing_xmp(prefix, name);
            if (portable_name.empty()
                || xmp_property_is_nonportable_blob(prefix, portable_name)) {
                return false;
            }
            if (portable_existing_xmp_promotes_scalar_to_lang_alt(prefix,
                                                                  portable_name)
                && portable_scalar_like_value_supported(arena, e.value)
                && !existing_xmp_has_explicit_lang_alt_base(arena, decls,
                                                            entries, prefix,
                                                            portable_name)) {
                if (options.existing_standard_namespace_policy
                        == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                    && existing_standard_portable_property_is_managed(
                        prefix, portable_name)
                    && generated_replacement_exists_for_existing_base_property(
                        generated_shapes, generated_lang_alt, prefix,
                        portable_name)) {
                    return false;
                }

                bool new_claim = false;
                if (!claim_portable_lang_alt_property_key(
                        claims, lang_alt_claims, prefix, portable_name,
                        "x-default", PortablePropertyOwner::ExistingXmp,
                        &new_claim)
                    || !new_claim) {
                    return false;
                }

                PortableLangAltProperty item;
                item.prefix = prefix;
                item.base   = portable_name;
                item.lang   = "x-default";
                item.order  = order;
                item.value  = &e.value;
                lang_alt->push_back(item);
                return false;
            }
            if (portable_existing_xmp_promotes_scalar_to_indexed(prefix,
                                                                 portable_name)
                && portable_scalar_like_value_supported(arena, e.value)
                && !existing_xmp_has_explicit_indexed_base(arena, decls,
                                                           entries, prefix,
                                                           portable_name)) {
                if (options.existing_standard_namespace_policy
                        == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                    && existing_standard_portable_property_is_managed(
                        prefix, portable_name)
                    && generated_replacement_exists_for_existing_base_property(
                        generated_shapes, generated_lang_alt, prefix,
                        portable_name)) {
                    return false;
                }

                bool new_claim = false;
                if (!claim_portable_property_key(
                        claims, prefix, portable_name,
                        PortablePropertyOwner::ExistingXmp,
                        PortablePropertyShape::Indexed, &new_claim)
                    || !new_claim) {
                    return false;
                }

                PortableIndexedProperty item;
                item.prefix = prefix;
                item.base   = portable_name;
                item.index  = 1U;
                item.order  = order;
                item.value  = &e.value;
                item.container
                    = portable_existing_xmp_indexed_container(prefix,
                                                              portable_name);
                indexed->push_back(item);
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_name, PortablePropertyShape::Scalar)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_name)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_name)) {
                return false;
            }
            bool new_claim = false;
            if (!claim_portable_property_key(claims, prefix, portable_name,
                                             PortablePropertyOwner::ExistingXmp,
                                             PortablePropertyShape::Scalar,
                                             &new_claim)
                || !new_claim) {
                return false;
            }
            return emit_portable_property(w, prefix, portable_name, arena,
                                          e.value);
        }

        std::string_view base_name;
        std::string_view lang;
        if (parse_lang_alt_xmp_property_name(name, &base_name, &lang)) {
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            if (portable_base.empty()
                || xmp_property_is_nonportable_blob(prefix, portable_base)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base, PortablePropertyShape::LangAlt)) {
                PortablePropertyShape required_shape
                    = PortablePropertyShape::Scalar;
                if (portable_scalar_like_value_supported(arena, e.value)
                    && standard_existing_xmp_required_base_shape(
                        prefix, portable_base, &required_shape)
                    && required_shape == PortablePropertyShape::Indexed
                    && lang == "x-default"
                    && !existing_xmp_has_explicit_indexed_base(arena, decls,
                                                               entries, prefix,
                                                               portable_base)) {
                    if (options.existing_standard_namespace_policy
                            == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                        && existing_standard_portable_property_is_managed(
                            prefix, portable_base)
                        && generated_replacement_exists_for_existing_base_property(
                            generated_shapes, generated_lang_alt, prefix,
                            portable_base)) {
                        return false;
                    }

                    bool new_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::Indexed, &new_claim)
                        || !new_claim) {
                        return false;
                    }

                    PortableIndexedProperty item;
                    item.prefix    = prefix;
                    item.base      = portable_base;
                    item.index     = 1U;
                    item.order     = order;
                    item.value     = &e.value;
                    item.container = portable_existing_xmp_indexed_container(
                        prefix, portable_base);
                    indexed->push_back(item);
                }
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(
                    prefix, portable_base)) {
                if (portable_generated_lang_alt_is_present(
                        generated_lang_alt, prefix, portable_base, lang)) {
                    return false;
                }
                if (!portable_property_prefers_lang_alt(prefix, portable_base)
                    && generated_replacement_exists_for_existing_base_property(
                        generated_shapes, generated_lang_alt, prefix,
                        portable_base)) {
                    return false;
                }
            }

            bool new_claim = false;
            if (!claim_portable_lang_alt_property_key(
                    claims, lang_alt_claims, prefix, portable_base, lang,
                    PortablePropertyOwner::ExistingXmp, &new_claim)) {
                return false;
            }

            PortableLangAltProperty item;
            item.prefix = prefix;
            item.base   = portable_base;
            item.lang   = lang;
            item.order  = order;
            item.value  = &e.value;
            lang_alt->push_back(item);
            return false;
        }

        uint32_t index = 0U;
        if (parse_indexed_xmp_property_name(name, &base_name, &index)) {
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            if (portable_base.empty()
                || xmp_property_is_nonportable_blob(prefix, portable_base)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base, PortablePropertyShape::Indexed)) {
                PortablePropertyShape required_shape
                    = PortablePropertyShape::Scalar;
                if (portable_scalar_like_value_supported(arena, e.value)
                    && standard_existing_xmp_required_base_shape(
                        prefix, portable_base, &required_shape)
                    && required_shape == PortablePropertyShape::LangAlt
                    && index == 1U
                    && !existing_xmp_has_explicit_lang_alt_base(
                        arena, decls, entries, prefix, portable_base)) {
                    if (options.existing_standard_namespace_policy
                            == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                        && existing_standard_portable_property_is_managed(
                            prefix, portable_base)
                        && generated_replacement_exists_for_existing_base_property(
                            generated_shapes, generated_lang_alt, prefix,
                            portable_base)) {
                        return false;
                    }

                    bool new_claim = false;
                    if (!claim_portable_lang_alt_property_key(
                            claims, lang_alt_claims, prefix, portable_base,
                            "x-default", PortablePropertyOwner::ExistingXmp,
                            &new_claim)
                        || !new_claim) {
                        return false;
                    }

                    PortableLangAltProperty item;
                    item.prefix = prefix;
                    item.base   = portable_base;
                    item.lang   = "x-default";
                    item.order  = order;
                    item.value  = &e.value;
                    lang_alt->push_back(item);
                }
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }

            bool new_claim = false;
            if (!claim_portable_property_key(claims, prefix, portable_base,
                                             PortablePropertyOwner::ExistingXmp,
                                             PortablePropertyShape::Indexed,
                                             &new_claim)) {
                return false;
            }

            PortableIndexedProperty item;
            item.prefix = prefix;
            item.base   = portable_base;
            item.index  = index;
            item.order  = order;
            item.value  = &e.value;
            item.container
                = portable_existing_xmp_indexed_container(prefix,
                                                          portable_base);
            indexed->push_back(item);
            return false;
        }

        std::string_view child_name;
        std::string_view grandchild_name;
        if (parse_nested_structured_xmp_property_name(name, &base_name,
                                                      &child_name,
                                                      &grandchild_name)) {
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            const std::string_view portable_child
                = portable_property_name_for_existing_xmp(prefix, child_name);
            const std::string_view portable_grandchild
                = portable_property_name_for_existing_xmp(prefix,
                                                          grandchild_name);
            if (portable_base.empty() || portable_child.empty()
                || portable_grandchild.empty()
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(prefix, portable_child)
                || xmp_property_is_nonportable_blob(prefix, portable_grandchild)
                || !portable_scalar_like_value_supported(arena, e.value)) {
                return false;
            }
            std::string_view flattened_child;
            if (standard_existing_xmp_flattened_nested_alias(
                    prefix, portable_base, portable_child, portable_grandchild,
                    &flattened_child)) {
                if (!standard_existing_xmp_base_accepts_shape(
                        prefix, portable_base,
                        PortablePropertyShape::Structured)) {
                    return false;
                }
                if (!standard_existing_xmp_structured_child_accepts_shape(
                        prefix, portable_base, prefix, flattened_child,
                        PortableStructuredChildShape::Scalar)) {
                    return false;
                }
                if (options.existing_standard_namespace_policy
                        == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                    && ((existing_standard_portable_property_is_managed(
                             prefix, portable_base)
                         && generated_replacement_exists_for_existing_base_property(
                             generated_shapes, generated_lang_alt, prefix,
                             portable_base))
                        || generated_replacement_exists_for_existing_flattened_nested_alias(
                            generated_shapes, generated_lang_alt, prefix,
                            portable_base, portable_child,
                            portable_grandchild))) {
                    return false;
                }

                bool new_base_claim = false;
                if (!claim_portable_property_key(
                        claims, prefix, portable_base,
                        PortablePropertyOwner::ExistingXmp,
                        PortablePropertyShape::Structured, &new_base_claim)) {
                    return false;
                }
                if (!claim_portable_structured_child_key(
                        structured_child_claims, prefix, portable_base, prefix,
                        flattened_child,
                        PortableStructuredChildShape::Scalar)) {
                    return false;
                }

                PortableStructuredProperty item;
                item.prefix       = prefix;
                item.base         = portable_base;
                item.child_prefix = prefix;
                item.child        = flattened_child;
                item.order        = order;
                item.value        = &e.value;
                structured->push_back(item);
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base, PortablePropertyShape::Structured)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }

            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, std::string_view {}, portable_child,
                    PortableStructuredChildShape::Resource)) {
                return false;
            }
            PortableStructuredChildShape required_nested_shape
                = PortableStructuredChildShape::Scalar;
            const bool has_required_nested_shape
                = standard_existing_xmp_required_nested_child_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    &required_nested_shape);
            if (!standard_existing_xmp_nested_child_accepts_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    PortableStructuredChildShape::Scalar)) {
                if (has_required_nested_shape
                    && required_nested_shape
                           == PortableStructuredChildShape::LangAlt
                    && !existing_xmp_has_explicit_structured_nested_lang_alt_child(
                        arena, decls, entries, prefix, portable_base, 0U,
                        portable_child, portable_grandchild)) {
                    bool new_base_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::Structured,
                            &new_base_claim)) {
                        return false;
                    }

                    bool new_nested_claim = false;
                    if (!claim_portable_structured_nested_lang_alt_property_key(
                            structured_child_claims,
                            structured_nested_child_claims,
                            structured_nested_lang_alt_claims, prefix,
                            portable_base, std::string_view {}, portable_child,
                            portable_grandchild, "x-default",
                            PortablePropertyOwner::ExistingXmp,
                            &new_nested_claim)
                        || !new_nested_claim) {
                        return false;
                    }

                    PortableStructuredNestedLangAltProperty item;
                    item.prefix     = prefix;
                    item.base       = portable_base;
                    item.child      = portable_child;
                    item.grandchild = portable_grandchild;
                    item.lang       = "x-default";
                    item.order      = order;
                    item.value      = &e.value;
                    structured_nested_lang_alt->push_back(item);
                } else if (has_required_nested_shape
                           && required_nested_shape
                                  == PortableStructuredChildShape::Indexed
                           && !existing_xmp_has_explicit_structured_nested_indexed_child(
                               arena, decls, entries, prefix, portable_base, 0U,
                               portable_child, portable_grandchild)) {
                    bool new_base_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::Structured,
                            &new_base_claim)) {
                        return false;
                    }

                    if (!claim_portable_structured_nested_indexed_property_key(
                            structured_child_claims,
                            structured_nested_child_claims, prefix,
                            portable_base, std::string_view {}, portable_child,
                            portable_grandchild)) {
                        return false;
                    }

                    PortableStructuredNestedIndexedProperty item;
                    item.prefix     = prefix;
                    item.base       = portable_base;
                    item.child      = portable_child;
                    item.grandchild = portable_grandchild;
                    item.index      = 1U;
                    item.order      = order;
                    item.value      = &e.value;
                    item.container  = portable_existing_xmp_indexed_container(
                        prefix, portable_grandchild);
                    structured_nested_indexed->push_back(item);
                }
                return false;
            }

            bool new_base_claim = false;
            if (!claim_portable_property_key(claims, prefix, portable_base,
                                             PortablePropertyOwner::ExistingXmp,
                                             PortablePropertyShape::Structured,
                                             &new_base_claim)) {
                return false;
            }

            bool new_nested_claim = false;
            if (!claim_portable_structured_nested_property_key(
                    structured_child_claims, structured_nested_child_claims,
                    structured_nested_claims, prefix, portable_base,
                    std::string_view {}, portable_child, portable_grandchild,
                    PortablePropertyOwner::ExistingXmp, &new_nested_claim)
                || !new_nested_claim) {
                return false;
            }

            PortableStructuredNestedProperty item;
            item.prefix     = prefix;
            item.base       = portable_base;
            item.child      = portable_child;
            item.grandchild = portable_grandchild;
            item.order      = order;
            item.value      = &e.value;
            structured_nested->push_back(item);
            return false;
        }

        uint32_t nested_item_index = 0U;
        if (parse_indexed_nested_structured_xmp_property_name(
                name, &base_name, &nested_item_index, &child_name,
                &grandchild_name)) {
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            std::string_view portable_grandchild_prefix;
            std::string_view portable_grandchild;
            bool normalized_child      = false;
            bool normalized_grandchild = false;
            if (portable_base.empty()
                || !resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, child_name, &portable_child_prefix,
                    &portable_child, &normalized_child)
                || !resolve_existing_xmp_nested_grandchild_to_portable(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, grandchild_name,
                    &portable_grandchild_prefix, &portable_grandchild,
                    &normalized_grandchild)
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                    portable_child)
                || xmp_property_is_nonportable_blob(portable_grandchild_prefix,
                                                    portable_grandchild)
                || !portable_scalar_like_value_supported(arena, e.value)) {
                return false;
            }
            std::string_view flattened_child;
            if (standard_existing_xmp_flattened_nested_alias(
                    prefix, portable_base, portable_child, portable_grandchild,
                    &flattened_child)) {
                if (!standard_existing_xmp_base_accepts_shape(
                        prefix, portable_base,
                        PortablePropertyShape::StructuredIndexed)) {
                    return false;
                }
                if (!standard_existing_xmp_structured_child_accepts_shape(
                        prefix, portable_base, prefix, flattened_child,
                        PortableStructuredChildShape::Scalar)) {
                    return false;
                }
                if (options.existing_standard_namespace_policy
                        == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                    && existing_standard_portable_property_is_managed(
                        prefix, portable_base)
                    && generated_replacement_exists_for_existing_base_property(
                        generated_shapes, generated_lang_alt, prefix,
                        portable_base)) {
                    return false;
                }

                bool new_base_claim = false;
                if (!claim_portable_property_key(
                        claims, prefix, portable_base,
                        PortablePropertyOwner::ExistingXmp,
                        PortablePropertyShape::StructuredIndexed,
                        &new_base_claim)) {
                    return false;
                }
                if (!claim_portable_indexed_structured_child_key(
                        indexed_structured_child_claims, prefix, portable_base,
                        nested_item_index, prefix, flattened_child,
                        PortableStructuredChildShape::Scalar)) {
                    return false;
                }

                PortableIndexedStructuredProperty item;
                item.prefix       = prefix;
                item.base         = portable_base;
                item.item_index   = nested_item_index;
                item.child_prefix = prefix;
                item.child        = flattened_child;
                item.order        = order;
                item.value        = &e.value;
                item.container
                    = portable_existing_xmp_indexed_container(prefix,
                                                              portable_base);
                indexed_structured->push_back(item);
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base,
                    PortablePropertyShape::StructuredIndexed)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }

            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, PortableStructuredChildShape::Resource)) {
                return false;
            }
            PortableStructuredChildShape required_nested_shape
                = PortableStructuredChildShape::Scalar;
            const bool has_required_nested_shape
                = standard_existing_xmp_required_nested_child_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    &required_nested_shape);
            if (!standard_existing_xmp_nested_child_accepts_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    PortableStructuredChildShape::Scalar)) {
                if (has_required_nested_shape
                    && required_nested_shape
                           == PortableStructuredChildShape::LangAlt
                    && !existing_xmp_has_explicit_structured_nested_lang_alt_child(
                        arena, decls, entries, prefix, portable_base,
                        nested_item_index, portable_child,
                        portable_grandchild)) {
                    bool new_base_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::StructuredIndexed,
                            &new_base_claim)) {
                        return false;
                    }

                    bool new_nested_claim = false;
                    if (!claim_portable_indexed_structured_nested_lang_alt_property_key(
                            indexed_structured_child_claims,
                            indexed_structured_nested_child_claims,
                            indexed_structured_nested_lang_alt_claims, prefix,
                            portable_base, nested_item_index,
                            portable_child_prefix, portable_child,
                            portable_grandchild, "x-default",
                            PortablePropertyOwner::ExistingXmp,
                            &new_nested_claim)
                        || !new_nested_claim) {
                        return false;
                    }

                    PortableIndexedStructuredNestedLangAltProperty item;
                    item.prefix     = prefix;
                    item.base       = portable_base;
                    item.item_index = nested_item_index;
                    item.child      = child_name;
                    item.grandchild = grandchild_name;
                    item.lang       = "x-default";
                    item.order      = order;
                    item.value      = &e.value;
                    indexed_structured_nested_lang_alt->push_back(item);
                } else if (has_required_nested_shape
                           && required_nested_shape
                                  == PortableStructuredChildShape::Indexed
                           && !existing_xmp_has_explicit_structured_nested_indexed_child(
                               arena, decls, entries, prefix, portable_base,
                               nested_item_index, portable_child,
                               portable_grandchild)) {
                    bool new_base_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::StructuredIndexed,
                            &new_base_claim)) {
                        return false;
                    }

                    if (!claim_portable_indexed_structured_nested_indexed_property_key(
                            indexed_structured_child_claims,
                            indexed_structured_nested_child_claims, prefix,
                            portable_base, nested_item_index,
                            portable_child_prefix, portable_child,
                            portable_grandchild)) {
                        return false;
                    }

                    PortableIndexedStructuredNestedIndexedProperty item;
                    item.prefix     = prefix;
                    item.base       = portable_base;
                    item.item_index = nested_item_index;
                    item.child      = child_name;
                    item.grandchild = grandchild_name;
                    item.index      = 1U;
                    item.order      = order;
                    item.value      = &e.value;
                    item.container  = portable_existing_xmp_indexed_container(
                        portable_grandchild_prefix, portable_grandchild);
                    indexed_structured_nested_indexed->push_back(item);
                }
                return false;
            }
            if ((normalized_child || normalized_grandchild)
                && existing_xmp_has_explicit_structured_nested_scalar_child(
                    arena, decls, entries, prefix, portable_base,
                    current_entry_index, nested_item_index,
                    portable_child_prefix, portable_child,
                    portable_grandchild_prefix, portable_grandchild)) {
                return false;
            }

            bool new_base_claim = false;
            if (!claim_portable_property_key(
                    claims, prefix, portable_base,
                    PortablePropertyOwner::ExistingXmp,
                    PortablePropertyShape::StructuredIndexed,
                    &new_base_claim)) {
                return false;
            }

            bool new_nested_claim = false;
            if (!claim_portable_indexed_structured_nested_property_key(
                    indexed_structured_child_claims,
                    indexed_structured_nested_child_claims,
                    indexed_structured_nested_claims, prefix, portable_base,
                    nested_item_index, portable_child_prefix, portable_child,
                    portable_grandchild, PortablePropertyOwner::ExistingXmp,
                    &new_nested_claim)
                || !new_nested_claim) {
                return false;
            }

            PortableIndexedStructuredNestedProperty item;
            item.prefix     = prefix;
            item.base       = portable_base;
            item.item_index = nested_item_index;
            item.child      = normalized_child
                                  ? standard_existing_xmp_qualified_component_literal(
                                   portable_child_prefix, portable_child)
                                  : child_name;
            item.grandchild
                = normalized_grandchild
                      ? standard_existing_xmp_qualified_component_literal(
                            portable_grandchild_prefix, portable_grandchild)
                      : grandchild_name;
            item.order = order;
            item.value = &e.value;
            indexed_structured_nested->push_back(item);
            return false;
        }

        std::string_view leaf_name;
        if (parse_indexed_nested_structured_deep_xmp_property_name(
                name, &base_name, &nested_item_index, &child_name,
                &grandchild_name, &leaf_name)) {
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            std::string_view portable_grandchild_prefix;
            std::string_view portable_grandchild;
            std::string_view portable_leaf_prefix;
            std::string_view portable_leaf;
            bool normalized_child      = false;
            bool normalized_grandchild = false;
            if (portable_base.empty()
                || !resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, child_name, &portable_child_prefix,
                    &portable_child, &normalized_child)
                || !resolve_existing_xmp_nested_grandchild_to_portable(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, grandchild_name,
                    &portable_grandchild_prefix, &portable_grandchild,
                    &normalized_grandchild)
                || !resolve_existing_xmp_component_to_portable(
                    prefix, leaf_name, &portable_leaf_prefix, &portable_leaf)
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                    portable_child)
                || xmp_property_is_nonportable_blob(portable_grandchild_prefix,
                                                    portable_grandchild)
                || xmp_property_is_nonportable_blob(portable_leaf_prefix,
                                                    portable_leaf)
                || !portable_scalar_like_value_supported(arena, e.value)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base,
                    PortablePropertyShape::StructuredIndexed)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }
            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, PortableStructuredChildShape::Resource)) {
                return false;
            }
            if (!standard_existing_xmp_nested_child_accepts_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    PortableStructuredChildShape::Resource)) {
                return false;
            }

            bool new_base_claim = false;
            if (!claim_portable_property_key(
                    claims, prefix, portable_base,
                    PortablePropertyOwner::ExistingXmp,
                    PortablePropertyShape::StructuredIndexed,
                    &new_base_claim)) {
                return false;
            }

            bool new_deep_nested_claim = false;
            if (!claim_portable_indexed_structured_deep_nested_property_key(
                    indexed_structured_child_claims,
                    indexed_structured_nested_child_claims,
                    indexed_structured_deep_nested_claims, prefix,
                    portable_base, nested_item_index, portable_child_prefix,
                    portable_child, portable_grandchild_prefix,
                    portable_grandchild, portable_leaf_prefix, portable_leaf,
                    PortablePropertyOwner::ExistingXmp, &new_deep_nested_claim)
                || !new_deep_nested_claim) {
                return false;
            }

            PortableIndexedStructuredDeepNestedProperty item;
            item.prefix            = prefix;
            item.base              = portable_base;
            item.item_index        = nested_item_index;
            item.child             = normalized_child
                                         ? standard_existing_xmp_qualified_component_literal(
                                   portable_child_prefix, portable_child)
                                         : child_name;
            item.grandchild_prefix = portable_grandchild_prefix;
            item.grandchild
                = normalized_grandchild
                      ? standard_existing_xmp_qualified_component_literal(
                            portable_grandchild_prefix, portable_grandchild)
                      : grandchild_name;
            item.leaf_prefix = portable_leaf_prefix;
            item.leaf        = portable_leaf;
            item.order       = order;
            item.value       = &e.value;
            indexed_structured_deep_nested->push_back(item);
            return false;
        }

        uint32_t nested_child_item_index = 0U;
        if (parse_indexed_structured_indexed_nested_xmp_property_name(
                name, &base_name, &nested_item_index, &child_name,
                &nested_child_item_index, &grandchild_name)) {
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            std::string_view portable_grandchild_prefix;
            std::string_view portable_grandchild;
            bool normalized_child      = false;
            bool normalized_grandchild = false;
            if (portable_base.empty()
                || !resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, child_name, &portable_child_prefix,
                    &portable_child, &normalized_child)
                || !resolve_existing_xmp_nested_grandchild_to_portable(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, grandchild_name,
                    &portable_grandchild_prefix, &portable_grandchild,
                    &normalized_grandchild)
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                    portable_child)
                || xmp_property_is_nonportable_blob(portable_grandchild_prefix,
                                                    portable_grandchild)
                || !portable_scalar_like_value_supported(arena, e.value)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base,
                    PortablePropertyShape::StructuredIndexed)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }
            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, PortableStructuredChildShape::Indexed)) {
                return false;
            }
            if ((normalized_child || normalized_grandchild)
                && existing_xmp_has_explicit_indexed_structured_indexed_nested_scalar_child(
                    arena, decls, entries, prefix, portable_base,
                    current_entry_index, nested_item_index,
                    portable_child_prefix, portable_child,
                    nested_child_item_index, portable_grandchild_prefix,
                    portable_grandchild)) {
                return false;
            }

            bool new_base_claim = false;
            if (!claim_portable_property_key(
                    claims, prefix, portable_base,
                    PortablePropertyOwner::ExistingXmp,
                    PortablePropertyShape::StructuredIndexed,
                    &new_base_claim)) {
                return false;
            }

            bool new_nested_claim = false;
            if (!claim_portable_indexed_structured_indexed_nested_property_key(
                    indexed_structured_child_claims,
                    indexed_structured_indexed_nested_child_claims,
                    indexed_structured_indexed_nested_claims, prefix,
                    portable_base, nested_item_index, portable_child_prefix,
                    portable_child, nested_child_item_index,
                    portable_grandchild, PortablePropertyOwner::ExistingXmp,
                    &new_nested_claim)
                || !new_nested_claim) {
                return false;
            }

            PortableIndexedStructuredIndexedNestedProperty item;
            item.prefix       = prefix;
            item.base         = portable_base;
            item.item_index   = nested_item_index;
            item.child_prefix = portable_child_prefix;
            item.child        = portable_child;
            item.child_index  = nested_child_item_index;
            item.grandchild
                = normalized_grandchild
                      ? standard_existing_xmp_qualified_component_literal(
                            portable_grandchild_prefix, portable_grandchild)
                      : grandchild_name;
            item.order = order;
            item.value = &e.value;
            item.child_container
                = portable_existing_xmp_indexed_container(portable_child_prefix,
                                                          portable_child);
            indexed_structured_indexed_nested->push_back(item);
            return false;
        }

        std::string_view grandchild_lang;
        if (parse_nested_structured_lang_alt_xmp_property_name(
                name, &base_name, &child_name, &grandchild_name,
                &grandchild_lang)) {
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            std::string_view portable_grandchild_prefix;
            std::string_view portable_grandchild;
            if (portable_base.empty()
                || !resolve_existing_xmp_component_to_portable(
                    prefix, child_name, &portable_child_prefix, &portable_child)
                || !resolve_existing_xmp_component_to_portable(
                    prefix, grandchild_name, &portable_grandchild_prefix,
                    &portable_grandchild)
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                    portable_child)
                || xmp_property_is_nonportable_blob(portable_grandchild_prefix,
                                                    portable_grandchild)
                || !portable_scalar_like_value_supported(arena, e.value)
                || !xmp_lang_value_is_safe(grandchild_lang)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base, PortablePropertyShape::Structured)) {
                return false;
            }
            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, PortableStructuredChildShape::Resource)) {
                return false;
            }
            PortableStructuredChildShape required_nested_shape
                = PortableStructuredChildShape::Scalar;
            const bool has_required_nested_shape
                = standard_existing_xmp_required_nested_child_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    &required_nested_shape);
            if (!standard_existing_xmp_nested_child_accepts_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    PortableStructuredChildShape::LangAlt)) {
                if (has_required_nested_shape
                    && required_nested_shape
                           == PortableStructuredChildShape::Indexed
                    && grandchild_lang == "x-default"
                    && !existing_xmp_has_explicit_structured_nested_indexed_child(
                        arena, decls, entries, prefix, portable_base, 0U,
                        portable_child, portable_grandchild)) {
                    bool new_base_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::Structured,
                            &new_base_claim)) {
                        return false;
                    }
                    if (!claim_portable_structured_nested_indexed_property_key(
                            structured_child_claims,
                            structured_nested_child_claims, prefix,
                            portable_base, portable_child_prefix,
                            portable_child, portable_grandchild)) {
                        return false;
                    }

                    PortableStructuredNestedIndexedProperty item;
                    item.prefix     = prefix;
                    item.base       = portable_base;
                    item.child      = child_name;
                    item.grandchild = grandchild_name;
                    item.index      = 1U;
                    item.order      = order;
                    item.value      = &e.value;
                    item.container  = portable_existing_xmp_indexed_container(
                        portable_grandchild_prefix, portable_grandchild);
                    structured_nested_indexed->push_back(item);
                }
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && generated_replacement_exists_for_existing_flattened_nested_alias(
                    generated_shapes, generated_lang_alt, prefix, portable_base,
                    portable_child, portable_grandchild)) {
                return false;
            }

            bool new_base_claim = false;
            if (!claim_portable_property_key(claims, prefix, portable_base,
                                             PortablePropertyOwner::ExistingXmp,
                                             PortablePropertyShape::Structured,
                                             &new_base_claim)) {
                return false;
            }

            bool new_nested_claim = false;
            if (!claim_portable_structured_nested_lang_alt_property_key(
                    structured_child_claims, structured_nested_child_claims,
                    structured_nested_lang_alt_claims, prefix, portable_base,
                    portable_child_prefix, portable_child, portable_grandchild,
                    grandchild_lang, PortablePropertyOwner::ExistingXmp,
                    &new_nested_claim)
                || !new_nested_claim) {
                return false;
            }

            PortableStructuredNestedLangAltProperty item;
            item.prefix     = prefix;
            item.base       = portable_base;
            item.child      = child_name;
            item.grandchild = grandchild_name;
            item.lang       = grandchild_lang;
            item.order      = order;
            item.value      = &e.value;
            structured_nested_lang_alt->push_back(item);
            return false;
        }

        uint32_t nested_grandchild_index = 0U;
        if (parse_nested_structured_indexed_xmp_property_name(
                name, &base_name, &child_name, &grandchild_name,
                &nested_grandchild_index)) {
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            std::string_view portable_grandchild_prefix;
            std::string_view portable_grandchild;
            if (portable_base.empty()
                || !resolve_existing_xmp_component_to_portable(
                    prefix, child_name, &portable_child_prefix, &portable_child)
                || !resolve_existing_xmp_component_to_portable(
                    prefix, grandchild_name, &portable_grandchild_prefix,
                    &portable_grandchild)
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                    portable_child)
                || xmp_property_is_nonportable_blob(portable_grandchild_prefix,
                                                    portable_grandchild)
                || !portable_scalar_like_value_supported(arena, e.value)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base, PortablePropertyShape::Structured)) {
                return false;
            }
            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, PortableStructuredChildShape::Resource)) {
                return false;
            }
            PortableStructuredChildShape required_nested_shape
                = PortableStructuredChildShape::Scalar;
            const bool has_required_nested_shape
                = standard_existing_xmp_required_nested_child_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    &required_nested_shape);
            if (!standard_existing_xmp_nested_child_accepts_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    PortableStructuredChildShape::Indexed)) {
                if (has_required_nested_shape
                    && required_nested_shape
                           == PortableStructuredChildShape::LangAlt
                    && nested_grandchild_index == 1U
                    && !existing_xmp_has_explicit_structured_nested_lang_alt_child(
                        arena, decls, entries, prefix, portable_base, 0U,
                        portable_child, portable_grandchild)) {
                    bool new_base_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::Structured,
                            &new_base_claim)) {
                        return false;
                    }

                    bool new_nested_claim = false;
                    if (!claim_portable_structured_nested_lang_alt_property_key(
                            structured_child_claims,
                            structured_nested_child_claims,
                            structured_nested_lang_alt_claims, prefix,
                            portable_base, portable_child_prefix,
                            portable_child, portable_grandchild, "x-default",
                            PortablePropertyOwner::ExistingXmp,
                            &new_nested_claim)
                        || !new_nested_claim) {
                        return false;
                    }

                    PortableStructuredNestedLangAltProperty item;
                    item.prefix     = prefix;
                    item.base       = portable_base;
                    item.child      = child_name;
                    item.grandchild = grandchild_name;
                    item.lang       = "x-default";
                    item.order      = order;
                    item.value      = &e.value;
                    structured_nested_lang_alt->push_back(item);
                }
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && generated_replacement_exists_for_existing_flattened_nested_alias(
                    generated_shapes, generated_lang_alt, prefix, portable_base,
                    portable_child, portable_grandchild)) {
                return false;
            }

            bool new_base_claim = false;
            if (!claim_portable_property_key(claims, prefix, portable_base,
                                             PortablePropertyOwner::ExistingXmp,
                                             PortablePropertyShape::Structured,
                                             &new_base_claim)) {
                return false;
            }

            if (!claim_portable_structured_nested_indexed_property_key(
                    structured_child_claims, structured_nested_child_claims,
                    prefix, portable_base, portable_child_prefix,
                    portable_child, portable_grandchild)) {
                return false;
            }

            PortableStructuredNestedIndexedProperty item;
            item.prefix     = prefix;
            item.base       = portable_base;
            item.child      = child_name;
            item.grandchild = grandchild_name;
            item.index      = nested_grandchild_index;
            item.order      = order;
            item.value      = &e.value;
            item.container  = portable_existing_xmp_indexed_container(
                portable_grandchild_prefix, portable_grandchild);
            structured_nested_indexed->push_back(item);
            return false;
        }

        uint32_t nested_lang_item_index = 0U;
        if (parse_indexed_nested_structured_lang_alt_xmp_property_name(
                name, &base_name, &nested_lang_item_index, &child_name,
                &grandchild_name, &grandchild_lang)) {
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            std::string_view portable_grandchild_prefix;
            std::string_view portable_grandchild;
            if (portable_base.empty()
                || !resolve_existing_xmp_component_to_portable(
                    prefix, child_name, &portable_child_prefix, &portable_child)
                || !resolve_existing_xmp_component_to_portable(
                    prefix, grandchild_name, &portable_grandchild_prefix,
                    &portable_grandchild)
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                    portable_child)
                || xmp_property_is_nonportable_blob(portable_grandchild_prefix,
                                                    portable_grandchild)
                || !portable_scalar_like_value_supported(arena, e.value)
                || !xmp_lang_value_is_safe(grandchild_lang)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base,
                    PortablePropertyShape::StructuredIndexed)) {
                return false;
            }
            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, PortableStructuredChildShape::Resource)) {
                return false;
            }
            PortableStructuredChildShape required_nested_shape
                = PortableStructuredChildShape::Scalar;
            const bool has_required_nested_shape
                = standard_existing_xmp_required_nested_child_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    &required_nested_shape);
            if (!standard_existing_xmp_nested_child_accepts_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    PortableStructuredChildShape::LangAlt)) {
                if (has_required_nested_shape
                    && required_nested_shape
                           == PortableStructuredChildShape::Indexed
                    && grandchild_lang == "x-default"
                    && !existing_xmp_has_explicit_structured_nested_indexed_child(
                        arena, decls, entries, prefix, portable_base,
                        nested_lang_item_index, portable_child,
                        portable_grandchild)) {
                    bool new_base_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::StructuredIndexed,
                            &new_base_claim)) {
                        return false;
                    }
                    if (!claim_portable_indexed_structured_nested_indexed_property_key(
                            indexed_structured_child_claims,
                            indexed_structured_nested_child_claims, prefix,
                            portable_base, nested_lang_item_index,
                            portable_child_prefix, portable_child,
                            portable_grandchild)) {
                        return false;
                    }

                    PortableIndexedStructuredNestedIndexedProperty item;
                    item.prefix     = prefix;
                    item.base       = portable_base;
                    item.item_index = nested_lang_item_index;
                    item.child      = child_name;
                    item.grandchild = grandchild_name;
                    item.index      = 1U;
                    item.order      = order;
                    item.value      = &e.value;
                    item.container  = portable_existing_xmp_indexed_container(
                        portable_grandchild_prefix, portable_grandchild);
                    indexed_structured_nested_indexed->push_back(item);
                }
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }

            bool new_base_claim = false;
            if (!claim_portable_property_key(
                    claims, prefix, portable_base,
                    PortablePropertyOwner::ExistingXmp,
                    PortablePropertyShape::StructuredIndexed,
                    &new_base_claim)) {
                return false;
            }

            bool new_nested_claim = false;
            if (!claim_portable_indexed_structured_nested_lang_alt_property_key(
                    indexed_structured_child_claims,
                    indexed_structured_nested_child_claims,
                    indexed_structured_nested_lang_alt_claims, prefix,
                    portable_base, nested_lang_item_index,
                    portable_child_prefix, portable_child, portable_grandchild,
                    grandchild_lang, PortablePropertyOwner::ExistingXmp,
                    &new_nested_claim)
                || !new_nested_claim) {
                return false;
            }

            PortableIndexedStructuredNestedLangAltProperty item;
            item.prefix     = prefix;
            item.base       = portable_base;
            item.item_index = nested_lang_item_index;
            item.child      = child_name;
            item.grandchild = grandchild_name;
            item.lang       = grandchild_lang;
            item.order      = order;
            item.value      = &e.value;
            indexed_structured_nested_lang_alt->push_back(item);
            return false;
        }

        uint32_t nested_index_item_index = 0U;
        if (parse_indexed_nested_structured_indexed_xmp_property_name(
                name, &base_name, &nested_index_item_index, &child_name,
                &grandchild_name, &nested_grandchild_index)) {
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            std::string_view portable_grandchild_prefix;
            std::string_view portable_grandchild;
            if (portable_base.empty()
                || !resolve_existing_xmp_component_to_portable(
                    prefix, child_name, &portable_child_prefix, &portable_child)
                || !resolve_existing_xmp_component_to_portable(
                    prefix, grandchild_name, &portable_grandchild_prefix,
                    &portable_grandchild)
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                    portable_child)
                || xmp_property_is_nonportable_blob(portable_grandchild_prefix,
                                                    portable_grandchild)
                || !portable_scalar_like_value_supported(arena, e.value)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base,
                    PortablePropertyShape::StructuredIndexed)) {
                return false;
            }
            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, PortableStructuredChildShape::Resource)) {
                return false;
            }
            PortableStructuredChildShape required_nested_shape
                = PortableStructuredChildShape::Scalar;
            const bool has_required_nested_shape
                = standard_existing_xmp_required_nested_child_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    &required_nested_shape);
            if (!standard_existing_xmp_nested_child_accepts_shape(
                    prefix, portable_base, portable_child, portable_grandchild,
                    PortableStructuredChildShape::Indexed)) {
                if (has_required_nested_shape
                    && required_nested_shape
                           == PortableStructuredChildShape::LangAlt
                    && nested_grandchild_index == 1U
                    && !existing_xmp_has_explicit_structured_nested_lang_alt_child(
                        arena, decls, entries, prefix, portable_base,
                        nested_index_item_index, portable_child,
                        portable_grandchild)) {
                    bool new_base_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::StructuredIndexed,
                            &new_base_claim)) {
                        return false;
                    }

                    bool new_nested_claim = false;
                    if (!claim_portable_indexed_structured_nested_lang_alt_property_key(
                            indexed_structured_child_claims,
                            indexed_structured_nested_child_claims,
                            indexed_structured_nested_lang_alt_claims, prefix,
                            portable_base, nested_index_item_index,
                            portable_child_prefix, portable_child,
                            portable_grandchild, "x-default",
                            PortablePropertyOwner::ExistingXmp,
                            &new_nested_claim)
                        || !new_nested_claim) {
                        return false;
                    }

                    PortableIndexedStructuredNestedLangAltProperty item;
                    item.prefix     = prefix;
                    item.base       = portable_base;
                    item.item_index = nested_index_item_index;
                    item.child      = child_name;
                    item.grandchild = grandchild_name;
                    item.lang       = "x-default";
                    item.order      = order;
                    item.value      = &e.value;
                    indexed_structured_nested_lang_alt->push_back(item);
                }
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }

            bool new_base_claim = false;
            if (!claim_portable_property_key(
                    claims, prefix, portable_base,
                    PortablePropertyOwner::ExistingXmp,
                    PortablePropertyShape::StructuredIndexed,
                    &new_base_claim)) {
                return false;
            }

            if (!claim_portable_indexed_structured_nested_indexed_property_key(
                    indexed_structured_child_claims,
                    indexed_structured_nested_child_claims, prefix,
                    portable_base, nested_index_item_index,
                    portable_child_prefix, portable_child,
                    portable_grandchild)) {
                return false;
            }

            PortableIndexedStructuredNestedIndexedProperty item;
            item.prefix     = prefix;
            item.base       = portable_base;
            item.item_index = nested_index_item_index;
            item.child      = child_name;
            item.grandchild = grandchild_name;
            item.index      = nested_grandchild_index;
            item.order      = order;
            item.value      = &e.value;
            item.container  = portable_existing_xmp_indexed_container(
                portable_grandchild_prefix, portable_grandchild);
            indexed_structured_nested_indexed->push_back(item);
            return false;
        }

        std::string_view child_lang;
        if (parse_structured_lang_alt_xmp_property_name(name, &base_name,
                                                        &child_name,
                                                        &child_lang)) {
            std::string_view raw_child_prefix;
            std::string_view raw_child_name;
            if (!split_qualified_xmp_property_name(child_name,
                                                   &raw_child_prefix,
                                                   &raw_child_name)) {
                return false;
            }
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            const std::string_view portable_child_prefix
                = raw_child_prefix.empty() ? prefix : raw_child_prefix;
            const std::string_view portable_child
                = portable_property_name_for_existing_xmp(portable_child_prefix,
                                                          raw_child_name);
            if (portable_base.empty() || portable_child.empty()
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                    portable_child)
                || !portable_scalar_like_value_supported(arena, e.value)
                || !xmp_lang_value_is_safe(child_lang)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base, PortablePropertyShape::Structured)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && generated_replacement_exists_for_existing_structured_child_alias(
                    generated_shapes, generated_lang_alt, prefix, portable_base,
                    portable_child_prefix, portable_child)) {
                return false;
            }
            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, PortableStructuredChildShape::LangAlt)) {
                PortableStructuredChildShape required_child_shape
                    = PortableStructuredChildShape::Scalar;
                const bool has_required_child_shape
                    = standard_existing_xmp_required_structured_child_shape(
                        prefix, portable_base, portable_child_prefix,
                        portable_child, &required_child_shape);
                if (has_required_child_shape
                    && required_child_shape
                           == PortableStructuredChildShape::Scalar
                    && child_lang == "x-default"
                    && !existing_xmp_has_explicit_structured_scalar_child(
                        arena, decls, entries, prefix, portable_base,
                        current_entry_index, 0U, portable_child_prefix,
                        portable_child)) {
                    bool new_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::Structured, &new_claim)) {
                        return false;
                    }
                    if (!claim_portable_structured_child_key(
                            structured_child_claims, prefix, portable_base,
                            portable_child_prefix, portable_child,
                            PortableStructuredChildShape::Scalar)) {
                        return false;
                    }

                    PortableStructuredProperty item;
                    item.prefix       = prefix;
                    item.base         = portable_base;
                    item.child_prefix = portable_child_prefix;
                    item.child        = portable_child;
                    item.order        = order;
                    item.value        = &e.value;
                    structured->push_back(item);
                } else if (has_required_child_shape
                           && required_child_shape
                                  == PortableStructuredChildShape::Indexed
                           && child_lang == "x-default"
                           && !existing_xmp_has_explicit_structured_indexed_child(
                               arena, decls, entries, prefix, portable_base,
                               entries.size(), 0U, portable_child_prefix,
                               portable_child)) {
                    bool new_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::Structured, &new_claim)) {
                        return false;
                    }
                    if (!claim_portable_structured_child_key(
                            structured_child_claims, prefix, portable_base,
                            portable_child_prefix, portable_child,
                            PortableStructuredChildShape::Indexed)) {
                        return false;
                    }

                    PortableStructuredIndexedProperty item;
                    item.prefix       = prefix;
                    item.base         = portable_base;
                    item.child_prefix = portable_child_prefix;
                    item.child        = portable_child;
                    item.index        = 1U;
                    item.order        = order;
                    item.value        = &e.value;
                    item.container    = portable_existing_xmp_indexed_container(
                        portable_child_prefix, portable_child);
                    structured_indexed->push_back(item);
                }
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && generated_replacement_exists_for_existing_structured_child_alias(
                    generated_shapes, generated_lang_alt, prefix, portable_base,
                    portable_child_prefix, portable_child)) {
                return false;
            }

            bool new_base_claim = false;
            if (!claim_portable_property_key(claims, prefix, portable_base,
                                             PortablePropertyOwner::ExistingXmp,
                                             PortablePropertyShape::Structured,
                                             &new_base_claim)) {
                return false;
            }

            bool new_lang_claim = false;
            if (!claim_portable_structured_lang_alt_property_key(
                    structured_child_claims, structured_lang_alt_claims, prefix,
                    portable_base, portable_child_prefix, portable_child,
                    child_lang, PortablePropertyOwner::ExistingXmp,
                    &new_lang_claim)
                || !new_lang_claim) {
                return false;
            }

            PortableStructuredLangAltProperty item;
            item.prefix       = prefix;
            item.base         = portable_base;
            item.child_prefix = portable_child_prefix;
            item.child        = portable_child;
            item.lang         = child_lang;
            item.order        = order;
            item.value        = &e.value;
            structured_lang_alt->push_back(item);
            return false;
        }

        uint32_t item_lang_index = 0U;
        if (parse_indexed_structured_lang_alt_xmp_property_name(
                name, &base_name, &item_lang_index, &child_name, &child_lang)) {
            std::string_view raw_child_prefix;
            std::string_view raw_child_name;
            if (!split_qualified_xmp_property_name(child_name,
                                                   &raw_child_prefix,
                                                   &raw_child_name)) {
                return false;
            }
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            const std::string_view portable_child_prefix
                = raw_child_prefix.empty() ? prefix : raw_child_prefix;
            const std::string_view portable_child
                = portable_property_name_for_existing_xmp(portable_child_prefix,
                                                          raw_child_name);
            if (portable_base.empty() || portable_child.empty()
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                    portable_child)
                || !portable_scalar_like_value_supported(arena, e.value)
                || !xmp_lang_value_is_safe(child_lang)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base,
                    PortablePropertyShape::StructuredIndexed)) {
                return false;
            }
            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, PortableStructuredChildShape::LangAlt)) {
                PortableStructuredChildShape required_child_shape
                    = PortableStructuredChildShape::Scalar;
                const bool has_required_child_shape
                    = standard_existing_xmp_required_structured_child_shape(
                        prefix, portable_base, portable_child_prefix,
                        portable_child, &required_child_shape);
                if (has_required_child_shape
                    && required_child_shape
                           == PortableStructuredChildShape::Scalar
                    && child_lang == "x-default"
                    && !existing_xmp_has_explicit_structured_scalar_child(
                        arena, decls, entries, prefix, portable_base,
                        current_entry_index, item_lang_index,
                        portable_child_prefix, portable_child)) {
                    bool new_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::StructuredIndexed,
                            &new_claim)) {
                        return false;
                    }
                    if (!claim_portable_indexed_structured_child_key(
                            indexed_structured_child_claims, prefix,
                            portable_base, item_lang_index,
                            portable_child_prefix, portable_child,
                            PortableStructuredChildShape::Scalar)) {
                        return false;
                    }

                    PortableIndexedStructuredProperty item;
                    item.prefix       = prefix;
                    item.base         = portable_base;
                    item.item_index   = item_lang_index;
                    item.child_prefix = portable_child_prefix;
                    item.child        = portable_child;
                    item.order        = order;
                    item.value        = &e.value;
                    item.container    = portable_existing_xmp_indexed_container(
                        prefix, portable_base);
                    indexed_structured->push_back(item);
                } else if (
                    has_required_child_shape
                    && required_child_shape
                           == PortableStructuredChildShape::Indexed
                    && child_lang == "x-default"
                    && !existing_xmp_has_explicit_structured_indexed_child(
                        arena, decls, entries, prefix, portable_base,
                        entries.size(), item_lang_index, portable_child_prefix,
                        portable_child)
                    && !existing_xmp_has_explicit_indexed_structured_indexed_nested_child(
                        arena, decls, entries, prefix, portable_base,
                        item_lang_index, portable_child_prefix,
                        portable_child)) {
                    bool new_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::StructuredIndexed,
                            &new_claim)) {
                        return false;
                    }
                    if (!claim_portable_indexed_structured_child_key(
                            indexed_structured_child_claims, prefix,
                            portable_base, item_lang_index,
                            portable_child_prefix, portable_child,
                            PortableStructuredChildShape::Indexed)) {
                        return false;
                    }

                    PortableIndexedStructuredIndexedProperty item;
                    item.prefix       = prefix;
                    item.base         = portable_base;
                    item.item_index   = item_lang_index;
                    item.child_prefix = portable_child_prefix;
                    item.child        = portable_child;
                    item.index        = 1U;
                    item.order        = order;
                    item.value        = &e.value;
                    item.container    = portable_existing_xmp_indexed_container(
                        portable_child_prefix, portable_child);
                    indexed_structured_indexed->push_back(item);
                }
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }

            bool new_base_claim = false;
            if (!claim_portable_property_key(
                    claims, prefix, portable_base,
                    PortablePropertyOwner::ExistingXmp,
                    PortablePropertyShape::StructuredIndexed,
                    &new_base_claim)) {
                return false;
            }

            bool new_lang_claim = false;
            if (!claim_portable_indexed_structured_lang_alt_property_key(
                    indexed_structured_child_claims,
                    indexed_structured_lang_alt_claims, prefix, portable_base,
                    item_lang_index, portable_child_prefix, portable_child,
                    child_lang, PortablePropertyOwner::ExistingXmp,
                    &new_lang_claim)
                || !new_lang_claim) {
                return false;
            }

            PortableIndexedStructuredLangAltProperty item;
            item.prefix       = prefix;
            item.base         = portable_base;
            item.item_index   = item_lang_index;
            item.child_prefix = portable_child_prefix;
            item.child        = portable_child;
            item.lang         = child_lang;
            item.order        = order;
            item.value        = &e.value;
            indexed_structured_lang_alt->push_back(item);
            return false;
        }

        uint32_t child_index = 0U;
        if (parse_structured_indexed_xmp_property_name(name, &base_name,
                                                       &child_name,
                                                       &child_index)) {
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            bool normalized_child = false;
            if (!resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, child_name, &portable_child_prefix,
                    &portable_child, &normalized_child)) {
                return false;
            }
            if (portable_base.empty() || portable_child.empty()
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                    portable_child)
                || !portable_scalar_like_value_supported(arena, e.value)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base, PortablePropertyShape::Structured)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && generated_replacement_exists_for_existing_structured_child_alias(
                    generated_shapes, generated_lang_alt, prefix, portable_base,
                    portable_child_prefix, portable_child)) {
                return false;
            }
            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, PortableStructuredChildShape::Indexed)) {
                PortableStructuredChildShape required_child_shape
                    = PortableStructuredChildShape::Scalar;
                const bool has_required_child_shape
                    = standard_existing_xmp_required_structured_child_shape(
                        prefix, portable_base, portable_child_prefix,
                        portable_child, &required_child_shape);
                if (has_required_child_shape
                    && required_child_shape
                           == PortableStructuredChildShape::Scalar
                    && child_index == 1U
                    && !existing_xmp_has_explicit_structured_scalar_child(
                        arena, decls, entries, prefix, portable_base,
                        current_entry_index, 0U, portable_child_prefix,
                        portable_child)) {
                    bool new_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::Structured, &new_claim)) {
                        return false;
                    }
                    if (!claim_portable_structured_child_key(
                            structured_child_claims, prefix, portable_base,
                            portable_child_prefix, portable_child,
                            PortableStructuredChildShape::Scalar)) {
                        return false;
                    }

                    PortableStructuredProperty item;
                    item.prefix       = prefix;
                    item.base         = portable_base;
                    item.child_prefix = portable_child_prefix;
                    item.child        = portable_child;
                    item.order        = order;
                    item.value        = &e.value;
                    structured->push_back(item);
                } else if (has_required_child_shape
                           && required_child_shape
                                  == PortableStructuredChildShape::LangAlt
                           && child_index == 1U
                           && !existing_xmp_has_explicit_structured_lang_alt_child(
                               arena, decls, entries, prefix, portable_base, 0U,
                               portable_child_prefix, portable_child)) {
                    bool new_base_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::Structured,
                            &new_base_claim)) {
                        return false;
                    }

                    bool new_lang_claim = false;
                    if (!claim_portable_structured_lang_alt_property_key(
                            structured_child_claims, structured_lang_alt_claims,
                            prefix, portable_base, portable_child_prefix,
                            portable_child, "x-default",
                            PortablePropertyOwner::ExistingXmp, &new_lang_claim)
                        || !new_lang_claim) {
                        return false;
                    }

                    PortableStructuredLangAltProperty item;
                    item.prefix       = prefix;
                    item.base         = portable_base;
                    item.child_prefix = portable_child_prefix;
                    item.child        = portable_child;
                    item.lang         = "x-default";
                    item.order        = order;
                    item.value        = &e.value;
                    structured_lang_alt->push_back(item);
                }
                return false;
            }
            if (normalized_child
                && existing_xmp_has_explicit_structured_indexed_child_item(
                    arena, decls, entries, prefix, portable_base,
                    current_entry_index, 0U, portable_child_prefix,
                    portable_child, child_index)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && generated_replacement_exists_for_existing_structured_child_alias(
                    generated_shapes, generated_lang_alt, prefix, portable_base,
                    portable_child_prefix, portable_child)) {
                return false;
            }

            bool new_base_claim = false;
            if (!claim_portable_property_key(claims, prefix, portable_base,
                                             PortablePropertyOwner::ExistingXmp,
                                             PortablePropertyShape::Structured,
                                             &new_base_claim)) {
                return false;
            }
            if (!claim_portable_structured_child_key(
                    structured_child_claims, prefix, portable_base,
                    portable_child_prefix, portable_child,
                    PortableStructuredChildShape::Indexed)) {
                return false;
            }

            PortableStructuredIndexedProperty item;
            item.prefix       = prefix;
            item.base         = portable_base;
            item.child_prefix = portable_child_prefix;
            item.child        = portable_child;
            item.index        = child_index;
            item.order        = order;
            item.value        = &e.value;
            item.container
                = portable_existing_xmp_indexed_container(portable_child_prefix,
                                                          portable_child);
            structured_indexed->push_back(item);
            return false;
        }

        uint32_t item_index       = 0U;
        uint32_t child_item_index = 0U;
        if (parse_indexed_structured_indexed_xmp_property_name(
                name, &base_name, &item_index, &child_name, &child_item_index)) {
            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            bool normalized_child = false;
            if (!resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, child_name, &portable_child_prefix,
                    &portable_child, &normalized_child)) {
                return false;
            }
            if (portable_base.empty() || portable_child.empty()
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                    portable_child)
                || !portable_scalar_like_value_supported(arena, e.value)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base,
                    PortablePropertyShape::StructuredIndexed)) {
                return false;
            }
            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, PortableStructuredChildShape::Indexed)) {
                PortableStructuredChildShape required_child_shape
                    = PortableStructuredChildShape::Scalar;
                const bool has_required_child_shape
                    = standard_existing_xmp_required_structured_child_shape(
                        prefix, portable_base, portable_child_prefix,
                        portable_child, &required_child_shape);
                if (has_required_child_shape
                    && required_child_shape
                           == PortableStructuredChildShape::Scalar
                    && child_item_index == 1U
                    && !existing_xmp_has_explicit_structured_scalar_child(
                        arena, decls, entries, prefix, portable_base,
                        current_entry_index, item_index, portable_child_prefix,
                        portable_child)) {
                    bool new_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::StructuredIndexed,
                            &new_claim)) {
                        return false;
                    }
                    if (!claim_portable_indexed_structured_child_key(
                            indexed_structured_child_claims, prefix,
                            portable_base, item_index, portable_child_prefix,
                            portable_child,
                            PortableStructuredChildShape::Scalar)) {
                        return false;
                    }

                    PortableIndexedStructuredProperty item;
                    item.prefix       = prefix;
                    item.base         = portable_base;
                    item.item_index   = item_index;
                    item.child_prefix = portable_child_prefix;
                    item.child        = portable_child;
                    item.order        = order;
                    item.value        = &e.value;
                    item.container    = portable_existing_xmp_indexed_container(
                        prefix, portable_base);
                    indexed_structured->push_back(item);
                } else if (has_required_child_shape
                           && required_child_shape
                                  == PortableStructuredChildShape::LangAlt
                           && child_item_index == 1U
                           && !existing_xmp_has_explicit_structured_lang_alt_child(
                               arena, decls, entries, prefix, portable_base,
                               item_index, portable_child_prefix,
                               portable_child)) {
                    bool new_base_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::StructuredIndexed,
                            &new_base_claim)) {
                        return false;
                    }

                    bool new_lang_claim = false;
                    if (!claim_portable_indexed_structured_lang_alt_property_key(
                            indexed_structured_child_claims,
                            indexed_structured_lang_alt_claims, prefix,
                            portable_base, item_index, portable_child_prefix,
                            portable_child, "x-default",
                            PortablePropertyOwner::ExistingXmp, &new_lang_claim)
                        || !new_lang_claim) {
                        return false;
                    }

                    PortableIndexedStructuredLangAltProperty item;
                    item.prefix       = prefix;
                    item.base         = portable_base;
                    item.item_index   = item_index;
                    item.child_prefix = portable_child_prefix;
                    item.child        = portable_child;
                    item.lang         = "x-default";
                    item.order        = order;
                    item.value        = &e.value;
                    indexed_structured_lang_alt->push_back(item);
                }
                return false;
            }
            if (normalized_child
                && existing_xmp_has_explicit_structured_indexed_child_item(
                    arena, decls, entries, prefix, portable_base,
                    current_entry_index, item_index, portable_child_prefix,
                    portable_child, child_item_index)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }

            bool new_base_claim = false;
            if (!claim_portable_property_key(
                    claims, prefix, portable_base,
                    PortablePropertyOwner::ExistingXmp,
                    PortablePropertyShape::StructuredIndexed,
                    &new_base_claim)) {
                return false;
            }
            if (!claim_portable_indexed_structured_child_key(
                    indexed_structured_child_claims, prefix, portable_base,
                    item_index, portable_child_prefix, portable_child,
                    PortableStructuredChildShape::Indexed)) {
                return false;
            }

            PortableIndexedStructuredIndexedProperty item;
            item.prefix       = prefix;
            item.base         = portable_base;
            item.item_index   = item_index;
            item.child_prefix = portable_child_prefix;
            item.child        = portable_child;
            item.index        = child_item_index;
            item.order        = order;
            item.value        = &e.value;
            item.container
                = portable_existing_xmp_indexed_container(portable_child_prefix,
                                                          portable_child);
            indexed_structured_indexed->push_back(item);
            return false;
        }

        if (!parse_structured_xmp_property_name(name, &base_name, &child_name)) {
            uint32_t parsed_item_index = 0U;
            if (!parse_indexed_structured_xmp_property_name(name, &base_name,
                                                            &parsed_item_index,
                                                            &child_name)) {
                return false;
            }

            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            std::string_view portable_child_prefix;
            std::string_view portable_child;
            bool normalized_child = false;
            if (!resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, child_name, &portable_child_prefix,
                    &portable_child, &normalized_child)) {
                return false;
            }
            if (portable_base.empty() || portable_child.empty()
                || xmp_property_is_nonportable_blob(prefix, portable_base)
                || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                    portable_child)
                || !portable_scalar_like_value_supported(arena, e.value)) {
                return false;
            }
            if (!standard_existing_xmp_base_accepts_shape(
                    prefix, portable_base,
                    PortablePropertyShape::StructuredIndexed)) {
                return false;
            }
            if (options.existing_standard_namespace_policy
                    == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
                && existing_standard_portable_property_is_managed(prefix,
                                                                  portable_base)
                && generated_replacement_exists_for_existing_base_property(
                    generated_shapes, generated_lang_alt, prefix,
                    portable_base)) {
                return false;
            }

            PortableStructuredChildShape required_child_shape
                = PortableStructuredChildShape::Scalar;
            const bool has_required_child_shape
                = standard_existing_xmp_required_structured_child_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, &required_child_shape);
            if (!standard_existing_xmp_structured_child_accepts_shape(
                    prefix, portable_base, portable_child_prefix,
                    portable_child, PortableStructuredChildShape::Scalar)) {
                if (has_required_child_shape
                    && required_child_shape
                           == PortableStructuredChildShape::LangAlt
                    && !existing_xmp_has_explicit_structured_lang_alt_child(
                        arena, decls, entries, prefix, portable_base,
                        parsed_item_index, portable_child_prefix,
                        portable_child)) {
                    bool new_base_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::StructuredIndexed,
                            &new_base_claim)) {
                        return false;
                    }

                    bool new_lang_claim = false;
                    if (!claim_portable_indexed_structured_lang_alt_property_key(
                            indexed_structured_child_claims,
                            indexed_structured_lang_alt_claims, prefix,
                            portable_base, parsed_item_index,
                            portable_child_prefix, portable_child, "x-default",
                            PortablePropertyOwner::ExistingXmp, &new_lang_claim)
                        || !new_lang_claim) {
                        return false;
                    }

                    PortableIndexedStructuredLangAltProperty item;
                    item.prefix       = prefix;
                    item.base         = portable_base;
                    item.item_index   = parsed_item_index;
                    item.child_prefix = portable_child_prefix;
                    item.child        = portable_child;
                    item.lang         = "x-default";
                    item.order        = order;
                    item.value        = &e.value;
                    indexed_structured_lang_alt->push_back(item);
                } else if (
                    has_required_child_shape
                    && required_child_shape
                           == PortableStructuredChildShape::Indexed
                    && !existing_xmp_has_explicit_structured_indexed_child(
                        arena, decls, entries, prefix, portable_base,
                        entries.size(), parsed_item_index,
                        portable_child_prefix, portable_child)
                    && !existing_xmp_has_explicit_indexed_structured_indexed_nested_child(
                        arena, decls, entries, prefix, portable_base,
                        parsed_item_index, portable_child_prefix,
                        portable_child)) {
                    bool new_claim = false;
                    if (!claim_portable_property_key(
                            claims, prefix, portable_base,
                            PortablePropertyOwner::ExistingXmp,
                            PortablePropertyShape::StructuredIndexed,
                            &new_claim)) {
                        return false;
                    }
                    if (!claim_portable_indexed_structured_child_key(
                            indexed_structured_child_claims, prefix,
                            portable_base, parsed_item_index,
                            portable_child_prefix, portable_child,
                            PortableStructuredChildShape::Indexed)) {
                        return false;
                    }

                    PortableIndexedStructuredIndexedProperty item;
                    item.prefix       = prefix;
                    item.base         = portable_base;
                    item.item_index   = parsed_item_index;
                    item.child_prefix = portable_child_prefix;
                    item.child        = portable_child;
                    item.index        = 1U;
                    item.order        = order;
                    item.value        = &e.value;
                    item.container    = portable_existing_xmp_indexed_container(
                        portable_child_prefix, portable_child);
                    indexed_structured_indexed->push_back(item);
                }
                return false;
            }
            if (normalized_child
                && existing_xmp_has_explicit_structured_scalar_child(
                    arena, decls, entries, prefix, portable_base,
                    current_entry_index, parsed_item_index,
                    portable_child_prefix, portable_child)) {
                return false;
            }

            bool new_claim = false;
            if (!claim_portable_property_key(
                    claims, prefix, portable_base,
                    PortablePropertyOwner::ExistingXmp,
                    PortablePropertyShape::StructuredIndexed, &new_claim)) {
                return false;
            }
            if (!claim_portable_indexed_structured_child_key(
                    indexed_structured_child_claims, prefix, portable_base,
                    parsed_item_index, portable_child_prefix, portable_child,
                    PortableStructuredChildShape::Scalar)) {
                return false;
            }

            PortableIndexedStructuredProperty item;
            item.prefix       = prefix;
            item.base         = portable_base;
            item.item_index   = parsed_item_index;
            item.child_prefix = portable_child_prefix;
            item.child        = portable_child;
            item.order        = order;
            item.value        = &e.value;
            item.container
                = portable_existing_xmp_indexed_container(prefix,
                                                          portable_base);
            indexed_structured->push_back(item);
            return false;
        }

        const std::string_view portable_base
            = portable_property_name_for_existing_xmp(prefix, base_name);
        std::string_view portable_child_prefix;
        std::string_view portable_child;
        bool normalized_child = false;
        if (!resolve_existing_xmp_structured_child_to_portable(
                prefix, portable_base, child_name, &portable_child_prefix,
                &portable_child, &normalized_child)) {
            return false;
        }
        if (portable_base.empty() || portable_child.empty()
            || xmp_property_is_nonportable_blob(prefix, portable_base)
            || xmp_property_is_nonportable_blob(portable_child_prefix,
                                                portable_child)
            || !portable_scalar_like_value_supported(arena, e.value)) {
            return false;
        }
        if (!standard_existing_xmp_base_accepts_shape(
                prefix, portable_base, PortablePropertyShape::Structured)) {
            return false;
        }
        if (options.existing_standard_namespace_policy
                == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
            && existing_standard_portable_property_is_managed(prefix,
                                                              portable_base)
            && generated_replacement_exists_for_existing_base_property(
                generated_shapes, generated_lang_alt, prefix, portable_base)) {
            return false;
        }
        if (options.existing_standard_namespace_policy
                == XmpExistingStandardNamespacePolicy::CanonicalizeManaged
            && generated_replacement_exists_for_existing_structured_child_alias(
                generated_shapes, generated_lang_alt, prefix, portable_base,
                portable_child_prefix, portable_child)) {
            return false;
        }

        PortableStructuredChildShape required_child_shape
            = PortableStructuredChildShape::Scalar;
        const bool has_required_child_shape
            = standard_existing_xmp_required_structured_child_shape(
                prefix, portable_base, portable_child_prefix, portable_child,
                &required_child_shape);
        if (!standard_existing_xmp_structured_child_accepts_shape(
                prefix, portable_base, portable_child_prefix, portable_child,
                PortableStructuredChildShape::Scalar)) {
            if (has_required_child_shape
                && required_child_shape == PortableStructuredChildShape::LangAlt
                && !existing_xmp_has_explicit_structured_lang_alt_child(
                    arena, decls, entries, prefix, portable_base, 0U,
                    portable_child_prefix, portable_child)) {
                bool new_base_claim = false;
                if (!claim_portable_property_key(
                        claims, prefix, portable_base,
                        PortablePropertyOwner::ExistingXmp,
                        PortablePropertyShape::Structured, &new_base_claim)) {
                    return false;
                }

                bool new_lang_claim = false;
                if (!claim_portable_structured_lang_alt_property_key(
                        structured_child_claims, structured_lang_alt_claims,
                        prefix, portable_base, portable_child_prefix,
                        portable_child, "x-default",
                        PortablePropertyOwner::ExistingXmp, &new_lang_claim)
                    || !new_lang_claim) {
                    return false;
                }

                PortableStructuredLangAltProperty item;
                item.prefix       = prefix;
                item.base         = portable_base;
                item.child_prefix = portable_child_prefix;
                item.child        = portable_child;
                item.lang         = "x-default";
                item.order        = order;
                item.value        = &e.value;
                structured_lang_alt->push_back(item);
            } else if (has_required_child_shape
                       && required_child_shape
                              == PortableStructuredChildShape::Indexed
                       && !existing_xmp_has_explicit_structured_indexed_child(
                           arena, decls, entries, prefix, portable_base,
                           entries.size(), 0U, portable_child_prefix,
                           portable_child)) {
                bool new_claim = false;
                if (!claim_portable_property_key(
                        claims, prefix, portable_base,
                        PortablePropertyOwner::ExistingXmp,
                        PortablePropertyShape::Structured, &new_claim)) {
                    return false;
                }
                if (!claim_portable_structured_child_key(
                        structured_child_claims, prefix, portable_base,
                        portable_child_prefix, portable_child,
                        PortableStructuredChildShape::Indexed)) {
                    return false;
                }

                PortableStructuredIndexedProperty item;
                item.prefix       = prefix;
                item.base         = portable_base;
                item.child_prefix = portable_child_prefix;
                item.child        = portable_child;
                item.index        = 1U;
                item.order        = order;
                item.value        = &e.value;
                item.container    = portable_existing_xmp_indexed_container(
                    portable_child_prefix, portable_child);
                structured_indexed->push_back(item);
            }
            return false;
        }
        if (normalized_child
            && existing_xmp_has_explicit_structured_scalar_child(
                arena, decls, entries, prefix, portable_base,
                current_entry_index, 0U, portable_child_prefix,
                portable_child)) {
            return false;
        }

        bool new_claim = false;
        if (!claim_portable_property_key(claims, prefix, portable_base,
                                         PortablePropertyOwner::ExistingXmp,
                                         PortablePropertyShape::Structured,
                                         &new_claim)) {
            return false;
        }
        if (!claim_portable_structured_child_key(
                structured_child_claims, prefix, portable_base,
                portable_child_prefix, portable_child,
                PortableStructuredChildShape::Scalar)) {
            return false;
        }

        PortableStructuredProperty item;
        item.prefix       = prefix;
        item.base         = portable_base;
        item.child_prefix = portable_child_prefix;
        item.child        = portable_child;
        item.order        = order;
        item.value        = &e.value;
        structured->push_back(item);
        return false;
    }

    static void collect_portable_custom_ns_decls(
        const ByteArena& arena, std::span<const Entry> entries,
        const XmpPortableOptions& options,
        std::vector<PortableCustomNsDecl>* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        if (!options.include_existing_xmp
            || options.existing_namespace_policy
                   != XmpExistingNamespacePolicy::PreserveCustom) {
            return;
        }

        uint32_t next_index = 1U;
        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (e.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }

            const std::string_view ns
                = arena_string(arena, e.key.data.xmp_property.schema_ns);
            std::string_view prefix;
            if (xmp_ns_to_portable_prefix(ns, &prefix)
                || !xmp_namespace_uri_is_xml_attr_safe(ns)
                || portable_custom_ns_prefix_for_uri(
                    ns,
                    std::span<const PortableCustomNsDecl>(out->data(),
                                                          out->size()),
                    &prefix)) {
                continue;
            }

            const std::string_view name
                = arena_string(arena, e.key.data.xmp_property.property_path);
            std::string_view portable_name;
            if (is_simple_xmp_property_name(name)) {
                portable_name = portable_property_name_for_existing_xmp("omns",
                                                                        name);
            } else {
                std::string_view base_name;
                std::string_view lang;
                if (parse_lang_alt_xmp_property_name(name, &base_name, &lang)) {
                    portable_name
                        = portable_property_name_for_existing_xmp("omns",
                                                                  base_name);
                } else {
                    uint32_t index = 0U;
                    if (parse_indexed_xmp_property_name(name, &base_name,
                                                        &index)) {
                        portable_name = portable_property_name_for_existing_xmp(
                            "omns", base_name);
                    } else {
                        std::string_view child_name;
                        std::string_view grandchild_name;
                        std::string_view lang2;
                        uint32_t grandchild_index2 = 0U;
                        if (parse_nested_structured_xmp_property_name(
                                name, &base_name, &child_name,
                                &grandchild_name)) {
                            portable_name
                                = portable_property_name_for_existing_xmp(
                                    "omns", base_name);
                        } else if (parse_nested_structured_lang_alt_xmp_property_name(
                                       name, &base_name, &child_name,
                                       &grandchild_name, &lang2)) {
                            portable_name
                                = portable_property_name_for_existing_xmp(
                                    "omns", base_name);
                        } else if (parse_nested_structured_indexed_xmp_property_name(
                                       name, &base_name, &child_name,
                                       &grandchild_name, &index)) {
                            portable_name
                                = portable_property_name_for_existing_xmp(
                                    "omns", base_name);
                        } else if (parse_structured_xmp_property_name(
                                       name, &base_name, &child_name)) {
                            portable_name
                                = portable_property_name_for_existing_xmp(
                                    "omns", base_name);
                        } else {
                            if (parse_indexed_nested_structured_xmp_property_name(
                                    name, &base_name, &index, &child_name,
                                    &grandchild_name)) {
                                portable_name
                                    = portable_property_name_for_existing_xmp(
                                        "omns", base_name);
                            } else if (
                                parse_indexed_nested_structured_lang_alt_xmp_property_name(
                                    name, &base_name, &index, &child_name,
                                    &grandchild_name, &lang2)) {
                                portable_name
                                    = portable_property_name_for_existing_xmp(
                                        "omns", base_name);
                            } else if (
                                parse_indexed_nested_structured_indexed_xmp_property_name(
                                    name, &base_name, &index, &child_name,
                                    &grandchild_name, &grandchild_index2)) {
                                portable_name
                                    = portable_property_name_for_existing_xmp(
                                        "omns", base_name);
                            } else if (
                                parse_indexed_structured_indexed_nested_xmp_property_name(
                                    name, &base_name, &index, &child_name,
                                    &grandchild_index2, &grandchild_name)) {
                                portable_name
                                    = portable_property_name_for_existing_xmp(
                                        "omns", base_name);
                            } else if (!parse_indexed_structured_xmp_property_name(
                                           name, &base_name, &index,
                                           &child_name)) {
                                continue;
                            } else {
                                portable_name
                                    = portable_property_name_for_existing_xmp(
                                        "omns", base_name);
                            }
                        }
                    }
                }
            }
            if (portable_name.empty()
                || xmp_property_is_nonportable_blob("omns", portable_name)
                || !portable_scalar_like_value_supported(arena, e.value)) {
                continue;
            }

            PortableCustomNsDecl decl;
            decl.prefix = "omns" + std::to_string(next_index);
            decl.uri.assign(ns.data(), ns.size());
            out->push_back(std::move(decl));
            next_index += 1U;
        }
    }

    static bool existing_xmp_namespace_is_used(
        const ByteArena& arena, std::span<const Entry> entries,
        const XmpPortableOptions& options, std::string_view ns) noexcept
    {
        if (!options.include_existing_xmp || ns.empty()) {
            return false;
        }
        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (e.key.kind != MetaKeyKind::XmpProperty
                || any(e.flags, EntryFlags::Deleted)) {
                continue;
            }
            const std::string_view schema_ns
                = arena_string(arena, e.key.data.xmp_property.schema_ns);
            if (schema_ns == ns) {
                return true;
            }
        }
        return false;
    }

    static bool
    existing_xmp_child_prefix_is_used(const ByteArena& arena,
                                      std::span<const Entry> entries,
                                      const XmpPortableOptions& options,
                                      std::string_view wanted_prefix) noexcept
    {
        if (!options.include_existing_xmp || wanted_prefix.empty()) {
            return false;
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (e.key.kind != MetaKeyKind::XmpProperty
                || any(e.flags, EntryFlags::Deleted)) {
                continue;
            }

            const std::string_view path
                = arena_string(arena, e.key.data.xmp_property.property_path);
            const std::string_view schema_ns
                = arena_string(arena, e.key.data.xmp_property.schema_ns);
            std::string_view base_name;
            std::string_view child_name;
            std::string_view grandchild_name;
            std::string_view lang;
            uint32_t item_index       = 0U;
            uint32_t child_index      = 0U;
            uint32_t grandchild_index = 0U;

            const bool parsed
                = parse_structured_xmp_property_name(path, &base_name,
                                                     &child_name)
                  || parse_indexed_structured_xmp_property_name(path,
                                                                &base_name,
                                                                &item_index,
                                                                &child_name)
                  || parse_structured_lang_alt_xmp_property_name(path,
                                                                 &base_name,
                                                                 &child_name,
                                                                 &lang)
                  || parse_indexed_structured_lang_alt_xmp_property_name(
                      path, &base_name, &item_index, &child_name, &lang)
                  || parse_structured_indexed_xmp_property_name(path,
                                                                &base_name,
                                                                &child_name,
                                                                &child_index)
                  || parse_indexed_structured_indexed_xmp_property_name(
                      path, &base_name, &item_index, &child_name, &child_index)
                  || parse_nested_structured_xmp_property_name(path, &base_name,
                                                               &child_name,
                                                               &grandchild_name)
                  || parse_nested_structured_lang_alt_xmp_property_name(
                      path, &base_name, &child_name, &grandchild_name, &lang)
                  || parse_nested_structured_indexed_xmp_property_name(
                      path, &base_name, &child_name, &grandchild_name,
                      &grandchild_index)
                  || parse_indexed_nested_structured_xmp_property_name(
                      path, &base_name, &item_index, &child_name,
                      &grandchild_name)
                  || parse_indexed_nested_structured_lang_alt_xmp_property_name(
                      path, &base_name, &item_index, &child_name,
                      &grandchild_name, &lang)
                  || parse_indexed_nested_structured_indexed_xmp_property_name(
                      path, &base_name, &item_index, &child_name,
                      &grandchild_name, &grandchild_index)
                  || parse_indexed_structured_indexed_nested_xmp_property_name(
                      path, &base_name, &item_index, &child_name, &child_index,
                      &grandchild_name);
            if (!parsed) {
                continue;
            }

            std::string_view prefix;
            if (!xmp_ns_to_portable_prefix(schema_ns, &prefix)
                || prefix.empty()) {
                continue;
            }

            const std::string_view portable_base
                = portable_property_name_for_existing_xmp(prefix, base_name);
            std::string_view child_prefix;
            std::string_view child_leaf;
            if (!resolve_existing_xmp_structured_child_to_portable(
                    prefix, portable_base, child_name, &child_prefix,
                    &child_leaf, nullptr)) {
                continue;
            }
            if (child_prefix == wanted_prefix) {
                return true;
            }
            if (!grandchild_name.empty()) {
                std::string_view grandchild_prefix;
                std::string_view grandchild_leaf;
                if (resolve_existing_xmp_nested_grandchild_to_portable(
                        prefix, portable_base, child_prefix, child_leaf,
                        grandchild_name, &grandchild_prefix, &grandchild_leaf,
                        nullptr)
                    && grandchild_prefix == wanted_prefix) {
                    return true;
                }
            }
        }
        return false;
    }

    static bool process_portable_exif_property(
        const ByteArena& arena, std::span<const Entry> entries,
        std::string_view prefix, std::string_view ifd, uint16_t tag,
        std::string_view portable_tag_name, bool exiftool_gpsdatetime_alias,
        const MetaValue& v, SpanWriter* w,
        PortablePropertyClaimMap* claims) noexcept
    {
        if (!w || !claims || prefix.empty() || portable_tag_name.empty()) {
            return false;
        }

        if ((ifd == "gpsifd" || ifd.ends_with("_gpsifd"))
            && (has_invalid_urational_value(arena, v)
                || has_invalid_srational_value(arena, v))) {
            return false;
        }

        if (portable_skip_invalid_rational_tag(tag)
            && (has_invalid_urational_value(arena, v)
                || has_invalid_srational_value(arena, v))) {
            return false;
        }
        if (tag == 0x9203U && has_invalid_srational_value(arena, v)) {
            return false;
        }

        const std::string_view tag_name = exif_tag_name(ifd, tag);
        if (tag_name.empty() || exif_tag_is_nonportable_blob(tag)
            || tag_name.ends_with("IFDPointer") || tag_name == "SubIFDs") {
            return false;
        }

        std::string_view emitted_name = portable_tag_name;
        if (exiftool_gpsdatetime_alias && prefix == "exif" && tag == 0x0007U
            && emitted_name == "GPSTimeStamp") {
            emitted_name = "GPSDateTime";
        }

        bool new_claim = false;
        if (!claim_portable_property_key(claims, prefix, emitted_name,
                                         PortablePropertyOwner::Exif,
                                         PortablePropertyShape::Scalar,
                                         &new_claim)
            || !new_claim) {
            return false;
        }

        if (emit_portable_exif_tag_property_override(w, prefix, ifd, tag,
                                                     emitted_name, arena,
                                                     entries, v)) {
            return true;
        }

        return emit_portable_property(w, prefix, emitted_name, arena, v);
    }

    static bool
    process_portable_exif_entry(const ByteArena& arena,
                                std::span<const Entry> entries, const Entry& e,
                                bool exiftool_gpsdatetime_alias, SpanWriter* w,
                                PortablePropertyClaimMap* claims) noexcept
    {
        if (!w || !claims || e.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }

        const std::string_view ifd = arena_string(arena,
                                                  e.key.data.exif_tag.ifd);
        std::string_view prefix;
        if (!ifd_to_portable_prefix(ifd, &prefix)) {
            return false;
        }

        const uint16_t tag = e.key.data.exif_tag.tag;

        const std::string_view tag_name = exif_tag_name(ifd, tag);
        if (tag_name.empty()) {
            return false;
        }

        std::string_view portable_tag_name
            = portable_property_name_for_exif_tag(prefix, ifd, tag, tag_name);
        if (exiftool_gpsdatetime_alias && prefix == "exif" && tag == 0x0007U
            && portable_tag_name == "GPSTimeStamp") {
            portable_tag_name = "GPSDateTime";
        }
        if (portable_tag_name.empty()) {
            return false;
        }

        return process_portable_exif_property(arena, entries, prefix, ifd, tag,
                                              portable_tag_name,
                                              exiftool_gpsdatetime_alias,
                                              e.value, w, claims);
    }

    static bool process_portable_exif_xmp_alias_entry(
        const ByteArena& arena, std::span<const Entry> entries, const Entry& e,
        SpanWriter* w, PortablePropertyClaimMap* claims) noexcept
    {
        if (!w || !claims || e.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }

        const std::string_view ifd = arena_string(arena,
                                                  e.key.data.exif_tag.ifd);
        const uint16_t tag         = e.key.data.exif_tag.tag;
        const std::string_view alias_name
            = portable_xmp_alias_name_for_exif_tag(ifd, tag);
        if (alias_name.empty()) {
            return false;
        }

        return process_portable_exif_property(arena, entries, "xmp", ifd, tag,
                                              alias_name, false, e.value, w,
                                              claims);
    }

    static bool iptc_portable_text_value(const ByteArena& arena,
                                         const MetaValue& value,
                                         std::string_view* out) noexcept
    {
        if (!out) {
            return false;
        }
        *out = {};

        if (value.kind != MetaValueKind::Text
            && value.kind != MetaValueKind::Bytes) {
            return false;
        }
        if (value.kind == MetaValueKind::Text
            && (value.text_encoding == TextEncoding::Utf16LE
                || value.text_encoding == TextEncoding::Utf16BE)) {
            return false;
        }

        const std::span<const std::byte> raw = arena.span(value.data.span);
        if (!bytes_are_ascii_text(raw)) {
            return false;
        }
        const std::string_view text(reinterpret_cast<const char*>(raw.data()),
                                    raw.size());
        return trim_ascii_nuls_and_spaces(text, out) && !out->empty();
    }

    static uint32_t iptc_decimal_digits(std::string_view text, size_t offset,
                                        size_t count) noexcept
    {
        uint32_t value = 0U;
        for (size_t i = 0U; i < count; ++i) {
            value = value * 10U + static_cast<uint32_t>(text[offset + i] - '0');
        }
        return value;
    }

    static bool iptc_date_is_valid(uint32_t year, uint32_t month,
                                   uint32_t day) noexcept
    {
        if (year == 0U || year > 9999U || month == 0U || month > 12U) {
            return false;
        }
        static constexpr std::array<uint8_t, 12> kDaysInMonth = {
            31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
        };
        uint32_t max_day = kDaysInMonth[month - 1U];
        const bool leap  = (year % 4U == 0U)
                          && ((year % 100U) != 0U || (year % 400U) == 0U);
        if (month == 2U && leap) {
            max_day = 29U;
        }
        return day > 0U && day <= max_day;
    }

    static bool iptc_date_to_xmp(std::string_view text,
                                 std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (text.size() != 8U) {
            return false;
        }
        for (const char c : text) {
            if (c < '0' || c > '9') {
                return false;
            }
        }

        const uint32_t year  = iptc_decimal_digits(text, 0U, 4U);
        const uint32_t month = iptc_decimal_digits(text, 4U, 2U);
        const uint32_t day   = iptc_decimal_digits(text, 6U, 2U);
        if (!iptc_date_is_valid(year, month, day)) {
            return false;
        }

        out->reserve(25U);
        out->append(text.substr(0U, 4U));
        out->push_back('-');
        out->append(text.substr(4U, 2U));
        out->push_back('-');
        out->append(text.substr(6U, 2U));
        return true;
    }

    static bool append_iptc_time_to_xmp(std::string_view text,
                                        std::string* out) noexcept
    {
        if (!out || (text.size() != 6U && text.size() != 11U)) {
            return false;
        }
        for (size_t i = 0U; i < 6U; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return false;
            }
        }
        const uint32_t hour   = iptc_decimal_digits(text, 0U, 2U);
        const uint32_t minute = iptc_decimal_digits(text, 2U, 2U);
        const uint32_t second = iptc_decimal_digits(text, 4U, 2U);
        if (hour > 23U || minute > 59U || second > 60U) {
            return false;
        }

        uint32_t offset_hour   = 0U;
        uint32_t offset_minute = 0U;
        if (text.size() == 11U) {
            if ((text[6] != '+' && text[6] != '-') || text[7] < '0'
                || text[7] > '9' || text[8] < '0' || text[8] > '9'
                || text[9] < '0' || text[9] > '9' || text[10] < '0'
                || text[10] > '9') {
                return false;
            }
            offset_hour   = iptc_decimal_digits(text, 7U, 2U);
            offset_minute = iptc_decimal_digits(text, 9U, 2U);
            if (offset_hour > 23U || offset_minute > 59U) {
                return false;
            }
        }

        out->push_back('T');
        out->append(text.substr(0U, 2U));
        out->push_back(':');
        out->append(text.substr(2U, 2U));
        out->push_back(':');
        out->append(text.substr(4U, 2U));
        if (text.size() == 11U) {
            out->push_back(text[6]);
            out->append(text.substr(7U, 2U));
            out->push_back(':');
            out->append(text.substr(9U, 2U));
        }
        return true;
    }

    static bool build_portable_iptc_datetime(const ByteArena& arena,
                                             std::span<const Entry> entries,
                                             uint16_t date_dataset,
                                             uint16_t time_dataset,
                                             std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();

        for (const Entry& entry : entries) {
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::IptcDataset
                || entry.key.data.iptc_dataset.record != 2U
                || entry.key.data.iptc_dataset.dataset != date_dataset) {
                continue;
            }
            std::string_view text;
            if (iptc_portable_text_value(arena, entry.value, &text)
                && iptc_date_to_xmp(text, out)) {
                break;
            }
        }
        if (out->empty()) {
            return false;
        }

        for (const Entry& entry : entries) {
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::IptcDataset
                || entry.key.data.iptc_dataset.record != 2U
                || entry.key.data.iptc_dataset.dataset != time_dataset) {
                continue;
            }
            std::string_view text;
            if (!iptc_portable_text_value(arena, entry.value, &text)) {
                continue;
            }
            const size_t date_size = out->size();
            if (append_iptc_time_to_xmp(text, out)) {
                break;
            }
            out->resize(date_size);
        }
        return true;
    }

    static bool emit_portable_iptc_datetime_property(
        const ByteArena& arena, std::span<const Entry> entries,
        uint16_t date_dataset, uint16_t time_dataset, std::string_view prefix,
        std::string_view name, SpanWriter* w,
        PortablePropertyClaimMap* claims) noexcept
    {
        if (!w || !claims) {
            return false;
        }
        std::string value;
        if (!build_portable_iptc_datetime(arena, entries, date_dataset,
                                          time_dataset, &value)) {
            return false;
        }

        bool new_claim = false;
        if (!claim_portable_property_key(claims, prefix, name,
                                         PortablePropertyOwner::Iptc,
                                         PortablePropertyShape::Scalar,
                                         &new_claim)
            || !new_claim) {
            return false;
        }
        return emit_portable_property_text(w, prefix, name, value);
    }

    static bool map_iptc_dataset_to_portable(
        uint16_t record, uint16_t dataset, std::string_view* out_prefix,
        std::string_view* out_name, bool* out_indexed,
        PortableIndexedProperty::Container* out_container) noexcept
    {
        if (!out_prefix || !out_name || !out_indexed || !out_container) {
            return false;
        }

        *out_prefix    = {};
        *out_name      = {};
        *out_indexed   = false;
        *out_container = PortableIndexedProperty::Container::Seq;

        if (record != 2U) {
            return false;
        }

        switch (dataset) {
        case 5U:  // ObjectName
            *out_prefix = "dc";
            *out_name   = "title";
            return true;
        case 25U:  // Keywords
            *out_prefix    = "dc";
            *out_name      = "subject";
            *out_indexed   = true;
            *out_container = PortableIndexedProperty::Container::Bag;
            return true;
        case 80U:  // By-line
            *out_prefix    = "dc";
            *out_name      = "creator";
            *out_indexed   = true;
            *out_container = PortableIndexedProperty::Container::Seq;
            return true;
        case 116U:  // CopyrightNotice
            *out_prefix = "dc";
            *out_name   = "rights";
            return true;
        case 120U:  // Caption-Abstract
            *out_prefix = "dc";
            *out_name   = "description";
            return true;
        case 15U:  // Category
            *out_prefix = "photoshop";
            *out_name   = "Category";
            return true;
        case 20U:  // SupplementalCategories
            *out_prefix    = "photoshop";
            *out_name      = "SupplementalCategories";
            *out_indexed   = true;
            *out_container = PortableIndexedProperty::Container::Bag;
            return true;
        case 40U:  // SpecialInstructions
            *out_prefix = "photoshop";
            *out_name   = "Instructions";
            return true;
        case 85U:  // By-lineTitle
            *out_prefix = "photoshop";
            *out_name   = "AuthorsPosition";
            return true;
        case 90U:  // City
            *out_prefix = "photoshop";
            *out_name   = "City";
            return true;
        case 92U:  // Sub-location
            *out_prefix = "Iptc4xmpCore";
            *out_name   = "Location";
            return true;
        case 95U:  // Province-State
            *out_prefix = "photoshop";
            *out_name   = "State";
            return true;
        case 100U:  // Country-PrimaryLocationCode
            *out_prefix = "Iptc4xmpCore";
            *out_name   = "CountryCode";
            return true;
        case 101U:  // Country-PrimaryLocationName
            *out_prefix = "photoshop";
            *out_name   = "Country";
            return true;
        case 103U:  // OriginalTransmissionReference
            *out_prefix = "photoshop";
            *out_name   = "TransmissionReference";
            return true;
        case 105U:  // Headline
            *out_prefix = "photoshop";
            *out_name   = "Headline";
            return true;
        case 110U:  // Credit
            *out_prefix = "photoshop";
            *out_name   = "Credit";
            return true;
        case 115U:  // Source
            *out_prefix = "photoshop";
            *out_name   = "Source";
            return true;
        case 122U:  // Writer-Editor
            *out_prefix = "photoshop";
            *out_name   = "CaptionWriter";
            return true;
        default: return false;
        }
    }

    static bool map_iptc_dataset_to_portable_structured(
        uint16_t record, uint16_t dataset, std::string_view* out_prefix,
        std::string_view* out_base, std::string_view* out_child_prefix,
        std::string_view* out_child) noexcept
    {
        if (!out_prefix || !out_base || !out_child_prefix || !out_child) {
            return false;
        }

        *out_prefix       = {};
        *out_base         = {};
        *out_child_prefix = {};
        *out_child        = {};

        if (record != 2U) {
            return false;
        }

        switch (dataset) {
        case 90U:  // City
            *out_prefix       = "Iptc4xmpCore";
            *out_base         = "LocationCreated";
            *out_child_prefix = "Iptc4xmpCore";
            *out_child        = "City";
            return true;
        case 92U:  // Sub-location
            *out_prefix       = "Iptc4xmpCore";
            *out_base         = "LocationCreated";
            *out_child_prefix = "Iptc4xmpCore";
            *out_child        = "Location";
            return true;
        case 95U:  // Province-State
            *out_prefix       = "Iptc4xmpCore";
            *out_base         = "LocationCreated";
            *out_child_prefix = "Iptc4xmpCore";
            *out_child        = "ProvinceState";
            return true;
        case 100U:  // Country-PrimaryLocationCode
            *out_prefix       = "Iptc4xmpCore";
            *out_base         = "LocationCreated";
            *out_child_prefix = "Iptc4xmpCore";
            *out_child        = "CountryCode";
            return true;
        case 101U:  // Country-PrimaryLocationName
            *out_prefix       = "Iptc4xmpCore";
            *out_base         = "LocationCreated";
            *out_child_prefix = "Iptc4xmpCore";
            *out_child        = "CountryName";
            return true;
        default: return false;
        }
    }

    static bool process_portable_iptc_entry(
        const ByteArena& arena, const Entry& e, uint32_t order, SpanWriter* w,
        PortablePropertyClaimMap* claims,
        PortableLangAltClaimOwnerMap* lang_alt_claims,
        PortableStructuredChildClaimMap* structured_child_claims,
        std::vector<PortableIndexedProperty>* indexed,
        std::vector<PortableLangAltProperty>* lang_alt,
        std::vector<PortableStructuredProperty>* structured) noexcept
    {
        if (!w || !claims || !lang_alt_claims || !structured_child_claims
            || !indexed || !lang_alt || !structured
            || e.key.kind != MetaKeyKind::IptcDataset) {
            return false;
        }

        std::string_view structured_prefix;
        std::string_view structured_base;
        std::string_view structured_child_prefix;
        std::string_view structured_child;
        if (map_iptc_dataset_to_portable_structured(
                e.key.data.iptc_dataset.record, e.key.data.iptc_dataset.dataset,
                &structured_prefix, &structured_base, &structured_child_prefix,
                &structured_child)
            && portable_scalar_like_value_supported(arena, e.value)) {
            bool new_claim = false;
            if (!claim_portable_property_key(claims, structured_prefix,
                                             structured_base,
                                             PortablePropertyOwner::Iptc,
                                             PortablePropertyShape::Structured,
                                             &new_claim)) {
                return false;
            }
            if (!claim_portable_structured_child_key(
                    structured_child_claims, structured_prefix, structured_base,
                    structured_child_prefix, structured_child,
                    PortableStructuredChildShape::Scalar)) {
                return false;
            }

            PortableStructuredProperty structured_item;
            structured_item.prefix       = structured_prefix;
            structured_item.base         = structured_base;
            structured_item.child_prefix = structured_child_prefix;
            structured_item.child        = structured_child;
            structured_item.order        = order;
            structured_item.value        = &e.value;
            structured->push_back(structured_item);
        }

        std::string_view prefix;
        std::string_view name;
        bool indexed_property = false;
        PortableIndexedProperty::Container container
            = PortableIndexedProperty::Container::Seq;
        if (!map_iptc_dataset_to_portable(e.key.data.iptc_dataset.record,
                                          e.key.data.iptc_dataset.dataset,
                                          &prefix, &name, &indexed_property,
                                          &container)) {
            return false;
        }

        if (indexed_property) {
            bool new_claim = false;
            if (!claim_portable_property_key(claims, prefix, name,
                                             PortablePropertyOwner::Iptc,
                                             PortablePropertyShape::Indexed,
                                             &new_claim)) {
                return false;
            }
            PortableIndexedProperty item;
            item.prefix    = prefix;
            item.base      = name;
            item.index     = order + 1U;
            item.order     = order;
            item.value     = &e.value;
            item.container = container;
            indexed->push_back(item);
            return false;
        }

        if (portable_property_prefers_lang_alt(prefix, name)) {
            bool new_claim = false;
            if (!claim_portable_lang_alt_property_key(
                    claims, lang_alt_claims, prefix, name, "x-default",
                    PortablePropertyOwner::Iptc, &new_claim)) {
                return false;
            }
            PortableLangAltProperty item;
            item.prefix = prefix;
            item.base   = name;
            item.lang   = "x-default";
            item.order  = order;
            item.value  = &e.value;
            lang_alt->push_back(item);
            return false;
        }

        bool new_claim = false;
        if (!claim_portable_property_key(claims, prefix, name,
                                         PortablePropertyOwner::Iptc,
                                         PortablePropertyShape::Scalar,
                                         &new_claim)
            || !new_claim) {
            return false;
        }

        return emit_portable_property(w, prefix, name, arena, e.value);
    }

    static bool portable_property_would_emit(std::string_view prefix,
                                             std::string_view name,
                                             const ByteArena& arena,
                                             const MetaValue& v) noexcept
    {
        SpanWriter w(std::span<std::byte> {}, 0U);
        const uint64_t before = w.needed;
        return emit_portable_property(&w, prefix, name, arena, v)
               && w.needed > before;
    }

    static bool portable_exif_property_would_emit(
        std::string_view prefix, std::string_view ifd, uint16_t tag,
        std::string_view name, const ByteArena& arena,
        std::span<const Entry> entries, const MetaValue& v) noexcept
    {
        SpanWriter w(std::span<std::byte> {}, 0U);
        const uint64_t before = w.needed;
        if (emit_portable_exif_tag_property_override(&w, prefix, ifd, tag, name,
                                                     arena, entries, v)) {
            return w.needed > before;
        }
        return emit_portable_property(&w, prefix, name, arena, v)
               && w.needed > before;
    }

    static void collect_generated_portable_property_keys(
        const ByteArena& arena, std::span<const Entry> entries,
        const XmpPortableOptions& options,
        PortablePropertyGeneratedShapeSet* out,
        PortableGeneratedLangAltKeySet* out_lang_alt) noexcept
    {
        if (!out || !out_lang_alt) {
            return;
        }
        out->clear();
        out_lang_alt->clear();

        if (options.include_iptc) {
            std::string value;
            if (build_portable_iptc_datetime(arena, entries, 55U, 60U, &value)) {
                (void)out->insert(PortablePropertyGeneratedShape {
                    PortablePropertyKey { "photoshop", "DateCreated" },
                    PortablePropertyShape::Scalar });
            }
            if (build_portable_iptc_datetime(arena, entries, 62U, 63U, &value)) {
                (void)out->insert(PortablePropertyGeneratedShape {
                    PortablePropertyKey { "xmp", "CreateDate" },
                    PortablePropertyShape::Scalar });
            }
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (any(e.flags, EntryFlags::Deleted)) {
                continue;
            }

            if (options.include_exif && e.key.kind == MetaKeyKind::ExifTag) {
                const std::string_view ifd
                    = arena_string(arena, e.key.data.exif_tag.ifd);
                std::string_view prefix;
                if (!ifd_to_portable_prefix(ifd, &prefix)) {
                    continue;
                }

                const uint16_t tag = e.key.data.exif_tag.tag;
                if ((ifd == "gpsifd" || ifd.ends_with("_gpsifd"))
                    && (has_invalid_urational_value(arena, e.value)
                        || has_invalid_srational_value(arena, e.value))) {
                    continue;
                }
                if (portable_skip_invalid_rational_tag(tag)
                    && (has_invalid_urational_value(arena, e.value)
                        || has_invalid_srational_value(arena, e.value))) {
                    continue;
                }
                if (tag == 0x9203U
                    && has_invalid_srational_value(arena, e.value)) {
                    continue;
                }

                const std::string_view tag_name = exif_tag_name(ifd, tag);
                if (tag_name.empty()) {
                    continue;
                }

                std::string_view portable_tag_name
                    = portable_property_name_for_exif_tag(prefix, ifd, tag,
                                                          tag_name);
                if (options.exiftool_gpsdatetime_alias && prefix == "exif"
                    && tag == 0x0007U && portable_tag_name == "GPSTimeStamp") {
                    portable_tag_name = "GPSDateTime";
                }
                if (portable_tag_name.empty()
                    || exif_tag_is_nonportable_blob(tag)
                    || tag_name.ends_with("IFDPointer")
                    || tag_name == "SubIFDs") {
                    continue;
                }

                if (portable_exif_property_would_emit(prefix, ifd, tag,
                                                      portable_tag_name, arena,
                                                      entries, e.value)) {
                    (void)out->insert(PortablePropertyGeneratedShape {
                        PortablePropertyKey { prefix, portable_tag_name },
                        PortablePropertyShape::Scalar });
                }

                const std::string_view xmp_alias_name
                    = portable_xmp_alias_name_for_exif_tag(ifd, tag);
                if (!xmp_alias_name.empty()
                    && portable_exif_property_would_emit("xmp", ifd, tag,
                                                         xmp_alias_name, arena,
                                                         entries, e.value)) {
                    (void)out->insert(PortablePropertyGeneratedShape {
                        PortablePropertyKey { "xmp", xmp_alias_name },
                        PortablePropertyShape::Scalar });
                }
                continue;
            }

            if (!options.include_iptc
                || e.key.kind != MetaKeyKind::IptcDataset) {
                continue;
            }

            std::string_view prefix;
            std::string_view name;
            bool indexed_property = false;
            PortableIndexedProperty::Container container
                = PortableIndexedProperty::Container::Seq;
            if (!map_iptc_dataset_to_portable(e.key.data.iptc_dataset.record,
                                              e.key.data.iptc_dataset.dataset,
                                              &prefix, &name, &indexed_property,
                                              &container)) {
                continue;
            }

            if (indexed_property) {
                if (portable_scalar_like_value_supported(arena, e.value)) {
                    (void)out->insert(PortablePropertyGeneratedShape {
                        PortablePropertyKey { prefix, name },
                        PortablePropertyShape::Indexed });
                }
                continue;
            }

            std::string_view structured_prefix;
            std::string_view structured_base;
            std::string_view structured_child_prefix;
            std::string_view structured_child;
            if (map_iptc_dataset_to_portable_structured(
                    e.key.data.iptc_dataset.record,
                    e.key.data.iptc_dataset.dataset, &structured_prefix,
                    &structured_base, &structured_child_prefix,
                    &structured_child)
                && portable_property_would_emit(structured_child_prefix,
                                                structured_child, arena,
                                                e.value)) {
                (void)out->insert(PortablePropertyGeneratedShape {
                    PortablePropertyKey { structured_prefix, structured_base },
                    PortablePropertyShape::Structured });
            }

            if (portable_property_prefers_lang_alt(prefix, name)) {
                if (portable_scalar_like_value_supported(arena, e.value)) {
                    (void)out_lang_alt->insert(PortableGeneratedLangAltKey {
                        PortablePropertyKey { prefix, name }, "x-default" });
                }
                continue;
            }

            if (portable_property_would_emit(prefix, name, arena, e.value)) {
                (void)out->insert(PortablePropertyGeneratedShape {
                    PortablePropertyKey { prefix, name },
                    PortablePropertyShape::Scalar });
            }
        }
    }

    static bool
    portable_lang_alt_property_less(const PortableLangAltProperty& a,
                                    const PortableLangAltProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.lang != b.lang) {
            return a.lang < b.lang;
        }
        return a.order < b.order;
    }

    static bool emit_portable_lang_alt_property_group(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena,
        std::span<const PortableLangAltProperty> items) noexcept
    {
        if (!w || prefix.empty() || name.empty() || items.empty()) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || !xmp_lang_value_is_safe(items[i].lang)) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent3);
        w->append("<");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        w->append(kIndent4);
        w->append("<rdf:Alt>\n");

        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || !xmp_lang_value_is_safe(items[i].lang)
                || !portable_scalar_like_value_supported(arena,
                                                         *items[i].value)) {
                continue;
            }
            w->append(kIndent4);
            w->append(kIndent1);
            w->append("<rdf:li xml:lang=\"");
            w->append(items[i].lang);
            w->append("\">");
            (void)emit_portable_value_inline(arena, *items[i].value, w);
            w->append("</rdf:li>\n");
        }

        w->append(kIndent4);
        w->append("</rdf:Alt>\n");
        w->append(kIndent3);
        w->append("</");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static void emit_portable_lang_alt_groups(
        SpanWriter* w, const ByteArena& arena,
        std::vector<PortableLangAltProperty>* lang_alt, uint32_t max_entries,
        uint32_t* emitted) noexcept
    {
        if (!w || !lang_alt || !emitted || w->limit_hit || lang_alt->empty()) {
            return;
        }

        std::stable_sort(lang_alt->begin(), lang_alt->end(),
                         portable_lang_alt_property_less);

        size_t i = 0U;
        while (i < lang_alt->size()) {
            if (max_entries != 0U && *emitted >= max_entries) {
                w->limit_hit = true;
                break;
            }

            size_t j = i + 1U;
            while (j < lang_alt->size()
                   && (*lang_alt)[j].prefix == (*lang_alt)[i].prefix
                   && (*lang_alt)[j].base == (*lang_alt)[i].base) {
                j += 1U;
            }

            if (emit_portable_lang_alt_property_group(
                    w, (*lang_alt)[i].prefix, (*lang_alt)[i].base, arena,
                    std::span<const PortableLangAltProperty>(
                        lang_alt->data() + i, j - i))) {
                *emitted += 1U;
            }

            i = j;
        }
    }

    static bool
    portable_indexed_property_less(const PortableIndexedProperty& a,
                                   const PortableIndexedProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.index != b.index) {
            return a.index < b.index;
        }
        if (a.container != b.container) {
            return static_cast<uint8_t>(a.container)
                   < static_cast<uint8_t>(b.container);
        }
        return a.order < b.order;
    }

    static bool portable_structured_property_less(
        const PortableStructuredProperty& a,
        const PortableStructuredProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.child_prefix != b.child_prefix) {
            return a.child_prefix < b.child_prefix;
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        return a.child < b.child;
    }

    static bool portable_structured_lang_alt_property_less(
        const PortableStructuredLangAltProperty& a,
        const PortableStructuredLangAltProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.child_prefix != b.child_prefix) {
            return a.child_prefix < b.child_prefix;
        }
        if (a.child != b.child) {
            return a.child < b.child;
        }
        if (a.lang != b.lang) {
            return a.lang < b.lang;
        }
        return a.order < b.order;
    }

    static bool portable_structured_indexed_property_less(
        const PortableStructuredIndexedProperty& a,
        const PortableStructuredIndexedProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.child_prefix != b.child_prefix) {
            return a.child_prefix < b.child_prefix;
        }
        if (a.child != b.child) {
            return a.child < b.child;
        }
        if (a.index != b.index) {
            return a.index < b.index;
        }
        if (a.container != b.container) {
            return static_cast<uint8_t>(a.container)
                   < static_cast<uint8_t>(b.container);
        }
        return a.order < b.order;
    }

    static bool portable_structured_nested_property_less(
        const PortableStructuredNestedProperty& a,
        const PortableStructuredNestedProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        if (a.child != b.child) {
            return a.child < b.child;
        }
        return a.grandchild < b.grandchild;
    }

    static bool portable_structured_nested_lang_alt_property_less(
        const PortableStructuredNestedLangAltProperty& a,
        const PortableStructuredNestedLangAltProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        if (a.child != b.child) {
            return a.child < b.child;
        }
        if (a.grandchild != b.grandchild) {
            return a.grandchild < b.grandchild;
        }
        return a.lang < b.lang;
    }

    static bool portable_structured_nested_indexed_property_less(
        const PortableStructuredNestedIndexedProperty& a,
        const PortableStructuredNestedIndexedProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        if (a.child != b.child) {
            return a.child < b.child;
        }
        if (a.grandchild != b.grandchild) {
            return a.grandchild < b.grandchild;
        }
        if (a.index != b.index) {
            return a.index < b.index;
        }
        return static_cast<uint8_t>(a.container)
               < static_cast<uint8_t>(b.container);
    }

    static bool portable_indexed_structured_property_less(
        const PortableIndexedStructuredProperty& a,
        const PortableIndexedStructuredProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.item_index != b.item_index) {
            return a.item_index < b.item_index;
        }
        if (a.child_prefix != b.child_prefix) {
            return a.child_prefix < b.child_prefix;
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        return a.child < b.child;
    }

    static bool portable_indexed_structured_nested_property_less(
        const PortableIndexedStructuredNestedProperty& a,
        const PortableIndexedStructuredNestedProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.item_index != b.item_index) {
            return a.item_index < b.item_index;
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        if (a.child != b.child) {
            return a.child < b.child;
        }
        return a.grandchild < b.grandchild;
    }

    static bool portable_indexed_structured_nested_lang_alt_property_less(
        const PortableIndexedStructuredNestedLangAltProperty& a,
        const PortableIndexedStructuredNestedLangAltProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.item_index != b.item_index) {
            return a.item_index < b.item_index;
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        if (a.child != b.child) {
            return a.child < b.child;
        }
        if (a.grandchild != b.grandchild) {
            return a.grandchild < b.grandchild;
        }
        return a.lang < b.lang;
    }

    static bool portable_indexed_structured_nested_indexed_property_less(
        const PortableIndexedStructuredNestedIndexedProperty& a,
        const PortableIndexedStructuredNestedIndexedProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.item_index != b.item_index) {
            return a.item_index < b.item_index;
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        if (a.child != b.child) {
            return a.child < b.child;
        }
        if (a.grandchild != b.grandchild) {
            return a.grandchild < b.grandchild;
        }
        if (a.index != b.index) {
            return a.index < b.index;
        }
        return static_cast<uint8_t>(a.container)
               < static_cast<uint8_t>(b.container);
    }

    static bool portable_indexed_structured_indexed_nested_property_less(
        const PortableIndexedStructuredIndexedNestedProperty& a,
        const PortableIndexedStructuredIndexedNestedProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.item_index != b.item_index) {
            return a.item_index < b.item_index;
        }
        if (a.child_prefix != b.child_prefix) {
            return a.child_prefix < b.child_prefix;
        }
        if (a.child != b.child) {
            return a.child < b.child;
        }
        if (a.child_index != b.child_index) {
            return a.child_index < b.child_index;
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        if (a.grandchild != b.grandchild) {
            return a.grandchild < b.grandchild;
        }
        return static_cast<uint8_t>(a.child_container)
               < static_cast<uint8_t>(b.child_container);
    }

    static bool portable_indexed_structured_deep_nested_property_less(
        const PortableIndexedStructuredDeepNestedProperty& a,
        const PortableIndexedStructuredDeepNestedProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.item_index != b.item_index) {
            return a.item_index < b.item_index;
        }
        if (a.child != b.child) {
            return a.child < b.child;
        }
        if (a.grandchild_prefix != b.grandchild_prefix) {
            return a.grandchild_prefix < b.grandchild_prefix;
        }
        if (a.grandchild != b.grandchild) {
            return a.grandchild < b.grandchild;
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        if (a.leaf_prefix != b.leaf_prefix) {
            return a.leaf_prefix < b.leaf_prefix;
        }
        return a.leaf < b.leaf;
    }

    static bool portable_indexed_structured_lang_alt_property_less(
        const PortableIndexedStructuredLangAltProperty& a,
        const PortableIndexedStructuredLangAltProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.item_index != b.item_index) {
            return a.item_index < b.item_index;
        }
        if (a.child_prefix != b.child_prefix) {
            return a.child_prefix < b.child_prefix;
        }
        if (a.child != b.child) {
            return a.child < b.child;
        }
        if (a.lang != b.lang) {
            return a.lang < b.lang;
        }
        return a.order < b.order;
    }

    static bool portable_indexed_structured_indexed_property_less(
        const PortableIndexedStructuredIndexedProperty& a,
        const PortableIndexedStructuredIndexedProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.item_index != b.item_index) {
            return a.item_index < b.item_index;
        }
        if (a.child_prefix != b.child_prefix) {
            return a.child_prefix < b.child_prefix;
        }
        if (a.child != b.child) {
            return a.child < b.child;
        }
        if (a.index != b.index) {
            return a.index < b.index;
        }
        if (a.container != b.container) {
            return static_cast<uint8_t>(a.container)
                   < static_cast<uint8_t>(b.container);
        }
        return a.order < b.order;
    }


    static bool emit_portable_indexed_property_group(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena,
        std::span<const PortableIndexedProperty> items) noexcept
    {
        if (!w || prefix.empty() || name.empty() || items.empty()) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent3);
        w->append("<");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        w->append(kIndent4);
        const PortableIndexedProperty::Container container = items[0].container;
        w->append(container == PortableIndexedProperty::Container::Bag
                      ? "<rdf:Bag>\n"
                      : "<rdf:Seq>\n");

        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value
                || !portable_scalar_like_value_supported(arena,
                                                         *items[i].value)) {
                continue;
            }
            w->append(kIndent4);
            w->append(kIndent1);
            w->append("<rdf:li>");
            (void)emit_portable_value_inline(arena, *items[i].value, w);
            w->append("</rdf:li>\n");
        }

        w->append(kIndent4);
        w->append(container == PortableIndexedProperty::Container::Bag
                      ? "</rdf:Bag>\n"
                      : "</rdf:Seq>\n");
        w->append(kIndent3);
        w->append("</");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static void
    emit_portable_indexed_groups(SpanWriter* w, const ByteArena& arena,
                                 std::vector<PortableIndexedProperty>* indexed,
                                 uint32_t max_entries,
                                 uint32_t* emitted) noexcept
    {
        if (!w || !indexed || !emitted || w->limit_hit || indexed->empty()) {
            return;
        }

        std::stable_sort(indexed->begin(), indexed->end(),
                         portable_indexed_property_less);

        size_t i = 0U;
        while (i < indexed->size()) {
            if (max_entries != 0U && *emitted >= max_entries) {
                w->limit_hit = true;
                break;
            }

            size_t j = i + 1U;
            while (j < indexed->size()
                   && (*indexed)[j].prefix == (*indexed)[i].prefix
                   && (*indexed)[j].base == (*indexed)[i].base
                   && (*indexed)[j].container == (*indexed)[i].container) {
                j += 1U;
            }

            if (emit_portable_indexed_property_group(
                    w, (*indexed)[i].prefix, (*indexed)[i].base, arena,
                    std::span<const PortableIndexedProperty>(indexed->data() + i,
                                                             j - i))) {
                *emitted += 1U;
            }

            i = j;
        }
    }

    static bool emit_portable_structured_indexed_child_group(
        SpanWriter* w, std::string_view prefix, std::string_view child_prefix,
        std::string_view name, const ByteArena& arena,
        std::span<const PortableStructuredIndexedProperty> items) noexcept
    {
        (void)prefix;
        if (!w || prefix.empty() || child_prefix.empty() || name.empty()
            || items.empty()) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent4);
        w->append("<");
        w->append(child_prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        w->append(kIndent4);
        w->append(kIndent1);
        w->append(items[0].container == PortableIndexedProperty::Container::Bag
                      ? "<rdf:Bag>\n"
                      : "<rdf:Seq>\n");

        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value
                || !portable_scalar_like_value_supported(arena,
                                                         *items[i].value)) {
                continue;
            }
            w->append(kIndent4);
            w->append(kIndent2);
            w->append("<rdf:li>");
            (void)emit_portable_value_inline(arena, *items[i].value, w);
            w->append("</rdf:li>\n");
        }

        w->append(kIndent4);
        w->append(kIndent1);
        w->append(items[0].container == PortableIndexedProperty::Container::Bag
                      ? "</rdf:Bag>\n"
                      : "</rdf:Seq>\n");
        w->append(kIndent4);
        w->append("</");
        w->append(child_prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_structured_lang_alt_child_group(
        SpanWriter* w, std::string_view prefix, std::string_view child_prefix,
        std::string_view name, const ByteArena& arena,
        std::span<const PortableStructuredLangAltProperty> items) noexcept
    {
        (void)prefix;
        if (!w || prefix.empty() || child_prefix.empty() || name.empty()
            || items.empty()) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || !xmp_lang_value_is_safe(items[i].lang)) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent4);
        w->append("<");
        w->append(child_prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        w->append(kIndent4);
        w->append(kIndent1);
        w->append("<rdf:Alt>\n");

        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || !xmp_lang_value_is_safe(items[i].lang)
                || !portable_scalar_like_value_supported(arena,
                                                         *items[i].value)) {
                continue;
            }
            w->append(kIndent4);
            w->append(kIndent2);
            w->append("<rdf:li xml:lang=\"");
            w->append(items[i].lang);
            w->append("\">");
            (void)emit_portable_value_inline(arena, *items[i].value, w);
            w->append("</rdf:li>\n");
        }

        w->append(kIndent4);
        w->append(kIndent1);
        w->append("</rdf:Alt>\n");
        w->append(kIndent4);
        w->append("</");
        w->append(child_prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_structured_nested_lang_alt_child_group(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena,
        std::span<const PortableStructuredNestedLangAltProperty> items) noexcept
    {
        if (!w || prefix.empty() || name.empty() || items.empty()) {
            return false;
        }
        std::string_view child_prefix;
        std::string_view child_name;
        if (!resolve_qualified_xmp_property_name(prefix, name, &child_prefix,
                                                 &child_name)) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || !xmp_lang_value_is_safe(items[i].lang)) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent4);
        w->append(kIndent1);
        w->append("<");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(">\n");
        w->append(kIndent4);
        w->append(kIndent2);
        w->append("<rdf:Alt>\n");

        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || !xmp_lang_value_is_safe(items[i].lang)
                || !portable_scalar_like_value_supported(arena,
                                                         *items[i].value)) {
                continue;
            }
            w->append(kIndent4);
            w->append(kIndent3);
            w->append("<rdf:li xml:lang=\"");
            w->append(items[i].lang);
            w->append("\">");
            (void)emit_portable_value_inline(arena, *items[i].value, w);
            w->append("</rdf:li>\n");
        }

        w->append(kIndent4);
        w->append(kIndent2);
        w->append("</rdf:Alt>\n");
        w->append(kIndent4);
        w->append(kIndent1);
        w->append("</");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_structured_nested_indexed_child_group(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena,
        std::span<const PortableStructuredNestedIndexedProperty> items) noexcept
    {
        if (!w || prefix.empty() || name.empty() || items.empty()) {
            return false;
        }
        std::string_view child_prefix;
        std::string_view child_name;
        if (!resolve_qualified_xmp_property_name(prefix, name, &child_prefix,
                                                 &child_name)) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent4);
        w->append(kIndent1);
        w->append("<");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(">\n");
        w->append(kIndent4);
        w->append(kIndent2);
        w->append(items[0].container == PortableIndexedProperty::Container::Bag
                      ? "<rdf:Bag>\n"
                      : "<rdf:Seq>\n");

        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value
                || !portable_scalar_like_value_supported(arena,
                                                         *items[i].value)) {
                continue;
            }
            w->append(kIndent4);
            w->append(kIndent3);
            w->append("<rdf:li>");
            (void)emit_portable_value_inline(arena, *items[i].value, w);
            w->append("</rdf:li>\n");
        }

        w->append(kIndent4);
        w->append(kIndent2);
        w->append(items[0].container == PortableIndexedProperty::Container::Bag
                      ? "</rdf:Bag>\n"
                      : "</rdf:Seq>\n");
        w->append(kIndent4);
        w->append(kIndent1);
        w->append("</");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_structured_nested_child_group(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena,
        std::span<const PortableStructuredNestedProperty> items,
        std::span<const PortableStructuredNestedLangAltProperty> lang_alt_items,
        std::span<const PortableStructuredNestedIndexedProperty>
            indexed_items) noexcept
    {
        if (!w || prefix.empty() || name.empty()
            || (items.empty() && lang_alt_items.empty()
                && indexed_items.empty())) {
            return false;
        }
        std::string_view child_prefix;
        std::string_view child_name;
        if (!resolve_qualified_xmp_property_name(prefix, name, &child_prefix,
                                                 &child_name)) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || items[i].grandchild.empty()) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < lang_alt_items.size(); ++i) {
            if (!lang_alt_items[i].value
                || !xmp_lang_value_is_safe(lang_alt_items[i].lang)) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena,
                                                     *lang_alt_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < indexed_items.size(); ++i) {
            if (!indexed_items[i].value) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena,
                                                     *indexed_items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent4);
        w->append("<");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(" rdf:parseType=\"Resource\">\n");

        size_t scalar_i   = 0U;
        size_t lang_alt_i = 0U;
        size_t indexed_i  = 0U;
        while (scalar_i < items.size() || lang_alt_i < lang_alt_items.size()
               || indexed_i < indexed_items.size()) {
            uint32_t scalar_order = UINT32_MAX;
            uint32_t lang_order   = UINT32_MAX;
            uint32_t idx_order    = UINT32_MAX;
            if (scalar_i < items.size()) {
                scalar_order = items[scalar_i].order;
            }
            if (lang_alt_i < lang_alt_items.size()) {
                lang_order = lang_alt_items[lang_alt_i].order;
            }
            if (indexed_i < indexed_items.size()) {
                idx_order = indexed_items[indexed_i].order;
            }

            if (scalar_order <= lang_order && scalar_order <= idx_order) {
                if (items[scalar_i].value && items[scalar_i].grandchild.size()
                    && portable_scalar_like_value_supported(
                        arena, *items[scalar_i].value)) {
                    std::string_view grandchild_prefix;
                    std::string_view grandchild_name;
                    if (!resolve_qualified_xmp_property_name(
                            prefix, items[scalar_i].grandchild,
                            &grandchild_prefix, &grandchild_name)) {
                        scalar_i += 1U;
                        continue;
                    }
                    w->append(kIndent4);
                    w->append(kIndent1);
                    w->append("<");
                    w->append(grandchild_prefix);
                    w->append(":");
                    w->append(grandchild_name);
                    w->append(">");
                    (void)emit_portable_value_inline(arena,
                                                     *items[scalar_i].value, w);
                    w->append("</");
                    w->append(grandchild_prefix);
                    w->append(":");
                    w->append(grandchild_name);
                    w->append(">\n");
                }
                scalar_i += 1U;
                continue;
            }

            if (lang_order <= idx_order) {
                size_t lang_alt_j = lang_alt_i + 1U;
                while (lang_alt_j < lang_alt_items.size()
                       && lang_alt_items[lang_alt_j].grandchild
                              == lang_alt_items[lang_alt_i].grandchild) {
                    lang_alt_j += 1U;
                }
                (void)emit_portable_structured_nested_lang_alt_child_group(
                    w, prefix, lang_alt_items[lang_alt_i].grandchild, arena,
                    std::span<const PortableStructuredNestedLangAltProperty>(
                        lang_alt_items.data() + lang_alt_i,
                        lang_alt_j - lang_alt_i));
                lang_alt_i = lang_alt_j;
                continue;
            }

            size_t indexed_j = indexed_i + 1U;
            while (indexed_j < indexed_items.size()
                   && indexed_items[indexed_j].grandchild
                          == indexed_items[indexed_i].grandchild
                   && indexed_items[indexed_j].container
                          == indexed_items[indexed_i].container) {
                indexed_j += 1U;
            }
            (void)emit_portable_structured_nested_indexed_child_group(
                w, prefix, indexed_items[indexed_i].grandchild, arena,
                std::span<const PortableStructuredNestedIndexedProperty>(
                    indexed_items.data() + indexed_i, indexed_j - indexed_i));
            indexed_i = indexed_j;
        }

        w->append(kIndent4);
        w->append("</");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_indexed_structured_indexed_child_group(
        SpanWriter* w, std::string_view prefix, std::string_view child_prefix,
        std::string_view name, const ByteArena& arena,
        std::span<const PortableIndexedStructuredIndexedProperty> items) noexcept
    {
        (void)prefix;
        if (!w || prefix.empty() || child_prefix.empty() || name.empty()
            || items.empty()) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent4);
        w->append(kIndent2);
        w->append("<");
        w->append(child_prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        w->append(kIndent4);
        w->append(kIndent3);
        w->append(items[0].container == PortableIndexedProperty::Container::Bag
                      ? "<rdf:Bag>\n"
                      : "<rdf:Seq>\n");

        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value
                || !portable_scalar_like_value_supported(arena,
                                                         *items[i].value)) {
                continue;
            }
            w->append(kIndent4);
            w->append(kIndent4);
            w->append("<rdf:li>");
            (void)emit_portable_value_inline(arena, *items[i].value, w);
            w->append("</rdf:li>\n");
        }

        w->append(kIndent4);
        w->append(kIndent3);
        w->append(items[0].container == PortableIndexedProperty::Container::Bag
                      ? "</rdf:Bag>\n"
                      : "</rdf:Seq>\n");
        w->append(kIndent4);
        w->append(kIndent2);
        w->append("</");
        w->append(child_prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_indexed_structured_nested_lang_alt_child_group(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena,
        std::span<const PortableIndexedStructuredNestedLangAltProperty>
            items) noexcept
    {
        if (!w || prefix.empty() || name.empty() || items.empty()) {
            return false;
        }
        std::string_view child_prefix;
        std::string_view child_name;
        if (!resolve_qualified_xmp_property_name(prefix, name, &child_prefix,
                                                 &child_name)) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || !xmp_lang_value_is_safe(items[i].lang)) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent4);
        w->append(kIndent2);
        w->append("<");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(">\n");
        w->append(kIndent4);
        w->append(kIndent3);
        w->append("<rdf:Alt>\n");

        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || !xmp_lang_value_is_safe(items[i].lang)
                || !portable_scalar_like_value_supported(arena,
                                                         *items[i].value)) {
                continue;
            }
            w->append(kIndent4);
            w->append(kIndent4);
            w->append("<rdf:li xml:lang=\"");
            w->append(items[i].lang);
            w->append("\">");
            (void)emit_portable_value_inline(arena, *items[i].value, w);
            w->append("</rdf:li>\n");
        }

        w->append(kIndent4);
        w->append(kIndent3);
        w->append("</rdf:Alt>\n");
        w->append(kIndent4);
        w->append(kIndent2);
        w->append("</");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_indexed_structured_nested_indexed_child_group(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena,
        std::span<const PortableIndexedStructuredNestedIndexedProperty>
            items) noexcept
    {
        if (!w || prefix.empty() || name.empty() || items.empty()) {
            return false;
        }
        std::string_view child_prefix;
        std::string_view child_name;
        if (!resolve_qualified_xmp_property_name(prefix, name, &child_prefix,
                                                 &child_name)) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent4);
        w->append(kIndent2);
        w->append("<");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(">\n");
        w->append(kIndent4);
        w->append(kIndent3);
        w->append(items[0].container == PortableIndexedProperty::Container::Bag
                      ? "<rdf:Bag>\n"
                      : "<rdf:Seq>\n");

        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value
                || !portable_scalar_like_value_supported(arena,
                                                         *items[i].value)) {
                continue;
            }
            w->append(kIndent4);
            w->append(kIndent4);
            w->append("<rdf:li>");
            (void)emit_portable_value_inline(arena, *items[i].value, w);
            w->append("</rdf:li>\n");
        }

        w->append(kIndent4);
        w->append(kIndent3);
        w->append(items[0].container == PortableIndexedProperty::Container::Bag
                      ? "</rdf:Bag>\n"
                      : "</rdf:Seq>\n");
        w->append(kIndent4);
        w->append(kIndent2);
        w->append("</");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_indexed_structured_nested_child_group(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena,
        std::span<const PortableIndexedStructuredNestedProperty> items,
        std::span<const PortableIndexedStructuredNestedLangAltProperty>
            lang_alt_items,
        std::span<const PortableIndexedStructuredNestedIndexedProperty>
            indexed_items,
        std::span<const PortableIndexedStructuredDeepNestedProperty>
            deep_nested_items) noexcept
    {
        if (!w || prefix.empty() || name.empty()
            || (items.empty() && lang_alt_items.empty() && indexed_items.empty()
                && deep_nested_items.empty())) {
            return false;
        }
        std::string_view child_prefix;
        std::string_view child_name;
        if (!resolve_qualified_xmp_property_name(prefix, name, &child_prefix,
                                                 &child_name)) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || items[i].grandchild.empty()) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < lang_alt_items.size(); ++i) {
            if (!lang_alt_items[i].value
                || !xmp_lang_value_is_safe(lang_alt_items[i].lang)) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena,
                                                     *lang_alt_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < indexed_items.size(); ++i) {
            if (!indexed_items[i].value) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena,
                                                     *indexed_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < deep_nested_items.size(); ++i) {
            if (!deep_nested_items[i].value
                || deep_nested_items[i].leaf.empty()) {
                continue;
            }
            if (portable_scalar_like_value_supported(
                    arena, *deep_nested_items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent4);
        w->append(kIndent2);
        w->append("<");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(" rdf:parseType=\"Resource\">\n");

        size_t scalar_i      = 0U;
        size_t lang_alt_i    = 0U;
        size_t indexed_i     = 0U;
        size_t deep_nested_i = 0U;
        while (scalar_i < items.size() || lang_alt_i < lang_alt_items.size()
               || indexed_i < indexed_items.size()
               || deep_nested_i < deep_nested_items.size()) {
            uint32_t scalar_order      = UINT32_MAX;
            uint32_t lang_order        = UINT32_MAX;
            uint32_t idx_order         = UINT32_MAX;
            uint32_t deep_nested_order = UINT32_MAX;
            if (scalar_i < items.size()) {
                scalar_order = items[scalar_i].order;
            }
            if (lang_alt_i < lang_alt_items.size()) {
                lang_order = lang_alt_items[lang_alt_i].order;
            }
            if (indexed_i < indexed_items.size()) {
                idx_order = indexed_items[indexed_i].order;
            }
            if (deep_nested_i < deep_nested_items.size()) {
                deep_nested_order = deep_nested_items[deep_nested_i].order;
            }

            if (scalar_order <= lang_order && scalar_order <= idx_order
                && scalar_order <= deep_nested_order) {
                if (items[scalar_i].value && items[scalar_i].grandchild.size()
                    && portable_scalar_like_value_supported(
                        arena, *items[scalar_i].value)) {
                    std::string_view grandchild_prefix;
                    std::string_view grandchild_name;
                    if (!resolve_qualified_xmp_property_name(
                            prefix, items[scalar_i].grandchild,
                            &grandchild_prefix, &grandchild_name)) {
                        scalar_i += 1U;
                        continue;
                    }
                    w->append(kIndent4);
                    w->append(kIndent3);
                    w->append("<");
                    w->append(grandchild_prefix);
                    w->append(":");
                    w->append(grandchild_name);
                    w->append(">");
                    (void)emit_portable_value_inline(arena,
                                                     *items[scalar_i].value, w);
                    w->append("</");
                    w->append(grandchild_prefix);
                    w->append(":");
                    w->append(grandchild_name);
                    w->append(">\n");
                }
                scalar_i += 1U;
                continue;
            }

            if (lang_order <= idx_order && lang_order <= deep_nested_order) {
                size_t lang_alt_j = lang_alt_i + 1U;
                while (lang_alt_j < lang_alt_items.size()
                       && lang_alt_items[lang_alt_j].grandchild
                              == lang_alt_items[lang_alt_i].grandchild) {
                    lang_alt_j += 1U;
                }
                (void)emit_portable_indexed_structured_nested_lang_alt_child_group(
                    w, prefix, lang_alt_items[lang_alt_i].grandchild, arena,
                    std::span<
                        const PortableIndexedStructuredNestedLangAltProperty>(
                        lang_alt_items.data() + lang_alt_i,
                        lang_alt_j - lang_alt_i));
                lang_alt_i = lang_alt_j;
                continue;
            }

            if (deep_nested_order <= idx_order) {
                size_t deep_nested_j = deep_nested_i + 1U;
                while (
                    deep_nested_j < deep_nested_items.size()
                    && deep_nested_items[deep_nested_j].grandchild_prefix
                           == deep_nested_items[deep_nested_i].grandchild_prefix
                    && deep_nested_items[deep_nested_j].grandchild
                           == deep_nested_items[deep_nested_i].grandchild) {
                    deep_nested_j += 1U;
                }

                const std::string_view grandchild_prefix
                    = deep_nested_items[deep_nested_i].grandchild_prefix.empty()
                          ? prefix
                          : deep_nested_items[deep_nested_i].grandchild_prefix;
                const std::string_view grandchild_name
                    = deep_nested_items[deep_nested_i].grandchild;

                w->append(kIndent4);
                w->append(kIndent2);
                w->append("<");
                w->append(grandchild_prefix);
                w->append(":");
                w->append(grandchild_name);
                w->append(" rdf:parseType=\"Resource\">\n");

                for (size_t emit_i = deep_nested_i; emit_i < deep_nested_j;
                     ++emit_i) {
                    if (!deep_nested_items[emit_i].value
                        || deep_nested_items[emit_i].leaf.empty()
                        || !portable_scalar_like_value_supported(
                            arena, *deep_nested_items[emit_i].value)) {
                        continue;
                    }

                    const std::string_view leaf_prefix
                        = deep_nested_items[emit_i].leaf_prefix.empty()
                              ? prefix
                              : deep_nested_items[emit_i].leaf_prefix;
                    const std::string_view leaf_name
                        = deep_nested_items[emit_i].leaf;
                    w->append(kIndent4);
                    w->append(kIndent3);
                    w->append("<");
                    w->append(leaf_prefix);
                    w->append(":");
                    w->append(leaf_name);
                    w->append(">");
                    (void)emit_portable_value_inline(
                        arena, *deep_nested_items[emit_i].value, w);
                    w->append("</");
                    w->append(leaf_prefix);
                    w->append(":");
                    w->append(leaf_name);
                    w->append(">\n");
                }

                w->append(kIndent4);
                w->append(kIndent2);
                w->append("</");
                w->append(grandchild_prefix);
                w->append(":");
                w->append(grandchild_name);
                w->append(">\n");
                deep_nested_i = deep_nested_j;
                continue;
            }

            size_t indexed_j = indexed_i + 1U;
            while (indexed_j < indexed_items.size()
                   && indexed_items[indexed_j].grandchild
                          == indexed_items[indexed_i].grandchild
                   && indexed_items[indexed_j].container
                          == indexed_items[indexed_i].container) {
                indexed_j += 1U;
            }
            (void)emit_portable_indexed_structured_nested_indexed_child_group(
                w, prefix, indexed_items[indexed_i].grandchild, arena,
                std::span<const PortableIndexedStructuredNestedIndexedProperty>(
                    indexed_items.data() + indexed_i, indexed_j - indexed_i));
            indexed_i = indexed_j;
        }

        w->append(kIndent4);
        w->append(kIndent2);
        w->append("</");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_indexed_structured_indexed_nested_child_group(
        SpanWriter* w, std::string_view child_prefix,
        std::string_view child_name, const ByteArena& arena,
        std::span<const PortableIndexedStructuredIndexedNestedProperty>
            items) noexcept
    {
        if (!w || child_prefix.empty() || child_name.empty() || items.empty()) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || items[i].grandchild.empty()) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent4);
        w->append(kIndent2);
        w->append("<");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(">\n");
        w->append(kIndent4);
        w->append(kIndent3);
        w->append(items[0].child_container
                          == PortableIndexedProperty::Container::Bag
                      ? "<rdf:Bag>\n"
                      : "<rdf:Seq>\n");

        size_t i = 0U;
        while (i < items.size()) {
            const uint32_t child_index = items[i].child_index;
            size_t i_next              = i;
            while (i_next < items.size()
                   && items[i_next].child_index == child_index) {
                i_next += 1U;
            }

            bool item_valid = false;
            for (size_t probe = i; probe < i_next; ++probe) {
                if (!items[probe].value || items[probe].grandchild.empty()) {
                    continue;
                }
                if (portable_scalar_like_value_supported(arena,
                                                         *items[probe].value)) {
                    item_valid = true;
                    break;
                }
            }
            if (item_valid) {
                w->append(kIndent4);
                w->append(kIndent4);
                w->append("<rdf:li rdf:parseType=\"Resource\">\n");
                for (size_t emit_i = i; emit_i < i_next; ++emit_i) {
                    if (!items[emit_i].value
                        || !portable_scalar_like_value_supported(
                            arena, *items[emit_i].value)) {
                        continue;
                    }
                    std::string_view grandchild_prefix;
                    std::string_view grandchild_name;
                    if (!resolve_qualified_xmp_property_name(
                            items[emit_i].prefix, items[emit_i].grandchild,
                            &grandchild_prefix, &grandchild_name)) {
                        continue;
                    }
                    w->append(kIndent4);
                    w->append(kIndent4);
                    w->append(kIndent1);
                    w->append("<");
                    w->append(grandchild_prefix);
                    w->append(":");
                    w->append(grandchild_name);
                    w->append(">");
                    (void)emit_portable_value_inline(arena,
                                                     *items[emit_i].value, w);
                    w->append("</");
                    w->append(grandchild_prefix);
                    w->append(":");
                    w->append(grandchild_name);
                    w->append(">\n");
                }
                w->append(kIndent4);
                w->append(kIndent4);
                w->append("</rdf:li>\n");
            }
            i = i_next;
        }

        w->append(kIndent4);
        w->append(kIndent3);
        w->append(items[0].child_container
                          == PortableIndexedProperty::Container::Bag
                      ? "</rdf:Bag>\n"
                      : "</rdf:Seq>\n");
        w->append(kIndent4);
        w->append(kIndent2);
        w->append("</");
        w->append(child_prefix);
        w->append(":");
        w->append(child_name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_structured_property_group(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena,
        std::span<const PortableStructuredProperty> items,
        std::span<const PortableStructuredLangAltProperty> lang_alt_items,
        std::span<const PortableStructuredIndexedProperty> indexed_items,
        std::span<const PortableStructuredNestedProperty> nested_items,
        std::span<const PortableStructuredNestedLangAltProperty>
            nested_lang_alt_items,
        std::span<const PortableStructuredNestedIndexedProperty>
            nested_indexed_items) noexcept
    {
        if (!w || prefix.empty() || name.empty()
            || (items.empty() && lang_alt_items.empty() && indexed_items.empty()
                && nested_items.empty() && nested_lang_alt_items.empty()
                && nested_indexed_items.empty())) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || items[i].child.empty()) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < lang_alt_items.size(); ++i) {
            if (!lang_alt_items[i].value
                || !xmp_lang_value_is_safe(lang_alt_items[i].lang)) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena,
                                                     *lang_alt_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < indexed_items.size(); ++i) {
            if (!indexed_items[i].value) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena,
                                                     *indexed_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < nested_items.size(); ++i) {
            if (!nested_items[i].value || nested_items[i].grandchild.empty()) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena,
                                                     *nested_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < nested_lang_alt_items.size(); ++i) {
            if (!nested_lang_alt_items[i].value
                || !nested_lang_alt_items[i].grandchild.size()
                || !xmp_lang_value_is_safe(nested_lang_alt_items[i].lang)) {
                continue;
            }
            if (portable_scalar_like_value_supported(
                    arena, *nested_lang_alt_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < nested_indexed_items.size(); ++i) {
            if (!nested_indexed_items[i].value
                || !nested_indexed_items[i].grandchild.size()) {
                continue;
            }
            if (portable_scalar_like_value_supported(
                    arena, *nested_indexed_items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent3);
        w->append("<");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(" rdf:parseType=\"Resource\">\n");

        size_t scalar_i          = 0U;
        size_t lang_alt_i        = 0U;
        size_t indexed_i         = 0U;
        size_t nested_i          = 0U;
        size_t nested_lang_alt_i = 0U;
        size_t nested_indexed_i  = 0U;
        while (scalar_i < items.size() || lang_alt_i < lang_alt_items.size()
               || indexed_i < indexed_items.size()
               || nested_i < nested_items.size()
               || nested_lang_alt_i < nested_lang_alt_items.size()
               || nested_indexed_i < nested_indexed_items.size()) {
            uint32_t scalar_order      = UINT32_MAX;
            uint32_t lang_order        = UINT32_MAX;
            uint32_t idx_order         = UINT32_MAX;
            uint32_t nested_order      = UINT32_MAX;
            uint32_t nested_lang_order = UINT32_MAX;
            uint32_t nested_idx_order  = UINT32_MAX;
            if (scalar_i < items.size()) {
                scalar_order = items[scalar_i].order;
            }
            if (lang_alt_i < lang_alt_items.size()) {
                lang_order = lang_alt_items[lang_alt_i].order;
            }
            if (indexed_i < indexed_items.size()) {
                idx_order = indexed_items[indexed_i].order;
            }
            if (nested_i < nested_items.size()) {
                nested_order = nested_items[nested_i].order;
            }
            if (nested_lang_alt_i < nested_lang_alt_items.size()) {
                nested_lang_order
                    = nested_lang_alt_items[nested_lang_alt_i].order;
            }
            if (nested_indexed_i < nested_indexed_items.size()) {
                nested_idx_order = nested_indexed_items[nested_indexed_i].order;
            }

            if (scalar_order <= lang_order && scalar_order <= idx_order
                && scalar_order <= nested_order
                && scalar_order <= nested_lang_order
                && scalar_order <= nested_idx_order) {
                if (items[scalar_i].value && !items[scalar_i].child.empty()
                    && portable_scalar_like_value_supported(
                        arena, *items[scalar_i].value)) {
                    const std::string_view child_prefix
                        = items[scalar_i].child_prefix.empty()
                              ? prefix
                              : items[scalar_i].child_prefix;
                    w->append(kIndent4);
                    w->append("<");
                    w->append(child_prefix);
                    w->append(":");
                    w->append(items[scalar_i].child);
                    w->append(">");
                    (void)emit_portable_value_inline(arena,
                                                     *items[scalar_i].value, w);
                    w->append("</");
                    w->append(child_prefix);
                    w->append(":");
                    w->append(items[scalar_i].child);
                    w->append(">\n");
                }
                scalar_i += 1U;
                continue;
            }

            if (lang_order <= idx_order && lang_order <= nested_order
                && lang_order <= nested_lang_order
                && lang_order <= nested_idx_order) {
                size_t lang_alt_j = lang_alt_i + 1U;
                while (lang_alt_j < lang_alt_items.size()
                       && lang_alt_items[lang_alt_j].child_prefix
                              == lang_alt_items[lang_alt_i].child_prefix
                       && lang_alt_items[lang_alt_j].child
                              == lang_alt_items[lang_alt_i].child) {
                    lang_alt_j += 1U;
                }
                (void)emit_portable_structured_lang_alt_child_group(
                    w, prefix,
                    lang_alt_items[lang_alt_i].child_prefix.empty()
                        ? prefix
                        : lang_alt_items[lang_alt_i].child_prefix,
                    lang_alt_items[lang_alt_i].child, arena,
                    std::span<const PortableStructuredLangAltProperty>(
                        lang_alt_items.data() + lang_alt_i,
                        lang_alt_j - lang_alt_i));
                lang_alt_i = lang_alt_j;
                continue;
            }

            if (nested_order <= idx_order && nested_order <= nested_lang_order
                && nested_order <= nested_idx_order) {
                size_t nested_j = nested_i + 1U;
                while (nested_j < nested_items.size()
                       && nested_items[nested_j].child
                              == nested_items[nested_i].child) {
                    nested_j += 1U;
                }
                (void)emit_portable_structured_nested_child_group(
                    w, prefix, nested_items[nested_i].child, arena,
                    std::span<const PortableStructuredNestedProperty>(
                        nested_items.data() + nested_i, nested_j - nested_i),
                    std::span<const PortableStructuredNestedLangAltProperty>(),
                    std::span<const PortableStructuredNestedIndexedProperty>());
                nested_i = nested_j;
                continue;
            }

            if (nested_lang_order <= idx_order
                && nested_lang_order <= nested_idx_order) {
                size_t nested_lang_alt_j = nested_lang_alt_i + 1U;
                while (
                    nested_lang_alt_j < nested_lang_alt_items.size()
                    && nested_lang_alt_items[nested_lang_alt_j].child
                           == nested_lang_alt_items[nested_lang_alt_i].child) {
                    nested_lang_alt_j += 1U;
                }
                (void)emit_portable_structured_nested_child_group(
                    w, prefix, nested_lang_alt_items[nested_lang_alt_i].child,
                    arena, std::span<const PortableStructuredNestedProperty>(),
                    std::span<const PortableStructuredNestedLangAltProperty>(
                        nested_lang_alt_items.data() + nested_lang_alt_i,
                        nested_lang_alt_j - nested_lang_alt_i),
                    std::span<const PortableStructuredNestedIndexedProperty>());
                nested_lang_alt_i = nested_lang_alt_j;
                continue;
            }

            if (nested_idx_order <= idx_order) {
                size_t nested_indexed_j = nested_indexed_i + 1U;
                while (
                    nested_indexed_j < nested_indexed_items.size()
                    && nested_indexed_items[nested_indexed_j].child
                           == nested_indexed_items[nested_indexed_i].child
                    && nested_indexed_items[nested_indexed_j].container
                           == nested_indexed_items[nested_indexed_i].container) {
                    nested_indexed_j += 1U;
                }
                (void)emit_portable_structured_nested_child_group(
                    w, prefix, nested_indexed_items[nested_indexed_i].child,
                    arena, std::span<const PortableStructuredNestedProperty>(),
                    std::span<const PortableStructuredNestedLangAltProperty>(),
                    std::span<const PortableStructuredNestedIndexedProperty>(
                        nested_indexed_items.data() + nested_indexed_i,
                        nested_indexed_j - nested_indexed_i));
                nested_indexed_i = nested_indexed_j;
                continue;
            }

            size_t indexed_j = indexed_i + 1U;
            while (indexed_j < indexed_items.size()
                   && indexed_items[indexed_j].child_prefix
                          == indexed_items[indexed_i].child_prefix
                   && indexed_items[indexed_j].child
                          == indexed_items[indexed_i].child
                   && indexed_items[indexed_j].container
                          == indexed_items[indexed_i].container) {
                indexed_j += 1U;
            }
            (void)emit_portable_structured_indexed_child_group(
                w, prefix,
                indexed_items[indexed_i].child_prefix.empty()
                    ? prefix
                    : indexed_items[indexed_i].child_prefix,
                indexed_items[indexed_i].child, arena,
                std::span<const PortableStructuredIndexedProperty>(
                    indexed_items.data() + indexed_i, indexed_j - indexed_i));
            indexed_i = indexed_j;
        }

        w->append(kIndent3);
        w->append("</");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static void emit_portable_structured_groups(
        SpanWriter* w, const ByteArena& arena,
        std::vector<PortableStructuredProperty>* structured,
        std::vector<PortableStructuredLangAltProperty>* structured_lang_alt,
        std::vector<PortableStructuredIndexedProperty>* structured_indexed,
        std::vector<PortableStructuredNestedProperty>* structured_nested,
        std::vector<PortableStructuredNestedLangAltProperty>*
            structured_nested_lang_alt,
        std::vector<PortableStructuredNestedIndexedProperty>*
            structured_nested_indexed,
        uint32_t max_entries, uint32_t* emitted) noexcept
    {
        if (!w || !structured || !structured_lang_alt || !structured_indexed
            || !structured_nested || !structured_nested_lang_alt
            || !structured_nested_indexed || !emitted || w->limit_hit
            || (structured->empty() && structured_lang_alt->empty()
                && structured_indexed->empty() && structured_nested->empty()
                && structured_nested_lang_alt->empty()
                && structured_nested_indexed->empty())) {
            return;
        }

        std::stable_sort(structured->begin(), structured->end(),
                         portable_structured_property_less);
        std::stable_sort(structured_lang_alt->begin(),
                         structured_lang_alt->end(),
                         portable_structured_lang_alt_property_less);
        std::stable_sort(structured_indexed->begin(), structured_indexed->end(),
                         portable_structured_indexed_property_less);
        std::stable_sort(structured_nested->begin(), structured_nested->end(),
                         portable_structured_nested_property_less);
        std::stable_sort(structured_nested_lang_alt->begin(),
                         structured_nested_lang_alt->end(),
                         portable_structured_nested_lang_alt_property_less);
        std::stable_sort(structured_nested_indexed->begin(),
                         structured_nested_indexed->end(),
                         portable_structured_nested_indexed_property_less);

        size_t i = 0U;
        size_t j = 0U;
        size_t k = 0U;
        size_t m = 0U;
        size_t n = 0U;
        size_t p = 0U;
        while (i < structured->size() || j < structured_lang_alt->size()
               || k < structured_indexed->size()
               || m < structured_nested->size()
               || n < structured_nested_lang_alt->size()
               || p < structured_nested_indexed->size()) {
            if (max_entries != 0U && *emitted >= max_entries) {
                w->limit_hit = true;
                break;
            }

            std::string_view group_prefix;
            std::string_view group_base;
            bool have_group = false;
            if (i < structured->size()) {
                group_prefix = (*structured)[i].prefix;
                group_base   = (*structured)[i].base;
                have_group   = true;
            }
            if (j < structured_lang_alt->size()) {
                const PortableStructuredLangAltProperty& alt
                    = (*structured_lang_alt)[j];
                if (!have_group || alt.prefix < group_prefix
                    || (alt.prefix == group_prefix && alt.base < group_base)) {
                    group_prefix = alt.prefix;
                    group_base   = alt.base;
                    have_group   = true;
                }
            }
            if (k < structured_indexed->size()) {
                const PortableStructuredIndexedProperty& idx
                    = (*structured_indexed)[k];
                if (!have_group || idx.prefix < group_prefix
                    || (idx.prefix == group_prefix && idx.base < group_base)) {
                    group_prefix = idx.prefix;
                    group_base   = idx.base;
                }
            }
            if (m < structured_nested->size()) {
                const PortableStructuredNestedProperty& nested
                    = (*structured_nested)[m];
                if (!have_group || nested.prefix < group_prefix
                    || (nested.prefix == group_prefix
                        && nested.base < group_base)) {
                    group_prefix = nested.prefix;
                    group_base   = nested.base;
                }
            }
            if (n < structured_nested_lang_alt->size()) {
                const PortableStructuredNestedLangAltProperty& nested_alt
                    = (*structured_nested_lang_alt)[n];
                if (!have_group || nested_alt.prefix < group_prefix
                    || (nested_alt.prefix == group_prefix
                        && nested_alt.base < group_base)) {
                    group_prefix = nested_alt.prefix;
                    group_base   = nested_alt.base;
                    have_group   = true;
                }
            }
            if (p < structured_nested_indexed->size()) {
                const PortableStructuredNestedIndexedProperty& nested_idx
                    = (*structured_nested_indexed)[p];
                if (!have_group || nested_idx.prefix < group_prefix
                    || (nested_idx.prefix == group_prefix
                        && nested_idx.base < group_base)) {
                    group_prefix = nested_idx.prefix;
                    group_base   = nested_idx.base;
                    have_group   = true;
                }
            }

            size_t i_next = i;
            while (i_next < structured->size()
                   && (*structured)[i_next].prefix == group_prefix
                   && (*structured)[i_next].base == group_base) {
                i_next += 1U;
            }
            size_t j_next = j;
            while (j_next < structured_lang_alt->size()
                   && (*structured_lang_alt)[j_next].prefix == group_prefix
                   && (*structured_lang_alt)[j_next].base == group_base) {
                j_next += 1U;
            }
            size_t k_next = k;
            while (k_next < structured_indexed->size()
                   && (*structured_indexed)[k_next].prefix == group_prefix
                   && (*structured_indexed)[k_next].base == group_base) {
                k_next += 1U;
            }
            size_t m_next = m;
            while (m_next < structured_nested->size()
                   && (*structured_nested)[m_next].prefix == group_prefix
                   && (*structured_nested)[m_next].base == group_base) {
                m_next += 1U;
            }
            size_t n_next = n;
            while (
                n_next < structured_nested_lang_alt->size()
                && (*structured_nested_lang_alt)[n_next].prefix == group_prefix
                && (*structured_nested_lang_alt)[n_next].base == group_base) {
                n_next += 1U;
            }
            size_t p_next = p;
            while (p_next < structured_nested_indexed->size()
                   && (*structured_nested_indexed)[p_next].prefix
                          == group_prefix
                   && (*structured_nested_indexed)[p_next].base == group_base) {
                p_next += 1U;
            }

            if (emit_portable_structured_property_group(
                    w, group_prefix, group_base, arena,
                    std::span<const PortableStructuredProperty>(
                        structured->data() + i, i_next - i),
                    std::span<const PortableStructuredLangAltProperty>(
                        structured_lang_alt->data() + j, j_next - j),
                    std::span<const PortableStructuredIndexedProperty>(
                        structured_indexed->data() + k, k_next - k),
                    std::span<const PortableStructuredNestedProperty>(
                        structured_nested->data() + m, m_next - m),
                    std::span<const PortableStructuredNestedLangAltProperty>(
                        structured_nested_lang_alt->data() + n, n_next - n),
                    std::span<const PortableStructuredNestedIndexedProperty>(
                        structured_nested_indexed->data() + p, p_next - p))) {
                *emitted += 1U;
            }

            i = i_next;
            j = j_next;
            k = k_next;
            m = m_next;
            n = n_next;
            p = p_next;
        }
    }

    static bool emit_portable_indexed_structured_property_group(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena,
        std::span<const PortableIndexedStructuredProperty> items,
        std::span<const PortableIndexedStructuredLangAltProperty> lang_alt_items,
        std::span<const PortableIndexedStructuredIndexedProperty> indexed_items,
        std::span<const PortableIndexedStructuredNestedProperty> nested_items,
        std::span<const PortableIndexedStructuredNestedLangAltProperty>
            nested_lang_alt_items,
        std::span<const PortableIndexedStructuredNestedIndexedProperty>
            nested_indexed_items,
        std::span<const PortableIndexedStructuredDeepNestedProperty>
            deep_nested_items,
        std::span<const PortableIndexedStructuredIndexedNestedProperty>
            indexed_nested_items) noexcept
    {
        if (!w || prefix.empty() || name.empty()
            || (items.empty() && lang_alt_items.empty() && indexed_items.empty()
                && nested_items.empty() && nested_lang_alt_items.empty()
                && nested_indexed_items.empty() && deep_nested_items.empty()
                && indexed_nested_items.empty())) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value || items[i].child.empty()) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < lang_alt_items.size(); ++i) {
            if (!lang_alt_items[i].value
                || !xmp_lang_value_is_safe(lang_alt_items[i].lang)) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena,
                                                     *lang_alt_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < indexed_items.size(); ++i) {
            if (!indexed_items[i].value) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena,
                                                     *indexed_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < nested_items.size(); ++i) {
            if (!nested_items[i].value || nested_items[i].grandchild.empty()) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena,
                                                     *nested_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < nested_lang_alt_items.size(); ++i) {
            if (!nested_lang_alt_items[i].value
                || !nested_lang_alt_items[i].grandchild.size()
                || !xmp_lang_value_is_safe(nested_lang_alt_items[i].lang)) {
                continue;
            }
            if (portable_scalar_like_value_supported(
                    arena, *nested_lang_alt_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < nested_indexed_items.size(); ++i) {
            if (!nested_indexed_items[i].value
                || !nested_indexed_items[i].grandchild.size()) {
                continue;
            }
            if (portable_scalar_like_value_supported(
                    arena, *nested_indexed_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < indexed_nested_items.size(); ++i) {
            if (!indexed_nested_items[i].value
                || indexed_nested_items[i].child.empty()
                || !indexed_nested_items[i].grandchild.size()) {
                continue;
            }
            if (portable_scalar_like_value_supported(
                    arena, *indexed_nested_items[i].value)) {
                valid += 1U;
            }
        }
        for (size_t i = 0; i < deep_nested_items.size(); ++i) {
            if (!deep_nested_items[i].value
                || deep_nested_items[i].child.empty()
                || !deep_nested_items[i].grandchild.size()
                || !deep_nested_items[i].leaf.size()) {
                continue;
            }
            if (portable_scalar_like_value_supported(
                    arena, *deep_nested_items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent3);
        w->append("<");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        PortableIndexedProperty::Container container
            = PortableIndexedProperty::Container::Seq;
        if (!items.empty()) {
            container = items[0].container;
        } else if (!indexed_items.empty()) {
            container = indexed_items[0].container;
        } else if (!nested_indexed_items.empty()) {
            container = nested_indexed_items[0].container;
        }

        w->append(kIndent4);
        w->append(container == PortableIndexedProperty::Container::Bag
                      ? "<rdf:Bag>\n"
                      : "<rdf:Seq>\n");

        size_t i = 0U;
        size_t j = 0U;
        size_t k = 0U;
        size_t m = 0U;
        size_t n = 0U;
        size_t p = 0U;
        size_t d = 0U;
        size_t q = 0U;
        while (i < items.size() || j < lang_alt_items.size()
               || k < indexed_items.size() || m < nested_items.size()
               || n < nested_lang_alt_items.size()
               || p < nested_indexed_items.size()
               || d < deep_nested_items.size()
               || q < indexed_nested_items.size()) {
            uint32_t item_index = 0U;
            if (i < items.size()) {
                item_index = items[i].item_index;
            }
            if (j < lang_alt_items.size()
                && (item_index == 0U
                    || lang_alt_items[j].item_index < item_index)) {
                item_index = lang_alt_items[j].item_index;
            }
            if (k < indexed_items.size()
                && (item_index == 0U
                    || indexed_items[k].item_index < item_index)) {
                item_index = indexed_items[k].item_index;
            }
            if (m < nested_items.size()
                && (item_index == 0U
                    || nested_items[m].item_index < item_index)) {
                item_index = nested_items[m].item_index;
            }
            if (n < nested_lang_alt_items.size()
                && (item_index == 0U
                    || nested_lang_alt_items[n].item_index < item_index)) {
                item_index = nested_lang_alt_items[n].item_index;
            }
            if (p < nested_indexed_items.size()
                && (item_index == 0U
                    || nested_indexed_items[p].item_index < item_index)) {
                item_index = nested_indexed_items[p].item_index;
            }
            if (d < deep_nested_items.size()
                && (item_index == 0U
                    || deep_nested_items[d].item_index < item_index)) {
                item_index = deep_nested_items[d].item_index;
            }
            if (q < indexed_nested_items.size()
                && (item_index == 0U
                    || indexed_nested_items[q].item_index < item_index)) {
                item_index = indexed_nested_items[q].item_index;
            }

            size_t i_next = i;
            while (i_next < items.size()
                   && items[i_next].item_index == item_index) {
                i_next += 1U;
            }
            size_t j_next = j;
            while (j_next < lang_alt_items.size()
                   && lang_alt_items[j_next].item_index == item_index) {
                j_next += 1U;
            }
            size_t k_next = k;
            while (k_next < indexed_items.size()
                   && indexed_items[k_next].item_index == item_index) {
                k_next += 1U;
            }
            size_t m_next = m;
            while (m_next < nested_items.size()
                   && nested_items[m_next].item_index == item_index) {
                m_next += 1U;
            }
            size_t n_next = n;
            while (n_next < nested_lang_alt_items.size()
                   && nested_lang_alt_items[n_next].item_index == item_index) {
                n_next += 1U;
            }
            size_t p_next = p;
            while (p_next < nested_indexed_items.size()
                   && nested_indexed_items[p_next].item_index == item_index) {
                p_next += 1U;
            }
            size_t d_next = d;
            while (d_next < deep_nested_items.size()
                   && deep_nested_items[d_next].item_index == item_index) {
                d_next += 1U;
            }
            size_t q_next = q;
            while (q_next < indexed_nested_items.size()
                   && indexed_nested_items[q_next].item_index == item_index) {
                q_next += 1U;
            }

            bool item_valid = false;
            for (size_t scalar_probe = i; scalar_probe < i_next;
                 ++scalar_probe) {
                if (items[scalar_probe].value
                    && !items[scalar_probe].child.empty()
                    && portable_scalar_like_value_supported(
                        arena, *items[scalar_probe].value)) {
                    item_valid = true;
                    break;
                }
            }
            if (!item_valid) {
                for (size_t alt_probe = j; alt_probe < j_next; ++alt_probe) {
                    if (lang_alt_items[alt_probe].value
                        && !lang_alt_items[alt_probe].child.empty()
                        && xmp_lang_value_is_safe(lang_alt_items[alt_probe].lang)
                        && portable_scalar_like_value_supported(
                            arena, *lang_alt_items[alt_probe].value)) {
                        item_valid = true;
                        break;
                    }
                }
            }
            if (!item_valid) {
                for (size_t idx_probe = k; idx_probe < k_next; ++idx_probe) {
                    if (indexed_items[idx_probe].value
                        && !indexed_items[idx_probe].child.empty()
                        && portable_scalar_like_value_supported(
                            arena, *indexed_items[idx_probe].value)) {
                        item_valid = true;
                        break;
                    }
                }
            }
            if (!item_valid) {
                for (size_t nested_probe = m; nested_probe < m_next;
                     ++nested_probe) {
                    if (nested_items[nested_probe].value
                        && !nested_items[nested_probe].child.empty()
                        && !nested_items[nested_probe].grandchild.empty()
                        && portable_scalar_like_value_supported(
                            arena, *nested_items[nested_probe].value)) {
                        item_valid = true;
                        break;
                    }
                }
            }
            if (!item_valid) {
                for (size_t nested_alt_probe = n; nested_alt_probe < n_next;
                     ++nested_alt_probe) {
                    if (nested_lang_alt_items[nested_alt_probe].value
                        && !nested_lang_alt_items[nested_alt_probe].child.empty()
                        && !nested_lang_alt_items[nested_alt_probe]
                                .grandchild.empty()
                        && xmp_lang_value_is_safe(
                            nested_lang_alt_items[nested_alt_probe].lang)
                        && portable_scalar_like_value_supported(
                            arena,
                            *nested_lang_alt_items[nested_alt_probe].value)) {
                        item_valid = true;
                        break;
                    }
                }
            }
            if (!item_valid) {
                for (size_t nested_idx_probe = p; nested_idx_probe < p_next;
                     ++nested_idx_probe) {
                    if (nested_indexed_items[nested_idx_probe].value
                        && !nested_indexed_items[nested_idx_probe].child.empty()
                        && !nested_indexed_items[nested_idx_probe]
                                .grandchild.empty()
                        && portable_scalar_like_value_supported(
                            arena,
                            *nested_indexed_items[nested_idx_probe].value)) {
                        item_valid = true;
                        break;
                    }
                }
            }
            if (!item_valid) {
                for (size_t indexed_nested_probe = q;
                     indexed_nested_probe < q_next; ++indexed_nested_probe) {
                    if (indexed_nested_items[indexed_nested_probe].value
                        && !indexed_nested_items[indexed_nested_probe]
                                .child.empty()
                        && !indexed_nested_items[indexed_nested_probe]
                                .grandchild.empty()
                        && portable_scalar_like_value_supported(
                            arena,
                            *indexed_nested_items[indexed_nested_probe].value)) {
                        item_valid = true;
                        break;
                    }
                }
            }
            if (!item_valid) {
                for (size_t deep_nested_probe = d; deep_nested_probe < d_next;
                     ++deep_nested_probe) {
                    if (deep_nested_items[deep_nested_probe].value
                        && !deep_nested_items[deep_nested_probe].child.empty()
                        && !deep_nested_items[deep_nested_probe]
                                .grandchild.empty()
                        && !deep_nested_items[deep_nested_probe].leaf.empty()
                        && portable_scalar_like_value_supported(
                            arena,
                            *deep_nested_items[deep_nested_probe].value)) {
                        item_valid = true;
                        break;
                    }
                }
            }
            if (item_valid) {
                w->append(kIndent4);
                w->append(kIndent1);
                w->append("<rdf:li rdf:parseType=\"Resource\">\n");
                size_t scalar_k          = i;
                size_t lang_alt_k        = j;
                size_t indexed_k         = k;
                size_t nested_k          = m;
                size_t nested_lang_alt_k = n;
                size_t nested_indexed_k  = p;
                size_t deep_nested_k     = d;
                size_t indexed_nested_k  = q;
                while (scalar_k < i_next || lang_alt_k < j_next
                       || indexed_k < k_next || nested_k < m_next
                       || nested_lang_alt_k < n_next
                       || nested_indexed_k < p_next || deep_nested_k < d_next
                       || indexed_nested_k < q_next) {
                    uint32_t scalar_order         = UINT32_MAX;
                    uint32_t lang_order           = UINT32_MAX;
                    uint32_t idx_order            = UINT32_MAX;
                    uint32_t nested_order         = UINT32_MAX;
                    uint32_t nested_lang_order    = UINT32_MAX;
                    uint32_t nested_idx_order     = UINT32_MAX;
                    uint32_t deep_nested_order    = UINT32_MAX;
                    uint32_t indexed_nested_order = UINT32_MAX;
                    if (scalar_k < i_next) {
                        scalar_order = items[scalar_k].order;
                    }
                    if (lang_alt_k < j_next) {
                        lang_order = lang_alt_items[lang_alt_k].order;
                    }
                    if (indexed_k < k_next) {
                        idx_order = indexed_items[indexed_k].order;
                    }
                    if (nested_k < m_next) {
                        nested_order = nested_items[nested_k].order;
                    }
                    if (nested_lang_alt_k < n_next) {
                        nested_lang_order
                            = nested_lang_alt_items[nested_lang_alt_k].order;
                    }
                    if (nested_indexed_k < p_next) {
                        nested_idx_order
                            = nested_indexed_items[nested_indexed_k].order;
                    }
                    if (deep_nested_k < d_next) {
                        deep_nested_order
                            = deep_nested_items[deep_nested_k].order;
                    }
                    if (indexed_nested_k < q_next) {
                        indexed_nested_order
                            = indexed_nested_items[indexed_nested_k].order;
                    }

                    if (scalar_order <= lang_order && scalar_order <= idx_order
                        && scalar_order <= nested_order
                        && scalar_order <= nested_lang_order
                        && scalar_order <= nested_idx_order
                        && scalar_order <= deep_nested_order
                        && scalar_order <= indexed_nested_order) {
                        if (items[scalar_k].value
                            && !items[scalar_k].child.empty()
                            && portable_scalar_like_value_supported(
                                arena, *items[scalar_k].value)) {
                            const std::string_view child_prefix
                                = items[scalar_k].child_prefix.empty()
                                      ? prefix
                                      : items[scalar_k].child_prefix;
                            w->append(kIndent4);
                            w->append(kIndent2);
                            w->append("<");
                            w->append(child_prefix);
                            w->append(":");
                            w->append(items[scalar_k].child);
                            w->append(">");
                            (void)emit_portable_value_inline(
                                arena, *items[scalar_k].value, w);
                            w->append("</");
                            w->append(child_prefix);
                            w->append(":");
                            w->append(items[scalar_k].child);
                            w->append(">\n");
                        }
                        scalar_k += 1U;
                        continue;
                    }

                    if (lang_order <= idx_order && lang_order <= nested_order
                        && lang_order <= nested_lang_order
                        && lang_order <= nested_idx_order
                        && lang_order <= deep_nested_order
                        && lang_order <= indexed_nested_order) {
                        size_t lang_alt_next = lang_alt_k + 1U;
                        while (lang_alt_next < j_next
                               && lang_alt_items[lang_alt_next].child_prefix
                                      == lang_alt_items[lang_alt_k].child_prefix
                               && lang_alt_items[lang_alt_next].child
                                      == lang_alt_items[lang_alt_k].child) {
                            lang_alt_next += 1U;
                        }

                        const std::string_view child_prefix
                            = lang_alt_items[lang_alt_k].child_prefix.empty()
                                  ? prefix
                                  : lang_alt_items[lang_alt_k].child_prefix;
                        w->append(kIndent4);
                        w->append(kIndent2);
                        w->append("<");
                        w->append(child_prefix);
                        w->append(":");
                        w->append(lang_alt_items[lang_alt_k].child);
                        w->append(">\n");
                        w->append(kIndent4);
                        w->append(kIndent3);
                        w->append("<rdf:Alt>\n");
                        for (size_t alt_emit = lang_alt_k;
                             alt_emit < lang_alt_next; ++alt_emit) {
                            if (!lang_alt_items[alt_emit].value
                                || !xmp_lang_value_is_safe(
                                    lang_alt_items[alt_emit].lang)
                                || !portable_scalar_like_value_supported(
                                    arena, *lang_alt_items[alt_emit].value)) {
                                continue;
                            }
                            w->append(kIndent4);
                            w->append(kIndent4);
                            w->append("<rdf:li xml:lang=\"");
                            w->append(lang_alt_items[alt_emit].lang);
                            w->append("\">");
                            (void)emit_portable_value_inline(
                                arena, *lang_alt_items[alt_emit].value, w);
                            w->append("</rdf:li>\n");
                        }
                        w->append(kIndent4);
                        w->append(kIndent3);
                        w->append("</rdf:Alt>\n");
                        w->append(kIndent4);
                        w->append(kIndent2);
                        w->append("</");
                        w->append(child_prefix);
                        w->append(":");
                        w->append(lang_alt_items[lang_alt_k].child);
                        w->append(">\n");
                        lang_alt_k = lang_alt_next;
                        continue;
                    }

                    if (nested_order <= idx_order
                        && nested_order <= nested_lang_order
                        && nested_order <= nested_idx_order
                        && nested_order <= deep_nested_order
                        && nested_order <= indexed_nested_order) {
                        size_t nested_next = nested_k + 1U;
                        while (nested_next < m_next
                               && nested_items[nested_next].child
                                      == nested_items[nested_k].child) {
                            nested_next += 1U;
                        }
                        (void)emit_portable_indexed_structured_nested_child_group(
                            w, prefix, nested_items[nested_k].child, arena,
                            std::span<
                                const PortableIndexedStructuredNestedProperty>(
                                nested_items.data() + nested_k,
                                nested_next - nested_k),
                            std::span<
                                const PortableIndexedStructuredNestedLangAltProperty>(),
                            std::span<
                                const PortableIndexedStructuredNestedIndexedProperty>(),
                            std::span<
                                const PortableIndexedStructuredDeepNestedProperty>());
                        nested_k = nested_next;
                        continue;
                    }

                    if (nested_lang_order <= idx_order
                        && nested_lang_order <= nested_idx_order
                        && nested_lang_order <= deep_nested_order
                        && nested_lang_order <= indexed_nested_order) {
                        size_t nested_lang_next = nested_lang_alt_k + 1U;
                        while (
                            nested_lang_next < n_next
                            && nested_lang_alt_items[nested_lang_next].child
                                   == nested_lang_alt_items[nested_lang_alt_k]
                                          .child) {
                            nested_lang_next += 1U;
                        }
                        (void)emit_portable_indexed_structured_nested_child_group(
                            w, prefix,
                            nested_lang_alt_items[nested_lang_alt_k].child,
                            arena,
                            std::span<
                                const PortableIndexedStructuredNestedProperty>(),
                            std::span<
                                const PortableIndexedStructuredNestedLangAltProperty>(
                                nested_lang_alt_items.data() + nested_lang_alt_k,
                                nested_lang_next - nested_lang_alt_k),
                            std::span<
                                const PortableIndexedStructuredNestedIndexedProperty>(),
                            std::span<
                                const PortableIndexedStructuredDeepNestedProperty>());
                        nested_lang_alt_k = nested_lang_next;
                        continue;
                    }

                    if (nested_idx_order <= idx_order
                        && nested_idx_order <= deep_nested_order
                        && nested_idx_order <= indexed_nested_order) {
                        size_t nested_idx_next = nested_indexed_k + 1U;
                        while (
                            nested_idx_next < p_next
                            && nested_indexed_items[nested_idx_next].child
                                   == nested_indexed_items[nested_indexed_k].child
                            && nested_indexed_items[nested_idx_next].container
                                   == nested_indexed_items[nested_indexed_k]
                                          .container) {
                            nested_idx_next += 1U;
                        }
                        (void)emit_portable_indexed_structured_nested_child_group(
                            w, prefix,
                            nested_indexed_items[nested_indexed_k].child, arena,
                            std::span<
                                const PortableIndexedStructuredNestedProperty>(),
                            std::span<
                                const PortableIndexedStructuredNestedLangAltProperty>(),
                            std::span<
                                const PortableIndexedStructuredNestedIndexedProperty>(
                                nested_indexed_items.data() + nested_indexed_k,
                                nested_idx_next - nested_indexed_k),
                            std::span<
                                const PortableIndexedStructuredDeepNestedProperty>());
                        nested_indexed_k = nested_idx_next;
                        continue;
                    }

                    if (deep_nested_order <= idx_order
                        && deep_nested_order <= indexed_nested_order) {
                        size_t deep_nested_next = deep_nested_k + 1U;
                        while (
                            deep_nested_next < d_next
                            && deep_nested_items[deep_nested_next].child
                                   == deep_nested_items[deep_nested_k].child) {
                            deep_nested_next += 1U;
                        }
                        (void)emit_portable_indexed_structured_nested_child_group(
                            w, prefix, deep_nested_items[deep_nested_k].child,
                            arena,
                            std::span<
                                const PortableIndexedStructuredNestedProperty>(),
                            std::span<
                                const PortableIndexedStructuredNestedLangAltProperty>(),
                            std::span<
                                const PortableIndexedStructuredNestedIndexedProperty>(),
                            std::span<
                                const PortableIndexedStructuredDeepNestedProperty>(
                                deep_nested_items.data() + deep_nested_k,
                                deep_nested_next - deep_nested_k));
                        deep_nested_k = deep_nested_next;
                        continue;
                    }

                    if (indexed_nested_order <= idx_order) {
                        size_t indexed_nested_next = indexed_nested_k + 1U;
                        while (
                            indexed_nested_next < q_next
                            && indexed_nested_items[indexed_nested_next]
                                       .child_prefix
                                   == indexed_nested_items[indexed_nested_k]
                                          .child_prefix
                            && indexed_nested_items[indexed_nested_next].child
                                   == indexed_nested_items[indexed_nested_k].child
                            && indexed_nested_items[indexed_nested_next]
                                       .child_container
                                   == indexed_nested_items[indexed_nested_k]
                                          .child_container) {
                            indexed_nested_next += 1U;
                        }
                        (void)emit_portable_indexed_structured_indexed_nested_child_group(
                            w,
                            indexed_nested_items[indexed_nested_k]
                                    .child_prefix.empty()
                                ? prefix
                                : indexed_nested_items[indexed_nested_k]
                                      .child_prefix,
                            indexed_nested_items[indexed_nested_k].child, arena,
                            std::span<
                                const PortableIndexedStructuredIndexedNestedProperty>(
                                indexed_nested_items.data() + indexed_nested_k,
                                indexed_nested_next - indexed_nested_k));
                        indexed_nested_k = indexed_nested_next;
                        continue;
                    }

                    size_t indexed_next = indexed_k + 1U;
                    while (indexed_next < k_next
                           && indexed_items[indexed_next].child_prefix
                                  == indexed_items[indexed_k].child_prefix
                           && indexed_items[indexed_next].child
                                  == indexed_items[indexed_k].child
                           && indexed_items[indexed_next].container
                                  == indexed_items[indexed_k].container) {
                        indexed_next += 1U;
                    }
                    (void)emit_portable_indexed_structured_indexed_child_group(
                        w, prefix,
                        indexed_items[indexed_k].child_prefix.empty()
                            ? prefix
                            : indexed_items[indexed_k].child_prefix,
                        indexed_items[indexed_k].child, arena,
                        std::span<const PortableIndexedStructuredIndexedProperty>(
                            indexed_items.data() + indexed_k,
                            indexed_next - indexed_k));
                    indexed_k = indexed_next;
                }
                w->append(kIndent4);
                w->append(kIndent1);
                w->append("</rdf:li>\n");
            }

            i = i_next;
            j = j_next;
            k = k_next;
            m = m_next;
            n = n_next;
            p = p_next;
            d = d_next;
            q = q_next;
        }

        w->append(kIndent4);
        w->append(container == PortableIndexedProperty::Container::Bag
                      ? "</rdf:Bag>\n"
                      : "</rdf:Seq>\n");
        w->append(kIndent3);
        w->append("</");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static void emit_portable_indexed_structured_groups(
        SpanWriter* w, const ByteArena& arena,
        std::vector<PortableIndexedStructuredProperty>* indexed_structured,
        std::vector<PortableIndexedStructuredLangAltProperty>*
            indexed_structured_lang_alt,
        std::vector<PortableIndexedStructuredNestedProperty>*
            indexed_structured_nested,
        std::vector<PortableIndexedStructuredNestedLangAltProperty>*
            indexed_structured_nested_lang_alt,
        std::vector<PortableIndexedStructuredNestedIndexedProperty>*
            indexed_structured_nested_indexed,
        std::vector<PortableIndexedStructuredDeepNestedProperty>*
            indexed_structured_deep_nested,
        std::vector<PortableIndexedStructuredIndexedNestedProperty>*
            indexed_structured_indexed_nested,
        std::vector<PortableIndexedStructuredIndexedProperty>*
            indexed_structured_indexed,
        uint32_t max_entries, uint32_t* emitted) noexcept
    {
        if (!w || !indexed_structured || !indexed_structured_lang_alt
            || !indexed_structured_nested || !indexed_structured_nested_lang_alt
            || !indexed_structured_nested_indexed
            || !indexed_structured_deep_nested
            || !indexed_structured_indexed_nested || !indexed_structured_indexed
            || !emitted || w->limit_hit
            || (indexed_structured->empty()
                && indexed_structured_lang_alt->empty()
                && indexed_structured_nested->empty()
                && indexed_structured_nested_lang_alt->empty()
                && indexed_structured_nested_indexed->empty()
                && indexed_structured_deep_nested->empty()
                && indexed_structured_indexed_nested->empty()
                && indexed_structured_indexed->empty())) {
            return;
        }

        std::stable_sort(indexed_structured->begin(), indexed_structured->end(),
                         portable_indexed_structured_property_less);
        std::stable_sort(indexed_structured_lang_alt->begin(),
                         indexed_structured_lang_alt->end(),
                         portable_indexed_structured_lang_alt_property_less);
        std::stable_sort(indexed_structured_nested->begin(),
                         indexed_structured_nested->end(),
                         portable_indexed_structured_nested_property_less);
        std::stable_sort(
            indexed_structured_nested_lang_alt->begin(),
            indexed_structured_nested_lang_alt->end(),
            portable_indexed_structured_nested_lang_alt_property_less);
        std::stable_sort(
            indexed_structured_nested_indexed->begin(),
            indexed_structured_nested_indexed->end(),
            portable_indexed_structured_nested_indexed_property_less);
        std::stable_sort(indexed_structured_deep_nested->begin(),
                         indexed_structured_deep_nested->end(),
                         portable_indexed_structured_deep_nested_property_less);
        std::stable_sort(
            indexed_structured_indexed_nested->begin(),
            indexed_structured_indexed_nested->end(),
            portable_indexed_structured_indexed_nested_property_less);
        std::stable_sort(indexed_structured_indexed->begin(),
                         indexed_structured_indexed->end(),
                         portable_indexed_structured_indexed_property_less);

        size_t i = 0U;
        size_t j = 0U;
        size_t k = 0U;
        size_t m = 0U;
        size_t n = 0U;
        size_t p = 0U;
        size_t d = 0U;
        size_t q = 0U;
        while (i < indexed_structured->size()
               || j < indexed_structured_lang_alt->size()
               || k < indexed_structured_nested->size()
               || m < indexed_structured_nested_lang_alt->size()
               || n < indexed_structured_indexed->size()
               || p < indexed_structured_nested_indexed->size()
               || d < indexed_structured_deep_nested->size()
               || q < indexed_structured_indexed_nested->size()) {
            if (max_entries != 0U && *emitted >= max_entries) {
                w->limit_hit = true;
                break;
            }

            std::string_view group_prefix;
            std::string_view group_base;
            bool have_group = false;
            if (i < indexed_structured->size()) {
                group_prefix = (*indexed_structured)[i].prefix;
                group_base   = (*indexed_structured)[i].base;
                have_group   = true;
            }
            if (j < indexed_structured_lang_alt->size()) {
                const PortableIndexedStructuredLangAltProperty& alt
                    = (*indexed_structured_lang_alt)[j];
                if (!have_group || alt.prefix < group_prefix
                    || (alt.prefix == group_prefix && alt.base < group_base)) {
                    group_prefix = alt.prefix;
                    group_base   = alt.base;
                    have_group   = true;
                }
            }
            if (k < indexed_structured_nested->size()) {
                const PortableIndexedStructuredNestedProperty& nested
                    = (*indexed_structured_nested)[k];
                if (!have_group || nested.prefix < group_prefix
                    || (nested.prefix == group_prefix
                        && nested.base < group_base)) {
                    group_prefix = nested.prefix;
                    group_base   = nested.base;
                    have_group   = true;
                }
            }
            if (m < indexed_structured_nested_lang_alt->size()) {
                const PortableIndexedStructuredNestedLangAltProperty& nested_alt
                    = (*indexed_structured_nested_lang_alt)[m];
                if (!have_group || nested_alt.prefix < group_prefix
                    || (nested_alt.prefix == group_prefix
                        && nested_alt.base < group_base)) {
                    group_prefix = nested_alt.prefix;
                    group_base   = nested_alt.base;
                    have_group   = true;
                }
            }
            if (n < indexed_structured_indexed->size()) {
                const PortableIndexedStructuredIndexedProperty& idx
                    = (*indexed_structured_indexed)[n];
                if (!have_group || idx.prefix < group_prefix
                    || (idx.prefix == group_prefix && idx.base < group_base)) {
                    group_prefix = idx.prefix;
                    group_base   = idx.base;
                }
            }
            if (p < indexed_structured_nested_indexed->size()) {
                const PortableIndexedStructuredNestedIndexedProperty& nested_idx
                    = (*indexed_structured_nested_indexed)[p];
                if (!have_group || nested_idx.prefix < group_prefix
                    || (nested_idx.prefix == group_prefix
                        && nested_idx.base < group_base)) {
                    group_prefix = nested_idx.prefix;
                    group_base   = nested_idx.base;
                    have_group   = true;
                }
            }
            if (d < indexed_structured_deep_nested->size()) {
                const PortableIndexedStructuredDeepNestedProperty& deep_nested
                    = (*indexed_structured_deep_nested)[d];
                if (!have_group || deep_nested.prefix < group_prefix
                    || (deep_nested.prefix == group_prefix
                        && deep_nested.base < group_base)) {
                    group_prefix = deep_nested.prefix;
                    group_base   = deep_nested.base;
                    have_group   = true;
                }
            }
            if (q < indexed_structured_indexed_nested->size()) {
                const PortableIndexedStructuredIndexedNestedProperty& nested_idx
                    = (*indexed_structured_indexed_nested)[q];
                if (!have_group || nested_idx.prefix < group_prefix
                    || (nested_idx.prefix == group_prefix
                        && nested_idx.base < group_base)) {
                    group_prefix = nested_idx.prefix;
                    group_base   = nested_idx.base;
                    have_group   = true;
                }
            }

            size_t i_next = i;
            while (i_next < indexed_structured->size()
                   && (*indexed_structured)[i_next].prefix == group_prefix
                   && (*indexed_structured)[i_next].base == group_base) {
                i_next += 1U;
            }
            size_t j_next = j;
            while (
                j_next < indexed_structured_lang_alt->size()
                && (*indexed_structured_lang_alt)[j_next].prefix == group_prefix
                && (*indexed_structured_lang_alt)[j_next].base == group_base) {
                j_next += 1U;
            }
            size_t k_next = k;
            while (k_next < indexed_structured_nested->size()
                   && (*indexed_structured_nested)[k_next].prefix
                          == group_prefix
                   && (*indexed_structured_nested)[k_next].base == group_base) {
                k_next += 1U;
            }
            size_t m_next = m;
            while (m_next < indexed_structured_nested_lang_alt->size()
                   && (*indexed_structured_nested_lang_alt)[m_next].prefix
                          == group_prefix
                   && (*indexed_structured_nested_lang_alt)[m_next].base
                          == group_base) {
                m_next += 1U;
            }
            size_t n_next = n;
            while (
                n_next < indexed_structured_indexed->size()
                && (*indexed_structured_indexed)[n_next].prefix == group_prefix
                && (*indexed_structured_indexed)[n_next].base == group_base) {
                n_next += 1U;
            }
            size_t p_next = p;
            while (p_next < indexed_structured_nested_indexed->size()
                   && (*indexed_structured_nested_indexed)[p_next].prefix
                          == group_prefix
                   && (*indexed_structured_nested_indexed)[p_next].base
                          == group_base) {
                p_next += 1U;
            }
            size_t d_next = d;
            while (d_next < indexed_structured_deep_nested->size()
                   && (*indexed_structured_deep_nested)[d_next].prefix
                          == group_prefix
                   && (*indexed_structured_deep_nested)[d_next].base
                          == group_base) {
                d_next += 1U;
            }
            size_t q_next = q;
            while (q_next < indexed_structured_indexed_nested->size()
                   && (*indexed_structured_indexed_nested)[q_next].prefix
                          == group_prefix
                   && (*indexed_structured_indexed_nested)[q_next].base
                          == group_base) {
                q_next += 1U;
            }

            if (emit_portable_indexed_structured_property_group(
                    w, group_prefix, group_base, arena,
                    std::span<const PortableIndexedStructuredProperty>(
                        indexed_structured->data() + i, i_next - i),
                    std::span<const PortableIndexedStructuredLangAltProperty>(
                        indexed_structured_lang_alt->data() + j, j_next - j),
                    std::span<const PortableIndexedStructuredIndexedProperty>(
                        indexed_structured_indexed->data() + n, n_next - n),
                    std::span<const PortableIndexedStructuredNestedProperty>(
                        indexed_structured_nested->data() + k, k_next - k),
                    std::span<
                        const PortableIndexedStructuredNestedLangAltProperty>(
                        indexed_structured_nested_lang_alt->data() + m,
                        m_next - m),
                    std::span<
                        const PortableIndexedStructuredNestedIndexedProperty>(
                        indexed_structured_nested_indexed->data() + p,
                        p_next - p),
                    std::span<const PortableIndexedStructuredDeepNestedProperty>(
                        indexed_structured_deep_nested->data() + d, d_next - d),
                    std::span<
                        const PortableIndexedStructuredIndexedNestedProperty>(
                        indexed_structured_indexed_nested->data() + q,
                        q_next - q))) {
                *emitted += 1U;
            }

            i = i_next;
            j = j_next;
            k = k_next;
            m = m_next;
            n = n_next;
            p = p_next;
            d = d_next;
            q = q_next;
        }
    }

    enum class PortablePassKind : uint8_t {
        ExistingXmp,
        Exif,
        ExifXmpAlias,
        Iptc,
    };

    static void emit_portable_pass(
        PortablePassKind pass, const ByteArena& arena,
        std::span<const PortableCustomNsDecl> custom_decls,
        std::span<const Entry> entries, const XmpPortableOptions& options,
        const PortablePropertyGeneratedShapeSet* generated_keys,
        const PortableGeneratedLangAltKeySet* generated_lang_alt, SpanWriter* w,
        PortablePropertyClaimMap* claims,
        PortableLangAltClaimOwnerMap* lang_alt_claims,
        PortableStructuredChildClaimMap* structured_child_claims,
        PortableIndexedStructuredChildClaimMap* indexed_structured_child_claims,
        PortableStructuredLangAltClaimOwnerMap* structured_lang_alt_claims,
        PortableIndexedStructuredLangAltClaimOwnerMap*
            indexed_structured_lang_alt_claims,
        PortableStructuredNestedChildClaimMap* structured_nested_child_claims,
        PortableIndexedStructuredNestedChildClaimMap*
            indexed_structured_nested_child_claims,
        PortableStructuredNestedClaimOwnerMap* structured_nested_claims,
        PortableStructuredNestedLangAltClaimOwnerMap*
            structured_nested_lang_alt_claims,
        PortableIndexedStructuredNestedClaimOwnerMap*
            indexed_structured_nested_claims,
        PortableIndexedStructuredNestedLangAltClaimOwnerMap*
            indexed_structured_nested_lang_alt_claims,
        PortableIndexedStructuredDeepNestedClaimOwnerMap*
            indexed_structured_deep_nested_claims,
        PortableIndexedStructuredIndexedNestedChildClaimMap*
            indexed_structured_indexed_nested_child_claims,
        PortableIndexedStructuredIndexedNestedClaimOwnerMap*
            indexed_structured_indexed_nested_claims,
        std::vector<PortableIndexedProperty>* indexed,
        std::vector<PortableLangAltProperty>* lang_alt,
        std::vector<PortableStructuredProperty>* structured,
        std::vector<PortableStructuredLangAltProperty>* structured_lang_alt,
        std::vector<PortableStructuredIndexedProperty>* structured_indexed,
        std::vector<PortableStructuredNestedProperty>* structured_nested,
        std::vector<PortableStructuredNestedLangAltProperty>*
            structured_nested_lang_alt,
        std::vector<PortableStructuredNestedIndexedProperty>*
            structured_nested_indexed,
        std::vector<PortableIndexedStructuredProperty>* indexed_structured,
        std::vector<PortableIndexedStructuredLangAltProperty>*
            indexed_structured_lang_alt,
        std::vector<PortableIndexedStructuredNestedProperty>*
            indexed_structured_nested,
        std::vector<PortableIndexedStructuredNestedLangAltProperty>*
            indexed_structured_nested_lang_alt,
        std::vector<PortableIndexedStructuredNestedIndexedProperty>*
            indexed_structured_nested_indexed,
        std::vector<PortableIndexedStructuredDeepNestedProperty>*
            indexed_structured_deep_nested,
        std::vector<PortableIndexedStructuredIndexedNestedProperty>*
            indexed_structured_indexed_nested,
        std::vector<PortableIndexedStructuredIndexedProperty>*
            indexed_structured_indexed,
        uint32_t* emitted, uint32_t* iptc_order) noexcept
    {
        if (!w || !claims || !lang_alt_claims || !indexed || !lang_alt
            || !structured_child_claims || !indexed_structured_child_claims
            || !structured_lang_alt_claims
            || !indexed_structured_lang_alt_claims
            || !structured_nested_child_claims
            || !indexed_structured_nested_child_claims
            || !structured_nested_claims || !structured_nested_lang_alt_claims
            || !indexed_structured_nested_claims
            || !indexed_structured_nested_lang_alt_claims
            || !indexed_structured_deep_nested_claims
            || !indexed_structured_indexed_nested_child_claims
            || !indexed_structured_indexed_nested_claims || !structured
            || !structured_lang_alt || !structured_indexed || !structured_nested
            || !structured_nested_lang_alt || !structured_nested_indexed
            || !indexed_structured || !indexed_structured_lang_alt
            || !indexed_structured_nested || !indexed_structured_nested_lang_alt
            || !indexed_structured_nested_indexed
            || !indexed_structured_deep_nested
            || !indexed_structured_indexed_nested || !indexed_structured_indexed
            || !emitted || !iptc_order || w->limit_hit) {
            return;
        }

        if (pass == PortablePassKind::Iptc && options.include_iptc) {
            if (options.limits.max_entries != 0U
                && *emitted >= options.limits.max_entries) {
                w->limit_hit = true;
                return;
            }
            if (emit_portable_iptc_datetime_property(arena, entries, 55U, 60U,
                                                     "photoshop", "DateCreated",
                                                     w, claims)) {
                *emitted += 1U;
            }

            if (options.limits.max_entries != 0U
                && *emitted >= options.limits.max_entries) {
                w->limit_hit = true;
                return;
            }
            if (emit_portable_iptc_datetime_property(arena, entries, 62U, 63U,
                                                     "xmp", "CreateDate", w,
                                                     claims)) {
                *emitted += 1U;
            }
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            if (options.limits.max_entries != 0U
                && *emitted >= options.limits.max_entries) {
                w->limit_hit = true;
                break;
            }

            const Entry& e = entries[i];
            if (any(e.flags, EntryFlags::Deleted)) {
                continue;
            }

            if (pass == PortablePassKind::Exif) {
                if (!options.include_exif
                    || e.key.kind != MetaKeyKind::ExifTag) {
                    continue;
                }
                if (process_portable_exif_entry(
                        arena, entries, e, options.exiftool_gpsdatetime_alias,
                        w, claims)) {
                    *emitted += 1U;
                }
                continue;
            }

            if (pass == PortablePassKind::ExifXmpAlias) {
                if (!options.include_exif
                    || e.key.kind != MetaKeyKind::ExifTag) {
                    continue;
                }
                if (process_portable_exif_xmp_alias_entry(arena, entries, e, w,
                                                          claims)) {
                    *emitted += 1U;
                }
                continue;
            }

            if (pass == PortablePassKind::ExistingXmp) {
                if (!options.include_existing_xmp
                    || e.key.kind != MetaKeyKind::XmpProperty) {
                    continue;
                }
                if (process_portable_existing_xmp_entry(
                        arena, custom_decls, entries, options, e,
                        static_cast<uint32_t>(i), generated_keys,
                        generated_lang_alt, w, claims, lang_alt_claims,
                        structured_child_claims,
                        indexed_structured_child_claims,
                        structured_lang_alt_claims,
                        indexed_structured_lang_alt_claims,
                        structured_nested_child_claims,
                        indexed_structured_nested_child_claims,
                        structured_nested_claims,
                        structured_nested_lang_alt_claims,
                        indexed_structured_nested_claims,
                        indexed_structured_nested_lang_alt_claims,
                        indexed_structured_deep_nested_claims,
                        indexed_structured_indexed_nested_child_claims,
                        indexed_structured_indexed_nested_claims, indexed,
                        lang_alt, structured, structured_lang_alt,
                        structured_indexed, structured_nested,
                        structured_nested_lang_alt, structured_nested_indexed,
                        indexed_structured, indexed_structured_lang_alt,
                        indexed_structured_nested,
                        indexed_structured_nested_lang_alt,
                        indexed_structured_nested_indexed,
                        indexed_structured_deep_nested,
                        indexed_structured_indexed_nested,
                        indexed_structured_indexed)) {
                    *emitted += 1U;
                }
                continue;
            }

            if (!options.include_iptc
                || e.key.kind != MetaKeyKind::IptcDataset) {
                continue;
            }
            if (process_portable_iptc_entry(arena, e, *iptc_order, w, claims,
                                            lang_alt_claims,
                                            structured_child_claims, indexed,
                                            lang_alt, structured)) {
                *emitted += 1U;
            }
            *iptc_order += 1U;
        }
    }

}  // namespace


XmpDumpResult
dump_xmp_portable(const MetaStore& store, std::span<std::byte> out,
                  const XmpPortableOptions& options) noexcept
{
    XmpDumpResult r;
    SpanWriter w(out, options.limits.max_output_bytes);

    static constexpr std::array<XmpNsDecl, 12> kDecls = {
        XmpNsDecl { "xmp", kXmpNsXmp },
        XmpNsDecl { "tiff", kXmpNsTiff },
        XmpNsDecl { "exif", kXmpNsExif },
        XmpNsDecl { "aux", kXmpNsExifAux },
        XmpNsDecl { "dc", kXmpNsDc },
        XmpNsDecl { "pdf", kXmpNsPdf },
        XmpNsDecl { "crs", kXmpNsCrs },
        XmpNsDecl { "lr", kXmpNsLr },
        XmpNsDecl { "xmpMM", kXmpNsXmpMM },
        XmpNsDecl { "xmpRights", kXmpNsXmpRights },
        XmpNsDecl { "photoshop", kXmpNsPhotoshop },
        XmpNsDecl { "Iptc4xmpCore", kXmpNsIptc4xmpCore },
    };
    const ByteArena& arena          = store.arena();
    const std::span<const Entry> es = store.entries();

    std::vector<PortableCustomNsDecl> custom_decls;
    collect_portable_custom_ns_decls(arena, es, options, &custom_decls);

    std::vector<XmpNsDecl> decls;
    decls.reserve(kDecls.size() + custom_decls.size());
    for (size_t i = 0; i < kDecls.size(); ++i) {
        decls.push_back(kDecls[i]);
    }
    if (existing_xmp_namespace_is_used(arena, es, options, kXmpNsPlus)) {
        decls.push_back(XmpNsDecl { "plus", kXmpNsPlus });
    }
    if (existing_xmp_namespace_is_used(arena, es, options, kXmpNsDng)) {
        decls.push_back(XmpNsDecl { "dng", kXmpNsDng });
    }
    if (existing_xmp_namespace_is_used(arena, es, options, kXmpNsXmpBJ)) {
        decls.push_back(XmpNsDecl { "xmpBJ", kXmpNsXmpBJ });
    }
    if (existing_xmp_namespace_is_used(arena, es, options, kXmpNsXmpDM)) {
        decls.push_back(XmpNsDecl { "xmpDM", kXmpNsXmpDM });
    }
    if (existing_xmp_namespace_is_used(arena, es, options, kXmpNsIptc4xmpExt)) {
        decls.push_back(XmpNsDecl { "Iptc4xmpExt", kXmpNsIptc4xmpExt });
    }
    if (existing_xmp_namespace_is_used(arena, es, options, kXmpNsXmpTPg)) {
        decls.push_back(XmpNsDecl { "xmpTPg", kXmpNsXmpTPg });
    }
    if (existing_xmp_child_prefix_is_used(arena, es, options, "stDim")) {
        decls.push_back(XmpNsDecl { "stDim", kXmpNsStDim });
    }
    if (existing_xmp_child_prefix_is_used(arena, es, options, "stEvt")) {
        decls.push_back(XmpNsDecl { "stEvt", kXmpNsStEvt });
    }
    if (existing_xmp_child_prefix_is_used(arena, es, options, "stFnt")) {
        decls.push_back(XmpNsDecl { "stFnt", kXmpNsStFnt });
    }
    if (existing_xmp_child_prefix_is_used(arena, es, options, "stJob")) {
        decls.push_back(XmpNsDecl { "stJob", kXmpNsStJob });
    }
    if (existing_xmp_child_prefix_is_used(arena, es, options, "stMfs")) {
        decls.push_back(XmpNsDecl { "stMfs", kXmpNsStMfs });
    }
    if (existing_xmp_child_prefix_is_used(arena, es, options, "stRef")) {
        decls.push_back(XmpNsDecl { "stRef", kXmpNsStRef });
    }
    if (existing_xmp_child_prefix_is_used(arena, es, options, "stVer")) {
        decls.push_back(XmpNsDecl { "stVer", kXmpNsStVer });
    }
    if (existing_xmp_child_prefix_is_used(arena, es, options, "xmpG")) {
        decls.push_back(XmpNsDecl { "xmpG", kXmpNsXmpG });
    }
    for (size_t i = 0; i < custom_decls.size(); ++i) {
        decls.push_back(
            XmpNsDecl { custom_decls[i].prefix, custom_decls[i].uri });
    }
    emit_xmp_packet_begin(&w, std::span<const XmpNsDecl>(decls.data(),
                                                         decls.size()));

    std::vector<PortableIndexedProperty> indexed;
    indexed.reserve(128);
    std::vector<PortableLangAltProperty> lang_alt;
    lang_alt.reserve(64);
    std::vector<PortableStructuredProperty> structured;
    structured.reserve(64);
    std::vector<PortableStructuredLangAltProperty> structured_lang_alt;
    structured_lang_alt.reserve(64);
    std::vector<PortableStructuredIndexedProperty> structured_indexed;
    structured_indexed.reserve(64);
    std::vector<PortableStructuredNestedProperty> structured_nested;
    structured_nested.reserve(64);
    std::vector<PortableStructuredNestedLangAltProperty>
        structured_nested_lang_alt;
    structured_nested_lang_alt.reserve(64);
    std::vector<PortableStructuredNestedIndexedProperty>
        structured_nested_indexed;
    structured_nested_indexed.reserve(64);
    std::vector<PortableIndexedStructuredProperty> indexed_structured;
    indexed_structured.reserve(64);
    std::vector<PortableIndexedStructuredLangAltProperty>
        indexed_structured_lang_alt;
    indexed_structured_lang_alt.reserve(64);
    std::vector<PortableIndexedStructuredNestedProperty>
        indexed_structured_nested;
    indexed_structured_nested.reserve(64);
    std::vector<PortableIndexedStructuredNestedLangAltProperty>
        indexed_structured_nested_lang_alt;
    indexed_structured_nested_lang_alt.reserve(64);
    std::vector<PortableIndexedStructuredNestedIndexedProperty>
        indexed_structured_nested_indexed;
    indexed_structured_nested_indexed.reserve(64);
    std::vector<PortableIndexedStructuredDeepNestedProperty>
        indexed_structured_deep_nested;
    indexed_structured_deep_nested.reserve(64);
    std::vector<PortableIndexedStructuredIndexedNestedProperty>
        indexed_structured_indexed_nested;
    indexed_structured_indexed_nested.reserve(64);
    std::vector<PortableIndexedStructuredIndexedProperty>
        indexed_structured_indexed;
    indexed_structured_indexed.reserve(64);
    PortablePropertyClaimMap claims;
    claims.reserve(256);
    PortableLangAltClaimOwnerMap lang_alt_claims;
    lang_alt_claims.reserve(128);
    PortableStructuredChildClaimMap structured_child_claims;
    structured_child_claims.reserve(128);
    PortableIndexedStructuredChildClaimMap indexed_structured_child_claims;
    indexed_structured_child_claims.reserve(128);
    PortableStructuredLangAltClaimOwnerMap structured_lang_alt_claims;
    structured_lang_alt_claims.reserve(128);
    PortableIndexedStructuredLangAltClaimOwnerMap
        indexed_structured_lang_alt_claims;
    indexed_structured_lang_alt_claims.reserve(128);
    PortableStructuredNestedChildClaimMap structured_nested_child_claims;
    structured_nested_child_claims.reserve(128);
    PortableIndexedStructuredNestedChildClaimMap
        indexed_structured_nested_child_claims;
    indexed_structured_nested_child_claims.reserve(128);
    PortableStructuredNestedClaimOwnerMap structured_nested_claims;
    structured_nested_claims.reserve(128);
    PortableStructuredNestedLangAltClaimOwnerMap
        structured_nested_lang_alt_claims;
    structured_nested_lang_alt_claims.reserve(128);
    PortableIndexedStructuredNestedClaimOwnerMap indexed_structured_nested_claims;
    indexed_structured_nested_claims.reserve(128);
    PortableIndexedStructuredNestedLangAltClaimOwnerMap
        indexed_structured_nested_lang_alt_claims;
    indexed_structured_nested_lang_alt_claims.reserve(128);
    PortableIndexedStructuredDeepNestedClaimOwnerMap
        indexed_structured_deep_nested_claims;
    indexed_structured_deep_nested_claims.reserve(128);
    PortableIndexedStructuredIndexedNestedChildClaimMap
        indexed_structured_indexed_nested_child_claims;
    indexed_structured_indexed_nested_child_claims.reserve(128);
    PortableIndexedStructuredIndexedNestedClaimOwnerMap
        indexed_structured_indexed_nested_claims;
    indexed_structured_indexed_nested_claims.reserve(128);
    PortablePropertyGeneratedShapeSet generated_keys;
    PortableGeneratedLangAltKeySet generated_lang_alt;
    if (options.include_existing_xmp
        && options.existing_standard_namespace_policy
               == XmpExistingStandardNamespacePolicy::CanonicalizeManaged) {
        generated_keys.reserve(256);
        generated_lang_alt.reserve(64);
        collect_generated_portable_property_keys(arena, es, options,
                                                 &generated_keys,
                                                 &generated_lang_alt);
    }

    uint32_t emitted    = 0;
    uint32_t iptc_order = 0U;

    if (options.conflict_policy == XmpConflictPolicy::ExistingWins) {
        emit_portable_pass(
            PortablePassKind::ExistingXmp, arena,
            std::span<const PortableCustomNsDecl>(custom_decls.data(),
                                                  custom_decls.size()),
            es, options, &generated_keys, &generated_lang_alt, &w, &claims,
            &lang_alt_claims, &structured_child_claims,
            &indexed_structured_child_claims, &structured_lang_alt_claims,
            &indexed_structured_lang_alt_claims,
            &structured_nested_child_claims,
            &indexed_structured_nested_child_claims, &structured_nested_claims,
            &structured_nested_lang_alt_claims,
            &indexed_structured_nested_claims,
            &indexed_structured_nested_lang_alt_claims,
            &indexed_structured_deep_nested_claims,
            &indexed_structured_indexed_nested_child_claims,
            &indexed_structured_indexed_nested_claims, &indexed, &lang_alt,
            &structured, &structured_lang_alt, &structured_indexed,
            &structured_nested, &structured_nested_lang_alt,
            &structured_nested_indexed, &indexed_structured,
            &indexed_structured_lang_alt, &indexed_structured_nested,
            &indexed_structured_nested_lang_alt,
            &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
            &indexed_structured_indexed_nested, &indexed_structured_indexed,
            &emitted, &iptc_order);
        emit_portable_pass(
            PortablePassKind::Exif, arena,
            std::span<const PortableCustomNsDecl>(custom_decls.data(),
                                                  custom_decls.size()),
            es, options, &generated_keys, &generated_lang_alt, &w, &claims,
            &lang_alt_claims, &structured_child_claims,
            &indexed_structured_child_claims, &structured_lang_alt_claims,
            &indexed_structured_lang_alt_claims,
            &structured_nested_child_claims,
            &indexed_structured_nested_child_claims, &structured_nested_claims,
            &structured_nested_lang_alt_claims,
            &indexed_structured_nested_claims,
            &indexed_structured_nested_lang_alt_claims,
            &indexed_structured_deep_nested_claims,
            &indexed_structured_indexed_nested_child_claims,
            &indexed_structured_indexed_nested_claims, &indexed, &lang_alt,
            &structured, &structured_lang_alt, &structured_indexed,
            &structured_nested, &structured_nested_lang_alt,
            &structured_nested_indexed, &indexed_structured,
            &indexed_structured_lang_alt, &indexed_structured_nested,
            &indexed_structured_nested_lang_alt,
            &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
            &indexed_structured_indexed_nested, &indexed_structured_indexed,
            &emitted, &iptc_order);
        emit_portable_pass(
            PortablePassKind::ExifXmpAlias, arena,
            std::span<const PortableCustomNsDecl>(custom_decls.data(),
                                                  custom_decls.size()),
            es, options, &generated_keys, &generated_lang_alt, &w, &claims,
            &lang_alt_claims, &structured_child_claims,
            &indexed_structured_child_claims, &structured_lang_alt_claims,
            &indexed_structured_lang_alt_claims,
            &structured_nested_child_claims,
            &indexed_structured_nested_child_claims, &structured_nested_claims,
            &structured_nested_lang_alt_claims,
            &indexed_structured_nested_claims,
            &indexed_structured_nested_lang_alt_claims,
            &indexed_structured_deep_nested_claims,
            &indexed_structured_indexed_nested_child_claims,
            &indexed_structured_indexed_nested_claims, &indexed, &lang_alt,
            &structured, &structured_lang_alt, &structured_indexed,
            &structured_nested, &structured_nested_lang_alt,
            &structured_nested_indexed, &indexed_structured,
            &indexed_structured_lang_alt, &indexed_structured_nested,
            &indexed_structured_nested_lang_alt,
            &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
            &indexed_structured_indexed_nested, &indexed_structured_indexed,
            &emitted, &iptc_order);
        emit_portable_pass(
            PortablePassKind::Iptc, arena,
            std::span<const PortableCustomNsDecl>(custom_decls.data(),
                                                  custom_decls.size()),
            es, options, &generated_keys, &generated_lang_alt, &w, &claims,
            &lang_alt_claims, &structured_child_claims,
            &indexed_structured_child_claims, &structured_lang_alt_claims,
            &indexed_structured_lang_alt_claims,
            &structured_nested_child_claims,
            &indexed_structured_nested_child_claims, &structured_nested_claims,
            &structured_nested_lang_alt_claims,
            &indexed_structured_nested_claims,
            &indexed_structured_nested_lang_alt_claims,
            &indexed_structured_deep_nested_claims,
            &indexed_structured_indexed_nested_child_claims,
            &indexed_structured_indexed_nested_claims, &indexed, &lang_alt,
            &structured, &structured_lang_alt, &structured_indexed,
            &structured_nested, &structured_nested_lang_alt,
            &structured_nested_indexed, &indexed_structured,
            &indexed_structured_lang_alt, &indexed_structured_nested,
            &indexed_structured_nested_lang_alt,
            &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
            &indexed_structured_indexed_nested, &indexed_structured_indexed,
            &emitted, &iptc_order);
    } else if (options.conflict_policy == XmpConflictPolicy::GeneratedWins) {
        emit_portable_pass(
            PortablePassKind::Exif, arena,
            std::span<const PortableCustomNsDecl>(custom_decls.data(),
                                                  custom_decls.size()),
            es, options, &generated_keys, &generated_lang_alt, &w, &claims,
            &lang_alt_claims, &structured_child_claims,
            &indexed_structured_child_claims, &structured_lang_alt_claims,
            &indexed_structured_lang_alt_claims,
            &structured_nested_child_claims,
            &indexed_structured_nested_child_claims, &structured_nested_claims,
            &structured_nested_lang_alt_claims,
            &indexed_structured_nested_claims,
            &indexed_structured_nested_lang_alt_claims,
            &indexed_structured_deep_nested_claims,
            &indexed_structured_indexed_nested_child_claims,
            &indexed_structured_indexed_nested_claims, &indexed, &lang_alt,
            &structured, &structured_lang_alt, &structured_indexed,
            &structured_nested, &structured_nested_lang_alt,
            &structured_nested_indexed, &indexed_structured,
            &indexed_structured_lang_alt, &indexed_structured_nested,
            &indexed_structured_nested_lang_alt,
            &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
            &indexed_structured_indexed_nested, &indexed_structured_indexed,
            &emitted, &iptc_order);
        emit_portable_pass(
            PortablePassKind::ExifXmpAlias, arena,
            std::span<const PortableCustomNsDecl>(custom_decls.data(),
                                                  custom_decls.size()),
            es, options, &generated_keys, &generated_lang_alt, &w, &claims,
            &lang_alt_claims, &structured_child_claims,
            &indexed_structured_child_claims, &structured_lang_alt_claims,
            &indexed_structured_lang_alt_claims,
            &structured_nested_child_claims,
            &indexed_structured_nested_child_claims, &structured_nested_claims,
            &structured_nested_lang_alt_claims,
            &indexed_structured_nested_claims,
            &indexed_structured_nested_lang_alt_claims,
            &indexed_structured_deep_nested_claims,
            &indexed_structured_indexed_nested_child_claims,
            &indexed_structured_indexed_nested_claims, &indexed, &lang_alt,
            &structured, &structured_lang_alt, &structured_indexed,
            &structured_nested, &structured_nested_lang_alt,
            &structured_nested_indexed, &indexed_structured,
            &indexed_structured_lang_alt, &indexed_structured_nested,
            &indexed_structured_nested_lang_alt,
            &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
            &indexed_structured_indexed_nested, &indexed_structured_indexed,
            &emitted, &iptc_order);
        emit_portable_pass(
            PortablePassKind::Iptc, arena,
            std::span<const PortableCustomNsDecl>(custom_decls.data(),
                                                  custom_decls.size()),
            es, options, &generated_keys, &generated_lang_alt, &w, &claims,
            &lang_alt_claims, &structured_child_claims,
            &indexed_structured_child_claims, &structured_lang_alt_claims,
            &indexed_structured_lang_alt_claims,
            &structured_nested_child_claims,
            &indexed_structured_nested_child_claims, &structured_nested_claims,
            &structured_nested_lang_alt_claims,
            &indexed_structured_nested_claims,
            &indexed_structured_nested_lang_alt_claims,
            &indexed_structured_deep_nested_claims,
            &indexed_structured_indexed_nested_child_claims,
            &indexed_structured_indexed_nested_claims, &indexed, &lang_alt,
            &structured, &structured_lang_alt, &structured_indexed,
            &structured_nested, &structured_nested_lang_alt,
            &structured_nested_indexed, &indexed_structured,
            &indexed_structured_lang_alt, &indexed_structured_nested,
            &indexed_structured_nested_lang_alt,
            &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
            &indexed_structured_indexed_nested, &indexed_structured_indexed,
            &emitted, &iptc_order);
        emit_portable_pass(
            PortablePassKind::ExistingXmp, arena,
            std::span<const PortableCustomNsDecl>(custom_decls.data(),
                                                  custom_decls.size()),
            es, options, &generated_keys, &generated_lang_alt, &w, &claims,
            &lang_alt_claims, &structured_child_claims,
            &indexed_structured_child_claims, &structured_lang_alt_claims,
            &indexed_structured_lang_alt_claims,
            &structured_nested_child_claims,
            &indexed_structured_nested_child_claims, &structured_nested_claims,
            &structured_nested_lang_alt_claims,
            &indexed_structured_nested_claims,
            &indexed_structured_nested_lang_alt_claims,
            &indexed_structured_deep_nested_claims,
            &indexed_structured_indexed_nested_child_claims,
            &indexed_structured_indexed_nested_claims, &indexed, &lang_alt,
            &structured, &structured_lang_alt, &structured_indexed,
            &structured_nested, &structured_nested_lang_alt,
            &structured_nested_indexed, &indexed_structured,
            &indexed_structured_lang_alt, &indexed_structured_nested,
            &indexed_structured_nested_lang_alt,
            &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
            &indexed_structured_indexed_nested, &indexed_structured_indexed,
            &emitted, &iptc_order);
    } else {
        emit_portable_pass(
            PortablePassKind::Exif, arena,
            std::span<const PortableCustomNsDecl>(custom_decls.data(),
                                                  custom_decls.size()),
            es, options, &generated_keys, &generated_lang_alt, &w, &claims,
            &lang_alt_claims, &structured_child_claims,
            &indexed_structured_child_claims, &structured_lang_alt_claims,
            &indexed_structured_lang_alt_claims,
            &structured_nested_child_claims,
            &indexed_structured_nested_child_claims, &structured_nested_claims,
            &structured_nested_lang_alt_claims,
            &indexed_structured_nested_claims,
            &indexed_structured_nested_lang_alt_claims,
            &indexed_structured_deep_nested_claims,
            &indexed_structured_indexed_nested_child_claims,
            &indexed_structured_indexed_nested_claims, &indexed, &lang_alt,
            &structured, &structured_lang_alt, &structured_indexed,
            &structured_nested, &structured_nested_lang_alt,
            &structured_nested_indexed, &indexed_structured,
            &indexed_structured_lang_alt, &indexed_structured_nested,
            &indexed_structured_nested_lang_alt,
            &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
            &indexed_structured_indexed_nested, &indexed_structured_indexed,
            &emitted, &iptc_order);
        emit_portable_pass(
            PortablePassKind::ExifXmpAlias, arena,
            std::span<const PortableCustomNsDecl>(custom_decls.data(),
                                                  custom_decls.size()),
            es, options, &generated_keys, &generated_lang_alt, &w, &claims,
            &lang_alt_claims, &structured_child_claims,
            &indexed_structured_child_claims, &structured_lang_alt_claims,
            &indexed_structured_lang_alt_claims,
            &structured_nested_child_claims,
            &indexed_structured_nested_child_claims, &structured_nested_claims,
            &structured_nested_lang_alt_claims,
            &indexed_structured_nested_claims,
            &indexed_structured_nested_lang_alt_claims,
            &indexed_structured_deep_nested_claims,
            &indexed_structured_indexed_nested_child_claims,
            &indexed_structured_indexed_nested_claims, &indexed, &lang_alt,
            &structured, &structured_lang_alt, &structured_indexed,
            &structured_nested, &structured_nested_lang_alt,
            &structured_nested_indexed, &indexed_structured,
            &indexed_structured_lang_alt, &indexed_structured_nested,
            &indexed_structured_nested_lang_alt,
            &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
            &indexed_structured_indexed_nested, &indexed_structured_indexed,
            &emitted, &iptc_order);
        emit_portable_pass(
            PortablePassKind::ExistingXmp, arena,
            std::span<const PortableCustomNsDecl>(custom_decls.data(),
                                                  custom_decls.size()),
            es, options, &generated_keys, &generated_lang_alt, &w, &claims,
            &lang_alt_claims, &structured_child_claims,
            &indexed_structured_child_claims, &structured_lang_alt_claims,
            &indexed_structured_lang_alt_claims,
            &structured_nested_child_claims,
            &indexed_structured_nested_child_claims, &structured_nested_claims,
            &structured_nested_lang_alt_claims,
            &indexed_structured_nested_claims,
            &indexed_structured_nested_lang_alt_claims,
            &indexed_structured_deep_nested_claims,
            &indexed_structured_indexed_nested_child_claims,
            &indexed_structured_indexed_nested_claims, &indexed, &lang_alt,
            &structured, &structured_lang_alt, &structured_indexed,
            &structured_nested, &structured_nested_lang_alt,
            &structured_nested_indexed, &indexed_structured,
            &indexed_structured_lang_alt, &indexed_structured_nested,
            &indexed_structured_nested_lang_alt,
            &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
            &indexed_structured_indexed_nested, &indexed_structured_indexed,
            &emitted, &iptc_order);
        emit_portable_pass(
            PortablePassKind::Iptc, arena,
            std::span<const PortableCustomNsDecl>(custom_decls.data(),
                                                  custom_decls.size()),
            es, options, &generated_keys, &generated_lang_alt, &w, &claims,
            &lang_alt_claims, &structured_child_claims,
            &indexed_structured_child_claims, &structured_lang_alt_claims,
            &indexed_structured_lang_alt_claims,
            &structured_nested_child_claims,
            &indexed_structured_nested_child_claims, &structured_nested_claims,
            &structured_nested_lang_alt_claims,
            &indexed_structured_nested_claims,
            &indexed_structured_nested_lang_alt_claims,
            &indexed_structured_deep_nested_claims,
            &indexed_structured_indexed_nested_child_claims,
            &indexed_structured_indexed_nested_claims, &indexed, &lang_alt,
            &structured, &structured_lang_alt, &structured_indexed,
            &structured_nested, &structured_nested_lang_alt,
            &structured_nested_indexed, &indexed_structured,
            &indexed_structured_lang_alt, &indexed_structured_nested,
            &indexed_structured_nested_lang_alt,
            &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
            &indexed_structured_indexed_nested, &indexed_structured_indexed,
            &emitted, &iptc_order);
    }

    emit_portable_lang_alt_groups(&w, arena, &lang_alt,
                                  options.limits.max_entries, &emitted);
    emit_portable_indexed_groups(&w, arena, &indexed,
                                 options.limits.max_entries, &emitted);
    emit_portable_structured_groups(&w, arena, &structured,
                                    &structured_lang_alt, &structured_indexed,
                                    &structured_nested,
                                    &structured_nested_lang_alt,
                                    &structured_nested_indexed,
                                    options.limits.max_entries, &emitted);
    emit_portable_indexed_structured_groups(
        &w, arena, &indexed_structured, &indexed_structured_lang_alt,
        &indexed_structured_nested, &indexed_structured_nested_lang_alt,
        &indexed_structured_nested_indexed, &indexed_structured_deep_nested,
        &indexed_structured_indexed_nested, &indexed_structured_indexed,
        options.limits.max_entries, &emitted);

    emit_xmp_packet_end(&w);

    r.entries = emitted;
    if (w.limit_hit) {
        r.status = XmpDumpStatus::LimitExceeded;
    } else if (w.needed > static_cast<uint64_t>(out.size())) {
        r.status = XmpDumpStatus::OutputTruncated;
    } else {
        r.status = XmpDumpStatus::Ok;
    }

    r.written = (w.written < w.needed) ? w.written : w.needed;
    r.needed  = w.needed;
    return r;
}


XmpDumpResult
dump_xmp_sidecar(const MetaStore& store, std::vector<std::byte>* out,
                 const XmpSidecarOptions& options) noexcept
{
    XmpDumpResult r;
    if (!out) {
        r.status = XmpDumpStatus::LimitExceeded;
        return r;
    }

    uint64_t initial_bytes = options.initial_output_bytes;
    if (initial_bytes == 0U) {
        initial_bytes = 1024ULL * 1024ULL;
    }
    if (initial_bytes > kMaxInitialSidecarOutputBytes) {
        initial_bytes = kMaxInitialSidecarOutputBytes;
    }
    if (options.format == XmpSidecarFormat::Portable
        && options.portable.limits.max_output_bytes != 0U
        && initial_bytes > options.portable.limits.max_output_bytes) {
        initial_bytes = options.portable.limits.max_output_bytes;
    }
    if (options.format == XmpSidecarFormat::Lossless
        && options.lossless.limits.max_output_bytes != 0U
        && initial_bytes > options.lossless.limits.max_output_bytes) {
        initial_bytes = options.lossless.limits.max_output_bytes;
    }
    if (initial_bytes > static_cast<uint64_t>(SIZE_MAX)) {
        r.status = XmpDumpStatus::LimitExceeded;
        return r;
    }
    out->assign(static_cast<size_t>(initial_bytes), std::byte { 0 });

    for (;;) {
        const std::span<std::byte> span(out->data(), out->size());
        if (options.format == XmpSidecarFormat::Portable) {
            r = dump_xmp_portable(store, span, options.portable);
        } else {
            r = dump_xmp_lossless(store, span, options.lossless);
        }

        if (r.status == XmpDumpStatus::OutputTruncated
            && r.needed > out->size()) {
            if (r.needed > static_cast<uint64_t>(SIZE_MAX)) {
                r.status = XmpDumpStatus::LimitExceeded;
                return r;
            }
            out->resize(static_cast<size_t>(r.needed));
            continue;
        }
        break;
    }

    if (r.status == XmpDumpStatus::Ok && r.written <= out->size()) {
        out->resize(static_cast<size_t>(r.written));
    }
    return r;
}


XmpSidecarOptions
make_xmp_sidecar_options(const XmpSidecarRequest& request) noexcept
{
    XmpSidecarOptions options;
    options.format               = request.format;
    options.initial_output_bytes = request.initial_output_bytes;

    options.portable.limits               = request.limits;
    options.portable.include_exif         = request.include_exif;
    options.portable.include_iptc         = request.include_iptc;
    options.portable.include_existing_xmp = request.include_existing_xmp;
    options.portable.existing_namespace_policy
        = request.portable_existing_namespace_policy;
    options.portable.existing_standard_namespace_policy
        = request.portable_existing_standard_namespace_policy;
    options.portable.conflict_policy = request.portable_conflict_policy;
    options.portable.exiftool_gpsdatetime_alias
        = request.portable_exiftool_gpsdatetime_alias;

    options.lossless.limits         = request.limits;
    options.lossless.include_origin = request.include_origin;
    options.lossless.include_wire   = request.include_wire;
    options.lossless.include_flags  = request.include_flags;
    options.lossless.include_names  = request.include_names;
    return options;
}


XmpDumpResult
dump_xmp_sidecar(const MetaStore& store, std::vector<std::byte>* out,
                 const XmpSidecarRequest& request) noexcept
{
    const XmpSidecarOptions options = make_xmp_sidecar_options(request);
    return dump_xmp_sidecar(store, out, options);
}

}  // namespace openmeta
