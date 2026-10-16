// SPDX-License-Identifier: Apache-2.0

#include "openmeta/iptc_iim_serialize.h"

#include "iptc_iim_serialize_internal.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace openmeta {
namespace {

    struct IptcDatasetItem final {
        EntryId entry_ordinal = kInvalidEntryId;
        uint16_t record       = 0U;
        uint16_t dataset      = 0U;
        uint32_t source_order = 0U;
        std::span<const std::byte> payload;
    };

    static bool iptc_dataset_item_less(const IptcDatasetItem& first,
                                       const IptcDatasetItem& second) noexcept
    {
        const bool first_charset  = first.record == 1U && first.dataset == 90U;
        const bool second_charset = second.record == 1U
                                    && second.dataset == 90U;
        if (first_charset != second_charset) {
            return first_charset;
        }
        if (first.source_order != second.source_order) {
            return first.source_order < second.source_order;
        }
        if (first.record != second.record) {
            return first.record < second.record;
        }
        if (first.dataset != second.dataset) {
            return first.dataset < second.dataset;
        }
        return first.entry_ordinal < second.entry_ordinal;
    }


    static uint32_t iptc_array_element_size(MetaElementType type) noexcept
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


    static bool iptc_value_payload(const MetaStore& store,
                                   const MetaValue& value,
                                   std::span<const std::byte>* payload) noexcept
    {
        if (!payload
            || (value.kind != MetaValueKind::Bytes
                && value.kind != MetaValueKind::Text
                && value.kind != MetaValueKind::Array)) {
            return false;
        }

        const std::span<const std::byte> arena = store.arena().bytes();
        const uint64_t offset                  = value.data.span.offset;
        const uint64_t size                    = value.data.span.size;
        if (offset > arena.size() || size > arena.size() - offset) {
            return false;
        }

        if (value.kind == MetaValueKind::Bytes
            || value.kind == MetaValueKind::Text) {
            if (value.count != size) {
                return false;
            }
        } else if (value.kind == MetaValueKind::Array) {
            const uint32_t element_size = iptc_array_element_size(
                value.elem_type);
            if (element_size == 0U
                || static_cast<uint64_t>(value.count) * element_size != size) {
                return false;
            }
        }

        *payload = arena.subspan(static_cast<size_t>(offset),
                                 static_cast<size_t>(size));
        return true;
    }


    static bool iptc_serialize_options_valid(
        const IptcIimSerializeOptions& options) noexcept
    {
        return options.max_datasets != 0U && options.max_dataset_bytes != 0U
               && options.max_output_bytes != 0U
               && options.max_output_bytes <= UINT32_MAX
               && options.max_output_bytes
                      <= std::numeric_limits<size_t>::max();
    }


    static IptcIimSerializeResult
    iptc_serialize_error(IptcIimSerializeStatus status,
                         EntryId failed_entry = kInvalidEntryId) noexcept
    {
        IptcIimSerializeResult result;
        result.status       = status;
        result.failed_entry = failed_entry;
        return result;
    }


    static void append_u16be(std::vector<std::byte>* output,
                             uint16_t value) noexcept
    {
        output->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
        output->push_back(static_cast<std::byte>(value & 0xFFU));
    }


    static void append_u32be(std::vector<std::byte>* output,
                             uint32_t value) noexcept
    {
        output->push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
        output->push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
        output->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
        output->push_back(static_cast<std::byte>(value & 0xFFU));
    }


    static bool
    iptc_dataset_is_unsupported(const MetaStore& store, const Entry& entry,
                                std::span<const std::byte>* payload) noexcept
    {
        if (any(entry.flags, EntryFlags::Truncated)
            || any(entry.flags, EntryFlags::Unreadable)
            || entry.key.data.iptc_dataset.record > 255U
            || entry.key.data.iptc_dataset.dataset > 255U) {
            return true;
        }
        return !iptc_value_payload(store, entry.value, payload);
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

}  // namespace

IptcIimSerializeResult
detail::build_iptc_iim_payload(const MetaStore& store,
                               const IptcIimSerializeOptions& options,
                               bool skip_unsupported,
                               std::vector<std::byte>* output) noexcept
{
    if (!iptc_serialize_options_valid(options)) {
        return iptc_serialize_error(IptcIimSerializeStatus::InvalidOptions);
    }
    uint32_t active_count                = 0U;
    uint32_t serialized_count            = 0U;
    uint32_t skipped_count               = 0U;
    uint64_t total_output_size           = 0U;
    const std::span<const Entry> entries = store.entries();

    for (size_t index = 0U; index < entries.size(); ++index) {
        const Entry& entry = entries[index];
        if (any(entry.flags, EntryFlags::Deleted)
            || entry.key.kind != MetaKeyKind::IptcDataset) {
            continue;
        }
        if (active_count >= options.max_datasets) {
            return iptc_serialize_error(IptcIimSerializeStatus::LimitExceeded,
                                        static_cast<EntryId>(index));
        }
        active_count += 1U;

        std::span<const std::byte> payload;
        if (iptc_dataset_is_unsupported(store, entry, &payload)) {
            if (!skip_unsupported) {
                return iptc_serialize_error(
                    IptcIimSerializeStatus::InvalidMetadata,
                    static_cast<EntryId>(index));
            }
            skipped_count += 1U;
            continue;
        }
        if (payload.size() > options.max_dataset_bytes) {
            return iptc_serialize_error(IptcIimSerializeStatus::LimitExceeded,
                                        static_cast<EntryId>(index));
        }

        const uint64_t header_size  = payload.size() <= 0x7FFFU ? 5U : 9U;
        const uint64_t dataset_size = header_size + payload.size();
        if (dataset_size > options.max_output_bytes - total_output_size) {
            return iptc_serialize_error(IptcIimSerializeStatus::LimitExceeded,
                                        static_cast<EntryId>(index));
        }
        total_output_size += dataset_size;
        serialized_count += 1U;
    }

    IptcIimSerializeResult result;
    result.needed             = total_output_size;
    result.entries_serialized = serialized_count;
    result.entries_skipped    = skipped_count;
    if (!output) {
        return result;
    }

    std::vector<IptcDatasetItem> items;
    items.reserve(serialized_count);
    for (size_t index = 0U; index < entries.size(); ++index) {
        const Entry& entry = entries[index];
        if (any(entry.flags, EntryFlags::Deleted)
            || entry.key.kind != MetaKeyKind::IptcDataset) {
            continue;
        }
        std::span<const std::byte> payload;
        if (iptc_dataset_is_unsupported(store, entry, &payload)) {
            continue;
        }

        IptcDatasetItem item;
        item.entry_ordinal = static_cast<EntryId>(index);
        item.record        = entry.key.data.iptc_dataset.record;
        item.dataset       = entry.key.data.iptc_dataset.dataset;
        item.source_order  = entry.origin.order_in_block;
        item.payload       = payload;
        items.push_back(item);
    }

    std::sort(items.begin(), items.end(), iptc_dataset_item_less);

    std::vector<std::byte> built;
    built.reserve(static_cast<size_t>(total_output_size));
    for (const IptcDatasetItem& item : items) {
        built.push_back(std::byte { 0x1CU });
        built.push_back(static_cast<std::byte>(item.record));
        built.push_back(static_cast<std::byte>(item.dataset));
        if (item.payload.size() <= 0x7FFFU) {
            append_u16be(&built, static_cast<uint16_t>(item.payload.size()));
        } else {
            append_u16be(&built, 0x8004U);
            append_u32be(&built, static_cast<uint32_t>(item.payload.size()));
        }
        built.insert(built.end(), item.payload.begin(), item.payload.end());
    }

    output->swap(built);
    result.written = total_output_size;
    return result;
}


IptcIimSerializeResult
serialize_iptc_iim(const MetaStore& store, std::span<std::byte> output,
                   const IptcIimSerializeOptions& options) noexcept
{
    if (!iptc_serialize_options_valid(options)) {
        return iptc_serialize_error(IptcIimSerializeStatus::InvalidOptions);
    }
    if (!store.is_finalized()) {
        return iptc_serialize_error(IptcIimSerializeStatus::StoreNotFinalized);
    }

    const std::span<const std::byte> arena = store.arena().bytes();
    if (pointer_ranges_overlap(output.data(), output.size(), arena.data(),
                               arena.size())) {
        return iptc_serialize_error(IptcIimSerializeStatus::InvalidOptions);
    }

    std::vector<std::byte> built;
    IptcIimSerializeResult result
        = detail::build_iptc_iim_payload(store, options, false,
                                         output.empty() ? nullptr : &built);
    if (!result.ok()) {
        return result;
    }
    if (result.entries_serialized == 0U) {
        result.status  = IptcIimSerializeStatus::NoIptcData;
        result.written = 0U;
        result.needed  = 0U;
        return result;
    }
    if (output.empty()) {
        result.status = IptcIimSerializeStatus::OutputTruncated;
        return result;
    }

    const size_t copied = std::min(output.size(), built.size());
    if (copied != 0U) {
        std::memcpy(output.data(), built.data(), copied);
    }
    result.written = copied;
    result.status  = copied == built.size()
                         ? IptcIimSerializeStatus::Ok
                         : IptcIimSerializeStatus::OutputTruncated;
    return result;
}


const char*
iptc_iim_serialize_status_name(IptcIimSerializeStatus status) noexcept
{
    switch (status) {
    case IptcIimSerializeStatus::Ok: return "ok";
    case IptcIimSerializeStatus::OutputTruncated: return "output_truncated";
    case IptcIimSerializeStatus::InvalidOptions: return "invalid_options";
    case IptcIimSerializeStatus::StoreNotFinalized:
        return "store_not_finalized";
    case IptcIimSerializeStatus::InvalidMetadata: return "invalid_metadata";
    case IptcIimSerializeStatus::NoIptcData: return "no_iptc_data";
    case IptcIimSerializeStatus::LimitExceeded: return "limit_exceeded";
    }
    return "unknown";
}

}  // namespace openmeta
