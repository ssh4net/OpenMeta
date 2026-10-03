Security
========

OpenMeta assumes metadata comes from untrusted files. The main risks are memory
corruption, resource exhaustion (including decompression bombs), and output
injection (terminals, JSON/XML, logs).

What the code does
------------------

- Bounds-checks every offset/size before reading.
- Guards arithmetic when computing sizes (overflow checks + range validation).
- Enforces explicit decode limits (entries/IFDs/value bytes).
- Sanitizes console output (ASCII-only, escapes control/non-ASCII, truncates).
- Caps decompression output size and part counts.

Recursion and traversal recommendations
--------------------------------------

OpenMeta does not allow unlimited recursion by design. Use explicit caps per
metadata block family:

- EXIF/TIFF + MakerNotes (IFD graph):
  ``exif_limits.max_ifds <= 128``, ``max_entries_per_ifd <= 4096``,
  ``max_total_entries <= 200000``.
- XMP RDF/XML nesting:
  ``xmp_limits.max_depth <= 128``, ``xmp_limits.max_properties <= 200000``.
- JUMBF/C2PA (BMFF + CBOR nesting):
  ``jumbf_limits.max_box_depth <= 32``, ``max_boxes <= 65536``,
  ``max_cbor_depth <= 64``, ``max_cbor_items <= 200000``. Keep
  ``max_semantic_work`` finite; zero selects the library's finite default
  rather than disabling this budget.
- BMFF metadata discovery (HEIF/AVIF/CR3/JP2/JXL): internal depth and box-count
  caps are enforced per path (no unlimited traversal).
- CR3 preview UUID walk: internal caps ``depth <= 16`` and ``boxes <= 65536``.
- CRW/CIFF directory decode: internal cap ``depth <= 32`` plus EXIF entry
  budgets.

For JUMBF preflight checks, use
``measure_jumbf_structure(std::span<const std::byte>, limits)`` before full
decode. For global defaults across decoders, initialize policy with
``recommended_resource_policy()``.

Safe/unsafe contract
--------------------

- Safe paths are default: they return explicit status/issues for malformed or
  unsafe metadata text and do not silently fall back to raw bytes.
- Unsafe paths are explicit opt-in and may expose raw metadata bytes, while
  still enforcing memory/path/resource safety checks.
- Safe console rendering uses structured placeholders for unsafe text (for
  example ``<CORRUPTED_TEXT:...>``) to avoid display/log injection confusion.

Validation issue codes
----------------------

``metavalidate`` exposes machine-readable issue codes that can be consumed by
gates and CI (for example ``xmp/output_truncated`` and
``xmp/invalid_or_malformed_xml_text``).

What the tests do
-----------------

- Unit tests cover normal and malformed inputs.
- libFuzzer and FuzzTest targets exercise parsers under sanitizers.

Some optional release/interop tests can launch external tools such as ExifTool
to validate files produced by OpenMeta. Treat those tools as part of your test
environment, not as OpenMeta runtime dependencies. On macOS, use a patched
ExifTool version when running validations against untrusted files; older
ExifTool releases have had macOS metadata-write command-injection issues in
their own tooling.

Reporting vulnerabilities
-------------------------

If you find a security issue, please use GitHub private vulnerability
reporting:

- https://github.com/ssh4net/OpenMeta/security/advisories/new

Please include a minimal reproducer file (or hex snippet), build flags, and
stack trace. Avoid publishing exploit details until a fix is available.

For the full policy and threat model, see ``SECURITY.md`` in the repository.
