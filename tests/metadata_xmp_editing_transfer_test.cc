// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_xmp_editing.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static constexpr std::string_view kCore
        = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
    static constexpr std::string_view kExt
        = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
    static constexpr std::string_view kPlus
        = "http://ns.useplus.org/ldf/xmp/1.0/";
    static constexpr std::string_view kRights
        = "http://ns.adobe.com/xap/1.0/rights/";
    static constexpr std::string_view kDc = "http://purl.org/dc/elements/1.1/";

    static MetadataAuthoringEntry
    leaf(std::string_view ns, std::string_view path, std::string_view text)
    {
        MetadataAuthoringEntry entry;
        entry.key   = make_xmp_property_key_view(ns, path);
        entry.value = make_value_view_text(text, TextEncoding::Utf8);
        return entry;
    }

    static MetaStore structured_source()
    {
        const std::array contact = {
            leaf(kCore, "CreatorContactInfo/CiEmailWork", "old@example.test"),
            leaf(kCore, "CreatorContactInfo/CiUrlWork",
                 "https://old.example.test"),
        };
        const std::array locations = {
            leaf(kExt, "LocationShown[1]/City", "Paris"),
            leaf(kExt, "LocationShown[1]/CountryName", "France"),
            leaf(kExt, "LocationShown[1]/CountryCode", "FR"),
            leaf(kExt, "LocationShown[2]/City", "Kyoto"),
            leaf(kExt, "LocationShown[2]/CountryName", "Japan"),
        };
        const std::array licensees = {
            leaf(kPlus, "Licensee[1]/LicenseeName", "Old agency"),
            leaf(kPlus, "Licensee[2]/LicenseeName", "Other partner"),
        };
        const std::array terms = {
            leaf(kRights, "UsageTerms[@xml:lang=x-default]", "Old terms"),
            leaf(kRights, "UsageTerms[@xml:lang=fr-FR]", "Droits conservés"),
        };
        const std::array unrelated = {
            leaf(kDc, "title[@xml:lang=x-default]", "Unrelated title"),
            leaf(kDc, "title[@xml:lang=ja-JP]", "保存する題名"),
        };
        MetaStore store;
        EXPECT_TRUE(create_metadata_store(unrelated, &store).ok());
        const std::array operations = {
            MetadataXmpEditingOperation { MetadataXmpEditingOperationKind::Add,
                                          kCore, "CreatorContactInfo",
                                          contact },
            MetadataXmpEditingOperation { MetadataXmpEditingOperationKind::Add,
                                          kExt, "LocationShown", locations },
            MetadataXmpEditingOperation { MetadataXmpEditingOperationKind::Add,
                                          kPlus, "Licensee", licensees },
            MetadataXmpEditingOperation { MetadataXmpEditingOperationKind::Add,
                                          kRights, "UsageTerms", terms },
        };
        EXPECT_TRUE(edit_metadata_xmp(store, operations, &store).ok());
        return store;
    }

    static MetaStore changed_source(const MetaStore& source)
    {
        const std::array contact = {
            leaf(kCore, "CreatorContactInfo/CiEmailWork", "new@example.test"),
        };
        const std::array location = {
            leaf(kExt, "LocationShown[1]/City", "Lyon"),
            leaf(kExt, "LocationShown[1]/CountryName", "France"),
        };
        const std::array terms = {
            leaf(kRights, "UsageTerms[@xml:lang=x-default]", "New terms"),
        };
        const std::array licensee = {
            leaf(kPlus, "Licensee[3]/LicenseeName", "New partner"),
        };
        const std::array operations = {
            MetadataXmpEditingOperation {
                MetadataXmpEditingOperationKind::Replace, kCore,
                "CreatorContactInfo", contact },
            MetadataXmpEditingOperation {
                MetadataXmpEditingOperationKind::Replace, kExt,
                "LocationShown[1]", location },
            MetadataXmpEditingOperation {
                MetadataXmpEditingOperationKind::Replace, kRights,
                "UsageTerms[@xml:lang=x-default]", terms },
            MetadataXmpEditingOperation {
                MetadataXmpEditingOperationKind::Remove,
                kPlus,
                "Licensee[1]",
                {} },
            MetadataXmpEditingOperation { MetadataXmpEditingOperationKind::Add,
                                          kPlus, "Licensee[3]", licensee },
        };
        MetaStore output;
        EXPECT_TRUE(edit_metadata_xmp(source, operations, &output).ok());
        return output;
    }

    static void expect_text(const MetaStore& store, std::string_view ns,
                            std::string_view path, std::string_view expected)
    {
        const auto ids = store.find_all(make_xmp_property_key_view(ns, path));
        ASSERT_EQ(ids.size(), 1U) << path;
        const Entry& entry = store.entry(ids.front());
        ASSERT_EQ(entry.value.kind, MetaValueKind::Text);
        const auto raw = store.arena().span(entry.value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(raw.data()),
                                   raw.size()),
                  expected)
            << path;
    }

    static void expect_changed(const MetaStore& store, bool serialized)
    {
        expect_text(store, kCore, "CreatorContactInfo/CiEmailWork",
                    "new@example.test");
        EXPECT_TRUE(store
                        .find_all(make_xmp_property_key_view(
                            kCore, "CreatorContactInfo/CiUrlWork"))
                        .empty());
        expect_text(store, kExt, "LocationShown[1]/City", "Lyon");
        expect_text(store, kExt, "LocationShown[2]/City", "Kyoto");
        EXPECT_TRUE(store
                        .find_all(make_xmp_property_key_view(
                            kExt, "LocationShown[1]/CountryCode"))
                        .empty());
        expect_text(store, kRights, "UsageTerms[@xml:lang=x-default]",
                    "New terms");
        expect_text(store, kRights, "UsageTerms[@xml:lang=fr-FR]",
                    "Droits conservés");
        expect_text(store, kDc, "title[@xml:lang=ja-JP]", "保存する題名");
        expect_text(store, kPlus,
                    serialized ? "Licensee[1]/LicenseeName"
                               : "Licensee[2]/LicenseeName",
                    "Other partner");
        expect_text(store, kPlus,
                    serialized ? "Licensee[2]/LicenseeName"
                               : "Licensee[3]/LicenseeName",
                    "New partner");
    }

    TEST(MetadataXmpEditingTransfer,
         RoundTripsWholeRecordEditsAndLanguageSiblings)
    {
        const MetaStore source = structured_source();
        const MetaStore edited = changed_source(source);
        expect_changed(edited, false);
        expect_text(source, kExt, "LocationShown[1]/City", "Paris");
        XmpPortableOptions options;
        options.include_exif         = false;
        options.include_iptc         = false;
        options.include_existing_xmp = true;
        std::vector<std::byte> bytes(16384U);
        const auto dumped = dump_xmp_portable(edited, bytes, options);
        ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
        bytes.resize(dumped.written);
        MetaStore decoded;
        ASSERT_EQ(decode_xmp_packet(bytes, decoded).status,
                  XmpDecodeStatus::Ok);
        decoded.finalize();
        expect_changed(decoded, true);
    }

    TEST(MetadataXmpEditingTransfer, EditedRecordsReachPreparedTargetBackends)
    {
        const MetaStore source   = structured_source();
        const MetaStore edited   = changed_source(source);
        const std::array targets = {
            TransferTargetFormat::Jpeg, TransferTargetFormat::Tiff,
            TransferTargetFormat::Dng,  TransferTargetFormat::Png,
            TransferTargetFormat::Webp, TransferTargetFormat::Jp2,
            TransferTargetFormat::Jxl,  TransferTargetFormat::Heif,
            TransferTargetFormat::Avif, TransferTargetFormat::Cr3,
        };
        for (auto target : targets) {
            SCOPED_TRACE(static_cast<unsigned>(target));
            PrepareTransferRequest request;
            request.target_format      = target;
            request.include_exif_app1  = false;
            request.include_icc_app2   = false;
            request.include_iptc_app13 = false;
            request.xmp_project_exif   = false;
            request.xmp_project_iptc   = false;
            PreparedTransferBundle bundle;
            ASSERT_EQ(
                prepare_metadata_for_target(edited, request, &bundle).status,
                TransferStatus::Ok);
            unsigned xmp_blocks = 0U;
            for (const auto& block : bundle.blocks) {
                if (block.kind != TransferBlockKind::Xmp)
                    continue;
                ++xmp_blocks;
                const std::string_view payload(reinterpret_cast<const char*>(
                                                   block.payload.data()),
                                               block.payload.size());
                const std::string_view packet(
                    reinterpret_cast<const char*>(
                        bundle.generated_xmp_sidecar.data()),
                    bundle.generated_xmp_sidecar.size());
                EXPECT_FALSE(packet.empty());
                EXPECT_TRUE(payload.ends_with(packet));
            }
            EXPECT_EQ(xmp_blocks, 1U);
            MetaStore decoded;
            ASSERT_EQ(
                decode_xmp_packet(bundle.generated_xmp_sidecar, decoded).status,
                XmpDecodeStatus::Ok);
            decoded.finalize();
            expect_changed(decoded, true);
            const auto executed = execute_prepared_transfer(&bundle);
            EXPECT_EQ(executed.compile.status, TransferStatus::Ok);
            EXPECT_EQ(executed.emit.status, TransferStatus::Ok);
            EXPECT_GT(executed.compiled_ops, 0U);
        }
    }

}  // namespace
}  // namespace openmeta
