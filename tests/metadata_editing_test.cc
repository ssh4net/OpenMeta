// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_creation.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
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

    static MetaStore create_base(std::span<const MetadataCreationField> fields)
    {
        MetadataCreationRequest request;
        request.fields = fields;
        MetaStore store;
        const MetadataCreationResult result = create_metadata(request, &store);
        EXPECT_EQ(result.status, MetadataCreationStatus::Ok);
        return store;
    }

    static std::string dump_portable_xmp(const MetaStore& store)
    {
        XmpPortableOptions options;
        options.include_exif         = false;
        options.include_iptc         = false;
        options.include_existing_xmp = true;

        std::vector<std::byte> out(16384U);
        const XmpDumpResult result = dump_xmp_portable(store, out, options);
        EXPECT_EQ(result.status, XmpDumpStatus::Ok);
        return std::string(reinterpret_cast<const char*>(out.data()),
                           result.written);
    }

    TEST(MetadataEditing, AppliesSequentialLogicalOperations)
    {
        const std::array fields = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                        "Original"),
            make_metadata_creation_text(MetadataCreationFieldKind::Creator,
                                        "Alice"),
            make_metadata_creation_text(MetadataCreationFieldKind::Creator,
                                        "Bob"),
            make_metadata_creation_text(MetadataCreationFieldKind::Keyword,
                                        "night"),
        };
        const MetaStore base = create_base(fields);

        const std::array operations = {
            make_metadata_edit_set(
                make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                            "Edited")),
            make_metadata_edit_set(
                make_metadata_creation_text(MetadataCreationFieldKind::Creator,
                                            "Carol"),
                1U),
            make_metadata_edit_add(
                make_metadata_creation_text(MetadataCreationFieldKind::Keyword,
                                            "city")),
            make_metadata_edit_remove(MetadataCreationFieldKind::Creator, 0U),
        };
        MetadataEditingRequest request;
        request.operations = operations;
        MetaStore edited;
        const MetadataEditingResult result = edit_metadata(base, request,
                                                           &edited);

        ASSERT_EQ(result.status, MetadataEditingStatus::Ok);
        EXPECT_EQ(result.operations_applied, operations.size());
        EXPECT_EQ(result.entries_added, 1U);
        EXPECT_EQ(result.entries_updated, 2U);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_TRUE(edited.is_finalized());
        ASSERT_EQ(edited.entries().size(), 5U);
        EXPECT_TRUE(any(edited.entry(1U).flags, EntryFlags::Deleted));
        EXPECT_TRUE(any(edited.entry(1U).flags, EntryFlags::Dirty));
        EXPECT_EQ(arena_string(edited.arena(), edited.entry(2U).value.data.span),
                  "Carol");
        EXPECT_EQ(
            arena_string(edited.arena(),
                         edited.entry(4U).key.data.xmp_property.property_path),
            "subject[2]");

        const std::string xmp = dump_portable_xmp(edited);
        EXPECT_NE(xmp.find("Edited"), std::string::npos);
        EXPECT_NE(xmp.find("Carol"), std::string::npos);
        EXPECT_NE(xmp.find("city"), std::string::npos);
        EXPECT_EQ(xmp.find("Alice"), std::string::npos);
    }

    TEST(MetadataEditing, SetPreservesOriginAndWireProvenance)
    {
        MetaStore base;
        const BlockId block = base.add_block(BlockInfo { 7U, 8U, 9U });
        ASSERT_NE(block, kInvalidBlockId);

        Entry title;
        title.key   = make_xmp_property_key(base.arena(),
                                            "http://purl.org/dc/elements/1.1/",
                                            "title[@xml:lang=x-default]");
        title.value = make_text(base.arena(), "Before", TextEncoding::Utf8);
        title.origin.block          = block;
        title.origin.order_in_block = 17U;
        title.origin.wire_type      = WireType { WireFamily::Other, 23U };
        title.origin.wire_count     = 4U;
        title.origin.wire_type_name = base.arena().append_string("utf8");
        title.flags                 = EntryFlags::Derived;
        ASSERT_NE(base.add_entry(title), kInvalidEntryId);
        base.finalize();

        const std::array operations = {
            make_metadata_edit_set(
                make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                            "After")),
        };
        MetadataEditingRequest request;
        request.operations = operations;
        MetaStore edited;
        ASSERT_EQ(edit_metadata(base, request, &edited).status,
                  MetadataEditingStatus::Ok);

        const Entry& result = edited.entry(0U);
        EXPECT_EQ(result.origin.block, block);
        EXPECT_EQ(result.origin.order_in_block, 17U);
        EXPECT_EQ(result.origin.wire_type.family, WireFamily::Other);
        EXPECT_EQ(result.origin.wire_type.code, 23U);
        EXPECT_EQ(result.origin.wire_count, 4U);
        EXPECT_EQ(arena_string(edited.arena(), result.origin.wire_type_name),
                  "utf8");
        EXPECT_TRUE(any(result.flags, EntryFlags::Derived));
        EXPECT_TRUE(any(result.flags, EntryFlags::Dirty));
        EXPECT_EQ(arena_string(edited.arena(), result.value.data.span),
                  "After");
    }

    TEST(MetadataEditing, RejectsConflictWithoutChangingOutput)
    {
        const std::array duplicate_titles = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                        "sentinel"),
        };
        MetaStore output = create_base(duplicate_titles);

        MetaStore base;
        const BlockId block = base.add_block(BlockInfo {});
        for (uint32_t i = 0U; i < 2U; ++i) {
            Entry title;
            title.key   = make_xmp_property_key(base.arena(),
                                                "http://purl.org/dc/elements/1.1/",
                                                "title[@xml:lang=x-default]");
            title.value = make_text(base.arena(), i == 0U ? "one" : "two",
                                    TextEncoding::Utf8);
            title.origin.block          = block;
            title.origin.order_in_block = i;
            ASSERT_NE(base.add_entry(title), kInvalidEntryId);
        }
        base.finalize();

        const std::array operations = {
            make_metadata_edit_set(
                make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                            "replacement")),
        };
        MetadataEditingRequest request;
        request.operations                 = operations;
        const MetadataEditingResult result = edit_metadata(base, request,
                                                           &output);

        EXPECT_EQ(result.status, MetadataEditingStatus::AmbiguousTarget);
        EXPECT_EQ(result.failed_operation_index, 0U);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(arena_string(output.arena(), output.entry(0U).value.data.span),
                  "sentinel");
    }

    TEST(MetadataEditing, RemoveAllThenAddRepairsSingletonConflict)
    {
        MetaStore base;
        const BlockId block = base.add_block(BlockInfo {});
        for (uint32_t i = 0U; i < 2U; ++i) {
            Entry title;
            title.key   = make_xmp_property_key(base.arena(),
                                                "http://purl.org/dc/elements/1.1/",
                                                "title[@xml:lang=x-default]");
            title.value = make_text(base.arena(), i == 0U ? "one" : "two",
                                    TextEncoding::Utf8);
            title.origin.block = block;
            ASSERT_NE(base.add_entry(title), kInvalidEntryId);
        }
        base.finalize();

        const std::array operations = {
            make_metadata_edit_remove_all(MetadataCreationFieldKind::Title),
            make_metadata_edit_add(
                make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                            "clean")),
        };
        MetadataEditingRequest request;
        request.operations = operations;
        MetaStore edited;
        const MetadataEditingResult result = edit_metadata(base, request,
                                                           &edited);
        ASSERT_EQ(result.status, MetadataEditingStatus::Ok);
        EXPECT_EQ(result.entries_added, 1U);
        EXPECT_EQ(result.entries_removed, 2U);
        EXPECT_TRUE(any(edited.entry(0U).flags, EntryFlags::Deleted));
        EXPECT_TRUE(any(edited.entry(1U).flags, EntryFlags::Deleted));
        EXPECT_EQ(dump_portable_xmp(edited).find("clean") != std::string::npos,
                  true);
    }

    TEST(MetadataEditing, AddsToFinalizedEmptyStore)
    {
        MetaStore base;
        base.finalize();
        const std::array operations = {
            make_metadata_edit_add(
                make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                            "Fresh")),
        };
        MetadataEditingRequest request;
        request.operations = operations;
        MetaStore edited;
        const MetadataEditingResult result = edit_metadata(base, request,
                                                           &edited);
        ASSERT_EQ(result.status, MetadataEditingStatus::Ok);
        ASSERT_EQ(edited.entries().size(), 1U);
        EXPECT_EQ(edited.entry(0U).origin.block, kInvalidBlockId);
        EXPECT_NE(dump_portable_xmp(edited).find("Fresh"), std::string::npos);
    }

    TEST(MetadataEditing, ValidatesRequestsTransactionally)
    {
        const std::array fields = {
            make_metadata_creation_u32(MetadataCreationFieldKind::Orientation,
                                       1U),
        };
        const MetaStore base = create_base(fields);
        MetaStore output     = create_base(fields);

        const std::array invalid_values = {
            make_metadata_edit_set(make_metadata_creation_u32(
                MetadataCreationFieldKind::Orientation, 9U)),
        };
        MetadataEditingRequest invalid_request;
        invalid_request.operations = invalid_values;
        EXPECT_EQ(edit_metadata(base, invalid_request, &output).status,
                  MetadataEditingStatus::InvalidValue);
        EXPECT_EQ(output.entry(0U).value.data.u64, 1U);

        const std::array missing = {
            make_metadata_edit_remove(MetadataCreationFieldKind::Title),
        };
        MetadataEditingRequest missing_request;
        missing_request.operations = missing;
        EXPECT_EQ(edit_metadata(base, missing_request, &output).status,
                  MetadataEditingStatus::TargetNotFound);

        MetaStore unfinalized;
        EXPECT_EQ(edit_metadata(unfinalized, invalid_request, &output).status,
                  MetadataEditingStatus::BaseNotFinalized);

        MetadataEditingRequest empty_request;
        MetaStore copy;
        const MetadataEditingResult empty_result
            = edit_metadata(base, empty_request, &copy);
        EXPECT_EQ(empty_result.status, MetadataEditingStatus::Ok);
        EXPECT_TRUE(copy.is_finalized());
        EXPECT_EQ(copy.entries().size(), base.entries().size());
    }

    TEST(MetadataEditing, EnforcesLimitsAndExposesStableNames)
    {
        const std::array fields = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                        "Before"),
        };
        const MetaStore base        = create_base(fields);
        const std::array operations = {
            make_metadata_edit_set(
                make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                            "After")),
        };
        MetadataEditingRequest request;
        request.operations = operations;
        MetaStore output;

        request.limits.max_operations = 0U;
        EXPECT_EQ(edit_metadata(base, request, &output).status,
                  MetadataEditingStatus::InvalidLimits);

        request.limits.max_operations = kMetadataEditingMaxOperations;
        request.limits.max_text_bytes_per_operation = 4U;
        EXPECT_EQ(edit_metadata(base, request, &output).status,
                  MetadataEditingStatus::TextTooLong);

        request.limits.max_text_bytes_per_operation
            = kMetadataEditingMaxTextBytesPerOperation;
        request.limits.max_total_text_bytes = 4U;
        EXPECT_EQ(edit_metadata(base, request, &output).status,
                  MetadataEditingStatus::TotalTextTooLong);

        MetadataEditingOperation invalid_kind = operations[0];
        invalid_kind.kind = static_cast<MetadataEditingOperationKind>(255U);
        request.operations
            = std::span<const MetadataEditingOperation>(&invalid_kind, 1U);
        request.limits.max_total_text_bytes = kMetadataEditingMaxTotalTextBytes;
        EXPECT_EQ(edit_metadata(base, request, &output).status,
                  MetadataEditingStatus::InvalidOperationKind);

        EXPECT_STREQ(metadata_editing_operation_kind_name(
                         MetadataEditingOperationKind::Remove),
                     "remove");
        EXPECT_STREQ(metadata_editing_status_name(
                         MetadataEditingStatus::SingletonAlreadyExists),
                     "singleton_already_exists");
    }

    TEST(MetadataEditing, EditedValuesReachPreparedTransfer)
    {
        const std::array fields = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                        "Before"),
            make_metadata_creation_i32(MetadataCreationFieldKind::Rating, 1),
        };
        const MetaStore base        = create_base(fields);
        const std::array operations = {
            make_metadata_edit_set(
                make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                            "After")),
            make_metadata_edit_set(
                make_metadata_creation_i32(MetadataCreationFieldKind::Rating,
                                           5)),
        };
        MetadataEditingRequest edit_request;
        edit_request.operations = operations;
        MetaStore edited;
        ASSERT_EQ(edit_metadata(base, edit_request, &edited).status,
                  MetadataEditingStatus::Ok);

        PrepareTransferRequest transfer_request;
        transfer_request.target_format      = TransferTargetFormat::Jpeg;
        transfer_request.include_exif_app1  = false;
        transfer_request.include_icc_app2   = false;
        transfer_request.include_iptc_app13 = false;
        transfer_request.xmp_project_exif   = false;
        transfer_request.xmp_project_iptc   = false;

        PreparedTransferBundle bundle;
        const PrepareTransferResult prepared
            = prepare_metadata_for_target(edited, transfer_request, &bundle);
        ASSERT_EQ(prepared.status, TransferStatus::Ok);
        ASSERT_EQ(bundle.blocks.size(), 1U);
        const std::string_view payload(reinterpret_cast<const char*>(
                                           bundle.blocks[0].payload.data()),
                                       bundle.blocks[0].payload.size());
        EXPECT_NE(payload.find("After"), std::string_view::npos);
        EXPECT_NE(payload.find("<xmp:Rating>5</xmp:Rating>"),
                  std::string_view::npos);
        EXPECT_EQ(payload.find("Before"), std::string_view::npos);
    }

}  // namespace
}  // namespace openmeta
