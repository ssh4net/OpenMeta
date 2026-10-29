// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/meta_value.h"

#include <cstddef>
#include <cstring>
#include <span>

namespace openmeta::python_detail {

template<typename T>
struct ArrayBytes final {
    std::span<const std::byte> bytes;

    size_t size() const noexcept { return bytes.size() / sizeof(T); }
    T operator[](size_t index) const noexcept
    {
        T value;
        std::memcpy(&value, bytes.data() + index * sizeof(T), sizeof(T));
        return value;
    }
};

template<typename T>
ArrayBytes<T> array_span(const ByteArena& arena, const MetaValue& value) noexcept
{
    const std::span<const std::byte> bytes = arena.span(value.data.span);
    if (bytes.size() % sizeof(T) != 0U
        || bytes.size() / sizeof(T) != static_cast<size_t>(value.count)) {
        return {};
    }
    return { bytes };
}

}  // namespace openmeta::python_detail
