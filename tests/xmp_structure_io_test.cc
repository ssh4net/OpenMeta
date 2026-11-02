// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"
#include "openmeta/xmp_structure.h"

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

    static constexpr std::string_view kRootNs = "urn:test:custom:root";

    static void add_container(MetaStore* store, BlockId block,
                              std::string_view schema_ns, std::string_view path,
                              XmpContainerKind kind)
    {
        const MetadataAuthoringEntry request
            = make_xmp_container_entry(schema_ns, path, kind);
        Entry entry;
        entry.key = make_xmp_property_key(store->arena(), schema_ns, path);
        entry.origin.block      = block;
        entry.origin.wire_type  = request.wire_type;
        entry.origin.wire_count = request.wire_count;
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

    static MetaStore make_tree_store()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        add_container(&store, block, kRootNs, "Record",
                      XmpContainerKind::Struct);
        add_container(&store, block, kRootNs, "Record/Items",
                      XmpContainerKind::Seq);
        add_container(&store, block, kRootNs, "Record/Items[1]",
                      XmpContainerKind::LangAlt);
        add_text(&store, block, kRootNs, "Record/Items[1][@xml:lang=en-US]",
                 "English item");
        add_text(&store, block, kRootNs, "Record/Items[1][@xml:lang=x-default]",
                 "Default item");
        add_container(&store, block, kRootNs, "Record/Bag",
                      XmpContainerKind::Bag);
        add_text(&store, block, kRootNs, "Record/Bag[1]", "bag item");
        add_container(&store, block, kRootNs, "Record/Empty",
                      XmpContainerKind::Seq);
        add_text(
            &store, block, kRootNs,
            "Record/nsu_75726e3a746573743a637573746f6d3a6e657374266564:Value",
            "  A & < > Ω\r\t\nB  ");
        store.finalize();
        return store;
    }

    static std::vector<std::byte> render_custom_tree(const MetaStore& store)
    {
        XmpPortableOptions options;
        options.include_existing_xmp = true;
        options.existing_namespace_policy
            = XmpExistingNamespacePolicy::PreserveCustom;
        const XmpDumpResult measured = dump_xmp_portable(store, {}, options);
        if (measured.status != XmpDumpStatus::OutputTruncated)
            return {};
        std::vector<std::byte> output(static_cast<size_t>(measured.needed));
        const XmpDumpResult written = dump_xmp_portable(store, output, options);
        if (written.status != XmpDumpStatus::Ok)
            return {};
        output.resize(static_cast<size_t>(written.written));
        return output;
    }

    static std::string_view as_string(std::span<const std::byte> bytes) noexcept
    {
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static std::vector<std::string> tree_signature(const MetaStore& store)
    {
        std::vector<std::string> signature;
        for (const Entry& entry : store.entries()) {
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            const std::string_view path
                = arena_string(store.arena(),
                               entry.key.data.xmp_property.property_path);
            const std::string_view schema
                = arena_string(store.arena(),
                               entry.key.data.xmp_property.schema_ns);
            // The writer adds its toolkit attribute outside the custom data.
            if (schema == "adobe:ns:meta/" && path == "XMPToolkit")
                continue;
            std::string item(schema);
            item.push_back('|');
            item.append(path);
            item.push_back('|');
            const XmpContainerKind kind = xmp_container_kind(entry);
            if (kind != XmpContainerKind::None) {
                item.push_back('D');
                item.push_back(
                    static_cast<char>('0' + static_cast<uint8_t>(kind)));
            } else if (entry.value.kind == MetaValueKind::Text) {
                item.push_back('T');
                const std::span<const std::byte> raw = store.arena().span(
                    entry.value.data.span);
                item.append(reinterpret_cast<const char*>(raw.data()),
                            raw.size());
            } else {
                item.push_back('E');
            }
            signature.push_back(std::move(item));
        }
        std::sort(signature.begin(), signature.end());
        return signature;
    }

    static bool has_text_at(const MetaStore& store, std::string_view path,
                            std::string_view expected) noexcept
    {
        for (const Entry& entry : store.entries()) {
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || xmp_container_kind(entry) != XmpContainerKind::None
                || entry.value.kind != MetaValueKind::Text
                || arena_string(store.arena(),
                                entry.key.data.xmp_property.schema_ns)
                       != kRootNs
                || arena_string(store.arena(),
                                entry.key.data.xmp_property.property_path)
                       != path) {
                continue;
            }
            const std::span<const std::byte> raw = store.arena().span(
                entry.value.data.span);
            if (as_string(raw) == expected)
                return true;
        }
        return false;
    }

    static XmpDecodeStatus decode_custom_status(std::string_view packet,
                                                XmpDecodeOptions options)
    {
        MetaStore store;
        options.preserve_custom_structures = true;
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(packet.data()), packet.size());
        return decode_xmp_packet(bytes, store, EntryFlags::None, options).status;
    }

    static std::span<const std::byte> custom_packet() noexcept
    {
        static constexpr std::string_view kPacket
            = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
              "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' "
              "xmlns:xml='http://www.w3.org/XML/1998/namespace' "
              "xmlns:m='urn:test:custom:root' "
              "xmlns:n='urn:test:custom:nest&amp;ed'>"
              "<rdf:RDF><rdf:Description>"
              "<m:Record rdf:parseType='Resource'>"
              "<m:Items><rdf:Seq><rdf:li><rdf:Alt>"
              "<rdf:li xml:lang='en-US'>  item &amp; &lt;Ω&#13;\n  </rdf:li>"
              "<rdf:li xml:lang='x-default'>default</rdf:li>"
              "</rdf:Alt></rdf:li><rdf:li><rdf:Alt>"
              "<rdf:li xml:lang='x-default'>second item</rdf:li>"
              "</rdf:Alt></rdf:li></rdf:Seq></m:Items>"
              "<m:Bag><rdf:Bag><rdf:li>one</rdf:li>"
              "<rdf:li>two</rdf:li></rdf:Bag></m:Bag>"
              "<m:Empty><rdf:Seq/></m:Empty>"
              "<n:Value>  A &amp; &lt; &gt; Ω&#13;\t\nB  </n:Value>"
              "</m:Record></rdf:Description></rdf:RDF></x:xmpmeta>";
        return std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                              kPacket.data()),
                                          kPacket.size());
    }

}  // namespace

TEST(XmpStructureIo, EmitsDeclaredContainersAndExactCustomText)
{
    MetaStore store = make_tree_store();
    XmpPortableOptions options;
    options.include_existing_xmp = true;
    options.existing_namespace_policy
        = XmpExistingNamespacePolicy::PreserveCustom;
    const XmpDumpResult measured = dump_xmp_portable(store, {}, options);
    ASSERT_EQ(measured.status, XmpDumpStatus::OutputTruncated);
    std::vector<std::byte> output(static_cast<size_t>(measured.needed));
    const XmpDumpResult emitted = dump_xmp_portable(store, output, options);
    ASSERT_EQ(emitted.status, XmpDumpStatus::Ok);
    const std::string_view xml = as_string(
        std::span<const std::byte>(output.data(),
                                   static_cast<size_t>(emitted.written)));
    EXPECT_NE(xml.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(xml.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(xml.find("<rdf:Alt>"), std::string_view::npos);
    const size_t default_language = xml.find("xml:lang=\"x-default\"");
    const size_t english_language = xml.find("xml:lang=\"en-US\"");
    EXPECT_NE(default_language, std::string_view::npos);
    EXPECT_NE(english_language, std::string_view::npos);
    EXPECT_LT(default_language, english_language);
    EXPECT_NE(xml.find("xmlns:omns1=\"urn:test:custom:nest&amp;ed\""),
              std::string_view::npos);
    EXPECT_NE(xml.find("xmlns:omns2=\"urn:test:custom:root\""),
              std::string_view::npos);
    EXPECT_NE(xml.find("&amp; &lt; &gt; Ω&#13;\t\nB  "),
              std::string_view::npos);
    EXPECT_NE(xml.find("<omns1:Value>"), std::string_view::npos);
    EXPECT_EQ(xml.find("<omns2:Record>"), std::string_view::npos);
    EXPECT_NE(xml.find("<omns2:Record rdf:parseType=\"Resource\">"),
              std::string_view::npos);
}

TEST(XmpStructureIo, MarkerOnlyCustomRootGetsADeclaredPrefix)
{
    static constexpr std::string_view kMarkerNs = "urn:test:marker-only";
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    add_container(&store, block, kMarkerNs, "EmptyArray",
                  XmpContainerKind::Bag);
    store.finalize();

    XmpPortableOptions options;
    options.include_existing_xmp = true;
    options.existing_namespace_policy
        = XmpExistingNamespacePolicy::PreserveCustom;
    const XmpDumpResult measured = dump_xmp_portable(store, {}, options);
    ASSERT_EQ(measured.status, XmpDumpStatus::OutputTruncated);
    std::vector<std::byte> output(static_cast<size_t>(measured.needed));
    const XmpDumpResult emitted = dump_xmp_portable(store, output, options);
    ASSERT_EQ(emitted.status, XmpDumpStatus::Ok);
    const std::string_view xml = as_string(
        std::span<const std::byte>(output.data(),
                                   static_cast<size_t>(emitted.written)));
    EXPECT_NE(xml.find("xmlns:omns1=\"urn:test:marker-only\""),
              std::string_view::npos);
    EXPECT_NE(xml.find("<omns1:EmptyArray>"), std::string_view::npos);
    EXPECT_NE(xml.find("<rdf:Bag>"), std::string_view::npos);
}

TEST(XmpStructureIo, DeletedOnlyCustomNamespaceDoesNotLeakIntoPacket)
{
    static constexpr std::string_view kDeletedNs = "urn:aaa:deleted-only";
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    add_container(&store, block, kRootNs, "Record", XmpContainerKind::Struct);
    add_text(&store, block, kRootNs, "Record/Value", "live");
    add_text(&store, block, kDeletedNs, "Gone", "deleted", EntryFlags::Deleted);
    store.finalize();

    const std::vector<std::byte> packet = render_custom_tree(store);
    ASSERT_FALSE(packet.empty());
    const std::string_view xml = as_string(
        std::span<const std::byte>(packet.data(), packet.size()));
    EXPECT_EQ(xml.find(kDeletedNs), std::string_view::npos);
    EXPECT_NE(xml.find("xmlns:omns1=\"urn:test:custom:root\""),
              std::string_view::npos);
    EXPECT_EQ(xml.find("xmlns:omns2="), std::string_view::npos);
}

TEST(XmpStructureIo, InvalidGraphAndOptionsLeaveCallerOutputUntouched)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    add_container(&store, block, kRootNs, "Record", XmpContainerKind::Struct);
    add_text(&store, block, kRootNs, "Record/Missing/Value", "orphan");
    store.finalize();

    std::array<std::byte, 128U> output;
    output.fill(std::byte { 0x5a });
    const std::array<std::byte, 128U> initial = output;
    XmpPortableOptions options;
    options.include_existing_xmp = true;
    options.existing_namespace_policy
        = XmpExistingNamespacePolicy::PreserveCustom;
    const XmpDumpResult invalid = dump_xmp_portable(store, output, options);
    EXPECT_EQ(invalid.status, XmpDumpStatus::InvalidMetadata);
    EXPECT_EQ(output, initial);

    std::array<std::byte, 4096U> excluded_output;
    excluded_output.fill(std::byte { 0x2a });
    XmpPortableOptions excluded      = options;
    excluded.include_existing_xmp    = false;
    const XmpDumpResult not_selected = dump_xmp_portable(store, excluded_output,
                                                         excluded);
    EXPECT_EQ(not_selected.status, XmpDumpStatus::Ok);
    const std::string_view excluded_xml = as_string(
        std::span<const std::byte>(excluded_output.data(),
                                   static_cast<size_t>(not_selected.written)));
    EXPECT_EQ(excluded_xml.find("Record"), std::string_view::npos);
    EXPECT_EQ(excluded_xml.find("Missing"), std::string_view::npos);
    EXPECT_EQ(excluded_xml.find(kRootNs), std::string_view::npos);
    EXPECT_EQ(excluded_xml.find("xmlns:omns"), std::string_view::npos);

    excluded = options;
    excluded.existing_namespace_policy
        = XmpExistingNamespacePolicy::KnownPortableOnly;
    const XmpDumpResult known_only = dump_xmp_portable(store, excluded_output,
                                                       excluded);
    EXPECT_EQ(known_only.status, XmpDumpStatus::Ok);
    const std::string_view known_only_xml = as_string(
        std::span<const std::byte>(excluded_output.data(),
                                   static_cast<size_t>(known_only.written)));
    EXPECT_EQ(known_only_xml.find("Record"), std::string_view::npos);
    EXPECT_EQ(known_only_xml.find("Missing"), std::string_view::npos);
    EXPECT_EQ(known_only_xml.find(kRootNs), std::string_view::npos);
    EXPECT_EQ(known_only_xml.find("xmlns:omns"), std::string_view::npos);

    std::vector<std::byte> sidecar(32U, std::byte { 0x3c });
    XmpSidecarOptions sidecar_options;
    sidecar_options.format              = XmpSidecarFormat::Portable;
    sidecar_options.portable            = options;
    const XmpDumpResult invalid_sidecar = dump_xmp_sidecar(store, &sidecar,
                                                           sidecar_options);
    EXPECT_EQ(invalid_sidecar.status, XmpDumpStatus::InvalidMetadata);
    EXPECT_EQ(sidecar, std::vector<std::byte>(32U, std::byte { 0x3c }));

    MetaStore valid              = make_tree_store();
    options.structures.max_nodes = 0U;
    output.fill(std::byte { 0x6b });
    const std::array<std::byte, 128U> invalid_options_initial = output;
    const XmpDumpResult invalid_options = dump_xmp_portable(valid, output,
                                                            options);
    EXPECT_EQ(invalid_options.status, XmpDumpStatus::InvalidOptions);
    EXPECT_EQ(output, invalid_options_initial);

    XmpSidecarRequest request;
    request.format                        = XmpSidecarFormat::Portable;
    request.portable_structures.max_nodes = 0U;
    EXPECT_EQ(make_xmp_sidecar_options(request).portable.structures.max_nodes,
              0U);
    sidecar.assign(24U, std::byte { 0x4d });
    const std::vector<std::byte> request_initial = sidecar;
    const XmpDumpResult request_invalid = dump_xmp_sidecar(valid, &sidecar,
                                                           request);
    EXPECT_EQ(request_invalid.status, XmpDumpStatus::InvalidOptions);
    EXPECT_EQ(sidecar, request_initial);
}

TEST(XmpStructureIo, RejectsOutputAliasAndRetainsShortSpanPrefix)
{
    MetaStore store = make_tree_store();
    XmpPortableOptions options;
    options.include_existing_xmp = true;
    options.existing_namespace_policy
        = XmpExistingNamespacePolicy::PreserveCustom;

    std::span<std::byte> arena_output = store.arena().bytes_mut();
    std::vector<std::byte> arena_before(arena_output.begin(),
                                        arena_output.end());
    const XmpDumpResult overlap = dump_xmp_portable(store, arena_output,
                                                    options);
    EXPECT_EQ(overlap.status, XmpDumpStatus::InvalidOptions);
    EXPECT_TRUE(std::equal(arena_output.begin(), arena_output.end(),
                           arena_before.begin(), arena_before.end()));

    const std::vector<std::byte> complete = render_custom_tree(store);
    ASSERT_FALSE(complete.empty());
    std::array<std::byte, 37U> short_output;
    short_output.fill(std::byte { 0 });
    const XmpDumpResult short_result = dump_xmp_portable(store, short_output,
                                                         options);
    EXPECT_EQ(short_result.status, XmpDumpStatus::OutputTruncated);
    EXPECT_EQ(short_result.written, short_output.size());
    EXPECT_TRUE(std::equal(short_output.begin(), short_output.end(),
                           complete.begin(),
                           complete.begin() + short_output.size()));
}

TEST(XmpStructureIo, OptInDecodeRetainsTreesAndDefaultRemainsFlat)
{
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
    GTEST_SKIP() << "Expat is required for structured decode readback.";
#endif
    const std::span<const std::byte> packet = custom_packet();
    MetaStore flat;
    const XmpDecodeResult flat_result = decode_xmp_packet(packet, flat);
    ASSERT_EQ(flat_result.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(flat_result.entries_decoded, 7U);
    for (const Entry& entry : flat.entries())
        EXPECT_EQ(xmp_container_kind(entry), XmpContainerKind::None);
    bool flat_custom_text_trimmed = false;
    for (const Entry& entry : flat.entries()) {
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || arena_string(flat.arena(),
                            entry.key.data.xmp_property.property_path)
                   != "Record/nsu_75726e3a746573743a637573746f6d3a6e657374266564:Value") {
            continue;
        }
        const std::span<const std::byte> raw = flat.arena().span(
            entry.value.data.span);
        flat_custom_text_trimmed = as_string(raw) == "A & < > Ω\r\t\nB";
    }
    EXPECT_TRUE(flat_custom_text_trimmed);

    MetaStore structured;
    XmpDecodeOptions options;
    options.preserve_custom_structures = true;
    const XmpDecodeResult structured_result
        = decode_xmp_packet(packet, structured, EntryFlags::None, options);
    ASSERT_EQ(structured_result.status, XmpDecodeStatus::Ok);
    EXPECT_GT(structured_result.entries_decoded, flat_result.entries_decoded);

    bool has_root                     = false;
    bool has_seq                      = false;
    bool has_alt                      = false;
    bool has_bag                      = false;
    bool has_empty_seq                = false;
    bool has_encoded_nested_path      = false;
    bool has_empty_text_for_container = false;
    bool has_exact_custom_text        = false;
    for (const Entry& entry : structured.entries()) {
        if (entry.key.kind != MetaKeyKind::XmpProperty)
            continue;
        const std::string_view path
            = arena_string(structured.arena(),
                           entry.key.data.xmp_property.property_path);
        const XmpContainerKind kind = xmp_container_kind(entry);
        has_root |= path == "Record" && kind == XmpContainerKind::Struct;
        has_seq |= path == "Record/Items" && kind == XmpContainerKind::Seq;
        has_alt |= path == "Record/Items[1]"
                   && kind == XmpContainerKind::LangAlt;
        has_bag |= path == "Record/Bag" && kind == XmpContainerKind::Bag;
        has_empty_seq |= path == "Record/Empty"
                         && kind == XmpContainerKind::Seq;
        has_encoded_nested_path
            |= path
               == "Record/nsu_75726e3a746573743a637573746f6d3a6e657374266564:Value";
        has_empty_text_for_container |= path == "Record/Empty"
                                        && entry.value.kind
                                               == MetaValueKind::Text;
        if (path
                == "Record/nsu_75726e3a746573743a637573746f6d3a6e657374266564:Value"
            && entry.value.kind == MetaValueKind::Text) {
            const std::span<const std::byte> raw = structured.arena().span(
                entry.value.data.span);
            has_exact_custom_text = as_string(raw) == "  A & < > Ω\r\t\nB  ";
        }
    }
    EXPECT_TRUE(has_root);
    EXPECT_TRUE(has_seq);
    EXPECT_TRUE(has_alt);
    EXPECT_TRUE(has_bag);
    EXPECT_TRUE(has_empty_seq);
    EXPECT_TRUE(has_encoded_nested_path);
    EXPECT_FALSE(has_empty_text_for_container);
    EXPECT_TRUE(has_exact_custom_text);
    EXPECT_TRUE(has_text_at(structured, "Record/Items[1][@xml:lang=en-US]",
                            "  item & <Ω\r\n  "));
    EXPECT_TRUE(has_text_at(structured, "Record/Items[1][@xml:lang=x-default]",
                            "default"));
    EXPECT_TRUE(has_text_at(structured, "Record/Items[2][@xml:lang=x-default]",
                            "second item"));
    EXPECT_TRUE(has_text_at(structured, "Record/Bag[1]", "one"));
    EXPECT_TRUE(has_text_at(structured, "Record/Bag[2]", "two"));
    const std::vector<std::string> expected_signature = tree_signature(
        structured);

    structured.finalize();
    XmpPortableOptions portable;
    portable.include_existing_xmp = true;
    portable.existing_namespace_policy
        = XmpExistingNamespacePolicy::PreserveCustom;
    const XmpDumpResult measured = dump_xmp_portable(structured, {}, portable);
    ASSERT_EQ(measured.status, XmpDumpStatus::OutputTruncated);
    std::vector<std::byte> output(static_cast<size_t>(measured.needed));
    const XmpDumpResult emitted = dump_xmp_portable(structured, output,
                                                    portable);
    ASSERT_EQ(emitted.status, XmpDumpStatus::Ok);
    const std::string_view xml = as_string(
        std::span<const std::byte>(output.data(), output.size()));
    EXPECT_NE(xml.find("rdf:parseType=\"Resource\""), std::string_view::npos);
    EXPECT_NE(xml.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(xml.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(xml.find("<rdf:Alt>"), std::string_view::npos);
    EXPECT_NE(xml.find("&#13;"), std::string_view::npos);

    MetaStore roundtrip;
    XmpDecodeOptions preserve_roundtrip;
    preserve_roundtrip.preserve_custom_structures = true;
    const std::span<const std::byte> rendered_packet(
        reinterpret_cast<const std::byte*>(xml.data()), xml.size());
    const XmpDecodeResult roundtrip_result
        = decode_xmp_packet(rendered_packet, roundtrip, EntryFlags::None,
                            preserve_roundtrip);
    ASSERT_EQ(roundtrip_result.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(tree_signature(roundtrip), expected_signature);
    EXPECT_TRUE(has_text_at(
        roundtrip,
        "Record/nsu_75726e3a746573743a637573746f6d3a6e657374266564:Value",
        "  A & < > Ω\r\t\nB  "));

    static constexpr std::string_view kStandardArray
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' "
          "xmlns:dc='http://purl.org/dc/elements/1.1/'>"
          "<rdf:RDF><rdf:Description><dc:creator><rdf:Seq>"
          "<rdf:li>Creator</rdf:li></rdf:Seq></dc:creator>"
          "</rdf:Description></rdf:RDF></x:xmpmeta>";
    const std::span<const std::byte> standard_packet(
        reinterpret_cast<const std::byte*>(kStandardArray.data()),
        kStandardArray.size());
    MetaStore standard_store;
    const XmpDecodeResult standard_result
        = decode_xmp_packet(standard_packet, standard_store, EntryFlags::None,
                            options);
    EXPECT_EQ(standard_result.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(standard_result.entries_decoded, 1U);
    for (const Entry& entry : standard_store.entries())
        EXPECT_EQ(xmp_container_kind(entry), XmpContainerKind::None);
}

TEST(XmpStructureIo, CustomMixedContentAndUnsupportedQualifiersFailClearly)
{
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
    GTEST_SKIP() << "Expat is required for structured decode readback.";
#endif
    static constexpr std::string_view kPrefixSuffix
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' "
          "xmlns:m='urn:test:custom:root'><rdf:RDF><rdf:Description>"
          "<m:Record>prefix<m:Child/>suffix</m:Record>"
          "</rdf:Description></rdf:RDF></x:xmpmeta>";
    static constexpr std::string_view kArrayText
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' "
          "xmlns:m='urn:test:custom:root'><rdf:RDF><rdf:Description>"
          "<m:Values><rdf:Seq>unexpected<rdf:li>value</rdf:li>"
          "</rdf:Seq></m:Values>"
          "</rdf:Description></rdf:RDF></x:xmpmeta>";
    static constexpr std::string_view kResource
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' "
          "xmlns:m='urn:test:custom:root'><rdf:RDF><rdf:Description>"
          "<m:Resource rdf:resource='urn:test:value'/>"
          "</rdf:Description></rdf:RDF></x:xmpmeta>";
    static constexpr std::string_view kQualifier
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' "
          "xmlns:m='urn:test:custom:root'><rdf:RDF><rdf:Description>"
          "<m:Value rdf:type='urn:test:type'>value</m:Value>"
          "</rdf:Description></rdf:RDF></x:xmpmeta>";

    const std::array<std::string_view, 4U> packets = {
        kPrefixSuffix,
        kArrayText,
        kResource,
        kQualifier,
    };
    XmpDecodeOptions options;
    options.preserve_custom_structures = true;
    for (const std::string_view packet : packets) {
        MetaStore store;
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(packet.data()), packet.size());
        const XmpDecodeResult result
            = decode_xmp_packet(bytes, store, EntryFlags::None, options);
        EXPECT_EQ(result.status, XmpDecodeStatus::Unsupported);
    }
}

TEST(XmpStructureIo, CustomNamespaceDepthNodeAndRequestHardBounds)
{
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
    GTEST_SKIP() << "Expat is required for structured decode readback.";
#endif
    static constexpr std::string_view kNamespacePrefix
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' ";
    std::string namespace_packet(kNamespacePrefix);
    namespace_packet.append("xmlns:m='urn:test:custom:root' ");
    for (uint32_t i = 0U; i < 64U; ++i) {
        namespace_packet.append("xmlns:n");
        namespace_packet.append(std::to_string(i));
        namespace_packet.append("='urn:test:nested:");
        namespace_packet.append(std::to_string(i));
        namespace_packet.append("' ");
    }
    namespace_packet.append("><rdf:RDF><rdf:Description>"
                            "<m:Record rdf:parseType='Resource'>");
    for (uint32_t i = 0U; i < 64U; ++i) {
        namespace_packet.append("<n");
        namespace_packet.append(std::to_string(i));
        namespace_packet.append(":Field>");
        namespace_packet.append(std::to_string(i));
        namespace_packet.append("</n");
        namespace_packet.append(std::to_string(i));
        namespace_packet.append(":Field>");
    }
    namespace_packet.append(
        "</m:Record></rdf:Description></rdf:RDF></x:xmpmeta>");
    EXPECT_EQ(decode_custom_status(namespace_packet, XmpDecodeOptions {}),
              XmpDecodeStatus::LimitExceeded);

    // A full custom namespace registry must not exclude unrelated standard roots.
    std::string mixed_packet(kNamespacePrefix);
    mixed_packet.append("xmlns:xmp='http://ns.adobe.com/xap/1.0/' ");
    for (uint32_t i = 0U; i < 64U; ++i) {
        mixed_packet.append("xmlns:n" + std::to_string(i)
                            + "='urn:test:root:" + std::to_string(i) + "' ");
    }
    mixed_packet.append("><rdf:RDF><rdf:Description>");
    for (uint32_t i = 0U; i < 64U; ++i)
        mixed_packet.append("<n" + std::to_string(i)
                            + ":Root rdf:parseType='Resource'/>");
    mixed_packet.append("<xmp:CreatorTool>tool</xmp:CreatorTool>"
                        "</rdf:Description></rdf:RDF></x:xmpmeta>");
    EXPECT_EQ(decode_custom_status(mixed_packet, XmpDecodeOptions {}),
              XmpDecodeStatus::Ok);

    std::string node_packet(kNamespacePrefix);
    node_packet.append(
        "xmlns:m='urn:test:custom:root'><rdf:RDF><rdf:Description>"
        "<m:Record rdf:parseType='Resource'>");
    for (uint32_t i = 0U; i < 4096U; ++i) {
        node_packet.append("<m:Field");
        node_packet.append(std::to_string(i));
        node_packet.append(">v</m:Field");
        node_packet.append(std::to_string(i));
        node_packet.append(">");
    }
    node_packet.append("</m:Record></rdf:Description></rdf:RDF></x:xmpmeta>");
    EXPECT_EQ(decode_custom_status(node_packet, XmpDecodeOptions {}),
              XmpDecodeStatus::LimitExceeded);

    std::string request_packet(kNamespacePrefix);
    request_packet.append(
        "xmlns:m='urn:test:custom:root'><rdf:RDF><rdf:Description>"
        "<m:Value>");
    request_packet.append(16U * 1024U * 1024U + 1U, 'v');
    request_packet.append("</m:Value></rdf:Description></rdf:RDF></x:xmpmeta>");
    XmpDecodeOptions unlimited_text;
    unlimited_text.limits.max_value_bytes       = 0U;
    unlimited_text.limits.max_total_value_bytes = 0U;
    EXPECT_EQ(decode_custom_status(request_packet, unlimited_text),
              XmpDecodeStatus::LimitExceeded);
}

TEST(XmpStructureIo, OptInRejectsEmptyInvalidAndOversizedRootUris)
{
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
    GTEST_SKIP() << "Expat is required for structured decode readback.";
#endif
    static constexpr std::string_view kEmptyUri
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:RDF><rdf:Description><Record>value</Record>"
          "</rdf:Description></rdf:RDF></x:xmpmeta>";
    static constexpr std::string_view kInvalidUri
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' "
          "xmlns:m='not a uri'><rdf:RDF><rdf:Description>"
          "<m:Record>value</m:Record>"
          "</rdf:Description></rdf:RDF></x:xmpmeta>";
    EXPECT_EQ(decode_custom_status(kEmptyUri, XmpDecodeOptions {}),
              XmpDecodeStatus::Unsupported);
    EXPECT_EQ(decode_custom_status(kInvalidUri, XmpDecodeOptions {}),
              XmpDecodeStatus::Unsupported);

    std::string oversized_uri = "urn:";
    oversized_uri.append(4093U, 'x');
    std::string oversized_packet
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' xmlns:m='";
    oversized_packet.append(oversized_uri);
    oversized_packet.append("'><rdf:RDF><rdf:Description>"
                            "<m:Record>value</m:Record>"
                            "</rdf:Description></rdf:RDF></x:xmpmeta>");
    EXPECT_EQ(decode_custom_status(oversized_packet, XmpDecodeOptions {}),
              XmpDecodeStatus::LimitExceeded);
}

TEST(XmpStructureIo, CustomAltWithoutLanguageAndDeclarationLimitsFailClearly)
{
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
    GTEST_SKIP() << "Expat is required for structured decode readback.";
#endif
    static constexpr std::string_view kNoLanguage
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' "
          "xmlns:m='urn:test:custom:root'><rdf:RDF><rdf:Description>"
          "<m:Alt><rdf:Alt><rdf:li>value</rdf:li></rdf:Alt></m:Alt>"
          "</rdf:Description></rdf:RDF></x:xmpmeta>";
    const std::span<const std::byte> bad_packet(
        reinterpret_cast<const std::byte*>(kNoLanguage.data()),
        kNoLanguage.size());
    MetaStore unsupported_store;
    XmpDecodeOptions preserve;
    preserve.preserve_custom_structures = true;
    const XmpDecodeResult unsupported
        = decode_xmp_packet(bad_packet, unsupported_store, EntryFlags::None,
                            preserve);
    EXPECT_EQ(unsupported.status, XmpDecodeStatus::Unsupported);

    MetaStore limited_store;
    XmpDecodeOptions limited      = preserve;
    limited.limits.max_properties = 1U;
    const XmpDecodeResult limit   = decode_xmp_packet(custom_packet(),
                                                      limited_store,
                                                      EntryFlags::None, limited);
    EXPECT_EQ(limit.status, XmpDecodeStatus::LimitExceeded);
}

}  // namespace openmeta
