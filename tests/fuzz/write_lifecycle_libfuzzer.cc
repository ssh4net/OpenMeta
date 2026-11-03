// SPDX-License-Identifier: Apache-2.0

#include "../metadata_container_test_fixture.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"
#include "openmeta/xmp_structure.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace openmeta {
namespace {

    static constexpr size_t kMaxInputBytes   = 64U * 1024U;
    static constexpr size_t kMaxOutputBytes  = 1024U * 1024U;
    static constexpr uint32_t kMaxEntries    = 128U;
    static constexpr uint64_t kMaxArenaBytes = 64U * 1024U;
    static constexpr std::string_view kDcNamespace
        = "http://purl.org/dc/elements/1.1/";
    static constexpr std::string_view kB39CustomNamespacePrefix
        = "urn:openmeta:fuzz:b39:";
    static constexpr std::string_view kB39SharedNamespace
        = "urn:openmeta:fuzz:b39:shared";
    static constexpr std::string_view kB39SourceNamespace
        = "urn:openmeta:fuzz:b39:source";
    static constexpr std::string_view kB39DestinationNamespace
        = "urn:openmeta:fuzz:b39:destination";
    static constexpr char kJpegXmpPrefix[] = "http://ns.adobe.com/xap/1.0/";
    static constexpr std::array<std::byte, 12U> kJpegIccSignature = {
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
        std::byte { '_' }, std::byte { 'P' }, std::byte { 'R' },
        std::byte { 'O' }, std::byte { 'F' }, std::byte { 'I' },
        std::byte { 'L' }, std::byte { 'E' }, std::byte { 0U },
    };
    [[noreturn]] static void fuzz_trap() noexcept
    {
#if defined(__clang__) || defined(__GNUC__)
        __builtin_trap();
#else
        std::abort();
#endif
    }

    static void require(bool condition) noexcept
    {
        if (!condition) {
            fuzz_trap();
        }
    }

    static uint8_t input_byte(std::span<const uint8_t> input,
                              size_t index) noexcept
    {
        return input.empty() ? 0U : input[index % input.size()];
    }

    static EntryId add_dc_text(MetaStore* store, BlockId block,
                               std::string_view path, std::string_view text,
                               uint32_t order)
    {
        Entry entry;
        entry.key   = make_xmp_property_key(store->arena(), kDcNamespace, path);
        entry.value = make_text(store->arena(), text, TextEncoding::Utf8);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        return store->add_entry(entry);
    }

    static std::string_view make_indexed_path(std::array<char, 32U>* storage,
                                              std::string_view base,
                                              uint32_t index) noexcept
    {
        size_t length = 0U;
        for (const char character : base) {
            (*storage)[length++] = character;
        }
        (*storage)[length++] = '[';
        if (index >= 10U) {
            (*storage)[length++] = static_cast<char>('0' + (index / 10U));
        }
        (*storage)[length++] = static_cast<char>('0' + (index % 10U));
        (*storage)[length++] = ']';
        return std::string_view(storage->data(), length);
    }

    static std::string_view arena_string(const MetaStore& store,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = store.arena().span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool text_equals(const MetaStore& store, const Entry& entry,
                            std::string_view expected) noexcept
    {
        if (entry.value.kind != MetaValueKind::Text) {
            return false;
        }
        const std::span<const std::byte> bytes = store.arena().span(
            entry.value.data.span);
        if (bytes.size() != expected.size()) {
            return false;
        }
        const auto* expected_bytes = reinterpret_cast<const std::byte*>(
            expected.data());
        for (size_t index = 0U; index < bytes.size(); ++index) {
            if (bytes[index] != expected_bytes[index]) {
                return false;
            }
        }
        return true;
    }

    static bool bytes_equal(std::span<const std::byte> left,
                            std::span<const std::byte> right) noexcept
    {
        if (left.size() != right.size()) {
            return false;
        }
        for (size_t index = 0U; index < left.size(); ++index) {
            if (left[index] != right[index]) {
                return false;
            }
        }
        return true;
    }

    class BoundedVectorWriter final : public TransferByteWriter {
    public:
        TransferStatus write(std::span<const std::byte> bytes) noexcept override
        {
            calls += 1U;
            if (output.size() > kMaxOutputBytes
                || bytes.size() > kMaxOutputBytes - output.size()) {
                return TransferStatus::LimitExceeded;
            }
            output.insert(output.end(), bytes.begin(), bytes.end());
            return TransferStatus::Ok;
        }

        uint32_t calls = 0U;
        std::vector<std::byte> output;
    };

    class NoAllocationWriter final : public TransferByteWriter {
    public:
        NoAllocationWriter() noexcept { storage.fill(std::byte { 0xA5U }); }

        TransferStatus write(std::span<const std::byte> bytes) noexcept override
        {
            calls += 1U;
            if (bytes.size() > storage.size() - written) {
                return TransferStatus::LimitExceeded;
            }
            for (size_t index = 0U; index < bytes.size(); ++index) {
                storage[written + index] = bytes[index];
            }
            written += bytes.size();
            return TransferStatus::Ok;
        }

        std::array<std::byte, 16U> storage {};
        size_t written = 0U;
        uint32_t calls = 0U;
    };

    struct TiffRootTag final {
        uint16_t tag   = 0U;
        uint16_t type  = 0U;
        uint32_t count = 0U;
        std::array<std::byte, 4U> value {};
    };

    struct TiffRootInfo final {
        std::array<TiffRootTag, 16U> tags {};
        uint16_t tag_count    = 0U;
        uint32_t icc_count    = 0U;
        uint32_t icc_offset   = 0U;
        uint32_t icc_bytes    = 0U;
        uint32_t width        = 0U;
        uint32_t height       = 0U;
        uint32_t strip_offset = 0U;
        uint32_t strip_bytes  = 0U;
        bool has_width        = false;
        bool has_height       = false;
        bool has_icc_data     = false;
        bool has_strip_offset = false;
        bool has_strip_bytes  = false;
        bool has_dng_version  = false;
    };

    static bool read_u16_le(std::span<const std::byte> bytes, size_t offset,
                            uint16_t* value) noexcept
    {
        if (value == nullptr || offset > bytes.size()
            || bytes.size() - offset < 2U) {
            return false;
        }
        *value = static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset]))
                 | static_cast<uint16_t>(
                     static_cast<uint16_t>(
                         std::to_integer<uint8_t>(bytes[offset + 1U]))
                     << 8U);
        return true;
    }

    static bool read_u32_le(std::span<const std::byte> bytes, size_t offset,
                            uint32_t* value) noexcept
    {
        if (value == nullptr || offset > bytes.size()
            || bytes.size() - offset < 4U) {
            return false;
        }
        *value = static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset]))
                 | (static_cast<uint32_t>(
                        std::to_integer<uint8_t>(bytes[offset + 1U]))
                    << 8U)
                 | (static_cast<uint32_t>(
                        std::to_integer<uint8_t>(bytes[offset + 2U]))
                    << 16U)
                 | (static_cast<uint32_t>(
                        std::to_integer<uint8_t>(bytes[offset + 3U]))
                    << 24U);
        return true;
    }

    static bool tiff_inline_uint(const TiffRootTag& tag,
                                 uint32_t* value) noexcept
    {
        if (value == nullptr || tag.count != 1U) {
            return false;
        }
        if (tag.type == 3U) {
            *value = static_cast<uint32_t>(
                         std::to_integer<uint8_t>(tag.value[0U]))
                     | (static_cast<uint32_t>(
                            std::to_integer<uint8_t>(tag.value[1U]))
                        << 8U);
            return true;
        }
        if (tag.type == 4U) {
            *value = static_cast<uint32_t>(
                         std::to_integer<uint8_t>(tag.value[0U]))
                     | (static_cast<uint32_t>(
                            std::to_integer<uint8_t>(tag.value[1U]))
                        << 8U)
                     | (static_cast<uint32_t>(
                            std::to_integer<uint8_t>(tag.value[2U]))
                        << 16U)
                     | (static_cast<uint32_t>(
                            std::to_integer<uint8_t>(tag.value[3U]))
                        << 24U);
            return true;
        }
        return false;
    }

    static bool read_tiff_ifd0(std::span<const std::byte> tiff,
                               TiffRootInfo* info) noexcept
    {
        if (info == nullptr || tiff.size() < 8U || tiff[0U] != std::byte { 'I' }
            || tiff[1U] != std::byte { 'I' }) {
            return false;
        }
        uint16_t version        = 0U;
        uint32_t ifd_offset_u32 = 0U;
        if (!read_u16_le(tiff, 2U, &version)
            || !read_u32_le(tiff, 4U, &ifd_offset_u32) || version != 42U) {
            return false;
        }
        const size_t ifd_offset = static_cast<size_t>(ifd_offset_u32);
        if (ifd_offset > tiff.size() || tiff.size() - ifd_offset < 6U) {
            return false;
        }
        uint16_t entry_count = 0U;
        if (!read_u16_le(tiff, ifd_offset, &entry_count)) {
            return false;
        }
        const size_t entries_offset = ifd_offset + 2U;
        const size_t ifd_remaining  = tiff.size() - entries_offset;
        if (ifd_remaining < 4U
            || static_cast<size_t>(entry_count) > (ifd_remaining - 4U) / 12U
            || entry_count > info->tags.size()) {
            return false;
        }

        info->tag_count = entry_count;
        for (uint16_t index = 0U; index < entry_count; ++index) {
            const size_t offset = entries_offset
                                  + static_cast<size_t>(index) * 12U;
            TiffRootTag& tag = info->tags[index];
            if (!read_u16_le(tiff, offset, &tag.tag)
                || !read_u16_le(tiff, offset + 2U, &tag.type)
                || !read_u32_le(tiff, offset + 4U, &tag.count)) {
                return false;
            }
            for (size_t value_index = 0U; value_index < tag.value.size();
                 ++value_index) {
                tag.value[value_index] = tiff[offset + 8U + value_index];
            }
            if (tag.tag == 34675U) {
                info->icc_count += 1U;
                uint32_t payload_offset = 0U;
                if (tag.type == 7U && tag.count > 4U
                    && read_u32_le(tiff, offset + 8U, &payload_offset)
                    && payload_offset <= tiff.size()
                    && static_cast<size_t>(tag.count)
                           <= tiff.size()
                                  - static_cast<size_t>(payload_offset)) {
                    info->icc_offset   = payload_offset;
                    info->icc_bytes    = tag.count;
                    info->has_icc_data = true;
                }
            } else if (tag.tag == 256U) {
                info->has_width = tiff_inline_uint(tag, &info->width);
            } else if (tag.tag == 257U) {
                info->has_height = tiff_inline_uint(tag, &info->height);
            } else if (tag.tag == 273U) {
                info->has_strip_offset = tiff_inline_uint(tag,
                                                          &info->strip_offset);
            } else if (tag.tag == 279U) {
                info->has_strip_bytes = tiff_inline_uint(tag,
                                                         &info->strip_bytes);
            } else if (tag.tag == 0xC612U) {
                info->has_dng_version = tag.type == 1U && tag.count == 4U;
            }
        }
        return true;
    }

    static const TiffRootTag* find_tiff_root_tag(const TiffRootInfo& info,
                                                 uint16_t wanted) noexcept
    {
        for (uint16_t index = 0U; index < info.tag_count; ++index) {
            if (info.tags[index].tag == wanted) {
                return &info.tags[index];
            }
        }
        return nullptr;
    }

    static bool same_tiff_root_tag(const TiffRootTag& left,
                                   const TiffRootTag& right) noexcept
    {
        return left.tag == right.tag && left.type == right.type
               && left.count == right.count && left.value == right.value;
    }

    static bool tiff_non_icc_source_is_preserved(
        std::span<const std::byte> source, std::span<const std::byte> output,
        uint32_t expected_source_icc_count) noexcept
    {
        TiffRootInfo source_info;
        TiffRootInfo output_info;
        if (!read_tiff_ifd0(source, &source_info)
            || !read_tiff_ifd0(output, &output_info)
            || source_info.icc_count != expected_source_icc_count
            || output_info.icc_count != 0U
            || source_info.tag_count != output_info.tag_count + 1U
            || !source_info.has_width || !output_info.has_width
            || !source_info.has_height || !output_info.has_height
            || source_info.width != output_info.width
            || source_info.height != output_info.height
            || !source_info.has_strip_offset || !output_info.has_strip_offset
            || !source_info.has_strip_bytes || !output_info.has_strip_bytes
            || source_info.strip_bytes != output_info.strip_bytes
            || source_info.has_dng_version != output_info.has_dng_version) {
            return false;
        }

        for (uint16_t source_index = 0U; source_index < source_info.tag_count;
             ++source_index) {
            const TiffRootTag& source_tag = source_info.tags[source_index];
            if (source_tag.tag == 273U || source_tag.tag == 34675U) {
                continue;
            }
            const TiffRootTag* output_tag = find_tiff_root_tag(output_info,
                                                               source_tag.tag);
            if (output_tag == nullptr
                || !same_tiff_root_tag(source_tag, *output_tag)) {
                return false;
            }
        }
        for (uint16_t output_index = 0U; output_index < output_info.tag_count;
             ++output_index) {
            const TiffRootTag& output_tag = output_info.tags[output_index];
            if (output_tag.tag == 273U) {
                continue;
            }
            const TiffRootTag* source_tag = find_tiff_root_tag(source_info,
                                                               output_tag.tag);
            if (source_tag == nullptr
                || !same_tiff_root_tag(*source_tag, output_tag)) {
                return false;
            }
        }

        const size_t source_strip_offset = static_cast<size_t>(
            source_info.strip_offset);
        const size_t output_strip_offset = static_cast<size_t>(
            output_info.strip_offset);
        const size_t strip_size = static_cast<size_t>(source_info.strip_bytes);
        if (source_strip_offset > source.size()
            || strip_size > source.size() - source_strip_offset
            || output_strip_offset > output.size()
            || strip_size > output.size() - output_strip_offset) {
            return false;
        }
        return bytes_equal(source.subspan(source_strip_offset, strip_size),
                           output.subspan(output_strip_offset, strip_size));
    }

    static bool jpeg_primary_icc_count(std::span<const std::byte> jpeg,
                                       uint32_t* count,
                                       bool* has_scan = nullptr) noexcept
    {
        if (count == nullptr || jpeg.size() < 4U
            || jpeg[0U] != std::byte { 0xFFU }
            || jpeg[1U] != std::byte { 0xD8U }) {
            return false;
        }
        *count = 0U;
        if (has_scan != nullptr) {
            *has_scan = false;
        }

        size_t offset = 2U;
        while (offset < jpeg.size()) {
            if (jpeg[offset] != std::byte { 0xFFU }) {
                return false;
            }
            while (offset < jpeg.size()
                   && jpeg[offset] == std::byte { 0xFFU }) {
                offset += 1U;
            }
            if (offset >= jpeg.size()) {
                return false;
            }
            const uint8_t marker = std::to_integer<uint8_t>(jpeg[offset]);
            offset += 1U;
            if (marker == 0xDAU) {
                if (has_scan != nullptr) {
                    *has_scan = true;
                }
                return true;
            }
            if (marker == 0xD9U) {
                return true;
            }
            if (marker == 0xD8U || marker == 0x01U
                || (marker >= 0xD0U && marker <= 0xD7U)) {
                continue;
            }
            if (jpeg.size() - offset < 2U) {
                return false;
            }
            const uint16_t length
                = static_cast<uint16_t>(
                      static_cast<uint16_t>(
                          std::to_integer<uint8_t>(jpeg[offset]))
                      << 8U)
                  | static_cast<uint16_t>(
                      std::to_integer<uint8_t>(jpeg[offset + 1U]));
            if (length < 2U || length > jpeg.size() - offset) {
                return false;
            }
            const size_t payload_offset = offset + 2U;
            const size_t payload_size   = static_cast<size_t>(length) - 2U;
            if (marker == 0xE2U && payload_size >= kJpegIccSignature.size()) {
                bool signature_matches = true;
                for (size_t index = 0U; index < kJpegIccSignature.size();
                     ++index) {
                    if (jpeg[payload_offset + index]
                        != kJpegIccSignature[index]) {
                        signature_matches = false;
                        break;
                    }
                }
                if (signature_matches) {
                    if (payload_size < 14U
                        || jpeg[payload_offset + 12U] != std::byte { 1U }
                        || jpeg[payload_offset + 13U] != std::byte { 1U }) {
                        return false;
                    }
                    *count += 1U;
                }
            }
            offset += length;
        }
        return false;
    }

    static bool jpeg_has_eoi_after_scan(std::span<const std::byte> jpeg) noexcept
    {
        uint32_t ignored_icc_count = 0U;
        bool has_scan              = false;
        return jpeg.size() >= 2U && jpeg.back() == std::byte { 0xD9U }
               && jpeg[jpeg.size() - 2U] == std::byte { 0xFFU }
               && jpeg_primary_icc_count(jpeg, &ignored_icc_count, &has_scan)
               && has_scan;
    }

    struct JpegIccSegment final {
        size_t payload_offset = 0U;
        size_t payload_size   = 0U;
    };

    static bool find_jpeg_icc_segment(std::span<const std::byte> jpeg,
                                      JpegIccSegment* segment) noexcept
    {
        if (segment == nullptr || jpeg.size() < 4U
            || jpeg[0U] != std::byte { 0xFFU }
            || jpeg[1U] != std::byte { 0xD8U }) {
            return false;
        }

        size_t offset = 2U;
        while (offset < jpeg.size()) {
            if (jpeg[offset] != std::byte { 0xFFU }) {
                return false;
            }
            while (offset < jpeg.size()
                   && jpeg[offset] == std::byte { 0xFFU }) {
                offset += 1U;
            }
            if (offset >= jpeg.size()) {
                return false;
            }
            const uint8_t marker = std::to_integer<uint8_t>(jpeg[offset]);
            offset += 1U;
            if (marker == 0xDAU || marker == 0xD9U) {
                return false;
            }
            if (marker == 0xD8U || marker == 0x01U
                || (marker >= 0xD0U && marker <= 0xD7U)) {
                continue;
            }
            if (jpeg.size() - offset < 2U) {
                return false;
            }
            const uint16_t length
                = static_cast<uint16_t>(
                      static_cast<uint16_t>(
                          std::to_integer<uint8_t>(jpeg[offset]))
                      << 8U)
                  | static_cast<uint16_t>(
                      std::to_integer<uint8_t>(jpeg[offset + 1U]));
            if (length < 2U || length > jpeg.size() - offset) {
                return false;
            }
            const size_t payload_offset = offset + 2U;
            const size_t payload_size   = static_cast<size_t>(length) - 2U;
            if (marker == 0xE2U && payload_size >= 14U) {
                bool signature_matches = true;
                for (size_t index = 0U; index < kJpegIccSignature.size();
                     ++index) {
                    if (jpeg[payload_offset + index]
                        != kJpegIccSignature[index]) {
                        signature_matches = false;
                        break;
                    }
                }
                if (signature_matches) {
                    if (payload_size <= 14U
                        || jpeg[payload_offset + 12U] != std::byte { 1U }
                        || jpeg[payload_offset + 13U] != std::byte { 1U }) {
                        return false;
                    }
                    segment->payload_offset = payload_offset;
                    segment->payload_size   = payload_size;
                    return true;
                }
            }
            offset += length;
        }
        return false;
    }

    static bool mutate_jpeg_icc_profile(const std::vector<std::byte>& source,
                                        std::span<const uint8_t> input,
                                        std::vector<std::byte>* mutated)
    {
        if (mutated == nullptr) {
            return false;
        }
        JpegIccSegment segment;
        if (!find_jpeg_icc_segment(source, &segment)) {
            return false;
        }
        const size_t profile_size = segment.payload_size - 14U;
        if (profile_size == 0U) {
            return false;
        }
        const size_t profile_index = static_cast<size_t>(input_byte(input, 3U))
                                     % profile_size;
        const size_t byte_offset = segment.payload_offset + 14U + profile_index;
        *mutated                 = source;
        (*mutated)[byte_offset] ^= static_cast<std::byte>(input_byte(input, 4U)
                                                          | 1U);
        return true;
    }

    static bool truncate_jpeg_icc_segment(const std::vector<std::byte>& source,
                                          std::vector<std::byte>* truncated)
    {
        if (truncated == nullptr) {
            return false;
        }
        JpegIccSegment segment;
        if (!find_jpeg_icc_segment(source, &segment)
            || segment.payload_size == 0U) {
            return false;
        }
        *truncated = source;
        truncated->resize(segment.payload_offset + segment.payload_size - 1U);
        return true;
    }

    static bool mutate_tiff_icc_profile(const std::vector<std::byte>& source,
                                        std::span<const uint8_t> input,
                                        std::vector<std::byte>* mutated)
    {
        if (mutated == nullptr) {
            return false;
        }
        TiffRootInfo info;
        if (!read_tiff_ifd0(source, &info) || info.icc_count != 1U
            || !info.has_icc_data || info.icc_bytes == 0U) {
            return false;
        }
        const size_t profile_size  = static_cast<size_t>(info.icc_bytes);
        const size_t profile_index = static_cast<size_t>(input_byte(input, 3U))
                                     % profile_size;
        *mutated                 = source;
        const size_t byte_offset = static_cast<size_t>(info.icc_offset)
                                   + profile_index;
        (*mutated)[byte_offset] ^= static_cast<std::byte>(input_byte(input, 4U)
                                                          | 1U);
        return true;
    }

    static bool mutate_tiff_ifd0_offset(const std::vector<std::byte>& source,
                                        std::vector<std::byte>* malformed)
    {
        if (malformed == nullptr || source.size() < 8U
            || source[0U] != std::byte { 'I' }
            || source[1U] != std::byte { 'I' }
            || source[2U] != std::byte { 42U }
            || source[3U] != std::byte { 0U }) {
            return false;
        }
        *malformed       = source;
        (*malformed)[4U] = std::byte { 0xF0U };
        (*malformed)[5U] = std::byte { 0xFFU };
        (*malformed)[6U] = std::byte { 0xFFU };
        (*malformed)[7U] = std::byte { 0xFFU };
        return true;
    }

    static bool prepare_icc_removal_bundle(TransferTargetFormat target,
                                           PreparedTransferBundle* bundle)
    {
        if (bundle == nullptr) {
            return false;
        }
        MetaStore empty;
        empty.finalize();
        PrepareTransferRequest request;
        request.target_format       = target;
        request.dng_target_mode     = DngTargetMode::ExistingTarget;
        request.include_exif_app1   = false;
        request.include_xmp_app1    = false;
        request.include_icc_app2    = false;
        request.include_iptc_app13  = false;
        request.xmp_portable        = false;
        request.xmp_project_exif    = false;
        request.xmp_project_iptc    = false;
        request.remove_existing_icc = true;
        const PrepareTransferResult prepared
            = prepare_metadata_for_target(empty, request, bundle);
        return prepared.status == TransferStatus::Ok
               && bundle->target_format == target && bundle->remove_existing_icc
               && bundle->blocks.empty();
    }

    static PreparedTransferBundle
    make_icc_seed_bundle(TransferTargetFormat target, bool jpeg,
                         std::span<const uint8_t> input)
    {
        PreparedTransferBundle bundle;
        bundle.target_format = target;
        PreparedTransferBlock block;
        block.kind  = TransferBlockKind::Icc;
        block.route = jpeg ? "jpeg:app2-icc" : "tiff:tag-34675-icc";
        if (jpeg) {
            block.payload.assign(kJpegIccSignature.begin(),
                                 kJpegIccSignature.end());
            block.payload.push_back(std::byte { 1U });
            block.payload.push_back(std::byte { 1U });
        }
        const size_t profile_size
            = 5U + static_cast<size_t>(input_byte(input, 0U) % 28U);
        for (size_t index = 0U; index < profile_size; ++index) {
            block.payload.push_back(
                static_cast<std::byte>(input_byte(input, index + 1U)));
        }
        bundle.blocks.push_back(std::move(block));
        return bundle;
    }

    static bool make_jpeg_icc_base(std::vector<std::byte>* base)
    {
        if (base == nullptr) {
            return false;
        }
        const container_test::JpegFixture fixture
            = container_test::make_jpeg_fixture();
        if (fixture.bytes.size() < 2U
            || fixture.bytes[fixture.bytes.size() - 2U] != std::byte { 0xFFU }
            || fixture.bytes.back() != std::byte { 0xD9U }
            || fixture.bytes.size() + kJpegIccSignature.size()
                   > kMaxInputBytes) {
            return false;
        }
        *base = fixture.bytes;
        base->insert(base->end() - 2, kJpegIccSignature.begin(),
                     kJpegIccSignature.end());
        uint32_t icc_count = 0U;
        return jpeg_primary_icc_count(*base, &icc_count) && icc_count == 0U;
    }

    static PreparedTransferPackagePlan
    make_generic_source_copy_plan(TransferTargetFormat target,
                                  size_t input_size)
    {
        PreparedTransferPackagePlan plan;
        plan.target_format = target;
        plan.input_size    = static_cast<uint64_t>(input_size);
        plan.output_size   = static_cast<uint64_t>(input_size);
        PreparedTransferPackageChunk source;
        source.kind          = TransferPackageChunkKind::SourceRange;
        source.output_offset = 0U;
        source.source_offset = 0U;
        source.size          = static_cast<uint64_t>(input_size);
        plan.chunks.push_back(std::move(source));
        return plan;
    }

    static bool rejected_package_preserves_sentinels(
        std::span<const std::byte> input, const PreparedTransferBundle& removal,
        const PreparedTransferPackagePlan& generic_plan)
    {
        PreparedTransferPackageBatch batch;
        batch.target_format = removal.target_format;
        batch.output_size   = 1U;
        PreparedTransferPackageBlob sentinel;
        sentinel.kind          = TransferPackageChunkKind::InlineBytes;
        sentinel.output_offset = 0U;
        sentinel.bytes         = { std::byte { 0xA5U } };
        batch.chunks.push_back(std::move(sentinel));

        const EmitTransferResult materialized
            = build_prepared_transfer_package_batch(input, removal,
                                                    generic_plan, &batch);
        if (materialized.status == TransferStatus::Ok
            || batch.target_format != removal.target_format
            || batch.output_size != 1U || batch.chunks.size() != 1U
            || batch.chunks.front().bytes.size() != 1U
            || batch.chunks.front().bytes[0U] != std::byte { 0xA5U }) {
            return false;
        }

        NoAllocationWriter writer;
        const EmitTransferResult written
            = write_prepared_transfer_package(input, removal, generic_plan,
                                              writer);
        if (written.status == TransferStatus::Ok || writer.calls != 0U
            || writer.written != 0U) {
            return false;
        }
        for (const std::byte value : writer.storage) {
            if (value != std::byte { 0xA5U }) {
                return false;
            }
        }
        return true;
    }

    static bool rejected_malformed_jpeg_preserves_sentinel(
        std::span<const std::byte> malformed,
        const PreparedTransferBundle& removal)
    {
        const JpegEditPlan plan = plan_prepared_bundle_jpeg_edit(malformed,
                                                                 removal);
        if (plan.status == TransferStatus::Ok) {
            return false;
        }
        std::vector<std::byte> output = { std::byte { 0xA5U } };
        const EmitTransferResult applied
            = apply_prepared_bundle_jpeg_edit(malformed, removal, plan,
                                              &output);
        if (applied.status == TransferStatus::Ok || output.size() != 1U
            || output[0U] != std::byte { 0xA5U }) {
            return false;
        }
        NoAllocationWriter writer;
        const EmitTransferResult written
            = write_prepared_bundle_jpeg_edit(malformed, removal, plan, writer);
        return written.status != TransferStatus::Ok && writer.calls == 0U
               && writer.written == 0U;
    }

    static bool rejected_malformed_tiff_preserves_sentinel(
        std::span<const std::byte> malformed,
        const PreparedTransferBundle& removal)
    {
        const TiffEditPlan plan = plan_prepared_bundle_tiff_edit(malformed,
                                                                 removal);
        if (plan.status == TransferStatus::Ok) {
            return false;
        }
        std::vector<std::byte> output = { std::byte { 0xA5U } };
        const EmitTransferResult applied
            = apply_prepared_bundle_tiff_edit(malformed, removal, plan,
                                              &output);
        if (applied.status == TransferStatus::Ok || output.size() != 1U
            || output[0U] != std::byte { 0xA5U }) {
            return false;
        }
        NoAllocationWriter writer;
        const EmitTransferResult written
            = write_prepared_bundle_tiff_edit(malformed, removal, plan, writer);
        return written.status != TransferStatus::Ok && writer.calls == 0U
               && writer.written == 0U;
    }

    static bool package_jpeg_replay_matches_direct(
        std::span<const std::byte> input, const PreparedTransferBundle& removal,
        const JpegEditPlan& edit_plan,
        const std::vector<std::byte>& direct_output)
    {
        PreparedTransferPackagePlan package;
        const EmitTransferResult packaged
            = build_prepared_bundle_jpeg_package(input, removal, edit_plan,
                                                 &package);
        if (packaged.status != TransferStatus::Ok) {
            return false;
        }
        PreparedTransferPackageBatch batch;
        const EmitTransferResult materialized
            = build_prepared_transfer_package_batch(input, removal, package,
                                                    &batch);
        if (materialized.status != TransferStatus::Ok
            || batch.output_size != direct_output.size()
            || batch.output_size > kMaxOutputBytes) {
            return false;
        }

        BoundedVectorWriter first_writer;
        const EmitTransferResult first_replay
            = write_prepared_transfer_package_batch(batch, first_writer);
        if (first_replay.status != TransferStatus::Ok
            || first_writer.output != direct_output) {
            return false;
        }
        BoundedVectorWriter second_writer;
        const EmitTransferResult second_replay
            = write_prepared_transfer_package_batch(batch, second_writer);
        return second_replay.status == TransferStatus::Ok
               && second_writer.output == direct_output;
    }

    static bool package_tiff_replay_matches_direct(
        std::span<const std::byte> input, const PreparedTransferBundle& removal,
        const TiffEditPlan& edit_plan,
        const std::vector<std::byte>& direct_output)
    {
        PreparedTransferPackagePlan package;
        const EmitTransferResult packaged
            = build_prepared_bundle_tiff_package(input, removal, edit_plan,
                                                 &package);
        if (packaged.status != TransferStatus::Ok) {
            return false;
        }
        PreparedTransferPackageBatch batch;
        const EmitTransferResult materialized
            = build_prepared_transfer_package_batch(input, removal, package,
                                                    &batch);
        if (materialized.status != TransferStatus::Ok
            || batch.output_size != direct_output.size()
            || batch.output_size > kMaxOutputBytes) {
            return false;
        }

        BoundedVectorWriter first_writer;
        const EmitTransferResult first_replay
            = write_prepared_transfer_package_batch(batch, first_writer);
        if (first_replay.status != TransferStatus::Ok
            || first_writer.output != direct_output) {
            return false;
        }
        BoundedVectorWriter second_writer;
        const EmitTransferResult second_replay
            = write_prepared_transfer_package_batch(batch, second_writer);
        return second_replay.status == TransferStatus::Ok
               && second_writer.output == direct_output;
    }

    static void fuzz_icc_jpeg_removal(std::span<const uint8_t> input)
    {
        std::vector<std::byte> base;
        require(make_jpeg_icc_base(&base));
        const std::vector<std::byte> base_before = base;
        uint32_t icc_count                       = 0U;
        require(jpeg_primary_icc_count(base, &icc_count) && icc_count == 0U);

        const PreparedTransferBundle seed
            = make_icc_seed_bundle(TransferTargetFormat::Jpeg, true, input);
        const JpegEditPlan seed_plan = plan_prepared_bundle_jpeg_edit(base,
                                                                      seed);
        require(seed_plan.status == TransferStatus::Ok);
        std::vector<std::byte> seeded;
        const EmitTransferResult seeded_result
            = apply_prepared_bundle_jpeg_edit(base, seed, seed_plan, &seeded);
        require(seeded_result.status == TransferStatus::Ok && !seeded.empty()
                && seeded.size() <= kMaxInputBytes);
        const std::vector<std::byte> seeded_before = seeded;
        require(jpeg_primary_icc_count(seeded, &icc_count) && icc_count == 1U);

        PreparedTransferBundle removal;
        require(
            prepare_icc_removal_bundle(TransferTargetFormat::Jpeg, &removal));
        const JpegEditPlan no_op_plan = plan_prepared_bundle_jpeg_edit(base,
                                                                       removal);
        require(no_op_plan.status == TransferStatus::Ok);
        std::vector<std::byte> no_op_output;
        const EmitTransferResult no_op_result
            = apply_prepared_bundle_jpeg_edit(base, removal, no_op_plan,
                                              &no_op_output);
        require(no_op_result.status == TransferStatus::Ok
                && no_op_output == base);

        const JpegEditPlan edit_plan = plan_prepared_bundle_jpeg_edit(seeded,
                                                                      removal);
        require(edit_plan.status == TransferStatus::Ok);
        std::vector<std::byte> direct_output;
        const EmitTransferResult direct_result
            = apply_prepared_bundle_jpeg_edit(seeded, removal, edit_plan,
                                              &direct_output);
        require(direct_result.status == TransferStatus::Ok
                && !direct_output.empty()
                && direct_output.size() <= kMaxOutputBytes);
        require(jpeg_primary_icc_count(direct_output, &icc_count)
                && icc_count == 0U && direct_output == base
                && jpeg_has_eoi_after_scan(direct_output));

        std::vector<std::byte> repeated_output;
        const EmitTransferResult repeated_result
            = apply_prepared_bundle_jpeg_edit(seeded, removal, edit_plan,
                                              &repeated_output);
        require(repeated_result.status == TransferStatus::Ok
                && repeated_output == direct_output);

        std::vector<std::byte> alias = seeded;
        const EmitTransferResult alias_result
            = apply_prepared_bundle_jpeg_edit(alias, removal, edit_plan,
                                              &alias);
        require(alias_result.status == TransferStatus::Ok
                && alias == direct_output);

        const JpegEditPlan reapply_plan
            = plan_prepared_bundle_jpeg_edit(direct_output, removal);
        require(reapply_plan.status == TransferStatus::Ok);
        std::vector<std::byte> reapply_output;
        const EmitTransferResult reapply_result
            = apply_prepared_bundle_jpeg_edit(direct_output, removal,
                                              reapply_plan, &reapply_output);
        require(reapply_result.status == TransferStatus::Ok
                && reapply_output == direct_output);

        std::vector<std::byte> mutated_seeded;
        require(mutate_jpeg_icc_profile(seeded, input, &mutated_seeded)
                && mutated_seeded != seeded);
        const std::vector<std::byte> mutated_seeded_before = mutated_seeded;
        require(jpeg_primary_icc_count(mutated_seeded, &icc_count)
                && icc_count == 1U);
        const JpegEditPlan mutated_edit_plan
            = plan_prepared_bundle_jpeg_edit(mutated_seeded, removal);
        require(mutated_edit_plan.status == TransferStatus::Ok);
        std::vector<std::byte> mutated_output;
        const EmitTransferResult mutated_result
            = apply_prepared_bundle_jpeg_edit(mutated_seeded, removal,
                                              mutated_edit_plan,
                                              &mutated_output);
        require(mutated_result.status == TransferStatus::Ok
                && !mutated_output.empty()
                && mutated_output.size() <= kMaxOutputBytes);
        require(jpeg_primary_icc_count(mutated_output, &icc_count)
                && icc_count == 0U && jpeg_has_eoi_after_scan(mutated_output));
        require(package_jpeg_replay_matches_direct(mutated_seeded, removal,
                                                   mutated_edit_plan,
                                                   mutated_output));

        std::vector<std::byte> truncated;
        require(truncate_jpeg_icc_segment(seeded, &truncated)
                && truncated != seeded);
        require(rejected_malformed_jpeg_preserves_sentinel(truncated, removal));
        const PreparedTransferPackagePlan generic_plan
            = make_generic_source_copy_plan(TransferTargetFormat::Jpeg,
                                            mutated_seeded.size());
        require(rejected_package_preserves_sentinels(mutated_seeded, removal,
                                                     generic_plan));

        PreparedTransferBundle conflict = removal;
        PreparedTransferBlock conflicting_icc;
        conflicting_icc.kind  = TransferBlockKind::Icc;
        conflicting_icc.route = "jpeg:app2-icc";
        conflict.blocks.push_back(std::move(conflicting_icc));
        require(plan_prepared_bundle_jpeg_edit(seeded, conflict).status
                != TransferStatus::Ok);
        std::vector<std::byte> sentinel = { std::byte { 0xA5U } };
        const EmitTransferResult conflict_result
            = apply_prepared_bundle_jpeg_edit(seeded, conflict, edit_plan,
                                              &sentinel);
        require(conflict_result.status != TransferStatus::Ok
                && sentinel.size() == 1U
                && sentinel[0U] == std::byte { 0xA5U });

        require(base == base_before && seeded == seeded_before
                && mutated_seeded == mutated_seeded_before);
    }

    static void fuzz_icc_tiff_removal(bool dng, std::span<const uint8_t> input)
    {
        const TransferTargetFormat target = dng ? TransferTargetFormat::Dng
                                                : TransferTargetFormat::Tiff;
        const container_test::TiffFixtureVariant variant { false, false, dng };
        const std::vector<std::byte> base = container_test::make_tiff_fixture(
            variant);
        require(base.size() <= kMaxInputBytes);
        const std::vector<std::byte> base_before = base;

        const PreparedTransferBundle seed = make_icc_seed_bundle(target, false,
                                                                 input);
        const TiffEditPlan seed_plan      = plan_prepared_bundle_tiff_edit(base,
                                                                           seed);
        require(seed_plan.status == TransferStatus::Ok);
        std::vector<std::byte> seeded;
        const EmitTransferResult seeded_result
            = apply_prepared_bundle_tiff_edit(base, seed, seed_plan, &seeded);
        require(seeded_result.status == TransferStatus::Ok && !seeded.empty()
                && seeded.size() <= kMaxInputBytes);
        const std::vector<std::byte> seeded_before = seeded;
        TiffRootInfo seeded_info;
        require(read_tiff_ifd0(seeded, &seeded_info)
                && seeded_info.icc_count == 1U);

        PreparedTransferBundle removal;
        require(prepare_icc_removal_bundle(target, &removal));
        PlanTiffEditOptions no_update_options;
        no_update_options.require_updates = false;
        const TiffEditPlan no_op_plan
            = plan_prepared_bundle_tiff_edit(base, removal, no_update_options);
        require(no_op_plan.status == TransferStatus::Ok);
        std::vector<std::byte> no_op_output;
        const EmitTransferResult no_op_result
            = apply_prepared_bundle_tiff_edit(base, removal, no_op_plan,
                                              &no_op_output);
        require(no_op_result.status == TransferStatus::Ok
                && no_op_output == base);

        const TiffEditPlan edit_plan = plan_prepared_bundle_tiff_edit(seeded,
                                                                      removal);
        require(edit_plan.status == TransferStatus::Ok);
        std::vector<std::byte> direct_output;
        const EmitTransferResult direct_result
            = apply_prepared_bundle_tiff_edit(seeded, removal, edit_plan,
                                              &direct_output);
        require(direct_result.status == TransferStatus::Ok
                && !direct_output.empty()
                && direct_output.size() <= kMaxOutputBytes);
        require(tiff_non_icc_source_is_preserved(seeded, direct_output, 1U));

        std::vector<std::byte> repeated_output;
        const EmitTransferResult repeated_result
            = apply_prepared_bundle_tiff_edit(seeded, removal, edit_plan,
                                              &repeated_output);
        require(repeated_result.status == TransferStatus::Ok
                && repeated_output == direct_output);

        std::vector<std::byte> alias = seeded;
        const EmitTransferResult alias_result
            = apply_prepared_bundle_tiff_edit(alias, removal, edit_plan,
                                              &alias);
        require(alias_result.status == TransferStatus::Ok
                && alias == direct_output);

        const TiffEditPlan reapply_plan
            = plan_prepared_bundle_tiff_edit(direct_output, removal,
                                             no_update_options);
        require(reapply_plan.status == TransferStatus::Ok);
        std::vector<std::byte> reapply_output;
        const EmitTransferResult reapply_result
            = apply_prepared_bundle_tiff_edit(direct_output, removal,
                                              reapply_plan, &reapply_output);
        require(reapply_result.status == TransferStatus::Ok
                && reapply_output == direct_output);

        std::vector<std::byte> mutated_seeded;
        require(mutate_tiff_icc_profile(seeded, input, &mutated_seeded)
                && mutated_seeded != seeded);
        const std::vector<std::byte> mutated_seeded_before = mutated_seeded;
        TiffRootInfo mutated_seeded_info;
        require(read_tiff_ifd0(mutated_seeded, &mutated_seeded_info)
                && mutated_seeded_info.icc_count == 1U
                && mutated_seeded_info.has_icc_data);
        const TiffEditPlan mutated_edit_plan
            = plan_prepared_bundle_tiff_edit(mutated_seeded, removal);
        require(mutated_edit_plan.status == TransferStatus::Ok);
        std::vector<std::byte> mutated_output;
        const EmitTransferResult mutated_result
            = apply_prepared_bundle_tiff_edit(mutated_seeded, removal,
                                              mutated_edit_plan,
                                              &mutated_output);
        require(mutated_result.status == TransferStatus::Ok
                && !mutated_output.empty()
                && mutated_output.size() <= kMaxOutputBytes);
        require(tiff_non_icc_source_is_preserved(mutated_seeded, mutated_output,
                                                 1U));
        require(package_tiff_replay_matches_direct(mutated_seeded, removal,
                                                   mutated_edit_plan,
                                                   mutated_output));

        std::vector<std::byte> malformed_offset;
        require(mutate_tiff_ifd0_offset(seeded, &malformed_offset)
                && malformed_offset != seeded);
        require(rejected_malformed_tiff_preserves_sentinel(malformed_offset,
                                                           removal));
        const PreparedTransferPackagePlan generic_plan
            = make_generic_source_copy_plan(target, mutated_seeded.size());
        require(rejected_package_preserves_sentinels(mutated_seeded, removal,
                                                     generic_plan));

        PreparedTransferBundle conflict = removal;
        PreparedTransferBlock conflicting_icc;
        conflicting_icc.kind  = TransferBlockKind::Icc;
        conflicting_icc.route = "tiff:tag-34675-icc";
        conflict.blocks.push_back(std::move(conflicting_icc));
        require(plan_prepared_bundle_tiff_edit(mutated_seeded, conflict).status
                != TransferStatus::Ok);
        std::vector<std::byte> sentinel = { std::byte { 0xA5U } };
        const EmitTransferResult conflict_result
            = apply_prepared_bundle_tiff_edit(mutated_seeded, conflict,
                                              mutated_edit_plan, &sentinel);
        require(conflict_result.status != TransferStatus::Ok
                && sentinel.size() == 1U
                && sentinel[0U] == std::byte { 0xA5U });

        require(base == base_before && seeded == seeded_before
                && mutated_seeded == mutated_seeded_before);
    }

    static bool readback_collection_matches(
        const MetaStore& store, std::string_view base,
        const std::array<std::string_view, 2U>& expected_values) noexcept
    {
        uint32_t path_count = 0U;
        for (const Entry& entry : store.entries()) {
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || arena_string(store, entry.key.data.xmp_property.schema_ns)
                       != kDcNamespace) {
                continue;
            }
            const std::string_view path
                = arena_string(store,
                               entry.key.data.xmp_property.property_path);
            if (any(entry.flags, EntryFlags::Deleted)
                || !path.starts_with(base)) {
                continue;
            }
            const std::string_view suffix = path.substr(base.size());
            if (!suffix.empty() && suffix.front() != '[') {
                continue;
            }
            if (suffix != "[1]" && suffix != "[2]") {
                return false;
            }
            path_count += 1U;
        }
        if (path_count != 2U) {
            return false;
        }

        for (uint32_t item = 0U; item < 2U; ++item) {
            std::array<char, 32U> path_storage {};
            size_t path_size = 0U;
            for (const char character : base) {
                path_storage[path_size++] = character;
            }
            path_storage[path_size++] = '[';
            path_storage[path_size++] = static_cast<char>('1' + item);
            path_storage[path_size++] = ']';
            MetaKeyView key;
            key.kind                        = MetaKeyKind::XmpProperty;
            key.data.xmp_property.schema_ns = kDcNamespace;
            key.data.xmp_property.property_path
                = std::string_view(path_storage.data(), path_size);
            const std::span<const EntryId> ids = store.find_all(key);
            if (ids.size() != 1U
                || !text_equals(store, store.entry(ids[0U]),
                                expected_values[item])) {
                return false;
            }
        }
        return true;
    }

    static XmpPortableOptions fuzz_portable_options() noexcept
    {
        XmpPortableOptions options;
        options.include_exif            = false;
        options.include_iptc            = false;
        options.include_existing_xmp    = true;
        options.limits.max_output_bytes = kMaxOutputBytes;
        options.limits.max_entries      = kMaxEntries;
        return options;
    }

    static XmpDecodeOptions fuzz_decode_options() noexcept
    {
        XmpDecodeOptions options;
        options.limits.max_input_bytes       = kMaxOutputBytes;
        options.limits.max_properties        = kMaxEntries;
        options.limits.max_path_bytes        = 128U;
        options.limits.max_namespace_bytes   = 128U;
        options.limits.max_value_bytes       = 1024U;
        options.limits.max_total_value_bytes = 16U * 1024U;
        options.limits.max_arena_bytes       = kMaxArenaBytes;
        return options;
    }

    static bool render_xmp(const MetaStore& store,
                           std::vector<std::byte>* output)
    {
        if (output == nullptr) {
            return false;
        }
        const XmpPortableOptions options = fuzz_portable_options();
        const XmpDumpResult measured = dump_xmp_portable(store, {}, options);
        if (measured.status != XmpDumpStatus::OutputTruncated
            || measured.needed == 0U || measured.needed > kMaxOutputBytes) {
            return false;
        }
        output->resize(static_cast<size_t>(measured.needed));
        const XmpDumpResult written = dump_xmp_portable(store, *output,
                                                        options);
        if (written.status != XmpDumpStatus::Ok
            || written.written != measured.needed) {
            return false;
        }
        std::vector<std::byte> repeat(static_cast<size_t>(measured.needed));
        const XmpDumpResult repeated = dump_xmp_portable(store, repeat,
                                                         options);
        if (repeated.status != XmpDumpStatus::Ok
            || repeated.written != measured.needed) {
            return false;
        }
        for (size_t index = 0U; index < output->size(); ++index) {
            if ((*output)[index] != repeat[index]) {
                return false;
            }
        }
        return true;
    }

    static bool render_custom_xmp(const MetaStore& store,
                                  std::vector<std::byte>* output)
    {
        if (output == nullptr) {
            return false;
        }
        XmpPortableOptions options = fuzz_portable_options();
        options.existing_namespace_policy
            = XmpExistingNamespacePolicy::PreserveCustom;
        const XmpDumpResult measured = dump_xmp_portable(store, {}, options);
        if (measured.status != XmpDumpStatus::OutputTruncated
            || measured.needed == 0U || measured.needed > kMaxOutputBytes) {
            return false;
        }
        output->resize(static_cast<size_t>(measured.needed));
        const XmpDumpResult written = dump_xmp_portable(store, *output,
                                                        options);
        if (written.status != XmpDumpStatus::Ok
            || written.written != measured.needed) {
            return false;
        }
        return true;
    }

    static bool decode_xmp(std::span<const std::byte> packet, MetaStore* output)
    {
        if (output == nullptr || packet.empty()
            || packet.size() > kMaxOutputBytes) {
            return false;
        }
        output->constrain_resources(kMaxEntries, kMaxArenaBytes);
        const XmpDecodeResult decoded
            = decode_xmp_packet(packet, *output, EntryFlags::None,
                                fuzz_decode_options());
        if (decoded.status != XmpDecodeStatus::Ok) {
            return false;
        }
        output->finalize();
        return true;
    }

    static bool decode_custom_xmp(std::span<const std::byte> packet,
                                  MetaStore* output)
    {
        if (output == nullptr || packet.empty()
            || packet.size() > kMaxOutputBytes) {
            return false;
        }
        output->constrain_resources(kMaxEntries, kMaxArenaBytes);
        XmpDecodeOptions options           = fuzz_decode_options();
        options.preserve_custom_structures = true;
        const XmpDecodeResult decoded
            = decode_xmp_packet(packet, *output, EntryFlags::None, options);
        if (decoded.status != XmpDecodeStatus::Ok) {
            return false;
        }
        output->finalize();
        return true;
    }

    static bool same_source_entry(const MetaStore& store, const Entry& left,
                                  const Entry& right) noexcept
    {
        return left.key.kind == right.key.kind
               && arena_string(store, left.key.data.xmp_property.schema_ns)
                      == arena_string(store,
                                      right.key.data.xmp_property.schema_ns)
               && left.key.data.xmp_property.schema_ns.offset
                      == right.key.data.xmp_property.schema_ns.offset
               && left.key.data.xmp_property.schema_ns.size
                      == right.key.data.xmp_property.schema_ns.size
               && arena_string(store, left.key.data.xmp_property.property_path)
                      == arena_string(store,
                                      right.key.data.xmp_property.property_path)
               && left.key.data.xmp_property.property_path.offset
                      == right.key.data.xmp_property.property_path.offset
               && left.key.data.xmp_property.property_path.size
                      == right.key.data.xmp_property.property_path.size
               && left.value.kind == right.value.kind
               && left.value.elem_type == right.value.elem_type
               && left.value.text_encoding == right.value.text_encoding
               && left.value.count == right.value.count
               && (left.value.kind == MetaValueKind::Empty
                   || (left.value.kind == MetaValueKind::Text
                       && left.value.data.span.offset
                              == right.value.data.span.offset
                       && left.value.data.span.size
                              == right.value.data.span.size))
               && left.origin.block == right.origin.block
               && left.origin.order_in_block == right.origin.order_in_block
               && left.origin.wire_type.family == right.origin.wire_type.family
               && left.origin.wire_type.code == right.origin.wire_type.code
               && left.origin.wire_count == right.origin.wire_count
               && left.origin.wire_type_name.offset
                      == right.origin.wire_type_name.offset
               && left.origin.wire_type_name.size
                      == right.origin.wire_type_name.size
               && left.origin.name_context_kind
                      == right.origin.name_context_kind
               && left.origin.name_context_variant
                      == right.origin.name_context_variant
               && left.flags == right.flags;
    }

    static bool store_unchanged(const MetaStore& store,
                                std::span<const Entry> entries_before,
                                std::span<const std::byte> arena_before,
                                uint32_t block_count_before) noexcept
    {
        if (store.block_count() != block_count_before
            || store.entries().size() != entries_before.size()) {
            return false;
        }
        const std::span<const std::byte> arena_after = store.arena().bytes();
        if (arena_after.size() != arena_before.size()) {
            return false;
        }
        for (size_t index = 0U; index < arena_after.size(); ++index) {
            if (arena_after[index] != arena_before[index]) {
                return false;
            }
        }
        for (size_t index = 0U; index < entries_before.size(); ++index) {
            if (!same_source_entry(store,
                                   store.entry(static_cast<EntryId>(index)),
                                   entries_before[index])) {
                return false;
            }
        }
        return true;
    }

    static void append_jpeg_segment(std::vector<std::byte>* jpeg,
                                    uint8_t marker,
                                    std::span<const std::byte> payload)
    {
        const uint16_t length = static_cast<uint16_t>(payload.size() + 2U);
        jpeg->push_back(std::byte { 0xFFU });
        jpeg->push_back(static_cast<std::byte>(marker));
        jpeg->push_back(static_cast<std::byte>(length >> 8U));
        jpeg->push_back(static_cast<std::byte>(length & 0xFFU));
        jpeg->insert(jpeg->end(), payload.begin(), payload.end());
    }

    static bool make_jpeg_with_xmp(std::span<const std::byte> packet,
                                   std::vector<std::byte>* output)
    {
        constexpr size_t prefix_size = sizeof(kJpegXmpPrefix);
        if (output == nullptr || packet.empty()
            || packet.size() + prefix_size + 2U > 65535U) {
            return false;
        }
        const container_test::JpegFixture fixture
            = container_test::make_jpeg_fixture();
        if (fixture.bytes.size() < 2U
            || fixture.bytes.size() > kMaxInputBytes) {
            return false;
        }

        std::vector<std::byte> payload;
        payload.reserve(prefix_size + packet.size());
        for (size_t index = 0U; index < prefix_size; ++index) {
            payload.push_back(static_cast<std::byte>(kJpegXmpPrefix[index]));
        }
        payload.insert(payload.end(), packet.begin(), packet.end());

        output->clear();
        output->reserve(fixture.bytes.size() + payload.size() + 4U);
        output->insert(output->end(), fixture.bytes.begin(),
                       fixture.bytes.begin() + 2);
        append_jpeg_segment(output, 0xE1U, payload);
        output->insert(output->end(), fixture.bytes.begin() + 2,
                       fixture.bytes.end());
        return output->size() <= kMaxInputBytes;
    }

    static bool jpeg_existing_xmp(std::span<const std::byte> jpeg,
                                  std::span<const std::byte>* packet) noexcept
    {
        constexpr size_t prefix_size = sizeof(kJpegXmpPrefix);
        if (packet == nullptr || jpeg.size() < 4U
            || jpeg[0U] != std::byte { 0xFFU }
            || jpeg[1U] != std::byte { 0xD8U }) {
            return false;
        }

        size_t offset = 2U;
        while (offset < jpeg.size()) {
            if (jpeg[offset] != std::byte { 0xFFU }) {
                return false;
            }
            while (offset < jpeg.size()
                   && jpeg[offset] == std::byte { 0xFFU }) {
                offset += 1U;
            }
            if (offset >= jpeg.size()) {
                return false;
            }
            const uint8_t marker = std::to_integer<uint8_t>(jpeg[offset]);
            offset += 1U;
            if (marker == 0xDAU || marker == 0xD9U) {
                return false;
            }
            if (marker == 0xD8U || marker == 0x01U
                || (marker >= 0xD0U && marker <= 0xD7U)) {
                continue;
            }
            if (jpeg.size() - offset < 2U) {
                return false;
            }
            const uint16_t length
                = static_cast<uint16_t>(std::to_integer<uint8_t>(jpeg[offset])
                                        << 8U)
                  | static_cast<uint16_t>(
                      std::to_integer<uint8_t>(jpeg[offset + 1U]));
            if (length < 2U || length > jpeg.size() - offset) {
                return false;
            }
            const size_t payload_offset = offset + 2U;
            const size_t payload_size   = static_cast<size_t>(length) - 2U;
            if (marker == 0xE1U && payload_size >= prefix_size) {
                bool prefix_matches = true;
                for (size_t index = 0U; index < prefix_size; ++index) {
                    if (jpeg[payload_offset + index]
                        != static_cast<std::byte>(kJpegXmpPrefix[index])) {
                        prefix_matches = false;
                        break;
                    }
                }
                if (prefix_matches) {
                    *packet = jpeg.subspan(payload_offset + prefix_size,
                                           payload_size - prefix_size);
                    return !packet->empty();
                }
            }
            offset += length;
        }
        return false;
    }

    struct CustomStoreWriter final {
        MetaStore* store = nullptr;
        std::array<BlockId, 2U> blocks { kInvalidBlockId, kInvalidBlockId };
        std::array<uint32_t, 2U> next_order {};
        uint32_t next_slot = 0U;
        bool mixed_origins = false;
    };

    struct ExpectedCustomItem final {
        std::string_view schema_ns;
        std::array<char, 128U> path_storage {};
        uint32_t path_size              = 0U;
        XmpContainerKind container_kind = XmpContainerKind::None;
        std::string_view text;

        std::string_view path_view() const noexcept
        {
            return std::string_view(path_storage.data(), path_size);
        }
    };

    static constexpr size_t kExpectedCustomItemCapacity = 24U;

    struct ExpectedCustomSet final {
        std::array<ExpectedCustomItem, kExpectedCustomItemCapacity> items {};
        uint32_t count = 0U;
    };

    enum class CustomSourceRootMode : uint8_t {
        Populated,
        Empty,
        Scalar,
    };

    static BlockId custom_next_origin(CustomStoreWriter* writer,
                                      uint32_t* order) noexcept
    {
        const uint32_t slot = writer->mixed_origins ? writer->next_slot++ & 1U
                                                    : 0U;
        *order              = writer->next_order[slot]++;
        return writer->blocks[slot];
    }

    static EntryId add_custom_container(CustomStoreWriter* writer,
                                        std::string_view schema_ns,
                                        std::string_view path,
                                        XmpContainerKind kind)
    {
        const MetadataAuthoringEntry declaration
            = make_xmp_container_entry(schema_ns, path, kind);
        uint32_t order = 0U;
        Entry entry;
        entry.key = make_xmp_property_key(writer->store->arena(), schema_ns,
                                          path);
        entry.origin.block          = custom_next_origin(writer, &order);
        entry.origin.order_in_block = order;
        entry.origin.wire_type      = declaration.wire_type;
        entry.origin.wire_count     = declaration.wire_count;
        return writer->store->add_entry(entry);
    }

    static EntryId add_custom_text(CustomStoreWriter* writer,
                                   std::string_view schema_ns,
                                   std::string_view path, std::string_view text)
    {
        uint32_t order = 0U;
        Entry entry;
        entry.key   = make_xmp_property_key(writer->store->arena(), schema_ns,
                                            path);
        entry.value = make_text(writer->store->arena(), text,
                                TextEncoding::Utf8);
        entry.origin.block          = custom_next_origin(writer, &order);
        entry.origin.order_in_block = order;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0U };
        entry.origin.wire_count     = static_cast<uint32_t>(text.size());
        return writer->store->add_entry(entry);
    }

    static void add_expected_custom_item(ExpectedCustomSet* expected,
                                         std::string_view schema_ns,
                                         std::string_view path,
                                         XmpContainerKind container_kind,
                                         std::string_view text) noexcept
    {
        require(expected->count < expected->items.size());
        require(path.size() <= expected->items[0U].path_storage.size());
        ExpectedCustomItem& item = expected->items[expected->count++];
        item.schema_ns           = schema_ns;
        item.path_size           = static_cast<uint32_t>(path.size());
        for (uint32_t index = 0U; index < item.path_size; ++index) {
            item.path_storage[index] = path[index];
        }
        item.container_kind = container_kind;
        item.text           = text;
    }

    static void add_custom_container_expected(CustomStoreWriter* writer,
                                              ExpectedCustomSet* expected,
                                              std::string_view schema_ns,
                                              std::string_view path,
                                              XmpContainerKind kind)
    {
        require(add_custom_container(writer, schema_ns, path, kind)
                != kInvalidEntryId);
        add_expected_custom_item(expected, schema_ns, path, kind, {});
    }

    static void add_custom_text_expected(CustomStoreWriter* writer,
                                         ExpectedCustomSet* expected,
                                         std::string_view schema_ns,
                                         std::string_view authored_path,
                                         std::string_view readback_path,
                                         std::string_view text)
    {
        require(add_custom_text(writer, schema_ns, authored_path, text)
                != kInvalidEntryId);
        add_expected_custom_item(expected, schema_ns, readback_path,
                                 XmpContainerKind::None, text);
    }

    static bool is_b39_custom_namespace(std::string_view schema_ns) noexcept
    {
        return schema_ns.starts_with(kB39CustomNamespacePrefix);
    }

    static bool is_shared_custom_root(const ExpectedCustomItem& item) noexcept
    {
        constexpr std::string_view kRoot = "Record";
        const std::string_view path      = item.path_view();
        if (item.schema_ns != kB39SharedNamespace || !path.starts_with(kRoot)) {
            return false;
        }
        return path.size() == kRoot.size() || path[kRoot.size()] == '/'
               || path[kRoot.size()] == '[';
    }

    static void append_expected_custom_items(ExpectedCustomSet* destination,
                                             const ExpectedCustomSet& source,
                                             bool include_shared_root)
    {
        for (uint32_t index = 0U; index < source.count; ++index) {
            const ExpectedCustomItem& item = source.items[index];
            if (is_shared_custom_root(item) != include_shared_root) {
                continue;
            }
            require(destination->count < destination->items.size());
            destination->items[destination->count++] = item;
        }
    }

    static bool
    expected_custom_set_matches(const MetaStore& store,
                                const ExpectedCustomSet& expected) noexcept
    {
        std::array<bool, kExpectedCustomItemCapacity> seen {};
        uint32_t actual_count = 0U;
        for (const Entry& entry : store.entries()) {
            if (entry.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }
            const std::string_view schema_ns
                = arena_string(store, entry.key.data.xmp_property.schema_ns);
            if (!is_b39_custom_namespace(schema_ns)) {
                continue;
            }
            const std::string_view path
                = arena_string(store,
                               entry.key.data.xmp_property.property_path);
            bool found = false;
            for (uint32_t index = 0U; index < expected.count; ++index) {
                const ExpectedCustomItem& wanted = expected.items[index];
                if (schema_ns != wanted.schema_ns
                    || path != wanted.path_view()) {
                    continue;
                }
                if (seen[index]) {
                    return false;
                }
                if (wanted.container_kind == XmpContainerKind::None) {
                    if (xmp_container_kind(entry) != XmpContainerKind::None
                        || !text_equals(store, entry, wanted.text)) {
                        return false;
                    }
                } else if (xmp_container_kind(entry) != wanted.container_kind
                           || entry.value.kind != MetaValueKind::Empty) {
                    return false;
                }
                seen[index] = true;
                found       = true;
                break;
            }
            if (!found) {
                return false;
            }
            actual_count += 1U;
        }
        if (actual_count != expected.count) {
            return false;
        }
        for (uint32_t index = 0U; index < expected.count; ++index) {
            if (!seen[index]) {
                return false;
            }
        }
        return true;
    }

    static bool
    has_multiple_custom_origin_blocks(const MetaStore& store) noexcept
    {
        BlockId first_block = kInvalidBlockId;
        for (const Entry& entry : store.entries()) {
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || !is_b39_custom_namespace(
                    arena_string(store,
                                 entry.key.data.xmp_property.schema_ns))) {
                continue;
            }
            if (first_block == kInvalidBlockId) {
                first_block = entry.origin.block;
            } else if (entry.origin.block != first_block) {
                return true;
            }
        }
        return false;
    }

    static void init_custom_store_writer(MetaStore* store, bool mixed_origins,
                                         CustomStoreWriter* writer)
    {
        store->constrain_resources(kMaxEntries, kMaxArenaBytes);
        writer->store         = store;
        writer->mixed_origins = mixed_origins;
        writer->blocks[0U]    = store->add_block(BlockInfo {});
        require(writer->blocks[0U] != kInvalidBlockId);
        if (mixed_origins) {
            writer->blocks[1U] = store->add_block(BlockInfo {});
            require(writer->blocks[1U] != kInvalidBlockId);
        }
    }

    static void build_custom_tree_items(
        CustomStoreWriter* writer, ExpectedCustomSet* expected,
        bool source_provider, XmpContainerKind kind, bool repeated_text,
        uint32_t first_sparse_index, uint32_t second_sparse_index)
    {
        const std::string_view first_value  = source_provider
                                                  ? "source_item_first"
                                                  : "destination_item_first";
        const std::string_view second_value = source_provider
                                                  ? "source_item_second"
                                                  : "destination_item_second";
        const std::string_view repeated_value
            = source_provider ? "source_repeated_text"
                              : "destination_repeated_text";
        if (kind == XmpContainerKind::Struct) {
            add_custom_text_expected(writer, expected, kB39SharedNamespace,
                                     "Record/Label", "Record/Label",
                                     source_provider ? "source_label"
                                                     : "destination_label");
            add_custom_container_expected(writer, expected, kB39SharedNamespace,
                                          "Record/Items",
                                          XmpContainerKind::Seq);
            std::array<char, 32U> first_path_storage {};
            std::array<char, 32U> second_path_storage {};
            const std::string_view first_path
                = make_indexed_path(&first_path_storage, "Record/Items",
                                    first_sparse_index);
            const std::string_view second_path
                = make_indexed_path(&second_path_storage, "Record/Items",
                                    second_sparse_index);
            add_custom_text_expected(writer, expected, kB39SharedNamespace,
                                     first_path, "Record/Items[1]",
                                     repeated_text ? repeated_value
                                                   : first_value);
            add_custom_text_expected(writer, expected, kB39SharedNamespace,
                                     second_path, "Record/Items[2]",
                                     repeated_text ? repeated_value
                                                   : second_value);
            return;
        }
        if (kind == XmpContainerKind::Bag || kind == XmpContainerKind::Seq) {
            std::array<char, 32U> first_path_storage {};
            std::array<char, 32U> second_path_storage {};
            const std::string_view first_path
                = make_indexed_path(&first_path_storage, "Record",
                                    first_sparse_index);
            const std::string_view second_path
                = make_indexed_path(&second_path_storage, "Record",
                                    second_sparse_index);
            add_custom_text_expected(writer, expected, kB39SharedNamespace,
                                     first_path, "Record[1]",
                                     repeated_text ? repeated_value
                                                   : first_value);
            add_custom_text_expected(writer, expected, kB39SharedNamespace,
                                     second_path, "Record[2]",
                                     repeated_text ? repeated_value
                                                   : second_value);
            return;
        }
        require(kind == XmpContainerKind::LangAlt);
        const std::string_view first_language_path
            = source_provider ? "Record[@xml:lang=en-US]"
                              : "Record[@xml:lang=fr-FR]";
        const std::string_view default_language_path
            = "Record[@xml:lang=x-default]";
        add_custom_text_expected(writer, expected, kB39SharedNamespace,
                                 first_language_path, first_language_path,
                                 repeated_text ? repeated_value : first_value);
        add_custom_text_expected(writer, expected, kB39SharedNamespace,
                                 default_language_path, default_language_path,
                                 repeated_text ? repeated_value : second_value);
    }

    static void build_custom_provider_store(
        MetaStore* store, bool source_provider, XmpContainerKind selected_kind,
        CustomSourceRootMode source_mode, bool repeated_text,
        uint32_t first_sparse_index, uint32_t second_sparse_index,
        ExpectedCustomSet* expected)
    {
        CustomStoreWriter writer;
        init_custom_store_writer(store, source_provider, &writer);
        if (source_provider && source_mode == CustomSourceRootMode::Scalar) {
            add_custom_text_expected(&writer, expected, kB39SharedNamespace,
                                     "Record", "Record", "source_root_scalar");
        } else {
            add_custom_container_expected(&writer, expected,
                                          kB39SharedNamespace, "Record",
                                          selected_kind);
            if (!source_provider
                || source_mode == CustomSourceRootMode::Populated) {
                build_custom_tree_items(&writer, expected, source_provider,
                                        selected_kind, repeated_text,
                                        first_sparse_index,
                                        second_sparse_index);
            }
        }

        const std::string_view provider_namespace
            = source_provider ? kB39SourceNamespace : kB39DestinationNamespace;
        add_custom_container_expected(&writer, expected, provider_namespace,
                                      "ProviderRoot", XmpContainerKind::Struct);
        add_custom_text_expected(&writer, expected, provider_namespace,
                                 "ProviderRoot/Value", "ProviderRoot/Value",
                                 source_provider
                                     ? "source_independent_root"
                                     : "destination_independent_root");
        store->finalize();
    }

    static void fuzz_custom_xmp_root_merge(std::span<const uint8_t> input)
    {
        static constexpr std::array<XmpContainerKind, 4U> kKinds = {
            XmpContainerKind::Struct,
            XmpContainerKind::Bag,
            XmpContainerKind::Seq,
            XmpContainerKind::LangAlt,
        };
        const XmpContainerKind selected_kind
            = kKinds[input_byte(input, 5U) % kKinds.size()];
        const XmpContainerKind destination_kind
            = kKinds[input_byte(input, 10U) % kKinds.size()];
        const uint8_t source_mode_selector = input_byte(input, 6U) % 3U;
        CustomSourceRootMode source_mode   = CustomSourceRootMode::Populated;
        if (source_mode_selector == 1U) {
            source_mode = CustomSourceRootMode::Empty;
        } else if (source_mode_selector == 2U) {
            source_mode = CustomSourceRootMode::Scalar;
        }
        const bool repeated_text           = (input_byte(input, 7U) & 1U) != 0U;
        const uint32_t source_first_index  = 2U + (input_byte(input, 8U) % 3U);
        const uint32_t source_second_index = source_first_index + 5U;
        const uint32_t destination_first_index = 12U
                                                 + (input_byte(input, 9U) % 3U);
        const uint32_t destination_second_index = destination_first_index + 5U;

        MetaStore source;
        MetaStore destination;
        ExpectedCustomSet source_expected;
        ExpectedCustomSet destination_expected;
        build_custom_provider_store(&source, true, selected_kind, source_mode,
                                    repeated_text, source_first_index,
                                    source_second_index, &source_expected);
        build_custom_provider_store(&destination, false, destination_kind,
                                    CustomSourceRootMode::Populated,
                                    repeated_text, destination_first_index,
                                    destination_second_index,
                                    &destination_expected);
        require(source.entries().size() <= kMaxEntries
                && source.arena().bytes().size() <= kMaxArenaBytes
                && destination.entries().size() <= kMaxEntries
                && destination.arena().bytes().size() <= kMaxArenaBytes);
        require(has_multiple_custom_origin_blocks(source));

        const std::vector<Entry> source_entries_before(source.entries().begin(),
                                                       source.entries().end());
        const std::vector<std::byte> source_arena_before(
            source.arena().bytes().begin(), source.arena().bytes().end());
        const uint32_t source_block_count_before = source.block_count();
        const std::vector<Entry> destination_entries_before(
            destination.entries().begin(), destination.entries().end());
        const std::vector<std::byte> destination_arena_before(
            destination.arena().bytes().begin(),
            destination.arena().bytes().end());
        const uint32_t destination_block_count_before
            = destination.block_count();

        std::vector<std::byte> source_packet;
        std::vector<std::byte> destination_packet;
        require(render_custom_xmp(source, &source_packet));
        require(render_custom_xmp(destination, &destination_packet));
        std::vector<std::byte> source_jpeg;
        std::vector<std::byte> target_jpeg;
        require(make_jpeg_with_xmp(source_packet, &source_jpeg));
        require(make_jpeg_with_xmp(destination_packet, &target_jpeg));
        require(source_jpeg.size() <= kMaxInputBytes
                && target_jpeg.size() <= kMaxInputBytes);
        const std::vector<std::byte> source_jpeg_before = source_jpeg;
        const std::vector<std::byte> target_jpeg_before = target_jpeg;
        std::span<const std::byte> source_carrier_packet;
        std::span<const std::byte> target_carrier_packet;
        require(jpeg_existing_xmp(source_jpeg, &source_carrier_packet)
                && bytes_equal(source_carrier_packet, source_packet));
        require(jpeg_existing_xmp(target_jpeg, &target_carrier_packet)
                && bytes_equal(target_carrier_packet, destination_packet));

        MetaStore source_readback;
        MetaStore destination_readback;
        require(decode_custom_xmp(source_carrier_packet, &source_readback));
        require(
            decode_custom_xmp(target_carrier_packet, &destination_readback));
        require(expected_custom_set_matches(source_readback, source_expected));
        require(expected_custom_set_matches(destination_readback,
                                            destination_expected));

        const TransferSourceSnapshot snapshot = build_transfer_source_snapshot(
            source);
        require(snapshot.store.is_finalized()
                && snapshot.store.entries().size() <= kMaxEntries
                && snapshot.store.arena().bytes().size() <= kMaxArenaBytes
                && has_multiple_custom_origin_blocks(snapshot.store));
        const std::vector<Entry> snapshot_entries_before(
            snapshot.store.entries().begin(), snapshot.store.entries().end());
        const std::vector<std::byte> snapshot_arena_before(
            snapshot.store.arena().bytes().begin(),
            snapshot.store.arena().bytes().end());
        const uint32_t snapshot_block_count_before
            = snapshot.store.block_count();

        constexpr std::array<XmpExistingDestinationEmbeddedPrecedence, 2U>
            kPrecedences = {
                XmpExistingDestinationEmbeddedPrecedence::SourceWins,
                XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
            };
        for (const XmpExistingDestinationEmbeddedPrecedence precedence :
             kPrecedences) {
            ExecutePreparedTransferSnapshotOptions options;
            options.prepare.target_format        = TransferTargetFormat::Jpeg;
            options.prepare.include_exif_app1    = false;
            options.prepare.include_icc_app2     = false;
            options.prepare.include_iptc_app13   = false;
            options.prepare.xmp_project_exif     = false;
            options.prepare.xmp_project_iptc     = false;
            options.prepare.xmp_include_existing = true;
            options.prepare.xmp_existing_namespace_policy
                = XmpExistingNamespacePolicy::PreserveCustom;
            options.prepare.xmp_conflict_policy
                = XmpConflictPolicy::ExistingWins;
            options.execute.edit_apply             = true;
            options.preserve_custom_xmp_structures = true;
            options.xmp_existing_destination_embedded_mode
                = XmpExistingDestinationEmbeddedMode::MergeIfPresent;
            options.xmp_existing_destination_embedded_precedence = precedence;

            const ExecutePreparedTransferFileResult result
                = execute_prepared_transfer_snapshot(snapshot, target_jpeg,
                                                     options);
            require(result.xmp_existing_destination_embedded_loaded);
            require(result.prepared.prepare.status == TransferStatus::Ok);
            require(result.execute.edit_plan_status == TransferStatus::Ok);
            require(result.execute.edit_apply.status == TransferStatus::Ok);
            require(!result.execute.edited_output.empty()
                    && result.execute.edited_output.size() <= kMaxOutputBytes);

            std::span<const std::byte> output_packet;
            require(jpeg_existing_xmp(result.execute.edited_output,
                                      &output_packet));
            MetaStore readback;
            require(decode_custom_xmp(output_packet, &readback));

            const bool source_wins
                = precedence
                  == XmpExistingDestinationEmbeddedPrecedence::SourceWins;
            ExpectedCustomSet expected_merged;
            append_expected_custom_items(&expected_merged, source_expected,
                                         false);
            append_expected_custom_items(&expected_merged, destination_expected,
                                         false);
            append_expected_custom_items(&expected_merged,
                                         source_wins ? source_expected
                                                     : destination_expected,
                                         true);
            require(expected_custom_set_matches(readback, expected_merged));
            require(readback.entries().size() <= kMaxEntries
                    && readback.arena().bytes().size() <= kMaxArenaBytes);
        }
        require(source_jpeg == source_jpeg_before
                && target_jpeg == target_jpeg_before);
        require(store_unchanged(source, source_entries_before,
                                source_arena_before,
                                source_block_count_before));
        require(store_unchanged(destination, destination_entries_before,
                                destination_arena_before,
                                destination_block_count_before));
        require(store_unchanged(snapshot.store, snapshot_entries_before,
                                snapshot_arena_before,
                                snapshot_block_count_before));
    }

    static void fuzz_dc_provider_merge(std::span<const uint8_t> input)
    {
        MetaStore source;
        source.constrain_resources(kMaxEntries, kMaxArenaBytes);
        const BlockId source_block_a = source.add_block(BlockInfo {});
        const BlockId source_block_b = source.add_block(BlockInfo {});
        require(source_block_a != kInvalidBlockId
                && source_block_b != kInvalidBlockId);

        const uint32_t first_index  = 3U + (input_byte(input, 0U) & 3U);
        const uint32_t second_index = first_index + 2U;
        const bool repeated_creator = (input_byte(input, 1U) & 1U) != 0U;
        const std::string_view source_creator_first
            = repeated_creator ? "source_repeated_creator"
                               : "source_creator_first";
        const std::string_view source_creator_second
            = repeated_creator ? "source_repeated_creator"
                               : "source_creator_second";
        constexpr std::array<std::string_view, 2U> kSourceSubjectValues = {
            "source_repeated_keyword",
            "source_repeated_keyword",
        };
        constexpr std::array<std::string_view, 2U> kDestinationCreatorValues = {
            "destination_creator_first",
            "destination_creator_second",
        };
        constexpr std::array<std::string_view, 2U> kDestinationSubjectValues = {
            "destination_repeated_keyword",
            "destination_repeated_keyword",
        };

        std::array<char, 32U> source_creator_first_path {};
        std::array<char, 32U> source_creator_second_path {};
        std::array<char, 32U> source_subject_first_path {};
        std::array<char, 32U> source_subject_second_path {};
        const std::string_view source_creator_first_path_view
            = make_indexed_path(&source_creator_first_path, "creator",
                                first_index);
        const std::string_view source_creator_second_path_view
            = make_indexed_path(&source_creator_second_path, "creator",
                                second_index);
        const std::string_view source_subject_first_path_view
            = make_indexed_path(&source_subject_first_path, "subject",
                                first_index);
        const std::string_view source_subject_second_path_view
            = make_indexed_path(&source_subject_second_path, "subject",
                                second_index);

        require(add_dc_text(&source, source_block_b,
                            source_creator_first_path_view,
                            source_creator_first, 0U)
                != kInvalidEntryId);
        require(add_dc_text(&source, source_block_a,
                            source_creator_second_path_view,
                            source_creator_second, 0U)
                != kInvalidEntryId);
        require(add_dc_text(&source, source_block_b,
                            source_subject_first_path_view,
                            kSourceSubjectValues[0U], 1U)
                != kInvalidEntryId);
        require(add_dc_text(&source, source_block_a,
                            source_subject_second_path_view,
                            kSourceSubjectValues[1U], 1U)
                != kInvalidEntryId);
        source.finalize();

        const std::vector<Entry> source_entries_before(source.entries().begin(),
                                                       source.entries().end());
        const std::vector<std::byte> source_arena_before(
            source.arena().bytes().begin(), source.arena().bytes().end());
        const uint32_t source_block_count_before = source.block_count();

        std::vector<std::byte> source_packet;
        require(render_xmp(source, &source_packet));
        MetaStore direct_readback;
        require(decode_xmp(source_packet, &direct_readback));
        const std::array<std::string_view, 2U> source_creator_values = {
            source_creator_first,
            source_creator_second,
        };
        require(readback_collection_matches(direct_readback, "creator",
                                            source_creator_values));
        require(readback_collection_matches(direct_readback, "subject",
                                            kSourceSubjectValues));
        require(store_unchanged(source, source_entries_before,
                                source_arena_before,
                                source_block_count_before));

        MetaStore destination;
        destination.constrain_resources(kMaxEntries, kMaxArenaBytes);
        const BlockId destination_block = destination.add_block(BlockInfo {});
        require(destination_block != kInvalidBlockId);
        require(add_dc_text(&destination, destination_block, "creator[1]",
                            kDestinationCreatorValues[0U], 0U)
                != kInvalidEntryId);
        require(add_dc_text(&destination, destination_block, "creator[2]",
                            kDestinationCreatorValues[1U], 1U)
                != kInvalidEntryId);
        require(add_dc_text(&destination, destination_block, "subject[1]",
                            kDestinationSubjectValues[0U], 2U)
                != kInvalidEntryId);
        require(add_dc_text(&destination, destination_block, "subject[2]",
                            kDestinationSubjectValues[1U], 3U)
                != kInvalidEntryId);
        destination.finalize();

        std::vector<std::byte> destination_packet;
        require(render_xmp(destination, &destination_packet));
        std::vector<std::byte> target_jpeg;
        require(make_jpeg_with_xmp(destination_packet, &target_jpeg));
        const std::vector<std::byte> target_jpeg_before = target_jpeg;

        const TransferSourceSnapshot snapshot = build_transfer_source_snapshot(
            source);
        require(snapshot.store.is_finalized());
        require(snapshot.store.entries().size() <= kMaxEntries);
        require(snapshot.store.arena().bytes().size() <= kMaxArenaBytes);

        constexpr std::array<XmpExistingDestinationEmbeddedPrecedence, 2U>
            kPrecedences = {
                XmpExistingDestinationEmbeddedPrecedence::SourceWins,
                XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
            };
        for (const XmpExistingDestinationEmbeddedPrecedence precedence :
             kPrecedences) {
            ExecutePreparedTransferSnapshotOptions options;
            options.prepare.target_format        = TransferTargetFormat::Jpeg;
            options.prepare.include_exif_app1    = false;
            options.prepare.include_icc_app2     = false;
            options.prepare.include_iptc_app13   = false;
            options.prepare.xmp_include_existing = true;
            options.prepare.xmp_conflict_policy
                = XmpConflictPolicy::ExistingWins;
            options.execute.edit_apply = true;
            options.xmp_existing_destination_embedded_mode
                = XmpExistingDestinationEmbeddedMode::MergeIfPresent;
            options.xmp_existing_destination_embedded_precedence = precedence;

            const ExecutePreparedTransferFileResult result
                = execute_prepared_transfer_snapshot(snapshot, target_jpeg,
                                                     options);
            require(result.xmp_existing_destination_embedded_loaded);
            require(result.prepared.prepare.status == TransferStatus::Ok);
            require(result.execute.edit_plan_status == TransferStatus::Ok);
            require(result.execute.edit_apply.status == TransferStatus::Ok);
            require(!result.execute.edited_output.empty()
                    && result.execute.edited_output.size() <= kMaxOutputBytes);

            std::span<const std::byte> output_packet;
            require(jpeg_existing_xmp(result.execute.edited_output,
                                      &output_packet));
            MetaStore readback;
            require(decode_xmp(output_packet, &readback));
            const bool source_wins
                = precedence
                  == XmpExistingDestinationEmbeddedPrecedence::SourceWins;
            const std::array<std::string_view, 2U>& creator_values
                = source_wins ? source_creator_values
                              : kDestinationCreatorValues;
            const std::array<std::string_view, 2U>& subject_values
                = source_wins ? kSourceSubjectValues
                              : kDestinationSubjectValues;
            require(readback_collection_matches(readback, "creator",
                                                creator_values));
            require(readback_collection_matches(readback, "subject",
                                                subject_values));
        }
        require(target_jpeg == target_jpeg_before);
        require(store_unchanged(source, source_entries_before,
                                source_arena_before,
                                source_block_count_before));
    }

}  // namespace
}  // namespace openmeta

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (data == nullptr || size > openmeta::kMaxInputBytes) {
        return 0;
    }
    openmeta::fuzz_dc_provider_merge(std::span<const uint8_t>(data, size));
    openmeta::fuzz_custom_xmp_root_merge(std::span<const uint8_t>(data, size));
    switch (openmeta::input_byte(std::span<const uint8_t>(data, size), 2U)
            % 3U) {
    case 0U:
        openmeta::fuzz_icc_jpeg_removal(std::span<const uint8_t>(data, size));
        break;
    case 1U:
        openmeta::fuzz_icc_tiff_removal(false,
                                        std::span<const uint8_t>(data, size));
        break;
    case 2U:
        openmeta::fuzz_icc_tiff_removal(true,
                                        std::span<const uint8_t>(data, size));
        break;
    default: openmeta::fuzz_trap();
    }
    return 0;
}
