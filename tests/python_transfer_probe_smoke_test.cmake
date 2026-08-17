cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED OPENMETA_PYTHON_EXECUTABLE OR OPENMETA_PYTHON_EXECUTABLE STREQUAL "")
  message(FATAL_ERROR "OPENMETA_PYTHON_EXECUTABLE is required")
endif()
if(NOT EXISTS "${OPENMETA_PYTHON_EXECUTABLE}")
  message(FATAL_ERROR "Python executable not found: ${OPENMETA_PYTHON_EXECUTABLE}")
endif()

if(NOT DEFINED OPENMETA_PYTHONPATH OR OPENMETA_PYTHONPATH STREQUAL "")
  message(FATAL_ERROR "OPENMETA_PYTHONPATH is required")
endif()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_python_transfer_probe_smoke")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_jpg "${WORK_DIR}/sample.jpg")
set(_jpg_exr "${WORK_DIR}/sample_exr.jpg")

set(_py_code
"from pathlib import Path
import openmeta
from openmeta.python import get_exr_attribute_batch, get_libraw_orientation, probe_exr_attribute_batch, probe_libraw_orientation, update_dng_sdk_file

assert openmeta.INTEROP_EXPORT_CONTRACT_VERSION == 1
assert openmeta.FLAT_HOST_EXPORT_CONTRACT_VERSION == 1
assert openmeta.COMPATIBILITY_DUMP_CONTRACT_VERSION == 1

cap = openmeta.metadata_capability(
    openmeta.TransferTargetFormat.Avif,
    openmeta.MetadataCapabilityFamily.Xmp,
)
assert cap['format'] == openmeta.TransferTargetFormat.Avif, cap
assert cap['family'] == openmeta.MetadataCapabilityFamily.Xmp, cap
assert cap['family_name'] == 'xmp', cap
assert cap['target_edit'] == openmeta.MetadataCapabilitySupport.Bounded, cap
assert cap['target_edit_available'] is True, cap
assert openmeta.metadata_capability_support_name(cap['target_edit']) == cap['target_edit_name'], cap

bmff_cap = openmeta.metadata_capability(
    openmeta.TransferTargetFormat.Cr3,
    openmeta.MetadataCapabilityFamily.BmffFields,
)
assert bmff_cap['read'] == openmeta.MetadataCapabilitySupport.Supported, bmff_cap
assert bmff_cap['target_edit'] == openmeta.MetadataCapabilitySupport.Unsupported, bmff_cap
assert openmeta.metadata_capability_available(bmff_cap['target_edit']) is False, bmff_cap

p = Path(r'''${_jpg}''')
p_exr = Path(r'''${_jpg_exr}''')
p_orient_exif = Path(r'''${WORK_DIR}/orient_exif.tif''')
p_orient_default = Path(r'''${WORK_DIR}/orient_default.tif''')
p_orient_xmp = Path(r'''${WORK_DIR}/orient_xmp.jpg''')
p_dng_target = Path(r'''${WORK_DIR}/target_sdk.dng''')
p_dng_target_helper = Path(r'''${WORK_DIR}/target_sdk_helper.dng''')
t = bytearray()
t += b'II*\\x00'
t += (8).to_bytes(4, 'little')
t += (1).to_bytes(2, 'little')
t += (0x0132).to_bytes(2, 'little')
t += (2).to_bytes(2, 'little')
t += (20).to_bytes(4, 'little')
t += (26).to_bytes(4, 'little')
t += (0).to_bytes(4, 'little')
t += b'2000:01:02 03:04:05\\x00'
app1 = b'Exif\\x00\\x00' + bytes(t)
ln = (len(app1) + 2).to_bytes(2, 'big')
p.write_bytes(b'\\xFF\\xD8\\xFF\\xE1' + ln + app1 + b'\\xFF\\xD9')

t_exr = bytearray()
t_exr += b'II*\\x00'
t_exr += (8).to_bytes(4, 'little')
t_exr += (1).to_bytes(2, 'little')
t_exr += (0x010F).to_bytes(2, 'little')
t_exr += (2).to_bytes(2, 'little')
t_exr += (7).to_bytes(4, 'little')
t_exr += (26).to_bytes(4, 'little')
t_exr += (0).to_bytes(4, 'little')
t_exr += b'Vendor\\x00'
app1_exr = b'Exif\\x00\\x00' + bytes(t_exr)
ln_exr = (len(app1_exr) + 2).to_bytes(2, 'big')
p_exr.write_bytes(b'\\xFF\\xD8\\xFF\\xE1' + ln_exr + app1_exr + b'\\xFF\\xD9')

t_orient = bytearray()
t_orient += b'II*\\x00'
t_orient += (8).to_bytes(4, 'little')
t_orient += (1).to_bytes(2, 'little')
t_orient += (0x0112).to_bytes(2, 'little')
t_orient += (3).to_bytes(2, 'little')
t_orient += (1).to_bytes(4, 'little')
t_orient += (6).to_bytes(2, 'little')
t_orient += (0).to_bytes(2, 'little')
t_orient += (0).to_bytes(4, 'little')
p_orient_exif.write_bytes(bytes(t_orient))

t_orient_default = bytearray()
t_orient_default += b'II*\\x00'
t_orient_default += (8).to_bytes(4, 'little')
t_orient_default += (0).to_bytes(2, 'little')
t_orient_default += (0).to_bytes(4, 'little')
p_orient_default.write_bytes(bytes(t_orient_default))

xmp = (
    b\"<?xpacket begin='\\xef\\xbb\\xbf' id='W5M0MpCehiHzreSzNTczkc9d'?>\"
    b\"<x:xmpmeta xmlns:x='adobe:ns:meta/'>\"
    b\"<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>\"
    b\"<rdf:Description xmlns:tiff='http://ns.adobe.com/tiff/1.0/' \"
    b\"tiff:Orientation='8'/>\"
    b\"</rdf:RDF></x:xmpmeta><?xpacket end='w'?>\"
)
app1_xmp = b'http://ns.adobe.com/xap/1.0/\\x00' + xmp
ln_xmp = (len(app1_xmp) + 2).to_bytes(2, 'big')
p_orient_xmp.write_bytes(b'\\xFF\\xD8\\xFF\\xE1' + ln_xmp + app1_xmp + b'\\xFF\\xD9')

dng = bytearray()
dng += b'II'
dng += (42).to_bytes(2, 'little')
dng += (8).to_bytes(4, 'little')
dng += (1).to_bytes(2, 'little')
dng += (0xC612).to_bytes(2, 'little')
dng += (1).to_bytes(2, 'little')
dng += (4).to_bytes(4, 'little')
dng += bytes([1, 6, 0, 0])
dng += (0).to_bytes(4, 'little')
p_dng_target.write_bytes(bytes(dng))
p_dng_target_helper.write_bytes(bytes(dng))

p_icc = Path(r'''${WORK_DIR}/sample_icc.jpg''')
icc = bytearray(156)
icc[0:4] = (156).to_bytes(4, 'big')
icc[36:40] = b'acsp'
icc[128:132] = (1).to_bytes(4, 'big')
icc[132:136] = b'desc'
icc[136:140] = (144).to_bytes(4, 'big')
icc[140:144] = (12).to_bytes(4, 'big')
icc[144:156] = bytes([0x11]) * 12
app2 = b'ICC_PROFILE\\x00\\x01\\x01' + bytes(icc)
ln2 = (len(app2) + 2).to_bytes(2, 'big')
p_icc.write_bytes(b'\\xFF\\xD8\\xFF\\xE2' + ln2 + app2 + b'\\xFF\\xD9')

r = openmeta.transfer_probe(
    str(p),
    format=openmeta.XmpSidecarFormat.Portable,
    include_payloads=False,
    time_patches={'DateTime': '2024:12:31 23:59:59'},
)
assert r['file_status'] == openmeta.TransferFileStatus.Ok, r
assert r['prepare_status'] == openmeta.TransferStatus.Ok, r
assert r['time_patch_status'] == openmeta.TransferStatus.Ok, r
assert r['time_patch_patched_slots'] >= 1, r
assert r['emit_status'] == openmeta.TransferStatus.Ok, r
assert len(r['blocks']) >= 1, r
assert len(r['marker_summary']) >= 1, r

snapshot_file = openmeta.read_transfer_source_snapshot_file(str(p))
assert snapshot_file['overall_status'] == openmeta.TransferStatus.Ok, snapshot_file
assert snapshot_file['file_status'] == openmeta.TransferFileStatus.Ok, snapshot_file
assert snapshot_file['code_name'] == 'none', snapshot_file
assert snapshot_file['entry_count'] == r['entry_count'], snapshot_file
assert snapshot_file['snapshot'].entry_count == r['entry_count'], snapshot_file
maker_note_audit = snapshot_file['snapshot'].makernote_transfer_audit()
assert maker_note_audit['trust'] == openmeta.TransferMakerNoteTrust.NotPresent, maker_note_audit
assert maker_note_audit['trust_name'] == 'not_present', maker_note_audit
assert maker_note_audit['raw_payload_count'] == 0, maker_note_audit
assert maker_note_audit['decoded_only_entry_count'] == 0, maker_note_audit
assert maker_note_audit['decoded_rewrite_available'] is False, maker_note_audit
assert maker_note_audit['internal_offset_relocation_available'] is False, maker_note_audit
assert maker_note_audit['vendor_checksum_recalculation_available'] is False, maker_note_audit
assert maker_note_audit['semantic_validation_available'] is False, maker_note_audit
assert maker_note_audit['raw_carrier_passthrough_available'] is False, maker_note_audit
maker_note_layout_audit = snapshot_file['snapshot'].makernote_layout_transfer_audit()
assert maker_note_layout_audit['trust'] == openmeta.TransferMakerNoteLayoutTrust.NotPresent, maker_note_layout_audit
assert maker_note_layout_audit['vendor'] == openmeta.TransferMakerNoteVendor.Unknown, maker_note_layout_audit
assert maker_note_layout_audit['layout'] == openmeta.TransferMakerNoteLayout.UnknownOrMixed, maker_note_layout_audit
assert maker_note_layout_audit['raw_payload_count'] == 0, maker_note_layout_audit
assert maker_note_layout_audit['vendor_private_offsets_verified'] is False, maker_note_layout_audit
assert maker_note_layout_audit['vendor_checksum_validation_available'] is False, maker_note_layout_audit
assert maker_note_layout_audit['semantic_roundtrip_validation_available'] is False, maker_note_layout_audit

snapshot_bytes = openmeta.read_transfer_source_snapshot_bytes(p.read_bytes())
assert snapshot_bytes['overall_status'] == openmeta.TransferStatus.Ok, snapshot_bytes
assert snapshot_bytes['status'] == openmeta.TransferStatus.Ok, snapshot_bytes
assert snapshot_bytes['code_name'] == 'none', snapshot_bytes
assert snapshot_bytes['entry_count'] == r['entry_count'], snapshot_bytes

snapshot_raw = openmeta.read_transfer_source_snapshot_bytes(
    p.read_bytes(),
    preserve_raw_carriers=True,
)
assert snapshot_raw['overall_status'] == openmeta.TransferStatus.Ok, snapshot_raw
assert snapshot_raw['raw_carrier_count'] >= 1, snapshot_raw
assert snapshot_raw['snapshot'].raw_carrier_count == snapshot_raw['raw_carrier_count'], snapshot_raw
raw_carriers = snapshot_raw['snapshot'].raw_carriers(include_payload=True)
assert raw_carriers[0]['payload_preserved'] is True, raw_carriers
assert isinstance(raw_carriers[0]['payload'], (bytes, bytearray)), raw_carriers
assert raw_carriers[0]['decoded_entry_count'] >= 1, raw_carriers
assert len(raw_carriers[0]['decoded_entry_ids']) == raw_carriers[0]['decoded_entry_count'], raw_carriers
raw_audit = snapshot_raw['snapshot'].raw_carrier_passthrough_audit(
    target_format=openmeta.TransferTargetFormat.Jpeg,
    safety=openmeta.TransferSafetyMode.CompatibleFile,
)
assert raw_audit['carrier_count'] == snapshot_raw['raw_carrier_count'], raw_audit
assert raw_audit['eligible_count'] >= 1, raw_audit
assert raw_audit['decisions'][0]['reason_name'] == 'candidate', raw_audit
assert openmeta.TransferRawCarrierPassthroughMode.WhenSafe != openmeta.TransferRawCarrierPassthroughMode.Disabled

r_snapshot_raw_mode = openmeta.transfer_snapshot_probe(
    snapshot_raw['snapshot'],
    format=openmeta.XmpSidecarFormat.Portable,
    raw_carrier_passthrough_mode=openmeta.TransferRawCarrierPassthroughMode.WhenSafe,
)
assert r_snapshot_raw_mode['overall_status'] == openmeta.TransferStatus.Ok, r_snapshot_raw_mode
assert r_snapshot_raw_mode['prepare_status'] == openmeta.TransferStatus.Ok, r_snapshot_raw_mode

doc_snapshot = openmeta.read(str(p)).build_transfer_source_snapshot()
assert doc_snapshot.entry_count == r['entry_count'], doc_snapshot
doc_maker_note_audit = openmeta.read(str(p)).makernote_transfer_audit()
assert doc_maker_note_audit == maker_note_audit, doc_maker_note_audit
doc_maker_note_layout_audit = openmeta.read(str(p)).makernote_layout_transfer_audit()
assert doc_maker_note_layout_audit == maker_note_layout_audit, doc_maker_note_layout_audit
snapshot_dump = doc_snapshot.compatibility_dump()
assert snapshot_dump.startswith('openmeta.compat.metadata version=1'), snapshot_dump
module_snapshot = openmeta.build_transfer_source_snapshot(openmeta.read(str(p)))
assert module_snapshot.entry_count == r['entry_count'], module_snapshot
doc_dump = openmeta.read(str(p)).compatibility_dump(max_value_bytes=32)
assert 'style=\"flat_host\"' in doc_dump, doc_dump

r_snapshot = openmeta.transfer_snapshot_probe(
    snapshot_file['snapshot'],
    format=openmeta.XmpSidecarFormat.Portable,
    time_patches={'DateTime': '2024:12:31 23:59:59'},
)
assert r_snapshot['overall_status'] == openmeta.TransferStatus.Ok, r_snapshot
assert r_snapshot['prepare_status'] == openmeta.TransferStatus.Ok, r_snapshot
assert r_snapshot['time_patch_status'] == openmeta.TransferStatus.Ok, r_snapshot
assert r_snapshot['emit_status'] == openmeta.TransferStatus.Ok, r_snapshot
assert len(r_snapshot['blocks']) >= 1, r_snapshot

r_snapshot_edit = openmeta.unsafe_transfer_snapshot_probe(
    snapshot_bytes['snapshot'],
    edit_target_path='memory_target.jpg',
    target_bytes=bytes([255, 216, 255, 217]),
    time_patches={'DateTime': '2024:12:31 23:59:59'},
    include_edited_bytes=True,
)
assert r_snapshot_edit['overall_status'] == openmeta.TransferStatus.Ok, r_snapshot_edit
assert r_snapshot_edit['edit_requested'] is True, r_snapshot_edit
assert r_snapshot_edit['edit_plan_status'] == openmeta.TransferStatus.Ok, r_snapshot_edit
assert r_snapshot_edit['edit_apply_status'] == openmeta.TransferStatus.Ok, r_snapshot_edit
assert isinstance(r_snapshot_edit['edited_bytes'], (bytes, bytearray)), r_snapshot_edit
assert bytes(r_snapshot_edit['edited_bytes']).startswith(b'\\xFF\\xD8'), r_snapshot_edit

snapshot_output = Path(r'''${WORK_DIR}/snapshot_edit.jpg''')
r_snapshot_file = openmeta.transfer_snapshot_file(
    doc_snapshot,
    edit_target_path='memory_target.jpg',
    target_bytes=bytes([255, 216, 255, 217]),
    time_patches={'DateTime': '2024:12:31 23:59:59'},
    output_path=str(snapshot_output),
)
assert r_snapshot_file['overall_status'] == openmeta.TransferStatus.Ok, r_snapshot_file
assert r_snapshot_file['persist_status'] == openmeta.TransferStatus.Ok, r_snapshot_file
assert r_snapshot_file['persist_output_status'] == openmeta.TransferStatus.Ok, r_snapshot_file
assert snapshot_output.exists(), r_snapshot_file

r_jxl = openmeta.transfer_probe(
    str(p_icc),
    target_format=openmeta.TransferTargetFormat.Jxl,
    format=openmeta.XmpSidecarFormat.Portable,
)
assert r_jxl['overall_status'] == openmeta.TransferStatus.Ok, r_jxl
assert r_jxl['emit_status_name'] == 'ok', r_jxl
assert r_jxl['jxl_encoder_handoff'] is not None, r_jxl
assert r_jxl['jxl_encoder_handoff']['status_name'] == 'ok', r_jxl
assert r_jxl['jxl_encoder_handoff']['has_icc_profile'] is True, r_jxl
assert r_jxl['jxl_encoder_handoff']['icc_profile_bytes'] > 0, r_jxl

r_jxl_u = openmeta.unsafe_transfer_probe(
    str(p_icc),
    target_format=openmeta.TransferTargetFormat.Jxl,
    format=openmeta.XmpSidecarFormat.Portable,
    include_jxl_encoder_handoff_bytes=True,
)
assert r_jxl_u['jxl_encoder_handoff_requested'] is True, r_jxl_u
assert r_jxl_u['jxl_encoder_handoff_status_name'] == 'ok', r_jxl_u
assert r_jxl_u['jxl_encoder_handoff_code_name'] == 'none', r_jxl_u
assert r_jxl_u['jxl_encoder_handoff_bytes_written'] > 0, r_jxl_u
assert isinstance(r_jxl_u['jxl_encoder_handoff_bytes'], (bytes, bytearray)), r_jxl_u
assert bytes(r_jxl_u['jxl_encoder_handoff_bytes'])[:8] == b'OMJXICC1', r_jxl_u

jxl_handoff = openmeta.inspect_jxl_encoder_handoff(
    r_jxl_u['jxl_encoder_handoff_bytes'],
)
assert jxl_handoff['status_name'] == 'ok', jxl_handoff
assert jxl_handoff['code_name'] == 'none', jxl_handoff
assert jxl_handoff['has_icc_profile'] is True, jxl_handoff
assert jxl_handoff['icc_profile_bytes'] > 0, jxl_handoff
assert jxl_handoff['icc_profile'] is None, jxl_handoff

jxl_handoff_u = openmeta.unsafe_inspect_jxl_encoder_handoff(
    r_jxl_u['jxl_encoder_handoff_bytes'],
    include_icc_profile=True,
)
assert jxl_handoff_u['overall_status'] == openmeta.TransferStatus.Ok, jxl_handoff_u
assert isinstance(jxl_handoff_u['icc_profile'], (bytes, bytearray)), jxl_handoff_u

artifact_jxl = openmeta.inspect_transfer_artifact(
    r_jxl_u['jxl_encoder_handoff_bytes'],
)
assert artifact_jxl['status_name'] == 'ok', artifact_jxl
assert artifact_jxl['kind_name'] == 'jxl_encoder_handoff', artifact_jxl
assert artifact_jxl['target_format_name'] == 'jxl', artifact_jxl
assert artifact_jxl['icc_profile_bytes'] > 0, artifact_jxl

r_exr = openmeta.transfer_probe(
    str(p_exr),
    target_format=openmeta.TransferTargetFormat.Exr,
)
assert r_exr['overall_status'] == openmeta.TransferStatus.Ok, r_exr
assert len(r_exr['exr_attribute_summary']) == 1, r_exr
assert r_exr['exr_attribute_summary'][0]['name'] == 'Make', r_exr
assert r_exr['exr_attribute_batch_status'] == openmeta.ExrAdapterStatus.Ok, r_exr
assert r_exr['exr_attribute_batch_status_name'] == 'ok', r_exr
assert r_exr['exr_attribute_batch_exported'] == 1, r_exr
assert len(r_exr['exr_attribute_batch']) == 1, r_exr
assert r_exr['exr_attribute_batch'][0]['name'] == 'Make', r_exr
assert r_exr['exr_attribute_batch'][0]['type_name'] == 'string', r_exr
assert r_exr['exr_attribute_batch'][0]['bytes'] == 6, r_exr
assert r_exr['exr_attribute_batch'][0]['value'] is None, r_exr

r_exr_safe_values = openmeta.transfer_probe(
    str(p_exr),
    target_format=openmeta.TransferTargetFormat.Exr,
    include_exr_attribute_values=True,
)
assert r_exr_safe_values['overall_status'] == openmeta.TransferStatus.UnsafeData, r_exr_safe_values
assert r_exr_safe_values['error_stage'] == 'api', r_exr_safe_values
assert r_exr_safe_values['error_code'] == 'unsafe_exr_attribute_values_forbidden', r_exr_safe_values
assert r_exr_safe_values['exr_attribute_values_requested'] is True, r_exr_safe_values
assert r_exr_safe_values['exr_attribute_values_status_name'] == 'unsafe_data', r_exr_safe_values
assert r_exr_safe_values['exr_attribute_batch'][0]['value'] is None, r_exr_safe_values

r_exr_u = openmeta.unsafe_transfer_probe(
    str(p_exr),
    target_format=openmeta.TransferTargetFormat.Exr,
    include_exr_attribute_values=True,
)
assert r_exr_u['overall_status'] == openmeta.TransferStatus.Ok, r_exr_u
assert r_exr_u['exr_attribute_values_requested'] is True, r_exr_u
assert r_exr_u['exr_attribute_values_status_name'] == 'ok', r_exr_u
assert r_exr_u['exr_attribute_batch_status_name'] == 'ok', r_exr_u
assert len(r_exr_u['exr_attribute_batch']) == 1, r_exr_u
assert isinstance(r_exr_u['exr_attribute_batch'][0]['value'], (bytes, bytearray)), r_exr_u
assert bytes(r_exr_u['exr_attribute_batch'][0]['value']) == b'Vendor', r_exr_u

r_exr_helper = probe_exr_attribute_batch(str(p_exr))
assert r_exr_helper['overall_status_name'] == 'ok', r_exr_helper
assert r_exr_helper['exr_attribute_batch_status_name'] == 'ok', r_exr_helper
assert len(r_exr_helper['exr_attribute_batch']) == 1, r_exr_helper
assert r_exr_helper['exr_attribute_batch'][0]['name'] == 'Make', r_exr_helper
assert r_exr_helper['exr_attribute_batch'][0]['value'] is None, r_exr_helper

r_exr_direct = openmeta.build_exr_attribute_batch_from_file(
    str(p_exr),
    include_values=True,
)
assert r_exr_direct['overall_status_name'] == 'ok', r_exr_direct
assert r_exr_direct['exr_attribute_batch_status_name'] == 'ok', r_exr_direct
assert len(r_exr_direct['exr_attribute_batch']) == 1, r_exr_direct
assert isinstance(r_exr_direct['exr_attribute_batch'][0]['value'], (bytes, bytearray)), r_exr_direct
assert bytes(r_exr_direct['exr_attribute_batch'][0]['value']) == b'Vendor', r_exr_direct

batch_exr = get_exr_attribute_batch(str(p_exr), include_values=True)
assert len(batch_exr) == 1, batch_exr
assert batch_exr[0]['name'] == 'Make', batch_exr
assert batch_exr[0]['type_name'] == 'string', batch_exr
assert isinstance(batch_exr[0]['value'], (bytes, bytearray)), batch_exr
assert bytes(batch_exr[0]['value']) == b'Vendor', batch_exr

r_libraw_exif = openmeta.map_meta_orientation_to_libraw_flip_from_file(
    str(p_orient_exif),
)
assert r_libraw_exif['overall_status_name'] == 'ok', r_libraw_exif
assert r_libraw_exif['file_status_name'] == 'ok', r_libraw_exif
assert r_libraw_exif['orientation_status_name'] == 'ok', r_libraw_exif
assert r_libraw_exif['orientation_source_name'] == 'exif_ifd0', r_libraw_exif
assert r_libraw_exif['exif_orientation'] == 6, r_libraw_exif
assert r_libraw_exif['libraw_flip'] == 6, r_libraw_exif
assert r_libraw_exif['apply_flip'] is True, r_libraw_exif

r_libraw_xmp = probe_libraw_orientation(str(p_orient_xmp))
assert r_libraw_xmp['overall_status_name'] == 'ok', r_libraw_xmp
assert r_libraw_xmp['orientation_source_name'] == 'xmp_tiff_orientation', r_libraw_xmp
assert r_libraw_xmp['exif_orientation'] == 8, r_libraw_xmp
assert r_libraw_xmp['libraw_flip'] == 5, r_libraw_xmp

r_libraw_default = get_libraw_orientation(str(p_orient_default))
assert r_libraw_default['overall_status_name'] == 'ok', r_libraw_default
assert r_libraw_default['orientation_code_name'] == 'missing_exif_orientation_assumed_default', r_libraw_default
assert r_libraw_default['orientation_source_name'] == 'assumed_default', r_libraw_default
assert r_libraw_default['exif_orientation'] == 1, r_libraw_default
assert r_libraw_default['libraw_flip'] == 0, r_libraw_default

r_libraw_preview = probe_libraw_orientation(
    str(p_orient_exif),
    target=openmeta.LibRawOrientationTarget.EmbeddedPreview,
)
assert r_libraw_preview['orientation_code_name'] == 'preview_pass_through', r_libraw_preview
assert r_libraw_preview['preview_passthrough'] is True, r_libraw_preview

r_libraw_back = openmeta.map_libraw_flip_to_exif_orientation(5)
assert r_libraw_back['orientation_status_name'] == 'ok', r_libraw_back
assert r_libraw_back['orientation_code_name'] == 'none', r_libraw_back
assert r_libraw_back['exif_orientation'] == 8, r_libraw_back

r_libraw_back_preview = openmeta.map_libraw_flip_to_exif_orientation(
    6,
    target=openmeta.LibRawOrientationTarget.EmbeddedPreview,
)
assert r_libraw_back_preview['orientation_code_name'] == 'preview_pass_through', r_libraw_back_preview
assert r_libraw_back_preview['exif_orientation'] == 1, r_libraw_back_preview
assert r_libraw_back_preview['preview_passthrough'] is True, r_libraw_back_preview

r_dng = openmeta.update_dng_sdk_file_from_file(str(p_exr), str(p_dng_target))
if openmeta.dng_sdk_adapter_available():
    dng_before = bytes(dng)
    assert r_dng['overall_status_name'] == 'ok', r_dng
    assert r_dng['adapter_status_name'] == 'ok', r_dng
    assert r_dng['updated_stream'] is True, r_dng
    assert p_dng_target.read_bytes() != dng_before, r_dng

    r_dng_helper = update_dng_sdk_file(str(p_exr), str(p_dng_target_helper))
    assert r_dng_helper['overall_status_name'] == 'ok', r_dng_helper
    assert r_dng_helper['adapter_status_name'] == 'ok', r_dng_helper
    assert r_dng_helper['updated_stream'] is True, r_dng_helper
    assert p_dng_target_helper.read_bytes() != dng_before, r_dng_helper
else:
    assert r_dng['adapter_status_name'] == 'unsupported', r_dng

r2 = openmeta.transfer_probe(
    str(p),
    format=openmeta.XmpSidecarFormat.Portable,
    include_payloads=True,
)
assert r2['overall_status'] == openmeta.TransferStatus.UnsafeData, r2
assert r2['error_stage'] == 'api', r2
assert r2['error_code'] == 'unsafe_payloads_forbidden', r2
assert r2['blocks'], r2
assert r2['blocks'][0]['payload'] is None, r2

r3 = openmeta.unsafe_transfer_probe(
    str(p),
    format=openmeta.XmpSidecarFormat.Portable,
    include_payloads=True,
)
assert r3['overall_status'] == openmeta.TransferStatus.Ok, r3
assert r3['blocks'], r3
assert isinstance(r3['blocks'][0]['payload'], (bytes, bytearray)), r3

r3b = openmeta.unsafe_transfer_probe(
    str(p),
    format=openmeta.XmpSidecarFormat.Portable,
    include_transfer_payload_batch_bytes=True,
    include_transfer_package_batch_bytes=True,
)
assert r3b['transfer_payload_batch_requested'] is True, r3b
assert r3b['transfer_payload_batch_status_name'] == 'ok', r3b
assert r3b['transfer_payload_batch_code_name'] == 'none', r3b
assert r3b['transfer_payload_batch_bytes_written'] > 0, r3b
assert isinstance(r3b['transfer_payload_batch_bytes'], (bytes, bytearray)), r3b
assert bytes(r3b['transfer_payload_batch_bytes'])[:8] == b'OMTPLD01', r3b
assert r3b['transfer_package_batch_requested'] is True, r3b
assert r3b['transfer_package_batch_status_name'] == 'ok', r3b
assert r3b['transfer_package_batch_code_name'] == 'none', r3b
assert r3b['transfer_package_batch_bytes_written'] > 0, r3b
assert isinstance(r3b['transfer_package_batch_bytes'], (bytes, bytearray)), r3b
assert bytes(r3b['transfer_package_batch_bytes'])[:8] == b'OMTPKG01', r3b

artifact_payload = openmeta.inspect_transfer_artifact(
    r3b['transfer_payload_batch_bytes'],
)
assert artifact_payload['status_name'] == 'ok', artifact_payload
assert artifact_payload['kind_name'] == 'transfer_payload_batch', artifact_payload
assert artifact_payload['target_format_name'] == 'jpeg', artifact_payload
assert artifact_payload['entry_count'] >= 1, artifact_payload

inspected = openmeta.inspect_transfer_payload_batch(
    r3b['transfer_payload_batch_bytes'],
)
assert inspected['status_name'] == 'ok', inspected
assert inspected['code_name'] == 'none', inspected
assert inspected['target_format_name'] == 'jpeg', inspected
assert inspected['payload_count'] >= 1, inspected
assert inspected['payloads'][0]['semantic_name'] == 'Exif', inspected
assert inspected['payloads'][0]['payload'] is None, inspected

inspected_u = openmeta.unsafe_inspect_transfer_payload_batch(
    r3b['transfer_payload_batch_bytes'],
    include_payloads=True,
)
assert inspected_u['overall_status'] == openmeta.TransferStatus.Ok, inspected_u
assert inspected_u['payload_count'] >= 1, inspected_u
assert isinstance(inspected_u['payloads'][0]['payload'], (bytes, bytearray)), inspected_u

pkg = openmeta.inspect_transfer_package_batch(
    r3b['transfer_package_batch_bytes'],
)
assert pkg['status_name'] == 'ok', pkg
assert pkg['code_name'] == 'none', pkg
assert pkg['target_format_name'] == 'jpeg', pkg
assert pkg['chunk_count'] >= 1, pkg
assert pkg['chunks'][0]['semantic_name'] == 'Exif', pkg
assert pkg['chunks'][0]['bytes'] is None, pkg

pkg_u = openmeta.unsafe_inspect_transfer_package_batch(
    r3b['transfer_package_batch_bytes'],
    include_chunk_bytes=True,
)
assert pkg_u['overall_status'] == openmeta.TransferStatus.Ok, pkg_u
assert pkg_u['chunk_count'] >= 1, pkg_u
assert isinstance(pkg_u['chunks'][0]['bytes'], (bytes, bytearray)), pkg_u

p_c2pa = Path(r'''${WORK_DIR}/sample_c2pa.jpg''')
cbor = bytes([0xA1, 0x61, 0x61, 0x01])
jumd = b'c2pa\\x00'
box = lambda t, payload: (8 + len(payload)).to_bytes(4, 'big') + t + payload
jumb = box(b'jumb', box(b'jumd', jumd) + box(b'cbor', cbor))
seg = b'JP\\x00\\x00' + (1).to_bytes(4, 'big') + jumb
p_c2pa.write_bytes(b'\\xFF\\xD8\\xFF\\xEB' + (len(seg) + 2).to_bytes(2, 'big') + seg + b'\\xFF\\xD9')

signed_cbor = bytearray()
signed_cbor += bytes([0xA1])
signed_cbor += bytes([0x68]) + b'manifest'
signed_cbor += bytes([0x81])
signed_cbor += bytes([0xA2])
signed_cbor += bytes([0x6F]) + b'claim_generator'
signed_cbor += bytes([0x64]) + b'test'
signed_cbor += bytes([0x66]) + b'claims'
signed_cbor += bytes([0x81])
signed_cbor += bytes([0xA2])
signed_cbor += bytes([0x6A]) + b'assertions'
signed_cbor += bytes([0x81])
signed_cbor += bytes([0xA1])
signed_cbor += bytes([0x65]) + b'label'
signed_cbor += bytes([0x6E]) + b'c2pa.hash.data'
signed_cbor += bytes([0x6A]) + b'signatures'
signed_cbor += bytes([0x81])
signed_cbor += bytes([0xA2])
signed_cbor += bytes([0x63]) + b'alg'
signed_cbor += bytes([0x65]) + b'ES256'
signed_cbor += bytes([0x69]) + b'signature'
signed_cbor += bytes([0x44, 0x01, 0x02, 0x03, 0x04])
signed_jumb = box(b'jumb', box(b'jumd', jumd) + box(b'cbor', bytes(signed_cbor)))

p_c2pa_heif = Path(r'''${WORK_DIR}/sample_c2pa.heif''')
u32 = lambda v: v.to_bytes(4, 'big')
bmff_box = lambda t, payload: u32(8 + len(payload)) + t + payload
bmff_uuid_box = lambda uuid, payload: u32(24 + len(payload)) + b'uuid' + uuid + payload
bmff_marker = b'openmeta:bmff_transfer_meta:v1'
bmff_uuid = b'OpenMetaBmffMeta'
bmff_infe = (
    b'\\x02\\x00\\x00\\x00'
    + (1).to_bytes(2, 'big')
    + (0).to_bytes(2, 'big')
    + b'c2pa'
    + b'C2PA\\x00'
)
bmff_iinf = bmff_box(
    b'iinf',
    b'\\x00\\x00\\x00\\x00' + (1).to_bytes(2, 'big') + bmff_box(b'infe', bmff_infe),
)
bmff_idat = bmff_box(b'idat', bytes(signed_jumb))
bmff_iloc = bmff_box(
    b'iloc',
    b'\\x01\\x00\\x00\\x00'
    + bytes([0x44, 0x40])
    + (1).to_bytes(2, 'big')
    + (1).to_bytes(2, 'big')
    + (1).to_bytes(2, 'big')
    + (0).to_bytes(2, 'big')
    + (0).to_bytes(4, 'big')
    + (1).to_bytes(2, 'big')
    + (0).to_bytes(4, 'big')
    + len(signed_jumb).to_bytes(4, 'big'),
)
bmff_meta = bmff_box(
    b'meta',
    b'\\x00\\x00\\x00\\x00'
    + bmff_uuid_box(bmff_uuid, bmff_marker)
    + bmff_iinf
    + bmff_idat
    + bmff_iloc,
)
p_c2pa_heif.write_bytes(
    bmff_box(b'ftyp', b'heic' + u32(0) + b'mif1heic')
    + bmff_box(b'mdat', bytes([0x11, 0x22, 0x33, 0x44]))
    + bmff_meta
)

p_target_jxl = Path(r'''${WORK_DIR}/target.jxl''')
p_target_jxl.write_bytes(
    u32(12) + b'JXL ' + u32(0x0D0A870A) + bmff_box(b'jxlc', bytes([0x11, 0x22, 0x33, 0x44]))
)

r4 = openmeta.transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_policy=openmeta.TransferPolicyAction.Invalidate,
)
assert r4['prepare_status'] == openmeta.TransferStatus.Ok, r4
assert any(d['subject_name'] == 'c2pa' and d['effective_name'] == 'keep' and d['reason_name'] == 'draft_invalidation_payload' and d['c2pa_mode_name'] == 'draft_unsigned_invalidation' and d['c2pa_source_kind_name'] == 'content_bound' and d['c2pa_prepared_output_name'] == 'generated_draft_unsigned_invalidation' for d in r4['policy_decisions']), r4
assert any(b['route'] == 'jpeg:app11-c2pa' for b in r4['blocks']), r4
assert r4['c2pa_rewrite']['state_name'] == 'not_requested', r4
assert r4['c2pa_rewrite']['matched_entries'] > 0, r4
assert r4['c2pa_rewrite']['existing_carrier_segments'] == 1, r4
assert r4['c2pa_sign_request']['status_name'] == 'unsupported', r4
assert r4['c2pa_sign_request']['carrier_route'] == 'jpeg:app11-c2pa', r4

r5 = openmeta.transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_policy=openmeta.TransferPolicyAction.Rewrite,
)
assert r5['prepare_status'] == openmeta.TransferStatus.Unsupported, r5
assert any(d['subject_name'] == 'c2pa' and d['reason_name'] == 'signed_rewrite_unavailable' for d in r5['policy_decisions']), r5
assert r5['c2pa_rewrite']['state_name'] == 'signing_material_required', r5
assert r5['c2pa_rewrite']['source_kind_name'] == 'content_bound', r5
assert r5['c2pa_rewrite']['matched_entries'] > 0, r5
assert r5['c2pa_rewrite']['existing_carrier_segments'] == 1, r5
assert r5['c2pa_rewrite']['target_carrier_available'] is True, r5
assert r5['c2pa_rewrite']['requires_private_key'] is True, r5
assert r5['c2pa_rewrite']['content_binding_bytes'] == 4, r5
assert len(r5['c2pa_rewrite']['content_binding_chunks']) == 2, r5
assert all(c['kind_name'] == 'source_range' for c in r5['c2pa_rewrite']['content_binding_chunks']), r5
assert r5['c2pa_sign_request']['status_name'] == 'ok', r5
assert r5['c2pa_sign_request']['carrier_route'] == 'jpeg:app11-c2pa', r5
assert r5['c2pa_sign_request']['manifest_label'] == 'c2pa', r5
assert r5['c2pa_sign_request']['source_range_chunks'] == 2, r5
assert r5['c2pa_sign_request']['prepared_segment_chunks'] == 0, r5
assert r5['c2pa_sign_request']['content_binding_bytes'] == 4, r5

r5b = openmeta.unsafe_transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_policy=openmeta.TransferPolicyAction.Rewrite,
    include_c2pa_binding_bytes=True,
)
assert r5b['c2pa_binding_requested'] is True, r5b
assert r5b['c2pa_binding_status_name'] == 'ok', r5b
assert r5b['c2pa_binding_code_name'] == 'none', r5b
assert r5b['c2pa_binding_bytes_written'] == 4, r5b
assert isinstance(r5b['c2pa_binding_bytes'], (bytes, bytearray)), r5b
assert bytes(r5b['c2pa_binding_bytes']) == bytes([0xFF, 0xD8, 0xFF, 0xD9]), r5b
assert r5b['c2pa_handoff_requested'] is False, r5b

r5c = openmeta.unsafe_transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_policy=openmeta.TransferPolicyAction.Rewrite,
    include_c2pa_handoff_bytes=True,
)
assert r5c['c2pa_handoff_requested'] is True, r5c
assert r5c['c2pa_handoff_status_name'] == 'ok', r5c
assert r5c['c2pa_handoff_code_name'] == 'none', r5c
assert r5c['c2pa_handoff_bytes_written'] > 0, r5c
assert isinstance(r5c['c2pa_handoff_bytes'], (bytes, bytearray)), r5c

r6 = openmeta.unsafe_transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_signed_logical_payload=signed_jumb,
    c2pa_certificate_chain=bytes([0x30, 0x82, 0x01, 0x00]),
    c2pa_private_key_reference='test-key-ref',
    c2pa_signing_time='2026-03-09T00:00:00Z',
    c2pa_manifest_builder_output=bytes(signed_cbor),
    edit_target_path=str(p_c2pa),
    edit_apply=True,
    include_edited_bytes=True,
)
assert r6['prepare_status'] == openmeta.TransferStatus.Unsupported, r6
assert r6['c2pa_stage_requested'] is True, r6
assert r6['c2pa_stage_validation_status_name'] == 'ok', r6
assert r6['c2pa_stage_validation_code_name'] == 'none', r6
assert r6['c2pa_stage_validation_payload_kind_name'] == 'content_bound', r6
assert r6['c2pa_stage_validation_semantic_status_name'] == 'ok', r6
assert r6['c2pa_stage_validation_semantic_reason'] == 'ok', r6
assert r6['c2pa_stage_validation_semantic_manifest_present'] == 1, r6
assert r6['c2pa_stage_validation_semantic_manifest_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_claim_generator_present'] == 1, r6
assert r6['c2pa_stage_validation_semantic_assertion_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_primary_claim_assertion_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_primary_claim_referenced_by_signature_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_primary_signature_linked_claim_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_primary_signature_reference_key_hits'] == 0, r6
assert r6['c2pa_stage_validation_semantic_primary_signature_explicit_reference_present'] == 0, r6
assert r6['c2pa_stage_validation_semantic_primary_signature_explicit_reference_resolved_claim_count'] == 0, r6
assert r6['c2pa_stage_validation_semantic_claim_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_signature_count'] == 1, r6
assert r6['c2pa_stage_validation_semantic_signature_linked'] == 1, r6
assert r6['c2pa_stage_validation_semantic_signature_orphan'] == 0, r6
assert r6['c2pa_stage_validation_staged_segments'] == 1, r6
assert r6['c2pa_stage_status_name'] == 'ok', r6
assert any(d['subject_name'] == 'c2pa' and d['reason_name'] == 'external_signed_payload' and d['c2pa_mode_name'] == 'signed_rewrite' and d['c2pa_prepared_output_name'] == 'signed_rewrite' for d in r6['policy_decisions']), r6
assert r6['c2pa_rewrite']['state_name'] == 'ready', r6
assert r6['c2pa_sign_request']['status_name'] == 'ok', r6
assert r6['overall_status'] == openmeta.TransferStatus.Ok, r6
assert r6['edit_apply_status'] == openmeta.TransferStatus.Ok, r6
assert isinstance(r6['edited_bytes'], (bytes, bytearray)), r6

r6b = openmeta.unsafe_transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_signed_logical_payload=signed_jumb,
    c2pa_certificate_chain=bytes([0x30, 0x82, 0x01, 0x00]),
    c2pa_private_key_reference='test-key-ref',
    c2pa_signing_time='2026-03-09T00:00:00Z',
    c2pa_manifest_builder_output=bytes(signed_cbor),
    include_c2pa_signed_package_bytes=True,
)
assert r6b['c2pa_signed_package_requested'] is True, r6b
assert r6b['c2pa_signed_package_status_name'] == 'ok', r6b
assert r6b['c2pa_signed_package_code_name'] == 'none', r6b
assert r6b['c2pa_signed_package_bytes_written'] > 0, r6b
assert isinstance(r6b['c2pa_signed_package_bytes'], (bytes, bytearray)), r6b

r7 = openmeta.unsafe_transfer_probe(
    str(p_c2pa),
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_signed_package=bytes(r6b['c2pa_signed_package_bytes']),
    edit_target_path=str(p_c2pa),
    edit_apply=True,
    include_edited_bytes=True,
)
assert r7['c2pa_stage_requested'] is True, r7
assert r7['c2pa_stage_validation_status_name'] == 'ok', r7
assert r7['c2pa_stage_validation_semantic_status_name'] == 'ok', r7
assert r7['c2pa_stage_validation_semantic_manifest_count'] == 1, r7
assert r7['c2pa_stage_validation_semantic_claim_generator_present'] == 1, r7
assert r7['c2pa_stage_validation_semantic_assertion_count'] == 1, r7
assert r7['c2pa_stage_validation_semantic_primary_claim_assertion_count'] == 1, r7
assert r7['c2pa_stage_validation_semantic_primary_claim_referenced_by_signature_count'] == 1, r7
assert r7['c2pa_stage_validation_semantic_primary_signature_linked_claim_count'] == 1, r7
assert r7['c2pa_stage_validation_semantic_primary_signature_reference_key_hits'] == 0, r7
assert r7['c2pa_stage_validation_semantic_primary_signature_explicit_reference_present'] == 0, r7
assert r7['c2pa_stage_validation_semantic_primary_signature_explicit_reference_resolved_claim_count'] == 0, r7
assert r7['c2pa_stage_status_name'] == 'ok', r7
assert r7['overall_status'] == openmeta.TransferStatus.Ok, r7
assert isinstance(r7['edited_bytes'], (bytes, bytearray)), r7

r8 = openmeta.unsafe_transfer_probe(
    str(p_c2pa_heif),
    target_format=openmeta.TransferTargetFormat.Heif,
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_policy=openmeta.TransferPolicyAction.Rewrite,
    include_c2pa_binding_bytes=True,
)
assert r8['c2pa_binding_requested'] is True, r8
assert r8['c2pa_binding_status_name'] == 'ok', r8
assert r8['c2pa_binding_code_name'] == 'none', r8
assert r8['c2pa_binding_bytes_written'] == 36, r8
assert bytes(r8['c2pa_binding_bytes']) == bytes.fromhex('000000186674797068656963000000006d696631686569630000000c6d64617411223344'), r8
assert r8['c2pa_sign_request']['carrier_route'] == 'bmff:item-c2pa', r8

r9 = openmeta.unsafe_transfer_probe(
    str(p_c2pa_heif),
    target_format=openmeta.TransferTargetFormat.Heif,
    include_exif_app1=False,
    include_xmp_app1=False,
    include_icc_app2=False,
    include_iptc_app13=False,
    c2pa_signed_logical_payload=signed_jumb,
    c2pa_certificate_chain=bytes([0x30, 0x82, 0x01, 0x00]),
    c2pa_private_key_reference='test-key-ref',
    c2pa_signing_time='2026-03-09T00:00:00Z',
    c2pa_manifest_builder_output=bytes(signed_cbor),
    edit_target_path=str(p_c2pa_heif),
    edit_apply=True,
    include_edited_bytes=True,
)
assert r9['c2pa_stage_requested'] is True, r9
assert r9['c2pa_stage_validation_status_name'] == 'ok', r9
assert r9['c2pa_stage_status_name'] == 'ok', r9
assert r9['c2pa_stage_skipped'] == 0, r9
assert r9['c2pa_rewrite']['target_format_name'] == 'heif', r9
assert r9['c2pa_rewrite']['state_name'] == 'ready', r9
assert r9['edit_apply_status'] == openmeta.TransferStatus.Ok, r9
assert isinstance(r9['edited_bytes'], (bytes, bytearray)), r9

r10 = openmeta.unsafe_transfer_probe(
    str(p),
    target_format=openmeta.TransferTargetFormat.Jxl,
    include_icc_app2=False,
    edit_target_path=str(p_target_jxl),
    edit_apply=True,
    include_edited_bytes=True,
)
assert r10['edit_plan_status_name'] == 'ok', r10
assert r10['edit_apply_status_name'] == 'ok', r10
assert isinstance(r10['edited_bytes'], (bytes, bytearray)), r10
assert bytes(r10['edited_bytes'])[4:8] == b'JXL ', r10
print('openmeta.transfer_probe smoke ok')
")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -c "${_py_code}"
  RESULT_VARIABLE _rv
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)
if(NOT _rv EQUAL 0)
  message(FATAL_ERROR
    "python transfer probe smoke failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

message(STATUS "python transfer probe smoke gate passed")
