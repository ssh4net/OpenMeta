# Metadata Backend Matrix (Draft)

Date: March 5, 2026

## Goal

Define one OpenMeta write/transfer contract that can target multiple container
backends without per-backend metadata logic duplication.

For the public per-target preserve/replace guarantees, see
[writer_target_contract.md](writer_target_contract.md).

## Capability Matrix

| Backend | Native metadata write primitives | Best use in OpenMeta |
| --- | --- | --- |
| libjpeg-turbo | `jpeg_write_marker`, `jpeg_write_m_header`, `jpeg_write_m_byte`, `jpeg_write_icc_profile` | Primary JPEG metadata emitter (APP1/APP2/APP13 direct control) |
| libtiff | `TIFFSetField`, `TIFFCreateEXIFDirectory`, `TIFFCreateGPSDirectory`, `TIFFWriteCustomDirectory`, `TIFFMergeFieldInfo` | Primary TIFF metadata emitter (native tag and IFD path) |
| libjxl | `JxlEncoderUseBoxes`, `JxlEncoderAddBox`, `JxlEncoderCloseBoxes`, `JxlEncoderSetICCProfile` | Primary JXL metadata emitter (`Exif`, `xml `, `jumb`, optional `brob`) plus encoder ICC profile |
| OpenEXR | `Header::insert`, typed attributes, `OpaqueAttribute` for unknown attr types | EXR header attribute path (not EXIF block packaging) |

## Container-Level Notes

### JPEG (libjpeg-turbo)

- Write EXIF as APP1 (`Exif\\0\\0` + TIFF payload).
- Write XMP as APP1 (`http://ns.adobe.com/xap/1.0/\\0` + packet).
- Write IPTC/IRB as APP13 (`Photoshop 3.0` resource block layout).
- Write ICC as APP2 chunk chain (`ICC_PROFILE` header + sequence index/count).
- OpenMeta should own marker ordering policy and payload splitting limits.

### TIFF (libtiff)

- EXIF/GPS should be written as directories and linked from root IFD pointers.
- XMP uses `TIFFTAG_XMLPACKET` (700).
- IPTC uses `TIFFTAG_RICHTIFFIPTC` / `TIFFTAG_EP_IPTC_NAA` (33723 alias).
- Photoshop IRB uses `TIFFTAG_PHOTOSHOP` (34377).
- ICC uses `TIFFTAG_ICCPROFILE` (34675).
- OpenMeta should own EXIF serializer policy before `TIFFSetField`.
- Public OpenMeta fast path is backend-emitter or rewrite/edit based:
  `emit_prepared_transfer_compiled(..., TiffTransferEmitter&)` or
  `write_prepared_bundle_tiff_edit(...)`.
- TIFF intentionally does not expose a metadata-only byte-writer emit API.

### JXL (libjxl)

- Metadata requires container boxes enabled.
- Add `Exif`, `xml `, `jumb` boxes through `JxlEncoderAddBox`.
- Set ICC through `JxlEncoderSetICCProfile`; ICC is not a JXL metadata box.
- `Exif` box content must include the 4-byte TIFF header offset prefix.
- Optional Brotli-compressed box storage (`brob`) is backend-controlled.
- OpenMeta should prepare box payloads once and reuse in emit path.
- Current OpenMeta transfer support on this path is intentionally bounded:
  EXIF/XMP are prepared and emitted through `JxlTransferEmitter`, ICC is
  prepared as `jxl:icc-profile` and emitted through the encoder ICC path,
  file-based prepare can preserve source generic JUMBF payloads and raw
  OpenMeta draft C2PA invalidation payloads as JXL boxes, can generate a
  draft unsigned invalidation payload for content-bound source C2PA, and
  store-only prepare can project decoded non-C2PA `JumbfCborKey` roots into
  generic JXL `jumb` boxes. IPTC requested for JXL is projected into the
  `xml ` XMP box; there is no raw IPTC-IIM JXL route.
  `build_prepared_jxl_encoder_handoff_view(...)` is the explicit OpenMeta
  encoder-side ICC contract for JXL, and
  `build_prepared_jxl_encoder_handoff(...)` /
  `serialize_prepared_jxl_encoder_handoff(...)` add the owned persisted
  form of that handoff: one optional `jxl:icc-profile` payload plus the
  remaining JXL box counts. The JXL compile/emit path rejects multiple
  prepared ICC profiles so that handoff contract matches backend execution.
  `inspect_prepared_transfer_artifact(...)` is the shared persisted-artifact
  inspect path across payload batches, package batches, persisted C2PA
  handoff/signed packages, and persisted JXL encoder handoffs.
  `build_prepared_transfer_emit_package(...)` plus
  `write_prepared_transfer_package(...)` can also serialize direct JXL box
  bytes from prepared bundles, and `execute_prepared_transfer(...)` can use
  that same box-only serializer through `emit_output_writer`. OpenMeta also
  supports a bounded file-level JXL edit path that preserves the signature and
  non-managed top-level boxes, replaces only the metadata families present in
  the prepared bundle, and appends the prepared JXL boxes to an existing
  container file. Unrelated source JXL metadata boxes are preserved, and
  uncompressed source `jumb` boxes are distinguished as generic JUMBF vs C2PA
  for that replacement decision. When Brotli support is available, the same
  family check also covers compressed `brob(realtype=jumb)` source boxes. The
  byte-writer/file-edit path still does not serialize `jxl:icc-profile`; ICC
  remains encoder-only. The bounded external-signer path now also supports
  JXL `jumb` staging for content-bound C2PA rewrite on top of that edit path;
  full in-process re-sign remains out of scope.

### WebP (RIFF metadata chunks)

- Metadata is carried as RIFF chunks, not TIFF tags or BMFF boxes.
- Standard chunk carriers:
  - `EXIF`
  - `XMP `
  - `ICCP`
  - bounded `C2PA`
- The prepared `EXIF` chunk payload is the TIFF byte stream only. It does not
  include the JPEG APP1 `Exif\0\0` preamble.
- OpenMeta now has a bounded WebP transfer path on the core transfer API:
  `prepare_metadata_for_target(..., TransferTargetFormat::Webp, ...)`,
  `compile_prepared_bundle_webp(...)`,
  `emit_prepared_bundle_webp(...)`,
  `emit_prepared_bundle_webp_compiled(...)`, and
  `emit_prepared_transfer_compiled(..., WebpTransferEmitter&)`.
- IPTC requested for WebP is projected into the existing `XMP ` chunk;
  OpenMeta does not create a raw IPTC-IIM WebP carrier.
- `build_prepared_transfer_emit_package(...)` plus
  `write_prepared_transfer_package(...)` can serialize direct WebP chunk bytes
  from prepared bundles, and the owned package batch/replay path can persist
  or hand off those bytes without keeping the source bundle alive.
- The file edit path rewrites managed metadata chunks and patches existing
  `VP8X` feature bits. It does not synthesize a missing `VP8X` chunk.
- Full WebP signed C2PA rewrite/re-sign is still outside the current WebP
  transfer contract.

### ISO-BMFF metadata items (HEIF / AVIF / CR3)

- This bounded transfer path is metadata-item/property oriented. OpenMeta also
  supports a bounded BMFF metadata edit path over an existing BMFF target file.
- Current prepared item routes:
  - `bmff:item-exif`
  - `bmff:item-xmp`
  - `bmff:item-jumb`
  - `bmff:item-c2pa`
- Current prepared property routes:
  - `bmff:property-colr-icc`
- EXIF item payloads use the BMFF Exif item shape:
  - 4-byte big-endian TIFF offset prefix
  - followed by full `Exif\0\0` bytes
- IPTC requested for BMFF is projected into `bmff:item-xmp`; OpenMeta does
  not create a raw IPTC-IIM BMFF carrier.
- ICC requested for BMFF uses the bounded property path:
  - `bmff:property-colr-icc`
  - payload bytes are `u32be('prof') + <icc-profile>`
  - this is a `colr` property payload, not a metadata item
- File-based prepare can preserve source generic JUMBF payloads and raw
  OpenMeta draft C2PA invalidation payloads as BMFF metadata items.
- Store-only prepare can project decoded non-C2PA `JumbfCborKey` roots into
  `bmff:item-jumb` when no raw source payload is available.
- Core emitter surface:
  - `compile_prepared_bundle_bmff(...)`
  - `emit_prepared_bundle_bmff(...)`
  - `emit_prepared_bundle_bmff_compiled(...)`
  - `emit_prepared_transfer_compiled(..., BmffTransferEmitter&)`
- `build_prepared_transfer_emit_package(...)` plus
  `write_prepared_transfer_package(...)` can serialize direct BMFF item and
  property payload bytes from prepared bundles. This is a host/adapter payload
  handoff, not a standalone BMFF file rewrite.
- `execute_prepared_transfer(...)` exposes that same bounded payload sequence
  through `ExecutePreparedTransferOptions::emit_output_writer`, with capacity
  preflight and BMFF item/property summaries. The output remains a host handoff,
  not a standalone BMFF container.
- The shared package-batch persistence/replay layer can own and hand off
  stable BMFF item and property payload bytes.
- `metatransfer` / `openmeta.transfer_probe(...)` expose BMFF summaries,
  including `bmff_item mime/xmp ...` and `bmff_property colr/prof ...`.
- `metatransfer --target-heif|--target-avif|--target-cr3 --source-meta ... -o ...`
  performs bounded metadata edits for targets with no foreign top-level `meta`
  box, with a prior OpenMeta-authored metadata-only `meta` box from the same
  bounded contract, or with a parseable foreign top-level `meta` item graph.
  The foreign-`meta` path merges, replaces, or strips bounded
  Exif/XMP/JUMBF/C2PA metadata items by extending `iinf`, `iloc`, `idat`, and
  `iref` with `cdsc` references to the primary item. It requires a single
  parseable `iinf`, `iloc` version 0/1/2, `pitm`, and at most one `idat`.
  Inserted item IDs can use the 32-bit item-id space: supported `iloc` version
  0/1 graphs are upgraded to output `iloc` version 2 when needed, and OpenMeta
  emits wider `infe`/`iref` records for inserted items that exceed 16 bits.
  Newly inserted metadata item records keep `iloc` construction method 0 and
  use absolute file-offset extents for broad reader
  compatibility. When retained self-contained records can be represented that
  way, the rebuilt `iloc` also compacts the base-offset field width to zero.
  Retained foreign item locations are supported for construction method 0
  file-offset extents and construction method 1 extents into an existing
  `idat`, with data reference index 0. Construction method 2 is supported only
  when the retained item has parseable `iref` `iloc` references, using explicit
  extent indexes or reference order, and every referenced item is also retained
  with a supported local location. External data references, missing method-2
  references, removed referenced items, and other construction methods fail
  safely.
- Foreign-`meta` ICC property merge is bounded to
  `bmff:property-colr-icc`. OpenMeta removes prior ICC `colr/prof` and
  `colr/rICC` properties from `iprp/ipco`, compacts/remaps existing `ipma`
  associations, appends the transferred `colr/prof` property, and associates
  it with the primary item and any retained item that previously referenced a
  replaced ICC property while preserving the prior essential association bit.
  Broader non-ICC property replacement and arbitrary scene/property-graph
  rewrites remain out of scope for the current contract.
- Embedded-XMP strip mode removes XMP from OpenMeta-authored metadata `meta`
  boxes and from parseable foreign top-level `meta` item graphs that satisfy
  the same bounded merge contract. Foreign graphs without a primary item
  relationship (`pitm`) or outside the supported `iinf`/`iloc`/`idat`/`iref`
  shape still fail explicitly instead of silently claiming removal.
- The same bounded BMFF edit contract now also participates in the core /
  file-helper C2PA signer path:
  - sign-request derivation
  - binding-byte materialization
  - signed-payload validation
  - staged bmff:item-c2pa apply before bounded metadata-only edit
- Out of scope for the current BMFF contract:
  - thin CLI/Python signer-input exposure for BMFF
  - arbitrary rewrite of foreign top-level BMFF scene/property graphs
  - full BMFF signed rewrite/re-sign beyond the bounded metadata-only edit path

### EXR (OpenEXR)

- EXR metadata is typed header attributes, not EXIF/TIFF IFD blocks.
- Use typed attributes for semantic fields.
- Unknown attribute types can be preserved as opaque attributes.
- OpenMeta EXR path should remain an attribute adapter, not block repackaging.
- Current public EXR bridge:
  - `build_exr_attribute_batch(...)` exports one owned per-part attribute list
    from `MetaStore`
  - `build_exr_attribute_part_spans(...)` groups that batch into contiguous
    per-part spans
  - `build_exr_attribute_part_views(...)` exposes zero-copy grouped per-part
    views over the same batch
  - `replay_exr_attribute_batch(...)` replays the grouped batch through
    explicit host callbacks
  - known scalar/vector EXR types are re-encoded into deterministic EXR
    attribute bytes
  - unknown/custom attrs are preserved as opaque raw bytes when
    `Origin::wire_type_name` is available
  - ambiguous attrs without a stable wire-type contract fail closed or can be
    skipped explicitly

## Unified Workaround (Single Contract)

Use one two-phase pipeline for all backends:

1. `prepare_metadata_for_target(source_store, target_format, profile)`
   - Build target-ready block payloads once.
   - Build deterministic write order.
   - Build optional fixed-size patch map for per-frame time fields.
2. `emit_prepared_bundle(bundle, writer_target, frame_patch)`
   - Emit prebuilt payloads through backend-specific write calls.
   - Apply optional fixed-width time patch before final write.

This keeps heavy work out of hot per-frame loops.

## Backend Interface Shape (Draft)

Define narrow backend interfaces:

- `JpegWriterBackend::write_app_block(app_marker, bytes)`
- `TiffWriterBackend::set_tag(tag_id, bytes_or_scalar)` plus
  `commit_exif_directory(offset_ref)`
- `JxlWriterBackend::add_box(type4cc, bytes, compress)`
- `ExrWriterBackend::set_attribute(name, typed_or_opaque_value)`

OpenMeta core emits backend-neutral prepared payloads. Backends only perform
container call mapping.

## Policy Defaults (Draft)

- No-edits transfer mode: preserve payloads when legal for target container.
- EXIF pointer tags are regenerated for target container layout.
- MakerNote and C2PA/JUMBF transfer policy is explicit and deterministic.
- Current JPEG/TIFF prepare behavior:
  - MakerNote: `Keep` default, `Drop` supported, `Invalidate` currently
    resolves to `Drop`, and unavailable `Rewrite` resolves to `Drop` rather
    than silently preserving raw bytes. `Keep` carries the original opaque
    payload, but does not relocate vendor-private offsets, repair checksums, or
    prove semantic readability after the surrounding EXIF layout changes. Use
    `makernote_transfer_audit_from_store(...)` for the generic machine-readable
    trust boundary and `makernote_layout_transfer_audit_from_store(...)` for
    bounded Nikon type 1/type 3 offset-layout evidence plus conservative Canon
    source-dependent IFD recognition. Canon notes retain an explicitly
    ambiguous source offset basis. Nikon type 3 structural validation covers
    standard embedded-TIFF offsets, not vendor-private binary offsets or
    checksums.
  - JUMBF: file-based JPEG prepare can preserve source payloads by repacking
    them into APP11 segments; store-only JPEG prepare can project decoded
    non-C2PA `JumbfCborKey` roots into generic APP11 JUMBF payloads; explicit
    raw JUMBF -> JPEG APP11 append is available through
    `append_prepared_bundle_jpeg_jumbf(...)`; and JPEG rewrite/edit removes
    existing APP11 JUMBF when the resolved policy is `Drop`. Ambiguous numeric
    map keys and decoded-CBOR bool/simple/sentinel/large-negative fallback
    forms in projected JUMBF are rejected; tagged CBOR values are preserved.
  - C2PA: `Invalidate` on JPEG now emits a draft unsigned APP11 C2PA
    invalidation payload.
    - `PreparedTransferPolicyDecision` exposes `mode`, `source_kind`, and
      `prepared_output` so adapters can branch without parsing free-form
      messages.
    - `PreparedTransferBundle::c2pa_rewrite` exposes the separate
      rewrite-prerequisites contract for future signing flows:
      current rewrite state, source kind, existing carrier segment count,
      and whether manifest builder, content binding, certificate chain,
      private key, and signing time are still required.
    - For JPEG it exposes `content_binding_chunks` as preserved source ranges
      plus prepared JPEG segments; for the bounded BMFF edit path it uses
      preserved source ranges plus one prepared metadata-only `meta` box.
    - `build_prepared_c2pa_sign_request(...)` derives the external signer view
      from that same data without changing the bundle contract.
    - `build_prepared_c2pa_sign_request_binding(...)` materializes the exact
      content-binding bytes from that request and the target container input.
      Current bounded targets are JPEG and BMFF.
    - `build_prepared_c2pa_handoff_package(...)` combines the signer request
      and exact binding bytes into one public handoff object.
    - `serialize_prepared_c2pa_handoff_package(...)` and
      `deserialize_prepared_c2pa_handoff_package(...)` persist that object.
    - `build_prepared_c2pa_signed_package(...)` combines the sign request and
      signer-returned material into one persisted signed package.
    - `serialize_prepared_c2pa_signed_package(...)` and
      `deserialize_prepared_c2pa_signed_package(...)` persist that package.
    - Thin wrappers expose that as CLI dump output and Python unsafe raw
      bytes for JPEG without duplicating the reconstruction logic. BMFF uses
      the same core helper without separate wrapper flags yet.
    - `validate_prepared_c2pa_sign_result(...)` validates a returned signed
      logical C2PA payload before bundle mutation and reports staged carrier
      size and segment count.
    - Current JPEG validation also checks semantic manifest/claim/signature
      consistency, resolved explicit references, request-aware manifest count
      / `claim_generator` requirements, decoded-assertion presence when
      content binding is required, the primary signature linking back to the
      prepared primary claim under that same content-binding contract, no
      primary-signature explicit-reference ambiguity under that same request,
      no multi-signature drift where the primary claim is referenced by more
      than one signature under the current sign request, and no extra linked
      signatures beyond the prepared sign request,
      manifest/claim/signature projection shape under the prepared manifest
      contract, exact primary manifest-CBOR equality against
      `manifest_builder_output`, staged APP11 sequence order, and exact
      logical-payload reconstruction. Final JPEG emit/write also rejects
      prepared APP11 C2PA carriers with non-contiguous sequence numbers,
      inconsistent repeated headers, invalid logical root type, or BMFF
      declared-size drift before backend bytes are written.
    - Final JPEG emit/write also rejects prepared APP11 C2PA carriers that
      violate the resolved bundle contract:
      missing required carriers, draft-invalidated carriers under a signed
      rewrite contract, signed-rewrite carriers under a draft contract, and
      `Ready` rewrite state without `SignedRewrite` prepared output.
    - `apply_prepared_c2pa_sign_result(...)` accepts the external signer
      output and stages a content-bound logical C2PA payload back into
      prepared JPEG APP11 blocks, JXL `jumb` boxes, or bounded BMFF
      `bmff:item-c2pa` items after strict request validation.
    - The file-level execution helper can now validate that stage step, apply
      it, and continue into normal JPEG emit/edit, bounded JXL edit, or
      bounded BMFF edit flow. Thin CLI/Python signer-input wrappers now cover
      JPEG, JXL, and bounded BMFF targets. The option name
      `--jpeg-c2pa-signed` remains for compatibility.
    - `PreparedTransferPackagePlan` now provides the shared final-output
      package contract for current JPEG/TIFF rewrite paths plus direct
      JPEG/JXL emit packaging:
      deterministic source ranges, prepared direct blocks, prepared JPEG
      segments, and inline generated bytes can be written through one generic
      package writer.
    - `PreparedTransferPackageBatch` is the owned replay form of that same
      contract: each package chunk is materialized into stable bytes so the
      final metadata package can be cached or handed off without the original
      input stream or prepared bundle storage.
    - `serialize_prepared_transfer_package_batch(...)` and
      `deserialize_prepared_transfer_package_batch(...)` persist that owned
      batch so host layers can move it across process or integration boundaries
      without reopening the source file or rebuilding the bundle.
    - `collect_prepared_transfer_payload_views(...)` and
      `build_prepared_transfer_payload_batch(...)` now provide the matching
      target-neutral semantic payload surface directly over prepared bundles.
    - `serialize_prepared_transfer_payload_batch(...)` and
      `deserialize_prepared_transfer_payload_batch(...)` persist that earlier
      semantic payload batch for cross-process or cross-layer handoff before
      final package materialization.
    - Host metadata tables that use specification naming should export with
      `ExportNameStyle::FlatHost` plus `ExportNamePolicy::Spec`. This preserves
      names such as `Exif:ISOSpeedRatings` and `Exif:ExposureBiasValue` instead
      of applying the default ExifTool-compatible aliases.
    - `serialize_transfer_source_snapshot(...)` now persists the target-neutral
      decoded source state for later editing and preparation when the output
      format is not yet known. This is distinct from target-specific payload and
      package batches.
    - `import_flat_host_metadata(...)` provides bounded typed writeback by exact
      source identity or unique exported name, with explicit keys required for
      new custom metadata. Remove-by-identity and remove-by-unique-name preserve
      stable tombstones; duplicate flat names are never collapsed implicitly.
    - `create_metadata_store(...)` builds a fresh finalized store from exact
      borrowed EXIF/TIFF/DNG-style, XMP, and IPTC-IIM entries when no source
      snapshot exists. `validate_store(...)` supplies structured schema and
      image-context diagnostics before encoder handoff.
    - `serialize_exif_tiff(...)` exposes the deterministic unwrapped TIFF/EXIF
      payload directly. Hosts can measure exact storage, retain immutable bytes,
      and apply their own container framing without selecting a fake transfer
      target. Existing transfer preparation wraps this same canonical payload.
    - `collect_prepared_transfer_package_views(...)` is the target-neutral
      semantic package surface above that persisted batch.
    - `replay_prepared_transfer_package_batch(...)` is the target-neutral
      callback replay surface above that same persisted batch.
    - OpenMeta no longer ships an in-library host-specific payload/package
      bridge above these target-neutral package views and replay APIs.
    - `build_prepared_transfer_adapter_view(...)` now provides the parallel
      target-neutral adapter view for JPEG/TIFF/JXL/WebP/PNG/JP2/EXR/BMFF host
      integrations that want explicit compiled operations without route
      parsing. The v1 operation schema has a dedicated contract version and
      canonical validation; EXR name/type/value uses a typed accessor.
    - `emit_prepared_transfer_adapter_view(...)` replays that compiled view
      through one generic host sink.
    - `replay_prepared_transfer_payload_batch(...)` now reuses that same
      earlier persisted semantic payload stage directly, before final package
      materialization.
    - File-based JPEG prepare can preserve an existing OpenMeta draft
      invalidation payload as raw APP11 C2PA (`TransferC2paMode::PreserveRaw`).
    - Content-bound `Keep` still resolves to `Drop`.
    - `Rewrite` resolves to `Drop` with explicit
      `SignedRewriteUnavailable` until re-sign support exists, but externally
      signed payload staging is now available once a signer has consumed the
      request.
    - JPEG content-changing rewrite/edit removes existing APP11 C2PA from the
      target before inserting the new prepared payload.
- Safety limits are enforced before backend calls (size, truncation, malformed).

## Recommended Integration Order

1. JPEG and TIFF direct backends (highest transfer value).
2. JXL box emitter parity for EXIF/XMP plus bounded JUMBF/C2PA preserve or
   projection.
3. If a future host-specific bridge needs persistence or replay formats,
   build that on top of the target-neutral package and adapter surfaces
   rather than adding a host-specific wrapper inside OpenMeta.
4. Extend the current EXR attribute batch bridge if host-side replay or
   persistence formats are needed.

This order aligns with fast transfer requirements and minimal overhead in
high-FPS pipelines.
