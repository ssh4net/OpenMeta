// SPDX-License-Identifier: Apache-2.0

#include "openmeta/libraw_adapter.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace openmeta {
namespace {

static bool write_bytes(const char* path,
                        std::span<const std::byte> bytes) noexcept
{
    if (!path) {
        return false;
    }
    std::FILE* f = std::fopen(path, "wb");
    if (!f) {
        return false;
    }
    const bool ok = bytes.empty()
                    || std::fwrite(bytes.data(), 1U, bytes.size(), f)
                           == bytes.size();
    std::fclose(f);
    return ok;
}

static std::string temp_path(const char* name)
{
    constexpr const char* kVariables[] = { "TMPDIR", "TEMP", "TMP" };
    std::string path;
    for (const char* variable : kVariables) {
        const char* value = std::getenv(variable);
        if (value && *value) {
            path = value;
            break;
        }
    }
    if (path.empty()) {
#if defined(_WIN32)
        path = ".";
#else
        path = "/tmp";
#endif
    }
    if (path.back() != '/' && path.back() != '\\') {
#if defined(_WIN32)
        path.push_back('\\');
#else
        path.push_back('/');
#endif
    }
    path.append(name);
    return path;
}

static std::vector<std::byte> make_minimal_tiff_with_orientation(
    uint16_t orientation) noexcept
{
    std::vector<std::byte> bytes;
    bytes.reserve(26U);
    bytes.push_back(std::byte { 'I' });
    bytes.push_back(std::byte { 'I' });
    bytes.push_back(std::byte { 42U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 8U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 1U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0x12U });
    bytes.push_back(std::byte { 0x01U });
    bytes.push_back(std::byte { 0x03U });
    bytes.push_back(std::byte { 0x00U });
    bytes.push_back(std::byte { 0x01U });
    bytes.push_back(std::byte { 0x00U });
    bytes.push_back(std::byte { 0x00U });
    bytes.push_back(std::byte { 0x00U });
    bytes.push_back(std::byte { static_cast<uint8_t>(orientation & 0xFFU) });
    bytes.push_back(
        std::byte { static_cast<uint8_t>((orientation >> 8U) & 0xFFU) });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    return bytes;
}

static std::vector<std::byte> make_minimal_tiff_without_orientation() noexcept
{
    std::vector<std::byte> bytes;
    bytes.reserve(14U);
    bytes.push_back(std::byte { 'I' });
    bytes.push_back(std::byte { 'I' });
    bytes.push_back(std::byte { 42U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 8U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    bytes.push_back(std::byte { 0U });
    return bytes;
}

TEST(LibRawAdapter, MapsExactRawOrientations) {
    {
        const LibRawOrientationResult result
            = map_exif_orientation_to_libraw_flip(1);
        EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
        EXPECT_EQ(result.code, LibRawOrientationCode::None);
        EXPECT_EQ(result.libraw_flip, 0U);
        EXPECT_FALSE(result.apply_flip);
        EXPECT_FALSE(result.mirrored);
    }
    {
        const LibRawOrientationResult result
            = map_exif_orientation_to_libraw_flip(3);
        EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
        EXPECT_EQ(result.libraw_flip, 3U);
        EXPECT_TRUE(result.apply_flip);
    }
    {
        const LibRawOrientationResult result
            = map_exif_orientation_to_libraw_flip(6);
        EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
        EXPECT_EQ(result.libraw_flip, 6U);
        EXPECT_TRUE(result.apply_flip);
    }
    {
        const LibRawOrientationResult result
            = map_exif_orientation_to_libraw_flip(8);
        EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
        EXPECT_EQ(result.libraw_flip, 5U);
        EXPECT_TRUE(result.apply_flip);
    }
}

TEST(LibRawAdapter, MapsExactLibRawFlipsBackToExifOrientation) {
    {
        const LibRawFlipToExifResult result
            = map_libraw_flip_to_exif_orientation(0U);
        EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
        EXPECT_EQ(result.code, LibRawFlipToExifCode::None);
        EXPECT_EQ(result.exif_orientation, 1U);
    }
    {
        const LibRawFlipToExifResult result
            = map_libraw_flip_to_exif_orientation(3U);
        EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
        EXPECT_EQ(result.exif_orientation, 3U);
    }
    {
        const LibRawFlipToExifResult result
            = map_libraw_flip_to_exif_orientation(5U);
        EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
        EXPECT_EQ(result.exif_orientation, 8U);
    }
    {
        const LibRawFlipToExifResult result
            = map_libraw_flip_to_exif_orientation(6U);
        EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
        EXPECT_EQ(result.exif_orientation, 6U);
    }
}

TEST(LibRawAdapter, RejectsInvalidLibRawFlip) {
    const LibRawFlipToExifResult result
        = map_libraw_flip_to_exif_orientation(2U);
    EXPECT_EQ(result.status, LibRawOrientationStatus::InvalidArgument);
    EXPECT_EQ(result.code, LibRawFlipToExifCode::InvalidLibRawFlip);
}

TEST(LibRawAdapter, PreservesEmbeddedPreviewWhenMappingLibRawFlipBack) {
    LibRawFlipToExifOptions options {};
    options.target = LibRawOrientationTarget::EmbeddedPreview;

    const LibRawFlipToExifResult result
        = map_libraw_flip_to_exif_orientation(6U, options);
    EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
    EXPECT_EQ(result.code, LibRawFlipToExifCode::PreviewPassThrough);
    EXPECT_EQ(result.exif_orientation, 1U);
    EXPECT_TRUE(result.preview_passthrough);
}

TEST(LibRawAdapter, CanMapEmbeddedPreviewFlipWhenPassthroughDisabled) {
    LibRawFlipToExifOptions options {};
    options.target = LibRawOrientationTarget::EmbeddedPreview;
    options.preserve_embedded_preview_orientation = false;

    const LibRawFlipToExifResult result
        = map_libraw_flip_to_exif_orientation(6U, options);
    EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
    EXPECT_EQ(result.code, LibRawFlipToExifCode::None);
    EXPECT_EQ(result.exif_orientation, 6U);
    EXPECT_FALSE(result.preview_passthrough);
}

TEST(LibRawAdapter, RejectsInvalidExifOrientation) {
    const LibRawOrientationResult zero
        = map_exif_orientation_to_libraw_flip(0);
    EXPECT_EQ(zero.status, LibRawOrientationStatus::InvalidArgument);
    EXPECT_EQ(zero.code, LibRawOrientationCode::InvalidExifOrientation);

    const LibRawOrientationResult too_large
        = map_exif_orientation_to_libraw_flip(9);
    EXPECT_EQ(too_large.status, LibRawOrientationStatus::InvalidArgument);
    EXPECT_EQ(too_large.code, LibRawOrientationCode::InvalidExifOrientation);
}

TEST(LibRawAdapter, RejectsMirroredOrientationsByDefault) {
    const LibRawOrientationResult result
        = map_exif_orientation_to_libraw_flip(5);
    EXPECT_EQ(result.status, LibRawOrientationStatus::Unsupported);
    EXPECT_EQ(result.code,
              LibRawOrientationCode::UnsupportedMirroredOrientation);
    EXPECT_TRUE(result.mirrored);
    EXPECT_FALSE(result.apply_flip);
}

TEST(LibRawAdapter, CanDropMirrorIntoRotationOnlyApproximation) {
    LibRawOrientationOptions options {};
    options.mirror_policy = LibRawMirrorPolicy::DropMirror;

    {
        const LibRawOrientationResult result
            = map_exif_orientation_to_libraw_flip(2, options);
        EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
        EXPECT_EQ(result.code, LibRawOrientationCode::MirroredOrientationDropped);
        EXPECT_EQ(result.libraw_flip, 0U);
        EXPECT_FALSE(result.apply_flip);
        EXPECT_TRUE(result.mirrored);
    }
    {
        const LibRawOrientationResult result
            = map_exif_orientation_to_libraw_flip(4, options);
        EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
        EXPECT_EQ(result.libraw_flip, 3U);
        EXPECT_TRUE(result.apply_flip);
    }
    {
        const LibRawOrientationResult result
            = map_exif_orientation_to_libraw_flip(5, options);
        EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
        EXPECT_EQ(result.libraw_flip, 5U);
        EXPECT_TRUE(result.apply_flip);
    }
    {
        const LibRawOrientationResult result
            = map_exif_orientation_to_libraw_flip(7, options);
        EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
        EXPECT_EQ(result.libraw_flip, 6U);
        EXPECT_TRUE(result.apply_flip);
    }
}

TEST(LibRawAdapter, PreservesEmbeddedPreviewByDefault) {
    LibRawOrientationOptions options {};
    options.target = LibRawOrientationTarget::EmbeddedPreview;

    const LibRawOrientationResult result
        = map_exif_orientation_to_libraw_flip(6, options);
    EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
    EXPECT_EQ(result.code, LibRawOrientationCode::PreviewPassThrough);
    EXPECT_TRUE(result.preview_passthrough);
    EXPECT_EQ(result.libraw_flip, 0U);
    EXPECT_FALSE(result.apply_flip);
}

TEST(LibRawAdapter, CanMapEmbeddedPreviewWhenPassthroughDisabled) {
    LibRawOrientationOptions options {};
    options.target = LibRawOrientationTarget::EmbeddedPreview;
    options.preserve_embedded_preview_orientation = false;

    const LibRawOrientationResult result
        = map_exif_orientation_to_libraw_flip(6, options);
    EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
    EXPECT_EQ(result.code, LibRawOrientationCode::None);
    EXPECT_EQ(result.libraw_flip, 6U);
    EXPECT_TRUE(result.apply_flip);
    EXPECT_FALSE(result.preview_passthrough);
}

TEST(LibRawAdapter, ReadsExifOrientationFromMetaStore) {
    MetaStore store;

    Entry orientation;
    orientation.key = make_exif_tag_key(store.arena(), "ifd0", 0x0112U);
    orientation.value = make_u16(6U);
    orientation.origin.block = 0U;
    orientation.origin.order_in_block = 0U;
    (void)store.add_entry(orientation);

    const LibRawOrientationResult result
        = map_meta_orientation_to_libraw_flip(store);
    EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
    EXPECT_EQ(result.source, LibRawOrientationSource::ExifIfd0);
    EXPECT_EQ(result.exif_orientation, 6U);
    EXPECT_TRUE(result.has_exif_ifd0_orientation);
    EXPECT_EQ(result.exif_ifd0_orientation, 6U);
    EXPECT_FALSE(result.has_xmp_tiff_orientation);
    EXPECT_FALSE(result.orientation_conflict);
    EXPECT_EQ(result.libraw_flip, 6U);
}

TEST(LibRawAdapter, FallsBackToXmpOrientationWhenExifMissing) {
    MetaStore store;

    Entry orientation;
    orientation.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/tiff/1.0/", "Orientation");
    orientation.value = make_text(store.arena(), "8", TextEncoding::Utf8);
    orientation.origin.block = 0U;
    orientation.origin.order_in_block = 0U;
    (void)store.add_entry(orientation);

    const LibRawOrientationResult result
        = map_meta_orientation_to_libraw_flip(store);
    EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
    EXPECT_EQ(result.source, LibRawOrientationSource::XmpTiffOrientation);
    EXPECT_EQ(result.exif_orientation, 8U);
    EXPECT_FALSE(result.has_exif_ifd0_orientation);
    EXPECT_TRUE(result.has_xmp_tiff_orientation);
    EXPECT_EQ(result.xmp_tiff_orientation, 8U);
    EXPECT_FALSE(result.orientation_conflict);
    EXPECT_EQ(result.libraw_flip, 5U);
}

TEST(LibRawAdapter, AssumesDefaultOrientationWhenMissing) {
    MetaStore store;

    const LibRawOrientationResult result
        = map_meta_orientation_to_libraw_flip(store);
    EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
    EXPECT_EQ(result.code,
              LibRawOrientationCode::MissingExifOrientationAssumedDefault);
    EXPECT_EQ(result.source, LibRawOrientationSource::AssumedDefault);
    EXPECT_EQ(result.exif_orientation, 1U);
    EXPECT_EQ(result.libraw_flip, 0U);
}

TEST(LibRawAdapter, PrefersExifOverXmpOrientation) {
    MetaStore store;

    Entry xmp_orientation;
    xmp_orientation.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/tiff/1.0/", "Orientation");
    xmp_orientation.value = make_text(store.arena(), "8", TextEncoding::Utf8);
    xmp_orientation.origin.block = 0U;
    xmp_orientation.origin.order_in_block = 0U;
    (void)store.add_entry(xmp_orientation);

    Entry exif_orientation;
    exif_orientation.key = make_exif_tag_key(store.arena(), "ifd0", 0x0112U);
    exif_orientation.value = make_u16(6U);
    exif_orientation.origin.block = 0U;
    exif_orientation.origin.order_in_block = 1U;
    (void)store.add_entry(exif_orientation);

    const LibRawOrientationResult result
        = map_meta_orientation_to_libraw_flip(store);
    EXPECT_EQ(result.status, LibRawOrientationStatus::Ok);
    EXPECT_EQ(result.source, LibRawOrientationSource::ExifIfd0);
    EXPECT_EQ(result.exif_orientation, 6U);
    EXPECT_TRUE(result.has_exif_ifd0_orientation);
    EXPECT_TRUE(result.has_xmp_tiff_orientation);
    EXPECT_EQ(result.exif_ifd0_orientation, 6U);
    EXPECT_EQ(result.xmp_tiff_orientation, 8U);
    EXPECT_TRUE(result.orientation_conflict);
    EXPECT_EQ(result.libraw_flip, 6U);
}

TEST(LibRawAdapter, FileHelperReadsExifOrientationFromTiff) {
    const std::vector<std::byte> bytes = make_minimal_tiff_with_orientation(6U);
    const std::string path = temp_path("openmeta_libraw_orientation.tif");
    ASSERT_TRUE(write_bytes(path.c_str(), bytes));

    const LibRawOrientationFileResult result
        = map_meta_orientation_to_libraw_flip_from_file(path.c_str());
    EXPECT_EQ(result.file_status, LibRawOrientationFileStatus::Ok);
    EXPECT_EQ(result.orientation.status, LibRawOrientationStatus::Ok);
    EXPECT_EQ(result.orientation.source, LibRawOrientationSource::ExifIfd0);
    EXPECT_EQ(result.orientation.exif_orientation, 6U);
    EXPECT_TRUE(result.orientation.has_exif_ifd0_orientation);
    EXPECT_EQ(result.orientation.exif_ifd0_orientation, 6U);
    EXPECT_EQ(result.orientation.libraw_flip, 6U);
}

TEST(LibRawAdapter, FileHelperAssumesDefaultWhenOrientationMissing) {
    const std::vector<std::byte> bytes = make_minimal_tiff_without_orientation();
    const std::string path
        = temp_path("openmeta_libraw_orientation_default.tif");
    ASSERT_TRUE(write_bytes(path.c_str(), bytes));

    const LibRawOrientationFileResult result
        = map_meta_orientation_to_libraw_flip_from_file(path.c_str());
    EXPECT_EQ(result.file_status, LibRawOrientationFileStatus::Ok);
    EXPECT_EQ(result.orientation.status, LibRawOrientationStatus::Ok);
    EXPECT_EQ(result.orientation.code,
              LibRawOrientationCode::MissingExifOrientationAssumedDefault);
    EXPECT_EQ(result.orientation.source, LibRawOrientationSource::AssumedDefault);
    EXPECT_EQ(result.orientation.libraw_flip, 0U);
}

}  // namespace
}  // namespace openmeta
