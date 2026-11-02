// SPDX-License-Identifier: Apache-2.0

#include "custom_xmp_test_fixture.h"

#include "openmeta/meta_flags.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_query.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_xmp_editing.h"
#include "openmeta/validate.h"
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

    namespace fixture = custom_xmp_test;

    static std::vector<std::byte> snapshot_bytes(const MetaStore& store)
    {
        std::vector<std::byte> bytes;
        const TransferSourceSnapshotIoResult result
            = serialize_transfer_source_snapshot(build_transfer_source_snapshot(
                                                     store),
                                                 &bytes);
        EXPECT_EQ(result.status, TransferStatus::Ok) << result.message;
        return bytes;
    }

    static EntryId unique_id(const MetaStore& store, std::string_view schema,
                             std::string_view path)
    {
        const std::span<const EntryId> entries = store.find_all(
            make_xmp_property_key_view(schema, path));
        EXPECT_EQ(entries.size(), 1U) << path;
        return entries.size() == 1U ? entries.front() : kInvalidEntryId;
    }

    static void expect_text(const MetaStore& store, std::string_view schema,
                            std::string_view path, std::string_view text)
    {
        const EntryId id = unique_id(store, schema, path);
        ASSERT_NE(id, kInvalidEntryId);
        const Entry& entry = store.entry(id);
        ASSERT_EQ(entry.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> bytes = store.arena().span(
            entry.value.data.span);
        ASSERT_EQ(bytes.size(), entry.value.count);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                   bytes.size()),
                  text);
    }

    static MetadataXmpEditingOperation
    group_operation(MetadataXmpEditingOperationKind kind,
                    std::string_view schema, std::string_view scope,
                    std::span<const MetadataAuthoringEntry> entries = {})
    {
        return MetadataXmpEditingOperation { kind, schema, scope, entries };
    }

    TEST(MetadataCustomXmp,
         AuthoringChecksWholeTreesAndPreservesOutputOnFailure)
    {
        std::vector<MetadataAuthoringEntry> entries = fixture::initial_entries();
        entries.push_back(make_xmp_container_entry(fixture::kSchema, "Title",
                                                   XmpContainerKind::Struct));
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(entries, &store).ok());
        ASSERT_TRUE(validate_store(store).ok());
        EXPECT_EQ(kMetadataAuthoringContractVersion, 3U);
        EXPECT_EQ(kMetadataTypedEditingContractVersion, 3U);
        EXPECT_EQ(kMetadataXmpEditingContractVersion, 2U);
        EXPECT_EQ(kMetadataValidationContractVersion, 2U);
        EXPECT_EQ(kXmpStructureContractVersion, 1U);

        const EntryId title_marker = unique_id(store, fixture::kSchema,
                                               "Title");
        ASSERT_NE(title_marker, kInvalidEntryId);
        const MetadataQueryResult descriptive
            = query_metadata(store, MetadataQueryKind::Descriptive);
        for (const MetadataQueryMatch& match : descriptive.matches)
            EXPECT_NE(match.entry_id, title_marker);

        const std::vector<std::byte> original = snapshot_bytes(store);
        MetadataAuthoringEntry invalid
            = make_xmp_container_entry(fixture::kSchema, "Root",
                                       XmpContainerKind::Struct);
        invalid.value = make_value_view_u64(UINT64_MAX);
        EXPECT_EQ(create_metadata_store({ &invalid, 1U }, &store).status,
                  MetadataAuthoringStatus::InvalidValue);
        EXPECT_EQ(snapshot_bytes(store), original);

        const std::array orphan {
            make_xmp_container_entry(fixture::kSchema, "Root",
                                     XmpContainerKind::Struct),
            fixture::text_entry(fixture::kSchema, "Root/Missing/Value", "x")
        };
        const MetadataAuthoringResult failed = create_metadata_store(orphan,
                                                                     &store);
        EXPECT_EQ(failed.status, MetadataAuthoringStatus::ValidationFailed);
        EXPECT_EQ(failed.validation_issue,
                  MetadataValidationIssueCode::InvalidXmpStructure);
        EXPECT_EQ(snapshot_bytes(store), original);
    }

    TEST(MetadataCustomXmp, SharedLimitsReachAuthoringAndExactTypedEdits)
    {
        const std::vector<MetadataAuthoringEntry> entries
            = fixture::initial_entries();
        MetaStore source;
        ASSERT_TRUE(create_metadata_store(entries, &source).ok());
        const std::vector<std::byte> original = snapshot_bytes(source);
        const MetadataTypedEditingOperation update {
            MetadataEditingOperationKind::Set,
            fixture::text_entry(fixture::kSchema, "Record/Title", "updated")
        };
        for (uint32_t variant = 0U; variant < 4U; ++variant) {
            XmpStructureLimits limits;
            switch (variant) {
            case 0U: limits.max_nodes = 1U; break;
            case 1U: limits.max_depth = 1U; break;
            case 2U: limits.max_namespaces = 1U; break;
            default: limits.max_request_bytes = 1U; break;
            }
            MetadataAuthoringOptions authoring;
            authoring.validation.structures = limits;
            MetaStore output                = source;
            const MetadataAuthoringResult authored
                = create_metadata_store(entries, &output, authoring);
            EXPECT_EQ(authored.status, MetadataAuthoringStatus::LimitExceeded);
            EXPECT_EQ(authored.validation_issue,
                      MetadataValidationIssueCode::XmpStructureLimitExceeded);
            EXPECT_EQ(snapshot_bytes(output), original);

            MetadataTypedEditingOptions editing;
            editing.validation.structures = limits;
            const MetadataTypedEditingResult edited
                = edit_metadata_typed(output, { &update, 1U }, &output,
                                      editing);
            EXPECT_EQ(edited.status, MetadataTypedEditingStatus::LimitExceeded);
            EXPECT_EQ(edited.validation_issue,
                      MetadataValidationIssueCode::XmpStructureLimitExceeded);
            EXPECT_EQ(snapshot_bytes(output), original);
        }
        MetadataXmpEditingOptions invalid_options;
        invalid_options.typed.validation.structures.max_nodes = 0U;
        const MetadataXmpEditingOperation operation
            = group_operation(MetadataXmpEditingOperationKind::Remove,
                              fixture::kSchema, "Record/EmptyStruct");
        EXPECT_EQ(edit_metadata_xmp(source, { &operation, 1U }, &source,
                                    invalid_options)
                      .status,
                  MetadataXmpEditingStatus::InvalidOptions);
        EXPECT_EQ(snapshot_bytes(source), original);
    }

    TEST(MetadataCustomXmp,
         MixedTransactionsRetainIdentityAndExactItemBoundaries)
    {
        const std::vector<MetadataAuthoringEntry> entries
            = fixture::initial_entries();
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(entries, &store).ok());
        const EntryId values    = unique_id(store, fixture::kSchema,
                                            "Record/Values");
        const EntryId three     = unique_id(store, fixture::kSchema,
                                            "Record/Values[3]");
        const EntryId frame_ten = unique_id(store, fixture::kSchema,
                                            "Record/Frames[10]/Name");
        ASSERT_NE(values, kInvalidEntryId);
        ASSERT_NE(three, kInvalidEntryId);
        ASSERT_NE(frame_ten, kInvalidEntryId);
        const Origin origin = store.entry(values).origin;
        const std::array replacement {
            make_xmp_container_entry(fixture::kSchema, "Record/Values",
                                     XmpContainerKind::Seq),
            fixture::text_entry(fixture::kSchema, "Record/Values[3]", "THREE"),
            fixture::text_entry(fixture::kSchema, "Record/Values[10]", "TEN")
        };
        constexpr std::string_view dc = "http://purl.org/dc/elements/1.1/";
        const std::array title { fixture::text_entry(
            dc, "title[@xml:lang=x-default]", "Standard title") };
        const std::array language {
            fixture::text_entry(fixture::kSchema,
                                "Record/Labels[@xml:lang=en-US]", "New English")
        };
        const std::array operations {
            group_operation(MetadataXmpEditingOperationKind::Replace,
                            fixture::kSchema, "Record/Values", replacement),
            group_operation(MetadataXmpEditingOperationKind::Remove,
                            fixture::kSchema, "Record/Frames[1]"),
            group_operation(MetadataXmpEditingOperationKind::Replace,
                            fixture::kSchema, "Record/Labels[@xml:lang=EN-us]",
                            language),
            group_operation(MetadataXmpEditingOperationKind::Add, dc, "title",
                            title)
        };
        const MetadataXmpEditingResult result
            = edit_metadata_xmp(store, operations, &store);
        ASSERT_TRUE(result.ok())
            << metadata_xmp_editing_status_name(result.status);
        EXPECT_EQ(result.operations_applied, operations.size());
        EXPECT_EQ(unique_id(store, fixture::kSchema, "Record/Values"), values);
        EXPECT_EQ(unique_id(store, fixture::kSchema, "Record/Values[3]"),
                  three);
        EXPECT_EQ(xmp_container_kind(store.entry(values)),
                  XmpContainerKind::Seq);
        EXPECT_EQ(store.entry(values).origin.block, origin.block);
        EXPECT_EQ(store.entry(values).origin.order_in_block,
                  origin.order_in_block);
        EXPECT_TRUE(
            store
                .find_all(make_xmp_property_key_view(fixture::kSchema,
                                                     "Record/Values[1]"))
                .empty());
        EXPECT_TRUE(
            store
                .find_all(make_xmp_property_key_view(fixture::kSchema,
                                                     "Record/Frames[1]"))
                .empty());
        EXPECT_EQ(unique_id(store, fixture::kSchema, "Record/Frames[10]/Name"),
                  frame_ten);
        expect_text(store, fixture::kSchema, "Record/Frames[10]/Name", "ten");
        expect_text(store, fixture::kSchema, "Record/Labels[@xml:lang=en-US]",
                    "New English");
        expect_text(store, fixture::kSchema,
                    "Record/Labels[@xml:lang=x-default]", "Default label");
        expect_text(store, fixture::kOtherSchema, "Record/Local",
                    "other-root-namespace");
        expect_text(store, dc, "title[@xml:lang=x-default]", "Standard title");
        ASSERT_TRUE(validate_store(store).ok());
    }

    TEST(MetadataCustomXmp,
         WholeRecordReplacementAndRemovalPreserveOtherNamespaces)
    {
        const std::vector<MetadataAuthoringEntry> entries
            = fixture::initial_entries();
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(entries, &store).ok());
        const EntryId root  = unique_id(store, fixture::kSchema, "Record");
        const EntryId title = unique_id(store, fixture::kSchema,
                                        "Record/Title");
        const std::array replacement {
            make_xmp_container_entry(fixture::kSchema, "Record",
                                     XmpContainerKind::Struct),
            fixture::text_entry(fixture::kSchema, "Record/Title", "only field")
        };
        const MetadataXmpEditingOperation replace
            = group_operation(MetadataXmpEditingOperationKind::Replace,
                              fixture::kSchema, "Record", replacement);
        ASSERT_TRUE(edit_metadata_xmp(store, { &replace, 1U }, &store).ok());
        EXPECT_EQ(unique_id(store, fixture::kSchema, "Record"), root);
        EXPECT_EQ(unique_id(store, fixture::kSchema, "Record/Title"), title);
        EXPECT_TRUE(store
                        .find_all(make_xmp_property_key_view(fixture::kSchema,
                                                             "Record/Frames"))
                        .empty());
        expect_text(store, fixture::kOtherSchema, "Record/Local",
                    "other-root-namespace");

        const MetadataXmpEditingOperation remove
            = group_operation(MetadataXmpEditingOperationKind::Remove,
                              fixture::kSchema, "Record");
        ASSERT_TRUE(edit_metadata_xmp(store, { &remove, 1U }, &store).ok());
        EXPECT_TRUE(store
                        .find_all(make_xmp_property_key_view(fixture::kSchema,
                                                             "Record"))
                        .empty());
        expect_text(store, fixture::kOtherSchema, "Record/Local",
                    "other-root-namespace");
        expect_text(store, fixture::kXapSchema, "CreatorTool", "old tool");
        ASSERT_TRUE(validate_store(store).ok());
    }

    TEST(MetadataCustomXmp,
         LateFailuresAndMarkerOnlyDeletionLeaveAliasedStoreUnchanged)
    {
        const std::vector<MetadataAuthoringEntry> entries
            = fixture::initial_entries();
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(entries, &store).ok());
        const std::vector<std::byte> original = snapshot_bytes(store);
        const std::array title { fixture::text_entry(fixture::kSchema,
                                                     "Record/Title",
                                                     "must roll back") };
        const std::array existing_frame {
            make_xmp_container_entry(fixture::kSchema, "Record/Frames[10]",
                                     XmpContainerKind::Struct),
            fixture::text_entry(fixture::kSchema, "Record/Frames[10]/Name",
                                "duplicate")
        };
        const std::array operations {
            group_operation(MetadataXmpEditingOperationKind::Replace,
                            fixture::kSchema, "Record/Title", title),
            group_operation(MetadataXmpEditingOperationKind::Add,
                            fixture::kSchema, "Record/Frames[10]",
                            existing_frame)
        };
        const MetadataXmpEditingResult failed
            = edit_metadata_xmp(store, operations, &store);
        EXPECT_EQ(failed.status, MetadataXmpEditingStatus::ScopeAlreadyExists);
        EXPECT_EQ(failed.failed_operation_index, 1U);
        EXPECT_EQ(snapshot_bytes(store), original);

        MetadataTypedEditingOperation marker_only;
        marker_only.kind      = MetadataEditingOperationKind::Remove;
        marker_only.entry.key = make_xmp_property_key_view(fixture::kSchema,
                                                           "Record/Matrix");
        const MetadataTypedEditingResult orphaned
            = edit_metadata_typed(store, { &marker_only, 1U }, &store);
        EXPECT_EQ(orphaned.status,
                  MetadataTypedEditingStatus::ValidationFailed);
        EXPECT_EQ(orphaned.validation_issue,
                  MetadataValidationIssueCode::InvalidXmpStructure);
        EXPECT_EQ(snapshot_bytes(store), original);
        marker_only.entry.key = make_xmp_property_key_view(fixture::kSchema,
                                                           "Record");
        EXPECT_FALSE(
            edit_metadata_typed(store, { &marker_only, 1U }, &store).ok());
        EXPECT_EQ(snapshot_bytes(store), original);

        const MetadataXmpEditingOperation complete
            = group_operation(MetadataXmpEditingOperationKind::Remove,
                              fixture::kSchema, "Record/Matrix");
        EXPECT_TRUE(edit_metadata_xmp(store, { &complete, 1U }, &store).ok());
        EXPECT_TRUE(validate_store(store).ok());
    }

    TEST(MetadataCustomXmp, SnapshotWireRetainsKindsIndicesAndTombstones)
    {
        const std::vector<MetadataAuthoringEntry> entries
            = fixture::initial_entries();
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(entries, &store).ok());
        const MetadataXmpEditingOperation remove
            = group_operation(MetadataXmpEditingOperationKind::Remove,
                              fixture::kSchema, "Record/Frames[1]");
        ASSERT_TRUE(edit_metadata_xmp(store, { &remove, 1U }, &store).ok());
        const std::vector<std::byte> bytes = snapshot_bytes(store);
        TransferSourceSnapshot restored;
        ASSERT_EQ(deserialize_transfer_source_snapshot(bytes, &restored).status,
                  TransferStatus::Ok);
        ASSERT_EQ(restored.store.entries().size(), store.entries().size());
        EXPECT_EQ(snapshot_bytes(restored.store), bytes);
        for (EntryId id = 0U; id < store.entries().size(); ++id) {
            const Entry& before = store.entry(id);
            const Entry& after  = restored.store.entry(id);
            EXPECT_EQ(after.flags, before.flags);
            EXPECT_EQ(after.origin.wire_type.family,
                      before.origin.wire_type.family);
            EXPECT_EQ(after.origin.wire_type.code,
                      before.origin.wire_type.code);
            EXPECT_EQ(after.origin.wire_count, before.origin.wire_count);
            EXPECT_EQ(xmp_container_kind(after), xmp_container_kind(before));
        }
        expect_text(restored.store, fixture::kSchema, "Record/Frames[10]/Name",
                    "ten");
        ASSERT_TRUE(validate_store(restored.store).ok());

        MetaStore malformed;
        Entry invalid;
        invalid.key = make_xmp_property_key(malformed.arena(), fixture::kSchema,
                                            "Bad");
        invalid.origin.wire_type = WireType { WireFamily::Other, 0x584fU };
        (void)malformed.add_entry(invalid);
        malformed.finalize();
        const std::vector<std::byte> malformed_wire = snapshot_bytes(malformed);
        ASSERT_EQ(deserialize_transfer_source_snapshot(malformed_wire, &restored)
                      .status,
                  TransferStatus::Ok);
        EXPECT_EQ(validate_store(restored.store).status,
                  MetadataValidationStatus::InvalidMetadata);
    }

    TEST(MetadataCustomXmp, SupportsMoreThanNineIndependentRootScopes)
    {
        std::array<std::string, 12U> roots;
        std::array<MetadataAuthoringEntry, 12U> entries;
        std::array<MetadataXmpEditingOperation, 12U> operations;
        for (uint32_t index = 0U; index < roots.size(); ++index)
            roots[index] = "Root" + std::to_string(index);
        for (uint32_t index = 0U; index < roots.size(); ++index) {
            entries[index] = make_xmp_container_entry(fixture::kSchema,
                                                      roots[index],
                                                      XmpContainerKind::Struct);
            operations[index]
                = group_operation(MetadataXmpEditingOperationKind::Add,
                                  fixture::kSchema, roots[index],
                                  { &entries[index], 1U });
        }
        MetaStore store;
        store.finalize();
        ASSERT_TRUE(edit_metadata_xmp(store, operations, &store).ok());
        EXPECT_EQ(store.entries().size(), roots.size());
        EXPECT_TRUE(validate_store(store).ok());
    }

}  // namespace
}  // namespace openmeta
