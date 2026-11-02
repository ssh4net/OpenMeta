// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_reconcile.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/validate.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_structure.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static constexpr size_t kMaxInputBytes           = 64U * 1024U;
    static constexpr uint32_t kMaxSourceEntries      = 128U;
    static constexpr uint64_t kMaxSourceArenaBytes   = 64ULL * 1024ULL;
    static constexpr uint64_t kMaxTextBytes          = 64ULL * 1024ULL;
    static constexpr uint64_t kMaxParticipatingBytes = 128ULL * 1024ULL;
    static constexpr uint32_t kMaxOperations         = 128U;
    static constexpr size_t kMaxOutputEntries        = 512U;
#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT
    static constexpr size_t kMaxWrappedInputBytes = kMaxInputBytes + 64U;
#endif

    static constexpr std::string_view kDc = "http://purl.org/dc/elements/1.1/";
    static constexpr std::string_view kTiff = "http://ns.adobe.com/tiff/1.0/";
    static constexpr std::string_view kCustom
        = "urn:openmeta:fuzz:reconcile:custom";
    static constexpr std::string_view kSentinel
        = "urn:openmeta:fuzz:reconcile:sentinel";

    [[noreturn]] static void fuzz_trap() noexcept
    {
#if defined(__clang__) || defined(__GNUC__)
        __builtin_trap();
#else
        std::abort();
#endif
    }

    static void require(bool condition) noexcept
    {
        if (!condition)
            fuzz_trap();
    }

    static bool bytes_equal(std::span<const std::byte> left,
                            std::span<const std::byte> right) noexcept
    {
        if (left.size() != right.size())
            return false;
        for (size_t index = 0U; index < left.size(); ++index) {
            if (left[index] != right[index])
                return false;
        }
        return true;
    }

    static bool same_span(ByteSpan left, ByteSpan right) noexcept
    {
        return left.offset == right.offset && left.size == right.size;
    }

    static bool same_span_contents(const MetaStore& left_store, ByteSpan left,
                                   const MetaStore& right_store,
                                   ByteSpan right) noexcept
    {
        return bytes_equal(left_store.arena().span(left),
                           right_store.arena().span(right));
    }

    static bool same_key(const MetaStore& left_store, const MetaKey& left,
                         const MetaStore& right_store, const MetaKey& right,
                         bool exact_spans) noexcept
    {
        if (left.kind != right.kind)
            return false;
        switch (left.kind) {
        case MetaKeyKind::ExifTag:
            if (left.data.exif_tag.tag != right.data.exif_tag.tag)
                return false;
            if (exact_spans
                && !same_span(left.data.exif_tag.ifd, right.data.exif_tag.ifd))
                return false;
            return same_span_contents(left_store, left.data.exif_tag.ifd,
                                      right_store, right.data.exif_tag.ifd);
        case MetaKeyKind::IptcDataset:
            return left.data.iptc_dataset.record
                       == right.data.iptc_dataset.record
                   && left.data.iptc_dataset.dataset
                          == right.data.iptc_dataset.dataset;
        case MetaKeyKind::XmpProperty:
            if (exact_spans
                && (!same_span(left.data.xmp_property.schema_ns,
                               right.data.xmp_property.schema_ns)
                    || !same_span(left.data.xmp_property.property_path,
                                  right.data.xmp_property.property_path)))
                return false;
            return same_span_contents(left_store,
                                      left.data.xmp_property.schema_ns,
                                      right_store,
                                      right.data.xmp_property.schema_ns)
                   && same_span_contents(left_store,
                                         left.data.xmp_property.property_path,
                                         right_store,
                                         right.data.xmp_property.property_path);
        default: return false;
        }
    }

    static bool same_value(const MetaStore& left_store, const MetaValue& left,
                           const MetaStore& right_store, const MetaValue& right,
                           bool exact_spans) noexcept
    {
        if (left.kind != right.kind || left.elem_type != right.elem_type
            || left.text_encoding != right.text_encoding
            || left.count != right.count)
            return false;
        switch (left.kind) {
        case MetaValueKind::Empty: return true;
        case MetaValueKind::Scalar:
            switch (left.elem_type) {
            case MetaElementType::U8:
            case MetaElementType::U16:
            case MetaElementType::U32:
            case MetaElementType::U64: return left.data.u64 == right.data.u64;
            case MetaElementType::I8:
            case MetaElementType::I16:
            case MetaElementType::I32:
            case MetaElementType::I64: return left.data.i64 == right.data.i64;
            case MetaElementType::F32:
                return left.data.f32_bits == right.data.f32_bits;
            case MetaElementType::F64:
                return left.data.f64_bits == right.data.f64_bits;
            case MetaElementType::URational:
                return left.data.ur.numer == right.data.ur.numer
                       && left.data.ur.denom == right.data.ur.denom;
            case MetaElementType::SRational:
                return left.data.sr.numer == right.data.sr.numer
                       && left.data.sr.denom == right.data.sr.denom;
            }
            return false;
        case MetaValueKind::Array:
        case MetaValueKind::Bytes:
        case MetaValueKind::Text:
            if (exact_spans && !same_span(left.data.span, right.data.span))
                return false;
            return same_span_contents(left_store, left.data.span, right_store,
                                      right.data.span);
        }
        return false;
    }

    static bool same_origin(const MetaStore& left_store, const Origin& left,
                            const MetaStore& right_store, const Origin& right,
                            bool exact_spans) noexcept
    {
        if (left.block != right.block
            || left.order_in_block != right.order_in_block
            || left.wire_type.family != right.wire_type.family
            || left.wire_type.code != right.wire_type.code
            || left.wire_count != right.wire_count
            || left.name_context_kind != right.name_context_kind
            || left.name_context_variant != right.name_context_variant)
            return false;
        if (exact_spans
            && !same_span(left.wire_type_name, right.wire_type_name))
            return false;
        return same_span_contents(left_store, left.wire_type_name, right_store,
                                  right.wire_type_name);
    }

    static bool same_entry(const MetaStore& left_store, const Entry& left,
                           const MetaStore& right_store, const Entry& right,
                           bool exact_spans) noexcept
    {
        return same_key(left_store, left.key, right_store, right.key,
                        exact_spans)
               && same_value(left_store, left.value, right_store, right.value,
                             exact_spans)
               && same_origin(left_store, left.origin, right_store,
                              right.origin, exact_spans)
               && left.flags == right.flags;
    }

    static bool same_store(const MetaStore& left,
                           const MetaStore& right) noexcept
    {
        if (left.is_finalized() != right.is_finalized()
            || left.resource_limit_exceeded() != right.resource_limit_exceeded()
            || left.arena().limit_exceeded() != right.arena().limit_exceeded()
            || left.block_count() != right.block_count()
            || left.entries().size() != right.entries().size()
            || !bytes_equal(left.arena().bytes(), right.arena().bytes()))
            return false;
        for (BlockId block = 0U; block < left.block_count(); ++block) {
            const BlockInfo& left_info  = left.block_info(block);
            const BlockInfo& right_info = right.block_info(block);
            if (left_info.format != right_info.format
                || left_info.container != right_info.container
                || left_info.id != right_info.id)
                return false;
        }
        for (size_t index = 0U; index < left.entries().size(); ++index) {
            if (!same_entry(left, left.entry(static_cast<EntryId>(index)),
                            right, right.entry(static_cast<EntryId>(index)),
                            true))
                return false;
        }
        return true;
    }

    static bool same_result(const MetadataReconcileResult& left,
                            const MetadataReconcileResult& right) noexcept
    {
        return left.status == right.status
               && left.failed_field == right.failed_field
               && left.failed_family == right.failed_family
               && left.failed_stage == right.failed_stage
               && left.failed_entry == right.failed_entry
               && left.validation_issue == right.validation_issue
               && left.source_properties == right.source_properties
               && left.groups_reconciled == right.groups_reconciled
               && left.translator_passes == right.translator_passes
               && left.entries_added == right.entries_added
               && left.entries_updated == right.entries_updated
               && left.entries_removed == right.entries_removed;
    }

    static bool
    mutation_counts_are_zero(const MetadataReconcileResult& result) noexcept
    {
        return result.entries_added == 0U && result.entries_updated == 0U
               && result.entries_removed == 0U;
    }

    struct OwnedText final {
        std::array<char, 64U> bytes {};
        size_t size = 0U;

        std::string_view view() const noexcept
        {
            return std::string_view(bytes.data(), size);
        }
    };

    static void append_char(OwnedText* text, char value) noexcept
    {
        require(text != nullptr && text->size < text->bytes.size());
        text->bytes[text->size] = value;
        ++text->size;
    }

    static void append_literal(OwnedText* text, std::string_view value) noexcept
    {
        for (char item : value)
            append_char(text, item);
    }

    static OwnedText make_text(std::span<const uint8_t> input, size_t offset,
                               char field_code, char family_code,
                               bool same_across_families,
                               bool include_utf8) noexcept
    {
        static constexpr char kHex[]                  = "0123456789abcdef";
        static constexpr std::string_view kUtf8Prefix = "caf\xc3\xa9-b37-";
        OwnedText text;
        append_literal(&text, include_utf8 ? kUtf8Prefix : "b37-");
        append_char(&text, field_code);
        append_char(&text, '-');
        append_char(&text, same_across_families ? '=' : family_code);
        append_char(&text, '-');
        for (size_t index = 0U; index < 6U; ++index) {
            const uint8_t value = input.empty()
                                      ? 0U
                                      : input[(offset + index) % input.size()];
            append_char(&text, kHex[value >> 4U]);
            append_char(&text, kHex[value & 0x0fU]);
        }
        return text;
    }

    enum class ReconcileFieldIndex : size_t {
        Title       = 0U,
        Description = 1U,
        Creator     = 2U,
        Keywords    = 3U,
        Rights      = 4U,
    };

    enum class ReconcileFamilyIndex : size_t {
        Xmp  = 0U,
        Iptc = 1U,
        Exif = 2U,
    };

    struct GeneratedTexts final {
        std::array<std::array<OwnedText, 3U>, 5U> fields {};
        std::array<OwnedText, 3U> sparse_creator {};
        OwnedText title_sibling;
        OwnedText description_sibling;
        OwnedText rights_sibling;
        OwnedText custom_default;
        OwnedText custom_sibling;
        OwnedText custom_value_one;
        OwnedText custom_value_three;
    };

    static uint32_t input_selector(std::span<const uint8_t> input) noexcept
    {
        uint32_t value = 2166136261U;
        for (uint8_t byte : input) {
            value ^= byte;
            value *= 16777619U;
        }
        return value;
    }

    static uint32_t input_value_selector(std::span<const uint8_t> input) noexcept
    {
        uint32_t value = 0x9e3779b9U;
        for (uint8_t byte : input) {
            value ^= byte;
            value *= 0x85ebca6bU;
            value ^= value >> 13U;
        }
        return value;
    }

    static GeneratedTexts
    make_generated_texts(std::span<const uint8_t> input, uint32_t selector,
                         uint32_t values_selector) noexcept
    {
        static constexpr std::array<char, 5U> kFieldCodes  = { 't', 'd', 'c',
                                                               'k', 'r' };
        static constexpr std::array<char, 3U> kFamilyCodes = { 'x', 'i', 'e' };
        const bool include_utf8 = (selector & 0x80000000U) != 0U;
        GeneratedTexts result;
        for (size_t field = 0U; field < result.fields.size(); ++field) {
            const bool same = ((values_selector >> field) & 1U) != 0U;
            for (size_t family = 0U; family < result.fields[field].size();
                 ++family) {
                result.fields[field][family]
                    = make_text(input, field * 3U, kFieldCodes[field],
                                kFamilyCodes[family], same, include_utf8);
            }
            result.sparse_creator[field % result.sparse_creator.size()]
                = make_text(input, 31U + field, 'c', kFamilyCodes[field % 3U],
                            false, include_utf8);
        }
        result.title_sibling       = make_text(input, 41U, 't', 'l', true,
                                               include_utf8);
        result.description_sibling = make_text(input, 43U, 'd', 'l', true,
                                               include_utf8);
        result.rights_sibling      = make_text(input, 47U, 'r', 'l', true,
                                               include_utf8);
        result.custom_default      = make_text(input, 53U, 'u', 'c', true,
                                               include_utf8);
        result.custom_sibling      = make_text(input, 59U, 'v', 'c', true,
                                               include_utf8);
        result.custom_value_one    = make_text(input, 61U, 'w', 'c', true,
                                               include_utf8);
        result.custom_value_three  = make_text(input, 67U, 'z', 'c', true,
                                               include_utf8);
        return result;
    }

    static MetadataAuthoringEntry
    text_entry(MetaKeyView key, std::string_view value,
               TextEncoding encoding = TextEncoding::Utf8) noexcept
    {
        MetadataAuthoringEntry entry;
        entry.key   = key;
        entry.value = make_value_view_text(value, encoding);
        return entry;
    }

    static MetadataAuthoringEntry exif_text_entry(uint16_t tag,
                                                  std::string_view value,
                                                  bool include_utf8) noexcept
    {
        MetadataAuthoringEntry entry
            = text_entry(make_exif_tag_key_view("ifd0", tag), value,
                         include_utf8 ? TextEncoding::Utf8
                                      : TextEncoding::Ascii);
        entry.wire_type = WireType {
            WireFamily::Tiff, static_cast<uint16_t>(include_utf8 ? 129U : 2U)
        };
        entry.wire_count = static_cast<uint32_t>(value.size() + 1U);
        return entry;
    }

    static void
    push_entry(std::array<MetadataAuthoringEntry, kMaxSourceEntries>* entries,
               size_t* count, const MetadataAuthoringEntry& entry) noexcept
    {
        require(entries != nullptr && count != nullptr
                && *count < entries->size());
        (*entries)[*count] = entry;
        ++*count;
    }

    static void
    push_xmp(std::array<MetadataAuthoringEntry, kMaxSourceEntries>* entries,
             size_t* count, std::string_view schema, std::string_view path,
             const OwnedText& value) noexcept
    {
        push_entry(entries, count,
                   text_entry(make_xmp_property_key_view(schema, path),
                              value.view()));
    }

    static MetadataAuthoringOptions
    source_authoring_options(uint32_t selector) noexcept
    {
        MetadataAuthoringOptions options;
        options.max_entries                          = kMaxSourceEntries;
        options.max_arena_bytes                      = kMaxSourceArenaBytes;
        options.max_value_bytes                      = 4096U;
        options.max_key_bytes                        = 256U;
        options.block.format                         = 0x423337U;
        options.block.container                      = 0x524543U;
        options.block.id                             = selector;
        options.validation.max_entries               = kMaxSourceEntries;
        options.validation.max_arena_bytes           = kMaxSourceArenaBytes;
        options.validation.max_value_bytes           = 4096U;
        options.validation.max_key_bytes             = 256U;
        options.validation.structures.max_nodes      = 32U;
        options.validation.structures.max_depth      = 8U;
        options.validation.structures.max_namespaces = 8U;
        options.validation.structures.max_path_bytes = 256U;
        options.validation.structures.max_namespace_bytes = 256U;
        options.validation.structures.max_request_bytes   = 16U * 1024U;
        options.validation.structures.max_array_index     = 16U;
        return options;
    }

    static void build_authored_source(std::span<const uint8_t> input,
                                      uint32_t selector, MetaStore* source)
    {
        const GeneratedTexts texts
            = make_generated_texts(input, selector,
                                   input_value_selector(input));
        const bool include_utf8       = (selector & 0x80000000U) != 0U;
        const bool duplicate_keywords = (selector & 0x00400000U) != 0U;
        std::array<MetadataAuthoringEntry, kMaxSourceEntries> entries {};
        size_t count = 0U;
        for (size_t field = 0U; field < texts.fields.size(); ++field) {
            const OwnedText& xmp  = texts.fields[field][0U];
            const OwnedText& iptc = texts.fields[field][1U];
            const OwnedText& exif = texts.fields[field][2U];
            switch (static_cast<ReconcileFieldIndex>(field)) {
            case ReconcileFieldIndex::Title:
                push_xmp(&entries, &count, kDc, "title[@xml:lang=x-default]",
                         xmp);
                push_xmp(&entries, &count, kDc, "title[@xml:lang=fr-FR]",
                         texts.title_sibling);
                push_entry(&entries, &count,
                           text_entry(make_iptc_dataset_key_view(2U, 5U),
                                      iptc.view()));
                break;
            case ReconcileFieldIndex::Description:
                push_xmp(&entries, &count, kDc,
                         "description[@xml:lang=x-default]", xmp);
                push_xmp(&entries, &count, kDc, "description[@xml:lang=fr-FR]",
                         texts.description_sibling);
                push_xmp(&entries, &count, kTiff, "ImageDescription", xmp);
                push_entry(&entries, &count,
                           text_entry(make_iptc_dataset_key_view(2U, 120U),
                                      iptc.view()));
                break;
            case ReconcileFieldIndex::Creator:
                push_xmp(&entries, &count, kDc, "creator[1]", xmp);
                if ((selector & 0x00100000U) != 0U) {
                    push_xmp(&entries, &count, kDc, "creator[10]",
                             texts.sparse_creator[0U]);
                }
                push_entry(&entries, &count,
                           text_entry(make_iptc_dataset_key_view(2U, 80U),
                                      iptc.view()));
                if ((selector & 0x00200000U) != 0U) {
                    push_entry(&entries, &count,
                               text_entry(make_iptc_dataset_key_view(2U, 80U),
                                          texts.sparse_creator[1U].view()));
                }
                push_entry(&entries, &count,
                           exif_text_entry(0x013bU, exif.view(), include_utf8));
                break;
            case ReconcileFieldIndex::Keywords:
                push_xmp(&entries, &count, kDc, "subject[1]", xmp);
                push_xmp(&entries, &count, kDc, "subject[3]",
                         duplicate_keywords ? xmp : iptc);
                push_entry(&entries, &count,
                           text_entry(make_iptc_dataset_key_view(2U, 25U),
                                      iptc.view()));
                push_entry(&entries, &count,
                           text_entry(make_iptc_dataset_key_view(2U, 25U),
                                      duplicate_keywords ? iptc.view()
                                                         : xmp.view()));
                break;
            case ReconcileFieldIndex::Rights:
                push_xmp(&entries, &count, kDc, "rights[@xml:lang=x-default]",
                         xmp);
                push_xmp(&entries, &count, kDc, "rights[@xml:lang=de-DE]",
                         texts.rights_sibling);
                push_entry(&entries, &count,
                           text_entry(make_iptc_dataset_key_view(2U, 116U),
                                      iptc.view()));
                break;
            }
        }
        push_entry(&entries, &count,
                   exif_text_entry(0x010eU, texts.fields[1U][2U].view(),
                                   include_utf8));
        push_entry(&entries, &count,
                   exif_text_entry(0x8298U, texts.fields[4U][2U].view(),
                                   include_utf8));

        if (include_utf8) {
            static constexpr std::array<std::byte, 3U> kUtf8Charset = {
                std::byte { 0x1bU }, std::byte { 0x25U }, std::byte { 0x47U }
            };
            MetadataAuthoringEntry marker;
            marker.key   = make_iptc_dataset_key_view(1U, 90U);
            marker.value = make_value_view_bytes(kUtf8Charset);
            push_entry(&entries, &count, marker);
        }

        push_entry(&entries, &count,
                   make_xmp_container_entry(kCustom, "Record",
                                            XmpContainerKind::Struct));
        push_entry(&entries, &count,
                   make_xmp_container_entry(kCustom, "Record/Labels",
                                            XmpContainerKind::LangAlt));
        push_xmp(&entries, &count, kCustom,
                 "Record/Labels[@xml:lang=x-default]", texts.custom_default);
        push_xmp(&entries, &count, kCustom, "Record/Labels[@xml:lang=fr-FR]",
                 texts.custom_sibling);
        push_entry(&entries, &count,
                   make_xmp_container_entry(kCustom, "Record/Values",
                                            XmpContainerKind::Seq));
        push_entry(&entries, &count,
                   make_xmp_container_entry(kCustom, "Record/Empty",
                                            XmpContainerKind::Seq));
        push_xmp(&entries, &count, kCustom, "Record/Values[1]",
                 texts.custom_value_one);
        push_xmp(&entries, &count, kCustom, "Record/Values[3]",
                 texts.custom_value_three);

        MetadataAuthoringOptions authoring = source_authoring_options(selector);
        const MetadataAuthoringResult result = create_metadata_store(
            std::span<const MetadataAuthoringEntry>(entries.data(), count),
            source, authoring);
        require(result.ok());
        require(source->is_finalized());
        require(source->entries().size() <= kMaxSourceEntries);
        require(source->arena().bytes().size() <= kMaxSourceArenaBytes);
        for (const Entry& entry : source->entries()) {
            require(entry.origin.block == 0U);
            if (include_utf8 && entry.key.kind == MetaKeyKind::ExifTag) {
                require(entry.origin.wire_type.code == 129U);
                require(entry.value.kind == MetaValueKind::Text);
                require(entry.value.text_encoding == TextEncoding::Utf8);
            }
        }
        if (include_utf8) {
            const std::span<const EntryId> markers = source->find_all(
                make_iptc_dataset_key_view(1U, 90U));
            require(markers.size() == 1U);
            require(source->entry(markers.front()).origin.block == 0U);
            static constexpr std::array<std::byte, 3U> kUtf8Charset = {
                std::byte { 0x1bU }, std::byte { 0x25U }, std::byte { 0x47U }
            };
            require(
                bytes_equal(source->arena().span(
                                source->entry(markers.front()).value.data.span),
                            kUtf8Charset));
        }
    }

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT
    static bool
    make_jpeg_wrapped_xmp(std::span<const uint8_t> packet,
                          std::array<std::byte, kMaxWrappedInputBytes>* output,
                          size_t* output_size) noexcept
    {
        static constexpr char kPrefix[] = "http://ns.adobe.com/xap/1.0/\0";
        constexpr size_t prefix_size    = sizeof(kPrefix) - 1U;
        if (output == nullptr || output_size == nullptr || packet.empty()
            || packet[0U] != static_cast<uint8_t>('<'))
            return false;
        const size_t segment_length = 2U + prefix_size + packet.size();
        if (segment_length > UINT16_MAX)
            return false;
        const size_t needed = 8U + prefix_size + packet.size();
        if (needed > output->size())
            return false;
        size_t cursor       = 0U;
        (*output)[cursor++] = std::byte { 0xff };
        (*output)[cursor++] = std::byte { 0xd8 };
        (*output)[cursor++] = std::byte { 0xff };
        (*output)[cursor++] = std::byte { 0xe1 };
        (*output)[cursor++] = static_cast<std::byte>(segment_length >> 8U);
        (*output)[cursor++] = static_cast<std::byte>(segment_length & 0xffU);
        for (size_t index = 0U; index < prefix_size; ++index)
            (*output)[cursor++] = static_cast<std::byte>(kPrefix[index]);
        for (uint8_t value : packet)
            (*output)[cursor++] = static_cast<std::byte>(value);
        (*output)[cursor++] = std::byte { 0xff };
        (*output)[cursor++] = std::byte { 0xd9 };
        *output_size        = cursor;
        return true;
    }
#endif

    static bool build_raw_xml_source(std::span<const uint8_t> input,
                                     MetaStore* source)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        (void)input;
        (void)source;
        return false;
#else
        std::array<std::byte, kMaxWrappedInputBytes> wrapped {};
        size_t wrapped_size = 0U;
        if (!make_jpeg_wrapped_xmp(input, &wrapped, &wrapped_size))
            return false;
        ReadTransferSourceSnapshotOptions options;
        options.include_pointer_tags                    = false;
        options.decode_makernote                        = false;
        options.decode_embedded_containers              = false;
        options.decompress                              = false;
        options.preserve_custom_xmp_structures          = true;
        options.policy.max_file_bytes                   = kMaxWrappedInputBytes;
        options.policy.xmp_limits.max_depth             = 16U;
        options.policy.xmp_limits.max_properties        = kMaxSourceEntries;
        options.policy.xmp_limits.max_input_bytes       = kMaxInputBytes;
        options.policy.xmp_limits.max_path_bytes        = 256U;
        options.policy.xmp_limits.max_namespace_bytes   = 256U;
        options.policy.xmp_limits.max_value_bytes       = 4096U;
        options.policy.xmp_limits.max_total_value_bytes = kMaxTextBytes;
        options.policy.xmp_limits.max_arena_bytes       = kMaxSourceArenaBytes;
        const ReadTransferSourceSnapshotBytesResult read
            = read_transfer_source_snapshot_bytes(
                std::span<const std::byte>(wrapped.data(), wrapped_size),
                options);
        if (read.status != TransferStatus::Ok
            || read.read.xmp.status != XmpDecodeStatus::Ok)
            return false;
        require(read.snapshot.store.is_finalized());
        require(read.snapshot.store.entries().size() <= kMaxSourceEntries);
        require(read.snapshot.store.arena().bytes().size()
                <= kMaxSourceArenaBytes);
        *source = read.snapshot.store;
        return true;
#endif
    }

    static MetadataAuthoringOptions sentinel_authoring_options() noexcept
    {
        MetadataAuthoringOptions options;
        options.max_entries                = 2U;
        options.max_arena_bytes            = 4096U;
        options.max_value_bytes            = 1024U;
        options.max_key_bytes              = 256U;
        options.validation.max_entries     = 2U;
        options.validation.max_arena_bytes = 4096U;
        options.validation.max_value_bytes = 1024U;
        options.validation.max_key_bytes   = 256U;
        return options;
    }

    static MetaStore make_output_sentinel()
    {
        static constexpr std::string_view kValue = "output sentinel";
        const std::array entries                 = {
            text_entry(make_xmp_property_key_view(kSentinel, "Keep"), kValue)
        };
        MetaStore store;
        const MetadataAuthoringResult created
            = create_metadata_store(entries, &store,
                                    sentinel_authoring_options());
        require(created.ok());
        return store;
    }

    static bool is_descriptive_entry(const MetaStore& store,
                                     const Entry& entry) noexcept
    {
        if (entry.key.kind == MetaKeyKind::IptcDataset) {
            if (entry.key.data.iptc_dataset.record == 1U)
                return entry.key.data.iptc_dataset.dataset == 90U;
            if (entry.key.data.iptc_dataset.record != 2U)
                return false;
            const uint16_t dataset = entry.key.data.iptc_dataset.dataset;
            return dataset == 5U || dataset == 120U || dataset == 80U
                   || dataset == 25U || dataset == 116U;
        }
        if (entry.key.kind == MetaKeyKind::ExifTag) {
            const std::span<const std::byte> ifd = store.arena().span(
                entry.key.data.exif_tag.ifd);
            const std::string_view ifd_name(reinterpret_cast<const char*>(
                                                ifd.data()),
                                            ifd.size());
            const uint16_t tag = entry.key.data.exif_tag.tag;
            return ifd_name == "ifd0"
                   && (tag == 0x010eU || tag == 0x013bU || tag == 0x8298U);
        }
        if (entry.key.kind != MetaKeyKind::XmpProperty)
            return false;
        const std::span<const std::byte> schema = store.arena().span(
            entry.key.data.xmp_property.schema_ns);
        const std::span<const std::byte> path = store.arena().span(
            entry.key.data.xmp_property.property_path);
        const std::string_view schema_name(reinterpret_cast<const char*>(
                                               schema.data()),
                                           schema.size());
        const std::string_view property_path(reinterpret_cast<const char*>(
                                                 path.data()),
                                             path.size());
        if (schema_name == kDc) {
            return property_path == "title[@xml:lang=x-default]"
                   || property_path == "description[@xml:lang=x-default]"
                   || property_path == "rights[@xml:lang=x-default]"
                   || property_path.starts_with("creator[")
                   || property_path.starts_with("subject[");
        }
        return schema_name == kTiff
               && (property_path == "ImageDescription"
                   || property_path == "Artist"
                   || property_path == "Copyright");
    }

    static void
    require_unmanaged_entries_retained(const MetaStore& source,
                                       const MetaStore& output) noexcept
    {
        std::array<bool, kMaxOutputEntries> matched {};
        require(output.entries().size() <= matched.size());
        for (const Entry& expected : source.entries()) {
            if (is_descriptive_entry(source, expected))
                continue;
            bool found = false;
            for (size_t index = 0U; index < output.entries().size(); ++index) {
                if (matched[index]
                    || !same_entry(source, expected, output,
                                   output.entry(static_cast<EntryId>(index)),
                                   false))
                    continue;
                matched[index] = true;
                found          = true;
                break;
            }
            require(found);
        }
    }

    static MetadataReconcileOptions make_options(uint32_t selector) noexcept
    {
        static constexpr std::array<std::array<MetadataReconcileFamily, 3U>, 6U>
            kPriorities = { {
                { MetadataReconcileFamily::Xmp, MetadataReconcileFamily::Iptc,
                  MetadataReconcileFamily::Exif },
                { MetadataReconcileFamily::Xmp, MetadataReconcileFamily::Exif,
                  MetadataReconcileFamily::Iptc },
                { MetadataReconcileFamily::Iptc, MetadataReconcileFamily::Xmp,
                  MetadataReconcileFamily::Exif },
                { MetadataReconcileFamily::Iptc, MetadataReconcileFamily::Exif,
                  MetadataReconcileFamily::Xmp },
                { MetadataReconcileFamily::Exif, MetadataReconcileFamily::Xmp,
                  MetadataReconcileFamily::Iptc },
                { MetadataReconcileFamily::Exif, MetadataReconcileFamily::Iptc,
                  MetadataReconcileFamily::Xmp },
            } };
        MetadataReconcileOptions options;
        options.participate_iptc   = (selector & 1U) != 0U;
        options.enable_exif_bridge = (selector & 2U) != 0U;
        options.title              = (selector & 4U) != 0U;
        options.description        = (selector & 8U) != 0U;
        options.creators           = (selector & 16U) != 0U;
        options.keywords           = (selector & 32U) != 0U;
        options.copyright          = (selector & 64U) != 0U;
        options.family_priority    = kPriorities[(selector >> 7U) % 6U];
        if ((selector & 0x00800000U) != 0U)
            options.family_priority[2U] = options.family_priority[0U];
        options.conflict_policy
            = (selector & 0x400U) != 0U
                  ? MetadataReconcileConflictPolicy::PreferSourceOrder
                  : MetadataReconcileConflictPolicy::FailOnConflict;
        options.deletion_policy
            = (selector & 0x800U) != 0U
                  ? MetadataReconcileDeletionPolicy::PropagateDirty
                  : MetadataReconcileDeletionPolicy::Ignore;
        options.max_source_entries                   = kMaxSourceEntries;
        options.max_source_arena_bytes               = kMaxSourceArenaBytes;
        options.max_source_properties                = kMaxSourceEntries;
        options.max_total_text_bytes                 = kMaxTextBytes;
        options.max_total_participating_bytes        = kMaxParticipatingBytes;
        options.max_operations                       = kMaxOperations;
        options.validation.max_entries               = kMaxSourceEntries;
        options.validation.max_arena_bytes           = kMaxSourceArenaBytes;
        options.validation.max_value_bytes           = kMaxTextBytes;
        options.validation.max_key_bytes             = 256U;
        options.validation.structures.max_nodes      = 32U;
        options.validation.structures.max_depth      = 8U;
        options.validation.structures.max_namespaces = 8U;
        options.validation.structures.max_path_bytes = 256U;
        options.validation.structures.max_namespace_bytes = 256U;
        options.validation.structures.max_request_bytes   = 16U * 1024U;
        options.validation.structures.max_array_index     = 16U;

        if ((selector & 0x1000U) != 0U)
            options.max_source_properties = 1U + ((selector >> 13U) % 8U);
        if ((selector & 0x2000U) != 0U)
            options.max_total_text_bytes = 1U + ((selector >> 16U) % 1024U);
        if ((selector & 0x4000U) != 0U)
            options.max_total_participating_bytes
                = 1U + ((selector >> 19U) % 2048U);
        if ((selector & 0x8000U) != 0U)
            options.max_operations = 1U + ((selector >> 22U) % 16U);
        return options;
    }

    static void apply_optional_title_deletion(MetaStore* source,
                                              uint32_t selector)
    {
        if ((selector & 0x10000U) == 0U)
            return;
        MetadataTypedEditingOperation operation;
        operation.kind = MetadataEditingOperationKind::Remove;
        operation.entry.key
            = make_xmp_property_key_view(kDc, "title[@xml:lang=x-default]");
        operation.occurrence = kMetadataEditingAllOccurrences;
        const MetadataTypedEditingOptions options;
        const MetadataTypedEditingResult result = edit_metadata_typed(
            *source,
            std::span<const MetadataTypedEditingOperation>(&operation, 1U),
            source, options);
        require(result.status == MetadataTypedEditingStatus::Ok);
        require(result.entries_removed == 1U);
        require(source->entries().size() <= kMaxSourceEntries);
        require(source->arena().bytes().size() <= kMaxSourceArenaBytes);
    }

    static MetadataReconcileOptions
    wide_options(const MetadataReconcileOptions& source) noexcept
    {
        MetadataReconcileOptions result = source;
        const MetadataReconcileOptions defaults;
        result.max_source_entries     = defaults.max_source_entries;
        result.max_source_arena_bytes = defaults.max_source_arena_bytes;
        result.max_source_properties  = defaults.max_source_properties;
        result.max_total_text_bytes   = defaults.max_total_text_bytes;
        result.max_total_participating_bytes
            = defaults.max_total_participating_bytes;
        result.max_operations = defaults.max_operations;
        result.validation     = defaults.validation;
        return result;
    }

}  // namespace
}  // namespace openmeta

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    using namespace openmeta;
    if (size > kMaxInputBytes)
        return 0;
    const std::span<const uint8_t> input(data, size);
    const uint32_t selector = input_selector(input);

    MetaStore source;
    const bool raw_xml = !input.empty()
                         && input[0U] == static_cast<uint8_t>('<');
    if (raw_xml) {
        if (!build_raw_xml_source(input, &source))
            return 0;
    } else {
        build_authored_source(input, selector, &source);
        apply_optional_title_deletion(&source, selector);
    }
    require(source.is_finalized());
    require(source.entries().size() <= kMaxSourceEntries);
    require(source.arena().bytes().size() <= kMaxSourceArenaBytes);

    const MetaStore source_before          = source;
    const MetadataReconcileOptions options = make_options(selector);
    const MetaStore output_before          = make_output_sentinel();
    MetaStore first_output                 = output_before;
    const MetadataReconcileResult first
        = reconcile_descriptive_metadata(source, options, &first_output);
    require(same_store(source, source_before));
    require(first.translator_passes <= 3U);

    MetaStore second_output = output_before;
    const MetadataReconcileResult second
        = reconcile_descriptive_metadata(source, options, &second_output);
    require(same_store(source, source_before));
    require(same_result(first, second));

    MetaStore alias = source_before;
    const MetadataReconcileResult alias_result
        = reconcile_descriptive_metadata(alias, options, &alias);
    require(same_result(first, alias_result));
    if (first.status == MetadataReconcileStatus::Ok) {
        require(same_store(first_output, second_output));
        require(same_store(first_output, alias));
        require(first.source_properties <= options.max_source_properties);
        require(static_cast<uint64_t>(first.entries_added)
                    + first.entries_updated + first.entries_removed
                <= options.max_operations);
        require_unmanaged_entries_retained(source_before, first_output);

        MetadataValidationOptions validation;
        validation.require_finalized    = true;
        validation.max_entries          = kMaxSourceEntries + kMaxOperations;
        validation.max_arena_bytes      = kMaxParticipatingBytes;
        validation.max_value_bytes      = kMaxTextBytes;
        validation.max_key_bytes        = 256U;
        validation.structures.max_nodes = 64U;
        validation.structures.max_depth = 8U;
        validation.structures.max_namespaces      = 8U;
        validation.structures.max_path_bytes      = 256U;
        validation.structures.max_namespace_bytes = 256U;
        validation.structures.max_request_bytes   = 16U * 1024U;
        validation.structures.max_array_index     = 16U;
        const MetadataValidationResult validated  = validate_store(first_output,
                                                                   validation);
        require(validated.ok());

        const MetadataReconcileOptions wide = wide_options(options);
        MetaStore wide_output               = make_output_sentinel();
        const MetadataReconcileResult wide_result
            = reconcile_descriptive_metadata(source_before, wide, &wide_output);
        require(wide_result.status == MetadataReconcileStatus::Ok);
        require(wide_result.translator_passes <= 3U);
        require(same_store(first_output, wide_output));

        MetaStore idempotent = wide_output;
        const MetadataReconcileResult idempotent_result
            = reconcile_descriptive_metadata(idempotent, wide, &idempotent);
        require(idempotent_result.status == MetadataReconcileStatus::Ok);
        require(mutation_counts_are_zero(idempotent_result));
        require(idempotent_result.translator_passes <= 3U);
        require(same_store(idempotent, wide_output));
    } else {
        require(mutation_counts_are_zero(first));
        require(same_store(first_output, output_before));
        require(same_store(second_output, output_before));
        require(same_store(alias, source_before));
    }
    return 0;
}
