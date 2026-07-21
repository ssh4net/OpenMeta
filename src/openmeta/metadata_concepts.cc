// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_concepts.h"

#include "openmeta/byte_arena.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/exif_value_names.h"
#include "openmeta/meta_flags.h"
#include "openmeta/metadata_interpretation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static constexpr uint16_t kExifOrientationTag               = 0x0112U;
    static constexpr uint16_t kExifDateTimeTag                  = 0x0132U;
    static constexpr uint16_t kExifExposureTimeTag              = 0x829AU;
    static constexpr uint16_t kExifFNumberTag                   = 0x829DU;
    static constexpr uint16_t kExifCopyrightTag                 = 0x8298U;
    static constexpr uint16_t kExifExposureProgramTag           = 0x8822U;
    static constexpr uint16_t kExifPhotographicSensitivityTag   = 0x8827U;
    static constexpr uint16_t kExifDateTimeOriginalTag          = 0x9003U;
    static constexpr uint16_t kExifDateTimeDigitizedTag         = 0x9004U;
    static constexpr uint16_t kExifOffsetTimeTag                = 0x9010U;
    static constexpr uint16_t kExifOffsetTimeOriginalTag        = 0x9011U;
    static constexpr uint16_t kExifOffsetTimeDigitizedTag       = 0x9012U;
    static constexpr uint16_t kExifShutterSpeedValueTag         = 0x9201U;
    static constexpr uint16_t kExifApertureValueTag             = 0x9202U;
    static constexpr uint16_t kExifExposureBiasValueTag         = 0x9204U;
    static constexpr uint16_t kExifMaxApertureValueTag          = 0x9205U;
    static constexpr uint16_t kExifExposureIndexTag             = 0x9215U;
    static constexpr uint16_t kExifSubSecTimeTag                = 0x9290U;
    static constexpr uint16_t kExifSubSecTimeOriginalTag        = 0x9291U;
    static constexpr uint16_t kExifSubSecTimeDigitizedTag       = 0x9292U;
    static constexpr uint16_t kExifColorSpaceTag                = 0xA001U;
    static constexpr uint16_t kExifGainControlTag               = 0xA407U;
    static constexpr uint16_t kExifDocumentNameTag              = 0x010DU;
    static constexpr uint16_t kExifImageDescriptionTag          = 0x010EU;
    static constexpr uint16_t kExifArtistTag                    = 0x013BU;
    static constexpr uint16_t kExifXpTitleTag                   = 0x9C9BU;
    static constexpr uint16_t kExifXpCommentTag                 = 0x9C9CU;
    static constexpr uint16_t kExifXpAuthorTag                  = 0x9C9DU;
    static constexpr uint16_t kExifXpKeywordsTag                = 0x9C9EU;
    static constexpr uint16_t kDngBaselineExposureTag           = 0xC62AU;
    static constexpr uint16_t kDngBaselineExposureOffsetTag     = 0xC7A5U;
    static constexpr uint16_t kDngRawToPreviewGainTag           = 0xC7A8U;
    static constexpr uint16_t kDngProfileGainTableMapTag        = 0xCD2DU;
    static constexpr uint16_t kDngProfileGainTableMap2Tag       = 0xCD40U;
    static constexpr uint16_t kGpsLatitudeRefTag                = 0x0001U;
    static constexpr uint16_t kGpsLatitudeTag                   = 0x0002U;
    static constexpr uint16_t kGpsLongitudeRefTag               = 0x0003U;
    static constexpr uint16_t kGpsLongitudeTag                  = 0x0004U;
    static constexpr uint16_t kGpsAltitudeRefTag                = 0x0005U;
    static constexpr uint16_t kGpsAltitudeTag                   = 0x0006U;
    static constexpr uint16_t kGpsTimeStampTag                  = 0x0007U;
    static constexpr uint16_t kGpsDestLatitudeRefTag            = 0x0013U;
    static constexpr uint16_t kGpsDestLatitudeTag               = 0x0014U;
    static constexpr uint16_t kGpsDestLongitudeRefTag           = 0x0015U;
    static constexpr uint16_t kGpsDestLongitudeTag              = 0x0016U;
    static constexpr uint16_t kGpsDateStampTag                  = 0x001DU;
    static constexpr uint16_t kIptcDateCreatedDataset           = 55U;
    static constexpr uint16_t kIptcTimeCreatedDataset           = 60U;
    static constexpr uint16_t kIptcDigitalCreationDateDataset   = 62U;
    static constexpr uint16_t kIptcDigitalCreationTimeDataset   = 63U;
    static constexpr uint16_t kIptcObjectNameDataset            = 5U;
    static constexpr uint16_t kIptcUrgencyDataset               = 10U;
    static constexpr uint16_t kIptcCategoryDataset              = 15U;
    static constexpr uint16_t kIptcSupplementalCategoryDataset  = 20U;
    static constexpr uint16_t kIptcKeywordsDataset              = 25U;
    static constexpr uint16_t kIptcInstructionsDataset          = 40U;
    static constexpr uint16_t kIptcBylineDataset                = 80U;
    static constexpr uint16_t kIptcBylineTitleDataset           = 85U;
    static constexpr uint16_t kIptcCityDataset                  = 90U;
    static constexpr uint16_t kIptcSublocationDataset           = 92U;
    static constexpr uint16_t kIptcProvinceStateDataset         = 95U;
    static constexpr uint16_t kIptcCountryCodeDataset           = 100U;
    static constexpr uint16_t kIptcCountryNameDataset           = 101U;
    static constexpr uint16_t kIptcTransmissionReferenceDataset = 103U;
    static constexpr uint16_t kIptcHeadlineDataset              = 105U;
    static constexpr uint16_t kIptcCreditDataset                = 110U;
    static constexpr uint16_t kIptcSourceDataset                = 115U;
    static constexpr uint16_t kIptcCopyrightNoticeDataset       = 116U;
    static constexpr uint16_t kIptcCaptionDataset               = 120U;
    static constexpr uint16_t kIptcCaptionWriterDataset         = 122U;
    static constexpr uint32_t kIccHeaderRgbColorSpaceOffset     = 16U;
    static constexpr size_t kMaxDateTimeSubsecondDigits         = 9U;
    static constexpr std::string_view kExifXmpSchema
        = "http://ns.adobe.com/exif/1.0/";
    static constexpr std::string_view kDcXmpSchema
        = "http://purl.org/dc/elements/1.1/";
    static constexpr std::string_view kPhotoshopXmpSchema
        = "http://ns.adobe.com/photoshop/1.0/";
    static constexpr std::string_view kXmpBasicSchema
        = "http://ns.adobe.com/xap/1.0/";
    static constexpr std::string_view kXmpMmSchema
        = "http://ns.adobe.com/xap/1.0/mm/";
    static constexpr std::string_view kIptcCoreXmpSchema
        = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
    static constexpr std::string_view kIptcExtXmpSchema
        = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
    static constexpr std::string_view kXmpRightsSchema
        = "http://ns.adobe.com/xap/1.0/rights/";
    static constexpr std::string_view kPlusXmpSchema
        = "http://ns.useplus.org/ldf/xmp/1.0/";

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static char ascii_lower(char c) noexcept
    {
        if (c >= 'A' && c <= 'Z') {
            return static_cast<char>(c + ('a' - 'A'));
        }
        return c;
    }

    static bool ascii_is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

    static bool ascii_is_alnum(char c) noexcept
    {
        if (c >= '0' && c <= '9') {
            return true;
        }
        if (c >= 'A' && c <= 'Z') {
            return true;
        }
        return c >= 'a' && c <= 'z';
    }

    static bool ascii_equal_ci(std::string_view a, std::string_view b) noexcept
    {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0U; i < a.size(); ++i) {
            if (ascii_lower(a[i]) != ascii_lower(b[i])) {
                return false;
            }
        }
        return true;
    }

    static bool ascii_contains_ci(std::string_view text,
                                  std::string_view needle) noexcept
    {
        if (needle.empty()) {
            return true;
        }
        if (text.size() < needle.size()) {
            return false;
        }
        const size_t limit = text.size() - needle.size();
        for (size_t pos = 0U; pos <= limit; ++pos) {
            bool matched = true;
            for (size_t i = 0U; i < needle.size(); ++i) {
                if (ascii_lower(text[pos + i]) != ascii_lower(needle[i])) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return true;
            }
        }
        return false;
    }

    static size_t ascii_find_ci(std::string_view text,
                                std::string_view needle) noexcept
    {
        if (needle.empty()) {
            return 0U;
        }
        if (text.size() < needle.size()) {
            return std::string_view::npos;
        }
        const size_t limit = text.size() - needle.size();
        for (size_t pos = 0U; pos <= limit; ++pos) {
            bool matched = true;
            for (size_t i = 0U; i < needle.size(); ++i) {
                if (ascii_lower(text[pos + i]) != ascii_lower(needle[i])) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return pos;
            }
        }
        return std::string_view::npos;
    }

    static bool ascii_ends_with_ci(std::string_view text,
                                   std::string_view suffix) noexcept
    {
        if (text.size() < suffix.size()) {
            return false;
        }
        const size_t offset = text.size() - suffix.size();
        for (size_t i = 0U; i < suffix.size(); ++i) {
            if (ascii_lower(text[offset + i]) != ascii_lower(suffix[i])) {
                return false;
            }
        }
        return true;
    }

    static bool ascii_starts_with_ci(std::string_view text,
                                     std::string_view prefix) noexcept
    {
        if (text.size() < prefix.size()) {
            return false;
        }
        for (size_t i = 0U; i < prefix.size(); ++i) {
            if (ascii_lower(text[i]) != ascii_lower(prefix[i])) {
                return false;
            }
        }
        return true;
    }

    static bool xmp_leaf_matches(std::string_view path,
                                 std::string_view name) noexcept
    {
        if (ascii_equal_ci(path, name)) {
            return true;
        }
        if (!ascii_ends_with_ci(path, name)) {
            return false;
        }
        const size_t offset = path.size() - name.size();
        if (offset == 0U) {
            return true;
        }
        const char c = path[offset - 1U];
        return c == ':' || c == '/' || c == '.';
    }

    static std::string_view xmp_property_leaf(std::string_view path) noexcept
    {
        size_t begin                = 0U;
        const size_t path_separator = path.rfind('/');
        if (path_separator != std::string_view::npos) {
            begin = path_separator + 1U;
        }
        size_t end = path.find('[', begin);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        for (size_t i = 0U; i < end; ++i) {
            if (i >= begin && (path[i] == ':' || path[i] == '.')) {
                begin = i + 1U;
            }
        }
        return path.substr(begin, end - begin);
    }

    static std::string_view xmp_path_language(std::string_view path) noexcept
    {
        static constexpr std::string_view marker = "[@xml:lang=";
        const size_t begin                       = path.find(marker);
        if (begin == std::string_view::npos) {
            return {};
        }
        const size_t value_begin = begin + marker.size();
        const size_t end         = path.find(']', value_begin);
        if (end == std::string_view::npos || end == value_begin) {
            return {};
        }
        return path.substr(value_begin, end - value_begin);
    }

    static void assign_candidate_language(std::string_view path,
                                          MetadataConceptCandidate* candidate)
    {
        if (!candidate) {
            return;
        }
        const std::string_view language = xmp_path_language(path);
        if (language.empty()) {
            candidate->language.assign("x-default");
            return;
        }
        candidate->language.resize(language.size());
        for (size_t i = 0U; i < language.size(); ++i) {
            candidate->language[i] = ascii_lower(language[i]);
        }
    }

    static std::string_view xmp_property_scope(std::string_view path) noexcept
    {
        const size_t separator = path.rfind('/');
        if (separator == std::string_view::npos) {
            return {};
        }
        return path.substr(0U, separator);
    }

    static std::string_view xmp_scope_leaf(std::string_view scope) noexcept
    {
        const size_t separator = scope.find_last_of(":/.");
        if (separator == std::string_view::npos) {
            return scope;
        }
        return scope.substr(separator + 1U);
    }

    static bool xmp_location_shown_scope(std::string_view scope) noexcept
    {
        static constexpr std::string_view prefix = "LocationShown[";
        scope                                    = xmp_scope_leaf(scope);
        if (!ascii_starts_with_ci(scope, prefix) || scope.back() != ']') {
            return false;
        }
        const size_t index_begin = prefix.size();
        if (index_begin + 1U >= scope.size()) {
            return false;
        }
        for (size_t i = index_begin; i + 1U < scope.size(); ++i) {
            if (!ascii_is_digit(scope[i])) {
                return false;
            }
        }
        return true;
    }

    static bool xmp_location_created_scope(std::string_view scope) noexcept
    {
        return ascii_equal_ci(xmp_scope_leaf(scope), "LocationCreated");
    }

    static bool xmp_schema_matches(const MetaStore& store, const Entry& entry,
                                   std::string_view schema) noexcept
    {
        if (entry.key.kind != MetaKeyKind::XmpProperty) {
            return false;
        }
        const std::string_view entry_schema
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.schema_ns);
        return entry_schema == schema;
    }

    static MetadataConceptSourceFamily
    source_family_for_entry(const Entry& entry) noexcept
    {
        switch (entry.key.kind) {
        case MetaKeyKind::ExifTag: return MetadataConceptSourceFamily::Exif;
        case MetaKeyKind::XmpProperty: return MetadataConceptSourceFamily::Xmp;
        case MetaKeyKind::IptcDataset: return MetadataConceptSourceFamily::Iptc;
        case MetaKeyKind::IccHeaderField:
        case MetaKeyKind::IccTag: return MetadataConceptSourceFamily::Icc;
        case MetaKeyKind::PngText: return MetadataConceptSourceFamily::PngText;
        case MetaKeyKind::Comment:
        case MetaKeyKind::ExrAttribute:
        case MetaKeyKind::PhotoshopIrb:
        case MetaKeyKind::PhotoshopIrbField:
        case MetaKeyKind::GeotiffKey:
        case MetaKeyKind::PrintImField:
        case MetaKeyKind::BmffField:
        case MetaKeyKind::JumbfField:
        case MetaKeyKind::JumbfCborKey: break;
        }
        return MetadataConceptSourceFamily::Unknown;
    }

    static uint32_t element_size(MetaElementType type) noexcept
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
        case MetaElementType::F64:
        case MetaElementType::URational:
        case MetaElementType::SRational: return 8U;
        }
        return 0U;
    }

    static bool scalar_to_double(const MetaValue& value, double* out) noexcept
    {
        if (!out || value.kind != MetaValueKind::Scalar) {
            return false;
        }
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64:
            *out = static_cast<double>(value.data.u64);
            return true;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            *out = static_cast<double>(value.data.i64);
            return true;
        case MetaElementType::F32: {
            float f = 0.0F;
            std::memcpy(&f, &value.data.f32_bits, sizeof(f));
            *out = static_cast<double>(f);
            return true;
        }
        case MetaElementType::F64: {
            double d = 0.0;
            std::memcpy(&d, &value.data.f64_bits, sizeof(d));
            *out = d;
            return true;
        }
        case MetaElementType::URational:
            if (value.data.ur.denom == 0U) {
                return false;
            }
            *out = static_cast<double>(value.data.ur.numer)
                   / static_cast<double>(value.data.ur.denom);
            return true;
        case MetaElementType::SRational:
            if (value.data.sr.denom == 0) {
                return false;
            }
            *out = static_cast<double>(value.data.sr.numer)
                   / static_cast<double>(value.data.sr.denom);
            return true;
        }
        return false;
    }

    static bool array_element_to_double(std::span<const std::byte> bytes,
                                        MetaElementType type, uint32_t index,
                                        double* out) noexcept
    {
        if (!out) {
            return false;
        }
        const uint32_t elem_size = element_size(type);
        if (elem_size == 0U) {
            return false;
        }
        const size_t offset = static_cast<size_t>(index)
                              * static_cast<size_t>(elem_size);
        if (offset > bytes.size()
            || bytes.size() - offset < static_cast<size_t>(elem_size)) {
            return false;
        }
        const std::byte* data = bytes.data() + offset;
        switch (type) {
        case MetaElementType::U8: {
            uint8_t v = 0U;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::I8: {
            int8_t v = 0;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::U16: {
            uint16_t v = 0U;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::I16: {
            int16_t v = 0;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::U32: {
            uint32_t v = 0U;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::I32: {
            int32_t v = 0;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::U64: {
            uint64_t v = 0U;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::I64: {
            int64_t v = 0;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::F32: {
            uint32_t bits = 0U;
            float v       = 0.0F;
            std::memcpy(&bits, data, sizeof(bits));
            std::memcpy(&v, &bits, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::F64: {
            uint64_t bits = 0U;
            double v      = 0.0;
            std::memcpy(&bits, data, sizeof(bits));
            std::memcpy(&v, &bits, sizeof(v));
            *out = v;
            return true;
        }
        case MetaElementType::URational: {
            URational v;
            std::memcpy(&v, data, sizeof(v));
            if (v.denom == 0U) {
                return false;
            }
            *out = static_cast<double>(v.numer) / static_cast<double>(v.denom);
            return true;
        }
        case MetaElementType::SRational: {
            SRational v;
            std::memcpy(&v, data, sizeof(v));
            if (v.denom == 0) {
                return false;
            }
            *out = static_cast<double>(v.numer) / static_cast<double>(v.denom);
            return true;
        }
        }
        return false;
    }

    static uint8_t value_to_numeric_array(const ByteArena& arena,
                                          const MetaValue& value, double* out,
                                          uint8_t max_count) noexcept
    {
        if (!out || max_count == 0U) {
            return 0U;
        }
        if (value.kind == MetaValueKind::Scalar) {
            double v = 0.0;
            if (!scalar_to_double(value, &v)) {
                return 0U;
            }
            out[0] = v;
            return 1U;
        }
        if (value.kind != MetaValueKind::Array) {
            return 0U;
        }
        const uint32_t elem_size               = element_size(value.elem_type);
        const std::span<const std::byte> bytes = arena.span(value.data.span);
        if (elem_size == 0U) {
            return 0U;
        }
        if (value.count > bytes.size() / elem_size) {
            return 0U;
        }
        const uint8_t count = static_cast<uint8_t>(
            std::min<uint32_t>(value.count, max_count));
        uint8_t written = 0U;
        for (uint8_t i = 0U; i < count; ++i) {
            double v = 0.0;
            if (!array_element_to_double(bytes, value.elem_type, i, &v)) {
                break;
            }
            out[written] = v;
            written += 1U;
        }
        return written;
    }

    static bool value_to_text(const ByteArena& arena, const MetaValue& value,
                              std::string* out)
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (value.kind == MetaValueKind::Text) {
            const std::span<const std::byte> bytes = arena.span(
                value.data.span);
            out->assign(reinterpret_cast<const char*>(bytes.data()),
                        bytes.size());
            return true;
        }
        if (value.kind != MetaValueKind::Scalar) {
            return false;
        }
        double v = 0.0;
        if (!scalar_to_double(value, &v)) {
            return false;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.12g", v);
        out->assign(buf);
        return true;
    }

    static void normalize_text_key(std::string_view text, std::string* out)
    {
        if (!out) {
            return;
        }
        out->clear();
        out->reserve(text.size());
        for (size_t i = 0U; i < text.size(); ++i) {
            const char c = text[i];
            if (ascii_is_alnum(c)) {
                out->push_back(ascii_lower(c));
            }
        }
    }

    static bool ascii_is_field_space(char c) noexcept
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0';
    }

    static std::string_view trim_ascii_field(std::string_view text) noexcept
    {
        size_t begin = 0U;
        while (begin < text.size() && ascii_is_field_space(text[begin])) {
            begin += 1U;
        }
        size_t end = text.size();
        while (end > begin && ascii_is_field_space(text[end - 1U])) {
            end -= 1U;
        }
        return text.substr(begin, end - begin);
    }

    static bool normalize_subsecond_text(std::string_view text,
                                         std::string* out)
    {
        if (!out) {
            return false;
        }
        out->clear();
        text = trim_ascii_field(text);
        if (text.empty() || text.size() > kMaxDateTimeSubsecondDigits) {
            return false;
        }
        for (size_t i = 0U; i < text.size(); ++i) {
            if (!ascii_is_digit(text[i])) {
                return false;
            }
        }
        out->assign(text);
        return true;
    }

    static uint32_t parse_decimal_digits(std::string_view text, size_t offset,
                                         size_t count) noexcept
    {
        uint32_t value = 0U;
        for (size_t i = 0U; i < count; ++i) {
            value *= 10U;
            value += static_cast<uint32_t>(text[offset + i] - '0');
        }
        return value;
    }

    static bool valid_date(uint32_t year, uint32_t month, uint32_t day) noexcept
    {
        if (year < 1U || year > 9999U || month < 1U || month > 12U) {
            return false;
        }
        static constexpr uint8_t days_in_month[] = {
            31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
        };
        uint32_t max_day = days_in_month[month - 1U];
        const bool leap  = (year % 4U == 0U)
                          && ((year % 100U) != 0U || (year % 400U) == 0U);
        if (month == 2U && leap) {
            max_day = 29U;
        }
        return day >= 1U && day <= max_day;
    }

    static bool valid_time(uint32_t hour, uint32_t minute,
                           uint32_t second) noexcept
    {
        return hour < 24U && minute < 60U && second < 61U;
    }

    static void set_datetime_precision(MetadataConceptCandidate* candidate)
    {
        if (!candidate || !candidate->has_date_time) {
            return;
        }
        if (candidate->date_time_has_time
            && candidate->date_time_has_subsecond) {
            candidate->date_time_precision
                = MetadataConceptDateTimePrecision::DateTimeSubsecond;
        } else if (candidate->date_time_has_time) {
            candidate->date_time_precision
                = MetadataConceptDateTimePrecision::DateTime;
        } else {
            candidate->date_time_precision
                = MetadataConceptDateTimePrecision::Date;
        }
    }

    static void set_datetime_timezone(MetadataConceptCandidate* candidate,
                                      bool has_offset,
                                      int16_t offset_min) noexcept
    {
        if (!candidate) {
            return;
        }
        candidate->date_time_has_utc_offset = has_offset;
        if (!has_offset) {
            candidate->date_time_zone
                = candidate->date_time_has_time
                      ? MetadataConceptTimeZoneKind::Local
                      : MetadataConceptTimeZoneKind::Unknown;
            return;
        }
        candidate->date_time_utc_offset_min = offset_min;
        if (offset_min == 0) {
            candidate->date_time_zone = MetadataConceptTimeZoneKind::Utc;
        } else {
            candidate->date_time_zone = MetadataConceptTimeZoneKind::Offset;
        }
    }

    static void format_datetime_key(const MetadataConceptCandidate& candidate,
                                    std::string* out)
    {
        if (!out) {
            return;
        }
        out->clear();
        if (!candidate.has_date_time) {
            return;
        }
        char buf[32];
        if (candidate.date_time_has_time) {
            std::snprintf(buf, sizeof(buf), "%04d%02u%02u%02u%02u%02u",
                          static_cast<int>(candidate.date_time_year),
                          static_cast<unsigned>(candidate.date_time_month),
                          static_cast<unsigned>(candidate.date_time_day),
                          static_cast<unsigned>(candidate.date_time_hour),
                          static_cast<unsigned>(candidate.date_time_minute),
                          static_cast<unsigned>(candidate.date_time_second));
        } else {
            std::snprintf(buf, sizeof(buf), "%04d%02u%02u",
                          static_cast<int>(candidate.date_time_year),
                          static_cast<unsigned>(candidate.date_time_month),
                          static_cast<unsigned>(candidate.date_time_day));
        }
        out->assign(buf);
        if (candidate.date_time_has_time && candidate.date_time_has_subsecond
            && !candidate.date_time_subsecond.empty()) {
            size_t digits = candidate.date_time_subsecond.size();
            while (digits > 1U
                   && candidate.date_time_subsecond[digits - 1U] == '0') {
                digits -= 1U;
            }
            out->push_back('.');
            out->append(candidate.date_time_subsecond.data(), digits);
        }
    }

    static bool timezone_offset_from_text(std::string_view text,
                                          uint32_t min_digits_before,
                                          int16_t* offset_min) noexcept
    {
        if (!offset_min) {
            return false;
        }
        uint32_t digits_before = 0U;
        for (size_t i = 0U; i < text.size(); ++i) {
            if (ascii_is_digit(text[i])) {
                digits_before += 1U;
                continue;
            }
            if ((text[i] == 'Z' || text[i] == 'z')
                && digits_before >= min_digits_before) {
                *offset_min = 0;
                return true;
            }
            if ((text[i] == '+' || text[i] == '-')
                && digits_before >= min_digits_before && i + 2U < text.size()
                && ascii_is_digit(text[i + 1U])
                && ascii_is_digit(text[i + 2U])) {
                const uint32_t hour = parse_decimal_digits(text, i + 1U, 2U);
                size_t minute_pos   = i + 3U;
                if (minute_pos < text.size() && text[minute_pos] == ':') {
                    minute_pos += 1U;
                }
                uint32_t minute = 0U;
                if (minute_pos + 1U < text.size()
                    && ascii_is_digit(text[minute_pos])
                    && ascii_is_digit(text[minute_pos + 1U])) {
                    minute = parse_decimal_digits(text, minute_pos, 2U);
                }
                if (hour > 23U || minute > 59U) {
                    return false;
                }
                int32_t signed_offset = static_cast<int32_t>(hour * 60U
                                                             + minute);
                if (text[i] == '-') {
                    signed_offset = -signed_offset;
                }
                *offset_min = static_cast<int16_t>(signed_offset);
                return true;
            }
        }
        return false;
    }

    static bool exif_timezone_offset_from_text(std::string_view text,
                                               int16_t* offset_min) noexcept
    {
        if (!offset_min) {
            return false;
        }
        text = trim_ascii_field(text);
        if (text.size() != 6U || (text[0] != '+' && text[0] != '-')
            || !ascii_is_digit(text[1]) || !ascii_is_digit(text[2])
            || text[3] != ':' || !ascii_is_digit(text[4])
            || !ascii_is_digit(text[5])) {
            return false;
        }
        const uint32_t hour   = parse_decimal_digits(text, 1U, 2U);
        const uint32_t minute = parse_decimal_digits(text, 4U, 2U);
        if (hour > 23U || minute > 59U) {
            return false;
        }
        int32_t value = static_cast<int32_t>(hour * 60U + minute);
        if (text[0] == '-') {
            value = -value;
        }
        *offset_min = static_cast<int16_t>(value);
        return true;
    }

    static bool fill_datetime_from_text(std::string_view text,
                                        MetadataConceptCandidate* candidate)
    {
        if (!candidate) {
            return false;
        }
        std::string digits;
        digits.reserve(14U);
        size_t date_time_digits_end = 0U;
        for (size_t i = 0U; i < text.size(); ++i) {
            if (ascii_is_digit(text[i])) {
                digits.push_back(text[i]);
                if (digits.size() == 14U) {
                    date_time_digits_end = i + 1U;
                    break;
                }
            }
        }
        if (digits.size() < 8U) {
            return false;
        }
        const uint32_t year  = parse_decimal_digits(digits, 0U, 4U);
        const uint32_t month = parse_decimal_digits(digits, 4U, 2U);
        const uint32_t day   = parse_decimal_digits(digits, 6U, 2U);
        if (!valid_date(year, month, day)) {
            return false;
        }

        candidate->has_date_time      = true;
        candidate->date_time_year     = static_cast<int16_t>(year);
        candidate->date_time_month    = static_cast<uint8_t>(month);
        candidate->date_time_day      = static_cast<uint8_t>(day);
        candidate->date_time_has_time = false;
        candidate->date_time_zone     = MetadataConceptTimeZoneKind::Unknown;
        if (digits.size() >= 14U) {
            const uint32_t hour   = parse_decimal_digits(digits, 8U, 2U);
            const uint32_t minute = parse_decimal_digits(digits, 10U, 2U);
            const uint32_t second = parse_decimal_digits(digits, 12U, 2U);
            if (valid_time(hour, minute, second)) {
                candidate->date_time_has_time = true;
                candidate->date_time_hour     = static_cast<uint8_t>(hour);
                candidate->date_time_minute   = static_cast<uint8_t>(minute);
                candidate->date_time_second   = static_cast<uint8_t>(second);
                candidate->date_time_zone = MetadataConceptTimeZoneKind::Local;

                if (date_time_digits_end < text.size()
                    && text[date_time_digits_end] == '.') {
                    size_t fraction_end = date_time_digits_end + 1U;
                    while (fraction_end < text.size()
                           && ascii_is_digit(text[fraction_end])) {
                        fraction_end += 1U;
                    }
                    const std::string_view fraction
                        = text.substr(date_time_digits_end + 1U,
                                      fraction_end - date_time_digits_end - 1U);
                    if (normalize_subsecond_text(
                            fraction, &candidate->date_time_subsecond)) {
                        candidate->date_time_has_subsecond = true;
                    }
                }
            }
        }
        set_datetime_precision(candidate);
        int16_t offset = 0;
        if (timezone_offset_from_text(text, 14U, &offset)) {
            set_datetime_timezone(candidate, true, offset);
        }
        format_datetime_key(*candidate, &candidate->value_key);
        return true;
    }

    static bool fill_time_from_value(const ByteArena& arena,
                                     const MetaValue& value, uint8_t* hour,
                                     uint8_t* minute, uint8_t* second,
                                     bool* has_utc_offset,
                                     int16_t* utc_offset_min)
    {
        if (!hour || !minute || !second || !has_utc_offset || !utc_offset_min) {
            return false;
        }
        *has_utc_offset = false;
        *utc_offset_min = 0;
        double values[3] {};
        if (value_to_numeric_array(arena, value, values, 3U) == 3U) {
            if (values[0] < 0.0 || values[1] < 0.0 || values[2] < 0.0) {
                return false;
            }
            const uint32_t h = static_cast<uint32_t>(values[0]);
            const uint32_t m = static_cast<uint32_t>(values[1]);
            const uint32_t s = static_cast<uint32_t>(values[2]);
            if (!valid_time(h, m, s)) {
                return false;
            }
            *hour   = static_cast<uint8_t>(h);
            *minute = static_cast<uint8_t>(m);
            *second = static_cast<uint8_t>(s);
            return true;
        }

        std::string text;
        if (!value_to_text(arena, value, &text)) {
            return false;
        }
        std::string digits;
        digits.reserve(6U);
        for (size_t i = 0U; i < text.size(); ++i) {
            if (ascii_is_digit(text[i])) {
                digits.push_back(text[i]);
                if (digits.size() == 6U) {
                    break;
                }
            }
        }
        if (digits.size() < 6U) {
            return false;
        }
        const uint32_t h = parse_decimal_digits(digits, 0U, 2U);
        const uint32_t m = parse_decimal_digits(digits, 2U, 2U);
        const uint32_t s = parse_decimal_digits(digits, 4U, 2U);
        if (!valid_time(h, m, s)) {
            return false;
        }
        *hour          = static_cast<uint8_t>(h);
        *minute        = static_cast<uint8_t>(m);
        *second        = static_cast<uint8_t>(s);
        int16_t offset = 0;
        if (timezone_offset_from_text(text, 6U, &offset)) {
            *has_utc_offset = true;
            *utc_offset_min = offset;
        }
        return true;
    }

    static bool attach_time_to_candidate(MetadataConceptCandidate* candidate,
                                         uint8_t hour, uint8_t minute,
                                         uint8_t second, bool has_utc_offset,
                                         int16_t utc_offset_min) noexcept
    {
        if (!candidate || !candidate->has_date_time) {
            return false;
        }
        if (!valid_time(hour, minute, second)) {
            return false;
        }
        candidate->date_time_has_time = true;
        candidate->date_time_hour     = hour;
        candidate->date_time_minute   = minute;
        candidate->date_time_second   = second;
        set_datetime_precision(candidate);
        set_datetime_timezone(candidate, has_utc_offset, utc_offset_min);
        format_datetime_key(*candidate, &candidate->value_key);
        return true;
    }

    static std::string numeric_key(double v)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.12g", v);
        return std::string(buf);
    }

    static std::string gps_numeric_key(double v)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.8f", v);
        return std::string(buf);
    }

    static bool add_unique_entry(std::vector<EntryId>* entries,
                                 EntryId entry_id)
    {
        if (!entries || entry_id == kInvalidEntryId) {
            return false;
        }
        for (size_t i = 0U; i < entries->size(); ++i) {
            if ((*entries)[i] == entry_id) {
                return false;
            }
        }
        entries->push_back(entry_id);
        return true;
    }

    static void set_transfer_hint(MetadataConceptCandidate* candidate,
                                  MetadataConceptTransferHint hint,
                                  bool compatible_safe,
                                  bool rendered_safe) noexcept
    {
        if (!candidate) {
            return;
        }
        candidate->transfer_hint        = hint;
        candidate->compatible_file_safe = compatible_safe;
        candidate->rendered_image_safe  = rendered_safe;
        candidate->requires_target_image_spec
            = hint == MetadataConceptTransferHint::RequiresTargetImageSpec;
        candidate->source_bound
            = hint == MetadataConceptTransferHint::SourceBound
              || hint == MetadataConceptTransferHint::RenderedUnsafe;
    }

    static void set_raw_applicability(MetadataConceptCandidate* candidate,
                                      MetadataRawApplicabilityState state,
                                      bool requires_storage_context,
                                      bool can_affect_decode) noexcept
    {
        if (!candidate) {
            return;
        }
        candidate->raw_applicability = state;
        candidate->raw_applicability_requires_storage_context
            = requires_storage_context;
        candidate->raw_applicability_can_affect_decode = can_affect_decode;
    }

    static bool metadata_raw_applicability_can_affect_decode(
        MetadataRawApplicabilityState state) noexcept
    {
        return state == MetadataRawApplicabilityState::AppliesToStoredRaw
               || state
                      == MetadataRawApplicabilityState::ConditionalOnRawEncoding;
    }

    static bool
    raw_data_encoding_is_compressed(MetadataRawDataEncoding encoding) noexcept
    {
        return encoding == MetadataRawDataEncoding::LosslessCompressed
               || encoding == MetadataRawDataEncoding::LossyCompressed;
    }

    static void assign_raw_applicability(
        MetadataConceptCandidate* candidate,
        const MetadataRawDataDescriptor* descriptor) noexcept
    {
        if (!candidate
            || candidate->kind != MetadataConceptKind::RawProcessing) {
            return;
        }
        MetadataRawDataDescriptor unknown_descriptor;
        const MetadataRawDataDescriptor& raw_descriptor
            = descriptor ? *descriptor : unknown_descriptor;
        const MetadataRawApplicabilityState state
            = metadata_raw_applicability_for_descriptor(candidate->role,
                                                        raw_descriptor);
        if (state != MetadataRawApplicabilityState::Unknown) {
            set_raw_applicability(
                candidate, state,
                state
                    == MetadataRawApplicabilityState::ConditionalOnRawEncoding,
                metadata_raw_applicability_can_affect_decode(state));
            return;
        }

        switch (candidate->role) {
        case MetadataConceptRole::BlackLevel:
        case MetadataConceptRole::WhiteLevel:
        case MetadataConceptRole::CfaLayout:
        case MetadataConceptRole::SensorGeometry:
        case MetadataConceptRole::RawStorage:
        case MetadataConceptRole::Linearization:
        case MetadataConceptRole::RawValueCurve:
        case MetadataConceptRole::RawLinearityLimit:
        case MetadataConceptRole::RawCalibrationCurve:
        case MetadataConceptRole::RawCurveControlPoints:
        case MetadataConceptRole::SourceProcessing:
        case MetadataConceptRole::ComputationalProcessing:
        case MetadataConceptRole::ThermalProcessing:
        case MetadataConceptRole::StitchProcessing:
        case MetadataConceptRole::ExposureTime:
        case MetadataConceptRole::Aperture:
        case MetadataConceptRole::IsoSensitivity:
        case MetadataConceptRole::ExposureBias:
        case MetadataConceptRole::ExposureProgram:
        case MetadataConceptRole::Gain:
        case MetadataConceptRole::RawExposureAdjustment:
        case MetadataConceptRole::ContentBoundMetadata:
        case MetadataConceptRole::MultiImageScene:
        case MetadataConceptRole::DerivedImageConstruction:
        case MetadataConceptRole::TiledImageConfiguration:
        case MetadataConceptRole::Primary:
        case MetadataConceptRole::Orientation:
        case MetadataConceptRole::Created:
        case MetadataConceptRole::Digitized:
        case MetadataConceptRole::Modified:
        case MetadataConceptRole::MetadataDate:
        case MetadataConceptRole::DateCreated:
        case MetadataConceptRole::ColorSpace:
        case MetadataConceptRole::IccProfile:
        case MetadataConceptRole::ColorMatrix:
        case MetadataConceptRole::WhiteBalance:
        case MetadataConceptRole::SourceColorTransform:
        case MetadataConceptRole::Latitude:
        case MetadataConceptRole::Longitude:
        case MetadataConceptRole::Altitude:
        case MetadataConceptRole::DestinationLatitude:
        case MetadataConceptRole::DestinationLongitude:
        case MetadataConceptRole::LocationShownLatitude:
        case MetadataConceptRole::LocationShownLongitude:
        case MetadataConceptRole::LocationShownAltitude:
        case MetadataConceptRole::LocationCreatedLatitude:
        case MetadataConceptRole::LocationCreatedLongitude:
        case MetadataConceptRole::LocationCreatedAltitude:
        case MetadataConceptRole::Title:
        case MetadataConceptRole::Headline:
        case MetadataConceptRole::Description:
        case MetadataConceptRole::Creator:
        case MetadataConceptRole::Keywords:
        case MetadataConceptRole::LocationName:
        case MetadataConceptRole::Sublocation:
        case MetadataConceptRole::City:
        case MetadataConceptRole::ProvinceState:
        case MetadataConceptRole::CountryName:
        case MetadataConceptRole::CountryCode:
        case MetadataConceptRole::WorldRegion:
        case MetadataConceptRole::LocationIdentifier:
        case MetadataConceptRole::CopyrightNotice:
        case MetadataConceptRole::CopyrightStatus:
        case MetadataConceptRole::RightsUsageTerms:
        case MetadataConceptRole::RightsWebStatement:
        case MetadataConceptRole::RightsCertificate:
        case MetadataConceptRole::RightsMarked:
        case MetadataConceptRole::RightsHolderName:
        case MetadataConceptRole::RightsHolderIdentifier:
        case MetadataConceptRole::LicenseIdentifier:
        case MetadataConceptRole::LicenseTermsUrl:
        case MetadataConceptRole::LicensorName:
        case MetadataConceptRole::LicensorIdentifier:
        case MetadataConceptRole::CreditLine:
        case MetadataConceptRole::CreditLineRequired:
        case MetadataConceptRole::Source:
        case MetadataConceptRole::DigitalSourceType:
        case MetadataConceptRole::Timestamp:
        case MetadataConceptRole::Crop:
        case MetadataConceptRole::ActiveArea:
        case MetadataConceptRole::Border:
        case MetadataConceptRole::LensCorrection: break;
        default: break;
        }
    }

    static void
    assign_transfer_hint(MetadataConceptCandidate* candidate) noexcept
    {
        if (!candidate) {
            return;
        }
        switch (candidate->kind) {
        case MetadataConceptKind::DateTime:
        case MetadataConceptKind::Gps:
            set_transfer_hint(candidate, MetadataConceptTransferHint::Safe,
                              true, true);
            return;
        case MetadataConceptKind::Descriptive:
            switch (candidate->record_kind) {
            case MetadataConceptRecordKind::ImageRegion:
                set_transfer_hint(
                    candidate,
                    MetadataConceptTransferHint::RequiresTargetImageSpec, true,
                    false);
                return;
            case MetadataConceptRecordKind::ImageAsset:
            case MetadataConceptRecordKind::RegistryEntry:
            case MetadataConceptRecordKind::ResourceReference:
            case MetadataConceptRecordKind::ResourceEvent:
            case MetadataConceptRecordKind::PantryItem:
                set_transfer_hint(candidate,
                                  MetadataConceptTransferHint::SourceBound,
                                  true, false);
                return;
            default: break;
            }
            set_transfer_hint(candidate, MetadataConceptTransferHint::Safe,
                              true, true);
            return;
        case MetadataConceptKind::Exposure:
            if (candidate->role == MetadataConceptRole::RawExposureAdjustment) {
                set_transfer_hint(candidate,
                                  MetadataConceptTransferHint::RenderedUnsafe,
                                  true, false);
                return;
            }
            set_transfer_hint(candidate, MetadataConceptTransferHint::Safe,
                              true, true);
            return;
        case MetadataConceptKind::ContainerGraph:
            set_transfer_hint(candidate,
                              MetadataConceptTransferHint::SourceBound, true,
                              false);
            return;
        case MetadataConceptKind::Orientation:
            set_transfer_hint(
                candidate, MetadataConceptTransferHint::RequiresTargetImageSpec,
                true, false);
            return;
        case MetadataConceptKind::Geometry:
            set_transfer_hint(
                candidate, MetadataConceptTransferHint::RequiresTargetImageSpec,
                true, false);
            return;
        case MetadataConceptKind::LensCorrection:
            set_transfer_hint(candidate,
                              MetadataConceptTransferHint::RenderedUnsafe, true,
                              false);
            return;
        case MetadataConceptKind::RawProcessing:
            switch (candidate->role) {
            case MetadataConceptRole::BlackLevel:
            case MetadataConceptRole::WhiteLevel:
            case MetadataConceptRole::Linearization:
            case MetadataConceptRole::RawValueCurve:
            case MetadataConceptRole::RawLinearityLimit:
            case MetadataConceptRole::RawCalibrationCurve:
            case MetadataConceptRole::RawCurveControlPoints:
                set_transfer_hint(candidate,
                                  MetadataConceptTransferHint::RenderedUnsafe,
                                  true, false);
                return;
            case MetadataConceptRole::CfaLayout:
            case MetadataConceptRole::SensorGeometry:
            case MetadataConceptRole::RawStorage:
            case MetadataConceptRole::SourceProcessing:
            case MetadataConceptRole::ComputationalProcessing:
            case MetadataConceptRole::ThermalProcessing:
            case MetadataConceptRole::StitchProcessing:
            case MetadataConceptRole::ExposureTime:
            case MetadataConceptRole::Aperture:
            case MetadataConceptRole::IsoSensitivity:
            case MetadataConceptRole::ExposureBias:
            case MetadataConceptRole::ExposureProgram:
            case MetadataConceptRole::Gain:
            case MetadataConceptRole::RawExposureAdjustment:
            case MetadataConceptRole::ContentBoundMetadata:
            case MetadataConceptRole::MultiImageScene:
            case MetadataConceptRole::DerivedImageConstruction:
            case MetadataConceptRole::TiledImageConfiguration:
            case MetadataConceptRole::Primary:
                set_transfer_hint(candidate,
                                  MetadataConceptTransferHint::SourceBound,
                                  true, false);
                return;
            case MetadataConceptRole::Orientation:
            case MetadataConceptRole::Created:
            case MetadataConceptRole::Digitized:
            case MetadataConceptRole::Modified:
            case MetadataConceptRole::MetadataDate:
            case MetadataConceptRole::DateCreated:
            case MetadataConceptRole::ColorSpace:
            case MetadataConceptRole::IccProfile:
            case MetadataConceptRole::ColorMatrix:
            case MetadataConceptRole::WhiteBalance:
            case MetadataConceptRole::SourceColorTransform:
            case MetadataConceptRole::Latitude:
            case MetadataConceptRole::Longitude:
            case MetadataConceptRole::Altitude:
            case MetadataConceptRole::DestinationLatitude:
            case MetadataConceptRole::DestinationLongitude:
            case MetadataConceptRole::LocationShownLatitude:
            case MetadataConceptRole::LocationShownLongitude:
            case MetadataConceptRole::LocationShownAltitude:
            case MetadataConceptRole::LocationCreatedLatitude:
            case MetadataConceptRole::LocationCreatedLongitude:
            case MetadataConceptRole::LocationCreatedAltitude:
            case MetadataConceptRole::Title:
            case MetadataConceptRole::Headline:
            case MetadataConceptRole::Description:
            case MetadataConceptRole::Creator:
            case MetadataConceptRole::Keywords:
            case MetadataConceptRole::LocationName:
            case MetadataConceptRole::Sublocation:
            case MetadataConceptRole::City:
            case MetadataConceptRole::ProvinceState:
            case MetadataConceptRole::CountryName:
            case MetadataConceptRole::CountryCode:
            case MetadataConceptRole::WorldRegion:
            case MetadataConceptRole::LocationIdentifier:
            case MetadataConceptRole::CopyrightNotice:
            case MetadataConceptRole::CopyrightStatus:
            case MetadataConceptRole::RightsUsageTerms:
            case MetadataConceptRole::RightsWebStatement:
            case MetadataConceptRole::RightsCertificate:
            case MetadataConceptRole::RightsMarked:
            case MetadataConceptRole::RightsHolderName:
            case MetadataConceptRole::RightsHolderIdentifier:
            case MetadataConceptRole::LicenseIdentifier:
            case MetadataConceptRole::LicenseTermsUrl:
            case MetadataConceptRole::LicensorName:
            case MetadataConceptRole::LicensorIdentifier:
            case MetadataConceptRole::CreditLine:
            case MetadataConceptRole::CreditLineRequired:
            case MetadataConceptRole::Source:
            case MetadataConceptRole::DigitalSourceType:
            case MetadataConceptRole::Timestamp:
            case MetadataConceptRole::Crop:
            case MetadataConceptRole::ActiveArea:
            case MetadataConceptRole::Border:
            case MetadataConceptRole::LensCorrection: break;
            default: break;
            }
            break;
        case MetadataConceptKind::ColorProfile:
            switch (candidate->role) {
            case MetadataConceptRole::ColorMatrix:
            case MetadataConceptRole::WhiteBalance:
            case MetadataConceptRole::SourceColorTransform:
                set_transfer_hint(candidate,
                                  MetadataConceptTransferHint::RenderedUnsafe,
                                  true, false);
                return;
            case MetadataConceptRole::ColorSpace:
            case MetadataConceptRole::IccProfile:
            case MetadataConceptRole::Primary:
                set_transfer_hint(
                    candidate,
                    MetadataConceptTransferHint::RequiresTargetImageSpec, true,
                    false);
                return;
            case MetadataConceptRole::Orientation:
            case MetadataConceptRole::Created:
            case MetadataConceptRole::Digitized:
            case MetadataConceptRole::Modified:
            case MetadataConceptRole::MetadataDate:
            case MetadataConceptRole::DateCreated:
            case MetadataConceptRole::Latitude:
            case MetadataConceptRole::Longitude:
            case MetadataConceptRole::Altitude:
            case MetadataConceptRole::DestinationLatitude:
            case MetadataConceptRole::DestinationLongitude:
            case MetadataConceptRole::LocationShownLatitude:
            case MetadataConceptRole::LocationShownLongitude:
            case MetadataConceptRole::LocationShownAltitude:
            case MetadataConceptRole::LocationCreatedLatitude:
            case MetadataConceptRole::LocationCreatedLongitude:
            case MetadataConceptRole::LocationCreatedAltitude:
            case MetadataConceptRole::Title:
            case MetadataConceptRole::Headline:
            case MetadataConceptRole::Description:
            case MetadataConceptRole::Creator:
            case MetadataConceptRole::Keywords:
            case MetadataConceptRole::LocationName:
            case MetadataConceptRole::Sublocation:
            case MetadataConceptRole::City:
            case MetadataConceptRole::ProvinceState:
            case MetadataConceptRole::CountryName:
            case MetadataConceptRole::CountryCode:
            case MetadataConceptRole::WorldRegion:
            case MetadataConceptRole::LocationIdentifier:
            case MetadataConceptRole::CopyrightNotice:
            case MetadataConceptRole::CopyrightStatus:
            case MetadataConceptRole::RightsUsageTerms:
            case MetadataConceptRole::RightsWebStatement:
            case MetadataConceptRole::RightsCertificate:
            case MetadataConceptRole::RightsMarked:
            case MetadataConceptRole::RightsHolderName:
            case MetadataConceptRole::RightsHolderIdentifier:
            case MetadataConceptRole::LicenseIdentifier:
            case MetadataConceptRole::LicenseTermsUrl:
            case MetadataConceptRole::LicensorName:
            case MetadataConceptRole::LicensorIdentifier:
            case MetadataConceptRole::CreditLine:
            case MetadataConceptRole::CreditLineRequired:
            case MetadataConceptRole::Source:
            case MetadataConceptRole::DigitalSourceType:
            case MetadataConceptRole::Timestamp:
            case MetadataConceptRole::Crop:
            case MetadataConceptRole::ActiveArea:
            case MetadataConceptRole::Border:
            case MetadataConceptRole::SensorGeometry:
            case MetadataConceptRole::LensCorrection:
            case MetadataConceptRole::BlackLevel:
            case MetadataConceptRole::WhiteLevel:
            case MetadataConceptRole::Linearization:
            case MetadataConceptRole::RawValueCurve:
            case MetadataConceptRole::RawLinearityLimit:
            case MetadataConceptRole::RawCalibrationCurve:
            case MetadataConceptRole::RawCurveControlPoints:
            case MetadataConceptRole::CfaLayout:
            case MetadataConceptRole::RawStorage:
            case MetadataConceptRole::SourceProcessing:
            case MetadataConceptRole::ComputationalProcessing:
            case MetadataConceptRole::ThermalProcessing:
            case MetadataConceptRole::StitchProcessing:
            case MetadataConceptRole::ExposureTime:
            case MetadataConceptRole::Aperture:
            case MetadataConceptRole::IsoSensitivity:
            case MetadataConceptRole::ExposureBias:
            case MetadataConceptRole::ExposureProgram:
            case MetadataConceptRole::Gain:
            case MetadataConceptRole::RawExposureAdjustment: break;
            case MetadataConceptRole::ContentBoundMetadata:
            case MetadataConceptRole::MultiImageScene:
            case MetadataConceptRole::DerivedImageConstruction:
            case MetadataConceptRole::TiledImageConfiguration: break;
            default: break;
            }
            break;
        }
        set_transfer_hint(candidate, MetadataConceptTransferHint::Unknown,
                          false, false);
    }

    static bool role_is_contact_detail(MetadataConceptRole role) noexcept
    {
        return role == MetadataConceptRole::Address
               || role == MetadataConceptRole::PostalCode
               || role == MetadataConceptRole::Email
               || role == MetadataConceptRole::Telephone
               || role == MetadataConceptRole::Url
               || role == MetadataConceptRole::City
               || role == MetadataConceptRole::ProvinceState
               || role == MetadataConceptRole::CountryName;
    }

    static bool role_is_legal_rights(MetadataConceptRole role) noexcept
    {
        switch (role) {
        case MetadataConceptRole::CopyrightNotice:
        case MetadataConceptRole::CopyrightStatus:
        case MetadataConceptRole::RightsUsageTerms:
        case MetadataConceptRole::RightsWebStatement:
        case MetadataConceptRole::RightsCertificate:
        case MetadataConceptRole::RightsMarked:
        case MetadataConceptRole::RightsHolderName:
        case MetadataConceptRole::RightsHolderIdentifier:
        case MetadataConceptRole::LicenseIdentifier:
        case MetadataConceptRole::LicenseTermsUrl:
        case MetadataConceptRole::LicensorName:
        case MetadataConceptRole::LicensorIdentifier:
        case MetadataConceptRole::RightsExpression:
        case MetadataConceptRole::RightsExpressionEncoding:
        case MetadataConceptRole::RightsExpressionLanguage:
        case MetadataConceptRole::LicenseStartDate:
        case MetadataConceptRole::LicenseEndDate:
        case MetadataConceptRole::MediaConstraint:
        case MetadataConceptRole::RegionConstraint:
        case MetadataConceptRole::ProductOrServiceConstraint:
        case MetadataConceptRole::ImageFileConstraint:
        case MetadataConceptRole::ImageAlterationConstraint:
        case MetadataConceptRole::OtherLicenseRequirement:
        case MetadataConceptRole::OtherCondition:
        case MetadataConceptRole::LicenseeTransactionIdentifier:
        case MetadataConceptRole::LicensorTransactionIdentifier:
        case MetadataConceptRole::LicenseeProjectReference:
        case MetadataConceptRole::LicenseTransactionDate:
        case MetadataConceptRole::ReleaseStatus:
        case MetadataConceptRole::ReleaseIdentifier:
        case MetadataConceptRole::MediaSummaryCode:
        case MetadataConceptRole::ImageDuplicationConstraint:
        case MetadataConceptRole::MinorModelAgeDisclosure:
        case MetadataConceptRole::AdultContentWarning:
        case MetadataConceptRole::CopyrightRegistrationNumber:
        case MetadataConceptRole::Reuse:
        case MetadataConceptRole::DataMining:
        case MetadataConceptRole::OtherLicenseDocument:
        case MetadataConceptRole::OtherLicenseInformation: return true;
        default: break;
        }
        return false;
    }

    static bool role_is_location_detail(MetadataConceptRole role) noexcept
    {
        return role == MetadataConceptRole::LocationName
               || role == MetadataConceptRole::Sublocation
               || role == MetadataConceptRole::City
               || role == MetadataConceptRole::ProvinceState
               || role == MetadataConceptRole::CountryName
               || role == MetadataConceptRole::CountryCode
               || role == MetadataConceptRole::WorldRegion
               || role == MetadataConceptRole::LocationIdentifier;
    }

    static void assign_sensitivity(MetadataConceptCandidate* candidate) noexcept
    {
        if (!candidate) {
            return;
        }
        candidate->sensitivity = MetadataConceptSensitivity::None;
        if (candidate->record_kind == MetadataConceptRecordKind::CreatorContact
            || ((candidate->record_kind == MetadataConceptRecordKind::Licensor
                 || candidate->record_kind
                        == MetadataConceptRecordKind::Licensee)
                && role_is_contact_detail(candidate->role))) {
            candidate->sensitivity = MetadataConceptSensitivity::PersonalContact;
            return;
        }
        if (candidate->record_kind == MetadataConceptRecordKind::Person
            || candidate->record_kind == MetadataConceptRecordKind::ImageCreator
            || candidate->role == MetadataConceptRole::Creator
            || candidate->role == MetadataConceptRole::CreatorIdentifier
            || candidate->role == MetadataConceptRole::CreatorTitle
            || candidate->role == MetadataConceptRole::CaptionWriter
            || candidate->role == MetadataConceptRole::Age) {
            candidate->sensitivity = MetadataConceptSensitivity::PersonIdentity;
            return;
        }
        if (candidate->kind == MetadataConceptKind::Gps
            || role_is_location_detail(candidate->role)) {
            candidate->sensitivity = MetadataConceptSensitivity::Location;
            return;
        }
        if (candidate->record_kind
                == MetadataConceptRecordKind::RightsExpression
            || candidate->record_kind == MetadataConceptRecordKind::RightsHolder
            || candidate->record_kind == MetadataConceptRecordKind::Licensor
            || candidate->record_kind == MetadataConceptRecordKind::Licensee
            || candidate->record_kind == MetadataConceptRecordKind::License
            || candidate->record_kind == MetadataConceptRecordKind::Release
            || candidate->record_kind == MetadataConceptRecordKind::EndUser
            || role_is_legal_rights(candidate->role)) {
            candidate->sensitivity = MetadataConceptSensitivity::LegalRights;
        }
    }

    static MetadataConceptCandidate*
    find_candidate(MetadataConceptResolution* resolution, EntryId entry_id,
                   MetadataConceptRole role,
                   MetadataQueryValueShape shape) noexcept
    {
        if (!resolution || entry_id == kInvalidEntryId) {
            return nullptr;
        }
        for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
            MetadataConceptCandidate& candidate = resolution->candidates[i];
            if (candidate.entry_id == entry_id && candidate.role == role
                && candidate.shape == shape) {
                return &candidate;
            }
        }
        return nullptr;
    }

    static void merge_candidate(MetadataConceptCandidate* dst,
                                const MetadataConceptCandidate& src)
    {
        if (!dst) {
            return;
        }
        if (src.priority > dst->priority) {
            dst->priority = src.priority;
        }
        if (dst->semantic == MetadataQuerySemanticKind::Unknown) {
            dst->semantic = src.semantic;
        }
        if (dst->shape == MetadataQueryValueShape::Unknown) {
            dst->shape = src.shape;
        }
        for (size_t i = 0U; i < src.source_entries.size(); ++i) {
            add_unique_entry(&dst->source_entries, src.source_entries[i]);
        }
        if (!dst->has_numeric && src.has_numeric) {
            dst->has_numeric   = true;
            dst->numeric_count = src.numeric_count;
            for (uint8_t i = 0U; i < src.numeric_count; ++i) {
                dst->numeric[i] = src.numeric[i];
            }
        }
        if (!dst->has_values && src.has_values) {
            dst->has_values = true;
            dst->values     = src.values;
        }
        if (!dst->has_origin && src.has_origin) {
            dst->has_origin = true;
            dst->origin[0]  = src.origin[0];
            dst->origin[1]  = src.origin[1];
        }
        if (!dst->has_size && src.has_size) {
            dst->has_size = true;
            dst->size[0]  = src.size[0];
            dst->size[1]  = src.size[1];
        }
        if (!dst->has_rect && src.has_rect) {
            dst->has_rect = true;
            for (uint8_t i = 0U; i < 4U; ++i) {
                dst->rect[i] = src.rect[i];
            }
        }
        if (!dst->has_margins && src.has_margins) {
            dst->has_margins = true;
            for (uint8_t i = 0U; i < 4U; ++i) {
                dst->margins[i] = src.margins[i];
            }
        }
        if ((dst->text.empty() || src.text.size() > dst->text.size())
            && !src.text.empty()) {
            dst->text = src.text;
        }
        if ((dst->value_key.empty()
             || src.value_key.size() > dst->value_key.size())
            && !src.value_key.empty()) {
            dst->value_key = src.value_key;
        }
        if (!dst->has_date_time && src.has_date_time) {
            dst->has_date_time            = true;
            dst->date_time_has_time       = src.date_time_has_time;
            dst->date_time_has_utc_offset = src.date_time_has_utc_offset;
            dst->date_time_precision      = src.date_time_precision;
            dst->date_time_zone           = src.date_time_zone;
            dst->date_time_year           = src.date_time_year;
            dst->date_time_month          = src.date_time_month;
            dst->date_time_day            = src.date_time_day;
            dst->date_time_hour           = src.date_time_hour;
            dst->date_time_minute         = src.date_time_minute;
            dst->date_time_second         = src.date_time_second;
            dst->date_time_has_subsecond  = src.date_time_has_subsecond;
            dst->date_time_subsecond      = src.date_time_subsecond;
            dst->date_time_utc_offset_min = src.date_time_utc_offset_min;
        } else if (dst->has_date_time && src.has_date_time
                   && dst->date_time_year == src.date_time_year
                   && dst->date_time_month == src.date_time_month
                   && dst->date_time_day == src.date_time_day) {
            if (!dst->date_time_has_time && src.date_time_has_time) {
                dst->date_time_has_time  = true;
                dst->date_time_precision = src.date_time_precision;
                dst->date_time_hour      = src.date_time_hour;
                dst->date_time_minute    = src.date_time_minute;
                dst->date_time_second    = src.date_time_second;
            }
            if (!dst->date_time_has_subsecond && src.date_time_has_subsecond) {
                dst->date_time_has_subsecond = true;
                dst->date_time_subsecond     = src.date_time_subsecond;
                dst->date_time_precision     = src.date_time_precision;
            }
            if (!dst->date_time_has_utc_offset
                && src.date_time_has_utc_offset) {
                dst->date_time_has_utc_offset = true;
                dst->date_time_zone           = src.date_time_zone;
                dst->date_time_utc_offset_min = src.date_time_utc_offset_min;
            }
        }
        if (!dst->has_gps_altitude_reference
            && src.has_gps_altitude_reference) {
            dst->has_gps_altitude_reference = src.has_gps_altitude_reference;
            dst->gps_altitude_below_sea_level = src.gps_altitude_below_sea_level;
            dst->gps_altitude_reference_code = src.gps_altitude_reference_code;
        }
        if (dst->location_scope.empty() && !src.location_scope.empty()) {
            dst->location_scope = src.location_scope;
        }
        if (dst->record_scope.empty() && !src.record_scope.empty()) {
            dst->record_scope = src.record_scope;
        }
        if (dst->language.empty() && !src.language.empty()) {
            dst->language = src.language;
        }
    }

    static void append_candidate(MetadataConceptResolution* resolution,
                                 const MetadataConceptCandidate& candidate)
    {
        if (!resolution) {
            return;
        }
        MetadataConceptCandidate* existing
            = find_candidate(resolution, candidate.entry_id, candidate.role,
                             candidate.shape);
        if (existing) {
            merge_candidate(existing, candidate);
            return;
        }
        resolution->candidates.push_back(candidate);
    }

    static MetadataConceptCandidate
    make_entry_candidate(const MetaStore& store, EntryId entry_id,
                         MetadataConceptKind kind, MetadataConceptRole role,
                         MetadataQuerySemanticKind semantic,
                         MetadataQueryValueShape shape, uint8_t priority)
    {
        MetadataConceptCandidate out;
        out.kind     = kind;
        out.role     = role;
        out.semantic = semantic;
        out.shape    = shape;
        out.entry_id = entry_id;
        out.priority = priority;
        if (entry_id != kInvalidEntryId) {
            out.family = source_family_for_entry(store.entry(entry_id));
            out.source_entries.push_back(entry_id);
        }
        return out;
    }

    static bool exif_entry_ifd_and_tag(const MetaStore& store,
                                       const Entry& entry, std::string_view ifd,
                                       uint16_t tag) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }
        if (entry.key.data.exif_tag.tag != tag) {
            return false;
        }
        const std::string_view entry_ifd
            = arena_string(store.arena(), entry.key.data.exif_tag.ifd);
        return ascii_equal_ci(entry_ifd, ifd);
    }

    static bool exif_entry_tag(const Entry& entry, uint16_t tag) noexcept
    {
        return entry.key.kind == MetaKeyKind::ExifTag
               && entry.key.data.exif_tag.tag == tag;
    }

    static bool bmff_entry_field_matches(const MetaStore& store,
                                         const Entry& entry,
                                         std::string_view field) noexcept
    {
        if (entry.key.kind != MetaKeyKind::BmffField) {
            return false;
        }
        const std::string_view entry_field
            = arena_string(store.arena(), entry.key.data.bmff_field.field);
        return ascii_equal_ci(entry_field, field);
    }

    static bool find_exif_text_entry(const MetaStore& store,
                                     std::string_view ifd, uint16_t tag,
                                     EntryId* out_id, std::string* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (!exif_entry_ifd_and_tag(store, entry, ifd, tag)) {
                continue;
            }
            if (!value_to_text(store.arena(), entry.value, out)) {
                continue;
            }
            if (out_id) {
                *out_id = id;
            }
            return true;
        }
        if (out_id) {
            *out_id = kInvalidEntryId;
        }
        if (out) {
            out->clear();
        }
        return false;
    }

    static bool find_exif_time_entry(const MetaStore& store,
                                     std::string_view ifd, uint16_t tag,
                                     EntryId* out_id, uint8_t* hour,
                                     uint8_t* minute, uint8_t* second)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (!exif_entry_ifd_and_tag(store, entry, ifd, tag)) {
                continue;
            }
            bool has_offset = false;
            int16_t offset  = 0;
            if (!fill_time_from_value(store.arena(), entry.value, hour, minute,
                                      second, &has_offset, &offset)) {
                continue;
            }
            if (out_id) {
                *out_id = id;
            }
            return true;
        }
        if (out_id) {
            *out_id = kInvalidEntryId;
        }
        return false;
    }

    static bool find_exif_numeric_entry(const MetaStore& store,
                                        std::string_view ifd, uint16_t tag,
                                        EntryId* out_id, double* out) noexcept
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (!exif_entry_ifd_and_tag(store, entry, ifd, tag)) {
                continue;
            }
            double values[1] {};
            if (value_to_numeric_array(store.arena(), entry.value, values, 1U)
                != 1U) {
                continue;
            }
            if (out_id) {
                *out_id = id;
            }
            if (out) {
                *out = values[0];
            }
            return true;
        }
        if (out_id) {
            *out_id = kInvalidEntryId;
        }
        return false;
    }

    static void fill_numeric_candidate(MetadataConceptCandidate* candidate,
                                       const double* values, uint8_t count)
    {
        if (!candidate || !values || count == 0U) {
            return;
        }
        const uint8_t copy_count = std::min<uint8_t>(count, 4U);
        candidate->has_numeric   = true;
        candidate->numeric_count = copy_count;
        for (uint8_t i = 0U; i < copy_count; ++i) {
            candidate->numeric[i] = values[i];
        }
    }

    static void fill_values_candidate(MetadataConceptCandidate* candidate,
                                      const std::vector<double>& values)
    {
        if (!candidate || values.empty()) {
            return;
        }
        candidate->has_values = true;
        candidate->values     = values;
        if (!candidate->has_numeric) {
            const uint8_t count = static_cast<uint8_t>(
                std::min<size_t>(values.size(), 4U));
            fill_numeric_candidate(candidate, values.data(), count);
        }
    }

    static std::string values_key(const std::vector<double>& values)
    {
        std::string out;
        if (values.empty()) {
            return out;
        }
        out.reserve(values.size() * 16U);
        char buf[64];
        for (size_t i = 0U; i < values.size(); ++i) {
            if (i != 0U) {
                out.push_back(',');
            }
            std::snprintf(buf, sizeof(buf), "%.12g", values[i]);
            out.append(buf);
        }
        return out;
    }

    static void fill_pair_candidate(bool present, const double* src,
                                    bool* dst_present, double* dst) noexcept
    {
        if (!dst_present || !dst) {
            return;
        }
        *dst_present = present;
        if (!present || !src) {
            return;
        }
        dst[0] = src[0];
        dst[1] = src[1];
    }

    static void fill_quad_candidate(bool present, const double* src,
                                    bool* dst_present, double* dst) noexcept
    {
        if (!dst_present || !dst) {
            return;
        }
        *dst_present = present;
        if (!present || !src) {
            return;
        }
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
    }

    static std::string geometry_key(const MetadataConceptCandidate& candidate)
    {
        char buf[160];
        if (candidate.has_rect) {
            std::snprintf(buf, sizeof(buf), "rect:%.12g,%.12g,%.12g,%.12g",
                          candidate.rect[0], candidate.rect[1],
                          candidate.rect[2], candidate.rect[3]);
            return std::string(buf);
        }
        if (candidate.has_margins) {
            std::snprintf(buf, sizeof(buf), "margins:%.12g,%.12g,%.12g,%.12g",
                          candidate.margins[0], candidate.margins[1],
                          candidate.margins[2], candidate.margins[3]);
            return std::string(buf);
        }
        if (candidate.has_size) {
            std::snprintf(buf, sizeof(buf), "size:%.12g,%.12g",
                          candidate.size[0], candidate.size[1]);
            return std::string(buf);
        }
        if (candidate.has_origin) {
            std::snprintf(buf, sizeof(buf), "origin:%.12g,%.12g",
                          candidate.origin[0], candidate.origin[1]);
            return std::string(buf);
        }
        if (candidate.has_numeric && candidate.numeric_count != 0U) {
            return numeric_key(candidate.numeric[0]);
        }
        return std::string();
    }

    static bool parse_xmp_gps_coordinate(std::string_view text, double* out)
    {
        if (!out || text.empty()) {
            return false;
        }
        std::string tmp(text);
        const char* cursor = tmp.c_str();
        char* end          = nullptr;
        double degrees     = std::strtod(cursor, &end);
        if (end == cursor) {
            return false;
        }
        double value = degrees;
        if (*end == ',') {
            cursor         = end + 1;
            double minutes = std::strtod(cursor, &end);
            if (end != cursor) {
                value = std::fabs(degrees) + (minutes / 60.0);
                if (*end == ',') {
                    cursor         = end + 1;
                    double seconds = std::strtod(cursor, &end);
                    if (end != cursor) {
                        value += seconds / 3600.0;
                    }
                }
            }
        }
        bool explicit_negative = degrees < 0.0;
        for (size_t i = 0U; i < tmp.size(); ++i) {
            const char c = ascii_lower(tmp[i]);
            if (c == 's' || c == 'w') {
                explicit_negative = true;
            }
            if (c == 'n' || c == 'e') {
                explicit_negative = false;
            }
        }
        if (explicit_negative) {
            value = -std::fabs(value);
        }
        *out = value;
        return true;
    }

    static bool gps_coordinate_from_value(const MetaStore& store,
                                          const MetaValue& value,
                                          std::string_view ref,
                                          double* out) noexcept
    {
        if (!out) {
            return false;
        }
        double values[4] {};
        const uint8_t count = value_to_numeric_array(store.arena(), value,
                                                     values, 4U);
        if (count == 0U) {
            return false;
        }
        double result = values[0];
        if (count >= 2U) {
            result += values[1] / 60.0;
        }
        if (count >= 3U) {
            result += values[2] / 3600.0;
        }
        if (!ref.empty()) {
            const char c = ascii_lower(ref[0]);
            if (c == 's' || c == 'w') {
                result = -std::fabs(result);
            }
        }
        *out = result;
        return true;
    }

    static void append_interpretation_candidates(
        const MetaStore& store, MetadataQueryKind query_kind,
        MetadataConceptKind concept_kind, MetadataConceptRole role,
        uint8_t priority, MetadataConceptResolution* out)
    {
        MetadataInterpretationResult result
            = interpret_metadata_query(store, query_kind);
        for (size_t i = 0U; i < result.records.size(); ++i) {
            const MetadataInterpretationRecord& record = result.records[i];
            for (size_t e = 0U; e < record.source_entries.size(); ++e) {
                const EntryId entry_id = record.source_entries[e];
                if (entry_id == kInvalidEntryId) {
                    continue;
                }
                MetadataConceptCandidate candidate
                    = make_entry_candidate(store, entry_id, concept_kind, role,
                                           record.semantic, record.shape,
                                           priority);
                if (record.has_values && !record.values.empty()) {
                    fill_values_candidate(&candidate, record.values);
                    candidate.value_key = numeric_key(record.values[0]);
                }
                append_candidate(out, candidate);
            }
        }
    }

    static void
    append_exif_orientation_candidate(const MetaStore& store, EntryId id,
                                      const Entry& entry,
                                      MetadataConceptResolution* out)
    {
        double values[1] {};
        if (value_to_numeric_array(store.arena(), entry.value, values, 1U)
            != 1U) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::Orientation,
                                   MetadataConceptRole::Orientation,
                                   MetadataQuerySemanticKind::Orientation,
                                   MetadataQueryValueShape::Scalar, 100U);
        fill_numeric_candidate(&candidate, values, 1U);
        candidate.value_key = numeric_key(values[0]);
        append_candidate(out, candidate);
    }

    static void append_xmp_orientation_candidate(const MetaStore& store,
                                                 EntryId id, const Entry& entry,
                                                 MetadataConceptResolution* out)
    {
        const std::string_view path
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.property_path);
        if (!xmp_leaf_matches(path, "Orientation")) {
            return;
        }

        std::string text;
        if (!value_to_text(store.arena(), entry.value, &text)) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::Orientation,
                                   MetadataConceptRole::Orientation,
                                   MetadataQuerySemanticKind::Orientation,
                                   MetadataQueryValueShape::Scalar, 80U);
        double numeric = 0.0;
        if (parse_xmp_gps_coordinate(text, &numeric)) {
            fill_numeric_candidate(&candidate, &numeric, 1U);
            candidate.value_key = numeric_key(numeric);
        } else {
            normalize_text_key(text, &candidate.value_key);
        }
        candidate.text = text;
        append_candidate(out, candidate);
    }

    static void append_orientation_candidates(const MetaStore& store,
                                              MetadataConceptResolution* out)
    {
        append_interpretation_candidates(store, MetadataQueryKind::Orientation,
                                         MetadataConceptKind::Orientation,
                                         MetadataConceptRole::Orientation, 95U,
                                         out);

        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (exif_entry_tag(entry, kExifOrientationTag)) {
                append_exif_orientation_candidate(store, id, entry, out);
                continue;
            }
            if (entry.key.kind == MetaKeyKind::XmpProperty) {
                append_xmp_orientation_candidate(store, id, entry, out);
            }
        }
    }

    static void append_date_text_candidate(const MetaStore& store, EntryId id,
                                           MetadataConceptRole role,
                                           uint8_t priority,
                                           MetadataConceptResolution* out)
    {
        const Entry& entry = store.entry(id);
        std::string text;
        if (!value_to_text(store.arena(), entry.value, &text)) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::DateTime,
                                   role, MetadataQuerySemanticKind::Unknown,
                                   MetadataQueryValueShape::Text, priority);
        candidate.text = text;
        if (!fill_datetime_from_text(text, &candidate)) {
            normalize_text_key(text, &candidate.value_key);
        }
        append_candidate(out, candidate);
    }

    static void
    attach_exif_datetime_companions(const MetaStore& store, uint16_t offset_tag,
                                    uint16_t subsecond_tag,
                                    MetadataConceptCandidate* candidate)
    {
        if (!candidate || !candidate->has_date_time
            || !candidate->date_time_has_time) {
            return;
        }

        EntryId subsecond_id = kInvalidEntryId;
        std::string subsecond_text;
        std::string normalized_subsecond;
        bool has_subsecond_companion = false;
        if (find_exif_text_entry(store, "exififd", subsecond_tag, &subsecond_id,
                                 &subsecond_text)
            && normalize_subsecond_text(subsecond_text, &normalized_subsecond)) {
            candidate->date_time_has_subsecond = true;
            candidate->date_time_subsecond     = normalized_subsecond;
            add_unique_entry(&candidate->source_entries, subsecond_id);
            has_subsecond_companion = true;
        }

        EntryId offset_id = kInvalidEntryId;
        std::string offset_text;
        int16_t offset_min = 0;
        bool has_offset    = false;
        if (find_exif_text_entry(store, "exififd", offset_tag, &offset_id,
                                 &offset_text)
            && exif_timezone_offset_from_text(offset_text, &offset_min)) {
            set_datetime_timezone(candidate, true, offset_min);
            add_unique_entry(&candidate->source_entries, offset_id);
            has_offset = true;
        }

        if (has_subsecond_companion) {
            candidate->text.push_back('.');
            candidate->text.append(candidate->date_time_subsecond);
        }
        if (has_offset) {
            const std::string_view normalized_offset = trim_ascii_field(
                offset_text);
            candidate->text.append(normalized_offset);
        }
        set_datetime_precision(candidate);
        format_datetime_key(*candidate, &candidate->value_key);
    }

    static void append_exif_datetime_candidate(const MetaStore& store,
                                               EntryId id, const Entry& entry,
                                               MetadataConceptResolution* out)
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return;
        }
        MetadataConceptRole role = MetadataConceptRole::Primary;
        uint8_t priority         = 0U;
        uint16_t offset_tag      = 0U;
        uint16_t subsecond_tag   = 0U;
        std::string_view base_ifd;
        switch (entry.key.data.exif_tag.tag) {
        case kExifDateTimeOriginalTag:
            role          = MetadataConceptRole::Created;
            priority      = 100U;
            offset_tag    = kExifOffsetTimeOriginalTag;
            subsecond_tag = kExifSubSecTimeOriginalTag;
            base_ifd      = "exififd";
            break;
        case kExifDateTimeDigitizedTag:
            role          = MetadataConceptRole::Digitized;
            priority      = 90U;
            offset_tag    = kExifOffsetTimeDigitizedTag;
            subsecond_tag = kExifSubSecTimeDigitizedTag;
            base_ifd      = "exififd";
            break;
        case kExifDateTimeTag:
            role          = MetadataConceptRole::Modified;
            priority      = 80U;
            offset_tag    = kExifOffsetTimeTag;
            subsecond_tag = kExifSubSecTimeTag;
            base_ifd      = "ifd0";
            break;
        default: return;
        }
        if (!exif_entry_ifd_and_tag(store, entry, base_ifd,
                                    entry.key.data.exif_tag.tag)) {
            return;
        }

        std::string text;
        if (!value_to_text(store.arena(), entry.value, &text)) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::DateTime,
                                   role, MetadataQuerySemanticKind::Unknown,
                                   MetadataQueryValueShape::Text, priority);
        candidate.text = text;
        if (!fill_datetime_from_text(text, &candidate)) {
            normalize_text_key(text, &candidate.value_key);
        } else {
            attach_exif_datetime_companions(store, offset_tag, subsecond_tag,
                                            &candidate);
        }
        append_candidate(out, candidate);
    }

    static void append_xmp_datetime_candidate(const MetaStore& store,
                                              EntryId id, const Entry& entry,
                                              MetadataConceptResolution* out)
    {
        const std::string_view path
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.property_path);
        MetadataConceptRole role = MetadataConceptRole::Primary;
        uint8_t priority         = 0U;
        if (xmp_leaf_matches(path, "CreateDate")
            || xmp_leaf_matches(path, "DateCreated")) {
            role     = MetadataConceptRole::Created;
            priority = 95U;
        } else if (xmp_leaf_matches(path, "ModifyDate")) {
            role     = MetadataConceptRole::Modified;
            priority = 75U;
        } else if (xmp_leaf_matches(path, "MetadataDate")) {
            role     = MetadataConceptRole::MetadataDate;
            priority = 70U;
        } else if (xmp_leaf_matches(path, "DateTimeOriginal")) {
            role     = MetadataConceptRole::Created;
            priority = 90U;
        } else if (xmp_leaf_matches(path, "DateTimeDigitized")) {
            role     = MetadataConceptRole::Digitized;
            priority = 85U;
        } else {
            return;
        }
        append_date_text_candidate(store, id, role, priority, out);
    }

    static void append_iptc_datetime_candidate(const MetaStore& store,
                                               EntryId id, const Entry& entry,
                                               MetadataConceptResolution* out)
    {
        if (entry.key.kind != MetaKeyKind::IptcDataset) {
            return;
        }
        if (entry.key.data.iptc_dataset.record != 2U) {
            return;
        }
        if (entry.key.data.iptc_dataset.dataset == kIptcDateCreatedDataset) {
            append_date_text_candidate(store, id,
                                       MetadataConceptRole::DateCreated, 70U,
                                       out);
        } else if (entry.key.data.iptc_dataset.dataset
                   == kIptcTimeCreatedDataset) {
            append_date_text_candidate(store, id,
                                       MetadataConceptRole::Timestamp, 65U,
                                       out);
        } else if (entry.key.data.iptc_dataset.dataset
                   == kIptcDigitalCreationDateDataset) {
            append_date_text_candidate(store, id,
                                       MetadataConceptRole::Digitized, 68U,
                                       out);
        } else if (entry.key.data.iptc_dataset.dataset
                   == kIptcDigitalCreationTimeDataset) {
            append_date_text_candidate(store, id,
                                       MetadataConceptRole::Timestamp, 63U,
                                       out);
        }
    }

    static bool find_iptc_text_entry(const MetaStore& store, uint16_t record,
                                     uint16_t dataset, EntryId* out_id,
                                     std::string* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (entry.key.kind != MetaKeyKind::IptcDataset) {
                continue;
            }
            if (entry.key.data.iptc_dataset.record != record
                || entry.key.data.iptc_dataset.dataset != dataset) {
                continue;
            }
            if (!value_to_text(store.arena(), entry.value, out)) {
                continue;
            }
            if (out_id) {
                *out_id = id;
            }
            return true;
        }
        if (out_id) {
            *out_id = kInvalidEntryId;
        }
        if (out) {
            out->clear();
        }
        return false;
    }

    static void append_iptc_datetime_composite(const MetaStore& store,
                                               uint16_t date_dataset,
                                               uint16_t time_dataset,
                                               MetadataConceptRole role,
                                               uint8_t priority,
                                               MetadataConceptResolution* out)
    {
        EntryId date_id = kInvalidEntryId;
        EntryId time_id = kInvalidEntryId;
        std::string date_text;
        std::string time_text;
        if (!find_iptc_text_entry(store, 2U, date_dataset, &date_id,
                                  &date_text)) {
            return;
        }
        if (!find_iptc_text_entry(store, 2U, time_dataset, &time_id,
                                  &time_text)) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, date_id,
                                   MetadataConceptKind::DateTime, role,
                                   MetadataQuerySemanticKind::Unknown,
                                   MetadataQueryValueShape::Text, priority);
        candidate.text = date_text;
        candidate.text.push_back(' ');
        candidate.text.append(time_text);
        if (!fill_datetime_from_text(date_text, &candidate)) {
            return;
        }
        const Entry& time_entry = store.entry(time_id);
        uint8_t hour            = 0U;
        uint8_t minute          = 0U;
        uint8_t second          = 0U;
        bool has_offset         = false;
        int16_t offset          = 0;
        if (!fill_time_from_value(store.arena(), time_entry.value, &hour,
                                  &minute, &second, &has_offset, &offset)) {
            return;
        }
        (void)attach_time_to_candidate(&candidate, hour, minute, second,
                                       has_offset, offset);
        add_unique_entry(&candidate.source_entries, time_id);
        append_candidate(out, candidate);

        if (role == MetadataConceptRole::DateCreated) {
            MetadataConceptCandidate created_candidate = candidate;
            created_candidate.role     = MetadataConceptRole::Created;
            created_candidate.priority = 85U;
            append_candidate(out, created_candidate);
        }
    }

    static void append_datetime_candidates(const MetaStore& store,
                                           MetadataConceptResolution* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (entry.key.kind == MetaKeyKind::ExifTag) {
                append_exif_datetime_candidate(store, id, entry, out);
            } else if (entry.key.kind == MetaKeyKind::XmpProperty) {
                append_xmp_datetime_candidate(store, id, entry, out);
            } else if (entry.key.kind == MetaKeyKind::IptcDataset) {
                append_iptc_datetime_candidate(store, id, entry, out);
            }
        }
        append_iptc_datetime_composite(store, kIptcDateCreatedDataset,
                                       kIptcTimeCreatedDataset,
                                       MetadataConceptRole::DateCreated, 76U,
                                       out);
        append_iptc_datetime_composite(store, kIptcDigitalCreationDateDataset,
                                       kIptcDigitalCreationTimeDataset,
                                       MetadataConceptRole::Digitized, 74U,
                                       out);
    }

    static void
    copy_interpretation_values(const MetadataInterpretationRecord& record,
                               MetadataConceptCandidate* candidate);

    static bool is_dng_exposure_adjustment_tag(uint16_t tag) noexcept
    {
        switch (tag) {
        case kDngBaselineExposureTag:
        case kDngBaselineExposureOffsetTag:
        case kDngRawToPreviewGainTag:
        case kDngProfileGainTableMapTag:
        case kDngProfileGainTableMap2Tag: return true;
        default: break;
        }
        return false;
    }

    static MetadataConceptRole exposure_role_from_exif_tag(uint16_t tag) noexcept
    {
        switch (tag) {
        case kExifExposureTimeTag:
        case kExifShutterSpeedValueTag:
            return MetadataConceptRole::ExposureTime;
        case kExifFNumberTag:
        case kExifApertureValueTag:
        case kExifMaxApertureValueTag: return MetadataConceptRole::Aperture;
        case kExifPhotographicSensitivityTag:
        case kExifExposureIndexTag: return MetadataConceptRole::IsoSensitivity;
        case kExifExposureBiasValueTag:
            return MetadataConceptRole::ExposureBias;
        case kExifExposureProgramTag:
            return MetadataConceptRole::ExposureProgram;
        case kExifGainControlTag: return MetadataConceptRole::Gain;
        default: break;
        }
        if (is_dng_exposure_adjustment_tag(tag)) {
            return MetadataConceptRole::RawExposureAdjustment;
        }
        return MetadataConceptRole::Primary;
    }

    static bool name_in_list_ci(std::string_view name,
                                const std::string_view* names,
                                size_t count) noexcept
    {
        for (size_t i = 0U; i < count; ++i) {
            if (ascii_equal_ci(name, names[i])) {
                return true;
            }
        }
        return false;
    }

    static MetadataConceptRole
    exposure_role_from_exif_name(std::string_view name) noexcept
    {
        static constexpr std::string_view kExposureTimeNames[] = {
            "ExposureTime",        "TargetExposureTime", "ShutterSpeed",
            "ShutterSpeedSetting", "ShutterSpeedValue",
        };
        static constexpr std::string_view kApertureNames[] = {
            "Aperture",        "ApertureSetting",      "ApertureValue",
            "DisplayAperture", "EffectiveMaxAperture", "FNumber",
            "MaxAperture",     "MaxApertureValue",     "MinAperture",
            "TargetAperture",
        };
        static constexpr std::string_view kIsoNames[] = {
            "BaseISO",  "ISO",      "ISOSetting",
            "ISOSpeed", "ISOValue", "PhotographicSensitivity",
        };
        static constexpr std::string_view kExposureBiasNames[] = {
            "BaseExposureCompensation",    "CMExposureCompensation",
            "EasyExposureCompensation",    "ExposureBias",
            "ExposureBiasValue",           "ExposureCompensation",
            "ExposureCompensation2",       "ExposureCompensationSet",
            "ExposureCompensationSetting", "NetExposureCompensation",
            "RawDevExposureBiasValue",
        };
        static constexpr std::string_view kExposureProgramNames[] = {
            "AEProgramMode",
            "CanonExposureMode",
            "ExposureMode",
            "ExposureProgram",
        };
        static constexpr std::string_view kGainNames[] = {
            "GainControl",
        };

        if (name_in_list_ci(name, kExposureTimeNames,
                            std::size(kExposureTimeNames))) {
            return MetadataConceptRole::ExposureTime;
        }
        if (name_in_list_ci(name, kApertureNames, std::size(kApertureNames))) {
            return MetadataConceptRole::Aperture;
        }
        if (name_in_list_ci(name, kIsoNames, std::size(kIsoNames))) {
            return MetadataConceptRole::IsoSensitivity;
        }
        if (name_in_list_ci(name, kExposureBiasNames,
                            std::size(kExposureBiasNames))) {
            return MetadataConceptRole::ExposureBias;
        }
        if (name_in_list_ci(name, kExposureProgramNames,
                            std::size(kExposureProgramNames))) {
            return MetadataConceptRole::ExposureProgram;
        }
        if (name_in_list_ci(name, kGainNames, std::size(kGainNames))) {
            return MetadataConceptRole::Gain;
        }
        return MetadataConceptRole::Primary;
    }

    static bool is_raw_xmp_namespace(std::string_view ns) noexcept
    {
        return ascii_contains_ci(ns, "camera-raw-settings")
               || ascii_contains_ci(ns, "/crs/")
               || ascii_contains_ci(ns, "photoshop/camera/raw")
               || ascii_contains_ci(ns, "crs/1.0");
    }

    static MetadataConceptRole
    exposure_role_from_xmp_path(std::string_view ns,
                                std::string_view path) noexcept
    {
        if (ascii_contains_ci(path, "ExposureTime")
            || ascii_contains_ci(path, "ShutterSpeed")) {
            return MetadataConceptRole::ExposureTime;
        }
        if (ascii_contains_ci(path, "FNumber")
            || ascii_contains_ci(path, "Aperture")) {
            return MetadataConceptRole::Aperture;
        }
        if (ascii_contains_ci(path, "PhotographicSensitivity")
            || ascii_contains_ci(path, "ISOSpeed")
            || xmp_leaf_matches(path, "ISO")) {
            return MetadataConceptRole::IsoSensitivity;
        }
        if (ascii_contains_ci(path, "ExposureBias")
            || ascii_contains_ci(path, "ExposureCompensation")) {
            return MetadataConceptRole::ExposureBias;
        }
        if (ascii_contains_ci(path, "ExposureProgram")) {
            return MetadataConceptRole::ExposureProgram;
        }
        if (is_raw_xmp_namespace(ns)
            && (ascii_contains_ci(path, "Exposure")
                || ascii_contains_ci(path, "Gain"))) {
            return MetadataConceptRole::RawExposureAdjustment;
        }
        if (ascii_contains_ci(path, "Gain")) {
            return MetadataConceptRole::Gain;
        }
        return MetadataConceptRole::Primary;
    }

    static MetadataConceptRole
    exposure_role_from_record(const MetaStore& store,
                              const MetadataInterpretationRecord& record)
    {
        if (!record.source_entries.empty()) {
            const EntryId entry_id = record.source_entries[0];
            if (entry_id != kInvalidEntryId) {
                const Entry& entry = store.entry(entry_id);
                if (entry.key.kind == MetaKeyKind::ExifTag) {
                    const MetadataConceptRole tag_role
                        = exposure_role_from_exif_tag(
                            entry.key.data.exif_tag.tag);
                    if (tag_role != MetadataConceptRole::Primary) {
                        return tag_role;
                    }
                    return exposure_role_from_exif_name(
                        exif_entry_name(store, entry,
                                        ExifTagNamePolicy::ExifToolCompat));
                }
                if (entry.key.kind == MetaKeyKind::XmpProperty) {
                    const std::string_view ns
                        = arena_string(store.arena(),
                                       entry.key.data.xmp_property.schema_ns);
                    const std::string_view path = arena_string(
                        store.arena(),
                        entry.key.data.xmp_property.property_path);
                    return exposure_role_from_xmp_path(ns, path);
                }
            }
        }

        switch (record.semantic) {
        case MetadataQuerySemanticKind::Gain: return MetadataConceptRole::Gain;
        case MetadataQuerySemanticKind::ExposureGain:
            return MetadataConceptRole::RawExposureAdjustment;
        case MetadataQuerySemanticKind::Exposure:
            return MetadataConceptRole::Primary;
        case MetadataQuerySemanticKind::Unknown:
        case MetadataQuerySemanticKind::Crop:
        case MetadataQuerySemanticKind::Border:
        case MetadataQuerySemanticKind::ActiveArea:
        case MetadataQuerySemanticKind::Color:
        case MetadataQuerySemanticKind::ColorProfile:
        case MetadataQuerySemanticKind::WhiteBalance:
        case MetadataQuerySemanticKind::ColorMatrix:
        case MetadataQuerySemanticKind::SourceColorTransform:
        case MetadataQuerySemanticKind::LensCorrection:
        case MetadataQuerySemanticKind::Orientation:
        case MetadataQuerySemanticKind::BlackLevel:
        case MetadataQuerySemanticKind::WhiteLevel:
        case MetadataQuerySemanticKind::Linearization:
        case MetadataQuerySemanticKind::RawValueCurve:
        case MetadataQuerySemanticKind::RawLinearityLimit:
        case MetadataQuerySemanticKind::RawCalibrationCurve:
        case MetadataQuerySemanticKind::RawCurveControlPoints:
        case MetadataQuerySemanticKind::CfaLayout:
        case MetadataQuerySemanticKind::SensorGeometry:
        case MetadataQuerySemanticKind::RawStorage:
        case MetadataQuerySemanticKind::SourceProcessing:
        case MetadataQuerySemanticKind::ComputationalProcessing:
        case MetadataQuerySemanticKind::ThermalProcessing:
        case MetadataQuerySemanticKind::StitchProcessing:
        case MetadataQuerySemanticKind::Title:
        case MetadataQuerySemanticKind::Description:
        case MetadataQuerySemanticKind::Creator:
        case MetadataQuerySemanticKind::Keywords:
        case MetadataQuerySemanticKind::Rights:
        case MetadataQuerySemanticKind::License:
        case MetadataQuerySemanticKind::Credit:
        case MetadataQuerySemanticKind::Source:
        case MetadataQuerySemanticKind::Contact:
        case MetadataQuerySemanticKind::Event:
        case MetadataQuerySemanticKind::Person:
        case MetadataQuerySemanticKind::Organization:
        case MetadataQuerySemanticKind::Product:
        case MetadataQuerySemanticKind::Artwork:
        case MetadataQuerySemanticKind::RightsExpression:
        case MetadataQuerySemanticKind::Release:
        case MetadataQuerySemanticKind::Editorial:
        case MetadataQuerySemanticKind::Accessibility:
        case MetadataQuerySemanticKind::Taxonomy:
        case MetadataQuerySemanticKind::DocumentIdentity:
        case MetadataQuerySemanticKind::Registry:
        case MetadataQuerySemanticKind::ImageRegion:
        case MetadataQuerySemanticKind::DocumentLineage:
        case MetadataQuerySemanticKind::DocumentHistory: break;
        }
        return MetadataConceptRole::Primary;
    }

    static bool double_to_u64_enum(double value, uint64_t* out) noexcept
    {
        if (!out || value < 0.0 || value > 4294967295.0
            || std::floor(value) != value) {
            return false;
        }
        *out = static_cast<uint64_t>(value);
        return true;
    }

    static const char* exposure_exif_value_label(
        const MetaStore& store, const Entry& entry,
        const MetadataConceptCandidate& candidate) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return "";
        }

        double value = 0.0;
        if (candidate.has_values && candidate.values.size() == 1U) {
            value = candidate.values[0];
        } else {
            double values[1] {};
            if (value_to_numeric_array(store.arena(), entry.value, values, 1U)
                != 1U) {
                return "";
            }
            value = values[0];
        }

        uint64_t enum_value = 0U;
        if (!double_to_u64_enum(value, &enum_value)) {
            return "";
        }
        const uint16_t tag         = entry.key.data.exif_tag.tag;
        const std::string_view ifd = arena_string(store.arena(),
                                                  entry.key.data.exif_tag.ifd);
        return exif_tag_numeric_value_name(ifd, tag, enum_value);
    }

    static void apply_exposure_display_text(const MetaStore& store,
                                            EntryId entry_id,
                                            MetadataConceptCandidate* candidate)
    {
        if (!candidate || entry_id == kInvalidEntryId) {
            return;
        }
        const Entry& entry = store.entry(entry_id);
        const char* label = exposure_exif_value_label(store, entry, *candidate);
        if (!label || label[0] == '\0') {
            return;
        }
        candidate->text = label;
        normalize_text_key(candidate->text, &candidate->value_key);
    }

    static void append_exposure_candidates(const MetaStore& store,
                                           MetadataConceptResolution* out)
    {
        MetadataInterpretationResult result
            = interpret_metadata_query(store, MetadataQueryKind::ExposureGain);
        for (size_t i = 0U; i < result.records.size(); ++i) {
            const MetadataInterpretationRecord& record = result.records[i];
            if (record.source_entries.empty()) {
                continue;
            }
            const EntryId entry_id = record.source_entries[0];
            if (entry_id == kInvalidEntryId) {
                continue;
            }
            const MetadataConceptRole role = exposure_role_from_record(store,
                                                                       record);
            MetadataConceptCandidate candidate = make_entry_candidate(
                store, entry_id, MetadataConceptKind::Exposure, role,
                record.semantic, record.shape, record.confidence);
            candidate.source_entries.clear();
            for (size_t e = 0U; e < record.source_entries.size(); ++e) {
                add_unique_entry(&candidate.source_entries,
                                 record.source_entries[e]);
            }
            copy_interpretation_values(record, &candidate);
            apply_exposure_display_text(store, entry_id, &candidate);
            append_candidate(out, candidate);
        }
    }

    static MetadataConceptRole
    color_role_from_semantic(MetadataQuerySemanticKind semantic) noexcept
    {
        switch (semantic) {
        case MetadataQuerySemanticKind::ColorMatrix:
            return MetadataConceptRole::ColorMatrix;
        case MetadataQuerySemanticKind::WhiteBalance:
            return MetadataConceptRole::WhiteBalance;
        case MetadataQuerySemanticKind::ColorProfile:
            return MetadataConceptRole::IccProfile;
        case MetadataQuerySemanticKind::SourceColorTransform:
            return MetadataConceptRole::SourceColorTransform;
        case MetadataQuerySemanticKind::Color:
            return MetadataConceptRole::Primary;
        case MetadataQuerySemanticKind::Unknown:
        case MetadataQuerySemanticKind::Crop:
        case MetadataQuerySemanticKind::Border:
        case MetadataQuerySemanticKind::ActiveArea:
        case MetadataQuerySemanticKind::Exposure:
        case MetadataQuerySemanticKind::Gain:
        case MetadataQuerySemanticKind::LensCorrection:
        case MetadataQuerySemanticKind::Orientation:
        case MetadataQuerySemanticKind::ExposureGain:
        case MetadataQuerySemanticKind::BlackLevel:
        case MetadataQuerySemanticKind::WhiteLevel:
        case MetadataQuerySemanticKind::Linearization:
        case MetadataQuerySemanticKind::RawValueCurve:
        case MetadataQuerySemanticKind::RawLinearityLimit:
        case MetadataQuerySemanticKind::RawCalibrationCurve:
        case MetadataQuerySemanticKind::RawCurveControlPoints:
        case MetadataQuerySemanticKind::CfaLayout:
        case MetadataQuerySemanticKind::SensorGeometry:
        case MetadataQuerySemanticKind::RawStorage:
        case MetadataQuerySemanticKind::SourceProcessing:
        case MetadataQuerySemanticKind::ComputationalProcessing:
        case MetadataQuerySemanticKind::ThermalProcessing:
        case MetadataQuerySemanticKind::StitchProcessing:
        case MetadataQuerySemanticKind::Title:
        case MetadataQuerySemanticKind::Description:
        case MetadataQuerySemanticKind::Creator:
        case MetadataQuerySemanticKind::Keywords:
        case MetadataQuerySemanticKind::Rights:
        case MetadataQuerySemanticKind::License:
        case MetadataQuerySemanticKind::Credit:
        case MetadataQuerySemanticKind::Source:
        case MetadataQuerySemanticKind::Contact:
        case MetadataQuerySemanticKind::Event:
        case MetadataQuerySemanticKind::Person:
        case MetadataQuerySemanticKind::Organization:
        case MetadataQuerySemanticKind::Product:
        case MetadataQuerySemanticKind::Artwork:
        case MetadataQuerySemanticKind::RightsExpression:
        case MetadataQuerySemanticKind::Release:
        case MetadataQuerySemanticKind::Editorial:
        case MetadataQuerySemanticKind::Accessibility:
        case MetadataQuerySemanticKind::Taxonomy:
        case MetadataQuerySemanticKind::DocumentIdentity:
        case MetadataQuerySemanticKind::Registry:
        case MetadataQuerySemanticKind::ImageRegion:
        case MetadataQuerySemanticKind::DocumentLineage:
        case MetadataQuerySemanticKind::DocumentHistory: break;
        }
        return MetadataConceptRole::Primary;
    }

    static MetadataConceptRole
    color_role_from_record(const MetaStore& store,
                           const MetadataInterpretationRecord& record)
    {
        if (!record.source_entries.empty()) {
            const EntryId entry_id = record.source_entries[0];
            if (entry_id != kInvalidEntryId) {
                const Entry& entry = store.entry(entry_id);
                if (entry.key.kind == MetaKeyKind::ExifTag
                    && entry.key.data.exif_tag.tag == kExifColorSpaceTag) {
                    return MetadataConceptRole::ColorSpace;
                }
                if (entry.key.kind == MetaKeyKind::IccHeaderField) {
                    if (entry.key.data.icc_header_field.offset
                        == kIccHeaderRgbColorSpaceOffset) {
                        return MetadataConceptRole::ColorSpace;
                    }
                    return MetadataConceptRole::IccProfile;
                }
                if (entry.key.kind == MetaKeyKind::IccTag
                    || entry.key.kind == MetaKeyKind::PngText) {
                    return MetadataConceptRole::IccProfile;
                }
                if (entry.key.kind == MetaKeyKind::XmpProperty) {
                    const std::string_view path = arena_string(
                        store.arena(),
                        entry.key.data.xmp_property.property_path);
                    if (xmp_leaf_matches(path, "ICCProfile")
                        || xmp_leaf_matches(path, "ICCProfileName")
                        || ascii_contains_ci(path, "iccprofile")) {
                        return MetadataConceptRole::IccProfile;
                    }
                    if (xmp_leaf_matches(path, "ColorSpace")
                        || ascii_contains_ci(path, "colorspace")) {
                        return MetadataConceptRole::ColorSpace;
                    }
                }
            }
        }
        return color_role_from_semantic(record.semantic);
    }

    static MetadataConceptRole
    lens_role_from_semantic(MetadataQuerySemanticKind semantic) noexcept
    {
        if (semantic == MetadataQuerySemanticKind::LensCorrection) {
            return MetadataConceptRole::LensCorrection;
        }
        return MetadataConceptRole::Primary;
    }

    static MetadataConceptRole
    raw_role_from_semantic(MetadataQuerySemanticKind semantic) noexcept
    {
        switch (semantic) {
        case MetadataQuerySemanticKind::BlackLevel:
            return MetadataConceptRole::BlackLevel;
        case MetadataQuerySemanticKind::WhiteLevel:
            return MetadataConceptRole::WhiteLevel;
        case MetadataQuerySemanticKind::Linearization:
            return MetadataConceptRole::Linearization;
        case MetadataQuerySemanticKind::RawValueCurve:
            return MetadataConceptRole::RawValueCurve;
        case MetadataQuerySemanticKind::RawLinearityLimit:
            return MetadataConceptRole::RawLinearityLimit;
        case MetadataQuerySemanticKind::RawCalibrationCurve:
            return MetadataConceptRole::RawCalibrationCurve;
        case MetadataQuerySemanticKind::RawCurveControlPoints:
            return MetadataConceptRole::RawCurveControlPoints;
        case MetadataQuerySemanticKind::CfaLayout:
            return MetadataConceptRole::CfaLayout;
        case MetadataQuerySemanticKind::SensorGeometry:
            return MetadataConceptRole::SensorGeometry;
        case MetadataQuerySemanticKind::RawStorage:
            return MetadataConceptRole::RawStorage;
        case MetadataQuerySemanticKind::SourceProcessing:
            return MetadataConceptRole::SourceProcessing;
        case MetadataQuerySemanticKind::ComputationalProcessing:
            return MetadataConceptRole::ComputationalProcessing;
        case MetadataQuerySemanticKind::ThermalProcessing:
            return MetadataConceptRole::ThermalProcessing;
        case MetadataQuerySemanticKind::StitchProcessing:
            return MetadataConceptRole::StitchProcessing;
        case MetadataQuerySemanticKind::Unknown:
        case MetadataQuerySemanticKind::Crop:
        case MetadataQuerySemanticKind::Border:
        case MetadataQuerySemanticKind::ActiveArea:
        case MetadataQuerySemanticKind::Exposure:
        case MetadataQuerySemanticKind::Gain:
        case MetadataQuerySemanticKind::Color:
        case MetadataQuerySemanticKind::ColorProfile:
        case MetadataQuerySemanticKind::WhiteBalance:
        case MetadataQuerySemanticKind::ColorMatrix:
        case MetadataQuerySemanticKind::SourceColorTransform:
        case MetadataQuerySemanticKind::LensCorrection:
        case MetadataQuerySemanticKind::Orientation:
        case MetadataQuerySemanticKind::ExposureGain:
        case MetadataQuerySemanticKind::Title:
        case MetadataQuerySemanticKind::Description:
        case MetadataQuerySemanticKind::Creator:
        case MetadataQuerySemanticKind::Keywords:
        case MetadataQuerySemanticKind::Rights:
        case MetadataQuerySemanticKind::License:
        case MetadataQuerySemanticKind::Credit:
        case MetadataQuerySemanticKind::Source:
        case MetadataQuerySemanticKind::Contact:
        case MetadataQuerySemanticKind::Event:
        case MetadataQuerySemanticKind::Person:
        case MetadataQuerySemanticKind::Organization:
        case MetadataQuerySemanticKind::Product:
        case MetadataQuerySemanticKind::Artwork:
        case MetadataQuerySemanticKind::RightsExpression:
        case MetadataQuerySemanticKind::Release:
        case MetadataQuerySemanticKind::Editorial:
        case MetadataQuerySemanticKind::Accessibility:
        case MetadataQuerySemanticKind::Taxonomy:
        case MetadataQuerySemanticKind::DocumentIdentity:
        case MetadataQuerySemanticKind::Registry:
        case MetadataQuerySemanticKind::ImageRegion:
        case MetadataQuerySemanticKind::DocumentLineage:
        case MetadataQuerySemanticKind::DocumentHistory: break;
        }
        return MetadataConceptRole::Primary;
    }

    static void
    copy_interpretation_values(const MetadataInterpretationRecord& record,
                               MetadataConceptCandidate* candidate)
    {
        if (!candidate) {
            return;
        }
        if (!record.has_values || record.values.empty()) {
            return;
        }
        fill_values_candidate(candidate, record.values);
        candidate->value_key = values_key(record.values);
    }

    typedef MetadataConceptRole (*ConceptRoleFromSemanticFn)(
        MetadataQuerySemanticKind) noexcept;

    static void append_query_concept_candidates(
        const MetaStore& store, MetadataQueryKind query_kind,
        MetadataConceptKind concept_kind, ConceptRoleFromSemanticFn role_fn,
        uint8_t default_priority, MetadataConceptResolution* out)
    {
        MetadataInterpretationResult result
            = interpret_metadata_query(store, query_kind);
        for (size_t i = 0U; i < result.records.size(); ++i) {
            const MetadataInterpretationRecord& record = result.records[i];
            if (!role_fn || record.source_entries.empty()) {
                continue;
            }
            const MetadataConceptRole role = role_fn(record.semantic);
            const EntryId entry_id         = record.source_entries[0];
            if (entry_id == kInvalidEntryId) {
                continue;
            }
            MetadataConceptCandidate candidate = make_entry_candidate(
                store, entry_id, concept_kind, role, record.semantic,
                record.shape,
                record.confidence != 0U ? record.confidence : default_priority);
            candidate.source_entries.clear();
            for (size_t e = 0U; e < record.source_entries.size(); ++e) {
                add_unique_entry(&candidate.source_entries,
                                 record.source_entries[e]);
            }
            copy_interpretation_values(record, &candidate);
            append_candidate(out, candidate);
        }
    }

    static void
    append_color_query_concept_candidates(const MetaStore& store,
                                          MetadataQueryKind query_kind,
                                          MetadataConceptResolution* out)
    {
        MetadataInterpretationResult result
            = interpret_metadata_query(store, query_kind);
        for (size_t i = 0U; i < result.records.size(); ++i) {
            const MetadataInterpretationRecord& record = result.records[i];
            if (record.source_entries.empty()) {
                continue;
            }
            const EntryId entry_id = record.source_entries[0];
            if (entry_id == kInvalidEntryId) {
                continue;
            }
            const MetadataConceptRole role     = color_role_from_record(store,
                                                                        record);
            MetadataConceptCandidate candidate = make_entry_candidate(
                store, entry_id, MetadataConceptKind::ColorProfile, role,
                record.semantic, record.shape,
                record.confidence != 0U ? record.confidence : 60U);
            candidate.source_entries.clear();
            for (size_t e = 0U; e < record.source_entries.size(); ++e) {
                add_unique_entry(&candidate.source_entries,
                                 record.source_entries[e]);
            }
            copy_interpretation_values(record, &candidate);
            append_candidate(out, candidate);
        }
    }

    static void
    append_color_interpretation_candidates(const MetaStore& store,
                                           MetadataConceptResolution* out)
    {
        append_color_query_concept_candidates(store, MetadataQueryKind::Color,
                                              out);
        append_color_query_concept_candidates(store,
                                              MetadataQueryKind::WhiteBalance,
                                              out);
    }

    static void append_lens_correction_candidates(const MetaStore& store,
                                                  MetadataConceptResolution* out)
    {
        append_query_concept_candidates(store,
                                        MetadataQueryKind::LensCorrection,
                                        MetadataConceptKind::LensCorrection,
                                        lens_role_from_semantic, 70U, out);
    }

    static void append_raw_processing_candidates(const MetaStore& store,
                                                 MetadataConceptResolution* out)
    {
        append_query_concept_candidates(store, MetadataQueryKind::RawProcessing,
                                        MetadataConceptKind::RawProcessing,
                                        raw_role_from_semantic, 70U, out);
    }

    static void append_exif_colorspace_candidate(const MetaStore& store,
                                                 EntryId id, const Entry& entry,
                                                 MetadataConceptResolution* out)
    {
        double values[1] {};
        if (value_to_numeric_array(store.arena(), entry.value, values, 1U)
            != 1U) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::ColorProfile,
                                   MetadataConceptRole::ColorSpace,
                                   MetadataQuerySemanticKind::ColorProfile,
                                   MetadataQueryValueShape::Scalar, 90U);
        fill_numeric_candidate(&candidate, values, 1U);
        candidate.value_key = numeric_key(values[0]);
        append_candidate(out, candidate);
    }

    static void append_icc_candidate(const MetaStore& store, EntryId id,
                                     const Entry& entry,
                                     MetadataConceptResolution* out)
    {
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::ColorProfile,
                                   MetadataConceptRole::IccProfile,
                                   MetadataQuerySemanticKind::ColorProfile,
                                   MetadataQueryValueShape::Blob, 100U);
        if (entry.key.kind == MetaKeyKind::IccHeaderField
            && entry.key.data.icc_header_field.offset
                   == kIccHeaderRgbColorSpaceOffset) {
            candidate.role  = MetadataConceptRole::ColorSpace;
            candidate.shape = MetadataQueryValueShape::Scalar;
            double values[1] {};
            if (value_to_numeric_array(store.arena(), entry.value, values, 1U)
                == 1U) {
                fill_numeric_candidate(&candidate, values, 1U);
            }
        }
        append_candidate(out, candidate);
    }

    static void append_xmp_color_candidate(const MetaStore& store, EntryId id,
                                           const Entry& entry,
                                           MetadataConceptResolution* out)
    {
        const std::string_view path
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.property_path);
        MetadataConceptRole role = MetadataConceptRole::Primary;
        uint8_t priority         = 0U;
        if (xmp_leaf_matches(path, "ICCProfile")
            || xmp_leaf_matches(path, "ICCProfileName")
            || ascii_contains_ci(path, "iccprofile")) {
            role     = MetadataConceptRole::IccProfile;
            priority = 80U;
        } else if (xmp_leaf_matches(path, "ColorSpace")
                   || ascii_contains_ci(path, "colorspace")) {
            role     = MetadataConceptRole::ColorSpace;
            priority = 75U;
        } else {
            return;
        }

        std::string text;
        (void)value_to_text(store.arena(), entry.value, &text);
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::ColorProfile,
                                   role,
                                   MetadataQuerySemanticKind::ColorProfile,
                                   MetadataQueryValueShape::Text, priority);
        candidate.text = text;
        normalize_text_key(text, &candidate.value_key);
        append_candidate(out, candidate);
    }

    static void append_png_color_candidate(const MetaStore& store, EntryId id,
                                           const Entry& entry,
                                           MetadataConceptResolution* out)
    {
        const std::string_view keyword
            = arena_string(store.arena(), entry.key.data.png_text.keyword);
        if (!ascii_contains_ci(keyword, "icc")
            && !ascii_contains_ci(keyword, "profile")
            && !ascii_contains_ci(keyword, "colorspace")) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::ColorProfile,
                                   MetadataConceptRole::IccProfile,
                                   MetadataQuerySemanticKind::ColorProfile,
                                   MetadataQueryValueShape::Text, 65U);
        value_to_text(store.arena(), entry.value, &candidate.text);
        normalize_text_key(candidate.text, &candidate.value_key);
        append_candidate(out, candidate);
    }

    static void append_color_profile_candidates(const MetaStore& store,
                                                MetadataConceptResolution* out)
    {
        append_color_interpretation_candidates(store, out);
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (exif_entry_tag(entry, kExifColorSpaceTag)) {
                append_exif_colorspace_candidate(store, id, entry, out);
            } else if (entry.key.kind == MetaKeyKind::IccHeaderField
                       || entry.key.kind == MetaKeyKind::IccTag) {
                append_icc_candidate(store, id, entry, out);
            } else if (entry.key.kind == MetaKeyKind::XmpProperty) {
                append_xmp_color_candidate(store, id, entry, out);
            } else if (entry.key.kind == MetaKeyKind::PngText) {
                append_png_color_candidate(store, id, entry, out);
            }
        }
    }

    static MetadataConceptRole
    geometry_role_from_semantic(MetadataQuerySemanticKind semantic) noexcept
    {
        switch (semantic) {
        case MetadataQuerySemanticKind::Crop: return MetadataConceptRole::Crop;
        case MetadataQuerySemanticKind::ActiveArea:
            return MetadataConceptRole::ActiveArea;
        case MetadataQuerySemanticKind::Border:
            return MetadataConceptRole::Border;
        case MetadataQuerySemanticKind::SensorGeometry:
            return MetadataConceptRole::SensorGeometry;
        case MetadataQuerySemanticKind::Unknown:
        case MetadataQuerySemanticKind::Exposure:
        case MetadataQuerySemanticKind::Gain:
        case MetadataQuerySemanticKind::Color:
        case MetadataQuerySemanticKind::ColorProfile:
        case MetadataQuerySemanticKind::WhiteBalance:
        case MetadataQuerySemanticKind::ColorMatrix:
        case MetadataQuerySemanticKind::SourceColorTransform:
        case MetadataQuerySemanticKind::LensCorrection:
        case MetadataQuerySemanticKind::Orientation:
        case MetadataQuerySemanticKind::ExposureGain:
        case MetadataQuerySemanticKind::BlackLevel:
        case MetadataQuerySemanticKind::WhiteLevel:
        case MetadataQuerySemanticKind::Linearization:
        case MetadataQuerySemanticKind::CfaLayout:
        case MetadataQuerySemanticKind::RawStorage:
        case MetadataQuerySemanticKind::SourceProcessing:
        case MetadataQuerySemanticKind::ComputationalProcessing:
        case MetadataQuerySemanticKind::ThermalProcessing:
        case MetadataQuerySemanticKind::StitchProcessing:
        case MetadataQuerySemanticKind::Title:
        case MetadataQuerySemanticKind::Description:
        case MetadataQuerySemanticKind::Creator:
        case MetadataQuerySemanticKind::Keywords:
        case MetadataQuerySemanticKind::RawValueCurve:
        case MetadataQuerySemanticKind::RawLinearityLimit:
        case MetadataQuerySemanticKind::RawCalibrationCurve:
        case MetadataQuerySemanticKind::RawCurveControlPoints:
        case MetadataQuerySemanticKind::Rights:
        case MetadataQuerySemanticKind::License:
        case MetadataQuerySemanticKind::Credit:
        case MetadataQuerySemanticKind::Source:
        case MetadataQuerySemanticKind::Contact:
        case MetadataQuerySemanticKind::Event:
        case MetadataQuerySemanticKind::Person:
        case MetadataQuerySemanticKind::Organization:
        case MetadataQuerySemanticKind::Product:
        case MetadataQuerySemanticKind::Artwork:
        case MetadataQuerySemanticKind::RightsExpression:
        case MetadataQuerySemanticKind::Release:
        case MetadataQuerySemanticKind::Editorial:
        case MetadataQuerySemanticKind::Accessibility:
        case MetadataQuerySemanticKind::Taxonomy:
        case MetadataQuerySemanticKind::DocumentIdentity:
        case MetadataQuerySemanticKind::Registry:
        case MetadataQuerySemanticKind::ImageRegion:
        case MetadataQuerySemanticKind::DocumentLineage:
        case MetadataQuerySemanticKind::DocumentHistory: break;
        }
        return MetadataConceptRole::Primary;
    }

    static void
    copy_interpretation_geometry(const MetadataInterpretationRecord& record,
                                 MetadataConceptCandidate* candidate)
    {
        if (!candidate) {
            return;
        }
        fill_pair_candidate(record.has_origin, record.origin,
                            &candidate->has_origin, candidate->origin);
        fill_pair_candidate(record.has_size, record.size, &candidate->has_size,
                            candidate->size);
        fill_quad_candidate(record.has_rect, record.rect, &candidate->has_rect,
                            candidate->rect);
        fill_quad_candidate(record.has_margins, record.margins,
                            &candidate->has_margins, candidate->margins);
        if (record.has_values && !record.values.empty()) {
            fill_values_candidate(candidate, record.values);
        }
        candidate->value_key = geometry_key(*candidate);
    }

    static void append_geometry_candidates(const MetaStore& store,
                                           MetadataConceptResolution* out)
    {
        if (!out) {
            return;
        }
        MetadataInterpretationResult result
            = interpret_metadata_query(store, MetadataQueryKind::Crop);
        for (size_t i = 0U; i < result.records.size(); ++i) {
            const MetadataInterpretationRecord& record = result.records[i];
            const MetadataConceptRole role = geometry_role_from_semantic(
                record.semantic);
            if (role == MetadataConceptRole::Primary) {
                continue;
            }
            if (record.source_entries.empty()) {
                continue;
            }
            const EntryId entry_id = record.source_entries[0];
            if (entry_id == kInvalidEntryId) {
                continue;
            }
            MetadataConceptCandidate candidate = make_entry_candidate(
                store, entry_id, MetadataConceptKind::Geometry, role,
                record.semantic, record.shape, record.confidence);
            candidate.source_entries.clear();
            for (size_t e = 0U; e < record.source_entries.size(); ++e) {
                add_unique_entry(&candidate.source_entries,
                                 record.source_entries[e]);
            }
            copy_interpretation_geometry(record, &candidate);
            append_candidate(out, candidate);
        }
    }

    static void append_gps_numeric_candidate(const MetaStore& store, EntryId id,
                                             MetadataConceptRole role,
                                             double value, EntryId ref_id,
                                             uint8_t priority,
                                             MetadataConceptResolution* out)
    {
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::Gps, role,
                                   MetadataQuerySemanticKind::Unknown,
                                   MetadataQueryValueShape::Scalar, priority);
        fill_numeric_candidate(&candidate, &value, 1U);
        candidate.value_key = gps_numeric_key(value);
        add_unique_entry(&candidate.source_entries, ref_id);
        append_candidate(out, candidate);
    }

    static void append_exif_gps_candidate(const MetaStore& store, EntryId id,
                                          const Entry& entry,
                                          MetadataConceptResolution* out)
    {
        if (!exif_entry_ifd_and_tag(store, entry, "gpsifd",
                                    entry.key.data.exif_tag.tag)) {
            return;
        }

        if (entry.key.data.exif_tag.tag == kGpsLatitudeTag) {
            EntryId ref_id = kInvalidEntryId;
            std::string ref;
            (void)find_exif_text_entry(store, "gpsifd", kGpsLatitudeRefTag,
                                       &ref_id, &ref);
            double value = 0.0;
            if (gps_coordinate_from_value(store, entry.value, ref, &value)) {
                append_gps_numeric_candidate(store, id,
                                             MetadataConceptRole::Latitude,
                                             value, ref_id, 100U, out);
            }
        } else if (entry.key.data.exif_tag.tag == kGpsLongitudeTag) {
            EntryId ref_id = kInvalidEntryId;
            std::string ref;
            (void)find_exif_text_entry(store, "gpsifd", kGpsLongitudeRefTag,
                                       &ref_id, &ref);
            double value = 0.0;
            if (gps_coordinate_from_value(store, entry.value, ref, &value)) {
                append_gps_numeric_candidate(store, id,
                                             MetadataConceptRole::Longitude,
                                             value, ref_id, 100U, out);
            }
        } else if (entry.key.data.exif_tag.tag == kGpsDestLatitudeTag) {
            EntryId ref_id = kInvalidEntryId;
            std::string ref;
            (void)find_exif_text_entry(store, "gpsifd", kGpsDestLatitudeRefTag,
                                       &ref_id, &ref);
            double value = 0.0;
            if (gps_coordinate_from_value(store, entry.value, ref, &value)) {
                append_gps_numeric_candidate(
                    store, id, MetadataConceptRole::DestinationLatitude, value,
                    ref_id, 95U, out);
            }
        } else if (entry.key.data.exif_tag.tag == kGpsDestLongitudeTag) {
            EntryId ref_id = kInvalidEntryId;
            std::string ref;
            (void)find_exif_text_entry(store, "gpsifd", kGpsDestLongitudeRefTag,
                                       &ref_id, &ref);
            double value = 0.0;
            if (gps_coordinate_from_value(store, entry.value, ref, &value)) {
                append_gps_numeric_candidate(
                    store, id, MetadataConceptRole::DestinationLongitude, value,
                    ref_id, 95U, out);
            }
        } else if (entry.key.data.exif_tag.tag == kGpsAltitudeTag) {
            double value = 0.0;
            if (value_to_numeric_array(store.arena(), entry.value, &value, 1U)
                == 1U) {
                double ref         = 0.0;
                EntryId ref_id     = kInvalidEntryId;
                const bool has_ref = find_exif_numeric_entry(store, "gpsifd",
                                                             kGpsAltitudeRefTag,
                                                             &ref_id, &ref);
                const bool below_sea_level = has_ref && ref > 0.0;
                if (below_sea_level) {
                    value = -std::fabs(value);
                }
                MetadataConceptCandidate candidate
                    = make_entry_candidate(store, id, MetadataConceptKind::Gps,
                                           MetadataConceptRole::Altitude,
                                           MetadataQuerySemanticKind::Unknown,
                                           MetadataQueryValueShape::Scalar,
                                           90U);
                fill_numeric_candidate(&candidate, &value, 1U);
                candidate.value_key = gps_numeric_key(value);
                if (has_ref) {
                    candidate.has_gps_altitude_reference   = true;
                    candidate.gps_altitude_below_sea_level = below_sea_level;
                    candidate.gps_altitude_reference_code
                        = static_cast<uint8_t>(ref > 0.0 ? 1U : 0U);
                    add_unique_entry(&candidate.source_entries, ref_id);
                }
                append_candidate(out, candidate);
            }
        } else if (entry.key.data.exif_tag.tag == kGpsTimeStampTag
                   || entry.key.data.exif_tag.tag == kGpsDateStampTag) {
            std::string text;
            if (!value_to_text(store.arena(), entry.value, &text)) {
                return;
            }
            MetadataConceptCandidate candidate
                = make_entry_candidate(store, id, MetadataConceptKind::Gps,
                                       MetadataConceptRole::Timestamp,
                                       MetadataQuerySemanticKind::Unknown,
                                       MetadataQueryValueShape::Text, 80U);
            candidate.text = text;
            if (!fill_datetime_from_text(text, &candidate)) {
                normalize_text_key(text, &candidate.value_key);
            }
            append_candidate(out, candidate);
        }
    }

    static bool find_xmp_gps_altitude_ref(const MetaStore& store,
                                          std::string_view schema,
                                          std::string_view scope,
                                          EntryId* out_id,
                                          uint8_t* out_ref_code)
    {
        if (out_id) {
            *out_id = kInvalidEntryId;
        }
        if (out_ref_code) {
            *out_ref_code = 0U;
        }
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (!xmp_schema_matches(store, entry, schema)) {
                continue;
            }
            const std::string_view path
                = arena_string(store.arena(),
                               entry.key.data.xmp_property.property_path);
            if (xmp_property_scope(path) != scope
                || !xmp_leaf_matches(path, "GPSAltitudeRef")) {
                continue;
            }

            double numeric = 0.0;
            if (value_to_numeric_array(store.arena(), entry.value, &numeric, 1U)
                == 1U) {
                if (out_id) {
                    *out_id = id;
                }
                if (out_ref_code) {
                    *out_ref_code = numeric > 0.0 ? 1U : 0U;
                }
                return true;
            }

            std::string text;
            if (!value_to_text(store.arena(), entry.value, &text)) {
                continue;
            }
            std::string key;
            normalize_text_key(text, &key);
            uint8_t ref_code = 0U;
            if (!key.empty() && key[0] == '1') {
                ref_code = 1U;
            } else if (ascii_contains_ci(key, "below")
                       || ascii_contains_ci(key, "sealevelbelow")) {
                ref_code = 1U;
            }
            if (out_id) {
                *out_id = id;
            }
            if (out_ref_code) {
                *out_ref_code = ref_code;
            }
            return true;
        }
        return false;
    }

    static bool xmp_leaf_entry_exists(const MetaStore& store,
                                      std::string_view schema,
                                      std::string_view scope,
                                      std::string_view path_leaf,
                                      EntryId skip_id) noexcept
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            if (id == skip_id) {
                continue;
            }
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (!xmp_schema_matches(store, entry, schema)) {
                continue;
            }
            const std::string_view path
                = arena_string(store.arena(),
                               entry.key.data.xmp_property.property_path);
            if (xmp_property_scope(path) == scope
                && xmp_leaf_matches(path, path_leaf)) {
                return true;
            }
        }
        return false;
    }

    static void append_xmp_gps_candidate(const MetaStore& store, EntryId id,
                                         const Entry& entry,
                                         MetadataConceptResolution* out)
    {
        const std::string_view path
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.property_path);
        const std::string_view scope = xmp_property_scope(path);
        if (xmp_schema_matches(store, entry, kIptcExtXmpSchema)) {
            MetadataConceptRole role = MetadataConceptRole::Primary;
            if (xmp_location_shown_scope(scope)) {
                if (xmp_leaf_matches(path, "GPSLatitude")) {
                    role = MetadataConceptRole::LocationShownLatitude;
                } else if (xmp_leaf_matches(path, "GPSLongitude")) {
                    role = MetadataConceptRole::LocationShownLongitude;
                } else if (xmp_leaf_matches(path, "GPSAltitude")) {
                    role = MetadataConceptRole::LocationShownAltitude;
                }
            } else if (xmp_location_created_scope(scope)) {
                if (xmp_leaf_matches(path, "GPSLatitude")) {
                    role = MetadataConceptRole::LocationCreatedLatitude;
                } else if (xmp_leaf_matches(path, "GPSLongitude")) {
                    role = MetadataConceptRole::LocationCreatedLongitude;
                } else if (xmp_leaf_matches(path, "GPSAltitude")) {
                    role = MetadataConceptRole::LocationCreatedAltitude;
                }
            }
            if (role == MetadataConceptRole::Primary) {
                return;
            }

            std::string text;
            if (!value_to_text(store.arena(), entry.value, &text)) {
                return;
            }
            MetadataConceptCandidate candidate
                = make_entry_candidate(store, id, MetadataConceptKind::Gps,
                                       role, MetadataQuerySemanticKind::Unknown,
                                       MetadataQueryValueShape::Text, 78U);
            candidate.text.assign(text);
            candidate.location_scope.assign(xmp_scope_leaf(scope));
            double numeric = 0.0;
            if (parse_xmp_gps_coordinate(text, &numeric)) {
                if (role == MetadataConceptRole::LocationShownAltitude
                    || role == MetadataConceptRole::LocationCreatedAltitude) {
                    EntryId ref_id   = kInvalidEntryId;
                    uint8_t ref_code = 0U;
                    const bool has_ref
                        = find_xmp_gps_altitude_ref(store, kIptcExtXmpSchema,
                                                    scope, &ref_id, &ref_code);
                    if (has_ref && ref_code != 0U) {
                        numeric = -std::fabs(numeric);
                    }
                    if (has_ref) {
                        candidate.has_gps_altitude_reference   = true;
                        candidate.gps_altitude_below_sea_level = ref_code != 0U;
                        candidate.gps_altitude_reference_code  = ref_code;
                        add_unique_entry(&candidate.source_entries, ref_id);
                    }
                }
                fill_numeric_candidate(&candidate, &numeric, 1U);
                candidate.value_key = gps_numeric_key(numeric);
            }
            if (candidate.value_key.empty()) {
                normalize_text_key(text, &candidate.value_key);
            }
            append_candidate(out, candidate);
            return;
        }
        if (!xmp_schema_matches(store, entry, kExifXmpSchema)) {
            return;
        }
        const std::string_view schema = kExifXmpSchema;
        const bool split_date_stamp   = xmp_leaf_matches(path, "GPSDateStamp");
        const bool split_time_stamp   = xmp_leaf_matches(path, "GPSTimeStamp");
        if ((split_date_stamp
             && xmp_leaf_entry_exists(store, schema, scope, "GPSTimeStamp", id))
            || (split_time_stamp
                && xmp_leaf_entry_exists(store, schema, scope, "GPSDateStamp",
                                         id))) {
            return;
        }
        MetadataConceptRole role = MetadataConceptRole::Primary;
        uint8_t priority         = 0U;
        if (xmp_leaf_matches(path, "GPSDestLatitude")) {
            role     = MetadataConceptRole::DestinationLatitude;
            priority = 78U;
        } else if (xmp_leaf_matches(path, "GPSDestLongitude")) {
            role     = MetadataConceptRole::DestinationLongitude;
            priority = 78U;
        } else if (xmp_leaf_matches(path, "GPSDestLatitudeRef")
                   || xmp_leaf_matches(path, "GPSDestLongitudeRef")) {
            return;
        } else if (xmp_leaf_matches(path, "GPSLatitude")) {
            role     = MetadataConceptRole::Latitude;
            priority = 80U;
        } else if (xmp_leaf_matches(path, "GPSLongitude")) {
            role     = MetadataConceptRole::Longitude;
            priority = 80U;
        } else if (xmp_leaf_matches(path, "GPSAltitudeRef")) {
            return;
        } else if (xmp_leaf_matches(path, "GPSAltitude")) {
            role     = MetadataConceptRole::Altitude;
            priority = 75U;
        } else if (xmp_leaf_matches(path, "GPSDateTime")
                   || xmp_leaf_matches(path, "GPSDateTimeStamp")) {
            role     = MetadataConceptRole::Timestamp;
            priority = 84U;
        } else if (ascii_contains_ci(path, "GPSTime")
                   || ascii_contains_ci(path, "GPSDate")) {
            role     = MetadataConceptRole::Timestamp;
            priority = 70U;
        } else {
            return;
        }

        std::string text;
        if (!value_to_text(store.arena(), entry.value, &text)) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::Gps, role,
                                   MetadataQuerySemanticKind::Unknown,
                                   MetadataQueryValueShape::Text, priority);
        candidate.text = text;
        if (role == MetadataConceptRole::Latitude
            || role == MetadataConceptRole::Longitude
            || role == MetadataConceptRole::DestinationLatitude
            || role == MetadataConceptRole::DestinationLongitude) {
            double numeric = 0.0;
            if (parse_xmp_gps_coordinate(text, &numeric)) {
                fill_numeric_candidate(&candidate, &numeric, 1U);
                candidate.value_key = gps_numeric_key(numeric);
            }
        } else if (role == MetadataConceptRole::Altitude) {
            double numeric = 0.0;
            if (parse_xmp_gps_coordinate(text, &numeric)) {
                EntryId ref_id     = kInvalidEntryId;
                uint8_t ref_code   = 0U;
                const bool has_ref = find_xmp_gps_altitude_ref(store, schema,
                                                               scope, &ref_id,
                                                               &ref_code);
                if (has_ref && ref_code != 0U) {
                    numeric = -std::fabs(numeric);
                }
                fill_numeric_candidate(&candidate, &numeric, 1U);
                candidate.value_key = gps_numeric_key(numeric);
                if (has_ref) {
                    candidate.has_gps_altitude_reference   = true;
                    candidate.gps_altitude_below_sea_level = ref_code != 0U;
                    candidate.gps_altitude_reference_code  = ref_code;
                    add_unique_entry(&candidate.source_entries, ref_id);
                }
            }
        }
        if (candidate.value_key.empty()) {
            if (role == MetadataConceptRole::Timestamp) {
                (void)fill_datetime_from_text(text, &candidate);
            }
            if (candidate.value_key.empty()) {
                normalize_text_key(text, &candidate.value_key);
            }
        }
        append_candidate(out, candidate);
    }

    static void
    append_exif_gps_timestamp_composite(const MetaStore& store,
                                        MetadataConceptResolution* out)
    {
        EntryId date_id = kInvalidEntryId;
        EntryId time_id = kInvalidEntryId;
        std::string date_text;
        if (!find_exif_text_entry(store, "gpsifd", kGpsDateStampTag, &date_id,
                                  &date_text)) {
            return;
        }
        uint8_t hour   = 0U;
        uint8_t minute = 0U;
        uint8_t second = 0U;
        if (!find_exif_time_entry(store, "gpsifd", kGpsTimeStampTag, &time_id,
                                  &hour, &minute, &second)) {
            return;
        }

        MetadataConceptCandidate candidate
            = make_entry_candidate(store, date_id, MetadataConceptKind::Gps,
                                   MetadataConceptRole::Timestamp,
                                   MetadataQuerySemanticKind::Unknown,
                                   MetadataQueryValueShape::Text, 90U);
        candidate.text = date_text;
        char time_buf[16];
        std::snprintf(time_buf, sizeof(time_buf), " %02u:%02u:%02uZ",
                      static_cast<unsigned>(hour),
                      static_cast<unsigned>(minute),
                      static_cast<unsigned>(second));
        candidate.text.append(time_buf);
        if (!fill_datetime_from_text(date_text, &candidate)) {
            return;
        }
        (void)attach_time_to_candidate(&candidate, hour, minute, second, true,
                                       0);
        add_unique_entry(&candidate.source_entries, time_id);
        append_candidate(out, candidate);
    }

    static bool find_xmp_text_entry_leaf(const MetaStore& store,
                                         std::string_view schema,
                                         std::string_view scope,
                                         std::string_view path_leaf,
                                         EntryId* out_id, std::string* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (!xmp_schema_matches(store, entry, schema)) {
                continue;
            }
            const std::string_view path
                = arena_string(store.arena(),
                               entry.key.data.xmp_property.property_path);
            if (xmp_property_scope(path) != scope
                || !xmp_leaf_matches(path, path_leaf)) {
                continue;
            }
            if (!value_to_text(store.arena(), entry.value, out)) {
                continue;
            }
            if (out_id) {
                *out_id = id;
            }
            return true;
        }
        if (out_id) {
            *out_id = kInvalidEntryId;
        }
        if (out) {
            out->clear();
        }
        return false;
    }

    static void
    append_xmp_gps_timestamp_composite(const MetaStore& store,
                                       MetadataConceptResolution* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId date_id = 0U; date_id < entries.size(); ++date_id) {
            const Entry& date_entry = entries[date_id];
            if (any(date_entry.flags, EntryFlags::Deleted)
                || !xmp_schema_matches(store, date_entry, kExifXmpSchema)) {
                continue;
            }
            const std::string_view date_path
                = arena_string(store.arena(),
                               date_entry.key.data.xmp_property.property_path);
            if (!xmp_leaf_matches(date_path, "GPSDateStamp")) {
                continue;
            }

            std::string date_text;
            if (!value_to_text(store.arena(), date_entry.value, &date_text)) {
                continue;
            }
            const std::string_view scope = xmp_property_scope(date_path);
            EntryId time_id              = kInvalidEntryId;
            std::string time_text;
            if (!find_xmp_text_entry_leaf(store, kExifXmpSchema, scope,
                                          "GPSTimeStamp", &time_id,
                                          &time_text)) {
                continue;
            }

            std::string combined_text = date_text;
            combined_text.push_back(' ');
            combined_text.append(time_text);

            MetadataConceptCandidate candidate
                = make_entry_candidate(store, date_id, MetadataConceptKind::Gps,
                                       MetadataConceptRole::Timestamp,
                                       MetadataQuerySemanticKind::Unknown,
                                       MetadataQueryValueShape::Text, 82U);
            candidate.text = combined_text;
            if (!fill_datetime_from_text(date_text, &candidate)) {
                continue;
            }

            const Entry& time_entry = store.entry(time_id);
            uint8_t hour            = 0U;
            uint8_t minute          = 0U;
            uint8_t second          = 0U;
            bool has_offset         = false;
            int16_t offset          = 0;
            if (!fill_time_from_value(store.arena(), time_entry.value, &hour,
                                      &minute, &second, &has_offset, &offset)) {
                continue;
            }
            (void)attach_time_to_candidate(&candidate, hour, minute, second,
                                           has_offset, offset);
            candidate.text = combined_text;
            add_unique_entry(&candidate.source_entries, time_id);
            append_candidate(out, candidate);
        }
    }

    static void append_gps_candidates(const MetaStore& store,
                                      MetadataConceptResolution* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (entry.key.kind == MetaKeyKind::ExifTag) {
                append_exif_gps_candidate(store, id, entry, out);
            } else if (entry.key.kind == MetaKeyKind::XmpProperty) {
                append_xmp_gps_candidate(store, id, entry, out);
            }
        }
        append_exif_gps_timestamp_composite(store, out);
        append_xmp_gps_timestamp_composite(store, out);
    }

    static bool descriptive_role_is_collection(MetadataConceptRole role) noexcept
    {
        return role == MetadataConceptRole::Creator
               || role == MetadataConceptRole::Keywords
               || role == MetadataConceptRole::LocationIdentifier
               || role == MetadataConceptRole::RightsHolderName
               || role == MetadataConceptRole::RightsHolderIdentifier
               || role == MetadataConceptRole::LicensorName
               || role == MetadataConceptRole::LicensorIdentifier
               || role == MetadataConceptRole::Name
               || role == MetadataConceptRole::Identifier
               || role == MetadataConceptRole::Address
               || role == MetadataConceptRole::Email
               || role == MetadataConceptRole::Telephone
               || role == MetadataConceptRole::Url
               || role == MetadataConceptRole::Characteristic
               || role == MetadataConceptRole::Gtin
               || role == MetadataConceptRole::InventoryNumber
               || role == MetadataConceptRole::StylePeriod
               || role == MetadataConceptRole::CreatorIdentifier
               || role == MetadataConceptRole::RightsExpression
               || role == MetadataConceptRole::MediaConstraint
               || role == MetadataConceptRole::RegionConstraint
               || role == MetadataConceptRole::ProductOrServiceConstraint
               || role == MetadataConceptRole::ImageFileConstraint
               || role == MetadataConceptRole::ImageAlterationConstraint
               || role == MetadataConceptRole::OtherLicenseRequirement
               || role == MetadataConceptRole::OtherCondition
               || role == MetadataConceptRole::LicenseeTransactionIdentifier
               || role == MetadataConceptRole::LicensorTransactionIdentifier
               || role == MetadataConceptRole::LicenseeProjectReference
               || role == MetadataConceptRole::ReleaseIdentifier
               || role == MetadataConceptRole::SupplementalCategory
               || role == MetadataConceptRole::SceneCode
               || role == MetadataConceptRole::SubjectCode
               || role == MetadataConceptRole::ResourceIdentifier
               || role == MetadataConceptRole::ImageIdentifier
               || role == MetadataConceptRole::OtherLicenseDocument
               || role == MetadataConceptRole::CreatorTitle
               || role == MetadataConceptRole::CaptionWriter
               || role == MetadataConceptRole::AlternatePath
               || role == MetadataConceptRole::ChangedParts;
    }

    static bool descriptive_role_is_localized(MetadataConceptRole role) noexcept
    {
        switch (role) {
        case MetadataConceptRole::Title:
        case MetadataConceptRole::Headline:
        case MetadataConceptRole::Description:
        case MetadataConceptRole::CopyrightNotice:
        case MetadataConceptRole::RightsUsageTerms:
        case MetadataConceptRole::LocationName:
        case MetadataConceptRole::Sublocation:
        case MetadataConceptRole::City:
        case MetadataConceptRole::ProvinceState:
        case MetadataConceptRole::CountryName:
        case MetadataConceptRole::CountryCode:
        case MetadataConceptRole::WorldRegion:
        case MetadataConceptRole::Name:
        case MetadataConceptRole::ContentDescription:
        case MetadataConceptRole::ContributionDescription:
        case MetadataConceptRole::PhysicalDescription:
        case MetadataConceptRole::MediaConstraint:
        case MetadataConceptRole::RegionConstraint:
        case MetadataConceptRole::ProductOrServiceConstraint:
        case MetadataConceptRole::OtherLicenseRequirement:
        case MetadataConceptRole::OtherCondition:
        case MetadataConceptRole::AccessibilityAltText:
        case MetadataConceptRole::AccessibilityExtendedDescription:
        case MetadataConceptRole::Notes:
        case MetadataConceptRole::OtherImageInformation:
        case MetadataConceptRole::OtherLicenseInformation:
        case MetadataConceptRole::TermName:
        case MetadataConceptRole::RegionName:
        case MetadataConceptRole::RegionContentTypeName:
        case MetadataConceptRole::RegionRoleName: return true;
        default: break;
        }
        return false;
    }

    static bool descriptive_role_is_location(MetadataConceptRole role) noexcept
    {
        switch (role) {
        case MetadataConceptRole::LocationName:
        case MetadataConceptRole::Sublocation:
        case MetadataConceptRole::City:
        case MetadataConceptRole::ProvinceState:
        case MetadataConceptRole::CountryName:
        case MetadataConceptRole::CountryCode:
        case MetadataConceptRole::WorldRegion:
        case MetadataConceptRole::LocationIdentifier: return true;
        default: break;
        }
        return false;
    }

    static MetadataQuerySemanticKind descriptive_semantic_for_role(
        MetadataConceptRole role,
        MetadataConceptRecordKind record_kind) noexcept
    {
        if (role == MetadataConceptRole::CreditLine
            || role == MetadataConceptRole::CreditLineRequired) {
            return MetadataQuerySemanticKind::Credit;
        }
        switch (record_kind) {
        case MetadataConceptRecordKind::CreatorContact:
            return MetadataQuerySemanticKind::Contact;
        case MetadataConceptRecordKind::Event:
            return MetadataQuerySemanticKind::Event;
        case MetadataConceptRecordKind::Person:
            return MetadataQuerySemanticKind::Person;
        case MetadataConceptRecordKind::Organization:
            return MetadataQuerySemanticKind::Organization;
        case MetadataConceptRecordKind::Product:
            return MetadataQuerySemanticKind::Product;
        case MetadataConceptRecordKind::ArtworkOrObject:
            return MetadataQuerySemanticKind::Artwork;
        case MetadataConceptRecordKind::RightsExpression:
            return MetadataQuerySemanticKind::RightsExpression;
        case MetadataConceptRecordKind::RightsHolder:
            return MetadataQuerySemanticKind::Rights;
        case MetadataConceptRecordKind::Licensor:
        case MetadataConceptRecordKind::Licensee:
        case MetadataConceptRecordKind::License:
            return MetadataQuerySemanticKind::License;
        case MetadataConceptRecordKind::Release:
            return MetadataQuerySemanticKind::Release;
        case MetadataConceptRecordKind::EndUser:
            return MetadataQuerySemanticKind::License;
        case MetadataConceptRecordKind::ImageCreator:
            return MetadataQuerySemanticKind::Creator;
        case MetadataConceptRecordKind::ImageSupplier:
            return MetadataQuerySemanticKind::Source;
        case MetadataConceptRecordKind::ImageAsset:
            return MetadataQuerySemanticKind::DocumentIdentity;
        case MetadataConceptRecordKind::ControlledVocabularyTerm:
            return MetadataQuerySemanticKind::Taxonomy;
        case MetadataConceptRecordKind::RegistryEntry:
            return MetadataQuerySemanticKind::Registry;
        case MetadataConceptRecordKind::ImageRegion:
            return MetadataQuerySemanticKind::ImageRegion;
        case MetadataConceptRecordKind::ResourceReference:
        case MetadataConceptRecordKind::PantryItem:
            return MetadataQuerySemanticKind::DocumentLineage;
        case MetadataConceptRecordKind::ResourceEvent:
            return MetadataQuerySemanticKind::DocumentHistory;
        case MetadataConceptRecordKind::None: break;
        }
        switch (role) {
        case MetadataConceptRole::Title:
        case MetadataConceptRole::Headline:
            return MetadataQuerySemanticKind::Title;
        case MetadataConceptRole::Description:
            return MetadataQuerySemanticKind::Description;
        case MetadataConceptRole::Creator:
        case MetadataConceptRole::CreatorTitle:
        case MetadataConceptRole::CaptionWriter:
            return MetadataQuerySemanticKind::Creator;
        case MetadataConceptRole::Keywords:
            return MetadataQuerySemanticKind::Keywords;
        case MetadataConceptRole::CopyrightNotice:
        case MetadataConceptRole::CopyrightStatus:
        case MetadataConceptRole::RightsWebStatement:
        case MetadataConceptRole::RightsCertificate:
        case MetadataConceptRole::RightsMarked:
        case MetadataConceptRole::RightsHolderName:
        case MetadataConceptRole::RightsHolderIdentifier:
            return MetadataQuerySemanticKind::Rights;
        case MetadataConceptRole::RightsUsageTerms:
        case MetadataConceptRole::LicenseIdentifier:
        case MetadataConceptRole::LicenseTermsUrl:
        case MetadataConceptRole::LicensorName:
        case MetadataConceptRole::LicensorIdentifier:
            return MetadataQuerySemanticKind::License;
        case MetadataConceptRole::CreditLine:
        case MetadataConceptRole::CreditLineRequired:
            return MetadataQuerySemanticKind::Credit;
        case MetadataConceptRole::Source:
        case MetadataConceptRole::DigitalSourceType:
            return MetadataQuerySemanticKind::Source;
        case MetadataConceptRole::Urgency:
        case MetadataConceptRole::Category:
        case MetadataConceptRole::SupplementalCategory:
        case MetadataConceptRole::Instructions:
        case MetadataConceptRole::TransmissionReference:
            return MetadataQuerySemanticKind::Editorial;
        case MetadataConceptRole::AccessibilityAltText:
        case MetadataConceptRole::AccessibilityExtendedDescription:
            return MetadataQuerySemanticKind::Accessibility;
        case MetadataConceptRole::IntellectualGenre:
        case MetadataConceptRole::SceneCode:
        case MetadataConceptRole::SubjectCode:
            return MetadataQuerySemanticKind::Taxonomy;
        case MetadataConceptRole::ResourceIdentifier:
        case MetadataConceptRole::DerivedFromIdentifier:
        case MetadataConceptRole::DocumentIdentifier:
        case MetadataConceptRole::InstanceIdentifier:
        case MetadataConceptRole::OriginalDocumentIdentifier:
        case MetadataConceptRole::RenditionClass:
            return MetadataQuerySemanticKind::DocumentIdentity;
        default: break;
        }
        return MetadataQuerySemanticKind::Unknown;
    }

    static bool descriptive_xmp_location_scope(std::string_view path,
                                               std::string* out)
    {
        if (out) {
            out->clear();
        }
        const size_t separator = path.find('/');
        if (separator == std::string_view::npos) {
            return false;
        }
        const std::string_view root = xmp_scope_leaf(
            path.substr(0U, separator));
        if (xmp_location_created_scope(root)) {
            if (out) {
                out->assign("LocationCreated");
            }
            return true;
        }
        if (xmp_location_shown_scope(root)) {
            if (out) {
                out->assign(root);
            }
            return true;
        }
        return false;
    }

    static std::string_view
    descriptive_xmp_record_scope(std::string_view path,
                                 std::string_view root_name) noexcept
    {
        const size_t separator      = path.find('/');
        const std::string_view root = xmp_scope_leaf(
            separator == std::string_view::npos ? path
                                                : path.substr(0U, separator));
        if (ascii_equal_ci(root, root_name)) {
            return root;
        }
        if (root.size() <= root_name.size() + 2U
            || !ascii_starts_with_ci(root, root_name)
            || root[root_name.size()] != '[' || root.back() != ']') {
            return {};
        }
        for (size_t i = root_name.size() + 1U; i + 1U < root.size(); ++i) {
            if (!ascii_is_digit(root[i])) {
                return {};
            }
        }
        return root;
    }

    static bool descriptive_xmp_normalized_record_scope(
        std::string_view path, std::string_view root_name,
        std::string_view normalized_name, std::string* out)
    {
        if (out) {
            out->clear();
        }
        const std::string_view scope = descriptive_xmp_record_scope(path,
                                                                    root_name);
        if (scope.empty()) {
            return false;
        }
        if (out) {
            out->assign(normalized_name);
            if (scope.size() > root_name.size()) {
                out->append(scope.substr(root_name.size()));
            }
        }
        return true;
    }

    static MetadataConceptRole
    descriptive_location_role_for_leaf(std::string_view leaf) noexcept
    {
        if (ascii_equal_ci(leaf, "LocationName")) {
            return MetadataConceptRole::LocationName;
        }
        if (ascii_equal_ci(leaf, "Location")
            || ascii_equal_ci(leaf, "Sublocation")) {
            return MetadataConceptRole::Sublocation;
        }
        if (ascii_equal_ci(leaf, "City")) {
            return MetadataConceptRole::City;
        }
        if (ascii_equal_ci(leaf, "State")
            || ascii_equal_ci(leaf, "ProvinceState")) {
            return MetadataConceptRole::ProvinceState;
        }
        if (ascii_equal_ci(leaf, "Country")
            || ascii_equal_ci(leaf, "CountryName")) {
            return MetadataConceptRole::CountryName;
        }
        if (ascii_equal_ci(leaf, "CountryCode")) {
            return MetadataConceptRole::CountryCode;
        }
        if (ascii_equal_ci(leaf, "WorldRegion")) {
            return MetadataConceptRole::WorldRegion;
        }
        if (ascii_equal_ci(leaf, "LocationId")) {
            return MetadataConceptRole::LocationIdentifier;
        }
        return MetadataConceptRole::Primary;
    }

    static void append_descriptive_text_candidate(
        const MetaStore& store, EntryId id, MetadataConceptRole role,
        MetadataConceptRecordKind record_kind, uint8_t priority,
        std::string_view location_scope, std::string_view record_scope,
        std::string_view xmp_path, MetadataConceptResolution* out)
    {
        std::string text;
        if (!value_to_text(store.arena(), store.entry(id).value, &text)) {
            return;
        }
        MetadataConceptCandidate candidate = make_entry_candidate(
            store, id, MetadataConceptKind::Descriptive, role,
            descriptive_semantic_for_role(role, record_kind),
            MetadataQueryValueShape::Text, priority);
        candidate.record_kind = record_kind;
        candidate.text.assign(text);
        normalize_text_key(text, &candidate.value_key);
        if (candidate.value_key.empty()) {
            return;
        }
        if (!location_scope.empty()) {
            candidate.location_scope.assign(location_scope);
        }
        if (!record_scope.empty()) {
            candidate.record_scope.assign(record_scope);
        }
        if (descriptive_role_is_localized(role)) {
            assign_candidate_language(xmp_path, &candidate);
        }
        append_candidate(out, candidate);
    }

    static void
    append_exif_descriptive_candidate(const MetaStore& store, EntryId id,
                                      const Entry& entry,
                                      MetadataConceptResolution* out)
    {
        const uint16_t tag = entry.key.data.exif_tag.tag;
        if (!exif_entry_ifd_and_tag(store, entry, "ifd0", tag)) {
            return;
        }
        MetadataConceptRole role = MetadataConceptRole::Primary;
        uint8_t priority         = 0U;
        switch (tag) {
        case kExifDocumentNameTag:
            role     = MetadataConceptRole::Title;
            priority = 76U;
            break;
        case kExifXpTitleTag:
            role     = MetadataConceptRole::Title;
            priority = 80U;
            break;
        case kExifImageDescriptionTag:
            role     = MetadataConceptRole::Description;
            priority = 76U;
            break;
        case kExifXpCommentTag:
            role     = MetadataConceptRole::Description;
            priority = 80U;
            break;
        case kExifArtistTag:
            role     = MetadataConceptRole::Creator;
            priority = 76U;
            break;
        case kExifCopyrightTag:
            role     = MetadataConceptRole::CopyrightNotice;
            priority = 76U;
            break;
        case kExifXpAuthorTag:
            role     = MetadataConceptRole::Creator;
            priority = 80U;
            break;
        case kExifXpKeywordsTag:
            role     = MetadataConceptRole::Keywords;
            priority = 80U;
            break;
        default: return;
        }
        append_descriptive_text_candidate(store, id, role,
                                          MetadataConceptRecordKind::None,
                                          priority, {}, {}, {}, out);
    }

    static void
    append_iptc_descriptive_candidate(const MetaStore& store, EntryId id,
                                      const Entry& entry,
                                      MetadataConceptResolution* out)
    {
        if (entry.key.data.iptc_dataset.record != 2U) {
            return;
        }
        MetadataConceptRole role = MetadataConceptRole::Primary;
        const uint16_t dataset   = entry.key.data.iptc_dataset.dataset;
        switch (dataset) {
        case kIptcObjectNameDataset: role = MetadataConceptRole::Title; break;
        case kIptcUrgencyDataset: role = MetadataConceptRole::Urgency; break;
        case kIptcCategoryDataset: role = MetadataConceptRole::Category; break;
        case kIptcSupplementalCategoryDataset:
            role = MetadataConceptRole::SupplementalCategory;
            break;
        case kIptcKeywordsDataset: role = MetadataConceptRole::Keywords; break;
        case kIptcInstructionsDataset:
            role = MetadataConceptRole::Instructions;
            break;
        case kIptcBylineDataset: role = MetadataConceptRole::Creator; break;
        case kIptcBylineTitleDataset:
            role = MetadataConceptRole::CreatorTitle;
            break;
        case kIptcHeadlineDataset: role = MetadataConceptRole::Headline; break;
        case kIptcCreditDataset: role = MetadataConceptRole::CreditLine; break;
        case kIptcSourceDataset: role = MetadataConceptRole::Source; break;
        case kIptcCopyrightNoticeDataset:
            role = MetadataConceptRole::CopyrightNotice;
            break;
        case kIptcCaptionDataset:
            role = MetadataConceptRole::Description;
            break;
        case kIptcCityDataset: role = MetadataConceptRole::City; break;
        case kIptcSublocationDataset:
            role = MetadataConceptRole::Sublocation;
            break;
        case kIptcProvinceStateDataset:
            role = MetadataConceptRole::ProvinceState;
            break;
        case kIptcCountryCodeDataset:
            role = MetadataConceptRole::CountryCode;
            break;
        case kIptcCountryNameDataset:
            role = MetadataConceptRole::CountryName;
            break;
        case kIptcTransmissionReferenceDataset:
            role = MetadataConceptRole::TransmissionReference;
            break;
        case kIptcCaptionWriterDataset:
            role = MetadataConceptRole::CaptionWriter;
            break;
        default: return;
        }
        const std::string_view scope = descriptive_role_is_location(role)
                                           ? std::string_view("LocationCreated")
                                           : std::string_view {};
        append_descriptive_text_candidate(store, id, role,
                                          MetadataConceptRecordKind::None, 86U,
                                          scope, {}, {}, out);
    }

    static bool map_creator_contact_descriptive(std::string_view leaf,
                                                MetadataConceptRole* role)
    {
        if (ascii_equal_ci(leaf, "CiAdrCity")) {
            *role = MetadataConceptRole::City;
        } else if (ascii_equal_ci(leaf, "CiAdrCtry")) {
            *role = MetadataConceptRole::CountryName;
        } else if (ascii_equal_ci(leaf, "CiAdrExtadr")) {
            *role = MetadataConceptRole::Address;
        } else if (ascii_equal_ci(leaf, "CiAdrPcode")) {
            *role = MetadataConceptRole::PostalCode;
        } else if (ascii_equal_ci(leaf, "CiAdrRegion")
                   || ascii_equal_ci(leaf, "ProvinceName")) {
            *role = MetadataConceptRole::ProvinceState;
        } else if (ascii_equal_ci(leaf, "ProvinceCode")) {
            *role = MetadataConceptRole::Identifier;
        } else if (ascii_equal_ci(leaf, "CiEmailWork")) {
            *role = MetadataConceptRole::Email;
        } else if (ascii_equal_ci(leaf, "CiTelWork")) {
            *role = MetadataConceptRole::Telephone;
        } else if (ascii_equal_ci(leaf, "CiUrlWork")) {
            *role = MetadataConceptRole::Url;
        }
        return *role != MetadataConceptRole::Primary;
    }

    static bool map_iptc_core_structured_descriptive(
        std::string_view path, std::string_view leaf, MetadataConceptRole* role,
        MetadataConceptRecordKind* record_kind, std::string* record_scope)
    {
        if (!descriptive_xmp_normalized_record_scope(path, "CreatorContactInfo",
                                                     "CreatorContact",
                                                     record_scope)) {
            return false;
        }
        *record_kind = MetadataConceptRecordKind::CreatorContact;
        return map_creator_contact_descriptive(leaf, role);
    }

    static bool map_iptc_core_flat_descriptive(
        std::string_view leaf, MetadataConceptRole* role,
        MetadataConceptRecordKind* record_kind, std::string* record_scope)
    {
        if (ascii_starts_with_ci(leaf, "Ci")
            && map_creator_contact_descriptive(leaf, role)) {
            *record_kind = MetadataConceptRecordKind::CreatorContact;
            record_scope->assign("CreatorContact");
            return true;
        }
        if (ascii_equal_ci(leaf, "AltTextAccessibility")) {
            *role = MetadataConceptRole::AccessibilityAltText;
        } else if (ascii_equal_ci(leaf, "ExtDescrAccessibility")) {
            *role = MetadataConceptRole::AccessibilityExtendedDescription;
        } else if (ascii_equal_ci(leaf, "IntellectualGenre")) {
            *role = MetadataConceptRole::IntellectualGenre;
        } else if (ascii_equal_ci(leaf, "Scene")) {
            *role = MetadataConceptRole::SceneCode;
        } else if (ascii_equal_ci(leaf, "SubjectCode")) {
            *role = MetadataConceptRole::SubjectCode;
        }
        return *role != MetadataConceptRole::Primary;
    }

    static bool map_artwork_descriptive(std::string_view leaf,
                                        MetadataConceptRole* role)
    {
        if (ascii_equal_ci(leaf, "AOCopyrightNotice")) {
            *role = MetadataConceptRole::CopyrightNotice;
        } else if (ascii_equal_ci(leaf, "AOCreator")) {
            *role = MetadataConceptRole::Creator;
        } else if (ascii_equal_ci(leaf, "AOCreatorId")) {
            *role = MetadataConceptRole::CreatorIdentifier;
        } else if (ascii_equal_ci(leaf, "AODateCreated")
                   || ascii_equal_ci(leaf, "AOCircaDateCreated")) {
            *role = MetadataConceptRole::DateCreated;
        } else if (ascii_equal_ci(leaf, "AOSource")) {
            *role = MetadataConceptRole::Source;
        } else if (ascii_equal_ci(leaf, "AOSourceInvNo")) {
            *role = MetadataConceptRole::InventoryNumber;
        } else if (ascii_equal_ci(leaf, "AOTitle")) {
            *role = MetadataConceptRole::Title;
        } else if (ascii_equal_ci(leaf, "AOCurrentCopyrightOwnerName")) {
            *role = MetadataConceptRole::RightsHolderName;
        } else if (ascii_equal_ci(leaf, "AOCurrentCopyrightOwnerId")) {
            *role = MetadataConceptRole::RightsHolderIdentifier;
        } else if (ascii_equal_ci(leaf, "AOCurrentLicensorName")) {
            *role = MetadataConceptRole::LicensorName;
        } else if (ascii_equal_ci(leaf, "AOCurrentLicensorId")) {
            *role = MetadataConceptRole::LicensorIdentifier;
        } else if (ascii_equal_ci(leaf, "AOStylePeriod")) {
            *role = MetadataConceptRole::StylePeriod;
        } else if (ascii_equal_ci(leaf, "AOSourceInvURL")) {
            *role = MetadataConceptRole::Url;
        } else if (ascii_equal_ci(leaf, "AOContentDescription")) {
            *role = MetadataConceptRole::ContentDescription;
        } else if (ascii_equal_ci(leaf, "AOContributionDescription")) {
            *role = MetadataConceptRole::ContributionDescription;
        } else if (ascii_equal_ci(leaf, "AOPhysicalDescription")) {
            *role = MetadataConceptRole::PhysicalDescription;
        }
        return *role != MetadataConceptRole::Primary;
    }

    static bool map_controlled_vocabulary_term_descriptive(
        std::string_view path, std::string_view leaf, MetadataConceptRole* role,
        MetadataConceptRecordKind* record_kind, std::string* record_scope)
    {
        bool matched = descriptive_xmp_normalized_record_scope(path,
                                                               "AboutCvTerm",
                                                               "AboutCvTerm",
                                                               record_scope);
        if (!matched) {
            matched = descriptive_xmp_normalized_record_scope(path, "Genre",
                                                              "Genre",
                                                              record_scope);
        }
        if (!matched
            && descriptive_xmp_normalized_record_scope(
                path, "CVterm", "ControlledVocabularyTerm", record_scope)) {
            *record_kind = MetadataConceptRecordKind::ControlledVocabularyTerm;
            *role        = MetadataConceptRole::TermIdentifier;
            return true;
        }
        if (!matched) {
            return false;
        }
        *record_kind = MetadataConceptRecordKind::ControlledVocabularyTerm;
        if (ascii_equal_ci(leaf, "CvId")) {
            *role = MetadataConceptRole::VocabularyIdentifier;
        } else if (ascii_equal_ci(leaf, "CvTermId")) {
            *role = MetadataConceptRole::TermIdentifier;
        } else if (ascii_equal_ci(leaf, "CvTermName")) {
            *role = MetadataConceptRole::TermName;
        } else if (ascii_equal_ci(leaf, "CvTermRefinedAbout")) {
            *role = MetadataConceptRole::RefinedAbout;
        }
        return *role != MetadataConceptRole::Primary;
    }

    static bool map_registry_entry_descriptive(
        std::string_view path, std::string_view leaf, MetadataConceptRole* role,
        MetadataConceptRecordKind* record_kind, std::string* record_scope)
    {
        if (!descriptive_xmp_normalized_record_scope(path, "RegistryId",
                                                     "RegistryEntry",
                                                     record_scope)) {
            if (path.find('/') != std::string_view::npos
                || (!ascii_equal_ci(leaf, "RegItemId")
                    && !ascii_equal_ci(leaf, "RegOrgId"))) {
                return false;
            }
            record_scope->assign("RegistryEntry[legacy]");
        }
        *record_kind = MetadataConceptRecordKind::RegistryEntry;
        if (ascii_equal_ci(leaf, "RegItemId")) {
            *role = MetadataConceptRole::RegistryItemIdentifier;
        } else if (ascii_equal_ci(leaf, "RegOrgId")) {
            *role = MetadataConceptRole::RegistryOrganizationIdentifier;
        } else if (ascii_equal_ci(leaf, "RegEntryRole")) {
            *role = MetadataConceptRole::RegistryEntryRole;
        }
        return *role != MetadataConceptRole::Primary;
    }

    static void append_nested_record_scope(std::string_view path,
                                           std::string_view segment,
                                           std::string_view normalized,
                                           std::string* record_scope)
    {
        if (!record_scope) {
            return;
        }
        const size_t begin = ascii_find_ci(path, segment);
        if (begin == std::string_view::npos) {
            return;
        }
        size_t end = path.find('/', begin);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        record_scope->append("/");
        record_scope->append(normalized);
        const size_t suffix = begin + segment.size();
        if (suffix < end && path[suffix] == '[') {
            record_scope->append(path.substr(suffix, end - suffix));
        }
    }

    static bool map_image_region_descriptive(
        std::string_view path, std::string_view leaf, MetadataConceptRole* role,
        MetadataConceptRecordKind* record_kind, std::string* record_scope)
    {
        if (!descriptive_xmp_normalized_record_scope(path, "ImageRegion",
                                                     "ImageRegion",
                                                     record_scope)) {
            return false;
        }
        *record_kind = MetadataConceptRecordKind::ImageRegion;
        if (ascii_contains_ci(path, "rCtype")) {
            append_nested_record_scope(path, "rCtype", "ContentType",
                                       record_scope);
            if (ascii_equal_ci(leaf, "Identifier")) {
                *role = MetadataConceptRole::RegionContentTypeIdentifier;
            } else if (ascii_equal_ci(leaf, "Name")) {
                *role = MetadataConceptRole::RegionContentTypeName;
            }
        } else if (ascii_contains_ci(path, "rRole")) {
            append_nested_record_scope(path, "rRole", "Role", record_scope);
            if (ascii_equal_ci(leaf, "Identifier")) {
                *role = MetadataConceptRole::RegionRoleIdentifier;
            } else if (ascii_equal_ci(leaf, "Name")) {
                *role = MetadataConceptRole::RegionRoleName;
            }
        } else if (ascii_equal_ci(leaf, "rId")) {
            *role = MetadataConceptRole::RegionIdentifier;
        } else if (ascii_equal_ci(leaf, "Name")) {
            *role = MetadataConceptRole::RegionName;
        }
        return *role != MetadataConceptRole::Primary;
    }

    static bool map_iptc_ext_structured_descriptive(
        std::string_view path, std::string_view leaf, MetadataConceptRole* role,
        MetadataConceptRecordKind* record_kind, std::string* record_scope)
    {
        if (map_controlled_vocabulary_term_descriptive(path, leaf, role,
                                                       record_kind,
                                                       record_scope)
            || map_registry_entry_descriptive(path, leaf, role, record_kind,
                                              record_scope)
            || map_image_region_descriptive(path, leaf, role, record_kind,
                                            record_scope)) {
            return true;
        }
        if (descriptive_xmp_normalized_record_scope(path, "ArtworkOrObject",
                                                    "ArtworkOrObject",
                                                    record_scope)) {
            *record_kind = MetadataConceptRecordKind::ArtworkOrObject;
            return map_artwork_descriptive(leaf, role);
        }
        if (descriptive_xmp_normalized_record_scope(path,
                                                    "PersonInImageWDetails",
                                                    "Person", record_scope)) {
            *record_kind = MetadataConceptRecordKind::Person;
            if (ascii_equal_ci(leaf, "PersonName")) {
                *role = MetadataConceptRole::Name;
            } else if (ascii_equal_ci(leaf, "PersonId")) {
                *role = MetadataConceptRole::Identifier;
            } else if (ascii_equal_ci(leaf, "PersonDescription")) {
                *role = MetadataConceptRole::Description;
            } else if (ascii_equal_ci(leaf, "PersonCharacteristic")
                       || ascii_equal_ci(leaf, "CvId")
                       || ascii_equal_ci(leaf, "CvTermId")
                       || ascii_equal_ci(leaf, "CvTermName")
                       || ascii_equal_ci(leaf, "CvTermRefinedAbout")) {
                *role = MetadataConceptRole::Characteristic;
            }
            return *role != MetadataConceptRole::Primary;
        }
        if (descriptive_xmp_normalized_record_scope(path, "ProductInImage",
                                                    "Product", record_scope)) {
            *record_kind = MetadataConceptRecordKind::Product;
            if (ascii_equal_ci(leaf, "ProductName")) {
                *role = MetadataConceptRole::Name;
            } else if (ascii_equal_ci(leaf, "ProductId")) {
                *role = MetadataConceptRole::Identifier;
            } else if (ascii_equal_ci(leaf, "ProductGTIN")) {
                *role = MetadataConceptRole::Gtin;
            } else if (ascii_equal_ci(leaf, "ProductDescription")) {
                *role = MetadataConceptRole::Description;
            }
            return *role != MetadataConceptRole::Primary;
        }
        if (descriptive_xmp_normalized_record_scope(path, "EmbdEncRightsExpr",
                                                    "EmbeddedRightsExpression",
                                                    record_scope)
            || descriptive_xmp_normalized_record_scope(path,
                                                       "LinkedEncRightsExpr",
                                                       "LinkedRightsExpression",
                                                       record_scope)) {
            *record_kind = MetadataConceptRecordKind::RightsExpression;
            if (ascii_equal_ci(leaf, "EncRightsExpr")
                || ascii_equal_ci(leaf, "LinkedRightsExpr")) {
                *role = MetadataConceptRole::RightsExpression;
            } else if (ascii_equal_ci(leaf, "RightsExprEncType")) {
                *role = MetadataConceptRole::RightsExpressionEncoding;
            } else if (ascii_equal_ci(leaf, "RightsExprLangId")) {
                *role = MetadataConceptRole::RightsExpressionLanguage;
            }
            return *role != MetadataConceptRole::Primary;
        }
        if (descriptive_xmp_normalized_record_scope(path, "PersonInImage",
                                                    "Person", record_scope)) {
            *record_kind = MetadataConceptRecordKind::Person;
            *role        = MetadataConceptRole::Name;
            return true;
        }
        if (descriptive_xmp_normalized_record_scope(path, "ModelAge", "Person",
                                                    record_scope)) {
            *record_kind = MetadataConceptRecordKind::Person;
            *role        = MetadataConceptRole::Age;
            return true;
        }
        if (descriptive_xmp_normalized_record_scope(path,
                                                    "OrganisationInImageName",
                                                    "Organization",
                                                    record_scope)) {
            *record_kind = MetadataConceptRecordKind::Organization;
            *role        = MetadataConceptRole::Name;
            return true;
        }
        if (descriptive_xmp_normalized_record_scope(path,
                                                    "OrganisationInImageCode",
                                                    "Organization",
                                                    record_scope)) {
            *record_kind = MetadataConceptRecordKind::Organization;
            *role        = MetadataConceptRole::Identifier;
            return true;
        }
        if (ascii_equal_ci(leaf, "Event")) {
            *record_kind = MetadataConceptRecordKind::Event;
            *role        = MetadataConceptRole::Name;
            record_scope->assign("Event");
            return true;
        }
        if (descriptive_xmp_normalized_record_scope(path, "EventId", "Event",
                                                    record_scope)) {
            *record_kind = MetadataConceptRecordKind::Event;
            *role        = MetadataConceptRole::Identifier;
            record_scope->assign("Event");
            return true;
        }
        return false;
    }

    static bool map_xmp_mm_resource_reference_role(std::string_view leaf,
                                                   MetadataConceptRole* role)
    {
        if (ascii_equal_ci(leaf, "documentID")) {
            *role = MetadataConceptRole::DocumentIdentifier;
        } else if (ascii_equal_ci(leaf, "instanceID")) {
            *role = MetadataConceptRole::InstanceIdentifier;
        } else if (ascii_equal_ci(leaf, "originalDocumentID")) {
            *role = MetadataConceptRole::OriginalDocumentIdentifier;
        } else if (ascii_equal_ci(leaf, "renditionClass")) {
            *role = MetadataConceptRole::RenditionClass;
        } else if (ascii_equal_ci(leaf, "renditionParams")) {
            *role = MetadataConceptRole::RenditionParameters;
        } else if (ascii_equal_ci(leaf, "versionID")) {
            *role = MetadataConceptRole::VersionIdentifier;
        } else if (ascii_equal_ci(leaf, "manager")) {
            *role = MetadataConceptRole::Manager;
        } else if (ascii_equal_ci(leaf, "managerVariant")) {
            *role = MetadataConceptRole::ManagerVariant;
        } else if (ascii_equal_ci(leaf, "manageTo")) {
            *role = MetadataConceptRole::ManageTo;
        } else if (ascii_equal_ci(leaf, "manageUI")) {
            *role = MetadataConceptRole::ManageUi;
        } else if (ascii_equal_ci(leaf, "alternatePaths")) {
            *role = MetadataConceptRole::AlternatePath;
        } else if (ascii_equal_ci(leaf, "filePath")) {
            *role = MetadataConceptRole::FilePath;
        } else if (ascii_equal_ci(leaf, "fromPart")) {
            *role = MetadataConceptRole::FromPart;
        } else if (ascii_equal_ci(leaf, "toPart")) {
            *role = MetadataConceptRole::ToPart;
        } else if (ascii_equal_ci(leaf, "lastModifyDate")) {
            *role = MetadataConceptRole::LastModifiedDate;
        } else if (ascii_equal_ci(leaf, "maskMarkers")) {
            *role = MetadataConceptRole::MaskMarkers;
        } else if (ascii_equal_ci(leaf, "partMapping")) {
            *role = MetadataConceptRole::PartMapping;
        } else if (ascii_equal_ci(leaf, "lastURL")) {
            *role = MetadataConceptRole::LastUrl;
        } else if (ascii_equal_ci(leaf, "linkForm")) {
            *role = MetadataConceptRole::LinkForm;
        } else if (ascii_equal_ci(leaf, "linkCategory")) {
            *role = MetadataConceptRole::LinkCategory;
        } else if (ascii_equal_ci(leaf, "placedXResolution")) {
            *role = MetadataConceptRole::PlacedXResolution;
        } else if (ascii_equal_ci(leaf, "placedYResolution")) {
            *role = MetadataConceptRole::PlacedYResolution;
        } else if (ascii_equal_ci(leaf, "placedResolutionUnit")) {
            *role = MetadataConceptRole::PlacedResolutionUnit;
        }
        return *role != MetadataConceptRole::Primary;
    }

    static bool map_xmp_mm_resource_event_role(std::string_view leaf,
                                               MetadataConceptRole* role)
    {
        if (ascii_equal_ci(leaf, "action")) {
            *role = MetadataConceptRole::EventAction;
        } else if (ascii_equal_ci(leaf, "instanceID")) {
            *role = MetadataConceptRole::InstanceIdentifier;
        } else if (ascii_equal_ci(leaf, "parameters")) {
            *role = MetadataConceptRole::EventParameters;
        } else if (ascii_equal_ci(leaf, "softwareAgent")) {
            *role = MetadataConceptRole::SoftwareAgent;
        } else if (ascii_equal_ci(leaf, "when")) {
            *role = MetadataConceptRole::EventWhen;
        } else if (ascii_equal_ci(leaf, "changed")) {
            *role = MetadataConceptRole::ChangedParts;
        }
        return *role != MetadataConceptRole::Primary;
    }

    static bool map_xmp_mm_structured_descriptive(
        std::string_view path, std::string_view leaf, MetadataConceptRole* role,
        MetadataConceptRecordKind* record_kind, std::string* record_scope)
    {
        const std::string_view reference_roots[]
            = { "DerivedFrom", "ManagedFrom", "RenditionOf", "Ingredients" };
        const std::string_view reference_scopes[]
            = { "DerivedFrom", "ManagedFrom", "RenditionOf", "Ingredient" };
        for (size_t i = 0U; i < 4U; ++i) {
            if (!descriptive_xmp_normalized_record_scope(path,
                                                         reference_roots[i],
                                                         reference_scopes[i],
                                                         record_scope)) {
                continue;
            }
            *record_kind = MetadataConceptRecordKind::ResourceReference;
            return map_xmp_mm_resource_reference_role(leaf, role);
        }
        if (descriptive_xmp_normalized_record_scope(path, "Manifest",
                                                    "Manifest", record_scope)
            && ascii_contains_ci(path, "reference/")) {
            record_scope->append("/Reference");
            *record_kind = MetadataConceptRecordKind::ResourceReference;
            return map_xmp_mm_resource_reference_role(leaf, role);
        }
        if (descriptive_xmp_normalized_record_scope(path, "History",
                                                    "HistoryEvent",
                                                    record_scope)) {
            *record_kind = MetadataConceptRecordKind::ResourceEvent;
            return map_xmp_mm_resource_event_role(leaf, role);
        }
        if (descriptive_xmp_normalized_record_scope(path, "Versions", "Version",
                                                    record_scope)
            && ascii_contains_ci(path, "event/")) {
            record_scope->append("/Event");
            *record_kind = MetadataConceptRecordKind::ResourceEvent;
            return map_xmp_mm_resource_event_role(leaf, role);
        }
        if (descriptive_xmp_normalized_record_scope(path, "Pantry",
                                                    "PantryItem",
                                                    record_scope)) {
            *record_kind = MetadataConceptRecordKind::PantryItem;
            if (ascii_equal_ci(leaf, "InstanceID")) {
                *role = MetadataConceptRole::InstanceIdentifier;
            } else if (ascii_equal_ci(leaf, "format")) {
                *role = MetadataConceptRole::Format;
            }
            return *role != MetadataConceptRole::Primary;
        }
        return false;
    }

    static bool map_plus_record_descriptive(
        std::string_view path, std::string_view leaf, MetadataConceptRole* role,
        MetadataConceptRecordKind* record_kind, std::string* record_scope)
    {
        if (descriptive_xmp_normalized_record_scope(path, "CopyrightOwner",
                                                    "CopyrightOwner",
                                                    record_scope)) {
            *record_kind = MetadataConceptRecordKind::RightsHolder;
            if (ascii_equal_ci(leaf, "CopyrightOwnerName")) {
                *role = MetadataConceptRole::RightsHolderName;
            } else if (ascii_equal_ci(leaf, "CopyrightOwnerID")) {
                *role = MetadataConceptRole::RightsHolderIdentifier;
            } else if (ascii_equal_ci(leaf, "CopyrightOwnerImageID")) {
                *role = MetadataConceptRole::ImageIdentifier;
            }
            return *role != MetadataConceptRole::Primary;
        }
        if (descriptive_xmp_normalized_record_scope(path, "Licensor",
                                                    "Licensor", record_scope)) {
            *record_kind = MetadataConceptRecordKind::Licensor;
            if (ascii_equal_ci(leaf, "LicensorName")) {
                *role = MetadataConceptRole::LicensorName;
            } else if (ascii_equal_ci(leaf, "LicensorID")) {
                *role = MetadataConceptRole::LicensorIdentifier;
            } else if (ascii_equal_ci(leaf, "LicensorStreetAddress")
                       || ascii_equal_ci(leaf, "LicensorExtendedAddress")) {
                *role = MetadataConceptRole::Address;
            } else if (ascii_equal_ci(leaf, "LicensorCity")) {
                *role = MetadataConceptRole::City;
            } else if (ascii_equal_ci(leaf, "LicensorRegion")) {
                *role = MetadataConceptRole::ProvinceState;
            } else if (ascii_equal_ci(leaf, "LicensorPostalCode")) {
                *role = MetadataConceptRole::PostalCode;
            } else if (ascii_equal_ci(leaf, "LicensorCountry")) {
                *role = MetadataConceptRole::CountryName;
            } else if (ascii_equal_ci(leaf, "LicensorTelephone1")
                       || ascii_equal_ci(leaf, "LicensorTelephone2")) {
                *role = MetadataConceptRole::Telephone;
            } else if (ascii_equal_ci(leaf, "LicensorTelephoneType1")
                       || ascii_equal_ci(leaf, "LicensorTelephoneType2")) {
                *role = MetadataConceptRole::Characteristic;
            } else if (ascii_equal_ci(leaf, "LicensorEmail")) {
                *role = MetadataConceptRole::Email;
            } else if (ascii_equal_ci(leaf, "LicensorURL")) {
                *role = MetadataConceptRole::Url;
            } else if (ascii_equal_ci(leaf, "LicensorNotes")) {
                *role = MetadataConceptRole::Notes;
            } else if (ascii_equal_ci(leaf, "LicensorImageID")) {
                *role = MetadataConceptRole::ImageIdentifier;
            }
            return *role != MetadataConceptRole::Primary;
        }
        if (descriptive_xmp_normalized_record_scope(path, "Licensee",
                                                    "Licensee", record_scope)) {
            *record_kind = MetadataConceptRecordKind::Licensee;
            if (ascii_equal_ci(leaf, "LicenseeName")) {
                *role = MetadataConceptRole::Name;
            } else if (ascii_equal_ci(leaf, "LicenseeID")) {
                *role = MetadataConceptRole::Identifier;
            } else if (ascii_equal_ci(leaf, "LicenseeURL")) {
                *role = MetadataConceptRole::Url;
            }
            return *role != MetadataConceptRole::Primary;
        }
        if (descriptive_xmp_normalized_record_scope(path, "EndUser", "EndUser",
                                                    record_scope)) {
            *record_kind = MetadataConceptRecordKind::EndUser;
            if (ascii_equal_ci(leaf, "EndUserName")) {
                *role = MetadataConceptRole::Name;
            } else if (ascii_equal_ci(leaf, "EndUserID")) {
                *role = MetadataConceptRole::Identifier;
            }
            return *role != MetadataConceptRole::Primary;
        }
        if (descriptive_xmp_normalized_record_scope(path, "ImageCreator",
                                                    "ImageCreator",
                                                    record_scope)) {
            *record_kind = MetadataConceptRecordKind::ImageCreator;
            if (ascii_equal_ci(leaf, "ImageCreatorName")) {
                *role = MetadataConceptRole::Name;
            } else if (ascii_equal_ci(leaf, "ImageCreatorID")) {
                *role = MetadataConceptRole::Identifier;
            } else if (ascii_equal_ci(leaf, "ImageCreatorImageID")) {
                *role = MetadataConceptRole::ImageIdentifier;
            }
            return *role != MetadataConceptRole::Primary;
        }
        return false;
    }

    static bool map_plus_license_descriptive(
        std::string_view leaf, MetadataConceptRole* role,
        MetadataConceptRecordKind* record_kind, std::string* record_scope)
    {
        *record_kind = MetadataConceptRecordKind::License;
        if (ascii_equal_ci(leaf, "CopyrightStatus")) {
            *record_kind = MetadataConceptRecordKind::RightsHolder;
            *role        = MetadataConceptRole::CopyrightStatus;
            record_scope->assign("Copyright");
        } else if (ascii_equal_ci(leaf, "CopyrightRegistrationNumber")) {
            *record_kind = MetadataConceptRecordKind::RightsHolder;
            *role        = MetadataConceptRole::CopyrightRegistrationNumber;
            record_scope->assign("Copyright");
        } else if (ascii_equal_ci(leaf, "FirstPublicationDate")) {
            *record_kind = MetadataConceptRecordKind::RightsHolder;
            *role        = MetadataConceptRole::FirstPublicationDate;
            record_scope->assign("Copyright");
        } else if (ascii_equal_ci(leaf, "LicenseID")) {
            *role = MetadataConceptRole::LicenseIdentifier;
        } else if (ascii_equal_ci(leaf, "MediaSummaryCode")) {
            *role = MetadataConceptRole::MediaSummaryCode;
        } else if (ascii_equal_ci(leaf, "LicenseStartDate")) {
            *role = MetadataConceptRole::LicenseStartDate;
        } else if (ascii_equal_ci(leaf, "LicenseEndDate")) {
            *role = MetadataConceptRole::LicenseEndDate;
        } else if (ascii_equal_ci(leaf, "MediaConstraints")) {
            *role = MetadataConceptRole::MediaConstraint;
        } else if (ascii_equal_ci(leaf, "RegionConstraints")) {
            *role = MetadataConceptRole::RegionConstraint;
        } else if (ascii_equal_ci(leaf, "ProductOrServiceConstraints")) {
            *role = MetadataConceptRole::ProductOrServiceConstraint;
        } else if (ascii_equal_ci(leaf, "ImageFileConstraints")) {
            *role = MetadataConceptRole::ImageFileConstraint;
        } else if (ascii_equal_ci(leaf, "ImageAlterationConstraints")) {
            *role = MetadataConceptRole::ImageAlterationConstraint;
        } else if (ascii_equal_ci(leaf, "ImageDuplicationConstraints")) {
            *role = MetadataConceptRole::ImageDuplicationConstraint;
        } else if (ascii_equal_ci(leaf, "OtherLicenseRequirements")) {
            *role = MetadataConceptRole::OtherLicenseRequirement;
        } else if (ascii_equal_ci(leaf, "OtherConditions")
                   || ascii_equal_ci(leaf, "OtherConstraints")) {
            *role = MetadataConceptRole::OtherCondition;
        } else if (ascii_equal_ci(leaf, "TermsAndConditionsText")) {
            *role = MetadataConceptRole::RightsUsageTerms;
        } else if (ascii_equal_ci(leaf, "TermsAndConditionsURL")) {
            *role = MetadataConceptRole::LicenseTermsUrl;
        } else if (ascii_equal_ci(leaf, "LicensorTransactionID")) {
            *role = MetadataConceptRole::LicensorTransactionIdentifier;
        } else if (ascii_equal_ci(leaf, "LicenseeTransactionID")) {
            *role = MetadataConceptRole::LicenseeTransactionIdentifier;
        } else if (ascii_equal_ci(leaf, "LicenseeProjectReference")) {
            *role = MetadataConceptRole::LicenseeProjectReference;
        } else if (ascii_equal_ci(leaf, "LicenseTransactionDate")) {
            *role = MetadataConceptRole::LicenseTransactionDate;
        } else if (ascii_equal_ci(leaf, "CreditLineRequired")) {
            *role = MetadataConceptRole::CreditLineRequired;
        } else if (ascii_equal_ci(leaf, "AdultContentWarning")) {
            *role = MetadataConceptRole::AdultContentWarning;
        } else if (ascii_equal_ci(leaf, "Reuse")) {
            *role = MetadataConceptRole::Reuse;
        } else if (ascii_equal_ci(leaf, "DataMining")) {
            *role = MetadataConceptRole::DataMining;
        } else if (ascii_equal_ci(leaf, "OtherLicenseDocuments")) {
            *role = MetadataConceptRole::OtherLicenseDocument;
        } else if (ascii_equal_ci(leaf, "OtherLicenseInfo")) {
            *role = MetadataConceptRole::OtherLicenseInformation;
        } else if (ascii_equal_ci(leaf, "LicensorImageID")) {
            *record_kind = MetadataConceptRecordKind::Licensor;
            *role        = MetadataConceptRole::ImageIdentifier;
            record_scope->assign("Licensor");
        } else if (ascii_equal_ci(leaf, "LicenseeImageID")) {
            *record_kind = MetadataConceptRecordKind::Licensee;
            *role        = MetadataConceptRole::ImageIdentifier;
            record_scope->assign("Licensee");
        } else if (ascii_equal_ci(leaf, "LicenseeImageNotes")) {
            *record_kind = MetadataConceptRecordKind::Licensee;
            *role        = MetadataConceptRole::Notes;
            record_scope->assign("Licensee");
        } else if (ascii_equal_ci(leaf, "ImageSupplierName")) {
            *record_kind = MetadataConceptRecordKind::ImageSupplier;
            *role        = MetadataConceptRole::Name;
            record_scope->assign("ImageSupplier");
        } else if (ascii_equal_ci(leaf, "ImageSupplierID")) {
            *record_kind = MetadataConceptRecordKind::ImageSupplier;
            *role        = MetadataConceptRole::Identifier;
            record_scope->assign("ImageSupplier");
        } else if (ascii_equal_ci(leaf, "ImageSupplierImageID")) {
            *record_kind = MetadataConceptRecordKind::ImageSupplier;
            *role        = MetadataConceptRole::ImageIdentifier;
            record_scope->assign("ImageSupplier");
        } else if (ascii_equal_ci(leaf, "ImageType")) {
            *record_kind = MetadataConceptRecordKind::ImageAsset;
            *role        = MetadataConceptRole::DeliveredImageType;
            record_scope->assign("ImageAsset");
        } else if (ascii_equal_ci(leaf, "FileNameAsDelivered")) {
            *record_kind = MetadataConceptRecordKind::ImageAsset;
            *role        = MetadataConceptRole::DeliveredFileName;
            record_scope->assign("ImageAsset");
        } else if (ascii_equal_ci(leaf, "ImageFileFormatAsDelivered")) {
            *record_kind = MetadataConceptRecordKind::ImageAsset;
            *role        = MetadataConceptRole::DeliveredFileFormat;
            record_scope->assign("ImageAsset");
        } else if (ascii_equal_ci(leaf, "ImageFileSizeAsDelivered")) {
            *record_kind = MetadataConceptRecordKind::ImageAsset;
            *role        = MetadataConceptRole::DeliveredFileSize;
            record_scope->assign("ImageAsset");
        } else if (ascii_equal_ci(leaf, "OtherImageInfo")) {
            *record_kind = MetadataConceptRecordKind::ImageAsset;
            *role        = MetadataConceptRole::OtherImageInformation;
            record_scope->assign("ImageAsset");
        } else if (ascii_equal_ci(leaf, "ModelReleaseStatus")
                   || ascii_equal_ci(leaf, "PropertyReleaseStatus")) {
            *record_kind = MetadataConceptRecordKind::Release;
            *role        = MetadataConceptRole::ReleaseStatus;
            record_scope->assign(ascii_equal_ci(leaf, "ModelReleaseStatus")
                                     ? "ModelRelease"
                                     : "PropertyRelease");
        } else if (ascii_equal_ci(leaf, "ModelReleaseID")
                   || ascii_equal_ci(leaf, "PropertyReleaseID")) {
            *record_kind = MetadataConceptRecordKind::Release;
            *role        = MetadataConceptRole::ReleaseIdentifier;
            record_scope->assign(ascii_equal_ci(leaf, "ModelReleaseID")
                                     ? "ModelRelease"
                                     : "PropertyRelease");
        } else if (ascii_equal_ci(leaf, "MinorModelAgeDisclosure")) {
            *record_kind = MetadataConceptRecordKind::Release;
            *role        = MetadataConceptRole::MinorModelAgeDisclosure;
            record_scope->assign("ModelRelease");
        }
        return *role != MetadataConceptRole::Primary;
    }

    static void append_xmp_descriptive_candidate(const MetaStore& store,
                                                 EntryId id, const Entry& entry,
                                                 MetadataConceptResolution* out)
    {
        const std::string_view path
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.property_path);
        const std::string_view leaf           = xmp_property_leaf(path);
        MetadataConceptRole role              = MetadataConceptRole::Primary;
        MetadataConceptRecordKind record_kind = MetadataConceptRecordKind::None;
        std::string location_scope;
        std::string record_scope;
        uint8_t priority = 0U;

        if (xmp_schema_matches(store, entry, kDcXmpSchema)) {
            if (ascii_equal_ci(leaf, "title")) {
                role = MetadataConceptRole::Title;
            } else if (ascii_equal_ci(leaf, "description")) {
                role = MetadataConceptRole::Description;
            } else if (ascii_equal_ci(leaf, "creator")) {
                role = MetadataConceptRole::Creator;
            } else if (ascii_equal_ci(leaf, "subject")) {
                role = MetadataConceptRole::Keywords;
            } else if (ascii_equal_ci(leaf, "rights")) {
                role = MetadataConceptRole::CopyrightNotice;
            } else if (ascii_equal_ci(leaf, "identifier")) {
                role         = MetadataConceptRole::ResourceIdentifier;
                record_kind  = MetadataConceptRecordKind::ImageAsset;
                record_scope = "ImageAsset";
            } else if (ascii_equal_ci(leaf, "source")) {
                role         = MetadataConceptRole::DerivedFromIdentifier;
                record_kind  = MetadataConceptRecordKind::ImageAsset;
                record_scope = "ImageAsset";
            }
            priority = 100U;
        } else if (xmp_schema_matches(store, entry, kPhotoshopXmpSchema)) {
            if (ascii_equal_ci(leaf, "Headline")) {
                role = MetadataConceptRole::Headline;
            } else if (ascii_equal_ci(leaf, "Credit")) {
                role = MetadataConceptRole::CreditLine;
            } else if (ascii_equal_ci(leaf, "Source")) {
                role = MetadataConceptRole::Source;
            } else if (ascii_equal_ci(leaf, "Urgency")) {
                role = MetadataConceptRole::Urgency;
            } else if (ascii_equal_ci(leaf, "Category")) {
                role = MetadataConceptRole::Category;
            } else if (ascii_equal_ci(leaf, "SupplementalCategories")) {
                role = MetadataConceptRole::SupplementalCategory;
            } else if (ascii_equal_ci(leaf, "Instructions")) {
                role = MetadataConceptRole::Instructions;
            } else if (ascii_equal_ci(leaf, "AuthorsPosition")) {
                role = MetadataConceptRole::CreatorTitle;
            } else if (ascii_equal_ci(leaf, "TransmissionReference")) {
                role = MetadataConceptRole::TransmissionReference;
            } else if (ascii_equal_ci(leaf, "CaptionWriter")) {
                role = MetadataConceptRole::CaptionWriter;
            } else {
                role = descriptive_location_role_for_leaf(leaf);
                if (descriptive_role_is_location(role)) {
                    location_scope.assign("LocationCreated");
                }
            }
            priority = 92U;
        } else if (xmp_schema_matches(store, entry, kXmpBasicSchema)) {
            if (ascii_equal_ci(leaf, "Identifier")) {
                role         = MetadataConceptRole::ResourceIdentifier;
                record_kind  = MetadataConceptRecordKind::ImageAsset;
                record_scope = "ImageAsset";
            }
            priority = 100U;
        } else if (xmp_schema_matches(store, entry, kXmpMmSchema)) {
            if (map_xmp_mm_structured_descriptive(path, leaf, &role,
                                                  &record_kind,
                                                  &record_scope)) {
                priority = 100U;
            } else if (path.find('/') == std::string_view::npos
                       && map_xmp_mm_resource_reference_role(leaf, &role)) {
                record_kind  = MetadataConceptRecordKind::ImageAsset;
                record_scope = "ImageAsset";
                priority     = 100U;
            } else {
                return;
            }
        } else if (xmp_schema_matches(store, entry, kIptcCoreXmpSchema)) {
            if (map_iptc_core_structured_descriptive(path, leaf, &role,
                                                     &record_kind,
                                                     &record_scope)) {
                priority = 100U;
            } else if (map_iptc_core_flat_descriptive(leaf, &role, &record_kind,
                                                      &record_scope)) {
                priority = 100U;
            } else if (!descriptive_xmp_location_scope(path, &location_scope)) {
                if (ascii_equal_ci(leaf, "Location")
                    || ascii_equal_ci(leaf, "CountryCode")) {
                    location_scope.assign("LocationCreated");
                } else {
                    return;
                }
            }
            if (role == MetadataConceptRole::Primary) {
                role = descriptive_location_role_for_leaf(leaf);
            }
            if (priority == 0U) {
                priority = 96U;
            }
        } else if (xmp_schema_matches(store, entry, kIptcExtXmpSchema)) {
            if (!descriptive_xmp_location_scope(path, &location_scope)) {
                if (ascii_equal_ci(leaf, "DigitalSourceType")) {
                    role = MetadataConceptRole::DigitalSourceType;
                } else if (!map_iptc_ext_structured_descriptive(
                               path, leaf, &role, &record_kind, &record_scope)) {
                    return;
                }
            } else {
                role = descriptive_location_role_for_leaf(leaf);
            }
            priority = 100U;
        } else if (xmp_schema_matches(store, entry, kXmpRightsSchema)) {
            if (ascii_equal_ci(leaf, "Certificate")) {
                role = MetadataConceptRole::RightsCertificate;
            } else if (ascii_equal_ci(leaf, "Marked")) {
                role = MetadataConceptRole::RightsMarked;
            } else if (ascii_equal_ci(leaf, "Owner")) {
                role        = MetadataConceptRole::RightsHolderName;
                record_kind = MetadataConceptRecordKind::RightsHolder;
                descriptive_xmp_normalized_record_scope(path, "Owner",
                                                        "RightsHolder",
                                                        &record_scope);
            } else if (ascii_equal_ci(leaf, "UsageTerms")) {
                role = MetadataConceptRole::RightsUsageTerms;
            } else if (ascii_equal_ci(leaf, "WebStatement")) {
                role = MetadataConceptRole::RightsWebStatement;
            }
            priority = 100U;
        } else if (xmp_schema_matches(store, entry, kPlusXmpSchema)) {
            if (!map_plus_record_descriptive(path, leaf, &role, &record_kind,
                                             &record_scope)
                && !map_plus_license_descriptive(leaf, &role, &record_kind,
                                                 &record_scope)) {
                return;
            }
            priority = 100U;
        } else {
            return;
        }
        if (role == MetadataConceptRole::Primary) {
            return;
        }
        append_descriptive_text_candidate(store, id, role, record_kind,
                                          priority, location_scope,
                                          record_scope, path, out);
    }

    static void append_descriptive_candidates(const MetaStore& store,
                                              MetadataConceptResolution* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            switch (entry.key.kind) {
            case MetaKeyKind::ExifTag:
                append_exif_descriptive_candidate(store, id, entry, out);
                break;
            case MetaKeyKind::IptcDataset:
                append_iptc_descriptive_candidate(store, id, entry, out);
                break;
            case MetaKeyKind::XmpProperty:
                append_xmp_descriptive_candidate(store, id, entry, out);
                break;
            default: break;
            }
        }
    }

    static void append_container_graph_candidates(const MetaStore& store,
                                                  MetadataConceptResolution* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::BmffField) {
                continue;
            }

            MetadataConceptRole role = MetadataConceptRole::Primary;
            uint8_t priority         = 0U;
            if (bmff_entry_field_matches(store, entry,
                                         "scene.content_bound_metadata_policy")
                || bmff_entry_field_matches(store, entry,
                                            "scene.component.metadata_policy")
                || bmff_entry_field_matches(
                    store, entry,
                    "scene.primary_graph_component_metadata_policy")) {
                role     = MetadataConceptRole::ContentBoundMetadata;
                priority = 90U;
            } else if (bmff_entry_field_matches(store, entry,
                                                "scene.multi_image_candidate")
                       || bmff_entry_field_matches(store, entry,
                                                   "scene.multi_image_policy")
                       || bmff_entry_field_matches(
                           store, entry, "scene.component.multi_image_policy")
                       || bmff_entry_field_matches(
                           store, entry, "scene.component.multi_image_candidate")
                       || bmff_entry_field_matches(
                           store, entry,
                           "scene.primary_graph_component_multi_image_"
                           "policy")
                       || bmff_entry_field_matches(
                           store, entry,
                           "scene.primary_graph_component_multi_image_"
                           "candidate")) {
                role     = MetadataConceptRole::MultiImageScene;
                priority = 88U;
            } else if (bmff_entry_field_matches(store, entry,
                                                "derived_image.construction")
                       || bmff_entry_field_matches(
                           store, entry, "primary.derived_construction")) {
                role     = MetadataConceptRole::DerivedImageConstruction;
                priority = 89U;
            } else if (bmff_entry_field_matches(store, entry,
                                                "tiled_image.configuration")) {
                role     = MetadataConceptRole::TiledImageConfiguration;
                priority = 89U;
            } else {
                continue;
            }

            MetadataQueryValueShape shape = MetadataQueryValueShape::Unknown;
            MetadataConceptCandidate candidate;
            if (entry.value.kind == MetaValueKind::Text) {
                shape     = MetadataQueryValueShape::Text;
                candidate = make_entry_candidate(
                    store, id, MetadataConceptKind::ContainerGraph, role,
                    MetadataQuerySemanticKind::SourceProcessing, shape,
                    priority);
                candidate.text = std::string(
                    arena_string(store.arena(), entry.value.data.span));
                if (role == MetadataConceptRole::MultiImageScene) {
                    candidate.has_numeric   = true;
                    candidate.numeric_count = 1U;
                    candidate.numeric[0]    = 1.0;
                    candidate.value_key     = numeric_key(1.0);
                } else {
                    candidate.value_key = candidate.text;
                }
            } else {
                double value = 0.0;
                if (!scalar_to_double(entry.value, &value) || value <= 0.0) {
                    continue;
                }
                shape     = MetadataQueryValueShape::Scalar;
                candidate = make_entry_candidate(
                    store, id, MetadataConceptKind::ContainerGraph, role,
                    MetadataQuerySemanticKind::SourceProcessing, shape,
                    priority);
                candidate.has_numeric   = true;
                candidate.numeric_count = 1U;
                candidate.numeric[0]    = value;
                candidate.value_key     = numeric_key(value);
            }
            append_candidate(out, candidate);
        }
    }

    static MetadataConceptRole
    conflict_group_role(MetadataConceptRole role) noexcept
    {
        if (role == MetadataConceptRole::DateCreated) {
            return MetadataConceptRole::Created;
        }
        return role;
    }

    static bool role_uses_location_scope(MetadataConceptRole role) noexcept
    {
        switch (role) {
        case MetadataConceptRole::LocationShownLatitude:
        case MetadataConceptRole::LocationShownLongitude:
        case MetadataConceptRole::LocationShownAltitude:
        case MetadataConceptRole::LocationCreatedLatitude:
        case MetadataConceptRole::LocationCreatedLongitude:
        case MetadataConceptRole::LocationCreatedAltitude:
        case MetadataConceptRole::LocationName:
        case MetadataConceptRole::Sublocation:
        case MetadataConceptRole::City:
        case MetadataConceptRole::ProvinceState:
        case MetadataConceptRole::CountryName:
        case MetadataConceptRole::CountryCode:
        case MetadataConceptRole::WorldRegion:
        case MetadataConceptRole::LocationIdentifier: return true;
        case MetadataConceptRole::Primary:
        case MetadataConceptRole::Orientation:
        case MetadataConceptRole::Created:
        case MetadataConceptRole::Digitized:
        case MetadataConceptRole::Modified:
        case MetadataConceptRole::MetadataDate:
        case MetadataConceptRole::DateCreated:
        case MetadataConceptRole::ColorSpace:
        case MetadataConceptRole::IccProfile:
        case MetadataConceptRole::ColorMatrix:
        case MetadataConceptRole::WhiteBalance:
        case MetadataConceptRole::Latitude:
        case MetadataConceptRole::Longitude:
        case MetadataConceptRole::Altitude:
        case MetadataConceptRole::Timestamp:
        case MetadataConceptRole::Crop:
        case MetadataConceptRole::ActiveArea:
        case MetadataConceptRole::Border:
        case MetadataConceptRole::SensorGeometry:
        case MetadataConceptRole::LensCorrection:
        case MetadataConceptRole::BlackLevel:
        case MetadataConceptRole::WhiteLevel:
        case MetadataConceptRole::Linearization:
        case MetadataConceptRole::CfaLayout:
        case MetadataConceptRole::RawStorage:
        case MetadataConceptRole::SourceProcessing:
        case MetadataConceptRole::ComputationalProcessing:
        case MetadataConceptRole::ThermalProcessing:
        case MetadataConceptRole::StitchProcessing:
        case MetadataConceptRole::ExposureTime:
        case MetadataConceptRole::Aperture:
        case MetadataConceptRole::IsoSensitivity:
        case MetadataConceptRole::ExposureBias:
        case MetadataConceptRole::ExposureProgram:
        case MetadataConceptRole::Gain:
        case MetadataConceptRole::RawExposureAdjustment:
        case MetadataConceptRole::SourceColorTransform:
        case MetadataConceptRole::RawValueCurve:
        case MetadataConceptRole::RawLinearityLimit:
        case MetadataConceptRole::RawCalibrationCurve:
        case MetadataConceptRole::RawCurveControlPoints:
        case MetadataConceptRole::ContentBoundMetadata:
        case MetadataConceptRole::MultiImageScene:
        case MetadataConceptRole::DerivedImageConstruction:
        case MetadataConceptRole::TiledImageConfiguration:
        case MetadataConceptRole::DestinationLatitude:
        case MetadataConceptRole::DestinationLongitude:
        case MetadataConceptRole::Title:
        case MetadataConceptRole::Headline:
        case MetadataConceptRole::Description:
        case MetadataConceptRole::Creator:
        case MetadataConceptRole::Keywords:
        case MetadataConceptRole::CopyrightNotice:
        case MetadataConceptRole::CopyrightStatus:
        case MetadataConceptRole::RightsUsageTerms:
        case MetadataConceptRole::RightsWebStatement:
        case MetadataConceptRole::RightsCertificate:
        case MetadataConceptRole::RightsMarked:
        case MetadataConceptRole::RightsHolderName:
        case MetadataConceptRole::RightsHolderIdentifier:
        case MetadataConceptRole::LicenseIdentifier:
        case MetadataConceptRole::LicenseTermsUrl:
        case MetadataConceptRole::LicensorName:
        case MetadataConceptRole::LicensorIdentifier:
        case MetadataConceptRole::CreditLine:
        case MetadataConceptRole::CreditLineRequired:
        case MetadataConceptRole::Source:
        case MetadataConceptRole::DigitalSourceType: break;
        default: break;
        }
        return false;
    }

    static bool role_uses_language_scope(MetadataConceptRole role) noexcept
    {
        return descriptive_role_is_localized(role);
    }

    static bool role_uses_record_scope(MetadataConceptRole role) noexcept
    {
        return static_cast<uint8_t>(role)
               >= static_cast<uint8_t>(MetadataConceptRole::Title);
    }

    static bool
    candidates_share_role_scope(MetadataConceptRole role,
                                const MetadataConceptCandidate& a,
                                const MetadataConceptCandidate& b) noexcept
    {
        if (role_uses_record_scope(role)
            && (a.record_kind != b.record_kind
                || a.record_scope != b.record_scope)) {
            return false;
        }
        if (role_uses_location_scope(role)
            && a.location_scope != b.location_scope) {
            return false;
        }
        if (role_uses_language_scope(role) && a.language != b.language) {
            return false;
        }
        return true;
    }

    static int64_t civil_day_number(int32_t year, uint32_t month,
                                    uint32_t day) noexcept
    {
        year -= month <= 2U ? 1 : 0;
        const int32_t era = year >= 0 ? year / 400 : (year - 399) / 400;
        const uint32_t year_of_era   = static_cast<uint32_t>(year - era * 400);
        const int32_t adjusted_month = static_cast<int32_t>(month)
                                       + (month > 2U ? -3 : 9);
        const uint32_t day_of_year
            = (153U * static_cast<uint32_t>(adjusted_month) + 2U) / 5U + day
              - 1U;
        const uint32_t day_of_era = year_of_era * 365U + year_of_era / 4U
                                    - year_of_era / 100U + day_of_year;
        return static_cast<int64_t>(era) * 146097LL
               + static_cast<int64_t>(day_of_era);
    }

    static bool date_time_utc_second(const MetadataConceptCandidate& candidate,
                                     int64_t* out) noexcept
    {
        if (!out || !candidate.has_date_time || !candidate.date_time_has_time
            || !candidate.date_time_has_utc_offset
            || candidate.date_time_second > 59U) {
            return false;
        }
        const int64_t days
            = civil_day_number(static_cast<int32_t>(candidate.date_time_year),
                               candidate.date_time_month,
                               candidate.date_time_day);
        *out = days * 86400LL
               + static_cast<int64_t>(candidate.date_time_hour) * 3600LL
               + static_cast<int64_t>(candidate.date_time_minute) * 60LL
               + static_cast<int64_t>(candidate.date_time_second)
               - static_cast<int64_t>(candidate.date_time_utc_offset_min)
                     * 60LL;
        return true;
    }

    static std::string_view
    normalized_subsecond(const MetadataConceptCandidate& candidate) noexcept
    {
        if (!candidate.date_time_has_subsecond
            || candidate.date_time_subsecond.empty()) {
            return {};
        }
        size_t digits = candidate.date_time_subsecond.size();
        while (digits > 1U
               && candidate.date_time_subsecond[digits - 1U] == '0') {
            digits -= 1U;
        }
        return std::string_view(candidate.date_time_subsecond.data(), digits);
    }

    static bool
    subsecond_values_conflict(const MetadataConceptCandidate& a,
                              const MetadataConceptCandidate& b) noexcept
    {
        if (!a.date_time_has_subsecond || !b.date_time_has_subsecond) {
            return false;
        }
        return normalized_subsecond(a) != normalized_subsecond(b);
    }

    static bool
    date_time_candidates_conflict(const MetadataConceptCandidate& a,
                                  const MetadataConceptCandidate& b) noexcept
    {
        if (!a.has_date_time || !b.has_date_time) {
            return false;
        }
        if (a.date_time_has_time && b.date_time_has_time
            && a.date_time_has_utc_offset && b.date_time_has_utc_offset) {
            int64_t a_utc_second = 0;
            int64_t b_utc_second = 0;
            if (date_time_utc_second(a, &a_utc_second)
                && date_time_utc_second(b, &b_utc_second)) {
                return a_utc_second != b_utc_second
                       || subsecond_values_conflict(a, b);
            }
        }
        if (a.date_time_year != b.date_time_year
            || a.date_time_month != b.date_time_month
            || a.date_time_day != b.date_time_day) {
            return true;
        }
        if (a.date_time_has_time && b.date_time_has_time) {
            if (a.date_time_hour != b.date_time_hour
                || a.date_time_minute != b.date_time_minute
                || a.date_time_second != b.date_time_second) {
                return true;
            }
            if (a.date_time_has_utc_offset && b.date_time_has_utc_offset
                && a.date_time_utc_offset_min != b.date_time_utc_offset_min) {
                return true;
            }
            if (subsecond_values_conflict(a, b)) {
                return true;
            }
        }
        return false;
    }

    static bool
    candidates_share_source_entries(const MetadataConceptCandidate& a,
                                    const MetadataConceptCandidate& b) noexcept
    {
        for (size_t i = 0U; i < a.source_entries.size(); ++i) {
            const EntryId entry_id = a.source_entries[i];
            if (entry_id == kInvalidEntryId) {
                continue;
            }
            for (size_t j = 0U; j < b.source_entries.size(); ++j) {
                if (b.source_entries[j] == entry_id) {
                    return true;
                }
            }
        }
        return false;
    }

    static double numeric_conflict_tolerance(MetadataConceptRole role) noexcept
    {
        switch (role) {
        case MetadataConceptRole::Latitude:
        case MetadataConceptRole::Longitude:
        case MetadataConceptRole::DestinationLatitude:
        case MetadataConceptRole::DestinationLongitude:
        case MetadataConceptRole::LocationShownLatitude:
        case MetadataConceptRole::LocationShownLongitude:
        case MetadataConceptRole::LocationCreatedLatitude:
        case MetadataConceptRole::LocationCreatedLongitude: return 0.0000001;
        case MetadataConceptRole::Altitude:
        case MetadataConceptRole::LocationShownAltitude:
        case MetadataConceptRole::LocationCreatedAltitude: return 0.001;
        case MetadataConceptRole::Primary:
        case MetadataConceptRole::Orientation:
        case MetadataConceptRole::Created:
        case MetadataConceptRole::Digitized:
        case MetadataConceptRole::Modified:
        case MetadataConceptRole::MetadataDate:
        case MetadataConceptRole::DateCreated:
        case MetadataConceptRole::ColorSpace:
        case MetadataConceptRole::IccProfile:
        case MetadataConceptRole::ColorMatrix:
        case MetadataConceptRole::WhiteBalance:
        case MetadataConceptRole::SourceColorTransform:
        case MetadataConceptRole::Timestamp:
        case MetadataConceptRole::Crop:
        case MetadataConceptRole::ActiveArea:
        case MetadataConceptRole::Border:
        case MetadataConceptRole::SensorGeometry:
        case MetadataConceptRole::LensCorrection:
        case MetadataConceptRole::BlackLevel:
        case MetadataConceptRole::WhiteLevel:
        case MetadataConceptRole::Linearization:
        case MetadataConceptRole::RawValueCurve:
        case MetadataConceptRole::RawLinearityLimit:
        case MetadataConceptRole::RawCalibrationCurve:
        case MetadataConceptRole::RawCurveControlPoints:
        case MetadataConceptRole::CfaLayout:
        case MetadataConceptRole::RawStorage:
        case MetadataConceptRole::SourceProcessing:
        case MetadataConceptRole::ComputationalProcessing:
        case MetadataConceptRole::ThermalProcessing:
        case MetadataConceptRole::StitchProcessing:
        case MetadataConceptRole::ExposureTime:
        case MetadataConceptRole::Aperture:
        case MetadataConceptRole::IsoSensitivity:
        case MetadataConceptRole::ExposureBias:
        case MetadataConceptRole::ExposureProgram:
        case MetadataConceptRole::Gain:
        case MetadataConceptRole::RawExposureAdjustment:
        case MetadataConceptRole::ContentBoundMetadata:
        case MetadataConceptRole::MultiImageScene:
        case MetadataConceptRole::DerivedImageConstruction:
        case MetadataConceptRole::TiledImageConfiguration:
        case MetadataConceptRole::Title:
        case MetadataConceptRole::Headline:
        case MetadataConceptRole::Description:
        case MetadataConceptRole::Creator:
        case MetadataConceptRole::Keywords:
        case MetadataConceptRole::LocationName:
        case MetadataConceptRole::Sublocation:
        case MetadataConceptRole::City:
        case MetadataConceptRole::ProvinceState:
        case MetadataConceptRole::CountryName:
        case MetadataConceptRole::CountryCode:
        case MetadataConceptRole::WorldRegion:
        case MetadataConceptRole::LocationIdentifier:
        case MetadataConceptRole::CopyrightNotice:
        case MetadataConceptRole::CopyrightStatus:
        case MetadataConceptRole::RightsUsageTerms:
        case MetadataConceptRole::RightsWebStatement:
        case MetadataConceptRole::RightsCertificate:
        case MetadataConceptRole::RightsMarked:
        case MetadataConceptRole::RightsHolderName:
        case MetadataConceptRole::RightsHolderIdentifier:
        case MetadataConceptRole::LicenseIdentifier:
        case MetadataConceptRole::LicenseTermsUrl:
        case MetadataConceptRole::LicensorName:
        case MetadataConceptRole::LicensorIdentifier:
        case MetadataConceptRole::CreditLine:
        case MetadataConceptRole::CreditLineRequired:
        case MetadataConceptRole::Source:
        case MetadataConceptRole::DigitalSourceType: break;
        default: break;
        }
        return 0.0;
    }

    static bool
    numeric_candidates_conflict(const MetadataConceptCandidate& a,
                                const MetadataConceptCandidate& b) noexcept
    {
        if (!a.has_numeric || !b.has_numeric) {
            return false;
        }
        if (a.numeric_count != b.numeric_count) {
            return true;
        }
        const double tolerance = numeric_conflict_tolerance(
            conflict_group_role(a.role));
        for (uint8_t i = 0U; i < a.numeric_count; ++i) {
            if (std::fabs(a.numeric[i] - b.numeric[i]) > tolerance) {
                return true;
            }
        }
        const MetadataConceptRole role = conflict_group_role(a.role);
        if ((role == MetadataConceptRole::Altitude
             || role == MetadataConceptRole::LocationShownAltitude
             || role == MetadataConceptRole::LocationCreatedAltitude)
            && a.has_gps_altitude_reference && b.has_gps_altitude_reference
            && a.gps_altitude_reference_code != b.gps_altitude_reference_code) {
            return true;
        }
        return false;
    }

    static bool
    concept_values_conflict(const MetadataConceptCandidate& a,
                            const MetadataConceptCandidate& b) noexcept
    {
        if (candidates_share_source_entries(a, b)) {
            return false;
        }
        if (a.has_date_time && b.has_date_time) {
            return date_time_candidates_conflict(a, b);
        }
        if (a.has_numeric && b.has_numeric) {
            return numeric_candidates_conflict(a, b);
        }
        if (a.value_key.empty() || b.value_key.empty()) {
            return false;
        }
        return a.value_key != b.value_key;
    }

    static void mark_role_conflicts(MetadataConceptResolution* resolution,
                                    MetadataConceptRole role)
    {
        if (!resolution) {
            return;
        }
        if (descriptive_role_is_collection(role)) {
            return;
        }
        if (role_uses_location_scope(role) || role_uses_language_scope(role)
            || role_uses_record_scope(role)) {
            for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
                MetadataConceptCandidate& a = resolution->candidates[i];
                if (a.role != role) {
                    continue;
                }
                for (size_t j = i + 1U; j < resolution->candidates.size();
                     ++j) {
                    MetadataConceptCandidate& b = resolution->candidates[j];
                    if (b.role != role
                        || !candidates_share_role_scope(role, a, b)) {
                        continue;
                    }
                    if (concept_values_conflict(a, b)) {
                        a.conflict           = true;
                        b.conflict           = true;
                        resolution->conflict = true;
                    }
                }
            }
            return;
        }
        const MetadataConceptRole group = conflict_group_role(role);
        bool conflict                   = false;
        for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
            const MetadataConceptCandidate& a = resolution->candidates[i];
            if (conflict_group_role(a.role) != group) {
                continue;
            }
            for (size_t j = i + 1U; j < resolution->candidates.size(); ++j) {
                const MetadataConceptCandidate& b = resolution->candidates[j];
                if (conflict_group_role(b.role) != group) {
                    continue;
                }
                if (concept_values_conflict(a, b)) {
                    conflict = true;
                    break;
                }
            }
            if (conflict) {
                break;
            }
        }
        if (!conflict) {
            return;
        }
        resolution->conflict = true;
        for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
            MetadataConceptCandidate& candidate = resolution->candidates[i];
            if (conflict_group_role(candidate.role) == group) {
                candidate.conflict = true;
            }
        }
    }

    static void mark_role_preferred(MetadataConceptResolution* resolution,
                                    MetadataConceptRole role)
    {
        if (!resolution) {
            return;
        }
        if (descriptive_role_is_collection(role)) {
            for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
                MetadataConceptCandidate& candidate = resolution->candidates[i];
                if (candidate.role != role) {
                    continue;
                }
                bool best = true;
                for (size_t j = 0U; j < resolution->candidates.size(); ++j) {
                    if (i == j) {
                        continue;
                    }
                    const MetadataConceptCandidate& other
                        = resolution->candidates[j];
                    if (other.role != role
                        || !candidates_share_role_scope(role, candidate, other)
                        || other.value_key != candidate.value_key) {
                        continue;
                    }
                    if (other.priority > candidate.priority
                        || (other.priority == candidate.priority && j < i)) {
                        best = false;
                        break;
                    }
                }
                if (best) {
                    candidate.preferred = true;
                }
            }
            return;
        }
        if (role_uses_location_scope(role) || role_uses_language_scope(role)
            || role_uses_record_scope(role)) {
            for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
                MetadataConceptCandidate& candidate = resolution->candidates[i];
                if (candidate.role != role) {
                    continue;
                }
                bool best = true;
                for (size_t j = 0U; j < resolution->candidates.size(); ++j) {
                    if (i == j) {
                        continue;
                    }
                    const MetadataConceptCandidate& other
                        = resolution->candidates[j];
                    if (other.role != role
                        || !candidates_share_role_scope(role, candidate,
                                                        other)) {
                        continue;
                    }
                    if (other.priority > candidate.priority
                        || (other.priority == candidate.priority && j < i)) {
                        best = false;
                        break;
                    }
                }
                if (best) {
                    candidate.preferred = true;
                }
            }
            return;
        }
        size_t best_index  = resolution->candidates.size();
        uint8_t best_score = 0U;
        for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate
                = resolution->candidates[i];
            if (candidate.role != role) {
                continue;
            }
            if (best_index == resolution->candidates.size()
                || candidate.priority > best_score) {
                best_index = i;
                best_score = candidate.priority;
            }
        }
        if (best_index < resolution->candidates.size()) {
            resolution->candidates[best_index].preferred = true;
        }
    }

    static void
    finalize_resolution(MetadataConceptResolution* resolution,
                        const MetadataRawDataDescriptor* raw_descriptor)
    {
        if (!resolution) {
            return;
        }
        resolution->found           = !resolution->candidates.empty();
        resolution->conflict        = false;
        resolution->preferred_entry = kInvalidEntryId;
        resolution->source_entries.clear();

        for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
            MetadataConceptCandidate& candidate = resolution->candidates[i];
            candidate.preferred                 = false;
            candidate.conflict                  = false;
            assign_transfer_hint(&candidate);
            assign_sensitivity(&candidate);
            assign_raw_applicability(&candidate, raw_descriptor);
            if (candidate.source_entries.empty()) {
                add_unique_entry(&resolution->source_entries,
                                 candidate.entry_id);
            } else {
                for (size_t e = 0U; e < candidate.source_entries.size(); ++e) {
                    add_unique_entry(&resolution->source_entries,
                                     candidate.source_entries[e]);
                }
            }
        }

        const MetadataConceptRole roles[] = {
            MetadataConceptRole::Primary,
            MetadataConceptRole::Orientation,
            MetadataConceptRole::Created,
            MetadataConceptRole::Digitized,
            MetadataConceptRole::Modified,
            MetadataConceptRole::MetadataDate,
            MetadataConceptRole::DateCreated,
            MetadataConceptRole::ColorSpace,
            MetadataConceptRole::IccProfile,
            MetadataConceptRole::ColorMatrix,
            MetadataConceptRole::WhiteBalance,
            MetadataConceptRole::Latitude,
            MetadataConceptRole::Longitude,
            MetadataConceptRole::Altitude,
            MetadataConceptRole::DestinationLatitude,
            MetadataConceptRole::DestinationLongitude,
            MetadataConceptRole::LocationShownLatitude,
            MetadataConceptRole::LocationShownLongitude,
            MetadataConceptRole::LocationShownAltitude,
            MetadataConceptRole::LocationCreatedLatitude,
            MetadataConceptRole::LocationCreatedLongitude,
            MetadataConceptRole::LocationCreatedAltitude,
            MetadataConceptRole::Title,
            MetadataConceptRole::Headline,
            MetadataConceptRole::Description,
            MetadataConceptRole::Creator,
            MetadataConceptRole::Keywords,
            MetadataConceptRole::LocationName,
            MetadataConceptRole::Sublocation,
            MetadataConceptRole::City,
            MetadataConceptRole::ProvinceState,
            MetadataConceptRole::CountryName,
            MetadataConceptRole::CountryCode,
            MetadataConceptRole::WorldRegion,
            MetadataConceptRole::LocationIdentifier,
            MetadataConceptRole::CopyrightNotice,
            MetadataConceptRole::CopyrightStatus,
            MetadataConceptRole::RightsUsageTerms,
            MetadataConceptRole::RightsWebStatement,
            MetadataConceptRole::RightsCertificate,
            MetadataConceptRole::RightsMarked,
            MetadataConceptRole::RightsHolderName,
            MetadataConceptRole::RightsHolderIdentifier,
            MetadataConceptRole::LicenseIdentifier,
            MetadataConceptRole::LicenseTermsUrl,
            MetadataConceptRole::LicensorName,
            MetadataConceptRole::LicensorIdentifier,
            MetadataConceptRole::CreditLine,
            MetadataConceptRole::CreditLineRequired,
            MetadataConceptRole::Source,
            MetadataConceptRole::DigitalSourceType,
            MetadataConceptRole::Name,
            MetadataConceptRole::Identifier,
            MetadataConceptRole::Address,
            MetadataConceptRole::PostalCode,
            MetadataConceptRole::Email,
            MetadataConceptRole::Telephone,
            MetadataConceptRole::Url,
            MetadataConceptRole::Characteristic,
            MetadataConceptRole::Gtin,
            MetadataConceptRole::InventoryNumber,
            MetadataConceptRole::StylePeriod,
            MetadataConceptRole::CreatorIdentifier,
            MetadataConceptRole::Age,
            MetadataConceptRole::ContentDescription,
            MetadataConceptRole::ContributionDescription,
            MetadataConceptRole::PhysicalDescription,
            MetadataConceptRole::RightsExpression,
            MetadataConceptRole::RightsExpressionEncoding,
            MetadataConceptRole::RightsExpressionLanguage,
            MetadataConceptRole::LicenseStartDate,
            MetadataConceptRole::LicenseEndDate,
            MetadataConceptRole::MediaConstraint,
            MetadataConceptRole::RegionConstraint,
            MetadataConceptRole::ProductOrServiceConstraint,
            MetadataConceptRole::ImageFileConstraint,
            MetadataConceptRole::ImageAlterationConstraint,
            MetadataConceptRole::OtherLicenseRequirement,
            MetadataConceptRole::OtherCondition,
            MetadataConceptRole::LicenseeTransactionIdentifier,
            MetadataConceptRole::LicensorTransactionIdentifier,
            MetadataConceptRole::LicenseeProjectReference,
            MetadataConceptRole::LicenseTransactionDate,
            MetadataConceptRole::ReleaseStatus,
            MetadataConceptRole::ReleaseIdentifier,
            MetadataConceptRole::Urgency,
            MetadataConceptRole::Category,
            MetadataConceptRole::SupplementalCategory,
            MetadataConceptRole::Instructions,
            MetadataConceptRole::CreatorTitle,
            MetadataConceptRole::TransmissionReference,
            MetadataConceptRole::CaptionWriter,
            MetadataConceptRole::AccessibilityAltText,
            MetadataConceptRole::AccessibilityExtendedDescription,
            MetadataConceptRole::IntellectualGenre,
            MetadataConceptRole::SceneCode,
            MetadataConceptRole::SubjectCode,
            MetadataConceptRole::ResourceIdentifier,
            MetadataConceptRole::DerivedFromIdentifier,
            MetadataConceptRole::DocumentIdentifier,
            MetadataConceptRole::InstanceIdentifier,
            MetadataConceptRole::OriginalDocumentIdentifier,
            MetadataConceptRole::RenditionClass,
            MetadataConceptRole::ImageIdentifier,
            MetadataConceptRole::Notes,
            MetadataConceptRole::MediaSummaryCode,
            MetadataConceptRole::ImageDuplicationConstraint,
            MetadataConceptRole::MinorModelAgeDisclosure,
            MetadataConceptRole::AdultContentWarning,
            MetadataConceptRole::DeliveredImageType,
            MetadataConceptRole::DeliveredFileName,
            MetadataConceptRole::DeliveredFileFormat,
            MetadataConceptRole::DeliveredFileSize,
            MetadataConceptRole::CopyrightRegistrationNumber,
            MetadataConceptRole::FirstPublicationDate,
            MetadataConceptRole::OtherImageInformation,
            MetadataConceptRole::Reuse,
            MetadataConceptRole::DataMining,
            MetadataConceptRole::OtherLicenseDocument,
            MetadataConceptRole::OtherLicenseInformation,
            MetadataConceptRole::VocabularyIdentifier,
            MetadataConceptRole::TermIdentifier,
            MetadataConceptRole::TermName,
            MetadataConceptRole::RefinedAbout,
            MetadataConceptRole::RegistryItemIdentifier,
            MetadataConceptRole::RegistryOrganizationIdentifier,
            MetadataConceptRole::RegistryEntryRole,
            MetadataConceptRole::RegionIdentifier,
            MetadataConceptRole::RegionName,
            MetadataConceptRole::RegionContentTypeIdentifier,
            MetadataConceptRole::RegionContentTypeName,
            MetadataConceptRole::RegionRoleIdentifier,
            MetadataConceptRole::RegionRoleName,
            MetadataConceptRole::VersionIdentifier,
            MetadataConceptRole::RenditionParameters,
            MetadataConceptRole::FilePath,
            MetadataConceptRole::FromPart,
            MetadataConceptRole::ToPart,
            MetadataConceptRole::Manager,
            MetadataConceptRole::ManagerVariant,
            MetadataConceptRole::ManageTo,
            MetadataConceptRole::ManageUi,
            MetadataConceptRole::AlternatePath,
            MetadataConceptRole::LastModifiedDate,
            MetadataConceptRole::MaskMarkers,
            MetadataConceptRole::PartMapping,
            MetadataConceptRole::LastUrl,
            MetadataConceptRole::LinkForm,
            MetadataConceptRole::LinkCategory,
            MetadataConceptRole::PlacedXResolution,
            MetadataConceptRole::PlacedYResolution,
            MetadataConceptRole::PlacedResolutionUnit,
            MetadataConceptRole::EventAction,
            MetadataConceptRole::EventParameters,
            MetadataConceptRole::SoftwareAgent,
            MetadataConceptRole::EventWhen,
            MetadataConceptRole::ChangedParts,
            MetadataConceptRole::Format,
            MetadataConceptRole::Timestamp,
            MetadataConceptRole::Crop,
            MetadataConceptRole::ActiveArea,
            MetadataConceptRole::Border,
            MetadataConceptRole::SensorGeometry,
            MetadataConceptRole::LensCorrection,
            MetadataConceptRole::BlackLevel,
            MetadataConceptRole::WhiteLevel,
            MetadataConceptRole::Linearization,
            MetadataConceptRole::RawValueCurve,
            MetadataConceptRole::RawLinearityLimit,
            MetadataConceptRole::RawCalibrationCurve,
            MetadataConceptRole::RawCurveControlPoints,
            MetadataConceptRole::CfaLayout,
            MetadataConceptRole::RawStorage,
            MetadataConceptRole::SourceProcessing,
            MetadataConceptRole::ComputationalProcessing,
            MetadataConceptRole::ThermalProcessing,
            MetadataConceptRole::StitchProcessing,
            MetadataConceptRole::ExposureTime,
            MetadataConceptRole::Aperture,
            MetadataConceptRole::IsoSensitivity,
            MetadataConceptRole::ExposureBias,
            MetadataConceptRole::ExposureProgram,
            MetadataConceptRole::Gain,
            MetadataConceptRole::RawExposureAdjustment,
            MetadataConceptRole::SourceColorTransform,
            MetadataConceptRole::ContentBoundMetadata,
            MetadataConceptRole::MultiImageScene,
            MetadataConceptRole::DerivedImageConstruction,
            MetadataConceptRole::TiledImageConfiguration,
        };
        for (size_t i = 0U; i < std::size(roles); ++i) {
            mark_role_preferred(resolution, roles[i]);
            mark_role_conflicts(resolution, roles[i]);
        }

        uint8_t best_score = 0U;
        for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate
                = resolution->candidates[i];
            if (!candidate.preferred) {
                continue;
            }
            if (resolution->preferred_entry == kInvalidEntryId
                || candidate.priority > best_score) {
                resolution->preferred_entry = candidate.entry_id;
                best_score                  = candidate.priority;
            }
        }
    }

}  // namespace

MetadataConceptResolution
resolve_metadata_concept(const MetaStore& store, MetadataConceptKind kind)
{
    MetadataConceptResolution out;
    out.kind = kind;
    switch (kind) {
    case MetadataConceptKind::Orientation:
        append_orientation_candidates(store, &out);
        break;
    case MetadataConceptKind::DateTime:
        append_datetime_candidates(store, &out);
        break;
    case MetadataConceptKind::ColorProfile:
        append_color_profile_candidates(store, &out);
        break;
    case MetadataConceptKind::Gps: append_gps_candidates(store, &out); break;
    case MetadataConceptKind::Geometry:
        append_geometry_candidates(store, &out);
        break;
    case MetadataConceptKind::LensCorrection:
        append_lens_correction_candidates(store, &out);
        break;
    case MetadataConceptKind::RawProcessing:
        append_raw_processing_candidates(store, &out);
        break;
    case MetadataConceptKind::Exposure:
        append_exposure_candidates(store, &out);
        break;
    case MetadataConceptKind::ContainerGraph:
        append_container_graph_candidates(store, &out);
        break;
    case MetadataConceptKind::Descriptive:
        append_descriptive_candidates(store, &out);
        break;
    }
    finalize_resolution(&out, nullptr);
    return out;
}

MetadataConceptResult
resolve_metadata_concepts(const MetaStore& store)
{
    MetadataConceptResult out;
    out.concepts.reserve(10U);
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Orientation));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::DateTime));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Exposure));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::ColorProfile));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Gps));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Geometry));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::LensCorrection));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::RawProcessing));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::ContainerGraph));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Descriptive));
    return out;
}

MetadataConceptResolution
resolve_metadata_concept(const MetaStore& store, MetadataConceptKind kind,
                         const MetadataRawDataDescriptor& raw_descriptor)
{
    MetadataConceptResolution out;
    out.kind = kind;
    switch (kind) {
    case MetadataConceptKind::Orientation:
        append_orientation_candidates(store, &out);
        break;
    case MetadataConceptKind::DateTime:
        append_datetime_candidates(store, &out);
        break;
    case MetadataConceptKind::ColorProfile:
        append_color_profile_candidates(store, &out);
        break;
    case MetadataConceptKind::Gps: append_gps_candidates(store, &out); break;
    case MetadataConceptKind::Geometry:
        append_geometry_candidates(store, &out);
        break;
    case MetadataConceptKind::LensCorrection:
        append_lens_correction_candidates(store, &out);
        break;
    case MetadataConceptKind::RawProcessing:
        append_raw_processing_candidates(store, &out);
        break;
    case MetadataConceptKind::Exposure:
        append_exposure_candidates(store, &out);
        break;
    case MetadataConceptKind::ContainerGraph:
        append_container_graph_candidates(store, &out);
        break;
    case MetadataConceptKind::Descriptive:
        append_descriptive_candidates(store, &out);
        break;
    }
    finalize_resolution(&out, &raw_descriptor);
    return out;
}

MetadataConceptResult
resolve_metadata_concepts(const MetaStore& store,
                          const MetadataRawDataDescriptor& raw_descriptor)
{
    MetadataConceptResult out;
    out.concepts.reserve(10U);
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Orientation,
                                 raw_descriptor));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::DateTime,
                                 raw_descriptor));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Exposure,
                                 raw_descriptor));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::ColorProfile,
                                 raw_descriptor));
    out.concepts.push_back(resolve_metadata_concept(store,
                                                    MetadataConceptKind::Gps,
                                                    raw_descriptor));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Geometry,
                                 raw_descriptor));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::LensCorrection,
                                 raw_descriptor));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::RawProcessing,
                                 raw_descriptor));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::ContainerGraph,
                                 raw_descriptor));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Descriptive,
                                 raw_descriptor));
    return out;
}

MetadataRawApplicabilityState
metadata_raw_applicability_for_descriptor(
    MetadataConceptRole role,
    const MetadataRawDataDescriptor& descriptor) noexcept
{
    switch (role) {
    case MetadataConceptRole::BlackLevel:
    case MetadataConceptRole::WhiteLevel:
    case MetadataConceptRole::CfaLayout:
    case MetadataConceptRole::SensorGeometry:
    case MetadataConceptRole::RawStorage:
        if (descriptor.encoding == MetadataRawDataEncoding::Rendered) {
            return MetadataRawApplicabilityState::NotApplicableToStoredRaw;
        }
        return MetadataRawApplicabilityState::AppliesToStoredRaw;
    case MetadataConceptRole::Linearization:
    case MetadataConceptRole::RawValueCurve:
    case MetadataConceptRole::RawLinearityLimit:
    case MetadataConceptRole::RawCalibrationCurve:
    case MetadataConceptRole::RawCurveControlPoints:
        if (descriptor.encoding == MetadataRawDataEncoding::Rendered) {
            return MetadataRawApplicabilityState::NotApplicableToStoredRaw;
        }
        if (descriptor.requires_primary_raw_plane) {
            if (!descriptor.has_plane_index) {
                return MetadataRawApplicabilityState::ConditionalOnRawEncoding;
            }
            if (descriptor.plane_index != 0U) {
                return MetadataRawApplicabilityState::NotApplicableToStoredRaw;
            }
        }
        if (descriptor.requires_compressed_raw_encoding) {
            if (descriptor.encoding == MetadataRawDataEncoding::Unknown) {
                return MetadataRawApplicabilityState::ConditionalOnRawEncoding;
            }
            if (raw_data_encoding_is_compressed(descriptor.encoding)) {
                return MetadataRawApplicabilityState::AppliesToStoredRaw;
            }
            return MetadataRawApplicabilityState::NotApplicableToStoredRaw;
        }
        if (descriptor.encoding == MetadataRawDataEncoding::Unknown) {
            return MetadataRawApplicabilityState::ConditionalOnRawEncoding;
        }
        return MetadataRawApplicabilityState::AppliesToStoredRaw;
    case MetadataConceptRole::Primary:
    case MetadataConceptRole::Orientation:
    case MetadataConceptRole::Created:
    case MetadataConceptRole::Digitized:
    case MetadataConceptRole::Modified:
    case MetadataConceptRole::MetadataDate:
    case MetadataConceptRole::DateCreated:
    case MetadataConceptRole::ColorSpace:
    case MetadataConceptRole::IccProfile:
    case MetadataConceptRole::ColorMatrix:
    case MetadataConceptRole::WhiteBalance:
    case MetadataConceptRole::Latitude:
    case MetadataConceptRole::Longitude:
    case MetadataConceptRole::Altitude:
    case MetadataConceptRole::DestinationLatitude:
    case MetadataConceptRole::DestinationLongitude:
    case MetadataConceptRole::LocationShownLatitude:
    case MetadataConceptRole::LocationShownLongitude:
    case MetadataConceptRole::LocationShownAltitude:
    case MetadataConceptRole::LocationCreatedLatitude:
    case MetadataConceptRole::LocationCreatedLongitude:
    case MetadataConceptRole::LocationCreatedAltitude:
    case MetadataConceptRole::Timestamp:
    case MetadataConceptRole::Crop:
    case MetadataConceptRole::ActiveArea:
    case MetadataConceptRole::Border:
    case MetadataConceptRole::LensCorrection:
    case MetadataConceptRole::SourceProcessing:
    case MetadataConceptRole::ComputationalProcessing:
    case MetadataConceptRole::ThermalProcessing:
    case MetadataConceptRole::StitchProcessing:
    case MetadataConceptRole::ExposureTime:
    case MetadataConceptRole::Aperture:
    case MetadataConceptRole::IsoSensitivity:
    case MetadataConceptRole::ExposureBias:
    case MetadataConceptRole::ExposureProgram:
    case MetadataConceptRole::Gain:
    case MetadataConceptRole::RawExposureAdjustment:
    case MetadataConceptRole::SourceColorTransform:
    case MetadataConceptRole::ContentBoundMetadata:
    case MetadataConceptRole::MultiImageScene:
    case MetadataConceptRole::DerivedImageConstruction:
    case MetadataConceptRole::TiledImageConfiguration:
    case MetadataConceptRole::Title:
    case MetadataConceptRole::Headline:
    case MetadataConceptRole::Description:
    case MetadataConceptRole::Creator:
    case MetadataConceptRole::Keywords:
    case MetadataConceptRole::LocationName:
    case MetadataConceptRole::Sublocation:
    case MetadataConceptRole::City:
    case MetadataConceptRole::ProvinceState:
    case MetadataConceptRole::CountryName:
    case MetadataConceptRole::CountryCode:
    case MetadataConceptRole::WorldRegion:
    case MetadataConceptRole::LocationIdentifier:
    case MetadataConceptRole::CopyrightNotice:
    case MetadataConceptRole::CopyrightStatus:
    case MetadataConceptRole::RightsUsageTerms:
    case MetadataConceptRole::RightsWebStatement:
    case MetadataConceptRole::RightsCertificate:
    case MetadataConceptRole::RightsMarked:
    case MetadataConceptRole::RightsHolderName:
    case MetadataConceptRole::RightsHolderIdentifier:
    case MetadataConceptRole::LicenseIdentifier:
    case MetadataConceptRole::LicenseTermsUrl:
    case MetadataConceptRole::LicensorName:
    case MetadataConceptRole::LicensorIdentifier:
    case MetadataConceptRole::CreditLine:
    case MetadataConceptRole::CreditLineRequired:
    case MetadataConceptRole::Source:
    case MetadataConceptRole::DigitalSourceType: break;
    default: break;
    }
    return MetadataRawApplicabilityState::Unknown;
}

const char*
metadata_concept_kind_name(MetadataConceptKind kind) noexcept
{
    switch (kind) {
    case MetadataConceptKind::Orientation: return "orientation";
    case MetadataConceptKind::DateTime: return "date_time";
    case MetadataConceptKind::ColorProfile: return "color_profile";
    case MetadataConceptKind::Gps: return "gps";
    case MetadataConceptKind::Geometry: return "geometry";
    case MetadataConceptKind::LensCorrection: return "lens_correction";
    case MetadataConceptKind::RawProcessing: return "raw_processing";
    case MetadataConceptKind::Exposure: return "exposure";
    case MetadataConceptKind::ContainerGraph: return "container_graph";
    case MetadataConceptKind::Descriptive: return "descriptive";
    }
    return "unknown";
}

const char*
metadata_concept_source_family_name(MetadataConceptSourceFamily family) noexcept
{
    switch (family) {
    case MetadataConceptSourceFamily::Unknown: return "unknown";
    case MetadataConceptSourceFamily::Exif: return "exif";
    case MetadataConceptSourceFamily::Xmp: return "xmp";
    case MetadataConceptSourceFamily::Iptc: return "iptc";
    case MetadataConceptSourceFamily::Icc: return "icc";
    case MetadataConceptSourceFamily::PngText: return "png_text";
    case MetadataConceptSourceFamily::InterpretationRecord:
        return "interpretation_record";
    }
    return "unknown";
}

const char*
metadata_concept_role_name(MetadataConceptRole role) noexcept
{
    switch (role) {
    case MetadataConceptRole::Primary: return "primary";
    case MetadataConceptRole::Orientation: return "orientation";
    case MetadataConceptRole::Created: return "created";
    case MetadataConceptRole::Digitized: return "digitized";
    case MetadataConceptRole::Modified: return "modified";
    case MetadataConceptRole::MetadataDate: return "metadata_date";
    case MetadataConceptRole::DateCreated: return "date_created";
    case MetadataConceptRole::ColorSpace: return "color_space";
    case MetadataConceptRole::IccProfile: return "icc_profile";
    case MetadataConceptRole::ColorMatrix: return "color_matrix";
    case MetadataConceptRole::WhiteBalance: return "white_balance";
    case MetadataConceptRole::Latitude: return "latitude";
    case MetadataConceptRole::Longitude: return "longitude";
    case MetadataConceptRole::Altitude: return "altitude";
    case MetadataConceptRole::DestinationLatitude:
        return "destination_latitude";
    case MetadataConceptRole::DestinationLongitude:
        return "destination_longitude";
    case MetadataConceptRole::LocationShownLatitude:
        return "location_shown_latitude";
    case MetadataConceptRole::LocationShownLongitude:
        return "location_shown_longitude";
    case MetadataConceptRole::LocationShownAltitude:
        return "location_shown_altitude";
    case MetadataConceptRole::LocationCreatedLatitude:
        return "location_created_latitude";
    case MetadataConceptRole::LocationCreatedLongitude:
        return "location_created_longitude";
    case MetadataConceptRole::LocationCreatedAltitude:
        return "location_created_altitude";
    case MetadataConceptRole::Timestamp: return "timestamp";
    case MetadataConceptRole::Crop: return "crop";
    case MetadataConceptRole::ActiveArea: return "active_area";
    case MetadataConceptRole::Border: return "border";
    case MetadataConceptRole::SensorGeometry: return "sensor_geometry";
    case MetadataConceptRole::LensCorrection: return "lens_correction";
    case MetadataConceptRole::BlackLevel: return "black_level";
    case MetadataConceptRole::WhiteLevel: return "white_level";
    case MetadataConceptRole::Linearization: return "linearization";
    case MetadataConceptRole::RawValueCurve: return "raw_value_curve";
    case MetadataConceptRole::RawLinearityLimit: return "raw_linearity_limit";
    case MetadataConceptRole::RawCalibrationCurve:
        return "raw_calibration_curve";
    case MetadataConceptRole::RawCurveControlPoints:
        return "raw_curve_control_points";
    case MetadataConceptRole::CfaLayout: return "cfa_layout";
    case MetadataConceptRole::RawStorage: return "raw_storage";
    case MetadataConceptRole::SourceProcessing: return "source_processing";
    case MetadataConceptRole::ComputationalProcessing:
        return "computational_processing";
    case MetadataConceptRole::ThermalProcessing: return "thermal_processing";
    case MetadataConceptRole::StitchProcessing: return "stitch_processing";
    case MetadataConceptRole::ExposureTime: return "exposure_time";
    case MetadataConceptRole::Aperture: return "aperture";
    case MetadataConceptRole::IsoSensitivity: return "iso_sensitivity";
    case MetadataConceptRole::ExposureBias: return "exposure_bias";
    case MetadataConceptRole::ExposureProgram: return "exposure_program";
    case MetadataConceptRole::Gain: return "gain";
    case MetadataConceptRole::RawExposureAdjustment:
        return "raw_exposure_adjustment";
    case MetadataConceptRole::SourceColorTransform:
        return "source_color_transform";
    case MetadataConceptRole::ContentBoundMetadata:
        return "content_bound_metadata";
    case MetadataConceptRole::MultiImageScene: return "multi_image_scene";
    case MetadataConceptRole::DerivedImageConstruction:
        return "derived_image_construction";
    case MetadataConceptRole::TiledImageConfiguration:
        return "tiled_image_configuration";
    case MetadataConceptRole::Title: return "title";
    case MetadataConceptRole::Headline: return "headline";
    case MetadataConceptRole::Description: return "description";
    case MetadataConceptRole::Creator: return "creator";
    case MetadataConceptRole::Keywords: return "keywords";
    case MetadataConceptRole::LocationName: return "location_name";
    case MetadataConceptRole::Sublocation: return "sublocation";
    case MetadataConceptRole::City: return "city";
    case MetadataConceptRole::ProvinceState: return "province_state";
    case MetadataConceptRole::CountryName: return "country_name";
    case MetadataConceptRole::CountryCode: return "country_code";
    case MetadataConceptRole::WorldRegion: return "world_region";
    case MetadataConceptRole::LocationIdentifier: return "location_identifier";
    case MetadataConceptRole::CopyrightNotice: return "copyright_notice";
    case MetadataConceptRole::CopyrightStatus: return "copyright_status";
    case MetadataConceptRole::RightsUsageTerms: return "rights_usage_terms";
    case MetadataConceptRole::RightsWebStatement: return "rights_web_statement";
    case MetadataConceptRole::RightsCertificate: return "rights_certificate";
    case MetadataConceptRole::RightsMarked: return "rights_marked";
    case MetadataConceptRole::RightsHolderName: return "rights_holder_name";
    case MetadataConceptRole::RightsHolderIdentifier:
        return "rights_holder_identifier";
    case MetadataConceptRole::LicenseIdentifier: return "license_identifier";
    case MetadataConceptRole::LicenseTermsUrl: return "license_terms_url";
    case MetadataConceptRole::LicensorName: return "licensor_name";
    case MetadataConceptRole::LicensorIdentifier: return "licensor_identifier";
    case MetadataConceptRole::CreditLine: return "credit_line";
    case MetadataConceptRole::CreditLineRequired: return "credit_line_required";
    case MetadataConceptRole::Source: return "source";
    case MetadataConceptRole::DigitalSourceType: return "digital_source_type";
    case MetadataConceptRole::Name: return "name";
    case MetadataConceptRole::Identifier: return "identifier";
    case MetadataConceptRole::Address: return "address";
    case MetadataConceptRole::PostalCode: return "postal_code";
    case MetadataConceptRole::Email: return "email";
    case MetadataConceptRole::Telephone: return "telephone";
    case MetadataConceptRole::Url: return "url";
    case MetadataConceptRole::Characteristic: return "characteristic";
    case MetadataConceptRole::Gtin: return "gtin";
    case MetadataConceptRole::InventoryNumber: return "inventory_number";
    case MetadataConceptRole::StylePeriod: return "style_period";
    case MetadataConceptRole::CreatorIdentifier: return "creator_identifier";
    case MetadataConceptRole::Age: return "age";
    case MetadataConceptRole::ContentDescription: return "content_description";
    case MetadataConceptRole::ContributionDescription:
        return "contribution_description";
    case MetadataConceptRole::PhysicalDescription:
        return "physical_description";
    case MetadataConceptRole::RightsExpression: return "rights_expression";
    case MetadataConceptRole::RightsExpressionEncoding:
        return "rights_expression_encoding";
    case MetadataConceptRole::RightsExpressionLanguage:
        return "rights_expression_language";
    case MetadataConceptRole::LicenseStartDate: return "license_start_date";
    case MetadataConceptRole::LicenseEndDate: return "license_end_date";
    case MetadataConceptRole::MediaConstraint: return "media_constraint";
    case MetadataConceptRole::RegionConstraint: return "region_constraint";
    case MetadataConceptRole::ProductOrServiceConstraint:
        return "product_or_service_constraint";
    case MetadataConceptRole::ImageFileConstraint:
        return "image_file_constraint";
    case MetadataConceptRole::ImageAlterationConstraint:
        return "image_alteration_constraint";
    case MetadataConceptRole::OtherLicenseRequirement:
        return "other_license_requirement";
    case MetadataConceptRole::OtherCondition: return "other_condition";
    case MetadataConceptRole::LicenseeTransactionIdentifier:
        return "licensee_transaction_identifier";
    case MetadataConceptRole::LicensorTransactionIdentifier:
        return "licensor_transaction_identifier";
    case MetadataConceptRole::LicenseeProjectReference:
        return "licensee_project_reference";
    case MetadataConceptRole::LicenseTransactionDate:
        return "license_transaction_date";
    case MetadataConceptRole::ReleaseStatus: return "release_status";
    case MetadataConceptRole::ReleaseIdentifier: return "release_identifier";
    case MetadataConceptRole::Urgency: return "urgency";
    case MetadataConceptRole::Category: return "category";
    case MetadataConceptRole::SupplementalCategory:
        return "supplemental_category";
    case MetadataConceptRole::Instructions: return "instructions";
    case MetadataConceptRole::CreatorTitle: return "creator_title";
    case MetadataConceptRole::TransmissionReference:
        return "transmission_reference";
    case MetadataConceptRole::CaptionWriter: return "caption_writer";
    case MetadataConceptRole::AccessibilityAltText:
        return "accessibility_alt_text";
    case MetadataConceptRole::AccessibilityExtendedDescription:
        return "accessibility_extended_description";
    case MetadataConceptRole::IntellectualGenre: return "intellectual_genre";
    case MetadataConceptRole::SceneCode: return "scene_code";
    case MetadataConceptRole::SubjectCode: return "subject_code";
    case MetadataConceptRole::ResourceIdentifier: return "resource_identifier";
    case MetadataConceptRole::DerivedFromIdentifier:
        return "derived_from_identifier";
    case MetadataConceptRole::DocumentIdentifier: return "document_identifier";
    case MetadataConceptRole::InstanceIdentifier: return "instance_identifier";
    case MetadataConceptRole::OriginalDocumentIdentifier:
        return "original_document_identifier";
    case MetadataConceptRole::RenditionClass: return "rendition_class";
    case MetadataConceptRole::ImageIdentifier: return "image_identifier";
    case MetadataConceptRole::Notes: return "notes";
    case MetadataConceptRole::MediaSummaryCode: return "media_summary_code";
    case MetadataConceptRole::ImageDuplicationConstraint:
        return "image_duplication_constraint";
    case MetadataConceptRole::MinorModelAgeDisclosure:
        return "minor_model_age_disclosure";
    case MetadataConceptRole::AdultContentWarning:
        return "adult_content_warning";
    case MetadataConceptRole::DeliveredImageType: return "delivered_image_type";
    case MetadataConceptRole::DeliveredFileName: return "delivered_file_name";
    case MetadataConceptRole::DeliveredFileFormat:
        return "delivered_file_format";
    case MetadataConceptRole::DeliveredFileSize: return "delivered_file_size";
    case MetadataConceptRole::CopyrightRegistrationNumber:
        return "copyright_registration_number";
    case MetadataConceptRole::FirstPublicationDate:
        return "first_publication_date";
    case MetadataConceptRole::OtherImageInformation:
        return "other_image_information";
    case MetadataConceptRole::Reuse: return "reuse";
    case MetadataConceptRole::DataMining: return "data_mining";
    case MetadataConceptRole::OtherLicenseDocument:
        return "other_license_document";
    case MetadataConceptRole::OtherLicenseInformation:
        return "other_license_information";
    case MetadataConceptRole::VocabularyIdentifier:
        return "vocabulary_identifier";
    case MetadataConceptRole::TermIdentifier: return "term_identifier";
    case MetadataConceptRole::TermName: return "term_name";
    case MetadataConceptRole::RefinedAbout: return "refined_about";
    case MetadataConceptRole::RegistryItemIdentifier:
        return "registry_item_identifier";
    case MetadataConceptRole::RegistryOrganizationIdentifier:
        return "registry_organization_identifier";
    case MetadataConceptRole::RegistryEntryRole: return "registry_entry_role";
    case MetadataConceptRole::RegionIdentifier: return "region_identifier";
    case MetadataConceptRole::RegionName: return "region_name";
    case MetadataConceptRole::RegionContentTypeIdentifier:
        return "region_content_type_identifier";
    case MetadataConceptRole::RegionContentTypeName:
        return "region_content_type_name";
    case MetadataConceptRole::RegionRoleIdentifier:
        return "region_role_identifier";
    case MetadataConceptRole::RegionRoleName: return "region_role_name";
    case MetadataConceptRole::VersionIdentifier: return "version_identifier";
    case MetadataConceptRole::RenditionParameters:
        return "rendition_parameters";
    case MetadataConceptRole::FilePath: return "file_path";
    case MetadataConceptRole::FromPart: return "from_part";
    case MetadataConceptRole::ToPart: return "to_part";
    case MetadataConceptRole::Manager: return "manager";
    case MetadataConceptRole::ManagerVariant: return "manager_variant";
    case MetadataConceptRole::ManageTo: return "manage_to";
    case MetadataConceptRole::ManageUi: return "manage_ui";
    case MetadataConceptRole::AlternatePath: return "alternate_path";
    case MetadataConceptRole::LastModifiedDate: return "last_modified_date";
    case MetadataConceptRole::MaskMarkers: return "mask_markers";
    case MetadataConceptRole::PartMapping: return "part_mapping";
    case MetadataConceptRole::LastUrl: return "last_url";
    case MetadataConceptRole::LinkForm: return "link_form";
    case MetadataConceptRole::LinkCategory: return "link_category";
    case MetadataConceptRole::PlacedXResolution: return "placed_x_resolution";
    case MetadataConceptRole::PlacedYResolution: return "placed_y_resolution";
    case MetadataConceptRole::PlacedResolutionUnit:
        return "placed_resolution_unit";
    case MetadataConceptRole::EventAction: return "event_action";
    case MetadataConceptRole::EventParameters: return "event_parameters";
    case MetadataConceptRole::SoftwareAgent: return "software_agent";
    case MetadataConceptRole::EventWhen: return "event_when";
    case MetadataConceptRole::ChangedParts: return "changed_parts";
    case MetadataConceptRole::Format: return "format";
    }
    return "unknown";
}

const char*
metadata_concept_record_kind_name(MetadataConceptRecordKind kind) noexcept
{
    switch (kind) {
    case MetadataConceptRecordKind::None: return "none";
    case MetadataConceptRecordKind::CreatorContact: return "creator_contact";
    case MetadataConceptRecordKind::Event: return "event";
    case MetadataConceptRecordKind::Person: return "person";
    case MetadataConceptRecordKind::Organization: return "organization";
    case MetadataConceptRecordKind::Product: return "product";
    case MetadataConceptRecordKind::ArtworkOrObject: return "artwork_or_object";
    case MetadataConceptRecordKind::RightsExpression:
        return "rights_expression";
    case MetadataConceptRecordKind::RightsHolder: return "rights_holder";
    case MetadataConceptRecordKind::Licensor: return "licensor";
    case MetadataConceptRecordKind::Licensee: return "licensee";
    case MetadataConceptRecordKind::License: return "license";
    case MetadataConceptRecordKind::Release: return "release";
    case MetadataConceptRecordKind::EndUser: return "end_user";
    case MetadataConceptRecordKind::ImageCreator: return "image_creator";
    case MetadataConceptRecordKind::ImageSupplier: return "image_supplier";
    case MetadataConceptRecordKind::ImageAsset: return "image_asset";
    case MetadataConceptRecordKind::ControlledVocabularyTerm:
        return "controlled_vocabulary_term";
    case MetadataConceptRecordKind::RegistryEntry: return "registry_entry";
    case MetadataConceptRecordKind::ImageRegion: return "image_region";
    case MetadataConceptRecordKind::ResourceReference:
        return "resource_reference";
    case MetadataConceptRecordKind::ResourceEvent: return "resource_event";
    case MetadataConceptRecordKind::PantryItem: return "pantry_item";
    }
    return "none";
}

const char*
metadata_concept_sensitivity_name(MetadataConceptSensitivity sensitivity) noexcept
{
    switch (sensitivity) {
    case MetadataConceptSensitivity::None: return "none";
    case MetadataConceptSensitivity::PersonalContact: return "personal_contact";
    case MetadataConceptSensitivity::PersonIdentity: return "person_identity";
    case MetadataConceptSensitivity::Location: return "location";
    case MetadataConceptSensitivity::LegalRights: return "legal_rights";
    }
    return "none";
}

const char*
metadata_concept_datetime_precision_name(
    MetadataConceptDateTimePrecision precision) noexcept
{
    switch (precision) {
    case MetadataConceptDateTimePrecision::Unknown: return "unknown";
    case MetadataConceptDateTimePrecision::Date: return "date";
    case MetadataConceptDateTimePrecision::DateTime: return "date_time";
    case MetadataConceptDateTimePrecision::DateTimeSubsecond:
        return "date_time_subsecond";
    }
    return "unknown";
}

const char*
metadata_concept_timezone_kind_name(MetadataConceptTimeZoneKind kind) noexcept
{
    switch (kind) {
    case MetadataConceptTimeZoneKind::Unknown: return "unknown";
    case MetadataConceptTimeZoneKind::Local: return "local";
    case MetadataConceptTimeZoneKind::Utc: return "utc";
    case MetadataConceptTimeZoneKind::Offset: return "offset";
    }
    return "unknown";
}

const char*
metadata_concept_transfer_hint_name(MetadataConceptTransferHint hint) noexcept
{
    switch (hint) {
    case MetadataConceptTransferHint::Unknown: return "unknown";
    case MetadataConceptTransferHint::Safe: return "safe";
    case MetadataConceptTransferHint::SourceBound: return "source_bound";
    case MetadataConceptTransferHint::RenderedUnsafe: return "rendered_unsafe";
    case MetadataConceptTransferHint::RequiresTargetImageSpec:
        return "requires_target_image_spec";
    }
    return "unknown";
}

const char*
metadata_raw_data_encoding_name(MetadataRawDataEncoding encoding) noexcept
{
    switch (encoding) {
    case MetadataRawDataEncoding::Unknown: return "unknown";
    case MetadataRawDataEncoding::Uncompressed: return "uncompressed";
    case MetadataRawDataEncoding::Packed: return "packed";
    case MetadataRawDataEncoding::LosslessCompressed:
        return "lossless_compressed";
    case MetadataRawDataEncoding::LossyCompressed: return "lossy_compressed";
    case MetadataRawDataEncoding::Rendered: return "rendered";
    }
    return "unknown";
}

const char*
metadata_raw_applicability_state_name(
    MetadataRawApplicabilityState state) noexcept
{
    switch (state) {
    case MetadataRawApplicabilityState::Unknown: return "unknown";
    case MetadataRawApplicabilityState::AppliesToStoredRaw:
        return "applies_to_stored_raw";
    case MetadataRawApplicabilityState::ConditionalOnRawEncoding:
        return "conditional_on_raw_encoding";
    case MetadataRawApplicabilityState::NotApplicableToStoredRaw:
        return "not_applicable_to_stored_raw";
    }
    return "unknown";
}

const char*
metadata_concept_gps_altitude_reference_name(uint8_t code) noexcept
{
    switch (code) {
    case 0U: return "above_sea_level";
    case 1U: return "below_sea_level";
    default: break;
    }
    return "unknown";
}

}  // namespace openmeta
