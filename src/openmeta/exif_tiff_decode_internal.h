// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/exif_tiff_decode.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace openmeta {

// Internal-only (non-installed) types for EXIF/TIFF decoding internals.
// This header is used to split vendor MakerNote decoders into separate
// translation units without exposing these helpers as part of the public API.

struct TiffConfig final {
    bool le      = true;
    bool bigtiff = false;
};

struct ClassicIfdCandidate final {
    uint64_t offset        = 0;
    bool le                = true;
    uint16_t entry_count   = 0;
    uint32_t valid_entries = 0;
};

namespace exif_internal {

    constexpr uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
    }

    struct ClassicIfdEntry final {
        uint16_t tag            = 0;
        uint16_t type           = 0;
        uint32_t count32        = 0;
        uint32_t value_or_off32 = 0;
    };

    struct OffsetPolicy final {
        // For classic TIFF IFD entries, out-of-line values use an offset field
        // that is interpreted relative to a base. Most TIFF/EXIF uses base=0
        // (offsets are relative to the start of the TIFF byte stream).
        uint64_t out_of_line_base = 0;

        // Some vendor MakerNotes use a signed origin for offsets (ie, base may
        // be negative). When enabled, out-of-line offsets are resolved using
        // `out_of_line_base_i64` + off32 with overflow and negative checks.
        bool out_of_line_base_is_signed = false;
        int64_t out_of_line_base_i64    = 0;
    };

    struct MakerNoteLayout final {
        TiffConfig cfg;
        std::span<const std::byte> bytes;
        OffsetPolicy offsets;
    };

    struct ClassicIfdValueRef final {
        uint64_t value_off   = 0;
        uint64_t value_bytes = 0;
        bool inline_value    = false;
    };

    // Allocation-free reader used by nested TIFF/MakerNote decoders. Offsets
    // are always relative to range, not to the current cache window.
    struct SourceTiffReader final {
        const RandomAccessSourceRange* range = nullptr;
        RandomAccessReadWindow window;
        std::span<std::byte> value_scratch;
        RandomAccessReadLimits limits;
        RandomAccessReadWindowOptions window_options;
        ExifRandomAccessDecodeResult* result = nullptr;
    };

    struct ExifContext final {
        explicit ExifContext(const MetaStore& store) noexcept;

        bool find_first_value(std::string_view ifd, uint16_t tag,
                              MetaValue* out) noexcept;
        bool find_first_text(std::string_view ifd, uint16_t tag,
                             std::string_view* out) noexcept;
        bool find_first_u32(std::string_view ifd, uint16_t tag,
                            uint32_t* out) noexcept;
        bool find_first_i32(std::string_view ifd, uint16_t tag,
                            int32_t* out) noexcept;

    private:
        struct Slot final {
            std::string_view ifd;
            uint16_t tag  = 0;
            EntryId entry = kInvalidEntryId;  // Cache only hits.
        };

        const MetaStore* store_ = nullptr;
        Slot slots_[32] {};
        uint32_t next_ = 0;

        bool find_first_entry(std::string_view ifd, uint16_t tag,
                              EntryId* out) noexcept;
        void cache_hit(std::string_view ifd, uint16_t tag,
                       EntryId entry) noexcept;
    };

    bool read_classic_ifd_entry(const TiffConfig& cfg,
                                std::span<const std::byte> bytes,
                                uint64_t entry_off,
                                ClassicIfdEntry* out) noexcept;

    bool classic_ifd_entry_value_bytes(const ClassicIfdEntry& e,
                                       uint64_t* out) noexcept;

    bool resolve_classic_ifd_value_ref(const MakerNoteLayout& layout,
                                       uint64_t entry_off,
                                       const ClassicIfdEntry& e,
                                       ClassicIfdValueRef* out,
                                       ExifDecodeResult* status_out) noexcept;

    bool resolve_classic_ifd_value_ref(const OffsetPolicy& offsets,
                                       uint64_t entry_off,
                                       const ClassicIfdEntry& e,
                                       ClassicIfdValueRef* out,
                                       ExifDecodeResult* status_out) noexcept;

    bool source_tiff_view(SourceTiffReader* source, uint64_t offset,
                          uint64_t size,
                          std::span<const std::byte>* out) noexcept;

    bool source_tiff_value(SourceTiffReader* source, uint64_t offset,
                           uint64_t size,
                           std::span<const std::byte>* out) noexcept;

    bool source_tiff_contains(const SourceTiffReader& source, uint64_t offset,
                              uint64_t size) noexcept;

    bool decode_classic_ifd_from_source(
        SourceTiffReader* source, const TiffConfig& cfg, uint64_t ifd_off,
        const OffsetPolicy& offsets, std::string_view ifd_name,
        MetaStore& store, const ExifDecodeOptions& options,
        ExifDecodeResult* status_out, EntryFlags extra_flags) noexcept;

    // Low-level helpers (implemented in exif_tiff_decode.cc).
    bool match_bytes(std::span<const std::byte> bytes, uint64_t offset,
                     const char* magic, uint32_t magic_len) noexcept;

    bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                    uint16_t* out) noexcept;
    bool read_u16le(std::span<const std::byte> bytes, uint64_t offset,
                    uint16_t* out) noexcept;
    bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                    uint32_t* out) noexcept;
    bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                    uint32_t* out) noexcept;

    bool read_tiff_u16(const TiffConfig& cfg, std::span<const std::byte> bytes,
                       uint64_t offset, uint16_t* out) noexcept;
    bool read_tiff_u32(const TiffConfig& cfg, std::span<const std::byte> bytes,
                       uint64_t offset, uint32_t* out) noexcept;

    bool read_u16_endian(bool le, std::span<const std::byte> bytes,
                         uint64_t offset, uint16_t* out) noexcept;
    bool read_i16_endian(bool le, std::span<const std::byte> bytes,
                         uint64_t offset, int16_t* out) noexcept;

    std::string_view
    make_mk_subtable_ifd_token(std::string_view vendor_prefix,
                               std::string_view subtable, uint32_t index,
                               std::span<char> scratch) noexcept;

    inline std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    MetaValue make_fixed_ascii_text(ByteArena& arena,
                                    std::span<const std::byte> raw) noexcept;

    void emit_bin_dir_entries(std::string_view ifd_name, MetaStore& store,
                              std::span<const uint16_t> tags,
                              std::span<const MetaValue> values,
                              const ExifDecodeLimits& limits,
                              ExifDecodeResult* status_out) noexcept;

    uint64_t tiff_type_size(uint16_t type) noexcept;

    void update_status(ExifDecodeResult* out, ExifDecodeStatus status) noexcept;

    MetaValue decode_tiff_value(const TiffConfig& cfg,
                                std::span<const std::byte> bytes, uint16_t type,
                                uint64_t count, uint64_t value_off,
                                uint64_t value_bytes, ByteArena& arena,
                                const ExifDecodeLimits& limits,
                                ExifDecodeResult* result) noexcept;

    bool score_classic_ifd_candidate(const TiffConfig& cfg,
                                     std::span<const std::byte> bytes,
                                     uint64_t ifd_off,
                                     const ExifDecodeLimits& limits,
                                     ClassicIfdCandidate* out) noexcept;

    bool find_best_classic_ifd_candidate(std::span<const std::byte> bytes,
                                         uint64_t scan_bytes,
                                         const ExifDecodeLimits& limits,
                                         ClassicIfdCandidate* out) noexcept;

    bool looks_like_classic_ifd(const TiffConfig& cfg,
                                std::span<const std::byte> bytes,
                                uint64_t ifd_off,
                                const ExifDecodeLimits& limits) noexcept;

    void decode_classic_ifd_no_header(
        const TiffConfig& cfg, std::span<const std::byte> bytes,
        uint64_t ifd_off, std::string_view ifd_name, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out,
        EntryFlags extra_flags) noexcept;

    // Vendor MakerNote entry points (implemented in their own .cc files).
    bool decode_olympus_makernote(const TiffConfig& parent_cfg,
                                  std::span<const std::byte> tiff_bytes,
                                  uint64_t maker_note_off,
                                  uint64_t maker_note_bytes,
                                  std::string_view mk_ifd0, MetaStore& store,
                                  const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out) noexcept;

    bool decode_pentax_makernote(std::span<const std::byte> maker_note_bytes,
                                 std::string_view mk_ifd0, MetaStore& store,
                                 const ExifDecodeOptions& options,
                                 ExifDecodeResult* status_out) noexcept;

    bool decode_casio_makernote(const TiffConfig& parent_cfg,
                                std::span<const std::byte> tiff_bytes,
                                uint64_t maker_note_off,
                                uint64_t maker_note_bytes,
                                std::string_view mk_ifd0, MetaStore& store,
                                const ExifDecodeOptions& options,
                                ExifDecodeResult* status_out) noexcept;

    bool decode_casio_qvci(std::span<const std::byte> qvci_bytes,
                           std::string_view mk_ifd0, MetaStore& store,
                           const ExifDecodeLimits& limits,
                           ExifDecodeResult* status_out) noexcept;

    bool decode_flir_fff(std::span<const std::byte> fff_bytes, MetaStore& store,
                         const ExifDecodeLimits& limits,
                         ExifDecodeResult* status_out) noexcept;

    bool decode_canon_makernote(const TiffConfig& parent_cfg,
                                std::span<const std::byte> tiff_bytes,
                                uint64_t maker_note_off,
                                uint64_t maker_note_bytes,
                                std::string_view mk_ifd0, MetaStore& store,
                                const ExifDecodeOptions& options,
                                ExifDecodeResult* status_out) noexcept;

    bool decode_fuji_makernote(std::span<const std::byte> tiff_bytes,
                               uint64_t maker_note_off,
                               uint64_t maker_note_bytes,
                               std::string_view mk_ifd0, MetaStore& store,
                               const ExifDecodeOptions& options,
                               ExifDecodeResult* status_out) noexcept;

    bool decode_sony_makernote(const TiffConfig& parent_cfg,
                               std::span<const std::byte> tiff_bytes,
                               uint64_t maker_note_off,
                               uint64_t maker_note_bytes,
                               std::string_view mk_ifd0, MetaStore& store,
                               const ExifDecodeOptions& options,
                               ExifDecodeResult* status_out) noexcept;

    bool decode_panasonic_makernote(const TiffConfig& parent_cfg,
                                    std::span<const std::byte> tiff_bytes,
                                    uint64_t maker_note_off,
                                    uint64_t maker_note_bytes,
                                    std::string_view mk_ifd0, MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept;

    bool decode_kodak_makernote(const TiffConfig& parent_cfg,
                                std::span<const std::byte> tiff_bytes,
                                uint64_t maker_note_off,
                                uint64_t maker_note_bytes,
                                std::string_view mk_ifd0, MetaStore& store,
                                const ExifDecodeOptions& options,
                                ExifDecodeResult* status_out) noexcept;

    bool decode_flir_makernote(const TiffConfig& parent_cfg,
                               std::span<const std::byte> tiff_bytes,
                               uint64_t maker_note_off,
                               uint64_t maker_note_bytes,
                               std::string_view mk_ifd0, MetaStore& store,
                               const ExifDecodeOptions& options,
                               ExifDecodeResult* status_out) noexcept;

    bool decode_ricoh_makernote(const TiffConfig& parent_cfg,
                                std::span<const std::byte> tiff_bytes,
                                uint64_t maker_note_off,
                                uint64_t maker_note_bytes,
                                std::string_view mk_ifd0, MetaStore& store,
                                const ExifDecodeOptions& options,
                                ExifDecodeResult* status_out) noexcept;

    bool decode_minolta_makernote(const TiffConfig& parent_cfg,
                                  std::span<const std::byte> tiff_bytes,
                                  uint64_t maker_note_off,
                                  uint64_t maker_note_bytes,
                                  std::string_view mk_ifd0, MetaStore& store,
                                  const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out) noexcept;

    bool decode_samsung_makernote(const TiffConfig& parent_cfg,
                                  std::span<const std::byte> tiff_bytes,
                                  uint64_t maker_note_off,
                                  uint64_t maker_note_bytes,
                                  std::string_view mk_ifd0, MetaStore& store,
                                  const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out) noexcept;

    bool decode_hp_makernote(std::span<const std::byte> maker_note_bytes,
                             std::string_view mk_ifd0, MetaStore& store,
                             const ExifDecodeOptions& options,
                             ExifDecodeResult* status_out) noexcept;

    bool decode_nintendo_makernote(const TiffConfig& parent_cfg,
                                   std::span<const std::byte> tiff_bytes,
                                   uint64_t maker_note_off,
                                   uint64_t maker_note_bytes,
                                   std::string_view mk_ifd0, MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept;

    bool decode_reconyx_makernote(std::span<const std::byte> maker_note_bytes,
                                  std::string_view mk_ifd0, MetaStore& store,
                                  const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out) noexcept;

    void decode_sony_cipher_subdirs(std::string_view mk_ifd0, MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept;

    void decode_nikon_binary_subdirs(std::string_view mk_ifd0, MetaStore& store,
                                     bool le, const ExifDecodeOptions& options,
                                     ExifDecodeResult* status_out) noexcept;

    void decode_nikon_preview_aliases(MetaStore& store,
                                      const ExifDecodeOptions& options,
                                      ExifDecodeResult* status_out) noexcept;

    void decode_pentax_binary_subdirs(std::string_view mk_ifd0,
                                      MetaStore& store, bool le,
                                      const ExifDecodeOptions& options,
                                      ExifDecodeResult* status_out) noexcept;

}  // namespace exif_internal
}  // namespace openmeta
