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
