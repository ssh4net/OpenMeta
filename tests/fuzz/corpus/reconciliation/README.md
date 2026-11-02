# Metadata reconciliation fuzz seeds

`metadata_reconcile_libfuzzer.cc` bounds input to 64 KiB and generates an
authored source with at most 128 entries and a 64 KiB arena. It varies all five
DC/IPTC groups, optional IFD0 text, all six family priorities, conflict and
deletion policies, duplicate keyword values, sparse creators and subjects, and
lowered resource ceilings. Successful runs compare whole `MetaStore` contents
(including exact arena bytes and entry provenance); they do not compare an XMP
dump with the original store or serialize a private snapshot format.

- `priority_0.bin` through `priority_5.bin` select each priority permutation
  while IPTC and EXIF descriptions disagree under `PreferSourceOrder`.
- `lowered_limits.bin` selects a low descriptive-property ceiling.
- `duplicate_keywords.bin` supplies repeated keyword text values.
- `utf8_native_families.bin` supplies valid UTF-8, a same-block IPTC `ESC % G`
  marker, and EXIF type 129 values with UTF-8 encoding.
- `invalid_priority.bin` duplicates a family in the priority array.
- `dirty_title_delete.bin` removes the authored XMP title and selects dirty
  deletion propagation.
- `raw_custom_tree.xml` starts with `<`, so the harness wraps it in a bounded
  JPEG XMP segment and reads it with custom-structure retention and tight XMP
  limits. It contains duplicate subjects, language siblings, and an empty
  custom Seq.
- `empty.bin` covers the zero-byte authored-source path.

The raw XML reader path accepts only a complete successful XMP decode. Fuzz
inputs remain synthetic metadata and do not claim media or codec validity.
