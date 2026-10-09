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
                EntryFlags flags = EntryFlags::None, bool big_endian = false,
                uint16_t wire_code = 7U, uint32_t wire_count = 0U)
    {
        std::array<std::byte, 4U * kMetadataLearningOptOutInTranslationMaxSets
                                   + 2U>
            bytes {};
        for (size_t i = 0U; i < words.size(); ++i) {
            if (big_endian) {
                bytes[i * 2U] = static_cast<std::byte>(words[i] >> 8U);
                bytes[i * 2U + 1U]
                    = static_cast<std::byte>(words[i] & 0xffU);
            } else {
                bytes[i * 2U] = static_cast<std::byte>(words[i] & 0xffU);
                bytes[i * 2U + 1U] = static_cast<std::byte>(words[i] >> 8U);
            }
        }
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), "exififd", 0x9287U);
        entry.value = make_bytes(store.arena(),
                                 std::span<const std::byte>(bytes.data(),
                                                            words.size() * 2U));
        entry.origin.wire_type = { WireFamily::Tiff, wire_code };
        entry.origin.wire_count
            = wire_count == 0U ? entry.value.count : wire_count;
        entry.origin.order_in_block = 17U;
        constexpr std::string_view type_name = "UNDEFINED";
        entry.origin.wire_type_name = store.arena().append(std::as_bytes(
            std::span(type_name.data(), type_name.size())));
        if (big_endian)
            flags |= EntryFlags::ValueBigEndian;
        entry.flags = flags;
        (void)store.add_entry(entry);
    }

    void learning_source(MetaStore& store,
                         EntryFlags flags = EntryFlags::Dirty)
    {
        xmp(store, "LearningOptOutIn/NumberOfSets", "1", flags);
        xmp(store, "LearningOptOutIn/Values[1]", "0", flags);
        xmp(store, "LearningOptOutIn/Values[2]", "2", flags);
    }

    void learning_deletion_source(MetaStore& store)
    {
        xmp(store, "LearningOptOutIn/NumberOfSets", "",
            EntryFlags::Dirty | EntryFlags::Deleted);
    }

    EntryId add_learning_marker(MetaStore& store, EntryFlags flags)
    {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), "exififd", 0x9287U);
        entry.value = make_bytes(store.arena(), std::span<const std::byte> {});
        entry.origin.wire_type = { WireFamily::Tiff, 7U };
        entry.origin.wire_count = 0U;
        entry.flags = flags;
        return store.add_entry(entry);
    }

    std::string_view ifd_text(const MetaStore& store, ByteSpan span)
    {
        const auto raw = store.arena().span(span);
        return { reinterpret_cast<const char*>(raw.data()), raw.size() };
    }

    const Entry* find_any_native(const MetaStore& store, bool deleted)
    {
        for (const Entry& entry : store.entries()) {
            if (entry.key.kind == MetaKeyKind::ExifTag
                && ifd_text(store, entry.key.data.exif_tag.ifd) == "exififd"
                && entry.key.data.exif_tag.tag == 0x9287U
                && any(entry.flags, EntryFlags::Deleted) == deleted) {
                return &entry;
            }
        }
        return nullptr;
    }

    size_t native_count(const MetaStore& store, bool deleted)
    {
        size_t count = 0U;
        for (const Entry& entry : store.entries()) {
            if (entry.key.kind == MetaKeyKind::ExifTag
                && ifd_text(store, entry.key.data.exif_tag.ifd) == "exififd"
                && entry.key.data.exif_tag.tag == 0x9287U
                && any(entry.flags, EntryFlags::Deleted) == deleted) {
                ++count;
            }
        }
        return count;
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
            = MetadataCaptureTranslationConflictPolicy::PreserveExisting;
        ASSERT_EQ(translate_xmp_learning_opt_out_in_metadata(
                       remove, options, &remove)
                       .status,
                   Status::Ok);
        ASSERT_NE(find_native(remove), nullptr);
        EXPECT_FALSE(any(find_native(remove)->flags, EntryFlags::Deleted));
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
        EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                      remove, options, &remove)
                      .status,
                  Status::NativeConflict);
        ASSERT_NE(find_native(remove), nullptr);
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
        const Entry* removed = find_any_native(remove, true);
        ASSERT_NE(removed, nullptr);
        EXPECT_TRUE(any(removed->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(removed->flags, EntryFlags::Deleted));

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

    TEST(MetadataLearningOptOutInTranslation,
         ExactSingletonPromotionRetainsLittleAndBigEndianBytes)
    {
        const std::array<uint16_t, 3U> words { 1U, 0U, 2U };
        for (const bool big_endian : std::array<bool, 2U> { false, true }) {
            MetaStore source;
            native(source, words, EntryFlags::None, big_endian);
            learning_source(source);
            source.finalize();

            std::array<std::byte, 6U> raw_before {};
            const Entry* before = find_native(source);
            ASSERT_NE(before, nullptr);
            const auto before_bytes
                = source.arena().span(before->value.data.span);
            for (size_t i = 0U; i < raw_before.size(); ++i)
                raw_before[i] = before_bytes[i];

            MetadataLearningOptOutInTranslationOptions options;
            options.exif_version = 310U;
            const auto promoted = translate_xmp_learning_opt_out_in_metadata(
                source, options, &source);
            ASSERT_EQ(promoted.status, Status::Ok);
            EXPECT_EQ(promoted.groups_translated, 1U);
            EXPECT_EQ(promoted.entries_updated, 1U);
            EXPECT_EQ(promoted.entries_added, 0U);
            EXPECT_EQ(source.find_all(
                          make_exif_tag_key_view("exififd", 0x9000U))
                          .size(),
                      0U);
            const Entry* entry = find_native(source);
            ASSERT_NE(entry, nullptr);
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty));
            EXPECT_EQ(any(entry->flags, EntryFlags::ValueBigEndian),
                      big_endian);
            EXPECT_EQ(entry->origin.wire_type.code, 7U);
            EXPECT_EQ(entry->origin.wire_count, 6U);
            EXPECT_EQ(entry->origin.order_in_block, 17U);
            EXPECT_EQ(ifd_text(source, entry->origin.wire_type_name),
                      "UNDEFINED");
            const auto raw_after = source.arena().span(entry->value.data.span);
            for (size_t i = 0U; i < raw_before.size(); ++i)
                EXPECT_EQ(raw_after[i], raw_before[i]);

            const auto unchanged = translate_xmp_learning_opt_out_in_metadata(
                source, options, &source);
            ASSERT_EQ(unchanged.status, Status::Ok);
            EXPECT_EQ(unchanged.groups_unchanged, 1U);
            EXPECT_EQ(unchanged.entries_updated, 0U);
            EXPECT_EQ(unchanged.entries_added, 0U);
        }
    }

    TEST(MetadataLearningOptOutInTranslation,
         InvalidWireTypeOrCountCannotMatchDirtyNativeValue)
    {
        const std::array<uint16_t, 3U> words { 1U, 0U, 2U };
        MetadataLearningOptOutInTranslationOptions options;
        options.exif_version = 310U;

        MetaStore wrong_type;
        native(wrong_type, words,
               EntryFlags::Dirty | EntryFlags::ValueBigEndian, true, 2U, 6U);
        learning_source(wrong_type);
        wrong_type.finalize();
        EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                      wrong_type, options, &wrong_type)
                      .status,
                  Status::NativeConflict);
        ASSERT_NE(find_native(wrong_type), nullptr);
        EXPECT_EQ(find_native(wrong_type)->origin.wire_type.code, 2U);
        EXPECT_TRUE(any(find_native(wrong_type)->flags,
                        EntryFlags::ValueBigEndian));

        MetaStore wrong_count;
        native(wrong_count, words, EntryFlags::Dirty, false, 7U, 7U);
        learning_source(wrong_count);
        wrong_count.finalize();
        EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                      wrong_count, options, &wrong_count)
                      .status,
                  Status::NativeConflict);
        ASSERT_NE(find_native(wrong_count), nullptr);
        EXPECT_EQ(find_native(wrong_count)->origin.wire_count, 7U);
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        ASSERT_EQ(translate_xmp_learning_opt_out_in_metadata(
                       wrong_count, options, &wrong_count)
                       .status,
                   Status::Ok);
        const Entry* canonical = find_native(wrong_count);
        ASSERT_NE(canonical, nullptr);
        EXPECT_EQ(canonical->origin.wire_type.code, 7U);
        EXPECT_EQ(canonical->origin.wire_count, 6U);
        EXPECT_FALSE(any(canonical->flags, EntryFlags::ValueBigEndian));
    }

    TEST(MetadataLearningOptOutInTranslation,
         AbsentCleanAndDirtyDeletionIntentsAreIdempotent)
    {
        MetadataLearningOptOutInTranslationOptions options;
        options.exif_version = 310U;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;

        MetaStore protected_deletion;
        learning_deletion_source(protected_deletion);
        protected_deletion.finalize();
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::PreserveExisting;
        EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                      protected_deletion, options, &protected_deletion)
                      .status,
                  Status::Ok);
        EXPECT_EQ(native_count(protected_deletion, true), 0U);
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
        EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                      protected_deletion, options, &protected_deletion)
                      .status,
                  Status::Ok);
        EXPECT_EQ(native_count(protected_deletion, true), 0U);
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;

        MetaStore absent;
        learning_deletion_source(absent);
        absent.finalize();
        const auto added = translate_xmp_learning_opt_out_in_metadata(
            absent, options, &absent);
        ASSERT_EQ(added.status, Status::Ok);
        EXPECT_EQ(added.entries_added, 1U);
        const Entry* marker = find_any_native(absent, true);
        ASSERT_NE(marker, nullptr);
        EXPECT_TRUE(any(marker->flags, EntryFlags::Dirty));
        EXPECT_EQ(marker->value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(marker->value.elem_type, MetaElementType::U8);
        EXPECT_EQ(marker->value.count, 0U);
        EXPECT_EQ(marker->origin.wire_type.code, 7U);
        EXPECT_EQ(marker->origin.wire_count, 0U);
        const auto repeated = translate_xmp_learning_opt_out_in_metadata(
            absent, options, &absent);
        ASSERT_EQ(repeated.status, Status::Ok);
        EXPECT_EQ(repeated.groups_unchanged, 1U);
        EXPECT_EQ(repeated.entries_added, 0U);
        EXPECT_EQ(native_count(absent, true), 1U);

        MetaStore clean;
        learning_deletion_source(clean);
        ASSERT_NE(add_learning_marker(clean, EntryFlags::Deleted),
                  kInvalidEntryId);
        clean.finalize();
        const auto promoted = translate_xmp_learning_opt_out_in_metadata(
            clean, options, &clean);
        ASSERT_EQ(promoted.status, Status::Ok);
        EXPECT_EQ(promoted.entries_updated, 1U);
        EXPECT_EQ(promoted.entries_added, 0U);
        const Entry* clean_marker = find_any_native(clean, true);
        ASSERT_NE(clean_marker, nullptr);
        EXPECT_TRUE(any(clean_marker->flags, EntryFlags::Dirty));

        MetaStore dirty;
        learning_deletion_source(dirty);
        ASSERT_NE(add_learning_marker(
                      dirty, EntryFlags::Dirty | EntryFlags::Deleted),
                  kInvalidEntryId);
        dirty.finalize();
        const auto reused = translate_xmp_learning_opt_out_in_metadata(
            dirty, options, &dirty);
        ASSERT_EQ(reused.status, Status::Ok);
        EXPECT_EQ(reused.groups_unchanged, 1U);
        EXPECT_EQ(reused.entries_updated, 0U);
        EXPECT_EQ(reused.entries_added, 0U);
        EXPECT_EQ(native_count(dirty, true), 1U);
    }

    TEST(MetadataLearningOptOutInTranslation,
         LimitsDuplicatesMasksAndLateFailuresRollback)
    {
        const std::array<uint16_t, 3U> words { 1U, 0U, 2U };
        MetadataLearningOptOutInTranslationOptions options;
        options.exif_version = 310U;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;

        MetaStore duplicate;
        native(duplicate, words);
        native(duplicate, words);
        learning_source(duplicate);
        duplicate.finalize();
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
        EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                      duplicate, options, &duplicate)
                      .status,
                  Status::NativeConflict);
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        options.max_operations = 1U;
        EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                      duplicate, options, &duplicate)
                      .status,
                  Status::OperationLimitExceeded);
        EXPECT_EQ(native_count(duplicate, false), 2U);
        options.max_operations = kMetadataCaptureTranslationMaxOperations;
        const auto collapsed = translate_xmp_learning_opt_out_in_metadata(
            duplicate, options, &duplicate);
        ASSERT_EQ(collapsed.status, Status::Ok);
        EXPECT_EQ(collapsed.entries_updated, 1U);
        EXPECT_EQ(collapsed.entries_removed, 1U);
        EXPECT_EQ(native_count(duplicate, false), 1U);
        EXPECT_EQ(native_count(duplicate, true), 1U);

        MetaStore masked;
        learning_source(masked, EntryFlags::None);
        masked.finalize();
        options.source_mode = MetadataCaptureTranslationSourceMode::DirtyOnly;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
        const auto ignored = translate_xmp_learning_opt_out_in_metadata(
            masked, options, &masked);
        ASSERT_EQ(ignored.status, Status::Ok);
        EXPECT_EQ(ignored.source_properties, 0U);
        EXPECT_EQ(native_count(masked, false), 0U);
        options.source_mode = MetadataCaptureTranslationSourceMode::All;
        const auto accepted = translate_xmp_learning_opt_out_in_metadata(
            masked, options, &masked);
        ASSERT_EQ(accepted.status, Status::Ok);
        EXPECT_EQ(accepted.entries_added, 1U);

        MetaStore late_invalid;
        xmp(late_invalid, "LearningOptOutIn/NumberOfSets", "1");
        xmp(late_invalid, "LearningOptOutIn/Values[1]", "0");
        xmp(late_invalid, "LearningOptOutIn/Values[2]", "9");
        late_invalid.finalize();
        options.source_mode = MetadataCaptureTranslationSourceMode::DirtyOnly;
        EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                      late_invalid, options, &late_invalid)
                      .status,
                  Status::ValueOutOfRange);
        EXPECT_EQ(native_count(late_invalid, false), 0U);

        MetaStore duplicate_source;
        xmp(duplicate_source, "LearningOptOutIn/NumberOfSets", "1");
        xmp(duplicate_source, "LearningOptOutIn/NumberOfSets", "1");
        duplicate_source.finalize();
        EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                      duplicate_source, options, &duplicate_source)
                      .status,
                  Status::AmbiguousSource);
        EXPECT_EQ(native_count(duplicate_source, false), 0U);

        MetaStore version_zero;
        learning_deletion_source(version_zero);
        version_zero.finalize();
        options.exif_version = 0U;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                      version_zero, options, &version_zero)
                      .status,
                  Status::IncompleteSource);
        EXPECT_EQ(native_count(version_zero, true), 0U);

        MetaStore partial;
        xmp(partial, "LearningOptOutIn/NumberOfSets", "1");
        xmp(partial, "LearningOptOutIn/Values[1]", "0");
        partial.finalize();
        options.exif_version = 310U;
        EXPECT_EQ(translate_xmp_learning_opt_out_in_metadata(
                      partial, options, &partial)
                      .status,
                  Status::IncompleteSource);
        EXPECT_EQ(native_count(partial, false), 0U);
    }

}  // namespace
}  // namespace openmeta
