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
