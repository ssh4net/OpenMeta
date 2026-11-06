# Fuzz Seed Corpus (Public)

This directory contains small, deterministic seed inputs for libFuzzer
targets in `tests/fuzz/`.

## Layout

- `container_scan/`
  - `bmff_method2_iref_v1_ok.bin`: valid BMFF `iloc` method-2 item resolved
    through `iref` version-1 (`32-bit`) item IDs.
  - `bmff_method2_missing_iref.bin`: method-2 item without `iref` mapping.
  - `bmff_method2_extent_index_oob.bin`: method-2 item with explicit
    `extent_index` out of range for available references.
  - `bmff_method2_noidx_ref_mismatch.bin`: method-2 item with `idx_size=0`
    and extent/reference count mismatch.
- `bmff_meta/`
  - minimal payload seeds for the `openmeta_fuzz_bmff_meta` harness.
- `metadata_transfer/`
  - minimal prepared-transfer artifact magic seeds for the
    `openmeta_fuzz_metadata_transfer` harness.

- `exif_tiff_decode/`
  - `canon_afinfo2_declared_scan.bin`: synthetic Canon MakerNote with invalid
    primary AFInfo2 bytes and a valid aligned payload inside its declared span.
  - `cr3_known_box_headers.bin`: synthetic positional CNCV/CTBO metadata with
    XMP/preview UUID and media headers, sharing finite scratch and I/O limits.
- `write_lifecycle/`
  - `icc_png.bin`, `icc_webp.bin`: deterministic selectors for duplicate ICC
    carrier removal, exact rewrite/replay, no-op reapply and rejected copy plans.
    The harness varies payload length/content and WebP odd-byte padding.

## Usage

Use an empty output corpus directory first so seed files stay unchanged:

```bash
mkdir -p build-fuzz/_corpus_out
ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_container_scan \
  build-fuzz/_corpus_out \
  tests/fuzz/corpus/container_scan \
  -runs=500
```
