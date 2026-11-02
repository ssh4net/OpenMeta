// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/xmp_structure.h"

#include "../src/openmeta/xmp_structure_internal.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace openmeta {
namespace {

    using detail::XmpStructureBuildResult;
    using detail::XmpStructureStatus;
    using detail::XmpStructureTree;

    static constexpr std::string_view kNsOne = "urn:test:one";
    static constexpr std::string_view kNsTwo = "urn:test:two";

    static void add_container(MetaStore* store, BlockId block,
                              std::string_view schema_ns, std::string_view path,
                              XmpContainerKind kind,
                              EntryFlags flags = EntryFlags::None)
    {
        const MetadataAuthoringEntry request
            = make_xmp_container_entry(schema_ns, path, kind);
        Entry entry;
        entry.key = make_xmp_property_key(store->arena(), schema_ns, path);
        entry.origin.block      = block;
        entry.origin.wire_type  = request.wire_type;
        entry.origin.wire_count = request.wire_count;
        entry.flags             = flags;
        (void)store->add_entry(entry);
    }

    static void add_text(MetaStore* store, BlockId block,
                         std::string_view schema_ns, std::string_view path,
                         std::string_view text,
                         EntryFlags flags = EntryFlags::None)
    {
        Entry entry;
        entry.key   = make_xmp_property_key(store->arena(), schema_ns, path);
        entry.value = make_text(store->arena(), text, TextEncoding::Utf8);
        entry.origin.block      = block;
        entry.origin.wire_type  = WireType { WireFamily::Other, 0U };
        entry.origin.wire_count = static_cast<uint32_t>(text.size());
        entry.flags             = flags;
        (void)store->add_entry(entry);
    }

    static MetaStore make_valid_store()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Record",
                      XmpContainerKind::Struct);
        add_container(&store, block, kNsOne, "Record/Inner",
                      XmpContainerKind::Struct);
        add_text(&store, block, kNsOne,
                 "Record/Inner/nsu_75726e3a6e657374656431:Name", "one");
        add_text(&store, block, kNsOne,
                 "Record/Inner/nsu_75726e3a6e657374656432:Name", "two");
        add_container(&store, block, kNsOne, "Record/Inner/levels",
                      XmpContainerKind::Seq);
        add_container(&store, block, kNsOne, "Record/Inner/levels[1]",
                      XmpContainerKind::Struct);
        add_container(&store, block, kNsOne, "Record/Inner/levels[1]/values",
                      XmpContainerKind::Seq);
        add_text(&store, block, kNsOne, "Record/Inner/levels[1]/values[1]",
                 "nested one");
        add_text(&store, block, kNsOne, "Record/Inner/levels[1]/values[2]",
                 "nested two");
        add_container(&store, block, kNsOne, "Record/items",
                      XmpContainerKind::Bag);
        add_text(&store, block, kNsOne, "Record/items[1]", "first");
        add_text(&store, block, kNsOne, "Record/items[2]", "second");
        add_container(&store, block, kNsOne, "Record/title",
                      XmpContainerKind::LangAlt);
        add_text(&store, block, kNsOne, "Record/title[@xml:lang=x-default]",
                 "default");
        add_text(&store, block, kNsOne, "Record/title[@xml:lang=en-US]",
                 "English");
        add_container(&store, block, kNsOne, "Record/empty",
                      XmpContainerKind::Struct);
        add_container(&store, block, kNsOne, "Record/emptyBag",
                      XmpContainerKind::Bag);
        add_container(&store, block, kNsOne, "Record/emptySeq",
                      XmpContainerKind::Seq);
        add_container(&store, block, kNsTwo, "Record",
                      XmpContainerKind::Struct);
        add_text(&store, block, kNsTwo, "Record/Value", "other URI");
        store.finalize();
        return store;
    }

    static XmpStructureBuildResult
    prepare(MetaStore& store, XmpStructureTree* tree,
            const XmpStructureLimits& limits = XmpStructureLimits {})
    {
        return detail::prepare_xmp_structure_tree(store, limits, tree);
    }

    static bool has_path(const XmpStructureTree& tree, std::string_view path,
                         XmpContainerKind kind) noexcept
    {
        for (const detail::XmpStructureNode& node : tree.nodes) {
            if (node.path == path && node.container_kind == kind)
                return true;
        }
        return false;
    }

}  // namespace

TEST(XmpStructure, PublicMarkersDistinguishBagAndSeq)
{
    const MetadataAuthoringEntry bag
        = make_xmp_container_entry(kNsOne, "Items", XmpContainerKind::Bag);
    const MetadataAuthoringEntry sequence
        = make_xmp_container_entry(kNsOne, "Items", XmpContainerKind::Seq);
    EXPECT_EQ(bag.value.kind, MetaValueKind::Empty);
    EXPECT_EQ(bag.value.count, 0U);
    EXPECT_EQ(bag.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(bag.value.text_encoding, TextEncoding::Unknown);
    EXPECT_EQ(bag.wire_type.family, WireFamily::Other);
    EXPECT_EQ(bag.wire_type.code, 0x5842U);
    EXPECT_EQ(sequence.wire_type.code, 0x5843U);
    EXPECT_TRUE(detail::xmp_container_view_shape_is_valid(bag));
    MetadataAuthoringEntry malformed_bag = bag;
    malformed_bag.wire_count             = 1U;
    EXPECT_FALSE(detail::xmp_container_view_shape_is_valid(malformed_bag));

    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    add_container(&store, block, kNsOne, "Items", XmpContainerKind::Bag);
    add_container(&store, block, kNsOne, "Sequence", XmpContainerKind::Seq);
    store.finalize();
    EXPECT_EQ(xmp_container_kind(store.entry(0U)), XmpContainerKind::Bag);
    EXPECT_EQ(xmp_container_kind(store.entry(1U)), XmpContainerKind::Seq);
    EXPECT_TRUE(detail::xmp_container_entry_shape_is_valid(store.entry(0U)));
}

TEST(XmpStructure,
     MarkerFreeStoresStayOnTheLegacyPathAndStandardRootsRejectMarkers)
{
    MetaStore legacy;
    const BlockId legacy_block = legacy.add_block(BlockInfo {});
    add_text(&legacy, legacy_block, "urn:test:legacy", "Simple", "value");
    legacy.finalize();
    XmpStructureTree tree;
    EXPECT_EQ(prepare(legacy, &tree).status, XmpStructureStatus::Ok);
    EXPECT_TRUE(tree.nodes.empty());

    MetaStore standard;
    const BlockId standard_block = standard.add_block(BlockInfo {});
    add_container(&standard, standard_block, "http://ns.adobe.com/xap/1.0/",
                  "Root", XmpContainerKind::Struct);
    standard.finalize();
    EXPECT_EQ(prepare(standard, &tree).status,
              XmpStructureStatus::InvalidMetadata);
}

TEST(XmpStructure, ReservedNumbersRequireXmpOtherWireForm)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry exif_numeric_hint;
    exif_numeric_hint.key   = make_exif_tag_key(store.arena(), "ifd0", 0x010eU);
    exif_numeric_hint.value = make_text(store.arena(), "legacy",
                                        TextEncoding::Utf8);
    exif_numeric_hint.origin.block     = block;
    exif_numeric_hint.origin.wire_type = WireType { WireFamily::Other,
                                                    0x5841U };
    (void)store.add_entry(exif_numeric_hint);

    Entry xmp_non_other;
    xmp_non_other.key   = make_xmp_property_key(store.arena(), kNsOne, "Root");
    xmp_non_other.value = make_text(store.arena(), "legacy",
                                    TextEncoding::Utf8);
    xmp_non_other.origin.block     = block;
    xmp_non_other.origin.wire_type = WireType { WireFamily::Tiff, 0x5841U };
    (void)store.add_entry(xmp_non_other);

    store.finalize();
    EXPECT_FALSE(detail::xmp_structure_is_declaration(store.entry(0U)));
    EXPECT_FALSE(detail::xmp_structure_is_declaration(store.entry(1U)));
    EXPECT_EQ(xmp_container_kind(store.entry(0U)), XmpContainerKind::None);
    EXPECT_EQ(xmp_container_kind(store.entry(1U)), XmpContainerKind::None);

    XmpStructureTree tree;
    EXPECT_EQ(prepare(store, &tree).status, XmpStructureStatus::Ok);
    EXPECT_TRUE(tree.nodes.empty());
}

TEST(XmpStructure, BuildsNestedContainersAndKeepsUriIdentity)
{
    MetaStore store = make_valid_store();
    XmpStructureTree tree;
    const XmpStructureBuildResult result = prepare(store, &tree);
    ASSERT_EQ(result.status, XmpStructureStatus::Ok);
    EXPECT_EQ(result.failed_entry, kInvalidEntryId);
    EXPECT_EQ(tree.nodes.size(), 20U);
    EXPECT_EQ(tree.namespaces.size(), 4U);
    EXPECT_TRUE(has_path(tree, "Record/items", XmpContainerKind::Bag));
    EXPECT_TRUE(has_path(tree, "Record/title", XmpContainerKind::LangAlt));
    EXPECT_TRUE(has_path(tree, "Record/empty", XmpContainerKind::Struct));
    EXPECT_TRUE(has_path(tree, "Record/emptyBag", XmpContainerKind::Bag));
    EXPECT_TRUE(has_path(tree, "Record/emptySeq", XmpContainerKind::Seq));
    EXPECT_TRUE(
        has_path(tree, "Record/Inner/levels[1]/values", XmpContainerKind::Seq));

    uint32_t title_node = detail::kInvalidXmpStructureNode;
    for (size_t i = 0U; i < tree.nodes.size(); ++i)
        if (tree.nodes[i].path == "Record/title")
            title_node = static_cast<uint32_t>(i);
    ASSERT_NE(title_node, detail::kInvalidXmpStructureNode);
    ASSERT_FALSE(tree.ordered_children.empty());
    size_t first_title_child = tree.ordered_children.size();
    for (size_t i = 0U; i < tree.ordered_children.size(); ++i)
        if (tree.ordered_children[i].parent_node == title_node) {
            first_title_child = i;
            break;
        }
    ASSERT_LT(first_title_child, tree.ordered_children.size());
    EXPECT_TRUE(detail::xmp_structure_language_is_default(
        tree.ordered_children[first_title_child].language));

    std::string previous_uri;
    std::array<char, detail::kXmpStructureMaxUriBytes> uri_storage {};
    for (const detail::XmpStructureNamespace& name_space : tree.namespaces) {
        std::string_view uri;
        ASSERT_TRUE(detail::xmp_structure_namespace_uri(
            name_space.ref, uri_storage.data(), uri_storage.size(), &uri));
        if (!previous_uri.empty())
            EXPECT_TRUE(std::string_view(previous_uri) < uri);
        EXPECT_TRUE(uri == kNsOne || uri == kNsTwo || uri == "urn:nested1"
                    || uri == "urn:nested2");
        previous_uri.assign(uri);
    }

    uint32_t item_one_parent = detail::kInvalidXmpStructureNode;
    uint32_t item_two_parent = detail::kInvalidXmpStructureNode;
    for (size_t i = 0U; i < tree.nodes.size(); ++i) {
        if (tree.nodes[i].path == "Record/items[1]")
            item_one_parent = tree.nodes[i].parent_node;
        if (tree.nodes[i].path == "Record/items[2]")
            item_two_parent = tree.nodes[i].parent_node;
    }
    EXPECT_NE(item_one_parent, detail::kInvalidXmpStructureNode);
    EXPECT_EQ(item_one_parent, item_two_parent);
}

TEST(XmpStructure, ParserRejectsNoncanonicalSelectorsAndExcessDepth)
{
    detail::XmpStructurePath path;
    EXPECT_TRUE(
        detail::xmp_structure_parse_path("Root/items[1]/name[@xml:lang=en-US]",
                                         8U, 65535U, &path));
    EXPECT_EQ(path.depth, 3U);
    EXPECT_EQ(path.expanded_depth, 5U);
    EXPECT_FALSE(
        detail::xmp_structure_parse_path("Root/items[01]", 8U, 65535U, &path));
    EXPECT_FALSE(
        detail::xmp_structure_parse_path("Root/items[@xml:lang=en--US]", 8U,
                                         65535U, &path));
    EXPECT_FALSE(detail::xmp_structure_parse_path("Root/unknown:field", 8U,
                                                  65535U, &path));
    EXPECT_FALSE(detail::xmp_structure_parse_path("Root/a/b/c/d/e/f/g/h/i", 8U,
                                                  65535U, &path));
    ASSERT_TRUE(detail::xmp_structure_parse_path("Root/Matrix[1][2]", 8U,
                                                 65535U, &path));
    EXPECT_EQ(path.depth, 2U);
    EXPECT_EQ(path.expanded_depth, 4U);
    ASSERT_EQ(path.segments[1].selector_count, 2U);
    EXPECT_EQ(path.segments[1].selectors[0].index, 1U);
    EXPECT_EQ(path.segments[1].selectors[1].index, 2U);
    EXPECT_FALSE(
        detail::xmp_structure_parse_path("Root/items[@xml:lang=en]/child", 8U,
                                         65535U, &path));
}

TEST(XmpStructure, RejectsDuplicatesOrphansAndMixedArrayItems)
{
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct);
        add_text(&store, block, kNsOne, "Root/name", "first");
        add_text(&store, block, kNsOne, "Root/name", "duplicate");
        store.finalize();
        XmpStructureTree tree;
        EXPECT_EQ(prepare(store, &tree).status,
                  XmpStructureStatus::InvalidMetadata);
    }
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct);
        add_text(&store, block, kNsOne, "Root/missing/leaf", "orphan");
        store.finalize();
        XmpStructureTree tree;
        EXPECT_EQ(prepare(store, &tree).status,
                  XmpStructureStatus::InvalidMetadata);
    }
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct);
        add_container(&store, block, kNsOne, "Root/items",
                      XmpContainerKind::Seq);
        add_text(&store, block, kNsOne, "Root/items[1]", "scalar");
        add_container(&store, block, kNsOne, "Root/items[2]",
                      XmpContainerKind::Struct);
        add_text(&store, block, kNsOne, "Root/items[2]/name", "structured");
        store.finalize();
        XmpStructureTree tree;
        EXPECT_EQ(prepare(store, &tree).status,
                  XmpStructureStatus::InvalidMetadata);
    }
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct);
        add_container(&store, block, kNsOne, "Root/items",
                      XmpContainerKind::Seq);
        add_container(&store, block, kNsOne, "Root/items[1]",
                      XmpContainerKind::Bag);
        add_container(&store, block, kNsOne, "Root/items[2]",
                      XmpContainerKind::Seq);
        store.finalize();
        XmpStructureTree tree;
        EXPECT_EQ(prepare(store, &tree).status,
                  XmpStructureStatus::InvalidMetadata);
    }
}

TEST(XmpStructure, AllowsSparseIndicesAndOrdersThemNumerically)
{
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct);
        add_container(&store, block, kNsOne, "Root/items",
                      XmpContainerKind::Bag);
        add_text(&store, block, kNsOne, "Root/items[1]", "one");
        add_text(&store, block, kNsOne, "Root/items[3]", "three");
        add_text(&store, block, kNsOne, "Root/items[10]", "ten");
        store.finalize();
        XmpStructureTree tree;
        ASSERT_EQ(prepare(store, &tree).status, XmpStructureStatus::Ok);
        uint32_t items_node = detail::kInvalidXmpStructureNode;
        for (size_t i = 0U; i < tree.nodes.size(); ++i)
            if (tree.nodes[i].path == "Root/items")
                items_node = static_cast<uint32_t>(i);
        ASSERT_NE(items_node, detail::kInvalidXmpStructureNode);
        std::array<std::string_view, 3U> ordered_paths {};
        size_t ordered_count = 0U;
        for (const detail::XmpStructureChildRef& child :
             tree.ordered_children) {
            if (child.parent_node != items_node)
                continue;
            ASSERT_LT(ordered_count, ordered_paths.size());
            ordered_paths[ordered_count++] = tree.nodes[child.node_index].path;
        }
        ASSERT_EQ(ordered_count, 3U);
        EXPECT_EQ(ordered_paths[0], "Root/items[1]");
        EXPECT_EQ(ordered_paths[1], "Root/items[3]");
        EXPECT_EQ(ordered_paths[2], "Root/items[10]");
    }
}

TEST(XmpStructure, SupportsDirectNestedArraySelectors)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct);
    add_container(&store, block, kNsOne, "Root/Matrix", XmpContainerKind::Seq);
    add_container(&store, block, kNsOne, "Root/Matrix[1]",
                  XmpContainerKind::Bag);
    add_text(&store, block, kNsOne, "Root/Matrix[1][2]", "cell");
    store.finalize();
    XmpStructureTree tree;
    ASSERT_EQ(prepare(store, &tree).status, XmpStructureStatus::Ok);
    uint32_t matrix_item = detail::kInvalidXmpStructureNode;
    uint32_t cell        = detail::kInvalidXmpStructureNode;
    for (size_t i = 0U; i < tree.nodes.size(); ++i) {
        if (tree.nodes[i].path == "Root/Matrix[1]")
            matrix_item = static_cast<uint32_t>(i);
        if (tree.nodes[i].path == "Root/Matrix[1][2]")
            cell = static_cast<uint32_t>(i);
    }
    ASSERT_NE(matrix_item, detail::kInvalidXmpStructureNode);
    ASSERT_NE(cell, detail::kInvalidXmpStructureNode);
    EXPECT_EQ(tree.nodes[cell].parent_node, matrix_item);
}

TEST(XmpStructure, RejectsAliasCollisionsAndCaseFoldedLangAltDuplicates)
{
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct);
        add_container(&store, block, kNsOne, "Root/Child",
                      XmpContainerKind::Struct);
        add_text(&store, block, kNsOne, "Root/Child/xmp:Name", "one");
        add_text(
            &store, block, kNsOne,
            "Root/Child/nsu_687474703a2f2f6e732e61646f62652e636f6d2f7861702f312e302f:Name",
            "same expanded name");
        store.finalize();
        XmpStructureTree tree;
        EXPECT_EQ(prepare(store, &tree).status,
                  XmpStructureStatus::InvalidMetadata);
    }
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct);
        add_container(&store, block, kNsOne, "Root/title",
                      XmpContainerKind::LangAlt);
        add_text(&store, block, kNsOne, "Root/title[@xml:lang=en-US]", "one");
        add_text(&store, block, kNsOne, "Root/title[@xml:lang=EN-us]",
                 "duplicate language");
        store.finalize();
        XmpStructureTree tree;
        EXPECT_EQ(prepare(store, &tree).status,
                  XmpStructureStatus::InvalidMetadata);
    }
}

TEST(XmpStructure, AppliesLimitsAndRejectsInvalidReservedForms)
{
    MetaStore store = make_valid_store();
    XmpStructureTree tree;
    XmpStructureLimits limits;
    limits.max_nodes = 2U;
    EXPECT_EQ(prepare(store, &tree, limits).status,
              XmpStructureStatus::LimitExceeded);
    limits           = XmpStructureLimits {};
    limits.max_depth = 2U;
    EXPECT_EQ(prepare(store, &tree, limits).status,
              XmpStructureStatus::LimitExceeded);
    limits                   = XmpStructureLimits {};
    limits.max_request_bytes = 1U;
    EXPECT_EQ(prepare(store, &tree, limits).status,
              XmpStructureStatus::LimitExceeded);
    limits                = XmpStructureLimits {};
    limits.max_namespaces = 1U;
    EXPECT_EQ(prepare(store, &tree, limits).status,
              XmpStructureStatus::LimitExceeded);
    limits                = XmpStructureLimits {};
    limits.max_path_bytes = 5U;
    EXPECT_EQ(prepare(store, &tree, limits).status,
              XmpStructureStatus::LimitExceeded);
    limits                     = XmpStructureLimits {};
    limits.max_namespace_bytes = 4U;
    EXPECT_EQ(prepare(store, &tree, limits).status,
              XmpStructureStatus::LimitExceeded);
    limits                 = XmpStructureLimits {};
    limits.max_array_index = 1U;
    EXPECT_EQ(prepare(store, &tree, limits).status,
              XmpStructureStatus::LimitExceeded);
    limits           = XmpStructureLimits {};
    limits.max_nodes = 0U;
    EXPECT_EQ(prepare(store, &tree, limits).status,
              XmpStructureStatus::InvalidOptions);

    MetaStore invalid;
    const BlockId block = invalid.add_block(BlockInfo {});
    MetadataAuthoringEntry request
        = make_xmp_container_entry(kNsOne, "Root", XmpContainerKind::Struct);
    request.wire_count = 1U;
    Entry marker;
    marker.key = make_xmp_property_key(invalid.arena(), kNsOne, "Root");
    marker.origin.block      = block;
    marker.origin.wire_type  = request.wire_type;
    marker.origin.wire_count = request.wire_count;
    (void)invalid.add_entry(marker);
    invalid.finalize();
    XmpStructureTree invalid_tree;
    EXPECT_EQ(prepare(invalid, &invalid_tree).status,
              XmpStructureStatus::InvalidMetadata);

    MetaStore invalid_reserved_code;
    const BlockId reserved_block = invalid_reserved_code.add_block(
        BlockInfo {});
    const MetadataAuthoringEntry invalid_kind
        = make_xmp_container_entry(kNsOne, "Root", XmpContainerKind::None);
    Entry reserved_marker;
    reserved_marker.key = make_xmp_property_key(invalid_reserved_code.arena(),
                                                kNsOne, "Root");
    reserved_marker.origin.block     = reserved_block;
    reserved_marker.origin.wire_type = invalid_kind.wire_type;
    (void)invalid_reserved_code.add_entry(reserved_marker);
    invalid_reserved_code.finalize();
    EXPECT_EQ(prepare(invalid_reserved_code, &invalid_tree).status,
              XmpStructureStatus::InvalidMetadata);
}

TEST(XmpStructure, CountsRetainedDeclarationsBeforeParsingTheirKeys)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct,
                  EntryFlags::Deleted);
    add_container(&store, block, kNsOne, "bad:prefix/Root",
                  XmpContainerKind::Struct, EntryFlags::Deleted);
    store.finalize();

    XmpStructureLimits limits;
    limits.max_nodes = 1U;
    XmpStructureTree tree;
    EXPECT_EQ(prepare(store, &tree, limits).status,
              XmpStructureStatus::LimitExceeded);
    limits                   = XmpStructureLimits {};
    limits.max_request_bytes = 1U;
    EXPECT_EQ(prepare(store, &tree, limits).status,
              XmpStructureStatus::LimitExceeded);

    MetaStore leaf_budget;
    const BlockId leaf_block = leaf_budget.add_block(BlockInfo {});
    add_container(&leaf_budget, leaf_block, kNsOne, "Root",
                  XmpContainerKind::Struct);
    add_text(&leaf_budget, leaf_block, kNsOne, "Root/value", "leaf");
    leaf_budget.finalize();
    limits           = XmpStructureLimits {};
    limits.max_nodes = 1U;
    EXPECT_EQ(prepare(leaf_budget, &tree, limits).status,
              XmpStructureStatus::LimitExceeded);
}

TEST(XmpStructure, RejectsUnreadableLiveNodesAndLeaves)
{
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct,
                      EntryFlags::Truncated);
        store.finalize();
        XmpStructureTree tree;
        EXPECT_EQ(prepare(store, &tree).status,
                  XmpStructureStatus::InvalidMetadata);
    }
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct);
        add_text(&store, block, kNsOne, "Root/name", "value",
                 EntryFlags::Unreadable);
        store.finalize();
        XmpStructureTree tree;
        EXPECT_EQ(prepare(store, &tree).status,
                  XmpStructureStatus::InvalidMetadata);
    }
}

TEST(XmpStructure,
     ValidatesDecodedNamespaceUrisAndAllowsUppercasePercentEscapes)
{
    EXPECT_TRUE(
        detail::xmp_structure_namespace_is_custom("urn:test:UP%2Fpart"));
    EXPECT_FALSE(
        detail::xmp_structure_namespace_is_custom("urn:test:bad%2Gpart"));

    for (const std::string_view bad_prefix :
         { "nsu_75726e3a746573743ac0af:Bad",
           "nsu_75726e3a746573743a0078:Bad" }) {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct);
        std::string path = "Root/";
        path.append(bad_prefix.data(), bad_prefix.size());
        add_text(&store, block, kNsOne, path, "invalid URI");
        store.finalize();
        XmpStructureTree tree;
        EXPECT_EQ(prepare(store, &tree).status,
                  XmpStructureStatus::InvalidMetadata);
    }
}

TEST(XmpStructure,
     PreparePublishesOnlySuccessfulTreesAndAllowsUnfinalizedStores)
{
    MetaStore valid;
    const BlockId valid_block = valid.add_block(BlockInfo {});
    add_container(&valid, valid_block, kNsOne, "Root",
                  XmpContainerKind::Struct);
    add_text(&valid, valid_block, kNsOne, "Root/name", "value");

    XmpStructureTree tree;
    ASSERT_EQ(prepare(valid, &tree).status, XmpStructureStatus::Ok);
    ASSERT_FALSE(tree.nodes.empty());
    const size_t old_node_count           = tree.nodes.size();
    const size_t old_namespace_count      = tree.namespaces.size();
    const size_t old_child_count          = tree.ordered_children.size();
    const std::string_view old_first_path = tree.nodes[0].path;

    MetaStore invalid;
    const BlockId invalid_block = invalid.add_block(BlockInfo {});
    add_container(&invalid, invalid_block, kNsOne, "Root",
                  XmpContainerKind::Struct);
    add_text(&invalid, invalid_block, kNsOne, "Root/name", "one");
    add_text(&invalid, invalid_block, kNsOne, "Root/name", "duplicate");
    invalid.finalize();
    EXPECT_EQ(prepare(invalid, &tree).status,
              XmpStructureStatus::InvalidMetadata);
    EXPECT_EQ(tree.nodes.size(), old_node_count);
    EXPECT_EQ(tree.namespaces.size(), old_namespace_count);
    EXPECT_EQ(tree.ordered_children.size(), old_child_count);
    EXPECT_EQ(tree.nodes[0].path, old_first_path);

    MetaStore marker_free;
    const BlockId legacy_block = marker_free.add_block(BlockInfo {});
    add_text(&marker_free, legacy_block, kNsOne, "Simple", "legacy");
    EXPECT_EQ(prepare(marker_free, &tree).status, XmpStructureStatus::Ok);
    EXPECT_TRUE(tree.nodes.empty());
    EXPECT_TRUE(tree.namespaces.empty());
    EXPECT_TRUE(tree.ordered_children.empty());

    ASSERT_EQ(prepare(valid, &tree).status, XmpStructureStatus::Ok);
    MetaStore deleted_scope;
    const BlockId deleted_block = deleted_scope.add_block(BlockInfo {});
    add_container(&deleted_scope, deleted_block, kNsOne, "Gone",
                  XmpContainerKind::Struct, EntryFlags::Deleted);
    add_text(&deleted_scope, deleted_block, kNsOne, "Gone/value", "gone",
             EntryFlags::Deleted);
    deleted_scope.finalize();
    EXPECT_EQ(prepare(deleted_scope, &tree).status, XmpStructureStatus::Ok);
    EXPECT_TRUE(tree.nodes.empty());
    EXPECT_TRUE(tree.namespaces.empty());
    EXPECT_TRUE(tree.ordered_children.empty());
}

TEST(XmpStructure, DeletedRootDoesNotHideSurvivingChildren)
{
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct,
                      EntryFlags::Deleted);
        add_text(&store, block, kNsOne, "Root/name", "orphan");
        store.finalize();
        XmpStructureTree tree;
        EXPECT_EQ(prepare(store, &tree).status,
                  XmpStructureStatus::InvalidMetadata);
    }
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct,
                      EntryFlags::Deleted);
        add_text(&store, block, kNsOne, "Root/name", "gone",
                 EntryFlags::Deleted);
        store.finalize();
        XmpStructureTree tree;
        const XmpStructureBuildResult result = prepare(store, &tree);
        EXPECT_EQ(result.status, XmpStructureStatus::Ok);
        EXPECT_TRUE(tree.nodes.empty());
        EXPECT_TRUE(tree.namespaces.empty());
    }
}

TEST(XmpStructure, DeletedNestedIntentIsRootScopedAcrossSameUri)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    add_container(&store, block, kNsOne, "RootA", XmpContainerKind::Struct);
    add_text(&store, block, kNsOne, "RootA/name", "valid sibling root");
    add_container(&store, block, kNsOne, "RootB/Nested",
                  XmpContainerKind::Struct, EntryFlags::Deleted);
    add_text(&store, block, kNsOne, "RootB/Nested/value", "surviving child");
    store.finalize();

    XmpStructureTree tree;
    EXPECT_EQ(prepare(store, &tree).status,
              XmpStructureStatus::InvalidMetadata);
}

TEST(XmpStructure, EmptyMarkersIgnoreInactiveStorageAndScalarLeavesRejectSafely)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    Entry marker;
    marker.key          = make_xmp_property_key(store.arena(), kNsOne, "Root");
    marker.origin.block = block;
    marker.origin.wire_type = WireType { WireFamily::Other, 0x5841U };
    marker.value.data.u64   = UINT64_MAX;
    ASSERT_TRUE(detail::xmp_container_entry_shape_is_valid(marker));
    (void)store.add_entry(marker);
    XmpStructureTree tree;
    ASSERT_TRUE(prepare(store, &tree).ok());
    ASSERT_EQ(tree.nodes.size(), 1U);

    MetaStore invalid;
    const BlockId invalid_block = invalid.add_block(BlockInfo {});
    add_container(&invalid, invalid_block, kNsOne, "Root",
                  XmpContainerKind::Struct);
    Entry scalar;
    scalar.key   = make_xmp_property_key(invalid.arena(), kNsOne, "Root/Bad");
    scalar.value = make_u64(UINT64_MAX);
    scalar.origin.block = invalid_block;
    (void)invalid.add_entry(scalar);
    EXPECT_EQ(prepare(invalid, &tree).status,
              XmpStructureStatus::InvalidMetadata);
    ASSERT_EQ(tree.nodes.size(), 1U);
    EXPECT_EQ(tree.nodes.front().path, "Root");
}

TEST(XmpStructure, RejectsXmlAndRdfSyntaxNamespacesAsData)
{
    EXPECT_FALSE(detail::xmp_structure_namespace_is_custom(
        "http://www.w3.org/2000/xmlns/"));
    constexpr std::string_view paths[] = {
        "Root/xml:lang",
        "Root/nsu_687474703a2f2f7777772e77332e6f72672f313939392f30322f32322d7264662d73796e7461782d6e7323:Description",
        "Root/nsu_687474703a2f2f7777772e77332e6f72672f323030302f786d6c6e732f:Binding"
    };
    for (const std::string_view path : paths) {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct);
        add_text(&store, block, kNsOne, path, "syntax namespace");
        XmpStructureTree tree;
        EXPECT_EQ(prepare(store, &tree).status,
                  XmpStructureStatus::InvalidMetadata);
    }
}

TEST(XmpStructure, SupportsLanguageArraysInsideIndexedArrays)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    add_container(&store, block, kNsOne, "Root", XmpContainerKind::Struct);
    add_container(&store, block, kNsOne, "Root/Rows", XmpContainerKind::Seq);
    add_container(&store, block, kNsOne, "Root/Rows[3]",
                  XmpContainerKind::LangAlt);
    add_text(&store, block, kNsOne, "Root/Rows[3][@xml:lang=en-US]", "English");
    add_text(&store, block, kNsOne, "Root/Rows[3][@xml:lang=x-default]",
             "Default");
    XmpStructureTree tree;
    ASSERT_TRUE(prepare(store, &tree).ok());
    EXPECT_EQ(tree.nodes.size(), 5U);
    detail::XmpStructurePath parsed;
    EXPECT_TRUE(
        detail::xmp_structure_parse_path("Root/Rows[3][@xml:lang=en-US]", 8U,
                                         65535U, &parsed));
    EXPECT_FALSE(
        detail::xmp_structure_parse_path("Root/Rows[@xml:lang=en-US][3]", 8U,
                                         65535U, &parsed));
}

}  // namespace openmeta
