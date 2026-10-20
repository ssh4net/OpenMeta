// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_translation.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static constexpr std::string_view kDcNs = "http://purl.org/dc/elements/1.1/";
    static constexpr std::string_view kXmpNs  = "http://ns.adobe.com/xap/1.0/";
    static constexpr std::string_view kTiffNs = "http://ns.adobe.com/tiff/1.0/";
    static constexpr std::string_view kExifNs = "http://ns.adobe.com/exif/1.0/";
    static constexpr std::string_view kPhotoshopNs
        = "http://ns.adobe.com/photoshop/1.0/";
    static constexpr std::string_view kIptcExtNs
        = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";

    struct Outcome final {
        bool ok                    = false;
        bool internal_error        = false;
        uint32_t source_properties = 0U;
        uint32_t groups_translated = 0U;
        uint32_t groups_preserved  = 0U;
        uint32_t groups_unchanged  = 0U;
        uint32_t entries_added     = 0U;
        uint32_t entries_updated   = 0U;
        uint32_t entries_removed   = 0U;
        bool utf8_charset_added    = false;
    };

    using TranslationCall = Outcome (*)(const MetaStore&, MetaStore*);

    static EntryId add_xmp_text(MetaStore* store, BlockId block,
                                std::string_view schema, std::string_view path,
                                std::string_view text,
                                EntryFlags flags = EntryFlags::Dirty)
    {
        Entry entry;
        entry.key   = make_xmp_property_key(store->arena(), schema, path);
        entry.value = make_text(store->arena(), text, TextEncoding::Utf8);
        entry.origin.block = block;
        entry.flags        = flags;
        return store->add_entry(entry);
    }

    static EntryId add_xmp_value(MetaStore* store, BlockId block,
                                 std::string_view schema, std::string_view path,
                                 const MetaValue& value,
                                 EntryFlags flags = EntryFlags::Dirty)
    {
        Entry entry;
        entry.key   = make_xmp_property_key(store->arena(), schema, path);
        entry.value = value;
        entry.origin.block = block;
        entry.flags        = flags;
        return store->add_entry(entry);
    }

    static EntryId add_iptc_bytes(MetaStore* store, BlockId block,
                                  uint16_t record, uint16_t dataset,
                                  std::string_view bytes)
    {
        Entry entry;
        entry.key = make_iptc_dataset_key(record, dataset);
        entry.value
            = make_bytes(store->arena(),
                         std::span<const std::byte>(
                             reinterpret_cast<const std::byte*>(bytes.data()),
                             bytes.size()));
        entry.origin.block = block;
        return store->add_entry(entry);
    }

    static bool same_span(const MetaStore& left_store, ByteSpan left,
                          const MetaStore& right_store, ByteSpan right)
    {
        const std::span<const std::byte> left_bytes = left_store.arena().span(
            left);
        const std::span<const std::byte> right_bytes = right_store.arena().span(
            right);
        if (left_bytes.size() != right_bytes.size()) {
            return false;
        }
        for (size_t i = 0U; i < left_bytes.size(); ++i) {
            if (left_bytes[i] != right_bytes[i]) {
                return false;
            }
        }
        return true;
    }

    static bool same_key(const MetaStore& left_store, const MetaKey& left,
                         const MetaStore& right_store, const MetaKey& right)
    {
        if (left.kind != right.kind) {
            return false;
        }
        switch (left.kind) {
        case MetaKeyKind::IptcDataset:
            return left.data.iptc_dataset.record
                       == right.data.iptc_dataset.record
                   && left.data.iptc_dataset.dataset
                          == right.data.iptc_dataset.dataset;
        case MetaKeyKind::XmpProperty:
            return same_span(left_store, left.data.xmp_property.schema_ns,
                             right_store, right.data.xmp_property.schema_ns)
                   && same_span(left_store,
                                left.data.xmp_property.property_path,
                                right_store,
                                right.data.xmp_property.property_path);
        default: return false;
        }
    }

    static bool same_value(const MetaStore& left_store, const MetaValue& left,
                           const MetaStore& right_store, const MetaValue& right)
    {
        if (left.kind != right.kind || left.elem_type != right.elem_type
            || left.text_encoding != right.text_encoding
            || left.count != right.count) {
            return false;
        }
        switch (left.kind) {
        case MetaValueKind::Empty: return true;
        case MetaValueKind::Scalar:
            switch (left.elem_type) {
            case MetaElementType::U8:
            case MetaElementType::U16:
            case MetaElementType::U32:
            case MetaElementType::U64: return left.data.u64 == right.data.u64;
            case MetaElementType::I8:
            case MetaElementType::I16:
            case MetaElementType::I32:
            case MetaElementType::I64: return left.data.i64 == right.data.i64;
            case MetaElementType::F32:
                return left.data.f32_bits == right.data.f32_bits;
            case MetaElementType::F64:
                return left.data.f64_bits == right.data.f64_bits;
            case MetaElementType::URational:
                return left.data.ur.numer == right.data.ur.numer
                       && left.data.ur.denom == right.data.ur.denom;
            case MetaElementType::SRational:
                return left.data.sr.numer == right.data.sr.numer
                       && left.data.sr.denom == right.data.sr.denom;
            }
            return false;
        case MetaValueKind::Array:
        case MetaValueKind::Bytes:
        case MetaValueKind::Text:
            return same_span(left_store, left.data.span, right_store,
                             right.data.span);
        }
        return false;
    }

    static bool same_entry(const MetaStore& left_store, const Entry& left,
                           const MetaStore& right_store, const Entry& right)
    {
        return same_key(left_store, left.key, right_store, right.key)
               && same_value(left_store, left.value, right_store, right.value)
               && left.origin.block == right.origin.block
               && left.origin.order_in_block == right.origin.order_in_block
               && left.origin.wire_type.family == right.origin.wire_type.family
               && left.origin.wire_type.code == right.origin.wire_type.code
               && left.origin.wire_count == right.origin.wire_count
               && same_span(left_store, left.origin.wire_type_name, right_store,
                            right.origin.wire_type_name)
               && left.origin.name_context_kind
                      == right.origin.name_context_kind
               && left.origin.name_context_variant
                      == right.origin.name_context_variant
               && left.flags == right.flags;
    }

    static bool same_store(const MetaStore& left, const MetaStore& right)
    {
        if (left.is_finalized() != right.is_finalized()
            || left.resource_limit_exceeded() != right.resource_limit_exceeded()
            || left.block_count() != right.block_count()
            || left.entries().size() != right.entries().size()
            || left.arena().bytes().size() != right.arena().bytes().size()) {
            return false;
        }
        for (size_t i = 0U; i < left.arena().bytes().size(); ++i) {
            if (left.arena().bytes()[i] != right.arena().bytes()[i]) {
                return false;
            }
        }
        for (BlockId i = 0U; i < left.block_count(); ++i) {
            const BlockInfo& left_info  = left.block_info(i);
            const BlockInfo& right_info = right.block_info(i);
            if (left_info.format != right_info.format
                || left_info.container != right_info.container
                || left_info.id != right_info.id) {
                return false;
            }
        }
        for (size_t i = 0U; i < left.entries().size(); ++i) {
            if (!same_entry(left, left.entry(static_cast<EntryId>(i)), right,
                            right.entry(static_cast<EntryId>(i)))) {
                return false;
            }
        }
        return true;
    }

    static MetaStore make_sentinel_output()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        (void)add_xmp_text(&store, block, "urn:openmeta:test:commit-limits",
                           "Sentinel", "keep", EntryFlags::None);
        store.finalize();
        return store;
    }

    static MetaStore make_technical_source()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        (void)add_xmp_text(&store, block, kTiffNs, "Make", "Canon");
        store.finalize();
        return store;
    }

    static MetaStore make_creation_date_source()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        (void)add_xmp_text(&store, block, kXmpNs, "CreateDate",
                           "2024-01-02T03:04:05Z");
        store.finalize();
        return store;
    }

    static MetaStore make_descriptive_source()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        (void)add_xmp_text(&store, block, kDcNs, "title[@xml:lang=x-default]",
                           "A title");
        store.finalize();
        return store;
    }

    static MetaStore make_structured_location_source()
    {
        static constexpr std::array<std::string_view, 5U> kPaths = {
            "LocationShown[1]/City", "LocationShown[1]/Sublocation",
            "LocationShown[1]/ProvinceState", "LocationShown[1]/CountryName",
            "LocationShown[1]/CountryCode"
        };
        static constexpr std::array<std::string_view, 5U> kValues
            = { "Kyoto", "Garden", "Kyoto", "Japan", "JPN" };
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        for (size_t i = 0U; i < kPaths.size(); ++i) {
            (void)add_xmp_text(&store, block, kIptcExtNs, kPaths[i],
                               kValues[i]);
        }
        store.finalize();
        return store;
    }

    static MetaStore make_location_creation_source()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        (void)add_xmp_text(&store, block, kPhotoshopNs, "City", "Kyoto");
        store.finalize();
        return store;
    }

    static MetaStore make_geometry_source()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        (void)add_xmp_value(&store, block, kTiffNs, "Orientation",
                            make_u16(1U));
        store.finalize();
        return store;
    }

    static MetaStore make_gps_source()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        (void)add_xmp_text(&store, block, kExifNs, "GPSLatitude", "35,0N");
        store.finalize();
        return store;
    }

    static MetaStore make_iptc_source()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        (void)add_iptc_bytes(&store, block, 2U, 5U, "Title");
        store.finalize();
        return store;
    }

    static MetaStore make_iptc_date_source()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        (void)add_iptc_bytes(&store, block, 2U, 55U, "20240229");
        store.finalize();
        return store;
    }

    static Outcome call_technical(const MetaStore& source, MetaStore* output)
    {
        const MetadataTechnicalTranslationResult result
            = translate_xmp_technical_metadata(
                source, MetadataTechnicalTranslationOptions {}, output);
        return { result.status == MetadataTechnicalTranslationStatus::Ok,
                 result.status
                     == MetadataTechnicalTranslationStatus::InternalError,
                 result.source_properties,
                 result.groups_translated,
                 result.groups_preserved,
                 result.groups_unchanged,
                 result.entries_added,
                 result.entries_updated,
                 result.entries_removed,
                 false };
    }

    static Outcome call_creation_dates(const MetaStore& source,
                                       MetaStore* output)
    {
        MetadataDateTranslationOptions options;
        options.create_date_to_iptc_digital_creation = false;
        options.date_created_to_iptc_created         = false;
        options.date_time_original_to_exif_original  = false;
        const MetadataDateTranslationResult result
            = translate_xmp_creation_dates(source, options, output);
        return { result.status == MetadataDateTranslationStatus::Ok,
                 result.status == MetadataDateTranslationStatus::InternalError,
                 result.source_properties,
                 result.groups_translated,
                 result.groups_preserved,
                 result.groups_unchanged,
                 result.entries_added,
                 result.entries_updated,
                 result.entries_removed,
                 false };
    }

    static Outcome call_descriptive(const MetaStore& source, MetaStore* output)
    {
        MetadataDescriptiveTranslationOptions options;
        options.description_to_iptc_caption = false;
        options.creators_to_iptc_bylines    = false;
        options.keywords_to_iptc_keywords   = false;
        options.copyright_to_iptc_copyright = false;
        options.credit_to_iptc_credit       = false;
        options.source_to_iptc_source       = false;
        const MetadataDescriptiveTranslationResult result
            = translate_xmp_descriptive_metadata(source, options, output);
        return { result.status == MetadataDescriptiveTranslationStatus::Ok,
                 result.status
                     == MetadataDescriptiveTranslationStatus::InternalError,
                 result.source_properties,
                 result.groups_translated,
                 result.groups_preserved,
                 result.groups_unchanged,
                 result.entries_added,
                 result.entries_updated,
                 result.entries_removed,
                 result.utf8_charset_added };
    }

    static Outcome call_structured_location(const MetaStore& source,
                                            MetaStore* output)
    {
        const MetadataDescriptiveTranslationResult result
            = translate_xmp_structured_location_metadata(
                source, MetadataStructuredLocationTranslationOptions {},
                output);
        return { result.status == MetadataDescriptiveTranslationStatus::Ok,
                 result.status
                     == MetadataDescriptiveTranslationStatus::InternalError,
                 result.source_properties,
                 result.groups_translated,
                 result.groups_preserved,
                 result.groups_unchanged,
                 result.entries_added,
                 result.entries_updated,
                 result.entries_removed,
                 result.utf8_charset_added };
    }

    static Outcome call_location_creation(const MetaStore& source,
                                          MetaStore* output)
    {
        MetadataLocationCreationTranslationOptions options;
        options.location_kind  = MetadataStructuredLocationKind::Shown;
        options.location_index = 1U;
        const MetadataDescriptiveTranslationResult result
            = translate_xmp_location_to_structured_metadata(source, options,
                                                            output);
        return { result.status == MetadataDescriptiveTranslationStatus::Ok,
                 result.status
                     == MetadataDescriptiveTranslationStatus::InternalError,
                 result.source_properties,
                 result.groups_translated,
                 result.groups_preserved,
                 result.groups_unchanged,
                 result.entries_added,
                 result.entries_updated,
                 result.entries_removed,
                 result.utf8_charset_added };
    }

    static Outcome call_geometry(const MetaStore& source, MetaStore* output)
    {
        TransferTargetImageSpec target;
        target.has_orientation = true;
        target.orientation     = 1U;
        MetadataGeometryTranslationOptions options;
        options.dimensions_to_exif = false;
        const MetadataGeometryTranslationResult result
            = translate_xmp_image_geometry(source, target, options, output);
        return { result.status == MetadataGeometryTranslationStatus::Ok,
                 result.status
                     == MetadataGeometryTranslationStatus::InternalError,
                 result.source_properties,
                 result.groups_translated,
                 result.groups_preserved,
                 result.groups_unchanged,
                 result.entries_added,
                 result.entries_updated,
                 result.entries_removed,
                 false };
    }

    static Outcome call_gps(const MetaStore& source, MetaStore* output)
    {
        const MetadataGpsTranslationResult result = translate_xmp_gps_metadata(
            source, MetadataGpsTranslationOptions {}, output);
        return { result.status == MetadataGpsTranslationStatus::Ok,
                 result.status == MetadataGpsTranslationStatus::InternalError,
                 result.source_properties,
                 result.groups_translated,
                 result.groups_preserved,
                 result.groups_unchanged,
                 result.entries_added,
                 result.entries_updated,
                 result.entries_removed,
                 false };
    }

    static Outcome call_iptc_to_xmp(const MetaStore& source, MetaStore* output)
    {
        const MetadataIptcToXmpResult result
            = translate_iptc_to_xmp(source, MetadataIptcToXmpOptions {},
                                    output);
        return { result.status == MetadataIptcToXmpStatus::Ok,
                 result.status == MetadataIptcToXmpStatus::InternalError,
                 result.source_properties,
                 result.groups_translated,
                 result.groups_preserved,
                 result.groups_unchanged,
                 result.entries_added,
                 result.entries_updated,
                 result.entries_removed,
                 false };
    }

    static Outcome call_iptc_dates_to_xmp(const MetaStore& source,
                                          MetaStore* output)
    {
        MetadataIptcDateToXmpOptions options;
        options.digital_creation = false;
        const MetadataIptcDateToXmpResult result
            = translate_iptc_dates_to_xmp(source, options, output);
        return { result.status == MetadataIptcDateToXmpStatus::Ok,
                 result.status == MetadataIptcDateToXmpStatus::InternalError,
                 result.source_properties,
                 result.groups_translated,
                 result.groups_preserved,
                 result.groups_unchanged,
                 result.entries_added,
                 result.entries_updated,
                 result.entries_removed,
                 false };
    }

    static void expect_zero_mutations(const Outcome& result)
    {
        EXPECT_EQ(result.groups_translated, 0U);
        EXPECT_EQ(result.entries_added, 0U);
        EXPECT_EQ(result.entries_updated, 0U);
        EXPECT_EQ(result.entries_removed, 0U);
        EXPECT_FALSE(result.utf8_charset_added);
    }

    static void expect_resource_limit_rollback(const MetaStore& source,
                                               TranslationCall translate,
                                               std::string_view name)
    {
        for (uint8_t ceiling = 0U; ceiling < 2U; ++ceiling) {
            SCOPED_TRACE(ceiling == 0U ? "entry ceiling" : "arena ceiling");
            MetaStore constrained = source;
            if (ceiling == 0U) {
                constrained.constrain_resources(static_cast<uint32_t>(
                                                    source.entries().size()),
                                                0U);
            } else {
                constrained.constrain_resources(0U,
                                                source.arena().bytes().size());
            }
            ASSERT_FALSE(constrained.resource_limit_exceeded());
            const MetaStore constrained_before = constrained;

            MetaStore separate_output       = make_sentinel_output();
            const MetaStore separate_before = separate_output;
            const Outcome separate_result   = translate(constrained,
                                                        &separate_output);
            EXPECT_FALSE(separate_result.ok) << name;
            EXPECT_TRUE(separate_result.internal_error) << name;
            expect_zero_mutations(separate_result);
            EXPECT_TRUE(same_store(constrained, constrained_before)) << name;
            EXPECT_TRUE(same_store(separate_output, separate_before)) << name;

            MetaStore alias              = constrained;
            const MetaStore alias_before = alias;
            const Outcome alias_result   = translate(alias, &alias);
            EXPECT_FALSE(alias_result.ok) << name;
            EXPECT_TRUE(alias_result.internal_error) << name;
            expect_zero_mutations(alias_result);
            EXPECT_TRUE(same_store(alias, alias_before)) << name;
        }
    }

    static void expect_success_control(const MetaStore& source,
                                       TranslationCall translate,
                                       std::string_view name)
    {
        MetaStore output;
        const Outcome result = translate(source, &output);
        EXPECT_TRUE(result.ok) << name;
        EXPECT_FALSE(result.internal_error) << name;
        EXPECT_GT(result.entries_added + result.entries_updated
                      + result.entries_removed,
                  0U)
            << name;
        EXPECT_TRUE(output.is_finalized()) << name;
        EXPECT_FALSE(output.resource_limit_exceeded()) << name;
    }

    TEST(MetadataCommitLimits,
         EveryChangedPublicationSiteRollsBackEntryAndArenaLimits)
    {
        const MetaStore technical = make_technical_source();
        expect_success_control(technical, call_technical, "technical");
        expect_resource_limit_rollback(technical, call_technical, "technical");

        const MetaStore creation_dates = make_creation_date_source();
        expect_success_control(creation_dates, call_creation_dates,
                               "creation dates");
        expect_resource_limit_rollback(creation_dates, call_creation_dates,
                                       "creation dates");

        const MetaStore descriptive = make_descriptive_source();
        expect_success_control(descriptive, call_descriptive, "descriptive");
        expect_resource_limit_rollback(descriptive, call_descriptive,
                                       "descriptive");

        const MetaStore structured_location = make_structured_location_source();
        expect_success_control(structured_location, call_structured_location,
                               "structured location flattening");
        expect_resource_limit_rollback(structured_location,
                                       call_structured_location,
                                       "structured location flattening");

        const MetaStore location_creation = make_location_creation_source();
        expect_success_control(location_creation, call_location_creation,
                               "structured location synthesis");
        expect_resource_limit_rollback(location_creation,
                                       call_location_creation,
                                       "structured location synthesis");

        const MetaStore geometry = make_geometry_source();
        expect_success_control(geometry, call_geometry, "geometry");
        expect_resource_limit_rollback(geometry, call_geometry, "geometry");

        const MetaStore gps = make_gps_source();
        expect_success_control(gps, call_gps, "GPS");
        expect_resource_limit_rollback(gps, call_gps, "GPS");

        const MetaStore iptc = make_iptc_source();
        expect_success_control(iptc, call_iptc_to_xmp, "IPTC to XMP");
        expect_resource_limit_rollback(iptc, call_iptc_to_xmp, "IPTC to XMP");

        const MetaStore iptc_dates = make_iptc_date_source();
        expect_success_control(iptc_dates, call_iptc_dates_to_xmp,
                               "IPTC dates to XMP");
        expect_resource_limit_rollback(iptc_dates, call_iptc_dates_to_xmp,
                                       "IPTC dates to XMP");
    }

}  // namespace
}  // namespace openmeta
