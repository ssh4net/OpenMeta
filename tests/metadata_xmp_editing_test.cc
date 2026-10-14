// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_xmp_editing.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static constexpr std::string_view kIptcCore
        = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
    static constexpr std::string_view kIptcExt
        = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
    static constexpr std::string_view kPlus
        = "http://ns.useplus.org/ldf/xmp/1.0/";
    static constexpr std::string_view kDc
        = "http://purl.org/dc/elements/1.1/";
    static constexpr std::string_view kXmpRights
        = "http://ns.adobe.com/xap/1.0/rights/";

    static void add_xmp_text(
        MetaStore* store, std::string_view schema, std::string_view path,
        std::string_view value, uint32_t order,
        EntryFlags flags = EntryFlags::None, BlockId block = kInvalidBlockId,
        WireType wire_type = {}, uint32_t wire_count = 0U)
    {
        Entry entry;
        entry.key = make_xmp_property_key(store->arena(), schema, path);
        entry.value = make_text(store->arena(), value, TextEncoding::Utf8);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.origin.wire_type      = wire_type;
        entry.origin.wire_count     = wire_count;
        entry.flags                 = flags;
        ASSERT_NE(store->add_entry(entry), kInvalidEntryId);
    }

    static MetadataAuthoringEntry authoring_text(std::string_view schema,
                                                 std::string_view path,
                                                 std::string_view value)
    {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = schema;
        key.data.xmp_property.property_path = path;
        return MetadataAuthoringEntry {
            key,
            make_value_view_text(value, TextEncoding::Utf8),
            WireType {},
            0U,
        };
    }

    static MetadataXmpEditingOperation make_operation(
        MetadataXmpEditingOperationKind kind, std::string_view schema,
        std::string_view scope,
        std::span<const MetadataAuthoringEntry> entries = {})
    {
        MetadataXmpEditingOperation operation;
        operation.kind        = kind;
        operation.schema_ns  = schema;
        operation.property_path = scope;
        operation.entries    = entries;
        return operation;
    }

    static const Entry* find_any(const MetaStore& store,
                                 std::string_view schema,
                                 std::string_view path)
    {
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (entry.key.kind != MetaKeyKind::XmpProperty)
                continue;
            const std::span<const std::byte> entry_schema = store.arena().span(
                entry.key.data.xmp_property.schema_ns);
            const std::span<const std::byte> entry_path = store.arena().span(
                entry.key.data.xmp_property.property_path);
            if (entry_schema.size()
                    != entry.key.data.xmp_property.schema_ns.size
                || entry_path.size()
                       != entry.key.data.xmp_property.property_path.size)
                continue;
            const std::string_view schema_view(
                reinterpret_cast<const char*>(entry_schema.data()),
                entry_schema.size());
            const std::string_view path_view(
                reinterpret_cast<const char*>(entry_path.data()),
                entry_path.size());
            if (schema_view == schema && path_view == path)
                return &entry;
        }
        return nullptr;
    }

    static std::string_view active_value_for(const MetaStore& store,
                                             std::string_view schema,
                                             std::string_view path)
    {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = schema;
        key.data.xmp_property.property_path = path;
        const std::span<const EntryId> ids = store.find_all(key);
        if (ids.size() != 1U)
            return {};
        const Entry& entry = store.entry(ids[0]);
        const std::span<const std::byte> bytes
            = store.arena().span(entry.value.data.span);
        if (bytes.size() != entry.value.count || bytes.empty())
            return {};
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static void add_sentinel(MetaStore* store)
    {
        add_xmp_text(store, kIptcExt, "LocationShown[9]/City", "sentinel",
                     0U);
        store->finalize();
    }

    static void expect_sentinel(const MetaStore& store)
    {
        EXPECT_EQ(active_value_for(store, kIptcExt,
                                   "LocationShown[9]/City"),
                  "sentinel");
        EXPECT_EQ(store.entries().size(), 1U);
    }

    TEST(MetadataXmpEditing, ItemScopeOneDoesNotSelectItemTen)
    {
        MetaStore base;
        add_xmp_text(&base, kIptcExt, "LocationShown[1]/City", "First", 0U);
        add_xmp_text(&base, kIptcExt, "LocationShown[10]/City", "Tenth", 1U);
        base.finalize();

        const MetadataAuthoringEntry replacement = authoring_text(
            kIptcExt, "LocationShown[1]/City", "Edited first");
        const MetadataXmpEditingOperation operation = make_operation(
            MetadataXmpEditingOperationKind::Replace, kIptcExt,
            "LocationShown[1]", { &replacement, 1U });
        MetaStore edited;
        const MetadataXmpEditingResult result = edit_metadata_xmp(
            base, { &operation, 1U }, &edited);

        ASSERT_TRUE(result.ok())
            << metadata_xmp_editing_status_name(result.status);
        EXPECT_EQ(result.operations_applied, 1U);
        EXPECT_EQ(active_value_for(edited, kIptcExt,
                                   "LocationShown[1]/City"),
                  "Edited first");
        EXPECT_EQ(active_value_for(edited, kIptcExt,
                                   "LocationShown[10]/City"),
                  "Tenth");
    }

    TEST(MetadataXmpEditing, AcceptsEveryV1RootAndCoreNestedLeafShape)
    {
        std::array<MetadataAuthoringEntry, 13U> entries {
            authoring_text(kIptcCore,
                           "CreatorContactInfo/CiAdrRegion/CustomScalar",
                           "scalar"),
            authoring_text(kIptcCore,
                           "CreatorContactInfo/CiAdrRegion/ProvinceCode[1]",
                           "CA"),
            authoring_text(
                kIptcCore,
                "CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=en-US]",
                "California"),
            authoring_text(kIptcCore, "LocationCreated/City", "Kyoto"),
            authoring_text(kIptcExt,
                           "LocationShown[1]/xmp:Identifier[1]", "asset-id"),
            authoring_text(kIptcExt,
                           "LocationShown[1]/exif:GPSLatitude", "35.0"),
            authoring_text(kIptcExt, "LocationShown[1]/City", "Kyoto"),
            authoring_text(kPlus, "Licensee[1]/LicenseeName", "Archive"),
            authoring_text(kDc, "title[@xml:lang=en-US]", "Title"),
            authoring_text(kDc, "description[@xml:lang=en-US]", "Summary"),
            authoring_text(kDc, "rights[@xml:lang=en-US]", "Rights"),
            authoring_text(kXmpRights, "UsageTerms[@xml:lang=en-US]", "Use"),
            authoring_text(kXmpRights, "Owner[1]", "Owner name"),
        };
        std::array<MetadataXmpEditingOperation, 9U> operations {
            make_operation(MetadataXmpEditingOperationKind::Add, kIptcCore,
                           "CreatorContactInfo", { entries.data(), 3U }),
            make_operation(MetadataXmpEditingOperationKind::Add, kIptcCore,
                           "LocationCreated", { entries.data() + 3U, 1U }),
            make_operation(MetadataXmpEditingOperationKind::Add, kIptcExt,
                           "LocationShown[1]", { entries.data() + 4U, 3U }),
            make_operation(MetadataXmpEditingOperationKind::Add, kPlus,
                           "Licensee[1]", { entries.data() + 7U, 1U }),
            make_operation(MetadataXmpEditingOperationKind::Add, kDc, "title",
                           { entries.data() + 8U, 1U }),
            make_operation(MetadataXmpEditingOperationKind::Add, kDc,
                           "description", { entries.data() + 9U, 1U }),
            make_operation(MetadataXmpEditingOperationKind::Add, kDc, "rights",
                           { entries.data() + 10U, 1U }),
            make_operation(MetadataXmpEditingOperationKind::Add, kXmpRights,
                           "UsageTerms", { entries.data() + 11U, 1U }),
            make_operation(MetadataXmpEditingOperationKind::Add, kXmpRights,
                           "Owner", { entries.data() + 12U, 1U }),
        };
        MetaStore base;
        base.finalize();
        MetaStore edited;
        const MetadataXmpEditingResult result
            = edit_metadata_xmp(base, operations, &edited);

        ASSERT_TRUE(result.ok())
            << metadata_xmp_editing_status_name(result.status)
            << " op=" << result.failed_operation_index;
        EXPECT_EQ(result.operations_applied, operations.size());
        EXPECT_EQ(result.entries_added, entries.size());
        EXPECT_EQ(active_value_for(
                      edited, kIptcExt,
                      "LocationShown[1]/xmp:Identifier[1]"),
                  "asset-id");
        EXPECT_EQ(active_value_for(edited, kIptcExt,
                                   "LocationShown[1]/exif:GPSLatitude"),
                  "35.0");
        EXPECT_EQ(active_value_for(
                      edited, kIptcCore,
                      "CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=en-US]"),
                  "California");
    }

    TEST(MetadataXmpEditing, RejectsUnreadableAndTruncatedSelectedBaseLeaves)
    {
        const std::array<EntryFlags, 2U> bad_flags {
            EntryFlags::Truncated,
            EntryFlags::Unreadable,
        };
        for (const EntryFlags flags : bad_flags) {
            MetaStore base;
            add_xmp_text(&base, kIptcExt, "LocationShown[1]/City", "partial",
                         0U, flags);
            base.finalize();
            const MetadataAuthoringEntry replacement = authoring_text(
                kIptcExt, "LocationShown[1]/City", "whole");
            const MetadataXmpEditingOperation operation = make_operation(
                MetadataXmpEditingOperationKind::Replace, kIptcExt,
                "LocationShown[1]", { &replacement, 1U });
            MetaStore output;
            add_sentinel(&output);

            const MetadataXmpEditingResult result = edit_metadata_xmp(
                base, { &operation, 1U }, &output);

            EXPECT_EQ(result.status,
                      MetadataXmpEditingStatus::UnsupportedSelectedEntry);
            EXPECT_EQ(result.failed_operation_index, 0U);
            expect_sentinel(output);
        }
    }

    TEST(MetadataXmpEditing, RejectsBaseAndInputResourceAncestorCollisions)
    {
        const MetadataAuthoringEntry replacement = authoring_text(
            kIptcCore, "CreatorContactInfo/City", "replacement");
        const MetadataXmpEditingOperation replace = make_operation(
            MetadataXmpEditingOperationKind::Replace, kIptcCore,
            "CreatorContactInfo", { &replacement, 1U });
        MetaStore colliding_base;
        add_xmp_text(&colliding_base, kIptcCore, "CreatorContactInfo/Foo",
                     "scalar", 0U);
        add_xmp_text(&colliding_base, kIptcCore,
                     "CreatorContactInfo/Foo/Bar", "nested", 1U);
        colliding_base.finalize();
        MetaStore base_output;
        add_sentinel(&base_output);

        const MetadataXmpEditingResult base_result = edit_metadata_xmp(
            colliding_base, { &replace, 1U }, &base_output);

        EXPECT_EQ(base_result.status, MetadataXmpEditingStatus::ShapeCollision);
        EXPECT_EQ(base_result.failed_operation_index, 0U);
        expect_sentinel(base_output);

        std::array<MetadataAuthoringEntry, 2U> colliding_input {
            authoring_text(kIptcCore, "CreatorContactInfo/Foo", "scalar"),
            authoring_text(kIptcCore, "CreatorContactInfo/Foo/Bar", "nested"),
        };
        const MetadataXmpEditingOperation add = make_operation(
            MetadataXmpEditingOperationKind::Add, kIptcCore,
            "CreatorContactInfo", colliding_input);
        MetaStore empty;
        empty.finalize();
        MetaStore input_output;
        add_sentinel(&input_output);

        const MetadataXmpEditingResult input_result
            = edit_metadata_xmp(empty, { &add, 1U }, &input_output);

        EXPECT_EQ(input_result.status, MetadataXmpEditingStatus::ShapeCollision);
        EXPECT_EQ(input_result.failed_operation_index, 0U);
        expect_sentinel(input_output);
    }

    TEST(MetadataXmpEditing, OrderedAddReplaceRemoveUpdatesThenTombstones)
    {
        const MetadataAuthoringEntry added = authoring_text(
            kIptcExt, "LocationShown[1]/City", "First");
        const std::array<MetadataAuthoringEntry, 2U> replacements {
            authoring_text(kIptcExt, "LocationShown[1]/City", "Updated"),
            authoring_text(kIptcExt, "LocationShown[1]/CountryCode", "JP"),
        };
        const std::array<MetadataXmpEditingOperation, 3U> operations {
            make_operation(MetadataXmpEditingOperationKind::Add, kIptcExt,
                           "LocationShown[1]", { &added, 1U }),
            make_operation(MetadataXmpEditingOperationKind::Replace, kIptcExt,
                           "LocationShown[1]", replacements),
            make_operation(MetadataXmpEditingOperationKind::Remove, kIptcExt,
                           "LocationShown[1]"),
        };
        MetaStore base;
        base.finalize();
        MetaStore edited;
        const MetadataXmpEditingResult result
            = edit_metadata_xmp(base, operations, &edited);

        ASSERT_TRUE(result.ok())
            << metadata_xmp_editing_status_name(result.status)
            << " op=" << result.failed_operation_index;
        EXPECT_EQ(result.operations_applied, 3U);
        EXPECT_EQ(result.expanded_operations, 5U);
        EXPECT_EQ(result.entries_added, 2U);
        EXPECT_EQ(result.entries_updated, 1U);
        EXPECT_EQ(result.entries_removed, 2U);
        EXPECT_EQ(active_value_for(edited, kIptcExt,
                                   "LocationShown[1]/City"), "");
        EXPECT_EQ(active_value_for(edited, kIptcExt,
                                   "LocationShown[1]/CountryCode"), "");
        const Entry* city = find_any(edited, kIptcExt,
                                     "LocationShown[1]/City");
        const Entry* country = find_any(edited, kIptcExt,
                                        "LocationShown[1]/CountryCode");
        ASSERT_NE(city, nullptr);
        ASSERT_NE(country, nullptr);
        EXPECT_TRUE(any(city->flags, EntryFlags::Deleted));
        EXPECT_TRUE(any(city->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(country->flags, EntryFlags::Deleted));
        EXPECT_TRUE(any(country->flags, EntryFlags::Dirty));
    }

    TEST(MetadataXmpEditing, FailedLaterOperationDoesNotChangeOutput)
    {
        const MetadataAuthoringEntry added = authoring_text(
            kIptcExt, "LocationShown[1]/City", "First");
        const std::array<MetadataXmpEditingOperation, 2U> operations {
            make_operation(MetadataXmpEditingOperationKind::Add, kIptcExt,
                           "LocationShown[1]", { &added, 1U }),
            make_operation(MetadataXmpEditingOperationKind::Remove, kIptcExt,
                           "LocationShown[2]"),
        };
        MetaStore base;
        base.finalize();
        MetaStore output;
        add_sentinel(&output);

        const MetadataXmpEditingResult result
            = edit_metadata_xmp(base, operations, &output);

        EXPECT_EQ(result.status, MetadataXmpEditingStatus::ScopeNotFound);
        EXPECT_EQ(result.failed_operation_index, 1U);
        expect_sentinel(output);
    }

    TEST(MetadataXmpEditing, PreservesWireOriginAndFlagsForCommonKeys)
    {
        MetaStore base;
        const BlockId block = base.add_block(BlockInfo { 1U, 2U, 3U });
        const WireType original_wire { WireFamily::Tiff, 2U };
        add_xmp_text(&base, kIptcExt, "LocationShown[1]/City", "old", 17U,
                     EntryFlags::Derived, block, original_wire, 1U);
        add_xmp_text(&base, kIptcExt, "LocationShown[1]/CountryName", "old",
                     18U, EntryFlags::Derived, block, original_wire, 5U);
        base.finalize();

        const std::array<MetadataAuthoringEntry, 2U> replacement_entries {
            authoring_text(kIptcExt, "LocationShown[1]/City", "new"),
            authoring_text(kIptcExt, "LocationShown[1]/WorldRegion", "North"),
        };
        const MetadataXmpEditingOperation operation = make_operation(
            MetadataXmpEditingOperationKind::Replace, kIptcExt,
            "LocationShown[1]", replacement_entries);
        MetaStore edited;
        const MetadataXmpEditingResult result = edit_metadata_xmp(
            base, { &operation, 1U }, &edited);

        ASSERT_TRUE(result.ok())
            << metadata_xmp_editing_status_name(result.status);
        const Entry* city = find_any(edited, kIptcExt,
                                     "LocationShown[1]/City");
        const Entry* removed = find_any(edited, kIptcExt,
                                        "LocationShown[1]/CountryName");
        const Entry* added = find_any(edited, kIptcExt,
                                      "LocationShown[1]/WorldRegion");
        ASSERT_NE(city, nullptr);
        ASSERT_NE(removed, nullptr);
        ASSERT_NE(added, nullptr);
        EXPECT_EQ(city->origin.block, block);
        EXPECT_EQ(city->origin.order_in_block, 17U);
        EXPECT_EQ(city->origin.wire_type.family, WireFamily::Tiff);
        EXPECT_EQ(city->origin.wire_type.code, 2U);
        EXPECT_EQ(city->origin.wire_count, 1U);
        EXPECT_TRUE(any(city->flags, EntryFlags::Derived));
        EXPECT_TRUE(any(city->flags, EntryFlags::Dirty));
        EXPECT_FALSE(any(city->flags, EntryFlags::Deleted));
        EXPECT_EQ(removed->origin.block, block);
        EXPECT_EQ(removed->origin.order_in_block, 18U);
        EXPECT_EQ(removed->origin.wire_count, 5U);
        EXPECT_TRUE(any(removed->flags, EntryFlags::Derived));
        EXPECT_TRUE(any(removed->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(removed->flags, EntryFlags::Deleted));
        EXPECT_EQ(added->origin.block, kInvalidBlockId);
        EXPECT_EQ(added->origin.wire_type.family, WireFamily::None);
        EXPECT_EQ(added->origin.wire_count, 0U);
        EXPECT_TRUE(any(added->flags, EntryFlags::Dirty));
        EXPECT_FALSE(any(added->flags, EntryFlags::Deleted));
    }

    TEST(MetadataXmpEditing, LanguageSelectorsAreExactAndKeepSiblings)
    {
        const std::array<MetadataAuthoringEntry, 2U> initial {
            authoring_text(kDc, "title[@xml:lang=en]", "English"),
            authoring_text(kDc, "title[@xml:lang=fr]", "Francais"),
        };
        const MetadataAuthoringEntry english = authoring_text(
            kDc, "title[@xml:lang=en]", "Updated English");
        const std::array<MetadataXmpEditingOperation, 2U> operations {
            make_operation(MetadataXmpEditingOperationKind::Add, kDc, "title",
                           initial),
            make_operation(MetadataXmpEditingOperationKind::Replace, kDc,
                           "title[@xml:lang=en]", { &english, 1U }),
        };
        MetaStore base;
        base.finalize();
        MetaStore edited;

        const MetadataXmpEditingResult result
            = edit_metadata_xmp(base, operations, &edited);

        ASSERT_TRUE(result.ok())
            << metadata_xmp_editing_status_name(result.status);
        EXPECT_EQ(active_value_for(edited, kDc, "title[@xml:lang=en]"),
                  "Updated English");
        EXPECT_EQ(active_value_for(edited, kDc, "title[@xml:lang=fr]"),
                  "Francais");
    }

    TEST(MetadataXmpEditing, RejectsDuplicateInputAndBaseKeysTransactionally)
    {
        const std::array<MetadataAuthoringEntry, 2U> duplicate_input {
            authoring_text(kIptcExt, "LocationShown[1]/City", "one"),
            authoring_text(kIptcExt, "LocationShown[1]/City", "two"),
        };
        const MetadataXmpEditingOperation add = make_operation(
            MetadataXmpEditingOperationKind::Add, kIptcExt,
            "LocationShown[1]", duplicate_input);
        MetaStore empty;
        empty.finalize();
        MetaStore output;
        add_sentinel(&output);

        const MetadataXmpEditingResult input_result
            = edit_metadata_xmp(empty, { &add, 1U }, &output);

        EXPECT_EQ(input_result.status, MetadataXmpEditingStatus::DuplicateEntry);
        expect_sentinel(output);

        MetaStore duplicate_base;
        add_xmp_text(&duplicate_base, kIptcExt, "LocationShown[1]/City", "one",
                     0U);
        add_xmp_text(&duplicate_base, kIptcExt, "LocationShown[1]/City", "two",
                     1U);
        duplicate_base.finalize();
        const MetadataAuthoringEntry replacement = authoring_text(
            kIptcExt, "LocationShown[1]/City", "new");
        const MetadataXmpEditingOperation replace = make_operation(
            MetadataXmpEditingOperationKind::Replace, kIptcExt,
            "LocationShown[1]", { &replacement, 1U });
        MetaStore base_output;
        add_sentinel(&base_output);

        const MetadataXmpEditingResult base_result = edit_metadata_xmp(
            duplicate_base, { &replace, 1U }, &base_output);

        EXPECT_EQ(base_result.status, MetadataXmpEditingStatus::DuplicateEntry);
        expect_sentinel(base_output);
    }

    TEST(MetadataXmpEditing, RejectsMalformedOperationForms)
    {
        MetaStore base;
        base.finalize();
        const MetadataAuthoringEntry entry = authoring_text(
            kIptcExt, "LocationShown[1]/City", "value");
        MetaStore output;
        add_sentinel(&output);

        const MetadataXmpEditingOperation empty_add = make_operation(
            MetadataXmpEditingOperationKind::Add, kIptcExt,
            "LocationShown[1]");
        EXPECT_EQ(edit_metadata_xmp(base, { &empty_add, 1U }, &output).status,
                  MetadataXmpEditingStatus::EmptyEntries);
        expect_sentinel(output);

        const MetadataXmpEditingOperation remove_with_entries = make_operation(
            MetadataXmpEditingOperationKind::Remove, kIptcExt,
            "LocationShown[1]", { &entry, 1U });
        EXPECT_EQ(edit_metadata_xmp(base, { &remove_with_entries, 1U }, &output)
                      .status,
                  MetadataXmpEditingStatus::UnexpectedEntries);
        expect_sentinel(output);

        MetadataXmpEditingOperation invalid_kind = empty_add;
        invalid_kind.kind = static_cast<MetadataXmpEditingOperationKind>(255U);
        EXPECT_EQ(edit_metadata_xmp(base, { &invalid_kind, 1U }, &output).status,
                  MetadataXmpEditingStatus::InvalidOperation);
        expect_sentinel(output);

        const MetadataAuthoringEntry wrong_item = authoring_text(
            kIptcExt, "LocationShown[10]/City", "wrong item");
        const MetadataXmpEditingOperation wrong_scope = make_operation(
            MetadataXmpEditingOperationKind::Add, kIptcExt,
            "LocationShown[1]", { &wrong_item, 1U });
        EXPECT_EQ(edit_metadata_xmp(base, { &wrong_scope, 1U }, &output).status,
                  MetadataXmpEditingStatus::InvalidEntry);
        expect_sentinel(output);

        MetaStore present;
        add_xmp_text(&present, kIptcExt, "LocationShown[1]/City", "existing",
                     0U);
        present.finalize();
        const MetadataAuthoringEntry new_leaf = authoring_text(
            kIptcExt, "LocationShown[1]/CountryCode", "JP");
        const MetadataXmpEditingOperation add_existing = make_operation(
            MetadataXmpEditingOperationKind::Add, kIptcExt,
            "LocationShown[1]", { &new_leaf, 1U });
        EXPECT_EQ(edit_metadata_xmp(present, { &add_existing, 1U }, &output)
                      .status,
                  MetadataXmpEditingStatus::ScopeAlreadyExists);
        expect_sentinel(output);
    }

    TEST(MetadataXmpEditing, SupportsAliasedBaseOutput)
    {
        MetaStore store;
        add_xmp_text(&store, kIptcExt, "LocationShown[1]/City", "before", 0U);
        store.finalize();
        const MetadataAuthoringEntry replacement = authoring_text(
            kIptcExt, "LocationShown[1]/City", "after");
        const MetadataXmpEditingOperation operation = make_operation(
            MetadataXmpEditingOperationKind::Replace, kIptcExt,
            "LocationShown[1]", { &replacement, 1U });

        const MetadataXmpEditingResult result = edit_metadata_xmp(
            store, { &operation, 1U }, &store);

        ASSERT_TRUE(result.ok())
            << metadata_xmp_editing_status_name(result.status);
        EXPECT_EQ(active_value_for(store, kIptcExt,
                                   "LocationShown[1]/City"),
                  "after");
    }

    TEST(MetadataXmpEditing, RejectsNoncanonicalAliasesAndIndexOverflow)
    {
        const std::array<std::string_view, 5U> invalid_scopes {
            "Iptc4xmpExt:LocationShown[1]",
            "LocationShown[01]",
            "LocationShown[65536]",
            "LocationShown/Address/City",
            "LocationShown[1]/Address/City",
        };
        MetaStore base;
        base.finalize();
        for (const std::string_view scope : invalid_scopes) {
            const MetadataXmpEditingOperation operation = make_operation(
                MetadataXmpEditingOperationKind::Add, kIptcExt, scope,
                {});
            MetaStore output;
            add_sentinel(&output);

            const MetadataXmpEditingResult result = edit_metadata_xmp(
                base, { &operation, 1U }, &output);

            EXPECT_EQ(result.status, MetadataXmpEditingStatus::InvalidScope)
                << scope;
            expect_sentinel(output);
        }

        const std::array<std::string_view, 4U> invalid_leaves {
            "LocationShown[1]/Iptc4xmpExt:City",
            "LocationShown[1]/Identifier[1]",
            "LocationShown[1]/unregistered:City",
            "LocationShown[1]/Address/City",
        };
        for (const std::string_view path : invalid_leaves) {
            const MetadataAuthoringEntry entry
                = authoring_text(kIptcExt, path, "value");
            const MetadataXmpEditingOperation operation = make_operation(
                MetadataXmpEditingOperationKind::Add, kIptcExt,
                "LocationShown[1]", { &entry, 1U });
            MetaStore output;
            add_sentinel(&output);

            const MetadataXmpEditingResult result = edit_metadata_xmp(
                base, { &operation, 1U }, &output);

            EXPECT_EQ(result.status, MetadataXmpEditingStatus::UnsupportedPath)
                << path;
            expect_sentinel(output);
        }
    }

    TEST(MetadataXmpEditing, AcceptsMaximumArrayIndexAndRejectsNextIndex)
    {
        MetaStore base;
        base.finalize();
        const MetadataAuthoringEntry last = authoring_text(
            kIptcExt, "LocationShown[65535]/City", "last");
        const MetadataXmpEditingOperation accepted = make_operation(
            MetadataXmpEditingOperationKind::Add, kIptcExt,
            "LocationShown[65535]", { &last, 1U });
        MetaStore output;

        const MetadataXmpEditingResult accepted_result
            = edit_metadata_xmp(base, { &accepted, 1U }, &output);

        ASSERT_TRUE(accepted_result.ok())
            << metadata_xmp_editing_status_name(accepted_result.status);
        EXPECT_EQ(active_value_for(output, kIptcExt,
                                   "LocationShown[65535]/City"),
                  "last");
    }

    TEST(MetadataXmpEditing, EnforcesOptionsAndPreparationBounds)
    {
        MetaStore base;
        base.finalize();
        MetaStore output;
        add_sentinel(&output);
        MetadataXmpEditingOptions invalid_options;
        invalid_options.max_operations = 0U;
        const MetadataXmpEditingResult invalid_result
            = edit_metadata_xmp(base, {}, &output, invalid_options);
        EXPECT_EQ(invalid_result.status, MetadataXmpEditingStatus::InvalidOptions);
        expect_sentinel(output);

        MetadataXmpEditingOptions above_hard_limit;
        above_hard_limit.max_scope_entries
            = kMetadataXmpEditingMaxScopeEntries + 1U;
        const MetadataXmpEditingResult hard_limit_result
            = edit_metadata_xmp(base, {}, &output, above_hard_limit);
        EXPECT_EQ(hard_limit_result.status,
                  MetadataXmpEditingStatus::InvalidOptions);
        expect_sentinel(output);

        const std::array<MetadataXmpEditingOperation, 2U> operations {
            make_operation(MetadataXmpEditingOperationKind::Remove, kIptcExt,
                           "LocationShown[1]"),
            make_operation(MetadataXmpEditingOperationKind::Remove, kIptcExt,
                           "LocationShown[2]"),
        };
        MetadataXmpEditingOptions one_operation;
        one_operation.max_operations = 1U;
        const MetadataXmpEditingResult too_many = edit_metadata_xmp(
            base, operations, &output, one_operation);
        EXPECT_EQ(too_many.status, MetadataXmpEditingStatus::TooManyOperations);
        expect_sentinel(output);

        const std::array<MetadataAuthoringEntry, 2U> two_leaves {
            authoring_text(kIptcExt, "LocationShown[1]/City", "city"),
            authoring_text(kIptcExt, "LocationShown[1]/CountryCode", "JP"),
        };
        const MetadataXmpEditingOperation add = make_operation(
            MetadataXmpEditingOperationKind::Add, kIptcExt,
            "LocationShown[1]", two_leaves);
        MetadataXmpEditingOptions one_expansion;
        one_expansion.max_expanded_operations = 1U;
        const MetadataXmpEditingResult expansion_limit = edit_metadata_xmp(
            base, { &add, 1U }, &output, one_expansion);
        EXPECT_EQ(expansion_limit.status, MetadataXmpEditingStatus::LimitExceeded);
        expect_sentinel(output);

        MetadataXmpEditingOptions one_scope_entry;
        one_scope_entry.max_scope_entries = 1U;
        const MetadataXmpEditingResult input_limit = edit_metadata_xmp(
            base, { &add, 1U }, &output, one_scope_entry);
        EXPECT_EQ(input_limit.status, MetadataXmpEditingStatus::LimitExceeded);
        expect_sentinel(output);

        MetaStore represented_items;
        add_xmp_text(&represented_items, kIptcExt,
                     "LocationShown[1]/City", "one", 0U);
        add_xmp_text(&represented_items, kIptcExt,
                     "LocationShown[10]/City", "ten", 1U);
        represented_items.finalize();
        const MetadataAuthoringEntry replace_entry = authoring_text(
            kIptcExt, "LocationShown[1]/City", "updated");
        const MetadataXmpEditingOperation replace = make_operation(
            MetadataXmpEditingOperationKind::Replace, kIptcExt,
            "LocationShown[1]", { &replace_entry, 1U });
        const MetadataXmpEditingResult tracked_limit = edit_metadata_xmp(
            represented_items, { &replace, 1U }, &output, one_scope_entry);
        EXPECT_EQ(tracked_limit.status,
                  MetadataXmpEditingStatus::LimitExceeded);
        expect_sentinel(output);

        MetaStore represented_roots;
        add_xmp_text(&represented_roots, kIptcCore,
                     "LocationCreated/City", "Kyoto", 0U);
        add_xmp_text(&represented_roots, kDc,
                     "title[@xml:lang=en]", "Title", 1U);
        represented_roots.finalize();
        const MetadataAuthoringEntry core_replacement = authoring_text(
            kIptcCore, "LocationCreated/City", "Osaka");
        const MetadataAuthoringEntry dc_replacement = authoring_text(
            kDc, "title[@xml:lang=en]", "Name");
        const std::array<MetadataXmpEditingOperation, 2U> root_replacements {
            make_operation(MetadataXmpEditingOperationKind::Replace, kIptcCore,
                           "LocationCreated", { &core_replacement, 1U }),
            make_operation(MetadataXmpEditingOperationKind::Replace, kDc,
                           "title", { &dc_replacement, 1U }),
        };
        const MetadataXmpEditingResult roots_limit = edit_metadata_xmp(
            represented_roots, root_replacements, &output, one_scope_entry);
        EXPECT_EQ(roots_limit.status, MetadataXmpEditingStatus::LimitExceeded);
        expect_sentinel(output);
    }

    TEST(MetadataXmpEditing, RequestByteLimitPrecedesScanningLaterPayloads)
    {
        const std::string_view path = "LocationShown[1]/City";
        const MetadataAuthoringEntry initial
            = authoring_text(kIptcExt, path, "a");
        const char invalid_utf8_byte = static_cast<char>(0xffU);
        const MetadataAuthoringEntry invalid_replacement
            = authoring_text(kIptcExt, path,
                             std::string_view(&invalid_utf8_byte, 1U));
        const std::array<MetadataXmpEditingOperation, 2U> operations {
            make_operation(MetadataXmpEditingOperationKind::Add, kIptcExt,
                           "LocationShown[1]", { &initial, 1U }),
            make_operation(MetadataXmpEditingOperationKind::Replace, kIptcExt,
                           "LocationShown[1]", { &invalid_replacement, 1U }),
        };
        MetaStore base;
        base.finalize();
        MetaStore output;
        add_sentinel(&output);

        MetadataXmpEditingOptions limited;
        limited.typed.max_request_bytes
            = static_cast<uint64_t>(kIptcExt.size() + path.size() + 1U);
        const MetadataXmpEditingResult limited_result = edit_metadata_xmp(
            base, operations, &output, limited);

        EXPECT_EQ(limited_result.status, MetadataXmpEditingStatus::LimitExceeded);
        EXPECT_EQ(limited_result.failed_operation_index, 1U);
        expect_sentinel(output);

        const MetadataXmpEditingResult invalid_result
            = edit_metadata_xmp(base, operations, &output);
        EXPECT_EQ(invalid_result.status, MetadataXmpEditingStatus::InvalidEntry);
        EXPECT_EQ(invalid_result.failed_operation_index, 1U);
        expect_sentinel(output);
    }

    TEST(MetadataXmpEditing, EmptyTextIsRepresentable)
    {
        MetaStore base;
        base.finalize();
        const MetadataAuthoringEntry empty = authoring_text(
            kDc, "title[@xml:lang=en]", "");
        const MetadataXmpEditingOperation operation = make_operation(
            MetadataXmpEditingOperationKind::Add, kDc, "title",
            { &empty, 1U });
        MetaStore edited;

        const MetadataXmpEditingResult result = edit_metadata_xmp(
            base, { &operation, 1U }, &edited);

        ASSERT_TRUE(result.ok())
            << metadata_xmp_editing_status_name(result.status);
        const Entry* entry = find_any(edited, kDc,
                                      "title[@xml:lang=en]");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->value.kind, MetaValueKind::Text);
        EXPECT_EQ(entry->value.count, 0U);
    }

}  // namespace
}  // namespace openmeta
