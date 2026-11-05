// SPDX-License-Identifier: Apache-2.0
#include "openmeta/metadata_concepts.h"
#include "openmeta/metadata_interpretation.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include <gtest/gtest.h>
#include <array>
#include <vector>

namespace openmeta {
namespace {
    static EntryId add_scoped(MetaStore& store, BlockId block, std::string_view ifd,
                               uint16_t tag, std::span<const uint32_t> values)
    {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd, tag);
        entry.origin.block = block;
        entry.value = make_u32_array(store.arena(), values);
        return store.add_entry(entry);
    }

    static const MetadataInterpretationRecord* record_for(
        const MetadataInterpretationResult& result, EntryId id)
    {
        for (const MetadataInterpretationRecord& record : result.records) {
            if (record.source_entries.size() == 1U && record.source_entries[0] == id)
                return &record;
        }
        return nullptr;
    }

    static const MetadataConceptCandidate* candidate_for(
        const MetadataConceptResolution& result, EntryId id)
    {
        for (const MetadataConceptCandidate& candidate : result.candidates) {
            if (candidate.entry_id == id && candidate.source_entries.size() == 1U)
                return &candidate;
        }
        return nullptr;
    }
}

TEST(MetadataScopePropagation, CompleteAndLimitedCurvesRetainOriginalCounts)
{
    for (uint32_t size : { 17U, 4096U }) {
        MetaStore store;
        const BlockId block = store.add_block({});
        std::vector<uint32_t> values(size);
        for (uint32_t i = 0U; i < size; ++i) values[i] = i;
        const EntryId id = add_scoped(store, block, "ifd0", 0xc618U, values);
        store.finalize();
        const MetadataInterpretationResult full
            = interpret_metadata_query(store, MetadataQueryKind::RawProcessing);
        const MetadataInterpretationRecord* record = record_for(full, id);
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->values.size(), size);
        EXPECT_EQ(record->total_numeric_values, size);
        EXPECT_EQ(record->numeric_status, MetadataQueryNumericStatus::Complete);
        ASSERT_EQ(record->components.size(), 1U);
        EXPECT_EQ(record->components[0].source_entry, id);
        EXPECT_EQ(record->components[0].total_value_count, size);
        EXPECT_EQ(record->components[0].role, MetadataQueryComponentRole::LinearizationTable);
        MetadataQueryOptions options;
        options.max_values_per_entry = 16U;
        const MetadataInterpretationResult limited
            = interpret_metadata_query(store, MetadataQueryKind::RawProcessing, options);
        record = record_for(limited, id);
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->values.size(), 16U);
        EXPECT_EQ(record->total_numeric_values, size);
        EXPECT_EQ(record->numeric_status, MetadataQueryNumericStatus::Partial);
        EXPECT_TRUE(limited.numeric_values_limited);
        const MetadataConceptResolution concepts
            = resolve_metadata_concept(store, MetadataConceptKind::RawProcessing, options);
        const MetadataConceptCandidate* candidate = candidate_for(concepts, id);
        ASSERT_NE(candidate, nullptr);
        EXPECT_EQ(candidate->values.size(), 16U);
        EXPECT_EQ(candidate->total_numeric_values, size);
        EXPECT_EQ(candidate->numeric_status, MetadataQueryNumericStatus::Partial);
        EXPECT_FALSE(candidate->preferred);
        ASSERT_EQ(candidate->components.size(), 1U);
        EXPECT_EQ(candidate->components[0].returned_value_count, 16U);
        EXPECT_EQ(store.entry(id).value.count, size);
        options.include_numeric_values = false;
        const MetadataConceptResolution suppressed
            = resolve_metadata_concept(store, MetadataConceptKind::RawProcessing, options);
        candidate = candidate_for(suppressed, id);
        ASSERT_NE(candidate, nullptr);
        EXPECT_TRUE(candidate->values.empty());
        EXPECT_EQ(candidate->values.capacity(), 0U);
        EXPECT_EQ(candidate->total_numeric_values, size);
        EXPECT_EQ(candidate->numeric_status, MetadataQueryNumericStatus::Partial);
    }
}

TEST(MetadataScopePropagation, AggregatedServicesShareOneNumericBudget)
{
    MetaStore store;
    const BlockId block = store.add_block({});
    const std::array<uint32_t, 3> neutral { 1, 2, 3 };
    std::array<uint32_t, 17> curve {};
    for (uint32_t i = 0U; i < curve.size(); ++i) curve[i] = i;
    ASSERT_NE(add_scoped(store, block, "ifd0", 0xc628U, neutral), kInvalidEntryId);
    ASSERT_NE(add_scoped(store, block, "ifd0", 0xc618U, curve), kInvalidEntryId);
    store.finalize();
    MetadataQueryOptions options;
    options.max_total_values = 7U;
    const MetadataInterpretationResult interpreted = interpret_metadata(store, options);
    EXPECT_LE(interpreted.returned_numeric_values, 7U);
    EXPECT_TRUE(interpreted.numeric_values_limited);
    size_t actual_values = 0U;
    for (const MetadataInterpretationRecord& record : interpreted.records)
        actual_values += record.values.size();
    EXPECT_EQ(actual_values, interpreted.returned_numeric_values);
    const MetadataConceptResult concepts = resolve_metadata_concepts(store, options);
    EXPECT_LE(concepts.returned_numeric_values, 7U);
    EXPECT_TRUE(concepts.numeric_values_limited);
    actual_values = 0U;
    for (const MetadataConceptResolution& resolution : concepts.concepts)
        for (const MetadataConceptCandidate& candidate : resolution.candidates)
            actual_values += candidate.values.size();
    EXPECT_LE(actual_values, 7U);
}

TEST(MetadataScopePropagation, ConflictsAndPreferencesStayInCallerImageScope)
{
    MetaStore store;
    const BlockId a = store.add_block({}), b = store.add_block({});
    const std::array<uint32_t, 3> first { 1, 2, 3 }, second { 4, 5, 6 };
    const EntryId first_id = add_scoped(store, a, "ifd0", 0xc618U, first);
    const EntryId second_id = add_scoped(store, b, "ifd0", 0xc618U, second);
    store.finalize();
    const MetadataConceptResolution all
        = resolve_metadata_concept(store, MetadataConceptKind::RawProcessing);
    ASSERT_EQ(all.candidates.size(), 2U);
    EXPECT_FALSE(all.conflict);
    EXPECT_EQ(all.preferred_entry, kInvalidEntryId);
    EXPECT_TRUE(all.candidates[0].preferred);
    EXPECT_TRUE(all.candidates[1].preferred);
    const std::array<MetadataDirectorySelector, 2> directories {{{ a, "ifd0" }, { b, "ifd0" }}};
    MetadataQueryOptions options;
    options.scope.enabled = true;
    options.scope.directories = std::span<const MetadataDirectorySelector>(directories).subspan(1U);
    const MetadataConceptResolution selected
        = resolve_metadata_concept(store, MetadataConceptKind::RawProcessing, options);
    ASSERT_EQ(selected.candidates.size(), 1U);
    EXPECT_EQ(selected.candidates[0].entry_id, second_id);
    EXPECT_EQ(selected.candidates[0].source_block, b);
    EXPECT_EQ(selected.preferred_entry, second_id);
    EXPECT_FALSE(selected.conflict);
    options.scope.directories = directories;
    const MetadataConceptResolution one_image
        = resolve_metadata_concept(store, MetadataConceptKind::RawProcessing, options);
    EXPECT_TRUE(one_image.conflict);
    EXPECT_EQ(one_image.preferred_entry, first_id);
}

TEST(MetadataScopePropagation, DirectoryNamesAndUnassignedBlocksAreExactSelectors)
{
    MetaStore store;
    const BlockId block = store.add_block({});
    const std::array<uint32_t, 2> values { 1, 2 };
    const EntryId raw = add_scoped(store, block, "ifd0", 0xc618U, values);
    const EntryId preview = add_scoped(store, block, "ifd1", 0xc618U, values);
    const EntryId unassigned = add_scoped(store, kInvalidBlockId, "ifd0", 0xc618U, values);
    store.finalize();
    MetadataQueryOptions options;
    options.scope.enabled = true;
    MetadataDirectorySelector directory { block, "ifd0" };
    options.scope.directories = std::span<const MetadataDirectorySelector>(&directory, 1U);
    MetadataInterpretationResult interpreted
        = interpret_metadata_query(store, MetadataQueryKind::RawProcessing, options);
    ASSERT_EQ(interpreted.records.size(), 1U);
    EXPECT_EQ(interpreted.records[0].source_entries[0], raw);
    EXPECT_EQ(record_for(interpreted, preview), nullptr);
    directory.block = kInvalidBlockId;
    interpreted = interpret_metadata_query(store, MetadataQueryKind::RawProcessing, options);
    ASSERT_EQ(interpreted.records.size(), 1U);
    EXPECT_EQ(interpreted.records[0].source_entries[0], unassigned);
    options.scope.directories = {};
    EXPECT_TRUE(interpret_metadata(store, options).records.empty());
    const MetadataConceptResult concepts = resolve_metadata_concepts(store, options);
    for (const MetadataConceptResolution& resolution : concepts.concepts)
        EXPECT_FALSE(resolution.found);
}

TEST(MetadataScopePropagation, GeometryKeepsItsCoordinateBasisAcrossServices)
{
    MetaStore store;
    const BlockId block = store.add_block({});
    const std::array<uint32_t, 2> origin { 5, 7 }, size { 100, 200 };
    const std::array<uint32_t, 4> active { 20, 30, 1020, 2030 };
    const EntryId origin_id = add_scoped(store, block, "ifd0", 0xc61fU, origin);
    ASSERT_NE(add_scoped(store, block, "ifd0", 0xc620U, size), kInvalidEntryId);
    ASSERT_NE(add_scoped(store, block, "ifd0", 0xc68dU, active), kInvalidEntryId);
    store.finalize();
    const MetadataInterpretationResult interpreted
        = interpret_metadata_query(store, MetadataQueryKind::Crop);
    bool saw_crop = false, saw_active = false;
    for (const MetadataInterpretationRecord& record : interpreted.records) {
        if (record.semantic == MetadataQuerySemanticKind::Crop) {
            EXPECT_EQ(record.source_entries[0], origin_id);
            EXPECT_EQ(record.coordinate_basis, MetadataQueryCoordinateBasis::ActiveAreaRelativeRaw);
            EXPECT_EQ(record.rect[0], 5.0);
            EXPECT_EQ(record.rect[1], 7.0);
            saw_crop = true;
        } else if (record.semantic == MetadataQuerySemanticKind::ActiveArea) {
            EXPECT_EQ(record.coordinate_basis, MetadataQueryCoordinateBasis::StoredImageRelativeRaw);
            saw_active = true;
        }
    }
    EXPECT_TRUE(saw_crop);
    EXPECT_TRUE(saw_active);
    const MetadataConceptResolution concepts
        = resolve_metadata_concept(store, MetadataConceptKind::Geometry);
    saw_crop = false;
    for (const MetadataConceptCandidate& candidate : concepts.candidates) {
        if (candidate.role != MetadataConceptRole::Crop) continue;
        EXPECT_EQ(candidate.coordinate_basis, MetadataQueryCoordinateBasis::ActiveAreaRelativeRaw);
        EXPECT_FALSE(candidate.components.empty());
        saw_crop = true;
    }
    EXPECT_TRUE(saw_crop);
}

}  // namespace openmeta

namespace openmeta {
TEST(MetadataScopePropagation, NonfiniteDirectConceptIsInvalidAndNeverPreferred)
{
    MetaStore store; const auto block=store.add_block({});
    Entry entry; entry.origin.block=block;
    entry.key=make_exif_tag_key(store.arena(),"gpsifd",0x0006U);
    entry.value=make_f64_bits(0x7ff8000000000001ULL);
    const auto id=store.add_entry(entry); store.finalize();
    const auto result=resolve_metadata_concept(store,MetadataConceptKind::Gps);
    ASSERT_EQ(result.candidates.size(),1U);
    EXPECT_TRUE(result.has_invalid_numeric_values);
    EXPECT_EQ(result.candidates[0].entry_id,id);
    EXPECT_EQ(result.candidates[0].numeric_status,MetadataQueryNumericStatus::Invalid);
    EXPECT_EQ(result.candidates[0].total_numeric_values,1U);
    EXPECT_TRUE(result.candidates[0].values.empty());
    EXPECT_FALSE(result.candidates[0].preferred);
    EXPECT_EQ(result.preferred_entry,kInvalidEntryId);
    EXPECT_EQ(store.entry(id).value.data.f64_bits,0x7ff8000000000001ULL);
}
}
