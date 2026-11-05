// SPDX-License-Identifier: Apache-2.0

#include "openmeta/ccm_query.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <vector>
#include <span>

namespace openmeta {
namespace {

    static EntryId add_scoped_ccm(MetaStore& store, BlockId block,
                                  std::string_view ifd, uint16_t tag,
                                  const MetaValue& value)
    {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd, tag);
        entry.value = value;
        entry.origin.block = block;
        return store.add_entry(entry);
    }

    static const CcmField* find_field(const std::vector<CcmField>& fields,
                                      std::string_view ifd,
                                      std::string_view name) noexcept
    {
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].ifd == ifd && fields[i].name == name) {
                return &fields[i];
            }
        }
        return nullptr;
    }

    static bool has_issue(const std::vector<CcmIssue>& issues,
                          CcmIssueCode code) noexcept
    {
        for (size_t i = 0; i < issues.size(); ++i) {
            if (issues[i].code == code) {
                return true;
            }
        }
        return false;
    }

    static bool has_issue(const std::vector<CcmIssue>& issues,
                          CcmIssueSeverity severity, CcmIssueCode code) noexcept
    {
        for (size_t i = 0; i < issues.size(); ++i) {
            if (issues[i].severity == severity && issues[i].code == code) {
                return true;
            }
        }
        return false;
    }

    static uint32_t count_issue_code(const std::vector<CcmIssue>& issues,
                                     CcmIssueCode code) noexcept
    {
        uint32_t n = 0U;
        for (size_t i = 0; i < issues.size(); ++i) {
            if (issues[i].code == code) {
                n += 1U;
            }
        }
        return n;
    }

}  // namespace


TEST(CcmQuery, ScopeControlsDngContextAndExplicitInheritance)
{
    MetaStore store;
    const BlockId parent = store.add_block({}), raw = store.add_block({});
    const std::array<uint8_t, 4> version { 1, 7, 0, 0 };
    const std::array<uint32_t, 9> matrix { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    ASSERT_NE(add_scoped_ccm(store, parent, "ifd0", 0xc612,
                             make_u8_array(store.arena(), version)), kInvalidEntryId);
    ASSERT_NE(add_scoped_ccm(store, parent, "ifd0", 0xc65a, make_u16(21)), kInvalidEntryId);
    const EntryId matrix_id = add_scoped_ccm(store, raw, "subifd0", 0xc621,
                                             make_u32_array(store.arena(), matrix));
    store.finalize();
    std::vector<CcmField> fields;
    std::vector<CcmIssue> issues;
    CcmQueryOptions options;
    ASSERT_EQ(collect_dng_ccm_fields(store, &fields, options).status, CcmQueryStatus::Ok);
    ASSERT_EQ(fields.size(), 1U);
    EXPECT_EQ(fields[0].block, parent);
    const std::array<MetadataDirectorySelector, 2> directories {{{ parent, "ifd0" }, { raw, "subifd0" }}};
    options.scope.enabled = true;
    options.scope.directories = directories;
    options.scope.inherit_across_directories = true;
    ASSERT_EQ(collect_dng_ccm_fields(store, &fields, options, &issues).status, CcmQueryStatus::Ok);
    ASSERT_EQ(fields.size(), 2U);
    EXPECT_FALSE(has_issue(issues, CcmIssueCode::MissingCompanionTag));
    const CcmField* found = find_field(fields, "subifd0", "ColorMatrix1");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->entry_id, matrix_id);
    EXPECT_EQ(found->block, raw);
    options.scope.directories = std::span<const MetadataDirectorySelector>(directories).subspan(1U);
    EXPECT_EQ(collect_dng_ccm_fields(store, &fields, options).fields_found, 0U);
}

TEST(CcmQuery, DoesNotShareCompanionValidationAcrossBlocks)
{
    MetaStore store;
    const BlockId a = store.add_block({}), b = store.add_block({});
    const std::array<uint8_t, 4> version { 1, 7, 0, 0 };
    const std::array<uint32_t, 9> matrix { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    for (BlockId block : { a, b }) {
        ASSERT_NE(add_scoped_ccm(store, block, "ifd0", 0xc612,
                                 make_u8_array(store.arena(), version)), kInvalidEntryId);
        ASSERT_NE(add_scoped_ccm(store, block, "ifd0", 0xc621,
                                 make_u32_array(store.arena(), matrix)), kInvalidEntryId);
    }
    ASSERT_NE(add_scoped_ccm(store, a, "ifd0", 0xc65a, make_u16(21)), kInvalidEntryId);
    store.finalize();
    std::vector<CcmField> fields;
    std::vector<CcmIssue> issues;
    CcmQueryOptions options;
    ASSERT_EQ(collect_dng_ccm_fields(store, &fields, options, &issues).status, CcmQueryStatus::Ok);
    ASSERT_EQ(fields.size(), 3U);
    bool missing_b = false;
    for (const CcmIssue& issue : issues) {
        if (issue.code == CcmIssueCode::MissingCompanionTag) {
            EXPECT_EQ(issue.block, b);
            missing_b = true;
        }
    }
    EXPECT_TRUE(missing_b);
    const MetadataDirectorySelector selected { a, "ifd0" };
    options.scope.enabled = true;
    options.scope.directories = std::span<const MetadataDirectorySelector>(&selected, 1U);
    ASSERT_EQ(collect_dng_ccm_fields(store, &fields, options, &issues).fields_found, 2U);
    EXPECT_FALSE(has_issue(issues, CcmIssueCode::MissingCompanionTag));
    options.scope.directories = {};
    EXPECT_EQ(collect_dng_ccm_fields(store, &fields, options).fields_found, 0U);
}

TEST(CcmQuery, MarksLimitedValuesAndRejectsMalformedBacking)
{
    MetaStore store;
    const BlockId block = store.add_block({});
    const std::array<uint32_t, 9> matrix { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    const EntryId id = add_scoped_ccm(store, block, "ifd0", 0xc621,
                                      make_u32_array(store.arena(), matrix));
    store.finalize();
    CcmQueryOptions options;
    options.require_dng_context = false;
    options.limits.max_values_per_field = 4U;
    std::vector<CcmField> fields;
    ASSERT_EQ(collect_dng_ccm_fields(store, &fields, options).status, CcmQueryStatus::LimitExceeded);
    ASSERT_EQ(fields.size(), 1U);
    EXPECT_EQ(fields[0].entry_id, id);
    EXPECT_EQ(fields[0].total_values, 9U);
    EXPECT_FALSE(fields[0].values_complete);
    EXPECT_EQ(fields[0].values.size(), 4U);
    EXPECT_EQ(fields[0].rows, 3U);
    EXPECT_EQ(fields[0].cols, 3U);
    EXPECT_EQ(store.entry(id).value.count, 9U);
    MetaStore malformed;
    MetaValue short_value = make_u32_array(malformed.arena(), std::span<const uint32_t>(matrix).first(2U));
    short_value.count = 9U;
    ASSERT_NE(add_scoped_ccm(malformed, kInvalidBlockId, "ifd0", 0xc621, short_value), kInvalidEntryId);
    malformed.finalize();
    std::vector<CcmIssue> issues;
    (void)collect_dng_ccm_fields(malformed, &fields, options, &issues);
    EXPECT_TRUE(fields.empty());
    EXPECT_TRUE(has_issue(issues, CcmIssueCode::DecodeFailed));
}

TEST(CcmQuery, CollectsNormalizedFieldsInDngContext)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1, 6, 0, 0 };
    Entry dng_ver;
    dng_ver.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
    dng_ver.value        = make_u8_array(store.arena(),
                                         std::span<const uint8_t>(dng_version.data(),
                                                                  dng_version.size()));
    dng_ver.origin.block = block;
    dng_ver.origin.order_in_block = 0;
    (void)store.add_entry(dng_ver);

    const URational cm_values[9] = {
        { 1000, 1000 }, { 0, 1000 },    { 0, 1000 },
        { 0, 1000 },    { 1000, 1000 }, { 0, 1000 },
        { 0, 1000 },    { 0, 1000 },    { 1000, 1000 },
    };
    Entry cm1;
    cm1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value        = make_urational_array(store.arena(),
                                            std::span<const URational>(cm_values, 9));
    cm1.origin.block = block;
    cm1.origin.order_in_block = 1;
    (void)store.add_entry(cm1);

    const URational as_shot_neutral[3] = {
        { 1, 1 },
        { 1, 1 },
        { 1, 1 },
    };
    Entry asn;
    asn.key = make_exif_tag_key(store.arena(), "ifd0", 0xC628);
    asn.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(as_shot_neutral, 3));
    asn.origin.block          = block;
    asn.origin.order_in_block = 2;
    (void)store.add_entry(asn);

    store.finalize();

    std::vector<CcmField> fields;
    const CcmQueryResult r = collect_dng_ccm_fields(store, &fields);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_EQ(r.fields_found, fields.size());

    const CcmField* cm = find_field(fields, "ifd0", "ColorMatrix1");
    ASSERT_NE(cm, nullptr);
    EXPECT_EQ(cm->rows, 3U);
    EXPECT_EQ(cm->cols, 3U);
    ASSERT_EQ(cm->values.size(), 9U);
    EXPECT_DOUBLE_EQ(cm->values[0], 1.0);
    EXPECT_DOUBLE_EQ(cm->values[4], 1.0);
    EXPECT_DOUBLE_EQ(cm->values[8], 1.0);

    const CcmField* neutral = find_field(fields, "ifd0", "AsShotNeutral");
    ASSERT_NE(neutral, nullptr);
    EXPECT_EQ(neutral->rows, 1U);
    EXPECT_EQ(neutral->cols, 3U);
    ASSERT_EQ(neutral->values.size(), 3U);
    EXPECT_DOUBLE_EQ(neutral->values[0], 1.0);
    EXPECT_DOUBLE_EQ(neutral->values[1], 1.0);
    EXPECT_DOUBLE_EQ(neutral->values[2], 1.0);
}


TEST(CcmQuery, RespectsRequireContextAndLimits)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const URational cm_values[9] = {
        { 1000, 1000 }, { 0, 1000 },    { 0, 1000 },
        { 0, 1000 },    { 1000, 1000 }, { 0, 1000 },
        { 0, 1000 },    { 0, 1000 },    { 1000, 1000 },
    };
    Entry cm1;
    cm1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value        = make_urational_array(store.arena(),
                                            std::span<const URational>(cm_values, 9));
    cm1.origin.block = block;
    cm1.origin.order_in_block = 0;
    (void)store.add_entry(cm1);
    store.finalize();

    std::vector<CcmField> fields;
    CcmQueryOptions opts;
    opts.require_dng_context = true;
    CcmQueryResult r         = collect_dng_ccm_fields(store, &fields, opts);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_TRUE(fields.empty());

    opts.require_dng_context         = false;
    opts.limits.max_values_per_field = 4U;
    r = collect_dng_ccm_fields(store, &fields, opts);
    EXPECT_EQ(r.status, CcmQueryStatus::LimitExceeded);
    ASSERT_EQ(fields.size(), 1U);
    EXPECT_EQ(fields[0].values.size(), 4U);
}


TEST(CcmQuery, ValidationWarnsWithoutRejectingStructuredMismatches)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1, 7, 0, 0 };
    Entry dng_ver;
    dng_ver.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
    dng_ver.value        = make_u8_array(store.arena(),
                                         std::span<const uint8_t>(dng_version.data(),
                                                                  dng_version.size()));
    dng_ver.origin.block = block;
    dng_ver.origin.order_in_block = 0;
    (void)store.add_entry(dng_ver);

    const URational cm_values[8] = {
        { 1, 1 }, { 0, 1 }, { 0, 1 }, { 0, 1 },
        { 1, 1 }, { 0, 1 }, { 0, 1 }, { 1, 1 },
    };
    Entry cm1;
    cm1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value        = make_urational_array(store.arena(),
                                            std::span<const URational>(cm_values, 8));
    cm1.origin.block = block;
    cm1.origin.order_in_block = 1;
    (void)store.add_entry(cm1);

    const URational as_shot_neutral[3] = {
        { 1, 1 },
        { 1, 1 },
        { 1, 1 },
    };
    Entry asn;
    asn.key = make_exif_tag_key(store.arena(), "ifd0", 0xC628);
    asn.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(as_shot_neutral, 3));
    asn.origin.block          = block;
    asn.origin.order_in_block = 2;
    (void)store.add_entry(asn);

    const URational as_shot_xy[2] = { { 3127, 10000 }, { 3290, 10000 } };
    Entry asxy;
    asxy.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC629);
    asxy.value        = make_urational_array(store.arena(),
                                             std::span<const URational>(as_shot_xy, 2));
    asxy.origin.block = block;
    asxy.origin.order_in_block = 3;
    (void)store.add_entry(asxy);

    store.finalize();

    std::vector<CcmField> fields;
    std::vector<CcmIssue> issues;
    CcmQueryOptions opts;
    opts.validation_mode = CcmValidationMode::DngSpecWarnings;

    const CcmQueryResult r = collect_dng_ccm_fields(store, &fields, opts,
                                                    &issues);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_EQ(r.fields_found, fields.size());
    EXPECT_EQ(r.fields_dropped, 0U);
    ASSERT_FALSE(fields.empty());
    EXPECT_TRUE(has_issue(issues, CcmIssueCode::MatrixCountNotDivisibleBy3));
    EXPECT_TRUE(has_issue(issues, CcmIssueCode::AsShotConflict));
    EXPECT_GT(r.issues_reported, 0U);
}


TEST(CcmQuery, NonFiniteValuesAreDroppedAndReported)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1, 7, 0, 0 };
    Entry dng_ver;
    dng_ver.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
    dng_ver.value        = make_u8_array(store.arena(),
                                         std::span<const uint8_t>(dng_version.data(),
                                                                  dng_version.size()));
    dng_ver.origin.block = block;
    dng_ver.origin.order_in_block = 0;
    (void)store.add_entry(dng_ver);

    const uint64_t nan_bits[9] = {
        0x7FF8000000000000ULL, 0x3FF0000000000000ULL, 0x3FF0000000000000ULL,
        0x3FF0000000000000ULL, 0x3FF0000000000000ULL, 0x3FF0000000000000ULL,
        0x3FF0000000000000ULL, 0x3FF0000000000000ULL, 0x3FF0000000000000ULL,
    };
    Entry cm1;
    cm1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value        = make_f64_bits_array(store.arena(),
                                           std::span<const uint64_t>(nan_bits, 9));
    cm1.origin.block = block;
    cm1.origin.order_in_block = 1;
    (void)store.add_entry(cm1);

    store.finalize();

    std::vector<CcmField> fields;
    std::vector<CcmIssue> issues;
    CcmQueryOptions opts;
    opts.validation_mode = CcmValidationMode::DngSpecWarnings;

    const CcmQueryResult r = collect_dng_ccm_fields(store, &fields, opts,
                                                    &issues);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_TRUE(fields.empty());
    EXPECT_EQ(r.fields_found, 0U);
    EXPECT_EQ(r.fields_dropped, 1U);
    EXPECT_TRUE(has_issue(issues, CcmIssueSeverity::Error,
                          CcmIssueCode::NonFiniteValue));
}

TEST(CcmQuery, ValidationWarnsOnUnusuallyLargeMatrixCount)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1, 7, 0, 0 };
    Entry dng_ver;
    dng_ver.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
    dng_ver.value        = make_u8_array(store.arena(),
                                         std::span<const uint8_t>(dng_version.data(),
                                                                  dng_version.size()));
    dng_ver.origin.block = block;
    dng_ver.origin.order_in_block = 0;
    (void)store.add_entry(dng_ver);

    const URational cm_values[39] = {
        { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 },
        { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 },
        { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 },
        { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 },
        { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 },
        { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 },
    };
    Entry cm1;
    cm1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value        = make_urational_array(store.arena(),
                                            std::span<const URational>(cm_values, 39));
    cm1.origin.block = block;
    cm1.origin.order_in_block = 1;
    (void)store.add_entry(cm1);

    Entry cal1;
    cal1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC65A);
    cal1.value        = make_u16(21U);
    cal1.origin.block = block;
    cal1.origin.order_in_block = 2;
    (void)store.add_entry(cal1);

    store.finalize();

    std::vector<CcmField> fields;
    std::vector<CcmIssue> issues;
    CcmQueryOptions opts;
    opts.validation_mode = CcmValidationMode::DngSpecWarnings;

    const CcmQueryResult r = collect_dng_ccm_fields(store, &fields, opts,
                                                    &issues);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_FALSE(fields.empty());
    EXPECT_TRUE(has_issue(issues, CcmIssueCode::UnexpectedCount));
}

TEST(CcmQuery, ValidationWarnsOnIlluminantCodeAndWhiteXYRange)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1, 7, 0, 0 };
    Entry dng_ver;
    dng_ver.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
    dng_ver.value        = make_u8_array(store.arena(),
                                         std::span<const uint8_t>(dng_version.data(),
                                                                  dng_version.size()));
    dng_ver.origin.block = block;
    dng_ver.origin.order_in_block = 0;
    (void)store.add_entry(dng_ver);

    Entry cal1;
    cal1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC65A);
    cal1.value        = make_u16(500U);
    cal1.origin.block = block;
    cal1.origin.order_in_block = 1;
    (void)store.add_entry(cal1);

    const URational as_shot_xy[2] = { { 11, 10 }, { 1, 10 } };
    Entry asxy;
    asxy.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC629);
    asxy.value        = make_urational_array(store.arena(),
                                             std::span<const URational>(as_shot_xy, 2));
    asxy.origin.block = block;
    asxy.origin.order_in_block = 2;
    (void)store.add_entry(asxy);

    store.finalize();

    std::vector<CcmField> fields;
    std::vector<CcmIssue> issues;
    CcmQueryOptions opts;
    opts.validation_mode = CcmValidationMode::DngSpecWarnings;

    const CcmQueryResult r = collect_dng_ccm_fields(store, &fields, opts,
                                                    &issues);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_EQ(r.fields_found, fields.size());
    EXPECT_TRUE(has_issue(issues, CcmIssueCode::InvalidIlluminantCode));
    EXPECT_TRUE(has_issue(issues, CcmIssueCode::WhiteXYOutOfRange));
    EXPECT_GT(r.issues_reported, 0U);
}

TEST(CcmQuery, ValidationWarnsOnCrossFieldCountMismatches)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1, 7, 0, 0 };
    Entry dng_ver;
    dng_ver.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
    dng_ver.value        = make_u8_array(store.arena(),
                                         std::span<const uint8_t>(dng_version.data(),
                                                                  dng_version.size()));
    dng_ver.origin.block = block;
    dng_ver.origin.order_in_block = 0;
    (void)store.add_entry(dng_ver);

    const URational cm1_values[9] = {
        { 1, 1 }, { 0, 1 }, { 0, 1 }, { 0, 1 }, { 1, 1 },
        { 0, 1 }, { 0, 1 }, { 0, 1 }, { 1, 1 },
    };
    Entry cm1;
    cm1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value        = make_urational_array(store.arena(),
                                            std::span<const URational>(cm1_values, 9));
    cm1.origin.block = block;
    cm1.origin.order_in_block = 1;
    (void)store.add_entry(cm1);

    const URational cm2_values[12] = {
        { 1, 1 }, { 0, 1 }, { 0, 1 }, { 0, 1 }, { 1, 1 }, { 0, 1 },
        { 0, 1 }, { 0, 1 }, { 1, 1 }, { 1, 2 }, { 1, 3 }, { 1, 4 },
    };
    Entry cm2;
    cm2.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC622);
    cm2.value        = make_urational_array(store.arena(),
                                            std::span<const URational>(cm2_values, 12));
    cm2.origin.block = block;
    cm2.origin.order_in_block = 2;
    (void)store.add_entry(cm2);

    Entry cal2;
    cal2.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC65B);
    cal2.value        = make_u16(21U);
    cal2.origin.block = block;
    cal2.origin.order_in_block = 3;
    (void)store.add_entry(cal2);

    const URational analog_balance[4] = {
        { 1, 1 },
        { 1, 1 },
        { 1, 1 },
        { 1, 1 },
    };
    Entry ab;
    ab.key = make_exif_tag_key(store.arena(), "ifd0", 0xC627);
    ab.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(analog_balance, 4));
    ab.origin.block          = block;
    ab.origin.order_in_block = 4;
    (void)store.add_entry(ab);

    const URational as_shot_neutral[3] = {
        { 1, 1 },
        { 1, 1 },
        { 1, 1 },
    };
    Entry asn;
    asn.key = make_exif_tag_key(store.arena(), "ifd0", 0xC628);
    asn.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(as_shot_neutral, 3));
    asn.origin.block          = block;
    asn.origin.order_in_block = 5;
    (void)store.add_entry(asn);

    store.finalize();

    std::vector<CcmField> fields;
    std::vector<CcmIssue> issues;
    CcmQueryOptions opts;
    opts.validation_mode = CcmValidationMode::DngSpecWarnings;

    const CcmQueryResult r = collect_dng_ccm_fields(store, &fields, opts,
                                                    &issues);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_EQ(r.fields_found, fields.size());
    EXPECT_EQ(r.fields_dropped, 0U);

    const uint32_t unexpected_count_issues
        = count_issue_code(issues, CcmIssueCode::UnexpectedCount);
    EXPECT_GE(unexpected_count_issues, 2U);
    EXPECT_TRUE(has_issue(issues, CcmIssueCode::MissingCompanionTag));
    EXPECT_GT(r.issues_reported, 0U);
}

TEST(CcmQuery, ValidationWarnsWhenAsShotChannelCountDiffersFromColorMatrix1)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1, 7, 0, 0 };
    Entry dng_ver;
    dng_ver.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
    dng_ver.value        = make_u8_array(store.arena(),
                                         std::span<const uint8_t>(dng_version.data(),
                                                                  dng_version.size()));
    dng_ver.origin.block = block;
    dng_ver.origin.order_in_block = 0;
    (void)store.add_entry(dng_ver);

    Entry cal1;
    cal1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC65A);
    cal1.value        = make_u16(21U);
    cal1.origin.block = block;
    cal1.origin.order_in_block = 1;
    (void)store.add_entry(cal1);

    const URational cm1_values[12] = {
        { 1, 1 }, { 0, 1 }, { 0, 1 }, { 0, 1 }, { 1, 1 }, { 0, 1 },
        { 0, 1 }, { 0, 1 }, { 1, 1 }, { 1, 2 }, { 1, 3 }, { 1, 4 },
    };
    Entry cm1;
    cm1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value        = make_urational_array(store.arena(),
                                            std::span<const URational>(cm1_values, 12));
    cm1.origin.block = block;
    cm1.origin.order_in_block = 2;
    (void)store.add_entry(cm1);

    const URational as_shot_neutral[3] = {
        { 1, 1 },
        { 1, 1 },
        { 1, 1 },
    };
    Entry asn;
    asn.key = make_exif_tag_key(store.arena(), "ifd0", 0xC628);
    asn.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(as_shot_neutral, 3));
    asn.origin.block          = block;
    asn.origin.order_in_block = 3;
    (void)store.add_entry(asn);

    const URational analog_balance[5] = {
        { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 },
    };
    Entry ab;
    ab.key = make_exif_tag_key(store.arena(), "ifd0", 0xC627);
    ab.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(analog_balance, 5));
    ab.origin.block          = block;
    ab.origin.order_in_block = 4;
    (void)store.add_entry(ab);

    store.finalize();

    std::vector<CcmField> fields;
    std::vector<CcmIssue> issues;
    CcmQueryOptions opts;
    opts.validation_mode = CcmValidationMode::DngSpecWarnings;

    const CcmQueryResult r = collect_dng_ccm_fields(store, &fields, opts,
                                                    &issues);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_EQ(r.fields_found, fields.size());
    EXPECT_TRUE(has_issue(issues, CcmIssueCode::UnexpectedCount));
    EXPECT_GE(count_issue_code(issues, CcmIssueCode::UnexpectedCount), 2U);
}

}  // namespace openmeta

namespace openmeta {
TEST(CcmQuery, FourPlaneMatricesUseTheirDefinedShapes)
{
    MetaStore store;
    const std::array<uint32_t,12> matrix {1,0,0,0,1,0,0,0,1,1,1,1};
    const std::array<uint32_t,16> calibration {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    add_scoped_ccm(store,kInvalidBlockId,"ifd0",0xc621,make_u32_array(store.arena(),matrix));
    add_scoped_ccm(store,kInvalidBlockId,"ifd0",0xc623,make_u32_array(store.arena(),calibration));
    add_scoped_ccm(store,kInvalidBlockId,"ifd0",0xc714,make_u32_array(store.arena(),matrix));
    store.finalize();
    CcmQueryOptions options; options.require_dng_context=false;
    std::vector<CcmField> fields; std::vector<CcmIssue> issues;
    (void)collect_dng_ccm_fields(store,&fields,options,&issues);
    const CcmField* color=find_field(fields,"ifd0","ColorMatrix1");
    const CcmField* camera=find_field(fields,"ifd0","CameraCalibration1");
    const CcmField* forward=find_field(fields,"ifd0","ForwardMatrix1");
    ASSERT_NE(color,nullptr); ASSERT_NE(camera,nullptr); ASSERT_NE(forward,nullptr);
    EXPECT_EQ(color->rows,4U); EXPECT_EQ(color->cols,3U);
    EXPECT_EQ(camera->rows,4U); EXPECT_EQ(camera->cols,4U);
    EXPECT_EQ(forward->rows,3U); EXPECT_EQ(forward->cols,4U);
    EXPECT_FALSE(has_issue(issues,CcmIssueCode::UnexpectedCount));
}
}
