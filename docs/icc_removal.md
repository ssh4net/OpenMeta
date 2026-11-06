# Explicit primary ICC carrier removal

Version 0.8.28 adds `remove_existing_icc` to the C++
`PrepareTransferRequest` and `PreparedTransferBundle`. It defaults to false.
Omission, `include_icc_app2=false`, and deleting the last authored ICC tag do
not request carrier removal. Existing target ICC remains unless a replacement
or this explicit removal intent applies.

## Request and scope

```cpp
openmeta::PrepareTransferRequest request;
request.target_format = openmeta::TransferTargetFormat::Jpeg;
request.remove_existing_icc = true;
// Prepare against the chosen source, then plan/apply an existing-target edit.
```

Removal suppresses source ICC output even when `include_icc_app2` is true.
Source entries and captured snapshots remain unchanged. Other source metadata
keeps its selected transfer policy. Check the preparation result before using
its bundle; legacy preparation can return a diagnostic bundle on failure.

Supported edits are the leading JPEG `ICC_PROFILE` APP2 chain, primary
TIFF/BigTIFF/DNG IFD0 tag 34675, and (since 0.8.32) active top-level PNG `iCCP`
and WebP `ICCP` chunks. Duplicate ICC chunks are all removed. WebP also clears
the VP8X ICC bit while preserving unrelated flags. PNG removal does not require
ICC decompression or zlib. The operation preserves image bytes and unrelated
metadata. It does not remove nested copies, JPEG data after SOS, or DNG
AsShot/Current profile tags. TIFF editing changes active references; historical,
unreferenced bytes can remain in the file.

PNG preserves bytes after its first IEND as opaque trailing data. Removal
checks only active primary carriers; it does not erase historical or nested
profile bytes. RIFF sizes and WebP chunk padding remain valid.

An absent primary carrier can be a no-op. TIFF callers that allow this use
`PlanTiffEditOptions::require_updates=false`. Other requested edits and the
existing C2PA invalidation policy can still change output.

## Validation and consumers

Preparation rejects unsupported target families. A manually constructed bundle
that combines removal with an ICC payload or primary ICC route is invalid.
Fresh emission, adapter views, payload-only packages, codec and stable handoffs
reject the edit-only intent. For a new output that simply omits ICC, leave
removal false and disable ICC inclusion.

Use the existing edit-plan and exact package APIs to apply removal. Exact-output
package batches can be serialized, parsed and replayed without adding a removal
field to their wire format. The package writer and materializer check removal
against the virtual planned output after validating all chunk ranges and before
publication or writer callbacks. This also rejects a whole-source-copy plan
that would retain the ICC carrier. The check reads chunks without materializing
the whole output or adding a replay allocation. Its JPEG leading-header, TIFF IFD0 and PNG/WebP chunk scans
have a 4,096-item ceiling; excess reports `LimitExceeded`.

JPEG ICC removal uses the existing C2PA invalidation and exact binding paths.
Cryptographic signing and trust remain host responsibilities. No signing or
pixel/color-rendering guarantee is inferred from removing a profile.

## Compatibility

ABI 4 remains unfrozen. Version 0.8.32 adds target coverage without changing
public layouts or package wire versions. Rebuild with matching headers and
binaries; the request/bundle fields were introduced in 0.8.28. Source snapshot wire 1 and existing package,
patch and ICC serialization contracts are unchanged. The new request flag is
C++ only; existing Python operations retain their defaults.

See [ICC authoring](icc_authoring.md), [canonical serialization](canonical_serialization.md)
and [XMP synchronization policy](xmp_sync_policy.md).
