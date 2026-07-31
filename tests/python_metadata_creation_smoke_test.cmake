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
fields = [
    openmeta.metadata_creation_text(K.Title, 'Evening frame'),
    openmeta.metadata_creation_text(K.Creator, 'Alice'),
    openmeta.metadata_creation_text(K.Creator, 'Bob'),
    openmeta.metadata_creation_text(K.Keyword, 'night'),
    openmeta.metadata_creation_i32(K.Rating, 5),
    openmeta.metadata_creation_u32(K.Orientation, 6),
    openmeta.metadata_creation_urational(K.ExposureTime, 1, 125),
]
doc = openmeta.create_metadata(fields)
assert openmeta.METADATA_CREATION_CONTRACT_VERSION == 1
assert doc.path == ''
assert doc.entry_count == len(fields)
assert doc.block_count == 1
assert doc.xmp_entries_decoded == len(fields)

packet, result = doc.dump_xmp_portable(
    include_exif=False,
    include_iptc=False,
    include_existing_xmp=True,
)
assert result.status == openmeta.XmpDumpStatus.Ok
assert b'<dc:title>' in packet
assert b'<rdf:li>Alice</rdf:li>' in packet
assert b'<xmp:Rating>5</xmp:Rating>' in packet
assert b'<tiff:Orientation>6</tiff:Orientation>' in packet
assert b'<exif:ExposureTime>1/125</exif:ExposureTime>' in packet

query = doc.metadata_query(openmeta.MetadataQueryKind.Descriptive)
assert len(query['matches']) >= 4
snapshot = doc.build_transfer_source_snapshot()
assert snapshot.entry_count == len(fields)

try:
    openmeta.create_metadata([
        openmeta.metadata_creation_text(K.Title, 'one'),
        openmeta.metadata_creation_text(K.Title, 'two'),
    ])
except ValueError as exc:
    assert 'duplicate_singleton at field 1' in str(exc)
else:
    raise AssertionError('duplicate singleton was accepted')

print('openmeta metadata creation smoke ok')
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
    "python metadata creation smoke failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

message(STATUS "python metadata creation smoke gate passed")
