// SPDX-License-Identifier: Apache-2.0

#include "openmeta/jumbf_decode.h"

#include "openmeta/container_scan.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if defined(OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE) \
    && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
#    include <openssl/evp.h>
#    include <openssl/pem.h>
#    include <openssl/rsa.h>
#    include <openssl/x509.h>
#    include <openssl/x509_vfy.h>
#endif

namespace openmeta {
namespace {

#if !defined(OPENMETA_ENABLE_C2PA_VERIFY)
#    define OPENMETA_ENABLE_C2PA_VERIFY 0
#endif

#if !defined(OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE)
#    if defined(_WIN32) || defined(__APPLE__)
#        define OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE 1
#    else
#        define OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE 0
#    endif
#endif

#if !defined(OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE)
#    define OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE 0
#endif

    struct BmffBox final {
        uint64_t offset      = 0;
        uint64_t size        = 0;
        uint64_t header_size = 0;
        uint32_t type        = 0;
    };

    struct DecodeContext final {
        MetaStore* store = nullptr;
        BlockId block    = kInvalidBlockId;
        EntryFlags flags = EntryFlags::None;
        std::span<const std::byte> input_bytes;
        struct ParsedBox final {
            uint32_t type        = 0;
            int32_t parent_index = -1;
            std::span<const std::byte> payload;
            bool has_jumb_label = false;
            std::string jumb_label;
        };
        std::vector<ParsedBox> boxes;
        JumbfDecodeOptions options;
        JumbfDecodeResult result;
        uint32_t order_in_block = 0;
        bool c2pa_emitted       = false;
        bool c2pa_detected      = false;
    };

    static constexpr uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
    }


    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2U > bytes.size()) {
            return false;
        }
        *out = static_cast<uint16_t>(u8(bytes[offset + 0U]) << 8U)
               | static_cast<uint16_t>(u8(bytes[offset + 1U]) << 0U);
        return true;
    }


    static bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4U > bytes.size()) {
            return false;
        }
        uint32_t value = 0;
        value |= static_cast<uint32_t>(u8(bytes[offset + 0U])) << 24U;
        value |= static_cast<uint32_t>(u8(bytes[offset + 1U])) << 16U;
        value |= static_cast<uint32_t>(u8(bytes[offset + 2U])) << 8U;
        value |= static_cast<uint32_t>(u8(bytes[offset + 3U])) << 0U;
        *out = value;
        return true;
    }


    static bool read_u64be(std::span<const std::byte> bytes, uint64_t offset,
                           uint64_t* out) noexcept
    {
        if (!out || offset + 8U > bytes.size()) {
            return false;
        }
        uint64_t value = 0;
        for (uint32_t index = 0; index < 8U; ++index) {
            value = (value << 8U)
                    | static_cast<uint64_t>(u8(bytes[offset + index]));
        }
        *out = value;
        return true;
    }


    static bool parse_bmff_box(std::span<const std::byte> bytes,
                               uint64_t offset, uint64_t parent_end,
                               BmffBox* out) noexcept
    {
        if (!out || offset + 8U > parent_end || parent_end > bytes.size()) {
            return false;
        }

        uint32_t size32 = 0;
        uint32_t type   = 0;
        if (!read_u32be(bytes, offset + 0U, &size32)
            || !read_u32be(bytes, offset + 4U, &type)) {
            return false;
        }

        uint64_t header_size = 8U;
        uint64_t box_size    = static_cast<uint64_t>(size32);
        if (size32 == 1U) {
            uint64_t size64 = 0;
            if (!read_u64be(bytes, offset + 8U, &size64)) {
                return false;
            }
            header_size = 16U;
            box_size    = size64;
        } else if (size32 == 0U) {
            box_size = parent_end - offset;
        }

        if (box_size < header_size) {
            return false;
        }
        if (offset > parent_end || box_size > parent_end - offset) {
            return false;
        }
        if (offset > bytes.size() || box_size > bytes.size() - offset) {
            return false;
        }

        out->offset      = offset;
        out->size        = box_size;
        out->header_size = header_size;
        out->type        = type;
        return true;
    }


    static bool looks_like_bmff_sequence(std::span<const std::byte> bytes,
                                         uint64_t begin, uint64_t end) noexcept
    {
        if (begin >= end || end > bytes.size()) {
            return false;
        }
        BmffBox box;
        return parse_bmff_box(bytes, begin, end, &box);
    }


    static bool is_printable_ascii(uint8_t c) noexcept
    {
        return c >= 0x20U && c <= 0x7EU;
    }


    static std::string fourcc_to_text(uint32_t value)
    {
        char out[5];
        out[0] = static_cast<char>((value >> 24U) & 0xFFU);
        out[1] = static_cast<char>((value >> 16U) & 0xFFU);
        out[2] = static_cast<char>((value >> 8U) & 0xFFU);
        out[3] = static_cast<char>((value >> 0U) & 0xFFU);
        out[4] = '\0';
        if (is_printable_ascii(static_cast<uint8_t>(out[0]))
            && is_printable_ascii(static_cast<uint8_t>(out[1]))
            && is_printable_ascii(static_cast<uint8_t>(out[2]))
            && is_printable_ascii(static_cast<uint8_t>(out[3]))) {
            return std::string(out, out + 4);
        }
        char hex[16];
        std::snprintf(hex, sizeof(hex), "0x%08X", static_cast<unsigned>(value));
        return std::string(hex);
    }


    static bool has_entry_room(DecodeContext* ctx) noexcept
    {
        if (!ctx) {
            return false;
        }
        const uint32_t max_entries = ctx->options.limits.max_entries;
        if (max_entries != 0U && ctx->result.entries_decoded >= max_entries) {
            ctx->result.status = JumbfDecodeStatus::LimitExceeded;
            return false;
        }
        return true;
    }

    static void normalize_jumbf_limits(JumbfDecodeLimits* limits) noexcept
    {
        if (!limits) {
            return;
        }

        if (limits->max_box_depth == 0U) {
            limits->max_box_depth = 32U;
        }
        if (limits->max_boxes == 0U) {
            limits->max_boxes = 1U << 16;
        }
        if (limits->max_entries == 0U) {
            limits->max_entries = 200000U;
        }
        if (limits->max_cbor_depth == 0U) {
            limits->max_cbor_depth = 64U;
        }
        if (limits->max_cbor_items == 0U) {
            limits->max_cbor_items = 200000U;
        }
    }


    static bool emit_field_text(DecodeContext* ctx, std::string_view field,
                                std::string_view value,
                                EntryFlags extra_flags) noexcept
    {
        if (!ctx || !ctx->store || !has_entry_room(ctx)) {
            return false;
        }
        Entry entry;
        entry.key          = make_jumbf_field_key(ctx->store->arena(), field);
        entry.value        = make_text(ctx->store->arena(), value,
                                       TextEncoding::Ascii);
        entry.origin.block = ctx->block;
        entry.origin.order_in_block = ctx->order_in_block++;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = 1U;
        entry.flags                 = ctx->flags | extra_flags;
        (void)ctx->store->add_entry(entry);
        ctx->result.entries_decoded += 1U;
        return true;
    }


    static bool emit_field_u64(DecodeContext* ctx, std::string_view field,
                               uint64_t value, EntryFlags extra_flags) noexcept
    {
        if (!ctx || !ctx->store || !has_entry_room(ctx)) {
            return false;
        }
        Entry entry;
        entry.key          = make_jumbf_field_key(ctx->store->arena(), field);
        entry.value        = make_u64(value);
        entry.origin.block = ctx->block;
        entry.origin.order_in_block = ctx->order_in_block++;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = 1U;
        entry.flags                 = ctx->flags | extra_flags;
        (void)ctx->store->add_entry(entry);
        ctx->result.entries_decoded += 1U;
        return true;
    }


    static bool emit_field_u8(DecodeContext* ctx, std::string_view field,
                              uint8_t value, EntryFlags extra_flags) noexcept
    {
        if (!ctx || !ctx->store || !has_entry_room(ctx)) {
            return false;
        }
        Entry entry;
        entry.key          = make_jumbf_field_key(ctx->store->arena(), field);
        entry.value        = make_u8(value);
        entry.origin.block = ctx->block;
        entry.origin.order_in_block = ctx->order_in_block++;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = 1U;
        entry.flags                 = ctx->flags | extra_flags;
        (void)ctx->store->add_entry(entry);
        ctx->result.entries_decoded += 1U;
        return true;
    }


    static bool emit_cbor_value(DecodeContext* ctx, std::string_view key,
                                const MetaValue& value) noexcept
    {
        if (!ctx || !ctx->store || !has_entry_room(ctx)) {
            return false;
        }
        Entry entry;
        entry.key          = make_jumbf_cbor_key(ctx->store->arena(), key);
        entry.value        = value;
        entry.origin.block = ctx->block;
        entry.origin.order_in_block = ctx->order_in_block++;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = 1U;
        entry.flags                 = ctx->flags;
        (void)ctx->store->add_entry(entry);
        ctx->result.entries_decoded += 1U;
        return true;
    }


    static void make_child_path(std::string_view parent, uint32_t child_index,
                                std::string* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        if (parent.empty()) {
            out->append("box.");
        } else {
            out->append(parent.data(), parent.size());
            out->push_back('.');
        }
        out->append(std::to_string(static_cast<unsigned>(child_index)));
    }


    static void make_field_key(std::string_view path, std::string_view suffix,
                               std::string* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        out->append(path.data(), path.size());
        out->push_back('.');
        out->append(suffix.data(), suffix.size());
    }


    static bool append_c2pa_marker(DecodeContext* ctx,
                                   std::string_view marker_path) noexcept
    {
        if (!ctx) {
            return false;
        }
        ctx->c2pa_detected = true;
        if (ctx->c2pa_emitted) {
            return true;
        }
        if (!emit_field_u8(ctx, "c2pa.detected", 1U, EntryFlags::Derived)) {
            return false;
        }
        if (!marker_path.empty()) {
            if (!emit_field_text(ctx, "c2pa.marker_path", marker_path,
                                 EntryFlags::Derived)) {
                return false;
            }
        }
        ctx->c2pa_emitted = true;
        return true;
    }

    static const char*
    c2pa_verify_backend_name(C2paVerifyBackend backend) noexcept
    {
        switch (backend) {
        case C2paVerifyBackend::None: return "none";
        case C2paVerifyBackend::Auto: return "auto";
        case C2paVerifyBackend::Native: return "native";
        case C2paVerifyBackend::OpenSsl: return "openssl";
        }
        return "unknown";
    }

    static const char* c2pa_verify_status_name(C2paVerifyStatus status) noexcept
    {
        switch (status) {
        case C2paVerifyStatus::NotRequested: return "not_requested";
        case C2paVerifyStatus::DisabledByBuild: return "disabled_by_build";
        case C2paVerifyStatus::BackendUnavailable: return "backend_unavailable";
        case C2paVerifyStatus::NoSignatures: return "no_signatures";
        case C2paVerifyStatus::InvalidSignature: return "invalid_signature";
        case C2paVerifyStatus::VerificationFailed: return "verification_failed";
        case C2paVerifyStatus::Verified: return "verified";
        case C2paVerifyStatus::NotImplemented: return "not_implemented";
        case C2paVerifyStatus::SignatureVerifiedOnly:
            return "signature_verified_only";
        }
        return "unknown";
    }

    enum class C2paVerifyDetailStatus : uint8_t {
        NotChecked,
        Pass,
        Fail,
    };

    static const char*
    c2pa_verify_detail_status_name(C2paVerifyDetailStatus status) noexcept
    {
        switch (status) {
        case C2paVerifyDetailStatus::NotChecked: return "not_checked";
        case C2paVerifyDetailStatus::Pass: return "pass";
        case C2paVerifyDetailStatus::Fail: return "fail";
        }
        return "unknown";
    }

#if OPENMETA_ENABLE_C2PA_VERIFY
    static uint8_t c2pa_chain_rank(C2paVerifyDetailStatus status) noexcept
    {
        switch (status) {
        case C2paVerifyDetailStatus::Pass: return 2U;
        case C2paVerifyDetailStatus::Fail: return 1U;
        case C2paVerifyDetailStatus::NotChecked: return 0U;
        }
        return 0U;
    }
    static C2paVerifyBackend
    resolve_c2pa_verify_backend(C2paVerifyBackend requested) noexcept
    {
        switch (requested) {
        case C2paVerifyBackend::None: return C2paVerifyBackend::None;
        case C2paVerifyBackend::Native:
#    if OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE
            return C2paVerifyBackend::Native;
#    else
            return C2paVerifyBackend::None;
#    endif
        case C2paVerifyBackend::OpenSsl:
#    if OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
            return C2paVerifyBackend::OpenSsl;
#    else
            return C2paVerifyBackend::None;
#    endif
        case C2paVerifyBackend::Auto:
        default:
#    if OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
            return C2paVerifyBackend::OpenSsl;
#    elif OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE
            return C2paVerifyBackend::Native;
#    else
            return C2paVerifyBackend::None;
#    endif
        }
    }
#endif

    static std::string_view arena_string_view(const ByteArena& arena,
                                              ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool cbor_path_separator(char c) noexcept
    {
        return c == '.' || c == '[' || c == ']' || c == '@';
    }

    static bool cbor_key_has_segment(std::string_view key,
                                     std::string_view segment) noexcept
    {
        if (key.empty() || segment.empty()) {
            return false;
        }
        size_t pos = 0U;
        while (true) {
            pos = key.find(segment, pos);
            if (pos == std::string_view::npos) {
                return false;
            }
            const size_t end   = pos + segment.size();
            const bool left_ok = (pos == 0U)
                                 || cbor_path_separator(key[pos - 1U]);
            const bool right_ok = (end >= key.size())
                                  || cbor_path_separator(key[end]);
            if (left_ok && right_ok) {
                return true;
            }
            pos += 1U;
        }
    }

    static bool
    bytes_all_ascii_printable(std::span<const std::byte> bytes) noexcept
    {
        for (const std::byte b : bytes) {
            const uint8_t c = u8(b);
            if (c < 0x20U || c > 0x7EU) {
                return false;
            }
        }
        return true;
    }

    static bool string_starts_with(std::string_view text,
                                   std::string_view prefix) noexcept
    {
        return text.size() >= prefix.size()
               && text.compare(0U, prefix.size(), prefix) == 0;
    }

    static bool string_ends_with(std::string_view text,
                                 std::string_view suffix) noexcept
    {
        return text.size() >= suffix.size()
               && text.compare(text.size() - suffix.size(), suffix.size(),
                               suffix)
                      == 0;
    }

    static bool string_ends_with_ascii_icase(std::string_view text,
                                             std::string_view suffix) noexcept
    {
        if (text.size() < suffix.size()) {
            return false;
        }
        const size_t offset = text.size() - suffix.size();
        for (size_t i = 0U; i < suffix.size(); ++i) {
            const char a  = text[offset + i];
            const char b  = suffix[i];
            const char la = (a >= 'A' && a <= 'Z')
                                ? static_cast<char>(a - 'A' + 'a')
                                : a;
            const char lb = (b >= 'A' && b <= 'Z')
                                ? static_cast<char>(b - 'A' + 'a')
                                : b;
            if (la != lb) {
                return false;
            }
        }
        return true;
    }

    static bool key_matches_field(std::string_view key, std::string_view prefix,
                                  std::string_view field) noexcept
    {
        if (prefix.empty() || field.empty()) {
            return false;
        }
        std::string full(prefix);
        full.push_back('.');
        full.append(field.data(), field.size());
        if (!string_starts_with(key, full)) {
            return false;
        }
        if (key.size() == full.size()) {
            return true;
        }
        const char next = key[full.size()];
        return next == '.' || next == '[';
    }

    static bool key_is_indexed_item(std::string_view key,
                                    std::string_view prefix,
                                    uint32_t index) noexcept
    {
        if (!string_starts_with(key, prefix)) {
            return false;
        }
        size_t pos = prefix.size();
        if (pos >= key.size() || key[pos] != '[') {
            return false;
        }
        pos += 1U;
        uint64_t parsed = 0U;
        bool have_digit = false;
        while (pos < key.size() && key[pos] >= '0' && key[pos] <= '9') {
            have_digit = true;
            parsed     = parsed * 10U
                     + static_cast<uint64_t>(
                         static_cast<unsigned>(key[pos] - '0'));
            if (parsed > UINT32_MAX) {
                return false;
            }
            pos += 1U;
        }
        if (!have_digit || pos >= key.size() || key[pos] != ']') {
            return false;
        }
        pos += 1U;
        return pos == key.size() && parsed == static_cast<uint64_t>(index);
    }

    static bool vector_contains_string(const std::vector<std::string>& values,
                                       std::string_view needle) noexcept
    {
        for (const std::string& value : values) {
            if (value == needle) {
                return true;
            }
        }
        return false;
    }

    static bool find_jumbf_field_entry(const DecodeContext& ctx,
                                       std::string_view field,
                                       const Entry** out_entry) noexcept
    {
        if (!ctx.store || !out_entry) {
            return false;
        }
        *out_entry                           = nullptr;
        const std::span<const Entry> entries = ctx.store->entries();
        for (const Entry& entry : entries) {
            if (entry.origin.block != ctx.block
                || entry.key.kind != MetaKeyKind::JumbfField) {
                continue;
            }
            const std::string_view key
                = arena_string_view(ctx.store->arena(),
                                    entry.key.data.jumbf_field.field);
            if (key == field) {
                *out_entry = &entry;
            }
        }
        return *out_entry != nullptr;
    }

    static bool read_jumbf_field_u64(const DecodeContext& ctx,
                                     std::string_view field,
                                     uint64_t* out_value) noexcept
    {
        if (!out_value) {
            return false;
        }
        const Entry* entry = nullptr;
        if (!find_jumbf_field_entry(ctx, field, &entry) || !entry) {
            return false;
        }
        if (entry->value.kind != MetaValueKind::Scalar) {
            return false;
        }
        if (entry->value.elem_type == MetaElementType::U64
            || entry->value.elem_type == MetaElementType::U32
            || entry->value.elem_type == MetaElementType::U16
            || entry->value.elem_type == MetaElementType::U8) {
            *out_value = entry->value.data.u64;
            return true;
        }
        return false;
    }

    static bool find_indexed_segment_prefix(std::string_view key,
                                            std::string_view marker,
                                            std::string* out_prefix) noexcept
    {
        if (!out_prefix || key.empty() || marker.empty()) {
            return false;
        }
        const size_t pos = key.find(marker);
        if (pos == std::string_view::npos) {
            return false;
        }
        const size_t rb = key.find(']', pos + marker.size());
        if (rb == std::string_view::npos) {
            return false;
        }
        out_prefix->assign(key.substr(0U, rb + 1U));
        return true;
    }

    static bool key_is_in_prefix_scope(std::string_view key,
                                       std::string_view prefix) noexcept
    {
        if (prefix.empty() || !string_starts_with(key, prefix)) {
            return false;
        }
        if (key.size() == prefix.size()) {
            return true;
        }
        const char next = key[prefix.size()];
        return next == '.' || next == '[';
    }

    struct C2paVerifySignatureCandidate final {
        std::string prefix;
        bool has_algorithm = false;
        std::string algorithm;
        bool has_cose_unprotected_alg_int = false;
        int64_t cose_unprotected_alg_int  = 0;
        bool has_signing_input            = false;
        std::vector<std::byte> signing_input;
        bool has_signature_bytes = false;
        std::vector<std::byte> signature_bytes;
        bool has_cose_protected_bytes = false;
        std::vector<std::byte> cose_protected_bytes;
        bool has_cose_payload_bytes = false;
        bool cose_payload_is_null   = false;
        bool has_invalid_cose_signature_shape = false;
        std::vector<std::byte> cose_payload_bytes;
        bool has_cose_signature_bytes = false;
        std::vector<std::byte> cose_signature_bytes;
        bool has_source_cbor_box_index = false;
        uint32_t source_cbor_box_index = 0;
        bool has_public_key_der        = false;
        std::vector<std::byte> public_key_der;
        bool has_public_key_pem = false;
        std::string public_key_pem;
        bool has_certificate_der = false;
        std::vector<std::byte> certificate_der;
        bool has_certificate_chain_der = false;
        std::vector<std::vector<std::byte>> certificate_chain_der;
        std::vector<std::vector<std::byte>> detached_payload_candidates;
    };

    struct C2paProfileSummary final {
        bool available                                         = false;
        uint64_t claim_count                                   = 0U;
        uint64_t signature_count                               = 0U;
        uint64_t signature_linked                              = 0U;
        uint64_t signature_orphan                              = 0U;
        uint64_t explicit_reference_signature_count            = 0U;
        uint64_t explicit_reference_unresolved_signature_count = 0U;
        uint64_t explicit_reference_ambiguous_signature_count  = 0U;
        uint64_t manifest_present                              = 0U;
        uint64_t claim_present                                 = 0U;
        uint64_t signature_present                             = 0U;
    };

    struct C2paVerifyEvaluation final {
        C2paVerifyStatus status = C2paVerifyStatus::NotImplemented;
        C2paVerifyDetailStatus profile_status
            = C2paVerifyDetailStatus::NotChecked;
        const char* profile_reason = "not_checked";
        C2paVerifyDetailStatus chain_status = C2paVerifyDetailStatus::NotChecked;
        const char* chain_reason = "not_checked";
        C2paProfileSummary profile_summary;
    };

    static bool find_detached_payload_from_claim_bytes(
        const DecodeContext& ctx, const C2paVerifySignatureCandidate& candidate,
        std::vector<std::byte>* out_payload) noexcept;
    static bool find_detached_payload_from_claim_box_layout(
        const DecodeContext& ctx, uint32_t signature_cbor_box_index,
        std::vector<std::byte>* out_payload) noexcept;
    static void append_detached_payload_candidates_in_order(
        const std::vector<std::vector<std::byte>>& candidates,
        uint64_t max_bytes,
        std::vector<std::vector<std::byte>>* out_candidates) noexcept;
    static int32_t find_first_descendant_box_of_type(const DecodeContext& ctx,
                                                     int32_t ancestor_index,
                                                     uint32_t type) noexcept;
    static bool ascii_icase_contains_text(std::string_view haystack,
                                          std::string_view needle) noexcept;
    static std::string ascii_lower(std::string_view text);
    static uint64_t cbor_limit_or_default(uint64_t configured,
                                          uint64_t default_value) noexcept;
    static void append_unique_string_value(std::vector<std::string>* values,
                                           std::string_view value) noexcept;
    static bool vector_contains_u32(const std::vector<uint32_t>& values,
                                    uint32_t value) noexcept;
    static void sort_unique_u32(std::vector<uint32_t>* values) noexcept;
    static void sort_unique_strings(std::vector<std::string>* values) noexcept;

    static void append_unique_detached_payload_candidate(
        std::span<const std::byte> payload, uint64_t max_bytes,
        std::vector<std::vector<std::byte>>* out_candidates) noexcept
    {
        if (!out_candidates || payload.empty()) {
            return;
        }
        if (max_bytes != 0U && payload.size() > max_bytes) {
            return;
        }
        constexpr size_t kMaxDetachedCandidates = 32U;
        for (const std::vector<std::byte>& existing : *out_candidates) {
            if (existing.size() != payload.size()) {
                continue;
            }
            if (existing.empty()) {
                return;
            }
            if (std::memcmp(existing.data(), payload.data(), payload.size())
                == 0) {
                return;
            }
        }
        if (out_candidates->size() >= kMaxDetachedCandidates) {
            return;
        }
        std::vector<std::byte> copy(payload.begin(), payload.end());
        out_candidates->push_back(copy);
    }

    static bool cbor_key_is_claim_payload_field(std::string_view key) noexcept
    {
        static constexpr std::array<std::string_view, 4U> kClaimFields = {
            std::string_view { ".claim" }, std::string_view { ".claim_bytes" },
            std::string_view { ".claim_cbor" },
            std::string_view { ".claim_payload" }
        };
        for (const std::string_view suffix : kClaimFields) {
            if (string_ends_with(key, suffix)) {
                return true;
            }
        }
        return false;
    }

    static bool cbor_key_is_claim_reference_field(std::string_view key) noexcept
    {
        if (key.empty()) {
            return false;
        }
        std::string_view key_no_index = key;
        if (key_no_index.size() >= 3U && key_no_index.back() == ']') {
            const size_t lb = key_no_index.rfind('[');
            if (lb != std::string_view::npos && lb + 1U < key_no_index.size()
                && lb + 1U < key_no_index.size() - 1U) {
                bool digits_only = true;
                for (size_t i = lb + 1U; i + 1U < key_no_index.size(); ++i) {
                    const char c = key_no_index[i];
                    if (c < '0' || c > '9') {
                        digits_only = false;
                        break;
                    }
                }
                if (digits_only) {
                    key_no_index = key_no_index.substr(0U, lb);
                }
            }
        }

        static constexpr std::array<std::string_view, 79U> kRefSuffixes = {
            std::string_view { ".ref" },
            std::string_view { ".refs" },
            std::string_view { ".reference" },
            std::string_view { ".references" },
            std::string_view { ".reference_id" },
            std::string_view { ".reference-id" },
            std::string_view { ".referenceid" },
            std::string_view { ".references_id" },
            std::string_view { ".references-id" },
            std::string_view { ".referencesid" },
            std::string_view { ".reference_index" },
            std::string_view { ".reference-index" },
            std::string_view { ".referenceindex" },
            std::string_view { ".references_index" },
            std::string_view { ".references-index" },
            std::string_view { ".referencesindex" },
            std::string_view { ".claims" },
            std::string_view { ".ref_id" },
            std::string_view { ".ref-id" },
            std::string_view { ".refid" },
            std::string_view { ".ref_index" },
            std::string_view { ".ref-index" },
            std::string_view { ".refindex" },
            std::string_view { ".claim_ref" },
            std::string_view { ".claim-ref" },
            std::string_view { ".claim_reference" },
            std::string_view { ".claim-reference" },
            std::string_view { ".claim_references" },
            std::string_view { ".claim-references" },
            std::string_view { ".claimref" },
            std::string_view { ".claimreference" },
            std::string_view { ".claim_refs" },
            std::string_view { ".claim-refs" },
            std::string_view { ".claimrefs" },
            std::string_view { ".claimreferences" },
            std::string_view { ".claim_ref_index" },
            std::string_view { ".claim-ref-index" },
            std::string_view { ".claim_ref_id" },
            std::string_view { ".claim-ref-id" },
            std::string_view { ".claimrefid" },
            std::string_view { ".claim_reference_id" },
            std::string_view { ".claim-reference-id" },
            std::string_view { ".claimreferenceid" },
            std::string_view { ".claim_index" },
            std::string_view { ".claim-index" },
            std::string_view { ".claimindex" },
            std::string_view { ".claim_id" },
            std::string_view { ".claim-id" },
            std::string_view { ".claimid" },
            std::string_view { ".claim_url" },
            std::string_view { ".claim-url" },
            std::string_view { ".claim_uri" },
            std::string_view { ".claim-uri" },
            std::string_view { ".claim_link" },
            std::string_view { ".claim-link" },
            std::string_view { ".claimurl" },
            std::string_view { ".claimuri" },
            std::string_view { ".claimlink" },
            std::string_view { ".claim-urls" },
            std::string_view { ".claim-uris" },
            std::string_view { ".claim-links" },
            std::string_view { ".claimurls" },
            std::string_view { ".claimuris" },
            std::string_view { ".claimlinks" },
            std::string_view { ".jumbf_ref" },
            std::string_view { ".jumbf-ref" },
            std::string_view { ".jumbf_reference" },
            std::string_view { ".jumbf-reference" },
            std::string_view { ".jumbf_refs" },
            std::string_view { ".jumbf-refs" },
            std::string_view { ".jumbf_references" },
            std::string_view { ".jumbf-references" },
            std::string_view { ".jumbf_url" },
            std::string_view { ".jumbf-url" },
            std::string_view { ".jumbf_uri" },
            std::string_view { ".jumbf-uri" },
            std::string_view { ".jumbf_link" },
            std::string_view { ".jumbf-link" },
            std::string_view { ".jumbf" },
        };
        for (const std::string_view suffix : kRefSuffixes) {
            if (string_ends_with_ascii_icase(key_no_index, suffix)) {
                return true;
            }
        }
        if (string_ends_with_ascii_icase(key_no_index, ".url")
            || string_ends_with_ascii_icase(key_no_index, ".uri")
            || string_ends_with_ascii_icase(key_no_index, ".href")
            || string_ends_with_ascii_icase(key_no_index, ".link")) {
            const std::string lowered = ascii_lower(key_no_index);
            if (cbor_key_has_segment(lowered, "claim")
                || cbor_key_has_segment(lowered, "reference")
                || cbor_key_has_segment(lowered, "references")
                || cbor_key_has_segment(lowered, "jumbf")
                || cbor_key_has_segment(lowered, "manifest")) {
                return true;
            }
        }
        return false;
    }

    static bool cbor_key_is_reference_index_field(std::string_view key) noexcept
    {
        if (key.empty()) {
            return false;
        }

        std::string_view key_no_index = key;
        if (key_no_index.size() >= 3U && key_no_index.back() == ']') {
            const size_t lb = key_no_index.rfind('[');
            if (lb != std::string_view::npos && lb + 1U < key_no_index.size()
                && lb + 1U < key_no_index.size() - 1U) {
                bool digits_only = true;
                for (size_t i = lb + 1U; i + 1U < key_no_index.size(); ++i) {
                    const char c = key_no_index[i];
                    if (c < '0' || c > '9') {
                        digits_only = false;
                        break;
                    }
                }
                if (digits_only) {
                    key_no_index = key_no_index.substr(0U, lb);
                }
            }
        }

        if (!string_ends_with_ascii_icase(key_no_index, ".index")) {
            return false;
        }
        if (cbor_key_has_segment(key_no_index, "reference")
            || cbor_key_has_segment(key_no_index, "references")) {
            return true;
        }
        return false;
    }

    static bool cbor_key_is_reference_id_field(std::string_view key) noexcept
    {
        if (key.empty()) {
            return false;
        }

        std::string_view key_no_index = key;
        if (key_no_index.size() >= 3U && key_no_index.back() == ']') {
            const size_t lb = key_no_index.rfind('[');
            if (lb != std::string_view::npos && lb + 1U < key_no_index.size()
                && lb + 1U < key_no_index.size() - 1U) {
                bool digits_only = true;
                for (size_t i = lb + 1U; i + 1U < key_no_index.size(); ++i) {
                    const char c = key_no_index[i];
                    if (c < '0' || c > '9') {
                        digits_only = false;
                        break;
                    }
                }
                if (digits_only) {
                    key_no_index = key_no_index.substr(0U, lb);
                }
            }
        }

        if (!string_ends_with_ascii_icase(key_no_index, ".id")) {
            return false;
        }
        if (cbor_key_has_segment(key_no_index, "reference")
            || cbor_key_has_segment(key_no_index, "references")) {
            return true;
        }
        return false;
    }

    static bool claim_reference_text_from_entry(const DecodeContext& ctx,
                                                const Entry& e,
                                                std::string* out_text) noexcept
    {
        if (!ctx.store || !out_text) {
            return false;
        }
        out_text->clear();
        if (e.value.kind == MetaValueKind::Text) {
            const std::span<const std::byte> text = ctx.store->arena().span(
                e.value.data.span);
            out_text->assign(reinterpret_cast<const char*>(text.data()),
                             text.size());
            return !out_text->empty();
        }
        if (e.value.kind == MetaValueKind::Bytes
            || (e.value.kind == MetaValueKind::Array
                && e.value.elem_type == MetaElementType::U8)) {
            const std::span<const std::byte> text = ctx.store->arena().span(
                e.value.data.span);
            if (!bytes_all_ascii_printable(text)) {
                return false;
            }
            out_text->assign(reinterpret_cast<const char*>(text.data()),
                             text.size());
            return !out_text->empty();
        }
        return false;
    }

    static bool
    claim_reference_index_from_scalar_entry(const Entry& e,
                                            uint32_t* out_index) noexcept
    {
        if (!out_index || e.value.kind != MetaValueKind::Scalar) {
            return false;
        }
        switch (e.value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
            *out_index = static_cast<uint32_t>(e.value.data.u64);
            return true;
        case MetaElementType::U64:
            if (e.value.data.u64 > UINT32_MAX) {
                return false;
            }
            *out_index = static_cast<uint32_t>(e.value.data.u64);
            return true;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            if (e.value.data.i64 < 0
                || static_cast<uint64_t>(e.value.data.i64) > UINT32_MAX) {
                return false;
            }
            *out_index = static_cast<uint32_t>(e.value.data.i64);
            return true;
        default: return false;
        }
    }

    static bool parse_decimal_u32(std::string_view text, size_t begin,
                                  size_t end, uint32_t* out) noexcept
    {
        if (!out || begin >= end || end > text.size()) {
            return false;
        }
        uint64_t value = 0U;
        for (size_t i = begin; i < end; ++i) {
            const char c = text[i];
            if (c < '0' || c > '9') {
                return false;
            }
            value = value * 10U
                    + static_cast<uint64_t>(static_cast<unsigned>(c - '0'));
            if (value > UINT32_MAX) {
                return false;
            }
        }
        *out = static_cast<uint32_t>(value);
        return true;
    }

    static bool reference_token_boundary(char c) noexcept
    {
        if (c == '?' || c == '&' || c == '#' || c == ';' || c == '/'
            || c == '\\' || c == ':' || c == ',' || c == '.' || c == '['
            || c == ']' || c == '(' || c == ')' || c == '{' || c == '}'
            || c == '<' || c == '>' || c == '"' || c == '\'' || c == '=') {
            return true;
        }
        return static_cast<unsigned char>(c) <= 0x20U;
    }

    static bool hex_nibble_value(char c, uint8_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (c >= '0' && c <= '9') {
            *out = static_cast<uint8_t>(c - '0');
            return true;
        }
        if (c >= 'a' && c <= 'f') {
            *out = static_cast<uint8_t>(10 + (c - 'a'));
            return true;
        }
        if (c >= 'A' && c <= 'F') {
            *out = static_cast<uint8_t>(10 + (c - 'A'));
            return true;
        }
        return false;
    }

    static std::string uri_percent_decode_once(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0U; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '%' && i + 2U < text.size()) {
                uint8_t hi = 0U;
                uint8_t lo = 0U;
                if (hex_nibble_value(text[i + 1U], &hi)
                    && hex_nibble_value(text[i + 2U], &lo)) {
                    out.push_back(static_cast<char>((hi << 4U) | lo));
                    i += 2U;
                    continue;
                }
            }
            out.push_back(c);
        }
        return out;
    }

    static std::string uri_percent_decode(std::string_view text)
    {
        // Minor real-world edge case: some references are encoded multiple
        // times before entering CBOR payloads. Decode a small fixed number of
        // passes to normalize those forms without unbounded work.
        std::string decoded = uri_percent_decode_once(text);
        for (uint32_t pass = 0U; pass < 2U; ++pass) {
            const std::string next = uri_percent_decode_once(
                std::string_view(decoded));
            if (next == decoded) {
                break;
            }
            decoded = next;
        }
        return decoded;
    }

    static bool claim_index_from_reference_text(std::string_view reference,
                                                uint32_t* out_index) noexcept
    {
        if (!out_index || reference.empty()) {
            return false;
        }
        const std::string decoded = uri_percent_decode(reference);
        const std::string_view decoded_view(decoded);

        size_t begin_trim = 0U;
        size_t end_trim   = decoded_view.size();
        while (begin_trim < end_trim
               && static_cast<unsigned char>(decoded_view[begin_trim])
                      <= 0x20U) {
            begin_trim += 1U;
        }
        while (end_trim > begin_trim
               && static_cast<unsigned char>(decoded_view[end_trim - 1U])
                      <= 0x20U) {
            end_trim -= 1U;
        }
        if (begin_trim >= end_trim) {
            return false;
        }
        const std::string_view trimmed
            = decoded_view.substr(begin_trim, end_trim - begin_trim);
        if (parse_decimal_u32(trimmed, 0U, trimmed.size(), out_index)) {
            return true;
        }

        const std::string lowered = ascii_lower(trimmed);
        static constexpr std::array<std::string_view, 3U> kMarkers = {
            std::string_view { "claims[" },
            std::string_view { "claims/" },
            std::string_view { "claims." },
        };
        for (const std::string_view marker : kMarkers) {
            size_t pos = 0U;
            while (true) {
                pos = lowered.find(marker, pos);
                if (pos == std::string_view::npos) {
                    break;
                }
                const size_t begin = pos + marker.size();
                size_t end         = begin;
                while (end < trimmed.size() && trimmed[end] >= '0'
                       && trimmed[end] <= '9') {
                    end += 1U;
                }
                if (end == begin) {
                    pos += 1U;
                    continue;
                }
                if (marker == std::string_view { "claims[" }) {
                    if (end >= trimmed.size() || trimmed[end] != ']') {
                        pos += 1U;
                        continue;
                    }
                }
                if (parse_decimal_u32(trimmed, begin, end, out_index)) {
                    return true;
                }
                pos += 1U;
            }
        }

        static constexpr std::array<std::string_view, 38U> kQueryFieldRoots = {
            std::string_view { "claim_ref" },
            std::string_view { "claim-ref" },
            std::string_view { "claimref" },
            std::string_view { "claim_reference" },
            std::string_view { "claim-reference" },
            std::string_view { "claimreference" },
            std::string_view { "claim_ref_id" },
            std::string_view { "claim-ref-id" },
            std::string_view { "claimrefid" },
            std::string_view { "claim_reference_id" },
            std::string_view { "claim-reference-id" },
            std::string_view { "claimreferenceid" },
            std::string_view { "claim_index" },
            std::string_view { "claim-index" },
            std::string_view { "claimindex" },
            std::string_view { "claim_id" },
            std::string_view { "claim-id" },
            std::string_view { "claimid" },
            std::string_view { "reference_id" },
            std::string_view { "reference-id" },
            std::string_view { "referenceid" },
            std::string_view { "references_id" },
            std::string_view { "references-id" },
            std::string_view { "referencesid" },
            std::string_view { "reference_index" },
            std::string_view { "reference-index" },
            std::string_view { "referenceindex" },
            std::string_view { "references_index" },
            std::string_view { "references-index" },
            std::string_view { "referencesindex" },
            std::string_view { "ref_id" },
            std::string_view { "ref-id" },
            std::string_view { "refid" },
            std::string_view { "ref_index" },
            std::string_view { "ref-index" },
            std::string_view { "refindex" },
            std::string_view { "claim" },
            std::string_view { "claims" },
        };
        for (const std::string_view root : kQueryFieldRoots) {
            size_t pos = 0U;
            while (true) {
                pos = lowered.find(root, pos);
                if (pos == std::string_view::npos) {
                    break;
                }
                if (pos > 0U && !reference_token_boundary(trimmed[pos - 1U])) {
                    pos += 1U;
                    continue;
                }
                size_t value_pos = pos + root.size();
                while (value_pos < trimmed.size()
                       && static_cast<unsigned char>(trimmed[value_pos])
                              <= 0x20U) {
                    value_pos += 1U;
                }
                if (value_pos >= trimmed.size()
                    || (trimmed[value_pos] != '='
                        && trimmed[value_pos] != ':')) {
                    pos += 1U;
                    continue;
                }
                value_pos += 1U;
                while (value_pos < trimmed.size()
                       && static_cast<unsigned char>(trimmed[value_pos])
                              <= 0x20U) {
                    value_pos += 1U;
                }
                size_t value_end = value_pos;
                while (value_end < trimmed.size() && trimmed[value_end] >= '0'
                       && trimmed[value_end] <= '9') {
                    value_end += 1U;
                }
                if (value_end == value_pos) {
                    pos += 1U;
                    continue;
                }
                if (value_end < trimmed.size()
                    && !reference_token_boundary(trimmed[value_end])) {
                    pos += 1U;
                    continue;
                }
                if (parse_decimal_u32(trimmed, value_pos, value_end,
                                      out_index)) {
                    return true;
                }
                pos += 1U;
            }
        }
        return false;
    }

    static bool claim_label_from_reference_text(std::string_view reference,
                                                std::string* out_label) noexcept
    {
        if (!out_label || reference.empty()) {
            return false;
        }
        out_label->clear();

        const std::string decoded_reference = uri_percent_decode(reference);
        if (decoded_reference.empty()) {
            return false;
        }
        const std::string lowered = ascii_lower(decoded_reference);
        size_t token_pos          = lowered.find("jumbf=");
        if (token_pos == std::string::npos) {
            token_pos = lowered.find("c2pa.claim");
            if (token_pos == std::string::npos) {
                return false;
            }
        } else {
            token_pos += 6U;
            if (token_pos >= decoded_reference.size()) {
                return false;
            }
        }

        size_t token_end = token_pos;
        while (token_end < decoded_reference.size()) {
            const char c = decoded_reference[token_end];
            if (c == '&' || c == '?' || c == ';' || c == ' ' || c == '\t'
                || c == '\r' || c == '\n' || c == '#') {
                break;
            }
            token_end += 1U;
        }
        if (token_end <= token_pos) {
            return false;
        }

        while (token_pos < token_end
               && (decoded_reference[token_pos] == '#'
                   || decoded_reference[token_pos] == '/')) {
            token_pos += 1U;
        }
        if (token_pos >= token_end) {
            return false;
        }

        {
            const std::string_view candidate
                = std::string_view(decoded_reference.data() + token_pos,
                                   token_end - token_pos);
            const std::string candidate_lower = ascii_lower(candidate);
            const size_t claim_pos = candidate_lower.find("c2pa.claim");
            if (claim_pos != std::string::npos) {
                token_pos += claim_pos;
                token_end = token_pos;
                while (token_end < decoded_reference.size()) {
                    const char c = decoded_reference[token_end];
                    if (c == '&' || c == '?' || c == ';' || c == '#' || c == '/'
                        || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                        break;
                    }
                    token_end += 1U;
                }
            }
        }

        while (token_end > token_pos) {
            const char tail = decoded_reference[token_end - 1U];
            if (tail == '/' || tail == '"' || tail == '\'' || tail == ']'
                || tail == ')') {
                token_end -= 1U;
                continue;
            }
            break;
        }
        if (token_end <= token_pos) {
            return false;
        }

        out_label->assign(
            decoded_reference.substr(token_pos, token_end - token_pos));
        return !out_label->empty();
    }

    static bool append_detached_payload_candidate_from_claim_index(
        const DecodeContext& ctx, std::string_view parent_prefix,
        uint32_t claim_index, uint64_t max_bytes,
        std::vector<std::vector<std::byte>>* out_candidates) noexcept
    {
        if (!ctx.store || !out_candidates || parent_prefix.empty()) {
            return false;
        }
        bool added = false;
        std::string claim_prefix(parent_prefix);
        claim_prefix.append(".claims[");
        claim_prefix.append(
            std::to_string(static_cast<unsigned long long>(claim_index)));
        claim_prefix.push_back(']');

        static constexpr std::array<std::string_view, 4U> kClaimFields = {
            std::string_view { ".claim" },
            std::string_view { ".claim_bytes" },
            std::string_view { ".claim_cbor" },
            std::string_view { ".claim_payload" },
        };

        std::string full_key;
        const std::span<const Entry> entries = ctx.store->entries();
        for (const std::string_view field : kClaimFields) {
            full_key.assign(claim_prefix);
            full_key.append(field.data(), field.size());
            for (const Entry& e : entries) {
                if (e.origin.block != ctx.block
                    || e.key.kind != MetaKeyKind::JumbfCborKey) {
                    continue;
                }
                const std::string_view key
                    = arena_string_view(ctx.store->arena(),
                                        e.key.data.jumbf_cbor_key.key);
                if (key != full_key) {
                    continue;
                }
                if (e.value.kind != MetaValueKind::Bytes
                    && !(e.value.kind == MetaValueKind::Array
                         && e.value.elem_type == MetaElementType::U8)) {
                    continue;
                }
                const std::span<const std::byte> payload
                    = ctx.store->arena().span(e.value.data.span);
                append_unique_detached_payload_candidate(payload, max_bytes,
                                                         out_candidates);
                added = true;
            }
        }
        return added;
    }

    static std::string claim_scope_prefix_from_signature_prefix(
        std::string_view signature_prefix,
        std::string_view parent_prefix) noexcept
    {
        std::string claim_item_prefix;
        if (!find_indexed_segment_prefix(signature_prefix, ".claims[",
                                         &claim_item_prefix)) {
            return std::string(parent_prefix);
        }
        const size_t marker_pos = claim_item_prefix.rfind(".claims[");
        if (marker_pos == std::string::npos) {
            return std::string(parent_prefix);
        }
        return claim_item_prefix.substr(0U, marker_pos);
    }

    static void collect_detached_payload_candidates_from_claim_box_label(
        const DecodeContext& ctx, std::string_view label_token,
        uint64_t max_bytes,
        std::vector<std::vector<std::byte>>* out_candidates) noexcept
    {
        if (!out_candidates || label_token.empty()) {
            return;
        }
        const uint32_t jumb_type = fourcc('j', 'u', 'm', 'b');
        const uint32_t cbor_type = fourcc('c', 'b', 'o', 'r');
        for (size_t i = 0U; i < ctx.boxes.size(); ++i) {
            const DecodeContext::ParsedBox& box = ctx.boxes[i];
            if (box.type != jumb_type || !box.has_jumb_label
                || !ascii_icase_contains_text(box.jumb_label, "claim")) {
                continue;
            }
            if (!ascii_icase_contains_text(box.jumb_label, label_token)
                && !ascii_icase_contains_text(label_token, box.jumb_label)) {
                continue;
            }
            const int32_t claim_cbor_index = find_first_descendant_box_of_type(
                ctx, static_cast<int32_t>(i), cbor_type);
            if (claim_cbor_index < 0) {
                continue;
            }
            const std::span<const std::byte> payload
                = ctx.boxes[static_cast<size_t>(claim_cbor_index)].payload;
            append_unique_detached_payload_candidate(payload, max_bytes,
                                                     out_candidates);
        }
    }

    static bool claim_label_has_numeric_suffix(std::string_view label,
                                               uint32_t claim_index) noexcept
    {
        const std::string index_text
            = std::to_string(static_cast<unsigned long long>(claim_index));
        if (!string_ends_with(label, index_text)) {
            return false;
        }
        const size_t prefix_size = label.size() - index_text.size();
        if (prefix_size != 0U) {
            const char prev = label[prefix_size - 1U];
            if (prev >= '0' && prev <= '9') {
                return false;
            }
        }
        return true;
    }

    static void collect_detached_payload_candidates_from_claim_box_index_suffix(
        const DecodeContext& ctx, uint32_t claim_index, uint64_t max_bytes,
        std::vector<std::vector<std::byte>>* out_candidates) noexcept
    {
        if (!out_candidates) {
            return;
        }
        const uint32_t jumb_type = fourcc('j', 'u', 'm', 'b');
        const uint32_t cbor_type = fourcc('c', 'b', 'o', 'r');
        for (size_t i = 0U; i < ctx.boxes.size(); ++i) {
            const DecodeContext::ParsedBox& box = ctx.boxes[i];
            if (box.type != jumb_type || !box.has_jumb_label
                || !ascii_icase_contains_text(box.jumb_label, "claim")
                || !claim_label_has_numeric_suffix(box.jumb_label,
                                                   claim_index)) {
                continue;
            }
            const int32_t claim_cbor_index = find_first_descendant_box_of_type(
                ctx, static_cast<int32_t>(i), cbor_type);
            if (claim_cbor_index < 0) {
                continue;
            }
            const std::span<const std::byte> payload
                = ctx.boxes[static_cast<size_t>(claim_cbor_index)].payload;
            append_unique_detached_payload_candidate(payload, max_bytes,
                                                     out_candidates);
        }
    }

    static bool collect_detached_payload_candidates_from_claim_references(
        const DecodeContext& ctx, const C2paVerifySignatureCandidate& candidate,
        uint64_t max_bytes, std::vector<std::vector<std::byte>>* out_candidates,
        bool* out_saw_reference, uint64_t* out_index_hits,
        uint64_t* out_label_hits) noexcept
    {
        if (!ctx.store || !out_candidates) {
            return false;
        }
        if (out_saw_reference) {
            *out_saw_reference = false;
        }
        if (out_index_hits) {
            *out_index_hits = 0U;
        }
        if (out_label_hits) {
            *out_label_hits = 0U;
        }
        const std::string_view signature_prefix(candidate.prefix);
        const size_t marker_pos = signature_prefix.rfind(".signatures[");
        if (marker_pos == std::string_view::npos) {
            return false;
        }
        const std::string parent_prefix(
            signature_prefix.substr(0U, marker_pos));
        if (parent_prefix.empty()) {
            return false;
        }
        const std::string claim_scope_prefix
            = claim_scope_prefix_from_signature_prefix(signature_prefix,
                                                       parent_prefix);

        std::vector<uint32_t> referenced_claim_indices;
        referenced_claim_indices.reserve(8U);
        std::vector<std::string> referenced_claim_labels;
        referenced_claim_labels.reserve(8U);
        bool saw_nested_index_reference = false;
        const std::span<const Entry> entries = ctx.store->entries();
        for (const Entry& e : entries) {
            if (e.origin.block != ctx.block
                || e.key.kind != MetaKeyKind::JumbfCborKey) {
                continue;
            }
            const std::string_view key
                = arena_string_view(ctx.store->arena(),
                                    e.key.data.jumbf_cbor_key.key);
            const bool is_claim_reference = cbor_key_is_claim_reference_field(
                key);
            const bool is_reference_index = cbor_key_is_reference_index_field(
                key);
            const bool is_reference_id = cbor_key_is_reference_id_field(key);
            if (!key_is_in_prefix_scope(key, signature_prefix)
                || (!is_claim_reference && !is_reference_index
                    && !is_reference_id)) {
                continue;
            }
            if (out_saw_reference) {
                *out_saw_reference = true;
            }
            const bool is_nested_reference
                = (key.find(".references[", signature_prefix.size())
                   != std::string_view::npos);

            uint32_t claim_index = 0U;
            if (claim_reference_index_from_scalar_entry(e, &claim_index)) {
                if (!vector_contains_u32(referenced_claim_indices,
                                         claim_index)) {
                    referenced_claim_indices.push_back(claim_index);
                }
                if (is_nested_reference) {
                    saw_nested_index_reference = true;
                }
            }

            std::string reference;
            if (!claim_reference_text_from_entry(ctx, e, &reference)) {
                continue;
            }

            claim_index = 0U;
            if (claim_index_from_reference_text(reference, &claim_index)) {
                if (!vector_contains_u32(referenced_claim_indices,
                                         claim_index)) {
                    referenced_claim_indices.push_back(claim_index);
                }
                if (is_nested_reference) {
                    saw_nested_index_reference = true;
                }
            }

            std::string claim_label;
            if (claim_label_from_reference_text(reference, &claim_label)) {
                append_unique_string_value(&referenced_claim_labels,
                                           claim_label);
            }
        }

        sort_unique_u32(&referenced_claim_indices);
        sort_unique_strings(&referenced_claim_labels);
        if (out_index_hits) {
            *out_index_hits = static_cast<uint64_t>(
                referenced_claim_indices.size());
        }
        if (out_label_hits) {
            *out_label_hits = static_cast<uint64_t>(
                referenced_claim_labels.size());
        }

        std::vector<std::vector<std::byte>> index_candidates;
        index_candidates.reserve(referenced_claim_indices.size());
        for (const uint32_t claim_index : referenced_claim_indices) {
            (void)append_detached_payload_candidate_from_claim_index(
                ctx, claim_scope_prefix, claim_index, max_bytes,
                &index_candidates);
            if (saw_nested_index_reference) {
                collect_detached_payload_candidates_from_claim_box_index_suffix(
                    ctx, claim_index, max_bytes, &index_candidates);
            }
        }

        std::vector<std::vector<std::byte>> label_candidates;
        label_candidates.reserve(referenced_claim_labels.size());
        for (const std::string& claim_label : referenced_claim_labels) {
            collect_detached_payload_candidates_from_claim_box_label(
                ctx, claim_label, max_bytes, &label_candidates);
        }

        const size_t before = out_candidates->size();
        append_detached_payload_candidates_in_order(index_candidates, max_bytes,
                                                    out_candidates);
        append_detached_payload_candidates_in_order(label_candidates, max_bytes,
                                                    out_candidates);
        const bool added_any = out_candidates->size() > before;
        return added_any;
    }

    static void append_detached_payload_candidates_in_order(
        const std::vector<std::vector<std::byte>>& candidates,
        uint64_t max_bytes,
        std::vector<std::vector<std::byte>>* out_candidates) noexcept
    {
        if (!out_candidates) {
            return;
        }
        for (const std::vector<std::byte>& payload : candidates) {
            if (payload.empty()) {
                continue;
            }
            append_unique_detached_payload_candidate(
                std::span<const std::byte>(payload.data(), payload.size()),
                max_bytes, out_candidates);
        }
    }

    static void collect_detached_payload_candidates_from_claim_keys(
        const DecodeContext& ctx, uint64_t max_bytes,
        std::vector<std::vector<std::byte>>* out_candidates) noexcept
    {
        if (!ctx.store || !out_candidates) {
            return;
        }
        const std::span<const Entry> entries = ctx.store->entries();
        for (const Entry& e : entries) {
            if (e.origin.block != ctx.block
                || e.key.kind != MetaKeyKind::JumbfCborKey) {
                continue;
            }
            if (e.value.kind != MetaValueKind::Bytes
                && !(e.value.kind == MetaValueKind::Array
                     && e.value.elem_type == MetaElementType::U8)) {
                continue;
            }
            const std::string_view key
                = arena_string_view(ctx.store->arena(),
                                    e.key.data.jumbf_cbor_key.key);
            if (!cbor_key_has_segment(key, "claims")
                || !cbor_key_is_claim_payload_field(key)) {
                continue;
            }
            const std::span<const std::byte> payload = ctx.store->arena().span(
                e.value.data.span);
            append_unique_detached_payload_candidate(payload, max_bytes,
                                                     out_candidates);
        }
    }

    static void collect_detached_payload_candidates_from_claim_boxes(
        const DecodeContext& ctx, uint64_t max_bytes,
        std::vector<std::vector<std::byte>>* out_candidates) noexcept
    {
        if (!out_candidates) {
            return;
        }
        const uint32_t jumb_type = fourcc('j', 'u', 'm', 'b');
        const uint32_t cbor_type = fourcc('c', 'b', 'o', 'r');
        for (size_t i = 0U; i < ctx.boxes.size(); ++i) {
            const DecodeContext::ParsedBox& box = ctx.boxes[i];
            if (box.type != jumb_type || !box.has_jumb_label
                || !ascii_icase_contains_text(box.jumb_label, "claim")) {
                continue;
            }
            const int32_t claim_cbor_index = find_first_descendant_box_of_type(
                ctx, static_cast<int32_t>(i), cbor_type);
            if (claim_cbor_index < 0) {
                continue;
            }
            const std::span<const std::byte> payload
                = ctx.boxes[static_cast<size_t>(claim_cbor_index)].payload;
            append_unique_detached_payload_candidate(payload, max_bytes,
                                                     out_candidates);
        }
    }

    static void collect_detached_payload_candidates(
        const DecodeContext& ctx, const C2paVerifySignatureCandidate& candidate,
        std::vector<std::vector<std::byte>>* out_candidates) noexcept
    {
        if (!out_candidates) {
            return;
        }
        out_candidates->clear();
        const uint64_t max_bytes
            = cbor_limit_or_default(ctx.options.limits.max_cbor_bytes_bytes,
                                    8U * 1024U * 1024U);

        bool saw_reference = false;
        const bool have_reference_candidates
            = collect_detached_payload_candidates_from_claim_references(
                ctx, candidate, max_bytes, out_candidates, &saw_reference,
                nullptr, nullptr);
        if (saw_reference || have_reference_candidates) {
            return;
        }

        std::vector<std::byte> detached;
        if (find_detached_payload_from_claim_bytes(ctx, candidate, &detached)) {
            append_unique_detached_payload_candidate(
                std::span<const std::byte>(detached.data(), detached.size()),
                max_bytes, out_candidates);
        }
        if (candidate.has_source_cbor_box_index) {
            detached.clear();
            if (find_detached_payload_from_claim_box_layout(
                    ctx, candidate.source_cbor_box_index, &detached)) {
                append_unique_detached_payload_candidate(
                    std::span<const std::byte>(detached.data(), detached.size()),
                    max_bytes, out_candidates);
            }
        }
        collect_detached_payload_candidates_from_claim_keys(ctx, max_bytes,
                                                            out_candidates);
        collect_detached_payload_candidates_from_claim_boxes(ctx, max_bytes,
                                                             out_candidates);
    }

    [[maybe_unused]] static bool
    collect_c2pa_profile_summary(DecodeContext* ctx,
                                 C2paProfileSummary* out) noexcept
    {
        if (!ctx || !ctx->store || !out) {
            return false;
        }
        *out = C2paProfileSummary {};

        uint64_t value = 0U;
        bool have_any  = false;

        if (read_jumbf_field_u64(*ctx, "c2pa.semantic.claim_count", &value)) {
            out->claim_count = value;
            have_any         = true;
        }
        if (read_jumbf_field_u64(*ctx, "c2pa.semantic.signature_count",
                                 &value)) {
            out->signature_count = value;
            have_any             = true;
        }
        if (read_jumbf_field_u64(*ctx, "c2pa.semantic.signature_linked_count",
                                 &value)) {
            out->signature_linked = value;
            have_any              = true;
        }
        if (read_jumbf_field_u64(*ctx, "c2pa.semantic.signature_orphan_count",
                                 &value)) {
            out->signature_orphan = value;
            have_any              = true;
        }
        if (read_jumbf_field_u64(
                *ctx, "c2pa.semantic.explicit_reference_signature_count",
                &value)) {
            out->explicit_reference_signature_count = value;
            have_any                                = true;
        }
        if (read_jumbf_field_u64(
                *ctx,
                "c2pa.semantic.explicit_reference_unresolved_signature_count",
                &value)) {
            out->explicit_reference_unresolved_signature_count = value;
            have_any                                           = true;
        }
        if (read_jumbf_field_u64(
                *ctx,
                "c2pa.semantic.explicit_reference_ambiguous_signature_count",
                &value)) {
            out->explicit_reference_ambiguous_signature_count = value;
            have_any                                          = true;
        }
        if (read_jumbf_field_u64(*ctx, "c2pa.semantic.manifest_present",
                                 &value)) {
            out->manifest_present = value;
            have_any              = true;
        }
        if (read_jumbf_field_u64(*ctx, "c2pa.semantic.claim_present", &value)) {
            out->claim_present = value;
            have_any           = true;
        }
        if (read_jumbf_field_u64(*ctx, "c2pa.semantic.signature_present",
                                 &value)) {
            out->signature_present = value;
            have_any               = true;
        }

        out->available = have_any;
        return true;
    }

    [[maybe_unused]] static void
    evaluate_c2pa_profile_summary(const C2paProfileSummary& summary,
                                  bool require_resolved_explicit_references,
                                  C2paVerifyEvaluation* evaluation) noexcept
    {
        if (!evaluation) {
            return;
        }
        evaluation->profile_summary = summary;
        if (!summary.available) {
            evaluation->profile_status = C2paVerifyDetailStatus::NotChecked;
            evaluation->profile_reason = "semantic_fields_missing";
            return;
        }

        if (summary.manifest_present == 0U) {
            evaluation->profile_status = C2paVerifyDetailStatus::Fail;
            evaluation->profile_reason = "manifest_missing";
            return;
        }
        if (summary.claim_count == 0U || summary.claim_present == 0U) {
            evaluation->profile_status = C2paVerifyDetailStatus::Fail;
            evaluation->profile_reason = "claim_missing";
            return;
        }
        if (summary.signature_count == 0U || summary.signature_present == 0U) {
            evaluation->profile_status = C2paVerifyDetailStatus::Fail;
            evaluation->profile_reason = "signature_missing";
            return;
        }
        if (summary.signature_linked > summary.signature_count) {
            evaluation->profile_status = C2paVerifyDetailStatus::Fail;
            evaluation->profile_reason = "signature_linked_count_invalid";
            return;
        }
        if (summary.signature_orphan > summary.signature_count) {
            evaluation->profile_status = C2paVerifyDetailStatus::Fail;
            evaluation->profile_reason = "signature_orphan_count_invalid";
            return;
        }
        if ((summary.signature_linked + summary.signature_orphan)
            != summary.signature_count) {
            evaluation->profile_status = C2paVerifyDetailStatus::Fail;
            evaluation->profile_reason = "signature_link_consistency_invalid";
            return;
        }
        if (summary.explicit_reference_signature_count
            > summary.signature_count) {
            evaluation->profile_status = C2paVerifyDetailStatus::Fail;
            evaluation->profile_reason = "explicit_reference_count_invalid";
            return;
        }
        if (summary.explicit_reference_unresolved_signature_count
            > summary.explicit_reference_signature_count) {
            evaluation->profile_status = C2paVerifyDetailStatus::Fail;
            evaluation->profile_reason
                = "explicit_reference_unresolved_count_invalid";
            return;
        }
        if (summary.explicit_reference_ambiguous_signature_count
            > summary.explicit_reference_signature_count) {
            evaluation->profile_status = C2paVerifyDetailStatus::Fail;
            evaluation->profile_reason
                = "explicit_reference_ambiguous_count_invalid";
            return;
        }
        if (summary.explicit_reference_unresolved_signature_count
            > (summary.explicit_reference_signature_count
               - summary.explicit_reference_ambiguous_signature_count)) {
            evaluation->profile_status = C2paVerifyDetailStatus::Fail;
            evaluation->profile_reason
                = "explicit_reference_consistency_invalid";
            return;
        }
        if (require_resolved_explicit_references
            && summary.explicit_reference_signature_count != 0U) {
            if (summary.explicit_reference_unresolved_signature_count != 0U) {
                evaluation->profile_status = C2paVerifyDetailStatus::Fail;
                evaluation->profile_reason = "explicit_reference_unresolved";
                return;
            }
            if (summary.explicit_reference_ambiguous_signature_count != 0U) {
                evaluation->profile_status = C2paVerifyDetailStatus::Fail;
                evaluation->profile_reason = "explicit_reference_ambiguous";
                return;
            }
        }
        if (summary.signature_linked == 0U) {
            if (summary.explicit_reference_signature_count != 0U) {
                evaluation->profile_status
                    = C2paVerifyDetailStatus::NotChecked;
                evaluation->profile_reason = "signature_unlinked";
                return;
            }
            evaluation->profile_status = C2paVerifyDetailStatus::Fail;
            evaluation->profile_reason = "signature_unlinked";
            return;
        }

        evaluation->profile_status = C2paVerifyDetailStatus::Pass;
        evaluation->profile_reason = "ok";
    }

    static uint8_t lower_ascii(uint8_t c) noexcept
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<uint8_t>(c + 32U) : c;
    }

    static std::string ascii_lower(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (char ch : text) {
            out.push_back(
                static_cast<char>(lower_ascii(static_cast<uint8_t>(ch))));
        }
        return out;
    }

    static bool ascii_icase_contains_text(std::string_view haystack,
                                          std::string_view needle) noexcept
    {
        if (needle.empty() || haystack.size() < needle.size()) {
            return false;
        }
        for (size_t i = 0U; i + needle.size() <= haystack.size(); ++i) {
            bool match = true;
            for (size_t j = 0U; j < needle.size(); ++j) {
                const uint8_t a = lower_ascii(
                    static_cast<uint8_t>(haystack[i + j]));
                const uint8_t b = lower_ascii(static_cast<uint8_t>(needle[j]));
                if (a != b) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
        return false;
    }

    static bool parse_jumd_label(std::span<const std::byte> payload,
                                 std::string* out_label) noexcept
    {
        if (!out_label) {
            return false;
        }
        out_label->clear();
        if (payload.empty()) {
            return false;
        }

        size_t start = 0U;
        if (payload.size() >= 4U + 16U + 1U) {
            start = 20U;
        }
        if (start >= payload.size()) {
            return false;
        }

        size_t end = start;
        while (end < payload.size() && payload[end] != std::byte { 0x00 }) {
            end += 1U;
        }
        if (end <= start) {
            return false;
        }
        const size_t len = end - start;
        if (len > 256U) {
            return false;
        }
        for (size_t i = 0U; i < len; ++i) {
            if (!is_printable_ascii(u8(payload[start + i]))) {
                return false;
            }
        }
        out_label->assign(reinterpret_cast<const char*>(payload.data() + start),
                          len);
        return !out_label->empty();
    }

    static uint64_t cbor_limit_or_default(uint64_t configured,
                                          uint64_t default_value) noexcept;

    static int32_t find_ancestor_box_of_type(const DecodeContext& ctx,
                                             int32_t start_index,
                                             uint32_t type) noexcept
    {
        if (start_index < 0
            || static_cast<size_t>(start_index) >= ctx.boxes.size()) {
            return -1;
        }
        const uint32_t max_hops = (ctx.options.limits.max_box_depth != 0U)
                                      ? (ctx.options.limits.max_box_depth + 1U)
                                      : 64U;
        int32_t current
            = ctx.boxes[static_cast<size_t>(start_index)].parent_index;
        for (uint32_t hops = 0U; current >= 0 && hops < max_hops; ++hops) {
            if (static_cast<size_t>(current) >= ctx.boxes.size()) {
                return -1;
            }
            if (ctx.boxes[static_cast<size_t>(current)].type == type) {
                return current;
            }
            current = ctx.boxes[static_cast<size_t>(current)].parent_index;
        }
        return -1;
    }

    static bool box_is_descendant_of(const DecodeContext& ctx,
                                     int32_t node_index,
                                     int32_t ancestor_index) noexcept
    {
        if (node_index < 0 || ancestor_index < 0
            || static_cast<size_t>(node_index) >= ctx.boxes.size()
            || static_cast<size_t>(ancestor_index) >= ctx.boxes.size()) {
            return false;
        }
        const uint32_t max_hops = (ctx.options.limits.max_box_depth != 0U)
                                      ? (ctx.options.limits.max_box_depth + 1U)
                                      : 64U;
        int32_t current         = node_index;
        for (uint32_t hops = 0U; current >= 0 && hops < max_hops; ++hops) {
            if (current == ancestor_index) {
                return true;
            }
            current = ctx.boxes[static_cast<size_t>(current)].parent_index;
        }
        return false;
    }

    static int32_t find_first_descendant_box_of_type(const DecodeContext& ctx,
                                                     int32_t ancestor_index,
                                                     uint32_t type) noexcept
    {
        if (ancestor_index < 0
            || static_cast<size_t>(ancestor_index) >= ctx.boxes.size()) {
            return -1;
        }
        for (size_t i = 0U; i < ctx.boxes.size(); ++i) {
            const DecodeContext::ParsedBox& box = ctx.boxes[i];
            if (box.type != type) {
                continue;
            }
            if (box_is_descendant_of(ctx, static_cast<int32_t>(i),
                                     ancestor_index)) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    static bool prefer_claim_candidate(int32_t candidate_index,
                                       bool candidate_before,
                                       uint32_t candidate_distance,
                                       int32_t best_index, bool best_before,
                                       uint32_t best_distance) noexcept
    {
        if (best_index < 0) {
            return true;
        }
        if (candidate_before != best_before) {
            return candidate_before;
        }
        if (candidate_distance != best_distance) {
            return candidate_distance < best_distance;
        }
        if (candidate_before) {
            return candidate_index > best_index;
        }
        return candidate_index < best_index;
    }

    static bool find_best_claim_jumb_candidate(
        const DecodeContext& ctx, int32_t signature_jumb_index,
        int32_t manifest_jumb_index, bool require_manifest_match,
        int32_t* out_claim_jumb_index, int32_t* out_claim_cbor_index) noexcept
    {
        if (!out_claim_jumb_index || !out_claim_cbor_index) {
            return false;
        }
        *out_claim_jumb_index = -1;
        *out_claim_cbor_index = -1;

        const uint32_t jumb_type = fourcc('j', 'u', 'm', 'b');
        const uint32_t cbor_type = fourcc('c', 'b', 'o', 'r');

        int32_t best_claim_index = -1;
        int32_t best_cbor_index  = -1;
        bool best_before         = false;
        uint32_t best_distance   = UINT32_MAX;

        for (size_t i = 0U; i < ctx.boxes.size(); ++i) {
            const DecodeContext::ParsedBox& box = ctx.boxes[i];
            if (box.type != jumb_type || !box.has_jumb_label) {
                continue;
            }
            if (!ascii_icase_contains_text(box.jumb_label, "claim")) {
                continue;
            }
            const int32_t claim_jumb_index = static_cast<int32_t>(i);
            if (claim_jumb_index == signature_jumb_index) {
                continue;
            }
            if (require_manifest_match && manifest_jumb_index >= 0
                && !box_is_descendant_of(ctx, claim_jumb_index,
                                         manifest_jumb_index)) {
                continue;
            }

            const int32_t claim_cbor_index
                = find_first_descendant_box_of_type(ctx, claim_jumb_index,
                                                    cbor_type);
            if (claim_cbor_index < 0) {
                continue;
            }

            const bool before       = claim_jumb_index < signature_jumb_index;
            const uint32_t distance = static_cast<uint32_t>(
                before ? (signature_jumb_index - claim_jumb_index)
                       : (claim_jumb_index - signature_jumb_index));
            if (!prefer_claim_candidate(claim_jumb_index, before, distance,
                                        best_claim_index, best_before,
                                        best_distance)) {
                continue;
            }
            best_claim_index = claim_jumb_index;
            best_cbor_index  = claim_cbor_index;
            best_before      = before;
            best_distance    = distance;
        }

        if (best_claim_index < 0 || best_cbor_index < 0) {
            return false;
        }
        *out_claim_jumb_index = best_claim_index;
        *out_claim_cbor_index = best_cbor_index;
        return true;
    }

    static bool collect_c2pa_label_summary(
        const DecodeContext& ctx, uint64_t* out_manifest_present,
        uint64_t* out_claim_count, uint64_t* out_signature_count,
        uint64_t* out_signature_linked, uint64_t* out_signature_orphan) noexcept
    {
        if (!out_manifest_present || !out_claim_count || !out_signature_count
            || !out_signature_linked || !out_signature_orphan) {
            return false;
        }
        *out_manifest_present = 0U;
        *out_claim_count      = 0U;
        *out_signature_count  = 0U;
        *out_signature_linked = 0U;
        *out_signature_orphan = 0U;

        struct ManifestCounts final {
            int32_t manifest_index   = -1;
            uint64_t claim_count     = 0U;
            uint64_t signature_count = 0U;
        };
        std::vector<ManifestCounts> manifests;
        manifests.reserve(8U);

        const uint32_t jumb_type = fourcc('j', 'u', 'm', 'b');
        for (size_t i = 0U; i < ctx.boxes.size(); ++i) {
            const DecodeContext::ParsedBox& box = ctx.boxes[i];
            if (box.type != jumb_type || !box.has_jumb_label) {
                continue;
            }
            const std::string_view label(box.jumb_label);
            const bool is_claim = ascii_icase_contains_text(label, "claim");
            const bool is_sig   = ascii_icase_contains_text(label, "signature");
            if (!is_claim && !is_sig) {
                continue;
            }
            const int32_t manifest_index
                = find_ancestor_box_of_type(ctx, static_cast<int32_t>(i),
                                            jumb_type);
            if (manifest_index < 0) {
                continue;
            }

            size_t manifest_slot = static_cast<size_t>(-1);
            for (size_t m = 0U; m < manifests.size(); ++m) {
                if (manifests[m].manifest_index == manifest_index) {
                    manifest_slot = m;
                    break;
                }
            }
            if (manifest_slot == static_cast<size_t>(-1)) {
                ManifestCounts counts;
                counts.manifest_index = manifest_index;
                manifests.push_back(counts);
                manifest_slot = manifests.size() - 1U;
            }
            if (is_claim) {
                manifests[manifest_slot].claim_count += 1U;
            }
            if (is_sig) {
                manifests[manifest_slot].signature_count += 1U;
            }
        }

        *out_manifest_present = manifests.size();
        for (const ManifestCounts& manifest : manifests) {
            *out_claim_count += manifest.claim_count;
            *out_signature_count += manifest.signature_count;
            const uint64_t linked = (manifest.claim_count
                                     < manifest.signature_count)
                                        ? manifest.claim_count
                                        : manifest.signature_count;
            *out_signature_linked += linked;
            if (manifest.signature_count > linked) {
                *out_signature_orphan += (manifest.signature_count - linked);
            }
        }

        return *out_manifest_present != 0U || *out_claim_count != 0U
               || *out_signature_count != 0U;
    }

    static bool find_detached_payload_from_claim_box_layout(
        const DecodeContext& ctx, uint32_t signature_cbor_box_index,
        std::vector<std::byte>* out_payload) noexcept
    {
        if (!out_payload) {
            return false;
        }
        out_payload->clear();

        if (signature_cbor_box_index >= ctx.boxes.size()) {
            return false;
        }

        const uint32_t jumb_type = fourcc('j', 'u', 'm', 'b');

        const int32_t signature_cbor_index = static_cast<int32_t>(
            signature_cbor_box_index);
        const int32_t signature_jumb_index
            = find_ancestor_box_of_type(ctx, signature_cbor_index, jumb_type);
        if (signature_jumb_index < 0) {
            return false;
        }
        const int32_t manifest_jumb_index
            = find_ancestor_box_of_type(ctx, signature_jumb_index, jumb_type);
        if (manifest_jumb_index < 0) {
            return false;
        }
        int32_t claim_jumb_index = -1;
        int32_t claim_cbor_index = -1;
        if (!find_best_claim_jumb_candidate(ctx, signature_jumb_index,
                                            manifest_jumb_index, true,
                                            &claim_jumb_index,
                                            &claim_cbor_index)) {
            if (!find_best_claim_jumb_candidate(ctx, signature_jumb_index,
                                                manifest_jumb_index, false,
                                                &claim_jumb_index,
                                                &claim_cbor_index)) {
                return false;
            }
        }

        const uint64_t max_bytes
            = cbor_limit_or_default(ctx.options.limits.max_cbor_bytes_bytes,
                                    8U * 1024U * 1024U);
        const std::span<const std::byte> payload
            = ctx.boxes[static_cast<size_t>(claim_cbor_index)].payload;
        if (max_bytes != 0U && payload.size() > max_bytes) {
            return false;
        }

        out_payload->assign(payload.begin(), payload.end());
        return !out_payload->empty();
    }

    [[maybe_unused]] static bool
    is_alg_ecdsa(std::string_view algorithm) noexcept
    {
        return algorithm == "es256" || algorithm == "es384"
               || algorithm == "es512";
    }

    [[maybe_unused]] static bool
    is_alg_eddsa(std::string_view algorithm) noexcept
    {
        return algorithm == "ed25519" || algorithm == "eddsa";
    }

    [[maybe_unused]] static bool is_alg_rsa(std::string_view algorithm) noexcept
    {
        return algorithm == "rs256" || algorithm == "rs384"
               || algorithm == "rs512" || algorithm == "ps256"
               || algorithm == "ps384" || algorithm == "ps512";
    }

    struct CborHeadLite final {
        uint8_t major      = 0U;
        uint8_t addl       = 0U;
        uint64_t arg       = 0U;
        bool indefinite    = false;
        bool is_break_item = false;
    };

    static bool cbor_lite_peek_break(std::span<const std::byte> bytes,
                                     size_t pos) noexcept
    {
        return pos < bytes.size() && u8(bytes[pos]) == 0xFFU;
    }

    static bool cbor_lite_read_head(std::span<const std::byte> bytes,
                                    size_t* pos, CborHeadLite* out) noexcept
    {
        if (!pos || !out || *pos >= bytes.size()) {
            return false;
        }
        const uint8_t ib = u8(bytes[*pos]);
        *pos += 1U;

        out->major         = static_cast<uint8_t>((ib >> 5U) & 0x07U);
        out->addl          = static_cast<uint8_t>(ib & 0x1FU);
        out->arg           = 0U;
        out->indefinite    = false;
        out->is_break_item = false;

        if (out->major == 7U && out->addl == 31U) {
            out->is_break_item = true;
            return true;
        }

        if (out->addl <= 23U) {
            out->arg = static_cast<uint64_t>(out->addl);
            return true;
        }
        if (out->addl == 24U) {
            if (*pos + 1U > bytes.size()) {
                return false;
            }
            out->arg = static_cast<uint64_t>(u8(bytes[*pos]));
            *pos += 1U;
            return true;
        }
        if (out->addl == 25U) {
            if (*pos + 2U > bytes.size()) {
                return false;
            }
            out->arg = (static_cast<uint64_t>(u8(bytes[*pos + 0U])) << 8U)
                       | static_cast<uint64_t>(u8(bytes[*pos + 1U]));
            *pos += 2U;
            return true;
        }
        if (out->addl == 26U) {
            if (*pos + 4U > bytes.size()) {
                return false;
            }
            out->arg = (static_cast<uint64_t>(u8(bytes[*pos + 0U])) << 24U)
                       | (static_cast<uint64_t>(u8(bytes[*pos + 1U])) << 16U)
                       | (static_cast<uint64_t>(u8(bytes[*pos + 2U])) << 8U)
                       | (static_cast<uint64_t>(u8(bytes[*pos + 3U])) << 0U);
            *pos += 4U;
            return true;
        }
        if (out->addl == 27U) {
            if (*pos + 8U > bytes.size()) {
                return false;
            }
            uint64_t value = 0U;
            for (uint32_t i = 0U; i < 8U; ++i) {
                value = (value << 8U)
                        | static_cast<uint64_t>(u8(bytes[*pos + i]));
            }
            out->arg = value;
            *pos += 8U;
            return true;
        }
        if (out->addl == 31U) {
            out->indefinite = true;
            return out->major != 7U;
        }
        return false;
    }

    static bool cbor_lite_skip_item(std::span<const std::byte> bytes,
                                    size_t* pos, uint32_t depth) noexcept;

    static bool cbor_lite_skip_from_head(std::span<const std::byte> bytes,
                                         size_t* pos, uint32_t depth,
                                         const CborHeadLite& head) noexcept
    {
        if (!pos || depth > 64U) {
            return false;
        }
        if (head.is_break_item) {
            return false;
        }

        if (head.major == 0U || head.major == 1U || head.major == 7U) {
            return true;
        }
        if (head.major == 2U || head.major == 3U) {
            if (!head.indefinite) {
                if (*pos > bytes.size() || head.arg > bytes.size() - *pos) {
                    return false;
                }
                *pos += static_cast<size_t>(head.arg);
                return true;
            }
            while (true) {
                if (cbor_lite_peek_break(bytes, *pos)) {
                    *pos += 1U;
                    return true;
                }
                CborHeadLite chunk;
                if (!cbor_lite_read_head(bytes, pos, &chunk)
                    || chunk.is_break_item) {
                    return false;
                }
                if (chunk.major != head.major || chunk.indefinite) {
                    return false;
                }
                if (*pos > bytes.size() || chunk.arg > bytes.size() - *pos) {
                    return false;
                }
                *pos += static_cast<size_t>(chunk.arg);
            }
        }
        if (head.major == 4U) {
            if (!head.indefinite) {
                for (uint64_t i = 0U; i < head.arg; ++i) {
                    if (!cbor_lite_skip_item(bytes, pos, depth + 1U)) {
                        return false;
                    }
                }
                return true;
            }
            while (true) {
                if (cbor_lite_peek_break(bytes, *pos)) {
                    *pos += 1U;
                    return true;
                }
                if (!cbor_lite_skip_item(bytes, pos, depth + 1U)) {
                    return false;
                }
            }
        }
        if (head.major == 5U) {
            if (!head.indefinite) {
                for (uint64_t i = 0U; i < head.arg; ++i) {
                    if (!cbor_lite_skip_item(bytes, pos, depth + 1U)
                        || !cbor_lite_skip_item(bytes, pos, depth + 1U)) {
                        return false;
                    }
                }
                return true;
            }
            while (true) {
                if (cbor_lite_peek_break(bytes, *pos)) {
                    *pos += 1U;
                    return true;
                }
                if (!cbor_lite_skip_item(bytes, pos, depth + 1U)
                    || !cbor_lite_skip_item(bytes, pos, depth + 1U)) {
                    return false;
                }
            }
        }
        if (head.major == 6U) {
            if (head.indefinite) {
                return false;
            }
            return cbor_lite_skip_item(bytes, pos, depth + 1U);
        }
        return false;
    }

    static bool cbor_lite_skip_item(std::span<const std::byte> bytes,
                                    size_t* pos, uint32_t depth) noexcept
    {
        if (!pos || *pos > bytes.size()) {
            return false;
        }
        CborHeadLite head;
        if (!cbor_lite_read_head(bytes, pos, &head) || head.is_break_item) {
            return false;
        }
        return cbor_lite_skip_from_head(bytes, pos, depth, head);
    }

    static bool cose_alg_name_from_int(int64_t alg, std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        switch (alg) {
        case -7: out->assign("es256"); return true;
        case -35: out->assign("es384"); return true;
        case -36: out->assign("es512"); return true;
        case -257: out->assign("rs256"); return true;
        case -258: out->assign("rs384"); return true;
        case -259: out->assign("rs512"); return true;
        case -37: out->assign("ps256"); return true;
        case -38: out->assign("ps384"); return true;
        case -39: out->assign("ps512"); return true;
        case -8: out->assign("eddsa"); return true;
        default: break;
        }
        return false;
    }

    static bool cose_extract_algorithm_from_protected_header(
        std::span<const std::byte> protected_header_bytes,
        std::string* out_algorithm) noexcept
    {
        if (!out_algorithm) {
            return false;
        }
        out_algorithm->clear();
        if (protected_header_bytes.empty()
            || protected_header_bytes.size() > (1U << 20U)) {
            return false;
        }

        size_t pos = 0U;
        CborHeadLite head;
        if (!cbor_lite_read_head(protected_header_bytes, &pos, &head)
            || head.is_break_item || head.major != 5U) {
            return false;
        }

        uint64_t pairs = head.indefinite ? UINT64_MAX : head.arg;
        for (uint64_t i = 0U; i < pairs; ++i) {
            if (head.indefinite
                && cbor_lite_peek_break(protected_header_bytes, pos)) {
                return false;
            }

            CborHeadLite key_head;
            if (!cbor_lite_read_head(protected_header_bytes, &pos, &key_head)
                || key_head.is_break_item) {
                return false;
            }

            bool key_is_alg = false;
            if (key_head.major == 0U && !key_head.indefinite
                && key_head.arg == 1U) {
                key_is_alg = true;
            } else if (key_head.major == 3U && !key_head.indefinite) {
                if (pos > protected_header_bytes.size()
                    || key_head.arg > protected_header_bytes.size() - pos) {
                    return false;
                }
                const std::string_view key_text(
                    reinterpret_cast<const char*>(protected_header_bytes.data()
                                                  + pos),
                    static_cast<size_t>(key_head.arg));
                pos += static_cast<size_t>(key_head.arg);
                key_is_alg = (key_text == "alg" || key_text == "algorithm");
            } else {
                if (!cbor_lite_skip_from_head(protected_header_bytes, &pos, 0U,
                                              key_head)) {
                    return false;
                }
            }

            CborHeadLite value_head;
            if (!cbor_lite_read_head(protected_header_bytes, &pos, &value_head)
                || value_head.is_break_item) {
                return false;
            }

            if (!key_is_alg) {
                if (!cbor_lite_skip_from_head(protected_header_bytes, &pos, 0U,
                                              value_head)) {
                    return false;
                }
                continue;
            }

            if ((value_head.major == 0U || value_head.major == 1U)
                && !value_head.indefinite) {
                const int64_t alg
                    = (value_head.major == 0U)
                          ? static_cast<int64_t>(value_head.arg)
                          : (-1 - static_cast<int64_t>(value_head.arg));
                return cose_alg_name_from_int(alg, out_algorithm);
            }

            if (value_head.major == 3U && !value_head.indefinite) {
                if (pos > protected_header_bytes.size()
                    || value_head.arg > protected_header_bytes.size() - pos) {
                    return false;
                }
                const std::string_view alg_text(
                    reinterpret_cast<const char*>(protected_header_bytes.data()
                                                  + pos),
                    static_cast<size_t>(value_head.arg));
                pos += static_cast<size_t>(value_head.arg);
                *out_algorithm = ascii_lower(alg_text);
                return !out_algorithm->empty();
            }
            return false;
        }
        return false;
    }

    static void cbor_encode_head(std::vector<std::byte>* out, uint8_t major,
                                 uint64_t value)
    {
        if (!out) {
            return;
        }
        const uint32_t major_bits = static_cast<uint32_t>(major) << 5U;
        if (value <= 23U) {
            const uint8_t ib = static_cast<uint8_t>(
                major_bits | static_cast<uint32_t>(value));
            out->push_back(std::byte { ib });
            return;
        }
        if (value <= 0xFFU) {
            const uint8_t ib = static_cast<uint8_t>(major_bits | 24U);
            out->push_back(std::byte { ib });
            out->push_back(std::byte { static_cast<uint8_t>(value) });
            return;
        }
        if (value <= 0xFFFFU) {
            const uint8_t ib = static_cast<uint8_t>(major_bits | 25U);
            out->push_back(std::byte { ib });
            out->push_back(
                std::byte { static_cast<uint8_t>((value >> 8U) & 0xFFU) });
            out->push_back(
                std::byte { static_cast<uint8_t>((value >> 0U) & 0xFFU) });
            return;
        }
        if (value <= 0xFFFFFFFFU) {
            const uint8_t ib = static_cast<uint8_t>(major_bits | 26U);
            out->push_back(std::byte { ib });
            out->push_back(
                std::byte { static_cast<uint8_t>((value >> 24U) & 0xFFU) });
            out->push_back(
                std::byte { static_cast<uint8_t>((value >> 16U) & 0xFFU) });
            out->push_back(
                std::byte { static_cast<uint8_t>((value >> 8U) & 0xFFU) });
            out->push_back(
                std::byte { static_cast<uint8_t>((value >> 0U) & 0xFFU) });
            return;
        }
        {
            const uint8_t ib = static_cast<uint8_t>(major_bits | 27U);
            out->push_back(std::byte { ib });
        }
        for (uint32_t i = 0U; i < 8U; ++i) {
            const uint32_t shift = 56U - (i * 8U);
            out->push_back(
                std::byte { static_cast<uint8_t>((value >> shift) & 0xFFU) });
        }
    }

    static void cbor_encode_text(std::vector<std::byte>* out,
                                 std::string_view text)
    {
        cbor_encode_head(out, 3U, text.size());
        for (char c : text) {
            out->push_back(std::byte { static_cast<uint8_t>(c) });
        }
    }

    static void cbor_encode_bytes(std::vector<std::byte>* out,
                                  std::span<const std::byte> bytes)
    {
        cbor_encode_head(out, 2U, bytes.size());
        out->insert(out->end(), bytes.begin(), bytes.end());
    }

    static void cbor_encode_array(std::vector<std::byte>* out, uint64_t count)
    {
        cbor_encode_head(out, 4U, count);
    }

    static bool
    cose_build_sig_structure(std::span<const std::byte> protected_header_bytes,
                             std::span<const std::byte> payload_bytes,
                             std::vector<std::byte>* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        out->reserve(64U + protected_header_bytes.size()
                     + payload_bytes.size());

        cbor_encode_array(out, 4U);
        cbor_encode_text(out, "Signature1");
        cbor_encode_bytes(out, protected_header_bytes);
        cbor_encode_bytes(out, std::span<const std::byte> {});
        cbor_encode_bytes(out, payload_bytes);
        return true;
    }

    static uint64_t cbor_limit_or_default(uint64_t configured,
                                          uint64_t fallback) noexcept
    {
        return configured != 0U ? configured : fallback;
    }

    static bool cbor_lite_decode_i64(const CborHeadLite& head,
                                     int64_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (head.indefinite || head.is_break_item) {
            return false;
        }
        if (head.major == 0U) {
            if (head.arg > static_cast<uint64_t>(INT64_MAX)) {
                return false;
            }
            *out = static_cast<int64_t>(head.arg);
            return true;
        }
        if (head.major == 1U) {
            if (head.arg > static_cast<uint64_t>(INT64_MAX)) {
                return false;
            }
            *out = -1 - static_cast<int64_t>(head.arg);
            return true;
        }
        return false;
    }

    static bool
    cbor_lite_read_bytes_payload(std::span<const std::byte> cbor, size_t* pos,
                                 const CborHeadLite& head, uint64_t max_total,
                                 std::vector<std::byte>* out) noexcept
    {
        if (!pos || !out || head.is_break_item || head.major != 2U) {
            return false;
        }

        out->clear();
        if (!head.indefinite) {
            if (*pos > cbor.size() || head.arg > cbor.size() - *pos) {
                return false;
            }
            if (max_total != 0U && head.arg > max_total) {
                return false;
            }
            const std::span<const std::byte> payload
                = cbor.subspan(*pos, static_cast<size_t>(head.arg));
            out->assign(payload.begin(), payload.end());
            *pos += static_cast<size_t>(head.arg);
            return true;
        }

        uint64_t total = 0U;
        while (true) {
            if (cbor_lite_peek_break(cbor, *pos)) {
                *pos += 1U;
                return true;
            }
            CborHeadLite chunk;
            if (!cbor_lite_read_head(cbor, pos, &chunk)
                || chunk.is_break_item) {
                return false;
            }
            if (chunk.major != 2U || chunk.indefinite) {
                return false;
            }
            if (*pos > cbor.size() || chunk.arg > cbor.size() - *pos) {
                return false;
            }
            if (total > UINT64_MAX - chunk.arg) {
                return false;
            }
            total += chunk.arg;
            if (max_total != 0U && total > max_total) {
                return false;
            }
            const std::span<const std::byte> payload
                = cbor.subspan(*pos, static_cast<size_t>(chunk.arg));
            out->insert(out->end(), payload.begin(), payload.end());
            *pos += static_cast<size_t>(chunk.arg);
        }
    }

    static bool cbor_lite_read_text_payload(std::span<const std::byte> cbor,
                                            size_t* pos,
                                            const CborHeadLite& head,
                                            uint64_t max_total,
                                            std::string* out) noexcept
    {
        if (!pos || !out || head.is_break_item || head.major != 3U) {
            return false;
        }

        out->clear();
        if (!head.indefinite) {
            if (*pos > cbor.size() || head.arg > cbor.size() - *pos) {
                return false;
            }
            if (max_total != 0U && head.arg > max_total) {
                return false;
            }
            const std::span<const std::byte> payload
                = cbor.subspan(*pos, static_cast<size_t>(head.arg));
            out->assign(reinterpret_cast<const char*>(payload.data()),
                        payload.size());
            *pos += static_cast<size_t>(head.arg);
            return true;
        }

        uint64_t total = 0U;
        while (true) {
            if (cbor_lite_peek_break(cbor, *pos)) {
                *pos += 1U;
                return true;
            }
            CborHeadLite chunk;
            if (!cbor_lite_read_head(cbor, pos, &chunk)
                || chunk.is_break_item) {
                return false;
            }
            if (chunk.major != 3U || chunk.indefinite) {
                return false;
            }
            if (*pos > cbor.size() || chunk.arg > cbor.size() - *pos) {
                return false;
            }
            if (total > UINT64_MAX - chunk.arg) {
                return false;
            }
            total += chunk.arg;
            if (max_total != 0U && total > max_total) {
                return false;
            }
            const std::span<const std::byte> payload
                = cbor.subspan(*pos, static_cast<size_t>(chunk.arg));
            out->append(reinterpret_cast<const char*>(payload.data()),
                        payload.size());
            *pos += static_cast<size_t>(chunk.arg);
        }
    }

    static constexpr uint32_t kCoseMaxX5ChainCerts = 64U;

    static uint64_t cose_chain_total_bytes(
        const std::vector<std::vector<std::byte>>& chain) noexcept
    {
        uint64_t total = 0U;
        for (const std::vector<std::byte>& cert : chain) {
            if (total > UINT64_MAX - cert.size()) {
                return UINT64_MAX;
            }
            total += static_cast<uint64_t>(cert.size());
        }
        return total;
    }

    static bool cose_parse_x5chain_value(
        std::span<const std::byte> cbor, size_t* pos, const CborHeadLite& head,
        uint64_t max_bytes,
        std::vector<std::vector<std::byte>>* out_chain) noexcept
    {
        if (!pos || !out_chain || head.is_break_item) {
            return false;
        }

        uint64_t chain_total = cose_chain_total_bytes(*out_chain);

        if (head.major == 2U) {
            if (out_chain->size() >= kCoseMaxX5ChainCerts) {
                return cbor_lite_skip_from_head(cbor, pos, 0U, head);
            }
            if (!head.indefinite && chain_total <= max_bytes
                && head.arg > max_bytes - chain_total) {
                return cbor_lite_skip_from_head(cbor, pos, 0U, head);
            }
            std::vector<std::byte> cert;
            if (!cbor_lite_read_bytes_payload(cbor, pos, head, max_bytes,
                                              &cert)) {
                return false;
            }
            if (chain_total <= max_bytes
                && cert.size() > max_bytes - chain_total) {
                return true;
            }
            out_chain->push_back(cert);
            return true;
        }

        if (head.major != 4U) {
            return cbor_lite_skip_from_head(cbor, pos, 0U, head);
        }

        uint64_t count = head.indefinite ? UINT64_MAX : head.arg;
        for (uint64_t i = 0U; i < count; ++i) {
            if (head.indefinite && cbor_lite_peek_break(cbor, *pos)) {
                *pos += 1U;
                return true;
            }
            CborHeadLite elem;
            if (!cbor_lite_read_head(cbor, pos, &elem) || elem.is_break_item) {
                return false;
            }
            if (elem.major != 2U) {
                if (!cbor_lite_skip_from_head(cbor, pos, 0U, elem)) {
                    return false;
                }
                continue;
            }
            if (out_chain->size() >= kCoseMaxX5ChainCerts) {
                if (!cbor_lite_skip_from_head(cbor, pos, 0U, elem)) {
                    return false;
                }
                continue;
            }
            if (!elem.indefinite && chain_total <= max_bytes
                && elem.arg > max_bytes - chain_total) {
                if (!cbor_lite_skip_from_head(cbor, pos, 0U, elem)) {
                    return false;
                }
                continue;
            }
            std::vector<std::byte> cert;
            if (!cbor_lite_read_bytes_payload(cbor, pos, elem, max_bytes,
                                              &cert)) {
                return false;
            }
            if (chain_total <= max_bytes
                && cert.size() > max_bytes - chain_total) {
                continue;
            }
            if (chain_total > UINT64_MAX - cert.size()) {
                return false;
            }
            chain_total += static_cast<uint64_t>(cert.size());
            out_chain->push_back(cert);
        }
        if (head.indefinite) {
            return false;
        }
        return true;
    }

    static bool cose_parse_unprotected_header(
        std::span<const std::byte> cbor, size_t* pos, const CborHeadLite& head,
        const JumbfDecodeLimits& limits,
        C2paVerifySignatureCandidate* out_candidate) noexcept
    {
        if (!pos || !out_candidate || head.is_break_item || head.major != 5U) {
            return false;
        }

        const uint64_t max_bytes
            = cbor_limit_or_default(limits.max_cbor_bytes_bytes,
                                    8U * 1024U * 1024U);
        const uint64_t max_key_bytes
            = cbor_limit_or_default(limits.max_cbor_key_bytes, 1024U);

        uint64_t pairs = head.indefinite ? UINT64_MAX : head.arg;
        for (uint64_t i = 0U; i < pairs; ++i) {
            if (head.indefinite && cbor_lite_peek_break(cbor, *pos)) {
                *pos += 1U;
                return true;
            }

            CborHeadLite key_head;
            if (!cbor_lite_read_head(cbor, pos, &key_head)
                || key_head.is_break_item) {
                return false;
            }

            bool key_is_int  = false;
            int64_t key_int  = 0;
            bool key_is_text = false;
            std::string key_text;

            if (key_head.major == 0U || key_head.major == 1U) {
                if (!cbor_lite_decode_i64(key_head, &key_int)) {
                    return false;
                }
                key_is_int = true;
            } else if (key_head.major == 3U) {
                if (!cbor_lite_read_text_payload(cbor, pos, key_head,
                                                 max_key_bytes, &key_text)) {
                    return false;
                }
                key_is_text = true;
            } else {
                if (!cbor_lite_skip_from_head(cbor, pos, 0U, key_head)) {
                    return false;
                }
            }

            CborHeadLite value_head;
            if (!cbor_lite_read_head(cbor, pos, &value_head)
                || value_head.is_break_item) {
                return false;
            }

            const bool key_is_alg = (key_is_int && key_int == 1)
                                    || (key_is_text
                                        && (key_text == "1" || key_text == "alg"
                                            || key_text == "algorithm"));
            const bool key_is_x5chain = (key_is_int && key_int == 33)
                                        || (key_is_text
                                            && (key_text == "33"
                                                || key_text == "x5chain"
                                                || key_text == "x5c"));

            if (key_is_alg && !out_candidate->has_cose_unprotected_alg_int) {
                int64_t alg_int = 0;
                if (cbor_lite_decode_i64(value_head, &alg_int)) {
                    out_candidate->cose_unprotected_alg_int     = alg_int;
                    out_candidate->has_cose_unprotected_alg_int = true;
                }
                continue;
            }

            if (key_is_x5chain) {
                if (!out_candidate->has_certificate_chain_der) {
                    out_candidate->certificate_chain_der.clear();
                }
                if (!cose_parse_x5chain_value(
                        cbor, pos, value_head, max_bytes,
                        &out_candidate->certificate_chain_der)) {
                    return false;
                }
                if (!out_candidate->certificate_chain_der.empty()) {
                    out_candidate->has_certificate_chain_der = true;
                }
                continue;
            }

            const bool key_is_public_der = key_is_text
                                           && (key_text == "public_key_der"
                                               || key_text == "public_key");
            const bool key_is_certificate_der
                = key_is_text
                  && (key_text == "certificate_der"
                      || key_text == "certificate");

            if (key_is_public_der && !out_candidate->has_public_key_der
                && value_head.major == 2U) {
                std::vector<std::byte> pub;
                if (!cbor_lite_read_bytes_payload(cbor, pos, value_head,
                                                  max_bytes, &pub)) {
                    return false;
                }
                out_candidate->public_key_der     = pub;
                out_candidate->has_public_key_der = true;
                continue;
            }

            if (key_is_certificate_der && !out_candidate->has_certificate_der
                && value_head.major == 2U) {
                std::vector<std::byte> cert;
                if (!cbor_lite_read_bytes_payload(cbor, pos, value_head,
                                                  max_bytes, &cert)) {
                    return false;
                }
                out_candidate->certificate_der     = cert;
                out_candidate->has_certificate_der = true;
                continue;
            }

            if (!cbor_lite_skip_from_head(cbor, pos, 0U, value_head)) {
                return false;
            }
        }

        if (head.indefinite) {
            return false;
        }
        return true;
    }

    static bool cose_bytes_look_like_sign1(std::span<const std::byte> bytes,
                                           const JumbfDecodeLimits& limits)
        noexcept
    {
        if (bytes.empty()
            || bytes.size() > static_cast<size_t>(
                   cbor_limit_or_default(limits.max_cbor_bytes_bytes,
                                         8U * 1024U * 1024U))) {
            return false;
        }

        size_t pos = 0U;
        CborHeadLite head;
        if (!cbor_lite_read_head(bytes, &pos, &head) || head.is_break_item) {
            return false;
        }

        bool saw_sign1_tag = false;
        while (head.major == 6U && !head.indefinite) {
            if (head.arg == 18U) {
                saw_sign1_tag = true;
            }
            if (!cbor_lite_read_head(bytes, &pos, &head)
                || head.is_break_item) {
                return saw_sign1_tag;
            }
        }

        return saw_sign1_tag || head.major == 4U;
    }

    static bool
    cbor_payload_looks_like_cose_sign1(std::span<const std::byte> payload,
                                       const JumbfDecodeLimits& limits) noexcept
    {
        if (payload.empty()
            || payload.size() > static_cast<size_t>(
                   cbor_limit_or_default(limits.max_cbor_bytes_bytes,
                                         8U * 1024U * 1024U))) {
            return false;
        }
        if (cose_bytes_look_like_sign1(payload, limits)) {
            return true;
        }

        size_t pos = 0U;
        CborHeadLite head;
        if (!cbor_lite_read_head(payload, &pos, &head) || head.is_break_item) {
            return false;
        }

        while (head.major == 6U && !head.indefinite) {
            if (!cbor_lite_read_head(payload, &pos, &head)
                || head.is_break_item) {
                return false;
            }
        }

        if (head.major != 2U || head.indefinite) {
            return false;
        }

        const uint64_t max_bytes
            = cbor_limit_or_default(limits.max_cbor_bytes_bytes,
                                    8U * 1024U * 1024U);
        if (max_bytes != 0U && head.arg > max_bytes) {
            return false;
        }
        if (pos > payload.size() || head.arg > (payload.size() - pos)) {
            return false;
        }

        const std::span<const std::byte> inner
            = payload.subspan(pos, static_cast<size_t>(head.arg));
        return cose_bytes_look_like_sign1(inner, limits);
    }

    static bool
    cose_decode_sign1_bytes(std::span<const std::byte> cose_bytes,
                            const JumbfDecodeLimits& limits,
                            C2paVerifySignatureCandidate* out) noexcept
    {
        if (!out || cose_bytes.empty()
            || cose_bytes.size() > static_cast<size_t>(
                   cbor_limit_or_default(limits.max_cbor_bytes_bytes,
                                         8U * 1024U * 1024U))) {
            return false;
        }

        size_t pos = 0U;
        CborHeadLite head;
        if (!cbor_lite_read_head(cose_bytes, &pos, &head)
            || head.is_break_item) {
            return false;
        }

        // COSE_Sign1 is frequently wrapped in CBOR tag(18).
        while (head.major == 6U && !head.indefinite) {
            if (!cbor_lite_read_head(cose_bytes, &pos, &head)
                || head.is_break_item) {
                return false;
            }
        }

        if (head.major != 4U) {
            return false;
        }

        if (!head.indefinite && head.arg != 4U) {
            return false;
        }

        const uint64_t max_bytes
            = cbor_limit_or_default(limits.max_cbor_bytes_bytes,
                                    8U * 1024U * 1024U);

        CborHeadLite protected_head;
        if (!cbor_lite_read_head(cose_bytes, &pos, &protected_head)
            || protected_head.is_break_item || protected_head.major != 2U) {
            return false;
        }
        std::vector<std::byte> protected_bytes;
        if (!cbor_lite_read_bytes_payload(cose_bytes, &pos, protected_head,
                                          max_bytes, &protected_bytes)) {
            return false;
        }

        CborHeadLite unprotected_head;
        if (!cbor_lite_read_head(cose_bytes, &pos, &unprotected_head)
            || unprotected_head.is_break_item || unprotected_head.major != 5U) {
            return false;
        }
        C2paVerifySignatureCandidate unprotected_candidate;
        if (!cose_parse_unprotected_header(cose_bytes, &pos, unprotected_head,
                                           limits, &unprotected_candidate)) {
            return false;
        }

        CborHeadLite payload_head;
        if (!cbor_lite_read_head(cose_bytes, &pos, &payload_head)
            || payload_head.is_break_item) {
            return false;
        }

        bool payload_is_null = false;
        std::vector<std::byte> payload_bytes;
        if (payload_head.major == 2U) {
            if (!cbor_lite_read_bytes_payload(cose_bytes, &pos, payload_head,
                                              max_bytes, &payload_bytes)) {
                return false;
            }
        } else if (payload_head.major == 7U && !payload_head.indefinite
                   && payload_head.addl == 22U) {
            payload_is_null = true;
        } else {
            return false;
        }

        CborHeadLite signature_head;
        if (!cbor_lite_read_head(cose_bytes, &pos, &signature_head)
            || signature_head.is_break_item || signature_head.major != 2U) {
            return false;
        }
        std::vector<std::byte> signature_bytes;
        if (!cbor_lite_read_bytes_payload(cose_bytes, &pos, signature_head,
                                          max_bytes, &signature_bytes)) {
            return false;
        }

        if (head.indefinite) {
            if (!cbor_lite_peek_break(cose_bytes, pos)) {
                return false;
            }
            pos += 1U;
        }
        if (pos != cose_bytes.size()) {
            return false;
        }

        if (!out->has_cose_protected_bytes) {
            out->cose_protected_bytes     = protected_bytes;
            out->has_cose_protected_bytes = true;
        }
        if (!out->has_cose_signature_bytes) {
            out->cose_signature_bytes     = signature_bytes;
            out->has_cose_signature_bytes = true;
        }
        if (!payload_is_null && !out->has_cose_payload_bytes) {
            out->cose_payload_bytes.assign(payload_bytes.begin(),
                                           payload_bytes.end());
            out->has_cose_payload_bytes = true;
            out->cose_payload_is_null   = false;
        }
        if (payload_is_null && !out->has_cose_payload_bytes) {
            out->cose_payload_is_null = true;
        }

        if (!out->has_cose_unprotected_alg_int
            && unprotected_candidate.has_cose_unprotected_alg_int) {
            out->cose_unprotected_alg_int
                = unprotected_candidate.cose_unprotected_alg_int;
            out->has_cose_unprotected_alg_int = true;
        }

        if (!out->has_public_key_der
            && unprotected_candidate.has_public_key_der) {
            out->public_key_der     = unprotected_candidate.public_key_der;
            out->has_public_key_der = true;
        }
        if (!out->has_certificate_der
            && unprotected_candidate.has_certificate_der) {
            out->certificate_der     = unprotected_candidate.certificate_der;
            out->has_certificate_der = true;
        }
        if (!out->has_certificate_chain_der
            && unprotected_candidate.has_certificate_chain_der) {
            out->certificate_chain_der
                = unprotected_candidate.certificate_chain_der;
            out->has_certificate_chain_der = true;
        }
        return true;
    }

    static bool cose_decode_sign1_from_cbor_payload(
        std::span<const std::byte> cbor_payload,
        const JumbfDecodeLimits& limits,
        C2paVerifySignatureCandidate* out) noexcept
    {
        if (!out || cbor_payload.empty()
            || cbor_payload.size() > static_cast<size_t>(
                   cbor_limit_or_default(limits.max_cbor_bytes_bytes,
                                         8U * 1024U * 1024U))) {
            return false;
        }

        // Common case: COSE_Sign1 is directly encoded in the CBOR payload.
        if (cose_decode_sign1_bytes(cbor_payload, limits, out)) {
            return true;
        }

        // Some payloads wrap COSE_Sign1 in a CBOR byte string.
        size_t pos = 0U;
        CborHeadLite head;
        if (!cbor_lite_read_head(cbor_payload, &pos, &head)
            || head.is_break_item) {
            return false;
        }

        while (head.major == 6U && !head.indefinite) {
            if (!cbor_lite_read_head(cbor_payload, &pos, &head)
                || head.is_break_item) {
                return false;
            }
        }

        if (head.major != 2U || head.indefinite) {
            return false;
        }

        const uint64_t max_bytes
            = cbor_limit_or_default(limits.max_cbor_bytes_bytes,
                                    8U * 1024U * 1024U);
        if (max_bytes != 0U && head.arg > max_bytes) {
            return false;
        }
        if (pos > cbor_payload.size()
            || head.arg > (cbor_payload.size() - pos)) {
            return false;
        }

        const std::span<const std::byte> inner
            = cbor_payload.subspan(pos, static_cast<size_t>(head.arg));
        return cose_decode_sign1_bytes(inner, limits, out);
    }

#if OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    static bool asn1_read_length(std::span<const std::byte> bytes, size_t* pos,
                                 size_t* out_len) noexcept
    {
        if (!pos || !out_len || *pos >= bytes.size()) {
            return false;
        }
        const uint8_t first = u8(bytes[*pos]);
        *pos += 1U;
        if ((first & 0x80U) == 0U) {
            *out_len = static_cast<size_t>(first & 0x7FU);
            return true;
        }
        const uint8_t octets = static_cast<uint8_t>(first & 0x7FU);
        if (octets == 0U || octets > sizeof(size_t)
            || *pos + octets > bytes.size()) {
            return false;
        }
        size_t len = 0U;
        for (uint8_t i = 0U; i < octets; ++i) {
            len = (len << 8U) | static_cast<size_t>(u8(bytes[*pos]));
            *pos += 1U;
        }
        if (len < 128U) {
            return false;
        }
        *out_len = len;
        return true;
    }

    static bool asn1_read_integer(std::span<const std::byte> bytes, size_t* pos,
                                  size_t end) noexcept
    {
        if (!pos || *pos >= end || *pos >= bytes.size()) {
            return false;
        }
        if (u8(bytes[*pos]) != 0x02U) {
            return false;
        }
        *pos += 1U;
        size_t len = 0U;
        if (!asn1_read_length(bytes, pos, &len) || len == 0U) {
            return false;
        }
        if (*pos + len > end || *pos + len > bytes.size()) {
            return false;
        }
        const uint8_t first = u8(bytes[*pos]);
        if (first == 0x00U && len > 1U) {
            const uint8_t second = u8(bytes[*pos + 1U]);
            if ((second & 0x80U) == 0U) {
                return false;
            }
        } else if ((first & 0x80U) != 0U) {
            return false;
        }
        *pos += len;
        return true;
    }

    static bool signature_is_ecdsa_der(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.size() < 8U) {
            return false;
        }
        size_t pos = 0U;
        if (u8(bytes[pos]) != 0x30U) {
            return false;
        }
        pos += 1U;
        size_t seq_len = 0U;
        if (!asn1_read_length(bytes, &pos, &seq_len)) {
            return false;
        }
        if (pos + seq_len != bytes.size()) {
            return false;
        }
        const size_t end = pos + seq_len;
        if (!asn1_read_integer(bytes, &pos, end)) {
            return false;
        }
        if (!asn1_read_integer(bytes, &pos, end)) {
            return false;
        }
        return pos == end;
    }

    static size_t
    ecdsa_raw_part_len_for_algorithm(std::string_view algorithm) noexcept
    {
        if (algorithm == "es256") {
            return 32U;
        }
        if (algorithm == "es384") {
            return 48U;
        }
        if (algorithm == "es512") {
            return 66U;
        }
        return 0U;
    }

    static void asn1_append_length(std::vector<std::byte>* out,
                                   size_t len) noexcept
    {
        if (!out) {
            return;
        }
        if (len < 128U) {
            out->push_back(std::byte { static_cast<uint8_t>(len) });
            return;
        }
        size_t tmp     = len;
        uint8_t octets = 0U;
        while (tmp != 0U && octets < 8U) {
            octets += 1U;
            tmp >>= 8U;
        }
        out->push_back(std::byte { static_cast<uint8_t>(0x80U | octets) });
        for (uint8_t i = 0U; i < octets; ++i) {
            const uint32_t shift = static_cast<uint32_t>((octets - 1U - i)
                                                         * 8U);
            out->push_back(std::byte { static_cast<uint8_t>(
                (static_cast<uint64_t>(len) >> shift) & 0xFFU) });
        }
    }

    static void
    asn1_append_integer_value(std::span<const std::byte> fixed_width,
                              std::vector<std::byte>* out_value) noexcept
    {
        if (!out_value) {
            return;
        }
        out_value->clear();
        size_t first_nonzero = 0U;
        while (first_nonzero < fixed_width.size()
               && u8(fixed_width[first_nonzero]) == 0U) {
            first_nonzero += 1U;
        }
        if (first_nonzero == fixed_width.size()) {
            out_value->push_back(std::byte { 0x00 });
            return;
        }
        const std::span<const std::byte> trimmed = fixed_width.subspan(
            first_nonzero);
        if ((u8(trimmed[0]) & 0x80U) != 0U) {
            out_value->push_back(std::byte { 0x00 });
        }
        out_value->insert(out_value->end(), trimmed.begin(), trimmed.end());
    }

    static bool
    ecdsa_raw_signature_to_der(std::span<const std::byte> raw_signature,
                               size_t part_len,
                               std::vector<std::byte>* out_der) noexcept
    {
        if (!out_der || part_len == 0U
            || raw_signature.size() != part_len * 2U) {
            return false;
        }

        const std::span<const std::byte> r_fixed
            = raw_signature.subspan(0U, part_len);
        const std::span<const std::byte> s_fixed
            = raw_signature.subspan(part_len, part_len);

        std::vector<std::byte> r_value;
        std::vector<std::byte> s_value;
        asn1_append_integer_value(r_fixed, &r_value);
        asn1_append_integer_value(s_fixed, &s_value);

        out_der->clear();
        out_der->reserve(8U + r_value.size() + s_value.size());

        std::vector<std::byte> seq;
        seq.reserve(8U + r_value.size() + s_value.size());

        seq.push_back(std::byte { 0x02 });
        asn1_append_length(&seq, r_value.size());
        seq.insert(seq.end(), r_value.begin(), r_value.end());

        seq.push_back(std::byte { 0x02 });
        asn1_append_length(&seq, s_value.size());
        seq.insert(seq.end(), s_value.begin(), s_value.end());

        out_der->push_back(std::byte { 0x30 });
        asn1_append_length(out_der, seq.size());
        out_der->insert(out_der->end(), seq.begin(), seq.end());
        return true;
    }
#endif

    static size_t find_verify_signature_candidate(
        const std::vector<C2paVerifySignatureCandidate>& candidates,
        std::string_view prefix) noexcept
    {
        for (size_t index = 0U; index < candidates.size(); ++index) {
            if (candidates[index].prefix == prefix) {
                return index;
            }
        }
        return static_cast<size_t>(-1);
    }

    static size_t add_or_get_verify_signature_candidate(
        std::vector<C2paVerifySignatureCandidate>* candidates,
        std::string_view prefix) noexcept
    {
        if (!candidates) {
            return static_cast<size_t>(-1);
        }
        const size_t existing = find_verify_signature_candidate(*candidates,
                                                                prefix);
        if (existing != static_cast<size_t>(-1)) {
            return existing;
        }
        C2paVerifySignatureCandidate candidate;
        candidate.prefix.assign(prefix.data(), prefix.size());
        candidates->push_back(candidate);
        return candidates->size() - 1U;
    }

    [[maybe_unused]] static bool c2pa_verify_candidate_has_key_material(
        const C2paVerifySignatureCandidate& candidate) noexcept
    {
        return candidate.has_public_key_der || candidate.has_public_key_pem
               || candidate.has_certificate_der;
    }

#if OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    enum class OpenSslVerifyResult : uint8_t {
        Verified,
        VerificationFailed,
        InvalidSignature,
        NotSupported,
        BackendError,
    };

    enum class OpenSslChainResult : uint8_t {
        Pass,
        Fail,
        NotChecked,
        BackendError,
    };

    static const EVP_MD*
    openssl_md_for_algorithm(std::string_view algorithm) noexcept
    {
        if (algorithm == "es256" || algorithm == "rs256"
            || algorithm == "ps256") {
            return EVP_sha256();
        }
        if (algorithm == "es384" || algorithm == "rs384"
            || algorithm == "ps384") {
            return EVP_sha384();
        }
        if (algorithm == "es512" || algorithm == "rs512"
            || algorithm == "ps512") {
            return EVP_sha512();
        }
        return nullptr;
    }

    static bool openssl_alg_is_pss(std::string_view algorithm) noexcept
    {
        return algorithm == "ps256" || algorithm == "ps384"
               || algorithm == "ps512";
    }

    static EVP_PKEY* openssl_load_public_key_from_candidate(
        const C2paVerifySignatureCandidate& candidate) noexcept
    {
        // A supplied certificate is authoritative for the signature key. Do
        // not fall back to an independent key when certificate parsing fails
        // or the signature does not match that leaf certificate.
        if (candidate.has_certificate_der
            && !candidate.certificate_der.empty()) {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(
                candidate.certificate_der.data());
            X509* cert
                = d2i_X509(nullptr, &p,
                           static_cast<long>(candidate.certificate_der.size()));
            if (cert) {
                EVP_PKEY* key = X509_get_pubkey(cert);
                X509_free(cert);
                if (key) {
                    return key;
                }
            }
            return nullptr;
        }

        if (candidate.has_public_key_der && !candidate.public_key_der.empty()) {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(
                candidate.public_key_der.data());
            EVP_PKEY* key = d2i_PUBKEY(nullptr, &p,
                                       static_cast<long>(
                                           candidate.public_key_der.size()));
            if (key) {
                return key;
            }
        }

        if (candidate.has_public_key_pem && !candidate.public_key_pem.empty()) {
            BIO* bio = BIO_new_mem_buf(candidate.public_key_pem.data(),
                                       static_cast<int>(
                                           candidate.public_key_pem.size()));
            if (!bio) {
                return nullptr;
            }
            EVP_PKEY* key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
            BIO_free(bio);
            if (key) {
                return key;
            }
        }
        return nullptr;
    }

    static const char* openssl_chain_reason_from_x509_error(int err) noexcept
    {
        switch (err) {
        case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT: return "self_signed_leaf";
        case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN: return "self_signed_chain";
        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
            return "issuer_not_trusted";
        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT: return "issuer_not_found";
        case X509_V_ERR_CERT_HAS_EXPIRED: return "certificate_expired";
        case X509_V_ERR_CERT_NOT_YET_VALID: return "certificate_not_yet_valid";
        default: break;
        }
        return "trust_chain_unverified";
    }

    static OpenSslChainResult openssl_verify_certificate_chain(
        const C2paVerifySignatureCandidate& candidate,
        const char** out_reason) noexcept
    {
        if (out_reason) {
            *out_reason = "not_checked";
        }
        if (!candidate.has_certificate_der
            || candidate.certificate_der.empty()) {
            return OpenSslChainResult::NotChecked;
        }

        const unsigned char* cert_data = reinterpret_cast<const unsigned char*>(
            candidate.certificate_der.data());
        X509* cert
            = d2i_X509(nullptr, &cert_data,
                       static_cast<long>(candidate.certificate_der.size()));
        if (!cert) {
            if (out_reason) {
                *out_reason = "certificate_parse_failed";
            }
            return OpenSslChainResult::Fail;
        }

        const ASN1_TIME* not_before = X509_get0_notBefore(cert);
        const ASN1_TIME* not_after  = X509_get0_notAfter(cert);
        if (!not_before || !not_after) {
            X509_free(cert);
            if (out_reason) {
                *out_reason = "certificate_time_missing";
            }
            return OpenSslChainResult::Fail;
        }
        if (X509_cmp_current_time(not_before) > 0
            || X509_cmp_current_time(not_after) < 0) {
            X509_free(cert);
            if (out_reason) {
                *out_reason = "certificate_expired";
            }
            return OpenSslChainResult::Fail;
        }

        X509_STORE* store = X509_STORE_new();
        if (!store) {
            X509_free(cert);
            if (out_reason) {
                *out_reason = "store_alloc_failed";
            }
            return OpenSslChainResult::BackendError;
        }
        if (X509_STORE_set_default_paths(store) != 1) {
            X509_STORE_free(store);
            X509_free(cert);
            if (out_reason) {
                *out_reason = "store_default_paths_failed";
            }
            return OpenSslChainResult::BackendError;
        }

        X509_STORE_CTX* store_ctx = X509_STORE_CTX_new();
        if (!store_ctx) {
            X509_STORE_free(store);
            X509_free(cert);
            if (out_reason) {
                *out_reason = "store_ctx_alloc_failed";
            }
            return OpenSslChainResult::BackendError;
        }

        STACK_OF(X509)* untrusted_chain = nullptr;
        if (candidate.has_certificate_chain_der
            && candidate.certificate_chain_der.size() > 1U) {
            untrusted_chain = sk_X509_new_null();
            if (!untrusted_chain) {
                X509_STORE_CTX_free(store_ctx);
                X509_STORE_free(store);
                X509_free(cert);
                if (out_reason) {
                    *out_reason = "certificate_chain_alloc_failed";
                }
                return OpenSslChainResult::BackendError;
            }
            for (size_t i = 1U; i < candidate.certificate_chain_der.size();
                 ++i) {
                const std::vector<std::byte>& cert_bytes
                    = candidate.certificate_chain_der[i];
                if (cert_bytes.empty()) {
                    continue;
                }
                const unsigned char* p = reinterpret_cast<const unsigned char*>(
                    cert_bytes.data());
                X509* chain_cert
                    = d2i_X509(nullptr, &p,
                               static_cast<long>(cert_bytes.size()));
                if (!chain_cert) {
                    sk_X509_pop_free(untrusted_chain, X509_free);
                    X509_STORE_CTX_free(store_ctx);
                    X509_STORE_free(store);
                    X509_free(cert);
                    if (out_reason) {
                        *out_reason = "certificate_chain_parse_failed";
                    }
                    return OpenSslChainResult::Fail;
                }
                if (sk_X509_push(untrusted_chain, chain_cert) == 0) {
                    X509_free(chain_cert);
                    sk_X509_pop_free(untrusted_chain, X509_free);
                    X509_STORE_CTX_free(store_ctx);
                    X509_STORE_free(store);
                    X509_free(cert);
                    if (out_reason) {
                        *out_reason = "certificate_chain_push_failed";
                    }
                    return OpenSslChainResult::BackendError;
                }
            }
        }

        if (X509_STORE_CTX_init(store_ctx, store, cert, untrusted_chain) != 1) {
            if (untrusted_chain) {
                sk_X509_pop_free(untrusted_chain, X509_free);
            }
            X509_STORE_CTX_free(store_ctx);
            X509_STORE_free(store);
            X509_free(cert);
            if (out_reason) {
                *out_reason = "store_ctx_init_failed";
            }
            return OpenSslChainResult::BackendError;
        }

        const int verify_ok   = X509_verify_cert(store_ctx);
        const int verify_code = X509_STORE_CTX_get_error(store_ctx);
        if (untrusted_chain) {
            sk_X509_pop_free(untrusted_chain, X509_free);
        }
        X509_STORE_CTX_free(store_ctx);
        X509_STORE_free(store);
        X509_free(cert);

        if (verify_ok == 1) {
            if (out_reason) {
                *out_reason = "ok";
            }
            return OpenSslChainResult::Pass;
        }
        if (out_reason) {
            *out_reason = openssl_chain_reason_from_x509_error(verify_code);
        }
        return OpenSslChainResult::Fail;
    }

    static OpenSslVerifyResult openssl_verify_candidate(
        const C2paVerifySignatureCandidate& candidate) noexcept
    {
        if (!candidate.has_algorithm || !candidate.has_signing_input
            || !candidate.has_signature_bytes) {
            return OpenSslVerifyResult::NotSupported;
        }
        if (!c2pa_verify_candidate_has_key_material(candidate)) {
            return OpenSslVerifyResult::NotSupported;
        }

        const std::string_view algorithm(candidate.algorithm);
        const std::span<const std::byte> signing_input(
            candidate.signing_input.data(), candidate.signing_input.size());
        const std::span<const std::byte> signature(
            candidate.signature_bytes.data(), candidate.signature_bytes.size());

        std::span<const std::byte> signature_to_verify = signature;
        std::vector<std::byte> signature_der;

        if (is_alg_ecdsa(algorithm)) {
            if (!signature_is_ecdsa_der(signature)) {
                const size_t part_len = ecdsa_raw_part_len_for_algorithm(
                    algorithm);
                if (part_len == 0U || signature.size() != part_len * 2U
                    || !ecdsa_raw_signature_to_der(signature, part_len,
                                                   &signature_der)) {
                    return OpenSslVerifyResult::InvalidSignature;
                }
                signature_to_verify
                    = std::span<const std::byte>(signature_der.data(),
                                                 signature_der.size());
            }
        }
        if (is_alg_eddsa(algorithm) && signature.size() != 64U) {
            return OpenSslVerifyResult::InvalidSignature;
        }
        if (is_alg_rsa(algorithm) && signature.empty()) {
            return OpenSslVerifyResult::InvalidSignature;
        }

        EVP_PKEY* key = openssl_load_public_key_from_candidate(candidate);
        if (!key) {
            return OpenSslVerifyResult::NotSupported;
        }

        const int key_type = EVP_PKEY_base_id(key);
        if (is_alg_ecdsa(algorithm) && key_type != EVP_PKEY_EC) {
            EVP_PKEY_free(key);
            return OpenSslVerifyResult::VerificationFailed;
        }
        if (is_alg_eddsa(algorithm) && key_type != EVP_PKEY_ED25519) {
            EVP_PKEY_free(key);
            return OpenSslVerifyResult::VerificationFailed;
        }
        if (is_alg_rsa(algorithm) && key_type != EVP_PKEY_RSA
            && key_type != EVP_PKEY_RSA_PSS) {
            EVP_PKEY_free(key);
            return OpenSslVerifyResult::VerificationFailed;
        }

        EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
        if (!md_ctx) {
            EVP_PKEY_free(key);
            return OpenSslVerifyResult::BackendError;
        }

        EVP_PKEY_CTX* pkey_ctx = nullptr;
        int init_ok            = 0;
        if (is_alg_eddsa(algorithm)) {
            init_ok = EVP_DigestVerifyInit(md_ctx, &pkey_ctx, nullptr, nullptr,
                                           key);
        } else {
            const EVP_MD* md = openssl_md_for_algorithm(algorithm);
            if (!md) {
                EVP_MD_CTX_free(md_ctx);
                EVP_PKEY_free(key);
                return OpenSslVerifyResult::NotSupported;
            }
            init_ok = EVP_DigestVerifyInit(md_ctx, &pkey_ctx, md, nullptr, key);
        }
        if (init_ok != 1) {
            EVP_MD_CTX_free(md_ctx);
            EVP_PKEY_free(key);
            return OpenSslVerifyResult::BackendError;
        }

        if (openssl_alg_is_pss(algorithm)) {
            if (!pkey_ctx
                || EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING)
                       != 1
                || EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, -1) != 1) {
                EVP_MD_CTX_free(md_ctx);
                EVP_PKEY_free(key);
                return OpenSslVerifyResult::BackendError;
            }
        }

        int verify_ok = 0;
        if (is_alg_eddsa(algorithm)) {
            verify_ok = EVP_DigestVerify(md_ctx,
                                         reinterpret_cast<const unsigned char*>(
                                             signature_to_verify.data()),
                                         signature_to_verify.size(),
                                         reinterpret_cast<const unsigned char*>(
                                             signing_input.data()),
                                         signing_input.size());
        } else {
            if (EVP_DigestVerifyUpdate(md_ctx,
                                       reinterpret_cast<const unsigned char*>(
                                           signing_input.data()),
                                       signing_input.size())
                != 1) {
                EVP_MD_CTX_free(md_ctx);
                EVP_PKEY_free(key);
                return OpenSslVerifyResult::BackendError;
            }
            verify_ok
                = EVP_DigestVerifyFinal(md_ctx,
                                        reinterpret_cast<const unsigned char*>(
                                            signature_to_verify.data()),
                                        signature_to_verify.size());
        }

        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(key);

        if (verify_ok == 1) {
            return OpenSslVerifyResult::Verified;
        }
        if (verify_ok == 0) {
            return OpenSslVerifyResult::VerificationFailed;
        }
        return OpenSslVerifyResult::BackendError;
    }
#endif

    struct ClaimProjection final {
        struct AssertionProjection final {
            std::string prefix;
            uint64_t key_hits = 0U;
        };
        struct IngredientProjection final {
            std::string prefix;
            uint64_t key_hits = 0U;
            bool has_title = false;
            std::string title;
            bool has_relationship = false;
            std::string relationship;
            bool has_thumbnail_url = false;
            std::string thumbnail_url;
        };
        struct SignatureProjection final {
            std::string prefix;
            uint64_t key_hits  = 0U;
            bool has_algorithm = false;
            std::string algorithm;
        };
        struct NamedCount final {
            std::string key;
            uint64_t count = 0U;
        };

        std::string prefix;
        uint64_t key_hits                      = 0U;
        uint64_t signature_key_hits            = 0U;
        uint64_t referenced_by_signature_count = 0U;
        uint64_t ingredient_title_count        = 0U;
        uint64_t ingredient_relationship_count = 0U;
        uint64_t ingredient_thumbnail_url_count = 0U;
        uint64_t linked_ingredient_signature_count = 0U;
        uint64_t linked_direct_ingredient_signature_count = 0U;
        uint64_t linked_cross_ingredient_signature_count = 0U;
        uint64_t linked_ingredient_title_count = 0U;
        uint64_t linked_ingredient_relationship_count = 0U;
        uint64_t linked_ingredient_thumbnail_url_count = 0U;
        uint64_t linked_ingredient_explicit_reference_signature_count = 0U;
        uint64_t
            linked_ingredient_explicit_reference_direct_signature_count
            = 0U;
        uint64_t
            linked_ingredient_explicit_reference_cross_signature_count
            = 0U;
        uint64_t linked_ingredient_explicit_reference_title_count = 0U;
        uint64_t linked_ingredient_explicit_reference_relationship_count = 0U;
        uint64_t linked_ingredient_explicit_reference_thumbnail_url_count
            = 0U;
        uint64_t
            linked_ingredient_explicit_reference_unresolved_signature_count
            = 0U;
        uint64_t
            linked_ingredient_explicit_reference_unresolved_direct_signature_count
            = 0U;
        uint64_t
            linked_ingredient_explicit_reference_unresolved_cross_signature_count
            = 0U;
        uint64_t linked_ingredient_explicit_reference_unresolved_title_count
            = 0U;
        uint64_t
            linked_ingredient_explicit_reference_unresolved_relationship_count
            = 0U;
        uint64_t
            linked_ingredient_explicit_reference_unresolved_thumbnail_url_count
            = 0U;
        uint64_t linked_ingredient_explicit_reference_ambiguous_signature_count
            = 0U;
        uint64_t
            linked_ingredient_explicit_reference_ambiguous_direct_signature_count
            = 0U;
        uint64_t
            linked_ingredient_explicit_reference_ambiguous_cross_signature_count
            = 0U;
        uint64_t linked_ingredient_explicit_reference_ambiguous_title_count
            = 0U;
        uint64_t
            linked_ingredient_explicit_reference_ambiguous_relationship_count
            = 0U;
        uint64_t
            linked_ingredient_explicit_reference_ambiguous_thumbnail_url_count
            = 0U;
        std::vector<AssertionProjection> assertions;
        std::vector<IngredientProjection> ingredients;
        std::vector<SignatureProjection> signatures;
        std::vector<NamedCount> ingredient_relationship_counts;
        std::vector<NamedCount> linked_ingredient_relationship_counts;
        std::vector<NamedCount>
            linked_ingredient_explicit_reference_relationship_counts;
        std::vector<NamedCount>
            linked_ingredient_explicit_reference_unresolved_relationship_counts;
        std::vector<NamedCount>
            linked_ingredient_explicit_reference_ambiguous_relationship_counts;
        bool has_claim_generator = false;
        std::string claim_generator;
    };

    struct SignatureProjection final {
        std::string prefix;
        uint64_t key_hits                                = 0U;
        uint64_t reference_key_hits                      = 0U;
        uint64_t explicit_reference_index_hits           = 0U;
        uint64_t explicit_reference_label_hits           = 0U;
        uint64_t linked_claim_count                      = 0U;
        uint64_t linked_ingredient_claim_count           = 0U;
        uint64_t linked_direct_ingredient_claim_count    = 0U;
        uint64_t linked_cross_ingredient_claim_count     = 0U;
        uint64_t linked_direct_ingredient_title_count    = 0U;
        uint64_t linked_cross_ingredient_title_count     = 0U;
        uint64_t linked_direct_ingredient_relationship_count = 0U;
        uint64_t linked_cross_ingredient_relationship_count = 0U;
        uint64_t linked_direct_ingredient_thumbnail_url_count = 0U;
        uint64_t linked_cross_ingredient_thumbnail_url_count = 0U;
        uint64_t linked_ingredient_title_count           = 0U;
        uint64_t linked_ingredient_relationship_count    = 0U;
        uint64_t linked_ingredient_thumbnail_url_count   = 0U;
        uint64_t cross_claim_link_count                  = 0U;
        bool has_explicit_reference                      = false;
        bool direct_claim_has_ingredients                = false;
        uint64_t explicit_reference_resolved_claim_count = 0U;
        bool explicit_reference_unresolved               = false;
        bool explicit_reference_ambiguous                = false;
        bool has_algorithm                               = false;
        std::string direct_claim_prefix;
        std::string algorithm;
        std::vector<std::string> linked_claim_prefixes;
        std::vector<ClaimProjection::NamedCount>
            linked_ingredient_relationship_counts;
    };

    struct ManifestProjection final {
        std::string prefix;
        bool is_active_manifest                      = false;
        uint64_t claim_count                         = 0U;
        uint64_t ingredient_claim_count              = 0U;
        uint64_t ingredient_claim_with_signature_count = 0U;
        uint64_t ingredient_claim_referenced_by_signature_count = 0U;
        uint64_t ingredient_signature_count          = 0U;
        uint64_t ingredient_linked_claim_count       = 0U;
        uint64_t ingredient_linked_claim_explicit_reference_count = 0U;
        uint64_t ingredient_linked_claim_explicit_reference_unresolved_count
            = 0U;
        uint64_t ingredient_linked_claim_explicit_reference_ambiguous_count
            = 0U;
        uint64_t ingredient_linked_claim_direct_source_count = 0U;
        uint64_t ingredient_linked_claim_cross_source_count = 0U;
        uint64_t ingredient_linked_claim_mixed_source_count = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_direct_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_cross_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_mixed_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_unresolved_direct_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_unresolved_cross_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_unresolved_mixed_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_ambiguous_direct_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_ambiguous_cross_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_ambiguous_mixed_source_count
            = 0U;
        uint64_t ingredient_linked_signature_count   = 0U;
        uint64_t ingredient_linked_direct_claim_count = 0U;
        uint64_t ingredient_linked_cross_claim_count = 0U;
        uint64_t ingredient_linked_signature_direct_source_count = 0U;
        uint64_t ingredient_linked_signature_cross_source_count = 0U;
        uint64_t ingredient_linked_signature_mixed_source_count = 0U;
        uint64_t ingredient_linked_signature_direct_title_count = 0U;
        uint64_t ingredient_linked_signature_cross_title_count = 0U;
        uint64_t ingredient_linked_signature_direct_relationship_count = 0U;
        uint64_t ingredient_linked_signature_cross_relationship_count
            = 0U;
        uint64_t ingredient_linked_signature_direct_thumbnail_url_count = 0U;
        uint64_t ingredient_linked_signature_cross_thumbnail_url_count
            = 0U;
        uint64_t ingredient_linked_signature_title_count = 0U;
        uint64_t ingredient_linked_signature_relationship_count = 0U;
        uint64_t ingredient_linked_signature_thumbnail_url_count = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_direct_claim_count
            = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_cross_claim_count
            = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_direct_source_count
            = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_cross_source_count
            = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_mixed_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_direct_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_cross_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_direct_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_cross_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_direct_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_cross_thumbnail_url_count
            = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_direct_claim_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_cross_claim_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_direct_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_cross_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_mixed_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_direct_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_cross_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_direct_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_cross_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_direct_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_cross_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_direct_claim_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_cross_claim_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_direct_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_cross_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_mixed_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_direct_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_cross_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_direct_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_cross_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_direct_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_cross_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_thumbnail_url_count
            = 0U;
        uint64_t ingredient_explicit_reference_signature_count = 0U;
        uint64_t ingredient_explicit_reference_unresolved_signature_count
            = 0U;
        uint64_t ingredient_explicit_reference_ambiguous_signature_count
            = 0U;
        uint64_t assertion_count                     = 0U;
        uint64_t ingredient_count                    = 0U;
        uint64_t ingredient_relationship_count       = 0U;
        uint64_t ingredient_thumbnail_url_count      = 0U;
        uint64_t signature_count                     = 0U;
        uint64_t signature_linked_count              = 0U;
        uint64_t signature_orphan_count              = 0U;
        uint64_t cross_claim_link_count              = 0U;
        uint64_t explicit_reference_signature_count  = 0U;
        uint64_t explicit_reference_unresolved_count = 0U;
        uint64_t explicit_reference_ambiguous_count  = 0U;
        std::vector<ClaimProjection::NamedCount> ingredient_relationship_counts;
        std::vector<ClaimProjection::NamedCount>
            ingredient_linked_signature_relationship_counts;
        std::vector<ClaimProjection::NamedCount>
            ingredient_linked_signature_explicit_reference_relationship_counts;
        std::vector<ClaimProjection::NamedCount>
            ingredient_linked_signature_explicit_reference_unresolved_relationship_counts;
        std::vector<ClaimProjection::NamedCount>
            ingredient_linked_signature_explicit_reference_ambiguous_relationship_counts;
    };

    struct ClaimProjectionLess final {
        bool operator()(const ClaimProjection& a,
                        const ClaimProjection& b) const noexcept
        {
            return a.prefix < b.prefix;
        }
    };

    struct AssertionProjectionLess final {
        bool
        operator()(const ClaimProjection::AssertionProjection& a,
                   const ClaimProjection::AssertionProjection& b) const noexcept
        {
            return a.prefix < b.prefix;
        }
    };

    struct IngredientProjectionLess final {
        bool
        operator()(const ClaimProjection::IngredientProjection& a,
                   const ClaimProjection::IngredientProjection& b) const noexcept
        {
            return a.prefix < b.prefix;
        }
    };

    struct SignatureProjectionLess final {
        bool operator()(const SignatureProjection& a,
                        const SignatureProjection& b) const noexcept
        {
            return a.prefix < b.prefix;
        }
    };

    struct ManifestProjectionLess final {
        bool operator()(const ManifestProjection& a,
                        const ManifestProjection& b) const noexcept
        {
            return a.prefix < b.prefix;
        }
    };

    struct ClaimSignatureProjectionLess final {
        bool
        operator()(const ClaimProjection::SignatureProjection& a,
                   const ClaimProjection::SignatureProjection& b) const noexcept
        {
            return a.prefix < b.prefix;
        }
    };

    static size_t
    find_claim_projection(const std::vector<ClaimProjection>& claims,
                          std::string_view prefix) noexcept
    {
        for (size_t index = 0U; index < claims.size(); ++index) {
            if (claims[index].prefix == prefix) {
                return index;
            }
        }
        return static_cast<size_t>(-1);
    }

    static size_t
    add_or_get_claim_projection(std::vector<ClaimProjection>* claims,
                                std::string_view prefix) noexcept
    {
        if (!claims) {
            return static_cast<size_t>(-1);
        }
        const size_t existing = find_claim_projection(*claims, prefix);
        if (existing != static_cast<size_t>(-1)) {
            return existing;
        }
        ClaimProjection claim;
        claim.prefix.assign(prefix.data(), prefix.size());
        claims->push_back(claim);
        return claims->size() - 1U;
    }

    static size_t find_assertion_projection(
        const std::vector<ClaimProjection::AssertionProjection>& assertions,
        std::string_view prefix) noexcept
    {
        for (size_t index = 0U; index < assertions.size(); ++index) {
            if (assertions[index].prefix == prefix) {
                return index;
            }
        }
        return static_cast<size_t>(-1);
    }

    static size_t add_or_get_assertion_projection(
        std::vector<ClaimProjection::AssertionProjection>* assertions,
        std::string_view prefix) noexcept
    {
        if (!assertions) {
            return static_cast<size_t>(-1);
        }
        const size_t existing = find_assertion_projection(*assertions, prefix);
        if (existing != static_cast<size_t>(-1)) {
            return existing;
        }
        ClaimProjection::AssertionProjection assertion;
        assertion.prefix.assign(prefix.data(), prefix.size());
        assertions->push_back(assertion);
        return assertions->size() - 1U;
    }

    static size_t find_claim_signature_projection(
        const std::vector<ClaimProjection::SignatureProjection>& signatures,
        std::string_view prefix) noexcept
    {
        for (size_t index = 0U; index < signatures.size(); ++index) {
            if (signatures[index].prefix == prefix) {
                return index;
            }
        }
        return static_cast<size_t>(-1);
    }

    static size_t add_or_get_claim_signature_projection(
        std::vector<ClaimProjection::SignatureProjection>* signatures,
        std::string_view prefix) noexcept
    {
        if (!signatures) {
            return static_cast<size_t>(-1);
        }
        const size_t existing = find_claim_signature_projection(*signatures,
                                                                prefix);
        if (existing != static_cast<size_t>(-1)) {
            return existing;
        }
        ClaimProjection::SignatureProjection signature;
        signature.prefix.assign(prefix.data(), prefix.size());
        signatures->push_back(signature);
        return signatures->size() - 1U;
    }

    static size_t find_ingredient_projection(
        const std::vector<ClaimProjection::IngredientProjection>& ingredients,
        std::string_view prefix) noexcept
    {
        for (size_t index = 0U; index < ingredients.size(); ++index) {
            if (ingredients[index].prefix == prefix) {
                return index;
            }
        }
        return static_cast<size_t>(-1);
    }

    static size_t add_or_get_ingredient_projection(
        std::vector<ClaimProjection::IngredientProjection>* ingredients,
        std::string_view prefix) noexcept
    {
        if (!ingredients) {
            return static_cast<size_t>(-1);
        }
        const size_t existing = find_ingredient_projection(*ingredients,
                                                           prefix);
        if (existing != static_cast<size_t>(-1)) {
            return existing;
        }
        ClaimProjection::IngredientProjection ingredient;
        ingredient.prefix.assign(prefix.data(), prefix.size());
        ingredients->push_back(ingredient);
        return ingredients->size() - 1U;
    }

    static size_t find_signature_projection(
        const std::vector<SignatureProjection>& signatures,
        std::string_view prefix) noexcept
    {
        for (size_t index = 0U; index < signatures.size(); ++index) {
            if (signatures[index].prefix == prefix) {
                return index;
            }
        }
        return static_cast<size_t>(-1);
    }

    static size_t add_or_get_signature_projection(
        std::vector<SignatureProjection>* signatures,
        std::string_view prefix) noexcept
    {
        if (!signatures) {
            return static_cast<size_t>(-1);
        }
        const size_t existing = find_signature_projection(*signatures, prefix);
        if (existing != static_cast<size_t>(-1)) {
            return existing;
        }
        SignatureProjection signature;
        signature.prefix.assign(prefix.data(), prefix.size());
        signatures->push_back(signature);
        return signatures->size() - 1U;
    }

    static size_t
    find_manifest_projection(const std::vector<ManifestProjection>& manifests,
                             std::string_view prefix) noexcept
    {
        for (size_t index = 0U; index < manifests.size(); ++index) {
            if (manifests[index].prefix == prefix) {
                return index;
            }
        }
        return static_cast<size_t>(-1);
    }

    static size_t
    add_or_get_manifest_projection(std::vector<ManifestProjection>* manifests,
                                   std::string_view prefix) noexcept
    {
        if (!manifests) {
            return static_cast<size_t>(-1);
        }
        const size_t existing = find_manifest_projection(*manifests, prefix);
        if (existing != static_cast<size_t>(-1)) {
            return existing;
        }
        ManifestProjection manifest;
        manifest.prefix.assign(prefix.data(), prefix.size());
        manifests->push_back(manifest);
        return manifests->size() - 1U;
    }

    static bool extract_manifest_prefix_from_projection_prefix(
        std::string_view prefix, std::string* out_prefix) noexcept
    {
        if (!out_prefix || prefix.empty()) {
            return false;
        }
        size_t cut = std::string_view::npos;

        const size_t claim_pos = prefix.find(".claims[");
        if (claim_pos != std::string_view::npos) {
            cut = claim_pos;
        }

        const size_t assertion_pos = prefix.find(".assertions[");
        if (assertion_pos != std::string_view::npos
            && (cut == std::string_view::npos || assertion_pos < cut)) {
            cut = assertion_pos;
        }

        const size_t signature_pos = prefix.find(".signatures[");
        if (signature_pos != std::string_view::npos
            && (cut == std::string_view::npos || signature_pos < cut)) {
            cut = signature_pos;
        }

        if (cut == std::string_view::npos || cut == 0U) {
            return false;
        }

        out_prefix->assign(prefix.substr(0U, cut));
        return !out_prefix->empty();
    }

    static bool
    extract_manifest_prefix_from_cbor_key(std::string_view key,
                                          std::string* out_prefix) noexcept
    {
        if (!out_prefix || key.empty()) {
            return false;
        }
        static constexpr std::string_view marker(".manifests.");
        const size_t pos = key.find(marker);
        if (pos == std::string_view::npos) {
            return false;
        }
        const size_t label_begin = pos + marker.size();
        if (label_begin >= key.size()) {
            return false;
        }
        size_t label_end = label_begin;
        while (label_end < key.size() && !cbor_path_separator(key[label_end])) {
            label_end += 1U;
        }
        if (label_end == label_begin) {
            return false;
        }
        out_prefix->assign(key.substr(0U, label_end));
        return !out_prefix->empty();
    }

    static bool manifest_prefix_is_active(std::string_view prefix) noexcept
    {
        return string_ends_with(prefix, ".manifests.active_manifest");
    }

    static void append_unique_string_value(std::vector<std::string>* values,
                                           std::string_view value) noexcept
    {
        if (!values || value.empty()) {
            return;
        }
        if (vector_contains_string(*values, value)) {
            return;
        }
        values->emplace_back(value.data(), value.size());
    }

    static bool vector_contains_u32(const std::vector<uint32_t>& values,
                                    uint32_t value) noexcept
    {
        for (const uint32_t existing : values) {
            if (existing == value) {
                return true;
            }
        }
        return false;
    }

    static void sort_unique_u32(std::vector<uint32_t>* values) noexcept
    {
        if (!values || values->empty()) {
            return;
        }
        std::sort(values->begin(), values->end());
        size_t w = 1U;
        for (size_t i = 1U; i < values->size(); ++i) {
            if ((*values)[i] != (*values)[w - 1U]) {
                (*values)[w] = (*values)[i];
                w += 1U;
            }
        }
        values->resize(w);
    }

    static void sort_unique_strings(std::vector<std::string>* values) noexcept
    {
        if (!values || values->empty()) {
            return;
        }
        std::sort(values->begin(), values->end());
        size_t w = 1U;
        for (size_t i = 1U; i < values->size(); ++i) {
            if ((*values)[i] != (*values)[w - 1U]) {
                (*values)[w] = (*values)[i];
                w += 1U;
            }
        }
        values->resize(w);
    }

    static size_t find_named_count(
        const std::vector<ClaimProjection::NamedCount>& counts,
        std::string_view key) noexcept
    {
        for (size_t index = 0U; index < counts.size(); ++index) {
            if (counts[index].key == key) {
                return index;
            }
        }
        return static_cast<size_t>(-1);
    }

    static void add_or_increment_named_count(
        std::vector<ClaimProjection::NamedCount>* counts,
        std::string_view key, uint64_t add) noexcept
    {
        if (!counts || key.empty() || add == 0U) {
            return;
        }
        const size_t existing = find_named_count(*counts, key);
        if (existing != static_cast<size_t>(-1)) {
            (*counts)[existing].count += add;
            return;
        }
        ClaimProjection::NamedCount named_count;
        named_count.key.assign(key.data(), key.size());
        named_count.count = add;
        counts->push_back(named_count);
    }

    struct NamedCountLess final {
        bool operator()(const ClaimProjection::NamedCount& a,
                        const ClaimProjection::NamedCount& b) const noexcept
        {
            return a.key < b.key;
        }
    };

    static void sort_named_counts(
        std::vector<ClaimProjection::NamedCount>* counts) noexcept
    {
        if (!counts) {
            return;
        }
        std::sort(counts->begin(), counts->end(), NamedCountLess {});
    }

    static bool sanitize_ascii_summary_segment(std::string_view text,
                                               uint32_t max_output_bytes,
                                               std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (text.empty()) {
            out->assign("_");
            return true;
        }
        const size_t limit = (max_output_bytes != 0U
                              && text.size() > max_output_bytes)
                                 ? static_cast<size_t>(max_output_bytes)
                                 : text.size();
        for (size_t i = 0U; i < limit; ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            const bool allowed = (c >= 'a' && c <= 'z')
                                 || (c >= 'A' && c <= 'Z')
                                 || (c >= '0' && c <= '9') || c == '_'
                                 || c == '-' || c == '.';
            if (allowed) {
                out->push_back(static_cast<char>(c));
            } else {
                out->push_back('_');
            }
        }
        if (out->empty()) {
            out->assign("_");
        }
        return true;
    }

    static bool payload_bytes_equal(std::span<const std::byte> a,
                                    std::span<const std::byte> b) noexcept
    {
        if (a.size() != b.size()) {
            return false;
        }
        if (a.empty()) {
            return true;
        }
        return std::memcmp(a.data(), b.data(), a.size()) == 0;
    }

    static void collect_claim_payloads_for_prefix(
        const DecodeContext& ctx, std::string_view claim_prefix,
        uint64_t max_bytes,
        std::vector<std::vector<std::byte>>* out_payloads) noexcept
    {
        if (!ctx.store || !out_payloads || claim_prefix.empty()) {
            return;
        }
        const std::span<const Entry> entries = ctx.store->entries();
        for (const Entry& e : entries) {
            if (e.origin.block != ctx.block
                || e.key.kind != MetaKeyKind::JumbfCborKey) {
                continue;
            }
            if (e.value.kind != MetaValueKind::Bytes
                && !(e.value.kind == MetaValueKind::Array
                     && e.value.elem_type == MetaElementType::U8)) {
                continue;
            }
            const std::string_view key
                = arena_string_view(ctx.store->arena(),
                                    e.key.data.jumbf_cbor_key.key);
            if (!string_starts_with(key, claim_prefix)
                || !cbor_key_is_claim_payload_field(key)) {
                continue;
            }
            const std::span<const std::byte> payload = ctx.store->arena().span(
                e.value.data.span);
            append_unique_detached_payload_candidate(payload, max_bytes,
                                                     out_payloads);
        }
    }

    static void append_signature_explicit_reference_links(
        const DecodeContext& ctx, const std::vector<ClaimProjection>& claims,
        const std::vector<std::vector<std::vector<std::byte>>>&
            claim_payloads_by_index,
        uint64_t max_bytes, SignatureProjection* signature) noexcept
    {
        if (!signature || !ctx.store) {
            return;
        }
        if (claims.empty() || claim_payloads_by_index.size() != claims.size()) {
            return;
        }

        C2paVerifySignatureCandidate candidate;
        candidate.prefix = signature->prefix;

        bool saw_reference  = false;
        uint64_t index_hits = 0U;
        uint64_t label_hits = 0U;
        std::vector<std::vector<std::byte>> detached_candidates;
        (void)collect_detached_payload_candidates_from_claim_references(
            ctx, candidate, max_bytes, &detached_candidates, &saw_reference,
            &index_hits, &label_hits);
        if (!saw_reference) {
            return;
        }
        signature->has_explicit_reference        = true;
        signature->explicit_reference_index_hits = index_hits;
        signature->explicit_reference_label_hits = label_hits;

        for (const std::vector<std::byte>& detached : detached_candidates) {
            const std::span<const std::byte> detached_span(detached.data(),
                                                           detached.size());
            for (size_t claim_index = 0U; claim_index < claims.size();
                 ++claim_index) {
                const std::vector<std::vector<std::byte>>& claim_payloads
                    = claim_payloads_by_index[claim_index];
                bool matched_claim = false;
                for (const std::vector<std::byte>& payload : claim_payloads) {
                    if (!payload_bytes_equal(
                            detached_span,
                            std::span<const std::byte>(payload.data(),
                                                       payload.size()))) {
                        continue;
                    }
                    append_unique_string_value(&signature->linked_claim_prefixes,
                                               claims[claim_index].prefix);
                    matched_claim = true;
                    break;
                }
                if (matched_claim) {
                    continue;
                }
            }
        }

        signature->explicit_reference_resolved_claim_count
            = static_cast<uint64_t>(signature->linked_claim_prefixes.size());
        signature->explicit_reference_unresolved
            = (signature->explicit_reference_resolved_claim_count == 0U);
        signature->explicit_reference_ambiguous
            = (signature->explicit_reference_resolved_claim_count > 1U);
    }

    static void append_signature_nested_claim_links(
        const std::vector<ClaimProjection>& claims,
        SignatureProjection* signature) noexcept
    {
        if (!signature) {
            return;
        }
        for (const ClaimProjection& claim : claims) {
            if (!string_starts_with(signature->prefix, claim.prefix)
                || signature->prefix.size() <= claim.prefix.size()
                || signature->prefix[claim.prefix.size()] != '.') {
                continue;
            }
            append_unique_string_value(&signature->linked_claim_prefixes,
                                       claim.prefix);
        }
    }

    static bool append_c2pa_semantic_fields(DecodeContext* ctx) noexcept
    {
        if (!ctx || !ctx->store) {
            return false;
        }

        uint64_t cbor_key_count     = 0U;
        uint64_t assertion_key_hits = 0U;
        uint64_t ingredient_key_hits = 0U;
        uint64_t signature_key_hits = 0U;
        uint64_t reference_key_hits = 0U;
        bool has_manifest           = false;
        bool has_claim              = false;
        bool has_assertions         = false;
        bool has_ingredients        = false;
        bool has_signature          = false;
        bool has_claim_generator    = false;
        std::string claim_generator;
        std::vector<ClaimProjection> claims;
        std::vector<std::string> global_assertions;
        std::vector<std::string> global_ingredients;
        std::vector<SignatureProjection> signatures;
        std::vector<ManifestProjection> manifests;
        manifests.reserve(8U);

        const std::span<const Entry> entries = ctx->store->entries();
        for (const Entry& e : entries) {
            if (e.origin.block != ctx->block
                || e.key.kind != MetaKeyKind::JumbfCborKey) {
                continue;
            }
            cbor_key_count += 1U;

            const std::string_view key
                = arena_string_view(ctx->store->arena(),
                                    e.key.data.jumbf_cbor_key.key);
            std::string manifest_prefix_from_key;
            if (extract_manifest_prefix_from_cbor_key(
                    key, &manifest_prefix_from_key)) {
                const size_t manifest_index
                    = add_or_get_manifest_projection(&manifests,
                                                     manifest_prefix_from_key);
                if (manifest_index != static_cast<size_t>(-1)
                    && manifest_prefix_is_active(manifest_prefix_from_key)) {
                    manifests[manifest_index].is_active_manifest = true;
                }
            }
            if (cbor_key_has_segment(key, "manifest")
                || cbor_key_has_segment(key, "manifests")) {
                has_manifest = true;
            }
            if (cbor_key_has_segment(key, "claim")
                || cbor_key_has_segment(key, "claims")) {
                has_claim = true;
            }
            if (cbor_key_has_segment(key, "assertion")
                || cbor_key_has_segment(key, "assertions")) {
                has_assertions = true;
                assertion_key_hits += 1U;
            }
            if (cbor_key_has_segment(key, "ingredient")
                || cbor_key_has_segment(key, "ingredients")) {
                has_ingredients = true;
                ingredient_key_hits += 1U;
            }
            if (cbor_key_has_segment(key, "signature")
                || cbor_key_has_segment(key, "signatures")) {
                has_signature = true;
                signature_key_hits += 1U;
            }

            if (!has_claim_generator
                && cbor_key_has_segment(key, "claim_generator")
                && e.value.kind == MetaValueKind::Text) {
                const std::span<const std::byte> text
                    = ctx->store->arena().span(e.value.data.span);
                if (bytes_all_ascii_printable(text)) {
                    claim_generator.assign(reinterpret_cast<const char*>(
                                               text.data()),
                                           text.size());
                    has_claim_generator = true;
                }
            }

            size_t key_claim_index = static_cast<size_t>(-1);
            std::string claim_prefix;
            if (find_indexed_segment_prefix(key, ".claims[", &claim_prefix)) {
                const size_t claim_index
                    = add_or_get_claim_projection(&claims, claim_prefix);
                if (claim_index != static_cast<size_t>(-1)) {
                    key_claim_index = claim_index;
                    claims[claim_index].key_hits += 1U;

                    std::string claim_gen_key(claim_prefix);
                    claim_gen_key.append(".claim_generator");
                    if (!claims[claim_index].has_claim_generator
                        && string_starts_with(key, claim_gen_key)
                        && e.value.kind == MetaValueKind::Text) {
                        const std::span<const std::byte> text
                            = ctx->store->arena().span(e.value.data.span);
                        if (bytes_all_ascii_printable(text)) {
                            claims[claim_index].claim_generator.assign(
                                reinterpret_cast<const char*>(text.data()),
                                text.size());
                            claims[claim_index].has_claim_generator = true;
                        }
                    }
                }
            }

            std::string assertion_prefix;
            if (find_indexed_segment_prefix(key, ".assertions[",
                                            &assertion_prefix)) {
                if (!vector_contains_string(global_assertions,
                                            assertion_prefix)) {
                    global_assertions.push_back(assertion_prefix);
                }
                for (ClaimProjection& claim : claims) {
                    if (string_starts_with(assertion_prefix, claim.prefix)
                        && assertion_prefix.size() > claim.prefix.size()
                        && assertion_prefix[claim.prefix.size()] == '.') {
                        const size_t assertion_index
                            = add_or_get_assertion_projection(&claim.assertions,
                                                              assertion_prefix);
                        if (assertion_index != static_cast<size_t>(-1)) {
                            claim.assertions[assertion_index].key_hits += 1U;
                        }
                    }
                }
            }

            std::string ingredient_prefix;
            if (find_indexed_segment_prefix(key, ".ingredients[",
                                            &ingredient_prefix)) {
                if (!vector_contains_string(global_ingredients,
                                            ingredient_prefix)) {
                    global_ingredients.push_back(ingredient_prefix);
                }
                for (ClaimProjection& claim : claims) {
                    if (string_starts_with(ingredient_prefix, claim.prefix)
                        && ingredient_prefix.size() > claim.prefix.size()
                        && ingredient_prefix[claim.prefix.size()] == '.') {
                        const size_t ingredient_index
                            = add_or_get_ingredient_projection(
                                &claim.ingredients, ingredient_prefix);
                        if (ingredient_index != static_cast<size_t>(-1)) {
                            claim.ingredients[ingredient_index].key_hits += 1U;
                            const bool ingredient_title_key
                                = key_is_in_prefix_scope(key, ingredient_prefix)
                                  && (key_matches_field(key, ingredient_prefix,
                                                        "title")
                                      || string_ends_with_ascii_icase(
                                          key, ".title")
                                      || string_ends_with_ascii_icase(
                                          key, ".dc_title")
                                      || string_ends_with_ascii_icase(
                                          key, ".dc.title"));
                            if (!claim.ingredients[ingredient_index].has_title
                                && ingredient_title_key
                                && e.value.kind == MetaValueKind::Text) {
                                const std::span<const std::byte> text
                                    = ctx->store->arena().span(
                                        e.value.data.span);
                                if (bytes_all_ascii_printable(text)) {
                                    claim.ingredients[ingredient_index].title
                                        .assign(
                                            reinterpret_cast<const char*>(
                                                text.data()),
                                            text.size());
                                    claim.ingredients[ingredient_index]
                                        .has_title = true;
                                }
                            }
                            if (!claim.ingredients[ingredient_index]
                                     .has_relationship
                                && key_matches_field(key, ingredient_prefix,
                                                     "relationship")
                                && e.value.kind == MetaValueKind::Text) {
                                const std::span<const std::byte> text
                                    = ctx->store->arena().span(
                                        e.value.data.span);
                                if (bytes_all_ascii_printable(text)) {
                                    claim.ingredients[ingredient_index]
                                        .relationship.assign(
                                            reinterpret_cast<const char*>(
                                                text.data()),
                                            text.size());
                                    claim.ingredients[ingredient_index]
                                        .has_relationship = true;
                                }
                            }
                            if (!claim.ingredients[ingredient_index]
                                     .has_thumbnail_url
                                && key_matches_field(key, ingredient_prefix,
                                                     "thumbnailUrl")
                                && e.value.kind == MetaValueKind::Text) {
                                const std::span<const std::byte> text
                                    = ctx->store->arena().span(
                                        e.value.data.span);
                                if (bytes_all_ascii_printable(text)) {
                                    claim.ingredients[ingredient_index]
                                        .thumbnail_url.assign(
                                            reinterpret_cast<const char*>(
                                                text.data()),
                                            text.size());
                                    claim.ingredients[ingredient_index]
                                        .has_thumbnail_url = true;
                                }
                            }
                        }
                    }
                }
            }

            std::string signature_prefix;
            if (find_indexed_segment_prefix(key, ".signatures[",
                                            &signature_prefix)) {
                bool has_algorithm = false;
                std::string algorithm_value;
                std::string alg_key(signature_prefix);
                alg_key.append(".alg");
                std::string algorithm_key(signature_prefix);
                algorithm_key.append(".algorithm");
                if ((string_starts_with(key, alg_key)
                     || string_starts_with(key, algorithm_key))
                    && e.value.kind == MetaValueKind::Text) {
                    const std::span<const std::byte> text
                        = ctx->store->arena().span(e.value.data.span);
                    if (bytes_all_ascii_printable(text)) {
                        has_algorithm = true;
                        algorithm_value.assign(reinterpret_cast<const char*>(
                                                   text.data()),
                                               text.size());
                    }
                }

                const size_t signature_index
                    = add_or_get_signature_projection(&signatures,
                                                      signature_prefix);
                if (signature_index != static_cast<size_t>(-1)) {
                    signatures[signature_index].key_hits += 1U;
                    if (cbor_key_is_claim_reference_field(key)
                        || cbor_key_is_reference_index_field(key)
                        || cbor_key_is_reference_id_field(key)) {
                        signatures[signature_index].reference_key_hits += 1U;
                        signatures[signature_index].has_explicit_reference
                            = true;
                        reference_key_hits += 1U;
                    }
                    if (!signatures[signature_index].has_algorithm
                        && has_algorithm) {
                        signatures[signature_index].algorithm = algorithm_value;
                        signatures[signature_index].has_algorithm = true;
                    }
                }

                for (ClaimProjection& claim : claims) {
                    if (!string_starts_with(signature_prefix, claim.prefix)
                        || signature_prefix.size() <= claim.prefix.size()
                        || signature_prefix[claim.prefix.size()] != '.') {
                        continue;
                    }
                    const size_t claim_signature_index
                        = add_or_get_claim_signature_projection(
                            &claim.signatures, signature_prefix);
                    if (claim_signature_index == static_cast<size_t>(-1)) {
                        continue;
                    }
                    claim.signatures[claim_signature_index].key_hits += 1U;
                    if (!claim.signatures[claim_signature_index].has_algorithm
                        && has_algorithm) {
                        claim.signatures[claim_signature_index].algorithm
                            = algorithm_value;
                        claim.signatures[claim_signature_index].has_algorithm
                            = true;
                    }
                }
            }

            if (key_claim_index != static_cast<size_t>(-1)
                && (cbor_key_has_segment(key, "signature")
                    || cbor_key_has_segment(key, "signatures"))) {
                claims[key_claim_index].signature_key_hits += 1U;
            }
        }

        uint64_t label_manifest_present = 0U;
        uint64_t label_claim_count      = 0U;
        uint64_t label_signature_count  = 0U;
        uint64_t label_signature_linked = 0U;
        uint64_t label_signature_orphan = 0U;
        const bool have_label_summary   = collect_c2pa_label_summary(
            *ctx, &label_manifest_present, &label_claim_count,
            &label_signature_count, &label_signature_linked,
            &label_signature_orphan);

        if (cbor_key_count == 0U && !have_label_summary) {
            return true;
        }

        if (have_label_summary) {
            has_manifest  = has_manifest || (label_manifest_present != 0U);
            has_claim     = has_claim || (label_claim_count != 0U);
            has_signature = has_signature || (label_signature_count != 0U);
        }

        const bool use_label_counts = have_label_summary && claims.empty()
                                      && signatures.empty();
        uint64_t signature_linked_count                        = 0U;
        uint64_t cross_claim_link_count                        = 0U;
        uint64_t explicit_reference_signature_count            = 0U;
        uint64_t explicit_reference_unresolved_signature_count = 0U;
        uint64_t explicit_reference_ambiguous_signature_count  = 0U;
        uint64_t explicit_reference_index_hits                 = 0U;
        uint64_t explicit_reference_label_hits                 = 0U;
        uint64_t ingredient_signature_count                   = 0U;
        uint64_t ingredient_linked_signature_count            = 0U;
        uint64_t ingredient_linked_direct_claim_count        = 0U;
        uint64_t ingredient_linked_cross_claim_count         = 0U;
        uint64_t ingredient_linked_signature_direct_source_count = 0U;
        uint64_t ingredient_linked_signature_cross_source_count = 0U;
        uint64_t ingredient_linked_signature_mixed_source_count = 0U;
        uint64_t ingredient_linked_signature_direct_title_count = 0U;
        uint64_t ingredient_linked_signature_cross_title_count = 0U;
        uint64_t ingredient_linked_signature_direct_relationship_count = 0U;
        uint64_t ingredient_linked_signature_cross_relationship_count
            = 0U;
        uint64_t ingredient_linked_signature_direct_thumbnail_url_count = 0U;
        uint64_t ingredient_linked_signature_cross_thumbnail_url_count
            = 0U;
        uint64_t ingredient_linked_signature_title_count      = 0U;
        uint64_t ingredient_linked_signature_relationship_count = 0U;
        uint64_t ingredient_linked_signature_thumbnail_url_count = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_direct_claim_count
            = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_cross_claim_count
            = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_direct_source_count
            = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_cross_source_count
            = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_mixed_source_count
            = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_direct_title_count
            = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_cross_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_direct_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_cross_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_direct_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_cross_thumbnail_url_count
            = 0U;
        uint64_t ingredient_linked_signature_explicit_reference_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_direct_claim_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_cross_claim_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_direct_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_cross_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_mixed_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_direct_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_cross_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_direct_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_cross_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_direct_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_cross_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_unresolved_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_direct_claim_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_cross_claim_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_direct_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_cross_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_mixed_source_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_direct_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_cross_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_direct_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_cross_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_direct_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_cross_thumbnail_url_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_title_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_relationship_count
            = 0U;
        uint64_t
            ingredient_linked_signature_explicit_reference_ambiguous_thumbnail_url_count
            = 0U;
        uint64_t ingredient_explicit_reference_signature_count = 0U;
        uint64_t ingredient_explicit_reference_unresolved_signature_count
            = 0U;
        uint64_t ingredient_explicit_reference_ambiguous_signature_count
            = 0U;
        std::vector<ClaimProjection::NamedCount>
            ingredient_linked_signature_relationship_counts;
        std::vector<ClaimProjection::NamedCount>
            ingredient_linked_signature_explicit_reference_relationship_counts;
        std::vector<ClaimProjection::NamedCount>
            ingredient_linked_signature_explicit_reference_unresolved_relationship_counts;
        std::vector<ClaimProjection::NamedCount>
            ingredient_linked_signature_explicit_reference_ambiguous_relationship_counts;
        if (!use_label_counts) {
            const uint64_t max_detached_bytes = cbor_limit_or_default(
                ctx->options.limits.max_cbor_bytes_bytes, 8U * 1024U * 1024U);

            std::vector<std::vector<std::vector<std::byte>>>
                claim_payloads_by_index;
            claim_payloads_by_index.resize(claims.size());
            for (size_t claim_index = 0U; claim_index < claims.size();
                 ++claim_index) {
                collect_claim_payloads_for_prefix(
                    *ctx, claims[claim_index].prefix, max_detached_bytes,
                    &claim_payloads_by_index[claim_index]);
            }

            for (SignatureProjection& signature : signatures) {
                append_signature_explicit_reference_links(
                    *ctx, claims, claim_payloads_by_index, max_detached_bytes,
                    &signature);
                if (signature.linked_claim_prefixes.empty()
                    && !signature.has_explicit_reference) {
                    append_signature_nested_claim_links(claims, &signature);
                }
            }

            for (ClaimProjection& claim : claims) {
                claim.referenced_by_signature_count = 0U;
            }
            for (SignatureProjection& signature : signatures) {
                signature.linked_claim_count = static_cast<uint64_t>(
                    signature.linked_claim_prefixes.size());
                signature.linked_ingredient_claim_count = 0U;
                signature.linked_direct_ingredient_claim_count = 0U;
                signature.linked_cross_ingredient_claim_count = 0U;
                if (signature.has_explicit_reference) {
                    explicit_reference_signature_count += 1U;
                    explicit_reference_index_hits
                        += signature.explicit_reference_index_hits;
                    explicit_reference_label_hits
                        += signature.explicit_reference_label_hits;
                    if (signature.explicit_reference_unresolved) {
                        explicit_reference_unresolved_signature_count += 1U;
                    }
                    if (signature.explicit_reference_ambiguous) {
                        explicit_reference_ambiguous_signature_count += 1U;
                    }
                }
                if (signature.linked_claim_count != 0U) {
                    signature_linked_count += 1U;
                }

                std::string direct_claim_prefix;
                for (const ClaimProjection& claim : claims) {
                    if (!string_starts_with(signature.prefix, claim.prefix)
                        || signature.prefix.size() <= claim.prefix.size()
                        || signature.prefix[claim.prefix.size()] != '.') {
                        continue;
                    }
                    direct_claim_prefix = claim.prefix;
                    break;
                }
                signature.direct_claim_prefix = direct_claim_prefix;
                signature.direct_claim_has_ingredients = false;
                if (!direct_claim_prefix.empty()) {
                    const size_t direct_claim_index
                        = find_claim_projection(claims, direct_claim_prefix);
                    if (direct_claim_index != static_cast<size_t>(-1)
                        && !claims[direct_claim_index].ingredients.empty()) {
                        signature.direct_claim_has_ingredients = true;
                    }
                }
                if (signature.direct_claim_has_ingredients) {
                    ingredient_signature_count += 1U;
                    if (signature.has_explicit_reference) {
                        if (signature.explicit_reference_unresolved) {
                            ingredient_explicit_reference_unresolved_signature_count
                                += 1U;
                        }
                        if (signature.explicit_reference_ambiguous) {
                            ingredient_explicit_reference_ambiguous_signature_count
                                += 1U;
                        }
                    }
                }

                signature.cross_claim_link_count = 0U;
                for (const std::string& linked_claim_prefix :
                     signature.linked_claim_prefixes) {
                    const bool is_direct_link = !direct_claim_prefix.empty()
                                                && linked_claim_prefix
                                                       == direct_claim_prefix;
                    if (direct_claim_prefix.empty()
                        || linked_claim_prefix != direct_claim_prefix) {
                        signature.cross_claim_link_count += 1U;
                    }
                    const size_t linked_claim_index
                        = find_claim_projection(claims, linked_claim_prefix);
                    if (linked_claim_index != static_cast<size_t>(-1)) {
                        if (!claims[linked_claim_index].ingredients.empty()) {
                            signature.linked_ingredient_claim_count += 1U;
                            if (is_direct_link) {
                                signature.linked_direct_ingredient_claim_count
                                    += 1U;
                            } else {
                                signature.linked_cross_ingredient_claim_count
                                    += 1U;
                            }
                        }
                        claims[linked_claim_index].referenced_by_signature_count
                            += 1U;
                    }
                }
                if (signature.linked_ingredient_claim_count != 0U) {
                    ingredient_linked_signature_count += 1U;
                    if (signature.has_explicit_reference) {
                        ingredient_explicit_reference_signature_count += 1U;
                    }
                }
                cross_claim_link_count += signature.cross_claim_link_count;
            }
        } else {
            signature_linked_count = label_signature_linked;
        }

        manifests.reserve(manifests.size() + claims.size() + signatures.size());

        uint64_t ingredient_relationship_count = 0U;
        uint64_t ingredient_thumbnail_url_count = 0U;
        std::vector<ClaimProjection::NamedCount> ingredient_relationship_counts;
        for (ClaimProjection& claim : claims) {
            claim.ingredient_title_count = 0U;
            claim.ingredient_relationship_count = 0U;
            claim.ingredient_thumbnail_url_count = 0U;
            claim.linked_ingredient_signature_count = 0U;
            claim.linked_direct_ingredient_signature_count = 0U;
            claim.linked_cross_ingredient_signature_count = 0U;
            claim.linked_ingredient_title_count = 0U;
            claim.linked_ingredient_relationship_count = 0U;
            claim.linked_ingredient_thumbnail_url_count = 0U;
            claim.linked_ingredient_explicit_reference_signature_count = 0U;
            claim.linked_ingredient_explicit_reference_direct_signature_count
                = 0U;
            claim.linked_ingredient_explicit_reference_cross_signature_count
                = 0U;
            claim.linked_ingredient_explicit_reference_title_count = 0U;
            claim.linked_ingredient_explicit_reference_relationship_count = 0U;
            claim.linked_ingredient_explicit_reference_thumbnail_url_count
                = 0U;
            claim.linked_ingredient_explicit_reference_unresolved_signature_count
                = 0U;
            claim
                .linked_ingredient_explicit_reference_unresolved_direct_signature_count
                = 0U;
            claim
                .linked_ingredient_explicit_reference_unresolved_cross_signature_count
                = 0U;
            claim.linked_ingredient_explicit_reference_unresolved_title_count
                = 0U;
            claim
                .linked_ingredient_explicit_reference_unresolved_relationship_count
                = 0U;
            claim
                .linked_ingredient_explicit_reference_unresolved_thumbnail_url_count
                = 0U;
            claim.linked_ingredient_explicit_reference_ambiguous_signature_count
                = 0U;
            claim
                .linked_ingredient_explicit_reference_ambiguous_direct_signature_count
                = 0U;
            claim
                .linked_ingredient_explicit_reference_ambiguous_cross_signature_count
                = 0U;
            claim.linked_ingredient_explicit_reference_ambiguous_title_count
                = 0U;
            claim
                .linked_ingredient_explicit_reference_ambiguous_relationship_count
                = 0U;
            claim
                .linked_ingredient_explicit_reference_ambiguous_thumbnail_url_count
                = 0U;
            claim.ingredient_relationship_counts.clear();
            claim.linked_ingredient_relationship_counts.clear();
            claim.linked_ingredient_explicit_reference_relationship_counts
                .clear();
            claim.linked_ingredient_explicit_reference_unresolved_relationship_counts
                .clear();
            claim.linked_ingredient_explicit_reference_ambiguous_relationship_counts
                .clear();
            for (const ClaimProjection::IngredientProjection& ingredient :
                claim.ingredients) {
                if (ingredient.has_title) {
                    claim.ingredient_title_count += 1U;
                }
                if (ingredient.has_relationship) {
                    std::string relationship_key;
                    if (sanitize_ascii_summary_segment(
                            ingredient.relationship,
                            ctx->options.limits.max_cbor_key_bytes,
                            &relationship_key)) {
                        add_or_increment_named_count(
                            &claim.ingredient_relationship_counts,
                            relationship_key, 1U);
                        add_or_increment_named_count(
                            &ingredient_relationship_counts,
                            relationship_key, 1U);
                    }
                    claim.ingredient_relationship_count += 1U;
                    ingredient_relationship_count += 1U;
                }
                if (ingredient.has_thumbnail_url) {
                    claim.ingredient_thumbnail_url_count += 1U;
                    ingredient_thumbnail_url_count += 1U;
                }
            }
        }

        for (SignatureProjection& signature : signatures) {
            signature.linked_direct_ingredient_title_count = 0U;
            signature.linked_cross_ingredient_title_count = 0U;
            signature.linked_direct_ingredient_relationship_count = 0U;
            signature.linked_cross_ingredient_relationship_count = 0U;
            signature.linked_direct_ingredient_thumbnail_url_count = 0U;
            signature.linked_cross_ingredient_thumbnail_url_count = 0U;
            signature.linked_ingredient_title_count = 0U;
            signature.linked_ingredient_relationship_count = 0U;
            signature.linked_ingredient_thumbnail_url_count = 0U;
            signature.linked_ingredient_relationship_counts.clear();
            for (const std::string& linked_claim_prefix :
                 signature.linked_claim_prefixes) {
                const bool is_direct_link
                    = !signature.direct_claim_prefix.empty()
                      && linked_claim_prefix == signature.direct_claim_prefix;
                const size_t linked_claim_index
                    = find_claim_projection(claims, linked_claim_prefix);
                if (linked_claim_index == static_cast<size_t>(-1)) {
                    continue;
                }
                ClaimProjection& linked_claim = claims[linked_claim_index];
                if (!linked_claim.ingredients.empty()) {
                    linked_claim.linked_ingredient_signature_count += 1U;
                    if (is_direct_link) {
                        linked_claim.linked_direct_ingredient_signature_count
                            += 1U;
                    } else {
                        linked_claim.linked_cross_ingredient_signature_count
                            += 1U;
                    }
                    linked_claim.linked_ingredient_title_count
                        += linked_claim.ingredient_title_count;
                    linked_claim.linked_ingredient_relationship_count
                        += linked_claim.ingredient_relationship_count;
                    linked_claim.linked_ingredient_thumbnail_url_count
                        += linked_claim.ingredient_thumbnail_url_count;
                    for (const ClaimProjection::NamedCount& named_count :
                         linked_claim.ingredient_relationship_counts) {
                        add_or_increment_named_count(
                            &linked_claim
                                 .linked_ingredient_relationship_counts,
                            named_count.key, named_count.count);
                    }
                    if (signature.has_explicit_reference) {
                        linked_claim
                            .linked_ingredient_explicit_reference_signature_count
                            += 1U;
                        if (is_direct_link) {
                            linked_claim
                                .linked_ingredient_explicit_reference_direct_signature_count
                                += 1U;
                        } else {
                            linked_claim
                                .linked_ingredient_explicit_reference_cross_signature_count
                                += 1U;
                        }
                        linked_claim
                            .linked_ingredient_explicit_reference_title_count
                            += linked_claim.ingredient_title_count;
                        linked_claim
                            .linked_ingredient_explicit_reference_relationship_count
                            += linked_claim.ingredient_relationship_count;
                        linked_claim
                            .linked_ingredient_explicit_reference_thumbnail_url_count
                            += linked_claim.ingredient_thumbnail_url_count;
                        for (const ClaimProjection::NamedCount& named_count :
                             linked_claim.ingredient_relationship_counts) {
                            add_or_increment_named_count(
                                &linked_claim
                                     .linked_ingredient_explicit_reference_relationship_counts,
                                named_count.key, named_count.count);
                        }
                        if (signature.explicit_reference_unresolved) {
                            linked_claim
                                .linked_ingredient_explicit_reference_unresolved_signature_count
                                += 1U;
                            if (is_direct_link) {
                                linked_claim
                                    .linked_ingredient_explicit_reference_unresolved_direct_signature_count
                                    += 1U;
                            } else {
                                linked_claim
                                    .linked_ingredient_explicit_reference_unresolved_cross_signature_count
                                    += 1U;
                            }
                            linked_claim
                                .linked_ingredient_explicit_reference_unresolved_title_count
                                += linked_claim.ingredient_title_count;
                            linked_claim
                                .linked_ingredient_explicit_reference_unresolved_relationship_count
                                += linked_claim
                                       .ingredient_relationship_count;
                            linked_claim
                                .linked_ingredient_explicit_reference_unresolved_thumbnail_url_count
                                += linked_claim
                                       .ingredient_thumbnail_url_count;
                            for (const ClaimProjection::NamedCount& named_count :
                                 linked_claim.ingredient_relationship_counts) {
                                add_or_increment_named_count(
                                    &linked_claim
                                         .linked_ingredient_explicit_reference_unresolved_relationship_counts,
                                    named_count.key, named_count.count);
                            }
                        }
                        if (signature.explicit_reference_ambiguous) {
                            linked_claim
                                .linked_ingredient_explicit_reference_ambiguous_signature_count
                                += 1U;
                            if (is_direct_link) {
                                linked_claim
                                    .linked_ingredient_explicit_reference_ambiguous_direct_signature_count
                                    += 1U;
                            } else {
                                linked_claim
                                    .linked_ingredient_explicit_reference_ambiguous_cross_signature_count
                                    += 1U;
                            }
                            linked_claim
                                .linked_ingredient_explicit_reference_ambiguous_title_count
                                += linked_claim.ingredient_title_count;
                            linked_claim
                                .linked_ingredient_explicit_reference_ambiguous_relationship_count
                                += linked_claim
                                       .ingredient_relationship_count;
                            linked_claim
                                .linked_ingredient_explicit_reference_ambiguous_thumbnail_url_count
                                += linked_claim
                                       .ingredient_thumbnail_url_count;
                            for (const ClaimProjection::NamedCount& named_count :
                                 linked_claim.ingredient_relationship_counts) {
                                add_or_increment_named_count(
                                    &linked_claim
                                         .linked_ingredient_explicit_reference_ambiguous_relationship_counts,
                                    named_count.key, named_count.count);
                            }
                        }
                    }
                }
                signature.linked_ingredient_title_count
                    += linked_claim.ingredient_title_count;
                signature.linked_ingredient_relationship_count
                    += linked_claim.ingredient_relationship_count;
                signature.linked_ingredient_thumbnail_url_count
                    += linked_claim.ingredient_thumbnail_url_count;
                if (is_direct_link) {
                    signature.linked_direct_ingredient_title_count
                        += linked_claim.ingredient_title_count;
                    signature.linked_direct_ingredient_relationship_count
                        += linked_claim.ingredient_relationship_count;
                    signature.linked_direct_ingredient_thumbnail_url_count
                        += linked_claim.ingredient_thumbnail_url_count;
                } else {
                    signature.linked_cross_ingredient_title_count
                        += linked_claim.ingredient_title_count;
                    signature.linked_cross_ingredient_relationship_count
                        += linked_claim.ingredient_relationship_count;
                    signature.linked_cross_ingredient_thumbnail_url_count
                        += linked_claim.ingredient_thumbnail_url_count;
                }
                for (const ClaimProjection::NamedCount& named_count :
                     linked_claim.ingredient_relationship_counts) {
                    add_or_increment_named_count(
                        &signature.linked_ingredient_relationship_counts,
                        named_count.key, named_count.count);
                }
            }
        }

        uint64_t ingredient_claim_count = 0U;
        uint64_t ingredient_claim_with_signature_count = 0U;
        uint64_t ingredient_claim_referenced_by_signature_count = 0U;
        uint64_t ingredient_linked_claim_count = 0U;
        uint64_t ingredient_linked_claim_explicit_reference_count = 0U;
        uint64_t ingredient_linked_claim_explicit_reference_unresolved_count
            = 0U;
        uint64_t ingredient_linked_claim_explicit_reference_ambiguous_count
            = 0U;
        uint64_t ingredient_linked_claim_direct_source_count = 0U;
        uint64_t ingredient_linked_claim_cross_source_count = 0U;
        uint64_t ingredient_linked_claim_mixed_source_count = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_direct_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_cross_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_mixed_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_unresolved_direct_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_unresolved_cross_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_unresolved_mixed_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_ambiguous_direct_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_ambiguous_cross_source_count
            = 0U;
        uint64_t
            ingredient_linked_claim_explicit_reference_ambiguous_mixed_source_count
            = 0U;
        for (const ClaimProjection& claim : claims) {
            std::string manifest_prefix;
            if (!extract_manifest_prefix_from_projection_prefix(
                    claim.prefix, &manifest_prefix)) {
                continue;
            }
            const size_t manifest_index
                = add_or_get_manifest_projection(&manifests, manifest_prefix);
            if (manifest_index == static_cast<size_t>(-1)) {
                continue;
            }
            ManifestProjection& manifest = manifests[manifest_index];
            if (manifest_prefix_is_active(manifest_prefix)) {
                manifest.is_active_manifest = true;
            }
            manifest.claim_count += 1U;
            if (!claim.ingredients.empty()) {
                ingredient_claim_count += 1U;
                manifest.ingredient_claim_count += 1U;
                if (!claim.signatures.empty()) {
                    ingredient_claim_with_signature_count += 1U;
                    manifest.ingredient_claim_with_signature_count += 1U;
                }
                if (claim.referenced_by_signature_count != 0U) {
                    ingredient_claim_referenced_by_signature_count += 1U;
                    manifest
                        .ingredient_claim_referenced_by_signature_count += 1U;
                }
                if (claim.linked_ingredient_signature_count != 0U) {
                    ingredient_linked_claim_count += 1U;
                    manifest.ingredient_linked_claim_count += 1U;
                }
                if (claim.linked_direct_ingredient_signature_count != 0U) {
                    ingredient_linked_claim_direct_source_count += 1U;
                    manifest.ingredient_linked_claim_direct_source_count += 1U;
                }
                if (claim.linked_cross_ingredient_signature_count != 0U) {
                    ingredient_linked_claim_cross_source_count += 1U;
                    manifest.ingredient_linked_claim_cross_source_count += 1U;
                }
                if (claim.linked_direct_ingredient_signature_count != 0U
                    && claim.linked_cross_ingredient_signature_count != 0U) {
                    ingredient_linked_claim_mixed_source_count += 1U;
                    manifest.ingredient_linked_claim_mixed_source_count += 1U;
                }
                if (claim.linked_ingredient_explicit_reference_signature_count
                    != 0U) {
                    ingredient_linked_claim_explicit_reference_count += 1U;
                    manifest
                        .ingredient_linked_claim_explicit_reference_count += 1U;
                }
                if (claim
                        .linked_ingredient_explicit_reference_direct_signature_count
                    != 0U) {
                    ingredient_linked_claim_explicit_reference_direct_source_count
                        += 1U;
                    manifest
                        .ingredient_linked_claim_explicit_reference_direct_source_count
                        += 1U;
                }
                if (claim
                        .linked_ingredient_explicit_reference_cross_signature_count
                    != 0U) {
                    ingredient_linked_claim_explicit_reference_cross_source_count
                        += 1U;
                    manifest
                        .ingredient_linked_claim_explicit_reference_cross_source_count
                        += 1U;
                }
                if (claim
                        .linked_ingredient_explicit_reference_direct_signature_count
                        != 0U
                    && claim
                           .linked_ingredient_explicit_reference_cross_signature_count
                           != 0U) {
                    ingredient_linked_claim_explicit_reference_mixed_source_count
                        += 1U;
                    manifest
                        .ingredient_linked_claim_explicit_reference_mixed_source_count
                        += 1U;
                }
                if (claim
                        .linked_ingredient_explicit_reference_unresolved_signature_count
                    != 0U) {
                    ingredient_linked_claim_explicit_reference_unresolved_count
                        += 1U;
                    manifest
                        .ingredient_linked_claim_explicit_reference_unresolved_count
                        += 1U;
                }
                if (claim
                        .linked_ingredient_explicit_reference_unresolved_direct_signature_count
                    != 0U) {
                    ingredient_linked_claim_explicit_reference_unresolved_direct_source_count
                        += 1U;
                    manifest
                        .ingredient_linked_claim_explicit_reference_unresolved_direct_source_count
                        += 1U;
                }
                if (claim
                        .linked_ingredient_explicit_reference_unresolved_cross_signature_count
                    != 0U) {
                    ingredient_linked_claim_explicit_reference_unresolved_cross_source_count
                        += 1U;
                    manifest
                        .ingredient_linked_claim_explicit_reference_unresolved_cross_source_count
                        += 1U;
                }
                if (claim
                        .linked_ingredient_explicit_reference_unresolved_direct_signature_count
                        != 0U
                    && claim
                           .linked_ingredient_explicit_reference_unresolved_cross_signature_count
                           != 0U) {
                    ingredient_linked_claim_explicit_reference_unresolved_mixed_source_count
                        += 1U;
                    manifest
                        .ingredient_linked_claim_explicit_reference_unresolved_mixed_source_count
                        += 1U;
                }
                if (claim
                        .linked_ingredient_explicit_reference_ambiguous_signature_count
                    != 0U) {
                    ingredient_linked_claim_explicit_reference_ambiguous_count
                        += 1U;
                    manifest
                        .ingredient_linked_claim_explicit_reference_ambiguous_count
                        += 1U;
                }
                if (claim
                        .linked_ingredient_explicit_reference_ambiguous_direct_signature_count
                    != 0U) {
                    ingredient_linked_claim_explicit_reference_ambiguous_direct_source_count
                        += 1U;
                    manifest
                        .ingredient_linked_claim_explicit_reference_ambiguous_direct_source_count
                        += 1U;
                }
                if (claim
                        .linked_ingredient_explicit_reference_ambiguous_cross_signature_count
                    != 0U) {
                    ingredient_linked_claim_explicit_reference_ambiguous_cross_source_count
                        += 1U;
                    manifest
                        .ingredient_linked_claim_explicit_reference_ambiguous_cross_source_count
                        += 1U;
                }
                if (claim
                        .linked_ingredient_explicit_reference_ambiguous_direct_signature_count
                        != 0U
                    && claim
                           .linked_ingredient_explicit_reference_ambiguous_cross_signature_count
                           != 0U) {
                    ingredient_linked_claim_explicit_reference_ambiguous_mixed_source_count
                        += 1U;
                    manifest
                        .ingredient_linked_claim_explicit_reference_ambiguous_mixed_source_count
                        += 1U;
                }
            }
            manifest.assertion_count += static_cast<uint64_t>(
                claim.assertions.size());
            manifest.ingredient_count += static_cast<uint64_t>(
                claim.ingredients.size());
            manifest.ingredient_relationship_count
                += claim.ingredient_relationship_count;
            manifest.ingredient_thumbnail_url_count
                += claim.ingredient_thumbnail_url_count;
            for (const ClaimProjection::NamedCount& named_count :
                 claim.ingredient_relationship_counts) {
                add_or_increment_named_count(
                    &manifest.ingredient_relationship_counts,
                    named_count.key, named_count.count);
            }
        }

        for (const SignatureProjection& signature : signatures) {
            std::string manifest_prefix;
            if (!extract_manifest_prefix_from_projection_prefix(
                    signature.prefix, &manifest_prefix)) {
                continue;
            }
            const size_t manifest_index
                = add_or_get_manifest_projection(&manifests, manifest_prefix);
            if (manifest_index == static_cast<size_t>(-1)) {
                continue;
            }
            ManifestProjection& manifest = manifests[manifest_index];
            if (manifest_prefix_is_active(manifest_prefix)) {
                manifest.is_active_manifest = true;
            }
            manifest.signature_count += 1U;
            if (signature.direct_claim_has_ingredients) {
                manifest.ingredient_signature_count += 1U;
                if (signature.explicit_reference_unresolved) {
                    manifest
                        .ingredient_explicit_reference_unresolved_signature_count
                        += 1U;
                }
                if (signature.explicit_reference_ambiguous) {
                    manifest
                        .ingredient_explicit_reference_ambiguous_signature_count
                        += 1U;
                }
            }
            if (signature.linked_claim_count != 0U) {
                manifest.signature_linked_count += 1U;
            }
            if (signature.linked_ingredient_claim_count != 0U) {
                manifest.ingredient_linked_signature_count += 1U;
                manifest.ingredient_linked_direct_claim_count
                    += signature.linked_direct_ingredient_claim_count;
                manifest.ingredient_linked_cross_claim_count
                    += signature.linked_cross_ingredient_claim_count;
                ingredient_linked_direct_claim_count
                    += signature.linked_direct_ingredient_claim_count;
                ingredient_linked_cross_claim_count
                    += signature.linked_cross_ingredient_claim_count;
                if (signature.linked_direct_ingredient_claim_count != 0U) {
                    manifest.ingredient_linked_signature_direct_source_count
                        += 1U;
                    ingredient_linked_signature_direct_source_count += 1U;
                }
                if (signature.linked_cross_ingredient_claim_count != 0U) {
                    manifest.ingredient_linked_signature_cross_source_count
                        += 1U;
                    ingredient_linked_signature_cross_source_count += 1U;
                }
                if (signature.linked_direct_ingredient_claim_count != 0U
                    && signature.linked_cross_ingredient_claim_count != 0U) {
                    manifest.ingredient_linked_signature_mixed_source_count
                        += 1U;
                    ingredient_linked_signature_mixed_source_count += 1U;
                }
                manifest.ingredient_linked_signature_direct_title_count
                    += signature.linked_direct_ingredient_title_count;
                manifest.ingredient_linked_signature_cross_title_count
                    += signature.linked_cross_ingredient_title_count;
                manifest.ingredient_linked_signature_direct_relationship_count
                    += signature.linked_direct_ingredient_relationship_count;
                manifest.ingredient_linked_signature_cross_relationship_count
                    += signature.linked_cross_ingredient_relationship_count;
                manifest.ingredient_linked_signature_direct_thumbnail_url_count
                    += signature.linked_direct_ingredient_thumbnail_url_count;
                manifest.ingredient_linked_signature_cross_thumbnail_url_count
                    += signature.linked_cross_ingredient_thumbnail_url_count;
                ingredient_linked_signature_direct_title_count
                    += signature.linked_direct_ingredient_title_count;
                ingredient_linked_signature_cross_title_count
                    += signature.linked_cross_ingredient_title_count;
                ingredient_linked_signature_direct_relationship_count
                    += signature.linked_direct_ingredient_relationship_count;
                ingredient_linked_signature_cross_relationship_count
                    += signature.linked_cross_ingredient_relationship_count;
                ingredient_linked_signature_direct_thumbnail_url_count
                    += signature.linked_direct_ingredient_thumbnail_url_count;
                ingredient_linked_signature_cross_thumbnail_url_count
                    += signature.linked_cross_ingredient_thumbnail_url_count;
                manifest.ingredient_linked_signature_title_count
                    += signature.linked_ingredient_title_count;
                manifest.ingredient_linked_signature_relationship_count
                    += signature.linked_ingredient_relationship_count;
                manifest.ingredient_linked_signature_thumbnail_url_count
                    += signature.linked_ingredient_thumbnail_url_count;
                ingredient_linked_signature_title_count
                    += signature.linked_ingredient_title_count;
                ingredient_linked_signature_relationship_count
                    += signature.linked_ingredient_relationship_count;
                ingredient_linked_signature_thumbnail_url_count
                    += signature.linked_ingredient_thumbnail_url_count;
                for (const ClaimProjection::NamedCount& named_count :
                     signature.linked_ingredient_relationship_counts) {
                    add_or_increment_named_count(
                        &manifest
                             .ingredient_linked_signature_relationship_counts,
                        named_count.key, named_count.count);
                    add_or_increment_named_count(
                        &ingredient_linked_signature_relationship_counts,
                        named_count.key, named_count.count);
                }
                if (signature.has_explicit_reference) {
                    manifest.ingredient_explicit_reference_signature_count
                        += 1U;
                    manifest
                        .ingredient_linked_signature_explicit_reference_direct_claim_count
                        += signature.linked_direct_ingredient_claim_count;
                    manifest
                        .ingredient_linked_signature_explicit_reference_cross_claim_count
                        += signature.linked_cross_ingredient_claim_count;
                    ingredient_linked_signature_explicit_reference_direct_claim_count
                        += signature.linked_direct_ingredient_claim_count;
                    ingredient_linked_signature_explicit_reference_cross_claim_count
                        += signature.linked_cross_ingredient_claim_count;
                    if (signature.linked_direct_ingredient_claim_count != 0U) {
                        manifest
                            .ingredient_linked_signature_explicit_reference_direct_source_count
                            += 1U;
                        ingredient_linked_signature_explicit_reference_direct_source_count
                            += 1U;
                    }
                    if (signature.linked_cross_ingredient_claim_count != 0U) {
                        manifest
                            .ingredient_linked_signature_explicit_reference_cross_source_count
                            += 1U;
                        ingredient_linked_signature_explicit_reference_cross_source_count
                            += 1U;
                    }
                    if (signature.linked_direct_ingredient_claim_count != 0U
                        && signature.linked_cross_ingredient_claim_count
                               != 0U) {
                        manifest
                            .ingredient_linked_signature_explicit_reference_mixed_source_count
                            += 1U;
                        ingredient_linked_signature_explicit_reference_mixed_source_count
                            += 1U;
                    }
                    manifest
                        .ingredient_linked_signature_explicit_reference_direct_title_count
                        += signature.linked_direct_ingredient_title_count;
                    manifest
                        .ingredient_linked_signature_explicit_reference_cross_title_count
                        += signature.linked_cross_ingredient_title_count;
                    manifest
                        .ingredient_linked_signature_explicit_reference_direct_relationship_count
                        += signature
                               .linked_direct_ingredient_relationship_count;
                    manifest
                        .ingredient_linked_signature_explicit_reference_cross_relationship_count
                        += signature
                               .linked_cross_ingredient_relationship_count;
                    manifest
                        .ingredient_linked_signature_explicit_reference_direct_thumbnail_url_count
                        += signature
                               .linked_direct_ingredient_thumbnail_url_count;
                    manifest
                        .ingredient_linked_signature_explicit_reference_cross_thumbnail_url_count
                        += signature
                               .linked_cross_ingredient_thumbnail_url_count;
                    ingredient_linked_signature_explicit_reference_direct_title_count
                        += signature.linked_direct_ingredient_title_count;
                    ingredient_linked_signature_explicit_reference_cross_title_count
                        += signature.linked_cross_ingredient_title_count;
                    ingredient_linked_signature_explicit_reference_direct_relationship_count
                        += signature.linked_direct_ingredient_relationship_count;
                    ingredient_linked_signature_explicit_reference_cross_relationship_count
                        += signature.linked_cross_ingredient_relationship_count;
                    ingredient_linked_signature_explicit_reference_direct_thumbnail_url_count
                        += signature.linked_direct_ingredient_thumbnail_url_count;
                    ingredient_linked_signature_explicit_reference_cross_thumbnail_url_count
                        += signature.linked_cross_ingredient_thumbnail_url_count;
                    manifest
                        .ingredient_linked_signature_explicit_reference_title_count
                        += signature.linked_ingredient_title_count;
                    manifest
                        .ingredient_linked_signature_explicit_reference_relationship_count
                        += signature.linked_ingredient_relationship_count;
                    manifest
                        .ingredient_linked_signature_explicit_reference_thumbnail_url_count
                        += signature.linked_ingredient_thumbnail_url_count;
                    ingredient_linked_signature_explicit_reference_title_count
                        += signature.linked_ingredient_title_count;
                    ingredient_linked_signature_explicit_reference_relationship_count
                        += signature.linked_ingredient_relationship_count;
                    ingredient_linked_signature_explicit_reference_thumbnail_url_count
                        += signature.linked_ingredient_thumbnail_url_count;
                    for (const ClaimProjection::NamedCount& named_count :
                         signature.linked_ingredient_relationship_counts) {
                        add_or_increment_named_count(
                            &manifest
                                 .ingredient_linked_signature_explicit_reference_relationship_counts,
                            named_count.key, named_count.count);
                        add_or_increment_named_count(
                            &ingredient_linked_signature_explicit_reference_relationship_counts,
                            named_count.key, named_count.count);
                    }
                    if (signature.explicit_reference_unresolved) {
                        manifest
                            .ingredient_linked_signature_explicit_reference_unresolved_direct_claim_count
                            += signature.linked_direct_ingredient_claim_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_unresolved_cross_claim_count
                            += signature.linked_cross_ingredient_claim_count;
                        ingredient_linked_signature_explicit_reference_unresolved_direct_claim_count
                            += signature.linked_direct_ingredient_claim_count;
                        ingredient_linked_signature_explicit_reference_unresolved_cross_claim_count
                            += signature.linked_cross_ingredient_claim_count;
                        if (signature.linked_direct_ingredient_claim_count
                            != 0U) {
                            manifest
                                .ingredient_linked_signature_explicit_reference_unresolved_direct_source_count
                                += 1U;
                            ingredient_linked_signature_explicit_reference_unresolved_direct_source_count
                                += 1U;
                        }
                        if (signature.linked_cross_ingredient_claim_count
                            != 0U) {
                            manifest
                                .ingredient_linked_signature_explicit_reference_unresolved_cross_source_count
                                += 1U;
                            ingredient_linked_signature_explicit_reference_unresolved_cross_source_count
                                += 1U;
                        }
                        if (signature.linked_direct_ingredient_claim_count
                                != 0U
                            && signature.linked_cross_ingredient_claim_count
                                   != 0U) {
                            manifest
                                .ingredient_linked_signature_explicit_reference_unresolved_mixed_source_count
                                += 1U;
                            ingredient_linked_signature_explicit_reference_unresolved_mixed_source_count
                                += 1U;
                        }
                        manifest
                            .ingredient_linked_signature_explicit_reference_unresolved_direct_title_count
                            += signature.linked_direct_ingredient_title_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_unresolved_cross_title_count
                            += signature.linked_cross_ingredient_title_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_unresolved_direct_relationship_count
                            += signature
                                   .linked_direct_ingredient_relationship_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_unresolved_cross_relationship_count
                            += signature
                                   .linked_cross_ingredient_relationship_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_unresolved_direct_thumbnail_url_count
                            += signature
                                   .linked_direct_ingredient_thumbnail_url_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_unresolved_cross_thumbnail_url_count
                            += signature
                                   .linked_cross_ingredient_thumbnail_url_count;
                        ingredient_linked_signature_explicit_reference_unresolved_direct_title_count
                            += signature.linked_direct_ingredient_title_count;
                        ingredient_linked_signature_explicit_reference_unresolved_cross_title_count
                            += signature.linked_cross_ingredient_title_count;
                        ingredient_linked_signature_explicit_reference_unresolved_direct_relationship_count
                            += signature
                                   .linked_direct_ingredient_relationship_count;
                        ingredient_linked_signature_explicit_reference_unresolved_cross_relationship_count
                            += signature
                                   .linked_cross_ingredient_relationship_count;
                        ingredient_linked_signature_explicit_reference_unresolved_direct_thumbnail_url_count
                            += signature
                                   .linked_direct_ingredient_thumbnail_url_count;
                        ingredient_linked_signature_explicit_reference_unresolved_cross_thumbnail_url_count
                            += signature
                                   .linked_cross_ingredient_thumbnail_url_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_unresolved_title_count
                            += signature.linked_ingredient_title_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_unresolved_relationship_count
                            += signature.linked_ingredient_relationship_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_unresolved_thumbnail_url_count
                            += signature.linked_ingredient_thumbnail_url_count;
                        ingredient_linked_signature_explicit_reference_unresolved_title_count
                            += signature.linked_ingredient_title_count;
                        ingredient_linked_signature_explicit_reference_unresolved_relationship_count
                            += signature.linked_ingredient_relationship_count;
                        ingredient_linked_signature_explicit_reference_unresolved_thumbnail_url_count
                            += signature.linked_ingredient_thumbnail_url_count;
                        for (const ClaimProjection::NamedCount& named_count :
                             signature.linked_ingredient_relationship_counts) {
                            add_or_increment_named_count(
                                &manifest
                                     .ingredient_linked_signature_explicit_reference_unresolved_relationship_counts,
                                named_count.key, named_count.count);
                            add_or_increment_named_count(
                                &ingredient_linked_signature_explicit_reference_unresolved_relationship_counts,
                                named_count.key, named_count.count);
                        }
                    }
                    if (signature.explicit_reference_ambiguous) {
                        manifest
                            .ingredient_linked_signature_explicit_reference_ambiguous_direct_claim_count
                            += signature.linked_direct_ingredient_claim_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_ambiguous_cross_claim_count
                            += signature.linked_cross_ingredient_claim_count;
                        ingredient_linked_signature_explicit_reference_ambiguous_direct_claim_count
                            += signature.linked_direct_ingredient_claim_count;
                        ingredient_linked_signature_explicit_reference_ambiguous_cross_claim_count
                            += signature.linked_cross_ingredient_claim_count;
                        if (signature.linked_direct_ingredient_claim_count
                            != 0U) {
                            manifest
                                .ingredient_linked_signature_explicit_reference_ambiguous_direct_source_count
                                += 1U;
                            ingredient_linked_signature_explicit_reference_ambiguous_direct_source_count
                                += 1U;
                        }
                        if (signature.linked_cross_ingredient_claim_count
                            != 0U) {
                            manifest
                                .ingredient_linked_signature_explicit_reference_ambiguous_cross_source_count
                                += 1U;
                            ingredient_linked_signature_explicit_reference_ambiguous_cross_source_count
                                += 1U;
                        }
                        if (signature.linked_direct_ingredient_claim_count
                                != 0U
                            && signature.linked_cross_ingredient_claim_count
                                   != 0U) {
                            manifest
                                .ingredient_linked_signature_explicit_reference_ambiguous_mixed_source_count
                                += 1U;
                            ingredient_linked_signature_explicit_reference_ambiguous_mixed_source_count
                                += 1U;
                        }
                        manifest
                            .ingredient_linked_signature_explicit_reference_ambiguous_direct_title_count
                            += signature.linked_direct_ingredient_title_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_ambiguous_cross_title_count
                            += signature.linked_cross_ingredient_title_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_ambiguous_direct_relationship_count
                            += signature
                                   .linked_direct_ingredient_relationship_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_ambiguous_cross_relationship_count
                            += signature
                                   .linked_cross_ingredient_relationship_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_ambiguous_direct_thumbnail_url_count
                            += signature
                                   .linked_direct_ingredient_thumbnail_url_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_ambiguous_cross_thumbnail_url_count
                            += signature
                                   .linked_cross_ingredient_thumbnail_url_count;
                        ingredient_linked_signature_explicit_reference_ambiguous_direct_title_count
                            += signature.linked_direct_ingredient_title_count;
                        ingredient_linked_signature_explicit_reference_ambiguous_cross_title_count
                            += signature.linked_cross_ingredient_title_count;
                        ingredient_linked_signature_explicit_reference_ambiguous_direct_relationship_count
                            += signature
                                   .linked_direct_ingredient_relationship_count;
                        ingredient_linked_signature_explicit_reference_ambiguous_cross_relationship_count
                            += signature
                                   .linked_cross_ingredient_relationship_count;
                        ingredient_linked_signature_explicit_reference_ambiguous_direct_thumbnail_url_count
                            += signature
                                   .linked_direct_ingredient_thumbnail_url_count;
                        ingredient_linked_signature_explicit_reference_ambiguous_cross_thumbnail_url_count
                            += signature
                                   .linked_cross_ingredient_thumbnail_url_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_ambiguous_title_count
                            += signature.linked_ingredient_title_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_ambiguous_relationship_count
                            += signature.linked_ingredient_relationship_count;
                        manifest
                            .ingredient_linked_signature_explicit_reference_ambiguous_thumbnail_url_count
                            += signature.linked_ingredient_thumbnail_url_count;
                        ingredient_linked_signature_explicit_reference_ambiguous_title_count
                            += signature.linked_ingredient_title_count;
                        ingredient_linked_signature_explicit_reference_ambiguous_relationship_count
                            += signature.linked_ingredient_relationship_count;
                        ingredient_linked_signature_explicit_reference_ambiguous_thumbnail_url_count
                            += signature.linked_ingredient_thumbnail_url_count;
                        for (const ClaimProjection::NamedCount& named_count :
                             signature.linked_ingredient_relationship_counts) {
                            add_or_increment_named_count(
                                &manifest
                                     .ingredient_linked_signature_explicit_reference_ambiguous_relationship_counts,
                                named_count.key, named_count.count);
                            add_or_increment_named_count(
                                &ingredient_linked_signature_explicit_reference_ambiguous_relationship_counts,
                                named_count.key, named_count.count);
                        }
                    }
                }
            }
            manifest.cross_claim_link_count += signature.cross_claim_link_count;
            if (signature.has_explicit_reference) {
                manifest.explicit_reference_signature_count += 1U;
            }
            if (signature.explicit_reference_unresolved) {
                manifest.explicit_reference_unresolved_count += 1U;
            }
            if (signature.explicit_reference_ambiguous) {
                manifest.explicit_reference_ambiguous_count += 1U;
            }
        }

        for (ManifestProjection& manifest : manifests) {
            manifest.signature_orphan_count
                = (manifest.signature_count > manifest.signature_linked_count)
                      ? (manifest.signature_count
                         - manifest.signature_linked_count)
                      : 0U;
        }

        uint64_t manifest_count = static_cast<uint64_t>(manifests.size());
        if (manifest_count == 0U && have_label_summary) {
            manifest_count = label_manifest_present;
        }
        if (manifest_count == 0U && has_manifest) {
            manifest_count = 1U;
        }
        uint64_t active_manifest_count = 0U;
        std::string active_manifest_prefix;
        for (const ManifestProjection& manifest : manifests) {
            if (!manifest.is_active_manifest) {
                continue;
            }
            active_manifest_count += 1U;
            if (active_manifest_count == 1U) {
                active_manifest_prefix = manifest.prefix;
            } else {
                active_manifest_prefix.clear();
            }
        }

        if (has_manifest || has_claim || has_assertions || has_ingredients
            || has_signature) {
            if (!append_c2pa_marker(ctx, "cbor.semantic")) {
                return false;
            }
        }

        if (!emit_field_u64(ctx, "c2pa.semantic.cbor_key_count", cbor_key_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.semantic.manifest_present",
                           has_manifest ? 1U : 0U, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.semantic.claim_present",
                           has_claim ? 1U : 0U, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.semantic.assertion_present",
                           has_assertions ? 1U : 0U, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.semantic.ingredient_present",
                           has_ingredients ? 1U : 0U,
                           EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.semantic.signature_present",
                           has_signature ? 1U : 0U, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.manifest_count", manifest_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.semantic.active_manifest_present",
                           active_manifest_count != 0U ? 1U : 0U,
                           EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.active_manifest_count",
                            active_manifest_count, EntryFlags::Derived)) {
            return false;
        }
        if (!active_manifest_prefix.empty()) {
            if (!emit_field_text(ctx, "c2pa.semantic.active_manifest.prefix",
                                 active_manifest_prefix, EntryFlags::Derived)) {
                return false;
            }
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.assertion_key_hits",
                            assertion_key_hits, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.ingredient_key_hits",
                            ingredient_key_hits, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.signature_key_hits",
                            signature_key_hits, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.reference_key_hits",
                            reference_key_hits, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.cross_claim_link_count",
                            cross_claim_link_count, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx,
                            "c2pa.semantic.explicit_reference_signature_count",
                            explicit_reference_signature_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic.explicit_reference_unresolved_signature_count",
                explicit_reference_unresolved_signature_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic.explicit_reference_ambiguous_signature_count",
                explicit_reference_ambiguous_signature_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.explicit_reference_index_hits",
                            explicit_reference_index_hits,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.explicit_reference_label_hits",
                            explicit_reference_label_hits,
                            EntryFlags::Derived)) {
            return false;
        }
        const uint64_t claim_count = use_label_counts ? label_claim_count
                                                      : claims.size();
        if (!emit_field_u64(ctx, "c2pa.semantic.claim_count", claim_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.assertion_count",
                            global_assertions.size(), EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.ingredient_count",
                            global_ingredients.size(),
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.ingredient_claim_count",
                            ingredient_claim_count, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx,
                            "c2pa.semantic.ingredient_claim_with_signature_count",
                            ingredient_claim_with_signature_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic.ingredient_claim_referenced_by_signature_count",
                ingredient_claim_referenced_by_signature_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.ingredient_signature_count",
                            ingredient_signature_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.ingredient_linked_claim_count",
                            ingredient_linked_claim_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic.ingredient_linked_claim_direct_source_count",
                ingredient_linked_claim_direct_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic.ingredient_linked_claim_cross_source_count",
                ingredient_linked_claim_cross_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic.ingredient_linked_claim_mixed_source_count",
                ingredient_linked_claim_mixed_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_claim_explicit_reference_count",
                ingredient_linked_claim_explicit_reference_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_claim_explicit_reference_direct_source_count",
                ingredient_linked_claim_explicit_reference_direct_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_claim_explicit_reference_cross_source_count",
                ingredient_linked_claim_explicit_reference_cross_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_claim_explicit_reference_mixed_source_count",
                ingredient_linked_claim_explicit_reference_mixed_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_claim_explicit_reference_unresolved_count",
                ingredient_linked_claim_explicit_reference_unresolved_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_claim_explicit_reference_unresolved_direct_source_count",
                ingredient_linked_claim_explicit_reference_unresolved_direct_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_claim_explicit_reference_unresolved_cross_source_count",
                ingredient_linked_claim_explicit_reference_unresolved_cross_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_claim_explicit_reference_unresolved_mixed_source_count",
                ingredient_linked_claim_explicit_reference_unresolved_mixed_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_claim_explicit_reference_ambiguous_count",
                ingredient_linked_claim_explicit_reference_ambiguous_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_claim_explicit_reference_ambiguous_direct_source_count",
                ingredient_linked_claim_explicit_reference_ambiguous_direct_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_claim_explicit_reference_ambiguous_cross_source_count",
                ingredient_linked_claim_explicit_reference_ambiguous_cross_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_claim_explicit_reference_ambiguous_mixed_source_count",
                ingredient_linked_claim_explicit_reference_ambiguous_mixed_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx,
                            "c2pa.semantic.ingredient_linked_signature_count",
                            ingredient_linked_signature_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic.ingredient_linked_direct_claim_count",
                ingredient_linked_direct_claim_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic.ingredient_linked_cross_claim_count",
                ingredient_linked_cross_claim_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_direct_source_count",
                ingredient_linked_signature_direct_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_cross_source_count",
                ingredient_linked_signature_cross_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_mixed_source_count",
                ingredient_linked_signature_mixed_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_direct_title_count",
                ingredient_linked_signature_direct_title_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_cross_title_count",
                ingredient_linked_signature_cross_title_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_direct_relationship_count",
                ingredient_linked_signature_direct_relationship_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_cross_relationship_count",
                ingredient_linked_signature_cross_relationship_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_direct_thumbnail_url_count",
                ingredient_linked_signature_direct_thumbnail_url_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_cross_thumbnail_url_count",
                ingredient_linked_signature_cross_thumbnail_url_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic.ingredient_linked_signature_title_count",
                ingredient_linked_signature_title_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic.ingredient_linked_signature_relationship_count",
                ingredient_linked_signature_relationship_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_thumbnail_url_count",
                ingredient_linked_signature_thumbnail_url_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_direct_claim_count",
                ingredient_linked_signature_explicit_reference_direct_claim_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_cross_claim_count",
                ingredient_linked_signature_explicit_reference_cross_claim_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_direct_source_count",
                ingredient_linked_signature_explicit_reference_direct_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_cross_source_count",
                ingredient_linked_signature_explicit_reference_cross_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_mixed_source_count",
                ingredient_linked_signature_explicit_reference_mixed_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_direct_title_count",
                ingredient_linked_signature_explicit_reference_direct_title_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_cross_title_count",
                ingredient_linked_signature_explicit_reference_cross_title_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_direct_relationship_count",
                ingredient_linked_signature_explicit_reference_direct_relationship_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_cross_relationship_count",
                ingredient_linked_signature_explicit_reference_cross_relationship_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_direct_thumbnail_url_count",
                ingredient_linked_signature_explicit_reference_direct_thumbnail_url_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_cross_thumbnail_url_count",
                ingredient_linked_signature_explicit_reference_cross_thumbnail_url_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_title_count",
                ingredient_linked_signature_explicit_reference_title_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_relationship_count",
                ingredient_linked_signature_explicit_reference_relationship_count,
                EntryFlags::Derived)) {
            return false;
        }
        sort_named_counts(
            &ingredient_linked_signature_explicit_reference_relationship_counts);
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_relationship_kind_count",
                static_cast<uint64_t>(
                    ingredient_linked_signature_explicit_reference_relationship_counts
                        .size()),
                EntryFlags::Derived)) {
            return false;
        }
        for (const ClaimProjection::NamedCount& named_count :
             ingredient_linked_signature_explicit_reference_relationship_counts) {
            std::string relationship_field(
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_relationship.");
            relationship_field.append(named_count.key);
            relationship_field.append("_count");
            if (!emit_field_u64(ctx, relationship_field, named_count.count,
                                EntryFlags::Derived)) {
                return false;
            }
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_thumbnail_url_count",
                ingredient_linked_signature_explicit_reference_thumbnail_url_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_direct_claim_count",
                ingredient_linked_signature_explicit_reference_unresolved_direct_claim_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_cross_claim_count",
                ingredient_linked_signature_explicit_reference_unresolved_cross_claim_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_direct_source_count",
                ingredient_linked_signature_explicit_reference_unresolved_direct_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_cross_source_count",
                ingredient_linked_signature_explicit_reference_unresolved_cross_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_mixed_source_count",
                ingredient_linked_signature_explicit_reference_unresolved_mixed_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_direct_title_count",
                ingredient_linked_signature_explicit_reference_unresolved_direct_title_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_cross_title_count",
                ingredient_linked_signature_explicit_reference_unresolved_cross_title_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_direct_relationship_count",
                ingredient_linked_signature_explicit_reference_unresolved_direct_relationship_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_cross_relationship_count",
                ingredient_linked_signature_explicit_reference_unresolved_cross_relationship_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_direct_thumbnail_url_count",
                ingredient_linked_signature_explicit_reference_unresolved_direct_thumbnail_url_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_cross_thumbnail_url_count",
                ingredient_linked_signature_explicit_reference_unresolved_cross_thumbnail_url_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_title_count",
                ingredient_linked_signature_explicit_reference_unresolved_title_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_relationship_count",
                ingredient_linked_signature_explicit_reference_unresolved_relationship_count,
                EntryFlags::Derived)) {
            return false;
        }
        sort_named_counts(
            &ingredient_linked_signature_explicit_reference_unresolved_relationship_counts);
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_relationship_kind_count",
                static_cast<uint64_t>(
                    ingredient_linked_signature_explicit_reference_unresolved_relationship_counts
                        .size()),
                EntryFlags::Derived)) {
            return false;
        }
        for (const ClaimProjection::NamedCount& named_count :
             ingredient_linked_signature_explicit_reference_unresolved_relationship_counts) {
            std::string relationship_field(
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_relationship.");
            relationship_field.append(named_count.key);
            relationship_field.append("_count");
            if (!emit_field_u64(ctx, relationship_field, named_count.count,
                                EntryFlags::Derived)) {
                return false;
            }
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_unresolved_thumbnail_url_count",
                ingredient_linked_signature_explicit_reference_unresolved_thumbnail_url_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_direct_claim_count",
                ingredient_linked_signature_explicit_reference_ambiguous_direct_claim_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_cross_claim_count",
                ingredient_linked_signature_explicit_reference_ambiguous_cross_claim_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_direct_source_count",
                ingredient_linked_signature_explicit_reference_ambiguous_direct_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_cross_source_count",
                ingredient_linked_signature_explicit_reference_ambiguous_cross_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_mixed_source_count",
                ingredient_linked_signature_explicit_reference_ambiguous_mixed_source_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_direct_title_count",
                ingredient_linked_signature_explicit_reference_ambiguous_direct_title_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_cross_title_count",
                ingredient_linked_signature_explicit_reference_ambiguous_cross_title_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_direct_relationship_count",
                ingredient_linked_signature_explicit_reference_ambiguous_direct_relationship_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_cross_relationship_count",
                ingredient_linked_signature_explicit_reference_ambiguous_cross_relationship_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_direct_thumbnail_url_count",
                ingredient_linked_signature_explicit_reference_ambiguous_direct_thumbnail_url_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_cross_thumbnail_url_count",
                ingredient_linked_signature_explicit_reference_ambiguous_cross_thumbnail_url_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_title_count",
                ingredient_linked_signature_explicit_reference_ambiguous_title_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_relationship_count",
                ingredient_linked_signature_explicit_reference_ambiguous_relationship_count,
                EntryFlags::Derived)) {
            return false;
        }
        sort_named_counts(
            &ingredient_linked_signature_explicit_reference_ambiguous_relationship_counts);
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_relationship_kind_count",
                static_cast<uint64_t>(
                    ingredient_linked_signature_explicit_reference_ambiguous_relationship_counts
                        .size()),
                EntryFlags::Derived)) {
            return false;
        }
        for (const ClaimProjection::NamedCount& named_count :
             ingredient_linked_signature_explicit_reference_ambiguous_relationship_counts) {
            std::string relationship_field(
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_relationship.");
            relationship_field.append(named_count.key);
            relationship_field.append("_count");
            if (!emit_field_u64(ctx, relationship_field, named_count.count,
                                EntryFlags::Derived)) {
                return false;
            }
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_explicit_reference_ambiguous_thumbnail_url_count",
                ingredient_linked_signature_explicit_reference_ambiguous_thumbnail_url_count,
                EntryFlags::Derived)) {
            return false;
        }
        sort_named_counts(&ingredient_linked_signature_relationship_counts);
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_linked_signature_relationship_kind_count",
                static_cast<uint64_t>(
                    ingredient_linked_signature_relationship_counts.size()),
                EntryFlags::Derived)) {
            return false;
        }
        for (const ClaimProjection::NamedCount& named_count :
             ingredient_linked_signature_relationship_counts) {
            std::string relationship_field(
                "c2pa.semantic.ingredient_linked_signature_relationship.");
            relationship_field.append(named_count.key);
            relationship_field.append("_count");
            if (!emit_field_u64(ctx, relationship_field, named_count.count,
                                EntryFlags::Derived)) {
                return false;
            }
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic.ingredient_explicit_reference_signature_count",
                ingredient_explicit_reference_signature_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_explicit_reference_unresolved_signature_count",
                ingredient_explicit_reference_unresolved_signature_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.semantic."
                "ingredient_explicit_reference_ambiguous_signature_count",
                ingredient_explicit_reference_ambiguous_signature_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx,
                            "c2pa.semantic.ingredient_relationship_count",
                            ingredient_relationship_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx,
                            "c2pa.semantic.ingredient_thumbnail_url_count",
                            ingredient_thumbnail_url_count,
                            EntryFlags::Derived)) {
            return false;
        }
        sort_named_counts(&ingredient_relationship_counts);
        if (!emit_field_u64(
                ctx, "c2pa.semantic.ingredient_relationship_kind_count",
                static_cast<uint64_t>(ingredient_relationship_counts.size()),
                EntryFlags::Derived)) {
            return false;
        }
        for (const ClaimProjection::NamedCount& named_count :
             ingredient_relationship_counts) {
            std::string relationship_field("c2pa.semantic.ingredient_relationship.");
            relationship_field.append(named_count.key);
            relationship_field.append("_count");
            if (!emit_field_u64(ctx, relationship_field, named_count.count,
                                EntryFlags::Derived)) {
                return false;
            }
        }
        uint64_t signature_count = use_label_counts ? label_signature_count
                                                    : signatures.size();
        if (signature_count == 0U && has_signature) {
            signature_count = 1U;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.signature_count",
                            signature_count, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.signature_linked_count",
                            signature_linked_count, EntryFlags::Derived)) {
            return false;
        }
        const uint64_t signature_orphan_count
            = use_label_counts
                  ? label_signature_orphan
                  : ((signature_count > signature_linked_count)
                         ? (signature_count - signature_linked_count)
                         : 0U);
        if (!emit_field_u64(ctx, "c2pa.semantic.signature_orphan_count",
                            signature_orphan_count, EntryFlags::Derived)) {
            return false;
        }
        uint64_t ingredient_manifest_count = 0U;
        for (const ManifestProjection& manifest : manifests) {
            if (manifest.ingredient_claim_count != 0U) {
                ingredient_manifest_count += 1U;
            }
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.ingredient_manifest_count",
                            ingredient_manifest_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (has_claim_generator) {
            if (!emit_field_text(ctx, "c2pa.semantic.claim_generator",
                                 claim_generator, EntryFlags::Derived)) {
                return false;
            }
        }

        std::sort(manifests.begin(), manifests.end(),
                  ManifestProjectionLess {});
        for (size_t index = 0U; index < manifests.size(); ++index) {
            const ManifestProjection& manifest = manifests[index];
            std::string field;
            field.reserve(64U);
            field.append("c2pa.semantic.manifest.");
            field.append(
                std::to_string(static_cast<unsigned long long>(index)));
            const std::string field_prefix(field);

            field.assign(field_prefix);
            field.append(".prefix");
            if (!emit_field_text(ctx, field, manifest.prefix,
                                 EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".is_active");
            if (!emit_field_u8(ctx, field,
                               manifest.is_active_manifest ? 1U : 0U,
                               EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".claim_count");
            if (!emit_field_u64(ctx, field, manifest.claim_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_claim_count");
            if (!emit_field_u64(ctx, field, manifest.ingredient_claim_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_claim_with_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_claim_with_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_claim_referenced_by_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_claim_referenced_by_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_signature_count");
            if (!emit_field_u64(ctx, field,
                                manifest.ingredient_signature_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_claim_count");
            if (!emit_field_u64(ctx, field,
                                manifest.ingredient_linked_claim_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_claim_direct_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_claim_direct_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_claim_cross_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_claim_cross_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_claim_mixed_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_claim_mixed_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_claim_explicit_reference_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_claim_explicit_reference_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_claim_explicit_reference_direct_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_claim_explicit_reference_direct_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_claim_explicit_reference_cross_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_claim_explicit_reference_cross_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_claim_explicit_reference_mixed_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_claim_explicit_reference_mixed_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_claim_explicit_reference_unresolved_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_claim_explicit_reference_unresolved_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_claim_explicit_reference_unresolved_direct_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_claim_explicit_reference_unresolved_direct_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_claim_explicit_reference_unresolved_cross_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_claim_explicit_reference_unresolved_cross_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_claim_explicit_reference_unresolved_mixed_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_claim_explicit_reference_unresolved_mixed_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_claim_explicit_reference_ambiguous_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_claim_explicit_reference_ambiguous_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_claim_explicit_reference_ambiguous_direct_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_claim_explicit_reference_ambiguous_direct_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_claim_explicit_reference_ambiguous_cross_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_claim_explicit_reference_ambiguous_cross_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_claim_explicit_reference_ambiguous_mixed_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_claim_explicit_reference_ambiguous_mixed_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_count");
            if (!emit_field_u64(ctx, field,
                                manifest.ingredient_linked_signature_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_direct_claim_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_direct_claim_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_cross_claim_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_cross_claim_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_direct_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_signature_direct_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_cross_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_signature_cross_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_mixed_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_signature_mixed_source_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_direct_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_signature_direct_title_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_cross_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_signature_cross_title_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_direct_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_signature_direct_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_cross_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_signature_cross_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_direct_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_signature_direct_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_cross_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_signature_cross_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_signature_title_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_signature_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_linked_signature_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_direct_claim_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_direct_claim_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_cross_claim_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_cross_claim_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_direct_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_direct_source_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_cross_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_cross_source_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_mixed_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_mixed_source_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_direct_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_direct_title_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_cross_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_cross_title_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_direct_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_direct_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_cross_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_cross_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_direct_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_direct_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_cross_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_cross_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_title_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }
            std::vector<ClaimProjection::NamedCount>
                manifest_explicit_reference_relationship_counts
                = manifest
                      .ingredient_linked_signature_explicit_reference_relationship_counts;
            sort_named_counts(&manifest_explicit_reference_relationship_counts);
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_relationship_kind_count");
            if (!emit_field_u64(
                    ctx, field,
                    static_cast<uint64_t>(
                        manifest_explicit_reference_relationship_counts.size()),
                    EntryFlags::Derived)) {
                return false;
            }
            for (const ClaimProjection::NamedCount& named_count :
                 manifest_explicit_reference_relationship_counts) {
                std::string relationship_field(field_prefix);
                relationship_field.append(
                    ".ingredient_linked_signature_explicit_reference_relationship.");
                relationship_field.append(named_count.key);
                relationship_field.append("_count");
                if (!emit_field_u64(
                        ctx, relationship_field, named_count.count,
                        EntryFlags::Derived)) {
                    return false;
                }
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_direct_claim_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_direct_claim_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_cross_claim_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_cross_claim_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_direct_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_direct_source_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_cross_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_cross_source_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_mixed_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_mixed_source_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_direct_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_direct_title_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_cross_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_cross_title_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_direct_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_direct_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_cross_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_cross_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_direct_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_direct_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_cross_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_cross_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_title_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }
            std::vector<ClaimProjection::NamedCount>
                manifest_unresolved_relationship_counts
                = manifest
                      .ingredient_linked_signature_explicit_reference_unresolved_relationship_counts;
            sort_named_counts(&manifest_unresolved_relationship_counts);
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_relationship_kind_count");
            if (!emit_field_u64(
                    ctx, field,
                    static_cast<uint64_t>(
                        manifest_unresolved_relationship_counts.size()),
                    EntryFlags::Derived)) {
                return false;
            }
            for (const ClaimProjection::NamedCount& named_count :
                 manifest_unresolved_relationship_counts) {
                std::string relationship_field(field_prefix);
                relationship_field.append(
                    ".ingredient_linked_signature_explicit_reference_unresolved_relationship.");
                relationship_field.append(named_count.key);
                relationship_field.append("_count");
                if (!emit_field_u64(
                        ctx, relationship_field, named_count.count,
                        EntryFlags::Derived)) {
                    return false;
                }
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_unresolved_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_unresolved_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_direct_claim_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_direct_claim_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_cross_claim_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_cross_claim_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_direct_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_direct_source_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_cross_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_cross_source_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_mixed_source_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_mixed_source_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_direct_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_direct_title_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_cross_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_cross_title_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_direct_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_direct_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_cross_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_cross_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_direct_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_direct_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_cross_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_cross_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_title_count,
                    EntryFlags::Derived)) {
                return false;
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }
            std::vector<ClaimProjection::NamedCount>
                manifest_ambiguous_relationship_counts
                = manifest
                      .ingredient_linked_signature_explicit_reference_ambiguous_relationship_counts;
            sort_named_counts(&manifest_ambiguous_relationship_counts);
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_relationship_kind_count");
            if (!emit_field_u64(
                    ctx, field,
                    static_cast<uint64_t>(
                        manifest_ambiguous_relationship_counts.size()),
                    EntryFlags::Derived)) {
                return false;
            }
            for (const ClaimProjection::NamedCount& named_count :
                 manifest_ambiguous_relationship_counts) {
                std::string relationship_field(field_prefix);
                relationship_field.append(
                    ".ingredient_linked_signature_explicit_reference_ambiguous_relationship.");
                relationship_field.append(named_count.key);
                relationship_field.append("_count");
                if (!emit_field_u64(
                        ctx, relationship_field, named_count.count,
                        EntryFlags::Derived)) {
                    return false;
                }
            }
            field.assign(field_prefix);
            field.append(
                ".ingredient_linked_signature_explicit_reference_ambiguous_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_linked_signature_explicit_reference_ambiguous_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }

            std::vector<ClaimProjection::NamedCount>
                manifest_linked_signature_relationship_counts
                = manifest.ingredient_linked_signature_relationship_counts;
            sort_named_counts(&manifest_linked_signature_relationship_counts);
            field.assign(field_prefix);
            field.append(".ingredient_linked_signature_relationship_kind_count");
            if (!emit_field_u64(
                    ctx, field,
                    static_cast<uint64_t>(
                        manifest_linked_signature_relationship_counts.size()),
                    EntryFlags::Derived)) {
                return false;
            }
            for (const ClaimProjection::NamedCount& named_count :
                 manifest_linked_signature_relationship_counts) {
                std::string relationship_field(field_prefix);
                relationship_field
                    .append(".ingredient_linked_signature_relationship.");
                relationship_field.append(named_count.key);
                relationship_field.append("_count");
                if (!emit_field_u64(
                        ctx, relationship_field, named_count.count,
                        EntryFlags::Derived)) {
                    return false;
                }
            }

            field.assign(field_prefix);
            field.append(".ingredient_explicit_reference_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest.ingredient_explicit_reference_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_explicit_reference_unresolved_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_explicit_reference_unresolved_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".ingredient_explicit_reference_ambiguous_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    manifest
                        .ingredient_explicit_reference_ambiguous_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".assertion_count");
            if (!emit_field_u64(ctx, field, manifest.assertion_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_count");
            if (!emit_field_u64(ctx, field, manifest.ingredient_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_relationship_count");
            if (!emit_field_u64(ctx, field,
                                manifest.ingredient_relationship_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_thumbnail_url_count");
            if (!emit_field_u64(ctx, field,
                                manifest.ingredient_thumbnail_url_count,
                                EntryFlags::Derived)) {
                return false;
            }

            std::vector<ClaimProjection::NamedCount> manifest_relationship_counts
                = manifest.ingredient_relationship_counts;
            sort_named_counts(&manifest_relationship_counts);
            field.assign(field_prefix);
            field.append(".ingredient_relationship_kind_count");
            if (!emit_field_u64(
                    ctx, field,
                    static_cast<uint64_t>(manifest_relationship_counts.size()),
                    EntryFlags::Derived)) {
                return false;
            }
            for (const ClaimProjection::NamedCount& named_count :
                 manifest_relationship_counts) {
                std::string relationship_field(field_prefix);
                relationship_field.append(".ingredient_relationship.");
                relationship_field.append(named_count.key);
                relationship_field.append("_count");
                if (!emit_field_u64(ctx, relationship_field, named_count.count,
                                    EntryFlags::Derived)) {
                    return false;
                }
            }

            field.assign(field_prefix);
            field.append(".signature_count");
            if (!emit_field_u64(ctx, field, manifest.signature_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".signature_linked_count");
            if (!emit_field_u64(ctx, field, manifest.signature_linked_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".signature_orphan_count");
            if (!emit_field_u64(ctx, field, manifest.signature_orphan_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".cross_claim_link_count");
            if (!emit_field_u64(ctx, field, manifest.cross_claim_link_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".explicit_reference_signature_count");
            if (!emit_field_u64(ctx, field,
                                manifest.explicit_reference_signature_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".explicit_reference_unresolved_count");
            if (!emit_field_u64(ctx, field,
                                manifest.explicit_reference_unresolved_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".explicit_reference_ambiguous_count");
            if (!emit_field_u64(ctx, field,
                                manifest.explicit_reference_ambiguous_count,
                                EntryFlags::Derived)) {
                return false;
            }
        }

        std::sort(claims.begin(), claims.end(), ClaimProjectionLess {});
        for (size_t index = 0U; index < claims.size(); ++index) {
            const ClaimProjection& claim = claims[index];
            std::string field;
            field.reserve(64U);
            field.append("c2pa.semantic.claim.");
            field.append(
                std::to_string(static_cast<unsigned long long>(index)));
            const std::string field_prefix(field);

            field.assign(field_prefix);
            field.append(".prefix");
            if (!emit_field_text(ctx, field, claim.prefix,
                                 EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".key_hits");
            if (!emit_field_u64(ctx, field, claim.key_hits,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".assertion_count");
            if (!emit_field_u64(ctx, field, claim.assertions.size(),
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_count");
            if (!emit_field_u64(ctx, field, claim.ingredients.size(),
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_relationship_count");
            if (!emit_field_u64(ctx, field,
                                claim.ingredient_relationship_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".ingredient_thumbnail_url_count");
            if (!emit_field_u64(ctx, field,
                                claim.ingredient_thumbnail_url_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_ingredient_signature_count");
            if (!emit_field_u64(ctx, field,
                                claim.linked_ingredient_signature_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_direct_ingredient_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim.linked_direct_ingredient_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_cross_ingredient_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim.linked_cross_ingredient_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_ingredient_title_count");
            if (!emit_field_u64(ctx, field,
                                claim.linked_ingredient_title_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_ingredient_relationship_count");
            if (!emit_field_u64(ctx, field,
                                claim.linked_ingredient_relationship_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_ingredient_thumbnail_url_count");
            if (!emit_field_u64(ctx, field,
                                claim.linked_ingredient_thumbnail_url_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim.linked_ingredient_explicit_reference_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_direct_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_direct_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_cross_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_cross_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_ingredient_explicit_reference_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim.linked_ingredient_explicit_reference_title_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_unresolved_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_unresolved_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_unresolved_direct_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_unresolved_direct_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_unresolved_cross_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_unresolved_cross_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_unresolved_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_unresolved_title_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_unresolved_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_unresolved_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_unresolved_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_unresolved_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_ambiguous_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_ambiguous_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_ambiguous_direct_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_ambiguous_direct_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_ambiguous_cross_signature_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_ambiguous_cross_signature_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_ambiguous_title_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_ambiguous_title_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_ambiguous_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_ambiguous_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_ambiguous_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    claim
                        .linked_ingredient_explicit_reference_ambiguous_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }

            std::vector<ClaimProjection::NamedCount>
                claim_linked_relationship_counts
                = claim.linked_ingredient_relationship_counts;
            sort_named_counts(&claim_linked_relationship_counts);
            field.assign(field_prefix);
            field.append(".linked_ingredient_relationship_kind_count");
            if (!emit_field_u64(
                    ctx, field,
                    static_cast<uint64_t>(
                        claim_linked_relationship_counts.size()),
                    EntryFlags::Derived)) {
                return false;
            }
            for (const ClaimProjection::NamedCount& named_count :
                 claim_linked_relationship_counts) {
                std::string relationship_field(field_prefix);
                relationship_field.append(".linked_ingredient_relationship.");
                relationship_field.append(named_count.key);
                relationship_field.append("_count");
                if (!emit_field_u64(ctx, relationship_field, named_count.count,
                                    EntryFlags::Derived)) {
                    return false;
                }
            }

            std::vector<ClaimProjection::NamedCount>
                claim_linked_explicit_relationship_counts
                = claim
                       .linked_ingredient_explicit_reference_relationship_counts;
            sort_named_counts(&claim_linked_explicit_relationship_counts);
            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_relationship_kind_count");
            if (!emit_field_u64(
                    ctx, field,
                    static_cast<uint64_t>(
                        claim_linked_explicit_relationship_counts.size()),
                    EntryFlags::Derived)) {
                return false;
            }
            for (const ClaimProjection::NamedCount& named_count :
                 claim_linked_explicit_relationship_counts) {
                std::string relationship_field(field_prefix);
                relationship_field.append(
                    ".linked_ingredient_explicit_reference_relationship.");
                relationship_field.append(named_count.key);
                relationship_field.append("_count");
                if (!emit_field_u64(ctx, relationship_field, named_count.count,
                                    EntryFlags::Derived)) {
                    return false;
                }
            }

            std::vector<ClaimProjection::NamedCount>
                claim_linked_unresolved_relationship_counts
                = claim
                       .linked_ingredient_explicit_reference_unresolved_relationship_counts;
            sort_named_counts(&claim_linked_unresolved_relationship_counts);
            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_unresolved_relationship_kind_count");
            if (!emit_field_u64(
                    ctx, field,
                    static_cast<uint64_t>(
                        claim_linked_unresolved_relationship_counts.size()),
                    EntryFlags::Derived)) {
                return false;
            }
            for (const ClaimProjection::NamedCount& named_count :
                 claim_linked_unresolved_relationship_counts) {
                std::string relationship_field(field_prefix);
                relationship_field.append(
                    ".linked_ingredient_explicit_reference_unresolved_relationship.");
                relationship_field.append(named_count.key);
                relationship_field.append("_count");
                if (!emit_field_u64(ctx, relationship_field, named_count.count,
                                    EntryFlags::Derived)) {
                    return false;
                }
            }

            std::vector<ClaimProjection::NamedCount>
                claim_linked_ambiguous_relationship_counts
                = claim
                       .linked_ingredient_explicit_reference_ambiguous_relationship_counts;
            sort_named_counts(&claim_linked_ambiguous_relationship_counts);
            field.assign(field_prefix);
            field.append(
                ".linked_ingredient_explicit_reference_ambiguous_relationship_kind_count");
            if (!emit_field_u64(
                    ctx, field,
                    static_cast<uint64_t>(
                        claim_linked_ambiguous_relationship_counts.size()),
                    EntryFlags::Derived)) {
                return false;
            }
            for (const ClaimProjection::NamedCount& named_count :
                 claim_linked_ambiguous_relationship_counts) {
                std::string relationship_field(field_prefix);
                relationship_field.append(
                    ".linked_ingredient_explicit_reference_ambiguous_relationship.");
                relationship_field.append(named_count.key);
                relationship_field.append("_count");
                if (!emit_field_u64(ctx, relationship_field, named_count.count,
                                    EntryFlags::Derived)) {
                    return false;
                }
            }

            std::vector<ClaimProjection::NamedCount> claim_relationship_counts
                = claim.ingredient_relationship_counts;
            sort_named_counts(&claim_relationship_counts);
            field.assign(field_prefix);
            field.append(".ingredient_relationship_kind_count");
            if (!emit_field_u64(
                    ctx, field,
                    static_cast<uint64_t>(claim_relationship_counts.size()),
                    EntryFlags::Derived)) {
                return false;
            }
            for (const ClaimProjection::NamedCount& named_count :
                 claim_relationship_counts) {
                std::string relationship_field(field_prefix);
                relationship_field.append(".ingredient_relationship.");
                relationship_field.append(named_count.key);
                relationship_field.append("_count");
                if (!emit_field_u64(ctx, relationship_field, named_count.count,
                                    EntryFlags::Derived)) {
                    return false;
                }
            }

            field.assign(field_prefix);
            field.append(".signature_count");
            if (!emit_field_u64(ctx, field, claim.signatures.size(),
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".signature_key_hits");
            if (!emit_field_u64(ctx, field, claim.signature_key_hits,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".referenced_by_signature_count");
            if (!emit_field_u64(ctx, field, claim.referenced_by_signature_count,
                                EntryFlags::Derived)) {
                return false;
            }

            std::vector<ClaimProjection::AssertionProjection> assertions
                = claim.assertions;
            std::sort(assertions.begin(), assertions.end(),
                      AssertionProjectionLess {});
            for (size_t assertion_index = 0U;
                 assertion_index < assertions.size(); ++assertion_index) {
                const ClaimProjection::AssertionProjection& assertion
                    = assertions[assertion_index];
                std::string assertion_field(field_prefix);
                assertion_field.append(".assertion.");
                assertion_field.append(std::to_string(
                    static_cast<unsigned long long>(assertion_index)));

                field.assign(assertion_field);
                field.append(".prefix");
                if (!emit_field_text(ctx, field, assertion.prefix,
                                     EntryFlags::Derived)) {
                    return false;
                }

                field.assign(assertion_field);
                field.append(".key_hits");
                if (!emit_field_u64(ctx, field, assertion.key_hits,
                                    EntryFlags::Derived)) {
                    return false;
                }
            }

            std::vector<ClaimProjection::IngredientProjection> ingredients
                = claim.ingredients;
            std::sort(ingredients.begin(), ingredients.end(),
                      IngredientProjectionLess {});
            for (size_t ingredient_index = 0U;
                 ingredient_index < ingredients.size(); ++ingredient_index) {
                const ClaimProjection::IngredientProjection& ingredient
                    = ingredients[ingredient_index];
                std::string ingredient_field(field_prefix);
                ingredient_field.append(".ingredient.");
                ingredient_field.append(std::to_string(
                    static_cast<unsigned long long>(ingredient_index)));

                field.assign(ingredient_field);
                field.append(".prefix");
                if (!emit_field_text(ctx, field, ingredient.prefix,
                                     EntryFlags::Derived)) {
                    return false;
                }

                field.assign(ingredient_field);
                field.append(".key_hits");
                if (!emit_field_u64(ctx, field, ingredient.key_hits,
                                    EntryFlags::Derived)) {
                    return false;
                }

                if (ingredient.has_title) {
                    field.assign(ingredient_field);
                    field.append(".title");
                    if (!emit_field_text(ctx, field, ingredient.title,
                                         EntryFlags::Derived)) {
                        return false;
                    }
                }

                if (ingredient.has_relationship) {
                    field.assign(ingredient_field);
                    field.append(".relationship");
                    if (!emit_field_text(ctx, field,
                                         ingredient.relationship,
                                         EntryFlags::Derived)) {
                        return false;
                    }
                }

                if (ingredient.has_thumbnail_url) {
                    field.assign(ingredient_field);
                    field.append(".thumbnail_url");
                    if (!emit_field_text(ctx, field,
                                         ingredient.thumbnail_url,
                                         EntryFlags::Derived)) {
                        return false;
                    }
                }
            }

            std::vector<ClaimProjection::SignatureProjection> claim_signatures
                = claim.signatures;
            std::sort(claim_signatures.begin(), claim_signatures.end(),
                      ClaimSignatureProjectionLess {});
            for (size_t signature_index = 0U;
                 signature_index < claim_signatures.size(); ++signature_index) {
                const ClaimProjection::SignatureProjection& signature
                    = claim_signatures[signature_index];
                std::string signature_field(field_prefix);
                signature_field.append(".signature.");
                signature_field.append(std::to_string(
                    static_cast<unsigned long long>(signature_index)));

                field.assign(signature_field);
                field.append(".prefix");
                if (!emit_field_text(ctx, field, signature.prefix,
                                     EntryFlags::Derived)) {
                    return false;
                }

                field.assign(signature_field);
                field.append(".key_hits");
                if (!emit_field_u64(ctx, field, signature.key_hits,
                                    EntryFlags::Derived)) {
                    return false;
                }

                if (signature.has_algorithm) {
                    field.assign(signature_field);
                    field.append(".algorithm");
                    if (!emit_field_text(ctx, field, signature.algorithm,
                                         EntryFlags::Derived)) {
                        return false;
                    }
                }
            }

            if (claim.has_claim_generator) {
                field.assign(field_prefix);
                field.append(".claim_generator");
                if (!emit_field_text(ctx, field, claim.claim_generator,
                                     EntryFlags::Derived)) {
                    return false;
                }
            }
        }

        std::sort(signatures.begin(), signatures.end(),
                  SignatureProjectionLess {});
        for (size_t index = 0U; index < signatures.size(); ++index) {
            const SignatureProjection& signature = signatures[index];
            std::string field;
            field.reserve(64U);
            field.append("c2pa.semantic.signature.");
            field.append(
                std::to_string(static_cast<unsigned long long>(index)));
            const std::string field_prefix(field);

            field.assign(field_prefix);
            field.append(".prefix");
            if (!emit_field_text(ctx, field, signature.prefix,
                                 EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".key_hits");
            if (!emit_field_u64(ctx, field, signature.key_hits,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".reference_key_hits");
            if (!emit_field_u64(ctx, field, signature.reference_key_hits,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".explicit_reference_index_hits");
            if (!emit_field_u64(ctx, field,
                                signature.explicit_reference_index_hits,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".explicit_reference_label_hits");
            if (!emit_field_u64(ctx, field,
                                signature.explicit_reference_label_hits,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_claim_count");
            if (!emit_field_u64(ctx, field, signature.linked_claim_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_ingredient_claim_count");
            if (!emit_field_u64(ctx, field,
                                signature.linked_ingredient_claim_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_direct_ingredient_claim_count");
            if (!emit_field_u64(
                    ctx, field,
                    signature.linked_direct_ingredient_claim_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_cross_ingredient_claim_count");
            if (!emit_field_u64(
                    ctx, field,
                    signature.linked_cross_ingredient_claim_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_ingredient_title_count");
            if (!emit_field_u64(ctx, field,
                                signature.linked_ingredient_title_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".linked_ingredient_relationship_count");
            if (!emit_field_u64(
                    ctx, field,
                    signature.linked_ingredient_relationship_count,
                    EntryFlags::Derived)) {
                return false;
            }

            std::vector<ClaimProjection::NamedCount>
                signature_relationship_counts
                = signature.linked_ingredient_relationship_counts;
            sort_named_counts(&signature_relationship_counts);

            field.assign(field_prefix);
            field.append(".linked_ingredient_relationship_kind_count");
            if (!emit_field_u64(
                    ctx, field,
                    static_cast<uint64_t>(
                        signature_relationship_counts.size()),
                    EntryFlags::Derived)) {
                return false;
            }
            for (const ClaimProjection::NamedCount& named_count :
                 signature_relationship_counts) {
                std::string relationship_field(field_prefix);
                relationship_field.append(".linked_ingredient_relationship.");
                relationship_field.append(named_count.key);
                relationship_field.append("_count");
                if (!emit_field_u64(
                        ctx, relationship_field, named_count.count,
                        EntryFlags::Derived)) {
                    return false;
                }
            }

            field.assign(field_prefix);
            field.append(".linked_ingredient_thumbnail_url_count");
            if (!emit_field_u64(
                    ctx, field,
                    signature.linked_ingredient_thumbnail_url_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".direct_claim_has_ingredients");
            if (!emit_field_u8(ctx, field,
                               signature.direct_claim_has_ingredients ? 1U
                                                                      : 0U,
                               EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".cross_claim_link_count");
            if (!emit_field_u64(ctx, field, signature.cross_claim_link_count,
                                EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".explicit_reference_present");
            if (!emit_field_u8(ctx, field,
                               signature.has_explicit_reference ? 1U : 0U,
                               EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".explicit_reference_resolved_claim_count");
            if (!emit_field_u64(
                    ctx, field,
                    signature.explicit_reference_resolved_claim_count,
                    EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".explicit_reference_unresolved");
            if (!emit_field_u8(ctx, field,
                               signature.explicit_reference_unresolved ? 1U : 0U,
                               EntryFlags::Derived)) {
                return false;
            }

            field.assign(field_prefix);
            field.append(".explicit_reference_ambiguous");
            if (!emit_field_u8(ctx, field,
                               signature.explicit_reference_ambiguous ? 1U : 0U,
                               EntryFlags::Derived)) {
                return false;
            }

            std::vector<std::string> linked_claim_prefixes
                = signature.linked_claim_prefixes;
            std::sort(linked_claim_prefixes.begin(),
                      linked_claim_prefixes.end());
            for (size_t claim_index = 0U;
                 claim_index < linked_claim_prefixes.size(); ++claim_index) {
                std::string claim_field(field_prefix);
                claim_field.append(".linked_claim.");
                claim_field.append(std::to_string(
                    static_cast<unsigned long long>(claim_index)));
                claim_field.append(".prefix");
                if (!emit_field_text(ctx, claim_field,
                                     linked_claim_prefixes[claim_index],
                                     EntryFlags::Derived)) {
                    return false;
                }
            }

            if (signature.has_algorithm) {
                field.assign(field_prefix);
                field.append(".algorithm");
                if (!emit_field_text(ctx, field, signature.algorithm,
                                     EntryFlags::Derived)) {
                    return false;
                }
            }
        }

        return true;
    }

    static bool collect_c2pa_verify_candidates(
        DecodeContext* ctx, bool* out_has_signatures,
        std::vector<C2paVerifySignatureCandidate>* out_candidates) noexcept
    {
        if (!ctx || !ctx->store || !out_has_signatures || !out_candidates) {
            return false;
        }
        *out_has_signatures = false;
        out_candidates->clear();

        for (const DecodeContext::ParsedBox& box : ctx->boxes) {
            if (box.type == fourcc('j', 'u', 'm', 'b') && box.has_jumb_label
                && ascii_icase_contains_text(box.jumb_label, "signature")) {
                *out_has_signatures = true;
                break;
            }
        }

        const std::span<const Entry> entries = ctx->store->entries();
        for (const Entry& e : entries) {
            if (e.origin.block != ctx->block
                || e.key.kind != MetaKeyKind::JumbfCborKey) {
                continue;
            }
            const std::string_view key
                = arena_string_view(ctx->store->arena(),
                                    e.key.data.jumbf_cbor_key.key);
            if (cbor_key_has_segment(key, "signature")
                || cbor_key_has_segment(key, "signatures")) {
                *out_has_signatures = true;
            }

            std::string signature_prefix;
            if (!find_indexed_segment_prefix(key, ".signatures[",
                                             &signature_prefix)) {
                continue;
            }
            const size_t candidate_index
                = add_or_get_verify_signature_candidate(out_candidates,
                                                        signature_prefix);
            if (candidate_index == static_cast<size_t>(-1)) {
                continue;
            }
            C2paVerifySignatureCandidate& candidate
                = (*out_candidates)[candidate_index];

            if ((key_matches_field(key, signature_prefix, "alg")
                 || key_matches_field(key, signature_prefix, "algorithm"))
                && e.value.kind == MetaValueKind::Text) {
                const std::span<const std::byte> text
                    = ctx->store->arena().span(e.value.data.span);
                if (bytes_all_ascii_printable(text)) {
                    candidate.algorithm.assign(reinterpret_cast<const char*>(
                                                   text.data()),
                                               text.size());
                    candidate.algorithm     = ascii_lower(candidate.algorithm);
                    candidate.has_algorithm = true;
                }
                continue;
            }

            const bool is_bytes_value = (e.value.kind == MetaValueKind::Bytes)
                                        || (e.value.kind == MetaValueKind::Array
                                            && e.value.elem_type
                                                   == MetaElementType::U8);

            if (is_bytes_value) {
                const std::span<const std::byte> bytes
                    = ctx->store->arena().span(e.value.data.span);

                if (key == signature_prefix) {
                    // Some C2PA payloads embed COSE_Sign1 as a CBOR byte string.
                    // Best-effort decode: ignore failures and fall back to other
                    // explicit fields when present.
                    if (!cose_decode_sign1_bytes(bytes, ctx->options.limits,
                                                 &candidate)
                        && cose_bytes_look_like_sign1(bytes,
                                                      ctx->options.limits)) {
                        candidate.has_invalid_cose_signature_shape = true;
                    }
                }

                if (key_is_indexed_item(key, signature_prefix, 0U)) {
                    candidate.cose_protected_bytes.assign(bytes.begin(),
                                                          bytes.end());
                    candidate.has_cose_protected_bytes = true;
                    continue;
                }
                if (key_is_indexed_item(key, signature_prefix, 2U)) {
                    candidate.cose_payload_bytes.assign(bytes.begin(),
                                                        bytes.end());
                    candidate.has_cose_payload_bytes = true;
                    candidate.cose_payload_is_null   = false;
                    continue;
                }
                if (key_is_indexed_item(key, signature_prefix, 3U)) {
                    candidate.cose_signature_bytes.assign(bytes.begin(),
                                                          bytes.end());
                    candidate.has_cose_signature_bytes = true;
                    continue;
                }

                if (key_matches_field(key, signature_prefix, "signature")
                    || key_matches_field(key, signature_prefix, "sig")) {
                    candidate.signature_bytes.assign(bytes.begin(),
                                                     bytes.end());
                    candidate.has_signature_bytes = true;
                    continue;
                }

                if (key_matches_field(key, signature_prefix, "signing_input")
                    || key_matches_field(key, signature_prefix, "to_be_signed")
                    || key_matches_field(key, signature_prefix, "signed_bytes")
                    || key_matches_field(key, signature_prefix, "payload")) {
                    candidate.signing_input.assign(bytes.begin(), bytes.end());
                    candidate.has_signing_input = true;
                    continue;
                }

                if (key_matches_field(key, signature_prefix, "public_key_der")
                    || key_matches_field(key, signature_prefix, "public_key")) {
                    candidate.public_key_der.assign(bytes.begin(), bytes.end());
                    candidate.has_public_key_der = true;
                    continue;
                }

                if (key_matches_field(key, signature_prefix, "certificate_der")
                    || key_matches_field(key, signature_prefix, "certificate")
                    || key_matches_field(key, signature_prefix, "x5chain")
                    || key_matches_field(key, signature_prefix, "x5c")) {
                    candidate.certificate_der.assign(bytes.begin(),
                                                     bytes.end());
                    candidate.has_certificate_der = true;
                    continue;
                }

                const std::string unprotected_prefix = signature_prefix
                                                       + std::string("[1]");
                if (key_matches_field(key, unprotected_prefix, "public_key_der")
                    || key_matches_field(key, unprotected_prefix,
                                         "public_key")) {
                    candidate.public_key_der.assign(bytes.begin(), bytes.end());
                    candidate.has_public_key_der = true;
                    continue;
                }
                if (key_matches_field(key, unprotected_prefix, "certificate_der")
                    || key_matches_field(key, unprotected_prefix,
                                         "certificate")) {
                    candidate.certificate_der.assign(bytes.begin(),
                                                     bytes.end());
                    candidate.has_certificate_der = true;
                    continue;
                }

                const std::array<std::string_view, 3U> x5_keys = {
                    std::string_view { "33" }, std::string_view { "x5chain" },
                    std::string_view { "x5c" }
                };
                for (const std::string_view x5_key : x5_keys) {
                    std::string x5_prefix(unprotected_prefix);
                    x5_prefix.push_back('.');
                    x5_prefix.append(x5_key.data(), x5_key.size());
                    if (!string_starts_with(key, x5_prefix)) {
                        continue;
                    }

                    uint32_t cert_index = 0U;
                    if (key.size() == x5_prefix.size()) {
                        cert_index = 0U;
                    } else {
                        const size_t bracket_pos = x5_prefix.size();
                        if (bracket_pos >= key.size() || key[bracket_pos] != '['
                            || key.back() != ']') {
                            continue;
                        }
                        size_t p        = bracket_pos + 1U;
                        uint64_t parsed = 0U;
                        bool have_digit = false;
                        while (p < key.size() && key[p] >= '0'
                               && key[p] <= '9') {
                            have_digit = true;
                            parsed     = parsed * 10U
                                     + static_cast<uint64_t>(
                                         static_cast<unsigned>(key[p] - '0'));
                            if (parsed > UINT32_MAX) {
                                break;
                            }
                            p += 1U;
                        }
                        if (!have_digit || parsed > UINT32_MAX) {
                            continue;
                        }
                        if (p + 1U != key.size() || key[p] != ']') {
                            continue;
                        }
                        cert_index = static_cast<uint32_t>(parsed);
                    }
                    if (cert_index >= kCoseMaxX5ChainCerts) {
                        continue;
                    }
                    if (candidate.certificate_chain_der.size()
                        <= static_cast<size_t>(cert_index)) {
                        candidate.certificate_chain_der.resize(
                            static_cast<size_t>(cert_index) + 1U);
                    }
                    candidate.certificate_chain_der[cert_index].assign(
                        bytes.begin(), bytes.end());
                    candidate.has_certificate_chain_der = true;
                    break;
                }
            }

            if (key_is_indexed_item(key, signature_prefix, 2U)
                && e.value.kind == MetaValueKind::Text) {
                const std::span<const std::byte> text
                    = ctx->store->arena().span(e.value.data.span);
                if (text.size() == 4U
                    && std::memcmp(text.data(), "null", 4U) == 0) {
                    candidate.cose_payload_is_null = true;
                }
            }

            const std::string unprotected_prefix = signature_prefix
                                                   + std::string("[1]");
            if (!candidate.has_cose_unprotected_alg_int
                && key_matches_field(key, unprotected_prefix, "1")
                && e.value.kind == MetaValueKind::Scalar
                && (e.value.elem_type == MetaElementType::I64
                    || e.value.elem_type == MetaElementType::U64)) {
                candidate.cose_unprotected_alg_int
                    = (e.value.elem_type == MetaElementType::U64)
                          ? static_cast<int64_t>(e.value.data.u64)
                          : e.value.data.i64;
                candidate.has_cose_unprotected_alg_int = true;
            }

            if (e.value.kind == MetaValueKind::Text
                && (key_matches_field(key, signature_prefix, "public_key_pem")
                    || key_matches_field(key, signature_prefix, "public_key"))) {
                const std::span<const std::byte> text
                    = ctx->store->arena().span(e.value.data.span);
                if (bytes_all_ascii_printable(text) && text.size() >= 16U) {
                    const std::string pem(reinterpret_cast<const char*>(
                                              text.data()),
                                          text.size());
                    if (pem.find("BEGIN PUBLIC KEY") != std::string::npos) {
                        candidate.public_key_pem     = pem;
                        candidate.has_public_key_pem = true;
                    }
                }
            }

            if (e.value.kind == MetaValueKind::Text
                && (key_matches_field(key, unprotected_prefix, "public_key_pem")
                    || key_matches_field(key, unprotected_prefix,
                                         "public_key"))) {
                const std::span<const std::byte> text
                    = ctx->store->arena().span(e.value.data.span);
                if (bytes_all_ascii_printable(text) && text.size() >= 16U) {
                    const std::string pem(reinterpret_cast<const char*>(
                                              text.data()),
                                          text.size());
                    if (pem.find("BEGIN PUBLIC KEY") != std::string::npos) {
                        candidate.public_key_pem     = pem;
                        candidate.has_public_key_pem = true;
                    }
                }
            }
        }

        if (ctx->options.verify_c2pa) {
            const uint32_t jumb_type = fourcc('j', 'u', 'm', 'b');
            const uint32_t cbor_type = fourcc('c', 'b', 'o', 'r');
            for (uint32_t i = 0U; i < ctx->boxes.size(); ++i) {
                const DecodeContext::ParsedBox& box = ctx->boxes[i];
                if (box.type != cbor_type) {
                    continue;
                }
                const int32_t container_jumb
                    = find_ancestor_box_of_type(*ctx, static_cast<int32_t>(i),
                                                jumb_type);
                if (container_jumb < 0) {
                    continue;
                }
                const DecodeContext::ParsedBox& jumb
                    = ctx->boxes[static_cast<size_t>(container_jumb)];
                if (!jumb.has_jumb_label
                    || !ascii_icase_contains_text(jumb.jumb_label,
                                                  "signature")) {
                    continue;
                }

                C2paVerifySignatureCandidate candidate;
                candidate.prefix = "c2pa.jumbf.signature_box["
                                   + std::to_string(
                                       static_cast<unsigned long long>(i))
                                   + "]";
                candidate.has_source_cbor_box_index = true;
                candidate.source_cbor_box_index     = i;
                if (!cose_decode_sign1_from_cbor_payload(box.payload,
                                                         ctx->options.limits,
                                                         &candidate)) {
                    if (cbor_payload_looks_like_cose_sign1(
                            box.payload, ctx->options.limits)) {
                        candidate.has_invalid_cose_signature_shape = true;
                        out_candidates->push_back(candidate);
                        *out_has_signatures = true;
                    }
                    continue;
                }
                out_candidates->push_back(candidate);
                *out_has_signatures = true;
            }
        }

        for (C2paVerifySignatureCandidate& candidate : *out_candidates) {
            if (candidate.has_certificate_chain_der) {
                std::vector<std::vector<std::byte>> compressed;
                compressed.reserve(candidate.certificate_chain_der.size());
                for (const std::vector<std::byte>& cert :
                     candidate.certificate_chain_der) {
                    if (!cert.empty()) {
                        compressed.push_back(cert);
                    }
                }
                candidate.certificate_chain_der = compressed;
                if (candidate.certificate_chain_der.empty()) {
                    candidate.has_certificate_chain_der = false;
                }
            }

            if (!candidate.has_certificate_der
                && candidate.has_certificate_chain_der
                && !candidate.certificate_chain_der.empty()) {
                candidate.certificate_der = candidate.certificate_chain_der[0];
                candidate.has_certificate_der = true;
            }
            if (candidate.has_certificate_der
                && (!candidate.has_certificate_chain_der
                    || candidate.certificate_chain_der.empty())) {
                candidate.certificate_chain_der.clear();
                candidate.certificate_chain_der.push_back(
                    candidate.certificate_der);
                candidate.has_certificate_chain_der = true;
            }

            if (!candidate.has_algorithm
                && candidate.has_cose_protected_bytes) {
                std::string alg;
                if (cose_extract_algorithm_from_protected_header(
                        std::span<const std::byte>(
                            candidate.cose_protected_bytes.data(),
                            candidate.cose_protected_bytes.size()),
                        &alg)) {
                    candidate.algorithm     = alg;
                    candidate.has_algorithm = true;
                }
            }
            if (!candidate.has_algorithm
                && candidate.has_cose_unprotected_alg_int) {
                std::string alg;
                if (cose_alg_name_from_int(candidate.cose_unprotected_alg_int,
                                           &alg)) {
                    candidate.algorithm     = alg;
                    candidate.has_algorithm = true;
                }
            }
            if (!candidate.has_signature_bytes
                && candidate.has_cose_signature_bytes) {
                candidate.signature_bytes     = candidate.cose_signature_bytes;
                candidate.has_signature_bytes = true;
            }
            if (candidate.cose_payload_is_null
                && !candidate.has_cose_payload_bytes) {
                collect_detached_payload_candidates(
                    *ctx, candidate, &candidate.detached_payload_candidates);
                if (candidate.detached_payload_candidates.size() == 1U) {
                    candidate.cose_payload_bytes
                        = candidate.detached_payload_candidates[0];
                    candidate.has_cose_payload_bytes = true;
                    candidate.cose_payload_is_null   = false;
                }
            }
            if (!candidate.has_signing_input
                && candidate.has_cose_protected_bytes
                && candidate.has_cose_payload_bytes
                && !candidate.cose_payload_is_null) {
                std::vector<std::byte> sig_structure;
                if (cose_build_sig_structure(
                        std::span<const std::byte>(
                            candidate.cose_protected_bytes.data(),
                            candidate.cose_protected_bytes.size()),
                        std::span<const std::byte>(
                            candidate.cose_payload_bytes.data(),
                            candidate.cose_payload_bytes.size()),
                        &sig_structure)) {
                    candidate.signing_input     = sig_structure;
                    candidate.has_signing_input = true;
                }
            }
        }
        return true;
    }

    static bool find_detached_payload_from_claim_bytes(
        const DecodeContext& ctx, const C2paVerifySignatureCandidate& candidate,
        std::vector<std::byte>* out_payload) noexcept
    {
        if (!ctx.store || !out_payload) {
            return false;
        }
        out_payload->clear();

        const std::string_view signature_prefix(candidate.prefix);
        const size_t marker_pos = signature_prefix.rfind(".signatures[");
        if (marker_pos == std::string_view::npos) {
            return false;
        }
        const std::string parent_prefix(
            signature_prefix.substr(0U, marker_pos));
        if (parent_prefix.empty()) {
            return false;
        }

        const uint64_t max_bytes
            = cbor_limit_or_default(ctx.options.limits.max_cbor_bytes_bytes,
                                    8U * 1024U * 1024U);

        const std::array<std::string_view, 4U> claim_fields = {
            std::string_view { "claim" },
            std::string_view { "claim_bytes" },
            std::string_view { "claim_cbor" },
            std::string_view { "claim_payload" },
        };

        std::string full_key;
        const std::span<const Entry> entries = ctx.store->entries();
        for (const std::string_view field : claim_fields) {
            full_key.assign(parent_prefix);
            full_key.push_back('.');
            full_key.append(field.data(), field.size());

            for (const Entry& e : entries) {
                if (e.origin.block != ctx.block
                    || e.key.kind != MetaKeyKind::JumbfCborKey) {
                    continue;
                }
                const std::string_view key
                    = arena_string_view(ctx.store->arena(),
                                        e.key.data.jumbf_cbor_key.key);
                if (key != full_key) {
                    continue;
                }
                if (e.value.kind != MetaValueKind::Bytes
                    && !(e.value.kind == MetaValueKind::Array
                         && e.value.elem_type == MetaElementType::U8)) {
                    continue;
                }
                const std::span<const std::byte> bytes
                    = ctx.store->arena().span(e.value.data.span);
                if (max_bytes != 0U && bytes.size() > max_bytes) {
                    return false;
                }
                out_payload->assign(bytes.begin(), bytes.end());
                return true;
            }
        }

        std::string claim_prefix;
        if (find_indexed_segment_prefix(signature_prefix, ".claims[",
                                        &claim_prefix)) {
            for (const std::string_view field : claim_fields) {
                full_key.assign(claim_prefix);
                full_key.push_back('.');
                full_key.append(field.data(), field.size());

                for (const Entry& e : entries) {
                    if (e.origin.block != ctx.block
                        || e.key.kind != MetaKeyKind::JumbfCborKey) {
                        continue;
                    }
                    const std::string_view key
                        = arena_string_view(ctx.store->arena(),
                                            e.key.data.jumbf_cbor_key.key);
                    if (key != full_key) {
                        continue;
                    }
                    if (e.value.kind != MetaValueKind::Bytes
                        && !(e.value.kind == MetaValueKind::Array
                             && e.value.elem_type == MetaElementType::U8)) {
                        continue;
                    }
                    const std::span<const std::byte> bytes
                        = ctx.store->arena().span(e.value.data.span);
                    if (max_bytes != 0U && bytes.size() > max_bytes) {
                        return false;
                    }
                    out_payload->assign(bytes.begin(), bytes.end());
                    return true;
                }
            }
        }

        const std::string claims_scope = parent_prefix + ".claims[";
        struct ClaimFieldHit final {
            std::string claim_prefix;
            std::vector<std::byte> payload;
            bool has_payload = false;
        };
        std::vector<ClaimFieldHit> claim_hits;

        for (const Entry& e : entries) {
            if (e.origin.block != ctx.block
                || e.key.kind != MetaKeyKind::JumbfCborKey) {
                continue;
            }
            const std::string_view key
                = arena_string_view(ctx.store->arena(),
                                    e.key.data.jumbf_cbor_key.key);
            if (!string_starts_with(key, claims_scope)) {
                continue;
            }
            if (e.value.kind != MetaValueKind::Bytes
                && !(e.value.kind == MetaValueKind::Array
                     && e.value.elem_type == MetaElementType::U8)) {
                continue;
            }

            std::string claim_item_prefix;
            if (!find_indexed_segment_prefix(key, ".claims[",
                                             &claim_item_prefix)) {
                continue;
            }

            bool field_match = false;
            for (const std::string_view field : claim_fields) {
                full_key.assign(claim_item_prefix);
                full_key.push_back('.');
                full_key.append(field.data(), field.size());
                if (key == full_key) {
                    field_match = true;
                    break;
                }
            }
            if (!field_match) {
                continue;
            }

            const std::span<const std::byte> bytes = ctx.store->arena().span(
                e.value.data.span);
            if (max_bytes != 0U && bytes.size() > max_bytes) {
                return false;
            }

            size_t slot = static_cast<size_t>(-1);
            for (size_t i = 0U; i < claim_hits.size(); ++i) {
                if (claim_hits[i].claim_prefix == claim_item_prefix) {
                    slot = i;
                    break;
                }
            }
            if (slot == static_cast<size_t>(-1)) {
                ClaimFieldHit hit;
                hit.claim_prefix.assign(claim_item_prefix.data(),
                                        claim_item_prefix.size());
                hit.payload.assign(bytes.begin(), bytes.end());
                hit.has_payload = true;
                claim_hits.push_back(hit);
            } else if (!claim_hits[slot].has_payload) {
                claim_hits[slot].payload.assign(bytes.begin(), bytes.end());
                claim_hits[slot].has_payload = true;
            }
        }

        if (claim_hits.size() == 1U && claim_hits[0].has_payload) {
            *out_payload = claim_hits[0].payload;
            return true;
        }

        return false;
    }

#if OPENMETA_ENABLE_C2PA_VERIFY
    static C2paVerifyEvaluation evaluate_c2pa_verify_candidates(
        DecodeContext* ctx, C2paVerifyBackend selected,
        const std::vector<C2paVerifySignatureCandidate>& candidates) noexcept
    {
        C2paVerifyEvaluation evaluation;

        C2paProfileSummary profile_summary;
        if (!collect_c2pa_profile_summary(ctx, &profile_summary)) {
            profile_summary = C2paProfileSummary {};
        }
        evaluate_c2pa_profile_summary(
            profile_summary,
            ctx && ctx->options.verify_require_resolved_references,
            &evaluation);
        if (evaluation.profile_status == C2paVerifyDetailStatus::Fail) {
            evaluation.status = C2paVerifyStatus::VerificationFailed;
            return evaluation;
        }

        if (selected == C2paVerifyBackend::Native) {
            evaluation.status = C2paVerifyStatus::NotImplemented;
            return evaluation;
        }
        if (selected != C2paVerifyBackend::OpenSsl) {
            evaluation.status = C2paVerifyStatus::BackendUnavailable;
            return evaluation;
        }

        const bool require_trusted_chain
            = (ctx && ctx->options.verify_require_trusted_chain);

        bool has_known_algorithm        = false;
        bool attempted_verify           = false;
        bool saw_signature_mismatch     = false;
        bool saw_invalid_signature      = false;
        bool saw_unverifiable_candidate = false;
        bool saw_backend_error          = false;
        bool saw_chain_backend_error    = false;

        bool best_verified               = false;
        uint8_t best_verified_chain_rank = 0U;
        C2paVerifyDetailStatus best_verified_chain_status
            = C2paVerifyDetailStatus::NotChecked;
        const char* best_verified_chain_reason = "not_checked";

        uint8_t best_attempt_chain_rank = 0U;
        C2paVerifyDetailStatus best_attempt_chain_status
            = C2paVerifyDetailStatus::NotChecked;
        const char* best_attempt_chain_reason = "not_checked";

        for (const C2paVerifySignatureCandidate& candidate : candidates) {
            if (candidate.has_invalid_cose_signature_shape) {
                saw_invalid_signature = true;
                continue;
            }
            if (!candidate.has_algorithm || !candidate.has_signature_bytes) {
                continue;
            }
            const std::string_view algorithm(candidate.algorithm);
            if (!is_alg_ecdsa(algorithm) && !is_alg_eddsa(algorithm)
                && !is_alg_rsa(algorithm)) {
                continue;
            }
            has_known_algorithm = true;

            if (!c2pa_verify_candidate_has_key_material(candidate)) {
                // Keep scanning: some containers have multiple signatures and
                // not all of them are complete/usable.
                continue;
            }

            std::vector<C2paVerifySignatureCandidate> verify_trials;
            if (candidate.has_signing_input) {
                verify_trials.push_back(candidate);
            } else if (candidate.has_cose_protected_bytes) {
                if (candidate.has_cose_payload_bytes
                    && !candidate.cose_payload_is_null) {
                    std::vector<std::byte> sig_structure;
                    if (cose_build_sig_structure(
                            std::span<const std::byte>(
                                candidate.cose_protected_bytes.data(),
                                candidate.cose_protected_bytes.size()),
                            std::span<const std::byte>(
                                candidate.cose_payload_bytes.data(),
                                candidate.cose_payload_bytes.size()),
                            &sig_structure)) {
                        C2paVerifySignatureCandidate trial = candidate;
                        trial.signing_input                = sig_structure;
                        trial.has_signing_input            = true;
                        verify_trials.push_back(trial);
                    }
                }
                for (const std::vector<std::byte>& detached_payload :
                     candidate.detached_payload_candidates) {
                    std::vector<std::byte> sig_structure;
                    if (!cose_build_sig_structure(
                            std::span<const std::byte>(
                                candidate.cose_protected_bytes.data(),
                                candidate.cose_protected_bytes.size()),
                            std::span<const std::byte>(detached_payload.data(),
                                                       detached_payload.size()),
                            &sig_structure)) {
                        continue;
                    }
                    C2paVerifySignatureCandidate trial = candidate;
                    trial.signing_input                = sig_structure;
                    trial.has_signing_input            = true;
                    verify_trials.push_back(trial);
                }
            }

            if (verify_trials.empty()) {
                continue;
            }
            attempted_verify = true;

#    if OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
            C2paVerifyDetailStatus chain_status
                = C2paVerifyDetailStatus::NotChecked;
            const char* chain_reason = "not_checked";
            bool chain_backend_error = false;
            const OpenSslChainResult chain_result
                = openssl_verify_certificate_chain(candidate, &chain_reason);
            switch (chain_result) {
            case OpenSslChainResult::Pass:
                chain_status = C2paVerifyDetailStatus::Pass;
                break;
            case OpenSslChainResult::Fail:
                chain_status = C2paVerifyDetailStatus::Fail;
                break;
            case OpenSslChainResult::NotChecked: break;
            case OpenSslChainResult::BackendError:
                chain_backend_error = true;
                chain_status        = C2paVerifyDetailStatus::Fail;
                // Treat trust-store failures as a detail signal rather than a
                // hard failure for signature verification.
                break;
            }

            bool candidate_verified = false;
            for (const C2paVerifySignatureCandidate& trial : verify_trials) {
                const OpenSslVerifyResult result = openssl_verify_candidate(
                    trial);
                switch (result) {
                case OpenSslVerifyResult::Verified: {
                    if (require_trusted_chain
                        && chain_status != C2paVerifyDetailStatus::Pass) {
                        if (chain_backend_error) {
                            saw_chain_backend_error = true;
                        }
                        break;
                    }
                    const uint8_t candidate_rank = c2pa_chain_rank(
                        chain_status);
                    if (!best_verified
                        || candidate_rank > best_verified_chain_rank) {
                        best_verified              = true;
                        best_verified_chain_rank   = candidate_rank;
                        best_verified_chain_status = chain_status;
                        best_verified_chain_reason = chain_reason;
                    }
                    candidate_verified = true;
                    break;
                }
                case OpenSslVerifyResult::VerificationFailed:
                    saw_signature_mismatch = true;
                    break;
                case OpenSslVerifyResult::InvalidSignature:
                    saw_invalid_signature = true;
                    break;
                case OpenSslVerifyResult::NotSupported:
                    saw_unverifiable_candidate = true;
                    break;
                case OpenSslVerifyResult::BackendError:
                    saw_backend_error = true;
                    break;
                }
                if (candidate_verified) {
                    break;
                }
            }

            if (chain_backend_error) {
                saw_chain_backend_error = true;
            }

            const uint8_t candidate_chain_rank = c2pa_chain_rank(chain_status);
            if (candidate_chain_rank > best_attempt_chain_rank) {
                best_attempt_chain_rank   = candidate_chain_rank;
                best_attempt_chain_status = chain_status;
                best_attempt_chain_reason = chain_reason;
            }
#    else
            (void)candidate;
            evaluation.status = C2paVerifyStatus::BackendUnavailable;
            return evaluation;
#    endif
        }

        if (attempted_verify) {
            if (best_verified) {
                evaluation.chain_status = best_verified_chain_status;
                evaluation.chain_reason = best_verified_chain_reason;
                if (evaluation.chain_status
                    == C2paVerifyDetailStatus::NotChecked) {
                    evaluation.chain_reason = "no_certificate";
                }
                // Only the selected claim/signature bytes have been checked.
                // The decoder does not receive the complete containing asset,
                // so it cannot establish a C2PA hard binding and must never
                // report full asset verification here.
                evaluation.status = C2paVerifyStatus::SignatureVerifiedOnly;
                return evaluation;
            }

            evaluation.chain_status = best_attempt_chain_status;
            evaluation.chain_reason = best_attempt_chain_reason;
            if (evaluation.chain_status == C2paVerifyDetailStatus::NotChecked) {
                evaluation.chain_reason = "no_certificate";
            }

            if (require_trusted_chain) {
                evaluation.status = saw_chain_backend_error
                                        ? C2paVerifyStatus::BackendUnavailable
                                        : C2paVerifyStatus::VerificationFailed;
                return evaluation;
            }

            if (saw_signature_mismatch) {
                evaluation.status = C2paVerifyStatus::VerificationFailed;
                return evaluation;
            }
            if (saw_invalid_signature) {
                evaluation.status = C2paVerifyStatus::InvalidSignature;
                return evaluation;
            }
            if (saw_unverifiable_candidate) {
                evaluation.status = C2paVerifyStatus::VerificationFailed;
                return evaluation;
            }
            if (saw_backend_error) {
                evaluation.status = C2paVerifyStatus::BackendUnavailable;
                return evaluation;
            }

            evaluation.status = C2paVerifyStatus::NotImplemented;
            return evaluation;
        }
        if (saw_invalid_signature) {
            evaluation.status = C2paVerifyStatus::InvalidSignature;
            return evaluation;
        }
        if (has_known_algorithm) {
            evaluation.status = C2paVerifyStatus::NotImplemented;
            return evaluation;
        }
        evaluation.status = C2paVerifyStatus::NotImplemented;
        return evaluation;
    }
#endif

    static bool append_c2pa_verify_scaffold_fields(DecodeContext* ctx) noexcept
    {
        if (!ctx || !ctx->store) {
            return false;
        }

        bool has_signatures = false;
        std::vector<C2paVerifySignatureCandidate> verify_candidates;
        if (!collect_c2pa_verify_candidates(ctx, &has_signatures,
                                            &verify_candidates)) {
            return false;
        }

        const bool requested           = ctx->options.verify_c2pa;
        C2paVerifyBackend selected     = C2paVerifyBackend::None;
        C2paVerifyStatus verify_status = C2paVerifyStatus::NotRequested;
        C2paVerifyEvaluation evaluation;
        if (requested) {
#if OPENMETA_ENABLE_C2PA_VERIFY
            selected = resolve_c2pa_verify_backend(ctx->options.verify_backend);
            if (!has_signatures) {
                verify_status = C2paVerifyStatus::NoSignatures;
            } else if (selected == C2paVerifyBackend::None) {
                verify_status = C2paVerifyStatus::BackendUnavailable;
            } else {
                evaluation    = evaluate_c2pa_verify_candidates(ctx, selected,
                                                                verify_candidates);
                verify_status = evaluation.status;
            }
#else
            verify_status = C2paVerifyStatus::DisabledByBuild;
#endif
        }

        ctx->result.verify_status           = verify_status;
        ctx->result.verify_backend_selected = selected;

        if (!requested && !ctx->c2pa_detected && !has_signatures) {
            return true;
        }

        if ((ctx->c2pa_detected || has_signatures)
            && !append_c2pa_marker(ctx, "verify.scaffold")) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.verify.requested", requested ? 1U : 0U,
                           EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.verify.require_resolved_references",
                           ctx->options.verify_require_resolved_references ? 1U
                                                                           : 0U,
                           EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.verify.require_trusted_chain",
                           ctx->options.verify_require_trusted_chain ? 1U : 0U,
                           EntryFlags::Derived)) {
            return false;
        }
#if OPENMETA_ENABLE_C2PA_VERIFY
        if (!emit_field_u8(ctx, "c2pa.verify.enabled_in_build", 1U,
                           EntryFlags::Derived)) {
            return false;
        }
#else
        if (!emit_field_u8(ctx, "c2pa.verify.enabled_in_build", 0U,
                           EntryFlags::Derived)) {
            return false;
        }
#endif
        if (!emit_field_u8(ctx, "c2pa.verify.signatures_present",
                           has_signatures ? 1U : 0U, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_text(ctx, "c2pa.verify.backend_requested",
                             c2pa_verify_backend_name(
                                 ctx->options.verify_backend),
                             EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_text(ctx, "c2pa.verify.backend_selected",
                             c2pa_verify_backend_name(selected),
                             EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_text(ctx, "c2pa.verify.status",
                             c2pa_verify_status_name(verify_status),
                             EntryFlags::Derived)) {
            return false;
        }

        if (!emit_field_text(ctx, "c2pa.verify.profile_status",
                             c2pa_verify_detail_status_name(
                                 evaluation.profile_status),
                             EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_text(ctx, "c2pa.verify.profile_reason",
                             evaluation.profile_reason, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_text(ctx, "c2pa.verify.chain_status",
                             c2pa_verify_detail_status_name(
                                 evaluation.chain_status),
                             EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_text(ctx, "c2pa.verify.chain_reason",
                             evaluation.chain_reason, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.verify.claim_count",
                            evaluation.profile_summary.claim_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.verify.signature_count",
                            evaluation.profile_summary.signature_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.verify.signature_linked_count",
                            evaluation.profile_summary.signature_linked,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.verify.signature_orphan_count",
                            evaluation.profile_summary.signature_orphan,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx, "c2pa.verify.explicit_reference_signature_count",
                evaluation.profile_summary.explicit_reference_signature_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx,
                "c2pa.verify.explicit_reference_unresolved_signature_count",
                evaluation.profile_summary
                    .explicit_reference_unresolved_signature_count,
                EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(
                ctx, "c2pa.verify.explicit_reference_ambiguous_signature_count",
                evaluation.profile_summary
                    .explicit_reference_ambiguous_signature_count,
                EntryFlags::Derived)) {
            return false;
        }
        return true;
    }


    static bool ascii_icase_contains(std::span<const std::byte> bytes,
                                     std::string_view needle,
                                     uint32_t max_bytes) noexcept
    {
        if (needle.empty() || bytes.empty()) {
            return false;
        }
        const size_t haystack_size = (max_bytes != 0U
                                      && bytes.size() > max_bytes)
                                         ? static_cast<size_t>(max_bytes)
                                         : bytes.size();
        if (haystack_size < needle.size()) {
            return false;
        }
        for (size_t i = 0; i + needle.size() <= haystack_size; ++i) {
            bool match = true;
            for (size_t j = 0; j < needle.size(); ++j) {
                uint8_t a = u8(bytes[i + j]);
                uint8_t b = static_cast<uint8_t>(needle[j]);
                if (a >= 'A' && a <= 'Z') {
                    a = static_cast<uint8_t>(a + 32U);
                }
                if (b >= 'A' && b <= 'Z') {
                    b = static_cast<uint8_t>(b + 32U);
                }
                if (a != b) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
        return false;
    }


    static bool bytes_valid_utf8(std::span<const std::byte> bytes) noexcept
    {
        size_t index = 0;
        while (index < bytes.size()) {
            const uint8_t c0 = u8(bytes[index]);
            if ((c0 & 0x80U) == 0U) {
                index += 1U;
                continue;
            }

            uint32_t needed    = 0U;
            uint32_t min_cp    = 0U;
            uint32_t codepoint = 0U;

            if ((c0 & 0xE0U) == 0xC0U) {
                needed    = 1U;
                min_cp    = 0x80U;
                codepoint = c0 & 0x1FU;
            } else if ((c0 & 0xF0U) == 0xE0U) {
                needed    = 2U;
                min_cp    = 0x800U;
                codepoint = c0 & 0x0FU;
            } else if ((c0 & 0xF8U) == 0xF0U) {
                needed    = 3U;
                min_cp    = 0x10000U;
                codepoint = c0 & 0x07U;
            } else {
                return false;
            }

            if (index + needed >= bytes.size()) {
                return false;
            }

            for (uint32_t j = 0U; j < needed; ++j) {
                const uint8_t c = u8(bytes[index + 1U + j]);
                if ((c & 0xC0U) != 0x80U) {
                    return false;
                }
                codepoint = (codepoint << 6U)
                            | static_cast<uint32_t>(c & 0x3FU);
            }

            if (codepoint < min_cp || codepoint > 0x10FFFFU
                || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
                return false;
            }
            index += static_cast<size_t>(needed + 1U);
        }
        return true;
    }


    static bool sanitize_cbor_path_segment(std::span<const std::byte> bytes,
                                           uint32_t max_output_bytes,
                                           std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (bytes.empty()) {
            out->assign("_");
            return true;
        }

        const size_t limit = (max_output_bytes != 0U
                              && bytes.size() > max_output_bytes)
                                 ? static_cast<size_t>(max_output_bytes)
                                 : bytes.size();
        for (size_t i = 0; i < limit; ++i) {
            const uint8_t c    = u8(bytes[i]);
            const bool allowed = (c >= 'a' && c <= 'z')
                                 || (c >= 'A' && c <= 'Z')
                                 || (c >= '0' && c <= '9') || c == '_'
                                 || c == '-' || c == '.';
            if (allowed) {
                out->push_back(static_cast<char>(c));
            } else {
                out->push_back('_');
            }
        }
        if (out->empty()) {
            out->assign("_");
        }
        return true;
    }

    struct CborHead final {
        uint8_t major   = 0U;
        uint8_t addl    = 0U;
        uint64_t arg    = 0U;
        bool indefinite = false;
    };

    static bool read_cbor_head(std::span<const std::byte> bytes, uint64_t* pos,
                               CborHead* out) noexcept
    {
        if (!pos || !out || *pos >= bytes.size()) {
            return false;
        }
        const uint8_t ib = u8(bytes[*pos]);
        *pos += 1U;
        out->major      = static_cast<uint8_t>((ib >> 5U) & 0x07U);
        out->addl       = static_cast<uint8_t>(ib & 0x1FU);
        out->indefinite = false;

        if (out->addl <= 23U) {
            out->arg = static_cast<uint64_t>(out->addl);
            return true;
        }
        if (out->addl == 24U) {
            if (*pos + 1U > bytes.size()) {
                return false;
            }
            out->arg = static_cast<uint64_t>(u8(bytes[*pos]));
            *pos += 1U;
            return true;
        }
        if (out->addl == 25U) {
            uint16_t value = 0;
            if (!read_u16be(bytes, *pos, &value)) {
                return false;
            }
            out->arg = static_cast<uint64_t>(value);
            *pos += 2U;
            return true;
        }
        if (out->addl == 26U) {
            uint32_t value = 0;
            if (!read_u32be(bytes, *pos, &value)) {
                return false;
            }
            out->arg = static_cast<uint64_t>(value);
            *pos += 4U;
            return true;
        }
        if (out->addl == 27U) {
            uint64_t value = 0;
            if (!read_u64be(bytes, *pos, &value)) {
                return false;
            }
            out->arg = value;
            *pos += 8U;
            return true;
        }
        if (out->addl == 31U) {
            out->indefinite = true;
            out->arg        = 0U;
            // Break marker (0xFF) is not a data item and must be handled by
            // callers that parse indefinite-length containers.
            return out->major != 7U;
        }
        // Unsupported: reserved addl values.
        return false;
    }


    static bool cbor_item_budget_take(DecodeContext* ctx) noexcept
    {
        if (!ctx) {
            return false;
        }
        ctx->result.cbor_items += 1U;
        const uint32_t max_items = ctx->options.limits.max_cbor_items;
        if (max_items != 0U && ctx->result.cbor_items > max_items) {
            ctx->result.status = JumbfDecodeStatus::LimitExceeded;
            return false;
        }
        return true;
    }


    static bool cbor_depth_ok(DecodeContext* ctx, uint32_t depth) noexcept
    {
        if (!ctx) {
            return false;
        }
        const uint32_t max_depth = ctx->options.limits.max_cbor_depth;
        if (max_depth != 0U && depth > max_depth) {
            ctx->result.status = JumbfDecodeStatus::LimitExceeded;
            return false;
        }
        return true;
    }

    static bool read_cbor_text(std::span<const std::byte> cbor, uint64_t* pos,
                               uint64_t len,
                               std::span<const std::byte>* out) noexcept
    {
        if (!out || !pos) {
            return false;
        }
        if (*pos > cbor.size() || len > cbor.size() - *pos) {
            return false;
        }
        *out = cbor.subspan(static_cast<size_t>(*pos),
                            static_cast<size_t>(len));
        *pos += len;
        return true;
    }

    static bool cbor_peek_break(std::span<const std::byte> cbor,
                                uint64_t pos) noexcept
    {
        if (pos >= cbor.size()) {
            return false;
        }
        return u8(cbor[pos]) == 0xFFU;
    }

    static bool cbor_consume_break(std::span<const std::byte> cbor,
                                   uint64_t* pos) noexcept
    {
        if (!pos || !cbor_peek_break(cbor, *pos)) {
            return false;
        }
        *pos += 1U;
        return true;
    }

    static const char* cbor_major_suffix(uint8_t major) noexcept
    {
        switch (major) {
        case 0U: return "u";
        case 1U: return "n";
        case 2U: return "bytes";
        case 3U: return "text";
        case 4U: return "arr";
        case 5U: return "map";
        case 6U: return "tag";
        case 7U: return "simple";
        default: return "key";
        }
    }

    static bool assign_synth_cbor_key(uint32_t map_index,
                                      std::string_view suffix,
                                      uint32_t max_output_bytes,
                                      std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        out->reserve(24U + suffix.size());
        out->append("k");
        out->append(std::to_string(static_cast<unsigned long long>(map_index)));
        out->push_back('_');
        out->append(suffix.data(), suffix.size());
        if (max_output_bytes != 0U
            && out->size() > static_cast<size_t>(max_output_bytes)) {
            out->resize(static_cast<size_t>(max_output_bytes));
        }
        if (out->empty()) {
            out->assign("_");
        }
        return true;
    }

    static bool skip_cbor_item(DecodeContext* ctx,
                               std::span<const std::byte> cbor, uint64_t* pos,
                               uint32_t depth) noexcept;

    static bool skip_cbor_item_from_head(DecodeContext* ctx,
                                         std::span<const std::byte> cbor,
                                         uint64_t* pos, uint32_t depth,
                                         const CborHead& head) noexcept
    {
        if (!ctx || !pos || !cbor_depth_ok(ctx, depth)) {
            return false;
        }

        if (head.major == 0U || head.major == 1U || head.major == 7U) {
            return true;
        }

        if (head.major == 2U || head.major == 3U) {
            if (!head.indefinite) {
                const uint64_t max_len
                    = (head.major == 2U)
                          ? ctx->options.limits.max_cbor_bytes_bytes
                          : ctx->options.limits.max_cbor_text_bytes;
                if (max_len != 0U && head.arg > max_len) {
                    ctx->result.status = JumbfDecodeStatus::LimitExceeded;
                    return false;
                }
                if (*pos > cbor.size() || head.arg > cbor.size() - *pos) {
                    return false;
                }
                *pos += head.arg;
                return true;
            }

            uint64_t total_len = 0U;
            while (true) {
                if (cbor_peek_break(cbor, *pos)) {
                    return cbor_consume_break(cbor, pos);
                }
                CborHead chunk;
                if (!read_cbor_head(cbor, pos, &chunk)
                    || !cbor_item_budget_take(ctx)) {
                    return false;
                }
                if (chunk.major != head.major || chunk.indefinite) {
                    return false;
                }
                if (total_len > UINT64_MAX - chunk.arg) {
                    return false;
                }
                total_len += chunk.arg;
                const uint64_t max_len
                    = (head.major == 2U)
                          ? ctx->options.limits.max_cbor_bytes_bytes
                          : ctx->options.limits.max_cbor_text_bytes;
                if (max_len != 0U && total_len > max_len) {
                    ctx->result.status = JumbfDecodeStatus::LimitExceeded;
                    return false;
                }
                if (*pos > cbor.size() || chunk.arg > cbor.size() - *pos) {
                    return false;
                }
                *pos += chunk.arg;
            }
        }

        if (head.major == 4U) {
            if (!head.indefinite) {
                for (uint64_t index = 0; index < head.arg; ++index) {
                    if (!skip_cbor_item(ctx, cbor, pos, depth + 1U)) {
                        return false;
                    }
                }
                return true;
            }
            while (true) {
                if (cbor_peek_break(cbor, *pos)) {
                    return cbor_consume_break(cbor, pos);
                }
                if (!skip_cbor_item(ctx, cbor, pos, depth + 1U)) {
                    return false;
                }
            }
        }

        if (head.major == 5U) {
            if (!head.indefinite) {
                for (uint64_t pair = 0; pair < head.arg; ++pair) {
                    if (!skip_cbor_item(ctx, cbor, pos, depth + 1U)
                        || !skip_cbor_item(ctx, cbor, pos, depth + 1U)) {
                        return false;
                    }
                }
                return true;
            }
            while (true) {
                if (cbor_peek_break(cbor, *pos)) {
                    return cbor_consume_break(cbor, pos);
                }
                if (!skip_cbor_item(ctx, cbor, pos, depth + 1U)
                    || !skip_cbor_item(ctx, cbor, pos, depth + 1U)) {
                    return false;
                }
            }
        }

        if (head.major == 6U) {
            if (head.indefinite) {
                return false;
            }
            return skip_cbor_item(ctx, cbor, pos, depth + 1U);
        }

        return false;
    }

    static bool skip_cbor_item(DecodeContext* ctx,
                               std::span<const std::byte> cbor, uint64_t* pos,
                               uint32_t depth) noexcept
    {
        if (!ctx || !pos || !cbor_depth_ok(ctx, depth)) {
            return false;
        }
        CborHead head;
        if (!read_cbor_head(cbor, pos, &head) || !cbor_item_budget_take(ctx)) {
            return false;
        }
        return skip_cbor_item_from_head(ctx, cbor, pos, depth, head);
    }

    static uint32_t cbor_half_to_f32_bits(uint16_t half_bits) noexcept
    {
        const uint32_t sign = static_cast<uint32_t>(half_bits & 0x8000U) << 16U;
        uint32_t exp        = static_cast<uint32_t>((half_bits >> 10U) & 0x1FU);
        uint32_t frac       = static_cast<uint32_t>(half_bits & 0x03FFU);

        if (exp == 0U) {
            if (frac == 0U) {
                return sign;
            }

            int32_t shift = 0;
            while ((frac & 0x0400U) == 0U) {
                frac <<= 1U;
                shift += 1;
            }
            frac &= 0x03FFU;
            exp = static_cast<uint32_t>(127 - 15 - shift + 1);
            return sign | (exp << 23U) | (frac << 13U);
        }

        if (exp == 31U) {
            return sign | 0x7F800000U | (frac << 13U);
        }

        exp = exp + static_cast<uint32_t>(127 - 15);
        return sign | (exp << 23U) | (frac << 13U);
    }

    static bool append_cbor_chunk(std::span<const std::byte> bytes,
                                  uint32_t max_total,
                                  std::vector<std::byte>* out) noexcept
    {
        if (!out) {
            return false;
        }
        const size_t old_size = out->size();
        if (bytes.size() > SIZE_MAX - old_size) {
            return false;
        }
        const size_t new_size = old_size + bytes.size();
        if (max_total != 0U && new_size > static_cast<size_t>(max_total)) {
            return false;
        }
        out->insert(out->end(), bytes.begin(), bytes.end());
        return true;
    }

    static bool read_cbor_byte_or_text_payload(
        DecodeContext* ctx, std::span<const std::byte> cbor, uint64_t* pos,
        const CborHead& head, std::vector<std::byte>* out) noexcept
    {
        if (!ctx || !pos || !out) {
            return false;
        }
        out->clear();

        const uint32_t max_total
            = (head.major == 2U) ? ctx->options.limits.max_cbor_bytes_bytes
                                 : ctx->options.limits.max_cbor_text_bytes;
        if (!head.indefinite) {
            std::span<const std::byte> payload;
            if (!read_cbor_text(cbor, pos, head.arg, &payload)) {
                return false;
            }
            if (!append_cbor_chunk(payload, max_total, out)) {
                if (max_total != 0U) {
                    ctx->result.status = JumbfDecodeStatus::LimitExceeded;
                }
                return false;
            }
            return true;
        }

        while (true) {
            if (cbor_peek_break(cbor, *pos)) {
                return cbor_consume_break(cbor, pos);
            }
            CborHead chunk;
            if (!read_cbor_head(cbor, pos, &chunk)
                || !cbor_item_budget_take(ctx)) {
                return false;
            }
            if (chunk.major != head.major || chunk.indefinite) {
                return false;
            }
            std::span<const std::byte> payload;
            if (!read_cbor_text(cbor, pos, chunk.arg, &payload)) {
                return false;
            }
            if (!append_cbor_chunk(payload, max_total, out)) {
                if (max_total != 0U) {
                    ctx->result.status = JumbfDecodeStatus::LimitExceeded;
                }
                return false;
            }
        }
    }


    static bool parse_cbor_key(DecodeContext* ctx,
                               std::span<const std::byte> cbor, uint64_t* pos,
                               uint32_t depth, uint32_t map_index,
                               std::string* out) noexcept
    {
        if (!ctx || !pos || !out) {
            return false;
        }
        if (!cbor_depth_ok(ctx, depth)) {
            return false;
        }

        CborHead head;
        if (!read_cbor_head(cbor, pos, &head) || !cbor_item_budget_take(ctx)) {
            return false;
        }

        if (head.major == 3U) {
            std::vector<std::byte> text_bytes;
            if (!read_cbor_byte_or_text_payload(ctx, cbor, pos, head,
                                                &text_bytes)) {
                return false;
            }
            return sanitize_cbor_path_segment(
                std::span<const std::byte>(text_bytes.data(), text_bytes.size()),
                ctx->options.limits.max_cbor_key_bytes, out);
        }

        if (head.major == 0U) {
            out->assign(
                std::to_string(static_cast<unsigned long long>(head.arg)));
            return true;
        }

        if (head.major == 1U) {
            out->assign("n");
            out->append(
                std::to_string(static_cast<unsigned long long>(head.arg)));
            return true;
        }

        if (head.major == 7U) {
            if (head.indefinite) {
                return false;
            }
            if (head.addl == 20U) {
                out->assign("false");
                return true;
            }
            if (head.addl == 21U) {
                out->assign("true");
                return true;
            }
            if (head.addl == 22U) {
                out->assign("null");
                return true;
            }
            if (head.addl == 23U) {
                out->assign("undefined");
                return true;
            }
            out->assign("simple");
            return true;
        }

        if (!skip_cbor_item_from_head(ctx, cbor, pos, depth + 1U, head)) {
            return false;
        }
        return assign_synth_cbor_key(map_index, cbor_major_suffix(head.major),
                                     ctx->options.limits.max_cbor_key_bytes,
                                     out);
    }

    static bool parse_cbor_item(DecodeContext* ctx,
                                std::span<const std::byte> cbor, uint64_t* pos,
                                uint32_t depth, std::string_view path) noexcept;


    static bool parse_cbor_item(DecodeContext* ctx,
                                std::span<const std::byte> cbor, uint64_t* pos,
                                uint32_t depth, std::string_view path) noexcept
    {
        if (!ctx || !pos || !cbor_depth_ok(ctx, depth)) {
            return false;
        }

        CborHead head;
        if (!read_cbor_head(cbor, pos, &head) || !cbor_item_budget_take(ctx)) {
            return false;
        }

        if (head.major == 0U) {
            return emit_cbor_value(ctx, path, make_u64(head.arg));
        }

        if (head.major == 1U) {
            if (head.arg >= static_cast<uint64_t>(INT64_MAX)) {
                const std::string value
                    = "-(1+"
                      + std::to_string(
                          static_cast<unsigned long long>(head.arg))
                      + ")";
                return emit_cbor_value(ctx, path,
                                       make_text(ctx->store->arena(), value,
                                                 TextEncoding::Ascii));
            }
            const int64_t value = -1 - static_cast<int64_t>(head.arg);
            return emit_cbor_value(ctx, path, make_i64(value));
        }

        if (head.major == 2U) {
            std::vector<std::byte> data_bytes;
            if (!read_cbor_byte_or_text_payload(ctx, cbor, pos, head,
                                                &data_bytes)) {
                return false;
            }
            return emit_cbor_value(
                ctx, path,
                make_bytes(ctx->store->arena(),
                           std::span<const std::byte>(data_bytes.data(),
                                                      data_bytes.size())));
        }

        if (head.major == 3U) {
            std::vector<std::byte> text_bytes;
            if (!read_cbor_byte_or_text_payload(ctx, cbor, pos, head,
                                                &text_bytes)) {
                return false;
            }
            const std::span<const std::byte> text(text_bytes.data(),
                                                  text_bytes.size());
            if (bytes_valid_utf8(text)) {
                const std::string_view text_sv(reinterpret_cast<const char*>(
                                                   text.data()),
                                               text.size());
                return emit_cbor_value(ctx, path,
                                       make_text(ctx->store->arena(), text_sv,
                                                 TextEncoding::Utf8));
            }
            return emit_cbor_value(ctx, path,
                                   make_bytes(ctx->store->arena(), text));
        }

        if (head.major == 4U) {
            uint64_t index = 0U;
            while (true) {
                if (head.indefinite && cbor_peek_break(cbor, *pos)) {
                    return cbor_consume_break(cbor, pos);
                }
                if (!head.indefinite && index >= head.arg) {
                    return true;
                }
                std::string child_path;
                child_path.reserve(path.size() + 24U);
                child_path.append(path.data(), path.size());
                child_path.push_back('[');
                child_path.append(
                    std::to_string(static_cast<unsigned long long>(index)));
                child_path.push_back(']');
                if (!parse_cbor_item(ctx, cbor, pos, depth + 1U, child_path)) {
                    return false;
                }
                index += 1U;
            }
        }

        if (head.major == 5U) {
            uint64_t map_index = 0U;
            while (true) {
                if (head.indefinite && cbor_peek_break(cbor, *pos)) {
                    return cbor_consume_break(cbor, pos);
                }
                if (!head.indefinite && map_index >= head.arg) {
                    return true;
                }
                std::string key_segment;
                if (!parse_cbor_key(ctx, cbor, pos, depth + 1U,
                                    static_cast<uint32_t>(map_index),
                                    &key_segment)) {
                    return false;
                }

                std::string child_path;
                child_path.reserve(path.size() + key_segment.size() + 1U);
                child_path.append(path.data(), path.size());
                if (!child_path.empty()) {
                    child_path.push_back('.');
                }
                child_path.append(key_segment.data(), key_segment.size());

                if (!parse_cbor_item(ctx, cbor, pos, depth + 1U, child_path)) {
                    return false;
                }
                map_index += 1U;
            }
        }

        if (head.major == 6U) {
            if (head.indefinite) {
                return false;
            }
            std::string tag_field(path);
            tag_field.append(".@tag");
            if (!emit_cbor_value(ctx, tag_field, make_u64(head.arg))) {
                return false;
            }
            return parse_cbor_item(ctx, cbor, pos, depth + 1U, path);
        }

        if (head.major == 7U) {
            if (head.indefinite) {
                return false;
            }
            if (head.addl <= 19U) {
                return emit_cbor_value(ctx, path,
                                       make_u8(static_cast<uint8_t>(head.addl)));
            }
            if (head.addl == 20U) {
                return emit_cbor_value(ctx, path, make_u8(0U));
            }
            if (head.addl == 21U) {
                return emit_cbor_value(ctx, path, make_u8(1U));
            }
            if (head.addl == 22U) {
                return emit_cbor_value(ctx, path,
                                       make_text(ctx->store->arena(), "null",
                                                 TextEncoding::Ascii));
            }
            if (head.addl == 23U) {
                return emit_cbor_value(ctx, path,
                                       make_text(ctx->store->arena(),
                                                 "undefined",
                                                 TextEncoding::Ascii));
            }
            if (head.addl == 24U) {
                return emit_cbor_value(
                    ctx, path, make_u8(static_cast<uint8_t>(head.arg & 0xFFU)));
            }
            if (head.addl == 25U) {
                return emit_cbor_value(ctx, path,
                                       make_f32_bits(cbor_half_to_f32_bits(
                                           static_cast<uint16_t>(head.arg
                                                                 & 0xFFFFU))));
            }
            if (head.addl == 26U) {
                return emit_cbor_value(ctx, path,
                                       make_f32_bits(static_cast<uint32_t>(
                                           head.arg & 0xFFFFFFFFU)));
            }
            if (head.addl == 27U) {
                return emit_cbor_value(ctx, path, make_f64_bits(head.arg));
            }
            const std::string simple_text
                = "simple("
                  + std::to_string(static_cast<unsigned long long>(head.addl))
                  + ")";
            return emit_cbor_value(ctx, path,
                                   make_text(ctx->store->arena(), simple_text,
                                             TextEncoding::Ascii));
        }

        return false;
    }


    static bool decode_cbor_payload(DecodeContext* ctx,
                                    std::span<const std::byte> cbor_payload,
                                    std::string_view path_prefix) noexcept
    {
        if (!ctx) {
            return false;
        }
        uint64_t offset = 0U;
        while (offset < cbor_payload.size()) {
            if (!parse_cbor_item(ctx, cbor_payload, &offset, 0U, path_prefix)) {
                return false;
            }
        }
        return true;
    }


    static bool decode_jumbf_boxes(DecodeContext* ctx,
                                   std::span<const std::byte> bytes,
                                   uint64_t begin, uint64_t end, uint32_t depth,
                                   int32_t parent_box_index,
                                   std::string_view parent_path) noexcept
    {
        if (!ctx || !ctx->store) {
            return false;
        }

        const uint32_t max_depth = ctx->options.limits.max_box_depth;
        if (max_depth != 0U && depth > max_depth) {
            ctx->result.status = JumbfDecodeStatus::LimitExceeded;
            return false;
        }

        uint64_t offset      = begin;
        uint32_t child_index = 0U;
        while (offset < end) {
            BmffBox box;
            if (!parse_bmff_box(bytes, offset, end, &box)) {
                return false;
            }

            ctx->result.boxes_decoded += 1U;
            const uint32_t max_boxes = ctx->options.limits.max_boxes;
            if (max_boxes != 0U && ctx->result.boxes_decoded > max_boxes) {
                ctx->result.status = JumbfDecodeStatus::LimitExceeded;
                return false;
            }

            std::string box_path;
            make_child_path(parent_path, child_index, &box_path);
            child_index += 1U;

            const uint64_t payload_off  = box.offset + box.header_size;
            const uint64_t payload_size = box.size - box.header_size;
            const std::span<const std::byte> payload
                = bytes.subspan(static_cast<size_t>(payload_off),
                                static_cast<size_t>(payload_size));

            const int32_t box_index = static_cast<int32_t>(ctx->boxes.size());
            DecodeContext::ParsedBox parsed;
            parsed.type         = box.type;
            parsed.parent_index = parent_box_index;
            parsed.payload      = payload;
            ctx->boxes.push_back(parsed);

            std::string jumb_label;
            bool have_jumb_label = false;
            if (box.type == fourcc('j', 'u', 'm', 'd')) {
                if (parse_jumd_label(payload, &jumb_label)
                    && parent_box_index >= 0
                    && static_cast<size_t>(parent_box_index) < ctx->boxes.size()
                    && ctx->boxes[static_cast<size_t>(parent_box_index)].type
                           == fourcc('j', 'u', 'm', 'b')
                    && !ctx->boxes[static_cast<size_t>(parent_box_index)]
                            .has_jumb_label) {
                    have_jumb_label = true;
                    ctx->boxes[static_cast<size_t>(parent_box_index)]
                        .has_jumb_label
                        = true;
                    ctx->boxes[static_cast<size_t>(parent_box_index)].jumb_label
                        = jumb_label;
                }
            }

            std::string field_key;
            make_field_key(box_path, "type", &field_key);
            if (!emit_field_text(ctx, field_key, fourcc_to_text(box.type),
                                 EntryFlags::Derived)) {
                return false;
            }
            make_field_key(box_path, "size", &field_key);
            if (!emit_field_u64(ctx, field_key, box.size, EntryFlags::Derived)) {
                return false;
            }
            make_field_key(box_path, "payload_size", &field_key);
            if (!emit_field_u64(ctx, field_key, payload_size,
                                EntryFlags::Derived)) {
                return false;
            }
            make_field_key(box_path, "offset", &field_key);
            if (!emit_field_u64(ctx, field_key, box.offset,
                                EntryFlags::Derived)) {
                return false;
            }
            if (have_jumb_label && !parent_path.empty()) {
                make_field_key(parent_path, "jumb_label", &field_key);
                if (!emit_field_text(ctx, field_key, jumb_label,
                                     EntryFlags::Derived)) {
                    return false;
                }
            }

            if (ctx->options.detect_c2pa) {
                if (box.type == fourcc('c', '2', 'p', 'a')) {
                    if (!append_c2pa_marker(ctx, box_path)) {
                        return false;
                    }
                } else if (box.type == fourcc('j', 'u', 'm', 'd')) {
                    if (ascii_icase_contains(payload, "c2pa", 4096U)) {
                        if (!append_c2pa_marker(ctx, box_path)) {
                            return false;
                        }
                    }
                }
            }

            if (ctx->options.decode_cbor
                && box.type == fourcc('c', 'b', 'o', 'r')) {
                std::string cbor_prefix(box_path);
                cbor_prefix.append(".cbor");
                if (!decode_cbor_payload(ctx, payload, cbor_prefix)) {
                    return false;
                }
            }

            if (looks_like_bmff_sequence(bytes, payload_off,
                                         payload_off + payload_size)) {
                if (!decode_jumbf_boxes(ctx, bytes, payload_off,
                                        payload_off + payload_size, depth + 1U,
                                        box_index, box_path)) {
                    return false;
                }
            }

            offset += box.size;
            if (box.size == 0U) {
                break;
            }
        }
        return true;
    }

    struct JumbfMeasureCtx final {
        JumbfStructureEstimate result;
        JumbfDecodeLimits limits;
    };

    static bool measure_cbor_item(std::span<const std::byte> bytes, size_t* pos,
                                  uint32_t depth,
                                  JumbfMeasureCtx* ctx) noexcept;

    static bool measure_cbor_depth_ok(JumbfMeasureCtx* ctx,
                                      uint32_t depth) noexcept
    {
        if (!ctx) {
            return false;
        }
        if (depth > ctx->result.max_cbor_depth) {
            ctx->result.max_cbor_depth = depth;
        }
        const uint32_t max_depth = ctx->limits.max_cbor_depth;
        if (max_depth != 0U && depth > max_depth) {
            ctx->result.status = JumbfDecodeStatus::LimitExceeded;
            return false;
        }
        return true;
    }

    static bool measure_cbor_take_item(JumbfMeasureCtx* ctx) noexcept
    {
        if (!ctx) {
            return false;
        }
        ctx->result.cbor_items += 1U;
        const uint32_t max_items = ctx->limits.max_cbor_items;
        if (max_items != 0U && ctx->result.cbor_items > max_items) {
            ctx->result.status = JumbfDecodeStatus::LimitExceeded;
            return false;
        }
        return true;
    }

    static bool measure_cbor_item_from_head(std::span<const std::byte> bytes,
                                            size_t* pos, uint32_t depth,
                                            const CborHeadLite& head,
                                            JumbfMeasureCtx* ctx) noexcept
    {
        if (!pos || !ctx || !measure_cbor_depth_ok(ctx, depth)) {
            return false;
        }
        if (head.is_break_item) {
            return false;
        }
        if (head.major == 0U || head.major == 1U || head.major == 7U) {
            return true;
        }
        if (head.major == 2U || head.major == 3U) {
            if (!head.indefinite) {
                if (*pos > bytes.size() || head.arg > bytes.size() - *pos) {
                    return false;
                }
                *pos += static_cast<size_t>(head.arg);
                return true;
            }
            while (true) {
                if (cbor_lite_peek_break(bytes, *pos)) {
                    *pos += 1U;
                    return true;
                }
                CborHeadLite chunk;
                if (!cbor_lite_read_head(bytes, pos, &chunk)
                    || chunk.is_break_item || !measure_cbor_take_item(ctx)) {
                    return false;
                }
                if (chunk.major != head.major || chunk.indefinite) {
                    return false;
                }
                if (*pos > bytes.size() || chunk.arg > bytes.size() - *pos) {
                    return false;
                }
                *pos += static_cast<size_t>(chunk.arg);
            }
        }
        if (head.major == 4U) {
            if (!head.indefinite) {
                for (uint64_t i = 0U; i < head.arg; ++i) {
                    if (!measure_cbor_item(bytes, pos, depth + 1U, ctx)) {
                        return false;
                    }
                }
                return true;
            }
            while (true) {
                if (cbor_lite_peek_break(bytes, *pos)) {
                    *pos += 1U;
                    return true;
                }
                if (!measure_cbor_item(bytes, pos, depth + 1U, ctx)) {
                    return false;
                }
            }
        }
        if (head.major == 5U) {
            if (!head.indefinite) {
                for (uint64_t i = 0U; i < head.arg; ++i) {
                    if (!measure_cbor_item(bytes, pos, depth + 1U, ctx)
                        || !measure_cbor_item(bytes, pos, depth + 1U, ctx)) {
                        return false;
                    }
                }
                return true;
            }
            while (true) {
                if (cbor_lite_peek_break(bytes, *pos)) {
                    *pos += 1U;
                    return true;
                }
                if (!measure_cbor_item(bytes, pos, depth + 1U, ctx)
                    || !measure_cbor_item(bytes, pos, depth + 1U, ctx)) {
                    return false;
                }
            }
        }
        if (head.major == 6U) {
            if (head.indefinite) {
                return false;
            }
            return measure_cbor_item(bytes, pos, depth + 1U, ctx);
        }
        return false;
    }

    static bool measure_cbor_item(std::span<const std::byte> bytes, size_t* pos,
                                  uint32_t depth, JumbfMeasureCtx* ctx) noexcept
    {
        if (!pos || *pos > bytes.size() || !ctx
            || !measure_cbor_depth_ok(ctx, depth)) {
            return false;
        }
        CborHeadLite head;
        if (!cbor_lite_read_head(bytes, pos, &head) || head.is_break_item
            || !measure_cbor_take_item(ctx)) {
            return false;
        }
        return measure_cbor_item_from_head(bytes, pos, depth, head, ctx);
    }

    static bool measure_cbor_payload(std::span<const std::byte> payload,
                                     JumbfMeasureCtx* ctx) noexcept
    {
        if (!ctx) {
            return false;
        }
        size_t pos = 0U;
        while (pos < payload.size()) {
            if (!measure_cbor_item(payload, &pos, 0U, ctx)) {
                return false;
            }
        }
        return true;
    }

    static bool measure_jumbf_boxes(std::span<const std::byte> bytes,
                                    uint64_t begin, uint64_t end,
                                    uint32_t depth,
                                    JumbfMeasureCtx* ctx) noexcept
    {
        if (!ctx) {
            return false;
        }
        if (depth > ctx->result.max_box_depth) {
            ctx->result.max_box_depth = depth;
        }
        const uint32_t max_depth = ctx->limits.max_box_depth;
        if (max_depth != 0U && depth > max_depth) {
            ctx->result.status = JumbfDecodeStatus::LimitExceeded;
            return false;
        }

        uint64_t offset = begin;
        while (offset < end) {
            BmffBox box;
            if (!parse_bmff_box(bytes, offset, end, &box)) {
                ctx->result.status = JumbfDecodeStatus::Malformed;
                return false;
            }

            ctx->result.boxes_scanned += 1U;
            const uint32_t max_boxes = ctx->limits.max_boxes;
            if (max_boxes != 0U && ctx->result.boxes_scanned > max_boxes) {
                ctx->result.status = JumbfDecodeStatus::LimitExceeded;
                return false;
            }

            const uint64_t payload_off  = box.offset + box.header_size;
            const uint64_t payload_size = box.size - box.header_size;
            const std::span<const std::byte> payload
                = bytes.subspan(static_cast<size_t>(payload_off),
                                static_cast<size_t>(payload_size));

            if (box.type == fourcc('c', 'b', 'o', 'r')) {
                ctx->result.cbor_payloads += 1U;
                if (!measure_cbor_payload(payload, ctx)) {
                    if (ctx->result.status == JumbfDecodeStatus::Unsupported) {
                        ctx->result.status = JumbfDecodeStatus::Malformed;
                    }
                    return false;
                }
            }

            if (looks_like_bmff_sequence(bytes, payload_off,
                                         payload_off + payload_size)) {
                if (!measure_jumbf_boxes(bytes, payload_off,
                                         payload_off + payload_size, depth + 1U,
                                         ctx)) {
                    return false;
                }
            }

            offset += box.size;
            if (box.size == 0U) {
                break;
            }
        }
        return true;
    }

}  // namespace

JumbfDecodeResult
decode_jumbf_payload(std::span<const std::byte> bytes, MetaStore& store,
                     EntryFlags flags,
                     const JumbfDecodeOptions& options) noexcept
{
    JumbfDecodeResult out;
    out.status = JumbfDecodeStatus::Unsupported;

    JumbfDecodeOptions effective_options = options;
    normalize_jumbf_limits(&effective_options.limits);

    if (effective_options.limits.max_input_bytes != 0U
        && bytes.size() > effective_options.limits.max_input_bytes) {
        out.status = JumbfDecodeStatus::LimitExceeded;
        return out;
    }

    if (!looks_like_bmff_sequence(bytes, 0U, bytes.size())) {
        return out;
    }

    DecodeContext ctx;
    ctx.store         = &store;
    ctx.flags         = flags;
    ctx.input_bytes   = bytes;
    ctx.options       = effective_options;
    ctx.result.status = JumbfDecodeStatus::Ok;
    ctx.block         = store.add_block(BlockInfo {});
    if (ctx.block == kInvalidBlockId) {
        out.status = JumbfDecodeStatus::LimitExceeded;
        return out;
    }

    if (!decode_jumbf_boxes(&ctx, bytes, 0U, bytes.size(), 0U, -1,
                            std::string_view {})) {
        if (ctx.result.status == JumbfDecodeStatus::Ok) {
            ctx.result.status = JumbfDecodeStatus::Malformed;
        }
        return ctx.result;
    }

    if (!append_c2pa_semantic_fields(&ctx)) {
        if (ctx.result.status == JumbfDecodeStatus::Ok) {
            ctx.result.status = JumbfDecodeStatus::Malformed;
        }
    }
    if (!append_c2pa_verify_scaffold_fields(&ctx)) {
        if (ctx.result.status == JumbfDecodeStatus::Ok) {
            ctx.result.status = JumbfDecodeStatus::Malformed;
        }
    }

    return ctx.result;
}

JumbfDecodeResult
measure_jumbf_payload(std::span<const std::byte> bytes,
                      const JumbfDecodeOptions& options) noexcept
{
    MetaStore scratch;
    return decode_jumbf_payload(bytes, scratch, EntryFlags::None, options);
}

JumbfStructureEstimate
measure_jumbf_structure(std::span<const std::byte> bytes,
                        const JumbfDecodeLimits& limits) noexcept
{
    JumbfStructureEstimate out;
    out.status = JumbfDecodeStatus::Unsupported;

    JumbfMeasureCtx ctx;
    ctx.limits        = limits;
    ctx.result        = out;
    ctx.result.status = JumbfDecodeStatus::Ok;
    normalize_jumbf_limits(&ctx.limits);

    if (ctx.limits.max_input_bytes != 0U
        && bytes.size() > ctx.limits.max_input_bytes) {
        ctx.result.status = JumbfDecodeStatus::LimitExceeded;
        return ctx.result;
    }

    if (!looks_like_bmff_sequence(bytes, 0U, bytes.size())) {
        ctx.result.status = JumbfDecodeStatus::Unsupported;
        return ctx.result;
    }

    if (!measure_jumbf_boxes(bytes, 0U, bytes.size(), 0U, &ctx)) {
        if (ctx.result.status == JumbfDecodeStatus::Ok) {
            ctx.result.status = JumbfDecodeStatus::Malformed;
        }
    }
    return ctx.result;
}

}  // namespace openmeta
