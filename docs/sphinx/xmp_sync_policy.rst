XMP Sync And Writeback Policy
=============================

This page defines the bounded public policy for generated portable XMP during
metadata transfer.

It covers:

- generated EXIF-to-XMP properties
- generated IPTC-to-XMP properties
- source embedded XMP decoded from the source metadata
- destination embedded XMP loaded from the target file
- destination sidecar XMP loaded from a sibling ``.xmp``

This is not a full arbitrary metadata synchronization engine. It is the
writer-side contract used by the transfer helpers so hosts can predict
preserve, merge, and writeback behavior.

For target-specific embedded carrier replacement and unmanaged-data
preservation rules, see :doc:`writer_target_contract`.

Default Contract
----------------

The default transfer behavior is conservative:

.. list-table::
   :header-rows: 1
   :widths: 34 18 48

   * - Option
     - Default
     - Effect
   * - ``PrepareTransferRequest::xmp_project_exif``
     - ``true``
     - Generate portable XMP properties from EXIF/TIFF/GPS fields.
   * - ``PrepareTransferRequest::xmp_project_iptc``
     - ``true``
     - Generate portable XMP properties from IPTC-IIM fields.
   * - ``PrepareTransferRequest::xmp_include_existing``
     - ``true``
     - Include existing XMP already decoded into the source ``MetaStore``.
   * - ``PrepareTransferRequest::xmp_conflict_policy``
     - ``CurrentBehavior``
     - Emit generated EXIF properties, then existing XMP, then generated IPTC
       properties.
   * - ``xmp_existing_sidecar_mode``
     - ``Ignore``
     - Do not merge a destination sidecar unless explicitly requested.
   * - ``xmp_existing_destination_embedded_mode``
     - ``Ignore``
     - Do not merge destination embedded XMP unless explicitly requested.
   * - ``xmp_writeback_mode``
     - ``EmbeddedOnly``
     - Keep generated XMP in the managed embedded carrier.
   * - ``xmp_destination_embedded_mode``
     - ``PreserveExisting``
     - Preserve existing embedded XMP when sidecar-only writeback suppresses
       generated embedded XMP.
   * - ``xmp_destination_sidecar_mode``
     - ``PreserveExisting``
     - Preserve an existing sibling ``.xmp`` when embedded-only writeback is
       used.

Native EXIF and IPTC carrier emission is independent from XMP projection.
Turning off ``xmp_project_exif`` or ``xmp_project_iptc`` suppresses only the
generated XMP projection, not native EXIF/IPTC transfer.

Existing XMP Carrier Precedence
-------------------------------

OpenMeta first builds one decoded transfer store. Optional destination XMP
carriers are merged into that store before or after the source entries
according to explicit precedence options.

For duplicate existing XMP properties, earlier merged entries win during
portable XMP output.

.. list-table::
   :header-rows: 1
   :widths: 30 34 18 18

   * - Conflict
     - Option
     - Default order
     - Alternate order
   * - destination sidecar vs source embedded XMP
     - ``xmp_existing_sidecar_precedence``
     - sidecar before source (``SidecarWins``)
     - source before sidecar (``SourceWins``)
   * - destination embedded XMP vs source embedded XMP
     - ``xmp_existing_destination_embedded_precedence``
     - destination before source (``DestinationWins``)
     - source before destination (``SourceWins``)
   * - destination sidecar vs destination embedded XMP
     - ``xmp_existing_destination_carrier_precedence``
     - sidecar before embedded (``SidecarWins``)
     - embedded before sidecar (``EmbeddedWins``)

The destination sidecar is loaded only when
``xmp_existing_sidecar_mode == MergeIfPresent``. The destination embedded
packet is loaded only when
``xmp_existing_destination_embedded_mode == MergeIfPresent``.

If both destination sidecar and destination embedded XMP are merged on the
same side of the source entries,
``xmp_existing_destination_carrier_precedence`` decides which destination
carrier wins. If they are placed on opposite sides of the source entries, the
two source-precedence options define the effective order.

Failed or missing optional destination carriers do not silently change the
source metadata. The result reports a status and message for the attempted
sidecar or destination-embedded load.

Generated EXIF/IPTC Versus Existing XMP
---------------------------------------

After the transfer store is assembled, ``xmp_conflict_policy`` decides the
relative precedence between generated portable XMP and the existing XMP set.
The existing XMP set includes source embedded XMP plus any destination XMP
carriers that were explicitly merged.

.. list-table::
   :header-rows: 1
   :widths: 18 34 48

   * - Policy
     - Pass order
     - Practical effect
   * - ``CurrentBehavior``
     - EXIF-derived, existing XMP, IPTC-derived
     - EXIF projection wins over existing XMP; existing XMP wins over IPTC
       projection.
   * - ``ExistingWins``
     - existing XMP, EXIF-derived, IPTC-derived
     - Existing XMP wins over generated EXIF/IPTC projection.
   * - ``GeneratedWins``
     - EXIF-derived, IPTC-derived, existing XMP
     - Generated EXIF/IPTC projection wins over existing XMP.

When generated EXIF and generated IPTC projection both claim the same portable
property, EXIF-derived output is emitted first and wins.

``xmp_existing_standard_namespace_policy`` applies inside the existing-XMP
pass:

- ``PreserveAll`` keeps existing standard portable namespace entries subject
  to the conflict order above.
- ``CanonicalizeManaged`` drops OpenMeta-managed standard portable properties
  from existing XMP when a generated replacement exists.

``xmp_existing_namespace_policy`` controls breadth:

- ``KnownPortableOnly`` keeps only standard portable namespaces known to
  OpenMeta.
- ``PreserveCustom`` also preserves safe simple or indexed properties from
  custom namespaces.

Paired IPTC Creation Dates
--------------------------

Portable IPTC projection composes these record-2 dataset pairs before applying
the conflict policy:

.. list-table::
   :header-rows: 1
   :widths: 58 42

   * - IPTC-IIM datasets
     - Portable XMP property
   * - ``DateCreated`` (2:55) plus optional ``TimeCreated`` (2:60)
     - ``photoshop:DateCreated``
   * - ``DigitalCreationDate`` (2:62) plus optional
       ``DigitalCreationTime`` (2:63)
     - ``xmp:CreateDate``

Dates must use the IPTC ``YYYYMMDD`` form and pass Gregorian calendar
validation. Times must use ``HHMMSS`` with an optional ``+HHMM`` or ``-HHMM``
offset. OpenMeta normalizes accepted values to XMP date/date-time syntax,
including a colon in the UTC offset.

The first valid non-deleted date and time occurrence in each pair is used. A
valid date emits as date-only when its time companion is absent or malformed.
A time without a valid date emits nothing. Existing-XMP conflict behavior is
the same as for other generated IPTC properties. ``CanonicalizeManaged``
removes an existing managed date only when strict composition produced a valid
replacement.

Writeback Modes
---------------

Writeback mode controls where the generated XMP packet goes after transfer
execution.

.. list-table::
   :header-rows: 1
   :widths: 18 32 25 25

   * - Mode
     - Edited file
     - Sibling ``.xmp``
     - Cleanup behavior
   * - ``EmbeddedOnly``
     - Generated XMP stays in the managed embedded carrier.
     - No generated sidecar output.
     - Existing sidecar is preserved unless
       ``xmp_destination_sidecar_mode == StripExisting``.
   * - ``SidecarOnly``
     - Generated embedded XMP blocks are suppressed.
     - Generated XMP is returned as sidecar output.
     - Existing embedded XMP is preserved unless
       ``xmp_destination_embedded_mode == StripExisting``.
   * - ``EmbeddedAndSidecar``
     - Generated XMP stays in the managed embedded carrier.
     - The same generated XMP is returned as sidecar output.
     - No destination sidecar cleanup is requested.

Destination sidecar cleanup is supported only for embedded writeback. If
``xmp_destination_sidecar_mode == StripExisting``, OpenMeta returns a cleanup
request for the sibling ``.xmp``;
``persist_prepared_transfer_file_result(...)`` performs the removal when
``remove_destination_xmp_sidecar`` is true.

Destination embedded-XMP stripping is supported for sidecar-only writeback on
the current managed writer targets: JPEG, TIFF/DNG, PNG, WebP, JP2, JXL, and
bounded BMFF targets (``HEIF``, ``AVIF``, ``CR3``). Other combinations report
an unsupported policy result instead of guessing.

Sidecar output and cleanup paths are derived from ``xmp_sidecar_base_path``
when provided, otherwise from the edit/output target path. Hosts that do not
have a filesystem path can set ``xmp_existing_destination_sidecar_state``
explicitly so OpenMeta can return deterministic cleanup guidance without
probing the filesystem.

The persistence helper writes generated sidecars only when sidecar output is
requested. It does not overwrite an existing sidecar unless
``overwrite_xmp_sidecar`` is true.

Non-Goals
---------

This bounded policy intentionally does not claim:

- full MWG-style EXIF/IPTC/XMP reconciliation
- arbitrary XMP graph editing
- raw packet passthrough or byte-for-byte XMP preservation
- semantic conflict resolution beyond the documented portable property order
- full sidecar and embedded synchronization for every possible namespace

Those remain broader parity work. The current contract is meant to be stable
enough for predictable transfer and export workflows.
