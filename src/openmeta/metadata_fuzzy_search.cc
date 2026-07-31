// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_fuzzy_search.h"

#include "openmeta/byte_arena.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/geotiff_key_names.h"
#include "openmeta/meta_flags.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
#    include <rapidfuzz/fuzz.hpp>
#endif

namespace openmeta {
namespace {

    struct SearchAlias final {
        std::string_view alias;
        std::string_view canonical;
    };

    static constexpr SearchAlias kSearchAliases[] = {
        { "author", "creator" },
        { "artist", "creator" },
        { "photographer", "creator" },
        { "byline", "creator" },
        { "caption", "description" },
        { "image caption", "description" },
        { "abstract", "description" },
        { "tags", "keywords" },
        { "tag list", "keywords" },
        { "photo credit", "credit" },
        { "license", "usage terms" },
        { "license terms", "usage terms" },
        { "shutter speed", "exposure time" },
        { "aperture", "f number" },
        { "iso", "photographic sensitivity" },
        { "iso speed", "photographic sensitivity" },
        { "exposure compensation", "exposure bias" },
        { "date taken", "date time original" },
        { "capture time", "date time original" },
        { "shooting date", "date time original" },
        { "rotation", "orientation" },
        { "image rotation", "orientation" },
        { "white balance setting", "white balance" },
        { "colour profile", "color profile" },
        { "camera profile", "color profile" },
        { "icc profile", "color profile" },
        { "colour space", "color space" },
        { "copyright owner", "copyright" },
        { "rights owner", "copyright" },
        { "crop rectangle", "crop" },
        { "crop offset", "default crop origin" },
        { "crop dimensions", "default crop size" },
        { "active rectangle", "active area" },
        { "sensor area", "active area" },
        { "raw black", "black level" },
        { "raw white", "white level" },
        { "linearisation table", "linearization table" },
        { "tone mapping curve", "tone curve" },
        { "lens corrections", "lens correction" },
        { "camera make", "make" },
        { "camera brand", "make" },
        { "manufacturer", "make" },
        { "camera model", "model" },
        { "camera serial", "serial number" },
        { "lens name", "lens model" },
        { "gps lat", "gps latitude" },
        { "gps lon", "gps longitude" },
        { "location latitude", "gps latitude" },
        { "location longitude", "gps longitude" },
        { "edit history", "history" },
    };

    struct SearchableKeyName final {
        std::string_view group;
        std::string_view name;
    };

    struct SearchScore final {
        MetadataFuzzySearchMatchKind kind = MetadataFuzzySearchMatchKind::Fuzzy;
        uint8_t score                     = 0U;
    };

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool ascii_is_upper(char c) noexcept { return c >= 'A' && c <= 'Z'; }

    static bool ascii_is_lower(char c) noexcept { return c >= 'a' && c <= 'z'; }

    static bool ascii_is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

    static bool ascii_is_alnum(char c) noexcept
    {
        return ascii_is_upper(c) || ascii_is_lower(c) || ascii_is_digit(c);
    }

    static char ascii_lower(char c) noexcept
    {
        if (ascii_is_upper(c)) {
            return static_cast<char>(c + ('a' - 'A'));
        }
        return c;
    }

    static void append_normalized_space(std::string* out)
    {
        if (!out || out->empty() || (*out)[out->size() - 1U] == ' ') {
            return;
        }
        out->push_back(' ');
    }

    static bool normalize_ascii(std::string_view text, bool reject_non_ascii,
                                std::string* out)
    {
        if (!out) {
            return false;
        }
        out->clear();
        out->reserve(text.size() + 8U);

        char previous = '\0';
        for (size_t i = 0U; i < text.size(); ++i) {
            const unsigned char byte = static_cast<unsigned char>(text[i]);
            if (byte >= 0x80U) {
                if (reject_non_ascii) {
                    out->clear();
                    return false;
                }
                append_normalized_space(out);
                previous = ' ';
                continue;
            }

            const char c = static_cast<char>(byte);
            if (!ascii_is_alnum(c)) {
                append_normalized_space(out);
                previous = ' ';
                continue;
            }
            if (ascii_is_upper(c)
                && (ascii_is_lower(previous) || ascii_is_digit(previous)
                    || (ascii_is_upper(previous) && i + 1U < text.size()
                        && ascii_is_lower(text[i + 1U])))) {
                append_normalized_space(out);
            }
            out->push_back(ascii_lower(c));
            previous = c;
        }
        while (!out->empty() && (*out)[out->size() - 1U] == ' ') {
            out->resize(out->size() - 1U);
        }
        return true;
    }

    static bool contains_normalized_phrase(std::string_view text,
                                           std::string_view phrase) noexcept
    {
        if (phrase.empty() || text.size() < phrase.size()) {
            return false;
        }
        const size_t limit = text.size() - phrase.size();
        for (size_t pos = 0U; pos <= limit; ++pos) {
            if (pos != 0U && text[pos - 1U] != ' ') {
                continue;
            }
            if (pos + phrase.size() < text.size()
                && text[pos + phrase.size()] != ' ') {
                continue;
            }
            if (text.substr(pos, phrase.size()) == phrase) {
                return true;
            }
        }
        return false;
    }

#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
    static uint8_t score_to_u8(double score) noexcept
    {
        if (score <= 0.0) {
            return 0U;
        }
        if (score >= 100.0) {
            return 100U;
        }
        return static_cast<uint8_t>(score + 0.5);
    }
#endif

    static uint8_t fuzzy_score(std::string_view candidate,
                               std::string_view query,
                               uint8_t minimum_score) noexcept
    {
#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
        if (candidate.empty() || query.empty()) {
            return 0U;
        }
        double best_score
            = rapidfuzz::fuzz::WRatio(candidate, query,
                                      static_cast<double>(minimum_score));
        size_t token_start = 0U;
        while (token_start < candidate.size()) {
            size_t token_end = token_start;
            while (token_end < candidate.size()) {
                if (candidate[token_end] == ' ') {
                    const double score = rapidfuzz::fuzz::ratio(
                        candidate.substr(token_start, token_end - token_start),
                        query, static_cast<double>(minimum_score));
                    if (score > best_score) {
                        best_score = score;
                    }
                }
                ++token_end;
            }
            const double score
                = rapidfuzz::fuzz::ratio(candidate.substr(token_start), query,
                                         static_cast<double>(minimum_score));
            if (score > best_score) {
                best_score = score;
            }
            const size_t next_space = candidate.find(' ', token_start);
            if (next_space == std::string_view::npos) {
                break;
            }
            token_start = next_space + 1U;
        }
        return score_to_u8(best_score);
#else
        (void)candidate;
        (void)query;
        (void)minimum_score;
        return 0U;
#endif
    }

    static uint8_t fuzzy_alias_score(std::string_view alias,
                                     std::string_view query,
                                     uint8_t minimum_score) noexcept
    {
#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
        if (alias.empty() || query.empty()) {
            return 0U;
        }
        const uint8_t full_score = score_to_u8(
            rapidfuzz::fuzz::ratio(alias, query,
                                   static_cast<double>(minimum_score)));
        if (full_score < minimum_score) {
            return 0U;
        }

        const uint8_t token_minimum_score = static_cast<uint8_t>(minimum_score
                                                                 - 10U);
        size_t alias_start                = 0U;
        size_t query_start                = 0U;
        while (alias_start < alias.size() && query_start < query.size()) {
            const size_t alias_end    = alias.find(' ', alias_start);
            const size_t query_end    = query.find(' ', query_start);
            const size_t alias_count  = alias_end == std::string_view::npos
                                            ? alias.size() - alias_start
                                            : alias_end - alias_start;
            const size_t query_count  = query_end == std::string_view::npos
                                            ? query.size() - query_start
                                            : query_end - query_start;
            const uint8_t token_score = score_to_u8(rapidfuzz::fuzz::ratio(
                alias.substr(alias_start, alias_count),
                query.substr(query_start, query_count),
                static_cast<double>(token_minimum_score)));
            if (token_score < token_minimum_score) {
                return 0U;
            }
            if (alias_end == std::string_view::npos
                || query_end == std::string_view::npos) {
                return alias_end == query_end ? full_score : 0U;
            }
            alias_start = alias_end + 1U;
            query_start = query_end + 1U;
        }
        return alias_start == alias.size() && query_start == query.size()
                   ? full_score
                   : 0U;
#else
        (void)alias;
        (void)query;
        (void)minimum_score;
        return 0U;
#endif
    }

    static int match_kind_priority(MetadataFuzzySearchMatchKind kind) noexcept
    {
        switch (kind) {
        case MetadataFuzzySearchMatchKind::Exact: return 3;
        case MetadataFuzzySearchMatchKind::Alias: return 2;
        case MetadataFuzzySearchMatchKind::Fuzzy: return 1;
        }
        return 0;
    }

    static bool score_precedes(const SearchScore& a,
                               const SearchScore& b) noexcept
    {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return match_kind_priority(a.kind) > match_kind_priority(b.kind);
    }

    static void note_score(SearchScore* best, MetadataFuzzySearchMatchKind kind,
                           uint8_t score) noexcept
    {
        if (!best || score == 0U) {
            return;
        }
        SearchScore candidate;
        candidate.kind  = kind;
        candidate.score = score;
        if (score_precedes(candidate, *best)) {
            *best = candidate;
        }
    }

    static SearchScore alias_score(std::string_view normalized_name,
                                   std::string_view normalized_group,
                                   std::string_view normalized_query,
                                   uint8_t minimum_score) noexcept
    {
        SearchScore best;
        for (size_t i = 0U;
             i < sizeof(kSearchAliases) / sizeof(kSearchAliases[0]); ++i) {
            if (!contains_normalized_phrase(normalized_name,
                                            kSearchAliases[i].canonical)
                && !contains_normalized_phrase(normalized_group,
                                               kSearchAliases[i].canonical)) {
                continue;
            }
            if (kSearchAliases[i].alias == normalized_query) {
                note_score(&best, MetadataFuzzySearchMatchKind::Alias, 96U);
                continue;
            }
            const uint8_t score = fuzzy_alias_score(kSearchAliases[i].alias,
                                                    normalized_query,
                                                    minimum_score);
            if (score >= minimum_score) {
                note_score(&best, MetadataFuzzySearchMatchKind::Alias,
                           score > 95U ? 95U : score);
            }
        }
        return best;
    }

    static SearchScore score_candidate(std::string_view normalized_name,
                                       std::string_view normalized_group,
                                       std::string_view normalized_query,
                                       uint8_t minimum_score) noexcept
    {
        SearchScore best;
        if (contains_normalized_phrase(normalized_name, normalized_query)
            || contains_normalized_phrase(normalized_group, normalized_query)) {
            best.kind  = MetadataFuzzySearchMatchKind::Exact;
            best.score = 100U;
            return best;
        }

        note_score(&best, MetadataFuzzySearchMatchKind::Fuzzy,
                   fuzzy_score(normalized_name, normalized_query,
                               minimum_score));
        note_score(&best, MetadataFuzzySearchMatchKind::Fuzzy,
                   fuzzy_score(normalized_group, normalized_query,
                               minimum_score));

        const SearchScore alias = alias_score(normalized_name, normalized_group,
                                              normalized_query, minimum_score);
        if (alias.score != 0U) {
            note_score(&best, alias.kind, alias.score);
        }
        return best;
    }

    static const char* iptc_dataset_name(uint16_t record,
                                         uint16_t dataset) noexcept
    {
        if (record != 2U) {
            return "";
        }
        switch (dataset) {
        case 5U: return "ObjectName";
        case 25U: return "Keywords";
        case 40U: return "Instructions";
        case 80U: return "By-line";
        case 85U: return "By-lineTitle";
        case 105U: return "Headline";
        case 110U: return "Credit";
        case 115U: return "Source";
        case 116U: return "CopyrightNotice";
        case 120U: return "Caption-Abstract";
        case 122U: return "CaptionWriter";
        case 130U: return "ImageType";
        case 131U: return "ImageOrientation";
        case 150U: return "AudioType";
        case 151U: return "AudioSamplingRate";
        case 152U: return "AudioSamplingResolution";
        case 153U: return "AudioDuration";
        case 154U: return "AudioOutcue";
        default: break;
        }
        return "";
    }

    static const char* icc_header_name(uint32_t offset) noexcept
    {
        switch (offset) {
        case 0U: return "ICCProfileSize";
        case 16U: return "ICCColorSpace";
        case 20U: return "ICCProfileConnectionSpace";
        default: break;
        }
        return "ICCProfileHeader";
    }

    static void icc_signature_name(uint32_t signature, char out[5]) noexcept
    {
        out[0] = static_cast<char>((signature >> 24U) & 0xffU);
        out[1] = static_cast<char>((signature >> 16U) & 0xffU);
        out[2] = static_cast<char>((signature >> 8U) & 0xffU);
        out[3] = static_cast<char>(signature & 0xffU);
        out[4] = '\0';
    }

    static SearchableKeyName searchable_key_name(const MetaStore& store,
                                                 const Entry& entry,
                                                 char scratch[5]) noexcept
    {
        SearchableKeyName out;
        switch (entry.key.kind) {
        case MetaKeyKind::ExifTag:
            out.group = arena_string(store.arena(),
                                     entry.key.data.exif_tag.ifd);
            out.name  = exif_entry_name(store, entry,
                                        ExifTagNamePolicy::ExifToolCompat);
            break;
        case MetaKeyKind::Comment:
            out.group = "comment";
            out.name  = "Comment";
            break;
        case MetaKeyKind::ExrAttribute:
            out.group = "exr";
            out.name  = arena_string(store.arena(),
                                     entry.key.data.exr_attribute.name);
            break;
        case MetaKeyKind::IptcDataset:
            out.group = "iptc";
            out.name  = iptc_dataset_name(entry.key.data.iptc_dataset.record,
                                          entry.key.data.iptc_dataset.dataset);
            break;
        case MetaKeyKind::XmpProperty:
            out.group = arena_string(store.arena(),
                                     entry.key.data.xmp_property.schema_ns);
            out.name  = arena_string(store.arena(),
                                     entry.key.data.xmp_property.property_path);
            break;
        case MetaKeyKind::IccHeaderField:
            out.group = "icc";
            out.name  = icc_header_name(entry.key.data.icc_header_field.offset);
            break;
        case MetaKeyKind::IccTag:
            out.group = "icc";
            icc_signature_name(entry.key.data.icc_tag.signature, scratch);
            out.name = std::string_view(scratch, 4U);
            break;
        case MetaKeyKind::PhotoshopIrb: out.group = "photoshop irb"; break;
        case MetaKeyKind::PhotoshopIrbField:
            out.group = "photoshop irb";
            out.name  = arena_string(store.arena(),
                                     entry.key.data.photoshop_irb_field.field);
            break;
        case MetaKeyKind::GeotiffKey:
            out.group = "geotiff";
            out.name  = geotiff_key_name(entry.key.data.geotiff_key.key_id);
            break;
        case MetaKeyKind::PrintImField:
            out.group = "printim";
            out.name  = arena_string(store.arena(),
                                     entry.key.data.printim_field.field);
            break;
        case MetaKeyKind::BmffField:
            out.group = "bmff";
            out.name  = arena_string(store.arena(),
                                     entry.key.data.bmff_field.field);
            break;
        case MetaKeyKind::JumbfField:
            out.group = "jumbf";
            out.name  = arena_string(store.arena(),
                                     entry.key.data.jumbf_field.field);
            break;
        case MetaKeyKind::JumbfCborKey:
            out.group = "jumbf cbor";
            out.name  = arena_string(store.arena(),
                                     entry.key.data.jumbf_cbor_key.key);
            break;
        case MetaKeyKind::PngText:
            out.group = arena_string(store.arena(),
                                     entry.key.data.png_text.keyword);
            out.name  = arena_string(store.arena(),
                                     entry.key.data.png_text.field);
            if (out.name.empty()) {
                out.name  = out.group;
                out.group = "png";
            }
            break;
        }
        return out;
    }

    static bool match_precedes(const MetadataFuzzySearchMatch& a,
                               const MetadataFuzzySearchMatch& b) noexcept
    {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        const int a_priority = match_kind_priority(a.match_kind);
        const int b_priority = match_kind_priority(b.match_kind);
        if (a_priority != b_priority) {
            return a_priority > b_priority;
        }
        return a.entry_id < b.entry_id;
    }

    static void insert_bounded_match(MetadataFuzzySearchResult* result,
                                     const MetadataFuzzySearchMatch& match,
                                     uint32_t max_results)
    {
        if (!result || max_results == 0U) {
            return;
        }

        size_t position = 0U;
        while (position < result->matches.size()
               && !match_precedes(match, result->matches[position])) {
            ++position;
        }
        if (result->matches.size() >= max_results
            && position >= result->matches.size()) {
            return;
        }
        if (result->matches.size() >= max_results) {
            result->matches.pop_back();
        }
        result->matches.insert(result->matches.begin()
                                   + static_cast<std::ptrdiff_t>(position),
                               match);
    }

}  // namespace

MetadataFuzzySearchResult
fuzzy_search_metadata(const MetaStore& store, std::string_view query,
                      const MetadataFuzzySearchOptions& options)
{
    MetadataFuzzySearchResult result;
    if (options.minimum_score < 50U || options.minimum_score > 100U
        || options.max_results == 0U
        || options.max_results > kMetadataFuzzySearchMaxResults) {
        result.status = MetadataFuzzySearchStatus::InvalidOptions;
        return result;
    }

    if (query.size() > kMetadataFuzzySearchMaxQueryBytes) {
        result.status = MetadataFuzzySearchStatus::QueryTooLong;
        return result;
    }
    std::string normalized_query;
    if (!normalize_ascii(query, true, &normalized_query)) {
        result.status = MetadataFuzzySearchStatus::UnsupportedQueryText;
        return result;
    }
    if (normalized_query.empty()) {
        result.status = MetadataFuzzySearchStatus::EmptyQuery;
        return result;
    }
    if (normalized_query.size() < 3U) {
        result.status = MetadataFuzzySearchStatus::QueryTooShort;
        return result;
    }
    if (!metadata_fuzzy_search_available()) {
        result.status = MetadataFuzzySearchStatus::FeatureUnavailable;
        return result;
    }

    result.matches.reserve(options.max_results);
    std::string normalized_name;
    std::string normalized_group;
    const std::span<const Entry> entries = store.entries();
    for (EntryId entry_id = 0U; entry_id < static_cast<EntryId>(entries.size());
         ++entry_id) {
        const Entry& entry = entries[entry_id];
        if (any(entry.flags, EntryFlags::Deleted)) {
            continue;
        }

        char scratch[5]                  = {};
        const SearchableKeyName key_name = searchable_key_name(store, entry,
                                                               scratch);
        if (key_name.group.empty() && key_name.name.empty()) {
            continue;
        }
        ++result.examined_entry_count;
        normalize_ascii(
            key_name.name.substr(0U, kMetadataFuzzySearchMaxCandidateBytes),
            false, &normalized_name);
        normalize_ascii(
            key_name.group.substr(0U, kMetadataFuzzySearchMaxCandidateBytes),
            false, &normalized_group);
        const SearchScore score
            = score_candidate(normalized_name, normalized_group,
                              normalized_query, options.minimum_score);
        if (score.score < options.minimum_score) {
            continue;
        }

        ++result.qualified_match_count;
        MetadataFuzzySearchMatch match;
        match.entry_id        = entry_id;
        match.key_kind        = entry.key.kind;
        match.match_kind      = score.kind;
        match.score           = score.score;
        match.group_truncated = key_name.group.size()
                                > kMetadataFuzzySearchMaxCandidateBytes;
        match.name_truncated = key_name.name.size()
                               > kMetadataFuzzySearchMaxCandidateBytes;
        const std::string_view result_group
            = key_name.group.substr(0U, kMetadataFuzzySearchMaxCandidateBytes);
        const std::string_view result_name
            = key_name.name.substr(0U, kMetadataFuzzySearchMaxCandidateBytes);
        match.group.assign(result_group.data(), result_group.size());
        match.name.assign(result_name.data(), result_name.size());
        insert_bounded_match(&result, match, options.max_results);
    }
    result.truncated = result.qualified_match_count > result.matches.size();
    return result;
}

bool
metadata_fuzzy_search_available() noexcept
{
#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
    return true;
#else
    return false;
#endif
}

const char*
metadata_fuzzy_search_status_name(MetadataFuzzySearchStatus status) noexcept
{
    switch (status) {
    case MetadataFuzzySearchStatus::Ok: return "ok";
    case MetadataFuzzySearchStatus::FeatureUnavailable:
        return "feature_unavailable";
    case MetadataFuzzySearchStatus::EmptyQuery: return "empty_query";
    case MetadataFuzzySearchStatus::QueryTooShort: return "query_too_short";
    case MetadataFuzzySearchStatus::QueryTooLong: return "query_too_long";
    case MetadataFuzzySearchStatus::UnsupportedQueryText:
        return "unsupported_query_text";
    case MetadataFuzzySearchStatus::InvalidOptions: return "invalid_options";
    }
    return "unknown";
}

const char*
metadata_fuzzy_search_match_kind_name(MetadataFuzzySearchMatchKind kind) noexcept
{
    switch (kind) {
    case MetadataFuzzySearchMatchKind::Exact: return "exact";
    case MetadataFuzzySearchMatchKind::Alias: return "alias";
    case MetadataFuzzySearchMatchKind::Fuzzy: return "fuzzy";
    }
    return "unknown";
}

}  // namespace openmeta
