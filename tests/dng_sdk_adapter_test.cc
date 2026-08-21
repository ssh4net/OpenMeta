// SPDX-License-Identifier: Apache-2.0

#include "openmeta/dng_sdk_adapter.h"

#include "openmeta/meta_key.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#    include <process.h>
#else
#    include <unistd.h>
#endif

#if defined(OPENMETA_HAS_DNG_SDK) && OPENMETA_HAS_DNG_SDK
#    include "dng_auto_ptr.h"
#    include "dng_exif.h"
#    include "dng_file_stream.h"
#    include "dng_host.h"
#    include "dng_info.h"
#    include "dng_negative.h"
#endif

namespace {

static void
append_ascii(std::vector<std::byte>* out, std::string_view s)
{
    ASSERT_NE(out, nullptr);
    for (size_t i = 0; i < s.size(); ++i) {
        out->push_back(std::byte { static_cast<unsigned char>(s[i]) });
    }
}

static void
append_u16le(std::vector<std::byte>* out, uint16_t v)
{
    ASSERT_NE(out, nullptr);
    out->push_back(std::byte { static_cast<unsigned char>(v & 0xFFU) });
    out->push_back(
        std::byte { static_cast<unsigned char>((v >> 8U) & 0xFFU) });
}

static void
append_u32le(std::vector<std::byte>* out, uint32_t v)
{
    ASSERT_NE(out, nullptr);
    append_u16le(out, static_cast<uint16_t>(v & 0xFFFFU));
    append_u16le(out, static_cast<uint16_t>((v >> 16U) & 0xFFFFU));
}

static std::string
unique_temp_path(const char* suffix)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ticks = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    const unsigned long process_id
#if defined(_WIN32)
        = static_cast<unsigned long>(::_getpid());
#else
        = static_cast<unsigned long>(::getpid());
#endif
    char buf[256];
    std::snprintf(buf, sizeof(buf), "openmeta_dng_sdk_%lu_%llu%s", process_id,
                  ticks, suffix ? suffix : "");
    return std::string(buf);
}

static bool
write_bytes_file(const std::string& path, std::span<const std::byte> bytes)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    const size_t n = std::fwrite(bytes.data(), 1U, bytes.size(), f);
    std::fclose(f);
    return n == bytes.size();
}

static bool
read_bytes_file(const std::string& path, std::vector<std::byte>* out)
{
    if (!out) {
        return false;
    }
    out->clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    const long size = std::ftell(f);
    if (size < 0 || std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    out->resize(static_cast<size_t>(size));
    const size_t n = std::fread(out->data(), 1U, out->size(), f);
    std::fclose(f);
    return n == out->size();
}

static std::vector<std::byte>
make_minimal_dng_little_endian()
{
    std::vector<std::byte> out;
    append_ascii(&out, "II");
    append_u16le(&out, 42U);
    append_u32le(&out, 8U);
    append_u16le(&out, 1U);
    append_u16le(&out, 0xC612U);
    append_u16le(&out, 1U);
    append_u32le(&out, 4U);
    out.push_back(std::byte { 0x01 });
    out.push_back(std::byte { 0x06 });
    out.push_back(std::byte { 0x00 });
    out.push_back(std::byte { 0x00 });
    append_u32le(&out, 0U);
    return out;
}

static std::vector<std::byte>
make_source_jpeg_with_make()
{
    std::vector<std::byte> tiff;
    append_ascii(&tiff, "II");
    append_u16le(&tiff, 42U);
    append_u32le(&tiff, 8U);
    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x010FU);
    append_u16le(&tiff, 2U);
    append_u32le(&tiff, 7U);
    append_u32le(&tiff, 26U);
    append_u32le(&tiff, 0U);
    append_ascii(&tiff, "Vendor");
    tiff.push_back(std::byte { 0x00 });

    std::vector<std::byte> app1;
    append_ascii(&app1, "Exif");
    app1.push_back(std::byte { 0x00 });
    app1.push_back(std::byte { 0x00 });
    app1.insert(app1.end(), tiff.begin(), tiff.end());

    std::vector<std::byte> jpeg;
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD8 });
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xE1 });
    append_u16le(&jpeg, 0U);
    const uint16_t len = static_cast<uint16_t>(app1.size() + 2U);
    jpeg[jpeg.size() - 2U] = std::byte {
        static_cast<unsigned char>((len >> 8U) & 0xFFU) };
    jpeg[jpeg.size() - 1U]
        = std::byte { static_cast<unsigned char>(len & 0xFFU) };
    jpeg.insert(jpeg.end(), app1.begin(), app1.end());
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD9 });
    return jpeg;
}

static openmeta::PreparedTransferBundle
make_prepared_dng_bundle()
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});

    openmeta::Entry make;
    make.key = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x010FU);
    make.value = openmeta::make_text(store.arena(), "Vendor",
                                     openmeta::TextEncoding::Utf8);
    make.origin.block          = block;
    make.origin.order_in_block = 0U;
    EXPECT_NE(store.add_entry(make), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(store.arena(), "OpenMeta",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    EXPECT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Dng;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    EXPECT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    return bundle;
}

}  // namespace

TEST(DngSdkAdapter, ReportsAvailabilityFlag)
{
#if defined(OPENMETA_HAS_DNG_SDK) && OPENMETA_HAS_DNG_SDK
    EXPECT_TRUE(openmeta::dng_sdk_adapter_available());
#else
    EXPECT_FALSE(openmeta::dng_sdk_adapter_available());
#endif
}

TEST(DngSdkAdapter, ReturnsUnsupportedWhenSdkUnavailable)
{
#if !defined(OPENMETA_HAS_DNG_SDK) || !OPENMETA_HAS_DNG_SDK
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Dng;
    const openmeta::DngSdkAdapterResult result
        = openmeta::apply_prepared_dng_sdk_metadata(bundle, nullptr, nullptr);
    EXPECT_EQ(result.status, openmeta::DngSdkAdapterStatus::Unsupported);
    EXPECT_FALSE(result.message.empty());
#endif
}

#if defined(OPENMETA_HAS_DNG_SDK) && OPENMETA_HAS_DNG_SDK
TEST(DngSdkAdapter, AppliesPreparedExifAndXmpToNegative)
{
    const openmeta::PreparedTransferBundle bundle = make_prepared_dng_bundle();

    ::dng_host host;
    AutoPtr<dng_negative> negative(host.Make_dng_negative());

    const openmeta::DngSdkAdapterResult result
        = openmeta::apply_prepared_dng_sdk_metadata(bundle, &host,
                                                    negative.Get());

    ASSERT_EQ(result.status, openmeta::DngSdkAdapterStatus::Ok);
    EXPECT_TRUE(result.exif_applied);
    EXPECT_TRUE(result.xmp_applied);
    EXPECT_TRUE(result.synchronized_metadata);
    ASSERT_NE(negative->GetExif(), nullptr);
    EXPECT_STREQ(negative->GetExif()->fMake.Get(), "Vendor");
}

TEST(DngSdkAdapter, UpdatesPreparedMetadataIntoDngFileStream)
{
    const openmeta::PreparedTransferBundle bundle = make_prepared_dng_bundle();

    const std::string target_path = unique_temp_path(".dng");
    const std::vector<std::byte> target = make_minimal_dng_little_endian();
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target.data(), target.size())));

    {
        ::dng_host host;
        AutoPtr<dng_negative> negative(host.Make_dng_negative());
        ASSERT_NE(negative.Get(), nullptr);

        std::FILE* file = std::fopen(target_path.c_str(), "r+b");
        ASSERT_NE(file, nullptr);
        ::dng_file_stream stream(file);

        const openmeta::DngSdkAdapterResult result
            = openmeta::update_prepared_dng_sdk_stream_metadata(
                bundle, &host, negative.Get(), &stream);
        ASSERT_EQ(result.status, openmeta::DngSdkAdapterStatus::Ok);
        EXPECT_TRUE(result.updated_stream);
        EXPECT_TRUE(result.exif_applied);
        EXPECT_TRUE(result.xmp_applied);
        stream.Flush();
    }

    std::vector<std::byte> updated;
    ASSERT_TRUE(read_bytes_file(target_path, &updated));
    std::remove(target_path.c_str());
    EXPECT_GE(updated.size(), target.size());
    EXPECT_NE(updated, target);
}

TEST(DngSdkAdapter, UpdatesExistingDngFileFromSourceFileHelper)
{
    const std::string source_path = unique_temp_path(".jpg");
    const std::vector<std::byte> source = make_source_jpeg_with_make();
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source.data(), source.size())));

    const std::string target_path = unique_temp_path(".dng");
    const std::vector<std::byte> target = make_minimal_dng_little_endian();
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target.data(), target.size())));

    const openmeta::ApplyDngSdkMetadataFileResult result
        = openmeta::update_dng_sdk_file_from_file(source_path.c_str(),
                                                  target_path.c_str());

    std::vector<std::byte> updated;
    ASSERT_TRUE(read_bytes_file(target_path, &updated));
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.adapter.status, openmeta::DngSdkAdapterStatus::Ok);
    EXPECT_TRUE(result.adapter.updated_stream);
    EXPECT_GE(updated.size(), target.size());
    EXPECT_NE(updated, target);
}
#endif
