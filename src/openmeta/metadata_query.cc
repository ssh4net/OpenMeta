// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_query.h"

#include "openmeta/byte_arena.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_flags.h"
#include "openmeta/phaseone_geometry.h"
#include "openmeta/vendor_raw_processing.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
#    include <rapidfuzz/fuzz.hpp>
#endif

namespace openmeta {
namespace {

    static constexpr uint16_t kDngDefaultCropOriginTag        = 0xC61FU;
    static constexpr uint16_t kDngDefaultCropSizeTag          = 0xC620U;
    static constexpr uint16_t kDngActiveAreaTag               = 0xC68DU;
    static constexpr uint16_t kDngMaskedAreasTag              = 0xC68EU;
    static constexpr uint16_t kExifDocumentNameTag            = 0x010DU;
    static constexpr uint16_t kExifImageDescriptionTag        = 0x010EU;
    static constexpr uint16_t kExifOrientationTag             = 0x0112U;
    static constexpr uint16_t kExifArtistTag                  = 0x013BU;
    static constexpr uint16_t kExifCopyrightTag               = 0x8298U;
    static constexpr uint16_t kExifThumbnailOrientationTag    = 0x5029U;
    static constexpr uint16_t kExifExposureTimeTag            = 0x829AU;
    static constexpr uint16_t kExifFNumberTag                 = 0x829DU;
    static constexpr uint16_t kExifExposureProgramTag         = 0x8822U;
    static constexpr uint16_t kExifPhotographicSensitivityTag = 0x8827U;
    static constexpr uint16_t kExifShutterSpeedValueTag       = 0x9201U;
    static constexpr uint16_t kExifApertureValueTag           = 0x9202U;
    static constexpr uint16_t kExifBrightnessValueTag         = 0x9203U;
    static constexpr uint16_t kExifExposureBiasValueTag       = 0x9204U;
    static constexpr uint16_t kExifMaxApertureValueTag        = 0x9205U;
    static constexpr uint16_t kExifLightSourceTag             = 0x9208U;
    static constexpr uint16_t kExifExposureIndexTag           = 0x9215U;
    static constexpr uint16_t kExifColorSpaceTag              = 0xA001U;
    static constexpr uint16_t kExifWhiteBalanceTag            = 0xA403U;
    static constexpr uint16_t kExifGainControlTag             = 0xA407U;
    static constexpr uint16_t kExifCfaRepeatPatternDimTag     = 0x828DU;
    static constexpr uint16_t kExifCfaPatternTag              = 0x828EU;
    static constexpr uint16_t kExifCfaPattern2Tag             = 0xA302U;
    static constexpr uint16_t kExifXpTitleTag                 = 0x9C9BU;
    static constexpr uint16_t kExifXpCommentTag               = 0x9C9CU;
    static constexpr uint16_t kExifXpAuthorTag                = 0x9C9DU;
    static constexpr uint16_t kExifXpKeywordsTag              = 0x9C9EU;
    static constexpr std::string_view kDcXmpSchema
        = "http://purl.org/dc/elements/1.1/";
    static constexpr std::string_view kPhotoshopXmpSchema
        = "http://ns.adobe.com/photoshop/1.0/";
    static constexpr std::string_view kIptcCoreXmpSchema
        = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
    static constexpr std::string_view kIptcExtXmpSchema
        = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
    static constexpr std::string_view kXmpRightsSchema
        = "http://ns.adobe.com/xap/1.0/rights/";
    static constexpr std::string_view kPlusXmpSchema
        = "http://ns.useplus.org/ldf/xmp/1.0/";
    static constexpr uint16_t kPanasonicLinearityLimitRedTag   = 0x000EU;
    static constexpr uint16_t kPanasonicLinearityLimitGreenTag = 0x000FU;
    static constexpr uint16_t kPanasonicLinearityLimitBlueTag  = 0x0010U;
    static constexpr uint16_t kSonyToneCurveTag                = 0x7010U;
    static constexpr uint16_t kDngCfaPlaneColorTag             = 0xC616U;
    static constexpr uint16_t kDngCfaLayoutTag                 = 0xC617U;
    static constexpr uint16_t kDngLinearizationTableTag        = 0xC618U;
    static constexpr uint16_t kDngBlackLevelRepeatDimTag       = 0xC619U;
    static constexpr uint16_t kDngBlackLevelTag                = 0xC61AU;
    static constexpr uint16_t kDngBlackLevelDeltaHTag          = 0xC61BU;
    static constexpr uint16_t kDngBlackLevelDeltaVTag          = 0xC61CU;
    static constexpr uint16_t kDngWhiteLevelTag                = 0xC61DU;
    static constexpr uint16_t kDngDefaultScaleTag              = 0xC61EU;
    static constexpr uint16_t kDngColorMatrix1Tag              = 0xC621U;
    static constexpr uint16_t kDngColorMatrix2Tag              = 0xC622U;
    static constexpr uint16_t kDngCameraCalibration1Tag        = 0xC623U;
    static constexpr uint16_t kDngCameraCalibration2Tag        = 0xC624U;
    static constexpr uint16_t kDngReductionMatrix1Tag          = 0xC625U;
    static constexpr uint16_t kDngReductionMatrix2Tag          = 0xC626U;
    static constexpr uint16_t kDngAnalogBalanceTag             = 0xC627U;
    static constexpr uint16_t kDngAsShotNeutralTag             = 0xC628U;
    static constexpr uint16_t kDngAsShotWhiteXyTag             = 0xC629U;
    static constexpr uint16_t kDngBaselineExposureTag          = 0xC62AU;
    static constexpr uint16_t kDngLinearResponseLimitTag       = 0xC62EU;
    static constexpr uint16_t kDngCalibrationIlluminant1Tag    = 0xC65AU;
    static constexpr uint16_t kDngCalibrationIlluminant2Tag    = 0xC65BU;
    static constexpr uint16_t kDngForwardMatrix1Tag            = 0xC714U;
    static constexpr uint16_t kDngForwardMatrix2Tag            = 0xC715U;
    static constexpr uint16_t kDngOpcodeList1Tag               = 0xC740U;
    static constexpr uint16_t kDngOpcodeList2Tag               = 0xC741U;
    static constexpr uint16_t kDngOpcodeList3Tag               = 0xC74EU;
    static constexpr uint16_t kDngBaselineExposureOffsetTag    = 0xC7A5U;
    static constexpr uint16_t kDngRawToPreviewGainTag          = 0xC7A8U;
    static constexpr uint16_t kDngProfileGainTableMapTag       = 0xCD2DU;
    static constexpr uint16_t kDngCalibrationIlluminant3Tag    = 0xCD31U;
    static constexpr uint16_t kDngCameraCalibration3Tag        = 0xCD32U;
    static constexpr uint16_t kDngColorMatrix3Tag              = 0xCD33U;
    static constexpr uint16_t kDngForwardMatrix3Tag            = 0xCD34U;
    static constexpr uint16_t kDngReductionMatrix3Tag          = 0xCD3AU;
    static constexpr uint16_t kDngProfileGainTableMap2Tag      = 0xCD40U;
    static constexpr uint16_t kDngRawDataUniqueIdTag           = 0xC65DU;
    static constexpr uint16_t kDngOriginalRawFileNameTag       = 0xC68BU;
    static constexpr uint16_t kDngOriginalRawFileDataTag       = 0xC68CU;
    static constexpr uint16_t kDngRawImageDigestTag            = 0xC71CU;
    static constexpr uint16_t kDngOriginalRawFileDigestTag     = 0xC71DU;
    static constexpr uint16_t kDngNewRawImageDigestTag         = 0xC7A7U;
    static constexpr uint16_t kSamsungVignettingCorrParamsTag  = 0x7032U;
    static constexpr uint16_t kSamsungChromaticAberrationCorrParamsTag = 0x7035U;
    static constexpr uint16_t kSamsungDistortionCorrParamsTag = 0x7037U;

    static constexpr uint16_t kPhaseOneSensorWidthTag          = 0x0108U;
    static constexpr uint16_t kPhaseOneSensorHeightTag         = 0x0109U;
    static constexpr uint16_t kPhaseOneSensorLeftMarginTag     = 0x010AU;
    static constexpr uint16_t kPhaseOneSensorTopMarginTag      = 0x010BU;
    static constexpr uint16_t kPhaseOneImageWidthTag           = 0x010CU;
    static constexpr uint16_t kPhaseOneImageHeightTag          = 0x010DU;
    static constexpr std::string_view kPhaseOneMainIfd         = "mk_phaseone0";
    static constexpr uint16_t kFujiRafRawImageFullSizeTag      = 0x0100U;
    static constexpr uint16_t kFujiRafRawImageCropTopLeftTag   = 0x0110U;
    static constexpr uint16_t kFujiRafRawImageCroppedSizeTag   = 0x0111U;
    static constexpr uint16_t kFujiRafRawZoomTopLeftTag        = 0x0118U;
    static constexpr uint16_t kFujiRafRawZoomSizeTag           = 0x0119U;
    static constexpr uint16_t kCanonAspectCroppedImageWidthTag = 0x0001U;
    static constexpr uint16_t kCanonAspectCroppedImageHeightTag = 0x0002U;
    static constexpr uint16_t kCanonAspectCroppedImageLeftTag   = 0x0003U;
    static constexpr uint16_t kCanonAspectCroppedImageTopTag    = 0x0004U;
    static constexpr uint16_t kCanonCropLeftMarginTag           = 0x0000U;
    static constexpr uint16_t kCanonCropRightMarginTag          = 0x0001U;
    static constexpr uint16_t kCanonCropTopMarginTag            = 0x0002U;
    static constexpr uint16_t kCanonCropBottomMarginTag         = 0x0003U;
    static constexpr uint16_t kNikonCaptureCropLeftTag          = 0x001EU;
    static constexpr uint16_t kNikonCaptureCropTopTag           = 0x0026U;
    static constexpr uint16_t kNikonCaptureCropRightTag         = 0x002EU;
    static constexpr uint16_t kNikonCaptureCropBottomTag        = 0x0036U;
    static constexpr uint16_t kSonyPanoramaCropLeftTag          = 0x0004U;
    static constexpr uint16_t kSonyPanoramaCropTopTag           = 0x0005U;
    static constexpr uint16_t kSonyPanoramaCropRightTag         = 0x0006U;
    static constexpr uint16_t kSonyPanoramaCropBottomTag        = 0x0007U;
    static constexpr uint32_t kIccHeaderProfileSizeOffset       = 0U;
    static constexpr uint32_t kIccHeaderColorSpaceOffset        = 16U;
    static constexpr uint32_t kIccHeaderPcsOffset               = 20U;

    static constexpr uint16_t kDngColorMatrixTags[] = {
        kDngColorMatrix1Tag,
        kDngColorMatrix2Tag,
        kDngColorMatrix3Tag,
    };
    static constexpr uint16_t kDngCameraCalibrationTags[] = {
        kDngCameraCalibration1Tag,
        kDngCameraCalibration2Tag,
        kDngCameraCalibration3Tag,
    };
    static constexpr uint16_t kDngReductionMatrixTags[] = {
        kDngReductionMatrix1Tag,
        kDngReductionMatrix2Tag,
        kDngReductionMatrix3Tag,
    };
    static constexpr uint16_t kDngForwardMatrixTags[] = {
        kDngForwardMatrix1Tag,
        kDngForwardMatrix2Tag,
        kDngForwardMatrix3Tag,
    };
    static constexpr uint16_t kDngWhiteBalanceVectorTags[] = {
        kDngAsShotNeutralTag,
        kDngAsShotWhiteXyTag,
        kDngAnalogBalanceTag,
    };
    static constexpr uint16_t kDngExposureGainTags[] = {
        kDngBaselineExposureTag,     kDngBaselineExposureOffsetTag,
        kDngRawToPreviewGainTag,     kDngProfileGainTableMapTag,
        kDngProfileGainTableMap2Tag,
    };
    static constexpr uint16_t kDngBlackLevelTags[] = {
        kDngBlackLevelRepeatDimTag,
        kDngBlackLevelTag,
        kDngBlackLevelDeltaHTag,
        kDngBlackLevelDeltaVTag,
    };
    static constexpr uint16_t kDngCfaLayoutTags[] = {
        kExifCfaRepeatPatternDimTag, kExifCfaPatternTag, kExifCfaPattern2Tag,
        kDngCfaPlaneColorTag,        kDngCfaLayoutTag,
    };
    static constexpr uint16_t kDngSensorGeometryTags[] = {
        kDngDefaultCropOriginTag, kDngDefaultCropSizeTag, kDngActiveAreaTag,
        kDngMaskedAreasTag,       kDngDefaultScaleTag,
    };
    static constexpr uint16_t kDngRawStorageTags[] = {
        kDngRawDataUniqueIdTag,       kDngOriginalRawFileNameTag,
        kDngOriginalRawFileDataTag,   kDngRawImageDigestTag,
        kDngOriginalRawFileDigestTag, kDngNewRawImageDigestTag,
    };

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

#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
    static bool ascii_is_upper(char c) noexcept { return c >= 'A' && c <= 'Z'; }

    static bool ascii_is_lower(char c) noexcept { return c >= 'a' && c <= 'z'; }

    static bool ascii_is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

    static bool ascii_is_alnum(char c) noexcept
    {
        return ascii_is_upper(c) || ascii_is_lower(c) || ascii_is_digit(c);
    }
#endif

    static bool contains_ascii_case_insensitive(std::string_view text,
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

    static bool
    starts_with_ascii_case_insensitive(std::string_view text,
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

    static bool equals_ascii_case_insensitive(std::string_view a,
                                              std::string_view b) noexcept
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

#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
    static void append_fuzzy_space(std::string* out)
    {
        if (!out || out->empty() || (*out)[out->size() - 1U] == ' ') {
            return;
        }
        out->push_back(' ');
    }

    static void normalize_ascii_for_fuzzy(std::string_view text,
                                          std::string* out)
    {
        if (!out) {
            return;
        }
        out->clear();
        out->reserve(text.size() + 8U);

        char previous = '\0';
        for (size_t i = 0U; i < text.size(); ++i) {
            const char c = text[i];
            if (!ascii_is_alnum(c)) {
                append_fuzzy_space(out);
                previous = ' ';
                continue;
            }
            if (ascii_is_upper(c)
                && (ascii_is_lower(previous) || ascii_is_digit(previous))) {
                append_fuzzy_space(out);
            }
            out->push_back(ascii_lower(c));
            previous = c;
        }
        while (!out->empty() && (*out)[out->size() - 1U] == ' ') {
            out->resize(out->size() - 1U);
        }
    }
#endif

    static double fuzzy_threshold_for_term(std::string_view term) noexcept
    {
        if (term.size() < 5U) {
            return 101.0;
        }
        if (term.size() == 5U) {
            return 90.0;
        }
        if (term.size() == 6U) {
            return 83.0;
        }
        return 85.0;
    }

    static double rapidfuzz_term_score(std::string_view text,
                                       std::string_view term)
    {
#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
        const double threshold = fuzzy_threshold_for_term(term);
        if (threshold > 100.0 || text.empty()) {
            return 0.0;
        }

        std::string normalized_text;
        std::string normalized_term;
        normalize_ascii_for_fuzzy(text, &normalized_text);
        normalize_ascii_for_fuzzy(term, &normalized_term);
        if (normalized_text.empty() || normalized_term.empty()) {
            return 0.0;
        }
        return rapidfuzz::fuzz::partial_ratio(normalized_text, normalized_term,
                                              threshold);
#else
        (void)text;
        (void)term;
        return 0.0;
#endif
    }

    struct MatchProvenanceState final {
        bool exact_match    = false;
        bool fuzzy_match    = false;
        uint8_t fuzzy_score = 0U;
    };

    static uint8_t fuzzy_score_to_u8(double score) noexcept
    {
        if (score <= 0.0) {
            return 0U;
        }
        if (score >= 100.0) {
            return 100U;
        }
        return static_cast<uint8_t>(score + 0.5);
    }

    static void note_exact_match(MatchProvenanceState* provenance) noexcept
    {
        if (provenance) {
            provenance->exact_match = true;
        }
    }

    static void note_fuzzy_match(MatchProvenanceState* provenance,
                                 double score) noexcept
    {
        if (!provenance) {
            return;
        }
        provenance->fuzzy_match = true;
        const uint8_t score_u8  = fuzzy_score_to_u8(score);
        if (score_u8 > provenance->fuzzy_score) {
            provenance->fuzzy_score = score_u8;
        }
    }

    static bool term_matches(std::string_view text, std::string_view term,
                             bool enable_fuzzy,
                             MatchProvenanceState* provenance)
    {
        if (contains_ascii_case_insensitive(text, term)) {
            note_exact_match(provenance);
            return true;
        }
        if (!enable_fuzzy) {
            return false;
        }
        const double score = rapidfuzz_term_score(text, term);
        if (score < fuzzy_threshold_for_term(term)) {
            return false;
        }
        note_fuzzy_match(provenance, score);
        return true;
    }

    static MetadataQueryValueShape value_shape(const MetaValue& value) noexcept
    {
        switch (value.kind) {
        case MetaValueKind::Empty: return MetadataQueryValueShape::Unknown;
        case MetaValueKind::Scalar: return MetadataQueryValueShape::Scalar;
        case MetaValueKind::Bytes: return MetadataQueryValueShape::Blob;
        case MetaValueKind::Text: return MetadataQueryValueShape::Text;
        case MetaValueKind::Array:
            switch (value.count) {
            case 2U: return MetadataQueryValueShape::Vec2;
            case 3U: return MetadataQueryValueShape::Vec3;
            case 4U: return MetadataQueryValueShape::Vec4;
            case 9U: return MetadataQueryValueShape::Matrix3x3;
            default: return MetadataQueryValueShape::Array;
            }
        }
        return MetadataQueryValueShape::Unknown;
    }

    static uint32_t numeric_element_size(MetaElementType type) noexcept
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

    static double f32_bits_to_double(uint32_t bits) noexcept
    {
        float value = 0.0F;
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&value, &bits, sizeof(value));
        return static_cast<double>(value);
    }

    static double f64_bits_to_double(uint64_t bits) noexcept
    {
        double value = 0.0;
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&value, &bits, sizeof(value));
        return value;
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
        case MetaElementType::F32:
            *out = f32_bits_to_double(value.data.f32_bits);
            return true;
        case MetaElementType::F64:
            *out = f64_bits_to_double(value.data.f64_bits);
            return true;
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
                                        MetaElementType type, size_t offset,
                                        double* out) noexcept
    {
        if (!out || offset > bytes.size()) {
            return false;
        }
        switch (type) {
        case MetaElementType::U8:
            if (offset + 1U > bytes.size()) {
                return false;
            }
            *out = static_cast<double>(static_cast<uint8_t>(bytes[offset]));
            return true;
        case MetaElementType::I8:
            if (offset + 1U > bytes.size()) {
                return false;
            }
            *out = static_cast<double>(
                static_cast<int8_t>(static_cast<uint8_t>(bytes[offset])));
            return true;
        case MetaElementType::U16: {
            uint16_t value = 0U;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            *out = static_cast<double>(value);
            return true;
        }
        case MetaElementType::I16: {
            int16_t value = 0;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            *out = static_cast<double>(value);
            return true;
        }
        case MetaElementType::U32: {
            uint32_t value = 0U;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            *out = static_cast<double>(value);
            return true;
        }
        case MetaElementType::I32: {
            int32_t value = 0;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            *out = static_cast<double>(value);
            return true;
        }
        case MetaElementType::U64: {
            uint64_t value = 0U;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            *out = static_cast<double>(value);
            return true;
        }
        case MetaElementType::I64: {
            int64_t value = 0;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            *out = static_cast<double>(value);
            return true;
        }
        case MetaElementType::F32: {
            uint32_t bits = 0U;
            if (offset + sizeof(bits) > bytes.size()) {
                return false;
            }
            std::memcpy(&bits, bytes.data() + offset, sizeof(bits));
            *out = f32_bits_to_double(bits);
            return true;
        }
        case MetaElementType::F64: {
            uint64_t bits = 0U;
            if (offset + sizeof(bits) > bytes.size()) {
                return false;
            }
            std::memcpy(&bits, bytes.data() + offset, sizeof(bits));
            *out = f64_bits_to_double(bits);
            return true;
        }
        case MetaElementType::URational: {
            URational value;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            if (value.denom == 0U) {
                return false;
            }
            *out = static_cast<double>(value.numer)
                   / static_cast<double>(value.denom);
            return true;
        }
        case MetaElementType::SRational: {
            SRational value;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            if (value.denom == 0) {
                return false;
            }
            *out = static_cast<double>(value.numer)
                   / static_cast<double>(value.denom);
            return true;
        }
        }
        return false;
    }

    static bool value_to_double_array(const MetaStore& store,
                                      const MetaValue& value,
                                      double* out_values, uint32_t max_values,
                                      uint32_t* out_count) noexcept
    {
        if (!out_values || !out_count || max_values == 0U) {
            return false;
        }
        *out_count = 0U;
        if (value.kind == MetaValueKind::Scalar) {
            if (!scalar_to_double(value, &out_values[0])) {
                return false;
            }
            *out_count = 1U;
            return true;
        }
        if (value.kind != MetaValueKind::Array) {
            return false;
        }
        const uint32_t element_size = numeric_element_size(value.elem_type);
        if (element_size == 0U) {
            return false;
        }
        const std::span<const std::byte> bytes = store.arena().span(
            value.data.span);
        const uint32_t available = static_cast<uint32_t>(bytes.size()
                                                         / element_size);
        uint32_t count           = value.count;
        if (count > available) {
            count = available;
        }
        if (count > max_values) {
            count = max_values;
        }
        for (uint32_t i = 0U; i < count; ++i) {
            if (!array_element_to_double(bytes, value.elem_type,
                                         static_cast<size_t>(i) * element_size,
                                         &out_values[i])) {
                return false;
            }
        }
        *out_count = count;
        return count > 0U;
    }

    static bool parse_decimal_number(std::string_view text,
                                     double* out) noexcept
    {
        if (!out || text.empty()) {
            return false;
        }

        size_t i       = 0U;
        double sign    = 1.0;
        double value   = 0.0;
        bool saw_digit = false;

        if (text[i] == '+') {
            ++i;
        } else if (text[i] == '-') {
            sign = -1.0;
            ++i;
        }

        for (; i < text.size(); ++i) {
            const char c = text[i];
            if (c < '0' || c > '9') {
                break;
            }
            saw_digit = true;
            value     = value * 10.0 + static_cast<double>(c - '0');
        }

        if (i < text.size() && text[i] == '.') {
            ++i;
            double scale = 0.1;
            for (; i < text.size(); ++i) {
                const char c = text[i];
                if (c < '0' || c > '9') {
                    break;
                }
                saw_digit = true;
                value += scale * static_cast<double>(c - '0');
                scale *= 0.1;
            }
        }

        if (!saw_digit || i != text.size()) {
            return false;
        }
        *out = sign * value;
        return true;
    }

    static bool is_number_separator(char c) noexcept
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ','
               || c == ';' || c == '[' || c == ']' || c == '(' || c == ')';
    }

    static bool parse_number_list(std::string_view text, double* out_values,
                                  uint32_t max_values,
                                  uint32_t* out_count) noexcept
    {
        if (!out_values || !out_count || max_values == 0U) {
            return false;
        }
        *out_count = 0U;

        size_t token_start = 0U;
        bool in_token      = false;
        for (size_t i = 0U; i <= text.size(); ++i) {
            const bool at_end = i == text.size();
            const char c      = at_end ? ' ' : text[i];
            if (!at_end && !is_number_separator(c)) {
                if (!in_token) {
                    token_start = i;
                    in_token    = true;
                }
                continue;
            }
            if (!in_token) {
                continue;
            }
            if (*out_count >= max_values) {
                return true;
            }
            double value = 0.0;
            if (!parse_decimal_number(text.substr(token_start, i - token_start),
                                      &value)) {
                return false;
            }
            out_values[*out_count] = value;
            *out_count += 1U;
            in_token = false;
        }
        return *out_count > 0U;
    }

    static bool text_value(const MetaStore& store, const MetaValue& value,
                           std::string_view* out) noexcept
    {
        if (!out || value.kind != MetaValueKind::Text) {
            return false;
        }
        *out = arena_string(store.arena(), value.data.span);
        return true;
    }

    static bool entry_is_deleted(const Entry& entry) noexcept
    {
        return any(entry.flags, EntryFlags::Deleted);
    }

    static void append_unique_entry(std::vector<EntryId>* entries, EntryId id)
    {
        if (!entries || id == kInvalidEntryId) {
            return;
        }
        for (size_t i = 0U; i < entries->size(); ++i) {
            if ((*entries)[i] == id) {
                return;
            }
        }
        entries->push_back(id);
    }

    static bool tag_in_series(uint16_t tag, const uint16_t* tags,
                              size_t tag_count) noexcept
    {
        if (!tags) {
            return false;
        }
        for (size_t i = 0U; i < tag_count; ++i) {
            if (tags[i] == tag) {
                return true;
            }
        }
        return false;
    }

    static bool exif_entry_in_tag_series(const Entry& entry,
                                         const uint16_t* tags,
                                         size_t tag_count) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }
        return tag_in_series(entry.key.data.exif_tag.tag, tags, tag_count);
    }

    static uint32_t crop_match_terms(std::string_view name,
                                     std::string_view group, bool enable_fuzzy,
                                     MatchProvenanceState* provenance) noexcept
    {
        uint32_t terms = 0U;
        if (term_matches(name, "crop", enable_fuzzy, provenance)
            || term_matches(name, "rawcrop", enable_fuzzy, provenance)
            || term_matches(name, "raw crop", enable_fuzzy, provenance)
            || term_matches(name, "defaultcrop", enable_fuzzy, provenance)
            || term_matches(name, "default crop", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Crop);
        }
        if (term_matches(name, "border", enable_fuzzy, provenance)
            || term_matches(name, "sensorborder", enable_fuzzy, provenance)
            || term_matches(name, "sensor border", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Border);
        }
        if (term_matches(name, "margin", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Margin);
        }
        if (term_matches(name, "padding", enable_fuzzy, provenance)
            || term_matches(name, "maskedarea", enable_fuzzy, provenance)
            || term_matches(name, "masked area", enable_fuzzy, provenance)
            || term_matches(name, "opticalblack", enable_fuzzy, provenance)
            || term_matches(name, "optical black", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Padding);
        }
        if (term_matches(name, "activearea", enable_fuzzy, provenance)
            || term_matches(name, "active area", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::ActiveArea);
        }
        if (term_matches(name, "origin", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Origin);
        }
        if (term_matches(name, "offset", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Offset);
        }
        if (term_matches(name, "size", enable_fuzzy, provenance)
            || term_matches(name, "width", enable_fuzzy, provenance)
            || term_matches(name, "height", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Size);
        }
        if (term_matches(name, "sensor", enable_fuzzy, provenance)
            || contains_ascii_case_insensitive(group, "phaseone")) {
            if (contains_ascii_case_insensitive(group, "phaseone")) {
                note_exact_match(provenance);
            }
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor);
        }
        if (term_matches(name, "image", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Image);
        }
        return terms;
    }

    static uint32_t
    exposure_gain_match_terms(std::string_view name, bool enable_fuzzy,
                              MatchProvenanceState* provenance) noexcept
    {
        uint32_t terms = 0U;
        if (term_matches(name, "exposure", enable_fuzzy, provenance)
            || term_matches(name, "aeprogram", enable_fuzzy, provenance)
            || term_matches(name, "ae program", enable_fuzzy, provenance)
            || term_matches(name, "shutter", enable_fuzzy, provenance)
            || term_matches(name, "aperture", enable_fuzzy, provenance)
            || term_matches(name, "fnumber", enable_fuzzy, provenance)
            || term_matches(name, "f-number", enable_fuzzy, provenance)
            || term_matches(name, "f number", enable_fuzzy, provenance)
            || term_matches(name, "brightness", enable_fuzzy, provenance)
            || contains_ascii_case_insensitive(name, "iso")) {
            if (contains_ascii_case_insensitive(name, "iso")) {
                note_exact_match(provenance);
            }
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Exposure);
        }
        if (term_matches(name, "bias", enable_fuzzy, provenance)
            || term_matches(name, "compensation", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Bias);
        }
        if (term_matches(name, "gain", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Gain);
        }
        return terms;
    }

    static uint32_t
    white_balance_match_terms(std::string_view name, bool enable_fuzzy,
                              MatchProvenanceState* provenance) noexcept
    {
        uint32_t terms = 0U;
        if (term_matches(name, "whitebalance", enable_fuzzy, provenance)
            || term_matches(name, "white balance", enable_fuzzy, provenance)
            || term_matches(name, "asshotneutral", enable_fuzzy, provenance)
            || term_matches(name, "asshotwhitexy", enable_fuzzy, provenance)
            || term_matches(name, "colortemp", enable_fuzzy, provenance)
            || term_matches(name, "color temperature", enable_fuzzy, provenance)
            || starts_with_ascii_case_insensitive(name, "wb")
            || contains_ascii_case_insensitive(name, "_wb")
            || contains_ascii_case_insensitive(name, " wb")) {
            if (starts_with_ascii_case_insensitive(name, "wb")
                || contains_ascii_case_insensitive(name, "_wb")
                || contains_ascii_case_insensitive(name, " wb")) {
                note_exact_match(provenance);
            }
            terms |= static_cast<uint32_t>(
                MetadataQueryMatchTerm::WhiteBalance);
        }
        return terms;
    }

    static uint32_t color_match_terms(std::string_view name, bool enable_fuzzy,
                                      MatchProvenanceState* provenance) noexcept
    {
        uint32_t terms = 0U;
        if (term_matches(name, "color", enable_fuzzy, provenance)
            || term_matches(name, "colour", enable_fuzzy, provenance)
            || term_matches(name, "illuminant", enable_fuzzy, provenance)
            || term_matches(name, "profile", enable_fuzzy, provenance)
            || term_matches(name, "tonecurve", enable_fuzzy, provenance)
            || term_matches(name, "tone curve", enable_fuzzy, provenance)
            || term_matches(name, "creative style", enable_fuzzy, provenance)
            || term_matches(name, "creativestyle", enable_fuzzy, provenance)
            || term_matches(name, "picture style", enable_fuzzy, provenance)
            || term_matches(name, "picturestyle", enable_fuzzy, provenance)
            || term_matches(name, "film simulation", enable_fuzzy, provenance)
            || term_matches(name, "filmsimulation", enable_fuzzy, provenance)
            || term_matches(name, "dynamic range optimizer", enable_fuzzy,
                            provenance)
            || term_matches(name, "dynamicrangeoptimizer", enable_fuzzy,
                            provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Color);
        }
        if (term_matches(name, "matrix", enable_fuzzy, provenance)
            || term_matches(name, "forwardmatrix", enable_fuzzy, provenance)
            || term_matches(name, "forward matrix", enable_fuzzy, provenance)
            || term_matches(name, "reductionmatrix", enable_fuzzy, provenance)
            || term_matches(name, "reduction matrix", enable_fuzzy, provenance)
            || term_matches(name, "cameratoxyz", enable_fuzzy, provenance)
            || term_matches(name, "camera to xyz", enable_fuzzy, provenance)
            || term_matches(name, "cameratorgb", enable_fuzzy, provenance)
            || term_matches(name, "camera to rgb", enable_fuzzy, provenance)
            || term_matches(name, "ccm", enable_fuzzy, provenance)
            || term_matches(name, "colorcorrectionmatrix", enable_fuzzy,
                            provenance)
            || term_matches(name, "color correction matrix", enable_fuzzy,
                            provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Matrix);
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Color);
        }
        if (term_matches(name, "calibration", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Calibration);
        }
        if (term_matches(name, "profile", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Profile);
        }
        return terms;
    }

    static bool name_is_af_micro_adjustment(std::string_view name) noexcept
    {
        return contains_ascii_case_insensitive(name, "afmicroadj")
               || contains_ascii_case_insensitive(name, "af micro adj")
               || contains_ascii_case_insensitive(name, "af micro adjustment")
               || contains_ascii_case_insensitive(name, "afmicroadjustment")
               || contains_ascii_case_insensitive(name, "microadjustment");
    }

    static uint32_t
    lens_correction_match_terms(std::string_view name, bool enable_fuzzy,
                                MatchProvenanceState* provenance) noexcept
    {
        uint32_t terms = 0U;
        if (term_matches(name, "lens", enable_fuzzy, provenance)
            || term_matches(name, "distort", enable_fuzzy, provenance)
            || term_matches(name, "vignet", enable_fuzzy, provenance)
            || term_matches(name, "aberration", enable_fuzzy, provenance)
            || term_matches(name, "shading", enable_fuzzy, provenance)
            || term_matches(name, "peripheral", enable_fuzzy, provenance)
            || term_matches(name, "diffraction", enable_fuzzy, provenance)
            || term_matches(name, "opcode", enable_fuzzy, provenance)
            || term_matches(name, "chromaticaberration", enable_fuzzy,
                            provenance)
            || term_matches(name, "chromatic aberration", enable_fuzzy,
                            provenance)
            || term_matches(name, "geometricdistortion", enable_fuzzy,
                            provenance)
            || term_matches(name, "geometric distortion", enable_fuzzy,
                            provenance)
            || term_matches(name, "radial correction", enable_fuzzy, provenance)
            || term_matches(name, "radialcorrection", enable_fuzzy, provenance)
            || term_matches(name, "optical correction", enable_fuzzy, provenance)
            || term_matches(name, "opticalcorrection", enable_fuzzy, provenance)
            || term_matches(name, "peripheralillumination", enable_fuzzy,
                            provenance)
            || term_matches(name, "peripheral illumination", enable_fuzzy,
                            provenance)
            || name_is_af_micro_adjustment(name)) {
            if (name_is_af_micro_adjustment(name)) {
                note_exact_match(provenance);
            }
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Lens);
        }
        if (term_matches(name, "correction", enable_fuzzy, provenance)
            || term_matches(name, "corr", enable_fuzzy, provenance)
            || term_matches(name, "distort", enable_fuzzy, provenance)
            || term_matches(name, "vignet", enable_fuzzy, provenance)
            || term_matches(name, "aberration", enable_fuzzy, provenance)
            || term_matches(name, "shading", enable_fuzzy, provenance)
            || term_matches(name, "opcode", enable_fuzzy, provenance)
            || name_is_af_micro_adjustment(name)) {
            if (name_is_af_micro_adjustment(name)) {
                note_exact_match(provenance);
            }
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Correction);
        }
        return terms;
    }

    static uint32_t
    raw_processing_match_terms(std::string_view name, std::string_view group,
                               bool enable_fuzzy,
                               MatchProvenanceState* provenance) noexcept
    {
        uint32_t terms = 0U;
        if (term_matches(name, "blacklevel", enable_fuzzy, provenance)
            || term_matches(name, "black level", enable_fuzzy, provenance)
            || term_matches(name, "opticalblack", enable_fuzzy, provenance)
            || term_matches(name, "optical black", enable_fuzzy, provenance)
            || term_matches(name, "blackmask", enable_fuzzy, provenance)
            || term_matches(name, "black mask", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::BlackLevel);
        }
        if (term_matches(name, "whitelevel", enable_fuzzy, provenance)
            || term_matches(name, "white level", enable_fuzzy, provenance)
            || term_matches(name, "whiteclip", enable_fuzzy, provenance)
            || term_matches(name, "white clip", enable_fuzzy, provenance)
            || term_matches(name, "saturationlevel", enable_fuzzy, provenance)
            || term_matches(name, "saturation level", enable_fuzzy,
                            provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::WhiteLevel);
        }
        if (term_matches(name, "linearization", enable_fuzzy, provenance)
            || term_matches(name, "linearity", enable_fuzzy, provenance)
            || term_matches(name, "linearresponse", enable_fuzzy, provenance)
            || term_matches(name, "linear response", enable_fuzzy, provenance)
            || term_matches(name, "rawcurve", enable_fuzzy, provenance)
            || term_matches(name, "raw curve", enable_fuzzy, provenance)
            || term_matches(name, "neflinearizationtable", enable_fuzzy,
                            provenance)
            || term_matches(name, "nef linearization table", enable_fuzzy,
                            provenance)
            || term_matches(name, "klut", enable_fuzzy, provenance)
            || term_matches(name, "nonlinearity", enable_fuzzy, provenance)
            || term_matches(name, "non linearity", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(
                MetadataQueryMatchTerm::Linearization);
        }
        if (contains_ascii_case_insensitive(name, "cfa")
            || term_matches(name, "bayer", enable_fuzzy, provenance)) {
            if (contains_ascii_case_insensitive(name, "cfa")) {
                note_exact_match(provenance);
            }
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Cfa);
        }
        if (term_matches(name, "rawdata", enable_fuzzy, provenance)
            || term_matches(name, "raw data", enable_fuzzy, provenance)
            || term_matches(name, "rawfile", enable_fuzzy, provenance)
            || term_matches(name, "raw file", enable_fuzzy, provenance)
            || term_matches(name, "rawformat", enable_fuzzy, provenance)
            || term_matches(name, "raw format", enable_fuzzy, provenance)
            || term_matches(name, "rawimage", enable_fuzzy, provenance)
            || term_matches(name, "raw image", enable_fuzzy, provenance)
            || term_matches(name, "originalraw", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Raw);
        }
        if (term_matches(name, "storage", enable_fuzzy, provenance)
            || term_matches(name, "strip", enable_fuzzy, provenance)
            || term_matches(name, "bytecount", enable_fuzzy, provenance)
            || term_matches(name, "byte count", enable_fuzzy, provenance)
            || term_matches(name, "fileoffset", enable_fuzzy, provenance)
            || term_matches(name, "file offset", enable_fuzzy, provenance)
            || term_matches(name, "dataoffset", enable_fuzzy, provenance)
            || term_matches(name, "data offset", enable_fuzzy, provenance)
            || term_matches(name, "datalength", enable_fuzzy, provenance)
            || term_matches(name, "data length", enable_fuzzy, provenance)
            || term_matches(name, "compresseddata", enable_fuzzy, provenance)
            || term_matches(name, "compressed data", enable_fuzzy, provenance)
            || term_matches(name, "byteorder", enable_fuzzy, provenance)
            || term_matches(name, "byte order", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Storage);
        }
        if (term_matches(name, "sensor", enable_fuzzy, provenance)
            || term_matches(name, "validbits", enable_fuzzy, provenance)
            || term_matches(name, "valid bits", enable_fuzzy, provenance)
            || term_matches(name, "bitdepth", enable_fuzzy, provenance)
            || term_matches(name, "bit depth", enable_fuzzy, provenance)
            || term_matches(name, "rawdepth", enable_fuzzy, provenance)
            || term_matches(name, "raw depth", enable_fuzzy, provenance)
            || term_matches(name, "rawvaluerange", enable_fuzzy, provenance)
            || term_matches(name, "raw value range", enable_fuzzy, provenance)
            || term_matches(name, "rawvaluemedian", enable_fuzzy, provenance)
            || term_matches(name, "raw value median", enable_fuzzy, provenance)
            || term_matches(name, "maskedpixels", enable_fuzzy, provenance)
            || term_matches(name, "masked pixels", enable_fuzzy, provenance)
            || contains_ascii_case_insensitive(group, "phaseone")) {
            if (contains_ascii_case_insensitive(group, "phaseone")) {
                note_exact_match(provenance);
            }
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor);
        }
        if (term_matches(name, "pixelshift", enable_fuzzy, provenance)
            || term_matches(name, "pixel shift", enable_fuzzy, provenance)
            || term_matches(name, "multishot", enable_fuzzy, provenance)
            || term_matches(name, "multi shot", enable_fuzzy, provenance)
            || term_matches(name, "multiframe", enable_fuzzy, provenance)
            || term_matches(name, "multi frame", enable_fuzzy, provenance)
            || term_matches(name, "deepfusion", enable_fuzzy, provenance)
            || term_matches(name, "deep fusion", enable_fuzzy, provenance)
            || term_matches(name, "smart hdr", enable_fuzzy, provenance)
            || term_matches(name, "imagefusion", enable_fuzzy, provenance)
            || term_matches(name, "image fusion", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(
                MetadataQueryMatchTerm::SourceProcessing);
        }
        if (term_matches(name, "creative style", enable_fuzzy, provenance)
            || term_matches(name, "creativestyle", enable_fuzzy, provenance)
            || term_matches(name, "picture style", enable_fuzzy, provenance)
            || term_matches(name, "picturestyle", enable_fuzzy, provenance)
            || term_matches(name, "film simulation", enable_fuzzy, provenance)
            || term_matches(name, "filmsimulation", enable_fuzzy, provenance)
            || term_matches(name, "dynamic range optimizer", enable_fuzzy,
                            provenance)
            || term_matches(name, "dynamicrangeoptimizer", enable_fuzzy,
                            provenance)
            || term_matches(name, "active dlighting", enable_fuzzy, provenance)
            || term_matches(name, "activedlighting", enable_fuzzy, provenance)
            || term_matches(name, "raw development", enable_fuzzy, provenance)
            || term_matches(name, "rawdevelopment", enable_fuzzy, provenance)
            || term_matches(name, "raw develop", enable_fuzzy, provenance)
            || term_matches(name, "rawdevelop", enable_fuzzy, provenance)
            || contains_ascii_case_insensitive(name, "ambience")) {
            if (contains_ascii_case_insensitive(name, "ambience")) {
                note_exact_match(provenance);
            }
            terms |= static_cast<uint32_t>(
                MetadataQueryMatchTerm::SourceProcessing);
        }
        if (term_matches(name, "thermal", enable_fuzzy, provenance)
            || term_matches(name, "radiometric", enable_fuzzy, provenance)
            || term_matches(name, "emissivity", enable_fuzzy, provenance)
            || term_matches(name, "planck", enable_fuzzy, provenance)
            || term_matches(name, "rawthermal", enable_fuzzy, provenance)
            || term_matches(name, "raw thermal", enable_fuzzy, provenance)
            || term_matches(name, "irwindow", enable_fuzzy, provenance)
            || term_matches(name, "ir window", enable_fuzzy, provenance)
            || term_matches(name, "real2ir", enable_fuzzy, provenance)
            || term_matches(name, "real to ir", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(
                MetadataQueryMatchTerm::SourceProcessing);
        }
        if (term_matches(name, "stitch", enable_fuzzy, provenance)
            || term_matches(name, "panorama", enable_fuzzy, provenance)
            || term_matches(name, "panoramic", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(
                MetadataQueryMatchTerm::SourceProcessing);
        }
        return terms;
    }

    static uint32_t
    orientation_match_terms(std::string_view name, bool enable_fuzzy,
                            MatchProvenanceState* provenance) noexcept
    {
        if (term_matches(name, "orientation", enable_fuzzy, provenance)) {
            return static_cast<uint32_t>(MetadataQueryMatchTerm::Orientation);
        }
        return 0U;
    }

    static uint32_t
    descriptive_match_terms(std::string_view name, bool enable_fuzzy,
                            MatchProvenanceState* provenance) noexcept
    {
        uint32_t terms = 0U;
        if (term_matches(name, "title", enable_fuzzy, provenance)
            || term_matches(name, "objectname", enable_fuzzy, provenance)
            || term_matches(name, "object name", enable_fuzzy, provenance)
            || term_matches(name, "headline", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Title);
        }
        if (term_matches(name, "description", enable_fuzzy, provenance)
            || term_matches(name, "caption", enable_fuzzy, provenance)
            || term_matches(name, "abstract", enable_fuzzy, provenance)
            || term_matches(name, "comment", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Description);
        }
        if (term_matches(name, "creator", enable_fuzzy, provenance)
            || term_matches(name, "author", enable_fuzzy, provenance)
            || term_matches(name, "byline", enable_fuzzy, provenance)
            || term_matches(name, "by-line", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Creator);
        }
        if (term_matches(name, "keyword", enable_fuzzy, provenance)
            || term_matches(name, "keywords", enable_fuzzy, provenance)
            || term_matches(name, "subject", enable_fuzzy, provenance)) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Keywords);
        }
        return terms;
    }

    static const char* iptc_descriptive_dataset_name(uint16_t record,
                                                     uint16_t dataset) noexcept
    {
        if (record != 2U) {
            return "";
        }
        switch (dataset) {
        case 5U: return "ObjectName";
        case 25U: return "Keywords";
        case 80U: return "By-line";
        case 105U: return "Headline";
        case 110U: return "Credit";
        case 115U: return "Source";
        case 116U: return "CopyrightNotice";
        case 120U: return "Caption-Abstract";
        default: break;
        }
        return "";
    }

    static uint32_t exact_iptc_terms_for_kind(uint16_t record, uint16_t dataset,
                                              MetadataQueryKind kind) noexcept
    {
        if (kind != MetadataQueryKind::Descriptive || record != 2U) {
            return 0U;
        }
        switch (dataset) {
        case 5U:
        case 105U: return static_cast<uint32_t>(MetadataQueryMatchTerm::Title);
        case 25U:
            return static_cast<uint32_t>(MetadataQueryMatchTerm::Keywords);
        case 80U: return static_cast<uint32_t>(MetadataQueryMatchTerm::Creator);
        case 120U:
            return static_cast<uint32_t>(MetadataQueryMatchTerm::Description);
        default: break;
        }
        return 0U;
    }

    static std::string_view xmp_leaf_property(std::string_view path) noexcept
    {
        size_t start                = 0U;
        const size_t path_separator = path.rfind('/');
        if (path_separator != std::string_view::npos) {
            start = path_separator + 1U;
        }
        size_t end = path.find('[', start);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        for (size_t i = start; i < end; ++i) {
            if (path[i] == ':') {
                start = i + 1U;
            }
        }
        return path.substr(start, end - start);
    }

    static bool xmp_path_has_root(std::string_view path,
                                  std::string_view expected) noexcept
    {
        const size_t separator = path.find('/');
        std::string_view root  = separator == std::string_view::npos
                                     ? path
                                     : path.substr(0U, separator);
        const size_t prefix    = root.find_last_of(":.");
        if (prefix != std::string_view::npos) {
            root.remove_prefix(prefix + 1U);
        }
        const size_t qualifier = root.find('[');
        if (qualifier != std::string_view::npos) {
            root = root.substr(0U, qualifier);
        }
        return equals_ascii_case_insensitive(root, expected);
    }

    static uint32_t
    xmp_descriptive_terms(std::string_view path,
                          MatchProvenanceState* provenance) noexcept
    {
        const std::string_view leaf = xmp_leaf_property(path);
        if (equals_ascii_case_insensitive(leaf, "title")
            || equals_ascii_case_insensitive(leaf, "headline")
            || equals_ascii_case_insensitive(leaf, "objectname")) {
            note_exact_match(provenance);
            return static_cast<uint32_t>(MetadataQueryMatchTerm::Title);
        }
        if (equals_ascii_case_insensitive(leaf, "description")
            || equals_ascii_case_insensitive(leaf, "caption")
            || equals_ascii_case_insensitive(leaf, "abstract")) {
            note_exact_match(provenance);
            return static_cast<uint32_t>(MetadataQueryMatchTerm::Description);
        }
        if (equals_ascii_case_insensitive(leaf, "creator")
            || equals_ascii_case_insensitive(leaf, "author")
            || equals_ascii_case_insensitive(leaf, "byline")) {
            note_exact_match(provenance);
            return static_cast<uint32_t>(MetadataQueryMatchTerm::Creator);
        }
        if (equals_ascii_case_insensitive(leaf, "subject")
            || equals_ascii_case_insensitive(leaf, "keyword")
            || equals_ascii_case_insensitive(leaf, "keywords")) {
            note_exact_match(provenance);
            return static_cast<uint32_t>(MetadataQueryMatchTerm::Keywords);
        }
        return 0U;
    }

    static MetadataQuerySemanticKind
    xmp_descriptive_exact_semantic(std::string_view ns,
                                   std::string_view path) noexcept
    {
        const std::string_view leaf = xmp_leaf_property(path);
        if (ns == kDcXmpSchema
            && equals_ascii_case_insensitive(leaf, "rights")) {
            return MetadataQuerySemanticKind::Rights;
        }
        if (ns == kPhotoshopXmpSchema) {
            if (equals_ascii_case_insensitive(leaf, "Credit")) {
                return MetadataQuerySemanticKind::Credit;
            }
            if (equals_ascii_case_insensitive(leaf, "Source")) {
                return MetadataQuerySemanticKind::Source;
            }
        }
        if (ns == kIptcCoreXmpSchema
            && xmp_path_has_root(path, "CreatorContactInfo")) {
            return MetadataQuerySemanticKind::Contact;
        }
        if (ns == kIptcExtXmpSchema
            && equals_ascii_case_insensitive(leaf, "DigitalSourceType")) {
            return MetadataQuerySemanticKind::Source;
        }
        if (ns == kIptcExtXmpSchema) {
            if (xmp_path_has_root(path, "ArtworkOrObject")) {
                return MetadataQuerySemanticKind::Artwork;
            }
            if (xmp_path_has_root(path, "PersonInImage")
                || xmp_path_has_root(path, "PersonInImageWDetails")
                || xmp_path_has_root(path, "ModelAge")) {
                return MetadataQuerySemanticKind::Person;
            }
            if (xmp_path_has_root(path, "OrganisationInImageName")
                || xmp_path_has_root(path, "OrganisationInImageCode")) {
                return MetadataQuerySemanticKind::Organization;
            }
            if (xmp_path_has_root(path, "ProductInImage")) {
                return MetadataQuerySemanticKind::Product;
            }
            if (xmp_path_has_root(path, "Event")
                || xmp_path_has_root(path, "EventId")) {
                return MetadataQuerySemanticKind::Event;
            }
            if (xmp_path_has_root(path, "EmbdEncRightsExpr")
                || xmp_path_has_root(path, "LinkedEncRightsExpr")) {
                return MetadataQuerySemanticKind::RightsExpression;
            }
        }
        if (ns == kXmpRightsSchema) {
            if (equals_ascii_case_insensitive(leaf, "UsageTerms")) {
                return MetadataQuerySemanticKind::License;
            }
            if (equals_ascii_case_insensitive(leaf, "Certificate")
                || equals_ascii_case_insensitive(leaf, "Marked")
                || equals_ascii_case_insensitive(leaf, "Owner")
                || equals_ascii_case_insensitive(leaf, "WebStatement")) {
                return MetadataQuerySemanticKind::Rights;
            }
        }
        if (ns == kPlusXmpSchema) {
            if (xmp_path_has_root(path, "CopyrightOwner")) {
                return MetadataQuerySemanticKind::Rights;
            }
            if (xmp_path_has_root(path, "Licensor")
                || xmp_path_has_root(path, "Licensee")) {
                return MetadataQuerySemanticKind::License;
            }
            if (equals_ascii_case_insensitive(leaf, "ModelReleaseStatus")
                || equals_ascii_case_insensitive(leaf, "ModelReleaseID")
                || equals_ascii_case_insensitive(leaf, "PropertyReleaseStatus")
                || equals_ascii_case_insensitive(leaf, "PropertyReleaseID")) {
                return MetadataQuerySemanticKind::Release;
            }
            if (equals_ascii_case_insensitive(leaf, "LicenseID")
                || equals_ascii_case_insensitive(leaf, "LicenseStartDate")
                || equals_ascii_case_insensitive(leaf, "LicenseEndDate")
                || equals_ascii_case_insensitive(leaf, "MediaConstraints")
                || equals_ascii_case_insensitive(leaf, "RegionConstraints")
                || equals_ascii_case_insensitive(leaf,
                                                 "ProductOrServiceConstraints")
                || equals_ascii_case_insensitive(leaf, "ImageFileConstraints")
                || equals_ascii_case_insensitive(leaf,
                                                 "ImageAlterationConstraints")
                || equals_ascii_case_insensitive(leaf,
                                                 "OtherLicenseRequirements")
                || equals_ascii_case_insensitive(leaf, "OtherConditions")
                || equals_ascii_case_insensitive(leaf, "OtherConstraints")
                || equals_ascii_case_insensitive(leaf, "LicensorTransactionID")
                || equals_ascii_case_insensitive(leaf, "LicenseeTransactionID")
                || equals_ascii_case_insensitive(leaf,
                                                 "LicenseeProjectReference")
                || equals_ascii_case_insensitive(leaf, "LicenseTransactionDate")
                || equals_ascii_case_insensitive(leaf, "TermsAndConditionsText")
                || equals_ascii_case_insensitive(leaf, "TermsAndConditionsURL")
                || equals_ascii_case_insensitive(leaf, "LicensorName")
                || equals_ascii_case_insensitive(leaf, "LicensorID")) {
                return MetadataQuerySemanticKind::License;
            }
            if (equals_ascii_case_insensitive(leaf, "CreditLineRequired")) {
                return MetadataQuerySemanticKind::Credit;
            }
            if (equals_ascii_case_insensitive(leaf, "CopyrightStatus")
                || equals_ascii_case_insensitive(leaf, "CopyrightOwnerName")
                || equals_ascii_case_insensitive(leaf, "CopyrightOwnerID")) {
                return MetadataQuerySemanticKind::Rights;
            }
        }
        return MetadataQuerySemanticKind::Unknown;
    }

    static MetadataQuerySemanticKind
    iptc_descriptive_exact_semantic(uint16_t record, uint16_t dataset) noexcept
    {
        if (record != 2U) {
            return MetadataQuerySemanticKind::Unknown;
        }
        switch (dataset) {
        case 110U: return MetadataQuerySemanticKind::Credit;
        case 115U: return MetadataQuerySemanticKind::Source;
        case 116U: return MetadataQuerySemanticKind::Rights;
        default: break;
        }
        return MetadataQuerySemanticKind::Unknown;
    }

    static uint32_t color_profile_terms() noexcept
    {
        return static_cast<uint32_t>(MetadataQueryMatchTerm::Color)
               | static_cast<uint32_t>(MetadataQueryMatchTerm::Profile);
    }

    static bool color_name_is_source_transform(std::string_view name) noexcept
    {
        return contains_ascii_case_insensitive(name, "creative style")
               || contains_ascii_case_insensitive(name, "creativestyle")
               || contains_ascii_case_insensitive(name, "picture style")
               || contains_ascii_case_insensitive(name, "picturestyle")
               || contains_ascii_case_insensitive(name, "film simulation")
               || contains_ascii_case_insensitive(name, "filmsimulation")
               || contains_ascii_case_insensitive(name,
                                                  "dynamic range optimizer")
               || contains_ascii_case_insensitive(name, "dynamicrangeoptimizer")
               || contains_ascii_case_insensitive(name, "camera profile")
               || contains_ascii_case_insensitive(name, "cameraprofile")
               || contains_ascii_case_insensitive(name, "tone curve")
               || contains_ascii_case_insensitive(name, "tonecurve")
               || contains_ascii_case_insensitive(name, "raw development")
               || contains_ascii_case_insensitive(name, "rawdevelopment");
    }

    static bool exif_group_is_canon_colordata_source_transform(
        std::string_view group) noexcept
    {
        return contains_ascii_case_insensitive(group, "mk_canon_colordata")
               || contains_ascii_case_insensitive(group,
                                                  "makernote:canon:colordata");
    }

    static bool exif_group_is_nikonsettings_source_processing(
        std::string_view group) noexcept
    {
        return contains_ascii_case_insensitive(group, "mk_nikonsettings")
               || contains_ascii_case_insensitive(group, "nikonsettings");
    }

    static uint32_t
    xmp_color_profile_terms(std::string_view path,
                            MatchProvenanceState* provenance) noexcept
    {
        const std::string_view leaf = xmp_leaf_property(path);
        if (equals_ascii_case_insensitive(leaf, "ICCProfile")
            || equals_ascii_case_insensitive(leaf, "ICCProfileName")
            || contains_ascii_case_insensitive(path, "iccprofile")) {
            note_exact_match(provenance);
            return color_profile_terms();
        }
        if (equals_ascii_case_insensitive(leaf, "ColorSpace")
            || contains_ascii_case_insensitive(path, "colorspace")) {
            note_exact_match(provenance);
            return color_profile_terms();
        }
        return 0U;
    }

    static bool
    xmp_namespace_is_camera_raw_settings(std::string_view ns) noexcept
    {
        return contains_ascii_case_insensitive(ns, "camera-raw-settings")
               || contains_ascii_case_insensitive(ns, "/crs/");
    }

    static uint32_t
    xmp_source_color_transform_terms(std::string_view ns, std::string_view path,
                                     MatchProvenanceState* provenance) noexcept
    {
        const std::string_view leaf = xmp_leaf_property(path);
        if (xmp_namespace_is_camera_raw_settings(ns)
            && (equals_ascii_case_insensitive(leaf, "CameraProfile")
                || equals_ascii_case_insensitive(leaf, "ProfileName")
                || equals_ascii_case_insensitive(leaf, "Look")
                || equals_ascii_case_insensitive(leaf, "LookName")
                || starts_with_ascii_case_insensitive(leaf, "ToneCurve"))) {
            note_exact_match(provenance);
            return static_cast<uint32_t>(MetadataQueryMatchTerm::Color);
        }
        if (color_name_is_source_transform(path)) {
            note_exact_match(provenance);
            return static_cast<uint32_t>(MetadataQueryMatchTerm::Color);
        }
        return 0U;
    }

    static bool raw_curve_name_is_linearity_limit(std::string_view name) noexcept
    {
        return contains_ascii_case_insensitive(name, "linearitylimit")
               || contains_ascii_case_insensitive(name, "linearity limit")
               || contains_ascii_case_insensitive(name, "linearresponse")
               || contains_ascii_case_insensitive(name, "linear response")
               || contains_ascii_case_insensitive(name, "linearityuppermargin")
               || contains_ascii_case_insensitive(name,
                                                  "linearity upper margin")
               || contains_ascii_case_insensitive(name,
                                                  "highlightlinearitylimit")
               || contains_ascii_case_insensitive(name,
                                                  "highlight linearity limit");
    }

    static bool
    raw_curve_name_is_calibration_curve(std::string_view name) noexcept
    {
        return contains_ascii_case_insensitive(name,
                                               "linearizationcoefficients")
               || contains_ascii_case_insensitive(name,
                                                  "linearization coefficients")
               || contains_ascii_case_insensitive(name, "nonlinearityspline")
               || contains_ascii_case_insensitive(name, "non linearity spline")
               || contains_ascii_case_insensitive(name, "linearityspline")
               || contains_ascii_case_insensitive(name, "linearity spline");
    }

    static bool raw_curve_name_is_value_curve(std::string_view name) noexcept
    {
        return contains_ascii_case_insensitive(name, "linearizationtable")
               || contains_ascii_case_insensitive(name, "linearization table")
               || contains_ascii_case_insensitive(name, "neflinearizationtable")
               || contains_ascii_case_insensitive(name,
                                                  "nef linearization table")
               || contains_ascii_case_insensitive(name, "klut")
               || contains_ascii_case_insensitive(name, "lin12toklut")
               || contains_ascii_case_insensitive(name, "klut12tolin12")
               || contains_ascii_case_insensitive(name, "invnifnonlinearity");
    }

    static bool raw_curve_tag_is_panasonic_linearity_limit(uint16_t tag) noexcept
    {
        return tag == kPanasonicLinearityLimitRedTag
               || tag == kPanasonicLinearityLimitGreenTag
               || tag == kPanasonicLinearityLimitBlueTag;
    }

    static MetadataQuerySemanticKind
    raw_curve_semantic_from_name(std::string_view group, std::string_view name,
                                 uint16_t tag) noexcept
    {
        const bool panasonic_linearity_limit
            = raw_curve_tag_is_panasonic_linearity_limit(tag)
              && (contains_ascii_case_insensitive(group, "panasonic")
                  || raw_curve_name_is_linearity_limit(name));
        if (tag == kSonyToneCurveTag
            && contains_ascii_case_insensitive(name, "sonytonecurve")) {
            return MetadataQuerySemanticKind::RawCurveControlPoints;
        }
        if (tag == kDngLinearizationTableTag
            || raw_curve_name_is_value_curve(name)) {
            return MetadataQuerySemanticKind::RawValueCurve;
        }
        if (tag == kDngLinearResponseLimitTag || panasonic_linearity_limit
            || raw_curve_name_is_linearity_limit(name)) {
            return MetadataQuerySemanticKind::RawLinearityLimit;
        }
        if ((contains_ascii_case_insensitive(group, "phaseone")
             && (tag == 0x0419U || tag == 0x041AU))
            || raw_curve_name_is_calibration_curve(name)) {
            return MetadataQuerySemanticKind::RawCalibrationCurve;
        }
        return MetadataQuerySemanticKind::Unknown;
    }

    static MetadataQuerySemanticKind
    source_processing_semantic_from_name(std::string_view name) noexcept
    {
        if (contains_ascii_case_insensitive(name, "thermal")
            || contains_ascii_case_insensitive(name, "radiometric")
            || contains_ascii_case_insensitive(name, "emissivity")
            || contains_ascii_case_insensitive(name, "planck")
            || contains_ascii_case_insensitive(name, "rawthermal")
            || contains_ascii_case_insensitive(name, "raw thermal")
            || contains_ascii_case_insensitive(name, "irwindow")
            || contains_ascii_case_insensitive(name, "ir window")
            || contains_ascii_case_insensitive(name, "real2ir")
            || contains_ascii_case_insensitive(name, "real to ir")) {
            return MetadataQuerySemanticKind::ThermalProcessing;
        }
        if (contains_ascii_case_insensitive(name, "stitch")
            || contains_ascii_case_insensitive(name, "panorama")
            || contains_ascii_case_insensitive(name, "panoramic")) {
            return MetadataQuerySemanticKind::StitchProcessing;
        }
        if (contains_ascii_case_insensitive(name, "deepfusion")
            || contains_ascii_case_insensitive(name, "deep fusion")
            || contains_ascii_case_insensitive(name, "smart hdr")
            || contains_ascii_case_insensitive(name, "hdrplus")
            || contains_ascii_case_insensitive(name, "hdr headroom")
            || contains_ascii_case_insensitive(name, "hdrheadroom")
            || contains_ascii_case_insensitive(name, "pixelshift")
            || contains_ascii_case_insensitive(name, "pixel shift")
            || contains_ascii_case_insensitive(name, "multishot")
            || contains_ascii_case_insensitive(name, "multi shot")
            || contains_ascii_case_insensitive(name, "multiframe")
            || contains_ascii_case_insensitive(name, "multi frame")
            || contains_ascii_case_insensitive(name, "imagefusion")
            || contains_ascii_case_insensitive(name, "image fusion")
            || contains_ascii_case_insensitive(name, "shotlog")
            || contains_ascii_case_insensitive(name, "shot log")
            || contains_ascii_case_insensitive(name, "auto lighting optimizer")
            || contains_ascii_case_insensitive(name, "autolightingoptimizer")) {
            return MetadataQuerySemanticKind::ComputationalProcessing;
        }
        return MetadataQuerySemanticKind::Unknown;
    }

    static MetadataQuerySemanticKind source_processing_semantic_from_groups(
        VendorRawProcessingGroup groups) noexcept
    {
        if (vendor_raw_processing_group_has(groups,
                                            VendorRawProcessingGroup::Thermal)) {
            return MetadataQuerySemanticKind::ThermalProcessing;
        }
        if (vendor_raw_processing_group_has(groups,
                                            VendorRawProcessingGroup::Stitch)) {
            return MetadataQuerySemanticKind::StitchProcessing;
        }
        if (vendor_raw_processing_group_has(
                groups, VendorRawProcessingGroup::Computational)) {
            return MetadataQuerySemanticKind::ComputationalProcessing;
        }
        return MetadataQuerySemanticKind::Unknown;
    }

    static const char* icc_header_field_query_name(uint32_t offset) noexcept
    {
        switch (offset) {
        case kIccHeaderProfileSizeOffset: return "ICCProfileSize";
        case kIccHeaderColorSpaceOffset: return "ICCColorSpace";
        case kIccHeaderPcsOffset: return "ICCProfileConnectionSpace";
        default: break;
        }
        return "ICCProfileHeader";
    }

    static bool png_text_is_color_profile(std::string_view keyword,
                                          std::string_view field) noexcept
    {
        return contains_ascii_case_insensitive(keyword, "icc")
               || contains_ascii_case_insensitive(keyword, "profile")
               || contains_ascii_case_insensitive(keyword, "colorspace")
               || contains_ascii_case_insensitive(field, "icc")
               || contains_ascii_case_insensitive(field, "profile")
               || contains_ascii_case_insensitive(field, "colorspace");
    }

    static uint32_t exact_exif_terms_for_kind(uint16_t tag,
                                              MetadataQueryKind kind) noexcept
    {
        switch (kind) {
        case MetadataQueryKind::Crop:
            switch (tag) {
            case kDngDefaultCropOriginTag:
            case kDngDefaultCropSizeTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Crop);
            case kDngActiveAreaTag:
                return static_cast<uint32_t>(
                    MetadataQueryMatchTerm::ActiveArea);
            case kDngMaskedAreasTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Border);
            default: break;
            }
            return 0U;
        case MetadataQueryKind::ExposureGain:
            switch (tag) {
            case kExifExposureTimeTag:
            case kExifFNumberTag:
            case kExifExposureProgramTag:
            case kExifPhotographicSensitivityTag:
            case kExifShutterSpeedValueTag:
            case kExifApertureValueTag:
            case kExifBrightnessValueTag:
            case kExifMaxApertureValueTag:
            case kExifExposureIndexTag:
            case kDngBaselineExposureTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Exposure);
            case kExifExposureBiasValueTag:
            case kDngBaselineExposureOffsetTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Exposure)
                       | static_cast<uint32_t>(MetadataQueryMatchTerm::Bias);
            case kExifGainControlTag:
            case kDngRawToPreviewGainTag:
            case kDngProfileGainTableMapTag:
            case kDngProfileGainTableMap2Tag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Gain);
            default: break;
            }
            return 0U;
        case MetadataQueryKind::WhiteBalance:
            switch (tag) {
            case kExifLightSourceTag:
            case kExifWhiteBalanceTag:
            case kDngAnalogBalanceTag:
            case kDngAsShotNeutralTag:
            case kDngAsShotWhiteXyTag:
            case kDngCalibrationIlluminant1Tag:
            case kDngCalibrationIlluminant2Tag:
            case kDngCalibrationIlluminant3Tag:
                return static_cast<uint32_t>(
                    MetadataQueryMatchTerm::WhiteBalance);
            default: break;
            }
            return 0U;
        case MetadataQueryKind::Color:
            switch (tag) {
            case kExifColorSpaceTag: return color_profile_terms();
            case kDngColorMatrix1Tag:
            case kDngColorMatrix2Tag:
            case kDngReductionMatrix1Tag:
            case kDngReductionMatrix2Tag:
            case kDngForwardMatrix1Tag:
            case kDngForwardMatrix2Tag:
            case kDngColorMatrix3Tag:
            case kDngForwardMatrix3Tag:
            case kDngReductionMatrix3Tag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Color)
                       | static_cast<uint32_t>(MetadataQueryMatchTerm::Matrix);
            case kDngCameraCalibration1Tag:
            case kDngCameraCalibration2Tag:
            case kDngCameraCalibration3Tag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Color)
                       | static_cast<uint32_t>(
                           MetadataQueryMatchTerm::Calibration);
            case kDngCalibrationIlluminant1Tag:
            case kDngCalibrationIlluminant2Tag:
            case kDngCalibrationIlluminant3Tag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Color);
            default: break;
            }
            return 0U;
        case MetadataQueryKind::LensCorrection:
            switch (tag) {
            case kSamsungVignettingCorrParamsTag:
            case kSamsungChromaticAberrationCorrParamsTag:
            case kSamsungDistortionCorrParamsTag:
            case kDngOpcodeList1Tag:
            case kDngOpcodeList2Tag:
            case kDngOpcodeList3Tag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                       | static_cast<uint32_t>(
                           MetadataQueryMatchTerm::Correction);
            default: break;
            }
            return 0U;
        case MetadataQueryKind::Orientation:
            switch (tag) {
            case kExifOrientationTag:
            case kExifThumbnailOrientationTag:
                return static_cast<uint32_t>(
                    MetadataQueryMatchTerm::Orientation);
            default: break;
            }
            return 0U;
        case MetadataQueryKind::RawProcessing:
            switch (tag) {
            case kDngBlackLevelRepeatDimTag:
            case kDngBlackLevelTag:
            case kDngBlackLevelDeltaHTag:
            case kDngBlackLevelDeltaVTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor)
                       | static_cast<uint32_t>(
                           MetadataQueryMatchTerm::BlackLevel);
            case kDngWhiteLevelTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor)
                       | static_cast<uint32_t>(
                           MetadataQueryMatchTerm::WhiteLevel);
            case kDngLinearizationTableTag:
            case kDngLinearResponseLimitTag:
            case kSonyToneCurveTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor)
                       | static_cast<uint32_t>(
                           MetadataQueryMatchTerm::Linearization);
            case kExifCfaRepeatPatternDimTag:
            case kExifCfaPatternTag:
            case kExifCfaPattern2Tag:
            case kDngCfaPlaneColorTag:
            case kDngCfaLayoutTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor)
                       | static_cast<uint32_t>(MetadataQueryMatchTerm::Cfa);
            case kDngDefaultCropOriginTag:
            case kDngDefaultCropSizeTag:
            case kDngActiveAreaTag:
            case kDngMaskedAreasTag:
            case kDngDefaultScaleTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor)
                       | static_cast<uint32_t>(MetadataQueryMatchTerm::Size);
            case kDngRawDataUniqueIdTag:
            case kDngOriginalRawFileNameTag:
            case kDngOriginalRawFileDataTag:
            case kDngRawImageDigestTag:
            case kDngOriginalRawFileDigestTag:
            case kDngNewRawImageDigestTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Raw)
                       | static_cast<uint32_t>(MetadataQueryMatchTerm::Storage);
            default: break;
            }
            return 0U;
        case MetadataQueryKind::Descriptive:
            switch (tag) {
            case kExifDocumentNameTag:
            case kExifXpTitleTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Title);
            case kExifImageDescriptionTag:
            case kExifXpCommentTag:
                return static_cast<uint32_t>(
                    MetadataQueryMatchTerm::Description);
            case kExifArtistTag:
            case kExifXpAuthorTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Creator);
            case kExifXpKeywordsTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Keywords);
            default: break;
            }
            return 0U;
        }
        return 0U;
    }

    static uint32_t vendor_terms_for_kind(VendorRawProcessingGroup groups,
                                          MetadataQueryKind kind) noexcept
    {
        uint32_t terms = 0U;
        switch (kind) {
        case MetadataQueryKind::Crop:
            if (vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::Geometry)) {
                terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor);
            }
            break;
        case MetadataQueryKind::ExposureGain: break;
        case MetadataQueryKind::WhiteBalance:
            if (vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::WhiteBalance)) {
                terms |= static_cast<uint32_t>(
                    MetadataQueryMatchTerm::WhiteBalance);
            }
            break;
        case MetadataQueryKind::Color:
            if (vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::Color)) {
                terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Color);
            }
            break;
        case MetadataQueryKind::LensCorrection:
            if (vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::LensCorrection)) {
                terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                         | static_cast<uint32_t>(
                             MetadataQueryMatchTerm::Correction);
            }
            break;
        case MetadataQueryKind::Orientation: break;
        case MetadataQueryKind::RawProcessing:
            if (vendor_raw_processing_group_has(groups,
                                                VendorRawProcessingGroup::Sensor)
                || vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::Geometry)) {
                terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor);
            }
            if (vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::RawData)) {
                terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Raw);
            }
            if (vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::Storage)) {
                terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Storage);
            }
            if (vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::PrivateTable)
                || vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::Preview)
                || vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::FaceGeometry)
                || vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::Computational)
                || vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::Thermal)
                || vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::Stitch)) {
                terms |= static_cast<uint32_t>(
                    MetadataQueryMatchTerm::SourceProcessing);
            }
            break;
        case MetadataQueryKind::Descriptive: break;
        }
        return terms;
    }

    static uint32_t
    match_terms_for_kind(std::string_view name, std::string_view group,
                         MetadataQueryKind kind, bool enable_fuzzy,
                         MatchProvenanceState* provenance) noexcept
    {
        switch (kind) {
        case MetadataQueryKind::Crop:
            return crop_match_terms(name, group, enable_fuzzy, provenance);
        case MetadataQueryKind::ExposureGain:
            return exposure_gain_match_terms(name, enable_fuzzy, provenance);
        case MetadataQueryKind::WhiteBalance:
            return white_balance_match_terms(name, enable_fuzzy, provenance);
        case MetadataQueryKind::Color:
            return color_match_terms(name, enable_fuzzy, provenance);
        case MetadataQueryKind::LensCorrection:
            return lens_correction_match_terms(name, enable_fuzzy, provenance);
        case MetadataQueryKind::Orientation:
            return orientation_match_terms(name, enable_fuzzy, provenance);
        case MetadataQueryKind::RawProcessing:
            return raw_processing_match_terms(name, group, enable_fuzzy,
                                              provenance);
        case MetadataQueryKind::Descriptive:
            return descriptive_match_terms(name, enable_fuzzy, provenance);
        }
        return 0U;
    }

    static MetadataQuerySemanticKind
    crop_semantic_from_terms(uint32_t terms) noexcept
    {
        if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::ActiveArea))
            != 0U) {
            return MetadataQuerySemanticKind::ActiveArea;
        }
        if ((terms
             & (static_cast<uint32_t>(MetadataQueryMatchTerm::Border)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Margin)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Padding)))
            != 0U) {
            return MetadataQuerySemanticKind::Border;
        }
        if ((terms
             & (static_cast<uint32_t>(MetadataQueryMatchTerm::Crop)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Origin)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Size)))
            != 0U) {
            return MetadataQuerySemanticKind::Crop;
        }
        return MetadataQuerySemanticKind::Unknown;
    }

    static MetadataQuerySemanticKind
    semantic_from_terms(MetadataQueryKind kind, uint32_t terms) noexcept
    {
        switch (kind) {
        case MetadataQueryKind::Crop: return crop_semantic_from_terms(terms);
        case MetadataQueryKind::ExposureGain:
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Gain))
                != 0U) {
                return MetadataQuerySemanticKind::Gain;
            }
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Exposure)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Bias)))
                != 0U) {
                return MetadataQuerySemanticKind::Exposure;
            }
            break;
        case MetadataQueryKind::WhiteBalance:
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::WhiteBalance))
                != 0U) {
                return MetadataQuerySemanticKind::WhiteBalance;
            }
            break;
        case MetadataQueryKind::Color:
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Matrix)
                    | static_cast<uint32_t>(
                        MetadataQueryMatchTerm::Calibration)))
                != 0U) {
                return MetadataQuerySemanticKind::ColorMatrix;
            }
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Color)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Profile)))
                != 0U) {
                return MetadataQuerySemanticKind::Color;
            }
            break;
        case MetadataQueryKind::LensCorrection:
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Correction)))
                != 0U) {
                return MetadataQuerySemanticKind::LensCorrection;
            }
            break;
        case MetadataQueryKind::Orientation:
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::Orientation))
                != 0U) {
                return MetadataQuerySemanticKind::Orientation;
            }
            break;
        case MetadataQueryKind::RawProcessing:
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::BlackLevel))
                != 0U) {
                return MetadataQuerySemanticKind::BlackLevel;
            }
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::WhiteLevel))
                != 0U) {
                return MetadataQuerySemanticKind::WhiteLevel;
            }
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::Linearization))
                != 0U) {
                return MetadataQuerySemanticKind::Linearization;
            }
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Cfa))
                != 0U) {
                return MetadataQuerySemanticKind::CfaLayout;
            }
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Raw)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Storage)))
                != 0U) {
                return MetadataQuerySemanticKind::RawStorage;
            }
            if ((terms
                 & static_cast<uint32_t>(
                     MetadataQueryMatchTerm::SourceProcessing))
                != 0U) {
                return MetadataQuerySemanticKind::SourceProcessing;
            }
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor))
                != 0U) {
                return MetadataQuerySemanticKind::SensorGeometry;
            }
            break;
        case MetadataQueryKind::Descriptive:
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Title))
                != 0U) {
                return MetadataQuerySemanticKind::Title;
            }
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::Description))
                != 0U) {
                return MetadataQuerySemanticKind::Description;
            }
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Creator))
                != 0U) {
                return MetadataQuerySemanticKind::Creator;
            }
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Keywords))
                != 0U) {
                return MetadataQuerySemanticKind::Keywords;
            }
            break;
        }
        return MetadataQuerySemanticKind::Unknown;
    }

    static uint8_t crop_confidence_from_terms(uint32_t terms) noexcept
    {
        if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Crop))
            != 0U) {
            return 90U;
        }
        if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::ActiveArea))
            != 0U) {
            return 88U;
        }
        if ((terms
             & (static_cast<uint32_t>(MetadataQueryMatchTerm::Border)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Margin)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Padding)))
            != 0U) {
            return 70U;
        }
        if ((terms
             & (static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Image)))
            != 0U) {
            return 45U;
        }
        return 0U;
    }

    static uint8_t confidence_from_terms(MetadataQueryKind kind,
                                         uint32_t terms) noexcept
    {
        switch (kind) {
        case MetadataQueryKind::Crop: return crop_confidence_from_terms(terms);
        case MetadataQueryKind::ExposureGain:
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Exposure)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Gain)))
                != 0U) {
                return 90U;
            }
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Bias))
                != 0U) {
                return 82U;
            }
            break;
        case MetadataQueryKind::WhiteBalance:
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::WhiteBalance))
                != 0U) {
                return 90U;
            }
            break;
        case MetadataQueryKind::Color:
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Matrix))
                != 0U) {
                return 94U;
            }
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::Calibration))
                != 0U) {
                return 88U;
            }
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Profile))
                != 0U) {
                return 88U;
            }
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Color))
                != 0U) {
                return 70U;
            }
            break;
        case MetadataQueryKind::LensCorrection:
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Correction)))
                == (static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                    | static_cast<uint32_t>(
                        MetadataQueryMatchTerm::Correction))) {
                return 92U;
            }
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Correction)))
                != 0U) {
                return 68U;
            }
            break;
        case MetadataQueryKind::Orientation:
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::Orientation))
                != 0U) {
                return 95U;
            }
            break;
        case MetadataQueryKind::RawProcessing:
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::BlackLevel)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::WhiteLevel)
                    | static_cast<uint32_t>(
                        MetadataQueryMatchTerm::Linearization)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Cfa)))
                != 0U) {
                return 92U;
            }
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Raw)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Storage)))
                != 0U) {
                return 86U;
            }
            if ((terms
                 & static_cast<uint32_t>(
                     MetadataQueryMatchTerm::SourceProcessing))
                != 0U) {
                return 84U;
            }
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor))
                != 0U) {
                return 78U;
            }
            break;
        case MetadataQueryKind::Descriptive:
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Title)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Description)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Creator)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Keywords)))
                != 0U) {
                return 90U;
            }
            break;
        }
        return 0U;
    }

    static void append_match(MetadataQueryResult* result, EntryId entry_id,
                             const Entry& entry, std::string_view group,
                             std::string_view name, MetadataQueryKind kind,
                             uint32_t terms,
                             const MatchProvenanceState& provenance,
                             MetadataQuerySemanticKind explicit_semantic
                             = MetadataQuerySemanticKind::Unknown)
    {
        if (!result
            || (terms == 0U
                && explicit_semantic == MetadataQuerySemanticKind::Unknown)) {
            return;
        }
        MetadataQueryMatch match;
        match.entry_id = entry_id;
        match.key_kind = entry.key.kind;
        match.semantic = explicit_semantic != MetadataQuerySemanticKind::Unknown
                             ? explicit_semantic
                             : semantic_from_terms(kind, terms);
        match.shape    = value_shape(entry.value);
        match.confidence    = terms == 0U ? 90U
                                          : confidence_from_terms(kind, terms);
        match.matched_terms = terms;
        match.exact_match   = provenance.exact_match;
        match.fuzzy_match   = provenance.fuzzy_match;
        match.fuzzy_score   = provenance.fuzzy_score;
        if (entry.key.kind == MetaKeyKind::ExifTag) {
            match.exif_tag = entry.key.data.exif_tag.tag;
        }
        match.group.assign(group.data(), group.size());
        match.name.assign(name.data(), name.size());
        result->matches.push_back(match);
    }

    static void append_exif_match_if_relevant(const MetaStore& store,
                                              MetadataQueryResult* result,
                                              EntryId entry_id,
                                              const Entry& entry,
                                              MetadataQueryKind kind)
    {
        const std::string_view ifd = arena_string(store.arena(),
                                                  entry.key.data.exif_tag.ifd);
        const std::string_view name
            = exif_entry_name(store, entry, ExifTagNamePolicy::ExifToolCompat);
        MatchProvenanceState provenance;
        uint32_t terms = match_terms_for_kind(name, ifd, kind, false,
                                              &provenance);
        const uint32_t exact_exif_terms
            = exact_exif_terms_for_kind(entry.key.data.exif_tag.tag, kind);
        if (exact_exif_terms != 0U) {
            note_exact_match(&provenance);
            terms |= exact_exif_terms;
        }
        const VendorRawProcessingGroup groups
            = classify_vendor_raw_processing_field(ifd, name,
                                                   entry.key.data.exif_tag.tag);
        const uint32_t vendor_terms = vendor_terms_for_kind(groups, kind);
        if (vendor_terms != 0U) {
            note_exact_match(&provenance);
            terms |= vendor_terms;
        }
        if (kind == MetadataQueryKind::LensCorrection
            && name_is_af_micro_adjustment(ifd)) {
            note_exact_match(&provenance);
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                     | static_cast<uint32_t>(
                         MetadataQueryMatchTerm::Correction);
        }
        if (kind == MetadataQueryKind::RawProcessing
            && contains_ascii_case_insensitive(ifd, "ambience")) {
            note_exact_match(&provenance);
            terms |= static_cast<uint32_t>(
                MetadataQueryMatchTerm::SourceProcessing);
        }
        if (kind == MetadataQueryKind::Color
            && exif_group_is_canon_colordata_source_transform(ifd)) {
            note_exact_match(&provenance);
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Color);
        }
        if (kind == MetadataQueryKind::RawProcessing
            && exif_group_is_nikonsettings_source_processing(ifd)) {
            note_exact_match(&provenance);
            terms |= static_cast<uint32_t>(
                MetadataQueryMatchTerm::SourceProcessing);
        }
        MetadataQuerySemanticKind explicit_semantic
            = MetadataQuerySemanticKind::Unknown;
        if (kind == MetadataQueryKind::Descriptive
            && entry.key.data.exif_tag.tag == kExifCopyrightTag) {
            note_exact_match(&provenance);
            explicit_semantic = MetadataQuerySemanticKind::Rights;
        }
        if (kind == MetadataQueryKind::Color
            && entry.key.data.exif_tag.tag == kExifColorSpaceTag) {
            explicit_semantic = MetadataQuerySemanticKind::ColorProfile;
        } else if (kind == MetadataQueryKind::Color
                   && ((vendor_raw_processing_group_has(
                            groups, VendorRawProcessingGroup::Color)
                        && (terms
                            & (static_cast<uint32_t>(
                                   MetadataQueryMatchTerm::Matrix)
                               | static_cast<uint32_t>(
                                   MetadataQueryMatchTerm::Calibration)))
                               == 0U)
                       || color_name_is_source_transform(name)
                       || exif_group_is_canon_colordata_source_transform(ifd))) {
            explicit_semantic = MetadataQuerySemanticKind::SourceColorTransform;
        } else if (kind == MetadataQueryKind::RawProcessing
                   && semantic_from_terms(kind, terms)
                          == MetadataQuerySemanticKind::SourceProcessing) {
            explicit_semantic = source_processing_semantic_from_groups(groups);
            if (explicit_semantic == MetadataQuerySemanticKind::Unknown) {
                explicit_semantic = source_processing_semantic_from_name(name);
            }
        } else if (kind == MetadataQueryKind::RawProcessing
                   && (terms
                       & static_cast<uint32_t>(
                           MetadataQueryMatchTerm::Linearization))
                          != 0U) {
            explicit_semantic
                = raw_curve_semantic_from_name(ifd, name,
                                               entry.key.data.exif_tag.tag);
        }
        append_match(result, entry_id, entry, ifd, name, kind, terms,
                     provenance, explicit_semantic);
    }

    static void append_xmp_match_if_relevant(const MetaStore& store,
                                             MetadataQueryResult* result,
                                             EntryId entry_id,
                                             const Entry& entry,
                                             MetadataQueryKind kind)
    {
        const std::string_view ns
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.schema_ns);
        const std::string_view path
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.property_path);
        MatchProvenanceState provenance;
        uint32_t terms = 0U;
        if (kind == MetadataQueryKind::Descriptive) {
            terms = xmp_descriptive_terms(path, &provenance);
        } else {
            terms = match_terms_for_kind(path, ns, kind, true, &provenance);
        }
        MetadataQuerySemanticKind explicit_semantic
            = MetadataQuerySemanticKind::Unknown;
        if (kind == MetadataQueryKind::Descriptive) {
            explicit_semantic = xmp_descriptive_exact_semantic(ns, path);
            if (explicit_semantic != MetadataQuerySemanticKind::Unknown) {
                note_exact_match(&provenance);
            }
        } else if (kind == MetadataQueryKind::Color) {
            const uint32_t profile_terms = xmp_color_profile_terms(path,
                                                                   &provenance);
            if (profile_terms != 0U) {
                terms |= profile_terms;
                explicit_semantic = MetadataQuerySemanticKind::ColorProfile;
            } else {
                const uint32_t source_terms
                    = xmp_source_color_transform_terms(ns, path, &provenance);
                if (source_terms != 0U) {
                    terms |= source_terms;
                    explicit_semantic
                        = MetadataQuerySemanticKind::SourceColorTransform;
                }
            }
        } else if (kind == MetadataQueryKind::RawProcessing
                   && semantic_from_terms(kind, terms)
                          == MetadataQuerySemanticKind::SourceProcessing) {
            explicit_semantic = source_processing_semantic_from_name(path);
        }
        append_match(result, entry_id, entry, ns, path, kind, terms, provenance,
                     explicit_semantic);
    }

    static void append_iptc_match_if_relevant(MetadataQueryResult* result,
                                              EntryId entry_id,
                                              const Entry& entry,
                                              MetadataQueryKind kind)
    {
        const uint16_t record  = entry.key.data.iptc_dataset.record;
        const uint16_t dataset = entry.key.data.iptc_dataset.dataset;
        const uint32_t terms = exact_iptc_terms_for_kind(record, dataset, kind);
        const MetadataQuerySemanticKind explicit_semantic
            = kind == MetadataQueryKind::Descriptive
                  ? iptc_descriptive_exact_semantic(record, dataset)
                  : MetadataQuerySemanticKind::Unknown;
        if (terms == 0U
            && explicit_semantic == MetadataQuerySemanticKind::Unknown) {
            return;
        }
        MatchProvenanceState provenance;
        note_exact_match(&provenance);
        append_match(result, entry_id, entry, "iptc",
                     iptc_descriptive_dataset_name(record, dataset), kind,
                     terms, provenance, explicit_semantic);
    }

    static void append_icc_match_if_relevant(MetadataQueryResult* result,
                                             EntryId entry_id,
                                             const Entry& entry,
                                             MetadataQueryKind kind)
    {
        if (kind != MetadataQueryKind::Color) {
            return;
        }
        MatchProvenanceState provenance;
        note_exact_match(&provenance);
        if (entry.key.kind == MetaKeyKind::IccHeaderField) {
            append_match(result, entry_id, entry, "icc",
                         icc_header_field_query_name(
                             entry.key.data.icc_header_field.offset),
                         kind, color_profile_terms(), provenance,
                         MetadataQuerySemanticKind::ColorProfile);
            return;
        }
        append_match(result, entry_id, entry, "icc", "ICCProfileTag", kind,
                     color_profile_terms(), provenance,
                     MetadataQuerySemanticKind::ColorProfile);
    }

    static void append_png_text_match_if_relevant(const MetaStore& store,
                                                  MetadataQueryResult* result,
                                                  EntryId entry_id,
                                                  const Entry& entry,
                                                  MetadataQueryKind kind)
    {
        if (kind != MetadataQueryKind::Color) {
            return;
        }
        const std::string_view keyword
            = arena_string(store.arena(), entry.key.data.png_text.keyword);
        const std::string_view field
            = arena_string(store.arena(), entry.key.data.png_text.field);
        if (!png_text_is_color_profile(keyword, field)) {
            return;
        }
        MatchProvenanceState provenance;
        note_exact_match(&provenance);
        append_match(result, entry_id, entry, "png_text", keyword, kind,
                     color_profile_terms(), provenance,
                     MetadataQuerySemanticKind::ColorProfile);
    }

    static bool exif_entry_is(const MetaStore& store, const Entry& entry,
                              std::string_view ifd, uint16_t tag) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag
            || entry.key.data.exif_tag.tag != tag) {
            return false;
        }
        return arena_string(store.arena(), entry.key.data.exif_tag.ifd) == ifd;
    }

    static EntryId find_first_exif_entry(const MetaStore& store,
                                         std::string_view ifd,
                                         uint16_t tag) noexcept
    {
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            if (entry_is_deleted(entries[i])) {
                continue;
            }
            if (exif_entry_is(store, entries[i], ifd, tag)) {
                return static_cast<EntryId>(i);
            }
        }
        return kInvalidEntryId;
    }

    static EntryId find_first_exif_tag_any_ifd(const MetaStore& store,
                                               uint16_t tag) noexcept
    {
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            if (entry_is_deleted(entries[i])
                || entries[i].key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            if (entries[i].key.data.exif_tag.tag == tag) {
                return static_cast<EntryId>(i);
            }
        }
        return kInvalidEntryId;
    }

    static bool has_previous_exif_tag_series_ifd(const MetaStore& store,
                                                 size_t entry_index,
                                                 std::string_view ifd,
                                                 const uint16_t* tags,
                                                 size_t tag_count) noexcept
    {
        const std::span<const Entry> entries = store.entries();
        if (entry_index > entries.size()) {
            return false;
        }
        for (size_t i = 0U; i < entry_index; ++i) {
            if (entry_is_deleted(entries[i])
                || !exif_entry_in_tag_series(entries[i], tags, tag_count)) {
                continue;
            }
            const std::string_view current_ifd
                = arena_string(store.arena(), entries[i].key.data.exif_tag.ifd);
            if (current_ifd == ifd) {
                return true;
            }
        }
        return false;
    }

    static bool append_candidate_numeric_values_min(const MetaStore& store,
                                                    EntryId entry_id,
                                                    MetadataQueryCandidate* out,
                                                    uint32_t min_count)
    {
        if (!out || entry_id == kInvalidEntryId) {
            return false;
        }
        double values[64] {};
        uint32_t count = 0U;
        if (!value_to_double_array(store, store.entry(entry_id).value, values,
                                   64U, &count)) {
            return min_count == 0U;
        }
        if (count < min_count) {
            return false;
        }
        if (count == 0U) {
            return true;
        }
        out->has_values = true;
        out->values.reserve(out->values.size() + static_cast<size_t>(count));
        for (uint32_t i = 0U; i < count; ++i) {
            out->values.push_back(values[i]);
        }
        return true;
    }

    static void append_exif_tag_series_candidates(
        const MetaStore& store, MetadataQueryResult* result,
        const uint16_t* tags, size_t tag_count,
        MetadataQuerySemanticKind semantic, MetadataQueryValueShape shape,
        uint8_t confidence, uint32_t min_values_per_entry = 0U)
    {
        if (!result || !tags || tag_count == 0U) {
            return;
        }

        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (entry_is_deleted(entry)
                || !exif_entry_in_tag_series(entry, tags, tag_count)) {
                continue;
            }

            const std::string_view ifd
                = arena_string(store.arena(), entry.key.data.exif_tag.ifd);
            if (has_previous_exif_tag_series_ifd(store, i, ifd, tags,
                                                 tag_count)) {
                continue;
            }

            MetadataQueryCandidate candidate;
            candidate.semantic         = semantic;
            candidate.normalized_shape = shape;
            candidate.confidence       = confidence;
            candidate.source_entries.reserve(tag_count);

            for (size_t tag_index = 0U; tag_index < tag_count; ++tag_index) {
                const EntryId entry_id = find_first_exif_entry(store, ifd,
                                                               tags[tag_index]);
                if (entry_id == kInvalidEntryId) {
                    continue;
                }
                if (!append_candidate_numeric_values_min(
                        store, entry_id, &candidate, min_values_per_entry)) {
                    continue;
                }
                append_unique_entry(&candidate.source_entries, entry_id);
            }

            if (candidate.source_entries.size() >= 2U) {
                result->candidates.push_back(candidate);
            }
        }
    }

    static std::string_view
    vendor_raw_processing_family_group_key(std::string_view group) noexcept
    {
        if (starts_with_ascii_case_insensitive(group, "mk_sony")) {
            return "mk_sony";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_canon")) {
            return "mk_canon";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_nikon")) {
            return "mk_nikon";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_fuji")) {
            return "mk_fuji";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_pentax")) {
            return "mk_pentax";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_panasonic")) {
            return "mk_panasonic";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_olympus")) {
            return "mk_olympus";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_kodak")) {
            return "mk_kodak";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_minolta")) {
            return "mk_minolta";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_sigma")) {
            return "mk_sigma";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_samsung")) {
            return "mk_samsung";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_ricoh")) {
            return "mk_ricoh";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_apple")) {
            return "mk_apple";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_dji")) {
            return "mk_dji";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_google")) {
            return "mk_google";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_flir")) {
            return "mk_flir";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_casio")) {
            return "mk_casio";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_sanyo")) {
            return "mk_sanyo";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_kyoceraraw")) {
            return "mk_kyoceraraw";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_kyocera")) {
            return "mk_kyocera";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_reconyx")) {
            return "mk_reconyx";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_hp")) {
            return "mk_hp";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_jvc")) {
            return "mk_jvc";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_ge")) {
            return "mk_ge";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_motorola")) {
            return "mk_motorola";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_nintendo")) {
            return "mk_nintendo";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_microsoft")) {
            return "mk_microsoft";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_phaseone")) {
            return "mk_phaseone";
        }
        if (starts_with_ascii_case_insensitive(group, "mk_leaf")) {
            return "mk_leaf";
        }
        if (starts_with_ascii_case_insensitive(group, "raf_")
            || starts_with_ascii_case_insensitive(group, "raf")) {
            return "raf";
        }
        if (starts_with_ascii_case_insensitive(group, "x3f_")
            || starts_with_ascii_case_insensitive(group, "x3f")) {
            return "x3f";
        }
        return {};
    }

    static std::string_view
    lens_correction_group_key(std::string_view group) noexcept
    {
        const std::string_view vendor_key
            = vendor_raw_processing_family_group_key(group);
        if (!vendor_key.empty()) {
            return vendor_key;
        }
        return group;
    }

    static bool
    lens_correction_match_can_group(const MetadataQueryMatch& match) noexcept
    {
        return match.entry_id != kInvalidEntryId
               && match.key_kind == MetaKeyKind::ExifTag
               && match.semantic == MetadataQuerySemanticKind::LensCorrection;
    }

    static bool
    has_previous_lens_correction_group(const MetadataQueryResult& result,
                                       size_t match_index,
                                       std::string_view group_key) noexcept
    {
        if (match_index > result.matches.size()) {
            return false;
        }
        for (size_t i = 0U; i < match_index; ++i) {
            if (!lens_correction_match_can_group(result.matches[i])) {
                continue;
            }
            const std::string_view current_key = lens_correction_group_key(
                result.matches[i].group);
            if (current_key == group_key) {
                return true;
            }
        }
        return false;
    }

    static void
    append_lens_correction_table_candidates(const MetaStore& store,
                                            MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }

        for (size_t i = 0U; i < result->matches.size(); ++i) {
            const MetadataQueryMatch& match = result->matches[i];
            if (!lens_correction_match_can_group(match)) {
                continue;
            }

            const std::string_view group_key = lens_correction_group_key(
                match.group);
            if (has_previous_lens_correction_group(*result, i, group_key)) {
                continue;
            }

            MetadataQueryCandidate candidate;
            candidate.semantic = MetadataQuerySemanticKind::LensCorrection;
            candidate.normalized_shape = MetadataQueryValueShape::Table;

            for (size_t j = i; j < result->matches.size(); ++j) {
                const MetadataQueryMatch& current = result->matches[j];
                if (!lens_correction_match_can_group(current)) {
                    continue;
                }
                const std::string_view current_key = lens_correction_group_key(
                    current.group);
                if (current_key != group_key) {
                    continue;
                }
                if (!append_candidate_numeric_values_min(store,
                                                         current.entry_id,
                                                         &candidate, 1U)) {
                    continue;
                }
                append_unique_entry(&candidate.source_entries,
                                    current.entry_id);
                if (candidate.confidence < current.confidence) {
                    candidate.confidence = current.confidence;
                }
            }

            if (candidate.source_entries.size() >= 2U) {
                result->candidates.push_back(candidate);
            }
        }
    }

    static bool vendor_match_can_group(const MetadataQueryMatch& match,
                                       MetadataQueryKind kind) noexcept
    {
        if (match.entry_id == kInvalidEntryId
            || match.key_kind != MetaKeyKind::ExifTag
            || match.semantic == MetadataQuerySemanticKind::Unknown) {
            return false;
        }
        if (vendor_raw_processing_family_group_key(match.group).empty()) {
            return false;
        }
        switch (kind) {
        case MetadataQueryKind::WhiteBalance:
            return match.semantic == MetadataQuerySemanticKind::WhiteBalance;
        case MetadataQueryKind::Color:
            return match.semantic == MetadataQuerySemanticKind::Color
                   || match.semantic == MetadataQuerySemanticKind::ColorMatrix
                   || match.semantic
                          == MetadataQuerySemanticKind::SourceColorTransform;
        case MetadataQueryKind::RawProcessing:
            return match.semantic == MetadataQuerySemanticKind::BlackLevel
                   || match.semantic == MetadataQuerySemanticKind::WhiteLevel
                   || match.semantic == MetadataQuerySemanticKind::Linearization
                   || match.semantic == MetadataQuerySemanticKind::RawValueCurve
                   || match.semantic
                          == MetadataQuerySemanticKind::RawLinearityLimit
                   || match.semantic
                          == MetadataQuerySemanticKind::RawCalibrationCurve
                   || match.semantic
                          == MetadataQuerySemanticKind::RawCurveControlPoints
                   || match.semantic == MetadataQuerySemanticKind::CfaLayout
                   || match.semantic
                          == MetadataQuerySemanticKind::SensorGeometry
                   || match.semantic == MetadataQuerySemanticKind::RawStorage
                   || match.semantic
                          == MetadataQuerySemanticKind::SourceProcessing
                   || match.semantic
                          == MetadataQuerySemanticKind::ComputationalProcessing
                   || match.semantic
                          == MetadataQuerySemanticKind::ThermalProcessing
                   || match.semantic
                          == MetadataQuerySemanticKind::StitchProcessing;
        case MetadataQueryKind::Crop:
        case MetadataQueryKind::ExposureGain:
        case MetadataQueryKind::LensCorrection:
        case MetadataQueryKind::Orientation:
        case MetadataQueryKind::Descriptive: break;
        }
        return false;
    }

    static MetadataQueryValueShape
    vendor_group_candidate_shape(MetadataQueryKind kind,
                                 MetadataQuerySemanticKind semantic) noexcept
    {
        switch (kind) {
        case MetadataQueryKind::WhiteBalance:
            return MetadataQueryValueShape::VectorSet;
        case MetadataQueryKind::Color:
            if (semantic == MetadataQuerySemanticKind::ColorMatrix) {
                return MetadataQueryValueShape::MatrixSet;
            }
            return MetadataQueryValueShape::Table;
        case MetadataQueryKind::RawProcessing:
            return MetadataQueryValueShape::Table;
        case MetadataQueryKind::Crop:
        case MetadataQueryKind::ExposureGain:
        case MetadataQueryKind::LensCorrection:
        case MetadataQueryKind::Orientation:
        case MetadataQueryKind::Descriptive: break;
        }
        return MetadataQueryValueShape::Table;
    }

    static uint32_t
    grouped_candidate_min_numeric_values(MetadataQueryValueShape shape) noexcept
    {
        if (shape == MetadataQueryValueShape::MatrixSet) {
            return 9U;
        }
        if (shape == MetadataQueryValueShape::VectorSet) {
            return 2U;
        }
        return 0U;
    }

    static bool has_previous_vendor_semantic_group(
        const MetadataQueryResult& result, size_t match_index,
        MetadataQueryKind kind, std::string_view group_key,
        MetadataQuerySemanticKind semantic) noexcept
    {
        if (match_index > result.matches.size()) {
            return false;
        }
        for (size_t i = 0U; i < match_index; ++i) {
            const MetadataQueryMatch& current = result.matches[i];
            if (!vendor_match_can_group(current, kind)
                || current.semantic != semantic) {
                continue;
            }
            const std::string_view current_key
                = vendor_raw_processing_family_group_key(current.group);
            if (current_key == group_key) {
                return true;
            }
        }
        return false;
    }

    static void
    append_vendor_grouped_query_candidates(const MetaStore& store,
                                           MetadataQueryResult* result,
                                           MetadataQueryKind kind)
    {
        if (!result) {
            return;
        }
        for (size_t i = 0U; i < result->matches.size(); ++i) {
            const MetadataQueryMatch& match = result->matches[i];
            if (!vendor_match_can_group(match, kind)) {
                continue;
            }
            const std::string_view group_key
                = vendor_raw_processing_family_group_key(match.group);
            if (has_previous_vendor_semantic_group(*result, i, kind, group_key,
                                                   match.semantic)) {
                continue;
            }

            MetadataQueryCandidate candidate;
            candidate.semantic = match.semantic;
            candidate.normalized_shape
                = vendor_group_candidate_shape(kind, match.semantic);
            const uint32_t min_values = grouped_candidate_min_numeric_values(
                candidate.normalized_shape);

            for (size_t j = i; j < result->matches.size(); ++j) {
                const MetadataQueryMatch& current = result->matches[j];
                if (!vendor_match_can_group(current, kind)
                    || current.semantic != match.semantic) {
                    continue;
                }
                const std::string_view current_key
                    = vendor_raw_processing_family_group_key(current.group);
                if (current_key != group_key) {
                    continue;
                }
                if (!append_candidate_numeric_values_min(
                        store, current.entry_id, &candidate, min_values)) {
                    continue;
                }
                append_unique_entry(&candidate.source_entries,
                                    current.entry_id);
                if (candidate.confidence < current.confidence) {
                    candidate.confidence = current.confidence;
                }
            }

            if (candidate.source_entries.size() >= 2U) {
                result->candidates.push_back(candidate);
            }
        }
    }

    static void append_default_crop_candidate(const MetaStore& store,
                                              MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }

        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& origin_entry = entries[i];
            if (entry_is_deleted(origin_entry)
                || origin_entry.key.kind != MetaKeyKind::ExifTag
                || origin_entry.key.data.exif_tag.tag
                       != kDngDefaultCropOriginTag) {
                continue;
            }
            const std::string_view ifd
                = arena_string(store.arena(),
                               origin_entry.key.data.exif_tag.ifd);
            const EntryId origin_id = static_cast<EntryId>(i);
            const EntryId size_id
                = find_first_exif_entry(store, ifd, kDngDefaultCropSizeTag);
            if (size_id == kInvalidEntryId) {
                continue;
            }

            double origin_values[4] {};
            double size_values[4] {};
            uint32_t origin_count = 0U;
            uint32_t size_count   = 0U;
            if (!value_to_double_array(store, store.entry(origin_id).value,
                                       origin_values, 4U, &origin_count)
                || !value_to_double_array(store, store.entry(size_id).value,
                                          size_values, 4U, &size_count)
                || origin_count < 2U || size_count < 2U) {
                continue;
            }

            MetadataQueryCandidate candidate;
            candidate.semantic         = MetadataQuerySemanticKind::Crop;
            candidate.normalized_shape = MetadataQueryValueShape::Rect;
            candidate.confidence       = 95U;
            append_unique_entry(&candidate.source_entries, origin_id);
            append_unique_entry(&candidate.source_entries, size_id);
            candidate.has_origin = true;
            candidate.origin[0]  = origin_values[0];
            candidate.origin[1]  = origin_values[1];
            candidate.has_size   = true;
            candidate.size[0]    = size_values[0];
            candidate.size[1]    = size_values[1];
            candidate.has_rect   = true;
            candidate.rect[0]    = origin_values[0];
            candidate.rect[1]    = origin_values[1];
            candidate.rect[2]    = size_values[0];
            candidate.rect[3]    = size_values[1];
            result->candidates.push_back(candidate);
        }
    }

    static void append_active_area_candidate(const MetaStore& store,
                                             MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }
        const EntryId active_id
            = find_first_exif_tag_any_ifd(store, kDngActiveAreaTag);
        if (active_id == kInvalidEntryId) {
            return;
        }
        double values[8] {};
        uint32_t count = 0U;
        if (!value_to_double_array(store, store.entry(active_id).value, values,
                                   8U, &count)
            || count < 4U) {
            return;
        }
        const double top    = values[0];
        const double left   = values[1];
        const double bottom = values[2];
        const double right  = values[3];
        if (right < left || bottom < top) {
            return;
        }

        MetadataQueryCandidate candidate;
        candidate.semantic         = MetadataQuerySemanticKind::ActiveArea;
        candidate.normalized_shape = MetadataQueryValueShape::Rect;
        candidate.confidence       = 92U;
        append_unique_entry(&candidate.source_entries, active_id);
        candidate.has_origin = true;
        candidate.origin[0]  = left;
        candidate.origin[1]  = top;
        candidate.has_size   = true;
        candidate.size[0]    = right - left;
        candidate.size[1]    = bottom - top;
        candidate.has_rect   = true;
        candidate.rect[0]    = left;
        candidate.rect[1]    = top;
        candidate.rect[2]    = right - left;
        candidate.rect[3]    = bottom - top;
        result->candidates.push_back(candidate);
    }

    static void append_phaseone_crop_candidate(const MetaStore& store,
                                               MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }
        const PhaseOneRawGeometryResult geometry
            = phaseone_raw_geometry_from_store(store);
        if (geometry.status != PhaseOneRawGeometryStatus::Ok) {
            return;
        }
        MetadataQueryCandidate candidate;
        candidate.semantic         = MetadataQuerySemanticKind::ActiveArea;
        candidate.normalized_shape = MetadataQueryValueShape::Rect;
        candidate.confidence       = 96U;
        append_unique_entry(&candidate.source_entries,
                            find_first_exif_entry(store, kPhaseOneMainIfd,
                                                  kPhaseOneSensorWidthTag));
        append_unique_entry(&candidate.source_entries,
                            find_first_exif_entry(store, kPhaseOneMainIfd,
                                                  kPhaseOneSensorHeightTag));
        append_unique_entry(&candidate.source_entries,
                            find_first_exif_entry(store, kPhaseOneMainIfd,
                                                  kPhaseOneSensorLeftMarginTag));
        append_unique_entry(&candidate.source_entries,
                            find_first_exif_entry(store, kPhaseOneMainIfd,
                                                  kPhaseOneSensorTopMarginTag));
        append_unique_entry(&candidate.source_entries,
                            find_first_exif_entry(store, kPhaseOneMainIfd,
                                                  kPhaseOneImageWidthTag));
        append_unique_entry(&candidate.source_entries,
                            find_first_exif_entry(store, kPhaseOneMainIfd,
                                                  kPhaseOneImageHeightTag));
        candidate.has_origin = true;
        candidate.origin[0]  = geometry.geometry.active_x;
        candidate.origin[1]  = geometry.geometry.active_y;
        candidate.has_size   = true;
        candidate.size[0]    = geometry.geometry.active_width;
        candidate.size[1]    = geometry.geometry.active_height;
        candidate.has_rect   = true;
        candidate.rect[0]    = geometry.geometry.active_x;
        candidate.rect[1]    = geometry.geometry.active_y;
        candidate.rect[2]    = geometry.geometry.active_width;
        candidate.rect[3]    = geometry.geometry.active_height;
        candidate.has_values = true;
        candidate.values.reserve(4U);
        candidate.values.push_back(geometry.geometry.sensor_left_margin);
        candidate.values.push_back(geometry.geometry.sensor_top_margin);
        candidate.values.push_back(geometry.geometry.right_margin);
        candidate.values.push_back(geometry.geometry.bottom_margin);
        candidate.has_margins = true;
        candidate.margins[0]  = geometry.geometry.sensor_left_margin;
        candidate.margins[1]  = geometry.geometry.sensor_top_margin;
        candidate.margins[2]  = geometry.geometry.right_margin;
        candidate.margins[3]  = geometry.geometry.bottom_margin;
        result->candidates.push_back(candidate);
    }

    static bool is_fujifilm_raf_data_ifd(std::string_view ifd) noexcept
    {
        if (starts_with_ascii_case_insensitive(ifd, "raf_header")) {
            return false;
        }
        if (starts_with_ascii_case_insensitive(ifd, "raf_")) {
            return true;
        }
        return ifd.size() == 3U
               && starts_with_ascii_case_insensitive(ifd, "raf");
    }

    static bool first_entry_value_to_double(const MetaStore& store, EntryId id,
                                            double* out) noexcept
    {
        if (!out || id == kInvalidEntryId) {
            return false;
        }
        double values[4] {};
        uint32_t count = 0U;
        if (!value_to_double_array(store, store.entry(id).value, values, 4U,
                                   &count)
            || count < 1U) {
            return false;
        }
        *out = values[0];
        return true;
    }

    static void append_origin_size_rect_candidate(
        const MetaStore& store, MetadataQueryResult* result, EntryId origin_id,
        EntryId size_id, EntryId full_size_id,
        MetadataQuerySemanticKind semantic, uint8_t confidence)
    {
        if (!result || origin_id == kInvalidEntryId
            || size_id == kInvalidEntryId) {
            return;
        }

        double origin_values[4] {};
        double size_values[4] {};
        double full_size_values[4] {};
        uint32_t origin_count    = 0U;
        uint32_t size_count      = 0U;
        uint32_t full_size_count = 0U;
        if (!value_to_double_array(store, store.entry(origin_id).value,
                                   origin_values, 4U, &origin_count)
            || !value_to_double_array(store, store.entry(size_id).value,
                                      size_values, 4U, &size_count)
            || origin_count < 2U || size_count < 2U) {
            return;
        }
        if (origin_values[0] < 0.0 || origin_values[1] < 0.0
            || size_values[0] <= 0.0 || size_values[1] <= 0.0) {
            return;
        }

        bool has_full_size = false;
        if (full_size_id != kInvalidEntryId) {
            has_full_size
                = value_to_double_array(store, store.entry(full_size_id).value,
                                        full_size_values, 4U, &full_size_count)
                  && full_size_count >= 2U && full_size_values[0] > 0.0
                  && full_size_values[1] > 0.0;
        }

        MetadataQueryCandidate candidate;
        candidate.semantic         = semantic;
        candidate.normalized_shape = MetadataQueryValueShape::Rect;
        candidate.confidence       = confidence;
        append_unique_entry(&candidate.source_entries, origin_id);
        append_unique_entry(&candidate.source_entries, size_id);
        if (has_full_size) {
            append_unique_entry(&candidate.source_entries, full_size_id);
        }
        candidate.has_origin = true;
        candidate.origin[0]  = origin_values[0];
        candidate.origin[1]  = origin_values[1];
        candidate.has_size   = true;
        candidate.size[0]    = size_values[0];
        candidate.size[1]    = size_values[1];
        candidate.has_rect   = true;
        candidate.rect[0]    = origin_values[0];
        candidate.rect[1]    = origin_values[1];
        candidate.rect[2]    = size_values[0];
        candidate.rect[3]    = size_values[1];
        candidate.has_values = true;
        candidate.values.reserve(has_full_size ? 6U : 4U);
        candidate.values.push_back(origin_values[0]);
        candidate.values.push_back(origin_values[1]);
        candidate.values.push_back(size_values[0]);
        candidate.values.push_back(size_values[1]);
        if (has_full_size) {
            candidate.values.push_back(full_size_values[0]);
            candidate.values.push_back(full_size_values[1]);

            const double right_margin = full_size_values[0] - origin_values[0]
                                        - size_values[0];
            const double bottom_margin = full_size_values[1] - origin_values[1]
                                         - size_values[1];
            if (right_margin >= 0.0 && bottom_margin >= 0.0) {
                candidate.has_margins = true;
                candidate.margins[0]  = origin_values[0];
                candidate.margins[1]  = origin_values[1];
                candidate.margins[2]  = right_margin;
                candidate.margins[3]  = bottom_margin;
            }
        }
        result->candidates.push_back(candidate);
    }

    static void append_fujifilm_raf_crop_candidate(const MetaStore& store,
                                                   MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }

        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (entry_is_deleted(entry)
                || entry.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            const uint16_t tag = entry.key.data.exif_tag.tag;
            if (tag != kFujiRafRawImageCropTopLeftTag
                && tag != kFujiRafRawZoomTopLeftTag) {
                continue;
            }

            const std::string_view ifd
                = arena_string(store.arena(), entry.key.data.exif_tag.ifd);
            if (!is_fujifilm_raf_data_ifd(ifd)) {
                continue;
            }

            const EntryId origin_id = static_cast<EntryId>(i);
            const EntryId full_size_id
                = find_first_exif_entry(store, ifd,
                                        kFujiRafRawImageFullSizeTag);
            if (tag == kFujiRafRawImageCropTopLeftTag) {
                const EntryId size_id
                    = find_first_exif_entry(store, ifd,
                                            kFujiRafRawImageCroppedSizeTag);
                append_origin_size_rect_candidate(
                    store, result, origin_id, size_id, full_size_id,
                    MetadataQuerySemanticKind::ActiveArea, 94U);
            } else {
                const EntryId size_id
                    = find_first_exif_entry(store, ifd, kFujiRafRawZoomSizeTag);
                append_origin_size_rect_candidate(
                    store, result, origin_id, size_id, full_size_id,
                    MetadataQuerySemanticKind::Crop, 88U);
            }
        }
    }

    static void append_scalar_origin_size_rect_candidate(
        const MetaStore& store, MetadataQueryResult* result, EntryId left_id,
        EntryId top_id, EntryId width_id, EntryId height_id,
        MetadataQuerySemanticKind semantic, uint8_t confidence)
    {
        if (!result || left_id == kInvalidEntryId || top_id == kInvalidEntryId
            || width_id == kInvalidEntryId || height_id == kInvalidEntryId) {
            return;
        }

        double left   = 0.0;
        double top    = 0.0;
        double width  = 0.0;
        double height = 0.0;
        if (!first_entry_value_to_double(store, left_id, &left)
            || !first_entry_value_to_double(store, top_id, &top)
            || !first_entry_value_to_double(store, width_id, &width)
            || !first_entry_value_to_double(store, height_id, &height)
            || left < 0.0 || top < 0.0 || width <= 0.0 || height <= 0.0) {
            return;
        }

        MetadataQueryCandidate candidate;
        candidate.semantic         = semantic;
        candidate.normalized_shape = MetadataQueryValueShape::Rect;
        candidate.confidence       = confidence;
        append_unique_entry(&candidate.source_entries, left_id);
        append_unique_entry(&candidate.source_entries, top_id);
        append_unique_entry(&candidate.source_entries, width_id);
        append_unique_entry(&candidate.source_entries, height_id);
        candidate.has_origin = true;
        candidate.origin[0]  = left;
        candidate.origin[1]  = top;
        candidate.has_size   = true;
        candidate.size[0]    = width;
        candidate.size[1]    = height;
        candidate.has_rect   = true;
        candidate.rect[0]    = left;
        candidate.rect[1]    = top;
        candidate.rect[2]    = width;
        candidate.rect[3]    = height;
        candidate.has_values = true;
        candidate.values.reserve(4U);
        candidate.values.push_back(left);
        candidate.values.push_back(top);
        candidate.values.push_back(width);
        candidate.values.push_back(height);
        result->candidates.push_back(candidate);
    }

    static void append_scalar_bounds_rect_candidate(
        const MetaStore& store, MetadataQueryResult* result, EntryId left_id,
        EntryId top_id, EntryId right_id, EntryId bottom_id,
        MetadataQuerySemanticKind semantic, uint8_t confidence)
    {
        if (!result || left_id == kInvalidEntryId || top_id == kInvalidEntryId
            || right_id == kInvalidEntryId || bottom_id == kInvalidEntryId) {
            return;
        }

        double left   = 0.0;
        double top    = 0.0;
        double right  = 0.0;
        double bottom = 0.0;
        if (!first_entry_value_to_double(store, left_id, &left)
            || !first_entry_value_to_double(store, top_id, &top)
            || !first_entry_value_to_double(store, right_id, &right)
            || !first_entry_value_to_double(store, bottom_id, &bottom)
            || left < 0.0 || top < 0.0 || right <= left || bottom <= top) {
            return;
        }

        MetadataQueryCandidate candidate;
        candidate.semantic         = semantic;
        candidate.normalized_shape = MetadataQueryValueShape::Rect;
        candidate.confidence       = confidence;
        append_unique_entry(&candidate.source_entries, left_id);
        append_unique_entry(&candidate.source_entries, top_id);
        append_unique_entry(&candidate.source_entries, right_id);
        append_unique_entry(&candidate.source_entries, bottom_id);
        candidate.has_origin = true;
        candidate.origin[0]  = left;
        candidate.origin[1]  = top;
        candidate.has_size   = true;
        candidate.size[0]    = right - left;
        candidate.size[1]    = bottom - top;
        candidate.has_rect   = true;
        candidate.rect[0]    = left;
        candidate.rect[1]    = top;
        candidate.rect[2]    = right - left;
        candidate.rect[3]    = bottom - top;
        candidate.has_values = true;
        candidate.values.reserve(4U);
        candidate.values.push_back(left);
        candidate.values.push_back(top);
        candidate.values.push_back(right);
        candidate.values.push_back(bottom);
        result->candidates.push_back(candidate);
    }

    static void append_scalar_margin_candidate(
        const MetaStore& store, MetadataQueryResult* result, EntryId left_id,
        EntryId top_id, EntryId right_id, EntryId bottom_id,
        MetadataQuerySemanticKind semantic, uint8_t confidence)
    {
        if (!result || left_id == kInvalidEntryId || top_id == kInvalidEntryId
            || right_id == kInvalidEntryId || bottom_id == kInvalidEntryId) {
            return;
        }

        double left   = 0.0;
        double top    = 0.0;
        double right  = 0.0;
        double bottom = 0.0;
        if (!first_entry_value_to_double(store, left_id, &left)
            || !first_entry_value_to_double(store, top_id, &top)
            || !first_entry_value_to_double(store, right_id, &right)
            || !first_entry_value_to_double(store, bottom_id, &bottom)
            || left < 0.0 || top < 0.0 || right < 0.0 || bottom < 0.0) {
            return;
        }

        MetadataQueryCandidate candidate;
        candidate.semantic         = semantic;
        candidate.normalized_shape = MetadataQueryValueShape::Vec4;
        candidate.confidence       = confidence;
        append_unique_entry(&candidate.source_entries, left_id);
        append_unique_entry(&candidate.source_entries, top_id);
        append_unique_entry(&candidate.source_entries, right_id);
        append_unique_entry(&candidate.source_entries, bottom_id);
        candidate.has_margins = true;
        candidate.margins[0]  = left;
        candidate.margins[1]  = top;
        candidate.margins[2]  = right;
        candidate.margins[3]  = bottom;
        candidate.has_values  = true;
        candidate.values.reserve(4U);
        candidate.values.push_back(left);
        candidate.values.push_back(top);
        candidate.values.push_back(right);
        candidate.values.push_back(bottom);
        result->candidates.push_back(candidate);
    }

    static void append_canon_aspect_crop_candidate(const MetaStore& store,
                                                   MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (entry_is_deleted(entry)
                || entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag
                       != kCanonAspectCroppedImageLeftTag) {
                continue;
            }
            const std::string_view ifd
                = arena_string(store.arena(), entry.key.data.exif_tag.ifd);
            if (!starts_with_ascii_case_insensitive(ifd,
                                                    "mk_canon_aspectinfo")) {
                continue;
            }
            append_scalar_origin_size_rect_candidate(
                store, result, static_cast<EntryId>(i),
                find_first_exif_entry(store, ifd,
                                      kCanonAspectCroppedImageTopTag),
                find_first_exif_entry(store, ifd,
                                      kCanonAspectCroppedImageWidthTag),
                find_first_exif_entry(store, ifd,
                                      kCanonAspectCroppedImageHeightTag),
                MetadataQuerySemanticKind::Crop, 91U);
        }
    }

    static void append_canon_crop_margin_candidate(const MetaStore& store,
                                                   MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (entry_is_deleted(entry)
                || entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != kCanonCropLeftMarginTag) {
                continue;
            }
            const std::string_view ifd
                = arena_string(store.arena(), entry.key.data.exif_tag.ifd);
            if (!starts_with_ascii_case_insensitive(ifd, "mk_canon_cropinfo")) {
                continue;
            }
            append_scalar_margin_candidate(
                store, result, static_cast<EntryId>(i),
                find_first_exif_entry(store, ifd, kCanonCropTopMarginTag),
                find_first_exif_entry(store, ifd, kCanonCropRightMarginTag),
                find_first_exif_entry(store, ifd, kCanonCropBottomMarginTag),
                MetadataQuerySemanticKind::Border, 90U);
        }
    }

    static void append_nikon_capture_crop_candidate(const MetaStore& store,
                                                    MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (entry_is_deleted(entry)
                || entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != kNikonCaptureCropLeftTag) {
                continue;
            }
            const std::string_view ifd
                = arena_string(store.arena(), entry.key.data.exif_tag.ifd);
            if (!starts_with_ascii_case_insensitive(
                    ifd, "mk_nikoncapture_cropdata")) {
                continue;
            }
            append_scalar_bounds_rect_candidate(
                store, result, static_cast<EntryId>(i),
                find_first_exif_entry(store, ifd, kNikonCaptureCropTopTag),
                find_first_exif_entry(store, ifd, kNikonCaptureCropRightTag),
                find_first_exif_entry(store, ifd, kNikonCaptureCropBottomTag),
                MetadataQuerySemanticKind::Crop, 88U);
        }
    }

    static void append_sony_panorama_crop_candidate(const MetaStore& store,
                                                    MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (entry_is_deleted(entry)
                || entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != kSonyPanoramaCropLeftTag) {
                continue;
            }
            const std::string_view ifd
                = arena_string(store.arena(), entry.key.data.exif_tag.ifd);
            if (!starts_with_ascii_case_insensitive(ifd, "mk_sony_panorama")) {
                continue;
            }
            append_scalar_margin_candidate(
                store, result, static_cast<EntryId>(i),
                find_first_exif_entry(store, ifd, kSonyPanoramaCropTopTag),
                find_first_exif_entry(store, ifd, kSonyPanoramaCropRightTag),
                find_first_exif_entry(store, ifd, kSonyPanoramaCropBottomTag),
                MetadataQuerySemanticKind::Border, 87U);
        }
    }

    static void append_masked_areas_candidate(const MetaStore& store,
                                              MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }
        const EntryId masked_id
            = find_first_exif_tag_any_ifd(store, kDngMaskedAreasTag);
        if (masked_id == kInvalidEntryId) {
            return;
        }

        double values[64] {};
        uint32_t count = 0U;
        if (!value_to_double_array(store, store.entry(masked_id).value, values,
                                   64U, &count)
            || count < 4U) {
            return;
        }

        MetadataQueryCandidate candidate;
        candidate.semantic         = MetadataQuerySemanticKind::Border;
        candidate.normalized_shape = MetadataQueryValueShape::Table;
        candidate.confidence       = 90U;
        append_unique_entry(&candidate.source_entries, masked_id);
        candidate.has_values = true;
        candidate.values.reserve(count);
        for (uint32_t i = 0U; i < count; ++i) {
            candidate.values.push_back(values[i]);
        }

        const double top    = values[0];
        const double left   = values[1];
        const double bottom = values[2];
        const double right  = values[3];
        if (right >= left && bottom >= top) {
            candidate.has_origin = true;
            candidate.origin[0]  = left;
            candidate.origin[1]  = top;
            candidate.has_size   = true;
            candidate.size[0]    = right - left;
            candidate.size[1]    = bottom - top;
            candidate.has_rect   = true;
            candidate.rect[0]    = left;
            candidate.rect[1]    = top;
            candidate.rect[2]    = right - left;
            candidate.rect[3]    = bottom - top;
        }
        result->candidates.push_back(candidate);
    }

    static void append_border_text_candidate(const MetaStore& store,
                                             MetadataQueryResult* result,
                                             const MetadataQueryMatch& match)
    {
        if (!result || match.entry_id == kInvalidEntryId
            || match.semantic != MetadataQuerySemanticKind::Border) {
            return;
        }

        std::string_view text;
        if (!text_value(store, store.entry(match.entry_id).value, &text)) {
            return;
        }

        double values[8] {};
        uint32_t count = 0U;
        if (!parse_number_list(text, values, 8U, &count) || count < 4U) {
            return;
        }

        MetadataQueryCandidate candidate;
        candidate.semantic         = MetadataQuerySemanticKind::Border;
        candidate.normalized_shape = MetadataQueryValueShape::Vec4;
        candidate.confidence       = match.confidence;
        append_unique_entry(&candidate.source_entries, match.entry_id);
        candidate.has_margins = true;
        candidate.margins[0]  = values[0];
        candidate.margins[1]  = values[1];
        candidate.margins[2]  = values[2];
        candidate.margins[3]  = values[3];
        candidate.has_values  = true;
        candidate.values.reserve(4U);
        for (uint32_t i = 0U; i < 4U; ++i) {
            candidate.values.push_back(values[i]);
        }
        result->candidates.push_back(candidate);
    }

    static void append_crop_match_candidates(const MetaStore& store,
                                             MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }
        for (size_t i = 0U; i < result->matches.size(); ++i) {
            append_border_text_candidate(store, result, result->matches[i]);
        }
    }

    static void append_query_value_candidate(const MetaStore& store,
                                             MetadataQueryResult* result,
                                             const MetadataQueryMatch& match)
    {
        if (!result || match.entry_id == kInvalidEntryId
            || match.semantic == MetadataQuerySemanticKind::Unknown) {
            return;
        }
        const Entry& entry = store.entry(match.entry_id);
        MetadataQueryCandidate candidate;
        candidate.semantic         = match.semantic;
        candidate.normalized_shape = value_shape(entry.value);
        candidate.confidence       = match.confidence;
        candidate.source_entries.reserve(1U);
        candidate.source_entries.push_back(match.entry_id);

        double values[16] {};
        uint32_t count = 0U;
        if (value_to_double_array(store, entry.value, values, 16U, &count)) {
            candidate.has_values = true;
            candidate.values.reserve(count);
            for (uint32_t i = 0U; i < count; ++i) {
                candidate.values.push_back(values[i]);
            }
        }
        result->candidates.push_back(candidate);
    }

    static void append_query_value_candidates(const MetaStore& store,
                                              MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }
        result->candidates.reserve(result->matches.size());
        for (size_t i = 0U; i < result->matches.size(); ++i) {
            append_query_value_candidate(store, result, result->matches[i]);
        }
    }

    static void append_grouped_query_candidates(const MetaStore& store,
                                                MetadataQueryResult* result,
                                                MetadataQueryKind kind)
    {
        if (!result) {
            return;
        }

        switch (kind) {
        case MetadataQueryKind::ExposureGain:
            append_exif_tag_series_candidates(
                store, result, kDngExposureGainTags,
                sizeof(kDngExposureGainTags) / sizeof(kDngExposureGainTags[0]),
                MetadataQuerySemanticKind::ExposureGain,
                MetadataQueryValueShape::Table, 90U);
            break;
        case MetadataQueryKind::Color:
            append_exif_tag_series_candidates(
                store, result, kDngColorMatrixTags,
                sizeof(kDngColorMatrixTags) / sizeof(kDngColorMatrixTags[0]),
                MetadataQuerySemanticKind::ColorMatrix,
                MetadataQueryValueShape::MatrixSet, 96U, 9U);
            append_exif_tag_series_candidates(
                store, result, kDngCameraCalibrationTags,
                sizeof(kDngCameraCalibrationTags)
                    / sizeof(kDngCameraCalibrationTags[0]),
                MetadataQuerySemanticKind::ColorMatrix,
                MetadataQueryValueShape::MatrixSet, 92U, 9U);
            append_exif_tag_series_candidates(
                store, result, kDngReductionMatrixTags,
                sizeof(kDngReductionMatrixTags)
                    / sizeof(kDngReductionMatrixTags[0]),
                MetadataQuerySemanticKind::ColorMatrix,
                MetadataQueryValueShape::MatrixSet, 92U, 9U);
            append_exif_tag_series_candidates(
                store, result, kDngForwardMatrixTags,
                sizeof(kDngForwardMatrixTags)
                    / sizeof(kDngForwardMatrixTags[0]),
                MetadataQuerySemanticKind::ColorMatrix,
                MetadataQueryValueShape::MatrixSet, 92U, 9U);
            append_vendor_grouped_query_candidates(store, result, kind);
            break;
        case MetadataQueryKind::WhiteBalance:
            append_exif_tag_series_candidates(
                store, result, kDngWhiteBalanceVectorTags,
                sizeof(kDngWhiteBalanceVectorTags)
                    / sizeof(kDngWhiteBalanceVectorTags[0]),
                MetadataQuerySemanticKind::WhiteBalance,
                MetadataQueryValueShape::VectorSet, 92U, 2U);
            append_vendor_grouped_query_candidates(store, result, kind);
            break;
        case MetadataQueryKind::LensCorrection:
            append_lens_correction_table_candidates(store, result);
            break;
        case MetadataQueryKind::RawProcessing:
            append_exif_tag_series_candidates(
                store, result, kDngBlackLevelTags,
                sizeof(kDngBlackLevelTags) / sizeof(kDngBlackLevelTags[0]),
                MetadataQuerySemanticKind::BlackLevel,
                MetadataQueryValueShape::Table, 94U);
            append_exif_tag_series_candidates(
                store, result, kDngCfaLayoutTags,
                sizeof(kDngCfaLayoutTags) / sizeof(kDngCfaLayoutTags[0]),
                MetadataQuerySemanticKind::CfaLayout,
                MetadataQueryValueShape::Table, 94U);
            append_exif_tag_series_candidates(
                store, result, kDngSensorGeometryTags,
                sizeof(kDngSensorGeometryTags)
                    / sizeof(kDngSensorGeometryTags[0]),
                MetadataQuerySemanticKind::SensorGeometry,
                MetadataQueryValueShape::Table, 90U);
            append_exif_tag_series_candidates(
                store, result, kDngRawStorageTags,
                sizeof(kDngRawStorageTags) / sizeof(kDngRawStorageTags[0]),
                MetadataQuerySemanticKind::RawStorage,
                MetadataQueryValueShape::Table, 88U);
            append_vendor_grouped_query_candidates(store, result, kind);
            break;
        case MetadataQueryKind::Crop:
        case MetadataQueryKind::Orientation:
        case MetadataQueryKind::Descriptive: break;
        }
    }

    static MetadataQueryResult query_semantic_metadata(const MetaStore& store,
                                                       MetadataQueryKind kind)
    {
        MetadataQueryResult result;
        result.kind = kind;

        const std::span<const Entry> entries = store.entries();
        result.matches.reserve(entries.size());
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (entry_is_deleted(entry)) {
                continue;
            }
            if (entry.key.kind == MetaKeyKind::ExifTag) {
                append_exif_match_if_relevant(store, &result,
                                              static_cast<EntryId>(i), entry,
                                              kind);
            } else if (entry.key.kind == MetaKeyKind::XmpProperty) {
                append_xmp_match_if_relevant(store, &result,
                                             static_cast<EntryId>(i), entry,
                                             kind);
            } else if (entry.key.kind == MetaKeyKind::IptcDataset) {
                append_iptc_match_if_relevant(&result, static_cast<EntryId>(i),
                                              entry, kind);
            } else if (entry.key.kind == MetaKeyKind::IccHeaderField
                       || entry.key.kind == MetaKeyKind::IccTag) {
                append_icc_match_if_relevant(&result, static_cast<EntryId>(i),
                                             entry, kind);
            } else if (entry.key.kind == MetaKeyKind::PngText) {
                append_png_text_match_if_relevant(store, &result,
                                                  static_cast<EntryId>(i),
                                                  entry, kind);
            }
        }
        append_query_value_candidates(store, &result);
        append_grouped_query_candidates(store, &result, kind);
        return result;
    }

}  // namespace

MetadataQueryResult
query_metadata(const MetaStore& store, MetadataQueryKind kind)
{
    switch (kind) {
    case MetadataQueryKind::Crop: return query_crop_metadata(store);
    case MetadataQueryKind::ExposureGain:
        return query_exposure_gain_metadata(store);
    case MetadataQueryKind::WhiteBalance:
        return query_white_balance_metadata(store);
    case MetadataQueryKind::Color: return query_color_metadata(store);
    case MetadataQueryKind::LensCorrection:
        return query_lens_correction_metadata(store);
    case MetadataQueryKind::Orientation:
        return query_orientation_metadata(store);
    case MetadataQueryKind::RawProcessing:
        return query_raw_processing_metadata(store);
    case MetadataQueryKind::Descriptive:
        return query_descriptive_metadata(store);
    }
    MetadataQueryResult result;
    result.kind = kind;
    return result;
}

MetadataQueryResult
query_crop_metadata(const MetaStore& store)
{
    MetadataQueryResult result;
    result.kind = MetadataQueryKind::Crop;

    const std::span<const Entry> entries = store.entries();
    result.matches.reserve(entries.size());
    for (size_t i = 0U; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        if (entry_is_deleted(entry)) {
            continue;
        }
        if (entry.key.kind == MetaKeyKind::ExifTag) {
            append_exif_match_if_relevant(store, &result,
                                          static_cast<EntryId>(i), entry,
                                          MetadataQueryKind::Crop);
        } else if (entry.key.kind == MetaKeyKind::XmpProperty) {
            append_xmp_match_if_relevant(store, &result,
                                         static_cast<EntryId>(i), entry,
                                         MetadataQueryKind::Crop);
        }
    }

    append_default_crop_candidate(store, &result);
    append_active_area_candidate(store, &result);
    append_phaseone_crop_candidate(store, &result);
    append_fujifilm_raf_crop_candidate(store, &result);
    append_canon_aspect_crop_candidate(store, &result);
    append_canon_crop_margin_candidate(store, &result);
    append_nikon_capture_crop_candidate(store, &result);
    append_sony_panorama_crop_candidate(store, &result);
    append_masked_areas_candidate(store, &result);
    append_crop_match_candidates(store, &result);
    return result;
}

MetadataQueryResult
query_exposure_gain_metadata(const MetaStore& store)
{
    return query_semantic_metadata(store, MetadataQueryKind::ExposureGain);
}

MetadataQueryResult
query_white_balance_metadata(const MetaStore& store)
{
    return query_semantic_metadata(store, MetadataQueryKind::WhiteBalance);
}

MetadataQueryResult
query_color_metadata(const MetaStore& store)
{
    return query_semantic_metadata(store, MetadataQueryKind::Color);
}

MetadataQueryResult
query_lens_correction_metadata(const MetaStore& store)
{
    return query_semantic_metadata(store, MetadataQueryKind::LensCorrection);
}

MetadataQueryResult
query_orientation_metadata(const MetaStore& store)
{
    return query_semantic_metadata(store, MetadataQueryKind::Orientation);
}

MetadataQueryResult
query_raw_processing_metadata(const MetaStore& store)
{
    return query_semantic_metadata(store, MetadataQueryKind::RawProcessing);
}

MetadataQueryResult
query_descriptive_metadata(const MetaStore& store)
{
    return query_semantic_metadata(store, MetadataQueryKind::Descriptive);
}

bool
metadata_query_fuzzy_search_available() noexcept
{
#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
    return true;
#else
    return false;
#endif
}

const char*
metadata_query_kind_name(MetadataQueryKind kind) noexcept
{
    switch (kind) {
    case MetadataQueryKind::Crop: return "crop";
    case MetadataQueryKind::ExposureGain: return "exposure_gain";
    case MetadataQueryKind::WhiteBalance: return "white_balance";
    case MetadataQueryKind::Color: return "color";
    case MetadataQueryKind::LensCorrection: return "lens_correction";
    case MetadataQueryKind::Orientation: return "orientation";
    case MetadataQueryKind::RawProcessing: return "raw_processing";
    case MetadataQueryKind::Descriptive: return "descriptive";
    }
    return "unknown";
}

const char*
metadata_query_semantic_kind_name(MetadataQuerySemanticKind kind) noexcept
{
    switch (kind) {
    case MetadataQuerySemanticKind::Unknown: return "unknown";
    case MetadataQuerySemanticKind::Crop: return "crop";
    case MetadataQuerySemanticKind::Border: return "border";
    case MetadataQuerySemanticKind::ActiveArea: return "active_area";
    case MetadataQuerySemanticKind::Exposure: return "exposure";
    case MetadataQuerySemanticKind::Gain: return "gain";
    case MetadataQuerySemanticKind::Color: return "color";
    case MetadataQuerySemanticKind::ColorProfile: return "color_profile";
    case MetadataQuerySemanticKind::WhiteBalance: return "white_balance";
    case MetadataQuerySemanticKind::ColorMatrix: return "color_matrix";
    case MetadataQuerySemanticKind::SourceColorTransform:
        return "source_color_transform";
    case MetadataQuerySemanticKind::LensCorrection: return "lens_correction";
    case MetadataQuerySemanticKind::Orientation: return "orientation";
    case MetadataQuerySemanticKind::ExposureGain: return "exposure_gain";
    case MetadataQuerySemanticKind::BlackLevel: return "black_level";
    case MetadataQuerySemanticKind::WhiteLevel: return "white_level";
    case MetadataQuerySemanticKind::Linearization: return "linearization";
    case MetadataQuerySemanticKind::CfaLayout: return "cfa_layout";
    case MetadataQuerySemanticKind::SensorGeometry: return "sensor_geometry";
    case MetadataQuerySemanticKind::RawStorage: return "raw_storage";
    case MetadataQuerySemanticKind::SourceProcessing:
        return "source_processing";
    case MetadataQuerySemanticKind::ComputationalProcessing:
        return "computational_processing";
    case MetadataQuerySemanticKind::ThermalProcessing:
        return "thermal_processing";
    case MetadataQuerySemanticKind::StitchProcessing:
        return "stitch_processing";
    case MetadataQuerySemanticKind::Title: return "title";
    case MetadataQuerySemanticKind::Description: return "description";
    case MetadataQuerySemanticKind::Creator: return "creator";
    case MetadataQuerySemanticKind::Keywords: return "keywords";
    case MetadataQuerySemanticKind::RawValueCurve: return "raw_value_curve";
    case MetadataQuerySemanticKind::RawLinearityLimit:
        return "raw_linearity_limit";
    case MetadataQuerySemanticKind::RawCalibrationCurve:
        return "raw_calibration_curve";
    case MetadataQuerySemanticKind::RawCurveControlPoints:
        return "raw_curve_control_points";
    case MetadataQuerySemanticKind::Rights: return "rights";
    case MetadataQuerySemanticKind::License: return "license";
    case MetadataQuerySemanticKind::Credit: return "credit";
    case MetadataQuerySemanticKind::Source: return "source";
    case MetadataQuerySemanticKind::Contact: return "contact";
    case MetadataQuerySemanticKind::Event: return "event";
    case MetadataQuerySemanticKind::Person: return "person";
    case MetadataQuerySemanticKind::Organization: return "organization";
    case MetadataQuerySemanticKind::Product: return "product";
    case MetadataQuerySemanticKind::Artwork: return "artwork";
    case MetadataQuerySemanticKind::RightsExpression:
        return "rights_expression";
    case MetadataQuerySemanticKind::Release: return "release";
    }
    return "unknown";
}

const char*
metadata_query_value_shape_name(MetadataQueryValueShape shape) noexcept
{
    switch (shape) {
    case MetadataQueryValueShape::Unknown: return "unknown";
    case MetadataQueryValueShape::Scalar: return "scalar";
    case MetadataQueryValueShape::Vec2: return "vec2";
    case MetadataQueryValueShape::Vec3: return "vec3";
    case MetadataQueryValueShape::Vec4: return "vec4";
    case MetadataQueryValueShape::Rect: return "rect";
    case MetadataQueryValueShape::Matrix3x3: return "matrix3x3";
    case MetadataQueryValueShape::VectorSet: return "vector_set";
    case MetadataQueryValueShape::MatrixSet: return "matrix_set";
    case MetadataQueryValueShape::Table: return "table";
    case MetadataQueryValueShape::Array: return "array";
    case MetadataQueryValueShape::Blob: return "blob";
    case MetadataQueryValueShape::Text: return "text";
    }
    return "unknown";
}

}  // namespace openmeta
