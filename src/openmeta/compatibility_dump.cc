// SPDX-License-Identifier: Apache-2.0

#include "openmeta/compatibility_dump.h"

#include "interop_value_format_internal.h"
#include "openmeta/console_format.h"

#include <cstdio>

namespace openmeta {
namespace {

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }


    static void append_u64(uint64_t v, std::string* out) noexcept
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llu",
                      static_cast<unsigned long long>(v));
        out->append(buf);
    }


    static void append_bool(bool v, std::string* out) noexcept
    {
        out->append(v ? "true" : "false");
    }


    static void append_field(std::string_view name, std::string_view value,
                             std::string* out) noexcept
    {
        out->push_back(' ');
        out->append(name);
        out->append("=\"");
        (void)append_console_escaped_ascii(value, 0U, out);
        out->push_back('"');
    }


    static void append_limited_field(std::string_view name,
                                     std::string_view value,
                                     uint32_t max_bytes,
                                     std::string* out) noexcept
    {
        out->push_back(' ');
        out->append(name);
        out->append("=\"");
        (void)append_console_escaped_ascii(value, max_bytes, out);
        out->push_back('"');
    }


    static void append_u64_field(std::string_view name, uint64_t value,
                                 std::string* out) noexcept
    {
        out->push_back(' ');
        out->append(name);
        out->push_back('=');
        append_u64(value, out);
    }


    static void append_u32_field(std::string_view name, uint32_t value,
                                 std::string* out) noexcept
    {
        append_u64_field(name, value, out);
    }


    static void append_bool_field(std::string_view name, bool value,
                                  std::string* out) noexcept
    {
        out->push_back(' ');
        out->append(name);
        out->push_back('=');
        append_bool(value, out);
    }


    static const char* export_style_name(ExportNameStyle style) noexcept
    {
        switch (style) {
        case ExportNameStyle::Canonical: return "canonical";
        case ExportNameStyle::XmpPortable: return "xmp_portable";
        case ExportNameStyle::FlatHost: return "flat_host";
        }
        return "unknown";
    }


    static const char* export_policy_name(ExportNamePolicy policy) noexcept
    {
        switch (policy) {
        case ExportNamePolicy::Spec: return "spec";
        case ExportNamePolicy::ExifToolAlias: return "exiftool_alias";
        }
        return "unknown";
    }


    static const char* key_kind_name(MetaKeyKind kind) noexcept
    {
        switch (kind) {
        case MetaKeyKind::ExifTag: return "exif_tag";
        case MetaKeyKind::Comment: return "comment";
        case MetaKeyKind::ExrAttribute: return "exr_attribute";
        case MetaKeyKind::IptcDataset: return "iptc_dataset";
        case MetaKeyKind::XmpProperty: return "xmp_property";
        case MetaKeyKind::IccHeaderField: return "icc_header_field";
        case MetaKeyKind::IccTag: return "icc_tag";
        case MetaKeyKind::PhotoshopIrb: return "photoshop_irb";
        case MetaKeyKind::PhotoshopIrbField: return "photoshop_irb_field";
        case MetaKeyKind::GeotiffKey: return "geotiff_key";
        case MetaKeyKind::PrintImField: return "printim_field";
        case MetaKeyKind::BmffField: return "bmff_field";
        case MetaKeyKind::JumbfField: return "jumbf_field";
        case MetaKeyKind::JumbfCborKey: return "jumbf_cbor_key";
        case MetaKeyKind::PngText: return "png_text";
        }
        return "unknown";
    }


    static const char* value_kind_name(MetaValueKind kind) noexcept
    {
        switch (kind) {
        case MetaValueKind::Empty: return "empty";
        case MetaValueKind::Scalar: return "scalar";
        case MetaValueKind::Array: return "array";
        case MetaValueKind::Bytes: return "bytes";
        case MetaValueKind::Text: return "text";
        }
        return "unknown";
    }


    static const char* element_type_name(MetaElementType type) noexcept
    {
        switch (type) {
        case MetaElementType::U8: return "u8";
        case MetaElementType::I8: return "i8";
        case MetaElementType::U16: return "u16";
        case MetaElementType::I16: return "i16";
        case MetaElementType::U32: return "u32";
        case MetaElementType::I32: return "i32";
        case MetaElementType::U64: return "u64";
        case MetaElementType::I64: return "i64";
        case MetaElementType::F32: return "f32";
        case MetaElementType::F64: return "f64";
        case MetaElementType::URational: return "urational";
        case MetaElementType::SRational: return "srational";
        }
        return "unknown";
    }


    static const char* text_encoding_name(TextEncoding encoding) noexcept
    {
        switch (encoding) {
        case TextEncoding::Unknown: return "unknown";
        case TextEncoding::Ascii: return "ascii";
        case TextEncoding::Utf8: return "utf8";
        case TextEncoding::Utf16LE: return "utf16le";
        case TextEncoding::Utf16BE: return "utf16be";
        }
        return "unknown";
    }


    static const char* wire_family_name(WireFamily family) noexcept
    {
        switch (family) {
        case WireFamily::None: return "none";
        case WireFamily::Tiff: return "tiff";
        case WireFamily::Other: return "other";
        }
        return "unknown";
    }


    static const char* transfer_status_name(TransferStatus status) noexcept
    {
        switch (status) {
        case TransferStatus::Ok: return "ok";
        case TransferStatus::InvalidArgument: return "invalid_argument";
        case TransferStatus::Unsupported: return "unsupported";
        case TransferStatus::LimitExceeded: return "limit_exceeded";
        case TransferStatus::Malformed: return "malformed";
        case TransferStatus::UnsafeData: return "unsafe_data";
        case TransferStatus::InternalError: return "internal_error";
        }
        return "unknown";
    }


    static const char* transfer_file_status_name(
        TransferFileStatus status) noexcept
    {
        switch (status) {
        case TransferFileStatus::Ok: return "ok";
        case TransferFileStatus::InvalidArgument: return "invalid_argument";
        case TransferFileStatus::OpenFailed: return "open_failed";
        case TransferFileStatus::StatFailed: return "stat_failed";
        case TransferFileStatus::TooLarge: return "too_large";
        case TransferFileStatus::MapFailed: return "map_failed";
        case TransferFileStatus::ReadFailed: return "read_failed";
        }
        return "unknown";
    }


    static const char* target_format_name(TransferTargetFormat format) noexcept
    {
        switch (format) {
        case TransferTargetFormat::Jpeg: return "jpeg";
        case TransferTargetFormat::Tiff: return "tiff";
        case TransferTargetFormat::Jxl: return "jxl";
        case TransferTargetFormat::Webp: return "webp";
        case TransferTargetFormat::Heif: return "heif";
        case TransferTargetFormat::Avif: return "avif";
        case TransferTargetFormat::Cr3: return "cr3";
        case TransferTargetFormat::Exr: return "exr";
        case TransferTargetFormat::Png: return "png";
        case TransferTargetFormat::Jp2: return "jp2";
        case TransferTargetFormat::Dng: return "dng";
        }
        return "unknown";
    }


    static const char* block_kind_name(TransferBlockKind kind) noexcept
    {
        switch (kind) {
        case TransferBlockKind::Exif: return "exif";
        case TransferBlockKind::Xmp: return "xmp";
        case TransferBlockKind::IptcIim: return "iptc_iim";
        case TransferBlockKind::PhotoshopIrb: return "photoshop_irb";
        case TransferBlockKind::Icc: return "icc";
        case TransferBlockKind::Jumbf: return "jumbf";
        case TransferBlockKind::C2pa: return "c2pa";
        case TransferBlockKind::ExrAttribute: return "exr_attribute";
        case TransferBlockKind::Other: return "other";
        }
        return "unknown";
    }


    static const char* policy_subject_name(TransferPolicySubject subject) noexcept
    {
        switch (subject) {
        case TransferPolicySubject::MakerNote: return "makernote";
        case TransferPolicySubject::Jumbf: return "jumbf";
        case TransferPolicySubject::C2pa: return "c2pa";
        case TransferPolicySubject::XmpExifProjection:
            return "xmp_exif_projection";
        case TransferPolicySubject::XmpIptcProjection:
            return "xmp_iptc_projection";
        case TransferPolicySubject::ImageProperties:
            return "image_properties";
        case TransferPolicySubject::IccProfile: return "icc_profile";
        case TransferPolicySubject::RawColorCalibration:
            return "raw_color_calibration";
        case TransferPolicySubject::CameraRawSettings:
            return "camera_raw_settings";
        }
        return "unknown";
    }


    static const char* policy_action_name(TransferPolicyAction action) noexcept
    {
        switch (action) {
        case TransferPolicyAction::Keep: return "keep";
        case TransferPolicyAction::Drop: return "drop";
        case TransferPolicyAction::Invalidate: return "invalidate";
        case TransferPolicyAction::Rewrite: return "rewrite";
        }
        return "unknown";
    }


    static const char* policy_reason_name(TransferPolicyReason reason) noexcept
    {
        switch (reason) {
        case TransferPolicyReason::Default: return "default";
        case TransferPolicyReason::NotPresent: return "not_present";
        case TransferPolicyReason::ExplicitDrop: return "explicit_drop";
        case TransferPolicyReason::CarrierDisabled: return "carrier_disabled";
        case TransferPolicyReason::ProjectedPayload: return "projected_payload";
        case TransferPolicyReason::DraftInvalidationPayload:
            return "draft_invalidation_payload";
        case TransferPolicyReason::ExternalSignedPayload:
            return "external_signed_payload";
        case TransferPolicyReason::ContentBoundTransferUnavailable:
            return "content_bound_transfer_unavailable";
        case TransferPolicyReason::SignedRewriteUnavailable:
            return "signed_rewrite_unavailable";
        case TransferPolicyReason::PortableInvalidationUnavailable:
            return "portable_invalidation_unavailable";
        case TransferPolicyReason::RewriteUnavailablePreservedRaw:
            return "rewrite_unavailable_preserved_raw";
        case TransferPolicyReason::TargetSerializationUnavailable:
            return "target_serialization_unavailable";
        case TransferPolicyReason::TargetImageProperties:
            return "target_image_properties";
        case TransferPolicyReason::SafetyModeFiltered:
            return "safety_mode_filtered";
        case TransferPolicyReason::RawDataDescriptorFiltered:
            return "raw_data_descriptor_filtered";
        case TransferPolicyReason::OpaquePayloadPreservedUnverified:
            return "opaque_payload_preserved_unverified";
        case TransferPolicyReason::RewriteUnavailableDropped:
            return "rewrite_unavailable_dropped";
        }
        return "unknown";
    }


    static const char* flags_name(EntryFlags flags) noexcept
    {
        if (flags == EntryFlags::None) {
            return "none";
        }
        return "set";
    }


    static void append_metadata_header(
        const MetadataCompatibilityDumpOptions& options,
        const MetaStore& store,
        std::string* out) noexcept
    {
        out->append("openmeta.compat.metadata");
        append_u32_field("version", kCompatibilityDumpContractVersion, out);
        append_u32_field("interop_version", kInteropExportContractVersion, out);
        append_u32_field("flat_host_version",
                         kFlatHostExportContractVersion, out);
        append_field("style", export_style_name(options.style), out);
        append_field("name_policy", export_policy_name(options.name_policy),
                     out);
        append_u32_field("entry_count",
                         static_cast<uint32_t>(store.entries().size()), out);
        append_u32_field("block_count", store.block_count(), out);
        out->push_back('\n');
    }


    class CompatibilityMetadataSink final : public MetadataSink {
    public:
        CompatibilityMetadataSink(const MetaStore& store,
                                  const MetadataCompatibilityDumpOptions& opts,
                                  std::string* out) noexcept
            : store_(store)
            , options_(opts)
            , out_(out)
        {
        }

        void on_item(const ExportItem& item) noexcept override
        {
            if (!out_ || !item.entry) {
                return;
            }
            const Entry& e = *item.entry;
            out_->append("entry");
            append_u32_field("index", emitted_, out_);
            append_field("name", item.name, out_);
            append_field("key_kind", key_kind_name(e.key.kind), out_);
            append_field("value_kind", value_kind_name(e.value.kind), out_);
            append_field("elem_type", element_type_name(e.value.elem_type),
                         out_);
            append_field("text_encoding",
                         text_encoding_name(e.value.text_encoding), out_);
            append_u32_field("count", e.value.count, out_);

            if (options_.include_values) {
                std::string value;
                if (interop_internal::format_value_for_text(
                        store_.arena(), e.value, options_.max_value_bytes,
                        &value)) {
                    append_field("value", value, out_);
                } else {
                    append_field("value", "", out_);
                }
            }

            if (options_.include_origins) {
                append_u32_field("origin_block", e.origin.block, out_);
                append_u32_field("origin_order", e.origin.order_in_block, out_);
                append_field("wire_family",
                             wire_family_name(e.origin.wire_type.family),
                             out_);
                append_u32_field("wire_code", e.origin.wire_type.code, out_);
                append_u32_field("wire_count", e.origin.wire_count, out_);
                append_field("wire_type_name",
                             arena_string(store_.arena(),
                                          e.origin.wire_type_name),
                             out_);
                append_u32_field(
                    "name_context",
                    static_cast<uint32_t>(e.origin.name_context_kind), out_);
                append_u32_field("name_variant",
                                 e.origin.name_context_variant, out_);
            }

            if (options_.include_flags) {
                append_field("flags", flags_name(e.flags), out_);
                append_bool_field("flag_deleted",
                                  any(e.flags, EntryFlags::Deleted), out_);
                append_bool_field("flag_dirty", any(e.flags, EntryFlags::Dirty),
                                  out_);
                append_bool_field("flag_derived",
                                  any(e.flags, EntryFlags::Derived), out_);
                append_bool_field("flag_truncated",
                                  any(e.flags, EntryFlags::Truncated), out_);
                append_bool_field("flag_unreadable",
                                  any(e.flags, EntryFlags::Unreadable), out_);
                append_bool_field(
                    "flag_contextual_name",
                    any(e.flags, EntryFlags::ContextualName), out_);
            }

            out_->push_back('\n');
            emitted_ += 1U;
        }

    private:
        const MetaStore& store_;
        const MetadataCompatibilityDumpOptions& options_;
        std::string* out_;
        uint32_t emitted_ = 0U;
    };


    static void append_transfer_header(
        const ExecutePreparedTransferFileResult& result,
        std::string* out) noexcept
    {
        out->append("openmeta.compat.transfer");
        append_u32_field("version", kCompatibilityDumpContractVersion, out);
        append_field("target_format",
                     target_format_name(result.prepared.bundle.target_format),
                     out);
        append_field("file_status",
                     transfer_file_status_name(result.prepared.file_status),
                     out);
        append_field("prepare_status",
                     transfer_status_name(result.prepared.prepare.status),
                     out);
        append_u32_field("prepared_blocks",
                         static_cast<uint32_t>(
                             result.prepared.bundle.blocks.size()),
                         out);
        out->push_back('\n');
    }


    static void append_policy_decisions(
        const PreparedTransferBundle& bundle,
        const TransferCompatibilityDumpOptions& options,
        std::string* out) noexcept
    {
        for (size_t i = 0; i < bundle.policy_decisions.size(); ++i) {
            const PreparedTransferPolicyDecision& d
                = bundle.policy_decisions[i];
            out->append("policy");
            append_u32_field("index", static_cast<uint32_t>(i), out);
            append_field("subject", policy_subject_name(d.subject), out);
            append_field("requested", policy_action_name(d.requested), out);
            append_field("effective", policy_action_name(d.effective), out);
            append_field("reason", policy_reason_name(d.reason), out);
            append_u32_field("matched_entries", d.matched_entries, out);
            append_limited_field("message", d.message,
                                 options.max_message_bytes, out);
            out->push_back('\n');
        }
    }


    static void append_prepared_blocks(const PreparedTransferBundle& bundle,
                                       std::string* out) noexcept
    {
        for (size_t i = 0; i < bundle.blocks.size(); ++i) {
            const PreparedTransferBlock& block = bundle.blocks[i];
            out->append("block");
            append_u32_field("index", static_cast<uint32_t>(i), out);
            append_u32_field("order", block.order, out);
            append_field("kind", block_kind_name(block.kind), out);
            append_field("route", block.route, out);
            append_u64_field("payload_bytes", block.payload.size(), out);
            out->push_back('\n');
        }
    }


    static void append_writeback_summary(
        const ExecutePreparedTransferFileResult& result,
        const TransferCompatibilityDumpOptions& options,
        std::string* out) noexcept
    {
        const ExecutePreparedTransferResult& exec = result.execute;
        out->append("execute");
        append_bool_field("edit_requested", exec.edit_requested, out);
        append_field("compile_status", transfer_status_name(exec.compile.status),
                     out);
        append_field("emit_status", transfer_status_name(exec.emit.status), out);
        append_field("edit_plan_status",
                     transfer_status_name(exec.edit_plan_status), out);
        append_field("edit_apply_status",
                     transfer_status_name(exec.edit_apply.status), out);
        append_bool_field("strip_existing_xmp", exec.strip_existing_xmp, out);
        append_u64_field("emit_output_size", exec.emit_output_size, out);
        append_u64_field("edit_input_size", exec.edit_input_size, out);
        append_u64_field("edit_output_size", exec.edit_output_size, out);
        append_limited_field("edit_plan_message", exec.edit_plan_message,
                             options.max_message_bytes, out);
        out->push_back('\n');

        out->append("writeback");
        append_bool_field("destination_embedded_loaded",
                          result.xmp_existing_destination_embedded_loaded, out);
        append_field(
            "destination_embedded_status",
            transfer_status_name(
                result.xmp_existing_destination_embedded_status),
            out);
        append_bool_field("xmp_sidecar_requested",
                          result.xmp_sidecar_requested, out);
        append_field("xmp_sidecar_status",
                     transfer_status_name(result.xmp_sidecar_status), out);
        append_field("xmp_sidecar_path", result.xmp_sidecar_path, out);
        append_u64_field("xmp_sidecar_bytes",
                         result.xmp_sidecar_output.size(), out);
        append_bool_field("xmp_sidecar_cleanup_requested",
                          result.xmp_sidecar_cleanup_requested, out);
        append_field(
            "xmp_sidecar_cleanup_status",
            transfer_status_name(result.xmp_sidecar_cleanup_status), out);
        append_field("xmp_sidecar_cleanup_path",
                     result.xmp_sidecar_cleanup_path, out);
        out->push_back('\n');
    }


    static void append_persist_summary(
        const PersistPreparedTransferFileResult& persisted,
        const TransferCompatibilityDumpOptions& options,
        std::string* out) noexcept
    {
        out->append("persist");
        append_field("status", transfer_status_name(persisted.status), out);
        append_limited_field("message", persisted.message,
                             options.max_message_bytes, out);
        append_field("output_status",
                     transfer_status_name(persisted.output_status), out);
        append_field("output_path", persisted.output_path, out);
        append_u64_field("output_bytes", persisted.output_bytes, out);
        append_field("xmp_sidecar_status",
                     transfer_status_name(persisted.xmp_sidecar_status), out);
        append_field("xmp_sidecar_path", persisted.xmp_sidecar_path, out);
        append_u64_field("xmp_sidecar_bytes", persisted.xmp_sidecar_bytes, out);
        append_field(
            "xmp_sidecar_cleanup_status",
            transfer_status_name(persisted.xmp_sidecar_cleanup_status), out);
        append_field("xmp_sidecar_cleanup_path",
                     persisted.xmp_sidecar_cleanup_path, out);
        append_bool_field("xmp_sidecar_cleanup_removed",
                          persisted.xmp_sidecar_cleanup_removed, out);
        out->push_back('\n');
    }

}  // namespace


bool
dump_metadata_compatibility(const MetaStore& store,
                            const MetadataCompatibilityDumpOptions& options,
                            std::string* out) noexcept
{
    if (!out) {
        return false;
    }
    out->clear();

    append_metadata_header(options, store, out);

    ExportOptions export_options;
    export_options.style              = options.style;
    export_options.name_policy        = options.name_policy;
    export_options.include_origin     = options.include_origins;
    export_options.include_flags      = options.include_flags;
    export_options.include_makernotes = true;

    CompatibilityMetadataSink sink(store, options, out);
    visit_metadata(store, export_options, sink);
    return true;
}


bool
dump_transfer_compatibility(
    const ExecutePreparedTransferFileResult& result,
    const PersistPreparedTransferFileResult* persisted,
    const TransferCompatibilityDumpOptions& options,
    std::string* out) noexcept
{
    if (!out) {
        return false;
    }
    out->clear();

    append_transfer_header(result, out);
    if (options.include_policy_decisions) {
        append_policy_decisions(result.prepared.bundle, options, out);
    }
    if (options.include_prepared_blocks) {
        append_prepared_blocks(result.prepared.bundle, out);
    }
    if (options.include_writeback_summary) {
        append_writeback_summary(result, options, out);
    }
    if (persisted) {
        append_persist_summary(*persisted, options, out);
    }
    return true;
}

}  // namespace openmeta
