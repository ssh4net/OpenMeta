// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_query.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static EntryId add_exif_u32_array(MetaStore* store, std::string_view ifd,
                                      uint16_t tag,
                                      std::span<const uint32_t> values,
                                      EntryFlags flags = EntryFlags::None)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_u32_array(store->arena(), values);
        entry.flags      = flags;
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_u32(MetaStore* store, std::string_view ifd,
                                uint16_t tag, uint32_t value,
                                EntryFlags flags = EntryFlags::None)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_u32(value);
        entry.flags      = flags;
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_urational(MetaStore* store, std::string_view ifd,
                                      uint16_t tag, uint32_t numer,
                                      uint32_t denom)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_urational(numer, denom);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_xmp_text(MetaStore* store, std::string_view ns,
                                std::string_view path, std::string_view value)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_xmp_property_key(store->arena(), ns, path);
        entry.value      = make_text(store->arena(), value, TextEncoding::Utf8);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_text(MetaStore* store, std::string_view ifd,
                                 uint16_t tag, std::string_view value)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_text(store->arena(), value, TextEncoding::Utf8);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_iptc_text(MetaStore* store, uint16_t record,
                                 uint16_t dataset, std::string_view value)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_iptc_dataset_key(record, dataset);
        entry.value      = make_text(store->arena(), value, TextEncoding::Utf8);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_icc_header_u32(MetaStore* store, uint32_t offset,
                                      uint32_t value)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_icc_header_field_key(offset);
        entry.value      = make_u32(value);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_icc_tag_bytes(MetaStore* store, uint32_t signature,
                                     std::span<const std::byte> bytes)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_icc_tag_key(signature);
        entry.value      = make_bytes(store->arena(), bytes);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_png_text(MetaStore* store, std::string_view keyword,
                                std::string_view field, std::string_view value)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_png_text_key(store->arena(), keyword, field);
        entry.value      = make_text(store->arena(), value, TextEncoding::Utf8);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static const MetadataQueryCandidate*
    find_candidate(const MetadataQueryResult& result,
                   MetadataQuerySemanticKind semantic)
    {
        for (size_t i = 0U; i < result.candidates.size(); ++i) {
            if (result.candidates[i].semantic == semantic) {
                return &result.candidates[i];
            }
        }
        return nullptr;
    }

    static const MetadataQueryCandidate* find_candidate_with_shape(
        const MetadataQueryResult& result, MetadataQuerySemanticKind semantic,
        MetadataQueryValueShape shape, size_t min_source_entries)
    {
        for (size_t i = 0U; i < result.candidates.size(); ++i) {
            const MetadataQueryCandidate& candidate = result.candidates[i];
            if (candidate.semantic == semantic
                && candidate.normalized_shape == shape
                && candidate.source_entries.size() >= min_source_entries) {
                return &candidate;
            }
        }
        return nullptr;
    }

    static const MetadataQueryMatch*
    find_match_for_entry(const MetadataQueryResult& result, EntryId entry_id)
    {
        for (size_t i = 0U; i < result.matches.size(); ++i) {
            if (result.matches[i].entry_id == entry_id) {
                return &result.matches[i];
            }
        }
        return nullptr;
    }

    static bool contains_entry(const std::vector<EntryId>& entries,
                               EntryId entry_id) noexcept
    {
        for (size_t i = 0U; i < entries.size(); ++i) {
            if (entries[i] == entry_id) {
                return true;
            }
        }
        return false;
    }

    static const MetadataQueryCandidate*
    find_candidate_for_entry(const MetadataQueryResult& result,
                             EntryId entry_id)
    {
        for (size_t i = 0U; i < result.candidates.size(); ++i) {
            if (contains_entry(result.candidates[i].source_entries, entry_id)) {
                return &result.candidates[i];
            }
        }
        return nullptr;
    }

}  // namespace

TEST(MetadataQuery, BuildsDngDefaultCropCandidate)
{
    MetaStore store;
    const std::array<uint32_t, 2> origin = { 12U, 34U };
    const std::array<uint32_t, 2> size   = { 4000U, 3000U };
    const EntryId origin_id
        = add_exif_u32_array(&store, "ifd0", 0xC61FU,
                             std::span<const uint32_t>(origin.data(),
                                                       origin.size()));
    const EntryId size_id
        = add_exif_u32_array(&store, "ifd0", 0xC620U,
                             std::span<const uint32_t>(size.data(),
                                                       size.size()));
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::Crop);
    EXPECT_GE(result.matches.size(), 2U);
    const MetadataQueryMatch* origin_match = find_match_for_entry(result,
                                                                  origin_id);
    ASSERT_NE(origin_match, nullptr);
    EXPECT_TRUE(origin_match->exact_match);
    EXPECT_FALSE(origin_match->fuzzy_match);
    EXPECT_EQ(origin_match->fuzzy_score, 0U);
    const MetadataQueryCandidate* candidate
        = find_candidate(result, MetadataQuerySemanticKind::Crop);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Rect);
    EXPECT_GE(candidate->confidence, 90U);
    ASSERT_TRUE(candidate->has_origin);
    EXPECT_DOUBLE_EQ(candidate->origin[0], 12.0);
    EXPECT_DOUBLE_EQ(candidate->origin[1], 34.0);
    ASSERT_TRUE(candidate->has_size);
    EXPECT_DOUBLE_EQ(candidate->size[0], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->size[1], 3000.0);
    ASSERT_TRUE(candidate->has_rect);
    EXPECT_DOUBLE_EQ(candidate->rect[0], 12.0);
    EXPECT_DOUBLE_EQ(candidate->rect[1], 34.0);
    EXPECT_DOUBLE_EQ(candidate->rect[2], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->rect[3], 3000.0);
    EXPECT_TRUE(contains_entry(candidate->source_entries, origin_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, size_id));
}

TEST(MetadataQuery, ReconcilesDescriptiveExifIptcXmpEntries)
{
    MetaStore store;
    const EntryId exif_title    = add_exif_text(&store, "ifd0", 0x9C9BU,
                                                "Evening frame");
    const EntryId iptc_caption  = add_iptc_text(&store, 2U, 120U,
                                                "Street after rain");
    const EntryId iptc_keywords = add_iptc_text(&store, 2U, 25U,
                                                "night,street");
    const EntryId xmp_creator   = add_xmp_text(&store,
                                               "http://purl.org/dc/elements/1.1/",
                                               "dc:creator", "Alice");
    const EntryId xmp_contact_city
        = add_xmp_text(&store, "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
                       "CreatorContactInfo/CiAdrCity", "Tokyo");
    store.finalize();

    const MetadataQueryResult result = query_descriptive_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::Descriptive);
    EXPECT_STREQ(metadata_query_kind_name(MetadataQueryKind::Descriptive),
                 "descriptive");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::Description),
                 "description");
    const MetadataQueryMatch* title_match = find_match_for_entry(result,
                                                                 exif_title);
    ASSERT_NE(title_match, nullptr);
    EXPECT_EQ(title_match->semantic, MetadataQuerySemanticKind::Title);
    EXPECT_TRUE(title_match->exact_match);

    const MetadataQueryMatch* caption_match
        = find_match_for_entry(result, iptc_caption);
    ASSERT_NE(caption_match, nullptr);
    EXPECT_EQ(caption_match->semantic, MetadataQuerySemanticKind::Description);
    EXPECT_EQ(caption_match->key_kind, MetaKeyKind::IptcDataset);

    const MetadataQueryMatch* keyword_match
        = find_match_for_entry(result, iptc_keywords);
    ASSERT_NE(keyword_match, nullptr);
    EXPECT_EQ(keyword_match->semantic, MetadataQuerySemanticKind::Keywords);

    const MetadataQueryMatch* creator_match = find_match_for_entry(result,
                                                                   xmp_creator);
    ASSERT_NE(creator_match, nullptr);
    EXPECT_EQ(creator_match->semantic, MetadataQuerySemanticKind::Creator);
    EXPECT_EQ(creator_match->key_kind, MetaKeyKind::XmpProperty);

    const MetadataQueryCandidate* creator_candidate
        = find_candidate_for_entry(result, xmp_creator);
    ASSERT_NE(creator_candidate, nullptr);
    EXPECT_EQ(creator_candidate->semantic, MetadataQuerySemanticKind::Creator);
    EXPECT_EQ(creator_candidate->normalized_shape,
              MetadataQueryValueShape::Text);

    const MetadataQueryMatch* contact_match
        = find_match_for_entry(result, xmp_contact_city);
    ASSERT_NE(contact_match, nullptr);
    EXPECT_EQ(contact_match->semantic, MetadataQuerySemanticKind::Contact);
    EXPECT_TRUE(contact_match->exact_match);

    const MetadataQueryResult dispatched
        = query_metadata(store, MetadataQueryKind::Descriptive);
    EXPECT_EQ(dispatched.kind, MetadataQueryKind::Descriptive);
    EXPECT_EQ(dispatched.matches.size(), result.matches.size());
}

TEST(MetadataQuery, ClassifiesStructuredEditorialAndLegalEntries)
{
    MetaStore store;
    const std::string_view ext  = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
    const std::string_view plus = "http://ns.useplus.org/ldf/xmp/1.0/";
    const EntryId event         = add_xmp_text(&store, ext, "Event", "Opening");
    const EntryId person        = add_xmp_text(&store, ext,
                                               "PersonInImageWDetails[1]/PersonName",
                                               "Alex");
    const EntryId organization  = add_xmp_text(&store, ext,
                                               "OrganisationInImageName[1]",
                                               "Example Org");
    const EntryId product       = add_xmp_text(&store, ext,
                                               "ProductInImage[1]/ProductName",
                                               "Example Camera");
    const EntryId artwork       = add_xmp_text(&store, ext,
                                               "ArtworkOrObject[1]/AOTitle",
                                               "Example Artwork");
    const EntryId rights        = add_xmp_text(&store, ext,
                                               "EmbdEncRightsExpr[1]/EncRightsExpr",
                                               "encoded");
    const EntryId license       = add_xmp_text(&store, plus, "MediaConstraints",
                                               "Editorial");
    const EntryId release = add_xmp_text(&store, plus, "ModelReleaseStatus",
                                         "MR-LMR");
    store.finalize();

    const MetadataQueryResult result = query_descriptive_metadata(store);

    const MetadataQueryMatch* event_match = find_match_for_entry(result, event);
    const MetadataQueryMatch* person_match = find_match_for_entry(result,
                                                                  person);
    const MetadataQueryMatch* organization_match
        = find_match_for_entry(result, organization);
    const MetadataQueryMatch* product_match = find_match_for_entry(result,
                                                                   product);
    const MetadataQueryMatch* artwork_match = find_match_for_entry(result,
                                                                   artwork);
    const MetadataQueryMatch* rights_match  = find_match_for_entry(result,
                                                                   rights);
    const MetadataQueryMatch* license_match = find_match_for_entry(result,
                                                                   license);
    const MetadataQueryMatch* release_match = find_match_for_entry(result,
                                                                   release);
    ASSERT_NE(event_match, nullptr);
    ASSERT_NE(person_match, nullptr);
    ASSERT_NE(organization_match, nullptr);
    ASSERT_NE(product_match, nullptr);
    ASSERT_NE(artwork_match, nullptr);
    ASSERT_NE(rights_match, nullptr);
    ASSERT_NE(license_match, nullptr);
    ASSERT_NE(release_match, nullptr);
    EXPECT_EQ(event_match->semantic, MetadataQuerySemanticKind::Event);
    EXPECT_EQ(person_match->semantic, MetadataQuerySemanticKind::Person);
    EXPECT_EQ(organization_match->semantic,
              MetadataQuerySemanticKind::Organization);
    EXPECT_EQ(product_match->semantic, MetadataQuerySemanticKind::Product);
    EXPECT_EQ(artwork_match->semantic, MetadataQuerySemanticKind::Artwork);
    EXPECT_EQ(rights_match->semantic,
              MetadataQuerySemanticKind::RightsExpression);
    EXPECT_EQ(license_match->semantic, MetadataQuerySemanticKind::License);
    EXPECT_EQ(release_match->semantic, MetadataQuerySemanticKind::Release);
}

TEST(MetadataQuery, ClassifiesEditorialTaxonomyIdentityAndPlusTail)
{
    MetaStore store;
    const std::string_view photoshop = "http://ns.adobe.com/photoshop/1.0/";
    const std::string_view core = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
    const std::string_view plus = "http://ns.useplus.org/ldf/xmp/1.0/";
    const EntryId urgency       = add_iptc_text(&store, 2U, 10U, "5");
    const EntryId instructions = add_xmp_text(&store, photoshop, "Instructions",
                                              "Do not crop");
    const EntryId creator_title  = add_iptc_text(&store, 2U, 85U,
                                                 "Staff Photographer");
    const EntryId transmission   = add_iptc_text(&store, 2U, 103U, "job-042");
    const EntryId caption_writer = add_iptc_text(&store, 2U, 122U,
                                                 "Editor Example");
    const EntryId accessibility
        = add_xmp_text(&store, core,
                       "AltTextAccessibility[@xml:lang=x-default]",
                       "Person crossing a street");
    const EntryId taxonomy = add_xmp_text(&store, core, "Scene[1]", "010100");
    const EntryId document = add_xmp_text(&store,
                                          "http://ns.adobe.com/xap/1.0/mm/",
                                          "DocumentID", "xmp.did:document");
    const EntryId resource = add_xmp_text(&store,
                                          "http://ns.adobe.com/xap/1.0/",
                                          "Identifier[1]", "asset-001");
    const EntryId end_user = add_xmp_text(&store, plus,
                                          "EndUser[1]/EndUserName",
                                          "Example Publisher");
    const EntryId image_creator
        = add_xmp_text(&store, plus, "ImageCreator[1]/ImageCreatorName",
                       "Alex Example");
    const EntryId supplier  = add_xmp_text(&store, plus, "ImageSupplierName",
                                           "Example Agency");
    const EntryId delivered = add_xmp_text(&store, plus, "FileNameAsDelivered",
                                           "asset.tif");
    const EntryId policy  = add_xmp_text(&store, plus, "DataMining", "DMI-PRO");
    const EntryId release = add_xmp_text(&store, plus,
                                         "MinorModelAgeDisclosure", "AG-A18");
    const EntryId registration
        = add_xmp_text(&store, plus, "CopyrightRegistrationNumber", "REG-001");
    const EntryId publication
        = add_xmp_text(&store, plus, "FirstPublicationDate", "2026-07-01");
    store.finalize();

    const MetadataQueryResult result        = query_descriptive_metadata(store);
    const MetadataQueryMatch* urgency_match = find_match_for_entry(result,
                                                                   urgency);
    const MetadataQueryMatch* instructions_match
        = find_match_for_entry(result, instructions);
    const MetadataQueryMatch* creator_title_match
        = find_match_for_entry(result, creator_title);
    const MetadataQueryMatch* transmission_match
        = find_match_for_entry(result, transmission);
    const MetadataQueryMatch* caption_writer_match
        = find_match_for_entry(result, caption_writer);
    const MetadataQueryMatch* accessibility_match
        = find_match_for_entry(result, accessibility);
    const MetadataQueryMatch* taxonomy_match = find_match_for_entry(result,
                                                                    taxonomy);
    const MetadataQueryMatch* document_match = find_match_for_entry(result,
                                                                    document);
    const MetadataQueryMatch* resource_match = find_match_for_entry(result,
                                                                    resource);
    const MetadataQueryMatch* end_user_match = find_match_for_entry(result,
                                                                    end_user);
    const MetadataQueryMatch* image_creator_match
        = find_match_for_entry(result, image_creator);
    const MetadataQueryMatch* supplier_match  = find_match_for_entry(result,
                                                                     supplier);
    const MetadataQueryMatch* delivered_match = find_match_for_entry(result,
                                                                     delivered);
    const MetadataQueryMatch* policy_match    = find_match_for_entry(result,
                                                                     policy);
    const MetadataQueryMatch* release_match   = find_match_for_entry(result,
                                                                     release);
    const MetadataQueryMatch* registration_match
        = find_match_for_entry(result, registration);
    const MetadataQueryMatch* publication_match
        = find_match_for_entry(result, publication);
    ASSERT_NE(urgency_match, nullptr);
    ASSERT_NE(instructions_match, nullptr);
    ASSERT_NE(creator_title_match, nullptr);
    ASSERT_NE(transmission_match, nullptr);
    ASSERT_NE(caption_writer_match, nullptr);
    ASSERT_NE(accessibility_match, nullptr);
    ASSERT_NE(taxonomy_match, nullptr);
    ASSERT_NE(document_match, nullptr);
    ASSERT_NE(resource_match, nullptr);
    ASSERT_NE(end_user_match, nullptr);
    ASSERT_NE(image_creator_match, nullptr);
    ASSERT_NE(supplier_match, nullptr);
    ASSERT_NE(delivered_match, nullptr);
    ASSERT_NE(policy_match, nullptr);
    ASSERT_NE(release_match, nullptr);
    ASSERT_NE(registration_match, nullptr);
    ASSERT_NE(publication_match, nullptr);
    EXPECT_EQ(urgency_match->semantic, MetadataQuerySemanticKind::Editorial);
    EXPECT_EQ(instructions_match->semantic,
              MetadataQuerySemanticKind::Editorial);
    EXPECT_EQ(creator_title_match->semantic,
              MetadataQuerySemanticKind::Creator);
    EXPECT_EQ(transmission_match->semantic,
              MetadataQuerySemanticKind::Editorial);
    EXPECT_EQ(caption_writer_match->semantic,
              MetadataQuerySemanticKind::Creator);
    EXPECT_EQ(accessibility_match->semantic,
              MetadataQuerySemanticKind::Accessibility);
    EXPECT_EQ(taxonomy_match->semantic, MetadataQuerySemanticKind::Taxonomy);
    EXPECT_EQ(document_match->semantic,
              MetadataQuerySemanticKind::DocumentIdentity);
    EXPECT_EQ(resource_match->semantic,
              MetadataQuerySemanticKind::DocumentIdentity);
    EXPECT_EQ(end_user_match->semantic, MetadataQuerySemanticKind::License);
    EXPECT_EQ(image_creator_match->semantic,
              MetadataQuerySemanticKind::Creator);
    EXPECT_EQ(supplier_match->semantic, MetadataQuerySemanticKind::Source);
    EXPECT_EQ(delivered_match->semantic,
              MetadataQuerySemanticKind::DocumentIdentity);
    EXPECT_EQ(policy_match->semantic, MetadataQuerySemanticKind::License);
    EXPECT_EQ(release_match->semantic, MetadataQuerySemanticKind::Release);
    EXPECT_EQ(registration_match->semantic, MetadataQuerySemanticKind::Rights);
    EXPECT_EQ(publication_match->semantic, MetadataQuerySemanticKind::Rights);
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::DocumentIdentity),
                 "document_identity");
}

TEST(MetadataQuery, ClassifiesLegacyIptcWorkflowTail)
{
    MetaStore store;
    const EntryId object_type   = add_iptc_text(&store, 2U, 3U, "01:news");
    const EntryId edit_status   = add_iptc_text(&store, 2U, 7U, "edited");
    const EntryId prior_service = add_iptc_text(&store, 2U, 45U, "NEWS");
    const EntryId program       = add_iptc_text(&store, 2U, 65U, "PhotoDesk");
    const EntryId contact       = add_iptc_text(&store, 2U, 118U,
                                                "desk@example.test");
    store.finalize();

    const MetadataQueryResult result = query_descriptive_metadata(store);
    const MetadataQueryMatch* object_type_match
        = find_match_for_entry(result, object_type);
    const MetadataQueryMatch* edit_status_match
        = find_match_for_entry(result, edit_status);
    const MetadataQueryMatch* prior_service_match
        = find_match_for_entry(result, prior_service);
    const MetadataQueryMatch* program_match = find_match_for_entry(result,
                                                                   program);
    const MetadataQueryMatch* contact_match = find_match_for_entry(result,
                                                                   contact);

    ASSERT_NE(object_type_match, nullptr);
    ASSERT_NE(edit_status_match, nullptr);
    ASSERT_NE(prior_service_match, nullptr);
    ASSERT_NE(program_match, nullptr);
    ASSERT_NE(contact_match, nullptr);
    EXPECT_EQ(object_type_match->semantic, MetadataQuerySemanticKind::Taxonomy);
    EXPECT_EQ(edit_status_match->semantic,
              MetadataQuerySemanticKind::Editorial);
    EXPECT_EQ(prior_service_match->semantic,
              MetadataQuerySemanticKind::DocumentLineage);
    EXPECT_EQ(program_match->semantic, MetadataQuerySemanticKind::Source);
    EXPECT_EQ(contact_match->semantic, MetadataQuerySemanticKind::Contact);
}

TEST(MetadataQuery, ClassifiesRightsLicenseCreditAndSourceEntries)
{
    MetaStore store;
    const EntryId exif_rights = add_exif_text(&store, "ifd0", 0x8298U,
                                              "Copyright Example");
    const EntryId iptc_credit = add_iptc_text(&store, 2U, 110U,
                                              "Example Studio");
    const EntryId iptc_source = add_iptc_text(&store, 2U, 115U,
                                              "Example Archive");
    const EntryId xmp_terms
        = add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/rights/",
                       "xmpRights:UsageTerms[@xml:lang=x-default]",
                       "Editorial use only");
    const EntryId plus_licensor
        = add_xmp_text(&store, "http://ns.useplus.org/ldf/xmp/1.0/",
                       "Licensor[1]/LicensorName", "Example Agency");
    const EntryId digital_source
        = add_xmp_text(&store, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                       "DigitalSourceType", "digitalCapture");
    store.finalize();

    const MetadataQueryResult result = query_descriptive_metadata(store);

    const MetadataQueryMatch* rights = find_match_for_entry(result,
                                                            exif_rights);
    const MetadataQueryMatch* credit = find_match_for_entry(result,
                                                            iptc_credit);
    const MetadataQueryMatch* source = find_match_for_entry(result,
                                                            iptc_source);
    const MetadataQueryMatch* terms  = find_match_for_entry(result, xmp_terms);
    const MetadataQueryMatch* licensor = find_match_for_entry(result,
                                                              plus_licensor);
    const MetadataQueryMatch* source_type
        = find_match_for_entry(result, digital_source);
    ASSERT_NE(rights, nullptr);
    ASSERT_NE(credit, nullptr);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(terms, nullptr);
    ASSERT_NE(licensor, nullptr);
    ASSERT_NE(source_type, nullptr);
    EXPECT_EQ(rights->semantic, MetadataQuerySemanticKind::Rights);
    EXPECT_EQ(credit->semantic, MetadataQuerySemanticKind::Credit);
    EXPECT_EQ(source->semantic, MetadataQuerySemanticKind::Source);
    EXPECT_EQ(terms->semantic, MetadataQuerySemanticKind::License);
    EXPECT_EQ(licensor->semantic, MetadataQuerySemanticKind::License);
    EXPECT_EQ(source_type->semantic, MetadataQuerySemanticKind::Source);
    EXPECT_TRUE(rights->exact_match);
    EXPECT_EQ(rights->matched_terms, 0U);
    EXPECT_GE(rights->confidence, 90U);
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::License),
                 "license");
}

TEST(MetadataQuery, DoesNotPairDngCropAcrossIfds)
{
    MetaStore store;
    const std::array<uint32_t, 2> origin = { 12U, 34U };
    const std::array<uint32_t, 2> size   = { 4000U, 3000U };
    add_exif_u32_array(&store, "ifd0", 0xC61FU,
                       std::span<const uint32_t>(origin.data(), origin.size()));
    add_exif_u32_array(&store, "subifd0", 0xC620U,
                       std::span<const uint32_t>(size.data(), size.size()));
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    EXPECT_EQ(find_candidate(result, MetadataQuerySemanticKind::Crop), nullptr);
}

TEST(MetadataQuery, NormalizesActiveAreaCandidate)
{
    MetaStore store;
    const std::array<uint32_t, 4> active_area = {
        10U,
        20U,
        3010U,
        4020U,
    };
    const EntryId active_id
        = add_exif_u32_array(&store, "ifd0", 0xC68DU,
                             std::span<const uint32_t>(active_area.data(),
                                                       active_area.size()));
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate(result, MetadataQuerySemanticKind::ActiveArea);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Rect);
    EXPECT_GE(candidate->confidence, 90U);
    ASSERT_TRUE(candidate->has_origin);
    EXPECT_DOUBLE_EQ(candidate->origin[0], 20.0);
    EXPECT_DOUBLE_EQ(candidate->origin[1], 10.0);
    ASSERT_TRUE(candidate->has_size);
    EXPECT_DOUBLE_EQ(candidate->size[0], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->size[1], 3000.0);
    ASSERT_TRUE(candidate->has_rect);
    EXPECT_DOUBLE_EQ(candidate->rect[0], 20.0);
    EXPECT_DOUBLE_EQ(candidate->rect[1], 10.0);
    EXPECT_DOUBLE_EQ(candidate->rect[2], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->rect[3], 3000.0);
    EXPECT_TRUE(contains_entry(candidate->source_entries, active_id));
}

TEST(MetadataQuery, NormalizesDngMaskedAreasCandidate)
{
    MetaStore store;
    const std::array<uint32_t, 4> masked_area = {
        0U,
        0U,
        24U,
        4000U,
    };
    const EntryId masked_id
        = add_exif_u32_array(&store, "ifd0", 0xC68EU,
                             std::span<const uint32_t>(masked_area.data(),
                                                       masked_area.size()));
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result, MetadataQuerySemanticKind::Border,
                                    MetadataQueryValueShape::Table, 1U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_GE(candidate->confidence, 90U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, masked_id));
    ASSERT_TRUE(candidate->has_rect);
    EXPECT_DOUBLE_EQ(candidate->rect[0], 0.0);
    EXPECT_DOUBLE_EQ(candidate->rect[1], 0.0);
    EXPECT_DOUBLE_EQ(candidate->rect[2], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->rect[3], 24.0);
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 4U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 0.0);
    EXPECT_DOUBLE_EQ(candidate->values[1], 0.0);
    EXPECT_DOUBLE_EQ(candidate->values[2], 24.0);
    EXPECT_DOUBLE_EQ(candidate->values[3], 4000.0);
}

TEST(MetadataQuery, NormalizesPhaseOneRawGeometryCandidate)
{
    MetaStore store;
    add_exif_u32(&store, "mk_phaseone0", 0x0108U, 10560U);
    add_exif_u32(&store, "mk_phaseone0", 0x0109U, 7920U);
    add_exif_u32(&store, "mk_phaseone0", 0x010AU, 64U);
    add_exif_u32(&store, "mk_phaseone0", 0x010BU, 32U);
    add_exif_u32(&store, "mk_phaseone0", 0x010CU, 10328U);
    add_exif_u32(&store, "mk_phaseone0", 0x010DU, 7760U);
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate(result, MetadataQuerySemanticKind::ActiveArea);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Rect);
    EXPECT_EQ(candidate->confidence, 96U);
    EXPECT_EQ(candidate->source_entries.size(), 6U);
    ASSERT_TRUE(candidate->has_origin);
    EXPECT_DOUBLE_EQ(candidate->origin[0], 64.0);
    EXPECT_DOUBLE_EQ(candidate->origin[1], 32.0);
    ASSERT_TRUE(candidate->has_size);
    EXPECT_DOUBLE_EQ(candidate->size[0], 10328.0);
    EXPECT_DOUBLE_EQ(candidate->size[1], 7760.0);
    ASSERT_TRUE(candidate->has_rect);
    EXPECT_DOUBLE_EQ(candidate->rect[0], 64.0);
    EXPECT_DOUBLE_EQ(candidate->rect[1], 32.0);
    EXPECT_DOUBLE_EQ(candidate->rect[2], 10328.0);
    EXPECT_DOUBLE_EQ(candidate->rect[3], 7760.0);
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 4U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 64.0);
    EXPECT_DOUBLE_EQ(candidate->values[1], 32.0);
    EXPECT_DOUBLE_EQ(candidate->values[2], 168.0);
    EXPECT_DOUBLE_EQ(candidate->values[3], 128.0);
    ASSERT_TRUE(candidate->has_margins);
    EXPECT_DOUBLE_EQ(candidate->margins[0], 64.0);
    EXPECT_DOUBLE_EQ(candidate->margins[1], 32.0);
    EXPECT_DOUBLE_EQ(candidate->margins[2], 168.0);
    EXPECT_DOUBLE_EQ(candidate->margins[3], 128.0);
}

TEST(MetadataQuery, NormalizesFujifilmRafRawCropCandidate)
{
    MetaStore store;
    const std::array<uint32_t, 2> full_size = { 4032U, 3024U };
    const std::array<uint32_t, 2> top_left  = { 16U, 8U };
    const std::array<uint32_t, 2> crop_size = { 4000U, 3000U };
    const EntryId full_id
        = add_exif_u32_array(&store, "raf_0", 0x0100U,
                             std::span<const uint32_t>(full_size.data(),
                                                       full_size.size()));
    const EntryId top_left_id
        = add_exif_u32_array(&store, "raf_0", 0x0110U,
                             std::span<const uint32_t>(top_left.data(),
                                                       top_left.size()));
    const EntryId size_id
        = add_exif_u32_array(&store, "raf_0", 0x0111U,
                             std::span<const uint32_t>(crop_size.data(),
                                                       crop_size.size()));
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate(result, MetadataQuerySemanticKind::ActiveArea);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Rect);
    EXPECT_EQ(candidate->confidence, 94U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, full_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, top_left_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, size_id));
    ASSERT_TRUE(candidate->has_origin);
    EXPECT_DOUBLE_EQ(candidate->origin[0], 16.0);
    EXPECT_DOUBLE_EQ(candidate->origin[1], 8.0);
    ASSERT_TRUE(candidate->has_size);
    EXPECT_DOUBLE_EQ(candidate->size[0], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->size[1], 3000.0);
    ASSERT_TRUE(candidate->has_rect);
    EXPECT_DOUBLE_EQ(candidate->rect[0], 16.0);
    EXPECT_DOUBLE_EQ(candidate->rect[1], 8.0);
    EXPECT_DOUBLE_EQ(candidate->rect[2], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->rect[3], 3000.0);
    ASSERT_TRUE(candidate->has_margins);
    EXPECT_DOUBLE_EQ(candidate->margins[0], 16.0);
    EXPECT_DOUBLE_EQ(candidate->margins[1], 8.0);
    EXPECT_DOUBLE_EQ(candidate->margins[2], 16.0);
    EXPECT_DOUBLE_EQ(candidate->margins[3], 16.0);
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 6U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 16.0);
    EXPECT_DOUBLE_EQ(candidate->values[2], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->values[4], 4032.0);
}

TEST(MetadataQuery, NormalizesFujifilmRafRawZoomCandidate)
{
    MetaStore store;
    const std::array<uint32_t, 2> full_size = { 4032U, 3024U };
    const std::array<uint32_t, 2> top_left  = { 64U, 48U };
    const std::array<uint32_t, 2> crop_size = { 3000U, 2000U };
    const EntryId full_id
        = add_exif_u32_array(&store, "raf_0", 0x0100U,
                             std::span<const uint32_t>(full_size.data(),
                                                       full_size.size()));
    const EntryId top_left_id
        = add_exif_u32_array(&store, "raf_0", 0x0118U,
                             std::span<const uint32_t>(top_left.data(),
                                                       top_left.size()));
    const EntryId size_id
        = add_exif_u32_array(&store, "raf_0", 0x0119U,
                             std::span<const uint32_t>(crop_size.data(),
                                                       crop_size.size()));
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate(result, MetadataQuerySemanticKind::Crop);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Rect);
    EXPECT_EQ(candidate->confidence, 88U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, full_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, top_left_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, size_id));
    ASSERT_TRUE(candidate->has_rect);
    EXPECT_DOUBLE_EQ(candidate->rect[0], 64.0);
    EXPECT_DOUBLE_EQ(candidate->rect[1], 48.0);
    EXPECT_DOUBLE_EQ(candidate->rect[2], 3000.0);
    EXPECT_DOUBLE_EQ(candidate->rect[3], 2000.0);
    ASSERT_TRUE(candidate->has_margins);
    EXPECT_DOUBLE_EQ(candidate->margins[2], 968.0);
    EXPECT_DOUBLE_EQ(candidate->margins[3], 976.0);
}

TEST(MetadataQuery, NormalizesCanonAspectInfoCropCandidate)
{
    MetaStore store;
    const EntryId width_id  = add_exif_u32(&store, "mk_canon_aspectinfo_0",
                                           0x0001U, 4000U);
    const EntryId height_id = add_exif_u32(&store, "mk_canon_aspectinfo_0",
                                           0x0002U, 3000U);
    const EntryId left_id   = add_exif_u32(&store, "mk_canon_aspectinfo_0",
                                           0x0003U, 12U);
    const EntryId top_id    = add_exif_u32(&store, "mk_canon_aspectinfo_0",
                                           0x0004U, 8U);
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate(result, MetadataQuerySemanticKind::Crop);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Rect);
    EXPECT_EQ(candidate->confidence, 91U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, width_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, height_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, left_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, top_id));
    ASSERT_TRUE(candidate->has_origin);
    EXPECT_DOUBLE_EQ(candidate->origin[0], 12.0);
    EXPECT_DOUBLE_EQ(candidate->origin[1], 8.0);
    ASSERT_TRUE(candidate->has_size);
    EXPECT_DOUBLE_EQ(candidate->size[0], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->size[1], 3000.0);
    ASSERT_TRUE(candidate->has_rect);
    EXPECT_DOUBLE_EQ(candidate->rect[0], 12.0);
    EXPECT_DOUBLE_EQ(candidate->rect[1], 8.0);
    EXPECT_DOUBLE_EQ(candidate->rect[2], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->rect[3], 3000.0);
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 4U);
}

TEST(MetadataQuery, NormalizesCanonCropInfoMargins)
{
    MetaStore store;
    const EntryId left_id = add_exif_u32(&store, "mk_canon_cropinfo_0", 0x0000U,
                                         16U);
    const EntryId right_id = add_exif_u32(&store, "mk_canon_cropinfo_0",
                                          0x0001U, 20U);
    const EntryId top_id = add_exif_u32(&store, "mk_canon_cropinfo_0", 0x0002U,
                                        4U);
    const EntryId bottom_id = add_exif_u32(&store, "mk_canon_cropinfo_0",
                                           0x0003U, 6U);
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result, MetadataQuerySemanticKind::Border,
                                    MetadataQueryValueShape::Vec4, 4U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->confidence, 90U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, left_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, right_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, top_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, bottom_id));
    ASSERT_TRUE(candidate->has_margins);
    EXPECT_DOUBLE_EQ(candidate->margins[0], 16.0);
    EXPECT_DOUBLE_EQ(candidate->margins[1], 4.0);
    EXPECT_DOUBLE_EQ(candidate->margins[2], 20.0);
    EXPECT_DOUBLE_EQ(candidate->margins[3], 6.0);
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 4U);
}

TEST(MetadataQuery, NormalizesNikonCaptureCropData)
{
    MetaStore store;
    const EntryId left_id   = add_exif_u32(&store, "mk_nikoncapture_cropdata_0",
                                           0x001EU, 10U);
    const EntryId top_id    = add_exif_u32(&store, "mk_nikoncapture_cropdata_0",
                                           0x0026U, 20U);
    const EntryId right_id  = add_exif_u32(&store, "mk_nikoncapture_cropdata_0",
                                           0x002EU, 4010U);
    const EntryId bottom_id = add_exif_u32(&store, "mk_nikoncapture_cropdata_0",
                                           0x0036U, 3020U);
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate(result, MetadataQuerySemanticKind::Crop);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Rect);
    EXPECT_EQ(candidate->confidence, 88U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, left_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, top_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, right_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, bottom_id));
    ASSERT_TRUE(candidate->has_rect);
    EXPECT_DOUBLE_EQ(candidate->rect[0], 10.0);
    EXPECT_DOUBLE_EQ(candidate->rect[1], 20.0);
    EXPECT_DOUBLE_EQ(candidate->rect[2], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->rect[3], 3000.0);
}

TEST(MetadataQuery, NormalizesSonyPanoramaCropMargins)
{
    MetaStore store;
    const EntryId left_id  = add_exif_u32(&store, "mk_sony_panorama_0", 0x0004U,
                                          100U);
    const EntryId top_id   = add_exif_u32(&store, "mk_sony_panorama_0", 0x0005U,
                                          20U);
    const EntryId right_id = add_exif_u32(&store, "mk_sony_panorama_0", 0x0006U,
                                          120U);
    const EntryId bottom_id = add_exif_u32(&store, "mk_sony_panorama_0",
                                           0x0007U, 30U);
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result, MetadataQuerySemanticKind::Border,
                                    MetadataQueryValueShape::Vec4, 4U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->confidence, 87U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, left_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, top_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, right_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, bottom_id));
    ASSERT_TRUE(candidate->has_margins);
    EXPECT_DOUBLE_EQ(candidate->margins[0], 100.0);
    EXPECT_DOUBLE_EQ(candidate->margins[1], 20.0);
    EXPECT_DOUBLE_EQ(candidate->margins[2], 120.0);
    EXPECT_DOUBLE_EQ(candidate->margins[3], 30.0);
}

TEST(MetadataQuery, MatchesFuzzyXmpCropPath)
{
    MetaStore store;
    const EntryId entry_id
        = add_xmp_text(&store, "http://example.invalid/aux/1.0/",
                       "aux:SensorBorderPadding", "64 32 168 128");
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryMatch* match = find_match_for_entry(result, entry_id);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->key_kind, MetaKeyKind::XmpProperty);
    EXPECT_EQ(match->semantic, MetadataQuerySemanticKind::Border);
    EXPECT_EQ(match->shape, MetadataQueryValueShape::Text);
    EXPECT_GE(match->confidence, 70U);
    EXPECT_TRUE(match->exact_match);
    EXPECT_FALSE(match->fuzzy_match);
    EXPECT_EQ(match->fuzzy_score, 0U);
    EXPECT_NE((match->matched_terms
               & static_cast<uint32_t>(MetadataQueryMatchTerm::Border)),
              0U);
    EXPECT_NE((match->matched_terms
               & static_cast<uint32_t>(MetadataQueryMatchTerm::Padding)),
              0U);
    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result, MetadataQuerySemanticKind::Border,
                                    MetadataQueryValueShape::Vec4, 1U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_TRUE(contains_entry(candidate->source_entries, entry_id));
    ASSERT_TRUE(candidate->has_margins);
    EXPECT_DOUBLE_EQ(candidate->margins[0], 64.0);
    EXPECT_DOUBLE_EQ(candidate->margins[1], 32.0);
    EXPECT_DOUBLE_EQ(candidate->margins[2], 168.0);
    EXPECT_DOUBLE_EQ(candidate->margins[3], 128.0);
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 4U);
}

TEST(MetadataQuery, ReportsRapidFuzzAvailability)
{
#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
    EXPECT_TRUE(metadata_query_fuzzy_search_available());
#else
    EXPECT_FALSE(metadata_query_fuzzy_search_available());
#endif
}

TEST(MetadataQuery, DoesNotFuzzyClassifyMisspelledXmpCropPath)
{
    MetaStore store;
    const EntryId entry_id
        = add_xmp_text(&store, "http://example.invalid/aux/1.0/",
                       "aux:SensrBordrPading", "64 32 168 128");
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryMatch* match = find_match_for_entry(result, entry_id);
    EXPECT_EQ(match, nullptr);
}

TEST(MetadataQuery, IgnoresDeletedEntries)
{
    MetaStore store;
    const std::array<uint32_t, 2> origin = { 12U, 34U };
    const std::array<uint32_t, 2> size   = { 4000U, 3000U };
    add_exif_u32_array(&store, "ifd0", 0xC61FU,
                       std::span<const uint32_t>(origin.data(), origin.size()),
                       EntryFlags::Deleted);
    add_exif_u32_array(&store, "ifd0", 0xC620U,
                       std::span<const uint32_t>(size.data(), size.size()));
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    EXPECT_EQ(find_candidate(result, MetadataQuerySemanticKind::Crop), nullptr);
    EXPECT_EQ(result.matches.size(), 1U);
}

TEST(MetadataQuery, QueryMetadataDispatchesCrop)
{
    MetaStore store;
    const std::array<uint32_t, 4> active_area = {
        10U,
        20U,
        3010U,
        4020U,
    };
    add_exif_u32_array(&store, "ifd0", 0xC68DU,
                       std::span<const uint32_t>(active_area.data(),
                                                 active_area.size()));
    store.finalize();

    const MetadataQueryResult result = query_metadata(store,
                                                      MetadataQueryKind::Crop);

    EXPECT_EQ(result.kind, MetadataQueryKind::Crop);
    EXPECT_NE(find_candidate(result, MetadataQuerySemanticKind::ActiveArea),
              nullptr);
    EXPECT_STREQ(metadata_query_kind_name(result.kind), "crop");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::ActiveArea),
                 "active_area");
    EXPECT_STREQ(metadata_query_value_shape_name(MetadataQueryValueShape::Rect),
                 "rect");
}

TEST(MetadataQuery, MatchesStandardExposureAndGain)
{
    MetaStore store;
    const EntryId exposure_id = add_exif_urational(&store, "exififd", 0x829AU,
                                                   1U, 125U);
    const EntryId gain_id     = add_exif_u32(&store, "exififd", 0xA407U, 2U);
    store.finalize();

    const MetadataQueryResult result = query_exposure_gain_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::ExposureGain);
    const MetadataQueryMatch* exposure_match
        = find_match_for_entry(result, exposure_id);
    ASSERT_NE(exposure_match, nullptr);
    EXPECT_EQ(exposure_match->semantic, MetadataQuerySemanticKind::Exposure);
    EXPECT_EQ(exposure_match->shape, MetadataQueryValueShape::Scalar);
    const MetadataQueryCandidate* exposure_candidate
        = find_candidate_for_entry(result, exposure_id);
    ASSERT_NE(exposure_candidate, nullptr);
    ASSERT_TRUE(exposure_candidate->has_values);
    ASSERT_EQ(exposure_candidate->values.size(), 1U);
    EXPECT_DOUBLE_EQ(exposure_candidate->values[0], 0.008);

    const MetadataQueryMatch* gain_match = find_match_for_entry(result,
                                                                gain_id);
    ASSERT_NE(gain_match, nullptr);
    EXPECT_EQ(gain_match->semantic, MetadataQuerySemanticKind::Gain);
    const MetadataQueryCandidate* gain_candidate
        = find_candidate_for_entry(result, gain_id);
    ASSERT_NE(gain_candidate, nullptr);
    ASSERT_TRUE(gain_candidate->has_values);
    ASSERT_EQ(gain_candidate->values.size(), 1U);
    EXPECT_DOUBLE_EQ(gain_candidate->values[0], 2.0);
}

TEST(MetadataQuery, GroupsDngExposureGainTable)
{
    MetaStore store;
    const EntryId baseline_id     = add_exif_u32(&store, "ifd0", 0xC62AU, 1U);
    const EntryId preview_gain_id = add_exif_u32(&store, "ifd0", 0xC7A8U, 2U);
    store.finalize();

    const MetadataQueryResult result = query_exposure_gain_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::ExposureGain,
                                    MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_GE(candidate->confidence, 90U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, baseline_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, preview_gain_id));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 2U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[1], 2.0);
}

TEST(MetadataQuery, MatchesXmpWhiteBalance)
{
    MetaStore store;
    const EntryId entry_id
        = add_xmp_text(&store, "http://ns.adobe.com/camera-raw-settings/1.0/",
                       "crs:WhiteBalance", "As Shot");
    store.finalize();

    const MetadataQueryResult result = query_white_balance_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::WhiteBalance);
    const MetadataQueryMatch* match = find_match_for_entry(result, entry_id);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->semantic, MetadataQuerySemanticKind::WhiteBalance);
    EXPECT_EQ(match->shape, MetadataQueryValueShape::Text);
    EXPECT_GE(match->confidence, 90U);
    EXPECT_NE((match->matched_terms
               & static_cast<uint32_t>(MetadataQueryMatchTerm::WhiteBalance)),
              0U);
    const MetadataQueryCandidate* candidate
        = find_candidate_for_entry(result, entry_id);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->semantic, MetadataQuerySemanticKind::WhiteBalance);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Text);
    EXPECT_FALSE(candidate->has_values);
}

TEST(MetadataQuery, TreatsCameraRawSettingsAsSourceProcessing)
{
    MetaStore store;
    const EntryId saturation
        = add_xmp_text(&store, "http://ns.adobe.com/camera-raw-settings/1.0/",
                       "Saturation", "+12");
    const EntryId vendor_shadow
        = add_xmp_text(&store, "urn:vendor:camera-raw-settings-shadow",
                       "Saturation", "+20");
    store.finalize();

    const MetadataQueryResult result = query_raw_processing_metadata(store);
    const MetadataQueryMatch* match  = find_match_for_entry(result, saturation);
    ASSERT_NE(match, nullptr);
    EXPECT_TRUE(match->exact_match);
    EXPECT_EQ(match->semantic, MetadataQuerySemanticKind::SourceProcessing);
    EXPECT_NE((match->matched_terms
               & static_cast<uint32_t>(
                   MetadataQueryMatchTerm::SourceProcessing)),
              0U);
    EXPECT_EQ(find_match_for_entry(result, vendor_shadow), nullptr);
}

TEST(MetadataQuery, MatchesDngColorMatrix)
{
    MetaStore store;
    const std::array<uint32_t, 9> matrix = {
        1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
    };
    const EntryId matrix_id
        = add_exif_u32_array(&store, "ifd0", 0xC621U,
                             std::span<const uint32_t>(matrix.data(),
                                                       matrix.size()));
    store.finalize();

    const MetadataQueryResult result = query_color_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::Color);
    const MetadataQueryMatch* match = find_match_for_entry(result, matrix_id);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->semantic, MetadataQuerySemanticKind::ColorMatrix);
    EXPECT_EQ(match->shape, MetadataQueryValueShape::Matrix3x3);
    EXPECT_GE(match->confidence, 90U);
    const MetadataQueryCandidate* candidate
        = find_candidate_for_entry(result, matrix_id);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Matrix3x3);
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 9U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[4], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[8], 1.0);
}

TEST(MetadataQuery, MatchesColorProfileCarriers)
{
    MetaStore store;
    const EntryId exif_color_space = add_exif_u32(&store, "exififd", 0xA001U,
                                                  1U);
    const EntryId xmp_icc
        = add_xmp_text(&store, "http://ns.adobe.com/photoshop/1.0/",
                       "photoshop:ICCProfile", "sRGB IEC61966-2.1");
    const EntryId icc_header = add_icc_header_u32(&store, 16U, 0x52474220U);
    const std::array<std::byte, 4> desc_bytes = {
        std::byte { 0x64U },
        std::byte { 0x65U },
        std::byte { 0x73U },
        std::byte { 0x63U },
    };
    const EntryId icc_tag
        = add_icc_tag_bytes(&store, 0x64657363U,
                            std::span<const std::byte>(desc_bytes.data(),
                                                       desc_bytes.size()));
    const EntryId png_iccp = add_png_text(&store, "iCCP", "profile_name",
                                          "sRGB");
    store.finalize();

    const MetadataQueryResult result = query_color_metadata(store);

    const MetadataQueryMatch* exif_match
        = find_match_for_entry(result, exif_color_space);
    ASSERT_NE(exif_match, nullptr);
    EXPECT_EQ(exif_match->semantic, MetadataQuerySemanticKind::ColorProfile);
    EXPECT_EQ(exif_match->shape, MetadataQueryValueShape::Scalar);
    EXPECT_TRUE(exif_match->exact_match);
    EXPECT_NE((exif_match->matched_terms
               & static_cast<uint32_t>(MetadataQueryMatchTerm::Profile)),
              0U);
    const MetadataQueryCandidate* exif_candidate
        = find_candidate_for_entry(result, exif_color_space);
    ASSERT_NE(exif_candidate, nullptr);
    EXPECT_EQ(exif_candidate->semantic,
              MetadataQuerySemanticKind::ColorProfile);
    EXPECT_EQ(exif_candidate->normalized_shape,
              MetadataQueryValueShape::Scalar);
    ASSERT_TRUE(exif_candidate->has_values);
    ASSERT_EQ(exif_candidate->values.size(), 1U);
    EXPECT_DOUBLE_EQ(exif_candidate->values[0], 1.0);

    const MetadataQueryMatch* xmp_match = find_match_for_entry(result, xmp_icc);
    ASSERT_NE(xmp_match, nullptr);
    EXPECT_EQ(xmp_match->semantic, MetadataQuerySemanticKind::ColorProfile);
    EXPECT_EQ(xmp_match->shape, MetadataQueryValueShape::Text);
    EXPECT_TRUE(xmp_match->exact_match);

    const MetadataQueryMatch* icc_header_match
        = find_match_for_entry(result, icc_header);
    ASSERT_NE(icc_header_match, nullptr);
    EXPECT_EQ(icc_header_match->semantic,
              MetadataQuerySemanticKind::ColorProfile);
    EXPECT_EQ(icc_header_match->shape, MetadataQueryValueShape::Scalar);
    EXPECT_EQ(icc_header_match->group, "icc");
    EXPECT_EQ(icc_header_match->name, "ICCColorSpace");

    const MetadataQueryMatch* icc_tag_match = find_match_for_entry(result,
                                                                   icc_tag);
    ASSERT_NE(icc_tag_match, nullptr);
    EXPECT_EQ(icc_tag_match->semantic, MetadataQuerySemanticKind::ColorProfile);
    EXPECT_EQ(icc_tag_match->shape, MetadataQueryValueShape::Blob);
    EXPECT_EQ(icc_tag_match->name, "ICCProfileTag");

    const MetadataQueryMatch* png_match = find_match_for_entry(result,
                                                               png_iccp);
    ASSERT_NE(png_match, nullptr);
    EXPECT_EQ(png_match->semantic, MetadataQuerySemanticKind::ColorProfile);
    EXPECT_EQ(png_match->shape, MetadataQueryValueShape::Text);
    EXPECT_EQ(png_match->group, "png_text");

    EXPECT_NE(find_candidate_for_entry(result, xmp_icc), nullptr);
    EXPECT_NE(find_candidate_for_entry(result, icc_header), nullptr);
    EXPECT_NE(find_candidate_for_entry(result, icc_tag), nullptr);
    EXPECT_NE(find_candidate_for_entry(result, png_iccp), nullptr);
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::ColorProfile),
                 "color_profile");
}

TEST(MetadataQuery, MatchesSourceColorTransformCarriers)
{
    MetaStore store;
    const EntryId camera_profile
        = add_xmp_text(&store, "http://ns.adobe.com/camera-raw-settings/1.0/",
                       "crs:CameraProfile", "Adobe Color");
    const EntryId look_name
        = add_xmp_text(&store, "http://ns.adobe.com/camera-raw-settings/1.0/",
                       "crs:LookName", "Camera Vivid");
    const EntryId tone_curve
        = add_xmp_text(&store, "http://ns.adobe.com/camera-raw-settings/1.0/",
                       "crs:ToneCurvePV2012", "0, 0, 255, 255");
    const EntryId canon_colordata
        = add_exif_u32(&store, "mk_canon_colordata8_0", 0x0043U, 64U);
    store.finalize();

    const MetadataQueryResult result = query_color_metadata(store);

    const MetadataQueryMatch* profile_match
        = find_match_for_entry(result, camera_profile);
    ASSERT_NE(profile_match, nullptr);
    EXPECT_EQ(profile_match->semantic,
              MetadataQuerySemanticKind::SourceColorTransform);
    EXPECT_EQ(profile_match->shape, MetadataQueryValueShape::Text);
    EXPECT_TRUE(profile_match->exact_match);

    const MetadataQueryMatch* look_match = find_match_for_entry(result,
                                                                look_name);
    ASSERT_NE(look_match, nullptr);
    EXPECT_EQ(look_match->semantic,
              MetadataQuerySemanticKind::SourceColorTransform);
    EXPECT_EQ(look_match->shape, MetadataQueryValueShape::Text);

    const MetadataQueryMatch* curve_match = find_match_for_entry(result,
                                                                 tone_curve);
    ASSERT_NE(curve_match, nullptr);
    EXPECT_EQ(curve_match->semantic,
              MetadataQuerySemanticKind::SourceColorTransform);

    const MetadataQueryMatch* canon_match
        = find_match_for_entry(result, canon_colordata);
    ASSERT_NE(canon_match, nullptr);
    EXPECT_EQ(canon_match->semantic,
              MetadataQuerySemanticKind::SourceColorTransform);
    EXPECT_TRUE(canon_match->exact_match);
    EXPECT_EQ(canon_match->group, "mk_canon_colordata8_0");

    EXPECT_NE(find_candidate_for_entry(result, camera_profile), nullptr);
    EXPECT_NE(find_candidate_for_entry(result, look_name), nullptr);
    EXPECT_NE(find_candidate_for_entry(result, tone_curve), nullptr);
    EXPECT_NE(find_candidate_for_entry(result, canon_colordata), nullptr);
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::SourceColorTransform),
                 "source_color_transform");
}

TEST(MetadataQuery, GroupsDngColorMatrixSet)
{
    MetaStore store;
    const std::array<uint32_t, 9> matrix1 = {
        1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
    };
    const std::array<uint32_t, 9> matrix2 = {
        2U, 0U, 0U, 0U, 2U, 0U, 0U, 0U, 2U,
    };
    const EntryId matrix1_id
        = add_exif_u32_array(&store, "ifd0", 0xC621U,
                             std::span<const uint32_t>(matrix1.data(),
                                                       matrix1.size()));
    const EntryId matrix2_id
        = add_exif_u32_array(&store, "ifd0", 0xC622U,
                             std::span<const uint32_t>(matrix2.data(),
                                                       matrix2.size()));
    store.finalize();

    const MetadataQueryResult result = query_color_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::ColorMatrix,
                                    MetadataQueryValueShape::MatrixSet, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_GE(candidate->confidence, 90U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, matrix1_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, matrix2_id));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 18U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[4], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[8], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[9], 2.0);
    EXPECT_DOUBLE_EQ(candidate->values[13], 2.0);
    EXPECT_DOUBLE_EQ(candidate->values[17], 2.0);
    EXPECT_STREQ(metadata_query_value_shape_name(
                     MetadataQueryValueShape::MatrixSet),
                 "matrix_set");
}

TEST(MetadataQuery, SkipsMalformedDngColorMatrixSet)
{
    MetaStore store;
    const std::array<uint32_t, 9> matrix1 = {
        1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
    };
    const std::array<uint32_t, 2> malformed_matrix = { 2U, 0U };
    const EntryId matrix1_id
        = add_exif_u32_array(&store, "ifd0", 0xC621U,
                             std::span<const uint32_t>(matrix1.data(),
                                                       matrix1.size()));
    (void)add_exif_u32_array(&store, "ifd0", 0xC622U,
                             std::span<const uint32_t>(malformed_matrix.data(),
                                                       malformed_matrix.size()));
    store.finalize();

    const MetadataQueryResult result = query_color_metadata(store);

    const MetadataQueryCandidate* matrix_candidate
        = find_candidate_for_entry(result, matrix1_id);
    ASSERT_NE(matrix_candidate, nullptr);
    EXPECT_EQ(matrix_candidate->normalized_shape,
              MetadataQueryValueShape::Matrix3x3);
    const MetadataQueryCandidate* grouped
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::ColorMatrix,
                                    MetadataQueryValueShape::MatrixSet, 2U);
    EXPECT_EQ(grouped, nullptr);
}

TEST(MetadataQuery, GroupsDngWhiteBalanceVectorSet)
{
    MetaStore store;
    const std::array<uint32_t, 3> neutral = { 1U, 2U, 3U };
    const std::array<uint32_t, 3> analog  = { 10U, 20U, 30U };
    const EntryId neutral_id
        = add_exif_u32_array(&store, "ifd0", 0xC628U,
                             std::span<const uint32_t>(neutral.data(),
                                                       neutral.size()));
    const EntryId analog_id
        = add_exif_u32_array(&store, "ifd0", 0xC627U,
                             std::span<const uint32_t>(analog.data(),
                                                       analog.size()));
    store.finalize();

    const MetadataQueryResult result = query_white_balance_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::WhiteBalance,
                                    MetadataQueryValueShape::VectorSet, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_GE(candidate->confidence, 90U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, neutral_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, analog_id));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 6U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[1], 2.0);
    EXPECT_DOUBLE_EQ(candidate->values[2], 3.0);
    EXPECT_DOUBLE_EQ(candidate->values[3], 10.0);
    EXPECT_DOUBLE_EQ(candidate->values[4], 20.0);
    EXPECT_DOUBLE_EQ(candidate->values[5], 30.0);
    EXPECT_STREQ(metadata_query_value_shape_name(
                     MetadataQueryValueShape::VectorSet),
                 "vector_set");
}

TEST(MetadataQuery, SkipsMalformedDngWhiteBalanceVectorSet)
{
    MetaStore store;
    const std::array<uint32_t, 3> neutral          = { 1U, 2U, 3U };
    const std::array<uint32_t, 1> malformed_vector = { 10U };
    const EntryId neutral_id
        = add_exif_u32_array(&store, "ifd0", 0xC628U,
                             std::span<const uint32_t>(neutral.data(),
                                                       neutral.size()));
    (void)add_exif_u32_array(&store, "ifd0", 0xC627U,
                             std::span<const uint32_t>(malformed_vector.data(),
                                                       malformed_vector.size()));
    store.finalize();

    const MetadataQueryResult result = query_white_balance_metadata(store);

    const MetadataQueryCandidate* neutral_candidate
        = find_candidate_for_entry(result, neutral_id);
    ASSERT_NE(neutral_candidate, nullptr);
    EXPECT_EQ(neutral_candidate->semantic,
              MetadataQuerySemanticKind::WhiteBalance);
    const MetadataQueryCandidate* grouped
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::WhiteBalance,
                                    MetadataQueryValueShape::VectorSet, 2U);
    EXPECT_EQ(grouped, nullptr);
}

TEST(MetadataQuery, ReusesVendorLensCorrectionClassification)
{
    MetaStore store;
    const EntryId entry_id = add_exif_u32(&store, "mk_nikon_distortinfo",
                                          0x0001U, 7U);
    store.finalize();

    const MetadataQueryResult result = query_lens_correction_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::LensCorrection);
    const MetadataQueryMatch* match = find_match_for_entry(result, entry_id);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->semantic, MetadataQuerySemanticKind::LensCorrection);
    EXPECT_GE(match->confidence, 90U);
    EXPECT_TRUE(match->exact_match);
    EXPECT_FALSE(match->fuzzy_match);
    EXPECT_EQ(match->fuzzy_score, 0U);
    EXPECT_NE((match->matched_terms
               & static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)),
              0U);
    EXPECT_NE((match->matched_terms
               & static_cast<uint32_t>(MetadataQueryMatchTerm::Correction)),
              0U);
}

TEST(MetadataQuery, GroupsVendorLensCorrectionTable)
{
    MetaStore store;
    const EntryId distort_id  = add_exif_u32(&store, "mk_nikon_distortinfo",
                                             0x0001U, 7U);
    const EntryId vignette_id = add_exif_u32(&store, "mk_nikon_vignette",
                                             0x0001U, 3U);
    store.finalize();

    const MetadataQueryResult result = query_lens_correction_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::LensCorrection,
                                    MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_GE(candidate->confidence, 90U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, distort_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, vignette_id));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 2U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 7.0);
    EXPECT_DOUBLE_EQ(candidate->values[1], 3.0);
    EXPECT_STREQ(metadata_query_value_shape_name(MetadataQueryValueShape::Table),
                 "table");
}

TEST(MetadataQuery, SkipsTextOnlyVendorLensCorrectionGroup)
{
    MetaStore store;
    (void)add_exif_text(&store, "mk_nikon_distortinfo", 0x0001U, "distortion");
    (void)add_exif_text(&store, "mk_nikon_vignette", 0x0001U, "vignette");
    store.finalize();

    const MetadataQueryResult result = query_lens_correction_metadata(store);

    const MetadataQueryCandidate* grouped
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::LensCorrection,
                                    MetadataQueryValueShape::Table, 2U);
    EXPECT_EQ(grouped, nullptr);
}

TEST(MetadataQuery, MatchesOrientationTags)
{
    MetaStore store;
    const EntryId entry_id = add_exif_u32(&store, "ifd0", 0x0112U, 6U);
    store.finalize();

    const MetadataQueryResult result = query_orientation_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::Orientation);
    const MetadataQueryMatch* match = find_match_for_entry(result, entry_id);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->semantic, MetadataQuerySemanticKind::Orientation);
    EXPECT_GE(match->confidence, 90U);
    const MetadataQueryCandidate* candidate
        = find_candidate_for_entry(result, entry_id);
    ASSERT_NE(candidate, nullptr);
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 1U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 6.0);
    EXPECT_STREQ(metadata_query_kind_name(result.kind), "orientation");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::Color),
                 "color");
    EXPECT_STREQ(metadata_query_value_shape_name(
                     MetadataQueryValueShape::Matrix3x3),
                 "matrix3x3");
}

TEST(MetadataQuery, MatchesDngRawProcessingLevels)
{
    MetaStore store;
    const std::array<uint32_t, 2> linearization = { 0U, 65535U };
    const EntryId black_id = add_exif_u32(&store, "ifd0", 0xC61AU, 512U);
    const EntryId white_id = add_exif_u32(&store, "ifd0", 0xC61DU, 16383U);
    const EntryId linearization_id
        = add_exif_u32_array(&store, "ifd0", 0xC618U,
                             std::span<const uint32_t>(linearization.data(),
                                                       linearization.size()));
    store.finalize();

    const MetadataQueryResult result = query_raw_processing_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::RawProcessing);
    const MetadataQueryMatch* black_match = find_match_for_entry(result,
                                                                 black_id);
    ASSERT_NE(black_match, nullptr);
    EXPECT_EQ(black_match->semantic, MetadataQuerySemanticKind::BlackLevel);
    const MetadataQueryMatch* white_match = find_match_for_entry(result,
                                                                 white_id);
    ASSERT_NE(white_match, nullptr);
    EXPECT_EQ(white_match->semantic, MetadataQuerySemanticKind::WhiteLevel);
    const MetadataQueryMatch* linearization_match
        = find_match_for_entry(result, linearization_id);
    ASSERT_NE(linearization_match, nullptr);
    EXPECT_EQ(linearization_match->semantic,
              MetadataQuerySemanticKind::RawValueCurve);

    const MetadataQueryCandidate* black_candidate
        = find_candidate_for_entry(result, black_id);
    ASSERT_NE(black_candidate, nullptr);
    ASSERT_TRUE(black_candidate->has_values);
    ASSERT_EQ(black_candidate->values.size(), 1U);
    EXPECT_DOUBLE_EQ(black_candidate->values[0], 512.0);
    const MetadataQueryCandidate* curve_candidate
        = find_candidate_for_entry(result, linearization_id);
    ASSERT_NE(curve_candidate, nullptr);
    EXPECT_EQ(curve_candidate->semantic,
              MetadataQuerySemanticKind::RawValueCurve);
    EXPECT_EQ(curve_candidate->normalized_shape, MetadataQueryValueShape::Vec2);
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::RawValueCurve),
                 "raw_value_curve");
}

TEST(MetadataQuery, ClassifiesRawCurveControlPointsAndLinearityLimits)
{
    MetaStore store;
    const std::array<uint32_t, 4> sony_curve = {
        1200U,
        2400U,
        3200U,
        3800U,
    };
    const EntryId sony_curve_id
        = add_exif_u32_array(&store, "exififd", 0x7010U,
                             std::span<const uint32_t>(sony_curve.data(),
                                                       sony_curve.size()));
    const EntryId response_limit_id  = add_exif_u32(&store, "ifd0", 0xC62EU,
                                                    95U);
    const EntryId panasonic_limit_id = add_exif_u32(&store, "ifd0", 0x000EU,
                                                    16320U);
    const EntryId phaseone_coeff_id
        = add_exif_u32_array(&store, "mk_phaseone_sensorcalibration", 0x0419U,
                             std::span<const uint32_t>(sony_curve.data(),
                                                       sony_curve.size()));
    store.finalize();

    const MetadataQueryResult result = query_raw_processing_metadata(store);

    const MetadataQueryMatch* sony_curve_match
        = find_match_for_entry(result, sony_curve_id);
    ASSERT_NE(sony_curve_match, nullptr);
    EXPECT_EQ(sony_curve_match->semantic,
              MetadataQuerySemanticKind::RawCurveControlPoints);

    const MetadataQueryMatch* response_limit_match
        = find_match_for_entry(result, response_limit_id);
    ASSERT_NE(response_limit_match, nullptr);
    EXPECT_EQ(response_limit_match->semantic,
              MetadataQuerySemanticKind::RawLinearityLimit);

    const MetadataQueryMatch* panasonic_limit_match
        = find_match_for_entry(result, panasonic_limit_id);
    ASSERT_NE(panasonic_limit_match, nullptr);
    EXPECT_EQ(panasonic_limit_match->semantic,
              MetadataQuerySemanticKind::RawLinearityLimit);

    const MetadataQueryMatch* phaseone_coeff_match
        = find_match_for_entry(result, phaseone_coeff_id);
    ASSERT_NE(phaseone_coeff_match, nullptr);
    EXPECT_EQ(phaseone_coeff_match->semantic,
              MetadataQuerySemanticKind::RawCalibrationCurve);

    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::RawCurveControlPoints),
                 "raw_curve_control_points");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::RawLinearityLimit),
                 "raw_linearity_limit");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::RawCalibrationCurve),
                 "raw_calibration_curve");
}

TEST(MetadataQuery, MatchesVendorSourceProcessingSubroles)
{
    MetaStore store;
    const EntryId computational_id
        = add_exif_u32(&store, "mk_google_shotlogdata", 0x0001U, 7U);
    const EntryId thermal_id = add_exif_u32(&store, "mk_dji_thermalparams",
                                            0x0048U, 98U);
    const EntryId stitch_id  = add_exif_u32(&store, "mk_microsoft_stitch",
                                            0x0003U, 12U);
    const EntryId nikonsettings_id
        = add_exif_u32(&store, "mk_nikonsettings_main_0", 0x00B1U, 1U);
    store.finalize();

    const MetadataQueryResult result = query_raw_processing_metadata(store);

    const MetadataQueryMatch* computational_match
        = find_match_for_entry(result, computational_id);
    ASSERT_NE(computational_match, nullptr);
    EXPECT_EQ(computational_match->semantic,
              MetadataQuerySemanticKind::ComputationalProcessing);
    EXPECT_GE(computational_match->confidence, 80U);
    EXPECT_NE((computational_match->matched_terms
               & static_cast<uint32_t>(
                   MetadataQueryMatchTerm::SourceProcessing)),
              0U);
    const MetadataQueryCandidate* computational
        = find_candidate_for_entry(result, computational_id);
    ASSERT_NE(computational, nullptr);
    EXPECT_EQ(computational->semantic,
              MetadataQuerySemanticKind::ComputationalProcessing);
    ASSERT_TRUE(computational->has_values);
    ASSERT_EQ(computational->values.size(), 1U);
    EXPECT_DOUBLE_EQ(computational->values[0], 7.0);

    const MetadataQueryMatch* thermal_match = find_match_for_entry(result,
                                                                   thermal_id);
    ASSERT_NE(thermal_match, nullptr);
    EXPECT_EQ(thermal_match->semantic,
              MetadataQuerySemanticKind::ThermalProcessing);
    const MetadataQueryCandidate* thermal
        = find_candidate_for_entry(result, thermal_id);
    ASSERT_NE(thermal, nullptr);
    EXPECT_EQ(thermal->semantic, MetadataQuerySemanticKind::ThermalProcessing);

    const MetadataQueryMatch* stitch_match = find_match_for_entry(result,
                                                                  stitch_id);
    ASSERT_NE(stitch_match, nullptr);
    EXPECT_EQ(stitch_match->semantic,
              MetadataQuerySemanticKind::StitchProcessing);
    const MetadataQueryCandidate* stitch = find_candidate_for_entry(result,
                                                                    stitch_id);
    ASSERT_NE(stitch, nullptr);
    EXPECT_EQ(stitch->semantic, MetadataQuerySemanticKind::StitchProcessing);

    const MetadataQueryMatch* nikonsettings_match
        = find_match_for_entry(result, nikonsettings_id);
    ASSERT_NE(nikonsettings_match, nullptr);
    EXPECT_EQ(nikonsettings_match->semantic,
              MetadataQuerySemanticKind::SourceProcessing);
    EXPECT_TRUE(nikonsettings_match->exact_match);
    const MetadataQueryCandidate* nikonsettings
        = find_candidate_for_entry(result, nikonsettings_id);
    ASSERT_NE(nikonsettings, nullptr);
    EXPECT_EQ(nikonsettings->semantic,
              MetadataQuerySemanticKind::SourceProcessing);

    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::ComputationalProcessing),
                 "computational_processing");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::ThermalProcessing),
                 "thermal_processing");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::StitchProcessing),
                 "stitch_processing");
}

TEST(MetadataQuery, MatchesCanonAfMicroAndAmbienceAliases)
{
    MetaStore store;
    const EntryId af_micro = add_exif_u32(&store, "makernote:canon:afmicroadj",
                                          0x0001U, 1U);
    const EntryId ambience = add_exif_u32(&store, "makernote:canon:ambience",
                                          0x0001U, 1U);
    store.finalize();

    const MetadataQueryResult lens_result = query_lens_correction_metadata(
        store);
    const MetadataQueryMatch* af_micro_match = find_match_for_entry(lens_result,
                                                                    af_micro);
    ASSERT_NE(af_micro_match, nullptr);
    EXPECT_EQ(af_micro_match->semantic,
              MetadataQuerySemanticKind::LensCorrection);
    EXPECT_TRUE(af_micro_match->exact_match);

    const MetadataQueryResult raw_result = query_raw_processing_metadata(store);
    const MetadataQueryMatch* ambience_match = find_match_for_entry(raw_result,
                                                                    ambience);
    ASSERT_NE(ambience_match, nullptr);
    EXPECT_EQ(ambience_match->semantic,
              MetadataQuerySemanticKind::SourceProcessing);
    EXPECT_TRUE(ambience_match->exact_match);
}

TEST(MetadataQuery, GroupsVendorWhiteBalanceVectorSetByFamily)
{
    MetaStore store;
    const std::array<uint32_t, 4> daylight = { 110U, 256U, 256U, 144U };
    const std::array<uint32_t, 4> cloudy   = { 120U, 256U, 256U, 136U };
    const EntryId daylight_id
        = add_exif_u32_array(&store, "mk_nikon_colorbalancec_0", 0x0114U,
                             std::span<const uint32_t>(daylight.data(),
                                                       daylight.size()));
    const EntryId cloudy_id
        = add_exif_u32_array(&store, "mk_nikon_colorbalancec_0", 0x0115U,
                             std::span<const uint32_t>(cloudy.data(),
                                                       cloudy.size()));
    store.finalize();

    const MetadataQueryResult result = query_white_balance_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::WhiteBalance,
                                    MetadataQueryValueShape::VectorSet, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_TRUE(contains_entry(candidate->source_entries, daylight_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, cloudy_id));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 8U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 110.0);
    EXPECT_DOUBLE_EQ(candidate->values[4], 120.0);
}

TEST(MetadataQuery, GroupsSamsungColorMatrixSetByAlias)
{
    MetaStore store;
    const std::array<uint32_t, 9> matrix_a = {
        1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
    };
    const std::array<uint32_t, 9> matrix_b = {
        2U, 0U, 0U, 0U, 2U, 0U, 0U, 0U, 2U,
    };
    const EntryId matrix_a_id
        = add_exif_u32_array(&store, "mk_samsung_type2_0", 0xA030U,
                             std::span<const uint32_t>(matrix_a.data(),
                                                       matrix_a.size()));
    const EntryId matrix_b_id
        = add_exif_u32_array(&store, "mk_samsung_type2_0", 0xA031U,
                             std::span<const uint32_t>(matrix_b.data(),
                                                       matrix_b.size()));
    store.finalize();

    const MetadataQueryResult result = query_color_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::ColorMatrix,
                                    MetadataQueryValueShape::MatrixSet, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_TRUE(contains_entry(candidate->source_entries, matrix_a_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, matrix_b_id));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 18U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[9], 2.0);
}

TEST(MetadataQuery, GroupsSamsungWhiteBalanceVectorSetByAlias)
{
    MetaStore store;
    const std::array<uint32_t, 4> uncorrected = { 110U, 256U, 256U, 144U };
    const std::array<uint32_t, 4> automatic   = { 120U, 256U, 256U, 136U };
    const EntryId uncorrected_id
        = add_exif_u32_array(&store, "mk_samsung_type2_0", 0xA021U,
                             std::span<const uint32_t>(uncorrected.data(),
                                                       uncorrected.size()));
    const EntryId automatic_id
        = add_exif_u32_array(&store, "mk_samsung_type2_0", 0xA022U,
                             std::span<const uint32_t>(automatic.data(),
                                                       automatic.size()));
    store.finalize();

    const MetadataQueryResult result = query_white_balance_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::WhiteBalance,
                                    MetadataQueryValueShape::VectorSet, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_TRUE(contains_entry(candidate->source_entries, uncorrected_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, automatic_id));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 8U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 110.0);
    EXPECT_DOUBLE_EQ(candidate->values[4], 120.0);
}

TEST(MetadataQuery, SkipsMalformedVendorColorMatrixSet)
{
    MetaStore store;
    const std::array<uint32_t, 9> color_matrix = {
        1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
    };
    const std::array<uint32_t, 4> malformed_matrix = { 2U, 0U, 0U, 2U };
    const EntryId matrix_id
        = add_exif_u32_array(&store, "mk_phaseone0", 0x0106U,
                             std::span<const uint32_t>(color_matrix.data(),
                                                       color_matrix.size()));
    (void)add_exif_u32_array(&store, "mk_phaseone0", 0x0226U,
                             std::span<const uint32_t>(malformed_matrix.data(),
                                                       malformed_matrix.size()));
    store.finalize();

    const MetadataQueryResult result = query_color_metadata(store);

    const MetadataQueryCandidate* matrix_candidate
        = find_candidate_for_entry(result, matrix_id);
    ASSERT_NE(matrix_candidate, nullptr);
    EXPECT_EQ(matrix_candidate->semantic,
              MetadataQuerySemanticKind::ColorMatrix);
    const MetadataQueryCandidate* grouped
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::ColorMatrix,
                                    MetadataQueryValueShape::MatrixSet, 2U);
    EXPECT_EQ(grouped, nullptr);
}

TEST(MetadataQuery, GroupsSamsungLensCorrectionTableByAlias)
{
    MetaStore store;
    const EntryId vignette_id   = add_exif_u32(&store, "mk_samsung_type2_0",
                                               0xA052U, 7U);
    const EntryId correction_id = add_exif_u32(&store, "mk_samsung_type2_0",
                                               0xA053U, 3U);
    store.finalize();

    const MetadataQueryResult result = query_lens_correction_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::LensCorrection,
                                    MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_TRUE(contains_entry(candidate->source_entries, vignette_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, correction_id));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 2U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 7.0);
    EXPECT_DOUBLE_EQ(candidate->values[1], 3.0);
}

TEST(MetadataQuery, GroupsVendorSourceColorTransformTableByFamily)
{
    MetaStore store;
    const EntryId color_a = add_exif_u32(&store, "mk_minolta_colorcomp_0",
                                         0x0010U, 12U);
    const EntryId color_b = add_exif_u32(&store, "mk_minolta_colorcomp_0",
                                         0x0011U, 24U);
    store.finalize();

    const MetadataQueryResult result = query_color_metadata(store);

    const MetadataQueryCandidate* candidate = find_candidate_with_shape(
        result, MetadataQuerySemanticKind::SourceColorTransform,
        MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_TRUE(contains_entry(candidate->source_entries, color_a));
    EXPECT_TRUE(contains_entry(candidate->source_entries, color_b));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 2U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 12.0);
    EXPECT_DOUBLE_EQ(candidate->values[1], 24.0);
}

TEST(MetadataQuery, GroupsSonyStyleComputationalProcessingByAlias)
{
    MetaStore store;
    const EntryId style_a = add_exif_u32(&store, "mk_sony0", 0xB020U, 1U);
    const EntryId style_b = add_exif_u32(&store, "mk_sony_camerasettings_0",
                                         0x001AU, 2U);
    store.finalize();

    const MetadataQueryResult result = query_raw_processing_metadata(store);

    const MetadataQueryCandidate* candidate = find_candidate_with_shape(
        result, MetadataQuerySemanticKind::ComputationalProcessing,
        MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_TRUE(contains_entry(candidate->source_entries, style_a));
    EXPECT_TRUE(contains_entry(candidate->source_entries, style_b));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 2U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[1], 2.0);
}

TEST(MetadataQuery, GroupsVendorRawProcessingTablesByFamilyAndSemantic)
{
    MetaStore store;
    const EntryId raw_offset = add_exif_u32(&store, "mk_panasonic_rawinfo_0",
                                            0x0100U, 128U);
    const EntryId raw_length = add_exif_u32(&store, "mk_panasonic_rawinfo_0",
                                            0x0101U, 4096U);
    const EntryId shot_log_a = add_exif_u32(&store, "mk_google_shotlogdata",
                                            0x0001U, 1U);
    const EntryId shot_log_b = add_exif_u32(&store, "mk_google_shotlogdata",
                                            0x0002U, 2U);
    store.finalize();

    const MetadataQueryResult result = query_raw_processing_metadata(store);

    const MetadataQueryCandidate* storage
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::RawStorage,
                                    MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(storage, nullptr);
    EXPECT_TRUE(contains_entry(storage->source_entries, raw_offset));
    EXPECT_TRUE(contains_entry(storage->source_entries, raw_length));
    ASSERT_TRUE(storage->has_values);
    ASSERT_EQ(storage->values.size(), 2U);
    EXPECT_DOUBLE_EQ(storage->values[0], 128.0);
    EXPECT_DOUBLE_EQ(storage->values[1], 4096.0);

    const MetadataQueryCandidate* source_processing = find_candidate_with_shape(
        result, MetadataQuerySemanticKind::ComputationalProcessing,
        MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(source_processing, nullptr);
    EXPECT_TRUE(contains_entry(source_processing->source_entries, shot_log_a));
    EXPECT_TRUE(contains_entry(source_processing->source_entries, shot_log_b));
    ASSERT_TRUE(source_processing->has_values);
    ASSERT_EQ(source_processing->values.size(), 2U);
    EXPECT_DOUBLE_EQ(source_processing->values[0], 1.0);
    EXPECT_DOUBLE_EQ(source_processing->values[1], 2.0);
}

TEST(MetadataQuery, GroupsDngBlackLevelAndCfaTables)
{
    MetaStore store;
    const std::array<uint32_t, 2> repeat_dim = { 2U, 2U };
    const std::array<uint32_t, 4> black      = { 512U, 513U, 514U, 515U };
    const std::array<uint32_t, 2> cfa_dim    = { 2U, 2U };
    const std::array<uint32_t, 4> cfa        = { 0U, 1U, 1U, 2U };
    const EntryId repeat_id
        = add_exif_u32_array(&store, "ifd0", 0xC619U,
                             std::span<const uint32_t>(repeat_dim.data(),
                                                       repeat_dim.size()));
    const EntryId black_id
        = add_exif_u32_array(&store, "ifd0", 0xC61AU,
                             std::span<const uint32_t>(black.data(),
                                                       black.size()));
    const EntryId cfa_dim_id
        = add_exif_u32_array(&store, "ifd0", 0x828DU,
                             std::span<const uint32_t>(cfa_dim.data(),
                                                       cfa_dim.size()));
    const EntryId cfa_id
        = add_exif_u32_array(&store, "ifd0", 0x828EU,
                             std::span<const uint32_t>(cfa.data(), cfa.size()));
    store.finalize();

    const MetadataQueryResult result = query_raw_processing_metadata(store);

    const MetadataQueryCandidate* black_candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::BlackLevel,
                                    MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(black_candidate, nullptr);
    EXPECT_TRUE(contains_entry(black_candidate->source_entries, repeat_id));
    EXPECT_TRUE(contains_entry(black_candidate->source_entries, black_id));
    ASSERT_TRUE(black_candidate->has_values);
    ASSERT_EQ(black_candidate->values.size(), 6U);
    EXPECT_DOUBLE_EQ(black_candidate->values[0], 2.0);
    EXPECT_DOUBLE_EQ(black_candidate->values[2], 512.0);
    EXPECT_DOUBLE_EQ(black_candidate->values[5], 515.0);

    const MetadataQueryCandidate* cfa_candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::CfaLayout,
                                    MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(cfa_candidate, nullptr);
    EXPECT_TRUE(contains_entry(cfa_candidate->source_entries, cfa_dim_id));
    EXPECT_TRUE(contains_entry(cfa_candidate->source_entries, cfa_id));
    ASSERT_TRUE(cfa_candidate->has_values);
    ASSERT_EQ(cfa_candidate->values.size(), 6U);
    EXPECT_DOUBLE_EQ(cfa_candidate->values[0], 2.0);
    EXPECT_DOUBLE_EQ(cfa_candidate->values[2], 0.0);
    EXPECT_DOUBLE_EQ(cfa_candidate->values[5], 2.0);
}

TEST(MetadataQuery, GroupsDngRawStorageTable)
{
    MetaStore store;
    const std::array<uint32_t, 4> raw_id = { 1U, 2U, 3U, 4U };
    const EntryId raw_id_entry
        = add_exif_u32_array(&store, "ifd0", 0xC65DU,
                             std::span<const uint32_t>(raw_id.data(),
                                                       raw_id.size()));
    const EntryId raw_name_entry = add_exif_text(&store, "ifd0", 0xC68BU,
                                                 "source.raw");
    store.finalize();

    const MetadataQueryResult result = query_raw_processing_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::RawStorage,
                                    MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_TRUE(contains_entry(candidate->source_entries, raw_id_entry));
    EXPECT_TRUE(contains_entry(candidate->source_entries, raw_name_entry));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 4U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[3], 4.0);
    EXPECT_STREQ(metadata_query_kind_name(result.kind), "raw_processing");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::CfaLayout),
                 "cfa_layout");
}

TEST(MetadataQuery, ClassifiesNestedTaxonomyRegistryRegionAndLineage)
{
    MetaStore store;
    const std::string_view ext = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
    const std::string_view mm  = "http://ns.adobe.com/xap/1.0/mm/";
    const EntryId taxonomy
        = add_xmp_text(&store, ext,
                       "AboutCvTerm[1]/CvTermName[@xml:lang=x-default]",
                       "Culture");
    const EntryId registry
        = add_xmp_text(&store, ext, "RegistryId[1]/RegItemId", "asset-001");
    const EntryId region
        = add_xmp_text(&store, ext, "ImageRegion[1]/rRole[1]/Name", "subject");
    const EntryId lineage = add_xmp_text(&store, mm,
                                         "Ingredients[1]/stRef:documentID",
                                         "xmp.did:ingredient");
    const EntryId history = add_xmp_text(&store, mm, "History[1]/stEvt:action",
                                         "saved");
    const EntryId version = add_xmp_text(&store, mm,
                                         "Versions[1]/stVer:comments",
                                         "Approved master");
    const EntryId pantry_lineage
        = add_xmp_text(&store, mm, "Pantry[1]/DerivedFrom/stRef:documentID",
                       "xmp.did:pantry-source");
    const EntryId pantry_history
        = add_xmp_text(&store, mm, "Pantry[1]/History[1]/stEvt:action",
                       "copied");
    const EntryId pantry_version
        = add_xmp_text(&store, mm, "Pantry[1]/Versions[1]/stVer:comments",
                       "Embedded component");
    const EntryId vendor_history
        = add_xmp_text(&store, mm, "Pantry[2]/ns:History[1]/ns:action",
                       "vendor-action");
    store.finalize();

    const MetadataQueryResult result = query_descriptive_metadata(store);
    const MetadataQueryMatch* taxonomy_match = find_match_for_entry(result,
                                                                    taxonomy);
    const MetadataQueryMatch* registry_match = find_match_for_entry(result,
                                                                    registry);
    const MetadataQueryMatch* region_match   = find_match_for_entry(result,
                                                                    region);
    const MetadataQueryMatch* lineage_match  = find_match_for_entry(result,
                                                                    lineage);
    const MetadataQueryMatch* history_match  = find_match_for_entry(result,
                                                                    history);
    const MetadataQueryMatch* version_match  = find_match_for_entry(result,
                                                                    version);
    const MetadataQueryMatch* pantry_lineage_match
        = find_match_for_entry(result, pantry_lineage);
    const MetadataQueryMatch* pantry_history_match
        = find_match_for_entry(result, pantry_history);
    const MetadataQueryMatch* pantry_version_match
        = find_match_for_entry(result, pantry_version);
    const MetadataQueryMatch* vendor_history_match
        = find_match_for_entry(result, vendor_history);

    ASSERT_NE(taxonomy_match, nullptr);
    ASSERT_NE(registry_match, nullptr);
    ASSERT_NE(region_match, nullptr);
    ASSERT_NE(lineage_match, nullptr);
    ASSERT_NE(history_match, nullptr);
    ASSERT_NE(version_match, nullptr);
    ASSERT_NE(pantry_lineage_match, nullptr);
    ASSERT_NE(pantry_history_match, nullptr);
    ASSERT_NE(pantry_version_match, nullptr);
    ASSERT_NE(vendor_history_match, nullptr);
    EXPECT_EQ(taxonomy_match->semantic, MetadataQuerySemanticKind::Taxonomy);
    EXPECT_EQ(registry_match->semantic, MetadataQuerySemanticKind::Registry);
    EXPECT_EQ(region_match->semantic, MetadataQuerySemanticKind::ImageRegion);
    EXPECT_EQ(lineage_match->semantic,
              MetadataQuerySemanticKind::DocumentLineage);
    EXPECT_EQ(history_match->semantic,
              MetadataQuerySemanticKind::DocumentHistory);
    EXPECT_EQ(version_match->semantic,
              MetadataQuerySemanticKind::DocumentHistory);
    EXPECT_EQ(pantry_lineage_match->semantic,
              MetadataQuerySemanticKind::DocumentLineage);
    EXPECT_EQ(pantry_history_match->semantic,
              MetadataQuerySemanticKind::DocumentHistory);
    EXPECT_EQ(pantry_version_match->semantic,
              MetadataQuerySemanticKind::DocumentHistory);
    EXPECT_EQ(vendor_history_match->semantic,
              MetadataQuerySemanticKind::DocumentLineage);
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::DocumentLineage),
                 "document_lineage");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::DocumentHistory),
                 "document_history");
}

TEST(MetadataQuery, ClassifiesLegacyIptcTechnicalSemantics)
{
    MetaStore store;
    const EntryId image   = add_iptc_text(&store, 2U, 131U, "L");
    const EntryId audio   = add_iptc_text(&store, 2U, 151U, "044100");
    const EntryId preview = add_iptc_text(&store, 2U, 200U, "11");
    store.finalize();

    const MetadataQueryResult result      = query_descriptive_metadata(store);
    const MetadataQueryMatch* image_match = find_match_for_entry(result, image);
    const MetadataQueryMatch* audio_match = find_match_for_entry(result, audio);
    const MetadataQueryMatch* preview_match = find_match_for_entry(result,
                                                                   preview);

    ASSERT_NE(image_match, nullptr);
    ASSERT_NE(audio_match, nullptr);
    ASSERT_NE(preview_match, nullptr);
    EXPECT_TRUE(image_match->exact_match);
    EXPECT_TRUE(audio_match->exact_match);
    EXPECT_TRUE(preview_match->exact_match);
    EXPECT_EQ(image_match->semantic, MetadataQuerySemanticKind::TechnicalImage);
    EXPECT_EQ(audio_match->semantic, MetadataQuerySemanticKind::Audio);
    EXPECT_EQ(preview_match->semantic, MetadataQuerySemanticKind::Preview);
    EXPECT_EQ(image_match->name, "ImageOrientation");
    EXPECT_EQ(audio_match->name, "AudioSamplingRate");
    EXPECT_EQ(preview_match->name, "ObjectPreviewFileFormat");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::TechnicalImage),
                 "technical_image");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::Audio),
                 "audio");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::Preview),
                 "preview");
}

}  // namespace openmeta
