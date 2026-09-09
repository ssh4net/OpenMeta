// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_edit.h"
#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_translation.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace openmeta {
namespace {
    using Status = MetadataGpsTranslationStatus;
    using Policy = MetadataGpsTranslationConflictPolicy;
    static constexpr std::string_view kExif = "http://ns.adobe.com/exif/1.0/";

    static std::string_view view(const MetaStore& store, ByteSpan span)
    {
        const auto bytes = store.arena().span(span);
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

    static EntryId xmp(MetaStore& store, std::string_view path, MetaValue value,
                       EntryFlags flags    = EntryFlags::Dirty,
                       std::string_view ns = kExif)
    {
        Entry entry;
        entry.key   = make_xmp_property_key(store.arena(), ns, path);
        entry.value = value;
        entry.flags = flags;
        entry.origin.order_in_block = static_cast<uint32_t>(
            store.entries().size());
        entry.origin.wire_type_name = store.arena().append_string("gps-source");
        return store.add_entry(entry);
    }

    static EntryId xmp_text(MetaStore& store, std::string_view path,
                            std::string_view value,
                            EntryFlags flags = EntryFlags::Dirty)
    {
        return xmp(store, path,
                   make_text(store.arena(), value, TextEncoding::Utf8), flags);
    }

    static EntryId native(MetaStore& store, uint16_t tag, MetaValue value)
    {
        Entry entry;
        entry.key   = make_exif_tag_key(store.arena(), "gpsifd", tag);
        entry.value = value;
        return store.add_entry(entry);
    }

    static const Entry* gps(const MetaStore& store, uint16_t tag)
    {
        for (const Entry& entry : store.entries()) {
            if (!any(entry.flags, EntryFlags::Deleted)
                && entry.key.kind == MetaKeyKind::ExifTag
                && entry.key.data.exif_tag.tag == tag
                && view(store, entry.key.data.exif_tag.ifd) == "gpsifd") {
                return &entry;
            }
        }
        return nullptr;
    }

    static size_t gps_count(const MetaStore& store, uint16_t tag)
    {
        size_t count = 0U;
        for (const Entry& entry : store.entries()) {
            if (!any(entry.flags, EntryFlags::Deleted)
                && entry.key.kind == MetaKeyKind::ExifTag
                && entry.key.data.exif_tag.tag == tag
                && view(store, entry.key.data.exif_tag.ifd) == "gpsifd") {
                ++count;
            }
        }
        return count;
    }

    static void expect_coordinate(const MetaStore& store, uint16_t tag,
                                  std::array<URational, 3> expected)
    {
        const Entry* entry = gps(store, tag);
        ASSERT_NE(entry, nullptr);
        ASSERT_EQ(entry->value.kind, MetaValueKind::Array);
        ASSERT_EQ(entry->value.elem_type, MetaElementType::URational);
        ASSERT_EQ(entry->value.count, 3U);
        const auto bytes = store.arena().span(entry->value.data.span);
        ASSERT_EQ(bytes.size(), sizeof(expected));
        std::array<URational, 3> actual;
        std::memcpy(actual.data(), bytes.data(), bytes.size());
        for (size_t i = 0U; i < 3U; ++i) {
            EXPECT_EQ(actual[i].numer, expected[i].numer);
            EXPECT_EQ(actual[i].denom, expected[i].denom);
        }
    }

    static void expect_failure(MetaStore& source, Status expected,
                               MetadataGpsTranslationOptions options = {})
    {
        source.finalize();
        const size_t original_count = source.entries().size();
        MetaStore output;
        native(output, 8U,
               make_text(output.arena(), "sentinel", TextEncoding::Ascii));
        output.finalize();
        const auto result = translate_xmp_gps_metadata(source, options,
                                                       &output);
        EXPECT_EQ(result.status, expected);
        EXPECT_EQ(source.entries().size(), original_count);
        ASSERT_EQ(output.entries().size(), 1U);
        ASSERT_NE(gps(output, 8U), nullptr);
        EXPECT_EQ(view(output, gps(output, 8U)->value.data.span), "sentinel");
    }

    static MetaStore position(EntryFlags flags = EntryFlags::Dirty)
    {
        MetaStore store;
        xmp_text(store, "GPSLatitude", "35,48.125N", flags);
        xmp_text(store, "GPSLongitude", "139,34,55.25W", flags);
        xmp_text(store, "GPSAltitude", "12345/100", flags);
        xmp_text(store, "GPSAltitudeRef", "1", flags);
        return store;
    }
}  // namespace

TEST(MetadataGpsTranslation, WritesExactPrimaryPositionAndOwnsOutput)
{
    MetaStore output;
    {
        MetaStore source = position();
        source.finalize();
        const auto result = translate_xmp_gps_metadata(source, {}, &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.source_properties, 4U);
        EXPECT_EQ(result.groups_translated, 3U);
        EXPECT_EQ(result.entries_added, 7U);
        EXPECT_EQ(source.entries().size(), 4U);
        EXPECT_EQ(output.entries().size(), 11U);
    }
    expect_coordinate(output, 2U,
                      { URational { 35U, 1U }, { 48U, 1U }, { 15U, 2U } });
    expect_coordinate(output, 4U,
                      { URational { 139U, 1U }, { 34U, 1U }, { 221U, 4U } });
    ASSERT_NE(gps(output, 1U), nullptr);
    EXPECT_EQ(view(output, gps(output, 1U)->value.data.span), "N");
    EXPECT_EQ(view(output, gps(output, 3U)->value.data.span), "W");
    EXPECT_EQ(gps(output, 5U)->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(gps(output, 5U)->value.data.u64, 1U);
    EXPECT_EQ(gps(output, 6U)->value.data.ur.numer, 2469U);
    EXPECT_EQ(gps(output, 6U)->value.data.ur.denom, 20U);
    const auto version = output.arena().span(gps(output, 0U)->value.data.span);
    ASSERT_EQ(version.size(), 4U);
    EXPECT_EQ(version[0], std::byte { 2U });
    EXPECT_EQ(version[1], std::byte { 3U });
    for (uint16_t tag = 0U; tag <= 6U; ++tag) {
        ASSERT_NE(gps(output, tag), nullptr);
        EXPECT_EQ(view(output, gps(output, tag)->origin.wire_type_name),
                  "gps-source");
    }
    MetaStore again;
    const auto repeated = translate_xmp_gps_metadata(output, {}, &again);
    EXPECT_EQ(repeated.status, Status::Ok);
    EXPECT_EQ(repeated.groups_unchanged, 3U);
    EXPECT_EQ(repeated.entries_added, 0U);
}

TEST(MetadataGpsTranslation, CoordinatesCoverHemispheresPolesAndExactFractions)
{
    struct Case final {
        const char* path;
        const char* text;
        uint16_t tag;
        uint32_t degree;
        uint32_t minute;
        URational second;
    };
    const std::array cases {
        Case { "GPSLatitude", "90,0N", 2U, 90U, 0U, { 0U, 1U } },
        Case { "GPSLatitude", "90,0,0S", 2U, 90U, 0U, { 0U, 1U } },
        Case { "GPSLongitude", "180,0E", 4U, 180U, 0U, { 0U, 1U } },
        Case { "GPSLongitude", "000,00.000W", 4U, 0U, 0U, { 0U, 1U } },
        Case {
            "GPSLatitude", "1,2.50000000000000000000S", 2U, 1U, 2U, { 30U, 1U } },
        Case { "GPSLongitude",
               "179,59.99999999E",
               4U,
               179U,
               59U,
               { 299999997U, 5000000U } },
        Case {
            "GPSLatitude", "0,0,0.000000001N", 2U, 0U, 0U, { 1U, 1000000000U } },
    };
    for (const Case& item : cases) {
        SCOPED_TRACE(item.text);
        MetaStore source;
        xmp_text(source, item.path, item.text);
        source.finalize();
        MetaStore output;
        ASSERT_EQ(translate_xmp_gps_metadata(source, {}, &output).status,
                  Status::Ok);
        expect_coordinate(output, item.tag,
                          { URational { item.degree, 1U },
                            { item.minute, 1U },
                            item.second });
    }
}

TEST(MetadataGpsTranslation,
     RejectsMalformedCoordinatesAndUnrepresentablePrecision)
{
    for (const std::string_view input : {
             "",
             "35.5",
             "35,10",
             "35,10n",
             "35,10E",
             "-35,10N",
             "+35,10N",
             "35, 10N",
             "35,10 N",
             "35,10,1,2N",
             "35,10e1N",
             "35,1/2N",
             "35,10.N",
             "35,.5N",
             "35.1,0N",
             "35,1.1,0N",
             "NaN,0N",
         }) {
        SCOPED_TRACE(input);
        MetaStore source;
        xmp_text(source, "GPSLatitude", input);
        expect_failure(source, Status::InvalidSourceValue);
    }
    for (const std::string_view input :
         { "91,0N", "90,0.1N", "89,60N", "89,59,60N" }) {
        MetaStore source;
        xmp_text(source, "GPSLatitude", input);
        expect_failure(source, Status::ValueOutOfRange);
    }
    for (const std::string_view input :
         { "0,0,0.0000000001N", "0,0.00000000001N",
           "0,0,18446744073709551616N" }) {
        MetaStore source;
        xmp_text(source, "GPSLatitude", input);
        expect_failure(source, Status::UnsupportedPrecision);
    }
    MetaStore nul;
    xmp_text(nul, "GPSLatitude", std::string_view("0,0\0N", 5U));
    expect_failure(nul, Status::InvalidSourceValue);
}

TEST(MetadataGpsTranslation, AltitudeAcceptsExactNonnegativeValuesIncludingZero)
{
    const std::array values { make_u64(0U), make_i64(123U),
                              make_urational(10U, 20U) };
    for (const MetaValue& value : values) {
        MetaStore source;
        xmp(source, "GPSAltitude", value);
        xmp(source, "GPSAltitudeRef", make_u32(0U));
        source.finalize();
        MetaStore output;
        const auto result = translate_xmp_gps_metadata(source, {}, &output);
        ASSERT_EQ(result.status, Status::Ok);
        ASSERT_NE(gps(output, 6U), nullptr);
        EXPECT_EQ(gps(output, 6U)->value.elem_type, MetaElementType::URational);
        EXPECT_EQ(result.entries_added, 3U);
    }
    for (const std::string_view value :
         { "0.0000", "4294967295", "8589934590/2", "1.25000" }) {
        MetaStore source;
        xmp_text(source, "GPSAltitude", value);
        xmp_text(source, "GPSAltitudeRef", "1");
        source.finalize();
        MetaStore output;
        EXPECT_EQ(translate_xmp_gps_metadata(source, {}, &output).status,
                  Status::Ok);
    }
}

TEST(MetadataGpsTranslation, RejectsInvalidAltitudeTypesReferencesAndPrecision)
{
    for (const std::string_view value :
         { "-1", "+1", "1 m", "1e3", "1/0", "1/2/3", "NaN", "1." }) {
        MetaStore source;
        xmp_text(source, "GPSAltitude", value);
        xmp_text(source, "GPSAltitudeRef", "0");
        expect_failure(source, Status::InvalidSourceValue);
    }
    for (const std::string_view value :
         { "4294967296", "1/4294967296", "0.0000000001" }) {
        MetaStore source;
        xmp_text(source, "GPSAltitude", value);
        xmp_text(source, "GPSAltitudeRef", "0");
        expect_failure(source, Status::UnsupportedPrecision);
    }
    for (const std::string_view value :
         { "2", "3", "01", "-1", "Below Sea Level", "0.0" }) {
        MetaStore source;
        xmp_text(source, "GPSAltitude", "1");
        xmp_text(source, "GPSAltitudeRef", value);
        expect_failure(source, Status::InvalidSourceValue);
    }
    for (MetaValue value :
         { make_f64_bits(0x3ff8000000000000ULL), make_i64(-1), make_urational(1U, 0U), make_u8(1U) }) {
        if (value.elem_type == MetaElementType::U8) {
            value.count = 2U;
        }
        MetaStore source;
        xmp(source, "GPSAltitude", value);
        xmp_text(source, "GPSAltitudeRef", "0");
        expect_failure(source, Status::InvalidSourceValue);
    }
}

TEST(MetadataGpsTranslation, DirtyAltitudeMemberSelectsCompletePair)
{
    for (const bool dirty_reference : { false, true }) {
        MetaStore source;
        xmp_text(source, "GPSAltitude", "100",
                 dirty_reference ? EntryFlags {} : EntryFlags::Dirty);
        xmp_text(source, "GPSAltitudeRef", "1",
                 dirty_reference ? EntryFlags::Dirty : EntryFlags {});
        source.finalize();
        MetaStore output;
        const auto result = translate_xmp_gps_metadata(source, {}, &output);
        EXPECT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.source_properties, 2U);
        EXPECT_EQ(result.entries_added, 3U);
    }
    for (const bool reference_only : { false, true }) {
        MetaStore source;
        xmp_text(source, reference_only ? "GPSAltitudeRef" : "GPSAltitude",
                 "1");
        expect_failure(source, Status::IncompleteSource);
    }
    MetaStore mixed;
    xmp_text(mixed, "GPSAltitude", "", EntryFlags::Dirty | EntryFlags::Deleted);
    xmp_text(mixed, "GPSAltitudeRef", "1");
    expect_failure(mixed, Status::IncompleteSource);
}

TEST(MetadataGpsTranslation, SourceModesFlagsNamespacesAndAmbiguityAreExplicit)
{
    MetaStore source = position(EntryFlags {});
    xmp(source, "GPSLatitude", make_u8(99U), EntryFlags::Dirty,
        "urn:unrelated");
    xmp_text(source, "GPSDestLatitude", "bad");
    xmp_text(source, "GPSLatitudeRef", "bad");
    source.finalize();
    MetaStore output;
    EXPECT_EQ(translate_xmp_gps_metadata(source, {}, &output).entries_added,
              0U);
    MetadataGpsTranslationOptions options;
    options.source_mode = MetadataGpsTranslationSourceMode::All;
    for (size_t enabled = 0U; enabled < 3U; ++enabled) {
        options.latitude_to_exif  = enabled == 0U;
        options.longitude_to_exif = enabled == 1U;
        options.altitude_to_exif  = enabled == 2U;
        const auto result         = translate_xmp_gps_metadata(source, options,
                                                               &output);
        EXPECT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.groups_translated, 1U);
        EXPECT_EQ(result.entries_added, 3U);
    }
    MetaStore duplicate;
    xmp_text(duplicate, "GPSLatitude", "1,2N", EntryFlags {});
    xmp_text(duplicate, "GPSLatitude", "1,2N");
    expect_failure(duplicate, Status::AmbiguousSource);
}

TEST(MetadataGpsTranslation, ConflictsReconcileCompletePairsAndFailAtomically)
{
    MetaStore source = position();
    native(source, 1U, make_text(source.arena(), "S", TextEncoding::Ascii));
    native(source, 1U, make_text(source.arena(), "N", TextEncoding::Ascii));
    native(source, 6U, make_urational(9U, 1U));
    expect_failure(source, Status::NativeConflict);
    MetadataGpsTranslationOptions options;
    options.conflict_policy = Policy::PreserveExisting;
    MetaStore preserved;
    const auto keep = translate_xmp_gps_metadata(source, options, &preserved);
    EXPECT_EQ(keep.status, Status::Ok);
    EXPECT_EQ(keep.groups_preserved, 2U);
    EXPECT_EQ(gps_count(preserved, 1U), 2U);
    EXPECT_EQ(gps(preserved, 2U), nullptr);
    options.conflict_policy = Policy::ReplaceExisting;
    MetaStore replaced;
    const auto result = translate_xmp_gps_metadata(source, options, &replaced);
    ASSERT_EQ(result.status, Status::Ok);
    EXPECT_EQ(result.entries_updated, 2U);
    EXPECT_EQ(result.entries_removed, 1U);
    EXPECT_EQ(result.entries_added, 5U);
    EXPECT_EQ(gps_count(replaced, 1U), 1U);
    EXPECT_EQ(view(replaced, gps(replaced, 1U)->value.data.span), "N");
}

TEST(MetadataGpsTranslation, EquivalentTypedRationalComponentsAreIdempotent)
{
    MetaStore source;
    xmp_text(source, "GPSLatitude", "35,48.5N");
    const std::array<URational, 3> coordinate {
        { { 70U, 2U }, { 96U, 2U }, { 60U, 2U } }
    };
    native(source, 1U, make_text(source.arena(), "N", TextEncoding::Ascii));
    native(source, 2U, make_urational_array(source.arena(), coordinate));
    source.finalize();
    MetaStore output;
    const auto result = translate_xmp_gps_metadata(source, {}, &output);
    ASSERT_EQ(result.status, Status::Ok);
    EXPECT_EQ(result.groups_unchanged, 1U);
    EXPECT_EQ(result.entries_added, 1U);
    EXPECT_EQ(result.entries_updated, 0U);
    expect_coordinate(output, 2U, coordinate);
}

TEST(MetadataGpsTranslation, RemovalCleansVersionOnlyAfterLastGpsValue)
{
    for (const bool keep_unrelated : { false, true }) {
        MetaStore original = position();
        if (keep_unrelated) {
            native(original, 8U,
                   make_text(original.arena(), "5 satellites",
                             TextEncoding::Ascii));
        }
        original.finalize();
        MetaStore populated;
        ASSERT_EQ(translate_xmp_gps_metadata(original, {}, &populated).status,
                  Status::Ok);
        MetaEdit edit;
        for (EntryId id = 0U; id < 4U; ++id) {
            edit.tombstone(id);
        }
        MetaStore deleted = commit(populated,
                                   std::span<const MetaEdit>(&edit, 1U));
        MetadataGpsTranslationOptions options;
        options.conflict_policy = Policy::ReplaceExisting;
        MetaStore output;
        const auto result = translate_xmp_gps_metadata(deleted, options,
                                                       &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.entries_removed, keep_unrelated ? 6U : 7U);
        EXPECT_EQ(gps(output, 0U) != nullptr, keep_unrelated);
        for (uint16_t tag = 1U; tag <= 6U; ++tag) {
            EXPECT_EQ(gps(output, tag), nullptr);
        }
        EXPECT_EQ(translate_xmp_gps_metadata(output, options, &deleted)
                      .groups_unchanged,
                  3U);
    }
}

TEST(MetadataGpsTranslation, CleanTombstonesDoNotDeleteNativePairs)
{
    MetaStore source;
    xmp_text(source, "GPSLatitude", "", EntryFlags::Deleted);
    native(source, 1U, make_text(source.arena(), "N", TextEncoding::Ascii));
    source.finalize();
    MetadataGpsTranslationOptions options;
    options.source_mode     = MetadataGpsTranslationSourceMode::All;
    options.conflict_policy = Policy::ReplaceExisting;
    MetaStore output;
    const auto result = translate_xmp_gps_metadata(source, options, &output);
    EXPECT_EQ(result.status, Status::Ok);
    EXPECT_EQ(result.entries_removed, 0U);
    EXPECT_NE(gps(output, 1U), nullptr);
}

TEST(MetadataGpsTranslation, Gps24PreservesSeaLevelMeaningAndVersion)
{
    for (const uint8_t reference : { 0U, 1U }) {
        MetaStore source;
        xmp_text(source, "GPSAltitude", "123.5");
        xmp(source, "GPSAltitudeRef", make_u8(reference));
        const std::array<uint8_t, 4> version { 2U, 4U, 0U, 0U };
        native(source, 0U, make_u8_array(source.arena(), version));
        source.finalize();
        MetaStore output;
        const auto result = translate_xmp_gps_metadata(source, {}, &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.entries_added, 2U);
        EXPECT_EQ(gps(output, 5U)->value.data.u64, reference + 2U);
        EXPECT_EQ(output.arena().span(gps(output, 0U)->value.data.span)[1],
                  std::byte { 4U });
        MetaStore again;
        EXPECT_EQ(
            translate_xmp_gps_metadata(output, {}, &again).groups_unchanged,
            1U);
    }
    MetaStore future;
    xmp_text(future, "GPSAltitude", "1");
    xmp_text(future, "GPSAltitudeRef", "0");
    const std::array<uint8_t, 4> version { 3U, 0U, 0U, 0U };
    native(future, 0U, make_u8_array(future.arena(), version));
    expect_failure(future, Status::UnsupportedGpsVersion);
}

TEST(MetadataGpsTranslation, MalformedVersionNeverReadsAnInactiveUnionMember)
{
    for (const bool scalar : { false, true }) {
        MetaStore source;
        xmp_text(source, "GPSLatitude", "1,2N");
        native(source, 0U,
               scalar
                   ? make_u64(UINT64_MAX)
                   : make_text(source.arena(), "2.3.0.0", TextEncoding::Ascii));
        expect_failure(source, Status::NativeConflict);
    }
    MetaStore duplicate;
    xmp_text(duplicate, "GPSLatitude", "1,2N");
    const std::array<uint8_t, 4> version { 2U, 3U, 0U, 0U };
    native(duplicate, 0U, make_u8_array(duplicate.arena(), version));
    native(duplicate, 0U, make_u8_array(duplicate.arena(), version));
    expect_failure(duplicate, Status::NativeConflict);
}

TEST(MetadataGpsTranslation, ResourceLimitsIncludeVersionAndDuplicateCleanup)
{
    MetaStore source = position();
    MetadataGpsTranslationOptions options;
    options.max_added_entries = 6U;
    expect_failure(source, Status::EntryLimitExceeded, options);
    options                = {};
    options.max_operations = 6U;
    expect_failure(source, Status::OperationLimitExceeded, options);
    options                             = {};
    options.max_text_bytes_per_property = 3U;
    expect_failure(source, Status::ValueTooLong, options);
    options                      = {};
    options.max_total_text_bytes = 20U;
    expect_failure(source, Status::SourceLimitExceeded, options);
    MetaStore duplicates;
    xmp_text(duplicates, "GPSLatitude", "1,2N");
    for (size_t i = 0U; i < 4U; ++i) {
        native(duplicates, 1U,
               make_text(duplicates.arena(), "S", TextEncoding::Ascii));
    }
    options                 = {};
    options.conflict_policy = Policy::ReplaceExisting;
    options.max_operations
        = 5U;  // Four ref operations, coordinate, and version need six.
    expect_failure(duplicates, Status::OperationLimitExceeded, options);
}

TEST(MetadataGpsTranslation,
     InvalidOptionsAndInPlaceTranslationAreDeterministic)
{
    MetaStore source = position();
    MetaStore output;
    EXPECT_EQ(translate_xmp_gps_metadata(source, {}, nullptr).status,
              Status::NullOutput);
    EXPECT_EQ(translate_xmp_gps_metadata(source, {}, &output).status,
              Status::SourceNotFinalized);
    source.finalize();
    std::array<MetadataGpsTranslationOptions, 7> invalid {};
    invalid[0].max_added_entries = 0U;
    invalid[1].max_operations    = kMetadataGpsTranslationMaxOperations + 1U;
    invalid[2].max_text_bytes_per_property
        = kMetadataGpsTranslationMaxTextBytesPerProperty + 1U;
    invalid[3].max_total_text_bytes = 0U;
    invalid[4].source_mode = static_cast<MetadataGpsTranslationSourceMode>(
        255U);
    invalid[5].conflict_policy        = static_cast<Policy>(255U);
    invalid[6].latitude_to_exif       = invalid[6].longitude_to_exif
        = invalid[6].altitude_to_exif = false;
    for (const auto& options : invalid) {
        expect_failure(source, Status::InvalidOptions, options);
    }
    EXPECT_EQ(translate_xmp_gps_metadata(source, {}, &source).status,
              Status::Ok);
    EXPECT_EQ(translate_xmp_gps_metadata(source, {}, &source).groups_unchanged,
              3U);
}

namespace {
    using NavigationOptions = MetadataGpsNavigationTranslationOptions;
    constexpr std::array<std::string_view, 7> kNavigationPaths {
        "GPSTimeStamp", "GPSSpeedRef",        "GPSSpeed",       "GPSTrackRef",
        "GPSTrack",     "GPSImgDirectionRef", "GPSImgDirection"
    };
    constexpr std::array<std::string_view, 7> kNavigationValues {
        "2024-03-01T00:30:12.125+01:00",
        "K",
        "12345/100",
        "T",
        "359.99",
        "M",
        "45.5"
    };
    constexpr std::array<uint16_t, 8> kNavigationTags { 7U,  29U, 12U, 13U,
                                                        14U, 15U, 16U, 17U };

    static MetaStore navigation(EntryFlags flags = EntryFlags::Dirty)
    {
        MetaStore store;
        for (size_t i = 0U; i < kNavigationPaths.size(); ++i) {
            xmp_text(store, kNavigationPaths[i], kNavigationValues[i], flags);
        }
        return store;
    }

    static void expect_navigation_failure(MetaStore& source, Status expected,
                                          NavigationOptions options = {})
    {
        source.finalize();
        MetaStore output;
        native(output, 8U,
               make_text(output.arena(), "sentinel", TextEncoding::Ascii));
        output.finalize();
        const size_t count = source.entries().size();
        const auto result
            = translate_xmp_gps_navigation_metadata(source, options, &output);
        EXPECT_EQ(result.status, expected);
        EXPECT_EQ(source.entries().size(), count);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(view(output, gps(output, 8U)->value.data.span), "sentinel");
        const auto alias
            = translate_xmp_gps_navigation_metadata(source, options, &source);
        EXPECT_EQ(alias.status, expected);
        EXPECT_EQ(source.entries().size(), count);
    }
}  // namespace

TEST(MetadataGpsNavigation, WritesFourGroupsWithOwnedProvenanceAndIsIdempotent)
{
    MetaStore output;
    {
        MetaStore source = navigation();
        source.finalize();
        const auto result = translate_xmp_gps_navigation_metadata(source, {},
                                                                  &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.source_properties, 7U);
        EXPECT_EQ(result.groups_translated, 4U);
        EXPECT_EQ(result.entries_added, 9U);
        EXPECT_EQ(source.entries().size(), 7U);
    }
    expect_coordinate(output, 7U,
                      { { { 23U, 1U }, { 30U, 1U }, { 97U, 8U } } });
    ASSERT_NE(gps(output, 29U), nullptr);
    EXPECT_EQ(view(output, gps(output, 29U)->value.data.span), "2024:02:29");
    for (const uint16_t tag : kNavigationTags) {
        ASSERT_NE(gps(output, tag), nullptr);
        EXPECT_EQ(view(output, gps(output, tag)->origin.wire_type_name),
                  "gps-source");
    }
    EXPECT_EQ(gps(output, 13U)->value.data.ur.numer, 2469U);
    EXPECT_EQ(gps(output, 13U)->value.data.ur.denom, 20U);
    EXPECT_EQ(gps(output, 17U)->value.data.ur.numer, 91U);
    const auto again = translate_xmp_gps_navigation_metadata(output, {},
                                                             &output);
    EXPECT_EQ(again.status, Status::Ok);
    EXPECT_EQ(again.groups_unchanged, 4U);
    EXPECT_EQ(again.entries_added, 0U);
}

TEST(MetadataGpsNavigation, NormalizesOffsetsAcrossCalendarBoundaries)
{
    struct Case {
        std::string_view input;
        std::string_view date;
        uint32_t hour;
        uint32_t minute;
    };
    const std::array cases {
        Case { "2024-01-01T00:30:00+01:00", "2023:12:31", 23U, 30U },
        Case { "2023-12-31T23:30:00-02:00", "2024:01:01", 1U, 30U },
        Case { "2000-03-01T00:00:00+00:30", "2000:02:29", 23U, 30U },
        Case { "1900-03-01T00:00:00+00:30", "1900:02:28", 23U, 30U },
        Case { "0001-01-01T00:00:00Z", "0001:01:01", 0U, 0U },
        Case { "9999-12-31T23:59:00-00:00", "9999:12:31", 23U, 59U },
        Case { "2024-01-01T00:00:00-23:59", "2024:01:01", 23U, 59U }
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.input);
        MetaStore source;
        xmp_text(source, "GPSTimeStamp", c.input);
        source.finalize();
        MetaStore output;
        ASSERT_EQ(
            translate_xmp_gps_navigation_metadata(source, {}, &output).status,
            Status::Ok);
        ASSERT_NE(gps(output, 29U), nullptr);
        EXPECT_EQ(view(output, gps(output, 29U)->value.data.span), c.date);
        expect_coordinate(output, 7U,
                          { { { c.hour, 1U }, { c.minute, 1U }, { 0U, 1U } } });
    }
}

TEST(MetadataGpsNavigation, RejectsIncompleteOrNoncanonicalTimestampSyntax)
{
    for (const std::string_view input :
         { "2024-02-29", "12:34:56Z", "2024-02-29T12:34Z",
           "2024-02-29T12:34:56", "2024-02-29 12:34:56Z",
           "2024-02-29T12:34:56z", "2024-02-29T12:34:5Z",
           "2024-02-29T12:34:56.Z", "2024-02-29T12:34:56Zjunk",
           "2024-02-29T12:34:56+0900", "2024-02-29T12:34:056Z",
           "2024-02-29T12:34:56.1e2Z" }) {
        SCOPED_TRACE(input);
        MetaStore source;
        xmp_text(source, "GPSTimeStamp", input);
        expect_navigation_failure(source, Status::InvalidSourceValue);
    }
}

TEST(MetadataGpsNavigation, RejectsInvalidDatesLeapSecondsAndUtcYearOverflow)
{
    for (const std::string_view input :
         { "2023-02-29T12:34:56Z", "0000-01-01T00:00:00Z",
           "2024-13-01T00:00:00Z", "2024-01-00T00:00:00Z",
           "2024-04-31T00:00:00Z", "2024-01-01T24:00:00Z",
           "2024-01-01T23:60:00Z", "2024-01-01T23:59:60Z",
           "2024-01-01T00:00:00+24:00", "2024-01-01T00:00:00-01:60",
           "0001-01-01T00:00:00+00:01", "9999-12-31T23:59:00-00:01" }) {
        SCOPED_TRACE(input);
        MetaStore source;
        xmp_text(source, "GPSTimeStamp", input);
        expect_navigation_failure(source, Status::ValueOutOfRange);
    }
}

TEST(MetadataGpsNavigation, TimestampFractionsAreExactAndNeverRounded)
{
    MetaStore source;
    xmp_text(source, "GPSTimeStamp", "2024-01-01T00:00:00.0000000005Z");
    source.finalize();
    MetaStore output;
    ASSERT_EQ(translate_xmp_gps_navigation_metadata(source, {}, &output).status,
              Status::Ok);
    expect_coordinate(output, 7U,
                      { { { 0U, 1U }, { 0U, 1U }, { 1U, 2000000000U } } });
    for (const std::string_view input : { "2024-01-01T00:00:00.0000000001Z",
                                          "2024-01-01T00:00:59.123456789Z" }) {
        source = MetaStore {};
        xmp_text(source, "GPSTimeStamp", input);
        expect_navigation_failure(source, Status::UnsupportedPrecision);
    }
    source = MetaStore {};
    xmp_text(source, "GPSTimeStamp",
             "2024-01-01T00:00:59.500000000000000000000Z");
    source.finalize();
    ASSERT_EQ(translate_xmp_gps_navigation_metadata(source, {}, &output).status,
              Status::Ok);
    expect_coordinate(output, 7U, { { { 0U, 1U }, { 0U, 1U }, { 119U, 2U } } });
}

TEST(MetadataGpsNavigation, AcceptsExactReferenceCodesAndPortableAliases)
{
    const std::array<std::string_view, 6> speeds { "K",    "M",   "N",
                                                   "km/h", "mph", "knots" };
    for (size_t i = 0U; i < speeds.size(); ++i) {
        MetaStore source;
        xmp_text(source, "GPSSpeedRef", speeds[i]);
        xmp(source, "GPSSpeed", make_urational(3U, 2U));
        source.finalize();
        MetaStore output;
        ASSERT_EQ(
            translate_xmp_gps_navigation_metadata(source, {}, &output).status,
            Status::Ok);
        EXPECT_EQ(view(output, gps(output, 12U)->value.data.span),
                  speeds[i % 3U]);
    }
    for (const bool image : { false, true }) {
        const std::array<std::string_view, 4> refs { "T", "M", "True North",
                                                     "Magnetic North" };
        for (size_t i = 0U; i < refs.size(); ++i) {
            MetaStore source;
            xmp_text(source, image ? "GPSImgDirectionRef" : "GPSTrackRef",
                     refs[i]);
            xmp(source, image ? "GPSImgDirection" : "GPSTrack", make_u32(123U));
            source.finalize();
            MetaStore output;
            ASSERT_EQ(translate_xmp_gps_navigation_metadata(source, {}, &output)
                          .status,
                      Status::Ok);
            EXPECT_EQ(view(output,
                           gps(output, image ? 16U : 14U)->value.data.span),
                      refs[i % 2U]);
        }
    }
}

TEST(MetadataGpsNavigation,
     ValidatesAnglesAndUnsignedRationalsWithoutConversion)
{
    for (const std::string_view input :
         { "-1", "+1", "1e2", "1/0", "1.0/2", "1 km/h", " 1" }) {
        MetaStore source;
        xmp_text(source, "GPSSpeedRef", "K");
        xmp_text(source, "GPSSpeed", input);
        expect_navigation_failure(source, Status::InvalidSourceValue);
    }
    for (const std::string_view input : { "359.991", "360", "720/2" }) {
        MetaStore source;
        xmp_text(source, "GPSTrackRef", "T");
        xmp_text(source, "GPSTrack", input);
        expect_navigation_failure(source, Status::ValueOutOfRange);
    }
    for (const std::string_view ref : { "t", "True", " T", "T ", "North" }) {
        MetaStore source;
        xmp_text(source, "GPSTrackRef", ref);
        xmp_text(source, "GPSTrack", "1/3");
        expect_navigation_failure(source, Status::InvalidSourceValue);
    }
    MetaStore source;
    xmp_text(source, "GPSSpeedRef", "K");
    xmp(source, "GPSSpeed", make_f64_bits(0x3ff0000000000000ULL));
    expect_navigation_failure(source, Status::InvalidSourceValue);
    source = MetaStore {};
    xmp_text(source, "GPSSpeedRef", "K");
    xmp_text(source, "GPSSpeed", "4294967296");
    expect_navigation_failure(source, Status::UnsupportedPrecision);
}

TEST(MetadataGpsNavigation,
     DirtySelectionAndIndependentFlagsLeavePrimaryApiUnchanged)
{
    MetaStore source = navigation(EntryFlags::None);
    source.finalize();
    MetaStore output;
    EXPECT_EQ(translate_xmp_gps_navigation_metadata(source, {}, &output)
                  .entries_added,
              0U);
    MetadataGpsTranslationOptions primary;
    primary.source_mode = MetadataGpsTranslationSourceMode::All;
    EXPECT_EQ(translate_xmp_gps_metadata(source, primary, &output).entries_added,
              0U);
    for (size_t i = 0U; i < 4U; ++i) {
        NavigationOptions options;
        options.source_mode             = MetadataGpsTranslationSourceMode::All;
        options.timestamp_to_exif       = i == 0U;
        options.speed_to_exif           = i == 1U;
        options.track_to_exif           = i == 2U;
        options.image_direction_to_exif = i == 3U;
        const auto result
            = translate_xmp_gps_navigation_metadata(source, options, &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.groups_translated, 1U);
        EXPECT_EQ(result.entries_added, 3U);
        EXPECT_NE(gps(output, kNavigationTags[i * 2U]), nullptr);
    }
    source = MetaStore {};
    xmp_text(source, "GPSSpeedRef", "M", EntryFlags::None);
    xmp_text(source, "GPSSpeed", "25");
    source.finalize();
    EXPECT_EQ(translate_xmp_gps_navigation_metadata(source, {}, &output)
                  .entries_added,
              3U);
}

TEST(MetadataGpsNavigation, RequiresCompletePairsAndRejectsDuplicateSources)
{
    for (size_t i = 1U; i < kNavigationPaths.size(); ++i) {
        MetaStore source;
        xmp_text(source, kNavigationPaths[i], kNavigationValues[i]);
        expect_navigation_failure(source, Status::IncompleteSource);
    }
    MetaStore source = navigation();
    xmp_text(source, "GPSTrack", "359.99");
    expect_navigation_failure(source, Status::AmbiguousSource);
    source = MetaStore {};
    xmp_text(source, "GPSSpeedRef", "K");
    xmp_text(source, "GPSSpeed", "25", EntryFlags::Dirty | EntryFlags::Deleted);
    expect_navigation_failure(source, Status::IncompleteSource);
}

TEST(MetadataGpsNavigation, ConflictsPreserveOrRepairTheWholeNativePair)
{
    for (const uint16_t tag : { 7U, 29U, 12U, 13U }) {
        MetaStore source = navigation();
        native(source, tag,
               make_text(source.arena(), "old", TextEncoding::Ascii));
        expect_navigation_failure(source, Status::NativeConflict);
        NavigationOptions options;
        options.conflict_policy = Policy::PreserveExisting;
        MetaStore output;
        const auto preserved
            = translate_xmp_gps_navigation_metadata(source, options, &output);
        ASSERT_EQ(preserved.status, Status::Ok);
        EXPECT_EQ(preserved.groups_preserved, 1U);
        const uint16_t other = tag == 7U    ? 29U
                               : tag == 29U ? 7U
                               : tag == 12U ? 13U
                                            : 12U;
        EXPECT_EQ(gps(output, other), nullptr);
        options.conflict_policy = Policy::ReplaceExisting;
        ASSERT_EQ(translate_xmp_gps_navigation_metadata(source, options, &output)
                      .status,
                  Status::Ok);
        EXPECT_NE(gps(output, other), nullptr);
    }
}

TEST(MetadataGpsNavigation,
     ComparesEquivalentNativeRationalsAndRepairsDuplicates)
{
    MetaStore source;
    xmp_text(source, "GPSSpeedRef", "K");
    xmp_text(source, "GPSSpeed", "1.5");
    native(source, 12U,
           make_text(source.arena(), std::string_view("K\0", 2U),
                     TextEncoding::Ascii));
    native(source, 13U, make_urational(6U, 4U));
    source.finalize();
    MetaStore output;
    const auto same = translate_xmp_gps_navigation_metadata(source, {},
                                                            &output);
    ASSERT_EQ(same.status, Status::Ok);
    EXPECT_EQ(same.groups_unchanged, 1U);
    EXPECT_EQ(same.entries_added, 1U);
    source = MetaStore {};
    xmp_text(source, "GPSSpeedRef", "K");
    xmp_text(source, "GPSSpeed", "1.5");
    native(source, 12U, make_text(source.arena(), "M", TextEncoding::Ascii));
    native(source, 12U, make_text(source.arena(), "N", TextEncoding::Ascii));
    native(source, 13U, make_urational(9U, 1U));
    native(source, 13U, make_urational(10U, 1U));
    source.finalize();
    NavigationOptions options;
    options.conflict_policy = Policy::ReplaceExisting;
    const auto fixed = translate_xmp_gps_navigation_metadata(source, options,
                                                             &output);
    ASSERT_EQ(fixed.status, Status::Ok);
    EXPECT_EQ(fixed.entries_removed, 2U);
    EXPECT_EQ(gps_count(output, 12U), 1U);
    EXPECT_EQ(gps_count(output, 13U), 1U);
}

TEST(MetadataGpsNavigation,
     CompleteDirtyRemovalCleansVersionUnlessOtherGpsRemains)
{
    for (const bool unrelated : { false, true }) {
        MetaStore source = navigation(EntryFlags::Dirty | EntryFlags::Deleted);
        native(source, 0U,
               make_u8_array(source.arena(),
                             std::array<uint8_t, 4> { 2U, 3U, 0U, 0U }));
        for (const uint16_t tag : kNavigationTags) {
            native(source, tag, make_u32(99U));
        }
        if (unrelated) {
            native(source, 8U,
                   make_text(source.arena(), "retained", TextEncoding::Ascii));
        }
        source.finalize();
        NavigationOptions options;
        options.conflict_policy = Policy::ReplaceExisting;
        MetaStore output;
        const auto result
            = translate_xmp_gps_navigation_metadata(source, options, &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.entries_removed, unrelated ? 8U : 9U);
        EXPECT_EQ(gps(output, 0U) != nullptr, unrelated);
        for (const uint16_t tag : kNavigationTags) {
            EXPECT_EQ(gps(output, tag), nullptr);
        }
    }
    MetaStore source = navigation(EntryFlags::Deleted);
    native(source, 7U, make_u32(99U));
    source.finalize();
    MetaStore output;
    EXPECT_EQ(translate_xmp_gps_navigation_metadata(source, {}, &output)
                  .entries_removed,
              0U);
    EXPECT_NE(gps(output, 7U), nullptr);
}

TEST(MetadataGpsNavigation, TimeVersionPolicyDoesNotUpgradeExistingGps)
{
    for (const uint8_t minor : { 0U, 1U, 2U, 3U, 4U, 5U }) {
        MetaStore source = navigation();
        native(source, 0U,
               make_u8_array(source.arena(),
                             std::array<uint8_t, 4> { 2U, minor, 0U, 0U }));
        source.finalize();
        MetaStore output;
        if (minor < 2U || minor > 4U) {
            expect_navigation_failure(source, Status::UnsupportedGpsVersion);
        } else {
            ASSERT_EQ(translate_xmp_gps_navigation_metadata(source, {}, &output)
                          .status,
                      Status::Ok);
            EXPECT_EQ(output.arena().span(gps(output, 0U)->value.data.span)[1],
                      static_cast<std::byte>(minor));
        }
        NavigationOptions options;
        options.timestamp_to_exif = false;
        EXPECT_EQ(translate_xmp_gps_navigation_metadata(source, options, &output)
                      .status,
                  Status::Ok);
    }
    MetaStore source = navigation();
    native(source, 0U, make_u32(2300U));
    expect_navigation_failure(source, Status::NativeConflict);
}

TEST(MetadataGpsNavigation, ExactPathsIgnoreSplitDateAndUnrelatedNamespaces)
{
    MetaStore source;
    xmp_text(source, "GPSDateStamp", "2024-02-29");
    xmp_text(source, "GPSDateTime", "2024-02-29T01:02:03Z");
    xmp_text(source, "GPSSpeed[1]", "100");
    xmp_text(source, "GPSTimeStamp[@xml:lang=x-default]",
             "2024-02-29T01:02:03Z");
    xmp(source, "GPSSpeed", make_u32(3U), EntryFlags::Dirty, "foreign");
    source.finalize();
    MetaStore output;
    EXPECT_EQ(translate_xmp_gps_navigation_metadata(source, {}, &output)
                  .entries_added,
              0U);
}

TEST(MetadataGpsNavigation, ResourceAndOptionLimitsLeaveOutputUnchanged)
{
    MetaStore source = navigation();
    MetaStore output;
    EXPECT_EQ(translate_xmp_gps_navigation_metadata(source, {}, nullptr).status,
              Status::NullOutput);
    EXPECT_EQ(translate_xmp_gps_navigation_metadata(source, {}, &output).status,
              Status::SourceNotFinalized);
    NavigationOptions options;
    options.max_added_entries = 8U;
    expect_navigation_failure(source, Status::EntryLimitExceeded, options);
    options                = {};
    options.max_operations = 8U;
    expect_navigation_failure(source, Status::OperationLimitExceeded, options);
    options                             = {};
    options.max_text_bytes_per_property = 3U;
    expect_navigation_failure(source, Status::ValueTooLong, options);
    options                      = {};
    options.max_total_text_bytes = 32U;
    expect_navigation_failure(source, Status::SourceLimitExceeded, options);
    std::array<NavigationOptions, 7> invalid {};
    invalid[0].max_added_entries           = 10U;
    invalid[1].max_operations              = 0U;
    invalid[2].max_text_bytes_per_property = 129U;
    invalid[3].max_total_text_bytes        = 897U;
    invalid[4].source_mode = static_cast<MetadataGpsTranslationSourceMode>(99U);
    invalid[5].conflict_policy     = static_cast<Policy>(99U);
    invalid[6].timestamp_to_exif   = invalid[6].speed_to_exif
        = invalid[6].track_to_exif = invalid[6].image_direction_to_exif = false;
    for (const NavigationOptions& option : invalid) {
        expect_navigation_failure(source, Status::InvalidOptions, option);
    }
    EXPECT_STREQ(metadata_gps_translation_mapping_name(
                     MetadataGpsTranslationMapping::ExifGpsTimeStamp),
                 "exif_gps_timestamp");
}

namespace {
    using DestinationOptions = MetadataGpsDestinationTranslationOptions;
    static constexpr std::array<uint16_t, 8> kDestinationTags { 19U, 20U, 21U,
                                                                22U, 23U, 24U,
                                                                25U, 26U };
    static MetaStore destination(EntryFlags flags = EntryFlags::Dirty)
    {
        MetaStore store;
        xmp_text(store, "GPSDestLatitude", "35,48.125S", flags);
        xmp_text(store, "GPSDestLongitude", "139,34,55.25E", flags);
        xmp_text(store, "GPSDestBearingRef", "True North", flags);
        xmp_text(store, "GPSDestBearing", "359.99", flags);
        xmp_text(store, "GPSDestDistanceRef", "Nautical miles", flags);
        xmp_text(store, "GPSDestDistance", "12345/100", flags);
        return store;
    }
    static void expect_destination_failure(MetaStore& source, Status expected,
                                           DestinationOptions options = {})
    {
        source.finalize();
        const size_t count = source.entries().size();
        MetaStore output;
        native(output, 8U,
               make_text(output.arena(), "sentinel", TextEncoding::Ascii));
        output.finalize();
        EXPECT_EQ(translate_xmp_gps_destination_metadata(source, options,
                                                         &output)
                      .status,
                  expected);
        EXPECT_EQ(source.entries().size(), count);
        ASSERT_EQ(output.entries().size(), 1U);
        ASSERT_NE(gps(output, 8U), nullptr);
        EXPECT_EQ(view(output, gps(output, 8U)->value.data.span), "sentinel");
        EXPECT_EQ(translate_xmp_gps_destination_metadata(source, options,
                                                         &source)
                      .status,
                  expected);
        EXPECT_EQ(source.entries().size(), count);
    }
}  // namespace

TEST(MetadataGpsDestination, WritesFourGroupsWithOwnedProvenanceAndIsIdempotent)
{
    MetaStore output;
    {
        MetaStore source = destination();
        source.finalize();
        const auto result = translate_xmp_gps_destination_metadata(source, {},
                                                                   &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.source_properties, 6U);
        EXPECT_EQ(result.groups_translated, 4U);
        EXPECT_EQ(result.entries_added, 9U);
        EXPECT_EQ(source.entries().size(), 6U);
    }
    expect_coordinate(output, 20U,
                      { { { 35U, 1U }, { 48U, 1U }, { 15U, 2U } } });
    expect_coordinate(output, 22U,
                      { { { 139U, 1U }, { 34U, 1U }, { 221U, 4U } } });
    for (const uint16_t tag : kDestinationTags) {
        ASSERT_NE(gps(output, tag), nullptr);
        EXPECT_EQ(view(output, gps(output, tag)->origin.wire_type_name),
                  "gps-source");
    }
    EXPECT_EQ(view(output, gps(output, 19U)->value.data.span), "S");
    EXPECT_EQ(view(output, gps(output, 21U)->value.data.span), "E");
    EXPECT_EQ(view(output, gps(output, 23U)->value.data.span), "T");
    EXPECT_EQ(view(output, gps(output, 25U)->value.data.span), "N");
    EXPECT_EQ(gps(output, 24U)->value.data.ur.numer, 35999U);
    EXPECT_EQ(gps(output, 24U)->value.data.ur.denom, 100U);
    EXPECT_EQ(gps(output, 26U)->value.data.ur.numer, 2469U);
    EXPECT_EQ(gps(output, 26U)->value.data.ur.denom, 20U);
    const auto again = translate_xmp_gps_destination_metadata(output, {},
                                                              &output);
    EXPECT_EQ(again.status, Status::Ok);
    EXPECT_EQ(again.groups_unchanged, 4U);
    EXPECT_EQ(again.entries_added, 0U);
}

TEST(MetadataGpsDestination, CoordinateBoundsPrecisionAndZeroHemisphereAreExact)
{
    for (const auto path : { "GPSDestLatitude", "GPSDestLongitude" }) {
        const bool latitude = std::string_view(path) == "GPSDestLatitude";
        for (const auto input : latitude ? std::array { "90,0S", "0,0S" }
                                         : std::array { "180,0W", "0,0W" }) {
            MetaStore source;
            xmp_text(source, path, input);
            source.finalize();
            MetaStore output;
            ASSERT_EQ(translate_xmp_gps_destination_metadata(source, {}, &output)
                          .status,
                      Status::Ok);
            EXPECT_EQ(view(output,
                           gps(output, latitude ? 19U : 21U)->value.data.span),
                      latitude ? "S" : "W");
        }
        MetaStore source;
        xmp_text(source, path, latitude ? "90,0.0001N" : "180,0,0.1E");
        expect_destination_failure(source, Status::ValueOutOfRange);
        source = MetaStore {};
        xmp_text(source, path, latitude ? "1,60N" : "1,0,60E");
        expect_destination_failure(source, Status::ValueOutOfRange);
        source = MetaStore {};
        xmp_text(source, path, latitude ? "1,2E" : "1,2N");
        expect_destination_failure(source, Status::InvalidSourceValue);
        source = MetaStore {};
        xmp_text(source, path,
                 latitude ? "1,0,0.0000000001N" : "1,0,0.0000000001E");
        expect_destination_failure(source, Status::UnsupportedPrecision);
    }
}

TEST(MetadataGpsDestination, DistanceUnitsAndBearingAliasesNeverConvertValues)
{
    for (const auto ref :
         { "K", "M", "N", "Kilometers", "Miles", "Nautical miles", "Knots" }) {
        MetaStore source;
        xmp_text(source, "GPSDestDistanceRef", ref);
        xmp_text(source, "GPSDestDistance", "7/3");
        source.finalize();
        MetaStore output;
        ASSERT_EQ(
            translate_xmp_gps_destination_metadata(source, {}, &output).status,
            Status::Ok);
        const std::string_view unit(ref);
        const char expected = unit == "K" || unit == "Kilometers" ? 'K'
                              : unit == "M" || unit == "Miles"    ? 'M'
                                                                  : 'N';
        EXPECT_EQ(view(output, gps(output, 25U)->value.data.span),
                  std::string_view(&expected, 1U));
        EXPECT_EQ(gps(output, 26U)->value.data.ur.numer, 7U);
        EXPECT_EQ(gps(output, 26U)->value.data.ur.denom, 3U);
    }
    for (const auto ref : { "T", "M", "True North", "Magnetic North" }) {
        MetaStore source;
        xmp_text(source, "GPSDestBearingRef", ref);
        xmp_text(source, "GPSDestBearing", "1/3");
        source.finalize();
        MetaStore output;
        ASSERT_EQ(
            translate_xmp_gps_destination_metadata(source, {}, &output).status,
            Status::Ok);
        EXPECT_EQ(view(output, gps(output, 23U)->value.data.span).front(),
                  ref[0]);
        EXPECT_EQ(gps(output, 24U)->value.data.ur.denom, 3U);
    }
    for (const auto ref :
         { "knots", "km/h", "mph", "n", " N", "N ", "Nautical Miles" }) {
        MetaStore source;
        xmp_text(source, "GPSDestDistanceRef", ref);
        xmp_text(source, "GPSDestDistance", "1");
        expect_destination_failure(source, Status::InvalidSourceValue);
    }
}

TEST(MetadataGpsDestination, RejectsInvalidRationalsAnglesAndFloatingPoint)
{
    for (const auto value :
         { "-1", "+1", "1e2", "1/0", "1.0/2", "1 miles", " 1" }) {
        MetaStore source;
        xmp_text(source, "GPSDestDistanceRef", "M");
        xmp_text(source, "GPSDestDistance", value);
        expect_destination_failure(source, Status::InvalidSourceValue);
    }
    for (const auto value : { "359.991", "360", "720/2" }) {
        MetaStore source;
        xmp_text(source, "GPSDestBearingRef", "T");
        xmp_text(source, "GPSDestBearing", value);
        expect_destination_failure(source, Status::ValueOutOfRange);
    }
    MetaStore source;
    xmp_text(source, "GPSDestDistanceRef", "N");
    xmp(source, "GPSDestDistance", make_f64_bits(0x3ff0000000000000ULL));
    expect_destination_failure(source, Status::InvalidSourceValue);
    source = MetaStore {};
    xmp_text(source, "GPSDestDistanceRef", "N");
    xmp_text(source, "GPSDestDistance", "4294967296");
    expect_destination_failure(source, Status::UnsupportedPrecision);
}

TEST(MetadataGpsDestination, AcceptsExactTypedZeroIntegerAndRationalDistance)
{
    for (const auto value :
         { make_u32(0U), make_i32(7), make_urational(14U, 6U) }) {
        MetaStore source;
        xmp_text(source, "GPSDestDistanceRef", "K");
        xmp(source, "GPSDestDistance", value);
        source.finalize();
        MetaStore output;
        ASSERT_EQ(
            translate_xmp_gps_destination_metadata(source, {}, &output).status,
            Status::Ok);
        EXPECT_EQ(gps(output, 26U)->value.elem_type,
                  MetaElementType::URational);
    }
}

TEST(MetadataGpsDestination,
     CompletePairsSelectDirtyCompanionAndRejectAmbiguity)
{
    for (const auto prefix : { "GPSDestBearing", "GPSDestDistance" }) {
        const std::string ref = std::string(prefix) + "Ref";
        for (const bool dirty_ref : { false, true }) {
            MetaStore source;
            xmp_text(source, ref,
                     std::string_view(prefix) == "GPSDestBearing" ? "T" : "K",
                     dirty_ref ? EntryFlags::Dirty : EntryFlags::None);
            xmp_text(source, prefix, "1.5",
                     dirty_ref ? EntryFlags::None : EntryFlags::Dirty);
            source.finalize();
            MetaStore output;
            EXPECT_EQ(translate_xmp_gps_destination_metadata(source, {}, &output)
                          .entries_added,
                      3U);
        }
        MetaStore source;
        xmp_text(source, prefix, "1");
        expect_destination_failure(source, Status::IncompleteSource);
        source = MetaStore {};
        xmp_text(source, ref, "K", EntryFlags::Dirty | EntryFlags::Deleted);
        xmp_text(source, prefix, "1");
        expect_destination_failure(source, Status::IncompleteSource);
        source = MetaStore {};
        xmp_text(source, prefix, "1");
        xmp_text(source, prefix, "1");
        xmp_text(source, ref, "T");
        expect_destination_failure(source, Status::AmbiguousSource);
    }
    MetaStore source = destination();
    xmp_text(source, "GPSDestLatitude", "1,2N");
    expect_destination_failure(source, Status::AmbiguousSource);
}

TEST(MetadataGpsDestination, PartialOrMalformedNativeGroupsShareConflictPolicy)
{
    for (const uint16_t tag : kDestinationTags) {
        MetaStore source = destination();
        native(source, tag, make_u32(99U));
        expect_destination_failure(source, Status::NativeConflict);
        DestinationOptions options;
        options.conflict_policy = Policy::PreserveExisting;
        MetaStore output;
        const auto preserved
            = translate_xmp_gps_destination_metadata(source, options, &output);
        ASSERT_EQ(preserved.status, Status::Ok);
        EXPECT_EQ(preserved.groups_preserved, 1U);
        const uint16_t other = tag % 2U != 0U ? tag + 1U : tag - 1U;
        EXPECT_EQ(gps(output, other), nullptr);
        options.conflict_policy = Policy::ReplaceExisting;
        ASSERT_EQ(translate_xmp_gps_destination_metadata(source, options,
                                                         &output)
                      .status,
                  Status::Ok);
        EXPECT_NE(gps(output, other), nullptr);
    }
}

TEST(MetadataGpsDestination,
     DirtySelectionAndIndependentFlagsLeavePrimaryApiUnchanged)
{
    MetaStore source = destination(EntryFlags::None);
    source.finalize();
    MetaStore output;
    EXPECT_EQ(translate_xmp_gps_destination_metadata(source, {}, &output)
                  .entries_added,
              0U);
    MetadataGpsTranslationOptions primary;
    primary.source_mode = MetadataGpsTranslationSourceMode::All;
    EXPECT_EQ(translate_xmp_gps_metadata(source, primary, &output).entries_added,
              0U);
    for (size_t i = 0U; i < 4U; ++i) {
        DestinationOptions options;
        options.source_mode       = MetadataGpsTranslationSourceMode::All;
        options.latitude_to_exif  = i == 0U;
        options.longitude_to_exif = i == 1U;
        options.bearing_to_exif   = i == 2U;
        options.distance_to_exif  = i == 3U;
        const auto result
            = translate_xmp_gps_destination_metadata(source, options, &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.groups_translated, 1U);
        EXPECT_EQ(result.entries_added, 3U);
        EXPECT_NE(gps(output, kDestinationTags[i * 2U]), nullptr);
    }
    source = MetaStore {};
    xmp_text(source, "GPSDestDistanceRef", "M", EntryFlags::None);
    xmp_text(source, "GPSDestDistance", "25");
    source.finalize();
    EXPECT_EQ(translate_xmp_gps_destination_metadata(source, {}, &output)
                  .entries_added,
              3U);
}

TEST(MetadataGpsDestination,
     ComparesEquivalentNativeRationalsAndRepairsDuplicates)
{
    MetaStore source;
    xmp_text(source, "GPSDestDistanceRef", "K");
    xmp_text(source, "GPSDestDistance", "1.5");
    native(source, 25U,
           make_text(source.arena(), std::string_view("K\0", 2U),
                     TextEncoding::Ascii));
    native(source, 26U, make_urational(6U, 4U));
    source.finalize();
    MetaStore output;
    const auto same = translate_xmp_gps_destination_metadata(source, {},
                                                             &output);
    ASSERT_EQ(same.status, Status::Ok);
    EXPECT_EQ(same.groups_unchanged, 1U);
    EXPECT_EQ(same.entries_added, 1U);
    source = MetaStore {};
    xmp_text(source, "GPSDestDistanceRef", "K");
    xmp_text(source, "GPSDestDistance", "1.5");
    native(source, 25U, make_text(source.arena(), "M", TextEncoding::Ascii));
    native(source, 25U, make_text(source.arena(), "N", TextEncoding::Ascii));
    native(source, 26U, make_urational(9U, 1U));
    native(source, 26U, make_urational(10U, 1U));
    source.finalize();
    DestinationOptions options;
    options.conflict_policy = Policy::ReplaceExisting;
    const auto fixed = translate_xmp_gps_destination_metadata(source, options,
                                                              &output);
    ASSERT_EQ(fixed.status, Status::Ok);
    EXPECT_EQ(fixed.entries_removed, 2U);
    EXPECT_EQ(gps_count(output, 25U), 1U);
    EXPECT_EQ(gps_count(output, 26U), 1U);
}

TEST(MetadataGpsDestination,
     CompleteDirtyRemovalCleansVersionUnlessOtherGpsRemains)
{
    for (const bool unrelated : { false, true }) {
        MetaStore source = destination(EntryFlags::Dirty | EntryFlags::Deleted);
        native(source, 0U,
               make_u8_array(source.arena(),
                             std::array<uint8_t, 4> { 2U, 3U, 0U, 0U }));
        for (const uint16_t tag : kDestinationTags) {
            native(source, tag, make_u32(99U));
        }
        if (unrelated) {
            native(source, 8U,
                   make_text(source.arena(), "retained", TextEncoding::Ascii));
        }
        source.finalize();
        DestinationOptions options;
        options.conflict_policy = Policy::ReplaceExisting;
        MetaStore output;
        const auto result
            = translate_xmp_gps_destination_metadata(source, options, &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.entries_removed, unrelated ? 8U : 9U);
        EXPECT_EQ(gps(output, 0U) != nullptr, unrelated);
        for (const uint16_t tag : kDestinationTags) {
            EXPECT_EQ(gps(output, tag), nullptr);
        }
    }
    MetaStore source = destination(EntryFlags::Deleted);
    native(source, 20U, make_u32(99U));
    source.finalize();
    MetaStore output;
    EXPECT_EQ(translate_xmp_gps_destination_metadata(source, {}, &output)
                  .entries_removed,
              0U);
    EXPECT_NE(gps(output, 20U), nullptr);
}

TEST(MetadataGpsDestination, ResourceAndOptionLimitsLeaveOutputUnchanged)
{
    MetaStore source = destination();
    MetaStore output;
    EXPECT_EQ(translate_xmp_gps_destination_metadata(source, {}, nullptr).status,
              Status::NullOutput);
    EXPECT_EQ(translate_xmp_gps_destination_metadata(source, {}, &output).status,
              Status::SourceNotFinalized);
    DestinationOptions options;
    options.max_added_entries = 8U;
    expect_destination_failure(source, Status::EntryLimitExceeded, options);
    options                = {};
    options.max_operations = 8U;
    expect_destination_failure(source, Status::OperationLimitExceeded, options);
    options                             = {};
    options.max_text_bytes_per_property = 3U;
    expect_destination_failure(source, Status::ValueTooLong, options);
    options                      = {};
    options.max_total_text_bytes = 32U;
    expect_destination_failure(source, Status::SourceLimitExceeded, options);
    std::array<DestinationOptions, 7> invalid {};
    invalid[0].max_added_entries           = 10U;
    invalid[1].max_operations              = 0U;
    invalid[2].max_text_bytes_per_property = 129U;
    invalid[3].max_total_text_bytes        = 769U;
    invalid[4].source_mode = static_cast<MetadataGpsTranslationSourceMode>(99U);
    invalid[5].conflict_policy       = static_cast<Policy>(99U);
    invalid[6].latitude_to_exif      = invalid[6].longitude_to_exif
        = invalid[6].bearing_to_exif = invalid[6].distance_to_exif = false;
    for (const DestinationOptions& option : invalid) {
        expect_destination_failure(source, Status::InvalidOptions, option);
    }
    EXPECT_STREQ(metadata_gps_translation_mapping_name(
                     MetadataGpsTranslationMapping::ExifGpsDestLatitude),
                 "exif_gps_dest_latitude");
}

TEST(MetadataGpsDestination,
     RetainsVersionAndUnrelatedGpsWithoutVersionInference)
{
    for (const uint8_t minor : { 0U, 2U, 3U, 4U, 9U }) {
        MetaStore source = destination();
        native(source, 0U,
               make_u8_array(source.arena(),
                             std::array<uint8_t, 4> { 2U, minor, 0U, 0U }));
        native(source, 8U,
               make_text(source.arena(), "retained", TextEncoding::Ascii));
        source.finalize();
        MetaStore output;
        ASSERT_EQ(
            translate_xmp_gps_destination_metadata(source, {}, &output).status,
            Status::Ok);
        EXPECT_EQ(output.arena().span(gps(output, 0U)->value.data.span)[1],
                  static_cast<std::byte>(minor));
        EXPECT_EQ(view(output, gps(output, 8U)->value.data.span), "retained");
    }
    MetaStore source = destination();
    native(source, 0U, make_u32(2300U));
    expect_destination_failure(source, Status::NativeConflict);
}

TEST(MetadataGpsDestination,
     ExactPathsIgnorePrimaryNavigationAndForeignProperties)
{
    MetaStore source = position();
    xmp_text(source, "GPSTimeStamp", "2024-01-01T00:00:00Z");
    xmp_text(source, "GPSDestLatitudeRef", "N");
    xmp_text(source, "GPSDestDistance[1]", "10");
    xmp(source, "GPSDestLatitude", make_u32(3U), EntryFlags::Dirty, "foreign");
    source.finalize();
    MetaStore output;
    EXPECT_EQ(translate_xmp_gps_destination_metadata(source, {}, &output)
                  .entries_added,
              0U);
}

}  // namespace openmeta
