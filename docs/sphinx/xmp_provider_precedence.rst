Complete custom XMP provider precedence
=======================================

Version 0.8.29 extends transfer precedence to explicitly declared custom XMP
roots. The first provider with an active or dirty-deleted top-level property
owns that whole root. Nested fields, arrays and language alternatives travel
with the selected root. A later provider cannot add children or change its
container kind. Separate roots remain independent, including the same local
name under a different namespace URI.

This applies when portable existing custom XMP output is selected:
``xmp_include_existing=true`` and
``xmp_existing_namespace_policy=PreserveCustom``. Use
``preserve_custom_xmp_structures=true`` when reading source files or destination
carriers through the high-level helpers. Explicit declarations support
``Struct``, ``Bag``, ``Seq`` and ``LangAlt``; see
:doc:`structured XMP editing <structured_xmp_editing>`.

Selection and deletion
----------------------

A source snapshot or file is one logical provider. An optional destination
embedded packet and an optional destination sidecar are separate providers.
Existing source/embedded/sidecar precedence options determine their order.
Provenance blocks inside a provider do not partition its trees.

The identity is the root namespace URI plus its unqualified top-level name.
All children belong to that scope, including fields with another namespace URI,
nested declarations and sparse indexed items. Selected items preserve their
order and multiplicity; portable RDF output can compact sparse array positions.

An empty live container claims its root. A dirty-deleted top-level property
claims deletion; clean tombstones do not claim ownership. A selected bare scalar
can replace a later declared tree. A child-only fragment without a top-level
property cannot claim the root. Discarded declarations cannot shadow a selected
scalar or resurrect its children. Source stores and captured snapshots remain
unchanged.

This policy selects complete declared custom roots. It does not combine fields
from competing versions of a root or define general RDF/MWG synchronization.
Standard namespaces retain their existing rules, including the complete DC
creator/subject selection added in 0.8.28.

Validation and bounds
---------------------

Each provider's explicit graph is checked before precedence removes competing
entries. Duplicate declarations, invalid kinds, missing live parents, malformed
paths and resource-limit failures do not become valid because that provider
would lose. Optional carrier decode failures retain their existing status and
rollback behavior; a failed optional packet contributes no partial tree.

The shared ``XmpStructureLimits`` apply to each input provider and the final
selected store. There are at most three providers. The scoped preflight shares
the existing structure core and borrows the store arena. It does not clone a
whole store for each graph check. File and sidecar decoding can use isolated
cold scratch before ordered append so decoder validation sees one provider at
a time. Preparation may allocate; package replay retains its existing contract.

No public API or wire-format version changes are required. ABI 4 remains
unfrozen. Hosts synchronize shared objects and use matching headers and binaries.

See :doc:`XMP synchronization policy <xmp_sync_policy>`,
:doc:`descriptive reconciliation <descriptive_reconciliation>`, and
:doc:`explicit ICC removal <icc_removal>`.
