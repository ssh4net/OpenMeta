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
and FLIR callback parity. Version 0.4.107 adds bounded JPEG segment scanning. It
provides:

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

JPEG segment scan
-----------------

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

Real-time and concurrent use
----------------------------

The contract adds no global state, virtual dispatch, ``std::function``, hidden
allocation, file-position mutation, locking, or background work. The callback
is a plain function pointer, and all counters belong to the caller-provided
``RandomAccessReadState``.

OpenMeta does not issue concurrent callbacks within one synchronous scan or
decode.
Separate operations may share an immutable source when ``concurrent_reads`` is
true and the host context supports concurrent positional reads. Each operation
must use separate decoder scratch and accounting state.

The TIFF/DNG decoder batches adjacent structural reads through caller-owned
scratch and keeps out-of-line values in separate caller storage so they do not
evict the structural cache. Pixel payloads are outside this metadata contract.

Source lifetime
---------------

``RandomAccessSource`` is borrowed. Backing memory, callback, and context must
remain valid and immutable for the complete operation. If a callback detects
replacement, truncation, or another size-changing mutation, it must return
``RandomAccessIoCode::SourceChanged``.

Use the memory descriptor when the host already owns the complete encoded asset.
Use the callback descriptor for file handles, range-backed storage, host asset
APIs, and I/O proxy objects.
