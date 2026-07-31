// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/**
 * \file metadata_fuzzy_search.h
 * \brief Optional bounded fuzzy search over metadata names and property paths.
 */

namespace openmeta {

static constexpr uint32_t kMetadataFuzzySearchMaxResults        = 256U;
static constexpr uint32_t kMetadataFuzzySearchMaxQueryBytes     = 256U;
static constexpr uint32_t kMetadataFuzzySearchMaxCandidateBytes = 1024U;

enum class MetadataFuzzySearchStatus : uint8_t {
    Ok,
    FeatureUnavailable,
    EmptyQuery,
    QueryTooShort,
    QueryTooLong,
    UnsupportedQueryText,
    InvalidOptions,
};

enum class MetadataFuzzySearchMatchKind : uint8_t {
    /// Exact normalized phrase match.
    Exact,
    /// Exact or typo-tolerant match through the curated alias vocabulary.
    Alias,
    /// General RapidFuzz candidate-name match.
    Fuzzy,
};

struct MetadataFuzzySearchOptions final {
    /// Inclusive score cutoff in the range 50..100.
    uint8_t minimum_score = 80U;
    /// Bounded result count in the range 1..kMetadataFuzzySearchMaxResults.
    uint32_t max_results = 16U;
};

struct MetadataFuzzySearchMatch final {
    EntryId entry_id     = kInvalidEntryId;
    MetaKeyKind key_kind = MetaKeyKind::ExifTag;
    MetadataFuzzySearchMatchKind match_kind
        = MetadataFuzzySearchMatchKind::Fuzzy;
    uint8_t score        = 0U;
    bool group_truncated = false;
    bool name_truncated  = false;
    std::string group;
    std::string name;
};

struct MetadataFuzzySearchResult final {
    MetadataFuzzySearchStatus status = MetadataFuzzySearchStatus::Ok;
    uint32_t examined_entry_count    = 0U;
    uint32_t qualified_match_count   = 0U;
    bool truncated                   = false;
    std::vector<MetadataFuzzySearchMatch> matches;
};

/**
 * Searches decoded metadata names and property paths.
 *
 * Results are ordered by descending score, then match provenance
 * (exact, alias, fuzzy), then ascending stable entry id. The result count is
 * bounded by \ref MetadataFuzzySearchOptions::max_results.
 *
 * The current normalization contract is ASCII-only and locale-independent.
 * ASCII case and separators are normalized, including camel-case and
 * acronym-to-word boundaries.
 * Non-ASCII query text returns
 * \ref MetadataFuzzySearchStatus::UnsupportedQueryText; no Unicode
 * normalization or transliteration is attempted. Non-ASCII candidate bytes
 * act as separators while ASCII fragments remain searchable. Query and
 * candidate scanning are bounded by `kMetadataFuzzySearchMaxQueryBytes` and
 * `kMetadataFuzzySearchMaxCandidateBytes`.
 *
 * The function keeps no global state and is safe for concurrent calls when the
 * finalized input store is not being mutated.
 */
MetadataFuzzySearchResult
fuzzy_search_metadata(const MetaStore& store, std::string_view query,
                      const MetadataFuzzySearchOptions& options = {});

bool
metadata_fuzzy_search_available() noexcept;

const char*
metadata_fuzzy_search_status_name(MetadataFuzzySearchStatus status) noexcept;

const char*
metadata_fuzzy_search_match_kind_name(
    MetadataFuzzySearchMatchKind kind) noexcept;

}  // namespace openmeta
