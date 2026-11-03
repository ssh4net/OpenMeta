// SPDX-License-Identifier: Apache-2.0

#include "icc_authoring_test_fixture.h"
#include "metadata_container_test_fixture.h"

#include "openmeta/meta_flags.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/prepared_transfer_handoff.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    using container_test::make_jpeg_fixture;
    using container_test::make_tiff_fixture;
    using container_test::TiffFixtureVariant;

    static bool make_authored_icc_store(MetaStore* store)
    {
        if (!store) {
            return false;
        }
        test::IccAuthoringFixtureData data;
        const std::array<MetadataAuthoringEntry, 10U> entries
            = test::icc_authoring_fixture_entries(data);
        return create_metadata_store(entries, store).ok();
    }

    static bool bundle_has_route(const PreparedTransferBundle& bundle,
                                 std::string_view route) noexcept
    {
        for (const PreparedTransferBlock& block : bundle.blocks) {
            if (block.route == route) {
                return true;
            }
        }
        return false;
    }

    static void append_jpeg_segment(std::vector<std::byte>* jpeg,
                                    uint8_t marker,
                                    std::span<const std::byte> payload)
    {
        const uint16_t length = static_cast<uint16_t>(payload.size() + 2U);
        jpeg->push_back(std::byte { 0xFFU });
        jpeg->push_back(static_cast<std::byte>(marker));
        jpeg->push_back(
            static_cast<std::byte>(static_cast<uint8_t>(length >> 8U)));
        jpeg->push_back(
            static_cast<std::byte>(static_cast<uint8_t>(length & 0xFFU)));
        jpeg->insert(jpeg->end(), payload.begin(), payload.end());
    }

    static std::vector<std::byte>
    make_jpeg_with_icc(std::span<const std::byte> profile)
    {
        static constexpr std::array<std::byte, 12U> signature = {
            std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
            std::byte { '_' }, std::byte { 'P' }, std::byte { 'R' },
            std::byte { 'O' }, std::byte { 'F' }, std::byte { 'I' },
            std::byte { 'L' }, std::byte { 'E' }, std::byte { 0U },
        };
        std::vector<std::byte> payload(signature.begin(), signature.end());
        payload.push_back(std::byte { 1U });
        payload.push_back(std::byte { 1U });
        payload.insert(payload.end(), profile.begin(), profile.end());

        const std::vector<std::byte> fixture = make_jpeg_fixture().bytes;
        std::vector<std::byte> jpeg(fixture.begin(), fixture.begin() + 2);
        append_jpeg_segment(&jpeg, 0xE2U, payload);
        jpeg.insert(jpeg.end(), fixture.begin() + 2, fixture.end());
        return jpeg;
    }

    static void append_u32be(std::vector<std::byte>* bytes, uint32_t value)
    {
        bytes->push_back(static_cast<std::byte>(value >> 24U));
        bytes->push_back(static_cast<std::byte>(value >> 16U));
        bytes->push_back(static_cast<std::byte>(value >> 8U));
        bytes->push_back(static_cast<std::byte>(value));
    }

    static void append_jumbf_box(std::vector<std::byte>* bytes,
                                 std::array<char, 4U> type,
                                 std::span<const std::byte> payload)
    {
        append_u32be(bytes, static_cast<uint32_t>(payload.size() + 8U));
        for (const char character : type) {
            bytes->push_back(
                static_cast<std::byte>(static_cast<uint8_t>(character)));
        }
        bytes->insert(bytes->end(), payload.begin(), payload.end());
    }

    static std::vector<std::byte> make_c2pa_app11_payload()
    {
        constexpr std::array<std::byte, 4U> cbor = {
            std::byte { 0xA1U },
            std::byte { 0x61U },
            std::byte { 'a' },
            std::byte { 0x01U },
        };
        const std::array<std::byte, 5U> jumd_payload = {
            std::byte { 'c' }, std::byte { '2' }, std::byte { 'p' },
            std::byte { 'a' }, std::byte { 0U },
        };
        std::vector<std::byte> children;
        append_jumbf_box(&children, { 'j', 'u', 'm', 'd' }, jumd_payload);
        append_jumbf_box(&children, { 'c', 'b', 'o', 'r' }, cbor);
        std::vector<std::byte> root;
        append_jumbf_box(&root, { 'j', 'u', 'm', 'b' }, children);
        std::vector<std::byte> app11 = { std::byte { 'J' }, std::byte { 'P' },
                                         std::byte { 0U }, std::byte { 0U } };
        append_u32be(&app11, 1U);
        app11.insert(app11.end(), root.begin(), root.end());
        return app11;
    }

    static std::vector<std::byte>
    make_jpeg_with_c2pa(bool include_icc,
                        std::span<const std::byte> profile = {})
    {
        std::vector<std::byte> jpeg = { std::byte { 0xFFU },
                                        std::byte { 0xD8U } };
        if (include_icc) {
            static constexpr std::array<std::byte, 12U> signature = {
                std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
                std::byte { '_' }, std::byte { 'P' }, std::byte { 'R' },
                std::byte { 'O' }, std::byte { 'F' }, std::byte { 'I' },
                std::byte { 'L' }, std::byte { 'E' }, std::byte { 0U },
            };
            std::vector<std::byte> icc_payload(signature.begin(),
                                               signature.end());
            icc_payload.push_back(std::byte { 1U });
            icc_payload.push_back(std::byte { 1U });
            icc_payload.insert(icc_payload.end(), profile.begin(),
                               profile.end());
            append_jpeg_segment(&jpeg, 0xE2U, icc_payload);
        }
        const std::vector<std::byte> app11 = make_c2pa_app11_payload();
        append_jpeg_segment(&jpeg, 0xEBU, app11);
        constexpr std::array<std::byte, 6U> scan_header = {
            std::byte { 1U }, std::byte { 1U },    std::byte { 0U },
            std::byte { 0U }, std::byte { 0x3FU }, std::byte { 0U },
        };
        append_jpeg_segment(&jpeg, 0xDAU, scan_header);
        jpeg.insert(jpeg.end(), { std::byte { 0x31U }, std::byte { 0xFFU },
                                  std::byte { 0xD9U } });
        return jpeg;
    }

    static bool jpeg_has_c2pa_label(std::span<const std::byte> jpeg) noexcept
    {
        static constexpr std::array<std::byte, 5U> label = {
            std::byte { 'c' }, std::byte { '2' }, std::byte { 'p' },
            std::byte { 'a' }, std::byte { 0U },
        };
        return std::search(jpeg.begin(), jpeg.end(), label.begin(), label.end())
               != jpeg.end();
    }

    static bool jpeg_has_icc_signature(std::span<const std::byte> jpeg) noexcept
    {
        static constexpr std::array<std::byte, 12U> signature = {
            std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
            std::byte { '_' }, std::byte { 'P' }, std::byte { 'R' },
            std::byte { 'O' }, std::byte { 'F' }, std::byte { 'I' },
            std::byte { 'L' }, std::byte { 'E' }, std::byte { 0U },
        };
        return std::search(jpeg.begin(), jpeg.end(), signature.begin(),
                           signature.end())
               != jpeg.end();
    }

    static uint16_t read_u16_le(std::span<const std::byte> bytes,
                                size_t offset) noexcept
    {
        return static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset]))
               | static_cast<uint16_t>(
                   static_cast<uint16_t>(
                       std::to_integer<uint8_t>(bytes[offset + 1U]))
                   << 8U);
    }

    static uint32_t read_u32_le(std::span<const std::byte> bytes,
                                size_t offset) noexcept
    {
        return static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset]))
               | (static_cast<uint32_t>(
                      std::to_integer<uint8_t>(bytes[offset + 1U]))
                  << 8U)
               | (static_cast<uint32_t>(
                      std::to_integer<uint8_t>(bytes[offset + 2U]))
                  << 16U)
               | (static_cast<uint32_t>(
                      std::to_integer<uint8_t>(bytes[offset + 3U]))
                  << 24U);
    }

    static uint64_t read_tiff_uint(std::span<const std::byte> bytes,
                                   size_t offset, size_t width,
                                   bool big_endian) noexcept
    {
        if (width == 0U || width > sizeof(uint64_t) || offset > bytes.size()
            || width > bytes.size() - offset) {
            return std::numeric_limits<uint64_t>::max();
        }
        uint64_t value = 0U;
        for (size_t index = 0U; index < width; ++index) {
            const size_t byte_index = big_endian ? offset + index
                                                 : offset + width - 1U - index;
            value = (value << 8U) | std::to_integer<uint8_t>(bytes[byte_index]);
        }
        return value;
    }

    static void append_tiff_uint(std::vector<std::byte>* bytes, size_t width,
                                 uint64_t value, bool big_endian)
    {
        for (size_t index = 0U; index < width; ++index) {
            const size_t shift = big_endian ? (width - 1U - index) * 8U
                                            : index * 8U;
            bytes->push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
        }
    }

    static void write_tiff_uint(std::vector<std::byte>* bytes, size_t offset,
                                size_t width, uint64_t value, bool big_endian)
    {
        for (size_t index = 0U; index < width; ++index) {
            const size_t shift       = big_endian ? (width - 1U - index) * 8U
                                                  : index * 8U;
            (*bytes)[offset + index] = static_cast<std::byte>((value >> shift)
                                                              & 0xFFU);
        }
    }

    static size_t find_tiff_ifd0_tag_offset(std::span<const std::byte> tiff,
                                            TiffFixtureVariant variant,
                                            uint16_t wanted_tag) noexcept
    {
        const size_t header_size = variant.big_tiff ? 16U : 8U;
        if (tiff.size() < header_size) {
            return SIZE_MAX;
        }
        const size_t count_width = variant.big_tiff ? 8U : 2U;
        const size_t entry_width = variant.big_tiff ? 20U : 12U;
        const bool big_endian    = variant.big_endian;
        const uint64_t ifd_offset
            = variant.big_tiff ? read_tiff_uint(tiff, 8U, 8U, big_endian)
                               : read_tiff_uint(tiff, 4U, 4U, big_endian);
        if (ifd_offset > tiff.size()
            || ifd_offset > std::numeric_limits<size_t>::max()) {
            return SIZE_MAX;
        }
        const size_t ifd = static_cast<size_t>(ifd_offset);
        if (ifd > tiff.size() || count_width > tiff.size() - ifd) {
            return SIZE_MAX;
        }
        const uint64_t count    = read_tiff_uint(tiff, ifd, count_width,
                                                 big_endian);
        const size_t entries    = ifd + count_width;
        const size_t next_width = variant.big_tiff ? 8U : 4U;
        if (entries > tiff.size() || next_width > tiff.size() - entries) {
            return SIZE_MAX;
        }
        const size_t available = tiff.size() - entries - next_width;
        if (count > available / entry_width) {
            return SIZE_MAX;
        }
        for (uint64_t index = 0U; index < count; ++index) {
            const size_t entry = entries
                                 + static_cast<size_t>(index) * entry_width;
            if (read_tiff_uint(tiff, entry, 2U, big_endian) == wanted_tag) {
                return entry;
            }
        }
        return SIZE_MAX;
    }

    static bool has_tiff_ifd0_tag(std::span<const std::byte> tiff,
                                  TiffFixtureVariant variant,
                                  uint16_t tag) noexcept
    {
        return find_tiff_ifd0_tag_offset(tiff, variant, tag) != SIZE_MAX;
    }

    static std::vector<std::byte>
    make_tiff_with_dng_profile_tags(TiffFixtureVariant variant)
    {
        std::vector<std::byte> tiff    = make_tiff_fixture(variant);
        const size_t count_width       = variant.big_tiff ? 8U : 2U;
        const size_t entry_width       = variant.big_tiff ? 20U : 12U;
        const bool big_endian          = variant.big_endian;
        const size_t ifd_pointer       = variant.big_tiff ? 8U : 4U;
        const size_t ifd_pointer_width = variant.big_tiff ? 8U : 4U;
        const uint64_t ifd_offset
            = read_tiff_uint(tiff, ifd_pointer, ifd_pointer_width, big_endian);
        const size_t ifd            = static_cast<size_t>(ifd_offset);
        const uint64_t count        = read_tiff_uint(tiff, ifd, count_width,
                                                     big_endian);
        const size_t entries_offset = ifd + count_width;
        const size_t next_ifd_offset
            = entries_offset + static_cast<size_t>(count) * entry_width;
        const size_t next_ifd_width = variant.big_tiff ? 8U : 4U;
        if (next_ifd_offset > tiff.size()
            || next_ifd_width > tiff.size() - next_ifd_offset) {
            return {};
        }

        static constexpr std::array<std::byte, 4U> kAsShotProfile = {
            std::byte { 0xA1U },
            std::byte { 0xB2U },
            std::byte { 0xC3U },
            std::byte { 0xD4U },
        };
        static constexpr std::array<std::byte, 4U> kCurrentProfile = {
            std::byte { 0x11U },
            std::byte { 0x22U },
            std::byte { 0x33U },
            std::byte { 0x44U },
        };
        std::vector<std::byte> additional_entries;
        container_test::append_tiff_entry(&additional_entries, variant, 0xC68FU,
                                          1U, kAsShotProfile.size(),
                                          kAsShotProfile, 0U);
        container_test::append_tiff_entry(&additional_entries, variant, 0xC691U,
                                          1U, kCurrentProfile.size(),
                                          kCurrentProfile, 0U);

        const size_t strip_entry = find_tiff_ifd0_tag_offset(tiff, variant,
                                                             0x0111U);
        if (strip_entry == SIZE_MAX) {
            return {};
        }
        const size_t value_offset = strip_entry + (variant.big_tiff ? 12U : 8U);
        const uint64_t strip_offset = read_tiff_uint(tiff, value_offset, 4U,
                                                     big_endian);
        if (strip_offset == std::numeric_limits<uint64_t>::max()
            || additional_entries.size()
                   > std::numeric_limits<uint64_t>::max() - strip_offset) {
            return {};
        }
        write_tiff_uint(&tiff, value_offset, 4U,
                        strip_offset + additional_entries.size(), big_endian);
        tiff.insert(tiff.begin() + static_cast<std::ptrdiff_t>(next_ifd_offset),
                    additional_entries.begin(), additional_entries.end());
        write_tiff_uint(&tiff, ifd, count_width, count + 2U, big_endian);
        return tiff;
    }

    static std::vector<std::byte>
    make_tiff_ifd0_entry_table(uint64_t count, bool include_icc_tag,
                               TiffFixtureVariant variant)
    {
        std::vector<std::byte> tiff;
        const bool big_endian    = variant.big_endian;
        const size_t count_width = variant.big_tiff ? 8U : 2U;
        const size_t ifd_offset  = variant.big_tiff ? 16U : 8U;
        const size_t next_width  = variant.big_tiff ? 8U : 4U;
        tiff.push_back(big_endian ? std::byte { 'M' } : std::byte { 'I' });
        tiff.push_back(big_endian ? std::byte { 'M' } : std::byte { 'I' });
        append_tiff_uint(&tiff, 2U, variant.big_tiff ? 43U : 42U, big_endian);
        if (variant.big_tiff) {
            append_tiff_uint(&tiff, 2U, 8U, big_endian);
            append_tiff_uint(&tiff, 2U, 0U, big_endian);
            append_tiff_uint(&tiff, 8U, ifd_offset, big_endian);
        } else {
            append_tiff_uint(&tiff, 4U, ifd_offset, big_endian);
        }
        append_tiff_uint(&tiff, count_width, count, big_endian);
        for (uint64_t index = 0U; index < count; ++index) {
            const uint16_t tag = include_icc_tag && index + 1U == count
                                     ? 34675U
                                     : static_cast<uint16_t>(0xD000U + index);
            append_tiff_uint(&tiff, 2U, tag, big_endian);
            append_tiff_uint(&tiff, 2U, 1U, big_endian);
            append_tiff_uint(&tiff, variant.big_tiff ? 8U : 4U, 1U, big_endian);
            append_tiff_uint(&tiff, variant.big_tiff ? 8U : 4U, 0x7FU,
                             big_endian);
        }
        append_tiff_uint(&tiff, next_width, 0U, big_endian);
        return tiff;
    }

    static bool
    has_classic_little_endian_ifd0_tag(std::span<const std::byte> tiff,
                                       uint16_t tag) noexcept
    {
        if (tiff.size() < 8U || tiff[0U] != std::byte { 'I' }
            || tiff[1U] != std::byte { 'I' } || read_u16_le(tiff, 2U) != 42U) {
            return false;
        }
        const uint32_t ifd_offset = read_u32_le(tiff, 4U);
        if (ifd_offset > tiff.size()
            || 2U > tiff.size() - static_cast<size_t>(ifd_offset)) {
            return false;
        }
        const size_t ifd     = static_cast<size_t>(ifd_offset);
        const uint16_t count = read_u16_le(tiff, ifd);
        const size_t entries = ifd + 2U;
        if (static_cast<size_t>(count) > (tiff.size() - entries) / 12U) {
            return false;
        }
        for (uint16_t index = 0U; index < count; ++index) {
            if (read_u16_le(tiff, entries + static_cast<size_t>(index) * 12U)
                == tag) {
                return true;
            }
        }
        return false;
    }

    static PreparedTransferBundle
    make_icc_removal_bundle(TransferTargetFormat target)
    {
        PreparedTransferBundle bundle;
        bundle.target_format       = target;
        bundle.remove_existing_icc = true;
        return bundle;
    }

    class CountingTransferWriter final : public TransferByteWriter {
    public:
        TransferStatus write(std::span<const std::byte> bytes) noexcept override
        {
            ++calls;
            output.insert(output.end(), bytes.begin(), bytes.end());
            return TransferStatus::Ok;
        }

        uint32_t calls = 0U;
        std::vector<std::byte> output;
    };

    struct PackageReplayCapture final {
        TransferTargetFormat target = TransferTargetFormat::Jpeg;
        uint32_t expected_chunks    = 0U;
        uint32_t emitted_chunks     = 0U;
        bool began                  = false;
        bool ended                  = false;
        std::vector<std::byte> output;
    };

    static TransferStatus capture_package_begin(void* user,
                                                TransferTargetFormat target,
                                                uint32_t chunk_count) noexcept
    {
        if (!user) {
            return TransferStatus::InvalidArgument;
        }
        PackageReplayCapture* capture = static_cast<PackageReplayCapture*>(
            user);
        capture->target          = target;
        capture->expected_chunks = chunk_count;
        capture->began           = true;
        return TransferStatus::Ok;
    }

    static TransferStatus
    capture_package_chunk(void* user,
                          const PreparedTransferPackageView* view) noexcept
    {
        if (!user || !view) {
            return TransferStatus::InvalidArgument;
        }
        PackageReplayCapture* capture = static_cast<PackageReplayCapture*>(
            user);
        if (!capture->began || capture->ended
            || view->output_offset != capture->output.size()) {
            return TransferStatus::InvalidArgument;
        }
        capture->output.insert(capture->output.end(), view->bytes.begin(),
                               view->bytes.end());
        ++capture->emitted_chunks;
        return TransferStatus::Ok;
    }

    static TransferStatus
    capture_package_end(void* user, TransferTargetFormat target) noexcept
    {
        if (!user) {
            return TransferStatus::InvalidArgument;
        }
        PackageReplayCapture* capture = static_cast<PackageReplayCapture*>(
            user);
        if (!capture->began || target != capture->target
            || capture->emitted_chunks != capture->expected_chunks) {
            return TransferStatus::InvalidArgument;
        }
        capture->ended = true;
        return TransferStatus::Ok;
    }

    static PreparedTransferPackagePlan
    make_full_source_copy_plan(TransferTargetFormat target,
                               std::span<const std::byte> input)
    {
        PreparedTransferPackagePlan plan;
        plan.target_format = target;
        plan.input_size    = static_cast<uint64_t>(input.size());
        plan.output_size   = static_cast<uint64_t>(input.size());
        PreparedTransferPackageChunk source;
        source.kind          = TransferPackageChunkKind::SourceRange;
        source.output_offset = 0U;
        source.source_offset = 0U;
        source.size          = static_cast<uint64_t>(input.size());
        plan.chunks.push_back(std::move(source));
        return plan;
    }

    static PreparedTransferPackagePlan
    make_split_source_copy_plan(TransferTargetFormat target,
                                std::span<const std::byte> input,
                                uint64_t split_offset)
    {
        PreparedTransferPackagePlan plan;
        plan.target_format = target;
        plan.input_size    = static_cast<uint64_t>(input.size());
        plan.output_size   = static_cast<uint64_t>(input.size());
        const std::array<uint64_t, 2U> sizes = {
            split_offset,
            static_cast<uint64_t>(input.size()) - split_offset,
        };
        uint64_t output_offset = 0U;
        for (const uint64_t size : sizes) {
            PreparedTransferPackageChunk source;
            source.kind          = TransferPackageChunkKind::SourceRange;
            source.output_offset = output_offset;
            source.source_offset = output_offset;
            source.size          = size;
            plan.chunks.push_back(std::move(source));
            output_offset += size;
        }
        return plan;
    }

    static std::vector<std::byte>
    make_jpeg_with_leading_comment_markers(uint32_t comment_count)
    {
        std::vector<std::byte> jpeg = { std::byte { 0xFFU },
                                        std::byte { 0xD8U } };
        for (uint32_t index = 0U; index < comment_count; ++index) {
            append_jpeg_segment(&jpeg, 0xFEU, {});
        }
        constexpr std::array<std::byte, 6U> scan_header = {
            std::byte { 1U }, std::byte { 1U },    std::byte { 0U },
            std::byte { 0U }, std::byte { 0x3FU }, std::byte { 0U },
        };
        append_jpeg_segment(&jpeg, 0xDAU, scan_header);
        jpeg.insert(jpeg.end(), { std::byte { 0x31U }, std::byte { 0xFFU },
                                  std::byte { 0xD9U } });
        return jpeg;
    }

    static void
    expect_full_copy_package_rejected(std::span<const std::byte> input,
                                      const PreparedTransferBundle& removal)
    {
        const PreparedTransferPackagePlan plan
            = make_full_source_copy_plan(removal.target_format, input);
        PreparedTransferPackageBatch batch;
        batch.target_format = removal.target_format;
        batch.output_size   = 1U;
        PreparedTransferPackageBlob sentinel;
        sentinel.output_offset = 0U;
        sentinel.bytes         = { std::byte { 0xA5U } };
        batch.chunks.push_back(std::move(sentinel));

        const EmitTransferResult materialized
            = build_prepared_transfer_package_batch(input, removal, plan,
                                                    &batch);
        EXPECT_NE(materialized.status, TransferStatus::Ok);
        ASSERT_EQ(batch.output_size, 1U);
        ASSERT_EQ(batch.chunks.size(), 1U);
        EXPECT_EQ(batch.chunks.front().bytes,
                  (std::vector<std::byte> { std::byte { 0xA5U } }));

        CountingTransferWriter writer;
        writer.output = { std::byte { 0xA5U } };
        const EmitTransferResult written
            = write_prepared_transfer_package(input, removal, plan, writer);
        EXPECT_NE(written.status, TransferStatus::Ok);
        EXPECT_EQ(writer.calls, 0U);
        EXPECT_EQ(writer.output,
                  (std::vector<std::byte> { std::byte { 0xA5U } }));
    }

    static void expect_specialized_package_survives(
        std::span<const std::byte> input, const PreparedTransferBundle& removal,
        TiffFixtureVariant variant = TiffFixtureVariant {})
    {
        PreparedTransferPackagePlan package;
        std::vector<std::byte> edited;
        if (removal.target_format == TransferTargetFormat::Jpeg) {
            const JpegEditPlan edit = plan_prepared_bundle_jpeg_edit(input,
                                                                     removal);
            ASSERT_EQ(edit.status, TransferStatus::Ok) << edit.message;
            ASSERT_EQ(build_prepared_bundle_jpeg_package(input, removal, edit,
                                                         &package)
                          .status,
                      TransferStatus::Ok);
            ASSERT_EQ(apply_prepared_bundle_jpeg_edit(input, removal, edit,
                                                      &edited)
                          .status,
                      TransferStatus::Ok);
        } else {
            const TiffEditPlan edit = plan_prepared_bundle_tiff_edit(input,
                                                                     removal);
            ASSERT_EQ(edit.status, TransferStatus::Ok) << edit.message;
            ASSERT_EQ(build_prepared_bundle_tiff_package(input, removal, edit,
                                                         &package)
                          .status,
                      TransferStatus::Ok);
            ASSERT_EQ(apply_prepared_bundle_tiff_edit(input, removal, edit,
                                                      &edited)
                          .status,
                      TransferStatus::Ok);
        }

        PreparedTransferPackageBatch batch;
        ASSERT_EQ(build_prepared_transfer_package_batch(input, removal, package,
                                                        &batch)
                      .status,
                  TransferStatus::Ok);
        std::vector<std::byte> encoded_batch;
        ASSERT_EQ(serialize_prepared_transfer_package_batch(batch,
                                                            &encoded_batch)
                      .status,
                  TransferStatus::Ok);
        PreparedTransferPackageBatch decoded_batch;
        ASSERT_EQ(deserialize_prepared_transfer_package_batch(encoded_batch,
                                                              &decoded_batch)
                      .status,
                  TransferStatus::Ok);
        PackageReplayCapture replay_capture;
        PreparedTransferPackageReplayCallbacks replay_callbacks;
        replay_callbacks.begin_batch = capture_package_begin;
        replay_callbacks.emit_chunk  = capture_package_chunk;
        replay_callbacks.end_batch   = capture_package_end;
        replay_callbacks.user        = &replay_capture;
        const PreparedTransferPackageReplayResult replay
            = replay_prepared_transfer_package_batch(decoded_batch,
                                                     replay_callbacks);
        ASSERT_EQ(replay.status, TransferStatus::Ok) << replay.message;
        EXPECT_TRUE(replay_capture.began);
        EXPECT_TRUE(replay_capture.ended);
        EXPECT_EQ(replay_capture.emitted_chunks,
                  replay_capture.expected_chunks);

        CountingTransferWriter writer;
        const EmitTransferResult written
            = write_prepared_transfer_package(input, removal, package, writer);
        ASSERT_EQ(written.status, TransferStatus::Ok) << written.message;
        EXPECT_GT(writer.calls, 0U);
        EXPECT_EQ(writer.output, edited);
        EXPECT_EQ(replay_capture.output, edited);
        if (removal.target_format == TransferTargetFormat::Jpeg) {
            EXPECT_FALSE(jpeg_has_icc_signature(writer.output));
        } else {
            EXPECT_FALSE(has_tiff_ifd0_tag(writer.output, variant, 34675U));
        }
    }

}  // namespace

TEST(MetadataIccRemoval, RequestOverridesSourceProfileAndDoesNotInferIntent)
{
    MetaStore source;
    ASSERT_TRUE(make_authored_icc_store(&source));
    const size_t original_entry_count = source.entries().size();

    PrepareTransferRequest request;
    request.target_format = TransferTargetFormat::Jpeg;
    PreparedTransferBundle emitted;
    ASSERT_EQ(prepare_metadata_for_target(source, request, &emitted).status,
              TransferStatus::Ok);
    EXPECT_FALSE(emitted.remove_existing_icc);
    EXPECT_TRUE(bundle_has_route(emitted, "jpeg:app2-icc"));

    request.remove_existing_icc = true;
    PreparedTransferBundle removed;
    ASSERT_EQ(prepare_metadata_for_target(source, request, &removed).status,
              TransferStatus::Ok);
    EXPECT_TRUE(removed.remove_existing_icc);
    EXPECT_FALSE(bundle_has_route(removed, "jpeg:app2-icc"));

    request.include_icc_app2 = false;
    PreparedTransferBundle disabled;
    ASSERT_EQ(prepare_metadata_for_target(source, request, &disabled).status,
              TransferStatus::Ok);
    EXPECT_TRUE(disabled.remove_existing_icc);
    EXPECT_FALSE(bundle_has_route(disabled, "jpeg:app2-icc"));
    EXPECT_EQ(source.entries().size(), original_entry_count);

    MetaStore last_tag;
    Entry deleted;
    deleted.key = make_icc_tag_key(0x64657363U);
    static constexpr std::array<std::byte, 4U> empty_tag = {
        std::byte { 't' },
        std::byte { 'e' },
        std::byte { 's' },
        std::byte { 't' },
    };
    deleted.value = make_bytes(last_tag.arena(), empty_tag);
    deleted.flags = EntryFlags::Dirty | EntryFlags::Deleted;
    ASSERT_NE(last_tag.add_entry(deleted), kInvalidEntryId);
    last_tag.finalize();
    request.remove_existing_icc = false;
    PreparedTransferBundle last_tag_bundle;
    (void)prepare_metadata_for_target(last_tag, request, &last_tag_bundle);
    EXPECT_FALSE(last_tag_bundle.remove_existing_icc);

    request.target_format       = TransferTargetFormat::Png;
    request.remove_existing_icc = true;
    PreparedTransferBundle unsupported;
    EXPECT_EQ(prepare_metadata_for_target(source, request, &unsupported).status,
              TransferStatus::Unsupported);
}

TEST(MetadataIccRemoval, EmptySourceStillPreparesExplicitRemovalForAllTargets)
{
    MetaStore empty;
    empty.finalize();
    static constexpr std::array<TransferTargetFormat, 3U> kTargets = {
        TransferTargetFormat::Jpeg,
        TransferTargetFormat::Tiff,
        TransferTargetFormat::Dng,
    };
    for (const TransferTargetFormat target : kTargets) {
        PrepareTransferRequest request;
        request.target_format       = target;
        request.remove_existing_icc = true;
        request.include_exif_app1   = false;
        request.include_xmp_app1    = false;
        request.include_icc_app2    = false;
        request.include_iptc_app13  = false;
        PreparedTransferBundle bundle;
        const PrepareTransferResult result
            = prepare_metadata_for_target(empty, request, &bundle);
        ASSERT_EQ(result.status, TransferStatus::Ok) << result.message;
        EXPECT_TRUE(bundle.remove_existing_icc);
        EXPECT_TRUE(bundle.blocks.empty());
    }
}

TEST(MetadataIccRemoval, JPEGEditsOnlyLeadingIccSegmentsAndPreservesNoop)
{
    MetaStore empty;
    empty.finalize();
    PrepareTransferRequest omitted_request;
    omitted_request.target_format      = TransferTargetFormat::Jpeg;
    omitted_request.include_exif_app1  = false;
    omitted_request.include_xmp_app1   = false;
    omitted_request.include_icc_app2   = false;
    omitted_request.include_iptc_app13 = false;
    PreparedTransferBundle omitted;
    ASSERT_EQ(
        prepare_metadata_for_target(empty, omitted_request, &omitted).status,
        TransferStatus::Ok);
    EXPECT_FALSE(omitted.remove_existing_icc);
    EXPECT_TRUE(omitted.blocks.empty());

    const std::vector<std::byte> source_profile
        = { std::byte { 1U }, std::byte { 2U }, std::byte { 3U } };
    const std::vector<std::byte> input = make_jpeg_with_icc(source_profile);
    ASSERT_TRUE(jpeg_has_icc_signature(input));
    const JpegEditPlan omitted_plan = plan_prepared_bundle_jpeg_edit(input,
                                                                     omitted);
    ASSERT_EQ(omitted_plan.status, TransferStatus::Ok) << omitted_plan.message;
    std::vector<std::byte> omitted_output;
    ASSERT_EQ(apply_prepared_bundle_jpeg_edit(input, omitted, omitted_plan,
                                              &omitted_output)
                  .status,
              TransferStatus::Ok);
    EXPECT_EQ(omitted_output, input);

    const PreparedTransferBundle removal = make_icc_removal_bundle(
        TransferTargetFormat::Jpeg);
    const JpegEditPlan plan = plan_prepared_bundle_jpeg_edit(input, removal);
    ASSERT_EQ(plan.status, TransferStatus::Ok) << plan.message;
    std::vector<std::byte> alias = input;
    const EmitTransferResult applied
        = apply_prepared_bundle_jpeg_edit(alias, removal, plan, &alias);
    ASSERT_EQ(applied.status, TransferStatus::Ok) << applied.message;
    EXPECT_FALSE(jpeg_has_icc_signature(alias));
    EXPECT_TRUE(jpeg_has_icc_signature(input));

    const std::vector<std::byte> no_icc = make_jpeg_fixture().bytes;
    const JpegEditPlan no_op_plan       = plan_prepared_bundle_jpeg_edit(no_icc,
                                                                         removal);
    ASSERT_EQ(no_op_plan.status, TransferStatus::Ok) << no_op_plan.message;
    std::vector<std::byte> no_op_output;
    ASSERT_EQ(apply_prepared_bundle_jpeg_edit(no_icc, removal, no_op_plan,
                                              &no_op_output)
                  .status,
              TransferStatus::Ok);
    EXPECT_EQ(no_op_output, no_icc);
}

TEST(MetadataIccRemoval, SnapshotPreparationSuppressesRawSourceProfile)
{
    MetaStore source;
    ASSERT_TRUE(make_authored_icc_store(&source));
    PrepareTransferRequest source_request;
    source_request.target_format = TransferTargetFormat::Jpeg;
    PreparedTransferBundle source_bundle;
    ASSERT_EQ(prepare_metadata_for_target(source, source_request, &source_bundle)
                  .status,
              TransferStatus::Ok);
    const PreparedTransferBlock* source_icc = nullptr;
    for (const PreparedTransferBlock& block : source_bundle.blocks) {
        if (block.route == "jpeg:app2-icc") {
            source_icc = &block;
            break;
        }
    }
    ASSERT_NE(source_icc, nullptr);
    ASSERT_GE(source_icc->payload.size(), 14U);

    const std::vector<std::byte> jpeg = make_jpeg_with_icc(
        std::span<const std::byte>(source_icc->payload.data() + 14U,
                                   source_icc->payload.size() - 14U));
    ReadTransferSourceSnapshotOptions read_options;
    read_options.preserve_raw_carriers = true;
    const ReadTransferSourceSnapshotBytesResult read
        = read_transfer_source_snapshot_bytes(jpeg, read_options);
    ASSERT_EQ(read.status, TransferStatus::Ok);

    PrepareTransferRequest request;
    request.target_format = TransferTargetFormat::Jpeg;
    request.raw_carrier_passthrough_mode
        = TransferRawCarrierPassthroughMode::WhenSafe;
    request.remove_existing_icc = true;
    PreparedTransferBundle prepared;
    ASSERT_EQ(prepare_metadata_for_target_snapshot(read.snapshot, request,
                                                   &prepared)
                  .status,
              TransferStatus::Ok);
    EXPECT_TRUE(prepared.remove_existing_icc);
    EXPECT_FALSE(bundle_has_route(prepared, "jpeg:app2-icc"));
}

TEST(MetadataIccRemoval, TiffRemovalChangesOnlyPrimaryCarrierAndNoCarrierIsNoop)
{
    MetaStore source;
    ASSERT_TRUE(make_authored_icc_store(&source));
    PrepareTransferRequest request;
    request.target_format = TransferTargetFormat::Tiff;
    PreparedTransferBundle emit;
    ASSERT_EQ(prepare_metadata_for_target(source, request, &emit).status,
              TransferStatus::Ok);
    ASSERT_TRUE(bundle_has_route(emit, "tiff:tag-34675-icc"));

    const TiffFixtureVariant variant {};
    const std::vector<std::byte> plain = make_tiff_fixture(variant);
    const TiffEditPlan emit_plan = plan_prepared_bundle_tiff_edit(plain, emit);
    ASSERT_EQ(emit_plan.status, TransferStatus::Ok) << emit_plan.message;
    std::vector<std::byte> with_icc;
    ASSERT_EQ(apply_prepared_bundle_tiff_edit(plain, emit, emit_plan, &with_icc)
                  .status,
              TransferStatus::Ok);
    ASSERT_TRUE(has_classic_little_endian_ifd0_tag(with_icc, 34675U));

    request.remove_existing_icc = true;
    PreparedTransferBundle remove;
    ASSERT_EQ(prepare_metadata_for_target(source, request, &remove).status,
              TransferStatus::Ok);
    EXPECT_TRUE(remove.remove_existing_icc);
    EXPECT_FALSE(bundle_has_route(remove, "tiff:tag-34675-icc"));
    const TiffEditPlan remove_plan = plan_prepared_bundle_tiff_edit(with_icc,
                                                                    remove);
    ASSERT_EQ(remove_plan.status, TransferStatus::Ok) << remove_plan.message;
    std::vector<std::byte> without_icc;
    ASSERT_EQ(apply_prepared_bundle_tiff_edit(with_icc, remove, remove_plan,
                                              &without_icc)
                  .status,
              TransferStatus::Ok);
    EXPECT_FALSE(has_classic_little_endian_ifd0_tag(without_icc, 34675U));

    std::vector<std::byte> alias = with_icc;
    ASSERT_EQ(apply_prepared_bundle_tiff_edit(alias, remove, remove_plan, &alias)
                  .status,
              TransferStatus::Ok);
    EXPECT_FALSE(has_classic_little_endian_ifd0_tag(alias, 34675U));
    EXPECT_TRUE(has_classic_little_endian_ifd0_tag(with_icc, 34675U));

    const PreparedTransferBundle no_op_removal = make_icc_removal_bundle(
        TransferTargetFormat::Tiff);
    PlanTiffEditOptions no_op_options;
    no_op_options.require_updates = false;
    const TiffEditPlan no_op_plan
        = plan_prepared_bundle_tiff_edit(plain, no_op_removal, no_op_options);
    ASSERT_EQ(no_op_plan.status, TransferStatus::Ok) << no_op_plan.message;
    std::vector<std::byte> no_op_output;
    ASSERT_EQ(apply_prepared_bundle_tiff_edit(plain, no_op_removal, no_op_plan,
                                              &no_op_output)
                  .status,
              TransferStatus::Ok);
    EXPECT_EQ(no_op_output, plain);
}

TEST(MetadataIccRemoval, TiffClassicBigEndianBigTiffAndDngRemoveOnlyPrimaryIcc)
{
    static constexpr std::array<TiffFixtureVariant, 6U> kVariants = {
        TiffFixtureVariant { false, false, false },
        TiffFixtureVariant { false, true, false },
        TiffFixtureVariant { true, false, false },
        TiffFixtureVariant { true, true, false },
        TiffFixtureVariant { false, false, true },
        TiffFixtureVariant { true, true, true },
    };
    MetaStore source;
    ASSERT_TRUE(make_authored_icc_store(&source));

    for (const TiffFixtureVariant variant : kVariants) {
        const TransferTargetFormat target = variant.dng
                                                ? TransferTargetFormat::Dng
                                                : TransferTargetFormat::Tiff;
        std::vector<std::byte> plain      = make_tiff_fixture(variant);
        if (variant.dng) {
            plain = make_tiff_with_dng_profile_tags(variant);
            ASSERT_FALSE(plain.empty());
            ASSERT_TRUE(has_tiff_ifd0_tag(plain, variant, 0xC68FU));
            ASSERT_TRUE(has_tiff_ifd0_tag(plain, variant, 0xC691U));
        }

        PrepareTransferRequest emit_request;
        emit_request.target_format = target;
        PreparedTransferBundle emit;
        ASSERT_EQ(
            prepare_metadata_for_target(source, emit_request, &emit).status,
            TransferStatus::Ok)
            << "target=" << static_cast<unsigned>(target);
        ASSERT_TRUE(bundle_has_route(emit, "tiff:tag-34675-icc"));

        const TiffEditPlan emit_plan = plan_prepared_bundle_tiff_edit(plain,
                                                                      emit);
        ASSERT_EQ(emit_plan.status, TransferStatus::Ok) << emit_plan.message;
        std::vector<std::byte> with_icc;
        ASSERT_EQ(apply_prepared_bundle_tiff_edit(plain, emit, emit_plan,
                                                  &with_icc)
                      .status,
                  TransferStatus::Ok);
        ASSERT_TRUE(has_tiff_ifd0_tag(with_icc, variant, 34675U));

        PrepareTransferRequest remove_request = emit_request;
        remove_request.remove_existing_icc    = true;
        PreparedTransferBundle remove;
        ASSERT_EQ(
            prepare_metadata_for_target(source, remove_request, &remove).status,
            TransferStatus::Ok);
        ASSERT_TRUE(remove.remove_existing_icc);
        EXPECT_FALSE(bundle_has_route(remove, "tiff:tag-34675-icc"));

        const TiffEditPlan remove_plan
            = plan_prepared_bundle_tiff_edit(with_icc, remove);
        ASSERT_EQ(remove_plan.status, TransferStatus::Ok)
            << remove_plan.message;
        std::vector<std::byte> without_icc;
        ASSERT_EQ(apply_prepared_bundle_tiff_edit(with_icc, remove, remove_plan,
                                                  &without_icc)
                      .status,
                  TransferStatus::Ok);
        EXPECT_FALSE(has_tiff_ifd0_tag(without_icc, variant, 34675U));
        if (variant.dng) {
            EXPECT_TRUE(has_tiff_ifd0_tag(without_icc, variant, 0xC68FU));
            EXPECT_TRUE(has_tiff_ifd0_tag(without_icc, variant, 0xC691U));
            const size_t as_shot = find_tiff_ifd0_tag_offset(without_icc,
                                                             variant, 0xC68FU);
            const size_t current = find_tiff_ifd0_tag_offset(without_icc,
                                                             variant, 0xC691U);
            ASSERT_NE(as_shot, SIZE_MAX);
            ASSERT_NE(current, SIZE_MAX);
            const size_t value_delta = variant.big_tiff ? 12U : 8U;
            static constexpr std::array<std::byte, 4U> kAsShotProfile = {
                std::byte { 0xA1U },
                std::byte { 0xB2U },
                std::byte { 0xC3U },
                std::byte { 0xD4U },
            };
            static constexpr std::array<std::byte, 4U> kCurrentProfile = {
                std::byte { 0x11U },
                std::byte { 0x22U },
                std::byte { 0x33U },
                std::byte { 0x44U },
            };
            ASSERT_LE(as_shot + value_delta + kAsShotProfile.size(),
                      without_icc.size());
            ASSERT_LE(current + value_delta + kCurrentProfile.size(),
                      without_icc.size());
            EXPECT_TRUE(std::equal(kAsShotProfile.begin(), kAsShotProfile.end(),
                                   without_icc.begin()
                                       + static_cast<std::ptrdiff_t>(
                                           as_shot + value_delta)));
            EXPECT_TRUE(std::equal(kCurrentProfile.begin(),
                                   kCurrentProfile.end(),
                                   without_icc.begin()
                                       + static_cast<std::ptrdiff_t>(
                                           current + value_delta)));
        }
        EXPECT_TRUE(has_tiff_ifd0_tag(with_icc, variant, 34675U));
        expect_specialized_package_survives(with_icc, remove, variant);
    }
}

TEST(MetadataIccRemoval, PackageReplayCannotRetainPrimaryIcc)
{
    MetaStore source;
    ASSERT_TRUE(make_authored_icc_store(&source));

    PrepareTransferRequest jpeg_emit_request;
    jpeg_emit_request.target_format = TransferTargetFormat::Jpeg;
    PreparedTransferBundle jpeg_emit;
    ASSERT_EQ(prepare_metadata_for_target(source, jpeg_emit_request, &jpeg_emit)
                  .status,
              TransferStatus::Ok);
    const PreparedTransferBlock* jpeg_icc = nullptr;
    for (const PreparedTransferBlock& block : jpeg_emit.blocks) {
        if (block.route == "jpeg:app2-icc") {
            jpeg_icc = &block;
            break;
        }
    }
    ASSERT_NE(jpeg_icc, nullptr);
    ASSERT_GE(jpeg_icc->payload.size(), 14U);
    const std::vector<std::byte> jpeg = make_jpeg_with_icc(
        std::span<const std::byte>(jpeg_icc->payload.data() + 14U,
                                   jpeg_icc->payload.size() - 14U));

    PreparedTransferBundle stale_jpeg;
    stale_jpeg.target_format       = TransferTargetFormat::Jpeg;
    stale_jpeg.remove_existing_icc = false;
    const PreparedTransferPackagePlan stale_jpeg_plan
        = make_full_source_copy_plan(TransferTargetFormat::Jpeg, jpeg);
    stale_jpeg.remove_existing_icc = true;
    CountingTransferWriter stale_jpeg_writer;
    const EmitTransferResult stale_jpeg_result
        = write_prepared_transfer_package(jpeg, stale_jpeg, stale_jpeg_plan,
                                          stale_jpeg_writer);
    EXPECT_NE(stale_jpeg_result.status, TransferStatus::Ok);
    EXPECT_EQ(stale_jpeg_writer.calls, 0U);

    PrepareTransferRequest jpeg_remove_request = jpeg_emit_request;
    jpeg_remove_request.remove_existing_icc    = true;
    PreparedTransferBundle jpeg_remove;
    ASSERT_EQ(prepare_metadata_for_target(source, jpeg_remove_request,
                                          &jpeg_remove)
                  .status,
              TransferStatus::Ok);
    expect_full_copy_package_rejected(jpeg, jpeg_remove);
    expect_specialized_package_survives(jpeg, jpeg_remove);

    PrepareTransferRequest tiff_emit_request;
    tiff_emit_request.target_format = TransferTargetFormat::Tiff;
    PreparedTransferBundle tiff_emit;
    ASSERT_EQ(prepare_metadata_for_target(source, tiff_emit_request, &tiff_emit)
                  .status,
              TransferStatus::Ok);
    const TiffFixtureVariant variant {};
    const std::vector<std::byte> plain_tiff = make_tiff_fixture(variant);
    const TiffEditPlan tiff_emit_plan
        = plan_prepared_bundle_tiff_edit(plain_tiff, tiff_emit);
    ASSERT_EQ(tiff_emit_plan.status, TransferStatus::Ok)
        << tiff_emit_plan.message;
    std::vector<std::byte> tiff;
    ASSERT_EQ(apply_prepared_bundle_tiff_edit(plain_tiff, tiff_emit,
                                              tiff_emit_plan, &tiff)
                  .status,
              TransferStatus::Ok);
    ASSERT_TRUE(has_classic_little_endian_ifd0_tag(tiff, 34675U));

    const PreparedTransferPackagePlan stale_tiff_plan
        = make_full_source_copy_plan(TransferTargetFormat::Tiff, tiff);
    PreparedTransferBundle stale_tiff;
    stale_tiff.target_format       = TransferTargetFormat::Tiff;
    stale_tiff.remove_existing_icc = false;
    stale_tiff.remove_existing_icc = true;
    CountingTransferWriter stale_tiff_writer;
    const EmitTransferResult stale_tiff_result
        = write_prepared_transfer_package(tiff, stale_tiff, stale_tiff_plan,
                                          stale_tiff_writer);
    EXPECT_NE(stale_tiff_result.status, TransferStatus::Ok);
    EXPECT_EQ(stale_tiff_writer.calls, 0U);

    PrepareTransferRequest tiff_remove_request = tiff_emit_request;
    tiff_remove_request.remove_existing_icc    = true;
    PreparedTransferBundle tiff_remove;
    ASSERT_EQ(prepare_metadata_for_target(source, tiff_remove_request,
                                          &tiff_remove)
                  .status,
              TransferStatus::Ok);
    expect_full_copy_package_rejected(tiff, tiff_remove);
    expect_specialized_package_survives(tiff, tiff_remove);
}

TEST(MetadataIccRemoval, VirtualPackageScannerReadsAcrossChunkBoundaries)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_icc(
        std::array<std::byte, 3U> {
            std::byte { 0x61U },
            std::byte { 0x62U },
            std::byte { 0x63U },
        });
    const PreparedTransferBundle jpeg_remove = make_icc_removal_bundle(
        TransferTargetFormat::Jpeg);
    const PreparedTransferPackagePlan jpeg_split
        = make_split_source_copy_plan(TransferTargetFormat::Jpeg, jpeg, 11U);
    PreparedTransferPackageBatch jpeg_batch;
    const EmitTransferResult jpeg_result
        = build_prepared_transfer_package_batch(jpeg, jpeg_remove, jpeg_split,
                                                &jpeg_batch);
    EXPECT_EQ(jpeg_result.status, TransferStatus::InvalidArgument);
    EXPECT_NE(jpeg_result.message.find("retains the primary ICC"),
              std::string::npos);

    const TiffFixtureVariant variant {};
    MetaStore source;
    ASSERT_TRUE(make_authored_icc_store(&source));
    PrepareTransferRequest emit_request;
    emit_request.target_format = TransferTargetFormat::Tiff;
    PreparedTransferBundle emit;
    ASSERT_EQ(prepare_metadata_for_target(source, emit_request, &emit).status,
              TransferStatus::Ok);
    const std::vector<std::byte> plain = make_tiff_fixture(variant);
    const TiffEditPlan edit = plan_prepared_bundle_tiff_edit(plain, emit);
    ASSERT_EQ(edit.status, TransferStatus::Ok) << edit.message;
    std::vector<std::byte> tiff;
    ASSERT_EQ(apply_prepared_bundle_tiff_edit(plain, emit, edit, &tiff).status,
              TransferStatus::Ok);
    const size_t icc_entry = find_tiff_ifd0_tag_offset(tiff, variant, 34675U);
    ASSERT_NE(icc_entry, SIZE_MAX);
    const PreparedTransferBundle tiff_remove = make_icc_removal_bundle(
        TransferTargetFormat::Tiff);
    const PreparedTransferPackagePlan tiff_split
        = make_split_source_copy_plan(TransferTargetFormat::Tiff, tiff,
                                      icc_entry + 1U);
    PreparedTransferPackageBatch tiff_batch;
    const EmitTransferResult tiff_result
        = build_prepared_transfer_package_batch(tiff, tiff_remove, tiff_split,
                                                &tiff_batch);
    EXPECT_EQ(tiff_result.status, TransferStatus::InvalidArgument);
    EXPECT_NE(tiff_result.message.find("retains the primary ICC"),
              std::string::npos);
}

TEST(MetadataIccRemoval, MalformedAndOverLimitVirtualPackagesFailBoundedly)
{
    const PreparedTransferBundle jpeg_remove = make_icc_removal_bundle(
        TransferTargetFormat::Jpeg);
    const std::vector<std::byte> malformed_jpeg = {
        std::byte { 0xFFU },
        std::byte { 0xD8U },
        std::byte { 0xFFU },
    };
    const PreparedTransferPackagePlan malformed_jpeg_plan
        = make_full_source_copy_plan(TransferTargetFormat::Jpeg,
                                     malformed_jpeg);
    PreparedTransferPackageBatch untouched_jpeg_batch;
    untouched_jpeg_batch.output_size = 1U;
    PreparedTransferPackageBlob jpeg_sentinel;
    jpeg_sentinel.bytes = { std::byte { 0xA5U } };
    untouched_jpeg_batch.chunks.push_back(std::move(jpeg_sentinel));
    const EmitTransferResult malformed_jpeg_result
        = build_prepared_transfer_package_batch(malformed_jpeg, jpeg_remove,
                                                malformed_jpeg_plan,
                                                &untouched_jpeg_batch);
    EXPECT_EQ(malformed_jpeg_result.status, TransferStatus::Malformed);
    ASSERT_EQ(untouched_jpeg_batch.output_size, 1U);
    ASSERT_EQ(untouched_jpeg_batch.chunks.size(), 1U);
    EXPECT_EQ(untouched_jpeg_batch.chunks.front().bytes,
              (std::vector<std::byte> { std::byte { 0xA5U } }));

    const PreparedTransferBundle tiff_remove = make_icc_removal_bundle(
        TransferTargetFormat::Tiff);
    const std::vector<std::byte> malformed_tiff = {
        std::byte { 'I' },   std::byte { 'I' },   std::byte { 42U },
        std::byte { 0U },    std::byte { 0xF0U }, std::byte { 0xFFU },
        std::byte { 0xFFU }, std::byte { 0xFFU },
    };
    const PreparedTransferPackagePlan malformed_tiff_plan
        = make_full_source_copy_plan(TransferTargetFormat::Tiff,
                                     malformed_tiff);
    const EmitTransferResult malformed_tiff_result
        = build_prepared_transfer_package_batch(malformed_tiff, tiff_remove,
                                                malformed_tiff_plan,
                                                &untouched_jpeg_batch);
    EXPECT_EQ(malformed_tiff_result.status, TransferStatus::Malformed);
    ASSERT_EQ(untouched_jpeg_batch.output_size, 1U);
    ASSERT_EQ(untouched_jpeg_batch.chunks.size(), 1U);
    EXPECT_EQ(untouched_jpeg_batch.chunks.front().bytes,
              (std::vector<std::byte> { std::byte { 0xA5U } }));

    const std::vector<std::byte> jpeg_at_limit
        = make_jpeg_with_leading_comment_markers(4095U);
    const PreparedTransferPackagePlan jpeg_at_limit_plan
        = make_full_source_copy_plan(TransferTargetFormat::Jpeg, jpeg_at_limit);
    PreparedTransferPackageBatch accepted_jpeg_batch;
    EXPECT_EQ(build_prepared_transfer_package_batch(jpeg_at_limit, jpeg_remove,
                                                    jpeg_at_limit_plan,
                                                    &accepted_jpeg_batch)
                  .status,
              TransferStatus::Ok);
    const std::vector<std::byte> jpeg_over_limit
        = make_jpeg_with_leading_comment_markers(4096U);
    const PreparedTransferPackagePlan jpeg_over_limit_plan
        = make_full_source_copy_plan(TransferTargetFormat::Jpeg,
                                     jpeg_over_limit);
    EXPECT_EQ(build_prepared_transfer_package_batch(jpeg_over_limit,
                                                    jpeg_remove,
                                                    jpeg_over_limit_plan,
                                                    &accepted_jpeg_batch)
                  .status,
              TransferStatus::LimitExceeded);

    const TiffFixtureVariant classic {};
    const std::vector<std::byte> tiff_at_limit
        = make_tiff_ifd0_entry_table(4096U, false, classic);
    ASSERT_FALSE(has_tiff_ifd0_tag(tiff_at_limit, classic, 34675U));
    const PreparedTransferPackagePlan tiff_at_limit_plan
        = make_full_source_copy_plan(TransferTargetFormat::Tiff, tiff_at_limit);
    PreparedTransferPackageBatch accepted_tiff_batch;
    const EmitTransferResult tiff_at_limit_result
        = build_prepared_transfer_package_batch(tiff_at_limit, tiff_remove,
                                                tiff_at_limit_plan,
                                                &accepted_tiff_batch);
    EXPECT_EQ(tiff_at_limit_result.status, TransferStatus::Ok)
        << tiff_at_limit_result.message;
    const std::vector<std::byte> tiff_over_limit
        = make_tiff_ifd0_entry_table(4097U, false, classic);
    ASSERT_FALSE(has_tiff_ifd0_tag(tiff_over_limit, classic, 34675U));
    const PreparedTransferPackagePlan tiff_over_limit_plan
        = make_full_source_copy_plan(TransferTargetFormat::Tiff,
                                     tiff_over_limit);
    EXPECT_EQ(build_prepared_transfer_package_batch(tiff_over_limit,
                                                    tiff_remove,
                                                    tiff_over_limit_plan,
                                                    &accepted_tiff_batch)
                  .status,
              TransferStatus::LimitExceeded);

    const std::vector<std::byte> source_tiff_over_limit
        = make_tiff_ifd0_entry_table(4097U, true, classic);
    const TiffEditPlan source_tiff_limit_result
        = plan_prepared_bundle_tiff_edit(source_tiff_over_limit, tiff_remove);
    EXPECT_EQ(source_tiff_limit_result.status, TransferStatus::LimitExceeded);
    const std::vector<std::byte> source_tiff_at_limit
        = make_tiff_ifd0_entry_table(4096U, true, classic);
    const TiffEditPlan source_tiff_boundary_result
        = plan_prepared_bundle_tiff_edit(source_tiff_at_limit, tiff_remove);
    EXPECT_EQ(source_tiff_boundary_result.status, TransferStatus::Ok)
        << source_tiff_boundary_result.message;
}

TEST(MetadataIccRemoval,
     C2paBindingMatchesRemovedIccEditAndPackageAndTracksIntent)
{
    static constexpr std::array<std::byte, 3U> profile = {
        std::byte { 0x61U },
        std::byte { 0x62U },
        std::byte { 0x63U },
    };
    const std::vector<std::byte> input = make_jpeg_with_c2pa(true, profile);
    ASSERT_TRUE(jpeg_has_icc_signature(input));
    ASSERT_TRUE(jpeg_has_c2pa_label(input));
    const ReadTransferSourceSnapshotBytesResult snapshot
        = read_transfer_source_snapshot_bytes(input);
    ASSERT_EQ(snapshot.status, TransferStatus::Ok);

    PrepareTransferRequest request;
    request.target_format       = TransferTargetFormat::Jpeg;
    request.remove_existing_icc = true;
    request.profile.c2pa        = TransferPolicyAction::Rewrite;
    PreparedTransferC2paSigningContext context;
    const PrepareTransferC2paSigningContextResult prepared
        = prepare_c2pa_signing_context(snapshot.snapshot, input, request,
                                       &context);
    ASSERT_EQ(prepared.binding.status, TransferStatus::Ok)
        << prepared.binding.message;
    ASSERT_TRUE(context.bundle.remove_existing_icc);
    EXPECT_EQ(context.bundle.c2pa_rewrite.state,
              TransferC2paRewriteState::SigningMaterialRequired);
    EXPECT_FALSE(bundle_has_route(context.bundle, "jpeg:app2-icc"));
    EXPECT_FALSE(jpeg_has_icc_signature(context.handoff.binding_bytes));
    EXPECT_FALSE(jpeg_has_c2pa_label(context.handoff.binding_bytes));

    const JpegEditPlan edit = plan_prepared_bundle_jpeg_edit(input,
                                                             context.bundle);
    ASSERT_EQ(edit.status, TransferStatus::Ok) << edit.message;
    std::vector<std::byte> edited;
    ASSERT_EQ(apply_prepared_bundle_jpeg_edit(input, context.bundle, edit,
                                              &edited)
                  .status,
              TransferStatus::Ok);
    EXPECT_EQ(context.handoff.binding_bytes, edited);

    PreparedTransferPackagePlan package;
    ASSERT_EQ(build_prepared_bundle_jpeg_package(input, context.bundle, edit,
                                                 &package)
                  .status,
              TransferStatus::Ok);
    PreparedTransferPackageBatch batch;
    ASSERT_EQ(build_prepared_transfer_package_batch(input, context.bundle,
                                                    package, &batch)
                  .status,
              TransferStatus::Ok);
    std::vector<std::byte> encoded_batch;
    ASSERT_EQ(
        serialize_prepared_transfer_package_batch(batch, &encoded_batch).status,
        TransferStatus::Ok);
    PreparedTransferPackageBatch decoded_batch;
    ASSERT_EQ(deserialize_prepared_transfer_package_batch(encoded_batch,
                                                          &decoded_batch)
                  .status,
              TransferStatus::Ok);
    PackageReplayCapture replay_capture;
    PreparedTransferPackageReplayCallbacks replay_callbacks;
    replay_callbacks.begin_batch = capture_package_begin;
    replay_callbacks.emit_chunk  = capture_package_chunk;
    replay_callbacks.end_batch   = capture_package_end;
    replay_callbacks.user        = &replay_capture;
    const PreparedTransferPackageReplayResult replay
        = replay_prepared_transfer_package_batch(decoded_batch,
                                                 replay_callbacks);
    ASSERT_EQ(replay.status, TransferStatus::Ok) << replay.message;
    EXPECT_EQ(replay_capture.output, context.handoff.binding_bytes);
    EXPECT_EQ(replay_capture.output, edited);

    const BuildPreparedC2paBindingResult unchanged_validation
        = validate_prepared_c2pa_signing_context(context, input);
    EXPECT_EQ(unchanged_validation.status, TransferStatus::Ok)
        << unchanged_validation.message;
    PreparedTransferC2paSigningContext changed_intent = context;
    changed_intent.bundle.remove_existing_icc         = false;
    const BuildPreparedC2paBindingResult intent_validation
        = validate_prepared_c2pa_signing_context(changed_intent, input);
    EXPECT_EQ(intent_validation.status, TransferStatus::InvalidArgument);
    EXPECT_EQ(intent_validation.code, EmitTransferCode::PlanMismatch);

    std::vector<std::byte> changed_source = input;
    ASSERT_GT(changed_source.size(), 3U);
    changed_source[changed_source.size() - 3U] ^= std::byte { 0x01U };
    const BuildPreparedC2paBindingResult source_validation
        = validate_prepared_c2pa_signing_context(context, changed_source);
    EXPECT_EQ(source_validation.status, TransferStatus::InvalidArgument);
    EXPECT_EQ(source_validation.code, EmitTransferCode::PlanMismatch);
}

TEST(MetadataIccRemoval,
     ExplicitRemovalStillInvalidatesExistingC2paWhenIccIsAbsent)
{
    const std::vector<std::byte> input = make_jpeg_with_c2pa(false);
    ASSERT_FALSE(jpeg_has_icc_signature(input));
    ASSERT_TRUE(jpeg_has_c2pa_label(input));
    const ReadTransferSourceSnapshotBytesResult snapshot
        = read_transfer_source_snapshot_bytes(input);
    ASSERT_EQ(snapshot.status, TransferStatus::Ok);

    PrepareTransferRequest request;
    request.target_format       = TransferTargetFormat::Jpeg;
    request.remove_existing_icc = true;
    request.profile.c2pa        = TransferPolicyAction::Rewrite;
    PreparedTransferC2paSigningContext context;
    const PrepareTransferC2paSigningContextResult prepared
        = prepare_c2pa_signing_context(snapshot.snapshot, input, request,
                                       &context);
    ASSERT_EQ(prepared.binding.status, TransferStatus::Ok)
        << prepared.binding.message;
    EXPECT_TRUE(context.bundle.remove_existing_icc);
    EXPECT_EQ(context.bundle.c2pa_rewrite.state,
              TransferC2paRewriteState::SigningMaterialRequired);

    const JpegEditPlan edit = plan_prepared_bundle_jpeg_edit(input,
                                                             context.bundle);
    ASSERT_EQ(edit.status, TransferStatus::Ok) << edit.message;
    std::vector<std::byte> edited;
    ASSERT_EQ(apply_prepared_bundle_jpeg_edit(input, context.bundle, edit,
                                              &edited)
                  .status,
              TransferStatus::Ok);
    EXPECT_FALSE(jpeg_has_icc_signature(edited));
    EXPECT_FALSE(jpeg_has_c2pa_label(edited));
    EXPECT_NE(edited, input);
    EXPECT_EQ(context.handoff.binding_bytes, edited);
}

TEST(MetadataIccRemoval, ConflictsAndNonEditConsumersRejectBeforePublication)
{
    const std::vector<std::byte> jpeg  = make_jpeg_fixture().bytes;
    const PreparedTransferBundle valid = make_icc_removal_bundle(
        TransferTargetFormat::Jpeg);
    const JpegEditPlan valid_plan = plan_prepared_bundle_jpeg_edit(jpeg, valid);
    ASSERT_EQ(valid_plan.status, TransferStatus::Ok) << valid_plan.message;

    for (const bool route_conflict : { false, true }) {
        PreparedTransferBundle invalid = valid;
        PreparedTransferBlock block;
        block.kind  = route_conflict ? TransferBlockKind::Other
                                     : TransferBlockKind::Icc;
        block.route = route_conflict ? "jpeg:app2-icc" : "unknown";
        invalid.blocks.push_back(std::move(block));
        std::vector<std::byte> output = { std::byte { 0xA5U } };
        EXPECT_NE(plan_prepared_bundle_jpeg_edit(jpeg, invalid).status,
                  TransferStatus::Ok);
        EXPECT_NE(apply_prepared_bundle_jpeg_edit(jpeg, invalid, valid_plan,
                                                  &output)
                      .status,
                  TransferStatus::Ok);
        EXPECT_EQ(output, (std::vector<std::byte> { std::byte { 0xA5U } }));
    }

    PreparedTransferAdapterView adapter;
    EXPECT_EQ(build_prepared_transfer_adapter_view(valid, &adapter).status,
              TransferStatus::Unsupported);
    PreparedTransferPayloadBatch payload;
    EXPECT_EQ(build_prepared_transfer_payload_batch(valid, &payload).status,
              TransferStatus::Unsupported);
    PreparedTransferPackagePlan fresh;
    EXPECT_EQ(build_prepared_transfer_emit_package(valid, &fresh).status,
              TransferStatus::Unsupported);

    const ReadTransferSourceSnapshotBytesResult snapshot
        = read_transfer_source_snapshot_bytes(jpeg);
    ASSERT_EQ(snapshot.status, TransferStatus::Ok);
    PrepareTransferRequest stable_request;
    stable_request.target_format       = TransferTargetFormat::Jpeg;
    stable_request.remove_existing_icc = true;
    PreparedTransferHandoff stable_handoff;
    const PreparedTransferHandoffResult stable_result
        = prepare_transfer_handoff(snapshot.snapshot, stable_request,
                                   EmitTransferOptions {}, &stable_handoff);
    EXPECT_EQ(stable_result.status, TransferStatus::Unsupported);
    EXPECT_FALSE(stable_handoff.valid());
}

}  // namespace openmeta
