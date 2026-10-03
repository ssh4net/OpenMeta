# Security Policy

OpenMeta treats **all file metadata as untrusted input**. Image/container files
and their metadata blocks are a common attack surface (memory corruption,
resource exhaustion, and output/log injection).

## Threat Model

Assume inputs may be maliciously crafted to:
- trigger out-of-bounds reads/writes via bad offsets/sizes/counts,
- cause integer overflows (e.g., `count * element_size`),
- create cycles/recursion in pointer structures (IFD loops),
- force unbounded allocations or decompression bombs ("zip bombs"),
- inject control sequences or confusing Unicode into console/logs/exports.

## Parsing Safety Requirements

All parsers/decoders must:
- Validate **every** offset/length against the available buffer before reading.
- Guard multiplications/additions against overflow and validate computed ranges.
- Enforce explicit resource limits (entries/blocks/value bytes) and fail "soft"
  (skip entry / mark status) rather than crashing.
- Avoid recursion; prefer iterative decoding with visited-set loop detection.
- Preserve unknown/invalid payloads losslessly as `bytes` (do not assume C-strings).

EXIF/TIFF decoding uses `ExifDecodeLimits` to cap IFD count, entry count, and
total value bytes.

## Recursion & Traversal Budgets (Recommended)

OpenMeta design rule: metadata decode must never rely on unlimited recursion or
unbounded graph walks.

Recommended limits by block family:
- EXIF/TIFF + MakerNotes (IFD pointer graph): keep
  `exif_limits.max_ifds <= 128`, `max_entries_per_ifd <= 4096`, and
  `max_total_entries <= 200000`; keep visited-offset loop checks enabled.
- XMP RDF/XML (element nesting): keep `xmp_limits.max_depth <= 128` and
  `xmp_limits.max_properties <= 200000`.
- JUMBF/C2PA (nested BMFF + CBOR): keep `jumbf_limits.max_box_depth <= 32`,
  `max_boxes <= 65536`, `max_cbor_depth <= 64`, and
  `max_cbor_items <= 200000`. Keep `max_semantic_work` finite; zero selects
  the library's finite default rather than disabling this budget.
- BMFF metadata discovery (HEIF/AVIF/CR3/JP2/JXL): uses internal hard caps for
  nested box scans (depth-capped and box-count-capped by decoder path).
- CR3 preview UUID scan: internal hard caps
  (`depth <= 16`, `boxes <= 65536`).
- CRW/CIFF directory decode: internal hard cap (`depth <= 32`) plus EXIF entry
  budgets.

For JUMBF preflight checks, use `estimate_jumbf_structure(...)` before full
decode when you need to enforce stricter deployment-specific depth policy.
For cross-decoder policy setup, start from `recommended_resource_policy()` and
override only what your application needs.

## Output & Export Safety Requirements

Tools and exporters must never emit raw metadata bytes without sanitization.

Safe/unsafe contract:
- `safe` paths are default. They must not silently fall back to raw bytes.
  If text is malformed/unsafe, return an explicit status/issue and keep output
  sanitized.
- `unsafe_...` paths are explicit opt-in for raw bytes/words. They still must
  enforce memory/path/resource safety checks.

Console (`metaread`) rules:
- Print **ASCII-only** output. Non-ASCII and control bytes must be escaped
  (e.g. `\\x1B`, `\\xE2\\x80\\xAE`) to prevent terminal injection and display spoofing.
- Apply strict size limits for printing (`--max-bytes`, `--max-elements`,
  `--max-cell-chars`). File-size caps (`--max-file-bytes`) are optional overrides;
  parser/decode budgets are the primary protection.
- If sanitization or truncation is triggered, annotate with a structured
  placeholder (for example `<CORRUPTED_TEXT:unsafe_console_text:...>`) so users
  don't mistake it for authoritative text.

Structured exports (JSON/XML/XMP/etc.) rules:
- Escape per-format (JSON string escaping; XML entity escaping) and never embed
  untrusted text into markup/attributes without escaping.
- Prefer explicit encodings for binary fields (hex/base64) instead of "best effort" text.
- Preserve provenance: mark values that were lossy-sanitized or truncated.
- Validation should expose machine-readable issue codes (for example
  `xmp/output_truncated`, `xmp/invalid_or_malformed_xml_text`) for gating and
  allowlist workflows.

## Testing & Fuzzing (Required)

Before merging changes that touch parsing, decoding, or exports:
- Build and run unit tests (`OPENMETA_BUILD_TESTS=ON`) with sanitizers when possible.
- Run libFuzzer targets (`OPENMETA_BUILD_FUZZERS=ON`) under ASan/UBSan for a
  reasonable time budget and keep corpus/regressions.
- Run FuzzTest targets (`OPENMETA_BUILD_FUZZTEST=ON`) when enabled in your environment.

Unit tests require GoogleTest to be discoverable via `find_package(GTest CONFIG)`
or `CMAKE_PREFIX_PATH`.

Some optional release/interop tests can launch external tools such as ExifTool
to validate files produced by OpenMeta. Treat those tools as part of your test
environment, not as OpenMeta runtime dependencies. On macOS, use a patched
ExifTool version when running validations against untrusted files; older
ExifTool releases have had macOS metadata-write command-injection issues in
their own tooling.

Example workflow (Linux/Clang):
```bash
cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOPENMETA_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure

cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOPENMETA_BUILD_FUZZERS=ON
cmake --build build-fuzz
./build-fuzz/openmeta_fuzz_exif_tiff_decode -max_total_time=60
```

## Reporting Vulnerabilities

If you find a security issue, please use GitHub private vulnerability
reporting:

- https://github.com/ssh4net/OpenMeta/security/advisories/new

Please provide a minimal reproducer file (or hex snippet), build flags, and
stack trace. Avoid publishing exploit details until a fix is available.
