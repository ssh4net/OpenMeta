// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_fuzzy_search.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openmeta {
namespace {

    static EntryId add_xmp_text(MetaStore* store, std::string_view ns,
                                std::string_view path,
                                EntryFlags flags = EntryFlags::None)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key   = make_xmp_property_key(store->arena(), ns, path);
        entry.value = make_text(store->arena(), "value", TextEncoding::Utf8);
        entry.flags = flags;
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static MetadataFuzzySearchResult
    search_with_score(const MetaStore& store, std::string_view query,
                      uint8_t minimum_score, uint32_t max_results = 16U)
    {
        MetadataFuzzySearchOptions options;
        options.minimum_score = minimum_score;
        options.max_results   = max_results;
        return fuzzy_search_metadata(store, query, options);
    }

}  // namespace

TEST(MetadataFuzzySearch, ReportsAvailabilityAndDisabledStatus)
{
    MetaStore store;
    add_xmp_text(&store, "http://purl.org/dc/elements/1.1/", "dc:creator");
    store.finalize();

    const MetadataFuzzySearchResult result = fuzzy_search_metadata(store,
                                                                   "creator");
#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
    EXPECT_TRUE(metadata_fuzzy_search_available());
    EXPECT_EQ(result.status, MetadataFuzzySearchStatus::Ok);
#else
    EXPECT_FALSE(metadata_fuzzy_search_available());
    EXPECT_EQ(result.status, MetadataFuzzySearchStatus::FeatureUnavailable);
#endif
}

TEST(MetadataFuzzySearch, ValidatesBoundsAndAsciiPolicy)
{
    MetaStore store;
    add_xmp_text(&store, "http://purl.org/dc/elements/1.1/", "dc:creator");
    store.finalize();

    EXPECT_EQ(fuzzy_search_metadata(store, "").status,
              MetadataFuzzySearchStatus::EmptyQuery);
    EXPECT_EQ(fuzzy_search_metadata(store, "wb").status,
              MetadataFuzzySearchStatus::QueryTooShort);
    const std::string long_query(kMetadataFuzzySearchMaxQueryBytes + 1U, 'a');
    EXPECT_EQ(fuzzy_search_metadata(store, long_query).status,
              MetadataFuzzySearchStatus::QueryTooLong);
    EXPECT_EQ(fuzzy_search_metadata(store, "cr\xC3\xA9"
                                           "ator")
                  .status,
              MetadataFuzzySearchStatus::UnsupportedQueryText);

    MetadataFuzzySearchOptions options;
    options.minimum_score = 49U;
    EXPECT_EQ(fuzzy_search_metadata(store, "creator", options).status,
              MetadataFuzzySearchStatus::InvalidOptions);
    options.minimum_score = 80U;
    options.max_results   = 0U;
    EXPECT_EQ(fuzzy_search_metadata(store, "creator", options).status,
              MetadataFuzzySearchStatus::InvalidOptions);
    options.max_results = kMetadataFuzzySearchMaxResults + 1U;
    EXPECT_EQ(fuzzy_search_metadata(store, "creator", options).status,
              MetadataFuzzySearchStatus::InvalidOptions);
}

TEST(MetadataFuzzySearch, RanksExactAliasAndTypoMatches)
{
    if (!metadata_fuzzy_search_available()) {
        GTEST_SKIP() << "RapidFuzz metadata search is not enabled";
    }

    MetaStore store;
    const EntryId creator     = add_xmp_text(&store,
                                             "http://purl.org/dc/elements/1.1/",
                                             "dc:creator");
    const EntryId description = add_xmp_text(&store,
                                             "http://purl.org/dc/elements/1.1/",
                                             "dc:description");
    const EntryId exposure    = add_xmp_text(&store,
                                             "http://ns.adobe.com/exif/1.0/",
                                             "exif:ExposureTime");
    const EntryId orientation = add_xmp_text(&store,
                                             "http://ns.adobe.com/tiff/1.0/",
                                             "tiff:Orientation");
    store.finalize();

    MetadataFuzzySearchResult result = search_with_score(store, "creator", 80U);
    ASSERT_EQ(result.status, MetadataFuzzySearchStatus::Ok);
    ASSERT_FALSE(result.matches.empty());
    EXPECT_EQ(result.matches[0].entry_id, creator);
    EXPECT_EQ(result.matches[0].match_kind,
              MetadataFuzzySearchMatchKind::Exact);
    EXPECT_EQ(result.matches[0].score, 100U);

    result = search_with_score(store, "author", 80U);
    ASSERT_FALSE(result.matches.empty());
    EXPECT_EQ(result.matches[0].entry_id, creator);
    EXPECT_EQ(result.matches[0].match_kind,
              MetadataFuzzySearchMatchKind::Alias);
    EXPECT_EQ(result.matches[0].score, 96U);

    result = search_with_score(store, "captin", 80U);
    ASSERT_FALSE(result.matches.empty());
    EXPECT_EQ(result.matches[0].entry_id, description);
    EXPECT_EQ(result.matches[0].match_kind,
              MetadataFuzzySearchMatchKind::Alias);

    result = search_with_score(store, "expsoure time", 80U);
    ASSERT_FALSE(result.matches.empty());
    EXPECT_EQ(result.matches[0].entry_id, exposure);

    result = search_with_score(store, "orentation", 80U);
    ASSERT_FALSE(result.matches.empty());
    EXPECT_EQ(result.matches[0].entry_id, orientation);
}

TEST(MetadataFuzzySearch, EnforcesDeterministicTopKAndSkipsDeletedEntries)
{
    if (!metadata_fuzzy_search_available()) {
        GTEST_SKIP() << "RapidFuzz metadata search is not enabled";
    }

    MetaStore store;
    const EntryId first  = add_xmp_text(&store, "http://example.invalid/one/",
                                        "aux:Creator");
    const EntryId second = add_xmp_text(&store, "http://example.invalid/two/",
                                        "aux:Creator");
    add_xmp_text(&store, "http://example.invalid/deleted/", "aux:Creator",
                 EntryFlags::Deleted);
    const EntryId third = add_xmp_text(&store, "http://example.invalid/three/",
                                       "aux:Creator");
    store.finalize();

    const MetadataFuzzySearchResult result = search_with_score(store, "creator",
                                                               80U, 2U);
    ASSERT_EQ(result.status, MetadataFuzzySearchStatus::Ok);
    EXPECT_EQ(result.examined_entry_count, 3U);
    EXPECT_EQ(result.qualified_match_count, 3U);
    EXPECT_TRUE(result.truncated);
    ASSERT_EQ(result.matches.size(), 2U);
    EXPECT_EQ(result.matches[0].entry_id, first);
    EXPECT_EQ(result.matches[1].entry_id, second);
    EXPECT_LT(second, third);
}

TEST(MetadataFuzzySearch, BoundsReturnedNameBytes)
{
    if (!metadata_fuzzy_search_available()) {
        GTEST_SKIP() << "RapidFuzz metadata search is not enabled";
    }

    MetaStore store;
    std::string long_path = "Creator/";
    long_path.append(kMetadataFuzzySearchMaxCandidateBytes, 'x');
    add_xmp_text(&store, "http://purl.org/dc/elements/1.1/", long_path);
    store.finalize();

    const MetadataFuzzySearchResult result = search_with_score(store, "creator",
                                                               80U);
    ASSERT_EQ(result.matches.size(), 1U);
    EXPECT_TRUE(result.matches[0].name_truncated);
    EXPECT_FALSE(result.matches[0].group_truncated);
    EXPECT_EQ(result.matches[0].name.size(),
              kMetadataFuzzySearchMaxCandidateBytes);
}

TEST(MetadataFuzzySearch, EnforcesDocumentedAsciiNormalization)
{
    if (!metadata_fuzzy_search_available()) {
        GTEST_SKIP() << "RapidFuzz metadata search is not enabled";
    }

    MetaStore store;
    const EntryId crop  = add_xmp_text(&store, "http://ns.adobe.com/dng/1.0/",
                                       "dng:DefaultCropOrigin");
    const EntryId mixed = add_xmp_text(&store, "http://example.invalid/",
                                       "aux:Cr\xC3\xA9"
                                       "atorName");
    store.finalize();

    MetadataFuzzySearchResult result
        = search_with_score(store, "DEFAULT_CROP-ORIGIN", 100U);
    ASSERT_EQ(result.matches.size(), 1U);
    EXPECT_EQ(result.matches[0].entry_id, crop);

    result = search_with_score(store, "name", 100U);
    ASSERT_EQ(result.matches.size(), 1U);
    EXPECT_EQ(result.matches[0].entry_id, mixed);

    result = search_with_score(store, "creator", 100U);
    EXPECT_TRUE(result.matches.empty());

    EXPECT_EQ(fuzzy_search_metadata(store, "cr\xC3\xA9"
                                           "ator")
                  .status,
              MetadataFuzzySearchStatus::UnsupportedQueryText);
    EXPECT_EQ(fuzzy_search_metadata(store, "\xE4\xBD\x9C\xE8\x80\x85").status,
              MetadataFuzzySearchStatus::UnsupportedQueryText);
    EXPECT_EQ(fuzzy_search_metadata(store, "creator \xF0\x9F\x93\xB7").status,
              MetadataFuzzySearchStatus::UnsupportedQueryText);
}

TEST(MetadataFuzzySearch, MeetsCuratedQualityMilestone)
{
    if (!metadata_fuzzy_search_available()) {
        GTEST_SKIP() << "RapidFuzz metadata search is not enabled";
    }

    MetaStore store;
    const EntryId creator     = add_xmp_text(&store,
                                             "http://purl.org/dc/elements/1.1/",
                                             "dc:creator");
    const EntryId description = add_xmp_text(&store,
                                             "http://purl.org/dc/elements/1.1/",
                                             "dc:description");
    const EntryId headline    = add_xmp_text(&store,
                                             "http://ns.adobe.com/photoshop/1.0/",
                                             "photoshop:Headline");
    const EntryId keywords    = add_xmp_text(&store,
                                             "http://purl.org/dc/elements/1.1/",
                                             "dc:Keywords");
    const EntryId credit      = add_xmp_text(&store,
                                             "http://ns.adobe.com/photoshop/1.0/",
                                             "photoshop:Credit");
    const EntryId usage_terms
        = add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/rights/",
                       "xmpRights:UsageTerms");
    const EntryId copyright = add_xmp_text(&store,
                                           "http://ns.adobe.com/photoshop/1.0/",
                                           "photoshop:Copyright");
    const EntryId exposure  = add_xmp_text(&store,
                                           "http://ns.adobe.com/exif/1.0/",
                                           "exif:ExposureTime");
    const EntryId aperture
        = add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/", "exif:FNumber");
    const EntryId sensitivity   = add_xmp_text(&store,
                                               "http://ns.adobe.com/exif/1.0/",
                                               "exif:PhotographicSensitivity");
    const EntryId exposure_bias = add_xmp_text(&store,
                                               "http://ns.adobe.com/exif/1.0/",
                                               "exif:ExposureBiasValue");
    const EntryId date_time_original
        = add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/",
                       "exif:DateTimeOriginal");
    const EntryId focal_length = add_xmp_text(&store,
                                              "http://ns.adobe.com/exif/1.0/",
                                              "exif:FocalLength");
    const EntryId orientation  = add_xmp_text(&store,
                                              "http://ns.adobe.com/tiff/1.0/",
                                              "tiff:Orientation");
    const EntryId white_balance
        = add_xmp_text(&store, "http://ns.adobe.com/camera-raw-settings/1.0/",
                       "crs:WhiteBalance");
    const EntryId lens_correction
        = add_xmp_text(&store, "http://ns.adobe.com/camera-raw-settings/1.0/",
                       "crs:LensCorrection");
    const EntryId lens_model = add_xmp_text(&store,
                                            "http://ns.adobe.com/exif/1.0/aux/",
                                            "aux:LensModel");
    const EntryId serial_number
        = add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/aux/",
                       "aux:SerialNumber");
    const EntryId tone_curve
        = add_xmp_text(&store, "http://ns.adobe.com/camera-raw-settings/1.0/",
                       "crs:ToneCurve");
    const EntryId crop_origin = add_xmp_text(&store,
                                             "http://ns.adobe.com/dng/1.0/",
                                             "dng:DefaultCropOrigin");
    const EntryId color_profile
        = add_xmp_text(&store, "http://ns.adobe.com/photoshop/1.0/",
                       "photoshop:ColorProfile");
    const EntryId crop_size   = add_xmp_text(&store,
                                             "http://ns.adobe.com/dng/1.0/",
                                             "dng:DefaultCropSize");
    const EntryId active_area = add_xmp_text(&store,
                                             "http://ns.adobe.com/dng/1.0/",
                                             "dng:ActiveArea");
    const EntryId black_level = add_xmp_text(&store,
                                             "http://ns.adobe.com/dng/1.0/",
                                             "dng:BlackLevel");
    const EntryId white_level = add_xmp_text(&store,
                                             "http://ns.adobe.com/dng/1.0/",
                                             "dng:WhiteLevel");
    const EntryId linearization_table
        = add_xmp_text(&store, "http://ns.adobe.com/dng/1.0/",
                       "dng:LinearizationTable");
    const EntryId color_space = add_xmp_text(&store,
                                             "http://ns.adobe.com/exif/1.0/",
                                             "exif:ColorSpace");
    const EntryId camera_make
        = add_xmp_text(&store, "http://ns.adobe.com/tiff/1.0/", "tiff:Make");
    add_xmp_text(&store, "http://ns.adobe.com/tiff/1.0/", "tiff:Model");
    const EntryId gps_latitude  = add_xmp_text(&store,
                                               "http://ns.adobe.com/exif/1.0/",
                                               "exif:GPSLatitude");
    const EntryId gps_longitude = add_xmp_text(&store,
                                               "http://ns.adobe.com/exif/1.0/",
                                               "exif:GPSLongitude");
    const EntryId history       = add_xmp_text(&store,
                                               "http://ns.adobe.com/xap/1.0/mm/",
                                               "xmpMM:History");
    store.finalize();

    struct PositiveCase final {
        const char* query;
        EntryId expected;
    };
    const PositiveCase positives[] = {
        { "cretaor", creator },
        { "author", creator },
        { "photografer", creator },
        { "descripton", description },
        { "caption", description },
        { "hedline", headline },
        { "keywrods", keywords },
        { "tag list", keywords },
        { "photo credt", credit },
        { "licence terms", usage_terms },
        { "copyrigt", copyright },
        { "expsoure time", exposure },
        { "shutter speed", exposure },
        { "apeture", aperture },
        { "iso", sensitivity },
        { "iso speeed", sensitivity },
        { "exposure compensaton", exposure_bias },
        { "date takn", date_time_original },
        { "capture time", date_time_original },
        { "focal lenght", focal_length },
        { "orentation", orientation },
        { "rotation", orientation },
        { "white balnce", white_balance },
        { "white balance setting", white_balance },
        { "lens corection", lens_correction },
        { "lens nane", lens_model },
        { "camera seral", serial_number },
        { "tone cruve", tone_curve },
        { "tone mapping curve", tone_curve },
        { "default crop orgin", crop_origin },
        { "crop offset", crop_origin },
        { "default crop sze", crop_size },
        { "crop dimensions", crop_size },
        { "active are", active_area },
        { "sensor area", active_area },
        { "raw blak", black_level },
        { "raw white", white_level },
        { "linearisation table", linearization_table },
        { "colour profile", color_profile },
        { "icc profile", color_profile },
        { "colour space", color_space },
        { "camera brand", camera_make },
        { "manufactrer", camera_make },
        { "gps lat", gps_latitude },
        { "location longitude", gps_longitude },
        { "edit histroy", history },
    };
    const char* negatives[] = {
        "network socket",      "audio compressor", "database index",
        "render thread",       "filesystem mount", "packet checksum",
        "shader compiler",     "window manager",   "memory allocator",
        "command buffer",      "focus peaking",    "sensor temperature",
        "battery level",       "thumbnail offset", "preview codec",
        "layer mask",          "alpha channel",    "tile index",
        "frame rate",          "audio duration",   "printer resolution",
        "document page count", "object detection", "face rectangle",
        "motion vector",       "noise reduction",  "film grain",
        "depth map",           "panorama stitch",  "thermal emissivity",
        "encryption key",      "hash algorithm",   "signature issuer",
        "network locator",     "storage class",
    };

    uint32_t true_positive_count  = 0U;
    uint32_t false_positive_count = 0U;
    for (size_t i = 0U; i < sizeof(positives) / sizeof(positives[0]); ++i) {
        SCOPED_TRACE(positives[i].query);
        const MetadataFuzzySearchResult result
            = search_with_score(store, positives[i].query, 80U, 4U);
        if (!result.matches.empty()
            && result.matches[0].entry_id == positives[i].expected) {
            ++true_positive_count;
        } else if (!result.matches.empty()) {
            ++false_positive_count;
        }
        ASSERT_FALSE(result.matches.empty());
        EXPECT_EQ(result.matches[0].entry_id, positives[i].expected)
            << "actual_name=" << result.matches[0].name
            << " score=" << static_cast<unsigned int>(result.matches[0].score)
            << " kind="
            << metadata_fuzzy_search_match_kind_name(
                   result.matches[0].match_kind);
    }
    for (size_t i = 0U; i < sizeof(negatives) / sizeof(negatives[0]); ++i) {
        SCOPED_TRACE(negatives[i]);
        const MetadataFuzzySearchResult result
            = search_with_score(store, negatives[i], 80U, 4U);
        if (!result.matches.empty()) {
            ++false_positive_count;
            ADD_FAILURE() << "actual_name=" << result.matches[0].name
                          << " score="
                          << static_cast<unsigned int>(result.matches[0].score)
                          << " kind="
                          << metadata_fuzzy_search_match_kind_name(
                                 result.matches[0].match_kind);
        }
    }

    const double recall = static_cast<double>(true_positive_count)
                          / static_cast<double>(sizeof(positives)
                                                / sizeof(positives[0]));
    const uint32_t positive_predictions = true_positive_count
                                          + false_positive_count;
    const double precision = positive_predictions == 0U
                                 ? 0.0
                                 : static_cast<double>(true_positive_count)
                                       / static_cast<double>(
                                           positive_predictions);
    EXPECT_GE(recall, 0.95);
    EXPECT_GE(precision, 0.95);
}

}  // namespace openmeta
