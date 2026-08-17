cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/test_python_interpreter.cmake")

if(NOT DEFINED METATRANSFER_BIN OR METATRANSFER_BIN STREQUAL "")
  message(FATAL_ERROR "METATRANSFER_BIN is required")
endif()
if(NOT EXISTS "${METATRANSFER_BIN}")
  message(FATAL_ERROR "metatransfer binary not found: ${METATRANSFER_BIN}")
endif()
if(NOT DEFINED OIIOTOOL_BIN OR OIIOTOOL_BIN STREQUAL "")
  message(FATAL_ERROR "OIIOTOOL_BIN is required")
endif()
if(NOT EXISTS "${OIIOTOOL_BIN}")
  message(FATAL_ERROR "oiiotool binary not found: ${OIIOTOOL_BIN}")
endif()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_metatransfer_image_usability")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_source_jpg "${WORK_DIR}/source_meta.jpg")
set(_source_icc_jpg "${WORK_DIR}/source_icc.jpg")
set(_source_xmp_jpg "${WORK_DIR}/source_xmp.jpg")
set(_source_makernote_jpg "${WORK_DIR}/source_makernote.jpg")
set(_makernote_hex "4f70656e4d6574614d616b65724e6f746500")
set(_makernote_xmp_title "OpenMeta MakerNote XMP Gate")

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; t=bytearray(); t+=b'II*\\x00'; t+=(8).to_bytes(4,'little'); t+=(1).to_bytes(2,'little'); t+=(0x0132).to_bytes(2,'little'); t+=(2).to_bytes(2,'little'); t+=(20).to_bytes(4,'little'); t+=(26).to_bytes(4,'little'); t+=(0).to_bytes(4,'little'); t+=b'2000:01:02 03:04:05\\x00'; app1=b'Exif\\x00\\x00'+bytes(t); ln=(len(app1)+2).to_bytes(2,'big'); Path(r'''${_source_jpg}''').write_bytes(b'\\xff\\xd8\\xff\\xe1'+ln+app1+b'\\xff\\xd9')"
  RESULT_VARIABLE _rv_source
  OUTPUT_VARIABLE _out_source
  ERROR_VARIABLE _err_source
)
if(NOT _rv_source EQUAL 0)
  message(FATAL_ERROR
    "failed to write image usability source fixture (${_rv_source})\nstdout:\n${_out_source}\nstderr:\n${_err_source}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; p=bytearray(156); p[0:4]=(156).to_bytes(4,'big'); p[36:40]=b'acsp'; p[128:132]=(1).to_bytes(4,'big'); p[132:136]=b'desc'; p[136:140]=(144).to_bytes(4,'big'); p[140:144]=(12).to_bytes(4,'big'); p[144:156]=bytes([0x11])*12; app2=b'ICC_PROFILE\\x00\\x01\\x01'+bytes(p); ln=(len(app2)+2).to_bytes(2,'big'); Path(r'''${_source_icc_jpg}''').write_bytes(b'\\xff\\xd8\\xff\\xe2'+ln+app2+b'\\xff\\xd9')"
  RESULT_VARIABLE _rv_source_icc
  OUTPUT_VARIABLE _out_source_icc
  ERROR_VARIABLE _err_source_icc
)
if(NOT _rv_source_icc EQUAL 0)
  message(FATAL_ERROR
    "failed to write image usability ICC source fixture (${_rv_source_icc})\nstdout:\n${_out_source_icc}\nstderr:\n${_err_source_icc}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    [=[
from pathlib import Path
import sys
p = (
    b'<x:xmpmeta xmlns:x="adobe:ns:meta/">'
    b'<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">'
    b'<rdf:Description xmlns:dc="http://purl.org/dc/elements/1.1/">'
    b'<dc:title><rdf:Alt>'
    b'<rdf:li xml:lang="x-default">OpenMeta XMP Gate</rdf:li>'
    b'</rdf:Alt></dc:title>'
    b'</rdf:Description></rdf:RDF></x:xmpmeta>'
)
app1 = b"http://ns.adobe.com/xap/1.0/\x00" + p
Path(sys.argv[1]).write_bytes(
    b"\xff\xd8\xff\xe1" + (len(app1) + 2).to_bytes(2, "big") + app1
    + b"\xff\xd9"
)
]=]
    "${_source_xmp_jpg}"
  RESULT_VARIABLE _rv_source_xmp
  OUTPUT_VARIABLE _out_source_xmp
  ERROR_VARIABLE _err_source_xmp
)
if(NOT _rv_source_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write image usability XMP source fixture (${_rv_source_xmp})\nstdout:\n${_out_source_xmp}\nstderr:\n${_err_source_xmp}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    [=[
from pathlib import Path
import sys


def u16(v):
    return int(v).to_bytes(2, "little")


def u32(v):
    return int(v).to_bytes(4, "little")


def entry(tag, typ, count, value):
    return u16(tag) + u16(typ) + u32(count) + value


maker_note = b"OpenMetaMakerNote\x00"
xmp = (
    b'<x:xmpmeta xmlns:x="adobe:ns:meta/">'
    b'<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">'
    b'<rdf:Description xmlns:dc="http://purl.org/dc/elements/1.1/">'
    b'<dc:title><rdf:Alt>'
    b'<rdf:li xml:lang="x-default">OpenMeta MakerNote XMP Gate</rdf:li>'
    b'</rdf:Alt></dc:title>'
    b'</rdf:Description></rdf:RDF></x:xmpmeta>'
)
ifd0_count = 5
exif_count = 7
ifd0_off = 8
ifd0_size = 2 + ifd0_count * 12 + 4
exif_off = ifd0_off + ifd0_size
exif_size = 2 + exif_count * 12 + 4
data_off = exif_off + exif_size
xres_off = data_off
yres_off = xres_off + 8
maker_off = yres_off + 8

tiff = bytearray()
tiff += b"II*\x00" + u32(ifd0_off)
tiff += u16(ifd0_count)
tiff += entry(0x011A, 5, 1, u32(xres_off))
tiff += entry(0x011B, 5, 1, u32(yres_off))
tiff += entry(0x0128, 3, 1, u16(2) + b"\x00\x00")
tiff += entry(0x0213, 3, 1, u16(1) + b"\x00\x00")
tiff += entry(0x8769, 4, 1, u32(exif_off))
tiff += u32(0)
tiff += u16(exif_count)
tiff += entry(0x9000, 7, 4, b"0232")
tiff += entry(0x9101, 7, 4, b"\x01\x02\x03\x00")
tiff += entry(0x927C, 7, len(maker_note), u32(maker_off))
tiff += entry(0xA000, 7, 4, b"0100")
tiff += entry(0xA001, 3, 1, u16(1) + b"\x00\x00")
tiff += entry(0xA002, 4, 1, u32(64))
tiff += entry(0xA003, 4, 1, u32(32))
tiff += u32(0)
tiff += u32(72) + u32(1)
tiff += u32(72) + u32(1)
tiff += maker_note

app1 = b"Exif\x00\x00" + bytes(tiff)
xmp_app1 = b"http://ns.adobe.com/xap/1.0/\x00" + xmp
Path(sys.argv[1]).write_bytes(
    b"\xff\xd8\xff\xe1" + (len(app1) + 2).to_bytes(2, "big") + app1
    + b"\xff\xe1" + (len(xmp_app1) + 2).to_bytes(2, "big") + xmp_app1
    + b"\xff\xd9"
)
]=]
    "${_source_makernote_jpg}"
  RESULT_VARIABLE _rv_source_makernote
  OUTPUT_VARIABLE _out_source_makernote
  ERROR_VARIABLE _err_source_makernote
)
if(NOT _rv_source_makernote EQUAL 0)
  message(FATAL_ERROR
    "failed to write image usability MakerNote source fixture (${_rv_source_makernote})\nstdout:\n${_out_source_makernote}\nstderr:\n${_err_source_makernote}")
endif()

function(_om_run label)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "${label} failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
endfunction()

function(_om_create_target format extension)
  set(_path "${WORK_DIR}/target.${extension}")
  if("${format}" STREQUAL "dng")
    _om_run("oiiotool create ${format}"
      "${OIIOTOOL_BIN}" --pattern checker 64x32 3 -d uint8
      -o:fileformatname=tiff "${_path}")
  else()
    _om_run("oiiotool create ${format}"
      "${OIIOTOOL_BIN}" --pattern checker 64x32 3 -d uint8 -o "${_path}")
  endif()
  if("${format}" STREQUAL "webp")
    _om_run("webp vp8x wrapper ${format}"
      "${_openmeta_test_python}" -c
        [=[
from pathlib import Path
import sys
p = Path(sys.argv[1])
b = bytearray(p.read_bytes())
if b[:4] != b"RIFF" or b[8:12] != b"WEBP":
    raise SystemExit("not webp")
vp8x = (
    b"VP8X"
    + (10).to_bytes(4, "little")
    + bytes([0, 0, 0, 0])
    + (63).to_bytes(3, "little")
    + (31).to_bytes(3, "little")
)
out = b if b[12:16] == b"VP8X" else b[:12] + vp8x + b[12:]
out[4:8] = (len(out) - 8).to_bytes(4, "little")
p.write_bytes(out)
]=]
        "${_path}")
  endif()
  set("TARGET_${format}" "${_path}" PARENT_SCOPE)
endfunction()

function(_om_check_oiio_file format path width height samples finite_count)
  execute_process(
    COMMAND "${OIIOTOOL_BIN}" --info --stats "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "oiiotool could not read edited ${format} (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "${width} x[ ]+${height}, ${samples} channel")
    message(FATAL_ERROR
      "oiiotool reported unexpected geometry for edited ${format}\n${_out}")
  endif()
  set(_finite_pattern "FiniteCount:")
  set(_finite_index 0)
  while(_finite_index LESS ${samples})
    set(_finite_pattern "${_finite_pattern} ${finite_count}")
    math(EXPR _finite_index "${_finite_index} + 1")
  endwhile()
  if(NOT _out MATCHES "${_finite_pattern}")
    message(FATAL_ERROR
      "oiiotool stats did not cover all pixels for edited ${format}\n${_out}")
  endif()
endfunction()

function(_om_check_oiio_readable_file format path)
  execute_process(
    COMMAND "${OIIOTOOL_BIN}" --info --stats "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "oiiotool could not read edited ${format} (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "FiniteCount:")
    message(FATAL_ERROR
      "oiiotool did not report pixel stats for edited ${format}\n${_out}")
  endif()
endfunction()

function(_om_check_decodable_file format path)
  execute_process(
    COMMAND "${OIIOTOOL_BIN}" --info --stats "${path}"
    RESULT_VARIABLE _rv_oiio
    OUTPUT_VARIABLE _out_oiio
    ERROR_VARIABLE _err_oiio
  )
  if(_rv_oiio EQUAL 0 AND _out_oiio MATCHES "FiniteCount:")
    return()
  endif()

  if(DEFINED FFMPEG_BIN AND NOT FFMPEG_BIN STREQUAL ""
     AND EXISTS "${FFMPEG_BIN}")
    execute_process(
      COMMAND "${FFMPEG_BIN}" -v error -i "${path}" -f null -
      RESULT_VARIABLE _rv_ffmpeg
      OUTPUT_VARIABLE _out_ffmpeg
      ERROR_VARIABLE _err_ffmpeg
    )
    if(_rv_ffmpeg EQUAL 0)
      return()
    endif()
    message(FATAL_ERROR
      "neither oiiotool nor ffmpeg could decode edited ${format}\n"
      "oiiotool stdout:\n${_out_oiio}\n"
      "oiiotool stderr:\n${_err_oiio}\n"
      "ffmpeg stdout:\n${_out_ffmpeg}\n"
      "ffmpeg stderr:\n${_err_ffmpeg}")
  endif()

  message(FATAL_ERROR
    "oiiotool could not read edited ${format}; provide FFMPEG_BIN for "
    "formats not supported by this oiiotool build\nstdout:\n${_out_oiio}\n"
    "stderr:\n${_err_oiio}")
endfunction()

function(_om_set_default_image_spec prefix)
  set("${prefix}_WIDTH" 64 PARENT_SCOPE)
  set("${prefix}_HEIGHT" 32 PARENT_SCOPE)
  set("${prefix}_SAMPLES" 3 PARENT_SCOPE)
  set("${prefix}_BITS" 8 PARENT_SCOPE)
  set("${prefix}_SAMPLE_FORMAT" 1 PARENT_SCOPE)
  set("${prefix}_PHOTOMETRIC" 2 PARENT_SCOPE)
  set("${prefix}_PLANAR" 1 PARENT_SCOPE)
  set("${prefix}_EXIF_COLOR_SPACE" 1 PARENT_SCOPE)
  set("${prefix}_FINITE_COUNT" 2048 PARENT_SCOPE)
  set("${prefix}_STRICT_OIIO" TRUE PARENT_SCOPE)
endfunction()

function(_om_probe_target_image_spec format path prefix out_var)
  set(_exiftool_arg "")
  if(DEFINED EXIFTOOL_BIN AND NOT EXIFTOOL_BIN STREQUAL ""
     AND EXISTS "${EXIFTOOL_BIN}")
    set(_exiftool_arg "${EXIFTOOL_BIN}")
  endif()
  execute_process(
    COMMAND "${_openmeta_test_python}" -c
      [=[
import re
import subprocess
import sys

exiftool = sys.argv[1]
oiiotool = sys.argv[2]
path = sys.argv[3]


def first_int(value):
    if value is None:
        return None
    match = re.search(r"-?\d+", value)
    if match is None:
        return None
    return int(match.group(0))


tags = {}
if exiftool:
    proc = subprocess.run(
        [
            exiftool,
            "-s",
            "-n",
            "-ImageWidth",
            "-ImageHeight",
            "-ExifImageWidth",
            "-ExifImageHeight",
            "-BitsPerSample",
            "-SamplesPerPixel",
            path,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if proc.returncode == 0:
        for line in proc.stdout.splitlines():
            match = re.match(r"([^:]+?)\s*:\s*(.*)", line)
            if match is not None:
                tags[match.group(1).strip()] = match.group(2).strip()

oiio_width = None
oiio_height = None
oiio_samples = None
oiio_type = ""
proc = subprocess.run(
    [oiiotool, "--info", "--stats", path],
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
)
if proc.returncode == 0:
    match = re.search(
        r":\s*(\d+)\s+x\s+(\d+),\s+(\d+)\s+channel,\s+([A-Za-z0-9_]+)",
        proc.stdout,
    )
    if match is not None:
        oiio_width = int(match.group(1))
        oiio_height = int(match.group(2))
        oiio_samples = int(match.group(3))
        oiio_type = match.group(4)

width = first_int(tags.get("ExifImageWidth"))
height = first_int(tags.get("ExifImageHeight"))
if width is None:
    width = first_int(tags.get("ImageWidth"))
if height is None:
    height = first_int(tags.get("ImageHeight"))
if width is None:
    width = oiio_width
if height is None:
    height = oiio_height
if width is None or height is None or width <= 0 or height <= 0:
    sys.stderr.write("could not infer target metadata dimensions\n")
    sys.exit(1)

samples = first_int(tags.get("SamplesPerPixel"))
if samples is None:
    samples = oiio_samples
if samples is None or samples <= 0:
    samples = 3

bits = first_int(tags.get("BitsPerSample"))
if bits is None:
    bits_by_type = {
        "uint8": 8,
        "int8": 8,
        "uint16": 16,
        "int16": 16,
        "half": 16,
        "uint32": 32,
        "int32": 32,
        "float": 32,
        "double": 64,
    }
    bits = bits_by_type.get(oiio_type, 8)

sample_format = 1
if oiio_type in ("half", "float", "double"):
    sample_format = 3
elif oiio_type.startswith("int"):
    sample_format = 2

photometric = 1 if samples == 1 else 2

print(f"WIDTH={width}")
print(f"HEIGHT={height}")
print(f"SAMPLES={samples}")
print(f"BITS={bits}")
print(f"SAMPLE_FORMAT={sample_format}")
print(f"PHOTOMETRIC={photometric}")
print("PLANAR=1")
print("EXIF_COLOR_SPACE=1")
print("FINITE_COUNT=")
print("STRICT_OIIO=FALSE")
]=]
      "${_exiftool_arg}" "${OIIOTOOL_BIN}" "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(STATUS
      "skipping ${format} image-property transfer; could not infer target "
      "image specs\nstdout:\n${_out}\nstderr:\n${_err}")
    set("${out_var}" FALSE PARENT_SCOPE)
    return()
  endif()
  string(REPLACE "\n" ";" _spec_lines "${_out}")
  foreach(_line IN LISTS _spec_lines)
    if(_line MATCHES "^([A-Z_]+)=(.*)$")
      set("${prefix}_${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}" PARENT_SCOPE)
    endif()
  endforeach()
  set("${out_var}" TRUE PARENT_SCOPE)
endfunction()

function(_om_check_exiftool_file format path width height samples)
  if(NOT DEFINED EXIFTOOL_BIN OR EXIFTOOL_BIN STREQUAL ""
     OR NOT EXISTS "${EXIFTOOL_BIN}")
    return()
  endif()
  execute_process(
    COMMAND "${EXIFTOOL_BIN}" -validate -warning -error -ImageWidth
            -ImageHeight -ExifImageWidth -ExifImageHeight -BitsPerSample
            -SamplesPerPixel -PhotometricInterpretation -PlanarConfiguration
            -Orientation -ColorSpace "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "exiftool could not read edited ${format} (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(_out MATCHES "Error[ ]*:")
    message(FATAL_ERROR "exiftool reported an error for edited ${format}\n${_out}")
  endif()
  if(_out MATCHES "Improper EXIF header")
    message(FATAL_ERROR
      "exiftool reported an improper EXIF header for edited ${format}\n${_out}")
  endif()
  if(NOT _out MATCHES "Image Width[ ]*: ${width}")
    message(FATAL_ERROR
      "exiftool missing ImageWidth=${width} for ${format}\n${_out}")
  endif()
  if(NOT _out MATCHES "Image Height[ ]*: ${height}")
    message(FATAL_ERROR
      "exiftool missing ImageHeight=${height} for ${format}\n${_out}")
  endif()
  if(NOT _out MATCHES "Exif Image Width[ ]*: ${width}")
    message(FATAL_ERROR
      "exiftool missing ExifImageWidth=${width} for ${format}\n${_out}")
  endif()
  if(NOT _out MATCHES "Exif Image Height[ ]*: ${height}")
    message(FATAL_ERROR
      "exiftool missing ExifImageHeight=${height} for ${format}\n${_out}")
  endif()
  if(NOT "${samples}" STREQUAL ""
     AND NOT _out MATCHES "Samples Per Pixel[ ]*: ${samples}")
    message(FATAL_ERROR
      "exiftool missing SamplesPerPixel=${samples} for ${format}\n${_out}")
  endif()
endfunction()

function(_om_check_exiftool_metadata_clean label path)
  if(NOT DEFINED EXIFTOOL_BIN OR EXIFTOOL_BIN STREQUAL ""
     OR NOT EXISTS "${EXIFTOOL_BIN}")
    return()
  endif()
  execute_process(
    COMMAND "${EXIFTOOL_BIN}" -validate -warning -error "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "exiftool could not read ${label} (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(_out MATCHES "Error[ ]*:")
    message(FATAL_ERROR "exiftool reported an error for ${label}\n${_out}")
  endif()
  if(_out MATCHES "Improper EXIF header")
    message(FATAL_ERROR
      "exiftool reported an improper EXIF header for ${label}\n${_out}")
  endif()
  if(_out MATCHES "Bad offset" OR _out MATCHES "Invalid offset")
    message(FATAL_ERROR
      "exiftool reported a corrupted EXIF offset for ${label}\n${_out}")
  endif()
endfunction()

function(_om_check_exiftool_makernote_exact label path expected_hex)
  if(NOT DEFINED EXIFTOOL_BIN OR EXIFTOOL_BIN STREQUAL ""
     OR NOT EXISTS "${EXIFTOOL_BIN}")
    return()
  endif()
  execute_process(
    COMMAND "${_openmeta_test_python}" -c
      [=[
import subprocess
import sys

exiftool = sys.argv[1]
path = sys.argv[2]
expected = bytes.fromhex(sys.argv[3])
proc = subprocess.run(
    [exiftool, "-U", "-b", "-MakerNotes", path],
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
if proc.returncode != 0:
    sys.stderr.write(proc.stderr.decode("utf-8", "replace"))
    sys.exit(proc.returncode)
if proc.stdout != expected:
    sys.stderr.write(
        f"MakerNote bytes mismatch: got={proc.stdout.hex()} "
        f"expected={expected.hex()}\n"
    )
    sys.exit(1)
]=]
      "${EXIFTOOL_BIN}" "${path}" "${expected_hex}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "exiftool did not find byte-exact MakerNote for ${label} (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
endfunction()

function(_om_exiftool_makernote_hex label path out_var)
  if(NOT DEFINED EXIFTOOL_BIN OR EXIFTOOL_BIN STREQUAL ""
     OR NOT EXISTS "${EXIFTOOL_BIN}")
    set("${out_var}" "" PARENT_SCOPE)
    return()
  endif()
  execute_process(
    COMMAND "${_openmeta_test_python}" -c
      [=[
import subprocess
import sys

exiftool = sys.argv[1]
path = sys.argv[2]
proc = subprocess.run(
    [exiftool, "-U", "-b", "-MakerNotes", path],
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
if proc.returncode != 0:
    sys.stderr.write(proc.stderr.decode("utf-8", "replace"))
    sys.exit(proc.returncode)
sys.stdout.write(proc.stdout.hex())
]=]
      "${EXIFTOOL_BIN}" "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "exiftool could not read MakerNotes for ${label} (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  string(STRIP "${_out}" _hex)
  set("${out_var}" "${_hex}" PARENT_SCOPE)
endfunction()

function(_om_check_exiftool_xmp_title label path expected_title)
  if(NOT DEFINED EXIFTOOL_BIN OR EXIFTOOL_BIN STREQUAL ""
     OR NOT EXISTS "${EXIFTOOL_BIN}")
    return()
  endif()
  execute_process(
    COMMAND "${EXIFTOOL_BIN}" -validate -warning -error -XMP:Title "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "exiftool could not read XMP title for ${label} (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(_out MATCHES "Error[ ]*:")
    message(FATAL_ERROR "exiftool reported an XMP error for ${label}\n${_out}")
  endif()
  if(NOT _out MATCHES "Title[ ]*: ${expected_title}")
    message(FATAL_ERROR
      "exiftool did not find XMP title for ${label}\n${_out}")
  endif()
endfunction()

function(_om_check_bmff_exif_reader_layout format path width height)
  execute_process(
    COMMAND "${METATRANSFER_BIN}" --no-build-info
            "--target-${format}"
            --no-xmp
            --no-icc
            --no-iptc
            "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "metatransfer could not summarize edited ${format} EXIF (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "bmff_item Exif count=1")
    message(FATAL_ERROR
      "metatransfer summary missing single BMFF Exif item for ${format}\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()

  if(DEFINED EXIFTOOL_BIN AND NOT EXIFTOOL_BIN STREQUAL ""
     AND EXISTS "${EXIFTOOL_BIN}")
    execute_process(
      COMMAND "${EXIFTOOL_BIN}" -validate -warning -error
              -ExifImageWidth -ExifImageHeight -EXIF:all "${path}"
      RESULT_VARIABLE _rv_exiftool
      OUTPUT_VARIABLE _out_exiftool
      ERROR_VARIABLE _err_exiftool
    )
    if(NOT _rv_exiftool EQUAL 0)
      message(FATAL_ERROR
        "exiftool could not read edited ${format} EXIF (${_rv_exiftool})\nstdout:\n${_out_exiftool}\nstderr:\n${_err_exiftool}")
    endif()
    if(_out_exiftool MATCHES "Error[ ]*:")
      message(FATAL_ERROR
        "exiftool reported an EXIF error for edited ${format}\n${_out_exiftool}")
    endif()
    if(_out_exiftool MATCHES "Can't currently extract Exif")
      message(FATAL_ERROR
        "exiftool could not extract BMFF Exif item layout for ${format}\n${_out_exiftool}")
    endif()
    if(NOT _out_exiftool MATCHES "Exif Image Width[ ]*: ${width}")
      message(FATAL_ERROR
        "exiftool missing BMFF ExifImageWidth=${width} for ${format}\n${_out_exiftool}")
    endif()
    if(NOT _out_exiftool MATCHES "Exif Image Height[ ]*: ${height}")
      message(FATAL_ERROR
        "exiftool missing BMFF ExifImageHeight=${height} for ${format}\n${_out_exiftool}")
    endif()
  endif()

endfunction()

function(_om_transfer_and_check format extension width height samples bits
         sample_format photometric planar color_space finite_count strict_oiio)
  set(_target "${TARGET_${format}}")
  set(_output "${WORK_DIR}/edited.${extension}")
  set(_common
    --no-build-info
    --source-meta "${_source_jpg}"
    --target-width "${width}"
    --target-height "${height}"
    --target-orientation 1
    --target-samples-per-pixel "${samples}"
    --target-bits-per-sample "${bits}"
    --target-sample-format "${sample_format}"
    --target-photometric "${photometric}"
    --target-planar-configuration "${planar}"
    --target-exif-color-space "${color_space}"
    --output "${_output}"
    --force)

  if("${format}" STREQUAL "jpg")
    _om_run("metatransfer image usability ${format}"
      "${METATRANSFER_BIN}" ${_common} --target-jpeg "${_target}")
  elseif("${format}" STREQUAL "tif")
    _om_run("metatransfer image usability ${format}"
      "${METATRANSFER_BIN}" ${_common} --target-tiff "${_target}")
  elseif("${format}" STREQUAL "dng")
    _om_run("metatransfer image usability ${format}"
      "${METATRANSFER_BIN}" ${_common} --target-dng "${_target}")
  else()
    _om_run("metatransfer image usability ${format}"
      "${METATRANSFER_BIN}" ${_common} "--target-${format}" "${_target}")
  endif()

  if(NOT EXISTS "${_output}")
    message(FATAL_ERROR "metatransfer did not write edited ${format}: ${_output}")
  endif()
  if("${strict_oiio}")
    _om_check_oiio_file("${format}" "${_output}" "${width}" "${height}"
      "${samples}" "${finite_count}")
  else()
    _om_check_decodable_file("${format}" "${_output}")
  endif()
  _om_check_exiftool_file(
    "${format}" "${_output}" "${width}" "${height}" "${samples}")
endfunction()

function(_om_transfer_default_and_check format extension)
  _om_set_default_image_spec(_default)
  _om_transfer_and_check("${format}" "${extension}"
    "${_default_WIDTH}" "${_default_HEIGHT}" "${_default_SAMPLES}"
    "${_default_BITS}" "${_default_SAMPLE_FORMAT}"
    "${_default_PHOTOMETRIC}" "${_default_PLANAR}"
    "${_default_EXIF_COLOR_SPACE}" "${_default_FINITE_COUNT}"
    "${_default_STRICT_OIIO}")
endfunction()

function(_om_transfer_makernote_and_check format extension safety expect_present
         width height samples bits sample_format photometric planar color_space)
  set(_target "${TARGET_${format}}")
  if("${_target}" STREQUAL "")
    return()
  endif()
  set(_output "${WORK_DIR}/edited_makernote_${format}_${safety}.${extension}")
  set(_target_makernote_hex "")
  if(NOT "${expect_present}")
    _om_exiftool_makernote_hex(
      "target ${format} MakerNote baseline" "${_target}"
      _target_makernote_hex)
  endif()
  set(_common
    --no-build-info
    --source-meta "${_source_makernote_jpg}"
    --target-width "${width}"
    --target-height "${height}"
    --target-orientation 1
    --target-samples-per-pixel "${samples}"
    --target-bits-per-sample "${bits}"
    --target-sample-format "${sample_format}"
    --target-photometric "${photometric}"
    --target-planar-configuration "${planar}"
    --target-exif-color-space "${color_space}"
    --output "${_output}"
    --force)
  if("${safety}" STREQUAL "rendered")
    list(APPEND _common --transfer-safety rendered)
  endif()

  if("${format}" STREQUAL "jpg")
    _om_run("metatransfer MakerNote ${format} ${safety}"
      "${METATRANSFER_BIN}" ${_common} --target-jpeg "${_target}")
  elseif("${format}" STREQUAL "tif")
    _om_run("metatransfer MakerNote ${format} ${safety}"
      "${METATRANSFER_BIN}" ${_common} --target-tiff "${_target}")
  elseif("${format}" STREQUAL "dng")
    _om_run("metatransfer MakerNote ${format} ${safety}"
      "${METATRANSFER_BIN}" ${_common} --target-dng "${_target}")
  else()
    _om_run("metatransfer MakerNote ${format} ${safety}"
      "${METATRANSFER_BIN}" ${_common} "--target-${format}" "${_target}")
  endif()

  if(NOT EXISTS "${_output}")
    message(FATAL_ERROR
      "metatransfer did not write MakerNote ${format} ${safety}: ${_output}")
  endif()
  _om_check_decodable_file("${format}" "${_output}")
  _om_check_exiftool_metadata_clean(
    "edited ${format} MakerNote ${safety}" "${_output}")
  if("${expect_present}")
    _om_check_exiftool_makernote_exact(
      "edited ${format} MakerNote ${safety}" "${_output}"
      "${_makernote_hex}")
  else()
    _om_check_exiftool_makernote_exact(
      "edited ${format} MakerNote ${safety}" "${_output}"
      "${_target_makernote_hex}")
  endif()
  _om_check_exiftool_xmp_title(
    "edited ${format} MakerNote ${safety}" "${_output}"
    "${_makernote_xmp_title}")
endfunction()

function(_om_transfer_default_makernote_and_check format extension safety
         expect_present)
  _om_set_default_image_spec(_default)
  _om_transfer_makernote_and_check("${format}" "${extension}" "${safety}"
    "${expect_present}" "${_default_WIDTH}" "${_default_HEIGHT}"
    "${_default_SAMPLES}" "${_default_BITS}"
    "${_default_SAMPLE_FORMAT}" "${_default_PHOTOMETRIC}"
    "${_default_PLANAR}" "${_default_EXIF_COLOR_SPACE}")
endfunction()

function(_om_transfer_bmff_makernote_if_available format extension)
  _om_prepare_bmff_target_if_available("${format}" "${extension}" "_makernote"
    " MakerNote" _target _configured_target)
  if("${_target}" STREQUAL "")
    return()
  endif()

  if(_configured_target)
    _om_probe_target_image_spec("${format}" "${_target}" _spec _have_spec)
    if(NOT _have_spec)
      return()
    endif()
  else()
    _om_set_default_image_spec(_spec)
  endif()

  set("TARGET_${format}" "${_target}")
  _om_transfer_makernote_and_check("${format}" "${extension}" "compatible"
    TRUE "${_spec_WIDTH}" "${_spec_HEIGHT}" "${_spec_SAMPLES}"
    "${_spec_BITS}" "${_spec_SAMPLE_FORMAT}" "${_spec_PHOTOMETRIC}"
    "${_spec_PLANAR}" "${_spec_EXIF_COLOR_SPACE}")
  _om_transfer_makernote_and_check("${format}" "${extension}" "rendered"
    FALSE "${_spec_WIDTH}" "${_spec_HEIGHT}" "${_spec_SAMPLES}"
    "${_spec_BITS}" "${_spec_SAMPLE_FORMAT}" "${_spec_PHOTOMETRIC}"
    "${_spec_PLANAR}" "${_spec_EXIF_COLOR_SPACE}")
endfunction()

function(_om_transfer_bmff_if_available format extension)
  _om_prepare_bmff_target_if_available("${format}" "${extension}" "" ""
    _target _configured_target)
  if("${_target}" STREQUAL "")
    return()
  endif()

  if(_configured_target)
    _om_probe_target_image_spec("${format}" "${_target}" _spec _have_spec)
    if(NOT _have_spec)
      return()
    endif()
  else()
    _om_set_default_image_spec(_spec)
  endif()

  set("TARGET_${format}" "${_target}")
  _om_transfer_and_check("${format}" "${extension}"
    "${_spec_WIDTH}" "${_spec_HEIGHT}" "${_spec_SAMPLES}" "${_spec_BITS}"
    "${_spec_SAMPLE_FORMAT}" "${_spec_PHOTOMETRIC}" "${_spec_PLANAR}"
    "${_spec_EXIF_COLOR_SPACE}" "${_spec_FINITE_COUNT}"
    "${_spec_STRICT_OIIO}")
  _om_check_bmff_exif_reader_layout(
    "${format}" "${WORK_DIR}/edited.${extension}" "${_spec_WIDTH}"
    "${_spec_HEIGHT}")
endfunction()

function(_om_prepare_bmff_target_if_available format extension suffix label
         out_var out_configured_var)
  string(TOUPPER "${format}" _format_upper)
  set(_target_var "BMFF_${_format_upper}_TEST_TARGET")
  set(_configured_target "")
  if(DEFINED ${_target_var})
    set(_configured_target "${${_target_var}}")
  endif()

  if(NOT "${_configured_target}" STREQUAL "")
    if(NOT EXISTS "${_configured_target}")
      message(FATAL_ERROR
        "configured ${format} image usability target does not exist: "
        "${_configured_target}")
    endif()
    message(STATUS
      "using configured ${format}${label} image usability target: "
      "${_configured_target}")
    set("${out_var}" "${_configured_target}" PARENT_SCOPE)
    set("${out_configured_var}" TRUE PARENT_SCOPE)
    return()
  endif()

  set(_target "${WORK_DIR}/target${suffix}.${extension}")
  execute_process(
    COMMAND "${OIIOTOOL_BIN}" --pattern checker 64x32 3 -d uint8 -o "${_target}"
    RESULT_VARIABLE _rv_create
    OUTPUT_VARIABLE _out_create
    ERROR_VARIABLE _err_create
  )
  if(NOT _rv_create EQUAL 0)
    message(STATUS
      "skipping ${format}${label} image usability check; "
      "oiiotool could not create target")
    set("${out_var}" "" PARENT_SCOPE)
    set("${out_configured_var}" FALSE PARENT_SCOPE)
    return()
  endif()
  if(NOT EXISTS "${_target}")
    message(STATUS
      "skipping ${format}${label} image usability check; "
      "oiiotool did not write target")
    set("${out_var}" "" PARENT_SCOPE)
    set("${out_configured_var}" FALSE PARENT_SCOPE)
    return()
  endif()

  set("${out_var}" "${_target}" PARENT_SCOPE)
  set("${out_configured_var}" FALSE PARENT_SCOPE)
endfunction()

function(_om_check_bmff_icc_metadata format path)
  execute_process(
    COMMAND "${METATRANSFER_BIN}" --no-build-info
            "--target-${format}"
            --no-exif
            --no-xmp
            --no-iptc
            "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "metatransfer could not summarize edited ${format} ICC (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "bmff_property colr/prof count=1")
    message(FATAL_ERROR
      "metatransfer summary missing BMFF ICC colr/prof for ${format}\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()

  if(NOT DEFINED EXIFTOOL_BIN OR EXIFTOOL_BIN STREQUAL ""
     OR NOT EXISTS "${EXIFTOOL_BIN}")
    return()
  endif()

  execute_process(
    COMMAND "${EXIFTOOL_BIN}" -validate -warning -error -icc_profile:all
            "${path}"
    RESULT_VARIABLE _rv_exiftool
    OUTPUT_VARIABLE _out_exiftool
    ERROR_VARIABLE _err_exiftool
  )
  if(NOT _rv_exiftool EQUAL 0)
    message(FATAL_ERROR
      "exiftool could not read edited ${format} ICC (${_rv_exiftool})\nstdout:\n${_out_exiftool}\nstderr:\n${_err_exiftool}")
  endif()
  if(_out_exiftool MATCHES "Error[ ]*:")
    message(FATAL_ERROR
      "exiftool reported an ICC error for edited ${format}\n${_out_exiftool}")
  endif()
  if(_out_exiftool MATCHES "Duplicate tag 'ipma'")
    message(FATAL_ERROR
      "exiftool reported duplicate ipma for edited ${format}\n${_out_exiftool}")
  endif()
  if(NOT _out_exiftool MATCHES "Profile File Signature[ ]*: acsp")
    message(FATAL_ERROR
      "exiftool did not find the transferred ICC profile for ${format}\n${_out_exiftool}")
  endif()
endfunction()

function(_om_check_bmff_xmp_metadata format path)
  execute_process(
    COMMAND "${METATRANSFER_BIN}" --no-build-info
            "--target-${format}"
            --no-exif
            --no-icc
            --no-iptc
            "${path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "metatransfer could not summarize edited ${format} XMP (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "bmff_item mime/xmp count=1")
    message(FATAL_ERROR
      "metatransfer summary missing single BMFF XMP item for ${format}\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()

  if(NOT DEFINED EXIFTOOL_BIN OR EXIFTOOL_BIN STREQUAL ""
     OR NOT EXISTS "${EXIFTOOL_BIN}")
    return()
  endif()

  execute_process(
    COMMAND "${EXIFTOOL_BIN}" -validate -warning -error -XMP:all "${path}"
    RESULT_VARIABLE _rv_exiftool
    OUTPUT_VARIABLE _out_exiftool
    ERROR_VARIABLE _err_exiftool
  )
  if(NOT _rv_exiftool EQUAL 0)
    message(FATAL_ERROR
      "exiftool could not read edited ${format} XMP (${_rv_exiftool})\nstdout:\n${_out_exiftool}\nstderr:\n${_err_exiftool}")
  endif()
  if(_out_exiftool MATCHES "Error[ ]*:")
    message(FATAL_ERROR
      "exiftool reported an XMP error for edited ${format}\n${_out_exiftool}")
  endif()
  if(_out_exiftool MATCHES "Duplicate tag 'ipma'")
    message(FATAL_ERROR
      "exiftool reported duplicate ipma for edited ${format}\n${_out_exiftool}")
  endif()
  if("${format}" STREQUAL "cr3")
    return()
  endif()
  if(NOT _out_exiftool MATCHES "Title[ ]*: OpenMeta XMP Gate")
    message(FATAL_ERROR
      "exiftool did not find the transferred XMP title for ${format}\n${_out_exiftool}")
  endif()
endfunction()

function(_om_transfer_bmff_icc_if_available format extension)
  _om_prepare_bmff_target_if_available("${format}" "${extension}" "_icc"
    " ICC" _target _configured_target)
  if("${_target}" STREQUAL "")
    return()
  endif()

  set(_output "${WORK_DIR}/edited_icc.${extension}")

  _om_run("metatransfer image usability ${format} ICC"
    "${METATRANSFER_BIN}" --no-build-info
    --source-meta "${_source_icc_jpg}"
    --no-exif
    --no-xmp
    --no-iptc
    --output "${_output}"
    --force
    "--target-${format}" "${_target}")

  if(NOT EXISTS "${_output}")
    message(FATAL_ERROR
      "metatransfer did not write edited ${format} ICC output: ${_output}")
  endif()
  _om_check_decodable_file("${format}" "${_output}")
  _om_check_bmff_icc_metadata("${format}" "${_output}")
endfunction()

function(_om_transfer_bmff_xmp_if_available format extension)
  _om_prepare_bmff_target_if_available("${format}" "${extension}" "_xmp"
    " XMP" _target _configured_target)
  if("${_target}" STREQUAL "")
    return()
  endif()

  set(_output "${WORK_DIR}/edited_xmp.${extension}")

  _om_run("metatransfer image usability ${format} XMP"
    "${METATRANSFER_BIN}" --no-build-info
    --source-meta "${_source_xmp_jpg}"
    --no-exif
    --no-icc
    --no-iptc
    --output "${_output}"
    --force
    "--target-${format}" "${_target}")

  if(NOT EXISTS "${_output}")
    message(FATAL_ERROR
      "metatransfer did not write edited ${format} XMP output: ${_output}")
  endif()
  _om_check_decodable_file("${format}" "${_output}")
  _om_check_bmff_xmp_metadata("${format}" "${_output}")
endfunction()

_om_create_target("jpg" "jpg")
_om_create_target("tif" "tif")
_om_create_target("dng" "dng")
_om_create_target("png" "png")
_om_create_target("webp" "webp")
_om_create_target("jp2" "jp2")
_om_create_target("jxl" "jxl")

_om_transfer_default_and_check("jpg" "jpg")
_om_transfer_default_and_check("tif" "tif")
_om_transfer_default_and_check("dng" "dng")
_om_transfer_default_and_check("png" "png")
_om_transfer_default_and_check("webp" "webp")
_om_transfer_default_and_check("jp2" "jp2")
_om_transfer_default_and_check("jxl" "jxl")

_om_transfer_default_makernote_and_check("jpg" "jpg" "compatible" TRUE)
_om_transfer_default_makernote_and_check("jpg" "jpg" "rendered" FALSE)
_om_transfer_default_makernote_and_check("tif" "tif" "compatible" TRUE)
_om_transfer_default_makernote_and_check("tif" "tif" "rendered" FALSE)
_om_transfer_default_makernote_and_check("dng" "dng" "compatible" TRUE)
_om_transfer_default_makernote_and_check("dng" "dng" "rendered" FALSE)
_om_transfer_default_makernote_and_check("webp" "webp" "compatible" TRUE)
_om_transfer_default_makernote_and_check("webp" "webp" "rendered" FALSE)
_om_transfer_default_makernote_and_check("jxl" "jxl" "compatible" TRUE)
_om_transfer_default_makernote_and_check("jxl" "jxl" "rendered" FALSE)

_om_transfer_bmff_if_available("heif" "heic")
_om_transfer_bmff_if_available("avif" "avif")
_om_transfer_bmff_if_available("cr3" "cr3")
_om_transfer_bmff_makernote_if_available("heif" "heic")
_om_transfer_bmff_makernote_if_available("avif" "avif")
_om_transfer_bmff_makernote_if_available("cr3" "cr3")
_om_transfer_bmff_icc_if_available("heif" "heic")
_om_transfer_bmff_icc_if_available("avif" "avif")
_om_transfer_bmff_icc_if_available("cr3" "cr3")
_om_transfer_bmff_xmp_if_available("heif" "heic")
_om_transfer_bmff_xmp_if_available("avif" "avif")
_om_transfer_bmff_xmp_if_available("cr3" "cr3")

message(STATUS "metatransfer external image usability gate passed")
