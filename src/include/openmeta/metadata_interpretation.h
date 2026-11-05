// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"
#include "openmeta/metadata_query.h"

#include <cstdint>
#include <vector>

/**
 * \file metadata_interpretation.h
 * \brief Structured metadata interpretation records built from semantic
 * query candidates.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/**
 * \brief One normalized interpretation record.
 *
 * Records preserve the source entries that produced the interpretation and use
 * the same semantic and shape vocabulary as metadata query. Numeric geometry is
 * normalized where available: rect is x, y, width, height; margins are left,
 * top, right, bottom.
 */
struct MetadataInterpretationRecord final {
    MetadataQueryKind query_kind       = MetadataQueryKind::Crop;
    MetadataQuerySemanticKind semantic = MetadataQuerySemanticKind::Unknown;
    MetadataQueryValueShape shape      = MetadataQueryValueShape::Unknown;
    uint8_t confidence                 = 0U;
    std::vector<EntryId> source_entries;
    uint64_t total_numeric_values = 0U;
    MetadataQueryNumericStatus numeric_status = MetadataQueryNumericStatus::NotNumeric;
    std::vector<MetadataQueryComponent> components;
    MetadataQueryCoordinateBasis coordinate_basis = MetadataQueryCoordinateBasis::Unknown;

    bool has_origin = false;
    double origin[2] {};

    bool has_size = false;
    double size[2] {};

    bool has_rect = false;
    double rect[4] {};

    bool has_margins = false;
    double margins[4] {};

    bool has_values = false;
    std::vector<double> values;
};

struct MetadataInterpretationResult final {
    std::vector<MetadataInterpretationRecord> records;
    uint64_t total_numeric_values = 0U;
    uint64_t returned_numeric_values = 0U;
    bool numeric_values_limited = false;
    bool has_invalid_numeric_values = false;
    MetadataQueryNumericStatus numeric_status = MetadataQueryNumericStatus::NotNumeric;
};

MetadataInterpretationResult
interpret_metadata_query(const MetaStore& store, MetadataQueryKind kind,
                         const MetadataQueryOptions& options = {});

MetadataInterpretationResult
interpret_metadata(const MetaStore& store,
                   const MetadataQueryOptions& options = {});

}  // namespace openmeta
OPENMETA_PUBLIC_END
