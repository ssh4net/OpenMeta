// SPDX-License-Identifier: Apache-2.0

#include "openmeta/ccm_query.h"
#include "metadata_scope_internal.h"

#include <cmath>
#include <cstring>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static constexpr uint16_t kTagCameraCalibrationSignature  = 0xC6F3U;
    static constexpr uint16_t kTagProfileCalibrationSignature = 0xC6F4U;
    static constexpr uint16_t kTagIlluminantData1             = 0xCD35U;
    static constexpr uint16_t kTagIlluminantData2             = 0xCD36U;
    static constexpr uint16_t kTagIlluminantData3             = 0xCD37U;

    struct CcmTagInfo final {
        uint16_t tag              = 0;
        const char* name          = nullptr;
        bool is_matrix3xN         = false;
        bool is_reduction         = false;
        bool is_scalar_illuminant = false;
    };

    static constexpr CcmTagInfo kCcmTags[] = {
        { 0xC621, "ColorMatrix1", true, false, false },
        { 0xC622, "ColorMatrix2", true, false, false },
        { 0xCD33, "ColorMatrix3", true, false, false },
        { 0xC623, "CameraCalibration1", true, false, false },
        { 0xC624, "CameraCalibration2", true, false, false },
        { 0xCD32, "CameraCalibration3", true, false, false },
        { 0xC714, "ForwardMatrix1", true, false, false },
        { 0xC715, "ForwardMatrix2", true, false, false },
        { 0xCD34, "ForwardMatrix3", true, false, false },
        { 0xC625, "ReductionMatrix1", true, true, false },
        { 0xC626, "ReductionMatrix2", true, true, false },
        { 0xCD3A, "ReductionMatrix3", true, true, false },
        { 0xC627, "AnalogBalance", false, false, false },
        { 0xC628, "AsShotNeutral", false, false, false },
        { 0xC629, "AsShotWhiteXY", false, false, false },
        { 0xC65A, "CalibrationIlluminant1", false, false, true },
        { 0xC65B, "CalibrationIlluminant2", false, false, true },
        { 0xCD31, "CalibrationIlluminant3", false, false, true },
    };

    struct IfdValidationState final {
        BlockId block = kInvalidBlockId;
        std::string ifd;
        bool has_analog_balance        = false;
        bool has_as_shot_neutral       = false;
        bool has_as_shot_white_xy      = false;
        uint32_t analog_balance_count  = 0U;
        uint32_t as_shot_neutral_count = 0U;

        bool has_cal_illum1 = false;
        bool has_cal_illum2 = false;
        bool has_cal_illum3 = false;
        int32_t cal_illum1  = -1;
        int32_t cal_illum2  = -1;
        int32_t cal_illum3  = -1;

        bool has_color1       = false;
        bool has_color2       = false;
        bool has_color3       = false;
        uint32_t color1_count = 0U;
        uint32_t color2_count = 0U;
        uint32_t color3_count = 0U;

        bool has_forward1       = false;
        bool has_forward2       = false;
        bool has_forward3       = false;
        uint32_t forward1_count = 0U;
        uint32_t forward2_count = 0U;
        uint32_t forward3_count = 0U;

        bool has_reduction1       = false;
        bool has_reduction2       = false;
        bool has_reduction3       = false;
        uint32_t reduction1_count = 0U;
        uint32_t reduction2_count = 0U;
        uint32_t reduction3_count = 0U;

        bool has_cam_cal1       = false;
        bool has_cam_cal2       = false;
        bool has_cam_cal3       = false;
        uint32_t cam_cal1_count = 0U;
        uint32_t cam_cal2_count = 0U;
        uint32_t cam_cal3_count = 0U;

        bool has_illum_data1 = false;
        bool has_illum_data2 = false;
        bool has_illum_data3 = false;

        bool has_camera_cal_sig  = false;
        bool has_profile_cal_sig = false;
        std::string camera_cal_sig;
        std::string profile_cal_sig;
    };

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool is_dng_version_tag(const Entry& e) noexcept
    {
        return e.key.kind == MetaKeyKind::ExifTag
               && e.key.data.exif_tag.tag == 0xC612U;
    }

    static const CcmTagInfo* find_ccm_tag(uint16_t tag) noexcept
    {
        for (size_t i = 0; i < sizeof(kCcmTags) / sizeof(kCcmTags[0]); ++i) {
            if (kCcmTags[i].tag == tag) {
                return &kCcmTags[i];
            }
        }
        return nullptr;
    }

    static bool ifd_is_dng_context(const MetaStore& store,
                                   const MetadataQueryScope& scope,
                                   const Entry& entry,
                                   const std::vector<EntryId>& versions) noexcept
    {
        for (EntryId id : versions) {
            if (metadata_scope_can_inherit(store, scope, entry, store.entry(id))) {
                return true;
            }
        }
        return false;
    }

    static void add_issue(std::vector<CcmIssue>* issues, CcmIssueSeverity sev,
                          CcmIssueCode code, BlockId block, std::string_view ifd, uint16_t tag,
                          std::string_view name, std::string_view message,
                          CcmQueryResult* result)
    {
        if (result) {
            result->issues_reported += 1U;
        }
        if (!issues) {
            return;
        }
        CcmIssue issue;
        issue.severity = sev;
        issue.code     = code;
        issue.block    = block;
        issue.ifd.assign(ifd.data(), ifd.size());
        issue.name.assign(name.data(), name.size());
        issue.tag = tag;
        issue.message.assign(message.data(), message.size());
        issues->push_back(std::move(issue));
    }

    static IfdValidationState*
    find_or_add_ifd_state(std::vector<IfdValidationState>* states,
                          BlockId block, std::string_view ifd, bool inherit) noexcept
    {
        if (!states) {
            return nullptr;
        }
        for (size_t i = 0; i < states->size(); ++i) {
            if (inherit || ((*states)[i].block == block && (*states)[i].ifd == ifd)) {
                return &(*states)[i];
            }
        }
        IfdValidationState state;
        state.block = block;
        state.ifd.assign(ifd.data(), ifd.size());
        states->push_back(std::move(state));
        return &(*states)[states->size() - 1U];
    }

    static bool read_u16(std::span<const std::byte> bytes, size_t off,
                         uint16_t* out) noexcept
    {
        if (!out || off + sizeof(uint16_t) > bytes.size()) {
            return false;
        }
        uint16_t v = 0;
        std::memcpy(&v, bytes.data() + off, sizeof(uint16_t));
        *out = v;
        return true;
    }

    static bool read_i16(std::span<const std::byte> bytes, size_t off,
                         int16_t* out) noexcept
    {
        if (!out || off + sizeof(int16_t) > bytes.size()) {
            return false;
        }
        int16_t v = 0;
        std::memcpy(&v, bytes.data() + off, sizeof(int16_t));
        *out = v;
        return true;
    }

    static bool read_u32(std::span<const std::byte> bytes, size_t off,
                         uint32_t* out) noexcept
    {
        if (!out || off + sizeof(uint32_t) > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        std::memcpy(&v, bytes.data() + off, sizeof(uint32_t));
        *out = v;
        return true;
    }

    static bool read_i32(std::span<const std::byte> bytes, size_t off,
                         int32_t* out) noexcept
    {
        if (!out || off + sizeof(int32_t) > bytes.size()) {
            return false;
        }
        int32_t v = 0;
        std::memcpy(&v, bytes.data() + off, sizeof(int32_t));
        *out = v;
        return true;
    }

    static bool read_u64(std::span<const std::byte> bytes, size_t off,
                         uint64_t* out) noexcept
    {
        if (!out || off + sizeof(uint64_t) > bytes.size()) {
            return false;
        }
        uint64_t v = 0;
        std::memcpy(&v, bytes.data() + off, sizeof(uint64_t));
        *out = v;
        return true;
    }

    static bool read_i64(std::span<const std::byte> bytes, size_t off,
                         int64_t* out) noexcept
    {
        if (!out || off + sizeof(int64_t) > bytes.size()) {
            return false;
        }
        int64_t v = 0;
        std::memcpy(&v, bytes.data() + off, sizeof(int64_t));
        *out = v;
        return true;
    }

    static bool read_f32(std::span<const std::byte> bytes, size_t off,
                         float* out) noexcept
    {
        if (!out || off + sizeof(float) > bytes.size()) {
            return false;
        }
        float v = 0.0F;
        std::memcpy(&v, bytes.data() + off, sizeof(float));
        *out = v;
        return true;
    }

    static bool read_f64(std::span<const std::byte> bytes, size_t off,
                         double* out) noexcept
    {
        if (!out || off + sizeof(double) > bytes.size()) {
            return false;
        }
        double v = 0.0;
        std::memcpy(&v, bytes.data() + off, sizeof(double));
        *out = v;
        return true;
    }

    static bool append_scalar_value(const MetaValue& value,
                                    std::vector<double>* out) noexcept
    {
        if (!out) {
            return false;
        }
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64:
            out->push_back(static_cast<double>(value.data.u64));
            return true;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            out->push_back(static_cast<double>(value.data.i64));
            return true;
        case MetaElementType::F32: {
            float f = 0.0F;
            std::memcpy(&f, &value.data.f32_bits, sizeof(float));
            out->push_back(static_cast<double>(f));
            return true;
        }
        case MetaElementType::F64: {
            double d = 0.0;
            std::memcpy(&d, &value.data.f64_bits, sizeof(double));
            out->push_back(d);
            return true;
        }
        case MetaElementType::URational: {
            const uint32_t den = value.data.ur.denom;
            if (den == 0U) {
                return false;
            }
            out->push_back(static_cast<double>(value.data.ur.numer)
                           / static_cast<double>(den));
            return true;
        }
        case MetaElementType::SRational: {
            const int32_t den = value.data.sr.denom;
            if (den == 0) {
                return false;
            }
            out->push_back(static_cast<double>(value.data.sr.numer)
                           / static_cast<double>(den));
            return true;
        }
        }
        return false;
    }

    static bool append_array_values(const ByteArena& arena,
                                    const MetaValue& value, uint32_t max_values,
                                    std::vector<double>* out,
                                    bool* limit_exceeded) noexcept
    {
        if (!out) {
            return false;
        }
        if (limit_exceeded) {
            *limit_exceeded = false;
        }
        const std::span<const std::byte> raw = arena.span(value.data.span);
        const uint32_t n                     = value.count;
        uint32_t element_bytes = 0U;
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::I8: element_bytes = 1U; break;
        case MetaElementType::U16:
        case MetaElementType::I16: element_bytes = 2U; break;
        case MetaElementType::U32:
        case MetaElementType::I32:
        case MetaElementType::F32: element_bytes = 4U; break;
        case MetaElementType::U64:
        case MetaElementType::I64:
        case MetaElementType::F64:
        case MetaElementType::URational:
        case MetaElementType::SRational: element_bytes = 8U; break;
        }
        if (element_bytes == 0U || n > raw.size() / element_bytes) {
            return false;
        }
        if (n == 0U) {
            return true;
        }
        uint32_t limit = n;
        if (max_values != 0U && limit > max_values) {
            limit = max_values;
            if (limit_exceeded) {
                *limit_exceeded = true;
            }
        }

        switch (value.elem_type) {
        case MetaElementType::U8: {
            for (uint32_t i = 0; i < limit; ++i) {
                if (static_cast<size_t>(i) >= raw.size()) {
                    return false;
                }
                out->push_back(static_cast<double>(
                    static_cast<uint8_t>(raw[static_cast<size_t>(i)])));
            }
            return true;
        }
        case MetaElementType::I8: {
            for (uint32_t i = 0; i < limit; ++i) {
                if (static_cast<size_t>(i) >= raw.size()) {
                    return false;
                }
                const int8_t v = static_cast<int8_t>(
                    static_cast<uint8_t>(raw[static_cast<size_t>(i)]));
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::U16: {
            for (uint32_t i = 0; i < limit; ++i) {
                uint16_t v = 0;
                if (!read_u16(raw, static_cast<size_t>(i) * sizeof(uint16_t),
                              &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::I16: {
            for (uint32_t i = 0; i < limit; ++i) {
                int16_t v = 0;
                if (!read_i16(raw, static_cast<size_t>(i) * sizeof(int16_t),
                              &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::U32: {
            for (uint32_t i = 0; i < limit; ++i) {
                uint32_t v = 0;
                if (!read_u32(raw, static_cast<size_t>(i) * sizeof(uint32_t),
                              &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::I32: {
            for (uint32_t i = 0; i < limit; ++i) {
                int32_t v = 0;
                if (!read_i32(raw, static_cast<size_t>(i) * sizeof(int32_t),
                              &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::U64: {
            for (uint32_t i = 0; i < limit; ++i) {
                uint64_t v = 0;
                if (!read_u64(raw, static_cast<size_t>(i) * sizeof(uint64_t),
                              &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::I64: {
            for (uint32_t i = 0; i < limit; ++i) {
                int64_t v = 0;
                if (!read_i64(raw, static_cast<size_t>(i) * sizeof(int64_t),
                              &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::F32: {
            for (uint32_t i = 0; i < limit; ++i) {
                float v = 0.0F;
                if (!read_f32(raw, static_cast<size_t>(i) * sizeof(float), &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::F64: {
            for (uint32_t i = 0; i < limit; ++i) {
                double v = 0.0;
                if (!read_f64(raw, static_cast<size_t>(i) * sizeof(double),
                              &v)) {
                    return false;
                }
                out->push_back(v);
            }
            return true;
        }
        case MetaElementType::URational: {
            for (uint32_t i = 0; i < limit; ++i) {
                URational r {};
                const size_t off = static_cast<size_t>(i) * sizeof(URational);
                if (off + sizeof(URational) > raw.size()) {
                    return false;
                }
                std::memcpy(&r, raw.data() + off, sizeof(URational));
                if (r.denom == 0U) {
                    return false;
                }
                out->push_back(static_cast<double>(r.numer)
                               / static_cast<double>(r.denom));
            }
            return true;
        }
        case MetaElementType::SRational: {
            for (uint32_t i = 0; i < limit; ++i) {
                SRational r {};
                const size_t off = static_cast<size_t>(i) * sizeof(SRational);
                if (off + sizeof(SRational) > raw.size()) {
                    return false;
                }
                std::memcpy(&r, raw.data() + off, sizeof(SRational));
                if (r.denom == 0) {
                    return false;
                }
                out->push_back(static_cast<double>(r.numer)
                               / static_cast<double>(r.denom));
            }
            return true;
        }
        }
        return false;
    }

    static bool decode_values(const ByteArena& arena, const MetaValue& value,
                              uint32_t max_values, std::vector<double>* out,
                              bool* limit_exceeded) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (limit_exceeded) {
            *limit_exceeded = false;
        }
        if (value.kind == MetaValueKind::Scalar) {
            return append_scalar_value(value, out);
        }
        if (value.kind == MetaValueKind::Array) {
            return append_array_values(arena, value, max_values, out,
                                       limit_exceeded);
        }
        return false;
    }

    static void infer_shape(const CcmTagInfo& info, size_t value_count,
                            uint32_t* rows, uint32_t* cols) noexcept
    {
        if (!rows || !cols) {
            return;
        }
        *rows = 1U;
        *cols = static_cast<uint32_t>(value_count);
        if (value_count == 0U) {
            return;
        }
        if (info.is_scalar_illuminant) {
            *rows = 1U;
            *cols = 1U;
            return;
        }
        if (info.tag == 0xc623U || info.tag == 0xc624U || info.tag == 0xcd32U) {
            const uint32_t planes = static_cast<uint32_t>(std::sqrt(static_cast<double>(value_count)));
            if (uint64_t(planes) * planes == value_count) {
                *rows = planes;
                *cols = planes;
            } else {
                *rows = 0U;
                *cols = 0U;
            }
        } else if (info.is_matrix3xN) {
            if ((value_count % 3U) != 0U) {
                *rows = 0U;
                *cols = 0U;
            } else if (info.tag == 0xc621U || info.tag == 0xc622U || info.tag == 0xcd33U) {
                *rows = static_cast<uint32_t>(value_count / 3U);
                *cols = 3U;
            } else {
                *rows = 3U;
                *cols = static_cast<uint32_t>(value_count / 3U);
            }
        }
    }

    static bool copy_text_like_value(const ByteArena& arena,
                                     const MetaValue& value,
                                     std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (value.kind != MetaValueKind::Text
            && value.kind != MetaValueKind::Bytes
            && value.kind != MetaValueKind::Array) {
            return false;
        }
        const std::span<const std::byte> bytes = arena.span(value.data.span);
        if (bytes.empty()) {
            return true;
        }
        size_t n = 0U;
        for (; n < bytes.size(); ++n) {
            if (bytes[n] == std::byte { 0x00 }) {
                break;
            }
        }
        out->reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const unsigned char c = static_cast<unsigned char>(bytes[i]);
            out->push_back(static_cast<char>(c));
        }
        return true;
    }

    static bool is_non_finite_present(const std::vector<double>& values) noexcept
    {
        for (size_t i = 0; i < values.size(); ++i) {
            if (!std::isfinite(values[i])) {
                return true;
            }
        }
        return false;
    }

    static bool is_matrix_tag(uint16_t tag) noexcept
    {
        return tag == 0xC621U || tag == 0xC622U || tag == 0xCD33U
               || tag == 0xC623U || tag == 0xC624U || tag == 0xCD32U
               || tag == 0xC714U || tag == 0xC715U || tag == 0xCD34U
               || tag == 0xC625U || tag == 0xC626U || tag == 0xCD3AU;
    }

    static bool is_valid_calibration_illuminant(int32_t value) noexcept
    {
        switch (value) {
        case 0:    // unknown
        case 1:    // daylight
        case 2:    // fluorescent
        case 3:    // tungsten
        case 4:    // flash
        case 9:    // fine weather
        case 10:   // cloudy
        case 11:   // shade
        case 12:   // daylight fluorescent
        case 13:   // day white fluorescent
        case 14:   // cool white fluorescent
        case 15:   // white fluorescent
        case 17:   // standard light A
        case 18:   // standard light B
        case 19:   // standard light C
        case 20:   // D55
        case 21:   // D65
        case 22:   // D75
        case 23:   // D50
        case 24:   // ISO studio tungsten
        case 255:  // other
            return true;
        default: return false;
        }
    }

    static bool validate_field(const CcmTagInfo& info, const CcmField& field,
                               CcmValidationMode validation_mode,
                               std::vector<CcmIssue>* issues,
                               CcmQueryResult* result) noexcept
    {
        if (is_non_finite_present(field.values)) {
            add_issue(issues, CcmIssueSeverity::Error,
                      CcmIssueCode::NonFiniteValue, field.block, field.ifd, field.tag,
                      field.name, "field contains non-finite numeric value",
                      result);
            return false;
        }

        if (validation_mode == CcmValidationMode::None) {
            return true;
        }

        const uint32_t n = static_cast<uint32_t>(field.values.size());
        const bool square_calibration = field.tag == 0xc623U || field.tag == 0xc624U || field.tag == 0xcd32U;
        if (square_calibration) {
            const uint32_t planes = static_cast<uint32_t>(std::sqrt(static_cast<double>(n)));
            if (uint64_t(planes) * planes != n) {
                add_issue(issues, CcmIssueSeverity::Warning,
                          CcmIssueCode::UnexpectedCount, field.block, field.ifd,
                          field.tag, field.name, "camera calibration count is not square", result);
            }
        }
        if (is_matrix_tag(field.tag) && !square_calibration && (n % 3U) != 0U) {
            add_issue(issues, CcmIssueSeverity::Warning,
                      CcmIssueCode::MatrixCountNotDivisibleBy3, field.block, field.ifd,
                      field.tag, field.name,
                      "matrix-like field count is not divisible by 3", result);
        }
        if (is_matrix_tag(field.tag) && n > 36U) {
            add_issue(issues, CcmIssueSeverity::Warning,
                      CcmIssueCode::UnexpectedCount, field.block, field.ifd, field.tag,
                      field.name,
                      "matrix-like field count exceeds conservative bound (36)",
                      result);
        }

        if (field.tag == 0xC629U && n != 2U) {
            add_issue(issues, CcmIssueSeverity::Warning,
                      CcmIssueCode::UnexpectedCount, field.block, field.ifd, field.tag,
                      field.name,
                      "AsShotWhiteXY should contain exactly 2 values", result);
        } else if (field.tag == 0xC629U && n == 2U) {
            const double x = field.values[0];
            const double y = field.values[1];
            if (!(x > 0.0 && x < 1.0 && y > 0.0 && y < 1.0 && (x + y) <= 1.0)) {
                add_issue(issues, CcmIssueSeverity::Warning,
                          CcmIssueCode::WhiteXYOutOfRange, field.block, field.ifd, field.tag,
                          field.name,
                          "AsShotWhiteXY values should be in (0,1) and x+y<=1",
                          result);
            }
        }

        if (info.is_scalar_illuminant && n != 1U) {
            add_issue(issues, CcmIssueSeverity::Warning,
                      CcmIssueCode::UnexpectedCount, field.block, field.ifd, field.tag,
                      field.name,
                      "CalibrationIlluminant should contain exactly 1 value",
                      result);
        } else if (info.is_scalar_illuminant && n == 1U) {
            const int32_t code = static_cast<int32_t>(
                std::llround(field.values[0]));
            if (std::fabs(field.values[0] - static_cast<double>(code)) > 1e-9
                || !is_valid_calibration_illuminant(code)) {
                add_issue(issues, CcmIssueSeverity::Warning,
                          CcmIssueCode::InvalidIlluminantCode, field.block, field.ifd,
                          field.tag, field.name,
                          "CalibrationIlluminant contains unsupported value",
                          result);
            }
        }

        if ((field.tag == 0xC627U || field.tag == 0xC628U) && n < 3U) {
            add_issue(issues, CcmIssueSeverity::Warning,
                      CcmIssueCode::UnexpectedCount, field.block, field.ifd, field.tag,
                      field.name,
                      "value count below common minimum of 3 channels", result);
        }

        if (field.tag == 0xC627U || field.tag == 0xC628U) {
            bool non_positive = false;
            for (size_t i = 0; i < field.values.size(); ++i) {
                if (field.values[i] <= 0.0) {
                    non_positive = true;
                    break;
                }
            }
            if (non_positive) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::NonPositiveValue, field.block, field.ifd, field.tag,
                    field.name,
                    "contains non-positive values (often invalid in DNG practice)",
                    result);
            }
        }

        return true;
    }

    static void mark_field_presence(const CcmField& field,
                                    IfdValidationState* state) noexcept
    {
        if (!state) {
            return;
        }
        const uint32_t n = static_cast<uint32_t>(field.total_values);
        switch (field.tag) {
        case 0xC621U:
            state->has_color1   = true;
            state->color1_count = n;
            break;
        case 0xC622U:
            state->has_color2   = true;
            state->color2_count = n;
            break;
        case 0xCD33U:
            state->has_color3   = true;
            state->color3_count = n;
            break;
        case 0xC714U:
            state->has_forward1   = true;
            state->forward1_count = n;
            break;
        case 0xC715U:
            state->has_forward2   = true;
            state->forward2_count = n;
            break;
        case 0xCD34U:
            state->has_forward3   = true;
            state->forward3_count = n;
            break;
        case 0xC625U:
            state->has_reduction1   = true;
            state->reduction1_count = n;
            break;
        case 0xC626U:
            state->has_reduction2   = true;
            state->reduction2_count = n;
            break;
        case 0xCD3AU:
            state->has_reduction3   = true;
            state->reduction3_count = n;
            break;
        case 0xC623U:
            state->has_cam_cal1   = true;
            state->cam_cal1_count = n;
            break;
        case 0xC624U:
            state->has_cam_cal2   = true;
            state->cam_cal2_count = n;
            break;
        case 0xCD32U:
            state->has_cam_cal3   = true;
            state->cam_cal3_count = n;
            break;
        case 0xC627U:
            state->has_analog_balance   = true;
            state->analog_balance_count = n;
            break;
        case 0xC628U:
            state->has_as_shot_neutral   = true;
            state->as_shot_neutral_count = n;
            break;
        case 0xC629U: state->has_as_shot_white_xy = true; break;
        case 0xC65AU:
            state->has_cal_illum1 = true;
            if (!field.values.empty()) {
                state->cal_illum1 = static_cast<int32_t>(
                    std::llround(field.values[0]));
            }
            break;
        case 0xC65BU:
            state->has_cal_illum2 = true;
            if (!field.values.empty()) {
                state->cal_illum2 = static_cast<int32_t>(
                    std::llround(field.values[0]));
            }
            break;
        case 0xCD31U:
            state->has_cal_illum3 = true;
            if (!field.values.empty()) {
                state->cal_illum3 = static_cast<int32_t>(
                    std::llround(field.values[0]));
            }
            break;
        default: break;
        }
    }

    static void
    run_cross_field_validation(const std::vector<IfdValidationState>& states,
                               std::vector<CcmIssue>* issues,
                               CcmQueryResult* result)
    {
        for (size_t i = 0; i < states.size(); ++i) {
            const IfdValidationState& s = states[i];

            if (s.has_as_shot_neutral && s.has_as_shot_white_xy) {
                add_issue(issues, CcmIssueSeverity::Warning,
                          CcmIssueCode::AsShotConflict, s.block, s.ifd, 0U, "AsShot*",
                          "AsShotNeutral and AsShotWhiteXY both present",
                          result);
            }
            if (s.has_analog_balance && s.has_as_shot_neutral
                && s.analog_balance_count != s.as_shot_neutral_count) {
                add_issue(issues, CcmIssueSeverity::Warning,
                          CcmIssueCode::UnexpectedCount, s.block, s.ifd, 0xC627U,
                          "AnalogBalance",
                          "AnalogBalance count differs from AsShotNeutral count",
                          result);
            }
            if (s.has_color1 && (s.color1_count % 3U) == 0U) {
                const uint32_t color1_channels = s.color1_count / 3U;
                if (s.has_as_shot_neutral
                    && s.as_shot_neutral_count != color1_channels) {
                    add_issue(
                        issues, CcmIssueSeverity::Warning,
                        CcmIssueCode::UnexpectedCount, s.block, s.ifd, 0xC628U,
                        "AsShotNeutral",
                        "AsShotNeutral count differs from ColorMatrix1 channel count",
                        result);
                }
                if (s.has_analog_balance
                    && s.analog_balance_count != color1_channels) {
                    add_issue(
                        issues, CcmIssueSeverity::Warning,
                        CcmIssueCode::UnexpectedCount, s.block, s.ifd, 0xC627U,
                        "AnalogBalance",
                        "AnalogBalance count differs from ColorMatrix1 channel count",
                        result);
                }
            }

            if (s.has_color1 && s.has_color2
                && s.color1_count != s.color2_count) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::UnexpectedCount, s.block, s.ifd, 0xC622U,
                    "ColorMatrix2",
                    "ColorMatrix1 and ColorMatrix2 should use the same element count",
                    result);
            }
            if (s.has_color3 && s.has_color1
                && s.color3_count != s.color1_count) {
                add_issue(issues, CcmIssueSeverity::Warning,
                          CcmIssueCode::UnexpectedCount, s.block, s.ifd, 0xCD33U,
                          "ColorMatrix3",
                          "ColorMatrix3 should match ColorMatrix1 element count",
                          result);
            }
            if (s.has_cam_cal1 && s.has_cam_cal2
                && s.cam_cal1_count != s.cam_cal2_count) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::UnexpectedCount, s.block, s.ifd, 0xC624U,
                    "CameraCalibration2",
                    "CameraCalibration1 and CameraCalibration2 should use the same element count",
                    result);
            }
            if (s.has_cam_cal3 && s.has_cam_cal1
                && s.cam_cal3_count != s.cam_cal1_count) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::UnexpectedCount, s.block, s.ifd, 0xCD32U,
                    "CameraCalibration3",
                    "CameraCalibration3 should match CameraCalibration1 element count",
                    result);
            }
            if (s.has_forward1 && s.has_forward2
                && s.forward1_count != s.forward2_count) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::UnexpectedCount, s.block, s.ifd, 0xC715U,
                    "ForwardMatrix2",
                    "ForwardMatrix1 and ForwardMatrix2 should use the same element count",
                    result);
            }
            if (s.has_forward3 && s.has_forward1
                && s.forward3_count != s.forward1_count) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::UnexpectedCount, s.block, s.ifd, 0xCD34U,
                    "ForwardMatrix3",
                    "ForwardMatrix3 should match ForwardMatrix1 element count",
                    result);
            }
            if (s.has_reduction1 && s.has_reduction2
                && s.reduction1_count != s.reduction2_count) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::UnexpectedCount, s.block, s.ifd, 0xC626U,
                    "ReductionMatrix2",
                    "ReductionMatrix1 and ReductionMatrix2 should use the same element count",
                    result);
            }
            if (s.has_reduction3 && s.has_reduction1
                && s.reduction3_count != s.reduction1_count) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::UnexpectedCount, s.block, s.ifd, 0xCD3AU,
                    "ReductionMatrix3",
                    "ReductionMatrix3 should match ReductionMatrix1 element count",
                    result);
            }

            if (s.has_cal_illum1 && !s.has_color1) {
                add_issue(issues, CcmIssueSeverity::Warning,
                          CcmIssueCode::MissingCompanionTag, s.block, s.ifd, 0xC65AU,
                          "CalibrationIlluminant1",
                          "CalibrationIlluminant1 present without ColorMatrix1",
                          result);
            }
            if (!s.has_cal_illum1 && s.has_color1) {
                add_issue(issues, CcmIssueSeverity::Warning,
                          CcmIssueCode::MissingCompanionTag, s.block, s.ifd, 0xC621U,
                          "ColorMatrix1",
                          "ColorMatrix1 present without CalibrationIlluminant1",
                          result);
            }
            if (s.has_cal_illum2 && !s.has_color2) {
                add_issue(issues, CcmIssueSeverity::Warning,
                          CcmIssueCode::MissingCompanionTag, s.block, s.ifd, 0xC65BU,
                          "CalibrationIlluminant2",
                          "CalibrationIlluminant2 present without ColorMatrix2",
                          result);
            }
            if (!s.has_cal_illum2 && s.has_color2) {
                add_issue(issues, CcmIssueSeverity::Warning,
                          CcmIssueCode::MissingCompanionTag, s.block, s.ifd, 0xC622U,
                          "ColorMatrix2",
                          "ColorMatrix2 present without CalibrationIlluminant2",
                          result);
            }

            if (s.has_cal_illum3) {
                if (!s.has_cal_illum1 || !s.has_cal_illum2) {
                    add_issue(
                        issues, CcmIssueSeverity::Warning,
                        CcmIssueCode::TripleIlluminantRule, s.block, s.ifd, 0xCD31U,
                        "CalibrationIlluminant3",
                        "CalibrationIlluminant3 requires CalibrationIlluminant1 and CalibrationIlluminant2",
                        result);
                }
                if (!s.has_color3) {
                    add_issue(issues, CcmIssueSeverity::Warning,
                              CcmIssueCode::TripleIlluminantRule, s.block, s.ifd,
                              0xCD33U, "ColorMatrix3",
                              "CalibrationIlluminant3 requires ColorMatrix3",
                              result);
                }
                const bool any_forward = s.has_forward1 || s.has_forward2
                                         || s.has_forward3;
                if (any_forward
                    && !(s.has_forward1 && s.has_forward2 && s.has_forward3)) {
                    add_issue(
                        issues, CcmIssueSeverity::Warning,
                        CcmIssueCode::TripleIlluminantRule, s.block, s.ifd, 0xCD34U,
                        "ForwardMatrix*",
                        "Triple-illuminant mode expects ForwardMatrix1/2/3 all present or all absent",
                        result);
                }
                const bool any_reduction = s.has_reduction1 || s.has_reduction2
                                           || s.has_reduction3;
                if (any_reduction
                    && !(s.has_reduction1 && s.has_reduction2
                         && s.has_reduction3)) {
                    add_issue(
                        issues, CcmIssueSeverity::Warning,
                        CcmIssueCode::TripleIlluminantRule, s.block, s.ifd, 0xCD3AU,
                        "ReductionMatrix*",
                        "Triple-illuminant mode expects ReductionMatrix1/2/3 all present or all absent",
                        result);
                }
            } else if (s.has_color3 || s.has_forward3 || s.has_reduction3) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::TripleIlluminantRule, s.block, s.ifd, 0U,
                    "ThirdIlluminantTags",
                    "third-illuminant tags present without CalibrationIlluminant3",
                    result);
            }

            if (s.has_cal_illum1 && s.cal_illum1 == 255 && !s.has_illum_data1) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::MissingIlluminantData, s.block, s.ifd, 0xCD35U,
                    "IlluminantData1",
                    "CalibrationIlluminant1=Other but IlluminantData1 is missing",
                    result);
            }
            if (s.has_cal_illum2 && s.cal_illum2 == 255 && !s.has_illum_data2) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::MissingIlluminantData, s.block, s.ifd, 0xCD36U,
                    "IlluminantData2",
                    "CalibrationIlluminant2=Other but IlluminantData2 is missing",
                    result);
            }
            if (s.has_cal_illum3 && s.cal_illum3 == 255 && !s.has_illum_data3) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::MissingIlluminantData, s.block, s.ifd, 0xCD37U,
                    "IlluminantData3",
                    "CalibrationIlluminant3=Other but IlluminantData3 is missing",
                    result);
            }

            if ((s.has_cam_cal1 || s.has_cam_cal2 || s.has_cam_cal3)
                && s.has_camera_cal_sig && s.has_profile_cal_sig
                && s.camera_cal_sig != s.profile_cal_sig) {
                add_issue(
                    issues, CcmIssueSeverity::Warning,
                    CcmIssueCode::CalibrationSignatureMismatch, s.block, s.ifd,
                    kTagCameraCalibrationSignature,
                    "CameraCalibrationSignature",
                    "CameraCalibrationSignature and ProfileCalibrationSignature do not match",
                    result);
            }
        }
    }

}  // namespace


CcmQueryResult
collect_dng_ccm_fields(const MetaStore& store, std::vector<CcmField>* out,
                       const CcmQueryOptions& options,
                       std::vector<CcmIssue>* issues) noexcept
{
    CcmQueryResult result;
    if (!out) {
        return result;
    }
    out->clear();
    if (issues) {
        issues->clear();
    }

    const ByteArena& arena           = store.arena();
    const std::span<const Entry> all = store.entries();

    std::vector<EntryId> dng_versions;
    dng_versions.reserve(8);

    for (size_t i = 0; i < all.size(); ++i) {
        const Entry& e = all[i];
        if (any(e.flags, EntryFlags::Deleted)
            || !metadata_scope_contains(store, options.scope, e)) {
            continue;
        }
        if (!is_dng_version_tag(e)) {
            continue;
        }
        const std::string_view ifd = arena_string(arena,
                                                  e.key.data.exif_tag.ifd);
        if (ifd.empty()) {
            continue;
        }
        dng_versions.push_back(static_cast<EntryId>(i));
    }

    if (options.require_dng_context && dng_versions.empty()) {
        return result;
    }

    std::vector<IfdValidationState> ifd_states;
    ifd_states.reserve(8U);

    for (size_t i = 0; i < all.size(); ++i) {
        const Entry& e = all[i];
        if (any(e.flags, EntryFlags::Deleted)
            || !metadata_scope_contains(store, options.scope, e)) {
            continue;
        }
        if (e.key.kind != MetaKeyKind::ExifTag) {
            continue;
        }
        const std::string_view ifd = arena_string(arena,
                                                  e.key.data.exif_tag.ifd);
        if (options.require_dng_context && !ifd_is_dng_context(store, options.scope, e, dng_versions)) {
            continue;
        }
        IfdValidationState* state = find_or_add_ifd_state(&ifd_states, e.origin.block, ifd,
                options.scope.enabled && options.scope.inherit_across_directories);
        if (!state) {
            continue;
        }
        const uint16_t tag = e.key.data.exif_tag.tag;
        if (tag == kTagIlluminantData1) {
            state->has_illum_data1 = true;
        } else if (tag == kTagIlluminantData2) {
            state->has_illum_data2 = true;
        } else if (tag == kTagIlluminantData3) {
            state->has_illum_data3 = true;
        } else if (tag == kTagCameraCalibrationSignature) {
            state->has_camera_cal_sig
                = copy_text_like_value(arena, e.value, &state->camera_cal_sig);
        } else if (tag == kTagProfileCalibrationSignature) {
            state->has_profile_cal_sig
                = copy_text_like_value(arena, e.value, &state->profile_cal_sig);
        }
    }

    for (size_t i = 0; i < all.size(); ++i) {
        const Entry& e = all[i];
        if (any(e.flags, EntryFlags::Deleted)
            || !metadata_scope_contains(store, options.scope, e)) {
            continue;
        }
        if (e.key.kind != MetaKeyKind::ExifTag) {
            continue;
        }
        const CcmTagInfo* info = find_ccm_tag(e.key.data.exif_tag.tag);
        if (!info) {
            continue;
        }
        if (!options.include_reduction_matrices && info->is_reduction) {
            continue;
        }

        const std::string_view ifd = arena_string(arena,
                                                  e.key.data.exif_tag.ifd);
        if (options.require_dng_context && !ifd_is_dng_context(store, options.scope, e, dng_versions)) {
            continue;
        }

        if (options.limits.max_fields != 0U
            && out->size() >= options.limits.max_fields) {
            result.status = CcmQueryStatus::LimitExceeded;
            break;
        }

        CcmField field;
        field.entry_id = static_cast<EntryId>(i);
        field.block = e.origin.block;
        field.total_values = e.value.kind == MetaValueKind::Scalar ? 1U : e.value.count;
        field.name.assign(info->name ? info->name : "");
        field.ifd.assign(ifd.data(), ifd.size());
        field.tag = info->tag;

        bool values_limit_exceeded = false;
        if (!decode_values(arena, e.value, options.limits.max_values_per_field,
                           &field.values, &values_limit_exceeded)) {
            if (options.validation_mode != CcmValidationMode::None) {
                add_issue(issues, CcmIssueSeverity::Warning,
                          CcmIssueCode::DecodeFailed, field.block, field.ifd, field.tag,
                          field.name, "failed to decode field payload",
                          &result);
            }
            continue;
        }
        if (field.values.empty()) {
            continue;
        }
        field.values_complete = !values_limit_exceeded;
        if (values_limit_exceeded) {
            result.status = CcmQueryStatus::LimitExceeded;
        }

        if (field.values_complete
            && !validate_field(*info, field, options.validation_mode, issues,
                               &result)) {
            result.fields_dropped += 1U;
            continue;
        }

        infer_shape(*info, static_cast<size_t>(field.total_values), &field.rows, &field.cols);
        out->push_back(field);

        if (options.validation_mode != CcmValidationMode::None) {
            IfdValidationState* state = find_or_add_ifd_state(&ifd_states, e.origin.block, ifd,
                options.scope.enabled && options.scope.inherit_across_directories);
            mark_field_presence(field, state);
        }
    }

    if (options.validation_mode != CcmValidationMode::None) {
        run_cross_field_validation(ifd_states, issues, &result);
    }

    result.fields_found = static_cast<uint32_t>(out->size());
    return result;
}

}  // namespace openmeta
