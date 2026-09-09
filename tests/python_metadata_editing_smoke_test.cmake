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

set(_py_code
"import openmeta

K = openmeta.MetadataCreationFieldKind
source = openmeta.create_metadata([
    openmeta.metadata_creation_text(K.Title, 'Before'),
    openmeta.metadata_creation_text(K.Creator, 'Alice'),
    openmeta.metadata_creation_text(K.Creator, 'Bob'),
    openmeta.metadata_creation_text(K.Keyword, 'night'),
    openmeta.metadata_creation_i32(K.Rating, 1),
])
edited = source.edit_metadata([
    openmeta.metadata_edit_set(
        openmeta.metadata_creation_text(K.Title, 'After')),
    openmeta.metadata_edit_set(
        openmeta.metadata_creation_text(K.Creator, 'Carol'), 1),
    openmeta.metadata_edit_add(
        openmeta.metadata_creation_text(K.Keyword, 'city')),
    openmeta.metadata_edit_remove(K.Creator, 0),
    openmeta.metadata_edit_set(
        openmeta.metadata_creation_i32(K.Rating, 5)),
])

assert openmeta.METADATA_EDITING_CONTRACT_VERSION == 1
assert source.entry_count == 5
assert edited.entry_count == 6
assert edited.path == ''
assert edited.build_transfer_source_snapshot().entry_count == 6

source_packet, _ = source.dump_xmp_portable(
    include_exif=False,
    include_iptc=False,
    include_existing_xmp=True,
)
edited_packet, _ = edited.dump_xmp_portable(
    include_exif=False,
    include_iptc=False,
    include_existing_xmp=True,
)
assert b'Before' in source_packet
assert b'Alice' in source_packet
assert b'After' in edited_packet
assert b'Carol' in edited_packet
assert b'city' in edited_packet
assert b'<xmp:Rating>5</xmp:Rating>' in edited_packet
assert b'Before' not in edited_packet
assert b'Alice' not in edited_packet

try:
    source.edit_metadata([
        openmeta.metadata_edit_add(
            openmeta.metadata_creation_text(K.Title, 'duplicate')),
    ])
except ValueError as exc:
    assert 'singleton_already_exists at operation 0' in str(exc)
else:
    raise AssertionError('duplicate singleton edit was accepted')

replaced = source.edit_metadata([
    openmeta.metadata_edit_remove_all(K.Title),
    openmeta.metadata_edit_add(
        openmeta.metadata_creation_text(K.Title, 'Replacement')),
])
replaced_packet, _ = replaced.dump_xmp_portable(
    include_exif=False,
    include_iptc=False,
    include_existing_xmp=True,
)
assert b'Replacement' in replaced_packet
assert b'Before' not in replaced_packet

dates = openmeta.create_metadata([
    openmeta.metadata_creation_text(
        K.CreateDate, '2024-08-30T01:02:03-02:30'),
    openmeta.metadata_creation_text(
        K.DateTimeOriginal, '2024-08-28T10:11:12.500Z'),
])
translated_dates = dates.translate_creation_dates(
    date_created_to_iptc_created=False)
assert openmeta.METADATA_DATE_TRANSLATION_CONTRACT_VERSION == 1
assert translated_dates.entry_count == dates.entry_count + 7
assert dates.entry_count == 2
translated_packet, _ = translated_dates.dump_xmp_portable(
    include_existing_xmp=True,
    conflict_policy=openmeta.XmpConflictPolicy.ExistingWins,
)
assert b'2024-08-30T01:02:03-02:30' in translated_packet
assert b'2024-08-28T10:11:12.500Z' in translated_packet

fractional = openmeta.create_metadata([
    openmeta.metadata_creation_text(
        K.CreateDate, '2024-08-30T01:02:03.125Z'),
])
try:
    fractional.translate_creation_dates(
        date_created_to_iptc_created=False)
except ValueError as exc:
    assert 'unsupported_precision for xmp_create_date' in str(exc)
else:
    raise AssertionError('lossy IPTC date translation was accepted')
fractional_exif = fractional.translate_creation_dates(
    create_date_to_iptc_digital_creation=False,
    date_created_to_iptc_created=False,
    date_time_original_to_exif_original=False,
)
assert fractional_exif.entry_count == fractional.entry_count + 3

technical = openmeta.create_metadata([
    openmeta.metadata_creation_text(
        K.ModifyDate, '2026-08-31T12:34:56.125+09:00'),
    openmeta.metadata_creation_text(K.CameraMake, 'OpenMeta Camera'),
    openmeta.metadata_creation_text(K.CameraModel, 'OM-1'),
    openmeta.metadata_creation_text(K.Software, 'OpenMeta Python'),
])
translated_technical = technical.translate_technical_metadata()
assert openmeta.METADATA_TECHNICAL_TRANSLATION_CONTRACT_VERSION == 1
assert translated_technical.entry_count == technical.entry_count + 6
assert technical.entry_count == 4

non_ascii_technical = openmeta.create_metadata([
    openmeta.metadata_creation_text(K.CameraMake, 'M' + chr(0xe4) + 'ke'),
])
try:
    non_ascii_technical.translate_technical_metadata()
except ValueError as exc:
    assert 'non_ascii_source for tiff_make' in str(exc)
else:
    raise AssertionError('non-ASCII EXIF technical translation was accepted')

capture = openmeta.create_metadata([
    openmeta.metadata_creation_urational(K.ExposureTime, 1, 125),
    openmeta.metadata_creation_urational(K.FNumber, 28, 10),
    openmeta.metadata_creation_u32(K.IsoSensitivity, 400),
    openmeta.metadata_creation_urational(K.FocalLength, 50, 1),
])
translated_capture = capture.translate_capture_metadata()
assert openmeta.METADATA_CAPTURE_TRANSLATION_CONTRACT_VERSION == 1
assert translated_capture.entry_count == capture.entry_count + 4
assert capture.entry_count == 4

oversized_iso = openmeta.create_metadata([
    openmeta.metadata_creation_u32(K.IsoSensitivity, 70000),
])
try:
    oversized_iso.translate_capture_metadata()
except ValueError as exc:
    assert 'value_out_of_range for xmp_iso' in str(exc)
else:
    raise AssertionError('out-of-range native EXIF ISO was accepted')

geometry = openmeta.create_metadata([
    openmeta.metadata_creation_u32(K.Orientation, 6),
    openmeta.metadata_creation_u32(K.PixelWidth, 640),
    openmeta.metadata_creation_u32(K.PixelHeight, 480),
])
target = openmeta.TransferTargetImageSpec()
target.has_dimensions = True
target.width = 640
target.height = 480
target.has_orientation = True
target.orientation = 6
translated_geometry = geometry.translate_image_geometry(target)
assert openmeta.METADATA_GEOMETRY_TRANSLATION_CONTRACT_VERSION == 1
assert translated_geometry.entry_count == geometry.entry_count + 5
assert geometry.entry_count == 3

mismatch = openmeta.TransferTargetImageSpec()
mismatch.has_dimensions = True
mismatch.width = 480
mismatch.height = 640
mismatch.has_orientation = True
mismatch.orientation = 6
try:
    geometry.translate_image_geometry(mismatch)
except ValueError as exc:
    assert 'target_image_spec_mismatch for xmp_dimensions' in str(exc)
else:
    raise AssertionError('mismatched target image geometry was accepted')

descriptive = openmeta.create_metadata([
    openmeta.metadata_creation_text(K.Title, 'Night ' + chr(0x666f)),
    openmeta.metadata_creation_text(K.Creator, 'Alice'),
    openmeta.metadata_creation_text(K.Creator, 'Bob'),
    openmeta.metadata_creation_text(K.Keyword, 'night'),
])
translated_descriptive = descriptive.translate_descriptive_metadata()
assert openmeta.METADATA_DESCRIPTIVE_TRANSLATION_CONTRACT_VERSION == 1
assert translated_descriptive.entry_count == descriptive.entry_count + 5
assert descriptive.entry_count == 4

oversized = openmeta.create_metadata([
    openmeta.metadata_creation_text(K.Title, 'x' * 65),
])
try:
    oversized.translate_descriptive_metadata()
except ValueError as exc:
    assert 'value_too_long for dc_title' in str(exc)
else:
    raise AssertionError('oversized IPTC title translation was accepted')

import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory() as temporary:
    location_path = Path(temporary) / 'location.jpg'
    xml = b'''<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">
<rdf:Description xmlns:p=\"http://ns.adobe.com/photoshop/1.0/\"
xmlns:i=\"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/\">
<p:City>Kyoto</p:City><i:Location>Garden</i:Location><p:State>Kyoto</p:State>
<p:Country>Japan</p:Country><i:CountryCode>JP</i:CountryCode>
</rdf:Description></rdf:RDF>'''
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    location_path.write_bytes(bytes.fromhex('ffd8ffe1') +
        (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    location = openmeta.read(str(location_path))
    clean = location.translate_location_metadata()
    assert clean.entry_count == location.entry_count
    translated_location = location.translate_location_metadata(
        source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All)
    assert openmeta.METADATA_LOCATION_TRANSLATION_CONTRACT_VERSION == 1
    assert translated_location.entry_count == location.entry_count + 5
    native_packet, _ = translated_location.dump_xmp_portable(
        include_existing_xmp=False, include_exif=False, include_iptc=True)
    assert b'Kyoto' in native_packet and b'Garden' in native_packet
    assert b'Japan' in native_packet and b'>JP<' in native_packet
    try:
        location.translate_location_metadata(
            source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
            max_added_entries=4)
    except ValueError as exc:
        assert 'entry_limit_exceeded' in str(exc)
    else:
        raise AssertionError('location entry limit was ignored')

with tempfile.TemporaryDirectory() as temporary:
    editorial_path = Path(temporary) / 'editorial.jpg'
    xml = b'''<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">
<rdf:Description xmlns:p=\"http://ns.adobe.com/photoshop/1.0/\">
<p:Headline>Garden opens</p:Headline><p:Instructions>Contact the editor</p:Instructions>
<p:TransmissionReference>JOB-42</p:TransmissionReference>
</rdf:Description></rdf:RDF>'''
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    editorial_path.write_bytes(bytes.fromhex('ffd8ffe1') +
        (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    editorial = openmeta.read(str(editorial_path))
    original_count = editorial.entry_count
    assert editorial.translate_editorial_metadata().entry_count == original_count
    translated_editorial = editorial.translate_editorial_metadata(
        source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All)
    assert openmeta.METADATA_EDITORIAL_TRANSLATION_CONTRACT_VERSION == 1
    assert openmeta.METADATA_EDITORIAL_TRANSLATION_MAX_ADDED_ENTRIES == 4
    assert translated_editorial.entry_count == original_count + 3
    native_packet, _ = translated_editorial.dump_xmp_portable(
        include_existing_xmp=False, include_exif=False, include_iptc=True)
    assert b'Garden opens' in native_packet and b'Contact the editor' in native_packet
    assert b'JOB-42' in native_packet
    assert editorial.entry_count == original_count
    for selected in range(3):
        one = editorial.translate_editorial_metadata(
            source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
            headline_to_iptc=selected == 0, instructions_to_iptc=selected == 1,
            transmission_reference_to_iptc=selected == 2)
        assert one.entry_count == original_count + 1
        packet, _ = one.dump_xmp_portable(
            include_existing_xmp=False, include_exif=False, include_iptc=True)
        for index, value in enumerate((b'Garden opens', b'Contact the editor', b'JOB-42')):
            assert (value in packet) == (index == selected)
    try:
        editorial.translate_editorial_metadata(
            source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
            max_added_entries=2)
    except ValueError as exc:
        assert 'entry_limit_exceeded' in str(exc)
    else:
        raise AssertionError('editorial entry limit was ignored')
    assert editorial.entry_count == original_count
    xml = xml.replace(b'JOB-42', b'R' * 33)
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    editorial_path.write_bytes(bytes.fromhex('ffd8ffe1') +
        (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    oversized_editorial = openmeta.read(str(editorial_path))
    try:
        oversized_editorial.translate_editorial_metadata(
            source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All)
    except ValueError as exc:
        assert 'value_too_long for photoshop_transmission_reference' in str(exc)
    else:
        raise AssertionError('oversized editorial identifier was accepted')

print('openmeta metadata editing smoke ok')
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
    "python metadata editing smoke failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

message(STATUS "python metadata editing smoke gate passed")
