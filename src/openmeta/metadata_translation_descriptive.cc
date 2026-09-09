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
    static constexpr std::string_view kXmpNsIptcCore
        = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
    static constexpr std::string_view kXmpNsIptcExt
        = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";

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

    static constexpr std::array<MappingDescriptor, 5U> kLocationMappings = {
        MappingDescriptor { MetadataDescriptiveTranslationMapping::PhotoshopCity,
                            kXmpNsPhotoshop, "City", 90U, 32U, false },
        MappingDescriptor { MetadataDescriptiveTranslationMapping::IptcLocation,
                            kXmpNsIptcCore, "Location", 92U, 32U, false },
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::PhotoshopState,
            kXmpNsPhotoshop, "State", 95U, 32U, false },
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::PhotoshopCountry,
            kXmpNsPhotoshop, "Country", 101U, 64U, false },
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::IptcCountryCode,
            kXmpNsIptcCore, "CountryCode", 100U, 3U, false },
    };

    static constexpr std::array<MappingDescriptor, 3U> kEditorialMappings = {
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::PhotoshopHeadline,
            kXmpNsPhotoshop, "Headline", 105U, 256U, false },
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::PhotoshopInstructions,
            kXmpNsPhotoshop, "Instructions", 40U, 256U, false },
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::PhotoshopTransmissionReference,
            kXmpNsPhotoshop, "TransmissionReference", 103U, 32U, false },
    };

    static constexpr std::array<MappingDescriptor, 5U> kWorkflowMappings = {
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::PhotoshopAuthorsPosition,
            kXmpNsPhotoshop, "AuthorsPosition", 85U, 32U, false },
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::PhotoshopCaptionWriter,
            kXmpNsPhotoshop, "CaptionWriter", 122U, 32U, false },
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::PhotoshopCategory,
            kXmpNsPhotoshop, "Category", 15U, 3U, false },
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::PhotoshopSupplementalCategories,
            kXmpNsPhotoshop, "SupplementalCategories", 20U, 32U, true },
        MappingDescriptor {
            MetadataDescriptiveTranslationMapping::PhotoshopUrgency,
            kXmpNsPhotoshop, "Urgency", 10U, 1U, false },
    };

    static constexpr size_t kIptcMappingCount = kMappings.size()
                                                + kLocationMappings.size()
                                                + kEditorialMappings.size()
                                                + kWorkflowMappings.size();

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

    static std::string_view urgency_scalar_text(const MetaValue& value) noexcept
    {
        if (value.kind != MetaValueKind::Scalar || value.count != 1U) {
            return {};
        }
        uint64_t number = 0U;
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64: number = value.data.u64; break;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            if (value.data.i64 < 1) {
                return {};
            }
            number = static_cast<uint64_t>(value.data.i64);
            break;
        default: return {};
        }
        if (number < 1U || number > 8U) {
            return {};
        }
        static constexpr std::string_view kDigits = "12345678";
        return kDigits.substr(static_cast<size_t>(number - 1U), 1U);
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
            std::string_view text;
            if (entry.value.kind == MetaValueKind::Text) {
                text = arena_text(source.arena(), entry.value.data.span);
            } else if (descriptor.mapping
                       == MetadataDescriptiveTranslationMapping::PhotoshopUrgency) {
                text = urgency_scalar_text(entry.value);
            } else {
                result->failed_mapping      = descriptor.mapping;
                result->failed_source_entry = candidate.entry_id;
                return MetadataDescriptiveTranslationStatus::InvalidSourceValue;
            }
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
            if (descriptor.mapping
                == MetadataDescriptiveTranslationMapping::PhotoshopCategory) {
                for (const char c : text) {
                    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
                        result->failed_mapping      = descriptor.mapping;
                        result->failed_source_entry = candidate.entry_id;
                        return MetadataDescriptiveTranslationStatus::
                            InvalidSourceValue;
                    }
                }
            }
            if (descriptor.mapping
                    == MetadataDescriptiveTranslationMapping::PhotoshopUrgency
                && (text.size() != 1U || text[0U] < '1' || text[0U] > '8')) {
                result->failed_mapping      = descriptor.mapping;
                result->failed_source_entry = candidate.entry_id;
                return MetadataDescriptiveTranslationStatus::InvalidSourceValue;
            }
            if (descriptor.mapping
                == MetadataDescriptiveTranslationMapping::IptcCountryCode) {
                bool valid = text.size() == 2U || text.size() == 3U;
                for (const char c : text) {
                    valid = valid && c >= 'A' && c <= 'Z';
                }
                if (!valid) {
                    result->failed_mapping      = descriptor.mapping;
                    result->failed_source_entry = candidate.entry_id;
                    return MetadataDescriptiveTranslationStatus::InvalidSourceValue;
                }
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

    static bool
    mapping_owns_native_entry(EntryId id,
                              std::span<const PlannedMapping> plans) noexcept
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
                      std::span<const PlannedMapping> plans,
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
                                  const SourceText& value,
                                  uint32_t repeated_order) noexcept
    {
        if (!edit || value.entry_id >= source.entries().size()) {
            return false;
        }
        Entry entry;
        entry.key    = make_iptc_dataset_key(2U, descriptor.iptc_dataset);
        entry.value  = make_iptc_value(edit->arena(), value.text);
        entry.origin = source.entry(value.entry_id).origin;
        if (descriptor.repeated) {
            // Equal ranks retain append order, even after a UINT32_MAX rank.
            entry.origin.order_in_block = repeated_order;
        }
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

    struct StructuredLocationPath final {
        std::string_view child;
        uint32_t index = 0U;
        bool scalar    = false;
    };

    static MetadataDescriptiveTranslationStatus
    parse_location_path(std::string_view path, std::string_view root,
                        StructuredLocationPath* out) noexcept
    {
        using Status = MetadataDescriptiveTranslationStatus;
        if (!path.starts_with(root)) {
            return Status::Ok;
        }
        path.remove_prefix(root.size());
        if (path.empty() || (path.front() != '[' && path.front() != '/')) {
            return Status::Ok;
        }
        if (path.front() == '/') {
            if (root != "LocationCreated" || path.size() == 1U) {
                return Status::UnsupportedSourceShape;
            }
            out->index  = 1U;
            out->scalar = true;
            out->child  = path.substr(1U);
            return Status::Ok;
        }
        const size_t close = path.find(']');
        if (close == std::string_view::npos || close < 2U || path[1U] == '0') {
            return Status::UnsupportedSourceShape;
        }
        uint32_t index = 0U;
        for (size_t i = 1U; i < close; ++i) {
            const char c = path[i];
            if (c < '0' || c > '9'
                || index
                       > (UINT32_MAX - static_cast<uint32_t>(c - '0')) / 10U) {
                return Status::UnsupportedSourceShape;
            }
            index = index * 10U + static_cast<uint32_t>(c - '0');
        }
        path.remove_prefix(close + 1U);
        if (!path.empty() && (path.front() != '/' || path.size() == 1U)) {
            return Status::UnsupportedSourceShape;
        }
        out->index = index;
        out->child = path.empty() ? path : path.substr(1U);
        return Status::Ok;
    }

    static size_t location_field_index(std::string_view child) noexcept
    {
        static constexpr std::string_view prefix = "Iptc4xmpExt:";
        if (child.starts_with(prefix)) {
            child.remove_prefix(prefix.size());
        }
        static constexpr std::array<std::string_view, 5U> fields {
            "City", "Sublocation", "ProvinceState", "CountryName", "CountryCode"
        };
        for (size_t i = 0U; i < fields.size(); ++i) {
            if (child == fields[i]) {
                return i;
            }
        }
        return fields.size();
    }

    static bool append_flat_location_entry(MetaEdit* edit,
                                           const MetaStore& source,
                                           const MappingDescriptor& descriptor,
                                           const SourceText& value)
    {
        Entry entry;
        entry.key   = make_xmp_property_key(edit->arena(), descriptor.schema_ns,
                                            descriptor.property_path);
        entry.value = make_text(edit->arena(), value.text, TextEncoding::Utf8);
        entry.origin = source.entry(value.entry_id).origin;
        if (entry.origin.wire_type_name.size != 0U) {
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

static MetadataDescriptiveTranslationResult
translate_iptc_text_mappings(
    const MetaStore& source,
    const MetadataDescriptiveTranslationOptions& options,
    std::span<const MappingDescriptor> mappings, MetaStore* out_store)
{
    if (!out_store) {
        return translation_error(
            MetadataDescriptiveTranslationStatus::NullOutput);
    }
    if (!source.is_finalized()) {
        return translation_error(
            MetadataDescriptiveTranslationStatus::SourceNotFinalized);
    }
    if (mappings.empty() || mappings.size() > kIptcMappingCount
        || options.max_source_properties == 0U
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

    std::array<PlannedMapping, kIptcMappingCount> plan_storage;
    const std::span<PlannedMapping> plans(plan_storage.data(), mappings.size());
    uint32_t matched_sources  = 0U;
    uint64_t total_text_bytes = 0U;
    MetadataDescriptiveTranslationResult result;
    for (size_t i = 0U; i < mappings.size(); ++i) {
        const MetadataDescriptiveTranslationStatus status
            = collect_source_mapping(source, options, mappings[i],
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
        const uint32_t repeated_order
            = overlap == 0U ? 0U
                            : source.entry(plan.native_entries[overlap - 1U])
                                  .origin.order_in_block;
        for (size_t i = overlap; i < plan.values.size(); ++i) {
            if (append_iptc_entry(&edit, source, *plan.descriptor,
                                  plan.values[i], repeated_order)) {
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

MetadataDescriptiveTranslationResult
translate_xmp_descriptive_metadata(
    const MetaStore& source,
    const MetadataDescriptiveTranslationOptions& options, MetaStore* out_store)
{
    const std::array enabled = {
        options.title_to_iptc_object_name,
        options.description_to_iptc_caption,
        options.creators_to_iptc_bylines,
        options.keywords_to_iptc_keywords,
        options.copyright_to_iptc_copyright,
        options.credit_to_iptc_credit,
        options.source_to_iptc_source,
    };
    std::array<MappingDescriptor, kMappings.size()> selected;
    size_t count = 0U;
    for (size_t i = 0U; i < kMappings.size(); ++i) {
        if (enabled[i]) {
            selected[count++] = kMappings[i];
        }
    }
    return translate_iptc_text_mappings(
        source, options,
        std::span<const MappingDescriptor>(selected.data(), count), out_store);
}

MetadataDescriptiveTranslationResult
translate_xmp_location_metadata(
    const MetaStore& source, const MetadataLocationTranslationOptions& options,
    MetaStore* out_store)
{
    if (!out_store) {
        return translation_error(
            MetadataDescriptiveTranslationStatus::NullOutput);
    }
    if (!source.is_finalized()) {
        return translation_error(
            MetadataDescriptiveTranslationStatus::SourceNotFinalized);
    }
    if (options.max_added_entries
        > kMetadataLocationTranslationMaxAddedEntries) {
        return translation_error(
            MetadataDescriptiveTranslationStatus::InvalidOptions);
    }
    const std::array enabled = {
        options.city_to_iptc,         options.sublocation_to_iptc,
        options.state_to_iptc,        options.country_to_iptc,
        options.country_code_to_iptc,
    };
    std::array<MappingDescriptor, kLocationMappings.size()> selected;
    size_t count = 0U;
    for (size_t i = 0U; i < kLocationMappings.size(); ++i) {
        if (enabled[i]) {
            selected[count++] = kLocationMappings[i];
        }
    }
    MetadataDescriptiveTranslationOptions text_options;
    text_options.source_mode           = options.source_mode;
    text_options.conflict_policy       = options.conflict_policy;
    text_options.max_source_properties = options.max_source_properties;
    text_options.max_added_entries     = options.max_added_entries;
    text_options.max_operations        = options.max_operations;
    text_options.max_total_text_bytes  = options.max_total_text_bytes;
    return translate_iptc_text_mappings(
        source, text_options,
        std::span<const MappingDescriptor>(selected.data(), count), out_store);
}

MetadataDescriptiveTranslationResult
translate_xmp_structured_location_metadata(
    const MetaStore& source,
    const MetadataStructuredLocationTranslationOptions& options,
    MetaStore* out_store)
{
    using Status = MetadataDescriptiveTranslationStatus;
    using Policy = MetadataDescriptiveTranslationConflictPolicy;
    using Kind   = MetadataStructuredLocationKind;
    if (!out_store) {
        return translation_error(Status::NullOutput);
    }
    if (!source.is_finalized()) {
        return translation_error(Status::SourceNotFinalized);
    }
    if ((options.location_kind != Kind::Shown
         && options.location_kind != Kind::Created)
        || (options.source_mode
                != MetadataDescriptiveTranslationSourceMode::DirtyOnly
            && options.source_mode
                   != MetadataDescriptiveTranslationSourceMode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (!options.city && !options.sublocation && !options.state
            && !options.country && !options.country_code)
        || options.max_source_properties == 0U
        || options.max_source_properties
               > kMetadataDescriptiveTranslationMaxSourceProperties
        || options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataStructuredLocationTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataDescriptiveTranslationMaxOperations
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataDescriptiveTranslationMaxTotalTextBytes) {
        return translation_error(Status::InvalidOptions);
    }

    const std::string_view root = options.location_kind == Kind::Shown
                                      ? "LocationShown"
                                      : "LocationCreated";
    uint32_t first_index        = 0U;
    uint32_t inspected          = 0U;
    bool multiple               = false;
    bool scalar                 = false;
    bool indexed                = false;
    bool requested_found        = false;
    for (EntryId id = 0U; id < source.entries().size(); ++id) {
        const Entry& entry = source.entry(id);
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || (any(entry.flags, EntryFlags::Deleted)
                && !any(entry.flags, EntryFlags::Dirty))
            || arena_text(source.arena(), entry.key.data.xmp_property.schema_ns)
                   != kXmpNsIptcExt) {
            continue;
        }
        StructuredLocationPath path;
        const Status status = parse_location_path(
            arena_text(source.arena(),
                       entry.key.data.xmp_property.property_path),
            root, &path);
        if (status != Status::Ok) {
            return translation_error(
                status, MetadataDescriptiveTranslationMapping::None, id);
        }
        // Container tombstones do not select or remove their former child fields.
        if (path.index == 0U
            || (path.child.empty() && any(entry.flags, EntryFlags::Deleted))) {
            continue;
        }
        if (++inspected > options.max_source_properties) {
            return translation_error(Status::SourceLimitExceeded,
                                     MetadataDescriptiveTranslationMapping::None,
                                     id);
        }
        scalar  = scalar || path.scalar;
        indexed = indexed || !path.scalar;
        if (first_index == 0U) {
            first_index = path.index;
        }
        multiple        = multiple || path.index != first_index;
        requested_found = requested_found
                          || path.index == options.location_index;
    }
    if (scalar && indexed) {
        return translation_error(Status::UnsupportedSourceShape);
    }
    if (options.location_index == 0U && multiple) {
        return translation_error(Status::AmbiguousLocation);
    }
    if (options.location_index != 0U && !requested_found) {
        return translation_error(Status::LocationNotFound);
    }
    const uint32_t selected_index = options.location_index != 0U
                                        ? options.location_index
                                        : first_index;
    const std::array enabled { options.city, options.sublocation, options.state,
                               options.country, options.country_code };
    struct FieldSource final {
        EntryId first     = kInvalidEntryId;
        EntryId duplicate = kInvalidEntryId;
        bool dirty        = false;
    };
    std::array<FieldSource, 5U> fields {};
    for (EntryId id = 0U; id < source.entries().size(); ++id) {
        const Entry& entry = source.entry(id);
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || (any(entry.flags, EntryFlags::Deleted)
                && !any(entry.flags, EntryFlags::Dirty))
            || arena_text(source.arena(), entry.key.data.xmp_property.schema_ns)
                   != kXmpNsIptcExt) {
            continue;
        }
        StructuredLocationPath path;
        parse_location_path(
            arena_text(source.arena(),
                       entry.key.data.xmp_property.property_path),
            root, &path);
        if (path.index == 0U || path.index != selected_index) {
            continue;
        }
        const size_t i = location_field_index(path.child);
        if (i == fields.size() || !enabled[i]) {
            continue;
        }
        FieldSource& field = fields[i];
        field.dirty        = field.dirty || any(entry.flags, EntryFlags::Dirty);
        if (field.first == kInvalidEntryId) {
            field.first = id;
        } else {
            field.duplicate = id;
        }
    }

    MetadataDescriptiveTranslationOptions text_options;
    text_options.source_mode           = options.source_mode;
    text_options.max_source_properties = options.max_source_properties;
    text_options.max_total_text_bytes  = options.max_total_text_bytes;
    text_options.max_operations        = options.max_operations;
    std::array<PlannedMapping, 5U> plans;
    std::array<std::vector<EntryId>, 5U> flat_entries;
    MetadataDescriptiveTranslationResult result;
    uint32_t matched_sources   = 0U;
    uint64_t text_bytes        = 0U;
    uint32_t native_properties = 0U;
    for (size_t i = 0U; i < plans.size(); ++i) {
        const FieldSource& field = fields[i];
        if (field.first == kInvalidEntryId
            || (options.source_mode
                    == MetadataDescriptiveTranslationSourceMode::DirtyOnly
                && !field.dirty)) {
            continue;
        }
        if (field.duplicate != kInvalidEntryId) {
            return translation_error(Status::AmbiguousSource,
                                     kLocationMappings[i].mapping,
                                     field.duplicate);
        }
        MappingDescriptor descriptor = kLocationMappings[i];
        descriptor.schema_ns         = kXmpNsIptcExt;
        descriptor.property_path     = arena_text(
            source.arena(),
            source.entry(field.first).key.data.xmp_property.property_path);
        PlannedMapping& plan = plans[i];
        result.status = collect_source_mapping(source, text_options, descriptor,
                                               &matched_sources, &text_bytes,
                                               &plan, &result);
        plan.descriptor = &kLocationMappings[i];
        if (result.status != Status::Ok) {
            return result;
        }
        result.status = analyze_native_mapping(source, text_options,
                                               &native_properties, &plan);
        if (result.status != Status::Ok) {
            result.failed_mapping = plan.descriptor->mapping;
            return result;
        }
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            uint32_t unused    = 0U;
            if (!any(entry.flags, EntryFlags::Deleted)
                && source_key_matches(source, entry, *plan.descriptor,
                                      &unused)) {
                if (native_properties >= options.max_operations) {
                    return translation_error(Status::OperationLimitExceeded,
                                             plan.descriptor->mapping);
                }
                ++native_properties;
                flat_entries[i].push_back(id);
            }
        }
        bool flat_exact = flat_entries[i].size() == plan.values.size();
        if (flat_exact && !plan.values.empty()) {
            const Entry& flat = source.entry(flat_entries[i][0U]);
            flat_exact        = flat.value.kind == MetaValueKind::Text
                         && entry_value_matches(source, flat,
                                                plan.values[0U].text);
        }
        const bool existing = !plan.native_entries.empty()
                              || !flat_entries[i].empty();
        if (options.conflict_policy == Policy::PreserveExisting && existing) {
            plan.preserved = true;
            ++result.groups_preserved;
            continue;
        }
        if (options.conflict_policy == Policy::FailOnConflict
            && ((!plan.native_entries.empty() && !plan.exact)
                || (!flat_entries[i].empty() && !flat_exact))) {
            return translation_error(Status::NativeConflict,
                                     plan.descriptor->mapping, field.first);
        }
        plan.apply = !plan.exact || !flat_exact;
        if (!plan.apply) {
            ++result.groups_unchanged;
        }
    }

    bool add_charset       = false;
    EntryId charset_source = kInvalidEntryId;
    result.status          = plan_utf8_charset(source, plans,
                                               options.max_total_text_bytes - text_bytes,
                                               &add_charset, &charset_source);
    if (result.status != Status::Ok) {
        return result;
    }
    uint32_t added      = add_charset ? 1U : 0U;
    uint32_t operations = added;
    for (size_t i = 0U; i < plans.size(); ++i) {
        const PlannedMapping& plan = plans[i];
        if (!plan.apply) {
            continue;
        }
        for (const bool flat : { false, true }) {
            const std::vector<EntryId>& ids = flat ? flat_entries[i]
                                                   : plan.native_entries;
            if (plan.values.empty()) {
                operations += static_cast<uint32_t>(ids.size());
            } else if (ids.empty()) {
                ++operations;
                ++added;
            } else {
                const Entry& current = source.entry(ids[0U]);
                const bool exact
                    = (!flat || current.value.kind == MetaValueKind::Text)
                      && entry_value_matches(source, current,
                                             plan.values[0U].text);
                operations += static_cast<uint32_t>(ids.size() - 1U)
                              + (exact ? 0U : 1U);
            }
        }
    }
    if (added > options.max_added_entries
        || source.entries().size()
               > static_cast<size_t>(kInvalidEntryId) - added) {
        return translation_error(Status::EntryLimitExceeded);
    }
    if (operations > options.max_operations) {
        return translation_error(Status::OperationLimitExceeded);
    }
    MetaEdit edit;
    edit.reserve_ops(operations);
    for (size_t i = 0U; i < plans.size(); ++i) {
        const PlannedMapping& plan = plans[i];
        if (!plan.apply) {
            continue;
        }
        for (const bool flat : { false, true }) {
            const std::vector<EntryId>& ids = flat ? flat_entries[i]
                                                   : plan.native_entries;
            const size_t kept = plan.values.empty() || ids.empty() ? 0U : 1U;
            if (kept != 0U) {
                const Entry& current         = source.entry(ids[0U]);
                const std::string_view value = plan.values[0U].text;
                if ((flat && current.value.kind != MetaValueKind::Text)
                    || !entry_value_matches(source, current, value)) {
                    edit.set_value(ids[0U],
                                   flat ? make_text(edit.arena(), value,
                                                    TextEncoding::Utf8)
                                        : make_iptc_value(edit.arena(), value));
                    ++result.entries_updated;
                }
            }
            for (size_t j = kept; j < ids.size(); ++j) {
                edit.tombstone(ids[j]);
                ++result.entries_removed;
            }
            if (!plan.values.empty() && ids.empty()) {
                const bool appended
                    = flat ? append_flat_location_entry(&edit, source,
                                                        *plan.descriptor,
                                                        plan.values[0U])
                           : append_iptc_entry(&edit, source, *plan.descriptor,
                                               plan.values[0U], 0U);
                if (appended) {
                    ++result.entries_added;
                }
            }
        }
        ++result.groups_translated;
    }
    if (add_charset
        && append_utf8_charset_entry(&edit, source, charset_source)) {
        ++result.entries_added;
        result.utf8_charset_added = true;
    }
    if (edit.ops().size() != operations || result.entries_added != added
        || edit.arena().limit_exceeded()) {
        return translation_error(Status::InternalError);
    }
    *out_store = commit(source, std::span<const MetaEdit>(&edit, 1U));
    return result;
}

MetadataDescriptiveTranslationResult
translate_xmp_editorial_metadata(
    const MetaStore& source, const MetadataEditorialTranslationOptions& options,
    MetaStore* out_store)
{
    if (!out_store) {
        return translation_error(
            MetadataDescriptiveTranslationStatus::NullOutput);
    }
    if (!source.is_finalized()) {
        return translation_error(
            MetadataDescriptiveTranslationStatus::SourceNotFinalized);
    }
    if (options.max_added_entries
        > kMetadataEditorialTranslationMaxAddedEntries) {
        return translation_error(
            MetadataDescriptiveTranslationStatus::InvalidOptions);
    }
    const std::array enabled = {
        options.headline_to_iptc,
        options.instructions_to_iptc,
        options.transmission_reference_to_iptc,
    };
    std::array<MappingDescriptor, kEditorialMappings.size()> selected;
    size_t count = 0U;
    for (size_t i = 0U; i < kEditorialMappings.size(); ++i) {
        if (enabled[i]) {
            selected[count++] = kEditorialMappings[i];
        }
    }
    MetadataDescriptiveTranslationOptions text_options;
    text_options.source_mode           = options.source_mode;
    text_options.conflict_policy       = options.conflict_policy;
    text_options.max_source_properties = options.max_source_properties;
    text_options.max_added_entries     = options.max_added_entries;
    text_options.max_operations        = options.max_operations;
    text_options.max_total_text_bytes  = options.max_total_text_bytes;
    return translate_iptc_text_mappings(
        source, text_options,
        std::span<const MappingDescriptor>(selected.data(), count), out_store);
}

MetadataDescriptiveTranslationResult
translate_xmp_iptc_metadata(const MetaStore& source,
                            const MetadataIptcTranslationOptions& options,
                            MetaStore* out_store)
{
    const std::array<bool, kIptcMappingCount> enabled = {
        options.title_to_iptc_object_name,
        options.description_to_iptc_caption,
        options.creators_to_iptc_bylines,
        options.keywords_to_iptc_keywords,
        options.copyright_to_iptc_copyright,
        options.credit_to_iptc_credit,
        options.source_to_iptc_source,
        options.city_to_iptc,
        options.sublocation_to_iptc,
        options.state_to_iptc,
        options.country_to_iptc,
        options.country_code_to_iptc,
        options.headline_to_iptc,
        options.instructions_to_iptc,
        options.transmission_reference_to_iptc,
        options.authors_position_to_iptc,
        options.caption_writer_to_iptc,
        options.category_to_iptc,
        options.supplemental_categories_to_iptc,
        options.urgency_to_iptc,
    };
    const std::array<std::span<const MappingDescriptor>, 4U> groups = {
        kMappings,
        kLocationMappings,
        kEditorialMappings,
        kWorkflowMappings,
    };
    std::array<MappingDescriptor, kIptcMappingCount> selected;
    size_t count = 0U;
    size_t index = 0U;
    for (const std::span<const MappingDescriptor> group : groups) {
        for (const MappingDescriptor& descriptor : group) {
            if (enabled[index++]) {
                selected[count++] = descriptor;
            }
        }
    }
    MetadataDescriptiveTranslationOptions text_options;
    text_options.source_mode           = options.source_mode;
    text_options.conflict_policy       = options.conflict_policy;
    text_options.max_source_properties = options.max_source_properties;
    text_options.max_added_entries     = options.max_added_entries;
    text_options.max_operations        = options.max_operations;
    text_options.max_total_text_bytes  = options.max_total_text_bytes;
    return translate_iptc_text_mappings(
        source, text_options,
        std::span<const MappingDescriptor>(selected.data(), count), out_store);
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
    case MetadataDescriptiveTranslationStatus::AmbiguousLocation:
        return "ambiguous_location";
    case MetadataDescriptiveTranslationStatus::LocationNotFound:
        return "location_not_found";
    case MetadataDescriptiveTranslationStatus::UnsupportedSourceShape:
        return "unsupported_source_shape";
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
    case MetadataDescriptiveTranslationMapping::PhotoshopCity:
        return "photoshop_city";
    case MetadataDescriptiveTranslationMapping::IptcLocation:
        return "iptc_location";
    case MetadataDescriptiveTranslationMapping::PhotoshopState:
        return "photoshop_state";
    case MetadataDescriptiveTranslationMapping::PhotoshopCountry:
        return "photoshop_country";
    case MetadataDescriptiveTranslationMapping::IptcCountryCode:
        return "iptc_country_code";
    case MetadataDescriptiveTranslationMapping::PhotoshopHeadline:
        return "photoshop_headline";
    case MetadataDescriptiveTranslationMapping::PhotoshopInstructions:
        return "photoshop_instructions";
    case MetadataDescriptiveTranslationMapping::PhotoshopTransmissionReference:
        return "photoshop_transmission_reference";
    case MetadataDescriptiveTranslationMapping::PhotoshopAuthorsPosition:
        return "photoshop_authors_position";
    case MetadataDescriptiveTranslationMapping::PhotoshopCaptionWriter:
        return "photoshop_caption_writer";
    case MetadataDescriptiveTranslationMapping::PhotoshopCategory:
        return "photoshop_category";
    case MetadataDescriptiveTranslationMapping::PhotoshopSupplementalCategories:
        return "photoshop_supplemental_categories";
    case MetadataDescriptiveTranslationMapping::PhotoshopUrgency:
        return "photoshop_urgency";
    }
    return "unknown";
}

}  // namespace openmeta
