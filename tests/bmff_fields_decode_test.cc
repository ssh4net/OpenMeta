// SPDX-License-Identifier: Apache-2.0

#include "openmeta/simple_meta.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openmeta {
namespace {

    static void append_u16be(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }

    static void append_u32be(std::vector<std::byte>* out, uint32_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }

    static void append_u64be(std::vector<std::byte>* out, uint64_t v)
    {
        for (uint32_t i = 0U; i < 8U; ++i) {
            const uint32_t shift = (7U - i) * 8U;
            out->push_back(
                std::byte { static_cast<uint8_t>((v >> shift) & 0xFFU) });
        }
    }

    static void append_fourcc(std::vector<std::byte>* out, uint32_t fourcc_v)
    {
        append_u32be(out, fourcc_v);
    }

    static void append_bytes(std::vector<std::byte>* out, const char* s)
    {
        for (size_t i = 0; s[i] != '\0'; ++i) {
            out->push_back(std::byte { static_cast<uint8_t>(s[i]) });
        }
    }

    static void append_fullbox_header(std::vector<std::byte>* out,
                                      uint8_t version)
    {
        out->push_back(std::byte { version });
        out->push_back(std::byte { 0 });
        out->push_back(std::byte { 0 });
        out->push_back(std::byte { 0 });
    }

    static void append_fullbox_header(std::vector<std::byte>* out,
                                      uint8_t version, uint32_t flags)
    {
        out->push_back(std::byte { version });
        out->push_back(
            std::byte { static_cast<uint8_t>((flags >> 16U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((flags >> 8U) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>(flags & 0xFFU) });
    }

    static void append_auxc_payload(std::vector<std::byte>* out,
                                    const char* aux_type,
                                    std::span<const std::byte> subtype)
    {
        append_fullbox_header(out, 0);
        append_bytes(out, aux_type);
        out->push_back(std::byte { 0x00 });
        out->insert(out->end(), subtype.begin(), subtype.end());
    }

    static void append_bmff_box(std::vector<std::byte>* out, uint32_t type,
                                std::span<const std::byte> payload)
    {
        append_u32be(out, static_cast<uint32_t>(8 + payload.size()));
        append_fourcc(out, type);
        out->insert(out->end(), payload.begin(), payload.end());
    }

    static void append_iref_v0_edge(std::vector<std::byte>* out, uint32_t type,
                                    uint16_t from_item_id, uint16_t to_item_id)
    {
        std::vector<std::byte> payload;
        append_u16be(&payload, from_item_id);
        append_u16be(&payload, 1U);
        append_u16be(&payload, to_item_id);
        append_bmff_box(out, type, payload);
    }

    static void append_iref_v1_edge(std::vector<std::byte>* out, uint32_t type,
                                    uint32_t from_item_id, uint32_t to_item_id)
    {
        std::vector<std::byte> payload;
        append_u32be(&payload, from_item_id);
        append_u16be(&payload, 1U);
        append_u32be(&payload, to_item_id);
        append_bmff_box(out, type, payload);
    }

    static void append_iloc_v1_entry(std::vector<std::byte>* out,
                                     uint16_t item_id, uint16_t method,
                                     uint32_t offset, uint32_t length)
    {
        append_u16be(out, item_id);
        append_u16be(out, method);
        append_u16be(out, 0U);
        append_u16be(out, 1U);
        append_u32be(out, offset);
        append_u32be(out, length);
    }

    static void append_iloc_v1_idat_entry(std::vector<std::byte>* out,
                                          uint16_t item_id, uint32_t offset,
                                          uint32_t length)
    {
        append_iloc_v1_entry(out, item_id, 1U, offset, length);
    }

    static void append_iloc_v1_indexed_entry(std::vector<std::byte>* out,
                                             uint16_t item_id, uint16_t method,
                                             uint32_t reference_index,
                                             uint32_t offset, uint32_t length)
    {
        append_u16be(out, item_id);
        append_u16be(out, method);
        append_u16be(out, 0U);
        append_u16be(out, 1U);
        append_u32be(out, reference_index);
        append_u32be(out, offset);
        append_u32be(out, length);
    }

    static void append_iloc_v1_split_file_entry(std::vector<std::byte>* out,
                                                uint16_t item_id,
                                                uint32_t offset,
                                                uint32_t length_a,
                                                uint32_t length_b)
    {
        append_u16be(out, item_id);
        append_u16be(out, 0U);
        append_u16be(out, 0U);
        append_u16be(out, 2U);
        append_u32be(out, offset);
        append_u32be(out, length_a);
        append_u32be(out, offset + length_a);
        append_u32be(out, length_b);
    }

    static void append_entity_group_box(std::vector<std::byte>* out,
                                        uint32_t group_type, uint32_t group_id,
                                        std::span<const uint32_t> entity_ids)
    {
        std::vector<std::byte> payload;
        append_fullbox_header(&payload, 0);
        append_u32be(&payload, group_id);
        append_u32be(&payload, static_cast<uint32_t>(entity_ids.size()));
        for (size_t i = 0; i < entity_ids.size(); ++i) {
            append_u32be(&payload, entity_ids[i]);
        }
        append_bmff_box(out, group_type, payload);
    }

    static void decode_bmff_test_file(std::span<const std::byte> file,
                                      MetaStore* store)
    {
        std::array<ContainerBlockRef, 16> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 4096> payload {};
        std::array<uint32_t, 64> payload_scratch {};
        ExifDecodeOptions exif_opts;
        PayloadOptions payload_opts;
        (void)simple_meta_read(file, *store, blocks, ifds, payload,
                               payload_scratch, exif_opts, payload_opts);
        store->finalize();
    }

    static void append_infe_v2(std::vector<std::byte>* out, uint16_t item_id,
                               uint16_t protection_index, uint32_t item_type,
                               const char* name)
    {
        std::vector<std::byte> payload;
        append_fullbox_header(&payload, 2);
        append_u16be(&payload, item_id);
        append_u16be(&payload, protection_index);
        append_u32be(&payload, item_type);
        append_bytes(&payload, name);
        payload.push_back(std::byte { 0 });
        append_bmff_box(out, fourcc('i', 'n', 'f', 'e'), payload);
    }

    static void append_infe_v2_mime(std::vector<std::byte>* out,
                                    uint16_t item_id, uint16_t protection_index,
                                    const char* name, const char* content_type,
                                    const char* content_encoding)
    {
        std::vector<std::byte> payload;
        append_fullbox_header(&payload, 2);
        append_u16be(&payload, item_id);
        append_u16be(&payload, protection_index);
        append_u32be(&payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&payload, name);
        payload.push_back(std::byte { 0 });
        append_bytes(&payload, content_type);
        payload.push_back(std::byte { 0 });
        append_bytes(&payload, content_encoding);
        payload.push_back(std::byte { 0 });
        append_bmff_box(out, fourcc('i', 'n', 'f', 'e'), payload);
    }

    static std::vector<std::byte> make_tiled_image_configuration_file(
        uint8_t version, uint32_t flags, uint32_t output_width,
        uint32_t output_height, uint32_t tile_width, uint32_t tile_height,
        std::span<const uint32_t> dimensions, bool truncate_last_dimension,
        uint8_t ispe_association_count, uint8_t tilc_association_count,
        uint32_t conditional_payload_bytes)
    {
        std::vector<std::byte> file;
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0U);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0U);
        append_u16be(&pitm_payload, 1U);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> infe_box;
        append_infe_v2(&infe_box, 1U, 0U, fourcc('t', 'i', 'l', 'i'), "tiled");
        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2U);
        append_u32be(&iinf_payload, 1U);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> ispe_payload;
        append_fullbox_header(&ispe_payload, 0U);
        append_u32be(&ispe_payload, output_width);
        append_u32be(&ispe_payload, output_height);
        std::vector<std::byte> ispe_box;
        append_bmff_box(&ispe_box, fourcc('i', 's', 'p', 'e'), ispe_payload);

        std::vector<std::byte> tilc_payload;
        tilc_payload.push_back(std::byte { version });
        tilc_payload.push_back(
            std::byte { static_cast<uint8_t>((flags >> 16U) & 0xFFU) });
        tilc_payload.push_back(
            std::byte { static_cast<uint8_t>((flags >> 8U) & 0xFFU) });
        tilc_payload.push_back(
            std::byte { static_cast<uint8_t>(flags & 0xFFU) });
        append_u32be(&tilc_payload, tile_width);
        append_u32be(&tilc_payload, tile_height);
        tilc_payload.push_back(
            std::byte { static_cast<uint8_t>(dimensions.size()) });
        size_t dimension_count = dimensions.size();
        if (truncate_last_dimension && dimension_count != 0U) {
            dimension_count -= 1U;
        }
        for (size_t i = 0U; i < dimension_count; ++i) {
            append_u32be(&tilc_payload, dimensions[i]);
        }
        for (uint32_t i = 0U; i < conditional_payload_bytes; ++i) {
            tilc_payload.push_back(std::byte { 0x5aU });
        }
        std::vector<std::byte> tilc_box;
        append_bmff_box(&tilc_box, fourcc('t', 'i', 'l', 'C'), tilc_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), ispe_box.begin(),
                            ispe_box.end());
        ipco_payload.insert(ipco_payload.end(), tilc_box.begin(),
                            tilc_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0U);
        append_u32be(&ipma_payload, 1U);
        append_u16be(&ipma_payload, 1U);
        ipma_payload.push_back(std::byte { static_cast<uint8_t>(
            ispe_association_count + tilc_association_count) });
        for (uint8_t i = 0U; i < ispe_association_count; ++i) {
            ipma_payload.push_back(std::byte { 1U });
        }
        for (uint8_t i = 0U; i < tilc_association_count; ++i) {
            ipma_payload.push_back(std::byte { 0x82U });
        }
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0U);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
        return file;
    }

    struct CompleteTiledImageOptions final {
        bool external_tiles                 = false;
        bool omit_internal_conditional      = false;
        bool add_external_conditional       = false;
        uint16_t data_reference_index       = 1U;
        uint16_t construction_method        = 0U;
        uint64_t input_item_count           = 2U;
        uint32_t tipa_property_index        = 3U;
        uint8_t tipa_version                = 0U;
        uint32_t tipa_flags                 = 0U;
        uint32_t deti_extra_flags           = 0U;
        uint8_t external_directory_flags    = 1U;
        bool omit_external_template_nul     = false;
        bool include_tile_sizes             = true;
        bool sequential_order               = false;
        uint32_t declared_offset_table_size = 16U;
        uint32_t first_tile_offset          = 16U;
        uint32_t first_tile_size            = 4U;
        uint32_t second_tile_offset         = 20U;
        uint32_t second_tile_size           = 4U;
    };

    static std::vector<std::byte>
    make_complete_tiled_image_file(const CompleteTiledImageOptions& options)
    {
        std::vector<std::byte> file;
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0U);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

        uint32_t mdat_payload_offset = 0U;
        uint32_t mdat_payload_size   = 0U;
        if (!options.external_tiles) {
            std::vector<std::byte> mdat_payload;
            append_u32be(&mdat_payload, options.first_tile_offset);
            if (options.include_tile_sizes) {
                append_u32be(&mdat_payload, options.first_tile_size);
            }
            append_u32be(&mdat_payload, options.second_tile_offset);
            if (options.include_tile_sizes) {
                append_u32be(&mdat_payload, options.second_tile_size);
            }
            append_u32be(&mdat_payload, 0x11111111U);
            append_u32be(&mdat_payload, 0x22222222U);
            mdat_payload_offset = static_cast<uint32_t>(file.size() + 8U);
            mdat_payload_size   = static_cast<uint32_t>(mdat_payload.size());
            append_bmff_box(&file, fourcc('m', 'd', 'a', 't'), mdat_payload);
        }

        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0U);
        append_u16be(&pitm_payload, 1U);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> infe_box;
        append_infe_v2(&infe_box, 1U, 0U, fourcc('t', 'i', 'l', 'i'), "tiled");
        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2U);
        append_u32be(&iinf_payload, 1U);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> ispe_payload;
        append_fullbox_header(&ispe_payload, 0U);
        append_u32be(&ispe_payload, 128U);
        append_u32be(&ispe_payload, 64U);
        std::vector<std::byte> ispe_box;
        append_bmff_box(&ispe_box, fourcc('i', 's', 'p', 'e'), ispe_payload);

        std::vector<std::byte> tilc_payload;
        append_fullbox_header(&tilc_payload, 0U);
        append_u32be(&tilc_payload, 64U);
        append_u32be(&tilc_payload, 64U);
        tilc_payload.push_back(std::byte { 0U });
        if ((!options.external_tiles && !options.omit_internal_conditional)
            || (options.external_tiles && options.add_external_conditional)) {
            append_fourcc(&tilc_payload, fourcc('a', 'v', '0', '1'));
            std::vector<std::byte> tipa_payload;
            append_fullbox_header(&tipa_payload, options.tipa_version,
                                  options.tipa_flags);
            tipa_payload.push_back(std::byte { 1U });
            if ((options.tipa_flags & 1U) == 0U) {
                tipa_payload.push_back(std::byte { static_cast<uint8_t>(
                    0x80U | (options.tipa_property_index & 0x7FU)) });
            } else {
                append_u16be(&tipa_payload,
                             static_cast<uint16_t>(
                                 0x8000U
                                 | (options.tipa_property_index & 0x7FFFU)));
            }
            append_bmff_box(&tilc_payload, fourcc('t', 'i', 'p', 'a'),
                            tipa_payload);
        }
        std::vector<std::byte> tilc_box;
        append_bmff_box(&tilc_box, fourcc('t', 'i', 'l', 'C'), tilc_payload);

        std::vector<std::byte> av1c_box;
        const std::array<std::byte, 0> no_payload {};
        append_bmff_box(&av1c_box, fourcc('a', 'v', '1', 'C'), no_payload);
        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), ispe_box.begin(),
                            ispe_box.end());
        ipco_payload.insert(ipco_payload.end(), tilc_box.begin(),
                            tilc_box.end());
        ipco_payload.insert(ipco_payload.end(), av1c_box.begin(),
                            av1c_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0U);
        append_u32be(&ipma_payload, 1U);
        append_u16be(&ipma_payload, 1U);
        ipma_payload.push_back(std::byte { 2U });
        ipma_payload.push_back(std::byte { 1U });
        ipma_payload.push_back(std::byte { 0x82U });
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> deti_payload;
        uint32_t deti_flags = options.external_tiles
                                  ? 0x80U
                                  : (options.include_tile_sizes ? 0x08U : 0U);
        if (options.sequential_order) {
            deti_flags |= 0x10U;
        }
        deti_flags |= options.deti_extra_flags;
        append_fullbox_header(&deti_payload, 0U, deti_flags);
        deti_payload.push_back(
            std::byte { static_cast<uint8_t>(options.input_item_count) });
        if (options.external_tiles) {
            deti_payload.push_back(
                std::byte { options.external_directory_flags });
            append_u16be(&deti_payload, 10U);
            append_u16be(&deti_payload, 12U);
            append_u64be(&deti_payload, 1000U);
            append_bytes(&deti_payload, "https://tiles.example/image/");
            deti_payload.push_back(std::byte { 0U });
            append_bytes(&deti_payload, "representation");
            deti_payload.push_back(std::byte { 0U });
            append_bytes(&deti_payload, "$tileID$.heif");
            if (!options.omit_external_template_nul) {
                deti_payload.push_back(std::byte { 0U });
            }
        } else {
            append_u32be(&deti_payload, 0U);
            append_u32be(&deti_payload, options.declared_offset_table_size);
        }
        std::vector<std::byte> deti_box;
        append_bmff_box(&deti_box, fourcc('d', 'e', 't', 'i'), deti_payload);
        std::vector<std::byte> dref_payload;
        append_fullbox_header(&dref_payload, 0U);
        append_u32be(&dref_payload, 1U);
        dref_payload.insert(dref_payload.end(), deti_box.begin(),
                            deti_box.end());
        std::vector<std::byte> dref_box;
        append_bmff_box(&dref_box, fourcc('d', 'r', 'e', 'f'), dref_payload);
        std::vector<std::byte> dinf_box;
        append_bmff_box(&dinf_box, fourcc('d', 'i', 'n', 'f'), dref_box);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1U);
        iloc_payload.push_back(std::byte { 0x44U });
        iloc_payload.push_back(std::byte { 0U });
        append_u16be(&iloc_payload, 1U);
        append_u16be(&iloc_payload, 1U);
        append_u16be(&iloc_payload, options.construction_method);
        append_u16be(&iloc_payload, options.data_reference_index);
        append_u16be(&iloc_payload, options.external_tiles ? 0U : 1U);
        if (!options.external_tiles) {
            append_u32be(&iloc_payload, mdat_payload_offset);
            append_u32be(&iloc_payload, mdat_payload_size);
        }
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0U);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        meta_payload.insert(meta_payload.end(), dinf_box.begin(),
                            dinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
        return file;
    }

    static MetaKeyView bmff_key(std::string_view field)
    {
        MetaKeyView key;
        key.kind                  = MetaKeyKind::BmffField;
        key.data.bmff_field.field = field;
        return key;
    }

    static std::vector<uint32_t> collect_u32_values(const MetaStore& store,
                                                    std::string_view field)
    {
        std::vector<uint32_t> out;
        const std::span<const EntryId> ids = store.find_all(bmff_key(field));
        out.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& e = store.entry(ids[i]);
            if (e.value.kind != MetaValueKind::Scalar
                || e.value.elem_type != MetaElementType::U32) {
                continue;
            }
            out.push_back(static_cast<uint32_t>(e.value.data.u64));
        }
        return out;
    }

    static std::vector<int32_t> collect_i32_values(const MetaStore& store,
                                                   std::string_view field)
    {
        std::vector<int32_t> out;
        const std::span<const EntryId> ids = store.find_all(bmff_key(field));
        out.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& e = store.entry(ids[i]);
            if (e.value.kind != MetaValueKind::Scalar
                || e.value.elem_type != MetaElementType::I32) {
                continue;
            }
            out.push_back(static_cast<int32_t>(e.value.data.i64));
        }
        return out;
    }

    static std::vector<uint8_t> collect_u8_values(const MetaStore& store,
                                                  std::string_view field)
    {
        std::vector<uint8_t> out;
        const std::span<const EntryId> ids = store.find_all(bmff_key(field));
        out.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& e = store.entry(ids[i]);
            if (e.value.kind != MetaValueKind::Scalar
                || e.value.elem_type != MetaElementType::U8) {
                continue;
            }
            out.push_back(static_cast<uint8_t>(e.value.data.u64));
        }
        return out;
    }

    static std::vector<uint16_t> collect_u16_values(const MetaStore& store,
                                                    std::string_view field)
    {
        std::vector<uint16_t> out;
        const std::span<const EntryId> ids = store.find_all(bmff_key(field));
        out.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& e = store.entry(ids[i]);
            if (e.value.kind != MetaValueKind::Scalar
                || e.value.elem_type != MetaElementType::U16) {
                continue;
            }
            out.push_back(static_cast<uint16_t>(e.value.data.u64));
        }
        return out;
    }

    static std::vector<uint64_t> collect_u64_values(const MetaStore& store,
                                                    std::string_view field)
    {
        std::vector<uint64_t> out;
        const std::span<const EntryId> ids = store.find_all(bmff_key(field));
        out.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& e = store.entry(ids[i]);
            if (e.value.kind != MetaValueKind::Scalar
                || e.value.elem_type != MetaElementType::U64) {
                continue;
            }
            out.push_back(e.value.data.u64);
        }
        return out;
    }

    static std::vector<std::string> collect_text_values(const MetaStore& store,
                                                        std::string_view field)
    {
        std::vector<std::string> out;
        const std::span<const EntryId> ids = store.find_all(bmff_key(field));
        out.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& e = store.entry(ids[i]);
            if (e.value.kind != MetaValueKind::Text) {
                continue;
            }
            const std::span<const std::byte> text = store.arena().span(
                e.value.data.span);
            out.emplace_back(reinterpret_cast<const char*>(text.data()),
                             text.size());
        }
        return out;
    }

}  // namespace

TEST(BmffDerivedFieldsDecode, BoundsNestedMetaTraversalAndSkipsInvalidMeta)
{
    for (const uint32_t depth : { 0U, 1U, 16U, 17U }) {
        SCOPED_TRACE(depth);
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0U);
        append_u16be(&pitm_payload, 77U);
        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0U);
        append_bmff_box(&meta_payload, fourcc('p', 'i', 't', 'm'),
                        pitm_payload);
        std::vector<std::byte> nested;
        append_bmff_box(&nested, fourcc('m', 'e', 't', 'a'), {});
        append_bmff_box(&nested, fourcc('m', 'e', 't', 'a'), meta_payload);
        for (uint32_t i = 0U; i < depth; ++i) {
            std::vector<std::byte> parent;
            append_bmff_box(&parent, fourcc('m', 'o', 'o', 'v'), nested);
            nested = std::move(parent);
        }
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0U);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), nested.begin(), nested.end());

        MetaStore store;
        std::array<ContainerBlockRef, 16U> blocks {};
        std::array<ExifIfdRef, 8U> ifds {};
        std::array<std::byte, 1024U> payload {};
        std::array<uint32_t, 32U> scratch {};
        (void)simple_meta_read(file, store, blocks, ifds, payload, scratch,
                               ExifDecodeOptions {}, PayloadOptions {});
        store.finalize();
        const auto ids = collect_u32_values(store, "meta.primary_item_id");
        if (depth <= 16U) {
            ASSERT_EQ(ids.size(), 1U);
            EXPECT_EQ(ids[0U], 77U);
        } else {
            EXPECT_TRUE(ids.empty());
        }
    }
}

TEST(BmffDerivedFieldsDecode, EmitsFtypAndPrimaryProps)
{
    // Minimal ISO-BMFF/HEIF with:
    // - ftyp(major_brand='heic', compat=['mif1'])
    // - meta(pitm primary item id=1, iprp/ipco(ispe+irot+imir), ipma associates props)

    std::vector<std::byte> file;

    // ftyp
    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);  // minor_version
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    // meta
    {
        // pitm (FullBox version 0): primary item id=1 (u16)
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        // ipco: ispe + irot + imir + colr
        std::vector<std::byte> ispe_payload;
        append_fullbox_header(&ispe_payload, 0);
        append_u32be(&ispe_payload, 640);
        append_u32be(&ispe_payload, 480);
        std::vector<std::byte> ispe_box;
        append_bmff_box(&ispe_box, fourcc('i', 's', 'p', 'e'), ispe_payload);

        std::vector<std::byte> irot_payload;
        irot_payload.push_back(std::byte { 1 });  // 90 degrees
        std::vector<std::byte> irot_box;
        append_bmff_box(&irot_box, fourcc('i', 'r', 'o', 't'), irot_payload);

        std::vector<std::byte> imir_payload;
        imir_payload.push_back(std::byte { 1 });
        std::vector<std::byte> imir_box;
        append_bmff_box(&imir_box, fourcc('i', 'm', 'i', 'r'), imir_payload);

        std::vector<std::byte> colr_payload;
        append_fourcc(&colr_payload, fourcc('n', 'c', 'l', 'x'));
        append_u16be(&colr_payload, 9);
        append_u16be(&colr_payload, 16);
        append_u16be(&colr_payload, 9);
        colr_payload.push_back(std::byte { 0x80 });
        std::vector<std::byte> colr_box;
        append_bmff_box(&colr_box, fourcc('c', 'o', 'l', 'r'), colr_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), ispe_box.begin(),
                            ispe_box.end());
        ipco_payload.insert(ipco_payload.end(), irot_box.begin(),
                            irot_box.end());
        ipco_payload.insert(ipco_payload.end(), imir_box.begin(),
                            imir_box.end());
        ipco_payload.insert(ipco_payload.end(), colr_box.begin(),
                            colr_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        // ipma (FullBox version 0): item 1 has properties [1,2,3,4]
        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 1);              // entry_count
        append_u16be(&ipma_payload, 1);              // item_ID
        ipma_payload.push_back(std::byte { 4 });     // association_count
        ipma_payload.push_back(std::byte { 1 });     // property_index=1
        ipma_payload.push_back(std::byte { 0x82 });  // essential, index=2
        ipma_payload.push_back(std::byte { 3 });     // property_index=3
        ipma_payload.push_back(std::byte { 4 });     // property_index=4
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    // ftyp.major_brand == 'heic'
    {
        const std::span<const EntryId> ids = store.find_all(
            bmff_key("ftyp.major_brand"));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64),
                  fourcc('h', 'e', 'i', 'c'));
    }
    {
        const std::vector<std::string> major_brand_names
            = collect_text_values(store, "ftyp.major_brand_name");
        ASSERT_EQ(major_brand_names.size(), 1U);
        EXPECT_EQ(major_brand_names[0], "heic");

        const std::vector<uint32_t> compat_counts
            = collect_u32_values(store, "ftyp.compat_brand_count");
        ASSERT_EQ(compat_counts.size(), 1U);
        EXPECT_EQ(compat_counts[0], 1U);

        const std::vector<std::string> compat_brand_names
            = collect_text_values(store, "ftyp.compat_brand_name");
        ASSERT_EQ(compat_brand_names.size(), 1U);
        EXPECT_EQ(compat_brand_names[0], "mif1");
    }

    // primary.width/height/rotation/mirror
    {
        const std::span<const EntryId> w = store.find_all(
            bmff_key("primary.width"));
        const std::span<const EntryId> h = store.find_all(
            bmff_key("primary.height"));
        const std::span<const EntryId> r = store.find_all(
            bmff_key("primary.rotation_degrees"));
        const std::vector<uint8_t> m = collect_u8_values(store,
                                                         "primary.mirror");
        ASSERT_EQ(w.size(), 1U);
        ASSERT_EQ(h.size(), 1U);
        ASSERT_EQ(r.size(), 1U);
        ASSERT_EQ(m.size(), 1U);
        EXPECT_EQ(static_cast<uint32_t>(store.entry(w[0]).value.data.u64),
                  640U);
        EXPECT_EQ(static_cast<uint32_t>(store.entry(h[0]).value.data.u64),
                  480U);
        EXPECT_EQ(static_cast<uint16_t>(store.entry(r[0]).value.data.u64), 90U);
        EXPECT_EQ(m[0], 1U);
    }
    {
        const std::vector<uint32_t> display_width
            = collect_u32_values(store, "primary.display_width");
        const std::vector<uint32_t> display_height
            = collect_u32_values(store, "primary.display_height");
        const std::vector<uint8_t> swapped
            = collect_u8_values(store, "primary.display_dimensions_swapped");
        const std::vector<std::string> transform
            = collect_text_values(store, "primary.transform_summary");
        ASSERT_EQ(display_width.size(), 1U);
        ASSERT_EQ(display_height.size(), 1U);
        ASSERT_EQ(swapped.size(), 1U);
        ASSERT_EQ(transform.size(), 1U);
        EXPECT_EQ(display_width[0], 480U);
        EXPECT_EQ(display_height[0], 640U);
        EXPECT_EQ(swapped[0], 1U);
        EXPECT_EQ(transform[0], "rotate_90_mirror_1");
    }

    const std::vector<uint32_t> color_type
        = collect_u32_values(store, "primary.color_type");
    ASSERT_EQ(color_type.size(), 1U);
    EXPECT_EQ(color_type[0], fourcc('n', 'c', 'l', 'x'));

    const std::vector<std::string> color_type_name
        = collect_text_values(store, "primary.color_type_name");
    ASSERT_EQ(color_type_name.size(), 1U);
    EXPECT_EQ(color_type_name[0], "nclx");

    const std::vector<uint32_t> ipco_property_count
        = collect_u32_values(store, "ipco.property_count");
    const std::vector<uint32_t> ipco_known_count
        = collect_u32_values(store, "ipco.known_property_count");
    const std::vector<uint32_t> ipco_unknown_count
        = collect_u32_values(store, "ipco.unknown_property_count");
    ASSERT_EQ(ipco_property_count.size(), 1U);
    ASSERT_EQ(ipco_known_count.size(), 1U);
    ASSERT_EQ(ipco_unknown_count.size(), 1U);
    EXPECT_EQ(ipco_property_count[0], 4U);
    EXPECT_EQ(ipco_known_count[0], 4U);
    EXPECT_EQ(ipco_unknown_count[0], 0U);
    const std::vector<uint32_t> ipco_ispe_count
        = collect_u32_values(store, "ipco.ispe_count");
    const std::vector<uint32_t> ipco_irot_count
        = collect_u32_values(store, "ipco.irot_count");
    const std::vector<uint32_t> ipco_imir_count
        = collect_u32_values(store, "ipco.imir_count");
    const std::vector<uint32_t> ipco_colr_count
        = collect_u32_values(store, "ipco.colr_count");
    ASSERT_EQ(ipco_ispe_count.size(), 1U);
    ASSERT_EQ(ipco_irot_count.size(), 1U);
    ASSERT_EQ(ipco_imir_count.size(), 1U);
    ASSERT_EQ(ipco_colr_count.size(), 1U);
    EXPECT_EQ(ipco_ispe_count[0], 1U);
    EXPECT_EQ(ipco_irot_count[0], 1U);
    EXPECT_EQ(ipco_imir_count[0], 1U);
    EXPECT_EQ(ipco_colr_count[0], 1U);

    const std::vector<uint16_t> primaries
        = collect_u16_values(store, "primary.nclx_colour_primaries");
    const std::vector<uint16_t> transfer
        = collect_u16_values(store, "primary.nclx_transfer_characteristics");
    const std::vector<uint16_t> matrix
        = collect_u16_values(store, "primary.nclx_matrix_coefficients");
    const std::vector<uint8_t> full_range
        = collect_u8_values(store, "primary.nclx_full_range_flag");
    ASSERT_EQ(primaries.size(), 1U);
    ASSERT_EQ(transfer.size(), 1U);
    ASSERT_EQ(matrix.size(), 1U);
    ASSERT_EQ(full_range.size(), 1U);
    EXPECT_EQ(primaries[0], 9U);
    EXPECT_EQ(transfer[0], 16U);
    EXPECT_EQ(matrix[0], 9U);
    EXPECT_EQ(full_range[0], 1U);

    const std::vector<uint32_t> association_count
        = collect_u32_values(store, "ipma.association_count");
    ASSERT_EQ(association_count.size(), 1U);
    EXPECT_EQ(association_count[0], 4U);

    const std::vector<uint32_t> association_item_ids
        = collect_u32_values(store, "ipma.item_id");
    const std::vector<uint32_t> property_indices
        = collect_u32_values(store, "ipma.property_index");
    const std::vector<uint8_t> essential = collect_u8_values(store,
                                                             "ipma.essential");
    ASSERT_EQ(association_item_ids.size(), 4U);
    ASSERT_EQ(property_indices.size(), 4U);
    ASSERT_EQ(essential.size(), 4U);
    EXPECT_EQ(association_item_ids[0], 1U);
    EXPECT_EQ(association_item_ids[3], 1U);
    EXPECT_EQ(property_indices[0], 1U);
    EXPECT_EQ(property_indices[1], 2U);
    EXPECT_EQ(property_indices[2], 3U);
    EXPECT_EQ(property_indices[3], 4U);
    EXPECT_EQ(essential[0], 0U);
    EXPECT_EQ(essential[1], 1U);
    EXPECT_EQ(essential[2], 0U);
    EXPECT_EQ(essential[3], 0U);

    const std::vector<std::string> property_type_names
        = collect_text_values(store, "ipma.property_type_name");
    ASSERT_EQ(property_type_names.size(), 4U);
    EXPECT_EQ(property_type_names[0], "ispe");
    EXPECT_EQ(property_type_names[1], "irot");
    EXPECT_EQ(property_type_names[2], "imir");
    EXPECT_EQ(property_type_names[3], "colr");

    const std::vector<uint32_t> ipma_ispe_association_count
        = collect_u32_values(store, "ipma.ispe.association_count");
    const std::vector<uint32_t> ipma_ispe_primary_count
        = collect_u32_values(store, "ipma.ispe.primary_association_count");
    const std::vector<uint32_t> ipma_ispe_essential_count
        = collect_u32_values(store, "ipma.ispe.essential_count");
    const std::vector<uint32_t> ipma_irot_essential_count
        = collect_u32_values(store, "ipma.irot.essential_count");
    const std::vector<uint32_t> ipma_colr_association_count
        = collect_u32_values(store, "ipma.colr.association_count");
    ASSERT_EQ(ipma_ispe_association_count.size(), 1U);
    ASSERT_EQ(ipma_ispe_primary_count.size(), 1U);
    ASSERT_EQ(ipma_irot_essential_count.size(), 1U);
    ASSERT_EQ(ipma_colr_association_count.size(), 1U);
    EXPECT_EQ(ipma_ispe_association_count[0], 1U);
    EXPECT_EQ(ipma_ispe_primary_count[0], 1U);
    EXPECT_TRUE(ipma_ispe_essential_count.empty());
    EXPECT_EQ(ipma_irot_essential_count[0], 1U);
    EXPECT_EQ(ipma_colr_association_count[0], 1U);
}

TEST(BmffDerivedFieldsDecode, EmitsPrimaryApertureAspectAndPixelDepth)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> pasp_payload;
        append_u32be(&pasp_payload, 4);
        append_u32be(&pasp_payload, 3);
        std::vector<std::byte> pasp_box;
        append_bmff_box(&pasp_box, fourcc('p', 'a', 's', 'p'), pasp_payload);

        std::vector<std::byte> pixi_payload;
        append_fullbox_header(&pixi_payload, 0);
        pixi_payload.push_back(std::byte { 3 });
        pixi_payload.push_back(std::byte { 10 });
        pixi_payload.push_back(std::byte { 10 });
        pixi_payload.push_back(std::byte { 10 });
        std::vector<std::byte> pixi_box;
        append_bmff_box(&pixi_box, fourcc('p', 'i', 'x', 'i'), pixi_payload);

        std::vector<std::byte> clap_payload;
        append_u32be(&clap_payload, 1920);
        append_u32be(&clap_payload, 1);
        append_u32be(&clap_payload, 1080);
        append_u32be(&clap_payload, 1);
        append_u32be(&clap_payload, 0xFFFFFFFCU);
        append_u32be(&clap_payload, 1);
        append_u32be(&clap_payload, 8);
        append_u32be(&clap_payload, 1);
        std::vector<std::byte> clap_box;
        append_bmff_box(&clap_box, fourcc('c', 'l', 'a', 'p'), clap_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), pasp_box.begin(),
                            pasp_box.end());
        ipco_payload.insert(ipco_payload.end(), pixi_box.begin(),
                            pixi_box.end());
        ipco_payload.insert(ipco_payload.end(), clap_box.begin(),
                            clap_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 1);
        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte { 3 });
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 2 });
        ipma_payload.push_back(std::byte { 3 });
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> ipco_property_count
        = collect_u32_values(store, "ipco.property_count");
    const std::vector<uint32_t> ipco_known_count
        = collect_u32_values(store, "ipco.known_property_count");
    const std::vector<uint32_t> ipco_pasp_count
        = collect_u32_values(store, "ipco.pasp_count");
    const std::vector<uint32_t> ipco_pixi_count
        = collect_u32_values(store, "ipco.pixi_count");
    const std::vector<uint32_t> ipco_clap_count
        = collect_u32_values(store, "ipco.clap_count");
    ASSERT_EQ(ipco_property_count.size(), 1U);
    ASSERT_EQ(ipco_known_count.size(), 1U);
    ASSERT_EQ(ipco_pasp_count.size(), 1U);
    ASSERT_EQ(ipco_pixi_count.size(), 1U);
    ASSERT_EQ(ipco_clap_count.size(), 1U);
    EXPECT_EQ(ipco_property_count[0], 3U);
    EXPECT_EQ(ipco_known_count[0], 3U);
    EXPECT_EQ(ipco_pasp_count[0], 1U);
    EXPECT_EQ(ipco_pixi_count[0], 1U);
    EXPECT_EQ(ipco_clap_count[0], 1U);

    const std::vector<uint32_t> ipma_pasp_association_count
        = collect_u32_values(store, "ipma.pasp.association_count");
    const std::vector<uint32_t> ipma_pixi_association_count
        = collect_u32_values(store, "ipma.pixi.association_count");
    const std::vector<uint32_t> ipma_clap_association_count
        = collect_u32_values(store, "ipma.clap.association_count");
    ASSERT_EQ(ipma_pasp_association_count.size(), 1U);
    ASSERT_EQ(ipma_pixi_association_count.size(), 1U);
    ASSERT_EQ(ipma_clap_association_count.size(), 1U);
    EXPECT_EQ(ipma_pasp_association_count[0], 1U);
    EXPECT_EQ(ipma_pixi_association_count[0], 1U);
    EXPECT_EQ(ipma_clap_association_count[0], 1U);

    const std::vector<uint32_t> pixel_aspect_h
        = collect_u32_values(store, "primary.pixel_aspect_h_spacing");
    const std::vector<uint32_t> pixel_aspect_v
        = collect_u32_values(store, "primary.pixel_aspect_v_spacing");
    ASSERT_EQ(pixel_aspect_h.size(), 1U);
    ASSERT_EQ(pixel_aspect_v.size(), 1U);
    EXPECT_EQ(pixel_aspect_h[0], 4U);
    EXPECT_EQ(pixel_aspect_v[0], 3U);

    const std::vector<uint8_t> channel_count
        = collect_u8_values(store, "primary.pixel_depth_channel_count");
    const std::vector<uint8_t> bits
        = collect_u8_values(store, "primary.pixel_depth_bits_per_channel");
    ASSERT_EQ(channel_count.size(), 1U);
    ASSERT_EQ(bits.size(), 3U);
    EXPECT_EQ(channel_count[0], 3U);
    EXPECT_EQ(bits[0], 10U);
    EXPECT_EQ(bits[1], 10U);
    EXPECT_EQ(bits[2], 10U);

    const std::vector<int32_t> clean_width_n
        = collect_i32_values(store, "primary.clean_aperture_width_n");
    const std::vector<int32_t> clean_height_n
        = collect_i32_values(store, "primary.clean_aperture_height_n");
    const std::vector<int32_t> horiz_off_n
        = collect_i32_values(store, "primary.clean_aperture_horiz_off_n");
    const std::vector<int32_t> vert_off_n
        = collect_i32_values(store, "primary.clean_aperture_vert_off_n");
    ASSERT_EQ(clean_width_n.size(), 1U);
    ASSERT_EQ(clean_height_n.size(), 1U);
    ASSERT_EQ(horiz_off_n.size(), 1U);
    ASSERT_EQ(vert_off_n.size(), 1U);
    EXPECT_EQ(clean_width_n[0], 1920);
    EXPECT_EQ(clean_height_n[0], 1080);
    EXPECT_EQ(horiz_off_n[0], -4);
    EXPECT_EQ(vert_off_n[0], 8);
}

TEST(BmffDerivedFieldsDecode, EmitsPrimaryIccColorProfileSummary)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> colr_payload;
        append_fourcc(&colr_payload, fourcc('r', 'I', 'C', 'C'));
        for (uint8_t i = 0; i < 12U; ++i) {
            colr_payload.push_back(std::byte { i });
        }
        std::vector<std::byte> colr_box;
        append_bmff_box(&colr_box, fourcc('c', 'o', 'l', 'r'), colr_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), colr_box.begin(),
                            colr_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 1);
        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 1 });
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> color_type
        = collect_u32_values(store, "primary.color_type");
    ASSERT_EQ(color_type.size(), 1U);
    EXPECT_EQ(color_type[0], fourcc('r', 'I', 'C', 'C'));

    const std::vector<std::string> color_type_name
        = collect_text_values(store, "primary.color_type_name");
    ASSERT_EQ(color_type_name.size(), 1U);
    EXPECT_EQ(color_type_name[0], "rICC");

    const std::vector<uint32_t> profile_bytes
        = collect_u32_values(store, "primary.color_profile_bytes");
    ASSERT_EQ(profile_bytes.size(), 1U);
    EXPECT_EQ(profile_bytes[0], 12U);

    EXPECT_TRUE(
        collect_u16_values(store, "primary.nclx_colour_primaries").empty());
}

TEST(BmffDerivedFieldsDecode, IgnoresShortPrimaryColorProperty)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        const std::array<std::byte, 3> short_colr_payload = {
            std::byte { 'n' },
            std::byte { 'c' },
            std::byte { 'l' },
        };
        std::vector<std::byte> colr_box;
        append_bmff_box(&colr_box, fourcc('c', 'o', 'l', 'r'),
                        short_colr_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), colr_box.begin(),
                            colr_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 1);
        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 1 });
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    EXPECT_TRUE(collect_u32_values(store, "primary.color_type").empty());
    EXPECT_TRUE(collect_text_values(store, "primary.color_type_name").empty());
}

TEST(BmffDerivedFieldsDecode, EmitsIrefEdgesAndPrimaryAuxLinks)
{
    // Minimal HEIF-like BMFF:
    // - ftyp(heic)
    // - meta(pitm primary item id=1)
    // - iref auxl edges: auxiliary items 2,3 -> master item 1

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 2U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 3U, 1U);
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> edge_count
        = collect_u32_values(store, "iref.edge_count");
    ASSERT_EQ(edge_count.size(), 1U);
    EXPECT_EQ(edge_count[0], 2U);

    const std::vector<uint32_t> ref_type = collect_u32_values(store,
                                                              "iref.ref_type");
    ASSERT_EQ(ref_type.size(), 2U);
    EXPECT_EQ(ref_type[0], fourcc('a', 'u', 'x', 'l'));
    EXPECT_EQ(ref_type[1], fourcc('a', 'u', 'x', 'l'));

    const std::vector<uint32_t> from_ids
        = collect_u32_values(store, "iref.from_item_id");
    ASSERT_EQ(from_ids.size(), 2U);
    EXPECT_EQ(from_ids[0], 2U);
    EXPECT_EQ(from_ids[1], 3U);

    const std::vector<uint32_t> to_ids = collect_u32_values(store,
                                                            "iref.to_item_id");
    ASSERT_EQ(to_ids.size(), 2U);
    EXPECT_EQ(to_ids[0], 1U);
    EXPECT_EQ(to_ids[1], 1U);

    const std::vector<uint32_t> item_count
        = collect_u32_values(store, "iref.item_count");
    ASSERT_EQ(item_count.size(), 1U);
    EXPECT_EQ(item_count[0], 3U);
    const std::vector<uint32_t> from_unique_count
        = collect_u32_values(store, "iref.from_item_unique_count");
    ASSERT_EQ(from_unique_count.size(), 1U);
    EXPECT_EQ(from_unique_count[0], 2U);
    const std::vector<uint32_t> to_unique_count
        = collect_u32_values(store, "iref.to_item_unique_count");
    ASSERT_EQ(to_unique_count.size(), 1U);
    EXPECT_EQ(to_unique_count[0], 1U);
    const std::vector<uint32_t> item_ids = collect_u32_values(store,
                                                              "iref.item_id");
    ASSERT_EQ(item_ids.size(), 3U);
    EXPECT_EQ(item_ids[0], 2U);
    EXPECT_EQ(item_ids[1], 1U);
    EXPECT_EQ(item_ids[2], 3U);
    const std::vector<uint32_t> item_out_counts
        = collect_u32_values(store, "iref.item_out_edge_count");
    ASSERT_EQ(item_out_counts.size(), 3U);
    EXPECT_EQ(item_out_counts[0], 1U);
    EXPECT_EQ(item_out_counts[1], 0U);
    EXPECT_EQ(item_out_counts[2], 1U);
    const std::vector<uint32_t> item_in_counts
        = collect_u32_values(store, "iref.item_in_edge_count");
    ASSERT_EQ(item_in_counts.size(), 3U);
    EXPECT_EQ(item_in_counts[0], 0U);
    EXPECT_EQ(item_in_counts[1], 2U);
    EXPECT_EQ(item_in_counts[2], 0U);

    const std::vector<uint32_t> primary_auxl
        = collect_u32_values(store, "primary.auxl_item_id");
    ASSERT_EQ(primary_auxl.size(), 2U);
    EXPECT_EQ(primary_auxl[0], 2U);
    EXPECT_EQ(primary_auxl[1], 3U);
    const std::vector<uint32_t> primary_auxl_count
        = collect_u32_values(store, "primary.auxl_count");
    ASSERT_EQ(primary_auxl_count.size(), 1U);
    EXPECT_EQ(primary_auxl_count[0], 2U);

    const std::vector<uint32_t> auxl_from_unique
        = collect_u32_values(store, "iref.auxl.from_item_unique_count");
    ASSERT_EQ(auxl_from_unique.size(), 1U);
    EXPECT_EQ(auxl_from_unique[0], 2U);
    const std::vector<uint32_t> auxl_to_unique
        = collect_u32_values(store, "iref.auxl.to_item_unique_count");
    ASSERT_EQ(auxl_to_unique.size(), 1U);
    EXPECT_EQ(auxl_to_unique[0], 1U);

    const std::vector<std::string> from_roles
        = collect_text_values(store, "iref.auxl.from_role");
    ASSERT_EQ(from_roles.size(), 2U);
    EXPECT_EQ(from_roles[0], "auxiliary_image");
    EXPECT_EQ(from_roles[1], "auxiliary_image");
    const std::vector<std::string> to_roles
        = collect_text_values(store, "iref.auxl.to_role");
    ASSERT_EQ(to_roles.size(), 2U);
    EXPECT_EQ(to_roles[0], "master_image");
    EXPECT_EQ(to_roles[1], "master_image");
    const std::vector<uint32_t> auxiliary_ids
        = collect_u32_values(store, "iref.auxl.auxiliary_item_id");
    ASSERT_EQ(auxiliary_ids.size(), 2U);
    EXPECT_EQ(auxiliary_ids[0], 2U);
    EXPECT_EQ(auxiliary_ids[1], 3U);
    const std::vector<uint32_t> master_ids
        = collect_u32_values(store, "iref.auxl.master_item_id");
    ASSERT_EQ(master_ids.size(), 2U);
    EXPECT_EQ(master_ids[0], 1U);
    EXPECT_EQ(master_ids[1], 1U);
}

TEST(BmffDerivedFieldsDecode, SeparatesPrimaryDerivedSourcesFromDerivedItems)
{
    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 2U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 3U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    EXPECT_TRUE(collect_u32_values(store, "primary.dimg_count").empty());

    const std::vector<uint32_t> source_count
        = collect_u32_values(store, "primary.source_image_count");
    ASSERT_EQ(source_count.size(), 1U);
    EXPECT_EQ(source_count[0], 2U);
    const std::vector<uint32_t> source_ids
        = collect_u32_values(store, "primary.source_image_item_id");
    ASSERT_EQ(source_ids.size(), 2U);
    EXPECT_EQ(source_ids[0], 2U);
    EXPECT_EQ(source_ids[1], 3U);
    const std::vector<uint32_t> dimg_source_ids
        = collect_u32_values(store, "primary.dimg_source_item_id");
    EXPECT_EQ(dimg_source_ids, source_ids);

    const std::vector<std::string> from_roles
        = collect_text_values(store, "iref.dimg.from_role");
    ASSERT_EQ(from_roles.size(), 2U);
    EXPECT_EQ(from_roles[0], "derived_image");
    EXPECT_EQ(from_roles[1], "derived_image");
    const std::vector<std::string> to_roles
        = collect_text_values(store, "iref.dimg.to_role");
    ASSERT_EQ(to_roles.size(), 2U);
    EXPECT_EQ(to_roles[0], "source_image");
    EXPECT_EQ(to_roles[1], "source_image");
}

TEST(BmffDerivedFieldsDecode, EmitsDerivedImageConstructionSemantics)
{
    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 10U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('g', 'r', 'i', 'd'), "grid");
    append_infe_v2(&iinf_payload, 2U, 0U, fourcc('i', 'o', 'v', 'l'),
                   "overlay");
    append_infe_v2(&iinf_payload, 3U, 0U, fourcc('i', 'd', 'e', 'n'),
                   "identity");
    append_infe_v2(&iinf_payload, 10U, 0U, fourcc('h', 'v', 'c', '1'),
                   "grid_0");
    append_infe_v2(&iinf_payload, 11U, 0U, fourcc('h', 'v', 'c', '1'),
                   "grid_1");
    append_infe_v2(&iinf_payload, 12U, 0U, fourcc('h', 'v', 'c', '1'),
                   "grid_2");
    append_infe_v2(&iinf_payload, 13U, 0U, fourcc('h', 'v', 'c', '1'),
                   "grid_3");
    append_infe_v2(&iinf_payload, 20U, 0U, fourcc('h', 'v', 'c', '1'),
                   "overlay_0");
    append_infe_v2(&iinf_payload, 21U, 0U, fourcc('h', 'v', 'c', '1'),
                   "overlay_1");
    append_infe_v2(&iinf_payload, 30U, 0U, fourcc('h', 'v', 'c', '1'),
                   "identity_source");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 10U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 11U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 12U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 13U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 2U, 20U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 2U, 21U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 3U, 30U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> idat_payload;
    idat_payload.push_back(std::byte { 0U });
    idat_payload.push_back(std::byte { 0U });
    idat_payload.push_back(std::byte { 1U });
    idat_payload.push_back(std::byte { 1U });
    append_u16be(&idat_payload, 640U);
    append_u16be(&idat_payload, 480U);
    const uint32_t overlay_offset = static_cast<uint32_t>(idat_payload.size());
    idat_payload.push_back(std::byte { 0U });
    idat_payload.push_back(std::byte { 0U });
    append_u16be(&idat_payload, 1U);
    append_u16be(&idat_payload, 2U);
    append_u16be(&idat_payload, 3U);
    append_u16be(&idat_payload, 0xFFFFU);
    append_u16be(&idat_payload, 800U);
    append_u16be(&idat_payload, 600U);
    append_u16be(&idat_payload, static_cast<uint16_t>(-10));
    append_u16be(&idat_payload, 20U);
    append_u16be(&idat_payload, 30U);
    append_u16be(&idat_payload, static_cast<uint16_t>(-40));
    const uint32_t overlay_length = static_cast<uint32_t>(idat_payload.size())
                                    - overlay_offset;
    std::vector<std::byte> idat_box;
    append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte { 0x44U });
    iloc_payload.push_back(std::byte { 0x00U });
    append_u16be(&iloc_payload, 2U);
    append_iloc_v1_idat_entry(&iloc_payload, 1U, 0U, 8U);
    append_iloc_v1_idat_entry(&iloc_payload, 2U, overlay_offset,
                              overlay_length);
    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    meta_payload.insert(meta_payload.end(), idat_box.begin(), idat_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> payload {};
    std::array<uint32_t, 64> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;
    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<std::string> construction
        = collect_text_values(store, "derived_image.construction");
    ASSERT_EQ(construction.size(), 3U);
    EXPECT_EQ(construction[0], "grid");
    EXPECT_EQ(construction[1], "overlay");
    EXPECT_EQ(construction[2], "identity");
    const std::vector<uint8_t> construction_valid
        = collect_u8_values(store, "derived_image.construction_valid");
    ASSERT_EQ(construction_valid.size(), 3U);
    EXPECT_EQ(construction_valid[0], 1U);
    EXPECT_EQ(construction_valid[1], 1U);
    EXPECT_EQ(construction_valid[2], 1U);
    const std::vector<uint8_t> descriptor_available
        = collect_u8_values(store, "derived_image.descriptor_available");
    ASSERT_EQ(descriptor_available.size(), 3U);
    EXPECT_EQ(descriptor_available[0], 1U);
    EXPECT_EQ(descriptor_available[1], 1U);
    EXPECT_EQ(descriptor_available[2], 0U);

    const std::vector<uint16_t> grid_rows
        = collect_u16_values(store, "derived_image.grid.rows");
    const std::vector<uint16_t> grid_columns
        = collect_u16_values(store, "derived_image.grid.columns");
    ASSERT_EQ(grid_rows.size(), 1U);
    ASSERT_EQ(grid_columns.size(), 1U);
    EXPECT_EQ(grid_rows[0], 2U);
    EXPECT_EQ(grid_columns[0], 2U);
    const std::vector<uint32_t> grid_source_ids
        = collect_u32_values(store, "derived_image.grid.source_item_id");
    const std::vector<uint32_t> grid_source_rows
        = collect_u32_values(store, "derived_image.grid.source_row");
    const std::vector<uint32_t> grid_source_columns
        = collect_u32_values(store, "derived_image.grid.source_column");
    ASSERT_EQ(grid_source_ids.size(), 4U);
    ASSERT_EQ(grid_source_rows.size(), 4U);
    ASSERT_EQ(grid_source_columns.size(), 4U);
    for (uint32_t i = 0U; i < 4U; ++i) {
        EXPECT_EQ(grid_source_ids[i], 10U + i);
    }
    EXPECT_EQ(grid_source_rows[0], 0U);
    EXPECT_EQ(grid_source_rows[1], 0U);
    EXPECT_EQ(grid_source_rows[2], 1U);
    EXPECT_EQ(grid_source_rows[3], 1U);
    EXPECT_EQ(grid_source_columns[0], 0U);
    EXPECT_EQ(grid_source_columns[1], 1U);
    EXPECT_EQ(grid_source_columns[2], 0U);
    EXPECT_EQ(grid_source_columns[3], 1U);

    const std::vector<uint32_t> overlay_width
        = collect_u32_values(store, "derived_image.overlay.output_width");
    const std::vector<uint32_t> overlay_height
        = collect_u32_values(store, "derived_image.overlay.output_height");
    ASSERT_EQ(overlay_width.size(), 1U);
    ASSERT_EQ(overlay_height.size(), 1U);
    EXPECT_EQ(overlay_width[0], 800U);
    EXPECT_EQ(overlay_height[0], 600U);
    const std::vector<int32_t> overlay_x
        = collect_i32_values(store, "derived_image.overlay.offset_x");
    const std::vector<int32_t> overlay_y
        = collect_i32_values(store, "derived_image.overlay.offset_y");
    ASSERT_EQ(overlay_x.size(), 2U);
    ASSERT_EQ(overlay_y.size(), 2U);
    EXPECT_EQ(overlay_x[0], -10);
    EXPECT_EQ(overlay_y[0], 20);
    EXPECT_EQ(overlay_x[1], 30);
    EXPECT_EQ(overlay_y[1], -40);

    const std::vector<uint32_t> identity_source
        = collect_u32_values(store, "derived_image.identity.source_item_id");
    ASSERT_EQ(identity_source.size(), 1U);
    EXPECT_EQ(identity_source[0], 30U);
    const std::vector<std::string> primary_construction
        = collect_text_values(store, "primary.derived_construction");
    ASSERT_EQ(primary_construction.size(), 1U);
    EXPECT_EQ(primary_construction[0], "grid");
    const std::vector<uint16_t> primary_rows
        = collect_u16_values(store, "primary.derived_grid_rows");
    ASSERT_EQ(primary_rows.size(), 1U);
    EXPECT_EQ(primary_rows[0], 2U);

    const std::vector<uint32_t> derived_semantic_count
        = collect_u32_values(store, "item.semantic_derived_count");
    ASSERT_EQ(derived_semantic_count.size(), 1U);
    EXPECT_EQ(derived_semantic_count[0], 3U);
}

TEST(BmffDerivedFieldsDecode, ReadsDerivedDescriptorFromFileOffsetExtent)
{
    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 2U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('g', 'r', 'i', 'd'),
                   "file_grid");
    append_infe_v2(&iinf_payload, 10U, 0U, fourcc('h', 'v', 'c', '1'),
                   "grid_source");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 10U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> descriptor;
    descriptor.push_back(std::byte { 0U });
    descriptor.push_back(std::byte { 0U });
    descriptor.push_back(std::byte { 0U });
    descriptor.push_back(std::byte { 0U });
    append_u16be(&descriptor, 320U);
    append_u16be(&descriptor, 240U);

    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte { 0x44U });
    iloc_payload.push_back(std::byte { 0x00U });
    append_u16be(&iloc_payload, 1U);
    append_iloc_v1_split_file_entry(&iloc_payload, 1U, 0U, 4U, 4U);
    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    const uint32_t descriptor_offset = static_cast<uint32_t>(
        file.size() + 8U + meta_payload.size() + 8U);

    iloc_payload.clear();
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte { 0x44U });
    iloc_payload.push_back(std::byte { 0x00U });
    append_u16be(&iloc_payload, 1U);
    append_iloc_v1_split_file_entry(&iloc_payload, 1U, descriptor_offset, 4U,
                                    4U);
    iloc_box.clear();
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);
    meta_payload.clear();
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    append_bmff_box(&file, fourcc('m', 'd', 'a', 't'), descriptor);

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 2048> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;
    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint8_t> construction_valid
        = collect_u8_values(store, "derived_image.construction_valid");
    ASSERT_EQ(construction_valid.size(), 1U);
    EXPECT_EQ(construction_valid[0], 1U);
    const std::vector<uint32_t> width
        = collect_u32_values(store, "derived_image.grid.output_width");
    const std::vector<uint32_t> height
        = collect_u32_values(store, "derived_image.grid.output_height");
    ASSERT_EQ(width.size(), 1U);
    ASSERT_EQ(height.size(), 1U);
    EXPECT_EQ(width[0], 320U);
    EXPECT_EQ(height[0], 240U);
}

TEST(BmffDerivedFieldsDecode, ReadsDerivedDescriptorThroughItemOffsets)
{
    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 4U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('g', 'r', 'i', 'd'),
                   "item_grid");
    append_infe_v2(&iinf_payload, 2U, 0U, fourcc('h', 'v', 'c', '1'),
                   "slice_1");
    append_infe_v2(&iinf_payload, 3U, 0U, fourcc('h', 'v', 'c', '1'),
                   "slice_2");
    append_infe_v2(&iinf_payload, 10U, 0U, fourcc('h', 'v', 'c', '1'),
                   "grid_source");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 10U);
    append_iref_v0_edge(&iref_payload, fourcc('i', 'l', 'o', 'c'), 1U, 2U);
    append_iref_v0_edge(&iref_payload, fourcc('i', 'l', 'o', 'c'), 2U, 3U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> idat_payload;
    idat_payload.push_back(std::byte { 0xAAU });
    idat_payload.push_back(std::byte { 0xBBU });
    idat_payload.push_back(std::byte { 0U });
    idat_payload.push_back(std::byte { 0U });
    idat_payload.push_back(std::byte { 0U });
    idat_payload.push_back(std::byte { 0U });
    append_u16be(&idat_payload, 1024U);
    append_u16be(&idat_payload, 768U);
    std::vector<std::byte> idat_box;
    append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte { 0x44U });
    iloc_payload.push_back(std::byte { 0x04U });
    append_u16be(&iloc_payload, 3U);
    append_iloc_v1_indexed_entry(&iloc_payload, 1U, 2U, 1U, 1U, 8U);
    append_iloc_v1_indexed_entry(&iloc_payload, 2U, 2U, 1U, 1U, 9U);
    append_iloc_v1_indexed_entry(&iloc_payload, 3U, 1U, 0U, 0U, 10U);
    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    meta_payload.insert(meta_payload.end(), idat_box.begin(), idat_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    MetaStore store;
    decode_bmff_test_file(file, &store);

    const std::vector<uint8_t> construction_valid
        = collect_u8_values(store, "derived_image.construction_valid");
    ASSERT_EQ(construction_valid.size(), 1U);
    EXPECT_EQ(construction_valid[0], 1U);
    const std::vector<uint32_t> width
        = collect_u32_values(store, "derived_image.grid.output_width");
    const std::vector<uint32_t> height
        = collect_u32_values(store, "derived_image.grid.output_height");
    const std::vector<uint32_t> depth
        = collect_u32_values(store, "derived_image.descriptor_reference_depth");
    ASSERT_EQ(width.size(), 1U);
    ASSERT_EQ(height.size(), 1U);
    ASSERT_EQ(depth.size(), 1U);
    EXPECT_EQ(width[0], 1024U);
    EXPECT_EQ(height[0], 768U);
    EXPECT_EQ(depth[0], 2U);
}

TEST(BmffDerivedFieldsDecode, RejectsInvalidItemOffsetIndexAndCycle)
{
    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 3U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('g', 'r', 'i', 'd'),
                   "bad_index");
    append_infe_v2(&iinf_payload, 2U, 0U, fourcc('g', 'r', 'i', 'd'), "cycle");
    append_infe_v2(&iinf_payload, 10U, 0U, fourcc('h', 'v', 'c', '1'),
                   "source");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 10U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 2U, 10U);
    append_iref_v0_edge(&iref_payload, fourcc('i', 'l', 'o', 'c'), 1U, 2U);
    append_iref_v0_edge(&iref_payload, fourcc('i', 'l', 'o', 'c'), 2U, 2U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte { 0x44U });
    iloc_payload.push_back(std::byte { 0x04U });
    append_u16be(&iloc_payload, 2U);
    append_iloc_v1_indexed_entry(&iloc_payload, 1U, 2U, 2U, 0U, 8U);
    append_iloc_v1_indexed_entry(&iloc_payload, 2U, 2U, 1U, 0U, 8U);
    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    MetaStore store;
    decode_bmff_test_file(file, &store);

    const std::vector<uint8_t> available
        = collect_u8_values(store, "derived_image.descriptor_available");
    const std::vector<uint8_t> valid
        = collect_u8_values(store, "derived_image.construction_valid");
    ASSERT_EQ(available.size(), 2U);
    ASSERT_EQ(valid.size(), 2U);
    EXPECT_EQ(available[0], 0U);
    EXPECT_EQ(available[1], 0U);
    EXPECT_EQ(valid[0], 0U);
    EXPECT_EQ(valid[1], 0U);
}

TEST(BmffDerivedFieldsDecode, RejectsDerivedGraphCyclesAndMissingSources)
{
    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 3U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('i', 'd', 'e', 'n'),
                   "cycle_a");
    append_infe_v2(&iinf_payload, 2U, 0U, fourcc('i', 'd', 'e', 'n'),
                   "cycle_b");
    append_infe_v2(&iinf_payload, 3U, 0U, fourcc('i', 'd', 'e', 'n'),
                   "missing");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 2U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 2U, 1U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 3U, 99U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    MetaStore store;
    decode_bmff_test_file(file, &store);

    const std::vector<uint8_t> graph_cycle
        = collect_u8_values(store, "derived_image.graph_cycle");
    const std::vector<uint8_t> graph_valid
        = collect_u8_values(store, "derived_image.graph_valid");
    const std::vector<uint32_t> missing
        = collect_u32_values(store, "derived_image.graph_missing_source_count");
    const std::vector<uint8_t> construction_valid
        = collect_u8_values(store, "derived_image.construction_valid");
    const std::vector<uint8_t> primary_cycle
        = collect_u8_values(store, "primary.derived_graph_cycle");
    const std::vector<uint32_t> primary_depth
        = collect_u32_values(store, "primary.derived_graph_max_depth");
    ASSERT_EQ(graph_cycle.size(), 3U);
    ASSERT_EQ(graph_valid.size(), 3U);
    ASSERT_EQ(missing.size(), 3U);
    ASSERT_EQ(construction_valid.size(), 3U);
    ASSERT_EQ(primary_cycle.size(), 1U);
    ASSERT_EQ(primary_depth.size(), 1U);
    EXPECT_EQ(graph_cycle[0], 1U);
    EXPECT_EQ(graph_cycle[1], 1U);
    EXPECT_EQ(graph_cycle[2], 0U);
    EXPECT_EQ(graph_valid[0], 0U);
    EXPECT_EQ(graph_valid[1], 0U);
    EXPECT_EQ(graph_valid[2], 0U);
    EXPECT_EQ(missing[0], 0U);
    EXPECT_EQ(missing[1], 0U);
    EXPECT_EQ(missing[2], 1U);
    EXPECT_EQ(construction_valid[0], 0U);
    EXPECT_EQ(construction_valid[1], 0U);
    EXPECT_EQ(construction_valid[2], 0U);
    EXPECT_EQ(primary_cycle[0], 1U);
    EXPECT_EQ(primary_depth[0], 1U);
}

TEST(BmffDerivedFieldsDecode, RejectsTruncatedDerivedReferenceGraph)
{
    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 2U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('i', 'd', 'e', 'n'),
                   "identity");
    append_infe_v2(&iinf_payload, 10U, 0U, fourcc('h', 'v', 'c', '1'),
                   "source");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    for (uint32_t i = 0U; i < 513U; ++i) {
        append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 10U);
    }
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    MetaStore store;
    decode_bmff_test_file(file, &store);

    const std::vector<uint8_t> truncated
        = collect_u8_values(store, "derived_image.graph_references_truncated");
    const std::vector<uint8_t> graph_valid
        = collect_u8_values(store, "derived_image.graph_valid");
    const std::vector<uint32_t> truncated_count
        = collect_u32_values(store,
                             "derived_image.graph_references_truncated_count");
    ASSERT_EQ(truncated.size(), 1U);
    ASSERT_EQ(graph_valid.size(), 1U);
    ASSERT_EQ(truncated_count.size(), 1U);
    EXPECT_EQ(truncated[0], 1U);
    EXPECT_EQ(graph_valid[0], 0U);
    EXPECT_EQ(truncated_count[0], 1U);
}

TEST(BmffDerivedFieldsDecode, RejectsInvalidDerivedImageConstructions)
{
    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 3U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('g', 'r', 'i', 'd'),
                   "bad_grid");
    append_infe_v2(&iinf_payload, 2U, 0U, fourcc('i', 'o', 'v', 'l'),
                   "bad_overlay");
    append_infe_v2(&iinf_payload, 3U, 0U, fourcc('i', 'd', 'e', 'n'),
                   "self_identity");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 10U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 11U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 1U, 12U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 2U, 20U);
    append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 3U, 3U);
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> idat_payload;
    idat_payload.push_back(std::byte { 0U });
    idat_payload.push_back(std::byte { 0U });
    idat_payload.push_back(std::byte { 1U });
    idat_payload.push_back(std::byte { 1U });
    append_u16be(&idat_payload, 640U);
    append_u16be(&idat_payload, 480U);
    const uint32_t overlay_offset = static_cast<uint32_t>(idat_payload.size());
    idat_payload.push_back(std::byte { 0U });
    idat_payload.push_back(std::byte { 0U });
    append_u16be(&idat_payload, 1U);
    std::vector<std::byte> idat_box;
    append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1U);
    iloc_payload.push_back(std::byte { 0x44U });
    iloc_payload.push_back(std::byte { 0x00U });
    append_u16be(&iloc_payload, 2U);
    append_iloc_v1_idat_entry(&iloc_payload, 1U, 0U, 8U);
    append_iloc_v1_idat_entry(&iloc_payload, 2U, overlay_offset, 4U);
    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    meta_payload.insert(meta_payload.end(), idat_box.begin(), idat_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> payload {};
    std::array<uint32_t, 64> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;
    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint8_t> descriptor_available
        = collect_u8_values(store, "derived_image.descriptor_available");
    const std::vector<uint8_t> descriptor_valid
        = collect_u8_values(store, "derived_image.descriptor_valid");
    const std::vector<uint8_t> source_count_valid
        = collect_u8_values(store, "derived_image.source_count_valid");
    const std::vector<uint8_t> construction_valid
        = collect_u8_values(store, "derived_image.construction_valid");
    ASSERT_EQ(descriptor_available.size(), 3U);
    ASSERT_EQ(descriptor_valid.size(), 3U);
    ASSERT_EQ(source_count_valid.size(), 3U);
    ASSERT_EQ(construction_valid.size(), 3U);
    EXPECT_EQ(descriptor_available[0], 1U);
    EXPECT_EQ(descriptor_available[1], 1U);
    EXPECT_EQ(descriptor_available[2], 0U);
    EXPECT_EQ(descriptor_valid[0], 1U);
    EXPECT_EQ(descriptor_valid[1], 0U);
    EXPECT_EQ(descriptor_valid[2], 1U);
    EXPECT_EQ(source_count_valid[0], 0U);
    EXPECT_EQ(source_count_valid[1], 1U);
    EXPECT_EQ(source_count_valid[2], 1U);
    EXPECT_EQ(construction_valid[0], 0U);
    EXPECT_EQ(construction_valid[1], 0U);
    EXPECT_EQ(construction_valid[2], 0U);

    EXPECT_TRUE(
        collect_i32_values(store, "derived_image.overlay.offset_x").empty());
    const std::vector<uint32_t> identity_source
        = collect_u32_values(store, "derived_image.identity.source_item_id");
    ASSERT_EQ(identity_source.size(), 1U);
    EXPECT_EQ(identity_source[0], 3U);
    const std::vector<uint8_t> primary_valid
        = collect_u8_values(store, "primary.derived_construction_valid");
    ASSERT_EQ(primary_valid.size(), 1U);
    EXPECT_EQ(primary_valid[0], 0U);
}

TEST(BmffDerivedFieldsDecode, EmitsItemGroupsAndPrimaryMembership)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> grpl_payload;
        const std::array<uint32_t, 3> altr_entities { 1U, 2U, 3U };
        const std::array<uint32_t, 2> ster_entities { 4U, 5U };
        append_entity_group_box(&grpl_payload, fourcc('a', 'l', 't', 'r'), 10U,
                                altr_entities);
        append_entity_group_box(&grpl_payload, fourcc('s', 't', 'e', 'r'), 20U,
                                ster_entities);
        std::vector<std::byte> grpl_box;
        append_bmff_box(&grpl_box, fourcc('g', 'r', 'p', 'l'), grpl_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), grpl_box.begin(),
                            grpl_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 2048> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> group_count
        = collect_u32_values(store, "item_group.count");
    ASSERT_EQ(group_count.size(), 1U);
    EXPECT_EQ(group_count[0], 2U);

    const std::vector<uint32_t> group_types
        = collect_u32_values(store, "item_group.type");
    ASSERT_EQ(group_types.size(), 2U);
    EXPECT_EQ(group_types[0], fourcc('a', 'l', 't', 'r'));
    EXPECT_EQ(group_types[1], fourcc('s', 't', 'e', 'r'));

    const std::vector<std::string> group_type_names
        = collect_text_values(store, "item_group.type_name");
    ASSERT_EQ(group_type_names.size(), 2U);
    EXPECT_EQ(group_type_names[0], "altr");
    EXPECT_EQ(group_type_names[1], "ster");

    const std::vector<std::string> group_semantics
        = collect_text_values(store, "item_group.semantic");
    ASSERT_EQ(group_semantics.size(), 2U);
    EXPECT_EQ(group_semantics[0], "alternatives");
    EXPECT_EQ(group_semantics[1], "stereo_pair");

    const std::vector<uint32_t> group_ids = collect_u32_values(store,
                                                               "item_group.id");
    ASSERT_EQ(group_ids.size(), 2U);
    EXPECT_EQ(group_ids[0], 10U);
    EXPECT_EQ(group_ids[1], 20U);

    const std::vector<uint32_t> entity_counts
        = collect_u32_values(store, "item_group.entity_count");
    ASSERT_EQ(entity_counts.size(), 2U);
    EXPECT_EQ(entity_counts[0], 3U);
    EXPECT_EQ(entity_counts[1], 2U);

    const std::vector<uint32_t> entity_ids
        = collect_u32_values(store, "item_group.entity_id");
    ASSERT_EQ(entity_ids.size(), 5U);
    EXPECT_EQ(entity_ids[0], 1U);
    EXPECT_EQ(entity_ids[1], 2U);
    EXPECT_EQ(entity_ids[2], 3U);
    EXPECT_EQ(entity_ids[3], 4U);
    EXPECT_EQ(entity_ids[4], 5U);

    const std::vector<uint32_t> entity_indices
        = collect_u32_values(store, "item_group.entity_index");
    ASSERT_EQ(entity_indices.size(), 5U);
    EXPECT_EQ(entity_indices[0], 0U);
    EXPECT_EQ(entity_indices[1], 1U);
    EXPECT_EQ(entity_indices[2], 2U);
    EXPECT_EQ(entity_indices[3], 0U);
    EXPECT_EQ(entity_indices[4], 1U);
    const std::vector<std::string> entity_roles
        = collect_text_values(store, "item_group.entity_role");
    ASSERT_EQ(entity_roles.size(), 5U);
    EXPECT_EQ(entity_roles[0], "alternative");
    EXPECT_EQ(entity_roles[1], "alternative");
    EXPECT_EQ(entity_roles[2], "alternative");
    EXPECT_EQ(entity_roles[3], "left_image");
    EXPECT_EQ(entity_roles[4], "right_image");

    const std::vector<uint32_t> altr_count
        = collect_u32_values(store, "item_group.altr.count");
    ASSERT_EQ(altr_count.size(), 1U);
    EXPECT_EQ(altr_count[0], 1U);
    const std::vector<uint32_t> altr_id
        = collect_u32_values(store, "item_group.altr.id");
    ASSERT_EQ(altr_id.size(), 1U);
    EXPECT_EQ(altr_id[0], 10U);
    const std::vector<uint32_t> altr_entities
        = collect_u32_values(store, "item_group.altr.entity_id");
    ASSERT_EQ(altr_entities.size(), 3U);
    EXPECT_EQ(altr_entities[0], 1U);
    EXPECT_EQ(altr_entities[1], 2U);
    EXPECT_EQ(altr_entities[2], 3U);

    const std::vector<uint32_t> ster_count
        = collect_u32_values(store, "item_group.ster.count");
    ASSERT_EQ(ster_count.size(), 1U);
    EXPECT_EQ(ster_count[0], 1U);
    const std::vector<uint32_t> ster_id
        = collect_u32_values(store, "item_group.ster.id");
    ASSERT_EQ(ster_id.size(), 1U);
    EXPECT_EQ(ster_id[0], 20U);

    const std::vector<uint32_t> primary_group_count
        = collect_u32_values(store, "primary.item_group_count");
    ASSERT_EQ(primary_group_count.size(), 1U);
    EXPECT_EQ(primary_group_count[0], 1U);
    const std::vector<uint32_t> primary_group_type
        = collect_u32_values(store, "primary.item_group_type");
    ASSERT_EQ(primary_group_type.size(), 1U);
    EXPECT_EQ(primary_group_type[0], fourcc('a', 'l', 't', 'r'));
    const std::vector<std::string> primary_group_type_name
        = collect_text_values(store, "primary.item_group_type_name");
    ASSERT_EQ(primary_group_type_name.size(), 1U);
    EXPECT_EQ(primary_group_type_name[0], "altr");
    const std::vector<uint32_t> primary_group_id
        = collect_u32_values(store, "primary.item_group_id");
    ASSERT_EQ(primary_group_id.size(), 1U);
    EXPECT_EQ(primary_group_id[0], 10U);
    const std::vector<uint32_t> primary_group_entity_count
        = collect_u32_values(store, "primary.item_group_entity_count");
    ASSERT_EQ(primary_group_entity_count.size(), 1U);
    EXPECT_EQ(primary_group_entity_count[0], 3U);
    const std::vector<std::string> primary_group_semantic
        = collect_text_values(store, "primary.item_group_semantic");
    ASSERT_EQ(primary_group_semantic.size(), 1U);
    EXPECT_EQ(primary_group_semantic[0], "alternatives");
    const std::vector<uint32_t> primary_group_index
        = collect_u32_values(store, "primary.item_group_primary_entity_index");
    ASSERT_EQ(primary_group_index.size(), 1U);
    EXPECT_EQ(primary_group_index[0], 0U);
    const std::vector<std::string> primary_group_role
        = collect_text_values(store, "primary.item_group_primary_role");
    ASSERT_EQ(primary_group_role.size(), 1U);
    EXPECT_EQ(primary_group_role[0], "alternative");
    const std::vector<uint32_t> primary_group_other_count
        = collect_u32_values(store, "primary.item_group_other_entity_count");
    ASSERT_EQ(primary_group_other_count.size(), 1U);
    EXPECT_EQ(primary_group_other_count[0], 2U);
}

TEST(BmffDerivedFieldsDecode, EmitsItemLocationAndIdatSummaries)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // offset/length sizes
        iloc_payload.push_back(std::byte { 0x40 });  // base/index sizes
        append_u16be(&iloc_payload, 2);              // item_count

        append_u16be(&iloc_payload, 1);  // item id
        append_u16be(&iloc_payload, 1);  // construction_method=idat offset
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u32be(&iloc_payload, 0);  // base_offset
        append_u16be(&iloc_payload, 2);  // extent_count
        append_u32be(&iloc_payload, 4);
        append_u32be(&iloc_payload, 12);
        append_u32be(&iloc_payload, 20);
        append_u32be(&iloc_payload, 8);

        append_u16be(&iloc_payload, 2);  // item id
        append_u16be(&iloc_payload, 0);  // construction_method=file offset
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u32be(&iloc_payload, 1000);
        append_u16be(&iloc_payload, 1);
        append_u32be(&iloc_payload, 32);
        append_u32be(&iloc_payload, 16);
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> idat_payload(64, std::byte { 0x33 });
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 2048> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint64_t> idat_bytes = collect_u64_values(store,
                                                                "idat.bytes");
    ASSERT_EQ(idat_bytes.size(), 1U);
    EXPECT_EQ(idat_bytes[0], 64U);

    const std::vector<uint32_t> item_location_count
        = collect_u32_values(store, "item_location.count");
    ASSERT_EQ(item_location_count.size(), 1U);
    EXPECT_EQ(item_location_count[0], 2U);

    const std::vector<uint8_t> version
        = collect_u8_values(store, "item_location.version");
    ASSERT_EQ(version.size(), 1U);
    EXPECT_EQ(version[0], 1U);
    const std::vector<uint8_t> offset_size
        = collect_u8_values(store, "item_location.offset_size");
    ASSERT_EQ(offset_size.size(), 1U);
    EXPECT_EQ(offset_size[0], 4U);
    const std::vector<uint8_t> length_size
        = collect_u8_values(store, "item_location.length_size");
    ASSERT_EQ(length_size.size(), 1U);
    EXPECT_EQ(length_size[0], 4U);
    const std::vector<uint8_t> base_offset_size
        = collect_u8_values(store, "item_location.base_offset_size");
    ASSERT_EQ(base_offset_size.size(), 1U);
    EXPECT_EQ(base_offset_size[0], 4U);

    const std::vector<uint32_t> item_ids
        = collect_u32_values(store, "item_location.item_id");
    ASSERT_EQ(item_ids.size(), 2U);
    EXPECT_EQ(item_ids[0], 1U);
    EXPECT_EQ(item_ids[1], 2U);

    const std::vector<uint16_t> methods
        = collect_u16_values(store, "item_location.construction_method");
    ASSERT_EQ(methods.size(), 2U);
    EXPECT_EQ(methods[0], 1U);
    EXPECT_EQ(methods[1], 0U);
    const std::vector<std::string> method_names
        = collect_text_values(store, "item_location.construction_method_name");
    ASSERT_EQ(method_names.size(), 2U);
    EXPECT_EQ(method_names[0], "idat_offset");
    EXPECT_EQ(method_names[1], "file_offset");

    const std::vector<uint64_t> base_offsets
        = collect_u64_values(store, "item_location.base_offset");
    ASSERT_EQ(base_offsets.size(), 2U);
    EXPECT_EQ(base_offsets[0], 0U);
    EXPECT_EQ(base_offsets[1], 1000U);
    const std::vector<uint64_t> total_bytes
        = collect_u64_values(store, "item_location.total_extent_bytes");
    ASSERT_EQ(total_bytes.size(), 2U);
    EXPECT_EQ(total_bytes[0], 20U);
    EXPECT_EQ(total_bytes[1], 16U);

    const std::vector<uint64_t> extent_offsets
        = collect_u64_values(store, "item_location.extent_offset");
    ASSERT_EQ(extent_offsets.size(), 3U);
    EXPECT_EQ(extent_offsets[0], 4U);
    EXPECT_EQ(extent_offsets[1], 20U);
    EXPECT_EQ(extent_offsets[2], 32U);
    const std::vector<uint64_t> extent_lengths
        = collect_u64_values(store, "item_location.extent_length");
    ASSERT_EQ(extent_lengths.size(), 3U);
    EXPECT_EQ(extent_lengths[0], 12U);
    EXPECT_EQ(extent_lengths[1], 8U);
    EXPECT_EQ(extent_lengths[2], 16U);

    const std::vector<uint32_t> idat_item_count
        = collect_u32_values(store, "item_location.idat_item_count");
    ASSERT_EQ(idat_item_count.size(), 1U);
    EXPECT_EQ(idat_item_count[0], 1U);

    const std::vector<uint32_t> primary_item_id
        = collect_u32_values(store, "primary.item_location.item_id");
    ASSERT_EQ(primary_item_id.size(), 1U);
    EXPECT_EQ(primary_item_id[0], 1U);
    const std::vector<uint16_t> primary_method
        = collect_u16_values(store,
                             "primary.item_location.construction_method");
    ASSERT_EQ(primary_method.size(), 1U);
    EXPECT_EQ(primary_method[0], 1U);
    const std::vector<uint64_t> primary_total
        = collect_u64_values(store, "primary.item_location.total_extent_bytes");
    ASSERT_EQ(primary_total.size(), 1U);
    EXPECT_EQ(primary_total[0], 20U);
}

TEST(BmffDerivedFieldsDecode, EmitsIrefEdgesForVersion1ItemIds)
{
    // Same auxl edge semantics as the v0 test, but with 32-bit item IDs in
    // pitm/iref (version=1 fullboxes).
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        const uint32_t kPrimary = 0x10001U;
        const uint32_t kAuxA    = 0x10002U;
        const uint32_t kAuxB    = 0x10003U;

        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 1);
        append_u32be(&pitm_payload, kPrimary);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 1);
        append_iref_v1_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), kAuxA,
                            kPrimary);
        append_iref_v1_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), kAuxB,
                            kPrimary);
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> edge_count
        = collect_u32_values(store, "iref.edge_count");
    ASSERT_EQ(edge_count.size(), 1U);
    EXPECT_EQ(edge_count[0], 2U);

    const std::vector<uint32_t> primary_auxl
        = collect_u32_values(store, "primary.auxl_item_id");
    ASSERT_EQ(primary_auxl.size(), 2U);
    EXPECT_EQ(primary_auxl[0], 0x10002U);
    EXPECT_EQ(primary_auxl[1], 0x10003U);
    const std::vector<uint32_t> primary_auxl_count
        = collect_u32_values(store, "primary.auxl_count");
    ASSERT_EQ(primary_auxl_count.size(), 1U);
    EXPECT_EQ(primary_auxl_count[0], 2U);
}

TEST(BmffDerivedFieldsDecode, EmitsNonPrimaryIrefTypedEdgesForVersion1ItemIds)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        const uint32_t kPrimary  = 0x10001U;
        const uint32_t kFromDimg = 0x20002U;
        const uint32_t kFromThmb = 0x20003U;
        const uint32_t kFromCdsc = 0x20004U;
        const uint32_t kToDimgA  = 0x30005U;
        const uint32_t kToDimgB  = 0x30006U;
        const uint32_t kToThmb   = 0x30007U;
        const uint32_t kToCdsc   = 0x30008U;

        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 1);
        append_u32be(&pitm_payload, kPrimary);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> dimg_payload;
        append_u32be(&dimg_payload, kFromDimg);
        append_u16be(&dimg_payload, 2);
        append_u32be(&dimg_payload, kToDimgA);
        append_u32be(&dimg_payload, kToDimgB);
        std::vector<std::byte> dimg_box;
        append_bmff_box(&dimg_box, fourcc('d', 'i', 'm', 'g'), dimg_payload);

        std::vector<std::byte> thmb_payload;
        append_u32be(&thmb_payload, kFromThmb);
        append_u16be(&thmb_payload, 1);
        append_u32be(&thmb_payload, kToThmb);
        std::vector<std::byte> thmb_box;
        append_bmff_box(&thmb_box, fourcc('t', 'h', 'm', 'b'), thmb_payload);

        std::vector<std::byte> cdsc_payload;
        append_u32be(&cdsc_payload, kFromCdsc);
        append_u16be(&cdsc_payload, 1);
        append_u32be(&cdsc_payload, kToCdsc);
        std::vector<std::byte> cdsc_box;
        append_bmff_box(&cdsc_box, fourcc('c', 'd', 's', 'c'), cdsc_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 1);
        iref_payload.insert(iref_payload.end(), dimg_box.begin(),
                            dimg_box.end());
        iref_payload.insert(iref_payload.end(), thmb_box.begin(),
                            thmb_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_box.begin(),
                            cdsc_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> edge_count
        = collect_u32_values(store, "iref.edge_count");
    ASSERT_EQ(edge_count.size(), 1U);
    EXPECT_EQ(edge_count[0], 4U);

    const std::vector<uint32_t> dimg_count
        = collect_u32_values(store, "iref.dimg.edge_count");
    ASSERT_EQ(dimg_count.size(), 1U);
    EXPECT_EQ(dimg_count[0], 2U);
    const std::vector<uint32_t> thmb_count
        = collect_u32_values(store, "iref.thmb.edge_count");
    ASSERT_EQ(thmb_count.size(), 1U);
    EXPECT_EQ(thmb_count[0], 1U);
    const std::vector<uint32_t> cdsc_count
        = collect_u32_values(store, "iref.cdsc.edge_count");
    ASSERT_EQ(cdsc_count.size(), 1U);
    EXPECT_EQ(cdsc_count[0], 1U);

    const std::vector<uint32_t> dimg_from
        = collect_u32_values(store, "iref.dimg.from_item_id");
    ASSERT_EQ(dimg_from.size(), 2U);
    EXPECT_EQ(dimg_from[0], 0x20002U);
    EXPECT_EQ(dimg_from[1], 0x20002U);
    const std::vector<uint32_t> dimg_to
        = collect_u32_values(store, "iref.dimg.to_item_id");
    ASSERT_EQ(dimg_to.size(), 2U);
    EXPECT_EQ(dimg_to[0], 0x30005U);
    EXPECT_EQ(dimg_to[1], 0x30006U);

    const std::vector<uint32_t> thmb_from
        = collect_u32_values(store, "iref.thmb.from_item_id");
    ASSERT_EQ(thmb_from.size(), 1U);
    EXPECT_EQ(thmb_from[0], 0x20003U);
    const std::vector<uint32_t> thmb_to
        = collect_u32_values(store, "iref.thmb.to_item_id");
    ASSERT_EQ(thmb_to.size(), 1U);
    EXPECT_EQ(thmb_to[0], 0x30007U);

    const std::vector<uint32_t> cdsc_from
        = collect_u32_values(store, "iref.cdsc.from_item_id");
    ASSERT_EQ(cdsc_from.size(), 1U);
    EXPECT_EQ(cdsc_from[0], 0x20004U);
    const std::vector<uint32_t> cdsc_to
        = collect_u32_values(store, "iref.cdsc.to_item_id");
    ASSERT_EQ(cdsc_to.size(), 1U);
    EXPECT_EQ(cdsc_to[0], 0x30008U);

    EXPECT_TRUE(collect_u32_values(store, "primary.dimg_item_id").empty());
    EXPECT_TRUE(collect_u32_values(store, "primary.thmb_item_id").empty());
    EXPECT_TRUE(collect_u32_values(store, "primary.cdsc_item_id").empty());
}

TEST(BmffDerivedFieldsDecode, EmitsPrimaryAuxSemanticsFromAuxC)
{
    // Minimal HEIF-like BMFF:
    // - primary item id = 1
    // - iref auxl edges: auxiliary items 2,3 -> master item 1
    // - ipco has auxC properties:
    //   - prop #1: urn:mpeg:hevc:2015:auxid:2 (depth)
    //   - prop #2: urn:mpeg:hevc:2015:auxid:1 (alpha)
    // - ipma maps item 2 -> prop #1, item 3 -> prop #2

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 2U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 3U, 1U);
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> auxc_depth_payload;
        static constexpr char kDepth[] = "urn:mpeg:hevc:2015:auxid:2";
        append_auxc_payload(&auxc_depth_payload, kDepth, {});
        auxc_depth_payload.push_back(std::byte { 0xAA });
        auxc_depth_payload.push_back(std::byte { 0xBB });
        std::vector<std::byte> auxc_depth_box;
        append_bmff_box(&auxc_depth_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_depth_payload);

        std::vector<std::byte> auxc_alpha_payload;
        static constexpr char kAlpha[] = "urn:mpeg:hevc:2015:auxid:1";
        append_auxc_payload(&auxc_alpha_payload, kAlpha, {});
        auxc_alpha_payload.push_back(std::byte { 0x11 });
        std::vector<std::byte> auxc_alpha_box;
        append_bmff_box(&auxc_alpha_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_alpha_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), auxc_depth_box.begin(),
                            auxc_depth_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_alpha_box.begin(),
                            auxc_alpha_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 3);  // entry_count

        append_u16be(&ipma_payload, 1);           // item id (primary)
        ipma_payload.push_back(std::byte { 0 });  // association_count

        append_u16be(&ipma_payload, 2);           // item id (aux depth)
        ipma_payload.push_back(std::byte { 1 });  // association_count
        ipma_payload.push_back(std::byte { 1 });  // property_index=1

        append_u16be(&ipma_payload, 3);           // item id (aux alpha)
        ipma_payload.push_back(std::byte { 1 });  // association_count
        ipma_payload.push_back(std::byte { 2 });  // property_index=2

        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 6);
        append_infe_v2(&iinf_payload, 2, 0, fourcc('a', 'u', 'x', 'l'),
                       "depth_aux");
        append_infe_v2(&iinf_payload, 3, 0, fourcc('a', 'u', 'x', 'l'),
                       "alpha_aux");
        append_infe_v2(&iinf_payload, 4, 0, fourcc('d', 'e', 'r', 'v'),
                       "derived");
        append_infe_v2(&iinf_payload, 5, 0, fourcc('t', 'h', 'm', 'b'),
                       "thumb");
        append_infe_v2(&iinf_payload, 6, 0, fourcc('c', 'd', 's', 'c'),
                       "caption");
        append_infe_v2(&iinf_payload, 7, 0, fourcc('a', 'u', 'x', 'l'),
                       "other_aux");
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> primary_auxl
        = collect_u32_values(store, "primary.auxl_item_id");
    ASSERT_EQ(primary_auxl.size(), 2U);
    EXPECT_EQ(primary_auxl[0], 2U);
    EXPECT_EQ(primary_auxl[1], 3U);
    const std::vector<uint32_t> primary_auxl_count
        = collect_u32_values(store, "primary.auxl_count");
    ASSERT_EQ(primary_auxl_count.size(), 1U);
    EXPECT_EQ(primary_auxl_count[0], 2U);

    const std::vector<std::string> primary_auxl_semantic
        = collect_text_values(store, "primary.auxl_semantic");
    ASSERT_EQ(primary_auxl_semantic.size(), 2U);
    EXPECT_EQ(primary_auxl_semantic[0], "depth");
    EXPECT_EQ(primary_auxl_semantic[1], "alpha");

    const std::vector<uint32_t> depth_ids
        = collect_u32_values(store, "primary.depth_item_id");
    ASSERT_EQ(depth_ids.size(), 1U);
    EXPECT_EQ(depth_ids[0], 2U);
    const std::vector<uint32_t> depth_count
        = collect_u32_values(store, "primary.depth_count");
    ASSERT_EQ(depth_count.size(), 1U);
    EXPECT_EQ(depth_count[0], 1U);

    const std::vector<uint32_t> alpha_ids
        = collect_u32_values(store, "primary.alpha_item_id");
    ASSERT_EQ(alpha_ids.size(), 1U);
    EXPECT_EQ(alpha_ids[0], 3U);
    const std::vector<uint32_t> alpha_count
        = collect_u32_values(store, "primary.alpha_count");
    ASSERT_EQ(alpha_count.size(), 1U);
    EXPECT_EQ(alpha_count[0], 1U);

    const std::vector<uint32_t> aux_item_ids
        = collect_u32_values(store, "aux.item_id");
    ASSERT_EQ(aux_item_ids.size(), 2U);
    EXPECT_EQ(aux_item_ids[0], 2U);
    EXPECT_EQ(aux_item_ids[1], 3U);
    const std::vector<uint32_t> aux_item_count
        = collect_u32_values(store, "aux.item_count");
    ASSERT_EQ(aux_item_count.size(), 1U);
    EXPECT_EQ(aux_item_count[0], 2U);
    const std::vector<uint32_t> aux_depth_count
        = collect_u32_values(store, "aux.depth_count");
    ASSERT_EQ(aux_depth_count.size(), 1U);
    EXPECT_EQ(aux_depth_count[0], 1U);
    const std::vector<uint32_t> aux_alpha_count
        = collect_u32_values(store, "aux.alpha_count");
    ASSERT_EQ(aux_alpha_count.size(), 1U);
    EXPECT_EQ(aux_alpha_count[0], 1U);
    EXPECT_TRUE(collect_u32_values(store, "aux.disparity_count").empty());
    EXPECT_TRUE(collect_u32_values(store, "aux.matte_count").empty());

    const std::vector<std::string> aux_semantic
        = collect_text_values(store, "aux.semantic");
    ASSERT_EQ(aux_semantic.size(), 2U);
    EXPECT_EQ(aux_semantic[0], "depth");
    EXPECT_EQ(aux_semantic[1], "alpha");

    const std::vector<std::string> aux_type = collect_text_values(store,
                                                                  "aux.type");
    ASSERT_EQ(aux_type.size(), 2U);
    EXPECT_EQ(aux_type[0], "urn:mpeg:hevc:2015:auxid:2");
    EXPECT_EQ(aux_type[1], "urn:mpeg:hevc:2015:auxid:1");

    const std::vector<std::string> aux_subtype
        = collect_text_values(store, "aux.subtype_hex");
    ASSERT_EQ(aux_subtype.size(), 2U);
    EXPECT_EQ(aux_subtype[0], "0xAABB");
    EXPECT_EQ(aux_subtype[1], "0x11");

    const std::vector<uint32_t> aux_subtype_len
        = collect_u32_values(store, "aux.subtype_len");
    ASSERT_EQ(aux_subtype_len.size(), 2U);
    EXPECT_EQ(aux_subtype_len[0], 2U);
    EXPECT_EQ(aux_subtype_len[1], 1U);

    const std::vector<std::string> aux_subtype_kind
        = collect_text_values(store, "aux.subtype_kind");
    ASSERT_EQ(aux_subtype_kind.size(), 2U);
    EXPECT_EQ(aux_subtype_kind[0], "u16be");
    EXPECT_EQ(aux_subtype_kind[1], "u8");

    const std::vector<uint32_t> aux_subtype_u32
        = collect_u32_values(store, "aux.subtype_u32");
    ASSERT_EQ(aux_subtype_u32.size(), 2U);
    EXPECT_EQ(aux_subtype_u32[0], 43707U);
    EXPECT_EQ(aux_subtype_u32[1], 17U);

    const std::vector<uint32_t> auxl_from
        = collect_u32_values(store, "iref.auxl.from_item_id");
    ASSERT_EQ(auxl_from.size(), 2U);
    EXPECT_EQ(auxl_from[0], 2U);
    EXPECT_EQ(auxl_from[1], 3U);

    const std::vector<uint32_t> auxl_to
        = collect_u32_values(store, "iref.auxl.to_item_id");
    ASSERT_EQ(auxl_to.size(), 2U);
    EXPECT_EQ(auxl_to[0], 1U);
    EXPECT_EQ(auxl_to[1], 1U);

    const std::vector<std::string> auxl_semantic
        = collect_text_values(store, "iref.auxl.semantic");
    ASSERT_EQ(auxl_semantic.size(), 2U);
    EXPECT_EQ(auxl_semantic[0], "depth");
    EXPECT_EQ(auxl_semantic[1], "alpha");

    const std::vector<std::string> auxl_type
        = collect_text_values(store, "iref.auxl.type");
    ASSERT_EQ(auxl_type.size(), 2U);
    EXPECT_EQ(auxl_type[0], "urn:mpeg:hevc:2015:auxid:2");
    EXPECT_EQ(auxl_type[1], "urn:mpeg:hevc:2015:auxid:1");

    const std::vector<std::string> auxl_subtype
        = collect_text_values(store, "iref.auxl.subtype_hex");
    ASSERT_EQ(auxl_subtype.size(), 2U);
    EXPECT_EQ(auxl_subtype[0], "0xAABB");
    EXPECT_EQ(auxl_subtype[1], "0x11");

    const std::vector<std::string> auxl_subtype_kind
        = collect_text_values(store, "iref.auxl.subtype_kind");
    ASSERT_EQ(auxl_subtype_kind.size(), 2U);
    EXPECT_EQ(auxl_subtype_kind[0], "u16be");
    EXPECT_EQ(auxl_subtype_kind[1], "u8");

    const std::vector<uint32_t> auxl_subtype_u32
        = collect_u32_values(store, "iref.auxl.subtype_u32");
    ASSERT_EQ(auxl_subtype_u32.size(), 2U);
    EXPECT_EQ(auxl_subtype_u32[0], 43707U);
    EXPECT_EQ(auxl_subtype_u32[1], 17U);
}

TEST(BmffDerivedFieldsDecode, EmitsPrimaryLinkedItemRoles)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 2U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 3U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 7U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('d', 'i', 'm', 'g'), 4U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('t', 'h', 'm', 'b'), 5U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('c', 'd', 's', 'c'), 6U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('c', 'd', 's', 'c'), 8U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('c', 'd', 's', 'c'), 5U, 1U);
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> auxc_depth_payload;
        static constexpr char kDepth[] = "urn:mpeg:hevc:2015:auxid:2";
        append_auxc_payload(&auxc_depth_payload, kDepth, {});
        std::vector<std::byte> auxc_depth_box;
        append_bmff_box(&auxc_depth_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_depth_payload);

        std::vector<std::byte> auxc_alpha_payload;
        static constexpr char kAlpha[] = "urn:mpeg:hevc:2015:auxid:1";
        append_auxc_payload(&auxc_alpha_payload, kAlpha, {});
        std::vector<std::byte> auxc_alpha_box;
        append_bmff_box(&auxc_alpha_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_alpha_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), auxc_depth_box.begin(),
                            auxc_depth_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_alpha_box.begin(),
                            auxc_alpha_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 4);

        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte { 0 });

        append_u16be(&ipma_payload, 2);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 1 });

        append_u16be(&ipma_payload, 3);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 2 });

        append_u16be(&ipma_payload, 7);
        ipma_payload.push_back(std::byte { 0 });

        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 7);
        append_infe_v2(&iinf_payload, 2, 0, fourcc('a', 'u', 'x', 'l'),
                       "depth_aux");
        append_infe_v2(&iinf_payload, 3, 0, fourcc('a', 'u', 'x', 'l'),
                       "alpha_aux");
        append_infe_v2(&iinf_payload, 4, 0, fourcc('d', 'e', 'r', 'v'),
                       "derived");
        append_infe_v2(&iinf_payload, 5, 0, fourcc('t', 'h', 'm', 'b'),
                       "thumb");
        append_infe_v2(&iinf_payload, 6, 0, fourcc('c', 'd', 's', 'c'),
                       "caption");
        append_infe_v2(&iinf_payload, 7, 0, fourcc('a', 'u', 'x', 'l'),
                       "other_aux");
        append_infe_v2_mime(&iinf_payload, 8, 0, "manifest",
                            "application/c2pa+jumbf", "");
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> role_count
        = collect_u32_values(store, "primary.linked_item_role_count");
    ASSERT_EQ(role_count.size(), 1U);
    EXPECT_EQ(role_count[0], 8U);

    const std::vector<uint32_t> sidecar_count
        = collect_u32_values(store, "primary.sidecar_count");
    const std::vector<uint32_t> scene_primary_item_count
        = collect_u32_values(store, "primary.scene_primary_item_count");
    const std::vector<uint32_t> scene_linked_item_count
        = collect_u32_values(store, "primary.scene_linked_item_count");
    const std::vector<uint32_t> scene_node_count
        = collect_u32_values(store, "primary.scene_node_count");
    const std::vector<uint32_t> scene_edge_count
        = collect_u32_values(store, "primary.scene_edge_count");
    const std::vector<uint32_t> scene_auxiliary_node_count
        = collect_u32_values(store, "primary.scene_auxiliary_node_count");
    const std::vector<uint32_t> scene_alpha_node_count
        = collect_u32_values(store, "primary.scene_alpha_node_count");
    const std::vector<uint32_t> scene_depth_node_count
        = collect_u32_values(store, "primary.scene_depth_node_count");
    const std::vector<uint32_t> scene_derived_image_node_count
        = collect_u32_values(store, "primary.scene_derived_image_node_count");
    const std::vector<uint32_t> scene_thumbnail_node_count
        = collect_u32_values(store, "primary.scene_thumbnail_node_count");
    const std::vector<uint32_t> scene_content_description_node_count
        = collect_u32_values(store,
                             "primary.scene_content_description_node_count");
    const std::vector<uint32_t> scene_auxiliary_edge_count
        = collect_u32_values(store, "primary.scene_auxiliary_edge_count");
    const std::vector<uint32_t> scene_alpha_edge_count
        = collect_u32_values(store, "primary.scene_alpha_edge_count");
    const std::vector<uint32_t> scene_depth_edge_count
        = collect_u32_values(store, "primary.scene_depth_edge_count");
    const std::vector<uint32_t> scene_derived_image_edge_count
        = collect_u32_values(store, "primary.scene_derived_image_edge_count");
    const std::vector<uint32_t> scene_thumbnail_edge_count
        = collect_u32_values(store, "primary.scene_thumbnail_edge_count");
    const std::vector<uint32_t> scene_content_description_edge_count
        = collect_u32_values(store,
                             "primary.scene_content_description_edge_count");
    const std::vector<uint32_t> scene_metadata_node_count
        = collect_u32_values(store, "primary.scene_metadata_node_count");
    const std::vector<uint32_t> scene_content_bound_metadata_node_count
        = collect_u32_values(store,
                             "primary.scene_content_bound_metadata_node_count");
    const std::vector<uint32_t> scene_image_node_count
        = collect_u32_values(store, "primary.scene_image_node_count");
    const std::vector<uint8_t> has_metadata_sidecar
        = collect_u8_values(store, "primary.has_metadata_sidecar");
    const std::vector<uint32_t> metadata_sidecar_count
        = collect_u32_values(store, "primary.metadata_sidecar_count");
    const std::vector<uint8_t> has_content_bound_metadata_sidecar
        = collect_u8_values(store,
                            "primary.has_content_bound_metadata_sidecar");
    const std::vector<uint32_t> content_bound_metadata_sidecar_count
        = collect_u32_values(store,
                             "primary.content_bound_metadata_sidecar_count");
    const std::vector<std::string> content_bound_metadata_policy
        = collect_text_values(store, "primary.content_bound_metadata_policy");
    const std::vector<uint8_t> has_image_sidecar
        = collect_u8_values(store, "primary.has_image_sidecar");
    const std::vector<uint32_t> image_sidecar_count
        = collect_u32_values(store, "primary.image_sidecar_count");
    const std::vector<uint32_t> auxiliary_sidecar_count
        = collect_u32_values(store, "primary.auxiliary_sidecar_count");
    const std::vector<uint32_t> derived_sidecar_count
        = collect_u32_values(store, "primary.derived_sidecar_count");
    const std::vector<uint32_t> thumbnail_sidecar_count
        = collect_u32_values(store, "primary.thumbnail_sidecar_count");
    const std::vector<uint32_t> content_description_sidecar_count
        = collect_u32_values(store,
                             "primary.content_description_sidecar_count");
    const std::vector<uint32_t> c2pa_sidecar_count
        = collect_u32_values(store, "primary.c2pa_sidecar_count");
    const std::vector<uint32_t> container_scene_item_count
        = collect_u32_values(store, "scene.item_count");
    const std::vector<uint32_t> container_scene_known_item_count
        = collect_u32_values(store, "scene.known_item_count");
    const std::vector<uint32_t> container_scene_image_node_count
        = collect_u32_values(store, "scene.image_node_count");
    const std::vector<uint32_t> container_scene_metadata_node_count
        = collect_u32_values(store, "scene.metadata_node_count");
    const std::vector<uint32_t> container_scene_content_bound_metadata_node_count
        = collect_u32_values(store, "scene.content_bound_metadata_node_count");
    const std::vector<uint32_t> container_scene_auxiliary_node_count
        = collect_u32_values(store, "scene.auxiliary_node_count");
    const std::vector<uint32_t> container_scene_derived_image_node_count
        = collect_u32_values(store, "scene.derived_image_node_count");
    const std::vector<uint32_t> container_scene_thumbnail_node_count
        = collect_u32_values(store, "scene.thumbnail_node_count");
    const std::vector<uint32_t> container_scene_content_description_node_count
        = collect_u32_values(store, "scene.content_description_node_count");
    const std::vector<uint32_t> container_scene_edge_count
        = collect_u32_values(store, "scene.edge_count");
    const std::vector<uint8_t> container_scene_has_content_bound_metadata
        = collect_u8_values(store, "scene.has_content_bound_metadata");
    const std::vector<std::string> container_scene_content_bound_metadata_policy
        = collect_text_values(store, "scene.content_bound_metadata_policy");
    const std::vector<uint8_t> container_scene_multi_image_candidate
        = collect_u8_values(store, "scene.multi_image_candidate");
    const std::vector<std::string> container_scene_multi_image_policy
        = collect_text_values(store, "scene.multi_image_policy");
    const std::vector<uint32_t> graph_node_count
        = collect_u32_values(store, "scene.graph_node_count");
    const std::vector<uint32_t> graph_component_count
        = collect_u32_values(store, "scene.graph_component_count");
    const std::vector<uint32_t> graph_image_component_count
        = collect_u32_values(store, "scene.graph_image_component_count");
    const std::vector<uint32_t> graph_multi_image_component_count
        = collect_u32_values(store, "scene.graph_multi_image_component_count");
    const std::vector<uint32_t> graph_content_bound_metadata_component_count
        = collect_u32_values(
            store, "scene.graph_content_bound_metadata_component_count");
    const std::vector<uint32_t> graph_observed_edge_count
        = collect_u32_values(store, "scene.graph_observed_edge_count");
    const std::vector<uint32_t> component_index
        = collect_u32_values(store, "scene.component.index");
    const std::vector<std::string> component_role
        = collect_text_values(store, "scene.component.role");
    const std::vector<uint32_t> component_node_count
        = collect_u32_values(store, "scene.component.node_count");
    const std::vector<uint32_t> component_known_node_count
        = collect_u32_values(store, "scene.component.known_node_count");
    const std::vector<uint32_t> component_unknown_node_count
        = collect_u32_values(store, "scene.component.unknown_node_count");
    const std::vector<uint32_t> component_image_node_count
        = collect_u32_values(store, "scene.component.image_node_count");
    const std::vector<uint32_t> component_metadata_node_count
        = collect_u32_values(store, "scene.component.metadata_node_count");
    const std::vector<uint32_t> component_content_bound_metadata_node_count
        = collect_u32_values(
            store, "scene.component.content_bound_metadata_node_count");
    const std::vector<uint32_t> component_edge_count
        = collect_u32_values(store, "scene.component.edge_count");
    const std::vector<uint32_t> component_auxiliary_node_count
        = collect_u32_values(store, "scene.component.auxiliary_node_count");
    const std::vector<uint32_t> component_derived_image_node_count
        = collect_u32_values(store, "scene.component.derived_image_node_count");
    const std::vector<uint32_t> component_thumbnail_node_count
        = collect_u32_values(store, "scene.component.thumbnail_node_count");
    const std::vector<uint32_t> component_content_description_node_count
        = collect_u32_values(store,
                             "scene.component.content_description_node_count");
    const std::vector<uint32_t> component_c2pa_node_count
        = collect_u32_values(store, "scene.component.c2pa_node_count");
    const std::vector<uint32_t> component_auxiliary_edge_count
        = collect_u32_values(store, "scene.component.auxiliary_edge_count");
    const std::vector<uint32_t> component_alpha_edge_count
        = collect_u32_values(store, "scene.component.alpha_edge_count");
    const std::vector<uint32_t> component_depth_edge_count
        = collect_u32_values(store, "scene.component.depth_edge_count");
    const std::vector<uint32_t> component_derived_image_edge_count
        = collect_u32_values(store, "scene.component.derived_image_edge_count");
    const std::vector<uint32_t> component_thumbnail_edge_count
        = collect_u32_values(store, "scene.component.thumbnail_edge_count");
    const std::vector<uint32_t> component_content_description_edge_count
        = collect_u32_values(store,
                             "scene.component.content_description_edge_count");
    const std::vector<uint32_t> component_item_ids
        = collect_u32_values(store, "scene.component.item_id");
    const std::vector<uint8_t> component_contains_primary
        = collect_u8_values(store, "scene.component.contains_primary");
    const std::vector<uint8_t> component_isolated
        = collect_u8_values(store, "scene.component.isolated");
    const std::vector<uint8_t> component_has_content_bound_metadata
        = collect_u8_values(store,
                            "scene.component.has_content_bound_metadata");
    const std::vector<uint8_t> component_multi_image_candidate
        = collect_u8_values(store, "scene.component.multi_image_candidate");
    const std::vector<std::string> component_metadata_policy
        = collect_text_values(store, "scene.component.metadata_policy");
    const std::vector<std::string> component_multi_image_policy
        = collect_text_values(store, "scene.component.multi_image_policy");
    const std::vector<uint32_t> primary_graph_component_node_count
        = collect_u32_values(store, "scene.primary_graph_component_node_count");
    const std::vector<uint32_t> primary_graph_component_image_node_count
        = collect_u32_values(store,
                             "scene.primary_graph_component_image_node_count");
    const std::vector<uint32_t> primary_graph_component_metadata_node_count
        = collect_u32_values(
            store, "scene.primary_graph_component_metadata_node_count");
    const std::vector<uint32_t>
        primary_graph_component_content_bound_metadata_node_count
        = collect_u32_values(
            store,
            "scene.primary_graph_component_content_bound_metadata_node_count");
    const std::vector<uint32_t> primary_graph_component_edge_count
        = collect_u32_values(store, "scene.primary_graph_component_edge_count");
    const std::vector<std::string> primary_graph_component_metadata_policy
        = collect_text_values(store,
                              "scene.primary_graph_component_metadata_policy");
    const std::vector<uint8_t> primary_graph_component_has_content_bound_metadata
        = collect_u8_values(
            store, "scene.primary_graph_component_has_content_bound_metadata");
    const std::vector<uint8_t> primary_graph_component_multi_image_candidate
        = collect_u8_values(
            store, "scene.primary_graph_component_multi_image_candidate");
    const std::vector<std::string> primary_graph_component_multi_image_policy
        = collect_text_values(
            store, "scene.primary_graph_component_multi_image_policy");
    ASSERT_EQ(sidecar_count.size(), 1U);
    ASSERT_EQ(scene_primary_item_count.size(), 1U);
    ASSERT_EQ(scene_linked_item_count.size(), 1U);
    ASSERT_EQ(scene_node_count.size(), 1U);
    ASSERT_EQ(scene_edge_count.size(), 1U);
    ASSERT_EQ(scene_auxiliary_node_count.size(), 1U);
    ASSERT_EQ(scene_alpha_node_count.size(), 1U);
    ASSERT_EQ(scene_depth_node_count.size(), 1U);
    ASSERT_EQ(scene_derived_image_node_count.size(), 1U);
    ASSERT_EQ(scene_thumbnail_node_count.size(), 1U);
    ASSERT_EQ(scene_content_description_node_count.size(), 1U);
    ASSERT_EQ(scene_auxiliary_edge_count.size(), 1U);
    ASSERT_EQ(scene_alpha_edge_count.size(), 1U);
    ASSERT_EQ(scene_depth_edge_count.size(), 1U);
    ASSERT_EQ(scene_derived_image_edge_count.size(), 1U);
    ASSERT_EQ(scene_thumbnail_edge_count.size(), 1U);
    ASSERT_EQ(scene_content_description_edge_count.size(), 1U);
    ASSERT_EQ(scene_metadata_node_count.size(), 1U);
    ASSERT_EQ(scene_content_bound_metadata_node_count.size(), 1U);
    ASSERT_EQ(scene_image_node_count.size(), 1U);
    ASSERT_EQ(has_metadata_sidecar.size(), 1U);
    ASSERT_EQ(metadata_sidecar_count.size(), 1U);
    ASSERT_EQ(has_content_bound_metadata_sidecar.size(), 1U);
    ASSERT_EQ(content_bound_metadata_sidecar_count.size(), 1U);
    ASSERT_EQ(content_bound_metadata_policy.size(), 1U);
    ASSERT_EQ(has_image_sidecar.size(), 1U);
    ASSERT_EQ(image_sidecar_count.size(), 1U);
    ASSERT_EQ(auxiliary_sidecar_count.size(), 1U);
    ASSERT_EQ(derived_sidecar_count.size(), 1U);
    ASSERT_EQ(thumbnail_sidecar_count.size(), 1U);
    ASSERT_EQ(content_description_sidecar_count.size(), 1U);
    ASSERT_EQ(c2pa_sidecar_count.size(), 1U);
    ASSERT_EQ(container_scene_item_count.size(), 1U);
    ASSERT_EQ(container_scene_known_item_count.size(), 1U);
    ASSERT_EQ(container_scene_image_node_count.size(), 1U);
    ASSERT_EQ(container_scene_metadata_node_count.size(), 1U);
    ASSERT_EQ(container_scene_content_bound_metadata_node_count.size(), 1U);
    ASSERT_EQ(container_scene_auxiliary_node_count.size(), 1U);
    ASSERT_EQ(container_scene_derived_image_node_count.size(), 1U);
    ASSERT_EQ(container_scene_thumbnail_node_count.size(), 1U);
    ASSERT_EQ(container_scene_content_description_node_count.size(), 1U);
    ASSERT_EQ(container_scene_edge_count.size(), 1U);
    ASSERT_EQ(container_scene_has_content_bound_metadata.size(), 1U);
    ASSERT_EQ(container_scene_content_bound_metadata_policy.size(), 1U);
    ASSERT_EQ(container_scene_multi_image_candidate.size(), 1U);
    ASSERT_EQ(container_scene_multi_image_policy.size(), 1U);
    ASSERT_EQ(graph_node_count.size(), 1U);
    ASSERT_EQ(graph_component_count.size(), 1U);
    ASSERT_EQ(graph_image_component_count.size(), 1U);
    ASSERT_EQ(graph_multi_image_component_count.size(), 1U);
    ASSERT_EQ(graph_content_bound_metadata_component_count.size(), 1U);
    ASSERT_EQ(graph_observed_edge_count.size(), 1U);
    ASSERT_EQ(component_index.size(), 1U);
    ASSERT_EQ(component_role.size(), 1U);
    ASSERT_EQ(component_node_count.size(), 1U);
    ASSERT_EQ(component_known_node_count.size(), 1U);
    ASSERT_EQ(component_unknown_node_count.size(), 1U);
    ASSERT_EQ(component_image_node_count.size(), 1U);
    ASSERT_EQ(component_metadata_node_count.size(), 1U);
    ASSERT_EQ(component_content_bound_metadata_node_count.size(), 1U);
    ASSERT_EQ(component_edge_count.size(), 1U);
    ASSERT_EQ(component_auxiliary_node_count.size(), 1U);
    ASSERT_EQ(component_derived_image_node_count.size(), 1U);
    ASSERT_EQ(component_thumbnail_node_count.size(), 1U);
    ASSERT_EQ(component_content_description_node_count.size(), 1U);
    ASSERT_EQ(component_c2pa_node_count.size(), 1U);
    ASSERT_EQ(component_auxiliary_edge_count.size(), 1U);
    ASSERT_EQ(component_alpha_edge_count.size(), 1U);
    ASSERT_EQ(component_depth_edge_count.size(), 1U);
    ASSERT_EQ(component_derived_image_edge_count.size(), 1U);
    ASSERT_EQ(component_thumbnail_edge_count.size(), 1U);
    ASSERT_EQ(component_content_description_edge_count.size(), 1U);
    ASSERT_EQ(component_item_ids.size(), 8U);
    ASSERT_EQ(component_contains_primary.size(), 1U);
    ASSERT_EQ(component_isolated.size(), 1U);
    ASSERT_EQ(component_has_content_bound_metadata.size(), 1U);
    ASSERT_EQ(component_multi_image_candidate.size(), 1U);
    ASSERT_EQ(component_metadata_policy.size(), 1U);
    ASSERT_EQ(component_multi_image_policy.size(), 1U);
    ASSERT_EQ(primary_graph_component_node_count.size(), 1U);
    ASSERT_EQ(primary_graph_component_image_node_count.size(), 1U);
    ASSERT_EQ(primary_graph_component_metadata_node_count.size(), 1U);
    ASSERT_EQ(primary_graph_component_content_bound_metadata_node_count.size(),
              1U);
    ASSERT_EQ(primary_graph_component_edge_count.size(), 1U);
    ASSERT_EQ(primary_graph_component_metadata_policy.size(), 1U);
    ASSERT_EQ(primary_graph_component_has_content_bound_metadata.size(), 1U);
    ASSERT_EQ(primary_graph_component_multi_image_candidate.size(), 1U);
    ASSERT_EQ(primary_graph_component_multi_image_policy.size(), 1U);
    EXPECT_EQ(sidecar_count[0], 7U);
    EXPECT_EQ(scene_primary_item_count[0], 1U);
    EXPECT_EQ(scene_linked_item_count[0], 7U);
    EXPECT_EQ(scene_node_count[0], 8U);
    EXPECT_EQ(scene_edge_count[0], 8U);
    EXPECT_EQ(scene_auxiliary_node_count[0], 1U);
    EXPECT_EQ(scene_alpha_node_count[0], 1U);
    EXPECT_EQ(scene_depth_node_count[0], 1U);
    EXPECT_EQ(scene_derived_image_node_count[0], 1U);
    EXPECT_EQ(scene_thumbnail_node_count[0], 1U);
    EXPECT_EQ(scene_content_description_node_count[0], 3U);
    EXPECT_EQ(scene_auxiliary_edge_count[0], 1U);
    EXPECT_EQ(scene_alpha_edge_count[0], 1U);
    EXPECT_EQ(scene_depth_edge_count[0], 1U);
    EXPECT_EQ(scene_derived_image_edge_count[0], 1U);
    EXPECT_EQ(scene_thumbnail_edge_count[0], 1U);
    EXPECT_EQ(scene_content_description_edge_count[0], 3U);
    EXPECT_EQ(scene_metadata_node_count[0], 2U);
    EXPECT_EQ(scene_content_bound_metadata_node_count[0], 1U);
    EXPECT_EQ(scene_image_node_count[0], 5U);
    EXPECT_EQ(has_metadata_sidecar[0], 1U);
    EXPECT_EQ(metadata_sidecar_count[0], 2U);
    EXPECT_EQ(has_content_bound_metadata_sidecar[0], 1U);
    EXPECT_EQ(content_bound_metadata_sidecar_count[0], 1U);
    EXPECT_EQ(content_bound_metadata_policy[0], "requires_target_rewrite");
    EXPECT_EQ(has_image_sidecar[0], 1U);
    EXPECT_EQ(image_sidecar_count[0], 5U);
    EXPECT_EQ(auxiliary_sidecar_count[0], 3U);
    EXPECT_EQ(derived_sidecar_count[0], 1U);
    EXPECT_EQ(thumbnail_sidecar_count[0], 1U);
    EXPECT_EQ(content_description_sidecar_count[0], 1U);
    EXPECT_EQ(c2pa_sidecar_count[0], 1U);
    EXPECT_EQ(container_scene_item_count[0], 7U);
    EXPECT_EQ(container_scene_known_item_count[0], 7U);
    EXPECT_EQ(container_scene_image_node_count[0], 5U);
    EXPECT_EQ(container_scene_metadata_node_count[0], 2U);
    EXPECT_EQ(container_scene_content_bound_metadata_node_count[0], 1U);
    EXPECT_EQ(container_scene_auxiliary_node_count[0], 3U);
    EXPECT_EQ(container_scene_derived_image_node_count[0], 1U);
    EXPECT_EQ(container_scene_thumbnail_node_count[0], 1U);
    EXPECT_EQ(container_scene_content_description_node_count[0], 1U);
    EXPECT_EQ(container_scene_edge_count[0], 8U);
    EXPECT_EQ(container_scene_has_content_bound_metadata[0], 1U);
    EXPECT_EQ(container_scene_content_bound_metadata_policy[0],
              "requires_target_rewrite");
    EXPECT_EQ(container_scene_multi_image_candidate[0], 1U);
    EXPECT_EQ(container_scene_multi_image_policy[0], "requires_target_rewrite");
    EXPECT_EQ(graph_node_count[0], 8U);
    EXPECT_EQ(graph_component_count[0], 1U);
    EXPECT_EQ(graph_image_component_count[0], 1U);
    EXPECT_EQ(graph_multi_image_component_count[0], 1U);
    EXPECT_EQ(graph_content_bound_metadata_component_count[0], 1U);
    EXPECT_EQ(graph_observed_edge_count[0], 8U);
    EXPECT_EQ(component_index[0], 0U);
    EXPECT_EQ(component_role[0], "primary_scene");
    EXPECT_EQ(component_node_count[0], 8U);
    EXPECT_EQ(component_known_node_count[0], 7U);
    EXPECT_EQ(component_unknown_node_count[0], 1U);
    EXPECT_EQ(component_image_node_count[0], 5U);
    EXPECT_EQ(component_metadata_node_count[0], 2U);
    EXPECT_EQ(component_content_bound_metadata_node_count[0], 1U);
    EXPECT_EQ(component_edge_count[0], 8U);
    EXPECT_EQ(component_auxiliary_node_count[0], 3U);
    EXPECT_EQ(component_derived_image_node_count[0], 1U);
    EXPECT_EQ(component_thumbnail_node_count[0], 1U);
    EXPECT_EQ(component_content_description_node_count[0], 1U);
    EXPECT_EQ(component_c2pa_node_count[0], 1U);
    EXPECT_EQ(component_auxiliary_edge_count[0], 3U);
    EXPECT_EQ(component_alpha_edge_count[0], 1U);
    EXPECT_EQ(component_depth_edge_count[0], 1U);
    EXPECT_EQ(component_derived_image_edge_count[0], 1U);
    EXPECT_EQ(component_thumbnail_edge_count[0], 1U);
    EXPECT_EQ(component_content_description_edge_count[0], 3U);
    for (uint32_t i = 0U; i < component_item_ids.size(); ++i) {
        EXPECT_EQ(component_item_ids[i], i + 1U);
    }
    EXPECT_EQ(component_contains_primary[0], 1U);
    EXPECT_EQ(component_isolated[0], 0U);
    EXPECT_EQ(component_has_content_bound_metadata[0], 1U);
    EXPECT_EQ(component_multi_image_candidate[0], 1U);
    EXPECT_EQ(component_metadata_policy[0], "requires_target_rewrite");
    EXPECT_EQ(component_multi_image_policy[0], "requires_target_rewrite");
    EXPECT_EQ(primary_graph_component_node_count[0], 8U);
    EXPECT_EQ(primary_graph_component_image_node_count[0], 5U);
    EXPECT_EQ(primary_graph_component_metadata_node_count[0], 2U);
    EXPECT_EQ(primary_graph_component_content_bound_metadata_node_count[0], 1U);
    EXPECT_EQ(primary_graph_component_edge_count[0], 8U);
    EXPECT_EQ(primary_graph_component_metadata_policy[0],
              "requires_target_rewrite");
    EXPECT_EQ(primary_graph_component_has_content_bound_metadata[0], 1U);
    EXPECT_EQ(primary_graph_component_multi_image_candidate[0], 1U);
    EXPECT_EQ(primary_graph_component_multi_image_policy[0],
              "requires_target_rewrite");

    const std::vector<uint32_t> derived_image_ids
        = collect_u32_values(store, "primary.derived_image_item_id");
    ASSERT_EQ(derived_image_ids.size(), 1U);
    EXPECT_EQ(derived_image_ids[0], 4U);
    const std::vector<uint32_t> thumbnail_image_ids
        = collect_u32_values(store, "primary.thumbnail_image_item_id");
    ASSERT_EQ(thumbnail_image_ids.size(), 1U);
    EXPECT_EQ(thumbnail_image_ids[0], 5U);
    const std::vector<uint32_t> descriptive_item_ids
        = collect_u32_values(store, "primary.descriptive_item_id");
    ASSERT_EQ(descriptive_item_ids.size(), 3U);
    EXPECT_EQ(descriptive_item_ids[0], 6U);
    EXPECT_EQ(descriptive_item_ids[1], 8U);
    EXPECT_EQ(descriptive_item_ids[2], 5U);

    const std::vector<std::string> dimg_from_roles
        = collect_text_values(store, "iref.dimg.from_role");
    const std::vector<std::string> dimg_to_roles
        = collect_text_values(store, "iref.dimg.to_role");
    ASSERT_EQ(dimg_from_roles.size(), 1U);
    ASSERT_EQ(dimg_to_roles.size(), 1U);
    EXPECT_EQ(dimg_from_roles[0], "derived_image");
    EXPECT_EQ(dimg_to_roles[0], "source_image");
    const std::vector<uint32_t> dimg_ids
        = collect_u32_values(store, "iref.dimg.derived_item_id");
    const std::vector<uint32_t> dimg_source_ids
        = collect_u32_values(store, "iref.dimg.source_item_id");
    ASSERT_EQ(dimg_ids.size(), 1U);
    ASSERT_EQ(dimg_source_ids.size(), 1U);
    EXPECT_EQ(dimg_ids[0], 4U);
    EXPECT_EQ(dimg_source_ids[0], 1U);

    const std::vector<std::string> thmb_from_roles
        = collect_text_values(store, "iref.thmb.from_role");
    const std::vector<std::string> thmb_to_roles
        = collect_text_values(store, "iref.thmb.to_role");
    ASSERT_EQ(thmb_from_roles.size(), 1U);
    ASSERT_EQ(thmb_to_roles.size(), 1U);
    EXPECT_EQ(thmb_from_roles[0], "thumbnail_image");
    EXPECT_EQ(thmb_to_roles[0], "master_image");
    const std::vector<uint32_t> thmb_ids
        = collect_u32_values(store, "iref.thmb.thumbnail_item_id");
    ASSERT_EQ(thmb_ids.size(), 1U);
    EXPECT_EQ(thmb_ids[0], 5U);

    const std::vector<std::string> cdsc_from_roles
        = collect_text_values(store, "iref.cdsc.from_role");
    const std::vector<std::string> cdsc_to_roles
        = collect_text_values(store, "iref.cdsc.to_role");
    ASSERT_EQ(cdsc_from_roles.size(), 3U);
    ASSERT_EQ(cdsc_to_roles.size(), 3U);
    for (uint32_t i = 0U; i < 3U; ++i) {
        EXPECT_EQ(cdsc_from_roles[i], "descriptive_item");
        EXPECT_EQ(cdsc_to_roles[i], "described_item");
    }
    const std::vector<uint32_t> cdsc_ids
        = collect_u32_values(store, "iref.cdsc.descriptive_item_id");
    const std::vector<uint32_t> described_ids
        = collect_u32_values(store, "iref.cdsc.described_item_id");
    ASSERT_EQ(cdsc_ids.size(), 3U);
    ASSERT_EQ(described_ids.size(), 3U);
    EXPECT_EQ(cdsc_ids[0], 6U);
    EXPECT_EQ(cdsc_ids[1], 8U);
    EXPECT_EQ(cdsc_ids[2], 5U);
    EXPECT_EQ(described_ids[0], 1U);
    EXPECT_EQ(described_ids[1], 1U);
    EXPECT_EQ(described_ids[2], 1U);

    const std::vector<uint32_t> role_item_ids
        = collect_u32_values(store, "primary.linked_item_id");
    ASSERT_EQ(role_item_ids.size(), 8U);
    EXPECT_EQ(role_item_ids[0], 2U);
    EXPECT_EQ(role_item_ids[1], 3U);
    EXPECT_EQ(role_item_ids[2], 7U);
    EXPECT_EQ(role_item_ids[3], 4U);
    EXPECT_EQ(role_item_ids[4], 5U);
    EXPECT_EQ(role_item_ids[5], 6U);
    EXPECT_EQ(role_item_ids[6], 8U);
    EXPECT_EQ(role_item_ids[7], 5U);

    const std::vector<uint32_t> role_item_types
        = collect_u32_values(store, "primary.linked_item_type");
    ASSERT_EQ(role_item_types.size(), 8U);
    EXPECT_EQ(role_item_types[0], fourcc('a', 'u', 'x', 'l'));
    EXPECT_EQ(role_item_types[1], fourcc('a', 'u', 'x', 'l'));
    EXPECT_EQ(role_item_types[2], fourcc('a', 'u', 'x', 'l'));
    EXPECT_EQ(role_item_types[3], fourcc('d', 'e', 'r', 'v'));
    EXPECT_EQ(role_item_types[4], fourcc('t', 'h', 'm', 'b'));
    EXPECT_EQ(role_item_types[5], fourcc('c', 'd', 's', 'c'));
    EXPECT_EQ(role_item_types[6], fourcc('m', 'i', 'm', 'e'));
    EXPECT_EQ(role_item_types[7], fourcc('t', 'h', 'm', 'b'));

    const std::vector<std::string> role_item_names
        = collect_text_values(store, "primary.linked_item_name");
    ASSERT_EQ(role_item_names.size(), 8U);
    EXPECT_EQ(role_item_names[0], "depth_aux");
    EXPECT_EQ(role_item_names[1], "alpha_aux");
    EXPECT_EQ(role_item_names[2], "other_aux");
    EXPECT_EQ(role_item_names[3], "derived");
    EXPECT_EQ(role_item_names[4], "thumb");
    EXPECT_EQ(role_item_names[5], "caption");
    EXPECT_EQ(role_item_names[6], "manifest");
    EXPECT_EQ(role_item_names[7], "thumb");

    const std::vector<std::string> roles
        = collect_text_values(store, "primary.linked_item_role");
    ASSERT_EQ(roles.size(), 8U);
    EXPECT_EQ(roles[0], "depth");
    EXPECT_EQ(roles[1], "alpha");
    EXPECT_EQ(roles[2], "auxiliary");
    EXPECT_EQ(roles[3], "derived");
    EXPECT_EQ(roles[4], "thumbnail");
    EXPECT_EQ(roles[5], "content_description");
    EXPECT_EQ(roles[6], "content_description");
    EXPECT_EQ(roles[7], "content_description");

    const std::vector<uint32_t> linked_known
        = collect_u32_values(store, "primary.linked_item_semantic_known_count");
    const std::vector<uint32_t> linked_metadata
        = collect_u32_values(store,
                             "primary.linked_item_semantic_metadata_count");
    const std::vector<uint32_t> linked_auxiliary
        = collect_u32_values(store,
                             "primary.linked_item_semantic_auxiliary_count");
    const std::vector<uint32_t> linked_derived
        = collect_u32_values(store,
                             "primary.linked_item_semantic_derived_count");
    const std::vector<uint32_t> linked_thumbnail
        = collect_u32_values(store,
                             "primary.linked_item_semantic_thumbnail_count");
    const std::vector<uint32_t> linked_content_description = collect_u32_values(
        store, "primary.linked_item_semantic_content_description_count");
    const std::vector<uint32_t> linked_c2pa
        = collect_u32_values(store, "primary.linked_item_semantic_c2pa_count");
    ASSERT_EQ(linked_known.size(), 1U);
    ASSERT_EQ(linked_metadata.size(), 1U);
    ASSERT_EQ(linked_auxiliary.size(), 1U);
    ASSERT_EQ(linked_derived.size(), 1U);
    ASSERT_EQ(linked_thumbnail.size(), 1U);
    ASSERT_EQ(linked_content_description.size(), 1U);
    ASSERT_EQ(linked_c2pa.size(), 1U);
    EXPECT_EQ(linked_known[0], 7U);
    EXPECT_EQ(linked_metadata[0], 2U);
    EXPECT_EQ(linked_auxiliary[0], 3U);
    EXPECT_EQ(linked_derived[0], 1U);
    EXPECT_EQ(linked_thumbnail[0], 1U);
    EXPECT_EQ(linked_content_description[0], 1U);
    EXPECT_EQ(linked_c2pa[0], 1U);
}

TEST(BmffDerivedFieldsDecode, EmitsComponentMembershipAndIndependentRoles)
{
    std::vector<std::byte> file;

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('a', 'v', 'i', 'f'));
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

    std::vector<std::byte> pitm_payload;
    append_fullbox_header(&pitm_payload, 0U);
    append_u16be(&pitm_payload, 1U);
    std::vector<std::byte> pitm_box;
    append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

    std::vector<std::byte> other_payload;
    append_u16be(&other_payload, 1U);
    append_u16be(&other_payload, 1U);
    append_u16be(&other_payload, 2U);
    std::vector<std::byte> other_box;
    append_bmff_box(&other_box, fourcc('a', 'b', 'c', 'd'), other_payload);
    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0U);
    append_iref_v0_edge(&iref_payload, fourcc('c', 'd', 's', 'c'), 2U, 1U);
    iref_payload.insert(iref_payload.end(), other_box.begin(), other_box.end());
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2U);
    append_u32be(&iinf_payload, 4U);
    append_infe_v2(&iinf_payload, 1U, 0U, fourcc('a', 'v', '0', '1'),
                   "primary");
    append_infe_v2_mime(&iinf_payload, 2U, 0U, "xmp", "application/rdf+xml",
                        "");
    append_infe_v2(&iinf_payload, 3U, 0U, fourcc('a', 'v', '0', '1'),
                   "independent");
    append_infe_v2_mime(&iinf_payload, 4U, 0U, "manifest", "application/json",
                        "");
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0U);
    meta_payload.insert(meta_payload.end(), pitm_box.begin(), pitm_box.end());
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<std::string> roles
        = collect_text_values(store, "scene.component.role");
    const std::vector<uint32_t> item_ids
        = collect_u32_values(store, "scene.component.item_id");
    const std::vector<uint32_t> node_counts
        = collect_u32_values(store, "scene.component.node_count");
    const std::vector<uint32_t> image_counts
        = collect_u32_values(store, "scene.component.image_node_count");
    const std::vector<uint32_t> metadata_counts
        = collect_u32_values(store, "scene.component.metadata_node_count");
    const std::vector<uint32_t> xmp_counts
        = collect_u32_values(store, "scene.component.xmp_node_count");
    const std::vector<uint32_t> json_counts
        = collect_u32_values(store, "scene.component.json_node_count");
    const std::vector<uint32_t> cdsc_edge_counts
        = collect_u32_values(store,
                             "scene.component.content_description_edge_count");
    const std::vector<uint32_t> other_edge_counts
        = collect_u32_values(store, "scene.component.other_edge_count");
    const std::vector<uint8_t> isolated
        = collect_u8_values(store, "scene.component.isolated");
    const std::vector<uint32_t> image_component_count
        = collect_u32_values(store, "scene.graph_image_component_count");
    const std::vector<uint8_t> multi_image
        = collect_u8_values(store, "scene.multi_image_candidate");

    ASSERT_EQ(roles.size(), 3U);
    EXPECT_EQ(roles[0], "primary_scene");
    EXPECT_EQ(roles[1], "image_scene");
    EXPECT_EQ(roles[2], "metadata_only");
    ASSERT_EQ(item_ids.size(), 4U);
    EXPECT_EQ(item_ids[0], 1U);
    EXPECT_EQ(item_ids[1], 2U);
    EXPECT_EQ(item_ids[2], 3U);
    EXPECT_EQ(item_ids[3], 4U);
    ASSERT_EQ(node_counts.size(), 3U);
    EXPECT_EQ(node_counts[0], 2U);
    EXPECT_EQ(node_counts[1], 1U);
    EXPECT_EQ(node_counts[2], 1U);
    ASSERT_EQ(image_counts.size(), 3U);
    EXPECT_EQ(image_counts[0], 1U);
    EXPECT_EQ(image_counts[1], 1U);
    EXPECT_EQ(image_counts[2], 0U);
    ASSERT_EQ(metadata_counts.size(), 3U);
    EXPECT_EQ(metadata_counts[0], 1U);
    EXPECT_EQ(metadata_counts[1], 0U);
    EXPECT_EQ(metadata_counts[2], 1U);
    ASSERT_EQ(xmp_counts.size(), 1U);
    EXPECT_EQ(xmp_counts[0], 1U);
    ASSERT_EQ(json_counts.size(), 1U);
    EXPECT_EQ(json_counts[0], 1U);
    ASSERT_EQ(cdsc_edge_counts.size(), 1U);
    EXPECT_EQ(cdsc_edge_counts[0], 1U);
    ASSERT_EQ(other_edge_counts.size(), 1U);
    EXPECT_EQ(other_edge_counts[0], 1U);
    ASSERT_EQ(isolated.size(), 3U);
    EXPECT_EQ(isolated[0], 0U);
    EXPECT_EQ(isolated[1], 1U);
    EXPECT_EQ(isolated[2], 1U);
    ASSERT_EQ(image_component_count.size(), 1U);
    EXPECT_EQ(image_component_count[0], 2U);
    ASSERT_EQ(multi_image.size(), 1U);
    EXPECT_EQ(multi_image[0], 1U);
}

TEST(BmffDerivedFieldsDecode, EmitsDisparityAndMatteAuxCountsFromAuxC)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 2U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 3U, 1U);
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> auxc_disparity_payload;
        static constexpr char kDisparity[]
            = "urn:mpeg:mpegB:cicp:systems:auxiliary:disparity";
        append_auxc_payload(&auxc_disparity_payload, kDisparity, {});
        auxc_disparity_payload.push_back(std::byte { 0x44 });
        std::vector<std::byte> auxc_disparity_box;
        append_bmff_box(&auxc_disparity_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_disparity_payload);

        std::vector<std::byte> auxc_matte_payload;
        static constexpr char kMatte[]
            = "urn:mpeg:mpegB:cicp:systems:auxiliary:matte";
        append_auxc_payload(&auxc_matte_payload, kMatte, {});
        auxc_matte_payload.push_back(std::byte { 0x55 });
        std::vector<std::byte> auxc_matte_box;
        append_bmff_box(&auxc_matte_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_matte_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), auxc_disparity_box.begin(),
                            auxc_disparity_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_matte_box.begin(),
                            auxc_matte_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 3);

        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte { 0 });

        append_u16be(&ipma_payload, 2);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 1 });

        append_u16be(&ipma_payload, 3);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 2 });

        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> aux_item_count
        = collect_u32_values(store, "aux.item_count");
    ASSERT_EQ(aux_item_count.size(), 1U);
    EXPECT_EQ(aux_item_count[0], 2U);

    const std::vector<uint32_t> disparity_count
        = collect_u32_values(store, "aux.disparity_count");
    ASSERT_EQ(disparity_count.size(), 1U);
    EXPECT_EQ(disparity_count[0], 1U);
    const std::vector<uint32_t> primary_disparity_count
        = collect_u32_values(store, "primary.disparity_count");
    ASSERT_EQ(primary_disparity_count.size(), 1U);
    EXPECT_EQ(primary_disparity_count[0], 1U);
    const std::vector<uint32_t> primary_disparity_ids
        = collect_u32_values(store, "primary.disparity_item_id");
    ASSERT_EQ(primary_disparity_ids.size(), 1U);
    EXPECT_EQ(primary_disparity_ids[0], 2U);

    const std::vector<uint32_t> matte_count
        = collect_u32_values(store, "aux.matte_count");
    ASSERT_EQ(matte_count.size(), 1U);
    EXPECT_EQ(matte_count[0], 1U);
    const std::vector<uint32_t> primary_matte_count
        = collect_u32_values(store, "primary.matte_count");
    ASSERT_EQ(primary_matte_count.size(), 1U);
    EXPECT_EQ(primary_matte_count[0], 1U);
    const std::vector<uint32_t> primary_matte_ids
        = collect_u32_values(store, "primary.matte_item_id");
    ASSERT_EQ(primary_matte_ids.size(), 1U);
    EXPECT_EQ(primary_matte_ids[0], 3U);

    EXPECT_TRUE(collect_u32_values(store, "aux.alpha_count").empty());
    EXPECT_TRUE(collect_u32_values(store, "aux.depth_count").empty());

    const std::vector<std::string> aux_semantic
        = collect_text_values(store, "aux.semantic");
    ASSERT_EQ(aux_semantic.size(), 2U);
    EXPECT_EQ(aux_semantic[0], "disparity");
    EXPECT_EQ(aux_semantic[1], "matte");

    const std::vector<std::string> primary_auxl_semantic
        = collect_text_values(store, "primary.auxl_semantic");
    ASSERT_EQ(primary_auxl_semantic.size(), 2U);
    EXPECT_EQ(primary_auxl_semantic[0], "disparity");
    EXPECT_EQ(primary_auxl_semantic[1], "matte");
}

TEST(BmffDerivedFieldsDecode, EmitsNonPrimaryIrefTypedEdges)
{
    // Minimal HEIF-like BMFF:
    // - primary item id = 1
    // - iref edges on non-primary items:
    //   dimg: 2 -> [5,6]
    //   thmb: 3 -> [7]
    //   cdsc: 4 -> [8]

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> dimg_payload;
        append_u16be(&dimg_payload, 2);  // from item id
        append_u16be(&dimg_payload, 2);  // ref count
        append_u16be(&dimg_payload, 5);  // to item id
        append_u16be(&dimg_payload, 6);  // to item id
        std::vector<std::byte> dimg_box;
        append_bmff_box(&dimg_box, fourcc('d', 'i', 'm', 'g'), dimg_payload);

        std::vector<std::byte> thmb_payload;
        append_u16be(&thmb_payload, 3);  // from item id
        append_u16be(&thmb_payload, 1);  // ref count
        append_u16be(&thmb_payload, 7);  // to item id
        std::vector<std::byte> thmb_box;
        append_bmff_box(&thmb_box, fourcc('t', 'h', 'm', 'b'), thmb_payload);

        std::vector<std::byte> cdsc_payload;
        append_u16be(&cdsc_payload, 4);  // from item id
        append_u16be(&cdsc_payload, 1);  // ref count
        append_u16be(&cdsc_payload, 8);  // to item id
        std::vector<std::byte> cdsc_box;
        append_bmff_box(&cdsc_box, fourcc('c', 'd', 's', 'c'), cdsc_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), dimg_box.begin(),
                            dimg_box.end());
        iref_payload.insert(iref_payload.end(), thmb_box.begin(),
                            thmb_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_box.begin(),
                            cdsc_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> edge_count
        = collect_u32_values(store, "iref.edge_count");
    ASSERT_EQ(edge_count.size(), 1U);
    EXPECT_EQ(edge_count[0], 4U);

    const std::vector<uint32_t> dimg_count
        = collect_u32_values(store, "iref.dimg.edge_count");
    ASSERT_EQ(dimg_count.size(), 1U);
    EXPECT_EQ(dimg_count[0], 2U);
    const std::vector<uint32_t> dimg_graph_count
        = collect_u32_values(store, "iref.graph.dimg.edge_count");
    ASSERT_EQ(dimg_graph_count.size(), 1U);
    EXPECT_EQ(dimg_graph_count[0], 2U);
    const std::vector<uint32_t> dimg_from_unique
        = collect_u32_values(store, "iref.dimg.from_item_unique_count");
    ASSERT_EQ(dimg_from_unique.size(), 1U);
    EXPECT_EQ(dimg_from_unique[0], 1U);
    const std::vector<uint32_t> dimg_graph_from_unique
        = collect_u32_values(store, "iref.graph.dimg.from_item_unique_count");
    ASSERT_EQ(dimg_graph_from_unique.size(), 1U);
    EXPECT_EQ(dimg_graph_from_unique[0], 1U);
    const std::vector<uint32_t> dimg_to_unique
        = collect_u32_values(store, "iref.dimg.to_item_unique_count");
    ASSERT_EQ(dimg_to_unique.size(), 1U);
    EXPECT_EQ(dimg_to_unique[0], 2U);
    const std::vector<uint32_t> dimg_graph_to_unique
        = collect_u32_values(store, "iref.graph.dimg.to_item_unique_count");
    ASSERT_EQ(dimg_graph_to_unique.size(), 1U);
    EXPECT_EQ(dimg_graph_to_unique[0], 2U);

    const std::vector<uint32_t> thmb_count
        = collect_u32_values(store, "iref.thmb.edge_count");
    ASSERT_EQ(thmb_count.size(), 1U);
    EXPECT_EQ(thmb_count[0], 1U);
    const std::vector<uint32_t> thmb_graph_count
        = collect_u32_values(store, "iref.graph.thmb.edge_count");
    ASSERT_EQ(thmb_graph_count.size(), 1U);
    EXPECT_EQ(thmb_graph_count[0], 1U);
    const std::vector<uint32_t> thmb_from_unique
        = collect_u32_values(store, "iref.thmb.from_item_unique_count");
    ASSERT_EQ(thmb_from_unique.size(), 1U);
    EXPECT_EQ(thmb_from_unique[0], 1U);
    const std::vector<uint32_t> thmb_graph_from_unique
        = collect_u32_values(store, "iref.graph.thmb.from_item_unique_count");
    ASSERT_EQ(thmb_graph_from_unique.size(), 1U);
    EXPECT_EQ(thmb_graph_from_unique[0], 1U);
    const std::vector<uint32_t> thmb_to_unique
        = collect_u32_values(store, "iref.thmb.to_item_unique_count");
    ASSERT_EQ(thmb_to_unique.size(), 1U);
    EXPECT_EQ(thmb_to_unique[0], 1U);
    const std::vector<uint32_t> thmb_graph_to_unique
        = collect_u32_values(store, "iref.graph.thmb.to_item_unique_count");
    ASSERT_EQ(thmb_graph_to_unique.size(), 1U);
    EXPECT_EQ(thmb_graph_to_unique[0], 1U);

    const std::vector<uint32_t> cdsc_count
        = collect_u32_values(store, "iref.cdsc.edge_count");
    ASSERT_EQ(cdsc_count.size(), 1U);
    EXPECT_EQ(cdsc_count[0], 1U);
    const std::vector<uint32_t> cdsc_graph_count
        = collect_u32_values(store, "iref.graph.cdsc.edge_count");
    ASSERT_EQ(cdsc_graph_count.size(), 1U);
    EXPECT_EQ(cdsc_graph_count[0], 1U);
    const std::vector<uint32_t> cdsc_from_unique
        = collect_u32_values(store, "iref.cdsc.from_item_unique_count");
    ASSERT_EQ(cdsc_from_unique.size(), 1U);
    EXPECT_EQ(cdsc_from_unique[0], 1U);
    const std::vector<uint32_t> cdsc_graph_from_unique
        = collect_u32_values(store, "iref.graph.cdsc.from_item_unique_count");
    ASSERT_EQ(cdsc_graph_from_unique.size(), 1U);
    EXPECT_EQ(cdsc_graph_from_unique[0], 1U);
    const std::vector<uint32_t> cdsc_to_unique
        = collect_u32_values(store, "iref.cdsc.to_item_unique_count");
    ASSERT_EQ(cdsc_to_unique.size(), 1U);
    EXPECT_EQ(cdsc_to_unique[0], 1U);
    const std::vector<uint32_t> cdsc_graph_to_unique
        = collect_u32_values(store, "iref.graph.cdsc.to_item_unique_count");
    ASSERT_EQ(cdsc_graph_to_unique.size(), 1U);
    EXPECT_EQ(cdsc_graph_to_unique[0], 1U);

    EXPECT_TRUE(
        collect_u32_values(store, "iref.graph.auxl.edge_count").empty());

    const std::vector<uint32_t> item_count
        = collect_u32_values(store, "iref.item_count");
    ASSERT_EQ(item_count.size(), 1U);
    EXPECT_EQ(item_count[0], 7U);
    const std::vector<uint32_t> from_unique_count
        = collect_u32_values(store, "iref.from_item_unique_count");
    ASSERT_EQ(from_unique_count.size(), 1U);
    EXPECT_EQ(from_unique_count[0], 3U);
    const std::vector<uint32_t> to_unique_count
        = collect_u32_values(store, "iref.to_item_unique_count");
    ASSERT_EQ(to_unique_count.size(), 1U);
    EXPECT_EQ(to_unique_count[0], 4U);

    const std::vector<uint32_t> dimg_from
        = collect_u32_values(store, "iref.dimg.from_item_id");
    ASSERT_EQ(dimg_from.size(), 2U);
    EXPECT_EQ(dimg_from[0], 2U);
    EXPECT_EQ(dimg_from[1], 2U);

    const std::vector<uint32_t> dimg_to
        = collect_u32_values(store, "iref.dimg.to_item_id");
    ASSERT_EQ(dimg_to.size(), 2U);
    EXPECT_EQ(dimg_to[0], 5U);
    EXPECT_EQ(dimg_to[1], 6U);

    const std::vector<uint32_t> thmb_from
        = collect_u32_values(store, "iref.thmb.from_item_id");
    ASSERT_EQ(thmb_from.size(), 1U);
    EXPECT_EQ(thmb_from[0], 3U);

    const std::vector<uint32_t> thmb_to
        = collect_u32_values(store, "iref.thmb.to_item_id");
    ASSERT_EQ(thmb_to.size(), 1U);
    EXPECT_EQ(thmb_to[0], 7U);

    const std::vector<uint32_t> cdsc_from
        = collect_u32_values(store, "iref.cdsc.from_item_id");
    ASSERT_EQ(cdsc_from.size(), 1U);
    EXPECT_EQ(cdsc_from[0], 4U);

    const std::vector<uint32_t> cdsc_to
        = collect_u32_values(store, "iref.cdsc.to_item_id");
    ASSERT_EQ(cdsc_to.size(), 1U);
    EXPECT_EQ(cdsc_to[0], 8U);

    EXPECT_TRUE(collect_u32_values(store, "primary.dimg_item_id").empty());
    EXPECT_TRUE(collect_u32_values(store, "primary.thmb_item_id").empty());
    EXPECT_TRUE(collect_u32_values(store, "primary.cdsc_item_id").empty());
}

TEST(BmffDerivedFieldsDecode, EmitsDynamicIrefTypedEdgesForUnknownAsciiFourcc)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> pred_payload;
        append_u16be(&pred_payload, 9);   // from item id
        append_u16be(&pred_payload, 2);   // ref count
        append_u16be(&pred_payload, 10);  // to item id
        append_u16be(&pred_payload, 11);  // to item id
        std::vector<std::byte> pred_box;
        append_bmff_box(&pred_box, fourcc('p', 'r', 'e', 'd'), pred_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), pred_box.begin(),
                            pred_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<std::string> ref_type_names
        = collect_text_values(store, "iref.ref_type_name");
    ASSERT_EQ(ref_type_names.size(), 2U);
    EXPECT_EQ(ref_type_names[0], "pred");
    EXPECT_EQ(ref_type_names[1], "pred");

    const std::vector<uint32_t> pred_count
        = collect_u32_values(store, "iref.pred.edge_count");
    ASSERT_EQ(pred_count.size(), 1U);
    EXPECT_EQ(pred_count[0], 2U);
    const std::vector<uint32_t> pred_graph_count
        = collect_u32_values(store, "iref.graph.pred.edge_count");
    ASSERT_EQ(pred_graph_count.size(), 1U);
    EXPECT_EQ(pred_graph_count[0], 2U);

    const std::vector<uint32_t> pred_from
        = collect_u32_values(store, "iref.pred.from_item_id");
    ASSERT_EQ(pred_from.size(), 2U);
    EXPECT_EQ(pred_from[0], 9U);
    EXPECT_EQ(pred_from[1], 9U);
    const std::vector<uint32_t> pred_to
        = collect_u32_values(store, "iref.pred.to_item_id");
    ASSERT_EQ(pred_to.size(), 2U);
    EXPECT_EQ(pred_to[0], 10U);
    EXPECT_EQ(pred_to[1], 11U);

    const std::vector<uint32_t> pred_item_count
        = collect_u32_values(store, "iref.pred.item_count");
    ASSERT_EQ(pred_item_count.size(), 1U);
    EXPECT_EQ(pred_item_count[0], 3U);
    const std::vector<uint32_t> pred_item_ids
        = collect_u32_values(store, "iref.pred.item_id");
    ASSERT_EQ(pred_item_ids.size(), 3U);
    EXPECT_EQ(pred_item_ids[0], 9U);
    EXPECT_EQ(pred_item_ids[1], 10U);
    EXPECT_EQ(pred_item_ids[2], 11U);
}

TEST(BmffDerivedFieldsDecode, EmitsAuxSubtypeU64AndAsciiZFromAuxC)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 2U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 3U, 1U);
        append_iref_v0_edge(&iref_payload, fourcc('a', 'u', 'x', 'l'), 4U, 1U);
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> auxc_depth_payload;
        append_fullbox_header(&auxc_depth_payload, 0);
        static constexpr char kDepth[] = "urn:mpeg:hevc:2015:auxid:2";
        for (size_t i = 0; i < sizeof(kDepth) - 1; ++i) {
            auxc_depth_payload.push_back(
                std::byte { static_cast<uint8_t>(kDepth[i]) });
        }
        auxc_depth_payload.push_back(std::byte { 0x00 });
        static constexpr char kAsciiZ[] = "profile";
        for (size_t i = 0; i < sizeof(kAsciiZ) - 1; ++i) {
            auxc_depth_payload.push_back(
                std::byte { static_cast<uint8_t>(kAsciiZ[i]) });
        }
        auxc_depth_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> auxc_depth_box;
        append_bmff_box(&auxc_depth_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_depth_payload);

        std::vector<std::byte> auxc_alpha_payload;
        append_fullbox_header(&auxc_alpha_payload, 0);
        static constexpr char kAlpha[] = "urn:mpeg:hevc:2015:auxid:1";
        for (size_t i = 0; i < sizeof(kAlpha) - 1; ++i) {
            auxc_alpha_payload.push_back(
                std::byte { static_cast<uint8_t>(kAlpha[i]) });
        }
        auxc_alpha_payload.push_back(std::byte { 0x00 });
        auxc_alpha_payload.push_back(std::byte { 0x11 });
        auxc_alpha_payload.push_back(std::byte { 0x22 });
        auxc_alpha_payload.push_back(std::byte { 0x33 });
        auxc_alpha_payload.push_back(std::byte { 0x44 });
        auxc_alpha_payload.push_back(std::byte { 0x55 });
        auxc_alpha_payload.push_back(std::byte { 0x66 });
        auxc_alpha_payload.push_back(std::byte { 0x77 });
        auxc_alpha_payload.push_back(std::byte { 0x88 });
        std::vector<std::byte> auxc_alpha_box;
        append_bmff_box(&auxc_alpha_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_alpha_payload);

        std::vector<std::byte> auxc_uuid_payload;
        append_fullbox_header(&auxc_uuid_payload, 0);
        for (size_t i = 0; i < sizeof(kAlpha) - 1; ++i) {
            auxc_uuid_payload.push_back(
                std::byte { static_cast<uint8_t>(kAlpha[i]) });
        }
        auxc_uuid_payload.push_back(std::byte { 0x00 });
        for (uint8_t i = 0; i < 16U; ++i) {
            auxc_uuid_payload.push_back(std::byte { i });
        }
        std::vector<std::byte> auxc_uuid_box;
        append_bmff_box(&auxc_uuid_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_uuid_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), auxc_depth_box.begin(),
                            auxc_depth_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_alpha_box.begin(),
                            auxc_alpha_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_uuid_box.begin(),
                            auxc_uuid_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 4);

        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte { 0 });

        append_u16be(&ipma_payload, 2);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 1 });

        append_u16be(&ipma_payload, 3);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 2 });

        append_u16be(&ipma_payload, 4);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 3 });
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<std::string> aux_subtype_kind
        = collect_text_values(store, "aux.subtype_kind");
    ASSERT_EQ(aux_subtype_kind.size(), 3U);
    EXPECT_EQ(aux_subtype_kind[0], "ascii_z");
    EXPECT_EQ(aux_subtype_kind[1], "u64be");
    EXPECT_EQ(aux_subtype_kind[2], "uuid");

    const std::vector<std::string> aux_subtype_text
        = collect_text_values(store, "aux.subtype_text");
    ASSERT_EQ(aux_subtype_text.size(), 2U);
    EXPECT_EQ(aux_subtype_text[0], "profile");
    EXPECT_EQ(aux_subtype_text[1], "00010203-0405-0607-0809-0A0B0C0D0E0F");

    const std::vector<std::string> aux_subtype_uuid
        = collect_text_values(store, "aux.subtype_uuid");
    ASSERT_EQ(aux_subtype_uuid.size(), 1U);
    EXPECT_EQ(aux_subtype_uuid[0], "00010203-0405-0607-0809-0A0B0C0D0E0F");

    const std::vector<uint64_t> aux_subtype_u64
        = collect_u64_values(store, "aux.subtype_u64");
    ASSERT_EQ(aux_subtype_u64.size(), 1U);
    EXPECT_EQ(aux_subtype_u64[0], 0x1122334455667788ULL);

    const std::vector<std::string> iref_auxl_subtype_kind
        = collect_text_values(store, "iref.auxl.subtype_kind");
    ASSERT_EQ(iref_auxl_subtype_kind.size(), 3U);
    EXPECT_EQ(iref_auxl_subtype_kind[0], "ascii_z");
    EXPECT_EQ(iref_auxl_subtype_kind[1], "u64be");
    EXPECT_EQ(iref_auxl_subtype_kind[2], "uuid");

    const std::vector<uint64_t> iref_auxl_subtype_u64
        = collect_u64_values(store, "iref.auxl.subtype_u64");
    ASSERT_EQ(iref_auxl_subtype_u64.size(), 1U);
    EXPECT_EQ(iref_auxl_subtype_u64[0], 0x1122334455667788ULL);

    const std::vector<std::string> iref_auxl_subtype_uuid
        = collect_text_values(store, "iref.auxl.subtype_uuid");
    ASSERT_EQ(iref_auxl_subtype_uuid.size(), 1U);
    EXPECT_EQ(iref_auxl_subtype_uuid[0],
              "00010203-0405-0607-0809-0A0B0C0D0E0F");
}

TEST(BmffDerivedFieldsDecode, EmitsPerTypeUniqueCountsWithDuplicateEdges)
{
    // Minimal HEIF-like BMFF with duplicate iref edges so per-type
    // unique counters can be distinguished from edge counters.
    //
    // auxl: 1 -> [2,2,3] and 1 -> [3]        => edge=4, from_unique=1, to=2
    // dimg: 2 -> [5,5] and 4 -> [5]          => edge=3, from_unique=2, to=1
    // thmb: 3 -> [7,7] and 3 -> [8]          => edge=3, from_unique=1, to=2
    // cdsc: 4 -> [8,9,9] and 5 -> [8]        => edge=4, from_unique=2, to=2

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> auxl_a_payload;
        append_u16be(&auxl_a_payload, 1);
        append_u16be(&auxl_a_payload, 3);
        append_u16be(&auxl_a_payload, 2);
        append_u16be(&auxl_a_payload, 2);
        append_u16be(&auxl_a_payload, 3);
        std::vector<std::byte> auxl_a_box;
        append_bmff_box(&auxl_a_box, fourcc('a', 'u', 'x', 'l'),
                        auxl_a_payload);

        std::vector<std::byte> auxl_b_payload;
        append_u16be(&auxl_b_payload, 1);
        append_u16be(&auxl_b_payload, 1);
        append_u16be(&auxl_b_payload, 3);
        std::vector<std::byte> auxl_b_box;
        append_bmff_box(&auxl_b_box, fourcc('a', 'u', 'x', 'l'),
                        auxl_b_payload);

        std::vector<std::byte> dimg_a_payload;
        append_u16be(&dimg_a_payload, 2);
        append_u16be(&dimg_a_payload, 2);
        append_u16be(&dimg_a_payload, 5);
        append_u16be(&dimg_a_payload, 5);
        std::vector<std::byte> dimg_a_box;
        append_bmff_box(&dimg_a_box, fourcc('d', 'i', 'm', 'g'),
                        dimg_a_payload);

        std::vector<std::byte> dimg_b_payload;
        append_u16be(&dimg_b_payload, 4);
        append_u16be(&dimg_b_payload, 1);
        append_u16be(&dimg_b_payload, 5);
        std::vector<std::byte> dimg_b_box;
        append_bmff_box(&dimg_b_box, fourcc('d', 'i', 'm', 'g'),
                        dimg_b_payload);

        std::vector<std::byte> thmb_a_payload;
        append_u16be(&thmb_a_payload, 3);
        append_u16be(&thmb_a_payload, 2);
        append_u16be(&thmb_a_payload, 7);
        append_u16be(&thmb_a_payload, 7);
        std::vector<std::byte> thmb_a_box;
        append_bmff_box(&thmb_a_box, fourcc('t', 'h', 'm', 'b'),
                        thmb_a_payload);

        std::vector<std::byte> thmb_b_payload;
        append_u16be(&thmb_b_payload, 3);
        append_u16be(&thmb_b_payload, 1);
        append_u16be(&thmb_b_payload, 8);
        std::vector<std::byte> thmb_b_box;
        append_bmff_box(&thmb_b_box, fourcc('t', 'h', 'm', 'b'),
                        thmb_b_payload);

        std::vector<std::byte> cdsc_a_payload;
        append_u16be(&cdsc_a_payload, 4);
        append_u16be(&cdsc_a_payload, 3);
        append_u16be(&cdsc_a_payload, 8);
        append_u16be(&cdsc_a_payload, 9);
        append_u16be(&cdsc_a_payload, 9);
        std::vector<std::byte> cdsc_a_box;
        append_bmff_box(&cdsc_a_box, fourcc('c', 'd', 's', 'c'),
                        cdsc_a_payload);

        std::vector<std::byte> cdsc_b_payload;
        append_u16be(&cdsc_b_payload, 5);
        append_u16be(&cdsc_b_payload, 1);
        append_u16be(&cdsc_b_payload, 8);
        std::vector<std::byte> cdsc_b_box;
        append_bmff_box(&cdsc_b_box, fourcc('c', 'd', 's', 'c'),
                        cdsc_b_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), auxl_a_box.begin(),
                            auxl_a_box.end());
        iref_payload.insert(iref_payload.end(), auxl_b_box.begin(),
                            auxl_b_box.end());
        iref_payload.insert(iref_payload.end(), dimg_a_box.begin(),
                            dimg_a_box.end());
        iref_payload.insert(iref_payload.end(), dimg_b_box.begin(),
                            dimg_b_box.end());
        iref_payload.insert(iref_payload.end(), thmb_a_box.begin(),
                            thmb_a_box.end());
        iref_payload.insert(iref_payload.end(), thmb_b_box.begin(),
                            thmb_b_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_a_box.begin(),
                            cdsc_a_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_b_box.begin(),
                            cdsc_b_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> edge_count
        = collect_u32_values(store, "iref.edge_count");
    ASSERT_EQ(edge_count.size(), 1U);
    EXPECT_EQ(edge_count[0], 14U);

    const std::vector<uint32_t> auxl_edge
        = collect_u32_values(store, "iref.auxl.edge_count");
    ASSERT_EQ(auxl_edge.size(), 1U);
    EXPECT_EQ(auxl_edge[0], 4U);
    const std::vector<uint32_t> auxl_graph_edge
        = collect_u32_values(store, "iref.graph.auxl.edge_count");
    ASSERT_EQ(auxl_graph_edge.size(), 1U);
    EXPECT_EQ(auxl_graph_edge[0], 4U);
    const std::vector<uint32_t> auxl_from_unique
        = collect_u32_values(store, "iref.auxl.from_item_unique_count");
    ASSERT_EQ(auxl_from_unique.size(), 1U);
    EXPECT_EQ(auxl_from_unique[0], 1U);
    const std::vector<uint32_t> auxl_graph_from_unique
        = collect_u32_values(store, "iref.graph.auxl.from_item_unique_count");
    ASSERT_EQ(auxl_graph_from_unique.size(), 1U);
    EXPECT_EQ(auxl_graph_from_unique[0], 1U);
    const std::vector<uint32_t> auxl_to_unique
        = collect_u32_values(store, "iref.auxl.to_item_unique_count");
    ASSERT_EQ(auxl_to_unique.size(), 1U);
    EXPECT_EQ(auxl_to_unique[0], 2U);
    const std::vector<uint32_t> auxl_graph_to_unique
        = collect_u32_values(store, "iref.graph.auxl.to_item_unique_count");
    ASSERT_EQ(auxl_graph_to_unique.size(), 1U);
    EXPECT_EQ(auxl_graph_to_unique[0], 2U);

    const std::vector<uint32_t> dimg_edge
        = collect_u32_values(store, "iref.dimg.edge_count");
    ASSERT_EQ(dimg_edge.size(), 1U);
    EXPECT_EQ(dimg_edge[0], 3U);
    const std::vector<uint32_t> dimg_graph_edge
        = collect_u32_values(store, "iref.graph.dimg.edge_count");
    ASSERT_EQ(dimg_graph_edge.size(), 1U);
    EXPECT_EQ(dimg_graph_edge[0], 3U);
    const std::vector<uint32_t> dimg_from_unique
        = collect_u32_values(store, "iref.dimg.from_item_unique_count");
    ASSERT_EQ(dimg_from_unique.size(), 1U);
    EXPECT_EQ(dimg_from_unique[0], 2U);
    const std::vector<uint32_t> dimg_graph_from_unique
        = collect_u32_values(store, "iref.graph.dimg.from_item_unique_count");
    ASSERT_EQ(dimg_graph_from_unique.size(), 1U);
    EXPECT_EQ(dimg_graph_from_unique[0], 2U);
    const std::vector<uint32_t> dimg_to_unique
        = collect_u32_values(store, "iref.dimg.to_item_unique_count");
    ASSERT_EQ(dimg_to_unique.size(), 1U);
    EXPECT_EQ(dimg_to_unique[0], 1U);
    const std::vector<uint32_t> dimg_graph_to_unique
        = collect_u32_values(store, "iref.graph.dimg.to_item_unique_count");
    ASSERT_EQ(dimg_graph_to_unique.size(), 1U);
    EXPECT_EQ(dimg_graph_to_unique[0], 1U);

    const std::vector<uint32_t> thmb_edge
        = collect_u32_values(store, "iref.thmb.edge_count");
    ASSERT_EQ(thmb_edge.size(), 1U);
    EXPECT_EQ(thmb_edge[0], 3U);
    const std::vector<uint32_t> thmb_graph_edge
        = collect_u32_values(store, "iref.graph.thmb.edge_count");
    ASSERT_EQ(thmb_graph_edge.size(), 1U);
    EXPECT_EQ(thmb_graph_edge[0], 3U);
    const std::vector<uint32_t> thmb_from_unique
        = collect_u32_values(store, "iref.thmb.from_item_unique_count");
    ASSERT_EQ(thmb_from_unique.size(), 1U);
    EXPECT_EQ(thmb_from_unique[0], 1U);
    const std::vector<uint32_t> thmb_graph_from_unique
        = collect_u32_values(store, "iref.graph.thmb.from_item_unique_count");
    ASSERT_EQ(thmb_graph_from_unique.size(), 1U);
    EXPECT_EQ(thmb_graph_from_unique[0], 1U);
    const std::vector<uint32_t> thmb_to_unique
        = collect_u32_values(store, "iref.thmb.to_item_unique_count");
    ASSERT_EQ(thmb_to_unique.size(), 1U);
    EXPECT_EQ(thmb_to_unique[0], 2U);
    const std::vector<uint32_t> thmb_graph_to_unique
        = collect_u32_values(store, "iref.graph.thmb.to_item_unique_count");
    ASSERT_EQ(thmb_graph_to_unique.size(), 1U);
    EXPECT_EQ(thmb_graph_to_unique[0], 2U);

    const std::vector<uint32_t> cdsc_edge
        = collect_u32_values(store, "iref.cdsc.edge_count");
    ASSERT_EQ(cdsc_edge.size(), 1U);
    EXPECT_EQ(cdsc_edge[0], 4U);
    const std::vector<uint32_t> cdsc_graph_edge
        = collect_u32_values(store, "iref.graph.cdsc.edge_count");
    ASSERT_EQ(cdsc_graph_edge.size(), 1U);
    EXPECT_EQ(cdsc_graph_edge[0], 4U);
    const std::vector<uint32_t> cdsc_from_unique
        = collect_u32_values(store, "iref.cdsc.from_item_unique_count");
    ASSERT_EQ(cdsc_from_unique.size(), 1U);
    EXPECT_EQ(cdsc_from_unique[0], 2U);
    const std::vector<uint32_t> cdsc_graph_from_unique
        = collect_u32_values(store, "iref.graph.cdsc.from_item_unique_count");
    ASSERT_EQ(cdsc_graph_from_unique.size(), 1U);
    EXPECT_EQ(cdsc_graph_from_unique[0], 2U);
    const std::vector<uint32_t> cdsc_to_unique
        = collect_u32_values(store, "iref.cdsc.to_item_unique_count");
    ASSERT_EQ(cdsc_to_unique.size(), 1U);
    EXPECT_EQ(cdsc_to_unique[0], 2U);
    const std::vector<uint32_t> cdsc_graph_to_unique
        = collect_u32_values(store, "iref.graph.cdsc.to_item_unique_count");
    ASSERT_EQ(cdsc_graph_to_unique.size(), 1U);
    EXPECT_EQ(cdsc_graph_to_unique[0], 2U);

    const std::vector<uint32_t> item_count
        = collect_u32_values(store, "iref.item_count");
    ASSERT_EQ(item_count.size(), 1U);
    EXPECT_EQ(item_count[0], 8U);
    const std::vector<uint32_t> from_unique_count
        = collect_u32_values(store, "iref.from_item_unique_count");
    ASSERT_EQ(from_unique_count.size(), 1U);
    EXPECT_EQ(from_unique_count[0], 5U);
    const std::vector<uint32_t> to_unique_count
        = collect_u32_values(store, "iref.to_item_unique_count");
    ASSERT_EQ(to_unique_count.size(), 1U);
    EXPECT_EQ(to_unique_count[0], 6U);

    const std::vector<uint32_t> auxl_item_count
        = collect_u32_values(store, "iref.auxl.item_count");
    ASSERT_EQ(auxl_item_count.size(), 1U);
    EXPECT_EQ(auxl_item_count[0], 3U);
    const std::vector<uint32_t> auxl_item_ids
        = collect_u32_values(store, "iref.auxl.item_id");
    ASSERT_EQ(auxl_item_ids.size(), 3U);
    EXPECT_EQ(auxl_item_ids[0], 1U);
    EXPECT_EQ(auxl_item_ids[1], 2U);
    EXPECT_EQ(auxl_item_ids[2], 3U);
    const std::vector<uint32_t> auxl_out
        = collect_u32_values(store, "iref.auxl.item_out_edge_count");
    ASSERT_EQ(auxl_out.size(), 3U);
    EXPECT_EQ(auxl_out[0], 4U);
    EXPECT_EQ(auxl_out[1], 0U);
    EXPECT_EQ(auxl_out[2], 0U);
    const std::vector<uint32_t> auxl_in
        = collect_u32_values(store, "iref.auxl.item_in_edge_count");
    ASSERT_EQ(auxl_in.size(), 3U);
    EXPECT_EQ(auxl_in[0], 0U);
    EXPECT_EQ(auxl_in[1], 2U);
    EXPECT_EQ(auxl_in[2], 2U);

    const std::vector<uint32_t> dimg_item_count
        = collect_u32_values(store, "iref.dimg.item_count");
    ASSERT_EQ(dimg_item_count.size(), 1U);
    EXPECT_EQ(dimg_item_count[0], 3U);
    const std::vector<uint32_t> dimg_item_ids
        = collect_u32_values(store, "iref.dimg.item_id");
    ASSERT_EQ(dimg_item_ids.size(), 3U);
    EXPECT_EQ(dimg_item_ids[0], 2U);
    EXPECT_EQ(dimg_item_ids[1], 5U);
    EXPECT_EQ(dimg_item_ids[2], 4U);
    const std::vector<uint32_t> dimg_out
        = collect_u32_values(store, "iref.dimg.item_out_edge_count");
    ASSERT_EQ(dimg_out.size(), 3U);
    EXPECT_EQ(dimg_out[0], 2U);
    EXPECT_EQ(dimg_out[1], 0U);
    EXPECT_EQ(dimg_out[2], 1U);
    const std::vector<uint32_t> dimg_in
        = collect_u32_values(store, "iref.dimg.item_in_edge_count");
    ASSERT_EQ(dimg_in.size(), 3U);
    EXPECT_EQ(dimg_in[0], 0U);
    EXPECT_EQ(dimg_in[1], 3U);
    EXPECT_EQ(dimg_in[2], 0U);

    const std::vector<uint32_t> thmb_item_count
        = collect_u32_values(store, "iref.thmb.item_count");
    ASSERT_EQ(thmb_item_count.size(), 1U);
    EXPECT_EQ(thmb_item_count[0], 3U);
    const std::vector<uint32_t> thmb_item_ids
        = collect_u32_values(store, "iref.thmb.item_id");
    ASSERT_EQ(thmb_item_ids.size(), 3U);
    EXPECT_EQ(thmb_item_ids[0], 3U);
    EXPECT_EQ(thmb_item_ids[1], 7U);
    EXPECT_EQ(thmb_item_ids[2], 8U);

    const std::vector<uint32_t> cdsc_item_count
        = collect_u32_values(store, "iref.cdsc.item_count");
    ASSERT_EQ(cdsc_item_count.size(), 1U);
    EXPECT_EQ(cdsc_item_count[0], 4U);
    const std::vector<uint32_t> cdsc_item_ids
        = collect_u32_values(store, "iref.cdsc.item_id");
    ASSERT_EQ(cdsc_item_ids.size(), 4U);
    EXPECT_EQ(cdsc_item_ids[0], 4U);
    EXPECT_EQ(cdsc_item_ids[1], 8U);
    EXPECT_EQ(cdsc_item_ids[2], 9U);
    EXPECT_EQ(cdsc_item_ids[3], 5U);
}

TEST(BmffDerivedFieldsDecode, EmitsItemInfoRowsAndPrimaryAliases)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        const uint32_t kMimeItem = 0x10001U;
        const uint32_t kExifItem = 0x10002U;

        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 1);
        append_u32be(&pitm_payload, kExifItem);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> infe1_payload;
        append_fullbox_header(&infe1_payload, 3);
        append_u32be(&infe1_payload, kMimeItem);
        append_u16be(&infe1_payload, 0);
        append_fourcc(&infe1_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe1_payload, "preview");
        infe1_payload.push_back(std::byte { 0 });
        append_bytes(&infe1_payload, "image/png");
        infe1_payload.push_back(std::byte { 0 });
        append_bytes(&infe1_payload, "gzip");
        infe1_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe1_box;
        append_bmff_box(&infe1_box, fourcc('i', 'n', 'f', 'e'), infe1_payload);

        std::vector<std::byte> infe2_payload;
        append_fullbox_header(&infe2_payload, 3);
        append_u32be(&infe2_payload, kExifItem);
        append_u16be(&infe2_payload, 0);
        append_fourcc(&infe2_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe2_payload, "exif");
        infe2_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe2_box;
        append_bmff_box(&infe2_box, fourcc('i', 'n', 'f', 'e'), infe2_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 2);
        iinf_payload.insert(iinf_payload.end(), infe1_box.begin(),
                            infe1_box.end());
        iinf_payload.insert(iinf_payload.end(), infe2_box.begin(),
                            infe2_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> info_count
        = collect_u32_values(store, "item.info_count");
    ASSERT_EQ(info_count.size(), 1U);
    EXPECT_EQ(info_count[0], 2U);

    const std::vector<uint32_t> item_ids = collect_u32_values(store, "item.id");
    ASSERT_EQ(item_ids.size(), 2U);
    EXPECT_EQ(item_ids[0], 0x10001U);
    EXPECT_EQ(item_ids[1], 0x10002U);

    const std::vector<uint32_t> item_types = collect_u32_values(store,
                                                                "item.type");
    ASSERT_EQ(item_types.size(), 2U);
    EXPECT_EQ(item_types[0], fourcc('m', 'i', 'm', 'e'));
    EXPECT_EQ(item_types[1], fourcc('E', 'x', 'i', 'f'));

    const std::vector<std::string> item_type_names
        = collect_text_values(store, "item.type_name");
    ASSERT_EQ(item_type_names.size(), 2U);
    EXPECT_EQ(item_type_names[0], "mime");
    EXPECT_EQ(item_type_names[1], "Exif");

    const std::vector<std::string> item_semantics
        = collect_text_values(store, "item.semantic");
    ASSERT_EQ(item_semantics.size(), 2U);
    EXPECT_EQ(item_semantics[0], "image");
    EXPECT_EQ(item_semantics[1], "exif");

    const std::vector<uint32_t> semantic_known_count
        = collect_u32_values(store, "item.semantic_known_count");
    ASSERT_EQ(semantic_known_count.size(), 1U);
    EXPECT_EQ(semantic_known_count[0], 2U);
    const std::vector<uint32_t> semantic_metadata_count
        = collect_u32_values(store, "item.semantic_metadata_count");
    ASSERT_EQ(semantic_metadata_count.size(), 1U);
    EXPECT_EQ(semantic_metadata_count[0], 1U);
    const std::vector<uint32_t> semantic_image_count
        = collect_u32_values(store, "item.semantic_image_count");
    ASSERT_EQ(semantic_image_count.size(), 1U);
    EXPECT_EQ(semantic_image_count[0], 1U);
    const std::vector<uint32_t> semantic_exif_count
        = collect_u32_values(store, "item.semantic_exif_count");
    ASSERT_EQ(semantic_exif_count.size(), 1U);
    EXPECT_EQ(semantic_exif_count[0], 1U);

    const std::vector<std::string> item_names
        = collect_text_values(store, "item.name");
    ASSERT_EQ(item_names.size(), 2U);
    EXPECT_EQ(item_names[0], "preview");
    EXPECT_EQ(item_names[1], "exif");

    const std::vector<std::string> content_types
        = collect_text_values(store, "item.content_type");
    ASSERT_EQ(content_types.size(), 1U);
    EXPECT_EQ(content_types[0], "image/png");

    const std::vector<std::string> content_encoding
        = collect_text_values(store, "item.content_encoding");
    ASSERT_EQ(content_encoding.size(), 1U);
    EXPECT_EQ(content_encoding[0], "gzip");

    const std::vector<uint32_t> primary_type
        = collect_u32_values(store, "primary.item_type");
    ASSERT_EQ(primary_type.size(), 1U);
    EXPECT_EQ(primary_type[0], fourcc('E', 'x', 'i', 'f'));

    const std::vector<std::string> primary_type_name
        = collect_text_values(store, "primary.item_type_name");
    ASSERT_EQ(primary_type_name.size(), 1U);
    EXPECT_EQ(primary_type_name[0], "Exif");

    const std::vector<std::string> primary_semantic
        = collect_text_values(store, "primary.item_semantic");
    ASSERT_EQ(primary_semantic.size(), 1U);
    EXPECT_EQ(primary_semantic[0], "exif");

    const std::vector<std::string> primary_name
        = collect_text_values(store, "primary.item_name");
    ASSERT_EQ(primary_name.size(), 1U);
    EXPECT_EQ(primary_name[0], "exif");

    EXPECT_TRUE(collect_text_values(store, "primary.content_type").empty());
}

TEST(BmffDerivedFieldsDecode, EmitsItemSemanticLabelsForMetadataCarrierItems)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 3);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 6);
        append_infe_v2(&iinf_payload, 1, 0, fourcc('E', 'x', 'i', 'f'), "exif");
        append_infe_v2_mime(&iinf_payload, 2, 0, "xmp", "application/rdf+xml",
                            "");
        append_infe_v2_mime(&iinf_payload, 3, 0, "manifest",
                            "application/c2pa+jumbf", "");
        append_infe_v2_mime(&iinf_payload, 4, 0, "icc",
                            "application/vnd.iccprofile", "");
        append_infe_v2(&iinf_payload, 5, 0, fourcc('j', 'u', 'm', 'b'),
                       "jumbf");
        append_infe_v2(&iinf_payload, 6, 0, fourcc('t', 'i', 'l', 'i'),
                       "tiled");
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<std::string> item_type_names
        = collect_text_values(store, "item.type_name");
    ASSERT_EQ(item_type_names.size(), 6U);
    EXPECT_EQ(item_type_names[0], "Exif");
    EXPECT_EQ(item_type_names[1], "mime");
    EXPECT_EQ(item_type_names[2], "mime");
    EXPECT_EQ(item_type_names[3], "mime");
    EXPECT_EQ(item_type_names[4], "jumb");
    EXPECT_EQ(item_type_names[5], "tili");

    const std::vector<std::string> item_semantics
        = collect_text_values(store, "item.semantic");
    ASSERT_EQ(item_semantics.size(), 6U);
    EXPECT_EQ(item_semantics[0], "exif");
    EXPECT_EQ(item_semantics[1], "xmp");
    EXPECT_EQ(item_semantics[2], "c2pa");
    EXPECT_EQ(item_semantics[3], "icc_profile");
    EXPECT_EQ(item_semantics[4], "jumbf");
    EXPECT_EQ(item_semantics[5], "image");

    const std::vector<uint32_t> semantic_known_count
        = collect_u32_values(store, "item.semantic_known_count");
    ASSERT_EQ(semantic_known_count.size(), 1U);
    EXPECT_EQ(semantic_known_count[0], 6U);
    const std::vector<uint32_t> semantic_metadata_count
        = collect_u32_values(store, "item.semantic_metadata_count");
    ASSERT_EQ(semantic_metadata_count.size(), 1U);
    EXPECT_EQ(semantic_metadata_count[0], 5U);
    const std::vector<uint32_t> semantic_image_count
        = collect_u32_values(store, "item.semantic_image_count");
    ASSERT_EQ(semantic_image_count.size(), 1U);
    EXPECT_EQ(semantic_image_count[0], 1U);
    const std::vector<uint32_t> semantic_exif_count
        = collect_u32_values(store, "item.semantic_exif_count");
    ASSERT_EQ(semantic_exif_count.size(), 1U);
    EXPECT_EQ(semantic_exif_count[0], 1U);
    const std::vector<uint32_t> semantic_xmp_count
        = collect_u32_values(store, "item.semantic_xmp_count");
    ASSERT_EQ(semantic_xmp_count.size(), 1U);
    EXPECT_EQ(semantic_xmp_count[0], 1U);
    const std::vector<uint32_t> semantic_c2pa_count
        = collect_u32_values(store, "item.semantic_c2pa_count");
    ASSERT_EQ(semantic_c2pa_count.size(), 1U);
    EXPECT_EQ(semantic_c2pa_count[0], 1U);
    const std::vector<uint32_t> semantic_icc_count
        = collect_u32_values(store, "item.semantic_icc_profile_count");
    ASSERT_EQ(semantic_icc_count.size(), 1U);
    EXPECT_EQ(semantic_icc_count[0], 1U);
    const std::vector<uint32_t> semantic_jumbf_count
        = collect_u32_values(store, "item.semantic_jumbf_count");
    ASSERT_EQ(semantic_jumbf_count.size(), 1U);
    EXPECT_EQ(semantic_jumbf_count[0], 1U);

    const std::vector<std::string> primary_semantic
        = collect_text_values(store, "primary.item_semantic");
    ASSERT_EQ(primary_semantic.size(), 1U);
    EXPECT_EQ(primary_semantic[0], "c2pa");

    const std::vector<uint8_t> primary_metadata_carrier
        = collect_u8_values(store, "primary.metadata_carrier");
    ASSERT_EQ(primary_metadata_carrier.size(), 1U);
    EXPECT_EQ(primary_metadata_carrier[0], 1U);

    const std::vector<uint8_t> primary_c2pa_carrier
        = collect_u8_values(store, "primary.c2pa_carrier");
    ASSERT_EQ(primary_c2pa_carrier.size(), 1U);
    EXPECT_EQ(primary_c2pa_carrier[0], 1U);

    const std::vector<uint8_t> primary_jumbf_carrier
        = collect_u8_values(store, "primary.jumbf_carrier");
    EXPECT_TRUE(primary_jumbf_carrier.empty());

    const std::vector<std::string> primary_content_type
        = collect_text_values(store, "primary.content_type");
    ASSERT_EQ(primary_content_type.size(), 1U);
    EXPECT_EQ(primary_content_type[0], "application/c2pa+jumbf");
}

TEST(BmffDerivedFieldsDecode, EmitsPrimaryMimeItemInfoFromInfeV2)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);
        append_u16be(&infe_payload, 7);
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "payload");
        infe_payload.push_back(std::byte { 0 });
        append_bytes(&infe_payload, "application/rdf+xml");
        infe_payload.push_back(std::byte { 0 });
        append_bytes(&infe_payload, "gzip");
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> primary_type
        = collect_u32_values(store, "primary.item_type");
    ASSERT_EQ(primary_type.size(), 1U);
    EXPECT_EQ(primary_type[0], fourcc('m', 'i', 'm', 'e'));

    const std::vector<std::string> primary_name
        = collect_text_values(store, "primary.item_name");
    ASSERT_EQ(primary_name.size(), 1U);
    EXPECT_EQ(primary_name[0], "payload");

    const std::vector<std::string> primary_content_type
        = collect_text_values(store, "primary.content_type");
    ASSERT_EQ(primary_content_type.size(), 1U);
    EXPECT_EQ(primary_content_type[0], "application/rdf+xml");

    const std::vector<std::string> primary_content_encoding
        = collect_text_values(store, "primary.content_encoding");
    ASSERT_EQ(primary_content_encoding.size(), 1U);
    EXPECT_EQ(primary_content_encoding[0], "gzip");
}

TEST(BmffDerivedFieldsDecode, EmitsItemInfoRowsWithoutPitm)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 3);
        append_u16be(&infe_payload, 0);
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "sidecar");
        infe_payload.push_back(std::byte { 0 });
        append_bytes(&infe_payload, "application/json");
        infe_payload.push_back(std::byte { 0 });
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> info_count
        = collect_u32_values(store, "item.info_count");
    ASSERT_EQ(info_count.size(), 1U);
    EXPECT_EQ(info_count[0], 1U);

    const std::vector<uint32_t> item_ids = collect_u32_values(store, "item.id");
    ASSERT_EQ(item_ids.size(), 1U);
    EXPECT_EQ(item_ids[0], 3U);

    const std::vector<uint32_t> item_types = collect_u32_values(store,
                                                                "item.type");
    ASSERT_EQ(item_types.size(), 1U);
    EXPECT_EQ(item_types[0], fourcc('m', 'i', 'm', 'e'));

    const std::vector<std::string> item_names
        = collect_text_values(store, "item.name");
    ASSERT_EQ(item_names.size(), 1U);
    EXPECT_EQ(item_names[0], "sidecar");

    const std::vector<std::string> content_types
        = collect_text_values(store, "item.content_type");
    ASSERT_EQ(content_types.size(), 1U);
    EXPECT_EQ(content_types[0], "application/json");

    EXPECT_TRUE(collect_u32_values(store, "meta.primary_item_id").empty());
    EXPECT_TRUE(collect_u32_values(store, "primary.item_type").empty());
    EXPECT_TRUE(collect_text_values(store, "primary.item_name").empty());
}

TEST(BmffDerivedFieldsDecode, EmitsPrimaryUriItemInfoFromInfeV2)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);
        append_u16be(&infe_payload, 3);
        append_fourcc(&infe_payload, fourcc('u', 'r', 'i', ' '));
        append_bytes(&infe_payload, "link");
        infe_payload.push_back(std::byte { 0 });
        append_bytes(&infe_payload, "https://ns.example/item");
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> item_types = collect_u32_values(store,
                                                                "item.type");
    ASSERT_EQ(item_types.size(), 1U);
    EXPECT_EQ(item_types[0], fourcc('u', 'r', 'i', ' '));

    const std::vector<std::string> item_names
        = collect_text_values(store, "item.name");
    ASSERT_EQ(item_names.size(), 1U);
    EXPECT_EQ(item_names[0], "link");

    const std::vector<std::string> item_uri_type
        = collect_text_values(store, "item.uri_type");
    ASSERT_EQ(item_uri_type.size(), 1U);
    EXPECT_EQ(item_uri_type[0], "https://ns.example/item");

    const std::vector<uint32_t> primary_type
        = collect_u32_values(store, "primary.item_type");
    ASSERT_EQ(primary_type.size(), 1U);
    EXPECT_EQ(primary_type[0], fourcc('u', 'r', 'i', ' '));

    const std::vector<std::string> primary_name
        = collect_text_values(store, "primary.item_name");
    ASSERT_EQ(primary_name.size(), 1U);
    EXPECT_EQ(primary_name[0], "link");

    const std::vector<std::string> primary_uri_type
        = collect_text_values(store, "primary.uri_type");
    ASSERT_EQ(primary_uri_type.size(), 1U);
    EXPECT_EQ(primary_uri_type[0], "https://ns.example/item");

    EXPECT_TRUE(collect_text_values(store, "primary.content_type").empty());
}

TEST(BmffDerivedFieldsDecode, InterpretsBoundedTiledImageConfiguration)
{
    const std::array<uint32_t, 2> dimensions { 3U, 5U };
    const std::vector<std::byte> file
        = make_tiled_image_configuration_file(0U, 0U, 1000U, 700U, 256U, 256U,
                                              dimensions, false, 1U, 1U, 7U);

    MetaStore store;
    decode_bmff_test_file(file, &store);

    EXPECT_EQ(collect_u32_values(store, "ipco.tilC_count"),
              std::vector<uint32_t>({ 1U }));
    EXPECT_EQ(collect_u32_values(store, "ipco.known_property_count"),
              std::vector<uint32_t>({ 2U }));
    EXPECT_EQ(collect_u32_values(store, "ipma.tilC.association_count"),
              std::vector<uint32_t>({ 1U }));
    EXPECT_EQ(collect_text_values(store, "ipma.property_type_name"),
              std::vector<std::string>({ "ispe", "tilC" }));
    EXPECT_EQ(collect_u32_values(store, "tiled_image.count"),
              std::vector<uint32_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.configuration_valid"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.layout_valid"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_text_values(store, "tiled_image.configuration"),
              std::vector<std::string>({ "tiled" }));
    EXPECT_EQ(collect_u32_values(store, "tiled_image.tile_width"),
              std::vector<uint32_t>({ 256U }));
    EXPECT_EQ(collect_u32_values(store, "tiled_image.tile_height"),
              std::vector<uint32_t>({ 256U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.extra_dimension_count"),
              std::vector<uint8_t>({ 2U }));
    EXPECT_EQ(collect_u32_values(store, "tiled_image.dimension_size"),
              std::vector<uint32_t>({ 3U, 5U }));
    EXPECT_EQ(collect_u32_values(store, "tiled_image.conditional_payload_bytes"),
              std::vector<uint32_t>({ 7U }));
    EXPECT_EQ(collect_u32_values(store, "tiled_image.tile_columns"),
              std::vector<uint32_t>({ 4U }));
    EXPECT_EQ(collect_u32_values(store, "tiled_image.tile_rows"),
              std::vector<uint32_t>({ 3U }));
    EXPECT_EQ(collect_u64_values(store, "tiled_image.expected_tile_count"),
              std::vector<uint64_t>({ 180U }));
}

TEST(BmffDerivedFieldsDecode, RejectsMalformedTiledImageConfigurationCores)
{
    const std::array<uint32_t, 1> dimension { 2U };
    const std::array<uint32_t, 1> zero_dimension { 0U };
    const std::array<uint32_t, 9> too_many_dimensions { 2U, 2U, 2U, 2U, 2U,
                                                        2U, 2U, 2U, 2U };

    const std::vector<std::vector<std::byte>> files {
        make_tiled_image_configuration_file(1U, 0U, 640U, 480U, 64U, 64U,
                                            dimension, false, 1U, 1U, 0U),
        make_tiled_image_configuration_file(0U, 1U, 640U, 480U, 64U, 64U,
                                            dimension, false, 1U, 1U, 0U),
        make_tiled_image_configuration_file(0U, 0U, 640U, 480U, 0U, 64U,
                                            dimension, false, 1U, 1U, 0U),
        make_tiled_image_configuration_file(0U, 0U, 640U, 480U, 64U, 64U,
                                            dimension, true, 1U, 1U, 0U),
        make_tiled_image_configuration_file(0U, 0U, 640U, 480U, 64U, 64U,
                                            zero_dimension, false, 1U, 1U, 0U),
        make_tiled_image_configuration_file(0U, 0U, 640U, 480U, 64U, 64U,
                                            too_many_dimensions, false, 1U, 1U,
                                            0U),
    };

    for (size_t i = 0U; i < files.size(); ++i) {
        MetaStore store;
        decode_bmff_test_file(files[i], &store);
        EXPECT_EQ(collect_u8_values(store, "tiled_image.configuration_valid"),
                  std::vector<uint8_t>({ 0U }));
        EXPECT_EQ(collect_u8_values(store, "tiled_image.layout_valid"),
                  std::vector<uint8_t>({ 0U }));
    }

    MetaStore truncated_store;
    decode_bmff_test_file(files[3], &truncated_store);
    EXPECT_EQ(collect_u8_values(truncated_store,
                                "tiled_image.dimensions_truncated"),
              std::vector<uint8_t>({ 1U }));

    MetaStore capped_store;
    decode_bmff_test_file(files[5], &capped_store);
    EXPECT_EQ(collect_u8_values(capped_store,
                                "tiled_image.dimensions_truncated"),
              std::vector<uint8_t>({ 1U }));
}

TEST(BmffDerivedFieldsDecode, ValidatesTiledImagePropertyRelationships)
{
    const std::array<uint32_t, 0> dimensions {};
    const std::vector<std::byte> duplicate_file
        = make_tiled_image_configuration_file(0U, 0U, 640U, 480U, 64U, 64U,
                                              dimensions, false, 1U, 2U, 0U);
    MetaStore duplicate_store;
    decode_bmff_test_file(duplicate_file, &duplicate_store);
    EXPECT_EQ(collect_u32_values(duplicate_store,
                                 "tiled_image.configuration_count"),
              std::vector<uint32_t>({ 2U }));
    EXPECT_EQ(collect_u8_values(duplicate_store,
                                "tiled_image.configuration_unique"),
              std::vector<uint8_t>({ 0U }));
    EXPECT_EQ(collect_u8_values(duplicate_store,
                                "tiled_image.configuration_valid"),
              std::vector<uint8_t>({ 0U }));

    const std::vector<std::byte> missing_ispe_file
        = make_tiled_image_configuration_file(0U, 0U, 640U, 480U, 64U, 64U,
                                              dimensions, false, 0U, 1U, 0U);
    MetaStore missing_ispe_store;
    decode_bmff_test_file(missing_ispe_file, &missing_ispe_store);
    EXPECT_EQ(collect_u8_values(missing_ispe_store,
                                "tiled_image.configuration_valid"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(missing_ispe_store, "tiled_image.ispe_present"),
              std::vector<uint8_t>({ 0U }));
    EXPECT_EQ(collect_u8_values(missing_ispe_store, "tiled_image.layout_valid"),
              std::vector<uint8_t>({ 0U }));

    const std::vector<std::byte> missing_tilc_file
        = make_tiled_image_configuration_file(0U, 0U, 640U, 480U, 64U, 64U,
                                              dimensions, false, 1U, 0U, 0U);
    MetaStore missing_tilc_store;
    decode_bmff_test_file(missing_tilc_file, &missing_tilc_store);
    EXPECT_EQ(collect_u8_values(missing_tilc_store,
                                "tiled_image.configuration_present"),
              std::vector<uint8_t>({ 0U }));
    EXPECT_EQ(collect_u8_values(missing_tilc_store,
                                "tiled_image.configuration_valid"),
              std::vector<uint8_t>({ 0U }));
    EXPECT_EQ(collect_u8_values(missing_tilc_store, "tiled_image.layout_valid"),
              std::vector<uint8_t>({ 0U }));
}

TEST(BmffDerivedFieldsDecode, RejectsTiledImageTileCountOverflow)
{
    const std::array<uint32_t, 1> dimensions { 2U };
    const std::vector<std::byte> file = make_tiled_image_configuration_file(
        0U, 0U, UINT32_MAX, UINT32_MAX, 1U, 1U, dimensions, false, 1U, 1U, 0U);
    MetaStore store;
    decode_bmff_test_file(file, &store);

    EXPECT_EQ(collect_u8_values(store, "tiled_image.configuration_valid"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.tile_count_overflow"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.layout_valid"),
              std::vector<uint8_t>({ 0U }));
    EXPECT_TRUE(
        collect_u64_values(store, "tiled_image.expected_tile_count").empty());
}

TEST(BmffDerivedFieldsDecode, ValidatesCompleteInternalTiledImageLayout)
{
    const CompleteTiledImageOptions options {};
    const std::vector<std::byte> file = make_complete_tiled_image_file(options);
    MetaStore store;
    decode_bmff_test_file(file, &store);

    EXPECT_EQ(collect_u8_values(store,
                                "tiled_image.complete_configuration_valid"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u32_values(store, "tiled_image.data_reference_index"),
              std::vector<uint32_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.data_reference_valid"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.external_tiles"),
              std::vector<uint8_t>({ 0U }));
    EXPECT_EQ(collect_u64_values(store, "tiled_image.input_item_count"),
              std::vector<uint64_t>({ 2U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.input_item_count_matches"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.conditional_payload_valid"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u32_values(store, "tiled_image.tile_item_type"),
              std::vector<uint32_t>({ fourcc('a', 'v', '0', '1') }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.tipa_valid"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u32_values(store, "tiled_image.tile_property_index"),
              std::vector<uint32_t>({ 3U }));
    EXPECT_EQ(collect_u32_values(store, "tiled_image.tile_property_type"),
              std::vector<uint32_t>({ fourcc('a', 'v', '1', 'C') }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.offset_table_valid"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u64_values(store,
                                 "tiled_image.expected_offset_table_size"),
              std::vector<uint64_t>({ 16U }));
    EXPECT_EQ(collect_u64_values(store, "tiled_image.logical_item_data_size"),
              std::vector<uint64_t>({ 24U }));
    EXPECT_EQ(collect_u64_values(store, "tiled_image.offset.start"),
              std::vector<uint64_t>({ 16U, 20U }));
    EXPECT_EQ(collect_u64_values(store, "tiled_image.offset.size"),
              std::vector<uint64_t>({ 4U, 4U }));
}

TEST(BmffDerivedFieldsDecode, ValidatesCompleteExternalTiledImageLayout)
{
    CompleteTiledImageOptions options;
    options.external_tiles            = true;
    const std::vector<std::byte> file = make_complete_tiled_image_file(options);
    MetaStore store;
    decode_bmff_test_file(file, &store);

    EXPECT_EQ(collect_u8_values(store,
                                "tiled_image.complete_configuration_valid"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.external_tiles"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.conditional_payload_valid"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u16_values(store, "tiled_image.directory_id_start"),
              std::vector<uint16_t>({ 10U }));
    EXPECT_EQ(collect_u16_values(store, "tiled_image.directory_id_end"),
              std::vector<uint16_t>({ 12U }));
    EXPECT_EQ(collect_u64_values(store, "tiled_image.tile_id_start"),
              std::vector<uint64_t>({ 1000U }));
    EXPECT_EQ(collect_text_values(store, "tiled_image.base_url"),
              std::vector<std::string>({ "https://tiles.example/image/" }));
    EXPECT_EQ(collect_text_values(store, "tiled_image.tile_request_template"),
              std::vector<std::string>({ "$tileID$.heif" }));
    EXPECT_TRUE(
        collect_u8_values(store, "tiled_image.offset_table_valid").empty());
}

TEST(BmffDerivedFieldsDecode, InfersSequentialTiledImageSizes)
{
    CompleteTiledImageOptions options;
    options.include_tile_sizes         = false;
    options.sequential_order           = true;
    options.declared_offset_table_size = 8U;
    options.first_tile_offset          = 8U;
    options.second_tile_offset         = 12U;
    options.tipa_flags                 = 1U;
    const std::vector<std::byte> file = make_complete_tiled_image_file(options);
    MetaStore store;
    decode_bmff_test_file(file, &store);

    EXPECT_EQ(collect_u8_values(store,
                                "tiled_image.complete_configuration_valid"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.sequential_order"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.size_field_bytes"),
              std::vector<uint8_t>({ 0U }));
    EXPECT_EQ(collect_u32_values(store, "tiled_image.tipa_flags"),
              std::vector<uint32_t>({ 1U }));
    EXPECT_EQ(collect_u8_values(store, "tiled_image.tile_sizes_validated"),
              std::vector<uint8_t>({ 1U }));
    EXPECT_EQ(collect_u64_values(store, "tiled_image.offset.start"),
              std::vector<uint64_t>({ 8U, 12U }));
    EXPECT_EQ(collect_u64_values(store, "tiled_image.offset.size"),
              std::vector<uint64_t>({ 4U, 4U }));
}

TEST(BmffDerivedFieldsDecode, RejectsIncompleteTiledImageLayouts)
{
    std::array<CompleteTiledImageOptions, 10> options {};
    options[0].data_reference_index       = 2U;
    options[1].input_item_count           = 3U;
    options[2].tipa_property_index        = 4U;
    options[3].declared_offset_table_size = 12U;
    options[4].second_tile_offset         = 23U;
    options[5].omit_internal_conditional  = true;
    options[6].construction_method        = 1U;
    options[7].tipa_version               = 1U;
    options[8].tipa_flags                 = 2U;
    options[9].deti_extra_flags           = 0x100U;

    for (size_t i = 0U; i < options.size(); ++i) {
        const std::vector<std::byte> file = make_complete_tiled_image_file(
            options[i]);
        MetaStore store;
        decode_bmff_test_file(file, &store);
        EXPECT_EQ(collect_u8_values(store,
                                    "tiled_image.complete_configuration_valid"),
                  std::vector<uint8_t>({ 0U }))
            << "case " << i;
    }

    CompleteTiledImageOptions external_options;
    external_options.external_tiles            = true;
    external_options.add_external_conditional  = true;
    const std::vector<std::byte> external_file = make_complete_tiled_image_file(
        external_options);
    MetaStore external_store;
    decode_bmff_test_file(external_file, &external_store);
    EXPECT_EQ(collect_u8_values(external_store,
                                "tiled_image.complete_configuration_valid"),
              std::vector<uint8_t>({ 0U }));
    EXPECT_EQ(collect_u8_values(external_store,
                                "tiled_image.conditional_payload_valid"),
              std::vector<uint8_t>({ 0U }));

    std::array<CompleteTiledImageOptions, 2> malformed_external {};
    malformed_external[0].external_tiles             = true;
    malformed_external[0].external_directory_flags   = 0x80U;
    malformed_external[1].external_tiles             = true;
    malformed_external[1].omit_external_template_nul = true;
    for (size_t i = 0U; i < malformed_external.size(); ++i) {
        const std::vector<std::byte> malformed_file
            = make_complete_tiled_image_file(malformed_external[i]);
        MetaStore malformed_store;
        decode_bmff_test_file(malformed_file, &malformed_store);
        EXPECT_EQ(collect_u8_values(malformed_store,
                                    "tiled_image.complete_configuration_valid"),
                  std::vector<uint8_t>({ 0U }))
            << "external case " << i;
        EXPECT_EQ(collect_u8_values(malformed_store,
                                    "tiled_image.data_reference_valid"),
                  std::vector<uint8_t>({ 0U }))
            << "external case " << i;
    }
}

}  // namespace openmeta
