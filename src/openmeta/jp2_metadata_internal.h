// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>

namespace openmeta::detail {
inline constexpr std::array<std::byte, 16> kJp2UuidExif = {
    std::byte { 0x4a }, std::byte { 0x70 }, std::byte { 0x67 },
    std::byte { 0x54 }, std::byte { 0x69 }, std::byte { 0x66 },
    std::byte { 0x66 }, std::byte { 0x45 }, std::byte { 0x78 },
    std::byte { 0x69 }, std::byte { 0x66 }, std::byte { 0x2d },
    std::byte { 0x3e }, std::byte { 0x4a }, std::byte { 0x50 },
    std::byte { 0x32 },
};
inline constexpr std::array<std::byte, 16> kJp2UuidXmp = {
    std::byte { 0xbe }, std::byte { 0x7a }, std::byte { 0xcf },
    std::byte { 0xcb }, std::byte { 0x97 }, std::byte { 0xa9 },
    std::byte { 0x42 }, std::byte { 0xe8 }, std::byte { 0x9c },
    std::byte { 0x71 }, std::byte { 0x99 }, std::byte { 0x94 },
    std::byte { 0x91 }, std::byte { 0xe3 }, std::byte { 0xaf },
    std::byte { 0xac },
};
}  // namespace openmeta::detail
