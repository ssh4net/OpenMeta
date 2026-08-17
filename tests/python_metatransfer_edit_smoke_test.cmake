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
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_python_metatransfer_edit_smoke")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_src_jpg "${WORK_DIR}/source.jpg")
set(_src_jpg_xmp "${WORK_DIR}/source_xmp.jpg")
set(_src_jpg_target_embedded_xmp "${WORK_DIR}/source_target_embedded_xmp.jpg")
set(_src_icc_jpg "${WORK_DIR}/source_icc.jpg")
set(_src_rendered_safety_jpg "${WORK_DIR}/source_rendered_safety.jpg")
set(_target_jpg "${WORK_DIR}/target.jpg")
set(_target_jpg_xmp "${WORK_DIR}/target_xmp.jpg")
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
set(_target_tif "${WORK_DIR}/target.tif")
set(_target_dng "${WORK_DIR}/target.dng")
set(_target_tif_xmp "${WORK_DIR}/target_xmp.tif")
set(_edited_tif "${WORK_DIR}/edited.tif")
set(_edited_dng "${WORK_DIR}/edited.dng")
set(_sidecar_only_strip_tif "${WORK_DIR}/sidecar_only_strip_tiff.tif")
set(_sidecar_only_strip_tif_sidecar "${WORK_DIR}/sidecar_only_strip_tiff.xmp")
set(_target_jxl "${WORK_DIR}/target.jxl")
set(_edited_jxl "${WORK_DIR}/edited.jxl")
set(_target_png "${WORK_DIR}/target.png")
set(_edited_png "${WORK_DIR}/edited.png")
set(_target_jp2 "${WORK_DIR}/target.jp2")
set(_edited_jp2 "${WORK_DIR}/edited.jp2")
set(_target_webp "${WORK_DIR}/target.webp")
set(_edited_webp "${WORK_DIR}/edited.webp")
set(_target_heif_xmp "${WORK_DIR}/target_xmp.heif")
set(_target_heif_existing_xmp "${WORK_DIR}/target_existing_xmp.heif")
set(_edited_heif_xmp "${WORK_DIR}/heif_xmp_edit.heif")
set(_edited_heif_xmp_sidecar "${WORK_DIR}/heif_xmp_edit.xmp")
set(_edited_heif_xmp_embedded "${WORK_DIR}/heif_xmp_embedded.heif")
set(_edited_heif_xmp_embedded_sidecar "${WORK_DIR}/heif_xmp_embedded.xmp")
set(_mixed_heif_sidecar_wins "${WORK_DIR}/heif_mixed_sidecar_wins.heif")
set(_mixed_heif_embedded_wins "${WORK_DIR}/heif_mixed_embedded_wins.heif")
set(_target_avif_xmp "${WORK_DIR}/target_xmp.avif")
set(_target_avif_existing_xmp "${WORK_DIR}/target_existing_xmp.avif")
set(_edited_avif_xmp "${WORK_DIR}/avif_xmp_edit.avif")
set(_edited_avif_xmp_sidecar "${WORK_DIR}/avif_xmp_edit.xmp")
set(_edited_avif_xmp_embedded "${WORK_DIR}/avif_xmp_embedded.avif")
set(_edited_avif_xmp_embedded_sidecar "${WORK_DIR}/avif_xmp_embedded.xmp")
set(_mixed_avif_sidecar_wins "${WORK_DIR}/avif_mixed_sidecar_wins.avif")
set(_mixed_avif_embedded_wins "${WORK_DIR}/avif_mixed_embedded_wins.avif")
set(_target_cr3_xmp "${WORK_DIR}/target_xmp.cr3")
set(_target_cr3_existing_xmp "${WORK_DIR}/target_existing_xmp.cr3")
set(_edited_cr3_xmp "${WORK_DIR}/cr3_xmp_edit.cr3")
set(_edited_cr3_xmp_sidecar "${WORK_DIR}/cr3_xmp_edit.xmp")
set(_edited_cr3_xmp_embedded "${WORK_DIR}/cr3_xmp_embedded.cr3")
set(_edited_cr3_xmp_embedded_sidecar "${WORK_DIR}/cr3_xmp_embedded.xmp")
set(_mixed_cr3_sidecar_wins "${WORK_DIR}/cr3_mixed_sidecar_wins.cr3")
set(_mixed_cr3_embedded_wins "${WORK_DIR}/cr3_mixed_embedded_wins.cr3")
set(_jxl_handoff "${WORK_DIR}/jxl_encoder_handoff.omjxic")
set(_c2pa_jpg "${WORK_DIR}/sample_c2pa.jpg")
set(_c2pa_jxl "${WORK_DIR}/sample_c2pa.jxl")
set(_c2pa_heif "${WORK_DIR}/sample_c2pa.heif")
set(_c2pa_signed_jumb "${WORK_DIR}/signed_c2pa.jumb")
set(_c2pa_manifest "${WORK_DIR}/manifest.cbor")
set(_c2pa_cert "${WORK_DIR}/cert.der")
set(_c2pa_handoff "${WORK_DIR}/handoff.omc2ph")
set(_c2pa_heif_handoff "${WORK_DIR}/heif_handoff.omc2ph")
set(_c2pa_signed_package "${WORK_DIR}/signed_package.omc2ps")
set(_c2pa_jxl_signed_package "${WORK_DIR}/jxl_signed_package.omc2ps")
set(_c2pa_heif_signed_package "${WORK_DIR}/heif_signed_package.omc2ps")
set(_c2pa_jxl_binding "${WORK_DIR}/jxl_binding.bin")
set(_c2pa_heif_binding "${WORK_DIR}/heif_binding.bin")
set(_transfer_payload_batch "${WORK_DIR}/payload_batch.omtpld")
set(_transfer_package_batch "${WORK_DIR}/package_batch.omtpkg")
set(_c2pa_from_package "${WORK_DIR}/edited_from_package.jpg")
set(_c2pa_jxl_out "${WORK_DIR}/edited_from_package.jxl")
set(_c2pa_heif_out "${WORK_DIR}/edited_from_package.heif")
set(_c2pa_heif_from_package "${WORK_DIR}/edited_from_signed_package.heif")
set(_check_tiff_py "${WORK_DIR}/check_tiff.py")
set(_check_readback_py "${WORK_DIR}/check_readback.py")
set(_rendered_safety_builder_py "${WORK_DIR}/build_rendered_safety_fixture.py")

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; t=bytearray(); t+=b'II*\\x00'; t+=(8).to_bytes(4,'little'); t+=(1).to_bytes(2,'little'); t+=(0x0132).to_bytes(2,'little'); t+=(2).to_bytes(2,'little'); t+=(20).to_bytes(4,'little'); t+=(26).to_bytes(4,'little'); t+=(0).to_bytes(4,'little'); t+=b'2000:01:02 03:04:05\\x00'; app1=b'Exif\\x00\\x00'+bytes(t); ln=(len(app1)+2).to_bytes(2,'big'); Path(r'''${_src_jpg}''').write_bytes(b'\\xFF\\xD8\\xFF\\xE1'+ln+app1+b'\\xFF\\xD9')"
  RESULT_VARIABLE _rv_src
  OUTPUT_VARIABLE _out_src
  ERROR_VARIABLE _err_src
)
if(NOT _rv_src EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer source fixture (${_rv_src})\nstdout:\n${_out_src}\nstderr:\n${_err_src}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; t=bytearray(); t+=b'II*\\x00'; t+=(8).to_bytes(4,'little'); t+=(1).to_bytes(2,'little'); t+=(0x0132).to_bytes(2,'little'); t+=(2).to_bytes(2,'little'); t+=(20).to_bytes(4,'little'); t+=(26).to_bytes(4,'little'); t+=(0).to_bytes(4,'little'); t+=b'2000:01:02 03:04:05\\x00'; app1=b'Exif\\x00\\x00'+bytes(t); xml=b\"<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'><xmp:CreatorTool>OpenMeta Transfer Source</xmp:CreatorTool></rdf:Description></rdf:RDF></x:xmpmeta>\"; xmp=b'http://ns.adobe.com/xap/1.0/\\x00'+xml; ln1=(len(app1)+2).to_bytes(2,'big'); ln2=(len(xmp)+2).to_bytes(2,'big'); Path(r'''${_src_jpg_xmp}''').write_bytes(b'\\xFF\\xD8\\xFF\\xE1'+ln1+app1+b'\\xFF\\xE1'+ln2+xmp+b'\\xFF\\xD9')"
  RESULT_VARIABLE _rv_src_xmp
  OUTPUT_VARIABLE _out_src_xmp
  ERROR_VARIABLE _err_src_xmp
)
if(NOT _rv_src_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer xmp source fixture (${_rv_src_xmp})\nstdout:\n${_out_src_xmp}\nstderr:\n${_err_src_xmp}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; xml=b\"<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'><xmp:CreatorTool>Target Embedded Existing</xmp:CreatorTool></rdf:Description></rdf:RDF></x:xmpmeta>\"; xmp=b'http://ns.adobe.com/xap/1.0/\\x00'+xml; ln=(len(xmp)+2).to_bytes(2,'big'); Path(r'''${_src_jpg_target_embedded_xmp}''').write_bytes(b'\\xFF\\xD8\\xFF\\xE1'+ln+xmp+b'\\xFF\\xD9')"
  RESULT_VARIABLE _rv_src_target_embedded_xmp
  OUTPUT_VARIABLE _out_src_target_embedded_xmp
  ERROR_VARIABLE _err_src_target_embedded_xmp
)
if(NOT _rv_src_target_embedded_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target-embedded xmp source fixture (${_rv_src_target_embedded_xmp})\nstdout:\n${_out_src_target_embedded_xmp}\nstderr:\n${_err_src_target_embedded_xmp}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; p=bytearray(156); p[0:4]=(156).to_bytes(4,'big'); p[36:40]=b'acsp'; p[128:132]=(1).to_bytes(4,'big'); p[132:136]=b'desc'; p[136:140]=(144).to_bytes(4,'big'); p[140:144]=(12).to_bytes(4,'big'); p[144:156]=bytes([0x11])*12; app2=b'ICC_PROFILE\\x00\\x01\\x01'+bytes(p); ln=(len(app2)+2).to_bytes(2,'big'); Path(r'''${_src_icc_jpg}''').write_bytes(b'\\xFF\\xD8\\xFF\\xE2'+ln+app2+b'\\xFF\\xD9')"
  RESULT_VARIABLE _rv_src_icc
  OUTPUT_VARIABLE _out_src_icc
  ERROR_VARIABLE _err_src_icc
)
if(NOT _rv_src_icc EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer icc source fixture (${_rv_src_icc})\nstdout:\n${_out_src_icc}\nstderr:\n${_err_src_icc}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; Path(r'''${_target_jpg}''').write_bytes(bytes([255,216,255,217]))"
  RESULT_VARIABLE _rv_target_jpg
  OUTPUT_VARIABLE _out_target_jpg
  ERROR_VARIABLE _err_target_jpg
)
if(NOT _rv_target_jpg EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target jpeg fixture (${_rv_target_jpg})\nstdout:\n${_out_target_jpg}\nstderr:\n${_err_target_jpg}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; xml=b\"<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'><xmp:CreatorTool>Target Embedded Existing</xmp:CreatorTool></rdf:Description></rdf:RDF></x:xmpmeta>\"; app1=b'http://ns.adobe.com/xap/1.0/\\x00'+xml; ln=(len(app1)+2).to_bytes(2,'big'); Path(r'''${_target_jpg_xmp}''').write_bytes(b'\\xFF\\xD8\\xFF\\xE1'+ln+app1+b'\\xFF\\xD9')"
  RESULT_VARIABLE _rv_target_jpg_xmp
  OUTPUT_VARIABLE _out_target_jpg_xmp
  ERROR_VARIABLE _err_target_jpg_xmp
)
if(NOT _rv_target_jpg_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target jpeg xmp fixture (${_rv_target_jpg_xmp})\nstdout:\n${_out_target_jpg_xmp}\nstderr:\n${_err_target_jpg_xmp}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; b=bytearray(); b+=b'II'; b+=(42).to_bytes(2,'little'); b+=(8).to_bytes(4,'little'); b+=(0).to_bytes(2,'little'); b+=(0).to_bytes(4,'little'); Path(r'''${_target_tif}''').write_bytes(bytes(b))"
  RESULT_VARIABLE _rv_target_tif
  OUTPUT_VARIABLE _out_target_tif
  ERROR_VARIABLE _err_target_tif
)
if(NOT _rv_target_tif EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target tiff fixture (${_rv_target_tif})\nstdout:\n${_out_target_tif}\nstderr:\n${_err_target_tif}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; b=bytearray(); b+=b'II'; b+=(42).to_bytes(2,'little'); b+=(8).to_bytes(4,'little'); b+=(0).to_bytes(2,'little'); b+=(0).to_bytes(4,'little'); Path(r'''${_target_dng}''').write_bytes(bytes(b))"
  RESULT_VARIABLE _rv_target_dng
  OUTPUT_VARIABLE _out_target_dng
  ERROR_VARIABLE _err_target_dng
)
if(NOT _rv_target_dng EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target dng fixture (${_rv_target_dng})\nstdout:\n${_out_target_dng}\nstderr:\n${_err_target_dng}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; xml=b\"<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'><xmp:CreatorTool>Target Embedded Existing</xmp:CreatorTool></rdf:Description></rdf:RDF></x:xmpmeta>\"; xoff=38; ifd=bytearray(); ifd+=(1).to_bytes(2,'little'); ifd+=(700).to_bytes(2,'little'); ifd+=(1).to_bytes(2,'little'); ifd+=len(xml).to_bytes(4,'little'); ifd+=xoff.to_bytes(4,'little'); ifd+=(0).to_bytes(4,'little'); b=bytearray(); b+=b'II'; b+=(42).to_bytes(2,'little'); b+=(8).to_bytes(4,'little'); b+=ifd; b+=xml; Path(r'''${_target_tif_xmp}''').write_bytes(bytes(b))"
  RESULT_VARIABLE _rv_target_tif_xmp
  OUTPUT_VARIABLE _out_target_tif_xmp
  ERROR_VARIABLE _err_target_tif_xmp
)
if(NOT _rv_target_tif_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target tiff xmp fixture (${_rv_target_tif_xmp})\nstdout:\n${_out_target_tif_xmp}\nstderr:\n${_err_target_tif_xmp}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; u32=lambda v:(v).to_bytes(4,'big'); box=lambda t,p:u32(8+len(p))+t+p; Path(r'''${_target_jxl}''').write_bytes(u32(12)+b'JXL '+u32(0x0D0A870A)+box(b'jxlc', bytes([0x11,0x22,0x33,0x44])))"
  RESULT_VARIABLE _rv_target_jxl
  OUTPUT_VARIABLE _out_target_jxl
  ERROR_VARIABLE _err_target_jxl
)
if(NOT _rv_target_jxl EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target jxl fixture (${_rv_target_jxl})\nstdout:\n${_out_target_jxl}\nstderr:\n${_err_target_jxl}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; sig=bytes.fromhex('89504e470d0a1a0a'); ihdr=(13).to_bytes(4,'big')+b'IHDR'+(1).to_bytes(4,'big')+(1).to_bytes(4,'big')+bytes([8,2,0,0,0])+(0).to_bytes(4,'big'); iend=(0).to_bytes(4,'big')+b'IEND'+(0).to_bytes(4,'big'); Path(r'''${_target_png}''').write_bytes(sig+ihdr+iend)"
  RESULT_VARIABLE _rv_target_png
  OUTPUT_VARIABLE _out_target_png
  ERROR_VARIABLE _err_target_png
)
if(NOT _rv_target_png EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target png fixture (${_rv_target_png})\nstdout:\n${_out_target_png}\nstderr:\n${_err_target_png}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; u32=lambda v:(v).to_bytes(4,'big'); box=lambda t,p:u32(8+len(p))+t+p; ftyp=b'jp2 '+u32(0)+b'jp2 '; sig=u32(12)+b'jP  '+u32(0x0D0A870A); Path(r'''${_target_jp2}''').write_bytes(sig+box(b'ftyp', ftyp)+box(b'free', bytes([0x11,0x22,0x33])))"
  RESULT_VARIABLE _rv_target_jp2
  OUTPUT_VARIABLE _out_target_jp2
  ERROR_VARIABLE _err_target_jp2
)
if(NOT _rv_target_jp2 EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target jp2 fixture (${_rv_target_jp2})\nstdout:\n${_out_target_jp2}\nstderr:\n${_err_target_jp2}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; riff_size=(32).to_bytes(4,'little'); vp8x=b'VP8X'+(10).to_bytes(4,'little')+bytes([0,0,0,0,0,0,0,0,0,0]); vp8=b'VP8 '+(1).to_bytes(4,'little')+b'\\x00\\x00'; Path(r'''${_target_webp}''').write_bytes(b'RIFF'+riff_size+b'WEBP'+vp8x+vp8)"
  RESULT_VARIABLE _rv_target_webp
  OUTPUT_VARIABLE _out_target_webp
  ERROR_VARIABLE _err_target_webp
)
if(NOT _rv_target_webp EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer target webp fixture (${_rv_target_webp})\nstdout:\n${_out_target_webp}\nstderr:\n${_err_target_webp}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; Path(r'''${_target_heif_xmp}''').write_bytes(bytes.fromhex('000000186674797068656963000000006d696631686569630000000c6d64617411223344')); Path(r'''${_target_avif_xmp}''').write_bytes(bytes.fromhex('000000186674797061766966000000006d696631617669660000000c6d64617411223344')); Path(r'''${_target_cr3_xmp}''').write_bytes(bytes.fromhex('0000001866747970637278200000000063727820435233200000000c6d64617411223344'))"
  RESULT_VARIABLE _rv_target_bmff_xmp
  OUTPUT_VARIABLE _out_target_bmff_xmp
  ERROR_VARIABLE _err_target_bmff_xmp
)
if(NOT _rv_target_bmff_xmp EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer bmff xmp target fixtures (${_rv_target_bmff_xmp})\nstdout:\n${_out_target_bmff_xmp}\nstderr:\n${_err_target_bmff_xmp}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; jumd=b'c2pa\\x00'; box=lambda t,p:(8+len(p)).to_bytes(4,'big')+t+p; cbor=bytes([0xA1,0x61,0x61,0x01]); seg_jumb=box(b'jumb', box(b'jumd', jumd)+box(b'cbor', cbor)); seg=b'JP\\x00\\x00'+(1).to_bytes(4,'big')+seg_jumb; signed_cbor=bytearray(); signed_cbor+=bytes([0xA1,0x68])+b'manifest'; signed_cbor+=bytes([0x81,0xA2,0x6F])+b'claim_generator'; signed_cbor+=bytes([0x64])+b'test'; signed_cbor+=bytes([0x66])+b'claims'; signed_cbor+=bytes([0x81,0xA2,0x6A])+b'assertions'; signed_cbor+=bytes([0x81,0xA1,0x65])+b'label'; signed_cbor+=bytes([0x6E])+b'c2pa.hash.data'; signed_cbor+=bytes([0x6A])+b'signatures'; signed_cbor+=bytes([0x81,0xA2,0x63])+b'alg'; signed_cbor+=bytes([0x65])+b'ES256'; signed_cbor+=bytes([0x69])+b'signature'; signed_cbor+=bytes([0x44,0x01,0x02,0x03,0x04]); signed_jumb=box(b'jumb', box(b'jumd', jumd)+box(b'cbor', bytes(signed_cbor))); Path(r'''${_c2pa_jpg}''').write_bytes(b'\\xFF\\xD8\\xFF\\xEB'+(len(seg)+2).to_bytes(2,'big')+seg+b'\\xFF\\xD9'); Path(r'''${_c2pa_signed_jumb}''').write_bytes(signed_jumb); Path(r'''${_c2pa_manifest}''').write_bytes(bytes(signed_cbor)); Path(r'''${_c2pa_cert}''').write_bytes(bytes([0x30,0x82,0x01,0x00]))"
  RESULT_VARIABLE _rv_c2pa
  OUTPUT_VARIABLE _out_c2pa
  ERROR_VARIABLE _err_c2pa
)
if(NOT _rv_c2pa EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer c2pa fixtures (${_rv_c2pa})\nstdout:\n${_out_c2pa}\nstderr:\n${_err_c2pa}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; u32=lambda v:(v).to_bytes(4,'big'); box=lambda t,p:u32(8+len(p))+t+p; cbor=bytes([0xA1,0x61,0x61,0x01]); jumd=b'c2pa\\x00'; logical=box(b'jumb', box(b'jumd', jumd)+box(b'cbor', cbor)); Path(r'''${_c2pa_jxl}''').write_bytes(u32(12)+b'JXL '+u32(0x0D0A870A)+box(b'jxlc', bytes([0x11,0x22,0x33,0x44]))+box(b'jumb', logical[8:]))"
  RESULT_VARIABLE _rv_c2pa_jxl
  OUTPUT_VARIABLE _out_c2pa_jxl
  ERROR_VARIABLE _err_c2pa_jxl
)
if(NOT _rv_c2pa_jxl EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer jxl c2pa fixture (${_rv_c2pa_jxl})\nstdout:\n${_out_c2pa_jxl}\nstderr:\n${_err_c2pa_jxl}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; u32=lambda v:(v).to_bytes(4,'big'); box=lambda t,p:u32(8+len(p))+t+p; uuidbox=lambda u,p:u32(24+len(p))+b'uuid'+u+p; logical=Path(r'''${_c2pa_signed_jumb}''').read_bytes(); marker=b'openmeta:bmff_transfer_meta:v1'; uuid=b'OpenMetaBmffMeta'; infe=b'\\x02\\x00\\x00\\x00'+(1).to_bytes(2,'big')+(0).to_bytes(2,'big')+b'c2pa'+b'C2PA\\x00'; iinf=box(b'iinf', b'\\x00\\x00\\x00\\x00'+(1).to_bytes(2,'big')+box(b'infe', infe)); idat=box(b'idat', logical); iloc=box(b'iloc', b'\\x01\\x00\\x00\\x00'+bytes([0x44,0x40])+(1).to_bytes(2,'big')+(1).to_bytes(2,'big')+(1).to_bytes(2,'big')+(0).to_bytes(2,'big')+(0).to_bytes(4,'big')+(1).to_bytes(2,'big')+(0).to_bytes(4,'big')+len(logical).to_bytes(4,'big')); meta=box(b'meta', b'\\x00\\x00\\x00\\x00'+uuidbox(uuid, marker)+iinf+idat+iloc); ftyp=box(b'ftyp', b'heic'+u32(0)+b'mif1heic'); mdat=box(b'mdat', bytes([0x11,0x22,0x33,0x44])); Path(r'''${_c2pa_heif}''').write_bytes(ftyp+mdat+meta)"
  RESULT_VARIABLE _rv_c2pa_heif
  OUTPUT_VARIABLE _out_c2pa_heif
  ERROR_VARIABLE _err_c2pa_heif
)
if(NOT _rv_c2pa_heif EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer heif c2pa fixture (${_rv_c2pa_heif})\nstdout:\n${_out_c2pa_heif}\nstderr:\n${_err_c2pa_heif}")
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
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" "${_rendered_safety_builder_py}" "${_src_rendered_safety_jpg}"
  RESULT_VARIABLE _rv_rendered_safety_src
  OUTPUT_VARIABLE _out_rendered_safety_src
  ERROR_VARIABLE _err_rendered_safety_src
)
if(NOT _rv_rendered_safety_src EQUAL 0)
  message(FATAL_ERROR
    "failed to write python metatransfer rendered-safety fixture (${_rv_rendered_safety_src})\nstdout:\n${_out_rendered_safety_src}\nstderr:\n${_err_rendered_safety_src}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --time-patch "DateTime=2024:12:31 23:59:59"
          --target-jpeg "${_target_jpg}"
          -o "${_edited_jpg}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_jpg
  OUTPUT_VARIABLE _out_jpg
  ERROR_VARIABLE _err_jpg
)
if(NOT _rv_jpg EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jpeg edit failed (${_rv_jpg})\nstdout:\n${_out_jpg}\nstderr:\n${_err_jpg}")
endif()
if(NOT _out_jpg MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "python metatransfer jpeg edit missing plan ok\nstdout:\n${_out_jpg}\nstderr:\n${_err_jpg}")
endif()
if(NOT _out_jpg MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer jpeg edit missing apply ok\nstdout:\n${_out_jpg}\nstderr:\n${_err_jpg}")
endif()
if(NOT EXISTS "${_edited_jpg}")
  message(FATAL_ERROR
    "python metatransfer jpeg edit did not write output\nstdout:\n${_out_jpg}\nstderr:\n${_err_jpg}")
endif()

execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; b=Path(r'''${_edited_jpg}''').read_bytes(); assert b[:2]==b'\\xff\\xd8'; assert b.find(b'Exif\\x00\\x00')!=-1; assert b.find(b'2024:12:31 23:59:59\\x00')!=-1"
  RESULT_VARIABLE _rv_jpg_check
  OUTPUT_VARIABLE _out_jpg_check
  ERROR_VARIABLE _err_jpg_check
)
if(NOT _rv_jpg_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jpeg output check failed (${_rv_jpg_check})\nstdout:\n${_out_jpg_check}\nstderr:\n${_err_jpg_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --xmp-include-existing
          --transfer-safety rendered
          "${_src_rendered_safety_jpg}"
  RESULT_VARIABLE _rv_rendered_safety
  OUTPUT_VARIABLE _out_rendered_safety
  ERROR_VARIABLE _err_rendered_safety
)
if(NOT _rv_rendered_safety EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer rendered safety probe failed (${_rv_rendered_safety})\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "prepare: status=ok")
  message(FATAL_ERROR
    "python metatransfer rendered safety missing prepare ok\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "policy\\[image_properties\\]: requested=keep effective=drop reason=target_image_properties matched=[1-9][0-9]*")
  message(FATAL_ERROR
    "python metatransfer rendered safety missing image-properties policy\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "policy\\[icc_profile\\]: requested=keep effective=drop reason=safety_mode_filtered matched=[1-9][0-9]*")
  message(FATAL_ERROR
    "python metatransfer rendered safety missing ICC policy\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "policy\\[raw_color_calibration\\]: requested=keep effective=drop reason=safety_mode_filtered matched=[1-9][0-9]*")
  message(FATAL_ERROR
    "python metatransfer rendered safety missing raw color/correction policy\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "policy\\[camera_raw_settings\\]: requested=keep effective=drop reason=safety_mode_filtered matched=[1-9][0-9]*")
  message(FATAL_ERROR
    "python metatransfer rendered safety missing camera raw settings policy\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "policy\\[makernote\\]: requested=keep effective=drop reason=safety_mode_filtered matched=[1-9][0-9]*")
  message(FATAL_ERROR
    "python metatransfer rendered safety missing MakerNote policy\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()
if(NOT _out_rendered_safety MATCHES "policy\\[jumbf\\]: requested=keep effective=drop reason=safety_mode_filtered matched=[1-9][0-9]*")
  message(FATAL_ERROR
    "python metatransfer rendered safety missing JUMBF policy\nstdout:\n${_out_rendered_safety}\nstderr:\n${_err_rendered_safety}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
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
          -o "${_target_spec_jpg}" --force
          "${_src_jpg}"
  RESULT_VARIABLE _rv_target_spec
  OUTPUT_VARIABLE _out_target_spec
  ERROR_VARIABLE _err_target_spec
)
if(NOT _rv_target_spec EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer target-spec jpeg edit failed (${_rv_target_spec})\nstdout:\n${_out_target_spec}\nstderr:\n${_err_target_spec}")
endif()
if(NOT EXISTS "${_target_spec_jpg}")
  message(FATAL_ERROR
    "python metatransfer target-spec jpeg did not write output\nstdout:\n${_out_target_spec}\nstderr:\n${_err_target_spec}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --source-meta "${_src_jpg}"
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
          -o "${_target_spec_tif}" --force
          "${_target_tif}"
  RESULT_VARIABLE _rv_target_spec_tif
  OUTPUT_VARIABLE _out_target_spec_tif
  ERROR_VARIABLE _err_target_spec_tif
)
if(NOT _rv_target_spec_tif EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer target-spec tiff edit failed (${_rv_target_spec_tif})\nstdout:\n${_out_target_spec_tif}\nstderr:\n${_err_target_spec_tif}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --source-meta "${_src_jpg}"
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
          -o "${_target_spec_dng}" --force
          "${_target_dng}"
  RESULT_VARIABLE _rv_target_spec_dng
  OUTPUT_VARIABLE _out_target_spec_dng
  ERROR_VARIABLE _err_target_spec_dng
)
if(NOT _rv_target_spec_dng EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer target-spec dng edit failed (${_rv_target_spec_dng})\nstdout:\n${_out_target_spec_dng}\nstderr:\n${_err_target_spec_dng}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jpeg "${_target_jpg}"
          --xmp-writeback embedded_and_sidecar
          -o "${_dual_jpg}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_dual
  OUTPUT_VARIABLE _out_dual
  ERROR_VARIABLE _err_dual
)
if(NOT _rv_dual EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer dual-write jpeg edit failed (${_rv_dual})\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
if(NOT EXISTS "${_dual_jpg}")
  message(FATAL_ERROR
    "python metatransfer dual-write did not write jpeg output\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
if(NOT EXISTS "${_dual_jpg_sidecar}")
  message(FATAL_ERROR
    "python metatransfer dual-write did not write xmp sidecar\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
if(NOT _out_dual MATCHES "xmp_sidecar_output=.*dual_write\\.xmp")
  message(FATAL_ERROR
    "python metatransfer dual-write missing xmp sidecar summary\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
if(NOT _out_dual MATCHES "xmp_sidecar: status=ok bytes=[0-9]+ path=.*dual_write\\.xmp")
  message(FATAL_ERROR
    "python metatransfer dual-write missing C++ sidecar path summary\nstdout:\n${_out_dual}\nstderr:\n${_err_dual}")
endif()
execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; b=Path(r'''${_dual_jpg_sidecar}''').read_bytes(); import sys; sys.exit(0 if (b.find(b'<x:xmpmeta')!=-1 or b.find(b'<rdf:RDF')!=-1) else 1)"
  RESULT_VARIABLE _rv_dual_check
  OUTPUT_VARIABLE _out_dual_check
  ERROR_VARIABLE _err_dual_check
)
if(NOT _rv_dual_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer dual-write sidecar content check failed (${_rv_dual_check})\nstdout:\n${_out_dual_check}\nstderr:\n${_err_dual_check}")
endif()

file(WRITE "${_embed_only_strip_sidecar}" "stale sidecar\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jpeg "${_target_jpg}"
          --xmp-destination-sidecar strip_existing
          -o "${_embed_only_strip_jpg}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_embed_strip
  OUTPUT_VARIABLE _out_embed_strip
  ERROR_VARIABLE _err_embed_strip
)
if(NOT _rv_embed_strip EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer embedded-only sidecar cleanup failed (${_rv_embed_strip})\nstdout:\n${_out_embed_strip}\nstderr:\n${_err_embed_strip}")
endif()
if(NOT EXISTS "${_embed_only_strip_jpg}")
  message(FATAL_ERROR
    "python metatransfer embedded-only cleanup did not write jpeg output\nstdout:\n${_out_embed_strip}\nstderr:\n${_err_embed_strip}")
endif()
if(EXISTS "${_embed_only_strip_sidecar}")
  message(FATAL_ERROR
    "python metatransfer embedded-only cleanup did not remove stale sidecar\nstdout:\n${_out_embed_strip}\nstderr:\n${_err_embed_strip}")
endif()
if(NOT _out_embed_strip MATCHES "xmp_sidecar_removed=.*embed_only_strip\\.xmp")
  message(FATAL_ERROR
    "python metatransfer embedded-only cleanup missing sidecar removal summary\nstdout:\n${_out_embed_strip}\nstderr:\n${_err_embed_strip}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jpeg "${_target_jpg_xmp}"
          --xmp-writeback sidecar
          --xmp-destination-embedded strip_existing
          -o "${_sidecar_only_strip_jpg}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_sidecar_strip
  OUTPUT_VARIABLE _out_sidecar_strip
  ERROR_VARIABLE _err_sidecar_strip
)
if(NOT _rv_sidecar_strip EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer sidecar-only embedded-strip failed (${_rv_sidecar_strip})\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}")
endif()
if(NOT EXISTS "${_sidecar_only_strip_jpg}")
  message(FATAL_ERROR
    "python metatransfer sidecar-only embedded-strip did not write jpeg output\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}")
endif()
if(NOT EXISTS "${_sidecar_only_strip_sidecar}")
  message(FATAL_ERROR
    "python metatransfer sidecar-only embedded-strip did not write xmp sidecar\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}")
endif()
if(NOT _out_sidecar_strip MATCHES "xmp_sidecar_output=.*sidecar_only_strip\\.xmp")
  message(FATAL_ERROR
    "python metatransfer sidecar-only embedded-strip missing generated sidecar summary\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}")
endif()
execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; b=Path(r'''${_sidecar_only_strip_jpg}''').read_bytes(); import sys; sys.exit(0 if (b.find(b'Target Embedded Existing')==-1 and b.find(b'http://ns.adobe.com/xap/1.0/')==-1) else 1)"
  RESULT_VARIABLE _rv_sidecar_strip_check
  OUTPUT_VARIABLE _out_sidecar_strip_check
  ERROR_VARIABLE _err_sidecar_strip_check
)
if(NOT _rv_sidecar_strip_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer sidecar-only embedded-strip output still contains embedded xmp (${_rv_sidecar_strip_check})\nstdout:\n${_out_sidecar_strip}\nstderr:\n${_err_sidecar_strip}\ncheck_stderr:\n${_err_sidecar_strip_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-tiff "${_target_tif_xmp}"
          --xmp-writeback sidecar
          --xmp-destination-embedded strip_existing
          -o "${_sidecar_only_strip_tif}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_sidecar_strip_tif
  OUTPUT_VARIABLE _out_sidecar_strip_tif
  ERROR_VARIABLE _err_sidecar_strip_tif
)
if(NOT _rv_sidecar_strip_tif EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer tiff sidecar-only embedded-strip failed (${_rv_sidecar_strip_tif})\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}")
endif()
if(NOT EXISTS "${_sidecar_only_strip_tif}")
  message(FATAL_ERROR
    "python metatransfer tiff sidecar-only embedded-strip did not write output\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}")
endif()
if(NOT EXISTS "${_sidecar_only_strip_tif_sidecar}")
  message(FATAL_ERROR
    "python metatransfer tiff sidecar-only embedded-strip did not write xmp sidecar\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}")
endif()
if(NOT _out_sidecar_strip_tif MATCHES "xmp_sidecar_output=.*sidecar_only_strip_tiff\\.xmp")
  message(FATAL_ERROR
    "python metatransfer tiff sidecar-only embedded-strip missing generated sidecar summary\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}")
endif()
execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; import sys; b=Path(r'''${_sidecar_only_strip_tif}''').read_bytes(); ok=(len(b)>=8 and b[0:2]==b'II' and int.from_bytes(b[2:4],'little')==42); off=int.from_bytes(b[4:8],'little') if ok else 0; ok=ok and (off+2<=len(b)); n=int.from_bytes(b[off:off+2],'little') if ok else 0; p=off+2; ok=ok and (p+n*12+4<=len(b)); tags=[int.from_bytes(b[p+i*12:p+i*12+2],'little') for i in range(n)] if ok else []; sys.exit(0 if (ok and 700 not in tags and 0x0132 in tags) else 1)"
  RESULT_VARIABLE _rv_sidecar_strip_tif_check
  OUTPUT_VARIABLE _out_sidecar_strip_tif_check
  ERROR_VARIABLE _err_sidecar_strip_tif_check
)
if(NOT _rv_sidecar_strip_tif_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer tiff sidecar-only embedded-strip output still contains xmp tag 700 or lost DateTime (${_rv_sidecar_strip_tif_check})\nstdout:\n${_out_sidecar_strip_tif}\nstderr:\n${_err_sidecar_strip_tif}\ncheck_stderr:\n${_err_sidecar_strip_tif_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jpeg "${_target_jpg_xmp}"
          --xmp-include-existing
          --xmp-conflict-policy existing_wins
          --xmp-include-existing-destination-embedded
          --xmp-existing-destination-embedded-precedence source_wins
          -o "${_destination_merge_jpg}"
          "${_src_jpg_xmp}"
  RESULT_VARIABLE _rv_destination_merge
  OUTPUT_VARIABLE _out_destination_merge
  ERROR_VARIABLE _err_destination_merge
)
if(NOT _rv_destination_merge EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer destination embedded merge failed (${_rv_destination_merge})\nstdout:\n${_out_destination_merge}\nstderr:\n${_err_destination_merge}")
endif()
if(NOT EXISTS "${_destination_merge_jpg}")
  message(FATAL_ERROR
    "python metatransfer destination embedded merge did not write jpeg output\nstdout:\n${_out_destination_merge}\nstderr:\n${_err_destination_merge}")
endif()
if(NOT _out_destination_merge MATCHES "xmp_existing_destination_embedded: status=ok loaded=yes path=.*target_xmp\\.jpg")
  message(FATAL_ERROR
    "python metatransfer destination embedded merge missing status summary\nstdout:\n${_out_destination_merge}\nstderr:\n${_err_destination_merge}")
endif()
execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
    "from pathlib import Path; b=Path(r'''${_destination_merge_jpg}''').read_bytes(); import sys; sys.exit(0 if (b.find(b'OpenMeta Transfer Source')!=-1 and b.find(b'Target Embedded Existing')==-1) else 1)"
  RESULT_VARIABLE _rv_destination_merge_check
  OUTPUT_VARIABLE _out_destination_merge_check
  ERROR_VARIABLE _err_destination_merge_check
)
if(NOT _rv_destination_merge_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer destination embedded precedence check failed (${_rv_destination_merge_check})\nstdout:\n${_out_destination_merge}\nstderr:\n${_err_destination_merge}\ncheck_stderr:\n${_err_destination_merge_check}")
endif()

file(WRITE "${_check_tiff_py}" [=[
import sys
from pathlib import Path

def read_u16(b, off):
    return int.from_bytes(b[off:off+2], "little", signed=False)

def read_u32(b, off):
    return int.from_bytes(b[off:off+4], "little", signed=False)

path = Path(sys.argv[1])
expect_dt = sys.argv[2]
b = path.read_bytes()
if len(b) < 8 or b[:2] != b"II" or read_u16(b, 2) != 42:
    raise SystemExit("invalid little-endian classic TIFF")
ifd0_off = read_u32(b, 4)
count = read_u16(b, ifd0_off)
p = ifd0_off + 2
tags = {}
for i in range(count):
    e = p + i * 12
    tag = read_u16(b, e + 0)
    typ = read_u16(b, e + 2)
    cnt = read_u32(b, e + 4)
    raw = b[e + 8:e + 12]
    tags[tag] = (typ, cnt, raw)
if 0x0132 not in tags:
    raise SystemExit("missing DateTime")
if 700 not in tags:
    raise SystemExit("missing XMP tag 700")
typ, cnt, raw = tags[0x0132]
if typ != 2 or cnt != 20:
    raise SystemExit("DateTime type/count mismatch")
off = read_u32(raw, 0)
dt = b[off:off+20].split(b"\x00", 1)[0].decode("ascii", "ignore")
if dt != expect_dt:
    raise SystemExit(f"DateTime mismatch: {dt!r}")
]=])

file(WRITE "${_check_readback_py}" [=[
import sys
from pathlib import Path

import openmeta


def fail(message: str) -> None:
    raise SystemExit(message)


def as_text(value) -> str:
    if isinstance(value, (bytes, bytearray)):
        return bytes(value).split(b"\x00", 1)[0].decode("ascii", "ignore")
    return str(value)


def as_ints(value) -> list[int]:
    if isinstance(value, int):
        return [int(value)]
    if isinstance(value, (bytes, bytearray)):
        return [int(v) for v in value]
    try:
        return [int(v) for v in value]
    except TypeError:
        return [int(value)]


def expect_exif(ifd: str, tag: int, expected: list[int]) -> None:
    entries = doc.find_exif(ifd, tag)
    if len(entries) == 0:
        fail(f"missing {ifd}:0x{tag:04X}")
    actual = as_ints(entries[0].value())
    if actual != expected:
        fail(f"{ifd}:0x{tag:04X} mismatch: {actual!r}")


mode = sys.argv[1]
path = Path(sys.argv[2])
expect_dt = sys.argv[3]

doc = openmeta.read(str(path))
if doc.scan_status != openmeta.ScanStatus.Ok:
    fail(f"scan_status={doc.scan_status.name}")
if mode != "bmff_xmp_creator_no_exif" and mode != "bmff_xmp_no_exif":
    if doc.exif_status != openmeta.ExifDecodeStatus.Ok:
        fail(f"exif_status={doc.exif_status.name}")

    dt_entries = doc.find_exif("ifd0", 0x0132)
    if len(dt_entries) == 0:
        fail("missing DateTime")
    dt = as_text(dt_entries[0].value())
    if dt != expect_dt:
        fail(f"DateTime mismatch: {dt!r}")

if mode == "tiff":
    if int(doc.xmp_entries_decoded) == 0:
        fail("missing decoded XMP entries")
elif mode == "dng":
    if int(doc.xmp_entries_decoded) == 0:
        fail("missing decoded XMP entries")
    dng_entries = doc.find_exif("ifd0", 0xC612)
    if len(dng_entries) == 0:
        fail("missing DNGVersion")
    dng_version = list(dng_entries[0].value())
    if dng_version != [1, 6, 0, 0]:
        fail(f"DNGVersion mismatch: {dng_version!r}")
elif mode == "bmff":
    if int(doc.xmp_entries_decoded) != 0:
        fail(f"unexpected XMP entries: {int(doc.xmp_entries_decoded)}")
elif mode == "bmff_xmp":
    if int(doc.xmp_entries_decoded) == 0:
        fail("missing decoded XMP entries")
elif mode == "bmff_xmp_no_exif":
    if int(doc.xmp_entries_decoded) == 0:
        fail("missing decoded XMP entries")
elif mode == "bmff_xmp_creator":
    if int(doc.xmp_entries_decoded) == 0:
        fail("missing decoded XMP entries")
    expected_creator = sys.argv[4]
    creators = []
    for i in range(len(doc)):
        entry = doc[i]
        schema = entry.xmp_schema_ns
        path_value = entry.xmp_path
        if schema is None or path_value is None:
            continue
        if str(schema) != "http://ns.adobe.com/xap/1.0/":
            continue
        if not str(path_value).endswith("CreatorTool"):
            continue
        creators.append(as_text(entry.value()))
    if expected_creator not in creators:
        fail(f"missing CreatorTool: {expected_creator!r} found={creators!r}")
    for forbidden_creator in sys.argv[5:]:
        if forbidden_creator != "-" and forbidden_creator in creators:
            fail(
                f"unexpected CreatorTool: {forbidden_creator!r} found={creators!r}"
            )
elif mode == "bmff_xmp_creator_no_exif":
    if int(doc.xmp_entries_decoded) == 0:
        fail("missing decoded XMP entries")
    expected_creator = sys.argv[4]
    creators = []
    for i in range(len(doc)):
        entry = doc[i]
        schema = entry.xmp_schema_ns
        path_value = entry.xmp_path
        if schema is None or path_value is None:
            continue
        if str(schema) != "http://ns.adobe.com/xap/1.0/":
            continue
        if not str(path_value).endswith("CreatorTool"):
            continue
        creators.append(as_text(entry.value()))
    if expected_creator not in creators:
        fail(f"missing CreatorTool: {expected_creator!r} found={creators!r}")
    for forbidden_creator in sys.argv[5:]:
        if forbidden_creator != "-" and forbidden_creator in creators:
            fail(
                f"unexpected CreatorTool: {forbidden_creator!r} found={creators!r}"
            )
elif mode == "target_spec":
    expect_exif("ifd0", 0x0100, [320])
    expect_exif("ifd0", 0x0101, [240])
    expect_exif("exififd", 0xA002, [320])
    expect_exif("exififd", 0xA003, [240])
    expect_exif("ifd0", 0x0112, [1])
    expect_exif("ifd0", 0x0115, [3])
    expect_exif("ifd0", 0x0102, [8, 8, 8])
    expect_exif("ifd0", 0x0153, [1, 1, 1])
    expect_exif("ifd0", 0x0106, [2])
    expect_exif("ifd0", 0x011C, [1])
    expect_exif("exififd", 0xA001, [1])
elif mode == "exif_only_no_xmp":
    if int(doc.xmp_entries_decoded) != 0:
        fail(f"unexpected XMP entries: {int(doc.xmp_entries_decoded)}")
else:
    fail(f"unknown mode: {mode}")
]=])

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
          "target_spec" "${_target_spec_jpg}" "2000:01:02 03:04:05"
  RESULT_VARIABLE _rv_target_spec_check
  OUTPUT_VARIABLE _out_target_spec_check
  ERROR_VARIABLE _err_target_spec_check
)
if(NOT _rv_target_spec_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer target-spec read-back check failed (${_rv_target_spec_check})\nstdout:\n${_out_target_spec}\nstderr:\n${_err_target_spec}\ncheck_stdout:\n${_out_target_spec_check}\ncheck_stderr:\n${_err_target_spec_check}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
          "target_spec" "${_target_spec_tif}" "2000:01:02 03:04:05"
  RESULT_VARIABLE _rv_target_spec_tif_check
  OUTPUT_VARIABLE _out_target_spec_tif_check
  ERROR_VARIABLE _err_target_spec_tif_check
)
if(NOT _rv_target_spec_tif_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer target-spec tiff read-back check failed (${_rv_target_spec_tif_check})\nstdout:\n${_out_target_spec_tif}\nstderr:\n${_err_target_spec_tif}\ncheck_stdout:\n${_out_target_spec_tif_check}\ncheck_stderr:\n${_err_target_spec_tif_check}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
          "target_spec" "${_target_spec_dng}" "2000:01:02 03:04:05"
  RESULT_VARIABLE _rv_target_spec_dng_check
  OUTPUT_VARIABLE _out_target_spec_dng_check
  ERROR_VARIABLE _err_target_spec_dng_check
)
if(NOT _rv_target_spec_dng_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer target-spec dng read-back check failed (${_rv_target_spec_dng_check})\nstdout:\n${_out_target_spec_dng}\nstderr:\n${_err_target_spec_dng}\ncheck_stdout:\n${_out_target_spec_dng_check}\ncheck_stderr:\n${_err_target_spec_dng_check}")
endif()

function(_run_bmff_dual_write_xmp_smoke label target_flag target_path output_path
         sidecar_path)
  get_filename_component(_sidecar_name "${sidecar_path}" NAME)

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${OPENMETA_PYTHONPATH}"
            "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
            --no-build-info
            --source-meta "${_src_jpg_xmp}"
            ${target_flag}
            --no-icc
            --no-iptc
            --xmp-writeback embedded_and_sidecar
            --output "${output_path}" --force
            "${target_path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "python metatransfer ${label} dual-write edit failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "edit_plan: status=ok")
    message(FATAL_ERROR
      "python metatransfer ${label} dual-write missing edit_plan ok\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "edit_apply: status=ok")
    message(FATAL_ERROR
      "python metatransfer ${label} dual-write missing edit_apply ok\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "xmp_sidecar_output=.*${_sidecar_name}")
    message(FATAL_ERROR
      "python metatransfer ${label} dual-write missing xmp sidecar summary\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT EXISTS "${output_path}")
    message(FATAL_ERROR
      "python metatransfer ${label} dual-write did not write edited output\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT EXISTS "${sidecar_path}")
    message(FATAL_ERROR
      "python metatransfer ${label} dual-write did not write xmp sidecar\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()

  execute_process(
    COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
      "from pathlib import Path; b=Path(r'''${sidecar_path}''').read_bytes(); import sys; sys.exit(0 if (b.find(b'<x:xmpmeta')!=-1 or b.find(b'<rdf:RDF')!=-1) else 1)"
    RESULT_VARIABLE _rv_sidecar
    OUTPUT_VARIABLE _out_sidecar
    ERROR_VARIABLE _err_sidecar
  )
  if(NOT _rv_sidecar EQUAL 0)
    message(FATAL_ERROR
      "python metatransfer ${label} dual-write sidecar content check failed (${_rv_sidecar})\nstdout:\n${_out}\nstderr:\n${_err}\ncheck_stdout:\n${_out_sidecar}\ncheck_stderr:\n${_err_sidecar}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${OPENMETA_PYTHONPATH}"
            "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
            "bmff_xmp" "${output_path}" "2000:01:02 03:04:05"
    RESULT_VARIABLE _rv_check
    OUTPUT_VARIABLE _out_check
    ERROR_VARIABLE _err_check
  )
  if(NOT _rv_check EQUAL 0)
    message(FATAL_ERROR
      "python metatransfer ${label} dual-write read-back check failed (${_rv_check})\nstdout:\n${_out}\nstderr:\n${_err}\ncheck_stdout:\n${_out_check}\ncheck_stderr:\n${_err_check}")
  endif()
endfunction()

function(_run_bmff_embedded_xmp_smoke label target_flag target_path output_path
         sidecar_path)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${OPENMETA_PYTHONPATH}"
            "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
            --no-build-info
            --source-meta "${_src_jpg_xmp}"
            ${target_flag}
            --no-icc
            --no-iptc
            --output "${output_path}" --force
            "${target_path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "python metatransfer ${label} embedded-xmp edit failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "edit_plan: status=ok")
    message(FATAL_ERROR
      "python metatransfer ${label} embedded-xmp missing edit_plan ok\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "edit_apply: status=ok")
    message(FATAL_ERROR
      "python metatransfer ${label} embedded-xmp missing edit_apply ok\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT EXISTS "${output_path}")
    message(FATAL_ERROR
      "python metatransfer ${label} embedded-xmp did not write edited output\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(EXISTS "${sidecar_path}")
    message(FATAL_ERROR
      "python metatransfer ${label} embedded-xmp unexpectedly wrote xmp sidecar\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${OPENMETA_PYTHONPATH}"
            "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
            "bmff_xmp" "${output_path}" "2000:01:02 03:04:05"
    RESULT_VARIABLE _rv_check
    OUTPUT_VARIABLE _out_check
    ERROR_VARIABLE _err_check
  )
  if(NOT _rv_check EQUAL 0)
    message(FATAL_ERROR
      "python metatransfer ${label} embedded-xmp read-back check failed (${_rv_check})\nstdout:\n${_out}\nstderr:\n${_err}\ncheck_stdout:\n${_out_check}\ncheck_stderr:\n${_err_check}")
  endif()
endfunction()

function(_write_bmff_existing_embedded_xmp_target label target_flag seed_target_path
         output_path)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${OPENMETA_PYTHONPATH}"
            "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
            --no-build-info
            --source-meta "${_src_jpg_target_embedded_xmp}"
            ${target_flag}
            --no-exif
            --no-icc
            --no-iptc
            --xmp-include-existing
            --output "${output_path}" --force
            "${seed_target_path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "python metatransfer ${label} existing-embedded target build failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "edit_plan: status=ok")
    message(FATAL_ERROR
      "python metatransfer ${label} existing-embedded target build missing edit_plan ok\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "edit_apply: status=ok")
    message(FATAL_ERROR
      "python metatransfer ${label} existing-embedded target build missing edit_apply ok\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT EXISTS "${output_path}")
    message(FATAL_ERROR
      "python metatransfer ${label} existing-embedded target build did not write output\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${OPENMETA_PYTHONPATH}"
            "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
            "bmff_xmp_no_exif" "${output_path}" "2000:01:02 03:04:05"
    RESULT_VARIABLE _rv_check
    OUTPUT_VARIABLE _out_check
    ERROR_VARIABLE _err_check
  )
  if(NOT _rv_check EQUAL 0)
    message(FATAL_ERROR
      "python metatransfer ${label} existing-embedded target read-back check failed (${_rv_check})\nstdout:\n${_out}\nstderr:\n${_err}\ncheck_stdout:\n${_out_check}\ncheck_stderr:\n${_err_check}")
  endif()

  execute_process(
    COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
      "from pathlib import Path; import sys; data=Path(sys.argv[1]).read_bytes(); expected=sys.argv[2].encode('utf-8'); bad1=sys.argv[3].encode('utf-8'); ok=(data.find(expected)!=-1 and data.find(bad1)==-1); sys.exit(0 if ok else 1)"
      "${output_path}" "Target Embedded Existing" "OpenMeta Transfer Source"
    RESULT_VARIABLE _rv_bytes
    OUTPUT_VARIABLE _out_bytes
    ERROR_VARIABLE _err_bytes
  )
  if(NOT _rv_bytes EQUAL 0)
    message(FATAL_ERROR
      "python metatransfer ${label} existing-embedded target raw packet check failed (${_rv_bytes})\nstdout:\n${_out}\nstderr:\n${_err}\ncheck_stdout:\n${_out_bytes}\ncheck_stderr:\n${_err_bytes}")
  endif()
endfunction()

function(_run_bmff_mixed_destination_carrier_precedence_smoke label target_flag
         target_path output_path carrier_precedence expected_creator
         forbidden_creator_one forbidden_creator_two)
  get_filename_component(_output_dir "${output_path}" DIRECTORY)
  get_filename_component(_output_stem "${output_path}" NAME_WE)
  set(_sidecar_path "${_output_dir}/${_output_stem}.xmp")
  file(WRITE "${_sidecar_path}"
    "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'><xmp:CreatorTool>Target Sidecar Existing</xmp:CreatorTool></rdf:Description></rdf:RDF></x:xmpmeta>")

  set(_carrier_args)
  if(carrier_precedence STREQUAL "embedded_wins")
    set(_carrier_args
      --xmp-existing-destination-carrier-precedence embedded_wins)
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${OPENMETA_PYTHONPATH}"
            "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
            --no-build-info
            --source-meta "${_src_jpg_xmp}"
            ${target_flag}
            --no-icc
            --no-iptc
            --xmp-include-existing
            --xmp-conflict-policy existing_wins
            --xmp-include-existing-sidecar
            --xmp-include-existing-destination-embedded
            ${_carrier_args}
            --xmp-writeback embedded_and_sidecar
            --output "${output_path}" --force
            "${target_path}"
    RESULT_VARIABLE _rv
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR
      "python metatransfer ${label} mixed-destination precedence edit failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "xmp_existing_sidecar: status=ok loaded=yes")
    message(FATAL_ERROR
      "python metatransfer ${label} mixed-destination precedence missing existing-sidecar load summary\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "xmp_existing_destination_embedded: status=ok loaded=yes")
    message(FATAL_ERROR
      "python metatransfer ${label} mixed-destination precedence missing existing-destination-embedded load summary\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "edit_plan: status=ok")
    message(FATAL_ERROR
      "python metatransfer ${label} mixed-destination precedence missing edit_plan ok\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT _out MATCHES "edit_apply: status=ok")
    message(FATAL_ERROR
      "python metatransfer ${label} mixed-destination precedence missing edit_apply ok\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT EXISTS "${output_path}")
    message(FATAL_ERROR
      "python metatransfer ${label} mixed-destination precedence did not write output\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()
  if(NOT EXISTS "${_sidecar_path}")
    message(FATAL_ERROR
      "python metatransfer ${label} mixed-destination precedence did not write sidecar\nstdout:\n${_out}\nstderr:\n${_err}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${OPENMETA_PYTHONPATH}"
            "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
            "bmff_xmp" "${output_path}" "2000:01:02 03:04:05"
    RESULT_VARIABLE _rv_check
    OUTPUT_VARIABLE _out_check
    ERROR_VARIABLE _err_check
  )
  if(NOT _rv_check EQUAL 0)
    message(FATAL_ERROR
      "python metatransfer ${label} mixed-destination precedence read-back check failed (${_rv_check})\nstdout:\n${_out}\nstderr:\n${_err}\ncheck_stdout:\n${_out_check}\ncheck_stderr:\n${_err_check}")
  endif()

  execute_process(
    COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
      "from pathlib import Path; import sys; data=Path(sys.argv[1]).read_bytes(); expected=sys.argv[2].encode('utf-8'); bad1=sys.argv[3].encode('utf-8'); bad2=sys.argv[4].encode('utf-8'); ok=(data.find(expected)!=-1 and data.find(bad1)==-1 and data.find(bad2)==-1); sys.exit(0 if ok else 1)"
      "${output_path}" "${expected_creator}" "${forbidden_creator_one}"
      "${forbidden_creator_two}"
    RESULT_VARIABLE _rv_output_bytes
    OUTPUT_VARIABLE _out_output_bytes
    ERROR_VARIABLE _err_output_bytes
  )
  if(NOT _rv_output_bytes EQUAL 0)
    message(FATAL_ERROR
      "python metatransfer ${label} mixed-destination precedence output packet check failed (${_rv_output_bytes})\nstdout:\n${_out}\nstderr:\n${_err}\ncheck_stdout:\n${_out_output_bytes}\ncheck_stderr:\n${_err_output_bytes}")
  endif()

  execute_process(
    COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c
      "from pathlib import Path; import sys; data=Path(sys.argv[1]).read_bytes(); expected=sys.argv[2].encode('utf-8'); bad1=sys.argv[3].encode('utf-8'); bad2=sys.argv[4].encode('utf-8'); ok=(data.find(expected)!=-1 and data.find(bad1)==-1 and data.find(bad2)==-1); sys.exit(0 if ok else 1)"
      "${_sidecar_path}" "${expected_creator}" "${forbidden_creator_one}"
      "${forbidden_creator_two}"
    RESULT_VARIABLE _rv_sidecar
    OUTPUT_VARIABLE _out_sidecar
    ERROR_VARIABLE _err_sidecar
  )
  if(NOT _rv_sidecar EQUAL 0)
    message(FATAL_ERROR
      "python metatransfer ${label} mixed-destination precedence sidecar content check failed (${_rv_sidecar})\nstdout:\n${_out}\nstderr:\n${_err}\ncheck_stdout:\n${_out_sidecar}\ncheck_stderr:\n${_err_sidecar}")
  endif()
endfunction()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --time-patch "DateTime=2024:12:31 23:59:59"
          --target-tiff "${_target_tif}"
          -o "${_edited_tif}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_tif
  OUTPUT_VARIABLE _out_tif
  ERROR_VARIABLE _err_tif
)
if(NOT _rv_tif EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer tiff edit failed (${_rv_tif})\nstdout:\n${_out_tif}\nstderr:\n${_err_tif}")
endif()
if(NOT _out_tif MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "python metatransfer tiff edit missing plan ok\nstdout:\n${_out_tif}\nstderr:\n${_err_tif}")
endif()
if(NOT _out_tif MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer tiff edit missing apply ok\nstdout:\n${_out_tif}\nstderr:\n${_err_tif}")
endif()
if(NOT EXISTS "${_edited_tif}")
  message(FATAL_ERROR
    "python metatransfer tiff edit did not write output\nstdout:\n${_out_tif}\nstderr:\n${_err_tif}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
          "tiff" "${_edited_tif}" "2024:12:31 23:59:59"
  RESULT_VARIABLE _rv_tif_check
  OUTPUT_VARIABLE _out_tif_check
  ERROR_VARIABLE _err_tif_check
)
if(NOT _rv_tif_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer tiff output check failed (${_rv_tif_check})\nstdout:\n${_out_tif_check}\nstderr:\n${_err_tif_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --time-patch "DateTime=2024:12:31 23:59:59"
          --target-dng "${_target_dng}"
          -o "${_edited_dng}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_dng
  OUTPUT_VARIABLE _out_dng
  ERROR_VARIABLE _err_dng
)
if(NOT _rv_dng EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer dng edit failed (${_rv_dng})\nstdout:\n${_out_dng}\nstderr:\n${_err_dng}")
endif()
if(NOT _out_dng MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "python metatransfer dng edit missing plan ok\nstdout:\n${_out_dng}\nstderr:\n${_err_dng}")
endif()
if(NOT _out_dng MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer dng edit missing apply ok\nstdout:\n${_out_dng}\nstderr:\n${_err_dng}")
endif()
if(NOT EXISTS "${_edited_dng}")
  message(FATAL_ERROR
    "python metatransfer dng edit did not write output\nstdout:\n${_out_dng}\nstderr:\n${_err_dng}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
          "dng" "${_edited_dng}" "2024:12:31 23:59:59"
  RESULT_VARIABLE _rv_dng_check
  OUTPUT_VARIABLE _out_dng_check
  ERROR_VARIABLE _err_dng_check
)
if(NOT _rv_dng_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer dng output check failed (${_rv_dng_check})\nstdout:\n${_out_dng_check}\nstderr:\n${_err_dng_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jxl
          --no-xmp
          --no-icc
          --no-iptc
          "${_src_jpg}"
  RESULT_VARIABLE _rv_jxl
  OUTPUT_VARIABLE _out_jxl
  ERROR_VARIABLE _err_jxl
)
if(NOT _rv_jxl EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl summary failed (${_rv_jxl})\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()
if(NOT _out_jxl MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl summary missing compile ok\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()
if(NOT _out_jxl MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl summary missing emit ok\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()
if(NOT _out_jxl MATCHES "jxl_box Exif count=1")
  message(FATAL_ERROR
    "python metatransfer jxl summary missing Exif box summary\nstdout:\n${_out_jxl}\nstderr:\n${_err_jxl}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jxl
          --no-exif
          --no-xmp
          --no-iptc
          "${_src_icc_jpg}"
  RESULT_VARIABLE _rv_jxl_icc
  OUTPUT_VARIABLE _out_jxl_icc
  ERROR_VARIABLE _err_jxl_icc
)
if(NOT _rv_jxl_icc EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl icc summary failed (${_rv_jxl_icc})\nstdout:\n${_out_jxl_icc}\nstderr:\n${_err_jxl_icc}")
endif()
if(NOT _out_jxl_icc MATCHES "jxl_icc_profile bytes=[1-9][0-9]*")
  message(FATAL_ERROR
    "python metatransfer jxl icc summary missing encoder icc handoff\nstdout:\n${_out_jxl_icc}\nstderr:\n${_err_jxl_icc}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jxl
          --no-exif
          --no-xmp
          --no-iptc
          --dump-jxl-encoder-handoff "${_jxl_handoff}"
          "${_src_icc_jpg}"
  RESULT_VARIABLE _rv_jxl_handoff
  OUTPUT_VARIABLE _out_jxl_handoff
  ERROR_VARIABLE _err_jxl_handoff
)
if(NOT _rv_jxl_handoff EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl encoder handoff dump failed (${_rv_jxl_handoff})\nstdout:\n${_out_jxl_handoff}\nstderr:\n${_err_jxl_handoff}")
endif()
if(NOT _out_jxl_handoff MATCHES "jxl_encoder_handoff: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl encoder handoff dump missing status ok\nstdout:\n${_out_jxl_handoff}\nstderr:\n${_err_jxl_handoff}")
endif()
if(NOT EXISTS "${_jxl_handoff}")
  message(FATAL_ERROR
    "python metatransfer jxl encoder handoff dump did not write output\nstdout:\n${_out_jxl_handoff}\nstderr:\n${_err_jxl_handoff}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --load-jxl-encoder-handoff "${_jxl_handoff}"
  RESULT_VARIABLE _rv_jxl_handoff_load
  OUTPUT_VARIABLE _out_jxl_handoff_load
  ERROR_VARIABLE _err_jxl_handoff_load
)
if(NOT _rv_jxl_handoff_load EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl encoder handoff load failed (${_rv_jxl_handoff_load})\nstdout:\n${_out_jxl_handoff_load}\nstderr:\n${_err_jxl_handoff_load}")
endif()
if(NOT _out_jxl_handoff_load MATCHES "jxl_icc_profile bytes=[1-9][0-9]*")
  message(FATAL_ERROR
    "python metatransfer jxl encoder handoff load missing icc summary\nstdout:\n${_out_jxl_handoff_load}\nstderr:\n${_err_jxl_handoff_load}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --load-transfer-artifact "${_jxl_handoff}"
  RESULT_VARIABLE _rv_artifact_jxl_load
  OUTPUT_VARIABLE _out_artifact_jxl_load
  ERROR_VARIABLE _err_artifact_jxl_load
)
if(NOT _rv_artifact_jxl_load EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer generic artifact load for jxl handoff failed (${_rv_artifact_jxl_load})\nstdout:\n${_out_artifact_jxl_load}\nstderr:\n${_err_artifact_jxl_load}")
endif()
if(NOT _out_artifact_jxl_load MATCHES "transfer_artifact: status=ok code=none kind=jxl_encoder_handoff bytes=[0-9]+ target=jxl")
  message(FATAL_ERROR
    "python metatransfer generic artifact load missing jxl handoff summary\nstdout:\n${_out_artifact_jxl_load}\nstderr:\n${_err_artifact_jxl_load}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jxl
          --no-xmp
          --no-icc
          --source-meta "${_src_jpg}"
          --output "${_edited_jxl}"
          "${_target_jxl}"
  RESULT_VARIABLE _rv_jxl_edit
  OUTPUT_VARIABLE _out_jxl_edit
  ERROR_VARIABLE _err_jxl_edit
)
if(NOT _rv_jxl_edit EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl edit failed (${_rv_jxl_edit})\nstdout:\n${_out_jxl_edit}\nstderr:\n${_err_jxl_edit}")
endif()
if(NOT _out_jxl_edit MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl edit missing edit apply ok\nstdout:\n${_out_jxl_edit}\nstderr:\n${_err_jxl_edit}")
endif()
if(NOT EXISTS "${_edited_jxl}")
  message(FATAL_ERROR
    "python metatransfer jxl edit did not write output\nstdout:\n${_out_jxl_edit}\nstderr:\n${_err_jxl_edit}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
          "exif_only_no_xmp" "${_edited_jxl}" "2000:01:02 03:04:05"
  RESULT_VARIABLE _rv_jxl_check
  OUTPUT_VARIABLE _out_jxl_check
  ERROR_VARIABLE _err_jxl_check
)
if(NOT _rv_jxl_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl read-back check failed (${_rv_jxl_check})\nstdout:\n${_out_jxl_edit}\nstderr:\n${_err_jxl_edit}\ncheck_stdout:\n${_out_jxl_check}\ncheck_stderr:\n${_err_jxl_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jp2
          --no-xmp
          --no-icc
          --no-iptc
          "${_src_jpg}"
  RESULT_VARIABLE _rv_jp2
  OUTPUT_VARIABLE _out_jp2
  ERROR_VARIABLE _err_jp2
)
if(NOT _rv_jp2 EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jp2 summary failed (${_rv_jp2})\nstdout:\n${_out_jp2}\nstderr:\n${_err_jp2}")
endif()
if(NOT _out_jp2 MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "python metatransfer jp2 summary missing compile ok\nstdout:\n${_out_jp2}\nstderr:\n${_err_jp2}")
endif()
if(NOT _out_jp2 MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "python metatransfer jp2 summary missing emit ok\nstdout:\n${_out_jp2}\nstderr:\n${_err_jp2}")
endif()
if(NOT _out_jp2 MATCHES "jp2_box Exif count=1")
  message(FATAL_ERROR
    "python metatransfer jp2 summary missing Exif box summary\nstdout:\n${_out_jp2}\nstderr:\n${_err_jp2}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --source-meta "${_src_jpg}"
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
    "python metatransfer jp2 edit failed (${_rv_jp2_edit})\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}")
endif()
if(NOT _out_jp2_edit MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "python metatransfer jp2 edit missing edit_plan ok\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}")
endif()
if(NOT _out_jp2_edit MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer jp2 edit missing edit_apply ok\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}")
endif()
if(NOT EXISTS "${_edited_jp2}")
  message(FATAL_ERROR
    "python metatransfer jp2 edit did not write output\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
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
    "python metatransfer jp2 roundtrip summary failed (${_rv_jp2_roundtrip})\nstdout:\n${_out_jp2_roundtrip}\nstderr:\n${_err_jp2_roundtrip}")
endif()
if(NOT _out_jp2_roundtrip MATCHES "jp2_box Exif count=1")
  message(FATAL_ERROR
    "python metatransfer jp2 roundtrip summary missing Exif box summary\nstdout:\n${_out_jp2_roundtrip}\nstderr:\n${_err_jp2_roundtrip}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
          "exif_only_no_xmp" "${_edited_jp2}" "2000:01:02 03:04:05"
  RESULT_VARIABLE _rv_jp2_check
  OUTPUT_VARIABLE _out_jp2_check
  ERROR_VARIABLE _err_jp2_check
)
if(NOT _rv_jp2_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jp2 read-back check failed (${_rv_jp2_check})\nstdout:\n${_out_jp2_edit}\nstderr:\n${_err_jp2_edit}\ncheck_stdout:\n${_out_jp2_check}\ncheck_stderr:\n${_err_jp2_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-png
          --no-xmp
          --no-icc
          --no-iptc
          "${_src_jpg}"
  RESULT_VARIABLE _rv_png
  OUTPUT_VARIABLE _out_png
  ERROR_VARIABLE _err_png
)
if(NOT _rv_png EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer png summary failed (${_rv_png})\nstdout:\n${_out_png}\nstderr:\n${_err_png}")
endif()
if(NOT _out_png MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "python metatransfer png summary missing compile ok\nstdout:\n${_out_png}\nstderr:\n${_err_png}")
endif()
if(NOT _out_png MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "python metatransfer png summary missing emit ok\nstdout:\n${_out_png}\nstderr:\n${_err_png}")
endif()
if(NOT _out_png MATCHES "png_chunk eXIf count=1")
  message(FATAL_ERROR
    "python metatransfer png summary missing eXIf chunk summary\nstdout:\n${_out_png}\nstderr:\n${_err_png}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --source-meta "${_src_jpg}"
          --target-png
          --no-xmp
          --no-icc
          --no-iptc
          --output "${_edited_png}" --force
          "${_target_png}"
  RESULT_VARIABLE _rv_png_edit
  OUTPUT_VARIABLE _out_png_edit
  ERROR_VARIABLE _err_png_edit
)
if(NOT _rv_png_edit EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer png edit failed (${_rv_png_edit})\nstdout:\n${_out_png_edit}\nstderr:\n${_err_png_edit}")
endif()
if(NOT _out_png_edit MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "python metatransfer png edit missing edit_plan ok\nstdout:\n${_out_png_edit}\nstderr:\n${_err_png_edit}")
endif()
if(NOT _out_png_edit MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer png edit missing edit_apply ok\nstdout:\n${_out_png_edit}\nstderr:\n${_err_png_edit}")
endif()
if(NOT EXISTS "${_edited_png}")
  message(FATAL_ERROR
    "python metatransfer png edit did not write output\nstdout:\n${_out_png_edit}\nstderr:\n${_err_png_edit}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-png
          --no-xmp
          --no-icc
          --no-iptc
          "${_edited_png}"
  RESULT_VARIABLE _rv_png_roundtrip
  OUTPUT_VARIABLE _out_png_roundtrip
  ERROR_VARIABLE _err_png_roundtrip
)
if(NOT _rv_png_roundtrip EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer png roundtrip summary failed (${_rv_png_roundtrip})\nstdout:\n${_out_png_roundtrip}\nstderr:\n${_err_png_roundtrip}")
endif()
if(NOT _out_png_roundtrip MATCHES "png_chunk eXIf count=1")
  message(FATAL_ERROR
    "python metatransfer png roundtrip summary missing eXIf chunk summary\nstdout:\n${_out_png_roundtrip}\nstderr:\n${_err_png_roundtrip}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
          "exif_only_no_xmp" "${_edited_png}" "2000:01:02 03:04:05"
  RESULT_VARIABLE _rv_png_check
  OUTPUT_VARIABLE _out_png_check
  ERROR_VARIABLE _err_png_check
)
if(NOT _rv_png_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer png read-back check failed (${_rv_png_check})\nstdout:\n${_out_png_edit}\nstderr:\n${_err_png_edit}\ncheck_stdout:\n${_out_png_check}\ncheck_stderr:\n${_err_png_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-webp
          --no-xmp
          --no-icc
          --no-iptc
          "${_src_jpg}"
  RESULT_VARIABLE _rv_webp
  OUTPUT_VARIABLE _out_webp
  ERROR_VARIABLE _err_webp
)
if(NOT _rv_webp EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer webp summary failed (${_rv_webp})\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()
if(NOT _out_webp MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "python metatransfer webp summary missing compile ok\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()
if(NOT _out_webp MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "python metatransfer webp summary missing emit ok\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()
if(NOT _out_webp MATCHES "webp_chunk EXIF count=1")
  message(FATAL_ERROR
    "python metatransfer webp summary missing EXIF chunk summary\nstdout:\n${_out_webp}\nstderr:\n${_err_webp}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --source-meta "${_src_jpg}"
          --target-webp
          --no-xmp
          --no-icc
          --no-iptc
          --output "${_edited_webp}" --force
          "${_target_webp}"
  RESULT_VARIABLE _rv_webp_edit
  OUTPUT_VARIABLE _out_webp_edit
  ERROR_VARIABLE _err_webp_edit
)
if(NOT _rv_webp_edit EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer webp edit failed (${_rv_webp_edit})\nstdout:\n${_out_webp_edit}\nstderr:\n${_err_webp_edit}")
endif()
if(NOT _out_webp_edit MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "python metatransfer webp edit missing edit_plan ok\nstdout:\n${_out_webp_edit}\nstderr:\n${_err_webp_edit}")
endif()
if(NOT _out_webp_edit MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer webp edit missing edit_apply ok\nstdout:\n${_out_webp_edit}\nstderr:\n${_err_webp_edit}")
endif()
if(NOT EXISTS "${_edited_webp}")
  message(FATAL_ERROR
    "python metatransfer webp edit did not write output\nstdout:\n${_out_webp_edit}\nstderr:\n${_err_webp_edit}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-webp
          --no-xmp
          --no-icc
          --no-iptc
          "${_edited_webp}"
  RESULT_VARIABLE _rv_webp_roundtrip
  OUTPUT_VARIABLE _out_webp_roundtrip
  ERROR_VARIABLE _err_webp_roundtrip
)
if(NOT _rv_webp_roundtrip EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer webp roundtrip summary failed (${_rv_webp_roundtrip})\nstdout:\n${_out_webp_roundtrip}\nstderr:\n${_err_webp_roundtrip}")
endif()
if(NOT _out_webp_roundtrip MATCHES "webp_chunk EXIF count=1")
  message(FATAL_ERROR
    "python metatransfer webp roundtrip summary missing EXIF chunk summary\nstdout:\n${_out_webp_roundtrip}\nstderr:\n${_err_webp_roundtrip}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
          "exif_only_no_xmp" "${_edited_webp}" "2000:01:02 03:04:05"
  RESULT_VARIABLE _rv_webp_check
  OUTPUT_VARIABLE _out_webp_check
  ERROR_VARIABLE _err_webp_check
)
if(NOT _rv_webp_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer webp read-back check failed (${_rv_webp_check})\nstdout:\n${_out_webp_edit}\nstderr:\n${_err_webp_edit}\ncheck_stdout:\n${_out_webp_check}\ncheck_stderr:\n${_err_webp_check}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-avif
          --no-xmp
          --no-icc
          --no-iptc
          "${_src_jpg}"
  RESULT_VARIABLE _rv_avif
  OUTPUT_VARIABLE _out_avif
  ERROR_VARIABLE _err_avif
)
if(NOT _rv_avif EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer avif summary failed (${_rv_avif})\nstdout:\n${_out_avif}\nstderr:\n${_err_avif}")
endif()
if(NOT _out_avif MATCHES "compile: status=ok")
  message(FATAL_ERROR
    "python metatransfer avif summary missing compile ok\nstdout:\n${_out_avif}\nstderr:\n${_err_avif}")
endif()
if(NOT _out_avif MATCHES "emit: status=ok")
  message(FATAL_ERROR
    "python metatransfer avif summary missing emit ok\nstdout:\n${_out_avif}\nstderr:\n${_err_avif}")
endif()
if(NOT _out_avif MATCHES "bmff_item Exif count=1")
  message(FATAL_ERROR
    "python metatransfer avif summary missing Exif item summary\nstdout:\n${_out_avif}\nstderr:\n${_err_avif}")
endif()

set(_avif_target "${WORK_DIR}/avif_target.bin")
execute_process(
  COMMAND "${OPENMETA_PYTHON_EXECUTABLE}" -c "from pathlib import Path; Path(r'${_avif_target}').write_bytes(bytes.fromhex('000000186674797068656963000000006d696631686569630000000c6d64617411223344'))"
  RESULT_VARIABLE _rv_avif_target
  OUTPUT_VARIABLE _out_avif_target
  ERROR_VARIABLE _err_avif_target
)
if(NOT _rv_avif_target EQUAL 0)
  message(FATAL_ERROR
    "failed to create AVIF target file (${_rv_avif_target})\nstdout:\n${_out_avif_target}\nstderr:\n${_err_avif_target}")
endif()

set(_avif_out "${WORK_DIR}/python_metatransfer_avif_edit.bin")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --source-meta "${_src_jpg}"
          --target-avif
          --no-xmp
          --no-icc
          --no-iptc
          --output "${_avif_out}" --force
          "${_avif_target}"
  RESULT_VARIABLE _rv_avif_edit
  OUTPUT_VARIABLE _out_avif_edit
  ERROR_VARIABLE _err_avif_edit
)
if(NOT _rv_avif_edit EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer avif edit failed (${_rv_avif_edit})\nstdout:\n${_out_avif_edit}\nstderr:\n${_err_avif_edit}")
endif()
if(NOT _out_avif_edit MATCHES "edit_plan: status=ok")
  message(FATAL_ERROR
    "python metatransfer avif edit missing edit_plan ok\nstdout:\n${_out_avif_edit}\nstderr:\n${_err_avif_edit}")
endif()
if(NOT _out_avif_edit MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer avif edit missing edit_apply ok\nstdout:\n${_out_avif_edit}\nstderr:\n${_err_avif_edit}")
endif()
if(NOT EXISTS "${_avif_out}")
  message(FATAL_ERROR
    "python metatransfer avif edit did not write output\nstdout:\n${_out_avif_edit}\nstderr:\n${_err_avif_edit}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-avif
          --no-xmp
          --no-icc
          --no-iptc
          "${_avif_out}"
  RESULT_VARIABLE _rv_avif_roundtrip
  OUTPUT_VARIABLE _out_avif_roundtrip
  ERROR_VARIABLE _err_avif_roundtrip
)
if(NOT _rv_avif_roundtrip EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer avif roundtrip summary failed (${_rv_avif_roundtrip})\nstdout:\n${_out_avif_roundtrip}\nstderr:\n${_err_avif_roundtrip}")
endif()
if(NOT _out_avif_roundtrip MATCHES "bmff_item Exif count=1")
  message(FATAL_ERROR
    "python metatransfer avif roundtrip summary missing Exif item summary\nstdout:\n${_out_avif_roundtrip}\nstderr:\n${_err_avif_roundtrip}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" "${_check_readback_py}"
          "bmff" "${_avif_out}" "2000:01:02 03:04:05"
  RESULT_VARIABLE _rv_avif_check
  OUTPUT_VARIABLE _out_avif_check
  ERROR_VARIABLE _err_avif_check
)
if(NOT _rv_avif_check EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer avif read-back check failed (${_rv_avif_check})\nstdout:\n${_out_avif_edit}\nstderr:\n${_err_avif_edit}\ncheck_stdout:\n${_out_avif_check}\ncheck_stderr:\n${_err_avif_check}")
endif()

_run_bmff_dual_write_xmp_smoke(
  "heif" "--target-heif" "${_target_heif_xmp}" "${_edited_heif_xmp}"
  "${_edited_heif_xmp_sidecar}")
_run_bmff_dual_write_xmp_smoke(
  "avif" "--target-avif" "${_target_avif_xmp}" "${_edited_avif_xmp}"
  "${_edited_avif_xmp_sidecar}")
_run_bmff_dual_write_xmp_smoke(
  "cr3" "--target-cr3" "${_target_cr3_xmp}" "${_edited_cr3_xmp}"
  "${_edited_cr3_xmp_sidecar}")
_run_bmff_embedded_xmp_smoke(
  "heif" "--target-heif" "${_target_heif_xmp}" "${_edited_heif_xmp_embedded}"
  "${_edited_heif_xmp_embedded_sidecar}")
_run_bmff_embedded_xmp_smoke(
  "avif" "--target-avif" "${_target_avif_xmp}" "${_edited_avif_xmp_embedded}"
  "${_edited_avif_xmp_embedded_sidecar}")
_run_bmff_embedded_xmp_smoke(
  "cr3" "--target-cr3" "${_target_cr3_xmp}" "${_edited_cr3_xmp_embedded}"
  "${_edited_cr3_xmp_embedded_sidecar}")
_write_bmff_existing_embedded_xmp_target(
  "heif" "--target-heif" "${_target_heif_xmp}" "${_target_heif_existing_xmp}")
_write_bmff_existing_embedded_xmp_target(
  "avif" "--target-avif" "${_target_avif_xmp}" "${_target_avif_existing_xmp}")
_write_bmff_existing_embedded_xmp_target(
  "cr3" "--target-cr3" "${_target_cr3_xmp}" "${_target_cr3_existing_xmp}")
_run_bmff_mixed_destination_carrier_precedence_smoke(
  "heif default-carrier" "--target-heif" "${_target_heif_existing_xmp}"
  "${_mixed_heif_sidecar_wins}" "sidecar_wins" "Target Sidecar Existing"
  "Target Embedded Existing" "OpenMeta Transfer Source")
_run_bmff_mixed_destination_carrier_precedence_smoke(
  "avif default-carrier" "--target-avif" "${_target_avif_existing_xmp}"
  "${_mixed_avif_sidecar_wins}" "sidecar_wins" "Target Sidecar Existing"
  "Target Embedded Existing" "OpenMeta Transfer Source")
_run_bmff_mixed_destination_carrier_precedence_smoke(
  "cr3 default-carrier" "--target-cr3" "${_target_cr3_existing_xmp}"
  "${_mixed_cr3_sidecar_wins}" "sidecar_wins" "Target Sidecar Existing"
  "Target Embedded Existing" "OpenMeta Transfer Source")
_run_bmff_mixed_destination_carrier_precedence_smoke(
  "heif embedded-wins" "--target-heif" "${_target_heif_existing_xmp}"
  "${_mixed_heif_embedded_wins}" "embedded_wins"
  "Target Embedded Existing" "Target Sidecar Existing"
  "OpenMeta Transfer Source")
_run_bmff_mixed_destination_carrier_precedence_smoke(
  "avif embedded-wins" "--target-avif" "${_target_avif_existing_xmp}"
  "${_mixed_avif_embedded_wins}" "embedded_wins"
  "Target Embedded Existing" "Target Sidecar Existing"
  "OpenMeta Transfer Source")
_run_bmff_mixed_destination_carrier_precedence_smoke(
  "cr3 embedded-wins" "--target-cr3" "${_target_cr3_existing_xmp}"
  "${_mixed_cr3_embedded_wins}" "embedded_wins"
  "Target Embedded Existing" "Target Sidecar Existing"
  "OpenMeta Transfer Source")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-avif
          --no-exif
          --no-xmp
          --no-iptc
          "${_src_icc_jpg}"
  RESULT_VARIABLE _rv_avif_icc
  OUTPUT_VARIABLE _out_avif_icc
  ERROR_VARIABLE _err_avif_icc
)
if(NOT _rv_avif_icc EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer avif icc summary failed (${_rv_avif_icc})\nstdout:\n${_out_avif_icc}\nstderr:\n${_err_avif_icc}")
endif()
if(NOT _out_avif_icc MATCHES "bmff_property colr/prof count=1")
  message(FATAL_ERROR
    "python metatransfer avif icc summary missing colr/prof property\nstdout:\n${_out_avif_icc}\nstderr:\n${_err_avif_icc}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --dump-transfer-payload-batch "${_transfer_payload_batch}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_payload_batch
  OUTPUT_VARIABLE _out_payload_batch
  ERROR_VARIABLE _err_payload_batch
)
if(NOT _rv_payload_batch EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer payload batch dump failed (${_rv_payload_batch})\nstdout:\n${_out_payload_batch}\nstderr:\n${_err_payload_batch}")
endif()
if(NOT _out_payload_batch MATCHES "transfer_payload_batch: status=ok")
  message(FATAL_ERROR
    "python metatransfer payload batch dump missing status ok\nstdout:\n${_out_payload_batch}\nstderr:\n${_err_payload_batch}")
endif()
if(NOT EXISTS "${_transfer_payload_batch}")
  message(FATAL_ERROR
    "python metatransfer payload batch dump did not write output\nstdout:\n${_out_payload_batch}\nstderr:\n${_err_payload_batch}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --load-transfer-payload-batch "${_transfer_payload_batch}"
  RESULT_VARIABLE _rv_payload_batch_load
  OUTPUT_VARIABLE _out_payload_batch_load
  ERROR_VARIABLE _err_payload_batch_load
)
if(NOT _rv_payload_batch_load EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer payload batch load failed (${_rv_payload_batch_load})\nstdout:\n${_out_payload_batch_load}\nstderr:\n${_err_payload_batch_load}")
endif()
if(NOT _out_payload_batch_load MATCHES "transfer_payload_batch: status=ok code=none bytes=[0-9]+ payloads=[0-9]+ target=jpeg")
  message(FATAL_ERROR
    "python metatransfer payload batch load missing summary\nstdout:\n${_out_payload_batch_load}\nstderr:\n${_err_payload_batch_load}")
endif()
if(NOT _out_payload_batch_load MATCHES "\\[0\\] semantic=Exif route=jpeg:app1-exif")
  message(FATAL_ERROR
    "python metatransfer payload batch load missing first payload summary\nstdout:\n${_out_payload_batch_load}\nstderr:\n${_err_payload_batch_load}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --load-transfer-artifact "${_transfer_payload_batch}"
  RESULT_VARIABLE _rv_artifact_payload_load
  OUTPUT_VARIABLE _out_artifact_payload_load
  ERROR_VARIABLE _err_artifact_payload_load
)
if(NOT _rv_artifact_payload_load EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer generic artifact load for payload batch failed (${_rv_artifact_payload_load})\nstdout:\n${_out_artifact_payload_load}\nstderr:\n${_err_artifact_payload_load}")
endif()
if(NOT _out_artifact_payload_load MATCHES "transfer_artifact: status=ok code=none kind=transfer_payload_batch bytes=[0-9]+ target=jpeg")
  message(FATAL_ERROR
    "python metatransfer generic artifact load missing payload-batch summary\nstdout:\n${_out_artifact_payload_load}\nstderr:\n${_err_artifact_payload_load}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --dump-transfer-package-batch "${_transfer_package_batch}"
          "${_src_jpg}"
  RESULT_VARIABLE _rv_package_batch
  OUTPUT_VARIABLE _out_package_batch
  ERROR_VARIABLE _err_package_batch
)
if(NOT _rv_package_batch EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer package batch dump failed (${_rv_package_batch})\nstdout:\n${_out_package_batch}\nstderr:\n${_err_package_batch}")
endif()
if(NOT _out_package_batch MATCHES "transfer_package_batch: status=ok")
  message(FATAL_ERROR
    "python metatransfer package batch dump missing status ok\nstdout:\n${_out_package_batch}\nstderr:\n${_err_package_batch}")
endif()
if(NOT EXISTS "${_transfer_package_batch}")
  message(FATAL_ERROR
    "python metatransfer package batch dump did not write output\nstdout:\n${_out_package_batch}\nstderr:\n${_err_package_batch}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --load-transfer-package-batch "${_transfer_package_batch}"
  RESULT_VARIABLE _rv_package_batch_load
  OUTPUT_VARIABLE _out_package_batch_load
  ERROR_VARIABLE _err_package_batch_load
)
if(NOT _rv_package_batch_load EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer package batch load failed (${_rv_package_batch_load})\nstdout:\n${_out_package_batch_load}\nstderr:\n${_err_package_batch_load}")
endif()
if(NOT _out_package_batch_load MATCHES "transfer_package_batch: status=ok code=none bytes=[0-9]+ chunks=[0-9]+ target=jpeg")
  message(FATAL_ERROR
    "python metatransfer package batch load missing summary\nstdout:\n${_out_package_batch_load}\nstderr:\n${_err_package_batch_load}")
endif()
if(NOT _out_package_batch_load MATCHES "\\[0\\] semantic=Exif route=jpeg:app1-exif")
  message(FATAL_ERROR
    "python metatransfer package batch load missing first chunk summary\nstdout:\n${_out_package_batch_load}\nstderr:\n${_err_package_batch_load}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-handoff "${_c2pa_handoff}"
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_handoff
  OUTPUT_VARIABLE _out_handoff
  ERROR_VARIABLE _err_handoff
)
if(NOT _rv_handoff EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer c2pa handoff dump failed (${_rv_handoff})\nstdout:\n${_out_handoff}\nstderr:\n${_err_handoff}")
endif()
if(NOT _out_handoff MATCHES "c2pa_handoff: status=ok")
  message(FATAL_ERROR
    "python metatransfer c2pa handoff dump missing status ok\nstdout:\n${_out_handoff}\nstderr:\n${_err_handoff}")
endif()
if(NOT EXISTS "${_c2pa_handoff}")
  message(FATAL_ERROR
    "python metatransfer c2pa handoff dump did not write output\nstdout:\n${_out_handoff}\nstderr:\n${_err_handoff}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --jpeg-c2pa-signed "${_c2pa_signed_jumb}"
          --c2pa-manifest-output "${_c2pa_manifest}"
          --c2pa-certificate-chain "${_c2pa_cert}"
          --c2pa-key-ref "test-key-ref"
          --c2pa-signing-time "2026-03-09T00:00:00Z"
          --dump-c2pa-signed-package "${_c2pa_signed_package}"
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_signed_package
  OUTPUT_VARIABLE _out_signed_package
  ERROR_VARIABLE _err_signed_package
)
if(NOT _rv_signed_package EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer signed c2pa package dump failed (${_rv_signed_package})\nstdout:\n${_out_signed_package}\nstderr:\n${_err_signed_package}")
endif()
if(NOT _out_signed_package MATCHES "c2pa_signed_package: status=ok")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package dump missing status ok\nstdout:\n${_out_signed_package}\nstderr:\n${_err_signed_package}")
endif()
if(NOT _out_signed_package MATCHES "c2pa_stage_semantics: status=ok reason=ok manifest=1 manifests=1 claim_generator=1 assertions=1 claims=1 signatures=1 linked=1 orphan=0 explicit_refs=0 unresolved=0 ambiguous=0")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package dump missing semantic summary\nstdout:\n${_out_signed_package}\nstderr:\n${_err_signed_package}")
endif()
if(NOT _out_signed_package MATCHES "c2pa_stage_linkage: claim0_assertions=1 claim0_refs=1 sig0_links=1")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package dump missing linkage summary\nstdout:\n${_out_signed_package}\nstderr:\n${_err_signed_package}")
endif()
if(NOT EXISTS "${_c2pa_signed_package}")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package dump did not write output\nstdout:\n${_out_signed_package}\nstderr:\n${_err_signed_package}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --load-c2pa-signed-package "${_c2pa_signed_package}"
          --target-jpeg "${_c2pa_jpg}"
          -o "${_c2pa_from_package}"
          "${_c2pa_jpg}"
  RESULT_VARIABLE _rv_from_package
  OUTPUT_VARIABLE _out_from_package
  ERROR_VARIABLE _err_from_package
)
if(NOT _rv_from_package EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer signed c2pa package apply failed (${_rv_from_package})\nstdout:\n${_out_from_package}\nstderr:\n${_err_from_package}")
endif()
if(NOT _out_from_package MATCHES "c2pa_stage: status=ok")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package apply missing stage ok\nstdout:\n${_out_from_package}\nstderr:\n${_err_from_package}")
endif()
if(NOT _out_from_package MATCHES "c2pa_stage_semantics: status=ok reason=ok manifest=1 manifests=1 claim_generator=1 assertions=1 claims=1 signatures=1 linked=1 orphan=0 explicit_refs=0 unresolved=0 ambiguous=0")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package apply missing semantic summary\nstdout:\n${_out_from_package}\nstderr:\n${_err_from_package}")
endif()
if(NOT _out_from_package MATCHES "c2pa_stage_linkage: claim0_assertions=1 claim0_refs=1 sig0_links=1")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package apply missing linkage summary\nstdout:\n${_out_from_package}\nstderr:\n${_err_from_package}")
endif()
if(NOT _out_from_package MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package apply missing edit ok\nstdout:\n${_out_from_package}\nstderr:\n${_err_from_package}")
endif()
if(NOT EXISTS "${_c2pa_from_package}")
  message(FATAL_ERROR
    "python metatransfer signed c2pa package apply did not write output\nstdout:\n${_out_from_package}\nstderr:\n${_err_from_package}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jxl
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-binding "${_c2pa_jxl_binding}"
          "${_c2pa_jxl}"
  RESULT_VARIABLE _rv_jxl_binding
  OUTPUT_VARIABLE _out_jxl_binding
  ERROR_VARIABLE _err_jxl_binding
)
if(NOT _rv_jxl_binding EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl c2pa binding dump failed (${_rv_jxl_binding})\nstdout:\n${_out_jxl_binding}\nstderr:\n${_err_jxl_binding}")
endif()
if(NOT _out_jxl_binding MATCHES "c2pa_binding: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl c2pa binding dump missing status ok\nstdout:\n${_out_jxl_binding}\nstderr:\n${_err_jxl_binding}")
endif()
if(NOT EXISTS "${_c2pa_jxl_binding}")
  message(FATAL_ERROR
    "python metatransfer jxl c2pa binding dump did not write output\nstdout:\n${_out_jxl_binding}\nstderr:\n${_err_jxl_binding}")
endif()
file(READ "${_c2pa_jxl_binding}" _c2pa_jxl_binding_hex HEX)
if(NOT _c2pa_jxl_binding_hex STREQUAL "0000000c4a584c200d0a870a0000000c6a786c6311223344")
  message(FATAL_ERROR
    "python metatransfer jxl c2pa binding bytes mismatch: ${_c2pa_jxl_binding_hex}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-jxl
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --jpeg-c2pa-signed "${_c2pa_signed_jumb}"
          --c2pa-manifest-output "${_c2pa_manifest}"
          --c2pa-certificate-chain "${_c2pa_cert}"
          --c2pa-key-ref "test-key-ref"
          --c2pa-signing-time "2026-03-09T00:00:00Z"
          --dump-c2pa-signed-package "${_c2pa_jxl_signed_package}"
          --output "${_c2pa_jxl_out}"
          "${_c2pa_jxl}"
  RESULT_VARIABLE _rv_jxl_signed_package
  OUTPUT_VARIABLE _out_jxl_signed_package
  ERROR_VARIABLE _err_jxl_signed_package
)
if(NOT _rv_jxl_signed_package EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer jxl signed c2pa package dump failed (${_rv_jxl_signed_package})\nstdout:\n${_out_jxl_signed_package}\nstderr:\n${_err_jxl_signed_package}")
endif()
if(NOT _out_jxl_signed_package MATCHES "c2pa_signed_package: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl signed c2pa package dump missing status ok\nstdout:\n${_out_jxl_signed_package}\nstderr:\n${_err_jxl_signed_package}")
endif()
if(NOT _out_jxl_signed_package MATCHES "c2pa_stage: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl signed c2pa package dump missing stage ok\nstdout:\n${_out_jxl_signed_package}\nstderr:\n${_err_jxl_signed_package}")
endif()
if(NOT _out_jxl_signed_package MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer jxl signed c2pa package dump missing edit ok\nstdout:\n${_out_jxl_signed_package}\nstderr:\n${_err_jxl_signed_package}")
endif()
if(NOT EXISTS "${_c2pa_jxl_signed_package}")
  message(FATAL_ERROR
    "python metatransfer jxl signed c2pa package dump did not write output\nstdout:\n${_out_jxl_signed_package}\nstderr:\n${_err_jxl_signed_package}")
endif()
if(NOT EXISTS "${_c2pa_jxl_out}")
  message(FATAL_ERROR
    "python metatransfer jxl signed c2pa edit did not write output\nstdout:\n${_out_jxl_signed_package}\nstderr:\n${_err_jxl_signed_package}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-heif
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-binding "${_c2pa_heif_binding}"
          "${_c2pa_heif}"
  RESULT_VARIABLE _rv_heif_binding
  OUTPUT_VARIABLE _out_heif_binding
  ERROR_VARIABLE _err_heif_binding
)
if(NOT _rv_heif_binding EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer heif c2pa binding dump failed (${_rv_heif_binding})\nstdout:\n${_out_heif_binding}\nstderr:\n${_err_heif_binding}")
endif()
if(NOT _out_heif_binding MATCHES "c2pa_binding: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif c2pa binding dump missing status ok\nstdout:\n${_out_heif_binding}\nstderr:\n${_err_heif_binding}")
endif()
if(NOT EXISTS "${_c2pa_heif_binding}")
  message(FATAL_ERROR
    "python metatransfer heif c2pa binding dump did not write output\nstdout:\n${_out_heif_binding}\nstderr:\n${_err_heif_binding}")
endif()
file(READ "${_c2pa_heif_binding}" _c2pa_heif_binding_hex HEX)
if(NOT _c2pa_heif_binding_hex STREQUAL "000000186674797068656963000000006d696631686569630000000c6d64617411223344")
  message(FATAL_ERROR
    "python metatransfer heif c2pa binding bytes mismatch: ${_c2pa_heif_binding_hex}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-heif
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --c2pa-policy rewrite
          --dump-c2pa-handoff "${_c2pa_heif_handoff}"
          "${_c2pa_heif}"
  RESULT_VARIABLE _rv_heif_handoff
  OUTPUT_VARIABLE _out_heif_handoff
  ERROR_VARIABLE _err_heif_handoff
)
if(NOT _rv_heif_handoff EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer heif c2pa handoff dump failed (${_rv_heif_handoff})\nstdout:\n${_out_heif_handoff}\nstderr:\n${_err_heif_handoff}")
endif()
if(NOT _out_heif_handoff MATCHES "c2pa_handoff: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif c2pa handoff dump missing status ok\nstdout:\n${_out_heif_handoff}\nstderr:\n${_err_heif_handoff}")
endif()
if(NOT EXISTS "${_c2pa_heif_handoff}")
  message(FATAL_ERROR
    "python metatransfer heif c2pa handoff dump did not write output\nstdout:\n${_out_heif_handoff}\nstderr:\n${_err_heif_handoff}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-heif
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --jpeg-c2pa-signed "${_c2pa_signed_jumb}"
          --c2pa-manifest-output "${_c2pa_manifest}"
          --c2pa-certificate-chain "${_c2pa_cert}"
          --c2pa-key-ref "test-key-ref"
          --c2pa-signing-time "2026-03-09T00:00:00Z"
          --dump-c2pa-signed-package "${_c2pa_heif_signed_package}"
          --output "${_c2pa_heif_out}"
          "${_c2pa_heif}"
  RESULT_VARIABLE _rv_heif_signed_package
  OUTPUT_VARIABLE _out_heif_signed_package
  ERROR_VARIABLE _err_heif_signed_package
)
if(NOT _rv_heif_signed_package EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package dump failed (${_rv_heif_signed_package})\nstdout:\n${_out_heif_signed_package}\nstderr:\n${_err_heif_signed_package}")
endif()
if(NOT _out_heif_signed_package MATCHES "c2pa_signed_package: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package dump missing status ok\nstdout:\n${_out_heif_signed_package}\nstderr:\n${_err_heif_signed_package}")
endif()
if(NOT _out_heif_signed_package MATCHES "c2pa_stage: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package dump missing stage ok\nstdout:\n${_out_heif_signed_package}\nstderr:\n${_err_heif_signed_package}")
endif()
if(NOT _out_heif_signed_package MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package dump missing bmff edit ok\nstdout:\n${_out_heif_signed_package}\nstderr:\n${_err_heif_signed_package}")
endif()
if(NOT EXISTS "${_c2pa_heif_signed_package}")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package dump did not write output\nstdout:\n${_out_heif_signed_package}\nstderr:\n${_err_heif_signed_package}")
endif()
if(NOT EXISTS "${_c2pa_heif_out}")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa edit did not write output\nstdout:\n${_out_heif_signed_package}\nstderr:\n${_err_heif_signed_package}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -m openmeta.python.metatransfer
          --no-build-info
          --target-heif
          --no-exif
          --no-xmp
          --no-icc
          --no-iptc
          --load-c2pa-signed-package "${_c2pa_heif_signed_package}"
          --output "${_c2pa_heif_from_package}"
          "${_c2pa_heif}"
  RESULT_VARIABLE _rv_heif_from_package
  OUTPUT_VARIABLE _out_heif_from_package
  ERROR_VARIABLE _err_heif_from_package
)
if(NOT _rv_heif_from_package EQUAL 0)
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package apply failed (${_rv_heif_from_package})\nstdout:\n${_out_heif_from_package}\nstderr:\n${_err_heif_from_package}")
endif()
if(NOT _out_heif_from_package MATCHES "c2pa_stage: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package apply missing stage ok\nstdout:\n${_out_heif_from_package}\nstderr:\n${_err_heif_from_package}")
endif()
if(NOT _out_heif_from_package MATCHES "edit_apply: status=ok")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package apply missing bmff edit ok\nstdout:\n${_out_heif_from_package}\nstderr:\n${_err_heif_from_package}")
endif()
if(NOT EXISTS "${_c2pa_heif_from_package}")
  message(FATAL_ERROR
    "python metatransfer heif signed c2pa package apply did not write output\nstdout:\n${_out_heif_from_package}\nstderr:\n${_err_heif_from_package}")
endif()

message(STATUS "python metatransfer edit smoke gate passed")
