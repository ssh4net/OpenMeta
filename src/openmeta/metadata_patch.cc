// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_patch.h"

#include "metadata_logical_field_internal.h"
#include "metadata_patch_internal.h"
#include "openmeta/validate.h"
#include "xmp_patch_internal.h"

#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace openmeta {

struct MetadataPatchHandleAccess final {
    static MetadataPatchHandle make(uint64_t plan_id, uint32_t index) noexcept
    {
        MetadataPatchHandle handle;
        handle.token_ = (plan_id << 16U) | static_cast<uint64_t>(index + 1U);
        return handle;
    }

    static uint64_t plan_id(MetadataPatchHandle handle) noexcept
    {
        return handle.token_ >> 16U;
    }

    static uint32_t index(MetadataPatchHandle handle) noexcept
    {
        const uint32_t encoded = static_cast<uint16_t>(handle.token_);
        return encoded == 0U ? UINT32_MAX : encoded - 1U;
    }
};

namespace detail {

    MetadataPatchResult metadata_patch_error(MetadataPatchCode code,
                                             uint32_t failed_index) noexcept
    {
        MetadataPatchResult result;
        result.code         = code;
        result.failed_index = failed_index;
        return result;
    }

    static uint32_t meta_element_width(MetaElementType type) noexcept
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

    bool valid_exif_patch_spec(const MetadataPatchValueSpec& spec) noexcept
    {
        if (spec.kind == MetaValueKind::Empty || spec.count == 0U
            || meta_element_width(spec.elem_type) == 0U) {
            return false;
        }
        if (spec.kind == MetaValueKind::Scalar) {
            return spec.count == 1U
                   && spec.text_encoding == TextEncoding::Unknown;
        }
        if (spec.kind == MetaValueKind::Array) {
            return spec.text_encoding == TextEncoding::Unknown;
        }
        if (spec.kind == MetaValueKind::Bytes) {
            return spec.elem_type == MetaElementType::U8
                   && spec.text_encoding == TextEncoding::Unknown;
        }
        if (spec.kind == MetaValueKind::Text) {
            return spec.elem_type == MetaElementType::U8
                   && (spec.text_encoding == TextEncoding::Ascii
                       || spec.text_encoding == TextEncoding::Utf8);
        }
        return false;
    }

    bool exif_patch_specs_equal(const MetadataPatchValueSpec& first,
                                const MetadataPatchValueSpec& second) noexcept
    {
        return first.kind == second.kind && first.elem_type == second.elem_type
               && first.text_encoding == second.text_encoding
               && first.count == second.count;
    }

    static MetadataPatchValueSpec
    exif_patch_value_spec_from_view(const MetaValueView& value) noexcept
    {
        MetadataPatchValueSpec spec;
        spec.kind          = value.kind;
        spec.elem_type     = value.elem_type;
        spec.text_encoding = value.text_encoding;
        spec.count         = value.count;
        return spec;
    }

    bool exif_patch_width(const MetadataPatchValueSpec& spec,
                          uint32_t* out_width) noexcept
    {
        if (!out_width || !valid_exif_patch_spec(spec)) {
            return false;
        }
        if (spec.kind == MetaValueKind::Bytes) {
            *out_width = spec.count;
            return true;
        }
        if (spec.kind == MetaValueKind::Text) {
            if (spec.count == UINT32_MAX) {
                return false;
            }
            *out_width = spec.count + 1U;
            return true;
        }
        const uint32_t element_width = meta_element_width(spec.elem_type);
        if (spec.count > UINT32_MAX / element_width) {
            return false;
        }
        *out_width = spec.count * element_width;
        return true;
    }

    static bool
    exif_patch_value_view_valid(const MetaValueView& value,
                                const MetadataPatchValueSpec& expected) noexcept
    {
        const MetadataPatchValueSpec actual = exif_patch_value_spec_from_view(
            value);
        if (!exif_patch_specs_equal(actual, expected)) {
            return false;
        }
        uint32_t serialized_width = 0U;
        if (!exif_patch_width(actual, &serialized_width)) {
            return false;
        }
        if (actual.kind == MetaValueKind::Scalar) {
            if (!value.payload.empty()) {
                return false;
            }
            switch (actual.elem_type) {
            case MetaElementType::U8: return value.scalar.u64 <= UINT8_MAX;
            case MetaElementType::U16: return value.scalar.u64 <= UINT16_MAX;
            case MetaElementType::U32: return value.scalar.u64 <= UINT32_MAX;
            case MetaElementType::I8:
                return value.scalar.i64 >= INT8_MIN
                       && value.scalar.i64 <= INT8_MAX;
            case MetaElementType::I16:
                return value.scalar.i64 >= INT16_MIN
                       && value.scalar.i64 <= INT16_MAX;
            case MetaElementType::I32:
                return value.scalar.i64 >= INT32_MIN
                       && value.scalar.i64 <= INT32_MAX;
            case MetaElementType::URational: return value.scalar.ur.denom != 0U;
            case MetaElementType::SRational: return value.scalar.sr.denom != 0;
            case MetaElementType::U64:
            case MetaElementType::I64:
            case MetaElementType::F32:
            case MetaElementType::F64: return true;
            }
            return false;
        }
        const uint32_t payload_width = actual.kind == MetaValueKind::Text
                                           ? actual.count
                                           : serialized_width;
        if (value.payload.size() != static_cast<size_t>(payload_width)) {
            return false;
        }
        if (actual.kind == MetaValueKind::Text) {
            const std::string_view text(reinterpret_cast<const char*>(
                                            value.payload.data()),
                                        value.payload.size());
            if (!detail::metadata_logical_text_is_valid(text)) {
                return false;
            }
            if (actual.text_encoding == TextEncoding::Ascii) {
                for (const char character : text) {
                    if (static_cast<unsigned char>(character) > 0x7FU) {
                        return false;
                    }
                }
            }
        }
        if (actual.kind != MetaValueKind::Array
            || (actual.elem_type != MetaElementType::URational
                && actual.elem_type != MetaElementType::SRational)) {
            return true;
        }
        for (uint32_t i = 0U; i < actual.count; ++i) {
            uint32_t denominator = 0U;
            std::memcpy(&denominator,
                        value.payload.data() + static_cast<size_t>(i) * 8U + 4U,
                        sizeof(denominator));
            if (denominator == 0U) {
                return false;
            }
        }
        return true;
    }

    static bool exif_patch_ranges_overlap(const void* first_data,
                                          uint64_t first_size,
                                          const void* second_data,
                                          uint64_t second_size) noexcept
    {
        if (!first_data || !second_data || first_size == 0U
            || second_size == 0U) {
            return false;
        }
        const uintptr_t first_begin  = reinterpret_cast<uintptr_t>(first_data);
        const uintptr_t second_begin = reinterpret_cast<uintptr_t>(second_data);
        const uintptr_t max_address  = std::numeric_limits<uintptr_t>::max();
        if (first_size > max_address - first_begin
            || second_size > max_address - second_begin) {
            return true;
        }
        const uintptr_t first_end = first_begin
                                    + static_cast<uintptr_t>(first_size);
        const uintptr_t second_end = second_begin
                                     + static_cast<uintptr_t>(second_size);
        return first_begin < second_end && second_begin < first_end;
    }

    static void patch_write_u16le(std::byte* out, uint16_t value) noexcept
    {
        out[0] = static_cast<std::byte>(value & 0xFFU);
        out[1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    }

    static void patch_write_u32le(std::byte* out, uint32_t value) noexcept
    {
        out[0] = static_cast<std::byte>(value & 0xFFU);
        out[1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
        out[2] = static_cast<std::byte>((value >> 16U) & 0xFFU);
        out[3] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    }

    static void patch_write_u64le(std::byte* out, uint64_t value) noexcept
    {
        patch_write_u32le(out, static_cast<uint32_t>(value & 0xFFFFFFFFULL));
        patch_write_u32le(out + 4U, static_cast<uint32_t>(value >> 32U));
    }

    static void patch_write_scalar(std::byte* out,
                                   const MetaValueView& value) noexcept
    {
        switch (value.elem_type) {
        case MetaElementType::U8:
            out[0] = static_cast<std::byte>(value.scalar.u64 & 0xFFU);
            return;
        case MetaElementType::I8:
            out[0] = static_cast<std::byte>(
                static_cast<uint8_t>(static_cast<int8_t>(value.scalar.i64)));
            return;
        case MetaElementType::U16:
            patch_write_u16le(out, static_cast<uint16_t>(value.scalar.u64));
            return;
        case MetaElementType::I16:
            patch_write_u16le(out, static_cast<uint16_t>(
                                       static_cast<int16_t>(value.scalar.i64)));
            return;
        case MetaElementType::U32:
            patch_write_u32le(out, static_cast<uint32_t>(value.scalar.u64));
            return;
        case MetaElementType::I32:
            patch_write_u32le(out, static_cast<uint32_t>(
                                       static_cast<int32_t>(value.scalar.i64)));
            return;
        case MetaElementType::U64:
            patch_write_u64le(out, value.scalar.u64);
            return;
        case MetaElementType::I64:
            patch_write_u64le(out, static_cast<uint64_t>(value.scalar.i64));
            return;
        case MetaElementType::F32:
            patch_write_u32le(out, value.scalar.f32_bits);
            return;
        case MetaElementType::F64:
            patch_write_u64le(out, value.scalar.f64_bits);
            return;
        case MetaElementType::URational:
            patch_write_u32le(out, value.scalar.ur.numer);
            patch_write_u32le(out + 4U, value.scalar.ur.denom);
            return;
        case MetaElementType::SRational:
            patch_write_u32le(out,
                              static_cast<uint32_t>(value.scalar.sr.numer));
            patch_write_u32le(out + 4U,
                              static_cast<uint32_t>(value.scalar.sr.denom));
            return;
        }
    }

    static void patch_write_array(std::byte* out,
                                  const MetaValueView& value) noexcept
    {
        const uint32_t width = meta_element_width(value.elem_type);
        if (width == 1U) {
            std::memcpy(out, value.payload.data(), value.payload.size());
            return;
        }
        for (uint32_t i = 0U; i < value.count; ++i) {
            const size_t offset = static_cast<size_t>(i) * width;
            if (width == 2U) {
                uint16_t element = 0U;
                std::memcpy(&element, value.payload.data() + offset,
                            sizeof(element));
                patch_write_u16le(out + offset, element);
            } else if (width == 4U) {
                uint32_t element = 0U;
                std::memcpy(&element, value.payload.data() + offset,
                            sizeof(element));
                patch_write_u32le(out + offset, element);
            } else if (value.elem_type == MetaElementType::URational
                       || value.elem_type == MetaElementType::SRational) {
                uint32_t first  = 0U;
                uint32_t second = 0U;
                std::memcpy(&first, value.payload.data() + offset,
                            sizeof(first));
                std::memcpy(&second, value.payload.data() + offset + 4U,
                            sizeof(second));
                patch_write_u32le(out + offset, first);
                patch_write_u32le(out + offset + 4U, second);
            } else {
                uint64_t element = 0U;
                std::memcpy(&element, value.payload.data() + offset,
                            sizeof(element));
                patch_write_u64le(out + offset, element);
            }
        }
    }

    static void patch_write_value(std::byte* out,
                                  const MetaValueView& value) noexcept
    {
        if (value.kind == MetaValueKind::Scalar) {
            patch_write_scalar(out, value);
            return;
        }
        if (value.kind == MetaValueKind::Array) {
            patch_write_array(out, value);
            return;
        }
        if (!value.payload.empty()) {
            std::memcpy(out, value.payload.data(), value.payload.size());
        }
        if (value.kind == MetaValueKind::Text) {
            out[value.payload.size()] = std::byte { 0U };
        }
    }


}  // namespace detail

namespace {

    using detail::CompiledMetadataPatchSlot;
    using detail::metadata_patch_error;

    struct MetadataPatchState final {
        uint64_t plan_id = 0U;
        std::array<std::vector<std::byte>, 2> payloads;
        std::vector<CompiledMetadataPatchSlot> slots;
        std::vector<uint32_t> seen;
        uint32_t seen_epoch = 0U;
    };

    static uint64_t payload_size(const MetadataPatchState& state) noexcept
    {
        return state.payloads[0].size() + state.payloads[1].size();
    }

    static std::span<const std::byte>
    get_payload(const void* opaque, MetadataPatchPayload family) noexcept
    {
        const MetadataPatchState* state
            = static_cast<const MetadataPatchState*>(opaque);
        const uint32_t index = static_cast<uint32_t>(family);
        if (!state || index >= state->payloads.size())
            return {};
        return state->payloads[index];
    }

    static bool simple_xmp_name(std::string_view name) noexcept
    {
        if (name.empty())
            return false;
        for (size_t i = 0; i < name.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(name[i]);
            const bool initial    = (c >= 'a' && c <= 'z')
                                 || (c >= 'A' && c <= 'Z') || c == '_';
            if (!initial
                && (i == 0 || !((c >= '0' && c <= '9') || c == '-' || c == '.')))
                return false;
        }
        return true;
    }

    static std::string_view xml_escape(unsigned char c) noexcept
    {
        switch (c) {
        case '&': return "&amp;";
        case '<': return "&lt;";
        case '>': return "&gt;";
        case '"': return "&quot;";
        case '\'': return "&apos;";
        case '\r': return "&#xD;";
        default: return {};
        }
    }

    static bool xmp_value_width(const MetaValueView& value,
                                uint64_t* width) noexcept
    {
        if (value.kind != MetaValueKind::Text
            || value.elem_type != MetaElementType::U8
            || (value.text_encoding != TextEncoding::Utf8
                && value.text_encoding != TextEncoding::Ascii)
            || value.payload.size() != value.count)
            return false;
        uint64_t encoded = 0U;
        for (size_t i = 0; i < value.payload.size();) {
            const uint8_t first = static_cast<uint8_t>(value.payload[i]);
            uint32_t cp         = first;
            size_t count        = 1U;
            if (first >= 0x80U) {
                if (value.text_encoding == TextEncoding::Ascii)
                    return false;
                if (first >= 0xC2U && first <= 0xDFU) {
                    cp    = first & 0x1FU;
                    count = 2U;
                } else if (first >= 0xE0U && first <= 0xEFU) {
                    cp    = first & 0x0FU;
                    count = 3U;
                } else if (first >= 0xF0U && first <= 0xF4U) {
                    cp    = first & 0x07U;
                    count = 4U;
                } else
                    return false;
                if (count > value.payload.size() - i)
                    return false;
                for (size_t j = 1U; j < count; ++j) {
                    const uint8_t c = static_cast<uint8_t>(
                        value.payload[i + j]);
                    if ((c & 0xC0U) != 0x80U)
                        return false;
                    cp = (cp << 6U) | (c & 0x3FU);
                }
                if ((count == 3U && cp < 0x800U)
                    || (count == 4U && cp < 0x10000U) || cp > 0x10FFFFU
                    || (cp >= 0xD800U && cp <= 0xDFFFU))
                    return false;
            }
            if ((cp < 0x20U && cp != 9U && cp != 10U && cp != 13U)
                || cp == 0xFFFEU || cp == 0xFFFFU)
                return false;
            const std::string_view escape = count == 1U ? xml_escape(first)
                                                        : std::string_view {};
            encoded += escape.empty() ? count : escape.size();
            i += count;
        }
        *width = encoded;
        return true;
    }

    static void write_xmp_value(std::byte* output,
                                const MetaValueView& value) noexcept
    {
        for (const std::byte byte : value.payload) {
            const std::string_view escape = xml_escape(
                static_cast<uint8_t>(byte));
            if (escape.empty()) {
                *output++ = byte;
            } else {
                std::memcpy(output, escape.data(), escape.size());
                output += escape.size();
            }
        }
    }

    static MetadataPatchResult compile_xmp(
        const MetaStore& store, std::span<const MetadataPatchRequest> requests,
        const XmpPortableOptions& options, MetadataPatchState* state) noexcept
    {
        std::vector<detail::XmpScalarPatchSlot> capture;
        capture.reserve(requests.size());
        for (uint32_t i = 0; i < requests.size(); ++i) {
            const MetadataPatchRequest& request = requests[i];
            if (request.key.kind != MetaKeyKind::XmpProperty)
                continue;
            const MetaKeyView::Data::XmpProperty& key
                = request.key.data.xmp_property;
            if (key.schema_ns.empty())
                return metadata_patch_error(MetadataPatchCode::InvalidRequest,
                                            i);
            if (!simple_xmp_name(key.property_path))
                return metadata_patch_error(MetadataPatchCode::UnsupportedShape,
                                            i);
            if (request.occurrence != 0U)
                return metadata_patch_error(
                    MetadataPatchCode::OccurrenceOutOfRange, i);
            for (const detail::XmpScalarPatchSlot& prior : capture) {
                if (prior.ns_uri == key.schema_ns
                    && prior.property_path == key.property_path)
                    return metadata_patch_error(
                        MetadataPatchCode::DuplicateRequest, i);
            }
            capture.push_back(detail::XmpScalarPatchSlot { key.schema_ns,
                                                           key.property_path });
        }
        XmpDumpResult dump
            = detail::dump_xmp_portable_for_patch(store, {}, options, capture);
        if (dump.status != XmpDumpStatus::OutputTruncated
            && dump.status != XmpDumpStatus::Ok) {
            MetadataPatchResult result = metadata_patch_error(
                MetadataPatchCode::LimitExceeded);
            result.xmp_status = dump.status;
            return result;
        }
        state->payloads[1].resize(static_cast<size_t>(dump.needed));
        dump = detail::dump_xmp_portable_for_patch(store, state->payloads[1],
                                                   options, capture);
        if (dump.status != XmpDumpStatus::Ok) {
            MetadataPatchResult result = metadata_patch_error(
                MetadataPatchCode::SerializationFailed);
            result.xmp_status = dump.status;
            return result;
        }
        const std::vector<std::byte>& packet = state->payloads[1];
        const std::string_view packet_text(reinterpret_cast<const char*>(
                                               packet.data()),
                                           packet.size());
        uint64_t checked_width = 0U;
        if (!xmp_value_width(make_value_view_text(packet_text,
                                                  TextEncoding::Utf8),
                             &checked_width))
            return metadata_patch_error(MetadataPatchCode::InvalidMetadata);
        uint32_t captured = 0U;
        for (uint32_t i = 0; i < requests.size(); ++i) {
            if (requests[i].key.kind != MetaKeyKind::XmpProperty)
                continue;
            const detail::XmpScalarPatchSlot& source = capture[captured++];
            if (source.matches == 0U)
                return metadata_patch_error(
                    MetadataPatchCode::EntryNotSerializable, i);
            if (source.matches != 1U)
                return metadata_patch_error(MetadataPatchCode::AmbiguousProperty,
                                            i);
            if (source.byte_width != requests[i].escaped_width)
                return metadata_patch_error(MetadataPatchCode::WidthMismatch,
                                            i);
            if (source.byte_offset > state->payloads[1].size()
                || source.byte_width
                       > state->payloads[1].size() - source.byte_offset)
                return metadata_patch_error(
                    MetadataPatchCode::SerializationFailed, i);
            CompiledMetadataPatchSlot& slot = state->slots[i];
            slot.family                     = MetadataPatchPayload::Xmp;
            slot.byte_offset = static_cast<uint32_t>(source.byte_offset);
            slot.byte_width  = static_cast<uint32_t>(source.byte_width);
        }
        return {};
    }

}  // namespace

PreparedMetadataPatchPlan::PreparedMetadataPatchPlan() noexcept = default;
PreparedMetadataPatchPlan::~PreparedMetadataPatchPlan() noexcept { reset(); }
PreparedMetadataPatchPlan::PreparedMetadataPatchPlan(
    PreparedMetadataPatchPlan&& other) noexcept
    : state_(other.state_)
{
    other.state_ = nullptr;
}
PreparedMetadataPatchPlan&
PreparedMetadataPatchPlan::operator=(PreparedMetadataPatchPlan&& other) noexcept
{
    if (this != &other) {
        reset();
        state_       = other.state_;
        other.state_ = nullptr;
    }
    return *this;
}
bool
PreparedMetadataPatchPlan::valid() const noexcept
{
    return state_ != nullptr;
}
uint32_t
PreparedMetadataPatchPlan::handle_count() const noexcept
{
    const MetadataPatchState* state = static_cast<const MetadataPatchState*>(
        state_);
    return state ? static_cast<uint32_t>(state->slots.size()) : 0U;
}
std::span<const std::byte>
PreparedMetadataPatchPlan::payload(MetadataPatchPayload family) const noexcept
{
    return get_payload(state_, family);
}
void
PreparedMetadataPatchPlan::reset() noexcept
{
    delete static_cast<MetadataPatchState*>(state_);
    state_ = nullptr;
}

PreparedMetadataPatchInstance::PreparedMetadataPatchInstance() noexcept
    = default;
PreparedMetadataPatchInstance::~PreparedMetadataPatchInstance() noexcept
{
    reset();
}
PreparedMetadataPatchInstance::PreparedMetadataPatchInstance(
    PreparedMetadataPatchInstance&& other) noexcept
    : state_(other.state_)
{
    other.state_ = nullptr;
}
PreparedMetadataPatchInstance&
PreparedMetadataPatchInstance::operator=(
    PreparedMetadataPatchInstance&& other) noexcept
{
    if (this != &other) {
        reset();
        state_       = other.state_;
        other.state_ = nullptr;
    }
    return *this;
}
bool
PreparedMetadataPatchInstance::valid() const noexcept
{
    return state_ != nullptr;
}
uint32_t
PreparedMetadataPatchInstance::handle_count() const noexcept
{
    const MetadataPatchState* state = static_cast<const MetadataPatchState*>(
        state_);
    return state ? static_cast<uint32_t>(state->slots.size()) : 0U;
}
std::span<const std::byte>
PreparedMetadataPatchInstance::payload(MetadataPatchPayload family) const noexcept
{
    return get_payload(state_, family);
}
void
PreparedMetadataPatchInstance::reset() noexcept
{
    delete static_cast<MetadataPatchState*>(state_);
    state_ = nullptr;
}

uint32_t
metadata_patch_contract_version() noexcept
{
    return kMetadataPatchContractVersion;
}

MetadataPatchResult
prepare_metadata_patch_plan(const MetaStore& store,
                            std::span<const MetadataPatchRequest> requests,
                            const MetadataPatchPlanOptions& options,
                            std::span<MetadataPatchHandle> handles,
                            PreparedMetadataPatchPlan* out_plan) noexcept
{
    if (!out_plan)
        return metadata_patch_error(MetadataPatchCode::NullOutput);
    if (options.max_patch_requests == 0U || options.plan_id == 0U
        || options.plan_id > kMaxMetadataPatchPlanId)
        return metadata_patch_error(MetadataPatchCode::InvalidOptions);
    if (requests.empty())
        return metadata_patch_error(MetadataPatchCode::EmptyRequests);
    if (requests.size() > options.max_patch_requests
        || requests.size() > kMaxPreparedMetadataPatchHandles)
        return metadata_patch_error(MetadataPatchCode::LimitExceeded);
    if (handles.size() != requests.size())
        return metadata_patch_error(
            MetadataPatchCode::HandleBufferSizeMismatch);
    if (!store.is_finalized())
        return metadata_patch_error(MetadataPatchCode::StoreNotFinalized);
    if (options.validate) {
        MetadataValidationOptions validation;
        validation.require_finalized = true;
        if (!validate_store(store, validation).ok())
            return metadata_patch_error(MetadataPatchCode::InvalidMetadata);
    }
    bool exif = false;
    bool xmp  = false;
    for (uint32_t i = 0; i < requests.size(); ++i) {
        const MetadataPatchRequest& request = requests[i];
        if (request.key.kind == MetaKeyKind::ExifTag) {
            if (request.escaped_width != 0U)
                return metadata_patch_error(MetadataPatchCode::InvalidRequest,
                                            i);
            exif = true;
        } else if (request.key.kind == MetaKeyKind::XmpProperty) {
            xmp = true;
        } else
            return metadata_patch_error(MetadataPatchCode::InvalidRequest, i);
    }
    if (exif
        && (options.exif.max_output_bytes == 0U
            || options.exif.max_output_bytes > UINT32_MAX
            || static_cast<uint8_t>(options.exif.makernote_policy)
                   > static_cast<uint8_t>(
                       ExifTiffMakerNotePolicy::PreserveOpaque)))
        return metadata_patch_error(MetadataPatchCode::InvalidOptions);
    if (xmp
        && (options.xmp.limits.max_output_bytes == 0U
            || options.xmp.limits.max_output_bytes > UINT32_MAX
            || static_cast<uint8_t>(options.xmp.conflict_policy)
                   > static_cast<uint8_t>(XmpConflictPolicy::GeneratedWins)
            || static_cast<uint8_t>(options.xmp.existing_namespace_policy)
                   > static_cast<uint8_t>(
                       XmpExistingNamespacePolicy::PreserveCustom)
            || static_cast<uint8_t>(
                   options.xmp.existing_standard_namespace_policy)
                   > static_cast<uint8_t>(
                       XmpExistingStandardNamespacePolicy::CanonicalizeManaged)))
        return metadata_patch_error(MetadataPatchCode::InvalidOptions);

    MetadataPatchState* state = new (std::nothrow) MetadataPatchState;
    if (!state)
        return metadata_patch_error(MetadataPatchCode::AllocationFailed);
    PreparedMetadataPatchPlan candidate;
    candidate.state_ = state;
    state->slots.resize(requests.size());
    if (exif) {
        const MetadataPatchResult result = detail::compile_exif_patch_payload(
            store, requests, options.exif, state->slots, &state->payloads[0]);
        if (!result.ok())
            return result;
    }
    if (xmp) {
        const MetadataPatchResult result = compile_xmp(store, requests,
                                                       options.xmp, state);
        if (!result.ok())
            return result;
    }
    state->plan_id = options.plan_id;
    MetadataPatchResult result;
    result.handle_count = static_cast<uint32_t>(state->slots.size());
    result.payload_size = payload_size(*state);
    for (uint32_t i = 0; i < requests.size(); ++i)
        handles[i] = MetadataPatchHandleAccess::make(state->plan_id, i);
    *out_plan = std::move(candidate);
    return result;
}

MetadataPatchResult
create_prepared_metadata_patch_instance(
    const PreparedMetadataPatchPlan& plan,
    PreparedMetadataPatchInstance* out_instance) noexcept
{
    if (!out_instance)
        return metadata_patch_error(MetadataPatchCode::NullOutput);
    const MetadataPatchState* source = static_cast<const MetadataPatchState*>(
        plan.state_);
    if (!source)
        return metadata_patch_error(MetadataPatchCode::InvalidPlan);
    MetadataPatchState* state = new (std::nothrow) MetadataPatchState;
    if (!state)
        return metadata_patch_error(MetadataPatchCode::AllocationFailed);
    state->plan_id  = source->plan_id;
    state->payloads = source->payloads;
    state->slots    = source->slots;
    state->seen.assign(state->slots.size(), 0U);
    PreparedMetadataPatchInstance candidate;
    candidate.state_ = state;
    MetadataPatchResult result;
    result.payload_size = payload_size(*state);
    result.handle_count = static_cast<uint32_t>(state->slots.size());
    *out_instance       = std::move(candidate);
    return result;
}

MetadataPatchResult
patch_prepared_metadata_instance(
    PreparedMetadataPatchInstance* instance,
    std::span<const MetadataPatchUpdate> updates) noexcept
{
    if (!instance)
        return metadata_patch_error(MetadataPatchCode::NullOutput);
    MetadataPatchState* state = static_cast<MetadataPatchState*>(
        instance->state_);
    if (!state)
        return metadata_patch_error(MetadataPatchCode::InvalidInstance);
    if (updates.empty())
        return metadata_patch_error(MetadataPatchCode::EmptyUpdates);
    if (updates.size() > state->slots.size())
        return metadata_patch_error(MetadataPatchCode::LimitExceeded);
    for (const std::vector<std::byte>& payload : state->payloads) {
        if (detail::exif_patch_ranges_overlap(payload.data(), payload.size(),
                                              updates.data(),
                                              updates.size_bytes()))
            return metadata_patch_error(
                MetadataPatchCode::ValueAliasesInstance);
    }
    ++state->seen_epoch;
    if (state->seen_epoch == 0U) {
        for (uint32_t& stamp : state->seen)
            stamp = 0U;
        state->seen_epoch = 1U;
    }
    for (uint32_t i = 0; i < updates.size(); ++i) {
        const MetadataPatchUpdate& update = updates[i];
        if (!update.handle.valid())
            return metadata_patch_error(MetadataPatchCode::InvalidHandle, i);
        if (MetadataPatchHandleAccess::plan_id(update.handle) != state->plan_id)
            return metadata_patch_error(MetadataPatchCode::ForeignHandle, i);
        const uint32_t index = MetadataPatchHandleAccess::index(update.handle);
        if (index >= state->slots.size())
            return metadata_patch_error(MetadataPatchCode::InvalidHandle, i);
        if (state->seen[index] == state->seen_epoch)
            return metadata_patch_error(MetadataPatchCode::DuplicateHandle, i);
        state->seen[index]                    = state->seen_epoch;
        const CompiledMetadataPatchSlot& slot = state->slots[index];
        if (slot.family == MetadataPatchPayload::Xmp) {
            uint64_t width = 0U;
            if (!xmp_value_width(update.value, &width))
                return metadata_patch_error(MetadataPatchCode::InvalidValue, i);
            if (width != slot.byte_width)
                return metadata_patch_error(MetadataPatchCode::WidthMismatch,
                                            i);
        } else {
            const MetadataPatchValueSpec actual
                = detail::exif_patch_value_spec_from_view(update.value);
            if (!detail::exif_patch_specs_equal(actual, slot.value))
                return metadata_patch_error(MetadataPatchCode::ValueTypeMismatch,
                                            i);
            if (!detail::exif_patch_value_view_valid(update.value, slot.value))
                return metadata_patch_error(MetadataPatchCode::InvalidValue, i);
        }
        for (const std::vector<std::byte>& payload : state->payloads) {
            if (detail::exif_patch_ranges_overlap(payload.data(),
                                                  payload.size(),
                                                  update.value.payload.data(),
                                                  update.value.payload.size()))
                return metadata_patch_error(
                    MetadataPatchCode::ValueAliasesInstance, i);
        }
    }
    for (const MetadataPatchUpdate& update : updates) {
        const CompiledMetadataPatchSlot& slot
            = state->slots[MetadataPatchHandleAccess::index(update.handle)];
        std::byte* output
            = state->payloads[static_cast<uint32_t>(slot.family)].data()
              + slot.byte_offset;
        if (slot.family == MetadataPatchPayload::Xmp)
            write_xmp_value(output, update.value);
        else
            detail::patch_write_value(output, update.value);
    }
    MetadataPatchResult result;
    result.payload_size    = payload_size(*state);
    result.handle_count    = static_cast<uint32_t>(state->slots.size());
    result.patched_handles = static_cast<uint32_t>(updates.size());
    return result;
}

MetadataPatchResult
replay_prepared_metadata_instance(const PreparedMetadataPatchInstance& instance,
                                  MetadataPatchReplayCallback callback,
                                  void* user) noexcept
{
    if (!instance.valid())
        return metadata_patch_error(MetadataPatchCode::InvalidInstance);
    if (!callback)
        return metadata_patch_error(MetadataPatchCode::NullReplayCallback);
    MetadataPatchResult result;
    result.handle_count = instance.handle_count();
    for (uint32_t i = 0; i < 2U; ++i) {
        const MetadataPatchPayload family = static_cast<MetadataPatchPayload>(
            i);
        const std::span<const std::byte> bytes = instance.payload(family);
        result.payload_size += bytes.size();
        if (!bytes.empty() && !callback(user, family, bytes))
            return metadata_patch_error(MetadataPatchCode::ReplayFailed, i);
    }
    return result;
}

const char*
metadata_patch_code_name(MetadataPatchCode code) noexcept
{
    switch (code) {
    case MetadataPatchCode::None: return "none";
    case MetadataPatchCode::NullOutput: return "null_output";
    case MetadataPatchCode::InvalidOptions: return "invalid_options";
    case MetadataPatchCode::StoreNotFinalized: return "store_not_finalized";
    case MetadataPatchCode::InvalidMetadata: return "invalid_metadata";
    case MetadataPatchCode::NoExifData: return "no_exif_data";
    case MetadataPatchCode::LimitExceeded: return "limit_exceeded";
    case MetadataPatchCode::SerializationFailed: return "serialization_failed";
    case MetadataPatchCode::EmptyRequests: return "empty_requests";
    case MetadataPatchCode::HandleBufferSizeMismatch:
        return "handle_buffer_size_mismatch";
    case MetadataPatchCode::InvalidRequest: return "invalid_request";
    case MetadataPatchCode::KeyNotFound: return "key_not_found";
    case MetadataPatchCode::OccurrenceOutOfRange:
        return "occurrence_out_of_range";
    case MetadataPatchCode::ValueTypeMismatch: return "value_type_mismatch";
    case MetadataPatchCode::EntryNotSerializable:
        return "entry_not_serializable";
    case MetadataPatchCode::DuplicateRequest: return "duplicate_request";
    case MetadataPatchCode::AllocationFailed: return "allocation_failed";
    case MetadataPatchCode::InvalidPlan: return "invalid_plan";
    case MetadataPatchCode::InvalidInstance: return "invalid_instance";
    case MetadataPatchCode::EmptyUpdates: return "empty_updates";
    case MetadataPatchCode::InvalidHandle: return "invalid_handle";
    case MetadataPatchCode::ForeignHandle: return "foreign_handle";
    case MetadataPatchCode::DuplicateHandle: return "duplicate_handle";
    case MetadataPatchCode::WidthMismatch: return "width_mismatch";
    case MetadataPatchCode::InvalidValue: return "invalid_value";
    case MetadataPatchCode::ValueAliasesInstance:
        return "value_aliases_instance";
    case MetadataPatchCode::UnsupportedShape: return "unsupported_shape";
    case MetadataPatchCode::AmbiguousProperty: return "ambiguous_property";
    case MetadataPatchCode::NullReplayCallback: return "null_replay_callback";
    case MetadataPatchCode::ReplayFailed: return "replay_failed";
    }
    return "unknown";
}


}  // namespace openmeta
