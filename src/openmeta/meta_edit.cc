// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_edit.h"

#include <cstring>

namespace openmeta {

ByteArena&
MetaEdit::arena() noexcept
{
    return arena_;
}


const ByteArena&
MetaEdit::arena() const noexcept
{
    return arena_;
}


void
MetaEdit::reserve_ops(size_t count)
{
    ops_.reserve(count);
}


void
MetaEdit::add_entry(const Entry& entry)
{
    EditOp op;
    op.kind  = EditOpKind::AddEntry;
    op.entry = entry;
    ops_.push_back(op);
}


void
MetaEdit::set_value(EntryId target, const MetaValue& value)
{
    EditOp op;
    op.kind   = EditOpKind::SetValue;
    op.target = target;
    op.value  = value;
    ops_.push_back(op);
}


void
MetaEdit::set_value(EntryId target, const MetaValue& value, WireType wire_type,
                    uint32_t wire_count)
{
    EditOp op;
    op.kind                    = EditOpKind::SetValueWithWire;
    op.target                  = target;
    op.value                   = value;
    op.entry.origin.wire_type  = wire_type;
    op.entry.origin.wire_count = wire_count;
    ops_.push_back(op);
}


void
MetaEdit::tombstone(EntryId target)
{
    EditOp op;
    op.kind   = EditOpKind::Tombstone;
    op.target = target;
    ops_.push_back(op);
}


std::span<const EditOp>
MetaEdit::ops() const noexcept
{
    return std::span<const EditOp>(ops_.data(), ops_.size());
}


static bool
copy_span(ByteSpan source, const ByteArena& src, ByteArena& dst,
          ByteSpan* copied)
{
    if (!copied) {
        return false;
    }
    const size_t source_size = src.bytes().size();
    const size_t offset = static_cast<size_t>(source.offset);
    const size_t size   = static_cast<size_t>(source.size);
    if (offset > source_size || size > source_size - offset) {
        *copied = ByteSpan {};
        return false;
    }
    const std::span<const std::byte> bytes = src.span(source);
    *copied = dst.append(bytes);
    return bytes.size() == size && copied->size == source.size
           && !dst.limit_exceeded();
}


static MetaKey
copy_key(const MetaKey& key, const ByteArena& src, ByteArena& dst,
         bool* success)
{
    MetaKey out = key;
    *success    = true;

    switch (key.kind) {
    case MetaKeyKind::ExifTag:
        *success = copy_span(key.data.exif_tag.ifd, src, dst,
                             &out.data.exif_tag.ifd);
        return out;
    case MetaKeyKind::Comment: return out;
    case MetaKeyKind::ExrAttribute:
        *success = copy_span(key.data.exr_attribute.name, src, dst,
                             &out.data.exr_attribute.name);
        return out;
    case MetaKeyKind::IptcDataset: return out;
    case MetaKeyKind::XmpProperty:
        *success = copy_span(key.data.xmp_property.schema_ns, src, dst,
                             &out.data.xmp_property.schema_ns)
                   && copy_span(key.data.xmp_property.property_path, src,
                                dst, &out.data.xmp_property.property_path);
        return out;
    case MetaKeyKind::IccHeaderField:
    case MetaKeyKind::IccTag:
    case MetaKeyKind::PhotoshopIrb:
    case MetaKeyKind::GeotiffKey: return out;
    case MetaKeyKind::PhotoshopIrbField:
        *success = copy_span(key.data.photoshop_irb_field.field, src, dst,
                             &out.data.photoshop_irb_field.field);
        return out;
    case MetaKeyKind::PrintImField:
        *success = copy_span(key.data.printim_field.field, src, dst,
                             &out.data.printim_field.field);
        return out;
    case MetaKeyKind::BmffField:
        *success = copy_span(key.data.bmff_field.field, src, dst,
                             &out.data.bmff_field.field);
        return out;
    case MetaKeyKind::JumbfField:
        *success = copy_span(key.data.jumbf_field.field, src, dst,
                             &out.data.jumbf_field.field);
        return out;
    case MetaKeyKind::JumbfCborKey:
        *success = copy_span(key.data.jumbf_cbor_key.key, src, dst,
                             &out.data.jumbf_cbor_key.key);
        return out;
    case MetaKeyKind::PngText:
        *success = copy_span(key.data.png_text.keyword, src, dst,
                             &out.data.png_text.keyword)
                   && copy_span(key.data.png_text.field, src, dst,
                                &out.data.png_text.field);
        return out;
    }
    return out;
}


static MetaValue
copy_value(const MetaValue& value, const ByteArena& src, ByteArena& dst,
           bool* success)
{
    MetaValue out = value;
    *success = true;
    if (value.kind == MetaValueKind::Bytes || value.kind == MetaValueKind::Text
        || value.kind == MetaValueKind::Array) {
        *success = copy_span(value.data.span, src, dst, &out.data.span);
    }
    return out;
}

static Origin
copy_origin(const Origin& origin, const ByteArena& src, ByteArena& dst,
            bool* success)
{
    Origin out = origin;
    if (origin.wire_type_name.size == 0U) {
        out.wire_type_name = {};
        *success           = true;
        return out;
    }
    *success = copy_span(origin.wire_type_name, src, dst,
                         &out.wire_type_name);
    return out;
}


MetaStore
commit(const MetaStore& base, std::span<const MetaEdit> edits)
{
    if (base.resource_limit_exceeded()) {
        MetaStore unchanged = base;
        unchanged.entry_limit_exceeded_ = true;
        return unchanged;
    }
    for (size_t e = 0; e < edits.size(); ++e) {
        if (edits[e].arena().limit_exceeded()) {
            MetaStore unchanged = base;
            unchanged.entry_limit_exceeded_ = true;
            return unchanged;
        }
    }

    MetaStore out  = base;
    out.finalized_ = false;
    out.clear_indices();
    bool failed    = false;

    for (size_t e = 0; e < edits.size(); ++e) {
        const MetaEdit& edit              = edits[e];
        const std::span<const EditOp> ops = edit.ops();

        for (size_t i = 0; i < ops.size(); ++i) {
            const EditOp& op = ops[i];
            switch (op.kind) {
            case EditOpKind::AddEntry: {
                if (out.entries_.size() >= out.max_entries_
                    || out.entries_.size()
                           >= static_cast<size_t>(kInvalidEntryId)) {
                    failed = true;
                    break;
                }
                Entry entry  = op.entry;
                bool copy_succeeded = false;
                entry.key = copy_key(entry.key, edit.arena(), out.arena(),
                                     &copy_succeeded);
                if (!copy_succeeded) {
                    failed = true;
                    break;
                }
                entry.value = copy_value(entry.value, edit.arena(),
                                         out.arena(), &copy_succeeded);
                if (!copy_succeeded) {
                    failed = true;
                    break;
                }
                entry.origin = copy_origin(entry.origin, edit.arena(),
                                           out.arena(), &copy_succeeded);
                if (!copy_succeeded) {
                    failed = true;
                    break;
                }
                out.entries_.push_back(entry);
                break;
            }
            case EditOpKind::SetValue:
            case EditOpKind::SetValueWithWire: {
                if (op.target >= out.entries_.size()) {
                    break;
                }
                Entry& updated = out.entries_[op.target];
                bool copy_succeeded = false;
                const MetaValue value
                    = copy_value(op.value, edit.arena(), out.arena(),
                                 &copy_succeeded);
                if (!copy_succeeded) {
                    failed = true;
                    break;
                }
                updated.value = value;
                updated.flags |= EntryFlags::Dirty;
                if (op.kind == EditOpKind::SetValueWithWire) {
                    updated.flags = static_cast<EntryFlags>(
                        static_cast<uint8_t>(updated.flags)
                        & ~static_cast<uint8_t>(EntryFlags::ValueBigEndian));
                    updated.origin.wire_type      = op.entry.origin.wire_type;
                    updated.origin.wire_count     = op.entry.origin.wire_count;
                    updated.origin.wire_type_name = {};
                }
                break;
            }
            case EditOpKind::Tombstone: {
                if (op.target >= out.entries_.size()) {
                    break;
                }
                out.entries_[op.target].flags |= (EntryFlags::Deleted
                                                  | EntryFlags::Dirty);
                break;
            }
            }
            if (failed) {
                break;
            }
        }
        if (failed) {
            break;
        }
    }

    if (failed || out.resource_limit_exceeded()) {
        MetaStore unchanged = base;
        unchanged.entry_limit_exceeded_ = true;
        return unchanged;
    }

    out.finalize();
    return out;
}


MetaStore
compact(const MetaStore& base)
{
    if (base.resource_limit_exceeded()) {
        MetaStore unchanged = base;
        unchanged.entry_limit_exceeded_ = true;
        return unchanged;
    }

    MetaStore out = base;
    out.entries_.clear();
    out.arena_.clear();
    out.clear_indices();
    out.finalized_ = false;

    const std::span<const Entry> entries = base.entries();
    for (EntryId id = 0; id < entries.size(); ++id) {
        const Entry& entry = entries[id];
        if (any(entry.flags, EntryFlags::Deleted)) {
            continue;
        }
        Entry copied  = entry;
        bool copy_succeeded = false;
        copied.key = copy_key(entry.key, base.arena(), out.arena(),
                              &copy_succeeded);
        if (!copy_succeeded) {
            MetaStore unchanged = base;
            unchanged.entry_limit_exceeded_ = true;
            return unchanged;
        }
        copied.value = copy_value(entry.value, base.arena(), out.arena(),
                                  &copy_succeeded);
        if (!copy_succeeded) {
            MetaStore unchanged = base;
            unchanged.entry_limit_exceeded_ = true;
            return unchanged;
        }
        copied.origin = copy_origin(entry.origin, base.arena(), out.arena(),
                                    &copy_succeeded);
        if (!copy_succeeded) {
            MetaStore unchanged = base;
            unchanged.entry_limit_exceeded_ = true;
            return unchanged;
        }
        out.entries_.push_back(copied);
    }

    if (out.resource_limit_exceeded()) {
        MetaStore unchanged = base;
        unchanged.entry_limit_exceeded_ = true;
        return unchanged;
    }

    out.finalize();
    return out;
}

}  // namespace openmeta
