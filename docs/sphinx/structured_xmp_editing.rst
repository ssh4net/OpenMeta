Structured XMP creation and editing
===================================

OpenMeta 0.8.14 adds ``edit_metadata_xmp()`` in
``openmeta/metadata_xmp_editing.h``, contract 1. A finalized empty store plus Add
operations creates fresh structured XMP. Existing stores support Add, Replace
and Remove for whole structures, whole arrays, indexed records and exact language
members, with one transactional publication and base/output alias support.

Scopes and representation
-------------------------

Supported roots are IPTC Core ``CreatorContactInfo``/``LocationCreated``, IPTC
Extension ``LocationShown``, PLUS ``Licensee``, Dublin Core ``title``/``description``/
``rights``, and XMP Rights ``UsageTerms``/``Owner``. Operations use exact schema
URIs and borrowed complete ``MetadataAuthoringEntry`` XMP text leaves.

Canonical paths reuse the portable writer's shapes, including contact child
structures, indices and language alternatives, indexed location and licensee
records, and declared location ``xmp:Identifier``/``exif:GPS*`` children. This is
structural representability, not complete schema field semantics. Unknown nested
prefixes, qualified roots, redundant prefixes, compatibility aliases, leading-zero
indices and unsupported shapes reject. Canonical location city paths are flat
``LocationShown[n]/City``; ``Address/City`` is an input compatibility alias.

Add requires absent scope/nonempty entries. Replace requires present scope and
nonempty replacement; common keys retain provenance, omitted keys tombstone and
new keys are Dirty. Remove requires present scope/no entries. Duplicate keys,
ancestor/leaf shape collisions, unsupported or truncated/unreadable selected data
and conflicting siblings reject. Item 1 never selects item 10. Requests observe
prior operations and fail without changing output.

Bounds and limits
-----------------

The editor expands bounded active-key state into a single typed edit. It is an
allocating preparation API. Ceilings cover operations, expanded operations,
tracked/input leaves, keys, values, request bytes and output arena bytes. Path
depth is at most 3 and canonical positive indices at most 65,535. ``options.typed``
reuses existing validation and byte ceilings. ``max_scope_entries`` also bounds
represented nonempty array items.

Portable serialization with ``include_existing_xmp=true`` and existing transfer
wrappers consume the result. Existing-XMP versus generated metadata conflict
policy remains explicit. Stored array gaps compact into RDF positions on output;
indices are not permanent IDs. The flat model retains language alternatives,
not arbitrary source RDF attributes, original Bag/Seq choice or XML bytes.
Existing decoder boundary-whitespace trimming is unchanged.

ABI 4 and existing option layouts/source snapshot v1 remain unchanged. New callers
need 0.8.14 headers and symbols. Hosts synchronize mutations. See
``docs/structured_xmp_editing.md`` for exact namespace URIs, example and limits.

The request-byte ceiling counts every supplied key and payload before text
validation, including inputs later replaced. Expanded transaction bytes are
checked separately.
