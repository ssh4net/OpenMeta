// SPDX-License-Identifier: Apache-2.0

#include "public_metadata_fixtures.h"

#include "openmeta/meta_key.h"
#include "openmeta/metadata_transfer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct FixtureReadState final {
    std::span<const std::byte> bytes;
    uint64_t forbidden_begin = UINT64_MAX;
    uint64_t forbidden_end   = UINT64_MAX;
    uint32_t requests        = 0U;
    bool touched_forbidden   = false;
};

openmeta::RandomAccessIoResult
fixture_read_at(void* context, uint64_t offset,
                std::span<std::byte> destination) noexcept
{
    FixtureReadState* state = static_cast<FixtureReadState*>(context);
    state->requests += 1U;
    const uint64_t end = destination.size() <= UINT64_MAX - offset
                             ? offset
                                   + static_cast<uint64_t>(destination.size())
                             : UINT64_MAX;
    if (offset < state->forbidden_end && end > state->forbidden_begin) {
        state->touched_forbidden = true;
    }
    if (offset > state->bytes.size()) {
        return { openmeta::RandomAccessIoCode::Ok, 0U };
    }
    uint64_t count = static_cast<uint64_t>(state->bytes.size()) - offset;
    if (count > destination.size()) {
        count = destination.size();
    }
    if (count != 0U) {
        std::memcpy(destination.data(),
                    state->bytes.data() + static_cast<size_t>(offset),
                    static_cast<size_t>(count));
    }
    return { openmeta::RandomAccessIoCode::Ok, count };
}

struct FixtureScratch final {
    std::array<openmeta::ContainerBlockRef, 32U> blocks {};
    std::array<openmeta::ExifIfdRef, 64U> ifds {};
    std::array<uint32_t, 32U> payload_indices {};
    std::array<std::byte, 1024U> read_window {};
    std::array<std::byte, 8192U> payload {};
    std::array<std::byte, 8192U> compressed {};
    std::array<std::byte, 8192U> value {};

    openmeta::ReadTransferSourceSnapshotRandomAccessScratch view() noexcept
    {
        openmeta::ReadTransferSourceSnapshotRandomAccessScratch out;
        out.blocks             = blocks;
        out.ifds               = ifds;
        out.payload_indices    = payload_indices;
        out.read_window        = read_window;
        out.payload            = payload;
        out.compressed_payload = compressed;
        out.value              = value;
        return out;
    }
};

openmeta::MetaKeyView
exif_key(std::string_view ifd, uint16_t tag) noexcept
{
    openmeta::MetaKeyView key;
    key.kind              = openmeta::MetaKeyKind::ExifTag;
    key.data.exif_tag.ifd = ifd;
    key.data.exif_tag.tag = tag;
    return key;
}

openmeta::MetaKeyView
xmp_key(std::string_view schema, std::string_view property) noexcept
{
    openmeta::MetaKeyView key;
    key.kind                            = openmeta::MetaKeyKind::XmpProperty;
    key.data.xmp_property.schema_ns     = schema;
    key.data.xmp_property.property_path = property;
    return key;
}

using RawScanFunction = openmeta::ContainerRandomAccessScanResult (*)(
    const openmeta::RandomAccessSourceRange&,
    std::span<openmeta::ContainerBlockRef>,
    const openmeta::ContainerRandomAccessScratch&,
    const openmeta::RandomAccessReadLimits&) noexcept;

using RawMeasureFunction = openmeta::ContainerRandomAccessScanResult (*)(
    const openmeta::RandomAccessSourceRange&,
    const openmeta::ContainerRandomAccessScratch&,
    const openmeta::RandomAccessReadLimits&) noexcept;

bool
same_discovered_block(const openmeta::ContainerBlockRef& lhs,
                      const openmeta::ContainerBlockRef& rhs) noexcept
{
    return lhs.format == rhs.format && lhs.kind == rhs.kind
           && lhs.outer_offset == rhs.outer_offset
           && lhs.data_offset == rhs.data_offset && lhs.id == rhs.id;
}

uint32_t
fixture_u32le(std::span<const std::byte> bytes, size_t offset) noexcept
{
    return static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 0U]))
           | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 1U]))
              << 8U)
           | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 2U]))
              << 16U)
           | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 3U]))
              << 24U);
}

void
expect_embedded_scan_parity(
    const openmeta::test_fixture::RawEmbeddedMetadataFixture& fixture,
    RawScanFunction scan, RawMeasureFunction measure)
{
    std::array<openmeta::ContainerBlockRef, 32U> contiguous_blocks {};
    const openmeta::ScanResult contiguous
        = openmeta::scan_auto(fixture.bytes, contiguous_blocks);
    ASSERT_EQ(contiguous.status, openmeta::ScanStatus::Ok);
    ASSERT_GT(contiguous.written, 0U);

    constexpr uint64_t prefix_size = 37U;
    std::vector<std::byte> backing(prefix_size, std::byte { 0xa5U });
    backing.insert(backing.end(), fixture.bytes.begin(), fixture.bytes.end());
    FixtureReadState callback { backing };
    callback.forbidden_begin = prefix_size + fixture.entropy_offset;
    callback.forbidden_end   = callback.forbidden_begin + fixture.entropy_size;
    const openmeta::RandomAccessSource source
        = openmeta::make_callback_random_access_source(backing.size(),
                                                       &callback,
                                                       fixture_read_at, true);
    const openmeta::RandomAccessSourceRange range
        = openmeta::make_random_access_source_range(source, prefix_size,
                                                    fixture.bytes.size());
    std::array<std::byte, 1024U> read_window {};
    openmeta::ContainerRandomAccessScratch scratch;
    scratch.read_window                       = read_window;
    scratch.window_options.minimum_read_bytes = 0U;
    std::array<openmeta::ContainerBlockRef, 32U> positional_blocks {};
    const openmeta::ContainerRandomAccessScanResult positional
        = scan(range, positional_blocks, scratch, {});

    ASSERT_TRUE(positional.complete());
    ASSERT_EQ(positional.scan.status, openmeta::ScanStatus::Ok);
    ASSERT_EQ(positional.scan.written, contiguous.written);
    EXPECT_FALSE(callback.touched_forbidden);
    EXPECT_LT(positional.input.bytes_requested,
              static_cast<uint64_t>(fixture.bytes.size()));
    for (uint32_t i = 0U; i < positional.scan.written; ++i) {
        bool found = false;
        for (uint32_t j = 0U; j < contiguous.written; ++j) {
            found |= same_discovered_block(positional_blocks[i],
                                           contiguous_blocks[j]);
        }
        EXPECT_TRUE(found) << "unmatched positional block " << i;
    }

    FixtureReadState measure_callback { backing };
    measure_callback.forbidden_begin = callback.forbidden_begin;
    measure_callback.forbidden_end   = callback.forbidden_end;
    const openmeta::RandomAccessSource measure_source
        = openmeta::make_callback_random_access_source(backing.size(),
                                                       &measure_callback,
                                                       fixture_read_at, true);
    const openmeta::ContainerRandomAccessScanResult measured = measure(
        openmeta::make_random_access_source_range(measure_source, prefix_size,
                                                  fixture.bytes.size()),
        scratch, {});
    ASSERT_TRUE(measured.complete());
    EXPECT_EQ(measured.scan.status, openmeta::ScanStatus::Ok);
    EXPECT_EQ(measured.scan.needed, positional.scan.needed);
    EXPECT_FALSE(measure_callback.touched_forbidden);
}

void
expect_embedded_snapshot(
    const openmeta::test_fixture::RawEmbeddedMetadataFixture& fixture,
    openmeta::ContainerFormat format, openmeta::MetaKeyView native_key)
{
    constexpr uint64_t prefix_size = 43U;
    std::vector<std::byte> backing(prefix_size, std::byte { 0xc3U });
    backing.insert(backing.end(), fixture.bytes.begin(), fixture.bytes.end());
    FixtureReadState callback { backing };
    callback.forbidden_begin = prefix_size + fixture.entropy_offset;
    callback.forbidden_end   = callback.forbidden_begin + fixture.entropy_size;
    const openmeta::RandomAccessSource source
        = openmeta::make_callback_random_access_source(backing.size(),
                                                       &callback,
                                                       fixture_read_at, true);
    FixtureScratch storage;
    openmeta::ReadTransferSourceSnapshotRandomAccessScratch scratch
        = storage.view();
    scratch.window_options.minimum_read_bytes = 0U;
    openmeta::ReadTransferSourceSnapshotRandomAccessOptions options;
    options.decode_makernote           = true;
    options.decode_embedded_containers = true;
    const openmeta::ReadTransferSourceSnapshotRandomAccessResult result
        = openmeta::read_transfer_source_snapshot_random_access(
            openmeta::make_random_access_source_range(source, prefix_size,
                                                      fixture.bytes.size()),
            format, scratch, options);

    ASSERT_TRUE(result.complete())
        << "status=" << static_cast<unsigned>(result.status)
        << " code=" << static_cast<unsigned>(result.code)
        << " residual=" << result.residual_metadata_paths;
    EXPECT_EQ(result.snapshot.store.find_all(native_key).size(), 1U);
    EXPECT_EQ(result.snapshot.store.find_all(exif_key("ifd0", 0x010fU)).size(),
              1U);
    EXPECT_EQ(
        result.snapshot.store.find_all(exif_key("mk_nikon0", 0x0001U)).size(),
        1U);
    EXPECT_EQ(result.snapshot.store
                  .find_all(
                      xmp_key("http://ns.adobe.com/xap/1.0/", "CreatorTool"))
                  .size(),
              1U);
    if (format == openmeta::ContainerFormat::Raf) {
        EXPECT_EQ(
            result.snapshot.store.find_all(exif_key("ifd0", 0x0106U)).size(),
            1U);
    }
    EXPECT_FALSE(callback.touched_forbidden);
    EXPECT_LT(result.input.bytes_requested,
              static_cast<uint64_t>(fixture.bytes.size()));
}

void
expect_nikon_fixture(std::span<const std::byte> bytes, bool expect_dng)
{
    openmeta::ReadTransferSourceSnapshotOptions options;
    options.decode_makernote = true;
    const openmeta::ReadTransferSourceSnapshotBytesResult result
        = openmeta::read_transfer_source_snapshot_bytes(bytes, options);
    ASSERT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.snapshot.store.find_all(exif_key("ifd0", 0x0112U)).size(),
              1U);
    EXPECT_EQ(
        result.snapshot.store.find_all(exif_key("mk_nikon0", 0x0001U)).size(),
        1U);
    EXPECT_EQ(result.snapshot.store.find_all(exif_key("ifd0", 0xc612U)).size(),
              expect_dng ? 1U : 0U);
}

void
expect_native_positional(std::span<const std::byte> bytes,
                         openmeta::ContainerFormat format,
                         uint64_t forbidden_begin, uint64_t forbidden_end,
                         openmeta::MetaKeyView expected_key)
{
    FixtureScratch contiguous_storage;
    const openmeta::RandomAccessSource contiguous_source
        = openmeta::make_memory_random_access_source(bytes);
    const openmeta::ReadTransferSourceSnapshotRandomAccessResult contiguous
        = openmeta::read_transfer_source_snapshot_random_access(
            openmeta::make_random_access_source_range(contiguous_source),
            format, contiguous_storage.view());
    ASSERT_TRUE(contiguous.complete())
        << "format=" << static_cast<unsigned>(format)
        << " status=" << static_cast<unsigned>(contiguous.status)
        << " code=" << static_cast<unsigned>(contiguous.code)
        << " exif=" << static_cast<unsigned>(contiguous.read.exif.status)
        << " residual=" << contiguous.residual_metadata_paths;

    constexpr uint64_t prefix_size = 31U;
    std::vector<std::byte> backing(prefix_size, std::byte { 0xa5U });
    backing.insert(backing.end(), bytes.begin(), bytes.end());
    FixtureReadState callback { backing };
    callback.forbidden_begin = prefix_size + forbidden_begin;
    callback.forbidden_end   = prefix_size + forbidden_end;
    const openmeta::RandomAccessSource source
        = openmeta::make_callback_random_access_source(backing.size(),
                                                       &callback,
                                                       fixture_read_at, true);
    FixtureScratch storage;
    const openmeta::ReadTransferSourceSnapshotRandomAccessResult positional
        = openmeta::read_transfer_source_snapshot_random_access(
            openmeta::make_random_access_source_range(source, prefix_size,
                                                      bytes.size()),
            format, storage.view());

    ASSERT_TRUE(positional.complete());
    EXPECT_EQ(positional.entry_count, contiguous.entry_count);
    EXPECT_EQ(positional.snapshot.store.find_all(expected_key).size(), 1U);
    EXPECT_GT(callback.requests, 0U);
    EXPECT_FALSE(callback.touched_forbidden);
    EXPECT_LT(positional.input.bytes_requested,
              static_cast<uint64_t>(bytes.size()));
}

TEST(PublicMetadataFixtures, DecodePortableIntegrationSet)
{
    expect_nikon_fixture(openmeta::test_fixture::tiff_with_nikon_makernote(),
                         false);
    expect_nikon_fixture(openmeta::test_fixture::dng_with_nikon_makernote(),
                         true);
    expect_nikon_fixture(openmeta::test_fixture::webp_with_exif(), false);
    expect_nikon_fixture(openmeta::test_fixture::avif_with_exif(), false);
    expect_nikon_fixture(openmeta::test_fixture::jp2_with_exif(), false);
    expect_nikon_fixture(openmeta::test_fixture::jxl_with_exif(), false);
}

TEST(PublicMetadataFixtures, NativeRawReadersSkipImagePayloadRanges)
{
    const std::vector<std::byte> raf
        = openmeta::test_fixture::raf_with_native_directory();
    expect_native_positional(raf, openmeta::ContainerFormat::Raf, 0x88U, 4096U,
                             exif_key("raf_0", 0x0100U));

    const std::vector<std::byte> x3f
        = openmeta::test_fixture::x3f_with_native_properties();
    expect_native_positional(x3f, openmeta::ContainerFormat::X3f, 264U, 4096U,
                             exif_key("x3f_prop", 0x0008U));

    const std::vector<std::byte> crw
        = openmeta::test_fixture::crw_with_native_ciff();
    expect_native_positional(crw, openmeta::ContainerFormat::Crw, 14U, 4096U,
                             exif_key("ciff_root", 0x0801U));
}

TEST(PublicMetadataFixtures, RafAndX3fDeclaredEmbeddedScansArePositional)
{
    expect_embedded_scan_parity(
        openmeta::test_fixture::raf_with_embedded_metadata(),
        openmeta::scan_raf_random_access,
        openmeta::measure_scan_raf_random_access);
    expect_embedded_scan_parity(
        openmeta::test_fixture::x3f_with_embedded_metadata(),
        openmeta::scan_x3f_random_access,
        openmeta::measure_scan_x3f_random_access);
}

TEST(PublicMetadataFixtures, RafAndX3fSnapshotsDecodeDeclaredEmbeddedMetadata)
{
    const openmeta::test_fixture::RawEmbeddedMetadataFixture raf
        = openmeta::test_fixture::raf_with_embedded_metadata();
    expect_embedded_snapshot(raf, openmeta::ContainerFormat::Raf,
                             exif_key("raf_0", 0x0100U));

    const openmeta::test_fixture::RawEmbeddedMetadataFixture x3f
        = openmeta::test_fixture::x3f_with_embedded_metadata();
    expect_embedded_snapshot(x3f, openmeta::ContainerFormat::X3f,
                             exif_key("x3f_prop", 0x0008U));
}

TEST(PublicMetadataFixtures, RafAndX3fEmbeddedScansReportBoundsAndLimits)
{
    openmeta::test_fixture::RawEmbeddedMetadataFixture raf
        = openmeta::test_fixture::raf_with_embedded_metadata();
    openmeta::test_fixture::detail::write_u32be(&raf.bytes, 0x54U, UINT32_MAX);
    FixtureReadState raf_callback { raf.bytes };
    const openmeta::RandomAccessSource raf_source
        = openmeta::make_callback_random_access_source(raf.bytes.size(),
                                                       &raf_callback,
                                                       fixture_read_at, true);
    std::array<std::byte, 1024U> read_window {};
    openmeta::ContainerRandomAccessScratch scan_scratch;
    scan_scratch.read_window = read_window;
    std::array<openmeta::ContainerBlockRef, 32U> blocks {};
    const openmeta::ContainerRandomAccessScanResult malformed
        = openmeta::scan_raf_random_access(
            openmeta::make_random_access_source_range(raf_source), blocks,
            scan_scratch);
    EXPECT_EQ(malformed.scan.status, openmeta::ScanStatus::Malformed);

    openmeta::test_fixture::RawEmbeddedMetadataFixture malformed_x3f
        = openmeta::test_fixture::x3f_with_embedded_metadata();
    const uint32_t directory_offset
        = fixture_u32le(malformed_x3f.bytes, malformed_x3f.bytes.size() - 4U);
    openmeta::test_fixture::detail::write_u32le(&malformed_x3f.bytes,
                                                directory_offset + 24U,
                                                UINT32_MAX);
    FixtureReadState malformed_x3f_callback { malformed_x3f.bytes };
    const openmeta::RandomAccessSource malformed_x3f_source
        = openmeta::make_callback_random_access_source(
            malformed_x3f.bytes.size(), &malformed_x3f_callback,
            fixture_read_at, true);
    const openmeta::ContainerRandomAccessScanResult malformed_section
        = openmeta::scan_x3f_random_access(
            openmeta::make_random_access_source_range(malformed_x3f_source),
            blocks, scan_scratch);
    EXPECT_EQ(malformed_section.scan.status, openmeta::ScanStatus::Malformed);

    const openmeta::test_fixture::RawEmbeddedMetadataFixture x3f
        = openmeta::test_fixture::x3f_with_embedded_metadata();
    FixtureReadState x3f_callback { x3f.bytes };
    const openmeta::RandomAccessSource x3f_source
        = openmeta::make_callback_random_access_source(x3f.bytes.size(),
                                                       &x3f_callback,
                                                       fixture_read_at, true);
    openmeta::RandomAccessReadLimits limits;
    limits.max_requests = 1U;
    const openmeta::ContainerRandomAccessScanResult limited
        = openmeta::scan_x3f_random_access(
            openmeta::make_random_access_source_range(x3f_source), blocks,
            scan_scratch, limits);
    EXPECT_EQ(limited.input.code,
              openmeta::RandomAccessReadCode::RequestLimitExceeded);
}

TEST(PublicMetadataFixtures, NativeRawReadersReportMalformedAndScratchLimits)
{
    std::vector<std::byte> raf
        = openmeta::test_fixture::raf_with_native_directory();
    openmeta::test_fixture::detail::write_u32be(&raf, 0x5cU, UINT32_MAX);
    FixtureReadState raf_callback { raf };
    const openmeta::RandomAccessSource raf_source
        = openmeta::make_callback_random_access_source(raf.size(),
                                                       &raf_callback,
                                                       fixture_read_at, true);
    FixtureScratch raf_storage;
    const openmeta::ReadTransferSourceSnapshotRandomAccessResult malformed
        = openmeta::read_transfer_source_snapshot_random_access(
            openmeta::make_random_access_source_range(raf_source),
            openmeta::ContainerFormat::Raf, raf_storage.view());
    EXPECT_EQ(malformed.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(malformed.read.exif.status,
              openmeta::ExifDecodeStatus::Malformed);

    const std::vector<std::byte> x3f
        = openmeta::test_fixture::x3f_with_native_properties();
    FixtureReadState x3f_callback { x3f };
    const openmeta::RandomAccessSource x3f_source
        = openmeta::make_callback_random_access_source(x3f.size(),
                                                       &x3f_callback,
                                                       fixture_read_at, true);
    FixtureScratch x3f_storage;
    openmeta::ReadTransferSourceSnapshotRandomAccessScratch x3f_scratch
        = x3f_storage.view();
    x3f_scratch.value = {};
    const openmeta::ReadTransferSourceSnapshotRandomAccessResult scratch
        = openmeta::read_transfer_source_snapshot_random_access(
            openmeta::make_random_access_source_range(x3f_source),
            openmeta::ContainerFormat::X3f, x3f_scratch);
    EXPECT_EQ(scratch.status, openmeta::TransferStatus::LimitExceeded);
    EXPECT_EQ(
        scratch.code,
        openmeta::ReadTransferSourceSnapshotRandomAccessCode::ScratchTooSmall);
    EXPECT_GT(scratch.value_scratch_needed, 0U);
}

TEST(PublicMetadataFixtures, NativeRawReadersHonorCumulativeRequestLimits)
{
    const std::vector<std::byte> crw
        = openmeta::test_fixture::crw_with_native_ciff();
    FixtureReadState callback { crw };
    const openmeta::RandomAccessSource source
        = openmeta::make_callback_random_access_source(crw.size(), &callback,
                                                       fixture_read_at, true);
    FixtureScratch storage;
    openmeta::RandomAccessReadLimits limits;
    limits.max_requests = 1U;
    const openmeta::ReadTransferSourceSnapshotRandomAccessResult result
        = openmeta::read_transfer_source_snapshot_random_access(
            openmeta::make_random_access_source_range(source),
            openmeta::ContainerFormat::Crw, storage.view(), {}, limits);
    EXPECT_EQ(result.status, openmeta::TransferStatus::LimitExceeded);
    EXPECT_EQ(result.input.code,
              openmeta::RandomAccessReadCode::RequestLimitExceeded);
}

TEST(PublicMetadataFixtures, StructuredDiagnosticsPreserveFailureDetails)
{
    openmeta::ReadTransferSourceSnapshotRandomAccessResult result;
    result.status     = openmeta::TransferStatus::LimitExceeded;
    result.format     = openmeta::ContainerFormat::Crw;
    result.input.code = openmeta::RandomAccessReadCode::RequestLimitExceeded;
    result.input.failure_offset        = 4096U;
    result.input.failure_request_bytes = 10U;

    std::array<openmeta::ReadTransferSourceDiagnostic, 2U> diagnostics {};
    const openmeta::ReadTransferSourceDiagnosticsResult collected
        = openmeta::collect_read_transfer_source_diagnostics(result,
                                                             diagnostics);
    ASSERT_TRUE(collected.complete());
    ASSERT_EQ(collected.written, 1U);
    EXPECT_EQ(diagnostics[0].code,
              openmeta::ReadTransferSourceDiagnosticCode::ResourceLimit);
    EXPECT_EQ(diagnostics[0].domain,
              openmeta::ReadTransferSourceDiagnosticDomain::Input);
    EXPECT_EQ(diagnostics[0].offset, 4096U);
    EXPECT_EQ(diagnostics[0].required_bytes, 10U);
    EXPECT_STREQ(openmeta::read_transfer_source_diagnostic_code_name(
                     diagnostics[0].code),
                 "resource_limit");
    EXPECT_STREQ(openmeta::read_transfer_source_diagnostic_message(
                     diagnostics[0].code),
                 "metadata read resource limit was reached");
}

TEST(PublicMetadataFixtures, StructuredDiagnosticsReportScratchAndMalformed)
{
    openmeta::ReadTransferSourceSnapshotRandomAccessResult result;
    result.status                     = openmeta::TransferStatus::Malformed;
    result.format                     = openmeta::ContainerFormat::Raf;
    result.value_scratch_needed       = 4096U;
    result.read.exif.status           = openmeta::ExifDecodeStatus::Malformed;
    result.read.exif.limit_ifd_offset = 0x5cU;
    result.read.exif.limit_tag        = 0x0100U;

    std::array<openmeta::ReadTransferSourceDiagnostic, 2U> diagnostics {};
    const openmeta::ReadTransferSourceDiagnosticsResult collected
        = openmeta::collect_read_transfer_source_diagnostics(result,
                                                             diagnostics);
    ASSERT_TRUE(collected.complete());
    ASSERT_EQ(collected.written, 2U);
    EXPECT_EQ(diagnostics[0].code,
              openmeta::ReadTransferSourceDiagnosticCode::ScratchTooSmall);
    EXPECT_EQ(diagnostics[0].domain,
              openmeta::ReadTransferSourceDiagnosticDomain::Exif);
    EXPECT_EQ(diagnostics[0].required_bytes, 4096U);
    EXPECT_EQ(diagnostics[1].code,
              openmeta::ReadTransferSourceDiagnosticCode::MalformedExif);
    EXPECT_EQ(diagnostics[1].offset, 0x5cU);
    EXPECT_EQ(diagnostics[1].tag, 0x0100U);
}

TEST(PublicMetadataFixtures, StructuredDiagnosticsClassifyDecoderLimits)
{
    openmeta::ReadTransferSourceSnapshotRandomAccessResult result;
    result.status          = openmeta::TransferStatus::LimitExceeded;
    result.format          = openmeta::ContainerFormat::Exr;
    result.read.exr.status = openmeta::ExrDecodeStatus::LimitExceeded;

    std::array<openmeta::ReadTransferSourceDiagnostic, 1U> diagnostic {};
    const openmeta::ReadTransferSourceDiagnosticsResult collected
        = openmeta::collect_read_transfer_source_diagnostics(result,
                                                             diagnostic);
    ASSERT_TRUE(collected.complete());
    ASSERT_EQ(collected.written, 1U);
    EXPECT_EQ(diagnostic[0].code,
              openmeta::ReadTransferSourceDiagnosticCode::ResourceLimit);
    EXPECT_EQ(diagnostic[0].domain,
              openmeta::ReadTransferSourceDiagnosticDomain::Exr);
}

TEST(PublicMetadataFixtures, StructuredDiagnosticsClassifyRequestedResiduals)
{
    openmeta::ReadTransferSourceSnapshotRandomAccessResult result;
    result.format = openmeta::ContainerFormat::Tiff;
    result.code   = openmeta::ReadTransferSourceSnapshotRandomAccessCode::
        ResidualMetadataPaths;
    result.residual_metadata_paths = 3U;
    openmeta::ReadTransferSourceDiagnosticOptions options;
    options.decode_makernote_requested           = true;
    options.decode_embedded_containers_requested = true;

    std::array<openmeta::ReadTransferSourceDiagnostic, 1U> diagnostic {};
    const openmeta::ReadTransferSourceDiagnosticsResult collected
        = openmeta::collect_read_transfer_source_diagnostics(result, diagnostic,
                                                             options);
    EXPECT_FALSE(collected.complete());
    EXPECT_EQ(collected.written, 1U);
    EXPECT_EQ(collected.needed, 2U);
    EXPECT_EQ(diagnostic[0].code,
              openmeta::ReadTransferSourceDiagnosticCode::IncompleteMakerNote);
    EXPECT_EQ(diagnostic[0].count, 3U);
}

}  // namespace
