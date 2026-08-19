Random-Access Input
===================

OpenMeta's random-access input contract is designed for image-processing and
transcoding hosts that already own storage, scheduling, and I/O policy. It is
not tied to a particular image library or file abstraction.

Current scope
-------------

Version 0.4.100 adds the allocation-free source primitive in
``openmeta/random_access_source.h``. It provides:

- a fixed source size and synchronous ``read_at(offset, destination)`` callback
- a non-owning descriptor for caller-owned contiguous memory
- distinct exact-read, short-read, I/O, cancellation, and source-change results
- per-operation request-count, total-byte, and single-read ceilings
- independent sticky accounting state with first-failure diagnostics

This release establishes the I/O contract only. Existing container and metadata
decoders still accept full byte spans or mapped files. TIFF/DNG is the first
planned decoder conversion. A format is not random-access capable until its
scanner, payload extraction, nested metadata, and parity tests use the contract
without materializing the complete source.

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

Decoder conversions will batch adjacent structural reads through caller-owned
scratch windows instead of issuing one callback for every TIFF scalar. Metadata
values will be fetched only after type, count, range, and resource validation.
Pixel payloads are outside this metadata input contract.

Source lifetime
---------------

``RandomAccessSource`` is borrowed. Backing memory, callback, and context must
remain valid and immutable for the complete operation. If a callback detects
replacement, truncation, or another size-changing mutation, it must return
``RandomAccessIoCode::SourceChanged``.

Use the memory descriptor when the host already owns the complete encoded asset.
Use the callback descriptor for file handles, range-backed storage, host asset
APIs, and I/O proxy objects.
