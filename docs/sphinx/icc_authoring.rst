ICC creation, editing and canonical serialization
=================================================

Primary ICC removal and collection precedence (0.8.28)
------------------------------------------------------

Explicit C++ primary ICC removal reaches JPEG and TIFF/DNG edits and exact
packages. Omission and last-tag deletion keep their prior behavior. Request and
bundle layouts grow within unfrozen ABI 4: match headers and binaries. See
:doc:`icc_removal`. DC creator/subject merges select complete collections.


OpenMeta 0.8.25 adds ``serialize_icc_profile()`` in
``openmeta/icc_serialize.h`` (contract 1). Generic authoring and exact-key typed
editing contract 2 accept ICC header fields and opaque typed tag payloads.
The logical-field/Python creation and editing field sets are unchanged.

Shared lifecycle
----------------

Use ``make_icc_header_field_key_view(offset)`` or
``make_icc_tag_key_view(signature)`` in borrowed ``MetadataAuthoringEntry``
inputs. ``create_metadata_store()`` copies inputs into a finalized store;
``edit_metadata_typed()`` applies one ordered Add/Set/Remove transaction with
existing occurrence, provenance, tombstone and resource rules. Failures leave
output unchanged, including when base and output alias.

ICC entry validation is independent of whole-profile completeness. Generic
duplicate policy is retained; the canonical serializer rejects duplicate
header offsets and tag signatures. An entry transaction can leave an incomplete
profile, which serialization rejects.

Header and tag inputs
---------------------

Required explicit offsets are 8 (version), 12 (device class), 16 (data color
space), 20 (PCS), 24 (creation date) and 68 (illuminant). Other supported offsets
are 0, 4, 36, 40, 44, 48, 52, 56, 64, 80 and 84. Size and ``acsp`` are derived.
The reserved header tail is zero. Fields have exact, non-overlapping widths:

* Most fields: Scalar U32, or exactly four raw bytes.
* Attributes at 56: Scalar U64, or eight raw bytes.
* Date at 24: U16[6], or twelve big-endian raw bytes; calendar values must be valid.
* Illuminant at 68: SRational[3] exactly representable as signed15.16, or twelve
  raw bytes. Integer conversion checks divisibility and signed range.
* Profile ID at 84: sixteen raw bytes.

Numeric encoding is big-endian. Raw inputs are Bytes/U8/Unknown; numeric hints
are Unknown. Version major is 2 or 4, class/spaces are nonzero, and an explicit
signature must agree with ``acsp``. Wire hints do not alter canonical encoding.
Tags have a nonzero key/type signature, four zero reserved bytes and at least
eight total bytes. Remaining payload bytes are opaque. Hosts supply coherent
color data, class-specific required tags and private payload semantics.

Measure/write and limits
------------------------

Call with an empty output for exact ``needed``/``OutputTruncated``; this does not
allocate a full output profile. Supply that many bytes for complete output.
Short outputs receive a canonical prefix after full preflight; other errors
leave output unchanged with zero ``written``/``needed``. Larger tails stay intact.
The finalized store stays immutable, and output cannot overlap its arena.

Active ICC entries are selected; other families and deleted entries are ignored.
Malformed, truncated/unreadable, duplicate or incomplete profiles are rejected.
No tags returns ``NoIccData``. Tags sort by signature with zero four-byte padding,
including the final tag. Layout arithmetic checks offsets and complete size.
Default limits: 65,536 tags, 32 MiB per tag, 64 MiB output, fitting uint32_t and size_t.
Preparation allocates bounded scratch; allocation failure is not recoverable.

``tags_serialized`` counts selected tags; ``header_fields_serialized`` counts
accepted explicit inputs, including supplied derived fields or cleared IDs.
``failed_entry`` identifies an attributable bad/oversized entry.

ID, transfer and compatibility
------------------------------

Default ``IccProfileIdPolicy::Clear`` emits zero ID bytes. ``Preserve`` copies a
host-supplied sixteen-byte ID without computing/verifying a hash. Hosts own
consistency between a preserved ID and final profile bytes.

Dirty ICC (including removal tombstones detected before filtering) uses strict
canonical transfer and clears IDs. Clean legacy transfer preserves existing
first-wins/skip, ID and final-length compatibility rules. Both use one layout/
emission engine and corrected twelve-byte illuminant encoding.

JPEG, TIFF/DNG, PNG, WebP and other supported carriers retain their contracts;
JXL ICC is encoder handoff. Last-tag deletion does not request carrier removal:
serialization returns ``NoIccData`` and dirty preparation fails. One-tag removal
with other active tags can serialize normally.

ABI 4 and existing layouts remain. Matching 0.8.25 headers and binaries are required;
snapshot 1, patch 2, IIM 1 and signer/adoption wires stay unchanged. Hosts synchronize
mutation; no atomics/mutexes/global IDs are added. Full class/tag conformance and
rendered color accuracy remain host qualification responsibilities.

See the fuller example/contract in ``docs/icc_authoring.md`` and
`ICC.1:2022 clauses7.1–7.4 <https://archive.color.org/specification/ICC.1-2022-05.pdf>`_.
