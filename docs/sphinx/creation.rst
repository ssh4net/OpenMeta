Metadata Creation
=================

``openmeta/metadata_creation.h`` is the first high-level Creation contract for
fresh metadata. A host supplies logical fields instead of wire tag IDs or XMP
namespace paths. OpenMeta validates the complete request and returns a
finalized ``MetaStore`` containing canonical portable-XMP entries.

The API is experimental and versioned by
``kMetadataCreationContractVersion == 1``.

C++ example
-----------

.. code-block:: cpp

   #include "openmeta/metadata_creation.h"

   #include <array>

   const std::array fields = {
       openmeta::make_metadata_creation_text(
           openmeta::MetadataCreationFieldKind::Title, "Evening frame"),
       openmeta::make_metadata_creation_text(
           openmeta::MetadataCreationFieldKind::Creator, "Alice"),
       openmeta::make_metadata_creation_text(
           openmeta::MetadataCreationFieldKind::Keyword, "night"),
       openmeta::make_metadata_creation_u32(
           openmeta::MetadataCreationFieldKind::Orientation, 6),
       openmeta::make_metadata_creation_urational(
           openmeta::MetadataCreationFieldKind::ExposureTime, 1, 125),
   };

   openmeta::MetadataCreationRequest request;
   request.fields = fields;

   openmeta::MetaStore store;
   const openmeta::MetadataCreationResult result =
       openmeta::create_metadata(request, &store);

The call is transactional. The output is replaced only after every field
passes validation and the new store is finalized. An empty request creates an
empty finalized store.

Current mapping
---------------

.. list-table::
   :header-rows: 1
   :widths: 20 18 38 24

   * - Logical field
     - Input type
     - Portable-XMP property
     - Constraint
   * - ``Title``
     - Text
     - ``dc:title[x-default]``
     - Singleton
   * - ``Description``
     - Text
     - ``dc:description[x-default]``
     - Singleton
   * - ``Creator``
     - Text
     - ``dc:creator[n]``
     - Additive ordered sequence
   * - ``Keyword``
     - Text
     - ``dc:subject[n]``
     - Additive bag
   * - ``Copyright``
     - Text
     - ``dc:rights[x-default]``
     - Singleton
   * - ``RightsUsageTerms``
     - Text
     - ``xmpRights:UsageTerms[x-default]``
     - Singleton
   * - ``Credit`` / ``Source``
     - Text
     - ``photoshop:Credit`` / ``photoshop:Source``
     - Singleton
   * - ``CreateDate`` / ``ModifyDate``
     - Text
     - ``xmp:CreateDate`` / ``xmp:ModifyDate``
     - Caller supplies XMP date syntax
   * - ``Rating``
     - Signed integer
     - ``xmp:Rating``
     - ``-1..5``
   * - ``Label``
     - Text
     - ``xmp:Label``
     - Singleton
   * - ``CameraMake`` / ``CameraModel``
     - Text
     - ``tiff:Make`` / ``tiff:Model``
     - Singleton
   * - ``Software``
     - Text
     - ``xmp:CreatorTool``
     - Singleton
   * - ``DateTimeOriginal``
     - Text
     - ``exif:DateTimeOriginal``
     - Caller supplies XMP date syntax
   * - ``Orientation``
     - Unsigned integer
     - ``tiff:Orientation``
     - EXIF index ``1..8``
   * - ``PixelWidth`` / ``PixelHeight``
     - Unsigned integer
     - ``exif:ExifImageWidth`` / ``exif:ExifImageHeight``
     - Nonzero
   * - ``ColorSpace``
     - Unsigned integer
     - ``exif:ColorSpace``
     - ``1..65535``
   * - ``ExposureTime`` / ``FNumber`` / ``FocalLength``
     - Unsigned rational
     - Matching ``exif`` property
     - Nonzero numerator and denominator
   * - ``IsoSensitivity``
     - Unsigned integer
     - ``exif:ISO``
     - Nonzero

Validation and threading
------------------------

Text must be non-empty valid UTF-8 containing XML 1.0 character data. Hard
limits are 1024 fields, 1 MiB of text per field, and 8 MiB of text per request.
Request limits may lower but not raise those bounds. Date text is preserved but
not parsed or normalized in this milestone.

The function uses no global state. Concurrent calls are safe when each call
uses a distinct output store.

Python
------

.. code-block:: python

   import openmeta

   K = openmeta.MetadataCreationFieldKind
   document = openmeta.create_metadata([
       openmeta.metadata_creation_text(K.Title, "Evening frame"),
       openmeta.metadata_creation_text(K.Creator, "Alice"),
       openmeta.metadata_creation_u32(K.Orientation, 6),
       openmeta.metadata_creation_urational(K.ExposureTime, 1, 125),
   ])

The returned object is a normal ``Document``. Invalid requests raise
``ValueError`` with the C++ status and rejected field index.

Use the matching transactional Editing API to modify these fields in an
existing finalized store. See :doc:`editing`.

Scope and safety
----------------

Creation emits canonical portable-XMP entries so existing sidecar and transfer
writers can consume the result without original file-layout information.
Direct EXIF/IPTC projection is not implied by this API. Use the explicit
reverse-date Translation step for supported creation dates; see
:doc:`translation`.

Dimensions, orientation, and color space must describe destination pixels.
They must not be copied from a differently sized, rotated, converted, or
color-transformed source image. Custom properties, non-default languages,
structured XMP, binary values, fresh ICC profiles, and direct EXIF/IPTC block
construction remain outside this milestone.
