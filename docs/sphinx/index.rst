OpenMeta
========

OpenMeta is a C++ library for reading metadata safely across common image and
media containers.

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

OpenMeta treats metadata as **untrusted input** and applies explicit limits and
sanitization to reduce memory and output attack surface.

.. toctree::
   :maxdepth: 2

   quick_start
   host_integration
   api_stability
   flat_host_mapping
   compatibility_dump
   xmp_sync_policy
   writer_target_contract
   interpretation_status
   fuzzy_search
   creation
   editing
   raw_read_parity_plan
   build
   shared_library
   development
   interop_api
   exr_metadata_contract
   testing
   security
   api
