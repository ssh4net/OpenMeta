// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_translation.h"

#include "metadata_logical_field_internal.h"
#include "metadata_translation_iptc_xmp_mapping_internal.h"

#include "openmeta/meta_edit.h"
#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace openmeta {
namespace {

    using MappingDescriptor = detail::IptcXmpMappingDescriptor;
    using Mapping           = MetadataDescriptiveTranslationMapping;
    using Status            = MetadataIptcToXmpStatus;

    static constexpr std::array<std::byte, 3U> kUtf8Escape
        = { std::byte { 0x1bU }, std::byte { 0x25U }, std::byte { 0x47U } };

    struct SourceText final {
        EntryId entry_id        = kInvalidEntryId;
        BlockId block           = kInvalidBlockId;
        uint32_t order_in_block = 0U;
        std::string_view text;
    };

    struct DestinationText final {
        EntryId entry_id = kInvalidEntryId;
        uint32_t index   = 0U;
        std::string_view text;
    };

    struct SelectedSource final {
        size_t mapping_index = 0U;
        SourceText source;
    };

    struct SelectedDestination final {
        size_t mapping_index = 0U;
        DestinationText destination;
    };

    struct PlannedGroup final {
        const MappingDescriptor* descriptor = nullptr;
        uint32_t active_count               = 0U;
        uint32_t target_count               = 0U;
        bool selected                       = false;
        bool preserve                       = false;
        bool unchanged                      = false;
        std::vector<SourceText> sources;
        std::vector<DestinationText> destinations;
    };

    struct CharsetRecord final {
        BlockId block = kInvalidBlockId;
        bool present  = false;
        bool valid    = true;
    };

    struct SourceOrder final {
        bool operator()(const SourceText& a, const SourceText& b) const noexcept
        {
            if (a.block != b.block) {
                return a.block < b.block;
            }
            if (a.order_in_block != b.order_in_block) {
                return a.order_in_block < b.order_in_block;
            }
            return a.entry_id < b.entry_id;
        }
    };

    struct DestinationOrder final {
        bool operator()(const DestinationText& a,
                        const DestinationText& b) const noexcept
        {
            return a.index == b.index ? a.entry_id < b.entry_id
                                      : a.index < b.index;
        }
    };

    enum class DestinationPathKind : uint8_t {
        NoMatch,
        Target,
        LanguageSibling,
        Unsupported,
    };

    static bool span_valid(const ByteArena& arena, ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.bytes();
        return span.offset <= bytes.size()
               && span.size <= bytes.size() - span.offset;
    }

    static bool arena_string(const ByteArena& arena, ByteSpan span,
                             std::string_view* out) noexcept
    {
        if (!out || !span_valid(arena, span)) {
            return false;
        }
        const std::span<const std::byte> bytes = arena.span(span);
        *out = bytes.empty() ? std::string_view {}
                             : std::string_view(reinterpret_cast<const char*>(
                                                    bytes.data()),
                                                bytes.size());
        return true;
    }

    static bool is_deleted(const Entry& entry) noexcept
    {
        return any(entry.flags, EntryFlags::Deleted);
    }

    static bool source_flags_valid(const Entry& entry) noexcept
    {
        constexpr uint8_t kKnownFlags = 0x7fU;
        constexpr uint8_t kDamagedFlags
            = static_cast<uint8_t>(EntryFlags::Truncated)
              | static_cast<uint8_t>(EntryFlags::Unreadable);
        const uint8_t flags = static_cast<uint8_t>(entry.flags);
        return (flags & static_cast<uint8_t>(~kKnownFlags)) == 0U
               && (flags & kDamagedFlags) == 0U;
    }

    static bool
    source_entry_matches(const Entry& entry,
                         const MappingDescriptor& descriptor) noexcept
    {
        return entry.key.kind == MetaKeyKind::IptcDataset
               && entry.key.data.iptc_dataset.record == 2U
               && entry.key.data.iptc_dataset.dataset
                      == descriptor.iptc_dataset;
    }

    static bool is_source_candidate(const Entry& entry,
                                    const MappingDescriptor& descriptor) noexcept
    {
        return source_entry_matches(entry, descriptor)
               && (!is_deleted(entry) || any(entry.flags, EntryFlags::Dirty));
    }

    static bool mapping_enabled(const MetadataIptcToXmpOptions& options,
                                Mapping mapping) noexcept
    {
        switch (mapping) {
        case Mapping::DcTitle: return options.title;
        case Mapping::DcDescription: return options.description;
        case Mapping::DcCreator: return options.creators;
        case Mapping::DcSubject: return options.keywords;
        case Mapping::DcRights: return options.copyright;
        case Mapping::PhotoshopCredit: return options.credit;
        case Mapping::PhotoshopSource: return options.source;
        case Mapping::PhotoshopCity: return options.city;
        case Mapping::IptcLocation: return options.sublocation;
        case Mapping::PhotoshopState: return options.state;
        case Mapping::PhotoshopCountry: return options.country;
        case Mapping::IptcCountryCode: return options.country_code;
        case Mapping::PhotoshopHeadline: return options.headline;
        case Mapping::PhotoshopInstructions: return options.instructions;
        case Mapping::PhotoshopTransmissionReference:
            return options.transmission_reference;
        case Mapping::PhotoshopAuthorsPosition: return options.authors_position;
        case Mapping::PhotoshopCaptionWriter: return options.caption_writer;
        case Mapping::PhotoshopCategory: return options.category;
        case Mapping::PhotoshopSupplementalCategories:
            return options.supplemental_categories;
        case Mapping::PhotoshopUrgency: return options.urgency;
        case Mapping::None: return false;
        }
        return false;
    }

    static bool is_charset_marker(const Entry& entry) noexcept
    {
        return entry.key.kind == MetaKeyKind::IptcDataset
               && entry.key.data.iptc_dataset.record == 1U
               && entry.key.data.iptc_dataset.dataset == 90U;
    }


    static void inspect_charset_entry(const MetaStore& source,
                                      const Entry& entry, BlockId block,
                                      CharsetRecord* result) noexcept
    {
        if (!result || entry.key.kind != MetaKeyKind::IptcDataset
            || entry.key.data.iptc_dataset.record != 1U
            || entry.key.data.iptc_dataset.dataset != 90U
            || entry.origin.block != block || is_deleted(entry)) {
            return;
        }
        if (!source_flags_valid(entry)) {
            result->valid = false;
        }
        if (result->present) {
            result->valid = false;
            return;
        }
        result->present = true;
        if (entry.value.kind != MetaValueKind::Bytes
            && entry.value.kind != MetaValueKind::Text) {
            result->valid = false;
            return;
        }
        if (entry.value.kind == MetaValueKind::Text
            && entry.value.text_encoding != TextEncoding::Ascii
            && entry.value.text_encoding != TextEncoding::Utf8) {
            result->valid = false;
        }
        const ByteSpan span = entry.value.data.span;
        if (entry.value.count != span.size
            || !span_valid(source.arena(), span)) {
            result->valid = false;
            return;
        }
        const std::span<const std::byte> bytes = source.arena().span(span);
        if (bytes.size() != kUtf8Escape.size()
            || !std::equal(bytes.begin(), bytes.end(), kUtf8Escape.begin())) {
            result->valid = false;
        }
    }

    static CharsetRecord inspect_charset_block(const MetaStore& source,
                                               BlockId block) noexcept
    {
        CharsetRecord result;
        result.block = block;
        if (block != kInvalidBlockId) {
            for (const EntryId id : source.entries_in_block(block)) {
                inspect_charset_entry(source, source.entry(id), block, &result);
            }
            return result;
        }
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            inspect_charset_entry(source, source.entry(id), block, &result);
        }
        return result;
    }

    static CharsetRecord* charset_for_block(
        const MetaStore& source, BlockId block,
        std::array<CharsetRecord, kMetadataIptcToXmpMaxSourceProperties>* cache,
        uint32_t* cache_count) noexcept
    {
        if (!cache || !cache_count) {
            return nullptr;
        }
        for (uint32_t i = 0U; i < *cache_count; ++i) {
            if ((*cache)[i].block == block) {
                return &(*cache)[i];
            }
        }
        if (*cache_count >= cache->size()) {
            return nullptr;
        }
        (*cache)[*cache_count] = inspect_charset_block(source, block);
        return &(*cache)[(*cache_count)++];
    }

    static bool bytes_have_non_ascii(std::span<const std::byte> bytes) noexcept
    {
        for (const std::byte byte : bytes) {
            if (std::to_integer<uint8_t>(byte) >= 0x80U) {
                return true;
            }
        }
        return false;
    }

    static bool text_has_non_ascii(std::string_view text) noexcept
    {
        for (const char c : text) {
            if (static_cast<unsigned char>(c) >= 0x80U) {
                return true;
            }
        }
        return false;
    }

    static bool source_field_value_valid(const MappingDescriptor& descriptor,
                                         std::string_view text) noexcept
    {
        if (descriptor.mapping == Mapping::PhotoshopCategory) {
            for (const char c : text) {
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
                    return false;
                }
            }
        } else if (descriptor.mapping == Mapping::IptcCountryCode) {
            if (text.size() != 2U && text.size() != 3U) {
                return false;
            }
            for (const char c : text) {
                if (c < 'A' || c > 'Z') {
                    return false;
                }
            }
        } else if (descriptor.mapping == Mapping::PhotoshopUrgency) {
            if (text.size() != 1U || text[0U] < '1' || text[0U] > '8') {
                return false;
            }
        }
        return true;
    }

    static Status inspect_source_structure(const MetaStore& source,
                                           const Entry& entry,
                                           const MappingDescriptor& descriptor,
                                           std::string_view* out_text) noexcept
    {
        if (!out_text
            || (entry.value.kind != MetaValueKind::Bytes
                && entry.value.kind != MetaValueKind::Text)) {
            return Status::InvalidSourceValue;
        }
        if (!source_flags_valid(entry)) {
            return Status::InvalidSourceValue;
        }
        if (entry.value.kind == MetaValueKind::Text
            && entry.value.text_encoding != TextEncoding::Ascii
            && entry.value.text_encoding != TextEncoding::Utf8) {
            return Status::InvalidSourceValue;
        }
        const ByteSpan span = entry.value.data.span;
        if (entry.value.count != span.size || span.size == 0U
            || !span_valid(source.arena(), span)) {
            return Status::InvalidSourceValue;
        }
        const std::span<const std::byte> bytes = source.arena().span(span);
        const std::string_view text(reinterpret_cast<const char*>(bytes.data()),
                                    bytes.size());
        if (text.size() > descriptor.max_bytes) {
            return Status::ValueTooLong;
        }
        *out_text = text;
        return Status::Ok;
    }

    static Status validate_source_text(
        const MetaStore& source, const Entry& entry,
        const MappingDescriptor& descriptor,
        std::array<CharsetRecord, kMetadataIptcToXmpMaxSourceProperties>* cache,
        uint32_t* cache_count, std::string_view text) noexcept
    {
        const std::span<const std::byte> bytes = source.arena().span(
            entry.value.data.span);
        const bool non_ascii = bytes_have_non_ascii(bytes);
        if (entry.value.kind == MetaValueKind::Text
            && entry.value.text_encoding == TextEncoding::Ascii && non_ascii) {
            return Status::SourceEncodingConflict;
        }
        if (!detail::metadata_logical_text_is_valid(text)
            || !source_field_value_valid(descriptor, text)) {
            return Status::InvalidSourceValue;
        }
        CharsetRecord* charset = charset_for_block(source, entry.origin.block,
                                                   cache, cache_count);
        if (!charset || !charset->valid || (non_ascii && !charset->present)) {
            return Status::SourceEncodingConflict;
        }
        return Status::Ok;
    }

    static bool options_valid(const MetadataIptcToXmpOptions& options) noexcept
    {
        const bool source_mode_valid
            = options.source_mode
                  == MetadataDescriptiveTranslationSourceMode::All
              || options.source_mode
                     == MetadataDescriptiveTranslationSourceMode::DirtyOnly;
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
                      <= kMetadataIptcToXmpMaxSourceProperties
               && options.max_added_entries > 0U
               && options.max_added_entries <= kMetadataIptcToXmpMaxAddedEntries
               && options.max_operations > 0U
               && options.max_operations <= kMetadataIptcToXmpMaxOperations
               && options.max_total_text_bytes > 0U
               && options.max_total_text_bytes
                      <= kMetadataIptcToXmpMaxTotalTextBytes;
    }

    static bool xmp_schema_matches(const MetaStore& source, const Entry& entry,
                                   const MappingDescriptor& descriptor) noexcept
    {
        if (entry.key.kind != MetaKeyKind::XmpProperty) {
            return false;
        }
        std::string_view schema;
        return arena_string(source.arena(),
                            entry.key.data.xmp_property.schema_ns, &schema)
               && schema == descriptor.schema_ns;
    }

    static DestinationPathKind
    classify_destination_path(std::string_view path,
                              const MappingDescriptor& descriptor,
                              uint32_t* out_index = nullptr) noexcept
    {
        const std::string_view root = descriptor.property_root;
        if (path == root) {
            if (descriptor.shape == detail::IptcXmpPropertyShape::Scalar) {
                if (out_index) {
                    *out_index = 0U;
                }
                return DestinationPathKind::Target;
            }
            return DestinationPathKind::Unsupported;
        }
        if (path.size() <= root.size() || path.substr(0U, root.size()) != root
            || (path[root.size()] != '[' && path[root.size()] != '/')) {
            return DestinationPathKind::NoMatch;
        }
        const std::string_view suffix = path.substr(root.size());
        if (descriptor.shape == detail::IptcXmpPropertyShape::Scalar) {
            return DestinationPathKind::Unsupported;
        }
        if (descriptor.shape == detail::IptcXmpPropertyShape::LangAlt) {
            static constexpr std::string_view kLangPrefix = "[@xml:lang=";
            if (!suffix.starts_with(kLangPrefix) || suffix.back() != ']'
                || suffix.size() <= kLangPrefix.size() + 1U) {
                return DestinationPathKind::Unsupported;
            }
            const std::string_view language
                = suffix.substr(kLangPrefix.size(),
                                suffix.size() - kLangPrefix.size() - 1U);
            if (language.find(']') != std::string_view::npos
                || language.find('[') != std::string_view::npos) {
                return DestinationPathKind::Unsupported;
            }
            if (language == "x-default") {
                if (out_index) {
                    *out_index = 0U;
                }
                return DestinationPathKind::Target;
            }
            return DestinationPathKind::LanguageSibling;
        }

        if (suffix.size() < 3U || suffix.front() != '['
            || suffix.back() != ']') {
            return DestinationPathKind::Unsupported;
        }
        const std::string_view digits = suffix.substr(1U, suffix.size() - 2U);
        if (digits.empty() || digits.front() == '0') {
            return DestinationPathKind::Unsupported;
        }
        uint32_t index = 0U;
        for (const char c : digits) {
            if (c < '0' || c > '9') {
                return DestinationPathKind::Unsupported;
            }
            const uint32_t digit = static_cast<uint32_t>(c - '0');
            if (index > (std::numeric_limits<uint32_t>::max() - digit) / 10U) {
                return DestinationPathKind::Unsupported;
            }
            index = index * 10U + digit;
        }
        if (index == 0U) {
            return DestinationPathKind::Unsupported;
        }
        if (out_index) {
            *out_index = index;
        }
        return DestinationPathKind::Target;
    }

    static DestinationPathKind
    destination_path_kind(const MetaStore& source, const Entry& entry,
                          const MappingDescriptor& descriptor,
                          std::string_view* out_path,
                          uint32_t* out_index = nullptr) noexcept
    {
        if (!xmp_schema_matches(source, entry, descriptor)) {
            return DestinationPathKind::NoMatch;
        }
        if (!arena_string(source.arena(),
                          entry.key.data.xmp_property.property_path,
                          out_path)) {
            return DestinationPathKind::Unsupported;
        }
        return classify_destination_path(*out_path, descriptor, out_index);
    }

    static Status
    inspect_destination_structure(const MetaStore& source, const Entry& entry,
                                  std::string_view* out_text) noexcept
    {
        constexpr uint8_t kKnownFlags = 0x7fU;
        constexpr uint8_t kDamagedFlags
            = static_cast<uint8_t>(EntryFlags::Truncated)
              | static_cast<uint8_t>(EntryFlags::Unreadable);
        const uint8_t flags = static_cast<uint8_t>(entry.flags);
        if (!out_text || entry.value.kind != MetaValueKind::Text
            || (entry.value.text_encoding != TextEncoding::Ascii
                && entry.value.text_encoding != TextEncoding::Utf8)
            || (flags & static_cast<uint8_t>(~kKnownFlags)) != 0U
            || (flags & kDamagedFlags) != 0U) {
            return Status::UnsupportedDestinationShape;
        }
        const ByteSpan span = entry.value.data.span;
        if (entry.value.count != span.size
            || !span_valid(source.arena(), span)) {
            return Status::UnsupportedDestinationShape;
        }
        const std::span<const std::byte> bytes = source.arena().span(span);
        const std::string_view text
            = bytes.empty() ? std::string_view {}
                            : std::string_view(reinterpret_cast<const char*>(
                                                   bytes.data()),
                                               bytes.size());
        *out_text = text;
        return Status::Ok;
    }

    static Status validate_destination_text(const Entry& entry,
                                            std::string_view text) noexcept
    {
        if ((entry.value.text_encoding == TextEncoding::Ascii
             && text_has_non_ascii(text))
            || !detail::metadata_logical_text_is_valid(text)) {
            return Status::UnsupportedDestinationShape;
        }
        return Status::Ok;
    }

    static bool destination_has_root(const MetaStore& source,
                                     const MappingDescriptor& descriptor,
                                     bool* out_has_target,
                                     bool* out_has_unsupported) noexcept
    {
        if (!out_has_target || !out_has_unsupported) {
            return false;
        }
        *out_has_target      = false;
        *out_has_unsupported = false;
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (is_deleted(entry)
                || !xmp_schema_matches(source, entry, descriptor)) {
                continue;
            }
            std::string_view path;
            const DestinationPathKind kind
                = destination_path_kind(source, entry, descriptor, &path);
            if (kind == DestinationPathKind::Target
                || kind == DestinationPathKind::LanguageSibling) {
                *out_has_target = true;
            } else if (kind == DestinationPathKind::Unsupported) {
                *out_has_unsupported = true;
            }
        }
        return true;
    }

    static bool make_destination_path(const MappingDescriptor& descriptor,
                                      uint32_t ordinal,
                                      std::array<char, 96U>* storage,
                                      std::string_view* out_path) noexcept
    {
        if (!storage || !out_path) {
            return false;
        }
        if (!descriptor.repeated) {
            *out_path = descriptor.property_path;
            return true;
        }
        const std::string_view root = descriptor.property_root;
        if (root.size() + 14U > storage->size()) {
            return false;
        }
        size_t offset = 0U;
        for (const char c : root) {
            (*storage)[offset++] = c;
        }
        (*storage)[offset++] = '[';
        const auto conversion
            = std::to_chars(storage->data() + offset,
                            storage->data() + storage->size() - 1U, ordinal);
        if (conversion.ec != std::errc {}) {
            return false;
        }
        offset = static_cast<size_t>(conversion.ptr - storage->data());
        (*storage)[offset++] = ']';
        *out_path            = std::string_view(storage->data(), offset);
        return true;
    }

    struct EditCounts final {
        uint32_t added       = 0U;
        uint32_t updated     = 0U;
        uint32_t removed     = 0U;
        uint32_t operations  = 0U;
        uint64_t arena_bytes = 0U;
    };

    static bool add_arena_bytes(uint64_t* total, uint64_t amount) noexcept
    {
        if (!total || amount > std::numeric_limits<uint64_t>::max() - *total) {
            return false;
        }
        *total += amount;
        return true;
    }

    static bool add_text_budget(uint64_t* total, size_t amount,
                                const MetadataIptcToXmpOptions& options) noexcept
    {
        if (!total || amount > options.max_total_text_bytes
            || amount > kMetadataIptcToXmpMaxTotalTextBytes) {
            return false;
        }
        const uint64_t bytes = static_cast<uint64_t>(amount);
        if (*total > options.max_total_text_bytes - bytes
            || *total > kMetadataIptcToXmpMaxTotalTextBytes - bytes) {
            return false;
        }
        *total += bytes;
        return true;
    }

}  // namespace

MetadataIptcToXmpResult
translate_iptc_to_xmp(const MetaStore& source,
                      const MetadataIptcToXmpOptions& options,
                      MetaStore* out_store)
{
    MetadataIptcToXmpResult result;
    if (!out_store) {
        result.status = Status::NullOutput;
        return result;
    }
    if (!source.is_finalized()) {
        result.status = Status::SourceNotFinalized;
        return result;
    }
    if (!options_valid(options)) {
        result.status = Status::InvalidOptions;
        return result;
    }

    std::array<PlannedGroup, detail::kIptcXmpMappings.size()> groups;
    std::array<uint64_t, detail::kIptcXmpMappings.size()> candidate_counts {};
    std::array<bool, detail::kIptcXmpMappings.size()> member_dirty {};
    std::array<bool, detail::kIptcXmpMappings.size()> dirty_marker_blocks {};
    const bool dirty_only
        = options.source_mode
          == MetadataDescriptiveTranslationSourceMode::DirtyOnly;
    std::vector<uint64_t> dirty_charset_blocks;
    bool sentinel_dirty_charset = false;
    if (dirty_only) {
        const size_t block_count = source.block_count();
        dirty_charset_blocks.resize(block_count / 64U
                                        + (block_count % 64U != 0U ? 1U : 0U),
                                    0U);
        // Block indices omit tombstones. This input-sized bitmap keeps marker
        // selection linear even when many deleted markers share one block.
        for (const Entry& entry : source.entries()) {
            if (!is_charset_marker(entry)
                || !any(entry.flags, EntryFlags::Dirty)) {
                continue;
            }
            const BlockId block = entry.origin.block;
            if (block == kInvalidBlockId) {
                sentinel_dirty_charset = true;
            } else if (block < source.block_count()) {
                dirty_charset_blocks[block / 64U] |= uint64_t { 1U }
                                                     << (block % 64U);
            }
        }
    }

    for (const Entry& entry : source.entries()) {
        bool dirty_charset = false;
        if (dirty_only && !is_deleted(entry)) {
            const BlockId block = entry.origin.block;
            if (block == kInvalidBlockId) {
                dirty_charset = sentinel_dirty_charset;
            } else if (block < source.block_count()) {
                dirty_charset = (dirty_charset_blocks[block / 64U]
                                 & (uint64_t { 1U } << (block % 64U)))
                                != 0U;
            }
        }
        for (size_t i = 0U; i < detail::kIptcXmpMappings.size(); ++i) {
            const MappingDescriptor& descriptor = detail::kIptcXmpMappings[i];
            if (!mapping_enabled(options, descriptor.mapping)
                || !is_source_candidate(entry, descriptor)) {
                continue;
            }
            ++candidate_counts[i];
            member_dirty[i] = member_dirty[i]
                              || any(entry.flags, EntryFlags::Dirty);
            dirty_marker_blocks[i] = dirty_marker_blocks[i] || dirty_charset;
        }
    }

    uint32_t selected_source_properties = 0U;
    for (size_t mapping_index = 0U;
         mapping_index < detail::kIptcXmpMappings.size(); ++mapping_index) {
        PlannedGroup& group = groups[mapping_index];
        group.descriptor    = &detail::kIptcXmpMappings[mapping_index];
        const MappingDescriptor& descriptor = *group.descriptor;
        const uint64_t candidates           = candidate_counts[mapping_index];
        if (candidates == 0U) {
            continue;
        }
        group.selected = options.source_mode
                             == MetadataDescriptiveTranslationSourceMode::All
                         || member_dirty[mapping_index]
                         || dirty_marker_blocks[mapping_index];
        if (!group.selected) {
            continue;
        }
        if (candidates > options.max_source_properties
            || candidates > kMetadataIptcToXmpMaxSourceProperties
            || selected_source_properties
                   > options.max_source_properties - candidates
            || selected_source_properties
                   > kMetadataIptcToXmpMaxSourceProperties - candidates) {
            result.status         = Status::SourceLimitExceeded;
            result.failed_mapping = descriptor.mapping;
            return result;
        }
        selected_source_properties += static_cast<uint32_t>(candidates);
    }

    for (EntryId id = 0U; id < source.entries().size(); ++id) {
        const Entry& entry = source.entry(id);
        if (is_deleted(entry) || entry.origin.block == kInvalidBlockId
            || entry.origin.block < source.block_count()) {
            continue;
        }
        for (size_t i = 0U; i < detail::kIptcXmpMappings.size(); ++i) {
            const MappingDescriptor& descriptor = detail::kIptcXmpMappings[i];
            if (groups[i].selected
                && mapping_enabled(options, descriptor.mapping)
                && source_entry_matches(entry, descriptor)) {
                result.status              = Status::InvalidSourceValue;
                result.failed_mapping      = descriptor.mapping;
                result.failed_source_entry = id;
                return result;
            }
        }
    }

    std::array<CharsetRecord, kMetadataIptcToXmpMaxSourceProperties>
        charset_cache;
    uint32_t charset_cache_count = 0U;
    uint64_t total_text_bytes    = 0U;
    std::array<SelectedSource, kMetadataIptcToXmpMaxSourceProperties>
        selected_sources;
    uint32_t selected_source_count = 0U;
    for (size_t mapping_index = 0U;
         mapping_index < detail::kIptcXmpMappings.size(); ++mapping_index) {
        PlannedGroup& group = groups[mapping_index];
        if (!group.selected || !group.descriptor) {
            continue;
        }
        EntryId first_active_entry = kInvalidEntryId;
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (!is_source_candidate(entry, *group.descriptor)
                || is_deleted(entry)) {
                continue;
            }
            std::string_view text;
            const Status structure_status
                = inspect_source_structure(source, entry, *group.descriptor,
                                           &text);
            if (structure_status != Status::Ok) {
                result.status              = structure_status;
                result.failed_mapping      = group.descriptor->mapping;
                result.failed_source_entry = id;
                return result;
            }
            if (!add_text_budget(&total_text_bytes, text.size(), options)) {
                result.status              = Status::SourceLimitExceeded;
                result.failed_mapping      = group.descriptor->mapping;
                result.failed_source_entry = id;
                return result;
            }
            const Status text_status
                = validate_source_text(source, entry, *group.descriptor,
                                       &charset_cache, &charset_cache_count,
                                       text);
            if (text_status != Status::Ok) {
                result.status              = text_status;
                result.failed_mapping      = group.descriptor->mapping;
                result.failed_source_entry = id;
                return result;
            }
            if (selected_source_count >= selected_sources.size()) {
                result.status = Status::SourceLimitExceeded;
                return result;
            }
            selected_sources[selected_source_count++] = SelectedSource {
                mapping_index, SourceText { id, entry.origin.block,
                                            entry.origin.order_in_block, text }
            };
            if (first_active_entry == kInvalidEntryId) {
                first_active_entry = id;
            }
            ++group.active_count;
        }
        if (!group.descriptor->repeated && group.active_count > 1U) {
            result.status              = Status::AmbiguousSource;
            result.failed_mapping      = group.descriptor->mapping;
            result.failed_source_entry = first_active_entry;
            return result;
        }
    }

    std::array<SelectedDestination, kMetadataIptcToXmpMaxOperations>
        selected_destinations;
    uint32_t selected_destination_count = 0U;
    uint32_t destination_entries        = 0U;
    for (size_t mapping_index = 0U;
         mapping_index < detail::kIptcXmpMappings.size(); ++mapping_index) {
        PlannedGroup& group = groups[mapping_index];
        if (!group.selected || !group.descriptor) {
            continue;
        }
        if (options.conflict_policy
            == MetadataIptcToXmpConflictPolicy::PreserveExisting) {
            bool has_target      = false;
            bool has_unsupported = false;
            if (!destination_has_root(source, *group.descriptor, &has_target,
                                      &has_unsupported)) {
                result.status = Status::InternalError;
                return result;
            }
            if (has_target || has_unsupported) {
                group.preserve = true;
                continue;
            }
        }

        uint32_t target_count = 0U;
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (is_deleted(entry)
                || !xmp_schema_matches(source, entry, *group.descriptor)) {
                continue;
            }
            std::string_view path;
            uint32_t index = 0U;
            const DestinationPathKind kind
                = destination_path_kind(source, entry, *group.descriptor, &path,
                                        &index);
            if (kind == DestinationPathKind::Unsupported) {
                result.status         = Status::UnsupportedDestinationShape;
                result.failed_mapping = group.descriptor->mapping;
                return result;
            }
            if (kind != DestinationPathKind::Target) {
                continue;
            }
            if (target_count >= options.max_operations
                || target_count >= kMetadataIptcToXmpMaxOperations
                || destination_entries >= options.max_operations
                || destination_entries >= kMetadataIptcToXmpMaxOperations) {
                result.status         = Status::OperationLimitExceeded;
                result.failed_mapping = group.descriptor->mapping;
                return result;
            }
            ++target_count;
            ++destination_entries;

            std::string_view text;
            const Status structure_status
                = inspect_destination_structure(source, entry, &text);
            if (structure_status != Status::Ok) {
                result.status         = structure_status;
                result.failed_mapping = group.descriptor->mapping;
                return result;
            }
            if (!add_text_budget(&total_text_bytes, text.size(), options)) {
                result.status         = Status::SourceLimitExceeded;
                result.failed_mapping = group.descriptor->mapping;
                return result;
            }
            const Status text_status = validate_destination_text(entry, text);
            if (text_status != Status::Ok) {
                result.status         = text_status;
                result.failed_mapping = group.descriptor->mapping;
                return result;
            }
            if (selected_destination_count >= selected_destinations.size()) {
                result.status         = Status::OperationLimitExceeded;
                result.failed_mapping = group.descriptor->mapping;
                return result;
            }
            selected_destinations[selected_destination_count++]
                = SelectedDestination { mapping_index,
                                        DestinationText { id, index, text } };
        }
        if (!group.descriptor->repeated && target_count > 1U) {
            result.status         = Status::UnsupportedDestinationShape;
            result.failed_mapping = group.descriptor->mapping;
            return result;
        }
        group.target_count = target_count;
    }

    // Source and destination payloads have been validated once, and their
    // cumulative text budget is known before the bounded group vectors allocate.
    for (PlannedGroup& group : groups) {
        if (!group.selected || !group.descriptor) {
            continue;
        }
        group.sources.reserve(group.active_count);
        if (!group.preserve) {
            group.destinations.reserve(group.target_count);
        }
    }
    for (uint32_t i = 0U; i < selected_source_count; ++i) {
        const SelectedSource& selected = selected_sources[i];
        groups[selected.mapping_index].sources.push_back(selected.source);
    }
    for (uint32_t i = 0U; i < selected_destination_count; ++i) {
        const SelectedDestination& selected = selected_destinations[i];
        groups[selected.mapping_index].destinations.push_back(
            selected.destination);
    }
    for (PlannedGroup& group : groups) {
        if (!group.selected || !group.descriptor) {
            continue;
        }
        if (group.descriptor->repeated) {
            std::sort(group.sources.begin(), group.sources.end(),
                      SourceOrder {});
            std::sort(group.destinations.begin(), group.destinations.end(),
                      DestinationOrder {});
            for (size_t i = 0U; i < group.destinations.size(); ++i) {
                if (group.destinations[i].index != i + 1U) {
                    result.status         = Status::UnsupportedDestinationShape;
                    result.failed_mapping = group.descriptor->mapping;
                    return result;
                }
            }
        }
    }

    EditCounts counts;
    for (PlannedGroup& group : groups) {
        if (!group.selected || !group.descriptor || group.preserve) {
            continue;
        }
        const size_t source_count      = group.sources.size();
        const size_t destination_count = group.destinations.size();
        bool exact                     = source_count == destination_count;
        if (exact) {
            for (size_t i = 0U; i < source_count; ++i) {
                if (group.destinations[i].text != group.sources[i].text) {
                    exact = false;
                    break;
                }
            }
        }
        if (exact) {
            group.unchanged = true;
            continue;
        }
        if (options.conflict_policy
                == MetadataIptcToXmpConflictPolicy::FailOnConflict
            && destination_count > 0U) {
            result.status         = Status::DestinationConflict;
            result.failed_mapping = group.descriptor->mapping;
            return result;
        }
        const size_t overlap = std::min(source_count, destination_count);
        for (size_t i = 0U; i < overlap; ++i) {
            if (group.destinations[i].text == group.sources[i].text) {
                continue;
            }
            ++counts.updated;
            ++counts.operations;
            if (!add_arena_bytes(&counts.arena_bytes,
                                 group.sources[i].text.size())) {
                result.status = Status::InternalError;
                return result;
            }
        }
        for (size_t i = overlap; i < destination_count; ++i) {
            ++counts.removed;
            ++counts.operations;
        }
        for (size_t i = overlap; i < source_count; ++i) {
            ++counts.added;
            ++counts.operations;
            if (!add_arena_bytes(&counts.arena_bytes,
                                 group.sources[i].text.size())
                || !add_arena_bytes(&counts.arena_bytes,
                                    group.descriptor->schema_ns.size())) {
                result.status = Status::InternalError;
                return result;
            }
            std::array<char, 96U> path_storage {};
            std::string_view path;
            const uint32_t ordinal = static_cast<uint32_t>(i + 1U);
            if (!make_destination_path(*group.descriptor, ordinal,
                                       &path_storage, &path)
                || !add_arena_bytes(&counts.arena_bytes, path.size())) {
                result.status = Status::InternalError;
                return result;
            }
        }
    }
    if (counts.added > options.max_added_entries
        || counts.added > kMetadataIptcToXmpMaxAddedEntries) {
        result.status = Status::EntryLimitExceeded;
        return result;
    }
    if (counts.operations > options.max_operations
        || counts.operations > kMetadataIptcToXmpMaxOperations) {
        result.status = Status::OperationLimitExceeded;
        return result;
    }
    if (source.entries().size() > UINT32_MAX - counts.added) {
        result.status = Status::EntryLimitExceeded;
        return result;
    }

    MetaEdit edit;
    edit.reserve_ops(counts.operations);
    edit.arena().reserve(static_cast<size_t>(counts.arena_bytes));
    for (const PlannedGroup& group : groups) {
        if (!group.selected || !group.descriptor || group.preserve
            || group.unchanged) {
            continue;
        }
        const size_t overlap = std::min(group.sources.size(),
                                        group.destinations.size());
        for (size_t i = 0U; i < overlap; ++i) {
            if (group.destinations[i].text == group.sources[i].text) {
                continue;
            }
            const MetaValue value = make_text(edit.arena(),
                                              group.sources[i].text,
                                              TextEncoding::Utf8);
            edit.set_value(group.destinations[i].entry_id, value);
        }
        for (size_t i = overlap; i < group.destinations.size(); ++i) {
            edit.tombstone(group.destinations[i].entry_id);
        }
        for (size_t i = overlap; i < group.sources.size(); ++i) {
            std::array<char, 96U> path_storage {};
            std::string_view path;
            if (!make_destination_path(*group.descriptor,
                                       static_cast<uint32_t>(i + 1U),
                                       &path_storage, &path)) {
                result.status = Status::InternalError;
                return result;
            }
            Entry entry;
            entry.key          = make_xmp_property_key(edit.arena(),
                                                       group.descriptor->schema_ns,
                                                       path);
            entry.value        = make_text(edit.arena(), group.sources[i].text,
                                           TextEncoding::Utf8);
            entry.origin.block = group.sources[i].block;
            entry.origin.order_in_block = group.sources[i].order_in_block;
            entry.flags                 = EntryFlags::Dirty;
            edit.add_entry(entry);
        }
    }
    if (edit.arena().limit_exceeded()
        || edit.ops().size() != counts.operations) {
        result.status = Status::InternalError;
        return result;
    }

    MetaStore built = commit(source, std::span<const MetaEdit>(&edit, 1U));
    if (built.resource_limit_exceeded()) {
        MetadataIptcToXmpResult failure;
        failure.status = Status::InternalError;
        return failure;
    }
    uint32_t groups_translated = 0U;
    uint32_t groups_preserved  = 0U;
    uint32_t groups_unchanged  = 0U;
    for (const PlannedGroup& group : groups) {
        if (!group.selected || !group.descriptor) {
            continue;
        }
        if (group.preserve) {
            ++groups_preserved;
        } else if (group.unchanged) {
            ++groups_unchanged;
        } else {
            const size_t source_count      = group.sources.size();
            const size_t destination_count = group.destinations.size();
            bool changed                   = source_count != destination_count;
            if (!changed) {
                for (size_t i = 0U; i < source_count; ++i) {
                    if (group.destinations[i].text != group.sources[i].text) {
                        changed = true;
                        break;
                    }
                }
            }
            if (changed) {
                ++groups_translated;
            } else {
                ++groups_unchanged;
            }
        }
    }

    *out_store               = std::move(built);
    result.status            = Status::Ok;
    result.source_properties = selected_source_properties;
    result.groups_translated = groups_translated;
    result.groups_preserved  = groups_preserved;
    result.groups_unchanged  = groups_unchanged;
    result.entries_added     = counts.added;
    result.entries_updated   = counts.updated;
    result.entries_removed   = counts.removed;
    return result;
}

const char*
metadata_iptc_to_xmp_status_name(MetadataIptcToXmpStatus status) noexcept
{
    switch (status) {
    case Status::Ok: return "ok";
    case Status::NullOutput: return "null_output";
    case Status::SourceNotFinalized: return "source_not_finalized";
    case Status::InvalidOptions: return "invalid_options";
    case Status::SourceLimitExceeded: return "source_limit_exceeded";
    case Status::AmbiguousSource: return "ambiguous_source";
    case Status::InvalidSourceValue: return "invalid_source_value";
    case Status::ValueTooLong: return "value_too_long";
    case Status::SourceEncodingConflict: return "source_encoding_conflict";
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
