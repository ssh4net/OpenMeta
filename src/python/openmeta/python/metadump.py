#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import sys

try:
    import openmeta
except ModuleNotFoundError:
    sys.stderr.write("error: Python module 'openmeta' was not found.\n")
    sys.stderr.write("Run this script with the same Python environment where the OpenMeta wheel is installed.\n")
    sys.stderr.write("\n")
    sys.stderr.write("Examples:\n")
    sys.stderr.write("  uv run python -m openmeta.python.metadump <file>\n")
    sys.stderr.write("  /path/to/venv/bin/python -m openmeta.python.metadump <file>\n")
    raise SystemExit(2)


def _default_out_path(path: str, out_dir: str) -> str:
    if out_dir:
        base = os.path.basename(path)
        return os.path.join(out_dir, base + ".xmp")
    return path + ".xmp"


def _has_known_output_extension(path: str) -> bool:
    lower = path.lower()
    return lower.endswith(".xmp") or lower.endswith(".jpg") or lower.endswith(".bin")


def _looks_like_output_path(path: str) -> bool:
    if _has_known_output_extension(path):
        return True
    return "/" in path or "\\" in path


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="metadump.py")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--no-build-info", action="store_true", help="hide OpenMeta build info header")
    ap.add_argument("--format", choices=["lossless", "portable"], default="lossless", help="XMP output format")
    ap.add_argument("--portable", action="store_true", help="alias for --format portable")
    ap.add_argument("--portable-no-exif", action="store_true", help="portable mode: skip EXIF/TIFF/GPS mapped fields")
    ap.add_argument(
        "--portable-include-existing-xmp",
        action="store_true",
        help="portable mode: include decoded standard XMP properties",
    )
    ap.add_argument(
        "--portable-exiftool-gpsdatetime-alias",
        action="store_true",
        help="portable mode: emit exif:GPSDateTime alias for GPS time",
    )
    ap.add_argument("--out", type=str, default="", help="output path (single input only)")
    ap.add_argument("--out-dir", type=str, default="", help="output directory (multiple inputs)")
    ap.add_argument("--force", action="store_true", help="overwrite existing output files")
    ap.add_argument("--xmp-sidecar", action="store_true", help="also read sidecar XMP (<file>.xmp, <basename>.xmp)")
    ap.add_argument("--c2pa-verify", action="store_true", help="request draft C2PA verify scaffold evaluation")
    ap.add_argument(
        "--c2pa-verify-backend",
        choices=["none", "auto", "native", "openssl"],
        default="auto",
        help="verification backend preference",
    )
    ap.add_argument(
        "--c2pa-verify-require-trusted-chain",
        action="store_true",
        help="fail C2PA verify when the certificate chain is untrusted or missing",
    )
    ap.add_argument(
        "--c2pa-verify-require-resolved-references",
        action="store_true",
        help="fail C2PA verify when explicit references are unresolved or ambiguous",
    )
    ap.add_argument("--no-pointer-tags", action="store_true", help="do not store pointer tags")
    ap.add_argument("--makernotes", action="store_true", help="attempt MakerNote decode (best-effort)")
    ap.add_argument("--no-decompress", action="store_true", help="do not decompress payloads")
    ap.add_argument("--max-file-bytes", type=int, default=0, help="optional file mapping cap in bytes (0=unlimited)")
    ap.add_argument("--max-output-bytes", type=int, default=0, help="refuse to generate dumps larger than N bytes (0=unlimited)")
    ap.add_argument("--max-entries", type=int, default=0, help="refuse to emit more than N entries (0=unlimited)")
    args = ap.parse_args(argv)

    if args.portable:
        args.format = "portable"

    input_paths = list(args.files)
    if len(input_paths) == 2 and not args.out and not args.out_dir:
        candidate = input_paths[1]
        second_is_output_hint = _has_known_output_extension(candidate) or (
            not os.path.exists(candidate) and _looks_like_output_path(candidate)
        )
        if second_is_output_hint:
            args.out = candidate
            input_paths = [input_paths[0]]

    if args.out and len(input_paths) != 1:
        sys.stderr.write("error: --out requires exactly one input file\n")
        return 2

    if not args.no_build_info:
        l1, l2 = openmeta.info_lines()
        print(l1)
        print(l2)
        print(openmeta.python_info_line())

    rc = 0
    c2pa_backend_none = getattr(openmeta.C2paVerifyBackend, "None")
    backend_map = {
        "none": c2pa_backend_none,
        "auto": openmeta.C2paVerifyBackend.Auto,
        "native": openmeta.C2paVerifyBackend.Native,
        "openssl": openmeta.C2paVerifyBackend.OpenSsl,
    }
    status_map = {
        openmeta.C2paVerifyStatus.NotRequested: "not_requested",
        openmeta.C2paVerifyStatus.DisabledByBuild: "disabled_by_build",
        openmeta.C2paVerifyStatus.BackendUnavailable: "backend_unavailable",
        openmeta.C2paVerifyStatus.NoSignatures: "no_signatures",
        openmeta.C2paVerifyStatus.InvalidSignature: "invalid_signature",
        openmeta.C2paVerifyStatus.VerificationFailed: "verification_failed",
        openmeta.C2paVerifyStatus.Verified: "verified",
        openmeta.C2paVerifyStatus.NotImplemented: "not_implemented",
        openmeta.C2paVerifyStatus.SignatureVerifiedOnly:
            "signature_verified_only",
    }
    backend_name_map = {
        c2pa_backend_none: "none",
        openmeta.C2paVerifyBackend.Auto: "auto",
        openmeta.C2paVerifyBackend.Native: "native",
        openmeta.C2paVerifyBackend.OpenSsl: "openssl",
    }
    for path in input_paths:
        out_path = args.out if args.out else _default_out_path(path, args.out_dir)

        if os.path.exists(out_path) and not args.force:
            sys.stderr.write(f"metadump: refusing to overwrite '{out_path}' (use --force)\n")
            rc = 1
            continue

        doc = openmeta.read(
            path,
            include_pointer_tags=not args.no_pointer_tags,
            decode_makernote=bool(args.makernotes),
            decompress=not args.no_decompress,
            include_xmp_sidecar=bool(args.xmp_sidecar),
            verify_c2pa=bool(args.c2pa_verify),
            verify_backend=backend_map[args.c2pa_verify_backend],
            verify_require_trusted_chain=bool(
                args.c2pa_verify_require_trusted_chain
            ),
            verify_require_resolved_references=bool(
                args.c2pa_verify_require_resolved_references
            ),
            max_file_bytes=int(args.max_file_bytes),
        )

        sidecar_format = (
            openmeta.XmpSidecarFormat.Portable
            if args.format == "portable"
            else openmeta.XmpSidecarFormat.Lossless
        )
        data, res = doc.dump_xmp_sidecar(
            format=sidecar_format,
            max_output_bytes=int(args.max_output_bytes),
            max_entries=int(args.max_entries),
            include_exif=not args.portable_no_exif,
            include_existing_xmp=bool(args.portable_include_existing_xmp),
            portable_exiftool_gpsdatetime_alias=bool(
                args.portable_exiftool_gpsdatetime_alias
            ),
        )

        try:
            os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
            with open(out_path, "wb") as f:
                f.write(data)
        except OSError as e:
            sys.stderr.write(f"metadump: failed to write '{out_path}': {e}\n")
            rc = 1
            continue

        verify_status = status_map.get(doc.jumbf_verify_status, "unknown")
        verify_backend = backend_name_map.get(doc.jumbf_verify_backend, "unknown")
        print(
            f"wrote={out_path} format={args.format} bytes={len(data)} "
            f"entries={res.entries} c2pa_verify={verify_status} c2pa_backend={verify_backend} "
            f"c2pa_require_trusted_chain={'on' if args.c2pa_verify_require_trusted_chain else 'off'} "
            f"c2pa_require_resolved_refs={'on' if args.c2pa_verify_require_resolved_references else 'off'}"
        )

    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
