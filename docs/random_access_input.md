# Random-Access Input

OpenMeta's random-access input contract is designed for image-processing and
transcoding hosts that already own storage, scheduling, and I/O policy. It is
not tied to a particular image library or file abstraction.

## Current Scope

Version 0.4.100 added the allocation-free source primitive in
`openmeta/random_access_source.h`. Version 0.4.101 added source-relative ranges,
caller-owned read windows, and the first decoder conversion. Version 0.4.102
added source-backed nested TIFF offset resolution. Version 0.4.103 extends that
conversion to Olympus, Panasonic, and Samsung MakerNotes. Version 0.4.104 adds
bounded Fujifilm and General Imaging MakerNote source layouts. Version 0.4.105
adds Kodak fixed-layout and outer-TIFF-relative MakerNotes. Version 0.4.106
adds Ricoh mixed-base and vendor subdirectory decoding plus Nintendo, Casio,
Minolta, and FLIR callback parity. Version 0.4.107 adds bounded JPEG segment
scanning. The contract now defines:

- a fixed source size and synchronous `read_at(offset, destination)` callback
- a non-owning descriptor for caller-owned contiguous memory
- exact-read semantics with distinct short-read, I/O, cancellation, and
  source-change results
- per-operation limits for request count, total requested bytes, and one read
- independent sticky accounting state with first-failure diagnostics
- borrowed source subranges without copying or changing callback offsets
- caller-owned read-ahead windows with direct zero-copy views for memory sources

`decode_exif_tiff_random_access(...)` now traverses classic TIFF, BigTIFF, DNG,
Panasonic RW2, and Olympus ORF headers, IFDs, pointer directories, and validated
metadata values without materializing the complete source. It also decodes
PrintIM, GeoTIFF, Pentax DNG private data, and selected self-contained
MakerNotes from caller scratch. Nikon embedded TIFF and type 1 MakerNotes and
Sony outer-TIFF-relative IFDs now read through the same bounded source. Canon
uses checked absolute, MakerNote-relative, and signed adjusted-base candidates.
Legacy Olympus values and nested sub-IFDs use outer-TIFF offsets, modern
Olympus/OM System notes retain MakerNote-relative recursion, Panasonic retains
its binary-table expansion, and self-contained Samsung STMN and Type2 notes
retain their derived tables. Fujifilm `FUJIFILM`/`GENERALE` notes use bounded
MakerNote-relative subranges, while General Imaging Type 2 notes use checked
source windows without copying or patching bytes. Kodak fixed-layout records
and Type 8, Type 10, and Type 11 IFDs use checked outer-TIFF offsets, including
pointer and embedded vendor subtables. Ricoh classic notes use per-entry checked
offset candidates and retain ImageInfo, CameraInfo, FaceInfo, SerialInfo, and
Theta expansion. Nintendo CameraInfo, Casio QVC/DCI directories, Minolta binary
tables, and FLIR outer-TIFF-relative values also retain their contiguous decode
behavior.

Callback decoding does not yet claim complete MakerNote parity. Canon retains
its complete existing derived-table decoder when required values are inside the
declared MakerNote payload. If Canon values extend outside that payload, the
main IFD is decoded from the source but the unconverted derived-table work is
counted by `ExifRandomAccessDecodeResult::nested_payloads_skipped`. Unknown or
unsupported vendor layouts use the same residual instead of decoding against an
incorrect subspan. A contiguous source keeps the full existing decoder behavior
and reports no random-access residual.

`scan_jpeg_random_access(...)` locates leading EXIF, XMP, ICC, MPF, vendor APP,
JUMBF, Photoshop IRB, FLIR, and comment segments through the same positional
source. It reads at most 512 bytes from a metadata segment for classification,
preserves multipart APP11 normalization, and stops at Start of Scan without
reading entropy-coded image data. Returned offsets are relative to the supplied
source range.

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

## TIFF/DNG Decode Example

```cpp
#include "openmeta/exif_tiff_decode.h"

std::array<std::byte, 16 * 1024> structural_window;
std::array<std::byte, 1 * 1024 * 1024> value_scratch;

openmeta::ExifRandomAccessScratch scratch;
scratch.read_window = structural_window;
scratch.value = value_scratch;
scratch.window_options.minimum_read_bytes = structural_window.size();

openmeta::RandomAccessSourceRange tiff =
    openmeta::make_random_access_source_range(source, tiff_offset, tiff_size);
openmeta::MetaStore store;
std::array<openmeta::ExifIfdRef, 128> ifds;

openmeta::ExifRandomAccessDecodeResult result =
    openmeta::decode_exif_tiff_random_access(
        tiff, store, ifds, scratch, openmeta::ExifDecodeOptions{}, limits);
```

`read_window` must hold 20 bytes for BigTIFF entries. Larger windows batch
adjacent structural reads. `value` must hold the largest metadata value that
the caller wants decoded and, when GeoTIFF is enabled, the combined GeoTIFF key,
double, and ASCII parameter payloads. If it is too small,
`value_scratch_needed` reports the required byte count; OpenMeta does not
allocate a replacement.

## JPEG Segment Scan Example

```cpp
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
    // Inspect scan.input for short read, I/O, source change, scratch, or limit.
}
if (scan.scan.status != openmeta::ScanStatus::Ok) {
    // The source was read successfully but is unsupported or malformed.
}
```

The 512-byte window is the maximum JPEG classification probe and preserves bare
APP1 XMP detection parity. Smaller windows remain valid for inputs whose
metadata probes fit, but return `ScratchTooSmall` when a larger probe is needed.
Use `measure_scan_jpeg_random_access(...)` with the same scratch and limits to
obtain `scan.needed` without block output storage.

## Real-Time And Concurrent Use

The contract adds no global state, virtual dispatch, `std::function`, hidden
allocation, file-position mutation, locking, or background work. The callback
is a plain function pointer, and all counters belong to the caller-provided
`RandomAccessReadState`.

OpenMeta will not issue concurrent callback calls within one synchronous scan or decode.
Separate operations may share an immutable source when `concurrent_reads` is
true and the host context actually supports concurrent positional reads. Each
operation must use separate decoder scratch and `RandomAccessReadState` objects.

The TIFF/DNG decoder batches adjacent structural reads through the caller-owned
window instead of issuing one host callback for every scalar. Out-of-line
metadata values use caller value scratch so they do not evict the structural
cache. Pixel payloads are outside this metadata input contract and are not
fetched as part of ordinary metadata decoding.

## Source Lifetime

`RandomAccessSource` is borrowed. The backing memory, callback, and context must
remain valid and immutable for the whole operation. The declared size is a
snapshot of the source extent. If a callback detects replacement, truncation,
or another size-changing mutation, it must return
`RandomAccessIoCode::SourceChanged`.

The memory descriptor is suitable when a realtime pipeline already has the
complete encoded asset in a stable buffer. The callback descriptor is suitable
for file handles, range-backed storage, host asset APIs, and I/O proxy objects.
