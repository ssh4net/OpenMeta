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


static MetaKey
copy_key(const MetaKey& key, const ByteArena& src, ByteArena& dst)
{
    MetaKey out = key;

    switch (key.kind) {
    case MetaKeyKind::ExifTag:
        out.data.exif_tag.ifd = dst.append(src.span(key.data.exif_tag.ifd));
        return out;
    case MetaKeyKind::Comment: return out;
    case MetaKeyKind::ExrAttribute:
        out.data.exr_attribute.name = dst.append(
            src.span(key.data.exr_attribute.name));
        return out;
    case MetaKeyKind::IptcDataset: return out;
    case MetaKeyKind::XmpProperty:
        out.data.xmp_property.schema_ns = dst.append(
            src.span(key.data.xmp_property.schema_ns));
        out.data.xmp_property.property_path = dst.append(
            src.span(key.data.xmp_property.property_path));
        return out;
    case MetaKeyKind::IccHeaderField:
    case MetaKeyKind::IccTag:
    case MetaKeyKind::PhotoshopIrb:
    case MetaKeyKind::GeotiffKey: return out;
    case MetaKeyKind::PhotoshopIrbField:
        out.data.photoshop_irb_field.field = dst.append(
            src.span(key.data.photoshop_irb_field.field));
        return out;
    case MetaKeyKind::PrintImField:
        out.data.printim_field.field = dst.append(
            src.span(key.data.printim_field.field));
        return out;
    case MetaKeyKind::BmffField:
        out.data.bmff_field.field = dst.append(
            src.span(key.data.bmff_field.field));
        return out;
    case MetaKeyKind::JumbfField:
        out.data.jumbf_field.field = dst.append(
            src.span(key.data.jumbf_field.field));
        return out;
    case MetaKeyKind::JumbfCborKey:
        out.data.jumbf_cbor_key.key = dst.append(
            src.span(key.data.jumbf_cbor_key.key));
        return out;
    case MetaKeyKind::PngText:
        out.data.png_text.keyword = dst.append(
            src.span(key.data.png_text.keyword));
        out.data.png_text.field = dst.append(src.span(key.data.png_text.field));
        return out;
    }
    return out;
}


static MetaValue
copy_value(const MetaValue& value, const ByteArena& src, ByteArena& dst)
{
    MetaValue out = value;
    if (value.kind == MetaValueKind::Bytes || value.kind == MetaValueKind::Text
        || value.kind == MetaValueKind::Array) {
        out.data.span = dst.append(src.span(value.data.span));
    }
    return out;
}

static Origin
copy_origin(const Origin& origin, const ByteArena& src, ByteArena& dst)
{
    Origin out = origin;
    if (origin.wire_type_name.size > 0U) {
        out.wire_type_name = dst.append(src.span(origin.wire_type_name));
    }
    return out;
}


MetaStore
commit(const MetaStore& base, std::span<const MetaEdit> edits)
{
    MetaStore out  = base;
    out.finalized_ = false;
    out.clear_indices();

    for (size_t e = 0; e < edits.size(); ++e) {
        const MetaEdit& edit              = edits[e];
        const std::span<const EditOp> ops = edit.ops();

        for (size_t i = 0; i < ops.size(); ++i) {
            const EditOp& op = ops[i];
            switch (op.kind) {
            case EditOpKind::AddEntry: {
                Entry entry  = op.entry;
                entry.key    = copy_key(entry.key, edit.arena(), out.arena());
                entry.value  = copy_value(entry.value, edit.arena(),
                                          out.arena());
                entry.origin = copy_origin(entry.origin, edit.arena(),
                                           out.arena());
                out.entries_.push_back(entry);
                break;
            }
            case EditOpKind::SetValue:
            case EditOpKind::SetValueWithWire: {
                if (op.target >= out.entries_.size()) {
                    break;
                }
                Entry& updated = out.entries_[op.target];
                updated.value = copy_value(op.value, edit.arena(), out.arena());
                updated.flags |= EntryFlags::Dirty;
                if (op.kind == EditOpKind::SetValueWithWire) {
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
        }
    }

    out.finalize();
    return out;
}


MetaStore
compact(const MetaStore& base)
{
    MetaStore out;
    out.blocks_ = base.blocks_;

    const std::span<const Entry> entries = base.entries();
    for (EntryId id = 0; id < entries.size(); ++id) {
        const Entry& entry = entries[id];
        if (any(entry.flags, EntryFlags::Deleted)) {
            continue;
        }
        Entry copied  = entry;
        copied.key    = copy_key(entry.key, base.arena(), out.arena());
        copied.value  = copy_value(entry.value, base.arena(), out.arena());
        copied.origin = copy_origin(entry.origin, base.arena(), out.arena());
        out.entries_.push_back(copied);
    }

    out.finalize();
    return out;
}

}  // namespace openmeta
