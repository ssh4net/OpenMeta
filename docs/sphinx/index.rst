OpenMeta
========

OpenMeta is a C++ library for reading metadata across common image and media
containers. It does not decode, decompress, demosaic, or render image pixels.

What it does today:

- Scans files to locate metadata blocks (EXIF, XMP, ICC, IPTC, Photoshop IRB, MPF,
  comments, JUMBF/C2PA hints, and BMFF item-graph metadata).
- Extracts and optionally decompresses block payloads (size-limited).
- Decodes common blocks into a typed, normalized in-memory model:
  EXIF/TIFF-IFD tags, XMP properties, IPTC-IIM datasets, ICC profile
  header/tag tables, Photoshop IRB (8BIM) resource blocks, BMFF derived fields,
  and EXR header attributes.
- Exposes sidecar dump paths (lossless and portable) and validation APIs/tools
  (`metavalidate`) with machine-readable issue codes for CI gating.

Read-path coverage snapshot:

- Tracked HEIC/HEIF, CR3, and mixed RAW EXIF compare gates are passing.
- EXR header metadata compare gate is passing for the documented contract.
- MakerNote support is broad and baseline-gated; unknown tags remain lossless.
- RAW read-depth gaps are tracked by family in :doc:`raw_read_parity_plan`.

Camera RAW support refers to metadata carrier discovery and documented
metadata interpretation. It does not imply RAW pixel decoding, complete vendor
private-table interpretation, or native camera RAW writing.

OpenMeta is designed to treat metadata as **untrusted input** and provides
explicit limits and sanitization controls. See :doc:`security` for current
known limitations. The optional C2PA verification scaffold is diagnostic only
and must not be used as an asset-authenticity or trust gate.

.. toctree::
   :maxdepth: 2

   quick_start
   host_integration
   host_adoption_profile
   prepared_transfer_handoff
   random_access_input
   api_stability
   flat_host_mapping
   compatibility_dump
   xmp_sync_policy
   writer_target_contract
   interpretation_status
   fuzzy_search
   creation
   generic_authoring
   editing
   translation
   canonical_serialization
   raw_read_parity_plan
   build
   shared_library
   development
   interop_api
   exr_metadata_contract
   testing
   security
   api
