// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_serialize.h"
#include "openmeta/metadata_translation.h"
#include "openmeta/validate.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
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

    void native(MetaStore& store, uint16_t tag, uint16_t value,
                EntryFlags flags = EntryFlags::None, uint16_t wire_code = 3U,
                uint32_t wire_count = 1U)
    {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), "exififd", tag);
        entry.value = make_u16(value);
        entry.origin.wire_type = { WireFamily::Tiff, wire_code };
        entry.origin.wire_count = wire_count;
        entry.flags = flags;
        (void)store.add_entry(entry);
    }

    void native_description(MetaStore& store, std::string_view raw,
                            uint32_t wire_count,
                            EntryFlags flags = EntryFlags::None)
    {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), "exififd", 0xa40eU);
        entry.value = make_text(store.arena(), raw, TextEncoding::Utf8);
        entry.origin.wire_type = { WireFamily::Tiff, 129U };
        entry.origin.wire_count = wire_count;
        entry.origin.order_in_block = 42U;
        constexpr std::string_view type_name = "UTF8";
        entry.origin.wire_type_name = store.arena().append(std::as_bytes(
            std::span(type_name.data(), type_name.size())));
        entry.flags = flags;
        (void)store.add_entry(entry);
    }

    void development_source(MetaStore& store,
                            EntryFlags flags = EntryFlags::Dirty)
    {
        xmp(store, "DevelopmentType/DevelopmentCharacterstic", "1", flags);
        xmp(store, "DevelopmentType/FactoryDefault", "4", flags);
        xmp(store, "DevelopmentTypeDescription", "Developed", flags);
        xmp(store, "DistortionCorrection", "1", flags);
        xmp(store, "ChromaticAberrationCorrection", "0", flags);
        xmp(store, "ShadingCorrection", "1", flags);
        xmp(store, "NoiseReduction", "3", flags);
    }

    void development_deletion_source(MetaStore& store)
    {
        const EntryFlags deleted = EntryFlags::Dirty | EntryFlags::Deleted;
        xmp(store, "DevelopmentType/DevelopmentCharacterstic", "", deleted);
        xmp(store, "DevelopmentType/FactoryDefault", "", deleted);
        xmp(store, "DevelopmentTypeDescription", "", deleted);
        xmp(store, "DistortionCorrection", "", deleted);
        xmp(store, "ChromaticAberrationCorrection", "", deleted);
        xmp(store, "ShadingCorrection", "", deleted);
        xmp(store, "NoiseReduction", "", deleted);
    }

    std::string_view ifd_text(const MetaStore& store, ByteSpan span)
    {
        const auto raw = store.arena().span(span);
        return { reinterpret_cast<const char*>(raw.data()), raw.size() };
    }

    const Entry* find_any_native(const MetaStore& store, uint16_t tag,
                                 bool deleted)
    {
        for (const Entry& entry : store.entries()) {
            if (entry.key.kind != MetaKeyKind::ExifTag
                || ifd_text(store, entry.key.data.exif_tag.ifd) != "exififd"
                || entry.key.data.exif_tag.tag != tag
                || any(entry.flags, EntryFlags::Deleted) != deleted) {
                continue;
            }
            return &entry;
        }
        return nullptr;
    }

    size_t native_count(const MetaStore& store, uint16_t tag, bool deleted)
    {
        size_t count = 0U;
        for (const Entry& entry : store.entries()) {
            if (entry.key.kind == MetaKeyKind::ExifTag
                && ifd_text(store, entry.key.data.exif_tag.ifd) == "exififd"
                && entry.key.data.exif_tag.tag == tag
                && any(entry.flags, EntryFlags::Deleted) == deleted) {
                ++count;
            }
        }
        return count;
    }

    const Entry* find(const MetaStore& store, uint16_t tag)
    {
        const auto ids = store.find_all(make_exif_tag_key_view("exififd", tag));
        return ids.size() == 1U ? &store.entry(ids.front()) : nullptr;
    }

    std::string text_value(const MetaStore& store, uint16_t tag)
    {
        const Entry* entry = find(store, tag);
        if (!entry || entry->value.kind != MetaValueKind::Text) {
            return {};
        }
        const auto raw = store.arena().span(entry->value.data.span);
        return { reinterpret_cast<const char*>(raw.data()), raw.size() };
    }

    std::string portable(const MetaStore& store)
    {
        std::vector<std::byte> bytes(256U * 1024U);
        const XmpDumpResult result = dump_xmp_portable(store, bytes, {});
        EXPECT_EQ(result.status, XmpDumpStatus::Ok);
        return { reinterpret_cast<const char*>(bytes.data()),
                 static_cast<size_t>(result.written) };
    }

    TEST(MetadataDevelopmentTranslation, FullBatchStrictWirePortableAndRoundTrip)
    {
        MetaStore source;
        xmp(source, "DevelopmentType/DevelopmentCharacterstic", "1");
        xmp(source, "DevelopmentType/FactoryDefault", "4");
        xmp(source, "DevelopmentTypeDescription", "Developed by host");
        xmp(source, "DistortionCorrection", "1");
        xmp(source, "ChromaticAberrationCorrection", "0");
        xmp(source, "ShadingCorrection", "1");
        xmp(source, "NoiseReduction", "3");
        source.finalize();

        MetadataDevelopmentCorrectionTranslationOptions options;
        options.exif_version = 310U;
        const auto result = translate_xmp_development_correction_metadata(
            source, options, &source);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.entries_added, 6U);
        EXPECT_EQ(result.source_properties, 7U);
        ASSERT_NE(find(source, 0xa40dU), nullptr);
        ASSERT_NE(find(source, 0xa40fU), nullptr);
        ASSERT_NE(find(source, 0xa410U), nullptr);
        ASSERT_NE(find(source, 0xa411U), nullptr);
        ASSERT_NE(find(source, 0xa412U), nullptr);
        ASSERT_NE(find(source, 0xa40eU), nullptr);
        EXPECT_EQ(find(source, 0xa40dU)->value.data.u64, 0x0104U);
        EXPECT_EQ(find(source, 0xa40fU)->value.data.u64, 1U);
        EXPECT_EQ(find(source, 0xa410U)->value.data.u64, 0U);
        EXPECT_EQ(find(source, 0xa411U)->value.data.u64, 1U);
        EXPECT_EQ(find(source, 0xa412U)->value.data.u64, 3U);
        EXPECT_EQ(find(source, 0xa40eU)->origin.wire_type.code, 129U);
        EXPECT_EQ(find(source, 0xa40eU)->origin.wire_count, 18U);
        EXPECT_EQ(text_value(source, 0xa40eU), "Developed by host");
        EXPECT_TRUE(validate_store(source).ok());

        const std::string packet = portable(source);
        EXPECT_NE(packet.find("xmlns:exifEX=\"http://cipa.jp/exif/1.0/\""),
                  std::string::npos);
        EXPECT_NE(packet.find(
                      "<exifEX:DevelopmentType rdf:parseType=\"Resource\">"),
                  std::string::npos);
        EXPECT_NE(packet.find("<exifEX:DevelopmentCharacterstic>1</exifEX:DevelopmentCharacterstic>"),
                  std::string::npos);
        EXPECT_NE(packet.find("<exifEX:FactoryDefault>4</exifEX:FactoryDefault>"),
                  std::string::npos);
        EXPECT_NE(packet.find("<exifEX:DevelopmentTypeDescription>Developed by host</exifEX:DevelopmentTypeDescription>"),
                  std::string::npos);

        MetaStore decoded;
        ASSERT_EQ(decode_xmp_packet(
                      std::as_bytes(std::span(packet.data(), packet.size())),
                      decoded)
                      .status,
                  XmpDecodeStatus::Ok);
        decoded.finalize();
        MetadataDevelopmentCorrectionTranslationOptions round_trip;
        round_trip.exif_version = 300U;
        round_trip.source_mode = MetadataCaptureTranslationSourceMode::All;
        const auto restored = translate_xmp_development_correction_metadata(
            decoded, round_trip, &decoded);
        ASSERT_EQ(restored.status, Status::Ok);
        const auto restored_type = decoded.find_all(
            make_exif_tag_key_view("exififd", 0xa40dU));
        ASSERT_EQ(restored_type.size(), 1U);
        EXPECT_EQ(decoded.entry(restored_type.front()).value.data.u64,
                   0x0104U);
        EXPECT_EQ(text_value(decoded, 0xa40eU), "Developed by host");
    }

    TEST(MetadataDevelopmentTranslation, VersionPolicyConflictAndShapeRollback)
    {
        MetaStore missing_version;
        xmp(missing_version, "DistortionCorrection", "1");
        missing_version.finalize();
        const auto missing = translate_xmp_development_correction_metadata(
            missing_version, {}, &missing_version);
        EXPECT_EQ(missing.status, Status::IncompleteSource);
        EXPECT_EQ(missing_version.find_all(
                      make_exif_tag_key_view("exififd", 0xa40fU))
                      .size(),
                  0U);

        MetaStore conflict;
        native(conflict, 0xa40fU, 0U);
        xmp(conflict, "DistortionCorrection", "1");
        conflict.finalize();
        MetadataDevelopmentCorrectionTranslationOptions strict;
        strict.exif_version = 310U;
        const auto failed = translate_xmp_development_correction_metadata(
            conflict, strict, &conflict);
        EXPECT_EQ(failed.status, Status::NativeConflict);
        EXPECT_EQ(find(conflict, 0xa40fU)->value.data.u64, 0U);

        MetaStore malformed;
        xmp(malformed, "DevelopmentType/DevelopmentCharacterstic", "1");
        xmp(malformed, "DevelopmentType/FactoryDefault", "8");
        malformed.finalize();
        const auto bad = translate_xmp_development_correction_metadata(
            malformed, strict, &malformed);
        EXPECT_EQ(bad.status, Status::ValueOutOfRange);
        EXPECT_EQ(malformed.find_all(
                      make_exif_tag_key_view("exififd", 0xa40dU))
                      .size(),
                  0U);

        MetaStore invalid_description;
        Entry description;
        description.key = make_exif_tag_key(invalid_description.arena(),
                                             "exififd", 0xa40eU);
        const std::string invalid_xml(1U, '\x01');
        description.value = make_text(invalid_description.arena(), invalid_xml,
                                       TextEncoding::Utf8);
        description.origin.wire_type = { WireFamily::Tiff, 129U };
        description.origin.wire_count = 2U;
        ASSERT_NE(invalid_description.add_entry(description),
                  kInvalidEntryId);
        invalid_description.finalize();
        EXPECT_FALSE(validate_store(invalid_description).ok());
    }

    TEST(MetadataDevelopmentTranslation, ReplaceAndTombstoneAreAtomic)
    {
        MetaStore replace;
        native(replace, 0xa40fU, 0U);
        xmp(replace, "DistortionCorrection", "1");
        replace.finalize();
        MetadataDevelopmentCorrectionTranslationOptions options;
        options.exif_version = 300U;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        ASSERT_EQ(translate_xmp_development_correction_metadata(
                       replace, options, &replace)
                       .status,
                   Status::Ok);
        EXPECT_EQ(find(replace, 0xa40fU)->value.data.u64, 1U);

        MetaStore remove;
        native(remove, 0xa40fU, 1U);
        xmp(remove, "DistortionCorrection", "", EntryFlags::Dirty
                                            | EntryFlags::Deleted);
        remove.finalize();
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::PreserveExisting;
        ASSERT_EQ(translate_xmp_development_correction_metadata(
                       remove, options, &remove)
                       .status,
                   Status::Ok);
        ASSERT_NE(find(remove, 0xa40fU), nullptr);
        EXPECT_FALSE(any(find(remove, 0xa40fU)->flags,
                         EntryFlags::Deleted));
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      remove, options, &remove)
                      .status,
                  Status::NativeConflict);
        ASSERT_NE(find(remove, 0xa40fU), nullptr);
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        ASSERT_EQ(translate_xmp_development_correction_metadata(
                       remove, options, &remove)
                       .status,
                   Status::Ok);
        EXPECT_EQ(remove.find_all(
                      make_exif_tag_key_view("exififd", 0xa40fU))
                      .size(),
                  0U);
        ASSERT_NE(find_any_native(remove, 0xa40fU, true), nullptr);
        EXPECT_TRUE(any(find_any_native(remove, 0xa40fU, true)->flags,
                        EntryFlags::Dirty));
        EXPECT_TRUE(any(find_any_native(remove, 0xa40fU, true)->flags,
                        EntryFlags::Deleted));
    }

    TEST(MetadataDevelopmentTranslation,
         ExactSingletonPromotionPreservesWireAndDirtyMatchesStayUnchanged)
    {
        MetaStore source;
        native(source, 0xa40dU, 0x0104U);
        const std::string description("Developed\0", 10U);
        native_description(source, description, 10U);
        native(source, 0xa40fU, 1U);
        native(source, 0xa410U, 0U, EntryFlags::ValueBigEndian);
        native(source, 0xa411U, 1U);
        native(source, 0xa412U, 3U);
        development_source(source);
        source.finalize();

        MetadataDevelopmentCorrectionTranslationOptions options;
        options.exif_version = 310U;
        const auto promoted = translate_xmp_development_correction_metadata(
            source, options, &source);
        ASSERT_EQ(promoted.status, Status::Ok);
        EXPECT_EQ(promoted.groups_translated, 6U);
        EXPECT_EQ(promoted.entries_updated, 6U);
        EXPECT_EQ(promoted.entries_added, 0U);
        EXPECT_EQ(source.find_all(
                      make_exif_tag_key_view("exififd", 0x9000U))
                      .size(),
                  0U);
        for (const uint16_t tag : std::array<uint16_t, 6U> {
                 0xa40dU, 0xa40eU, 0xa40fU, 0xa410U, 0xa411U, 0xa412U }) {
            const Entry* entry = find(source, tag);
            ASSERT_NE(entry, nullptr);
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty));
            EXPECT_FALSE(any(entry->flags, EntryFlags::Deleted));
        }
        ASSERT_NE(find(source, 0xa40eU), nullptr);
        const Entry& retained_description = *find(source, 0xa40eU);
        const auto raw = source.arena().span(
            retained_description.value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(raw.data()),
                                   raw.size()),
                  std::string_view(description.data(), description.size()));
        EXPECT_EQ(retained_description.value.text_encoding, TextEncoding::Utf8);
        EXPECT_EQ(retained_description.origin.wire_type.code, 129U);
        EXPECT_EQ(retained_description.origin.wire_count, 10U);
        EXPECT_EQ(retained_description.origin.order_in_block, 42U);
        EXPECT_EQ(ifd_text(source,
                           retained_description.origin.wire_type_name),
                  "UTF8");
        EXPECT_TRUE(any(find(source, 0xa410U)->flags,
                        EntryFlags::ValueBigEndian));

        const auto unchanged = translate_xmp_development_correction_metadata(
            source, options, &source);
        ASSERT_EQ(unchanged.status, Status::Ok);
        EXPECT_EQ(unchanged.groups_unchanged, 6U);
        EXPECT_EQ(unchanged.entries_updated, 0U);
        EXPECT_EQ(unchanged.entries_added, 0U);
        EXPECT_EQ(text_value(source, 0xa40eU), description);
        EXPECT_TRUE(validate_store(source).ok());
        EXPECT_EQ(serialize_exif_tiff(source, {}).status,
                  ExifTiffSerializeStatus::OutputTruncated);

        MetaStore wrong_terminated_count;
        native_description(wrong_terminated_count, description, 11U);
        wrong_terminated_count.finalize();
        EXPECT_FALSE(validate_store(wrong_terminated_count).ok());

        MetaStore interior_nul;
        native_description(interior_nul, std::string("Dev\0eloped\0", 11U),
                           11U);
        interior_nul.finalize();
        EXPECT_FALSE(validate_store(interior_nul).ok());
    }

    TEST(MetadataDevelopmentTranslation,
         InvalidNativeWireAndValueTypesConflictBeforeReplace)
    {
        MetadataDevelopmentCorrectionTranslationOptions options;
        options.exif_version = 310U;

        MetaStore bad_scalar_wire;
        native(bad_scalar_wire, 0xa40fU, 1U, EntryFlags::Dirty, 7U, 1U);
        xmp(bad_scalar_wire, "DistortionCorrection", "1");
        bad_scalar_wire.finalize();
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      bad_scalar_wire, options, &bad_scalar_wire)
                      .status,
                  Status::NativeConflict);
        ASSERT_NE(find(bad_scalar_wire, 0xa40fU), nullptr);
        EXPECT_EQ(find(bad_scalar_wire, 0xa40fU)->origin.wire_type.code, 7U);
        EXPECT_TRUE(any(find(bad_scalar_wire, 0xa40fU)->flags,
                        EntryFlags::Dirty));

        MetaStore bad_scalar_count;
        native(bad_scalar_count, 0xa40fU, 1U, EntryFlags::Dirty, 3U, 2U);
        xmp(bad_scalar_count, "DistortionCorrection", "1");
        bad_scalar_count.finalize();
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      bad_scalar_count, options, &bad_scalar_count)
                      .status,
                  Status::NativeConflict);

        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        const auto fixed_scalar = translate_xmp_development_correction_metadata(
            bad_scalar_wire, options, &bad_scalar_wire);
        ASSERT_EQ(fixed_scalar.status, Status::Ok);
        EXPECT_EQ(find(bad_scalar_wire, 0xa40fU)->value.elem_type,
                  MetaElementType::U16);
        EXPECT_EQ(find(bad_scalar_wire, 0xa40fU)->origin.wire_type.code, 3U);
        EXPECT_EQ(find(bad_scalar_wire, 0xa40fU)->origin.wire_count, 1U);

        MetaStore bad_description_count;
        native_description(bad_description_count, "Developed", 9U,
                           EntryFlags::Dirty);
        xmp(bad_description_count, "DevelopmentTypeDescription", "Developed");
        bad_description_count.finalize();
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      bad_description_count, options, &bad_description_count)
                      .status,
                  Status::NativeConflict);
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        ASSERT_EQ(translate_xmp_development_correction_metadata(
                       bad_description_count, options, &bad_description_count)
                       .status,
                   Status::Ok);
        EXPECT_EQ(find(bad_description_count, 0xa40eU)->origin.wire_count,
                  10U);

        MetaStore bad_scalar_type;
        Entry invalid;
        invalid.key = make_exif_tag_key(bad_scalar_type.arena(), "exififd",
                                        0xa40fU);
        invalid.value = make_u8(1U);
        invalid.origin.wire_type = { WireFamily::Tiff, 3U };
        invalid.origin.wire_count = 1U;
        invalid.flags = EntryFlags::Dirty;
        ASSERT_NE(bad_scalar_type.add_entry(invalid), kInvalidEntryId);
        xmp(bad_scalar_type, "DistortionCorrection", "1");
        bad_scalar_type.finalize();
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      bad_scalar_type, options, &bad_scalar_type)
                      .status,
                  Status::NativeConflict);
    }

    TEST(MetadataDevelopmentTranslation,
         AbsentAndExistingDeletionIntentsAreTypedAndBounded)
    {
        MetadataDevelopmentCorrectionTranslationOptions options;
        options.exif_version = 310U;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;

        MetaStore protected_deletion;
        development_deletion_source(protected_deletion);
        protected_deletion.finalize();
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::PreserveExisting;
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      protected_deletion, options, &protected_deletion)
                      .status,
                  Status::Ok);
        EXPECT_EQ(native_count(protected_deletion, 0xa40dU, true), 0U);
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      protected_deletion, options, &protected_deletion)
                      .status,
                  Status::Ok);
        EXPECT_EQ(native_count(protected_deletion, 0xa40dU, true), 0U);
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;

        MetaStore absent;
        development_deletion_source(absent);
        absent.finalize();
        const auto added = translate_xmp_development_correction_metadata(
            absent, options, &absent);
        ASSERT_EQ(added.status, Status::Ok);
        EXPECT_EQ(added.entries_added, 6U);
        for (const uint16_t tag : std::array<uint16_t, 6U> {
                 0xa40dU, 0xa40eU, 0xa40fU, 0xa410U, 0xa411U, 0xa412U }) {
            const Entry* marker = find_any_native(absent, tag, true);
            ASSERT_NE(marker, nullptr);
            EXPECT_TRUE(any(marker->flags, EntryFlags::Dirty));
            EXPECT_TRUE(any(marker->flags, EntryFlags::Deleted));
            if (tag == 0xa40eU) {
                EXPECT_EQ(marker->value.kind, MetaValueKind::Text);
                EXPECT_EQ(marker->value.text_encoding, TextEncoding::Utf8);
                EXPECT_EQ(marker->value.count, 0U);
                EXPECT_EQ(marker->origin.wire_type.code, 129U);
                EXPECT_EQ(marker->origin.wire_count, 1U);
            } else {
                EXPECT_EQ(marker->value.kind, MetaValueKind::Scalar);
                EXPECT_EQ(marker->value.elem_type, MetaElementType::U16);
                EXPECT_EQ(marker->origin.wire_type.code, 3U);
                EXPECT_EQ(marker->origin.wire_count, 1U);
                if (tag == 0xa40dU)
                    EXPECT_EQ(marker->value.data.u64, 0x0101U);
            }
        }
        const auto repeated = translate_xmp_development_correction_metadata(
            absent, options, &absent);
        ASSERT_EQ(repeated.status, Status::Ok);
        EXPECT_EQ(repeated.groups_unchanged, 6U);
        EXPECT_EQ(repeated.entries_added, 0U);
        EXPECT_EQ(repeated.entries_updated, 0U);

        MetaStore existing;
        development_deletion_source(existing);
        native(existing, 0xa40fU, 1U, EntryFlags::Deleted);
        native(existing, 0xa411U, 1U,
               EntryFlags::Dirty | EntryFlags::Deleted);
        existing.finalize();
        const auto reused = translate_xmp_development_correction_metadata(
            existing, options, &existing);
        ASSERT_EQ(reused.status, Status::Ok);
        EXPECT_EQ(reused.entries_added, 4U);
        EXPECT_EQ(reused.entries_updated, 1U);
        EXPECT_EQ(reused.groups_unchanged, 1U);
        const Entry* clean_marker = find_any_native(existing, 0xa40fU, true);
        ASSERT_NE(clean_marker, nullptr);
        EXPECT_TRUE(any(clean_marker->flags, EntryFlags::Dirty));
        const Entry* dirty_marker = find_any_native(existing, 0xa411U, true);
        ASSERT_NE(dirty_marker, nullptr);
        EXPECT_TRUE(any(dirty_marker->flags, EntryFlags::Dirty));

        MetaStore limited;
        development_deletion_source(limited);
        limited.finalize();
        options.max_added_entries = 1U;
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      limited, options, &limited)
                      .status,
                  Status::EntryLimitExceeded);
        EXPECT_EQ(limited.entries().size(), 7U);
        options.max_added_entries
            = kMetadataDevelopmentCorrectionTranslationMaxAddedEntries;
        options.max_operations = 5U;
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      limited, options, &limited)
                      .status,
                  Status::OperationLimitExceeded);
        EXPECT_EQ(limited.entries().size(), 7U);
    }

    TEST(MetadataDevelopmentTranslation,
         GroupedLateFailuresMasksDuplicatesAndVersionPolicyRollback)
    {
        MetadataDevelopmentCorrectionTranslationOptions options;
        options.exif_version = 310U;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::FailOnConflict;

        MetaStore exact_clean;
        native(exact_clean, 0xa40dU, 0x0104U);
        native_description(exact_clean, "Developed", 10U);
        native(exact_clean, 0xa40fU, 1U);
        native(exact_clean, 0xa410U, 0U);
        native(exact_clean, 0xa411U, 1U);
        native(exact_clean, 0xa412U, 3U);
        development_source(exact_clean);
        exact_clean.finalize();

        MetaStore sentinel;
        xmp(sentinel, "Unrelated", "keep");
        sentinel.finalize();
        options.max_operations = 5U;
        const auto limited_promotion
            = translate_xmp_development_correction_metadata(
                exact_clean, options, &sentinel);
        EXPECT_EQ(limited_promotion.status, Status::OperationLimitExceeded);
        for (const uint16_t tag : std::array<uint16_t, 6U> {
                 0xa40dU, 0xa40eU, 0xa40fU, 0xa410U, 0xa411U, 0xa412U }) {
            const Entry* entry = find(exact_clean, tag);
            ASSERT_NE(entry, nullptr);
            EXPECT_FALSE(any(entry->flags, EntryFlags::Dirty));
        }
        EXPECT_EQ(sentinel.entries().size(), 1U);
        const auto sentinel_ids = sentinel.find_all(
            make_xmp_property_key_view(kExifEx, "Unrelated"));
        ASSERT_EQ(sentinel_ids.size(), 1U);
        const Entry& sentinel_entry = sentinel.entry(sentinel_ids.front());
        const auto sentinel_bytes
            = sentinel.arena().span(sentinel_entry.value.data.span);
        EXPECT_EQ(std::string_view(
                      reinterpret_cast<const char*>(sentinel_bytes.data()),
                      sentinel_bytes.size()),
                  "keep");

        MetadataDevelopmentCorrectionTranslationOptions normal_options;
        normal_options.exif_version = 310U;
        normal_options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
        const auto accepted_promotion
            = translate_xmp_development_correction_metadata(
                exact_clean, normal_options, &sentinel);
        ASSERT_EQ(accepted_promotion.status, Status::Ok);
        EXPECT_EQ(accepted_promotion.entries_updated, 6U);
        for (const uint16_t tag : std::array<uint16_t, 6U> {
                 0xa40dU, 0xa40eU, 0xa40fU, 0xa410U, 0xa411U, 0xa412U }) {
            const Entry* entry = find(sentinel, tag);
            ASSERT_NE(entry, nullptr);
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty));
        }
        options.max_operations = kMetadataCaptureTranslationMaxOperations;

        MetaStore late_conflict;
        native(late_conflict, 0xa40dU, 0x0104U);
        native(late_conflict, 0xa412U, 0U);
        development_source(late_conflict);
        late_conflict.finalize();
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      late_conflict, options, &late_conflict)
                      .status,
                  Status::NativeConflict);
        EXPECT_FALSE(any(find(late_conflict, 0xa40dU)->flags,
                         EntryFlags::Dirty));
        EXPECT_EQ(find(late_conflict, 0xa412U)->value.data.u64, 0U);
        EXPECT_EQ(find(late_conflict, 0xa40fU), nullptr);

        MetaStore duplicate_source;
        xmp(duplicate_source, "DistortionCorrection", "1");
        xmp(duplicate_source, "DistortionCorrection", "1");
        duplicate_source.finalize();
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      duplicate_source, options, &duplicate_source)
                      .status,
                  Status::AmbiguousSource);
        EXPECT_EQ(find(duplicate_source, 0xa40fU), nullptr);

        MetaStore duplicate_native;
        native(duplicate_native, 0xa40fU, 0U);
        native(duplicate_native, 0xa40fU, 0U);
        xmp(duplicate_native, "DistortionCorrection", "1");
        duplicate_native.finalize();
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      duplicate_native, options, &duplicate_native)
                      .status,
                  Status::NativeConflict);
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        const auto collapsed = translate_xmp_development_correction_metadata(
            duplicate_native, options, &duplicate_native);
        ASSERT_EQ(collapsed.status, Status::Ok);
        EXPECT_EQ(collapsed.entries_updated, 1U);
        EXPECT_EQ(collapsed.entries_removed, 1U);
        EXPECT_EQ(native_count(duplicate_native, 0xa40fU, false), 1U);
        EXPECT_EQ(native_count(duplicate_native, 0xa40fU, true), 1U);

        MetaStore masked;
        development_source(masked, EntryFlags::None);
        masked.finalize();
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
        const auto ignored = translate_xmp_development_correction_metadata(
            masked, options, &masked);
        ASSERT_EQ(ignored.status, Status::Ok);
        EXPECT_EQ(ignored.source_properties, 0U);
        EXPECT_EQ(masked.find_all(
                      make_exif_tag_key_view("exififd", 0xa40dU))
                      .size(),
                  0U);
        options.source_mode = MetadataCaptureTranslationSourceMode::All;
        const auto accepted = translate_xmp_development_correction_metadata(
            masked, options, &masked);
        ASSERT_EQ(accepted.status, Status::Ok);
        EXPECT_EQ(accepted.entries_added, 6U);

        MetaStore version_zero;
        development_deletion_source(version_zero);
        version_zero.finalize();
        options.source_mode = MetadataCaptureTranslationSourceMode::DirtyOnly;
        options.exif_version = 0U;
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      version_zero, options, &version_zero)
                      .status,
                  Status::IncompleteSource);
        EXPECT_EQ(find(version_zero, 0xa40dU), nullptr);

        MetaStore partial;
        xmp(partial, "DevelopmentType/DevelopmentCharacterstic", "",
            EntryFlags::Dirty | EntryFlags::Deleted);
        partial.finalize();
        options.exif_version = 310U;
        EXPECT_EQ(translate_xmp_development_correction_metadata(
                      partial, options, &partial)
                      .status,
                  Status::IncompleteSource);
        EXPECT_EQ(find(partial, 0xa40dU), nullptr);
    }

}  // namespace
}  // namespace openmeta
