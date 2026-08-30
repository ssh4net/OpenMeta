// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_translation.h"

#include "metadata_logical_field_internal.h"

#include "openmeta/meta_edit.h"
#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static constexpr std::string_view kXmpNsDc
        = "http://purl.org/dc/elements/1.1/";
    static constexpr std::string_view kXmpNsPhotoshop
        = "http://ns.adobe.com/photoshop/1.0/";

    struct MappingDescriptor final {
        MetadataDescriptiveTranslationMapping mapping
            = MetadataDescriptiveTranslationMapping::None;
        std::string_view schema_ns;
        std::string_view property_path;
        uint16_t iptc_dataset = 0U;
        uint16_t max_bytes    = 0U;
        bool repeated         = false;
    };

    static constexpr std::array<MappingDescriptor, 7U> kMappings = {
        MappingDescriptor { MetadataDescriptiveTranslationMapping::DcTitle,
                            kXmpNsDc, "title[@xml:lang=x-default]", 5U, 64U,
                            false },
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::DcDescription, kXmpNsDc,
            "description[@xml:lang=x-default]", 120U, 2000U, false },
        MappingDescriptor { MetadataDescriptiveTranslationMapping::DcCreator,
                            kXmpNsDc, "creator", 80U, 32U, true },
        MappingDescriptor { MetadataDescriptiveTranslationMapping::DcSubject,
                            kXmpNsDc, "subject", 25U, 64U, true },
        MappingDescriptor { MetadataDescriptiveTranslationMapping::DcRights,
                            kXmpNsDc, "rights[@xml:lang=x-default]", 116U, 128U,
                            false },
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::PhotoshopCredit,
            kXmpNsPhotoshop, "Credit", 110U, 32U, false },
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::PhotoshopSource,
            kXmpNsPhotoshop, "Source", 115U, 32U, false },
    };

    struct SourceText final {
        EntryId entry_id = kInvalidEntryId;
        uint32_t index   = 0U;
        std::string_view text;
    };

    struct SourceCandidate final {
        EntryId entry_id = kInvalidEntryId;
        uint32_t index   = 0U;
        bool deleted     = false;
    };

    struct PlannedMapping final {
        const MappingDescriptor* descriptor = nullptr;
        std::vector<SourceText> values;
        std::vector<EntryId> native_entries;
        bool eligible  = false;
        bool exact     = false;
        bool apply     = false;
        bool preserved = false;
    };

    static std::string_view arena_text(const ByteArena& arena,
                                       ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool
    mapping_enabled(const MetadataDescriptiveTranslationOptions& options,
                    MetadataDescriptiveTranslationMapping mapping) noexcept
    {
        switch (mapping) {
        case MetadataDescriptiveTranslationMapping::DcTitle:
            return options.title_to_iptc_object_name;
        case MetadataDescriptiveTranslationMapping::DcDescription:
            return options.description_to_iptc_caption;
        case MetadataDescriptiveTranslationMapping::DcCreator:
            return options.creators_to_iptc_bylines;
        case MetadataDescriptiveTranslationMapping::DcSubject:
            return options.keywords_to_iptc_keywords;
        case MetadataDescriptiveTranslationMapping::DcRights:
            return options.copyright_to_iptc_copyright;
        case MetadataDescriptiveTranslationMapping::PhotoshopCredit:
            return options.credit_to_iptc_credit;
        case MetadataDescriptiveTranslationMapping::PhotoshopSource:
            return options.source_to_iptc_source;
        case MetadataDescriptiveTranslationMapping::None: return false;
        }
        return false;
    }

    static bool parse_indexed_path(std::string_view path, std::string_view base,
                                   uint32_t* out_index) noexcept
    {
        if (!out_index || path.size() <= base.size() + 2U
            || path.substr(0U, base.size()) != base || path[base.size()] != '['
            || path.back() != ']') {
            return false;
        }
        uint32_t index = 0U;
        for (size_t i = base.size() + 1U; i + 1U < path.size(); ++i) {
            const char c = path[i];
            if (c < '0' || c > '9') {
                return false;
            }
            const uint32_t digit = static_cast<uint32_t>(c - '0');
            if (index > (std::numeric_limits<uint32_t>::max() - digit) / 10U) {
                return false;
            }
            index = index * 10U + digit;
        }
        if (index == 0U) {
            return false;
        }
        *out_index = index;
        return true;
    }

    static bool source_key_matches(const MetaStore& store, const Entry& entry,
                                   const MappingDescriptor& descriptor,
                                   uint32_t* out_index) noexcept
    {
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || arena_text(store.arena(), entry.key.data.xmp_property.schema_ns)
                   != descriptor.schema_ns) {
            return false;
        }
        const std::string_view path
            = arena_text(store.arena(),
                         entry.key.data.xmp_property.property_path);
        if (!descriptor.repeated) {
            if (path != descriptor.property_path) {
                return false;
            }
            if (out_index) {
                *out_index = 0U;
            }
            return true;
        }
        return parse_indexed_path(path, descriptor.property_path, out_index);
    }

    static bool contains_non_ascii(std::string_view text) noexcept
    {
        for (const char c : text) {
            if (static_cast<unsigned char>(c) >= 0x80U) {
                return true;
            }
        }
        return false;
    }

    static std::span<const std::byte> entry_bytes(const MetaStore& store,
                                                  const Entry& entry) noexcept
    {
        if (entry.value.kind != MetaValueKind::Text
            && entry.value.kind != MetaValueKind::Bytes) {
            return {};
        }
        return store.arena().span(entry.value.data.span);
    }

    static bool entry_value_matches(const MetaStore& store, const Entry& entry,
                                    std::string_view text) noexcept
    {
        const std::span<const std::byte> bytes = entry_bytes(store, entry);
        return bytes.size() == text.size()
               && (bytes.empty()
                   || std::equal(bytes.begin(), bytes.end(),
                                 reinterpret_cast<const std::byte*>(
                                     text.data())));
    }

    static MetadataDescriptiveTranslationResult
    translation_error(MetadataDescriptiveTranslationStatus status,
                      MetadataDescriptiveTranslationMapping mapping
                      = MetadataDescriptiveTranslationMapping::None,
                      EntryId source_entry = kInvalidEntryId) noexcept
    {
        MetadataDescriptiveTranslationResult result;
        result.status              = status;
        result.failed_mapping      = mapping;
        result.failed_source_entry = source_entry;
        return result;
    }

    static MetadataDescriptiveTranslationStatus
    collect_source_mapping(const MetaStore& source,
                           const MetadataDescriptiveTranslationOptions& options,
                           const MappingDescriptor& descriptor,
                           uint32_t* matched_sources,
                           uint64_t* total_text_bytes, PlannedMapping* plan,
                           MetadataDescriptiveTranslationResult* result)
    {
        if (!matched_sources || !total_text_bytes || !plan || !result) {
            return MetadataDescriptiveTranslationStatus::InternalError;
        }
        plan->descriptor = &descriptor;
        if (!mapping_enabled(options, descriptor.mapping)) {
            return MetadataDescriptiveTranslationStatus::Ok;
        }

        bool has_dirty = false;
        std::vector<SourceCandidate> candidates;
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            uint32_t index     = 0U;
            if (!source_key_matches(source, entry, descriptor, &index)) {
                continue;
            }
            const bool dirty   = any(entry.flags, EntryFlags::Dirty);
            const bool deleted = any(entry.flags, EntryFlags::Deleted);
            if (deleted && !dirty) {
                continue;
            }
            has_dirty = has_dirty || dirty;
            if (candidates.size() >= options.max_source_properties
                || candidates.size()
                       >= kMetadataDescriptiveTranslationMaxSourceProperties) {
                result->failed_mapping      = descriptor.mapping;
                result->failed_source_entry = id;
                return MetadataDescriptiveTranslationStatus::SourceLimitExceeded;
            }
            candidates.push_back(SourceCandidate { id, index, deleted });
        }

        if (candidates.empty()
            || (options.source_mode
                    == MetadataDescriptiveTranslationSourceMode::DirtyOnly
                && !has_dirty)) {
            return MetadataDescriptiveTranslationStatus::Ok;
        }
        if (candidates.size() > options.max_source_properties
            || candidates.size()
                   > kMetadataDescriptiveTranslationMaxSourceProperties
            || *matched_sources
                   > options.max_source_properties - candidates.size()
            || *matched_sources
                   > kMetadataDescriptiveTranslationMaxSourceProperties
                         - candidates.size()) {
            result->failed_mapping      = descriptor.mapping;
            result->failed_source_entry = candidates.back().entry_id;
            return MetadataDescriptiveTranslationStatus::SourceLimitExceeded;
        }
        *matched_sources += static_cast<uint32_t>(candidates.size());

        for (const SourceCandidate& candidate : candidates) {
            if (candidate.deleted) {
                continue;
            }
            const Entry& entry = source.entry(candidate.entry_id);
            if (entry.value.kind != MetaValueKind::Text) {
                result->failed_mapping      = descriptor.mapping;
                result->failed_source_entry = candidate.entry_id;
                return MetadataDescriptiveTranslationStatus::InvalidSourceValue;
            }
            const std::string_view text = arena_text(source.arena(),
                                                     entry.value.data.span);
            if (text.empty() || !detail::metadata_logical_text_is_valid(text)) {
                result->failed_mapping      = descriptor.mapping;
                result->failed_source_entry = candidate.entry_id;
                return MetadataDescriptiveTranslationStatus::InvalidSourceValue;
            }
            if (text.size() > descriptor.max_bytes) {
                result->failed_mapping      = descriptor.mapping;
                result->failed_source_entry = candidate.entry_id;
                return MetadataDescriptiveTranslationStatus::ValueTooLong;
            }
            if (text.size() > options.max_total_text_bytes
                || text.size()
                       > kMetadataDescriptiveTranslationMaxTotalTextBytes
                || *total_text_bytes
                       > options.max_total_text_bytes - text.size()
                || *total_text_bytes
                       > kMetadataDescriptiveTranslationMaxTotalTextBytes
                             - text.size()) {
                result->failed_mapping      = descriptor.mapping;
                result->failed_source_entry = candidate.entry_id;
                return MetadataDescriptiveTranslationStatus::SourceLimitExceeded;
            }
            *total_text_bytes += text.size();
            plan->values.push_back(
                SourceText { candidate.entry_id, candidate.index, text });
        }
        plan->eligible = true;
        result->source_properties += static_cast<uint32_t>(candidates.size());

        if (!descriptor.repeated && plan->values.size() > 1U) {
            result->failed_mapping      = descriptor.mapping;
            result->failed_source_entry = plan->values[1U].entry_id;
            return MetadataDescriptiveTranslationStatus::AmbiguousSource;
        }
        if (descriptor.repeated) {
            std::stable_sort(plan->values.begin(), plan->values.end(),
                             [](const SourceText& a, const SourceText& b) {
                                 return a.index < b.index;
                             });
            for (size_t i = 1U; i < plan->values.size(); ++i) {
                if (plan->values[i - 1U].index == plan->values[i].index) {
                    result->failed_mapping      = descriptor.mapping;
                    result->failed_source_entry = plan->values[i].entry_id;
                    return MetadataDescriptiveTranslationStatus::AmbiguousSource;
                }
            }
        }
        return MetadataDescriptiveTranslationStatus::Ok;
    }

    static bool
    native_mapping_matches(const Entry& entry,
                           const MappingDescriptor& descriptor) noexcept
    {
        return entry.key.kind == MetaKeyKind::IptcDataset
               && entry.key.data.iptc_dataset.record == 2U
               && entry.key.data.iptc_dataset.dataset
                      == descriptor.iptc_dataset;
    }

    static MetadataDescriptiveTranslationStatus
    analyze_native_mapping(const MetaStore& source,
                           const MetadataDescriptiveTranslationOptions& options,
                           uint32_t* native_properties, PlannedMapping* plan)
    {
        if (!native_properties || !plan) {
            return MetadataDescriptiveTranslationStatus::InternalError;
        }
        if (!plan->eligible || !plan->descriptor) {
            return MetadataDescriptiveTranslationStatus::Ok;
        }
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (!any(entry.flags, EntryFlags::Deleted)
                && native_mapping_matches(entry, *plan->descriptor)) {
                if (*native_properties >= options.max_operations
                    || *native_properties
                           >= kMetadataDescriptiveTranslationMaxOperations) {
                    return MetadataDescriptiveTranslationStatus::
                        OperationLimitExceeded;
                }
                ++*native_properties;
                plan->native_entries.push_back(id);
            }
        }
        std::stable_sort(
            plan->native_entries.begin(), plan->native_entries.end(),
            [&source](EntryId a, EntryId b) {
                const uint32_t ao = source.entry(a).origin.order_in_block;
                const uint32_t bo = source.entry(b).origin.order_in_block;
                return ao == bo ? a < b : ao < bo;
            });
        plan->exact = plan->native_entries.size() == plan->values.size();
        if (plan->exact) {
            for (size_t i = 0U; i < plan->values.size(); ++i) {
                if (!entry_value_matches(source,
                                         source.entry(plan->native_entries[i]),
                                         plan->values[i].text)) {
                    plan->exact = false;
                    break;
                }
            }
        }
        return MetadataDescriptiveTranslationStatus::Ok;
    }

    static bool mapping_owns_native_entry(
        EntryId id, const std::array<PlannedMapping, 7U>& plans) noexcept
    {
        for (const PlannedMapping& plan : plans) {
            if (!plan.eligible || plan.preserved) {
                continue;
            }
            if (std::find(plan.native_entries.begin(),
                          plan.native_entries.end(), id)
                != plan.native_entries.end()) {
                return true;
            }
        }
        return false;
    }

    static bool
    bytes_contain_non_ascii(std::span<const std::byte> bytes) noexcept
    {
        for (const std::byte byte : bytes) {
            if (std::to_integer<uint8_t>(byte) >= 0x80U) {
                return true;
            }
        }
        return false;
    }

    static MetadataDescriptiveTranslationStatus
    plan_utf8_charset(const MetaStore& source,
                      const std::array<PlannedMapping, 7U>& plans,
                      uint64_t max_inspected_bytes, bool* out_add,
                      EntryId* out_source) noexcept
    {
        if (!out_add || !out_source) {
            return MetadataDescriptiveTranslationStatus::InternalError;
        }
        *out_add    = false;
        *out_source = kInvalidEntryId;

        bool needed = false;
        for (const PlannedMapping& plan : plans) {
            if (!plan.eligible || plan.preserved) {
                continue;
            }
            for (const SourceText& value : plan.values) {
                if (contains_non_ascii(value.text)) {
                    needed = true;
                    if (*out_source == kInvalidEntryId) {
                        *out_source = value.entry_id;
                    }
                }
            }
        }
        if (!needed) {
            return MetadataDescriptiveTranslationStatus::Ok;
        }

        static constexpr std::array<std::byte, 3U> kUtf8Escape
            = { std::byte { 0x1bU }, std::byte { 0x25U }, std::byte { 0x47U } };
        EntryId charset_entry    = kInvalidEntryId;
        uint64_t inspected_bytes = 0U;
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::IptcDataset
                || entry.key.data.iptc_dataset.record != 1U
                || entry.key.data.iptc_dataset.dataset != 90U) {
                continue;
            }
            if (charset_entry != kInvalidEntryId) {
                return MetadataDescriptiveTranslationStatus::NativeEncodingConflict;
            }
            charset_entry = id;
        }
        if (charset_entry != kInvalidEntryId) {
            const std::span<const std::byte> bytes
                = entry_bytes(source, source.entry(charset_entry));
            if (bytes.size() != kUtf8Escape.size()
                || !std::equal(bytes.begin(), bytes.end(),
                               kUtf8Escape.begin())) {
                return MetadataDescriptiveTranslationStatus::NativeEncodingConflict;
            }
            return MetadataDescriptiveTranslationStatus::Ok;
        }

        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::IptcDataset) {
                continue;
            }
            const std::span<const std::byte> bytes = entry_bytes(source, entry);
            if (bytes.size() > max_inspected_bytes
                || inspected_bytes > max_inspected_bytes - bytes.size()) {
                return MetadataDescriptiveTranslationStatus::SourceLimitExceeded;
            }
            inspected_bytes += bytes.size();
            const bool owned = mapping_owns_native_entry(id, plans);
            if (((entry.value.kind != MetaValueKind::Text
                  && entry.value.kind != MetaValueKind::Bytes)
                 && !owned)
                || (bytes_contain_non_ascii(bytes) && !owned)) {
                return MetadataDescriptiveTranslationStatus::NativeEncodingConflict;
            }
        }
        *out_add = true;
        return MetadataDescriptiveTranslationStatus::Ok;
    }

    static MetaValue make_iptc_value(ByteArena& arena,
                                     std::string_view text) noexcept
    {
        return make_bytes(arena,
                          std::span<const std::byte>(
                              reinterpret_cast<const std::byte*>(text.data()),
                              text.size()));
    }

    static bool append_iptc_entry(MetaEdit* edit, const MetaStore& source,
                                  const MappingDescriptor& descriptor,
                                  const SourceText& value) noexcept
    {
        if (!edit || value.entry_id >= source.entries().size()) {
            return false;
        }
        Entry entry;
        entry.key    = make_iptc_dataset_key(2U, descriptor.iptc_dataset);
        entry.value  = make_iptc_value(edit->arena(), value.text);
        entry.origin = source.entry(value.entry_id).origin;
        if (entry.origin.wire_type_name.size > 0U) {
            entry.origin.wire_type_name = edit->arena().append(
                source.arena().span(entry.origin.wire_type_name));
        }
        entry.flags = EntryFlags::Dirty;
        if (edit->arena().limit_exceeded()) {
            return false;
        }
        edit->add_entry(entry);
        return true;
    }

    static bool append_utf8_charset_entry(MetaEdit* edit,
                                          const MetaStore& source,
                                          EntryId source_entry) noexcept
    {
        if (!edit || source_entry >= source.entries().size()) {
            return false;
        }
        static constexpr std::array<std::byte, 3U> kUtf8Escape
            = { std::byte { 0x1bU }, std::byte { 0x25U }, std::byte { 0x47U } };
        Entry entry;
        entry.key                   = make_iptc_dataset_key(1U, 90U);
        entry.value                 = make_bytes(edit->arena(), kUtf8Escape);
        entry.origin                = source.entry(source_entry).origin;
        entry.origin.order_in_block = 0U;
        if (entry.origin.wire_type_name.size > 0U) {
            entry.origin.wire_type_name = edit->arena().append(
                source.arena().span(entry.origin.wire_type_name));
        }
        entry.flags = EntryFlags::Dirty;
        if (edit->arena().limit_exceeded()) {
            return false;
        }
        edit->add_entry(entry);
        return true;
    }

}  // namespace

MetadataDescriptiveTranslationResult
translate_xmp_descriptive_metadata(
    const MetaStore& source,
    const MetadataDescriptiveTranslationOptions& options, MetaStore* out_store)
{
    if (!out_store) {
        return translation_error(
            MetadataDescriptiveTranslationStatus::NullOutput);
    }
    if (!source.is_finalized()) {
        return translation_error(
            MetadataDescriptiveTranslationStatus::SourceNotFinalized);
    }
    const bool any_mapping = options.title_to_iptc_object_name
                             || options.description_to_iptc_caption
                             || options.creators_to_iptc_bylines
                             || options.keywords_to_iptc_keywords
                             || options.copyright_to_iptc_copyright
                             || options.credit_to_iptc_credit
                             || options.source_to_iptc_source;
    if (!any_mapping || options.max_source_properties == 0U
        || options.max_source_properties
               > kMetadataDescriptiveTranslationMaxSourceProperties
        || options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataDescriptiveTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataDescriptiveTranslationMaxOperations
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataDescriptiveTranslationMaxTotalTextBytes
        || (options.source_mode
                != MetadataDescriptiveTranslationSourceMode::DirtyOnly
            && options.source_mode
                   != MetadataDescriptiveTranslationSourceMode::All)
        || (options.conflict_policy
                != MetadataDescriptiveTranslationConflictPolicy::PreserveExisting
            && options.conflict_policy
                   != MetadataDescriptiveTranslationConflictPolicy::FailOnConflict
            && options.conflict_policy
                   != MetadataDescriptiveTranslationConflictPolicy::
                       ReplaceExisting)) {
        return translation_error(
            MetadataDescriptiveTranslationStatus::InvalidOptions);
    }

    std::array<PlannedMapping, 7U> plans;
    uint32_t matched_sources  = 0U;
    uint64_t total_text_bytes = 0U;
    MetadataDescriptiveTranslationResult result;
    for (size_t i = 0U; i < kMappings.size(); ++i) {
        const MetadataDescriptiveTranslationStatus status
            = collect_source_mapping(source, options, kMappings[i],
                                     &matched_sources, &total_text_bytes,
                                     &plans[i], &result);
        if (status != MetadataDescriptiveTranslationStatus::Ok) {
            result.status = status;
            return result;
        }
    }

    uint32_t native_properties = 0U;
    for (PlannedMapping& plan : plans) {
        const MetadataDescriptiveTranslationStatus analyze_status
            = analyze_native_mapping(source, options, &native_properties,
                                     &plan);
        if (analyze_status != MetadataDescriptiveTranslationStatus::Ok) {
            result.status = analyze_status;
            result.failed_mapping
                = plan.descriptor ? plan.descriptor->mapping
                                  : MetadataDescriptiveTranslationMapping::None;
            return result;
        }
        if (!plan.eligible) {
            continue;
        }
        switch (options.conflict_policy) {
        case MetadataDescriptiveTranslationConflictPolicy::PreserveExisting:
            if (!plan.native_entries.empty()) {
                plan.preserved = true;
                ++result.groups_preserved;
            } else if (plan.values.empty()) {
                ++result.groups_unchanged;
            } else {
                plan.apply = true;
            }
            break;
        case MetadataDescriptiveTranslationConflictPolicy::FailOnConflict:
            if (!plan.native_entries.empty() && !plan.exact) {
                result.status
                    = MetadataDescriptiveTranslationStatus::NativeConflict;
                result.failed_mapping      = plan.descriptor->mapping;
                result.failed_source_entry = plan.values.empty()
                                                 ? kInvalidEntryId
                                                 : plan.values.front().entry_id;
                return result;
            }
            if (plan.exact) {
                ++result.groups_unchanged;
            } else {
                plan.apply = true;
            }
            break;
        case MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting:
            if (plan.exact) {
                ++result.groups_unchanged;
            } else {
                plan.apply = true;
            }
            break;
        }
    }

    bool add_utf8_charset = false;
    EntryId utf8_source   = kInvalidEntryId;
    const MetadataDescriptiveTranslationStatus charset_status
        = plan_utf8_charset(source, plans,
                            options.max_total_text_bytes - total_text_bytes,
                            &add_utf8_charset, &utf8_source);
    if (charset_status != MetadataDescriptiveTranslationStatus::Ok) {
        result.status              = charset_status;
        result.failed_source_entry = utf8_source;
        return result;
    }

    uint32_t added_entries   = add_utf8_charset ? 1U : 0U;
    uint32_t operation_count = add_utf8_charset ? 1U : 0U;
    for (const PlannedMapping& plan : plans) {
        if (!plan.apply) {
            continue;
        }
        const size_t overlap = std::min(plan.values.size(),
                                        plan.native_entries.size());
        for (size_t i = 0U; i < overlap; ++i) {
            if (!entry_value_matches(source,
                                     source.entry(plan.native_entries[i]),
                                     plan.values[i].text)) {
                ++operation_count;
            }
        }
        operation_count += static_cast<uint32_t>(plan.native_entries.size()
                                                 - overlap);
        const uint32_t missing = static_cast<uint32_t>(plan.values.size()
                                                       - overlap);
        operation_count += missing;
        added_entries += missing;
    }
    if (added_entries > options.max_added_entries
        || source.entries().size() > static_cast<size_t>(kInvalidEntryId)
        || static_cast<size_t>(added_entries)
               > static_cast<size_t>(kInvalidEntryId)
                     - source.entries().size()) {
        result.status = MetadataDescriptiveTranslationStatus::EntryLimitExceeded;
        return result;
    }
    if (operation_count > options.max_operations) {
        result.status
            = MetadataDescriptiveTranslationStatus::OperationLimitExceeded;
        return result;
    }

    MetaEdit edit;
    edit.reserve_ops(operation_count);
    for (const PlannedMapping& plan : plans) {
        if (!plan.apply || !plan.descriptor) {
            continue;
        }
        const size_t overlap = std::min(plan.values.size(),
                                        plan.native_entries.size());
        for (size_t i = 0U; i < overlap; ++i) {
            if (!entry_value_matches(source,
                                     source.entry(plan.native_entries[i]),
                                     plan.values[i].text)) {
                edit.set_value(plan.native_entries[i],
                               make_iptc_value(edit.arena(),
                                               plan.values[i].text));
                ++result.entries_updated;
            }
        }
        for (size_t i = overlap; i < plan.native_entries.size(); ++i) {
            edit.tombstone(plan.native_entries[i]);
            ++result.entries_removed;
        }
        for (size_t i = overlap; i < plan.values.size(); ++i) {
            if (append_iptc_entry(&edit, source, *plan.descriptor,
                                  plan.values[i])) {
                ++result.entries_added;
            }
        }
        ++result.groups_translated;
    }
    if (add_utf8_charset
        && append_utf8_charset_entry(&edit, source, utf8_source)) {
        ++result.entries_added;
        result.utf8_charset_added = true;
    }
    if (edit.ops().size() != operation_count || edit.arena().limit_exceeded()
        || result.entries_added != added_entries) {
        result.status = MetadataDescriptiveTranslationStatus::InternalError;
        return result;
    }

    *out_store = commit(source, std::span<const MetaEdit>(&edit, 1U));
    return result;
}

const char*
metadata_descriptive_translation_status_name(
    MetadataDescriptiveTranslationStatus status) noexcept
{
    switch (status) {
    case MetadataDescriptiveTranslationStatus::Ok: return "ok";
    case MetadataDescriptiveTranslationStatus::NullOutput: return "null_output";
    case MetadataDescriptiveTranslationStatus::SourceNotFinalized:
        return "source_not_finalized";
    case MetadataDescriptiveTranslationStatus::InvalidOptions:
        return "invalid_options";
    case MetadataDescriptiveTranslationStatus::SourceLimitExceeded:
        return "source_limit_exceeded";
    case MetadataDescriptiveTranslationStatus::AmbiguousSource:
        return "ambiguous_source";
    case MetadataDescriptiveTranslationStatus::InvalidSourceValue:
        return "invalid_source_value";
    case MetadataDescriptiveTranslationStatus::ValueTooLong:
        return "value_too_long";
    case MetadataDescriptiveTranslationStatus::NativeConflict:
        return "native_conflict";
    case MetadataDescriptiveTranslationStatus::NativeEncodingConflict:
        return "native_encoding_conflict";
    case MetadataDescriptiveTranslationStatus::EntryLimitExceeded:
        return "entry_limit_exceeded";
    case MetadataDescriptiveTranslationStatus::OperationLimitExceeded:
        return "operation_limit_exceeded";
    case MetadataDescriptiveTranslationStatus::InternalError:
        return "internal_error";
    }
    return "unknown";
}

const char*
metadata_descriptive_translation_mapping_name(
    MetadataDescriptiveTranslationMapping mapping) noexcept
{
    switch (mapping) {
    case MetadataDescriptiveTranslationMapping::None: return "none";
    case MetadataDescriptiveTranslationMapping::DcTitle: return "dc_title";
    case MetadataDescriptiveTranslationMapping::DcDescription:
        return "dc_description";
    case MetadataDescriptiveTranslationMapping::DcCreator: return "dc_creator";
    case MetadataDescriptiveTranslationMapping::DcSubject: return "dc_subject";
    case MetadataDescriptiveTranslationMapping::DcRights: return "dc_rights";
    case MetadataDescriptiveTranslationMapping::PhotoshopCredit:
        return "photoshop_credit";
    case MetadataDescriptiveTranslationMapping::PhotoshopSource:
        return "photoshop_source";
    }
    return "unknown";
}

}  // namespace openmeta
