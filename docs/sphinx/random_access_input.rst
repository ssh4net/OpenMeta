Random-Access Input
===================

OpenMeta's random-access input contract is designed for image-processing and
transcoding hosts that already own storage, scheduling, and I/O policy. It is
not tied to a particular image library or file abstraction.

Current scope
-------------

Version 0.4.100 added the allocation-free source primitive in
``openmeta/random_access_source.h``. Version 0.4.101 adds source ranges,
caller-owned read windows, and the first decoder conversion. It provides:

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
also decoded from caller scratch.

Several vendor MakerNotes use outer-TIFF-relative offsets. Callback decoding
reports these through ``nested_payloads_skipped`` and ``complete()`` rather than
decoding against an incorrect subspan. Contiguous sources retain the complete
existing nested-decoder behavior.

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

Real-time and concurrent use
----------------------------

The contract adds no global state, virtual dispatch, ``std::function``, hidden
allocation, file-position mutation, locking, or background work. The callback
is a plain function pointer, and all counters belong to the caller-provided
``RandomAccessReadState``.

OpenMeta does not issue concurrent callbacks within one synchronous decode.
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
