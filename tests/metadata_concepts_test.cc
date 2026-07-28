// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_concepts.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static EntryId add_exif_u16(MetaStore* store, std::string_view ifd,
                                uint16_t tag, uint16_t value)
    {
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_u16(value);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_u32(MetaStore* store, std::string_view ifd,
                                uint16_t tag, uint32_t value)
    {
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_u32(value);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_urational(MetaStore* store, std::string_view ifd,
                                      uint16_t tag, uint32_t numer,
                                      uint32_t denom)
    {
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_urational(numer, denom);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_srational(MetaStore* store, std::string_view ifd,
                                      uint16_t tag, int32_t numer,
                                      int32_t denom)
    {
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_srational(numer, denom);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_text(MetaStore* store, std::string_view ifd,
                                 uint16_t tag, std::string_view value)
    {
        Entry entry;
        entry.key   = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value = make_text(store->arena(), value, TextEncoding::Ascii);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_urational_array(MetaStore* store,
                                            std::string_view ifd, uint16_t tag,
                                            std::span<const URational> values)
    {
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_urational_array(store->arena(), values);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_u32_array(MetaStore* store, std::string_view ifd,
                                      uint16_t tag,
                                      std::span<const uint32_t> values)
    {
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_u32_array(store->arena(), values);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_xmp_text(MetaStore* store, std::string_view ns,
                                std::string_view path, std::string_view value)
    {
        Entry entry;
        entry.key        = make_xmp_property_key(store->arena(), ns, path);
        entry.value      = make_text(store->arena(), value, TextEncoding::Utf8);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_iptc_text(MetaStore* store, uint16_t record,
                                 uint16_t dataset, std::string_view value)
    {
        Entry entry;
        entry.key   = make_iptc_dataset_key(record, dataset);
        entry.value = make_text(store->arena(), value, TextEncoding::Ascii);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_iptc_bytes(MetaStore* store, uint16_t record,
                                  uint16_t dataset,
                                  std::span<const std::byte> value)
    {
        Entry entry;
        entry.key        = make_iptc_dataset_key(record, dataset);
        entry.value      = make_bytes(store->arena(), value);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_iptc_wire_text(MetaStore* store, uint16_t record,
                                      uint16_t dataset, std::string_view value)
    {
        const std::span<const char> chars(value.data(), value.size());
        return add_iptc_bytes(store, record, dataset, std::as_bytes(chars));
    }

    static EntryId add_bmff_text(MetaStore* store, std::string_view field,
                                 std::string_view value)
    {
        Entry entry;
        entry.key        = make_bmff_field_key(store->arena(), field);
        entry.value      = make_text(store->arena(), value, TextEncoding::Utf8);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_bmff_u8(MetaStore* store, std::string_view field,
                               uint8_t value)
    {
        Entry entry;
        entry.key        = make_bmff_field_key(store->arena(), field);
        entry.value      = make_u8(value);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_icc_header_u32(MetaStore* store, uint32_t offset,
                                      uint32_t value)
    {
        Entry entry;
        entry.key        = make_icc_header_field_key(offset);
        entry.value      = make_u32(value);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static const MetadataConceptResolution*
    find_concept(const MetadataConceptResult& result, MetadataConceptKind kind)
    {
        for (size_t i = 0U; i < result.concepts.size(); ++i) {
            if (result.concepts[i].kind == kind) {
                return &result.concepts[i];
            }
        }
        return nullptr;
    }

    static const MetadataConceptCandidate*
    find_role(const MetadataConceptResolution& resolution,
              MetadataConceptRole role)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            if (resolution.candidates[i].role == role) {
                return &resolution.candidates[i];
            }
        }
        return nullptr;
    }

    static const MetadataConceptCandidate*
    find_role_shape(const MetadataConceptResolution& resolution,
                    MetadataConceptRole role, MetadataQueryValueShape shape)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = resolution.candidates[i];
            if (candidate.role == role && candidate.shape == shape) {
                return &candidate;
            }
        }
        return nullptr;
    }

    static const MetadataConceptCandidate*
    find_role_family(const MetadataConceptResolution& resolution,
                     MetadataConceptRole role,
                     MetadataConceptSourceFamily family)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = resolution.candidates[i];
            if (candidate.role == role && candidate.family == family) {
                return &candidate;
            }
        }
        return nullptr;
    }

    static const MetadataConceptCandidate*
    find_role_scope(const MetadataConceptResolution& resolution,
                    MetadataConceptRole role, std::string_view scope)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = resolution.candidates[i];
            if (candidate.role == role && candidate.location_scope == scope) {
                return &candidate;
            }
        }
        return nullptr;
    }

    static const MetadataConceptCandidate* find_role_family_scope_language(
        const MetadataConceptResolution& resolution, MetadataConceptRole role,
        MetadataConceptSourceFamily family, std::string_view scope,
        std::string_view language)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = resolution.candidates[i];
            if (candidate.role == role && candidate.family == family
                && candidate.location_scope == scope
                && candidate.language == language) {
                return &candidate;
            }
        }
        return nullptr;
    }

    static const MetadataConceptCandidate* find_role_family_value(
        const MetadataConceptResolution& resolution, MetadataConceptRole role,
        MetadataConceptSourceFamily family, std::string_view value_key)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = resolution.candidates[i];
            if (candidate.role == role && candidate.family == family
                && candidate.value_key == value_key) {
                return &candidate;
            }
        }
        return nullptr;
    }

    static const MetadataConceptCandidate* find_role_family_record_scope(
        const MetadataConceptResolution& resolution, MetadataConceptRole role,
        MetadataConceptSourceFamily family, std::string_view record_scope)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = resolution.candidates[i];
            if (candidate.role == role && candidate.family == family
                && candidate.record_scope == record_scope) {
                return &candidate;
            }
        }
        return nullptr;
    }

    static const MetadataConceptCandidate*
    find_record_role_scope(const MetadataConceptResolution& resolution,
                           MetadataConceptRecordKind record_kind,
                           MetadataConceptRole role,
                           std::string_view record_scope)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = resolution.candidates[i];
            if (candidate.record_kind == record_kind && candidate.role == role
                && candidate.record_scope == record_scope) {
                return &candidate;
            }
        }
        return nullptr;
    }

    static const MetadataConceptCandidate* find_preferred_role_scope_language(
        const MetadataConceptResolution& resolution, MetadataConceptRole role,
        std::string_view scope, std::string_view language)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = resolution.candidates[i];
            if (candidate.role == role && candidate.location_scope == scope
                && candidate.language == language && candidate.preferred) {
                return &candidate;
            }
        }
        return nullptr;
    }

    static bool contains_entry(const std::vector<EntryId>& entries,
                               EntryId entry_id) noexcept
    {
        for (size_t i = 0U; i < entries.size(); ++i) {
            if (entries[i] == entry_id) {
                return true;
            }
        }
        return false;
    }

    static const MetadataConceptCandidate*
    find_role_entries(const MetadataConceptResolution& resolution,
                      MetadataConceptRole role, EntryId first, EntryId second)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = resolution.candidates[i];
            if (candidate.role == role
                && contains_entry(candidate.source_entries, first)
                && contains_entry(candidate.source_entries, second)) {
                return &candidate;
            }
        }
        return nullptr;
    }

    TEST(MetadataConcepts, ResolvesCoreCrossFamilyConcepts)
    {
        MetaStore store;
        const EntryId exif_orientation = add_exif_u16(&store, "ifd0", 0x0112U,
                                                      6U);
        const EntryId xmp_orientation
            = add_xmp_text(&store, "http://ns.adobe.com/tiff/1.0/",
                           "tiff:Orientation", "8");

        const EntryId exif_created = add_exif_text(&store, "exififd", 0x9003U,
                                                   "2024:04:19 12:34:56");
        (void)add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/",
                           "xmp:CreateDate", "2024-04-19T12:34:56Z");
        const EntryId xmp_digitized
            = add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/",
                           "exif:DateTimeDigitized", "2024-04-19T12:35:01Z");
        (void)add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/",
                           "xmp:ModifyDate", "2024-04-20T01:02:03Z");
        (void)add_iptc_text(&store, 2U, 55U, "20240419");
        (void)add_iptc_text(&store, 2U, 60U, "123456+0000");

        const EntryId exif_colorspace = add_exif_u16(&store, "exififd", 0xA001U,
                                                     1U);
        const EntryId icc_colorspace  = add_icc_header_u32(&store, 16U,
                                                           0x52474220U);
        (void)add_xmp_text(&store, "http://ns.adobe.com/photoshop/1.0/",
                           "photoshop:ICCProfile", "sRGB IEC61966-2.1");
        const std::array<uint32_t, 9> color_matrix_values = {
            1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
        };
        const EntryId color_matrix = add_exif_u32_array(
            &store, "ifd0", 0xC621U,
            std::span<const uint32_t>(color_matrix_values.data(),
                                      color_matrix_values.size()));
        const std::array<uint32_t, 3> wb_neutral_values = { 1U, 2U, 3U };
        const std::array<uint32_t, 3> wb_analog_values  = { 10U, 20U, 30U };
        const EntryId wb_neutral                        = add_exif_u32_array(
            &store, "ifd0", 0xC628U,
            std::span<const uint32_t>(wb_neutral_values.data(),
                                                             wb_neutral_values.size()));
        const EntryId wb_analog = add_exif_u32_array(
            &store, "ifd0", 0xC627U,
            std::span<const uint32_t>(wb_analog_values.data(),
                                      wb_analog_values.size()));

        (void)add_exif_text(&store, "gpsifd", 0x0001U, "N");
        const std::array<URational, 3> lat = {
            URational { 41U, 1U },
            URational { 24U, 1U },
            URational { 30U, 1U },
        };
        const EntryId latitude
            = add_exif_urational_array(&store, "gpsifd", 0x0002U,
                                       std::span<const URational>(lat.data(),
                                                                  lat.size()));
        (void)add_exif_text(&store, "gpsifd", 0x0003U, "E");
        const std::array<URational, 3> lon = {
            URational { 2U, 1U },
            URational { 9U, 1U },
            URational { 0U, 1U },
        };
        const EntryId longitude
            = add_exif_urational_array(&store, "gpsifd", 0x0004U,
                                       std::span<const URational>(lon.data(),
                                                                  lon.size()));
        (void)add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/",
                           "exif:GPSLatitude", "41,24.500N");
        const EntryId altitude_ref = add_exif_u16(&store, "gpsifd", 0x0005U,
                                                  1U);
        const std::array<URational, 1> alt = {
            URational { 100U, 1U },
        };
        const EntryId altitude
            = add_exif_urational_array(&store, "gpsifd", 0x0006U,
                                       std::span<const URational>(alt.data(),
                                                                  alt.size()));
        const EntryId gps_date = add_exif_text(&store, "gpsifd", 0x001DU,
                                               "2024:04:19");
        const std::array<URational, 3> gps_time = {
            URational { 12U, 1U },
            URational { 34U, 1U },
            URational { 56U, 1U },
        };
        const EntryId gps_time_id = add_exif_urational_array(
            &store, "gpsifd", 0x0007U,
            std::span<const URational>(gps_time.data(), gps_time.size()));
        const std::array<uint32_t, 2> crop_origin_values = { 12U, 34U };
        const std::array<uint32_t, 2> crop_size_values   = { 4000U, 3000U };
        const std::span<const uint32_t> crop_origin_span(
            crop_origin_values.data(), crop_origin_values.size());
        const std::span<const uint32_t> crop_size_span(crop_size_values.data(),
                                                       crop_size_values.size());
        const EntryId crop_origin = add_exif_u32_array(&store, "ifd0", 0xC61FU,
                                                       crop_origin_span);
        const EntryId crop_size   = add_exif_u32_array(&store, "ifd0", 0xC620U,
                                                       crop_size_span);
        const std::array<uint32_t, 4> active_area_values = {
            10U,
            20U,
            3010U,
            4020U,
        };
        const EntryId active_area = add_exif_u32_array(
            &store, "ifd0", 0xC68DU,
            std::span<const uint32_t>(active_area_values.data(),
                                      active_area_values.size()));
        const EntryId border_padding
            = add_xmp_text(&store, "http://example.invalid/aux/1.0/",
                           "aux:SensorBorderPadding", "64 32 168 128");
        const EntryId lens_distort
            = add_exif_u32(&store, "mk_nikon_distortinfo", 0x0001U, 7U);
        const EntryId lens_vignette = add_exif_u32(&store, "mk_nikon_vignette",
                                                   0x0001U, 3U);
        const std::array<uint32_t, 2> linearization_values = { 0U, 65535U };
        const EntryId black_level = add_exif_u32(&store, "ifd0", 0xC61AU, 512U);
        const EntryId linearization = add_exif_u32_array(
            &store, "ifd0", 0xC618U,
            std::span<const uint32_t>(linearization_values.data(),
                                      linearization_values.size()));
        const std::array<uint32_t, 4> raw_id_values = { 1U, 2U, 3U, 4U };
        const EntryId raw_id                        = add_exif_u32_array(
            &store, "ifd0", 0xC65DU,
            std::span<const uint32_t>(raw_id_values.data(),
                                                             raw_id_values.size()));
        const EntryId raw_name = add_exif_text(&store, "ifd0", 0xC68BU,
                                               "source.raw");
        const EntryId source_processing
            = add_exif_u32(&store, "mk_google_shotlogdata", 0x0001U, 7U);
        store.finalize();

        const MetadataConceptResult result = resolve_metadata_concepts(store);

        const MetadataConceptResolution* orientation
            = find_concept(result, MetadataConceptKind::Orientation);
        ASSERT_NE(orientation, nullptr);
        EXPECT_TRUE(orientation->found);
        EXPECT_TRUE(orientation->conflict);
        EXPECT_EQ(orientation->preferred_entry, exif_orientation);
        ASSERT_NE(find_role_family(*orientation,
                                   MetadataConceptRole::Orientation,
                                   MetadataConceptSourceFamily::Exif),
                  nullptr);
        ASSERT_NE(find_role_family(*orientation,
                                   MetadataConceptRole::Orientation,
                                   MetadataConceptSourceFamily::Xmp),
                  nullptr);
        EXPECT_NE(xmp_orientation, kInvalidEntryId);

        const MetadataConceptResolution* datetime
            = find_concept(result, MetadataConceptKind::DateTime);
        ASSERT_NE(datetime, nullptr);
        EXPECT_TRUE(datetime->found);
        EXPECT_FALSE(datetime->conflict);
        EXPECT_EQ(datetime->preferred_entry, exif_created);
        const MetadataConceptCandidate* created
            = find_role(*datetime, MetadataConceptRole::Created);
        ASSERT_NE(created, nullptr);
        EXPECT_TRUE(created->has_date_time);
        EXPECT_TRUE(created->date_time_has_time);
        EXPECT_EQ(created->date_time_year, 2024);
        EXPECT_EQ(created->date_time_month, 4U);
        EXPECT_EQ(created->date_time_day, 19U);
        EXPECT_EQ(created->date_time_precision,
                  MetadataConceptDateTimePrecision::DateTime);
        EXPECT_EQ(created->date_time_zone, MetadataConceptTimeZoneKind::Local);
        const MetadataConceptCandidate* xmp_created
            = find_role_family(*datetime, MetadataConceptRole::Created,
                               MetadataConceptSourceFamily::Xmp);
        ASSERT_NE(xmp_created, nullptr);
        EXPECT_TRUE(xmp_created->date_time_has_utc_offset);
        EXPECT_EQ(xmp_created->date_time_utc_offset_min, 0);
        EXPECT_EQ(xmp_created->date_time_zone,
                  MetadataConceptTimeZoneKind::Utc);
        ASSERT_NE(find_role(*datetime, MetadataConceptRole::Modified), nullptr);
        const MetadataConceptCandidate* digitized
            = find_role(*datetime, MetadataConceptRole::Digitized);
        ASSERT_NE(digitized, nullptr);
        EXPECT_EQ(digitized->entry_id, xmp_digitized);
        EXPECT_TRUE(digitized->has_date_time);
        EXPECT_TRUE(digitized->date_time_has_time);
        const MetadataConceptCandidate* iptc_created
            = find_role(*datetime, MetadataConceptRole::DateCreated);
        ASSERT_NE(iptc_created, nullptr);
        EXPECT_TRUE(iptc_created->has_date_time);
        EXPECT_TRUE(iptc_created->date_time_has_time);
        EXPECT_EQ(iptc_created->date_time_zone,
                  MetadataConceptTimeZoneKind::Utc);
        const MetadataConceptCandidate* iptc_promoted_created
            = find_role_family(*datetime, MetadataConceptRole::Created,
                               MetadataConceptSourceFamily::Iptc);
        ASSERT_NE(iptc_promoted_created, nullptr);
        EXPECT_TRUE(iptc_promoted_created->has_date_time);
        EXPECT_TRUE(iptc_promoted_created->date_time_has_time);
        EXPECT_EQ(iptc_promoted_created->date_time_zone,
                  MetadataConceptTimeZoneKind::Utc);

        const MetadataConceptResolution* color
            = find_concept(result, MetadataConceptKind::ColorProfile);
        ASSERT_NE(color, nullptr);
        EXPECT_TRUE(color->found);
        ASSERT_NE(find_role_family(*color, MetadataConceptRole::ColorSpace,
                                   MetadataConceptSourceFamily::Exif),
                  nullptr);
        ASSERT_NE(find_role_family(*color, MetadataConceptRole::ColorSpace,
                                   MetadataConceptSourceFamily::Icc),
                  nullptr);
        const MetadataConceptCandidate* matrix_candidate
            = find_role(*color, MetadataConceptRole::ColorMatrix);
        ASSERT_NE(matrix_candidate, nullptr);
        EXPECT_EQ(matrix_candidate->shape, MetadataQueryValueShape::Matrix3x3);
        ASSERT_TRUE(matrix_candidate->has_values);
        ASSERT_EQ(matrix_candidate->values.size(), 9U);
        EXPECT_DOUBLE_EQ(matrix_candidate->values[0], 1.0);
        EXPECT_DOUBLE_EQ(matrix_candidate->values[4], 1.0);
        EXPECT_DOUBLE_EQ(matrix_candidate->values[8], 1.0);
        EXPECT_TRUE(
            contains_entry(matrix_candidate->source_entries, color_matrix));
        const MetadataConceptCandidate* wb_candidate
            = find_role_shape(*color, MetadataConceptRole::WhiteBalance,
                              MetadataQueryValueShape::VectorSet);
        ASSERT_NE(wb_candidate, nullptr);
        EXPECT_EQ(wb_candidate->shape, MetadataQueryValueShape::VectorSet);
        ASSERT_TRUE(wb_candidate->has_values);
        ASSERT_EQ(wb_candidate->values.size(), 6U);
        EXPECT_DOUBLE_EQ(wb_candidate->values[0], 1.0);
        EXPECT_DOUBLE_EQ(wb_candidate->values[3], 10.0);
        EXPECT_TRUE(contains_entry(wb_candidate->source_entries, wb_neutral));
        EXPECT_TRUE(contains_entry(wb_candidate->source_entries, wb_analog));
        EXPECT_NE(exif_colorspace, kInvalidEntryId);
        EXPECT_NE(icc_colorspace, kInvalidEntryId);

        const MetadataConceptResolution* gps
            = find_concept(result, MetadataConceptKind::Gps);
        ASSERT_NE(gps, nullptr);
        EXPECT_TRUE(gps->found);
        const MetadataConceptCandidate* lat_candidate
            = find_role(*gps, MetadataConceptRole::Latitude);
        ASSERT_NE(lat_candidate, nullptr);
        ASSERT_TRUE(lat_candidate->has_numeric);
        EXPECT_NEAR(lat_candidate->numeric[0], 41.408333333, 0.000001);
        const MetadataConceptCandidate* lon_candidate
            = find_role(*gps, MetadataConceptRole::Longitude);
        ASSERT_NE(lon_candidate, nullptr);
        ASSERT_TRUE(lon_candidate->has_numeric);
        EXPECT_NEAR(lon_candidate->numeric[0], 2.15, 0.000001);
        const MetadataConceptCandidate* altitude_candidate
            = find_role(*gps, MetadataConceptRole::Altitude);
        ASSERT_NE(altitude_candidate, nullptr);
        ASSERT_TRUE(altitude_candidate->has_numeric);
        EXPECT_NEAR(altitude_candidate->numeric[0], -100.0, 0.000001);
        EXPECT_TRUE(altitude_candidate->has_gps_altitude_reference);
        EXPECT_TRUE(altitude_candidate->gps_altitude_below_sea_level);
        EXPECT_EQ(altitude_candidate->gps_altitude_reference_code, 1U);
        EXPECT_TRUE(
            contains_entry(altitude_candidate->source_entries, altitude_ref));
        EXPECT_TRUE(
            contains_entry(altitude_candidate->source_entries, altitude));
        const MetadataConceptCandidate* gps_timestamp
            = find_role(*gps, MetadataConceptRole::Timestamp);
        ASSERT_NE(gps_timestamp, nullptr);
        EXPECT_TRUE(gps_timestamp->has_date_time);
        EXPECT_TRUE(gps_timestamp->date_time_has_time);
        EXPECT_TRUE(gps_timestamp->date_time_has_utc_offset);
        EXPECT_EQ(gps_timestamp->date_time_hour, 12U);
        EXPECT_EQ(gps_timestamp->date_time_minute, 34U);
        EXPECT_EQ(gps_timestamp->date_time_second, 56U);
        EXPECT_EQ(gps_timestamp->date_time_precision,
                  MetadataConceptDateTimePrecision::DateTime);
        EXPECT_EQ(gps_timestamp->date_time_zone,
                  MetadataConceptTimeZoneKind::Utc);
        EXPECT_TRUE(contains_entry(gps_timestamp->source_entries, gps_date));
        EXPECT_TRUE(contains_entry(gps_timestamp->source_entries, gps_time_id));
        EXPECT_EQ(gps->preferred_entry, latitude);
        EXPECT_NE(longitude, kInvalidEntryId);

        const MetadataConceptResolution* geometry
            = find_concept(result, MetadataConceptKind::Geometry);
        ASSERT_NE(geometry, nullptr);
        EXPECT_TRUE(geometry->found);
        const MetadataConceptCandidate* crop
            = find_role(*geometry, MetadataConceptRole::Crop);
        ASSERT_NE(crop, nullptr);
        ASSERT_TRUE(crop->has_origin);
        EXPECT_DOUBLE_EQ(crop->origin[0], 12.0);
        EXPECT_DOUBLE_EQ(crop->origin[1], 34.0);
        ASSERT_TRUE(crop->has_size);
        EXPECT_DOUBLE_EQ(crop->size[0], 4000.0);
        EXPECT_DOUBLE_EQ(crop->size[1], 3000.0);
        ASSERT_TRUE(crop->has_rect);
        EXPECT_DOUBLE_EQ(crop->rect[0], 12.0);
        EXPECT_DOUBLE_EQ(crop->rect[1], 34.0);
        EXPECT_DOUBLE_EQ(crop->rect[2], 4000.0);
        EXPECT_DOUBLE_EQ(crop->rect[3], 3000.0);
        EXPECT_TRUE(contains_entry(crop->source_entries, crop_origin));
        EXPECT_TRUE(contains_entry(crop->source_entries, crop_size));

        const MetadataConceptCandidate* active
            = find_role(*geometry, MetadataConceptRole::ActiveArea);
        ASSERT_NE(active, nullptr);
        ASSERT_TRUE(active->has_rect);
        EXPECT_DOUBLE_EQ(active->rect[0], 20.0);
        EXPECT_DOUBLE_EQ(active->rect[1], 10.0);
        EXPECT_DOUBLE_EQ(active->rect[2], 4000.0);
        EXPECT_DOUBLE_EQ(active->rect[3], 3000.0);
        EXPECT_TRUE(contains_entry(active->source_entries, active_area));

        const MetadataConceptCandidate* border
            = find_role(*geometry, MetadataConceptRole::Border);
        ASSERT_NE(border, nullptr);
        ASSERT_TRUE(border->has_margins);
        EXPECT_DOUBLE_EQ(border->margins[0], 64.0);
        EXPECT_DOUBLE_EQ(border->margins[1], 32.0);
        EXPECT_DOUBLE_EQ(border->margins[2], 168.0);
        EXPECT_DOUBLE_EQ(border->margins[3], 128.0);
        EXPECT_TRUE(contains_entry(border->source_entries, border_padding));

        const MetadataConceptResolution* lens
            = find_concept(result, MetadataConceptKind::LensCorrection);
        ASSERT_NE(lens, nullptr);
        EXPECT_TRUE(lens->found);
        const MetadataConceptCandidate* lens_candidate
            = find_role_shape(*lens, MetadataConceptRole::LensCorrection,
                              MetadataQueryValueShape::Table);
        ASSERT_NE(lens_candidate, nullptr);
        EXPECT_EQ(lens_candidate->shape, MetadataQueryValueShape::Table);
        ASSERT_TRUE(lens_candidate->has_values);
        ASSERT_EQ(lens_candidate->values.size(), 2U);
        EXPECT_DOUBLE_EQ(lens_candidate->values[0], 7.0);
        EXPECT_DOUBLE_EQ(lens_candidate->values[1], 3.0);
        EXPECT_TRUE(
            contains_entry(lens_candidate->source_entries, lens_distort));
        EXPECT_TRUE(
            contains_entry(lens_candidate->source_entries, lens_vignette));

        const MetadataConceptResolution* raw
            = find_concept(result, MetadataConceptKind::RawProcessing);
        ASSERT_NE(raw, nullptr);
        EXPECT_TRUE(raw->found);
        const MetadataConceptCandidate* black
            = find_role(*raw, MetadataConceptRole::BlackLevel);
        ASSERT_NE(black, nullptr);
        ASSERT_TRUE(black->has_values);
        EXPECT_DOUBLE_EQ(black->values[0], 512.0);
        EXPECT_TRUE(contains_entry(black->source_entries, black_level));
        const MetadataConceptCandidate* linear
            = find_role(*raw, MetadataConceptRole::RawValueCurve);
        ASSERT_NE(linear, nullptr);
        ASSERT_TRUE(linear->has_values);
        EXPECT_DOUBLE_EQ(linear->values[1], 65535.0);
        EXPECT_TRUE(contains_entry(linear->source_entries, linearization));
        const MetadataConceptCandidate* storage
            = find_role_shape(*raw, MetadataConceptRole::RawStorage,
                              MetadataQueryValueShape::Table);
        ASSERT_NE(storage, nullptr);
        EXPECT_EQ(storage->shape, MetadataQueryValueShape::Table);
        EXPECT_TRUE(contains_entry(storage->source_entries, raw_id));
        EXPECT_TRUE(contains_entry(storage->source_entries, raw_name));
        const MetadataConceptCandidate* source
            = find_role(*raw, MetadataConceptRole::ComputationalProcessing);
        ASSERT_NE(source, nullptr);
        ASSERT_TRUE(source->has_values);
        EXPECT_DOUBLE_EQ(source->values[0], 7.0);
        EXPECT_TRUE(contains_entry(source->source_entries, source_processing));
    }

    TEST(MetadataConcepts, ResolvesFujifilmRafRawCropAsTargetOwnedGeometry)
    {
        MetaStore store;
        const std::array<uint32_t, 2> full_size = { 4032U, 3024U };
        const std::array<uint32_t, 2> top_left  = { 16U, 8U };
        const std::array<uint32_t, 2> crop_size = { 4000U, 3000U };
        const EntryId full_id
            = add_exif_u32_array(&store, "raf_0", 0x0100U,
                                 std::span<const uint32_t>(full_size.data(),
                                                           full_size.size()));
        const EntryId top_left_id
            = add_exif_u32_array(&store, "raf_0", 0x0110U,
                                 std::span<const uint32_t>(top_left.data(),
                                                           top_left.size()));
        const EntryId size_id
            = add_exif_u32_array(&store, "raf_0", 0x0111U,
                                 std::span<const uint32_t>(crop_size.data(),
                                                           crop_size.size()));
        store.finalize();

        const MetadataConceptResult result = resolve_metadata_concepts(store);

        const MetadataConceptResolution* geometry
            = find_concept(result, MetadataConceptKind::Geometry);
        ASSERT_NE(geometry, nullptr);
        EXPECT_TRUE(geometry->found);
        const MetadataConceptCandidate* active
            = find_role(*geometry, MetadataConceptRole::ActiveArea);
        ASSERT_NE(active, nullptr);
        EXPECT_EQ(active->shape, MetadataQueryValueShape::Rect);
        EXPECT_EQ(active->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        EXPECT_TRUE(active->compatible_file_safe);
        EXPECT_FALSE(active->rendered_image_safe);
        EXPECT_TRUE(active->requires_target_image_spec);
        EXPECT_FALSE(active->source_bound);
        EXPECT_TRUE(contains_entry(active->source_entries, full_id));
        EXPECT_TRUE(contains_entry(active->source_entries, top_left_id));
        EXPECT_TRUE(contains_entry(active->source_entries, size_id));
        ASSERT_TRUE(active->has_rect);
        EXPECT_DOUBLE_EQ(active->rect[0], 16.0);
        EXPECT_DOUBLE_EQ(active->rect[1], 8.0);
        EXPECT_DOUBLE_EQ(active->rect[2], 4000.0);
        EXPECT_DOUBLE_EQ(active->rect[3], 3000.0);
        ASSERT_TRUE(active->has_margins);
        EXPECT_DOUBLE_EQ(active->margins[0], 16.0);
        EXPECT_DOUBLE_EQ(active->margins[1], 8.0);
        EXPECT_DOUBLE_EQ(active->margins[2], 16.0);
        EXPECT_DOUBLE_EQ(active->margins[3], 16.0);
    }

    TEST(MetadataConcepts, ResolvesVendorRawGeometryAsTargetOwnedConcepts)
    {
        MetaStore store;
        const EntryId canon_width
            = add_exif_u32(&store, "mk_canon_aspectinfo_0", 0x0001U, 4000U);
        const EntryId canon_height
            = add_exif_u32(&store, "mk_canon_aspectinfo_0", 0x0002U, 3000U);
        const EntryId canon_left = add_exif_u32(&store, "mk_canon_aspectinfo_0",
                                                0x0003U, 12U);
        const EntryId canon_top  = add_exif_u32(&store, "mk_canon_aspectinfo_0",
                                                0x0004U, 8U);

        const EntryId margin_left  = add_exif_u32(&store, "mk_canon_cropinfo_0",
                                                  0x0000U, 16U);
        const EntryId margin_right = add_exif_u32(&store, "mk_canon_cropinfo_0",
                                                  0x0001U, 20U);
        const EntryId margin_top   = add_exif_u32(&store, "mk_canon_cropinfo_0",
                                                  0x0002U, 4U);
        const EntryId margin_bottom
            = add_exif_u32(&store, "mk_canon_cropinfo_0", 0x0003U, 6U);

        const EntryId nikon_left
            = add_exif_u32(&store, "mk_nikoncapture_cropdata_0", 0x001EU, 10U);
        const EntryId nikon_top
            = add_exif_u32(&store, "mk_nikoncapture_cropdata_0", 0x0026U, 20U);
        const EntryId nikon_right  = add_exif_u32(&store,
                                                  "mk_nikoncapture_cropdata_0",
                                                  0x002EU, 4010U);
        const EntryId nikon_bottom = add_exif_u32(&store,
                                                  "mk_nikoncapture_cropdata_0",
                                                  0x0036U, 3020U);

        const EntryId sony_left   = add_exif_u32(&store, "mk_sony_panorama_0",
                                                 0x0004U, 100U);
        const EntryId sony_top    = add_exif_u32(&store, "mk_sony_panorama_0",
                                                 0x0005U, 20U);
        const EntryId sony_right  = add_exif_u32(&store, "mk_sony_panorama_0",
                                                 0x0006U, 120U);
        const EntryId sony_bottom = add_exif_u32(&store, "mk_sony_panorama_0",
                                                 0x0007U, 30U);
        store.finalize();

        const MetadataConceptResult result = resolve_metadata_concepts(store);

        const MetadataConceptResolution* geometry
            = find_concept(result, MetadataConceptKind::Geometry);
        ASSERT_NE(geometry, nullptr);
        EXPECT_TRUE(geometry->found);

        const MetadataConceptCandidate* canon_crop
            = find_role_entries(*geometry, MetadataConceptRole::Crop,
                                canon_left, canon_width);
        ASSERT_NE(canon_crop, nullptr);
        EXPECT_EQ(canon_crop->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        EXPECT_TRUE(canon_crop->requires_target_image_spec);
        ASSERT_TRUE(canon_crop->has_rect);
        EXPECT_DOUBLE_EQ(canon_crop->rect[0], 12.0);
        EXPECT_DOUBLE_EQ(canon_crop->rect[1], 8.0);
        EXPECT_DOUBLE_EQ(canon_crop->rect[2], 4000.0);
        EXPECT_DOUBLE_EQ(canon_crop->rect[3], 3000.0);
        EXPECT_TRUE(contains_entry(canon_crop->source_entries, canon_height));
        EXPECT_TRUE(contains_entry(canon_crop->source_entries, canon_top));

        const MetadataConceptCandidate* canon_border
            = find_role_entries(*geometry, MetadataConceptRole::Border,
                                margin_left, margin_right);
        ASSERT_NE(canon_border, nullptr);
        EXPECT_EQ(canon_border->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        ASSERT_TRUE(canon_border->has_margins);
        EXPECT_DOUBLE_EQ(canon_border->margins[0], 16.0);
        EXPECT_DOUBLE_EQ(canon_border->margins[1], 4.0);
        EXPECT_DOUBLE_EQ(canon_border->margins[2], 20.0);
        EXPECT_DOUBLE_EQ(canon_border->margins[3], 6.0);
        EXPECT_TRUE(contains_entry(canon_border->source_entries, margin_top));
        EXPECT_TRUE(
            contains_entry(canon_border->source_entries, margin_bottom));

        const MetadataConceptCandidate* nikon_crop
            = find_role_entries(*geometry, MetadataConceptRole::Crop,
                                nikon_left, nikon_right);
        ASSERT_NE(nikon_crop, nullptr);
        EXPECT_EQ(nikon_crop->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        ASSERT_TRUE(nikon_crop->has_rect);
        EXPECT_DOUBLE_EQ(nikon_crop->rect[0], 10.0);
        EXPECT_DOUBLE_EQ(nikon_crop->rect[1], 20.0);
        EXPECT_DOUBLE_EQ(nikon_crop->rect[2], 4000.0);
        EXPECT_DOUBLE_EQ(nikon_crop->rect[3], 3000.0);
        EXPECT_TRUE(contains_entry(nikon_crop->source_entries, nikon_top));
        EXPECT_TRUE(contains_entry(nikon_crop->source_entries, nikon_bottom));

        const MetadataConceptCandidate* sony_border
            = find_role_entries(*geometry, MetadataConceptRole::Border,
                                sony_left, sony_right);
        ASSERT_NE(sony_border, nullptr);
        EXPECT_EQ(sony_border->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        ASSERT_TRUE(sony_border->has_margins);
        EXPECT_DOUBLE_EQ(sony_border->margins[0], 100.0);
        EXPECT_DOUBLE_EQ(sony_border->margins[1], 20.0);
        EXPECT_DOUBLE_EQ(sony_border->margins[2], 120.0);
        EXPECT_DOUBLE_EQ(sony_border->margins[3], 30.0);
        EXPECT_TRUE(contains_entry(sony_border->source_entries, sony_top));
        EXPECT_TRUE(contains_entry(sony_border->source_entries, sony_bottom));
    }

    TEST(MetadataConcepts, ResolvesGroupedVendorRecordsForInspectionHints)
    {
        MetaStore store;
        const std::array<uint32_t, 9> color_matrix_a = {
            1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
        };
        const std::array<uint32_t, 9> color_matrix_b = {
            2U, 0U, 0U, 0U, 2U, 0U, 0U, 0U, 2U,
        };
        const EntryId matrix_a = add_exif_u32_array(
            &store, "mk_phaseone0", 0x0106U,
            std::span<const uint32_t>(color_matrix_a.data(),
                                      color_matrix_a.size()));
        const EntryId matrix_b = add_exif_u32_array(
            &store, "mk_phaseone0", 0x0226U,
            std::span<const uint32_t>(color_matrix_b.data(),
                                      color_matrix_b.size()));

        const std::array<uint32_t, 4> daylight = { 110U, 256U, 256U, 144U };
        const std::array<uint32_t, 4> cloudy   = { 120U, 256U, 256U, 136U };
        const EntryId daylight_id
            = add_exif_u32_array(&store, "mk_nikon_colorbalancec_0", 0x0114U,
                                 std::span<const uint32_t>(daylight.data(),
                                                           daylight.size()));
        const EntryId cloudy_id
            = add_exif_u32_array(&store, "mk_nikon_colorbalancec_0", 0x0115U,
                                 std::span<const uint32_t>(cloudy.data(),
                                                           cloudy.size()));

        const EntryId distort_id  = add_exif_u32(&store, "mk_nikon_distortinfo",
                                                 0x0001U, 7U);
        const EntryId vignette_id = add_exif_u32(&store, "mk_nikon_vignette",
                                                 0x0001U, 3U);

        const EntryId source_a = add_exif_u32(&store, "mk_google_shotlogdata",
                                              0x0001U, 1U);
        const EntryId source_b = add_exif_u32(&store, "mk_google_shotlogdata",
                                              0x0002U, 2U);
        store.finalize();

        const MetadataConceptResult result = resolve_metadata_concepts(store);

        const MetadataConceptResolution* color
            = find_concept(result, MetadataConceptKind::ColorProfile);
        ASSERT_NE(color, nullptr);
        const MetadataConceptCandidate* matrix
            = find_role_shape(*color, MetadataConceptRole::ColorMatrix,
                              MetadataQueryValueShape::MatrixSet);
        ASSERT_NE(matrix, nullptr);
        EXPECT_EQ(matrix->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(matrix->compatible_file_safe);
        EXPECT_FALSE(matrix->rendered_image_safe);
        EXPECT_TRUE(matrix->source_bound);
        EXPECT_TRUE(contains_entry(matrix->source_entries, matrix_a));
        EXPECT_TRUE(contains_entry(matrix->source_entries, matrix_b));
        ASSERT_TRUE(matrix->has_values);
        ASSERT_EQ(matrix->values.size(), 18U);
        EXPECT_DOUBLE_EQ(matrix->values[0], 1.0);
        EXPECT_DOUBLE_EQ(matrix->values[9], 2.0);

        const MetadataConceptCandidate* wb
            = find_role_shape(*color, MetadataConceptRole::WhiteBalance,
                              MetadataQueryValueShape::VectorSet);
        ASSERT_NE(wb, nullptr);
        EXPECT_EQ(wb->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(wb->compatible_file_safe);
        EXPECT_FALSE(wb->rendered_image_safe);
        EXPECT_TRUE(wb->source_bound);
        EXPECT_TRUE(contains_entry(wb->source_entries, daylight_id));
        EXPECT_TRUE(contains_entry(wb->source_entries, cloudy_id));
        ASSERT_TRUE(wb->has_values);
        ASSERT_EQ(wb->values.size(), 8U);
        EXPECT_DOUBLE_EQ(wb->values[0], 110.0);
        EXPECT_DOUBLE_EQ(wb->values[4], 120.0);

        const MetadataConceptResolution* lens
            = find_concept(result, MetadataConceptKind::LensCorrection);
        ASSERT_NE(lens, nullptr);
        const MetadataConceptCandidate* lens_table
            = find_role_shape(*lens, MetadataConceptRole::LensCorrection,
                              MetadataQueryValueShape::Table);
        ASSERT_NE(lens_table, nullptr);
        EXPECT_EQ(lens_table->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(lens_table->compatible_file_safe);
        EXPECT_FALSE(lens_table->rendered_image_safe);
        EXPECT_TRUE(lens_table->source_bound);
        EXPECT_TRUE(contains_entry(lens_table->source_entries, distort_id));
        EXPECT_TRUE(contains_entry(lens_table->source_entries, vignette_id));

        const MetadataConceptResolution* raw
            = find_concept(result, MetadataConceptKind::RawProcessing);
        ASSERT_NE(raw, nullptr);
        const MetadataConceptCandidate* source
            = find_role_entries(*raw,
                                MetadataConceptRole::ComputationalProcessing,
                                source_a, source_b);
        ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->shape, MetadataQueryValueShape::Table);
        EXPECT_EQ(source->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(source->source_bound);
        EXPECT_TRUE(contains_entry(source->source_entries, source_a));
        EXPECT_TRUE(contains_entry(source->source_entries, source_b));
        ASSERT_TRUE(source->has_values);
        ASSERT_EQ(source->values.size(), 2U);
        EXPECT_DOUBLE_EQ(source->values[0], 1.0);
        EXPECT_DOUBLE_EQ(source->values[1], 2.0);
    }

    TEST(MetadataConcepts, ResolvesLongTailVendorAliasGroupsForSafetyHints)
    {
        MetaStore store;
        const std::array<uint32_t, 9> matrix_a = {
            1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
        };
        const std::array<uint32_t, 9> matrix_b = {
            2U, 0U, 0U, 0U, 2U, 0U, 0U, 0U, 2U,
        };
        const EntryId matrix_a_id
            = add_exif_u32_array(&store, "mk_samsung_type2_0", 0xA030U,
                                 std::span<const uint32_t>(matrix_a.data(),
                                                           matrix_a.size()));
        const EntryId matrix_b_id
            = add_exif_u32_array(&store, "mk_samsung_type2_0", 0xA031U,
                                 std::span<const uint32_t>(matrix_b.data(),
                                                           matrix_b.size()));

        const std::array<uint32_t, 4> wb_a = { 110U, 256U, 256U, 144U };
        const std::array<uint32_t, 4> wb_b = { 120U, 256U, 256U, 136U };
        const EntryId wb_a_id
            = add_exif_u32_array(&store, "mk_samsung_type2_0", 0xA021U,
                                 std::span<const uint32_t>(wb_a.data(),
                                                           wb_a.size()));
        const EntryId wb_b_id
            = add_exif_u32_array(&store, "mk_samsung_type2_0", 0xA022U,
                                 std::span<const uint32_t>(wb_b.data(),
                                                           wb_b.size()));

        const EntryId lens_a  = add_exif_u32(&store, "mk_samsung_type2_0",
                                             0xA052U, 7U);
        const EntryId lens_b  = add_exif_u32(&store, "mk_samsung_type2_0",
                                             0xA053U, 3U);
        const EntryId style_a = add_exif_u32(&store, "mk_sony0", 0xB020U, 1U);
        const EntryId style_b = add_exif_u32(&store, "mk_sony_camerasettings_0",
                                             0x001AU, 2U);
        store.finalize();

        const MetadataConceptResult result = resolve_metadata_concepts(store);

        const MetadataConceptResolution* color
            = find_concept(result, MetadataConceptKind::ColorProfile);
        ASSERT_NE(color, nullptr);
        const MetadataConceptCandidate* matrix
            = find_role_shape(*color, MetadataConceptRole::ColorMatrix,
                              MetadataQueryValueShape::MatrixSet);
        ASSERT_NE(matrix, nullptr);
        EXPECT_EQ(matrix->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(matrix->source_bound);
        EXPECT_FALSE(matrix->rendered_image_safe);
        EXPECT_TRUE(contains_entry(matrix->source_entries, matrix_a_id));
        EXPECT_TRUE(contains_entry(matrix->source_entries, matrix_b_id));

        const MetadataConceptCandidate* wb
            = find_role_shape(*color, MetadataConceptRole::WhiteBalance,
                              MetadataQueryValueShape::VectorSet);
        ASSERT_NE(wb, nullptr);
        EXPECT_EQ(wb->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(wb->source_bound);
        EXPECT_FALSE(wb->rendered_image_safe);
        EXPECT_TRUE(contains_entry(wb->source_entries, wb_a_id));
        EXPECT_TRUE(contains_entry(wb->source_entries, wb_b_id));

        const MetadataConceptResolution* lens
            = find_concept(result, MetadataConceptKind::LensCorrection);
        ASSERT_NE(lens, nullptr);
        const MetadataConceptCandidate* lens_table
            = find_role_shape(*lens, MetadataConceptRole::LensCorrection,
                              MetadataQueryValueShape::Table);
        ASSERT_NE(lens_table, nullptr);
        EXPECT_EQ(lens_table->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(lens_table->source_bound);
        EXPECT_FALSE(lens_table->rendered_image_safe);
        EXPECT_TRUE(contains_entry(lens_table->source_entries, lens_a));
        EXPECT_TRUE(contains_entry(lens_table->source_entries, lens_b));

        const MetadataConceptResolution* raw
            = find_concept(result, MetadataConceptKind::RawProcessing);
        ASSERT_NE(raw, nullptr);
        const MetadataConceptCandidate* source
            = find_role_entries(*raw,
                                MetadataConceptRole::ComputationalProcessing,
                                style_a, style_b);
        ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->shape, MetadataQueryValueShape::Table);
        EXPECT_EQ(source->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(source->source_bound);
        EXPECT_FALSE(source->rendered_image_safe);
    }

    TEST(MetadataConcepts, FlagsConflictingCreatedDatesAcrossFamilies)
    {
        MetaStore store;
        (void)add_exif_text(&store, "exififd", 0x9003U, "2024:04:19 12:34:56");
        (void)add_iptc_text(&store, 2U, 55U, "20250419");
        store.finalize();

        const MetadataConceptResolution datetime
            = resolve_metadata_concept(store, MetadataConceptKind::DateTime);

        EXPECT_TRUE(datetime.found);
        EXPECT_TRUE(datetime.conflict);
        const MetadataConceptCandidate* created
            = find_role(datetime, MetadataConceptRole::Created);
        ASSERT_NE(created, nullptr);
        EXPECT_TRUE(created->conflict);
        const MetadataConceptCandidate* date_created
            = find_role(datetime, MetadataConceptRole::DateCreated);
        ASSERT_NE(date_created, nullptr);
        EXPECT_TRUE(date_created->conflict);
    }

    TEST(MetadataConcepts, CombinesIptcDigitalCreationDateAndTime)
    {
        MetaStore store;
        const EntryId date_id = add_iptc_text(&store, 2U, 62U, "20240419");
        const EntryId time_id = add_iptc_text(&store, 2U, 63U, "123501+0000");
        store.finalize();

        const MetadataConceptResolution datetime
            = resolve_metadata_concept(store, MetadataConceptKind::DateTime);

        EXPECT_TRUE(datetime.found);
        EXPECT_FALSE(datetime.conflict);
        const MetadataConceptCandidate* digitized
            = find_role_entries(datetime, MetadataConceptRole::Digitized,
                                date_id, time_id);
        ASSERT_NE(digitized, nullptr);
        EXPECT_TRUE(digitized->has_date_time);
        EXPECT_TRUE(digitized->date_time_has_time);
        EXPECT_TRUE(digitized->date_time_has_utc_offset);
        EXPECT_EQ(digitized->date_time_zone, MetadataConceptTimeZoneKind::Utc);
        EXPECT_EQ(digitized->date_time_year, 2024);
        EXPECT_EQ(digitized->date_time_month, 4U);
        EXPECT_EQ(digitized->date_time_day, 19U);
        EXPECT_EQ(digitized->date_time_hour, 12U);
        EXPECT_EQ(digitized->date_time_minute, 35U);
        EXPECT_EQ(digitized->date_time_second, 1U);
    }

    TEST(MetadataConcepts, ComposesExifDateTimeCompanionsAcrossTimeZones)
    {
        MetaStore store;
        const EntryId created_id   = add_exif_text(&store, "exififd", 0x9003U,
                                                   "2024:04:19 12:34:56");
        const EntryId offset_id    = add_exif_text(&store, "exififd", 0x9011U,
                                                   "+09:00");
        const EntryId subsecond_id = add_exif_text(&store, "exififd", 0x9291U,
                                                   "125");
        (void)add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/",
                           "xmp:CreateDate", "2024-04-19T03:34:56.125Z");
        store.finalize();

        const MetadataConceptResolution datetime
            = resolve_metadata_concept(store, MetadataConceptKind::DateTime);

        EXPECT_TRUE(datetime.found);
        EXPECT_FALSE(datetime.conflict);
        const MetadataConceptCandidate* created
            = find_role_family(datetime, MetadataConceptRole::Created,
                               MetadataConceptSourceFamily::Exif);
        ASSERT_NE(created, nullptr);
        EXPECT_EQ(created->entry_id, created_id);
        EXPECT_TRUE(contains_entry(created->source_entries, offset_id));
        EXPECT_TRUE(contains_entry(created->source_entries, subsecond_id));
        EXPECT_TRUE(created->date_time_has_subsecond);
        EXPECT_EQ(created->date_time_subsecond, "125");
        EXPECT_TRUE(created->date_time_has_utc_offset);
        EXPECT_EQ(created->date_time_utc_offset_min, 540);
        EXPECT_EQ(created->date_time_zone, MetadataConceptTimeZoneKind::Offset);
        EXPECT_EQ(created->date_time_precision,
                  MetadataConceptDateTimePrecision::DateTimeSubsecond);
        EXPECT_EQ(created->text, "2024:04:19 12:34:56.125+09:00");

        const MetadataConceptCandidate* xmp_created
            = find_role_family(datetime, MetadataConceptRole::Created,
                               MetadataConceptSourceFamily::Xmp);
        ASSERT_NE(xmp_created, nullptr);
        EXPECT_TRUE(xmp_created->date_time_has_subsecond);
        EXPECT_EQ(xmp_created->date_time_subsecond, "125");
        EXPECT_EQ(xmp_created->date_time_precision,
                  MetadataConceptDateTimePrecision::DateTimeSubsecond);
    }

    TEST(MetadataConcepts, FlagsKnownSubsecondConflicts)
    {
        MetaStore store;
        (void)add_exif_text(&store, "exififd", 0x9003U, "2024:04:19 12:34:56");
        (void)add_exif_text(&store, "exififd", 0x9011U, "+09:00");
        (void)add_exif_text(&store, "exififd", 0x9291U, "1250");
        (void)add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/",
                           "xmp:CreateDate", "2024-04-19T03:34:56.126Z");
        store.finalize();

        const MetadataConceptResolution datetime
            = resolve_metadata_concept(store, MetadataConceptKind::DateTime);

        EXPECT_TRUE(datetime.found);
        EXPECT_TRUE(datetime.conflict);
        const MetadataConceptCandidate* created
            = find_role(datetime, MetadataConceptRole::Created);
        ASSERT_NE(created, nullptr);
        EXPECT_TRUE(created->conflict);
    }

    TEST(MetadataConcepts, MapsModifiedAndDigitizedExifCompanions)
    {
        MetaStore store;
        const EntryId modified_id     = add_exif_text(&store, "ifd0", 0x0132U,
                                                      "2024:04:20 10:20:30");
        const EntryId modified_offset = add_exif_text(&store, "exififd",
                                                      0x9010U, "+01:30");
        const EntryId modified_subsecond = add_exif_text(&store, "exififd",
                                                         0x9290U, "25");
        const EntryId digitized_id = add_exif_text(&store, "exififd", 0x9004U,
                                                   "2024:04:19 12:35:01");
        const EntryId digitized_offset    = add_exif_text(&store, "exififd",
                                                          0x9012U, "-05:00");
        const EntryId digitized_subsecond = add_exif_text(&store, "exififd",
                                                          0x9292U, "500");
        store.finalize();

        const MetadataConceptResolution datetime
            = resolve_metadata_concept(store, MetadataConceptKind::DateTime);
        const MetadataConceptCandidate* modified
            = find_role(datetime, MetadataConceptRole::Modified);
        const MetadataConceptCandidate* digitized
            = find_role(datetime, MetadataConceptRole::Digitized);

        ASSERT_NE(modified, nullptr);
        EXPECT_EQ(modified->entry_id, modified_id);
        EXPECT_TRUE(contains_entry(modified->source_entries, modified_offset));
        EXPECT_TRUE(
            contains_entry(modified->source_entries, modified_subsecond));
        EXPECT_EQ(modified->date_time_utc_offset_min, 90);
        EXPECT_EQ(modified->date_time_subsecond, "25");

        ASSERT_NE(digitized, nullptr);
        EXPECT_EQ(digitized->entry_id, digitized_id);
        EXPECT_TRUE(
            contains_entry(digitized->source_entries, digitized_offset));
        EXPECT_TRUE(
            contains_entry(digitized->source_entries, digitized_subsecond));
        EXPECT_EQ(digitized->date_time_utc_offset_min, -300);
        EXPECT_EQ(digitized->date_time_subsecond, "500");
    }

    TEST(MetadataConcepts, IgnoresInvalidExifDateTimeCompanions)
    {
        MetaStore store;
        const EntryId created_id     = add_exif_text(&store, "exififd", 0x9003U,
                                                     "2024:04:19 12:34:56");
        const EntryId invalid_offset = add_exif_text(&store, "exififd", 0x9011U,
                                                     "+25:00");
        const EntryId excessive_subsecond
            = add_exif_text(&store, "exififd", 0x9291U, "1234567890");
        store.finalize();

        const MetadataConceptResolution datetime
            = resolve_metadata_concept(store, MetadataConceptKind::DateTime);
        const MetadataConceptCandidate* created
            = find_role(datetime, MetadataConceptRole::Created);

        ASSERT_NE(created, nullptr);
        EXPECT_EQ(created->entry_id, created_id);
        EXPECT_FALSE(created->date_time_has_utc_offset);
        EXPECT_FALSE(created->date_time_has_subsecond);
        EXPECT_FALSE(contains_entry(created->source_entries, invalid_offset));
        EXPECT_FALSE(
            contains_entry(created->source_entries, excessive_subsecond));
    }

    TEST(MetadataConcepts, ReconcilesCrossFamilyDescriptiveScalarsAndLanguages)
    {
        MetaStore store;
        const EntryId exif_title = add_exif_text(&store, "ifd0", 0x9C9BU,
                                                 "Evening frame");
        (void)add_exif_text(&store, "ifd0", 0x010EU, "Street after rain");
        (void)add_iptc_text(&store, 2U, 5U, "Evening frame");
        (void)add_iptc_text(&store, 2U, 105U, "City at dusk");
        (void)add_iptc_text(&store, 2U, 120U, "Street after rain");
        const EntryId xmp_title
            = add_xmp_text(&store, "http://purl.org/dc/elements/1.1/",
                           "dc:title[@xml:lang=x-default]", "Evening frame");
        const EntryId xmp_title_fr
            = add_xmp_text(&store, "http://purl.org/dc/elements/1.1/",
                           "dc:title[@xml:lang=fr-FR]", "Cadre du soir");
        (void)add_xmp_text(&store, "http://purl.org/dc/elements/1.1/",
                           "dc:description[@xml:lang=x-default]",
                           "Street after rain");
        (void)add_xmp_text(&store, "http://ns.adobe.com/photoshop/1.0/",
                           "photoshop:Headline", "City at dusk");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);

        EXPECT_TRUE(descriptive.found);
        EXPECT_FALSE(descriptive.conflict);
        EXPECT_TRUE(contains_entry(descriptive.source_entries, exif_title));
        EXPECT_TRUE(contains_entry(descriptive.source_entries, xmp_title));
        EXPECT_TRUE(contains_entry(descriptive.source_entries, xmp_title_fr));

        const MetadataConceptCandidate* exif_default
            = find_role_family_scope_language(descriptive,
                                              MetadataConceptRole::Title,
                                              MetadataConceptSourceFamily::Exif,
                                              {}, "x-default");
        const MetadataConceptCandidate* xmp_default
            = find_role_family_scope_language(descriptive,
                                              MetadataConceptRole::Title,
                                              MetadataConceptSourceFamily::Xmp,
                                              {}, "x-default");
        const MetadataConceptCandidate* xmp_french
            = find_role_family_scope_language(descriptive,
                                              MetadataConceptRole::Title,
                                              MetadataConceptSourceFamily::Xmp,
                                              {}, "fr-fr");
        const MetadataConceptCandidate* headline
            = find_role_family(descriptive, MetadataConceptRole::Headline,
                               MetadataConceptSourceFamily::Xmp);
        ASSERT_NE(exif_default, nullptr);
        ASSERT_NE(xmp_default, nullptr);
        ASSERT_NE(xmp_french, nullptr);
        ASSERT_NE(headline, nullptr);
        EXPECT_FALSE(exif_default->preferred);
        EXPECT_TRUE(xmp_default->preferred);
        EXPECT_TRUE(xmp_french->preferred);
        EXPECT_TRUE(headline->preferred);
    }

    TEST(MetadataConcepts, FlagsDescriptiveConflictsOnlyWithinLanguage)
    {
        MetaStore store;
        (void)add_exif_text(&store, "ifd0", 0x9C9BU, "Evening frame");
        (void)add_xmp_text(&store, "http://purl.org/dc/elements/1.1/",
                           "dc:title[@xml:lang=x-default]", "Morning frame");
        (void)add_xmp_text(&store, "http://purl.org/dc/elements/1.1/",
                           "dc:title[@xml:lang=fr-FR]", "Cadre du soir");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);

        EXPECT_TRUE(descriptive.conflict);
        const MetadataConceptCandidate* default_title
            = find_role_family_scope_language(descriptive,
                                              MetadataConceptRole::Title,
                                              MetadataConceptSourceFamily::Xmp,
                                              {}, "x-default");
        const MetadataConceptCandidate* french_title
            = find_role_family_scope_language(descriptive,
                                              MetadataConceptRole::Title,
                                              MetadataConceptSourceFamily::Xmp,
                                              {}, "fr-fr");
        ASSERT_NE(default_title, nullptr);
        ASSERT_NE(french_title, nullptr);
        EXPECT_TRUE(default_title->conflict);
        EXPECT_FALSE(french_title->conflict);
        EXPECT_TRUE(french_title->preferred);
    }

    TEST(MetadataConcepts, TreatsCreatorsAndKeywordsAsAdditiveCollections)
    {
        MetaStore store;
        (void)add_exif_text(&store, "ifd0", 0x013BU, "Alice");
        (void)add_iptc_text(&store, 2U, 80U, "Alice");
        (void)add_xmp_text(&store, "http://purl.org/dc/elements/1.1/",
                           "dc:creator[1]", "Alice");
        (void)add_xmp_text(&store, "http://purl.org/dc/elements/1.1/",
                           "dc:creator[2]", "Bob");
        (void)add_iptc_text(&store, 2U, 25U, "night");
        (void)add_xmp_text(&store, "http://purl.org/dc/elements/1.1/",
                           "dc:subject[1]", "night");
        (void)add_xmp_text(&store, "http://purl.org/dc/elements/1.1/",
                           "dc:subject[2]", "street");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);

        EXPECT_TRUE(descriptive.found);
        EXPECT_FALSE(descriptive.conflict);
        const MetadataConceptCandidate* exif_alice
            = find_role_family_value(descriptive, MetadataConceptRole::Creator,
                                     MetadataConceptSourceFamily::Exif,
                                     "alice");
        const MetadataConceptCandidate* xmp_alice
            = find_role_family_value(descriptive, MetadataConceptRole::Creator,
                                     MetadataConceptSourceFamily::Xmp, "alice");
        const MetadataConceptCandidate* xmp_bob
            = find_role_family_value(descriptive, MetadataConceptRole::Creator,
                                     MetadataConceptSourceFamily::Xmp, "bob");
        const MetadataConceptCandidate* iptc_night
            = find_role_family_value(descriptive, MetadataConceptRole::Keywords,
                                     MetadataConceptSourceFamily::Iptc,
                                     "night");
        const MetadataConceptCandidate* xmp_night
            = find_role_family_value(descriptive, MetadataConceptRole::Keywords,
                                     MetadataConceptSourceFamily::Xmp, "night");
        const MetadataConceptCandidate* xmp_street
            = find_role_family_value(descriptive, MetadataConceptRole::Keywords,
                                     MetadataConceptSourceFamily::Xmp,
                                     "street");
        ASSERT_NE(exif_alice, nullptr);
        ASSERT_NE(xmp_alice, nullptr);
        ASSERT_NE(xmp_bob, nullptr);
        ASSERT_NE(iptc_night, nullptr);
        ASSERT_NE(xmp_night, nullptr);
        ASSERT_NE(xmp_street, nullptr);
        EXPECT_FALSE(exif_alice->preferred);
        EXPECT_TRUE(xmp_alice->preferred);
        EXPECT_TRUE(xmp_bob->preferred);
        EXPECT_FALSE(iptc_night->preferred);
        EXPECT_TRUE(xmp_night->preferred);
        EXPECT_TRUE(xmp_street->preferred);
    }

    TEST(MetadataConcepts, ResolvesDescriptiveLocationsByScopeAndLanguage)
    {
        MetaStore store;
        (void)add_iptc_text(&store, 2U, 90U, "Paris");
        (void)add_xmp_text(&store, "http://ns.adobe.com/photoshop/1.0/",
                           "photoshop:City", "Paris");
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
                           "LocationCreated/Address/City", "Paris");
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/City", "Kyoto");
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[2]/City", "Paris");
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/LocationName[@xml:lang=x-default]",
                           "Gion");
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/LocationName[@xml:lang=fr-FR]",
                           "Quartier de Gion");
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/LocationId[1]", "shown-001");
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/LocationId[2]", "shown-002");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);

        EXPECT_TRUE(descriptive.found);
        EXPECT_FALSE(descriptive.conflict);
        const MetadataConceptCandidate* created_city
            = find_preferred_role_scope_language(descriptive,
                                                 MetadataConceptRole::City,
                                                 "LocationCreated",
                                                 "x-default");
        const MetadataConceptCandidate* shown1_city
            = find_preferred_role_scope_language(descriptive,
                                                 MetadataConceptRole::City,
                                                 "LocationShown[1]",
                                                 "x-default");
        const MetadataConceptCandidate* shown2_city
            = find_preferred_role_scope_language(descriptive,
                                                 MetadataConceptRole::City,
                                                 "LocationShown[2]",
                                                 "x-default");
        const MetadataConceptCandidate* shown_name_default
            = find_preferred_role_scope_language(
                descriptive, MetadataConceptRole::LocationName,
                "LocationShown[1]", "x-default");
        const MetadataConceptCandidate* shown_name_french
            = find_preferred_role_scope_language(
                descriptive, MetadataConceptRole::LocationName,
                "LocationShown[1]", "fr-fr");
        const MetadataConceptCandidate* shown_id_one = find_role_family_value(
            descriptive, MetadataConceptRole::LocationIdentifier,
            MetadataConceptSourceFamily::Xmp, "shown001");
        const MetadataConceptCandidate* shown_id_two = find_role_family_value(
            descriptive, MetadataConceptRole::LocationIdentifier,
            MetadataConceptSourceFamily::Xmp, "shown002");
        ASSERT_NE(created_city, nullptr);
        ASSERT_NE(shown1_city, nullptr);
        ASSERT_NE(shown2_city, nullptr);
        ASSERT_NE(shown_name_default, nullptr);
        ASSERT_NE(shown_name_french, nullptr);
        ASSERT_NE(shown_id_one, nullptr);
        ASSERT_NE(shown_id_two, nullptr);
        EXPECT_EQ(shown_id_one->text, "shown-001");
        EXPECT_EQ(shown_id_two->text, "shown-002");
        EXPECT_EQ(created_city->family, MetadataConceptSourceFamily::Xmp);
        EXPECT_TRUE(created_city->preferred);
        EXPECT_TRUE(shown1_city->preferred);
        EXPECT_TRUE(shown2_city->preferred);
        EXPECT_TRUE(shown_name_default->preferred);
        EXPECT_TRUE(shown_name_french->preferred);
        EXPECT_TRUE(shown_id_one->preferred);
        EXPECT_TRUE(shown_id_two->preferred);
    }

    TEST(MetadataConcepts, FlagsDescriptiveLocationConflictsWithinOneScope)
    {
        MetaStore store;
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/City", "Kyoto");
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/City", "Osaka");
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[2]/City", "Kyoto");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);

        EXPECT_TRUE(descriptive.conflict);
        size_t shown1_conflicts = 0U;
        for (size_t i = 0U; i < descriptive.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate
                = descriptive.candidates[i];
            if (candidate.role == MetadataConceptRole::City
                && candidate.location_scope == "LocationShown[1]"
                && candidate.conflict) {
                shown1_conflicts += 1U;
            }
        }
        EXPECT_EQ(shown1_conflicts, 2U);
        const MetadataConceptCandidate* shown2
            = find_role_scope(descriptive, MetadataConceptRole::City,
                              "LocationShown[2]");
        ASSERT_NE(shown2, nullptr);
        EXPECT_FALSE(shown2->conflict);
        EXPECT_TRUE(shown2->preferred);
    }

    TEST(MetadataConcepts, ReconcilesRightsCreditAndSourceAcrossFamilies)
    {
        MetaStore store;
        (void)add_exif_text(&store, "ifd0", 0x8298U,
                            "Copyright 2026 Example Studio");
        (void)add_iptc_text(&store, 2U, 116U, "Copyright 2026 Example Studio");
        (void)add_iptc_text(&store, 2U, 110U, "Example Studio");
        (void)add_iptc_text(&store, 2U, 115U, "Example Archive");
        (void)add_xmp_text(&store, "http://purl.org/dc/elements/1.1/",
                           "dc:rights[@xml:lang=x-default]",
                           "Copyright 2026 Example Studio");
        (void)add_xmp_text(&store, "http://ns.adobe.com/photoshop/1.0/",
                           "photoshop:Credit", "Example Studio");
        (void)add_xmp_text(&store, "http://ns.adobe.com/photoshop/1.0/",
                           "photoshop:Source", "Example Archive");
        (void)add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/rights/",
                           "xmpRights:Marked", "True");
        (void)add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/rights/",
                           "xmpRights:WebStatement",
                           "https://example.test/rights");
        (void)add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/rights/",
                           "xmpRights:Certificate",
                           "https://example.test/certificate");
        (void)add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/rights/",
                           "xmpRights:Owner[1]", "Example Studio");
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "DigitalSourceType",
                           "http://cv.iptc.org/newscodes/digitalsourcetype/"
                           "digitalCapture");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);

        EXPECT_TRUE(descriptive.found);
        EXPECT_FALSE(descriptive.conflict);
        const MetadataConceptCandidate* copyright_notice
            = find_role_family_scope_language(
                descriptive, MetadataConceptRole::CopyrightNotice,
                MetadataConceptSourceFamily::Xmp, {}, "x-default");
        const MetadataConceptCandidate* credit
            = find_role_family(descriptive, MetadataConceptRole::CreditLine,
                               MetadataConceptSourceFamily::Xmp);
        const MetadataConceptCandidate* source
            = find_role_family(descriptive, MetadataConceptRole::Source,
                               MetadataConceptSourceFamily::Xmp);
        const MetadataConceptCandidate* marked
            = find_role(descriptive, MetadataConceptRole::RightsMarked);
        const MetadataConceptCandidate* owner
            = find_role(descriptive, MetadataConceptRole::RightsHolderName);
        const MetadataConceptCandidate* digital_source
            = find_role(descriptive, MetadataConceptRole::DigitalSourceType);
        ASSERT_NE(copyright_notice, nullptr);
        ASSERT_NE(credit, nullptr);
        ASSERT_NE(source, nullptr);
        ASSERT_NE(marked, nullptr);
        ASSERT_NE(owner, nullptr);
        ASSERT_NE(digital_source, nullptr);
        EXPECT_TRUE(copyright_notice->preferred);
        EXPECT_TRUE(credit->preferred);
        EXPECT_TRUE(source->preferred);
        EXPECT_EQ(marked->semantic, MetadataQuerySemanticKind::Rights);
        EXPECT_EQ(owner->semantic, MetadataQuerySemanticKind::Rights);
        EXPECT_EQ(digital_source->semantic, MetadataQuerySemanticKind::Source);
        EXPECT_EQ(marked->transfer_hint, MetadataConceptTransferHint::Safe);
    }

    TEST(MetadataConcepts, ScopesLocalizedRightsConflictsByLanguage)
    {
        MetaStore store;
        (void)add_exif_text(&store, "ifd0", 0x8298U, "Copyright A");
        (void)add_xmp_text(&store, "http://purl.org/dc/elements/1.1/",
                           "dc:rights[@xml:lang=x-default]", "Copyright B");
        (void)add_xmp_text(&store, "http://purl.org/dc/elements/1.1/",
                           "dc:rights[@xml:lang=fr-FR]", "Copyright A FR");
        (void)add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/rights/",
                           "xmpRights:UsageTerms[@xml:lang=x-default]",
                           "Editorial use only");
        (void)add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/rights/",
                           "xmpRights:UsageTerms[@xml:lang=fr-FR]",
                           "Usage editorial uniquement");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);

        EXPECT_TRUE(descriptive.conflict);
        const MetadataConceptCandidate* default_rights
            = find_role_family_scope_language(
                descriptive, MetadataConceptRole::CopyrightNotice,
                MetadataConceptSourceFamily::Xmp, {}, "x-default");
        const MetadataConceptCandidate* french_rights
            = find_role_family_scope_language(
                descriptive, MetadataConceptRole::CopyrightNotice,
                MetadataConceptSourceFamily::Xmp, {}, "fr-fr");
        const MetadataConceptCandidate* french_terms
            = find_role_family_scope_language(
                descriptive, MetadataConceptRole::RightsUsageTerms,
                MetadataConceptSourceFamily::Xmp, {}, "fr-fr");
        ASSERT_NE(default_rights, nullptr);
        ASSERT_NE(french_rights, nullptr);
        ASSERT_NE(french_terms, nullptr);
        EXPECT_TRUE(default_rights->conflict);
        EXPECT_FALSE(french_rights->conflict);
        EXPECT_FALSE(french_terms->conflict);
        EXPECT_TRUE(french_rights->preferred);
        EXPECT_TRUE(french_terms->preferred);
    }

    TEST(MetadataConcepts, PreservesPlusRightsAndLicensorRecordScopes)
    {
        MetaStore store;
        const std::string_view plus = "http://ns.useplus.org/ldf/xmp/1.0/";
        (void)add_xmp_text(&store, plus, "Licensor[1]/LicensorName",
                           "Agency One");
        (void)add_xmp_text(&store, plus, "Licensor[1]/LicensorID", "L-001");
        (void)add_xmp_text(&store, plus, "Licensor[2]/LicensorName",
                           "Agency Two");
        (void)add_xmp_text(&store, plus, "CopyrightOwner[1]/CopyrightOwnerName",
                           "Example Studio");
        (void)add_xmp_text(&store, plus, "CopyrightOwner[1]/CopyrightOwnerID",
                           "O-001");
        (void)add_xmp_text(&store, plus, "LicenseID", "LICENSE-001");
        (void)add_xmp_text(&store, plus,
                           "TermsAndConditionsText[@xml:lang=x-default]",
                           "Use under contract");
        (void)add_xmp_text(&store, plus, "TermsAndConditionsURL",
                           "https://example.test/license");
        (void)add_xmp_text(&store, plus, "CopyrightStatus", "CS-PRO");
        (void)add_xmp_text(&store, plus, "CreditLineRequired", "CR-CAI");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);

        EXPECT_TRUE(descriptive.found);
        EXPECT_FALSE(descriptive.conflict);
        const MetadataConceptCandidate* licensor_name
            = find_role_family_record_scope(descriptive,
                                            MetadataConceptRole::LicensorName,
                                            MetadataConceptSourceFamily::Xmp,
                                            "Licensor[1]");
        const MetadataConceptCandidate* licensor_id
            = find_role_family_record_scope(
                descriptive, MetadataConceptRole::LicensorIdentifier,
                MetadataConceptSourceFamily::Xmp, "Licensor[1]");
        const MetadataConceptCandidate* owner_name
            = find_role_family_record_scope(
                descriptive, MetadataConceptRole::RightsHolderName,
                MetadataConceptSourceFamily::Xmp, "CopyrightOwner[1]");
        const MetadataConceptCandidate* owner_id = find_role_family_record_scope(
            descriptive, MetadataConceptRole::RightsHolderIdentifier,
            MetadataConceptSourceFamily::Xmp, "CopyrightOwner[1]");
        const MetadataConceptCandidate* license_id
            = find_role(descriptive, MetadataConceptRole::LicenseIdentifier);
        const MetadataConceptCandidate* license_url
            = find_role(descriptive, MetadataConceptRole::LicenseTermsUrl);
        const MetadataConceptCandidate* credit_required
            = find_role(descriptive, MetadataConceptRole::CreditLineRequired);
        ASSERT_NE(licensor_name, nullptr);
        ASSERT_NE(licensor_id, nullptr);
        ASSERT_NE(owner_name, nullptr);
        ASSERT_NE(owner_id, nullptr);
        ASSERT_NE(license_id, nullptr);
        ASSERT_NE(license_url, nullptr);
        ASSERT_NE(credit_required, nullptr);
        EXPECT_TRUE(licensor_name->preferred);
        EXPECT_TRUE(licensor_id->preferred);
        EXPECT_EQ(license_id->semantic, MetadataQuerySemanticKind::License);
        EXPECT_EQ(license_url->semantic, MetadataQuerySemanticKind::License);
        EXPECT_EQ(credit_required->semantic, MetadataQuerySemanticKind::Credit);
    }

    TEST(MetadataConcepts, InterpretsStructuredEditorialRecords)
    {
        MetaStore store;
        const std::string_view core
            = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
        const std::string_view ext
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
        (void)add_xmp_text(&store, core, "CreatorContactInfo/CiAdrCity",
                           "Tokyo");
        (void)add_xmp_text(&store, core, "CreatorContactInfo/CiEmailWork",
                           "editor@example.test");
        (void)add_xmp_text(&store, ext, "Event[@xml:lang=x-default]",
                           "Opening night");
        (void)add_xmp_text(&store, ext, "EventId[1]", "event-001");
        (void)add_xmp_text(&store, ext,
                           "PersonInImageWDetails[1]/PersonName"
                           "[@xml:lang=x-default]",
                           "Alex Example");
        (void)add_xmp_text(&store, ext, "PersonInImageWDetails[1]/PersonId[1]",
                           "person-001");
        (void)add_xmp_text(&store, ext, "OrganisationInImageName[1]",
                           "Example Org");
        (void)add_xmp_text(&store, ext, "OrganisationInImageCode[1]",
                           "org-001");
        (void)add_xmp_text(&store, ext,
                           "ProductInImage[1]/ProductName"
                           "[@xml:lang=x-default]",
                           "Example Camera");
        (void)add_xmp_text(&store, ext, "ProductInImage[1]/ProductGTIN",
                           "0123456789012");
        (void)add_xmp_text(&store, ext,
                           "ArtworkOrObject[1]/AOTitle"
                           "[@xml:lang=x-default]",
                           "Example Artwork");
        (void)add_xmp_text(&store, ext,
                           "ArtworkOrObject[1]/AOContentDescription"
                           "[@xml:lang=x-default]",
                           "A framed print");
        (void)add_xmp_text(&store, ext,
                           "ArtworkOrObject[1]/AOContributionDescription"
                           "[@xml:lang=x-default]",
                           "Restored for exhibition");
        (void)add_xmp_text(&store, ext, "EmbdEncRightsExpr[1]/EncRightsExpr",
                           "ZXhhbXBsZSByaWdodHM=");
        (void)add_xmp_text(&store, ext,
                           "EmbdEncRightsExpr[1]/RightsExprEncType", "base64");
        (void)add_xmp_text(&store, ext, "EmbdEncRightsExpr[1]/RightsExprLangId",
                           "en");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);

        EXPECT_TRUE(descriptive.found);
        EXPECT_FALSE(descriptive.conflict);
        const MetadataConceptCandidate* contact = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::CreatorContact,
            MetadataConceptRole::Email, "CreatorContact");
        const MetadataConceptCandidate* event
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::Event,
                                     MetadataConceptRole::Name, "Event");
        const MetadataConceptCandidate* person
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::Person,
                                     MetadataConceptRole::Name, "Person[1]");
        const MetadataConceptCandidate* organization = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::Organization,
            MetadataConceptRole::Identifier, "Organization[1]");
        const MetadataConceptCandidate* product
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::Product,
                                     MetadataConceptRole::Gtin, "Product[1]");
        const MetadataConceptCandidate* artwork_content
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::ArtworkOrObject,
                                     MetadataConceptRole::ContentDescription,
                                     "ArtworkOrObject[1]");
        const MetadataConceptCandidate* artwork_contribution
            = find_record_role_scope(
                descriptive, MetadataConceptRecordKind::ArtworkOrObject,
                MetadataConceptRole::ContributionDescription,
                "ArtworkOrObject[1]");
        const MetadataConceptCandidate* rights = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::RightsExpression,
            MetadataConceptRole::RightsExpression,
            "EmbeddedRightsExpression[1]");
        ASSERT_NE(contact, nullptr);
        ASSERT_NE(event, nullptr);
        ASSERT_NE(person, nullptr);
        ASSERT_NE(organization, nullptr);
        ASSERT_NE(product, nullptr);
        ASSERT_NE(artwork_content, nullptr);
        ASSERT_NE(artwork_contribution, nullptr);
        ASSERT_NE(rights, nullptr);
        EXPECT_EQ(contact->semantic, MetadataQuerySemanticKind::Contact);
        EXPECT_EQ(contact->sensitivity,
                  MetadataConceptSensitivity::PersonalContact);
        EXPECT_EQ(event->semantic, MetadataQuerySemanticKind::Event);
        EXPECT_EQ(event->language, "x-default");
        EXPECT_EQ(person->semantic, MetadataQuerySemanticKind::Person);
        EXPECT_EQ(person->sensitivity,
                  MetadataConceptSensitivity::PersonIdentity);
        EXPECT_EQ(organization->semantic,
                  MetadataQuerySemanticKind::Organization);
        EXPECT_EQ(product->semantic, MetadataQuerySemanticKind::Product);
        EXPECT_EQ(artwork_content->semantic,
                  MetadataQuerySemanticKind::Artwork);
        EXPECT_EQ(rights->semantic,
                  MetadataQuerySemanticKind::RightsExpression);
        EXPECT_EQ(rights->sensitivity, MetadataConceptSensitivity::LegalRights);
        EXPECT_STREQ(metadata_concept_record_kind_name(contact->record_kind),
                     "creator_contact");
        EXPECT_STREQ(metadata_concept_sensitivity_name(contact->sensitivity),
                     "personal_contact");
        EXPECT_TRUE(artwork_content->preferred);
        EXPECT_TRUE(artwork_contribution->preferred);
    }

    TEST(MetadataConcepts, InterpretsPlusLicenseConstraintsAndReleases)
    {
        MetaStore store;
        const std::string_view plus = "http://ns.useplus.org/ldf/xmp/1.0/";
        (void)add_xmp_text(&store, plus, "LicenseStartDate", "2026-07-01");
        (void)add_xmp_text(&store, plus, "LicenseEndDate", "2027-07-01");
        (void)add_xmp_text(&store, plus,
                           "MediaConstraints[@xml:lang=x-default]",
                           "Editorial media only");
        (void)add_xmp_text(&store, plus, "ImageAlterationConstraints[1]",
                           "AL-CRP");
        (void)add_xmp_text(&store, plus, "LicensorTransactionID[1]",
                           "transaction-001");
        (void)add_xmp_text(&store, plus, "ModelReleaseStatus", "MR-LMR");
        (void)add_xmp_text(&store, plus, "ModelReleaseID[1]", "model-001");
        (void)add_xmp_text(&store, plus, "PropertyReleaseStatus", "PR-UPR");
        (void)add_xmp_text(&store, plus, "PropertyReleaseID[1]",
                           "property-001");
        (void)add_xmp_text(&store, plus, "Licensor[1]/LicensorEmail",
                           "rights@example.test");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);

        EXPECT_FALSE(descriptive.conflict);
        const MetadataConceptCandidate* start
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::License,
                                     MetadataConceptRole::LicenseStartDate, {});
        const MetadataConceptCandidate* media
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::License,
                                     MetadataConceptRole::MediaConstraint, {});
        const MetadataConceptCandidate* transaction = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::License,
            MetadataConceptRole::LicensorTransactionIdentifier, {});
        const MetadataConceptCandidate* model_status = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::Release,
            MetadataConceptRole::ReleaseStatus, "ModelRelease");
        const MetadataConceptCandidate* property_status
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::Release,
                                     MetadataConceptRole::ReleaseStatus,
                                     "PropertyRelease");
        const MetadataConceptCandidate* licensor_email
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::Licensor,
                                     MetadataConceptRole::Email, "Licensor[1]");
        ASSERT_NE(start, nullptr);
        ASSERT_NE(media, nullptr);
        ASSERT_NE(transaction, nullptr);
        ASSERT_NE(model_status, nullptr);
        ASSERT_NE(property_status, nullptr);
        ASSERT_NE(licensor_email, nullptr);
        EXPECT_EQ(start->semantic, MetadataQuerySemanticKind::License);
        EXPECT_TRUE(start->language.empty());
        EXPECT_EQ(media->language, "x-default");
        EXPECT_EQ(model_status->semantic, MetadataQuerySemanticKind::Release);
        EXPECT_EQ(model_status->sensitivity,
                  MetadataConceptSensitivity::LegalRights);
        EXPECT_TRUE(model_status->preferred);
        EXPECT_TRUE(property_status->preferred);
        EXPECT_EQ(licensor_email->sensitivity,
                  MetadataConceptSensitivity::PersonalContact);
    }

    TEST(MetadataConcepts, ReconcilesLegacyEditorialIptcAndPhotoshopPairs)
    {
        MetaStore store;
        const std::string_view photoshop = "http://ns.adobe.com/photoshop/1.0/";
        (void)add_iptc_text(&store, 2U, 10U, "5");
        (void)add_iptc_text(&store, 2U, 15U, "NWS");
        (void)add_iptc_text(&store, 2U, 20U, "SCI");
        (void)add_iptc_text(&store, 2U, 40U, "Do not crop");
        (void)add_iptc_text(&store, 2U, 85U, "Staff Photographer");
        (void)add_iptc_text(&store, 2U, 103U, "job-042");
        (void)add_iptc_text(&store, 2U, 122U, "Editor Example");
        (void)add_xmp_text(&store, photoshop, "Urgency", "5");
        (void)add_xmp_text(&store, photoshop, "Category", "NWS");
        (void)add_xmp_text(&store, photoshop, "SupplementalCategories[1]",
                           "SCI");
        (void)add_xmp_text(&store, photoshop, "Instructions", "Do not crop");
        (void)add_xmp_text(&store, photoshop, "AuthorsPosition",
                           "Staff Photographer");
        (void)add_xmp_text(&store, photoshop, "TransmissionReference",
                           "job-042");
        (void)add_xmp_text(&store, photoshop, "CaptionWriter",
                           "Editor Example");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);

        EXPECT_TRUE(descriptive.found);
        EXPECT_FALSE(descriptive.conflict);
        const MetadataConceptCandidate* iptc_instructions
            = find_role_family(descriptive, MetadataConceptRole::Instructions,
                               MetadataConceptSourceFamily::Iptc);
        const MetadataConceptCandidate* xmp_instructions
            = find_role_family(descriptive, MetadataConceptRole::Instructions,
                               MetadataConceptSourceFamily::Xmp);
        const MetadataConceptCandidate* xmp_category
            = find_role_family(descriptive, MetadataConceptRole::Category,
                               MetadataConceptSourceFamily::Xmp);
        const MetadataConceptCandidate* xmp_supplemental
            = find_role_family(descriptive,
                               MetadataConceptRole::SupplementalCategory,
                               MetadataConceptSourceFamily::Xmp);
        const MetadataConceptCandidate* creator_title
            = find_role_family(descriptive, MetadataConceptRole::CreatorTitle,
                               MetadataConceptSourceFamily::Xmp);
        const MetadataConceptCandidate* transmission
            = find_role_family(descriptive,
                               MetadataConceptRole::TransmissionReference,
                               MetadataConceptSourceFamily::Xmp);
        const MetadataConceptCandidate* caption_writer
            = find_role_family(descriptive, MetadataConceptRole::CaptionWriter,
                               MetadataConceptSourceFamily::Xmp);
        ASSERT_NE(iptc_instructions, nullptr);
        ASSERT_NE(xmp_instructions, nullptr);
        ASSERT_NE(xmp_category, nullptr);
        ASSERT_NE(xmp_supplemental, nullptr);
        ASSERT_NE(creator_title, nullptr);
        ASSERT_NE(transmission, nullptr);
        ASSERT_NE(caption_writer, nullptr);
        EXPECT_FALSE(iptc_instructions->preferred);
        EXPECT_TRUE(xmp_instructions->preferred);
        EXPECT_EQ(xmp_instructions->semantic,
                  MetadataQuerySemanticKind::Editorial);
        EXPECT_TRUE(xmp_category->preferred);
        EXPECT_TRUE(xmp_supplemental->preferred);
        EXPECT_EQ(creator_title->semantic, MetadataQuerySemanticKind::Creator);
        EXPECT_EQ(creator_title->sensitivity,
                  MetadataConceptSensitivity::PersonIdentity);
        EXPECT_EQ(transmission->semantic, MetadataQuerySemanticKind::Editorial);
        EXPECT_EQ(caption_writer->sensitivity,
                  MetadataConceptSensitivity::PersonIdentity);
        EXPECT_STREQ(metadata_concept_role_name(caption_writer->role),
                     "caption_writer");
    }

    TEST(MetadataConcepts, FlagsLegacyEditorialConflictsByExactRole)
    {
        MetaStore store;
        const std::string_view photoshop = "http://ns.adobe.com/photoshop/1.0/";
        (void)add_iptc_text(&store, 2U, 40U, "Do not crop");
        (void)add_xmp_text(&store, photoshop, "Instructions", "Crop allowed");
        (void)add_iptc_text(&store, 2U, 103U, "job-001");
        (void)add_xmp_text(&store, photoshop, "TransmissionReference",
                           "job-002");
        (void)add_iptc_text(&store, 2U, 20U, "SCI");
        (void)add_xmp_text(&store, photoshop, "SupplementalCategories[1]",
                           "ART");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);
        const MetadataConceptCandidate* instructions
            = find_role_family(descriptive, MetadataConceptRole::Instructions,
                               MetadataConceptSourceFamily::Xmp);
        const MetadataConceptCandidate* transmission
            = find_role_family(descriptive,
                               MetadataConceptRole::TransmissionReference,
                               MetadataConceptSourceFamily::Xmp);
        const MetadataConceptCandidate* supplemental
            = find_role_family(descriptive,
                               MetadataConceptRole::SupplementalCategory,
                               MetadataConceptSourceFamily::Xmp);

        EXPECT_TRUE(descriptive.conflict);
        ASSERT_NE(instructions, nullptr);
        ASSERT_NE(transmission, nullptr);
        ASSERT_NE(supplemental, nullptr);
        EXPECT_TRUE(instructions->conflict);
        EXPECT_TRUE(transmission->conflict);
        EXPECT_FALSE(supplemental->conflict);
    }

    TEST(MetadataConcepts, InterpretsLegacyIptcWorkflowAndReferenceRecords)
    {
        MetaStore store;
        const EntryId release_date = add_iptc_text(&store, 2U, 30U, "20260724");
        const EntryId release_time = add_iptc_text(&store, 2U, 35U,
                                                   "101530+0900");
        (void)add_iptc_text(&store, 2U, 3U, "01:news");
        (void)add_iptc_text(&store, 2U, 4U, "001:current");
        (void)add_iptc_text(&store, 2U, 4U, "002:analysis");
        (void)add_iptc_text(&store, 2U, 7U, "edited");
        (void)add_iptc_text(&store, 2U, 8U, "01");
        (void)add_iptc_text(&store, 2U, 12U, "IPTC:15000000:news:politics");
        (void)add_iptc_text(&store, 2U, 22U, "daily-briefing");
        (void)add_iptc_text(&store, 2U, 26U, "JPN");
        (void)add_iptc_text(&store, 2U, 27U, "Japan");
        (void)add_iptc_text(&store, 2U, 37U, "20260725");
        (void)add_iptc_text(&store, 2U, 38U, "235959+0900");
        (void)add_iptc_text(&store, 2U, 42U, "02");
        (void)add_iptc_text(&store, 2U, 45U, "NEWS");
        (void)add_iptc_text(&store, 2U, 47U, "20260720");
        (void)add_iptc_text(&store, 2U, 50U, "00001234");
        (void)add_iptc_text(&store, 2U, 45U, "SPORT");
        (void)add_iptc_text(&store, 2U, 47U, "20260721");
        (void)add_iptc_text(&store, 2U, 50U, "00005678");
        (void)add_iptc_text(&store, 2U, 65U, "PhotoDesk");
        (void)add_iptc_text(&store, 2U, 70U, "4.2");
        (void)add_iptc_text(&store, 2U, 75U, "p");
        (void)add_iptc_text(&store, 2U, 118U, "desk@example.test");
        (void)add_iptc_text(&store, 2U, 135U, "en");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);
        const MetadataConceptCandidate* release = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::EditorialWorkflow,
            MetadataConceptRole::EditorialReleaseDate, "EditorialWorkflow");
        const MetadataConceptCandidate* expiration = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::EditorialWorkflow,
            MetadataConceptRole::EditorialExpirationDate, "EditorialWorkflow");
        const MetadataConceptCandidate* first_reference = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ResourceReference,
            MetadataConceptRole::ReferenceService, "PriorEnvelopeReference[1]");
        const MetadataConceptCandidate* second_reference
            = find_record_role_scope(
                descriptive, MetadataConceptRecordKind::ResourceReference,
                MetadataConceptRole::ReferenceNumber,
                "PriorEnvelopeReference[2]");
        const MetadataConceptCandidate* reference_date = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ResourceReference,
            MetadataConceptRole::ReferenceDate, "PriorEnvelopeReference[1]");
        const MetadataConceptCandidate* source_software
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::SourceSoftware,
                                     MetadataConceptRole::SoftwareAgent,
                                     "SourceSoftware");
        const MetadataConceptCandidate* source_version = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::SourceSoftware,
            MetadataConceptRole::VersionIdentifier, "SourceSoftware");
        const MetadataConceptCandidate* contact = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::EditorialContact,
            MetadataConceptRole::Contact, "EditorialContact[1]");
        const MetadataConceptCandidate* location_code
            = find_role_scope(descriptive, MetadataConceptRole::CountryCode,
                              "LocationShown[1]");
        const MetadataConceptCandidate* location_name
            = find_role_scope(descriptive, MetadataConceptRole::LocationName,
                              "LocationShown[1]");

        EXPECT_TRUE(descriptive.found);
        EXPECT_FALSE(descriptive.conflict);
        EXPECT_NE(find_role(descriptive,
                            MetadataConceptRole::ObjectTypeReference),
                  nullptr);
        EXPECT_NE(find_role(descriptive,
                            MetadataConceptRole::ObjectAttributeReference),
                  nullptr);
        EXPECT_NE(find_role(descriptive, MetadataConceptRole::SubjectReference),
                  nullptr);
        EXPECT_NE(find_role(descriptive, MetadataConceptRole::EditStatus),
                  nullptr);
        EXPECT_NE(find_role(descriptive, MetadataConceptRole::EditorialUpdate),
                  nullptr);
        EXPECT_NE(find_role(descriptive, MetadataConceptRole::FixtureIdentifier),
                  nullptr);
        EXPECT_NE(find_role(descriptive, MetadataConceptRole::ActionAdvised),
                  nullptr);
        EXPECT_NE(find_role(descriptive, MetadataConceptRole::ObjectCycle),
                  nullptr);
        EXPECT_NE(find_role(descriptive,
                            MetadataConceptRole::LanguageIdentifier),
                  nullptr);
        ASSERT_NE(release, nullptr);
        ASSERT_NE(expiration, nullptr);
        ASSERT_NE(first_reference, nullptr);
        ASSERT_NE(second_reference, nullptr);
        ASSERT_NE(reference_date, nullptr);
        ASSERT_NE(source_software, nullptr);
        ASSERT_NE(source_version, nullptr);
        ASSERT_NE(contact, nullptr);
        ASSERT_NE(location_code, nullptr);
        ASSERT_NE(location_name, nullptr);

        EXPECT_TRUE(release->has_date_time);
        EXPECT_TRUE(release->date_time_has_time);
        EXPECT_EQ(release->date_time_year, 2026);
        EXPECT_EQ(release->date_time_month, 7U);
        EXPECT_EQ(release->date_time_day, 24U);
        EXPECT_EQ(release->date_time_hour, 10U);
        EXPECT_EQ(release->date_time_utc_offset_min, 540);
        EXPECT_TRUE(contains_entry(release->source_entries, release_date));
        EXPECT_TRUE(contains_entry(release->source_entries, release_time));
        EXPECT_FALSE(release->source_bound);
        EXPECT_TRUE(release->rendered_image_safe);
        EXPECT_TRUE(expiration->has_date_time);
        EXPECT_EQ(reference_date->date_time_precision,
                  MetadataConceptDateTimePrecision::Date);
        EXPECT_EQ(first_reference->semantic,
                  MetadataQuerySemanticKind::DocumentLineage);
        EXPECT_TRUE(first_reference->source_bound);
        EXPECT_EQ(source_software->semantic, MetadataQuerySemanticKind::Source);
        EXPECT_TRUE(source_software->source_bound);
        EXPECT_TRUE(source_version->source_bound);
        EXPECT_EQ(contact->semantic, MetadataQuerySemanticKind::Contact);
        EXPECT_EQ(contact->sensitivity,
                  MetadataConceptSensitivity::PersonalContact);
        EXPECT_TRUE(contact->rendered_image_safe);
        EXPECT_EQ(location_code->sensitivity,
                  MetadataConceptSensitivity::Location);
        EXPECT_STREQ(metadata_concept_role_name(
                         MetadataConceptRole::EditorialReleaseDate),
                     "editorial_release_date");
        EXPECT_STREQ(metadata_concept_record_kind_name(
                         MetadataConceptRecordKind::SourceSoftware),
                     "source_software");
    }

    TEST(MetadataConcepts, InterpretsWireEncodedIptcTechnicalRecords)
    {
        MetaStore store;
        const std::array<std::byte, 4U> preview_data { std::byte { 0xFFU },
                                                       std::byte { 0xD8U },
                                                       std::byte { 0xFFU },
                                                       std::byte { 0xD9U } };
        const std::array<std::byte, 3U> rasterized_caption {
            std::byte { 0x10U }, std::byte { 0x20U }, std::byte { 0x30U }
        };
        const std::array<std::byte, 2U> preview_format { std::byte { 0x00U },
                                                         std::byte { 0x0BU } };
        const std::array<std::byte, 2U> preview_version { std::byte { 0x00U },
                                                          std::byte { 0x01U } };
        (void)add_iptc_wire_text(&store, 2U, 5U, "Technical sample");
        (void)add_iptc_wire_text(&store, 2U, 55U, "20260724");
        (void)add_iptc_wire_text(&store, 2U, 60U, "101530+0900");
        (void)add_iptc_bytes(&store, 2U, 125U, rasterized_caption);
        (void)add_iptc_wire_text(&store, 2U, 130U, "3C");
        (void)add_iptc_wire_text(&store, 2U, 131U, "L");
        (void)add_iptc_wire_text(&store, 2U, 150U, "2M");
        (void)add_iptc_wire_text(&store, 2U, 151U, "044100");
        (void)add_iptc_wire_text(&store, 2U, 152U, "16");
        (void)add_iptc_wire_text(&store, 2U, 153U, "013005");
        (void)add_iptc_wire_text(&store, 2U, 154U, "end cue");
        (void)add_iptc_bytes(&store, 2U, 200U, preview_format);
        (void)add_iptc_bytes(&store, 2U, 201U, preview_version);
        (void)add_iptc_bytes(&store, 2U, 202U, preview_data);
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);
        const MetadataConceptResolution datetime
            = resolve_metadata_concept(store, MetadataConceptKind::DateTime);
        const MetadataConceptCandidate* image_type = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::TechnicalImage,
            MetadataConceptRole::ImageType, "TechnicalImage");
        const MetadataConceptCandidate* component_count
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::TechnicalImage,
                                     MetadataConceptRole::ImageComponentCount,
                                     "TechnicalImage");
        const MetadataConceptCandidate* component_code = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::TechnicalImage,
            MetadataConceptRole::ImageColorComponentCode, "TechnicalImage");
        const MetadataConceptCandidate* image_layout = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::TechnicalImage,
            MetadataConceptRole::ImageLayout, "TechnicalImage");
        const MetadataConceptCandidate* audio_type = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::AudioAsset,
            MetadataConceptRole::AudioType, "AudioAsset");
        const MetadataConceptCandidate* audio_channels = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::AudioAsset,
            MetadataConceptRole::AudioChannelCount, "AudioAsset");
        const MetadataConceptCandidate* audio_rate = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::AudioAsset,
            MetadataConceptRole::AudioSamplingRate, "AudioAsset");
        const MetadataConceptCandidate* audio_resolution
            = find_record_role_scope(
                descriptive, MetadataConceptRecordKind::AudioAsset,
                MetadataConceptRole::AudioSamplingResolution, "AudioAsset");
        const MetadataConceptCandidate* audio_duration = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::AudioAsset,
            MetadataConceptRole::AudioDuration, "AudioAsset");
        const MetadataConceptCandidate* preview_format_candidate
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::PreviewAsset,
                                     MetadataConceptRole::PreviewFormat,
                                     "PreviewAsset");
        const MetadataConceptCandidate* preview_version_candidate
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::PreviewAsset,
                                     MetadataConceptRole::PreviewVersion,
                                     "PreviewAsset");
        const MetadataConceptCandidate* preview = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::PreviewAsset,
            MetadataConceptRole::PreviewData, "PreviewAsset");
        const MetadataConceptCandidate* rasterized = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::PreviewAsset,
            MetadataConceptRole::RasterizedCaption, "RasterizedCaption");
        const MetadataConceptCandidate* title
            = find_role(descriptive, MetadataConceptRole::Title);
        const MetadataConceptCandidate* created
            = find_role(datetime, MetadataConceptRole::DateCreated);

        ASSERT_NE(image_type, nullptr);
        ASSERT_NE(component_count, nullptr);
        ASSERT_NE(component_code, nullptr);
        ASSERT_NE(image_layout, nullptr);
        ASSERT_NE(audio_type, nullptr);
        ASSERT_NE(audio_channels, nullptr);
        ASSERT_NE(audio_rate, nullptr);
        ASSERT_NE(audio_resolution, nullptr);
        ASSERT_NE(audio_duration, nullptr);
        ASSERT_NE(preview_format_candidate, nullptr);
        ASSERT_NE(preview_version_candidate, nullptr);
        ASSERT_NE(preview, nullptr);
        ASSERT_NE(rasterized, nullptr);
        ASSERT_NE(title, nullptr);
        ASSERT_NE(created, nullptr);

        EXPECT_EQ(image_type->text, "3C");
        EXPECT_DOUBLE_EQ(component_count->numeric[0], 3.0);
        EXPECT_EQ(component_code->text, "C");
        EXPECT_EQ(image_layout->text, "landscape");
        EXPECT_EQ(image_layout->semantic,
                  MetadataQuerySemanticKind::TechnicalImage);
        EXPECT_EQ(find_role(descriptive, MetadataConceptRole::Orientation),
                  nullptr);
        EXPECT_EQ(audio_type->text, "stereo music");
        EXPECT_DOUBLE_EQ(audio_channels->numeric[0], 2.0);
        EXPECT_DOUBLE_EQ(audio_rate->numeric[0], 44100.0);
        EXPECT_DOUBLE_EQ(audio_resolution->numeric[0], 16.0);
        EXPECT_DOUBLE_EQ(audio_duration->numeric[0], 5405.0);
        EXPECT_EQ(audio_duration->text, "013005");
        EXPECT_DOUBLE_EQ(preview_format_candidate->numeric[0], 11.0);
        EXPECT_EQ(preview_format_candidate->text,
                  "JPEG File Interchange Format");
        EXPECT_DOUBLE_EQ(preview_version_candidate->numeric[0], 1.0);
        EXPECT_EQ(preview->shape, MetadataQueryValueShape::Blob);
        EXPECT_DOUBLE_EQ(preview->numeric[0], 4.0);
        EXPECT_FALSE(preview->value_key.empty());
        EXPECT_DOUBLE_EQ(rasterized->numeric[0], 3.0);
        EXPECT_EQ(title->text, "Technical sample");
        EXPECT_TRUE(created->has_date_time);
        EXPECT_TRUE(created->date_time_has_time);
        EXPECT_EQ(created->date_time_utc_offset_min, 540);
        EXPECT_TRUE(image_layout->source_bound);
        EXPECT_TRUE(audio_type->source_bound);
        EXPECT_TRUE(preview->source_bound);
        EXPECT_FALSE(image_layout->rendered_image_safe);
        EXPECT_FALSE(audio_type->rendered_image_safe);
        EXPECT_FALSE(preview->rendered_image_safe);
        EXPECT_STREQ(metadata_concept_role_name(
                         MetadataConceptRole::AudioSamplingResolution),
                     "audio_sampling_resolution");
        EXPECT_STREQ(metadata_concept_record_kind_name(
                         MetadataConceptRecordKind::PreviewAsset),
                     "preview_asset");
    }

    TEST(MetadataConcepts, InterpretsAccessibilityTaxonomyAndDocumentIdentity)
    {
        MetaStore store;
        const std::string_view core
            = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
        const std::string_view dc  = "http://purl.org/dc/elements/1.1/";
        const std::string_view xmp = "http://ns.adobe.com/xap/1.0/";
        const std::string_view mm  = "http://ns.adobe.com/xap/1.0/mm/";
        (void)add_xmp_text(&store, core,
                           "AltTextAccessibility[@xml:lang=x-default]",
                           "Person crossing a street");
        (void)add_xmp_text(&store, core,
                           "ExtDescrAccessibility[@xml:lang=en-US]",
                           "A detailed description of the crossing");
        (void)add_xmp_text(&store, core, "IntellectualGenre", "news");
        (void)add_xmp_text(&store, core, "Scene[1]", "010100");
        (void)add_xmp_text(&store, core, "SubjectCode[1]", "15000000");
        (void)add_xmp_text(&store, core, "CiEmailWork", "editor@example.test");
        (void)add_xmp_text(&store, dc, "identifier", "asset-001");
        (void)add_xmp_text(&store, dc, "source", "source-work-001");
        (void)add_xmp_text(&store, xmp, "Identifier[1]", "asset-001");
        (void)add_xmp_text(&store, mm, "DocumentID", "xmp.did:document");
        (void)add_xmp_text(&store, mm, "InstanceID", "xmp.iid:instance");
        (void)add_xmp_text(&store, mm, "OriginalDocumentID",
                           "xmp.did:original");
        (void)add_xmp_text(&store, mm, "RenditionClass", "proof:pdf");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);
        const MetadataConceptCandidate* alt
            = find_role(descriptive, MetadataConceptRole::AccessibilityAltText);
        const MetadataConceptCandidate* extended
            = find_role(descriptive,
                        MetadataConceptRole::AccessibilityExtendedDescription);
        const MetadataConceptCandidate* scene
            = find_role(descriptive, MetadataConceptRole::SceneCode);
        const MetadataConceptCandidate* subject
            = find_role(descriptive, MetadataConceptRole::SubjectCode);
        const MetadataConceptCandidate* contact = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::CreatorContact,
            MetadataConceptRole::Email, "CreatorContact");
        const MetadataConceptCandidate* document = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageAsset,
            MetadataConceptRole::DocumentIdentifier, "ImageAsset");
        const MetadataConceptCandidate* instance = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageAsset,
            MetadataConceptRole::InstanceIdentifier, "ImageAsset");
        const MetadataConceptCandidate* derived = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageAsset,
            MetadataConceptRole::DerivedFromIdentifier, "ImageAsset");
        ASSERT_NE(alt, nullptr);
        ASSERT_NE(extended, nullptr);
        ASSERT_NE(scene, nullptr);
        ASSERT_NE(subject, nullptr);
        ASSERT_NE(contact, nullptr);
        ASSERT_NE(document, nullptr);
        ASSERT_NE(instance, nullptr);
        ASSERT_NE(derived, nullptr);
        EXPECT_EQ(alt->semantic, MetadataQuerySemanticKind::Accessibility);
        EXPECT_EQ(alt->language, "x-default");
        EXPECT_EQ(extended->language, "en-us");
        EXPECT_EQ(scene->semantic, MetadataQuerySemanticKind::Taxonomy);
        EXPECT_EQ(subject->semantic, MetadataQuerySemanticKind::Taxonomy);
        EXPECT_EQ(contact->sensitivity,
                  MetadataConceptSensitivity::PersonalContact);
        EXPECT_EQ(document->semantic,
                  MetadataQuerySemanticKind::DocumentIdentity);
        EXPECT_EQ(instance->semantic,
                  MetadataQuerySemanticKind::DocumentIdentity);
        EXPECT_EQ(derived->text, "source-work-001");
    }

    TEST(MetadataConcepts, InterpretsRemainingPlusPartiesAssetsAndPolicy)
    {
        MetaStore store;
        const std::string_view plus = "http://ns.useplus.org/ldf/xmp/1.0/";
        (void)add_xmp_text(&store, plus, "EndUser[1]/EndUserName",
                           "Example Publisher");
        (void)add_xmp_text(&store, plus, "EndUser[1]/EndUserID", "EU-001");
        (void)add_xmp_text(&store, plus, "ImageCreator[1]/ImageCreatorName",
                           "Alex Example");
        (void)add_xmp_text(&store, plus, "ImageCreator[1]/ImageCreatorID",
                           "IC-001");
        (void)add_xmp_text(&store, plus, "ImageCreator[1]/ImageCreatorImageID",
                           "IMG-IC-1");
        (void)add_xmp_text(&store, plus, "ImageSupplierName", "Example Agency");
        (void)add_xmp_text(&store, plus, "ImageSupplierID", "IS-001");
        (void)add_xmp_text(&store, plus, "ImageSupplierImageID", "IMG-IS-1");
        (void)add_xmp_text(&store, plus, "MediaSummaryCode", "PLUS-MSC");
        (void)add_xmp_text(&store, plus, "ImageDuplicationConstraints",
                           "DUP-NO");
        (void)add_xmp_text(&store, plus, "MinorModelAgeDisclosure", "AG-A18");
        (void)add_xmp_text(&store, plus, "AdultContentWarning", "CW-AWR");
        (void)add_xmp_text(&store, plus, "ImageType", "TY-PHO");
        (void)add_xmp_text(&store, plus, "FileNameAsDelivered", "asset.tif");
        (void)add_xmp_text(&store, plus, "ImageFileFormatAsDelivered",
                           "FF-TIF");
        (void)add_xmp_text(&store, plus, "ImageFileSizeAsDelivered", "SZ-HI");
        (void)add_xmp_text(&store, plus, "CopyrightRegistrationNumber",
                           "REG-001");
        (void)add_xmp_text(&store, plus, "FirstPublicationDate", "2026-07-01");
        (void)add_xmp_text(&store, plus, "Reuse", "RE-NAP");
        (void)add_xmp_text(&store, plus, "DataMining", "DMI-PRO");
        (void)add_xmp_text(&store, plus, "OtherLicenseDocuments[1]",
                           "contract-001");
        (void)add_xmp_text(&store, plus,
                           "OtherLicenseInfo[@xml:lang=x-default]",
                           "Archive the signed contract");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);
        const MetadataConceptCandidate* end_user
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::EndUser,
                                     MetadataConceptRole::Name, "EndUser[1]");
        const MetadataConceptCandidate* creator = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageCreator,
            MetadataConceptRole::Name, "ImageCreator[1]");
        const MetadataConceptCandidate* supplier = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageSupplier,
            MetadataConceptRole::Name, "ImageSupplier");
        const MetadataConceptCandidate* delivered_name = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageAsset,
            MetadataConceptRole::DeliveredFileName, "ImageAsset");
        const MetadataConceptCandidate* duplication
            = find_role(descriptive,
                        MetadataConceptRole::ImageDuplicationConstraint);
        const MetadataConceptCandidate* minor_age = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::Release,
            MetadataConceptRole::MinorModelAgeDisclosure, "ModelRelease");
        const MetadataConceptCandidate* license_info
            = find_role(descriptive,
                        MetadataConceptRole::OtherLicenseInformation);
        ASSERT_NE(end_user, nullptr);
        ASSERT_NE(creator, nullptr);
        ASSERT_NE(supplier, nullptr);
        ASSERT_NE(delivered_name, nullptr);
        ASSERT_NE(duplication, nullptr);
        ASSERT_NE(minor_age, nullptr);
        ASSERT_NE(license_info, nullptr);
        EXPECT_EQ(end_user->semantic, MetadataQuerySemanticKind::License);
        EXPECT_EQ(end_user->sensitivity,
                  MetadataConceptSensitivity::LegalRights);
        EXPECT_EQ(creator->semantic, MetadataQuerySemanticKind::Creator);
        EXPECT_EQ(creator->sensitivity,
                  MetadataConceptSensitivity::PersonIdentity);
        EXPECT_EQ(supplier->semantic, MetadataQuerySemanticKind::Source);
        EXPECT_EQ(delivered_name->semantic,
                  MetadataQuerySemanticKind::DocumentIdentity);
        EXPECT_EQ(duplication->sensitivity,
                  MetadataConceptSensitivity::LegalRights);
        EXPECT_EQ(minor_age->semantic, MetadataQuerySemanticKind::Release);
        EXPECT_EQ(license_info->language, "x-default");
        EXPECT_STREQ(metadata_concept_record_kind_name(supplier->record_kind),
                     "image_supplier");
    }

    TEST(MetadataConcepts, UsesToleranceForGpsCoordinateConflicts)
    {
        {
            MetaStore store;
            (void)add_exif_text(&store, "gpsifd", 0x0001U, "N");
            const std::array<URational, 3> lat = {
                URational { 41U, 1U },
                URational { 24U, 1U },
                URational { 30U, 1U },
            };
            (void)add_exif_urational_array(
                &store, "gpsifd", 0x0002U,
                std::span<const URational>(lat.data(), lat.size()));
            (void)add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/",
                               "exif:GPSLatitude", "41,24.5000001N");
            store.finalize();

            const MetadataConceptResolution gps
                = resolve_metadata_concept(store, MetadataConceptKind::Gps);

            EXPECT_TRUE(gps.found);
            EXPECT_FALSE(gps.conflict);
        }

        {
            MetaStore store;
            (void)add_exif_text(&store, "gpsifd", 0x0001U, "N");
            const std::array<URational, 3> lat = {
                URational { 41U, 1U },
                URational { 24U, 1U },
                URational { 30U, 1U },
            };
            (void)add_exif_urational_array(
                &store, "gpsifd", 0x0002U,
                std::span<const URational>(lat.data(), lat.size()));
            (void)add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/",
                               "exif:GPSLatitude", "41,25.500N");
            store.finalize();

            const MetadataConceptResolution gps
                = resolve_metadata_concept(store, MetadataConceptKind::Gps);

            EXPECT_TRUE(gps.found);
            EXPECT_TRUE(gps.conflict);
            const MetadataConceptCandidate* lat_candidate
                = find_role(gps, MetadataConceptRole::Latitude);
            ASSERT_NE(lat_candidate, nullptr);
            EXPECT_TRUE(lat_candidate->conflict);
        }
    }

    TEST(MetadataConcepts,
         ResolvesStructuredXmpLocationsWithoutCameraGpsConflicts)
    {
        MetaStore store;
        (void)add_exif_text(&store, "gpsifd", 0x0001U, "N");
        const std::array<URational, 3> lat = {
            URational { 41U, 1U },
            URational { 24U, 1U },
            URational { 30U, 1U },
        };
        (void)add_exif_urational_array(&store, "gpsifd", 0x0002U,
                                       std::span<const URational>(lat.data(),
                                                                  lat.size()));
        (void)add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/",
                           "exif:GPSLatitude", "41,24.500N");
        const EntryId shown1_latitude
            = add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/GPSLatitude", "48,51.507N");
        const EntryId shown1_longitude
            = add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/GPSLongitude", "2,17.667E");
        const EntryId shown1_altitude
            = add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/GPSAltitude", "35");
        const EntryId shown1_altitude_ref
            = add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/GPSAltitudeRef", "0");
        const EntryId shown2_latitude
            = add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "Iptc4xmpExt:LocationShown[2]/"
                           "Iptc4xmpExt:GPSLatitude",
                           "35,40N");
        const EntryId created_longitude
            = add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationCreated/GPSLongitude", "2,9E");
        store.finalize();

        const MetadataConceptResolution gps
            = resolve_metadata_concept(store, MetadataConceptKind::Gps);

        EXPECT_TRUE(gps.found);
        EXPECT_FALSE(gps.conflict);
        EXPECT_TRUE(contains_entry(gps.source_entries, shown1_latitude));
        EXPECT_TRUE(contains_entry(gps.source_entries, shown1_longitude));
        EXPECT_TRUE(contains_entry(gps.source_entries, shown1_altitude));
        EXPECT_TRUE(contains_entry(gps.source_entries, shown2_latitude));
        EXPECT_TRUE(contains_entry(gps.source_entries, created_longitude));

        const MetadataConceptCandidate* camera_latitude
            = find_role(gps, MetadataConceptRole::Latitude);
        const MetadataConceptCandidate* shown1
            = find_role_scope(gps, MetadataConceptRole::LocationShownLatitude,
                              "LocationShown[1]");
        const MetadataConceptCandidate* shown2
            = find_role_scope(gps, MetadataConceptRole::LocationShownLatitude,
                              "LocationShown[2]");
        const MetadataConceptCandidate* shown_altitude
            = find_role_scope(gps, MetadataConceptRole::LocationShownAltitude,
                              "LocationShown[1]");
        const MetadataConceptCandidate* created
            = find_role_scope(gps,
                              MetadataConceptRole::LocationCreatedLongitude,
                              "LocationCreated");
        ASSERT_NE(camera_latitude, nullptr);
        ASSERT_NE(shown1, nullptr);
        ASSERT_NE(shown2, nullptr);
        ASSERT_NE(shown_altitude, nullptr);
        ASSERT_NE(created, nullptr);
        EXPECT_TRUE(camera_latitude->preferred);
        EXPECT_TRUE(shown1->preferred);
        EXPECT_TRUE(shown2->preferred);
        EXPECT_TRUE(created->preferred);
        EXPECT_NEAR(camera_latitude->numeric[0], 41.4083333333, 1.0e-9);
        EXPECT_NEAR(shown1->numeric[0], 48.85845, 1.0e-9);
        EXPECT_NEAR(shown2->numeric[0], 35.6666666667, 1.0e-9);
        EXPECT_TRUE(shown_altitude->has_gps_altitude_reference);
        EXPECT_FALSE(shown_altitude->gps_altitude_below_sea_level);
        EXPECT_TRUE(contains_entry(shown_altitude->source_entries,
                                   shown1_altitude_ref));
    }

    TEST(MetadataConcepts, FlagsStructuredLocationConflictsWithinOneScope)
    {
        MetaStore store;
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/GPSLatitude", "48,51.507N");
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[1]/GPSLatitude", "49,00N");
        (void)add_xmp_text(&store,
                           "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                           "LocationShown[2]/GPSLatitude", "35,40N");
        store.finalize();

        const MetadataConceptResolution gps
            = resolve_metadata_concept(store, MetadataConceptKind::Gps);

        EXPECT_TRUE(gps.conflict);
        size_t shown1_conflicts = 0U;
        for (size_t i = 0U; i < gps.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = gps.candidates[i];
            if (candidate.role == MetadataConceptRole::LocationShownLatitude
                && candidate.location_scope == "LocationShown[1]"
                && candidate.conflict) {
                shown1_conflicts += 1U;
            }
        }
        EXPECT_EQ(shown1_conflicts, 2U);
        const MetadataConceptCandidate* shown2
            = find_role_scope(gps, MetadataConceptRole::LocationShownLatitude,
                              "LocationShown[2]");
        ASSERT_NE(shown2, nullptr);
        EXPECT_FALSE(shown2->conflict);
        EXPECT_TRUE(shown2->preferred);
    }

    TEST(MetadataConcepts, ResolvesExifAndXmpDestinationCoordinates)
    {
        MetaStore store;
        const EntryId latitude_ref = add_exif_text(&store, "gpsifd", 0x0013U,
                                                   "N");
        const std::array<URational, 3> latitude = {
            URational { 48U, 1U },
            URational { 51U, 1U },
            URational { 3042U, 100U },
        };
        (void)add_exif_urational_array(
            &store, "gpsifd", 0x0014U,
            std::span<const URational>(latitude.data(), latitude.size()));
        const EntryId longitude_ref = add_exif_text(&store, "gpsifd", 0x0015U,
                                                    "E");
        const std::array<URational, 3> longitude = {
            URational { 2U, 1U },
            URational { 17U, 1U },
            URational { 4002U, 100U },
        };
        (void)add_exif_urational_array(
            &store, "gpsifd", 0x0016U,
            std::span<const URational>(longitude.data(), longitude.size()));
        (void)add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/",
                           "exif:GPSDestLatitude", "48,51.507N");
        (void)add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/",
                           "exif:GPSDestLongitude", "2,17.667E");
        store.finalize();

        const MetadataConceptResolution gps
            = resolve_metadata_concept(store, MetadataConceptKind::Gps);

        EXPECT_TRUE(gps.found);
        EXPECT_FALSE(gps.conflict);
        const MetadataConceptCandidate* exif_latitude
            = find_role_family(gps, MetadataConceptRole::DestinationLatitude,
                               MetadataConceptSourceFamily::Exif);
        const MetadataConceptCandidate* xmp_latitude
            = find_role_family(gps, MetadataConceptRole::DestinationLatitude,
                               MetadataConceptSourceFamily::Xmp);
        const MetadataConceptCandidate* exif_longitude
            = find_role_family(gps, MetadataConceptRole::DestinationLongitude,
                               MetadataConceptSourceFamily::Exif);
        ASSERT_NE(exif_latitude, nullptr);
        ASSERT_NE(xmp_latitude, nullptr);
        ASSERT_NE(exif_longitude, nullptr);
        EXPECT_TRUE(exif_latitude->preferred);
        EXPECT_TRUE(
            contains_entry(exif_latitude->source_entries, latitude_ref));
        EXPECT_TRUE(
            contains_entry(exif_longitude->source_entries, longitude_ref));
        EXPECT_NEAR(exif_latitude->numeric[0], xmp_latitude->numeric[0],
                    1.0e-9);
    }

    TEST(MetadataConcepts, CombinesXmpGpsDateAndTimeStamp)
    {
        MetaStore store;
        const EntryId date_id = add_xmp_text(&store,
                                             "http://ns.adobe.com/exif/1.0/",
                                             "exif:GPSDateStamp", "2024:04:19");
        const EntryId time_id = add_xmp_text(&store,
                                             "http://ns.adobe.com/exif/1.0/",
                                             "exif:GPSTimeStamp", "12:34:56Z");
        const EntryId direct_id
            = add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/",
                           "exif:GPSDateTime", "2024-04-19T12:34:56Z");
        store.finalize();

        const MetadataConceptResolution gps
            = resolve_metadata_concept(store, MetadataConceptKind::Gps);

        const MetadataConceptCandidate* composite = nullptr;
        const MetadataConceptCandidate* direct    = nullptr;
        for (size_t i = 0U; i < gps.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = gps.candidates[i];
            if (candidate.role == MetadataConceptRole::Timestamp
                && contains_entry(candidate.source_entries, date_id)
                && contains_entry(candidate.source_entries, time_id)) {
                composite = &candidate;
                continue;
            }
            if (candidate.role == MetadataConceptRole::Timestamp
                && contains_entry(candidate.source_entries, direct_id)) {
                direct = &candidate;
            }
        }

        ASSERT_NE(composite, nullptr);
        ASSERT_NE(direct, nullptr);
        EXPECT_FALSE(gps.conflict);
        EXPECT_EQ(gps.preferred_entry, direct_id);
        EXPECT_EQ(composite->text, "2024:04:19 12:34:56Z");
        EXPECT_TRUE(composite->has_date_time);
        EXPECT_TRUE(composite->date_time_has_time);
        EXPECT_TRUE(composite->date_time_has_utc_offset);
        EXPECT_EQ(composite->date_time_zone, MetadataConceptTimeZoneKind::Utc);
        EXPECT_EQ(composite->date_time_year, 2024);
        EXPECT_EQ(composite->date_time_hour, 12U);
        EXPECT_EQ(composite->date_time_minute, 34U);
        EXPECT_EQ(composite->date_time_second, 56U);
        EXPECT_TRUE(direct->has_date_time);
        EXPECT_TRUE(direct->date_time_has_time);
        EXPECT_TRUE(direct->date_time_has_utc_offset);
        EXPECT_EQ(direct->date_time_zone, MetadataConceptTimeZoneKind::Utc);
        EXPECT_EQ(direct->date_time_year, 2024);
        EXPECT_EQ(direct->date_time_hour, 12U);
        EXPECT_EQ(direct->date_time_minute, 34U);
        EXPECT_EQ(direct->date_time_second, 56U);
    }

    TEST(MetadataConcepts, ResolvesExposureConceptRoles)
    {
        MetaStore store;
        const EntryId exposure_time = add_exif_urational(&store, "exififd",
                                                         0x829AU, 1U, 125U);
        const EntryId aperture = add_exif_urational(&store, "exififd", 0x829DU,
                                                    56U, 10U);
        const EntryId iso      = add_exif_u16(&store, "exififd", 0x8827U, 200U);
        const EntryId bias = add_exif_srational(&store, "exififd", 0x9204U, -1,
                                                3);
        const EntryId program = add_exif_u16(&store, "exififd", 0x8822U, 3U);
        const EntryId gain    = add_exif_u16(&store, "exififd", 0xA407U, 1U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);
        EXPECT_FALSE(exposure.conflict);

        const MetadataConceptCandidate* time
            = find_role(exposure, MetadataConceptRole::ExposureTime);
        ASSERT_NE(time, nullptr);
        EXPECT_EQ(time->transfer_hint, MetadataConceptTransferHint::Safe);
        EXPECT_TRUE(time->rendered_image_safe);
        EXPECT_TRUE(contains_entry(time->source_entries, exposure_time));
        ASSERT_TRUE(time->has_values);
        EXPECT_NEAR(time->values[0], 0.008, 0.0000001);

        const MetadataConceptCandidate* f_number
            = find_role(exposure, MetadataConceptRole::Aperture);
        ASSERT_NE(f_number, nullptr);
        EXPECT_TRUE(contains_entry(f_number->source_entries, aperture));
        ASSERT_TRUE(f_number->has_values);
        EXPECT_NEAR(f_number->values[0], 5.6, 0.0000001);

        const MetadataConceptCandidate* sensitivity
            = find_role(exposure, MetadataConceptRole::IsoSensitivity);
        ASSERT_NE(sensitivity, nullptr);
        EXPECT_TRUE(contains_entry(sensitivity->source_entries, iso));
        ASSERT_TRUE(sensitivity->has_values);
        EXPECT_DOUBLE_EQ(sensitivity->values[0], 200.0);

        const MetadataConceptCandidate* exposure_bias
            = find_role(exposure, MetadataConceptRole::ExposureBias);
        ASSERT_NE(exposure_bias, nullptr);
        EXPECT_TRUE(contains_entry(exposure_bias->source_entries, bias));
        ASSERT_TRUE(exposure_bias->has_values);
        EXPECT_NEAR(exposure_bias->values[0], -0.333333333333, 0.0000001);

        const MetadataConceptCandidate* exposure_program
            = find_role(exposure, MetadataConceptRole::ExposureProgram);
        ASSERT_NE(exposure_program, nullptr);
        EXPECT_TRUE(contains_entry(exposure_program->source_entries, program));
        ASSERT_TRUE(exposure_program->has_values);
        EXPECT_DOUBLE_EQ(exposure_program->values[0], 3.0);
        EXPECT_EQ(exposure_program->text, "Aperture-priority AE");
        EXPECT_EQ(exposure_program->value_key, "aperturepriorityae");

        const MetadataConceptCandidate* gain_value
            = find_role(exposure, MetadataConceptRole::Gain);
        ASSERT_NE(gain_value, nullptr);
        EXPECT_TRUE(contains_entry(gain_value->source_entries, gain));
        ASSERT_TRUE(gain_value->has_values);
        EXPECT_DOUBLE_EQ(gain_value->values[0], 1.0);
        EXPECT_EQ(gain_value->text, "Low gain up");
        EXPECT_EQ(gain_value->value_key, "lowgainup");
    }

    TEST(MetadataConcepts, ResolvesVendorExposureNamesIntoRoles)
    {
        MetaStore store;
        const EntryId exposure_time = add_exif_urational(&store,
                                                         "mk_canon_shotinfo_0",
                                                         0x0005U, 1U, 125U);
        const EntryId aperture      = add_exif_urational(&store,
                                                         "mk_canon_shotinfo_0",
                                                         0x0004U, 56U, 10U);
        const EntryId bias = add_exif_srational(&store, "mk_canon_shotinfo_0",
                                                0x0006U, -2, 3);
        const EntryId iso  = add_exif_u16(&store, "mk_ricoh_imageinfo_0",
                                          0x0027U, 400U);
        const EntryId program
            = add_exif_u16(&store, "mk_canon_camerasettings_0", 0x0014U, 4U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);

        const MetadataConceptCandidate* time
            = find_role(exposure, MetadataConceptRole::ExposureTime);
        ASSERT_NE(time, nullptr);
        EXPECT_TRUE(contains_entry(time->source_entries, exposure_time));
        ASSERT_TRUE(time->has_values);
        EXPECT_NEAR(time->values[0], 0.008, 0.0000001);

        const MetadataConceptCandidate* f_number
            = find_role(exposure, MetadataConceptRole::Aperture);
        ASSERT_NE(f_number, nullptr);
        EXPECT_TRUE(contains_entry(f_number->source_entries, aperture));
        ASSERT_TRUE(f_number->has_values);
        EXPECT_NEAR(f_number->values[0], 5.6, 0.0000001);

        const MetadataConceptCandidate* exposure_bias
            = find_role(exposure, MetadataConceptRole::ExposureBias);
        ASSERT_NE(exposure_bias, nullptr);
        EXPECT_TRUE(contains_entry(exposure_bias->source_entries, bias));
        ASSERT_TRUE(exposure_bias->has_values);
        EXPECT_NEAR(exposure_bias->values[0], -0.666666666666, 0.0000001);

        const MetadataConceptCandidate* sensitivity
            = find_role(exposure, MetadataConceptRole::IsoSensitivity);
        ASSERT_NE(sensitivity, nullptr);
        EXPECT_TRUE(contains_entry(sensitivity->source_entries, iso));
        ASSERT_TRUE(sensitivity->has_values);
        EXPECT_DOUBLE_EQ(sensitivity->values[0], 400.0);

        const MetadataConceptCandidate* exposure_program
            = find_role(exposure, MetadataConceptRole::ExposureProgram);
        ASSERT_NE(exposure_program, nullptr);
        EXPECT_TRUE(contains_entry(exposure_program->source_entries, program));
        ASSERT_TRUE(exposure_program->has_values);
        EXPECT_DOUBLE_EQ(exposure_program->values[0], 4.0);
        EXPECT_EQ(exposure_program->text, "Manual");
        EXPECT_EQ(exposure_program->value_key, "manual");
    }

    TEST(MetadataConcepts, ResolvesStandardExposureModeNameIntoRoles)
    {
        MetaStore store;
        const EntryId exposure_mode = add_exif_u16(&store, "exififd", 0xA402U,
                                                   1U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);

        const MetadataConceptCandidate* exposure_program
            = find_role(exposure, MetadataConceptRole::ExposureProgram);
        ASSERT_NE(exposure_program, nullptr);
        EXPECT_TRUE(
            contains_entry(exposure_program->source_entries, exposure_mode));
        ASSERT_TRUE(exposure_program->has_values);
        EXPECT_DOUBLE_EQ(exposure_program->values[0], 1.0);
        EXPECT_EQ(exposure_program->text, "Manual");
        EXPECT_EQ(exposure_program->value_key, "manual");
    }

    TEST(MetadataConcepts, ResolvesSonyMakerNoteExposureNameLabels)
    {
        MetaStore store;
        const EntryId program = add_exif_u16(&store, "mk_sony_tag2010i_0",
                                             0x024CU, 5U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);

        const MetadataConceptCandidate* exposure_program
            = find_role(exposure, MetadataConceptRole::ExposureProgram);
        ASSERT_NE(exposure_program, nullptr);
        EXPECT_TRUE(contains_entry(exposure_program->source_entries, program));
        ASSERT_TRUE(exposure_program->has_values);
        EXPECT_DOUBLE_EQ(exposure_program->values[0], 5.0);
        EXPECT_EQ(exposure_program->text, "iAuto");
        EXPECT_EQ(exposure_program->value_key, "iauto");
    }

    TEST(MetadataConcepts, ResolvesAdditionalMakerNoteExposureNameLabels)
    {
        MetaStore store;
        const EntryId pentax_program
            = add_exif_u16(&store, "mk_pentax_aeinfo_0", 0x0006U, 216U);
        const EntryId olympus_program
            = add_exif_u16(&store, "mk_olympus_camerasettings_0", 0x0200U, 3U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);

        const MetadataConceptCandidate* pentax  = nullptr;
        const MetadataConceptCandidate* olympus = nullptr;
        for (size_t i = 0U; i < exposure.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = exposure.candidates[i];
            if (contains_entry(candidate.source_entries, pentax_program)) {
                pentax = &candidate;
            }
            if (contains_entry(candidate.source_entries, olympus_program)) {
                olympus = &candidate;
            }
        }

        ASSERT_NE(pentax, nullptr);
        EXPECT_EQ(pentax->role, MetadataConceptRole::ExposureProgram);
        ASSERT_TRUE(pentax->has_values);
        EXPECT_DOUBLE_EQ(pentax->values[0], 216.0);
        EXPECT_EQ(pentax->text, "HDR");
        EXPECT_EQ(pentax->value_key, "hdr");

        ASSERT_NE(olympus, nullptr);
        EXPECT_EQ(olympus->role, MetadataConceptRole::ExposureProgram);
        ASSERT_TRUE(olympus->has_values);
        EXPECT_DOUBLE_EQ(olympus->values[0], 3.0);
        EXPECT_EQ(olympus->text, "Aperture-priority AE");
        EXPECT_EQ(olympus->value_key, "aperturepriorityae");
    }

    TEST(MetadataConcepts, ResolvesLongTailMakerNoteExposureNameLabels)
    {
        MetaStore store;
        const EntryId ricoh_program = add_exif_u16(&store, "mk_ricoh0", 0x1001U,
                                                   5U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);

        const MetadataConceptCandidate* exposure_program
            = find_role(exposure, MetadataConceptRole::ExposureProgram);
        ASSERT_NE(exposure_program, nullptr);
        EXPECT_TRUE(
            contains_entry(exposure_program->source_entries, ricoh_program));
        ASSERT_TRUE(exposure_program->has_values);
        EXPECT_DOUBLE_EQ(exposure_program->values[0], 5.0);
        EXPECT_EQ(exposure_program->text, "Shutter/aperture priority AE");
        EXPECT_EQ(exposure_program->value_key, "shutteraperturepriorityae");
    }

    TEST(MetadataConcepts, MarksDngExposureAdjustmentsRenderedUnsafe)
    {
        MetaStore store;
        const EntryId baseline = add_exif_srational(&store, "ifd0", 0xC62AU, 1,
                                                    2);
        const EntryId preview_gain = add_exif_urational(&store, "ifd0", 0xC7A8U,
                                                        3U, 2U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);
        uint32_t raw_adjustments = 0U;
        bool saw_baseline        = false;
        bool saw_preview_gain    = false;
        for (size_t i = 0U; i < exposure.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = exposure.candidates[i];
            if (candidate.role != MetadataConceptRole::RawExposureAdjustment) {
                continue;
            }
            raw_adjustments += 1U;
            EXPECT_EQ(candidate.transfer_hint,
                      MetadataConceptTransferHint::RenderedUnsafe);
            EXPECT_TRUE(candidate.compatible_file_safe);
            EXPECT_FALSE(candidate.rendered_image_safe);
            EXPECT_TRUE(candidate.source_bound);
            saw_baseline = saw_baseline
                           || contains_entry(candidate.source_entries,
                                             baseline);
            saw_preview_gain = saw_preview_gain
                               || contains_entry(candidate.source_entries,
                                                 preview_gain);
        }
        EXPECT_GE(raw_adjustments, 2U);
        EXPECT_TRUE(saw_baseline);
        EXPECT_TRUE(saw_preview_gain);
    }

    TEST(MetadataConcepts, ExposesTransferHintsForHostPolicy)
    {
        MetaStore store;
        (void)add_exif_u16(&store, "ifd0", 0x0112U, 6U);
        (void)add_exif_text(&store, "exififd", 0x9003U, "2024:04:19 12:34:56");
        (void)add_exif_urational(&store, "exififd", 0x829AU, 1U, 125U);
        (void)add_exif_srational(&store, "ifd0", 0xC62AU, 1, 2);
        (void)add_exif_text(&store, "gpsifd", 0x0001U, "N");
        const std::array<URational, 3> lat = {
            URational { 41U, 1U },
            URational { 24U, 1U },
            URational { 30U, 1U },
        };
        (void)add_exif_urational_array(&store, "gpsifd", 0x0002U,
                                       std::span<const URational>(lat.data(),
                                                                  lat.size()));
        (void)add_exif_u16(&store, "exififd", 0xA001U, 1U);
        const std::array<uint32_t, 9> color_matrix_values = {
            1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
        };
        (void)add_exif_u32_array(
            &store, "ifd0", 0xC621U,
            std::span<const uint32_t>(color_matrix_values.data(),
                                      color_matrix_values.size()));
        const std::array<uint32_t, 4> active_area_values = {
            10U,
            20U,
            3010U,
            4020U,
        };
        (void)add_exif_u32_array(
            &store, "ifd0", 0xC68DU,
            std::span<const uint32_t>(active_area_values.data(),
                                      active_area_values.size()));
        (void)add_exif_u32(&store, "mk_nikon_distortinfo", 0x0001U, 7U);
        (void)add_exif_u32(&store, "ifd0", 0xC61AU, 512U);
        const std::array<uint32_t, 2> linearization_values = {
            0U,
            65535U,
        };
        (void)add_exif_u32_array(
            &store, "ifd0", 0xC618U,
            std::span<const uint32_t>(linearization_values.data(),
                                      linearization_values.size()));
        (void)add_exif_u32(&store, "mk_google_shotlogdata", 0x0001U, 7U);
        store.finalize();

        const MetadataConceptResult result = resolve_metadata_concepts(store);

        const MetadataConceptResolution* datetime
            = find_concept(result, MetadataConceptKind::DateTime);
        ASSERT_NE(datetime, nullptr);
        const MetadataConceptCandidate* created
            = find_role(*datetime, MetadataConceptRole::Created);
        ASSERT_NE(created, nullptr);
        EXPECT_EQ(created->transfer_hint, MetadataConceptTransferHint::Safe);
        EXPECT_TRUE(created->compatible_file_safe);
        EXPECT_TRUE(created->rendered_image_safe);

        const MetadataConceptResolution* gps
            = find_concept(result, MetadataConceptKind::Gps);
        ASSERT_NE(gps, nullptr);
        const MetadataConceptCandidate* gps_lat
            = find_role(*gps, MetadataConceptRole::Latitude);
        ASSERT_NE(gps_lat, nullptr);
        EXPECT_EQ(gps_lat->transfer_hint, MetadataConceptTransferHint::Safe);
        EXPECT_TRUE(gps_lat->rendered_image_safe);

        const MetadataConceptResolution* exposure
            = find_concept(result, MetadataConceptKind::Exposure);
        ASSERT_NE(exposure, nullptr);
        const MetadataConceptCandidate* exposure_time
            = find_role(*exposure, MetadataConceptRole::ExposureTime);
        ASSERT_NE(exposure_time, nullptr);
        EXPECT_EQ(exposure_time->transfer_hint,
                  MetadataConceptTransferHint::Safe);
        EXPECT_TRUE(exposure_time->rendered_image_safe);
        const MetadataConceptCandidate* raw_adjustment
            = find_role(*exposure, MetadataConceptRole::RawExposureAdjustment);
        ASSERT_NE(raw_adjustment, nullptr);
        EXPECT_EQ(raw_adjustment->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_FALSE(raw_adjustment->rendered_image_safe);

        const MetadataConceptResolution* orientation
            = find_concept(result, MetadataConceptKind::Orientation);
        ASSERT_NE(orientation, nullptr);
        const MetadataConceptCandidate* orientation_value
            = find_role(*orientation, MetadataConceptRole::Orientation);
        ASSERT_NE(orientation_value, nullptr);
        EXPECT_EQ(orientation_value->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        EXPECT_TRUE(orientation_value->compatible_file_safe);
        EXPECT_FALSE(orientation_value->rendered_image_safe);
        EXPECT_TRUE(orientation_value->requires_target_image_spec);

        const MetadataConceptResolution* color
            = find_concept(result, MetadataConceptKind::ColorProfile);
        ASSERT_NE(color, nullptr);
        const MetadataConceptCandidate* color_space
            = find_role(*color, MetadataConceptRole::ColorSpace);
        ASSERT_NE(color_space, nullptr);
        EXPECT_EQ(color_space->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        const MetadataConceptCandidate* matrix
            = find_role(*color, MetadataConceptRole::ColorMatrix);
        ASSERT_NE(matrix, nullptr);
        EXPECT_EQ(matrix->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(matrix->source_bound);
        EXPECT_FALSE(matrix->rendered_image_safe);

        const MetadataConceptResolution* geometry
            = find_concept(result, MetadataConceptKind::Geometry);
        ASSERT_NE(geometry, nullptr);
        const MetadataConceptCandidate* active
            = find_role(*geometry, MetadataConceptRole::ActiveArea);
        ASSERT_NE(active, nullptr);
        EXPECT_EQ(active->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        EXPECT_FALSE(active->rendered_image_safe);

        const MetadataConceptResolution* lens
            = find_concept(result, MetadataConceptKind::LensCorrection);
        ASSERT_NE(lens, nullptr);
        const MetadataConceptCandidate* lens_value
            = find_role(*lens, MetadataConceptRole::LensCorrection);
        ASSERT_NE(lens_value, nullptr);
        EXPECT_EQ(lens_value->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(lens_value->source_bound);

        const MetadataConceptResolution* raw
            = find_concept(result, MetadataConceptKind::RawProcessing);
        ASSERT_NE(raw, nullptr);
        const MetadataConceptCandidate* black
            = find_role(*raw, MetadataConceptRole::BlackLevel);
        ASSERT_NE(black, nullptr);
        EXPECT_EQ(black->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_EQ(black->raw_applicability,
                  MetadataRawApplicabilityState::AppliesToStoredRaw);
        EXPECT_FALSE(black->raw_applicability_requires_storage_context);
        EXPECT_TRUE(black->raw_applicability_can_affect_decode);
        const MetadataConceptCandidate* curve
            = find_role(*raw, MetadataConceptRole::RawValueCurve);
        ASSERT_NE(curve, nullptr);
        EXPECT_EQ(curve->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(curve->compatible_file_safe);
        EXPECT_FALSE(curve->rendered_image_safe);
        EXPECT_EQ(curve->raw_applicability,
                  MetadataRawApplicabilityState::ConditionalOnRawEncoding);
        EXPECT_TRUE(curve->raw_applicability_requires_storage_context);
        EXPECT_TRUE(curve->raw_applicability_can_affect_decode);
        const MetadataConceptCandidate* source
            = find_role(*raw, MetadataConceptRole::ComputationalProcessing);
        ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(source->compatible_file_safe);
        EXPECT_FALSE(source->rendered_image_safe);
        EXPECT_STREQ(metadata_concept_transfer_hint_name(
                         MetadataConceptTransferHint::SourceBound),
                     "source_bound");
        EXPECT_STREQ(metadata_concept_role_name(
                         MetadataConceptRole::RawValueCurve),
                     "raw_value_curve");
        EXPECT_STREQ(metadata_raw_data_encoding_name(
                         MetadataRawDataEncoding::LosslessCompressed),
                     "lossless_compressed");
        EXPECT_STREQ(
            metadata_raw_applicability_state_name(
                MetadataRawApplicabilityState::ConditionalOnRawEncoding),
            "conditional_on_raw_encoding");
    }

    TEST(MetadataConcepts, BindsRawApplicabilityToRawDataDescriptor)
    {
        MetaStore store;
        (void)add_exif_u32(&store, "ifd0", 0xC61AU, 512U);
        const std::array<uint32_t, 2> linearization_values = {
            0U,
            65535U,
        };
        (void)add_exif_u32_array(
            &store, "ifd0", 0xC618U,
            std::span<const uint32_t>(linearization_values.data(),
                                      linearization_values.size()));
        store.finalize();

        MetadataRawDataDescriptor raw_descriptor;
        raw_descriptor.encoding = MetadataRawDataEncoding::LosslessCompressed;
        raw_descriptor.requires_compressed_raw_encoding = true;
        raw_descriptor.has_dimensions                   = true;
        raw_descriptor.width                            = 4000U;
        raw_descriptor.height                           = 3000U;
        const MetadataConceptResolution raw_result = resolve_metadata_concept(
            store, MetadataConceptKind::RawProcessing, raw_descriptor);

        const MetadataConceptCandidate* raw_black
            = find_role(raw_result, MetadataConceptRole::BlackLevel);
        const MetadataConceptCandidate* raw_curve
            = find_role(raw_result, MetadataConceptRole::RawValueCurve);
        ASSERT_NE(raw_black, nullptr);
        ASSERT_NE(raw_curve, nullptr);
        EXPECT_EQ(raw_black->raw_applicability,
                  MetadataRawApplicabilityState::AppliesToStoredRaw);
        EXPECT_EQ(raw_curve->raw_applicability,
                  MetadataRawApplicabilityState::AppliesToStoredRaw);
        EXPECT_FALSE(raw_curve->raw_applicability_requires_storage_context);
        EXPECT_TRUE(raw_curve->raw_applicability_can_affect_decode);

        MetadataRawDataDescriptor secondary_plane_descriptor  = raw_descriptor;
        secondary_plane_descriptor.requires_primary_raw_plane = true;
        secondary_plane_descriptor.has_plane_index            = true;
        secondary_plane_descriptor.plane_index                = 1U;
        const MetadataConceptResolution secondary_plane_result
            = resolve_metadata_concept(store,
                                       MetadataConceptKind::RawProcessing,
                                       secondary_plane_descriptor);
        const MetadataConceptCandidate* secondary_plane_black
            = find_role(secondary_plane_result,
                        MetadataConceptRole::BlackLevel);
        const MetadataConceptCandidate* secondary_plane_curve
            = find_role(secondary_plane_result,
                        MetadataConceptRole::RawValueCurve);
        ASSERT_NE(secondary_plane_black, nullptr);
        ASSERT_NE(secondary_plane_curve, nullptr);
        EXPECT_EQ(secondary_plane_black->raw_applicability,
                  MetadataRawApplicabilityState::AppliesToStoredRaw);
        EXPECT_EQ(secondary_plane_curve->raw_applicability,
                  MetadataRawApplicabilityState::NotApplicableToStoredRaw);

        MetadataRawDataDescriptor unknown_plane_descriptor  = raw_descriptor;
        unknown_plane_descriptor.requires_primary_raw_plane = true;
        const MetadataConceptResolution unknown_plane_result
            = resolve_metadata_concept(store,
                                       MetadataConceptKind::RawProcessing,
                                       unknown_plane_descriptor);
        const MetadataConceptCandidate* unknown_plane_curve
            = find_role(unknown_plane_result,
                        MetadataConceptRole::RawValueCurve);
        ASSERT_NE(unknown_plane_curve, nullptr);
        EXPECT_EQ(unknown_plane_curve->raw_applicability,
                  MetadataRawApplicabilityState::ConditionalOnRawEncoding);
        EXPECT_TRUE(
            unknown_plane_curve->raw_applicability_requires_storage_context);

        MetadataRawDataDescriptor rendered_descriptor;
        rendered_descriptor.encoding = MetadataRawDataEncoding::Rendered;
        const MetadataConceptResolution rendered_result
            = resolve_metadata_concept(store,
                                       MetadataConceptKind::RawProcessing,
                                       rendered_descriptor);

        const MetadataConceptCandidate* rendered_black
            = find_role(rendered_result, MetadataConceptRole::BlackLevel);
        const MetadataConceptCandidate* rendered_curve
            = find_role(rendered_result, MetadataConceptRole::RawValueCurve);
        ASSERT_NE(rendered_black, nullptr);
        ASSERT_NE(rendered_curve, nullptr);
        EXPECT_EQ(rendered_black->raw_applicability,
                  MetadataRawApplicabilityState::NotApplicableToStoredRaw);
        EXPECT_FALSE(
            rendered_black->raw_applicability_requires_storage_context);
        EXPECT_FALSE(rendered_black->raw_applicability_can_affect_decode);
        EXPECT_EQ(rendered_curve->raw_applicability,
                  MetadataRawApplicabilityState::NotApplicableToStoredRaw);
        EXPECT_FALSE(
            rendered_curve->raw_applicability_requires_storage_context);
        EXPECT_FALSE(rendered_curve->raw_applicability_can_affect_decode);
        EXPECT_EQ(metadata_raw_applicability_for_descriptor(
                      MetadataConceptRole::RawValueCurve, rendered_descriptor),
                  MetadataRawApplicabilityState::NotApplicableToStoredRaw);

        MetadataRawDataDescriptor uncompressed_descriptor;
        uncompressed_descriptor.encoding = MetadataRawDataEncoding::Uncompressed;
        uncompressed_descriptor.requires_compressed_raw_encoding = true;
        const MetadataConceptResolution uncompressed_result
            = resolve_metadata_concept(store,
                                       MetadataConceptKind::RawProcessing,
                                       uncompressed_descriptor);

        const MetadataConceptCandidate* uncompressed_black
            = find_role(uncompressed_result, MetadataConceptRole::BlackLevel);
        const MetadataConceptCandidate* uncompressed_curve
            = find_role(uncompressed_result,
                        MetadataConceptRole::RawValueCurve);
        ASSERT_NE(uncompressed_black, nullptr);
        ASSERT_NE(uncompressed_curve, nullptr);
        EXPECT_EQ(uncompressed_black->raw_applicability,
                  MetadataRawApplicabilityState::AppliesToStoredRaw);
        EXPECT_EQ(uncompressed_curve->raw_applicability,
                  MetadataRawApplicabilityState::NotApplicableToStoredRaw);

        MetadataRawDataDescriptor unknown_compression_descriptor;
        unknown_compression_descriptor.encoding
            = MetadataRawDataEncoding::Unknown;
        unknown_compression_descriptor.requires_compressed_raw_encoding = true;
        EXPECT_EQ(metadata_raw_applicability_for_descriptor(
                      MetadataConceptRole::RawValueCurve,
                      unknown_compression_descriptor),
                  MetadataRawApplicabilityState::ConditionalOnRawEncoding);
    }

    TEST(MetadataConcepts, SurfacesColorAndGeometryConflicts)
    {
        {
            MetaStore store;
            (void)add_exif_u16(&store, "exififd", 0xA001U, 1U);
            (void)add_exif_u16(&store, "ifd0", 0xA001U, 2U);
            store.finalize();

            const MetadataConceptResolution color
                = resolve_metadata_concept(store,
                                           MetadataConceptKind::ColorProfile);

            EXPECT_TRUE(color.found);
            EXPECT_TRUE(color.conflict);
            uint32_t conflicts = 0U;
            for (size_t i = 0U; i < color.candidates.size(); ++i) {
                if (color.candidates[i].role == MetadataConceptRole::ColorSpace
                    && color.candidates[i].conflict) {
                    conflicts += 1U;
                }
            }
            EXPECT_GE(conflicts, 2U);
        }

        {
            MetaStore store;
            (void)add_xmp_text(&store, "http://example.invalid/aux/1.0/",
                               "aux:SensorBorderPadding", "64 32 168 128");
            (void)add_xmp_text(&store, "http://example.invalid/aux/1.0/",
                               "aux:OutputBorderPadding", "32 32 168 128");
            store.finalize();

            const MetadataConceptResolution geometry
                = resolve_metadata_concept(store,
                                           MetadataConceptKind::Geometry);

            EXPECT_TRUE(geometry.found);
            EXPECT_TRUE(geometry.conflict);
            uint32_t conflicts = 0U;
            for (size_t i = 0U; i < geometry.candidates.size(); ++i) {
                if (geometry.candidates[i].role == MetadataConceptRole::Border
                    && geometry.candidates[i].conflict) {
                    conflicts += 1U;
                }
            }
            EXPECT_GE(conflicts, 2U);
        }
    }

    TEST(MetadataConcepts, ResolvesSingleConcept)
    {
        MetaStore store;
        const EntryId xmp_color
            = add_xmp_text(&store, "http://ns.adobe.com/photoshop/1.0/",
                           "photoshop:ICCProfile", "Display P3");
        store.finalize();

        const MetadataConceptResolution color
            = resolve_metadata_concept(store,
                                       MetadataConceptKind::ColorProfile);

        EXPECT_TRUE(color.found);
        EXPECT_FALSE(color.conflict);
        EXPECT_EQ(color.preferred_entry, xmp_color);
        const MetadataConceptCandidate* candidate
            = find_role(color, MetadataConceptRole::IccProfile);
        ASSERT_NE(candidate, nullptr);
        EXPECT_TRUE(candidate->preferred);
        EXPECT_EQ(candidate->family, MetadataConceptSourceFamily::Xmp);
        EXPECT_EQ(candidate->text, "Display P3");
    }

    TEST(MetadataConcepts, ResolvesSourceColorTransformAsRenderedUnsafe)
    {
        MetaStore store;
        const EntryId profile
            = add_xmp_text(&store,
                           "http://ns.adobe.com/camera-raw-settings/1.0/",
                           "crs:CameraProfile", "Adobe Color");
        store.finalize();

        const MetadataConceptResolution color
            = resolve_metadata_concept(store,
                                       MetadataConceptKind::ColorProfile);

        EXPECT_TRUE(color.found);
        EXPECT_FALSE(color.conflict);
        EXPECT_EQ(color.preferred_entry, profile);
        const MetadataConceptCandidate* candidate
            = find_role(color, MetadataConceptRole::SourceColorTransform);
        ASSERT_NE(candidate, nullptr);
        EXPECT_TRUE(candidate->preferred);
        EXPECT_EQ(candidate->family, MetadataConceptSourceFamily::Xmp);
        EXPECT_EQ(candidate->semantic,
                  MetadataQuerySemanticKind::SourceColorTransform);
        EXPECT_EQ(candidate->shape, MetadataQueryValueShape::Text);
        EXPECT_EQ(candidate->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(candidate->compatible_file_safe);
        EXPECT_FALSE(candidate->rendered_image_safe);
        EXPECT_TRUE(candidate->source_bound);
        EXPECT_TRUE(contains_entry(candidate->source_entries, profile));
    }

    TEST(MetadataConcepts, ResolvesSourceProcessingSubrolesAsSourceBound)
    {
        MetaStore store;
        const EntryId computational
            = add_exif_u32(&store, "mk_google_shotlogdata", 0x0001U, 7U);
        const EntryId thermal = add_exif_u32(&store, "mk_dji_thermalparams",
                                             0x0048U, 98U);
        const EntryId stitch  = add_exif_u32(&store, "mk_microsoft_stitch",
                                             0x0003U, 12U);
        store.finalize();

        const MetadataConceptResolution raw
            = resolve_metadata_concept(store,
                                       MetadataConceptKind::RawProcessing);

        EXPECT_TRUE(raw.found);
        const MetadataConceptCandidate* computational_candidate
            = find_role(raw, MetadataConceptRole::ComputationalProcessing);
        ASSERT_NE(computational_candidate, nullptr);
        EXPECT_EQ(computational_candidate->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(computational_candidate->source_bound);
        EXPECT_TRUE(contains_entry(computational_candidate->source_entries,
                                   computational));

        const MetadataConceptCandidate* thermal_candidate
            = find_role(raw, MetadataConceptRole::ThermalProcessing);
        ASSERT_NE(thermal_candidate, nullptr);
        EXPECT_EQ(thermal_candidate->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(thermal_candidate->source_bound);
        EXPECT_TRUE(contains_entry(thermal_candidate->source_entries, thermal));

        const MetadataConceptCandidate* stitch_candidate
            = find_role(raw, MetadataConceptRole::StitchProcessing);
        ASSERT_NE(stitch_candidate, nullptr);
        EXPECT_EQ(stitch_candidate->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(stitch_candidate->source_bound);
        EXPECT_TRUE(contains_entry(stitch_candidate->source_entries, stitch));
    }

    TEST(MetadataConcepts, ResolvesBmffContainerGraphPolicyAsSourceBound)
    {
        MetaStore store;
        const EntryId content_bound
            = add_bmff_text(&store, "scene.content_bound_metadata_policy",
                            "requires_target_rewrite");
        const EntryId primary_component
            = add_bmff_text(&store,
                            "scene.primary_graph_component_metadata_policy",
                            "requires_target_rewrite");
        const EntryId component_content_bound
            = add_bmff_text(&store, "scene.component.metadata_policy",
                            "requires_target_rewrite");
        const EntryId multi_image
            = add_bmff_u8(&store, "scene.multi_image_candidate", 1U);
        const EntryId multi_image_policy
            = add_bmff_text(&store, "scene.multi_image_policy",
                            "requires_target_rewrite");
        const EntryId primary_component_multi_image = add_bmff_u8(
            &store, "scene.primary_graph_component_multi_image_candidate", 1U);
        const EntryId primary_component_multi_image_policy
            = add_bmff_text(&store,
                            "scene.primary_graph_component_multi_image_policy",
                            "requires_target_rewrite");
        const EntryId component_multi_image
            = add_bmff_u8(&store, "scene.component.multi_image_candidate", 1U);
        const EntryId component_multi_image_policy
            = add_bmff_text(&store, "scene.component.multi_image_policy",
                            "requires_target_rewrite");
        const EntryId derived_construction
            = add_bmff_text(&store, "derived_image.construction", "grid");
        const EntryId tiled_configuration
            = add_bmff_text(&store, "tiled_image.configuration", "tiled");
        store.finalize();

        const MetadataConceptResolution graph
            = resolve_metadata_concept(store,
                                       MetadataConceptKind::ContainerGraph);

        EXPECT_TRUE(graph.found);
        EXPECT_FALSE(graph.conflict);
        const MetadataConceptCandidate* content_candidate
            = find_role(graph, MetadataConceptRole::ContentBoundMetadata);
        ASSERT_NE(content_candidate, nullptr);
        EXPECT_EQ(content_candidate->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(content_candidate->compatible_file_safe);
        EXPECT_FALSE(content_candidate->rendered_image_safe);
        EXPECT_TRUE(content_candidate->source_bound);
        EXPECT_EQ(content_candidate->text, "requires_target_rewrite");
        EXPECT_TRUE(
            contains_entry(content_candidate->source_entries, content_bound));
        EXPECT_TRUE(contains_entry(graph.source_entries, primary_component));
        EXPECT_TRUE(
            contains_entry(graph.source_entries, component_content_bound));

        const MetadataConceptCandidate* multi_candidate
            = find_role(graph, MetadataConceptRole::MultiImageScene);
        ASSERT_NE(multi_candidate, nullptr);
        EXPECT_EQ(multi_candidate->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(multi_candidate->has_numeric);
        EXPECT_EQ(multi_candidate->numeric_count, 1U);
        EXPECT_EQ(multi_candidate->numeric[0], 1.0);
        EXPECT_TRUE(
            contains_entry(multi_candidate->source_entries, multi_image));
        EXPECT_TRUE(contains_entry(graph.source_entries, multi_image_policy));
        EXPECT_TRUE(contains_entry(graph.source_entries,
                                   primary_component_multi_image));
        EXPECT_TRUE(contains_entry(graph.source_entries,
                                   primary_component_multi_image_policy));
        EXPECT_TRUE(
            contains_entry(graph.source_entries, component_multi_image));
        EXPECT_TRUE(
            contains_entry(graph.source_entries, component_multi_image_policy));

        const MetadataConceptCandidate* derived_candidate
            = find_role(graph, MetadataConceptRole::DerivedImageConstruction);
        ASSERT_NE(derived_candidate, nullptr);
        EXPECT_EQ(derived_candidate->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(derived_candidate->compatible_file_safe);
        EXPECT_FALSE(derived_candidate->rendered_image_safe);
        EXPECT_TRUE(derived_candidate->source_bound);
        EXPECT_EQ(derived_candidate->text, "grid");
        EXPECT_TRUE(contains_entry(derived_candidate->source_entries,
                                   derived_construction));

        const MetadataConceptCandidate* tiled_candidate
            = find_role(graph, MetadataConceptRole::TiledImageConfiguration);
        ASSERT_NE(tiled_candidate, nullptr);
        EXPECT_EQ(tiled_candidate->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(tiled_candidate->compatible_file_safe);
        EXPECT_FALSE(tiled_candidate->rendered_image_safe);
        EXPECT_TRUE(tiled_candidate->source_bound);
        EXPECT_EQ(tiled_candidate->text, "tiled");
        EXPECT_TRUE(contains_entry(tiled_candidate->source_entries,
                                   tiled_configuration));

        const MetadataConceptResult all = resolve_metadata_concepts(store);
        const MetadataConceptResolution* graph_in_all
            = find_concept(all, MetadataConceptKind::ContainerGraph);
        ASSERT_NE(graph_in_all, nullptr);
        EXPECT_TRUE(graph_in_all->found);
    }

    TEST(MetadataConcepts, InterpretsIptcVocabularyRegistryAndRegionRecords)
    {
        MetaStore store;
        const std::string_view ext
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
        (void)add_xmp_text(&store, ext, "AboutCvTerm[1]/CvId",
                           "https://example.test/vocabulary");
        (void)add_xmp_text(&store, ext, "AboutCvTerm[1]/CvTermId",
                           "https://example.test/vocabulary/culture");
        (void)add_xmp_text(&store, ext,
                           "AboutCvTerm[1]/CvTermName[@xml:lang=en-US]",
                           "Culture");
        (void)add_xmp_text(&store, ext, "RegistryId[1]/RegItemId", "asset-001");
        (void)add_xmp_text(&store, ext, "RegistryId[1]/RegOrgId",
                           "registry.example");
        (void)add_xmp_text(&store, ext, "RegistryId[2]/RegItemId", "asset-002");
        (void)add_xmp_text(&store, ext,
                           "ImageRegion[1]/Name[@xml:lang=x-default]",
                           "Main subject");
        (void)add_xmp_text(&store, ext, "ImageRegion[1]/rId", "region-1");
        (void)add_xmp_text(&store, ext,
                           "ImageRegion[1]/rCtype[1]/xmp:Identifier[1]",
                           "face");
        (void)add_xmp_text(&store, ext,
                           "ImageRegion[1]/rRole[1]/Name[@xml:lang=x-default]",
                           "subject");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);
        const MetadataConceptCandidate* term_name = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ControlledVocabularyTerm,
            MetadataConceptRole::TermName, "AboutCvTerm[1]");
        const MetadataConceptCandidate* registry_one = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::RegistryEntry,
            MetadataConceptRole::RegistryItemIdentifier, "RegistryEntry[1]");
        const MetadataConceptCandidate* registry_two = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::RegistryEntry,
            MetadataConceptRole::RegistryItemIdentifier, "RegistryEntry[2]");
        const MetadataConceptCandidate* region_name = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageRegion,
            MetadataConceptRole::RegionName, "ImageRegion[1]");
        const MetadataConceptCandidate* content_type = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageRegion,
            MetadataConceptRole::RegionContentTypeIdentifier,
            "ImageRegion[1]/ContentType[1]");
        const MetadataConceptCandidate* region_role = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageRegion,
            MetadataConceptRole::RegionRoleName, "ImageRegion[1]/Role[1]");

        EXPECT_TRUE(descriptive.found);
        EXPECT_FALSE(descriptive.conflict);
        ASSERT_NE(term_name, nullptr);
        ASSERT_NE(registry_one, nullptr);
        ASSERT_NE(registry_two, nullptr);
        ASSERT_NE(region_name, nullptr);
        ASSERT_NE(content_type, nullptr);
        ASSERT_NE(region_role, nullptr);
        EXPECT_EQ(term_name->semantic, MetadataQuerySemanticKind::Taxonomy);
        EXPECT_EQ(term_name->language, "en-us");
        EXPECT_EQ(registry_one->semantic, MetadataQuerySemanticKind::Registry);
        EXPECT_EQ(registry_one->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_EQ(region_name->semantic,
                  MetadataQuerySemanticKind::ImageRegion);
        EXPECT_EQ(region_name->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        EXPECT_TRUE(region_name->requires_target_image_spec);
        EXPECT_EQ(content_type->text, "face");
        EXPECT_EQ(region_role->language, "x-default");
        EXPECT_STREQ(metadata_concept_record_kind_name(
                         MetadataConceptRecordKind::ControlledVocabularyTerm),
                     "controlled_vocabulary_term");
    }

    TEST(MetadataConcepts, NormalizesScopedIptcImageRegionBoundaries)
    {
        MetaStore store;
        const std::string_view ext
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
        (void)add_xmp_text(&store, ext, "ImageRegion[1]/RegionBoundary/rbShape",
                           "rectangle");
        (void)add_xmp_text(&store, ext, "ImageRegion[1]/RegionBoundary/rbUnit",
                           "pixel");
        (void)add_xmp_text(&store, ext, "ImageRegion[1]/RegionBoundary/rbX",
                           "120");
        (void)add_xmp_text(&store, ext, "ImageRegion[1]/RegionBoundary/rbY",
                           "45");
        (void)add_xmp_text(&store, ext, "ImageRegion[1]/RegionBoundary/rbW",
                           "640");
        (void)add_xmp_text(&store, ext, "ImageRegion[1]/RegionBoundary/rbH",
                           "480");
        (void)add_xmp_text(&store, ext, "ImageRegion[2]/RegionBoundary/rbShape",
                           "circle");
        (void)add_xmp_text(&store, ext, "ImageRegion[2]/RegionBoundary/rbUnit",
                           "relative");
        (void)add_xmp_text(&store, ext, "ImageRegion[2]/RegionBoundary/rbX",
                           "0.5");
        (void)add_xmp_text(&store, ext, "ImageRegion[2]/RegionBoundary/rbY",
                           "0.25");
        (void)add_xmp_text(&store, ext, "ImageRegion[2]/RegionBoundary/rbRx",
                           "0.1");
        (void)add_xmp_text(&store, ext, "ImageRegion[3]/RegionBoundary/rbShape",
                           "polygon");
        (void)add_xmp_text(&store, ext, "ImageRegion[3]/RegionBoundary/rbUnit",
                           "relative");
        (void)add_xmp_text(&store, ext,
                           "ImageRegion[3]/RegionBoundary/rbVertices[1]/rbX",
                           "0.1");
        (void)add_xmp_text(&store, ext,
                           "ImageRegion[3]/RegionBoundary/rbVertices[1]/rbY",
                           "0.2");
        (void)add_xmp_text(&store, ext,
                           "ImageRegion[3]/RegionBoundary/rbVertices[2]/rbX",
                           "0.8");
        (void)add_xmp_text(&store, ext,
                           "ImageRegion[3]/RegionBoundary/rbVertices[2]/rbY",
                           "0.2");
        (void)add_xmp_text(&store, ext,
                           "ImageRegion[3]/RegionBoundary/rbVertices[3]/rbX",
                           "0.5");
        (void)add_xmp_text(&store, ext,
                           "ImageRegion[3]/RegionBoundary/rbVertices[3]/rbY",
                           "0.9");
        (void)add_xmp_text(&store, ext, "ImageRegion[4]/RegionBoundary/rbShape",
                           "rectangle");
        (void)add_xmp_text(&store, ext, "ImageRegion[4]/RegionBoundary/rbUnit",
                           "pixel");
        (void)add_xmp_text(&store, ext, "ImageRegion[4]/RegionBoundary/rbX",
                           "8");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);
        const MetadataConceptCandidate* rectangle = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageRegionBoundary,
            MetadataConceptRole::RegionBoundary, "ImageRegion[1]/Boundary");
        const MetadataConceptCandidate* circle = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageRegionBoundary,
            MetadataConceptRole::RegionBoundary, "ImageRegion[2]/Boundary");
        const MetadataConceptCandidate* polygon = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageRegionBoundary,
            MetadataConceptRole::RegionBoundary, "ImageRegion[3]/Boundary");
        const MetadataConceptCandidate* first_vertex = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageRegionBoundary,
            MetadataConceptRole::RegionBoundaryVertex,
            "ImageRegion[3]/Boundary/Vertex[1]");
        const MetadataConceptCandidate* incomplete = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageRegionBoundary,
            MetadataConceptRole::RegionBoundary, "ImageRegion[4]/Boundary");
        const MetadataConceptCandidate* incomplete_x = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ImageRegionBoundary,
            MetadataConceptRole::RegionBoundaryX, "ImageRegion[4]/Boundary");

        EXPECT_TRUE(descriptive.found);
        EXPECT_FALSE(descriptive.conflict);
        ASSERT_NE(rectangle, nullptr);
        ASSERT_NE(circle, nullptr);
        ASSERT_NE(polygon, nullptr);
        ASSERT_NE(first_vertex, nullptr);
        EXPECT_EQ(incomplete, nullptr);
        ASSERT_NE(incomplete_x, nullptr);
        EXPECT_EQ(incomplete_x->shape, MetadataQueryValueShape::Scalar);
        EXPECT_TRUE(incomplete_x->has_numeric);
        EXPECT_DOUBLE_EQ(incomplete_x->numeric[0], 8.0);

        EXPECT_EQ(rectangle->shape, MetadataQueryValueShape::Rect);
        EXPECT_EQ(rectangle->image_region_shape,
                  MetadataImageRegionShape::Rectangle);
        EXPECT_EQ(rectangle->image_region_coordinate_unit,
                  MetadataImageRegionCoordinateUnit::Pixel);
        EXPECT_TRUE(rectangle->has_rect);
        EXPECT_DOUBLE_EQ(rectangle->rect[0], 120.0);
        EXPECT_DOUBLE_EQ(rectangle->rect[1], 45.0);
        EXPECT_DOUBLE_EQ(rectangle->rect[2], 640.0);
        EXPECT_DOUBLE_EQ(rectangle->rect[3], 480.0);
        EXPECT_EQ(rectangle->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);

        EXPECT_EQ(circle->shape, MetadataQueryValueShape::Vec3);
        EXPECT_EQ(circle->image_region_shape, MetadataImageRegionShape::Circle);
        EXPECT_EQ(circle->image_region_coordinate_unit,
                  MetadataImageRegionCoordinateUnit::Relative);
        ASSERT_TRUE(circle->has_values);
        ASSERT_EQ(circle->values.size(), 3U);
        EXPECT_DOUBLE_EQ(circle->values[2], 0.1);

        EXPECT_EQ(polygon->shape, MetadataQueryValueShape::VectorSet);
        EXPECT_EQ(polygon->image_region_shape,
                  MetadataImageRegionShape::Polygon);
        ASSERT_TRUE(polygon->has_values);
        ASSERT_EQ(polygon->values.size(), 6U);
        EXPECT_DOUBLE_EQ(polygon->values[0], 0.1);
        EXPECT_DOUBLE_EQ(polygon->values[5], 0.9);
        EXPECT_EQ(first_vertex->shape, MetadataQueryValueShape::Vec2);
        ASSERT_TRUE(first_vertex->has_values);
        EXPECT_DOUBLE_EQ(first_vertex->values[0], 0.1);
        EXPECT_DOUBLE_EQ(first_vertex->values[1], 0.2);
        EXPECT_STREQ(metadata_image_region_shape_name(
                         MetadataImageRegionShape::Rectangle),
                     "rectangle");
        EXPECT_STREQ(metadata_image_region_coordinate_unit_name(
                         MetadataImageRegionCoordinateUnit::Relative),
                     "relative");
        EXPECT_STREQ(metadata_concept_record_kind_name(
                         MetadataConceptRecordKind::ImageRegionBoundary),
                     "image_region_boundary");
    }

    TEST(MetadataConcepts, InterpretsXmpMmLineageHistoryAndPantryRecords)
    {
        MetaStore store;
        const std::string_view mm = "http://ns.adobe.com/xap/1.0/mm/";
        (void)add_xmp_text(&store, mm, "DerivedFrom/stRef:documentID",
                           "xmp.did:base");
        (void)add_xmp_text(&store, mm, "DerivedFrom/stRef:filePath",
                           "/assets/base.psd");
        (void)add_xmp_text(&store, mm, "Ingredients[1]/stRef:documentID",
                           "xmp.did:ingredient-1");
        (void)add_xmp_text(&store, mm, "Ingredients[2]/stRef:documentID",
                           "xmp.did:ingredient-2");
        (void)add_xmp_text(&store, mm, "History[1]/stEvt:action", "saved");
        (void)add_xmp_text(&store, mm, "History[1]/stEvt:softwareAgent",
                           "OpenMeta");
        (void)add_xmp_text(&store, mm, "History[2]/stEvt:action", "exported");
        (void)add_xmp_text(&store, mm,
                           "Manifest[1]/stMfs:reference/stRef:filePath",
                           "/assets/linked.tif");
        (void)add_xmp_text(&store, mm, "Manifest[1]/stMfs:linkForm",
                           "ReferenceStream");
        (void)add_xmp_text(&store, mm, "Manifest[1]/stMfs:placedXResolution",
                           "300");
        (void)add_xmp_text(&store, mm,
                           "Versions[1]/stVer:event/stEvt:parameters",
                           "quality=final");
        (void)add_xmp_text(&store, mm, "Versions[1]/stVer:comments",
                           "Approved master");
        (void)add_xmp_text(&store, mm, "Versions[1]/stVer:modifier", "Editor");
        (void)add_xmp_text(&store, mm, "Versions[1]/stVer:modifyDate",
                           "2026-07-21T10:30:00Z");
        (void)add_xmp_text(&store, mm, "Versions[1]/stVer:version", "7");
        (void)add_xmp_text(&store, mm, "Pantry[1]/InstanceID",
                           "xmp.iid:pantry-1");
        (void)add_xmp_text(&store, mm, "Pantry[1]/dc:format", "image/tiff");
        (void)add_xmp_text(&store, mm, "Pantry[1]/DerivedFrom/stRef:documentID",
                           "xmp.did:pantry-source");
        (void)add_xmp_text(&store, mm,
                           "Pantry[1]/Ingredients[1]/stRef:filePath",
                           "/assets/pantry-layer.tif");
        (void)add_xmp_text(
            &store, mm, "Pantry[1]/Manifest[1]/stMfs:reference/stRef:manageUI",
            "https://example.invalid/pantry");
        (void)add_xmp_text(&store, mm, "Pantry[1]/History[1]/stEvt:action",
                           "copied");
        (void)add_xmp_text(
            &store, mm, "Pantry[1]/Versions[1]/stVer:event/stEvt:softwareAgent",
            "Pantry Writer");
        (void)add_xmp_text(&store, mm, "Pantry[1]/Versions[1]/stVer:comments",
                           "Embedded component");
        (void)add_xmp_text(&store, mm, "Pantry[2]/ns:format",
                           "application/x-vendor");
        (void)add_xmp_text(&store, mm, "Pantry[2]/ns:payload/InstanceID",
                           "vendor-instance");
        (void)add_xmp_text(&store, mm,
                           "Pantry[2]/ns:DerivedFrom/ns:documentID",
                           "vendor-document");
        store.finalize();

        const MetadataConceptResolution descriptive
            = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);
        const MetadataConceptCandidate* derived = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ResourceReference,
            MetadataConceptRole::DocumentIdentifier, "DerivedFrom");
        const MetadataConceptCandidate* ingredient_one = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ResourceReference,
            MetadataConceptRole::DocumentIdentifier, "Ingredient[1]");
        const MetadataConceptCandidate* ingredient_two = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ResourceReference,
            MetadataConceptRole::DocumentIdentifier, "Ingredient[2]");
        const MetadataConceptCandidate* event = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ResourceEvent,
            MetadataConceptRole::EventAction, "HistoryEvent[1]");
        const MetadataConceptCandidate* agent = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ResourceEvent,
            MetadataConceptRole::SoftwareAgent, "HistoryEvent[1]");
        const MetadataConceptCandidate* pantry = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::PantryItem,
            MetadataConceptRole::InstanceIdentifier, "PantryItem[1]");
        const MetadataConceptCandidate* format = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::PantryItem,
            MetadataConceptRole::Format, "PantryItem[1]");
        const MetadataConceptCandidate* pantry_derived = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ResourceReference,
            MetadataConceptRole::DocumentIdentifier,
            "PantryItem[1]/DerivedFrom");
        const MetadataConceptCandidate* pantry_ingredient
            = find_record_role_scope(
                descriptive, MetadataConceptRecordKind::ResourceReference,
                MetadataConceptRole::FilePath, "PantryItem[1]/Ingredient[1]");
        const MetadataConceptCandidate* pantry_manifest = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ResourceReference,
            MetadataConceptRole::ManageUi,
            "PantryItem[1]/Manifest[1]/Reference");
        const MetadataConceptCandidate* pantry_history = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ResourceEvent,
            MetadataConceptRole::EventAction, "PantryItem[1]/HistoryEvent[1]");
        const MetadataConceptCandidate* pantry_version_event
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::ResourceEvent,
                                     MetadataConceptRole::SoftwareAgent,
                                     "PantryItem[1]/Version[1]/Event");
        const MetadataConceptCandidate* pantry_version = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::Version,
            MetadataConceptRole::VersionComments, "PantryItem[1]/Version[1]");
        const MetadataConceptCandidate* vendor_format = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::PantryItem,
            MetadataConceptRole::Format, "PantryItem[2]");
        const MetadataConceptCandidate* vendor_instance
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::PantryItem,
                                     MetadataConceptRole::InstanceIdentifier,
                                     "PantryItem[2]");
        const MetadataConceptCandidate* vendor_reference
            = find_record_role_scope(
                descriptive, MetadataConceptRecordKind::ResourceReference,
                MetadataConceptRole::DocumentIdentifier,
                "PantryItem[2]/DerivedFrom");
        const MetadataConceptCandidate* manifest = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ResourceReference,
            MetadataConceptRole::FilePath, "Manifest[1]/Reference");
        const MetadataConceptCandidate* version_event = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ResourceEvent,
            MetadataConceptRole::EventParameters, "Version[1]/Event");
        const MetadataConceptCandidate* manifest_link = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::ManifestItem,
            MetadataConceptRole::LinkForm, "Manifest[1]");
        const MetadataConceptCandidate* manifest_resolution
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::ManifestItem,
                                     MetadataConceptRole::PlacedXResolution,
                                     "Manifest[1]");
        const MetadataConceptCandidate* version_comments
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::Version,
                                     MetadataConceptRole::VersionComments,
                                     "Version[1]");
        const MetadataConceptCandidate* version_modifier
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::Version,
                                     MetadataConceptRole::VersionModifier,
                                     "Version[1]");
        const MetadataConceptCandidate* version_date = find_record_role_scope(
            descriptive, MetadataConceptRecordKind::Version,
            MetadataConceptRole::LastModifiedDate, "Version[1]");
        const MetadataConceptCandidate* version_identifier
            = find_record_role_scope(descriptive,
                                     MetadataConceptRecordKind::Version,
                                     MetadataConceptRole::VersionIdentifier,
                                     "Version[1]");

        EXPECT_TRUE(descriptive.found);
        EXPECT_FALSE(descriptive.conflict);
        ASSERT_NE(derived, nullptr);
        ASSERT_NE(ingredient_one, nullptr);
        ASSERT_NE(ingredient_two, nullptr);
        ASSERT_NE(event, nullptr);
        ASSERT_NE(agent, nullptr);
        ASSERT_NE(pantry, nullptr);
        ASSERT_NE(format, nullptr);
        ASSERT_NE(pantry_derived, nullptr);
        ASSERT_NE(pantry_ingredient, nullptr);
        ASSERT_NE(pantry_manifest, nullptr);
        ASSERT_NE(pantry_history, nullptr);
        ASSERT_NE(pantry_version_event, nullptr);
        ASSERT_NE(pantry_version, nullptr);
        EXPECT_EQ(vendor_format, nullptr);
        EXPECT_EQ(vendor_instance, nullptr);
        EXPECT_EQ(vendor_reference, nullptr);
        ASSERT_NE(manifest, nullptr);
        ASSERT_NE(version_event, nullptr);
        ASSERT_NE(manifest_link, nullptr);
        ASSERT_NE(manifest_resolution, nullptr);
        ASSERT_NE(version_comments, nullptr);
        ASSERT_NE(version_modifier, nullptr);
        ASSERT_NE(version_date, nullptr);
        ASSERT_NE(version_identifier, nullptr);
        EXPECT_EQ(derived->semantic,
                  MetadataQuerySemanticKind::DocumentLineage);
        EXPECT_EQ(derived->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(derived->source_bound);
        EXPECT_EQ(ingredient_one->text, "xmp.did:ingredient-1");
        EXPECT_EQ(ingredient_two->text, "xmp.did:ingredient-2");
        EXPECT_EQ(event->semantic, MetadataQuerySemanticKind::DocumentHistory);
        EXPECT_EQ(event->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_EQ(agent->text, "OpenMeta");
        EXPECT_EQ(pantry->semantic, MetadataQuerySemanticKind::DocumentLineage);
        EXPECT_EQ(format->text, "image/tiff");
        EXPECT_EQ(pantry_derived->text, "xmp.did:pantry-source");
        EXPECT_EQ(pantry_derived->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(pantry_derived->source_bound);
        EXPECT_EQ(pantry_ingredient->text, "/assets/pantry-layer.tif");
        EXPECT_EQ(pantry_manifest->text, "https://example.invalid/pantry");
        EXPECT_EQ(pantry_history->semantic,
                  MetadataQuerySemanticKind::DocumentHistory);
        EXPECT_EQ(pantry_version_event->semantic,
                  MetadataQuerySemanticKind::DocumentHistory);
        EXPECT_EQ(pantry_version->text, "Embedded component");
        EXPECT_EQ(manifest->text, "/assets/linked.tif");
        EXPECT_EQ(version_event->semantic,
                  MetadataQuerySemanticKind::DocumentHistory);
        EXPECT_EQ(manifest_link->semantic,
                  MetadataQuerySemanticKind::DocumentLineage);
        EXPECT_EQ(manifest_link->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_EQ(manifest_resolution->text, "300");
        EXPECT_EQ(version_comments->semantic,
                  MetadataQuerySemanticKind::DocumentHistory);
        EXPECT_EQ(version_comments->text, "Approved master");
        EXPECT_EQ(version_modifier->text, "Editor");
        EXPECT_EQ(version_date->text, "2026-07-21T10:30:00Z");
        EXPECT_EQ(version_identifier->text, "7");
        EXPECT_STREQ(metadata_concept_role_name(
                         MetadataConceptRole::SoftwareAgent),
                     "software_agent");
    }

}  // namespace
}  // namespace openmeta
