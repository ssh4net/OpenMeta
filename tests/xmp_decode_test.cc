// SPDX-License-Identifier: Apache-2.0

#include "openmeta/xmp_decode.h"

#include "openmeta/metadata_concepts.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openmeta {

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT

namespace {

    static void expect_xmp_text_value(const MetaStore& store,
                                      std::string_view schema_ns,
                                      std::string_view path,
                                      std::string_view expected)
    {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = schema_ns;
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    }

    static void expect_xmp_missing(const MetaStore& store,
                                   std::string_view schema_ns,
                                   std::string_view path)
    {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = schema_ns;
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 0U);
    }

}  // namespace

TEST(XmpDecodeTest, DecodesAttributesArraysAndRdfResource)
{
    const std::string xmp
        = "<?xpacket begin='\\xEF\\xBB\\xBF' id='W5M0MpCehiHzreSzNTczkc9d'?>"
          "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:dc='http://purl.org/dc/elements/1.1/' "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
          "xmp:CreatorTool='OpenMeta'>"
          "<dc:creator><rdf:Seq>"
          "<rdf:li>John</rdf:li><rdf:li>Jane</rdf:li>"
          "</rdf:Seq></dc:creator>"
          "<xmp:Rating> 5 </xmp:Rating>"
          "<xmpMM:InstanceID rdf:resource='uuid:123'/>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>"
          "<?xpacket end='w'?>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 5U);

    store.finalize();

    auto expect_text = [&](std::string_view schema_ns, std::string_view path,
                           std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = schema_ns;
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("http://ns.adobe.com/xap/1.0/", "CreatorTool", "OpenMeta");
    expect_text("http://purl.org/dc/elements/1.1/", "creator[1]", "John");
    expect_text("http://purl.org/dc/elements/1.1/", "creator[2]", "Jane");
    expect_text("http://ns.adobe.com/xap/1.0/", "Rating", "5");
    expect_text("http://ns.adobe.com/xap/1.0/mm/", "InstanceID", "uuid:123");
}

TEST(XmpDecodeTest, TrimsXmpMetaCloseWithAlternatePrefix)
{
    std::string xmp
        = "<?xpacket begin='\\xEF\\xBB\\xBF' id='W5M0MpCehiHzreSzNTczkc9d'?>"
          "<xmp:xmpmeta xmlns:xmp='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'>"
          "<xmp:Rating>0</xmp:Rating>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</xmp:xmpmeta>"
          "<?xpacket end='w'?>";
    xmp.append("\0\0\0padding", 10U);

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 1U);
}

TEST(XmpDecodeTest, NamespaceRebindingDoesNotLeakBetweenPackets)
{
    const std::string shadow_xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description xmlns:xmp='urn:vendor:shadow-xmp:'>"
          "<xmp:Flag>shadow</xmp:Flag>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";
    const std::span<const std::byte> shadow_bytes(
        reinterpret_cast<const std::byte*>(shadow_xmp.data()),
        shadow_xmp.size());

    MetaStore shadow_store;
    const XmpDecodeResult shadow = decode_xmp_packet(shadow_bytes,
                                                     shadow_store);
    EXPECT_EQ(shadow.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(shadow.entries_decoded, 1U);
    shadow_store.finalize();

    expect_xmp_text_value(shadow_store, "urn:vendor:shadow-xmp:", "Flag",
                          "shadow");
    expect_xmp_missing(shadow_store, "http://ns.adobe.com/xap/1.0/", "Flag");

    const std::string standard_xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'>"
          "<xmp:CreatorTool>OpenMeta</xmp:CreatorTool>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";
    const std::span<const std::byte> standard_bytes(
        reinterpret_cast<const std::byte*>(standard_xmp.data()),
        standard_xmp.size());

    MetaStore standard_store;
    const XmpDecodeResult standard = decode_xmp_packet(standard_bytes,
                                                       standard_store);
    EXPECT_EQ(standard.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(standard.entries_decoded, 1U);
    standard_store.finalize();

    expect_xmp_text_value(standard_store, "http://ns.adobe.com/xap/1.0/",
                          "CreatorTool", "OpenMeta");
    expect_xmp_missing(standard_store, "urn:vendor:shadow-xmp:", "CreatorTool");
}

TEST(XmpDecodeTest, DecodesRdfAboutAndEmptyBag)
{
    const std::string xmp
        = "<xmp:xmpmeta xmlns:xmp='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "rdf:about='uuid:faf5bdd5-ba3d-11da-ad31-d33d75182f1b' "
          "xmlns:dc='http://purl.org/dc/elements/1.1/'>"
          "<dc:subject><rdf:Bag></rdf:Bag></dc:subject>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</xmp:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    MetaKeyView about_key;
    about_key.kind = MetaKeyKind::XmpProperty;
    about_key.data.xmp_property.schema_ns
        = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    about_key.data.xmp_property.property_path = "About";

    const std::span<const EntryId> about_ids = store.find_all(about_key);
    ASSERT_EQ(about_ids.size(), 1U);

    const Entry& about = store.entry(about_ids[0]);
    ASSERT_EQ(about.value.kind, MetaValueKind::Text);
    const std::span<const std::byte> about_bytes = store.arena().span(
        about.value.data.span);
    const std::string_view about_text(reinterpret_cast<const char*>(
                                          about_bytes.data()),
                                      about_bytes.size());
    EXPECT_EQ(about_text, "uuid:faf5bdd5-ba3d-11da-ad31-d33d75182f1b");

    MetaKeyView subject_key;
    subject_key.kind = MetaKeyKind::XmpProperty;
    subject_key.data.xmp_property.schema_ns = "http://purl.org/dc/elements/1.1/";
    subject_key.data.xmp_property.property_path = "subject";

    const std::span<const EntryId> subject_ids = store.find_all(subject_key);
    ASSERT_EQ(subject_ids.size(), 1U);
    const Entry& subject = store.entry(subject_ids[0]);
    ASSERT_EQ(subject.value.kind, MetaValueKind::Text);
    EXPECT_EQ(subject.value.count, 0U);
}

TEST(XmpDecodeTest, SkipsEmptyRdfAbout)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description rdf:about=''/>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 0U);
}

TEST(XmpDecodeTest, DecodesXmpMmStructuredMixedNamespaceChildren)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
          "xmlns:stRef='http://ns.adobe.com/xap/1.0/sType/ResourceRef#' "
          "xmlns:stEvt='http://ns.adobe.com/xap/1.0/sType/ResourceEvent#'>"
          "<xmpMM:DerivedFrom rdf:parseType='Resource'>"
          "<stRef:documentID>xmp.did:base</stRef:documentID>"
          "<stRef:instanceID>xmp.iid:base</stRef:instanceID>"
          "<stRef:manageTo>https://example.invalid/base</stRef:manageTo>"
          "</xmpMM:DerivedFrom>"
          "<xmpMM:ManagedFrom rdf:parseType='Resource'>"
          "<stRef:documentID>xmp.did:managed</stRef:documentID>"
          "<stRef:instanceID>xmp.iid:managed</stRef:instanceID>"
          "</xmpMM:ManagedFrom>"
          "<xmpMM:Ingredients><rdf:Bag>"
          "<rdf:li rdf:parseType='Resource'>"
          "<stRef:documentID>xmp.did:ingredient</stRef:documentID>"
          "<stRef:instanceID>xmp.iid:ingredient</stRef:instanceID>"
          "</rdf:li>"
          "</rdf:Bag></xmpMM:Ingredients>"
          "<xmpMM:RenditionOf rdf:parseType='Resource'>"
          "<stRef:documentID>xmp.did:rendition</stRef:documentID>"
          "<stRef:filePath>/tmp/rendition.jpg</stRef:filePath>"
          "<stRef:renditionClass>proof:pdf</stRef:renditionClass>"
          "</xmpMM:RenditionOf>"
          "<xmpMM:History><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<stEvt:action>saved</stEvt:action>"
          "<stEvt:softwareAgent>OpenMeta</stEvt:softwareAgent>"
          "<stEvt:when>2026-04-15T09:00:00Z</stEvt:when>"
          "</rdf:li>"
          "</rdf:Seq></xmpMM:History>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 13U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = "http://ns.adobe.com/xap/1.0/mm/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("DerivedFrom/stRef:documentID", "xmp.did:base");
    expect_text("DerivedFrom/stRef:instanceID", "xmp.iid:base");
    expect_text("DerivedFrom/stRef:manageTo", "https://example.invalid/base");
    expect_text("ManagedFrom/stRef:documentID", "xmp.did:managed");
    expect_text("ManagedFrom/stRef:instanceID", "xmp.iid:managed");
    expect_text("Ingredients[1]/stRef:documentID", "xmp.did:ingredient");
    expect_text("Ingredients[1]/stRef:instanceID", "xmp.iid:ingredient");
    expect_text("RenditionOf/stRef:documentID", "xmp.did:rendition");
    expect_text("RenditionOf/stRef:filePath", "/tmp/rendition.jpg");
    expect_text("RenditionOf/stRef:renditionClass", "proof:pdf");
    expect_text("History[1]/stEvt:action", "saved");
    expect_text("History[1]/stEvt:softwareAgent", "OpenMeta");
    expect_text("History[1]/stEvt:when", "2026-04-15T09:00:00Z");
}

TEST(XmpDecodeTest, DecodesXmpMmPantryStructuredChildren)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
          "xmlns:dc='http://purl.org/dc/elements/1.1/' "
          "xmlns:stRef='http://ns.adobe.com/xap/1.0/sType/ResourceRef#' "
          "xmlns:stEvt='http://ns.adobe.com/xap/1.0/sType/ResourceEvent#' "
          "xmlns:stVer='http://ns.adobe.com/xap/1.0/sType/Version#' "
          "xmlns:vendor='https://example.invalid/xmp/vendor/' "
          "xmlns:other='urn:openmeta:other'>"
          "<xmpMM:Pantry><rdf:Bag>"
          "<rdf:li rdf:parseType='Resource'>"
          "<xmpMM:InstanceID>uuid:pantry-1</xmpMM:InstanceID>"
          "<dc:format>image/jpeg</dc:format>"
          "<xmpMM:DerivedFrom rdf:parseType='Resource'>"
          "<stRef:documentID>xmp.did:pantry-source</stRef:documentID>"
          "</xmpMM:DerivedFrom>"
          "<xmpMM:History><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<stEvt:action>copied</stEvt:action>"
          "</rdf:li>"
          "</rdf:Seq></xmpMM:History>"
          "<xmpMM:Versions><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<stVer:comments>Embedded component</stVer:comments>"
          "<stVer:event rdf:parseType='Resource'>"
          "<stEvt:softwareAgent>Pantry Writer</stEvt:softwareAgent>"
          "</stVer:event>"
          "</rdf:li>"
          "</rdf:Seq></xmpMM:Versions>"
          "<vendor:Payload>opaque-vendor-data</vendor:Payload>"
          "<other:Payload>other-vendor-data</other:Payload>"
          "</rdf:li>"
          "</rdf:Bag></xmpMM:Pantry>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 8U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = "http://ns.adobe.com/xap/1.0/mm/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("Pantry[1]/InstanceID", "uuid:pantry-1");
    expect_text("Pantry[1]/dc:format", "image/jpeg");
    expect_text("Pantry[1]/DerivedFrom/stRef:documentID",
                "xmp.did:pantry-source");
    expect_text("Pantry[1]/History[1]/stEvt:action", "copied");
    expect_text("Pantry[1]/Versions[1]/stVer:comments", "Embedded component");
    expect_text("Pantry[1]/Versions[1]/stVer:event/stEvt:softwareAgent",
                "Pantry Writer");
    expect_text("Pantry[1]/"
                "nsu_68747470733a2f2f6578616d706c652e696e76616c69642f786d702f"
                "76656e646f722f:Payload",
                "opaque-vendor-data");
    expect_text("Pantry[1]/nsu_75726e3a6f70656e6d6574613a6f74686572:Payload",
                "other-vendor-data");
}

TEST(XmpDecodeTest, DecodesLegacyUnqualifiedXmpMmStructuredChildren)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/'>"
          "<xmpMM:DerivedFrom rdf:parseType='Resource'>"
          "<documentID>xmp.did:base</documentID>"
          "<instanceID>xmp.iid:base</instanceID>"
          "<manageTo>https://example.invalid/base</manageTo>"
          "</xmpMM:DerivedFrom>"
          "<xmpMM:RenditionOf rdf:parseType='Resource'>"
          "<filePath>/tmp/rendition.jpg</filePath>"
          "</xmpMM:RenditionOf>"
          "<xmpMM:Manifest><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<linkForm>EmbedByReference</linkForm>"
          "<reference rdf:parseType='Resource'>"
          "<filePath>/tmp/manifest.dat</filePath>"
          "<manageUI>https://example.invalid/manage</manageUI>"
          "</reference>"
          "</rdf:li>"
          "</rdf:Seq></xmpMM:Manifest>"
          "<xmpMM:Versions><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<version>1.0</version>"
          "<event rdf:parseType='Resource'>"
          "<action>saved</action>"
          "<parameters>chapter=1</parameters>"
          "</event>"
          "</rdf:li>"
          "</rdf:Seq></xmpMM:Versions>"
          "<xmpMM:Pantry><rdf:Bag>"
          "<rdf:li rdf:parseType='Resource'>"
          "<InstanceID>uuid:pantry-1</InstanceID>"
          "<format>image/jpeg</format>"
          "</rdf:li>"
          "</rdf:Bag></xmpMM:Pantry>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    ASSERT_EQ(r.status, XmpDecodeStatus::Ok);
    store.finalize();

    const auto expect_text = [&](std::string_view path,
                                 std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = "http://ns.adobe.com/xap/1.0/mm/";
        key.data.xmp_property.property_path = path;
        const std::span<const EntryId> ids  = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("DerivedFrom/documentID", "xmp.did:base");
    expect_text("DerivedFrom/instanceID", "xmp.iid:base");
    expect_text("DerivedFrom/manageTo", "https://example.invalid/base");
    expect_text("RenditionOf/filePath", "/tmp/rendition.jpg");
    expect_text("Manifest[1]/linkForm", "EmbedByReference");
    expect_text("Manifest[1]/reference/filePath", "/tmp/manifest.dat");
    expect_text("Manifest[1]/reference/manageUI",
                "https://example.invalid/manage");
    expect_text("Versions[1]/version", "1.0");
    expect_text("Versions[1]/event/action", "saved");
    expect_text("Versions[1]/event/parameters", "chapter=1");
    expect_text("Pantry[1]/InstanceID", "uuid:pantry-1");
    expect_text("Pantry[1]/format", "image/jpeg");
}

TEST(XmpDecodeTest, DecodesAdobeStructuredWorkflowNamespaces)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmpBJ='http://ns.adobe.com/xap/1.0/bj/' "
          "xmlns:stJob='http://ns.adobe.com/xap/1.0/sType/Job#' "
          "xmlns:xmpTPg='http://ns.adobe.com/xap/1.0/t/pg/' "
          "xmlns:stDim='http://ns.adobe.com/xap/1.0/sType/Dimensions#' "
          "xmlns:stFnt='http://ns.adobe.com/xap/1.0/sType/Font#' "
          "xmlns:xmpG='http://ns.adobe.com/xap/1.0/g/' "
          "xmlns:xmpDM='http://ns.adobe.com/xmp/1.0/DynamicMedia/'>"
          "<xmpBJ:JobRef><rdf:Bag>"
          "<rdf:li rdf:parseType='Resource'>"
          "<stJob:id>job-1</stJob:id>"
          "<stJob:name>Layout Pass</stJob:name>"
          "<stJob:url>https://example.test/job/1</stJob:url>"
          "</rdf:li>"
          "</rdf:Bag></xmpBJ:JobRef>"
          "<xmpTPg:MaxPageSize rdf:parseType='Resource'>"
          "<stDim:w>8.5</stDim:w>"
          "<stDim:h>11</stDim:h>"
          "<stDim:unit>inch</stDim:unit>"
          "</xmpTPg:MaxPageSize>"
          "<xmpTPg:Fonts><rdf:Bag>"
          "<rdf:li rdf:parseType='Resource'>"
          "<stFnt:fontName>Source Serif</stFnt:fontName>"
          "<stFnt:childFontFiles><rdf:Seq>"
          "<rdf:li>SourceSerif-Regular.otf</rdf:li>"
          "<rdf:li>SourceSerif-It.otf</rdf:li>"
          "</rdf:Seq></stFnt:childFontFiles>"
          "</rdf:li>"
          "</rdf:Bag></xmpTPg:Fonts>"
          "<xmpTPg:Colorants><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<xmpG:swatchName>Process Cyan</xmpG:swatchName>"
          "<xmpG:mode>CMYK</xmpG:mode>"
          "</rdf:li>"
          "</rdf:Seq></xmpTPg:Colorants>"
          "<xmpTPg:SwatchGroups><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<xmpG:groupName>Brand Colors</xmpG:groupName>"
          "<xmpG:groupType>1</xmpG:groupType>"
          "<xmpTPg:Colorants><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<xmpG:swatchName>Accent Orange</xmpG:swatchName>"
          "<xmpG:mode>RGB</xmpG:mode>"
          "</rdf:li>"
          "</rdf:Seq></xmpTPg:Colorants>"
          "</rdf:li>"
          "</rdf:Seq></xmpTPg:SwatchGroups>"
          "<xmpDM:ProjectRef rdf:parseType='Resource'>"
          "<xmpDM:path>/proj/edit.prproj</xmpDM:path>"
          "<xmpDM:type>movie</xmpDM:type>"
          "</xmpDM:ProjectRef>"
          "<xmpDM:altTimecode rdf:parseType='Resource'>"
          "<xmpDM:timeFormat>2997DropTimecode</xmpDM:timeFormat>"
          "<xmpDM:timeValue>00:00:10:12</xmpDM:timeValue>"
          "<xmpDM:value>312</xmpDM:value>"
          "</xmpDM:altTimecode>"
          "<xmpDM:startTimecode rdf:parseType='Resource'>"
          "<xmpDM:timeFormat>2997NonDropTimecode</xmpDM:timeFormat>"
          "<xmpDM:timeValue>01:00:00:00</xmpDM:timeValue>"
          "<xmpDM:value>107892</xmpDM:value>"
          "</xmpDM:startTimecode>"
          "<xmpDM:duration rdf:parseType='Resource'>"
          "<xmpDM:scale>1/48000</xmpDM:scale>"
          "<xmpDM:value>96000</xmpDM:value>"
          "</xmpDM:duration>"
          "<xmpDM:introTime rdf:parseType='Resource'>"
          "<xmpDM:scale>1/1000</xmpDM:scale>"
          "<xmpDM:value>2500</xmpDM:value>"
          "</xmpDM:introTime>"
          "<xmpDM:outCue rdf:parseType='Resource'>"
          "<xmpDM:scale>1/1000</xmpDM:scale>"
          "<xmpDM:value>18000</xmpDM:value>"
          "</xmpDM:outCue>"
          "<xmpDM:relativeTimestamp rdf:parseType='Resource'>"
          "<xmpDM:scale>1/1000</xmpDM:scale>"
          "<xmpDM:value>450</xmpDM:value>"
          "</xmpDM:relativeTimestamp>"
          "<xmpDM:videoFrameSize rdf:parseType='Resource'>"
          "<stDim:w>1920</stDim:w>"
          "<stDim:h>1080</stDim:h>"
          "<stDim:unit>pixel</stDim:unit>"
          "</xmpDM:videoFrameSize>"
          "<xmpDM:videoAlphaPremultipleColor rdf:parseType='Resource'>"
          "<xmpG:mode>RGB</xmpG:mode>"
          "<xmpG:red>255</xmpG:red>"
          "<xmpG:green>0</xmpG:green>"
          "<xmpG:blue>255</xmpG:blue>"
          "</xmpDM:videoAlphaPremultipleColor>"
          "<xmpDM:beatSpliceParams rdf:parseType='Resource'>"
          "<xmpDM:riseInDecibel>3.5</xmpDM:riseInDecibel>"
          "<xmpDM:riseInTimeDuration rdf:parseType='Resource'>"
          "<xmpDM:scale>1/1000</xmpDM:scale>"
          "<xmpDM:value>1200</xmpDM:value>"
          "</xmpDM:riseInTimeDuration>"
          "<xmpDM:useFileBeatsMarker>True</xmpDM:useFileBeatsMarker>"
          "</xmpDM:beatSpliceParams>"
          "<xmpDM:markers rdf:parseType='Resource'>"
          "<xmpDM:name>Verse 1</xmpDM:name>"
          "<xmpDM:startTime>00:00:05.000</xmpDM:startTime>"
          "<xmpDM:cuePointParams rdf:parseType='Resource'>"
          "<xmpDM:key>chapter</xmpDM:key>"
          "<xmpDM:value>intro</xmpDM:value>"
          "</xmpDM:cuePointParams>"
          "</xmpDM:markers>"
          "<xmpDM:resampleParams rdf:parseType='Resource'>"
          "<xmpDM:quality>high</xmpDM:quality>"
          "</xmpDM:resampleParams>"
          "<xmpDM:timeScaleParams rdf:parseType='Resource'>"
          "<xmpDM:frameOverlappingPercentage>12.5</xmpDM:frameOverlappingPercentage>"
          "<xmpDM:frameSize>48</xmpDM:frameSize>"
          "<xmpDM:quality>medium</xmpDM:quality>"
          "</xmpDM:timeScaleParams>"
          "<xmpDM:contributedMedia><rdf:Bag>"
          "<rdf:li rdf:parseType='Resource'>"
          "<xmpDM:path>/media/broll.mov</xmpDM:path>"
          "<xmpDM:managed>True</xmpDM:managed>"
          "<xmpDM:track>V1</xmpDM:track>"
          "<xmpDM:webStatement>https://example.test/media/broll</xmpDM:webStatement>"
          "<xmpDM:duration rdf:parseType='Resource'>"
          "<xmpDM:scale>1/24000</xmpDM:scale>"
          "<xmpDM:value>48000</xmpDM:value>"
          "</xmpDM:duration>"
          "<xmpDM:startTime rdf:parseType='Resource'>"
          "<xmpDM:scale>1/24000</xmpDM:scale>"
          "<xmpDM:value>1200</xmpDM:value>"
          "</xmpDM:startTime>"
          "</rdf:li>"
          "</rdf:Bag></xmpDM:contributedMedia>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 58U);

    store.finalize();

    auto expect_text = [&](std::string_view schema_ns, std::string_view path,
                           std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = schema_ns;
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("http://ns.adobe.com/xap/1.0/bj/", "JobRef[1]/stJob:id",
                "job-1");
    expect_text("http://ns.adobe.com/xap/1.0/bj/", "JobRef[1]/stJob:name",
                "Layout Pass");
    expect_text("http://ns.adobe.com/xap/1.0/bj/", "JobRef[1]/stJob:url",
                "https://example.test/job/1");
    expect_text("http://ns.adobe.com/xap/1.0/t/pg/", "MaxPageSize/stDim:w",
                "8.5");
    expect_text("http://ns.adobe.com/xap/1.0/t/pg/", "MaxPageSize/stDim:h",
                "11");
    expect_text("http://ns.adobe.com/xap/1.0/t/pg/", "MaxPageSize/stDim:unit",
                "inch");
    expect_text("http://ns.adobe.com/xap/1.0/t/pg/", "Fonts[1]/stFnt:fontName",
                "Source Serif");
    expect_text("http://ns.adobe.com/xap/1.0/t/pg/",
                "Fonts[1]/stFnt:childFontFiles[1]", "SourceSerif-Regular.otf");
    expect_text("http://ns.adobe.com/xap/1.0/t/pg/",
                "Fonts[1]/stFnt:childFontFiles[2]", "SourceSerif-It.otf");
    expect_text("http://ns.adobe.com/xap/1.0/t/pg/",
                "Colorants[1]/xmpG:swatchName", "Process Cyan");
    expect_text("http://ns.adobe.com/xap/1.0/t/pg/", "Colorants[1]/xmpG:mode",
                "CMYK");
    expect_text("http://ns.adobe.com/xap/1.0/t/pg/",
                "SwatchGroups[1]/xmpG:groupName", "Brand Colors");
    expect_text("http://ns.adobe.com/xap/1.0/t/pg/",
                "SwatchGroups[1]/xmpG:groupType", "1");
    expect_text("http://ns.adobe.com/xap/1.0/t/pg/",
                "SwatchGroups[1]/Colorants[1]/xmpG:swatchName",
                "Accent Orange");
    expect_text("http://ns.adobe.com/xap/1.0/t/pg/",
                "SwatchGroups[1]/Colorants[1]/xmpG:mode", "RGB");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/", "ProjectRef/path",
                "/proj/edit.prproj");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/", "ProjectRef/type",
                "movie");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "altTimecode/timeFormat", "2997DropTimecode");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "altTimecode/timeValue", "00:00:10:12");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "altTimecode/value", "312");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "startTimecode/timeFormat", "2997NonDropTimecode");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "startTimecode/timeValue", "01:00:00:00");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "startTimecode/value", "107892");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/", "duration/scale",
                "1/48000");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/", "duration/value",
                "96000");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/", "introTime/scale",
                "1/1000");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/", "introTime/value",
                "2500");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/", "outCue/scale",
                "1/1000");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/", "outCue/value",
                "18000");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "relativeTimestamp/scale", "1/1000");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "relativeTimestamp/value", "450");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "videoFrameSize/stDim:w", "1920");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "videoFrameSize/stDim:h", "1080");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "videoFrameSize/stDim:unit", "pixel");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "videoAlphaPremultipleColor/xmpG:mode", "RGB");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "videoAlphaPremultipleColor/xmpG:red", "255");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "videoAlphaPremultipleColor/xmpG:green", "0");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "videoAlphaPremultipleColor/xmpG:blue", "255");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "beatSpliceParams/riseInDecibel", "3.5");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "beatSpliceParams/riseInTimeDuration/scale", "1/1000");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "beatSpliceParams/riseInTimeDuration/value", "1200");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "beatSpliceParams/useFileBeatsMarker", "True");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/", "markers/name",
                "Verse 1");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "markers/startTime", "00:00:05.000");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "markers/cuePointParams/key", "chapter");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "markers/cuePointParams/value", "intro");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "resampleParams/quality", "high");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "timeScaleParams/frameOverlappingPercentage", "12.5");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "timeScaleParams/frameSize", "48");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "timeScaleParams/quality", "medium");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "contributedMedia[1]/path", "/media/broll.mov");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "contributedMedia[1]/managed", "True");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "contributedMedia[1]/track", "V1");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "contributedMedia[1]/webStatement",
                "https://example.test/media/broll");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "contributedMedia[1]/duration/scale", "1/24000");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "contributedMedia[1]/duration/value", "48000");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "contributedMedia[1]/startTime/scale", "1/24000");
    expect_text("http://ns.adobe.com/xmp/1.0/DynamicMedia/",
                "contributedMedia[1]/startTime/value", "1200");
}

TEST(XmpDecodeTest, DecodesXmpDmTracksStructuredChildren)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmpDM='http://ns.adobe.com/xmp/1.0/DynamicMedia/'>"
          "<xmpDM:Tracks><rdf:Bag>"
          "<rdf:li rdf:parseType='Resource'>"
          "<xmpDM:trackName>Dialogue</xmpDM:trackName>"
          "<xmpDM:trackType>Audio</xmpDM:trackType>"
          "<xmpDM:frameRate>f24000</xmpDM:frameRate>"
          "<xmpDM:markers rdf:parseType='Resource'>"
          "<xmpDM:name>Scene 1</xmpDM:name>"
          "<xmpDM:startTime>00:00:01.000</xmpDM:startTime>"
          "<xmpDM:cuePointParams rdf:parseType='Resource'>"
          "<xmpDM:key>chapter</xmpDM:key>"
          "<xmpDM:value>scene-1</xmpDM:value>"
          "</xmpDM:cuePointParams>"
          "</xmpDM:markers>"
          "</rdf:li>"
          "</rdf:Bag></xmpDM:Tracks>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 7U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://ns.adobe.com/xmp/1.0/DynamicMedia/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("Tracks[1]/trackName", "Dialogue");
    expect_text("Tracks[1]/trackType", "Audio");
    expect_text("Tracks[1]/frameRate", "f24000");
    expect_text("Tracks[1]/markers/name", "Scene 1");
    expect_text("Tracks[1]/markers/startTime", "00:00:01.000");
    expect_text("Tracks[1]/markers/cuePointParams/key", "chapter");
    expect_text("Tracks[1]/markers/cuePointParams/value", "scene-1");
}

TEST(XmpDecodeTest, DecodesXmpMmManifestStructuredChildren)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
          "xmlns:stMfs='http://ns.adobe.com/xap/1.0/sType/ManifestItem#' "
          "xmlns:stRef='http://ns.adobe.com/xap/1.0/sType/ResourceRef#'>"
          "<xmpMM:Manifest><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<stMfs:linkForm>EmbedByReference</stMfs:linkForm>"
          "<stMfs:reference rdf:parseType='Resource'>"
          "<stRef:filePath>C:\\some path\\file.ext</stRef:filePath>"
          "</stMfs:reference>"
          "</rdf:li>"
          "</rdf:Seq></xmpMM:Manifest>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = "http://ns.adobe.com/xap/1.0/mm/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("Manifest[1]/stMfs:linkForm", "EmbedByReference");
    expect_text("Manifest[1]/stMfs:reference/stRef:filePath",
                "C:\\some path\\file.ext");
}

TEST(XmpDecodeTest, DecodesXmpMmVersionsStructuredChildren)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
          "xmlns:stVer='http://ns.adobe.com/xap/1.0/sType/Version#' "
          "xmlns:stEvt='http://ns.adobe.com/xap/1.0/sType/ResourceEvent#'>"
          "<xmpMM:Versions><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<stVer:version>1.0</stVer:version>"
          "<stVer:comments>Initial import</stVer:comments>"
          "<stVer:modifier>OpenMeta</stVer:modifier>"
          "<stVer:modifyDate>2026-04-16T10:15:00Z</stVer:modifyDate>"
          "<stVer:event rdf:parseType='Resource'>"
          "<stEvt:action>saved</stEvt:action>"
          "<stEvt:changed>/metadata</stEvt:changed>"
          "<stEvt:when>2026-04-16T10:15:00Z</stEvt:when>"
          "</stVer:event>"
          "</rdf:li>"
          "</rdf:Seq></xmpMM:Versions>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 7U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = "http://ns.adobe.com/xap/1.0/mm/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("Versions[1]/stVer:version", "1.0");
    expect_text("Versions[1]/stVer:comments", "Initial import");
    expect_text("Versions[1]/stVer:modifier", "OpenMeta");
    expect_text("Versions[1]/stVer:modifyDate", "2026-04-16T10:15:00Z");
    expect_text("Versions[1]/stVer:event/stEvt:action", "saved");
    expect_text("Versions[1]/stVer:event/stEvt:changed", "/metadata");
    expect_text("Versions[1]/stVer:event/stEvt:when", "2026-04-16T10:15:00Z");
}

TEST(XmpDecodeTest, ResolvesIptcImageRegionBoundaryGeometry)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
          "<Iptc4xmpExt:ImageRegion><rdf:Bag>"
          "<rdf:li rdf:parseType='Resource'>"
          "<Iptc4xmpExt:RegionBoundary rdf:parseType='Resource'>"
          "<Iptc4xmpExt:rbShape>rectangle</Iptc4xmpExt:rbShape>"
          "<Iptc4xmpExt:rbUnit>pixel</Iptc4xmpExt:rbUnit>"
          "<Iptc4xmpExt:rbX>10</Iptc4xmpExt:rbX>"
          "<Iptc4xmpExt:rbY>20</Iptc4xmpExt:rbY>"
          "<Iptc4xmpExt:rbW>300</Iptc4xmpExt:rbW>"
          "<Iptc4xmpExt:rbH>200</Iptc4xmpExt:rbH>"
          "</Iptc4xmpExt:RegionBoundary>"
          "</rdf:li></rdf:Bag></Iptc4xmpExt:ImageRegion>"
          "</rdf:Description></rdf:RDF></x:xmpmeta>";
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult decoded = decode_xmp_packet(bytes, store);
    ASSERT_EQ(decoded.status, XmpDecodeStatus::Ok);
    store.finalize();

    const MetadataConceptResolution descriptive
        = resolve_metadata_concept(store, MetadataConceptKind::Descriptive);
    const MetadataConceptCandidate* boundary = nullptr;
    for (size_t i = 0U; i < descriptive.candidates.size(); ++i) {
        const MetadataConceptCandidate& candidate = descriptive.candidates[i];
        if (candidate.record_kind
                == MetadataConceptRecordKind::ImageRegionBoundary
            && candidate.role == MetadataConceptRole::RegionBoundary) {
            boundary = &candidate;
            break;
        }
    }

    ASSERT_NE(boundary, nullptr);
    EXPECT_EQ(boundary->record_scope, "ImageRegion[1]/Boundary");
    EXPECT_EQ(boundary->image_region_shape,
              MetadataImageRegionShape::Rectangle);
    EXPECT_EQ(boundary->image_region_coordinate_unit,
              MetadataImageRegionCoordinateUnit::Pixel);
    ASSERT_TRUE(boundary->has_rect);
    EXPECT_DOUBLE_EQ(boundary->rect[0], 10.0);
    EXPECT_DOUBLE_EQ(boundary->rect[1], 20.0);
    EXPECT_DOUBLE_EQ(boundary->rect[2], 300.0);
    EXPECT_DOUBLE_EQ(boundary->rect[3], 200.0);
}

TEST(XmpDecodeTest, DecodesAltTextEntriesWithXmlLangPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description xmlns:dc='http://purl.org/dc/elements/1.1/'>"
          "<dc:title><rdf:Alt>"
          "<rdf:li xml:lang='x-default'>Default title</rdf:li>"
          "<rdf:li xml:lang='fr-FR'>Titre</rdf:li>"
          "</rdf:Alt></dc:title>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind                        = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns = "http://purl.org/dc/elements/1.1/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("title[@xml:lang=x-default]", "Default title");
    expect_text("title[@xml:lang=fr-FR]", "Titre");
}

TEST(XmpDecodeTest, DecodesStructuredResourcePaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
          "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
          "<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"
          "<Iptc4xmpCore:CiUrlWork>https://example.test/contact</Iptc4xmpCore:CiUrlWork>"
          "</Iptc4xmpCore:CreatorContactInfo>"
          "<Iptc4xmpCore:LocationCreated rdf:parseType='Resource'>"
          "<Iptc4xmpCore:City>Paris</Iptc4xmpCore:City>"
          "<Iptc4xmpCore:CountryName>France</Iptc4xmpCore:CountryName>"
          "</Iptc4xmpCore:LocationCreated>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 4U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("CreatorContactInfo/CiEmailWork", "editor@example.test");
    expect_text("CreatorContactInfo/CiUrlWork", "https://example.test/contact");
    expect_text("LocationCreated/City", "Paris");
    expect_text("LocationCreated/CountryName", "France");
}

TEST(XmpDecodeTest, DecodesIndexedStructuredResourcePaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:plus='http://ns.useplus.org/ldf/xmp/1.0/'>"
          "<plus:Licensee><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<plus:LicenseeName>Example Archive</plus:LicenseeName>"
          "<plus:LicenseeURL>https://example.test/archive</plus:LicenseeURL>"
          "</rdf:li>"
          "<rdf:li rdf:parseType='Resource'>"
          "<plus:LicenseeName>Editorial Partner</plus:LicenseeName>"
          "<plus:LicenseeID>lic-002</plus:LicenseeID>"
          "</rdf:li>"
          "</rdf:Seq></plus:Licensee>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 4U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind                        = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns = "http://ns.useplus.org/ldf/xmp/1.0/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("Licensee[1]/LicenseeName", "Example Archive");
    expect_text("Licensee[1]/LicenseeURL", "https://example.test/archive");
    expect_text("Licensee[2]/LicenseeName", "Editorial Partner");
    expect_text("Licensee[2]/LicenseeID", "lic-002");
}

TEST(XmpDecodeTest, DecodesIptc4xmpExtIndexedStructuredPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
          "<Iptc4xmpExt:LocationShown><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<Iptc4xmpExt:City>Paris</Iptc4xmpExt:City>"
          "<Iptc4xmpExt:CountryName>France</Iptc4xmpExt:CountryName>"
          "</rdf:li>"
          "<rdf:li rdf:parseType='Resource'>"
          "<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"
          "<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"
          "</rdf:li>"
          "</rdf:Seq></Iptc4xmpExt:LocationShown>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 4U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("LocationShown[1]/City", "Paris");
    expect_text("LocationShown[1]/CountryName", "France");
    expect_text("LocationShown[2]/City", "Kyoto");
    expect_text("LocationShown[2]/CountryName", "Japan");
}

TEST(XmpDecodeTest, DecodesStructuredChildLangAltPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
          "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
          "<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"
          "<Iptc4xmpCore:CiAdrCity><rdf:Alt>"
          "<rdf:li xml:lang='x-default'>Tokyo</rdf:li>"
          "<rdf:li xml:lang='ja-JP'>東京</rdf:li>"
          "</rdf:Alt></Iptc4xmpCore:CiAdrCity>"
          "</Iptc4xmpCore:CreatorContactInfo>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 3U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("CreatorContactInfo/CiEmailWork", "editor@example.test");
    expect_text("CreatorContactInfo/CiAdrCity[@xml:lang=x-default]", "Tokyo");
    expect_text("CreatorContactInfo/CiAdrCity[@xml:lang=ja-JP]", "東京");
}

TEST(XmpDecodeTest, DecodesIndexedStructuredChildLangAltPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
          "<Iptc4xmpExt:LocationShown><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"
          "<Iptc4xmpExt:Sublocation><rdf:Alt>"
          "<rdf:li xml:lang='x-default'>Gion</rdf:li>"
          "<rdf:li xml:lang='ja-JP'>祇園</rdf:li>"
          "</rdf:Alt></Iptc4xmpExt:Sublocation>"
          "</rdf:li>"
          "</rdf:Seq></Iptc4xmpExt:LocationShown>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 3U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("LocationShown[1]/CountryName", "Japan");
    expect_text("LocationShown[1]/Sublocation[@xml:lang=x-default]", "Gion");
    expect_text("LocationShown[1]/Sublocation[@xml:lang=ja-JP]", "祇園");
}

TEST(XmpDecodeTest, DecodesStructuredChildIndexedPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
          "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
          "<Iptc4xmpCore:CiAdrExtadr><rdf:Seq>"
          "<rdf:li>Line 1</rdf:li>"
          "<rdf:li>Line 2</rdf:li>"
          "</rdf:Seq></Iptc4xmpCore:CiAdrExtadr>"
          "</Iptc4xmpCore:CreatorContactInfo>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("CreatorContactInfo/CiAdrExtadr[1]", "Line 1");
    expect_text("CreatorContactInfo/CiAdrExtadr[2]", "Line 2");
}

TEST(XmpDecodeTest, DecodesIndexedStructuredChildIndexedPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
          "<Iptc4xmpExt:LocationShown><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<Iptc4xmpExt:Sublocation><rdf:Seq>"
          "<rdf:li>Gion</rdf:li>"
          "<rdf:li>Hanamikoji</rdf:li>"
          "</rdf:Seq></Iptc4xmpExt:Sublocation>"
          "</rdf:li>"
          "</rdf:Seq></Iptc4xmpExt:LocationShown>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("LocationShown[1]/Sublocation[1]", "Gion");
    expect_text("LocationShown[1]/Sublocation[2]", "Hanamikoji");
}

TEST(XmpDecodeTest, DecodesNestedStructuredResourcePaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
          "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
          "<Iptc4xmpCore:CiAdrRegion rdf:parseType='Resource'>"
          "<Iptc4xmpCore:ProvinceName>Tokyo</Iptc4xmpCore:ProvinceName>"
          "<Iptc4xmpCore:ProvinceCode>13</Iptc4xmpCore:ProvinceCode>"
          "</Iptc4xmpCore:CiAdrRegion>"
          "</Iptc4xmpCore:CreatorContactInfo>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("CreatorContactInfo/CiAdrRegion/ProvinceName", "Tokyo");
    expect_text("CreatorContactInfo/CiAdrRegion/ProvinceCode", "13");
}

TEST(XmpDecodeTest, DecodesIndexedNestedStructuredResourcePaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
          "<Iptc4xmpExt:LocationShown><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<Iptc4xmpExt:Address rdf:parseType='Resource'>"
          "<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"
          "<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"
          "</Iptc4xmpExt:Address>"
          "</rdf:li>"
          "</rdf:Seq></Iptc4xmpExt:LocationShown>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("LocationShown[1]/Address/City", "Kyoto");
    expect_text("LocationShown[1]/Address/CountryName", "Japan");
}

TEST(XmpDecodeTest, DecodesNestedStructuredChildLangAltPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
          "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
          "<Iptc4xmpCore:CiAdrRegion rdf:parseType='Resource'>"
          "<Iptc4xmpCore:ProvinceName><rdf:Alt>"
          "<rdf:li xml:lang='x-default'>Tokyo</rdf:li>"
          "<rdf:li xml:lang='ja-JP'>東京</rdf:li>"
          "</rdf:Alt></Iptc4xmpCore:ProvinceName>"
          "</Iptc4xmpCore:CiAdrRegion>"
          "</Iptc4xmpCore:CreatorContactInfo>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text(
        "CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=x-default]",
        "Tokyo");
    expect_text("CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=ja-JP]",
                "東京");
}

TEST(XmpDecodeTest, DecodesNestedStructuredChildIndexedPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
          "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
          "<Iptc4xmpCore:CiAdrRegion rdf:parseType='Resource'>"
          "<Iptc4xmpCore:ProvinceCode><rdf:Seq>"
          "<rdf:li>13</rdf:li>"
          "<rdf:li>JP-13</rdf:li>"
          "</rdf:Seq></Iptc4xmpCore:ProvinceCode>"
          "</Iptc4xmpCore:CiAdrRegion>"
          "</Iptc4xmpCore:CreatorContactInfo>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("CreatorContactInfo/CiAdrRegion/ProvinceCode[1]", "13");
    expect_text("CreatorContactInfo/CiAdrRegion/ProvinceCode[2]", "JP-13");
}

TEST(XmpDecodeTest, DecodesIndexedNestedStructuredChildLangAltPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
          "<Iptc4xmpExt:LocationShown><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<Iptc4xmpExt:Address rdf:parseType='Resource'>"
          "<Iptc4xmpExt:City><rdf:Alt>"
          "<rdf:li xml:lang='x-default'>Kyoto</rdf:li>"
          "<rdf:li xml:lang='ja-JP'>京都</rdf:li>"
          "</rdf:Alt></Iptc4xmpExt:City>"
          "</Iptc4xmpExt:Address>"
          "</rdf:li>"
          "</rdf:Seq></Iptc4xmpExt:LocationShown>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("LocationShown[1]/Address/City[@xml:lang=x-default]", "Kyoto");
    expect_text("LocationShown[1]/Address/City[@xml:lang=ja-JP]", "京都");
}

TEST(XmpDecodeTest, DecodesIndexedNestedStructuredChildIndexedPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
          "<Iptc4xmpExt:LocationShown><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<Iptc4xmpExt:Address rdf:parseType='Resource'>"
          "<Iptc4xmpExt:CountryCode><rdf:Seq>"
          "<rdf:li>JP</rdf:li>"
          "<rdf:li>JP-26</rdf:li>"
          "</rdf:Seq></Iptc4xmpExt:CountryCode>"
          "</Iptc4xmpExt:Address>"
          "</rdf:li>"
          "</rdf:Seq></Iptc4xmpExt:LocationShown>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("LocationShown[1]/Address/CountryCode[1]", "JP");
    expect_text("LocationShown[1]/Address/CountryCode[2]", "JP-26");
}

TEST(XmpDecodeTest, TrimsTrailingNulPadding)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'/>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    std::string padded = xmp;
    padded.append(16, '\0');

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               padded.data()),
                                           padded.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 1U);
}

TEST(XmpDecodeTest, DecodesMixedNamespaceStructuredLocationDetailsChildren)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/' "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmlns:exif='http://ns.adobe.com/exif/1.0/'>"
          "<Iptc4xmpExt:LocationShown><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<xmp:Identifier><rdf:Bag>"
          "<rdf:li>loc-001</rdf:li>"
          "<rdf:li>loc-002</rdf:li>"
          "</rdf:Bag></xmp:Identifier>"
          "<exif:GPSLatitude>41,24.5N</exif:GPSLatitude>"
          "<exif:GPSLongitude>2,9E</exif:GPSLongitude>"
          "</rdf:li>"
          "</rdf:Seq></Iptc4xmpExt:LocationShown>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    ASSERT_EQ(r.status, XmpDecodeStatus::Ok);
    store.finalize();

    const auto expect_text = [&](std::string_view path,
                                 std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
        key.data.xmp_property.property_path = path;
        const std::span<const EntryId> ids  = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("LocationShown[1]/xmp:Identifier[1]", "loc-001");
    expect_text("LocationShown[1]/xmp:Identifier[2]", "loc-002");
    expect_text("LocationShown[1]/exif:GPSLatitude", "41,24.5N");
    expect_text("LocationShown[1]/exif:GPSLongitude", "2,9E");
}

TEST(XmpDecodeTest,
     DecodesLegacyUnqualifiedMixedNamespaceLocationDetailsChildren)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
          "<Iptc4xmpExt:LocationShown><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<Identifier><rdf:Bag>"
          "<rdf:li>loc-001</rdf:li>"
          "<rdf:li>loc-002</rdf:li>"
          "</rdf:Bag></Identifier>"
          "<GPSLatitude>41,24.5N</GPSLatitude>"
          "<GPSLongitude>2,9E</GPSLongitude>"
          "<GPSAltitude>35.5</GPSAltitude>"
          "<GPSAltitudeRef>Above Sea Level</GPSAltitudeRef>"
          "</rdf:li>"
          "</rdf:Seq></Iptc4xmpExt:LocationShown>"
          "<Iptc4xmpExt:LocationCreated rdf:parseType='Resource'>"
          "<Identifier><rdf:Bag><rdf:li>par-001</rdf:li></rdf:Bag></Identifier>"
          "<GPSLatitude>48,51.507N</GPSLatitude>"
          "</Iptc4xmpExt:LocationCreated>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    ASSERT_EQ(r.status, XmpDecodeStatus::Ok);
    store.finalize();

    const auto expect_text = [&](std::string_view path,
                                 std::string_view expected) {
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
        key.data.xmp_property.property_path = path;
        const std::span<const EntryId> ids  = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("LocationShown[1]/Identifier[1]", "loc-001");
    expect_text("LocationShown[1]/Identifier[2]", "loc-002");
    expect_text("LocationShown[1]/GPSLatitude", "41,24.5N");
    expect_text("LocationShown[1]/GPSLongitude", "2,9E");
    expect_text("LocationShown[1]/GPSAltitude", "35.5");
    expect_text("LocationShown[1]/GPSAltitudeRef", "Above Sea Level");
    expect_text("LocationCreated/Identifier[1]", "par-001");
    expect_text("LocationCreated/GPSLatitude", "48,51.507N");
}

TEST(XmpDecodeTest, EstimateMatchesDecodeCounters)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'>"
          "<xmp:Rating>5</xmp:Rating>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    const XmpDecodeResult estimate = measure_xmp_packet(bytes);
    EXPECT_EQ(estimate.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(estimate.entries_decoded, 2U);

    MetaStore store;
    const XmpDecodeResult decoded = decode_xmp_packet(bytes, store);
    EXPECT_EQ(decoded.status, estimate.status);
    EXPECT_EQ(decoded.entries_decoded, estimate.entries_decoded);
}

TEST(XmpDecodeTest, EstimateRespectsPropertyLimitOverride)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'>"
          "<xmp:Rating>5</xmp:Rating>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    XmpDecodeOptions options;
    options.limits.max_properties  = 1U;
    const XmpDecodeResult estimate = measure_xmp_packet(bytes, options);
    EXPECT_EQ(estimate.status, XmpDecodeStatus::LimitExceeded);

    MetaStore store;
    const XmpDecodeResult decoded
        = decode_xmp_packet(bytes, store, EntryFlags::None, options);
    EXPECT_EQ(decoded.status, XmpDecodeStatus::LimitExceeded);
}

TEST(XmpDecodeTest, PreservesExplicitEmptyLeafValues)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:tiff='http://ns.adobe.com/tiff/1.0/'>"
          "<tiff:Artist/>"
          "<tiff:Copyright>   </tiff:Copyright>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    MetaKeyView artist_key;
    artist_key.kind                        = MetaKeyKind::XmpProperty;
    artist_key.data.xmp_property.schema_ns = "http://ns.adobe.com/tiff/1.0/";
    artist_key.data.xmp_property.property_path = "Artist";

    MetaKeyView copy_key;
    copy_key.kind                            = MetaKeyKind::XmpProperty;
    copy_key.data.xmp_property.schema_ns     = "http://ns.adobe.com/tiff/1.0/";
    copy_key.data.xmp_property.property_path = "Copyright";

    const std::span<const EntryId> artist_ids = store.find_all(artist_key);
    const std::span<const EntryId> copy_ids   = store.find_all(copy_key);
    ASSERT_EQ(artist_ids.size(), 1U);
    ASSERT_EQ(copy_ids.size(), 1U);

    const Entry& artist = store.entry(artist_ids[0]);
    const Entry& copy   = store.entry(copy_ids[0]);
    ASSERT_EQ(artist.value.kind, MetaValueKind::Text);
    ASSERT_EQ(copy.value.kind, MetaValueKind::Text);

    const std::span<const std::byte> artist_val = store.arena().span(
        artist.value.data.span);
    const std::span<const std::byte> copy_val = store.arena().span(
        copy.value.data.span);
    EXPECT_TRUE(artist_val.empty());
    EXPECT_TRUE(copy_val.empty());
}

TEST(XmpDecodeTest, SkipsLeadingMimePrefix)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'/>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    std::string blob = "application/rdf+xml";
    blob.push_back('\0');
    blob.append(xmp);
    blob.append(8, '\0');

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               blob.data()),
                                           blob.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 1U);
}

TEST(XmpDecodeTest, DecodesXmpToolkitOnXmpMetaRoot)
{
    const std::string xmp
        = "<?xpacket begin='' id='W5M0MpCehiHzreSzNTczkc9d' ?>"
          "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "x:xmptk='Adobe XMP Core 5.2-c004 1.136881, 2010/06/10-18:11:35'>"
          "</x:xmpmeta>"
          "<?xpacket end='w'?>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 1U);

    store.finalize();

    MetaKeyView key;
    key.kind                            = MetaKeyKind::XmpProperty;
    key.data.xmp_property.schema_ns     = "adobe:ns:meta/";
    key.data.xmp_property.property_path = "XMPToolkit";

    const std::span<const EntryId> ids = store.find_all(key);
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    ASSERT_EQ(e.value.kind, MetaValueKind::Text);
}

TEST(XmpDecodeTest, RejectsDoctypeAndEntityDeclarations)
{
    const std::string xmp
        = "<!DOCTYPE x:xmpmeta [<!ENTITY boom 'boom'>]>"
          "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'>"
          "<xmp:CreatorTool>&boom;</xmp:CreatorTool>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore strict_store;
    const XmpDecodeResult strict = decode_xmp_packet(bytes, strict_store);
    EXPECT_EQ(strict.status, XmpDecodeStatus::Malformed);
    EXPECT_EQ(strict.entries_decoded, 0U);

    MetaStore safe_store;
    XmpDecodeOptions options;
    options.malformed_mode     = XmpDecodeMalformedMode::OutputTruncated;
    const XmpDecodeResult safe = decode_xmp_packet(bytes, safe_store,
                                                   EntryFlags::None, options);
    EXPECT_EQ(safe.status, XmpDecodeStatus::OutputTruncated);
    EXPECT_EQ(safe.entries_decoded, 0U);
}

TEST(XmpDecodeTest, MalformedCanBeReportedAsOutputTruncated)
{
    std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'>"
          "<xmp:Rating>5";
    xmp.push_back(static_cast<char>(0x01));  // invalid XML control byte
    xmp += "</xmp:Rating>"
           "</rdf:Description>"
           "</rdf:RDF>"
           "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store_a;
    const XmpDecodeResult strict = decode_xmp_packet(bytes, store_a);
    EXPECT_EQ(strict.status, XmpDecodeStatus::Malformed);
    EXPECT_GE(strict.entries_decoded, 1U);

    MetaStore store_b;
    XmpDecodeOptions options;
    options.malformed_mode     = XmpDecodeMalformedMode::OutputTruncated;
    const XmpDecodeResult safe = decode_xmp_packet(bytes, store_b,
                                                   EntryFlags::None, options);
    EXPECT_EQ(safe.status, XmpDecodeStatus::OutputTruncated);
    EXPECT_EQ(safe.entries_decoded, strict.entries_decoded);
}

#else

TEST(XmpDecodeTest, ExpatNotEnabled)
{
    GTEST_SKIP()
        << "OPENMETA_HAS_EXPAT is not enabled; XMP decode is unavailable.";
}

#endif

}  // namespace openmeta
