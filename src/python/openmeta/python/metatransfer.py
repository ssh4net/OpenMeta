#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

try:
    import openmeta
except ModuleNotFoundError:
    sys.stderr.write("error: Python module 'openmeta' was not found.\n")
    sys.stderr.write(
        "Run this script with the same Python environment where the OpenMeta wheel is installed.\n"
    )
    sys.stderr.write("\n")
    sys.stderr.write("Examples:\n")
    sys.stderr.write("  uv run python -m openmeta.python.metatransfer <file>\n")
    sys.stderr.write(
        "  /path/to/venv/bin/python -m openmeta.python.metatransfer <file>\n"
    )
    raise SystemExit(2)


def _parse_status_maps() -> tuple[
    dict[object, str], dict[object, str], dict[object, str], dict[object, str], dict[object, str]
]:
    scan = {
        openmeta.ScanStatus.Ok: "ok",
        openmeta.ScanStatus.OutputTruncated: "output_truncated",
        openmeta.ScanStatus.Unsupported: "unsupported",
        openmeta.ScanStatus.Malformed: "malformed",
    }
    payload = {
        openmeta.PayloadStatus.Ok: "ok",
        openmeta.PayloadStatus.OutputTruncated: "output_truncated",
        openmeta.PayloadStatus.Unsupported: "unsupported",
        openmeta.PayloadStatus.Malformed: "malformed",
        openmeta.PayloadStatus.LimitExceeded: "limit_exceeded",
    }
    exif = {
        openmeta.ExifDecodeStatus.Ok: "ok",
        openmeta.ExifDecodeStatus.OutputTruncated: "output_truncated",
        openmeta.ExifDecodeStatus.Unsupported: "unsupported",
        openmeta.ExifDecodeStatus.Malformed: "malformed",
        openmeta.ExifDecodeStatus.LimitExceeded: "limit_exceeded",
    }
    xmp = {
        openmeta.XmpDecodeStatus.Ok: "ok",
        openmeta.XmpDecodeStatus.OutputTruncated: "output_truncated",
        openmeta.XmpDecodeStatus.Unsupported: "unsupported",
        openmeta.XmpDecodeStatus.Malformed: "malformed",
        openmeta.XmpDecodeStatus.LimitExceeded: "limit_exceeded",
    }
    jumbf = {
        openmeta.JumbfDecodeStatus.Ok: "ok",
        openmeta.JumbfDecodeStatus.Unsupported: "unsupported",
        openmeta.JumbfDecodeStatus.Malformed: "malformed",
        openmeta.JumbfDecodeStatus.LimitExceeded: "limit_exceeded",
    }
    return scan, payload, exif, xmp, jumbf


def _transfer_kind_name(kind: object) -> str:
    mapping = {
        openmeta.TransferBlockKind.Exif: "exif",
        openmeta.TransferBlockKind.Xmp: "xmp",
        openmeta.TransferBlockKind.IptcIim: "iptc_iim",
        openmeta.TransferBlockKind.PhotoshopIrb: "photoshop_irb",
        openmeta.TransferBlockKind.Icc: "icc",
        openmeta.TransferBlockKind.Jumbf: "jumbf",
        openmeta.TransferBlockKind.C2pa: "c2pa",
        openmeta.TransferBlockKind.ExrAttribute: "exr_attribute",
        openmeta.TransferBlockKind.Other: "other",
    }
    return mapping.get(kind, "unknown")


def _transfer_policy_action(name: str) -> object:
    mapping = {
        "keep": openmeta.TransferPolicyAction.Keep,
        "drop": openmeta.TransferPolicyAction.Drop,
        "invalidate": openmeta.TransferPolicyAction.Invalidate,
        "rewrite": openmeta.TransferPolicyAction.Rewrite,
    }
    return mapping[name]


def _transfer_safety_mode(name: str) -> object:
    mapping = {
        "compatible": openmeta.TransferSafetyMode.CompatibleFile,
        "rendered": openmeta.TransferSafetyMode.RenderedImage,
    }
    return mapping[name]


def _marker_name(marker: int) -> str:
    if 0xE0 <= marker <= 0xEF:
        return f"APP{marker - 0xE0}(0x{marker:02X})"
    if marker == 0xFE:
        return "COM(0xFE)"
    return f"0x{marker:02X}"


def _bmff_item_name(item_type: int, mime_xmp: bool) -> str:
    if mime_xmp:
        return "mime/xmp"
    return "".join(chr((item_type >> shift) & 0xFF) for shift in (24, 16, 8, 0))


def _bmff_property_name(property_type: int, property_subtype: int) -> str:
    return (
        "".join(chr((property_type >> shift) & 0xFF) for shift in (24, 16, 8, 0))
        + "/"
        + "".join(
            chr((property_subtype >> shift) & 0xFF) for shift in (24, 16, 8, 0)
        )
    )


def _transfer_payload_op_summary(payload: dict[object, object]) -> str:
    op_kind = str(payload["op_kind_name"])
    if op_kind == "jpeg_marker":
        return f"{op_kind} marker={_marker_name(int(payload['jpeg_marker_code']))}"
    if op_kind == "tiff_tag":
        return f"{op_kind} tag=0x{int(payload['tiff_tag']):04X}"
    if op_kind == "jxl_box":
        return f"{op_kind} type={payload['box_type']}"
    if op_kind == "jp2_box":
        return f"{op_kind} type={payload['box_type']}"
    if op_kind == "webp_chunk":
        return f"{op_kind} type={payload['chunk_type']}"
    if op_kind == "png_chunk":
        return f"{op_kind} type={payload['chunk_type']}"
    if op_kind == "bmff_item":
        return (
            f"{op_kind} type="
            f"{_bmff_item_name(int(payload['bmff_item_type']), bool(payload['bmff_mime_xmp']))}"
        )
    if op_kind == "bmff_property":
        return (
            f"{op_kind} type="
            f"{_bmff_property_name(int(payload['bmff_property_type']), int(payload['bmff_property_subtype']))}"
        )
    return op_kind


def _xmp_sidecar_path(path: str) -> str:
    root, ext = os.path.splitext(path)
    if ext:
        return root + ".xmp"
    return path + ".xmp"


def _transfer_package_chunk_summary(chunk: dict[object, object]) -> str:
    summary = (
        f"{chunk['package_kind_name']} offset={int(chunk['output_offset'])}"
    )
    marker = int(chunk["jpeg_marker_code"])
    if marker != 0:
        summary += f" marker={_marker_name(marker)}"
    return summary


def _sanitize_filename(s: str) -> str:
    out = []
    for ch in s:
        ok = ch.isalnum() or ch in "._-"
        out.append(ch if ok else "_")
    return "".join(out) or "file"


def _payload_dump_path(src_path: str, out_dir: str, route: str, index: int) -> str:
    base = _sanitize_filename(os.path.basename(src_path))
    rr = _sanitize_filename(route)
    name = f"{base}.meta.{index:03d}.{rr}.bin"
    if out_dir:
        return os.path.join(out_dir, name)
    return name


def _parse_time_patch_specs(specs: list[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    for spec in specs:
        if "=" not in spec:
            raise ValueError(f"invalid --time-patch '{spec}' (expected Field=Value)")
        field, value = spec.split("=", 1)
        field = field.strip()
        if not field:
            raise ValueError(f"invalid --time-patch '{spec}' (empty field)")
        out[field] = value
    return out


def _parse_u16_list(spec: str, option_name: str) -> list[int]:
    values: list[int] = []
    for raw in spec.split(","):
        text = raw.strip()
        if not text:
            raise ValueError(f"invalid {option_name} value (empty item)")
        try:
            value = int(text, 10)
        except ValueError as ex:
            raise ValueError(f"invalid {option_name} value: {text}") from ex
        if value <= 0 or value > 0xFFFF:
            raise ValueError(f"invalid {option_name} value: {text}")
        values.append(value)
    limit = int(openmeta.TRANSFER_TARGET_IMAGE_SPEC_MAX_SAMPLES)
    if len(values) > limit:
        raise ValueError(f"{option_name} accepts at most {limit} values")
    return values


def _build_target_image_spec(args: argparse.Namespace) -> object | None:
    target_fields = (
        args.target_width,
        args.target_height,
        args.target_orientation,
        args.target_samples_per_pixel,
        args.target_bits_per_sample,
        args.target_sample_format,
        args.target_photometric,
        args.target_planar_configuration,
        args.target_compression,
        args.target_exif_color_space,
    )
    if not any(value is not None for value in target_fields):
        return None
    if (args.target_width is None) != (args.target_height is None):
        raise ValueError("--target-width and --target-height must be used together")

    spec = openmeta.TransferTargetImageSpec()
    if args.target_width is not None and args.target_height is not None:
        if args.target_width <= 0 or args.target_height <= 0:
            raise ValueError("--target-width and --target-height must be non-zero")
        spec.has_dimensions = True
        spec.width = int(args.target_width)
        spec.height = int(args.target_height)
    if args.target_orientation is not None:
        if args.target_orientation < 1 or args.target_orientation > 8:
            raise ValueError("--target-orientation must be in 1..8")
        spec.has_orientation = True
        spec.orientation = int(args.target_orientation)
    if args.target_samples_per_pixel is not None:
        limit = int(openmeta.TRANSFER_TARGET_IMAGE_SPEC_MAX_SAMPLES)
        if args.target_samples_per_pixel <= 0 or args.target_samples_per_pixel > limit:
            raise ValueError(f"--target-samples-per-pixel must be in 1..{limit}")
        spec.has_samples_per_pixel = True
        spec.samples_per_pixel = int(args.target_samples_per_pixel)
    if args.target_bits_per_sample is not None:
        spec.bits_per_sample = _parse_u16_list(
            args.target_bits_per_sample, "--target-bits-per-sample"
        )
    if args.target_sample_format is not None:
        spec.sample_format = _parse_u16_list(
            args.target_sample_format, "--target-sample-format"
        )
    if args.target_photometric is not None:
        if args.target_photometric < 0 or args.target_photometric > 0xFFFF:
            raise ValueError("--target-photometric must be in 0..65535")
        spec.has_photometric_interpretation = True
        spec.photometric_interpretation = int(args.target_photometric)
    if args.target_planar_configuration is not None:
        if args.target_planar_configuration not in (1, 2):
            raise ValueError("--target-planar-configuration must be 1 or 2")
        spec.has_planar_configuration = True
        spec.planar_configuration = int(args.target_planar_configuration)
    if args.target_compression is not None:
        if args.target_compression <= 0 or args.target_compression > 0xFFFF:
            raise ValueError("--target-compression must be in 1..65535")
        spec.has_compression = True
        spec.compression = int(args.target_compression)
    if args.target_exif_color_space is not None:
        if args.target_exif_color_space <= 0 or args.target_exif_color_space > 0xFFFF:
            raise ValueError("--target-exif-color-space must be in 1..65535")
        spec.has_exif_color_space = True
        spec.exif_color_space = int(args.target_exif_color_space)
    return spec


def probe_exr_attribute_batch(
    path: str | os.PathLike[str],
    *,
    format: object = openmeta.XmpSidecarFormat.Portable,
    include_pointer_tags: bool = True,
    decode_makernote: bool = False,
    decode_embedded_containers: bool = True,
    decompress: bool = True,
    include_exif_app1: bool = True,
    include_xmp_app1: bool = True,
    include_icc_app2: bool = True,
    include_iptc_app13: bool = True,
    xmp_include_existing: bool = False,
    xmp_existing_namespace_policy: object = openmeta.XmpExistingNamespacePolicy.KnownPortableOnly,
    xmp_existing_standard_namespace_policy: object = openmeta.XmpExistingStandardNamespacePolicy.PreserveAll,
    xmp_exiftool_gpsdatetime_alias: bool = False,
    xmp_project_exif: bool = True,
    xmp_project_iptc: bool = True,
    makernote_policy: object = openmeta.TransferPolicyAction.Keep,
    jumbf_policy: object = openmeta.TransferPolicyAction.Keep,
    c2pa_policy: object = openmeta.TransferPolicyAction.Keep,
    max_file_bytes: int = 0,
    policy: object | None = None,
    include_values: bool = False,
) -> dict[object, object]:
    return openmeta.build_exr_attribute_batch_from_file(
        os.fspath(path),
        format=format,
        include_pointer_tags=include_pointer_tags,
        decode_makernote=decode_makernote,
        decode_embedded_containers=decode_embedded_containers,
        decompress=decompress,
        include_exif_app1=include_exif_app1,
        include_xmp_app1=include_xmp_app1,
        include_icc_app2=include_icc_app2,
        include_iptc_app13=include_iptc_app13,
        xmp_include_existing=xmp_include_existing,
        xmp_existing_namespace_policy=xmp_existing_namespace_policy,
        xmp_existing_standard_namespace_policy=xmp_existing_standard_namespace_policy,
        xmp_exiftool_gpsdatetime_alias=xmp_exiftool_gpsdatetime_alias,
        xmp_project_exif=xmp_project_exif,
        xmp_project_iptc=xmp_project_iptc,
        makernote_policy=makernote_policy,
        jumbf_policy=jumbf_policy,
        c2pa_policy=c2pa_policy,
        max_file_bytes=max_file_bytes,
        policy=policy,
        include_values=include_values,
    )


def get_exr_attribute_batch(
    path: str | os.PathLike[str],
    *,
    include_values: bool = False,
    **kwargs: object,
) -> list[dict[object, object]]:
    probe = probe_exr_attribute_batch(
        path,
        include_values=include_values,
        **kwargs,
    )
    batch_status = str(probe["exr_attribute_batch_status_name"])
    if batch_status != "ok":
        message = str(probe["exr_attribute_batch_message"] or "")
        raise RuntimeError(
            "failed to prepare exr attribute batch"
            + (f": {message}" if message else "")
        )
    overall_status = str(probe["overall_status_name"])
    if overall_status != "ok":
        message = str(probe["error_message"] or "")
        raise RuntimeError(
            "exr transfer probe failed"
            + (f": {message}" if message else "")
        )
    batch = probe["exr_attribute_batch"]
    if batch is None:
        raise RuntimeError("exr transfer probe returned no attribute batch")
    return list(batch)


def probe_libraw_orientation(
    path: str | os.PathLike[str],
    *,
    target: object = openmeta.LibRawOrientationTarget.RawImage,
    preserve_embedded_preview_orientation: bool = True,
    mirror_policy: object = openmeta.LibRawMirrorPolicy.Reject,
    max_file_bytes: int = 0,
) -> dict[object, object]:
    return openmeta.map_meta_orientation_to_libraw_flip_from_file(
        os.fspath(path),
        target=target,
        preserve_embedded_preview_orientation=preserve_embedded_preview_orientation,
        mirror_policy=mirror_policy,
        max_file_bytes=max_file_bytes,
    )


def get_libraw_orientation(
    path: str | os.PathLike[str],
    **kwargs: object,
) -> dict[object, object]:
    result = probe_libraw_orientation(path, **kwargs)
    if str(result["file_status_name"]) != "ok":
        raise RuntimeError(
            "failed to read file for libraw orientation: "
            + str(result["file_status_name"])
        )
    if str(result["orientation_status_name"]) != "ok":
        raise RuntimeError(
            "failed to map libraw orientation: "
            + str(result["orientation_code_name"])
        )
    return result


def update_dng_sdk_file(
    path: str | os.PathLike[str],
    target_path: str | os.PathLike[str],
    *,
    dng_target_mode: object = openmeta.DngTargetMode.MinimalFreshScaffold,
    format: object = openmeta.XmpSidecarFormat.Portable,
    include_pointer_tags: bool = True,
    decode_makernote: bool = False,
    decode_embedded_containers: bool = True,
    decompress: bool = True,
    include_exif_app1: bool = True,
    include_xmp_app1: bool = True,
    include_icc_app2: bool = True,
    include_iptc_app13: bool = True,
    xmp_include_existing: bool = False,
    xmp_existing_namespace_policy: object = openmeta.XmpExistingNamespacePolicy.KnownPortableOnly,
    xmp_existing_standard_namespace_policy: object = openmeta.XmpExistingStandardNamespacePolicy.PreserveAll,
    xmp_exiftool_gpsdatetime_alias: bool = False,
    xmp_project_exif: bool = True,
    xmp_project_iptc: bool = True,
    makernote_policy: object = openmeta.TransferPolicyAction.Keep,
    jumbf_policy: object = openmeta.TransferPolicyAction.Keep,
    c2pa_policy: object = openmeta.TransferPolicyAction.Keep,
    max_file_bytes: int = 0,
    policy: object | None = None,
    apply_exif: bool = True,
    apply_xmp: bool = True,
    apply_iptc: bool = True,
    synchronize_metadata: bool = True,
    cleanup_for_update: bool = True,
) -> dict[object, object]:
    return openmeta.update_dng_sdk_file_from_file(
        os.fspath(path),
        os.fspath(target_path),
        dng_target_mode=dng_target_mode,
        format=format,
        include_pointer_tags=include_pointer_tags,
        decode_makernote=decode_makernote,
        decode_embedded_containers=decode_embedded_containers,
        decompress=decompress,
        include_exif_app1=include_exif_app1,
        include_xmp_app1=include_xmp_app1,
        include_icc_app2=include_icc_app2,
        include_iptc_app13=include_iptc_app13,
        xmp_include_existing=xmp_include_existing,
        xmp_existing_namespace_policy=xmp_existing_namespace_policy,
        xmp_existing_standard_namespace_policy=xmp_existing_standard_namespace_policy,
        xmp_exiftool_gpsdatetime_alias=xmp_exiftool_gpsdatetime_alias,
        xmp_project_exif=xmp_project_exif,
        xmp_project_iptc=xmp_project_iptc,
        makernote_policy=makernote_policy,
        jumbf_policy=jumbf_policy,
        c2pa_policy=c2pa_policy,
        max_file_bytes=max_file_bytes,
        policy=policy,
        apply_exif=apply_exif,
        apply_xmp=apply_xmp,
        apply_iptc=apply_iptc,
        synchronize_metadata=synchronize_metadata,
        cleanup_for_update=cleanup_for_update,
    )


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="metatransfer.py")
    ap.add_argument("files", nargs="*")
    ap.add_argument("-i", "--input", action="append", default=[], help="input file (repeatable)")
    ap.add_argument("--no-build-info", action="store_true", help="hide OpenMeta build info header")
    ap.add_argument("--format", choices=["portable", "lossless"], default="portable", help="XMP mode for transfer-prepared APP1 XMP")
    ap.add_argument("--portable", action="store_true", help="alias for --format portable")
    ap.add_argument("--lossless", action="store_true", help="alias for --format lossless")
    ap.add_argument("--xmp-include-existing", action="store_true", help="include existing decoded XMP in generated transfer XMP")
    ap.add_argument("--xmp-existing-namespace-policy", choices=["known_portable_only", "preserve_custom"], default="known_portable_only", help="existing XMP namespace writeback policy for generated portable XMP")
    ap.add_argument("--xmp-existing-standard-namespace-policy", choices=["preserve_all", "canonicalize_managed"], default="preserve_all", help="reconcile existing managed standard portable XMP properties against canonical EXIF/IPTC mappings")
    ap.add_argument("--xmp-include-existing-sidecar", action="store_true", help="include an existing sibling .xmp sidecar from the output/edit target path in generated transfer XMP")
    ap.add_argument("--xmp-existing-sidecar-precedence", choices=["sidecar_wins", "source_wins"], default="sidecar_wins", help="conflict precedence between an existing output-side .xmp and source-embedded existing XMP")
    ap.add_argument("--xmp-include-existing-destination-embedded", action="store_true", help="include existing embedded XMP from the edit target in generated transfer XMP")
    ap.add_argument("--xmp-existing-destination-embedded-precedence", choices=["destination_wins", "source_wins"], default="destination_wins", help="conflict precedence between existing destination embedded XMP and source-embedded existing XMP")
    ap.add_argument("--xmp-existing-destination-carrier-precedence", choices=["sidecar_wins", "embedded_wins"], default="sidecar_wins", help="conflict precedence between an existing destination sidecar and existing destination embedded XMP")
    ap.add_argument("--xmp-no-exif-projection", action="store_true", help="do not mirror EXIF-derived properties into generated XMP")
    ap.add_argument("--xmp-no-iptc-projection", action="store_true", help="do not mirror IPTC-derived properties into generated XMP")
    ap.add_argument("--xmp-conflict-policy", choices=["current", "existing_wins", "generated_wins"], default="current", help="conflict policy between existing decoded XMP and generated portable EXIF/IPTC XMP")
    ap.add_argument("--xmp-writeback", choices=["embedded", "sidecar", "embedded_and_sidecar"], default="embedded", help="keep generated XMP embedded, persist it only as a sibling .xmp sidecar, or do both when --output is used")
    ap.add_argument("--xmp-destination-embedded", choices=["preserve_existing", "strip_existing"], default="preserve_existing", help="keep or remove destination embedded XMP during sidecar writeback; strip_existing is currently supported for JPEG, TIFF, DNG, PNG, WebP, JP2, and JXL sidecar-only writeback")
    ap.add_argument("--xmp-destination-sidecar", choices=["preserve_existing", "strip_existing"], default="preserve_existing", help="keep or remove an existing sibling .xmp sidecar when writeback stays embedded-only")
    ap.add_argument("--xmp-exiftool-gpsdatetime-alias", action="store_true", help="emit exif:GPSDateTime alias for GPS time in portable mode")
    ap.add_argument("--no-exif", action="store_true", help="skip EXIF APP1 preparation")
    ap.add_argument("--no-xmp", action="store_true", help="skip XMP APP1 preparation")
    ap.add_argument("--no-icc", action="store_true", help="skip ICC APP2 preparation")
    ap.add_argument("--no-iptc", action="store_true", help="skip IPTC APP13 preparation")
    ap.add_argument("--makernotes", action="store_true", help="enable best-effort MakerNote decode in read phase")
    ap.add_argument("--makernote-policy", choices=["keep", "drop", "invalidate", "rewrite"], default="keep", help="transfer policy for MakerNote payloads")
    ap.add_argument("--jumbf-policy", choices=["keep", "drop", "invalidate", "rewrite"], default="keep", help="transfer policy for JUMBF payloads")
    ap.add_argument("--c2pa-policy", choices=["keep", "drop", "invalidate", "rewrite"], default="keep", help="transfer policy for C2PA payloads")
    ap.add_argument("--transfer-safety", choices=["compatible", "rendered"], default="compatible", help="coarse metadata safety mode for compatible repackage or rendered-image export")
    ap.add_argument("--no-decompress", action="store_true", help="disable payload decompression in read phase")
    ap.add_argument(
        "--unsafe-write-payloads",
        action="store_true",
        help="write prepared raw block payload bytes to files",
    )
    ap.add_argument(
        "--write-payloads",
        action="store_true",
        help="deprecated alias for --unsafe-write-payloads",
    )
    ap.add_argument(
        "--out-dir",
        type=str,
        default="",
        help="output directory for --unsafe-write-payloads",
    )
    ap.add_argument("--target-jxl", action="store_true", help="target JPEG XL metadata emit summary")
    ap.add_argument("--target-webp", action="store_true", help="target WebP metadata transfer")
    ap.add_argument("--target-exr", action="store_true", help="target EXR metadata transfer")
    ap.add_argument("--target-png", action="store_true", help="target PNG metadata transfer")
    ap.add_argument("--target-jp2", action="store_true", help="target JP2 metadata transfer")
    ap.add_argument("--target-heif", action="store_true", help="target HEIF metadata transfer")
    ap.add_argument("--target-avif", action="store_true", help="target AVIF metadata transfer")
    ap.add_argument("--target-cr3", action="store_true", help="target CR3 metadata transfer")
    ap.add_argument("--target-jpeg", type=str, default="", help="target JPEG stream for edit/apply")
    ap.add_argument("--target-tiff", type=str, default="", help="target TIFF stream for edit/apply")
    ap.add_argument("--target-dng", type=str, default="", help="target DNG stream for edit/apply")
    ap.add_argument("--dng-target-mode", choices=["existing_target", "template_target", "minimal_fresh_scaffold"], default="minimal_fresh_scaffold", help="public DNG transfer contract for --target-dng")
    ap.add_argument("--target-width", type=int, default=None, help="host-supplied target pixel width for regenerated image-layout metadata")
    ap.add_argument("--target-height", type=int, default=None, help="host-supplied target pixel height for regenerated image-layout metadata")
    ap.add_argument("--target-orientation", type=int, default=None, help="host-supplied target EXIF orientation (1..8)")
    ap.add_argument("--target-samples-per-pixel", type=int, default=None, help="host-supplied target samples per pixel")
    ap.add_argument("--target-bits-per-sample", type=str, default=None, help="host-supplied target bits per sample, as N or N,N,...")
    ap.add_argument("--target-sample-format", type=str, default=None, help="host-supplied target TIFF sample format, as N or N,N,...")
    ap.add_argument("--target-photometric", type=int, default=None, help="host-supplied target TIFF photometric interpretation")
    ap.add_argument("--target-planar-configuration", type=int, default=None, help="host-supplied target TIFF planar configuration (1 or 2)")
    ap.add_argument("--target-compression", type=int, default=None, help="host-supplied target TIFF compression value")
    ap.add_argument("--target-exif-color-space", type=int, default=None, help="host-supplied target EXIF ColorSpace value")
    ap.add_argument("--source-meta", type=str, default="", help="source metadata file for edit/apply against a separate target file")
    ap.add_argument("--jpeg-c2pa-signed", type=str, default="", help="externally signed logical C2PA payload for JPEG, JXL, or bounded BMFF staging")
    ap.add_argument("--c2pa-manifest-output", type=str, default="", help="external manifest-builder output bytes for signed C2PA staging")
    ap.add_argument("--c2pa-certificate-chain", type=str, default="", help="external certificate-chain bytes for signed C2PA staging")
    ap.add_argument("--c2pa-key-ref", type=str, default="", help="private-key reference string for signed C2PA staging")
    ap.add_argument("--c2pa-signing-time", type=str, default="", help="signing time for signed C2PA staging")
    ap.add_argument("--dump-c2pa-binding", type=str, default="", help="write exact C2PA content-binding bytes for external signing on JPEG, JXL, or bounded BMFF targets")
    ap.add_argument("--dump-c2pa-handoff", type=str, default="", help="write one persisted C2PA handoff package for JPEG, JXL, or bounded BMFF targets")
    ap.add_argument("--dump-c2pa-signed-package", type=str, default="", help="write one persisted signed C2PA package for JPEG, JXL, or bounded BMFF targets")
    ap.add_argument("--load-c2pa-signed-package", type=str, default="", help="load one persisted signed C2PA package for JPEG, JXL, or bounded BMFF staging")
    ap.add_argument("--dump-transfer-payload-batch", type=str, default="", help="write one persisted semantic transfer payload batch")
    ap.add_argument("--load-transfer-payload-batch", type=str, default="", help="load and inspect one persisted semantic transfer payload batch")
    ap.add_argument("--dump-transfer-package-batch", type=str, default="", help="write one persisted final transfer package batch")
    ap.add_argument("--load-transfer-package-batch", type=str, default="", help="load and inspect one persisted final transfer package batch")
    ap.add_argument("--dump-jxl-encoder-handoff", type=str, default="", help="write one persisted JXL encoder ICC handoff")
    ap.add_argument("--load-jxl-encoder-handoff", type=str, default="", help="load and inspect one persisted JXL encoder ICC handoff")
    ap.add_argument("--load-transfer-artifact", type=str, default="", help="load and inspect one persisted transfer artifact of any supported kind")
    ap.add_argument("-o", "--output", type=str, default="", help="write edited output file")
    ap.add_argument("--dry-run", action="store_true", help="plan edit only; do not write output")
    ap.add_argument("--force", action="store_true", help="overwrite existing payload files")
    ap.add_argument(
        "--time-patch",
        action="append",
        default=[],
        help="time patch update Field=Value (repeatable)",
    )
    ap.add_argument(
        "--time-patch-lax-width",
        action="store_true",
        help="disable strict patch width checks",
    )
    ap.add_argument(
        "--time-patch-require-slot",
        action="store_true",
        help="fail if requested patch field is not present",
    )
    ap.add_argument(
        "--time-patch-no-auto-nul",
        action="store_true",
        help="disable auto NUL append for text patch values",
    )
    ap.add_argument("--max-file-bytes", type=int, default=0, help="optional mapped file cap in bytes (0=unlimited)")
    ap.add_argument("--max-payload-bytes", type=int, default=0, help="max reassembled/decompressed payload bytes")
    ap.add_argument("--max-payload-parts", type=int, default=0, help="max payload part count")
    ap.add_argument("--max-exif-ifds", type=int, default=0, help="max EXIF/TIFF IFD count")
    ap.add_argument("--max-exif-entries", type=int, default=0, help="max EXIF/TIFF entries per IFD")
    ap.add_argument("--max-exif-total", type=int, default=0, help="max EXIF/TIFF total entries")
    args = ap.parse_args(argv)

    input_paths = list(args.input) + list(args.files)
    write_payloads = bool(args.unsafe_write_payloads or args.write_payloads)
    target_count = (
        int(bool(args.target_jpeg))
        + int(bool(args.target_tiff))
        + int(bool(args.target_dng))
        + int(bool(args.target_jxl))
        + int(bool(args.target_webp))
        + int(bool(args.target_exr))
        + int(bool(args.target_png))
        + int(bool(args.target_jp2))
        + int(bool(args.target_heif))
        + int(bool(args.target_avif))
        + int(bool(args.target_cr3))
    )
    inspect_mode_count = (
        int(bool(args.load_transfer_payload_batch))
        + int(bool(args.load_transfer_package_batch))
        + int(bool(args.load_jxl_encoder_handoff))
        + int(bool(args.load_transfer_artifact))
    )
    if inspect_mode_count > 1:
        ap.error("--load-transfer-payload-batch, --load-transfer-package-batch, --load-jxl-encoder-handoff, and --load-transfer-artifact are mutually exclusive")
    if args.load_transfer_payload_batch:
        if input_paths:
            ap.error("--load-transfer-payload-batch is mutually exclusive with source input paths")
        if (
            target_count > 0
            or args.source_meta
            or args.output
            or args.dry_run
            or write_payloads
            or args.dump_transfer_payload_batch
            or args.dump_transfer_package_batch
            or args.dump_c2pa_binding
            or args.dump_c2pa_handoff
            or args.dump_c2pa_signed_package
            or args.load_c2pa_signed_package
            or args.jpeg_c2pa_signed
            or args.c2pa_manifest_output
            or args.c2pa_certificate_chain
            or args.c2pa_key_ref
            or args.c2pa_signing_time
            or args.dump_jxl_encoder_handoff
        ):
            ap.error("--load-transfer-payload-batch is an inspect-only mode")
    elif args.load_transfer_package_batch:
        if input_paths:
            ap.error("--load-transfer-package-batch is mutually exclusive with source input paths")
        if (
            target_count > 0
            or args.source_meta
            or args.output
            or args.dry_run
            or write_payloads
            or args.dump_transfer_payload_batch
            or args.dump_transfer_package_batch
            or args.dump_c2pa_binding
            or args.dump_c2pa_handoff
            or args.dump_c2pa_signed_package
            or args.load_c2pa_signed_package
            or args.jpeg_c2pa_signed
            or args.c2pa_manifest_output
            or args.c2pa_certificate_chain
            or args.c2pa_key_ref
            or args.c2pa_signing_time
            or args.dump_jxl_encoder_handoff
        ):
            ap.error("--load-transfer-package-batch is an inspect-only mode")
    elif args.load_jxl_encoder_handoff:
        if input_paths:
            ap.error("--load-jxl-encoder-handoff is mutually exclusive with source input paths")
        if (
            target_count > 0
            or args.source_meta
            or args.output
            or args.dry_run
            or write_payloads
            or args.dump_transfer_payload_batch
            or args.dump_transfer_package_batch
            or args.dump_c2pa_binding
            or args.dump_c2pa_handoff
            or args.dump_c2pa_signed_package
            or args.load_c2pa_signed_package
            or args.jpeg_c2pa_signed
            or args.c2pa_manifest_output
            or args.c2pa_certificate_chain
            or args.c2pa_key_ref
            or args.c2pa_signing_time
            or args.dump_jxl_encoder_handoff
        ):
            ap.error("--load-jxl-encoder-handoff is an inspect-only mode")
    elif args.load_transfer_artifact:
        if input_paths:
            ap.error("--load-transfer-artifact is mutually exclusive with source input paths")
        if (
            target_count > 0
            or args.source_meta
            or args.output
            or args.dry_run
            or write_payloads
            or args.dump_transfer_payload_batch
            or args.dump_transfer_package_batch
            or args.dump_c2pa_binding
            or args.dump_c2pa_handoff
            or args.dump_c2pa_signed_package
            or args.load_c2pa_signed_package
            or args.jpeg_c2pa_signed
            or args.c2pa_manifest_output
            or args.c2pa_certificate_chain
            or args.c2pa_key_ref
            or args.c2pa_signing_time
            or args.dump_jxl_encoder_handoff
        ):
            ap.error("--load-transfer-artifact is an inspect-only mode")
    elif not input_paths:
        ap.print_help(sys.stderr)
        return 2
    if target_count > 1:
        ap.error("--target-jpeg, --target-tiff, --target-dng, --target-exr, --target-png, --target-jp2, --target-jxl, --target-webp, --target-heif, --target-avif, and --target-cr3 are mutually exclusive")
    try:
        target_image_spec = _build_target_image_spec(args)
    except ValueError as ex:
        ap.error(str(ex))
    if inspect_mode_count > 0 and target_image_spec is not None:
        ap.error("target image spec options are transfer/edit options")
    if (
        args.jpeg_c2pa_signed
        or args.c2pa_manifest_output
        or args.c2pa_certificate_chain
        or args.c2pa_key_ref
        or args.c2pa_signing_time
    ) and (
        args.target_tiff
        or args.target_dng
        or args.target_webp
        or args.target_png
    ):
        ap.error("signed C2PA staging is only supported for JPEG, JXL, and BMFF targets")
    if args.output and not (
        args.target_jpeg
        or args.target_tiff
        or args.target_dng
        or args.target_webp
        or args.target_png
        or args.target_jp2
        or args.target_jxl
        or args.target_heif
        or args.target_avif
        or args.target_cr3
    ):
        ap.error(
            "--output requires --target-jpeg, --target-tiff, --target-dng, --target-webp, "
            "--target-png, --target-jp2, --target-jxl, --target-heif, "
            "--target-avif, or --target-cr3"
        )
    if args.xmp_writeback != "embedded" and not args.output:
        ap.error("--xmp-writeback sidecar or embedded_and_sidecar requires --output")
    if (
        args.xmp_destination_embedded == "strip_existing"
        and (
            args.xmp_writeback != "sidecar"
            or not (
                args.target_jpeg
                or args.target_tiff
                or args.target_dng
                or args.target_png
                or args.target_webp
                or args.target_jp2
                or args.target_jxl
            )
        )
    ):
        ap.error("--xmp-destination-embedded strip_existing is currently supported only for --target-jpeg, --target-tiff, --target-dng, --target-png, --target-webp, --target-jp2, and --target-jxl with --xmp-writeback sidecar")
    if (
        args.xmp_destination_sidecar == "strip_existing"
        and args.xmp_writeback != "embedded"
    ):
        ap.error("--xmp-destination-sidecar strip_existing is currently supported only with --xmp-writeback embedded")
    if (
        args.xmp_include_existing_destination_embedded
        and not (
            args.target_jpeg
            or args.target_tiff
            or args.target_dng
            or args.target_webp
            or args.target_png
            or args.target_jp2
            or args.target_jxl
            or args.target_heif
            or args.target_avif
            or args.target_cr3
        )
    ):
        ap.error("--xmp-include-existing-destination-embedded requires an edit target")
    if args.dump_c2pa_binding and (
        args.target_tiff
        or args.target_dng
        or args.target_webp
        or args.target_png
    ):
        ap.error("--dump-c2pa-binding is only supported for JPEG, JXL, and BMFF targets")
    if (
        args.dump_c2pa_handoff
        or args.dump_c2pa_signed_package
        or args.load_c2pa_signed_package
    ) and (
        args.target_tiff
        or args.target_dng
        or args.target_webp
        or args.target_png
    ):
        ap.error("C2PA package options are only supported for JPEG, JXL, and BMFF targets")
    if (
        args.target_jpeg
        or args.target_tiff
        or args.target_dng
        or args.target_webp
        or args.target_png
        or args.target_heif
        or args.target_avif
        or args.target_cr3
    ) and len(input_paths) != 1:
        ap.error("edit mode supports exactly one source input")
    if args.dump_c2pa_binding and len(input_paths) != 1:
        ap.error("--dump-c2pa-binding supports exactly one input path per run")
    if (
        args.dump_c2pa_handoff
        or args.dump_c2pa_signed_package
        or args.load_c2pa_signed_package
    ) and len(input_paths) != 1:
        ap.error("C2PA package options support exactly one input path per run")
    if args.dump_transfer_payload_batch and len(input_paths) != 1:
        ap.error("--dump-transfer-payload-batch supports exactly one input path per run")
    if args.dump_transfer_package_batch and len(input_paths) != 1:
        ap.error("--dump-transfer-package-batch supports exactly one input path per run")
    if args.dump_jxl_encoder_handoff and len(input_paths) != 1:
        ap.error("--dump-jxl-encoder-handoff supports exactly one input path per run")
    if args.load_c2pa_signed_package and (
        args.jpeg_c2pa_signed
        or args.c2pa_manifest_output
        or args.c2pa_certificate_chain
        or args.c2pa_key_ref
        or args.c2pa_signing_time
    ):
        ap.error(
            "--load-c2pa-signed-package is mutually exclusive with individual signed C2PA inputs"
        )

    if args.portable:
        args.format = "portable"
    if args.lossless:
        args.format = "lossless"
    try:
        time_patches = _parse_time_patch_specs(list(args.time_patch))
    except ValueError as ex:
        ap.error(str(ex))

    if not args.no_build_info:
        l1, l2 = openmeta.info_lines()
        print(l1)
        print(l2)
        print(openmeta.python_info_line())

    scan_map, payload_map, exif_map, xmp_map, jumbf_map = _parse_status_maps()

    policy = openmeta.ResourcePolicy()
    policy.max_file_bytes = int(args.max_file_bytes)
    if args.max_payload_bytes > 0:
        policy.payload_limits.max_output_bytes = int(args.max_payload_bytes)
    if args.max_payload_parts > 0:
        policy.payload_limits.max_parts = int(args.max_payload_parts)
    if args.max_exif_ifds > 0:
        policy.exif_limits.max_ifds = int(args.max_exif_ifds)
    if args.max_exif_entries > 0:
        policy.exif_limits.max_entries_per_ifd = int(args.max_exif_entries)
    if args.max_exif_total > 0:
        policy.exif_limits.max_total_entries = int(args.max_exif_total)

    sidecar_format = (
        openmeta.XmpSidecarFormat.Portable
        if args.format == "portable"
        else openmeta.XmpSidecarFormat.Lossless
    )
    makernote_policy = _transfer_policy_action(args.makernote_policy)
    jumbf_policy = _transfer_policy_action(args.jumbf_policy)
    c2pa_policy = _transfer_policy_action(args.c2pa_policy)
    transfer_safety = _transfer_safety_mode(args.transfer_safety)

    rc = 0
    if args.target_tiff:
        target_format = openmeta.TransferTargetFormat.Tiff
    elif args.target_dng:
        target_format = openmeta.TransferTargetFormat.Dng
    elif args.target_exr:
        target_format = openmeta.TransferTargetFormat.Exr
    elif args.target_png:
        target_format = openmeta.TransferTargetFormat.Png
    elif args.target_jp2:
        target_format = openmeta.TransferTargetFormat.Jp2
    elif args.target_jxl:
        target_format = openmeta.TransferTargetFormat.Jxl
    elif args.target_webp:
        target_format = openmeta.TransferTargetFormat.Webp
    elif args.target_heif:
        target_format = openmeta.TransferTargetFormat.Heif
    elif args.target_avif:
        target_format = openmeta.TransferTargetFormat.Avif
    elif args.target_cr3:
        target_format = openmeta.TransferTargetFormat.Cr3
    else:
        target_format = openmeta.TransferTargetFormat.Jpeg
    target_path = (
        args.target_tiff
        if args.target_tiff
        else (args.target_dng if args.target_dng else args.target_jpeg)
    )
    if (
        not target_path
        and (
            args.target_webp
            or args.target_png
            or args.target_jp2
            or args.target_jxl
            or args.target_heif
            or args.target_avif
            or args.target_cr3
        )
        and (args.output or args.dry_run)
        and len(input_paths) == 1
    ):
        target_path = input_paths[0]
    edit_requested = bool(target_path)
    edit_apply = bool(edit_requested and not args.dry_run)
    persist_output = bool(edit_apply and args.output)
    need_edited_bytes = False
    need_c2pa_binding_bytes = bool(args.dump_c2pa_binding)
    need_c2pa_handoff_bytes = bool(args.dump_c2pa_handoff)
    need_c2pa_signed_package_bytes = bool(args.dump_c2pa_signed_package)
    need_transfer_payload_batch_bytes = bool(args.dump_transfer_payload_batch)
    need_transfer_package_batch_bytes = bool(args.dump_transfer_package_batch)
    need_jxl_encoder_handoff_bytes = bool(args.dump_jxl_encoder_handoff)
    c2pa_signed_payload = (
        Path(args.jpeg_c2pa_signed).read_bytes() if args.jpeg_c2pa_signed else None
    )
    c2pa_manifest_output = (
        Path(args.c2pa_manifest_output).read_bytes()
        if args.c2pa_manifest_output
        else None
    )
    c2pa_certificate_chain = (
        Path(args.c2pa_certificate_chain).read_bytes()
        if args.c2pa_certificate_chain
        else None
    )
    c2pa_signed_package = (
        Path(args.load_c2pa_signed_package).read_bytes()
        if args.load_c2pa_signed_package
        else None
    )

    if args.load_transfer_payload_batch:
        batch_bytes = Path(args.load_transfer_payload_batch).read_bytes()
        inspected = openmeta.inspect_transfer_payload_batch(batch_bytes)
        print(f"== transfer_payload_batch={args.load_transfer_payload_batch}")
        print(
            f"  transfer_payload_batch: status={inspected['status_name']} "
            f"code={inspected['code_name']} "
            f"bytes={int(inspected['bytes_read'])} "
            f"payloads={int(inspected['payload_count'])} "
            f"target={inspected['target_format_name']}"
        )
        if inspected["message"]:
            print(f"  transfer_payload_batch_message={inspected['message']}")
        if inspected["overall_status"] != openmeta.TransferStatus.Ok:
            if inspected["error_message"]:
                print(f"  error_message={inspected['error_message']}")
            return 1
        for payload in inspected["payloads"]:
            print(
                f"  [{int(payload['index'])}] semantic={payload['semantic_name']} "
                f"route={payload['route']} "
                f"size={int(payload['size'])} "
                f"op={_transfer_payload_op_summary(payload)}"
            )
        return 0
    if args.load_transfer_package_batch:
        batch_bytes = Path(args.load_transfer_package_batch).read_bytes()
        inspected = openmeta.inspect_transfer_package_batch(batch_bytes)
        print(f"== transfer_package_batch={args.load_transfer_package_batch}")
        print(
            f"  transfer_package_batch: status={inspected['status_name']} "
            f"code={inspected['code_name']} "
            f"bytes={int(inspected['bytes_read'])} "
            f"chunks={int(inspected['chunk_count'])} "
            f"target={inspected['target_format_name']}"
        )
        if inspected["message"]:
            print(f"  transfer_package_batch_message={inspected['message']}")
        if inspected["overall_status"] != openmeta.TransferStatus.Ok:
            if inspected["error_message"]:
                print(f"  error_message={inspected['error_message']}")
            return 1
        for chunk in inspected["chunks"]:
            route = str(chunk["route"]) if chunk["route"] else "-"
            print(
                f"  [{int(chunk['index'])}] semantic={chunk['semantic_name']} "
                f"route={route} "
                f"size={int(chunk['size'])} "
                f"chunk={_transfer_package_chunk_summary(chunk)}"
            )
        return 0
    if args.load_jxl_encoder_handoff:
        handoff_bytes = Path(args.load_jxl_encoder_handoff).read_bytes()
        inspected = openmeta.inspect_jxl_encoder_handoff(handoff_bytes)
        print(f"== jxl_encoder_handoff={args.load_jxl_encoder_handoff}")
        print(
            f"  jxl_encoder_handoff: status={inspected['status_name']} "
            f"code={inspected['code_name']} "
            f"bytes={int(inspected['bytes_read'])} "
            f"box_count={int(inspected['box_count'])} "
            f"box_payload_bytes={int(inspected['box_payload_bytes'])}"
        )
        if inspected["message"]:
            print(f"  jxl_encoder_handoff_message={inspected['message']}")
        if inspected["overall_status"] != openmeta.TransferStatus.Ok:
            if inspected["error_message"]:
                print(f"  error_message={inspected['error_message']}")
            return 1
        if bool(inspected["has_icc_profile"]):
            print(f"  jxl_icc_profile bytes={int(inspected['icc_profile_bytes'])}")
        return 0
    if args.load_transfer_artifact:
        artifact_bytes = Path(args.load_transfer_artifact).read_bytes()
        inspected = openmeta.inspect_transfer_artifact(artifact_bytes)
        print(f"== transfer_artifact={args.load_transfer_artifact}")
        target_name = (
            str(inspected["target_format_name"])
            if inspected["target_format_name"] is not None
            else "-"
        )
        print(
            f"  transfer_artifact: status={inspected['status_name']} "
            f"code={inspected['code_name']} "
            f"kind={inspected['kind_name']} "
            f"bytes={int(inspected['bytes_read'])} "
            f"target={target_name}"
        )
        if inspected["message"]:
            print(f"  transfer_artifact_message={inspected['message']}")
        if inspected["overall_status"] != openmeta.TransferStatus.Ok:
            if inspected["error_message"]:
                print(f"  error_message={inspected['error_message']}")
            return 1
        kind_name = str(inspected["kind_name"])
        if kind_name == "transfer_payload_batch":
            print(
                f"  transfer_payload_batch: payloads={int(inspected['entry_count'])} "
                f"payload_bytes={int(inspected['payload_bytes'])}"
            )
        elif kind_name == "transfer_package_batch":
            print(
                f"  transfer_package_batch: chunks={int(inspected['entry_count'])} "
                f"payload_bytes={int(inspected['payload_bytes'])}"
            )
        elif kind_name == "jxl_encoder_handoff":
            print(
                f"  jxl_encoder_handoff: box_count={int(inspected['entry_count'])} "
                f"box_payload_bytes={int(inspected['box_payload_bytes'])}"
            )
            if bool(inspected["has_icc_profile"]):
                print(
                    f"  jxl_icc_profile bytes={int(inspected['icc_profile_bytes'])}"
                )
        elif kind_name == "c2pa_handoff_package":
            print(
                f"  c2pa_handoff: carrier={inspected['carrier_route'] or '-'} "
                f"manifest_label={inspected['manifest_label'] or '-'} "
                f"binding_bytes={int(inspected['binding_bytes'])} "
                f"chunks={int(inspected['entry_count'])}"
            )
        elif kind_name == "c2pa_signed_package":
            print(
                f"  c2pa_signed_package: carrier={inspected['carrier_route'] or '-'} "
                f"manifest_label={inspected['manifest_label'] or '-'} "
                f"signed_payload_bytes={int(inspected['signed_payload_bytes'])} "
                f"chunks={int(inspected['entry_count'])}"
            )
        return 0

    xmp_conflict_policy = openmeta.XmpConflictPolicy.CurrentBehavior
    if args.xmp_conflict_policy == "existing_wins":
        xmp_conflict_policy = openmeta.XmpConflictPolicy.ExistingWins
    elif args.xmp_conflict_policy == "generated_wins":
        xmp_conflict_policy = openmeta.XmpConflictPolicy.GeneratedWins
    xmp_existing_namespace_policy = (
        openmeta.XmpExistingNamespacePolicy.KnownPortableOnly
    )
    if args.xmp_existing_namespace_policy == "preserve_custom":
        xmp_existing_namespace_policy = (
            openmeta.XmpExistingNamespacePolicy.PreserveCustom
        )
    xmp_existing_standard_namespace_policy = (
        openmeta.XmpExistingStandardNamespacePolicy.PreserveAll
    )
    if args.xmp_existing_standard_namespace_policy == "canonicalize_managed":
        xmp_existing_standard_namespace_policy = (
            openmeta.XmpExistingStandardNamespacePolicy.CanonicalizeManaged
        )

    xmp_writeback_mode = openmeta.XmpWritebackMode.EmbeddedOnly
    if args.xmp_writeback == "sidecar":
        xmp_writeback_mode = openmeta.XmpWritebackMode.SidecarOnly
    elif args.xmp_writeback == "embedded_and_sidecar":
        xmp_writeback_mode = openmeta.XmpWritebackMode.EmbeddedAndSidecar
    xmp_destination_embedded_mode = (
        openmeta.XmpDestinationEmbeddedMode.PreserveExisting
    )
    if args.xmp_destination_embedded == "strip_existing":
        xmp_destination_embedded_mode = (
            openmeta.XmpDestinationEmbeddedMode.StripExisting
        )
    xmp_destination_sidecar_mode = (
        openmeta.XmpDestinationSidecarMode.PreserveExisting
    )
    if args.xmp_destination_sidecar == "strip_existing":
        xmp_destination_sidecar_mode = (
            openmeta.XmpDestinationSidecarMode.StripExisting
        )
    xmp_existing_sidecar_mode = openmeta.XmpExistingSidecarMode.Ignore
    if args.xmp_include_existing_sidecar:
        xmp_existing_sidecar_mode = (
            openmeta.XmpExistingSidecarMode.MergeIfPresent
        )
    xmp_existing_sidecar_precedence = (
        openmeta.XmpExistingSidecarPrecedence.SidecarWins
    )
    if args.xmp_existing_sidecar_precedence == "source_wins":
        xmp_existing_sidecar_precedence = (
            openmeta.XmpExistingSidecarPrecedence.SourceWins
        )
    xmp_existing_destination_embedded_mode = (
        openmeta.XmpExistingDestinationEmbeddedMode.Ignore
    )
    if args.xmp_include_existing_destination_embedded:
        xmp_existing_destination_embedded_mode = (
            openmeta.XmpExistingDestinationEmbeddedMode.MergeIfPresent
        )
    xmp_existing_destination_embedded_precedence = (
        openmeta.XmpExistingDestinationEmbeddedPrecedence.DestinationWins
    )
    if args.xmp_existing_destination_embedded_precedence == "source_wins":
        xmp_existing_destination_embedded_precedence = (
            openmeta.XmpExistingDestinationEmbeddedPrecedence.SourceWins
        )
    xmp_existing_destination_carrier_precedence = (
        openmeta.XmpExistingDestinationCarrierPrecedence.SidecarWins
    )
    if args.xmp_existing_destination_carrier_precedence == "embedded_wins":
        xmp_existing_destination_carrier_precedence = (
            openmeta.XmpExistingDestinationCarrierPrecedence.EmbeddedWins
        )
    dng_target_mode = openmeta.DngTargetMode.MinimalFreshScaffold
    if args.dng_target_mode == "existing_target":
        dng_target_mode = openmeta.DngTargetMode.ExistingTarget
    elif args.dng_target_mode == "template_target":
        dng_target_mode = openmeta.DngTargetMode.TemplateTarget

    for path in input_paths:
        source_path = args.source_meta if args.source_meta else path
        xmp_sidecar_base_path = None
        if args.output:
            xmp_sidecar_base_path = args.output
        elif target_path:
            xmp_sidecar_base_path = target_path
        elif args.xmp_include_existing_sidecar:
            xmp_sidecar_base_path = source_path
        use_unsafe_transfer = bool(
            write_payloads
            or need_edited_bytes
            or need_c2pa_binding_bytes
            or need_c2pa_handoff_bytes
            or need_c2pa_signed_package_bytes
            or need_transfer_payload_batch_bytes
            or need_transfer_package_batch_bytes
            or need_jxl_encoder_handoff_bytes
        )
        common_kwargs = dict(
            target_format=target_format,
            dng_target_mode=dng_target_mode,
            format=sidecar_format,
            include_pointer_tags=True,
            decode_makernote=bool(args.makernotes),
            decode_embedded_containers=True,
            decompress=not args.no_decompress,
            include_exif_app1=not args.no_exif,
            include_xmp_app1=not args.no_xmp,
            include_icc_app2=not args.no_icc,
            include_iptc_app13=not args.no_iptc,
            xmp_project_exif=not args.xmp_no_exif_projection,
            xmp_project_iptc=not args.xmp_no_iptc_projection,
            xmp_include_existing=bool(args.xmp_include_existing),
            xmp_existing_namespace_policy=xmp_existing_namespace_policy,
            xmp_existing_standard_namespace_policy=xmp_existing_standard_namespace_policy,
            xmp_conflict_policy=xmp_conflict_policy,
            xmp_exiftool_gpsdatetime_alias=bool(args.xmp_exiftool_gpsdatetime_alias),
            makernote_policy=makernote_policy,
            jumbf_policy=jumbf_policy,
            c2pa_policy=c2pa_policy,
            max_file_bytes=int(args.max_file_bytes),
            policy=policy,
            c2pa_signed_package=c2pa_signed_package,
            c2pa_signed_logical_payload=c2pa_signed_payload,
            c2pa_certificate_chain=c2pa_certificate_chain,
            c2pa_private_key_reference=(args.c2pa_key_ref or None),
            c2pa_signing_time=(args.c2pa_signing_time or None),
            c2pa_manifest_builder_output=c2pa_manifest_output,
            include_payloads=write_payloads,
            time_patches=time_patches if time_patches else None,
            time_patch_strict_width=not bool(args.time_patch_lax_width),
            time_patch_require_slot=bool(args.time_patch_require_slot),
            time_patch_auto_nul=not bool(args.time_patch_no_auto_nul),
            edit_target_path=(target_path if edit_requested else None),
            xmp_sidecar_base_path=xmp_sidecar_base_path,
            xmp_existing_sidecar_mode=xmp_existing_sidecar_mode,
            xmp_existing_sidecar_precedence=xmp_existing_sidecar_precedence,
            xmp_existing_destination_embedded_mode=xmp_existing_destination_embedded_mode,
            xmp_existing_destination_embedded_precedence=xmp_existing_destination_embedded_precedence,
            xmp_existing_destination_carrier_precedence=xmp_existing_destination_carrier_precedence,
            edit_apply=edit_apply,
            include_edited_bytes=need_edited_bytes,
            include_c2pa_binding_bytes=need_c2pa_binding_bytes,
            include_c2pa_handoff_bytes=need_c2pa_handoff_bytes,
            include_c2pa_signed_package_bytes=need_c2pa_signed_package_bytes,
            include_jxl_encoder_handoff_bytes=need_jxl_encoder_handoff_bytes,
            include_transfer_payload_batch_bytes=need_transfer_payload_batch_bytes,
            include_transfer_package_batch_bytes=need_transfer_package_batch_bytes,
            xmp_writeback_mode=xmp_writeback_mode,
            xmp_destination_embedded_mode=xmp_destination_embedded_mode,
            xmp_destination_sidecar_mode=xmp_destination_sidecar_mode,
            target_image_spec=target_image_spec,
            transfer_safety=transfer_safety,
        )
        if persist_output:
            os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
            transfer_fn = (
                openmeta.unsafe_transfer_file
                if use_unsafe_transfer
                else openmeta.transfer_file
            )
            probe = transfer_fn(
                source_path,
                **common_kwargs,
                output_path=args.output,
                overwrite_output=bool(args.force),
                overwrite_xmp_sidecar=bool(args.force),
                remove_destination_xmp_sidecar=True,
            )
        else:
            probe_fn = (
                openmeta.unsafe_transfer_probe
                if use_unsafe_transfer
                else openmeta.transfer_probe
            )
            probe = probe_fn(source_path, **common_kwargs)

        print(f"== {source_path}")
        if source_path != path:
            print(f"   target={path}")
        print(
            f"  file_status={probe['file_status_name']} size={probe['file_size']} entries={probe['entry_count']}"
        )
        print(
            "  read: "
            f"scan={scan_map.get(probe['scan_status'], 'unknown')} "
            f"payload={payload_map.get(probe['payload_status'], 'unknown')} "
            f"exif={exif_map.get(probe['exif_status'], 'unknown')} "
            f"xmp={xmp_map.get(probe['xmp_status'], 'unknown')} "
            f"jumbf={jumbf_map.get(probe['jumbf_status'], 'unknown')}"
        )
        print(
            f"  prepare: status={probe['prepare_status_name']} blocks={len(probe['blocks'])} "
            f"warnings={probe['prepare_warnings']} errors={probe['prepare_errors']} "
            f"remove_existing_iptc={probe['remove_existing_iptc']}"
        )
        if args.xmp_include_existing_sidecar:
            print(
                f"  xmp_existing_sidecar: status={probe['xmp_existing_sidecar_status_name']} "
                f"loaded={'yes' if probe['xmp_existing_sidecar_loaded'] else 'no'} "
                f"path={probe['xmp_existing_sidecar_path'] or '-'}"
            )
            if probe["xmp_existing_sidecar_message"]:
                print(
                    f"  xmp_existing_sidecar_message={probe['xmp_existing_sidecar_message']}"
                )
        if args.xmp_include_existing_destination_embedded:
            print(
                "  xmp_existing_destination_embedded: "
                f"status={probe['xmp_existing_destination_embedded_status_name']} "
                f"loaded={'yes' if probe['xmp_existing_destination_embedded_loaded'] else 'no'} "
                f"path={probe['xmp_existing_destination_embedded_path'] or '-'}"
            )
            if probe["xmp_existing_destination_embedded_message"]:
                print(
                    "  xmp_existing_destination_embedded_message="
                    f"{probe['xmp_existing_destination_embedded_message']}"
                )
        if probe["xmp_sidecar_requested"]:
            xmp_sidecar_path = str(probe["xmp_sidecar_path"] or "-")
            print(
                f"  xmp_sidecar: status={probe['xmp_sidecar_status_name']} "
                f"bytes={int(probe['xmp_sidecar_bytes'])} "
                f"path={xmp_sidecar_path}"
            )
            if probe["xmp_sidecar_message"]:
                print(f"  xmp_sidecar_message={probe['xmp_sidecar_message']}")
        if args.xmp_destination_sidecar == "strip_existing":
            print(
                f"  xmp_sidecar_cleanup: status={probe['xmp_sidecar_cleanup_status_name']} "
                f"requested={'yes' if probe['xmp_sidecar_cleanup_requested'] else 'no'} "
                f"path={probe['xmp_sidecar_cleanup_path'] or '-'}"
            )
            if probe["xmp_sidecar_cleanup_message"]:
                print(
                    f"  xmp_sidecar_cleanup_message={probe['xmp_sidecar_cleanup_message']}"
                )
        print(
            f"  time_patch: status={probe['time_patch_status_name']} patched={probe['time_patch_patched_slots']} "
            f"skipped={probe['time_patch_skipped_slots']} errors={probe['time_patch_errors']}"
        )
        print(
            f"  overall: status={probe['overall_status_name']} stage={probe['error_stage']} code={probe['error_code']}"
        )
        if probe["prepare_message"]:
            print(f"  prepare_message={probe['prepare_message']}")
        for decision in probe["policy_decisions"]:
            mode_text = ""
            if str(decision["subject_name"]) == "c2pa":
                mode_text = (
                    f" mode={decision['c2pa_mode_name']}"
                    f" source={decision['c2pa_source_kind_name']}"
                    f" output={decision['c2pa_prepared_output_name']}"
                )
            print(
                "  policy[{subject}]: requested={requested} effective={effective} "
                "reason={reason}{mode} matched={matched}".format(
                    subject=str(decision["subject_name"]),
                    requested=str(decision["requested_name"]),
                    effective=str(decision["effective_name"]),
                    reason=str(decision["reason_name"]),
                    mode=mode_text,
                    matched=int(decision["matched_entries"]),
                )
            )
            if decision["message"]:
                print(
                    f"  policy[{decision['subject_name']}]_message={decision['message']}"
                )
        rewrite = probe["c2pa_rewrite"]
        if (
            str(rewrite["state_name"]) != "not_applicable"
            or int(rewrite["matched_entries"]) > 0
        ):
            print(
                f"  c2pa_rewrite: state={rewrite['state_name']} "
                f"target={rewrite['target_format_name']} "
                f"source={rewrite['source_kind_name']} "
                f"matched={int(rewrite['matched_entries'])} "
                f"existing_segments={int(rewrite['existing_carrier_segments'])} "
                f"carrier_available={'yes' if rewrite['target_carrier_available'] else 'no'} "
                f"invalidates_existing={'yes' if rewrite['content_change_invalidates_existing'] else 'no'}"
            )
            if (
                rewrite["requires_manifest_builder"]
                or rewrite["requires_content_binding"]
                or rewrite["requires_certificate_chain"]
                or rewrite["requires_private_key"]
                or rewrite["requires_signing_time"]
            ):
                print(
                    "  c2pa_rewrite_requirements: "
                    f"manifest_builder={'yes' if rewrite['requires_manifest_builder'] else 'no'} "
                    f"content_binding={'yes' if rewrite['requires_content_binding'] else 'no'} "
                    f"certificate_chain={'yes' if rewrite['requires_certificate_chain'] else 'no'} "
                    f"private_key={'yes' if rewrite['requires_private_key'] else 'no'} "
                    f"signing_time={'yes' if rewrite['requires_signing_time'] else 'no'}"
                )
            if rewrite["message"]:
                print(f"  c2pa_rewrite_message={rewrite['message']}")
            if int(rewrite["content_binding_bytes"]) > 0:
                print(
                    f"  c2pa_rewrite_binding: chunks={len(rewrite['content_binding_chunks'])} "
                    f"bytes={int(rewrite['content_binding_bytes'])}"
                )
                for chunk in rewrite["content_binding_chunks"]:
                    if str(chunk["kind_name"]) == "source_range":
                        print(
                            f"  c2pa_rewrite_chunk[{int(chunk['index'])}]: "
                            f"kind={chunk['kind_name']} "
                            f"offset={int(chunk['source_offset'])} "
                            f"size={int(chunk['size'])}"
                        )
                    elif str(chunk["kind_name"]) == "prepared_bmff_meta_box":
                        print(
                            f"  c2pa_rewrite_chunk[{int(chunk['index'])}]: "
                            f"kind={chunk['kind_name']} "
                            f"size={int(chunk['size'])}"
                        )
                    else:
                        print(
                            f"  c2pa_rewrite_chunk[{int(chunk['index'])}]: "
                            f"kind={chunk['kind_name']} "
                            f"block={int(chunk['block_index'])} "
                            f"marker=0x{int(chunk['jpeg_marker_code']):02X} "
                            f"size={int(chunk['size'])} "
                            f"route={chunk['route']}"
                        )
        sign_request = probe["c2pa_sign_request"]
        if (
            str(sign_request["rewrite_state_name"]) != "not_applicable"
            or int(sign_request["content_binding_bytes"]) > 0
        ):
            print(
                f"  c2pa_sign_request: status={sign_request['status_name']} "
                f"carrier={sign_request['carrier_route'] or '-'} "
                f"manifest_label={sign_request['manifest_label'] or '-'} "
                f"source_ranges={int(sign_request['source_range_chunks'])} "
                f"prepared_segments={int(sign_request['prepared_segment_chunks'])} "
                f"bytes={int(sign_request['content_binding_bytes'])}"
            )
        if sign_request["message"]:
            print(f"  c2pa_sign_request_message={sign_request['message']}")
        if probe["c2pa_binding_requested"]:
            print(
                f"  c2pa_binding: status={probe['c2pa_binding_status_name']} "
                f"code={probe['c2pa_binding_code_name']} "
                f"bytes={int(probe['c2pa_binding_bytes_written'])} "
                f"errors={int(probe['c2pa_binding_errors'])}"
            )
            if probe["c2pa_binding_message"]:
                print(f"  c2pa_binding_message={probe['c2pa_binding_message']}")
        if probe["c2pa_handoff_requested"]:
            print(
                f"  c2pa_handoff: status={probe['c2pa_handoff_status_name']} "
                f"code={probe['c2pa_handoff_code_name']} "
                f"bytes={int(probe['c2pa_handoff_bytes_written'])} "
                f"errors={int(probe['c2pa_handoff_errors'])}"
            )
            if probe["c2pa_handoff_message"]:
                print(f"  c2pa_handoff_message={probe['c2pa_handoff_message']}")
        if probe["c2pa_signed_package_requested"]:
            print(
                f"  c2pa_signed_package: status={probe['c2pa_signed_package_status_name']} "
                f"code={probe['c2pa_signed_package_code_name']} "
                f"bytes={int(probe['c2pa_signed_package_bytes_written'])} "
                f"errors={int(probe['c2pa_signed_package_errors'])}"
            )
            if probe["c2pa_signed_package_message"]:
                print(
                    f"  c2pa_signed_package_message={probe['c2pa_signed_package_message']}"
                )
        if probe["transfer_payload_batch_requested"]:
            print(
                f"  transfer_payload_batch: status={probe['transfer_payload_batch_status_name']} "
                f"code={probe['transfer_payload_batch_code_name']} "
                f"bytes={int(probe['transfer_payload_batch_bytes_written'])} "
                f"errors={int(probe['transfer_payload_batch_errors'])}"
            )
            if probe["transfer_payload_batch_message"]:
                print(
                    f"  transfer_payload_batch_message={probe['transfer_payload_batch_message']}"
                )
        if probe["transfer_package_batch_requested"]:
            print(
                f"  transfer_package_batch: status={probe['transfer_package_batch_status_name']} "
                f"code={probe['transfer_package_batch_code_name']} "
                f"bytes={int(probe['transfer_package_batch_bytes_written'])} "
                f"errors={int(probe['transfer_package_batch_errors'])}"
            )
            if probe["transfer_package_batch_message"]:
                print(
                    f"  transfer_package_batch_message={probe['transfer_package_batch_message']}"
                )
        if probe["jxl_encoder_handoff_requested"]:
            print(
                f"  jxl_encoder_handoff: status={probe['jxl_encoder_handoff_status_name']} "
                f"code={probe['jxl_encoder_handoff_code_name']} "
                f"bytes={int(probe['jxl_encoder_handoff_bytes_written'])} "
                f"errors={int(probe['jxl_encoder_handoff_errors'])}"
            )
            if probe["jxl_encoder_handoff_message"]:
                print(
                    f"  jxl_encoder_handoff_message={probe['jxl_encoder_handoff_message']}"
                )
        if probe["c2pa_stage_requested"]:
            print(
                f"  c2pa_stage_validate: status={probe['c2pa_stage_validation_status_name']} "
                f"code={probe['c2pa_stage_validation_code_name']} "
                f"kind={probe['c2pa_stage_validation_payload_kind_name']} "
                f"payload_bytes={int(probe['c2pa_stage_validation_logical_payload_bytes'])} "
                f"carrier_bytes={int(probe['c2pa_stage_validation_staged_payload_bytes'])} "
                f"segments={int(probe['c2pa_stage_validation_staged_segments'])} "
                f"errors={int(probe['c2pa_stage_validation_errors'])}"
            )
            print(
                f"  c2pa_stage_semantics: status={probe['c2pa_stage_validation_semantic_status_name']} "
                f"reason={probe['c2pa_stage_validation_semantic_reason']} "
                f"manifest={int(probe['c2pa_stage_validation_semantic_manifest_present'])} "
                f"manifests={int(probe['c2pa_stage_validation_semantic_manifest_count'])} "
                f"claim_generator={int(probe['c2pa_stage_validation_semantic_claim_generator_present'])} "
                f"assertions={int(probe['c2pa_stage_validation_semantic_assertion_count'])} "
                f"claims={int(probe['c2pa_stage_validation_semantic_claim_count'])} "
                f"signatures={int(probe['c2pa_stage_validation_semantic_signature_count'])} "
                f"linked={int(probe['c2pa_stage_validation_semantic_signature_linked'])} "
                f"orphan={int(probe['c2pa_stage_validation_semantic_signature_orphan'])} "
                f"explicit_refs={int(probe['c2pa_stage_validation_semantic_explicit_reference_signature_count'])} "
                f"unresolved={int(probe['c2pa_stage_validation_semantic_explicit_reference_unresolved_signature_count'])} "
                f"ambiguous={int(probe['c2pa_stage_validation_semantic_explicit_reference_ambiguous_signature_count'])}"
            )
            print(
                f"  c2pa_stage_linkage: claim0_assertions={int(probe['c2pa_stage_validation_semantic_primary_claim_assertion_count'])} "
                f"claim0_refs={int(probe['c2pa_stage_validation_semantic_primary_claim_referenced_by_signature_count'])} "
                f"sig0_links={int(probe['c2pa_stage_validation_semantic_primary_signature_linked_claim_count'])}"
            )
            print(
                f"  c2pa_stage_references: sig0_keys={int(probe['c2pa_stage_validation_semantic_primary_signature_reference_key_hits'])} "
                f"sig0_present={int(probe['c2pa_stage_validation_semantic_primary_signature_explicit_reference_present'])} "
                f"sig0_resolved={int(probe['c2pa_stage_validation_semantic_primary_signature_explicit_reference_resolved_claim_count'])}"
            )
            if probe["c2pa_stage_validation_message"]:
                print(
                    f"  c2pa_stage_validate_message={probe['c2pa_stage_validation_message']}"
                )
            print(
                f"  c2pa_stage: status={probe['c2pa_stage_status_name']} "
                f"code={probe['c2pa_stage_code_name']} "
                f"emitted={int(probe['c2pa_stage_emitted'])} "
                f"removed={int(probe['c2pa_stage_skipped'])} "
                f"errors={int(probe['c2pa_stage_errors'])}"
            )
            if probe["c2pa_stage_message"]:
                print(f"  c2pa_stage_message={probe['c2pa_stage_message']}")
        if probe["time_patch_message"]:
            print(f"  time_patch_message={probe['time_patch_message']}")
        if probe["error_message"]:
            print(f"  error_message={probe['error_message']}")

        if probe["file_status"] != openmeta.TransferFileStatus.Ok:
            rc = 1
            continue

        if args.dump_c2pa_binding:
            if os.path.exists(args.dump_c2pa_binding) and not args.force:
                sys.stderr.write(
                    f"  c2pa binding output exists: {args.dump_c2pa_binding} (use --force)\n"
                )
                rc = 1
                continue
            binding = probe["c2pa_binding_bytes"]
            if binding is None:
                sys.stderr.write("  c2pa_binding_bytes missing from probe result\n")
                rc = 1
                continue
            os.makedirs(os.path.dirname(args.dump_c2pa_binding) or ".", exist_ok=True)
            with open(args.dump_c2pa_binding, "wb") as f:
                f.write(binding)
            print(
                f"  c2pa_binding_output={args.dump_c2pa_binding} bytes={len(binding)}"
            )
        if args.dump_c2pa_handoff:
            if os.path.exists(args.dump_c2pa_handoff) and not args.force:
                sys.stderr.write(
                    f"  c2pa handoff output exists: {args.dump_c2pa_handoff} (use --force)\n"
                )
                rc = 1
                continue
            handoff = probe["c2pa_handoff_bytes"]
            if handoff is None:
                sys.stderr.write("  c2pa_handoff_bytes missing from probe result\n")
                rc = 1
                continue
            os.makedirs(os.path.dirname(args.dump_c2pa_handoff) or ".", exist_ok=True)
            with open(args.dump_c2pa_handoff, "wb") as f:
                f.write(handoff)
            print(
                f"  c2pa_handoff_output={args.dump_c2pa_handoff} bytes={len(handoff)}"
            )
        if args.dump_c2pa_signed_package:
            if os.path.exists(args.dump_c2pa_signed_package) and not args.force:
                sys.stderr.write(
                    f"  signed c2pa package output exists: {args.dump_c2pa_signed_package} (use --force)\n"
                )
                rc = 1
                continue
            signed_package = probe["c2pa_signed_package_bytes"]
            if signed_package is None:
                sys.stderr.write(
                    "  c2pa_signed_package_bytes missing from probe result\n"
                )
                rc = 1
                continue
            os.makedirs(
                os.path.dirname(args.dump_c2pa_signed_package) or ".", exist_ok=True
            )
            with open(args.dump_c2pa_signed_package, "wb") as f:
                f.write(signed_package)
            print(
                f"  c2pa_signed_package_output={args.dump_c2pa_signed_package} bytes={len(signed_package)}"
            )
        if args.dump_transfer_payload_batch:
            if os.path.exists(args.dump_transfer_payload_batch) and not args.force:
                sys.stderr.write(
                    f"  transfer payload batch output exists: {args.dump_transfer_payload_batch} (use --force)\n"
                )
                rc = 1
                continue
            payload_batch = probe["transfer_payload_batch_bytes"]
            if payload_batch is None:
                sys.stderr.write(
                    "  transfer_payload_batch_bytes missing from probe result\n"
                )
                rc = 1
                continue
            os.makedirs(
                os.path.dirname(args.dump_transfer_payload_batch) or ".",
                exist_ok=True,
            )
            with open(args.dump_transfer_payload_batch, "wb") as f:
                f.write(payload_batch)
            print(
                f"  transfer_payload_batch_output={args.dump_transfer_payload_batch} bytes={len(payload_batch)}"
            )
        if args.dump_transfer_package_batch:
            if os.path.exists(args.dump_transfer_package_batch) and not args.force:
                sys.stderr.write(
                    f"  transfer package batch output exists: {args.dump_transfer_package_batch} (use --force)\n"
                )
                rc = 1
                continue
            package_batch = probe["transfer_package_batch_bytes"]
            if package_batch is None:
                sys.stderr.write(
                    "  transfer_package_batch_bytes missing from probe result\n"
                )
                rc = 1
                continue
            os.makedirs(
                os.path.dirname(args.dump_transfer_package_batch) or ".",
                exist_ok=True,
            )
            with open(args.dump_transfer_package_batch, "wb") as f:
                f.write(package_batch)
            print(
                f"  transfer_package_batch_output={args.dump_transfer_package_batch} bytes={len(package_batch)}"
            )
        if args.dump_jxl_encoder_handoff:
            if os.path.exists(args.dump_jxl_encoder_handoff) and not args.force:
                sys.stderr.write(
                    f"  jxl encoder handoff output exists: {args.dump_jxl_encoder_handoff} (use --force)\n"
                )
                rc = 1
                continue
            jxl_handoff = probe["jxl_encoder_handoff_bytes"]
            if jxl_handoff is None:
                sys.stderr.write(
                    "  jxl_encoder_handoff_bytes missing from probe result\n"
                )
                rc = 1
                continue
            os.makedirs(
                os.path.dirname(args.dump_jxl_encoder_handoff) or ".",
                exist_ok=True,
            )
            with open(args.dump_jxl_encoder_handoff, "wb") as f:
                f.write(jxl_handoff)
            print(
                f"  jxl_encoder_handoff_output={args.dump_jxl_encoder_handoff} bytes={len(jxl_handoff)}"
            )

        allow_output_only_success = (
            not edit_requested
            and not write_payloads
            and (
                (args.dump_c2pa_binding and probe["c2pa_binding_status"] == openmeta.TransferStatus.Ok)
                or (args.dump_c2pa_handoff and probe["c2pa_handoff_status"] == openmeta.TransferStatus.Ok)
                or (
                    args.dump_c2pa_signed_package
                    and probe["c2pa_signed_package_status"] == openmeta.TransferStatus.Ok
                )
                or (
                    args.dump_transfer_payload_batch
                    and probe["transfer_payload_batch_status"] == openmeta.TransferStatus.Ok
                )
                or (
                    args.dump_transfer_package_batch
                    and probe["transfer_package_batch_status"] == openmeta.TransferStatus.Ok
                )
                or (
                    args.dump_jxl_encoder_handoff
                    and probe["jxl_encoder_handoff_status"] == openmeta.TransferStatus.Ok
                )
            )
        )

        for block in probe["blocks"]:
            idx = int(block["index"])
            route = str(block["route"])
            size = int(block["size"])
            print(
                f"  [{idx}] route={route} kind={_transfer_kind_name(block['kind'])} size={size}"
            )
            if not write_payloads:
                continue
            out_path = _payload_dump_path(path, args.out_dir, route, idx)
            if os.path.exists(out_path) and not args.force:
                sys.stderr.write(f"  [{idx}] exists: {out_path} (use --force)\n")
                rc = 1
                continue
            payload = block["payload"]
            if payload is None:
                sys.stderr.write(
                    f"  [{idx}] payload_missing (internal probe payload disabled)\n"
                )
                rc = 1
                continue
            os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
            with open(out_path, "wb") as f:
                f.write(payload)
            print(f"  [{idx}] wrote={out_path}")

        print(
            f"  compile: status={probe['compile_status_name']} code={probe['compile_code_name']} "
            f"ops={int(probe['compile_ops'])}"
        )
        if probe["compile_message"]:
            print(f"  compile_message={probe['compile_message']}")

        print(
            f"  emit: status={probe['emit_status_name']} emitted={probe['emit_emitted']} "
            f"skipped={probe['emit_skipped']} errors={probe['emit_errors']}"
        )
        if probe["emit_message"]:
            print(f"  emit_message={probe['emit_message']}")

        if edit_requested:
            print(
                f"  edit_plan: status={probe['edit_plan_status_name']} input={int(probe['edit_input_size'])} "
                f"output={int(probe['edit_output_size'])}"
            )
            if probe["edit_plan_message"]:
                print(f"  edit_plan_message={probe['edit_plan_message']}")
            print(
                f"  edit_apply: status={probe['edit_apply_status_name']} code={probe['edit_apply_code_name']} "
                f"emitted={int(probe['edit_apply_emitted'])} skipped={int(probe['edit_apply_skipped'])} "
                f"errors={int(probe['edit_apply_errors'])}"
            )
            if probe["edit_apply_message"]:
                print(f"  edit_apply_message={probe['edit_apply_message']}")

            if persist_output:
                if probe["persist_output_status"] == openmeta.TransferStatus.Ok:
                    print(
                        f"  output={probe['persist_output_path']} bytes={int(probe['persist_output_bytes'])}"
                    )
                elif probe["persist_output_message"]:
                    print(f"  persist_output_message={probe['persist_output_message']}")

                if int(probe["persist_xmp_sidecar_bytes"]) > 0:
                    print(
                        f"  xmp_sidecar_output={probe['persist_xmp_sidecar_path']} "
                        f"bytes={int(probe['persist_xmp_sidecar_bytes'])}"
                    )
                elif (
                    args.xmp_writeback in ("sidecar", "embedded_and_sidecar")
                    and probe["persist_xmp_sidecar_status"] != openmeta.TransferStatus.Ok
                    and probe["persist_xmp_sidecar_message"]
                ):
                    print(
                        f"  persist_xmp_sidecar_message={probe['persist_xmp_sidecar_message']}"
                    )

                cleanup_path = str(probe["persist_xmp_sidecar_cleanup_path"] or "")
                if cleanup_path:
                    if bool(probe["persist_xmp_sidecar_cleanup_removed"]):
                        print(f"  xmp_sidecar_removed={cleanup_path}")
                    elif (
                        args.xmp_destination_sidecar == "strip_existing"
                        and probe["persist_xmp_sidecar_cleanup_status"]
                        == openmeta.TransferStatus.Ok
                    ):
                        print(f"  xmp_sidecar_remove={cleanup_path}")

        if (
            probe["overall_status"] != openmeta.TransferStatus.Ok
            and not allow_output_only_success
        ):
            rc = 1
            continue
        for m in probe["marker_summary"]:
            marker = int(m["marker"])
            print(
                f"  marker {_marker_name(marker)} count={int(m['count'])} bytes={int(m['bytes'])}"
            )
        for t in probe["tiff_tag_summary"]:
            print(
                f"  tiff_tag {int(t['tag'])} count={int(t['count'])} bytes={int(t['bytes'])}"
            )
        jxl_handoff = probe.get("jxl_encoder_handoff")
        if (
            jxl_handoff is not None
            and str(jxl_handoff["status_name"]) == "ok"
            and bool(jxl_handoff["has_icc_profile"])
        ):
            print(
                f"  jxl_icc_profile bytes={int(jxl_handoff['icc_profile_bytes'])}"
            )
        for b in probe["jxl_box_summary"]:
            print(
                f"  jxl_box {str(b['type'])} count={int(b['count'])} bytes={int(b['bytes'])}"
            )
        for c in probe["webp_chunk_summary"]:
            print(
                f"  webp_chunk {str(c['type'])} count={int(c['count'])} bytes={int(c['bytes'])}"
            )
        for c in probe["png_chunk_summary"]:
            print(
                f"  png_chunk {str(c['type'])} count={int(c['count'])} bytes={int(c['bytes'])}"
            )
        for a in probe["exr_attribute_summary"]:
            print(
                f"  exr_attribute {str(a['name'])} type={str(a['type_name'])} "
                f"count={int(a['count'])} bytes={int(a['bytes'])}"
            )
        for b in probe["jp2_box_summary"]:
            print(
                f"  jp2_box {str(b['type'])} count={int(b['count'])} bytes={int(b['bytes'])}"
            )
        for item in probe["bmff_item_summary"]:
            print(
                "  bmff_item "
                f"{_bmff_item_name(int(item['item_type']), bool(item['mime_xmp']))} "
                f"count={int(item['count'])} bytes={int(item['bytes'])}"
            )
        for prop in probe["bmff_property_summary"]:
            print(
                "  bmff_property "
                f"{_bmff_property_name(int(prop['property_type']), int(prop['property_subtype']))} "
                f"count={int(prop['count'])} bytes={int(prop['bytes'])}"
            )
        if probe["tiff_commit"]:
            print("  tiff_commit=true")

    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
