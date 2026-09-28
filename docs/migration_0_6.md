# Migrating to OpenMeta 0.6.0

OpenMeta 0.6.0 adds explicit native TIFF IFD0 deletion instructions to
`PreparedTransferBundle`. Its C++ layout changes, so applications and plugins
must rebuild against the new headers and library. C++ ABI 4 replaces ABI 3;
the Windows runtime is `openmeta-4.dll`, and ELF/macOS use ABI major 4.

The installed CMake package uses `SameMinorVersion`. Request
`find_package(OpenMeta 0.6 CONFIG REQUIRED)` after migration. A 0.5 request
does not accept the 0.6 package. Keep the older SDK for consumers that have
not rebuilt. Do not mix 0.5 headers or objects with the 0.6 library.

## Explicit TIFF deletion

The owned `tiff_ifd0_removals` list names supported tags to remove from an
existing TIFF/BigTIFF IFD0. This first contract covers `ImageDescription`
(`0x010E`), `Artist` (`0x013B`) and `Copyright` (`0x8298`). Other IFDs, page
directories, structural/storage tags and MakerNotes are outside this contract.

Preparation collects exact native `ifd0` entries marked both `Dirty` and
`Deleted` when EXIF transfer is enabled. A live entry of the same key takes
precedence over historical tombstones. Duplicate tombstones yield one sorted
instruction. Clean tombstones and absent source properties do not request
deletion. Setting `include_exif_app1 = false` disables collection. Snapshot v1
already stores the
required flags, so its encoding does not change.

For manually assembled bundles, use a sorted, unique list of supported tags.
Invalid lists and a simultaneous serialized replacement of a removed tag fail
before output. Use the existing TIFF edit plan/apply, stream-write or edit
package paths. Fresh tag emitters, the DNG SDK adapter and typed codec
handoffs cannot express destination deletion and reject a deletion-bearing bundle before callbacks.
Keep the input and prepared bundle unchanged between planning and applying.
`TiffEditPlan` is a summary, not a fingerprint of the exact edits; replan after
changing the bundle.

Translation's `ReplaceExisting` policy operates on the source store. To remove
an existing destination native tag, the source must retain the explicit native
tombstone through preparation. Omitting the source field alone still preserves
the destination field. XMP remains governed by its own preparation and carrier
replacement policies; a manually requested native deletion is not an implicit
XMP synchronization request.

The Python transfer probe reports `tiff_ifd0_removals` in its diagnostic result.
It remains a path-based probe; this release does not add an in-memory Python
prepared-bundle authoring API.

Preparation and edit planning may allocate. Existing host-owned synchronization
rules remain unchanged. No atomics or mutexes are added, and this change does
not claim general TIFF deletion or complete-file profile conformance.
