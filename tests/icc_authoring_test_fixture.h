// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/metadata_authoring.h"

#include <array>
#include <span>

namespace openmeta::test {

struct IccAuthoringFixtureData final {
    std::array<uint16_t, 6> date { 2026U, 11U, 2U, 3U, 4U, 5U };
    std::array<SRational, 3> illuminant { SRational { 63190, 65536 },
                                          SRational { 65536, 65536 },
                                          SRational { 54061, 65536 } };
    std::array<std::byte, 16> profile_id {
        std::byte { 1 },  std::byte { 2 },  std::byte { 3 },  std::byte { 4 },
        std::byte { 5 },  std::byte { 6 },  std::byte { 7 },  std::byte { 8 },
        std::byte { 9 },  std::byte { 10 }, std::byte { 11 }, std::byte { 12 },
        std::byte { 13 }, std::byte { 14 }, std::byte { 15 }, std::byte { 16 }
    };
    std::array<std::byte, 13> first_tag {
        std::byte { 'd' },  std::byte { 'a' },  std::byte { 't' },
        std::byte { 'a' },  std::byte { 0 },    std::byte { 0 },
        std::byte { 0 },    std::byte { 0 },    std::byte { 0x10 },
        std::byte { 0x20 }, std::byte { 0x30 }, std::byte { 0x40 },
        std::byte { 0x50 }
    };
    std::array<std::byte, 12> second_tag { std::byte { 't' }, std::byte { 'e' },
                                           std::byte { 'x' }, std::byte { 't' },
                                           std::byte { 0 },   std::byte { 0 },
                                           std::byte { 0 },   std::byte { 0 },
                                           std::byte { 'o' }, std::byte { 'l' },
                                           std::byte { 'd' }, std::byte { 0 } };
};

// Returned views borrow data; keep data alive until construction has copied it.
static std::array<MetadataAuthoringEntry, 10>
icc_authoring_fixture_entries(const IccAuthoringFixtureData& data)
{
    return {
        MetadataAuthoringEntry { make_icc_header_field_key_view(8U),
                                 make_value_view_u32(0x04400000U) },
        MetadataAuthoringEntry { make_icc_header_field_key_view(12U),
                                 make_value_view_u32(0x6D6E7472U) },
        MetadataAuthoringEntry { make_icc_header_field_key_view(16U),
                                 make_value_view_u32(0x52474220U) },
        MetadataAuthoringEntry { make_icc_header_field_key_view(20U),
                                 make_value_view_u32(0x58595A20U) },
        MetadataAuthoringEntry {
            make_icc_header_field_key_view(24U),
            make_value_view_array(MetaElementType::U16,
                                  std::as_bytes(std::span(data.date)), 6U) },
        MetadataAuthoringEntry {
            make_icc_header_field_key_view(68U),
            make_value_view_array(MetaElementType::SRational,
                                  std::as_bytes(std::span(data.illuminant)),
                                  3U) },
        MetadataAuthoringEntry { make_icc_header_field_key_view(80U),
                                 make_value_view_u32(0x4F4D5441U) },
        MetadataAuthoringEntry { make_icc_header_field_key_view(84U),
                                 make_value_view_bytes(data.profile_id) },
        MetadataAuthoringEntry { make_icc_tag_key_view(0x7A7A7A31U),
                                 make_value_view_bytes(data.first_tag) },
        MetadataAuthoringEntry { make_icc_tag_key_view(0x61616131U),
                                 make_value_view_bytes(data.second_tag) }
    };
}

}  // namespace openmeta::test
