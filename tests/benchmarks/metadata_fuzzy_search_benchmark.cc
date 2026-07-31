// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_fuzzy_search.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace openmeta {
namespace {

    static constexpr const char* kCandidateNames[] = {
        "dc:Creator",
        "dc:Description",
        "photoshop:Headline",
        "dc:Keywords",
        "exif:ExposureTime",
        "exif:FNumber",
        "exif:PhotographicSensitivity",
        "exif:DateTimeOriginal",
        "exif:FocalLength",
        "tiff:Orientation",
        "crs:WhiteBalance",
        "crs:LensCorrection",
        "crs:ToneCurve",
        "dng:DefaultCropOrigin",
        "dng:DefaultCropSize",
        "dng:ActiveArea",
        "dng:BlackLevel",
        "dng:WhiteLevel",
        "dng:LinearizationTable",
        "photoshop:ColorProfile",
        "exif:ColorSpace",
        "tiff:Make",
        "tiff:Model",
        "exif:GPSLatitude",
        "exif:GPSLongitude",
        "xmpMM:History",
    };

    static constexpr const char* kQueries[] = {
        "cretaor",        "descripton",
        "hedline",        "keywrods",
        "expsoure time",  "apeture",
        "iso speed",      "date takn",
        "focal lenght",   "orentation",
        "white balnce",   "lens corection",
        "tone cruve",     "default crop orgin",
        "colour profile", "location longitude",
    };

    struct BenchmarkSize final {
        uint32_t entry_count;
        uint32_t repeat_count;
    };

    static constexpr BenchmarkSize kBenchmarkSizes[] = {
        { 256U, 16U },
        { 1024U, 8U },
        { 4096U, 4U },
        { 16384U, 2U },
    };

    static bool build_store(uint32_t entry_count, MetaStore* store)
    {
        if (!store) {
            return false;
        }

        char path_buffer[64] = {};
        for (uint32_t i = 0U; i < entry_count; ++i) {
            const bool use_search_name = (i % 4U) == 0U;
            std::string_view path;
            if (use_search_name) {
                const size_t name_index = static_cast<size_t>(i / 4U)
                                          % (sizeof(kCandidateNames)
                                             / sizeof(kCandidateNames[0]));
                path = kCandidateNames[name_index];
            } else {
                const int count = std::snprintf(path_buffer,
                                                sizeof(path_buffer),
                                                "bench:FieldNumber%u",
                                                static_cast<unsigned int>(i));
                if (count <= 0
                    || static_cast<size_t>(count) >= sizeof(path_buffer)) {
                    return false;
                }
                path = std::string_view(path_buffer,
                                        static_cast<size_t>(count));
            }

            Entry entry;
            entry.key = make_xmp_property_key(
                store->arena(), "urn:openmeta:fuzzy-search-benchmark", path);
            entry.value = make_text(store->arena(), "value",
                                    TextEncoding::Utf8);
            if (store->add_entry(entry) == kInvalidEntryId) {
                return false;
            }
        }
        store->finalize();
        return true;
    }

    static bool run_query_batch(const MetaStore& store, uint32_t repeat_count,
                                uint64_t* checksum)
    {
        if (!checksum) {
            return false;
        }

        MetadataFuzzySearchOptions options;
        options.minimum_score = 80U;
        options.max_results   = 8U;
        for (uint32_t repeat = 0U; repeat < repeat_count; ++repeat) {
            for (size_t query_index = 0U;
                 query_index < sizeof(kQueries) / sizeof(kQueries[0]);
                 ++query_index) {
                const MetadataFuzzySearchResult result
                    = fuzzy_search_metadata(store, kQueries[query_index],
                                            options);
                if (result.status != MetadataFuzzySearchStatus::Ok
                    || result.examined_entry_count
                           != static_cast<uint32_t>(store.entries().size())
                    || result.matches.empty()) {
                    return false;
                }
                *checksum += result.examined_entry_count;
                *checksum += result.qualified_match_count;
                *checksum += result.matches[0].score;
                *checksum += result.matches[0].entry_id;
            }
        }
        return true;
    }

    static bool run_size(const BenchmarkSize& size)
    {
        MetaStore store;
        if (!build_store(size.entry_count, &store)) {
            return false;
        }

        uint64_t checksum = 0U;
        if (!run_query_batch(store, 1U, &checksum)) {
            return false;
        }

        const std::chrono::steady_clock::time_point start
            = std::chrono::steady_clock::now();
        if (!run_query_batch(store, size.repeat_count, &checksum)) {
            return false;
        }
        const std::chrono::steady_clock::time_point end
            = std::chrono::steady_clock::now();

        const uint64_t query_count
            = static_cast<uint64_t>(size.repeat_count)
              * static_cast<uint64_t>(sizeof(kQueries) / sizeof(kQueries[0]));
        const uint64_t entry_query_count
            = static_cast<uint64_t>(size.entry_count) * query_count;
        const double elapsed_ms
            = std::chrono::duration<double, std::milli>(end - start).count();
        const double elapsed_seconds    = elapsed_ms / 1000.0;
        const double queries_per_second = elapsed_seconds > 0.0
                                              ? static_cast<double>(query_count)
                                                    / elapsed_seconds
                                              : 0.0;
        const double nanoseconds_per_entry_query
            = std::chrono::duration<double, std::nano>(end - start).count()
              / static_cast<double>(entry_query_count);

        std::printf("%u\t%llu\t%.3f\t%.1f\t%.2f\t%llu\n",
                    static_cast<unsigned int>(size.entry_count),
                    static_cast<unsigned long long>(query_count), elapsed_ms,
                    queries_per_second, nanoseconds_per_entry_query,
                    static_cast<unsigned long long>(checksum));
        return checksum != 0U;
    }

}  // namespace
}  // namespace openmeta

int
main()
{
    if (!openmeta::metadata_fuzzy_search_available()) {
        std::fprintf(stderr,
                     "RapidFuzz-backed metadata search is not available\n");
        return 2;
    }

    std::printf("entries\tqueries\telapsed_ms\tqueries_per_second"
                "\tns_per_entry_query\tchecksum\n");
    for (size_t i = 0U; i < sizeof(openmeta::kBenchmarkSizes)
                                / sizeof(openmeta::kBenchmarkSizes[0]);
         ++i) {
        if (!openmeta::run_size(openmeta::kBenchmarkSizes[i])) {
            std::fprintf(stderr, "benchmark workload validation failed\n");
            return 1;
        }
    }
    return 0;
}
