// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_interpretation.h"

#include <cstddef>

namespace openmeta {
namespace {

    static void copy_pair_if_present(bool present, const double* src,
                                     bool* dst_present, double* dst) noexcept
    {
        if (!dst_present || !dst) {
            return;
        }
        *dst_present = present;
        if (!present || !src) {
            return;
        }
        dst[0] = src[0];
        dst[1] = src[1];
    }

    static void copy_quad_if_present(bool present, const double* src,
                                     bool* dst_present, double* dst) noexcept
    {
        if (!dst_present || !dst) {
            return;
        }
        *dst_present = present;
        if (!present || !src) {
            return;
        }
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
    }

    static MetadataInterpretationRecord
    make_record(MetadataQueryKind kind, const MetadataQueryCandidate& candidate)
    {
        MetadataInterpretationRecord out;
        out.query_kind     = kind;
        out.semantic       = candidate.semantic;
        out.shape          = candidate.normalized_shape;
        out.confidence     = candidate.confidence;
        out.source_entries = candidate.source_entries;
        out.total_numeric_values = candidate.total_numeric_values;
        out.numeric_status = candidate.numeric_status;
        out.components = candidate.components;
        out.coordinate_basis = candidate.coordinate_basis;

        copy_pair_if_present(candidate.has_origin, candidate.origin,
                             &out.has_origin, out.origin);
        copy_pair_if_present(candidate.has_size, candidate.size, &out.has_size,
                             out.size);
        copy_quad_if_present(candidate.has_rect, candidate.rect, &out.has_rect,
                             out.rect);
        copy_quad_if_present(candidate.has_margins, candidate.margins,
                             &out.has_margins, out.margins);

        out.has_values = candidate.has_values;
        if (candidate.has_values) {
            out.values = candidate.values;
        }
        return out;
    }

    static void append_query_records(MetadataQueryResult* query,
                                     MetadataInterpretationResult* out)
    {
        if (!query || !out) {
            return;
        }
        out->total_numeric_values += query->total_numeric_values;
        out->returned_numeric_values += query->returned_numeric_values;
        out->numeric_values_limited |= query->numeric_values_limited;
        out->has_invalid_numeric_values |= query->has_invalid_numeric_values;
        out->numeric_status = out->has_invalid_numeric_values
            ? MetadataQueryNumericStatus::Invalid
            : (out->numeric_values_limited ? MetadataQueryNumericStatus::Partial
               : (out->total_numeric_values != 0U ? MetadataQueryNumericStatus::Complete
                                                  : MetadataQueryNumericStatus::NotNumeric));
        out->records.reserve(out->records.size() + query->candidates.size());
        for (size_t i = 0U; i < query->candidates.size(); ++i) {
            const MetadataQueryCandidate& candidate = query->candidates[i];
            if (candidate.semantic == MetadataQuerySemanticKind::Unknown) {
                continue;
            }
            if (candidate.source_entries.empty()) {
                continue;
            }
            out->records.push_back(make_record(query->kind, candidate));
        }
    }

    static void append_kind_records(const MetaStore& store,
                                    MetadataQueryKind kind,
                                    const MetadataQueryOptions& options,
                                    MetadataInterpretationResult* out)
    {
        MetadataQueryOptions remaining = options;
        if (options.max_total_values != 0U) {
            if (out->returned_numeric_values >= options.max_total_values) {
                remaining.include_numeric_values = false;
            } else {
                remaining.max_total_values = options.max_total_values
                                             - out->returned_numeric_values;
            }
        }
        MetadataQueryResult query = query_metadata(store, kind, remaining);
        append_query_records(&query, out);
    }

}  // namespace

MetadataInterpretationResult
interpret_metadata_query(const MetaStore& store, MetadataQueryKind kind,
                         const MetadataQueryOptions& options)
{
    MetadataInterpretationResult out;
    append_kind_records(store, kind, options, &out);
    return out;
}

MetadataInterpretationResult
interpret_metadata(const MetaStore& store, const MetadataQueryOptions& options)
{
    MetadataInterpretationResult out;
    append_kind_records(store, MetadataQueryKind::Crop, options, &out);
    append_kind_records(store, MetadataQueryKind::Orientation, options, &out);
    append_kind_records(store, MetadataQueryKind::ExposureGain, options, &out);
    append_kind_records(store, MetadataQueryKind::WhiteBalance, options, &out);
    append_kind_records(store, MetadataQueryKind::Color, options, &out);
    append_kind_records(store, MetadataQueryKind::LensCorrection, options, &out);
    append_kind_records(store, MetadataQueryKind::RawProcessing, options, &out);
    append_kind_records(store, MetadataQueryKind::Descriptive, options, &out);
    return out;
}

}  // namespace openmeta
