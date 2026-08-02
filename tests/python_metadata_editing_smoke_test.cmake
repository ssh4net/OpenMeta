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
