// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"
#include "openmeta/exif_tiff_serialize.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static WireType tiff_wire(uint16_t code) noexcept
    {
        WireType type;
        type.family = WireFamily::Tiff;
        type.code   = code;
        return type;
    }

    static MetadataAuthoringEntry
    authoring_entry(const MetaKeyView& key, const MetaValueView& value,
                    WireType wire_type  = WireType {},
                    uint32_t wire_count = 0U) noexcept
    {
        MetadataAuthoringEntry entry;
        entry.key        = key;
        entry.value      = value;
        entry.wire_type  = wire_type;
        entry.wire_count = wire_count;
        return entry;
    }

    static std::string_view arena_text(const MetaStore& store,
                                       const MetaValue& value) noexcept
    {
        const std::span<const std::byte> bytes = store.arena().span(
            value.data.span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool has_issue(const MetadataValidationResult& result,
                          MetadataValidationIssueCode code) noexcept
    {
        for (const MetadataValidationIssue& issue : result.issues) {
            if (issue.code == code) {
                return true;
            }
        }
        return false;
    }

    static const PreparedTransferBlock*
    exif_block(const PreparedTransferBundle& bundle) noexcept
    {
        for (const PreparedTransferBlock& block : bundle.blocks) {
            if (block.kind == TransferBlockKind::Exif) {
                return &block;
            }
        }
        return nullptr;
    }

    static MetaStore make_datetime_store()
    {
        const std::array entries = {
            authoring_entry(make_exif_tag_key_view("ifd0", 0x010FU),
                            make_value_view_text("Example",
                                                 TextEncoding::Ascii)),
            authoring_entry(make_exif_tag_key_view("ifd0", 0x0132U),
                            make_value_view_text("2026:09:01 12:34:56",
                                                 TextEncoding::Ascii)),
        };
        MetaStore store;
        const MetadataAuthoringResult result = create_metadata_store(entries,
                                                                     &store);
        EXPECT_EQ(result.status, MetadataAuthoringStatus::Ok);
        return store;
    }

    TEST(MetadataAuthoring, BuildsValidatedMixedStoreAndCopiesBorrowedData)
    {
        std::string make                             = "Example Camera";
        std::string gain                             = "1.25";
        const std::array<uint16_t, 2> cfa_repeat     = { 2U, 2U };
        const std::array<uint8_t, 4> cfa_pattern     = { 0U, 1U, 1U, 2U };
        const std::array<SRational, 2> private_curve = {
            SRational { -1, 2 },
            SRational { 3, 4 },
        };
        const std::array<std::byte, 4> dng_version = {
            std::byte { 1U },
            std::byte { 6U },
            std::byte { 0U },
            std::byte { 0U },
        };

        const std::array entries = {
            authoring_entry(make_exif_tag_key_view("ifd0", 0x0100U),
                            make_value_view_u32(4000U), tiff_wire(4U), 1U),
            authoring_entry(make_exif_tag_key_view("ifd0", 0x0101U),
                            make_value_view_u32(3000U), tiff_wire(4U), 1U),
            authoring_entry(make_exif_tag_key_view("ifd0", 0x010FU),
                            make_value_view_text(make, TextEncoding::Ascii),
                            tiff_wire(2U),
                            static_cast<uint32_t>(make.size() + 1U)),
            authoring_entry(make_exif_tag_key_view("exififd", 0x829AU),
                            make_value_view_urational(1U, 125U), tiff_wire(5U),
                            1U),
            authoring_entry(make_exif_tag_key_view("ifd0", 0x828DU),
                            make_value_view_array(MetaElementType::U16,
                                                  std::as_bytes(
                                                      std::span<const uint16_t>(
                                                          cfa_repeat)),
                                                  2U),
                            tiff_wire(3U), 2U),
            authoring_entry(make_exif_tag_key_view("ifd0", 0x828EU),
                            make_value_view_array(MetaElementType::U8,
                                                  std::as_bytes(
                                                      std::span<const uint8_t>(
                                                          cfa_pattern)),
                                                  4U),
                            tiff_wire(1U), 4U),
            authoring_entry(make_exif_tag_key_view("ifd0", 0xC612U),
                            make_value_view_bytes(dng_version), tiff_wire(1U),
                            4U),
            authoring_entry(make_exif_tag_key_view("ifd0", 0xF001U),
                            make_value_view_u16(7U)),
            authoring_entry(make_xmp_property_key_view(
                                "urn:example:capture:1.0/", "Gain[1]"),
                            make_value_view_text(gain, TextEncoding::Utf8)),
            authoring_entry(make_iptc_dataset_key_view(2U, 5U),
                            make_value_view_text("Frame", TextEncoding::Utf8)),
            authoring_entry(
                make_exif_tag_key_view("ifd0", 0xF002U),
                make_value_view_array(MetaElementType::SRational,
                                      std::as_bytes(std::span<const SRational>(
                                          private_curve)),
                                      2U),
                tiff_wire(10U), 2U),
        };

        MetadataAuthoringOptions options;
        options.validation.context.has_dimensions = true;
        options.validation.context.width          = 4000U;
        options.validation.context.height         = 3000U;
        MetaStore store;
        const MetadataAuthoringResult result
            = create_metadata_store(entries, &store, options);

        ASSERT_EQ(result.status, MetadataAuthoringStatus::Ok);
        EXPECT_EQ(result.entries_created, entries.size());
        EXPECT_TRUE(store.is_finalized());
        EXPECT_EQ(store.block_count(), 1U);
        ASSERT_EQ(store.entries().size(), entries.size());
        make[0] = 'X';
        gain[0] = '9';
        EXPECT_EQ(arena_text(store, store.entry(2U).value), "Example Camera");
        EXPECT_EQ(arena_text(store, store.entry(8U).value), "1.25");

        XmpPortableOptions xmp_options;
        xmp_options.include_exif         = false;
        xmp_options.include_iptc         = false;
        xmp_options.include_existing_xmp = true;
        xmp_options.existing_namespace_policy
            = XmpExistingNamespacePolicy::PreserveCustom;
        std::vector<std::byte> xmp(4096U);
        const XmpDumpResult xmp_result = dump_xmp_portable(store, xmp,
                                                           xmp_options);
        ASSERT_EQ(xmp_result.status, XmpDumpStatus::Ok);
        const std::string_view packet(reinterpret_cast<const char*>(xmp.data()),
                                      static_cast<size_t>(xmp_result.written));
        EXPECT_NE(packet.find("xmlns:omns1=\"urn:example:capture:1.0/\""),
                  std::string_view::npos);
        EXPECT_NE(packet.find("<omns1:Gain>"), std::string_view::npos);
        EXPECT_NE(packet.find("<rdf:Seq>"), std::string_view::npos);
        EXPECT_NE(packet.find("<rdf:li>1.25</rdf:li>"), std::string_view::npos);

        const ExifTiffSerializeResult measured = serialize_exif_tiff(store, {});
        ASSERT_EQ(measured.status, ExifTiffSerializeStatus::OutputTruncated);
        std::vector<std::byte> tiff(static_cast<size_t>(measured.needed));
        ASSERT_EQ(serialize_exif_tiff(store, tiff).status,
                  ExifTiffSerializeStatus::Ok);
        MetaStore decoded;
        ASSERT_EQ(decode_exif_tiff(tiff, decoded, {}, {}).status,
                  ExifDecodeStatus::Ok);
        decoded.finalize();
        const std::span<const EntryId> version = decoded.find_all(
            make_exif_tag_key_view("ifd0", 0xC612U));
        ASSERT_EQ(version.size(), 1U);
        EXPECT_EQ(decoded.entry(version.front()).origin.wire_type.family,
                  WireFamily::Tiff);
        EXPECT_EQ(decoded.entry(version.front()).origin.wire_type.code, 1U);
        const std::span<const EntryId> curve = decoded.find_all(
            make_exif_tag_key_view("ifd0", 0xF002U));
        ASSERT_EQ(curve.size(), 1U);
        const MetaValue& curve_value = decoded.entry(curve.front()).value;
        ASSERT_EQ(curve_value.kind, MetaValueKind::Array);
        ASSERT_EQ(curve_value.elem_type, MetaElementType::SRational);
        ASSERT_EQ(curve_value.count, 2U);
        const std::span<const std::byte> curve_bytes = decoded.arena().span(
            curve_value.data.span);
        ASSERT_EQ(curve_bytes.size(), sizeof(private_curve));
        std::array<SRational, 2> round_trip_curve {};
        std::copy(curve_bytes.begin(), curve_bytes.end(),
                  reinterpret_cast<std::byte*>(round_trip_curve.data()));
        EXPECT_EQ(round_trip_curve[0].numer, -1);
        EXPECT_EQ(round_trip_curve[0].denom, 2);
        EXPECT_EQ(round_trip_curve[1].numer, 3);
        EXPECT_EQ(round_trip_curve[1].denom, 4);
    }

    TEST(MetadataAuthoring, InvalidRequestDoesNotReplaceOutput)
    {
        MetaStore output              = make_datetime_store();
        const size_t original_entries = output.entries().size();
        const std::array invalid      = {
            authoring_entry(make_exif_tag_key_view("exififd", 0x829AU),
                                 make_value_view_urational(1U, 0U)),
        };
        const MetadataAuthoringResult result = create_metadata_store(invalid,
                                                                     &output);
        EXPECT_EQ(result.status, MetadataAuthoringStatus::ValidationFailed);
        EXPECT_EQ(result.failed_entry, 0U);
        EXPECT_EQ(result.validation_issue,
                  MetadataValidationIssueCode::RationalDenominatorZero);
        EXPECT_EQ(output.entries().size(), original_entries);
        EXPECT_EQ(arena_text(output, output.entry(1U).value),
                  "2026:09:01 12:34:56");

        MetadataAuthoringOptions invalid_options;
        invalid_options.validation.unknown_exif_tags
            = static_cast<MetadataUnknownTagPolicy>(0xFFU);
        EXPECT_EQ(
            create_metadata_store(invalid, &output, invalid_options).status,
            MetadataAuthoringStatus::InvalidOptions);
        EXPECT_EQ(output.entries().size(), original_entries);
    }

    TEST(MetadataValidation, ReportsSchemaDuplicateAndContextFailures)
    {
        const std::array entries = {
            authoring_entry(make_exif_tag_key_view("exififd", 0x010FU),
                            make_value_view_text("Wrong IFD",
                                                 TextEncoding::Ascii)),
            authoring_entry(make_exif_tag_key_view("ifd0", 0x0100U),
                            make_value_view_u32(4000U)),
            authoring_entry(make_exif_tag_key_view("ifd0", 0x0100U),
                            make_value_view_u32(4000U)),
            authoring_entry(make_exif_tag_key_view("exififd", 0x829AU),
                            make_value_view_urational(1U, 0U)),
            authoring_entry(make_xmp_property_key_view("urn:example:test:1.0/",
                                                       "Bad[0]"),
                            make_value_view_text("x", TextEncoding::Utf8)),
        };
        MetadataAuthoringOptions authoring_options;
        authoring_options.validate = false;
        MetaStore store;
        ASSERT_EQ(
            create_metadata_store(entries, &store, authoring_options).status,
            MetadataAuthoringStatus::Ok);

        MetadataValidationOptions validation;
        validation.context.has_dimensions     = true;
        validation.context.width              = 3999U;
        validation.context.height             = 3000U;
        const MetadataValidationResult result = validate_store(store,
                                                               validation);
        EXPECT_EQ(result.status, MetadataValidationStatus::InvalidMetadata);
        EXPECT_TRUE(has_issue(result, MetadataValidationIssueCode::WrongIfd));
        EXPECT_TRUE(
            has_issue(result, MetadataValidationIssueCode::DuplicateSingleton));
        EXPECT_TRUE(
            has_issue(result,
                      MetadataValidationIssueCode::RationalDenominatorZero));
        EXPECT_TRUE(
            has_issue(result,
                      MetadataValidationIssueCode::InvalidXmpPropertyPath));
        EXPECT_TRUE(
            has_issue(result,
                      MetadataValidationIssueCode::ImageContextMismatch));
    }

    TEST(MetadataValidation, ReportsGenericKeyOriginAndStoreLimits)
    {
        MetaStore store;
        ASSERT_NE(store.add_block(BlockInfo {}), kInvalidBlockId);

        Entry malformed_key;
        malformed_key.key.kind                  = MetaKeyKind::BmffField;
        malformed_key.key.data.bmff_field.field = ByteSpan { 100U, 4U };
        malformed_key.value                     = make_u8(1U);
        malformed_key.origin.block              = 0U;
        ASSERT_NE(store.add_entry(malformed_key), kInvalidEntryId);

        Entry malformed_origin;
        malformed_origin.key          = make_comment_key();
        malformed_origin.value        = make_u8(2U);
        malformed_origin.origin.block = 9U;
        ASSERT_NE(store.add_entry(malformed_origin), kInvalidEntryId);
        store.finalize();

        const MetadataValidationResult structural = validate_store(store);
        EXPECT_EQ(structural.status, MetadataValidationStatus::InvalidMetadata);
        EXPECT_TRUE(
            has_issue(structural, MetadataValidationIssueCode::InvalidKey));
        EXPECT_TRUE(
            has_issue(structural, MetadataValidationIssueCode::InvalidOrigin));

        MetadataValidationOptions limited;
        limited.max_entries                     = 1U;
        const MetadataValidationResult resource = validate_store(store,
                                                                 limited);
        EXPECT_EQ(resource.status, MetadataValidationStatus::LimitExceeded);
        EXPECT_TRUE(has_issue(resource,
                              MetadataValidationIssueCode::StoreLimitExceeded));

        MetadataValidationOptions invalid;
        invalid.unknown_exif_tags = static_cast<MetadataUnknownTagPolicy>(
            0xFFU);
        EXPECT_EQ(validate_store(store, invalid).status,
                  MetadataValidationStatus::InvalidArgument);

        MetaStore reserve_limited;
        reserve_limited.constrain_resources(1U, 8U);
        reserve_limited.reserve(2U, 2U, 16U);
        EXPECT_TRUE(reserve_limited.resource_limit_exceeded());
    }

    TEST(MetadataValidation, AcceptsImageTagsInIndexedIfdsAndHonorsEntryOptions)
    {
        const std::array entries = {
            authoring_entry(make_exif_tag_key_view("subifd0", 0x0100U),
                            make_value_view_u32(4096U), tiff_wire(4U), 1U),
            authoring_entry(make_exif_tag_key_view("ifd1", 0x0101U),
                            make_value_view_u32(256U), tiff_wire(4U), 1U),
        };
        MetaStore store;
        ASSERT_EQ(create_metadata_store(entries, &store).status,
                  MetadataAuthoringStatus::Ok);
        EXPECT_TRUE(validate_store(store).ok());

        MetaStore building;
        const BlockId block = building.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        Entry entry;
        entry.key          = make_comment_key();
        entry.value        = make_u8(1U);
        entry.origin.block = block;
        const EntryId id   = building.add_entry(entry);
        ASSERT_NE(id, kInvalidEntryId);

        MetadataValidationOptions options;
        options.require_finalized             = true;
        const MetadataValidationResult result = validate_entry(building, id,
                                                               options);
        EXPECT_EQ(result.status, MetadataValidationStatus::InvalidMetadata);
        EXPECT_TRUE(
            has_issue(result, MetadataValidationIssueCode::StoreNotFinalized));
    }

    TEST(ExifTiffSerialize, MeasuresWritesDeterministicallyAndRoundTrips)
    {
        MetaStore store                        = make_datetime_store();
        const ExifTiffSerializeResult measured = serialize_exif_tiff(store, {});
        ASSERT_EQ(measured.status, ExifTiffSerializeStatus::OutputTruncated);
        ASSERT_GT(measured.needed, 8U);
        EXPECT_EQ(measured.written, 0U);

        std::vector<std::byte> first(static_cast<size_t>(measured.needed));
        std::vector<std::byte> second(first.size());
        const ExifTiffSerializeResult first_result = serialize_exif_tiff(store,
                                                                         first);
        const ExifTiffSerializeResult second_result
            = serialize_exif_tiff(store, second);
        ASSERT_EQ(first_result.status, ExifTiffSerializeStatus::Ok);
        ASSERT_EQ(second_result.status, ExifTiffSerializeStatus::Ok);
        EXPECT_EQ(first_result.written, measured.needed);
        EXPECT_EQ(first, second);
        std::array<std::byte, 4> short_output {};
        const ExifTiffSerializeResult short_result
            = serialize_exif_tiff(store, short_output);
        EXPECT_EQ(short_result.status,
                  ExifTiffSerializeStatus::OutputTruncated);
        EXPECT_EQ(short_result.written, short_output.size());
        EXPECT_EQ(short_result.needed, measured.needed);
        EXPECT_TRUE(std::equal(short_output.begin(), short_output.end(),
                               first.begin()));
        ASSERT_GE(first.size(), 8U);
        EXPECT_EQ(first[0], std::byte { 'I' });
        EXPECT_EQ(first[1], std::byte { 'I' });
        EXPECT_EQ(first[2], std::byte { 42U });
        EXPECT_NE(first[0], std::byte { 'E' });

        MetaStore decoded;
        const ExifDecodeResult decoded_result = decode_exif_tiff(first, decoded,
                                                                 {}, {});
        ASSERT_EQ(decoded_result.status, ExifDecodeStatus::Ok);
        decoded.finalize();
        const std::span<const EntryId> make = decoded.find_all(
            make_exif_tag_key_view("ifd0", 0x010FU));
        ASSERT_EQ(make.size(), 1U);
        EXPECT_EQ(arena_text(decoded, decoded.entry(make.front()).value),
                  "Example");

        ExifTiffSerializeOptions invalid;
        invalid.makernote_policy = static_cast<ExifTiffMakerNotePolicy>(0xFFU);
        EXPECT_EQ(serialize_exif_tiff(store, {}, invalid).status,
                  ExifTiffSerializeStatus::InvalidOptions);
    }

    TEST(ExifTiffSerialize, AppliesOpaqueMakerNotePolicyAndOutputLimit)
    {
        const std::array<std::byte, 6> maker_note = {
            std::byte { 'V' }, std::byte { 'E' }, std::byte { 'N' },
            std::byte { 'D' }, std::byte { 'O' }, std::byte { 'R' },
        };
        const std::array entries = {
            authoring_entry(make_exif_tag_key_view("ifd0", 0x010FU),
                            make_value_view_text("Example",
                                                 TextEncoding::Ascii)),
            authoring_entry(make_exif_tag_key_view("exififd", 0x927CU),
                            make_value_view_bytes(maker_note), tiff_wire(7U),
                            static_cast<uint32_t>(maker_note.size())),
        };
        MetaStore store;
        ASSERT_EQ(create_metadata_store(entries, &store).status,
                  MetadataAuthoringStatus::Ok);

        const ExifTiffSerializeResult dropped = serialize_exif_tiff(store, {});
        ASSERT_EQ(dropped.status, ExifTiffSerializeStatus::OutputTruncated);
        std::vector<std::byte> dropped_bytes(
            static_cast<size_t>(dropped.needed));
        ASSERT_EQ(serialize_exif_tiff(store, dropped_bytes).status,
                  ExifTiffSerializeStatus::Ok);
        MetaStore dropped_store;
        ASSERT_EQ(decode_exif_tiff(dropped_bytes, dropped_store, {}, {}).status,
                  ExifDecodeStatus::Ok);
        dropped_store.finalize();
        EXPECT_TRUE(
            dropped_store.find_all(make_exif_tag_key_view("exififd", 0x927CU))
                .empty());

        ExifTiffSerializeOptions preserve;
        preserve.makernote_policy = ExifTiffMakerNotePolicy::PreserveOpaque;
        const ExifTiffSerializeResult measured = serialize_exif_tiff(store, {},
                                                                     preserve);
        ASSERT_EQ(measured.status, ExifTiffSerializeStatus::OutputTruncated);
        EXPECT_GT(measured.needed, dropped.needed);
        std::vector<std::byte> preserved(static_cast<size_t>(measured.needed));
        ASSERT_EQ(serialize_exif_tiff(store, preserved, preserve).status,
                  ExifTiffSerializeStatus::Ok);
        MetaStore preserved_store;
        ASSERT_EQ(decode_exif_tiff(preserved, preserved_store, {}, {}).status,
                  ExifDecodeStatus::Ok);
        preserved_store.finalize();
        EXPECT_EQ(preserved_store
                      .find_all(make_exif_tag_key_view("exififd", 0x927CU))
                      .size(),
                  1U);

        preserve.max_output_bytes = measured.needed - 1U;
        EXPECT_EQ(serialize_exif_tiff(store, {}, preserve).status,
                  ExifTiffSerializeStatus::LimitExceeded);
    }

    TEST(ExifTiffSerialize, TransferWrappersUseCanonicalPayloadAndPatchOffsets)
    {
        const MetaStore store                  = make_datetime_store();
        const ExifTiffSerializeResult measured = serialize_exif_tiff(store, {});
        ASSERT_EQ(measured.status, ExifTiffSerializeStatus::OutputTruncated);
        std::vector<std::byte> canonical(static_cast<size_t>(measured.needed));
        ASSERT_EQ(serialize_exif_tiff(store, canonical).status,
                  ExifTiffSerializeStatus::Ok);

        struct TargetCase final {
            TransferTargetFormat target = TransferTargetFormat::Jpeg;
            size_t prefix               = 0U;
            bool inject_dng_version     = false;
        };
        static constexpr std::array targets = {
            TargetCase { TransferTargetFormat::Jpeg, 6U, false },
            TargetCase { TransferTargetFormat::Tiff, 6U, false },
            TargetCase { TransferTargetFormat::Dng, 6U, true },
            TargetCase { TransferTargetFormat::Png, 0U, false },
            TargetCase { TransferTargetFormat::Webp, 0U, false },
            TargetCase { TransferTargetFormat::Jp2, 10U, false },
            TargetCase { TransferTargetFormat::Jxl, 10U, false },
            TargetCase { TransferTargetFormat::Heif, 10U, false },
            TargetCase { TransferTargetFormat::Avif, 10U, false },
            TargetCase { TransferTargetFormat::Cr3, 10U, false },
        };
        for (const TargetCase& target_case : targets) {
            std::vector<std::byte> expected = canonical;
            if (target_case.inject_dng_version) {
                ExifTiffSerializeOptions canonical_options;
                canonical_options.include_subifds            = true;
                canonical_options.inject_minimal_dng_version = true;
                const ExifTiffSerializeResult dng_measured
                    = serialize_exif_tiff(store, {}, canonical_options);
                ASSERT_EQ(dng_measured.status,
                          ExifTiffSerializeStatus::OutputTruncated);
                expected.resize(static_cast<size_t>(dng_measured.needed));
                ASSERT_EQ(serialize_exif_tiff(store, expected, canonical_options)
                              .status,
                          ExifTiffSerializeStatus::Ok);
            }
            PrepareTransferRequest request;
            request.target_format      = target_case.target;
            request.include_xmp_app1   = false;
            request.include_icc_app2   = false;
            request.include_iptc_app13 = false;
            PreparedTransferBundle bundle;
            const PrepareTransferResult prepared
                = prepare_metadata_for_target(store, request, &bundle);
            ASSERT_EQ(prepared.status, TransferStatus::Ok);
            const PreparedTransferBlock* block = exif_block(bundle);
            ASSERT_NE(block, nullptr);

            const size_t prefix = target_case.prefix;
            ASSERT_EQ(block->payload.size(), expected.size() + prefix);
            EXPECT_TRUE(std::equal(expected.begin(), expected.end(),
                                   block->payload.begin()
                                       + static_cast<ptrdiff_t>(prefix)));
            if (prefix >= 6U) {
                EXPECT_EQ(block->payload[prefix - 6U], std::byte { 'E' });
                EXPECT_EQ(block->payload[prefix - 5U], std::byte { 'x' });
                EXPECT_EQ(block->payload[prefix - 4U], std::byte { 'i' });
                EXPECT_EQ(block->payload[prefix - 3U], std::byte { 'f' });
            }
            ASSERT_EQ(bundle.time_patch_map.size(), 1U);
            const TimePatchSlot& slot = bundle.time_patch_map.front();
            EXPECT_EQ(slot.byte_offset,
                      static_cast<uint32_t>(prefix)
                          + static_cast<uint32_t>(
                              std::search(expected.begin(), expected.end(),
                                          reinterpret_cast<const std::byte*>(
                                              "2026:09:01 12:34:56"),
                                          reinterpret_cast<const std::byte*>(
                                              "2026:09:01 12:34:56")
                                              + 19U)
                              - expected.begin()));
        }
    }

}  // namespace
}  // namespace openmeta
