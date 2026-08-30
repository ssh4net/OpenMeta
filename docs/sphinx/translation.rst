Metadata Translation
====================

``openmeta/metadata_translation.h`` provides bounded explicit projection
between metadata families. The first contract translates edited XMP creation
dates into native EXIF and IPTC date groups before transfer or writing.

The API is experimental and versioned by
``kMetadataDateTranslationContractVersion == 1``.

Workflow
--------

Translation is a separate step. Creation, editing, transfer, and writing do not
invoke it implicitly:

1. Read, create, or edit a finalized ``MetaStore``.
2. Call ``translate_xmp_creation_dates(...)`` with explicit mapping and
   conflict options.
3. Pass the returned finalized store to transfer preparation or a writer.

This separation prevents a transfer from unexpectedly replacing native camera
dates merely because decoded XMP is present. The default source mode is
``DirtyOnly``, so only caller-modified XMP values and tombstones are eligible.

Current mappings
----------------

.. list-table::
   :header-rows: 1
   :widths: 22 38 40

   * - XMP source
     - Native destination
     - Precision requirements
   * - ``xmp:CreateDate``
     - EXIF ``DateTimeDigitized``, ``OffsetTimeDigitized``, and
       ``SubSecTimeDigitized``
     - Time is required. Timezone and up to nine fractional digits are
       preserved in companion tags.
   * - ``xmp:CreateDate``
     - IPTC ``DigitalCreationDate`` and ``DigitalCreationTime``
     - Date-only or whole seconds are accepted. Fractional seconds are
       rejected.
   * - ``photoshop:DateCreated``
     - IPTC ``DateCreated`` and ``TimeCreated``
     - Date-only or whole seconds are accepted. Fractional seconds are
       rejected.
   * - XMP ``exif:DateTimeOriginal``
     - EXIF ``DateTimeOriginal``, ``OffsetTimeOriginal``, and
       ``SubSecTimeOriginal``
     - Time is required. Timezone and up to nine fractional digits are
       preserved in companion tags.

Accepted values use a full Gregorian ``YYYY-MM-DD`` date, optionally followed
by ``T`` and ``hh:mm:ss``, up to nine fractional digits, and ``Z`` or a
``+/-HH:MM`` timezone. Invalid dates, malformed values, and conversions that
would discard precision fail instead of being normalized or truncated.
Lexical ``-00:00`` is preserved as a negative-zero offset.

Each mapping can be disabled independently. For example, callers that need to
retain fractional ``xmp:CreateDate`` can disable its IPTC projection while
keeping exact EXIF projection.

Conflict and removal policy
---------------------------

``MetadataDateTranslationConflictPolicy`` applies to each complete native date
group:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Policy
     - Behavior
   * - ``PreserveExisting``
     - Keep the complete native group when any member already exists.
   * - ``FailOnConflict``
     - Require the group to be absent or already exactly equivalent. This is
       the default.
   * - ``ReplaceExisting``
     - Replace the group, tombstone duplicates, and tombstone stale companion
       fields.

A dirty deleted XMP source is propagated only with ``ReplaceExisting``; the
corresponding native group is tombstoned. Duplicate eligible XMP source
properties are rejected as ambiguous rather than choosing one occurrence.

The operation is transactional. The source is immutable and the output store
is replaced only after all selected mappings parse, reconcile, and finalize.
New native entries retain the source XMP block and wire provenance, with any
referenced wire-type name copied into output-owned storage.
``max_added_entries`` and ``max_operations`` may lower the public hard limits
of 16 added entries and 1024 native operations. Calls keep no global state and
are safe when each concurrent call owns its output store.

C++ example
-----------

.. code-block:: cpp

   #include "openmeta/metadata_translation.h"

   openmeta::MetadataDateTranslationOptions options;
   options.conflict_policy
       = openmeta::MetadataDateTranslationConflictPolicy::ReplaceExisting;

   openmeta::MetaStore translated;
   const openmeta::MetadataDateTranslationResult result
       = openmeta::translate_xmp_creation_dates(edited, options, &translated);
   if (result.status != openmeta::MetadataDateTranslationStatus::Ok) {
       // translated is unchanged.
   }

Python
------

Python calls the same C++ transaction and returns a detached ``Document``:

.. code-block:: python

   translated = edited.translate_creation_dates(
       conflict_policy=openmeta.MetadataDateTranslationConflictPolicy.ReplaceExisting,
   )

Invalid or lossy requests raise ``ValueError`` containing the C++ status,
mapping, and source entry ID. The original ``Document`` is not mutated.

Scope
-----

This milestone is intentionally limited to reverse synchronization of creation
dates. It does not yet provide general arbitrary EXIF/IPTC/XMP translation,
timezone inference, value repair, or automatic synchronization during transfer.
