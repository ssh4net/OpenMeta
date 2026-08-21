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
scanning. Version 0.4.108 adds positional PNG/WebP chunk scanning and bounded
JP2/JXL/ISO-BMFF box and metadata-item traversal. Version 0.4.109 adds
positional GIF extension scanning, EXR header traversal, logical metadata
payload extraction, and decoded source-snapshot assembly. Version 0.4.111 adds
native RAF, X3F, and CRW/CIFF positional metadata traversal plus structured
snapshot-read diagnostics. Version 0.4.112 adds bounded RAF preview-JPEG and
FujiIFD traversal plus X3F section-JPEG traversal. The contract now defines:

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

`scan_png_random_access(...)`, `scan_webp_random_access(...)`,
`scan_gif_random_access(...)`,
`scan_jp2_random_access(...)`, `scan_jxl_random_access(...)`, and
`scan_bmff_random_access(...)` provide the same descriptor contract. PNG text
prefixes are scanned incrementally. GIF traversal reads extension framing and
sub-block lengths but skips raster sub-block contents. JP2/JXL/BMFF traversal
reads box headers, brands, item-location tables, item references, ICC
properties, and CR3 metadata wrappers as needed, but skips image codestream,
`mdat`, and unrelated payload bytes. A 32-byte window is the minimum for these
six scanners; larger windows reduce callback traffic.

`decode_exr_header_random_access(...)` traverses EXR attributes and stops at the
header terminator without reading chunk tables or pixel data. Structural reads
use `ExrRandomAccessScratch::read_window`; the largest attribute value selected
for decode must fit `value`, or `value_scratch_needed` reports its exact size.
`measure_exr_header_random_access(...)` validates names, types, counts, and
ranges without fetching attribute bodies.

`extract_payload_random_access(...)` fetches one discovered logical metadata
stream. It supports direct ranges, GIF sub-blocks, multipart JPEG ICC and
extended XMP, general multipart blocks, and bounded Deflate/Brotli
decompression. Compressed input uses caller-owned compressed scratch before
decompression into the caller payload buffer.

Explicitly typed RAF, X3F, and CRW sources now use native positional readers.
RAF reads its fixed header and declared native directories, X3F reads its
header, section directory, and `PROP` sections, and CRW follows CIFF directory
offsets and individual values. These paths do not read intervening image
payload ranges. When embedded-container decoding is requested,
`scan_raf_random_access(...)` follows the header-declared preview JPEG and
FujiIFD/TIFF range, while `scan_x3f_random_access(...)` follows declared
`IMA2`/`IMAG` `SECi` JPEG sections. Both reuse bounded JPEG metadata scanning
and stop at Start of Scan without fetching entropy-coded image data.

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

## Container Scan Example

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

For PNG, WebP, GIF, JP2, JXL, and ISO-BMFF sources, call the corresponding
`scan_*_random_access(...)` or `measure_scan_*_random_access(...)` function with
the same source-range and result pattern. These scanners require at least 32
bytes of read-window storage. A 4 KiB or larger read-ahead window is generally a
better host default for table-heavy BMFF files.

## Bounded Payload And Snapshot Assembly

```cpp
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
        source_range, openmeta::ContainerFormat::Jpeg, scratch, {}, limits);
```

The scanner, payload, decompression, and value workspaces are caller-owned and
valid only for the call. The returned `TransferSourceSnapshot` owns its
finalized `MetaStore`, so the host may release or reuse those workspaces after
the call. Request-count and byte ceilings are cumulative across scan, payload,
decode, PNG text-prefix, and optional raw-carrier reads; they do not reset at
phase boundaries.

The current high-level positional path supports JPEG, PNG, WebP, GIF, JP2,
JXL, HEIF/AVIF/CR3 BMFF containers, native TIFF/DNG-family input, EXR headers,
and native RAF, X3F, and CRW metadata. Requested RAF/X3F embedded-container
decoding follows declared preview, FujiIFD, and section-JPEG ranges. It does
not search arbitrary image bytes for undeclared fallback signatures. A missing
declared lane that would require such a fallback, selected source-wide BMFF
enrichment, unsupported MakerNote subpaths, and whole-file raw-carrier
preservation increment `residual_metadata_paths`. The decoded snapshot remains
usable, but `complete()` returns false. Hosts must inspect the result rather
than treating a usable partial snapshot as full positional parity.

Project machine-readable diagnostics without allocation:

```cpp
std::array<openmeta::ReadTransferSourceDiagnostic, 8> diagnostics;
openmeta::ReadTransferSourceDiagnosticOptions diagnostic_options;
diagnostic_options.decode_makernote_requested = options.decode_makernote;
diagnostic_options.decode_embedded_containers_requested =
    options.decode_embedded_containers;

openmeta::ReadTransferSourceDiagnosticsResult diagnostic_result =
    openmeta::collect_read_transfer_source_diagnostics(
        result, diagnostics, diagnostic_options);
```

Each record has severity, stable code, domain, format, source offset, required
byte count, item count, EXIF/native tag, and the original input failure code
where available. Name and short-message helpers are static strings. If
`written != needed`, resize the caller buffer and project again; the snapshot
read itself is not repeated.

Raw-carrier preservation is opt-in and copies only discovered carrier bytes
within `max_raw_carrier_bytes`; ordinary decoded snapshot assembly does not
retain the whole source. Transfer preparation continues to apply normal safety
policy and decoded re-emission rules.

## Real-Time And Concurrent Use

The positional source, scanner, decoder, and payload layers add no global state,
virtual dispatch, `std::function`, hidden allocation, file-position mutation,
locking, or background work. The high-level snapshot result intentionally owns
its `MetaStore` and optional raw-carrier bytes. The callback is a plain function
pointer, and all counters belong to operation-local state.

OpenMeta will not issue concurrent callback calls within one synchronous scan or decode.
Separate operations may share an immutable source when `concurrent_reads` is
true and the host context actually supports concurrent positional reads. Each
operation must use separate decoder scratch and `RandomAccessReadState` objects.

The TIFF/DNG decoder batches adjacent structural reads through the caller-owned
window instead of issuing one host callback for every scalar. Out-of-line
metadata values use caller value scratch so they do not evict the structural
cache. Pixel and media payloads are outside this metadata input contract and
are not fetched as part of ordinary metadata decoding or positional container
scanning.

## Source Lifetime

`RandomAccessSource` is borrowed. The backing memory, callback, and context must
remain valid and immutable for the whole operation. The declared size is a
snapshot of the source extent. If a callback detects replacement, truncation,
or another size-changing mutation, it must return
`RandomAccessIoCode::SourceChanged`.

The memory descriptor is suitable when a realtime pipeline already has the
complete encoded asset in a stable buffer. The callback descriptor is suitable
for file handles, range-backed storage, host asset APIs, and I/O proxy objects.
