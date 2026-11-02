// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_xmp_editing.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"
#include "openmeta/xmp_structure.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static constexpr size_t kMaxInputBytes            = 64U * 1024U;
    static constexpr size_t kMaxOutputBytes           = 256U * 1024U;
    static constexpr uint32_t kMaxGeneratedEntries    = 64U;
    static constexpr uint64_t kMaxGeneratedArenaBytes = 64U * 1024U;
    static constexpr uint64_t kMaxGeneratedValueBytes = 4096U;

    static constexpr std::string_view kSchema = "urn:openmeta:fuzz:custom:1";
    static constexpr std::string_view kOtherSchema
        = "urn:openmeta:fuzz:custom:2";
    static constexpr std::string_view kNestedUriPath
        = "Record/nsu_75726e3a6f70656e6d6574613a66757a7a3a6368696c64:Local";
    static constexpr std::string_view kSentinelSchema
        = "urn:openmeta:fuzz:sentinel";

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
        if (!condition) {
            fuzz_trap();
        }
    }

    static XmpStructureLimits fuzz_structure_limits() noexcept
    {
        XmpStructureLimits limits;
        limits.max_nodes           = kMaxGeneratedEntries;
        limits.max_depth           = 4U;
        limits.max_namespaces      = 8U;
        limits.max_path_bytes      = 256U;
        limits.max_namespace_bytes = 256U;
        limits.max_request_bytes   = 16U * 1024U;
        limits.max_array_index     = 16U;
        return limits;
    }

    static XmpDecodeOptions fuzz_decode_options() noexcept
    {
        XmpDecodeOptions options;
        options.preserve_custom_structures   = true;
        options.limits.max_depth             = 16U;
        options.limits.max_properties        = 96U;
        options.limits.max_input_bytes       = kMaxInputBytes;
        options.limits.max_path_bytes        = 256U;
        options.limits.max_namespace_bytes   = 256U;
        options.limits.max_value_bytes       = 4096U;
        options.limits.max_total_value_bytes = 16U * 1024U;
        options.limits.max_arena_bytes       = 64U * 1024U;
        return options;
    }

    static XmpDecodeOptions fuzz_readback_decode_options() noexcept
    {
        XmpDecodeOptions options       = fuzz_decode_options();
        options.limits.max_input_bytes = kMaxOutputBytes;
        // XML escaping expands packets; the writer also adds its toolkit field.
        options.limits.max_properties        = 192U;
        options.limits.max_total_value_bytes = 32U * 1024U;
        options.limits.max_arena_bytes       = 128U * 1024U;
        return options;
    }

    static XmpPortableOptions fuzz_portable_options() noexcept
    {
        XmpPortableOptions options;
        options.limits.max_output_bytes = kMaxOutputBytes;
        options.limits.max_entries      = kMaxGeneratedEntries * 2U;
        options.include_exif            = false;
        options.include_iptc            = false;
        options.include_existing_xmp    = true;
        options.existing_namespace_policy
            = XmpExistingNamespacePolicy::PreserveCustom;
        options.structures = fuzz_structure_limits();
        return options;
    }

    static MetadataAuthoringOptions fuzz_authoring_options() noexcept
    {
        MetadataAuthoringOptions options;
        options.max_entries                = kMaxGeneratedEntries;
        options.max_arena_bytes            = kMaxGeneratedArenaBytes;
        options.max_value_bytes            = kMaxGeneratedValueBytes;
        options.max_key_bytes              = 256U;
        options.validation.max_entries     = kMaxGeneratedEntries;
        options.validation.max_arena_bytes = kMaxGeneratedArenaBytes;
        options.validation.max_value_bytes = kMaxGeneratedValueBytes;
        options.validation.max_key_bytes   = 256U;
        options.validation.structures      = fuzz_structure_limits();
        return options;
    }

    static MetadataXmpEditingOptions fuzz_xmp_editing_options() noexcept
    {
        MetadataXmpEditingOptions options;
        options.max_operations                   = 8U;
        options.max_expanded_operations          = 32U;
        options.max_scope_entries                = kMaxGeneratedEntries;
        options.typed.max_operations             = 32U;
        options.typed.max_entries                = kMaxGeneratedEntries;
        options.typed.max_key_bytes              = 256U;
        options.typed.max_arena_bytes            = kMaxGeneratedArenaBytes;
        options.typed.max_value_bytes            = kMaxGeneratedValueBytes;
        options.typed.max_request_bytes          = 16U * 1024U;
        options.typed.validation.max_entries     = kMaxGeneratedEntries;
        options.typed.validation.max_arena_bytes = kMaxGeneratedArenaBytes;
        options.typed.validation.max_value_bytes = kMaxGeneratedValueBytes;
        options.typed.validation.max_key_bytes   = 256U;
        options.typed.validation.structures      = fuzz_structure_limits();
        return options;
    }

    static std::string_view arena_string(const MetaStore& store,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = store.arena().span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static const Entry* find_xmp_entry(const MetaStore& store,
                                       std::string_view schema,
                                       std::string_view path) noexcept
    {
        const std::span<const EntryId> ids = store.find_all(
            make_xmp_property_key_view(schema, path));
        if (ids.size() != 1U) {
            return nullptr;
        }
        return &store.entry(ids[0U]);
    }

    static bool has_container(const MetaStore& store, std::string_view schema,
                              std::string_view path,
                              XmpContainerKind expected) noexcept
    {
        const Entry* entry = find_xmp_entry(store, schema, path);
        return entry != nullptr && xmp_container_kind(*entry) == expected;
    }

    static bool has_text(const MetaStore& store, std::string_view schema,
                         std::string_view path,
                         std::string_view expected) noexcept
    {
        const Entry* entry = find_xmp_entry(store, schema, path);
        if (entry == nullptr || entry->value.kind != MetaValueKind::Text) {
            return false;
        }
        const std::span<const std::byte> bytes = store.arena().span(
            entry->value.data.span);
        if (bytes.size() != expected.size()) {
            return false;
        }
        const auto* expected_bytes = reinterpret_cast<const std::byte*>(
            expected.data());
        return std::equal(bytes.begin(), bytes.end(), expected_bytes);
    }

    static bool has_no_entry(const MetaStore& store, std::string_view schema,
                             std::string_view path) noexcept
    {
        return store.find_all(make_xmp_property_key_view(schema, path)).empty();
    }

    static void require_container(const MetaStore& store,
                                  std::string_view schema,
                                  std::string_view path,
                                  XmpContainerKind expected) noexcept
    {
        require(has_container(store, schema, path, expected));
    }

    static void require_text(const MetaStore& store, std::string_view schema,
                             std::string_view path,
                             std::string_view expected) noexcept
    {
        require(has_text(store, schema, path, expected));
    }

    static bool path_is_within_declared_root(const MetaStore& roots,
                                             std::string_view schema,
                                             std::string_view path) noexcept
    {
        for (const Entry& root : roots.entries()) {
            if (root.key.kind != MetaKeyKind::XmpProperty
                || any(root.flags, EntryFlags::Deleted)
                || xmp_container_kind(root) == XmpContainerKind::None) {
                continue;
            }
            const std::string_view root_schema
                = arena_string(roots, root.key.data.xmp_property.schema_ns);
            if (root_schema != schema) {
                continue;
            }
            const std::string_view root_path
                = arena_string(roots, root.key.data.xmp_property.property_path);
            if (path == root_path) {
                return true;
            }
            if (path.size() <= root_path.size()
                || path.substr(0U, root_path.size()) != root_path) {
                continue;
            }
            const char boundary = path[root_path.size()];
            if (boundary == '/' || boundary == '[') {
                return true;
            }
        }
        return false;
    }

    static void verify_declarations_preserved(const MetaStore& source,
                                              const MetaStore& readback) noexcept
    {
        uint32_t source_count   = 0U;
        uint32_t readback_count = 0U;
        for (const Entry& entry : source.entries()) {
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || any(entry.flags, EntryFlags::Deleted)
                || xmp_container_kind(entry) == XmpContainerKind::None) {
                continue;
            }
            ++source_count;
            const std::string_view schema
                = arena_string(source, entry.key.data.xmp_property.schema_ns);
            const std::string_view path
                = arena_string(source,
                               entry.key.data.xmp_property.property_path);
            const Entry* actual = find_xmp_entry(readback, schema, path);
            require(actual != nullptr);
            require(xmp_container_kind(*actual) == xmp_container_kind(entry));
        }
        for (const Entry& entry : readback.entries()) {
            if (entry.key.kind == MetaKeyKind::XmpProperty
                && !any(entry.flags, EntryFlags::Deleted)
                && xmp_container_kind(entry) != XmpContainerKind::None) {
                ++readback_count;
            }
        }
        require(source_count == readback_count);

        uint32_t source_text_count   = 0U;
        uint32_t readback_text_count = 0U;
        for (const Entry& entry : source.entries()) {
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || any(entry.flags, EntryFlags::Deleted)
                || entry.value.kind != MetaValueKind::Text) {
                continue;
            }
            const std::string_view schema
                = arena_string(source, entry.key.data.xmp_property.schema_ns);
            const std::string_view path
                = arena_string(source,
                               entry.key.data.xmp_property.property_path);
            if (!path_is_within_declared_root(source, schema, path)) {
                continue;
            }
            ++source_text_count;
            const std::string_view text = arena_string(source,
                                                       entry.value.data.span);
            require(has_text(readback, schema, path, text));
        }
        for (const Entry& entry : readback.entries()) {
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || any(entry.flags, EntryFlags::Deleted)
                || entry.value.kind != MetaValueKind::Text) {
                continue;
            }
            const std::string_view schema
                = arena_string(readback, entry.key.data.xmp_property.schema_ns);
            const std::string_view path
                = arena_string(readback,
                               entry.key.data.xmp_property.property_path);
            if (!path_is_within_declared_root(source, schema, path)) {
                continue;
            }
            ++readback_text_count;
            const Entry* expected = find_xmp_entry(source, schema, path);
            require(expected != nullptr);
            require(expected->value.kind == MetaValueKind::Text);
            const std::string_view text = arena_string(readback,
                                                       entry.value.data.span);
            require(has_text(source, schema, path, text));
        }
        require(source_text_count == readback_text_count);
    }

    static void fuzz_raw_xml(std::span<const std::byte> bytes) noexcept
    {
        MetaStore decoded;
        const XmpDecodeOptions decode_options = fuzz_decode_options();
        const XmpDecodeResult decoded_result
            = decode_xmp_packet(bytes, decoded, EntryFlags::None,
                                decode_options);
        if (decoded_result.status != XmpDecodeStatus::Ok
            || decoded.resource_limit_exceeded()) {
            return;
        }
        decoded.finalize();

        const XmpPortableOptions portable_options = fuzz_portable_options();
        std::array<std::byte, kMaxOutputBytes> first_output {};
        const XmpDumpResult first_dump
            = dump_xmp_portable(decoded, first_output, portable_options);
        if (first_dump.status != XmpDumpStatus::Ok
            || first_dump.written > first_output.size()) {
            return;
        }

        const std::span<const std::byte> first_packet(first_output.data(),
                                                      static_cast<size_t>(
                                                          first_dump.written));
        MetaStore readback;
        const XmpDecodeOptions readback_options = fuzz_readback_decode_options();
        const XmpDecodeResult readback_result
            = decode_xmp_packet(first_packet, readback, EntryFlags::None,
                                readback_options);
        require(readback_result.status == XmpDecodeStatus::Ok);
        require(!readback.resource_limit_exceeded());
        readback.finalize();
        verify_declarations_preserved(decoded, readback);

        std::array<std::byte, kMaxOutputBytes> readback_output {};
        const XmpDumpResult readback_dump
            = dump_xmp_portable(readback, readback_output, portable_options);
        require(readback_dump.status == XmpDumpStatus::Ok);
        require(readback_dump.written <= readback_output.size());

        std::array<std::byte, kMaxOutputBytes> second_output {};
        const XmpDumpResult second_dump
            = dump_xmp_portable(decoded, second_output, portable_options);
        require(second_dump.status == XmpDumpStatus::Ok);
        require(second_dump.written <= second_output.size());
        require(second_dump.written == first_dump.written);
        require(
            std::equal(first_output.begin(),
                       first_output.begin()
                           + static_cast<std::ptrdiff_t>(first_dump.written),
                       second_output.begin()));
    }

    struct GeneratedText final {
        std::array<char, 64U> bytes {};
        size_t size = 0U;

        std::string_view view() const noexcept
        {
            return std::string_view(bytes.data(), size);
        }
    };

    static void append_char(GeneratedText* text, char value) noexcept
    {
        if (text == nullptr || text->size >= text->bytes.size()) {
            fuzz_trap();
        }
        text->bytes[text->size] = value;
        ++text->size;
    }

    static GeneratedText make_generated_text(std::span<const uint8_t> input,
                                             size_t start) noexcept
    {
        static constexpr char kHex[]              = "0123456789abcdef";
        static constexpr std::string_view kPrefix = "v &<>\r\t:";
        GeneratedText text;
        for (const char value : kPrefix) {
            append_char(&text, value);
        }
        if (!input.empty()) {
            for (size_t index = 0U; index < 8U; ++index) {
                const uint8_t value = input[(start + index) % input.size()];
                append_char(&text, kHex[value >> 4U]);
                append_char(&text, kHex[value & 0x0FU]);
            }
        }
        return text;
    }

    struct GeneratedTexts final {
        GeneratedText title;
        GeneratedText title_updated;
        GeneratedText value_one;
        GeneratedText value_three;
        GeneratedText step_one;
        GeneratedText step_two;
        GeneratedText label_default;
        GeneratedText label_english;
        GeneratedText frame_name;
        GeneratedText matrix_one_one;
        GeneratedText matrix_one_two;
        GeneratedText matrix_three_one;
        GeneratedText black_level;
        GeneratedText child_local;
        GeneratedText other_local;
        GeneratedText note_one;
        GeneratedText note_three;
        GeneratedText note_one_updated;
        GeneratedText choice_one;
        GeneratedText choice_three;
    };

    struct GeneratedVariation final {
        XmpContainerKind choice_kind    = XmpContainerKind::Bag;
        bool remove_choice_three        = false;
        bool exercise_lowered_limit     = false;
        bool exercise_invalid_authoring = false;
        bool exercise_parent_removal    = false;
    };

    static GeneratedVariation
    make_generated_variation(std::span<const uint8_t> input) noexcept
    {
        const uint8_t selector = input.empty() ? 0U : input.front();
        GeneratedVariation variation;
        variation.choice_kind = (selector & 1U) == 0U ? XmpContainerKind::Bag
                                                      : XmpContainerKind::Seq;
        variation.remove_choice_three        = (selector & 2U) != 0U;
        variation.exercise_lowered_limit     = (selector & 4U) != 0U;
        variation.exercise_invalid_authoring = (selector & 8U) != 0U;
        variation.exercise_parent_removal    = (selector & 16U) != 0U;
        return variation;
    }

    static GeneratedTexts
    make_generated_texts(std::span<const uint8_t> input) noexcept
    {
        GeneratedTexts text;
        text.title            = make_generated_text(input, 0U);
        text.title_updated    = make_generated_text(input, 1U);
        text.value_one        = make_generated_text(input, 2U);
        text.value_three      = make_generated_text(input, 3U);
        text.step_one         = make_generated_text(input, 4U);
        text.step_two         = make_generated_text(input, 5U);
        text.label_default    = make_generated_text(input, 6U);
        text.label_english    = make_generated_text(input, 7U);
        text.frame_name       = make_generated_text(input, 8U);
        text.matrix_one_one   = make_generated_text(input, 9U);
        text.matrix_one_two   = make_generated_text(input, 10U);
        text.matrix_three_one = make_generated_text(input, 11U);
        text.black_level      = make_generated_text(input, 12U);
        text.child_local      = make_generated_text(input, 13U);
        text.other_local      = make_generated_text(input, 14U);
        text.note_one         = make_generated_text(input, 15U);
        text.note_three       = make_generated_text(input, 16U);
        text.note_one_updated = make_generated_text(input, 17U);
        text.choice_one       = make_generated_text(input, 18U);
        text.choice_three     = make_generated_text(input, 19U);
        return text;
    }

    static MetadataAuthoringEntry
    make_text_entry(std::string_view schema, std::string_view path,
                    std::string_view text) noexcept
    {
        MetadataAuthoringEntry entry;
        entry.key   = make_xmp_property_key_view(schema, path);
        entry.value = make_value_view_text(text, TextEncoding::Utf8);
        return entry;
    }

    static void push_entry(
        std::array<MetadataAuthoringEntry, kMaxGeneratedEntries>* entries,
        size_t* count, const MetadataAuthoringEntry& entry) noexcept
    {
        if (entries == nullptr || count == nullptr
            || *count >= entries->size()) {
            fuzz_trap();
        }
        (*entries)[*count] = entry;
        ++(*count);
    }

    static void push_container(
        std::array<MetadataAuthoringEntry, kMaxGeneratedEntries>* entries,
        size_t* count, std::string_view schema, std::string_view path,
        XmpContainerKind kind) noexcept
    {
        push_entry(entries, count,
                   make_xmp_container_entry(schema, path, kind));
    }

    static void
    push_text(std::array<MetadataAuthoringEntry, kMaxGeneratedEntries>* entries,
              size_t* count, std::string_view schema, std::string_view path,
              std::string_view value) noexcept
    {
        push_entry(entries, count, make_text_entry(schema, path, value));
    }

    static size_t make_generated_entries(
        const GeneratedTexts& text, const GeneratedVariation& variation,
        std::array<MetadataAuthoringEntry, kMaxGeneratedEntries>* entries) noexcept
    {
        size_t count = 0U;
        push_container(entries, &count, kSchema, "Record",
                       XmpContainerKind::Struct);
        push_text(entries, &count, kSchema, "Record/Title", text.title.view());
        push_container(entries, &count, kSchema, "Record/Values",
                       XmpContainerKind::Bag);
        push_text(entries, &count, kSchema, "Record/Values[1]",
                  text.value_one.view());
        push_text(entries, &count, kSchema, "Record/Values[3]",
                  text.value_three.view());
        push_container(entries, &count, kSchema, "Record/Steps",
                       XmpContainerKind::Seq);
        push_text(entries, &count, kSchema, "Record/Steps[1]",
                  text.step_one.view());
        push_text(entries, &count, kSchema, "Record/Steps[2]",
                  text.step_two.view());
        push_container(entries, &count, kSchema, "Record/Labels",
                       XmpContainerKind::LangAlt);
        push_text(entries, &count, kSchema,
                  "Record/Labels[@xml:lang=x-default]",
                  text.label_default.view());
        push_text(entries, &count, kSchema, "Record/Labels[@xml:lang=en-US]",
                  text.label_english.view());
        push_container(entries, &count, kSchema, "Record/Frames",
                       XmpContainerKind::Seq);
        push_container(entries, &count, kSchema, "Record/Frames[1]",
                       XmpContainerKind::Struct);
        push_text(entries, &count, kSchema, "Record/Frames[1]/Name",
                  text.frame_name.view());
        push_container(entries, &count, kSchema, "Record/Matrix",
                       XmpContainerKind::Seq);
        push_container(entries, &count, kSchema, "Record/Matrix[1]",
                       XmpContainerKind::Seq);
        push_text(entries, &count, kSchema, "Record/Matrix[1][1]",
                  text.matrix_one_one.view());
        push_text(entries, &count, kSchema, "Record/Matrix[1][2]",
                  text.matrix_one_two.view());
        push_container(entries, &count, kSchema, "Record/Matrix[3]",
                       XmpContainerKind::Seq);
        push_text(entries, &count, kSchema, "Record/Matrix[3][1]",
                  text.matrix_three_one.view());
        push_container(entries, &count, kSchema, "Record/EmptyStruct",
                       XmpContainerKind::Struct);
        push_container(entries, &count, kSchema, "Record/EmptyBag",
                       XmpContainerKind::Bag);
        push_text(entries, &count, kSchema, "Record/BlackLevel",
                  text.black_level.view());
        push_container(entries, &count, kSchema, "Record/Choice",
                       variation.choice_kind);
        push_text(entries, &count, kSchema, "Record/Choice[1]",
                  text.choice_one.view());
        push_text(entries, &count, kSchema, "Record/Choice[3]",
                  text.choice_three.view());
        push_text(entries, &count, kSchema, kNestedUriPath,
                  text.child_local.view());
        push_container(entries, &count, kOtherSchema, "Record",
                       XmpContainerKind::Struct);
        push_text(entries, &count, kOtherSchema, "Record/Local",
                  text.other_local.view());
        return count;
    }

    enum class GeneratedStage : uint8_t {
        Initial,
        Edited,
        RoundTrip,
    };

    static void verify_generated_store(const MetaStore& store,
                                       const GeneratedTexts& text,
                                       const GeneratedVariation& variation,
                                       GeneratedStage stage) noexcept
    {
        require_container(store, kSchema, "Record", XmpContainerKind::Struct);
        require_container(store, kSchema, "Record/Values",
                          XmpContainerKind::Bag);
        require_container(store, kSchema, "Record/Steps",
                          XmpContainerKind::Seq);
        require_container(store, kSchema, "Record/Labels",
                          XmpContainerKind::LangAlt);
        require_container(store, kSchema, "Record/Frames",
                          XmpContainerKind::Seq);
        require_container(store, kSchema, "Record/Frames[1]",
                          XmpContainerKind::Struct);
        require_container(store, kSchema, "Record/EmptyStruct",
                          XmpContainerKind::Struct);
        require_container(store, kSchema, "Record/EmptyBag",
                          XmpContainerKind::Bag);
        require_container(store, kSchema, "Record/Matrix",
                          XmpContainerKind::Seq);
        require_container(store, kSchema, "Record/Choice",
                          variation.choice_kind);
        require_container(store, kOtherSchema, "Record",
                          XmpContainerKind::Struct);

        const bool round_trip = stage == GeneratedStage::RoundTrip;
        require_container(store, kSchema,
                          round_trip ? "Record/Matrix[2]" : "Record/Matrix[3]",
                          XmpContainerKind::Seq);
        require_container(store, kSchema, "Record/Matrix[1]",
                          XmpContainerKind::Seq);

        const std::string_view title = stage == GeneratedStage::Initial
                                           ? text.title.view()
                                           : text.title_updated.view();
        require_text(store, kSchema, "Record/Title", title);
        require_text(store, kSchema, "Record/Values[1]", text.value_one.view());
        require_text(store, kSchema, "Record/Steps[1]", text.step_one.view());
        require_text(store, kSchema, "Record/Steps[2]", text.step_two.view());
        require_text(store, kSchema, "Record/Labels[@xml:lang=x-default]",
                     text.label_default.view());
        require_text(store, kSchema, "Record/Labels[@xml:lang=en-US]",
                     text.label_english.view());
        require_text(store, kSchema, "Record/Frames[1]/Name",
                     text.frame_name.view());
        require_text(store, kSchema, "Record/Matrix[1][1]",
                     text.matrix_one_one.view());
        require_text(store, kSchema, "Record/Matrix[1][2]",
                     text.matrix_one_two.view());
        require_text(store, kSchema,
                     round_trip ? "Record/Matrix[2][1]" : "Record/Matrix[3][1]",
                     text.matrix_three_one.view());
        require_text(store, kSchema, "Record/BlackLevel",
                     text.black_level.view());
        require_text(store, kSchema, kNestedUriPath, text.child_local.view());
        require_text(store, kOtherSchema, "Record/Local",
                     text.other_local.view());

        if (stage == GeneratedStage::Initial) {
            require_text(store, kSchema, "Record/Choice[1]",
                         text.choice_one.view());
            require_text(store, kSchema, "Record/Choice[3]",
                         text.choice_three.view());
            require(has_no_entry(store, kSchema, "Record/Choice[2]"));
        } else if (variation.remove_choice_three) {
            require_text(store, kSchema, "Record/Choice[1]",
                         text.choice_one.view());
            require(has_no_entry(store, kSchema, "Record/Choice[2]"));
            require(has_no_entry(store, kSchema, "Record/Choice[3]"));
        } else if (stage == GeneratedStage::RoundTrip) {
            require_text(store, kSchema, "Record/Choice[1]",
                         text.choice_one.view());
            require_text(store, kSchema, "Record/Choice[2]",
                         text.choice_three.view());
            require(has_no_entry(store, kSchema, "Record/Choice[3]"));
        } else {
            require_text(store, kSchema, "Record/Choice[1]",
                         text.choice_one.view());
            require_text(store, kSchema, "Record/Choice[3]",
                         text.choice_three.view());
            require(has_no_entry(store, kSchema, "Record/Choice[2]"));
        }

        if (stage == GeneratedStage::Initial) {
            require_text(store, kSchema, "Record/Values[3]",
                         text.value_three.view());
            require(has_no_entry(store, kSchema, "Record/Notes"));
        } else {
            require(has_no_entry(store, kSchema, "Record/Values[3]"));
            require_container(store, kSchema, "Record/Notes",
                              XmpContainerKind::Bag);
            require_text(store, kSchema, "Record/Notes[1]",
                         text.note_one_updated.view());
            if (round_trip) {
                require_text(store, kSchema, "Record/Notes[2]",
                             text.note_three.view());
                require(has_no_entry(store, kSchema, "Record/Notes[3]"));
            } else {
                require_text(store, kSchema, "Record/Notes[3]",
                             text.note_three.view());
                require(has_no_entry(store, kSchema, "Record/Notes[2]"));
            }
        }

        require(has_no_entry(store, kSchema, "Record/Values[2]"));
        require(
            has_no_entry(store, kSchema,
                         round_trip ? "Record/Matrix[3]" : "Record/Matrix[2]"));
    }

    struct LosslessSnapshot final {
        std::array<std::byte, kMaxOutputBytes> bytes {};
        size_t size = 0U;
    };

    static LosslessSnapshot
    take_lossless_snapshot(const MetaStore& store) noexcept
    {
        LosslessSnapshot snapshot;
        XmpDumpOptions options;
        options.limits.max_output_bytes = kMaxOutputBytes;
        const XmpDumpResult dumped = dump_xmp_lossless(store, snapshot.bytes,
                                                       options);
        require(dumped.status == XmpDumpStatus::Ok);
        require(dumped.written <= snapshot.bytes.size());
        snapshot.size = static_cast<size_t>(dumped.written);
        return snapshot;
    }

    static void
    require_lossless_snapshot_unchanged(const MetaStore& store,
                                        const LosslessSnapshot& before) noexcept
    {
        const LosslessSnapshot after = take_lossless_snapshot(store);
        require(before.size == after.size);
        require(std::equal(before.bytes.begin(),
                           before.bytes.begin()
                               + static_cast<std::ptrdiff_t>(before.size),
                           after.bytes.begin()));
    }

    static void create_sentinel_store(MetaStore* output) noexcept
    {
        const std::array<MetadataAuthoringEntry, 1U> sentinel_entry = {
            make_text_entry(kSentinelSchema, "Preserve", "unchanged"),
        };
        const MetadataAuthoringResult created
            = create_metadata_store(sentinel_entry, output,
                                    fuzz_authoring_options());
        require(created.ok());
    }

    static void fuzz_transactional_edit(const MetaStore& base,
                                        const GeneratedTexts& text) noexcept
    {
        MetaStore output;
        create_sentinel_store(&output);
        const LosslessSnapshot before = take_lossless_snapshot(output);

        const std::array<MetadataAuthoringEntry, 1U> replacement = {
            make_text_entry(kSchema, "Record/Title", text.title_updated.view()),
        };
        const std::array<MetadataXmpEditingOperation, 2U> operations = { {
            { MetadataXmpEditingOperationKind::Replace, kSchema, "Record/Title",
              replacement },
            { MetadataXmpEditingOperationKind::Remove,
              kSchema,
              "Record/Missing",
              {} },
        } };
        const MetadataXmpEditingResult edited
            = edit_metadata_xmp(base, operations, &output,
                                fuzz_xmp_editing_options());
        require(!edited.ok());
        require_lossless_snapshot_unchanged(output, before);
        require_text(output, kSentinelSchema, "Preserve", "unchanged");
        require(has_no_entry(output, kSchema, "Record/Title"));
    }

    static void
    fuzz_rejected_authoring(const GeneratedVariation& variation) noexcept
    {
        if (!variation.exercise_invalid_authoring) {
            return;
        }

        MetaStore output;
        create_sentinel_store(&output);
        const LosslessSnapshot before = take_lossless_snapshot(output);
        std::array<MetadataAuthoringEntry, 2U> invalid_entries = { {
            make_xmp_container_entry(kSchema, "Record",
                                     XmpContainerKind::Struct),
            make_xmp_container_entry(kSchema, "Record/Invalid",
                                     XmpContainerKind::Bag),
        } };
        invalid_entries[1U].wire_count                         = 1U;

        const MetadataAuthoringResult result
            = create_metadata_store(invalid_entries, &output,
                                    fuzz_authoring_options());
        require(!result.ok());
        require_lossless_snapshot_unchanged(output, before);
        require_text(output, kSentinelSchema, "Preserve", "unchanged");
    }

    static void
    fuzz_rejected_parent_removal(const MetaStore& base,
                                 const GeneratedVariation& variation) noexcept
    {
        if (!variation.exercise_parent_removal) {
            return;
        }

        MetaStore output;
        create_sentinel_store(&output);
        const LosslessSnapshot before = take_lossless_snapshot(output);
        MetadataTypedEditingOperation removal;
        removal.kind      = MetadataEditingOperationKind::Remove;
        removal.entry.key = make_xmp_property_key_view(kSchema, "Record");
        const std::array<MetadataTypedEditingOperation, 1U> operations = {
            removal,
        };
        const MetadataXmpEditingOptions edit_options
            = fuzz_xmp_editing_options();
        const MetadataTypedEditingResult result
            = edit_metadata_typed(base, operations, &output,
                                  edit_options.typed);
        require(!result.ok());
        require_lossless_snapshot_unchanged(output, before);
        require_text(output, kSentinelSchema, "Preserve", "unchanged");
    }

    static void
    fuzz_lowered_structure_limit(const MetaStore& store,
                                 const GeneratedVariation& variation) noexcept
    {
        if (!variation.exercise_lowered_limit) {
            return;
        }

        const LosslessSnapshot before = take_lossless_snapshot(store);
        XmpPortableOptions options    = fuzz_portable_options();
        options.structures.max_nodes  = 1U;
        std::array<std::byte, kMaxOutputBytes> output {};
        const XmpDumpResult result = dump_xmp_portable(store, output, options);
        require(result.status == XmpDumpStatus::LimitExceeded);
        for (const std::byte byte : output)
            require(byte == std::byte { 0 });
        require_lossless_snapshot_unchanged(store, before);
    }

    static size_t dump_generated_store(
        const MetaStore& store, const XmpPortableOptions& options,
        std::array<std::byte, kMaxOutputBytes>* output) noexcept
    {
        const XmpDumpResult result = dump_xmp_portable(store, *output, options);
        require(result.status == XmpDumpStatus::Ok);
        require(result.written <= output->size());
        return static_cast<size_t>(result.written);
    }

    static void fuzz_generated_tree(std::span<const uint8_t> input) noexcept
    {
        const GeneratedTexts text          = make_generated_texts(input);
        const GeneratedVariation variation = make_generated_variation(input);
        std::array<MetadataAuthoringEntry, kMaxGeneratedEntries> entries {};
        const size_t entry_count = make_generated_entries(text, variation,
                                                          &entries);
        require(entry_count <= kMaxGeneratedEntries);

        MetaStore original;
        const MetadataAuthoringResult created = create_metadata_store(
            std::span<const MetadataAuthoringEntry>(entries.data(), entry_count),
            &original, fuzz_authoring_options());
        require(created.ok());
        verify_generated_store(original, text, variation,
                               GeneratedStage::Initial);
        fuzz_transactional_edit(original, text);
        fuzz_rejected_authoring(variation);
        fuzz_rejected_parent_removal(original, variation);

        const std::array<MetadataAuthoringEntry, 3U> notes = {
            make_xmp_container_entry(kSchema, "Record/Notes",
                                     XmpContainerKind::Bag),
            make_text_entry(kSchema, "Record/Notes[1]", text.note_one.view()),
            make_text_entry(kSchema, "Record/Notes[3]", text.note_three.view()),
        };
        const std::array<MetadataAuthoringEntry, 1U> note_replacement = {
            make_text_entry(kSchema, "Record/Notes[1]",
                            text.note_one_updated.view()),
        };
        const std::array<MetadataAuthoringEntry, 1U> title_replacement = {
            make_text_entry(kSchema, "Record/Title", text.title_updated.view()),
        };
        std::array<MetadataXmpEditingOperation, 5U> operations = { {
            { MetadataXmpEditingOperationKind::Add, kSchema, "Record/Notes",
              notes },
            { MetadataXmpEditingOperationKind::Replace, kSchema,
              "Record/Notes[1]", note_replacement },
            { MetadataXmpEditingOperationKind::Replace, kSchema, "Record/Title",
              title_replacement },
            { MetadataXmpEditingOperationKind::Remove,
              kSchema,
              "Record/Values[3]",
              {} },
        } };
        size_t operation_count                                 = 4U;
        if (variation.remove_choice_three) {
            operations[operation_count]
                = { MetadataXmpEditingOperationKind::Remove,
                    kSchema,
                    "Record/Choice[3]",
                    {} };
            ++operation_count;
        }

        MetaStore edited;
        const MetadataXmpEditingResult edit_result = edit_metadata_xmp(
            original,
            std::span<const MetadataXmpEditingOperation>(operations.data(),
                                                         operation_count),
            &edited, fuzz_xmp_editing_options());
        require(edit_result.ok());
        verify_generated_store(edited, text, variation, GeneratedStage::Edited);
        fuzz_lowered_structure_limit(edited, variation);

        const XmpPortableOptions portable_options = fuzz_portable_options();
        std::array<std::byte, kMaxOutputBytes> first_output {};
        const size_t first_size = dump_generated_store(edited, portable_options,
                                                       &first_output);
        const std::span<const std::byte> first_packet(first_output.data(),
                                                      first_size);

        MetaStore readback;
        const XmpDecodeResult decoded
            = decode_xmp_packet(first_packet, readback, EntryFlags::None,
                                fuzz_readback_decode_options());
        require(decoded.status == XmpDecodeStatus::Ok);
        require(!readback.resource_limit_exceeded());
        readback.finalize();
        verify_generated_store(readback, text, variation,
                               GeneratedStage::RoundTrip);

        std::array<std::byte, kMaxOutputBytes> readback_output {};
        const size_t readback_size = dump_generated_store(readback,
                                                          portable_options,
                                                          &readback_output);
        require(readback_size > 0U);

        std::array<std::byte, kMaxOutputBytes> second_output {};
        const size_t second_size
            = dump_generated_store(edited, portable_options, &second_output);
        require(first_size == second_size);
        require(std::equal(first_output.begin(),
                           first_output.begin()
                               + static_cast<std::ptrdiff_t>(first_size),
                           second_output.begin()));
    }

}  // namespace
}  // namespace openmeta

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size > openmeta::kMaxInputBytes) {
        return 0;
    }
    const std::span<const uint8_t> input(data, size);
    const std::span<const std::byte> xml(reinterpret_cast<const std::byte*>(
                                             data),
                                         size);
    openmeta::fuzz_raw_xml(xml);
    openmeta::fuzz_generated_tree(input);
    return 0;
}
