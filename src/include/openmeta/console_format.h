// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

// Appends an ASCII-only, terminal-safe representation of `s` into `out`.
//
// Behavior:
// - Escapes control bytes and non-ASCII as `\xNN`
// - Escapes `\n`, `\r`, `\t`
// - Truncates to `max_bytes` bytes (0 = unlimited) and appends "..."
//
// Returns true when any escaping or truncation occurred.
bool
append_console_escaped_ascii(std::string_view s, uint32_t max_bytes,
                             std::string* out) noexcept;

// Appends uppercase hex bytes into `out` (no "0x" prefix).
// Truncates to `max_bytes` (0 = unlimited) and appends "..." when truncated.
void
append_hex_bytes(std::span<const std::byte> bytes, uint32_t max_bytes,
                 std::string* out) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
