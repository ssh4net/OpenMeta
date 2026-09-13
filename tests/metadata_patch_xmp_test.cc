// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_patch.h"
#include "openmeta/xmp_decode.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace openmeta {
namespace {

    constexpr std::string_view kCapture = "https://example.test/capture/1.0/";
    constexpr std::string_view kExif    = "http://ns.adobe.com/exif/1.0/";
    constexpr std::string_view kXmp     = "http://ns.adobe.com/xap/1.0/";
    constexpr std::array<std::string_view, 9> kNames = {
        "FrameNumber",        "CameraTimestamp",  "LocalTimestampMilliseconds",
        "FrameDateTimeUTC",   "DateTimeOriginal", "DateTimeDigitized",
        "SubSecTimeOriginal", "ImageNumber",      "CreateDate"
    };
    constexpr std::array<std::string_view, 9> kInitial
        = { "0000000001",
            "00000000000000000001",
            "00000000000000000001",
            "2026-09-12T23:59:59.123456789Z",
            "2026-09-12T23:59:59",
            "2026-09-12T23:59:59",
            "123456789",
            "0000000001",
            "2026-09-12T23:59:59" };
    constexpr std::array<std::string_view, 9> kNext
        = { "0000000002",
            "00000000000000000002",
            "00000000000000000002",
            "2026-09-13T00:00:00.000000000Z",
            "2026-09-13T00:00:00",
            "2026-09-13T00:00:00",
            "000000000",
            "4294967295",
            "2026-09-13T00:00:00" };
    constexpr std::array<uint16_t, 5> kExifTags = { 0x9003U, 0x9004U, 0x9291U,
                                                    0x9011U, 0x9211U };
    constexpr std::array<std::string_view, 4> kExifInitial = {
        "2026:09:12 23:59:59", "2026:09:12 23:59:59", "123456789", "+00:00"
    };
    constexpr std::array<std::string_view, 4> kExifNext = {
        "2026:09:13 00:00:00", "2026:09:13 00:00:00", "000000000", "+00:00"
    };

    static std::string_view capture_namespace(size_t index) noexcept
    {
        return index < 4U ? kCapture : index < 8U ? kExif : kXmp;
    }

    static MetadataAuthoringEntry author(const MetaKeyView& key,
                                         const MetaValueView& value)
    {
        MetadataAuthoringEntry entry;
        entry.key   = key;
        entry.value = value;
        return entry;
    }

    static MetaStore capture_store()
    {
        std::vector<MetadataAuthoringEntry> entries;
        for (size_t i = 0; i < kNames.size(); ++i)
            entries.push_back(
                author(make_xmp_property_key_view(capture_namespace(i),
                                                  kNames[i]),
                       make_value_view_text(kInitial[i], TextEncoding::Utf8)));
        for (size_t i = 0; i < kExifInitial.size(); ++i)
            entries.push_back(
                author(make_exif_tag_key_view("exififd", kExifTags[i]),
                       make_value_view_text(kExifInitial[i],
                                            TextEncoding::Ascii)));
        entries.push_back(
            author(make_exif_tag_key_view("exififd", kExifTags[4]),
                   make_value_view_u32(1U)));
        const std::array<uint16_t, 4> calibration = { 1024U, 2048U, 2048U,
                                                      1536U };
        entries.push_back(
            author(make_xmp_property_key_view(kCapture, "Calibration"),
                   make_value_view_array(MetaElementType::U16,
                                         std::as_bytes(std::span(calibration)),
                                         4U)));
        MetaStore store;
        EXPECT_TRUE(create_metadata_store(entries, &store, {}).ok());
        return store;
    }

    static std::array<MetadataPatchRequest, 14> capture_requests()
    {
        std::array<MetadataPatchRequest, 14> requests;
        for (size_t i = 0; i < kNames.size(); ++i) {
            requests[i].key = make_xmp_property_key_view(capture_namespace(i),
                                                         kNames[i]);
            requests[i].escaped_width = static_cast<uint32_t>(
                kInitial[i].size());
        }
        for (size_t i = 0; i < 5U; ++i) {
            MetadataPatchRequest& request = requests[i + 9U];
            request.key = make_exif_tag_key_view("exififd", kExifTags[i]);
            request.expected.kind          = i == 4U ? MetaValueKind::Scalar
                                                     : MetaValueKind::Text;
            request.expected.elem_type     = i == 4U ? MetaElementType::U32
                                                     : MetaElementType::U8;
            request.expected.text_encoding = i == 4U ? TextEncoding::Unknown
                                                     : TextEncoding::Ascii;
            request.expected.count
                = i == 4U ? 1U : static_cast<uint32_t>(kExifInitial[i].size());
        }
        return requests;
    }

    static std::array<MetadataPatchUpdate, 14>
    capture_updates(const std::array<MetadataPatchHandle, 14>& handles)
    {
        std::array<MetadataPatchUpdate, 14> updates;
        for (size_t i = 0; i < 9U; ++i)
            updates[i] = { handles[i],
                           make_value_view_text(kNext[i], TextEncoding::Utf8) };
        for (size_t i = 0; i < 4U; ++i)
            updates[i + 9U]
                = { handles[i + 9U],
                    make_value_view_text(kExifNext[i], TextEncoding::Ascii) };
        updates[13] = { handles[13], make_value_view_u32(UINT32_MAX) };
        return updates;
    }

    static std::vector<std::byte>
    copy_payload(const PreparedMetadataPatchInstance& instance,
                 MetadataPatchPayload family)
    {
        const std::span<const std::byte> bytes = instance.payload(family);
        return { bytes.begin(), bytes.end() };
    }

    static bool same(std::span<const std::byte> a,
                     std::span<const std::byte> b) noexcept
    {
        return a.size() == b.size()
               && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
    }

    static bool count_replay(void* user, MetadataPatchPayload,
                             std::span<const std::byte>) noexcept
    {
        ++*static_cast<uint32_t*>(user);
        return true;
    }
    static bool fail_replay(void*, MetadataPatchPayload,
                            std::span<const std::byte>) noexcept
    {
        return false;
    }

    TEST(MetadataPatchXmp, MixedBatchIsAtomicAndWorkersOutlivePlan)
    {
        MetaStore store           = capture_store();
        const std::array requests = capture_requests();
        std::array<MetadataPatchHandle, 14> handles;
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, requests,
                                                { .plan_id = 1U }, handles,
                                                &plan)
                        .ok());
        PreparedMetadataPatchInstance first, second;
        ASSERT_TRUE(create_prepared_metadata_patch_instance(plan, &first).ok());
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &second).ok());
        const std::vector<std::byte> exif_before
            = copy_payload(first, MetadataPatchPayload::ExifTiff);
        const std::vector<std::byte> xmp_before
            = copy_payload(first, MetadataPatchPayload::Xmp);
        const std::byte* exif_address
            = first.payload(MetadataPatchPayload::ExifTiff).data();
        const std::byte* xmp_address
            = first.payload(MetadataPatchPayload::Xmp).data();
        std::array updates   = capture_updates(handles);
        updates.back().value = make_value_view_u64(UINT64_MAX);
        EXPECT_EQ(patch_prepared_metadata_instance(&first, updates).code,
                  MetadataPatchCode::ValueTypeMismatch);
        EXPECT_TRUE(
            same(first.payload(MetadataPatchPayload::ExifTiff), exif_before));
        EXPECT_TRUE(same(first.payload(MetadataPatchPayload::Xmp), xmp_before));
        updates = capture_updates(handles);
        // Put an invalid XMP value after valid EXIF values to exercise both orders.
        std::swap(updates[0], updates[13]);
        updates.back().value = make_value_view_text("bad", TextEncoding::Utf8);
        EXPECT_EQ(patch_prepared_metadata_instance(&first, updates).code,
                  MetadataPatchCode::WidthMismatch);
        EXPECT_TRUE(
            same(first.payload(MetadataPatchPayload::ExifTiff), exif_before));
        EXPECT_TRUE(same(first.payload(MetadataPatchPayload::Xmp), xmp_before));
        plan.reset();
        updates = capture_updates(handles);
        ASSERT_EQ(
            patch_prepared_metadata_instance(&first, updates).patched_handles,
            14U);
        EXPECT_EQ(first.payload(MetadataPatchPayload::ExifTiff).data(),
                  exif_address);
        EXPECT_EQ(first.payload(MetadataPatchPayload::Xmp).data(), xmp_address);
        EXPECT_EQ(first.payload(MetadataPatchPayload::ExifTiff).size(),
                  exif_before.size());
        EXPECT_EQ(first.payload(MetadataPatchPayload::Xmp).size(),
                  xmp_before.size());
        EXPECT_TRUE(
            same(second.payload(MetadataPatchPayload::ExifTiff), exif_before));
        EXPECT_TRUE(
            same(second.payload(MetadataPatchPayload::Xmp), xmp_before));
        const std::span<const std::byte> xmp = first.payload(
            MetadataPatchPayload::Xmp);
        const std::string text(reinterpret_cast<const char*>(xmp.data()),
                               xmp.size());
        for (std::string_view value : kNext)
            EXPECT_NE(text.find(value), std::string::npos);
        const std::string before(reinterpret_cast<const char*>(
                                     xmp_before.data()),
                                 xmp_before.size());
        const size_t start = before.find("<omns1:Calibration>");
        const size_t end   = before.find("</omns1:Calibration>", start);
        ASSERT_NE(start, std::string::npos);
        ASSERT_NE(end, std::string::npos);
        EXPECT_EQ(text.substr(start, end - start),
                  before.substr(start, end - start));
        MetaStore decoded;
        ASSERT_EQ(decode_exif_tiff(first.payload(MetadataPatchPayload::ExifTiff),
                                   decoded, {}, {})
                      .status,
                  ExifDecodeStatus::Ok);
        decoded.finalize();
        for (size_t i = 0; i < kExifTags.size(); ++i) {
            const auto ids = decoded.find_all(
                make_exif_tag_key_view("exififd", kExifTags[i]));
            ASSERT_EQ(ids.size(), 1U);
            const MetaValue& value = decoded.entry(ids[0]).value;
            if (i == 4U) {
                EXPECT_EQ(value.kind, MetaValueKind::Scalar);
                EXPECT_EQ(value.data.u64, UINT32_MAX);
            } else {
                ASSERT_EQ(value.kind, MetaValueKind::Text);
                const auto bytes = decoded.arena().span(value.data.span);
                EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                               bytes.data()),
                                           bytes.size()),
                          kExifNext[i]);
            }
        }
#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT
        MetaStore decoded_xmp;
        ASSERT_EQ(decode_xmp_packet(xmp, decoded_xmp).status,
                  XmpDecodeStatus::Ok);
        decoded_xmp.finalize();
        for (size_t i = 0; i < kNames.size(); ++i) {
            const auto ids = decoded_xmp.find_all(
                make_xmp_property_key_view(capture_namespace(i), kNames[i]));
            ASSERT_EQ(ids.size(), 1U);
            const MetaValue& value = decoded_xmp.entry(ids[0]).value;
            ASSERT_EQ(value.kind, MetaValueKind::Text);
            const auto bytes = decoded_xmp.arena().span(value.data.span);
            EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                           bytes.data()),
                                       bytes.size()),
                      kNext[i]);
        }
#endif
        uint32_t replayed = 0U;
        EXPECT_TRUE(
            replay_prepared_metadata_instance(first, count_replay, &replayed)
                .ok());
        EXPECT_EQ(replayed, 2U);
        EXPECT_EQ(
            replay_prepared_metadata_instance(first, fail_replay, nullptr).code,
            MetadataPatchCode::ReplayFailed);
        EXPECT_EQ(
            replay_prepared_metadata_instance(first, nullptr, nullptr).code,
            MetadataPatchCode::NullReplayCallback);
    }

    TEST(MetadataPatchXmp, HostIdsDistinguishIdenticalPreparationsAndReprepare)
    {
        MetaStore store           = capture_store();
        const std::array requests = capture_requests();
        std::array<MetadataPatchHandle, 14> first_handles, second_handles;
        PreparedMetadataPatchPlan first, second;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, requests,
                                                { .plan_id = 1U },
                                                first_handles, &first)
                        .ok());
        ASSERT_TRUE(prepare_metadata_patch_plan(store, requests,
                                                { .plan_id = 2U },
                                                second_handles, &second)
                        .ok());
        PreparedMetadataPatchInstance worker;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(second, &worker).ok());
        EXPECT_EQ(patch_prepared_metadata_instance(&worker, capture_updates(
                                                                first_handles))
                      .code,
                  MetadataPatchCode::ForeignHandle);
        ASSERT_TRUE(prepare_metadata_patch_plan(store, requests,
                                                { .plan_id = 3U },
                                                second_handles, &first)
                        .ok());
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(first, &worker).ok());
        EXPECT_EQ(patch_prepared_metadata_instance(&worker, capture_updates(
                                                                first_handles))
                      .code,
                  MetadataPatchCode::ForeignHandle);
    }

    TEST(MetadataPatchXmp, HostIdBoundsPreserveExistingPlanAndHandles)
    {
        MetaStore store           = capture_store();
        const std::array requests = capture_requests();
        std::array<MetadataPatchHandle, 14> handles;
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(
            prepare_metadata_patch_plan(store, requests,
                                        { .plan_id = kMaxMetadataPatchPlanId },
                                        handles, &plan)
                .ok());
        PreparedMetadataPatchInstance worker;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &worker).ok());
        const auto before = plan.payload(MetadataPatchPayload::Xmp);
        const std::array<uint64_t, 3> invalid_ids
            = { 0U, kMaxMetadataPatchPlanId + 1U, UINT64_MAX };
        for (const uint64_t invalid_id : invalid_ids) {
            EXPECT_EQ(prepare_metadata_patch_plan(store, requests,
                                                  { .plan_id = invalid_id },
                                                  handles, &plan)
                          .code,
                      MetadataPatchCode::InvalidOptions);
            EXPECT_EQ(plan.payload(MetadataPatchPayload::Xmp).data(),
                      before.data());
            EXPECT_EQ(plan.payload(MetadataPatchPayload::Xmp).size(),
                      before.size());
            EXPECT_TRUE(
                patch_prepared_metadata_instance(&worker,
                                                 capture_updates(handles))
                    .ok());
        }
    }

    TEST(MetadataPatchXmp, NativeProjectionUsesEmittedIdentityAndConflictPolicy)
    {
        const std::array entries = {
            author(make_exif_tag_key_view("exififd", 0x9003U),
                   make_value_view_text("2026:09:12 23:59:59",
                                        TextEncoding::Ascii)),
            author(make_xmp_property_key_view(kExif, "DateTimeOriginal"),
                   make_value_view_text("existing", TextEncoding::Utf8)),
        };
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(entries, &store, {}).ok());
        MetadataPatchRequest request;
        request.key = make_xmp_property_key_view(kExif, "DateTimeOriginal");
        request.escaped_width = 19U;
        MetadataPatchPlanOptions options { .plan_id = 1U };
        options.xmp.conflict_policy = XmpConflictPolicy::GeneratedWins;
        MetadataPatchHandle handle;
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, { &request, 1U },
                                                options, { &handle, 1U }, &plan)
                        .ok());
        EXPECT_TRUE(plan.payload(MetadataPatchPayload::ExifTiff).empty());
        XmpSidecarOptions dump_options;
        dump_options.format   = XmpSidecarFormat::Portable;
        dump_options.portable = options.xmp;
        std::vector<std::byte> ordinary;
        ASSERT_EQ(dump_xmp_sidecar(store, &ordinary, dump_options).status,
                  XmpDumpStatus::Ok);
        EXPECT_TRUE(same(plan.payload(MetadataPatchPayload::Xmp), ordinary));
        options.xmp.conflict_policy = XmpConflictPolicy::ExistingWins;
        EXPECT_EQ(prepare_metadata_patch_plan(store, { &request, 1U }, options,
                                              { &handle, 1U }, &plan)
                      .code,
                  MetadataPatchCode::WidthMismatch);
        EXPECT_TRUE(same(plan.payload(MetadataPatchPayload::Xmp), ordinary));
    }

    TEST(MetadataPatchXmp, EmptyScalarHasZeroWidthAndInvalidInitialUtf8Fails)
    {
        const auto key = make_xmp_property_key_view(kCapture, "Empty");
        const std::array entries
            = { author(key, make_value_view_text("", TextEncoding::Utf8)) };
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(entries, &store, {}).ok());
        MetadataPatchRequest request;
        request.key = key;
        MetadataPatchHandle handle;
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, { &request, 1U },
                                                { .plan_id = 1U },
                                                { &handle, 1U }, &plan)
                        .ok());
        PreparedMetadataPatchInstance worker;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &worker).ok());
        MetadataPatchUpdate update {
            handle, make_value_view_text("", TextEncoding::Utf8)
        };
        EXPECT_TRUE(
            patch_prepared_metadata_instance(&worker, { &update, 1U }).ok());
        const auto before = copy_payload(worker, MetadataPatchPayload::Xmp);
        update.value      = make_value_view_text("x", TextEncoding::Utf8);
        EXPECT_EQ(
            patch_prepared_metadata_instance(&worker, { &update, 1U }).code,
            MetadataPatchCode::WidthMismatch);
        EXPECT_TRUE(same(worker.payload(MetadataPatchPayload::Xmp), before));

        const std::array invalid_entries = {
            author(key, make_value_view_text("\xC0\xAF", TextEncoding::Utf8))
        };
        MetaStore invalid_store;
        ASSERT_TRUE(create_metadata_store(invalid_entries, &invalid_store,
                                          { .validate = false })
                        .ok());
        request.escaped_width = 2U;
        EXPECT_EQ(prepare_metadata_patch_plan(invalid_store, { &request, 1U },
                                              { .plan_id  = 2U,
                                                .validate = false },
                                              { &handle, 1U }, &plan)
                      .code,
                  MetadataPatchCode::InvalidMetadata);
        EXPECT_TRUE(same(plan.payload(MetadataPatchPayload::Xmp), before));
    }

    TEST(MetadataPatchXmp,
         EscapesLogicalUtf8AndRejectsInvalidOrWrongWidthValues)
    {
        const std::array entries = {
            author(make_xmp_property_key_view(kCapture, "Value"),
                   make_value_view_text("00000", TextEncoding::Utf8)),
        };
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(entries, &store, {}).ok());
        MetadataPatchRequest request;
        request.key           = make_xmp_property_key_view(kCapture, "Value");
        request.escaped_width = 5U;
        MetadataPatchHandle handle;
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, { &request, 1U },
                                                { .plan_id = 1U },
                                                { &handle, 1U }, &plan)
                        .ok());
        PreparedMetadataPatchInstance worker;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &worker).ok());
        const std::array<std::string_view, 5> good    = { "&", "\r", "<A",
                                                          "\xC3\xA9"
                                                             "abc",
                                                          "\xF0\x9F\x98\x80"
                                                             "A" };
        const std::array<std::string_view, 5> escaped = { "&amp;", "&#xD;",
                                                          "&lt;A",
                                                          "\xC3\xA9"
                                                          "abc",
                                                          "\xF0\x9F\x98\x80"
                                                          "A" };
        for (size_t i = 0; i < good.size(); ++i) {
            const MetadataPatchUpdate update {
                handle, make_value_view_text(good[i], TextEncoding::Utf8)
            };
            ASSERT_TRUE(
                patch_prepared_metadata_instance(&worker, { &update, 1U }).ok());
            const std::span<const std::byte> bytes = worker.payload(
                MetadataPatchPayload::Xmp);
            const std::string text(reinterpret_cast<const char*>(bytes.data()),
                                   bytes.size());
            EXPECT_NE(text.find(escaped[i]), std::string::npos);
        }
        const std::vector<std::byte> before
            = copy_payload(worker, MetadataPatchPayload::Xmp);
        const std::array<std::string_view, 8> invalid
            = { std::string_view("a\0b", 3U),
                "\x01",
                "\xC0\xAF",
                "\xED\xA0\x80",
                "\xEF\xBF\xBE",
                "\xF4\x90\x80\x80",
                "\xE2\x82",
                "\x80" };
        for (std::string_view value : invalid) {
            const MetadataPatchUpdate update {
                handle, make_value_view_text(value, TextEncoding::Utf8)
            };
            EXPECT_EQ(
                patch_prepared_metadata_instance(&worker, { &update, 1U }).code,
                MetadataPatchCode::InvalidValue);
            EXPECT_TRUE(
                same(worker.payload(MetadataPatchPayload::Xmp), before));
        }
        MetadataPatchUpdate update {
            handle, make_value_view_text("a", TextEncoding::Utf8)
        };
        EXPECT_EQ(
            patch_prepared_metadata_instance(&worker, { &update, 1U }).code,
            MetadataPatchCode::WidthMismatch);
        update.value = make_value_view_text("\xC3\xA9"
                                            "abc",
                                            TextEncoding::Ascii);
        EXPECT_EQ(
            patch_prepared_metadata_instance(&worker, { &update, 1U }).code,
            MetadataPatchCode::InvalidValue);
        update.value       = make_value_view_text("12345", TextEncoding::Utf8);
        update.value.count = 4U;
        EXPECT_EQ(
            patch_prepared_metadata_instance(&worker, { &update, 1U }).code,
            MetadataPatchCode::InvalidValue);
    }

    static void reuse_worker(PreparedMetadataPatchInstance* worker,
                             const std::array<MetadataPatchUpdate, 14>* updates,
                             bool* success)
    {
        *success = true;
        for (uint32_t i = 0; i < 10000U; ++i) {
            if (!patch_prepared_metadata_instance(worker, *updates).ok()) {
                *success = false;
                return;
            }
        }
    }

    TEST(MetadataPatchXmp, ConcurrentWorkersReuseTenThousandBatches)
    {
        MetaStore store           = capture_store();
        const std::array requests = capture_requests();
        std::array<MetadataPatchHandle, 14> handles;
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, requests,
                                                { .plan_id = 1U }, handles,
                                                &plan)
                        .ok());
        std::array<PreparedMetadataPatchInstance, 4> workers;
        for (PreparedMetadataPatchInstance& worker : workers)
            ASSERT_TRUE(
                create_prepared_metadata_patch_instance(plan, &worker).ok());
        plan.reset();
        const std::array updates    = capture_updates(handles);
        std::array<bool, 4> success = {};
        std::array<std::thread, 4> threads;
        for (size_t i = 0; i < threads.size(); ++i)
            threads[i] = std::thread(reuse_worker, &workers[i], &updates,
                                     &success[i]);
        for (std::thread& thread : threads)
            thread.join();
        for (bool ok : success)
            EXPECT_TRUE(ok);
    }

    TEST(MetadataPatchXmp, RejectsUnsupportedRequestsAndRetainsPlanAndHandles)
    {
        MetaStore store     = capture_store();
        std::array requests = capture_requests();
        std::array<MetadataPatchHandle, 14> handles;
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, requests,
                                                { .plan_id = 1U }, handles,
                                                &plan)
                        .ok());
        PreparedMetadataPatchInstance worker;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &worker).ok());
        const std::vector<std::byte> before
            = copy_payload(worker, MetadataPatchPayload::Xmp);
        requests[13] = requests[0];
        EXPECT_EQ(prepare_metadata_patch_plan(store, requests,
                                              { .plan_id = 1U }, handles, &plan)
                      .code,
                  MetadataPatchCode::DuplicateRequest);
        EXPECT_TRUE(
            patch_prepared_metadata_instance(&worker, capture_updates(handles))
                .ok());
        EXPECT_TRUE(same(plan.payload(MetadataPatchPayload::Xmp), before));
        requests        = capture_requests();
        requests[0].key = make_xmp_property_key_view(kCapture,
                                                     "Calibration[1]");
        EXPECT_EQ(prepare_metadata_patch_plan(store, requests,
                                              { .plan_id = 1U }, handles, &plan)
                      .code,
                  MetadataPatchCode::UnsupportedShape);
        requests[0].key = make_xmp_property_key_view(kCapture, "Calibration");
        EXPECT_EQ(prepare_metadata_patch_plan(store, requests,
                                              { .plan_id = 1U }, handles, &plan)
                      .code,
                  MetadataPatchCode::EntryNotSerializable);
        requests[0].key = make_xmp_property_key_view(kCapture, "Missing");
        EXPECT_EQ(prepare_metadata_patch_plan(store, requests,
                                              { .plan_id = 1U }, handles, &plan)
                      .code,
                  MetadataPatchCode::EntryNotSerializable);
        requests               = capture_requests();
        requests[0].occurrence = 1U;
        EXPECT_EQ(prepare_metadata_patch_plan(store, requests,
                                              { .plan_id = 1U }, handles, &plan)
                      .code,
                  MetadataPatchCode::OccurrenceOutOfRange);
        requests = capture_requests();
        MetadataPatchPlanOptions options { .plan_id = 1U };
        options.xmp.include_existing_xmp = false;
        EXPECT_EQ(prepare_metadata_patch_plan(store, requests, options, handles,
                                              &plan)
                      .code,
                  MetadataPatchCode::EntryNotSerializable);
        options                             = { .plan_id = 1U };
        options.xmp.limits.max_output_bytes = 128U;
        EXPECT_EQ(prepare_metadata_patch_plan(store, requests, options, handles,
                                              &plan)
                      .code,
                  MetadataPatchCode::LimitExceeded);
        options                        = { .plan_id = 1U };
        options.xmp.limits.max_entries = 1U;
        EXPECT_EQ(prepare_metadata_patch_plan(store, requests, options, handles,
                                              &plan)
                      .code,
                  MetadataPatchCode::LimitExceeded);
        options                    = { .plan_id = 1U };
        options.max_patch_requests = 13U;
        EXPECT_EQ(prepare_metadata_patch_plan(store, requests, options, handles,
                                              &plan)
                      .code,
                  MetadataPatchCode::LimitExceeded);
        options                             = { .plan_id = 1U };
        options.xmp.limits.max_output_bytes = 0U;
        EXPECT_EQ(prepare_metadata_patch_plan(store, requests, options, handles,
                                              &plan)
                      .code,
                  MetadataPatchCode::InvalidOptions);
        EXPECT_TRUE(same(plan.payload(MetadataPatchPayload::Xmp), before));
    }

    TEST(MetadataPatchXmp,
         RejectsCrossFamilyAliasesAndRecoversAfterEveryFailure)
    {
        MetaStore store           = capture_store();
        const std::array requests = capture_requests();
        std::array<MetadataPatchHandle, 14> handles;
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, requests,
                                                { .plan_id = 1U }, handles,
                                                &plan)
                        .ok());
        PreparedMetadataPatchInstance worker;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &worker).ok());
        const std::vector<std::byte> exif_before
            = copy_payload(worker, MetadataPatchPayload::ExifTiff);
        const std::vector<std::byte> xmp_before
            = copy_payload(worker, MetadataPatchPayload::Xmp);
        const std::span<const std::byte> xmp = worker.payload(
            MetadataPatchPayload::Xmp);
        const std::span<const std::byte> exif = worker.payload(
            MetadataPatchPayload::ExifTiff);
        const std::string_view xmp_text(reinterpret_cast<const char*>(
                                            xmp.data()),
                                        xmp.size());
        const std::string_view exif_text(reinterpret_cast<const char*>(
                                             exif.data()),
                                         exif.size());
        const size_t xmp_at  = xmp_text.find(kInitial[4]);
        const size_t exif_at = exif_text.find(kExifInitial[0]);
        ASSERT_NE(xmp_at, std::string_view::npos);
        ASSERT_NE(exif_at, std::string_view::npos);
        MetadataPatchUpdate native_alias {
            handles[9], make_value_view_text(xmp_text.substr(xmp_at, 19U),
                                             TextEncoding::Ascii)
        };
        EXPECT_EQ(patch_prepared_metadata_instance(&worker,
                                                   { &native_alias, 1U })
                      .code,
                  MetadataPatchCode::ValueAliasesInstance);
        MetadataPatchUpdate xmp_alias {
            handles[4], make_value_view_text(exif_text.substr(exif_at, 19U),
                                             TextEncoding::Utf8)
        };
        EXPECT_EQ(
            patch_prepared_metadata_instance(&worker, { &xmp_alias, 1U }).code,
            MetadataPatchCode::ValueAliasesInstance);
        std::array updates = capture_updates(handles);
        updates[13]        = updates[0];
        EXPECT_EQ(patch_prepared_metadata_instance(&worker, updates).code,
                  MetadataPatchCode::DuplicateHandle);
        updates[13].handle = {};
        EXPECT_EQ(patch_prepared_metadata_instance(&worker, updates).code,
                  MetadataPatchCode::InvalidHandle);
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::ExifTiff), exif_before));
        EXPECT_TRUE(
            same(worker.payload(MetadataPatchPayload::Xmp), xmp_before));
        EXPECT_TRUE(
            patch_prepared_metadata_instance(&worker, capture_updates(handles))
                .ok());
    }

    TEST(MetadataPatchXmp, PrefixIndependentSelectionAndAllEscapes)
    {
        constexpr std::string_view initial
            = "000000000000000000000000000000000000";
        constexpr std::string_view logical = "&<>\"'\r"
                                             "\xC3\xA9"
                                             "\xF0\x9F\x98\x80";
        constexpr std::string_view encoded = "&amp;&lt;&gt;&quot;&apos;&#xD;"
                                             "\xC3\xA9"
                                             "\xF0\x9F\x98\x80";
        // A second custom namespace changes serializer-generated prefix assignment.
        const std::array entries = {
            author(make_xmp_property_key_view("https://a.example.test/",
                                              "Other"),
                   make_value_view_text("static", TextEncoding::Utf8)),
            author(make_xmp_property_key_view(kCapture, "Value"),
                   make_value_view_text(initial.substr(0U, encoded.size()),
                                        TextEncoding::Utf8)),
        };
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(entries, &store, {}).ok());
        MetadataPatchRequest request;
        request.key           = entries[1].key;
        request.escaped_width = static_cast<uint32_t>(encoded.size());
        MetadataPatchHandle handle;
        PreparedMetadataPatchPlan plan;
        ASSERT_TRUE(prepare_metadata_patch_plan(store, { &request, 1U },
                                                { .plan_id = 1U },
                                                { &handle, 1U }, &plan)
                        .ok());
        PreparedMetadataPatchInstance worker;
        ASSERT_TRUE(
            create_prepared_metadata_patch_instance(plan, &worker).ok());
        const MetadataPatchUpdate update {
            handle, make_value_view_text(logical, TextEncoding::Utf8)
        };
        ASSERT_TRUE(
            patch_prepared_metadata_instance(&worker, { &update, 1U }).ok());
        const std::span<const std::byte> packet = worker.payload(
            MetadataPatchPayload::Xmp);
        const std::string_view text(reinterpret_cast<const char*>(packet.data()),
                                    packet.size());
        EXPECT_NE(text.find(encoded), std::string_view::npos);
        EXPECT_NE(text.find("static"), std::string_view::npos);
    }

}  // namespace
}  // namespace openmeta
