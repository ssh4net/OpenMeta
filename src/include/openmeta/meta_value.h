// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/byte_arena.h"

#include <cstdint>
#include <span>
#include <string_view>

/**
 * \file meta_value.h
 * \brief Typed metadata value representation (scalar/array/bytes/text).
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Unsigned rational (numerator/denominator), typically used by EXIF/TIFF.
struct URational final {
    uint32_t numer = 0;
    uint32_t denom = 1;
};

/// Signed rational (numerator/denominator), typically used by EXIF/TIFF.
struct SRational final {
    int32_t numer = 0;
    int32_t denom = 1;
};

/// Top-level value storage kind.
enum class MetaValueKind : uint8_t {
    Empty,
    /// An inline scalar stored in \ref MetaValue::data.
    Scalar,
    /// An array stored as raw bytes in a \ref ByteArena span.
    Array,
    /// Raw uninterpreted bytes in a \ref ByteArena span.
    Bytes,
    /// Text bytes in a \ref ByteArena span with an associated encoding.
    Text,
};

/// Element type used for scalar and array values.
enum class MetaElementType : uint8_t {
    U8,
    I8,
    U16,
    I16,
    U32,
    I32,
    U64,
    I64,
    F32,
    F64,
    URational,
    SRational,
};

/// Encoding hint for text values.
enum class TextEncoding : uint8_t {
    Unknown,
    Ascii,
    Utf8,
    Utf16LE,
    Utf16BE,
};

/**
 * \brief A typed metadata value.
 *
 * Storage rules:
 * - Scalar values are stored inline in `MetaValue::data`.
 * - Array/Bytes/Text values store their payload in `MetaValue::data.span`
 *   (a `ByteSpan` into a `ByteArena`). Text payload is not nul-terminated.
 *
 * The \ref count field is:
 * - 1 for scalars
 * - number of elements for arrays
 * - number of bytes for bytes/text
 */
struct MetaValue final {
    MetaValueKind kind         = MetaValueKind::Empty;
    MetaElementType elem_type  = MetaElementType::U8;
    TextEncoding text_encoding = TextEncoding::Unknown;
    uint32_t count             = 0;

    union Data {
        uint64_t u64;
        int64_t i64;
        /// Raw IEEE-754 bits for \ref MetaElementType::F32.
        uint32_t f32_bits;
        /// Raw IEEE-754 bits for \ref MetaElementType::F64.
        uint64_t f64_bits;
        URational ur;
        SRational sr;
        ByteSpan span;

        Data() noexcept
            : u64(0)
        {
        }
    } data;
};

/** \name Scalar constructors
 *  @{
 */
MetaValue
make_u8(uint8_t value) noexcept;
MetaValue
make_i8(int8_t value) noexcept;
MetaValue
make_u16(uint16_t value) noexcept;
MetaValue
make_i16(int16_t value) noexcept;
MetaValue
make_u32(uint32_t value) noexcept;
MetaValue
make_i32(int32_t value) noexcept;
MetaValue
make_u64(uint64_t value) noexcept;
MetaValue
make_i64(int64_t value) noexcept;
MetaValue
make_f32_bits(uint32_t bits) noexcept;
MetaValue
make_f64_bits(uint64_t bits) noexcept;
MetaValue
make_urational(uint32_t numer, uint32_t denom) noexcept;
MetaValue
make_srational(int32_t numer, int32_t denom) noexcept;
/** @} */

/** \name Arena-backed constructors
 *  @{
 */
MetaValue
make_bytes(ByteArena& arena, std::span<const std::byte> bytes);
MetaValue
make_text(ByteArena& arena, std::string_view text, TextEncoding encoding);

MetaValue
make_array(ByteArena& arena, MetaElementType elem_type,
           std::span<const std::byte> raw_elements, uint32_t element_size);
/** @} */

/** \name Convenience array constructors
 *  @{
 */
MetaValue
make_u8_array(ByteArena& arena, std::span<const uint8_t> values);
MetaValue
make_i8_array(ByteArena& arena, std::span<const int8_t> values);
MetaValue
make_u16_array(ByteArena& arena, std::span<const uint16_t> values);
MetaValue
make_i16_array(ByteArena& arena, std::span<const int16_t> values);
MetaValue
make_u32_array(ByteArena& arena, std::span<const uint32_t> values);
MetaValue
make_i32_array(ByteArena& arena, std::span<const int32_t> values);
MetaValue
make_u64_array(ByteArena& arena, std::span<const uint64_t> values);
MetaValue
make_i64_array(ByteArena& arena, std::span<const int64_t> values);
MetaValue
make_f32_bits_array(ByteArena& arena, std::span<const uint32_t> bits);
MetaValue
make_f64_bits_array(ByteArena& arena, std::span<const uint64_t> bits);
MetaValue
make_urational_array(ByteArena& arena, std::span<const URational> values);
MetaValue
make_srational_array(ByteArena& arena, std::span<const SRational> values);
/** @} */

}  // namespace openmeta
OPENMETA_PUBLIC_END
