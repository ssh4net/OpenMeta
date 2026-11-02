Descriptive metadata reconciliation
===================================

Version 0.8.27 adds experimental C++ contract 1 in
``openmeta/metadata_reconcile.h``. Resolve these shared fields before preparing
metadata for writing. Each input store represents one logical document; the
host partitions unrelated images and BMFF item scopes.

Selected fields and families
----------------------------

.. list-table::
   :header-rows: 1
   :widths: 38 25 37

   * - Canonical XMP
     - Native IPTC-IIM
     - Optional IFD0 / TIFF-XMP alias
   * - ``dc:title[@xml:lang=x-default]``
     - 2:5 ObjectName
     - None
   * - ``dc:description[@xml:lang=x-default]``
     - 2:120 CaptionAbstract
     - 0x010e / ``tiff:ImageDescription``
   * - ``dc:creator[n]``
     - Repeated 2:80 Byline
     - 0x013b / ``tiff:Artist``, one creator
   * - ``dc:subject[n]``
     - Repeated 2:25 Keywords
     - None
   * - ``dc:rights[@xml:lang=x-default]``
     - 2:116 CopyrightNotice
     - 0x8298 / ``tiff:Copyright``, scalar

The namespace URIs are ``http://purl.org/dc/elements/1.1/`` and
``http://ns.adobe.com/tiff/1.0/``. XMP always participates; native IPTC participates
unless ``participate_iptc=false``. Set ``enable_exif_bridge=true`` for EXIF.
Each field has a separate selection flag. Multiple creators and two-part native
copyright fail instead of being joined or split. Native type 2 ASCII and type
129 UTF-8 use the existing profile translator. Other EXIF fields are excluded.

Authority and deletion
----------------------

``FailOnConflict`` is the default. Different values fail before publication.
``PreferSourceOrder`` uses the first present family in the unique
``family_priority`` permutation, default XMP, IPTC, EXIF. Within XMP, canonical
DC wins over a conflicting TIFF alias under that explicit preference; an alias
supplies missing DC. Managed aliases are synchronized too.

Creator order matters. Keywords compare as multisets and retain repeated text.
Duplicate active singleton keys or duplicate XMP array indices are ambiguous,
even if their text matches. Original XMP IDs and sparse indices survive
positional updates. Excess items become tombstones; appended indices exceed
the retained managed maximum. Non-default languages and unrelated/custom XMP
remain in the store.

Absence never requests deletion. ``Ignore`` skips tombstones.
``PropagateDirty`` accepts a dirty group deletion only when no active replacement
remains. Active items supersede item tombstones. Clean tombstones are ignored
before their stale payload is inspected.

Transaction and limits
----------------------

The source must be finalized. Full family preflight, projection and final
store validation precede output publication. Source/output aliasing works.
Failure leaves output unchanged and mutation counts zero. The result identifies
the failed stage, field, family and entry where available. Hosts synchronize
shared stores; this API introduces no mutex or atomic state.

Cold preparation may allocate and use several staging stores. Existing
translators run in fixed batches. This is one externally visible transaction,
without an allocation-free or single-clone guarantee.

Default hard bounds cover 200,000 source entries and blocks, 64 MiB source arena,
4,096 bytes per inspected key, 1,024 unique selected source properties, 8 MiB
selected text, 16 MiB aggregate participating bytes and 4,096 operations. Callers
can lower these and the embedded validation limits. Text, participating-byte and
operation caps include projection and bounded scratch work; the source-property
count refers to original selected entries. A source entry is charged
once despite reinspection. Native IPTC byte caps remain 64 for title, 2,000 for
caption, 32 per byline, 64 per keyword and 128 for rights. Enabled EXIF
projections also obey existing profile translator text limits.

Native non-ASCII IPTC requires exactly one ESC ``%`` ``G`` charset declaration
in the same valid origin block. Invalid-block sentinels never establish shared
scope. Projected output is checked again. An incompatible block layout fails
``SourceEncodingConflict`` without invented provenance or unrelated retagging.

.. code-block:: cpp

   openmeta::MetadataReconcileOptions options;
   options.conflict_policy =
       openmeta::MetadataReconcileConflictPolicy::PreferSourceOrder;
   options.enable_exif_bridge = true;
   const auto result =
       openmeta::reconcile_descriptive_metadata(store, options, &store);
   if (result.status != openmeta::MetadataReconcileStatus::Ok) {
       // store is unchanged; inspect failed_stage and failed_field.
   }

Read, prepare and write
-----------------------

Set ``preserve_custom_xmp_structures=true`` on C++ bytes/file or random-access
snapshot read options, file preparation options, or prepared-snapshot execution
options that read destination XMP. The default is false. Retention mode rejects
unsupported, malformed or truncated XMP packets, even after another packet
decodes successfully. Size failures report ``LimitExceeded``. Optional destination
merge failures retain per-carrier status and warnings and contribute no partial
tree; preparation can continue from other metadata. Low-level XMP
preservation options remain available.

Reconcile the chosen source before preparation. Destination-sidecar and embedded
XMP merge controls remain separate writer policies; they do not import a source
sidecar. A later destination winner can change XMP without changing native
EXIF/IPTC. Choose source precedence when emitted native and XMP fields must
retain source authority, or select/merge the complete logical source before
reconciliation. XMP-only workflows should disable native emission and generated
native projection as appropriate. Non-overlapping custom trees survive;
overlapping declarations can fail graph validation rather than merge by guess.

Carrier fixtures cover JPEG, BigTIFF, PNG, WebP, JP2/JPH and bounded HEIF/AVIF/CR3
rewriting. Exact media and unmanaged bytes are compared. Synthetic payloads do
not establish pixel validity or every BMFF graph shape. Native IPTC readback is
asserted for JPEG/TIFF; other tested carriers carry reconciled XMP and EXIF.

Full MWG, arbitrary-schema reconciliation, general RDF, source-sidecar discovery
and ICC carrier removal remain outside this contract. Python does not yet expose
this C++ API. ABI 4 stays unfrozen: use matching 0.8.27 headers and binaries.
Existing translation contracts, prepared patch contract 2 and snapshot wire 1
are unchanged. See :doc:`xmp_sync_policy` and :doc:`writer_target_contract`.
