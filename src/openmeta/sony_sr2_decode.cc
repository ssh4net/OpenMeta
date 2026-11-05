// SPDX-License-Identifier: Apache-2.0

#include "sony_sr2_decode_internal.h"

#include <array>
#include <cstring>
#include <vector>

namespace openmeta::exif_internal {
namespace {

    static void mark_sr2_limit(ExifDecodeResult* result, ExifLimitReason reason,
                               uint64_t offset, uint16_t tag) noexcept
    {
        update_status(result, ExifDecodeStatus::LimitExceeded);
        if (result && result->limit_reason == ExifLimitReason::None) {
            result->limit_reason = reason;
            result->limit_ifd_offset = offset;
            result->limit_tag = tag;
        }
    }

    static bool sr2_field_bytes(SourceTiffReader* source, const ClassicIfdEntry& entry,
                                uint64_t entry_offset, std::span<const std::byte> raw,
                                std::span<const std::byte>* value,
                                ExifDecodeResult* result) noexcept
    {
        ClassicIfdValueRef ref;
        if (!resolve_classic_ifd_value_ref(OffsetPolicy {}, entry_offset,
                                           entry, &ref, result)) {
            return false;
        }
        if (ref.inline_value) {
            *value = raw.subspan(8U, static_cast<size_t>(ref.value_bytes));
            return true;
        }
        if (!source_tiff_contains(*source, ref.value_off, ref.value_bytes)) {
            update_status(result, ExifDecodeStatus::Malformed);
            return false;
        }
        return source_tiff_value(source, ref.value_off, ref.value_bytes, value);
    }

}  // namespace

bool sony_sr2_decrypt(std::span<std::byte> bytes, uint32_t key) noexcept
{
    if ((bytes.size() & 3U) != 0U) {
        return false;
    }
    std::array<uint32_t, 128> state {};
    for (size_t i = 0U; i < 4U; ++i) {
        key = key * 48828125U + 1U;
        state[i] = key;
    }
    state[3] = (state[3] << 1U) | ((state[0] ^ state[2]) >> 31U);
    for (size_t i = 4U; i < 127U; ++i) {
        state[i] = ((state[i - 4U] ^ state[i - 2U]) << 1U)
                   | ((state[i - 3U] ^ state[i - 1U]) >> 31U);
    }
    for (size_t word = 0U; word < bytes.size() / 4U; ++word) {
        const size_t cursor = (word + 127U) & 127U;
        const uint32_t mask = state[(cursor + 1U) & 127U]
                              ^ state[(cursor + 65U) & 127U];
        state[cursor] = mask;
        for (size_t byte = 0U; byte < 4U; ++byte) {
            bytes[word * 4U + byte] ^= static_cast<std::byte>(
                (mask >> (24U - static_cast<uint32_t>(byte) * 8U)) & 0xffU);
        }
    }
    return true;
}

void decode_sony_sr2_subifds(SourceTiffReader* source, const TiffConfig& cfg,
                            uint32_t root_offset, uint16_t root_count,
                            MetaStore& store, const ExifDecodeOptions& options,
                            ExifDecodeResult* result) noexcept
{
    std::array<uint32_t, 128> offsets {};
    uint32_t offset_count = 0U, length = 0U, key = 0U;
    bool have_offsets = false, have_length = false, have_key = false;
    for (uint32_t i = 0U; i < root_count; ++i) {
        const uint64_t at = uint64_t(root_offset) + 2U + uint64_t(i) * 12U;
        std::span<const std::byte> raw;
        if (!source_tiff_view(source, at, 12U, &raw)) {
            return;
        }
        ClassicIfdEntry entry;
        if (!read_classic_ifd_entry(cfg, raw, 0U, &entry)) {
            update_status(result, ExifDecodeStatus::Malformed);
            return;
        }
        if (entry.tag != 0x7200U && entry.tag != 0x7201U && entry.tag != 0x7221U) {
            continue;
        }
        const bool offset_field = entry.tag == 0x7200U;
        const bool key_field = entry.tag == 0x7221U;
        const bool type_ok = offset_field
            ? (entry.type == 4U || entry.type == 13U)
            : ((entry.type == 4U && entry.count32 == 1U)
               || (key_field && entry.type == 7U && entry.count32 == 4U));
        if (!type_ok || entry.count32 == 0U
            || (offset_field && have_offsets) || (key_field && have_key)
            || (entry.tag == 0x7201U && have_length)) {
            update_status(result, ExifDecodeStatus::Malformed);
            return;
        }
        if (offset_field && (entry.count32 > offsets.size()
                             || entry.count32 > options.limits.max_ifds)) {
            mark_sr2_limit(result, ExifLimitReason::MaxIfds, root_offset, entry.tag);
            return;
        }
        if (uint64_t(entry.count32) * tiff_type_size(entry.type)
            > options.limits.max_value_bytes) {
            mark_sr2_limit(result, ExifLimitReason::ValueCountTooLarge,
                            root_offset, entry.tag);
            return;
        }
        std::span<const std::byte> value;
        if (!sr2_field_bytes(source, entry, at, raw, &value, result)) {
            return;
        }
        if (offset_field) {
            offset_count = entry.count32;
            for (uint32_t j = 0U; j < offset_count; ++j) {
                if (!read_tiff_u32(cfg, value, uint64_t(j) * 4U, &offsets[j])) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return;
                }
            }
            have_offsets = true;
        } else if (key_field) {
            have_key = read_tiff_u32(cfg, value, 0U, &key);
        } else {
            have_length = read_tiff_u32(cfg, value, 0U, &length);
        }
    }
    if (!have_offsets && !have_length && !have_key) {
        return;
    }
    if (!have_offsets || !have_length || !have_key || length < 20U
        || (length & 3U) != 0U || offsets[0] == 0U
        || !source_tiff_contains(*source, offsets[0], length)) {
        update_status(result, ExifDecodeStatus::Malformed);
        return;
    }
    if (length > options.limits.max_value_bytes) {
        mark_sr2_limit(result, ExifLimitReason::ValueCountTooLarge, offsets[0], 0x7201U);
        return;
    }
    if (!source->range->source.contiguous_data && source->value_scratch.size() < length) {
        if (source->result->value_scratch_needed < length)
            source->result->value_scratch_needed = length;
        update_status(result, ExifDecodeStatus::OutputTruncated);
        return;
    }
    std::span<const std::byte> encrypted;
    if (!source_tiff_value(source, offsets[0], length, &encrypted)) {
        return;
    }
    std::vector<std::byte> owned;
    std::span<std::byte> plaintext;
    if (source->range->source.contiguous_data) {
        owned.assign(encrypted.begin(), encrypted.end());
        plaintext = owned;
    } else {
        plaintext = source->value_scratch.first(length);
    }
    (void)sony_sr2_decrypt(plaintext, key);

    const RandomAccessSourceRange range = make_random_access_source_range(
        make_memory_random_access_source(plaintext));
    ExifRandomAccessDecodeResult local;
    SourceTiffReader reader;
    reader.range = &range;
    reader.result = &local;
    OffsetPolicy policy;
    policy.out_of_line_base_is_signed = true;
    policy.out_of_line_base_i64 = -static_cast<int64_t>(offsets[0]);
    ExifDecodeOptions child_options = options;
    child_options.decode_makernote = false;
    child_options.decode_embedded_containers = false;
    for (uint32_t i = 0U; i < offset_count; ++i) {
        bool repeated = false;
        for (uint32_t j = 0U; j < i; ++j) repeated |= offsets[j] == offsets[i];
        if (repeated) continue;
        if (offsets[i] < offsets[0] || uint64_t(offsets[i]) - offsets[0] >= length) {
            update_status(result, ExifDecodeStatus::Malformed);
            return;
        }
        const uint64_t local_offset = uint64_t(offsets[i]) - offsets[0];
        uint16_t count = 0U;
        if (!read_tiff_u16(cfg, plaintext, local_offset, &count) || count == 0U
            || local_offset + 2U + uint64_t(count) * 12U + 4U > length) {
            update_status(result, ExifDecodeStatus::Malformed);
            return;
        }
        if (count > options.limits.max_entries_per_ifd) {
            mark_sr2_limit(result, ExifLimitReason::MaxEntriesPerIfd, offsets[i], 0x7200U);
            return;
        }
        if (uint64_t(result->ifds_needed) + source->private_ifds_visited
            >= options.limits.max_ifds) {
            mark_sr2_limit(result, ExifLimitReason::MaxIfds, offsets[i], 0x7200U);
            return;
        }
        ++source->private_ifds_visited;
        char token[64];
        const std::string_view ifd = make_mk_subtable_ifd_token(
            "mk_sony", "sr2subifd", i, token);
        if (!decode_classic_ifd_from_source(&reader, cfg, local_offset, policy,
                                             ifd, store, child_options, result,
                                             EntryFlags::Derived)) {
            update_status(result, ExifDecodeStatus::Malformed);
            return;
        }
        if (local.decode.status == ExifDecodeStatus::LimitExceeded
            || local.decode.status == ExifDecodeStatus::Malformed) {
            update_status(result, local.decode.status);
            return;
        }
    }
}

}  // namespace openmeta::exif_internal
