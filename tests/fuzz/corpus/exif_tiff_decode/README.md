These synthetic seeds exercise Sony C634 SR2-private metadata decryption,
malformed encrypted length, RAF private TIFF F000 traversal/cycles, and a
4096-value DNG linearization table, absolute/relative external Canon settings and standalone MRW chunks. They contain no original camera data.
The Sony ciphertext is independently generated with ExifTool's symmetric
Sony::Decrypt reference for an owned two-entry synthetic IFD.

The EXIF fuzzer exercises memory and bounded callback TIFF decode plus semantic
queries, and both native RAF/MRW paths when their signatures are present. A finite
mutation run provides bounded robustness evidence, not universal format proof.
