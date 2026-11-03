# Write lifecycle fuzz corpus

These compact selector inputs target `openmeta_fuzz_write_lifecycle`. Each
input drives a mixed-origin DC creator/subject source through a direct portable
XMP dump and through the public in-memory embedded-carrier merge API. The
harness checks both `SourceWins` and `DestinationWins` on every input, then
selects one primary ICC removal route: JPEG, TIFF, or DNG.

- `sparse_multi_origin_source.bin` selects a higher sparse starting index for
  the source collection and distinct creator text values.
- `repeated_multi_origin_source.bin` selects a lower sparse starting index and
  repeated creator text at distinct indices. Subject keywords repeat in both
  seeds.
- `icc_jpeg.bin`, `icc_tiff.bin`, and `icc_dng.bin` select primary ICC removal
  for each supported container route. Their remaining bytes vary the opaque
  ICC profile length/content and the byte changed before removal.

The harness creates small JPEG, TIFF/DNG, ICC, and XMP carriers in memory;
corpus files contain only route selectors and bounded mutation bytes. The ICC
path also truncates a copy of the JPEG ICC segment or changes a TIFF/DNG IFD0
offset in a malformed copy, without changing byte order or TIFF version.
