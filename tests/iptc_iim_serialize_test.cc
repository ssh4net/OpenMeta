// SPDX-License-Identifier: Apache-2.0

#include "openmeta/iptc_iim_decode.h"
#include "openmeta/iptc_iim_serialize.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static EntryId add_dataset(MetaStore& store, uint16_t record,
                               uint16_t dataset, uint32_t source_order,
                               MetaValue value,
                               EntryFlags flags = EntryFlags::None)
    {
        Entry entry;
        entry.key                   = make_iptc_dataset_key(record, dataset);
        entry.value                 = value;
        entry.origin.order_in_block = source_order;
        entry.flags                 = flags;
        return store.add_entry(entry);
    }


    static EntryId add_text_dataset(MetaStore& store, uint16_t record,
                                    uint16_t dataset, uint32_t source_order,
                                    std::string_view payload,
                                    EntryFlags flags = EntryFlags::None)
    {
        return add_dataset(store, record, dataset, source_order,
                           make_text(store.arena(), payload, TextEncoding::Utf8),
                           flags);
    }


    static void append_expected_dataset(std::vector<std::byte>* output,
                                        uint8_t record, uint8_t dataset,
                                        std::span<const std::byte> payload)
    {
        output->push_back(std::byte { 0x1CU });
        output->push_back(static_cast<std::byte>(record));
        output->push_back(static_cast<std::byte>(dataset));
        output->push_back(
            static_cast<std::byte>(static_cast<uint8_t>(payload.size() >> 8U)));
        output->push_back(static_cast<std::byte>(
            static_cast<uint8_t>(payload.size() & 0xFFU)));
        output->insert(output->end(), payload.begin(), payload.end());
    }


    static std::span<const std::byte> text_bytes(std::string_view text) noexcept
    {
        return std::as_bytes(std::span<const char>(text.data(), text.size()));
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


    TEST(IptcIimSerialize, OrdersCharsetAndDuplicatesAndHonorsDatasetLimit)
    {
        MetaStore store;
        add_text_dataset(store, 2U, 5U, 3U, "late");
        add_text_dataset(store, 1U, 90U, 9U, "UTF8");
        add_text_dataset(store, 1U, 9U, 2U, "dupA");
        add_text_dataset(store, 1U, 4U, 2U, "middle");
        add_text_dataset(store, 1U, 9U, 2U, "dupB");
        add_text_dataset(store, 1U, 5U, 1U, "early");
        store.finalize();

        const IptcIimSerializeResult measured = serialize_iptc_iim(store, {});
        EXPECT_EQ(measured.status, IptcIimSerializeStatus::OutputTruncated);
        EXPECT_EQ(measured.written, 0U);
        EXPECT_EQ(measured.entries_serialized, 6U);

        std::vector<std::byte> expected;
        append_expected_dataset(&expected, 1U, 90U, text_bytes("UTF8"));
        append_expected_dataset(&expected, 1U, 5U, text_bytes("early"));
        append_expected_dataset(&expected, 1U, 4U, text_bytes("middle"));
        append_expected_dataset(&expected, 1U, 9U, text_bytes("dupA"));
        append_expected_dataset(&expected, 1U, 9U, text_bytes("dupB"));
        append_expected_dataset(&expected, 2U, 5U, text_bytes("late"));
        EXPECT_EQ(measured.needed, expected.size());

        std::vector<std::byte> actual(static_cast<size_t>(measured.needed));
        const IptcIimSerializeResult emitted = serialize_iptc_iim(store,
                                                                  actual);
        EXPECT_EQ(emitted.status, IptcIimSerializeStatus::Ok);
        EXPECT_EQ(emitted.written, expected.size());
        EXPECT_EQ(actual, expected);

        IptcIimSerializeOptions limited;
        limited.max_datasets = 5U;
        std::array<std::byte, 1> untouched { std::byte { 0xA5U } };
        const IptcIimSerializeResult rejected
            = serialize_iptc_iim(store, std::span<std::byte>(untouched),
                                 limited);
        EXPECT_EQ(rejected.status, IptcIimSerializeStatus::LimitExceeded);
        EXPECT_EQ(rejected.failed_entry, 5U);
        EXPECT_EQ(rejected.written, 0U);
        EXPECT_EQ(rejected.needed, 0U);
        EXPECT_EQ(untouched[0], std::byte { 0xA5U });
    }


    TEST(IptcIimSerialize, EncodesBoundaryLengthsAndRoundTripsThroughDecoder)
    {
        struct LengthCase final {
            uint32_t payload_size;
            std::array<std::byte, 9> header;
            size_t header_size;
        };
        const std::array cases = {
            LengthCase { 0U,
                         { std::byte { 0x1CU }, std::byte { 1U },
                           std::byte { 5U }, std::byte { 0U },
                           std::byte { 0U } },
                         5U },
            LengthCase { 32767U,
                         { std::byte { 0x1CU }, std::byte { 1U },
                           std::byte { 5U }, std::byte { 0x7FU },
                           std::byte { 0xFFU } },
                         5U },
            LengthCase { 32768U,
                         { std::byte { 0x1CU }, std::byte { 1U },
                           std::byte { 5U }, std::byte { 0x80U },
                           std::byte { 4U }, std::byte { 0U }, std::byte { 0U },
                           std::byte { 0x80U }, std::byte { 0U } },
                         9U },
        };

        for (const LengthCase& length_case : cases) {
            MetaStore store;
            std::vector<std::byte> payload(length_case.payload_size,
                                           std::byte { 0xC3U });
            const MetaValue value = make_bytes(store.arena(), payload);
            add_dataset(store, 1U, 5U, 0U, value);
            store.finalize();

            const IptcIimSerializeResult measured = serialize_iptc_iim(store,
                                                                       {});
            ASSERT_EQ(measured.status, IptcIimSerializeStatus::OutputTruncated);
            ASSERT_EQ(measured.needed,
                      length_case.header_size + length_case.payload_size);
            std::vector<std::byte> output(static_cast<size_t>(measured.needed));
            const IptcIimSerializeResult emitted = serialize_iptc_iim(store,
                                                                      output);
            ASSERT_EQ(emitted.status, IptcIimSerializeStatus::Ok);
            ASSERT_EQ(emitted.entries_serialized, 1U);
            ASSERT_TRUE(
                std::equal(length_case.header.begin(),
                           length_case.header.begin() + length_case.header_size,
                           output.begin()));

            MetaStore decoded;
            const IptcIimDecodeResult decode = decode_iptc_iim(output, decoded);
            ASSERT_EQ(decode.status, IptcIimDecodeStatus::Ok);
            ASSERT_EQ(decode.entries_decoded, 1U);
            ASSERT_EQ(decoded.entries().size(), 1U);
            const std::span<const std::byte> decoded_payload
                = decoded.arena().span(decoded.entries()[0].value.data.span);
            EXPECT_EQ(decoded_payload.size(), payload.size());
            EXPECT_TRUE(std::equal(decoded_payload.begin(),
                                   decoded_payload.end(), payload.begin(),
                                   payload.end()));
        }
    }


    TEST(IptcIimSerialize, PreservesArrayBytesAndDoesNotValidateTextSemantics)
    {
        MetaStore store;
        const std::array<std::byte, 4> array_payload
            = { std::byte { 0x12U }, std::byte { 0x34U }, std::byte { 0xABU },
                std::byte { 0xCDU } };
        const MetaValue array = make_array(store.arena(), MetaElementType::U16,
                                           array_payload, 2U);
        add_dataset(store, 1U, 5U, 0U, array);

        const std::array<std::byte, 2> text_payload = { std::byte { 0xFFU },
                                                        std::byte { 0x00U } };
        MetaValue text;
        text.kind          = MetaValueKind::Text;
        text.text_encoding = TextEncoding::Utf8;
        text.count         = static_cast<uint32_t>(text_payload.size());
        text.data.span     = store.arena().append(text_payload);
        add_dataset(store, 1U, 6U, 1U, text);
        const MetaValue empty_array
            = make_array(store.arena(), MetaElementType::U8,
                         std::span<const std::byte> {}, 1U);
        add_dataset(store, 1U, 7U, 2U, empty_array);
        store.finalize();

        const IptcIimSerializeResult measured = serialize_iptc_iim(store, {});
        ASSERT_EQ(measured.status, IptcIimSerializeStatus::OutputTruncated);
        std::vector<std::byte> output(static_cast<size_t>(measured.needed));
        const IptcIimSerializeResult emitted = serialize_iptc_iim(store,
                                                                  output);
        ASSERT_EQ(emitted.status, IptcIimSerializeStatus::Ok);

        std::vector<std::byte> expected;
        append_expected_dataset(&expected, 1U, 5U, array_payload);
        append_expected_dataset(&expected, 1U, 6U, text_payload);
        append_expected_dataset(&expected, 1U, 7U, {});
        EXPECT_EQ(output, expected);
        EXPECT_EQ(emitted.entries_serialized, 3U);
    }


    enum class InvalidEntryShape : uint8_t {
        ValidBytes,
        Empty,
        Scalar,
        BytesCount,
        TextCount,
        ArrayType,
        ArrayCount,
        BadSpan,
    };

    struct InvalidEntryCase final {
        InvalidEntryShape shape;
        EntryFlags flags;
        uint16_t record;
        uint16_t dataset;
    };


    static MetaStore make_invalid_store(const InvalidEntryCase& test_case)
    {
        MetaStore store;
        add_text_dataset(store, 2U, 5U, 0U, "valid prefix");
        Entry entry;
        entry.key = make_iptc_dataset_key(test_case.record, test_case.dataset);
        entry.flags                            = test_case.flags;
        const std::array<std::byte, 2> payload = { std::byte { 0x21U },
                                                   std::byte { 0x43U } };
        switch (test_case.shape) {
        case InvalidEntryShape::ValidBytes:
            entry.value = make_bytes(store.arena(), payload);
            break;
        case InvalidEntryShape::Empty: entry.value = MetaValue {}; break;
        case InvalidEntryShape::Scalar: entry.value = make_u8(1U); break;
        case InvalidEntryShape::BytesCount:
            entry.value       = make_bytes(store.arena(), payload);
            entry.value.count = 1U;
            break;
        case InvalidEntryShape::TextCount:
            entry.value = make_text(store.arena(), "xy", TextEncoding::Utf8);
            entry.value.count = 1U;
            break;
        case InvalidEntryShape::ArrayType:
            entry.value
                = make_array(store.arena(), MetaElementType::U8,
                             std::span<const std::byte>(payload).first(1U), 1U);
            entry.value.elem_type = static_cast<MetaElementType>(0xFFU);
            break;
        case InvalidEntryShape::ArrayCount:
            entry.value       = make_array(store.arena(), MetaElementType::U16,
                                           payload, 2U);
            entry.value.count = 2U;
            break;
        case InvalidEntryShape::BadSpan:
            entry.value.kind      = MetaValueKind::Bytes;
            entry.value.count     = 1U;
            entry.value.data.span = ByteSpan { 100U, 1U };
            break;
        }
        (void)store.add_entry(entry);
        store.finalize();
        return store;
    }


    TEST(IptcIimSerialize, RejectsInvalidKindsShapesFlagsAndKeysAtomically)
    {
        const std::array cases = {
            InvalidEntryCase { InvalidEntryShape::Empty, EntryFlags::None, 1U,
                               5U },
            InvalidEntryCase { InvalidEntryShape::Scalar, EntryFlags::None, 1U,
                               5U },
            InvalidEntryCase { InvalidEntryShape::BytesCount, EntryFlags::None,
                               1U, 5U },
            InvalidEntryCase { InvalidEntryShape::TextCount, EntryFlags::None,
                               1U, 5U },
            InvalidEntryCase { InvalidEntryShape::ArrayType, EntryFlags::None,
                               1U, 5U },
            InvalidEntryCase { InvalidEntryShape::ArrayCount, EntryFlags::None,
                               1U, 5U },
            InvalidEntryCase { InvalidEntryShape::BadSpan, EntryFlags::None, 1U,
                               5U },
            InvalidEntryCase { InvalidEntryShape::ValidBytes,
                               EntryFlags::Truncated, 1U, 5U },
            InvalidEntryCase { InvalidEntryShape::ValidBytes,
                               EntryFlags::Unreadable, 1U, 5U },
            InvalidEntryCase { InvalidEntryShape::ValidBytes, EntryFlags::None,
                               256U, 5U },
            InvalidEntryCase { InvalidEntryShape::ValidBytes, EntryFlags::None,
                               1U, 256U },
        };

        for (const InvalidEntryCase& test_case : cases) {
            MetaStore store = make_invalid_store(test_case);
            std::array<std::byte, 4> output
                = { std::byte { 0xA5U }, std::byte { 0xA5U },
                    std::byte { 0xA5U }, std::byte { 0xA5U } };
            const IptcIimSerializeResult result = serialize_iptc_iim(store,
                                                                     output);
            EXPECT_EQ(result.status, IptcIimSerializeStatus::InvalidMetadata);
            EXPECT_EQ(result.failed_entry, 1U);
            EXPECT_EQ(result.written, 0U);
            EXPECT_EQ(result.needed, 0U);
            EXPECT_TRUE(all_bytes_equal(output, std::byte { 0xA5U }));
        }
    }


    TEST(IptcIimSerialize, AppliesExactLimitsAndIgnoresDeletedAndOtherFamilies)
    {
        MetaStore store;
        add_text_dataset(store, 1U, 5U, 0U, "xy");
        add_text_dataset(store, 1U, 6U, 1U, "z");
        add_dataset(store, 256U, 5U, 1U, make_u8(1U), EntryFlags::Deleted);
        Entry unrelated;
        unrelated.key.kind = MetaKeyKind::Comment;
        unrelated.value    = make_u8(1U);
        unrelated.flags    = EntryFlags::Truncated;
        (void)store.add_entry(unrelated);
        store.finalize();

        IptcIimSerializeOptions exact;
        exact.max_datasets      = 2U;
        exact.max_dataset_bytes = 2U;
        exact.max_output_bytes  = 13U;
        std::array<std::byte, 13> output {};
        const IptcIimSerializeResult accepted
            = serialize_iptc_iim(store, output, exact);
        EXPECT_EQ(accepted.status, IptcIimSerializeStatus::Ok);
        EXPECT_EQ(accepted.entries_serialized, 2U);
        EXPECT_EQ(accepted.entries_skipped, 0U);
        EXPECT_EQ(accepted.needed, 13U);
        EXPECT_EQ(accepted.written, 13U);

        std::array<std::byte, 13> untouched;
        untouched.fill(std::byte { 0xA5U });
        IptcIimSerializeOptions output_too_small = exact;
        output_too_small.max_output_bytes        = 12U;
        const IptcIimSerializeResult output_limit
            = serialize_iptc_iim(store, untouched, output_too_small);
        EXPECT_EQ(output_limit.status, IptcIimSerializeStatus::LimitExceeded);
        EXPECT_EQ(output_limit.failed_entry, 1U);
        EXPECT_EQ(output_limit.written, 0U);
        EXPECT_EQ(output_limit.needed, 0U);
        EXPECT_TRUE(all_bytes_equal(untouched, std::byte { 0xA5U }));

        IptcIimSerializeOptions payload_too_small = exact;
        payload_too_small.max_dataset_bytes       = 1U;
        const IptcIimSerializeResult payload_limit
            = serialize_iptc_iim(store, untouched, payload_too_small);
        EXPECT_EQ(payload_limit.status, IptcIimSerializeStatus::LimitExceeded);
        EXPECT_EQ(payload_limit.failed_entry, 0U);
        EXPECT_TRUE(all_bytes_equal(untouched, std::byte { 0xA5U }));

        IptcIimSerializeOptions invalid_options = exact;
        invalid_options.max_datasets            = 0U;
        EXPECT_EQ(serialize_iptc_iim(store, untouched, invalid_options).status,
                  IptcIimSerializeStatus::InvalidOptions);
        invalid_options                   = exact;
        invalid_options.max_dataset_bytes = 0U;
        EXPECT_EQ(serialize_iptc_iim(store, untouched, invalid_options).status,
                  IptcIimSerializeStatus::InvalidOptions);
        invalid_options                  = exact;
        invalid_options.max_output_bytes = 0U;
        EXPECT_EQ(serialize_iptc_iim(store, untouched, invalid_options).status,
                  IptcIimSerializeStatus::InvalidOptions);
        invalid_options                  = exact;
        invalid_options.max_output_bytes = static_cast<uint64_t>(UINT32_MAX)
                                           + 1U;
        EXPECT_EQ(serialize_iptc_iim(store, untouched, invalid_options).status,
                  IptcIimSerializeStatus::InvalidOptions);
        EXPECT_TRUE(all_bytes_equal(untouched, std::byte { 0xA5U }));

        MetaStore over_count;
        add_text_dataset(over_count, 1U, 5U, 0U, "a");
        add_text_dataset(over_count, 1U, 5U, 1U, "b");
        over_count.finalize();
        IptcIimSerializeOptions count_too_small = exact;
        count_too_small.max_datasets            = 1U;
        const IptcIimSerializeResult count_limit
            = serialize_iptc_iim(over_count, untouched, count_too_small);
        EXPECT_EQ(count_limit.status, IptcIimSerializeStatus::LimitExceeded);
        EXPECT_EQ(count_limit.failed_entry, 1U);
        EXPECT_TRUE(all_bytes_equal(untouched, std::byte { 0xA5U }));
    }


    TEST(IptcIimSerialize, WritesOnlyCanonicalPrefixAndRejectsArenaOverlap)
    {
        MetaStore store;
        add_text_dataset(store, 1U, 5U, 0U, "abcdef");
        store.finalize();

        std::vector<std::byte> expected;
        append_expected_dataset(&expected, 1U, 5U, text_bytes("abcdef"));
        std::array<std::byte, 14> backing;
        backing.fill(std::byte { 0xA5U });
        const std::span<std::byte> short_output(backing.data(), 7U);
        const IptcIimSerializeResult truncated
            = serialize_iptc_iim(store, short_output);
        EXPECT_EQ(truncated.status, IptcIimSerializeStatus::OutputTruncated);
        EXPECT_EQ(truncated.written, 7U);
        EXPECT_EQ(truncated.needed, expected.size());
        EXPECT_TRUE(std::equal(short_output.begin(), short_output.end(),
                               expected.begin()));
        for (size_t index = short_output.size(); index < backing.size();
             ++index) {
            EXPECT_EQ(backing[index], std::byte { 0xA5U });
        }

        backing.fill(std::byte { 0xA5U });
        const IptcIimSerializeResult complete = serialize_iptc_iim(store,
                                                                   backing);
        EXPECT_EQ(complete.status, IptcIimSerializeStatus::Ok);
        EXPECT_EQ(complete.written, expected.size());
        EXPECT_TRUE(
            std::equal(expected.begin(), expected.end(), backing.begin()));
        for (size_t index = expected.size(); index < backing.size(); ++index) {
            EXPECT_EQ(backing[index], std::byte { 0xA5U });
        }

        std::span<std::byte> arena = store.arena().bytes_mut();
        const std::vector<std::byte> arena_before(arena.begin(), arena.end());
        const IptcIimSerializeResult full_alias = serialize_iptc_iim(store,
                                                                     arena);
        EXPECT_EQ(full_alias.status, IptcIimSerializeStatus::InvalidOptions);
        EXPECT_EQ(full_alias.written, 0U);
        EXPECT_EQ(full_alias.needed, 0U);
        EXPECT_TRUE(std::equal(arena.begin(), arena.end(), arena_before.begin(),
                               arena_before.end()));

        const std::span<std::byte> partial_alias = arena.subspan(1U, 3U);
        const IptcIimSerializeResult partial
            = serialize_iptc_iim(store, partial_alias);
        EXPECT_EQ(partial.status, IptcIimSerializeStatus::InvalidOptions);
        EXPECT_TRUE(std::equal(arena.begin(), arena.end(), arena_before.begin(),
                               arena_before.end()));
    }


    TEST(IptcIimSerialize, ReportsLifecycleNoDataAndStableStatusNames)
    {
        MetaStore store;
        std::array<std::byte, 2> output          = { std::byte { 0xA5U },
                                                     std::byte { 0xA5U } };
        const IptcIimSerializeResult unfinalized = serialize_iptc_iim(store,
                                                                      output);
        EXPECT_EQ(unfinalized.status,
                  IptcIimSerializeStatus::StoreNotFinalized);
        EXPECT_EQ(unfinalized.written, 0U);
        EXPECT_EQ(unfinalized.needed, 0U);
        EXPECT_EQ(output[0], std::byte { 0xA5U });
        store.finalize();
        const IptcIimSerializeResult no_data = serialize_iptc_iim(store,
                                                                  output);
        EXPECT_EQ(no_data.status, IptcIimSerializeStatus::NoIptcData);
        EXPECT_EQ(no_data.written, 0U);
        EXPECT_EQ(no_data.needed, 0U);
        EXPECT_EQ(output[0], std::byte { 0xA5U });
        EXPECT_EQ(output[1], std::byte { 0xA5U });

        struct StatusName final {
            IptcIimSerializeStatus status;
            const char* name;
        };
        const std::array names = {
            StatusName { IptcIimSerializeStatus::Ok, "ok" },
            StatusName { IptcIimSerializeStatus::OutputTruncated,
                         "output_truncated" },
            StatusName { IptcIimSerializeStatus::InvalidOptions,
                         "invalid_options" },
            StatusName { IptcIimSerializeStatus::StoreNotFinalized,
                         "store_not_finalized" },
            StatusName { IptcIimSerializeStatus::InvalidMetadata,
                         "invalid_metadata" },
            StatusName { IptcIimSerializeStatus::NoIptcData, "no_iptc_data" },
            StatusName { IptcIimSerializeStatus::LimitExceeded,
                         "limit_exceeded" },
        };
        for (const StatusName& status_name : names) {
            EXPECT_STREQ(iptc_iim_serialize_status_name(status_name.status),
                         status_name.name);
        }
        EXPECT_STREQ(iptc_iim_serialize_status_name(
                         static_cast<IptcIimSerializeStatus>(0xFFU)),
                     "unknown");
    }

}  // namespace
}  // namespace openmeta
