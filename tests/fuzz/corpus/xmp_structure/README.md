# Custom XMP structure fuzz corpus

`custom_nested_langalt_escaped.xml` is a bounded, valid XMP packet for the
`openmeta_fuzz_xmp_structure` target. It exercises opt-in custom structure
retention, nested Seq/Alt and Bag containers, an empty Seq, an escaped nested
URI name, XML entity decoding, and CR/whitespace text. The harness also builds
a separate bounded custom tree, applies ordered edits, and checks marker/value
preservation through portable dump and decode.

`generated_variation_seq_remove.bin` starts with selector byte `0x1f`. That
selects a Seq instead of a Bag, removal of a sparse third item, a lowered
structure-node limit, invalid container authoring, and removal of a declared
parent while children remain. The remaining bytes are the valid XML seed and
also influence generated text. Its leading control byte intentionally makes
the raw-XML path skip decoding; the XML-only seed covers that path.
