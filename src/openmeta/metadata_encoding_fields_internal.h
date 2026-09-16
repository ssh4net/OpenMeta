// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openmeta/meta_store.h"

#include <array>
#include <cstring>
#include <span>
#include <string_view>

namespace openmeta::detail {

inline constexpr bool
encoding_tag(uint16_t tag) noexcept
{
    return tag == 0xa500U || tag == 0x9101U || tag == 0x9102U;
}

inline constexpr bool
composite_tag(uint16_t tag) noexcept
{
    return tag >= 0xa460U && tag <= 0xa462U;
}

inline constexpr std::array<std::string_view, 9> kCompositeMembers
    = { "TotalExposurePeriod",      "SumOfExposureTimesOfAll",
        "SumOfExposureTimesOfUsed", "MaxExposureTimesOfAll",
        "MaxExposureTimesOfUsed",   "MinExposureTimesOfAll",
        "MinExposureTimesOfUsed",   "NumberOfSequences",
        "NumberOfImagesInSequences" };

// Only these exact XMP summary slots define 0/0 as unavailable.
inline bool
composite_unknown_summary(const ByteArena& arena, const Entry& entry) noexcept
{
    if (entry.key.kind != MetaKeyKind::XmpProperty
        || entry.value.kind != MetaValueKind::Scalar || entry.value.count != 1U)
        return false;
    const auto ns_bytes   = arena.span(entry.key.data.xmp_property.schema_ns);
    const auto path_bytes = arena.span(
        entry.key.data.xmp_property.property_path);
    const std::string_view ns(reinterpret_cast<const char*>(ns_bytes.data()),
                              ns_bytes.size());
    std::string_view path(reinterpret_cast<const char*>(path_bytes.data()),
                          path_bytes.size());
    if (ns != "http://cipa.jp/exif/1.0/"
        && ns != "http://ns.adobe.com/exif/1.0/")
        return false;
    constexpr std::array<std::string_view, 2> roots = {
        "SourceExposureTimesOfCompositeImage/", "CompositeImageExposureTimes/"
    };
    for (const auto root : roots) {
        if (!path.starts_with(root))
            continue;
        path.remove_prefix(root.size());
        if (path.starts_with("exifEX:"))
            path.remove_prefix(7U);
        for (size_t i = 0U; i < 7U; ++i)
            if (path == kCompositeMembers[i])
                return true;
        return false;
    }
    return false;
}

inline uint32_t
composite_uint(std::span<const std::byte> raw, size_t offset, unsigned size,
               bool little) noexcept
{
    uint32_t result = 0U;
    for (unsigned i = 0U; i < size; ++i) {
        const unsigned shift = (little ? i : size - 1U - i) * 8U;
        result
            |= static_cast<uint32_t>(std::to_integer<uint8_t>(raw[offset + i]))
               << shift;
    }
    return result;
}

inline URational
composite_rational(std::span<const std::byte> raw, size_t offset,
                   bool little) noexcept
{
    return { composite_uint(raw, offset, 4U, little),
             composite_uint(raw, offset + 4U, 4U, little) };
}

inline bool
composite_payload_valid(std::span<const std::byte> raw, bool little,
                        uint32_t* source_count = nullptr) noexcept
{
    if (raw.size() < 58U)
        return false;
    for (size_t i = 0U; i < 7U; ++i) {
        const URational r = composite_rational(raw, i * 8U, little);
        if (r.denom == 0U && r.numer != 0U)
            return false;
    }
    const uint32_t sequences = composite_uint(raw, 56U, 2U, little);
    if (sequences == 0U) {
        if (source_count)
            *source_count = 0U;
        return raw.size() == 58U;
    }
    if (raw.size() < 60U)
        return false;
    const uint32_t images = composite_uint(raw, 58U, 2U, little);
    const uint64_t count  = static_cast<uint64_t>(sequences) * images;
    if (images == 0U || count < 2U || raw.size() != 60ULL + count * 8ULL)
        return false;
    for (size_t offset = 60U; offset < raw.size(); offset += 8U)
        if (composite_rational(raw, offset, little).denom == 0U)
            return false;
    if (source_count)
        *source_count = static_cast<uint32_t>(count);
    return true;
}

// Validation precedes every swap so malformed input remains byte-for-byte intact.
inline bool
composite_swap_bytes(std::span<std::byte> raw, bool little) noexcept
{
    if (!composite_payload_valid(raw, little))
        return false;
    for (size_t offset = 0U; offset < raw.size();) {
        const size_t width = offset == 56U || offset == 58U ? 2U : 4U;
        for (size_t i = 0U; i < width / 2U; ++i) {
            const std::byte temp         = raw[offset + i];
            raw[offset + i]              = raw[offset + width - 1U - i];
            raw[offset + width - 1U - i] = temp;
        }
        offset += width;
    }
    return true;
}

inline bool
encoding_value_valid(const ByteArena& arena, uint16_t tag,
                     const MetaValue& value) noexcept
{
    if (tag == 0x9101U) {
        if (value.kind != MetaValueKind::Bytes || value.count != 4U
            || value.data.span.size != 4U)
            return false;
        const std::span<const std::byte> raw = arena.span(value.data.span);
        if (raw.size() != 4U)
            return false;
        for (std::byte code : raw)
            if (std::to_integer<uint8_t>(code) > 6U)
                return false;
        return true;
    }
    return (tag == 0xa500U || tag == 0x9102U)
           && value.kind == MetaValueKind::Scalar && value.count == 1U
           && value.elem_type == MetaElementType::URational
           && value.data.ur.denom != 0U;
}

inline bool
composite_value_valid(const ByteArena& arena, uint16_t tag,
                      const MetaValue& value, EntryFlags flags) noexcept
{
    if (tag == 0xa460U)
        return value.kind == MetaValueKind::Scalar && value.count == 1U
               && value.elem_type == MetaElementType::U16
               && value.data.u64 <= 3U;
    if ((tag == 0xa461U && value.kind != MetaValueKind::Array)
        || (tag == 0xa462U && value.kind != MetaValueKind::Bytes))
        return false;
    const std::span<const std::byte> raw = arena.span(value.data.span);
    if (tag == 0xa461U) {
        if (value.kind != MetaValueKind::Array
            || value.elem_type != MetaElementType::U16 || value.count != 2U
            || value.data.span.size != 4U || raw.size() != 4U)
            return false;
        std::array<uint16_t, 2> counts {};
        std::memcpy(counts.data(), raw.data(), 4U);
        return counts[0] >= 2U
               && (counts[1] == 0U
                   || (counts[1] >= 2U && counts[1] <= counts[0]));
    }
    return tag == 0xa462U && value.kind == MetaValueKind::Bytes
           && value.count == value.data.span.size && raw.size() == value.count
           && composite_payload_valid(raw,
                                      !any(flags, EntryFlags::ValueBigEndian));
}

inline bool
primary_exif_entry(const ByteArena& arena, const Entry& entry,
                   uint16_t tag) noexcept
{
    if (entry.key.kind != MetaKeyKind::ExifTag
        || entry.key.data.exif_tag.tag != tag
        || any(entry.flags, EntryFlags::Deleted))
        return false;
    const std::span<const std::byte> raw = arena.span(
        entry.key.data.exif_tag.ifd);
    return raw.size() == 7U && std::memcmp(raw.data(), "exififd", 7U) == 0;
}

inline bool
composite_group_valid(const ByteArena& arena,
                      std::span<const Entry> entries) noexcept
{
    std::array<const Entry*, 3> group {};
    for (const Entry& entry : entries) {
        if (entry.key.kind != MetaKeyKind::ExifTag)
            continue;
        const uint16_t tag = entry.key.data.exif_tag.tag;
        if (!composite_tag(tag) || !primary_exif_entry(arena, entry, tag))
            continue;
        if (group[tag - 0xa460U]
            || !composite_value_valid(arena, tag, entry.value, entry.flags))
            return false;
        group[tag - 0xa460U] = &entry;
    }
    if (!group[0])
        return !group[1] && !group[2];
    const uint64_t code = group[0]->value.data.u64;
    if (code < 2U)
        return !group[1] && !group[2];
    if (code == 3U && (!group[1] || !group[2]))
        return false;
    if (group[1] && group[2]) {
        uint16_t total = 0U;
        std::memcpy(&total, arena.span(group[1]->value.data.span).data(),
                    sizeof(total));
        uint32_t listed = 0U;
        if (!composite_payload_valid(arena.span(group[2]->value.data.span),
                                     !any(group[2]->flags,
                                          EntryFlags::ValueBigEndian),
                                     &listed))
            return false;
        if (listed != 0U && listed != total)
            return false;
    }
    return true;
}

}  // namespace openmeta::detail
