// SPDX-License-Identifier: Apache-2.0

#include "icc_authoring_test_fixture.h"

#include "openmeta/icc_decode.h"
#include "openmeta/icc_serialize.h"
#include "openmeta/metadata_editing.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

namespace openmeta {
namespace {

    static std::vector<std::byte> serialized_profile(const MetaStore& store)
    {
        const IccProfileSerializeResult size = serialize_icc_profile(store, {});
        EXPECT_EQ(size.status, IccProfileSerializeStatus::OutputTruncated);
        std::vector<std::byte> bytes(static_cast<size_t>(size.needed));
        EXPECT_TRUE(serialize_icc_profile(store, bytes).ok());
        return bytes;
    }

    TEST(MetadataIccAuthoring, OwnsInputsAndAppliesOneMixedEditTransaction)
    {
        EXPECT_EQ(kMetadataAuthoringContractVersion, 2U);
        EXPECT_EQ(kMetadataTypedEditingContractVersion, 2U);
        test::IccAuthoringFixtureData data;
        const std::array fields = test::icc_authoring_fixture_entries(data);
        std::vector<MetadataAuthoringEntry> entries(fields.begin(),
                                                    fields.end());
        entries.push_back(
            { make_exif_tag_key_view("ifd0", 0x010FU),
              make_value_view_text("Camera", TextEncoding::Ascii) });
        entries.push_back(
            { make_iptc_dataset_key_view(2U, 5U),
              make_value_view_text("Title", TextEncoding::Utf8) });
        entries.push_back(
            { make_xmp_property_key_view("urn:openmeta:icc:test", "Owner"),
              make_value_view_text("Author", TextEncoding::Utf8) });
        MetaStore source;
        ASSERT_TRUE(create_metadata_store(entries, &source).ok());
        const std::vector<std::byte> original = serialized_profile(source);
        data.date[0]                          = 1999U;
        data.first_tag[8]                     = std::byte { 0xFF };
        EXPECT_EQ(serialized_profile(source), original);

        const MetaKeyView first = make_icc_tag_key_view(0x7A7A7A31U);
        const EntryId first_id  = source.find_all(first).front();
        std::array<std::byte, 15U> replacement {};
        std::copy(data.first_tag.begin(), data.first_tag.end(),
                  replacement.begin());
        replacement[13]             = std::byte { 0x66 };
        replacement[14]             = std::byte { 0x77 };
        const std::array operations = {
            MetadataTypedEditingOperation {
                MetadataEditingOperationKind::Set,
                { make_icc_header_field_key_view(80U),
                  make_value_view_u32(0x4E455854U) } },
            MetadataTypedEditingOperation {
                MetadataEditingOperationKind::Set,
                { first, make_value_view_bytes(replacement) } },
            MetadataTypedEditingOperation {
                MetadataEditingOperationKind::Remove,
                { make_icc_tag_key_view(0x61616131U), {} } },
            MetadataTypedEditingOperation {
                MetadataEditingOperationKind::Add,
                { make_icc_tag_key_view(0x6E657731U),
                  make_value_view_bytes(data.second_tag) } },
            MetadataTypedEditingOperation {
                MetadataEditingOperationKind::Set,
                { make_xmp_property_key_view("urn:openmeta:icc:test", "Owner"),
                  make_value_view_text("Editor", TextEncoding::Utf8) } },
        };
        MetaStore edited;
        const MetadataTypedEditingResult changed
            = edit_metadata_typed(source, operations, &edited);
        ASSERT_TRUE(changed.ok())
            << metadata_typed_editing_status_name(changed.status);
        EXPECT_EQ(changed.entries_updated, 3U);
        EXPECT_EQ(changed.entries_removed, 1U);
        EXPECT_EQ(changed.entries_added, 1U);
        EXPECT_EQ(edited.find_all(first).front(), first_id);
        EXPECT_EQ(edited.entry(first_id).origin.order_in_block,
                  source.entry(first_id).origin.order_in_block);
        EXPECT_TRUE(
            edited.find_all(make_icc_tag_key_view(0x61616131U)).empty());
        EXPECT_EQ(edited.find_all(make_icc_tag_key_view(0x6E657731U)).size(),
                  1U);
        EXPECT_EQ(
            edited.find_all(make_exif_tag_key_view("ifd0", 0x010FU)).size(),
            1U);
        EXPECT_EQ(edited.find_all(make_iptc_dataset_key_view(2U, 5U)).size(),
                  1U);
        EXPECT_EQ(serialized_profile(source), original);

        const std::vector<std::byte> bytes = serialized_profile(edited);
        MetaStore decoded;
        ASSERT_EQ(decode_icc_profile(bytes, decoded).status,
                  IccDecodeStatus::Ok);
        decoded.finalize();
        const EntryId creator
            = decoded.find_all(make_icc_header_field_key_view(80U)).front();
        EXPECT_EQ(decoded.entry(creator).value.data.u64, 0x4E455854U);
        const EntryId tag                    = decoded.find_all(first).front();
        const std::span<const std::byte> raw = decoded.arena().span(
            decoded.entry(tag).value.data.span);
        EXPECT_TRUE(std::equal(raw.begin(), raw.end(), replacement.begin(),
                               replacement.end()));
        EXPECT_EQ(decoded.find_all(make_icc_tag_key_view(0x6E657731U)).size(),
                  1U);
        EXPECT_TRUE(
            decoded.find_all(make_icc_tag_key_view(0x61616131U)).empty());
    }

    TEST(MetadataIccAuthoring, RejectsMalformedEntriesWithoutPublishingAnyEdit)
    {
        const test::IccAuthoringFixtureData data;
        const std::array fields = test::icc_authoring_fixture_entries(data);
        MetaStore output;
        ASSERT_TRUE(create_metadata_store(fields, &output).ok());
        const std::vector<std::byte> before     = serialized_profile(output);
        std::array<std::byte, 13U> bad_reserved = data.first_tag;
        bad_reserved[7]                         = std::byte { 1 };
        const std::array<SRational, 3U> inexact
            = { SRational { 1, 3 }, SRational { 1, 1 }, SRational { 1, 1 } };
        MetaValueView bad_shape     = make_value_view_bytes(data.first_tag);
        bad_shape.elem_type         = MetaElementType::U16;
        MetaValueView bad_hint      = make_value_view_u32(0x4F4D5441U);
        bad_hint.text_encoding      = TextEncoding::Utf8;
        const std::array bad_values = {
            MetadataAuthoringEntry { make_icc_header_field_key_view(24U),
                                     make_value_view_u32(2026U) },
            MetadataAuthoringEntry { make_icc_header_field_key_view(80U),
                                     make_value_view_u64(7U) },
            MetadataAuthoringEntry {
                make_icc_header_field_key_view(68U),
                make_value_view_array(MetaElementType::SRational,
                                      std::as_bytes(std::span(inexact)), 3U) },
            MetadataAuthoringEntry {
                make_icc_tag_key_view(0x7A7A7A31U),
                make_value_view_text("text payload", TextEncoding::Utf8) },
            MetadataAuthoringEntry { make_icc_tag_key_view(0x7A7A7A31U),
                                     make_value_view_bytes(bad_reserved) },
            MetadataAuthoringEntry { make_icc_tag_key_view(0x7A7A7A31U),
                                     bad_shape },
            MetadataAuthoringEntry { make_icc_header_field_key_view(80U),
                                     bad_hint },
            MetadataAuthoringEntry { make_icc_header_field_key_view(84U),
                                     make_value_view_bytes(data.first_tag) },
        };
        for (const MetadataAuthoringEntry& bad : bad_values) {
            SCOPED_TRACE(static_cast<unsigned>(bad.key.kind));
            MetadataAuthoringOptions raw_options;
            raw_options.validate = false;
            const MetadataAuthoringResult created
                = create_metadata_store({ &bad, 1U }, &output, raw_options);
            EXPECT_EQ(created.status, MetadataAuthoringStatus::InvalidValue);
            EXPECT_EQ(created.failed_entry, 0U);
            EXPECT_EQ(serialized_profile(output), before);
            const std::array operations = {
                MetadataTypedEditingOperation {
                    MetadataEditingOperationKind::Add,
                    { make_exif_tag_key_view("ifd0", 0x0131U),
                      make_value_view_text("temporary", TextEncoding::Ascii) } },
                MetadataTypedEditingOperation {
                    MetadataEditingOperationKind::Set, bad },
            };
            const MetadataTypedEditingResult edited
                = edit_metadata_typed(output, operations, &output);
            EXPECT_EQ(edited.status, MetadataTypedEditingStatus::InvalidValue);
            EXPECT_EQ(edited.failed_operation_index, 1U);
            EXPECT_TRUE(output.find_all(make_exif_tag_key_view("ifd0", 0x0131U))
                            .empty());
            EXPECT_EQ(serialized_profile(output), before);
        }
        const std::array bad_keys = {
            make_icc_header_field_key_view(1U),
            make_icc_header_field_key_view(100U),
            make_icc_header_field_key_view(UINT32_MAX),
            make_icc_tag_key_view(0U),
        };
        for (const MetaKeyView& key : bad_keys) {
            const MetadataAuthoringEntry entry { key, make_value_view_u32(1U) };
            EXPECT_EQ(create_metadata_store({ &entry, 1U }, &output).status,
                      MetadataAuthoringStatus::InvalidKey);
            EXPECT_EQ(serialized_profile(output), before);
        }
    }

    TEST(MetadataIccAuthoring, PreservesGenericDuplicateAndOccurrencePolicies)
    {
        const test::IccAuthoringFixtureData data;
        const std::array fields = test::icc_authoring_fixture_entries(data);
        std::vector<MetadataAuthoringEntry> repeated(fields.begin(),
                                                     fields.end());
        repeated.push_back(fields.back());
        MetaStore duplicates;
        ASSERT_TRUE(create_metadata_store(repeated, &duplicates).ok());
        const MetaKeyView key = fields.back().key;
        EXPECT_EQ(duplicates.find_all(key).size(), 2U);
        std::array<std::byte, 512U> sentinel;
        sentinel.fill(std::byte { 0xA5 });
        EXPECT_EQ(serialize_icc_profile(duplicates, sentinel).status,
                  IccProfileSerializeStatus::InvalidMetadata);
        for (const std::byte byte : sentinel)
            EXPECT_EQ(byte, std::byte { 0xA5 });
        MetadataAuthoringOptions reject;
        reject.duplicate_policy
            = MetadataAuthoringDuplicatePolicy::RejectExactKeys;
        EXPECT_EQ(create_metadata_store(repeated, &duplicates, reject).status,
                  MetadataAuthoringStatus::DuplicateKey);
        EXPECT_EQ(duplicates.find_all(key).size(), 2U);
        const MetadataTypedEditingOperation ambiguous {
            MetadataEditingOperationKind::Set,
            { key, make_value_view_bytes(data.first_tag) }
        };
        EXPECT_EQ(edit_metadata_typed(duplicates, { &ambiguous, 1U },
                                      &duplicates)
                      .status,
                  MetadataTypedEditingStatus::AmbiguousTarget);
        const std::array repair = {
            MetadataTypedEditingOperation { MetadataEditingOperationKind::Remove,
                                            { key, {} },
                                            kMetadataEditingAllOccurrences },
            MetadataTypedEditingOperation {
                MetadataEditingOperationKind::Add,
                { key, make_value_view_bytes(data.second_tag) } },
        };
        ASSERT_TRUE(edit_metadata_typed(duplicates, repair, &duplicates).ok());
        EXPECT_EQ(duplicates.find_all(key).size(), 1U);
        EXPECT_FALSE(serialized_profile(duplicates).empty());
    }

    TEST(MetadataIccAuthoring,
         SeparatesEntryValidationFromProfileCompletenessAndLimits)
    {
        const test::IccAuthoringFixtureData data;
        const std::array fields = test::icc_authoring_fixture_entries(data);
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(fields, &store).ok());
        const std::vector<std::byte> before = serialized_profile(store);
        MetadataAuthoringOptions limited;
        limited.max_arena_bytes = 32U;
        limited.max_value_bytes = 32U;
        EXPECT_EQ(create_metadata_store(fields, &store, limited).status,
                  MetadataAuthoringStatus::LimitExceeded);
        EXPECT_EQ(serialized_profile(store), before);
        const MetadataTypedEditingOperation replacement {
            MetadataEditingOperationKind::Set, fields.back()
        };
        MetadataTypedEditingOptions edit_limits;
        edit_limits.max_request_bytes = 8U;
        EXPECT_EQ(edit_metadata_typed(store, { &replacement, 1U }, &store,
                                      edit_limits)
                      .status,
                  MetadataTypedEditingStatus::LimitExceeded);
        EXPECT_EQ(serialized_profile(store), before);

        const MetadataTypedEditingOperation remove_required {
            MetadataEditingOperationKind::Remove,
            { make_icc_header_field_key_view(8U), {} }
        };
        ASSERT_TRUE(
            edit_metadata_typed(store, { &remove_required, 1U }, &store).ok());
        std::array<std::byte, 512U> output;
        output.fill(std::byte { 0xBC });
        const IccProfileSerializeResult incomplete
            = serialize_icc_profile(store, output);
        EXPECT_EQ(incomplete.status,
                  IccProfileSerializeStatus::InvalidMetadata);
        EXPECT_EQ(incomplete.written, 0U);
        EXPECT_EQ(incomplete.needed, 0U);
        for (const std::byte byte : output)
            EXPECT_EQ(byte, std::byte { 0xBC });
    }

}  // namespace
}  // namespace openmeta
