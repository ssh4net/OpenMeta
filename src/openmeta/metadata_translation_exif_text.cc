// SPDX-License-Identifier: Apache-2.0
#include "metadata_text_fields_internal.h"
#include "openmeta/meta_edit.h"
#include "openmeta/metadata_translation.h"

#include <array>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {
    using Status  = MetadataCaptureTranslationStatus;
    using Policy  = MetadataCaptureTranslationConflictPolicy;
    using Mapping = MetadataCaptureTranslationMapping;

    struct TextField final {
        std::string_view name;
        uint16_t tag;
        Mapping mapping;
        bool MetadataExifTextTranslationOptions::* enabled;
    };
    constexpr std::array<TextField, 13> kFields = { {
        { "ExifVersion", 0x9000U, Mapping::XmpExifVersion,
          &MetadataExifTextTranslationOptions::exif_version_to_exif },
        { "FlashpixVersion", 0xa000U, Mapping::XmpFlashpixVersion,
          &MetadataExifTextTranslationOptions::flashpix_version_to_exif },
        { "UserComment", 0x9286U, Mapping::XmpUserComment,
          &MetadataExifTextTranslationOptions::user_comment_to_exif },
        { "ImageTitle", 0xa436U, Mapping::XmpImageTitle,
          &MetadataExifTextTranslationOptions::image_title_to_exif },
        { "Photographer", 0xa437U, Mapping::XmpPhotographer,
          &MetadataExifTextTranslationOptions::photographer_to_exif },
        { "ImageEditor", 0xa438U, Mapping::XmpImageEditor,
          &MetadataExifTextTranslationOptions::image_editor_to_exif },
        { "CameraFirmware", 0xa439U, Mapping::XmpCameraFirmware,
          &MetadataExifTextTranslationOptions::camera_firmware_to_exif },
        { "RAWDevelopingSoftware", 0xa43aU, Mapping::XmpRAWDevelopingSoftware,
          &MetadataExifTextTranslationOptions::raw_developing_software_to_exif },
        { "ImageEditingSoftware", 0xa43bU, Mapping::XmpImageEditingSoftware,
          &MetadataExifTextTranslationOptions::image_editing_software_to_exif },
        { "MetadataEditingSoftware", 0xa43cU,
          Mapping::XmpMetadataEditingSoftware,
          &MetadataExifTextTranslationOptions::metadata_editing_software_to_exif },
        { "CameraOwnerName", 0xa430U, Mapping::XmpCameraOwnerName,
          &MetadataExifTextTranslationOptions::camera_owner_name_to_exif },
        { "LensMake", 0xa433U, Mapping::XmpLensMake,
          &MetadataExifTextTranslationOptions::lens_make_to_exif },
        { "LensModel", 0xa434U, Mapping::XmpLensModel,
          &MetadataExifTextTranslationOptions::lens_model_to_exif },
    } };

    struct TextPlan final {
        EntryId source        = kInvalidEntryId;
        const Entry* native   = nullptr;
        uint32_t native_count = 0U;
        EntryId clean_deleted = kInvalidEntryId;
        bool dirty_deleted    = false;
        bool deleted          = false;
        bool apply            = false;
        bool promote          = false;
        bool ascii            = true;
        uint16_t type         = 7U;
        std::string_view text;
        std::vector<std::byte> raw;
    };

    std::string_view arena_text(const ByteArena& arena, ByteSpan span) noexcept
    {
        const auto raw = arena.span(span);
        return { reinterpret_cast<const char*>(raw.data()), raw.size() };
    }

    bool exif_text_entry(const ByteArena& arena, const Entry& entry,
                         uint16_t tag) noexcept
    {
        return entry.key.kind == MetaKeyKind::ExifTag
               && entry.key.data.exif_tag.tag == tag
               && arena_text(arena, entry.key.data.exif_tag.ifd) == "exififd";
    }

    bool comment_equals(const ByteArena& arena, const Entry& native,
                        uint32_t version, std::string_view expected) noexcept
    {
        detail::UserCommentView view;
        if (!detail::user_comment_value(arena, native.value, version,
                                        native.flags, &view))
            return false;
        size_t a = 0U, b = 0U;
        while (a < view.text.size() && b < expected.size()) {
            uint32_t ac = 0U, bc = 0U;
            if (!detail::user_comment_next(view, &a, &ac)
                || !detail::capture_utf8_next(expected, &b, &bc) || ac != bc)
                return false;
        }
        return a == view.text.size() && b == expected.size();
    }

    bool equivalent(const MetaStore& source, size_t index, const TextPlan& plan,
                    uint32_t version) noexcept
    {
        if (plan.native_count != 1U)
            return false;
        const Entry& e = *plan.native;
        const bool native_type129
            = e.origin.wire_type.family == WireFamily::Tiff
              && e.origin.wire_type.code == 129U;
        if (version < 300U
            && (detail::exif3_text_tag(kFields[index].tag) || native_type129))
            return false;
        if (e.origin.wire_type.family == WireFamily::Tiff
            && e.origin.wire_type.code != plan.type) {
            const bool utf8_ascii
                = index >= 3U && plan.type == 2U
                  && native_type129 && version >= 300U;
            if (!utf8_ascii)
                return false;
        }
        if (index == 2U)
            return comment_equals(source.arena(), e, version, plan.text);
        if (index < 2U) {
            uint32_t parsed = 0U;
            return detail::exif_version_value(source.arena(), e.value,
                                              kFields[index].tag, &parsed)
                   && arena_text(source.arena(), e.value.data.span)
                          == plan.text;
        }
        std::string_view native;
        return detail::exif_text_view(source.arena(), e.value, true, &native)
               && native == plan.text;
    }

    Status reconcile(const MetaStore& source, size_t index, Policy policy,
                     uint32_t version, TextPlan* plan,
                     MetadataCaptureTranslationResult* result) noexcept
    {
        if (plan->deleted) {
            if (plan->native_count != 0U
                && policy == Policy::PreserveExisting) {
                ++result->groups_preserved;
                return Status::Ok;
            }
            if (plan->native_count != 0U && policy == Policy::FailOnConflict)
                return Status::NativeConflict;
            if (plan->native_count == 0U && plan->dirty_deleted) {
                ++result->groups_unchanged;
                return Status::Ok;
            }
            plan->apply = true;
            return Status::Ok;
        }
        if (policy == Policy::PreserveExisting && plan->native_count != 0U) {
            ++result->groups_preserved;
            return Status::Ok;
        }
        if (equivalent(source, index, *plan, version)) {
            if (any(plan->native->flags, EntryFlags::Dirty)) {
                ++result->groups_unchanged;
                return Status::Ok;
            }
            plan->apply   = true;
            plan->promote = true;
        } else if (policy == Policy::FailOnConflict
                   && plan->native_count != 0U) {
            return Status::NativeConflict;
        } else {
            plan->apply = true;
        }
        return Status::Ok;
    }

    MetaValue copy_value_to_edit(const MetaStore& source, MetaEdit* edit,
                                 const MetaValue& value)
    {
        MetaValue copied = value;
        if (value.kind == MetaValueKind::Array
            || value.kind == MetaValueKind::Bytes
            || value.kind == MetaValueKind::Text) {
            copied.data.span
                = edit->arena().append(source.arena().span(value.data.span));
        }
        return copied;
    }

    void append_delete_marker(size_t index, MetaEdit* edit)
    {
        Entry entry;
        entry.key = make_exif_tag_key(edit->arena(), "exififd",
                                      kFields[index].tag);
        if (index < 3U) {
            entry.value = make_bytes(edit->arena(),
                                     std::span<const std::byte> {});
            entry.origin.wire_type = { WireFamily::Tiff, 7U };
            entry.origin.wire_count = 0U;
        } else {
            entry.value = make_text(edit->arena(), {}, TextEncoding::Utf8);
            entry.origin.wire_type = { WireFamily::Tiff, 2U };
            entry.origin.wire_count = 1U;
        }
        entry.flags = EntryFlags::Dirty | EntryFlags::Deleted;
        edit->add_entry(entry);
    }

    void append_u16(std::vector<std::byte>* out, uint32_t code)
    {
        out->push_back(static_cast<std::byte>(code & 255U));
        out->push_back(static_cast<std::byte>((code >> 8U) & 255U));
    }

    void encode_comment(TextPlan* plan, uint32_t version)
    {
        const char* marker = plan->ascii ? "ASCII\0\0\0" : "UNICODE\0";
        const auto* start  = reinterpret_cast<const std::byte*>(marker);
        plan->raw.assign(start, start + 8U);
        if (plan->ascii || version >= 300U) {
            const auto* text = reinterpret_cast<const std::byte*>(
                plan->text.data());
            plan->raw.insert(plan->raw.end(), text, text + plan->text.size());
            return;
        }
        append_u16(&plan->raw, 0xfeffU);
        size_t offset = 0U;
        while (offset < plan->text.size()) {
            uint32_t code = 0U;
            (void)detail::capture_utf8_next(plan->text, &offset, &code);
            if (code >= 0x10000U) {
                code -= 0x10000U;
                append_u16(&plan->raw, 0xd800U + (code >> 10U));
                append_u16(&plan->raw, 0xdc00U + (code & 1023U));
            } else {
                append_u16(&plan->raw, code);
            }
        }
    }

    bool companion_present(const MetaStore& source, uint16_t tag) noexcept
    {
        uint32_t count = 0U;
        for (const Entry& e : source.entries()) {
            if (any(e.flags, EntryFlags::Deleted)
                || e.key.kind != MetaKeyKind::ExifTag
                || e.key.data.exif_tag.tag != tag
                || arena_text(source.arena(), e.key.data.exif_tag.ifd)
                       != "ifd0")
                continue;
            std::string_view text;
            if (++count != 1U
                || !detail::exif_text_view(source.arena(), e.value, true, &text)
                || text.empty())
                return false;
        }
        return count == 1U;
    }
}  // namespace

MetadataCaptureTranslationResult
translate_xmp_exif_text_metadata(
    const MetaStore& source, const MetadataExifTextTranslationOptions& options,
    MetaStore* out_store)
{
    MetadataCaptureTranslationResult result;
    if (!out_store) {
        result.status = Status::NullOutput;
        return result;
    }
    if (!source.is_finalized()) {
        result.status = Status::SourceNotFinalized;
        return result;
    }
    if ((options.source_mode != MetadataCaptureTranslationSourceMode::All
         && options.source_mode
                != MetadataCaptureTranslationSourceMode::DirtyOnly)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || options.max_added_entries
               > kMetadataExifTextTranslationMaxAddedEntries
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property
               > kMetadataExifTextTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes
               > kMetadataExifTextTranslationMaxTotalTextBytes) {
        result.status = Status::InvalidOptions;
        return result;
    }
    std::array<TextPlan, kFields.size()> plans;
    uint64_t total = 0U;
    for (size_t id = 0U; id < source.entries().size(); ++id) {
        const Entry& e = source.entries()[id];
        if (e.key.kind == MetaKeyKind::ExifTag) {
            for (size_t i = 0U; i < plans.size(); ++i) {
                if (!exif_text_entry(source.arena(), e, kFields[i].tag))
                    continue;
                if (any(e.flags, EntryFlags::Deleted)) {
                    if (any(e.flags, EntryFlags::Dirty))
                        plans[i].dirty_deleted = true;
                    else if (plans[i].clean_deleted == kInvalidEntryId)
                        plans[i].clean_deleted = static_cast<EntryId>(id);
                } else {
                    if (plans[i].native_count++ == 0U)
                        plans[i].native = &e;
                }
            }
            continue;
        }
        const bool dirty   = any(e.flags, EntryFlags::Dirty);
        const bool deleted = any(e.flags, EntryFlags::Deleted);
        if (e.key.kind != MetaKeyKind::XmpProperty || (deleted && !dirty)
            || (!dirty
                && options.source_mode
                       == MetadataCaptureTranslationSourceMode::DirtyOnly))
            continue;
        const auto ns   = arena_text(source.arena(),
                                     e.key.data.xmp_property.schema_ns);
        const auto path = arena_text(source.arena(),
                                     e.key.data.xmp_property.property_path);
        for (size_t i = 0U; i < kFields.size(); ++i) {
            const TextField& f = kFields[i];
            if (!(options.*(f.enabled))
                || ns
                       != (i < 3U ? "http://ns.adobe.com/exif/1.0/"
                                  : "http://cipa.jp/exif/1.0/")
                || !path.starts_with(f.name))
                continue;
            const auto tail = path.substr(f.name.size());
            if (!tail.empty() && tail.front() != '[' && tail.front() != '/')
                continue;
            if (i == 2U && tail.starts_with("[@xml:lang=")
                && tail.ends_with("]") && tail != "[@xml:lang=x-default]")
                continue;
            result.failed_mapping      = f.mapping;
            result.failed_source_entry = static_cast<EntryId>(id);
            if (!tail.empty()
                && !(i == 2U && tail == "[@xml:lang=x-default]")) {
                result.status = Status::UnsupportedSourceShape;
                return result;
            }
            TextPlan& plan = plans[i];
            if (plan.source != kInvalidEntryId) {
                result.status = Status::AmbiguousSource;
                return result;
            }
            plan.source  = static_cast<EntryId>(id);
            plan.deleted = deleted;
            plan.type    = i < 3U ? 7U : 2U;
            ++result.source_properties;
            if (deleted)
                break;
            if (!detail::exif_text_view(source.arena(), e.value, false,
                                        &plan.text)) {
                result.status = Status::InvalidSourceValue;
                return result;
            }
            if (plan.text.size() > options.max_text_bytes_per_property) {
                result.status = Status::ValueTooLong;
                return result;
            }
            if (plan.text.size() > options.max_total_text_bytes
                || total > options.max_total_text_bytes - plan.text.size()) {
                result.status = Status::SourceLimitExceeded;
                return result;
            }
            total += plan.text.size();
            (void)detail::exif_text_valid(plan.text, &plan.ascii);
            plan.type = i < 3U ? 7U : (plan.ascii ? 2U : 129U);
            if (i < 2U) {
                uint32_t version = 0U;
                if (!detail::exif_version(source.arena().span(e.value.data.span),
                                          &version)
                    || (i == 1U && version != 100U)) {
                    result.status = Status::InvalidSourceValue;
                    return result;
                }
            }
            break;
        }
    }
    uint32_t old_version = 0U;
    const bool old_valid = detail::exif_store_version(source.arena(),
                                                      source.entries(),
                                                      &old_version);
    uint32_t version     = old_version;
    for (size_t i = 0U; i < plans.size(); ++i) {
        TextPlan& plan = plans[i];
        if (plan.source == kInvalidEntryId)
            continue;
        result.failed_mapping      = kFields[i].mapping;
        result.failed_source_entry = plan.source;
        result.status = reconcile(source, i, options.conflict_policy,
                                  old_version, &plan, &result);
        if (result.status != Status::Ok)
            return result;
        if (i == 2U && !plan.deleted && plans[0].apply
            && (old_version >= 300U) != (version >= 300U)
            && options.conflict_policy == Policy::ReplaceExisting
            && equivalent(source, i, plan, old_version)) {
            if (!plan.apply)
                --result.groups_unchanged;
            plan.apply = true;
            plan.promote = false;
        }
        if (i == 0U && plan.apply) {
            version = 0U;
            if (!plan.deleted) {
                const auto* raw = reinterpret_cast<const std::byte*>(
                    plan.text.data());
                (void)detail::exif_version(std::span(raw, plan.text.size()),
                                           &version);
            }
        }
        if (i >= 10U && !plan.deleted && plans[0].apply
            && old_version >= 300U && version < 300U
            && options.conflict_policy == Policy::ReplaceExisting
            && plan.ascii && equivalent(source, i, plan, old_version)
            && plan.native->origin.wire_type.family == WireFamily::Tiff
            && plan.native->origin.wire_type.code == 129U) {
            if (!plan.apply)
                --result.groups_unchanged;
            plan.apply   = true;
            plan.promote = false;
        }
        if (!plan.apply || plan.deleted)
            continue;
        if ((!old_valid && !plans[0].apply)
            || (!plan.promote
                && (detail::exif3_text_tag(kFields[i].tag)
                    || plan.type == 129U)
                && version < 300U)
            || ((i == 4U || i == 5U) && !companion_present(source, 0x013bU))
            || ((i >= 6U && i <= 9U) && !companion_present(source, 0x0131U))) {
            result.status = Status::IncompleteSource;
            return result;
        }
        if (i == 2U && !plan.promote)
            encode_comment(&plan, version);
    }
    // Changing the declared character set must not reinterpret retained values.
    if (plans[0].apply && (old_version >= 300U) != (version >= 300U)) {
        result.failed_mapping      = Mapping::XmpExifVersion;
        result.failed_source_entry = plans[0].source;
        for (const Entry& e : source.entries()) {
            if (e.key.kind != MetaKeyKind::ExifTag
                || any(e.flags, EntryFlags::Deleted))
                continue;
            bool replaced = false;
            for (size_t i = 0U; i < plans.size(); ++i)
                if (plans[i].apply && !plans[i].promote
                    && detail::primary_exif_entry(source.arena(), e,
                                                  kFields[i].tag))
                    replaced = true;
            if (replaced)
                continue;
            const uint16_t tag = e.key.data.exif_tag.tag;
            if (detail::primary_exif_entry(source.arena(), e, 0x9286U)) {
                detail::UserCommentView a, b;
                if (!old_valid
                    || !detail::user_comment_value(source.arena(), e.value,
                                                   old_version, e.flags, &a)
                    || !detail::user_comment_value(source.arena(), e.value,
                                                   version, e.flags, &b)
                    || a.encoding != b.encoding) {
                    result.status = Status::NativeConflict;
                    return result;
                }
            }
            const auto ifd = arena_text(source.arena(),
                                        e.key.data.exif_tag.ifd);
            if (version < 300U && detail::exif_utf8_tag(ifd, tag)) {
                std::string_view text;
                bool ascii = true;
                if ((ifd == "exififd" && detail::exif3_text_tag(tag))
                    || !detail::exif_text_view(source.arena(), e.value, true,
                                               &text)
                    || !detail::exif_text_valid(text, &ascii) || !ascii
                    || (e.origin.wire_type.family == WireFamily::Tiff
                        && e.origin.wire_type.code == 129U)) {
                    result.status = Status::NativeConflict;
                    return result;
                }
            }
        }
    }
    uint64_t operations = 0U;
    uint32_t additions  = 0U;
    for (const TextPlan& plan : plans) {
        if (!plan.apply)
            continue;
        if (plan.deleted) {
            if (plan.native_count != 0U) {
                operations += plan.native_count;
            } else if (!plan.dirty_deleted) {
                ++operations;
                if (plan.clean_deleted == kInvalidEntryId)
                    ++additions;
            }
        } else if (plan.promote) {
            ++operations;
        } else {
            operations += plan.native_count != 0U ? plan.native_count : 1U;
            if (plan.native_count == 0U)
                ++additions;
        }
    }
    if (additions > options.max_added_entries
        || source.entries().size() > kInvalidEntryId - additions) {
        result.status = Status::EntryLimitExceeded;
        return result;
    }
    if (operations > options.max_operations) {
        result.status = Status::OperationLimitExceeded;
        return result;
    }
    MetaEdit edit;
    edit.reserve_ops(static_cast<size_t>(operations));
    for (size_t i = 0U; i < plans.size(); ++i) {
        const TextPlan& plan = plans[i];
        if (!plan.apply)
            continue;
        ++result.groups_translated;
        if (plan.deleted) {
            bool active = false;
            for (size_t id = 0U; id < source.entries().size(); ++id) {
                const Entry& e = source.entries()[id];
                if (!exif_text_entry(source.arena(), e, kFields[i].tag)
                    || any(e.flags, EntryFlags::Deleted))
                    continue;
                edit.tombstone(static_cast<EntryId>(id));
                ++result.entries_removed;
                active = true;
            }
            if (!active) {
                if (plan.clean_deleted != kInvalidEntryId) {
                    edit.tombstone(plan.clean_deleted);
                    ++result.entries_updated;
                } else if (!plan.dirty_deleted) {
                    append_delete_marker(i, &edit);
                    ++result.entries_added;
                }
            }
            continue;
        }
        MetaValue value;
        uint32_t wire_count = 0U;
        if (!plan.promote) {
            if (i == 2U)
                value = make_bytes(edit.arena(), plan.raw);
            else if (i < 2U)
                value = make_bytes(edit.arena(),
                                   std::as_bytes(std::span(plan.text.data(),
                                                           plan.text.size())));
            else
                value = make_text(edit.arena(), plan.text,
                                  plan.ascii ? TextEncoding::Ascii
                                             : TextEncoding::Utf8);
            wire_count = value.count + (i >= 3U ? 1U : 0U);
        }
        bool written = false;
        for (size_t id = 0U; id < source.entries().size(); ++id) {
            if (!exif_text_entry(source.arena(), source.entries()[id],
                                 kFields[i].tag)
                || any(source.entries()[id].flags, EntryFlags::Deleted))
                continue;
            if (plan.promote && !written) {
                edit.set_value(static_cast<EntryId>(id),
                               copy_value_to_edit(source, &edit,
                                                  source.entries()[id].value));
                ++result.entries_updated;
                written = true;
            } else if (!plan.promote && !written) {
                edit.set_value(static_cast<EntryId>(id), value,
                               { WireFamily::Tiff, plan.type }, wire_count);
                ++result.entries_updated;
                written = true;
            } else {
                edit.tombstone(static_cast<EntryId>(id));
                ++result.entries_removed;
            }
        }
        if (!written) {
            if (plan.promote) {
                result.status = Status::InternalError;
                return result;
            }
            Entry e;
            e.key = make_exif_tag_key(edit.arena(), "exififd", kFields[i].tag);
            e.value             = value;
            e.origin.wire_type  = { WireFamily::Tiff, plan.type };
            e.origin.wire_count = wire_count;
            e.flags             = EntryFlags::Dirty;
            edit.add_entry(e);
            ++result.entries_added;
        }
    }
    if (edit.arena().limit_exceeded() || edit.ops().size() != operations) {
        result.status = Status::InternalError;
        return result;
    }
    MetaStore candidate = commit(source, std::span<const MetaEdit>(&edit, 1U));
    candidate.constrain_resources(0U, 0U);
    if (candidate.resource_limit_exceeded()) {
        result.status = Status::EntryLimitExceeded;
        return result;
    }
    *out_store                 = std::move(candidate);
    result.failed_mapping      = Mapping::None;
    result.failed_source_entry = kInvalidEntryId;
    return result;
}
}  // namespace openmeta
