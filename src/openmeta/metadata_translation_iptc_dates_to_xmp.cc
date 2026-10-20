// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_translation.h"

#include "metadata_logical_field_internal.h"

#include "openmeta/meta_edit.h"
#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace openmeta {
namespace {

    using Status  = MetadataIptcDateToXmpStatus;
    using Mapping = MetadataDateTranslationMapping;

    static constexpr std::string_view kPhotoshopNs
        = "http://ns.adobe.com/photoshop/1.0/";
    static constexpr std::string_view kXmpNs  = "http://ns.adobe.com/xap/1.0/";
    static constexpr uint8_t kKnownEntryFlags = 0x7fU;

    struct PairDescriptor final {
        uint16_t date_dataset = 0U;
        uint16_t time_dataset = 0U;
        Mapping mapping       = Mapping::None;
        std::string_view schema_ns;
        std::string_view property_path;
    };

    static constexpr std::array<PairDescriptor, 2U> kPairDescriptors = { {
        { 55U, 60U, Mapping::PhotoshopDateCreated, kPhotoshopNs, "DateCreated" },
        { 62U, 63U, Mapping::XmpCreateDate, kXmpNs, "CreateDate" },
    } };

    struct NativeMember final {
        bool active           = false;
        EntryId active_entry  = kInvalidEntryId;
        BlockId active_block  = kInvalidBlockId;
        uint32_t active_order = 0U;
        std::string_view active_text;
        TextEncoding active_encoding = TextEncoding::Unknown;
        bool dirty_tombstone         = false;
        EntryId tombstone_entry      = kInvalidEntryId;
    };

    enum class Action : uint8_t {
        None,
        Add,
        Update,
        Remove,
        Preserve,
    };

    struct PairPlan final {
        const PairDescriptor* descriptor = nullptr;
        bool enabled                     = false;
        bool selected                    = false;
        NativeMember date;
        NativeMember time;

        bool destination_root_present = false;
        bool destination_present      = false;
        EntryId destination_entry     = kInvalidEntryId;
        std::string_view destination_text;
        TextEncoding destination_encoding = TextEncoding::Unknown;

        bool has_output = false;
        std::array<char, 25U> output_storage {};
        std::string_view output_text;
        Action action = Action::None;
    };

    struct DateParts final {
        uint16_t year     = 0U;
        uint8_t month     = 0U;
        uint8_t day       = 0U;
        uint8_t precision = 0U;
    };

    struct TimeParts final {
        uint8_t hour          = 0U;
        uint8_t minute        = 0U;
        uint8_t second        = 0U;
        bool has_offset       = false;
        char offset_sign      = '+';
        uint8_t offset_hour   = 0U;
        uint8_t offset_minute = 0U;
    };

    static MetadataIptcDateToXmpResult
    failure(Status status, Mapping mapping = Mapping::None,
            EntryId source_entry = kInvalidEntryId) noexcept
    {
        MetadataIptcDateToXmpResult result;
        result.status              = status;
        result.failed_mapping      = mapping;
        result.failed_source_entry = source_entry;
        return result;
    }

    static bool
    valid_options(const MetadataIptcDateToXmpOptions& options) noexcept
    {
        const bool source_mode_valid
            = options.source_mode == MetadataDateTranslationSourceMode::All
              || options.source_mode
                     == MetadataDateTranslationSourceMode::DirtyOnly;
        const bool conflict_policy_valid
            = options.conflict_policy
                  == MetadataIptcToXmpConflictPolicy::PreserveExisting
              || options.conflict_policy
                     == MetadataIptcToXmpConflictPolicy::FailOnConflict
              || options.conflict_policy
                     == MetadataIptcToXmpConflictPolicy::ReplaceExisting;
        return source_mode_valid && conflict_policy_valid
               && options.max_source_properties > 0U
               && options.max_source_properties
                      <= kMetadataIptcDateToXmpMaxSourceProperties
               && options.max_added_entries > 0U
               && options.max_added_entries
                      <= kMetadataIptcDateToXmpMaxAddedEntries
               && options.max_operations > 0U
               && options.max_operations <= kMetadataIptcDateToXmpMaxOperations
               && options.max_total_text_bytes > 0U
               && options.max_total_text_bytes
                      <= kMetadataIptcDateToXmpMaxTotalTextBytes;
    }

    static bool find_native_pair(const Entry& entry,
                                 const std::array<PairPlan, 2U>& plans,
                                 size_t* out_pair, bool* out_is_date) noexcept
    {
        if (entry.key.kind != MetaKeyKind::IptcDataset
            || entry.key.data.iptc_dataset.record != 2U) {
            return false;
        }
        const uint16_t dataset = entry.key.data.iptc_dataset.dataset;
        for (size_t i = 0U; i < plans.size(); ++i) {
            if (!plans[i].enabled) {
                continue;
            }
            if (dataset == plans[i].descriptor->date_dataset) {
                *out_pair    = i;
                *out_is_date = true;
                return true;
            }
            if (dataset == plans[i].descriptor->time_dataset) {
                *out_pair    = i;
                *out_is_date = false;
                return true;
            }
        }
        return false;
    }

    static bool view_arena_span(const MetaStore& store, ByteSpan span,
                                std::span<const std::byte>* out_view) noexcept
    {
        if (!out_view) {
            return false;
        }
        const std::span<const std::byte> bytes = store.arena().bytes();
        const size_t offset = static_cast<size_t>(span.offset);
        const size_t size   = static_cast<size_t>(span.size);
        if (offset > bytes.size() || size > bytes.size() - offset) {
            return false;
        }
        *out_view = bytes.subspan(offset, size);
        return true;
    }

    static std::string_view
    bytes_as_text(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.empty()) {
            return {};
        }
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

    static bool add_text_budget(uint64_t bytes,
                                const MetadataIptcDateToXmpOptions& options,
                                uint64_t* total) noexcept
    {
        if (!total || bytes > options.max_total_text_bytes
            || *total > options.max_total_text_bytes - bytes) {
            return false;
        }
        *total += bytes;
        return true;
    }

    static EntryId representative_source_entry(const PairPlan& plan) noexcept
    {
        if (plan.date.active) {
            return plan.date.active_entry;
        }
        if (plan.time.active) {
            return plan.time.active_entry;
        }
        if (plan.date.dirty_tombstone) {
            return plan.date.tombstone_entry;
        }
        if (plan.time.dirty_tombstone) {
            return plan.time.tombstone_entry;
        }
        return kInvalidEntryId;
    }

    static bool
    preflight_native_value(const MetaStore& source, const Entry& entry,
                           PairPlan* plan, NativeMember* member,
                           uint64_t* total_text_bytes,
                           const MetadataIptcDateToXmpOptions& options,
                           MetadataIptcDateToXmpResult* error) noexcept
    {
        if (any(entry.flags, EntryFlags::Truncated)
            || any(entry.flags, EntryFlags::Unreadable)) {
            *error = failure(Status::InvalidSourceValue,
                             plan->descriptor->mapping, member->active_entry);
            return false;
        }
        if (entry.origin.block != kInvalidBlockId
            && entry.origin.block >= source.block_count()) {
            *error = failure(Status::InvalidSourceValue,
                             plan->descriptor->mapping, member->active_entry);
            return false;
        }
        if (entry.value.kind != MetaValueKind::Bytes
            && entry.value.kind != MetaValueKind::Text) {
            *error = failure(Status::InvalidSourceValue,
                             plan->descriptor->mapping, member->active_entry);
            return false;
        }
        if (entry.value.kind == MetaValueKind::Text
            && entry.value.text_encoding != TextEncoding::Ascii
            && entry.value.text_encoding != TextEncoding::Utf8) {
            *error = failure(Status::InvalidSourceValue,
                             plan->descriptor->mapping, member->active_entry);
            return false;
        }

        const ByteSpan payload = entry.value.data.span;
        if (entry.value.count != payload.size) {
            *error = failure(Status::InvalidSourceValue,
                             plan->descriptor->mapping, member->active_entry);
            return false;
        }
        std::span<const std::byte> bytes;
        if (!view_arena_span(source, payload, &bytes)) {
            *error = failure(Status::InvalidSourceValue,
                             plan->descriptor->mapping, member->active_entry);
            return false;
        }
        if (!add_text_budget(bytes.size(), options, total_text_bytes)) {
            *error = failure(Status::SourceLimitExceeded,
                             plan->descriptor->mapping, member->active_entry);
            return false;
        }
        member->active_text     = bytes_as_text(bytes);
        member->active_encoding = entry.value.kind == MetaValueKind::Text
                                      ? entry.value.text_encoding
                                      : TextEncoding::Unknown;
        member->active_block    = entry.origin.block;
        member->active_order    = entry.origin.order_in_block;
        return true;
    }

    static bool pair_root_matches(std::string_view path, std::string_view root,
                                  bool* out_exact) noexcept
    {
        if (!out_exact || path.size() < root.size()
            || path.substr(0U, root.size()) != root) {
            return false;
        }
        *out_exact = path.size() == root.size();
        if (*out_exact) {
            return true;
        }
        return path[root.size()] == '[' || path[root.size()] == '/';
    }

    static bool
    preflight_destination_value(const MetaStore& source, const Entry& entry,
                                PairPlan* plan, uint64_t* total_text_bytes,
                                const MetadataIptcDateToXmpOptions& options,
                                MetadataIptcDateToXmpResult* error) noexcept
    {
        const uint8_t flags = static_cast<uint8_t>(entry.flags);
        if ((flags & static_cast<uint8_t>(~kKnownEntryFlags)) != 0U
            || any(entry.flags, EntryFlags::Truncated)
            || any(entry.flags, EntryFlags::Unreadable)
            || entry.value.kind != MetaValueKind::Text
            || (entry.value.text_encoding != TextEncoding::Ascii
                && entry.value.text_encoding != TextEncoding::Utf8)) {
            *error = failure(Status::UnsupportedDestinationShape,
                             plan->descriptor->mapping,
                             representative_source_entry(*plan));
            return false;
        }
        const ByteSpan payload = entry.value.data.span;
        if (entry.value.count != payload.size) {
            *error = failure(Status::UnsupportedDestinationShape,
                             plan->descriptor->mapping,
                             representative_source_entry(*plan));
            return false;
        }
        std::span<const std::byte> bytes;
        if (!view_arena_span(source, payload, &bytes)) {
            *error = failure(Status::UnsupportedDestinationShape,
                             plan->descriptor->mapping,
                             representative_source_entry(*plan));
            return false;
        }
        if (!add_text_budget(bytes.size(), options, total_text_bytes)) {
            *error = failure(Status::SourceLimitExceeded,
                             plan->descriptor->mapping,
                             representative_source_entry(*plan));
            return false;
        }
        plan->destination_text     = bytes_as_text(bytes);
        plan->destination_encoding = entry.value.text_encoding;
        return true;
    }

    static bool destination_text_is_valid(const PairPlan& plan) noexcept
    {
        if (plan.destination_encoding == TextEncoding::Ascii) {
            for (const char value : plan.destination_text) {
                if (static_cast<uint8_t>(value) > 0x7fU) {
                    return false;
                }
            }
        }
        return detail::metadata_logical_text_is_valid(plan.destination_text);
    }

    static bool parse_two_digits(std::string_view text, size_t offset,
                                 uint8_t* out_value) noexcept
    {
        if (!out_value || offset + 1U >= text.size()) {
            return false;
        }
        const char first  = text[offset];
        const char second = text[offset + 1U];
        if (first < '0' || first > '9' || second < '0' || second > '9') {
            return false;
        }
        *out_value = static_cast<uint8_t>(static_cast<uint8_t>(first - '0')
                                              * 10U
                                          + static_cast<uint8_t>(second - '0'));
        return true;
    }

    static bool parse_four_digits(std::string_view text, size_t offset,
                                  uint16_t* out_value) noexcept
    {
        if (!out_value || offset + 3U >= text.size()) {
            return false;
        }
        uint16_t value = 0U;
        for (size_t i = 0U; i < 4U; ++i) {
            const char digit = text[offset + i];
            if (digit < '0' || digit > '9') {
                return false;
            }
            value = static_cast<uint16_t>(value * 10U
                                          + static_cast<uint16_t>(digit - '0'));
        }
        *out_value = value;
        return true;
    }

    static uint8_t days_in_month(uint16_t year, uint8_t month) noexcept
    {
        switch (month) {
        case 1U:
        case 3U:
        case 5U:
        case 7U:
        case 8U:
        case 10U:
        case 12U: return 31U;
        case 4U:
        case 6U:
        case 9U:
        case 11U: return 30U;
        case 2U: {
            const bool leap = (year % 400U) == 0U
                              || ((year % 4U) == 0U && (year % 100U) != 0U);
            return leap ? 29U : 28U;
        }
        default: return 0U;
        }
    }

    static Status parse_date(std::string_view text,
                             DateParts* out_date) noexcept
    {
        if (!out_date || text.size() != 8U) {
            return Status::InvalidDateTime;
        }
        DateParts date;
        if (!parse_four_digits(text, 0U, &date.year)
            || !parse_two_digits(text, 4U, &date.month)
            || !parse_two_digits(text, 6U, &date.day)) {
            return Status::InvalidDateTime;
        }
        if (date.month > 12U || date.day > 31U) {
            return Status::InvalidDateTime;
        }
        if (date.year == 0U) {
            return Status::UnsupportedPrecision;
        }
        if (date.month == 0U) {
            if (date.day != 0U) {
                return Status::UnsupportedPrecision;
            }
            date.precision = 1U;
        } else if (date.day == 0U) {
            date.precision = 2U;
        } else {
            if (date.day > days_in_month(date.year, date.month)) {
                return Status::InvalidDateTime;
            }
            date.precision = 3U;
        }
        *out_date = date;
        return Status::Ok;
    }

    static Status parse_time(std::string_view text,
                             TimeParts* out_time) noexcept
    {
        if (!out_time || (text.size() != 6U && text.size() != 11U)) {
            return Status::InvalidDateTime;
        }
        TimeParts time;
        if (!parse_two_digits(text, 0U, &time.hour)
            || !parse_two_digits(text, 2U, &time.minute)
            || !parse_two_digits(text, 4U, &time.second)) {
            return Status::InvalidDateTime;
        }
        if (time.hour > 23U || time.minute > 59U || time.second > 60U) {
            return Status::InvalidDateTime;
        }
        if (text.size() == 11U) {
            time.offset_sign = text[6U];
            if ((time.offset_sign != '+' && time.offset_sign != '-')
                || !parse_two_digits(text, 7U, &time.offset_hour)
                || !parse_two_digits(text, 9U, &time.offset_minute)
                || time.offset_hour > 23U || time.offset_minute > 59U) {
                return Status::InvalidDateTime;
            }
            time.has_offset = true;
        }
        *out_time = time;
        return Status::Ok;
    }

    static bool source_ascii_hint_valid(const NativeMember& member) noexcept
    {
        if (member.active_encoding != TextEncoding::Ascii) {
            return true;
        }
        for (const char value : member.active_text) {
            if (static_cast<uint8_t>(value) > 0x7fU) {
                return false;
            }
        }
        return true;
    }

    static void append_two_digits(std::array<char, 25U>* storage,
                                  size_t* length, uint8_t value) noexcept
    {
        (*storage)[*length] = static_cast<char>('0' + value / 10U);
        ++*length;
        (*storage)[*length] = static_cast<char>('0' + value % 10U);
        ++*length;
    }

    static void append_four_digits(std::array<char, 25U>* storage,
                                   size_t* length, uint16_t value) noexcept
    {
        (*storage)[*length] = static_cast<char>('0' + (value / 1000U) % 10U);
        ++*length;
        (*storage)[*length] = static_cast<char>('0' + (value / 100U) % 10U);
        ++*length;
        (*storage)[*length] = static_cast<char>('0' + (value / 10U) % 10U);
        ++*length;
        (*storage)[*length] = static_cast<char>('0' + value % 10U);
        ++*length;
    }

    static std::string_view
    format_output(const DateParts& date, const TimeParts* time,
                  std::array<char, 25U>* storage) noexcept
    {
        size_t length = 0U;
        append_four_digits(storage, &length, date.year);
        if (date.precision >= 2U) {
            (*storage)[length++] = '-';
            append_two_digits(storage, &length, date.month);
        }
        if (date.precision == 3U) {
            (*storage)[length++] = '-';
            append_two_digits(storage, &length, date.day);
        }
        if (time) {
            (*storage)[length++] = 'T';
            append_two_digits(storage, &length, time->hour);
            (*storage)[length++] = ':';
            append_two_digits(storage, &length, time->minute);
            (*storage)[length++] = ':';
            append_two_digits(storage, &length, time->second);
            if (time->has_offset) {
                (*storage)[length++] = time->offset_sign;
                append_two_digits(storage, &length, time->offset_hour);
                (*storage)[length++] = ':';
                append_two_digits(storage, &length, time->offset_minute);
            }
        }
        return { storage->data(), length };
    }

    static Status
    prepare_pair_output(PairPlan* plan,
                        MetadataIptcDateToXmpResult* error) noexcept
    {
        const Mapping mapping = plan->descriptor->mapping;
        if (!plan->date.active) {
            if (plan->time.active) {
                *error = failure(Status::IncompleteSource, mapping,
                                 plan->time.active_entry);
                return error->status;
            }
            if (plan->date.dirty_tombstone) {
                plan->has_output = false;
                return Status::Ok;
            }
            if (plan->time.dirty_tombstone) {
                *error = failure(Status::IncompleteSource, mapping,
                                 plan->time.tombstone_entry);
                return error->status;
            }
            *error = failure(Status::IncompleteSource, mapping,
                             kInvalidEntryId);
            return error->status;
        }

        if (!source_ascii_hint_valid(plan->date)) {
            *error = failure(Status::InvalidSourceValue, mapping,
                             plan->date.active_entry);
            return error->status;
        }
        DateParts date;
        Status status = parse_date(plan->date.active_text, &date);
        if (status != Status::Ok) {
            *error = failure(status, mapping, plan->date.active_entry);
            return status;
        }

        TimeParts time;
        const TimeParts* time_ptr = nullptr;
        if (plan->time.active) {
            if (!source_ascii_hint_valid(plan->time)) {
                *error = failure(Status::InvalidSourceValue, mapping,
                                 plan->time.active_entry);
                return error->status;
            }
            status = parse_time(plan->time.active_text, &time);
            if (status != Status::Ok) {
                *error = failure(status, mapping, plan->time.active_entry);
                return status;
            }
            if (date.precision != 3U) {
                *error = failure(Status::UnsupportedPrecision, mapping,
                                 plan->time.active_entry);
                return error->status;
            }
            time_ptr = &time;
        }
        plan->output_text = format_output(date, time_ptr,
                                          &plan->output_storage);
        plan->has_output  = true;
        return Status::Ok;
    }

    static Status inspect_destinations(
        const MetaStore& source, std::array<PairPlan, 2U>* plans,
        const MetadataIptcDateToXmpOptions& options, uint64_t* total_text_bytes,
        MetadataIptcDateToXmpResult* error)
    {
        const std::span<const Entry> entries = source.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }

            std::span<const std::byte> schema_bytes;
            if (!view_arena_span(source, entry.key.data.xmp_property.schema_ns,
                                 &schema_bytes)) {
                continue;
            }
            const std::string_view schema = bytes_as_text(schema_bytes);
            for (size_t pair_index = 0U; pair_index < plans->size();
                 ++pair_index) {
                PairPlan& plan = (*plans)[pair_index];
                if (!plan.selected || schema != plan.descriptor->schema_ns) {
                    continue;
                }
                std::span<const std::byte> path_bytes;
                if (!view_arena_span(source,
                                     entry.key.data.xmp_property.property_path,
                                     &path_bytes)) {
                    *error = failure(Status::UnsupportedDestinationShape,
                                     plan.descriptor->mapping,
                                     representative_source_entry(plan));
                    return error->status;
                }
                const std::string_view path = bytes_as_text(path_bytes);
                bool exact_path             = false;
                if (!pair_root_matches(path, plan.descriptor->property_path,
                                       &exact_path)) {
                    continue;
                }
                plan.destination_root_present = true;
                if (options.conflict_policy
                    == MetadataIptcToXmpConflictPolicy::PreserveExisting) {
                    continue;
                }
                if (!exact_path || plan.destination_present) {
                    *error = failure(Status::UnsupportedDestinationShape,
                                     plan.descriptor->mapping,
                                     representative_source_entry(plan));
                    return error->status;
                }
                plan.destination_present = true;
                plan.destination_entry   = static_cast<EntryId>(i);
                if (!preflight_destination_value(source, entry, &plan,
                                                 total_text_bytes, options,
                                                 error)) {
                    return error->status;
                }
            }
        }
        return Status::Ok;
    }

}  // namespace

MetadataIptcDateToXmpResult
translate_iptc_dates_to_xmp(const MetaStore& source,
                            const MetadataIptcDateToXmpOptions& options,
                            MetaStore* out_store)
{
    if (!out_store) {
        return failure(Status::NullOutput);
    }
    if (!source.is_finalized()) {
        return failure(Status::SourceNotFinalized);
    }
    if (!valid_options(options)) {
        return failure(Status::InvalidOptions);
    }

    std::array<PairPlan, 2U> plans {};
    for (size_t i = 0U; i < plans.size(); ++i) {
        plans[i].descriptor = &kPairDescriptors[i];
    }
    plans[0U].enabled = options.created;
    plans[1U].enabled = options.digital_creation;

    const std::span<const Entry> entries = source.entries();
    for (size_t i = 0U; i < entries.size(); ++i) {
        size_t pair_index = 0U;
        bool is_date      = false;
        if (!find_native_pair(entries[i], plans, &pair_index, &is_date)) {
            continue;
        }
        const bool deleted = any(entries[i].flags, EntryFlags::Deleted);
        const bool dirty   = any(entries[i].flags, EntryFlags::Dirty);
        if (dirty
            || (options.source_mode == MetadataDateTranslationSourceMode::All
                && !deleted)) {
            plans[pair_index].selected = true;
        }
        (void)is_date;
    }

    uint32_t selected_source_properties = 0U;
    uint64_t total_text_bytes           = 0U;
    MetadataIptcDateToXmpResult error;
    for (size_t i = 0U; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        size_t pair_index  = 0U;
        bool is_date       = false;
        if (!find_native_pair(entry, plans, &pair_index, &is_date)) {
            continue;
        }
        PairPlan& plan = plans[pair_index];
        if (!plan.selected) {
            continue;
        }
        const bool deleted = any(entry.flags, EntryFlags::Deleted);
        const bool dirty   = any(entry.flags, EntryFlags::Dirty);
        if (deleted && !dirty) {
            continue;
        }
        if (selected_source_properties >= options.max_source_properties
            || selected_source_properties
                   >= kMetadataIptcDateToXmpMaxSourceProperties) {
            return failure(Status::SourceLimitExceeded,
                           plan.descriptor->mapping, static_cast<EntryId>(i));
        }
        ++selected_source_properties;

        const uint8_t flags = static_cast<uint8_t>(entry.flags);
        if ((flags & static_cast<uint8_t>(~kKnownEntryFlags)) != 0U) {
            return failure(Status::InvalidSourceValue, plan.descriptor->mapping,
                           static_cast<EntryId>(i));
        }
        NativeMember& member = is_date ? plan.date : plan.time;
        if (deleted) {
            member.dirty_tombstone = true;
            if (member.tombstone_entry == kInvalidEntryId) {
                member.tombstone_entry = static_cast<EntryId>(i);
            }
            continue;
        }
        if (member.active) {
            return failure(Status::AmbiguousSource, plan.descriptor->mapping,
                           static_cast<EntryId>(i));
        }
        member.active       = true;
        member.active_entry = static_cast<EntryId>(i);
        if (!preflight_native_value(source, entry, &plan, &member,
                                    &total_text_bytes, options, &error)) {
            return error;
        }
    }

    for (PairPlan& plan : plans) {
        if (!plan.selected) {
            continue;
        }
        if (plan.date.active && plan.time.active
            && plan.date.active_block != plan.time.active_block) {
            return failure(Status::AmbiguousSource, plan.descriptor->mapping,
                           plan.time.active_entry);
        }
    }

    Status status = inspect_destinations(source, &plans, options,
                                         &total_text_bytes, &error);
    if (status != Status::Ok) {
        return error;
    }

    for (PairPlan& plan : plans) {
        if (!plan.selected) {
            continue;
        }
        status = prepare_pair_output(&plan, &error);
        if (status != Status::Ok) {
            return error;
        }
        if (plan.destination_present && !destination_text_is_valid(plan)) {
            return failure(Status::UnsupportedDestinationShape,
                           plan.descriptor->mapping,
                           representative_source_entry(plan));
        }
        if (options.conflict_policy
                == MetadataIptcToXmpConflictPolicy::PreserveExisting
            && plan.destination_root_present) {
            plan.action = Action::Preserve;
            continue;
        }
        if (plan.has_output && plan.destination_present) {
            if (plan.output_text == plan.destination_text) {
                plan.action = Action::None;
            } else if (options.conflict_policy
                       == MetadataIptcToXmpConflictPolicy::FailOnConflict) {
                return failure(Status::DestinationConflict,
                               plan.descriptor->mapping,
                               plan.date.active ? plan.date.active_entry
                                                : plan.date.tombstone_entry);
            } else {
                plan.action = Action::Update;
            }
        } else if (plan.has_output) {
            plan.action = Action::Add;
        } else if (plan.destination_present) {
            if (options.conflict_policy
                == MetadataIptcToXmpConflictPolicy::FailOnConflict) {
                return failure(Status::DestinationConflict,
                               plan.descriptor->mapping,
                               plan.date.active ? plan.date.active_entry
                                                : plan.date.tombstone_entry);
            }
            plan.action = Action::Remove;
        }
    }

    uint32_t operation_count = 0U;
    uint32_t added_count     = 0U;
    uint32_t updated_count   = 0U;
    uint32_t removed_count   = 0U;
    for (const PairPlan& plan : plans) {
        switch (plan.action) {
        case Action::None:
        case Action::Preserve: break;
        case Action::Add:
            ++operation_count;
            ++added_count;
            break;
        case Action::Update:
            ++operation_count;
            ++updated_count;
            break;
        case Action::Remove:
            ++operation_count;
            ++removed_count;
            break;
        }
    }
    if (added_count > options.max_added_entries
        || added_count > kMetadataIptcDateToXmpMaxAddedEntries) {
        return failure(Status::EntryLimitExceeded);
    }
    if (operation_count > options.max_operations
        || operation_count > kMetadataIptcDateToXmpMaxOperations) {
        return failure(Status::OperationLimitExceeded);
    }

    MetaEdit edit;
    edit.reserve_ops(operation_count);
    for (const PairPlan& plan : plans) {
        if (plan.action == Action::Add) {
            if (!plan.date.active) {
                return failure(Status::InternalError, plan.descriptor->mapping,
                               plan.date.tombstone_entry);
            }
            Entry added;
            added.key          = make_xmp_property_key(edit.arena(),
                                                       plan.descriptor->schema_ns,
                                                       plan.descriptor->property_path);
            added.value        = make_text(edit.arena(), plan.output_text,
                                           TextEncoding::Utf8);
            added.origin.block = plan.date.active_block;
            added.origin.order_in_block = plan.date.active_order;
            added.flags                 = EntryFlags::Dirty;
            edit.add_entry(added);
        } else if (plan.action == Action::Update) {
            const MetaValue value = make_text(edit.arena(), plan.output_text,
                                              TextEncoding::Utf8);
            edit.set_value(plan.destination_entry, value);
        } else if (plan.action == Action::Remove) {
            edit.tombstone(plan.destination_entry);
        }
    }
    if (edit.arena().limit_exceeded() || edit.ops().size() != operation_count) {
        return failure(Status::InternalError);
    }

    MetaStore built = commit(source, std::span<const MetaEdit>(&edit, 1U));
    if (built.resource_limit_exceeded()) {
        return failure(Status::InternalError);
    }

    MetadataIptcDateToXmpResult result;
    result.source_properties = selected_source_properties;
    for (const PairPlan& plan : plans) {
        if (!plan.selected) {
            continue;
        }
        if (plan.action == Action::Preserve) {
            ++result.groups_preserved;
        } else if (plan.action == Action::None) {
            ++result.groups_unchanged;
        } else {
            ++result.groups_translated;
        }
    }
    *out_store             = std::move(built);
    result.status          = Status::Ok;
    result.entries_added   = added_count;
    result.entries_updated = updated_count;
    result.entries_removed = removed_count;
    return result;
}

const char*
metadata_iptc_date_to_xmp_status_name(MetadataIptcDateToXmpStatus status) noexcept
{
    switch (status) {
    case Status::Ok: return "ok";
    case Status::NullOutput: return "null_output";
    case Status::SourceNotFinalized: return "source_not_finalized";
    case Status::InvalidOptions: return "invalid_options";
    case Status::SourceLimitExceeded: return "source_limit_exceeded";
    case Status::AmbiguousSource: return "ambiguous_source";
    case Status::InvalidSourceValue: return "invalid_source_value";
    case Status::InvalidDateTime: return "invalid_date_time";
    case Status::UnsupportedPrecision: return "unsupported_precision";
    case Status::IncompleteSource: return "incomplete_source";
    case Status::DestinationConflict: return "destination_conflict";
    case Status::UnsupportedDestinationShape:
        return "unsupported_destination_shape";
    case Status::EntryLimitExceeded: return "entry_limit_exceeded";
    case Status::OperationLimitExceeded: return "operation_limit_exceeded";
    case Status::InternalError: return "internal_error";
    }
    return "unknown";
}

}  // namespace openmeta
