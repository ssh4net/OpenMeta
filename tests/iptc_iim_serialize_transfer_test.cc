// SPDX-License-Identifier: Apache-2.0

#include "openmeta/iptc_iim_decode.h"
#include "openmeta/iptc_iim_serialize.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_translation.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static PrepareTransferRequest iptc_request(TransferTargetFormat target)
    {
        PrepareTransferRequest request;
        request.target_format      = target;
        request.include_exif_app1  = false;
        request.include_xmp_app1   = false;
        request.include_icc_app2   = false;
        request.include_iptc_app13 = true;
        return request;
    }

    static void expect_carriers(const MetaStore& store,
                                std::span<const std::byte> expected,
                                uint32_t warnings = 0U)
    {
        for (const TransferTargetFormat target :
             { TransferTargetFormat::Jpeg, TransferTargetFormat::Tiff }) {
            PreparedTransferBundle bundle;
            const PrepareTransferResult prepared
                = prepare_metadata_for_target(store, iptc_request(target),
                                              &bundle);
            ASSERT_EQ(prepared.status, TransferStatus::Ok);
            EXPECT_EQ(prepared.warnings, warnings);
            ASSERT_EQ(bundle.blocks.size(), 1U);
            EXPECT_EQ(bundle.blocks[0].kind, TransferBlockKind::IptcIim);
            const std::span<const std::byte> payload(bundle.blocks[0].payload);
            if (target == TransferTargetFormat::Tiff) {
                ASSERT_EQ(payload.size(), expected.size());
                EXPECT_EQ(std::memcmp(payload.data(), expected.data(),
                                      expected.size()),
                          0);
            } else {
                ASSERT_EQ(payload.size(),
                          26U + expected.size() + (expected.size() & 1U));
                EXPECT_EQ(std::memcmp(payload.data(), "Photoshop 3.0\0", 14U),
                          0);
                EXPECT_EQ(std::memcmp(payload.data() + 14U, "8BIM\4\4\0\0", 8U),
                          0);
                uint32_t length = 0U;
                for (size_t index = 22U; index < 26U; ++index) {
                    length = (length << 8U)
                             | std::to_integer<uint8_t>(payload[index]);
                }
                EXPECT_EQ(length, expected.size());
                EXPECT_EQ(std::memcmp(payload.data() + 26U, expected.data(),
                                      expected.size()),
                          0);
                if ((expected.size() & 1U) != 0U) {
                    EXPECT_EQ(payload.back(), std::byte { 0 });
                }
            }
        }
    }

    static std::vector<std::byte> canonical(const MetaStore& store)
    {
        const IptcIimSerializeResult measured = serialize_iptc_iim(store, {});
        EXPECT_EQ(measured.status, IptcIimSerializeStatus::OutputTruncated);
        std::vector<std::byte> bytes(static_cast<size_t>(measured.needed));
        EXPECT_TRUE(serialize_iptc_iim(store, bytes).ok());
        return bytes;
    }

}  // namespace

TEST(IptcIimSerializeTransfer,
     GenericAuthoringAndUtf8TranslationMatchBothCarriers)
{
    std::array<MetadataAuthoringEntry, 3> entries;
    entries[0].key   = make_iptc_dataset_key_view(2U, 25U);
    entries[0].value = make_value_view_text("first", TextEncoding::Ascii);
    entries[1]       = entries[0];
    entries[2].key   = make_iptc_dataset_key_view(2U, 5U);
    entries[2].value = make_value_view_text("title", TextEncoding::Ascii);
    MetaStore authored;
    ASSERT_TRUE(create_metadata_store(entries, &authored).ok());
    expect_carriers(authored, canonical(authored));

    constexpr std::string_view dc = "http://purl.org/dc/elements/1.1/";
    entries[0].key                = make_xmp_property_key_view(dc,
                                                               "title[@xml:lang=x-default]");
    entries[0].value = make_value_view_text("Caf\xc3\xa9", TextEncoding::Utf8);
    entries[1].key   = make_xmp_property_key_view(dc, "subject[2]");
    entries[1].value = make_value_view_text("second", TextEncoding::Utf8);
    entries[2].key   = make_xmp_property_key_view(dc, "subject[1]");
    entries[2].value = make_value_view_text("first", TextEncoding::Utf8);
    MetaStore source;
    ASSERT_TRUE(create_metadata_store(entries, &source).ok());
    MetadataIptcTranslationOptions options;
    options.source_mode = MetadataDescriptiveTranslationSourceMode::All;
    MetaStore translated;
    ASSERT_EQ(translate_xmp_iptc_metadata(source, options, &translated).status,
              MetadataDescriptiveTranslationStatus::Ok);
    const std::vector<std::byte> bytes = canonical(translated);
    expect_carriers(translated, bytes);
    MetaStore decoded;
    ASSERT_EQ(decode_iptc_iim(bytes, decoded).status, IptcIimDecodeStatus::Ok);
    decoded.finalize();
    ASSERT_EQ(decoded.entries().size(), 4U);
    EXPECT_EQ(decoded.entry(0U).key.data.iptc_dataset.record, 1U);
    EXPECT_EQ(decoded.entry(0U).key.data.iptc_dataset.dataset, 90U);
    EXPECT_EQ(canonical(decoded), bytes);
}

TEST(IptcIimSerializeTransfer,
     UnsupportedAndDamagedEntriesAreSkippedOnlyByTransfer)
{
    MetaStore store;
    Entry entry;
    entry.key   = make_iptc_dataset_key(2U, 5U);
    entry.value = make_text(store.arena(), "ok", TextEncoding::Ascii);
    ASSERT_NE(store.add_entry(entry), kInvalidEntryId);
    entry.key   = make_iptc_dataset_key(2U, 10U);
    entry.value = make_u8(5U);
    ASSERT_NE(store.add_entry(entry), kInvalidEntryId);
    entry.key   = make_iptc_dataset_key(2U, 25U);
    entry.value = make_text(store.arena(), "damaged", TextEncoding::Ascii);
    entry.flags = EntryFlags::Truncated;
    ASSERT_NE(store.add_entry(entry), kInvalidEntryId);
    entry.flags                  = EntryFlags::None;
    entry.value.data.span.offset = UINT32_MAX;
    ASSERT_NE(store.add_entry(entry), kInvalidEntryId);
    store.finalize();
    EXPECT_EQ(serialize_iptc_iim(store, {}).status,
              IptcIimSerializeStatus::InvalidMetadata);
    constexpr std::array expected = { std::byte { 0x1c }, std::byte { 2 },
                                      std::byte { 5 },    std::byte { 0 },
                                      std::byte { 2 },    std::byte { 'o' },
                                      std::byte { 'k' } };
    expect_carriers(store, expected, 1U);
}

TEST(IptcIimSerializeTransfer, ExtendedLengthAndRawArrayRetainExactPayload)
{
    MetaStore store;
    std::vector<uint16_t> words(16384U, 0x1234U);
    Entry entry;
    entry.key   = make_iptc_dataset_key(9U, 255U);
    entry.value = make_u16_array(store.arena(), words);
    ASSERT_NE(store.add_entry(entry), kInvalidEntryId);
    store.finalize();
    const std::vector<std::byte> bytes = canonical(store);
    ASSERT_EQ(bytes.size(), 32768U + 9U);
    EXPECT_EQ(bytes[3U], std::byte { 0x80 });
    EXPECT_EQ(bytes[4U], std::byte { 4 });
    EXPECT_EQ(bytes[7U], std::byte { 0x80 });
    EXPECT_EQ(std::memcmp(bytes.data() + 9U, words.data(), 32768U), 0);
    expect_carriers(store, bytes);
}

TEST(IptcIimSerializeTransfer,
     OversizeDirtyDatasetFailsWithoutUsingStaleCarrier)
{
    MetaStore store;
    Entry entry;
    entry.key                  = make_photoshop_irb_key(0x0404U);
    constexpr std::array stale = { std::byte { 0x1c }, std::byte { 2 },
                                   std::byte { 5 },    std::byte { 0 },
                                   std::byte { 1 },    std::byte { 'x' } };
    entry.value                = make_bytes(store.arena(), stale);
    ASSERT_NE(store.add_entry(entry), kInvalidEntryId);
    const std::vector<std::byte> large(8U * 1024U * 1024U + 1U,
                                       std::byte { 'a' });
    entry.key   = make_iptc_dataset_key(2U, 120U);
    entry.value = make_bytes(store.arena(), large);
    entry.flags = EntryFlags::Dirty;
    ASSERT_NE(store.add_entry(entry), kInvalidEntryId);
    store.finalize();
    EXPECT_EQ(serialize_iptc_iim(store, {}).status,
              IptcIimSerializeStatus::LimitExceeded);
    for (const TransferTargetFormat target :
         { TransferTargetFormat::Jpeg, TransferTargetFormat::Tiff }) {
        PreparedTransferBundle bundle;
        const PrepareTransferResult result
            = prepare_metadata_for_target(store, iptc_request(target), &bundle);
        EXPECT_EQ(result.code, PrepareTransferCode::IptcPackFailed);
        EXPECT_NE(result.status, TransferStatus::Ok);
        EXPECT_TRUE(bundle.blocks.empty());
    }
}

TEST(IptcIimSerializeTransfer,
     ExistingUnfinalizedAndOpaqueFallbackRemainSupported)
{
    MetaStore store;
    Entry entry;
    entry.key   = make_iptc_dataset_key(2U, 5U);
    entry.value = make_text(store.arena(), "ok", TextEncoding::Ascii);
    ASSERT_NE(store.add_entry(entry), kInvalidEntryId);
    EXPECT_FALSE(store.is_finalized());
    EXPECT_EQ(serialize_iptc_iim(store, {}).status,
              IptcIimSerializeStatus::StoreNotFinalized);
    constexpr std::array expected = { std::byte { 0x1c }, std::byte { 2 },
                                      std::byte { 5 },    std::byte { 0 },
                                      std::byte { 2 },    std::byte { 'o' },
                                      std::byte { 'k' } };
    expect_carriers(store, expected);
    constexpr std::array opaque = { std::byte { 0x1c }, std::byte { 2 },
                                    std::byte { 5 },    std::byte { 0 },
                                    std::byte { 3 },    std::byte { 'o' },
                                    std::byte { 'l' },  std::byte { 'd' } };
    entry.key                   = make_photoshop_irb_key(0x0404U);
    entry.value                 = make_bytes(store.arena(), opaque);
    ASSERT_NE(store.add_entry(entry), kInvalidEntryId);
    store.finalize();
    expect_carriers(store, opaque);
    EXPECT_EQ(canonical(store),
              std::vector<std::byte>(expected.begin(), expected.end()));
}

}  // namespace openmeta
