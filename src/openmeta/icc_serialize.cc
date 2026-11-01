// SPDX-License-Identifier: Apache-2.0

#include "openmeta/icc_serialize.h"

#include "icc_profile_layout_internal.h"
#include "icc_serialize_internal.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace openmeta {
namespace {

    struct SelectedIccTag final {
        uint32_t signature = 0U;
        EntryId entry_id   = kInvalidEntryId;
        std::span<const std::byte> bytes;
    };

    static bool selected_icc_tag_less(const SelectedIccTag& first,
                                      const SelectedIccTag& second) noexcept
    {
        if (first.signature != second.signature) {
            return first.signature < second.signature;
        }
        return first.entry_id < second.entry_id;
    }

    static uint32_t element_width(MetaElementType type) noexcept
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

    static bool value_view_is_well_formed(const MetaValueView& value) noexcept
    {
        if (static_cast<uint8_t>(value.kind)
                > static_cast<uint8_t>(MetaValueKind::Text)
            || static_cast<uint8_t>(value.elem_type)
                   > static_cast<uint8_t>(MetaElementType::SRational)
            || static_cast<uint8_t>(value.text_encoding)
                   > static_cast<uint8_t>(TextEncoding::Utf16BE)) {
            return false;
        }
        if (!value.payload.empty() && !value.payload.data()) {
            return false;
        }
        switch (value.kind) {
        case MetaValueKind::Empty:
            return value.count == 0U && value.payload.empty();
        case MetaValueKind::Scalar:
            return value.count == 1U && value.payload.empty();
        case MetaValueKind::Array: {
            const uint32_t width = element_width(value.elem_type);
            return width != 0U
                   && static_cast<uint64_t>(value.count) * width
                          == value.payload.size();
        }
        case MetaValueKind::Bytes:
        case MetaValueKind::Text: return value.count == value.payload.size();
        }
        return false;
    }

    static bool icc_header_offset_valid(uint32_t offset) noexcept
    {
        switch (offset) {
        case 0U:
        case 4U:
        case 8U:
        case 12U:
        case 16U:
        case 20U:
        case 24U:
        case 36U:
        case 40U:
        case 44U:
        case 48U:
        case 52U:
        case 56U:
        case 64U:
        case 68U:
        case 80U:
        case 84U: return true;
        default: return false;
        }
    }

    static bool is_u32_header_offset(uint32_t offset) noexcept
    {
        switch (offset) {
        case 0U:
        case 4U:
        case 8U:
        case 12U:
        case 16U:
        case 20U:
        case 36U:
        case 40U:
        case 44U:
        case 48U:
        case 52U:
        case 64U:
        case 80U: return true;
        default: return false;
        }
    }

    static uint32_t read_u32be(std::span<const std::byte> bytes) noexcept
    {
        return (static_cast<uint32_t>(bytes[0]) << 24U)
               | (static_cast<uint32_t>(bytes[1]) << 16U)
               | (static_cast<uint32_t>(bytes[2]) << 8U)
               | static_cast<uint32_t>(bytes[3]);
    }

    static uint64_t read_u64be(std::span<const std::byte> bytes) noexcept
    {
        uint64_t value = 0U;
        for (size_t i = 0U; i < 8U; ++i) {
            value = (value << 8U) | static_cast<uint64_t>(bytes[i]);
        }
        return value;
    }

    static uint16_t read_u16be(std::span<const std::byte> bytes,
                               size_t offset) noexcept
    {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(bytes[offset]) << 8U)
            | static_cast<uint16_t>(bytes[offset + 1U]));
    }

    static bool date_is_valid(uint16_t year, uint16_t month, uint16_t day,
                              uint16_t hour, uint16_t minute,
                              uint16_t second) noexcept
    {
        if (year == 0U || month == 0U || month > 12U || hour > 23U
            || minute > 59U || second > 59U) {
            return false;
        }

        static constexpr std::array<uint8_t, 12U> kDaysInMonth = {
            31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
        };
        uint16_t max_day = kDaysInMonth[static_cast<size_t>(month - 1U)];
        const bool leap  = (year % 4U) == 0U
                          && ((year % 100U) != 0U || (year % 400U) == 0U);
        if (month == 2U && leap) {
            max_day = 29U;
        }
        return day != 0U && day <= max_day;
    }

    static bool extract_u32(const MetaValueView& value,
                            uint32_t* output) noexcept
    {
        if (!output) {
            return false;
        }
        if (value.kind == MetaValueKind::Scalar
            && value.elem_type == MetaElementType::U32
            && value.scalar.u64 <= UINT32_MAX) {
            *output = static_cast<uint32_t>(value.scalar.u64);
            return true;
        }
        if (value.kind == MetaValueKind::Bytes && value.payload.size() == 4U) {
            *output = read_u32be(value.payload);
            return true;
        }
        return false;
    }

    static bool extract_u64(const MetaValueView& value,
                            uint64_t* output) noexcept
    {
        if (!output) {
            return false;
        }
        if (value.kind == MetaValueKind::Scalar
            && value.elem_type == MetaElementType::U64) {
            *output = value.scalar.u64;
            return true;
        }
        if (value.kind == MetaValueKind::Bytes && value.payload.size() == 8U) {
            *output = read_u64be(value.payload);
            return true;
        }
        return false;
    }

    static bool date_value_is_valid(const MetaValueView& value) noexcept
    {
        if (value.kind == MetaValueKind::Bytes && value.payload.size() == 12U) {
            return date_is_valid(read_u16be(value.payload, 0U),
                                 read_u16be(value.payload, 2U),
                                 read_u16be(value.payload, 4U),
                                 read_u16be(value.payload, 6U),
                                 read_u16be(value.payload, 8U),
                                 read_u16be(value.payload, 10U));
        }
        if (value.kind != MetaValueKind::Array
            || value.elem_type != MetaElementType::U16 || value.count != 6U
            || value.payload.size() != 12U) {
            return false;
        }
        std::array<uint16_t, 6U> fields {};
        for (size_t i = 0U; i < fields.size(); ++i) {
            std::memcpy(&fields[i], value.payload.data() + i * sizeof(uint16_t),
                        sizeof(uint16_t));
        }
        return date_is_valid(fields[0], fields[1], fields[2], fields[3],
                             fields[4], fields[5]);
    }

    static bool header_value_is_valid(uint32_t offset,
                                      const MetaValueView& value) noexcept
    {
        if (is_u32_header_offset(offset)) {
            uint32_t field = 0U;
            if (!extract_u32(value, &field)) {
                return false;
            }
            if (offset == 8U) {
                const uint8_t major = static_cast<uint8_t>(field >> 24U);
                return major == 2U || major == 4U;
            }
            if (offset == 12U || offset == 16U || offset == 20U) {
                return field != 0U;
            }
            if (offset == 36U) {
                return field == UINT32_C(0x61637370);
            }
            return true;
        }
        if (offset == 24U) {
            return date_value_is_valid(value);
        }
        if (offset == 56U) {
            uint64_t field = 0U;
            return extract_u64(value, &field);
        }
        if (offset == 68U) {
            if (value.kind == MetaValueKind::Bytes) {
                return value.payload.size() == 12U;
            }
            if (value.kind != MetaValueKind::Array
                || value.elem_type != MetaElementType::SRational
                || value.count != 3U
                || value.payload.size() != sizeof(SRational) * 3U) {
                return false;
            }
            std::array<std::byte, 12U> encoded {};
            return detail::icc_srational_triplet_to_s15fixed16(value.payload,
                                                               value.count,
                                                               encoded);
        }
        if (offset == 84U) {
            return value.kind == MetaValueKind::Bytes
                   && value.payload.size() == 16U;
        }
        return false;
    }

    static void write_u16be(std::span<std::byte> output, size_t offset,
                            uint16_t value) noexcept
    {
        output[offset]      = static_cast<std::byte>((value >> 8U) & 0xFFU);
        output[offset + 1U] = static_cast<std::byte>(value & 0xFFU);
    }

    static void write_u32be(std::span<std::byte> output, size_t offset,
                            uint32_t value) noexcept
    {
        output[offset]      = static_cast<std::byte>((value >> 24U) & 0xFFU);
        output[offset + 1U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
        output[offset + 2U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
        output[offset + 3U] = static_cast<std::byte>(value & 0xFFU);
    }

    static void write_u64be(std::span<std::byte> output, size_t offset,
                            uint64_t value) noexcept
    {
        for (size_t i = 0U; i < 8U; ++i) {
            const uint32_t shift = static_cast<uint32_t>((7U - i) * 8U);
            output[offset + i]   = static_cast<std::byte>((value >> shift)
                                                          & 0xFFU);
        }
    }

    static bool encode_header_value(uint32_t offset, const MetaValueView& value,
                                    std::span<std::byte> header,
                                    IccProfileIdPolicy id_policy) noexcept
    {
        if (offset == 0U || offset == 36U) {
            return true;
        }
        if (offset == 84U) {
            if (id_policy == IccProfileIdPolicy::Preserve) {
                std::memcpy(header.data() + offset, value.payload.data(), 16U);
            }
            return true;
        }

        if (is_u32_header_offset(offset)) {
            if (value.kind == MetaValueKind::Bytes) {
                std::memcpy(header.data() + offset, value.payload.data(), 4U);
                return true;
            }
            write_u32be(header, offset,
                        static_cast<uint32_t>(value.scalar.u64));
            return true;
        }
        if (offset == 24U) {
            if (value.kind == MetaValueKind::Bytes) {
                std::memcpy(header.data() + offset, value.payload.data(), 12U);
                return true;
            }
            std::array<uint16_t, 6U> fields {};
            for (size_t i = 0U; i < fields.size(); ++i) {
                std::memcpy(&fields[i],
                            value.payload.data() + i * sizeof(uint16_t),
                            sizeof(uint16_t));
                write_u16be(header, offset + i * 2U, fields[i]);
            }
            return true;
        }
        if (offset == 56U) {
            if (value.kind == MetaValueKind::Bytes) {
                std::memcpy(header.data() + offset, value.payload.data(), 8U);
                return true;
            }
            write_u64be(header, offset, value.scalar.u64);
            return true;
        }
        if (offset == 68U) {
            if (value.kind == MetaValueKind::Bytes) {
                std::memcpy(header.data() + offset, value.payload.data(), 12U);
                return true;
            }
            return detail::icc_srational_triplet_to_s15fixed16(
                value.payload, value.count,
                std::span<std::byte, 12U>(header.data() + offset, 12U));
        }
        return false;
    }

    static bool make_meta_value_view(const MetaStore& store,
                                     const MetaValue& value,
                                     MetaValueView* output) noexcept
    {
        if (!output) {
            return false;
        }
        MetaValueView view;
        view.kind          = value.kind;
        view.elem_type     = value.elem_type;
        view.text_encoding = value.text_encoding;
        view.count         = value.count;
        view.scalar        = value.data;
        if (value.kind == MetaValueKind::Array
            || value.kind == MetaValueKind::Bytes
            || value.kind == MetaValueKind::Text) {
            const std::span<const std::byte> arena = store.arena().bytes();
            const uint64_t offset                  = value.data.span.offset;
            const uint64_t size                    = value.data.span.size;
            if (offset > arena.size() || size > arena.size() - offset) {
                return false;
            }
            view.payload = arena.subspan(static_cast<size_t>(offset),
                                         static_cast<size_t>(size));
        }
        if (!value_view_is_well_formed(view)) {
            return false;
        }
        *output = view;
        return true;
    }

    static bool icc_serialize_options_valid(
        const IccProfileSerializeOptions& options) noexcept
    {
        return options.max_tags != 0U && options.max_tag_bytes != 0U
               && options.max_output_bytes != 0U
               && options.max_output_bytes <= UINT32_MAX
               && options.max_output_bytes <= std::numeric_limits<size_t>::max()
               && static_cast<uint8_t>(options.profile_id_policy)
                      <= static_cast<uint8_t>(IccProfileIdPolicy::Preserve);
    }

    static IccProfileSerializeResult
    icc_serialize_error(IccProfileSerializeStatus status,
                        EntryId failed_entry = kInvalidEntryId) noexcept
    {
        IccProfileSerializeResult result;
        result.status       = status;
        result.failed_entry = failed_entry;
        return result;
    }

    static bool pointer_ranges_overlap(const void* first_data,
                                       uint64_t first_size,
                                       const void* second_data,
                                       uint64_t second_size) noexcept
    {
        if (first_size == 0U || second_size == 0U || !first_data
            || !second_data) {
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

    static bool
    project_icc_profile_size(uint64_t tag_count, uint64_t data_cursor,
                             uint64_t tag_size,
                             detail::IccProfileFinalTagPadding padding,
                             uint32_t* profile_size) noexcept
    {
        if (!profile_size) {
            return false;
        }
        internal::IccProfileLayout layout;
        internal::IccProfileTagLayout tag_layout;
        if (!internal::icc_profile_layout_begin(tag_count, &layout)
            || !internal::icc_profile_layout_tag(data_cursor, tag_size,
                                                 &tag_layout)) {
            return false;
        }
        uint64_t data_end = tag_layout.end_offset;
        if (padding == detail::IccProfileFinalTagPadding::FourByte) {
            if (data_end > std::numeric_limits<uint64_t>::max() - 3U) {
                return false;
            }
            data_end = (data_end + 3U) & ~uint64_t { 3U };
        } else if (padding
                   != detail::IccProfileFinalTagPadding::PreserveLegacyLength) {
            return false;
        }
        if (data_end > std::numeric_limits<uint64_t>::max()
                           - static_cast<uint64_t>(layout.data_offset)) {
            return false;
        }
        return internal::icc_profile_layout_finish(
            static_cast<uint64_t>(layout.data_offset) + data_end, profile_size);
    }

    static bool header_key_view(const Entry& entry,
                                MetaKeyView* output) noexcept
    {
        if (!output || entry.key.kind != MetaKeyKind::IccHeaderField) {
            return false;
        }
        MetaKeyView key;
        key.kind = MetaKeyKind::IccHeaderField;
        key.data.icc_header_field.offset
            = entry.key.data.icc_header_field.offset;
        *output = key;
        return true;
    }

    static bool tag_key_view(const Entry& entry, MetaKeyView* output) noexcept
    {
        if (!output || entry.key.kind != MetaKeyKind::IccTag) {
            return false;
        }
        MetaKeyView key;
        key.kind                   = MetaKeyKind::IccTag;
        key.data.icc_tag.signature = entry.key.data.icc_tag.signature;
        *output                    = key;
        return true;
    }

}  // namespace

namespace detail {

    bool icc_authoring_key_valid(const MetaKeyView& key) noexcept
    {
        if (key.kind == MetaKeyKind::IccHeaderField) {
            return icc_header_offset_valid(key.data.icc_header_field.offset);
        }
        return key.kind == MetaKeyKind::IccTag
               && key.data.icc_tag.signature != 0U;
    }


    bool icc_authoring_value_valid(const MetaKeyView& key,
                                   const MetaValueView& value) noexcept
    {
        if (!icc_authoring_key_valid(key) || !value_view_is_well_formed(value)
            || value.text_encoding != TextEncoding::Unknown
            || (value.kind == MetaValueKind::Bytes
                && value.elem_type != MetaElementType::U8)) {
            return false;
        }
        if (key.kind == MetaKeyKind::IccHeaderField) {
            return header_value_is_valid(key.data.icc_header_field.offset,
                                         value);
        }
        if (value.kind == MetaValueKind::Bytes) {
            if (value.payload.size() < 8U) {
                return false;
            }
        } else {
            return false;
        }
        if (read_u32be(value.payload.first(4U)) == 0U) {
            return false;
        }
        for (size_t i = 4U; i < 8U; ++i) {
            if (value.payload[i] != std::byte { 0U }) {
                return false;
            }
        }
        return true;
    }


    bool icc_srational_triplet_to_s15fixed16(
        std::span<const std::byte> raw, uint32_t count,
        std::span<std::byte, 12U> output) noexcept
    {
        if (count != 3U || sizeof(SRational) != 8U
            || raw.size() != sizeof(SRational) * 3U) {
            return false;
        }
        for (size_t i = 0U; i < 3U; ++i) {
            SRational value {};
            std::memcpy(&value, raw.data() + i * sizeof(SRational),
                        sizeof(SRational));
            if (value.denom == 0) {
                return false;
            }
            const int64_t scaled_numerator = static_cast<int64_t>(value.numer)
                                             * INT64_C(65536);
            const int64_t denominator = static_cast<int64_t>(value.denom);
            if (scaled_numerator % denominator != 0) {
                return false;
            }
            const int64_t fixed = scaled_numerator / denominator;
            if (fixed < static_cast<int64_t>(INT32_MIN)
                || fixed > static_cast<int64_t>(INT32_MAX)) {
                return false;
            }
            const uint32_t encoded = static_cast<uint32_t>(
                static_cast<int32_t>(fixed));
            const size_t offset = i * 4U;
            output[offset] = static_cast<std::byte>((encoded >> 24U) & 0xFFU);
            output[offset + 1U] = static_cast<std::byte>((encoded >> 16U)
                                                         & 0xFFU);
            output[offset + 2U] = static_cast<std::byte>((encoded >> 8U)
                                                         & 0xFFU);
            output[offset + 3U] = static_cast<std::byte>(encoded & 0xFFU);
        }
        return true;
    }


    bool icc_profile_measure(std::span<const IccProfileTagView> tags,
                             IccProfileFinalTagPadding final_padding,
                             uint32_t* profile_size) noexcept
    {
        if (!profile_size || tags.empty()
            || static_cast<uint8_t>(final_padding) > static_cast<uint8_t>(
                   IccProfileFinalTagPadding::PreserveLegacyLength)) {
            return false;
        }
        internal::IccProfileLayout layout;
        if (!internal::icc_profile_layout_begin(
                static_cast<uint64_t>(tags.size()), &layout)) {
            return false;
        }
        uint64_t cursor             = layout.data_offset;
        uint32_t previous_signature = 0U;
        for (size_t i = 0U; i < tags.size(); ++i) {
            if (i != 0U && tags[i].signature <= previous_signature) {
                return false;
            }
            if (!tags[i].bytes.empty() && !tags[i].bytes.data()) {
                return false;
            }
            previous_signature = tags[i].signature;
            internal::IccProfileTagLayout tag_layout;
            if (!internal::icc_profile_layout_tag(
                    cursor, static_cast<uint64_t>(tags[i].bytes.size()),
                    &tag_layout)) {
                return false;
            }
            cursor = tag_layout.end_offset;
        }
        uint64_t final_size = cursor;
        if (final_padding == IccProfileFinalTagPadding::FourByte) {
            if (cursor > std::numeric_limits<uint64_t>::max() - 3U) {
                return false;
            }
            final_size = (cursor + 3U) & ~uint64_t { 3U };
        }
        return internal::icc_profile_layout_finish(final_size, profile_size);
    }


    bool build_icc_profile_bytes_from_items(
        std::span<const std::byte, 128U> header,
        std::span<const IccProfileTagView> tags,
        IccProfileFinalTagPadding final_padding,
        std::vector<std::byte>* out_profile) noexcept
    {
        if (!out_profile || !header.data()) {
            return false;
        }
        uint32_t profile_size = 0U;
        if (!icc_profile_measure(tags, final_padding, &profile_size)) {
            return false;
        }
        const size_t output_size = static_cast<size_t>(profile_size);
        if (output_size > out_profile->max_size()) {
            return false;
        }

        internal::IccProfileLayout layout;
        if (!internal::icc_profile_layout_begin(
                static_cast<uint64_t>(tags.size()), &layout)) {
            return false;
        }
        std::vector<std::byte> built(output_size, std::byte { 0U });
        std::memcpy(built.data(), header.data(), header.size());
        std::span<std::byte> output(built.data(), built.size());
        write_u32be(output, 0U, profile_size);
        output[36U] = std::byte { 'a' };
        output[37U] = std::byte { 'c' };
        output[38U] = std::byte { 's' };
        output[39U] = std::byte { 'p' };
        write_u32be(output, 128U, layout.tag_count);

        uint64_t cursor       = layout.data_offset;
        uint32_t table_offset = 132U;
        for (const IccProfileTagView& tag : tags) {
            internal::IccProfileTagLayout tag_layout;
            if (!internal::icc_profile_layout_tag(
                    cursor, static_cast<uint64_t>(tag.bytes.size()),
                    &tag_layout)) {
                return false;
            }
            write_u32be(output, table_offset, tag.signature);
            write_u32be(output, table_offset + 4U, tag_layout.offset);
            write_u32be(output, table_offset + 8U, tag_layout.size);
            if (!tag.bytes.empty()) {
                if (!tag.bytes.data()) {
                    return false;
                }
                std::memcpy(output.data() + tag_layout.offset, tag.bytes.data(),
                            tag.bytes.size());
            }
            cursor = tag_layout.end_offset;
            table_offset += 12U;
        }
        out_profile->swap(built);
        return true;
    }

}  // namespace detail


IccProfileSerializeResult
serialize_icc_profile(const MetaStore& store, std::span<std::byte> output,
                      const IccProfileSerializeOptions& options) noexcept
{
    if (!icc_serialize_options_valid(options)) {
        return icc_serialize_error(IccProfileSerializeStatus::InvalidOptions);
    }
    if (!store.is_finalized()) {
        return icc_serialize_error(
            IccProfileSerializeStatus::StoreNotFinalized);
    }

    const std::span<const std::byte> arena = store.arena().bytes();
    if (pointer_ranges_overlap(output.data(), output.size(), arena.data(),
                               arena.size())) {
        return icc_serialize_error(IccProfileSerializeStatus::InvalidOptions);
    }

    std::array<std::byte, 128U> header {};
    std::array<bool, 128U> seen_headers {};
    std::vector<SelectedIccTag> selected_tags;
    uint64_t projected_tag_data_cursor   = 0U;
    uint32_t selected_header_count       = 0U;
    const std::span<const Entry> entries = store.entries();
    for (size_t index = 0U; index < entries.size(); ++index) {
        const Entry& entry = entries[index];
        if (any(entry.flags, EntryFlags::Deleted)) {
            continue;
        }
        if (entry.key.kind == MetaKeyKind::IccHeaderField) {
            const EntryId entry_id = static_cast<EntryId>(index);
            if (any(entry.flags, EntryFlags::Truncated)
                || any(entry.flags, EntryFlags::Unreadable)) {
                return icc_serialize_error(
                    IccProfileSerializeStatus::InvalidMetadata, entry_id);
            }
            MetaKeyView key;
            MetaValueView value;
            if (!header_key_view(entry, &key)
                || !detail::icc_authoring_key_valid(key)
                || !make_meta_value_view(store, entry.value, &value)
                || !detail::icc_authoring_value_valid(key, value)) {
                return icc_serialize_error(
                    IccProfileSerializeStatus::InvalidMetadata, entry_id);
            }
            const uint32_t offset = key.data.icc_header_field.offset;
            if (seen_headers[offset]) {
                return icc_serialize_error(
                    IccProfileSerializeStatus::InvalidMetadata, entry_id);
            }
            seen_headers[offset] = true;
            if (!encode_header_value(offset, value, header,
                                     options.profile_id_policy)) {
                return icc_serialize_error(
                    IccProfileSerializeStatus::InvalidMetadata, entry_id);
            }
            selected_header_count += 1U;
            continue;
        }
        if (entry.key.kind != MetaKeyKind::IccTag) {
            continue;
        }

        const EntryId entry_id = static_cast<EntryId>(index);
        if (any(entry.flags, EntryFlags::Truncated)
            || any(entry.flags, EntryFlags::Unreadable)) {
            return icc_serialize_error(
                IccProfileSerializeStatus::InvalidMetadata, entry_id);
        }
        if (selected_tags.size() >= options.max_tags) {
            return icc_serialize_error(IccProfileSerializeStatus::LimitExceeded,
                                       entry_id);
        }
        MetaKeyView key;
        MetaValueView value;
        if (!tag_key_view(entry, &key) || !detail::icc_authoring_key_valid(key)
            || !make_meta_value_view(store, entry.value, &value)
            || !detail::icc_authoring_value_valid(key, value)) {
            return icc_serialize_error(
                IccProfileSerializeStatus::InvalidMetadata, entry_id);
        }
        if (value.payload.size() > options.max_tag_bytes) {
            return icc_serialize_error(IccProfileSerializeStatus::LimitExceeded,
                                       entry_id);
        }
        uint32_t projected_profile_size = 0U;
        const uint64_t next_tag_count
            = static_cast<uint64_t>(selected_tags.size()) + 1U;
        if (!project_icc_profile_size(
                next_tag_count, projected_tag_data_cursor,
                static_cast<uint64_t>(value.payload.size()),
                detail::IccProfileFinalTagPadding::FourByte,
                &projected_profile_size)
            || static_cast<uint64_t>(projected_profile_size)
                   > options.max_output_bytes) {
            return icc_serialize_error(IccProfileSerializeStatus::LimitExceeded,
                                       entry_id);
        }
        internal::IccProfileTagLayout projected_tag_layout;
        if (!internal::icc_profile_layout_tag(projected_tag_data_cursor,
                                              static_cast<uint64_t>(
                                                  value.payload.size()),
                                              &projected_tag_layout)) {
            return icc_serialize_error(IccProfileSerializeStatus::LimitExceeded,
                                       entry_id);
        }
        projected_tag_data_cursor = projected_tag_layout.end_offset;
        SelectedIccTag tag;
        tag.signature = key.data.icc_tag.signature;
        tag.entry_id  = entry_id;
        tag.bytes     = value.payload;
        selected_tags.push_back(tag);
    }

    if (selected_tags.empty()) {
        return icc_serialize_error(IccProfileSerializeStatus::NoIccData);
    }

    static constexpr std::array<uint32_t, 6U> kRequiredHeaderOffsets = {
        8U, 12U, 16U, 20U, 24U, 68U,
    };
    for (const uint32_t offset : kRequiredHeaderOffsets) {
        if (!seen_headers[offset]) {
            return icc_serialize_error(
                IccProfileSerializeStatus::InvalidMetadata);
        }
    }

    std::sort(selected_tags.begin(), selected_tags.end(),
              selected_icc_tag_less);
    std::vector<detail::IccProfileTagView> tags;
    tags.reserve(selected_tags.size());
    for (size_t i = 0U; i < selected_tags.size(); ++i) {
        if (i != 0U
            && selected_tags[i - 1U].signature == selected_tags[i].signature) {
            return icc_serialize_error(
                IccProfileSerializeStatus::InvalidMetadata,
                selected_tags[i].entry_id);
        }
        detail::IccProfileTagView tag;
        tag.signature = selected_tags[i].signature;
        tag.bytes     = selected_tags[i].bytes;
        tags.push_back(tag);
    }

    uint32_t profile_size = 0U;
    if (!detail::icc_profile_measure(
            tags, detail::IccProfileFinalTagPadding::FourByte, &profile_size)) {
        return icc_serialize_error(IccProfileSerializeStatus::LimitExceeded);
    }
    if (static_cast<uint64_t>(profile_size) > options.max_output_bytes) {
        return icc_serialize_error(IccProfileSerializeStatus::LimitExceeded);
    }

    IccProfileSerializeResult result;
    result.needed                   = profile_size;
    result.tags_serialized          = static_cast<uint32_t>(tags.size());
    result.header_fields_serialized = selected_header_count;
    if (output.empty()) {
        result.status = IccProfileSerializeStatus::OutputTruncated;
        return result;
    }

    std::vector<std::byte> profile;
    if (!detail::build_icc_profile_bytes_from_items(
            header, tags, detail::IccProfileFinalTagPadding::FourByte, &profile)
        || profile.size() != static_cast<size_t>(profile_size)) {
        return icc_serialize_error(IccProfileSerializeStatus::LimitExceeded);
    }

    const size_t copied = std::min(output.size(), profile.size());
    if (copied != 0U) {
        std::memcpy(output.data(), profile.data(), copied);
    }
    result.written = copied;
    result.status  = copied == profile.size()
                         ? IccProfileSerializeStatus::Ok
                         : IccProfileSerializeStatus::OutputTruncated;
    return result;
}


const char*
icc_profile_serialize_status_name(IccProfileSerializeStatus status) noexcept
{
    switch (status) {
    case IccProfileSerializeStatus::Ok: return "ok";
    case IccProfileSerializeStatus::OutputTruncated: return "output_truncated";
    case IccProfileSerializeStatus::InvalidOptions: return "invalid_options";
    case IccProfileSerializeStatus::StoreNotFinalized:
        return "store_not_finalized";
    case IccProfileSerializeStatus::InvalidMetadata: return "invalid_metadata";
    case IccProfileSerializeStatus::NoIccData: return "no_icc_data";
    case IccProfileSerializeStatus::LimitExceeded: return "limit_exceeded";
    }
    return "unknown";
}

}  // namespace openmeta
