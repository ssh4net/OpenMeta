// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_creation.h"
#include "openmeta/metadata_query.h"
#include "openmeta/metadata_transfer.h"
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

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static std::string dump_created_xmp(const MetaStore& store)
    {
        XmpPortableOptions options;
        options.include_exif         = false;
        options.include_iptc         = false;
        options.include_existing_xmp = true;

        std::vector<std::byte> out(16384U);
        const XmpDumpResult result = dump_xmp_portable(store, out, options);
        EXPECT_EQ(result.status, XmpDumpStatus::Ok);
        return std::string(reinterpret_cast<const char*>(out.data()),
                           static_cast<size_t>(result.written));
    }

    TEST(MetadataCreation, BuildsFinalizedPortableXmpStore)
    {
        const std::array fields = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                        "Evening frame"),
            make_metadata_creation_text(MetadataCreationFieldKind::Description,
                                        "City lights"),
            make_metadata_creation_text(MetadataCreationFieldKind::Creator,
                                        "Alice"),
            make_metadata_creation_text(MetadataCreationFieldKind::Creator,
                                        "Bob"),
            make_metadata_creation_text(MetadataCreationFieldKind::Keyword,
                                        "night"),
            make_metadata_creation_text(MetadataCreationFieldKind::Keyword,
                                        "street"),
            make_metadata_creation_text(MetadataCreationFieldKind::Copyright,
                                        "Copyright 2026"),
            make_metadata_creation_text(
                MetadataCreationFieldKind::RightsUsageTerms,
                "Licensed use only"),
            make_metadata_creation_text(MetadataCreationFieldKind::Credit,
                                        "OpenMeta News"),
            make_metadata_creation_text(MetadataCreationFieldKind::CameraMake,
                                        "Example Camera"),
            make_metadata_creation_i32(MetadataCreationFieldKind::Rating, 5),
            make_metadata_creation_u32(MetadataCreationFieldKind::Orientation,
                                       6U),
            make_metadata_creation_u32(MetadataCreationFieldKind::PixelWidth,
                                       6000U),
            make_metadata_creation_u32(MetadataCreationFieldKind::IsoSensitivity,
                                       400U),
            make_metadata_creation_urational(
                MetadataCreationFieldKind::ExposureTime, 1U, 125U),
            make_metadata_creation_urational(MetadataCreationFieldKind::FNumber,
                                             28U, 10U),
            make_metadata_creation_urational(
                MetadataCreationFieldKind::FocalLength, 50U, 1U),
        };

        MetadataCreationRequest request;
        request.fields = fields;
        MetaStore store;
        const MetadataCreationResult result = create_metadata(request, &store);

        ASSERT_EQ(result.status, MetadataCreationStatus::Ok);
        EXPECT_EQ(result.field_count, fields.size());
        EXPECT_EQ(result.entries_created, fields.size());
        EXPECT_EQ(result.failed_field_index,
                  kInvalidMetadataCreationFieldIndex);
        ASSERT_EQ(store.block_count(), 1U);
        ASSERT_EQ(store.entries().size(), fields.size());

        for (uint32_t i = 0U; i < store.entries().size(); ++i) {
            const Entry& entry = store.entry(i);
            EXPECT_EQ(entry.key.kind, MetaKeyKind::XmpProperty);
            EXPECT_EQ(entry.origin.block, 0U);
            EXPECT_EQ(entry.origin.order_in_block, i);
            EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty));
            EXPECT_FALSE(any(entry.flags, EntryFlags::Derived));
        }

        EXPECT_EQ(
            arena_string(store.arena(),
                         store.entry(2U).key.data.xmp_property.property_path),
            "creator[1]");
        EXPECT_EQ(
            arena_string(store.arena(),
                         store.entry(3U).key.data.xmp_property.property_path),
            "creator[2]");
        EXPECT_EQ(
            arena_string(store.arena(),
                         store.entry(4U).key.data.xmp_property.property_path),
            "subject[1]");
        EXPECT_EQ(store.entry(10U).value.elem_type, MetaElementType::I32);
        EXPECT_EQ(store.entry(10U).value.data.i64, 5);
        EXPECT_EQ(store.entry(14U).value.elem_type, MetaElementType::URational);
        EXPECT_EQ(store.entry(14U).value.data.ur.numer, 1U);
        EXPECT_EQ(store.entry(14U).value.data.ur.denom, 125U);

        const std::string xmp = dump_created_xmp(store);
        EXPECT_NE(xmp.find("<dc:title>"), std::string::npos);
        EXPECT_NE(xmp.find(
                      "<rdf:li xml:lang=\"x-default\">Evening frame</rdf:li>"),
                  std::string::npos);
        EXPECT_NE(xmp.find("<dc:creator>"), std::string::npos);
        EXPECT_NE(xmp.find("<rdf:Seq>"), std::string::npos);
        EXPECT_NE(xmp.find("<rdf:li>Alice</rdf:li>"), std::string::npos);
        EXPECT_NE(xmp.find("<dc:subject>"), std::string::npos);
        EXPECT_NE(xmp.find("<rdf:Bag>"), std::string::npos);
        EXPECT_NE(xmp.find("<rdf:li>street</rdf:li>"), std::string::npos);
        EXPECT_NE(xmp.find("<xmp:Rating>5</xmp:Rating>"), std::string::npos);
        EXPECT_NE(xmp.find("<tiff:Orientation>6</tiff:Orientation>"),
                  std::string::npos);
        EXPECT_NE(xmp.find("<exif:ExposureTime>1/125</exif:ExposureTime>"),
                  std::string::npos);

        const MetadataQueryResult descriptive = query_descriptive_metadata(
            store);
        EXPECT_GE(descriptive.matches.size(), 8U);

        PrepareTransferRequest transfer_request;
        transfer_request.target_format      = TransferTargetFormat::Jpeg;
        transfer_request.include_exif_app1  = false;
        transfer_request.include_icc_app2   = false;
        transfer_request.include_iptc_app13 = false;
        transfer_request.xmp_project_exif   = false;
        transfer_request.xmp_project_iptc   = false;

        PreparedTransferBundle bundle;
        const PrepareTransferResult prepared
            = prepare_metadata_for_target(store, transfer_request, &bundle);
        ASSERT_EQ(prepared.status, TransferStatus::Ok);
        ASSERT_EQ(bundle.blocks.size(), 1U);
        EXPECT_EQ(bundle.blocks[0].kind, TransferBlockKind::Xmp);
        const std::string_view prepared_xmp(
            reinterpret_cast<const char*>(bundle.blocks[0].payload.data()),
            bundle.blocks[0].payload.size());
        EXPECT_NE(prepared_xmp.find("<dc:title>"), std::string_view::npos);
        EXPECT_NE(prepared_xmp.find("<xmp:Rating>5</xmp:Rating>"),
                  std::string_view::npos);
        EXPECT_EQ(prepared_xmp.find("<tiff:Orientation>"),
                  std::string_view::npos);
    }

    TEST(MetadataCreation, PreservesUnicodeText)
    {
        const std::array fields = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                        "\xe5\xa4\x9c\xe6\x99\xaf"),
        };
        MetadataCreationRequest request;
        request.fields = fields;
        MetaStore store;
        const MetadataCreationResult result = create_metadata(request, &store);
        ASSERT_EQ(result.status, MetadataCreationStatus::Ok);

        const std::string xmp = dump_created_xmp(store);
        EXPECT_NE(xmp.find("\xe5\xa4\x9c\xe6\x99\xaf"), std::string::npos);
    }

    TEST(MetadataCreation, RejectsInvalidRequestsTransactionally)
    {
        MetaStore store;
        const BlockId original_block = store.add_block(BlockInfo {});
        ASSERT_NE(original_block, kInvalidBlockId);
        Entry original;
        original.key   = make_xmp_property_key(store.arena(),
                                               "http://ns.adobe.com/xap/1.0/",
                                               "Label");
        original.value = make_text(store.arena(), "keep", TextEncoding::Utf8);
        original.origin.block = original_block;
        ASSERT_NE(store.add_entry(original), kInvalidEntryId);
        store.finalize();

        const std::array duplicate_fields = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title, "one"),
            make_metadata_creation_text(MetadataCreationFieldKind::Title, "two"),
        };
        MetadataCreationRequest duplicate_request;
        duplicate_request.fields = duplicate_fields;
        const MetadataCreationResult duplicate_result
            = create_metadata(duplicate_request, &store);
        EXPECT_EQ(duplicate_result.status,
                  MetadataCreationStatus::DuplicateSingleton);
        EXPECT_EQ(duplicate_result.failed_field_index, 1U);
        ASSERT_EQ(store.entries().size(), 1U);
        EXPECT_EQ(arena_string(store.arena(), store.entry(0U).value.data.span),
                  "keep");

        const std::array wrong_kind = {
            make_metadata_creation_text(MetadataCreationFieldKind::Orientation,
                                        "6"),
        };
        MetadataCreationRequest wrong_request;
        wrong_request.fields = wrong_kind;
        EXPECT_EQ(create_metadata(wrong_request, &store).status,
                  MetadataCreationStatus::WrongValueKind);

        const std::array empty_text = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title, ""),
        };
        MetadataCreationRequest empty_request;
        empty_request.fields = empty_text;
        EXPECT_EQ(create_metadata(empty_request, &store).status,
                  MetadataCreationStatus::EmptyText);

        const std::string invalid_utf8("\xc0\xaf", 2U);
        const std::array invalid_text = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                        invalid_utf8),
        };
        MetadataCreationRequest invalid_text_request;
        invalid_text_request.fields = invalid_text;
        EXPECT_EQ(create_metadata(invalid_text_request, &store).status,
                  MetadataCreationStatus::InvalidText);

        const std::array invalid_rating = {
            make_metadata_creation_i32(MetadataCreationFieldKind::Rating, 6),
        };
        MetadataCreationRequest invalid_rating_request;
        invalid_rating_request.fields = invalid_rating;
        EXPECT_EQ(create_metadata(invalid_rating_request, &store).status,
                  MetadataCreationStatus::InvalidValue);

        const std::array invalid_rational = {
            make_metadata_creation_urational(
                MetadataCreationFieldKind::ExposureTime, 1U, 0U),
        };
        MetadataCreationRequest invalid_rational_request;
        invalid_rational_request.fields = invalid_rational;
        EXPECT_EQ(create_metadata(invalid_rational_request, &store).status,
                  MetadataCreationStatus::InvalidValue);

        MetadataCreationRequest invalid_limits_request;
        invalid_limits_request.limits.max_fields = 0U;
        EXPECT_EQ(create_metadata(invalid_limits_request, &store).status,
                  MetadataCreationStatus::InvalidLimits);
        EXPECT_EQ(create_metadata(invalid_limits_request, nullptr).status,
                  MetadataCreationStatus::NullOutput);
    }

    TEST(MetadataCreation, EnforcesResourceLimits)
    {
        const std::array fields = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                        "12345"),
        };
        MetadataCreationRequest text_limit_request;
        text_limit_request.fields                          = fields;
        text_limit_request.limits.max_text_bytes_per_field = 4U;
        MetaStore store;
        EXPECT_EQ(create_metadata(text_limit_request, &store).status,
                  MetadataCreationStatus::TextTooLong);

        std::vector<MetadataCreationField> many_fields(
            kMetadataCreationMaxFields + 1U,
            make_metadata_creation_text(MetadataCreationFieldKind::Creator,
                                        "creator"));
        MetadataCreationRequest field_limit_request;
        field_limit_request.fields = many_fields;
        EXPECT_EQ(create_metadata(field_limit_request, &store).status,
                  MetadataCreationStatus::TooManyFields);
    }

    TEST(MetadataCreation, EmptyRequestBuildsEmptyFinalizedStore)
    {
        MetaStore store;
        MetadataCreationRequest request;
        const MetadataCreationResult result = create_metadata(request, &store);
        EXPECT_EQ(result.status, MetadataCreationStatus::Ok);
        EXPECT_EQ(result.field_count, 0U);
        EXPECT_EQ(result.entries_created, 0U);
        EXPECT_EQ(store.block_count(), 0U);
        EXPECT_TRUE(store.entries().empty());
    }

    TEST(MetadataCreation, ExposesStableNames)
    {
        EXPECT_STREQ(metadata_creation_field_kind_name(
                         MetadataCreationFieldKind::RightsUsageTerms),
                     "rights_usage_terms");
        EXPECT_STREQ(metadata_creation_status_name(
                         MetadataCreationStatus::DuplicateSingleton),
                     "duplicate_singleton");
    }

}  // namespace
}  // namespace openmeta
