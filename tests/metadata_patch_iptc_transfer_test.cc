// SPDX-License-Identifier: Apache-2.0

#include "openmeta/iptc_iim_decode.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_patch.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_translation.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace openmeta {
namespace {

    static constexpr std::string_view kDcNamespace
        = "http://purl.org/dc/elements/1.1/";
    static constexpr std::string_view kXapNamespace
        = "http://ns.adobe.com/xap/1.0/";
    static constexpr std::string_view kTitlePath = "title[@xml:lang=x-default]";
    static constexpr std::string_view kMakeText  = "Maker01";
    static constexpr std::string_view kEditedTitle  = "Edited!";
    static constexpr std::string_view kPatchedTitle = "Patched";
    static constexpr std::string_view kOldLabel     = "0000000001";
    static constexpr std::string_view kNewLabel     = "0000000042";

    static constexpr std::array<MetadataPatchPayload, 3U> kPayloadFamilies = {
        MetadataPatchPayload::ExifTiff,
        MetadataPatchPayload::Xmp,
        MetadataPatchPayload::IptcIim,
    };

    static bool same_bytes(std::span<const std::byte> first,
                           std::span<const std::byte> second) noexcept
    {
        return first.size() == second.size()
               && (first.empty()
                   || std::memcmp(first.data(), second.data(), first.size())
                          == 0);
    }

    static std::span<const std::byte> bytes_of(std::string_view text) noexcept
    {
        return std::as_bytes(std::span<const char>(text.data(), text.size()));
    }

    static std::string_view value_text(const MetaStore& store,
                                       const Entry& entry) noexcept
    {
        if (entry.value.kind != MetaValueKind::Text
            && entry.value.kind != MetaValueKind::Bytes) {
            return {};
        }
        const std::span<const std::byte> bytes = store.arena().span(
            entry.value.data.span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static MetadataPatchValueSpec exact_shape(const MetaValue& value) noexcept
    {
        MetadataPatchValueSpec spec;
        spec.kind          = value.kind;
        spec.elem_type     = value.elem_type;
        spec.text_encoding = value.text_encoding;
        spec.count         = value.count;
        return spec;
    }

    static MetaValueView
    fixed_payload_update(const MetaValue& selected,
                         std::string_view replacement) noexcept
    {
        if (replacement.size() != selected.count) {
            return {};
        }
        if (selected.kind == MetaValueKind::Bytes) {
            return make_value_view_bytes(bytes_of(replacement));
        }
        if (selected.kind == MetaValueKind::Text) {
            return make_value_view_text(replacement, selected.text_encoding);
        }
        return {};
    }

    struct PatchLifecycle final {
        MetaStore source;
        MetaStore logically_edited;
        MetaStore translated;
        MetadataAuthoringResult authoring;
        MetadataEditingResult editing;
        MetadataDescriptiveTranslationResult translation;
        std::array<MetadataPatchRequest, 3U> requests {};
        std::array<MetadataPatchHandle, 3U> handles {};
        PreparedMetadataPatchPlan plan;
        PreparedMetadataPatchInstance worker;
        MetadataPatchResult preparation;
        MetadataPatchResult worker_creation;
        MetadataPatchResult patching;
        std::array<std::vector<std::byte>, 3U> worker_before;
    };

    static bool build_patch_lifecycle(PatchLifecycle* state, uint64_t plan_id)
    {
        if (state == nullptr) {
            return false;
        }

        std::array<MetadataAuthoringEntry, 3U> authored {};
        authored[0U].key   = make_exif_tag_key_view("ifd0", 0x010FU);
        authored[0U].value = make_value_view_text(kMakeText,
                                                  TextEncoding::Ascii);
        authored[1U].key = make_xmp_property_key_view(kDcNamespace, kTitlePath);
        authored[1U].value = make_value_view_text("Before!",
                                                  TextEncoding::Utf8);
        authored[2U].key   = make_xmp_property_key_view(kXapNamespace, "Label");
        authored[2U].value = make_value_view_text(kOldLabel,
                                                  TextEncoding::Utf8);
        state->authoring   = create_metadata_store(authored, &state->source);
        if (!state->authoring.ok()) {
            return false;
        }

        const std::array<MetadataEditingOperation, 1U> edits = {
            make_metadata_edit_set(
                make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                            kEditedTitle)),
        };
        MetadataEditingRequest edit_request;
        edit_request.operations = edits;
        state->editing          = edit_metadata(state->source, edit_request,
                                                &state->logically_edited);
        if (state->editing.status != MetadataEditingStatus::Ok) {
            return false;
        }

        MetadataIptcTranslationOptions translation_options;
        translation_options.source_mode
            = MetadataDescriptiveTranslationSourceMode::All;
        state->translation = translate_xmp_iptc_metadata(
            state->logically_edited, translation_options, &state->translated);
        if (state->translation.status
            != MetadataDescriptiveTranslationStatus::Ok) {
            return false;
        }

        const std::span<const EntryId> native_title_ids
            = state->translated.find_all(make_iptc_dataset_key_view(2U, 5U));
        if (native_title_ids.size() != 1U) {
            return false;
        }
        const Entry& native_title = state->translated.entry(
            native_title_ids.front());

        state->requests[0U].key = make_exif_tag_key_view("ifd0", 0x010FU);
        const std::span<const EntryId> make_ids = state->translated.find_all(
            state->requests[0U].key);
        if (make_ids.size() != 1U) {
            return false;
        }
        state->requests[0U].expected = exact_shape(
            state->translated.entry(make_ids.front()).value);

        state->requests[1U].key = make_xmp_property_key_view(kXapNamespace,
                                                             "Label");
        const std::span<const EntryId> label_ids = state->translated.find_all(
            state->requests[1U].key);
        if (label_ids.size() != 1U) {
            return false;
        }
        state->requests[1U].expected = exact_shape(
            state->translated.entry(label_ids.front()).value);
        state->requests[1U].escaped_width = static_cast<uint32_t>(
            kOldLabel.size());

        state->requests[2U].key      = make_iptc_dataset_key_view(2U, 5U);
        state->requests[2U].expected = exact_shape(native_title.value);

        MetadataPatchPlanOptions plan_options;
        plan_options.plan_id = plan_id;
        state->preparation
            = prepare_metadata_patch_plan(state->translated, state->requests,
                                          plan_options, state->handles,
                                          &state->plan);
        if (!state->preparation.ok()) {
            return false;
        }

        state->worker_creation
            = create_prepared_metadata_patch_instance(state->plan,
                                                      &state->worker);
        if (!state->worker_creation.ok()) {
            return false;
        }

        for (size_t index = 0U; index < kPayloadFamilies.size(); ++index) {
            const std::span<const std::byte> payload = state->worker.payload(
                kPayloadFamilies[index]);
            if (payload.empty()) {
                return false;
            }
            state->worker_before[index].assign(payload.begin(), payload.end());
        }

        const MetaValueView native_title_update
            = fixed_payload_update(native_title.value, kPatchedTitle);
        if (native_title_update.kind == MetaValueKind::Empty) {
            return false;
        }
        const std::array<MetadataPatchUpdate, 3U> updates = { {
            { state->handles[0U],
              make_value_view_text("Maker02", TextEncoding::Ascii) },
            { state->handles[1U],
              make_value_view_text(kNewLabel, TextEncoding::Utf8) },
            { state->handles[2U], native_title_update },
        } };
        state->patching = patch_prepared_metadata_instance(&state->worker,
                                                           updates);
        return state->patching.ok();
    }

    struct ReplayCapture final {
        std::array<MetadataPatchPayload, 3U> families {};
        std::array<std::span<const std::byte>, 3U> payloads {};
        uint32_t count = 0U;
    };

    static bool capture_replay(void* user, MetadataPatchPayload family,
                               std::span<const std::byte> payload) noexcept
    {
        if (user == nullptr) {
            return false;
        }
        ReplayCapture* capture = static_cast<ReplayCapture*>(user);
        if (capture->count >= capture->families.size()) {
            return false;
        }
        const size_t index       = capture->count;
        capture->families[index] = family;
        capture->payloads[index] = payload;
        ++capture->count;
        return true;
    }

    static void append_bytes(std::vector<std::byte>* output,
                             std::span<const std::byte> bytes)
    {
        output->insert(output->end(), bytes.begin(), bytes.end());
    }

    static void append_be32(std::vector<std::byte>* output, uint32_t value)
    {
        for (uint32_t shift = 24U;; shift -= 8U) {
            output->push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
            if (shift == 0U) {
                break;
            }
        }
    }

    static bool add_payload_block(PreparedTransferBundle* bundle,
                                  TransferBlockKind kind, uint32_t order,
                                  std::string_view route,
                                  std::span<const std::byte> prefix,
                                  std::span<const std::byte> payload,
                                  bool append_odd_pad = false)
    {
        if (bundle == nullptr || prefix.size() > SIZE_MAX - payload.size()
            || (append_odd_pad && ((prefix.size() + payload.size()) & 1U) != 0U
                && prefix.size() + payload.size() == SIZE_MAX)) {
            return false;
        }
        PreparedTransferBlock block;
        block.kind  = kind;
        block.order = order;
        block.route.assign(route.data(), route.size());
        block.payload.reserve(prefix.size() + payload.size()
                              + (append_odd_pad ? 1U : 0U));
        append_bytes(&block.payload, prefix);
        append_bytes(&block.payload, payload);
        if (append_odd_pad && (block.payload.size() & 1U) != 0U) {
            block.payload.push_back(std::byte { 0U });
        }
        bundle->blocks.push_back(std::move(block));
        return true;
    }

    static bool make_prepared_bundle(TransferTargetFormat target,
                                     const ReplayCapture& capture,
                                     PreparedTransferBundle* bundle)
    {
        if (bundle == nullptr || capture.count != kPayloadFamilies.size()) {
            return false;
        }
        for (size_t index = 0U; index < kPayloadFamilies.size(); ++index) {
            if (capture.families[index] != kPayloadFamilies[index]
                || capture.payloads[index].empty()) {
                return false;
            }
        }

        bundle->target_format = target;
        if (target == TransferTargetFormat::Dng) {
            bundle->dng_target_mode = DngTargetMode::ExistingTarget;
        }
        const bool jpeg = target == TransferTargetFormat::Jpeg;
        static constexpr char kExifHeader[] = "Exif\0\0";
        static constexpr char kXmpHeader[]  = "http://ns.adobe.com/xap/1.0/";
        static constexpr char kPhotoshopHeader[]     = "Photoshop 3.0";
        static constexpr char kEightBim[]            = "8BIM";
        const std::span<const std::byte> exif_prefix = std::as_bytes(
            std::span<const char>(kExifHeader, sizeof(kExifHeader) - 1U));
        const std::span<const std::byte> xmp_prefix
            = jpeg ? std::as_bytes(
                         std::span<const char>(kXmpHeader, sizeof(kXmpHeader)))
                   : std::span<const std::byte>();
        if (!add_payload_block(bundle, TransferBlockKind::Exif, 0U,
                               jpeg ? "jpeg:app1-exif" : "tiff:ifd-exif-app1",
                               exif_prefix, capture.payloads[0U])) {
            return false;
        }
        if (!add_payload_block(bundle, TransferBlockKind::Xmp, 1U,
                               jpeg ? "jpeg:app1-xmp" : "tiff:tag-700-xmp",
                               xmp_prefix, capture.payloads[1U])) {
            return false;
        }

        std::vector<std::byte> iptc_prefix;
        if (jpeg) {
            append_bytes(&iptc_prefix,
                         std::as_bytes(
                             std::span<const char>(kPhotoshopHeader,
                                                   sizeof(kPhotoshopHeader))));
            append_bytes(&iptc_prefix,
                         std::as_bytes(std::span<const char>(kEightBim, 4U)));
            iptc_prefix.push_back(std::byte { 0x04U });
            iptc_prefix.push_back(std::byte { 0x04U });
            iptc_prefix.push_back(std::byte { 0U });
            iptc_prefix.push_back(std::byte { 0U });
            if (capture.payloads[2U].size() > UINT32_MAX) {
                return false;
            }
            append_be32(&iptc_prefix,
                        static_cast<uint32_t>(capture.payloads[2U].size()));
        }
        if (!add_payload_block(bundle, TransferBlockKind::IptcIim, 2U,
                               jpeg ? "jpeg:app13-iptc" : "tiff:tag-33723-iptc",
                               iptc_prefix, capture.payloads[2U], jpeg)) {
            return false;
        }
        return true;
    }

    static std::vector<std::byte> make_minimal_jpeg()
    {
        static constexpr std::array<std::byte, 20U> kBytes = {
            std::byte { 0xFFU }, std::byte { 0xD8U }, std::byte { 0xFFU },
            std::byte { 0xDAU }, std::byte { 0x00U }, std::byte { 0x08U },
            std::byte { 0x01U }, std::byte { 0x01U }, std::byte { 0x00U },
            std::byte { 0x00U }, std::byte { 0x3FU }, std::byte { 0x00U },
            std::byte { 0x11U }, std::byte { 0x22U }, std::byte { 0xFFU },
            std::byte { 0x00U }, std::byte { 0x33U }, std::byte { 0x44U },
            std::byte { 0xFFU }, std::byte { 0xD9U },
        };
        return std::vector<std::byte>(kBytes.begin(), kBytes.end());
    }

    static bool jpeg_scan_stream(std::span<const std::byte> bytes,
                                 std::span<const std::byte>* out) noexcept
    {
        if (out == nullptr || bytes.size() < 4U
            || bytes[0U] != std::byte { 0xFFU }
            || bytes[1U] != std::byte { 0xD8U }
            || bytes[bytes.size() - 2U] != std::byte { 0xFFU }
            || bytes[bytes.size() - 1U] != std::byte { 0xD9U }) {
            return false;
        }
        size_t offset = 2U;
        while (offset < bytes.size()) {
            const size_t marker_start = offset;
            if (bytes[offset] != std::byte { 0xFFU }) {
                return false;
            }
            while (offset < bytes.size()
                   && bytes[offset] == std::byte { 0xFFU }) {
                ++offset;
            }
            if (offset >= bytes.size()) {
                return false;
            }
            const uint8_t marker = std::to_integer<uint8_t>(bytes[offset]);
            ++offset;
            if (marker == 0xDAU) {
                if (offset > bytes.size() || 2U > bytes.size() - offset) {
                    return false;
                }
                const uint16_t length = static_cast<uint16_t>(
                    (static_cast<uint16_t>(
                         std::to_integer<uint8_t>(bytes[offset]))
                     << 8U)
                    | std::to_integer<uint8_t>(bytes[offset + 1U]));
                if (length < 2U || length > bytes.size() - offset) {
                    return false;
                }
                *out = bytes.subspan(marker_start);
                return true;
            }
            if (marker == 0xD8U || marker == 0xD9U || marker == 0x01U
                || (marker >= 0xD0U && marker <= 0xD7U)) {
                continue;
            }
            if (offset > bytes.size() || 2U > bytes.size() - offset) {
                return false;
            }
            const uint16_t length = static_cast<uint16_t>(
                (static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset]))
                 << 8U)
                | std::to_integer<uint8_t>(bytes[offset + 1U]));
            if (length < 2U || length > bytes.size() - offset) {
                return false;
            }
            offset += length;
        }
        return false;
    }

    static bool jpeg_single_iptc_iim(std::span<const std::byte> bytes,
                                     std::span<const std::byte>* out) noexcept
    {
        if (out == nullptr || bytes.size() < 4U
            || bytes[0U] != std::byte { 0xFFU }
            || bytes[1U] != std::byte { 0xD8U }) {
            return false;
        }
        static constexpr char kPhotoshopHeader[] = "Photoshop 3.0";
        static constexpr char kEightBim[]        = "8BIM";
        bool found_iptc                          = false;
        size_t offset                            = 2U;
        while (offset < bytes.size()) {
            if (bytes[offset] != std::byte { 0xFFU }) {
                return false;
            }
            while (offset < bytes.size()
                   && bytes[offset] == std::byte { 0xFFU }) {
                ++offset;
            }
            if (offset >= bytes.size()) {
                return false;
            }
            const uint8_t marker = std::to_integer<uint8_t>(bytes[offset]);
            ++offset;
            if (marker == 0xDAU) {
                return found_iptc;
            }
            if (marker == 0xD8U || marker == 0x01U
                || (marker >= 0xD0U && marker <= 0xD7U)) {
                continue;
            }
            if (offset > bytes.size() || 2U > bytes.size() - offset) {
                return false;
            }
            const uint16_t length = static_cast<uint16_t>(
                (static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset]))
                 << 8U)
                | std::to_integer<uint8_t>(bytes[offset + 1U]));
            if (length < 2U || length > bytes.size() - offset) {
                return false;
            }
            if (marker == 0xEDU) {
                if (found_iptc) {
                    return false;
                }
                const std::span<const std::byte> payload
                    = bytes.subspan(offset + 2U, length - 2U);
                if (payload.size() < 26U
                    || std::memcmp(payload.data(), "Photoshop 3.0\0", 14U) != 0
                    || std::memcmp(payload.data() + 14U, kEightBim, 4U) != 0
                    || payload[18U] != std::byte { 0x04U }
                    || payload[19U] != std::byte { 0x04U }
                    || payload[20U] != std::byte { 0U }
                    || payload[21U] != std::byte { 0U }) {
                    return false;
                }
                const uint32_t data_size
                    = (static_cast<uint32_t>(
                           std::to_integer<uint8_t>(payload[22U]))
                       << 24U)
                      | (static_cast<uint32_t>(
                             std::to_integer<uint8_t>(payload[23U]))
                         << 16U)
                      | (static_cast<uint32_t>(
                             std::to_integer<uint8_t>(payload[24U]))
                         << 8U)
                      | std::to_integer<uint8_t>(payload[25U]);
                if (data_size > payload.size() - 26U) {
                    return false;
                }
                const size_t pad_size = data_size & 1U;
                if (payload.size() - 26U != data_size + pad_size
                    || (pad_size != 0U && payload.back() != std::byte { 0U })) {
                    return false;
                }
                *out       = payload.subspan(26U, data_size);
                found_iptc = true;
            }
            offset += length;
        }
        return false;
    }

    static void append_tiff_unsigned(std::vector<std::byte>* output,
                                     uint64_t value, size_t width,
                                     bool big_endian)
    {
        for (size_t index = 0U; index < width; ++index) {
            const size_t shift = big_endian ? (width - 1U - index) * 8U
                                            : index * 8U;
            output->push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
        }
    }

    static std::array<std::byte, 8U>
    encode_tiff_unsigned(uint64_t value, size_t width, bool big_endian) noexcept
    {
        std::array<std::byte, 8U> output {};
        for (size_t index = 0U; index < width; ++index) {
            const size_t shift = big_endian ? (width - 1U - index) * 8U
                                            : index * 8U;
            output[index] = static_cast<std::byte>((value >> shift) & 0xFFU);
        }
        return output;
    }

    static void append_tiff_entry_inline(std::vector<std::byte>* output,
                                         bool big_tiff, bool big_endian,
                                         uint16_t tag, uint16_t type,
                                         uint64_t count,
                                         std::span<const std::byte> value)
    {
        append_tiff_unsigned(output, tag, 2U, big_endian);
        append_tiff_unsigned(output, type, 2U, big_endian);
        append_tiff_unsigned(output, count, big_tiff ? 8U : 4U, big_endian);
        const size_t inline_width = big_tiff ? 8U : 4U;
        append_bytes(output, value);
        for (size_t index = value.size(); index < inline_width; ++index) {
            output->push_back(std::byte { 0U });
        }
    }

    struct TiffFixture final {
        std::vector<std::byte> bytes;
        bool big_tiff             = false;
        bool big_endian           = false;
        uint64_t pixel_offset     = 0U;
        uint32_t pixel_byte_count = 4U;
    };

    static TiffFixture make_tiff_fixture(bool big_tiff, bool big_endian,
                                         bool dng)
    {
        TiffFixture fixture;
        fixture.big_tiff            = big_tiff;
        fixture.big_endian          = big_endian;
        const size_t entry_count    = dng ? 10U : 9U;
        const uint64_t ifd_offset   = big_tiff ? 16U : 8U;
        const uint64_t pixel_offset = (big_tiff
                                           ? 16U + 8U + 20U * entry_count + 8U
                                           : 8U + 2U + 12U * entry_count + 4U);
        fixture.pixel_offset        = pixel_offset;

        if (big_endian) {
            fixture.bytes.push_back(std::byte { 'M' });
            fixture.bytes.push_back(std::byte { 'M' });
        } else {
            fixture.bytes.push_back(std::byte { 'I' });
            fixture.bytes.push_back(std::byte { 'I' });
        }
        if (big_tiff) {
            append_tiff_unsigned(&fixture.bytes, 43U, 2U, big_endian);
            append_tiff_unsigned(&fixture.bytes, 8U, 2U, big_endian);
            append_tiff_unsigned(&fixture.bytes, 0U, 2U, big_endian);
            append_tiff_unsigned(&fixture.bytes, ifd_offset, 8U, big_endian);
            append_tiff_unsigned(&fixture.bytes, entry_count, 8U, big_endian);
        } else {
            append_tiff_unsigned(&fixture.bytes, 42U, 2U, big_endian);
            append_tiff_unsigned(&fixture.bytes, ifd_offset, 4U, big_endian);
            append_tiff_unsigned(&fixture.bytes, entry_count, 2U, big_endian);
        }

        const std::array<uint16_t, 4U> short_values = { 8U, 1U, 1U, 1U };
        const std::array<uint16_t, 4U> short_tags   = {
            258U,
            259U,
            262U,
            277U,
        };
        const std::array<uint64_t, 4U> long_values = {
            2U,
            2U,
            pixel_offset,
            2U,
        };
        const std::array<uint16_t, 4U> long_tags = {
            256U,
            257U,
            273U,
            278U,
        };
        size_t short_index = 0U;
        size_t long_index  = 0U;
        for (uint16_t tag = 256U; tag <= 279U; ++tag) {
            if (tag == 256U || tag == 257U || tag == 273U || tag == 278U) {
                const std::array<std::byte, 8U> value
                    = encode_tiff_unsigned(long_values[long_index], 4U,
                                           big_endian);
                append_tiff_entry_inline(
                    &fixture.bytes, big_tiff, big_endian, long_tags[long_index],
                    4U, 1U, std::span<const std::byte>(value.data(), 4U));
                ++long_index;
            } else if (tag == 258U || tag == 259U || tag == 262U
                       || tag == 277U) {
                const std::array<std::byte, 8U> value
                    = encode_tiff_unsigned(short_values[short_index], 2U,
                                           big_endian);
                append_tiff_entry_inline(&fixture.bytes, big_tiff, big_endian,
                                         short_tags[short_index], 3U, 1U,
                                         std::span<const std::byte>(value.data(),
                                                                    2U));
                ++short_index;
            } else if (tag == 279U) {
                const std::array<std::byte, 8U> value
                    = encode_tiff_unsigned(fixture.pixel_byte_count, 4U,
                                           big_endian);
                append_tiff_entry_inline(
                    &fixture.bytes, big_tiff, big_endian, 279U, 4U, 1U,
                    std::span<const std::byte>(value.data(), 4U));
            }
        }
        if (dng) {
            static constexpr std::array<std::byte, 4U> kDngVersion = {
                std::byte { 1U },
                std::byte { 4U },
                std::byte { 0U },
                std::byte { 0U },
            };
            append_tiff_entry_inline(&fixture.bytes, big_tiff, big_endian,
                                     50706U, 1U, 4U, kDngVersion);
        }
        append_tiff_unsigned(&fixture.bytes, 0U, big_tiff ? 8U : 4U,
                             big_endian);
        while (fixture.bytes.size() < fixture.pixel_offset) {
            fixture.bytes.push_back(std::byte { 0U });
        }
        static constexpr std::array<std::byte, 4U> kPixels = {
            std::byte { 0x91U },
            std::byte { 0x27U },
            std::byte { 0x4EU },
            std::byte { 0xB3U },
        };
        append_bytes(&fixture.bytes, kPixels);
        return fixture;
    }

    static bool read_unsigned(std::span<const std::byte> bytes, size_t offset,
                              size_t width, bool big_endian,
                              uint64_t* value) noexcept
    {
        if (value == nullptr || width == 0U || width > sizeof(uint64_t)
            || offset > bytes.size() || width > bytes.size() - offset) {
            return false;
        }
        uint64_t result = 0U;
        for (size_t index = 0U; index < width; ++index) {
            const size_t shift = big_endian ? (width - 1U - index) * 8U
                                            : index * 8U;
            result |= static_cast<uint64_t>(
                          std::to_integer<uint8_t>(bytes[offset + index]))
                      << shift;
        }
        *value = result;
        return true;
    }

    struct TiffLayout final {
        bool big_tiff        = false;
        bool big_endian      = false;
        uint64_t ifd0_offset = 0U;
    };

    static bool read_tiff_layout(std::span<const std::byte> bytes,
                                 TiffLayout* layout) noexcept
    {
        if (layout == nullptr || bytes.size() < 8U) {
            return false;
        }
        const bool big_endian = bytes[0U] == std::byte { 'M' }
                                && bytes[1U] == std::byte { 'M' };
        if (!big_endian
            && (bytes[0U] != std::byte { 'I' }
                || bytes[1U] != std::byte { 'I' })) {
            return false;
        }
        uint64_t magic = 0U;
        if (!read_unsigned(bytes, 2U, 2U, big_endian, &magic)) {
            return false;
        }
        layout->big_endian = big_endian;
        if (magic == 42U) {
            uint64_t ifd0 = 0U;
            if (!read_unsigned(bytes, 4U, 4U, big_endian, &ifd0)) {
                return false;
            }
            layout->big_tiff    = false;
            layout->ifd0_offset = ifd0;
            return true;
        }
        if (magic != 43U || bytes.size() < 16U) {
            return false;
        }
        uint64_t offset_width = 0U;
        uint64_t reserved     = 0U;
        uint64_t ifd0         = 0U;
        if (!read_unsigned(bytes, 4U, 2U, big_endian, &offset_width)
            || !read_unsigned(bytes, 6U, 2U, big_endian, &reserved)
            || offset_width != 8U || reserved != 0U
            || !read_unsigned(bytes, 8U, 8U, big_endian, &ifd0)) {
            return false;
        }
        layout->big_tiff    = true;
        layout->ifd0_offset = ifd0;
        return true;
    }

    struct TiffTagView final {
        uint16_t type       = 0U;
        uint64_t count      = 0U;
        size_t value_offset = 0U;
        size_t value_size   = 0U;
    };

    static size_t tiff_type_width(uint16_t type) noexcept
    {
        switch (type) {
        case 1U:
        case 2U:
        case 6U:
        case 7U: return 1U;
        case 3U:
        case 8U: return 2U;
        case 4U:
        case 9U:
        case 11U:
        case 13U: return 4U;
        case 5U:
        case 10U:
        case 12U:
        case 16U:
        case 17U:
        case 18U: return 8U;
        default: return 0U;
        }
    }

    static bool find_tiff_tag(std::span<const std::byte> bytes,
                              const TiffLayout& layout, uint16_t wanted_tag,
                              TiffTagView* result) noexcept
    {
        if (result == nullptr) {
            return false;
        }
        const size_t count_width  = layout.big_tiff ? 8U : 2U;
        const size_t entry_width  = layout.big_tiff ? 20U : 12U;
        const size_t inline_width = layout.big_tiff ? 8U : 4U;
        const size_t next_width   = layout.big_tiff ? 8U : 4U;
        if (layout.ifd0_offset > bytes.size()) {
            return false;
        }
        const size_t ifd0 = static_cast<size_t>(layout.ifd0_offset);
        if (count_width > bytes.size() - ifd0) {
            return false;
        }
        uint64_t count = 0U;
        if (!read_unsigned(bytes, ifd0, count_width, layout.big_endian,
                           &count)) {
            return false;
        }
        const size_t entries_offset = ifd0 + count_width;
        if (entries_offset > bytes.size()
            || count > (bytes.size() - entries_offset) / entry_width) {
            return false;
        }
        const size_t entries_size = static_cast<size_t>(count) * entry_width;
        if (next_width > bytes.size() - entries_offset - entries_size) {
            return false;
        }
        for (size_t index = 0U; index < static_cast<size_t>(count); ++index) {
            const size_t entry_offset = entries_offset + index * entry_width;
            uint64_t tag              = 0U;
            if (!read_unsigned(bytes, entry_offset, 2U, layout.big_endian,
                               &tag)) {
                return false;
            }
            if (tag != wanted_tag) {
                continue;
            }
            uint64_t type        = 0U;
            uint64_t value_count = 0U;
            if (!read_unsigned(bytes, entry_offset + 2U, 2U, layout.big_endian,
                               &type)
                || !read_unsigned(bytes, entry_offset + 4U,
                                  layout.big_tiff ? 8U : 4U, layout.big_endian,
                                  &value_count)) {
                return false;
            }
            const size_t type_width = tiff_type_width(
                static_cast<uint16_t>(type));
            if (type_width == 0U || value_count > SIZE_MAX / type_width) {
                return false;
            }
            const size_t value_size = static_cast<size_t>(value_count)
                                      * type_width;
            const size_t value_field = entry_offset
                                       + (layout.big_tiff ? 12U : 8U);
            size_t value_offset = value_field;
            if (value_size > inline_width) {
                uint64_t payload_offset = 0U;
                if (!read_unsigned(bytes, value_field, inline_width,
                                   layout.big_endian, &payload_offset)
                    || payload_offset > bytes.size()) {
                    return false;
                }
                value_offset = static_cast<size_t>(payload_offset);
            }
            if (value_offset > bytes.size()
                || value_size > bytes.size() - value_offset) {
                return false;
            }
            result->type         = static_cast<uint16_t>(type);
            result->count        = value_count;
            result->value_offset = value_offset;
            result->value_size   = value_size;
            return true;
        }
        return false;
    }

    static bool tiff_scalar(std::span<const std::byte> bytes,
                            const TiffLayout& layout, uint16_t tag,
                            uint16_t expected_type, uint64_t* value) noexcept
    {
        TiffTagView found;
        if (!find_tiff_tag(bytes, layout, tag, &found)
            || found.type != expected_type || found.count != 1U) {
            return false;
        }
        return read_unsigned(bytes, found.value_offset,
                             tiff_type_width(found.type), layout.big_endian,
                             value);
    }

    static void expect_text_entry(const MetaStore& store,
                                  const MetaKeyView& key,
                                  std::string_view expected)
    {
        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& entry = store.entry(ids.front());
        ASSERT_EQ(entry.value.kind, MetaValueKind::Text);
        EXPECT_EQ(value_text(store, entry), expected);
    }

    static void expect_iptc_text(const MetaStore& store, uint16_t record,
                                 uint16_t dataset, std::string_view expected)
    {
        const std::span<const EntryId> ids = store.find_all(
            make_iptc_dataset_key_view(record, dataset));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& entry = store.entry(ids.front());
        EXPECT_TRUE(entry.value.kind == MetaValueKind::Bytes
                    || entry.value.kind == MetaValueKind::Text);
        EXPECT_EQ(value_text(store, entry), expected);
    }

    static void expect_iptc_iim(std::span<const std::byte> bytes,
                                std::string_view expected)
    {
        MetaStore decoded;
        ASSERT_EQ(decode_iptc_iim(bytes, decoded).status,
                  IptcIimDecodeStatus::Ok);
        decoded.finalize();
        expect_iptc_text(decoded, 2U, 5U, expected);
    }

    static void expect_tiff_media_unchanged(std::span<const std::byte> before,
                                            std::span<const std::byte> after,
                                            const TiffFixture& fixture,
                                            bool dng)
    {
        TiffLayout before_layout;
        TiffLayout after_layout;
        ASSERT_TRUE(read_tiff_layout(before, &before_layout));
        ASSERT_TRUE(read_tiff_layout(after, &after_layout));
        EXPECT_EQ(before_layout.big_tiff, fixture.big_tiff);
        EXPECT_EQ(before_layout.big_endian, fixture.big_endian);
        EXPECT_EQ(after_layout.big_tiff, fixture.big_tiff);
        EXPECT_EQ(after_layout.big_endian, fixture.big_endian);

        struct ScalarExpectation final {
            uint16_t tag;
            uint16_t type;
            uint64_t value;
        };
        static constexpr std::array<ScalarExpectation, 5U> kImageScalars = { {
            { 256U, 4U, 2U },
            { 257U, 4U, 2U },
            { 258U, 3U, 8U },
            { 277U, 3U, 1U },
            { 278U, 4U, 2U },
        } };
        for (const ScalarExpectation& expected : kImageScalars) {
            uint64_t before_value = 0U;
            uint64_t after_value  = 0U;
            ASSERT_TRUE(tiff_scalar(before, before_layout, expected.tag,
                                    expected.type, &before_value));
            ASSERT_TRUE(tiff_scalar(after, after_layout, expected.tag,
                                    expected.type, &after_value));
            EXPECT_EQ(before_value, expected.value);
            EXPECT_EQ(after_value, before_value);
        }

        uint64_t before_offset = 0U;
        uint64_t after_offset  = 0U;
        uint64_t before_count  = 0U;
        uint64_t after_count   = 0U;
        ASSERT_TRUE(
            tiff_scalar(before, before_layout, 273U, 4U, &before_offset));
        ASSERT_TRUE(tiff_scalar(after, after_layout, 273U, 4U, &after_offset));
        ASSERT_TRUE(
            tiff_scalar(before, before_layout, 279U, 4U, &before_count));
        ASSERT_TRUE(tiff_scalar(after, after_layout, 279U, 4U, &after_count));
        EXPECT_EQ(before_offset, fixture.pixel_offset);
        EXPECT_EQ(after_offset, before_offset);
        EXPECT_EQ(before_count, fixture.pixel_byte_count);
        EXPECT_EQ(after_count, before_count);
        ASSERT_LE(before_offset, before.size());
        ASSERT_LE(after_offset, after.size());
        ASSERT_LE(before_count, before.size() - before_offset);
        ASSERT_LE(after_count, after.size() - after_offset);
        EXPECT_TRUE(
            same_bytes(before.subspan(static_cast<size_t>(before_offset),
                                      static_cast<size_t>(before_count)),
                       after.subspan(static_cast<size_t>(after_offset),
                                     static_cast<size_t>(after_count))));

        if (dng) {
            TiffTagView before_version;
            TiffTagView version;
            ASSERT_TRUE(
                find_tiff_tag(before, before_layout, 50706U, &before_version));
            ASSERT_TRUE(find_tiff_tag(after, after_layout, 50706U, &version));
            static constexpr std::array<std::byte, 4U> kDngVersion = {
                std::byte { 1U },
                std::byte { 4U },
                std::byte { 0U },
                std::byte { 0U },
            };
            ASSERT_EQ(before_version.type, 1U);
            ASSERT_EQ(before_version.count, kDngVersion.size());
            ASSERT_EQ(version.type, 1U);
            ASSERT_EQ(version.count, kDngVersion.size());
            EXPECT_TRUE(same_bytes(before.subspan(before_version.value_offset,
                                                  before_version.value_size),
                                   kDngVersion));
            EXPECT_TRUE(same_bytes(after.subspan(version.value_offset,
                                                 version.value_size),
                                   kDngVersion));
        }
    }

    static void expect_container_readback(TransferTargetFormat target,
                                          std::span<const std::byte> input,
                                          std::span<const std::byte> output,
                                          const ReplayCapture& replay,
                                          const TiffFixture* tiff_fixture)
    {
        const ReadTransferSourceSnapshotBytesResult read
            = read_transfer_source_snapshot_bytes(output);
        ASSERT_EQ(read.status, TransferStatus::Ok);
        expect_text_entry(read.snapshot.store,
                          make_exif_tag_key_view("ifd0", 0x010FU), "Maker02");
        expect_text_entry(read.snapshot.store,
                          make_xmp_property_key_view(kXapNamespace, "Label"),
                          kNewLabel);
        expect_text_entry(read.snapshot.store,
                          make_xmp_property_key_view(kDcNamespace, kTitlePath),
                          kEditedTitle);

        if (target == TransferTargetFormat::Jpeg) {
            expect_iptc_text(read.snapshot.store, 2U, 5U, kPatchedTitle);
            std::span<const std::byte> raw_iptc;
            ASSERT_TRUE(jpeg_single_iptc_iim(output, &raw_iptc));
            EXPECT_TRUE(same_bytes(raw_iptc, replay.payloads[2U]));
            expect_iptc_iim(raw_iptc, kPatchedTitle);
            std::span<const std::byte> input_scan;
            std::span<const std::byte> output_scan;
            ASSERT_TRUE(jpeg_scan_stream(input, &input_scan));
            ASSERT_TRUE(jpeg_scan_stream(output, &output_scan));
            EXPECT_TRUE(same_bytes(input_scan, output_scan));
            return;
        }

        ASSERT_NE(tiff_fixture, nullptr);
        TiffLayout layout;
        ASSERT_TRUE(read_tiff_layout(output, &layout));
        TiffTagView iptc_tag;
        ASSERT_TRUE(find_tiff_tag(output, layout, 33723U, &iptc_tag));
        ASSERT_EQ(iptc_tag.type, 7U);
        EXPECT_EQ(iptc_tag.count, replay.payloads[2U].size());
        const std::span<const std::byte> raw_iptc
            = output.subspan(iptc_tag.value_offset, iptc_tag.value_size);
        EXPECT_TRUE(same_bytes(raw_iptc, replay.payloads[2U]));
        expect_iptc_iim(raw_iptc, kPatchedTitle);
        expect_tiff_media_unchanged(input, output, *tiff_fixture,
                                    target == TransferTargetFormat::Dng);
    }

    static void execute_and_check_container(TransferTargetFormat target,
                                            const PatchLifecycle& lifecycle,
                                            const ReplayCapture& replay,
                                            std::span<const std::byte> input,
                                            const TiffFixture* tiff_fixture)
    {
        PreparedTransferBundle bundle;
        ASSERT_TRUE(make_prepared_bundle(target, replay, &bundle));
        ExecutePreparedTransferOptions options;
        options.edit_requested = true;
        options.edit_apply     = true;
        const ExecutePreparedTransferResult executed
            = execute_prepared_transfer(&bundle, input, options);
        ASSERT_EQ(executed.compile.status, TransferStatus::Ok);
        ASSERT_EQ(executed.emit.status, TransferStatus::Ok);
        ASSERT_EQ(executed.edit_plan_status, TransferStatus::Ok);
        ASSERT_EQ(executed.edit_apply.status, TransferStatus::Ok);
        ASSERT_FALSE(executed.edited_output.empty());
        expect_container_readback(target, input, executed.edited_output, replay,
                                  tiff_fixture);
        EXPECT_FALSE(
            lifecycle.worker.payload(MetadataPatchPayload::IptcIim).empty());
    }

}  // namespace

TEST(MetadataPatchIptcTransfer,
     GenericAuthoringTranslationPatchReplayAndHostContainerReadback)
{
    PatchLifecycle lifecycle;
    const bool built = build_patch_lifecycle(&lifecycle, 3401U);
    ASSERT_TRUE(lifecycle.authoring.ok());
    ASSERT_EQ(lifecycle.editing.status, MetadataEditingStatus::Ok);
    ASSERT_EQ(lifecycle.translation.status,
              MetadataDescriptiveTranslationStatus::Ok);
    ASSERT_TRUE(lifecycle.preparation.ok())
        << metadata_patch_code_name(lifecycle.preparation.code)
        << " failed_index=" << lifecycle.preparation.failed_index;
    ASSERT_TRUE(lifecycle.worker_creation.ok());
    ASSERT_TRUE(lifecycle.patching.ok())
        << metadata_patch_code_name(lifecycle.patching.code)
        << " failed_index=" << lifecycle.patching.failed_index;
    ASSERT_TRUE(built);

    expect_text_entry(lifecycle.source, make_exif_tag_key_view("ifd0", 0x010FU),
                      kMakeText);
    expect_text_entry(lifecycle.source,
                      make_xmp_property_key_view(kDcNamespace, kTitlePath),
                      "Before!");
    expect_text_entry(lifecycle.source,
                      make_xmp_property_key_view(kXapNamespace, "Label"),
                      kOldLabel);
    expect_text_entry(lifecycle.logically_edited,
                      make_xmp_property_key_view(kDcNamespace, kTitlePath),
                      kEditedTitle);
    expect_text_entry(lifecycle.translated,
                      make_xmp_property_key_view(kDcNamespace, kTitlePath),
                      kEditedTitle);
    expect_iptc_text(lifecycle.translated, 2U, 5U, kEditedTitle);

    for (size_t index = 0U; index < kPayloadFamilies.size(); ++index) {
        const std::span<const std::byte> plan_payload = lifecycle.plan.payload(
            kPayloadFamilies[index]);
        const std::span<const std::byte> worker_payload
            = lifecycle.worker.payload(kPayloadFamilies[index]);
        EXPECT_EQ(plan_payload.size(), lifecycle.worker_before[index].size());
        EXPECT_TRUE(same_bytes(plan_payload, lifecycle.worker_before[index]));
        EXPECT_EQ(worker_payload.size(), lifecycle.worker_before[index].size());
        EXPECT_FALSE(
            same_bytes(worker_payload, lifecycle.worker_before[index]));
    }

    ReplayCapture replay;
    const MetadataPatchResult replayed
        = replay_prepared_metadata_instance(lifecycle.worker, capture_replay,
                                            &replay);
    ASSERT_TRUE(replayed.ok());
    ASSERT_EQ(replay.count, kPayloadFamilies.size());
    for (size_t index = 0U; index < kPayloadFamilies.size(); ++index) {
        EXPECT_EQ(replay.families[index], kPayloadFamilies[index]);
        EXPECT_TRUE(
            same_bytes(replay.payloads[index],
                       lifecycle.worker.payload(kPayloadFamilies[index])));
    }

    const std::vector<std::byte> jpeg = make_minimal_jpeg();
    execute_and_check_container(TransferTargetFormat::Jpeg, lifecycle, replay,
                                jpeg, nullptr);

    for (const bool dng : { false, true }) {
        for (const bool big_tiff : { false, true }) {
            for (const bool big_endian : { false, true }) {
                SCOPED_TRACE(dng ? "dng" : "tiff");
                SCOPED_TRACE(big_tiff ? "bigtiff" : "classic");
                SCOPED_TRACE(big_endian ? "big-endian" : "little-endian");
                const TransferTargetFormat target
                    = dng ? TransferTargetFormat::Dng
                          : TransferTargetFormat::Tiff;
                const TiffFixture tiff = make_tiff_fixture(big_tiff, big_endian,
                                                           dng);
                execute_and_check_container(target, lifecycle, replay,
                                            tiff.bytes, &tiff);
            }
        }
    }
}

}  // namespace openmeta
