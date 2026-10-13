// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"
#include "openmeta/exif_tiff_serialize.h"
#include "openmeta/metadata_translation.h"
#include "openmeta/validate.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    constexpr std::string_view kTiff = "http://ns.adobe.com/tiff/1.0/";
    constexpr std::string_view kExif = "http://ns.adobe.com/exif/1.0/";
    using Status = MetadataCaptureTranslationStatus;

    void xmp(MetaStore& store, std::string_view ns, std::string_view path,
             std::string_view value,
             EntryFlags flags = EntryFlags::Dirty)
    {
        Entry entry;
        entry.key = make_xmp_property_key(store.arena(), ns, path);
        entry.value = make_text(store.arena(), value, TextEncoding::Utf8);
        entry.flags = flags;
        (void)store.add_entry(entry);
    }

    const Entry* find(const MetaStore& store, std::string_view ifd,
                      uint16_t tag)
    {
        const auto ids = store.find_all(make_exif_tag_key_view(ifd, tag));
        return ids.size() == 1U ? &store.entry(ids.front()) : nullptr;
    }

    const Entry* find_xmp(const MetaStore& store, std::string_view ns,
                          std::string_view path)
    {
        const auto ids = store.find_all(make_xmp_property_key_view(ns, path));
        return ids.size() == 1U ? &store.entry(ids.front()) : nullptr;
    }

    const Entry* find_deleted(const MetaStore& store, std::string_view ifd,
                              uint16_t tag)
    {
        for (EntryId id = 0U; id < store.entries().size(); ++id) {
            const Entry& entry = store.entry(id);
            if (!any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != tag)
                continue;
            const auto raw = store.arena().span(entry.key.data.exif_tag.ifd);
            const std::string_view entry_ifd(
                reinterpret_cast<const char*>(raw.data()), raw.size());
            if (entry_ifd == ifd)
                return &entry;
        }
        return nullptr;
    }

    std::string text(const MetaStore& store, const Entry& entry)
    {
        const auto raw = store.arena().span(entry.value.data.span);
        return { reinterpret_cast<const char*>(raw.data()), raw.size() };
    }

    static uint16_t read_tiff_u16_le(std::span<const std::byte> bytes,
                                     size_t offset) noexcept
    {
        if (offset > bytes.size() || bytes.size() - offset < 2U)
            return 0U;
        return static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset]))
               | static_cast<uint16_t>(
                   static_cast<uint16_t>(
                       std::to_integer<uint8_t>(bytes[offset + 1U]))
                   << 8U);
    }

    static uint32_t read_tiff_u32_le(std::span<const std::byte> bytes,
                                     size_t offset) noexcept
    {
        if (offset > bytes.size() || bytes.size() - offset < 4U)
            return 0U;
        uint32_t value = 0U;
        for (size_t i = 0U; i < 4U; ++i)
            value |= static_cast<uint32_t>(
                         std::to_integer<uint8_t>(bytes[offset + i]))
                     << (8U * i);
        return value;
    }

    static bool find_tiff_ifd0_wire(std::span<const std::byte> bytes,
                                    uint16_t tag, uint16_t* type,
                                    uint32_t* count) noexcept
    {
        if (!type || !count || bytes.size() < 8U
            || bytes[0U] != std::byte { 'I' }
            || bytes[1U] != std::byte { 'I' }
            || read_tiff_u16_le(bytes, 2U) != 42U)
            return false;
        const uint32_t ifd0_offset = read_tiff_u32_le(bytes, 4U);
        if (ifd0_offset > bytes.size()
            || bytes.size() - static_cast<size_t>(ifd0_offset) < 2U)
            return false;
        const size_t ifd0 = static_cast<size_t>(ifd0_offset);
        const uint16_t entries = read_tiff_u16_le(bytes, ifd0);
        const size_t entries_offset = ifd0 + 2U;
        if (entries > (bytes.size() - entries_offset) / 12U)
            return false;
        for (size_t i = 0U; i < entries; ++i) {
            const size_t entry_offset = entries_offset + i * 12U;
            if (read_tiff_u16_le(bytes, entry_offset) != tag)
                continue;
            *type = read_tiff_u16_le(bytes, entry_offset + 2U);
            *count = read_tiff_u32_le(bytes, entry_offset + 4U);
            return true;
        }
        return false;
    }

    void profile_description_with_native(MetaStore& store,
                                         uint16_t wire_type,
                                         uint32_t wire_count,
                                         EntryFlags native_flags)
    {
        xmp(store, kTiff, "ImageDescription", "Scene", EntryFlags::None);
        Entry native;
        native.key = make_exif_tag_key(store.arena(), "ifd0", 0x010eU);
        const std::string retained("Scene\0", 6U);
        native.value = make_text(
            store.arena(), std::string_view(retained.data(), retained.size()),
            wire_type == 129U ? TextEncoding::Utf8 : TextEncoding::Ascii);
        native.origin.wire_type = { WireFamily::Tiff, wire_type };
        native.origin.wire_count = wire_count;
        native.origin.wire_type_name = store.arena().append_string("UTF-8");
        native.origin.order_in_block = 7U;
        native.flags = native_flags;
        (void)store.add_entry(native);
        store.finalize();
    }

    void profile_three_descriptive_129(MetaStore& store,
                                       EntryFlags native_flags,
                                       TextEncoding encoding)
    {
        constexpr std::array<std::string_view, 3U> kPaths = {
            "ImageDescription", "Artist", "Copyright" };
        constexpr std::array<std::string_view, 3U> kValues = {
            "Scene", "Artist", "Owner" };
        constexpr std::array<uint16_t, 3U> kTags = {
            0x010eU, 0x013bU, 0x8298U };
        for (size_t i = 0U; i < kPaths.size(); ++i) {
            xmp(store, kTiff, kPaths[i], kValues[i], EntryFlags::None);
            Entry native;
            native.key = make_exif_tag_key(store.arena(), "ifd0", kTags[i]);
            native.value = make_text(store.arena(), kValues[i], encoding);
            native.origin.wire_type = { WireFamily::Tiff, 129U };
            native.origin.wire_count
                = static_cast<uint32_t>(kValues[i].size()) + 1U;
            native.origin.wire_type_name
                = store.arena().append_string("UTF-8");
            native.origin.order_in_block = kTags[i];
            native.flags = native_flags;
            (void)store.add_entry(native);
        }
        store.finalize();
    }

    void profile_text_native(MetaStore& store, std::string_view ifd,
                             uint16_t tag, std::string_view value,
                             uint16_t wire_type, uint32_t wire_count,
                             EntryFlags flags = EntryFlags::None)
    {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd, tag);
        entry.value = make_text(
            store.arena(), value,
            wire_type == 129U ? TextEncoding::Utf8 : TextEncoding::Ascii);
        entry.origin.wire_type = { WireFamily::Tiff, wire_type };
        entry.origin.wire_count = wire_count;
        entry.origin.wire_type_name = store.arena().append_string("profile");
        entry.origin.order_in_block = tag;
        entry.flags = flags;
        (void)store.add_entry(entry);
    }

    void profile_deleted_sources(MetaStore& store)
    {
        xmp(store, kTiff, "ImageDescription", "",
            EntryFlags::Dirty | EntryFlags::Deleted);
        xmp(store, kTiff, "Artist", "",
            EntryFlags::Dirty | EntryFlags::Deleted);
        xmp(store, kTiff, "Copyright", "",
            EntryFlags::Dirty | EntryFlags::Deleted);
        xmp(store, kExif, "ColorSpace", "",
            EntryFlags::Dirty | EntryFlags::Deleted);
        xmp(store, kExif, "RelatedSoundFile", "",
            EntryFlags::Dirty | EntryFlags::Deleted);
    }

    void profile_native_tombstone(MetaStore& store, std::string_view ifd,
                                  uint16_t tag, uint16_t wire_type,
                                  EntryFlags flags)
    {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd, tag);
        if (tag == 0xa001U)
            entry.value = make_u16(1U);
        else
            entry.value = make_text(store.arena(), "Old",
                                    wire_type == 129U ? TextEncoding::Utf8
                                                      : TextEncoding::Ascii);
        entry.origin.wire_type = { WireFamily::Tiff, wire_type };
        entry.origin.wire_count = tag == 0xa001U ? 1U : 4U;
        entry.origin.wire_type_name
            = store.arena().append_string("previous");
        entry.origin.order_in_block = tag;
        entry.flags = flags | EntryFlags::Deleted;
        (void)store.add_entry(entry);
    }

    TEST(MetadataProfileTranslation,
         ExactTextMatchesPromoteOnlyWithSerializerValidWireCount)
    {
        MetaStore clean;
        profile_description_with_native(clean, 129U, 6U, EntryFlags::None);
        const Entry* before = find(clean, "ifd0", 0x010eU);
        ASSERT_NE(before, nullptr);
        const std::string before_value = text(clean, *before);
        const WireType before_wire_type = before->origin.wire_type;
        const uint32_t before_wire_count = before->origin.wire_count;
        const uint32_t before_order = before->origin.order_in_block;
        const std::string before_wire_type_name(
            reinterpret_cast<const char*>(clean.arena()
                                              .span(before->origin.wire_type_name)
                                              .data()),
            clean.arena().span(before->origin.wire_type_name).size());

        MetadataProfileTranslationOptions options;
        options.source_mode = MetadataCaptureTranslationSourceMode::All;
        const auto promoted
            = translate_xmp_profile_metadata(clean, options, &clean);
        ASSERT_EQ(promoted.status, Status::Ok);
        EXPECT_EQ(promoted.entries_updated, 1U);
        EXPECT_EQ(promoted.entries_added, 0U);
        EXPECT_EQ(promoted.groups_translated, 1U);
        const Entry* after = find(clean, "ifd0", 0x010eU);
        ASSERT_NE(after, nullptr);
        EXPECT_TRUE(any(after->flags, EntryFlags::Dirty));
        EXPECT_FALSE(any(after->flags, EntryFlags::Deleted));
        EXPECT_EQ(text(clean, *after), before_value);
        EXPECT_EQ(after->value.kind, MetaValueKind::Text);
        EXPECT_EQ(after->value.text_encoding, TextEncoding::Utf8);
        EXPECT_EQ(after->value.count, 6U);
        EXPECT_EQ(after->origin.wire_type.family, before_wire_type.family);
        EXPECT_EQ(after->origin.wire_type.code, before_wire_type.code);
        EXPECT_EQ(after->origin.wire_count, before_wire_count);
        EXPECT_EQ(after->origin.order_in_block, before_order);
        const std::span<const std::byte> after_wire_type_name
            = clean.arena().span(after->origin.wire_type_name);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(
                                  after_wire_type_name.data()),
                              after_wire_type_name.size()),
                  before_wire_type_name);

        MetaStore malformed;
        profile_description_with_native(malformed, 129U, 5U,
                                        EntryFlags::None);
        const auto rejected
            = translate_xmp_profile_metadata(malformed, options, &malformed);
        EXPECT_EQ(rejected.status, Status::NativeConflict);
        const Entry* unchanged = find(malformed, "ifd0", 0x010eU);
        ASSERT_NE(unchanged, nullptr);
        EXPECT_FALSE(any(unchanged->flags, EntryFlags::Dirty));
        EXPECT_EQ(unchanged->origin.wire_count, 5U);

        MetaStore type2;
        profile_description_with_native(type2, 2U, 7U, EntryFlags::None);
        const auto type2_match
            = translate_xmp_profile_metadata(type2, options, &type2);
        ASSERT_EQ(type2_match.status, Status::Ok);
        EXPECT_EQ(type2_match.entries_updated, 1U);
        const Entry* type2_native = find(type2, "ifd0", 0x010eU);
        ASSERT_NE(type2_native, nullptr);
        EXPECT_EQ(type2_native->origin.wire_count, 7U);
        EXPECT_EQ(text(type2, *type2_native), std::string("Scene\0", 6U));
        const auto measured = serialize_exif_tiff(type2, {});
        ASSERT_EQ(measured.status, ExifTiffSerializeStatus::OutputTruncated);
        std::vector<std::byte> emitted(
            static_cast<size_t>(measured.needed));
        ASSERT_EQ(serialize_exif_tiff(type2, emitted).status,
                  ExifTiffSerializeStatus::Ok);
        uint16_t emitted_type = 0U;
        uint32_t emitted_count = 0U;
        ASSERT_TRUE(find_tiff_ifd0_wire(emitted, 0x010eU, &emitted_type,
                                        &emitted_count));
        EXPECT_EQ(emitted_type, 2U);
        EXPECT_EQ(emitted_count, 7U);
        EXPECT_EQ(emitted_count, type2_native->origin.wire_count);

        MetaStore dirty;
        profile_description_with_native(dirty, 129U, 6U, EntryFlags::Dirty);
        const auto unchanged_result
            = translate_xmp_profile_metadata(dirty, options, &dirty);
        ASSERT_EQ(unchanged_result.status, Status::Ok);
        EXPECT_EQ(unchanged_result.groups_unchanged, 1U);
        EXPECT_EQ(unchanged_result.entries_updated, 0U);
        EXPECT_EQ(unchanged_result.entries_added, 0U);
    }

    TEST(MetadataProfileTranslation,
         DescriptiveAsciiType129NeedsUtf8EncodingAndMatchesAllThree)
    {
        MetadataProfileTranslationOptions options;
        options.source_mode = MetadataCaptureTranslationSourceMode::All;

        MetaStore clean;
        profile_three_descriptive_129(clean, EntryFlags::None,
                                      TextEncoding::Utf8);
        const auto promoted
            = translate_xmp_profile_metadata(clean, options, &clean);
        ASSERT_EQ(promoted.status, Status::Ok);
        EXPECT_EQ(promoted.groups_translated, 3U);
        EXPECT_EQ(promoted.entries_updated, 3U);
        constexpr std::array<uint16_t, 3U> kTags = {
            0x010eU, 0x013bU, 0x8298U };
        constexpr std::array<std::string_view, 3U> kValues = {
            "Scene", "Artist", "Owner" };
        for (size_t i = 0U; i < kTags.size(); ++i) {
            const uint16_t tag = kTags[i];
            const std::string_view value = kValues[i];
            const Entry* entry = find(clean, "ifd0", tag);
            ASSERT_NE(entry, nullptr) << tag;
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty)) << tag;
            EXPECT_EQ(entry->value.text_encoding, TextEncoding::Utf8) << tag;
            EXPECT_EQ(text(clean, *entry), value) << tag;
            EXPECT_EQ(entry->origin.wire_type.code, 129U) << tag;
            EXPECT_EQ(entry->origin.wire_count, value.size() + 1U) << tag;
        }

        MetaStore dirty;
        profile_three_descriptive_129(dirty, EntryFlags::Dirty,
                                      TextEncoding::Utf8);
        const auto unchanged
            = translate_xmp_profile_metadata(dirty, options, &dirty);
        ASSERT_EQ(unchanged.status, Status::Ok);
        EXPECT_EQ(unchanged.groups_unchanged, 3U);
        EXPECT_EQ(unchanged.groups_translated, 0U);
        EXPECT_EQ(unchanged.entries_updated, 0U);
        for (size_t i = 0U; i < kTags.size(); ++i) {
            const Entry* entry = find(dirty, "ifd0", kTags[i]);
            ASSERT_NE(entry, nullptr) << kTags[i];
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty)) << kTags[i];
            EXPECT_EQ(entry->value.text_encoding, TextEncoding::Utf8)
                << kTags[i];
            EXPECT_EQ(text(dirty, *entry), kValues[i]) << kTags[i];
            EXPECT_EQ(entry->origin.wire_type.code, 129U) << kTags[i];
            EXPECT_EQ(entry->origin.wire_count, kValues[i].size() + 1U)
                << kTags[i];
        }

        MetaStore ascii_encoding;
        profile_three_descriptive_129(ascii_encoding, EntryFlags::None,
                                      TextEncoding::Ascii);
        const auto rejected = translate_xmp_profile_metadata(
            ascii_encoding, options, &ascii_encoding);
        EXPECT_EQ(rejected.status, Status::NativeConflict);
        for (const uint16_t tag : kTags) {
            const Entry* entry = find(ascii_encoding, "ifd0", tag);
            ASSERT_NE(entry, nullptr) << tag;
            EXPECT_FALSE(any(entry->flags, EntryFlags::Dirty)) << tag;
            EXPECT_EQ(entry->value.text_encoding, TextEncoding::Ascii) << tag;
            EXPECT_EQ(entry->origin.wire_type.code, 129U) << tag;
        }

        MetaStore dirty_ascii_encoding;
        profile_three_descriptive_129(dirty_ascii_encoding,
                                      EntryFlags::Dirty,
                                      TextEncoding::Ascii);
        const auto dirty_rejected = translate_xmp_profile_metadata(
            dirty_ascii_encoding, options, &dirty_ascii_encoding);
        EXPECT_EQ(dirty_rejected.status, Status::NativeConflict);
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        const auto repaired = translate_xmp_profile_metadata(
            dirty_ascii_encoding, options, &dirty_ascii_encoding);
        ASSERT_EQ(repaired.status, Status::Ok);
        EXPECT_EQ(repaired.entries_updated, 3U);
        EXPECT_TRUE(validate_store(dirty_ascii_encoding).ok());
    }

    TEST(MetadataProfileTranslation,
         MalformedUtf8NativeValueHasNoPromotionAuthority)
    {
        MetaStore store;
        xmp(store, kTiff, "ImageDescription", "Scene");
        Entry native;
        native.key = make_exif_tag_key(store.arena(), "ifd0", 0x010eU);
        const std::string malformed("\xc3(", 2U);
        native.value = make_text(store.arena(), malformed, TextEncoding::Utf8);
        native.origin.wire_type = { WireFamily::Tiff, 129U };
        native.origin.wire_count = 3U;
        native.flags = EntryFlags::None;
        (void)store.add_entry(native);
        store.finalize();

        MetadataProfileTranslationOptions options;
        options.source_mode = MetadataCaptureTranslationSourceMode::All;
        const auto conflict
            = translate_xmp_profile_metadata(store, options, &store);
        EXPECT_EQ(conflict.status, Status::NativeConflict);
        const Entry* unchanged = find(store, "ifd0", 0x010eU);
        ASSERT_NE(unchanged, nullptr);
        EXPECT_FALSE(any(unchanged->flags, EntryFlags::Dirty));
        EXPECT_EQ(text(store, *unchanged), malformed);

        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::PreserveExisting;
        const auto preserved
            = translate_xmp_profile_metadata(store, options, &store);
        ASSERT_EQ(preserved.status, Status::Ok);
        EXPECT_EQ(preserved.groups_preserved, 1U);
        EXPECT_EQ(preserved.entries_updated, 0U);
        unchanged = find(store, "ifd0", 0x010eU);
        ASSERT_NE(unchanged, nullptr);
        EXPECT_FALSE(any(unchanged->flags, EntryFlags::Dirty));
        EXPECT_EQ(unchanged->value.text_encoding, TextEncoding::Utf8);
        EXPECT_EQ(unchanged->origin.wire_count, 3U);
        EXPECT_EQ(text(store, *unchanged), malformed);
    }

    TEST(MetadataProfileTranslation,
         PromotesAllFiveWithoutChangingNativeValuesOrProvenance)
    {
        MetaStore store;
        xmp(store, kTiff, "ImageDescription", "Description",
            EntryFlags::None);
        xmp(store, kTiff, "Artist", "Artist", EntryFlags::None);
        xmp(store, kTiff, "Copyright", "Owner", EntryFlags::None);
        xmp(store, kExif, "ColorSpace", "1", EntryFlags::None);
        xmp(store, kExif, "RelatedSoundFile", "SOUND.WAV",
            EntryFlags::None);
        profile_text_native(store, "ifd0", 0x010eU, "Description", 129U,
                            12U);
        profile_text_native(store, "ifd0", 0x013bU, "Artist", 129U, 7U);
        profile_text_native(store, "ifd0", 0x8298U, "Owner", 129U, 6U);
        Entry color_space;
        color_space.key
            = make_exif_tag_key(store.arena(), "exififd", 0xa001U);
        color_space.value = make_u16(1U);
        color_space.origin.wire_type = { WireFamily::Tiff, 3U };
        color_space.origin.wire_count = 1U;
        color_space.origin.wire_type_name
            = store.arena().append_string("USHORT");
        color_space.origin.order_in_block = 19U;
        color_space.flags = EntryFlags::ValueBigEndian;
        (void)store.add_entry(color_space);
        profile_text_native(store, "exififd", 0xa004U, "SOUND.WAV", 2U,
                            10U);
        store.finalize();

        constexpr std::array<std::string_view, 5U> kIfds = {
            "ifd0", "ifd0", "ifd0", "exififd", "exififd" };
        constexpr std::array<uint16_t, 5U> kTags = {
            0x010eU, 0x013bU, 0x8298U, 0xa001U, 0xa004U };
        std::array<Entry, 5U> before {};
        std::array<std::string, 5U> before_text {};
        std::array<std::string, 5U> before_type_name {};
        for (size_t i = 0U; i < before.size(); ++i) {
            const Entry* entry = find(store, kIfds[i], kTags[i]);
            ASSERT_NE(entry, nullptr) << i;
            before[i] = *entry;
            if (entry->value.kind == MetaValueKind::Text)
                before_text[i] = text(store, *entry);
            const std::span<const std::byte> type_name
                = store.arena().span(entry->origin.wire_type_name);
            before_type_name[i].assign(
                reinterpret_cast<const char*>(type_name.data()),
                type_name.size());
        }

        MetadataProfileTranslationOptions options;
        options.source_mode = MetadataCaptureTranslationSourceMode::All;
        const auto result = translate_xmp_profile_metadata(store, options,
                                                            &store);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.entries_updated, 5U);
        EXPECT_EQ(result.entries_added, 0U);
        EXPECT_EQ(result.groups_translated, 5U);
        for (size_t i = 0U; i < before.size(); ++i) {
            const Entry* entry = find(store, kIfds[i], kTags[i]);
            ASSERT_NE(entry, nullptr) << i;
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty)) << i;
            EXPECT_FALSE(any(entry->flags, EntryFlags::Deleted)) << i;
            EXPECT_EQ(entry->value.kind, before[i].value.kind) << i;
            EXPECT_EQ(entry->value.elem_type, before[i].value.elem_type) << i;
            EXPECT_EQ(entry->value.count, before[i].value.count) << i;
            EXPECT_EQ(entry->value.text_encoding,
                      before[i].value.text_encoding) << i;
            if (entry->value.kind == MetaValueKind::Text)
                EXPECT_EQ(text(store, *entry), before_text[i]) << i;
            else
                EXPECT_EQ(entry->value.data.u64, before[i].value.data.u64)
                    << i;
            EXPECT_EQ(entry->origin.wire_type.family,
                      before[i].origin.wire_type.family) << i;
            EXPECT_EQ(entry->origin.wire_type.code,
                      before[i].origin.wire_type.code) << i;
            EXPECT_EQ(entry->origin.wire_count,
                      before[i].origin.wire_count) << i;
            EXPECT_EQ(entry->origin.order_in_block,
                      before[i].origin.order_in_block) << i;
            EXPECT_EQ(any(entry->flags, EntryFlags::ValueBigEndian),
                      any(before[i].flags, EntryFlags::ValueBigEndian)) << i;
            const std::span<const std::byte> type_name
                = store.arena().span(entry->origin.wire_type_name);
            EXPECT_EQ(std::string(reinterpret_cast<const char*>(
                                      type_name.data()),
                                  type_name.size()),
                      before_type_name[i]) << i;
        }
    }

    TEST(MetadataProfileTranslation,
         ReplaceExistingWritesOrReusesTypedDeletionIntents)
    {
        constexpr std::array<std::string_view, 5U> kIfds = {
            "ifd0", "ifd0", "ifd0", "exififd", "exififd" };
        constexpr std::array<uint16_t, 5U> kTags = {
            0x010eU, 0x013bU, 0x8298U, 0xa001U, 0xa004U };
        constexpr std::array<uint16_t, 5U> kWireTypes = {
            2U, 2U, 2U, 3U, 2U };

        MetadataProfileTranslationOptions options;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;

        MetaStore absent;
        profile_deleted_sources(absent);
        absent.finalize();
        const auto created
            = translate_xmp_profile_metadata(absent, options, &absent);
        ASSERT_EQ(created.status, Status::Ok);
        EXPECT_EQ(created.entries_added, 5U);
        EXPECT_EQ(created.entries_updated, 0U);
        EXPECT_EQ(created.groups_translated, 5U);
        for (size_t i = 0U; i < kTags.size(); ++i) {
            const Entry* marker = find_deleted(absent, kIfds[i], kTags[i]);
            ASSERT_NE(marker, nullptr) << i;
            EXPECT_TRUE(any(marker->flags, EntryFlags::Dirty)) << i;
            EXPECT_EQ(marker->origin.wire_type.family, WireFamily::Tiff) << i;
            EXPECT_EQ(marker->origin.wire_type.code, kWireTypes[i]) << i;
            EXPECT_EQ(marker->origin.wire_count, 1U) << i;
            if (i == 3U) {
                EXPECT_EQ(marker->value.kind, MetaValueKind::Scalar);
                EXPECT_EQ(marker->value.elem_type, MetaElementType::U16);
                EXPECT_EQ(marker->value.data.u64, 1U);
                EXPECT_EQ(marker->value.count, 1U);
            } else {
                EXPECT_EQ(marker->value.kind, MetaValueKind::Text) << i;
                EXPECT_EQ(marker->value.count, 0U) << i;
                EXPECT_EQ(marker->value.data.span.size, 0U) << i;
            }
        }

        MetaStore reused;
        profile_deleted_sources(reused);
        profile_native_tombstone(reused, "ifd0", 0x010eU, 129U,
                                 EntryFlags::None);
        profile_native_tombstone(reused, "ifd0", 0x013bU, 2U,
                                 EntryFlags::Dirty);
        reused.finalize();
        const Entry* clean_before
            = find_deleted(reused, "ifd0", 0x010eU);
        ASSERT_NE(clean_before, nullptr);
        const uint32_t old_clean_wire_count
            = clean_before->origin.wire_count;
        const std::string old_clean_text = text(reused, *clean_before);
        const Entry* dirty_before = find_deleted(reused, "ifd0", 0x013bU);
        ASSERT_NE(dirty_before, nullptr);
        const uint32_t old_dirty_wire_count
            = dirty_before->origin.wire_count;
        const std::string old_dirty_text = text(reused, *dirty_before);

        const auto reconciled
            = translate_xmp_profile_metadata(reused, options, &reused);
        ASSERT_EQ(reconciled.status, Status::Ok);
        EXPECT_EQ(reconciled.entries_added, 3U);
        EXPECT_EQ(reconciled.entries_updated, 1U);
        EXPECT_EQ(reconciled.groups_translated, 4U);
        EXPECT_EQ(reconciled.groups_unchanged, 1U);
        const Entry* promoted = find_deleted(reused, "ifd0", 0x010eU);
        ASSERT_NE(promoted, nullptr);
        EXPECT_TRUE(any(promoted->flags, EntryFlags::Dirty));
        EXPECT_EQ(promoted->origin.wire_type.code, 129U);
        EXPECT_EQ(promoted->origin.wire_count, old_clean_wire_count);
        EXPECT_EQ(text(reused, *promoted), old_clean_text);
        const Entry* existing = find_deleted(reused, "ifd0", 0x013bU);
        ASSERT_NE(existing, nullptr);
        EXPECT_TRUE(any(existing->flags, EntryFlags::Dirty));
        EXPECT_EQ(existing->origin.wire_count, old_dirty_wire_count);
        EXPECT_EQ(text(reused, *existing), old_dirty_text);
        for (size_t i = 2U; i < kTags.size(); ++i)
            ASSERT_NE(find_deleted(reused, kIfds[i], kTags[i]), nullptr) << i;

        const auto repeated
            = translate_xmp_profile_metadata(reused, options, &reused);
        ASSERT_EQ(repeated.status, Status::Ok);
        EXPECT_EQ(repeated.groups_unchanged, 5U);
        EXPECT_EQ(repeated.entries_added, 0U);
        EXPECT_EQ(repeated.entries_updated, 0U);
    }

    TEST(MetadataProfileTranslation,
         MasksDuplicatesAndLimitsRemainTransactional)
    {
        MetaStore masked;
        xmp(masked, kTiff, "Artist", "Photographer", EntryFlags::None);
        masked.finalize();
        const auto ignored
            = translate_xmp_profile_metadata(masked, {}, &masked);
        ASSERT_EQ(ignored.status, Status::Ok);
        EXPECT_EQ(ignored.source_properties, 0U);
        EXPECT_EQ(ignored.entries_added, 0U);
        EXPECT_EQ(masked.entries().size(), 1U);
        EXPECT_EQ(find(masked, "ifd0", 0x013bU), nullptr);
        MetadataProfileTranslationOptions all_sources;
        all_sources.source_mode = MetadataCaptureTranslationSourceMode::All;
        const auto selected
            = translate_xmp_profile_metadata(masked, all_sources, &masked);
        ASSERT_EQ(selected.status, Status::Ok);
        EXPECT_EQ(selected.source_properties, 1U);
        EXPECT_EQ(selected.entries_added, 1U);

        MetaStore duplicate;
        xmp(duplicate, kTiff, "Artist", "First");
        xmp(duplicate, kTiff, "Artist", "Second");
        duplicate.finalize();
        const auto ambiguous
            = translate_xmp_profile_metadata(duplicate, {}, &duplicate);
        EXPECT_EQ(ambiguous.status, Status::AmbiguousSource);
        EXPECT_EQ(duplicate.entries().size(), 2U);
        EXPECT_EQ(find(duplicate, "ifd0", 0x013bU), nullptr);

        MetaStore limited;
        profile_deleted_sources(limited);
        limited.finalize();
        MetadataProfileTranslationOptions replace;
        replace.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        replace.max_added_entries = 4U;
        const auto entry_limited
            = translate_xmp_profile_metadata(limited, replace, &limited);
        EXPECT_EQ(entry_limited.status, Status::EntryLimitExceeded);
        EXPECT_EQ(limited.entries().size(), 5U);
        constexpr std::array<uint16_t, 3U> kIfd0Tags = {
            0x010eU, 0x013bU, 0x8298U };
        for (uint16_t tag : kIfd0Tags)
            EXPECT_EQ(find_deleted(limited, "ifd0", tag), nullptr);
        EXPECT_EQ(find_deleted(limited, "exififd", 0xa001U), nullptr);
        EXPECT_EQ(find_deleted(limited, "exififd", 0xa004U), nullptr);

        replace.max_added_entries = 5U;
        replace.max_operations = 4U;
        const auto operation_limited
            = translate_xmp_profile_metadata(limited, replace, &limited);
        EXPECT_EQ(operation_limited.status, Status::OperationLimitExceeded);
        EXPECT_EQ(limited.entries().size(), 5U);
        EXPECT_EQ(find_deleted(limited, "ifd0", 0x010eU), nullptr);
        EXPECT_EQ(find_deleted(limited, "ifd0", 0x013bU), nullptr);
        EXPECT_EQ(find_deleted(limited, "ifd0", 0x8298U), nullptr);
        EXPECT_EQ(find_deleted(limited, "exififd", 0xa001U), nullptr);
        EXPECT_EQ(find_deleted(limited, "exififd", 0xa004U), nullptr);
    }

    TEST(MetadataProfileTranslation, FullBatchAndPortableOutput)
    {
        MetaStore source;
        xmp(source, kTiff, "ImageDescription", "A scene");
        xmp(source, kTiff, "Artist", "A. Photographer");
        xmp(source, kTiff, "Copyright", "Copyright 2026");
        xmp(source, kExif, "ColorSpace", "1");
        xmp(source, kExif, "RelatedSoundFile", "SOUND.WAV");
        source.finalize();

        const auto result = translate_xmp_profile_metadata(source, {}, &source);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.source_properties, 5U);
        EXPECT_EQ(result.entries_added, 5U);
        EXPECT_EQ(text(source, *find(source, "ifd0", 0x010eU)), "A scene");
        EXPECT_EQ(text(source, *find(source, "ifd0", 0x013bU)),
                  "A. Photographer");
        EXPECT_EQ(text(source, *find(source, "ifd0", 0x8298U)),
                  "Copyright 2026");
        EXPECT_EQ(find(source, "exififd", 0xa001U)->value.data.u64, 1U);
        EXPECT_EQ(text(source, *find(source, "exififd", 0xa004U)),
                  "SOUND.WAV");
        EXPECT_EQ(find(source, "exififd", 0xa004U)->origin.wire_count, 10U);
        EXPECT_TRUE(validate_store(source).ok());

        std::vector<std::byte> bytes(256U * 1024U);
        const auto dumped = dump_xmp_portable(source, bytes, {});
        ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
        const std::string packet(reinterpret_cast<const char*>(bytes.data()),
                                 static_cast<size_t>(dumped.written));
        EXPECT_NE(packet.find("<tiff:ImageDescription>A scene"),
                  std::string::npos);
        EXPECT_NE(packet.find("<tiff:Artist>A. Photographer"),
                  std::string::npos);
        EXPECT_NE(packet.find("<tiff:Copyright>Copyright 2026"),
                  std::string::npos);
        EXPECT_NE(packet.find("<exif:ColorSpace>sRGB</exif:ColorSpace>"),
                  std::string::npos);
    }

    TEST(MetadataProfileTranslation,
         NonAsciiCopyrightUsesUtf8AndRoundTripsThroughNativeExif)
    {
        const std::string value = "Owner 日本語";
        MetaStore store;
        xmp(store, kTiff, "Copyright", value);
        store.finalize();

        const auto translated
            = translate_xmp_profile_metadata(store, {}, &store);
        ASSERT_EQ(translated.status, Status::Ok);
        ASSERT_EQ(translated.entries_added, 1U);
        const Entry* native = find(store, "ifd0", 0x8298U);
        ASSERT_NE(native, nullptr);
        EXPECT_EQ(native->value.kind, MetaValueKind::Text);
        EXPECT_EQ(native->value.text_encoding, TextEncoding::Utf8);
        EXPECT_EQ(native->origin.wire_type.code, 129U);
        EXPECT_EQ(native->origin.wire_count, native->value.count + 1U);
        ASSERT_TRUE(validate_store(store).ok());

        const auto measured = serialize_exif_tiff(store, {});
        ASSERT_EQ(measured.status, ExifTiffSerializeStatus::OutputTruncated);
        std::vector<std::byte> wire(static_cast<size_t>(measured.needed));
        ASSERT_EQ(serialize_exif_tiff(store, wire).status,
                  ExifTiffSerializeStatus::Ok);
        MetaStore decoded;
        ASSERT_EQ(decode_exif_tiff(wire, decoded, {}, {}).status,
                  ExifDecodeStatus::Ok);
        decoded.finalize();
        ASSERT_TRUE(validate_store(decoded).ok());
        const Entry* decoded_native = find(decoded, "ifd0", 0x8298U);
        ASSERT_NE(decoded_native, nullptr);
        EXPECT_EQ(decoded_native->value.kind, MetaValueKind::Text);
        EXPECT_EQ(decoded_native->value.text_encoding, TextEncoding::Utf8);
        EXPECT_EQ(decoded_native->origin.wire_type.code, 129U);
        EXPECT_EQ(decoded_native->origin.wire_count,
                  decoded_native->value.count + 1U);
        EXPECT_EQ(text(decoded, *decoded_native), value);

        std::vector<std::byte> packet(65536U);
        const auto dumped = dump_xmp_portable(decoded, packet, {});
        ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
        const std::span<const std::byte> packet_bytes(
            packet.data(), static_cast<size_t>(dumped.written));
        const std::string_view packet_text(
            reinterpret_cast<const char*>(packet_bytes.data()),
            packet_bytes.size());
        EXPECT_NE(packet_text.find("<tiff:Copyright>"),
                  std::string_view::npos);
        EXPECT_NE(packet_text.find(value), std::string_view::npos);
        MetaStore portable;
        ASSERT_EQ(decode_xmp_packet(packet_bytes, portable).status,
                  XmpDecodeStatus::Ok);
        portable.finalize();
        const Entry* round_tripped
            = find_xmp(portable, kTiff, "Copyright");
        ASSERT_NE(round_tripped, nullptr);
        EXPECT_EQ(text(portable, *round_tripped), value);
    }

    TEST(MetadataProfileTranslation, BoundsConflictsAndAtomicRemoval)
    {
        MetaStore unsupported;
        xmp(unsupported, kTiff, "Artist[1]", "A. Photographer");
        unsupported.finalize();
        EXPECT_EQ(translate_xmp_profile_metadata(unsupported, {}, &unsupported)
                      .status,
                  Status::UnsupportedSourceShape);

        MetaStore invalid;
        xmp(invalid, kExif, "ColorSpace", "2");
        invalid.finalize();
        EXPECT_EQ(translate_xmp_profile_metadata(invalid, {}, &invalid).status,
                  Status::ValueOutOfRange);
        EXPECT_EQ(invalid.find_all(
                      make_exif_tag_key_view("exififd", 0xa001U))
                      .size(),
                  0U);

        MetaStore sound;
        xmp(sound, kExif, "RelatedSoundFile", "../SOUND.WAV");
        sound.finalize();
        EXPECT_EQ(translate_xmp_profile_metadata(sound, {}, &sound).status,
                  Status::InvalidSourceValue);

        MetaStore legacy;
        Entry old;
        old.key = make_exif_tag_key(legacy.arena(), "ifd0", 0x8298U);
        const std::string two_part("Owner\0Editor", 12U);
        old.value = make_bytes(
            legacy.arena(), std::as_bytes(std::span(two_part.data(),
                                                    two_part.size())));
        old.origin.wire_type = { WireFamily::Tiff, 2U };
        old.origin.wire_count = 13U;
        (void)legacy.add_entry(old);
        xmp(legacy, kTiff, "Copyright", "New owner");
        legacy.finalize();
        const std::string before_legacy
            = text(legacy, *find(legacy, "ifd0", 0x8298U));
        const auto legacy_conflict
            = translate_xmp_profile_metadata(legacy, {}, &legacy);
        EXPECT_EQ(legacy_conflict.status, Status::NativeConflict);
        EXPECT_EQ(text(legacy, *find(legacy, "ifd0", 0x8298U)),
                  before_legacy);
        MetadataProfileTranslationOptions preserve;
        preserve.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::PreserveExisting;
        const auto legacy_preserved
            = translate_xmp_profile_metadata(legacy, preserve, &legacy);
        ASSERT_EQ(legacy_preserved.status, Status::Ok);
        EXPECT_EQ(legacy_preserved.groups_preserved, 1U);
        EXPECT_EQ(legacy_preserved.entries_updated, 0U);
        EXPECT_EQ(text(legacy, *find(legacy, "ifd0", 0x8298U)),
                  before_legacy);
        MetadataProfileTranslationOptions replace;
        replace.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        const auto legacy_replaced
            = translate_xmp_profile_metadata(legacy, replace, &legacy);
        ASSERT_EQ(legacy_replaced.status, Status::Ok);
        EXPECT_EQ(legacy_replaced.entries_updated, 1U);
        EXPECT_EQ(legacy_replaced.entries_added, 0U);
        EXPECT_EQ(text(legacy, *find(legacy, "ifd0", 0x8298U)), "New owner");

        MetaStore bytes_native;
        Entry bytes_entry;
        bytes_entry.key
            = make_exif_tag_key(bytes_native.arena(), "ifd0", 0x8298U);
        const std::string new_owner = "New owner";
        bytes_entry.value = make_bytes(
            bytes_native.arena(),
            std::as_bytes(std::span(new_owner.data(), new_owner.size())));
        bytes_entry.origin.wire_type = { WireFamily::Tiff, 2U };
        bytes_entry.origin.wire_count = 10U;
        (void)bytes_native.add_entry(bytes_entry);
        xmp(bytes_native, kTiff, "Copyright", new_owner);
        bytes_native.finalize();
        const Entry* before_bytes_entry
            = find(bytes_native, "ifd0", 0x8298U);
        ASSERT_NE(before_bytes_entry, nullptr);
        const std::string before_bytes = text(bytes_native, *before_bytes_entry);
        const uint16_t before_bytes_wire_type
            = before_bytes_entry->origin.wire_type.code;
        const uint32_t before_bytes_wire_count
            = before_bytes_entry->origin.wire_count;
        const auto bytes_conflict
            = translate_xmp_profile_metadata(bytes_native, {}, &bytes_native);
        EXPECT_EQ(bytes_conflict.status, Status::NativeConflict);
        EXPECT_EQ(text(bytes_native, *find(bytes_native, "ifd0", 0x8298U)),
                  before_bytes);
        const auto bytes_preserved
            = translate_xmp_profile_metadata(bytes_native, preserve,
                                             &bytes_native);
        ASSERT_EQ(bytes_preserved.status, Status::Ok);
        EXPECT_EQ(bytes_preserved.groups_preserved, 1U);
        EXPECT_EQ(bytes_preserved.entries_updated, 0U);
        const Entry* preserved_bytes
            = find(bytes_native, "ifd0", 0x8298U);
        ASSERT_NE(preserved_bytes, nullptr);
        EXPECT_EQ(preserved_bytes->value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(text(bytes_native, *preserved_bytes), before_bytes);
        EXPECT_EQ(preserved_bytes->origin.wire_type.code,
                  before_bytes_wire_type);
        EXPECT_EQ(preserved_bytes->origin.wire_count,
                  before_bytes_wire_count);
        const auto bytes_replaced
            = translate_xmp_profile_metadata(bytes_native, replace,
                                             &bytes_native);
        ASSERT_EQ(bytes_replaced.status, Status::Ok);
        EXPECT_EQ(bytes_replaced.entries_updated, 1U);
        const Entry* canonical
            = find(bytes_native, "ifd0", 0x8298U);
        ASSERT_NE(canonical, nullptr);
        EXPECT_EQ(canonical->value.kind, MetaValueKind::Text);
        EXPECT_EQ(canonical->origin.wire_type.code, 2U);
        EXPECT_EQ(canonical->origin.wire_count, 10U);

        MetaStore remove;
        xmp(remove, kTiff, "Artist", "", EntryFlags::Dirty
                                             | EntryFlags::Deleted);
        Entry existing;
        existing.key = make_exif_tag_key(remove.arena(), "ifd0", 0x013bU);
        existing.value = make_text(remove.arena(), "Old", TextEncoding::Ascii);
        existing.origin.wire_type = { WireFamily::Tiff, 2U };
        existing.origin.wire_count = 4U;
        (void)remove.add_entry(existing);
        remove.finalize();
        replace.artist_to_exif = true;
        EXPECT_EQ(translate_xmp_profile_metadata(remove, replace, &remove)
                      .status,
                  Status::Ok);
        EXPECT_EQ(remove.find_all(make_exif_tag_key_view("ifd0", 0x013bU))
                      .size(),
                  0U);
    }

}  // namespace
}  // namespace openmeta
