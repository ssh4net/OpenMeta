# Write lifecycle fuzz corpus

These compact selector inputs target `openmeta_fuzz_write_lifecycle`. Each
input drives a mixed-origin DC creator/subject source through a direct portable
XMP dump and through the public in-memory embedded-carrier merge API. It also
selects a custom `Record` root kind and provider shape, dumps both providers to
in-memory JPEG XMP carriers, and checks whole-root precedence plus independent
same-local-name roots in distinct namespace URIs. The harness checks both
`SourceWins` and `DestinationWins` on every input, then selects one primary ICC
removal route: JPEG, TIFF, or DNG.

- `sparse_multi_origin_source.bin` selects a higher sparse starting index for
  the source collection and distinct creator text values.
- `repeated_multi_origin_source.bin` selects a lower sparse starting index and
  repeated creator text at distinct indices. Subject keywords repeat in both
  seeds.
- `icc_jpeg.bin`, `icc_tiff.bin`, and `icc_dng.bin` select primary ICC removal
  for each supported container route. Their remaining bytes vary the opaque
  ICC profile length/content and the byte changed before removal.
- `custom_root_struct.bin`, `custom_root_bag_empty.bin`,
  `custom_root_seq_scalar.bin`, `custom_root_lang_alt.bin`, and
  `custom_root_lang_alt_empty.bin` cover the declared custom root kinds.
  `custom_root_struct_empty.bin` and `custom_root_seq_empty.bin` add explicit
  empty Struct and Seq declarations, completing empty-marker seeds for all
  four kinds. `custom_root_differing_kind.bin` selects a Struct source root and
  a Seq destination root. Selector byte 5 chooses the source kind and byte 10
  independently chooses the destination kind. Other selector bytes exercise
  repeated text, sparse indexed children, and an earlier bare scalar against a
  later declared tree. Exact readback checks reject losing leaves or indices
  while retaining each provider's independent root.

The harness creates small JPEG, TIFF/DNG, ICC, and XMP carriers in memory;
corpus files contain only route selectors and bounded mutation bytes. The ICC
path also truncates a copy of the JPEG ICC segment or changes a TIFF/DNG IFD0
offset in a malformed copy, without changing byte order or TIFF version.
