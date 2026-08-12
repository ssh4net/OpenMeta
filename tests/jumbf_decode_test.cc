// SPDX-License-Identifier: Apache-2.0

#include "openmeta/jumbf_decode.h"

#include "openmeta/meta_key.h"
#include "openmeta/simple_meta.h"
#include "openmeta/validate.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE) \
    && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
#    include <openssl/bn.h>
#    include <openssl/ec.h>
#    include <openssl/ecdsa.h>
#    include <openssl/evp.h>
#    include <openssl/objects.h>
#    include <openssl/x509.h>
#endif

namespace openmeta {
namespace {

#if !defined(OPENMETA_ENABLE_C2PA_VERIFY)
#    define OPENMETA_ENABLE_C2PA_VERIFY 0
#endif

#if !defined(OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE)
#    define OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE 0
#endif

#if !defined(OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE)
#    define OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE 0
#endif

    static void append_u16be(std::vector<std::byte>* out, uint16_t value)
    {
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 0U) & 0xFFU) });
    }


    static void append_u32be(std::vector<std::byte>* out, uint32_t value)
    {
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 24U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 16U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 0U) & 0xFFU) });
    }


    static void append_fourcc(std::vector<std::byte>* out, uint32_t value)
    {
        append_u32be(out, value);
    }


    static void append_bytes(std::vector<std::byte>* out, std::string_view text)
    {
        for (char c : text) {
            out->push_back(std::byte { static_cast<uint8_t>(c) });
        }
    }


    static void append_cbor_head(std::vector<std::byte>* out, uint8_t major,
                                 uint64_t value)
    {
        if (value <= 23U) {
            out->push_back(
                std::byte { static_cast<uint8_t>((major << 5U) | value) });
            return;
        }
        if (value <= 0xFFU) {
            out->push_back(
                std::byte { static_cast<uint8_t>((major << 5U) | 24U) });
            out->push_back(std::byte { static_cast<uint8_t>(value) });
            return;
        }
        out->push_back(std::byte { static_cast<uint8_t>((major << 5U) | 25U) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 0U) & 0xFFU) });
    }

    static void append_cbor_i64(std::vector<std::byte>* out, int64_t value)
    {
        if (value >= 0) {
            append_cbor_head(out, 0U, static_cast<uint64_t>(value));
            return;
        }
        const uint64_t arg = static_cast<uint64_t>(-1 - value);
        append_cbor_head(out, 1U, arg);
    }


    static void append_cbor_map(std::vector<std::byte>* out, uint64_t entries)
    {
        append_cbor_head(out, 5U, entries);
    }


    static void append_cbor_array(std::vector<std::byte>* out, uint64_t count)
    {
        append_cbor_head(out, 4U, count);
    }


    static void append_cbor_text(std::vector<std::byte>* out,
                                 std::string_view text)
    {
        append_cbor_head(out, 3U, text.size());
        append_bytes(out, text);
    }


    static void append_cbor_bytes(std::vector<std::byte>* out,
                                  std::span<const std::byte> bytes)
    {
        append_cbor_head(out, 2U, bytes.size());
        out->insert(out->end(), bytes.begin(), bytes.end());
    }

    static void append_cbor_null(std::vector<std::byte>* out)
    {
        out->push_back(std::byte { 0xF6 });
    }


    static uint8_t hex_nibble(char c)
    {
        if (c >= '0' && c <= '9') {
            return static_cast<uint8_t>(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
            return static_cast<uint8_t>(10 + (c - 'a'));
        }
        if (c >= 'A' && c <= 'F') {
            return static_cast<uint8_t>(10 + (c - 'A'));
        }
        return 0xFFU;
    }


    static std::vector<std::byte> bytes_from_hex(std::string_view hex)
    {
        std::vector<std::byte> out;
        std::vector<uint8_t> nibbles;
        nibbles.reserve(hex.size());
        for (char c : hex) {
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                continue;
            }
            const uint8_t nib = hex_nibble(c);
            if (nib == 0xFFU) {
                return {};
            }
            nibbles.push_back(nib);
        }
        if ((nibbles.size() & 1U) != 0U) {
            return {};
        }
        out.reserve(nibbles.size() / 2U);
        for (size_t i = 0; i < nibbles.size(); i += 2U) {
            const uint8_t value = static_cast<uint8_t>((nibbles[i] << 4U)
                                                       | nibbles[i + 1U]);
            out.push_back(std::byte { value });
        }
        return out;
    }


    static void append_fullbox_header(std::vector<std::byte>* out,
                                      uint8_t version)
    {
        out->push_back(std::byte { version });
        out->push_back(std::byte { 0x00 });
        out->push_back(std::byte { 0x00 });
        out->push_back(std::byte { 0x00 });
    }


    static void append_bmff_box(std::vector<std::byte>* out, uint32_t type,
                                std::span<const std::byte> payload)
    {
        append_u32be(out, static_cast<uint32_t>(8U + payload.size()));
        append_fourcc(out, type);
        out->insert(out->end(), payload.begin(), payload.end());
    }

    static void append_jpeg_segment(std::vector<std::byte>* out,
                                    uint8_t marker,
                                    std::span<const std::byte> payload)
    {
        out->push_back(std::byte { 0xFF });
        out->push_back(std::byte { marker });
        const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
        append_u16be(out, seg_len);
        out->insert(out->end(), payload.begin(), payload.end());
    }

    static std::vector<std::byte>
    make_jumbf_payload_with_cbor(std::span<const std::byte> cbor_payload);


    static std::vector<std::byte> make_sample_jumbf_payload()
    {
        const std::vector<std::byte> cbor_payload = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { 0x01 },
        };
        return make_jumbf_payload_with_cbor(cbor_payload);
    }


    static std::vector<std::byte>
    make_jumbf_payload_with_cbor(std::span<const std::byte> cbor_payload)
    {
        std::vector<std::byte> jumd_payload;
        append_bytes(&jumd_payload, "c2pa");
        jumd_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> jumd_box;
        append_bmff_box(&jumd_box, fourcc('j', 'u', 'm', 'd'), jumd_payload);

        std::vector<std::byte> cbor_box;
        append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'), cbor_payload);

        std::vector<std::byte> jumb_payload;
        jumb_payload.insert(jumb_payload.end(), jumd_box.begin(),
                            jumd_box.end());
        jumb_payload.insert(jumb_payload.end(), cbor_box.begin(),
                            cbor_box.end());

        std::vector<std::byte> jumb_box;
        append_bmff_box(&jumb_box, fourcc('j', 'u', 'm', 'b'), jumb_payload);
        return jumb_box;
    }

    static std::vector<std::byte>
    make_jumb_box_with_label(std::string_view label,
                             std::span<const std::byte> payload_boxes)
    {
        std::vector<std::byte> jumd_payload;
        append_bytes(&jumd_payload, label);
        jumd_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> jumd_box;
        append_bmff_box(&jumd_box, fourcc('j', 'u', 'm', 'd'), jumd_payload);

        std::vector<std::byte> jumb_payload;
        jumb_payload.insert(jumb_payload.end(), jumd_box.begin(),
                            jumd_box.end());
        jumb_payload.insert(jumb_payload.end(), payload_boxes.begin(),
                            payload_boxes.end());

        std::vector<std::byte> jumb_box;
        append_bmff_box(&jumb_box, fourcc('j', 'u', 'm', 'b'), jumb_payload);
        return jumb_box;
    }

    static std::vector<std::byte>
    make_claim_jumb_box(std::string_view label,
                        std::span<const std::byte> claim_cbor)
    {
        std::vector<std::byte> claim_cbor_box;
        append_bmff_box(&claim_cbor_box, fourcc('c', 'b', 'o', 'r'),
                        claim_cbor);
        return make_jumb_box_with_label(
            label, std::span<const std::byte>(claim_cbor_box.data(),
                                              claim_cbor_box.size()));
    }


    static std::vector<std::byte>
    make_c2pa_verify_sample_payload(std::string_view algorithm,
                                    std::span<const std::byte> signing_input,
                                    std::span<const std::byte> signature,
                                    std::span<const std::byte> public_key_der)
    {
        std::vector<std::byte> cbor_payload;
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "manifests");
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "active_manifest");
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "claims");
        append_cbor_array(&cbor_payload, 1U);
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "signatures");
        append_cbor_array(&cbor_payload, 1U);
        append_cbor_map(&cbor_payload, 4U);
        append_cbor_text(&cbor_payload, "alg");
        append_cbor_text(&cbor_payload, algorithm);
        append_cbor_text(&cbor_payload, "signing_input");
        append_cbor_bytes(&cbor_payload, signing_input);
        append_cbor_text(&cbor_payload, "signature");
        append_cbor_bytes(&cbor_payload, signature);
        append_cbor_text(&cbor_payload, "public_key_der");
        append_cbor_bytes(&cbor_payload, public_key_der);
        return make_jumbf_payload_with_cbor(cbor_payload);
    }


    static std::vector<std::byte> make_heif_with_jumbf_item()
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('j', 'u', 'm', 'b'));
        append_bytes(&infe_payload, "manifest");
        infe_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        const std::vector<std::byte> jumbf = make_sample_jumbf_payload();
        std::vector<std::byte> idat_payload(jumbf.begin(), jumbf.end());
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload, 1);              // construction_method=1
        append_u16be(&iloc_payload, 0);              // data_reference_index
        append_u16be(&iloc_payload, 1);              // extent_count
        append_u32be(&iloc_payload, 0);              // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));

        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());
        return file;
    }

    static std::vector<std::byte>
    make_jpeg_app11_jumbf_file(std::span<const std::byte> jumbf_box)
    {
        std::vector<std::byte> seg;
        append_bytes(&seg, "JP");
        seg.push_back(std::byte { 0x00 });
        seg.push_back(std::byte { 0x00 });
        append_u32be(&seg, 1U);
        seg.insert(seg.end(), jumbf_box.begin(), jumbf_box.end());
        if (seg.size() + 2U > 0xFFFFU) {
            return {};
        }

        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });
        append_jpeg_segment(&jpeg, 0xEB, seg);
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });
        return jpeg;
    }

    static std::string temp_root()
    {
        const char* tmp = std::getenv("TMPDIR");
        if (!tmp || !*tmp) {
            tmp = std::getenv("TEMP");
        }
        if (!tmp || !*tmp) {
            tmp = std::getenv("TMP");
        }
        if (!tmp || !*tmp) {
            tmp = "/tmp";
        }
        return std::string(tmp);
    }

    static std::string make_temp_path(const char* suffix)
    {
        static uint64_t seq = 0;
        seq += 1U;

        const uint64_t now = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());

        std::string path = temp_root();
        if (!path.empty() && path.back() != '/' && path.back() != '\\') {
            path.push_back('/');
        }

        char name[160];
        std::snprintf(name, sizeof(name), "openmeta_jumbf_%llu_%llu%s",
                      static_cast<unsigned long long>(now),
                      static_cast<unsigned long long>(seq),
                      suffix ? suffix : "");
        path.append(name);
        return path;
    }

    static bool write_bytes_file(const std::string& path,
                                 std::span<const std::byte> bytes)
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            return false;
        }
        if (!bytes.empty()) {
            const size_t n = std::fwrite(bytes.data(), 1, bytes.size(), f);
            if (n != bytes.size()) {
                std::fclose(f);
                return false;
            }
        }
        return std::fclose(f) == 0;
    }

    class ScopedFile final {
    public:
        explicit ScopedFile(const std::string& path)
            : path_(path)
        {
        }

        ScopedFile(const ScopedFile&)            = delete;
        ScopedFile& operator=(const ScopedFile&) = delete;

        ~ScopedFile()
        {
            if (!path_.empty()) {
                (void)std::remove(path_.c_str());
            }
        }

    private:
        std::string path_;
    };

    static std::vector<std::byte> make_cose_protected_es256()
    {
        std::vector<std::byte> out;
        append_cbor_map(&out, 1U);
        append_cbor_head(&out, 0U, 1U);
        append_cbor_i64(&out, -7);
        return out;
    }

    static std::vector<std::byte>
    make_cose_sig_structure(std::span<const std::byte> protected_header_bytes,
                            std::span<const std::byte> payload_bytes)
    {
        std::vector<std::byte> out;
        append_cbor_array(&out, 4U);
        append_cbor_text(&out, "Signature1");
        append_cbor_bytes(&out, protected_header_bytes);
        append_cbor_bytes(&out, std::span<const std::byte> {});
        append_cbor_bytes(&out, payload_bytes);
        return out;
    }

#if defined(OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE) \
    && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    static bool make_ec_p256_keypair(EVP_PKEY** out_key)
    {
        if (!out_key) {
            return false;
        }
        *out_key = nullptr;

        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
        if (!ctx) {
            return false;
        }
        if (EVP_PKEY_keygen_init(ctx) != 1) {
            EVP_PKEY_CTX_free(ctx);
            return false;
        }
        if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1)
            != 1) {
            EVP_PKEY_CTX_free(ctx);
            return false;
        }

        EVP_PKEY* key = nullptr;
        if (EVP_PKEY_keygen(ctx, &key) != 1) {
            EVP_PKEY_CTX_free(ctx);
            return false;
        }
        EVP_PKEY_CTX_free(ctx);
        *out_key = key;
        return true;
    }

    static std::vector<std::byte> public_key_der_from_key(EVP_PKEY* key)
    {
        if (!key) {
            return {};
        }
        int len = i2d_PUBKEY(key, nullptr);
        if (len <= 0) {
            return {};
        }
        std::vector<std::byte> out(static_cast<size_t>(len));
        unsigned char* p = reinterpret_cast<unsigned char*>(out.data());
        if (i2d_PUBKEY(key, &p) != len) {
            return {};
        }
        return out;
    }

    static std::vector<std::byte> self_signed_cert_der_from_key(EVP_PKEY* key)
    {
        if (!key) {
            return {};
        }
        X509* cert = X509_new();
        if (!cert) {
            return {};
        }

        // v3 certificate (0-based: 0=v1, 1=v2, 2=v3)
        if (X509_set_version(cert, 2) != 1) {
            X509_free(cert);
            return {};
        }
        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
        X509_gmtime_adj(X509_get_notBefore(cert), 0);
        X509_gmtime_adj(X509_get_notAfter(cert), 60L * 60L * 24L * 365L);
        if (X509_set_pubkey(cert, key) != 1) {
            X509_free(cert);
            return {};
        }

        X509_NAME* name = X509_get_subject_name(cert);
        if (!name) {
            X509_free(cert);
            return {};
        }
        (void)X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                         reinterpret_cast<const unsigned char*>(
                                             "openmeta.test"),
                                         -1, -1, 0);
        if (X509_set_issuer_name(cert, name) != 1) {
            X509_free(cert);
            return {};
        }
        if (X509_sign(cert, key, EVP_sha256()) == 0) {
            X509_free(cert);
            return {};
        }

        const int len = i2d_X509(cert, nullptr);
        if (len <= 0) {
            X509_free(cert);
            return {};
        }
        std::vector<std::byte> out(static_cast<size_t>(len));
        unsigned char* p = reinterpret_cast<unsigned char*>(out.data());
        if (i2d_X509(cert, &p) != len) {
            X509_free(cert);
            return {};
        }
        X509_free(cert);
        return out;
    }

    static std::vector<std::byte>
    ecdsa_der_to_cose_raw_p256(std::span<const std::byte> der_sig)
    {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(
            der_sig.data());
        ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p,
                                       static_cast<long>(der_sig.size()));
        if (!sig) {
            return {};
        }
        const BIGNUM* r = nullptr;
        const BIGNUM* s = nullptr;
        ECDSA_SIG_get0(sig, &r, &s);
        std::vector<std::byte> raw(64U);
        const int r_ok
            = BN_bn2binpad(r, reinterpret_cast<unsigned char*>(raw.data()), 32);
        const int s_ok = BN_bn2binpad(
            s, reinterpret_cast<unsigned char*>(raw.data() + 32U), 32);
        ECDSA_SIG_free(sig);
        if (r_ok != 32 || s_ok != 32) {
            return {};
        }
        return raw;
    }

    static std::vector<std::byte>
    ecdsa_sign_sha256(EVP_PKEY* key, std::span<const std::byte> data)
    {
        if (!key) {
            return {};
        }
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        if (!mdctx) {
            return {};
        }
        if (EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, key)
            != 1) {
            EVP_MD_CTX_free(mdctx);
            return {};
        }
        if (EVP_DigestSignUpdate(mdctx, data.data(), data.size()) != 1) {
            EVP_MD_CTX_free(mdctx);
            return {};
        }
        size_t sig_len = 0U;
        if (EVP_DigestSignFinal(mdctx, nullptr, &sig_len) != 1
            || sig_len == 0U) {
            EVP_MD_CTX_free(mdctx);
            return {};
        }
        std::vector<std::byte> sig(static_cast<size_t>(sig_len));
        if (EVP_DigestSignFinal(mdctx,
                                reinterpret_cast<unsigned char*>(sig.data()),
                                &sig_len)
            != 1) {
            EVP_MD_CTX_free(mdctx);
            return {};
        }
        EVP_MD_CTX_free(mdctx);
        sig.resize(sig_len);
        return sig;
    }
#endif

    static std::string read_jumbf_field_text(const MetaStore& store,
                                             std::string_view field)
    {
        MetaKeyView key_view;
        key_view.kind                      = MetaKeyKind::JumbfField;
        key_view.data.jumbf_field.field    = field;
        const std::span<const EntryId> ids = store.find_all(key_view);
        if (ids.empty()) {
            return {};
        }
        const Entry& entry = store.entry(ids.back());
        if (entry.value.kind != MetaValueKind::Text) {
            return {};
        }
        const std::span<const std::byte> text = store.arena().span(
            entry.value.data.span);
        return std::string(reinterpret_cast<const char*>(text.data()),
                           text.size());
    }

    static uint64_t read_jumbf_field_u64(const MetaStore& store,
                                         std::string_view field)
    {
        MetaKeyView key_view;
        key_view.kind                      = MetaKeyKind::JumbfField;
        key_view.data.jumbf_field.field    = field;
        const std::span<const EntryId> ids = store.find_all(key_view);
        if (ids.empty()) {
            return 0U;
        }
        const Entry& entry = store.entry(ids.back());
        if (entry.value.kind != MetaValueKind::Scalar
            || (entry.value.elem_type != MetaElementType::U64
                && entry.value.elem_type != MetaElementType::U32
                && entry.value.elem_type != MetaElementType::U16
                && entry.value.elem_type != MetaElementType::U8)) {
            return 0U;
        }
        return entry.value.data.u64;
    }

    static bool validate_result_has_issue(const ValidateResult& result,
                                          std::string_view category,
                                          std::string_view code) noexcept
    {
        for (size_t i = 0; i < result.issues.size(); ++i) {
            const ValidateIssue& issue = result.issues[i];
            if (issue.category == category && issue.code == code) {
                return true;
            }
        }
        return false;
    }

    static int32_t
    find_signature_projection_slot_by_suffix(const MetaStore& store,
                                             std::string_view suffix)
    {
        for (uint32_t i = 0U; i < 128U; ++i) {
            std::string field("c2pa.semantic.signature.");
            field.append(std::to_string(static_cast<unsigned long long>(i)));
            field.append(".prefix");
            const std::string prefix = read_jumbf_field_text(store, field);
            if (prefix.empty()) {
                continue;
            }
            if (prefix.size() < suffix.size()) {
                continue;
            }
            const size_t off = prefix.size() - suffix.size();
            if (prefix.compare(off, suffix.size(), suffix) == 0) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    static std::vector<std::byte> make_c2pa_manifest_with_top_level_signature(
        std::string_view algorithm, std::span<const std::byte> signing_input,
        std::span<const std::byte> signature,
        std::span<const std::byte> public_key_der)
    {
        std::vector<std::byte> cbor_payload;
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "manifests");
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "active_manifest");
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "signatures");
        append_cbor_array(&cbor_payload, 1U);
        append_cbor_map(&cbor_payload, 4U);
        append_cbor_text(&cbor_payload, "alg");
        append_cbor_text(&cbor_payload, algorithm);
        append_cbor_text(&cbor_payload, "signing_input");
        append_cbor_bytes(&cbor_payload, signing_input);
        append_cbor_text(&cbor_payload, "signature");
        append_cbor_bytes(&cbor_payload, signature);
        append_cbor_text(&cbor_payload, "public_key_der");
        append_cbor_bytes(&cbor_payload, public_key_der);
        return make_jumbf_payload_with_cbor(cbor_payload);
    }

    static std::vector<std::byte> make_c2pa_manifest_with_certificate(
        std::string_view algorithm, std::span<const std::byte> signing_input,
        std::span<const std::byte> signature,
        std::span<const std::byte> certificate_der)
    {
        std::vector<std::byte> cbor_payload;
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "manifests");
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "active_manifest");
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "claims");
        append_cbor_array(&cbor_payload, 1U);
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "signatures");
        append_cbor_array(&cbor_payload, 1U);
        append_cbor_map(&cbor_payload, 4U);
        append_cbor_text(&cbor_payload, "alg");
        append_cbor_text(&cbor_payload, algorithm);
        append_cbor_text(&cbor_payload, "signing_input");
        append_cbor_bytes(&cbor_payload, signing_input);
        append_cbor_text(&cbor_payload, "signature");
        append_cbor_bytes(&cbor_payload, signature);
        append_cbor_text(&cbor_payload, "certificate_der");
        append_cbor_bytes(&cbor_payload, certificate_der);
        return make_jumbf_payload_with_cbor(cbor_payload);
    }


    static std::vector<std::byte> make_c2pa_manifest_with_key_and_certificate(
        std::string_view algorithm, std::span<const std::byte> signing_input,
        std::span<const std::byte> signature,
        std::span<const std::byte> public_key_der,
        std::span<const std::byte> certificate_der)
    {
        std::vector<std::byte> cbor_payload;
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "manifests");
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "active_manifest");
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "claims");
        append_cbor_array(&cbor_payload, 1U);
        append_cbor_map(&cbor_payload, 1U);
        append_cbor_text(&cbor_payload, "signatures");
        append_cbor_array(&cbor_payload, 1U);
        append_cbor_map(&cbor_payload, 5U);
        append_cbor_text(&cbor_payload, "alg");
        append_cbor_text(&cbor_payload, algorithm);
        append_cbor_text(&cbor_payload, "signing_input");
        append_cbor_bytes(&cbor_payload, signing_input);
        append_cbor_text(&cbor_payload, "signature");
        append_cbor_bytes(&cbor_payload, signature);
        append_cbor_text(&cbor_payload, "public_key_der");
        append_cbor_bytes(&cbor_payload, public_key_der);
        append_cbor_text(&cbor_payload, "certificate_der");
        append_cbor_bytes(&cbor_payload, certificate_der);
        return make_jumbf_payload_with_cbor(cbor_payload);
    }

}  // namespace

TEST(JumbfDecode, DecodesStructureAndCborMap)
{
    const std::vector<std::byte> payload = make_sample_jumbf_payload();

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    EXPECT_GE(result.boxes_decoded, 3U);
    EXPECT_GT(result.entries_decoded, 0U);

    store.finalize();

    MetaKeyView c2pa_key;
    c2pa_key.kind                       = MetaKeyKind::JumbfField;
    c2pa_key.data.jumbf_field.field     = "c2pa.detected";
    const std::span<const EntryId> c2pa = store.find_all(c2pa_key);
    ASSERT_EQ(c2pa.size(), 1U);
    const Entry& c2pa_entry = store.entry(c2pa[0]);
    ASSERT_EQ(c2pa_entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(c2pa_entry.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(static_cast<uint8_t>(c2pa_entry.value.data.u64), 1U);
    EXPECT_EQ(read_jumbf_field_text(store, "box.0.jumb_label"), "c2pa");

    MetaKeyView cbor_key;
    cbor_key.kind                       = MetaKeyKind::JumbfCborKey;
    cbor_key.data.jumbf_cbor_key.key    = "box.0.1.cbor.a";
    const std::span<const EntryId> cbor = store.find_all(cbor_key);
    ASSERT_EQ(cbor.size(), 1U);
    const Entry& cbor_entry = store.entry(cbor[0]);
    ASSERT_EQ(cbor_entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(cbor_entry.value.elem_type, MetaElementType::U64);
    EXPECT_EQ(cbor_entry.value.data.u64, 1U);
}

TEST(JumbfDecode, IntegratedViaSimpleMetaRead)
{
    const std::vector<std::byte> file = make_heif_with_jumbf_item();

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 16> ifds {};
    std::array<std::byte, 4096> payload {};
    std::array<uint32_t, 64> payload_parts {};
    SimpleMetaDecodeOptions options;

    const SimpleMetaResult read = simple_meta_read(file, store, blocks, ifds,
                                                   payload, payload_parts,
                                                   options);
    EXPECT_EQ(read.scan.status, ScanStatus::Ok);
    EXPECT_EQ(read.jumbf.status, JumbfDecodeStatus::Ok);
    EXPECT_GT(read.jumbf.entries_decoded, 0U);

    store.finalize();
    MetaKeyView cbor_key;
    cbor_key.kind                       = MetaKeyKind::JumbfCborKey;
    cbor_key.data.jumbf_cbor_key.key    = "box.0.1.cbor.a";
    const std::span<const EntryId> cbor = store.find_all(cbor_key);
    ASSERT_EQ(cbor.size(), 1U);
}

TEST(JumbfDecode, UnsupportedForNonBmffPayload)
{
    const std::array<std::byte, 4> bad = {
        std::byte { 0xDE },
        std::byte { 0xAD },
        std::byte { 0xBE },
        std::byte { 0xEF },
    };
    MetaStore store;
    const JumbfDecodeResult res = decode_jumbf_payload(
        std::span<const std::byte>(bad.data(), bad.size()), store);
    EXPECT_EQ(res.status, JumbfDecodeStatus::Unsupported);
}

TEST(JumbfDecode, EstimateStructureBasic)
{
    const std::vector<std::byte> payload  = make_sample_jumbf_payload();
    const JumbfStructureEstimate estimate = measure_jumbf_structure(payload);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::Ok);
    EXPECT_GE(estimate.boxes_scanned, 3U);
    EXPECT_GE(estimate.max_box_depth, 1U);
    EXPECT_EQ(estimate.cbor_payloads, 1U);
    EXPECT_GT(estimate.cbor_items, 0U);
}

TEST(JumbfDecode, EstimatePayloadMatchesDecodeCounters)
{
    const std::vector<std::byte> payload = make_sample_jumbf_payload();

    const JumbfDecodeResult estimate = measure_jumbf_payload(payload);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::Ok);
    EXPECT_GE(estimate.boxes_decoded, 3U);
    EXPECT_GT(estimate.entries_decoded, 0U);

    MetaStore store;
    const JumbfDecodeResult decoded = decode_jumbf_payload(payload, store);
    EXPECT_EQ(decoded.status, estimate.status);
    EXPECT_EQ(decoded.boxes_decoded, estimate.boxes_decoded);
    EXPECT_EQ(decoded.entries_decoded, estimate.entries_decoded);
}

TEST(JumbfDecode, EstimateStructureLimitExceededOnCborDepth)
{
    std::vector<std::byte> cbor_payload;
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_i64(&cbor_payload, 7);
    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    JumbfDecodeLimits limits;
    limits.max_cbor_depth                 = 1U;
    const JumbfStructureEstimate estimate = measure_jumbf_structure(payload,
                                                                    limits);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::LimitExceeded);
}

TEST(JumbfDecode, EstimateStructureZeroLimitsAreNormalized)
{
    const std::vector<std::byte> payload = make_sample_jumbf_payload();
    JumbfDecodeLimits limits;
    limits.max_box_depth                  = 0U;
    limits.max_boxes                      = 0U;
    limits.max_entries                    = 0U;
    limits.max_cbor_depth                 = 0U;
    limits.max_cbor_items                 = 0U;
    const JumbfStructureEstimate estimate = measure_jumbf_structure(payload,
                                                                    limits);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::Ok);
    EXPECT_GE(estimate.boxes_scanned, 3U);
}

TEST(JumbfDecode, EstimateStructureMalformedTruncatedTrailingHeader)
{
    std::vector<std::byte> bytes;
    append_u32be(&bytes, 8U);
    append_fourcc(&bytes, fourcc('f', 't', 'y', 'p'));
    bytes.push_back(std::byte { 0x00 });
    bytes.push_back(std::byte { 0x00 });
    bytes.push_back(std::byte { 0x00 });
    bytes.push_back(std::byte { 0x00 });

    const JumbfStructureEstimate estimate = measure_jumbf_structure(bytes);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::Malformed);
}

TEST(JumbfDecode, EstimateStructureMalformedInvalidSecondBoxSize)
{
    std::vector<std::byte> bytes;
    append_u32be(&bytes, 8U);
    append_fourcc(&bytes, fourcc('f', 't', 'y', 'p'));
    append_u32be(&bytes, 4U);
    append_fourcc(&bytes, fourcc('b', 'a', 'd', '!'));

    const JumbfStructureEstimate estimate = measure_jumbf_structure(bytes);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::Malformed);
}

TEST(JumbfDecode, EstimateStructureMalformedBrokenCborHead)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0x7F },  // indefinite text
        std::byte { 0x01 },  // invalid chunk head for text chunk
        std::byte { 0xFF },  // break
    };
    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    const JumbfStructureEstimate estimate = measure_jumbf_structure(payload);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::Malformed);
}

TEST(JumbfDecode, EstimateStructureMalformedInvalidIndefiniteCborSequence)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xBF },                      // indefinite map
        std::byte { 0x61 },                      // key "a"
        std::byte { 0x61 }, std::byte { 0x01 },  // value 1
        // missing break (0xFF)
    };
    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    const JumbfStructureEstimate estimate = measure_jumbf_structure(payload);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::Malformed);
}

TEST(JumbfDecode, EstimateStructureLimitBoundaryMaxBoxDepth)
{
    const std::vector<std::byte> inner = make_sample_jumbf_payload();
    std::vector<std::byte> outer;
    append_bmff_box(&outer, fourcc('r', 'o', 'o', 't'),
                    std::span<const std::byte>(inner.data(), inner.size()));

    JumbfDecodeLimits pass_limits;
    pass_limits.max_box_depth       = 2U;
    JumbfStructureEstimate estimate = measure_jumbf_structure(outer,
                                                              pass_limits);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::Ok);

    JumbfDecodeLimits fail_limits;
    fail_limits.max_box_depth = 1U;
    estimate                  = measure_jumbf_structure(outer, fail_limits);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::LimitExceeded);
}

TEST(JumbfDecode, EstimateStructureLimitBoundaryMaxBoxes)
{
    std::vector<std::byte> payload;
    append_u32be(&payload, 8U);
    append_fourcc(&payload, fourcc('a', 'a', 'a', 'a'));
    append_u32be(&payload, 8U);
    append_fourcc(&payload, fourcc('b', 'b', 'b', 'b'));

    JumbfDecodeLimits pass_limits;
    pass_limits.max_boxes           = 2U;
    JumbfStructureEstimate estimate = measure_jumbf_structure(payload,
                                                              pass_limits);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::Ok);

    JumbfDecodeLimits fail_limits;
    fail_limits.max_boxes = 1U;
    estimate              = measure_jumbf_structure(payload, fail_limits);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::LimitExceeded);
}

TEST(JumbfDecode, EstimateStructureLimitBoundaryMaxCborDepth)
{
    std::vector<std::byte> cbor_payload;
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_i64(&cbor_payload, 7);
    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    JumbfDecodeLimits pass_limits;
    pass_limits.max_cbor_depth      = 2U;
    JumbfStructureEstimate estimate = measure_jumbf_structure(payload,
                                                              pass_limits);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::Ok);

    JumbfDecodeLimits fail_limits;
    fail_limits.max_cbor_depth = 1U;
    estimate                   = measure_jumbf_structure(payload, fail_limits);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::LimitExceeded);
}

TEST(JumbfDecode, EstimateStructureLimitBoundaryMaxCborItems)
{
    std::vector<std::byte> cbor_payload;
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_i64(&cbor_payload, 9);
    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    JumbfDecodeLimits pass_limits;
    pass_limits.max_cbor_items      = 2U;
    JumbfStructureEstimate estimate = measure_jumbf_structure(payload,
                                                              pass_limits);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::Ok);

    JumbfDecodeLimits fail_limits;
    fail_limits.max_cbor_items = 1U;
    estimate                   = measure_jumbf_structure(payload, fail_limits);
    EXPECT_EQ(estimate.status, JumbfDecodeStatus::LimitExceeded);
}

TEST(JumbfDecode, CborCompositeKeyFallbackUsesStableName)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xA1 },  // map(1)
        std::byte { 0x82 },  // key: array(2)
        std::byte { 0x01 },  // key[0]
        std::byte { 0x02 },  // key[1]
        std::byte { 0x03 },  // value
    };
    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

    store.finalize();
    MetaKeyView key;
    key.kind                              = MetaKeyKind::JumbfCborKey;
    key.data.jumbf_cbor_key.key           = "box.0.1.cbor.k0_arr";
    const std::span<const EntryId> values = store.find_all(key);
    ASSERT_EQ(values.size(), 1U);
    const Entry& entry = store.entry(values[0]);
    ASSERT_EQ(entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(entry.value.elem_type, MetaElementType::U64);
    EXPECT_EQ(entry.value.data.u64, 3U);
}

TEST(JumbfDecode, CborHalfAndSimpleScalarsDecode)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xA2 },                      // map(2)
        std::byte { 0x61 },                      // text key "h"
        std::byte { 0x68 }, std::byte { 0xF9 },  // half float
        std::byte { 0x3E }, std::byte { 0x00 },  // 1.5f
        std::byte { 0x61 },                      // text key "s"
        std::byte { 0x73 }, std::byte { 0xF0 },  // simple(16)
    };
    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

    store.finalize();

    MetaKeyView half_key;
    half_key.kind                              = MetaKeyKind::JumbfCborKey;
    half_key.data.jumbf_cbor_key.key           = "box.0.1.cbor.h";
    const std::span<const EntryId> half_values = store.find_all(half_key);
    ASSERT_EQ(half_values.size(), 1U);
    const Entry& half_entry = store.entry(half_values[0]);
    ASSERT_EQ(half_entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(half_entry.value.elem_type, MetaElementType::F32);
    EXPECT_EQ(half_entry.value.data.f32_bits, 0x3FC00000U);

    MetaKeyView simple_key;
    simple_key.kind                              = MetaKeyKind::JumbfCborKey;
    simple_key.data.jumbf_cbor_key.key           = "box.0.1.cbor.s";
    const std::span<const EntryId> simple_values = store.find_all(simple_key);
    ASSERT_EQ(simple_values.size(), 1U);
    const Entry& simple_entry = store.entry(simple_values[0]);
    ASSERT_EQ(simple_entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(simple_entry.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(static_cast<uint8_t>(simple_entry.value.data.u64), 16U);
}

TEST(JumbfDecode, CborIndefiniteTextAndBytesDecode)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xA2 },                                          // map(2)
        std::byte { 0x61 },                                          // "t"
        std::byte { 0x74 }, std::byte { 0x7F },                      // text(*)
        std::byte { 0x62 },                                          // "hi"
        std::byte { 0x68 }, std::byte { 0x69 }, std::byte { 0x63 },  // "!!!"
        std::byte { 0x21 }, std::byte { 0x21 }, std::byte { 0x21 },
        std::byte { 0xFF },                      // break
        std::byte { 0x61 },                      // "b"
        std::byte { 0x62 }, std::byte { 0x5F },  // bytes(*)
        std::byte { 0x42 },                      // 0x01 0x02
        std::byte { 0x01 }, std::byte { 0x02 }, std::byte { 0x41 },  // 0x03
        std::byte { 0x03 }, std::byte { 0xFF },                      // break
    };
    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

    store.finalize();

    MetaKeyView text_key;
    text_key.kind                              = MetaKeyKind::JumbfCborKey;
    text_key.data.jumbf_cbor_key.key           = "box.0.1.cbor.t";
    const std::span<const EntryId> text_values = store.find_all(text_key);
    ASSERT_EQ(text_values.size(), 1U);
    const Entry& text_entry = store.entry(text_values[0]);
    ASSERT_EQ(text_entry.value.kind, MetaValueKind::Text);
    EXPECT_EQ(text_entry.value.text_encoding, TextEncoding::Utf8);
    const std::span<const std::byte> text_bytes = store.arena().span(
        text_entry.value.data.span);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(text_bytes.data()),
                               text_bytes.size()),
              "hi!!!");

    MetaKeyView bytes_key;
    bytes_key.kind                              = MetaKeyKind::JumbfCborKey;
    bytes_key.data.jumbf_cbor_key.key           = "box.0.1.cbor.b";
    const std::span<const EntryId> bytes_values = store.find_all(bytes_key);
    ASSERT_EQ(bytes_values.size(), 1U);
    const Entry& bytes_entry = store.entry(bytes_values[0]);
    ASSERT_EQ(bytes_entry.value.kind, MetaValueKind::Bytes);
    const std::span<const std::byte> bytes = store.arena().span(
        bytes_entry.value.data.span);
    ASSERT_EQ(bytes.size(), 3U);
    EXPECT_EQ(static_cast<uint8_t>(bytes[0]), 1U);
    EXPECT_EQ(static_cast<uint8_t>(bytes[1]), 2U);
    EXPECT_EQ(static_cast<uint8_t>(bytes[2]), 3U);
}

TEST(JumbfDecode, CborIndefiniteArrayAndMapDecode)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xA2 },  // map(2)
        std::byte { 0x63 },  // "arr"
        std::byte { 0x61 }, std::byte { 0x72 }, std::byte { 0x72 },
        std::byte { 0x9F },                                          // array(*)
        std::byte { 0x01 }, std::byte { 0x02 }, std::byte { 0xFF },  // break
        std::byte { 0x63 },                                          // "map"
        std::byte { 0x6D }, std::byte { 0x61 }, std::byte { 0x70 },
        std::byte { 0xBF },                      // map(*)
        std::byte { 0x01 },                      // key=1
        std::byte { 0x61 },                      // value="x"
        std::byte { 0x78 }, std::byte { 0xFF },  // break
    };
    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

    store.finalize();

    MetaKeyView arr0_key;
    arr0_key.kind                       = MetaKeyKind::JumbfCborKey;
    arr0_key.data.jumbf_cbor_key.key    = "box.0.1.cbor.arr[0]";
    const std::span<const EntryId> arr0 = store.find_all(arr0_key);
    ASSERT_EQ(arr0.size(), 1U);
    EXPECT_EQ(store.entry(arr0[0]).value.elem_type, MetaElementType::U64);
    EXPECT_EQ(store.entry(arr0[0]).value.data.u64, 1U);

    MetaKeyView arr1_key;
    arr1_key.kind                       = MetaKeyKind::JumbfCborKey;
    arr1_key.data.jumbf_cbor_key.key    = "box.0.1.cbor.arr[1]";
    const std::span<const EntryId> arr1 = store.find_all(arr1_key);
    ASSERT_EQ(arr1.size(), 1U);
    EXPECT_EQ(store.entry(arr1[0]).value.elem_type, MetaElementType::U64);
    EXPECT_EQ(store.entry(arr1[0]).value.data.u64, 2U);

    MetaKeyView map_key;
    map_key.kind                              = MetaKeyKind::JumbfCborKey;
    map_key.data.jumbf_cbor_key.key           = "box.0.1.cbor.map.1";
    const std::span<const EntryId> map_values = store.find_all(map_key);
    ASSERT_EQ(map_values.size(), 1U);
    const Entry& map_entry = store.entry(map_values[0]);
    ASSERT_EQ(map_entry.value.kind, MetaValueKind::Text);
    const std::span<const std::byte> map_text = store.arena().span(
        map_entry.value.data.span);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(map_text.data()),
                               map_text.size()),
              "x");
}

TEST(JumbfDecode, EmitsDraftC2paSemanticProjectionFields)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xA1 },  // map(1)
        std::byte { 0x69 },  // "manifests"
        std::byte { 0x6D }, std::byte { 0x61 },
        std::byte { 0x6E }, std::byte { 0x69 },
        std::byte { 0x66 }, std::byte { 0x65 },
        std::byte { 0x73 }, std::byte { 0x74 },
        std::byte { 0x73 }, std::byte { 0xA1 },  // map(1)
        std::byte { 0x6F },                      // "active_manifest"
        std::byte { 0x61 }, std::byte { 0x63 },
        std::byte { 0x74 }, std::byte { 0x69 },
        std::byte { 0x76 }, std::byte { 0x65 },
        std::byte { 0x5F }, std::byte { 0x6D },
        std::byte { 0x61 }, std::byte { 0x6E },
        std::byte { 0x69 }, std::byte { 0x66 },
        std::byte { 0x65 }, std::byte { 0x73 },
        std::byte { 0x74 }, std::byte { 0xA4 },  // map(4)
        std::byte { 0x6F },                      // "claim_generator"
        std::byte { 0x63 }, std::byte { 0x6C },
        std::byte { 0x61 }, std::byte { 0x69 },
        std::byte { 0x6D }, std::byte { 0x5F },
        std::byte { 0x67 }, std::byte { 0x65 },
        std::byte { 0x6E }, std::byte { 0x65 },
        std::byte { 0x72 }, std::byte { 0x61 },
        std::byte { 0x74 }, std::byte { 0x6F },
        std::byte { 0x72 }, std::byte { 0x68 },  // "OpenMeta"
        std::byte { 0x4F }, std::byte { 0x70 },
        std::byte { 0x65 }, std::byte { 0x6E },
        std::byte { 0x4D }, std::byte { 0x65 },
        std::byte { 0x74 }, std::byte { 0x61 },
        std::byte { 0x6A },  // "assertions"
        std::byte { 0x61 }, std::byte { 0x73 },
        std::byte { 0x73 }, std::byte { 0x65 },
        std::byte { 0x72 }, std::byte { 0x74 },
        std::byte { 0x69 }, std::byte { 0x6F },
        std::byte { 0x6E }, std::byte { 0x73 },
        std::byte { 0x82 },  // [1,2]
        std::byte { 0x01 }, std::byte { 0x02 },
        std::byte { 0x69 },  // "signature"
        std::byte { 0x73 }, std::byte { 0x69 },
        std::byte { 0x67 }, std::byte { 0x6E },
        std::byte { 0x61 }, std::byte { 0x74 },
        std::byte { 0x75 }, std::byte { 0x72 },
        std::byte { 0x65 }, std::byte { 0x62 },  // "ok"
        std::byte { 0x6F }, std::byte { 0x6B },
        std::byte { 0x65 },  // "claim"
        std::byte { 0x63 }, std::byte { 0x6C },
        std::byte { 0x61 }, std::byte { 0x69 },
        std::byte { 0x6D }, std::byte { 0x64 },  // "test"
        std::byte { 0x74 }, std::byte { 0x65 },
        std::byte { 0x73 }, std::byte { 0x74 },
    };
    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

    store.finalize();

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    EXPECT_EQ(read_u8_field("c2pa.detected"), 1U);
    EXPECT_EQ(read_u8_field("c2pa.semantic.manifest_present"), 1U);
    EXPECT_EQ(read_jumbf_field_u64(store,
                                   "c2pa.semantic.active_manifest_present"),
              1U);
    EXPECT_EQ(read_u8_field("c2pa.semantic.claim_present"), 1U);
    EXPECT_EQ(read_u8_field("c2pa.semantic.assertion_present"), 1U);
    EXPECT_EQ(read_u8_field("c2pa.semantic.signature_present"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.active_manifest_count"), 1U);
    EXPECT_EQ(read_jumbf_field_text(store,
                                    "c2pa.semantic.active_manifest.prefix"),
              "box.0.1.cbor.manifests.active_manifest");
    EXPECT_GE(read_u64_field("c2pa.semantic.cbor_key_count"), 5U);
    EXPECT_GE(read_u64_field("c2pa.semantic.assertion_key_hits"), 1U);

    MetaKeyView cg_key;
    cg_key.kind                           = MetaKeyKind::JumbfField;
    cg_key.data.jumbf_field.field         = "c2pa.semantic.claim_generator";
    const std::span<const EntryId> cg_ids = store.find_all(cg_key);
    ASSERT_EQ(cg_ids.size(), 1U);
    const Entry& cg = store.entry(cg_ids[0]);
    ASSERT_EQ(cg.value.kind, MetaValueKind::Text);
    const std::span<const std::byte> cg_text = store.arena().span(
        cg.value.data.span);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(cg_text.data()),
                               cg_text.size()),
              "OpenMeta");
}

TEST(JumbfDecode, EmitsDraftC2paPerClaimProjectionFields)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xA1 },  // map(1)
        std::byte { 0x69 },  // "manifests"
        std::byte { 0x6D }, std::byte { 0x61 },
        std::byte { 0x6E }, std::byte { 0x69 },
        std::byte { 0x66 }, std::byte { 0x65 },
        std::byte { 0x73 }, std::byte { 0x74 },
        std::byte { 0x73 }, std::byte { 0xA1 },  // map(1)
        std::byte { 0x6F },                      // "active_manifest"
        std::byte { 0x61 }, std::byte { 0x63 },
        std::byte { 0x74 }, std::byte { 0x69 },
        std::byte { 0x76 }, std::byte { 0x65 },
        std::byte { 0x5F }, std::byte { 0x6D },
        std::byte { 0x61 }, std::byte { 0x6E },
        std::byte { 0x69 }, std::byte { 0x66 },
        std::byte { 0x65 }, std::byte { 0x73 },
        std::byte { 0x74 }, std::byte { 0xA1 },  // map(1)
        std::byte { 0x66 },                      // "claims"
        std::byte { 0x63 }, std::byte { 0x6C },
        std::byte { 0x61 }, std::byte { 0x69 },
        std::byte { 0x6D }, std::byte { 0x73 },
        std::byte { 0x82 },  // array(2) claims
        std::byte { 0xA4 },  // claim[0] map(4)
        std::byte { 0x6F },  // "claim_generator"
        std::byte { 0x63 }, std::byte { 0x6C },
        std::byte { 0x61 }, std::byte { 0x69 },
        std::byte { 0x6D }, std::byte { 0x5F },
        std::byte { 0x67 }, std::byte { 0x65 },
        std::byte { 0x6E }, std::byte { 0x65 },
        std::byte { 0x72 }, std::byte { 0x61 },
        std::byte { 0x74 }, std::byte { 0x6F },
        std::byte { 0x72 }, std::byte { 0x68 },  // "OpenMeta"
        std::byte { 0x4F }, std::byte { 0x70 },
        std::byte { 0x65 }, std::byte { 0x6E },
        std::byte { 0x4D }, std::byte { 0x65 },
        std::byte { 0x74 }, std::byte { 0x61 },
        std::byte { 0x6A },  // "assertions"
        std::byte { 0x61 }, std::byte { 0x73 },
        std::byte { 0x73 }, std::byte { 0x65 },
        std::byte { 0x72 }, std::byte { 0x74 },
        std::byte { 0x69 }, std::byte { 0x6F },
        std::byte { 0x6E }, std::byte { 0x73 },
        std::byte { 0x82 },  // [1,2]
        std::byte { 0x01 }, std::byte { 0x02 },
        std::byte { 0x69 },  // "signature"
        std::byte { 0x73 }, std::byte { 0x69 },
        std::byte { 0x67 }, std::byte { 0x6E },
        std::byte { 0x61 }, std::byte { 0x74 },
        std::byte { 0x75 }, std::byte { 0x72 },
        std::byte { 0x65 }, std::byte { 0x63 },  // "sig"
        std::byte { 0x73 }, std::byte { 0x69 },
        std::byte { 0x67 }, std::byte { 0x6A },  // "signatures"
        std::byte { 0x73 }, std::byte { 0x69 },
        std::byte { 0x67 }, std::byte { 0x6E },
        std::byte { 0x61 }, std::byte { 0x74 },
        std::byte { 0x75 }, std::byte { 0x72 },
        std::byte { 0x65 }, std::byte { 0x73 },
        std::byte { 0x81 },  // array(1)
        std::byte { 0xA1 },  // map(1)
        std::byte { 0x63 },  // "alg"
        std::byte { 0x61 }, std::byte { 0x6C },
        std::byte { 0x67 }, std::byte { 0x65 },  // "es256"
        std::byte { 0x65 }, std::byte { 0x73 },
        std::byte { 0x32 }, std::byte { 0x35 },
        std::byte { 0x36 }, std::byte { 0xA2 },  // claim[1] map(2)
        std::byte { 0x6F },                      // "claim_generator"
        std::byte { 0x63 }, std::byte { 0x6C },
        std::byte { 0x61 }, std::byte { 0x69 },
        std::byte { 0x6D }, std::byte { 0x5F },
        std::byte { 0x67 }, std::byte { 0x65 },
        std::byte { 0x6E }, std::byte { 0x65 },
        std::byte { 0x72 }, std::byte { 0x61 },
        std::byte { 0x74 }, std::byte { 0x6F },
        std::byte { 0x72 }, std::byte { 0x65 },  // "Other"
        std::byte { 0x4F }, std::byte { 0x74 },
        std::byte { 0x68 }, std::byte { 0x65 },
        std::byte { 0x72 }, std::byte { 0x6A },  // "assertions"
        std::byte { 0x61 }, std::byte { 0x73 },
        std::byte { 0x73 }, std::byte { 0x65 },
        std::byte { 0x72 }, std::byte { 0x74 },
        std::byte { 0x69 }, std::byte { 0x6F },
        std::byte { 0x6E }, std::byte { 0x73 },
        std::byte { 0x81 },  // [3]
        std::byte { 0x03 },
    };
    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_text_field = [&](std::string_view field_name) -> std::string {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> text = store.arena().span(
            e.value.data.span);
        return std::string(reinterpret_cast<const char*>(text.data()),
                           text.size());
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.claim_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.assertion_count"), 3U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature_count"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature_linked_count"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature_orphan_count"), 0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest_count"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.active_manifest_count"), 1U);
    EXPECT_EQ(read_jumbf_field_u64(store,
                                   "c2pa.semantic.active_manifest_present"),
              1U);
    EXPECT_EQ(read_text_field("c2pa.semantic.active_manifest.prefix"),
              "box.0.1.cbor.manifests.active_manifest");
    EXPECT_GE(read_u64_field("c2pa.semantic.signature_key_hits"), 2U);

    EXPECT_EQ(read_text_field("c2pa.semantic.manifest.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest");
    EXPECT_EQ(read_jumbf_field_u64(store, "c2pa.semantic.manifest.0.is_active"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.claim_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.assertion_count"), 3U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.signature_count"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.signature_linked_count"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.signature_orphan_count"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.cross_claim_link_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0.explicit_reference_signature_count"),
              0U);

    EXPECT_EQ(read_u64_field("c2pa.semantic.claim.0.assertion_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.claim.1.assertion_count"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.claim.0.signature_count"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.claim.1.signature_count"), 0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.claim.0.signature_key_hits"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.claim.1.signature_key_hits"), 0U);

    EXPECT_EQ(read_u64_field("c2pa.semantic.claim.0.assertion.0.key_hits"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.claim.0.assertion.1.key_hits"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.claim.1.assertion.0.key_hits"), 1U);

    EXPECT_EQ(read_text_field("c2pa.semantic.claim.0.claim_generator"),
              "OpenMeta");
    EXPECT_EQ(read_text_field("c2pa.semantic.claim.1.claim_generator"),
              "Other");

    EXPECT_EQ(read_text_field("c2pa.semantic.claim.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[0]");
    EXPECT_EQ(read_text_field("c2pa.semantic.claim.1.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[1]");

    EXPECT_EQ(read_text_field("c2pa.semantic.claim.0.assertion.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[0].assertions[0]");
    EXPECT_EQ(read_text_field("c2pa.semantic.claim.0.assertion.1.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[0].assertions[1]");
    EXPECT_EQ(read_text_field("c2pa.semantic.claim.1.assertion.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[1].assertions[0]");
    EXPECT_EQ(read_text_field("c2pa.semantic.claim.0.signature.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[0].signatures[0]");
    EXPECT_EQ(read_u64_field("c2pa.semantic.claim.0.signature.0.key_hits"), 1U);
    EXPECT_EQ(read_text_field("c2pa.semantic.claim.0.signature.0.algorithm"),
              "es256");
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[0].signatures[0]");
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.0.key_hits"), 1U);
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.algorithm"), "es256");

    MetaKeyView missing_claim_signature;
    missing_claim_signature.kind = MetaKeyKind::JumbfField;
    missing_claim_signature.data.jumbf_field.field
        = "c2pa.semantic.claim.1.signature.0.prefix";
    EXPECT_TRUE(store.find_all(missing_claim_signature).empty());
}

TEST(JumbfDecode, EmitsDraftC2paIngredientProjectionFields)
{
    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "ingredients");
    append_cbor_array(&cbor_payload, 2U);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "title");
    append_cbor_text(&cbor_payload, "base");
    append_cbor_text(&cbor_payload, "relationship");
    append_cbor_text(&cbor_payload, "parentOf");
    append_cbor_text(&cbor_payload, "thumbnailUrl");
    append_cbor_text(&cbor_payload, "https://example.invalid/base.jpg");
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "dc:title");
    append_cbor_text(&cbor_payload, "edit");
    append_cbor_text(&cbor_payload, "relationship");
    append_cbor_text(&cbor_payload, "componentOf");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "ingredients");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "title");
    append_cbor_text(&cbor_payload, "parent");

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                   = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                   = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    auto read_text_field = [&](std::string_view field_name) -> std::string {
        MetaKeyView key;
        key.kind                   = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        if (ids.size() != 1U) {
            std::string missing("missing:");
            missing.append(field_name.data(), field_name.size());
            return missing;
        }
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> text = store.arena().span(
            e.value.data.span);
        return std::string(reinterpret_cast<const char*>(text.data()),
                           text.size());
    };

    EXPECT_EQ(read_jumbf_field_u64(store,
                                   "c2pa.semantic.active_manifest_present"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.claim_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.ingredient_count"), 3U);
    EXPECT_GE(read_u64_field("c2pa.semantic.ingredient_key_hits"), 3U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.ingredient_relationship_count"),
              2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.ingredient_thumbnail_url_count"),
              1U);
    EXPECT_EQ(
        read_u64_field("c2pa.semantic.ingredient_relationship_kind_count"),
        2U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.ingredient_relationship.parentOf_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.ingredient_relationship.componentOf_count"),
        1U);

    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.claim_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.ingredient_count"), 3U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0.ingredient_relationship_count"),
        2U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0.ingredient_thumbnail_url_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0.ingredient_relationship_kind_count"),
        2U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0.ingredient_relationship.parentOf_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0.ingredient_relationship.componentOf_count"),
        1U);

    EXPECT_EQ(read_u64_field("c2pa.semantic.claim.0.ingredient_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.claim.1.ingredient_count"), 1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0.ingredient_relationship_count"),
        2U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0.ingredient_thumbnail_url_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0.ingredient_relationship_kind_count"),
        2U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0.ingredient_relationship.parentOf_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0.ingredient_relationship.componentOf_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1.ingredient_relationship_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1.ingredient_thumbnail_url_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1.ingredient_relationship_kind_count"),
        0U);

    EXPECT_EQ(
        read_text_field("c2pa.semantic.claim.0.ingredient.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0].ingredients[0]");
    EXPECT_EQ(
        read_text_field("c2pa.semantic.claim.0.ingredient.1.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0].ingredients[1]");
    EXPECT_EQ(
        read_text_field("c2pa.semantic.claim.1.ingredient.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[1].ingredients[0]");

    EXPECT_GE(read_u64_field("c2pa.semantic.claim.0.ingredient.0.key_hits"), 1U);
    EXPECT_GE(read_u64_field("c2pa.semantic.claim.0.ingredient.1.key_hits"), 1U);
    EXPECT_GE(read_u64_field("c2pa.semantic.claim.1.ingredient.0.key_hits"), 1U);

    EXPECT_EQ(read_text_field("c2pa.semantic.claim.0.ingredient.0.title"),
              "base");
    EXPECT_EQ(
        read_text_field("c2pa.semantic.claim.0.ingredient.0.relationship"),
        "parentOf");
    EXPECT_EQ(
        read_text_field("c2pa.semantic.claim.0.ingredient.0.thumbnail_url"),
        "https://example.invalid/base.jpg");
    EXPECT_EQ(read_text_field("c2pa.semantic.claim.0.ingredient.1.title"),
              "edit");
    EXPECT_EQ(
        read_text_field("c2pa.semantic.claim.0.ingredient.1.relationship"),
        "componentOf");
    EXPECT_EQ(read_text_field("c2pa.semantic.claim.1.ingredient.0.title"),
              "parent");

    MetaKeyView missing_thumbnail_key;
    missing_thumbnail_key.kind = MetaKeyKind::JumbfField;
    missing_thumbnail_key.data.jumbf_field.field
        = "c2pa.semantic.claim.1.ingredient.0.thumbnail_url";
    EXPECT_TRUE(store.find_all(missing_thumbnail_key).empty());
}

TEST(JumbfDecode, EmitsDraftC2paReferenceLinkedProjectionFields)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x02 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_i64(&cbor_payload, 1);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_i64(&cbor_payload, 0);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    auto read_text_field = [&](std::string_view field_name) -> std::string {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> text = store.arena().span(
            e.value.data.span);
        return std::string(reinterpret_cast<const char*>(text.data()),
                           text.size());
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.claim_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature_linked_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature_orphan_count"), 0U);

    EXPECT_EQ(read_u64_field("c2pa.semantic.reference_key_hits"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.cross_claim_link_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_signature_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              0U);

    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.claim.0.referenced_by_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.claim.1.referenced_by_signature_count"),
              1U);

    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.0.reference_key_hits"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.1.reference_key_hits"),
              1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_present"),
              1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.1.explicit_reference_present"),
              1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.1.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_unresolved"),
              0U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.1.explicit_reference_unresolved"),
              0U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
              0U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.1.explicit_reference_ambiguous"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.0.linked_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.1.linked_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.0.cross_claim_link_count"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.1.cross_claim_link_count"),
              1U);

    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.linked_claim.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[1]");
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.1.linked_claim.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[0]");
}

TEST(JumbfDecode, EmitsDraftC2paIngredientClaimTopologyFields)
{
    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 3U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "ingredients");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "title");
    append_cbor_text(&cbor_payload, "base");
    append_cbor_text(&cbor_payload, "thumbnailUrl");
    append_cbor_text(&cbor_payload, "https://example.test/base");
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "ingredients");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "title");
    append_cbor_text(&cbor_payload, "parent");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                   = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                   = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.claim_count"), 3U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.ingredient_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.ingredient_claim_count"), 2U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.ingredient_claim_with_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.ingredient_claim_referenced_by_signature_count"),
        1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.ingredient_manifest_count"), 1U);

    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.claim_count"), 3U);
    EXPECT_EQ(
        read_u64_field("c2pa.semantic.manifest.0.ingredient_claim_count"),
        2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_claim_with_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_claim_referenced_by_signature_count"),
              1U);

    EXPECT_EQ(
        read_u64_field("c2pa.semantic.claim.0.referenced_by_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field("c2pa.semantic.claim.1.referenced_by_signature_count"),
        0U);
}

TEST(JumbfDecode, EmitsDraftC2paIngredientSignatureTopologyFields)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x02 },
    };
    const std::array<std::byte, 4U> claim2 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x63 },
        std::byte { 0x03 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 3U);

    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "ingredients");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "title");
    append_cbor_text(&cbor_payload, "base");
    append_cbor_text(&cbor_payload, "thumbnailUrl");
    append_cbor_text(&cbor_payload, "https://example.test/base");
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_i64(&cbor_payload, 0);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim2.data(), claim2.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                   = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                   = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.claim_count"), 3U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature_count"), 3U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature_linked_count"), 3U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.ingredient_claim_count"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.ingredient_signature_count"), 1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.ingredient_linked_signature_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.ingredient_linked_direct_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.ingredient_linked_cross_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_cross_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_direct_title_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_cross_title_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_direct_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_cross_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_direct_thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_cross_thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.ingredient_linked_signature_title_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_thumbnail_url_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_explicit_reference_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "title_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.ingredient_linked_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.ingredient_linked_claim_direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.ingredient_linked_claim_cross_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.ingredient_linked_claim_mixed_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_direct_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_cross_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_unresolved_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_unresolved_"
                  "direct_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_unresolved_"
                  "cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_unresolved_"
                  "mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_ambiguous_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_ambiguous_"
                  "direct_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_ambiguous_"
                  "cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_ambiguous_"
                  "mixed_source_count"),
              0U);

    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.signature_count"), 3U);
    EXPECT_EQ(
        read_u64_field("c2pa.semantic.manifest.0.ingredient_signature_count"),
        1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_direct_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_cross_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_cross_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_direct_title_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_cross_title_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_direct_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_cross_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_direct_thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_cross_thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_title_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_thumbnail_url_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_explicit_reference_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "title_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0.ingredient_linked_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_cross_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_mixed_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_direct_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_cross_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_unresolved_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_unresolved_"
                  "direct_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_unresolved_"
                  "cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_unresolved_"
                  "mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_ambiguous_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_ambiguous_"
                  "direct_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_ambiguous_"
                  "cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_ambiguous_"
                  "mixed_source_count"),
              0U);

    EXPECT_EQ(
        read_u64_field("c2pa.semantic.claim.0.referenced_by_signature_count"),
        2U);
    EXPECT_EQ(
        read_u64_field("c2pa.semantic.claim.1.referenced_by_signature_count"),
        0U);
    EXPECT_EQ(
        read_u64_field("c2pa.semantic.claim.2.referenced_by_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field("c2pa.semantic.claim.0.linked_ingredient_signature_count"),
        2U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0.linked_direct_ingredient_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0.linked_cross_ingredient_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field("c2pa.semantic.claim.0.linked_ingredient_title_count"),
        2U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0.linked_ingredient_relationship_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0.linked_ingredient_thumbnail_url_count"),
        2U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0."
            "linked_ingredient_explicit_reference_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0."
            "linked_ingredient_explicit_reference_direct_signature_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0."
            "linked_ingredient_explicit_reference_cross_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0."
            "linked_ingredient_explicit_reference_title_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0."
            "linked_ingredient_explicit_reference_relationship_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.0."
            "linked_ingredient_explicit_reference_thumbnail_url_count"),
        1U);
    EXPECT_EQ(
        read_u64_field("c2pa.semantic.claim.1.linked_ingredient_signature_count"),
        0U);
    EXPECT_EQ(
        read_u64_field("c2pa.semantic.claim.2.linked_ingredient_signature_count"),
        0U);

    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.linked_ingredient_claim_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.linked_direct_ingredient_claim_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.linked_cross_ingredient_claim_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.1.linked_ingredient_claim_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.1.linked_direct_ingredient_claim_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.1.linked_cross_ingredient_claim_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.2.linked_ingredient_claim_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.2.linked_direct_ingredient_claim_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.2.linked_cross_ingredient_claim_count"),
        0U);
    EXPECT_EQ(
        read_u8_field(
            "c2pa.semantic.signature.0.direct_claim_has_ingredients"),
        1U);
    EXPECT_EQ(
        read_u8_field(
            "c2pa.semantic.signature.1.direct_claim_has_ingredients"),
        0U);
    EXPECT_EQ(
        read_u8_field(
            "c2pa.semantic.signature.2.direct_claim_has_ingredients"),
        0U);
}

TEST(JumbfDecode, EmitsDraftC2paIngredientSignatureRelationshipKindFields)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x02 },
    };
    const std::array<std::byte, 4U> claim2 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x63 },
        std::byte { 0x03 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 3U);

    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "ingredients");
    append_cbor_array(&cbor_payload, 2U);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "relationship");
    append_cbor_text(&cbor_payload, "parentOf");
    append_cbor_text(&cbor_payload, "title");
    append_cbor_text(&cbor_payload, "Ingredient 0");
    append_cbor_text(&cbor_payload, "thumbnailUrl");
    append_cbor_text(&cbor_payload, "https://example.test/thumb0");
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "relationship");
    append_cbor_text(&cbor_payload, "componentOf");
    append_cbor_text(&cbor_payload, "title");
    append_cbor_text(&cbor_payload, "Ingredient 1");
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 0U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_i64(&cbor_payload, 0);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim2.data(), claim2.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                   = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.0."
                  "linked_ingredient_title_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.0."
                  "linked_ingredient_relationship_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.0."
                  "linked_ingredient_relationship_kind_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.0."
                  "linked_ingredient_relationship.parentOf_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.0."
                  "linked_ingredient_relationship.componentOf_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.0."
                  "linked_ingredient_thumbnail_url_count"),
              1U);

    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.1."
                  "linked_ingredient_title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.1."
                  "linked_ingredient_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.1."
                  "linked_ingredient_relationship_kind_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.1."
                  "linked_ingredient_thumbnail_url_count"),
              0U);

    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_relationship_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_relationship_kind_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_relationship.parentOf_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_relationship.componentOf_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_direct_title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_cross_title_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_direct_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_cross_relationship_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_direct_thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_cross_thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "title_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "relationship_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "thumbnail_url_count"),
              1U);

    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_relationship_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_relationship_kind_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_relationship.parentOf_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_relationship.componentOf_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_direct_title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_cross_title_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_direct_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_cross_relationship_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_direct_thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_cross_thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "title_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "relationship_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.ingredient_linked_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_unresolved_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_ambiguous_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0.ingredient_linked_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_unresolved_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_ambiguous_count"),
              0U);

    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.claim.0."
                  "linked_ingredient_relationship_kind_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.claim.0."
                  "linked_ingredient_relationship.parentOf_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.claim.0."
                  "linked_ingredient_relationship.componentOf_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.claim.0."
                  "linked_ingredient_explicit_reference_relationship_kind_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.claim.0."
                  "linked_ingredient_explicit_reference_relationship."
                  "parentOf_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.claim.0."
                  "linked_ingredient_explicit_reference_relationship."
                  "componentOf_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.claim.0."
                  "linked_ingredient_explicit_reference_unresolved_"
                  "relationship_kind_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.claim.0."
                  "linked_ingredient_explicit_reference_ambiguous_"
                  "relationship_kind_count"),
              0U);
}

TEST(JumbfDecode,
     EmitsDraftC2paIngredientExplicitReferenceStatusTopologyFields)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x02 },
    };
    const std::array<std::byte, 4U> claim2 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x63 },
        std::byte { 0x03 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 3U);

    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "ingredients");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "title");
    append_cbor_text(&cbor_payload, "base");
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref_id");
    append_cbor_i64(&cbor_payload, 999);

    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));
    append_cbor_text(&cbor_payload, "ingredients");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "title");
    append_cbor_text(&cbor_payload, "parent");
    append_cbor_text(&cbor_payload, "relationship");
    append_cbor_text(&cbor_payload, "componentOf");
    append_cbor_text(&cbor_payload, "thumbnailUrl");
    append_cbor_text(&cbor_payload, "https://example.test/parent");
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_id");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "reference");
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "id");
    append_cbor_i64(&cbor_payload, 2);
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload, "https://example.test/asset?jumbf="
                                    "c2pa.claim.missing0");
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.missing0");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim2.data(), claim2.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                   = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                   = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_signature_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              1U);

    EXPECT_EQ(read_u64_field("c2pa.semantic.ingredient_signature_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.ingredient_linked_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.ingredient_linked_claim_direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.ingredient_linked_claim_cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.ingredient_linked_claim_mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_unresolved_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_unresolved_"
                  "direct_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_unresolved_"
                  "cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_unresolved_"
                  "mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_ambiguous_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_ambiguous_"
                  "direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_ambiguous_"
                  "cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_claim_explicit_reference_ambiguous_"
                  "mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_explicit_reference_unresolved_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_explicit_reference_ambiguous_signature_count"),
              1U);

    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.ingredient_signature_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0.ingredient_linked_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_unresolved_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_unresolved_"
                  "direct_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_unresolved_"
                  "cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_unresolved_"
                  "mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_ambiguous_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_ambiguous_"
                  "direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_ambiguous_"
                  "cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_claim_explicit_reference_ambiguous_"
                  "mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_explicit_reference_unresolved_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_explicit_reference_ambiguous_signature_count"),
              1U);

    EXPECT_EQ(
        read_u64_field("c2pa.semantic.claim.0.linked_ingredient_signature_count"),
        0U);
    EXPECT_EQ(
        read_u64_field("c2pa.semantic.claim.1.linked_ingredient_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1.linked_direct_ingredient_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1.linked_cross_ingredient_signature_count"),
        0U);
    EXPECT_EQ(
        read_u64_field("c2pa.semantic.claim.1.linked_ingredient_title_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1.linked_ingredient_relationship_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1.linked_ingredient_thumbnail_url_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_direct_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_cross_signature_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_title_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_relationship_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_thumbnail_url_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_relationship_kind_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_relationship.componentOf_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_relationship_kind_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_relationship."
            "componentOf_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_unresolved_signature_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_unresolved_direct_signature_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_unresolved_cross_signature_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_unresolved_title_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_unresolved_relationship_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_unresolved_thumbnail_url_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_unresolved_"
            "relationship_kind_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_ambiguous_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_ambiguous_direct_signature_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_ambiguous_cross_signature_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_ambiguous_title_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_ambiguous_relationship_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_ambiguous_thumbnail_url_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_ambiguous_"
            "relationship_kind_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.claim.1."
            "linked_ingredient_explicit_reference_ambiguous_relationship."
            "componentOf_count"),
        1U);

    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_direct_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_cross_claim_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "title_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "relationship_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_title_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "direct_title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "cross_title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "direct_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "cross_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "direct_thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "cross_thumbnail_url_count"),
              0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic."
            "ingredient_linked_signature_explicit_reference_relationship_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic."
            "ingredient_linked_signature_explicit_reference_relationship_kind_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic."
            "ingredient_linked_signature_explicit_reference_relationship."
            "componentOf_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic."
            "ingredient_linked_signature_explicit_reference_thumbnail_url_count"),
        1U);

    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "direct_claim_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "cross_claim_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "direct_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "title_count"),
              0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic."
            "ingredient_linked_signature_explicit_reference_unresolved_"
            "relationship_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic."
            "ingredient_linked_signature_explicit_reference_unresolved_"
            "relationship_kind_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic."
            "ingredient_linked_signature_explicit_reference_unresolved_"
            "thumbnail_url_count"),
        0U);

    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "direct_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "cross_claim_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "direct_title_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "cross_title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "direct_relationship_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "cross_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "direct_thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "cross_thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "title_count"),
              1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic."
            "ingredient_linked_signature_explicit_reference_ambiguous_"
            "relationship_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic."
            "ingredient_linked_signature_explicit_reference_ambiguous_"
            "relationship_kind_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic."
            "ingredient_linked_signature_explicit_reference_ambiguous_"
            "relationship.componentOf_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic."
            "ingredient_linked_signature_explicit_reference_ambiguous_"
            "thumbnail_url_count"),
        1U);

    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "claim_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_mixed_"
                  "source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "title_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "relationship_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_direct_"
                  "thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_cross_"
                  "thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_title_count"),
              1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0."
            "ingredient_linked_signature_explicit_reference_relationship_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0."
            "ingredient_linked_signature_explicit_reference_relationship_kind_"
            "count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0."
            "ingredient_linked_signature_explicit_reference_relationship."
            "componentOf_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0."
            "ingredient_linked_signature_explicit_reference_"
            "thumbnail_url_count"),
        1U);

    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "direct_claim_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "cross_claim_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "direct_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "direct_title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "cross_title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "direct_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "cross_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "direct_thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "cross_thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_unresolved_"
                  "title_count"),
              0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0."
            "ingredient_linked_signature_explicit_reference_unresolved_"
            "relationship_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0."
            "ingredient_linked_signature_explicit_reference_unresolved_"
            "relationship_kind_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0."
            "ingredient_linked_signature_explicit_reference_unresolved_"
            "thumbnail_url_count"),
        0U);

    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "direct_claim_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "cross_claim_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "direct_source_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "cross_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "mixed_source_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "direct_title_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "cross_title_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "direct_relationship_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "cross_relationship_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "direct_thumbnail_url_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "cross_thumbnail_url_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0."
                  "ingredient_linked_signature_explicit_reference_ambiguous_"
                  "title_count"),
              1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0."
            "ingredient_linked_signature_explicit_reference_ambiguous_"
            "relationship_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0."
            "ingredient_linked_signature_explicit_reference_ambiguous_"
            "relationship_kind_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0."
            "ingredient_linked_signature_explicit_reference_ambiguous_"
            "relationship.componentOf_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.manifest.0."
            "ingredient_linked_signature_explicit_reference_ambiguous_"
            "thumbnail_url_count"),
        1U);

    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_unresolved"),
              1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.1.explicit_reference_ambiguous"),
              1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.linked_ingredient_claim_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.1.linked_ingredient_claim_count"),
        1U);
    EXPECT_EQ(
        read_u8_field(
            "c2pa.semantic.signature.0.direct_claim_has_ingredients"),
        1U);
    EXPECT_EQ(
        read_u8_field(
            "c2pa.semantic.signature.1.direct_claim_has_ingredients"),
        1U);
}

TEST(JumbfDecode, EmitsDraftC2paPerManifestProjectionFields)
{
    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 2U);

    append_cbor_text(&cbor_payload, "manifest0");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "assertions");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");

    append_cbor_text(&cbor_payload, "manifest1");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "assertions");
    append_cbor_array(&cbor_payload, 2U);
    append_cbor_i64(&cbor_payload, 2);
    append_cbor_i64(&cbor_payload, 3);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    auto read_text_field = [&](std::string_view field_name) -> std::string {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> text = store.arena().span(
            e.value.data.span);
        return std::string(reinterpret_cast<const char*>(text.data()),
                           text.size());
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.active_manifest_count"), 0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.claim_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.assertion_count"), 3U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature_linked_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature_orphan_count"), 0U);

    EXPECT_EQ(read_text_field("c2pa.semantic.manifest.0.prefix"),
              "box.0.1.cbor.manifests.manifest0");
    EXPECT_EQ(read_jumbf_field_u64(store, "c2pa.semantic.manifest.0.is_active"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.claim_count"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.assertion_count"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.signature_count"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.signature_linked_count"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.signature_orphan_count"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.0.cross_claim_link_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.0.explicit_reference_signature_count"),
              0U);

    EXPECT_EQ(read_text_field("c2pa.semantic.manifest.1.prefix"),
              "box.0.1.cbor.manifests.manifest1");
    EXPECT_EQ(read_jumbf_field_u64(store, "c2pa.semantic.manifest.1.is_active"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.1.claim_count"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.1.assertion_count"), 2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.1.signature_count"), 1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.1.signature_linked_count"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.1.signature_orphan_count"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.manifest.1.cross_claim_link_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.manifest.1.explicit_reference_signature_count"),
              0U);
    EXPECT_TRUE(
        read_jumbf_field_text(store, "c2pa.semantic.active_manifest.prefix")
            .empty());
}

TEST(JumbfDecode, EmitsDraftC2paExplicitReferenceAmbiguityFields)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x02 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claims[0]");
    append_cbor_text(&cbor_payload, "claims[1]");

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_i64(&cbor_payload, 1);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    auto read_text_field = [&](std::string_view field_name) -> std::string {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> text = store.arena().span(
            e.value.data.span);
        return std::string(reinterpret_cast<const char*>(text.data()),
                           text.size());
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_signature_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              1U);

    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_present"),
              1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        2U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_unresolved"),
              0U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.0.linked_claim_count"),
              2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.0.cross_claim_link_count"),
              1U);
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.linked_claim.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[0]");
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.linked_claim.1.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[1]");

    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.1.explicit_reference_present"),
              1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.1.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.1.explicit_reference_unresolved"),
              0U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.1.explicit_reference_ambiguous"),
              0U);
}

TEST(JumbfDecode, EmitsDraftC2paExplicitReferenceFromJumbfUriKey)
{
    const std::array<std::byte, 4U> claim_bad = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim_good = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim_bad.data(),
                                                 claim_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "jumbf_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/asset?jumbf=c2pa.claim.good");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim_good.data(),
                                                 claim_good.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_bad_jumb
        = make_claim_jumb_box("c2pa.claim.bad",
                              std::span<const std::byte>(claim_bad.data(),
                                                         claim_bad.size()));
    const std::vector<std::byte> claim_good_jumb
        = make_claim_jumb_box("c2pa.claim.good",
                              std::span<const std::byte>(claim_good.data(),
                                                         claim_good.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_bad_jumb.begin(),
                        claim_bad_jumb.end());
    root_payload.insert(root_payload.end(), claim_good_jumb.begin(),
                        claim_good_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    auto read_text_field = [&](std::string_view field_name) -> std::string {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> text = store.arena().span(
            e.value.data.span);
        return std::string(reinterpret_cast<const char*>(text.data()),
                           text.size());
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_index_hits"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_label_hits"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.0.reference_key_hits"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.0.explicit_reference_index_hits"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.0.explicit_reference_label_hits"),
              1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_present"),
              1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_unresolved"),
              0U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.0.linked_claim_count"),
              1U);

    const std::string linked_prefix = read_text_field(
        "c2pa.semantic.signature.0.linked_claim.0.prefix");
    EXPECT_NE(linked_prefix.find("claims[1]"), std::string::npos);
}


TEST(JumbfDecode,
     EmitsDraftC2paExplicitReferenceMixedIndexLabelUriDeterministic)
{
    const std::array<std::byte, 4U> claim_bad = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim_one = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x11 },
    };
    const std::array<std::byte, 4U> claim_two = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x22 },
    };
    const std::array<std::byte, 4U> claim_three = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x33 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 4U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim_bad.data(),
                                                 claim_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.two");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/asset?jumbf=c2pa.claim.three");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim_one.data(),
                                                 claim_one.size()));

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim_two.data(),
                                                 claim_two.size()));

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim_three.data(),
                                                 claim_three.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_one_jumb
        = make_claim_jumb_box("c2pa.claim.one",
                              std::span<const std::byte>(claim_one.data(),
                                                         claim_one.size()));
    const std::vector<std::byte> claim_two_jumb
        = make_claim_jumb_box("c2pa.claim.two",
                              std::span<const std::byte>(claim_two.data(),
                                                         claim_two.size()));
    const std::vector<std::byte> claim_three_jumb
        = make_claim_jumb_box("c2pa.claim.three",
                              std::span<const std::byte>(claim_three.data(),
                                                         claim_three.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_one_jumb.begin(),
                        claim_one_jumb.end());
    root_payload.insert(root_payload.end(), claim_two_jumb.begin(),
                        claim_two_jumb.end());
    root_payload.insert(root_payload.end(), claim_three_jumb.begin(),
                        claim_three_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    auto read_text_field = [&](std::string_view field_name) -> std::string {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> text = store.arena().span(
            e.value.data.span);
        return std::string(reinterpret_cast<const char*>(text.data()),
                           text.size());
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_index_hits"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_label_hits"),
              2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.0.reference_key_hits"),
              3U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.0.explicit_reference_index_hits"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.0.explicit_reference_label_hits"),
              2U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_present"),
              1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        3U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_unresolved"),
              0U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.0.linked_claim_count"),
              3U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.0.cross_claim_link_count"),
              3U);
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.linked_claim.0.prefix"),
              "box.0.4.cbor.manifests.active_manifest.claims[1]");
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.linked_claim.1.prefix"),
              "box.0.4.cbor.manifests.active_manifest.claims[2]");
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.linked_claim.2.prefix"),
              "box.0.4.cbor.manifests.active_manifest.claims[3]");
}

TEST(JumbfDecode,
     EmitsDraftC2paExplicitReferenceMultiSignatureClaimIdDeterministic)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x11 },
    };
    const std::array<std::byte, 4U> claim2 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x22 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 3U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_id");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.missing0");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload, "https://example.test/asset?jumbf="
                                    "c2pa.claim.missing0");

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "reference");
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "index");
    append_cbor_i64(&cbor_payload, 0);
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload, "https://example.test/asset?jumbf="
                                    "c2pa.claim.missing1");
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.missing1");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim2.data(), claim2.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    auto read_text_field = [&](std::string_view field_name) -> std::string {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> text = store.arena().span(
            e.value.data.span);
        return std::string(reinterpret_cast<const char*>(text.data()),
                           text.size());
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_signature_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_index_hits"),
              2U);

    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_present"),
              1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.1.explicit_reference_present"),
              1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.1.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
              0U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.1.explicit_reference_ambiguous"),
              0U);

    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.linked_claim.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[1]");
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.1.linked_claim.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[0]");
}

TEST(JumbfDecode, EmitsDraftC2paExplicitReferenceMultiSignatureIdAmbiguity)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x11 },
    };
    const std::array<std::byte, 4U> claim2 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x22 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 3U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_id");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "reference");
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "id");
    append_cbor_i64(&cbor_payload, 2);
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload, "https://example.test/asset?jumbf="
                                    "c2pa.claim.missing0");
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.missing0");

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_id");
    append_cbor_i64(&cbor_payload, 0);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim2.data(), claim2.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    auto read_text_field = [&](std::string_view field_name) -> std::string {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> text = store.arena().span(
            e.value.data.span);
        return std::string(reinterpret_cast<const char*>(text.data()),
                           text.size());
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_signature_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_index_hits"),
              3U);

    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        2U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.1.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
              1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.1.explicit_reference_ambiguous"),
              0U);

    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.linked_claim.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[1]");
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.linked_claim.1.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[2]");
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.1.linked_claim.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[0]");
}

TEST(JumbfDecode, EmitsDraftC2paExplicitReferenceHrefMapField)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x11 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "href");
    append_cbor_text(&cbor_payload, "https://example.test/media?claim-index=1");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    auto read_text_field = [&](std::string_view field_name) -> std::string {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> text = store.arena().span(
            e.value.data.span);
        return std::string(reinterpret_cast<const char*>(text.data()),
                           text.size());
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_signature_count"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_index_hits"),
              1U);

    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_present"),
              1U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.signature.0.explicit_reference_index_hits"),
              1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
              0U);
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.linked_claim.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[1]");
}

TEST(JumbfDecode, EmitsDraftC2paExplicitReferenceLinkMapField)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x11 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "link");
    append_cbor_text(&cbor_payload, "https://example.test/media?claim-index=1");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_signature_count"),
              1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              0U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_index_hits"),
              1U);
    EXPECT_EQ(
        read_jumbf_field_u64(
            store,
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.signature.0.explicit_reference_index_hits"),
              1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
              0U);
    EXPECT_EQ(read_jumbf_field_text(
                  store, "c2pa.semantic.signature.0.linked_claim.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[1]");
}

TEST(JumbfDecode,
     EmitsDraftC2paExplicitReferenceMultiSignatureClaimRefIdDeterministic)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x11 },
    };
    const std::array<std::byte, 4U> claim2 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x22 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 3U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref_id");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.missing0");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload, "https://example.test/asset?jumbf="
                                    "c2pa.claim.missing0");

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "reference");
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim_id");
    append_cbor_i64(&cbor_payload, 0);
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload, "https://example.test/asset?jumbf="
                                    "c2pa.claim.missing1");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim2.data(), claim2.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    auto read_text_field = [&](std::string_view field_name) -> std::string {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> text = store.arena().span(
            e.value.data.span);
        return std::string(reinterpret_cast<const char*>(text.data()),
                           text.size());
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_signature_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_index_hits"),
              2U);

    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.1.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_unresolved"),
              0U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.1.explicit_reference_unresolved"),
              0U);

    EXPECT_EQ(read_text_field("c2pa.semantic.signature.0.linked_claim.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[1]");
    EXPECT_EQ(read_text_field("c2pa.semantic.signature.1.linked_claim.0.prefix"),
              "box.0.1.cbor.manifests.active_manifest.claims[0]");
}

TEST(JumbfDecode,
     EmitsDraftC2paExplicitReferenceMultiSignatureClaimRefIdUnresolvedNoFallback)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x11 },
    };
    const std::array<std::byte, 4U> claim2 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x22 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 3U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref_id");
    append_cbor_i64(&cbor_payload, 999);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "reference");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim_id");
    append_cbor_i64(&cbor_payload, 888);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim2.data(), claim2.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_signature_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              2U);
    EXPECT_EQ(read_u64_field(
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_index_hits"),
              2U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.explicit_reference_label_hits"),
              0U);

    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        0U);
    EXPECT_EQ(
        read_u64_field(
            "c2pa.semantic.signature.1.explicit_reference_resolved_claim_count"),
        0U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.0.explicit_reference_unresolved"),
              1U);
    EXPECT_EQ(read_u8_field(
                  "c2pa.semantic.signature.1.explicit_reference_unresolved"),
              1U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.0.linked_claim_count"),
              0U);
    EXPECT_EQ(read_u64_field("c2pa.semantic.signature.1.linked_claim_count"),
              0U);
}

TEST(JumbfDecode, C2paVerifyScaffoldNotRequestedByDefault)
{
    const std::vector<std::byte> payload = make_sample_jumbf_payload();

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::NotRequested);
    EXPECT_EQ(result.verify_backend_selected, C2paVerifyBackend::None);

    store.finalize();
    MetaKeyView status_key;
    status_key.kind                           = MetaKeyKind::JumbfField;
    status_key.data.jumbf_field.field         = "c2pa.verify.status";
    const std::span<const EntryId> status_ids = store.find_all(status_key);
    ASSERT_EQ(status_ids.size(), 1U);
    const Entry& status_entry = store.entry(status_ids[0]);
    ASSERT_EQ(status_entry.value.kind, MetaValueKind::Text);
    const std::span<const std::byte> status_text = store.arena().span(
        status_entry.value.data.span);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(status_text.data()),
                               status_text.size()),
              "not_requested");
}

TEST(JumbfDecode,
     EmitsDraftC2paExplicitReferenceScopedPrefixDoesNotCrossMatchIndex10)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x11 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 11U);
    for (uint32_t i = 0U; i < 11U; ++i) {
        if (i == 10U) {
            append_cbor_map(&cbor_payload, 2U);
            append_cbor_text(&cbor_payload, "alg");
            append_cbor_text(&cbor_payload, "es256");
            append_cbor_text(&cbor_payload, "claim_ref_id");
            append_cbor_i64(&cbor_payload, 1);
        } else {
            append_cbor_map(&cbor_payload, 1U);
            append_cbor_text(&cbor_payload, "alg");
            append_cbor_text(&cbor_payload, "es256");
        }
    }

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    EXPECT_EQ(read_jumbf_field_u64(store, "c2pa.semantic.signature_count"),
              11U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_signature_count"),
              1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_index_hits"),
              1U);

    const int32_t slot_sig1
        = find_signature_projection_slot_by_suffix(store,
                                                   ".claims[0].signatures[1]");
    const int32_t slot_sig10
        = find_signature_projection_slot_by_suffix(store,
                                                   ".claims[0].signatures[10]");
    ASSERT_GE(slot_sig1, 0);
    ASSERT_GE(slot_sig10, 0);
    ASSERT_NE(slot_sig1, slot_sig10);

    std::string field;
    field.assign("c2pa.semantic.signature.");
    field.append(std::to_string(static_cast<unsigned long long>(slot_sig1)));
    field.append(".explicit_reference_present");
    EXPECT_EQ(read_jumbf_field_u64(store, field), 0U);

    field.assign("c2pa.semantic.signature.");
    field.append(std::to_string(static_cast<unsigned long long>(slot_sig1)));
    field.append(".explicit_reference_resolved_claim_count");
    EXPECT_EQ(read_jumbf_field_u64(store, field), 0U);

    field.assign("c2pa.semantic.signature.");
    field.append(std::to_string(static_cast<unsigned long long>(slot_sig10)));
    field.append(".explicit_reference_present");
    EXPECT_EQ(read_jumbf_field_u64(store, field), 1U);

    field.assign("c2pa.semantic.signature.");
    field.append(std::to_string(static_cast<unsigned long long>(slot_sig10)));
    field.append(".explicit_reference_resolved_claim_count");
    EXPECT_EQ(read_jumbf_field_u64(store, field), 1U);
}

TEST(JumbfDecode,
     EmitsDraftC2paPlainSignatureUrlDoesNotTriggerExplicitReference)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "url");
    append_cbor_text(&cbor_payload, "https://example.test/no-ref");

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_signature_count"),
              0U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.signature.0.reference_key_hits"),
              0U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.signature.0.explicit_reference_present"),
              0U);
}

TEST(JumbfDecode,
     EmitsDraftC2paExplicitReferenceIndexLabelUriConflictIsAmbiguous)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x02 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "reference_index");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload,
                     "https://example.test/a?jumbf=c2pa.claim.one");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/a?jumbf=c2pa.claim.two");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));

    const std::vector<std::byte> claim_one_box
        = make_claim_jumb_box("c2pa.claim.one",
                              std::span<const std::byte>(claim0.data(),
                                                         claim0.size()));
    const std::vector<std::byte> claim_two_box
        = make_claim_jumb_box("c2pa.claim.two",
                              std::span<const std::byte>(claim1.data(),
                                                         claim1.size()));

    std::vector<std::byte> payload_bytes;
    payload_bytes.insert(payload_bytes.end(), claim_one_box.begin(),
                         claim_one_box.end());
    payload_bytes.insert(payload_bytes.end(), claim_two_box.begin(),
                         claim_two_box.end());
    payload_bytes.insert(payload_bytes.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(payload_bytes.data(), payload_bytes.size()));

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_signature_count"),
              1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_index_hits"),
              1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_label_hits"),
              2U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);

    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.signature.0.explicit_reference_present"),
              1U);
    EXPECT_EQ(
        read_jumbf_field_u64(
            store,
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        2U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
              1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.signature.0.explicit_reference_unresolved"),
              0U);
}

TEST(JumbfDecode,
     EmitsDraftC2paExplicitReferenceIndexLabelUriConsistentReferencesCollapse)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x02 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "reference_index");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload,
                     "https://example.test/b?jumbf=c2pa.claim.two");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/c?jumbf=c2pa.claim.two");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));

    const std::vector<std::byte> claim_one_box
        = make_claim_jumb_box("c2pa.claim.one",
                              std::span<const std::byte>(claim0.data(),
                                                         claim0.size()));
    const std::vector<std::byte> claim_two_box
        = make_claim_jumb_box("c2pa.claim.two",
                              std::span<const std::byte>(claim1.data(),
                                                         claim1.size()));

    std::vector<std::byte> payload_bytes;
    payload_bytes.insert(payload_bytes.end(), claim_one_box.begin(),
                         claim_one_box.end());
    payload_bytes.insert(payload_bytes.end(), claim_two_box.begin(),
                         claim_two_box.end());
    payload_bytes.insert(payload_bytes.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(payload_bytes.data(), payload_bytes.size()));

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_signature_count"),
              1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              0U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(
        read_jumbf_field_u64(
            store,
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
              0U);
}

TEST(JumbfDecode,
     EmitsDraftC2paExplicitReferenceNestedMapIndexLabelHrefConflictIsAmbiguous)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x02 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "index");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "claims[0]");
    append_cbor_text(&cbor_payload, "href");
    append_cbor_text(&cbor_payload, "https://example.test/a?claim-index=1");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_signature_count"),
              1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_index_hits"),
              2U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_label_hits"),
              0U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(
        read_jumbf_field_u64(
            store,
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        2U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
              1U);
}

TEST(JumbfDecode,
     EmitsDraftC2paExplicitReferenceNestedMapIndexLabelHrefConsistent)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x02 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "index");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "https://example.test/c?claim-index=1");
    append_cbor_text(&cbor_payload, "href");
    append_cbor_text(&cbor_payload, "https://example.test/b?claim-index=1");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_signature_count"),
              1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_index_hits"),
              1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store, "c2pa.semantic.explicit_reference_label_hits"),
              0U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              0U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(
        read_jumbf_field_u64(
            store,
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
        1U);
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
              0U);
}

TEST(JumbfDecode, C2paVerifyScaffoldRequested)
{
    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "signature");
    append_cbor_text(&cbor_payload, "ok");
    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::Auto;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY
#    if OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
    EXPECT_EQ(result.verify_backend_selected, C2paVerifyBackend::OpenSsl);
#    elif OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
    EXPECT_EQ(result.verify_backend_selected, C2paVerifyBackend::Native);
#    else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(result.verify_backend_selected, C2paVerifyBackend::None);
#    endif
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(result.verify_backend_selected, C2paVerifyBackend::None);
#endif

    store.finalize();

    MetaKeyView requested_key;
    requested_key.kind                           = MetaKeyKind::JumbfField;
    requested_key.data.jumbf_field.field         = "c2pa.verify.requested";
    const std::span<const EntryId> requested_ids = store.find_all(
        requested_key);
    ASSERT_EQ(requested_ids.size(), 1U);
    const Entry& requested_entry = store.entry(requested_ids[0]);
    ASSERT_EQ(requested_entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(requested_entry.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(static_cast<uint8_t>(requested_entry.value.data.u64), 1U);
}

TEST(JumbfDecode, C2paVerifyScaffoldInvalidSignatureShape)
{
    const std::array<std::byte, 3U> message
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::array<std::byte, 2U> bad_signature = { std::byte { 0x01 },
                                                      std::byte { 0x02 } };
    const std::array<std::byte, 1U> bad_pub       = { std::byte { 0x00 } };
    const std::vector<std::byte> payload
        = make_c2pa_verify_sample_payload("es256", message, bad_signature,
                                          bad_pub);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::Auto;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY
#    if OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::InvalidSignature);
#    elif OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::NotImplemented);
#    else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#    endif
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyScaffoldReportsSignatureOnlyEs256)
{
    const std::array<std::byte, 3U> message
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> public_key_der = bytes_from_hex(
        "3059301306072a8648ce3d020106082a8648ce3d030107034200041d1e9d8e69b7"
        "e1b046b5f6ce6df9b09334c28019a60bbefb21e9fa73507f472549a83a6ea8486a15"
        "4ffd9f4d628f5059c9a61a5fb419743968400a4fda9d76db");
    const std::vector<std::byte> signature = bytes_from_hex(
        "3046022100d4814ed8eee0ee46575e6a010458262b694160355348e567ae0ed5a4"
        "e3c5ff62022100dcb50937189d3affdf696a7ba71789b5fd42f1ea0ef7291e623dc6"
        "7b14d52988");
    ASSERT_FALSE(public_key_der.empty());
    ASSERT_FALSE(signature.empty());

    const std::vector<std::byte> payload
        = make_c2pa_verify_sample_payload("es256", message, signature,
                                          public_key_der);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_NE(result.verify_status, C2paVerifyStatus::Verified);
    EXPECT_EQ(result.verify_backend_selected, C2paVerifyBackend::OpenSsl);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifySkipsIncompleteSignatureCandidate)
{
    const std::array<std::byte, 3U> message
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> public_key_der = bytes_from_hex(
        "3059301306072a8648ce3d020106082a8648ce3d030107034200041d1e9d8e69b7"
        "e1b046b5f6ce6df9b09334c28019a60bbefb21e9fa73507f472549a83a6ea8486a15"
        "4ffd9f4d628f5059c9a61a5fb419743968400a4fda9d76db");
    const std::vector<std::byte> signature = bytes_from_hex(
        "3046022100d4814ed8eee0ee46575e6a010458262b694160355348e567ae0ed5a4"
        "e3c5ff62022100dcb50937189d3affdf696a7ba71789b5fd42f1ea0ef7291e623dc6"
        "7b14d52988");
    ASSERT_FALSE(public_key_der.empty());
    ASSERT_FALSE(signature.empty());

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 2U);

    // Candidate 0: missing signing_input (should be ignored, not fatal).
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "signature");
    append_cbor_bytes(&cbor_payload, signature);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);

    // Candidate 1: complete and valid.
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "signing_input");
    append_cbor_bytes(&cbor_payload, message);
    append_cbor_text(&cbor_payload, "signature");
    append_cbor_bytes(&cbor_payload, signature);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyMultipleSignaturesSelectsVerifiedCandidate)
{
    const std::array<std::byte, 3U> message
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> public_key_der = bytes_from_hex(
        "3059301306072a8648ce3d020106082a8648ce3d030107034200041d1e9d8e69b7"
        "e1b046b5f6ce6df9b09334c28019a60bbefb21e9fa73507f472549a83a6ea8486a15"
        "4ffd9f4d628f5059c9a61a5fb419743968400a4fda9d76db");
    const std::vector<std::byte> signature_ok = bytes_from_hex(
        "3046022100d4814ed8eee0ee46575e6a010458262b694160355348e567ae0ed5a4"
        "e3c5ff62022100dcb50937189d3affdf696a7ba71789b5fd42f1ea0ef7291e623dc6"
        "7b14d52988");
    ASSERT_FALSE(public_key_der.empty());
    ASSERT_FALSE(signature_ok.empty());
    std::vector<std::byte> signature_bad = signature_ok;
    signature_bad.back()                 = std::byte { static_cast<uint8_t>(
        static_cast<uint8_t>(signature_bad.back()) ^ 0x01U) };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "signing_input");
    append_cbor_bytes(&cbor_payload, message);
    append_cbor_text(&cbor_payload, "signature");
    append_cbor_bytes(&cbor_payload, signature_bad);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);

    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "signing_input");
    append_cbor_bytes(&cbor_payload, message);
    append_cbor_text(&cbor_payload, "signature");
    append_cbor_bytes(&cbor_payload, signature_ok);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyStrictChainRequiresTrustedCertificate)
{
    const std::array<std::byte, 3U> message
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> cert_der = self_signed_cert_der_from_key(key);
    ASSERT_FALSE(cert_der.empty());
    const std::vector<std::byte> signature = ecdsa_sign_sha256(key, message);
    EVP_PKEY_free(key);
    ASSERT_FALSE(signature.empty());
#else
    const std::vector<std::byte> cert_der;
    const std::vector<std::byte> signature;
#endif

    const std::vector<std::byte> payload
        = make_c2pa_manifest_with_certificate("es256", message, signature,
                                              cert_der);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
    store.finalize();
    const std::string chain_reason
        = read_jumbf_field_text(store, "c2pa.verify.chain_reason");
    EXPECT_NE(chain_reason, "ok");
    EXPECT_EQ(read_jumbf_field_u64(store, "c2pa.verify.require_trusted_chain"),
              0U);

    MetaStore strict_store;
    options.verify_require_trusted_chain = true;
    const JumbfDecodeResult strict_result
        = decode_jumbf_payload(payload, strict_store, EntryFlags::None,
                               options);
    EXPECT_EQ(strict_result.status, JumbfDecodeStatus::Ok);
    EXPECT_TRUE(
        strict_result.verify_status == C2paVerifyStatus::VerificationFailed
        || strict_result.verify_status == C2paVerifyStatus::BackendUnavailable);
    strict_store.finalize();
    EXPECT_EQ(read_jumbf_field_u64(strict_store,
                                   "c2pa.verify.require_trusted_chain"),
              1U);
    const std::string strict_chain_reason
        = read_jumbf_field_text(strict_store, "c2pa.verify.chain_reason");
    EXPECT_TRUE(strict_chain_reason == "self_signed_leaf"
                || strict_chain_reason == "self_signed_chain"
                || strict_chain_reason == "issuer_not_trusted"
                || strict_chain_reason == "issuer_not_found"
                || strict_chain_reason == "trust_chain_unverified");
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, ValidateFileC2paStrictChainRequiresTrustedCertificate)
{
#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    const std::array<std::byte, 3U> message
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };

    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> cert_der = self_signed_cert_der_from_key(key);
    ASSERT_FALSE(cert_der.empty());
    const std::vector<std::byte> signature = ecdsa_sign_sha256(key, message);
    EVP_PKEY_free(key);
    ASSERT_FALSE(signature.empty());

    const std::vector<std::byte> payload
        = make_c2pa_manifest_with_certificate("es256", message, signature,
                                              cert_der);
    const std::vector<std::byte> jpeg = make_jpeg_app11_jumbf_file(payload);
    ASSERT_FALSE(jpeg.empty());

    const std::string path = make_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(path, jpeg));
    const ScopedFile cleanup(path);

    ValidateOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;

    const ValidateResult loose_result = validate_file(path.c_str(), options);
    ASSERT_EQ(loose_result.status, ValidateStatus::Ok);
    EXPECT_EQ(loose_result.read.jumbf.status, JumbfDecodeStatus::Ok);
    EXPECT_EQ(loose_result.read.jumbf.verify_status,
              C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_TRUE(loose_result.failed);
    EXPECT_GE(loose_result.error_count, 1U);
    EXPECT_GE(loose_result.warning_count, 1U);
    EXPECT_TRUE(validate_result_has_issue(loose_result, "c2pa",
                                          "signature_verified_only"));
    EXPECT_TRUE(
        validate_result_has_issue(loose_result, "c2pa", "untrusted_chain"));

    ValidateOptions warnings_as_errors_options = options;
    warnings_as_errors_options.warnings_as_errors = true;
    const ValidateResult warnings_as_errors_result
        = validate_file(path.c_str(), warnings_as_errors_options);
    ASSERT_EQ(warnings_as_errors_result.status, ValidateStatus::Ok);
    EXPECT_EQ(warnings_as_errors_result.read.jumbf.status,
              JumbfDecodeStatus::Ok);
    EXPECT_EQ(warnings_as_errors_result.read.jumbf.verify_status,
              C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_TRUE(warnings_as_errors_result.failed);
    EXPECT_GE(warnings_as_errors_result.error_count, 1U);
    EXPECT_TRUE(validate_result_has_issue(warnings_as_errors_result, "c2pa",
                                          "untrusted_chain"));

    ValidateOptions strict_options              = options;
    strict_options.verify_require_trusted_chain = true;
    const ValidateResult strict_result = validate_file(path.c_str(),
                                                       strict_options);
    ASSERT_EQ(strict_result.status, ValidateStatus::Ok);
    EXPECT_EQ(strict_result.read.jumbf.status, JumbfDecodeStatus::Ok);
    EXPECT_TRUE(strict_result.read.jumbf.verify_status
                    == C2paVerifyStatus::VerificationFailed
                || strict_result.read.jumbf.verify_status
                       == C2paVerifyStatus::BackendUnavailable);
    EXPECT_NE(strict_result.read.jumbf.verify_status,
              C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_GT(strict_result.error_count + strict_result.warning_count, 0U);
#else
    GTEST_SKIP() << "OpenSSL C2PA verification is unavailable";
#endif
}


TEST(JumbfDecode, C2paCertificateKeyOverridesMismatchedIndependentKey)
{
#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    const std::array<std::byte, 3U> message
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };

    EVP_PKEY* signing_key = nullptr;
    EVP_PKEY* cert_key    = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&signing_key));
    ASSERT_TRUE(make_ec_p256_keypair(&cert_key));

    const std::vector<std::byte> signing_public_key = public_key_der_from_key(
        signing_key);
    const std::vector<std::byte> matching_certificate
        = self_signed_cert_der_from_key(signing_key);
    const std::vector<std::byte> mismatched_certificate
        = self_signed_cert_der_from_key(cert_key);
    const std::vector<std::byte> signature = ecdsa_sign_sha256(signing_key,
                                                               message);
    EVP_PKEY_free(signing_key);
    EVP_PKEY_free(cert_key);

    ASSERT_FALSE(signing_public_key.empty());
    ASSERT_FALSE(matching_certificate.empty());
    ASSERT_FALSE(mismatched_certificate.empty());
    ASSERT_FALSE(signature.empty());

    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;

    MetaStore matching_store;
    const std::vector<std::byte> matching_payload
        = make_c2pa_manifest_with_key_and_certificate("es256", message,
                                                      signature,
                                                      signing_public_key,
                                                      matching_certificate);
    const JumbfDecodeResult matching_result
        = decode_jumbf_payload(matching_payload, matching_store,
                               EntryFlags::None, options);
    EXPECT_EQ(matching_result.verify_status,
              C2paVerifyStatus::SignatureVerifiedOnly);

    MetaStore mismatched_store;
    const std::vector<std::byte> mismatched_payload
        = make_c2pa_manifest_with_key_and_certificate("es256", message,
                                                      signature,
                                                      signing_public_key,
                                                      mismatched_certificate);
    const JumbfDecodeResult mismatched_result
        = decode_jumbf_payload(mismatched_payload, mismatched_store,
                               EntryFlags::None, options);
    EXPECT_EQ(mismatched_result.verify_status,
              C2paVerifyStatus::VerificationFailed);
#else
    GTEST_SKIP() << "OpenSSL C2PA verification is unavailable";
#endif
}

TEST(JumbfDecode, C2paVerifyScaffoldVerificationFailedEs256)
{
    const std::array<std::byte, 3U> message
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> public_key_der = bytes_from_hex(
        "3059301306072a8648ce3d020106082a8648ce3d030107034200041d1e9d8e69b7"
        "e1b046b5f6ce6df9b09334c28019a60bbefb21e9fa73507f472549a83a6ea8486a15"
        "4ffd9f4d628f5059c9a61a5fb419743968400a4fda9d76db");
    std::vector<std::byte> signature = bytes_from_hex(
        "3046022100d4814ed8eee0ee46575e6a010458262b694160355348e567ae0ed5a4"
        "e3c5ff62022100dcb50937189d3affdf696a7ba71789b5fd42f1ea0ef7291e623dc6"
        "7b14d52988");
    ASSERT_FALSE(public_key_der.empty());
    ASSERT_FALSE(signature.empty());
    signature.back() = std::byte { static_cast<uint8_t>(
        static_cast<uint8_t>(signature.back()) ^ 0x01U) };

    const std::vector<std::byte> payload
        = make_c2pa_verify_sample_payload("es256", message, signature,
                                          public_key_der);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
    EXPECT_EQ(result.verify_backend_selected, C2paVerifyBackend::OpenSsl);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyDraftRegressionFixtures)
{
    const std::array<std::byte, 3U> message
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> valid_signature = bytes_from_hex(
        "3046022100d4814ed8eee0ee46575e6a010458262b694160355348e567ae0ed5a4"
        "e3c5ff62022100dcb50937189d3affdf696a7ba71789b5fd42f1ea0ef7291e623dc6"
        "7b14d52988");
    const std::vector<std::byte> public_key_der = bytes_from_hex(
        "3059301306072a8648ce3d020106082a8648ce3d030107034200041d1e9d8e69b7"
        "e1b046b5f6ce6df9b09334c28019a60bbefb21e9fa73507f472549a83a6ea8486a15"
        "4ffd9f4d628f5059c9a61a5fb419743968400a4fda9d76db");
    const std::array<std::byte, 2U> bad_cert = { std::byte { 0x01 },
                                                 std::byte { 0x02 } };
    ASSERT_FALSE(valid_signature.empty());
    ASSERT_FALSE(public_key_der.empty());

    struct FixtureCase final {
        const char* name;
        std::vector<std::byte> payload;
        C2paVerifyStatus status;
        const char* profile_reason;
        const char* chain_reason;
    };

    const std::array<FixtureCase, 2U> fixtures = {
        FixtureCase {
            "missing_claims_profile",
            make_c2pa_manifest_with_top_level_signature("es256", message,
                                                        valid_signature,
                                                        public_key_der),
            C2paVerifyStatus::VerificationFailed,
            "claim_missing",
            "not_checked",
        },
        FixtureCase {
            "invalid_certificate_chain",
            make_c2pa_manifest_with_certificate("es256", message,
                                                valid_signature, bad_cert),
            C2paVerifyStatus::VerificationFailed,
            "ok",
            "certificate_parse_failed",
        },
    };

    for (const FixtureCase& fixture : fixtures) {
        MetaStore store;
        JumbfDecodeOptions options;
        options.verify_c2pa    = true;
        options.verify_backend = C2paVerifyBackend::OpenSsl;
        const JumbfDecodeResult result
            = decode_jumbf_payload(fixture.payload, store, EntryFlags::None,
                                   options);
        EXPECT_EQ(result.status, JumbfDecodeStatus::Ok) << fixture.name;

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
        EXPECT_EQ(result.verify_status, fixture.status) << fixture.name;
        store.finalize();
        EXPECT_EQ(read_jumbf_field_text(store, "c2pa.verify.profile_reason"),
                  fixture.profile_reason)
            << fixture.name;
        EXPECT_EQ(read_jumbf_field_text(store, "c2pa.verify.chain_reason"),
                  fixture.chain_reason)
            << fixture.name;
#elif OPENMETA_ENABLE_C2PA_VERIFY
        EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable)
            << fixture.name;
#else
        EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild)
            << fixture.name;
#endif
    }
}

TEST(JumbfDecode,
     C2paVerifyProfilePolicyRequireResolvedReferencesUnresolvedFails)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x02 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref_id");
    append_cbor_i64(&cbor_payload, 99);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref_id");
    append_cbor_i64(&cbor_payload, 0);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa                        = true;
    options.verify_backend                     = C2paVerifyBackend::OpenSsl;
    options.verify_require_resolved_references = true;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    MetaKeyView strict_key;
    strict_key.kind = MetaKeyKind::JumbfField;
    strict_key.data.jumbf_field.field
        = "c2pa.verify.require_resolved_references";
    const std::span<const EntryId> strict_ids = store.find_all(strict_key);
    ASSERT_EQ(strict_ids.size(), 1U);
    EXPECT_EQ(store.entry(strict_ids[0]).value.data.u64, 1U);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
    EXPECT_EQ(read_jumbf_field_text(store, "c2pa.verify.profile_reason"),
              "explicit_reference_unresolved");
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.verify.explicit_reference_unresolved_signature_count"),
              1U);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyProfilePolicyRequireResolvedReferencesAmbiguousFails)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x02 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref_id");
    append_cbor_i64(&cbor_payload, 0);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "claims[1]");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa                        = true;
    options.verify_backend                     = C2paVerifyBackend::OpenSsl;
    options.verify_require_resolved_references = true;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
    EXPECT_EQ(read_jumbf_field_text(store, "c2pa.verify.profile_reason"),
              "explicit_reference_ambiguous");
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.verify.explicit_reference_ambiguous_signature_count"),
              1U);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyProfilePolicyRequireResolvedReferencesDisabledDoesNotFailProfile)
{
    const std::array<std::byte, 4U> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::array<std::byte, 4U> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x02 },
    };

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref_id");
    append_cbor_i64(&cbor_payload, 99);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "alg");
    append_cbor_text(&cbor_payload, "es256");
    append_cbor_text(&cbor_payload, "claim_ref_id");
    append_cbor_i64(&cbor_payload, 0);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    // Explicit-reference policy is intentionally disabled in this test.
    options.verify_require_resolved_references = false;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    store.finalize();

    MetaKeyView strict_key;
    strict_key.kind = MetaKeyKind::JumbfField;
    strict_key.data.jumbf_field.field
        = "c2pa.verify.require_resolved_references";
    const std::span<const EntryId> strict_ids = store.find_all(strict_key);
    ASSERT_EQ(strict_ids.size(), 1U);
    EXPECT_EQ(store.entry(strict_ids[0]).value.data.u64, 0U);

#if OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.verify.explicit_reference_unresolved_signature_count"),
              1U);
#else
    EXPECT_EQ(read_jumbf_field_u64(
                  store,
                  "c2pa.verify.explicit_reference_unresolved_signature_count"),
              0U);
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
    EXPECT_NE(read_jumbf_field_text(store, "c2pa.verify.profile_reason"),
              "explicit_reference_unresolved");
}

TEST(JumbfDecode, C2paVerifyCoseSign1ArrayEs256)
{
    const std::array<std::byte, 3U> payload_bytes
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure
        = make_cose_sig_structure(protected_header, payload_bytes);
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);

    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_bytes(&cbor_payload, payload_bytes);
    append_cbor_bytes(&cbor_payload, raw_sig);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);
    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
    store.finalize();
    EXPECT_EQ(read_jumbf_field_text(store, "c2pa.verify.chain_reason"),
              "no_certificate");
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseSign1BytesEs256)
{
    const std::array<std::byte, 3U> payload_bytes
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure
        = make_cose_sig_structure(protected_header, payload_bytes);
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cose_sign1;
    cose_sign1.push_back(std::byte { 0xD2 });  // tag(18)
    append_cbor_array(&cose_sign1, 4U);
    append_cbor_bytes(&cose_sign1, protected_header);
    append_cbor_map(&cose_sign1, 1U);
    append_cbor_text(&cose_sign1, "public_key_der");
    append_cbor_bytes(&cose_sign1, public_key_der);
    append_cbor_bytes(&cose_sign1, payload_bytes);
    append_cbor_bytes(&cose_sign1, raw_sig);

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_bytes(&cbor_payload, cose_sign1);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);
    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
    store.finalize();
    EXPECT_EQ(read_jumbf_field_text(store, "c2pa.verify.chain_reason"),
              "no_certificate");
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseSign1BytesMalformedArrayShape)
{
    const std::array<std::byte, 3U> payload_bytes
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

    // COSE_Sign1 must be [protected, unprotected, payload, signature].
    // Here we intentionally emit a 3-element array to ensure shape validation
    // rejects it deterministically.
    std::vector<std::byte> cose_sign1;
    cose_sign1.push_back(std::byte { 0xD2 });  // tag(18)
    append_cbor_array(&cose_sign1, 3U);
    append_cbor_bytes(&cose_sign1, protected_header);
    append_cbor_map(&cose_sign1, 0U);
    append_cbor_bytes(&cose_sign1, payload_bytes);

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_bytes(&cbor_payload, cose_sign1);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);
    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::Auto;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY
#    if OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::InvalidSignature);
#    elif OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::NotImplemented);
#    else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#    endif
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseSign1BytesX5chainExtraction)
{
    const std::array<std::byte, 3U> payload_bytes
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

    const std::array<std::byte, 2U> bad_cert = { std::byte { 0x01 },
                                                 std::byte { 0x02 } };
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });

    std::vector<std::byte> cose_sign1;
    append_cbor_array(&cose_sign1, 4U);
    append_cbor_bytes(&cose_sign1, protected_header);
    append_cbor_map(&cose_sign1, 1U);
    append_cbor_head(&cose_sign1, 0U, 33U);
    append_cbor_array(&cose_sign1, 1U);
    append_cbor_bytes(&cose_sign1, bad_cert);
    append_cbor_bytes(&cose_sign1, payload_bytes);
    append_cbor_bytes(&cose_sign1, raw_sig);

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_bytes(&cbor_payload, cose_sign1);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);
    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
    store.finalize();
    EXPECT_EQ(read_jumbf_field_text(store, "c2pa.verify.chain_reason"),
              "certificate_parse_failed");
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseSign1BytesLargeX5chainArray)
{
    const std::array<std::byte, 3U> payload_bytes
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

    const std::array<std::byte, 2U> bad_cert = { std::byte { 0x01 },
                                                 std::byte { 0x02 } };
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });

    std::vector<std::byte> cose_sign1;
    append_cbor_array(&cose_sign1, 4U);
    append_cbor_bytes(&cose_sign1, protected_header);
    append_cbor_map(&cose_sign1, 1U);
    append_cbor_head(&cose_sign1, 0U, 33U);
    append_cbor_array(&cose_sign1, 96U);
    for (uint32_t i = 0U; i < 96U; ++i) {
        append_cbor_bytes(&cose_sign1, bad_cert);
    }
    append_cbor_bytes(&cose_sign1, payload_bytes);
    append_cbor_bytes(&cose_sign1, raw_sig);

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_bytes(&cbor_payload, cose_sign1);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);
    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
    store.finalize();
    EXPECT_EQ(read_jumbf_field_text(store, "c2pa.verify.chain_reason"),
              "certificate_parse_failed");
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadFromClaimBytes)
{
    const std::array<std::byte, 3U> payload_bytes
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure
        = make_cose_sig_structure(protected_header, payload_bytes);
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload, payload_bytes);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);

    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);
    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadFromManifestClaimsArray)
{
    const std::array<std::byte, 3U> payload_bytes
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure
        = make_cose_sig_structure(protected_header, payload_bytes);
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 2U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload, payload_bytes);

    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);
    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadFromNestedClaimPrefix)
{
    const std::array<std::byte, 3U> payload_bytes
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure
        = make_cose_sig_structure(protected_header, payload_bytes);
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload, payload_bytes);
    append_cbor_text(&cbor_payload, "sig_group");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);
    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadFromJumbfClaimBox)
{
    const std::vector<std::byte> claim_cbor = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim_cbor.data(), claim_cbor.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cose_sign1;
    cose_sign1.push_back(std::byte { 0xD2 });  // tag(18)
    append_cbor_array(&cose_sign1, 4U);
    append_cbor_bytes(&cose_sign1, protected_header);
    append_cbor_map(&cose_sign1, 1U);
    append_cbor_text(&cose_sign1, "public_key_der");
    append_cbor_bytes(&cose_sign1, public_key_der);
    append_cbor_null(&cose_sign1);
    append_cbor_bytes(&cose_sign1, raw_sig);

    std::vector<std::byte> claim_cbor_box;
    append_bmff_box(&claim_cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(claim_cbor.data(),
                                               claim_cbor.size()));
    const std::vector<std::byte> claim_jumb = make_jumb_box_with_label(
        "c2pa.claim", std::span<const std::byte>(claim_cbor_box.data(),
                                                 claim_cbor_box.size()));

    std::vector<std::byte> signature_cbor_box;
    append_bmff_box(&signature_cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cose_sign1.data(),
                                               cose_sign1.size()));
    const std::vector<std::byte> signature_jumb = make_jumb_box_with_label(
        "c2pa.signature",
        std::span<const std::byte>(signature_cbor_box.data(),
                                   signature_cbor_box.size()));

    std::vector<std::byte> manifest_payload;
    manifest_payload.insert(manifest_payload.end(), claim_jumb.begin(),
                            claim_jumb.end());
    manifest_payload.insert(manifest_payload.end(), signature_jumb.begin(),
                            signature_jumb.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa", std::span<const std::byte>(manifest_payload.data(),
                                           manifest_payload.size()));

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadFromNestedClaimJumbfBox)
{
    const std::vector<std::byte> claim_cbor = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim_cbor.data(), claim_cbor.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cose_sign1;
    cose_sign1.push_back(std::byte { 0xD2 });  // tag(18)
    append_cbor_array(&cose_sign1, 4U);
    append_cbor_bytes(&cose_sign1, protected_header);
    append_cbor_map(&cose_sign1, 1U);
    append_cbor_text(&cose_sign1, "public_key_der");
    append_cbor_bytes(&cose_sign1, public_key_der);
    append_cbor_null(&cose_sign1);
    append_cbor_bytes(&cose_sign1, raw_sig);

    std::vector<std::byte> claim_cbor_box;
    append_bmff_box(&claim_cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(claim_cbor.data(),
                                               claim_cbor.size()));
    const std::vector<std::byte> claim_jumb = make_jumb_box_with_label(
        "c2pa.claim", std::span<const std::byte>(claim_cbor_box.data(),
                                                 claim_cbor_box.size()));
    const std::vector<std::byte> claim_store_jumb = make_jumb_box_with_label(
        "c2pa.store",
        std::span<const std::byte>(claim_jumb.data(), claim_jumb.size()));

    std::vector<std::byte> signature_cbor_box;
    append_bmff_box(&signature_cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cose_sign1.data(),
                                               cose_sign1.size()));
    const std::vector<std::byte> signature_jumb = make_jumb_box_with_label(
        "c2pa.signature",
        std::span<const std::byte>(signature_cbor_box.data(),
                                   signature_cbor_box.size()));

    std::vector<std::byte> manifest_payload;
    manifest_payload.insert(manifest_payload.end(), claim_store_jumb.begin(),
                            claim_store_jumb.end());
    manifest_payload.insert(manifest_payload.end(), signature_jumb.begin(),
                            signature_jumb.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa", std::span<const std::byte>(manifest_payload.data(),
                                           manifest_payload.size()));

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadFromCrossManifestClaim)
{
    const std::vector<std::byte> claim_near_cbor = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> claim_far_cbor = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x02 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim_far_cbor.data(),
                                   claim_far_cbor.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cose_sign1;
    cose_sign1.push_back(std::byte { 0xD2 });  // tag(18)
    append_cbor_array(&cose_sign1, 4U);
    append_cbor_bytes(&cose_sign1, protected_header);
    append_cbor_map(&cose_sign1, 1U);
    append_cbor_text(&cose_sign1, "public_key_der");
    append_cbor_bytes(&cose_sign1, public_key_der);
    append_cbor_null(&cose_sign1);
    append_cbor_bytes(&cose_sign1, raw_sig);

    std::vector<std::byte> claim_near_cbor_box;
    append_bmff_box(&claim_near_cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(claim_near_cbor.data(),
                                               claim_near_cbor.size()));
    const std::vector<std::byte> claim_near_jumb = make_jumb_box_with_label(
        "c2pa.claim", std::span<const std::byte>(claim_near_cbor_box.data(),
                                                 claim_near_cbor_box.size()));

    std::vector<std::byte> claim_far_cbor_box;
    append_bmff_box(&claim_far_cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(claim_far_cbor.data(),
                                               claim_far_cbor.size()));
    const std::vector<std::byte> claim_far_jumb = make_jumb_box_with_label(
        "c2pa.claim", std::span<const std::byte>(claim_far_cbor_box.data(),
                                                 claim_far_cbor_box.size()));

    std::vector<std::byte> signature_cbor_box;
    append_bmff_box(&signature_cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cose_sign1.data(),
                                               cose_sign1.size()));
    const std::vector<std::byte> signature_jumb = make_jumb_box_with_label(
        "c2pa.signature",
        std::span<const std::byte>(signature_cbor_box.data(),
                                   signature_cbor_box.size()));

    std::vector<std::byte> manifest_near_payload;
    manifest_near_payload.insert(manifest_near_payload.end(),
                                 claim_near_jumb.begin(),
                                 claim_near_jumb.end());
    manifest_near_payload.insert(manifest_near_payload.end(),
                                 signature_jumb.begin(), signature_jumb.end());
    const std::vector<std::byte> manifest_near = make_jumb_box_with_label(
        "c2pa.manifest",
        std::span<const std::byte>(manifest_near_payload.data(),
                                   manifest_near_payload.size()));

    std::vector<std::byte> manifest_far_payload;
    manifest_far_payload.insert(manifest_far_payload.end(),
                                claim_far_jumb.begin(), claim_far_jumb.end());
    const std::vector<std::byte> manifest_far = make_jumb_box_with_label(
        "c2pa.manifest",
        std::span<const std::byte>(manifest_far_payload.data(),
                                   manifest_far_payload.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), manifest_near.begin(),
                        manifest_near.end());
    root_payload.insert(root_payload.end(), manifest_far.begin(),
                        manifest_far.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadFromClaimReferenceIndex)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 41U);
    for (uint32_t i = 0U; i < 40U; ++i) {
        append_cbor_map(&cbor_payload, (i == 0U) ? 2U : 1U);
        append_cbor_text(&cbor_payload, "claim");
        const uint8_t value = static_cast<uint8_t>((i % 253U) + 1U);
        const std::array<std::byte, 4U> bad_claim = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { value },
        };
        append_cbor_bytes(&cbor_payload,
                          std::span<const std::byte>(bad_claim.data(),
                                                     bad_claim.size()));
        if (i == 0U) {
            append_cbor_text(&cbor_payload, "signatures");
            append_cbor_array(&cbor_payload, 1U);
            append_cbor_array(&cbor_payload, 4U);
            append_cbor_bytes(&cbor_payload, protected_header);
            append_cbor_map(&cbor_payload, 2U);
            append_cbor_text(&cbor_payload, "public_key_der");
            append_cbor_bytes(&cbor_payload, public_key_der);
            append_cbor_text(&cbor_payload, "claim_ref");
            append_cbor_text(&cbor_payload, "claims[40]");
            append_cbor_null(&cbor_payload);
            append_cbor_bytes(&cbor_payload, raw_sig);
        }
    }
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadFromClaimReferenceScalarIndex)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 41U);
    for (uint32_t i = 0U; i < 40U; ++i) {
        append_cbor_map(&cbor_payload, (i == 0U) ? 2U : 1U);
        append_cbor_text(&cbor_payload, "claim");
        const uint8_t value = static_cast<uint8_t>((i % 253U) + 1U);
        const std::array<std::byte, 4U> bad_claim = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { value },
        };
        append_cbor_bytes(&cbor_payload,
                          std::span<const std::byte>(bad_claim.data(),
                                                     bad_claim.size()));
        if (i == 0U) {
            append_cbor_text(&cbor_payload, "signatures");
            append_cbor_array(&cbor_payload, 1U);
            append_cbor_array(&cbor_payload, 4U);
            append_cbor_bytes(&cbor_payload, protected_header);
            append_cbor_map(&cbor_payload, 2U);
            append_cbor_text(&cbor_payload, "public_key_der");
            append_cbor_bytes(&cbor_payload, public_key_der);
            append_cbor_text(&cbor_payload, "claim_ref");
            append_cbor_i64(&cbor_payload, 40);
            append_cbor_null(&cbor_payload);
            append_cbor_bytes(&cbor_payload, raw_sig);
        }
    }
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadFromClaimReferenceArrayElements)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 41U);
    for (uint32_t i = 0U; i < 40U; ++i) {
        append_cbor_map(&cbor_payload, (i == 0U) ? 2U : 1U);
        append_cbor_text(&cbor_payload, "claim");
        const uint8_t value = static_cast<uint8_t>((i % 253U) + 1U);
        const std::array<std::byte, 4U> bad_claim = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { value },
        };
        append_cbor_bytes(&cbor_payload,
                          std::span<const std::byte>(bad_claim.data(),
                                                     bad_claim.size()));
        if (i == 0U) {
            append_cbor_text(&cbor_payload, "signatures");
            append_cbor_array(&cbor_payload, 1U);
            append_cbor_array(&cbor_payload, 4U);
            append_cbor_bytes(&cbor_payload, protected_header);
            append_cbor_map(&cbor_payload, 2U);
            append_cbor_text(&cbor_payload, "public_key_der");
            append_cbor_bytes(&cbor_payload, public_key_der);
            append_cbor_text(&cbor_payload, "claimRef");
            append_cbor_array(&cbor_payload, 2U);
            append_cbor_text(&cbor_payload, "claims[39]");
            append_cbor_text(&cbor_payload, "claims[40]");
            append_cbor_null(&cbor_payload);
            append_cbor_bytes(&cbor_payload, raw_sig);
        }
    }
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadConflictingReferences)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 41U);
    for (uint32_t i = 0U; i < 40U; ++i) {
        append_cbor_map(&cbor_payload, (i == 0U) ? 2U : 1U);
        append_cbor_text(&cbor_payload, "claim");
        const uint8_t value = static_cast<uint8_t>((i % 253U) + 1U);
        const std::array<std::byte, 4U> claim_value = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { value },
        };
        append_cbor_bytes(&cbor_payload,
                          std::span<const std::byte>(claim_value.data(),
                                                     claim_value.size()));
        if (i == 0U) {
            append_cbor_text(&cbor_payload, "signatures");
            append_cbor_array(&cbor_payload, 1U);
            append_cbor_array(&cbor_payload, 4U);
            append_cbor_bytes(&cbor_payload, protected_header);
            append_cbor_map(&cbor_payload, 4U);
            append_cbor_text(&cbor_payload, "public_key_der");
            append_cbor_bytes(&cbor_payload, public_key_der);
            append_cbor_text(&cbor_payload, "claim_ref");
            append_cbor_text(&cbor_payload, "claims[39]");
            append_cbor_text(&cbor_payload, "claim_reference");
            append_cbor_text(&cbor_payload, "c2pa.claim.bad");
            append_cbor_text(&cbor_payload, "claim_uri");
            append_cbor_text(&cbor_payload, "https://example.test/asset?jumbf="
                                            "c2pa.claim.good");
            append_cbor_null(&cbor_payload);
            append_cbor_bytes(&cbor_payload, raw_sig);
        }
    }
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_bad_jumb
        = make_claim_jumb_box("c2pa.claim.bad",
                              std::span<const std::byte>(bad_claim.data(),
                                                         bad_claim.size()));
    const std::vector<std::byte> claim_good_jumb
        = make_claim_jumb_box("c2pa.claim.good",
                              std::span<const std::byte>(target_claim.data(),
                                                         target_claim.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_bad_jumb.begin(),
                        claim_bad_jumb.end());
    root_payload.insert(root_payload.end(), claim_good_jumb.begin(),
                        claim_good_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadReferencePrecedenceSkipsGenericFallback)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 41U);
    for (uint32_t i = 0U; i < 40U; ++i) {
        append_cbor_map(&cbor_payload, (i == 0U) ? 2U : 1U);
        append_cbor_text(&cbor_payload, "claim");
        const uint8_t value = static_cast<uint8_t>((i % 253U) + 1U);
        const std::array<std::byte, 4U> claim_value = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { value },
        };
        append_cbor_bytes(&cbor_payload,
                          std::span<const std::byte>(claim_value.data(),
                                                     claim_value.size()));
        if (i == 0U) {
            append_cbor_text(&cbor_payload, "signatures");
            append_cbor_array(&cbor_payload, 1U);
            append_cbor_array(&cbor_payload, 4U);
            append_cbor_bytes(&cbor_payload, protected_header);
            append_cbor_map(&cbor_payload, 4U);
            append_cbor_text(&cbor_payload, "public_key_der");
            append_cbor_bytes(&cbor_payload, public_key_der);
            append_cbor_text(&cbor_payload, "claim_ref");
            append_cbor_text(&cbor_payload, "claims[39]");
            append_cbor_text(&cbor_payload, "claim_reference");
            append_cbor_text(&cbor_payload, "c2pa.claim.bad");
            append_cbor_text(&cbor_payload, "claim_uri");
            append_cbor_text(&cbor_payload, "https://example.test/asset?jumbf="
                                            "c2pa.claim.bad");
            append_cbor_null(&cbor_payload);
            append_cbor_bytes(&cbor_payload, raw_sig);
        }
    }
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_bad_jumb
        = make_claim_jumb_box("c2pa.claim.bad",
                              std::span<const std::byte>(bad_claim.data(),
                                                         bad_claim.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_bad_jumb.begin(),
                        claim_bad_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadPluralReferencePrecedenceSkipsGenericFallback)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 41U);
    for (uint32_t i = 0U; i < 40U; ++i) {
        append_cbor_map(&cbor_payload, (i == 0U) ? 2U : 1U);
        append_cbor_text(&cbor_payload, "claim");
        const uint8_t value = static_cast<uint8_t>((i % 253U) + 1U);
        const std::array<std::byte, 4U> claim_value = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { value },
        };
        append_cbor_bytes(&cbor_payload,
                          std::span<const std::byte>(claim_value.data(),
                                                     claim_value.size()));
        if (i == 0U) {
            append_cbor_text(&cbor_payload, "signatures");
            append_cbor_array(&cbor_payload, 1U);
            append_cbor_array(&cbor_payload, 4U);
            append_cbor_bytes(&cbor_payload, protected_header);
            append_cbor_map(&cbor_payload, 2U);
            append_cbor_text(&cbor_payload, "public_key_der");
            append_cbor_bytes(&cbor_payload, public_key_der);
            append_cbor_text(&cbor_payload, "references");
            append_cbor_array(&cbor_payload, 2U);
            append_cbor_text(&cbor_payload, "c2pa.claim.bad");
            append_cbor_text(&cbor_payload, "https://example.test/asset?jumbf="
                                            "c2pa.claim.bad");
            append_cbor_null(&cbor_payload);
            append_cbor_bytes(&cbor_payload, raw_sig);
        }
    }
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_bad_jumb
        = make_claim_jumb_box("c2pa.claim.bad",
                              std::span<const std::byte>(bad_claim.data(),
                                                         bad_claim.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_bad_jumb.begin(),
                        claim_bad_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadUnresolvedReferenceSkipsGenericFallback)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_text(&cbor_payload, "claims[999]");
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.missing");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/asset?jumbf=c2pa.claim.missing");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::NotImplemented);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadHyphenReferenceUnresolvedSkipsGenericFallback)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim-ref-index");
    append_cbor_i64(&cbor_payload, 999);
    append_cbor_text(&cbor_payload, "claim-reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.missing");
    append_cbor_text(&cbor_payload, "claim-uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/asset?jumbf=c2pa.claim.missing");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::NotImplemented);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadPercentEncodedClaimReference)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 41U);
    for (uint32_t i = 0U; i < 40U; ++i) {
        append_cbor_map(&cbor_payload, (i == 0U) ? 2U : 1U);
        append_cbor_text(&cbor_payload, "claim");
        const uint8_t value = static_cast<uint8_t>((i % 253U) + 1U);
        const std::array<std::byte, 4U> claim_value = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { value },
        };
        append_cbor_bytes(&cbor_payload,
                          std::span<const std::byte>(claim_value.data(),
                                                     claim_value.size()));
        if (i == 0U) {
            append_cbor_text(&cbor_payload, "signatures");
            append_cbor_array(&cbor_payload, 1U);
            append_cbor_array(&cbor_payload, 4U);
            append_cbor_bytes(&cbor_payload, protected_header);
            append_cbor_map(&cbor_payload, 2U);
            append_cbor_text(&cbor_payload, "public_key_der");
            append_cbor_bytes(&cbor_payload, public_key_der);
            append_cbor_text(&cbor_payload, "claim_ref");
            append_cbor_text(
                &cbor_payload,
                "https://example.test/media/%63%6C%61%69%6D%73%5B40%5D");
            append_cbor_null(&cbor_payload);
            append_cbor_bytes(&cbor_payload, raw_sig);
        }
    }
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadPercentEncodedJumbfLabel)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 41U);
    for (uint32_t i = 0U; i < 40U; ++i) {
        append_cbor_map(&cbor_payload, (i == 0U) ? 2U : 1U);
        append_cbor_text(&cbor_payload, "claim");
        const uint8_t value = static_cast<uint8_t>((i % 253U) + 1U);
        const std::array<std::byte, 4U> claim_value = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { value },
        };
        append_cbor_bytes(&cbor_payload,
                          std::span<const std::byte>(claim_value.data(),
                                                     claim_value.size()));
        if (i == 0U) {
            append_cbor_text(&cbor_payload, "signatures");
            append_cbor_array(&cbor_payload, 1U);
            append_cbor_array(&cbor_payload, 4U);
            append_cbor_bytes(&cbor_payload, protected_header);
            append_cbor_map(&cbor_payload, 3U);
            append_cbor_text(&cbor_payload, "public_key_der");
            append_cbor_bytes(&cbor_payload, public_key_der);
            append_cbor_text(&cbor_payload, "claim_reference");
            append_cbor_text(&cbor_payload, "c2pa.claim.bad");
            append_cbor_text(&cbor_payload, "claim_uri");
            append_cbor_text(
                &cbor_payload,
                "https://example.test/asset#jumbf=%63%32%70%61%2E%63%6C%61%69%"
                "6D%2E%67%6F%6F%64");
            append_cbor_null(&cbor_payload);
            append_cbor_bytes(&cbor_payload, raw_sig);
        }
    }
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_bad_jumb
        = make_claim_jumb_box("c2pa.claim.bad",
                              std::span<const std::byte>(bad_claim.data(),
                                                         bad_claim.size()));
    const std::vector<std::byte> claim_good_jumb
        = make_claim_jumb_box("c2pa.claim.good",
                              std::span<const std::byte>(target_claim.data(),
                                                         target_claim.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_bad_jumb.begin(),
                        claim_bad_jumb.end());
    root_payload.insert(root_payload.end(), claim_good_jumb.begin(),
                        claim_good_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadQueryStyleClaimIndexReference)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 41U);
    for (uint32_t i = 0U; i < 40U; ++i) {
        append_cbor_map(&cbor_payload, (i == 0U) ? 2U : 1U);
        append_cbor_text(&cbor_payload, "claim");
        const uint8_t value = static_cast<uint8_t>((i % 253U) + 1U);
        const std::array<std::byte, 4U> bad_claim = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { value },
        };
        append_cbor_bytes(&cbor_payload,
                          std::span<const std::byte>(bad_claim.data(),
                                                     bad_claim.size()));
        if (i == 0U) {
            append_cbor_text(&cbor_payload, "signatures");
            append_cbor_array(&cbor_payload, 1U);
            append_cbor_array(&cbor_payload, 4U);
            append_cbor_bytes(&cbor_payload, protected_header);
            append_cbor_map(&cbor_payload, 3U);
            append_cbor_text(&cbor_payload, "public_key_der");
            append_cbor_bytes(&cbor_payload, public_key_der);
            append_cbor_text(&cbor_payload, "claim-reference");
            append_cbor_text(&cbor_payload, "c2pa.claim.missing");
            append_cbor_text(&cbor_payload, "claim-uri");
            append_cbor_text(&cbor_payload,
                             "https://example.test/media?claim-index=40");
            append_cbor_null(&cbor_payload);
            append_cbor_bytes(&cbor_payload, raw_sig);
        }
    }
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadConflictingQueryLabelUriDeterministic)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_text(&cbor_payload, "https://example.test/media?claim-index=1");
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.bad");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/media?jumbf=c2pa.claim.bad");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_bad_jumb
        = make_claim_jumb_box("c2pa.claim.bad",
                              std::span<const std::byte>(bad_claim.data(),
                                                         bad_claim.size()));
    const std::vector<std::byte> claim_good_jumb
        = make_claim_jumb_box("c2pa.claim.good",
                              std::span<const std::byte>(target_claim.data(),
                                                         target_claim.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_bad_jumb.begin(),
                        claim_bad_jumb.end());
    root_payload.insert(root_payload.end(), claim_good_jumb.begin(),
                        claim_good_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    MetaStore store0;
    JumbfDecodeOptions options0;
    options0.verify_c2pa    = true;
    options0.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result0
        = decode_jumbf_payload(payload, store0, EntryFlags::None, options0);
    EXPECT_EQ(result0.status, JumbfDecodeStatus::Ok);
    store0.finalize();
    const std::string status0 = read_jumbf_field_text(store0,
                                                      "c2pa.verify.status");
    const std::string reason0
        = read_jumbf_field_text(store0, "c2pa.verify.chain_reason");

    MetaStore store1;
    JumbfDecodeOptions options1;
    options1.verify_c2pa    = true;
    options1.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result1
        = decode_jumbf_payload(payload, store1, EntryFlags::None, options1);
    EXPECT_EQ(result1.status, JumbfDecodeStatus::Ok);
    store1.finalize();
    const std::string status1 = read_jumbf_field_text(store1,
                                                      "c2pa.verify.status");
    const std::string reason1
        = read_jumbf_field_text(store1, "c2pa.verify.chain_reason");

    MetaStore store2;
    JumbfDecodeOptions options2;
    options2.verify_c2pa    = true;
    options2.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result2
        = decode_jumbf_payload(payload, store2, EntryFlags::None, options2);
    EXPECT_EQ(result2.status, JumbfDecodeStatus::Ok);
    store2.finalize();
    const std::string status2 = read_jumbf_field_text(store2,
                                                      "c2pa.verify.status");
    const std::string reason2
        = read_jumbf_field_text(store2, "c2pa.verify.chain_reason");

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result0.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(result1.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(result2.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(status0, "signature_verified_only");
    EXPECT_EQ(status1, status0);
    EXPECT_EQ(status2, status0);
    EXPECT_EQ(reason1, reason0);
    EXPECT_EQ(reason2, reason0);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result0.verify_status, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(result1.verify_status, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(result2.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result0.verify_status, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(result1.verify_status, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(result2.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadClaimRefIdAndReferenceClaimIdDeterministic)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim_ref_id");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "reference");
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim_id");
    append_cbor_text(&cbor_payload, "1");
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/media?jumbf=c2pa.claim.missing");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    auto decode_once = [&](std::string* out_status,
                           std::string* out_reason) -> C2paVerifyStatus {
        MetaStore local_store;
        JumbfDecodeOptions options;
        options.verify_c2pa    = true;
        options.verify_backend = C2paVerifyBackend::OpenSsl;
        const JumbfDecodeResult result
            = decode_jumbf_payload(payload, local_store, EntryFlags::None,
                                   options);
        EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
        local_store.finalize();
        if (out_status) {
            *out_status = read_jumbf_field_text(local_store,
                                                "c2pa.verify.status");
        }
        if (out_reason) {
            *out_reason = read_jumbf_field_text(local_store,
                                                "c2pa.verify.chain_reason");
        }
        return result.verify_status;
    };

    std::string s0;
    std::string r0;
    const C2paVerifyStatus v0 = decode_once(&s0, &r0);
    std::string s1;
    std::string r1;
    const C2paVerifyStatus v1 = decode_once(&s1, &r1);
    std::string s2;
    std::string r2;
    const C2paVerifyStatus v2 = decode_once(&s2, &r2);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(v0, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v1, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v2, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(s0, "signature_verified_only");
    EXPECT_EQ(s1, s0);
    EXPECT_EQ(s2, s0);
    EXPECT_EQ(r1, r0);
    EXPECT_EQ(r2, r0);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(v0, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v1, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v2, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(v0, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v1, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v2, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadClaimRefIdUnresolvedNoFallback)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim_ref_id");
    append_cbor_text(&cbor_payload, "999");
    append_cbor_text(&cbor_payload, "reference");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim_id");
    append_cbor_i64(&cbor_payload, 888);
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::NotImplemented);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadNestedReferenceMapQueryLabelUri)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_text(&cbor_payload, "https://example.test/media?claim-index=1");
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.bad");
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/media?jumbf=c2pa.claim.bad");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_bad_jumb
        = make_claim_jumb_box("c2pa.claim.bad",
                              std::span<const std::byte>(bad_claim.data(),
                                                         bad_claim.size()));
    const std::vector<std::byte> claim_good_jumb
        = make_claim_jumb_box("c2pa.claim.good",
                              std::span<const std::byte>(target_claim.data(),
                                                         target_claim.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_bad_jumb.begin(),
                        claim_bad_jumb.end());
    root_payload.insert(root_payload.end(), claim_good_jumb.begin(),
                        claim_good_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadNestedReferenceMapLinkField)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "link");
    append_cbor_text(&cbor_payload, "https://example.test/media?claim-index=1");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadNestedReferenceMapIndexLabelHrefConflictDeterministic)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "index");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.bad");
    append_cbor_text(&cbor_payload, "href");
    append_cbor_text(&cbor_payload,
                     "https://example.test/media?jumbf=c2pa.claim.bad");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_bad_jumb
        = make_claim_jumb_box("c2pa.claim.bad",
                              std::span<const std::byte>(bad_claim.data(),
                                                         bad_claim.size()));
    const std::vector<std::byte> claim_good_jumb
        = make_claim_jumb_box("c2pa.claim.good",
                              std::span<const std::byte>(target_claim.data(),
                                                         target_claim.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_bad_jumb.begin(),
                        claim_bad_jumb.end());
    root_payload.insert(root_payload.end(), claim_good_jumb.begin(),
                        claim_good_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    MetaStore semantic_store;
    JumbfDecodeOptions semantic_options;
    semantic_options.verify_c2pa    = true;
    semantic_options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult semantic_result
        = decode_jumbf_payload(payload, semantic_store, EntryFlags::None,
                               semantic_options);
    EXPECT_EQ(semantic_result.status, JumbfDecodeStatus::Ok);
    semantic_store.finalize();
    EXPECT_EQ(read_jumbf_field_u64(
                  semantic_store,
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              1U);

    auto decode_once = [&](std::string* out_status,
                           std::string* out_reason) -> C2paVerifyStatus {
        MetaStore local_store;
        JumbfDecodeOptions options;
        options.verify_c2pa    = true;
        options.verify_backend = C2paVerifyBackend::OpenSsl;
        const JumbfDecodeResult result
            = decode_jumbf_payload(payload, local_store, EntryFlags::None,
                                   options);
        EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
        local_store.finalize();
        if (out_status) {
            *out_status = read_jumbf_field_text(local_store,
                                                "c2pa.verify.status");
        }
        if (out_reason) {
            *out_reason = read_jumbf_field_text(local_store,
                                                "c2pa.verify.chain_reason");
        }
        return result.verify_status;
    };

    std::string s0;
    std::string r0;
    const C2paVerifyStatus v0 = decode_once(&s0, &r0);
    std::string s1;
    std::string r1;
    const C2paVerifyStatus v1 = decode_once(&s1, &r1);
    std::string s2;
    std::string r2;
    const C2paVerifyStatus v2 = decode_once(&s2, &r2);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(v0, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v1, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v2, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(s0, "signature_verified_only");
    EXPECT_EQ(s1, s0);
    EXPECT_EQ(s2, s0);
    EXPECT_EQ(r1, r0);
    EXPECT_EQ(r2, r0);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(v0, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v1, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v2, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(v0, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v1, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v2, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadNestedReferenceMapIndexLabelHrefConsistent)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "index");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.good");
    append_cbor_text(&cbor_payload, "href");
    append_cbor_text(&cbor_payload,
                     "https://example.test/media?jumbf=c2pa.claim.good");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_bad_jumb
        = make_claim_jumb_box("c2pa.claim.bad",
                              std::span<const std::byte>(bad_claim.data(),
                                                         bad_claim.size()));
    const std::vector<std::byte> claim_good_jumb
        = make_claim_jumb_box("c2pa.claim.good",
                              std::span<const std::byte>(target_claim.data(),
                                                         target_claim.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_bad_jumb.begin(),
                        claim_bad_jumb.end());
    root_payload.insert(root_payload.end(), claim_good_jumb.begin(),
                        claim_good_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    MetaStore semantic_store;
    JumbfDecodeOptions semantic_options;
    semantic_options.verify_c2pa    = true;
    semantic_options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult semantic_result
        = decode_jumbf_payload(payload, semantic_store, EntryFlags::None,
                               semantic_options);
    EXPECT_EQ(semantic_result.status, JumbfDecodeStatus::Ok);
    semantic_store.finalize();
    EXPECT_EQ(read_jumbf_field_u64(
                  semantic_store,
                  "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
              0U);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadDuplicateOverlappingRefsDeterministic)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 5U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "claim-reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.good");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload, "https://example.test/media?claim_ref=1");
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/media?jumbf=c2pa.claim.good");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_good_jumb
        = make_claim_jumb_box("c2pa.claim.good",
                              std::span<const std::byte>(target_claim.data(),
                                                         target_claim.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_good_jumb.begin(),
                        claim_good_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    auto decode_once = [&](std::string* out_status,
                           std::string* out_reason) -> C2paVerifyStatus {
        MetaStore local_store;
        JumbfDecodeOptions options;
        options.verify_c2pa    = true;
        options.verify_backend = C2paVerifyBackend::OpenSsl;
        const JumbfDecodeResult result
            = decode_jumbf_payload(payload, local_store, EntryFlags::None,
                                   options);
        EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
        local_store.finalize();
        if (out_status) {
            *out_status = read_jumbf_field_text(local_store,
                                                "c2pa.verify.status");
        }
        if (out_reason) {
            *out_reason = read_jumbf_field_text(local_store,
                                                "c2pa.verify.chain_reason");
        }
        return result.verify_status;
    };

    std::string s0;
    std::string r0;
    const C2paVerifyStatus v0 = decode_once(&s0, &r0);
    std::string s1;
    std::string r1;
    const C2paVerifyStatus v1 = decode_once(&s1, &r1);
    std::string s2;
    std::string r2;
    const C2paVerifyStatus v2 = decode_once(&s2, &r2);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(v0, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v1, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v2, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(s0, "signature_verified_only");
    EXPECT_EQ(s1, s0);
    EXPECT_EQ(s2, s0);
    EXPECT_EQ(r1, r0);
    EXPECT_EQ(r2, r0);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(v0, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v1, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v2, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(v0, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v1, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v2, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadUnresolvedNestedRefsNoFallback)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_text(&cbor_payload, "claims[999]");
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.missing");

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/media?claim-index=999");
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/media?jumbf=c2pa.claim.missing");

    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::NotImplemented);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadPercentEncodedQueryIndexReference)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/media?claim%2Dindex%3D0");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadFromReferenceMapEntries)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 3U);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_i64(&cbor_payload, 1);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.bad");

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/asset?jumbf=c2pa.claim.good");

    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_bad_jumb
        = make_claim_jumb_box("c2pa.claim.bad",
                              std::span<const std::byte>(bad_claim.data(),
                                                         bad_claim.size()));
    const std::vector<std::byte> claim_good_jumb
        = make_claim_jumb_box("c2pa.claim.good",
                              std::span<const std::byte>(target_claim.data(),
                                                         target_claim.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_bad_jumb.begin(),
                        claim_bad_jumb.end());
    root_payload.insert(root_payload.end(), claim_good_jumb.begin(),
                        claim_good_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadFromReferenceMapClaimsArray)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_i64(&cbor_payload, 999);

    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadFromReferenceMapIndexAndUriKeys)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig_structure.empty());

    const std::vector<std::byte> der_sig = ecdsa_sign_sha256(key,
                                                             sig_structure);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig.empty());

    const std::vector<std::byte> raw_sig = ecdsa_der_to_cose_raw_p256(der_sig);
    ASSERT_EQ(raw_sig.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);

    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "reference");
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "index");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload, "https://example.test/asset?jumbf="
                                    "c2pa.claim.missing");

    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadMultiClaimMultiSignatureQueryIndexReference)
{
    const std::vector<std::byte> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x2B },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig0 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim1.data(), claim1.size()));
    const std::vector<std::byte> sig1 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim0.data(), claim0.size()));
    ASSERT_FALSE(sig0.empty());
    ASSERT_FALSE(sig1.empty());

    const std::vector<std::byte> der_sig0 = ecdsa_sign_sha256(key, sig0);
    const std::vector<std::byte> der_sig1 = ecdsa_sign_sha256(key, sig1);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig0.empty());
    ASSERT_FALSE(der_sig1.empty());

    const std::vector<std::byte> raw_sig0 = ecdsa_der_to_cose_raw_p256(
        der_sig0);
    const std::vector<std::byte> raw_sig1 = ecdsa_der_to_cose_raw_p256(
        der_sig1);
    ASSERT_EQ(raw_sig0.size(), 64U);
    ASSERT_EQ(raw_sig1.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig0(64U, std::byte { 0x00 });
    const std::vector<std::byte> raw_sig1(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0.data(), claim0.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim-uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/a?claim-index=1&jumbf="
                     "c2pa.claim.missing0");
    append_cbor_text(&cbor_payload, "claim-reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.missing0");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig0);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1.data(), claim1.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim-uri");
    append_cbor_text(&cbor_payload, "https://example.test/b?claim_ref=0&jumbf="
                                    "c2pa.claim.missing1");
    append_cbor_text(&cbor_payload, "claim-reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.missing1");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig1);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadMultiClaimMultiSignatureNestedReferencesMaps)
{
    const std::vector<std::byte> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x2B },
    };
    const std::vector<std::byte> claim0_bad = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> claim1_bad = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x02 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig0 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim1.data(), claim1.size()));
    const std::vector<std::byte> sig1 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim0.data(), claim0.size()));
    ASSERT_FALSE(sig0.empty());
    ASSERT_FALSE(sig1.empty());

    const std::vector<std::byte> der_sig0 = ecdsa_sign_sha256(key, sig0);
    const std::vector<std::byte> der_sig1 = ecdsa_sign_sha256(key, sig1);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig0.empty());
    ASSERT_FALSE(der_sig1.empty());

    const std::vector<std::byte> raw_sig0 = ecdsa_der_to_cose_raw_p256(
        der_sig0);
    const std::vector<std::byte> raw_sig1 = ecdsa_der_to_cose_raw_p256(
        der_sig1);
    ASSERT_EQ(raw_sig0.size(), 64U);
    ASSERT_EQ(raw_sig1.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig0(64U, std::byte { 0x00 });
    const std::vector<std::byte> raw_sig1(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0_bad.data(),
                                                 claim0_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 2U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_text(&cbor_payload, "https://example.test/a?claim-index=1");
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.bad0");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/a?jumbf=c2pa.claim.bad0");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig0);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1_bad.data(),
                                                 claim1_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 2U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim-uri");
    append_cbor_text(&cbor_payload, "https://example.test/b?claim_ref=0");
    append_cbor_text(&cbor_payload, "claim-reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.bad1");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/b?jumbf=c2pa.claim.bad1");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig1);

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_bad0_jumb
        = make_claim_jumb_box("c2pa.claim.bad0",
                              std::span<const std::byte>(claim0_bad.data(),
                                                         claim0_bad.size()));
    const std::vector<std::byte> claim_bad1_jumb
        = make_claim_jumb_box("c2pa.claim.bad1",
                              std::span<const std::byte>(claim1_bad.data(),
                                                         claim1_bad.size()));
    const std::vector<std::byte> claim_good0_jumb
        = make_claim_jumb_box("c2pa.claim.good0",
                              std::span<const std::byte>(claim0.data(),
                                                         claim0.size()));
    const std::vector<std::byte> claim_good1_jumb
        = make_claim_jumb_box("c2pa.claim.good1",
                              std::span<const std::byte>(claim1.data(),
                                                         claim1.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_bad0_jumb.begin(),
                        claim_bad0_jumb.end());
    root_payload.insert(root_payload.end(), claim_bad1_jumb.begin(),
                        claim_bad1_jumb.end());
    root_payload.insert(root_payload.end(), claim_good0_jumb.begin(),
                        claim_good0_jumb.end());
    root_payload.insert(root_payload.end(), claim_good1_jumb.begin(),
                        claim_good1_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    auto decode_once = [&](std::string* out_status,
                           std::string* out_reason) -> C2paVerifyStatus {
        MetaStore local_store;
        JumbfDecodeOptions options;
        options.verify_c2pa    = true;
        options.verify_backend = C2paVerifyBackend::OpenSsl;
        const JumbfDecodeResult result
            = decode_jumbf_payload(payload, local_store, EntryFlags::None,
                                   options);
        EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
        local_store.finalize();
        if (out_status) {
            *out_status = read_jumbf_field_text(local_store,
                                                "c2pa.verify.status");
        }
        if (out_reason) {
            *out_reason = read_jumbf_field_text(local_store,
                                                "c2pa.verify.chain_reason");
        }
        return result.verify_status;
    };

    std::string s0;
    std::string r0;
    const C2paVerifyStatus v0 = decode_once(&s0, &r0);
    std::string s1;
    std::string r1;
    const C2paVerifyStatus v1 = decode_once(&s1, &r1);
    std::string s2;
    std::string r2;
    const C2paVerifyStatus v2 = decode_once(&s2, &r2);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(v0, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v1, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v2, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(s0, "signature_verified_only");
    EXPECT_EQ(s1, s0);
    EXPECT_EQ(s2, s0);
    EXPECT_EQ(r1, r0);
    EXPECT_EQ(r2, r0);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(v0, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v1, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v2, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(v0, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v1, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v2, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadMultiClaimMultiSignatureNestedIdRefs)
{
    const std::vector<std::byte> claim0 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> claim1 = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x2B },
    };
    const std::vector<std::byte> claim0_bad = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> claim1_bad = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x02 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig0 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim1.data(), claim1.size()));
    const std::vector<std::byte> sig1 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim0.data(), claim0.size()));
    ASSERT_FALSE(sig0.empty());
    ASSERT_FALSE(sig1.empty());

    const std::vector<std::byte> der_sig0 = ecdsa_sign_sha256(key, sig0);
    const std::vector<std::byte> der_sig1 = ecdsa_sign_sha256(key, sig1);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig0.empty());
    ASSERT_FALSE(der_sig1.empty());

    const std::vector<std::byte> raw_sig0 = ecdsa_der_to_cose_raw_p256(
        der_sig0);
    const std::vector<std::byte> raw_sig1 = ecdsa_der_to_cose_raw_p256(
        der_sig1);
    ASSERT_EQ(raw_sig0.size(), 64U);
    ASSERT_EQ(raw_sig1.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig0(64U, std::byte { 0x00 });
    const std::vector<std::byte> raw_sig1(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0_bad.data(),
                                                 claim0_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 2U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim_ref_id");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.bad0");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "reference");
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim_id");
    append_cbor_text(&cbor_payload, "1");
    append_cbor_text(&cbor_payload, "uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/a?jumbf=c2pa.claim.bad0");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig0);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1_bad.data(),
                                                 claim1_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 2U);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim-reference-id");
    append_cbor_text(&cbor_payload, "0");
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.bad1");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "reference");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim_id");
    append_cbor_i64(&cbor_payload, 0);
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig1);

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));
    const std::vector<std::byte> claim_bad0_jumb
        = make_claim_jumb_box("c2pa.claim.bad0",
                              std::span<const std::byte>(claim0_bad.data(),
                                                         claim0_bad.size()));
    const std::vector<std::byte> claim_bad1_jumb
        = make_claim_jumb_box("c2pa.claim.bad1",
                              std::span<const std::byte>(claim1_bad.data(),
                                                         claim1_bad.size()));
    const std::vector<std::byte> claim_good0_jumb
        = make_claim_jumb_box("c2pa.claim.good0",
                              std::span<const std::byte>(claim0.data(),
                                                         claim0.size()));
    const std::vector<std::byte> claim_good1_jumb
        = make_claim_jumb_box("c2pa.claim.good1",
                              std::span<const std::byte>(claim1.data(),
                                                         claim1.size()));

    std::vector<std::byte> root_payload;
    root_payload.insert(root_payload.end(), claim_bad0_jumb.begin(),
                        claim_bad0_jumb.end());
    root_payload.insert(root_payload.end(), claim_bad1_jumb.begin(),
                        claim_bad1_jumb.end());
    root_payload.insert(root_payload.end(), claim_good0_jumb.begin(),
                        claim_good0_jumb.end());
    root_payload.insert(root_payload.end(), claim_good1_jumb.begin(),
                        claim_good1_jumb.end());
    root_payload.insert(root_payload.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(root_payload.data(), root_payload.size()));

    auto decode_once = [&](std::string* out_status,
                           std::string* out_reason) -> C2paVerifyStatus {
        MetaStore local_store;
        JumbfDecodeOptions options;
        options.verify_c2pa    = true;
        options.verify_backend = C2paVerifyBackend::OpenSsl;
        const JumbfDecodeResult result
            = decode_jumbf_payload(payload, local_store, EntryFlags::None,
                                   options);
        EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
        local_store.finalize();
        if (out_status) {
            *out_status = read_jumbf_field_text(local_store,
                                                "c2pa.verify.status");
        }
        if (out_reason) {
            *out_reason = read_jumbf_field_text(local_store,
                                                "c2pa.verify.chain_reason");
        }
        return result.verify_status;
    };

    std::string s0;
    std::string r0;
    const C2paVerifyStatus v0 = decode_once(&s0, &r0);
    std::string s1;
    std::string r1;
    const C2paVerifyStatus v1 = decode_once(&s1, &r1);
    std::string s2;
    std::string r2;
    const C2paVerifyStatus v2 = decode_once(&s2, &r2);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(v0, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v1, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v2, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(s0, "signature_verified_only");
    EXPECT_EQ(s1, s0);
    EXPECT_EQ(s2, s0);
    EXPECT_EQ(r1, r0);
    EXPECT_EQ(r2, r0);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(v0, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v1, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v2, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(v0, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v1, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v2, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadMultiClaimMultiSignatureIdRefsNoFallback)
{
    const std::vector<std::byte> target_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> bad_claim = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig0 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    const std::vector<std::byte> sig1 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(target_claim.data(), target_claim.size()));
    ASSERT_FALSE(sig0.empty());
    ASSERT_FALSE(sig1.empty());

    const std::vector<std::byte> der_sig0 = ecdsa_sign_sha256(key, sig0);
    const std::vector<std::byte> der_sig1 = ecdsa_sign_sha256(key, sig1);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig0.empty());
    ASSERT_FALSE(der_sig1.empty());

    const std::vector<std::byte> raw_sig0 = ecdsa_der_to_cose_raw_p256(
        der_sig0);
    const std::vector<std::byte> raw_sig1 = ecdsa_der_to_cose_raw_p256(
        der_sig1);
    ASSERT_EQ(raw_sig0.size(), 64U);
    ASSERT_EQ(raw_sig1.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig0(64U, std::byte { 0x00 });
    const std::vector<std::byte> raw_sig1(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 2U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim_ref_id");
    append_cbor_text(&cbor_payload, "999");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "reference");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim_id");
    append_cbor_i64(&cbor_payload, 888);
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig0);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(bad_claim.data(),
                                                 bad_claim.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "references");
    append_cbor_array(&cbor_payload, 2U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim-reference-id");
    append_cbor_i64(&cbor_payload, 777);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "reference");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim_id");
    append_cbor_text(&cbor_payload, "666");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig1);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(target_claim.data(),
                                                 target_claim.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::NotImplemented);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadMultiClaimMultiSignatureCrossManifestMixed)
{
    const std::vector<std::byte> claim0_good = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> claim1_good = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x2B },
    };
    const std::vector<std::byte> claim0_bad = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> claim1_bad = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x02 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig0 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim0_good.data(), claim0_good.size()));
    const std::vector<std::byte> sig1 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim1_good.data(), claim1_good.size()));
    ASSERT_FALSE(sig0.empty());
    ASSERT_FALSE(sig1.empty());

    const std::vector<std::byte> der_sig0 = ecdsa_sign_sha256(key, sig0);
    const std::vector<std::byte> der_sig1 = ecdsa_sign_sha256(key, sig1);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig0.empty());
    ASSERT_FALSE(der_sig1.empty());

    const std::vector<std::byte> raw_sig0 = ecdsa_der_to_cose_raw_p256(
        der_sig0);
    const std::vector<std::byte> raw_sig1 = ecdsa_der_to_cose_raw_p256(
        der_sig1);
    ASSERT_EQ(raw_sig0.size(), 64U);
    ASSERT_EQ(raw_sig1.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig0(64U, std::byte { 0x00 });
    const std::vector<std::byte> raw_sig1(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    // claims[0] + signature[0]
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0_bad.data(),
                                                 claim0_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_text(&cbor_payload, "claims[1]");
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.bad0");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/a?jumbf=c2pa.claim.good0");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig0);

    // claims[1] + signature[0]
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1_bad.data(),
                                                 claim1_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_i64(&cbor_payload, 0);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.bad1");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/b?jumbf=c2pa.claim.good1");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig1);

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));

    const std::vector<std::byte> claim_bad0_box
        = make_claim_jumb_box("c2pa.claim.bad0",
                              std::span<const std::byte>(claim0_bad.data(),
                                                         claim0_bad.size()));
    const std::vector<std::byte> claim_good0_box
        = make_claim_jumb_box("c2pa.claim.good0",
                              std::span<const std::byte>(claim0_good.data(),
                                                         claim0_good.size()));
    const std::vector<std::byte> claim_bad1_box
        = make_claim_jumb_box("c2pa.claim.bad1",
                              std::span<const std::byte>(claim1_bad.data(),
                                                         claim1_bad.size()));
    const std::vector<std::byte> claim_good1_box
        = make_claim_jumb_box("c2pa.claim.good1",
                              std::span<const std::byte>(claim1_good.data(),
                                                         claim1_good.size()));

    std::vector<std::byte> payload_bytes;
    payload_bytes.insert(payload_bytes.end(), claim_bad0_box.begin(),
                         claim_bad0_box.end());
    payload_bytes.insert(payload_bytes.end(), claim_good0_box.begin(),
                         claim_good0_box.end());
    payload_bytes.insert(payload_bytes.end(), claim_bad1_box.begin(),
                         claim_bad1_box.end());
    payload_bytes.insert(payload_bytes.end(), claim_good1_box.begin(),
                         claim_good1_box.end());
    payload_bytes.insert(payload_bytes.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(payload_bytes.data(), payload_bytes.size()));

    auto decode_once = [&](std::string* out_status,
                           std::string* out_reason) -> C2paVerifyStatus {
        MetaStore local_store;
        JumbfDecodeOptions options;
        options.verify_c2pa    = true;
        options.verify_backend = C2paVerifyBackend::OpenSsl;
        const JumbfDecodeResult result
            = decode_jumbf_payload(payload, local_store, EntryFlags::None,
                                   options);
        EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
        local_store.finalize();
        if (out_status) {
            *out_status = read_jumbf_field_text(local_store,
                                                "c2pa.verify.status");
        }
        if (out_reason) {
            *out_reason = read_jumbf_field_text(local_store,
                                                "c2pa.verify.chain_reason");
        }
        return result.verify_status;
    };

    std::string s0;
    std::string r0;
    const C2paVerifyStatus v0 = decode_once(&s0, &r0);
    std::string s1;
    std::string r1;
    const C2paVerifyStatus v1 = decode_once(&s1, &r1);
    std::string s2;
    std::string r2;
    const C2paVerifyStatus v2 = decode_once(&s2, &r2);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(v0, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v1, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v2, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(s0, "signature_verified_only");
    EXPECT_EQ(s1, s0);
    EXPECT_EQ(s2, s0);
    EXPECT_EQ(r1, r0);
    EXPECT_EQ(r2, r0);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(v0, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v1, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v2, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(v0, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v1, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v2, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadMultiClaimMultiSignatureExplicitPrecedence)
{
    const std::vector<std::byte> claim0_good = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> claim1_good = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x2B },
    };
    const std::vector<std::byte> claim0_bad = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> claim1_bad = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x02 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig0 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim0_good.data(), claim0_good.size()));
    const std::vector<std::byte> sig1 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim1_good.data(), claim1_good.size()));
    ASSERT_FALSE(sig0.empty());
    ASSERT_FALSE(sig1.empty());

    const std::vector<std::byte> der_sig0 = ecdsa_sign_sha256(key, sig0);
    const std::vector<std::byte> der_sig1 = ecdsa_sign_sha256(key, sig1);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig0.empty());
    ASSERT_FALSE(der_sig1.empty());

    const std::vector<std::byte> raw_sig0 = ecdsa_der_to_cose_raw_p256(
        der_sig0);
    const std::vector<std::byte> raw_sig1 = ecdsa_der_to_cose_raw_p256(
        der_sig1);
    ASSERT_EQ(raw_sig0.size(), 64U);
    ASSERT_EQ(raw_sig1.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig0(64U, std::byte { 0x00 });
    const std::vector<std::byte> raw_sig1(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    // claims[0] + signature[0]
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0_bad.data(),
                                                 claim0_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_text(&cbor_payload, "claims[1]");
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.bad0");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/a?jumbf=c2pa.claim.bad0");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig0);

    // claims[1] + signature[0]
    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1_bad.data(),
                                                 claim1_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 4U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "claim_ref");
    append_cbor_i64(&cbor_payload, 0);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.bad1");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/b?jumbf=c2pa.claim.bad1");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig1);

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));

    const std::vector<std::byte> claim_bad0_box
        = make_claim_jumb_box("c2pa.claim.bad0",
                              std::span<const std::byte>(claim0_bad.data(),
                                                         claim0_bad.size()));
    const std::vector<std::byte> claim_good0_box
        = make_claim_jumb_box("c2pa.claim.good0",
                              std::span<const std::byte>(claim0_good.data(),
                                                         claim0_good.size()));
    const std::vector<std::byte> claim_bad1_box
        = make_claim_jumb_box("c2pa.claim.bad1",
                              std::span<const std::byte>(claim1_bad.data(),
                                                         claim1_bad.size()));
    const std::vector<std::byte> claim_good1_box
        = make_claim_jumb_box("c2pa.claim.good1",
                              std::span<const std::byte>(claim1_good.data(),
                                                         claim1_good.size()));

    std::vector<std::byte> payload_bytes;
    payload_bytes.insert(payload_bytes.end(), claim_bad0_box.begin(),
                         claim_bad0_box.end());
    payload_bytes.insert(payload_bytes.end(), claim_good0_box.begin(),
                         claim_good0_box.end());
    payload_bytes.insert(payload_bytes.end(), claim_bad1_box.begin(),
                         claim_bad1_box.end());
    payload_bytes.insert(payload_bytes.end(), claim_good1_box.begin(),
                         claim_good1_box.end());
    payload_bytes.insert(payload_bytes.end(), cbor_box.begin(), cbor_box.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa",
        std::span<const std::byte>(payload_bytes.data(), payload_bytes.size()));

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadMultiClaimMultiSignatureReferenceFieldVariantsDeterministic)
{
    const std::vector<std::byte> claim0_bad = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> claim1_good = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x62 },
        std::byte { 0x2B },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig0 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim1_good.data(), claim1_good.size()));
    const std::vector<std::byte> sig1 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim0_bad.data(), claim0_bad.size()));
    ASSERT_FALSE(sig0.empty());
    ASSERT_FALSE(sig1.empty());

    const std::vector<std::byte> der_sig0 = ecdsa_sign_sha256(key, sig0);
    const std::vector<std::byte> der_sig1 = ecdsa_sign_sha256(key, sig1);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig0.empty());
    ASSERT_FALSE(der_sig1.empty());

    const std::vector<std::byte> raw_sig0 = ecdsa_der_to_cose_raw_p256(
        der_sig0);
    const std::vector<std::byte> raw_sig1 = ecdsa_der_to_cose_raw_p256(
        der_sig1);
    ASSERT_EQ(raw_sig0.size(), 64U);
    ASSERT_EQ(raw_sig1.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig0(64U, std::byte { 0x00 });
    const std::vector<std::byte> raw_sig1(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 2U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim0_bad.data(),
                                                 claim0_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 5U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "reference_id");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "reference_index");
    append_cbor_text(&cbor_payload, "0");
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.missing0");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/a?jumbf=c2pa.claim.missing0");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig0);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim1_good.data(),
                                                 claim1_good.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 5U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "reference-id");
    append_cbor_text(&cbor_payload, "0");
    append_cbor_text(&cbor_payload, "reference_index");
    append_cbor_i64(&cbor_payload, 1);
    append_cbor_text(&cbor_payload, "claim_reference");
    append_cbor_text(&cbor_payload, "c2pa.claim.missing1");
    append_cbor_text(&cbor_payload, "claim_uri");
    append_cbor_text(&cbor_payload,
                     "https://example.test/b?jumbf=c2pa.claim.missing1");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig1);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    auto decode_once = [&](std::string* out_status,
                           std::string* out_reason) -> C2paVerifyStatus {
        MetaStore local_store;
        JumbfDecodeOptions options;
        options.verify_c2pa    = true;
        options.verify_backend = C2paVerifyBackend::OpenSsl;
        const JumbfDecodeResult result
            = decode_jumbf_payload(payload, local_store, EntryFlags::None,
                                   options);
        EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
        local_store.finalize();
        if (out_status) {
            *out_status = read_jumbf_field_text(local_store,
                                                "c2pa.verify.status");
        }
        if (out_reason) {
            *out_reason = read_jumbf_field_text(local_store,
                                                "c2pa.verify.chain_reason");
        }
        return result.verify_status;
    };

    std::string s0;
    std::string r0;
    const C2paVerifyStatus v0 = decode_once(&s0, &r0);
    std::string s1;
    std::string r1;
    const C2paVerifyStatus v1 = decode_once(&s1, &r1);
    std::string s2;
    std::string r2;
    const C2paVerifyStatus v2 = decode_once(&s2, &r2);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(v0, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v1, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(v2, C2paVerifyStatus::SignatureVerifiedOnly);
    EXPECT_EQ(s0, "signature_verified_only");
    EXPECT_EQ(s1, s0);
    EXPECT_EQ(s2, s0);
    EXPECT_EQ(r1, r0);
    EXPECT_EQ(r2, r0);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(v0, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v1, C2paVerifyStatus::BackendUnavailable);
    EXPECT_EQ(v2, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(v0, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v1, C2paVerifyStatus::DisabledByBuild);
    EXPECT_EQ(v2, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode,
     C2paVerifyCoseDetachedPayloadMultiClaimMultiSignatureReferenceFieldVariantsNoFallback)
{
    const std::vector<std::byte> claim_bad = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> claim_good = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x2A },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig0 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim_good.data(), claim_good.size()));
    const std::vector<std::byte> sig1 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim_good.data(), claim_good.size()));
    ASSERT_FALSE(sig0.empty());
    ASSERT_FALSE(sig1.empty());

    const std::vector<std::byte> der_sig0 = ecdsa_sign_sha256(key, sig0);
    const std::vector<std::byte> der_sig1 = ecdsa_sign_sha256(key, sig1);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig0.empty());
    ASSERT_FALSE(der_sig1.empty());

    const std::vector<std::byte> raw_sig0 = ecdsa_der_to_cose_raw_p256(
        der_sig0);
    const std::vector<std::byte> raw_sig1 = ecdsa_der_to_cose_raw_p256(
        der_sig1);
    ASSERT_EQ(raw_sig0.size(), 64U);
    ASSERT_EQ(raw_sig1.size(), 64U);
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig0(64U, std::byte { 0x00 });
    const std::vector<std::byte> raw_sig1(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 3U);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim_bad.data(),
                                                 claim_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "reference_id");
    append_cbor_i64(&cbor_payload, 999);
    append_cbor_text(&cbor_payload, "reference_index");
    append_cbor_text(&cbor_payload, "888");
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig0);

    append_cbor_map(&cbor_payload, 2U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim_bad.data(),
                                                 claim_bad.size()));
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 3U);
    append_cbor_text(&cbor_payload, "public_key_der");
    append_cbor_bytes(&cbor_payload, public_key_der);
    append_cbor_text(&cbor_payload, "reference-id");
    append_cbor_text(&cbor_payload, "777");
    append_cbor_text(&cbor_payload, "ref_index");
    append_cbor_i64(&cbor_payload, 666);
    append_cbor_null(&cbor_payload);
    append_cbor_bytes(&cbor_payload, raw_sig1);

    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claim");
    append_cbor_bytes(&cbor_payload,
                      std::span<const std::byte>(claim_good.data(),
                                                 claim_good.size()));

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::NotImplemented);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseDetachedPayloadMultiClaimMultiSignatureLayout)
{
    const std::vector<std::byte> claim0_cbor = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    const std::vector<std::byte> claim1_cbor = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x02 },
    };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EVP_PKEY* key = nullptr;
    ASSERT_TRUE(make_ec_p256_keypair(&key));
    const std::vector<std::byte> public_key_der = public_key_der_from_key(key);
    ASSERT_FALSE(public_key_der.empty());

    const std::vector<std::byte> sig_structure0 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim0_cbor.data(), claim0_cbor.size()));
    const std::vector<std::byte> sig_structure1 = make_cose_sig_structure(
        std::span<const std::byte>(protected_header.data(),
                                   protected_header.size()),
        std::span<const std::byte>(claim1_cbor.data(), claim1_cbor.size()));
    ASSERT_FALSE(sig_structure0.empty());
    ASSERT_FALSE(sig_structure1.empty());

    const std::vector<std::byte> der_sig0 = ecdsa_sign_sha256(key,
                                                              sig_structure0);
    const std::vector<std::byte> der_sig1 = ecdsa_sign_sha256(key,
                                                              sig_structure1);
    EVP_PKEY_free(key);
    ASSERT_FALSE(der_sig0.empty());
    ASSERT_FALSE(der_sig1.empty());

    std::vector<std::byte> raw_sig_bad = ecdsa_der_to_cose_raw_p256(der_sig0);
    const std::vector<std::byte> raw_sig_good = ecdsa_der_to_cose_raw_p256(
        der_sig1);
    ASSERT_EQ(raw_sig_bad.size(), 64U);
    ASSERT_EQ(raw_sig_good.size(), 64U);
    raw_sig_bad.back() = std::byte { static_cast<uint8_t>(
        static_cast<uint8_t>(raw_sig_bad.back()) ^ 0x01U) };
#else
    const std::vector<std::byte> public_key_der;
    const std::vector<std::byte> raw_sig_good(64U, std::byte { 0x00 });
    const std::vector<std::byte> raw_sig_bad(64U, std::byte { 0x00 });
#endif

    std::vector<std::byte> cose_sign1_bad;
    cose_sign1_bad.push_back(std::byte { 0xD2 });  // tag(18)
    append_cbor_array(&cose_sign1_bad, 4U);
    append_cbor_bytes(&cose_sign1_bad, protected_header);
    append_cbor_map(&cose_sign1_bad, 1U);
    append_cbor_text(&cose_sign1_bad, "public_key_der");
    append_cbor_bytes(&cose_sign1_bad, public_key_der);
    append_cbor_null(&cose_sign1_bad);
    append_cbor_bytes(&cose_sign1_bad, raw_sig_bad);

    std::vector<std::byte> cose_sign1_good;
    cose_sign1_good.push_back(std::byte { 0xD2 });  // tag(18)
    append_cbor_array(&cose_sign1_good, 4U);
    append_cbor_bytes(&cose_sign1_good, protected_header);
    append_cbor_map(&cose_sign1_good, 1U);
    append_cbor_text(&cose_sign1_good, "public_key_der");
    append_cbor_bytes(&cose_sign1_good, public_key_der);
    append_cbor_null(&cose_sign1_good);
    append_cbor_bytes(&cose_sign1_good, raw_sig_good);

    std::vector<std::byte> claim0_cbor_box;
    append_bmff_box(&claim0_cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(claim0_cbor.data(),
                                               claim0_cbor.size()));
    const std::vector<std::byte> claim0_jumb = make_jumb_box_with_label(
        "c2pa.claim", std::span<const std::byte>(claim0_cbor_box.data(),
                                                 claim0_cbor_box.size()));

    std::vector<std::byte> signature0_cbor_box;
    append_bmff_box(&signature0_cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cose_sign1_bad.data(),
                                               cose_sign1_bad.size()));
    const std::vector<std::byte> signature0_jumb = make_jumb_box_with_label(
        "c2pa.signature",
        std::span<const std::byte>(signature0_cbor_box.data(),
                                   signature0_cbor_box.size()));

    std::vector<std::byte> claim1_cbor_box;
    append_bmff_box(&claim1_cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(claim1_cbor.data(),
                                               claim1_cbor.size()));
    const std::vector<std::byte> claim1_jumb = make_jumb_box_with_label(
        "c2pa.claim", std::span<const std::byte>(claim1_cbor_box.data(),
                                                 claim1_cbor_box.size()));

    std::vector<std::byte> signature1_cbor_box;
    append_bmff_box(&signature1_cbor_box, fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cose_sign1_good.data(),
                                               cose_sign1_good.size()));
    const std::vector<std::byte> signature1_jumb = make_jumb_box_with_label(
        "c2pa.signature",
        std::span<const std::byte>(signature1_cbor_box.data(),
                                   signature1_cbor_box.size()));

    std::vector<std::byte> manifest_payload;
    manifest_payload.insert(manifest_payload.end(), claim0_jumb.begin(),
                            claim0_jumb.end());
    manifest_payload.insert(manifest_payload.end(), signature0_jumb.begin(),
                            signature0_jumb.end());
    manifest_payload.insert(manifest_payload.end(), claim1_jumb.begin(),
                            claim1_jumb.end());
    manifest_payload.insert(manifest_payload.end(), signature1_jumb.begin(),
                            signature1_jumb.end());
    const std::vector<std::byte> payload = make_jumb_box_with_label(
        "c2pa", std::span<const std::byte>(manifest_payload.data(),
                                           manifest_payload.size()));

    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::SignatureVerifiedOnly);
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

TEST(JumbfDecode, C2paVerifyCoseX5chainExtraction)
{
    const std::array<std::byte, 3U> payload_bytes
        = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    const std::vector<std::byte> protected_header = make_cose_protected_es256();
    ASSERT_FALSE(protected_header.empty());

    const std::array<std::byte, 2U> bad_cert = { std::byte { 0x01 },
                                                 std::byte { 0x02 } };
    const std::vector<std::byte> raw_sig(64U, std::byte { 0x00 });

    std::vector<std::byte> cbor_payload;
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "manifests");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "active_manifest");
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "claims");
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_text(&cbor_payload, "signatures");
    append_cbor_array(&cbor_payload, 1U);

    append_cbor_array(&cbor_payload, 4U);
    append_cbor_bytes(&cbor_payload, protected_header);
    append_cbor_map(&cbor_payload, 1U);
    append_cbor_head(&cbor_payload, 0U, 33U);
    append_cbor_array(&cbor_payload, 1U);
    append_cbor_bytes(&cbor_payload, bad_cert);
    append_cbor_bytes(&cbor_payload, payload_bytes);
    append_cbor_bytes(&cbor_payload, raw_sig);

    const std::vector<std::byte> payload = make_jumbf_payload_with_cbor(
        cbor_payload);
    MetaStore store;
    JumbfDecodeOptions options;
    options.verify_c2pa    = true;
    options.verify_backend = C2paVerifyBackend::OpenSsl;
    const JumbfDecodeResult result
        = decode_jumbf_payload(payload, store, EntryFlags::None, options);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

#if OPENMETA_ENABLE_C2PA_VERIFY && OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::VerificationFailed);
    store.finalize();
    EXPECT_EQ(read_jumbf_field_text(store, "c2pa.verify.chain_reason"),
              "certificate_parse_failed");
#elif OPENMETA_ENABLE_C2PA_VERIFY
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::BackendUnavailable);
#else
    EXPECT_EQ(result.verify_status, C2paVerifyStatus::DisabledByBuild);
#endif
}

}  // namespace openmeta
