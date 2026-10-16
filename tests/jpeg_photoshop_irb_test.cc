// SPDX-License-Identifier: Apache-2.0

#include "../src/openmeta/jpeg_photoshop_irb.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace openmeta::detail {
namespace {

    static std::vector<std::byte> make_header_only_payload()
    {
        return { std::byte { 'P' }, std::byte { 'h' }, std::byte { 'o' },
                 std::byte { 't' }, std::byte { 'o' }, std::byte { 's' },
                 std::byte { 'h' }, std::byte { 'o' }, std::byte { 'p' },
                 std::byte { ' ' }, std::byte { '3' }, std::byte { '.' },
                 std::byte { '0' }, std::byte { 0U } };
    }


    static std::vector<std::byte> make_minimal_other_payload()
    {
        std::vector<std::byte> payload = make_header_only_payload();
        payload.insert(payload.end(),
                       { std::byte { '8' }, std::byte { 'B' },
                         std::byte { 'I' }, std::byte { 'M' },
                         std::byte { 0x12U }, std::byte { 0x34U },
                         std::byte { 0U }, std::byte { 0U }, std::byte { 0U },
                         std::byte { 0U }, std::byte { 0U },
                         std::byte { 0U } });
        return payload;
    }


    static void
    expect_invalid_without_publishing(const std::vector<std::byte>& payload)
    {
        JpegPhotoshopIptcInfo info { 11U, 22U, 33U };
        EXPECT_FALSE(inspect_jpeg_photoshop_iptc(payload, &info));
        EXPECT_EQ(info.iptc_resources, 11U);
        EXPECT_EQ(info.other_resources, 22U);
        EXPECT_EQ(info.retained_payload_size, 33U);
    }

    TEST(JpegPhotoshopIrb, PreservesNonIptcRecordsByteForByte)
    {
        constexpr std::array<std::byte, 14U> header
            = { std::byte { 'P' }, std::byte { 'h' }, std::byte { 'o' },
                std::byte { 't' }, std::byte { 'o' }, std::byte { 's' },
                std::byte { 'h' }, std::byte { 'o' }, std::byte { 'p' },
                std::byte { ' ' }, std::byte { '3' }, std::byte { '.' },
                std::byte { '0' }, std::byte { 0U } };
        constexpr std::array<std::byte, 16U> other_odd_name_odd_data
            = { std::byte { '8' },  std::byte { 'B' },   std::byte { 'I' },
                std::byte { 'M' },  std::byte { 0x03U }, std::byte { 0xE8U },
                std::byte { 1U },   std::byte { 'x' },   std::byte { 0U },
                std::byte { 0U },   std::byte { 0U },    std::byte { 3U },
                std::byte { 'A' },  std::byte { 'B' },   std::byte { 'C' },
                std::byte { 0xD1U } };
        constexpr std::array<std::byte, 14U> iptc_first
            = { std::byte { '8' }, std::byte { 'B' },   std::byte { 'I' },
                std::byte { 'M' }, std::byte { 0x04U }, std::byte { 0x04U },
                std::byte { 0U },  std::byte { 0xA1U }, std::byte { 0U },
                std::byte { 0U },  std::byte { 0U },    std::byte { 2U },
                std::byte { '1' }, std::byte { '2' } };
        constexpr std::array<std::byte, 18U> other_even_name_even_data
            = { std::byte { '8' }, std::byte { 'B' },   std::byte { 'I' },
                std::byte { 'M' }, std::byte { 0x04U }, std::byte { 0x05U },
                std::byte { 4U },  std::byte { 'e' },   std::byte { 'v' },
                std::byte { 'e' }, std::byte { 'n' },   std::byte { 0xB2U },
                std::byte { 0U },  std::byte { 0U },    std::byte { 0U },
                std::byte { 2U },  std::byte { 'X' },   std::byte { 'Y' } };
        constexpr std::array<std::byte, 12U> iptc_duplicate_zero_data
            = { std::byte { '8' }, std::byte { 'B' },   std::byte { 'I' },
                std::byte { 'M' }, std::byte { 0x04U }, std::byte { 0x04U },
                std::byte { 1U },  std::byte { 'z' },   std::byte { 0U },
                std::byte { 0U },  std::byte { 0U },    std::byte { 0U } };
        constexpr std::array<std::byte, 16U> other_padded_name_padded_data
            = { std::byte { '8' },   std::byte { 'B' },   std::byte { 'I' },
                std::byte { 'M' },   std::byte { 0x04U }, std::byte { 0x06U },
                std::byte { 2U },    std::byte { 'q' },   std::byte { 'R' },
                std::byte { 0xC3U }, std::byte { 0U },    std::byte { 0U },
                std::byte { 0U },    std::byte { 1U },    std::byte { 'Z' },
                std::byte { 0xD3U } };

        std::vector<std::byte> payload;
        payload.insert(payload.end(), header.begin(), header.end());
        payload.insert(payload.end(), other_odd_name_odd_data.begin(),
                       other_odd_name_odd_data.end());
        payload.insert(payload.end(), iptc_first.begin(), iptc_first.end());
        payload.insert(payload.end(), other_even_name_even_data.begin(),
                       other_even_name_even_data.end());
        payload.insert(payload.end(), iptc_duplicate_zero_data.begin(),
                       iptc_duplicate_zero_data.end());
        payload.insert(payload.end(), other_padded_name_padded_data.begin(),
                       other_padded_name_padded_data.end());

        JpegPhotoshopIptcInfo info { 99U, 99U, 99U };
        ASSERT_TRUE(inspect_jpeg_photoshop_iptc(payload, &info));
        EXPECT_EQ(info.iptc_resources, 2U);
        EXPECT_EQ(info.other_resources, 3U);
        EXPECT_EQ(info.retained_payload_size, 64U);

        std::vector<std::byte> expected;
        expected.insert(expected.end(), header.begin(), header.end());
        expected.insert(expected.end(), other_odd_name_odd_data.begin(),
                        other_odd_name_odd_data.end());
        expected.insert(expected.end(), other_even_name_even_data.begin(),
                        other_even_name_even_data.end());
        expected.insert(expected.end(), other_padded_name_padded_data.begin(),
                        other_padded_name_padded_data.end());
        std::vector<std::byte> output(info.retained_payload_size);
        copy_jpeg_photoshop_without_iptc(payload, output);
        EXPECT_EQ(output, expected);
    }


    TEST(JpegPhotoshopIrb, AcceptsHeaderOnlyAndAllIptcPayloads)
    {
        std::vector<std::byte> payload = make_header_only_payload();
        JpegPhotoshopIptcInfo info { 99U, 99U, 99U };
        ASSERT_TRUE(inspect_jpeg_photoshop_iptc(payload, &info));
        EXPECT_EQ(info.iptc_resources, 0U);
        EXPECT_EQ(info.other_resources, 0U);
        EXPECT_EQ(info.retained_payload_size, 14U);

        std::vector<std::byte> output(info.retained_payload_size);
        copy_jpeg_photoshop_without_iptc(payload, output);
        EXPECT_EQ(output, payload);

        payload.insert(payload.end(),
                       { std::byte { '8' }, std::byte { 'B' },
                         std::byte { 'I' }, std::byte { 'M' },
                         std::byte { 0x04U }, std::byte { 0x04U },
                         std::byte { 0U }, std::byte { 0U }, std::byte { 0U },
                         std::byte { 0U }, std::byte { 0U },
                         std::byte { 0U } });
        payload.insert(payload.end(),
                       { std::byte { '8' }, std::byte { 'B' },
                         std::byte { 'I' }, std::byte { 'M' },
                         std::byte { 0x04U }, std::byte { 0x04U },
                         std::byte { 0U }, std::byte { 0U }, std::byte { 0U },
                         std::byte { 0U }, std::byte { 0U },
                         std::byte { 0U } });
        ASSERT_TRUE(inspect_jpeg_photoshop_iptc(payload, &info));
        EXPECT_EQ(info.iptc_resources, 2U);
        EXPECT_EQ(info.other_resources, 0U);
        EXPECT_EQ(info.retained_payload_size, 14U);
        output.resize(info.retained_payload_size);
        copy_jpeg_photoshop_without_iptc(payload, output);
        EXPECT_EQ(output, make_header_only_payload());
    }


    TEST(JpegPhotoshopIrb, RejectsMalformedPayloadsWithoutPublishingInfo)
    {
        std::vector<std::vector<std::byte>> invalid_payloads;

        std::vector<std::byte> short_header = make_header_only_payload();
        short_header.resize(13U);
        invalid_payloads.push_back(std::move(short_header));

        std::vector<std::byte> bad_header = make_header_only_payload();
        bad_header[0U]                    = std::byte { 'X' };
        invalid_payloads.push_back(std::move(bad_header));

        std::vector<std::byte> missing_header_nul = make_header_only_payload();
        missing_header_nul[13U]                   = std::byte { '!' };
        invalid_payloads.push_back(std::move(missing_header_nul));

        std::vector<std::byte> truncated_signature = make_header_only_payload();
        truncated_signature.insert(truncated_signature.end(),
                                   { std::byte { '8' }, std::byte { 'B' },
                                     std::byte { 'I' } });
        invalid_payloads.push_back(std::move(truncated_signature));

        std::vector<std::byte> bad_signature = make_minimal_other_payload();
        bad_signature[17U]                   = std::byte { 'X' };
        invalid_payloads.push_back(std::move(bad_signature));

        std::vector<std::byte> truncated_id = make_header_only_payload();
        truncated_id.insert(truncated_id.end(),
                            { std::byte { '8' }, std::byte { 'B' },
                              std::byte { 'I' }, std::byte { 'M' },
                              std::byte { 0x12U } });
        invalid_payloads.push_back(std::move(truncated_id));

        std::vector<std::byte> missing_name_length = make_header_only_payload();
        missing_name_length.insert(missing_name_length.end(),
                                   { std::byte { '8' }, std::byte { 'B' },
                                     std::byte { 'I' }, std::byte { 'M' },
                                     std::byte { 0U }, std::byte { 1U } });
        invalid_payloads.push_back(std::move(missing_name_length));

        std::vector<std::byte> missing_name_padding = make_header_only_payload();
        missing_name_padding.insert(missing_name_padding.end(),
                                    { std::byte { '8' }, std::byte { 'B' },
                                      std::byte { 'I' }, std::byte { 'M' },
                                      std::byte { 0U }, std::byte { 1U },
                                      std::byte { 2U }, std::byte { 'a' },
                                      std::byte { 'b' } });
        invalid_payloads.push_back(std::move(missing_name_padding));

        std::vector<std::byte> missing_data_length = make_header_only_payload();
        missing_data_length.insert(missing_data_length.end(),
                                   { std::byte { '8' }, std::byte { 'B' },
                                     std::byte { 'I' }, std::byte { 'M' },
                                     std::byte { 0U }, std::byte { 1U },
                                     std::byte { 0U }, std::byte { 0U } });
        invalid_payloads.push_back(std::move(missing_data_length));

        std::vector<std::byte> truncated_data_length
            = make_header_only_payload();
        truncated_data_length.insert(truncated_data_length.end(),
                                     { std::byte { '8' }, std::byte { 'B' },
                                       std::byte { 'I' }, std::byte { 'M' },
                                       std::byte { 0U }, std::byte { 1U },
                                       std::byte { 0U }, std::byte { 0U },
                                       std::byte { 0U }, std::byte { 0U },
                                       std::byte { 0U } });
        invalid_payloads.push_back(std::move(truncated_data_length));

        std::vector<std::byte> truncated_data = make_minimal_other_payload();
        truncated_data.back()                 = std::byte { 1U };
        invalid_payloads.push_back(std::move(truncated_data));

        std::vector<std::byte> missing_data_padding
            = make_minimal_other_payload();
        missing_data_padding.back() = std::byte { 1U };
        missing_data_padding.push_back(std::byte { 'D' });
        invalid_payloads.push_back(std::move(missing_data_padding));

        std::vector<std::byte> trailing_garbage = make_header_only_payload();
        trailing_garbage.push_back(std::byte { 0U });
        invalid_payloads.push_back(std::move(trailing_garbage));

        for (size_t index = 0U; index < invalid_payloads.size(); ++index) {
            SCOPED_TRACE(index);
            expect_invalid_without_publishing(invalid_payloads[index]);
        }
    }


    TEST(JpegPhotoshopIrb, EnforcesPayloadCeilingWithEvenRecordSizes)
    {
        std::vector<std::byte> payload = make_header_only_payload();
        payload.insert(payload.end(),
                       { std::byte { '8' }, std::byte { 'B' },
                         std::byte { 'I' }, std::byte { 'M' },
                         std::byte { 0x12U }, std::byte { 0x34U },
                         std::byte { 0U }, std::byte { 0U }, std::byte { 0U },
                         std::byte { 0U }, std::byte { 0xFFU },
                         std::byte { 0xE2U } });
        payload.insert(payload.end(), 65506U, std::byte { 0x5AU });
        ASSERT_EQ(payload.size(), 65532U);

        JpegPhotoshopIptcInfo info { 99U, 99U, 99U };
        ASSERT_TRUE(inspect_jpeg_photoshop_iptc(payload, &info));
        EXPECT_EQ(info.iptc_resources, 0U);
        EXPECT_EQ(info.other_resources, 1U);
        EXPECT_EQ(info.retained_payload_size, 65532U);

        std::vector<std::byte> odd_ceiling = payload;
        odd_ceiling.push_back(std::byte { 0U });
        ASSERT_EQ(odd_ceiling.size(), 65533U);
        expect_invalid_without_publishing(odd_ceiling);

        std::vector<std::byte> over_ceiling = odd_ceiling;
        over_ceiling.push_back(std::byte { 0U });
        ASSERT_EQ(over_ceiling.size(), 65534U);
        expect_invalid_without_publishing(over_ceiling);
    }

}  // namespace
}  // namespace openmeta::detail
