# Descriptive metadata reconciliation

Version 0.8.27 adds experimental C++ contract 1 in
`openmeta/metadata_reconcile.h`. It resolves a bounded set of shared descriptive
fields before creation, editing, serialization and transfer. Each input store
must represent one logical document; the host partitions unrelated images and
BMFF item scopes before calling it.

## Selected fields and families

| Canonical XMP | IPTC-IIM | Optional IFD0 / TIFF-XMP alias |
| --- | --- | --- |
| `dc:title[@xml:lang=x-default]` | 2:5 ObjectName | None |
| `dc:description[@xml:lang=x-default]` | 2:120 CaptionAbstract | 0x010e / `tiff:ImageDescription` |
| `dc:creator[n]` | Repeated 2:80 Byline | 0x013b / `tiff:Artist`, one creator only |
| `dc:subject[n]` | Repeated 2:25 Keywords | None |
| `dc:rights[@xml:lang=x-default]` | 2:116 CopyrightNotice | 0x8298 / `tiff:Copyright`, scalar only |

The namespace URIs are `http://purl.org/dc/elements/1.1/` and
`http://ns.adobe.com/tiff/1.0/`. XMP always participates. Native IPTC participates
by default; set `participate_iptc=false` to exclude it. The EXIF bridge requires
`enable_exif_bridge=true`. Each field has an independent selection flag.
The bridge rejects multiple creators and two-part legacy native copyright;
it does not join or split those values. EXIF type 2 ASCII and type 129 UTF-8
follow the existing profile translator. Other EXIF fields are outside this API.

## Authority and deletion

`FailOnConflict` is the default. Different participating values fail before
publication. `PreferSourceOrder` uses the first present family in the unique
`family_priority` permutation, defaulting to XMP, IPTC, EXIF. Within XMP,
canonical DC wins over a conflicting TIFF alias under that explicit preference;
an alias supplies a missing DC value. The managed alias is synchronized too.
Creator order matters. Keywords compare as multisets, retaining repeated text.
Duplicate active singleton keys or duplicate XMP array indices are ambiguous,
even when their text matches.

Absence does not request deletion. The default `Ignore` policy ignores
tombstones. `PropagateDirty` accepts dirty deletion intent only when the group
has no active replacement. Active items supersede item tombstones. Clean
tombstones are ignored before their stale payload is inspected. Non-default
language siblings and unrelated/custom XMP remain in the store.

Original XMP entry IDs and sparse array indices survive positional updates.
Excess original items become tombstones; appended items use indices above the
retained managed maximum. No compaction or arbitrary RDF merge is inferred.

## Transaction and limits

The input must be finalized. Output publication occurs after all selected
families, projections and final store validation succeed. Source/output aliasing
is supported. Failure leaves output unchanged and mutation counts zero; the
result identifies the failed stage, field, family and entry where available.
Hosts synchronize shared objects. The API introduces no mutex or atomic state.
Preparation may allocate and use multiple cold staging stores. It reuses the
existing translators in fixed batches; this is one externally visible
transaction, not an allocation-free or single-clone path.

Hard maximum defaults bound the complete source before cloning: 200,000 entries
and blocks, 64 MiB arena, and 4,096 bytes per inspected key. Selected unique
source properties are capped at 1,024, selected text at 8 MiB, aggregate
participating bytes at 16 MiB, and edit operations at 4,096. Callers can lower
these limits and the embedded validation limits. Text, participating-byte and
operation budgets include projected values and bounded scratch work; the source
property count refers to original selected entries. Reinspection does not charge
the same original source entry twice. Native IPTC field byte limits still apply
(64 title, 2,000 caption, 32 per byline, 64 per keyword, 128 rights).
Enabled EXIF projections also obey the existing profile translator text limits.

Native non-ASCII IPTC needs exactly one ESC `%` `G` charset declaration in the
same valid origin block. An invalid-block sentinel is never a shared charset
scope. Output is checked again after projection. If existing block layout
cannot safely represent the new encoding, the transaction fails with
`SourceEncodingConflict`; it does not invent provenance or retag unrelated data.

```cpp
openmeta::MetadataReconcileOptions options;
options.conflict_policy =
    openmeta::MetadataReconcileConflictPolicy::PreferSourceOrder;
options.enable_exif_bridge = true;
const auto result =
    openmeta::reconcile_descriptive_metadata(store, options, &store);
if (result.status != openmeta::MetadataReconcileStatus::Ok) {
    // Existing store is unchanged. Inspect result.failed_stage and failed_field.
}
```

## Read, prepare and write

For custom containers, set `preserve_custom_xmp_structures=true` on the C++
bytes/file snapshot read options, random-access snapshot read options, file
preparation options, or prepared-snapshot execution options that read destination
XMP. The default is false. Retention mode rejects unsupported, malformed or
truncated XMP packets rather than returning a successful partial tree, including
when another packet decoded successfully. Size failures report `LimitExceeded`. Optional destination merges retain their
existing per-carrier failure status and warning; a failed carrier contributes no
partial tree, while preparation can continue from the other selected metadata.
The existing low-level XMP preservation flag remains available.

Reconcile the chosen source before preparation. Destination-sidecar and embedded
XMP merge controls are separate writer policies; loading a destination sidecar
does not import a source sidecar. A later destination winner can change XMP
without changing native EXIF/IPTC. Use source precedence when the emitted native
and XMP fields must retain the reconciled source authority, or select/merge the
complete logical source before reconciliation. XMP-only precedence workflows
should disable native emission and generated native projection as appropriate.
Non-overlapping custom trees are retained; overlapping declared roots can fail
structural validation instead of being merged heuristically.

Carrier regression fixtures cover JPEG, BigTIFF, PNG, WebP, JP2/JPH and bounded
HEIF/AVIF/CR3 rewriting with exact media/unmanaged-byte checks. Their synthetic
payloads establish metadata preservation, not pixel validity or every BMFF
graph shape. Native IPTC readback applies to JPEG/TIFF; other tested carriers
carry the reconciled XMP and EXIF.

This API does not implement full MWG synchronization, arbitrary-schema
reconciliation, general RDF, source-sidecar discovery or ICC carrier removal.
Python does not expose this new C++ contract yet. ABI 4 remains unfrozen; rebuild
with matching 0.8.27 headers and binaries. Existing translation contracts,
prepared patch contract 2 and snapshot wire 1 are unchanged.
