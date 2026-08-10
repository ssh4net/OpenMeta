// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include <string>
#include <string_view>

/**
 * \file build_info.h
 * \brief Runtime information about how OpenMeta was built.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/**
 * \brief OpenMeta build information.
 *
 * Values are compiled into the binary at build time.
 */
struct BuildInfo final {
    /// OpenMeta version string (e.g. "0.4.0").
    std::string_view version;

    /// Build timestamp in UTC (ISO-8601), or empty if not recorded.
    std::string_view build_timestamp_utc;

    /// Build type string (e.g. "Release", "Debug", "multi-config").
    std::string_view build_type;

    /// CMake generator used to configure the build (e.g. "Ninja").
    std::string_view cmake_generator;

    /// Target platform (e.g. "Linux", "Darwin", "Windows").
    std::string_view system_name;

    /// Target CPU architecture (e.g. "x86_64", "arm64").
    std::string_view system_processor;

    /// Compiler ID (e.g. "Clang", "GNU", "MSVC").
    std::string_view cxx_compiler_id;

    /// Compiler version string.
    std::string_view cxx_compiler_version;

    /// Compiler executable path, if available.
    std::string_view cxx_compiler;

    /// True if this binary was built from the static library target.
    bool linkage_static = false;
    /// True if this binary was built from the shared library target.
    bool linkage_shared = false;

    /// Whether zlib decompression was enabled at configure time.
    bool option_with_zlib = false;
    /// Whether brotli decompression was enabled at configure time.
    bool option_with_brotli = false;
    /// Whether Expat-based XMP parsing was enabled at configure time.
    bool option_with_expat = false;
    /// Whether RapidFuzz-backed metadata query matching was enabled at configure
    /// time.
    bool option_enable_rapidfuzz = false;

    /// Whether zlib support is compiled in (linked).
    bool has_zlib = false;
    /// Whether brotli support is compiled in (linked).
    bool has_brotli = false;
    /// Whether Expat support is compiled in (linked).
    bool has_expat = false;
    /// Whether RapidFuzz-backed metadata query matching is compiled in.
    bool has_rapidfuzz = false;
};

/// Returns build information for the linked OpenMeta library.
const BuildInfo&
build_info() noexcept;

/**
 * \brief Formats a stable, human-readable build info header (2 lines).
 *
 * Output format:
 * - `OpenMeta vX.Y.Z <build_type> [features] <linkage>`
 * - `built with <compiler> for <system>/<arch> (<timestamp>)`
 */
void
format_build_info_lines(const BuildInfo& info, std::string* line1,
                        std::string* line2) noexcept;

/// Convenience overload for the linked OpenMeta library build.
void
format_build_info_lines(std::string* line1, std::string* line2) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
