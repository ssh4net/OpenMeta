Generic Typed Metadata Authoring
================================

``openmeta/metadata_authoring.h`` provides the C++ v1 construction path for
applications that know their exact writable keys. It complements the simpler
logical-field Creation API.

The transactional builder accepts borrowed typed entries for EXIF/TIFF and
DNG-style tags, XMP properties, and IPTC-IIM datasets. Keys and values are
deep-copied into a finalized ``MetaStore``. Unknown/private EXIF tags and custom
XMP namespace URIs do not require registry changes.

.. code-block:: cpp

   const std::array entries = {
       openmeta::MetadataAuthoringEntry {
           openmeta::make_exif_tag_key_view("ifd0", 0x010F),
           openmeta::make_value_view_text(
               "Example Camera", openmeta::TextEncoding::Ascii),
           openmeta::WireType { openmeta::WireFamily::Tiff, 2 },
           15,
       },
       openmeta::MetadataAuthoringEntry {
           openmeta::make_xmp_property_key_view(
               "urn:example:capture:1.0/", "Gain[1]"),
           openmeta::make_value_view_text(
               "1.25", openmeta::TextEncoding::Utf8),
       },
   };

   openmeta::MetaStore store;
   const auto result = openmeta::create_metadata_store(entries, &store);

The output changes only after the complete request passes resource,
structural, and enabled schema validation. Duplicate keys are preserved by
default; known EXIF singletons are schema-checked. ``MetaStore::reserve()`` is
also available for trusted low-level build paths and honors active resource
ceilings.

Detached validation
-------------------

``validate_entry()`` and ``validate_store()`` check value shape, rational
denominators, known TIFF/EXIF/GPS/DNG IFD/type/count rules, duplicate
singletons, XMP URI/path syntax, resource limits, and optional image/CFA/color
relationships. Unknown/private EXIF tags remain allowed by default.

Custom XMP currently covers safe scalar and indexed properties. Emit them with
``dump_xmp_portable()`` and ``PreserveCustom``. Full arbitrary RDF structures
and caller-selected prefix spelling remain outside v1.

Structural validity does not prove image correctness. The host remains
authoritative for dimensions, channel layout, CFA, levels, color transforms,
and frame-varying capture facts.

See the complete contract in ``docs/generic_authoring.md``.

Standard field validation in 0.5.10
----------------------------------

All 64 reverse targets missing fixed schemas in the 0.5.9 audit now have native
IFD/type/count/singleton rules. Five registered Interop fields and three structural
pointers are recognized. InteroperabilityIndex no longer fails merely because
GPS reuses its numeric ID. See :doc:`exif_authoring_inventory`.

Shared checks cover capture enums, Flash, LightSource, sensitivity values,
rationals/sentinels, lens and subject arrays, GPS references/bearings, ASCII text
and encoded GPS prefixes. GainControl remains SHORT per its detailed definition.
Malformed known values formerly accepted as unknown can now fail default
construction, typed editing or canonical serialization. Invalid native values
leave eligible retained XMP available when existing-XMP output is enabled.

Detached stores do not acquire mandatory complete-file companions or translator
text/precision limits. Subsecond text can exceed nine digits and retain spaces.
Native APEX denominators may be negative but not zero. Native focal-plane units
1--5 remain accepted; reverse translation retains its narrower 2/3 contract.
Lens 0/0 remains restricted to the two unknown aperture slots. GPS JIS, Unicode
and undefined-code bodies stay opaque after prefix validation; ASCII bodies
must be 7-bit. Pointer schemas check type/count, not target existence.

Unknown/private policy, host synchronization, ABI 3 and snapshot v1 layout
remain unchanged. These rules do not establish full EXIF file conformance.
