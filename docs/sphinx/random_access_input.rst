Random-Access Input
===================

OpenMeta's random-access input contract is designed for image-processing and
transcoding hosts that already own storage, scheduling, and I/O policy. It is
not tied to a particular image library or file abstraction.

Current scope
-------------

Version 0.4.100 added the allocation-free source primitive in
``openmeta/random_access_source.h``. Version 0.4.101 added source ranges,
caller-owned read windows, and the first decoder conversion. Version 0.4.102
added source-backed nested TIFF offset resolution. Version 0.4.103 extends the
conversion to Olympus, Panasonic, and Samsung MakerNotes. Version 0.4.104 adds
bounded Fujifilm and General Imaging MakerNote source layouts. Version 0.4.105
adds Kodak fixed-layout and outer-TIFF-relative MakerNotes. Version 0.4.106 adds
Ricoh mixed-base and vendor subdirectory decoding plus Nintendo, Casio, Minolta,
and FLIR callback parity. Version 0.4.107 adds bounded JPEG segment scanning.
Version 0.4.108 adds positional PNG/WebP chunk scanning and bounded JP2/JXL/
ISO-BMFF box and metadata-item traversal. Version 0.4.109 adds positional GIF
extension scanning, EXR header traversal, logical metadata payload extraction,
and decoded source-snapshot assembly. Version 0.4.111 adds native RAF, X3F, and
CRW/CIFF positional metadata traversal plus structured snapshot-read
diagnostics. It provides:

- a fixed source size and synchronous ``read_at(offset, destination)`` callback
- a non-owning descriptor for caller-owned contiguous memory
- distinct exact-read, short-read, I/O, cancellation, and source-change results
- per-operation request-count, total-byte, and single-read ceilings
- independent sticky accounting state with first-failure diagnostics
- borrowed source subranges and caller-owned read-ahead windows
- direct zero-copy views for contiguous sources

``decode_exif_tiff_random_access(...)`` now traverses classic TIFF, BigTIFF,
DNG, Panasonic RW2, and Olympus ORF headers, IFDs, pointer directories, and
validated metadata values without materializing the complete source. PrintIM,
GeoTIFF, Pentax DNG private data, and selected self-contained MakerNotes are
also decoded from caller scratch. Nikon embedded TIFF/type 1, Sony outer-TIFF
IFDs, contained Canon adjusted-base payloads, Olympus nested IFDs, Panasonic
binary tables, Samsung STMN/Type2 derived tables, Fujifilm self-relative IFDs,
General Imaging Type 2 source windows, and Kodak Type 8, Type 10, and Type 11
IFDs with vendor subtables use the same bounded source. Ricoh classic notes
retain ImageInfo, CameraInfo, FaceInfo, SerialInfo, and Theta expansion, while
Nintendo CameraInfo, Casio QVC/DCI, Minolta binary tables, and FLIR
outer-TIFF-relative values retain their contiguous behavior.

Callback decoding still reports Canon derived subtables whose values extend
outside the declared MakerNote, plus unknown or unsupported vendor layouts,
through ``nested_payloads_skipped`` and ``complete()`` rather than decoding
against an incorrect subspan. Contiguous sources retain the complete existing
nested-decoder behavior.

``scan_jpeg_random_access(...)`` locates leading EXIF, XMP, ICC, MPF, vendor
APP, JUMBF, Photoshop IRB, FLIR, and comment segments through the same
positional source. It reads at most 512 bytes from a metadata segment for
classification, preserves multipart APP11 normalization, and stops at Start of
Scan without reading entropy-coded image data. Returned offsets are relative to
the supplied source range.

``scan_png_random_access(...)``, ``scan_webp_random_access(...)``,
``scan_gif_random_access(...)``,
``scan_jp2_random_access(...)``, ``scan_jxl_random_access(...)``, and
``scan_bmff_random_access(...)`` provide the same descriptor contract. PNG text
prefixes are scanned incrementally. GIF traversal reads extension framing and
sub-block lengths while skipping raster sub-block contents. JP2/JXL/BMFF
traversal reads structural boxes and metadata tables while skipping image
codestream, ``mdat``, and unrelated payload bytes. These six scanners require a
32-byte minimum window; larger windows reduce callback traffic.

``decode_exr_header_random_access(...)`` traverses EXR attributes and stops at
the header terminator without reading chunk tables or pixel data. Structural
reads use caller-owned read-window storage. The largest selected attribute value
must fit caller value scratch; ``value_scratch_needed`` reports its exact size.
``measure_exr_header_random_access(...)`` validates the structure without
fetching attribute bodies.

``extract_payload_random_access(...)`` fetches one discovered logical metadata
stream. It supports direct ranges, GIF sub-blocks, multipart JPEG ICC and
extended XMP, general multipart blocks, and bounded Deflate/Brotli
decompression through caller-owned compressed and output storage.

Explicitly typed RAF, X3F, and CRW sources use native positional readers. RAF
reads its fixed header and declared native directories, X3F reads its header,
section directory, and ``PROP`` sections, and CRW follows CIFF directory
offsets and individual values. These paths do not read intervening image
payload ranges. Optional RAF preview/FujiIFD enrichment and X3F section-JPEG
enrichment remain explicit residuals when requested.

Callback example
----------------

.. code-block:: cpp

   #include "openmeta/random_access_source.h"

   struct HostReader {
       // Host-owned file, range, asset, or proxy state.
   };

   openmeta::RandomAccessIoResult
   host_read_at(void* context, uint64_t offset,
                std::span<std::byte> destination) noexcept
   {
       HostReader* reader = static_cast<HostReader*>(context);
       return {/* code */, /* bytes_read */};
   }

   HostReader reader;
   openmeta::RandomAccessSource source =
       openmeta::make_callback_random_access_source(
           source_size, &reader, host_read_at, true);

   openmeta::RandomAccessReadLimits limits;
   limits.max_requests          = 4096;
   limits.max_total_bytes       = 32ULL * 1024ULL * 1024ULL;
   limits.max_single_read_bytes = 4ULL * 1024ULL * 1024ULL;

   openmeta::RandomAccessReadState state;
   std::array<std::byte, 16> header;
   openmeta::RandomAccessReadCode code =
       openmeta::random_access_read_exact(source, 0, header, &state, limits);

``random_access_read_exact(...)`` does not retry a short read. A host using a
partial-read transport must complete retries inside the callback or return the
actual shorter count. OpenMeta performs range and budget checks before invoking
the callback.

TIFF/DNG decode
---------------

.. code-block:: cpp

   std::array<std::byte, 16 * 1024> structural_window;
   std::array<std::byte, 1 * 1024 * 1024> value_scratch;

   openmeta::ExifRandomAccessScratch scratch;
   scratch.read_window = structural_window;
   scratch.value = value_scratch;
   scratch.window_options.minimum_read_bytes = structural_window.size();

   openmeta::RandomAccessSourceRange range =
       openmeta::make_random_access_source_range(
           source, tiff_offset, tiff_size);
   openmeta::MetaStore store;
   std::array<openmeta::ExifIfdRef, 128> ifds;
   openmeta::ExifRandomAccessDecodeResult result =
       openmeta::decode_exif_tiff_random_access(
           range, store, ifds, scratch,
           openmeta::ExifDecodeOptions{}, limits);

The structural window must hold at least 20 bytes for BigTIFF. Larger windows
batch adjacent reads. The value scratch holds one out-of-line metadata value or
the combined GeoTIFF parameter payloads. ``value_scratch_needed`` reports an
insufficient buffer without hidden allocation.

Container scan
--------------

.. code-block:: cpp

   #include "openmeta/container_scan.h"

   std::array<std::byte, 512> scan_window;
   openmeta::ContainerRandomAccessScratch scan_scratch;
   scan_scratch.read_window = scan_window;
   scan_scratch.window_options.minimum_read_bytes = scan_window.size();

   openmeta::RandomAccessSourceRange jpeg =
       openmeta::make_random_access_source_range(
           source, jpeg_offset, jpeg_size);
   std::array<openmeta::ContainerBlockRef, 32> blocks;
   openmeta::ContainerRandomAccessScanResult scan =
       openmeta::scan_jpeg_random_access(
           jpeg, blocks, scan_scratch, limits);

   if (!scan.complete()) {
       // Inspect scan.input for source, scratch, or resource-limit failure.
   }
   if (scan.scan.status != openmeta::ScanStatus::Ok) {
       // Source I/O completed, but the JPEG is unsupported or malformed.
   }

The 512-byte window preserves bare APP1 XMP detection parity. Smaller windows
work when every required probe fits and otherwise return ``ScratchTooSmall``.
``measure_scan_jpeg_random_access(...)`` reports ``scan.needed`` without block
output storage.

PNG, WebP, GIF, JP2, JXL, and ISO-BMFF use the corresponding
``scan_*_random_access(...)`` and ``measure_scan_*_random_access(...)`` pairs.
They require at least 32 bytes of read-window storage. A 4 KiB or larger window
is generally a better host default for table-heavy BMFF files.

Bounded payload and snapshot assembly
-------------------------------------

.. code-block:: cpp

   #include "openmeta/metadata_transfer.h"

   std::array<openmeta::ContainerBlockRef, 64> blocks;
   std::array<openmeta::ExifIfdRef, 128> ifds;
   std::array<uint32_t, 64> payload_indices;
   std::array<std::byte, 4 * 1024> read_window;
   std::array<std::byte, 2 * 1024 * 1024> payload;
   std::array<std::byte, 2 * 1024 * 1024> compressed_payload;
   std::array<std::byte, 1 * 1024 * 1024> value;

   openmeta::ReadTransferSourceSnapshotRandomAccessScratch scratch;
   scratch.blocks             = blocks;
   scratch.ifds               = ifds;
   scratch.payload_indices    = payload_indices;
   scratch.read_window        = read_window;
   scratch.payload            = payload;
   scratch.compressed_payload = compressed_payload;
   scratch.value              = value;

   openmeta::ReadTransferSourceSnapshotRandomAccessResult result =
       openmeta::read_transfer_source_snapshot_random_access(
           source_range, openmeta::ContainerFormat::Jpeg,
           scratch, {}, limits);

The scanner, payload, decompression, and value workspaces are caller-owned and
valid only for the call. The returned ``TransferSourceSnapshot`` owns its
finalized ``MetaStore``, so those workspaces may be released or reused afterward.
Request-count and byte ceilings are cumulative across scan, payload, decode,
PNG text-prefix, and optional raw-carrier reads.

The high-level positional path supports JPEG, PNG, WebP, GIF, JP2, JXL,
HEIF/AVIF/CR3 BMFF containers, native TIFF/DNG-family input, EXR headers, and
native RAF, X3F, and CRW metadata. Requested RAF/X3F embedded recursion,
selected source-wide BMFF enrichment, unsupported MakerNote subpaths, and
whole-file raw-carrier preservation increment
``residual_metadata_paths``. The decoded snapshot remains usable, but
``complete()`` returns false.

Project machine-readable diagnostics without allocation:

.. code-block:: cpp

   std::array<openmeta::ReadTransferSourceDiagnostic, 8> diagnostics;
   openmeta::ReadTransferSourceDiagnosticOptions diagnostic_options;
   diagnostic_options.decode_makernote_requested = options.decode_makernote;
   diagnostic_options.decode_embedded_containers_requested =
       options.decode_embedded_containers;

   openmeta::ReadTransferSourceDiagnosticsResult diagnostic_result =
       openmeta::collect_read_transfer_source_diagnostics(
           result, diagnostics, diagnostic_options);

Each record has severity, stable code, domain, format, source offset, required
byte count, item count, EXIF/native tag, and original input failure code where
available. Name and short-message helpers return static strings. If ``written``
differs from ``needed``, resize the caller buffer and project again; the
snapshot read itself is not repeated.

Raw-carrier preservation is opt-in and copies only discovered carrier bytes
within ``max_raw_carrier_bytes``. Ordinary decoded snapshot assembly does not
retain the whole source, and transfer still uses the normal safety policy and
decoded re-emission rules.

Real-time and concurrent use
----------------------------

The positional source, scanner, decoder, and payload layers add no global state,
virtual dispatch, ``std::function``, hidden allocation, file-position mutation,
locking, or background work. The high-level snapshot result intentionally owns
its ``MetaStore`` and optional raw-carrier bytes. The callback is a plain
function pointer, and all counters belong to operation-local state.

OpenMeta does not issue concurrent callbacks within one synchronous scan or
decode.
Separate operations may share an immutable source when ``concurrent_reads`` is
true and the host context supports concurrent positional reads. Each operation
must use separate decoder scratch and accounting state.

The TIFF/DNG decoder batches adjacent structural reads through caller-owned
scratch and keeps out-of-line values in separate caller storage so they do not
evict the structural cache. Pixel and media payloads are outside this metadata
contract and are not read by positional container scanning.

Source lifetime
---------------

``RandomAccessSource`` is borrowed. Backing memory, callback, and context must
remain valid and immutable for the complete operation. If a callback detects
replacement, truncation, or another size-changing mutation, it must return
``RandomAccessIoCode::SourceChanged``.

Use the memory descriptor when the host already owns the complete encoded asset.
Use the callback descriptor for file handles, range-backed storage, host asset
APIs, and I/O proxy objects.
