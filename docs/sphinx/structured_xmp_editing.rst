Structured XMP creation and editing
===================================

OpenMeta 0.8.14 adds ``edit_metadata_xmp()`` in
``openmeta/metadata_xmp_editing.h``, contract 2. A finalized empty store plus Add
operations creates fresh structured XMP. Existing stores support Add, Replace
and Remove for whole structures, whole arrays, indexed records and exact language
members, with one transactional publication and base/output alias support.

Standard scopes and representation
----------------------------------

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

ABI 4 and source snapshot v1 remain. The 0.8.26 option layouts grow within
unfrozen ABI 4; rebuild consumers with matching headers and binaries. Hosts synchronize mutations. See
``docs/structured_xmp_editing.md`` for exact namespace URIs, example and limits.

The request-byte ceiling counts every supplied key and payload before text
validation, including inputs later replaced. Expanded transaction bytes are
checked separately.

Custom trees in 0.8.26
----------------------

``make_xmp_container_entry()`` declares Struct, Bag, Seq and LangAlt nodes at
exact custom URI/path keys. Empty containers, record arrays and nested arrays
are persistent; leaves are UTF-8 or ASCII text. Parents must be declared.
Expanded QName and case-insensitive language duplicates reject. Sparse stored
indices remain stable and compact into RDF positions on output. Custom language
selection is case-insensitive; x-default emits first if supplied.

Unqualified children inherit the root URI. Registered prefixes or canonical
``nsu_<lowercase hexadecimal URI>:Local`` tokens name cross-namespace fields.
XML/XMLNS/RDF/OpenMeta syntax namespaces cannot be data fields.

``XmpStructureLimits`` hard maxima are 4096 retained declarations plus active
selected leaves, eight expanded name/selector levels, 64 URIs, 4096 bytes per
path/URI, 16 MiB total key/text bytes and index 65535. Limits are nonzero and may
only decrease. Retained tombstone declarations count and identify root intent;
live children require live parents. Complete deletion is valid. Creation and
typed/structured editing validate the whole final tree before publication.

Portable output selects these trees with ``include_existing_xmp=true`` and
``PreserveCustom``. Invalid selected graphs/options leave output unchanged;
short spans retain the existing prefix/OutputTruncated contract. Validation,
portable output, sidecars and transfer expose shared structure limits. Query
results suppress markers; custom fields remain opaque to raw filtering.

``preserve_custom_structures=true`` opts decoding into declaration retention and
exact custom text. Default decoding stays flat. Early node/key/text/namespace
budgets apply. Decode may append partial entries on failure; discard the scratch
store then. Unsupported resources, qualifiers, typed nodes, non-language Alt and
mixed structural text fail. This does not provide full RDF or original XML-byte
fidelity. Snapshot wire v1 retains declarations without a wire-version change.
The new API is C++; hosts own synchronization. See the Markdown guide for exact
options and a creation example.
