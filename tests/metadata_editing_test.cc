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


    static MetadataTypedEditingOperation
    typed_operation(MetadataEditingOperationKind kind, const MetaKeyView& key,
                    const MetaValueView& value = {},
                    uint32_t occurrence = kMetadataTypedEditingUniqueOccurrence)
    {
        MetadataTypedEditingOperation operation;
        operation.kind        = kind;
        operation.entry.key   = key;
        operation.entry.value = value;
        operation.occurrence  = occurrence;
        return operation;
    }

    TEST(MetadataTypedEditing, OrdersMixedOperationsAndOwnsAliasedInputs)
    {
        const auto make          = make_exif_tag_key_view("ifd0", 0x010fU);
        const auto keyword       = make_iptc_dataset_key_view(2U, 25U);
        const auto custom        = make_xmp_property_key_view("urn:typed-edit",
                                                              "Gain[1]");
        const auto private_key   = make_exif_tag_key_view("exififd", 0xd123U);
        const std::array entries = {
            MetadataAuthoringEntry {
                make,
                make_value_view_text("A", TextEncoding::Ascii),
                { WireFamily::Tiff, 2U },
                2U },
            MetadataAuthoringEntry {
                keyword, make_value_view_text("first", TextEncoding::Ascii) },
            MetadataAuthoringEntry {
                keyword, make_value_view_text("second", TextEncoding::Ascii) },
            MetadataAuthoringEntry { private_key,
                                     make_value_view_u16(7U),
                                     { WireFamily::Tiff, 3U },
                                     1U },
        };
        MetaStore base;
        ASSERT_TRUE(create_metadata_store(entries, &base).ok());
        const Origin origin         = base.entry(0U).origin;
        std::string replacement     = "Longer camera name";
        const std::array operations = {
            typed_operation(MetadataEditingOperationKind::Set, make,
                            make_value_view_text(replacement,
                                                 TextEncoding::Ascii)),
            typed_operation(MetadataEditingOperationKind::Remove, keyword, {},
                            0U),
            typed_operation(MetadataEditingOperationKind::Set, keyword,
                            make_value_view_text("retained",
                                                 TextEncoding::Ascii)),
            typed_operation(MetadataEditingOperationKind::Add, custom,
                            make_value_view_text("old", TextEncoding::Utf8)),
            typed_operation(MetadataEditingOperationKind::Set, custom,
                            make_value_view_text("new & exact",
                                                 TextEncoding::Utf8)),
            typed_operation(MetadataEditingOperationKind::Set, private_key,
                            make_value_view_u32(70000U)),
        };
        MetaStore result;
        const auto edited = edit_metadata_typed(base, operations, &result);
        ASSERT_TRUE(edited.ok())
            << metadata_typed_editing_status_name(edited.status);
        EXPECT_EQ(edited.operations_applied, 6U);
        EXPECT_EQ(edited.entries_added, 1U);
        EXPECT_EQ(edited.entries_updated, 4U);
        EXPECT_EQ(edited.entries_removed, 1U);
        EXPECT_EQ(result.entry(0U).origin.block, origin.block);
        EXPECT_EQ(result.entry(0U).origin.order_in_block,
                  origin.order_in_block);
        EXPECT_EQ(result.entry(0U).origin.wire_type.family, WireFamily::None);
        EXPECT_EQ(result.entry(0U).origin.wire_count, 0U);
        EXPECT_EQ(result.entry(3U).value.elem_type, MetaElementType::U32);
        EXPECT_EQ(base.entry(3U).value.elem_type, MetaElementType::U16);
        EXPECT_TRUE(any(result.entry(1U).flags, EntryFlags::Deleted));
        replacement.assign("destroyed");
        EXPECT_EQ(arena_string(result.arena(), result.entry(0U).value.data.span),
                  "Longer camera name");
        EXPECT_EQ(arena_string(base.arena(), base.entry(0U).value.data.span),
                  "A");
        const std::string_view borrowed
            = arena_string(result.arena(), result.entry(0U).value.data.span);
        const std::array alias = {
            typed_operation(MetadataEditingOperationKind::Set, custom,
                            make_value_view_text(borrowed, TextEncoding::Ascii))
        };
        ASSERT_TRUE(edit_metadata_typed(result, alias, &result).ok());
        const auto ids = result.find_all(custom);
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(arena_string(result.arena(),
                               result.entry(ids[0]).value.data.span),
                  "Longer camera name");
    }

    TEST(MetadataTypedEditing,
         RequiresExplicitDuplicatesAndRepairsWholeSingleton)
    {
        const auto make = make_exif_tag_key_view("ifd0", 0x010fU);
        const auto text = make_value_view_text("Camera", TextEncoding::Ascii);
        const std::array entries = { MetadataAuthoringEntry { make, text },
                                     MetadataAuthoringEntry { make, text } };
        MetadataAuthoringOptions authoring;
        authoring.validate = false;
        MetaStore base;
        ASSERT_TRUE(create_metadata_store(entries, &base, authoring).ok());
        MetaStore output      = base;
        std::array operations = {
            typed_operation(MetadataEditingOperationKind::Set, make, text)
        };
        EXPECT_EQ(edit_metadata_typed(base, operations, &output).status,
                  MetadataTypedEditingStatus::AmbiguousTarget);
        operations[0].kind = MetadataEditingOperationKind::Add;
        EXPECT_EQ(edit_metadata_typed(base, operations, &output).status,
                  MetadataTypedEditingStatus::TargetAlreadyExists);
        const std::array repair = {
            typed_operation(MetadataEditingOperationKind::Remove, make, {},
                            kMetadataEditingAllOccurrences),
            typed_operation(MetadataEditingOperationKind::Add, make, text),
        };
        ASSERT_TRUE(edit_metadata_typed(base, repair, &output).ok());
        ASSERT_EQ(output.find_all(make).size(), 1U);
        EXPECT_EQ(output.entries().size(), 3U);
        EXPECT_EQ(base.find_all(make).size(), 2U);
        MetadataTypedEditingOptions append;
        append.add_policy = MetadataTypedEditingAddPolicy::Append;
        EXPECT_EQ(
            edit_metadata_typed(output, operations, &output, append).status,
            MetadataTypedEditingStatus::ValidationFailed);
        EXPECT_EQ(output.find_all(make).size(), 1U);
        const auto private_key   = make_exif_tag_key_view("exififd", 0xd123U);
        const std::array repeats = {
            typed_operation(MetadataEditingOperationKind::Add, private_key,
                            make_value_view_u16(1U)),
            typed_operation(MetadataEditingOperationKind::Add, private_key,
                            make_value_view_u16(2U)),
            typed_operation(MetadataEditingOperationKind::Remove, private_key,
                            {}, 0U),
            typed_operation(MetadataEditingOperationKind::Set, private_key,
                            make_value_view_u16(3U)),
        };
        ASSERT_TRUE(edit_metadata_typed(output, repeats, &output, append).ok());
        const auto ids = output.find_all(private_key);
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(output.entry(ids[0]).value.data.u64, 3U);
    }

    TEST(MetadataTypedEditing,
         RejectsLateInvalidValuesAndMissingTargetsTransactionally)
    {
        const auto key = make_xmp_property_key_view("urn:typed-edit", "Value");
        MetaStore base;
        base.finalize();
        MetaStore output;
        const std::array seed
            = { MetadataAuthoringEntry { key, make_value_view_u32(99U) } };
        ASSERT_TRUE(create_metadata_store(seed, &output).ok());
        const auto old_size         = output.arena().bytes().size();
        const std::array bad_values = {
            make_value_view_urational(1U, 0U),
            make_value_view_text(std::string_view("\xc0\xaf", 2U),
                                 TextEncoding::Utf8),
            make_value_view_array(MetaElementType::U32, {}, 2U),
        };
        for (const MetaValueView& bad : bad_values) {
            const std::array operations = {
                typed_operation(MetadataEditingOperationKind::Add, key,
                                make_value_view_u32(1U)),
                typed_operation(MetadataEditingOperationKind::Set, key, bad),
            };
            const auto result = edit_metadata_typed(base, operations, &output);
            EXPECT_FALSE(result.ok());
            EXPECT_EQ(result.failed_operation_index, 1U);
            EXPECT_EQ(result.operations_applied, 0U);
            EXPECT_EQ(output.entry(0U).value.data.u64, 99U);
            EXPECT_EQ(output.arena().bytes().size(), old_size);
            EXPECT_TRUE(base.entries().empty());
        }
        const std::array missing = {
            typed_operation(MetadataEditingOperationKind::Add, key,
                            make_value_view_u32(1U)),
            typed_operation(MetadataEditingOperationKind::Remove,
                            make_xmp_property_key_view("urn:typed-edit",
                                                       "Absent")),
        };
        const auto result = edit_metadata_typed(base, missing, &base);
        EXPECT_EQ(result.status, MetadataTypedEditingStatus::TargetNotFound);
        EXPECT_EQ(result.failed_operation_index, 1U);
        EXPECT_TRUE(base.entries().empty());
    }

    TEST(MetadataTypedEditing, EnforcesOutputRequestAndExpandedOperationLimits)
    {
        const auto key   = make_iptc_dataset_key_view(2U, 25U);
        const auto value = make_value_view_text("word", TextEncoding::Ascii);
        const std::array entries = { MetadataAuthoringEntry { key, value },
                                     MetadataAuthoringEntry { key, value } };
        MetaStore base;
        ASSERT_TRUE(create_metadata_store(entries, &base).ok());
        const std::array remove
            = { typed_operation(MetadataEditingOperationKind::Remove, key, {},
                                kMetadataEditingAllOccurrences) };
        MetadataTypedEditingOptions options;
        options.max_operations = 1U;
        MetaStore output       = base;
        EXPECT_EQ(edit_metadata_typed(base, remove, &output, options).status,
                  MetadataTypedEditingStatus::LimitExceeded);
        EXPECT_EQ(output.find_all(key).size(), 2U);
        const std::array add
            = { typed_operation(MetadataEditingOperationKind::Add,
                                make_iptc_dataset_key_view(2U, 120U), value) };
        options             = {};
        options.max_entries = 2U;
        EXPECT_EQ(edit_metadata_typed(base, add, &output, options).status,
                  MetadataTypedEditingStatus::LimitExceeded);
        options                   = {};
        options.max_request_bytes = 3U;
        EXPECT_EQ(edit_metadata_typed(base, add, &output, options).status,
                  MetadataTypedEditingStatus::LimitExceeded);
        options                 = {};
        options.max_arena_bytes = base.arena().bytes().size();
        options.max_value_bytes = options.max_arena_bytes;
        EXPECT_EQ(edit_metadata_typed(base, add, &output, options).status,
                  MetadataTypedEditingStatus::LimitExceeded);
        options = {};
        base.constrain_resources(2U, 0U);
        EXPECT_EQ(edit_metadata_typed(base, add, &output, options).status,
                  MetadataTypedEditingStatus::LimitExceeded);
        EXPECT_EQ(output.entries().size(), 2U);
        EXPECT_FALSE(base.resource_limit_exceeded());
    }

    TEST(MetadataTypedEditing, AuthorsAndValidatesNineNativeCaptureShapes)
    {
        const std::array<std::byte, 1> file    = { std::byte { 2 } };
        const std::array<std::byte, 1> scene   = { std::byte { 1 } };
        constexpr std::array<uint16_t, 9> tags = { 0xa405U, 0xa300U, 0xa301U,
                                                   0x9400U, 0x9401U, 0x9402U,
                                                   0x9403U, 0x9404U, 0x9405U };
        const std::array<MetaValueView, 9> values
            = { make_value_view_u16(0U),
                make_value_view_bytes(file),
                make_value_view_bytes(scene),
                make_value_view_srational(-41, 2),
                make_value_view_urational(301U, 3U),
                make_value_view_urational(7U, UINT32_MAX),
                make_value_view_srational(-7, -1),
                make_value_view_urational(980665U, 1U),
                make_value_view_srational(-180, 1) };
        std::array<MetadataAuthoringEntry, 9> entries {};
        std::array<MetadataTypedEditingOperation, 9> operations {};
        for (size_t i = 0; i < tags.size(); ++i) {
            entries[i].key   = make_exif_tag_key_view("exififd", tags[i]);
            entries[i].value = values[i];
            operations[i] = typed_operation(MetadataEditingOperationKind::Set,
                                            entries[i].key, values[i]);
        }
        MetaStore source;
        ASSERT_TRUE(create_metadata_store(entries, &source).ok());
        ASSERT_TRUE(edit_metadata_typed(source, operations, &source).ok());
        EXPECT_TRUE(validate_store(source).ok());
        operations[0].entry.value = make_value_view_u16(50U);
        operations[8].entry.value = make_value_view_srational(180, 1);
        const auto rejected = edit_metadata_typed(source, operations, &source);
        EXPECT_EQ(rejected.status,
                  MetadataTypedEditingStatus::ValidationFailed);
        EXPECT_EQ(rejected.validation_issue,
                  MetadataValidationIssueCode::ScalarOutOfRange);
        EXPECT_EQ(rejected.failed_operation_index, 8U);
        EXPECT_EQ(source.entry(0U).value.data.u64, 0U);
        const std::array<std::byte, 1> invalid_code = { std::byte { 4 } };
        operations[1].entry.value = make_value_view_bytes(invalid_code);
        EXPECT_EQ(edit_metadata_typed(source, std::span(&operations[1], 1U),
                                      &source)
                      .status,
                  MetadataTypedEditingStatus::ValidationFailed);
    }

    TEST(MetadataTypedEditing, RepeatedSetsUseSeparateRequestAndOutputBudgets)
    {
        const auto key = make_exif_tag_key_view("exififd", 0xa405U);
        const MetadataAuthoringEntry entry { key, make_value_view_u16(35U) };
        MetaStore base;
        ASSERT_TRUE(create_metadata_store(std::span(&entry, 1U), &base).ok());
        const std::array operations
            = { typed_operation(MetadataEditingOperationKind::Set, key,
                                make_value_view_u16(50U)),
                typed_operation(MetadataEditingOperationKind::Set, key,
                                make_value_view_u16(85U)) };
        MetadataTypedEditingOptions options;
        options.max_entries     = 1U;
        options.max_arena_bytes = base.arena().bytes().size();
        options.max_value_bytes = options.max_arena_bytes;
        const auto result       = edit_metadata_typed(base, operations, &base,
                                                      options);
        ASSERT_TRUE(result.ok())
            << metadata_typed_editing_status_name(result.status);
        EXPECT_EQ(base.entry(0U).value.data.u64, 85U);
    }

    TEST(MetadataTypedEditing,
         ChecksExplicitWireShapesAndCompleteImageRelationships)
    {
        const auto key           = make_exif_tag_key_view("ifd0", 0xd123U);
        const std::array entries = { MetadataAuthoringEntry {
            key, make_value_view_u16(1U), { WireFamily::Tiff, 3U }, 1U } };
        MetaStore base;
        ASSERT_TRUE(create_metadata_store(entries, &base).ok());
        const std::array bytes = { std::byte { 1 }, std::byte { 2 },
                                   std::byte { 3 } };
        std::array operations
            = { typed_operation(MetadataEditingOperationKind::Set, key,
                                make_value_view_bytes(bytes)) };
        operations[0].entry.wire_type  = { WireFamily::Tiff, 7U };
        operations[0].entry.wire_count = 3U;
        MetaStore output;
        ASSERT_TRUE(edit_metadata_typed(base, operations, &output).ok());
        EXPECT_EQ(output.entry(0U).origin.wire_count, 3U);
        EXPECT_EQ(output.entry(0U).origin.wire_type.code, 7U);
        operations[0].entry.wire_count = 2U;
        const auto result = edit_metadata_typed(base, operations, &output);
        EXPECT_EQ(result.status, MetadataTypedEditingStatus::ValidationFailed);
        EXPECT_EQ(result.validation_issue,
                  MetadataValidationIssueCode::InvalidWireCount);
        EXPECT_EQ(output.entry(0U).origin.wire_count, 3U);
        const std::array geometry = {
            typed_operation(MetadataEditingOperationKind::Add,
                            make_exif_tag_key_view("ifd0", 0x100U),
                            make_value_view_u32(10U)),
            typed_operation(MetadataEditingOperationKind::Add,
                            make_exif_tag_key_view("ifd0", 0x101U),
                            make_value_view_u32(20U)),
        };
        MetadataTypedEditingOptions options;
        options.validation.context.has_dimensions = true;
        options.validation.context.width          = 10U;
        options.validation.context.height         = 21U;
        EXPECT_EQ(edit_metadata_typed(base, geometry, &output, options).status,
                  MetadataTypedEditingStatus::ValidationFailed);
        EXPECT_EQ(output.entries().size(), 1U);
    }

}  // namespace
}  // namespace openmeta
