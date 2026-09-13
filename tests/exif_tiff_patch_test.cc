// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_patch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static MetadataAuthoringEntry
    patch_authoring_entry(const MetaKeyView& key,
                          const MetaValueView& value) noexcept
    {
        MetadataAuthoringEntry entry;
        entry.key   = key;
        entry.value = value;
        return entry;
    }

    static MetadataPatchValueSpec
    patch_spec(MetaValueKind kind, MetaElementType type, uint32_t count,
               TextEncoding encoding = TextEncoding::Unknown) noexcept
    {
        MetadataPatchValueSpec spec;
        spec.kind          = kind;
        spec.elem_type     = type;
        spec.text_encoding = encoding;
        spec.count         = count;
        return spec;
    }

    static MetadataPatchRequest
    patch_request(const MetaKeyView& key,
                  const MetadataPatchValueSpec& expected,
                  uint32_t occurrence = 0U) noexcept
    {
        MetadataPatchRequest request;
        request.key        = key;
        request.occurrence = occurrence;
        request.expected   = expected;
        return request;
    }

    static bool patch_bytes_equal(std::span<const std::byte> first,
                                  std::span<const std::byte> second) noexcept
    {
        return first.size() == second.size()
               && std::equal(first.begin(), first.end(), second.begin());
    }

    static MetaStore make_patch_store(uint32_t width = 4000U)
    {
        const std::array<uint16_t, 3> levels         = { 64U, 1024U, 4095U };
        const std::array<std::byte, 4> private_bytes = {
            std::byte { 1U },
            std::byte { 2U },
            std::byte { 3U },
            std::byte { 4U },
        };
        const std::array entries = {
            patch_authoring_entry(make_exif_tag_key_view("ifd0", 0x0100U),
                                  make_value_view_u32(width)),
            patch_authoring_entry(make_exif_tag_key_view("exififd", 0x829AU),
                                  make_value_view_urational(1U, 125U)),
            patch_authoring_entry(make_exif_tag_key_view("ifd0", 0x010FU),
                                  make_value_view_text("CameraA",
                                                       TextEncoding::Ascii)),
            patch_authoring_entry(
                make_exif_tag_key_view("ifd0", 0xF001U),
                make_value_view_array(
                    MetaElementType::U16,
                    std::as_bytes(std::span<const uint16_t>(levels)), 3U)),
            patch_authoring_entry(make_exif_tag_key_view("ifd0", 0xF002U),
                                  make_value_view_bytes(private_bytes)),
        };
        MetadataAuthoringOptions options;
        options.validation.context.has_dimensions = true;
        options.validation.context.width          = width;
        options.validation.context.height         = 3000U;
        MetaStore store;
        EXPECT_TRUE(create_metadata_store(entries, &store, options).ok());
        return store;
    }

    static std::array<MetadataPatchRequest, 5> patch_requests()
    {
        return {
            patch_request(make_exif_tag_key_view("ifd0", 0x0100U),
                          patch_spec(MetaValueKind::Scalar,
                                     MetaElementType::U32, 1U)),
            patch_request(make_exif_tag_key_view("exififd", 0x829AU),
                          patch_spec(MetaValueKind::Scalar,
                                     MetaElementType::URational, 1U)),
            patch_request(make_exif_tag_key_view("ifd0", 0x010FU),
                          patch_spec(MetaValueKind::Text, MetaElementType::U8,
                                     7U, TextEncoding::Ascii)),
            patch_request(make_exif_tag_key_view("ifd0", 0xF001U),
                          patch_spec(MetaValueKind::Array, MetaElementType::U16,
                                     3U)),
            patch_request(make_exif_tag_key_view("ifd0", 0xF002U),
                          patch_spec(MetaValueKind::Bytes, MetaElementType::U8,
                                     4U)),
        };
    }

    TEST(MetadataPatch, CompilesTypedHandlesAndPatchesWorkerWithoutReallocation)
    {
        const MetaStore store     = make_patch_store();
        const std::array requests = patch_requests();
        std::array<MetadataPatchHandle, requests.size()> handles;
        PreparedMetadataPatchPlan plan;
        const MetadataPatchResult prepared
            = prepare_metadata_patch_plan(store, requests, { .plan_id = 1U },
                                          handles, &plan);
        ASSERT_TRUE(prepared.ok());
        ASSERT_TRUE(plan.valid());
        EXPECT_EQ(plan.handle_count(), requests.size());
        ASSERT_GT(plan.payload(MetadataPatchPayload::ExifTiff).size(), 8U);
        EXPECT_EQ(plan.payload(MetadataPatchPayload::ExifTiff)[0],
                  std::byte { 'I' });
        EXPECT_EQ(plan.payload(MetadataPatchPayload::ExifTiff)[1],
                  std::byte { 'I' });
        const std::vector<std::byte> immutable(
            plan.payload(MetadataPatchPayload::ExifTiff).begin(),
            plan.payload(MetadataPatchPayload::ExifTiff).end());

        PreparedMetadataPatchInstance first;
        PreparedMetadataPatchInstance second;
        ASSERT_TRUE(create_prepared_metadata_patch_instance(plan, &first).ok());
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &second).ok());
        const std::byte* const first_data
            = first.payload(MetadataPatchPayload::ExifTiff).data();
        const size_t first_size
            = first.payload(MetadataPatchPayload::ExifTiff).size();

        const std::array<uint16_t, 3> levels = { 128U, 2048U, 8191U };
        const std::array<std::byte, 4> bytes = {
            std::byte { 9U },
            std::byte { 8U },
            std::byte { 7U },
            std::byte { 6U },
        };
        const std::array updates = {
            MetadataPatchUpdate { handles[0], make_value_view_u32(8000U) },
            MetadataPatchUpdate { handles[1],
                                  make_value_view_urational(1U, 250U) },
            MetadataPatchUpdate {
                handles[2],
                make_value_view_text("CameraB", TextEncoding::Ascii) },
            MetadataPatchUpdate {
                handles[3],
                make_value_view_array(
                    MetaElementType::U16,
                    std::as_bytes(std::span<const uint16_t>(levels)), 3U) },
            MetadataPatchUpdate { handles[4], make_value_view_bytes(bytes) },
        };
        const MetadataPatchResult patched
            = patch_prepared_metadata_instance(&first, updates);
        ASSERT_TRUE(patched.ok());
        EXPECT_EQ(patched.patched_handles, updates.size());
        EXPECT_EQ(first.payload(MetadataPatchPayload::ExifTiff).data(),
                  first_data);
        EXPECT_EQ(first.payload(MetadataPatchPayload::ExifTiff).size(),
                  first_size);
        EXPECT_TRUE(
            patch_bytes_equal(immutable,
                              plan.payload(MetadataPatchPayload::ExifTiff)));
        EXPECT_TRUE(
            patch_bytes_equal(second.payload(MetadataPatchPayload::ExifTiff),
                              plan.payload(MetadataPatchPayload::ExifTiff)));
        EXPECT_FALSE(
            patch_bytes_equal(first.payload(MetadataPatchPayload::ExifTiff),
                              plan.payload(MetadataPatchPayload::ExifTiff)));

        MetaStore decoded;
        ASSERT_EQ(decode_exif_tiff(first.payload(MetadataPatchPayload::ExifTiff),
                                   decoded, {}, {})
                      .status,
                  ExifDecodeStatus::Ok);
        decoded.finalize();
        const std::span<const EntryId> width = decoded.find_all(
            make_exif_tag_key_view("ifd0", 0x0100U));
        const std::span<const EntryId> exposure = decoded.find_all(
            make_exif_tag_key_view("exififd", 0x829AU));
        const std::span<const EntryId> make = decoded.find_all(
            make_exif_tag_key_view("ifd0", 0x010FU));
        const std::span<const EntryId> decoded_levels = decoded.find_all(
            make_exif_tag_key_view("ifd0", 0xF001U));
        const std::span<const EntryId> decoded_bytes = decoded.find_all(
            make_exif_tag_key_view("ifd0", 0xF002U));
        ASSERT_EQ(width.size(), 1U);
        ASSERT_EQ(exposure.size(), 1U);
        ASSERT_EQ(make.size(), 1U);
        ASSERT_EQ(decoded_levels.size(), 1U);
        ASSERT_EQ(decoded_bytes.size(), 1U);
        EXPECT_EQ(decoded.entry(width[0]).value.data.u64, 8000U);
        EXPECT_EQ(decoded.entry(exposure[0]).value.data.ur.numer, 1U);
        EXPECT_EQ(decoded.entry(exposure[0]).value.data.ur.denom, 250U);
        const std::span<const std::byte> make_bytes = decoded.arena().span(
            decoded.entry(make[0]).value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                       make_bytes.data()),
                                   make_bytes.size()),
                  "CameraB");
        const std::span<const std::byte> level_bytes = decoded.arena().span(
            decoded.entry(decoded_levels[0]).value.data.span);
        std::array<uint16_t, 3> round_trip_levels {};
        ASSERT_EQ(level_bytes.size(), sizeof(round_trip_levels));
        std::memcpy(round_trip_levels.data(), level_bytes.data(),
                    level_bytes.size());
        EXPECT_EQ(round_trip_levels, levels);
        EXPECT_TRUE(patch_bytes_equal(
            decoded.arena().span(
                decoded.entry(decoded_bytes[0]).value.data.span),
            bytes));
    }

    TEST(MetadataPatch, FailedBatchIsTransactionalAndRejectsAliases)
    {
        const MetaStore store     = make_patch_store();
        const std::array requests = patch_requests();
        std::array<MetadataPatchHandle, requests.size()> handles;
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, requests,
                                                { .plan_id = 1U }, handles,
                                                &plan)
                        .ok());
        PreparedMetadataPatchInstance instance;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &instance).ok());
        const std::vector<std::byte> before(
            instance.payload(MetadataPatchPayload::ExifTiff).begin(),
            instance.payload(MetadataPatchPayload::ExifTiff).end());

        const std::array invalid = {
            MetadataPatchUpdate { handles[0], make_value_view_u32(9000U) },
            MetadataPatchUpdate { handles[1],
                                  make_value_view_urational(1U, 0U) },
        };
        const MetadataPatchResult invalid_result
            = patch_prepared_metadata_instance(&instance, invalid);
        EXPECT_EQ(invalid_result.code, MetadataPatchCode::InvalidValue);
        EXPECT_EQ(invalid_result.failed_index, 1U);
        EXPECT_TRUE(
            patch_bytes_equal(instance.payload(MetadataPatchPayload::ExifTiff),
                              before));

        const std::array duplicate = {
            MetadataPatchUpdate { handles[0], make_value_view_u32(9000U) },
            MetadataPatchUpdate { handles[0], make_value_view_u32(9001U) },
        };
        EXPECT_EQ(patch_prepared_metadata_instance(&instance, duplicate).code,
                  MetadataPatchCode::DuplicateHandle);
        EXPECT_TRUE(
            patch_bytes_equal(instance.payload(MetadataPatchPayload::ExifTiff),
                              before));

        const MetaValueView alias = make_value_view_bytes(
            instance.payload(MetadataPatchPayload::ExifTiff).subspan(0U, 4U));
        const MetadataPatchUpdate alias_update { handles[4], alias };
        EXPECT_EQ(patch_prepared_metadata_instance(
                      &instance,
                      std::span<const MetadataPatchUpdate>(&alias_update, 1U))
                      .code,
                  MetadataPatchCode::ValueAliasesInstance);
        EXPECT_TRUE(
            patch_bytes_equal(instance.payload(MetadataPatchPayload::ExifTiff),
                              before));

        MetaValueView overflow = make_value_view_u32(1U);
        overflow.scalar.u64    = UINT64_MAX;
        const MetadataPatchUpdate overflow_update { handles[0], overflow };
        EXPECT_EQ(patch_prepared_metadata_instance(
                      &instance,
                      std::span<const MetadataPatchUpdate>(&overflow_update, 1U))
                      .code,
                  MetadataPatchCode::InvalidValue);
        EXPECT_TRUE(
            patch_bytes_equal(instance.payload(MetadataPatchPayload::ExifTiff),
                              before));
    }

    TEST(MetadataPatch, RejectsForeignPlanHandle)
    {
        const MetaStore first_store  = make_patch_store(4000U);
        const MetaStore second_store = make_patch_store(5000U);
        const MetadataPatchRequest request
            = patch_request(make_exif_tag_key_view("ifd0", 0x0100U),
                            patch_spec(MetaValueKind::Scalar,
                                       MetaElementType::U32, 1U));
        MetadataPatchHandle first_handle;
        MetadataPatchHandle second_handle;
        PreparedMetadataPatchPlan first_plan;
        PreparedMetadataPatchPlan second_plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(
                        first_store,
                        std::span<const MetadataPatchRequest>(&request, 1U),
                        { .plan_id = 1U },
                        std::span<MetadataPatchHandle>(&first_handle, 1U),
                        &first_plan)
                        .ok());
        ASSERT_TRUE(prepare_metadata_patch_plan(
                        second_store,
                        std::span<const MetadataPatchRequest>(&request, 1U),
                        { .plan_id = 2U },
                        std::span<MetadataPatchHandle>(&second_handle, 1U),
                        &second_plan)
                        .ok());
        PreparedMetadataPatchInstance instance;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(first_plan, &instance).ok());
        const MetadataPatchUpdate update { second_handle,
                                           make_value_view_u32(6000U) };
        EXPECT_EQ(patch_prepared_metadata_instance(
                      &instance,
                      std::span<const MetadataPatchUpdate>(&update, 1U))
                      .code,
                  MetadataPatchCode::ForeignHandle);
    }

    TEST(MetadataPatch, CompilesExactDuplicateOccurrence)
    {
        const std::array entries = {
            patch_authoring_entry(make_exif_tag_key_view("ifd0", 0xF100U),
                                  make_value_view_u16(10U)),
            patch_authoring_entry(make_exif_tag_key_view("ifd0", 0xF100U),
                                  make_value_view_u16(20U)),
        };
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(entries, &store).ok());
        const std::array requests = {
            patch_request(make_exif_tag_key_view("ifd0", 0xF100U),
                          patch_spec(MetaValueKind::Scalar,
                                     MetaElementType::U16, 1U),
                          0U),
            patch_request(make_exif_tag_key_view("ifd0", 0xF100U),
                          patch_spec(MetaValueKind::Scalar,
                                     MetaElementType::U16, 1U),
                          1U),
        };
        std::array<MetadataPatchHandle, 2> handles;
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, requests,
                                                { .plan_id = 1U }, handles,
                                                &plan)
                        .ok());
        PreparedMetadataPatchInstance instance;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &instance).ok());
        const MetadataPatchUpdate update { handles[1],
                                           make_value_view_u16(30U) };
        ASSERT_TRUE(
            patch_prepared_metadata_instance(
                &instance, std::span<const MetadataPatchUpdate>(&update, 1U))
                .ok());

        MetaStore decoded;
        ASSERT_EQ(decode_exif_tiff(instance.payload(
                                       MetadataPatchPayload::ExifTiff),
                                   decoded, {}, {})
                      .status,
                  ExifDecodeStatus::Ok);
        decoded.finalize();
        const std::span<const EntryId> ids = decoded.find_all(
            make_exif_tag_key_view("ifd0", 0xF100U));
        ASSERT_EQ(ids.size(), 2U);
        EXPECT_EQ(decoded.entry(ids[0]).value.data.u64, 10U);
        EXPECT_EQ(decoded.entry(ids[1]).value.data.u64, 30U);
    }

}  // namespace
}  // namespace openmeta
