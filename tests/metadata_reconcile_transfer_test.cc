// SPDX-License-Identifier: Apache-2.0

#include "custom_xmp_test_fixture.h"
#include "metadata_container_test_fixture.h"
#include "openmeta/container_scan.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_reconcile.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"
#include "openmeta/xmp_structure.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#    include <process.h>
#else
#    include <unistd.h>
#endif

namespace {
using Bytes = std::vector<std::byte>;

static constexpr std::array<std::byte, 4U> kJp2Signature = {
    std::byte { 0x0DU },
    std::byte { 0x0AU },
    std::byte { 0x87U },
    std::byte { 0x0AU },
};
static constexpr std::array<std::byte, 5U> kJp2FreePayload = {
    std::byte { 'j' }, std::byte { 'p' }, std::byte { '2' },
    std::byte { 'k' }, std::byte { 'p' },
};
// This sentinel is only for byte-preservation checks; it is not a codec stream.
static constexpr std::array<std::byte, 12U> kSyntheticJp2Codestream = {
    std::byte { 0xFFU }, std::byte { 0x4FU }, std::byte { 0xFFU },
    std::byte { 0x51U }, std::byte { 0x10U }, std::byte { 0x20U },
    std::byte { 0x30U }, std::byte { 0x40U }, std::byte { 0x50U },
    std::byte { 0x60U }, std::byte { 0x70U }, std::byte { 0x80U },
};
static constexpr std::array<std::byte, 5U> kBmffFreePayload = {
    std::byte { 'b' }, std::byte { 'm' }, std::byte { 'f' },
    std::byte { 'k' }, std::byte { 'p' },
};
static constexpr std::array<std::byte, 8U> kSyntheticBmffMedia = {
    std::byte { 0x11U }, std::byte { 0x22U }, std::byte { 0x33U },
    std::byte { 0x44U }, std::byte { 0x55U }, std::byte { 0x66U },
    std::byte { 0x77U }, std::byte { 0x88U },
};

static void
append_u16_be(Bytes* output, uint16_t value)
{
    output->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    output->push_back(static_cast<std::byte>(value & 0xFFU));
}

static void
append_u32_be(Bytes* output, uint32_t value)
{
    output->push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
    output->push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
    output->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    output->push_back(static_cast<std::byte>(value & 0xFFU));
}

static void
append_box(Bytes* output, uint32_t type, std::span<const std::byte> payload)
{
    append_u32_be(output, static_cast<uint32_t>(8U + payload.size()));
    append_u32_be(output, type);
    output->insert(output->end(), payload.begin(), payload.end());
}

struct BoxView final {
    uint32_t type = 0U;
    std::span<const std::byte> raw;
    std::span<const std::byte> payload;
};

static bool
read_u32_be(std::span<const std::byte> bytes, size_t offset,
            uint32_t* value) noexcept
{
    if (value == nullptr || offset > bytes.size()
        || bytes.size() - offset < 4U) {
        return false;
    }
    *value
        = (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset]))
           << 24U)
          | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset + 1U]))
             << 16U)
          | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset + 2U]))
             << 8U)
          | static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset + 3U]));
    return true;
}

static bool
read_u64_be(std::span<const std::byte> bytes, size_t offset,
            uint64_t* value) noexcept
{
    if (value == nullptr || offset > bytes.size()
        || bytes.size() - offset < 8U) {
        return false;
    }
    uint64_t decoded = 0U;
    for (size_t i = 0U; i < 8U; ++i) {
        decoded = (decoded << 8U) | std::to_integer<uint8_t>(bytes[offset + i]);
    }
    *value = decoded;
    return true;
}

static bool
read_top_level_box(std::span<const std::byte> bytes, size_t offset,
                   BoxView* result, size_t* next_offset) noexcept
{
    if (result == nullptr || next_offset == nullptr || offset > bytes.size()
        || bytes.size() - offset < 8U) {
        return false;
    }

    uint32_t short_size = 0U;
    uint32_t type       = 0U;
    if (!read_u32_be(bytes, offset, &short_size)
        || !read_u32_be(bytes, offset + 4U, &type)) {
        return false;
    }

    uint64_t box_size   = short_size;
    size_t header_size  = 8U;
    const size_t remain = bytes.size() - offset;
    if (short_size == 1U) {
        if (remain < 16U || !read_u64_be(bytes, offset + 8U, &box_size)) {
            return false;
        }
        header_size = 16U;
    } else if (short_size == 0U) {
        box_size = static_cast<uint64_t>(remain);
    }

    if (box_size < header_size || box_size > static_cast<uint64_t>(remain)) {
        return false;
    }
    const size_t checked_size = static_cast<size_t>(box_size);
    result->type              = type;
    result->raw               = bytes.subspan(offset, checked_size);
    result->payload           = bytes.subspan(offset + header_size,
                                              checked_size - header_size);
    *next_offset              = offset + checked_size;
    return true;
}

static bool
find_top_level_box(std::span<const std::byte> bytes, uint32_t type,
                   uint32_t occurrence, BoxView* result) noexcept
{
    if (result == nullptr) {
        return false;
    }
    size_t offset = 0U;
    uint32_t seen = 0U;
    while (offset < bytes.size()) {
        BoxView box;
        size_t next_offset = 0U;
        if (!read_top_level_box(bytes, offset, &box, &next_offset)) {
            return false;
        }
        if (box.type == type) {
            if (seen == occurrence) {
                *result = box;
                return true;
            }
            seen += 1U;
        }
        offset = next_offset;
    }
    return false;
}

static bool
same_bytes(std::span<const std::byte> left,
           std::span<const std::byte> right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0U; i < left.size(); ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}

struct Jp2Fixture final {
    Bytes bytes;
    uint32_t brand = 0U;
};

static Jp2Fixture
make_jp2_fixture(uint32_t brand)
{
    Jp2Fixture fixture;
    fixture.brand = brand;

    append_box(&fixture.bytes, openmeta::fourcc('j', 'P', ' ', ' '),
               kJp2Signature);

    Bytes ftyp_payload;
    append_u32_be(&ftyp_payload, brand);
    append_u32_be(&ftyp_payload, 0U);
    append_u32_be(&ftyp_payload, brand);
    append_box(&fixture.bytes, openmeta::fourcc('f', 't', 'y', 'p'),
               ftyp_payload);

    Bytes ihdr_payload;
    append_u32_be(&ihdr_payload, 1U);
    append_u32_be(&ihdr_payload, 1U);
    append_u16_be(&ihdr_payload, 1U);
    ihdr_payload.push_back(std::byte { 7U });
    ihdr_payload.push_back(std::byte { 7U });
    ihdr_payload.push_back(std::byte { 0U });
    ihdr_payload.push_back(std::byte { 0U });
    Bytes jp2h_payload;
    append_box(&jp2h_payload, openmeta::fourcc('i', 'h', 'd', 'r'),
               ihdr_payload);
    append_box(&fixture.bytes, openmeta::fourcc('j', 'p', '2', 'h'),
               jp2h_payload);
    append_box(&fixture.bytes, openmeta::fourcc('f', 'r', 'e', 'e'),
               kJp2FreePayload);
    append_box(&fixture.bytes, openmeta::fourcc('j', 'p', '2', 'c'),
               kSyntheticJp2Codestream);
    return fixture;
}

enum class BmffFixtureBrand : uint8_t { Heif, Avif, Cr3 };

struct BmffFixture final {
    Bytes bytes;
    uint32_t major_brand = 0U;
    std::array<uint32_t, 2U> compatible_brands {};
};

static BmffFixture
make_bmff_fixture(BmffFixtureBrand brand)
{
    BmffFixture fixture;
    switch (brand) {
    case BmffFixtureBrand::Heif:
        fixture.major_brand       = openmeta::fourcc('h', 'e', 'i', 'c');
        fixture.compatible_brands = { openmeta::fourcc('m', 'i', 'f', '1'),
                                      fixture.major_brand };
        break;
    case BmffFixtureBrand::Avif:
        fixture.major_brand       = openmeta::fourcc('a', 'v', 'i', 'f');
        fixture.compatible_brands = { openmeta::fourcc('m', 'i', 'f', '1'),
                                      fixture.major_brand };
        break;
    case BmffFixtureBrand::Cr3:
        fixture.major_brand       = openmeta::fourcc('c', 'r', 'x', ' ');
        fixture.compatible_brands = { fixture.major_brand,
                                      openmeta::fourcc('C', 'R', '3', ' ') };
        break;
    }

    Bytes ftyp_payload;
    append_u32_be(&ftyp_payload, fixture.major_brand);
    append_u32_be(&ftyp_payload, 0U);
    for (uint32_t compatible_brand : fixture.compatible_brands) {
        append_u32_be(&ftyp_payload, compatible_brand);
    }
    append_box(&fixture.bytes, openmeta::fourcc('f', 't', 'y', 'p'),
               ftyp_payload);
    append_box(&fixture.bytes, openmeta::fourcc('f', 'r', 'e', 'e'),
               kBmffFreePayload);
    append_box(&fixture.bytes, openmeta::fourcc('m', 'd', 'a', 't'),
               kSyntheticBmffMedia);
    return fixture;
}

static constexpr std::string_view kDcNamespace
    = "http://purl.org/dc/elements/1.1/";
static constexpr std::string_view kTitleXmp       = "Title from XMP";
static constexpr std::string_view kDescriptionXmp = "Description from XMP";
static constexpr std::string_view kCreatorXmp     = "Creator from XMP";
static constexpr std::string_view kRightsXmp      = "Rights from XMP";
static constexpr std::string_view kFrenchTitle    = "Titre fran\xC3\xA7"
                                                    "ais";
static constexpr std::string_view kFrenchDescription
    = "Description fran\xC3\xA7"
      "aise";
static constexpr std::string_view kFrenchRights = "Droits fran\xC3\xA7"
                                                  "ais";

static openmeta::MetadataAuthoringEntry
make_xmp_text_entry(std::string_view schema, std::string_view path,
                    std::string_view text)
{
    openmeta::MetadataAuthoringEntry entry;
    entry.key   = openmeta::make_xmp_property_key_view(schema, path);
    entry.value = openmeta::make_value_view_text(text,
                                                 openmeta::TextEncoding::Utf8);
    return entry;
}

static openmeta::MetadataAuthoringEntry
make_iptc_text_entry(uint16_t record, uint16_t dataset, std::string_view text)
{
    openmeta::MetadataAuthoringEntry entry;
    entry.key   = openmeta::make_iptc_dataset_key_view(record, dataset);
    entry.value = openmeta::make_value_view_text(text,
                                                 openmeta::TextEncoding::Ascii);
    return entry;
}

static openmeta::MetadataAuthoringEntry
make_exif_text_entry(std::string_view ifd, uint16_t tag, std::string_view text)
{
    openmeta::MetadataAuthoringEntry entry;
    entry.key   = openmeta::make_exif_tag_key_view(ifd, tag);
    entry.value = openmeta::make_value_view_text(text,
                                                 openmeta::TextEncoding::Ascii);
    return entry;
}

static openmeta::MetadataAuthoringResult
make_reconcile_source(openmeta::MetaStore* store)
{
    std::vector<openmeta::MetadataAuthoringEntry> entries
        = openmeta::custom_xmp_test::initial_entries();
    entries.reserve(entries.size() + 20U);
    entries.push_back(make_xmp_text_entry(kDcNamespace,
                                          "title[@xml:lang=x-default]",
                                          kTitleXmp));
    entries.push_back(make_xmp_text_entry(kDcNamespace,
                                          "title[@xml:lang=fr-FR]",
                                          kFrenchTitle));
    entries.push_back(make_xmp_text_entry(kDcNamespace,
                                          "description[@xml:lang=x-default]",
                                          kDescriptionXmp));
    entries.push_back(make_xmp_text_entry(kDcNamespace,
                                          "description[@xml:lang=fr-FR]",
                                          kFrenchDescription));
    entries.push_back(
        make_xmp_text_entry(kDcNamespace, "creator[1]", kCreatorXmp));
    entries.push_back(
        make_xmp_text_entry(kDcNamespace, "subject[1]", "keyword-one"));
    entries.push_back(
        make_xmp_text_entry(kDcNamespace, "subject[2]", "keyword-two"));
    entries.push_back(make_xmp_text_entry(kDcNamespace,
                                          "rights[@xml:lang=x-default]",
                                          kRightsXmp));
    entries.push_back(make_xmp_text_entry(kDcNamespace,
                                          "rights[@xml:lang=fr-FR]",
                                          kFrenchRights));

    entries.push_back(make_iptc_text_entry(2U, 5U, "Title from IPTC"));
    entries.push_back(make_iptc_text_entry(2U, 120U, "Description from IPTC"));
    entries.push_back(make_iptc_text_entry(2U, 80U, "Creator from IPTC"));
    entries.push_back(make_iptc_text_entry(2U, 25U, "keyword-two"));
    entries.push_back(make_iptc_text_entry(2U, 25U, "keyword-one"));
    entries.push_back(make_iptc_text_entry(2U, 116U, "Rights from IPTC"));

    entries.push_back(
        make_exif_text_entry("ifd0", 0x010EU, "Description from EXIF"));
    entries.push_back(
        make_exif_text_entry("ifd0", 0x013BU, "Creator from EXIF"));
    entries.push_back(
        make_exif_text_entry("ifd0", 0x8298U, "Rights from EXIF"));

    return openmeta::create_metadata_store(entries, store);
}

static std::string_view
entry_text(const openmeta::MetaStore& store,
           const openmeta::Entry& entry) noexcept
{
    if (entry.value.kind != openmeta::MetaValueKind::Text
        && entry.value.kind != openmeta::MetaValueKind::Bytes) {
        return {};
    }
    const std::span<const std::byte> bytes = store.arena().span(
        entry.value.data.span);
    std::string_view text(reinterpret_cast<const char*>(bytes.data()),
                          bytes.size());
    while (!text.empty() && text.back() == '\0') {
        text.remove_suffix(1U);
    }
    return text;
}

static bool
has_text(const openmeta::MetaStore& store, openmeta::MetaKeyView key,
         std::string_view expected) noexcept
{
    const std::span<const openmeta::EntryId> found = store.find_all(key);
    for (openmeta::EntryId id : found) {
        if (id < store.entries().size()
            && entry_text(store, store.entry(id)) == expected) {
            return true;
        }
    }
    return false;
}

static uint32_t
count_text(const openmeta::MetaStore& store, openmeta::MetaKeyView key,
           std::string_view expected) noexcept
{
    uint32_t count                                 = 0U;
    const std::span<const openmeta::EntryId> found = store.find_all(key);
    for (openmeta::EntryId id : found) {
        if (id < store.entries().size()
            && entry_text(store, store.entry(id)) == expected) {
            ++count;
        }
    }
    return count;
}

static void
expect_custom_xmp_tree(const openmeta::MetaStore& store,
                       bool rdf_array_indices_compacted = false)
{
    using openmeta::XmpContainerKind;
    const std::array<std::pair<std::string_view, XmpContainerKind>, 6U> nodes = {
        std::pair { "Record", XmpContainerKind::Struct },
        std::pair { "Record/Values", XmpContainerKind::Bag },
        std::pair { "Record/Steps", XmpContainerKind::Seq },
        std::pair { "Record/Labels", XmpContainerKind::LangAlt },
        std::pair { "Record/EmptyStruct", XmpContainerKind::Struct },
        std::pair { "Record/EmptyBag", XmpContainerKind::Bag },
    };
    for (const auto& node : nodes) {
        const std::span<const openmeta::EntryId> found = store.find_all(
            openmeta::make_xmp_property_key_view(
                openmeta::custom_xmp_test::kSchema, node.first));
        ASSERT_EQ(found.size(), 1U) << node.first;
        ASSERT_LT(found.front(), store.entries().size());
        EXPECT_EQ(openmeta::xmp_container_kind(store.entry(found.front())),
                  node.second)
            << node.first;
    }
    EXPECT_TRUE(
        has_text(store,
                 openmeta::make_xmp_property_key_view(
                     openmeta::custom_xmp_test::kSchema, "Record/Title"),
                 "old"));
    if (rdf_array_indices_compacted) {
        EXPECT_TRUE(has_text(store,
                             openmeta::make_xmp_property_key_view(
                                 openmeta::custom_xmp_test::kSchema,
                                 "Record/Values[1]"),
                             "one"));
        EXPECT_TRUE(has_text(store,
                             openmeta::make_xmp_property_key_view(
                                 openmeta::custom_xmp_test::kSchema,
                                 "Record/Values[2]"),
                             "three"));
        EXPECT_TRUE(has_text(store,
                             openmeta::make_xmp_property_key_view(
                                 openmeta::custom_xmp_test::kSchema,
                                 "Record/Values[3]"),
                             "ten"));
    } else {
        EXPECT_TRUE(has_text(store,
                             openmeta::make_xmp_property_key_view(
                                 openmeta::custom_xmp_test::kSchema,
                                 "Record/Values[1]"),
                             "one"));
        EXPECT_TRUE(has_text(store,
                             openmeta::make_xmp_property_key_view(
                                 openmeta::custom_xmp_test::kSchema,
                                 "Record/Values[3]"),
                             "three"));
        EXPECT_TRUE(has_text(store,
                             openmeta::make_xmp_property_key_view(
                                 openmeta::custom_xmp_test::kSchema,
                                 "Record/Values[10]"),
                             "ten"));
    }
    EXPECT_TRUE(has_text(
        store,
        openmeta::make_xmp_property_key_view(openmeta::custom_xmp_test::kSchema,
                                             "Record/Labels[@xml:lang=en-US]"),
        "English label"));
}

static void
expect_xmp_descriptive_values(const openmeta::MetaStore& store)
{
    EXPECT_TRUE(has_text(store,
                         openmeta::make_xmp_property_key_view(
                             kDcNamespace, "title[@xml:lang=x-default]"),
                         kTitleXmp));
    EXPECT_TRUE(has_text(store,
                         openmeta::make_xmp_property_key_view(
                             kDcNamespace, "description[@xml:lang=x-default]"),
                         kDescriptionXmp));
    EXPECT_TRUE(has_text(store,
                         openmeta::make_xmp_property_key_view(kDcNamespace,
                                                              "creator[1]"),
                         kCreatorXmp));
    EXPECT_EQ(count_text(store,
                         openmeta::make_xmp_property_key_view(kDcNamespace,
                                                              "subject[1]"),
                         "keyword-one"),
              1U);
    EXPECT_EQ(count_text(store,
                         openmeta::make_xmp_property_key_view(kDcNamespace,
                                                              "subject[2]"),
                         "keyword-two"),
              1U);
    EXPECT_TRUE(has_text(store,
                         openmeta::make_xmp_property_key_view(
                             kDcNamespace, "rights[@xml:lang=x-default]"),
                         kRightsXmp));
    EXPECT_TRUE(
        has_text(store,
                 openmeta::make_xmp_property_key_view(kDcNamespace,
                                                      "title[@xml:lang=fr-FR]"),
                 kFrenchTitle));
    EXPECT_TRUE(has_text(store,
                         openmeta::make_xmp_property_key_view(
                             kDcNamespace, "description[@xml:lang=fr-FR]"),
                         kFrenchDescription));
    EXPECT_TRUE(has_text(store,
                         openmeta::make_xmp_property_key_view(
                             kDcNamespace, "rights[@xml:lang=fr-FR]"),
                         kFrenchRights));
}

static void
expect_exif_descriptive_values(const openmeta::MetaStore& store)
{
    EXPECT_TRUE(has_text(store,
                         openmeta::make_exif_tag_key_view("ifd0", 0x010EU),
                         kDescriptionXmp));
    EXPECT_TRUE(has_text(store,
                         openmeta::make_exif_tag_key_view("ifd0", 0x013BU),
                         kCreatorXmp));
    EXPECT_TRUE(has_text(store,
                         openmeta::make_exif_tag_key_view("ifd0", 0x8298U),
                         kRightsXmp));
}

static void
expect_iptc_descriptive_values(const openmeta::MetaStore& store)
{
    EXPECT_TRUE(has_text(store, openmeta::make_iptc_dataset_key_view(2U, 5U),
                         kTitleXmp));
    EXPECT_TRUE(has_text(store, openmeta::make_iptc_dataset_key_view(2U, 120U),
                         kDescriptionXmp));
    EXPECT_TRUE(has_text(store, openmeta::make_iptc_dataset_key_view(2U, 80U),
                         kCreatorXmp));
    EXPECT_TRUE(has_text(store, openmeta::make_iptc_dataset_key_view(2U, 25U),
                         "keyword-one"));
    EXPECT_TRUE(has_text(store, openmeta::make_iptc_dataset_key_view(2U, 25U),
                         "keyword-two"));
    EXPECT_TRUE(has_text(store, openmeta::make_iptc_dataset_key_view(2U, 116U),
                         kRightsXmp));
}

enum class CarrierKind : uint8_t {
    Jpeg,
    BigTiff,
    Png,
    Webp,
    Jp2,
    Jph,
    Heif,
    Avif,
    Cr3
};

struct CarrierCase final {
    const char* name;
    CarrierKind kind;
    openmeta::TransferTargetFormat format;
    bool carries_iptc;
};

static constexpr std::array<CarrierCase, 9U> kCarrierCases = {
    CarrierCase { "jpeg", CarrierKind::Jpeg,
                  openmeta::TransferTargetFormat::Jpeg, true },
    CarrierCase { "bigtiff", CarrierKind::BigTiff,
                  openmeta::TransferTargetFormat::Tiff, true },
    CarrierCase { "png", CarrierKind::Png, openmeta::TransferTargetFormat::Png,
                  false },
    CarrierCase { "webp", CarrierKind::Webp,
                  openmeta::TransferTargetFormat::Webp, false },
    CarrierCase { "jp2", CarrierKind::Jp2, openmeta::TransferTargetFormat::Jp2,
                  false },
    CarrierCase { "jph", CarrierKind::Jph, openmeta::TransferTargetFormat::Jp2,
                  false },
    CarrierCase { "heif", CarrierKind::Heif,
                  openmeta::TransferTargetFormat::Heif, false },
    CarrierCase { "avif", CarrierKind::Avif,
                  openmeta::TransferTargetFormat::Avif, false },
    CarrierCase { "cr3", CarrierKind::Cr3, openmeta::TransferTargetFormat::Cr3,
                  false },
};

static bool
make_carrier_target(CarrierKind kind, Bytes* output)
{
    if (output == nullptr) {
        return false;
    }
    switch (kind) {
    case CarrierKind::Jpeg: {
        openmeta::container_test::JpegFixture fixture
            = openmeta::container_test::make_jpeg_fixture();
        *output = std::move(fixture.bytes);
        return true;
    }
    case CarrierKind::BigTiff:
        *output = openmeta::container_test::make_tiff_fixture(
            { true, true, false });
        return true;
    case CarrierKind::Png:
        *output = openmeta::container_test::make_png_fixture();
        return true;
    case CarrierKind::Webp:
        *output = openmeta::container_test::make_webp_fixture();
        return true;
    case CarrierKind::Jp2:
        *output = make_jp2_fixture(openmeta::fourcc('j', 'p', '2', ' ')).bytes;
        return true;
    case CarrierKind::Jph:
        *output = make_jp2_fixture(openmeta::fourcc('j', 'p', 'h', ' ')).bytes;
        return true;
    case CarrierKind::Heif:
        *output = make_bmff_fixture(BmffFixtureBrand::Heif).bytes;
        return true;
    case CarrierKind::Avif:
        *output = make_bmff_fixture(BmffFixtureBrand::Avif).bytes;
        return true;
    case CarrierKind::Cr3:
        *output = make_bmff_fixture(BmffFixtureBrand::Cr3).bytes;
        return true;
    }
    return false;
}

static std::array<uint32_t, 3U>
expected_brand_fields(CarrierKind kind) noexcept
{
    switch (kind) {
    case CarrierKind::Jpeg:
    case CarrierKind::BigTiff:
    case CarrierKind::Png:
    case CarrierKind::Webp: return {};
    case CarrierKind::Jp2: {
        const uint32_t jp2 = openmeta::fourcc('j', 'p', '2', ' ');
        return { jp2, jp2, 0U };
    }
    case CarrierKind::Jph: {
        const uint32_t jph = openmeta::fourcc('j', 'p', 'h', ' ');
        return { jph, jph, 0U };
    }
    case CarrierKind::Heif: {
        const uint32_t heic = openmeta::fourcc('h', 'e', 'i', 'c');
        return { heic, openmeta::fourcc('m', 'i', 'f', '1'), heic };
    }
    case CarrierKind::Avif: {
        const uint32_t avif = openmeta::fourcc('a', 'v', 'i', 'f');
        return { avif, openmeta::fourcc('m', 'i', 'f', '1'), avif };
    }
    case CarrierKind::Cr3: {
        const uint32_t crx = openmeta::fourcc('c', 'r', 'x', ' ');
        return { crx, crx, openmeta::fourcc('C', 'R', '3', ' ') };
    }
    }
    return {};
}

static void
expect_carrier_brand(CarrierKind kind, std::span<const std::byte> bytes)
{
    if (kind != CarrierKind::Jp2 && kind != CarrierKind::Jph
        && kind != CarrierKind::Heif && kind != CarrierKind::Avif
        && kind != CarrierKind::Cr3) {
        return;
    }
    BoxView ftyp;
    ASSERT_TRUE(find_top_level_box(bytes, openmeta::fourcc('f', 't', 'y', 'p'),
                                   0U, &ftyp));
    const std::array<uint32_t, 3U> expected = expected_brand_fields(kind);
    uint32_t major_brand                    = 0U;
    uint32_t compatible_brand0              = 0U;
    ASSERT_TRUE(read_u32_be(ftyp.payload, 0U, &major_brand));
    ASSERT_TRUE(read_u32_be(ftyp.payload, 8U, &compatible_brand0));
    EXPECT_EQ(major_brand, expected[0U]);
    EXPECT_EQ(compatible_brand0, expected[1U]);
    if (kind == CarrierKind::Heif || kind == CarrierKind::Avif
        || kind == CarrierKind::Cr3) {
        uint32_t compatible_brand1 = 0U;
        ASSERT_TRUE(read_u32_be(ftyp.payload, 12U, &compatible_brand1));
        EXPECT_EQ(compatible_brand1, expected[2U]);
    }
}

static bool
read_u16_be(std::span<const std::byte> bytes, size_t offset,
            uint16_t* value) noexcept
{
    if (value == nullptr || offset > bytes.size()
        || bytes.size() - offset < 2U) {
        return false;
    }
    *value = static_cast<uint16_t>(
        (static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset])) << 8U)
        | static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset + 1U])));
    return true;
}

struct SegmentView final {
    std::span<const std::byte> raw;
    std::span<const std::byte> payload;
};

static bool
read_jpeg_segment(std::span<const std::byte> bytes, size_t offset,
                  uint8_t* marker, SegmentView* view,
                  size_t* next_offset) noexcept
{
    if (marker == nullptr || view == nullptr || next_offset == nullptr
        || offset > bytes.size() || bytes.size() - offset < 2U
        || bytes[offset] != std::byte { 0xFFU }) {
        return false;
    }
    const size_t start = offset;
    while (offset < bytes.size() && bytes[offset] == std::byte { 0xFFU }) {
        ++offset;
    }
    if (offset == bytes.size()) {
        return false;
    }
    *marker = std::to_integer<uint8_t>(bytes[offset]);
    ++offset;
    if (*marker == 0xD8U || *marker == 0xD9U || *marker == 0x01U
        || (*marker >= 0xD0U && *marker <= 0xD7U)) {
        view->raw     = bytes.subspan(start, offset - start);
        view->payload = {};
        *next_offset  = offset;
        return true;
    }
    uint16_t length = 0U;
    if (!read_u16_be(bytes, offset, &length) || length < 2U
        || static_cast<size_t>(length) > bytes.size() - offset) {
        return false;
    }
    const size_t end = offset + length;
    view->raw        = bytes.subspan(start, end - start);
    view->payload    = bytes.subspan(offset + 2U,
                                     static_cast<size_t>(length) - 2U);
    *next_offset     = end;
    return true;
}

static bool
find_jpeg_segment(std::span<const std::byte> bytes, uint8_t wanted,
                  SegmentView* result) noexcept
{
    if (result == nullptr || bytes.size() < 2U
        || bytes[0U] != std::byte { 0xFFU }
        || bytes[1U] != std::byte { 0xD8U }) {
        return false;
    }
    size_t offset = 2U;
    while (offset < bytes.size()) {
        uint8_t marker = 0U;
        SegmentView view;
        size_t next_offset = 0U;
        if (!read_jpeg_segment(bytes, offset, &marker, &view, &next_offset)) {
            return false;
        }
        if (marker == wanted) {
            *result = view;
            return true;
        }
        if (marker == 0xDAU || marker == 0xD9U) {
            return false;
        }
        offset = next_offset;
    }
    return false;
}

static bool
find_jpeg_scan(std::span<const std::byte> bytes,
               std::span<const std::byte>* scan) noexcept
{
    if (scan == nullptr || bytes.size() < 2U || bytes[0U] != std::byte { 0xFFU }
        || bytes[1U] != std::byte { 0xD8U }) {
        return false;
    }
    size_t offset = 2U;
    while (offset < bytes.size()) {
        const size_t start = offset;
        uint8_t marker     = 0U;
        SegmentView view;
        size_t next_offset = 0U;
        if (!read_jpeg_segment(bytes, offset, &marker, &view, &next_offset)) {
            return false;
        }
        if (marker == 0xDAU) {
            *scan = bytes.subspan(start);
            return true;
        }
        if (marker == 0xD9U) {
            return false;
        }
        offset = next_offset;
    }
    return false;
}

static bool
find_png_chunk(std::span<const std::byte> bytes, uint32_t wanted,
               SegmentView* result) noexcept
{
    static constexpr std::array<std::byte, 8U> kSignature = {
        std::byte { 0x89U }, std::byte { 'P' },   std::byte { 'N' },
        std::byte { 'G' },   std::byte { 0x0DU }, std::byte { 0x0AU },
        std::byte { 0x1AU }, std::byte { 0x0AU },
    };
    if (result == nullptr || bytes.size() < kSignature.size()
        || !same_bytes(bytes.first(kSignature.size()), kSignature)) {
        return false;
    }
    size_t offset = kSignature.size();
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 12U) {
            return false;
        }
        uint32_t length = 0U;
        uint32_t type   = 0U;
        if (!read_u32_be(bytes, offset, &length)
            || !read_u32_be(bytes, offset + 4U, &type)
            || static_cast<size_t>(length) > bytes.size() - offset - 12U) {
            return false;
        }
        const size_t total = 12U + static_cast<size_t>(length);
        if (type == wanted) {
            result->raw     = bytes.subspan(offset, total);
            result->payload = bytes.subspan(offset + 8U,
                                            static_cast<size_t>(length));
            return true;
        }
        offset += total;
    }
    return false;
}

static bool
read_u32_le(std::span<const std::byte> bytes, size_t offset,
            uint32_t* value) noexcept
{
    if (value == nullptr || offset > bytes.size()
        || bytes.size() - offset < 4U) {
        return false;
    }
    *value
        = static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset]))
          | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset + 1U]))
             << 8U)
          | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset + 2U]))
             << 16U)
          | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset + 3U]))
             << 24U);
    return true;
}

static bool
find_webp_chunk(std::span<const std::byte> bytes, uint32_t wanted,
                SegmentView* result) noexcept
{
    if (result == nullptr || bytes.size() < 12U
        || !same_bytes(bytes.first(4U), std::as_bytes(std::span("RIFF", 4U)))
        || !same_bytes(bytes.subspan(8U, 4U),
                       std::as_bytes(std::span("WEBP", 4U)))) {
        return false;
    }
    size_t offset = 12U;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 8U) {
            return false;
        }
        uint32_t type   = 0U;
        uint32_t length = 0U;
        if (!read_u32_be(bytes, offset, &type)
            || !read_u32_le(bytes, offset + 4U, &length)
            || static_cast<size_t>(length) > bytes.size() - offset - 8U) {
            return false;
        }
        const size_t padded = static_cast<size_t>(length) + (length & 1U);
        if (padded > bytes.size() - offset - 8U) {
            return false;
        }
        const size_t total = 8U + padded;
        if (type == wanted) {
            result->raw     = bytes.subspan(offset, total);
            result->payload = bytes.subspan(offset + 8U,
                                            static_cast<size_t>(length));
            return true;
        }
        offset += total;
    }
    return false;
}

static bool
bigtiff_strip(std::span<const std::byte> bytes,
              std::span<const std::byte>* strip) noexcept
{
    if (strip == nullptr) {
        return false;
    }
    openmeta::container_test::TiffEntrySpan offset_entry;
    openmeta::container_test::TiffEntrySpan size_entry;
    uint64_t offset = 0U;
    uint64_t size   = 0U;
    if (!openmeta::container_test::find_big_endian_bigtiff_entry(bytes, 0x0111U,
                                                                 &offset_entry)
        || !openmeta::container_test::find_big_endian_bigtiff_entry(bytes,
                                                                    0x0117U,
                                                                    &size_entry)
        || !openmeta::container_test::decode_big_endian_uint(offset_entry.value,
                                                             &offset)
        || !openmeta::container_test::decode_big_endian_uint(size_entry.value,
                                                             &size)
        || offset > bytes.size() || size > bytes.size() - offset) {
        return false;
    }
    *strip = bytes.subspan(static_cast<size_t>(offset),
                           static_cast<size_t>(size));
    return true;
}

static void
expect_media_and_unmanaged_bytes(CarrierKind kind,
                                 std::span<const std::byte> before,
                                 std::span<const std::byte> after)
{
    if (kind == CarrierKind::Jpeg) {
        SegmentView before_comment;
        SegmentView after_comment;
        std::span<const std::byte> before_scan;
        std::span<const std::byte> after_scan;
        ASSERT_TRUE(find_jpeg_segment(before, 0xFEU, &before_comment));
        ASSERT_TRUE(find_jpeg_segment(after, 0xFEU, &after_comment));
        ASSERT_TRUE(find_jpeg_scan(before, &before_scan));
        ASSERT_TRUE(find_jpeg_scan(after, &after_scan));
        EXPECT_TRUE(same_bytes(before_comment.raw, after_comment.raw));
        EXPECT_TRUE(same_bytes(before_scan, after_scan));
        return;
    }
    if (kind == CarrierKind::BigTiff) {
        std::span<const std::byte> before_strip;
        std::span<const std::byte> after_strip;
        ASSERT_TRUE(bigtiff_strip(before, &before_strip));
        ASSERT_TRUE(bigtiff_strip(after, &after_strip));
        EXPECT_TRUE(same_bytes(before_strip, after_strip));
        return;
    }
    if (kind == CarrierKind::Png) {
        SegmentView before_idat;
        SegmentView after_idat;
        SegmentView before_text;
        SegmentView after_text;
        ASSERT_TRUE(find_png_chunk(before, openmeta::fourcc('I', 'D', 'A', 'T'),
                                   &before_idat));
        ASSERT_TRUE(find_png_chunk(after, openmeta::fourcc('I', 'D', 'A', 'T'),
                                   &after_idat));
        ASSERT_TRUE(find_png_chunk(before, openmeta::fourcc('t', 'E', 'X', 't'),
                                   &before_text));
        ASSERT_TRUE(find_png_chunk(after, openmeta::fourcc('t', 'E', 'X', 't'),
                                   &after_text));
        EXPECT_TRUE(same_bytes(before_idat.raw, after_idat.raw));
        EXPECT_TRUE(same_bytes(before_text.raw, after_text.raw));
        return;
    }
    if (kind == CarrierKind::Webp) {
        SegmentView before_image;
        SegmentView after_image;
        SegmentView before_free;
        SegmentView after_free;
        ASSERT_TRUE(find_webp_chunk(before,
                                    openmeta::fourcc('V', 'P', '8', 'L'),
                                    &before_image));
        ASSERT_TRUE(find_webp_chunk(after, openmeta::fourcc('V', 'P', '8', 'L'),
                                    &after_image));
        ASSERT_TRUE(find_webp_chunk(before,
                                    openmeta::fourcc('F', 'R', 'E', 'E'),
                                    &before_free));
        ASSERT_TRUE(find_webp_chunk(after, openmeta::fourcc('F', 'R', 'E', 'E'),
                                    &after_free));
        EXPECT_TRUE(same_bytes(before_image.raw, after_image.raw));
        EXPECT_TRUE(same_bytes(before_free.raw, after_free.raw));
        return;
    }
    if (kind == CarrierKind::Jp2 || kind == CarrierKind::Jph) {
        BoxView before_brand;
        BoxView after_brand;
        BoxView before_codestream;
        BoxView after_codestream;
        BoxView before_free;
        BoxView after_free;
        ASSERT_TRUE(find_top_level_box(before,
                                       openmeta::fourcc('f', 't', 'y', 'p'), 0U,
                                       &before_brand));
        ASSERT_TRUE(find_top_level_box(after,
                                       openmeta::fourcc('f', 't', 'y', 'p'), 0U,
                                       &after_brand));
        ASSERT_TRUE(find_top_level_box(before,
                                       openmeta::fourcc('j', 'p', '2', 'c'), 0U,
                                       &before_codestream));
        ASSERT_TRUE(find_top_level_box(after,
                                       openmeta::fourcc('j', 'p', '2', 'c'), 0U,
                                       &after_codestream));
        ASSERT_TRUE(find_top_level_box(before,
                                       openmeta::fourcc('f', 'r', 'e', 'e'), 0U,
                                       &before_free));
        ASSERT_TRUE(find_top_level_box(after,
                                       openmeta::fourcc('f', 'r', 'e', 'e'), 0U,
                                       &after_free));
        EXPECT_TRUE(same_bytes(before_brand.raw, after_brand.raw));
        EXPECT_TRUE(same_bytes(before_codestream.raw, after_codestream.raw));
        EXPECT_TRUE(same_bytes(before_free.raw, after_free.raw));
        return;
    }

    BoxView before_brand;
    BoxView after_brand;
    BoxView before_media;
    BoxView after_media;
    BoxView before_free;
    BoxView after_free;
    ASSERT_TRUE(find_top_level_box(before, openmeta::fourcc('f', 't', 'y', 'p'),
                                   0U, &before_brand));
    ASSERT_TRUE(find_top_level_box(after, openmeta::fourcc('f', 't', 'y', 'p'),
                                   0U, &after_brand));
    ASSERT_TRUE(find_top_level_box(before, openmeta::fourcc('m', 'd', 'a', 't'),
                                   0U, &before_media));
    ASSERT_TRUE(find_top_level_box(after, openmeta::fourcc('m', 'd', 'a', 't'),
                                   0U, &after_media));
    ASSERT_TRUE(find_top_level_box(before, openmeta::fourcc('f', 'r', 'e', 'e'),
                                   0U, &before_free));
    ASSERT_TRUE(find_top_level_box(after, openmeta::fourcc('f', 'r', 'e', 'e'),
                                   0U, &after_free));
    EXPECT_TRUE(same_bytes(before_brand.raw, after_brand.raw));
    EXPECT_TRUE(same_bytes(before_media.raw, after_media.raw));
    EXPECT_TRUE(same_bytes(before_free.raw, after_free.raw));
}

static openmeta::PrepareTransferRequest
prepare_request_for(const CarrierCase& test_case)
{
    openmeta::PrepareTransferRequest request;
    request.target_format        = test_case.format;
    request.include_exif_app1    = true;
    request.include_xmp_app1     = true;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = test_case.carries_iptc;
    request.xmp_portable         = true;
    request.xmp_project_exif     = true;
    request.xmp_project_iptc     = true;
    request.xmp_include_existing = true;
    request.xmp_existing_namespace_policy
        = openmeta::XmpExistingNamespacePolicy::PreserveCustom;
    request.xmp_existing_standard_namespace_policy
        = openmeta::XmpExistingStandardNamespacePolicy::PreserveAll;
    request.xmp_conflict_policy = openmeta::XmpConflictPolicy::ExistingWins;
    return request;
}

static bool
make_title_snapshot(std::string_view title,
                    openmeta::TransferSourceSnapshot* output)
{
    if (output == nullptr) {
        return false;
    }
    const std::array<openmeta::MetadataAuthoringEntry, 1U> entries = {
        make_xmp_text_entry(kDcNamespace, "title[@xml:lang=x-default]", title)
    };
    openmeta::MetaStore store;
    if (!openmeta::create_metadata_store(entries, &store).ok()) {
        return false;
    }
    *output = openmeta::build_transfer_source_snapshot(store);
    return output->store.is_finalized();
}

struct TemporaryDirectory final {
    std::filesystem::path path;
    bool valid = false;

    TemporaryDirectory()
    {
        std::error_code error;
        const std::filesystem::path base = std::filesystem::temp_directory_path(
            error);
        if (error) {
            return;
        }
#if defined(_WIN32)
        const int process = _getpid();
#else
        const int process = static_cast<int>(getpid());
#endif
        for (uint32_t attempt = 0U; attempt < 128U; ++attempt) {
            const std::string name = "openmeta-b37-carrier-"
                                     + std::to_string(process) + "-"
                                     + std::to_string(attempt);
            const std::filesystem::path candidate = base / name;
            if (std::filesystem::create_directory(candidate, error)) {
                path  = candidate;
                valid = true;
                return;
            }
            if (error == std::errc::file_exists) {
                error.clear();
                continue;
            }
            if (error) {
                return;
            }
        }
    }

    ~TemporaryDirectory()
    {
        if (valid) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }
};

static bool
write_bytes_file(const std::filesystem::path& path,
                 std::span<const std::byte> bytes)
{
    const std::string name = path.string();
    std::FILE* output      = std::fopen(name.c_str(), "wb");
    if (output == nullptr) {
        return false;
    }
    const size_t written = std::fwrite(bytes.data(), 1U, bytes.size(), output);
    const int closed     = std::fclose(output);
    return written == bytes.size() && closed == 0;
}

static bool
build_portable_xmp_sidecar(const openmeta::MetaStore& store, Bytes* output)
{
    if (output == nullptr) {
        return false;
    }
    openmeta::XmpSidecarRequest request;
    request.format               = openmeta::XmpSidecarFormat::Portable;
    request.include_exif         = false;
    request.include_iptc         = false;
    request.include_existing_xmp = true;
    request.portable_existing_namespace_policy
        = openmeta::XmpExistingNamespacePolicy::PreserveCustom;
    const openmeta::XmpDumpResult result
        = openmeta::dump_xmp_sidecar(store, output, request);
    return result.status == openmeta::XmpDumpStatus::Ok && !output->empty();
}

TEST(MetadataReconcileTransfer,
     ReconcilePrepareApplyAndReadBackAcrossEmbeddedCarriers)
{
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
    GTEST_SKIP() << "Expat is required for XMP structure readback.";
#endif
    openmeta::MetaStore source;
    const openmeta::MetadataAuthoringResult authored = make_reconcile_source(
        &source);
    ASSERT_TRUE(authored.ok())
        << openmeta::metadata_authoring_status_name(authored.status);

    openmeta::MetadataReconcileOptions reconcile_options;
    reconcile_options.enable_exif_bridge = true;
    reconcile_options.conflict_policy
        = openmeta::MetadataReconcileConflictPolicy::PreferSourceOrder;
    reconcile_options.family_priority = {
        openmeta::MetadataReconcileFamily::Xmp,
        openmeta::MetadataReconcileFamily::Iptc,
        openmeta::MetadataReconcileFamily::Exif,
    };
    openmeta::MetaStore reconciled;
    const openmeta::MetadataReconcileResult reconcile
        = openmeta::reconcile_descriptive_metadata(source, reconcile_options,
                                                   &reconciled);
    ASSERT_EQ(reconcile.status, openmeta::MetadataReconcileStatus::Ok);
    EXPECT_EQ(reconcile.groups_reconciled, 5U);
    expect_xmp_descriptive_values(reconciled);
    expect_iptc_descriptive_values(reconciled);
    expect_exif_descriptive_values(reconciled);
    expect_custom_xmp_tree(reconciled);

    const openmeta::TransferSourceSnapshot snapshot
        = openmeta::build_transfer_source_snapshot(reconciled);
    ASSERT_TRUE(snapshot.store.is_finalized());

    for (const CarrierCase& test_case : kCarrierCases) {
        SCOPED_TRACE(test_case.name);
        Bytes target;
        ASSERT_TRUE(make_carrier_target(test_case.kind, &target));
        expect_carrier_brand(test_case.kind, target);
        const Bytes target_before = target;

        openmeta::ExecutePreparedTransferSnapshotOptions options;
        options.prepare                        = prepare_request_for(test_case);
        options.preserve_custom_xmp_structures = true;
        options.execute.edit_apply             = true;
        const openmeta::ExecutePreparedTransferFileResult executed
            = openmeta::execute_prepared_transfer_snapshot(
                snapshot,
                std::span<const std::byte>(target.data(), target.size()),
                options);
        ASSERT_EQ(executed.prepared.prepare.status, openmeta::TransferStatus::Ok)
            << executed.prepared.prepare.message;
        ASSERT_EQ(executed.execute.edit_plan_status,
                  openmeta::TransferStatus::Ok)
            << executed.execute.edit_plan_message;
        ASSERT_EQ(executed.execute.edit_apply.status,
                  openmeta::TransferStatus::Ok);
        ASSERT_FALSE(executed.execute.edited_output.empty());
        EXPECT_EQ(target, target_before);
        expect_carrier_brand(test_case.kind, executed.execute.edited_output);

        openmeta::ReadTransferSourceSnapshotOptions read_options;
        read_options.preserve_custom_xmp_structures = true;
        const openmeta::ReadTransferSourceSnapshotBytesResult readback
            = openmeta::read_transfer_source_snapshot_bytes(
                std::span<const std::byte>(
                    executed.execute.edited_output.data(),
                    executed.execute.edited_output.size()),
                read_options);
        ASSERT_EQ(readback.status, openmeta::TransferStatus::Ok)
            << static_cast<unsigned>(readback.code);
        const openmeta::MetaStore& decoded = readback.snapshot.store;
        expect_xmp_descriptive_values(decoded);
        expect_exif_descriptive_values(decoded);
        if (test_case.carries_iptc) {
            expect_iptc_descriptive_values(decoded);
        }
        expect_custom_xmp_tree(decoded, true);
        expect_media_and_unmanaged_bytes(
            test_case.kind, target,
            std::span<const std::byte>(executed.execute.edited_output.data(),
                                       executed.execute.edited_output.size()));
    }

    EXPECT_TRUE(has_text(source, openmeta::make_iptc_dataset_key_view(2U, 5U),
                         "Title from IPTC"));
    EXPECT_TRUE(has_text(source,
                         openmeta::make_exif_tag_key_view("ifd0", 0x010EU),
                         "Description from EXIF"));
}

TEST(MetadataReconcileTransfer,
     MalformedAndUnsupportedCarriersReturnNoEditedOutput)
{
    openmeta::TransferSourceSnapshot snapshot;
    ASSERT_TRUE(make_title_snapshot("Safe source title", &snapshot));

    const CarrierCase jp2_case { "jp2", CarrierKind::Jp2,
                                 openmeta::TransferTargetFormat::Jp2, false };
    Bytes malformed
        = make_jp2_fixture(openmeta::fourcc('j', 'p', '2', ' ')).bytes;
    malformed[0U]                = std::byte { 0U };
    malformed[1U]                = std::byte { 0U };
    malformed[2U]                = std::byte { 0U };
    malformed[3U]                = std::byte { 7U };
    const Bytes malformed_before = malformed;
    openmeta::ExecutePreparedTransferSnapshotOptions malformed_options;
    malformed_options.prepare            = prepare_request_for(jp2_case);
    malformed_options.execute.edit_apply = true;
    const openmeta::ExecutePreparedTransferFileResult malformed_result
        = openmeta::execute_prepared_transfer_snapshot(
            snapshot,
            std::span<const std::byte>(malformed.data(), malformed.size()),
            malformed_options);
    ASSERT_EQ(malformed_result.prepared.prepare.status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(malformed_result.execute.edit_plan_status,
              openmeta::TransferStatus::Malformed);
    EXPECT_EQ(malformed_result.execute.edit_apply.status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(malformed_result.execute.edited_output.empty());
    EXPECT_EQ(malformed, malformed_before);

    const CarrierCase heif_case { "heif", CarrierKind::Heif,
                                  openmeta::TransferTargetFormat::Heif, false };
    Bytes unsupported = make_bmff_fixture(BmffFixtureBrand::Heif).bytes;
    const std::array<std::byte, 4U> empty_meta_fullbox = {};
    append_box(&unsupported, openmeta::fourcc('m', 'e', 't', 'a'),
               empty_meta_fullbox);
    const Bytes unsupported_before = unsupported;
    openmeta::ExecutePreparedTransferSnapshotOptions unsupported_options;
    unsupported_options.prepare            = prepare_request_for(heif_case);
    unsupported_options.execute.edit_apply = true;
    const openmeta::ExecutePreparedTransferFileResult unsupported_result
        = openmeta::execute_prepared_transfer_snapshot(
            snapshot,
            std::span<const std::byte>(unsupported.data(), unsupported.size()),
            unsupported_options);
    ASSERT_EQ(unsupported_result.prepared.prepare.status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(unsupported_result.execute.edit_plan_status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(unsupported_result.execute.edit_apply.status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(unsupported_result.execute.edited_output.empty());
    EXPECT_EQ(unsupported, unsupported_before);
    EXPECT_TRUE(has_text(snapshot.store,
                         openmeta::make_xmp_property_key_view(
                             kDcNamespace, "title[@xml:lang=x-default]"),
                         "Safe source title"));
}

TEST(MetadataReconcileTransfer,
     DestinationCarrierPrecedenceControlsXmpOnlyEmbeddedAndSidecarOutput)
{
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
    GTEST_SKIP() << "Expat is required for XMP readback.";
#endif
    openmeta::TransferSourceSnapshot source_snapshot;
    openmeta::TransferSourceSnapshot embedded_snapshot;
    openmeta::TransferSourceSnapshot sidecar_snapshot;
    ASSERT_TRUE(make_title_snapshot("Transfer source title", &source_snapshot));
    ASSERT_TRUE(
        make_title_snapshot("Destination embedded title", &embedded_snapshot));
    ASSERT_TRUE(
        make_title_snapshot("Destination sidecar title", &sidecar_snapshot));

    const CarrierCase jpeg_case { "jpeg", CarrierKind::Jpeg,
                                  openmeta::TransferTargetFormat::Jpeg, false };
    Bytes clean_jpeg;
    ASSERT_TRUE(make_carrier_target(CarrierKind::Jpeg, &clean_jpeg));
    openmeta::ExecutePreparedTransferSnapshotOptions seed_options;
    seed_options.prepare                    = prepare_request_for(jpeg_case);
    seed_options.prepare.include_exif_app1  = false;
    seed_options.prepare.include_iptc_app13 = false;
    seed_options.prepare.xmp_project_exif   = false;
    seed_options.prepare.xmp_project_iptc   = false;
    seed_options.execute.edit_apply         = true;
    const openmeta::ExecutePreparedTransferFileResult embedded
        = openmeta::execute_prepared_transfer_snapshot(
            embedded_snapshot,
            std::span<const std::byte>(clean_jpeg.data(), clean_jpeg.size()),
            seed_options);
    ASSERT_EQ(embedded.prepared.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(embedded.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(embedded.execute.edit_apply.status, openmeta::TransferStatus::Ok);

    Bytes destination_sidecar;
    ASSERT_TRUE(build_portable_xmp_sidecar(sidecar_snapshot.store,
                                           &destination_sidecar));
    TemporaryDirectory directory;
    ASSERT_TRUE(directory.valid);
    const std::filesystem::path target_path  = directory.path / "target.jpg";
    const std::filesystem::path sidecar_path = directory.path / "target.xmp";
    ASSERT_TRUE(write_bytes_file(target_path, embedded.execute.edited_output));
    ASSERT_TRUE(write_bytes_file(sidecar_path, destination_sidecar));

    const std::array<openmeta::XmpExistingDestinationCarrierPrecedence, 2U>
        precedences = {
            openmeta::XmpExistingDestinationCarrierPrecedence::SidecarWins,
            openmeta::XmpExistingDestinationCarrierPrecedence::EmbeddedWins,
        };
    for (openmeta::XmpExistingDestinationCarrierPrecedence precedence :
         precedences) {
        SCOPED_TRACE(static_cast<uint8_t>(precedence));
        openmeta::ExecutePreparedTransferSnapshotOptions options;
        options.prepare                    = prepare_request_for(jpeg_case);
        options.prepare.include_exif_app1  = false;
        options.prepare.include_iptc_app13 = false;
        options.prepare.xmp_project_exif   = false;
        options.prepare.xmp_project_iptc   = false;
        options.prepare.xmp_conflict_policy
            = openmeta::XmpConflictPolicy::ExistingWins;
        options.xmp_existing_sidecar_base_path = target_path.string();
        options.xmp_existing_sidecar_mode
            = openmeta::XmpExistingSidecarMode::MergeIfPresent;
        options.xmp_existing_destination_carrier_precedence = precedence;
        options.xmp_existing_destination_embedded_mode
            = openmeta::XmpExistingDestinationEmbeddedMode::MergeIfPresent;
        options.xmp_existing_destination_embedded_precedence
            = openmeta::XmpExistingDestinationEmbeddedPrecedence::DestinationWins;
        options.edit_target_path      = target_path.string();
        options.xmp_sidecar_base_path = target_path.string();
        options.xmp_writeback_mode
            = openmeta::XmpWritebackMode::EmbeddedAndSidecar;
        options.execute.edit_apply = true;

        // This is the destination's sibling sidecar. The source is an in-memory
        // snapshot, so this case does not import a source-sidecar file.
        const openmeta::ExecutePreparedTransferFileResult result
            = openmeta::execute_prepared_transfer_snapshot(
                source_snapshot,
                std::span<const std::byte>(
                    embedded.execute.edited_output.data(),
                    embedded.execute.edited_output.size()),
                options);
        ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
        ASSERT_EQ(result.execute.edit_plan_status,
                  openmeta::TransferStatus::Ok);
        ASSERT_EQ(result.execute.edit_apply.status,
                  openmeta::TransferStatus::Ok);
        ASSERT_TRUE(result.prepared.xmp_existing_sidecar_loaded);
        ASSERT_TRUE(result.xmp_existing_destination_embedded_loaded);
        ASSERT_TRUE(result.xmp_sidecar_requested);
        ASSERT_EQ(result.xmp_sidecar_status, openmeta::TransferStatus::Ok);
        ASSERT_FALSE(result.xmp_sidecar_output.empty());

        const std::string_view expected
            = precedence
                      == openmeta::XmpExistingDestinationCarrierPrecedence::
                          SidecarWins
                  ? "Destination sidecar title"
                  : "Destination embedded title";
        openmeta::ReadTransferSourceSnapshotOptions read_options;
        const openmeta::ReadTransferSourceSnapshotBytesResult edited_read
            = openmeta::read_transfer_source_snapshot_bytes(
                std::span<const std::byte>(result.execute.edited_output.data(),
                                           result.execute.edited_output.size()),
                read_options);
        ASSERT_EQ(edited_read.status, openmeta::TransferStatus::Ok);
        EXPECT_TRUE(has_text(edited_read.snapshot.store,
                             openmeta::make_xmp_property_key_view(
                                 kDcNamespace, "title[@xml:lang=x-default]"),
                             expected));
        EXPECT_FALSE(has_text(edited_read.snapshot.store,
                              openmeta::make_xmp_property_key_view(
                                  kDcNamespace, "title[@xml:lang=x-default]"),
                              "Transfer source title"));

        openmeta::MetaStore sidecar_read;
        openmeta::XmpDecodeOptions decode_options;
        const openmeta::XmpDecodeResult sidecar_decoded
            = openmeta::decode_xmp_packet(result.xmp_sidecar_output,
                                          sidecar_read,
                                          openmeta::EntryFlags::None,
                                          decode_options);
        ASSERT_EQ(sidecar_decoded.status, openmeta::XmpDecodeStatus::Ok);
        sidecar_read.finalize();
        EXPECT_TRUE(has_text(sidecar_read,
                             openmeta::make_xmp_property_key_view(
                                 kDcNamespace, "title[@xml:lang=x-default]"),
                             expected));
        EXPECT_FALSE(has_text(sidecar_read,
                              openmeta::make_xmp_property_key_view(
                                  kDcNamespace, "title[@xml:lang=x-default]"),
                              "Transfer source title"));
    }
}
}  // namespace
