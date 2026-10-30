// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_patch.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static std::span<const std::byte> bytes_of(std::string_view value) noexcept
    {
        return std::as_bytes(std::span<const char>(value.data(), value.size()));
    }

    static Entry make_entry(BlockId block, MetaKey key, MetaValue value,
                            uint32_t source_order,
                            EntryFlags flags = EntryFlags::None) noexcept
    {
        Entry entry;
        entry.key                   = key;
        entry.value                 = value;
        entry.origin.block          = block;
        entry.origin.order_in_block = source_order;
        entry.flags                 = flags;
        return entry;
    }

    static MetaStore make_bytes_store(std::span<const std::byte> payload,
                                      uint16_t record  = 1U,
                                      uint16_t dataset = 5U,
                                      EntryFlags flags = EntryFlags::None)
    {
        MetaStore store;
        const BlockId block = store.add_block({});
        store.add_entry(
            make_entry(block, make_iptc_dataset_key(record, dataset),
                       make_bytes(store.arena(), payload), 0U, flags));
        store.finalize();
        return store;
    }

    static MetadataPatchRequest bytes_request(uint16_t record, uint16_t dataset,
                                              uint32_t count,
                                              uint32_t occurrence = 0U) noexcept
    {
        MetadataPatchRequest request;
        request.key        = make_iptc_dataset_key_view(record, dataset);
        request.occurrence = occurrence;
        request.expected   = { MetaValueKind::Bytes, MetaElementType::U8,
                               TextEncoding::Unknown, count };
        return request;
    }

    static bool same(std::span<const std::byte> first,
                     std::span<const std::byte> second) noexcept
    {
        return first.size() == second.size()
               && (first.empty()
                   || std::memcmp(first.data(), second.data(), first.size())
                          == 0);
    }

    struct ReplayTrace final {
        std::array<MetadataPatchPayload, 3> families {};
        uint32_t calls      = 0U;
        uint32_t fail_after = 0U;
    };

    static bool trace_replay(void* user, MetadataPatchPayload family,
                             std::span<const std::byte>) noexcept
    {
        ReplayTrace* trace            = static_cast<ReplayTrace*>(user);
        trace->families[trace->calls] = family;
        ++trace->calls;
        return trace->fail_after == 0U || trace->calls < trace->fail_after;
    }

    TEST(MetadataPatchIptc,
         CapturesCanonicalOrderAndRejectsMixedBatchAtomically)
    {
        MetaStore store;
        const BlockId block    = store.add_block({});
        const MetaKey iptc_key = make_iptc_dataset_key(1U, 5U);
        store.add_entry(make_entry(block, iptc_key,
                                   make_bytes(store.arena(), bytes_of("del")),
                                   0U, EntryFlags::Deleted));
        store.add_entry(make_entry(block, iptc_key,
                                   make_bytes(store.arena(), bytes_of("old")),
                                   20U));
        store.add_entry(make_entry(block, iptc_key,
                                   make_bytes(store.arena(), bytes_of("new")),
                                   10U));
        store.add_entry(
            make_entry(block, make_iptc_dataset_key(1U, 90U),
                       make_text(store.arena(), "UTF8", TextEncoding::Ascii),
                       99U));
        store.add_entry(
            make_entry(block,
                       make_exif_tag_key(store.arena(), "exififd", 0xA002U),
                       make_u32(1U), 30U));
        store.add_entry(
            make_entry(block,
                       make_xmp_property_key(store.arena(),
                                             "https://example.test/patch/1.0/",
                                             "Frame"),
                       make_text(store.arena(), "0", TextEncoding::Utf8), 40U));
        store.finalize();

        const std::span<const EntryId> active_ids = store.find_all(
            make_iptc_dataset_key_view(1U, 5U));
        ASSERT_EQ(active_ids.size(), 2U);
        EXPECT_EQ(active_ids[0], 1U);
        EXPECT_EQ(active_ids[1], 2U);

        std::array<MetadataPatchRequest, 4> requests {};
        requests[0].key      = make_exif_tag_key_view("exififd", 0xA002U);
        requests[0].expected = { MetaValueKind::Scalar, MetaElementType::U32,
                                 TextEncoding::Unknown, 1U };
        requests[1].key
            = make_xmp_property_key_view("https://example.test/patch/1.0/",
                                         "Frame");
        requests[1].escaped_width = 1U;
        requests[2].key           = make_iptc_dataset_key_view(1U, 5U);
        requests[2].occurrence    = 0U;
        requests[2].expected      = { MetaValueKind::Bytes, MetaElementType::U8,
                                      TextEncoding::Unknown, 3U };
        requests[3]               = requests[2];
        requests[3].occurrence    = 1U;

        MetadataPatchPlanOptions options { .plan_id = 1U };
        options.validate = false;
        std::array<MetadataPatchRequest, 2> duplicate_requests
            = { requests[2], requests[2] };
        std::array<MetadataPatchHandle, 2> duplicate_handles {};
        PreparedMetadataPatchPlan duplicate_plan;
        const MetadataPatchResult duplicate
            = prepare_metadata_patch_plan(store, duplicate_requests, options,
                                          duplicate_handles, &duplicate_plan);
        EXPECT_EQ(duplicate.code, MetadataPatchCode::DuplicateRequest);
        EXPECT_EQ(duplicate.failed_index, 1U);
        EXPECT_FALSE(duplicate_plan.valid());
        EXPECT_FALSE(duplicate_handles[0].valid());
        EXPECT_FALSE(duplicate_handles[1].valid());

        std::array<MetadataPatchHandle, 4> handles {};
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, requests, options,
                                                handles, &plan)
                        .ok());
        PreparedMetadataPatchInstance worker;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &worker).ok());
        PreparedMetadataPatchInstance sibling;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &sibling).ok());

        const std::vector<std::byte> exif_before(
            worker.payload(MetadataPatchPayload::ExifTiff).begin(),
            worker.payload(MetadataPatchPayload::ExifTiff).end());
        const std::vector<std::byte> xmp_before(
            worker.payload(MetadataPatchPayload::Xmp).begin(),
            worker.payload(MetadataPatchPayload::Xmp).end());
        const std::vector<std::byte> iptc_before(
            worker.payload(MetadataPatchPayload::IptcIim).begin(),
            worker.payload(MetadataPatchPayload::IptcIim).end());

        std::array<MetadataPatchUpdate, 4> updates = { {
            { handles[0], make_value_view_u32(2U) },
            { handles[1], make_value_view_text("1", TextEncoding::Utf8) },
            { handles[2], make_value_view_bytes(bytes_of("X11")) },
            { handles[3], make_value_view_text("Y22", TextEncoding::Utf8) },
        } };
        EXPECT_EQ(patch_prepared_metadata_instance(&worker, updates).code,
                  MetadataPatchCode::ValueTypeMismatch);
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::ExifTiff), exif_before));
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::Xmp), xmp_before));
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::IptcIim), iptc_before));

        MetaValueView short_payload = make_value_view_bytes(bytes_of("xy"));
        short_payload.count         = 3U;
        const MetadataPatchUpdate bad_width { handles[2], short_payload };
        EXPECT_EQ(
            patch_prepared_metadata_instance(&worker, { &bad_width, 1U }).code,
            MetadataPatchCode::WidthMismatch);
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::ExifTiff), exif_before));
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::Xmp), xmp_before));
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::IptcIim), iptc_before));

        updates[3].value = make_value_view_bytes(bytes_of("Y22"));
        std::array<MetadataPatchUpdate, 4> bad_exif = { {
            updates[2],
            updates[1],
            updates[3],
            { handles[0], make_value_view_text("2", TextEncoding::Utf8) },
        } };
        EXPECT_EQ(patch_prepared_metadata_instance(&worker, bad_exif).code,
                  MetadataPatchCode::ValueTypeMismatch);
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::ExifTiff), exif_before));
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::Xmp), xmp_before));
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::IptcIim), iptc_before));

        std::array<MetadataPatchUpdate, 4> bad_xmp = { {
            updates[0],
            updates[2],
            updates[3],
            { handles[1], make_value_view_text("long", TextEncoding::Utf8) },
        } };
        EXPECT_EQ(patch_prepared_metadata_instance(&worker, bad_xmp).code,
                  MetadataPatchCode::WidthMismatch);
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::ExifTiff), exif_before));
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::Xmp), xmp_before));
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::IptcIim), iptc_before));

        const std::array<MetadataPatchPayload, 3> alias_families = {
            MetadataPatchPayload::ExifTiff,
            MetadataPatchPayload::Xmp,
            MetadataPatchPayload::IptcIim,
        };
        for (const MetadataPatchPayload family : alias_families) {
            const std::span<const std::byte> payload = worker.payload(family);
            ASSERT_GE(payload.size(), 3U);
            const MetadataPatchUpdate alias {
                handles[2], make_value_view_bytes(payload.first(3U))
            };
            EXPECT_EQ(
                patch_prepared_metadata_instance(&worker, { &alias, 1U }).code,
                MetadataPatchCode::ValueAliasesInstance);
        }

        const std::array<MetadataPatchUpdate, 2> duplicate_updates
            = { { updates[0], updates[0] } };
        EXPECT_EQ(
            patch_prepared_metadata_instance(&worker, duplicate_updates).code,
            MetadataPatchCode::DuplicateHandle);
        MetadataPatchUpdate invalid_handle = updates[0];
        invalid_handle.handle              = {};
        EXPECT_EQ(patch_prepared_metadata_instance(&worker,
                                                   { &invalid_handle, 1U })
                      .code,
                  MetadataPatchCode::InvalidHandle);

        plan.reset();

        ASSERT_TRUE(patch_prepared_metadata_instance(&worker, updates).ok());
        const std::array<std::byte, 25> expected_iptc = {
            std::byte { 0x1CU }, std::byte { 1U },  std::byte { 90U },
            std::byte { 0U },    std::byte { 4U },  std::byte { 'U' },
            std::byte { 'T' },   std::byte { 'F' }, std::byte { '8' },
            std::byte { 0x1CU }, std::byte { 1U },  std::byte { 5U },
            std::byte { 0U },    std::byte { 3U },  std::byte { 'Y' },
            std::byte { '2' },   std::byte { '2' }, std::byte { 0x1CU },
            std::byte { 1U },    std::byte { 5U },  std::byte { 0U },
            std::byte { 3U },    std::byte { 'X' }, std::byte { '1' },
            std::byte { '1' },
        };
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::IptcIim), expected_iptc));
        EXPECT_TRUE(
            same(sibling.payload(MetadataPatchPayload::IptcIim), iptc_before));

        ReplayTrace replay_trace;
        ASSERT_TRUE(replay_prepared_metadata_instance(worker, trace_replay,
                                                      &replay_trace)
                        .ok());
        ASSERT_EQ(replay_trace.calls, 3U);
        EXPECT_EQ(replay_trace.families[0], MetadataPatchPayload::ExifTiff);
        EXPECT_EQ(replay_trace.families[1], MetadataPatchPayload::Xmp);
        EXPECT_EQ(replay_trace.families[2], MetadataPatchPayload::IptcIim);
        replay_trace = { .fail_after = 2U };
        const MetadataPatchResult short_replay
            = replay_prepared_metadata_instance(worker, trace_replay,
                                                &replay_trace);
        EXPECT_EQ(short_replay.code, MetadataPatchCode::ReplayFailed);
        EXPECT_EQ(short_replay.failed_index, 1U);
        EXPECT_EQ(replay_trace.calls, 2U);
    }

    TEST(MetadataPatchIptc, SupportsZeroShortAndExtendedRawValueWidths)
    {
        const std::array<uint32_t, 3> lengths = { 0U, 32767U, 32768U };
        for (uint32_t case_index = 0U; case_index < lengths.size();
             ++case_index) {
            const uint32_t length = lengths[case_index];
            std::vector<std::byte> original(length, std::byte { 0x31U });
            std::vector<std::byte> replacement(length, std::byte { 0xA7U });
            MetaStore store                    = make_bytes_store(original);
            const MetadataPatchRequest request = bytes_request(1U, 5U, length);
            MetadataPatchPlanOptions options { .plan_id = static_cast<uint64_t>(
                                                   case_index + 10U) };
            options.validate = false;
            MetadataPatchHandle handle;
            PreparedMetadataPatchPlan plan;
            ASSERT_TRUE(prepare_metadata_patch_plan(store, { &request, 1U },
                                                    options, { &handle, 1U },
                                                    &plan)
                            .ok());
            PreparedMetadataPatchInstance worker;
            ASSERT_TRUE(
                create_prepared_metadata_patch_instance(plan, &worker).ok());
            const MetadataPatchUpdate update {
                handle,
                make_value_view_bytes(std::span<const std::byte>(replacement))
            };
            ASSERT_TRUE(
                patch_prepared_metadata_instance(&worker, { &update, 1U }).ok());
            const std::span<const std::byte> stream = worker.payload(
                MetadataPatchPayload::IptcIim);
            const uint32_t header_size = length <= 0x7FFFU ? 5U : 9U;
            ASSERT_EQ(stream.size(), static_cast<size_t>(header_size + length));
            EXPECT_EQ(stream[0], std::byte { 0x1CU });
            EXPECT_EQ(stream[1], std::byte { 1U });
            EXPECT_EQ(stream[2], std::byte { 5U });
            if (length <= 0x7FFFU) {
                EXPECT_EQ(stream[3],
                          static_cast<std::byte>((length >> 8U) & 0xFFU));
                EXPECT_EQ(stream[4], static_cast<std::byte>(length & 0xFFU));
            } else {
                EXPECT_EQ(stream[3], std::byte { 0x80U });
                EXPECT_EQ(stream[4], std::byte { 4U });
                EXPECT_EQ(stream[5], std::byte { 0U });
                EXPECT_EQ(stream[6], std::byte { 0U });
                EXPECT_EQ(stream[7], std::byte { 0x80U });
                EXPECT_EQ(stream[8], std::byte { 0U });
            }
            EXPECT_TRUE(same(stream.subspan(header_size), replacement));
        }
    }

    TEST(MetadataPatchIptc, CopiesBytesTextAndRationalArraysWithoutConversion)
    {
        MetaStore store;
        const BlockId block                          = store.add_block({});
        const std::array<std::byte, 2> initial_bytes = { std::byte { 0x00U },
                                                         std::byte { 0xFFU } };
        const std::string_view initial_text("\xFF\0", 2U);
        const std::array<uint32_t, 2> initial_rational = { 7U, 1U };
        store.add_entry(make_entry(block, make_iptc_dataset_key(1U, 5U),
                                   make_bytes(store.arena(), initial_bytes),
                                   0U));
        store.add_entry(make_entry(block, make_iptc_dataset_key(1U, 6U),
                                   make_text(store.arena(), initial_text,
                                             TextEncoding::Utf8),
                                   1U));
        store.add_entry(
            make_entry(block, make_iptc_dataset_key(1U, 7U),
                       make_array(store.arena(), MetaElementType::URational,
                                  std::as_bytes(std::span(initial_rational)),
                                  8U),
                       2U));
        store.add_entry(make_entry(block, make_iptc_dataset_key(1U, 8U),
                                   make_text(store.arena(), std::string_view {},
                                             TextEncoding::Utf8),
                                   3U));
        store.finalize();

        std::array<MetadataPatchRequest, 4> requests = {
            bytes_request(1U, 5U, 2U),
            bytes_request(1U, 6U, 2U),
            bytes_request(1U, 7U, 1U),
            bytes_request(1U, 8U, 0U),
        };
        requests[1].expected = { MetaValueKind::Text, MetaElementType::U8,
                                 TextEncoding::Utf8, 2U };
        requests[2].expected = { MetaValueKind::Array,
                                 MetaElementType::URational,
                                 TextEncoding::Unknown, 1U };
        requests[3].expected = { MetaValueKind::Text, MetaElementType::U8,
                                 TextEncoding::Utf8, 0U };
        MetadataPatchPlanOptions options { .plan_id = 20U };
        options.validate = false;
        std::array<MetadataPatchHandle, 4> handles {};
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, requests, options,
                                                handles, &plan)
                        .ok());
        PreparedMetadataPatchInstance worker;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &worker).ok());
        const std::vector<std::byte> before(
            worker.payload(MetadataPatchPayload::IptcIim).begin(),
            worker.payload(MetadataPatchPayload::IptcIim).end());

        const std::array<std::byte, 2> replacement_bytes
            = { std::byte { 0xA5U }, std::byte { 0x00U } };
        const std::string_view replacement_text("\xC0\xAF", 2U);
        const std::array<uint32_t, 2> replacement_rational = { 0x11223344U,
                                                               0U };
        const auto rational_bytes                          = std::as_bytes(
            std::span(replacement_rational));
        const MetadataPatchUpdate wrong_text_hint {
            handles[1],
            make_value_view_text(replacement_text, TextEncoding::Ascii)
        };
        EXPECT_EQ(patch_prepared_metadata_instance(&worker,
                                                   { &wrong_text_hint, 1U })
                      .code,
                  MetadataPatchCode::ValueTypeMismatch);
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::IptcIim), before));
        const std::array<MetadataPatchUpdate, 4> updates = { {
            { handles[0], make_value_view_bytes(replacement_bytes) },
            { handles[1],
              make_value_view_text(replacement_text, TextEncoding::Utf8) },
            { handles[2], make_value_view_array(MetaElementType::URational,
                                                rational_bytes, 1U) },
            { handles[3],
              make_value_view_text(std::string_view {}, TextEncoding::Utf8) },
        } };
        ASSERT_TRUE(patch_prepared_metadata_instance(&worker, updates).ok());

        const std::span<const std::byte> stream = worker.payload(
            MetadataPatchPayload::IptcIim);
        ASSERT_EQ(stream.size(), 32U);
        EXPECT_TRUE(same(stream.subspan(5U, 2U), replacement_bytes));
        EXPECT_TRUE(same(stream.subspan(12U, 2U), bytes_of(replacement_text)));
        EXPECT_TRUE(same(stream.subspan(19U, 8U), rational_bytes));
        EXPECT_EQ(stream[27], std::byte { 0x1CU });
        EXPECT_EQ(stream[28], std::byte { 1U });
        EXPECT_EQ(stream[29], std::byte { 8U });
        EXPECT_EQ(stream[30], std::byte { 0U });
        EXPECT_EQ(stream[31], std::byte { 0U });
    }

    TEST(MetadataPatchIptc, EnforcesOnlyRequestedOptionsAndReportsLimits)
    {
        MetaStore store;
        const BlockId block = store.add_block({});
        const MetaKey key   = make_iptc_dataset_key(1U, 5U);
        store.add_entry(make_entry(block, key,
                                   make_bytes(store.arena(), bytes_of("one")),
                                   0U));
        store.add_entry(make_entry(block, key,
                                   make_bytes(store.arena(), bytes_of("two")),
                                   1U));
        store.finalize();
        MetadataPatchRequest request = bytes_request(1U, 5U, 3U, 1U);
        MetadataPatchPlanOptions options { .plan_id = 30U };
        options.validate          = false;
        options.iptc.max_datasets = 1U;
        MetadataPatchHandle handle;
        PreparedMetadataPatchPlan plan;
        const MetadataPatchResult dataset_limit
            = prepare_metadata_patch_plan(store, { &request, 1U }, options,
                                          { &handle, 1U }, &plan);
        EXPECT_EQ(dataset_limit.code, MetadataPatchCode::LimitExceeded);
        EXPECT_EQ(dataset_limit.iptc_status,
                  IptcIimSerializeStatus::LimitExceeded);
        EXPECT_EQ(dataset_limit.failed_index, 0U);
        EXPECT_FALSE(plan.valid());
        EXPECT_FALSE(handle.valid());

        options.iptc.max_datasets = 0U;
        EXPECT_EQ(prepare_metadata_patch_plan(store, { &request, 1U }, options,
                                              { &handle, 1U }, &plan)
                      .code,
                  MetadataPatchCode::InvalidOptions);

        std::array<std::byte, 3> raw { std::byte { 1U }, std::byte { 2U },
                                       std::byte { 3U } };
        MetaStore one                  = make_bytes_store(raw);
        request                        = bytes_request(1U, 5U, 3U);
        options.iptc.max_datasets      = 1U;
        options.iptc.max_dataset_bytes = 2U;
        const MetadataPatchResult value_limit
            = prepare_metadata_patch_plan(one, { &request, 1U }, options,
                                          { &handle, 1U }, &plan);
        EXPECT_EQ(value_limit.code, MetadataPatchCode::LimitExceeded);
        EXPECT_EQ(value_limit.failed_index, 0U);
        options.iptc.max_dataset_bytes = 3U;
        options.iptc.max_output_bytes  = 4U;
        const MetadataPatchResult output_limit
            = prepare_metadata_patch_plan(one, { &request, 1U }, options,
                                          { &handle, 1U }, &plan);
        EXPECT_EQ(output_limit.code, MetadataPatchCode::LimitExceeded);
        EXPECT_EQ(output_limit.failed_index, 0U);
        options.iptc.max_output_bytes = static_cast<uint64_t>(UINT32_MAX) + 1U;
        EXPECT_EQ(prepare_metadata_patch_plan(one, { &request, 1U }, options,
                                              { &handle, 1U }, &plan)
                      .code,
                  MetadataPatchCode::InvalidOptions);

        MetaStore exif_store;
        const BlockId exif_block = exif_store.add_block({});
        exif_store.add_entry(make_entry(exif_block,
                                        make_exif_tag_key(exif_store.arena(),
                                                          "exififd", 0xA002U),
                                        make_u32(1U), 0U));
        exif_store.finalize();
        MetadataPatchRequest exif_request;
        exif_request.key      = make_exif_tag_key_view("exififd", 0xA002U);
        exif_request.expected = { MetaValueKind::Scalar, MetaElementType::U32,
                                  TextEncoding::Unknown, 1U };
        options.iptc.max_datasets     = 0U;
        options.iptc.max_output_bytes = 0U;
        MetadataPatchPlanOptions exif_only_options { .plan_id = 31U };
        exif_only_options.validate = false;
        exif_only_options.iptc     = options.iptc;
        EXPECT_TRUE(prepare_metadata_patch_plan(exif_store,
                                                { &exif_request, 1U },
                                                exif_only_options,
                                                { &handle, 1U }, &plan)
                        .ok());
    }

    TEST(MetadataPatchIptc, RejectsInvalidSelectionsAndPreservesPriorPlan)
    {
        const std::array<std::byte, 3> source { std::byte { 1U },
                                                std::byte { 2U },
                                                std::byte { 3U } };
        MetaStore store              = make_bytes_store(source);
        MetadataPatchRequest request = bytes_request(1U, 5U, 3U);
        MetadataPatchPlanOptions options { .plan_id = 40U };
        options.validate = false;
        MetadataPatchHandle handle;
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, { &request, 1U },
                                                options, { &handle, 1U }, &plan)
                        .ok());
        PreparedMetadataPatchInstance worker;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &worker).ok());
        const std::vector<std::byte> before(
            plan.payload(MetadataPatchPayload::IptcIim).begin(),
            plan.payload(MetadataPatchPayload::IptcIim).end());
        const std::byte* address
            = plan.payload(MetadataPatchPayload::IptcIim).data();
        MetadataPatchRequest bad_shape = request;
        bad_shape.expected.count       = 2U;
        const MetadataPatchResult shape_failure
            = prepare_metadata_patch_plan(store, { &bad_shape, 1U }, options,
                                          { &handle, 1U }, &plan);
        EXPECT_EQ(shape_failure.code, MetadataPatchCode::ValueTypeMismatch);
        EXPECT_TRUE(plan.valid());
        EXPECT_EQ(plan.payload(MetadataPatchPayload::IptcIim).data(), address);
        EXPECT_TRUE(same(plan.payload(MetadataPatchPayload::IptcIim), before));
        EXPECT_TRUE(handle.valid());
        const MetadataPatchUpdate still_valid { handle, make_value_view_bytes(
                                                            bytes_of("abc")) };
        EXPECT_TRUE(
            patch_prepared_metadata_instance(&worker, { &still_valid, 1U })
                .ok());

        request.expected = { MetaValueKind::Scalar, MetaElementType::U8,
                             TextEncoding::Unknown, 1U };
        EXPECT_EQ(prepare_metadata_patch_plan(store, { &request, 1U }, options,
                                              { &handle, 1U }, &plan)
                      .code,
                  MetadataPatchCode::InvalidRequest);
        request.expected = { MetaValueKind::Array, MetaElementType::URational,
                             TextEncoding::Unknown, UINT32_MAX };
        EXPECT_EQ(prepare_metadata_patch_plan(store, { &request, 1U }, options,
                                              { &handle, 1U }, &plan)
                      .code,
                  MetadataPatchCode::InvalidRequest);
        request = bytes_request(256U, 5U, 3U);
        EXPECT_EQ(prepare_metadata_patch_plan(store, { &request, 1U }, options,
                                              { &handle, 1U }, &plan)
                      .code,
                  MetadataPatchCode::InvalidRequest);
        request = bytes_request(1U, 5U, 3U, 1U);
        EXPECT_EQ(prepare_metadata_patch_plan(store, { &request, 1U }, options,
                                              { &handle, 1U }, &plan)
                      .code,
                  MetadataPatchCode::OccurrenceOutOfRange);
        request = bytes_request(1U, 6U, 3U);
        EXPECT_EQ(prepare_metadata_patch_plan(store, { &request, 1U }, options,
                                              { &handle, 1U }, &plan)
                      .code,
                  MetadataPatchCode::KeyNotFound);

        MetaStore deleted_store = make_bytes_store(source, 1U, 5U,
                                                   EntryFlags::Deleted);
        request                 = bytes_request(1U, 5U, 3U);
        EXPECT_EQ(prepare_metadata_patch_plan(deleted_store, { &request, 1U },
                                              options, { &handle, 1U }, &plan)
                      .code,
                  MetadataPatchCode::KeyNotFound);

        MetaStore damaged_store;
        const BlockId block = damaged_store.add_block({});
        Entry damaged       = make_entry(block, make_iptc_dataset_key(1U, 5U),
                                         make_bytes(damaged_store.arena(), source),
                                         0U);
        damaged.value.data.span.offset = UINT32_MAX;
        damaged_store.add_entry(damaged);
        damaged_store.finalize();
        request = bytes_request(1U, 5U, 3U);
        const MetadataPatchResult damaged_result
            = prepare_metadata_patch_plan(damaged_store, { &request, 1U },
                                          options, { &handle, 1U }, &plan);
        EXPECT_EQ(damaged_result.code, MetadataPatchCode::EntryNotSerializable);
        EXPECT_EQ(damaged_result.failed_index, 0U);
        EXPECT_EQ(damaged_result.iptc_status,
                  IptcIimSerializeStatus::InvalidMetadata);

        MetaStore scalar_store;
        const BlockId scalar_block = scalar_store.add_block({});
        scalar_store.add_entry(make_entry(scalar_block,
                                          make_iptc_dataset_key(1U, 5U),
                                          make_u32(1U), 0U));
        scalar_store.finalize();
        request = bytes_request(1U, 5U, 1U);
        EXPECT_EQ(prepare_metadata_patch_plan(scalar_store, { &request, 1U },
                                              options, { &handle, 1U }, &plan)
                      .code,
                  MetadataPatchCode::EntryNotSerializable);

        MetaStore empty_store;
        const BlockId empty_block = empty_store.add_block({});
        MetaValue empty_value;
        empty_store.add_entry(make_entry(empty_block,
                                         make_iptc_dataset_key(1U, 5U),
                                         empty_value, 0U));
        empty_store.finalize();
        EXPECT_EQ(prepare_metadata_patch_plan(empty_store, { &request, 1U },
                                              options, { &handle, 1U }, &plan)
                      .code,
                  MetadataPatchCode::EntryNotSerializable);
    }

    TEST(MetadataPatchIptc, ForeignHandlesAreRejected)
    {
        const std::array<std::byte, 3> source { std::byte { 1U },
                                                std::byte { 2U },
                                                std::byte { 3U } };
        MetaStore store                    = make_bytes_store(source);
        const MetadataPatchRequest request = bytes_request(1U, 5U, 3U);
        MetadataPatchPlanOptions first_options { .plan_id = 50U };
        first_options.validate = false;
        MetadataPatchPlanOptions second_options { .plan_id = 51U };
        second_options.validate = false;
        MetadataPatchHandle first_handle, second_handle;
        PreparedMetadataPatchPlan first_plan, second_plan;
        ASSERT_TRUE(
            prepare_metadata_patch_plan(store, { &request, 1U }, first_options,
                                        { &first_handle, 1U }, &first_plan)
                .ok());
        ASSERT_TRUE(
            prepare_metadata_patch_plan(store, { &request, 1U }, second_options,
                                        { &second_handle, 1U }, &second_plan)
                .ok());
        PreparedMetadataPatchInstance second_worker;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(second_plan, &second_worker)
                .ok());
        const MetadataPatchUpdate foreign {
            first_handle, make_value_view_bytes(bytes_of("abc"))
        };
        EXPECT_EQ(patch_prepared_metadata_instance(&second_worker,
                                                   { &foreign, 1U })
                      .code,
                  MetadataPatchCode::ForeignHandle);
    }

}  // namespace
}  // namespace openmeta
