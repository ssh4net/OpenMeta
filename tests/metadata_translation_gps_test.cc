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

}  // namespace openmeta
