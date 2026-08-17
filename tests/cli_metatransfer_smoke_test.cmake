cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/test_python_interpreter.cmake")

if(NOT DEFINED METATRANSFER_BIN OR METATRANSFER_BIN STREQUAL "")
  message(FATAL_ERROR "METATRANSFER_BIN is required")
endif()
if(NOT EXISTS "${METATRANSFER_BIN}")
  message(FATAL_ERROR "metatransfer binary not found: ${METATRANSFER_BIN}")
endif()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_cli_metatransfer_smoke")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_jpg "${WORK_DIR}/sample.jpg")
set(_jpg_xmp "${WORK_DIR}/sample_xmp.jpg")
set(_jpg_exr "${WORK_DIR}/sample_exr.jpg")
set(_icc_jpg "${WORK_DIR}/sample_icc.jpg")
set(_target_jpg "${WORK_DIR}/target.jpg")
set(_target_jpg_xmp "${WORK_DIR}/target_xmp.jpg")
set(_dump_dir "${WORK_DIR}/payloads")
set(_edited_jpg "${WORK_DIR}/edited.jpg")
set(_target_spec_jpg "${WORK_DIR}/target_spec.jpg")
set(_target_spec_tif "${WORK_DIR}/target_spec.tif")
set(_target_spec_dng "${WORK_DIR}/target_spec.dng")
set(_dual_jpg "${WORK_DIR}/dual_write.jpg")
set(_dual_jpg_sidecar "${WORK_DIR}/dual_write.xmp")
set(_embed_only_strip_jpg "${WORK_DIR}/embed_only_strip.jpg")
set(_embed_only_strip_sidecar "${WORK_DIR}/embed_only_strip.xmp")
set(_sidecar_only_strip_jpg "${WORK_DIR}/sidecar_only_strip.jpg")
set(_sidecar_only_strip_sidecar "${WORK_DIR}/sidecar_only_strip.xmp")
set(_destination_merge_jpg "${WORK_DIR}/destination_merge.jpg")
set(_target_jxl "${WORK_DIR}/target.jxl")
set(_edited_jxl "${WORK_DIR}/edited.jxl")
set(_target_jp2 "${WORK_DIR}/target.jp2")
set(_edited_jp2 "${WORK_DIR}/edited.jp2")
set(_jxl_handoff "${WORK_DIR}/jxl_encoder_handoff.omjxic")
set(_split_jpg "${WORK_DIR}/split_injected.jpg")
set(_target_tif "${WORK_DIR}/target.tif")
set(_target_dng "${WORK_DIR}/target.dng")
set(_sdk_target_dng "${WORK_DIR}/sdk_target.dng")
set(_sdk_target_dng_before "${WORK_DIR}/sdk_target_before.dng")
set(_target_tif_xmp "${WORK_DIR}/target_xmp.tif")
set(_split_tif "${WORK_DIR}/split_injected.tif")
set(_split_dng "${WORK_DIR}/split_injected.dng")
set(_target_tif_be "${WORK_DIR}/target_be.tif")
set(_split_tif_be "${WORK_DIR}/split_injected_be.tif")
set(_jpg_rich "${WORK_DIR}/sample_rich.jpg")
set(_rendered_safety_jpg "${WORK_DIR}/sample_rendered_safety.jpg")
set(_c2pa_jpg "${WORK_DIR}/sample_c2pa.jpg")
set(_c2pa_jxl "${WORK_DIR}/sample_c2pa.jxl")
set(_c2pa_heif "${WORK_DIR}/sample_c2pa.heif")
set(_jumbf_box "${WORK_DIR}/sample.jumbf")
set(_signed_c2pa_box "${WORK_DIR}/signed_c2pa.jumb")
set(_signed_c2pa_manifest "${WORK_DIR}/signed_c2pa_manifest.bin")
set(_signed_c2pa_chain "${WORK_DIR}/signed_c2pa_chain.bin")
set(_c2pa_binding_out "${WORK_DIR}/sample_c2pa.binding.bin")
set(_c2pa_jxl_binding_out "${WORK_DIR}/sample_c2pa_jxl.binding.bin")
set(_c2pa_heif_binding_out "${WORK_DIR}/sample_c2pa_heif.binding.bin")
set(_c2pa_handoff_out "${WORK_DIR}/sample_c2pa.handoff.bin")
set(_c2pa_heif_handoff_out "${WORK_DIR}/sample_c2pa_heif.handoff.bin")
set(_signed_c2pa_package_out "${WORK_DIR}/sample_c2pa.signed.bin")
set(_signed_c2pa_jxl_package_out "${WORK_DIR}/sample_c2pa_jxl.signed.bin")
set(_signed_c2pa_heif_package_out "${WORK_DIR}/sample_c2pa_heif.signed.bin")
set(_transfer_payload_batch_out "${WORK_DIR}/sample.payload_batch.omtpld")
set(_transfer_package_batch_out "${WORK_DIR}/sample.package_batch.omtpkg")
set(_exr_attribute_batch_dump "${WORK_DIR}/sample.exr.txt")
set(_signed_c2pa_edited "${WORK_DIR}/signed_c2pa_edited.jpg")
set(_signed_c2pa_jxl_edited "${WORK_DIR}/signed_c2pa_edited.jxl")
set(_signed_c2pa_from_package "${WORK_DIR}/signed_c2pa_from_package.jpg")
set(_signed_c2pa_heif_edited "${WORK_DIR}/signed_c2pa_edited.heif")
set(_signed_c2pa_heif_from_package "${WORK_DIR}/signed_c2pa_from_package.heif")
set(_split_tif_rich "${WORK_DIR}/split_rich.tif")
set(_split_tif_be_rich "${WORK_DIR}/split_rich_be.tif")
set(_rich_builder_py "${WORK_DIR}/build_rich_exif_fixture.py")
set(_rendered_safety_builder_py "${WORK_DIR}/build_rendered_safety_fixture.py")
set(_rich_checker_py "${WORK_DIR}/check_rich_tiff_transfer.py")
set(_target_spec_checker_py "${WORK_DIR}/check_target_spec.py")
set(_sidecar_only_strip_tif "${WORK_DIR}/sidecar_only_strip_tiff.tif")
set(_sidecar_only_strip_tif_sidecar "${WORK_DIR}/sidecar_only_strip_tiff.xmp")
file(MAKE_DIRECTORY "${_dump_dir}")

#Minimal JPEG with APP1 Exif payload:
#SOI + APP1(Exif + tiny TIFF IFD0 with DateTime ASCII tag) + EOI.
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; t=bytearray(); t+=b'II*\\x00'; t+=(8).to_bytes(4,'little'); t+=(1).to_bytes(2,'little'); t+=(0x0132).to_bytes(2,'little'); t+=(2).to_bytes(2,'little'); t+=(20).to_bytes(4,'little'); t+=(26).to_bytes(4,'little'); t+=(0).to_bytes(4,'little'); t+=b'2000:01:02 03:04:05\\x00'; app1=b'Exif\\x00\\x00'+bytes(t); ln=(len(app1)+2).to_bytes(2,'big'); jpg=b'\\xFF\\xD8\\xFF\\xE1'+ln+app1+b'\\xFF\\xD9'; Path(r'''${_jpg}''').write_bytes(jpg)"
  RESULT_VARIABLE _rv_write
  OUTPUT_VARIABLE _out_write
  ERROR_VARIABLE _err_write
)
if(NOT _rv_write EQUAL 0)
  message(FATAL_ERROR
    "failed to write metatransfer fixture (${_rv_write})\nstdout:\n${_out_write}\nstderr:\n${_err_write}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; t=bytearray(); t+=b'II*\\x00'; t+=(8).to_bytes(4,'little'); t+=(1).to_bytes(2,'little'); t+=(0x0132).to_bytes(2,'little'); t+=(2).to_bytes(2,'little'); t+=(20).to_bytes(4,'little'); t+=(26).to_bytes(4,'little'); t+=(0).to_bytes(4,'little'); t+=b'2000:01:02 03:04:05\\x00'; app1=b'Exif\\x00\\x00'+bytes(t); xml=b\"<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'><xmp:CreatorTool>OpenMeta Transfer Source</xmp:CreatorTool></rdf:Description></rdf:RDF></x:xmpmeta>\"; xmp=b'http://ns.adobe.com/xap/1.0/\\x00'+xml; ln1=(len(app1)+2).to_bytes(2,'big'); ln2=(len(xmp)+2).to_bytes(2,'big'); jpg=b'\\xFF\\xD8\\xFF\\xE1'+ln1+app1+b'\\xFF\\xE1'+ln2+xmp+b'\\xFF\\xD9'; Path(r'''${_jpg_xmp}''').write_bytes(jpg)"
  RESULT_VARIABLE _rv_write_xmp
  OUTPUT_VARIABLE _out_write_xmp
  ERROR_VARIABLE _err_write_xmp
)
if(NOT _rv_write_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write xmp source jpeg fixture (${_rv_write_xmp})\nstdout:\n${_out_write_xmp}\nstderr:\n${_err_write_xmp}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; t=bytearray(); t+=b'II*\\x00'; t+=(8).to_bytes(4,'little'); t+=(1).to_bytes(2,'little'); t+=(0x010F).to_bytes(2,'little'); t+=(2).to_bytes(2,'little'); t+=(7).to_bytes(4,'little'); t+=(26).to_bytes(4,'little'); t+=(0).to_bytes(4,'little'); t+=b'Vendor\\x00'; app1=b'Exif\\x00\\x00'+bytes(t); ln=(len(app1)+2).to_bytes(2,'big'); jpg=b'\\xFF\\xD8\\xFF\\xE1'+ln+app1+b'\\xFF\\xD9'; Path(r'''${_jpg_exr}''').write_bytes(jpg)"
  RESULT_VARIABLE _rv_write_exr
  OUTPUT_VARIABLE _out_write_exr
  ERROR_VARIABLE _err_write_exr
)
if(NOT _rv_write_exr EQUAL 0)
  message(FATAL_ERROR
    "failed to write exr-source jpeg fixture (${_rv_write_exr})\nstdout:\n${_out_write_exr}\nstderr:\n${_err_write_exr}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; p=bytearray(156); p[0:4]=(156).to_bytes(4,'big'); p[36:40]=b'acsp'; p[128:132]=(1).to_bytes(4,'big'); p[132:136]=b'desc'; p[136:140]=(144).to_bytes(4,'big'); p[140:144]=(12).to_bytes(4,'big'); p[144:156]=bytes([0x11])*12; app2=b'ICC_PROFILE\\x00\\x01\\x01'+bytes(p); ln=(len(app2)+2).to_bytes(2,'big'); jpg=b'\\xFF\\xD8\\xFF\\xE2'+ln+app2+b'\\xFF\\xD9'; Path(r'''${_icc_jpg}''').write_bytes(jpg)"
  RESULT_VARIABLE _rv_write_icc
  OUTPUT_VARIABLE _out_write_icc
  ERROR_VARIABLE _err_write_icc
)
if(NOT _rv_write_icc EQUAL 0)
  message(FATAL_ERROR
    "failed to write icc jpeg fixture (${_rv_write_icc})\nstdout:\n${_out_write_icc}\nstderr:\n${_err_write_icc}")
endif()

#Minimal metadata - free JPEG target(SOI + EOI)
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; Path(r'''${_target_jpg}''').write_bytes(bytes([255,216,255,217]))"
  RESULT_VARIABLE _rv_write_target
  OUTPUT_VARIABLE _out_write_target
  ERROR_VARIABLE _err_write_target
)
if(NOT _rv_write_target EQUAL 0)
  message(FATAL_ERROR
    "failed to write target jpeg fixture (${_rv_write_target})\nstdout:\n${_out_write_target}\nstderr:\n${_err_write_target}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; xml=b\"<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'><xmp:CreatorTool>Target Embedded Existing</xmp:CreatorTool></rdf:Description></rdf:RDF></x:xmpmeta>\"; app1=b'http://ns.adobe.com/xap/1.0/\\x00'+xml; ln=(len(app1)+2).to_bytes(2,'big'); Path(r'''${_target_jpg_xmp}''').write_bytes(b'\\xFF\\xD8\\xFF\\xE1'+ln+app1+b'\\xFF\\xD9')"
  RESULT_VARIABLE _rv_write_target_xmp
  OUTPUT_VARIABLE _out_write_target_xmp
  ERROR_VARIABLE _err_write_target_xmp
)
if(NOT _rv_write_target_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write target jpeg xmp fixture (${_rv_write_target_xmp})\nstdout:\n${_out_write_target_xmp}\nstderr:\n${_err_write_target_xmp}")
endif()

# Minimal JXL container target: signature + jxlc codestream box.
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; u32=lambda v:(v).to_bytes(4,'big'); box=lambda t,p:u32(8+len(p))+t+p; Path(r'''${_target_jxl}''').write_bytes(u32(12)+b'JXL '+u32(0x0D0A870A)+box(b'jxlc', bytes([0x11,0x22,0x33,0x44])))"
  RESULT_VARIABLE _rv_write_jxl
  OUTPUT_VARIABLE _out_write_jxl
  ERROR_VARIABLE _err_write_jxl
)
if(NOT _rv_write_jxl EQUAL 0)
  message(FATAL_ERROR
    "failed to write target jxl fixture (${_rv_write_jxl})\nstdout:\n${_out_write_jxl}\nstderr:\n${_err_write_jxl}")
endif()

# Minimal JP2 target: signature + ftyp + free box.
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; u32=lambda v:(v).to_bytes(4,'big'); box=lambda t,p:u32(8+len(p))+t+p; ftyp=b'jp2 '+u32(0)+b'jp2 '; sig=u32(12)+b'jP  '+u32(0x0D0A870A); Path(r'''${_target_jp2}''').write_bytes(sig+box(b'ftyp', ftyp)+box(b'free', bytes([0x11,0x22,0x33])))"
  RESULT_VARIABLE _rv_write_jp2
  OUTPUT_VARIABLE _out_write_jp2
  ERROR_VARIABLE _err_write_jp2
)
if(NOT _rv_write_jp2 EQUAL 0)
  message(FATAL_ERROR
    "failed to write target jp2 fixture (${_rv_write_jp2})\nstdout:\n${_out_write_jp2}\nstderr:\n${_err_write_jp2}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; xml=b\"<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'><xmp:CreatorTool>Target Embedded Existing</xmp:CreatorTool></rdf:Description></rdf:RDF></x:xmpmeta>\"; xoff=38; ifd=bytearray(); ifd+=(1).to_bytes(2,'little'); ifd+=(700).to_bytes(2,'little'); ifd+=(1).to_bytes(2,'little'); ifd+=len(xml).to_bytes(4,'little'); ifd+=xoff.to_bytes(4,'little'); ifd+=(0).to_bytes(4,'little'); b=bytearray(); b+=b'II'; b+=(42).to_bytes(2,'little'); b+=(8).to_bytes(4,'little'); b+=ifd; b+=xml; Path(r'''${_target_tif_xmp}''').write_bytes(bytes(b))"
  RESULT_VARIABLE _rv_write_target_tif_xmp
  OUTPUT_VARIABLE _out_write_target_tif_xmp
  ERROR_VARIABLE _err_write_target_tif_xmp
)
if(NOT _rv_write_target_tif_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write target tiff xmp fixture (${_rv_write_target_tif_xmp})\nstdout:\n${_out_write_target_tif_xmp}\nstderr:\n${_err_write_target_tif_xmp}")
endif()

#Minimal classic TIFF target(II + 42 + IFD0 at offset 8 with 0 entries)
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; b=bytearray(); b+=b'II'; b+=(42).to_bytes(2,'little'); b+=(8).to_bytes(4,'little'); b+=(0).to_bytes(2,'little'); b+=(0).to_bytes(4,'little'); Path(r'''${_target_tif}''').write_bytes(bytes(b))"
  RESULT_VARIABLE _rv_write_tiff
  OUTPUT_VARIABLE _out_write_tiff
  ERROR_VARIABLE _err_write_tiff
)
if(NOT _rv_write_tiff EQUAL 0)
  message(FATAL_ERROR
    "failed to write target tiff fixture (${_rv_write_tiff})\nstdout:\n${_out_write_tiff}\nstderr:\n${_err_write_tiff}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; b=bytearray(); b+=b'II'; b+=(42).to_bytes(2,'little'); b+=(8).to_bytes(4,'little'); b+=(0).to_bytes(2,'little'); b+=(0).to_bytes(4,'little'); Path(r'''${_target_dng}''').write_bytes(bytes(b))"
  RESULT_VARIABLE _rv_write_dng
  OUTPUT_VARIABLE _out_write_dng
  ERROR_VARIABLE _err_write_dng
)
if(NOT _rv_write_dng EQUAL 0)
  message(FATAL_ERROR
    "failed to write target dng fixture (${_rv_write_dng})\nstdout:\n${_out_write_dng}\nstderr:\n${_err_write_dng}")
endif()
file(COPY_FILE "${_target_dng}" "${_sdk_target_dng}")
file(COPY_FILE "${_target_dng}" "${_sdk_target_dng_before}")

# Minimal classic big-endian TIFF target (MM + 42 + IFD0 at offset 8 with 0 entries)
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; b=bytearray(); b+=b'MM'; b+=(42).to_bytes(2,'big'); b+=(8).to_bytes(4,'big'); b+=(0).to_bytes(2,'big'); b+=(0).to_bytes(4,'big'); Path(r'''${_target_tif_be}''').write_bytes(bytes(b))"
  RESULT_VARIABLE _rv_write_tiff_be
  OUTPUT_VARIABLE _out_write_tiff_be
  ERROR_VARIABLE _err_write_tiff_be
)
if(NOT _rv_write_tiff_be EQUAL 0)
  message(FATAL_ERROR
    "failed to write big-endian target tiff fixture (${_rv_write_tiff_be})\nstdout:\n${_out_write_tiff_be}\nstderr:\n${_err_write_tiff_be}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; cbor=bytes([0xA1,0x61,0x61,0x01]); jumd=b'acme\\x00'; box=lambda t,p: (8+len(p)).to_bytes(4,'big')+t+p; payload=box(b'jumd', jumd)+box(b'cbor', cbor); Path(r'''${_jumbf_box}''').write_bytes(box(b'jumb', payload))"
  RESULT_VARIABLE _rv_write_jumbf
  OUTPUT_VARIABLE _out_write_jumbf
  ERROR_VARIABLE _err_write_jumbf
)
if(NOT _rv_write_jumbf EQUAL 0)
  message(FATAL_ERROR
    "failed to write raw jumbf fixture (${_rv_write_jumbf})\nstdout:\n${_out_write_jumbf}\nstderr:\n${_err_write_jumbf}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; cbor=bytes([0xA1,0x61,0x61,0x01]); jumd=b'c2pa\\x00'; box=lambda t,p: (8+len(p)).to_bytes(4,'big')+t+p; jumb=box(b'jumb', box(b'jumd', jumd)+box(b'cbor', cbor)); seg=b'JP\\x00\\x00'+(1).to_bytes(4,'big')+jumb; jpg=b'\\xFF\\xD8\\xFF\\xEB'+(len(seg)+2).to_bytes(2,'big')+seg+b'\\xFF\\xD9'; Path(r'''${_c2pa_jpg}''').write_bytes(jpg)"
  RESULT_VARIABLE _rv_write_c2pa
  OUTPUT_VARIABLE _out_write_c2pa
  ERROR_VARIABLE _err_write_c2pa
)
if(NOT _rv_write_c2pa EQUAL 0)
  message(FATAL_ERROR
    "failed to write c2pa jpeg fixture (${_rv_write_c2pa})\nstdout:\n${_out_write_c2pa}\nstderr:\n${_err_write_c2pa}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; u32=lambda v:(v).to_bytes(4,'big'); box=lambda t,p:u32(8+len(p))+t+p; cbor=bytes([0xA1,0x61,0x61,0x01]); jumd=b'c2pa\\x00'; logical=box(b'jumb', box(b'jumd', jumd)+box(b'cbor', cbor)); Path(r'''${_c2pa_jxl}''').write_bytes(u32(12)+b'JXL '+u32(0x0D0A870A)+box(b'jxlc', bytes([0x11,0x22,0x33,0x44]))+box(b'jumb', logical[8:]))"
  RESULT_VARIABLE _rv_write_c2pa_jxl
  OUTPUT_VARIABLE _out_write_c2pa_jxl
  ERROR_VARIABLE _err_write_c2pa_jxl
)
if(NOT _rv_write_c2pa_jxl EQUAL 0)
  message(FATAL_ERROR
    "failed to write c2pa jxl fixture (${_rv_write_c2pa_jxl})\nstdout:\n${_out_write_c2pa_jxl}\nstderr:\n${_err_write_c2pa_jxl}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; jumd=b'c2pa\\x00'; box=lambda t,p: (8+len(p)).to_bytes(4,'big')+t+p; cbor=bytearray(); cbor+=bytes([0xA1,0x68])+b'manifest'; cbor+=bytes([0x81,0xA2,0x6F])+b'claim_generator'; cbor+=bytes([0x64])+b'test'; cbor+=bytes([0x66])+b'claims'; cbor+=bytes([0x81,0xA2,0x6A])+b'assertions'; cbor+=bytes([0x81,0xA1,0x65])+b'label'; cbor+=bytes([0x6E])+b'c2pa.hash.data'; cbor+=bytes([0x6A])+b'signatures'; cbor+=bytes([0x81,0xA2,0x63])+b'alg'; cbor+=bytes([0x65])+b'ES256'; cbor+=bytes([0x69])+b'signature'; cbor+=bytes([0x44,0x01,0x02,0x03,0x04]); Path(r'''${_signed_c2pa_box}''').write_bytes(box(b'jumb', box(b'jumd', jumd)+box(b'cbor', bytes(cbor)))); Path(r'''${_signed_c2pa_manifest}''').write_bytes(bytes(cbor)); Path(r'''${_signed_c2pa_chain}''').write_bytes(bytes([0x30,0x82,0x01,0x00]))"
  RESULT_VARIABLE _rv_write_signed_c2pa
  OUTPUT_VARIABLE _out_write_signed_c2pa
  ERROR_VARIABLE _err_write_signed_c2pa
)
if(NOT _rv_write_signed_c2pa EQUAL 0)
  message(FATAL_ERROR
    "failed to write signed c2pa fixtures (${_rv_write_signed_c2pa})\nstdout:\n${_out_write_signed_c2pa}\nstderr:\n${_err_write_signed_c2pa}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; u32=lambda v:(v).to_bytes(4,'big'); box=lambda t,p:u32(8+len(p))+t+p; uuidbox=lambda u,p:u32(24+len(p))+b'uuid'+u+p; logical=Path(r'''${_signed_c2pa_box}''').read_bytes(); marker=b'openmeta:bmff_transfer_meta:v1'; uuid=b'OpenMetaBmffMeta'; infe=b'\\x02\\x00\\x00\\x00'+(1).to_bytes(2,'big')+(0).to_bytes(2,'big')+b'c2pa'+b'C2PA\\x00'; iinf=box(b'iinf', b'\\x00\\x00\\x00\\x00'+(1).to_bytes(2,'big')+box(b'infe', infe)); idat=box(b'idat', logical); iloc=box(b'iloc', b'\\x01\\x00\\x00\\x00'+bytes([0x44,0x40])+(1).to_bytes(2,'big')+(1).to_bytes(2,'big')+(1).to_bytes(2,'big')+(0).to_bytes(2,'big')+(0).to_bytes(4,'big')+(1).to_bytes(2,'big')+(0).to_bytes(4,'big')+len(logical).to_bytes(4,'big')); meta=box(b'meta', b'\\x00\\x00\\x00\\x00'+uuidbox(uuid, marker)+iinf+idat+iloc); ftyp=box(b'ftyp', b'heic'+u32(0)+b'mif1heic'); mdat=box(b'mdat', bytes([0x11,0x22,0x33,0x44])); Path(r'''${_c2pa_heif}''').write_bytes(ftyp+mdat+meta)"
  RESULT_VARIABLE _rv_write_c2pa_heif
  OUTPUT_VARIABLE _out_write_c2pa_heif
  ERROR_VARIABLE _err_write_c2pa_heif
)
if(NOT _rv_write_c2pa_heif EQUAL 0)
  message(FATAL_ERROR
    "failed to write c2pa heif fixture (${_rv_write_c2pa_heif})\nstdout:\n${_out_write_c2pa_heif}\nstderr:\n${_err_write_c2pa_heif}")
endif()

file(WRITE "${_rendered_safety_builder_py}" [=[
import struct
import sys
from pathlib import Path

def u16le(v): return struct.pack("<H", v)
def u32le(v): return struct.pack("<I", v)
def i32le(v): return struct.pack("<i", v)

def align2(v): return (v + 1) & ~1

class Entry:
    __slots__ = ("tag", "typ", "count", "value", "inline", "value_off")
    def __init__(self, tag, typ, count, value):
        self.tag = tag
        self.typ = typ
        self.count = count
        self.value = value
        self.inline = False
        self.value_off = 0

def add_ascii(dst, tag, text):
    raw = text.encode("ascii") + b"\x00"
    dst.append(Entry(tag, 2, len(raw), raw))

def add_long(dst, tag, values):
    raw = bytearray()
    for v in values:
        raw += u32le(v)
    dst.append(Entry(tag, 4, len(values), bytes(raw)))

def add_undefined(dst, tag, raw):
    dst.append(Entry(tag, 7, len(raw), raw))

def add_srational(dst, tag, pairs):
    raw = bytearray()
    for n, d in pairs:
        raw += i32le(n)
        raw += i32le(d)
    dst.append(Entry(tag, 10, len(pairs), bytes(raw)))

def add_pointer(dst, tag):
    dst.append(Entry(tag, 4, 1, u32le(0)))

def find_entry(dst, tag):
    for entry in dst:
        if entry.tag == tag:
            return entry
    raise RuntimeError(f"missing tag {tag}")

def write_ifd(buf, off, entries):
    buf[off:off + 2] = u16le(len(entries))
    pos = off + 2
    for entry in entries:
        buf[pos:pos + 2] = u16le(entry.tag)
        buf[pos + 2:pos + 4] = u16le(entry.typ)
        buf[pos + 4:pos + 8] = u32le(entry.count)
        if entry.inline:
            raw = entry.value + b"\x00" * (4 - len(entry.value))
            buf[pos + 8:pos + 12] = raw[:4]
        else:
            buf[pos + 8:pos + 12] = u32le(entry.value_off)
        pos += 12
    buf[pos:pos + 4] = u32le(0)

if len(sys.argv) != 2:
    raise SystemExit("usage: build_rendered_safety_fixture.py <out.jpg>")

ifd0 = []
exififd = []

add_long(ifd0, 0x0100, [999])
add_ascii(ifd0, 0x010F, "CameraVendor")
add_pointer(ifd0, 0x8769)
add_srational(
    ifd0,
    0xC621,
    [(1, 1), (0, 1), (0, 1), (0, 1), (1, 1),
     (0, 1), (0, 1), (0, 1), (1, 1)])
add_long(ifd0, 0xC68D, [0, 0, 4000, 6000])
add_undefined(ifd0, 0xC6F6, b"\x01\x02\x03\x04")

add_ascii(exififd, 0x9003, "2024:01:02 03:04:05")
add_undefined(exififd, 0x927C, b"MKRn")

ifd0.sort(key=lambda e: e.tag)
exififd.sort(key=lambda e: e.tag)

cursor = 8
ifd0_off = cursor
cursor += 2 + len(ifd0) * 12 + 4
exif_off = cursor
cursor += 2 + len(exififd) * 12 + 4
find_entry(ifd0, 0x8769).value = u32le(exif_off)

for entries in (ifd0, exififd):
    for entry in entries:
        if len(entry.value) <= 4:
            entry.inline = True
        else:
            cursor = align2(cursor)
            entry.value_off = cursor
            cursor += len(entry.value)

tiff = bytearray(cursor)
tiff[0:2] = b"II"
tiff[2:4] = u16le(42)
tiff[4:8] = u32le(ifd0_off)
write_ifd(tiff, ifd0_off, ifd0)
write_ifd(tiff, exif_off, exififd)
for entries in (ifd0, exififd):
    for entry in entries:
        if not entry.inline:
            tiff[entry.value_off:entry.value_off + len(entry.value)] = entry.value

exif_payload = b"Exif\x00\x00" + bytes(tiff)
xml = (
    b"<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
    b"<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
    b"<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
    b"xmlns:crs='http://ns.adobe.com/camera-raw-settings/1.0/' "
    b"xmlns:dng='http://ns.adobe.com/dng/1.0/'>"
    b"<xmp:CreatorTool>OpenMeta Transfer Source</xmp:CreatorTool>"
    b"<crs:Exposure2012>+0.35</crs:Exposure2012>"
    b"<dng:ProfileName>Source Raw Profile</dng:ProfileName>"
    b"</rdf:Description></rdf:RDF></x:xmpmeta>")
xmp_payload = b"http://ns.adobe.com/xap/1.0/\x00" + xml

profile = bytearray(156)
profile[0:4] = (156).to_bytes(4, "big")
profile[36:40] = b"acsp"
profile[128:132] = (1).to_bytes(4, "big")
profile[132:136] = b"desc"
profile[136:140] = (144).to_bytes(4, "big")
profile[140:144] = (12).to_bytes(4, "big")
profile[144:156] = bytes([0x11]) * 12
icc_payload = b"ICC_PROFILE\x00\x01\x01" + bytes(profile)

def box(t, payload):
    return (8 + len(payload)).to_bytes(4, "big") + t + payload

jumb = box(b"jumb", box(b"jumd", b"acme\x00") + box(b"cbor", b"\xA1\x61a\x01"))
jumbf_payload = b"JP\x00\x00" + (1).to_bytes(4, "big") + jumb

def segment(marker, payload):
    return b"\xFF" + bytes([marker]) + (len(payload) + 2).to_bytes(2, "big") + payload

jpg = (
    b"\xFF\xD8"
    + segment(0xE1, exif_payload)
    + segment(0xE1, xmp_payload)
    + segment(0xE2, icc_payload)
    + segment(0xEB, jumbf_payload)
    + b"\xFF\xD9")
Path(sys.argv[1]).write_bytes(jpg)
]=])

execute_process(
  COMMAND "${_openmeta_test_python}" "${_rendered_safety_builder_py}" "${_rendered_safety_jpg}"
  RESULT_VARIABLE _rv_write_rendered_safety
  OUTPUT_VARIABLE _out_write_rendered_safety
  ERROR_VARIABLE _err_write_rendered_safety
)
if(NOT _rv_write_rendered_safety EQUAL 0)
  message(FATAL_ERROR
    "failed to write rendered-safety fixture (${_rv_write_rendered_safety})\nstdout:\n${_out_write_rendered_safety}\nstderr:\n${_err_write_rendered_safety}")
endif()

file(WRITE "${_rich_builder_py}" [=[
import struct
import sys
from pathlib import Path

def u16le(v): return struct.pack("<H", v)
def u32le(v): return struct.pack("<I", v)
def align2(v): return (v + 1) & ~1

class Entry:
    __slots__ = ("tag", "typ", "count", "value", "order", "inline", "value_off", "dir_off")
    def __init__(self, tag, typ, count, value, order):
        self.tag = tag
        self.typ = typ
        self.count = count
        self.value = value
        self.order = order
        self.inline = False
        self.value_off = 0
        self.dir_off = 0

def add_ascii(dst, tag, s, order):
    dst.append(Entry(tag, 2, len(s) + 1, s.encode("ascii") + b"\x00", order))

def add_short(dst, tag, v, order):
    dst.append(Entry(tag, 3, 1, u16le(v), order))

def add_long(dst, tag, v, order):
    dst.append(Entry(tag, 4, 1, u32le(v), order))

def add_rational(dst, tag, pairs, order):
    raw = bytearray()
    for n, d in pairs:
        raw += u32le(n)
        raw += u32le(d)
    dst.append(Entry(tag, 5, len(pairs), bytes(raw), order))

def find_entry(dst, tag):
    for e in dst:
        if e.tag == tag:
            return e
    return None

if len(sys.argv) != 2:
    raise SystemExit("usage: build_rich_exif_fixture.py <out.jpg>")
out = Path(sys.argv[1])

ifd0 = []
exififd = []
gpsifd = []
interopifd = []

add_short(ifd0, 0x0112, 1, 0)
add_ascii(ifd0, 0x0132, "2000:01:02 03:04:05", 1)
add_long(ifd0, 0x8769, 0, 0xFFFFFFF0)
add_long(ifd0, 0x8825, 0, 0xFFFFFFF1)

add_short(exififd, 0x8827, 400, 0)
add_ascii(exififd, 0x9003, "2000:01:02 03:04:05", 1)
add_rational(exififd, 0x920A, [(66, 1)], 2)
add_long(exififd, 0xA005, 0, 0xFFFFFFF2)

add_ascii(gpsifd, 0x0001, "N", 0)
add_rational(gpsifd, 0x0002, [(41, 1), (24, 1), (5000, 100)], 1)

add_ascii(interopifd, 0x0001, "R98", 0)

ifd0.sort(key=lambda e: (e.tag, e.order))
exififd.sort(key=lambda e: (e.tag, e.order))
gpsifd.sort(key=lambda e: (e.tag, e.order))
interopifd.sort(key=lambda e: (e.tag, e.order))

cursor = 8
ifd0_off = cursor
cursor += 2 + len(ifd0) * 12 + 4
exif_off = cursor
cursor += 2 + len(exififd) * 12 + 4
gps_off = cursor
cursor += 2 + len(gpsifd) * 12 + 4
interop_off = cursor
cursor += 2 + len(interopifd) * 12 + 4

find_entry(ifd0, 0x8769).value = u32le(exif_off)
find_entry(ifd0, 0x8825).value = u32le(gps_off)
find_entry(exififd, 0xA005).value = u32le(interop_off)

for arr in (ifd0, exififd, gpsifd, interopifd):
    for e in arr:
        if len(e.value) <= 4:
            e.inline = True
        else:
            cursor = align2(cursor)
            e.inline = False
            e.value_off = cursor
            cursor += len(e.value)

tiff = bytearray(cursor)
tiff[0:2] = b"II"
tiff[2:4] = u16le(42)
tiff[4:8] = u32le(ifd0_off)

def write_ifd(buf, off, entries):
    buf[off:off+2] = u16le(len(entries))
    p = off + 2
    for e in entries:
        buf[p+0:p+2] = u16le(e.tag)
        buf[p+2:p+4] = u16le(e.typ)
        buf[p+4:p+8] = u32le(e.count)
        if e.inline:
            raw = e.value + b"\x00" * (4 - len(e.value))
            buf[p+8:p+12] = raw[:4]
        else:
            buf[p+8:p+12] = u32le(e.value_off)
        p += 12
    buf[p:p+4] = u32le(0)

write_ifd(tiff, ifd0_off, ifd0)
write_ifd(tiff, exif_off, exififd)
write_ifd(tiff, gps_off, gpsifd)
write_ifd(tiff, interop_off, interopifd)

for arr in (ifd0, exififd, gpsifd, interopifd):
    for e in arr:
        if not e.inline:
            tiff[e.value_off:e.value_off+len(e.value)] = e.value

app1 = b"Exif\x00\x00" + bytes(tiff)
jpg = b"\xFF\xD8\xFF\xE1" + struct.pack(">H", len(app1) + 2) + app1 + b"\xFF\xD9"
out.write_bytes(jpg)
]=])

execute_process(
  COMMAND "${_openmeta_test_python}" "${_rich_builder_py}" "${_jpg_rich}"
  RESULT_VARIABLE _rv_write_rich
  OUTPUT_VARIABLE _out_write_rich
  ERROR_VARIABLE _err_write_rich
)
if(NOT _rv_write_rich EQUAL 0)
  message(FATAL_ERROR
    "failed to write rich exif fixture (${_rv_write_rich})\nstdout:\n${_out_write_rich}\nstderr:\n${_err_write_rich}")
endif()

file(WRITE "${_rich_checker_py}" [=[
import sys
from pathlib import Path

TYPE_SIZE = {
    1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 6: 1, 7: 1,
    8: 2, 9: 4, 10: 8, 11: 4, 12: 8,
}

def fail(msg):
    print(msg, file=sys.stderr)
    raise SystemExit(1)

def read_u16(b, off, end):
    return int.from_bytes(b[off:off+2], end, signed=False)

def read_u32(b, off, end):
    return int.from_bytes(b[off:off+4], end, signed=False)

def parse_ifd(b, off, end):
    if off == 0 or off + 2 > len(b):
        fail("ifd offset out of range")
    n = read_u16(b, off, end)
    p = off + 2
    if p + n * 12 + 4 > len(b):
        fail("ifd entries truncated")
    out = {}
    for i in range(n):
        e = p + i * 12
        tag = read_u16(b, e + 0, end)
        typ = read_u16(b, e + 2, end)
        cnt = read_u32(b, e + 4, end)
        val = b[e + 8:e + 12]
        out[tag] = (typ, cnt, val)
    nxt = read_u32(b, p + n * 12, end)
    return out, nxt

def payload_for(b, entries, tag, end):
    if tag not in entries:
        fail(f"missing tag 0x{tag:04X}")
    typ, cnt, val = entries[tag]
    sz = TYPE_SIZE.get(typ, 0)
    if sz == 0:
        fail(f"unsupported tag type {typ}")
    n = sz * cnt
    if n <= 4:
        return typ, cnt, val[:n]
    off = int.from_bytes(val, end, signed=False)
    if off + n > len(b):
        fail(f"value for tag 0x{tag:04X} out of range")
    return typ, cnt, b[off:off+n]

def ascii_val(raw):
    return raw.split(b"\x00", 1)[0].decode("ascii", "ignore")

def short_val(raw, end):
    return int.from_bytes(raw[:2], end, signed=False)

def long_val(raw, end):
    return int.from_bytes(raw[:4], end, signed=False)

def rationals(raw, end):
    out = []
    for i in range(0, len(raw), 8):
        n = int.from_bytes(raw[i:i+4], end, signed=False)
        d = int.from_bytes(raw[i+4:i+8], end, signed=False)
        out.append((n, d))
    return out

if len(sys.argv) != 4:
    fail("usage: check_rich_tiff_transfer.py <tiff> <DateTime> <DateTimeOriginal>")

path = Path(sys.argv[1])
expect_dt = sys.argv[2]
expect_dto = sys.argv[3]
b = path.read_bytes()

if len(b) < 8:
    fail("tiff too small")
if b[0:2] == b"II":
    end = "little"
elif b[0:2] == b"MM":
    end = "big"
else:
    fail("invalid byte order")
if read_u16(b, 2, end) != 42:
    fail("invalid tiff magic")

ifd0_off = read_u32(b, 4, end)
ifd0, _ = parse_ifd(b, ifd0_off, end)

if 0x02BC not in ifd0:
    fail("missing XMP tag 700")

_, _, dt_raw = payload_for(b, ifd0, 0x0132, end)
if ascii_val(dt_raw) != expect_dt:
    fail("DateTime mismatch")

_, _, exif_ptr_raw = payload_for(b, ifd0, 0x8769, end)
_, _, gps_ptr_raw = payload_for(b, ifd0, 0x8825, end)
exif_off = long_val(exif_ptr_raw, end)
gps_off = long_val(gps_ptr_raw, end)
if exif_off == 0 or gps_off == 0:
    fail("missing ExifIFD/GPS pointers")

exififd, _ = parse_ifd(b, exif_off, end)
gpsifd, _ = parse_ifd(b, gps_off, end)

_, _, dto_raw = payload_for(b, exififd, 0x9003, end)
if ascii_val(dto_raw) != expect_dto:
    fail("DateTimeOriginal mismatch")

_, _, iso_raw = payload_for(b, exififd, 0x8827, end)
if short_val(iso_raw, end) != 400:
    fail("ISO value mismatch")

_, _, focal_raw = payload_for(b, exififd, 0x920A, end)
if rationals(focal_raw, end) != [(66, 1)]:
    fail("FocalLength mismatch")

_, _, interop_ptr_raw = payload_for(b, exififd, 0xA005, end)
interop_off = long_val(interop_ptr_raw, end)
if interop_off == 0:
    fail("missing InteropIFD pointer")
interopifd, _ = parse_ifd(b, interop_off, end)
_, _, interop_raw = payload_for(b, interopifd, 0x0001, end)
if ascii_val(interop_raw) != "R98":
    fail("InteropIndex mismatch")

_, _, lat_ref_raw = payload_for(b, gpsifd, 0x0001, end)
if ascii_val(lat_ref_raw) != "N":
    fail("GPSLatitudeRef mismatch")

_, _, lat_raw = payload_for(b, gpsifd, 0x0002, end)
if rationals(lat_raw, end) != [(41, 1), (24, 1), (5000, 100)]:
    fail("GPSLatitude mismatch")
]=])

file(WRITE "${_target_spec_checker_py}" [=[
import sys
from pathlib import Path

TYPE_SIZE = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 7: 1, 9: 4, 10: 8}

def fail(msg):
    print(msg, file=sys.stderr)
    raise SystemExit(1)

def read_u16(b, off, end):
    return int.from_bytes(b[off:off+2], end, signed=False)

def read_u32(b, off, end):
    return int.from_bytes(b[off:off+4], end, signed=False)

def exif_tiff_from_file(path):
    b = Path(path).read_bytes()
    if len(b) >= 8 and (b[:2] == b"II" or b[:2] == b"MM"):
        return b
    if len(b) < 4 or b[:2] != b"\xff\xd8":
        fail("not a JPEG or TIFF-family file")
    pos = 2
    while pos + 4 <= len(b):
        if b[pos] != 0xFF:
            fail("invalid JPEG marker stream")
        marker = b[pos + 1]
        pos += 2
        if marker == 0xD9:
            break
        if marker == 0xDA:
            fail("scan reached before Exif APP1")
        if pos + 2 > len(b):
            fail("truncated JPEG segment length")
        ln = int.from_bytes(b[pos:pos+2], "big")
        if ln < 2 or pos + ln > len(b):
            fail("invalid JPEG segment length")
        payload = b[pos+2:pos+ln]
        if marker == 0xE1 and payload.startswith(b"Exif\x00\x00"):
            return payload[6:]
        pos += ln
    fail("missing Exif APP1")

def parse_ifd(b, off, end):
    if off == 0 or off + 2 > len(b):
        fail("IFD offset out of range")
    n = read_u16(b, off, end)
    p = off + 2
    if p + n * 12 + 4 > len(b):
        fail("IFD entries truncated")
    out = {}
    for i in range(n):
        e = p + i * 12
        tag = read_u16(b, e, end)
        typ = read_u16(b, e + 2, end)
        cnt = read_u32(b, e + 4, end)
        raw = b[e + 8:e + 12]
        out[tag] = (typ, cnt, raw)
    return out

def values(b, entries, tag, end):
    if tag not in entries:
        fail(f"missing tag 0x{tag:04X}")
    typ, cnt, raw = entries[tag]
    size = TYPE_SIZE.get(typ, 0)
    if size == 0:
        fail(f"unsupported type for tag 0x{tag:04X}: {typ}")
    byte_count = size * cnt
    data = raw[:byte_count]
    if byte_count > 4:
        off = read_u32(raw, 0, end)
        if off + byte_count > len(b):
            fail(f"value for tag 0x{tag:04X} out of range")
        data = b[off:off+byte_count]
    out = []
    if typ == 3:
        for i in range(cnt):
            out.append(read_u16(data, i * 2, end))
    elif typ == 4:
        for i in range(cnt):
            out.append(read_u32(data, i * 4, end))
    else:
        fail(f"unexpected type for numeric tag 0x{tag:04X}: {typ}")
    return out

def expect(entries, tag, expected, where):
    actual = values(tiff, entries, tag, endian)
    if actual != expected:
        fail(f"{where}:0x{tag:04X} mismatch: {actual!r}")

if len(sys.argv) != 2:
    fail("usage: check_target_spec.py <jpeg-or-tiff>")

tiff = exif_tiff_from_file(sys.argv[1])
if len(tiff) < 8:
    fail("Exif TIFF too small")
if tiff[:2] == b"II":
    endian = "little"
elif tiff[:2] == b"MM":
    endian = "big"
else:
    fail("invalid TIFF byte order")
if read_u16(tiff, 2, endian) != 42:
    fail("invalid TIFF magic")

ifd0 = parse_ifd(tiff, read_u32(tiff, 4, endian), endian)
exif_ptr = values(tiff, ifd0, 0x8769, endian)
if not exif_ptr or exif_ptr[0] == 0:
    fail("missing ExifIFD pointer")
exififd = parse_ifd(tiff, exif_ptr[0], endian)

expect(ifd0, 0x0100, [320], "ifd0")
expect(ifd0, 0x0101, [240], "ifd0")
expect(exififd, 0xA002, [320], "exififd")
expect(exififd, 0xA003, [240], "exififd")
expect(ifd0, 0x0112, [1], "ifd0")
expect(ifd0, 0x0115, [3], "ifd0")
expect(ifd0, 0x0102, [8, 8, 8], "ifd0")
expect(ifd0, 0x0153, [1, 1, 1], "ifd0")
expect(ifd0, 0x0106, [2], "ifd0")
expect(ifd0, 0x011C, [1], "ifd0")
expect(exififd, 0xA001, [1], "exififd")
]=])

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --time-patch "DateTime=2024:12:31 23:59:59"
          "${_jpg}"
  RESULT_VARIABLE _rv_probe
  OUTPUT_VARIABLE _out_probe
  ERROR_VARIABLE _err_probe
)
if(NOT _rv_probe EQUAL 0)
  message(FATAL_ERROR
    "metatransfer probe failed (${_rv_probe})\nstdout:\n${_out_probe}\nstderr:\n${_err_probe}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --dump-exr-attribute-batch "${_exr_attribute_batch_dump}"
          --force
          "${_jpg_exr}"
  RESULT_VARIABLE _rv_exr_dump
  OUTPUT_VARIABLE _out_exr_dump
  ERROR_VARIABLE _err_exr_dump
)
if(NOT _rv_exr_dump EQUAL 0)
  message(FATAL_ERROR
    "metatransfer exr attribute batch dump failed (${_rv_exr_dump})\nstdout:\n${_out_exr_dump}\nstderr:\n${_err_exr_dump}")
endif()
if(NOT _out_exr_dump MATCHES "exr_attribute_batch_dump: status=ok path=")
  message(FATAL_ERROR
    "metatransfer exr attribute batch dump missing summary\nstdout:\n${_out_exr_dump}\nstderr:\n${_err_exr_dump}")
endif()
if(NOT EXISTS "${_exr_attribute_batch_dump}")
  message(FATAL_ERROR "expected exr attribute batch dump was not written")
endif()
file(READ "${_exr_attribute_batch_dump}" _exr_dump_text)
if(NOT _exr_dump_text MATCHES "exr_attribute_batch: status=ok")
  message(FATAL_ERROR
    "exr attribute batch dump missing batch status\n${_exr_dump_text}")
endif()
if(NOT _exr_dump_text MATCHES "name=Make")
  message(FATAL_ERROR
    "exr attribute batch dump missing Make attribute\n${_exr_dump_text}")
endif()
if(NOT _exr_dump_text MATCHES "type=string")
  message(FATAL_ERROR
    "exr attribute batch dump missing string type\n${_exr_dump_text}")
endif()
if(NOT _exr_dump_text MATCHES "value_ascii=Vendor")
  message(FATAL_ERROR
    "exr attribute batch dump missing ascii value\n${_exr_dump_text}")
endif()
if(NOT _exr_dump_text MATCHES "value_hex=56656E646F72")
  message(FATAL_ERROR
    "exr attribute batch dump missing hex value\n${_exr_dump_text}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --update-dng-sdk-file "${_sdk_target_dng}"
          "${_jpg_exr}"
  RESULT_VARIABLE _rv_dng_sdk_update
  OUTPUT_VARIABLE _out_dng_sdk_update
  ERROR_VARIABLE _err_dng_sdk_update
)
if(NOT _rv_dng_sdk_update EQUAL 0 AND NOT _out_dng_sdk_update MATCHES "dng_sdk_file_update: status=unsupported")
  message(FATAL_ERROR
    "metatransfer dng sdk update failed (${_rv_dng_sdk_update})\nstdout:\n${_out_dng_sdk_update}\nstderr:\n${_err_dng_sdk_update}")
endif()
if(_out_dng_sdk_update MATCHES "dng_sdk_file_update: status=ok")
  if(NOT _out_dng_sdk_update MATCHES "available=true")
    message(FATAL_ERROR
      "metatransfer dng sdk update missing available=true summary\nstdout:\n${_out_dng_sdk_update}\nstderr:\n${_err_dng_sdk_update}")
  endif()
  if(NOT _out_dng_sdk_update MATCHES "updated_stream=true")
    message(FATAL_ERROR
      "metatransfer dng sdk update missing updated_stream=true summary\nstdout:\n${_out_dng_sdk_update}\nstderr:\n${_err_dng_sdk_update}")
  endif()
  execute_process(
  COMMAND "${_openmeta_test_python}" -c
      "from pathlib import Path; import sys; a=Path(r'''${_sdk_target_dng_before}''').read_bytes(); b=Path(r'''${_sdk_target_dng}''').read_bytes(); sys.exit(0 if a!=b else 1)"
    RESULT_VARIABLE _rv_dng_sdk_update_check
    OUTPUT_VARIABLE _out_dng_sdk_update_check
    ERROR_VARIABLE _err_dng_sdk_update_check
  )
  if(NOT _rv_dng_sdk_update_check EQUAL 0)
    message(FATAL_ERROR
      "metatransfer dng sdk update did not mutate the target file\nstdout:\n${_out_dng_sdk_update}\nstderr:\n${_err_dng_sdk_update}\ncheck_stderr:\n${_err_dng_sdk_update_check}")
  endif()
elseif(_out_dng_sdk_update MATCHES "dng_sdk_file_update: status=unsupported")
  if(NOT _out_dng_sdk_update MATCHES "available=false")
    message(FATAL_ERROR
      "metatransfer dng sdk unsupported summary missing available=false\nstdout:\n${_out_dng_sdk_update}\nstderr:\n${_err_dng_sdk_update}")
  endif()
else()
  message(FATAL_ERROR
    "metatransfer dng sdk update missing expected summary\nstdout:\n${_out_dng_sdk_update}\nstderr:\n${_err_dng_sdk_update}")
endif()
if(NOT _out_probe MATCHES "prepare: status=ok")
  message(FATAL_ERROR
    "metatransfer probe missing prepare ok\nstdout:\n${_out_probe}\nstderr:\n${_err_probe}")
endif()
if(NOT _out_probe MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "metatransfer probe missing emit ok\nstdout:\n${_out_probe}\nstderr:\n${_err_probe}")
endif()
if(NOT _out_probe MATCHES "time_patch: status=ok patched=1")
  message(FATAL_ERROR
    "metatransfer probe missing time_patch patched=1\nstdout:\n${_out_probe}\nstderr:\n${_err_probe}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --xmp-include-existing
          --transfer-safety rendered
          "${_rendered_safety_jpg}"
  RESULT_VARIABLE _rv_rendered_safety
  OUTPUT_VARIABLE _out_rendered_safety
  ERROR_VARIABLE _err_rendered_safety
)
if(NOT _rv_rendered_safety EQUAL 0)
  message(FATAL_ERROR
    "metatransfer rendered safety probe failed (${_rv_rendered_safety})\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "prepare: status=ok")
  message(FATAL_ERROR
    "metatransfer rendered safety missing prepare ok\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "policy\\[image_properties\\]: requested=keep effective=drop reason=target_image_properties matched=[1-9][0-9]*")
  message(FATAL_ERROR
    "metatransfer rendered safety missing image-properties policy\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "policy\\[icc_profile\\]: requested=keep effective=drop reason=safety_mode_filtered matched=[1-9][0-9]*")
  message(FATAL_ERROR
    "metatransfer rendered safety missing ICC policy\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "policy\\[raw_color_calibration\\]: requested=keep effective=drop reason=safety_mode_filtered matched=[1-9][0-9]*")
  message(FATAL_ERROR
    "metatransfer rendered safety missing raw color/correction policy\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "policy\\[camera_raw_settings\\]: requested=keep effective=drop reason=safety_mode_filtered matched=[1-9][0-9]*")
  message(FATAL_ERROR
    "metatransfer rendered safety missing camera raw settings policy\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "policy\\[makernote\\]: requested=keep effective=drop reason=safety_mode_filtered matched=[1-9][0-9]*")
  message(FATAL_ERROR
    "metatransfer rendered safety missing MakerNote policy\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "policy\\[jumbf\\]: requested=keep effective=drop reason=safety_mode_filtered matched=[1-9][0-9]*")
  message(FATAL_ERROR
    "metatransfer rendered safety missing JUMBF policy\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --jpeg-jumbf "${_jumbf_box}"
          "${_jpg}"
  RESULT_VARIABLE _rv_jumbf
  OUTPUT_VARIABLE _out_jumbf
  ERROR_VARIABLE _err_jumbf
)
if(NOT _rv_jumbf EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jpeg jumbf append failed (${_rv_jumbf})\nstdout:\n${_out_jumbf}\nstderr:\n${_err_jumbf}")
endif()
if(NOT _out_jumbf MATCHES "append_jumbf: status=ok")
  message(FATAL_ERROR
    "metatransfer jpeg jumbf append missing append ok\nstdout:\n${_out_jumbf}\nstderr:\n${_err_jumbf}")
endif()
if(NOT _out_jumbf MATCHES "route=jpeg:app11-jumbf")
  message(FATAL_ERROR
    "metatransfer jpeg jumbf append missing prepared app11 route\nstdout:\n${_out_jumbf}\nstderr:\n${_err_jumbf}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --c2pa-policy invalidate
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_c2pa
  OUTPUT_VARIABLE _out_c2pa
  ERROR_VARIABLE _err_c2pa
)
if(NOT _rv_c2pa EQUAL 0)
  message(FATAL_ERROR
    "metatransfer c2pa invalidate failed (${_rv_c2pa})\nstdout:\n${_out_c2pa}\nstderr:\n${_err_c2pa}")
endif()
if(NOT _out_c2pa MATCHES "policy\\[c2pa\\]: requested=invalidate effective=keep reason=draft_invalidation_payload mode=draft_unsigned_invalidation source=content_bound output=generated_draft_unsigned_invalidation")
  message(FATAL_ERROR
    "metatransfer c2pa invalidate missing resolved policy\nstdout:\n${_out_c2pa}\nstderr:\n${_err_c2pa}")
endif()
if(NOT _out_c2pa MATCHES "route=jpeg:app11-c2pa")
  message(FATAL_ERROR
    "metatransfer c2pa invalidate missing prepared app11 c2pa route\nstdout:\n${_out_c2pa}\nstderr:\n${_err_c2pa}")
endif()
if(NOT _out_c2pa MATCHES "c2pa_rewrite: state=not_requested target=jpeg source=content_bound matched=[0-9]+ existing_segments=1 carrier_available=no invalidates_existing=no")
  message(FATAL_ERROR
    "metatransfer c2pa invalidate missing rewrite summary\nstdout:\n${_out_c2pa}\nstderr:\n${_err_c2pa}")
endif()
if(NOT _out_c2pa MATCHES "c2pa_sign_request: status=unsupported carrier=jpeg:app11-c2pa manifest_label=c2pa source_ranges=0 prepared_segments=0 bytes=0")
  message(FATAL_ERROR
    "metatransfer c2pa invalidate missing sign-request summary\nstdout:\n${_out_c2pa}\nstderr:\n${_err_c2pa}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --c2pa-policy rewrite
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_c2pa_rewrite
  OUTPUT_VARIABLE _out_c2pa_rewrite
  ERROR_VARIABLE _err_c2pa_rewrite
)
if(NOT _rv_c2pa_rewrite EQUAL 0)
  if(NOT _rv_c2pa_rewrite EQUAL 1)
    message(FATAL_ERROR
      "metatransfer c2pa rewrite returned unexpected exit (${_rv_c2pa_rewrite})\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
  endif()
endif()
if(NOT _out_c2pa_rewrite MATCHES "policy\\[c2pa\\]: requested=rewrite effective=drop reason=signed_rewrite_unavailable mode=drop source=content_bound output=dropped")
  message(FATAL_ERROR
    "metatransfer c2pa rewrite missing resolved policy\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
endif()
if(NOT _out_c2pa_rewrite MATCHES "c2pa_rewrite: state=signing_material_required target=jpeg source=content_bound matched=[0-9]+ existing_segments=1 carrier_available=yes invalidates_existing=yes")
  message(FATAL_ERROR
    "metatransfer c2pa rewrite missing rewrite requirements summary\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
endif()
if(NOT _out_c2pa_rewrite MATCHES "c2pa_rewrite_requirements: manifest_builder=yes content_binding=yes certificate_chain=yes private_key=yes signing_time=yes")
  message(FATAL_ERROR
    "metatransfer c2pa rewrite missing rewrite requirements line\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
endif()
if(NOT _out_c2pa_rewrite MATCHES "c2pa_rewrite_binding: chunks=2 bytes=4")
  message(FATAL_ERROR
    "metatransfer c2pa rewrite missing binding summary\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
endif()
if(NOT _out_c2pa_rewrite MATCHES "c2pa_rewrite_chunk\\[0\\]: kind=source_range offset=0 size=2")
  message(FATAL_ERROR
    "metatransfer c2pa rewrite missing first binding chunk\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
endif()
if(NOT _out_c2pa_rewrite MATCHES "c2pa_sign_request: status=ok carrier=jpeg:app11-c2pa manifest_label=c2pa source_ranges=2 prepared_segments=0 bytes=4")
  message(FATAL_ERROR
    "metatransfer c2pa rewrite missing sign-request summary\nstdout:\n${_out_c2pa_rewrite}\nstderr:\n${_err_c2pa_rewrite}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --dump-transfer-payload-batch "${_transfer_payload_batch_out}"
          --force
          "${_jpg}"
  RESULT_VARIABLE _rv_payload_batch
  OUTPUT_VARIABLE _out_payload_batch
  ERROR_VARIABLE _err_payload_batch
)
if(NOT _rv_payload_batch EQUAL 0)
  message(FATAL_ERROR
    "metatransfer payload batch dump failed (${_rv_payload_batch})\nstdout:\n${_out_payload_batch}\nstderr:\n${_err_payload_batch}")
endif()
if(NOT _out_payload_batch MATCHES "transfer_payload_batch: status=ok code=none bytes=[0-9]+ errors=0 path=")
  message(FATAL_ERROR
    "metatransfer payload batch dump missing summary\nstdout:\n${_out_payload_batch}\nstderr:\n${_err_payload_batch}")
endif()
if(NOT EXISTS "${_transfer_payload_batch_out}")
  message(FATAL_ERROR "expected transfer payload batch dump was not written")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --load-transfer-payload-batch "${_transfer_payload_batch_out}"
  RESULT_VARIABLE _rv_payload_batch_load
  OUTPUT_VARIABLE _out_payload_batch_load
  ERROR_VARIABLE _err_payload_batch_load
)
if(NOT _rv_payload_batch_load EQUAL 0)
  message(FATAL_ERROR
    "metatransfer payload batch load failed (${_rv_payload_batch_load})\nstdout:\n${_out_payload_batch_load}\nstderr:\n${_err_payload_batch_load}")
endif()
if(NOT _out_payload_batch_load MATCHES "transfer_payload_batch: status=ok code=none bytes=[0-9]+ payloads=[0-9]+ target=jpeg")
  message(FATAL_ERROR
    "metatransfer payload batch load missing summary\nstdout:\n${_out_payload_batch_load}\nstderr:\n${_err_payload_batch_load}")
endif()
if(NOT _out_payload_batch_load MATCHES "\\[0\\] semantic=Exif route=jpeg:app1-exif")
  message(FATAL_ERROR
    "metatransfer payload batch load missing first payload summary\nstdout:\n${_out_payload_batch_load}\nstderr:\n${_err_payload_batch_load}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --load-transfer-artifact "${_transfer_payload_batch_out}"
  RESULT_VARIABLE _rv_artifact_payload_load
  OUTPUT_VARIABLE _out_artifact_payload_load
  ERROR_VARIABLE _err_artifact_payload_load
)
if(NOT _rv_artifact_payload_load EQUAL 0)
  message(FATAL_ERROR
    "metatransfer generic artifact load for payload batch failed (${_rv_artifact_payload_load})\nstdout:\n${_out_artifact_payload_load}\nstderr:\n${_err_artifact_payload_load}")
endif()
if(NOT _out_artifact_payload_load MATCHES "transfer_artifact: status=ok code=none kind=transfer_payload_batch bytes=[0-9]+ target=jpeg")
  message(FATAL_ERROR
    "metatransfer generic artifact load missing payload-batch summary\nstdout:\n${_out_artifact_payload_load}\nstderr:\n${_err_artifact_payload_load}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --dump-transfer-package-batch "${_transfer_package_batch_out}"
          --force
          "${_jpg}"
  RESULT_VARIABLE _rv_package_batch
  OUTPUT_VARIABLE _out_package_batch
  ERROR_VARIABLE _err_package_batch
)
if(NOT _rv_package_batch EQUAL 0)
  message(FATAL_ERROR
    "metatransfer package batch dump failed (${_rv_package_batch})\nstdout:\n${_out_package_batch}\nstderr:\n${_err_package_batch}")
endif()
if(NOT _out_package_batch MATCHES "transfer_package_batch: status=ok code=none bytes=[0-9]+ errors=0 path=")
  message(FATAL_ERROR
    "metatransfer package batch dump missing summary\nstdout:\n${_out_package_batch}\nstderr:\n${_err_package_batch}")
endif()
if(NOT EXISTS "${_transfer_package_batch_out}")
  message(FATAL_ERROR "expected transfer package batch dump was not written")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --load-transfer-package-batch "${_transfer_package_batch_out}"
  RESULT_VARIABLE _rv_package_batch_load
  OUTPUT_VARIABLE _out_package_batch_load
  ERROR_VARIABLE _err_package_batch_load
)
if(NOT _rv_package_batch_load EQUAL 0)
  message(FATAL_ERROR
    "metatransfer package batch load failed (${_rv_package_batch_load})\nstdout:\n${_out_package_batch_load}\nstderr:\n${_err_package_batch_load}")
endif()
if(NOT _out_package_batch_load MATCHES "transfer_package_batch: status=ok code=none bytes=[0-9]+ chunks=[0-9]+ target=jpeg")
  message(FATAL_ERROR
    "metatransfer package batch load missing summary\nstdout:\n${_out_package_batch_load}\nstderr:\n${_err_package_batch_load}")
endif()
if(NOT _out_package_batch_load MATCHES "\\[0\\] semantic=Exif route=jpeg:app1-exif")
  message(FATAL_ERROR
    "metatransfer package batch load missing first chunk summary\nstdout:\n${_out_package_batch_load}\nstderr:\n${_err_package_batch_load}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-binding "${_c2pa_binding_out}"
          --force
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_c2pa_binding
  OUTPUT_VARIABLE _out_c2pa_binding
  ERROR_VARIABLE _err_c2pa_binding
)
if(NOT _rv_c2pa_binding EQUAL 0)
  message(FATAL_ERROR
    "metatransfer c2pa binding dump failed (${_rv_c2pa_binding})\nstdout:\n${_out_c2pa_binding}\nstderr:\n${_err_c2pa_binding}")
endif()
if(NOT _out_c2pa_binding MATCHES "c2pa_binding: status=ok code=none bytes=4 errors=0 path=")
  message(FATAL_ERROR
    "metatransfer c2pa binding dump missing binding summary\nstdout:\n${_out_c2pa_binding}\nstderr:\n${_err_c2pa_binding}")
endif()
if(NOT EXISTS "${_c2pa_binding_out}")
  message(FATAL_ERROR "expected c2pa binding dump was not written")
endif()
file(READ "${_c2pa_binding_out}" _c2pa_binding_hex HEX)
if(NOT _c2pa_binding_hex STREQUAL "ffd8ffd9")
  message(FATAL_ERROR
    "c2pa binding dump bytes mismatch: ${_c2pa_binding_hex}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-handoff "${_c2pa_handoff_out}"
          --force
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_c2pa_handoff
  OUTPUT_VARIABLE _out_c2pa_handoff
  ERROR_VARIABLE _err_c2pa_handoff
)
if(NOT _rv_c2pa_handoff EQUAL 0)
  message(FATAL_ERROR
    "metatransfer c2pa handoff dump failed (${_rv_c2pa_handoff})\nstdout:\n${_out_c2pa_handoff}\nstderr:\n${_err_c2pa_handoff}")
endif()
if(NOT _out_c2pa_handoff MATCHES "c2pa_handoff_package: status=ok code=none bytes=[0-9]+ errors=0 path=")
  message(FATAL_ERROR
    "metatransfer c2pa handoff dump missing package summary\nstdout:\n${_out_c2pa_handoff}\nstderr:\n${_err_c2pa_handoff}")
endif()
if(NOT EXISTS "${_c2pa_handoff_out}")
  message(FATAL_ERROR "expected c2pa handoff package was not written")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --jpeg-c2pa-signed "${_signed_c2pa_box}"
          --c2pa-manifest-output "${_signed_c2pa_manifest}"
          --c2pa-certificate-chain "${_signed_c2pa_chain}"
          --c2pa-key-ref "test-key-ref"
          --c2pa-signing-time "2026-03-09T00:00:00Z"
          --dump-c2pa-signed-package "${_signed_c2pa_package_out}"
          --output "${_signed_c2pa_edited}" --force
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_c2pa_stage
  OUTPUT_VARIABLE _out_c2pa_stage
  ERROR_VARIABLE _err_c2pa_stage
)
if(NOT _rv_c2pa_stage EQUAL 0)
  message(FATAL_ERROR
    "metatransfer signed c2pa staging failed (${_rv_c2pa_stage})\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "prepare: status=unsupported")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing initial prepare unsupported\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_stage_validate: status=ok code=none kind=content_bound payload_bytes=[0-9]+ carrier_bytes=[0-9]+ segments=1 errors=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing validation summary\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_stage_semantics: status=ok reason=ok manifest=1 manifests=1 claim_generator=1 assertions=1 claims=1 signatures=1 linked=1 orphan=0 explicit_refs=0 unresolved=0 ambiguous=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing semantic summary\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_stage_linkage: claim0_assertions=1 claim0_refs=1 sig0_links=1")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing linkage summary\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_stage_references: sig0_keys=0 sig0_present=0 sig0_resolved=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing reference summary\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_stage: status=ok code=none emitted=1 removed=0 errors=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing stage ok\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "policy\\[c2pa\\]: requested=rewrite effective=keep reason=external_signed_payload mode=signed_rewrite source=content_bound output=signed_rewrite")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing signed rewrite policy\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_rewrite: state=ready target=jpeg source=content_bound matched=[0-9]+ existing_segments=1 carrier_available=yes invalidates_existing=yes")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing ready rewrite summary\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "c2pa_signed_package: status=ok code=none bytes=[0-9]+ errors=0 path=")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing signed-package summary\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT _out_c2pa_stage MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "metatransfer signed c2pa staging missing edit apply ok\nstdout:\n${_out_c2pa_stage}\nstderr:\n${_err_c2pa_stage}")
endif()
if(NOT EXISTS "${_signed_c2pa_edited}")
  message(FATAL_ERROR "expected edited signed c2pa jpeg was not written")
endif()
if(NOT EXISTS "${_signed_c2pa_package_out}")
  message(FATAL_ERROR "expected signed c2pa package was not written")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --no-exif --no-xmp --no-icc --no-iptc
          --load-c2pa-signed-package "${_signed_c2pa_package_out}"
          --output "${_signed_c2pa_from_package}" --force
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_c2pa_stage_pkg
  OUTPUT_VARIABLE _out_c2pa_stage_pkg
  ERROR_VARIABLE _err_c2pa_stage_pkg
)
if(NOT _rv_c2pa_stage_pkg EQUAL 0)
  message(FATAL_ERROR
    "metatransfer signed c2pa package staging failed (${_rv_c2pa_stage_pkg})\nstdout:\n${_out_c2pa_stage_pkg}\nstderr:\n${_err_c2pa_stage_pkg}")
endif()
if(NOT _out_c2pa_stage_pkg MATCHES "c2pa_signed_package_input: status=ok code=none bytes=[0-9]+ errors=0 path=")
  message(FATAL_ERROR
    "metatransfer signed c2pa package staging missing package input summary\nstdout:\n${_out_c2pa_stage_pkg}\nstderr:\n${_err_c2pa_stage_pkg}")
endif()
if(NOT _out_c2pa_stage_pkg MATCHES "c2pa_stage_validate: status=ok code=none kind=content_bound payload_bytes=[0-9]+ carrier_bytes=[0-9]+ segments=1 errors=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa package staging missing validation summary\nstdout:\n${_out_c2pa_stage_pkg}\nstderr:\n${_err_c2pa_stage_pkg}")
endif()
if(NOT _out_c2pa_stage_pkg MATCHES "c2pa_stage_semantics: status=ok reason=ok manifest=1 manifests=1 claim_generator=1 assertions=1 claims=1 signatures=1 linked=1 orphan=0 explicit_refs=0 unresolved=0 ambiguous=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa package staging missing semantic summary\nstdout:\n${_out_c2pa_stage_pkg}\nstderr:\n${_err_c2pa_stage_pkg}")
endif()
if(NOT _out_c2pa_stage_pkg MATCHES "c2pa_stage_linkage: claim0_assertions=1 claim0_refs=1 sig0_links=1")
  message(FATAL_ERROR
    "metatransfer signed c2pa package staging missing linkage summary\nstdout:\n${_out_c2pa_stage_pkg}\nstderr:\n${_err_c2pa_stage_pkg}")
endif()
if(NOT _out_c2pa_stage_pkg MATCHES "c2pa_stage_references: sig0_keys=0 sig0_present=0 sig0_resolved=0")
  message(FATAL_ERROR
    "metatransfer signed c2pa package staging missing reference summary\nstdout:\n${_out_c2pa_stage_pkg}\nstderr:\n${_err_c2pa_stage_pkg}")
endif()
if(NOT EXISTS "${_signed_c2pa_from_package}")
  message(FATAL_ERROR "expected edited jpeg from signed c2pa package was not written")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-jxl
          --no-exif --no-xmp --no-icc --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-binding "${_c2pa_jxl_binding_out}"
          --force
          "${_c2pa_jxl}"
  RESULT_VARIABLE _rv_c2pa_jxl_binding
  OUTPUT_VARIABLE _out_c2pa_jxl_binding
  ERROR_VARIABLE _err_c2pa_jxl_binding
)
if(NOT _rv_c2pa_jxl_binding EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jxl c2pa binding dump failed (${_rv_c2pa_jxl_binding})\nstdout:\n${_out_c2pa_jxl_binding}\nstderr:\n${_err_c2pa_jxl_binding}")
endif()
if(NOT _out_c2pa_jxl_binding MATCHES "c2pa_sign_request: status=ok carrier=jxl:box-jumb")
  message(FATAL_ERROR
    "metatransfer jxl c2pa binding dump missing sign-request summary\nstdout:\n${_out_c2pa_jxl_binding}\nstderr:\n${_err_c2pa_jxl_binding}")
endif()
if(NOT _out_c2pa_jxl_binding MATCHES "c2pa_binding: status=ok code=none bytes=24 errors=0 path=")
  message(FATAL_ERROR
    "metatransfer jxl c2pa binding dump missing binding summary\nstdout:\n${_out_c2pa_jxl_binding}\nstderr:\n${_err_c2pa_jxl_binding}")
endif()
if(NOT EXISTS "${_c2pa_jxl_binding_out}")
  message(FATAL_ERROR "expected jxl c2pa binding dump was not written")
endif()
file(READ "${_c2pa_jxl_binding_out}" _c2pa_jxl_binding_hex HEX)
if(NOT _c2pa_jxl_binding_hex STREQUAL "0000000c4a584c200d0a870a0000000c6a786c6311223344")
  message(FATAL_ERROR
    "jxl c2pa binding dump bytes mismatch: ${_c2pa_jxl_binding_hex}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-jxl
          --no-exif --no-xmp --no-icc --no-iptc
          --jpeg-c2pa-signed "${_signed_c2pa_box}"
          --c2pa-manifest-output "${_signed_c2pa_manifest}"
          --c2pa-certificate-chain "${_signed_c2pa_chain}"
          --c2pa-key-ref "test-key-ref"
          --c2pa-signing-time "2026-03-09T00:00:00Z"
          --dump-c2pa-signed-package "${_signed_c2pa_jxl_package_out}"
          --output "${_signed_c2pa_jxl_edited}" --force
          "${_c2pa_jxl}"
  RESULT_VARIABLE _rv_c2pa_jxl_stage
  OUTPUT_VARIABLE _out_c2pa_jxl_stage
  ERROR_VARIABLE _err_c2pa_jxl_stage
)
if(NOT _rv_c2pa_jxl_stage EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jxl signed c2pa staging failed (${_rv_c2pa_jxl_stage})\nstdout:\n${_out_c2pa_jxl_stage}\nstderr:\n${_err_c2pa_jxl_stage}")
endif()
if(NOT _out_c2pa_jxl_stage MATCHES "prepare: status=unsupported")
  message(FATAL_ERROR
    "metatransfer jxl signed c2pa staging missing initial prepare unsupported\nstdout:\n${_out_c2pa_jxl_stage}\nstderr:\n${_err_c2pa_jxl_stage}")
endif()
if(NOT _out_c2pa_jxl_stage MATCHES "c2pa_stage_validate: status=ok code=none kind=content_bound payload_bytes=[0-9]+ carrier_bytes=[0-9]+ segments=1 errors=0")
  message(FATAL_ERROR
    "metatransfer jxl signed c2pa staging missing validation summary\nstdout:\n${_out_c2pa_jxl_stage}\nstderr:\n${_err_c2pa_jxl_stage}")
endif()
if(NOT _out_c2pa_jxl_stage MATCHES "c2pa_stage: status=ok code=none emitted=1 removed=0 errors=0")
  message(FATAL_ERROR
    "metatransfer jxl signed c2pa staging missing stage ok\nstdout:\n${_out_c2pa_jxl_stage}\nstderr:\n${_err_c2pa_jxl_stage}")
endif()
if(NOT _out_c2pa_jxl_stage MATCHES "policy\\[c2pa\\]: requested=rewrite effective=keep reason=external_signed_payload mode=signed_rewrite source=content_bound output=signed_rewrite")
  message(FATAL_ERROR
    "metatransfer jxl signed c2pa staging missing signed rewrite policy\nstdout:\n${_out_c2pa_jxl_stage}\nstderr:\n${_err_c2pa_jxl_stage}")
endif()
if(NOT _out_c2pa_jxl_stage MATCHES "c2pa_rewrite: state=ready target=jxl source=content_bound matched=[0-9]+ existing_segments=1 carrier_available=yes invalidates_existing=yes")
  message(FATAL_ERROR
    "metatransfer jxl signed c2pa staging missing ready rewrite summary\nstdout:\n${_out_c2pa_jxl_stage}\nstderr:\n${_err_c2pa_jxl_stage}")
endif()
if(NOT _out_c2pa_jxl_stage MATCHES "c2pa_signed_package: status=ok code=none bytes=[0-9]+ errors=0 path=")
  message(FATAL_ERROR
    "metatransfer jxl signed c2pa staging missing signed-package summary\nstdout:\n${_out_c2pa_jxl_stage}\nstderr:\n${_err_c2pa_jxl_stage}")
endif()
if(NOT _out_c2pa_jxl_stage MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "metatransfer jxl signed c2pa staging missing edit apply ok\nstdout:\n${_out_c2pa_jxl_stage}\nstderr:\n${_err_c2pa_jxl_stage}")
endif()
if(NOT EXISTS "${_signed_c2pa_jxl_edited}")
  message(FATAL_ERROR "expected edited signed c2pa jxl was not written")
endif()
if(NOT EXISTS "${_signed_c2pa_jxl_package_out}")
  message(FATAL_ERROR "expected signed c2pa jxl package was not written")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-heif
          --no-exif --no-xmp --no-icc --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-binding "${_c2pa_heif_binding_out}"
          --force
          "${_c2pa_heif}"
  RESULT_VARIABLE _rv_c2pa_heif_binding
  OUTPUT_VARIABLE _out_c2pa_heif_binding
  ERROR_VARIABLE _err_c2pa_heif_binding
)
if(NOT _rv_c2pa_heif_binding EQUAL 0)
  message(FATAL_ERROR
    "metatransfer heif c2pa binding dump failed (${_rv_c2pa_heif_binding})\nstdout:\n${_out_c2pa_heif_binding}\nstderr:\n${_err_c2pa_heif_binding}")
endif()
if(NOT _out_c2pa_heif_binding MATCHES "c2pa_sign_request: status=ok carrier=bmff:item-c2pa")
  message(FATAL_ERROR
    "metatransfer heif c2pa binding dump missing sign-request summary\nstdout:\n${_out_c2pa_heif_binding}\nstderr:\n${_err_c2pa_heif_binding}")
endif()
if(NOT _out_c2pa_heif_binding MATCHES "c2pa_binding: status=ok code=none bytes=36 errors=0 path=")
  message(FATAL_ERROR
    "metatransfer heif c2pa binding dump missing binding summary\nstdout:\n${_out_c2pa_heif_binding}\nstderr:\n${_err_c2pa_heif_binding}")
endif()
if(NOT EXISTS "${_c2pa_heif_binding_out}")
  message(FATAL_ERROR "expected heif c2pa binding dump was not written")
endif()
file(READ "${_c2pa_heif_binding_out}" _c2pa_heif_binding_hex HEX)
if(NOT _c2pa_heif_binding_hex STREQUAL "000000186674797068656963000000006d696631686569630000000c6d64617411223344")
  message(FATAL_ERROR
    "heif c2pa binding dump bytes mismatch: ${_c2pa_heif_binding_hex}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-heif
          --no-exif --no-xmp --no-icc --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-handoff "${_c2pa_heif_handoff_out}"
          --force
          "${_c2pa_heif}"
  RESULT_VARIABLE _rv_c2pa_heif_handoff
  OUTPUT_VARIABLE _out_c2pa_heif_handoff
  ERROR_VARIABLE _err_c2pa_heif_handoff
)
if(NOT _rv_c2pa_heif_handoff EQUAL 0)
  message(FATAL_ERROR
    "metatransfer heif c2pa handoff dump failed (${_rv_c2pa_heif_handoff})\nstdout:\n${_out_c2pa_heif_handoff}\nstderr:\n${_err_c2pa_heif_handoff}")
endif()
if(NOT _out_c2pa_heif_handoff MATCHES "c2pa_handoff_package: status=ok code=none bytes=[0-9]+ errors=0 path=")
  message(FATAL_ERROR
    "metatransfer heif c2pa handoff dump missing package summary\nstdout:\n${_out_c2pa_heif_handoff}\nstderr:\n${_err_c2pa_heif_handoff}")
endif()
if(NOT EXISTS "${_c2pa_heif_handoff_out}")
  message(FATAL_ERROR "expected heif c2pa handoff package was not written")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-heif
          --no-exif --no-xmp --no-icc --no-iptc
          --jpeg-c2pa-signed "${_signed_c2pa_box}"
          --c2pa-manifest-output "${_signed_c2pa_manifest}"
          --c2pa-certificate-chain "${_signed_c2pa_chain}"
          --c2pa-key-ref "test-key-ref"
          --c2pa-signing-time "2026-03-09T00:00:00Z"
          --dump-c2pa-signed-package "${_signed_c2pa_heif_package_out}"
          --output "${_signed_c2pa_heif_edited}" --force
          "${_c2pa_heif}"
  RESULT_VARIABLE _rv_c2pa_heif_stage
  OUTPUT_VARIABLE _out_c2pa_heif_stage
  ERROR_VARIABLE _err_c2pa_heif_stage
)
if(NOT _rv_c2pa_heif_stage EQUAL 0)
  message(FATAL_ERROR
    "metatransfer heif signed c2pa staging failed (${_rv_c2pa_heif_stage})\nstdout:\n${_out_c2pa_heif_stage}\nstderr:\n${_err_c2pa_heif_stage}")
endif()
if(NOT _out_c2pa_heif_stage MATCHES "prepare: status=unsupported")
  message(FATAL_ERROR
    "metatransfer heif signed c2pa staging missing initial prepare unsupported\nstdout:\n${_out_c2pa_heif_stage}\nstderr:\n${_err_c2pa_heif_stage}")
endif()
if(NOT _out_c2pa_heif_stage MATCHES "c2pa_stage_validate: status=ok code=none kind=content_bound payload_bytes=[0-9]+ carrier_bytes=[0-9]+ segments=1 errors=0")
  message(FATAL_ERROR
    "metatransfer heif signed c2pa staging missing validation summary\nstdout:\n${_out_c2pa_heif_stage}\nstderr:\n${_err_c2pa_heif_stage}")
endif()
if(NOT _out_c2pa_heif_stage MATCHES "c2pa_stage: status=ok code=none emitted=1 removed=0 errors=0")
  message(FATAL_ERROR
    "metatransfer heif signed c2pa staging missing stage ok\nstdout:\n${_out_c2pa_heif_stage}\nstderr:\n${_err_c2pa_heif_stage}")
endif()
if(NOT _out_c2pa_heif_stage MATCHES "policy\\[c2pa\\]: requested=rewrite effective=keep reason=external_signed_payload mode=signed_rewrite source=content_bound output=signed_rewrite")
  message(FATAL_ERROR
    "metatransfer heif signed c2pa staging missing signed rewrite policy\nstdout:\n${_out_c2pa_heif_stage}\nstderr:\n${_err_c2pa_heif_stage}")
endif()
if(NOT _out_c2pa_heif_stage MATCHES "c2pa_rewrite: state=ready target=heif source=content_bound matched=[0-9]+ existing_segments=[0-9]+ carrier_available=yes invalidates_existing=yes")
  message(FATAL_ERROR
    "metatransfer heif signed c2pa staging missing ready rewrite summary\nstdout:\n${_out_c2pa_heif_stage}\nstderr:\n${_err_c2pa_heif_stage}")
endif()
if(NOT _out_c2pa_heif_stage MATCHES "c2pa_signed_package: status=ok code=none bytes=[0-9]+ errors=0 path=")
  message(FATAL_ERROR
    "metatransfer heif signed c2pa staging missing signed-package summary\nstdout:\n${_out_c2pa_heif_stage}\nstderr:\n${_err_c2pa_heif_stage}")
endif()
if(NOT _out_c2pa_heif_stage MATCHES "bmff_edit_apply: status=ok")
  message(FATAL_ERROR
    "metatransfer heif signed c2pa staging missing bmff edit apply ok\nstdout:\n${_out_c2pa_heif_stage}\nstderr:\n${_err_c2pa_heif_stage}")
endif()
if(NOT EXISTS "${_signed_c2pa_heif_edited}")
  message(FATAL_ERROR "expected edited signed c2pa heif was not written")
endif()
if(NOT EXISTS "${_signed_c2pa_heif_package_out}")
  message(FATAL_ERROR "expected signed c2pa heif package was not written")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-heif
          --no-exif --no-xmp --no-icc --no-iptc
          --load-c2pa-signed-package "${_signed_c2pa_heif_package_out}"
          --output "${_signed_c2pa_heif_from_package}" --force
          "${_c2pa_heif}"
  RESULT_VARIABLE _rv_c2pa_heif_stage_pkg
  OUTPUT_VARIABLE _out_c2pa_heif_stage_pkg
  ERROR_VARIABLE _err_c2pa_heif_stage_pkg
)
if(NOT _rv_c2pa_heif_stage_pkg EQUAL 0)
  message(FATAL_ERROR
    "metatransfer heif signed c2pa package staging failed (${_rv_c2pa_heif_stage_pkg})\nstdout:\n${_out_c2pa_heif_stage_pkg}\nstderr:\n${_err_c2pa_heif_stage_pkg}")
endif()
if(NOT _out_c2pa_heif_stage_pkg MATCHES "c2pa_signed_package_input: status=ok code=none bytes=[0-9]+ errors=0 path=")
  message(FATAL_ERROR
    "metatransfer heif signed c2pa package staging missing package input summary\nstdout:\n${_out_c2pa_heif_stage_pkg}\nstderr:\n${_err_c2pa_heif_stage_pkg}")
endif()
if(NOT _out_c2pa_heif_stage_pkg MATCHES "c2pa_stage: status=ok code=none emitted=1 removed=0 errors=0")
  message(FATAL_ERROR
    "metatransfer heif signed c2pa package staging missing stage ok\nstdout:\n${_out_c2pa_heif_stage_pkg}\nstderr:\n${_err_c2pa_heif_stage_pkg}")
endif()
if(NOT _out_c2pa_heif_stage_pkg MATCHES "bmff_edit_apply: status=ok")
  message(FATAL_ERROR
    "metatransfer heif signed c2pa package staging missing bmff edit apply ok\nstdout:\n${_out_c2pa_heif_stage_pkg}\nstderr:\n${_err_c2pa_heif_stage_pkg}")
endif()
if(NOT EXISTS "${_signed_c2pa_heif_from_package}")
  message(FATAL_ERROR "expected edited signed c2pa heif from package was not written")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --mode auto --dry-run
          --time-patch "DateTime=2024:12:31 23:59:59"
          "${_jpg}"
  RESULT_VARIABLE _rv_dry
  OUTPUT_VARIABLE _out_dry
  ERROR_VARIABLE _err_dry
)
if(NOT _rv_dry EQUAL 0)
  message(FATAL_ERROR
    "metatransfer dry-run failed (${_rv_dry})\nstdout:\n${_out_dry}\nstderr:\n${_err_dry}")
endif()
if(NOT _out_dry MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "metatransfer dry-run missing edit_plan ok\nstdout:\n${_out_dry}\nstderr:\n${_err_dry}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --mode metadata_rewrite
          --time-patch "DateTime=2024:12:31 23:59:59"
          --output "${_edited_jpg}" --force
          "${_jpg}"
  RESULT_VARIABLE _rv_edit
  OUTPUT_VARIABLE _out_edit
  ERROR_VARIABLE _err_edit
)
if(NOT _rv_edit EQUAL 0)
  message(FATAL_ERROR
    "metatransfer edit apply failed (${_rv_edit})\nstdout:\n${_out_edit}\nstderr:\n${_err_edit}")
endif()
if(NOT EXISTS "${_edited_jpg}")
  message(FATAL_ERROR
    "metatransfer did not write edited output\nstdout:\n${_out_edit}\nstderr:\n${_err_edit}")
endif()
file(SIZE "${_edited_jpg}" _edited_size)
if(_edited_size LESS 10)
  message(FATAL_ERROR
    "metatransfer wrote suspiciously small edited output (${_edited_size})\nstdout:\n${_out_edit}\nstderr:\n${_err_edit}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-jpeg "${_target_jpg}"
          --target-width 320
          --target-height 240
          --target-orientation 1
          --target-samples-per-pixel 3
          --target-bits-per-sample 8
          --target-sample-format 1
          --target-photometric 2
          --target-planar-configuration 1
          --target-exif-color-space 1
          --output "${_target_spec_jpg}" --force
  RESULT_VARIABLE _rv_target_spec
  OUTPUT_VARIABLE _out_target_spec
  ERROR_VARIABLE _err_target_spec
)
if(NOT _rv_target_spec EQUAL 0)
  message(FATAL_ERROR
    "metatransfer target-spec jpeg edit failed (${_rv_target_spec})\nstdout:\n${_out_target_spec}\nstderr:\n${_err_target_spec}")
endif()
if(NOT EXISTS "${_target_spec_jpg}")
  message(FATAL_ERROR
    "metatransfer target-spec jpeg did not write output\nstdout:\n${_out_target_spec}\nstderr:\n${_err_target_spec}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" "${_target_spec_checker_py}" "${_target_spec_jpg}"
  RESULT_VARIABLE _rv_target_spec_check
  OUTPUT_VARIABLE _out_target_spec_check
  ERROR_VARIABLE _err_target_spec_check
)
if(NOT _rv_target_spec_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer target-spec jpeg read-back check failed (${_rv_target_spec_check})\nstdout:\n${_out_target_spec}\nstderr:\n${_err_target_spec}\ncheck_stdout:\n${_out_target_spec_check}\ncheck_stderr:\n${_err_target_spec_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-tiff "${_target_tif}"
          --target-width 320
          --target-height 240
          --target-orientation 1
          --target-samples-per-pixel 3
          --target-bits-per-sample 8
          --target-sample-format 1
          --target-photometric 2
          --target-planar-configuration 1
          --target-exif-color-space 1
          --output "${_target_spec_tif}" --force
  RESULT_VARIABLE _rv_target_spec_tif
  OUTPUT_VARIABLE _out_target_spec_tif
  ERROR_VARIABLE _err_target_spec_tif
)
if(NOT _rv_target_spec_tif EQUAL 0)
  message(FATAL_ERROR
    "metatransfer target-spec tiff edit failed (${_rv_target_spec_tif})\nstdout:\n${_out_target_spec_tif}\nstderr:\n${_err_target_spec_tif}")
endif()
if(NOT EXISTS "${_target_spec_tif}")
  message(FATAL_ERROR
    "metatransfer target-spec tiff did not write output\nstdout:\n${_out_target_spec_tif}\nstderr:\n${_err_target_spec_tif}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" "${_target_spec_checker_py}" "${_target_spec_tif}"
  RESULT_VARIABLE _rv_target_spec_tif_check
  OUTPUT_VARIABLE _out_target_spec_tif_check
  ERROR_VARIABLE _err_target_spec_tif_check
)
if(NOT _rv_target_spec_tif_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer target-spec tiff read-back check failed (${_rv_target_spec_tif_check})\nstdout:\n${_out_target_spec_tif}\nstderr:\n${_err_target_spec_tif}\ncheck_stdout:\n${_out_target_spec_tif_check}\ncheck_stderr:\n${_err_target_spec_tif_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-dng "${_target_dng}"
          --target-width 320
          --target-height 240
          --target-orientation 1
          --target-samples-per-pixel 3
          --target-bits-per-sample 8
          --target-sample-format 1
          --target-photometric 2
          --target-planar-configuration 1
          --target-exif-color-space 1
          --output "${_target_spec_dng}" --force
  RESULT_VARIABLE _rv_target_spec_dng
  OUTPUT_VARIABLE _out_target_spec_dng
  ERROR_VARIABLE _err_target_spec_dng
)
if(NOT _rv_target_spec_dng EQUAL 0)
  message(FATAL_ERROR
    "metatransfer target-spec dng edit failed (${_rv_target_spec_dng})\nstdout:\n${_out_target_spec_dng}\nstderr:\n${_err_target_spec_dng}")
endif()
if(NOT EXISTS "${_target_spec_dng}")
  message(FATAL_ERROR
    "metatransfer target-spec dng did not write output\nstdout:\n${_out_target_spec_dng}\nstderr:\n${_err_target_spec_dng}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" "${_target_spec_checker_py}" "${_target_spec_dng}"
  RESULT_VARIABLE _rv_target_spec_dng_check
  OUTPUT_VARIABLE _out_target_spec_dng_check
  ERROR_VARIABLE _err_target_spec_dng_check
)
if(NOT _rv_target_spec_dng_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer target-spec dng read-back check failed (${_rv_target_spec_dng_check})\nstdout:\n${_out_target_spec_dng}\nstderr:\n${_err_target_spec_dng}\ncheck_stdout:\n${_out_target_spec_dng_check}\ncheck_stderr:\n${_err_target_spec_dng_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-jpeg "${_target_jpg}"
          --xmp-writeback embedded_and_sidecar
          --output "${_dual_jpg}" --force
  RESULT_VARIABLE _rv_dual
  OUTPUT_VARIABLE _out_dual
  ERROR_VARIABLE _err_dual
)
if(NOT _rv_dual EQUAL 0)
  message(FATAL_ERROR
    "metatransfer dual-write jpeg edit failed (${_rv_dual})\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
if(NOT EXISTS "${_dual_jpg}")
  message(FATAL_ERROR
    "metatransfer dual-write did not write jpeg output\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
if(NOT EXISTS "${_dual_jpg_sidecar}")
  message(FATAL_ERROR
    "metatransfer dual-write did not write xmp sidecar\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
if(NOT _out_dual MATCHES "xmp_sidecar_output=.*dual_write\\.xmp")
  message(FATAL_ERROR
    "metatransfer dual-write missing xmp sidecar summary\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; b=Path(r'''${_dual_jpg_sidecar}''').read_bytes(); import sys; sys.exit(0 if (b.find(b'<x:xmpmeta')!=-1 or b.find(b'<rdf:RDF')!=-1) else 1)"
  RESULT_VARIABLE _rv_dual_check
  OUTPUT_VARIABLE _out_dual_check
  ERROR_VARIABLE _err_dual_check
)
if(NOT _rv_dual_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer dual-write sidecar content check failed (${_rv_dual_check})\nstdout:\n${_out_dual_check}\nstderr:\n${_err_dual_check}")
endif()

file(WRITE "${_embed_only_strip_sidecar}" "stale sidecar\n")
execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-jpeg "${_target_jpg}"
          --xmp-destination-sidecar strip_existing
          --output "${_embed_only_strip_jpg}" --force
  RESULT_VARIABLE _rv_embed_strip
  OUTPUT_VARIABLE _out_embed_strip
  ERROR_VARIABLE _err_embed_strip
)
if(NOT _rv_embed_strip EQUAL 0)
  message(FATAL_ERROR
    "metatransfer embedded-only sidecar cleanup failed (${_rv_embed_strip})\nstdout:\n${_out_embed_strip}\nstderr:\n${_err_embed_strip}")
endif()
if(NOT EXISTS "${_embed_only_strip_jpg}")
  message(FATAL_ERROR
    "metatransfer embedded-only cleanup did not write jpeg output\nstdout:\n${_out_embed_strip}\nstderr:\n${_err_embed_strip}")
endif()
if(EXISTS "${_embed_only_strip_sidecar}")
  message(FATAL_ERROR
    "metatransfer embedded-only cleanup did not remove stale sidecar\nstdout:\n${_out_embed_strip}\nstderr:\n${_err_embed_strip}")
endif()
if(NOT _out_embed_strip MATCHES "xmp_sidecar_removed=.*embed_only_strip\\.xmp")
  message(FATAL_ERROR
    "metatransfer embedded-only cleanup missing sidecar removal summary\nstdout:\n${_out_embed_strip}\nstderr:\n${_err_embed_strip}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-jpeg "${_target_jpg_xmp}"
          --xmp-writeback sidecar
          --xmp-destination-embedded strip_existing
          --output "${_sidecar_only_strip_jpg}" --force
  RESULT_VARIABLE _rv_sidecar_strip
  OUTPUT_VARIABLE _out_sidecar_strip
  ERROR_VARIABLE _err_sidecar_strip
)
if(NOT _rv_sidecar_strip EQUAL 0)
  message(FATAL_ERROR
    "metatransfer sidecar-only embedded-strip failed (${_rv_sidecar_strip})\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}")
endif()
if(NOT EXISTS "${_sidecar_only_strip_jpg}")
  message(FATAL_ERROR
    "metatransfer sidecar-only embedded-strip did not write jpeg output\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}")
endif()
if(NOT EXISTS "${_sidecar_only_strip_sidecar}")
  message(FATAL_ERROR
    "metatransfer sidecar-only embedded-strip did not write xmp sidecar\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}")
endif()
if(NOT _out_sidecar_strip MATCHES "xmp_sidecar_output=.*sidecar_only_strip\\.xmp")
  message(FATAL_ERROR
    "metatransfer sidecar-only embedded-strip missing generated sidecar summary\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; b=Path(r'''${_sidecar_only_strip_jpg}''').read_bytes(); import sys; sys.exit(0 if (b.find(b'Target Embedded Existing')==-1 and b.find(b'http://ns.adobe.com/xap/1.0/')==-1) else 1)"
  RESULT_VARIABLE _rv_sidecar_strip_check
  OUTPUT_VARIABLE _out_sidecar_strip_check
  ERROR_VARIABLE _err_sidecar_strip_check
)
if(NOT _rv_sidecar_strip_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer sidecar-only embedded-strip output still contains embedded xmp (${_rv_sidecar_strip_check})\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}\ncheck_stderr:\n${_err_sidecar_strip_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-tiff "${_target_tif_xmp}"
          --xmp-writeback sidecar
          --xmp-destination-embedded strip_existing
          --output "${_sidecar_only_strip_tif}" --force
  RESULT_VARIABLE _rv_sidecar_strip_tif
  OUTPUT_VARIABLE _out_sidecar_strip_tif
  ERROR_VARIABLE _err_sidecar_strip_tif
)
if(NOT _rv_sidecar_strip_tif EQUAL 0)
  message(FATAL_ERROR
    "metatransfer tiff sidecar-only embedded-strip failed (${_rv_sidecar_strip_tif})\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}")
endif()
if(NOT EXISTS "${_sidecar_only_strip_tif}")
  message(FATAL_ERROR
    "metatransfer tiff sidecar-only embedded-strip did not write output\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}")
endif()
if(NOT EXISTS "${_sidecar_only_strip_tif_sidecar}")
  message(FATAL_ERROR
    "metatransfer tiff sidecar-only embedded-strip did not write xmp sidecar\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}")
endif()
if(NOT _out_sidecar_strip_tif MATCHES "xmp_sidecar_output=.*sidecar_only_strip_tiff\\.xmp")
  message(FATAL_ERROR
    "metatransfer tiff sidecar-only embedded-strip missing generated sidecar summary\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; import sys; b=Path(r'''${_sidecar_only_strip_tif}''').read_bytes(); ok=(len(b)>=8 and b[0:2]==b'II' and int.from_bytes(b[2:4],'little')==42); off=int.from_bytes(b[4:8],'little') if ok else 0; ok=ok and (off+2<=len(b)); n=int.from_bytes(b[off:off+2],'little') if ok else 0; p=off+2; ok=ok and (p+n*12+4<=len(b)); tags=[int.from_bytes(b[p+i*12:p+i*12+2],'little') for i in range(n)] if ok else []; sys.exit(0 if (ok and 700 not in tags and 0x0132 in tags) else 1)"
  RESULT_VARIABLE _rv_sidecar_strip_tif_check
  OUTPUT_VARIABLE _out_sidecar_strip_tif_check
  ERROR_VARIABLE _err_sidecar_strip_tif_check
)
if(NOT _rv_sidecar_strip_tif_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer tiff sidecar-only embedded-strip output still contains xmp tag 700 or lost DateTime (${_rv_sidecar_strip_tif_check})\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}\ncheck_stderr:\n${_err_sidecar_strip_tif_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-jpeg "${_target_jpg_xmp}"
          --xmp-include-existing
          --xmp-conflict-policy existing_wins
          --xmp-include-existing-destination-embedded
          --xmp-existing-destination-embedded-precedence destination_wins
          --output "${_destination_merge_jpg}" --force
          "${_jpg_xmp}"
  RESULT_VARIABLE _rv_destination_merge
  OUTPUT_VARIABLE _out_destination_merge
  ERROR_VARIABLE _err_destination_merge
)
if(NOT _rv_destination_merge EQUAL 0)
  message(FATAL_ERROR
    "metatransfer destination embedded merge failed (${_rv_destination_merge})\nstdout:\n${_out_destination_merge}\nstderr:\n${_err_destination_merge}")
endif()
if(NOT EXISTS "${_destination_merge_jpg}")
  message(FATAL_ERROR
    "metatransfer destination embedded merge did not write jpeg output\nstdout:\n${_out_destination_merge}\nstderr:\n${_err_destination_merge}")
endif()
if(NOT _out_destination_merge MATCHES "xmp_existing_destination_embedded: status=ok loaded=yes path=.*target_xmp\\.jpg")
  message(FATAL_ERROR
    "metatransfer destination embedded merge missing status summary\nstdout:\n${_out_destination_merge}\nstderr:\n${_err_destination_merge}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; b=Path(r'''${_destination_merge_jpg}''').read_bytes(); import sys; sys.exit(0 if (b.find(b'Target Embedded Existing')!=-1 and b.find(b'OpenMeta Transfer Source')==-1) else 1)"
  RESULT_VARIABLE _rv_destination_merge_check
  OUTPUT_VARIABLE _out_destination_merge_check
  ERROR_VARIABLE _err_destination_merge_check
)
if(NOT _rv_destination_merge_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer destination embedded precedence check failed (${_rv_destination_merge_check})\nstdout:\n${_out_destination_merge}\nstderr:\n${_err_destination_merge}\ncheck_stderr:\n${_err_destination_merge_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-jpeg "${_target_jpg}"
          --mode metadata_rewrite
          --output "${_split_jpg}" --force
  RESULT_VARIABLE _rv_split
  OUTPUT_VARIABLE _out_split
  ERROR_VARIABLE _err_split
)
if(NOT _rv_split EQUAL 0)
  message(FATAL_ERROR
    "metatransfer source/target split edit failed (${_rv_split})\nstdout:\n${_out_split}\nstderr:\n${_err_split}")
endif()
if(NOT EXISTS "${_split_jpg}")
  message(FATAL_ERROR
    "metatransfer split mode did not write output\nstdout:\n${_out_split}\nstderr:\n${_err_split}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; b=Path(r'''${_split_jpg}''').read_bytes(); import sys; sys.exit(0 if (len(b)>=8 and b[0]==0xFF and b[1]==0xD8 and b[2]==0xFF and b[3]==0xE1) else 1)"
  RESULT_VARIABLE _rv_split_check
  OUTPUT_VARIABLE _out_split_check
  ERROR_VARIABLE _err_split_check
)
if(NOT _rv_split_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer split output does not look like injected APP1 jpeg\nstdout:\n${_out_split}\nstderr:\n${_err_split}\ncheck_stderr:\n${_err_split_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-tiff "${_target_tif}"
          --time-patch "DateTime=2024:12:31 23:59:59"
          --output "${_split_tif}" --force
  RESULT_VARIABLE _rv_split_tif
  OUTPUT_VARIABLE _out_split_tif
  ERROR_VARIABLE _err_split_tif
)
if(NOT _rv_split_tif EQUAL 0)
  message(FATAL_ERROR
    "metatransfer source/target tiff split edit failed (${_rv_split_tif})\nstdout:\n${_out_split_tif}\nstderr:\n${_err_split_tif}")
endif()
if(NOT EXISTS "${_split_tif}")
  message(FATAL_ERROR
    "metatransfer split tiff mode did not write output\nstdout:\n${_out_split_tif}\nstderr:\n${_err_split_tif}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; import sys; b=Path(r'''${_split_tif}''').read_bytes(); ok=(len(b)>=8 and b[0:2]==b'II' and int.from_bytes(b[2:4],'little')==42); off=int.from_bytes(b[4:8],'little') if ok else 0; ok=ok and (off+2<=len(b)); n=int.from_bytes(b[off:off+2],'little') if ok else 0; p=off+2; ok=ok and (p+n*12+4<=len(b)); tags=[int.from_bytes(b[p+i*12:p+i*12+2],'little') for i in range(n)] if ok else []; sys.exit(0 if (ok and 700 in tags and 0x0132 in tags) else 1)"
  RESULT_VARIABLE _rv_split_tif_check
  OUTPUT_VARIABLE _out_split_tif_check
  ERROR_VARIABLE _err_split_tif_check
)
if(NOT _rv_split_tif_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer split tiff output missing expected TIFF tags (700 and 0x0132)\nstdout:\n${_out_split_tif}\nstderr:\n${_err_split_tif}\ncheck_stderr:\n${_err_split_tif_check}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; import sys; b=Path(r'''${_split_tif}''').read_bytes(); ok=(len(b)>=8 and b[0:2]==b'II' and int.from_bytes(b[2:4],'little')==42); off=int.from_bytes(b[4:8],'little') if ok else 0; ok=ok and (off+2<=len(b)); n=int.from_bytes(b[off:off+2],'little') if ok else 0; p=off+2; ok=ok and (p+n*12+4<=len(b)); dt=None; \nfor i in range(n):\n e=p+i*12; tag=int.from_bytes(b[e:e+2],'little'); typ=int.from_bytes(b[e+2:e+4],'little'); cnt=int.from_bytes(b[e+4:e+8],'little'); vo=int.from_bytes(b[e+8:e+12],'little');\n if tag==0x0132 and typ==2 and cnt>0:\n  if cnt<=4: raw=b[e+8:e+8+cnt]\n  else: raw=b[vo:vo+cnt] if vo+cnt<=len(b) else b''\n  dt=raw.split(b'\\x00',1)[0].decode('ascii','ignore')\n  break\nsys.exit(0 if (ok and dt=='2024:12:31 23:59:59') else 1)"
  RESULT_VARIABLE _rv_split_tif_dt_check
  OUTPUT_VARIABLE _out_split_tif_dt_check
  ERROR_VARIABLE _err_split_tif_dt_check
)
if(NOT _rv_split_tif_dt_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer split tiff output missing patched DateTime value\nstdout:\n${_out_split_tif}\nstderr:\n${_err_split_tif}\ncheck_stderr:\n${_err_split_tif_dt_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-dng "${_target_dng}"
          --time-patch "DateTime=2024:12:31 23:59:59"
          --output "${_split_dng}" --force
  RESULT_VARIABLE _rv_split_dng
  OUTPUT_VARIABLE _out_split_dng
  ERROR_VARIABLE _err_split_dng
)
if(NOT _rv_split_dng EQUAL 0)
  message(FATAL_ERROR
    "metatransfer source/target dng split edit failed (${_rv_split_dng})\nstdout:\n${_out_split_dng}\nstderr:\n${_err_split_dng}")
endif()
if(NOT EXISTS "${_split_dng}")
  message(FATAL_ERROR
    "metatransfer split dng mode did not write output\nstdout:\n${_out_split_dng}\nstderr:\n${_err_split_dng}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; import sys; b=Path(r'''${_split_dng}''').read_bytes(); ok=(len(b)>=8 and b[0:2]==b'II' and int.from_bytes(b[2:4],'little')==42); off=int.from_bytes(b[4:8],'little') if ok else 0; ok=ok and (off+2<=len(b)); n=int.from_bytes(b[off:off+2],'little') if ok else 0; p=off+2; ok=ok and (p+n*12+4<=len(b)); tags=[int.from_bytes(b[p+i*12:p+i*12+2],'little') for i in range(n)] if ok else []; dt=None; \nfor i in range(n):\n e=p+i*12; tag=int.from_bytes(b[e:e+2],'little'); typ=int.from_bytes(b[e+2:e+4],'little'); cnt=int.from_bytes(b[e+4:e+8],'little'); vo=int.from_bytes(b[e+8:e+12],'little');\n if tag==0x0132 and typ==2 and cnt>0:\n  if cnt<=4: raw=b[e+8:e+8+cnt]\n  else: raw=b[vo:vo+cnt] if vo+cnt<=len(b) else b''\n  dt=raw.split(b'\\x00',1)[0].decode('ascii','ignore')\n  break\nsys.exit(0 if (ok and 700 in tags and 0x0132 in tags and dt=='2024:12:31 23:59:59') else 1)"
  RESULT_VARIABLE _rv_split_dng_check
  OUTPUT_VARIABLE _out_split_dng_check
  ERROR_VARIABLE _err_split_dng_check
)
if(NOT _rv_split_dng_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer split dng output missing expected TIFF/DNG tags and patched DateTime value\nstdout:\n${_out_split_dng}\nstderr:\n${_err_split_dng}\ncheck_stderr:\n${_err_split_dng_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-tiff "${_target_tif_be}"
          --time-patch "DateTime=2024:12:31 23:59:59"
          --output "${_split_tif_be}" --force
  RESULT_VARIABLE _rv_split_tif_be
  OUTPUT_VARIABLE _out_split_tif_be
  ERROR_VARIABLE _err_split_tif_be
)
if(NOT _rv_split_tif_be EQUAL 0)
  message(FATAL_ERROR
    "metatransfer source/target big-endian tiff split edit failed (${_rv_split_tif_be})\nstdout:\n${_out_split_tif_be}\nstderr:\n${_err_split_tif_be}")
endif()
if(NOT EXISTS "${_split_tif_be}")
  message(FATAL_ERROR
    "metatransfer split big-endian tiff mode did not write output\nstdout:\n${_out_split_tif_be}\nstderr:\n${_err_split_tif_be}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; import sys; b=Path(r'''${_split_tif_be}''').read_bytes(); ok=(len(b)>=8 and b[0:2]==b'MM' and int.from_bytes(b[2:4],'big')==42); off=int.from_bytes(b[4:8],'big') if ok else 0; ok=ok and (off+2<=len(b)); n=int.from_bytes(b[off:off+2],'big') if ok else 0; p=off+2; ok=ok and (p+n*12+4<=len(b)); entries=[(int.from_bytes(b[p+i*12:p+i*12+2],'big'), int.from_bytes(b[p+i*12+2:p+i*12+4],'big'), int.from_bytes(b[p+i*12+4:p+i*12+8],'big'), int.from_bytes(b[p+i*12+8:p+i*12+12],'big')) for i in range(n)] if ok else []; tags=[e[0] for e in entries]; dt=next(((b[p+i*12+8:p+i*12+8+cnt] if cnt<=4 else (b[vo:vo+cnt] if vo+cnt<=len(b) else b'')).split(b'\\x00',1)[0].decode('ascii','ignore') for i,(tag,typ,cnt,vo) in enumerate(entries) if tag==0x0132 and typ==2 and cnt>0), None); sys.exit(0 if (ok and 700 in tags and 0x0132 in tags and dt=='2024:12:31 23:59:59') else 1)"
  RESULT_VARIABLE _rv_split_tif_be_check
  OUTPUT_VARIABLE _out_split_tif_be_check
  ERROR_VARIABLE _err_split_tif_be_check
)
if(NOT _rv_split_tif_be_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer split big-endian tiff output missing expected tags or patched DateTime\nstdout:\n${_out_split_tif_be}\nstderr:\n${_err_split_tif_be}\ncheck_stderr:\n${_err_split_tif_be_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-jxl
          --no-xmp
          --no-icc
          --no-iptc
          "${_jpg}"
  RESULT_VARIABLE _rv_jxl
  OUTPUT_VARIABLE _out_jxl
  ERROR_VARIABLE _err_jxl
)
if(NOT _rv_jxl EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jxl emit summary failed (${_rv_jxl})\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()
if(NOT _out_jxl MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "metatransfer jxl emit summary missing compile ok\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()
if(NOT _out_jxl MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "metatransfer jxl emit summary missing emit ok\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()
if(NOT _out_jxl MATCHES "jxl_box Exif count=1")
  message(FATAL_ERROR
    "metatransfer jxl emit summary missing Exif box summary\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-jxl
          --no-exif
          --no-xmp
          --no-iptc
          "${_icc_jpg}"
  RESULT_VARIABLE _rv_jxl_icc
  OUTPUT_VARIABLE _out_jxl_icc
  ERROR_VARIABLE _err_jxl_icc
)
if(NOT _rv_jxl_icc EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jxl icc summary failed (${_rv_jxl_icc})\nstdout:\n${_out_jxl_icc}\nstderr:\n${_err_jxl_icc}")
endif()
if(NOT _out_jxl_icc MATCHES "jxl_icc_profile bytes=[1-9][0-9]*")
  message(FATAL_ERROR
    "metatransfer jxl icc summary missing encoder icc handoff\nstdout:\n${_out_jxl_icc}\nstderr:\n${_err_jxl_icc}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-jxl
          --no-exif
          --no-xmp
          --no-iptc
          --dump-jxl-encoder-handoff "${_jxl_handoff}"
          "${_icc_jpg}"
  RESULT_VARIABLE _rv_jxl_handoff
  OUTPUT_VARIABLE _out_jxl_handoff
  ERROR_VARIABLE _err_jxl_handoff
)
if(NOT _rv_jxl_handoff EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jxl encoder handoff dump failed (${_rv_jxl_handoff})\nstdout:\n${_out_jxl_handoff}\nstderr:\n${_err_jxl_handoff}")
endif()
if(NOT _out_jxl_handoff MATCHES "jxl_encoder_handoff: status=ok")
  message(FATAL_ERROR
    "metatransfer jxl encoder handoff dump missing status ok\nstdout:\n${_out_jxl_handoff}\nstderr:\n${_err_jxl_handoff}")
endif()
if(NOT EXISTS "${_jxl_handoff}")
  message(FATAL_ERROR
    "metatransfer jxl encoder handoff dump did not write output\nstdout:\n${_out_jxl_handoff}\nstderr:\n${_err_jxl_handoff}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --load-jxl-encoder-handoff "${_jxl_handoff}"
  RESULT_VARIABLE _rv_jxl_handoff_load
  OUTPUT_VARIABLE _out_jxl_handoff_load
  ERROR_VARIABLE _err_jxl_handoff_load
)
if(NOT _rv_jxl_handoff_load EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jxl encoder handoff load failed (${_rv_jxl_handoff_load})\nstdout:\n${_out_jxl_handoff_load}\nstderr:\n${_err_jxl_handoff_load}")
endif()
if(NOT _out_jxl_handoff_load MATCHES "jxl_icc_profile bytes=[1-9][0-9]*")
  message(FATAL_ERROR
    "metatransfer jxl encoder handoff load missing icc summary\nstdout:\n${_out_jxl_handoff_load}\nstderr:\n${_err_jxl_handoff_load}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --load-transfer-artifact "${_jxl_handoff}"
  RESULT_VARIABLE _rv_artifact_jxl_load
  OUTPUT_VARIABLE _out_artifact_jxl_load
  ERROR_VARIABLE _err_artifact_jxl_load
)
if(NOT _rv_artifact_jxl_load EQUAL 0)
  message(FATAL_ERROR
    "metatransfer generic artifact load for jxl handoff failed (${_rv_artifact_jxl_load})\nstdout:\n${_out_artifact_jxl_load}\nstderr:\n${_err_artifact_jxl_load}")
endif()
if(NOT _out_artifact_jxl_load MATCHES "transfer_artifact: status=ok code=none kind=jxl_encoder_handoff bytes=[0-9]+ target=jxl")
  message(FATAL_ERROR
    "metatransfer generic artifact load missing jxl handoff summary\nstdout:\n${_out_artifact_jxl_load}\nstderr:\n${_err_artifact_jxl_load}")
endif()
if(NOT _out_artifact_jxl_load MATCHES "jxl_icc_profile bytes=[1-9][0-9]*")
  message(FATAL_ERROR
    "metatransfer generic artifact load missing jxl icc summary\nstdout:\n${_out_artifact_jxl_load}\nstderr:\n${_err_artifact_jxl_load}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-jxl
          --no-icc
          --source-meta "${_jpg}"
          --output "${_edited_jxl}" --force
          "${_target_jxl}"
  RESULT_VARIABLE _rv_jxl_edit
  OUTPUT_VARIABLE _out_jxl_edit
  ERROR_VARIABLE _err_jxl_edit
)
if(NOT _rv_jxl_edit EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jxl edit failed (${_rv_jxl_edit})\nstdout:\n${_out_jxl_edit}\nstderr:\n${_err_jxl_edit}")
endif()
if(NOT _out_jxl_edit MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "metatransfer jxl edit missing edit apply ok\nstdout:\n${_out_jxl_edit}\nstderr:\n${_err_jxl_edit}")
endif()
if(NOT EXISTS "${_edited_jxl}")
  message(FATAL_ERROR
    "metatransfer jxl edit did not write output\nstdout:\n${_out_jxl_edit}\nstderr:\n${_err_jxl_edit}")
endif()
execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; import sys; b=Path(r'''${_edited_jxl}''').read_bytes(); sys.exit(0 if (len(b)>=12 and b[4:8]==b'JXL ' and b.find(b'Exif')!=-1) else 1)"
  RESULT_VARIABLE _rv_jxl_edit_check
  OUTPUT_VARIABLE _out_jxl_edit_check
  ERROR_VARIABLE _err_jxl_edit_check
)
if(NOT _rv_jxl_edit_check EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jxl edit output check failed\nstdout:\n${_out_jxl_edit}\nstderr:\n${_err_jxl_edit}\ncheck_stderr:\n${_err_jxl_edit_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-jp2
          --no-xmp
          --no-icc
          --no-iptc
          "${_jpg}"
  RESULT_VARIABLE _rv_jp2
  OUTPUT_VARIABLE _out_jp2
  ERROR_VARIABLE _err_jp2
)
if(NOT _rv_jp2 EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jp2 summary failed (${_rv_jp2})\nstdout:\n${_out_jp2}\nstderr:\n${_err_jp2}")
endif()
if(NOT _out_jp2 MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "metatransfer jp2 summary missing compile ok\nstdout:\n${_out_jp2}\nstderr:\n${_err_jp2}")
endif()
if(NOT _out_jp2 MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "metatransfer jp2 summary missing emit ok\nstdout:\n${_out_jp2}\nstderr:\n${_err_jp2}")
endif()
if(NOT _out_jp2 MATCHES "jp2_box Exif count=1")
  message(FATAL_ERROR
    "metatransfer jp2 summary missing Exif box summary\nstdout:\n${_out_jp2}\nstderr:\n${_err_jp2}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-jp2
          --no-xmp
          --no-icc
          --no-iptc
          --output "${_edited_jp2}" --force
          "${_target_jp2}"
  RESULT_VARIABLE _rv_jp2_edit
  OUTPUT_VARIABLE _out_jp2_edit
  ERROR_VARIABLE _err_jp2_edit
)
if(NOT _rv_jp2_edit EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jp2 edit failed (${_rv_jp2_edit})\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}")
endif()
if(NOT _out_jp2_edit MATCHES "jp2_edit: status=ok")
  message(FATAL_ERROR
    "metatransfer jp2 edit missing jp2_edit ok\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}")
endif()
if(NOT _out_jp2_edit MATCHES "jp2_edit_apply: status=ok")
  message(FATAL_ERROR
    "metatransfer jp2 edit missing jp2_edit_apply ok\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}")
endif()
if(NOT EXISTS "${_edited_jp2}")
  message(FATAL_ERROR
    "metatransfer jp2 edit did not write output\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-jp2
          --no-xmp
          --no-icc
          --no-iptc
          "${_edited_jp2}"
  RESULT_VARIABLE _rv_jp2_roundtrip
  OUTPUT_VARIABLE _out_jp2_roundtrip
  ERROR_VARIABLE _err_jp2_roundtrip
)
if(NOT _rv_jp2_roundtrip EQUAL 0)
  message(FATAL_ERROR
    "metatransfer jp2 roundtrip summary failed (${_rv_jp2_roundtrip})\nstdout:\n${_out_jp2_roundtrip}\nstderr:\n${_err_jp2_roundtrip}")
endif()
if(NOT _out_jp2_roundtrip MATCHES "jp2_box Exif count=1")
  message(FATAL_ERROR
    "metatransfer jp2 roundtrip summary missing Exif box summary\nstdout:\n${_out_jp2_roundtrip}\nstderr:\n${_err_jp2_roundtrip}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-webp
          --no-xmp
          --no-icc
          --no-iptc
          "${_jpg}"
  RESULT_VARIABLE _rv_webp
  OUTPUT_VARIABLE _out_webp
  ERROR_VARIABLE _err_webp
)
if(NOT _rv_webp EQUAL 0)
  message(FATAL_ERROR
    "metatransfer webp emit summary failed (${_rv_webp})\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()
if(NOT _out_webp MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "metatransfer webp emit summary missing compile ok\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()
if(NOT _out_webp MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "metatransfer webp emit summary missing emit ok\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()
if(NOT _out_webp MATCHES "webp_chunk EXIF count=1")
  message(FATAL_ERROR
    "metatransfer webp emit summary missing EXIF chunk summary\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-heif
          --no-xmp
          --no-icc
          --no-iptc
          "${_jpg}"
  RESULT_VARIABLE _rv_heif
  OUTPUT_VARIABLE _out_heif
  ERROR_VARIABLE _err_heif
)
if(NOT _rv_heif EQUAL 0)
  message(FATAL_ERROR
    "metatransfer heif emit summary failed (${_rv_heif})\nstdout:\n${_out_heif}\nstderr:\n${_err_heif}")
endif()
if(NOT _out_heif MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "metatransfer heif emit summary missing compile ok\nstdout:\n${_out_heif}\nstderr:\n${_err_heif}")
endif()
if(NOT _out_heif MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "metatransfer heif emit summary missing emit ok\nstdout:\n${_out_heif}\nstderr:\n${_err_heif}")
endif()
if(NOT _out_heif MATCHES "bmff_item Exif count=1")
  message(FATAL_ERROR
    "metatransfer heif emit summary missing Exif item summary\nstdout:\n${_out_heif}\nstderr:\n${_err_heif}")
endif()

set(_heif_target "${WORK_DIR}/heif_target.bin")
execute_process(
  COMMAND "${_openmeta_test_python}" -c "from pathlib import Path; Path(r'${_heif_target}').write_bytes(bytes.fromhex('000000186674797068656963000000006d696631686569630000000c6d64617411223344'))"
  RESULT_VARIABLE _rv_heif_target
  OUTPUT_VARIABLE _out_heif_target
  ERROR_VARIABLE _err_heif_target
)
if(NOT _rv_heif_target EQUAL 0)
  message(FATAL_ERROR
    "failed to create HEIF target file (${_rv_heif_target})\nstdout:\n${_out_heif_target}\nstderr:\n${_err_heif_target}")
endif()

set(_heif_out "${WORK_DIR}/metatransfer_heif_edit.bin")
execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg}"
          --target-heif
          --no-xmp
          --no-icc
          --no-iptc
          --output "${_heif_out}" --force
          "${_heif_target}"
  RESULT_VARIABLE _rv_heif_edit
  OUTPUT_VARIABLE _out_heif_edit
  ERROR_VARIABLE _err_heif_edit
)
if(NOT _rv_heif_edit EQUAL 0)
  message(FATAL_ERROR
    "metatransfer heif edit failed (${_rv_heif_edit})\nstdout:\n${_out_heif_edit}\nstderr:\n${_err_heif_edit}")
endif()
if(NOT _out_heif_edit MATCHES "bmff_edit: status=ok")
  message(FATAL_ERROR
    "metatransfer heif edit missing bmff_edit ok\nstdout:\n${_out_heif_edit}\nstderr:\n${_err_heif_edit}")
endif()
if(NOT _out_heif_edit MATCHES "bmff_edit_apply: status=ok")
  message(FATAL_ERROR
    "metatransfer heif edit missing bmff_edit_apply ok\nstdout:\n${_out_heif_edit}\nstderr:\n${_err_heif_edit}")
endif()
if(NOT EXISTS "${_heif_out}")
  message(FATAL_ERROR
    "metatransfer heif edit did not write output\nstdout:\n${_out_heif_edit}\nstderr:\n${_err_heif_edit}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-heif
          --no-xmp
          --no-icc
          --no-iptc
          "${_heif_out}"
  RESULT_VARIABLE _rv_heif_roundtrip
  OUTPUT_VARIABLE _out_heif_roundtrip
  ERROR_VARIABLE _err_heif_roundtrip
)
if(NOT _rv_heif_roundtrip EQUAL 0)
  message(FATAL_ERROR
    "metatransfer heif roundtrip summary failed (${_rv_heif_roundtrip})\nstdout:\n${_out_heif_roundtrip}\nstderr:\n${_err_heif_roundtrip}")
endif()
if(NOT _out_heif_roundtrip MATCHES "bmff_item Exif count=1")
  message(FATAL_ERROR
    "metatransfer heif roundtrip summary missing Exif item summary\nstdout:\n${_out_heif_roundtrip}\nstderr:\n${_err_heif_roundtrip}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --target-heif
          --no-exif
          --no-xmp
          --no-iptc
          "${_icc_jpg}"
  RESULT_VARIABLE _rv_heif_icc
  OUTPUT_VARIABLE _out_heif_icc
  ERROR_VARIABLE _err_heif_icc
)
if(NOT _rv_heif_icc EQUAL 0)
  message(FATAL_ERROR
    "metatransfer heif icc summary failed (${_rv_heif_icc})\nstdout:\n${_out_heif_icc}\nstderr:\n${_err_heif_icc}")
endif()
if(NOT _out_heif_icc MATCHES "bmff_property colr/prof count=1")
  message(FATAL_ERROR
    "metatransfer heif icc summary missing colr/prof property\nstdout:\n${_out_heif_icc}\nstderr:\n${_err_heif_icc}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg_rich}"
          --target-tiff "${_target_tif}"
          --time-patch "DateTime=2024:12:31 23:59:59"
          --time-patch "DateTimeOriginal=2024:12:31 23:59:59"
          --output "${_split_tif_rich}" --force
  RESULT_VARIABLE _rv_split_tif_rich
  OUTPUT_VARIABLE _out_split_tif_rich
  ERROR_VARIABLE _err_split_tif_rich
)
if(NOT _rv_split_tif_rich EQUAL 0)
  message(FATAL_ERROR
    "metatransfer rich split little-endian tiff edit failed (${_rv_split_tif_rich})\nstdout:\n${_out_split_tif_rich}\nstderr:\n${_err_split_tif_rich}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" "${_rich_checker_py}" "${_split_tif_rich}" "2024:12:31 23:59:59" "2024:12:31 23:59:59"
  RESULT_VARIABLE _rv_split_tif_rich_check
  OUTPUT_VARIABLE _out_split_tif_rich_check
  ERROR_VARIABLE _err_split_tif_rich_check
)
if(NOT _rv_split_tif_rich_check EQUAL 0)
  message(FATAL_ERROR
    "rich little-endian tiff transfer validation failed\nstdout:\n${_out_split_tif_rich}\nstderr:\n${_err_split_tif_rich}\ncheck_stderr:\n${_err_split_tif_rich_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info
          --source-meta "${_jpg_rich}"
          --target-tiff "${_target_tif_be}"
          --time-patch "DateTime=2024:12:31 23:59:59"
          --time-patch "DateTimeOriginal=2024:12:31 23:59:59"
          --output "${_split_tif_be_rich}" --force
  RESULT_VARIABLE _rv_split_tif_be_rich
  OUTPUT_VARIABLE _out_split_tif_be_rich
  ERROR_VARIABLE _err_split_tif_be_rich
)
if(NOT _rv_split_tif_be_rich EQUAL 0)
  message(FATAL_ERROR
    "metatransfer rich split big-endian tiff edit failed (${_rv_split_tif_be_rich})\nstdout:\n${_out_split_tif_be_rich}\nstderr:\n${_err_split_tif_be_rich}")
endif()

execute_process(
  COMMAND "${_openmeta_test_python}" "${_rich_checker_py}" "${_split_tif_be_rich}" "2024:12:31 23:59:59" "2024:12:31 23:59:59"
  RESULT_VARIABLE _rv_split_tif_be_rich_check
  OUTPUT_VARIABLE _out_split_tif_be_rich_check
  ERROR_VARIABLE _err_split_tif_be_rich_check
)
if(NOT _rv_split_tif_be_rich_check EQUAL 0)
  message(FATAL_ERROR
    "rich big-endian tiff transfer validation failed\nstdout:\n${_out_split_tif_be_rich}\nstderr:\n${_err_split_tif_be_rich}\ncheck_stderr:\n${_err_split_tif_be_rich_check}")
endif()

execute_process(
  COMMAND "${METATRANSFER_BIN}" --no-build-info --unsafe-write-payloads --out-dir "${_dump_dir}" --force "${_jpg}"
  RESULT_VARIABLE _rv_dump
  OUTPUT_VARIABLE _out_dump
  ERROR_VARIABLE _err_dump
)
if(NOT _rv_dump EQUAL 0)
  message(FATAL_ERROR
    "metatransfer payload dump failed (${_rv_dump})\nstdout:\n${_out_dump}\nstderr:\n${_err_dump}")
endif()

file(GLOB _dump_files "${_dump_dir}/*.bin")
list(LENGTH _dump_files _dump_count)
if(_dump_count LESS 1)
  message(FATAL_ERROR
    "metatransfer did not write payload dumps\nstdout:\n${_out_dump}\nstderr:\n${_err_dump}")
endif()

message(STATUS "metatransfer smoke gate passed")
