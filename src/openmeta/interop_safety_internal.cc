// SPDX-License-Identifier: Apache-2.0

#include "interop_safety_internal.h"

#include <cstdio>
#include <cstring>

namespace openmeta::interop_internal {
namespace {

    static bool is_unsafe_control_codepoint(uint32_t cp) noexcept
    {
        if (cp <= 0x1FU || cp == 0x7FU) {
            return true;
        }
        if (cp >= 0x80U && cp <= 0x9FU) {
            return true;
        }
        return false;
    }

    static void append_utf8_codepoint(uint32_t cp, std::string* out) noexcept
    {
        if (!out) {
            return;
        }
        if (cp <= 0x7FU) {
            out->push_back(static_cast<char>(cp));
            return;
        }
        if (cp <= 0x7FFU) {
            out->push_back(static_cast<char>(0xC0U | ((cp >> 6) & 0x1FU)));
            out->push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            return;
        }
        if (cp <= 0xFFFFU) {
            out->push_back(static_cast<char>(0xE0U | ((cp >> 12) & 0x0FU)));
            out->push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
            out->push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            return;
        }
        out->push_back(static_cast<char>(0xF0U | ((cp >> 18) & 0x07U)));
        out->push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3FU)));
        out->push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
        out->push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }

    static bool fail_control(InteropSafetyError* error,
                             std::string_view field_name,
                             std::string_view key_path, uint32_t cp) noexcept
    {
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "unsafe control character U+%04X in text value",
                      static_cast<unsigned>(cp));
        set_safety_error(error, InteropSafetyReason::UnsafeTextControlCharacter,
                         field_name, key_path, msg);
        return false;
    }

    static SafeTextStatus decode_ascii(std::span<const std::byte> bytes,
                                       std::string_view field_name,
                                       std::string_view key_path,
                                       std::string* out,
                                       InteropSafetyError* error) noexcept
    {
        if (!out) {
            return SafeTextStatus::Error;
        }
        out->clear();
        if (bytes.empty()) {
            return SafeTextStatus::Empty;
        }
        out->reserve(bytes.size());
        for (size_t i = 0; i < bytes.size(); ++i) {
            const uint8_t b = static_cast<uint8_t>(bytes[i]);
            if (b >= 0x80U) {
                set_safety_error(error,
                                 InteropSafetyReason::InvalidTextEncoding,
                                 field_name, key_path,
                                 "non-ASCII byte in ASCII text value");
                return SafeTextStatus::Error;
            }
            if (is_unsafe_control_codepoint(static_cast<uint32_t>(b))) {
                if (!fail_control(error, field_name, key_path, b)) {
                    return SafeTextStatus::Error;
                }
            }
            out->push_back(static_cast<char>(b));
        }
        return SafeTextStatus::Ok;
    }

    static SafeTextStatus decode_utf8(std::span<const std::byte> bytes,
                                      std::string_view field_name,
                                      std::string_view key_path,
                                      std::string* out,
                                      InteropSafetyError* error) noexcept
    {
        if (!out) {
            return SafeTextStatus::Error;
        }
        out->clear();
        if (bytes.empty()) {
            return SafeTextStatus::Empty;
        }

        out->reserve(bytes.size());
        size_t i = 0U;
        while (i < bytes.size()) {
            const uint8_t b0 = static_cast<uint8_t>(bytes[i]);
            uint32_t cp      = 0U;
            size_t len       = 0U;

            if (b0 <= 0x7FU) {
                cp  = b0;
                len = 1U;
            } else if (b0 >= 0xC2U && b0 <= 0xDFU) {
                cp  = static_cast<uint32_t>(b0 & 0x1FU);
                len = 2U;
            } else if (b0 >= 0xE0U && b0 <= 0xEFU) {
                cp  = static_cast<uint32_t>(b0 & 0x0FU);
                len = 3U;
            } else if (b0 >= 0xF0U && b0 <= 0xF4U) {
                cp  = static_cast<uint32_t>(b0 & 0x07U);
                len = 4U;
            } else {
                set_safety_error(error,
                                 InteropSafetyReason::InvalidTextEncoding,
                                 field_name, key_path,
                                 "invalid UTF-8 lead byte");
                return SafeTextStatus::Error;
            }

            if (i + len > bytes.size()) {
                set_safety_error(error,
                                 InteropSafetyReason::InvalidTextEncoding,
                                 field_name, key_path,
                                 "truncated UTF-8 sequence");
                return SafeTextStatus::Error;
            }

            if (len > 1U) {
                const uint8_t b1 = static_cast<uint8_t>(bytes[i + 1U]);
                if ((b1 & 0xC0U) != 0x80U) {
                    set_safety_error(error,
                                     InteropSafetyReason::InvalidTextEncoding,
                                     field_name, key_path,
                                     "invalid UTF-8 continuation byte");
                    return SafeTextStatus::Error;
                }
                if ((b0 == 0xE0U && b1 < 0xA0U) || (b0 == 0xEDU && b1 >= 0xA0U)
                    || (b0 == 0xF0U && b1 < 0x90U)
                    || (b0 == 0xF4U && b1 >= 0x90U)) {
                    set_safety_error(error,
                                     InteropSafetyReason::InvalidTextEncoding,
                                     field_name, key_path,
                                     "invalid UTF-8 codepoint range");
                    return SafeTextStatus::Error;
                }
            }

            for (size_t j = 1U; j < len; ++j) {
                const uint8_t bj = static_cast<uint8_t>(bytes[i + j]);
                if ((bj & 0xC0U) != 0x80U) {
                    set_safety_error(error,
                                     InteropSafetyReason::InvalidTextEncoding,
                                     field_name, key_path,
                                     "invalid UTF-8 continuation byte");
                    return SafeTextStatus::Error;
                }
                cp = (cp << 6U) | static_cast<uint32_t>(bj & 0x3FU);
            }

            if (cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) {
                set_safety_error(error,
                                 InteropSafetyReason::InvalidTextEncoding,
                                 field_name, key_path,
                                 "invalid UTF-8 codepoint");
                return SafeTextStatus::Error;
            }
            if (is_unsafe_control_codepoint(cp)) {
                if (!fail_control(error, field_name, key_path, cp)) {
                    return SafeTextStatus::Error;
                }
            }

            out->append(reinterpret_cast<const char*>(bytes.data() + i), len);
            i += len;
        }
        return SafeTextStatus::Ok;
    }

    static SafeTextStatus
    decode_utf16(std::span<const std::byte> bytes, bool little_endian,
                 std::string_view field_name, std::string_view key_path,
                 std::string* out, InteropSafetyError* error) noexcept
    {
        if (!out) {
            return SafeTextStatus::Error;
        }
        out->clear();
        if (bytes.empty()) {
            return SafeTextStatus::Empty;
        }
        if ((bytes.size() % 2U) != 0U) {
            set_safety_error(error, InteropSafetyReason::InvalidTextEncoding,
                             field_name, key_path,
                             "odd-sized UTF-16 text value");
            return SafeTextStatus::Error;
        }

        out->reserve(bytes.size());
        size_t i = 0U;
        while (i + 1U < bytes.size()) {
            const uint16_t u0
                = little_endian
                      ? static_cast<uint16_t>(
                            static_cast<uint8_t>(bytes[i])
                            | (static_cast<uint16_t>(
                                   static_cast<uint8_t>(bytes[i + 1U]))
                               << 8U))
                      : static_cast<uint16_t>(
                            (static_cast<uint16_t>(static_cast<uint8_t>(bytes[i]))
                             << 8U)
                            | static_cast<uint8_t>(bytes[i + 1U]));
            i += 2U;

            uint32_t cp = 0U;
            if (u0 >= 0xD800U && u0 <= 0xDBFFU) {
                if (i + 1U >= bytes.size()) {
                    set_safety_error(error,
                                     InteropSafetyReason::InvalidTextEncoding,
                                     field_name, key_path,
                                     "truncated UTF-16 surrogate pair");
                    return SafeTextStatus::Error;
                }
                const uint16_t u1
                    = little_endian
                          ? static_cast<uint16_t>(
                                static_cast<uint8_t>(bytes[i])
                                | (static_cast<uint16_t>(
                                       static_cast<uint8_t>(bytes[i + 1U]))
                                   << 8U))
                          : static_cast<uint16_t>(
                                (static_cast<uint16_t>(
                                     static_cast<uint8_t>(bytes[i]))
                                 << 8U)
                                | static_cast<uint8_t>(bytes[i + 1U]));
                i += 2U;
                if (u1 < 0xDC00U || u1 > 0xDFFFU) {
                    set_safety_error(error,
                                     InteropSafetyReason::InvalidTextEncoding,
                                     field_name, key_path,
                                     "invalid UTF-16 surrogate pair");
                    return SafeTextStatus::Error;
                }
                cp = 0x10000U
                     + (((static_cast<uint32_t>(u0) - 0xD800U) << 10U)
                        | (static_cast<uint32_t>(u1) - 0xDC00U));
            } else if (u0 >= 0xDC00U && u0 <= 0xDFFFU) {
                set_safety_error(error,
                                 InteropSafetyReason::InvalidTextEncoding,
                                 field_name, key_path,
                                 "unexpected UTF-16 low surrogate");
                return SafeTextStatus::Error;
            } else {
                cp = static_cast<uint32_t>(u0);
            }

            if (is_unsafe_control_codepoint(cp)) {
                if (!fail_control(error, field_name, key_path, cp)) {
                    return SafeTextStatus::Error;
                }
            }
            append_utf8_codepoint(cp, out);
        }
        return SafeTextStatus::Ok;
    }

}  // namespace

void
set_safety_error(InteropSafetyError* error, InteropSafetyReason reason,
                 std::string_view field_name, std::string_view key_path,
                 std::string_view message) noexcept
{
    if (!error) {
        return;
    }
    error->reason = reason;
    error->field_name.assign(field_name.data(), field_name.size());
    error->key_path.assign(key_path.data(), key_path.size());
    error->message.assign(message.data(), message.size());
}

SafeTextStatus
decode_text_to_utf8_safe(std::span<const std::byte> bytes,
                         TextEncoding encoding, std::string_view field_name,
                         std::string_view key_path, std::string* out,
                         InteropSafetyError* error) noexcept
{
    if (!out) {
        set_safety_error(error, InteropSafetyReason::InternalMismatch,
                         field_name, key_path, "null output buffer");
        return SafeTextStatus::Error;
    }
    switch (encoding) {
    case TextEncoding::Ascii:
        return decode_ascii(bytes, field_name, key_path, out, error);
    case TextEncoding::Utf8:
    case TextEncoding::Unknown:
        return decode_utf8(bytes, field_name, key_path, out, error);
    case TextEncoding::Utf16LE:
        return decode_utf16(bytes, true, field_name, key_path, out, error);
    case TextEncoding::Utf16BE:
        return decode_utf16(bytes, false, field_name, key_path, out, error);
    }
    set_safety_error(error, InteropSafetyReason::InvalidTextEncoding,
                     field_name, key_path, "unsupported text encoding");
    return SafeTextStatus::Error;
}

SafeTextStatus
decode_exif_prefixed_text_safe(std::span<const std::byte> bytes,
                               std::string* out) noexcept
{
    if (!out) {
        return SafeTextStatus::Error;
    }
    out->clear();
    if (bytes.size() < 8U) {
        return SafeTextStatus::Error;
    }
    TextEncoding encoding;
    std::span<const std::byte> payload = bytes.subspan(8U);
    size_t unit                        = 1U;
    if (std::memcmp(bytes.data(), "ASCII\0\0\0", 8U) == 0) {
        encoding = TextEncoding::Ascii;
    } else if (std::memcmp(bytes.data(), "UNICODE\0", 8U) == 0) {
        if (payload.size() < 2U || payload.size() % 2U != 0U) {
            return SafeTextStatus::Error;
        }
        if (payload[0] == std::byte { 0xff }
            && payload[1] == std::byte { 0xfe }) {
            encoding = TextEncoding::Utf16LE;
        } else if (payload[0] == std::byte { 0xfe }
                   && payload[1] == std::byte { 0xff }) {
            encoding = TextEncoding::Utf16BE;
        } else {
            return SafeTextStatus::Error;
        }
        payload = payload.subspan(2U);
        unit    = 2U;
    } else {
        return SafeTextStatus::Error;
    }
    if (payload.size() >= unit && payload.back() == std::byte { 0 }
        && (unit == 1U || payload[payload.size() - 2U] == std::byte { 0 })) {
        payload = payload.first(payload.size() - unit);
    }
    return decode_text_to_utf8_safe(payload, encoding, "GPS text", "Exif:GPS",
                                    out, nullptr);
}

void
encode_exif_prefixed_text_from_valid_utf8(std::string_view text,
                                          std::string* out)
{
    bool ascii = true;
    for (const unsigned char c : text) {
        ascii = ascii && c < 0x80U;
    }
    if (ascii) {
        out->assign("ASCII\0\0\0", 8U);
        out->append(text);
        return;
    }
    out->assign("UNICODE\0\xff\xfe", 10U);
    out->reserve(10U + 2U * text.size());
    for (size_t i = 0U; i < text.size();) {
        const uint8_t first  = static_cast<uint8_t>(text[i++]);
        const uint32_t count = first < 0x80U   ? 0U
                               : first < 0xe0U ? 1U
                               : first < 0xf0U ? 2U
                                               : 3U;
        uint32_t cp          = first
                      & (count == 0U   ? 0x7fU
                         : count == 1U ? 0x1fU
                         : count == 2U ? 0x0fU
                                       : 0x07U);
        for (uint32_t j = 0U; j < count; ++j) {
            cp = (cp << 6U) | (static_cast<uint8_t>(text[i++]) & 0x3fU);
        }
        if (cp > 0xffffU) {
            cp -= 0x10000U;
            const uint32_t high = 0xd800U + (cp >> 10U);
            out->push_back(static_cast<char>(high & 0xffU));
            out->push_back(static_cast<char>(high >> 8U));
            cp = 0xdc00U + (cp & 0x3ffU);
        }
        out->push_back(static_cast<char>(cp & 0xffU));
        out->push_back(static_cast<char>(cp >> 8U));
    }
}

std::string
format_safety_error_message(const InteropSafetyError& error)
{
    std::string out;
    if (!error.message.empty()) {
        out = error.message;
    } else {
        out = "unsafe metadata value";
    }
    if (!error.field_name.empty()) {
        out.append(" [field=");
        out.append(error.field_name);
        out.push_back(']');
    }
    if (!error.key_path.empty()) {
        out.append(" [key=");
        out.append(error.key_path);
        out.push_back(']');
    }
    return out;
}

}  // namespace openmeta::interop_internal
