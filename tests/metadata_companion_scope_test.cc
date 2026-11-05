// SPDX-License-Identifier: Apache-2.0
#include "openmeta/metadata_concepts.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include <gtest/gtest.h>
#include <array>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static EntryId add_exif_text(MetaStore& store, BlockId block,
                                 std::string_view ifd, uint16_t tag,
                                 std::string_view value)
    {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd, tag);
        entry.origin.block = block;
        entry.value = make_text(store.arena(), value, TextEncoding::Ascii);
        return store.add_entry(entry);
    }

    static EntryId add_exif_u16(MetaStore& store, BlockId block,
                                std::string_view ifd, uint16_t tag,
                                uint16_t value)
    {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd, tag);
        entry.origin.block = block;
        entry.value = make_u16(value);
        return store.add_entry(entry);
    }

    static EntryId add_exif_urational_array(
        MetaStore& store, BlockId block, std::string_view ifd, uint16_t tag,
        std::span<const URational> values)
    {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd, tag);
        entry.origin.block = block;
        entry.value = make_urational_array(store.arena(), values);
        return store.add_entry(entry);
    }

    static EntryId add_xmp_text(MetaStore& store, BlockId block,
                                std::string_view schema, std::string_view path,
                                std::string_view value)
    {
        Entry entry;
        entry.key = make_xmp_property_key(store.arena(), schema, path);
        entry.origin.block = block;
        entry.value = make_text(store.arena(), value, TextEncoding::Utf8);
        return store.add_entry(entry);
    }

    static EntryId add_iptc_text(MetaStore& store, BlockId block,
                                 uint16_t record, uint16_t dataset,
                                 std::string_view value)
    {
        Entry entry;
        entry.key = make_iptc_dataset_key(record, dataset);
        entry.origin.block = block;
        entry.value = make_text(store.arena(), value, TextEncoding::Ascii);
        return store.add_entry(entry);
    }

    static bool has_source(const MetadataConceptCandidate& candidate,
                           EntryId id)
    {
        for (EntryId source : candidate.source_entries) {
            if (source == id) {
                return true;
            }
        }
        return false;
    }

    static const MetadataConceptCandidate* candidate_for(
        const MetadataConceptResolution& resolution, EntryId id,
        MetadataConceptRole role)
    {
        for (const MetadataConceptCandidate& candidate : resolution.candidates) {
            if (candidate.entry_id == id && candidate.role == role) {
                return &candidate;
            }
        }
        return nullptr;
    }

    static const MetadataConceptCandidate* candidate_for_sources(
        const MetadataConceptResolution& resolution, EntryId first,
        EntryId second, MetadataConceptRole role)
    {
        for (const MetadataConceptCandidate& candidate : resolution.candidates) {
            if (candidate.role == role && has_source(candidate, first)
                && has_source(candidate, second)) {
                return &candidate;
            }
        }
        return nullptr;
    }

    TEST(MetadataCompanionScope,
         ExifCompanionsStayLocalAndOnlyInheritFromSelectedDirectories)
    {
        MetaStore store;
        const BlockId a = store.add_block({});
        const BlockId b = store.add_block({});
        const EntryId created_a = add_exif_text(
            store, a, "exififd", 0x9003U, "2024:04:19 12:34:56");
        const EntryId offset_a = add_exif_text(store, a, "exififd", 0x9011U,
                                                "+02:00");
        const EntryId subsecond_a = add_exif_text(
            store, a, "exififd", 0x9291U, "111");
        const EntryId created_b = add_exif_text(
            store, b, "exififd", 0x9003U, "2025:04:19 12:34:56");
        const EntryId offset_b = add_exif_text(store, b, "exififd", 0x9011U,
                                                "-03:00");
        const EntryId subsecond_b = add_exif_text(
            store, b, "exififd", 0x9291U, "222");
        const EntryId modified_a = add_exif_text(
            store, a, "ifd0", 0x0132U, "2024:04:20 10:20:30");
        const EntryId modified_offset_b = add_exif_text(
            store, b, "exififd", 0x9010U, "+04:30");

        const EntryId latitude_ref_a = add_exif_text(
            store, a, "gpsifd", 0x0001U, "S");
        const std::array<URational, 3U> latitude_values {{
            { 10U, 1U }, { 0U, 1U }, { 0U, 1U },
        }};
        const EntryId latitude_a = add_exif_urational_array(
            store, a, "gpsifd", 0x0002U, latitude_values);
        const EntryId latitude_ref_b = add_exif_text(
            store, b, "gpsifd", 0x0001U, "N");
        const EntryId latitude_b = add_exif_urational_array(
            store, b, "gpsifd", 0x0002U, latitude_values);
        const EntryId altitude_ref_a = add_exif_u16(
            store, a, "gpsifd", 0x0005U, 1U);
        const std::array<URational, 1U> altitude_value_a {{ { 10U, 1U } }};
        const EntryId altitude_a = add_exif_urational_array(
            store, a, "gpsifd", 0x0006U, altitude_value_a);
        const EntryId altitude_ref_b = add_exif_u16(
            store, b, "gpsifd", 0x0005U, 0U);
        const std::array<URational, 1U> altitude_value_b {{ { 20U, 1U } }};
        const EntryId altitude_b = add_exif_urational_array(
            store, b, "gpsifd", 0x0006U, altitude_value_b);

        const EntryId gps_date_a = add_exif_text(
            store, a, "gpsifd", 0x001DU, "2024:04:19");
        const std::array<URational, 3U> gps_time_values_a {{
            { 1U, 1U }, { 2U, 1U }, { 3U, 1U },
        }};
        const EntryId gps_time_a = add_exif_urational_array(
            store, a, "gpsifd", 0x0007U, gps_time_values_a);
        const EntryId gps_date_b = add_exif_text(
            store, b, "gpsifd", 0x001DU, "2025:04:19");
        const std::array<URational, 3U> gps_time_values_b {{
            { 4U, 1U }, { 5U, 1U }, { 6U, 1U },
        }};
        const EntryId gps_time_b = add_exif_urational_array(
            store, b, "gpsifd", 0x0007U, gps_time_values_b);
        store.finalize();

        const MetadataConceptResolution all_dates
            = resolve_metadata_concept(store, MetadataConceptKind::DateTime);
        ASSERT_EQ(all_dates.candidates.size(), 3U);
        const MetadataConceptCandidate* original_a = candidate_for(
            all_dates, created_a, MetadataConceptRole::Created);
        const MetadataConceptCandidate* original_b = candidate_for(
            all_dates, created_b, MetadataConceptRole::Created);
        const MetadataConceptCandidate* modified = candidate_for(
            all_dates, modified_a, MetadataConceptRole::Modified);
        ASSERT_NE(original_a, nullptr);
        ASSERT_NE(original_b, nullptr);
        ASSERT_NE(modified, nullptr);
        EXPECT_TRUE(has_source(*original_a, offset_a));
        EXPECT_TRUE(has_source(*original_a, subsecond_a));
        EXPECT_FALSE(has_source(*original_a, offset_b));
        EXPECT_FALSE(has_source(*original_a, subsecond_b));
        EXPECT_EQ(original_a->date_time_utc_offset_min, 120);
        EXPECT_EQ(original_a->date_time_subsecond, "111");
        EXPECT_TRUE(has_source(*original_b, offset_b));
        EXPECT_TRUE(has_source(*original_b, subsecond_b));
        EXPECT_FALSE(has_source(*original_b, offset_a));
        EXPECT_EQ(original_b->date_time_utc_offset_min, -180);
        EXPECT_EQ(original_b->date_time_subsecond, "222");
        EXPECT_FALSE(modified->date_time_has_utc_offset);

        const MetadataConceptResolution all_gps
            = resolve_metadata_concept(store, MetadataConceptKind::Gps);
        const MetadataConceptCandidate* lat_a = candidate_for(
            all_gps, latitude_a, MetadataConceptRole::Latitude);
        const MetadataConceptCandidate* lat_b = candidate_for(
            all_gps, latitude_b, MetadataConceptRole::Latitude);
        const MetadataConceptCandidate* alt_a = candidate_for(
            all_gps, altitude_a, MetadataConceptRole::Altitude);
        const MetadataConceptCandidate* alt_b = candidate_for(
            all_gps, altitude_b, MetadataConceptRole::Altitude);
        ASSERT_NE(lat_a, nullptr);
        ASSERT_NE(lat_b, nullptr);
        ASSERT_NE(alt_a, nullptr);
        ASSERT_NE(alt_b, nullptr);
        EXPECT_TRUE(has_source(*lat_a, latitude_ref_a));
        EXPECT_NEAR(lat_a->numeric[0], -10.0, 1.0e-9);
        EXPECT_TRUE(has_source(*lat_b, latitude_ref_b));
        EXPECT_NEAR(lat_b->numeric[0], 10.0, 1.0e-9);
        EXPECT_TRUE(has_source(*alt_a, altitude_ref_a));
        EXPECT_FALSE(has_source(*alt_a, altitude_ref_b));
        EXPECT_TRUE(has_source(*alt_b, altitude_ref_b));
        EXPECT_FALSE(has_source(*alt_b, altitude_ref_a));
        EXPECT_NEAR(alt_a->numeric[0], -10.0, 1.0e-9);
        EXPECT_NEAR(alt_b->numeric[0], 20.0, 1.0e-9);
        const MetadataConceptCandidate* timestamp_a = candidate_for_sources(
            all_gps, gps_date_a, gps_time_a, MetadataConceptRole::Timestamp);
        const MetadataConceptCandidate* timestamp_b = candidate_for_sources(
            all_gps, gps_date_b, gps_time_b, MetadataConceptRole::Timestamp);
        ASSERT_NE(timestamp_a, nullptr);
        ASSERT_NE(timestamp_b, nullptr);
        EXPECT_EQ(timestamp_a->date_time_hour, 1U);
        EXPECT_EQ(timestamp_b->date_time_hour, 4U);

        const std::array<MetadataDirectorySelector, 2U> both_directories {{
            { a, {} }, { b, {} },
        }};
        MetadataQueryOptions selected;
        selected.scope.enabled = true;
        selected.scope.directories = both_directories;
        const MetadataConceptResolution both_dates = resolve_metadata_concept(
            store, MetadataConceptKind::DateTime, selected);
        ASSERT_EQ(both_dates.candidates.size(), 3U);
        EXPECT_EQ(both_dates.preferred_entry, created_a);
        original_a = candidate_for(both_dates, created_a,
                                   MetadataConceptRole::Created);
        original_b = candidate_for(both_dates, created_b,
                                   MetadataConceptRole::Created);
        modified = candidate_for(both_dates, modified_a,
                                 MetadataConceptRole::Modified);
        ASSERT_NE(original_a, nullptr);
        ASSERT_NE(original_b, nullptr);
        ASSERT_NE(modified, nullptr);
        EXPECT_TRUE(original_a->preferred);
        EXPECT_FALSE(original_b->preferred);
        EXPECT_FALSE(modified->date_time_has_utc_offset);

        MetadataDirectorySelector only_a { a, {} };
        selected.scope.directories
            = std::span<const MetadataDirectorySelector>(&only_a, 1U);
        selected.scope.inherit_across_directories = true;
        const MetadataConceptResolution isolated_a = resolve_metadata_concept(
            store, MetadataConceptKind::DateTime, selected);
        ASSERT_EQ(isolated_a.candidates.size(), 2U);
        EXPECT_EQ(isolated_a.preferred_entry, created_a);
        modified = candidate_for(isolated_a, modified_a,
                                 MetadataConceptRole::Modified);
        ASSERT_NE(modified, nullptr);
        EXPECT_FALSE(modified->date_time_has_utc_offset);
        EXPECT_FALSE(has_source(*modified, modified_offset_b));

        selected.scope.directories = both_directories;
        selected.scope.inherit_across_directories = true;
        const MetadataConceptResolution inherited = resolve_metadata_concept(
            store, MetadataConceptKind::DateTime, selected);
        modified = candidate_for(inherited, modified_a,
                                 MetadataConceptRole::Modified);
        ASSERT_NE(modified, nullptr);
        EXPECT_TRUE(modified->date_time_has_utc_offset);
        EXPECT_EQ(modified->date_time_utc_offset_min, 270);
        EXPECT_TRUE(has_source(*modified, modified_offset_b));

        const MetadataConceptResolution both_gps = resolve_metadata_concept(
            store, MetadataConceptKind::Gps, selected);
        lat_a = candidate_for(both_gps, latitude_a,
                              MetadataConceptRole::Latitude);
        lat_b = candidate_for(both_gps, latitude_b,
                              MetadataConceptRole::Latitude);
        ASSERT_NE(lat_a, nullptr);
        ASSERT_NE(lat_b, nullptr);
        EXPECT_NEAR(lat_a->numeric[0], -10.0, 1.0e-9);
        EXPECT_NEAR(lat_b->numeric[0], 10.0, 1.0e-9);
        EXPECT_TRUE(has_source(*lat_b, latitude_ref_b));
    }

    TEST(MetadataCompanionScope,
         XmpAndIptcCompositesKeepBlockContextAndUseExplicitInheritance)
    {
        MetaStore store;
        const BlockId a = store.add_block({});
        const BlockId b = store.add_block({});
        static constexpr std::string_view kExifXmp
            = "http://ns.adobe.com/exif/1.0/";
        const EntryId xmp_altitude_a = add_xmp_text(
            store, a, kExifXmp, "exif:GPSAltitude", "10");
        const EntryId xmp_altitude_ref_a = add_xmp_text(
            store, a, kExifXmp, "exif:GPSAltitudeRef", "1");
        const EntryId xmp_altitude_b = add_xmp_text(
            store, b, kExifXmp, "exif:GPSAltitude", "20");
        const EntryId xmp_altitude_ref_b = add_xmp_text(
            store, b, kExifXmp, "exif:GPSAltitudeRef", "0");
        const EntryId xmp_date_a = add_xmp_text(
            store, a, kExifXmp, "exif:GPSDateStamp", "2024:04:19");
        const EntryId xmp_time_a = add_xmp_text(
            store, a, kExifXmp, "exif:GPSTimeStamp", "01:02:03Z");
        const EntryId xmp_date_b = add_xmp_text(
            store, b, kExifXmp, "exif:GPSDateStamp", "2025:04:19");
        const EntryId xmp_time_b = add_xmp_text(
            store, b, kExifXmp, "exif:GPSTimeStamp", "04:05:06Z");

        const EntryId iptc_date_a = add_iptc_text(
            store, a, 2U, 55U, "20240419");
        const EntryId iptc_date_b = add_iptc_text(
            store, b, 2U, 55U, "20250419");
        const EntryId iptc_time_b = add_iptc_text(
            store, b, 2U, 60U, "040506+0000");
        const EntryId release_date_a = add_iptc_text(
            store, a, 2U, 30U, "20240420");
        const EntryId release_time_a = add_iptc_text(
            store, a, 2U, 35U, "010203+0000");
        const EntryId release_date_b = add_iptc_text(
            store, b, 2U, 30U, "20250420");
        const EntryId release_time_b = add_iptc_text(
            store, b, 2U, 35U, "040506+0000");
        store.finalize();

        const MetadataConceptResolution gps
            = resolve_metadata_concept(store, MetadataConceptKind::Gps);
        const MetadataConceptCandidate* altitude_a = candidate_for(
            gps, xmp_altitude_a, MetadataConceptRole::Altitude);
        const MetadataConceptCandidate* altitude_b = candidate_for(
            gps, xmp_altitude_b, MetadataConceptRole::Altitude);
        ASSERT_NE(altitude_a, nullptr);
        ASSERT_NE(altitude_b, nullptr);
        EXPECT_TRUE(has_source(*altitude_a, xmp_altitude_ref_a));
        EXPECT_FALSE(has_source(*altitude_a, xmp_altitude_ref_b));
        EXPECT_TRUE(has_source(*altitude_b, xmp_altitude_ref_b));
        EXPECT_FALSE(has_source(*altitude_b, xmp_altitude_ref_a));
        EXPECT_NEAR(altitude_a->numeric[0], -10.0, 1.0e-9);
        EXPECT_NEAR(altitude_b->numeric[0], 20.0, 1.0e-9);
        const MetadataConceptCandidate* xmp_timestamp_a
            = candidate_for_sources(gps, xmp_date_a, xmp_time_a,
                                    MetadataConceptRole::Timestamp);
        const MetadataConceptCandidate* xmp_timestamp_b
            = candidate_for_sources(gps, xmp_date_b, xmp_time_b,
                                    MetadataConceptRole::Timestamp);
        ASSERT_NE(xmp_timestamp_a, nullptr);
        ASSERT_NE(xmp_timestamp_b, nullptr);
        EXPECT_EQ(xmp_timestamp_a->date_time_hour, 1U);
        EXPECT_EQ(xmp_timestamp_b->date_time_hour, 4U);

        const std::array<MetadataDirectorySelector, 2U> both_directories {{
            { a, {} }, { b, {} },
        }};
        MetadataQueryOptions selected;
        selected.scope.enabled = true;
        selected.scope.directories = both_directories;
        const MetadataConceptResolution dates = resolve_metadata_concept(
            store, MetadataConceptKind::DateTime, selected);
        const MetadataConceptCandidate* date_created_b = candidate_for_sources(
            dates, iptc_date_b, iptc_time_b,
            MetadataConceptRole::DateCreated);
        ASSERT_NE(date_created_b, nullptr);
        EXPECT_EQ(candidate_for_sources(dates, iptc_date_a, iptc_time_b,
                                        MetadataConceptRole::DateCreated),
                  nullptr);

        selected.scope.inherit_across_directories = true;
        const MetadataConceptResolution inherited_dates =
            resolve_metadata_concept(store, MetadataConceptKind::DateTime,
                                     selected);
        const MetadataConceptCandidate* date_created_a = candidate_for_sources(
            inherited_dates, iptc_date_a, iptc_time_b,
            MetadataConceptRole::DateCreated);
        date_created_b = candidate_for_sources(
            inherited_dates, iptc_date_b, iptc_time_b,
            MetadataConceptRole::DateCreated);
        ASSERT_NE(date_created_a, nullptr);
        ASSERT_NE(date_created_b, nullptr);
        EXPECT_EQ(date_created_a->date_time_hour, 4U);
        EXPECT_EQ(date_created_b->date_time_hour, 4U);

        const MetadataConceptResolution editorial =
            resolve_metadata_concept(store, MetadataConceptKind::Descriptive,
                                     selected);
        const MetadataConceptCandidate* release_a = candidate_for_sources(
            editorial, release_date_a, release_time_a,
            MetadataConceptRole::EditorialReleaseDate);
        const MetadataConceptCandidate* release_b = candidate_for_sources(
            editorial, release_date_b, release_time_b,
            MetadataConceptRole::EditorialReleaseDate);
        ASSERT_NE(release_a, nullptr);
        ASSERT_NE(release_b, nullptr);
        EXPECT_EQ(release_a->date_time_hour, 1U);
        EXPECT_EQ(release_b->date_time_hour, 4U);
    }

    TEST(MetadataCompanionScope,
         SemanticWorkBudgetBoundsTimestampScansAndIsSharedByAggregates)
    {
        MetaStore store;
        const BlockId block = store.add_block({});
        for (uint32_t i = 0U; i < 256U; ++i) {
            ASSERT_NE(add_exif_text(store, block, "gpsifd", 0x0007U,
                                    "01:02:03Z"),
                      kInvalidEntryId);
        }
        store.finalize();

        MetadataQueryOptions options;
        options.max_semantic_work = 32U;
        const MetadataConceptResolution gps = resolve_metadata_concept(
            store, MetadataConceptKind::Gps, options);
        EXPECT_EQ(gps.semantic_work_used, 32U);
        EXPECT_TRUE(gps.semantic_work_limited);
        EXPECT_EQ(gps.preferred_entry, kInvalidEntryId);
        for (const MetadataConceptCandidate& candidate : gps.candidates) {
            EXPECT_FALSE(candidate.preferred);
        }

        MetadataRawDataDescriptor raw_descriptor;
        const MetadataConceptResolution raw_gps = resolve_metadata_concept(
            store, MetadataConceptKind::Gps, raw_descriptor, options);
        EXPECT_LE(raw_gps.semantic_work_used, options.max_semantic_work);
        EXPECT_TRUE(raw_gps.semantic_work_limited);
        EXPECT_EQ(raw_gps.preferred_entry, kInvalidEntryId);

        options.max_semantic_work = 24U;
        const MetadataConceptResult all = resolve_metadata_concepts(store,
                                                                    options);
        EXPECT_EQ(all.semantic_work_used, options.max_semantic_work);
        EXPECT_TRUE(all.semantic_work_limited);
        const MetadataConceptResolution* gps_concept = nullptr;
        const MetadataConceptResolution* datetime_concept = nullptr;
        for (const MetadataConceptResolution& resolution : all.concepts) {
            if (resolution.kind == MetadataConceptKind::Gps) {
                gps_concept = &resolution;
            } else if (resolution.kind == MetadataConceptKind::DateTime) {
                datetime_concept = &resolution;
            }
        }
        ASSERT_NE(gps_concept, nullptr);
        ASSERT_NE(datetime_concept, nullptr);
        EXPECT_TRUE(gps_concept->semantic_work_limited);
        EXPECT_EQ(gps_concept->semantic_work_used, 0U);
        EXPECT_EQ(gps_concept->preferred_entry, kInvalidEntryId);
        EXPECT_EQ(datetime_concept->semantic_work_used,
                  options.max_semantic_work);

        const MetadataConceptResult all_raw = resolve_metadata_concepts(
            store, raw_descriptor, options);
        EXPECT_LE(all_raw.semantic_work_used, options.max_semantic_work);
        EXPECT_TRUE(all_raw.semantic_work_limited);
    }

    TEST(MetadataCompanionScope,
         ExplicitInheritedCompanionScansRespectSemanticWorkCap)
    {
        MetaStore store;
        const BlockId a = store.add_block({});
        const BlockId b = store.add_block({});
        const std::array<URational, 3U> latitude_values {{
            { 10U, 1U }, { 0U, 1U }, { 0U, 1U },
        }};
        const EntryId latitude = add_exif_urational_array(
            store, a, "gpsifd", 0x0002U, latitude_values);
        const EntryId inherited_ref = add_exif_text(
            store, b, "gpsifd", 0x0001U, "S");
        store.finalize();
        const std::array<MetadataDirectorySelector, 2U> directories {{
            { a, "gpsifd" }, { b, "gpsifd" },
        }};
        MetadataQueryOptions options;
        options.scope.enabled = true;
        options.scope.directories = directories;
        options.scope.inherit_across_directories = true;
        options.max_semantic_work = 32U;
        MetadataConceptResolution gps = resolve_metadata_concept(
            store, MetadataConceptKind::Gps, options);
        const MetadataConceptCandidate* candidate = candidate_for(
            gps, latitude, MetadataConceptRole::Latitude);
        ASSERT_NE(candidate, nullptr);
        EXPECT_TRUE(has_source(*candidate, inherited_ref));
        EXPECT_FALSE(gps.semantic_work_limited);
        EXPECT_LE(gps.semantic_work_used, options.max_semantic_work);

        options.max_semantic_work = 3U;
        gps = resolve_metadata_concept(store, MetadataConceptKind::Gps,
                                       options);
        candidate = candidate_for(gps, latitude,
                                  MetadataConceptRole::Latitude);
        ASSERT_NE(candidate, nullptr);
        EXPECT_TRUE(gps.semantic_work_limited);
        EXPECT_EQ(gps.semantic_work_used, options.max_semantic_work);
        EXPECT_FALSE(has_source(*candidate, inherited_ref));
        EXPECT_EQ(gps.preferred_entry, kInvalidEntryId);
        EXPECT_FALSE(candidate->preferred);
    }

    TEST(MetadataCompanionScope,
         DirectOrientationAndGpsValuesUseSharedNumericBudget)
    {
        MetaStore store;
        const BlockId a = store.add_block({});
        const BlockId b = store.add_block({});
        const EntryId orientation = add_exif_u16(
            store, a, "ifd0", 0x0112U, 6U);
        const EntryId altitude_ref_a = add_exif_u16(
            store, a, "gpsifd", 0x0005U, 0U);
        const std::array<URational, 1U> altitude_value_a {{ { 10U, 1U } }};
        const EntryId altitude_a = add_exif_urational_array(
            store, a, "gpsifd", 0x0006U, altitude_value_a);
        (void)add_exif_u16(store, b, "gpsifd", 0x0005U, 0U);
        const std::array<URational, 1U> altitude_value_b {{ { 20U, 1U } }};
        const EntryId altitude_b = add_exif_urational_array(
            store, b, "gpsifd", 0x0006U, altitude_value_b);
        store.finalize();

        MetadataQueryOptions options;
        options.max_total_values = 1U;
        const MetadataConceptResult limited = resolve_metadata_concepts(
            store, options);
        EXPECT_EQ(limited.returned_numeric_values, 1U);
        EXPECT_GE(limited.total_numeric_values, 3U);
        EXPECT_TRUE(limited.numeric_values_limited);
        const MetadataConceptResolution* orientation_result = nullptr;
        const MetadataConceptResolution* gps_result = nullptr;
        for (const MetadataConceptResolution& resolution : limited.concepts) {
            if (resolution.kind == MetadataConceptKind::Orientation) {
                orientation_result = &resolution;
            } else if (resolution.kind == MetadataConceptKind::Gps) {
                gps_result = &resolution;
            }
        }
        ASSERT_NE(orientation_result, nullptr);
        ASSERT_NE(gps_result, nullptr);
        const MetadataConceptCandidate* orientation_candidate = candidate_for(
            *orientation_result, orientation, MetadataConceptRole::Orientation);
        const MetadataConceptCandidate* gps_candidate = candidate_for(
            *gps_result, altitude_a, MetadataConceptRole::Altitude);
        ASSERT_NE(orientation_candidate, nullptr);
        ASSERT_NE(gps_candidate, nullptr);
        EXPECT_EQ(gps_result->returned_numeric_values, 0U);
        EXPECT_EQ(gps_candidate->total_numeric_values, 1U);
        EXPECT_TRUE(gps_candidate->values.empty());
        EXPECT_EQ(gps_candidate->numeric_status,
                  MetadataQueryNumericStatus::Partial);
        EXPECT_TRUE(has_source(*gps_candidate, altitude_ref_a));
        EXPECT_NE(candidate_for(*gps_result, altitude_b,
                                MetadataConceptRole::Altitude),
                  nullptr);

        options.include_numeric_values = false;
        const MetadataConceptResult suppressed = resolve_metadata_concepts(
            store, options);
        EXPECT_EQ(suppressed.returned_numeric_values, 0U);
        EXPECT_GE(suppressed.total_numeric_values, 3U);
        EXPECT_TRUE(suppressed.numeric_values_limited);
    }

}  // namespace
}  // namespace openmeta
