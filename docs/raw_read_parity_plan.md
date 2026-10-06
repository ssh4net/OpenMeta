# RAW Read Parity Plan

This page tracks the public read-path work needed to move OpenMeta closer to
ExifTool-level camera RAW coverage. It is about decoding and interpretation,
not writer policy.

Here, camera RAW support means metadata carrier discovery and metadata
interpretation. OpenMeta does not decode compressed sensor samples, demosaic or
render RAW pixels, or provide a general native camera RAW writer. A strong
shared TIFF/EXIF lane can still have partial vendor-private or model-specific
interpretation, and preserving a raw MakerNote block is not the same as naming
every entry inside it.

OpenMeta should keep a conservative rule here: preserve raw payloads whenever a
family is only partially understood, and only promote fields to structured
entries when their location, type, byte order, and meaning are stable enough to
test.

## Validation Method

RAW parity work is compared against ExifTool output with normalized values,
group names, and intentional-difference notes. Each new lane should add:

- a block-discovery test for the native container or embedded metadata carrier
- a structured decode test for stable tags
- a display-name or semantic-group test when the tag is user-visible
- a transfer-safety test when the decoded value is source-specific
- a compare note for unsupported or intentionally raw-only payloads

## Family Gap Matrix

| Family | Current lane | Main gap versus ExifTool | Next work |
| --- | --- | --- | --- |
| DNG and TIFF-based RAW | Strong baseline through TIFF/EXIF/IFD, DNG tags, XMP, ICC, and MakerNote payloads | Long-tail vendor private tables and model-specific MakerNote fields | Keep adding named tables only when they are stable and safety-classified |
| Nikon NEF/NRW | Strong TIFF/EXIF path plus expanded Nikon MakerNote tables and normalized Nikon Capture crop bounds | Model-specific encrypted/custom-setting tables and less common correction records | Add focused Nikon tables with byte-order/version gates and safety buckets |
| Sony ARW/SR2/SRF | Strong TIFF/EXIF path plus Sony RAW/source-processing classification and panorama crop-margin interpretation | Older SRF/SR2 private structures and model-specific private tables | Extend native SR2/SRF table naming and keep raw payload preservation as the fallback |
| Canon CR2 | Strong TIFF/EXIF path plus Canon MakerNote, normalized aspect/crop geometry, and crop/aspect/color-data classification | Long-tail Canon custom functions and per-model color/correction tables | Continue table-by-table decode with rendered-transfer safety coverage |
| Canon CR3 | Bounded BMFF plus EXIF/XMP/ICC/CR3 maker metadata, including item/property associations, direction-aware item relations, semantic item groups, component membership, semantic composition, typed relation counts, bounded `grid`/`iovl`/`iden` constructions, recursive item-offset descriptor resolution, graph-cycle/source validation, and complete bounded `tili` configuration/reference/offset-table interpretation | CR3-specific private records and deeper scene/property-graph semantics | Keep the independent tiled-image gate in release validation, then continue bounded CR3 private-table work |
| Canon CRW/CIFF | Partial native lane with bounded positional recursive CIFF directories, stable scalar/subtable decoding, common native names, and derived EXIF bridge | Older Canon private tables and long-tail legacy records | Continue table-by-table decode only where stable validation data exists |
| Fujifilm RAF | Partial native lane with bounded positional header/directory reads, header-declared preview-JPEG EXIF/XMP and FujiIFD/TIFF traversal, RAF header fields, RAF directory geometry tags, RAFData geometry projection, normalized raw crop/zoom rectangles, and contiguous standalone XMP fallback | Model-specific RAF tables, less common native sections outside the stable carrier/header/directory subset, and callback-safe discovery for undeclared standalone carriers | Extend native RAF section inventory table-by-table, with broader color/correction safety buckets before transfer use |
| Sigma X3F | Partial native lane with bounded positional header/section-directory/PROP reads and declared section-JPEG metadata traversal, known PROP properties, and contiguous legacy embedded-EXIF fallback | Deeper image-processing/compression sections, model-specific private records, and callback-safe discovery for undeclared carriers | Add X3F native sections only when they expose stable user-visible fields or transfer-safety inputs |
| Minolta MRW | Contiguous big-endian MRW TTW and native PRD/WBG/RIF fields; Sony embedded little-endian MRI through buffered or positional TIFF | Unknown native segments and standalone callback discovery | Add native segments only with original evidence and explicit bounds |
| Panasonic, Olympus, Pentax, Kodak, Minolta, Samsung, Ricoh | Mixed TIFF/EXIF and MakerNote table coverage | Older model tables, preview/correction subtables, and private RAW payloads | Prioritize tables that affect crop, color, lens correction, orientation, or transfer safety |
| Apple, DJI, Google, FLIR | Live-vendor source-processing classification exists for rendered-transfer safety | Computational, thermal, radiometric, and shot-log interpretation depth | Add decode only for stable fields that hosts can use safely |
| Rare and legacy RAW families | Raw-preservation-first | Native container and MakerNote depth | Preserve raw blocks, then add support only when validation inputs and stable structure are available |

## Legacy read corrections (0.8.3)

The shared TIFF lane accepts both Olympus `IIRO` and older `IIRS` headers in
buffered and positional decoding. Standalone big-endian MRW discovery scans
exact TTW TIFF spans inside declared metadata segments and excludes the pixel
tail. Version 0.8.7 adds PRD/WBG/RIF fields and Sony embedded MRI; standalone
MRW positional discovery remains separate work.

Nikon D300 ShotInfo uses the same `0210` prefix for two layouts: 5,291 bytes
select layout A and 5,303 bytes select layout B. The choice governs native
shutter count, AF adjustment, ISO code, decrypted firmware and custom settings.
Unknown lengths retain opaque source data. ExifTool value conversions are not
a raw-value equivalence rule.

Nikon D50 encrypted records use the documented nonnumeric serial-key fallback,
two-byte ShotInfo counter and ColorBalance0205 white-balance region. Sony
Tag9400c ModelReleaseYear uses a single byte. These corrections retain the
source payload; other raw scales and ExifTool conversions remain distinct.

Minolta MakerNote value pointers resolve against their enclosing TIFF stream,
including values outside the declared MakerNote byte count. Buffered and
positional reads retain enclosing range and resource limits; this does not
permit references outside the TIFF metadata carrier.

Grouped comparisons keep direct MakerNotes, NikonCapture/Custom tables and
Canon CIFF directories distinct. Matching names or flat IDs do not establish
semantic parity; partial, ambiguous and clipped values remain unqualified.

## Mixed camera corpus expansion (0.8.7)

The grouped reader update adds Canon CIFF CameraSettings, ShotInfo, SensorInfo,
CustomFunctions, ColorBalance and sequential AFInfo fields. Known signed defaults
and unsigned overrides keep their native types. ShotInfo uses the correct word
index; each expanded table keeps its source identity. FocalLength and RawJpgInfo
retain one structured projection rather than duplicate aliases.

Minolta MRW PRD/WBG/RIF fields and Sony little-endian embedded MRI records stop
at the declared metadata boundary. Sony RAW image offsets are distinguished
from valid metadata SubIFDs. TIFF Model/Make are available before private MRI
expansion, including unsorted root directories. WBG channel naming and conditional
RIF fields use documented make/model gates.

Nikon additions cover ColorBalanceA/B and early C, LensData 0202, the gated D4S
selector field and Nikon Capture records. Capture record hashes retain all 32 bits
in their namespace. Known fields are typed; unknown records and XML stay opaque.
Raw crop doubles retain their encoded scale; geometry queries normalize the
confirmed CropData bounds by two and reject nonfinite bounds.

Sony CameraSettings and older private CameraInfo/FocusInfo/MoreInfo records use
payload length, model, byte order and bit masks. Kodak private-IFD discovery and
legacy CameraInfo roots retain enclosing-TIFF offsets and prefer a complete
directory at its recorded pointer over nearby recovery candidates. Unknown Sony
MoreInfo records retain their bounded byte spans in a separate native table.
Encoded metering metadata remains source bytes; the reader does not construct
ExifTool's TIFF representation. Tag900b uses its cipher prefix and model gates.
Pentax/Samsung DNG private
carriers distinguish supported TemperatureInfo models from unknown large payloads.
Sigma/Hasselblad offsets, old RAF header bounds and SD9/SD10 X3F discovery are
corrected without reading image payloads as metadata.

The Sony A100's nonzero Minolta MakerNote pointer has a separate enclosing-TIFF
subtree. Its CameraInfo, image-stabilization, white-balance and settings layouts
use their own source formats and byte order, with an exact A100 model gate.
Unrecognized Sony private carriers and non-bare Hasselblad notes retain opaque
handling instead of inheriting a directory error from an unrecognized layout.

These native entries retain encoded numeric values unless an existing documented
semantic representation applies. ExifTool `-n` still applies ValueConv: Canon
APEX and focal units, Kodak reciprocal white balance, Nikon Capture exposure/crop
scales and MRW colour temperature need explicit comparison rules. Matching names,
flat ID unions or preserved raw parents alone do not establish field parity.
Unknown layouts, damaged originals and pixel/compression records remain separate
from the confirmed native-field scope.

## CR3 carrier inventory (0.6.2)

The bounded Canon UUID path currently recognizes these records:

| Record | Current handling | Limit |
| --- | --- | --- |
| `CMT1`, `CMT2`, `CMT4` | Discover a classic TIFF stream and use the generic TIFF decoder. | The scanner requires a valid classic-TIFF header; this does not define additional Canon private-field meanings. |
| `CMT3` | Decode the dedicated TIFF stream with Canon MakerNote tokens and expand known binary subtables. Fall back to generic TIFF decoding with Canon tokens when needed. | `decode_makernote=false` skips this stream. |
| `CNCV` | With MakerNote decoding enabled, expose bounded `CanonCR3_` compressor-version text as the derived BMFF field `cr3.compressor_version`. The opaque scanner block remains available. | Accept at most 256 bytes of printable ASCII with optional trailing zero padding. Other prefixes, embedded NULs, controls and non-ASCII bytes remain opaque. This does not parse version components or authorize writing codec settings. |
| `CTBO` | With MakerNote decoding enabled, expose validated absolute box offsets and sizes for XMP (ID 1), preview (ID 2), and media data (ID 3). | Derived `cr3.ctbo.xmp.offset` / `.size`, `cr3.ctbo.preview.offset` / `.size`, and `cr3.ctbo.media.offset` / `.size` are source-bound, not writable metadata. Validate the complete table and known target boxes before emitting fields. Unknown IDs retain opaque treatment. |
| `CCTP`, `CNTH`, `CNOP`, `CNDM`, `CMP1` | Descend into plausible nested boxes; otherwise expose leaf payloads as opaque scanner MakerNote blocks. | The simple metadata reader does not interpret these opaque leaf blocks into entries. |

The Canon UUID walk has separate depth, box-count and pending-range bounds.
Unknown children are searched only when they begin with a plausible nested
box. Other unknown payloads are skipped; general raw preservation does not
imply that every private CR3 record is exposed or safely relocatable. The
ordinary EXIF MakerNote tag and the separate `PRVW` JPEG preview route retain
their own contracts.

Grouped synthetic regressions exercise all eleven listed IDs, classic-TIFF
header rejection, opaque leaf boundaries, and CMT3 Canon CameraSettings
expansion, disabling and truncation. They qualify these existing routes, not
broader private semantics or real-file prevalence. The current original-file
inventory contains 23 CR3 files, each with a 30-byte `CNCV` record and one of
five compressor-version strings. Both independent record extraction and the
OpenMeta reader agree with ExifTool for all 23 values. The historical 24-file
EXIF comparison does not
list per-file inputs and does not establish the identity of this current
cohort. Further private-field decoding needs an original witness with recorded
model/version, offsets, byte order, bounds and expected values.

CTBO uses a big-endian count followed by 20-byte ID/offset/size rows. The known
IDs refer to complete boxes, including headers: XMP and preview require their
documented UUIDs, and media requires `mdat`. These fields describe the original
carrier locations; they do not identify the active metadata item after an edit,
decode image data, authorize relocation, or project into portable XMP. ExifTool
documents this layout in its Canon reader and QuickTime writer but does not
expose CTBO values while reading. CTBO comparisons therefore use independent
wire extraction, separately from ExifTool's compressor-version comparison.
The reader accepts 1–64 rows with an exact payload length; 64 is an
implementation bound, not a Canon format limit. Duplicate known IDs, invalid
known target ranges, mismatched box headers and size-zero-to-EOF target headers
remain opaque. Normal and extended-size headers are supported. Zero-size table
rows emit no location fields, and unknown IDs are not followed.

CR3 editing preserves every retained top-level source box at its original
offset. An edit that would move one rejects before output; CTBO and track
offsets are not repaired. Appending metadata and replacing EOF metadata remain
supported. General CR3 scene/property rewriting remains outside this contract.

Since 0.6.2, the scanner also discovers `CMP1` inside bounded `CRAW` sample
entries in `stbl/stsd`. This route requires version/flags zero, a normal
sample-entry header, the recognized `0x00010001` extension at byte 86, and a
complete child-box list beginning at byte 90. Unknown entry layouts are skipped.
The scanner charges entries and child boxes to its existing box-count budget
and keeps memory and positional-callback scanning equivalent without reading
`mdat` payloads.

With MakerNote decoding enabled, a normal 60-byte `CMP1` box with a 52-byte
payload and the prefix `ff00003001000000` exposes nonzero big-endian dimensions
at payload offsets 16 and 20 as `cr3.cmp1.width` and `cr3.cmp1.height`. Each
record has its own source block and an absolute `cr3.cmp1.offset`, so repeated
records remain distinguishable. These are derived source values, not writable
metadata or portable XMP. They do not assign main/raw/preview track roles or
rendered-image dimensions; sample-entry dimensions can differ. Other signatures,
sizes, and the separate Canon UUID `CMP1` route remain uninterpreted. Independent
wire extraction and ExifTool agree on the dimensions of all 46 records in the
current 23-file original cohort.

## Priority

The bounded tiled-image contract was independently qualified on 2026-09-22
with four internal fixtures covering explicit internal offsets, sequential
size inference, external tile URLs, and a malformed conditional payload. The
remaining BMFF work is deeper scene/property semantics and CR3 private records.

1. Keep writer safety explicit: decoded MakerNote sub-IFDs are not used to
   reconstruct vendor MakerNote blobs; the original raw MakerNote payload is
   preserved when available.
2. Continue high-visibility native read gaps: more model-specific RAF native
   sections, long-tail CRW/CIFF private tables, and deeper X3F section
   interpretation.
3. Deepen remaining BMFF interpretation for CR3, HEIF, and AVIF metadata
   graphs beyond current construction descriptors, component membership,
   direction-aware typed relations, semantic item groups, and primary-item
   summaries.
4. Add X3F image-processing section decode only when the fields can be named,
   typed, and safety-classified.
5. Continue vendor MakerNote table work for fields that affect crop, color,
   orientation, lens correction, or safe transfer decisions.

## RAW Curve Applicability

RAW curve and LUT metadata should not be treated as automatically active just
because the tag is present. Some formats may store curve-like metadata in both
compressed and uncompressed variants, while only one raw storage path actually
uses it.

OpenMeta now exposes a conservative applicability scaffold for RAW-processing
concept candidates:
- `MetadataRawDataEncoding` describes the host or decoder view of stored raw
  pixels, such as uncompressed, packed, lossless-compressed, lossy-compressed,
  rendered, or unknown.
- `MetadataRawDataDescriptor` is the public carrier for dimensions,
  channel-count, bit-depth, compression code, storage encoding, optional raw
  plane index, and optional `requires_compressed_raw_encoding` /
  `requires_primary_raw_plane` flags when a host or decoder can provide them.
- `MetadataRawApplicabilityState` marks current concept candidates as unknown,
  applicable to stored raw samples, conditional on raw encoding, or not
  applicable to stored raw samples.

The default resolver still marks curve/LUT-like RAW roles as conditional on raw
encoding when no storage context is supplied. Descriptor-aware concept
resolution overloads accept `MetadataRawDataDescriptor`; those overloads can
mark recognized RAW-processing roles as applicable to stored RAW samples for
known RAW encodings, not applicable for rendered data, or not applicable when a
curve/LUT-like role is explicitly marked compressed-storage-only but the source
descriptor says the raw samples are uncompressed or packed. This is a
conservative storage-context classification, not proof that a vendor curve is
active for a specific file.

If a decoder knows that a curve/LUT-like metadata entry only affects the
primary raw plane, set `requires_primary_raw_plane = true` and provide
`has_plane_index` / `plane_index` for the raw buffer being described. OpenMeta
then marks that curve as not applicable for non-primary planes and conditional
when the active plane is unknown.

Transfer preparation can also consume
`PrepareTransferRequest::source_raw_data_descriptor`. When that descriptor says
the source pixels are rendered, RAW-processing metadata is filtered even under
compatible-file safety. The remaining gap is finer binding to the exact raw
blob, packing/compression mode, and active decoder path before declaring that a
specific vendor LUT or curve is active.

Future interpretation work should bind curve/LUT entries to the raw data
descriptor that records the relevant blob, compression or packing mode,
sample layout, offsets/byte counts when available, and the decoder stage where
the curve applies.

Verification should require more than tag-name comparison: decoder-source
tracing, runtime branch confirmation, metadata mutation/removal tests, and raw
pixel-buffer diffs across compressed and uncompressed samples should be used
before OpenMeta promotes a curve/LUT from present metadata to an active
raw-processing operation.
