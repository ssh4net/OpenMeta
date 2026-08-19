# Random-Access Input

OpenMeta's random-access input contract is designed for image-processing and
transcoding hosts that already own storage, scheduling, and I/O policy. It is
not tied to a particular image library or file abstraction.

## Current Scope

Version 0.4.100 adds the allocation-free source primitive in
`openmeta/random_access_source.h`. It defines:

- a fixed source size and synchronous `read_at(offset, destination)` callback
- a non-owning descriptor for caller-owned contiguous memory
- exact-read semantics with distinct short-read, I/O, cancellation, and
  source-change results
- per-operation limits for request count, total requested bytes, and one read
- independent sticky accounting state with first-failure diagnostics

This release establishes the I/O contract only. The existing container and
metadata decoders still accept full byte spans or mapped files. TIFF/DNG is the
first planned decoder conversion; a format is not random-access capable until
its scanner, payload extraction, nested metadata, and parity tests use this
contract without materializing the complete source.

## Callback Example

```cpp
#include "openmeta/random_access_source.h"

struct HostReader {
    // File descriptor, range client, asset reader, or another host-owned object.
};

openmeta::RandomAccessIoResult
host_read_at(void* context, uint64_t offset,
             std::span<std::byte> destination) noexcept
{
    HostReader* reader = static_cast<HostReader*>(context);
    // Perform one synchronous positional read without changing shared position.
    // Return the actual count. Report SourceChanged if the captured size is no
    // longer valid.
    return {/* code */, /* bytes_read */};
}

HostReader reader;
openmeta::RandomAccessSource source =
    openmeta::make_callback_random_access_source(
        source_size, &reader, host_read_at, /* concurrent_reads */ true);

openmeta::RandomAccessReadLimits limits;
limits.max_requests          = 4096;
limits.max_total_bytes       = 32ULL * 1024ULL * 1024ULL;
limits.max_single_read_bytes = 4ULL * 1024ULL * 1024ULL;

openmeta::RandomAccessReadState state;
std::array<std::byte, 16> header;
openmeta::RandomAccessReadCode code = openmeta::random_access_read_exact(
    source, 0, header, &state, limits);
```

`random_access_read_exact(...)` does not retry a short read. A host that uses a
transport with partial-read semantics must complete its retry loop inside the
callback or return the actual shorter count. OpenMeta performs range and budget
checks before invoking the callback.

## Real-Time And Concurrent Use

The contract adds no global state, virtual dispatch, `std::function`, hidden
allocation, file-position mutation, locking, or background work. The callback
is a plain function pointer, and all counters belong to the caller-provided
`RandomAccessReadState`.

OpenMeta will not issue concurrent callback calls within one synchronous decode.
Separate operations may share an immutable source when `concurrent_reads` is
true and the host context actually supports concurrent positional reads. Each
operation must use separate decoder scratch and `RandomAccessReadState` objects.

Decoder conversions will batch adjacent structural reads through caller-owned
scratch windows instead of issuing one host callback for every TIFF scalar.
Large metadata values will be fetched only after their type, count, range, and
resource budget have been validated. Pixel payloads are outside this metadata
input contract and must not be fetched as part of ordinary metadata decoding.

## Source Lifetime

`RandomAccessSource` is borrowed. The backing memory, callback, and context must
remain valid and immutable for the whole operation. The declared size is a
snapshot of the source extent. If a callback detects replacement, truncation,
or another size-changing mutation, it must return
`RandomAccessIoCode::SourceChanged`.

The memory descriptor is suitable when a realtime pipeline already has the
complete encoded asset in a stable buffer. The callback descriptor is suitable
for file handles, range-backed storage, host asset APIs, and I/O proxy objects.
