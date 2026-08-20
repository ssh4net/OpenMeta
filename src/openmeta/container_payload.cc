// SPDX-License-Identifier: Apache-2.0

#include "openmeta/container_payload.h"

#include <array>
#include <cstring>

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
#    include <zlib.h>
#endif

#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI
#    include <brotli/decode.h>
#endif

namespace openmeta {
namespace {

    static uint8_t u8(std::byte b) noexcept { return static_cast<uint8_t>(b); }

    static bool validate_range(std::span<const std::byte> bytes,
                               uint64_t offset, uint64_t size) noexcept
    {
        const uint64_t bytes_size = static_cast<uint64_t>(bytes.size());
        if (offset > bytes_size) {
            return false;
        }
        const uint64_t cap = bytes_size - offset;
        return size <= cap;
    }


    static uint32_t safe_u32(uint64_t v) noexcept
    {
        return (v > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : static_cast<uint32_t>(v);
    }


    static void insertion_sort_by_part_index(
        std::span<uint32_t> indices,
        std::span<const ContainerBlockRef> blocks) noexcept
    {
        for (size_t i = 1; i < indices.size(); ++i) {
            const uint32_t key = indices[i];
            const uint32_t key_part
                = blocks[key].part_index;  // stable, no bounds checks here.
            size_t j = i;
            while (j > 0) {
                const uint32_t prev = indices[j - 1];
                if (blocks[prev].part_index <= key_part) {
                    break;
                }
                indices[j] = prev;
                j -= 1;
            }
            indices[j] = key;
        }
    }


    static void insertion_sort_by_logical_offset(
        std::span<uint32_t> indices,
        std::span<const ContainerBlockRef> blocks) noexcept
    {
        for (size_t i = 1; i < indices.size(); ++i) {
            const uint32_t key     = indices[i];
            const uint64_t key_off = blocks[key].logical_offset;
            size_t j               = i;
            while (j > 0) {
                const uint32_t prev = indices[j - 1];
                if (blocks[prev].logical_offset <= key_off) {
                    break;
                }
                indices[j] = prev;
                j -= 1;
            }
            indices[j] = key;
        }
    }


    static void copy_bytes(std::span<std::byte> dst, uint64_t dst_off,
                           std::span<const std::byte> src,
                           uint64_t* io_written) noexcept
    {
        const uint64_t dst_size = static_cast<uint64_t>(dst.size());
        if (dst_off >= dst_size) {
            return;
        }
        const uint64_t room = dst_size - dst_off;
        const uint64_t n    = (static_cast<uint64_t>(src.size()) < room)
                                  ? static_cast<uint64_t>(src.size())
                                  : room;
        if (n == 0) {
            return;
        }
        std::memcpy(dst.data() + static_cast<size_t>(dst_off), src.data(),
                    static_cast<size_t>(n));
        if (io_written) {
            *io_written += n;
        }
    }


    static PayloadResult
    extract_gif_subblocks(std::span<const std::byte> bytes,
                          std::span<std::byte> out,
                          const PayloadOptions& options) noexcept
    {
        PayloadResult res;
        uint64_t needed  = 0;
        uint64_t written = 0;

        const uint64_t max_out = options.limits.max_output_bytes;
        uint64_t p             = 0;
        while (p < bytes.size()) {
            const uint8_t sub = u8(bytes[static_cast<size_t>(p)]);
            p += 1;
            if (sub == 0) {
                break;
            }
            if (p > bytes.size() || sub > bytes.size() - p) {
                res.status = PayloadStatus::Malformed;
                return res;
            }

            needed += sub;
            if (max_out != 0U && needed > max_out) {
                res.status  = PayloadStatus::LimitExceeded;
                res.needed  = needed;
                res.written = written;
                return res;
            }

            const std::span<const std::byte> part
                = bytes.subspan(static_cast<size_t>(p),
                                static_cast<size_t>(sub));
            copy_bytes(out, written, part, &written);
            p += sub;
        }

        res.needed  = needed;
        res.written = written;
        if (written < needed) {
            res.status = PayloadStatus::OutputTruncated;
        }
        return res;
    }

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
    static PayloadResult inflate_zlib(std::span<const std::byte> in,
                                      std::span<std::byte> out,
                                      const PayloadOptions& options) noexcept
    {
        PayloadResult res;

        z_stream strm {};
        strm.zalloc = Z_NULL;
        strm.zfree  = Z_NULL;
        strm.opaque = Z_NULL;

        int ret = inflateInit(&strm);
        if (ret != Z_OK) {
            res.status = PayloadStatus::Malformed;
            return res;
        }

        std::array<std::byte, 32768> discard {};

        uint64_t in_off   = 0;
        uint64_t written  = 0;
        uint64_t produced = 0;

        const uint64_t max_out = options.limits.max_output_bytes;

        for (;;) {
            if (strm.avail_in == 0) {
                if (in_off >= in.size()) {
                    (void)inflateEnd(&strm);
                    res.status  = PayloadStatus::Malformed;
                    res.written = written;
                    res.needed  = produced;
                    return res;
                }
                const uint64_t remaining = static_cast<uint64_t>(in.size())
                                           - in_off;
                const uint64_t chunk64
                    = (remaining < static_cast<uint64_t>(0xFFFFFFFFU))
                          ? remaining
                          : static_cast<uint64_t>(0xFFFFFFFFU);
                const uint32_t chunk = safe_u32(chunk64);
                strm.next_in  = reinterpret_cast<Bytef*>(const_cast<std::byte*>(
                    in.data() + static_cast<size_t>(in_off)));
                strm.avail_in = static_cast<uInt>(chunk);
                in_off += chunk;
            }

            std::byte* out_ptr = nullptr;
            uint64_t out_room  = 0;
            if (written < out.size()) {
                out_ptr  = out.data() + static_cast<size_t>(written);
                out_room = static_cast<uint64_t>(out.size()) - written;
            } else {
                out_ptr  = discard.data();
                out_room = discard.size();
            }
            const uint64_t out_chunk64
                = (out_room < static_cast<uint64_t>(0xFFFFFFFFU))
                      ? out_room
                      : static_cast<uint64_t>(0xFFFFFFFFU);
            const uint32_t out_chunk = safe_u32(out_chunk64);
            strm.next_out            = reinterpret_cast<Bytef*>(
                reinterpret_cast<void*>(out_ptr));
            strm.avail_out = static_cast<uInt>(out_chunk);

            const uInt avail_before = strm.avail_out;
            ret                     = inflate(&strm, Z_NO_FLUSH);
            const uInt used_out     = avail_before - strm.avail_out;

            produced += used_out;
            if (written < out.size()) {
                written += used_out;
                if (written > out.size()) {
                    written = out.size();
                }
            }

            if (max_out != 0U && produced > max_out) {
                (void)inflateEnd(&strm);
                res.status  = PayloadStatus::LimitExceeded;
                res.written = written;
                res.needed  = produced;
                return res;
            }

            if (ret == Z_STREAM_END) {
                break;
            }
            if (ret != Z_OK) {
                (void)inflateEnd(&strm);
                res.status  = PayloadStatus::Malformed;
                res.written = written;
                res.needed  = produced;
                return res;
            }
        }

        (void)inflateEnd(&strm);
        res.written = written;
        res.needed  = produced;
        if (written < produced) {
            res.status = PayloadStatus::OutputTruncated;
        }
        return res;
    }
#endif

#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI
    static PayloadResult
    brotli_decompress(std::span<const std::byte> in, std::span<std::byte> out,
                      const PayloadOptions& options) noexcept
    {
        PayloadResult res;

        BrotliDecoderState* st = BrotliDecoderCreateInstance(nullptr, nullptr,
                                                             nullptr);
        if (!st) {
            res.status = PayloadStatus::LimitExceeded;
            return res;
        }

        std::array<std::byte, 32768> discard {};

        const uint8_t* next_in = reinterpret_cast<const uint8_t*>(in.data());
        size_t avail_in        = in.size();

        uint64_t written  = 0;
        uint64_t produced = 0;

        const uint64_t max_out = options.limits.max_output_bytes;

        for (;;) {
            uint8_t* next_out = nullptr;
            size_t avail_out  = 0;
            if (written < out.size()) {
                next_out = reinterpret_cast<uint8_t*>(
                    out.data() + static_cast<size_t>(written));
                avail_out = out.size() - static_cast<size_t>(written);
            } else {
                next_out  = reinterpret_cast<uint8_t*>(discard.data());
                avail_out = discard.size();
            }
            const size_t avail_before = avail_out;
            const BrotliDecoderResult r
                = BrotliDecoderDecompressStream(st, &avail_in, &next_in,
                                                &avail_out, &next_out, nullptr);
            const size_t used_out = avail_before - avail_out;
            produced += used_out;
            if (written < out.size()) {
                written += used_out;
                if (written > out.size()) {
                    written = out.size();
                }
            }

            if (max_out != 0U && produced > max_out) {
                BrotliDecoderDestroyInstance(st);
                res.status  = PayloadStatus::LimitExceeded;
                res.written = written;
                res.needed  = produced;
                return res;
            }

            if (r == BROTLI_DECODER_RESULT_SUCCESS) {
                break;
            }
            if (r == BROTLI_DECODER_RESULT_ERROR) {
                BrotliDecoderDestroyInstance(st);
                res.status  = PayloadStatus::Malformed;
                res.written = written;
                res.needed  = produced;
                return res;
            }

            if (r == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
                if (avail_in == 0) {
                    BrotliDecoderDestroyInstance(st);
                    res.status  = PayloadStatus::Malformed;
                    res.written = written;
                    res.needed  = produced;
                    return res;
                }
            }
        }

        BrotliDecoderDestroyInstance(st);
        res.written = written;
        res.needed  = produced;
        if (written < produced) {
            res.status = PayloadStatus::OutputTruncated;
        }
        return res;
    }
#endif

    static PayloadResult extract_single_block(
        std::span<const std::byte> file_bytes, const ContainerBlockRef& block,
        std::span<std::byte> out, const PayloadOptions& options) noexcept
    {
        PayloadResult res;
        if (!validate_range(file_bytes, block.data_offset, block.data_size)) {
            res.status = PayloadStatus::Malformed;
            return res;
        }

        const std::span<const std::byte> src
            = file_bytes.subspan(static_cast<size_t>(block.data_offset),
                                 static_cast<size_t>(block.data_size));

        if (block.chunking == BlockChunking::GifSubBlocks) {
            return extract_gif_subblocks(src, out, options);
        }

        if (!options.decompress
            || block.compression == BlockCompression::None) {
            const uint64_t max_out = options.limits.max_output_bytes;
            if (max_out != 0U && block.data_size > max_out) {
                res.status  = PayloadStatus::LimitExceeded;
                res.needed  = block.data_size;
                res.written = 0;
                return res;
            }
            res.needed       = block.data_size;
            uint64_t written = 0;
            copy_bytes(out, 0, src, &written);
            res.written = written;
            if (written < block.data_size) {
                res.status = PayloadStatus::OutputTruncated;
            }
            return res;
        }

        if (block.compression == BlockCompression::Deflate) {
#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
            return inflate_zlib(src, out, options);
#else
            res.status = PayloadStatus::Unsupported;
            return res;
#endif
        }
        if (block.compression == BlockCompression::Brotli) {
#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI
            return brotli_decompress(src, out, options);
#else
            res.status = PayloadStatus::Unsupported;
            return res;
#endif
        }

        res.status = PayloadStatus::Unsupported;
        return res;
    }


    static bool blocks_match_jpeg_icc(const ContainerBlockRef& seed,
                                      const ContainerBlockRef& b) noexcept
    {
        if (b.format != seed.format || b.kind != seed.kind) {
            return false;
        }
        if (b.chunking != BlockChunking::JpegApp2SeqTotal) {
            return false;
        }
        if (seed.part_count != 0U && b.part_count != 0U
            && b.part_count != seed.part_count) {
            return false;
        }
        return true;
    }


    static bool blocks_match_jpeg_xmp_ext(const ContainerBlockRef& seed,
                                          const ContainerBlockRef& b) noexcept
    {
        if (b.format != seed.format || b.kind != seed.kind) {
            return false;
        }
        if (b.chunking != BlockChunking::JpegXmpExtendedGuidOffset) {
            return false;
        }
        if (b.group != seed.group) {
            return false;
        }
        if (seed.logical_size != 0U && b.logical_size != 0U
            && b.logical_size != seed.logical_size) {
            return false;
        }
        return true;
    }


    static bool blocks_match_multipart(const ContainerBlockRef& seed,
                                       const ContainerBlockRef& b) noexcept
    {
        if (b.format != seed.format || b.kind != seed.kind) {
            return false;
        }
        if (b.group != seed.group) {
            return false;
        }
        if (b.id != seed.id) {
            return false;
        }
        if (seed.part_count != 0U && b.part_count != 0U
            && b.part_count != seed.part_count) {
            return false;
        }
        return true;
    }


    static PayloadResult
    extract_concat_parts(std::span<const std::byte> file_bytes,
                         std::span<const ContainerBlockRef> blocks,
                         std::span<const uint32_t> part_indices,
                         std::span<std::byte> out,
                         const PayloadOptions& options) noexcept
    {
        PayloadResult res;

        uint64_t needed = 0;
        for (size_t i = 0; i < part_indices.size(); ++i) {
            const ContainerBlockRef& b = blocks[part_indices[i]];
            if (!validate_range(file_bytes, b.data_offset, b.data_size)) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
            needed += b.data_size;
            const uint64_t max_out = options.limits.max_output_bytes;
            if (max_out != 0U && needed > max_out) {
                res.status = PayloadStatus::LimitExceeded;
                res.needed = needed;
                return res;
            }
        }

        uint64_t written = 0;
        for (size_t i = 0; i < part_indices.size(); ++i) {
            const ContainerBlockRef& b = blocks[part_indices[i]];
            const std::span<const std::byte> src
                = file_bytes.subspan(static_cast<size_t>(b.data_offset),
                                     static_cast<size_t>(b.data_size));
            copy_bytes(out, written, src, &written);
        }

        res.needed  = needed;
        res.written = written;
        if (written < needed) {
            res.status = PayloadStatus::OutputTruncated;
        }
        return res;
    }


    static PayloadResult
    extract_offset_parts(std::span<const std::byte> file_bytes,
                         std::span<const ContainerBlockRef> blocks,
                         std::span<const uint32_t> part_indices,
                         uint64_t logical_size, std::span<std::byte> out,
                         const PayloadOptions& options) noexcept
    {
        PayloadResult res;

        if (logical_size == 0U) {
            res.status = PayloadStatus::Malformed;
            return res;
        }

        const uint64_t max_out = options.limits.max_output_bytes;
        if (max_out != 0U && logical_size > max_out) {
            res.status = PayloadStatus::LimitExceeded;
            res.needed = logical_size;
            return res;
        }

        uint64_t expected = 0;
        uint64_t written  = 0;
        for (size_t i = 0; i < part_indices.size(); ++i) {
            const ContainerBlockRef& b = blocks[part_indices[i]];
            if (!validate_range(file_bytes, b.data_offset, b.data_size)) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
            if (b.logical_offset != expected) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
            if (b.data_size > logical_size - expected) {
                res.status = PayloadStatus::Malformed;
                return res;
            }

            const std::span<const std::byte> src
                = file_bytes.subspan(static_cast<size_t>(b.data_offset),
                                     static_cast<size_t>(b.data_size));
            copy_bytes(out, expected, src, &written);
            expected += b.data_size;
        }

        if (expected != logical_size) {
            res.status = PayloadStatus::Malformed;
            return res;
        }

        res.needed  = logical_size;
        res.written = written;
        if (written < logical_size) {
            res.status = PayloadStatus::OutputTruncated;
        }
        return res;
    }

}  // namespace

PayloadResult
extract_payload(std::span<const std::byte> file_bytes,
                std::span<const ContainerBlockRef> blocks, uint32_t seed_index,
                std::span<std::byte> out_payload,
                std::span<uint32_t> scratch_indices,
                const PayloadOptions& options) noexcept
{
    PayloadResult res;
    if (static_cast<size_t>(seed_index) >= blocks.size()) {
        res.status = PayloadStatus::Malformed;
        return res;
    }

    const ContainerBlockRef& seed = blocks[seed_index];

    if (seed.chunking == BlockChunking::GifSubBlocks) {
        return extract_single_block(file_bytes, seed, out_payload, options);
    }

    if (seed.part_count <= 1U
        && seed.chunking != BlockChunking::JpegApp2SeqTotal
        && seed.chunking != BlockChunking::JpegXmpExtendedGuidOffset) {
        return extract_single_block(file_bytes, seed, out_payload, options);
    }

    // Multi-part logical streams.
    size_t count = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const ContainerBlockRef& b = blocks[i];
        bool match                 = false;
        if (seed.chunking == BlockChunking::JpegApp2SeqTotal) {
            match = blocks_match_jpeg_icc(seed, b);
        } else if (seed.chunking == BlockChunking::JpegXmpExtendedGuidOffset) {
            match = blocks_match_jpeg_xmp_ext(seed, b);
        } else if (seed.part_count > 1U) {
            match = blocks_match_multipart(seed, b);
        }
        if (!match) {
            continue;
        }
        if (count >= static_cast<size_t>(options.limits.max_parts)) {
            res.status = PayloadStatus::LimitExceeded;
            res.needed = static_cast<uint64_t>(count);
            return res;
        }
        if (count >= scratch_indices.size()) {
            res.status = PayloadStatus::LimitExceeded;
            res.needed = static_cast<uint64_t>(count + 1U);
            return res;
        }
        if (i > static_cast<size_t>(0xFFFFFFFFU)) {
            res.status = PayloadStatus::LimitExceeded;
            return res;
        }
        scratch_indices[count] = static_cast<uint32_t>(i);
        count += 1;
    }

    if (count == 0) {
        res.status = PayloadStatus::Malformed;
        return res;
    }

    const std::span<uint32_t> parts = scratch_indices.first(count);

    if (seed.chunking == BlockChunking::JpegApp2SeqTotal) {
        insertion_sort_by_part_index(parts, blocks);
        const uint32_t expected_total = (seed.part_count != 0U)
                                            ? seed.part_count
                                            : static_cast<uint32_t>(count);
        if (expected_total == 0U || expected_total > options.limits.max_parts) {
            res.status = PayloadStatus::LimitExceeded;
            return res;
        }
        if (static_cast<uint32_t>(count) != expected_total) {
            res.status = PayloadStatus::Malformed;
            return res;
        }
        for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
            const ContainerBlockRef& b = blocks[parts[i]];
            if (b.part_index != i) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
        }
        return extract_concat_parts(file_bytes, blocks, parts, out_payload,
                                    options);
    }

    if (seed.chunking == BlockChunking::JpegXmpExtendedGuidOffset) {
        insertion_sort_by_logical_offset(parts, blocks);

        uint64_t logical_size = seed.logical_size;
        if (logical_size == 0U) {
            uint64_t max_end = 0;
            for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
                const ContainerBlockRef& b = blocks[parts[i]];
                const uint64_t end         = b.logical_offset + b.data_size;
                max_end                    = (end > max_end) ? end : max_end;
            }
            logical_size = max_end;
        }
        return extract_offset_parts(file_bytes, blocks, parts, logical_size,
                                    out_payload, options);
    }

    if (seed.part_count > 1U) {
        insertion_sort_by_part_index(parts, blocks);

        bool any_offsets = false;
        for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
            if (blocks[parts[i]].logical_offset != 0U) {
                any_offsets = true;
                break;
            }
        }
        if (any_offsets) {
            insertion_sort_by_logical_offset(parts, blocks);
            uint64_t logical_size = 0;
            uint64_t max_end      = 0;
            for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
                const ContainerBlockRef& b = blocks[parts[i]];
                if (b.logical_size != 0U) {
                    logical_size = b.logical_size;
                }
                const uint64_t end = b.logical_offset + b.data_size;
                max_end            = (end > max_end) ? end : max_end;
            }
            if (logical_size == 0U) {
                logical_size = max_end;
            }
            return extract_offset_parts(file_bytes, blocks, parts, logical_size,
                                        out_payload, options);
        }

        const uint32_t expected_total = (seed.part_count != 0U)
                                            ? seed.part_count
                                            : static_cast<uint32_t>(count);
        if (expected_total == 0U || expected_total > options.limits.max_parts) {
            res.status = PayloadStatus::LimitExceeded;
            return res;
        }
        if (static_cast<uint32_t>(count) != expected_total) {
            res.status = PayloadStatus::Malformed;
            return res;
        }
        for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
            const ContainerBlockRef& b = blocks[parts[i]];
            if (b.part_index != i) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
        }
        return extract_concat_parts(file_bytes, blocks, parts, out_payload,
                                    options);
    }

    res.status = PayloadStatus::Unsupported;
    return res;
}

namespace {

    struct RandomPayloadReader final {
        const RandomAccessSourceRange* source = nullptr;
        RandomAccessReadWindow window;
        PayloadRandomAccessResult* result = nullptr;
        RandomAccessReadLimits limits;
        RandomAccessReadWindowOptions window_options;
    };


    static bool
    random_payload_range_valid(const RandomAccessSourceRange& source,
                               uint64_t offset, uint64_t size) noexcept
    {
        return offset <= source.size && size <= source.size - offset;
    }


    static bool random_payload_u8(RandomPayloadReader* reader, uint64_t offset,
                                  uint8_t* out) noexcept
    {
        if (!reader || !reader->source || !reader->result || !out) {
            return false;
        }
        const RandomAccessViewResult view
            = random_access_read_view(*reader->source, offset, 1U,
                                      &reader->window, &reader->result->input,
                                      reader->limits, reader->window_options);
        if (!view.ok()) {
            reader->result->payload.status = PayloadStatus::Malformed;
            return false;
        }
        *out = u8(view.bytes[0]);
        return true;
    }


    static bool random_payload_copy(RandomPayloadReader* reader,
                                    uint64_t source_offset,
                                    uint64_t source_size,
                                    std::span<std::byte> out,
                                    uint64_t destination_offset,
                                    uint64_t* io_written) noexcept
    {
        if (!reader || !reader->source || !reader->result || !io_written
            || !random_payload_range_valid(*reader->source, source_offset,
                                           source_size)) {
            if (reader && reader->result) {
                reader->result->payload.status = PayloadStatus::Malformed;
            }
            return false;
        }
        if (destination_offset >= out.size() || source_size == 0U) {
            return true;
        }
        const uint64_t room = static_cast<uint64_t>(out.size())
                              - destination_offset;
        const uint64_t count = (source_size < room) ? source_size : room;
        if (count == 0U) {
            return true;
        }
        const std::span<std::byte> destination
            = out.subspan(static_cast<size_t>(destination_offset),
                          static_cast<size_t>(count));
        if (random_access_read_exact(*reader->source, source_offset,
                                     destination, &reader->result->input,
                                     reader->limits)
            != RandomAccessReadCode::Ok) {
            reader->result->payload.status = PayloadStatus::Malformed;
            return false;
        }
        *io_written += count;
        return true;
    }


    static PayloadResult extract_random_gif_subblocks(
        RandomPayloadReader* reader, const ContainerBlockRef& block,
        std::span<std::byte> out, const PayloadOptions& options) noexcept
    {
        PayloadResult res;
        if (!reader || !reader->source
            || !random_payload_range_valid(*reader->source, block.data_offset,
                                           block.data_size)) {
            res.status = PayloadStatus::Malformed;
            return res;
        }
        uint64_t p       = 0U;
        uint64_t needed  = 0U;
        uint64_t written = 0U;
        while (p < block.data_size) {
            uint8_t sub = 0U;
            if (!random_payload_u8(reader, block.data_offset + p, &sub)) {
                return reader->result->payload;
            }
            p += 1U;
            if (sub == 0U) {
                res.needed  = needed;
                res.written = written;
                if (written < needed) {
                    res.status = PayloadStatus::OutputTruncated;
                }
                return res;
            }
            if (sub > block.data_size - p) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
            needed += sub;
            if (options.limits.max_output_bytes != 0U
                && needed > options.limits.max_output_bytes) {
                res.status  = PayloadStatus::LimitExceeded;
                res.needed  = needed;
                res.written = written;
                return res;
            }
            if (!random_payload_copy(reader, block.data_offset + p, sub, out,
                                     written, &written)) {
                return reader->result->payload;
            }
            p += sub;
        }
        res.status = PayloadStatus::Malformed;
        return res;
    }


    static PayloadResult extract_random_parts(
        RandomPayloadReader* reader, std::span<const ContainerBlockRef> blocks,
        std::span<const uint32_t> part_indices, uint64_t logical_size,
        bool use_logical_offsets, std::span<std::byte> out,
        const PayloadOptions& options) noexcept
    {
        PayloadResult res;
        uint64_t needed = use_logical_offsets ? logical_size : 0U;
        if (use_logical_offsets && logical_size == 0U) {
            res.status = PayloadStatus::Malformed;
            return res;
        }
        if (!use_logical_offsets) {
            for (uint32_t index : part_indices) {
                const ContainerBlockRef& block = blocks[index];
                if (!random_payload_range_valid(*reader->source,
                                                block.data_offset,
                                                block.data_size)
                    || needed > UINT64_MAX - block.data_size) {
                    res.status = PayloadStatus::Malformed;
                    return res;
                }
                needed += block.data_size;
            }
        }
        if (options.limits.max_output_bytes != 0U
            && needed > options.limits.max_output_bytes) {
            res.status = PayloadStatus::LimitExceeded;
            res.needed = needed;
            return res;
        }

        uint64_t expected = 0U;
        uint64_t written  = 0U;
        for (uint32_t index : part_indices) {
            const ContainerBlockRef& block = blocks[index];
            const uint64_t destination     = use_logical_offsets
                                                 ? block.logical_offset
                                                 : expected;
            if (!random_payload_range_valid(*reader->source, block.data_offset,
                                            block.data_size)
                || (use_logical_offsets && destination != expected)
                || destination > needed
                || block.data_size > needed - destination) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
            if (!random_payload_copy(reader, block.data_offset, block.data_size,
                                     out, destination, &written)) {
                return reader->result->payload;
            }
            expected = destination + block.data_size;
        }
        if (expected != needed) {
            res.status = PayloadStatus::Malformed;
            return res;
        }
        res.needed  = needed;
        res.written = written;
        if (written < needed) {
            res.status = PayloadStatus::OutputTruncated;
        }
        return res;
    }


    static PayloadResult extract_payload_random_uncompressed(
        RandomPayloadReader* reader, std::span<const ContainerBlockRef> blocks,
        uint32_t seed_index, std::span<std::byte> out_payload,
        std::span<uint32_t> scratch_indices,
        const PayloadOptions& options) noexcept
    {
        PayloadResult res;
        if (!reader || !reader->source || seed_index >= blocks.size()) {
            res.status = PayloadStatus::Malformed;
            return res;
        }
        const ContainerBlockRef& seed = blocks[seed_index];
        if (seed.chunking == BlockChunking::GifSubBlocks) {
            return extract_random_gif_subblocks(reader, seed, out_payload,
                                                options);
        }
        if (seed.part_count <= 1U
            && seed.chunking != BlockChunking::JpegApp2SeqTotal
            && seed.chunking != BlockChunking::JpegXmpExtendedGuidOffset) {
            if (!random_payload_range_valid(*reader->source, seed.data_offset,
                                            seed.data_size)) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
            if (options.limits.max_output_bytes != 0U
                && seed.data_size > options.limits.max_output_bytes) {
                res.status = PayloadStatus::LimitExceeded;
                res.needed = seed.data_size;
                return res;
            }
            uint64_t written = 0U;
            if (!random_payload_copy(reader, seed.data_offset, seed.data_size,
                                     out_payload, 0U, &written)) {
                return reader->result->payload;
            }
            res.needed  = seed.data_size;
            res.written = written;
            if (written < seed.data_size) {
                res.status = PayloadStatus::OutputTruncated;
            }
            return res;
        }

        size_t count = 0U;
        for (size_t i = 0U; i < blocks.size(); ++i) {
            const ContainerBlockRef& block = blocks[i];
            bool match                     = false;
            if (seed.chunking == BlockChunking::JpegApp2SeqTotal) {
                match = blocks_match_jpeg_icc(seed, block);
            } else if (seed.chunking
                       == BlockChunking::JpegXmpExtendedGuidOffset) {
                match = blocks_match_jpeg_xmp_ext(seed, block);
            } else if (seed.part_count > 1U) {
                match = blocks_match_multipart(seed, block);
            }
            if (!match) {
                continue;
            }
            if (count >= options.limits.max_parts
                || count >= scratch_indices.size() || i > UINT32_MAX) {
                res.status = PayloadStatus::LimitExceeded;
                res.needed = count + 1U;
                return res;
            }
            scratch_indices[count++] = static_cast<uint32_t>(i);
        }
        if (count == 0U) {
            res.status = PayloadStatus::Malformed;
            return res;
        }
        std::span<uint32_t> parts = scratch_indices.first(count);

        if (seed.chunking == BlockChunking::JpegApp2SeqTotal) {
            insertion_sort_by_part_index(parts, blocks);
            const uint32_t expected_total = seed.part_count != 0U
                                                ? seed.part_count
                                                : static_cast<uint32_t>(count);
            if (expected_total == 0U
                || expected_total > options.limits.max_parts
                || count != expected_total) {
                res.status = (expected_total > options.limits.max_parts)
                                 ? PayloadStatus::LimitExceeded
                                 : PayloadStatus::Malformed;
                return res;
            }
            for (uint32_t i = 0U; i < count; ++i) {
                if (blocks[parts[i]].part_index != i) {
                    res.status = PayloadStatus::Malformed;
                    return res;
                }
            }
            return extract_random_parts(reader, blocks, parts, 0U, false,
                                        out_payload, options);
        }

        if (seed.chunking == BlockChunking::JpegXmpExtendedGuidOffset) {
            insertion_sort_by_logical_offset(parts, blocks);
            uint64_t logical_size = seed.logical_size;
            if (logical_size == 0U) {
                for (uint32_t index : parts) {
                    const ContainerBlockRef& block = blocks[index];
                    if (block.logical_offset > UINT64_MAX - block.data_size) {
                        res.status = PayloadStatus::Malformed;
                        return res;
                    }
                    const uint64_t end = block.logical_offset + block.data_size;
                    logical_size = (end > logical_size) ? end : logical_size;
                }
            }
            return extract_random_parts(reader, blocks, parts, logical_size,
                                        true, out_payload, options);
        }

        insertion_sort_by_part_index(parts, blocks);
        bool any_offsets = false;
        for (uint32_t index : parts) {
            any_offsets = any_offsets || blocks[index].logical_offset != 0U;
        }
        if (any_offsets) {
            insertion_sort_by_logical_offset(parts, blocks);
            uint64_t logical_size = 0U;
            for (uint32_t index : parts) {
                const ContainerBlockRef& block = blocks[index];
                if (block.logical_size != 0U) {
                    logical_size = block.logical_size;
                }
                if (block.logical_offset > UINT64_MAX - block.data_size) {
                    res.status = PayloadStatus::Malformed;
                    return res;
                }
                const uint64_t end = block.logical_offset + block.data_size;
                logical_size       = (logical_size == 0U || end > logical_size)
                                         ? end
                                         : logical_size;
            }
            return extract_random_parts(reader, blocks, parts, logical_size,
                                        true, out_payload, options);
        }

        const uint32_t expected_total = seed.part_count != 0U
                                            ? seed.part_count
                                            : static_cast<uint32_t>(count);
        if (expected_total == 0U || expected_total > options.limits.max_parts
            || count != expected_total) {
            res.status = (expected_total > options.limits.max_parts)
                             ? PayloadStatus::LimitExceeded
                             : PayloadStatus::Malformed;
            return res;
        }
        for (uint32_t i = 0U; i < count; ++i) {
            if (blocks[parts[i]].part_index != i) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
        }
        return extract_random_parts(reader, blocks, parts, 0U, false,
                                    out_payload, options);
    }

}  // namespace


PayloadRandomAccessResult
extract_payload_random_access(
    const RandomAccessSourceRange& source,
    std::span<const ContainerBlockRef> blocks, uint32_t seed_index,
    std::span<std::byte> out_payload, std::span<uint32_t> scratch_indices,
    const PayloadRandomAccessScratch& scratch, const PayloadOptions& options,
    const RandomAccessReadLimits& read_limits) noexcept
{
    PayloadRandomAccessResult result;
    if (!random_access_source_range_valid(source)
        || seed_index >= blocks.size()) {
        result.input.code     = RandomAccessReadCode::InvalidArgument;
        result.payload.status = PayloadStatus::Malformed;
        return result;
    }
    if (source.source.contiguous_data != nullptr) {
        const std::span<const std::byte> bytes(
            source.source.contiguous_data
                + static_cast<size_t>(source.source_offset),
            static_cast<size_t>(source.size));
        result.payload = extract_payload(bytes, blocks, seed_index, out_payload,
                                         scratch_indices, options);
        return result;
    }
    if (scratch.read_window.empty()) {
        result.input.code = RandomAccessReadCode::ScratchTooSmall;
        result.input.failure_request_bytes = 1U;
        result.payload.status              = PayloadStatus::OutputTruncated;
        return result;
    }

    RandomPayloadReader reader;
    reader.source         = &source;
    reader.window.storage = scratch.read_window;
    reader.result         = &result;
    reader.limits         = read_limits;
    reader.window_options = scratch.window_options;

    const ContainerBlockRef& seed = blocks[seed_index];
    if (!options.decompress || seed.compression == BlockCompression::None) {
        result.payload = extract_payload_random_uncompressed(
            &reader, blocks, seed_index, out_payload, scratch_indices, options);
        return result;
    }

    PayloadOptions compressed_options          = options;
    compressed_options.decompress              = false;
    compressed_options.limits.max_output_bytes = 0U;
    result.payload = extract_payload_random_uncompressed(&reader, blocks,
                                                         seed_index,
                                                         scratch.compressed,
                                                         scratch_indices,
                                                         compressed_options);
    if (result.payload.status == PayloadStatus::OutputTruncated) {
        result.compressed_scratch_needed = result.payload.needed;
        return result;
    }
    if (result.payload.status != PayloadStatus::Ok) {
        return result;
    }

    ContainerBlockRef compressed_block;
    compressed_block.compression = seed.compression;
    compressed_block.data_size   = result.payload.written;
    const std::array<ContainerBlockRef, 1> one { compressed_block };
    result.payload = extract_payload(
        scratch.compressed.first(static_cast<size_t>(result.payload.written)),
        one, 0U, out_payload, scratch_indices, options);
    return result;
}

}  // namespace openmeta
