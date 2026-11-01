// SPDX-License-Identifier: Apache-2.0

#include "openmeta/container_scan.h"
#include "openmeta/icc_serialize.h"

#include "../src/openmeta/icc_serialize_internal.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openmeta {
namespace {

    static void add_header_value(MetaStore* store, uint32_t offset,
                                 MetaValue value)
    {
        Entry entry;
        entry.key   = make_icc_header_field_key(offset);
        entry.value = value;
        store->add_entry(entry);
    }


    static void add_required_header(MetaStore* store)
    {
        add_header_value(store, 8U, make_u32(0x04300000U));
        add_header_value(store, 12U, make_u32(fourcc('m', 'n', 't', 'r')));
        add_header_value(store, 16U, make_u32(fourcc('R', 'G', 'B', ' ')));
        add_header_value(store, 20U, make_u32(fourcc('X', 'Y', 'Z', ' ')));
        const std::array<uint16_t, 6U> date = { 2026U, 1U, 28U, 0U, 0U, 0U };
        add_header_value(store, 24U, make_u16_array(store->arena(), date));
        const std::array<SRational, 3U> illuminant = {
            SRational { 63189, 65536 },
            SRational { 65536, 65536 },
            SRational { 54061, 65536 },
        };
        add_header_value(store, 68U,
                         make_srational_array(store->arena(), illuminant));
    }


    static EntryId add_valid_tag(MetaStore* store, uint32_t signature,
                                 std::byte final_byte = std::byte { 0xA5U })
    {
        const std::array<std::byte, 9U> bytes = {
            std::byte { 't' }, std::byte { 'e' }, std::byte { 'x' },
            std::byte { 't' }, std::byte { 0U },  std::byte { 0U },
            std::byte { 0U },  std::byte { 0U },  final_byte,
        };
        Entry entry;
        entry.key   = make_icc_tag_key(signature);
        entry.value = make_bytes(store->arena(), bytes);
        return store->add_entry(entry);
    }


    static uint32_t read_u32be(std::span<const std::byte> bytes,
                               size_t offset) noexcept
    {
        return (static_cast<uint32_t>(bytes[offset]) << 24U)
               | (static_cast<uint32_t>(bytes[offset + 1U]) << 16U)
               | (static_cast<uint32_t>(bytes[offset + 2U]) << 8U)
               | static_cast<uint32_t>(bytes[offset + 3U]);
    }


    static bool all_bytes_equal(std::span<const std::byte> bytes,
                                std::byte expected) noexcept
    {
        for (const std::byte value : bytes) {
            if (value != expected) {
                return false;
            }
        }
        return true;
    }


    TEST(IccSerialize, EncodesTypedIlluminantAsThreeSignedFixedValues)
    {
        MetaStore store;
        add_required_header(&store);
        add_header_value(&store, 80U, make_u32(fourcc('o', 'p', 'n', 'm')));
        const std::array<std::byte, 16U> profile_id = {
            std::byte { 0x00U }, std::byte { 0x01U }, std::byte { 0x02U },
            std::byte { 0x03U }, std::byte { 0x04U }, std::byte { 0x05U },
            std::byte { 0x06U }, std::byte { 0x07U }, std::byte { 0x08U },
            std::byte { 0x09U }, std::byte { 0x0AU }, std::byte { 0x0BU },
            std::byte { 0x0CU }, std::byte { 0x0DU }, std::byte { 0x0EU },
            std::byte { 0x0FU },
        };
        add_header_value(&store, 84U, make_bytes(store.arena(), profile_id));
        const std::array<std::byte, 9U> tag_bytes = {
            std::byte { 't' }, std::byte { 'e' }, std::byte { 'x' },
            std::byte { 't' }, std::byte { 0U },  std::byte { 0U },
            std::byte { 0U },  std::byte { 0U },  std::byte { 0xA5U },
        };
        Entry tag;
        tag.key   = make_icc_tag_key(fourcc('d', 'e', 's', 'c'));
        tag.value = make_bytes(store.arena(), tag_bytes);
        store.add_entry(tag);
        store.finalize();

        IccProfileSerializeOptions options;
        options.profile_id_policy = IccProfileIdPolicy::Preserve;
        const IccProfileSerializeResult measured
            = serialize_icc_profile(store, {}, options);
        ASSERT_EQ(measured.status, IccProfileSerializeStatus::OutputTruncated);
        ASSERT_GT(measured.needed, 0U);
        ASSERT_EQ(measured.tags_serialized, 1U);
        ASSERT_EQ(measured.header_fields_serialized, 8U);

        std::vector<std::byte> profile(static_cast<size_t>(measured.needed),
                                       std::byte { 0xCDU });
        const IccProfileSerializeResult emitted
            = serialize_icc_profile(store, profile, options);
        ASSERT_EQ(emitted.status, IccProfileSerializeStatus::Ok);
        ASSERT_EQ(emitted.written, profile.size());
        ASSERT_EQ(emitted.needed, profile.size());
        ASSERT_EQ(emitted.tags_serialized, 1U);
        ASSERT_EQ(emitted.header_fields_serialized, 8U);
        EXPECT_EQ(profile[68U], std::byte { 0x00U });
        EXPECT_EQ(profile[69U], std::byte { 0x00U });
        EXPECT_EQ(profile[70U], std::byte { 0xF6U });
        EXPECT_EQ(profile[71U], std::byte { 0xD5U });
        EXPECT_EQ(profile[72U], std::byte { 0x00U });
        EXPECT_EQ(profile[73U], std::byte { 0x01U });
        EXPECT_EQ(profile[74U], std::byte { 0x00U });
        EXPECT_EQ(profile[75U], std::byte { 0x00U });
        EXPECT_EQ(profile[76U], std::byte { 0x00U });
        EXPECT_EQ(profile[77U], std::byte { 0x00U });
        EXPECT_EQ(profile[78U], std::byte { 0xD3U });
        EXPECT_EQ(profile[79U], std::byte { 0x2DU });
        EXPECT_EQ(profile[80U], std::byte { 'o' });
        EXPECT_EQ(profile[81U], std::byte { 'p' });
        EXPECT_EQ(profile[82U], std::byte { 'n' });
        EXPECT_EQ(profile[83U], std::byte { 'm' });
        EXPECT_TRUE(std::equal(profile_id.begin(), profile_id.end(),
                               profile.begin() + 84U));
        for (size_t i = 100U; i < 128U; ++i) {
            EXPECT_EQ(profile[i], std::byte { 0U });
        }
        EXPECT_EQ(read_u32be(profile, 0U), profile.size());
        EXPECT_EQ(profile[36U], std::byte { 'a' });
        EXPECT_EQ(profile[37U], std::byte { 'c' });
        EXPECT_EQ(profile[38U], std::byte { 's' });
        EXPECT_EQ(profile[39U], std::byte { 'p' });
        EXPECT_EQ(read_u32be(profile, 136U), 144U);
        EXPECT_EQ(read_u32be(profile, 140U), 9U);
        EXPECT_EQ(profile.size(), 156U);
        EXPECT_EQ(profile[153U], std::byte { 0U });
        EXPECT_EQ(profile[154U], std::byte { 0U });
        EXPECT_EQ(profile[155U], std::byte { 0U });
    }


    TEST(IccSerialize, MeasuresPrefixAndClearsProfileIdByDefault)
    {
        MetaStore store;
        add_required_header(&store);
        const std::array<std::byte, 16U> profile_id = {
            std::byte { 0x10U }, std::byte { 0x11U }, std::byte { 0x12U },
            std::byte { 0x13U }, std::byte { 0x14U }, std::byte { 0x15U },
            std::byte { 0x16U }, std::byte { 0x17U }, std::byte { 0x18U },
            std::byte { 0x19U }, std::byte { 0x1AU }, std::byte { 0x1BU },
            std::byte { 0x1CU }, std::byte { 0x1DU }, std::byte { 0x1EU },
            std::byte { 0x1FU },
        };
        add_header_value(&store, 84U, make_bytes(store.arena(), profile_id));
        add_valid_tag(&store, fourcc('d', 'e', 's', 'c'));
        store.finalize();

        const IccProfileSerializeResult measured = serialize_icc_profile(store,
                                                                         {});
        ASSERT_EQ(measured.status, IccProfileSerializeStatus::OutputTruncated);
        ASSERT_GT(measured.needed, 0U);
        ASSERT_EQ(measured.tags_serialized, 1U);
        ASSERT_EQ(measured.header_fields_serialized, 7U);

        std::vector<std::byte> full(static_cast<size_t>(measured.needed) + 3U,
                                    std::byte { 0xCCU });
        const IccProfileSerializeResult emitted = serialize_icc_profile(store,
                                                                        full);
        ASSERT_EQ(emitted.status, IccProfileSerializeStatus::Ok);
        ASSERT_EQ(emitted.written, measured.needed);
        ASSERT_EQ(emitted.needed, measured.needed);
        EXPECT_EQ(full[measured.needed], std::byte { 0xCCU });
        EXPECT_EQ(full[measured.needed + 1U], std::byte { 0xCCU });
        EXPECT_EQ(full[measured.needed + 2U], std::byte { 0xCCU });
        for (size_t i = 84U; i < 100U; ++i) {
            EXPECT_EQ(full[i], std::byte { 0U });
        }

        std::array<std::byte, 7U> short_output {};
        const IccProfileSerializeResult short_result
            = serialize_icc_profile(store, short_output);
        EXPECT_EQ(short_result.status,
                  IccProfileSerializeStatus::OutputTruncated);
        EXPECT_EQ(short_result.needed, measured.needed);
        EXPECT_EQ(short_result.written, short_output.size());
        EXPECT_TRUE(
            std::equal(short_output.begin(), short_output.end(), full.begin()));
    }


    TEST(IccSerialize, OrdersOpaquePrivateTagsAndAcceptsExactRawHeaderWidths)
    {
        MetaStore store;
        const std::array<std::byte, 4U> version = {
            std::byte { 0x04U },
            std::byte { 0x40U },
            std::byte { 0x00U },
            std::byte { 0x00U },
        };
        const std::array<std::byte, 4U> device_class = {
            std::byte { 'm' },
            std::byte { 'n' },
            std::byte { 't' },
            std::byte { 'r' },
        };
        const std::array<std::byte, 4U> data_space = {
            std::byte { 'R' },
            std::byte { 'G' },
            std::byte { 'B' },
            std::byte { ' ' },
        };
        const std::array<std::byte, 4U> pcs = {
            std::byte { 'X' },
            std::byte { 'Y' },
            std::byte { 'Z' },
            std::byte { ' ' },
        };
        const std::array<std::byte, 12U> date = {
            std::byte { 0x07U }, std::byte { 0xEAU }, std::byte { 0x00U },
            std::byte { 0x0BU }, std::byte { 0x00U }, std::byte { 0x02U },
            std::byte { 0x00U }, std::byte { 0x03U }, std::byte { 0x00U },
            std::byte { 0x04U }, std::byte { 0x00U }, std::byte { 0x05U },
        };
        const std::array<std::byte, 12U> illuminant = {
            std::byte { 0x00U }, std::byte { 0x00U }, std::byte { 0xF6U },
            std::byte { 0xD6U }, std::byte { 0x00U }, std::byte { 0x01U },
            std::byte { 0x00U }, std::byte { 0x00U }, std::byte { 0x00U },
            std::byte { 0x00U }, std::byte { 0xD3U }, std::byte { 0x2DU },
        };
        add_header_value(&store, 8U, make_bytes(store.arena(), version));
        add_header_value(&store, 12U, make_bytes(store.arena(), device_class));
        add_header_value(&store, 16U, make_bytes(store.arena(), data_space));
        add_header_value(&store, 20U, make_bytes(store.arena(), pcs));
        add_header_value(&store, 24U, make_bytes(store.arena(), date));
        add_header_value(&store, 68U, make_bytes(store.arena(), illuminant));
        add_valid_tag(&store, 0x7A7A7A31U, std::byte { 0x71U });
        add_valid_tag(&store, 0x61616131U, std::byte { 0x61U });
        store.finalize();

        const IccProfileSerializeResult measured = serialize_icc_profile(store,
                                                                         {});
        ASSERT_EQ(measured.status, IccProfileSerializeStatus::OutputTruncated);
        std::vector<std::byte> profile(static_cast<size_t>(measured.needed));
        const IccProfileSerializeResult emitted
            = serialize_icc_profile(store, profile);
        ASSERT_EQ(emitted.status, IccProfileSerializeStatus::Ok);
        EXPECT_EQ(read_u32be(profile, 132U), 0x61616131U);
        EXPECT_EQ(read_u32be(profile, 136U), 156U);
        EXPECT_EQ(read_u32be(profile, 140U), 9U);
        EXPECT_EQ(read_u32be(profile, 144U), 0x7A7A7A31U);
        EXPECT_EQ(read_u32be(profile, 148U), 168U);
        EXPECT_EQ(read_u32be(profile, 152U), 9U);
        EXPECT_TRUE(std::equal(illuminant.begin(), illuminant.end(),
                               profile.begin() + 68U));
        EXPECT_EQ(profile.size(), 180U);
    }


    TEST(IccSerialize, RejectsDuplicatesAndNonCanonicalValueShapes)
    {
        MetaStore duplicate_tag;
        add_required_header(&duplicate_tag);
        const EntryId first_tag  = add_valid_tag(&duplicate_tag,
                                                 fourcc('d', 'e', 's', 'c'));
        const EntryId second_tag = add_valid_tag(&duplicate_tag,
                                                 fourcc('d', 'e', 's', 'c'));
        duplicate_tag.finalize();
        ASSERT_NE(first_tag, kInvalidEntryId);
        std::array<std::byte, 16U> untouched;
        untouched.fill(std::byte { 0xB7U });
        const IccProfileSerializeResult duplicate_result
            = serialize_icc_profile(duplicate_tag, untouched);
        EXPECT_EQ(duplicate_result.status,
                  IccProfileSerializeStatus::InvalidMetadata);
        EXPECT_EQ(duplicate_result.failed_entry, second_tag);
        EXPECT_EQ(duplicate_result.written, 0U);
        EXPECT_EQ(duplicate_result.needed, 0U);
        EXPECT_TRUE(all_bytes_equal(untouched, std::byte { 0xB7U }));

        MetaStore duplicate_header;
        add_required_header(&duplicate_header);
        add_header_value(&duplicate_header, 4U, make_u32(0x41424344U));
        add_header_value(&duplicate_header, 4U, make_u32(0x45464748U));
        add_valid_tag(&duplicate_header, fourcc('d', 'e', 's', 'c'));
        duplicate_header.finalize();
        EXPECT_EQ(serialize_icc_profile(duplicate_header, {}).status,
                  IccProfileSerializeStatus::InvalidMetadata);

        MetaStore array_tag;
        add_required_header(&array_tag);
        const std::array<uint8_t, 9U> payload = {
            't', 'e', 'x', 't', 0U, 0U, 0U, 0U, 0x55U,
        };
        Entry array_entry;
        array_entry.key   = make_icc_tag_key(fourcc('d', 'e', 's', 'c'));
        array_entry.value = make_u8_array(array_tag.arena(), payload);
        array_tag.add_entry(array_entry);
        array_tag.finalize();
        EXPECT_EQ(serialize_icc_profile(array_tag, {}).status,
                  IccProfileSerializeStatus::InvalidMetadata);

        MetaStore wrong_hint;
        add_required_header(&wrong_hint);
        const std::array<std::byte, 4U> raw_u32 = {
            std::byte { 0x41U },
            std::byte { 0x42U },
            std::byte { 0x43U },
            std::byte { 0x44U },
        };
        MetaValue bad_bytes = make_bytes(wrong_hint.arena(), raw_u32);
        bad_bytes.elem_type = MetaElementType::U16;
        add_header_value(&wrong_hint, 4U, bad_bytes);
        add_valid_tag(&wrong_hint, fourcc('d', 'e', 's', 'c'));
        wrong_hint.finalize();
        EXPECT_EQ(serialize_icc_profile(wrong_hint, {}).status,
                  IccProfileSerializeStatus::InvalidMetadata);

        MetaStore wrong_encoding;
        add_required_header(&wrong_encoding);
        MetaValue encoded_number     = make_u32(0x41424344U);
        encoded_number.text_encoding = TextEncoding::Utf8;
        add_header_value(&wrong_encoding, 4U, encoded_number);
        add_valid_tag(&wrong_encoding, fourcc('d', 'e', 's', 'c'));
        wrong_encoding.finalize();
        EXPECT_EQ(serialize_icc_profile(wrong_encoding, {}).status,
                  IccProfileSerializeStatus::InvalidMetadata);
    }


    TEST(IccSerialize, RejectsMalformedEnvelopesAndUnavailableEntries)
    {
        const std::array<std::byte, 8U> zero_type = {
            std::byte { 0U }, std::byte { 0U }, std::byte { 0U },
            std::byte { 0U }, std::byte { 0U }, std::byte { 0U },
            std::byte { 0U }, std::byte { 0U },
        };
        MetaStore invalid_type;
        add_required_header(&invalid_type);
        Entry zero_type_entry;
        zero_type_entry.key   = make_icc_tag_key(fourcc('d', 'e', 's', 'c'));
        zero_type_entry.value = make_bytes(invalid_type.arena(), zero_type);
        invalid_type.add_entry(zero_type_entry);
        invalid_type.finalize();
        EXPECT_EQ(serialize_icc_profile(invalid_type, {}).status,
                  IccProfileSerializeStatus::InvalidMetadata);

        const std::array<std::byte, 8U> nonzero_reserved = {
            std::byte { 't' }, std::byte { 'e' }, std::byte { 's' },
            std::byte { 't' }, std::byte { 0U },  std::byte { 0U },
            std::byte { 1U },  std::byte { 0U },
        };
        MetaStore invalid_reserved;
        add_required_header(&invalid_reserved);
        Entry reserved_entry;
        reserved_entry.key   = make_icc_tag_key(fourcc('d', 'e', 's', 'c'));
        reserved_entry.value = make_bytes(invalid_reserved.arena(),
                                          nonzero_reserved);
        invalid_reserved.add_entry(reserved_entry);
        invalid_reserved.finalize();
        EXPECT_EQ(serialize_icc_profile(invalid_reserved, {}).status,
                  IccProfileSerializeStatus::InvalidMetadata);

        const std::array<std::byte, 3U> short_header = {
            std::byte { 0x41U },
            std::byte { 0x42U },
            std::byte { 0x43U },
        };
        MetaStore invalid_width;
        add_required_header(&invalid_width);
        add_header_value(&invalid_width, 4U,
                         make_bytes(invalid_width.arena(), short_header));
        add_valid_tag(&invalid_width, fourcc('d', 'e', 's', 'c'));
        invalid_width.finalize();
        EXPECT_EQ(serialize_icc_profile(invalid_width, {}).status,
                  IccProfileSerializeStatus::InvalidMetadata);

        MetaStore unavailable;
        add_required_header(&unavailable);
        const std::array<std::byte, 8U> valid_envelope = {
            std::byte { 't' }, std::byte { 'e' }, std::byte { 's' },
            std::byte { 't' }, std::byte { 0U },  std::byte { 0U },
            std::byte { 0U },  std::byte { 0U },
        };
        Entry unreadable;
        unreadable.key   = make_icc_tag_key(fourcc('d', 'e', 's', 'c'));
        unreadable.value = make_bytes(unavailable.arena(), valid_envelope);
        unreadable.flags = EntryFlags::Unreadable;
        const EntryId unreadable_id = unavailable.add_entry(unreadable);
        unavailable.finalize();

        std::array<std::byte, 16U> untouched;
        untouched.fill(std::byte { 0x6BU });
        const IccProfileSerializeResult unavailable_result
            = serialize_icc_profile(unavailable, untouched);
        EXPECT_EQ(unavailable_result.status,
                  IccProfileSerializeStatus::InvalidMetadata);
        EXPECT_EQ(unavailable_result.failed_entry, unreadable_id);
        EXPECT_TRUE(all_bytes_equal(untouched, std::byte { 0x6BU }));

        MetaStore truncated;
        add_required_header(&truncated);
        Entry truncated_entry;
        truncated_entry.key   = make_icc_tag_key(fourcc('d', 'e', 's', 'c'));
        truncated_entry.value = make_bytes(truncated.arena(), valid_envelope);
        truncated_entry.flags = EntryFlags::Truncated;
        const EntryId truncated_id = truncated.add_entry(truncated_entry);
        truncated.finalize();
        const IccProfileSerializeResult truncated_result
            = serialize_icc_profile(truncated, {});
        EXPECT_EQ(truncated_result.status,
                  IccProfileSerializeStatus::InvalidMetadata);
        EXPECT_EQ(truncated_result.failed_entry, truncated_id);
    }


    TEST(IccSerialize, RejectsInvalidDateAndUnrepresentableIlluminant)
    {
        MetaStore invalid_date;
        add_header_value(&invalid_date, 8U, make_u32(0x04300000U));
        add_header_value(&invalid_date, 12U,
                         make_u32(fourcc('m', 'n', 't', 'r')));
        add_header_value(&invalid_date, 16U,
                         make_u32(fourcc('R', 'G', 'B', ' ')));
        add_header_value(&invalid_date, 20U,
                         make_u32(fourcc('X', 'Y', 'Z', ' ')));
        const std::array<uint16_t, 6U> impossible_date = { 2026U, 2U, 29U,
                                                           0U,    0U, 0U };
        add_header_value(&invalid_date, 24U,
                         make_u16_array(invalid_date.arena(), impossible_date));
        const std::array<SRational, 3U> valid_illuminant = {
            SRational { 63189, 65536 },
            SRational { 65536, 65536 },
            SRational { 54061, 65536 },
        };
        add_header_value(&invalid_date, 68U,
                         make_srational_array(invalid_date.arena(),
                                              valid_illuminant));
        add_valid_tag(&invalid_date, fourcc('d', 'e', 's', 'c'));
        invalid_date.finalize();
        EXPECT_EQ(serialize_icc_profile(invalid_date, {}).status,
                  IccProfileSerializeStatus::InvalidMetadata);

        MetaStore invalid_illuminant;
        add_header_value(&invalid_illuminant, 8U, make_u32(0x04300000U));
        add_header_value(&invalid_illuminant, 12U,
                         make_u32(fourcc('m', 'n', 't', 'r')));
        add_header_value(&invalid_illuminant, 16U,
                         make_u32(fourcc('R', 'G', 'B', ' ')));
        add_header_value(&invalid_illuminant, 20U,
                         make_u32(fourcc('X', 'Y', 'Z', ' ')));
        const std::array<uint16_t, 6U> valid_date
            = { 2026U, 2U, 28U, 0U, 0U, 0U };
        add_header_value(&invalid_illuminant, 24U,
                         make_u16_array(invalid_illuminant.arena(), valid_date));
        const std::array<SRational, 3U> inexact_illuminant = {
            SRational { 1, 3 },
            SRational { 65536, 65536 },
            SRational { 54061, 65536 },
        };
        add_header_value(&invalid_illuminant, 68U,
                         make_srational_array(invalid_illuminant.arena(),
                                              inexact_illuminant));
        add_valid_tag(&invalid_illuminant, fourcc('d', 'e', 's', 'c'));
        invalid_illuminant.finalize();
        EXPECT_EQ(serialize_icc_profile(invalid_illuminant, {}).status,
                  IccProfileSerializeStatus::InvalidMetadata);
    }


    TEST(IccSerialize, ReportsLimitsAliasAndEmptyProfilesWithoutWriting)
    {
        MetaStore store;
        add_required_header(&store);
        const EntryId tag_id = add_valid_tag(&store,
                                             fourcc('d', 'e', 's', 'c'));
        store.finalize();
        const IccProfileSerializeResult measured = serialize_icc_profile(store,
                                                                         {});
        ASSERT_EQ(measured.status, IccProfileSerializeStatus::OutputTruncated);

        IccProfileSerializeOptions invalid_options;
        invalid_options.max_tags = 0U;
        EXPECT_EQ(serialize_icc_profile(store, {}, invalid_options).status,
                  IccProfileSerializeStatus::InvalidOptions);

        IccProfileSerializeOptions tag_limit;
        tag_limit.max_tag_bytes = 8U;
        const IccProfileSerializeResult too_large_tag
            = serialize_icc_profile(store, {}, tag_limit);
        EXPECT_EQ(too_large_tag.status,
                  IccProfileSerializeStatus::LimitExceeded);
        EXPECT_EQ(too_large_tag.failed_entry, tag_id);

        MetaStore tag_count_store;
        add_required_header(&tag_count_store);
        add_valid_tag(&tag_count_store, 0x61616131U);
        const EntryId over_count_id = add_valid_tag(&tag_count_store,
                                                    0x7A7A7A31U);
        tag_count_store.finalize();
        IccProfileSerializeOptions tag_count_limit;
        tag_count_limit.max_tags = 1U;
        const IccProfileSerializeResult too_many_tags
            = serialize_icc_profile(tag_count_store, {}, tag_count_limit);
        EXPECT_EQ(too_many_tags.status,
                  IccProfileSerializeStatus::LimitExceeded);
        EXPECT_EQ(too_many_tags.failed_entry, over_count_id);

        IccProfileSerializeOptions output_limit;
        output_limit.max_output_bytes = measured.needed - 1U;
        const IccProfileSerializeResult too_large_output
            = serialize_icc_profile(store, {}, output_limit);
        EXPECT_EQ(too_large_output.status,
                  IccProfileSerializeStatus::LimitExceeded);
        EXPECT_EQ(too_large_output.failed_entry, tag_id);

        std::vector<std::byte> untouched(static_cast<size_t>(measured.needed),
                                         std::byte { 0xE1U });
        const IccProfileSerializeResult limited_write
            = serialize_icc_profile(store, untouched, output_limit);
        EXPECT_EQ(limited_write.status,
                  IccProfileSerializeStatus::LimitExceeded);
        EXPECT_EQ(limited_write.written, 0U);
        EXPECT_TRUE(all_bytes_equal(untouched, std::byte { 0xE1U }));

        std::span<std::byte> arena = store.arena().bytes_mut();
        const std::vector<std::byte> arena_before(arena.begin(), arena.end());
        const IccProfileSerializeResult alias = serialize_icc_profile(store,
                                                                      arena);
        EXPECT_EQ(alias.status, IccProfileSerializeStatus::InvalidOptions);
        EXPECT_TRUE(std::equal(arena.begin(), arena.end(), arena_before.begin(),
                               arena_before.end()));

        MetaStore no_tags;
        add_required_header(&no_tags);
        no_tags.finalize();
        const IccProfileSerializeResult empty = serialize_icc_profile(no_tags,
                                                                      {});
        EXPECT_EQ(empty.status, IccProfileSerializeStatus::NoIccData);
        EXPECT_EQ(empty.needed, 0U);
        EXPECT_EQ(empty.written, 0U);

        MetaStore unfinalized;
        add_required_header(&unfinalized);
        add_valid_tag(&unfinalized, fourcc('d', 'e', 's', 'c'));
        std::array<std::byte, 8U> unfinalized_output;
        unfinalized_output.fill(std::byte { 0x2AU });
        const IccProfileSerializeResult unfinalized_result
            = serialize_icc_profile(unfinalized, unfinalized_output);
        EXPECT_EQ(unfinalized_result.status,
                  IccProfileSerializeStatus::StoreNotFinalized);
        EXPECT_TRUE(all_bytes_equal(unfinalized_output, std::byte { 0x2AU }));
    }


    TEST(IccSerialize, SharedEmitterSupportsBorrowingFromDestination)
    {
        std::vector<std::byte> profile(160U);
        for (size_t i = 0U; i < profile.size(); ++i) {
            profile[i] = static_cast<std::byte>(i & 0xFFU);
        }
        std::array<std::byte, 8U> expected_payload {};
        std::copy(profile.begin() + 152U, profile.end(),
                  expected_payload.begin());

        const std::span<const std::byte, 128U> header(profile.data(), 128U);
        detail::IccProfileTagView tag;
        tag.signature = fourcc('t', 'e', 's', 't');
        tag.bytes     = std::span<const std::byte>(profile.data() + 152U, 8U);
        const std::array<detail::IccProfileTagView, 1U> tags = { tag };
        ASSERT_TRUE(detail::build_icc_profile_bytes_from_items(
            header, tags, detail::IccProfileFinalTagPadding::FourByte,
            &profile));

        ASSERT_EQ(profile.size(), 152U);
        EXPECT_TRUE(std::equal(expected_payload.begin(), expected_payload.end(),
                               profile.begin() + 144U));
        EXPECT_EQ(read_u32be(profile, 132U), tag.signature);
        EXPECT_EQ(read_u32be(profile, 136U), 144U);
        EXPECT_EQ(read_u32be(profile, 140U), 8U);
    }

}  // namespace
}  // namespace openmeta
