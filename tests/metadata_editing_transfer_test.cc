// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_creation.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_transfer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    struct TransferTargetCase final {
        TransferTargetFormat format;
        const char* name;
    };

    static MetaStore make_edited_store()
    {
        const std::array fields = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                        "Before"),
            make_metadata_creation_text(MetadataCreationFieldKind::Creator,
                                        "Alice"),
            make_metadata_creation_text(MetadataCreationFieldKind::Creator,
                                        "Bob"),
            make_metadata_creation_text(MetadataCreationFieldKind::Keyword,
                                        "night"),
            make_metadata_creation_text(MetadataCreationFieldKind::CameraMake,
                                        "Vendor Before"),
        };
        MetadataCreationRequest creation_request;
        creation_request.fields = fields;
        MetaStore source;
        EXPECT_EQ(create_metadata(creation_request, &source).status,
                  MetadataCreationStatus::Ok);

        const std::array operations = {
            make_metadata_edit_set(
                make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                            "After")),
            make_metadata_edit_set(
                make_metadata_creation_text(MetadataCreationFieldKind::Creator,
                                            "Carol"),
                1U),
            make_metadata_edit_add(
                make_metadata_creation_text(MetadataCreationFieldKind::Keyword,
                                            "city")),
            make_metadata_edit_remove(MetadataCreationFieldKind::Creator, 0U),
            make_metadata_edit_set(make_metadata_creation_text(
                MetadataCreationFieldKind::CameraMake, "Vendor After")),
        };
        MetadataEditingRequest editing_request;
        editing_request.operations = operations;
        MetaStore edited;
        EXPECT_EQ(edit_metadata(source, editing_request, &edited).status,
                  MetadataEditingStatus::Ok);
        return edited;
    }

    static bool bundle_contains_text(const PreparedTransferBundle& bundle,
                                     std::string_view text) noexcept
    {
        for (const PreparedTransferBlock& block : bundle.blocks) {
            const std::string_view payload(reinterpret_cast<const char*>(
                                               block.payload.data()),
                                           block.payload.size());
            if (payload.find(text) != std::string_view::npos) {
                return true;
            }
        }
        return false;
    }

    static bool
    bundle_contains_removed_text(const PreparedTransferBundle& bundle) noexcept
    {
        return bundle_contains_text(bundle, "Before")
               || bundle_contains_text(bundle, "Alice");
    }

    TEST(MetadataEditingTransfer,
         EditedStoreReachesEveryTargetPreparationAndExecutionBackend)
    {
        const MetaStore edited   = make_edited_store();
        const std::array targets = {
            TransferTargetCase { TransferTargetFormat::Jpeg, "jpeg" },
            TransferTargetCase { TransferTargetFormat::Tiff, "tiff" },
            TransferTargetCase { TransferTargetFormat::Dng, "dng" },
            TransferTargetCase { TransferTargetFormat::Jxl, "jxl" },
            TransferTargetCase { TransferTargetFormat::Webp, "webp" },
            TransferTargetCase { TransferTargetFormat::Png, "png" },
            TransferTargetCase { TransferTargetFormat::Jp2, "jp2" },
            TransferTargetCase { TransferTargetFormat::Heif, "heif" },
            TransferTargetCase { TransferTargetFormat::Avif, "avif" },
            TransferTargetCase { TransferTargetFormat::Cr3, "cr3" },
            TransferTargetCase { TransferTargetFormat::Exr, "exr" },
        };

        for (const TransferTargetCase& target : targets) {
            SCOPED_TRACE(target.name);
            PrepareTransferRequest request;
            request.target_format      = target.format;
            request.include_exif_app1  = false;
            request.include_icc_app2   = false;
            request.include_iptc_app13 = false;
            request.xmp_project_exif   = false;
            request.xmp_project_iptc   = false;

            PreparedTransferBundle bundle;
            const PrepareTransferResult prepared
                = prepare_metadata_for_target(edited, request, &bundle);
            ASSERT_EQ(prepared.status, TransferStatus::Ok);
            ASSERT_FALSE(bundle.blocks.empty());
            EXPECT_TRUE(bundle_contains_text(bundle, "After"));
            EXPECT_FALSE(bundle_contains_removed_text(bundle));
            if (target.format != TransferTargetFormat::Exr) {
                EXPECT_TRUE(bundle_contains_text(bundle, "Carol"));
                EXPECT_TRUE(bundle_contains_text(bundle, "city"));
            }

            // TIFF/DNG and EXR intentionally use typed writer or adapter
            // backends instead of a generic byte stream.
            const ExecutePreparedTransferResult executed
                = execute_prepared_transfer(&bundle);
            EXPECT_EQ(executed.compile.status, TransferStatus::Ok);
            EXPECT_EQ(executed.emit.status, TransferStatus::Ok);
            EXPECT_GT(executed.compiled_ops, 0U);
        }
    }

}  // namespace
}  // namespace openmeta
