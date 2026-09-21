// SPDX-License-Identifier: Apache-2.0

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

    constexpr std::string_view kExifEx = "http://cipa.jp/exif/1.0/";
    using Status = MetadataCaptureTranslationStatus;

    void xmp(MetaStore& store, std::string_view path, std::string_view text,
             EntryFlags flags = EntryFlags::Dirty)
    {
        Entry entry;
        entry.key = make_xmp_property_key(store.arena(), kExifEx, path);
        entry.value = make_text(store.arena(), text, TextEncoding::Utf8);
        entry.flags = flags;
        (void)store.add_entry(entry);
    }

    void native(MetaStore& store, std::span<const uint16_t> words,
                EntryFlags flags = EntryFlags::None)
    {
        std::array<std::byte, 4U * kMetadataLearningOptOutInTranslationMaxSets
                                   + 2U>
            bytes {};
        for (size_t i = 0U; i < words.size(); ++i) {
            bytes[i * 2U] = static_cast<std::byte>(words[i] & 0xffU);
            bytes[i * 2U + 1U] = static_cast<std::byte>(words[i] >> 8U);
        }
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), "exififd", 0x9287U);
        entry.value = make_bytes(store.arena(),
                                 std::span<const std::byte>(bytes.data(),
                                                            words.size() * 2U));
        entry.origin.wire_type = { WireFamily::Tiff, 7U };
        entry.origin.wire_count = entry.value.count;
        entry.flags = flags;
        (void)store.add_entry(entry);
    }

    const Entry* find_native(const MetaStore& store)
    {
        const auto ids = store.find_all(
            make_exif_tag_key_view("exififd", 0x9287U));
        return ids.size() == 1U ? &store.entry(ids.front()) : nullptr;
    }

    std::string portable(const MetaStore& store)
    {
        std::vector<std::byte> bytes(256U * 1024U);
        const XmpDumpResult result = dump_xmp_portable(store, bytes, {});
        EXPECT_EQ(result.status, XmpDumpStatus::Ok);
        return { reinterpret_cast<const char*>(bytes.data()),
                 static_cast<size_t>(result.written) };
    }

    TEST(MetadataLearningOptOutInTranslation,
         StrictBatchPortableAndRoundTrip)
    {
        MetaStore source;
        xmp(source, "LearningOptOutIn/NumberOfSets", "3");
        xmp(source, "LearningOptOutIn/Values[1]", "0");
        xmp(source, "LearningOptOutIn/Values[2]", "2");
        xmp(source, "LearningOptOutIn/Values[3]", "1");
        xmp(source, "LearningOptOutIn/Values[4]", "1");
        xmp(source, "LearningOptOutIn/Values[5]", "4");
        xmp(source, "LearningOptOutIn/Values[6]", "0");
        source.finalize();

        MetadataLearningOptOutInTranslationOptions options;
        options.exif_version = 310U;
        const auto result = translate_xmp_learning_opt_out_in_metadata(
            source, options, &source);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.source_properties, 7U);
        EXPECT_EQ(result.entries_added, 1U);
        const Entry* entry = find_native(source);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(entry->value.count, 14U);
        EXPECT_EQ(entry->origin.wire_type.code, 7U);
        EXPECT_EQ(entry->origin.wire_count, 14U);
        EXPECT_TRUE(validate_store(source).ok());

        const std::string packet = portable(source);
        EXPECT_NE(packet.find("xmlns:exifEX=\"http://cipa.jp/exif/1.0/\""),
                  std::string::npos);
        EXPECT_NE(packet.find(
                      "<exifEX:LearningOptOutIn rdf:parseType=\"Resource\">"),
                  std::string::npos);
        EXPECT_NE(packet.find("<exifEX:NumberOfSets>3</exifEX:NumberOfSets>"),
                  std::string::npos);
        EXPECT_NE(packet.find("<rdf:li>0</rdf:li>"), std::string::npos);
        EXPECT_NE(packet.find("<rdf:li>4</rdf:li>"), std::string::npos);

        MetaStore decoded;
        ASSERT_EQ(decode_xmp_packet(
                      std::as_bytes(std::span(packet.data(), packet.size())),
                      decoded)
                      .status,
                  XmpDecodeStatus::Ok);
        decoded.finalize();
        MetadataLearningOptOutInTranslationOptions round_trip;
        round_trip.source_mode = MetadataCaptureTranslationSourceMode::All;
        round_trip.exif_version = 300U;
        const auto restored = translate_xmp_learning_opt_out_in_metadata(
            decoded, round_trip, &decoded);
        ASSERT_EQ(restored.status, Status::Ok);
        EXPECT_NE(find_native(decoded), nullptr);
        EXPECT_TRUE(validate_store(decoded).ok());
    }

    TEST(MetadataLearningOptOutInTranslation, ValidationAndAtomicConflicts)
    {
        for (const std::array<std::string_view, 6U> values : {
                 std::array<std::string_view, 6U> { "1", "2", "1", "1", "4",
                                                   "0" },
                 std::array<std::string_view, 6U> { "0", "2", "1", "1", "1",
                                                   "0" },
                 std::array<std::string_view, 6U> { "0", "2", "1", "3", "4",
                                                   "0" },
             }) {
            MetaStore malformed;
            xmp(malformed, "LearningOptOutIn/NumberOfSets", "3");
            for (size_t i = 0U; i < values.size(); ++i) {
                const std::string path = "LearningOptOutIn/Values["
                                         + std::to_string(i + 1U) + "]";
                xmp(malformed, path, values[i]);
            }
            malformed.finalize();
            MetadataLearningOptOutInTranslationOptions options;
            options.exif_version = 310U;
            EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                           malformed, options, &malformed)
                           .status,
                       Status::ValueOutOfRange);
            EXPECT_EQ(malformed.find_all(
                          make_exif_tag_key_view("exififd", 0x9287U))
                          .size(),
                      0U);
        }

        MetaStore conflict;
        const std::array<uint16_t, 7U> old_words { 3U, 0U, 1U, 1U, 2U, 4U,
                                                   0U };
        native(conflict, old_words);
        xmp(conflict, "LearningOptOutIn/NumberOfSets", "3");
        xmp(conflict, "LearningOptOutIn/Values[1]", "0");
        xmp(conflict, "LearningOptOutIn/Values[2]", "2");
        xmp(conflict, "LearningOptOutIn/Values[3]", "1");
        xmp(conflict, "LearningOptOutIn/Values[4]", "1");
        xmp(conflict, "LearningOptOutIn/Values[5]", "4");
        xmp(conflict, "LearningOptOutIn/Values[6]", "0");
        conflict.finalize();
        MetadataLearningOptOutInTranslationOptions options;
        options.exif_version = 310U;
        EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                       conflict, options, &conflict)
                       .status,
                   Status::NativeConflict);
        EXPECT_EQ(find_native(conflict)->value.count, 14U);

        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        ASSERT_EQ(translate_xmp_learning_opt_out_in_metadata(
                       conflict, options, &conflict)
                       .status,
                   Status::Ok);
        EXPECT_EQ(find_native(conflict)->value.count, 14U);
    }

    TEST(MetadataLearningOptOutInTranslation, DeletionAndBigEndianValidation)
    {
        MetaStore remove;
        const std::array<uint16_t, 3U> old_words { 1U, 0U, 0U };
        native(remove, old_words);
        xmp(remove, "LearningOptOutIn/NumberOfSets", "", EntryFlags::Dirty
                                                            | EntryFlags::Deleted);
        remove.finalize();
        MetadataLearningOptOutInTranslationOptions options;
        options.exif_version = 310U;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        ASSERT_EQ(translate_xmp_learning_opt_out_in_metadata(
                       remove, options, &remove)
                       .status,
                   Status::Ok);
        EXPECT_EQ(remove.find_all(
                      make_exif_tag_key_view("exififd", 0x9287U))
                      .size(),
                  0U);

        MetaStore big_endian;
        Entry entry;
        entry.key = make_exif_tag_key(big_endian.arena(), "exififd", 0x9287U);
        const std::array<std::byte, 6U> bytes { std::byte { 0 }, std::byte { 1 },
                                                std::byte { 0 }, std::byte { 0 },
                                                std::byte { 0 }, std::byte { 0 } };
        entry.value = make_bytes(big_endian.arena(), bytes);
        entry.origin.wire_type = { WireFamily::Tiff, 7U };
        entry.origin.wire_count = 6U;
        entry.flags = EntryFlags::ValueBigEndian;
        (void)big_endian.add_entry(entry);
        big_endian.finalize();
        EXPECT_TRUE(validate_store(big_endian).ok());
    }

}  // namespace
}  // namespace openmeta
