// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_edit.h"
#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_translation.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

namespace openmeta {
namespace {
    using Status  = MetadataDescriptiveTranslationStatus;
    using Policy  = MetadataDescriptiveTranslationConflictPolicy;
    using Options = MetadataStructuredLocationTranslationOptions;
    constexpr std::string_view kExt
        = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
    constexpr std::string_view kPs = "http://ns.adobe.com/photoshop/1.0/";
    constexpr std::string_view kCore
        = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
    constexpr std::array<std::string_view, 5> kChildren {
        "City", "Sublocation", "ProvinceState", "CountryName", "CountryCode"
    };
    constexpr std::array<std::string_view, 5> kFlat { "City", "Location",
                                                      "State", "Country",
                                                      "CountryCode" };
    constexpr std::array<uint16_t, 5> kDatasets { 90U, 92U, 95U, 101U, 100U };
    constexpr std::array<std::string_view, 5> kValues { "Kyoto", "Garden",
                                                        "Kyoto", "Japan",
                                                        "JPN" };

    static std::string_view view(const MetaStore& store, ByteSpan span)
    {
        const auto bytes = store.arena().span(span);
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

    static EntryId xmp(MetaStore& store, std::string_view ns,
                       std::string_view path, std::string_view value,
                       EntryFlags flags = EntryFlags::Dirty)
    {
        Entry entry;
        entry.key   = make_xmp_property_key(store.arena(), ns, path);
        entry.value = make_text(store.arena(), value, TextEncoding::Utf8);
        entry.flags = flags;
        entry.origin.wire_type_name = store.arena().append_string(
            "structured-location-source");
        return store.add_entry(entry);
    }

    static void iptc(MetaStore& store, uint16_t dataset, std::string_view value)
    {
        Entry entry;
        entry.key   = make_iptc_dataset_key(2U, dataset);
        entry.value = make_text(store.arena(), value, TextEncoding::Utf8);
        store.add_entry(entry);
    }

    static MetaStore location(std::string_view root = "LocationShown[1]",
                              EntryFlags flags      = EntryFlags::Dirty)
    {
        MetaStore store;
        for (size_t i = 0U; i < kChildren.size(); ++i) {
            xmp(store, kExt,
                std::string(root) + "/" + std::string(kChildren[i]), kValues[i],
                flags);
        }
        return store;
    }

    static std::string_view flat_ns(size_t field)
    {
        return field == 1U || field == 4U ? kCore : kPs;
    }

    static const Entry* flat(const MetaStore& store, size_t field)
    {
        for (const Entry& entry : store.entries()) {
            if (!any(entry.flags, EntryFlags::Deleted)
                && entry.key.kind == MetaKeyKind::XmpProperty
                && view(store, entry.key.data.xmp_property.schema_ns)
                       == flat_ns(field)
                && view(store, entry.key.data.xmp_property.property_path)
                       == kFlat[field]) {
                return &entry;
            }
        }
        return nullptr;
    }

    static size_t native_count(const MetaStore& store, uint16_t dataset)
    {
        size_t count = 0U;
        for (const Entry& entry : store.entries()) {
            if (!any(entry.flags, EntryFlags::Deleted)
                && entry.key.kind == MetaKeyKind::IptcDataset
                && entry.key.data.iptc_dataset.record == 2U
                && entry.key.data.iptc_dataset.dataset == dataset) {
                ++count;
            }
        }
        return count;
    }

    static void expect_pair(const MetaStore& store, size_t field,
                            std::string_view expected)
    {
        const Entry* entry = flat(store, field);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->value.kind, MetaValueKind::Text);
        EXPECT_EQ(view(store, entry->value.data.span), expected);
        EXPECT_EQ(native_count(store, kDatasets[field]), 1U);
        for (const Entry& native : store.entries()) {
            if (!any(native.flags, EntryFlags::Deleted)
                && native.key.kind == MetaKeyKind::IptcDataset
                && native.key.data.iptc_dataset.record == 2U
                && native.key.data.iptc_dataset.dataset == kDatasets[field]) {
                EXPECT_EQ(view(store, native.value.data.span), expected);
            }
        }
    }

    static void expect_failure(MetaStore& source, Status status,
                               Options options = {})
    {
        source.finalize();
        const size_t count = source.entries().size();
        MetaStore output;
        xmp(output, kPs, "City", "sentinel");
        output.finalize();
        EXPECT_EQ(translate_xmp_structured_location_metadata(source, options,
                                                             &output)
                      .status,
                  status);
        EXPECT_EQ(source.entries().size(), count);
        ASSERT_EQ(output.entries().size(), 1U);
        ASSERT_NE(flat(output, 0U), nullptr);
        EXPECT_EQ(view(output, flat(output, 0U)->value.data.span), "sentinel");
    }
}  // namespace

TEST(MetadataStructuredLocation, WritesBothDestinationsAndOwnsProvenance)
{
    MetaStore output;
    {
        MetaStore source = location();
        source.finalize();
        const auto result
            = translate_xmp_structured_location_metadata(source, {}, &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.source_properties, 5U);
        EXPECT_EQ(result.groups_translated, 5U);
        EXPECT_EQ(result.entries_added, 10U);
        EXPECT_EQ(source.entries().size(), 5U);
    }
    for (size_t i = 0U; i < kChildren.size(); ++i) {
        expect_pair(output, i, kValues[i]);
        EXPECT_EQ(view(output, flat(output, i)->origin.wire_type_name),
                  "structured-location-source");
    }
    const auto again = translate_xmp_structured_location_metadata(output, {},
                                                                  &output);
    EXPECT_EQ(again.status, Status::Ok);
    EXPECT_EQ(again.groups_unchanged, 5U);
    EXPECT_EQ(again.entries_added, 0U);
}

TEST(MetadataStructuredLocation, RequiresExplicitIndexForMultipleRecords)
{
    MetaStore source = location("LocationShown[7]");
    xmp(source, kExt, "LocationShown[2]/City", "Osaka");
    xmp(source, kExt, "LocationShown[20]/LocationName[@xml:lang=x-default]",
        "Other");
    expect_failure(source, Status::AmbiguousLocation);
    Options options;
    options.location_index = 7U;
    MetaStore output;
    ASSERT_EQ(translate_xmp_structured_location_metadata(source, options,
                                                         &output)
                  .status,
              Status::Ok);
    expect_pair(output, 0U, "Kyoto");
    options.location_index = 2U;
    ASSERT_EQ(translate_xmp_structured_location_metadata(source, options,
                                                         &output)
                  .status,
              Status::Ok);
    expect_pair(output, 0U, "Osaka");
    EXPECT_EQ(flat(output, 3U), nullptr);
    options.location_index = 1U;
    expect_failure(source, Status::LocationNotFound, options);
}

TEST(MetadataStructuredLocation, CreatedIsExplicitAndAcceptsLegacyResourceOrBag)
{
    for (const std::string_view root :
         { "LocationCreated", "LocationCreated[1]" }) {
        MetaStore source = location(root);
        source.finalize();
        MetaStore output;
        EXPECT_EQ(translate_xmp_structured_location_metadata(source, {}, &output)
                      .entries_added,
                  0U);
        Options options;
        options.location_kind = MetadataStructuredLocationKind::Created;
        ASSERT_EQ(translate_xmp_structured_location_metadata(source, options,
                                                             &output)
                      .status,
                  Status::Ok);
        expect_pair(output, 0U, "Kyoto");
    }
    MetaStore mixed = location("LocationCreated");
    xmp(mixed, kExt, "LocationCreated[1]/City", "Kyoto");
    Options options;
    options.location_kind = MetadataStructuredLocationKind::Created;
    expect_failure(mixed, Status::UnsupportedSourceShape, options);
}

TEST(MetadataStructuredLocation,
     SourceModeAndIndependentFlagsSelectStructuredFieldsOnly)
{
    for (size_t selected = 0U; selected < 5U; ++selected) {
        MetaStore source = location("LocationShown[1]", EntryFlags::None);
        xmp(source, kPs, "City", "Kyoto", EntryFlags::Dirty);
        source.finalize();
        Options options;
        MetaStore output;
        EXPECT_EQ(translate_xmp_structured_location_metadata(source, options,
                                                             &output)
                      .entries_added,
                  0U);
        options.source_mode  = MetadataDescriptiveTranslationSourceMode::All;
        options.city         = selected == 0U;
        options.sublocation  = selected == 1U;
        options.state        = selected == 2U;
        options.country      = selected == 3U;
        options.country_code = selected == 4U;
        const auto result = translate_xmp_structured_location_metadata(source,
                                                                       options,
                                                                       &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.groups_translated, 1U);
        expect_pair(output, selected, kValues[selected]);
    }
}

TEST(MetadataStructuredLocation,
     BothDestinationsShareConflictAndPreservationPolicy)
{
    for (const bool existing_flat : { false, true }) {
        MetaStore source = location();
        if (existing_flat) {
            xmp(source, kPs, "City", "Old");
        } else {
            iptc(source, 90U, "Old");
        }
        expect_failure(source, Status::NativeConflict);
        Options options;
        options.conflict_policy = Policy::PreserveExisting;
        MetaStore output;
        const auto preserved
            = translate_xmp_structured_location_metadata(source, options,
                                                         &output);
        ASSERT_EQ(preserved.status, Status::Ok);
        EXPECT_EQ(preserved.groups_preserved, 1U);
        EXPECT_EQ(native_count(output, 90U), existing_flat ? 0U : 1U);
        EXPECT_EQ(flat(output, 0U) != nullptr, existing_flat);
        options.conflict_policy = Policy::ReplaceExisting;
        ASSERT_EQ(translate_xmp_structured_location_metadata(source, options,
                                                             &output)
                      .status,
                  Status::Ok);
        expect_pair(output, 0U, "Kyoto");
    }
}

TEST(MetadataStructuredLocation,
     MatchingPartialDestinationsAreCompletedAndDuplicatesRepaired)
{
    MetaStore source = location();
    xmp(source, kPs, "City", "Kyoto");
    iptc(source, 101U, "Japan");
    source.finalize();
    MetaStore output;
    ASSERT_EQ(
        translate_xmp_structured_location_metadata(source, {}, &output).status,
        Status::Ok);
    expect_pair(output, 0U, "Kyoto");
    expect_pair(output, 3U, "Japan");
    source = location();
    xmp(source, kPs, "City", "Kyoto");
    xmp(source, kPs, "City", "Other");
    iptc(source, 90U, "Old");
    iptc(source, 90U, "Older");
    expect_failure(source, Status::NativeConflict);
    Options options;
    options.conflict_policy = Policy::ReplaceExisting;
    const auto result
        = translate_xmp_structured_location_metadata(source, options, &output);
    ASSERT_EQ(result.status, Status::Ok);
    EXPECT_EQ(result.entries_removed, 2U);
    expect_pair(output, 0U, "Kyoto");
}

TEST(MetadataStructuredLocation, MissingFieldsAndContainerTombstonesDoNotDelete)
{
    MetaStore source;
    xmp(source, kExt, "LocationShown[1]/City", "Kyoto");
    xmp(source, kPs, "Country", "Retained");
    iptc(source, 101U, "Retained");
    xmp(source, kExt, "LocationShown", "",
        EntryFlags::Dirty | EntryFlags::Deleted);
    xmp(source, kExt, "LocationShown[2]", "",
        EntryFlags::Dirty | EntryFlags::Deleted);
    source.finalize();
    MetaStore output;
    ASSERT_EQ(
        translate_xmp_structured_location_metadata(source, {}, &output).status,
        Status::Ok);
    expect_pair(output, 3U, "Retained");
}

TEST(MetadataStructuredLocation, DirtyFieldDeletionRemovesBothDestinationsOnly)
{
    for (const bool dirty : { false, true }) {
        MetaStore source;
        xmp(source, kExt, "LocationShown[1]/City", "Old",
            dirty ? EntryFlags::Dirty | EntryFlags::Deleted
                  : EntryFlags::Deleted);
        xmp(source, kPs, "City", "Old");
        iptc(source, 90U, "Old");
        iptc(source, 105U, "Headline retained");
        source.finalize();
        Options options;
        options.conflict_policy = Policy::ReplaceExisting;
        MetaStore output;
        const auto result = translate_xmp_structured_location_metadata(source,
                                                                       options,
                                                                       &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.entries_removed, dirty ? 2U : 0U);
        EXPECT_EQ(flat(output, 0U) == nullptr, dirty);
        EXPECT_EQ(native_count(output, 90U), dirty ? 0U : 1U);
        EXPECT_EQ(native_count(output, 105U), 1U);
    }
}

TEST(MetadataStructuredLocation, ExactPathsAndDuplicateSourcesAreExplicit)
{
    MetaStore source;
    xmp(source, kExt, "LocationShown[1]/Address/City", "Nested");
    xmp(source, kExt, "LocationShown[1]/exif:GPSLatitude", "35,0N");
    xmp(source, kExt, "LocationShown[1]/photoshop:City", "Wrong namespace");
    xmp(source, kCore, "LocationShown[1]/City", "Wrong root namespace");
    xmp(source, kExt, "LocationShown[1]/City[@xml:lang=x-default]",
        "Qualified");
    source.finalize();
    MetaStore output;
    const auto ignored = translate_xmp_structured_location_metadata(source, {},
                                                                    &output);
    EXPECT_EQ(ignored.status, Status::Ok);
    EXPECT_EQ(ignored.entries_added, 0U);
    source = location();
    xmp(source, kExt, "LocationShown[1]/Iptc4xmpExt:City", "Kyoto");
    expect_failure(source, Status::AmbiguousSource);
    source = location();
    xmp(source, kExt, "LocationShown[1]/City", "",
        EntryFlags::Dirty | EntryFlags::Deleted);
    expect_failure(source, Status::AmbiguousSource);
    for (const std::string_view path :
         { "LocationShown/City", "LocationShown[0]/City",
           "LocationShown[01]/City", "LocationShown[-1]/City",
           "LocationShown[4294967296]/City", "LocationShown[1]City",
           "LocationShown[1]/" }) {
        source = MetaStore {};
        xmp(source, kExt, path, "Kyoto");
        expect_failure(source, Status::UnsupportedSourceShape);
    }
}

TEST(MetadataStructuredLocation, LimitsAndValueValidationApplyBeforeAnyChanges)
{
    for (const std::string_view bad : { "", "jp", "J1", "J", "JAPN" }) {
        MetaStore source;
        xmp(source, kExt, "LocationShown[1]/City", "Kyoto");
        xmp(source, kExt, "LocationShown[1]/CountryCode", bad);
        expect_failure(source, bad.size() > 3U ? Status::ValueTooLong
                                               : Status::InvalidSourceValue);
    }
    for (size_t field = 0U; field < 4U; ++field) {
        MetaStore source;
        xmp(source, kExt,
            std::string("LocationShown[1]/") + std::string(kChildren[field]),
            std::string(field == 3U ? 65U : 33U, 'x'));
        expect_failure(source, Status::ValueTooLong);
    }
    MetaStore source = location();
    Options options;
    options.max_added_entries = 9U;
    expect_failure(source, Status::EntryLimitExceeded, options);
    options                = Options {};
    options.max_operations = 9U;
    expect_failure(source, Status::OperationLimitExceeded, options);
    options                       = Options {};
    options.max_source_properties = 4U;
    expect_failure(source, Status::SourceLimitExceeded, options);
    options                      = Options {};
    options.max_total_text_bytes = 2U;
    expect_failure(source, Status::SourceLimitExceeded, options);
}

TEST(MetadataStructuredLocation, Utf8PromotionProtectsUnrelatedNativeData)
{
    MetaStore source;
    xmp(source, kExt, "LocationShown[1]/City", "Ky\xc5\x8dto");
    source.finalize();
    MetaStore output;
    const auto result = translate_xmp_structured_location_metadata(source, {},
                                                                   &output);
    ASSERT_EQ(result.status, Status::Ok);
    EXPECT_TRUE(result.utf8_charset_added);
    EXPECT_EQ(result.entries_added, 3U);
    expect_pair(output, 0U, "Ky\xc5\x8dto");
    source = MetaStore {};
    xmp(source, kExt, "LocationShown[1]/City", "Ky\xc5\x8dto");
    iptc(source, 105U, "legacy\xe9");
    expect_failure(source, Status::NativeEncodingConflict);
}

TEST(MetadataStructuredLocation, ValidatesOptionsAndSourceBeforeOutputMutation)
{
    MetaStore source = location();
    MetaStore output;
    EXPECT_EQ(
        translate_xmp_structured_location_metadata(source, {}, nullptr).status,
        Status::NullOutput);
    EXPECT_EQ(
        translate_xmp_structured_location_metadata(source, {}, &output).status,
        Status::SourceNotFinalized);
    Options options;
    options.city = options.sublocation = options.state = options.country
        = options.country_code                         = false;
    expect_failure(source, Status::InvalidOptions, options);
    options               = Options {};
    options.location_kind = static_cast<MetadataStructuredLocationKind>(99U);
    expect_failure(source, Status::InvalidOptions, options);
    EXPECT_STREQ(metadata_descriptive_translation_status_name(
                     Status::AmbiguousLocation),
                 "ambiguous_location");
    EXPECT_STREQ(metadata_descriptive_translation_status_name(
                     Status::LocationNotFound),
                 "location_not_found");
    EXPECT_STREQ(metadata_descriptive_translation_status_name(
                     Status::UnsupportedSourceShape),
                 "unsupported_source_shape");
}

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT
TEST(MetadataStructuredLocation,
     DecodesRealRdfBagsAndPreservesForeignChildNamespaces)
{
    const std::string_view xml
        = R"(<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
<rdf:Description xmlns:l="http://iptc.org/std/Iptc4xmpExt/2008-02-29/"
xmlns:p="http://ns.adobe.com/photoshop/1.0/" xmlns:g="http://ns.adobe.com/exif/1.0/">
<l:LocationShown><rdf:Bag><rdf:li rdf:parseType="Resource">
<l:City>Kyoto</l:City><l:CountryCode>JPN</l:CountryCode><p:City>Foreign city</p:City>
<g:GPSLatitude>35,0N</g:GPSLatitude></rdf:li></rdf:Bag></l:LocationShown>
<l:LocationCreated><rdf:Bag><rdf:li rdf:parseType="Resource"><l:City>Osaka</l:City>
</rdf:li></rdf:Bag></l:LocationCreated></rdf:Description></rdf:RDF>)";
    MetaStore source;
    ASSERT_EQ(decode_xmp_packet(std::as_bytes(std::span(xml.data(), xml.size())),
                                source)
                  .status,
              XmpDecodeStatus::Ok);
    source.finalize();
    bool foreign_city = false;
    bool gps          = false;
    for (const Entry& entry : source.entries()) {
        ASSERT_EQ(view(source, entry.key.data.xmp_property.schema_ns), kExt);
        const auto path = view(source,
                               entry.key.data.xmp_property.property_path);
        foreign_city    = foreign_city
                       || path == "LocationShown[1]/photoshop:City";
        gps = gps || path == "LocationShown[1]/exif:GPSLatitude";
    }
    EXPECT_TRUE(foreign_city);
    EXPECT_TRUE(gps);
    Options options;
    options.source_mode = MetadataDescriptiveTranslationSourceMode::All;
    MetaStore output;
    ASSERT_EQ(translate_xmp_structured_location_metadata(source, options,
                                                         &output)
                  .status,
              Status::Ok);
    expect_pair(output, 0U, "Kyoto");
    options.location_kind = MetadataStructuredLocationKind::Created;
    ASSERT_EQ(translate_xmp_structured_location_metadata(source, options,
                                                         &output)
                  .status,
              Status::Ok);
    expect_pair(output, 0U, "Osaka");
}
#endif
}  // namespace openmeta

namespace openmeta {
namespace {
    using CreationOptions = MetadataLocationCreationTranslationOptions;
    static CreationOptions
    creation_options(MetadataStructuredLocationKind kind
                     = MetadataStructuredLocationKind::Shown,
                     uint32_t index = 1U)
    {
        CreationOptions options;
        options.location_kind  = kind;
        options.location_index = index;
        return options;
    }
    static MetaStore creation_source(EntryFlags flags = EntryFlags::Dirty)
    {
        MetaStore source;
        for (size_t i = 0U; i < kFlat.size(); ++i)
            xmp(source, flat_ns(i), kFlat[i], kValues[i], flags);
        return source;
    }
    static const Entry* creation_leaf(const MetaStore& store,
                                      std::string_view path)
    {
        for (const auto id :
             store.find_all(make_xmp_property_key_view(kExt, path)))
            if (!any(store.entry(id).flags, EntryFlags::Deleted))
                return &store.entry(id);
        return nullptr;
    }
    static void creation_failure(MetaStore& source, Status status,
                                 const CreationOptions& options)
    {
        source.finalize();
        const auto count = source.entries().size();
        MetaStore output;
        xmp(output, kPs, "City", "Retained");
        output.finalize();
        EXPECT_EQ(translate_xmp_location_to_structured_metadata(source, options,
                                                                &output)
                      .status,
                  status);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(view(output, output.entry(0U).value.data.span), "Retained");
        EXPECT_EQ(translate_xmp_location_to_structured_metadata(source, options,
                                                                &source)
                      .status,
                  status);
        EXPECT_EQ(source.entries().size(), count);
    }
    TEST(MetadataLocationCreation,
         RequiresExplicitDestinationAndCreatesOwnedFiveFieldRecord)
    {
        for (const auto kind : { MetadataStructuredLocationKind::Shown,
                                 MetadataStructuredLocationKind::Created }) {
            MetaStore output;
            const auto options = creation_options(kind);
            const std::string root
                = kind == MetadataStructuredLocationKind::Shown
                      ? "LocationShown[1]"
                      : "LocationCreated[1]";
            {
                MetaStore source = creation_source();
                creation_failure(source, Status::InvalidOptions, {});
                const auto result
                    = translate_xmp_location_to_structured_metadata(source,
                                                                    options,
                                                                    &output);
                ASSERT_EQ(result.status, Status::Ok);
                EXPECT_EQ(result.source_properties, 5U);
                EXPECT_EQ(result.entries_added, 5U);
                EXPECT_EQ(result.groups_translated, 5U);
                EXPECT_FALSE(result.utf8_charset_added);
            }
            for (size_t i = 0U; i < kChildren.size(); ++i) {
                const Entry* entry
                    = creation_leaf(output, root + "/Iptc4xmpExt:"
                                                + std::string(kChildren[i]));
                ASSERT_NE(entry, nullptr);
                EXPECT_EQ(view(output, entry->value.data.span), kValues[i]);
                EXPECT_EQ(view(output, entry->origin.wire_type_name),
                          "structured-location-source");
                EXPECT_EQ(native_count(output, kDatasets[i]), 0U);
            }
            EXPECT_EQ(translate_xmp_location_to_structured_metadata(output,
                                                                    options,
                                                                    &output)
                          .groups_unchanged,
                      5U);
        }
    }
    TEST(MetadataLocationCreation,
         DirtyModeFlagsAndExactSourcesExcludeNativeInference)
    {
        MetaStore source = creation_source(EntryFlags::None);
        source.finalize();
        MetaStore output;
        auto options = creation_options();
        EXPECT_EQ(translate_xmp_location_to_structured_metadata(source, options,
                                                                &output)
                      .entries_added,
                  0U);
        options.source_mode = MetadataDescriptiveTranslationSourceMode::All;
        options.city        = false;
        EXPECT_EQ(translate_xmp_location_to_structured_metadata(source, options,
                                                                &output)
                      .entries_added,
                  4U);
        source = MetaStore {};
        iptc(source, 90U, "Native only");
        xmp(source, "foreign", "City", "Foreign");
        xmp(source, kPs, "City[1]", "Indexed");
        source.finalize();
        EXPECT_EQ(translate_xmp_location_to_structured_metadata(source, options,
                                                                &output)
                      .entries_added,
                  0U);
        source = creation_source();
        xmp(source, kPs, "City", "Duplicate");
        creation_failure(source, Status::AmbiguousSource, creation_options());
    }
    TEST(MetadataLocationCreation,
         ReconcilesSelectedRecordAndPreservesOtherPlacesAndFields)
    {
        MetaStore source = creation_source();
        xmp(source, kExt, "LocationShown[1]/Iptc4xmpExt:City", "Other place",
            EntryFlags::None);
        xmp(source, kExt, "LocationShown[2]/Iptc4xmpExt:City", "Old",
            EntryFlags::None);
        xmp(source, kExt, "LocationShown[2]/City", "Duplicate",
            EntryFlags::None);
        xmp(source, kExt, "LocationShown[2]/Iptc4xmpExt:WorldRegion", "Asia",
            EntryFlags::None);
        xmp(source, kExt, "LocationCreated[1]/Iptc4xmpExt:City",
            "Capture place", EntryFlags::None);
        auto options = creation_options(MetadataStructuredLocationKind::Shown,
                                        2U);
        creation_failure(source, Status::NativeConflict, options);
        MetaStore output;
        options.conflict_policy = Policy::PreserveExisting;
        EXPECT_EQ(translate_xmp_location_to_structured_metadata(source, options,
                                                                &output)
                      .groups_preserved,
                  1U);
        options.conflict_policy = Policy::ReplaceExisting;
        const auto result
            = translate_xmp_location_to_structured_metadata(source, options,
                                                            &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_EQ(result.entries_updated, 1U);
        EXPECT_EQ(view(output,
                       creation_leaf(output, "LocationShown[1]/Iptc4xmpExt:City")
                           ->value.data.span),
                  "Other place");
        EXPECT_EQ(view(output,
                       creation_leaf(output, "LocationShown[2]/Iptc4xmpExt:City")
                           ->value.data.span),
                  "Kyoto");
        EXPECT_EQ(view(output,
                       creation_leaf(output,
                                     "LocationShown[2]/Iptc4xmpExt:WorldRegion")
                           ->value.data.span),
                  "Asia");
        EXPECT_EQ(view(output,
                       creation_leaf(output,
                                     "LocationCreated[1]/Iptc4xmpExt:City")
                           ->value.data.span),
                  "Capture place");
    }
    TEST(MetadataLocationCreation, DenseAppendAndScalarCreatedShapeAreExplicit)
    {
        MetaStore source = creation_source();
        xmp(source, kExt, "LocationShown[1]/City", "First", EntryFlags::None);
        source.finalize();
        MetaStore output;
        auto options = creation_options(MetadataStructuredLocationKind::Shown,
                                        2U);
        ASSERT_EQ(translate_xmp_location_to_structured_metadata(source, options,
                                                                &output)
                      .status,
                  Status::Ok);
        EXPECT_NE(creation_leaf(output, "LocationShown[2]/Iptc4xmpExt:City"),
                  nullptr);
        options.location_index = 4U;
        creation_failure(source, Status::LocationNotFound, options);
        source = creation_source();
        xmp(source, kExt, "LocationShown[2]/City", "Sparse", EntryFlags::None);
        creation_failure(source, Status::UnsupportedSourceShape,
                         creation_options());
        source = creation_source();
        xmp(source, kExt, "LocationCreated/WorldRegion", "Asia",
            EntryFlags::None);
        source.finalize();
        options = creation_options(MetadataStructuredLocationKind::Created);
        ASSERT_EQ(translate_xmp_location_to_structured_metadata(source, options,
                                                                &output)
                      .status,
                  Status::Ok);
        EXPECT_NE(creation_leaf(output, "LocationCreated/Iptc4xmpExt:City"),
                  nullptr);
        EXPECT_EQ(creation_leaf(output, "LocationCreated[1]/Iptc4xmpExt:City"),
                  nullptr);
        options.location_index = 2U;
        creation_failure(source, Status::InvalidOptions, options);
    }
    TEST(MetadataLocationCreation,
         RemovalRetainsUnknownFieldsAndDoesNotRenumberOtherRecords)
    {
        auto options            = creation_options();
        options.conflict_policy = Policy::ReplaceExisting;
        MetaStore source        = creation_source(EntryFlags::Dirty
                                                  | EntryFlags::Deleted);
        for (const auto child : kChildren)
            xmp(source, kExt, "LocationShown[1]/" + std::string(child), "Old",
                EntryFlags::None);
        xmp(source, kExt, "LocationShown[1]/WorldRegion", "Asia",
            EntryFlags::None);
        source.finalize();
        MetaStore output;
        auto result = translate_xmp_location_to_structured_metadata(source,
                                                                    options,
                                                                    &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.entries_removed, 5U);
        EXPECT_NE(creation_leaf(output, "LocationShown[1]/WorldRegion"),
                  nullptr);
        source = MetaStore {};
        xmp(source, kPs, "City", "", EntryFlags::Dirty | EntryFlags::Deleted);
        xmp(source, kExt, "LocationShown[1]/City", "First", EntryFlags::None);
        xmp(source, kExt, "LocationShown[2]/City", "Second", EntryFlags::None);
        creation_failure(source, Status::UnsupportedSourceShape, options);
        options.location_index = 2U;
        result = translate_xmp_location_to_structured_metadata(source, options,
                                                               &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_NE(creation_leaf(output, "LocationShown[1]/City"), nullptr);
        EXPECT_EQ(creation_leaf(output, "LocationShown[2]/City"), nullptr);
    }
    TEST(MetadataLocationCreation, RejectsOpaqueMixedAndCompetingFieldShapes)
    {
        for (const std::string_view path :
             { "LocationShown", "LocationShown[1]", "LocationShown[01]/City",
               "LocationShown/City", "LocationShown[1]/City[@xml:lang=en]",
               "LocationShown[1]/City/Nested" }) {
            MetaStore source = creation_source();
            xmp(source, kExt, path, "Opaque", EntryFlags::None);
            creation_failure(source, Status::UnsupportedSourceShape,
                             creation_options());
        }
        MetaStore source = creation_source();
        xmp(source, kExt, "LocationCreated/City", "Scalar", EntryFlags::None);
        xmp(source, kExt, "LocationCreated[1]/CountryName", "Indexed",
            EntryFlags::None);
        creation_failure(source, Status::UnsupportedSourceShape,
                         creation_options(
                             MetadataStructuredLocationKind::Created));
    }
    TEST(MetadataLocationCreation, LimitsAndLateInvalidTextAreAtomic)
    {
        MetaStore source          = creation_source();
        auto options              = creation_options();
        options.max_added_entries = 4U;
        creation_failure(source, Status::EntryLimitExceeded, options);
        options                = creation_options();
        options.max_operations = 4U;
        creation_failure(source, Status::OperationLimitExceeded, options);
        options                      = creation_options();
        options.max_total_text_bytes = 5U;
        creation_failure(source, Status::SourceLimitExceeded, options);
        options                       = creation_options();
        options.max_source_properties = 4U;
        creation_failure(source, Status::SourceLimitExceeded, options);
        source = MetaStore {};
        xmp(source, kPs, "City", "Valid");
        xmp(source, kCore, "CountryCode", "jp");
        creation_failure(source, Status::InvalidSourceValue,
                         creation_options());
        EXPECT_EQ(creation_leaf(source, "LocationShown[1]/Iptc4xmpExt:City"),
                  nullptr);
        source = MetaStore {};
        xmp(source, kPs, "City", std::string(33U, 'A'));
        creation_failure(source, Status::ValueTooLong, creation_options());
        source = MetaStore {};
        xmp(source, kPs, "City", "");
        creation_failure(source, Status::InvalidSourceValue,
                         creation_options());
    }
    TEST(MetadataLocationCreation,
         PortableBagRoundTripRetainsUnicodeAndRecordIdentity)
    {
        for (const auto kind : { MetadataStructuredLocationKind::Shown,
                                 MetadataStructuredLocationKind::Created }) {
            MetaStore source;
            xmp(source, kPs, "City", "\xe4\xba\xac\xe9\x83\xbd");
            xmp(source, kCore, "Location", "Garden & water");
            source.finalize();
            const auto options = creation_options(kind);
            ASSERT_EQ(translate_xmp_location_to_structured_metadata(source,
                                                                    options,
                                                                    &source)
                          .status,
                      Status::Ok);
            XmpPortableOptions dump_options;
            dump_options.include_existing_xmp = true;
            dump_options.include_iptc         = false;
            std::vector<std::byte> bytes(8192U);
            const auto dumped = dump_xmp_portable(source, bytes, dump_options);
            ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
            bytes.resize(dumped.written);
            const std::string_view xml(reinterpret_cast<const char*>(
                                           bytes.data()),
                                       bytes.size());
            SCOPED_TRACE(xml);
            EXPECT_NE(xml.find("<rdf:Bag>"), std::string_view::npos);
            EXPECT_NE(xml.find("Garden &amp; water"), std::string_view::npos);
            MetaStore decoded;
            ASSERT_EQ(decode_xmp_packet(bytes, decoded).status,
                      XmpDecodeStatus::Ok);
            decoded.finalize();
            const std::string root
                = kind == MetadataStructuredLocationKind::Shown
                      ? "LocationShown[1]"
                      : "LocationCreated[1]";
            const Entry* city = creation_leaf(decoded, root + "/City");
            ASSERT_NE(city, nullptr);
            EXPECT_EQ(view(decoded, city->value.data.span),
                      "\xe4\xba\xac\xe9\x83\xbd");
            ASSERT_NE(creation_leaf(decoded, root + "/Sublocation"), nullptr);
        }
    }
}  // namespace
}  // namespace openmeta
