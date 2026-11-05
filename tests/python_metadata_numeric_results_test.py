#!/usr/bin/env python3
"""Check bounded numeric metadata fields exposed by the Python API."""

import struct
import tempfile
from pathlib import Path

import openmeta


def _tiff_entry(tag, type_code, count, raw_value, data, data_offset):
    if len(raw_value) <= 4:
        value_or_offset = raw_value.ljust(4, b"\0")
    else:
        padding = (-(data_offset + len(data))) & 3
        data.extend(b"\0" * padding)
        value_or_offset = struct.pack("<I", data_offset + len(data))
        data.extend(raw_value)
    return struct.pack("<HHI", tag, type_code, count) + value_or_offset


def _u32_values(values):
    return struct.pack("<" + "I" * len(values), *values)


def _signed_rationals(values):
    return b"".join(struct.pack("<ii", numerator, denominator)
                    for numerator, denominator in values)


def _make_numeric_dng():
    linear_17 = [17] * 17
    linear_4096 = [41] * 4096
    matrix = [(1, 1), (0, 1), (0, 1),
              (0, 1), (1, 1), (0, 1),
              (0, 1), (0, 1), (1, 1)]
    entries = [
        (0xC612, 1, 4, b"\x01\x04\x00\x00"),
        (0xC618, 4, 17, _u32_values(linear_17)),
        (0xC618, 4, 4096, _u32_values(linear_4096)),
        (0xC61F, 4, 2, _u32_values([12, 34])),
        (0xC620, 4, 2, _u32_values([4000, 3000])),
        (0xC621, 10, 9, _signed_rationals(matrix)),
        (0xC68D, 4, 4, _u32_values([0, 0, 3000, 4000])),
    ]
    ifd_offset = 8
    ifd_end = ifd_offset + 2 + (12 * len(entries)) + 4
    data_offset = (ifd_end + 3) & ~3
    data = bytearray()
    encoded_entries = []
    for tag, type_code, count, raw_value in entries:
        encoded_entries.append(
            _tiff_entry(tag, type_code, count, raw_value, data, data_offset)
        )
    ifd = struct.pack("<H", len(entries)) + b"".join(encoded_entries)
    ifd += struct.pack("<I", 0)
    ifd += b"\0" * (data_offset - ifd_end)
    return b"II\x2a\x00" + struct.pack("<I", ifd_offset) + ifd + data


def _make_two_block_exif_dng():
    ifd0_offset = 8
    ifd0_entry_count = 3
    exif_a_entry_count = 4
    exif_b_entry_count = 5
    ifd0_size = 2 + 12 * ifd0_entry_count + 4
    exif_a_size = 2 + 12 * exif_a_entry_count + 4
    exif_b_size = 2 + 12 * exif_b_entry_count + 4
    exif_a_offset = ifd0_offset + ifd0_size
    exif_b_offset = exif_a_offset + exif_a_size
    data_offset = exif_b_offset + exif_b_size
    data = bytearray()

    ifd0_entries = [
        _tiff_entry(0x8769, 4, 1, struct.pack("<I", exif_a_offset),
                    data, data_offset),
        _tiff_entry(0x8769, 4, 1, struct.pack("<I", exif_b_offset),
                    data, data_offset),
        _tiff_entry(0xC612, 1, 4, b"\x01\x04\x00\x00", data, data_offset),
    ]

    exif_a_entries = [
        _tiff_entry(0x9003, 2, 20, b"2024:04:19 12:34:56\0", data,
                    data_offset),
        _tiff_entry(0x9011, 2, 7, b"+02:00\0", data, data_offset),
        _tiff_entry(0xC612, 1, 4, b"\x01\x04\x00\x00", data, data_offset),
        _tiff_entry(0xC618, 4, 2, _u32_values([11, 12]), data, data_offset),
    ]
    exif_b_entries = [
        _tiff_entry(0x9003, 2, 20, b"2025:04:19 12:34:56\0", data,
                    data_offset),
        _tiff_entry(0x9011, 2, 7, b"-03:00\0", data, data_offset),
        _tiff_entry(0x9291, 2, 4, b"222\0", data, data_offset),
        _tiff_entry(0xC612, 1, 4, b"\x01\x04\x00\x00", data, data_offset),
        _tiff_entry(0xC618, 4, 2, _u32_values([21, 22]), data, data_offset),
    ]

    ifd0 = struct.pack("<H", len(ifd0_entries)) + b"".join(ifd0_entries)
    ifd0 += struct.pack("<I", 0)
    exif_a = struct.pack("<H", len(exif_a_entries)) + b"".join(exif_a_entries)
    exif_a += struct.pack("<I", 0)
    exif_b = struct.pack("<H", len(exif_b_entries)) + b"".join(exif_b_entries)
    exif_b += struct.pack("<I", 0)
    return (b"II\x2a\x00" + struct.pack("<I", ifd0_offset) + ifd0
            + exif_a + exif_b + data)


def _find(items, key, value):
    for item in items:
        if item.get(key) == value:
            return item
    raise AssertionError(f"missing {key}={value!r}")


def main():
    with tempfile.TemporaryDirectory() as temp_dir:
        path = Path(temp_dir) / "numeric.dng"
        path.write_bytes(_make_numeric_dng())
        document = openmeta.read(str(path))

        raw = document.metadata_query(openmeta.MetadataQueryKind.RawProcessing)
        assert raw["numeric_status_name"] == "complete"
        assert raw["numeric_status"] == (
            openmeta.MetadataQueryNumericStatus.Complete
        )
        # This fixture also emits individual/grouped sensor geometry. Totals
        # describe all returned candidate vectors, rather than unique entries.
        assert raw["total_numeric_values"] == sum(
            item["total_numeric_values"] for item in raw["candidates"]
        )
        assert raw["returned_numeric_values"] == sum(
            len(item["values"]) for item in raw["candidates"]
        )
        assert raw["returned_numeric_values"] == raw["total_numeric_values"]
        assert raw["numeric_values_limited"] is False
        assert raw["has_invalid_numeric_values"] is False
        candidate_17 = _find(raw["candidates"], "total_numeric_values", 17)
        candidate_4096 = _find(raw["candidates"], "total_numeric_values", 4096)
        assert candidate_17["returned_numeric_values"] == 17
        assert candidate_4096["returned_numeric_values"] == 4096
        assert len(candidate_17["values"]) == 17
        assert len(candidate_4096["values"]) == 4096
        assert candidate_17["values"][0] == 17.0
        assert candidate_4096["values"][0] == 41.0
        assert candidate_4096["numeric_status_name"] == "complete"
        linear_component = candidate_17["components"][0]
        assert linear_component["role"] == (
            openmeta.MetadataQueryComponentRole.LinearizationTable
        )
        assert linear_component["source_entry"] in candidate_17[
            "source_entries"
        ]
        assert linear_component["tag"] == 0xC618
        assert linear_component["total_value_count"] == 17
        assert linear_component["returned_value_count"] == 17

        crop = document.query_crop_metadata()
        crop_candidate = _find(crop["candidates"], "semantic_name", "crop")
        assert crop_candidate["coordinate_basis_name"] == (
            "active_area_relative_raw"
        )
        assert crop_candidate["coordinate_basis"] == (
            openmeta.MetadataQueryCoordinateBasis.ActiveAreaRelativeRaw
        )
        assert crop_candidate["returned_numeric_values"] == 4
        assert len(crop_candidate["components"]) == 2
        assert crop_candidate["components"][0]["role_name"] == "default_crop_origin"

        interpreted = document.interpret_metadata_query(
            openmeta.MetadataQueryKind.Crop
        )
        assert interpreted["total_numeric_values"] >= 4
        assert interpreted["returned_numeric_values"] <= (
            interpreted["total_numeric_values"]
        )
        assert interpreted["numeric_values_limited"] is False
        assert interpreted["numeric_status_name"] == "complete"
        crop_record = _find(interpreted["records"], "semantic_name", "crop")
        assert crop_record["total_numeric_values"] == 4
        assert crop_record["returned_numeric_values"] == 4
        assert crop_record["coordinate_basis_name"] == (
            "active_area_relative_raw"
        )
        assert crop_record["components"][0]["source_entry"] in crop_record[
            "source_entries"
        ]

        geometry = document.resolve_metadata_concept(
            openmeta.MetadataConceptKind.Geometry
        )
        geometry_candidate = _find(
            geometry["candidates"], "coordinate_basis_name",
            "active_area_relative_raw",
        )
        assert isinstance(geometry_candidate["source_block"], int)
        assert geometry_candidate["source_block"] == 0
        assert geometry_candidate["components"]
        assert geometry["total_numeric_values"] >= 4
        assert geometry["returned_numeric_values"] <= (
            geometry["total_numeric_values"]
        )
        assert geometry["numeric_values_limited"] is False
        assert geometry["numeric_status_name"] == "complete"

        full_ccm = document.dng_ccm_fields()
        assert full_ccm["status"] == openmeta.CcmQueryStatus.Ok
        matrix_field = _find(full_ccm["fields"], "name", "ColorMatrix1")
        assert matrix_field["total_values"] == 9
        assert matrix_field["returned_values"] == 9
        assert matrix_field["values_complete"] is True
        assert isinstance(matrix_field["entry_id"], int)
        assert matrix_field["block"] == 0

        capped_ccm = document.dng_ccm_fields(max_values_per_field=4)
        capped_matrix = _find(capped_ccm["fields"], "name", "ColorMatrix1")
        assert capped_ccm["status"] == openmeta.CcmQueryStatus.LimitExceeded
        assert capped_matrix["total_values"] == 9
        assert capped_matrix["returned_values"] == 4
        assert len(capped_matrix["values"]) == 4
        assert capped_matrix["values_complete"] is False

        selected_scope = openmeta.MetadataQueryScope()
        selected_scope.enabled = True
        selected_scope.add_directory(0, "ifd0")
        ccm_scope_result = document.dng_ccm_fields(scope=selected_scope)
        assert _find(ccm_scope_result["fields"], "name", "ColorMatrix1")[
            "total_values"
        ] == 9
        empty_scope = openmeta.MetadataQueryScope()
        empty_scope.enabled = True
        empty_ccm = document.dng_ccm_fields(scope=empty_scope)
        assert empty_ccm["fields"] == []

        saved_selectors = selected_scope.directories
        assert len(saved_selectors) == 1
        saved_selectors[0].ifd = "saved-ifd"
        assert selected_scope.directories[0].ifd == "ifd0"
        selected_scope.clear_directories()
        del selected_scope
        assert saved_selectors[0].ifd == "saved-ifd"

        scoped_path = Path(temp_dir) / "scope.dng"
        scoped_path.write_bytes(_make_two_block_exif_dng())
        scoped_document = openmeta.read(str(scoped_path))
        dates = scoped_document.resolve_metadata_concept(
            openmeta.MetadataConceptKind.DateTime
        )
        created = [
            candidate for candidate in dates["candidates"]
            if candidate["role"] == openmeta.MetadataConceptRole.Created
        ]
        assert len(created) == 2
        by_year = {candidate["date_time"]["year"]: candidate
                   for candidate in created}
        created_a = by_year[2024]
        created_b = by_year[2025]
        block_a = created_a["source_block"]
        block_b = created_b["source_block"]
        assert block_a != block_b
        assert created_a["date_time"]["utc_offset_minutes"] == 120
        assert created_a["date_time"]["has_subsecond"] is False
        assert created_b["date_time"]["utc_offset_minutes"] == -180
        assert created_b["date_time"]["subsecond"] == "222"

        empty_options = openmeta.MetadataQueryOptions()
        empty_options.scope = empty_scope
        empty_dates = scoped_document.resolve_metadata_concept(
            openmeta.MetadataConceptKind.DateTime, options=empty_options
        )
        assert empty_dates["found"] is False
        assert empty_dates["candidates"] == []

        only_a_scope = openmeta.MetadataQueryScope()
        only_a_scope.enabled = True
        only_a_scope.inherit_across_directories = True
        only_a_scope.add_directory(block_a, "exififd")
        only_a_options = openmeta.MetadataQueryOptions()
        only_a_options.scope = only_a_scope
        only_a_dates = scoped_document.resolve_metadata_concept(
            openmeta.MetadataConceptKind.DateTime, options=only_a_options
        )
        only_a_created = _find(
            only_a_dates["candidates"], "role_name", "created"
        )
        assert len(only_a_dates["candidates"]) == 1
        assert only_a_created["source_block"] == block_a
        assert only_a_created["date_time"]["has_subsecond"] is False

        both_scope = openmeta.MetadataQueryScope()
        both_scope.enabled = True
        both_scope.add_directory(block_a, "exififd")
        both_scope.add_directory(block_b, "exififd")
        both_options = openmeta.MetadataQueryOptions()
        both_options.scope = both_scope
        both_dates = scoped_document.resolve_metadata_concept(
            openmeta.MetadataConceptKind.DateTime, options=both_options
        )
        both_by_year = {
            candidate["date_time"]["year"]: candidate
            for candidate in both_dates["candidates"]
            if candidate["role"] == openmeta.MetadataConceptRole.Created
        }
        assert len(both_by_year) == 2
        assert both_by_year[2024]["date_time"]["has_subsecond"] is False

        both_scope.inherit_across_directories = True
        inherited_options = openmeta.MetadataQueryOptions()
        inherited_options.scope = both_scope
        inherited_dates = scoped_document.resolve_metadata_concept(
            openmeta.MetadataConceptKind.DateTime, options=inherited_options
        )
        inherited_by_year = {
            candidate["date_time"]["year"]: candidate
            for candidate in inherited_dates["candidates"]
            if candidate["role"] == openmeta.MetadataConceptRole.Created
        }
        inherited_a = inherited_by_year[2024]
        inherited_b = inherited_by_year[2025]
        assert inherited_a["date_time"]["subsecond"] == "222"
        assert set(inherited_a["source_entries"]).intersection(
            inherited_b["source_entries"]
        )

        capped_options = openmeta.MetadataQueryOptions()
        capped_options.scope = both_scope
        capped_options.max_total_values = 1
        capped_raw = scoped_document.metadata_query(
            openmeta.MetadataQueryKind.RawProcessing,
            options=capped_options,
        )
        assert capped_raw["total_numeric_values"] >= 4
        assert capped_raw["returned_numeric_values"] == 1
        assert capped_raw["numeric_values_limited"] is True

        work_options = openmeta.MetadataQueryOptions()
        work_options.scope = both_scope
        work_options.max_semantic_work = 1
        all_concepts = scoped_document.resolve_metadata_concepts(work_options)
        assert all_concepts["semantic_work_used"] <= 1
        assert all_concepts["semantic_work_limited"] is True

        source_scope = openmeta.MetadataQueryScope()
        source_scope.enabled = True
        source_scope.add_directory(block_a, "exififd")
        copied_options = openmeta.MetadataQueryOptions()
        copied_options.scope = source_scope
        source_scope.clear_directories()
        del source_scope
        assert copied_options.scope.directory_count == 1
        copied_result = scoped_document.metadata_query(
            openmeta.MetadataQueryKind.RawProcessing,
            options=copied_options,
        )
        assert copied_result["total_numeric_values"] > 0

        scoped_snapshot = scoped_document.build_transfer_source_snapshot()
        document_query = scoped_document.metadata_query(
            openmeta.MetadataQueryKind.RawProcessing,
            options=both_options,
        )
        snapshot_query = scoped_snapshot.metadata_query(
            openmeta.MetadataQueryKind.RawProcessing,
            options=both_options,
        )
        assert document_query["total_numeric_values"] == (
            snapshot_query["total_numeric_values"]
        )

        document_interpretation = scoped_document.interpret_metadata_query(
            openmeta.MetadataQueryKind.RawProcessing,
            options=both_options,
        )
        snapshot_interpretation = scoped_snapshot.interpret_metadata_query(
            openmeta.MetadataQueryKind.RawProcessing,
            options=both_options,
        )
        assert document_interpretation["total_numeric_values"] == (
            snapshot_interpretation["total_numeric_values"]
        )

        raw_descriptor = openmeta.MetadataRawDataDescriptor()
        document_concepts = scoped_document.resolve_metadata_concepts(
            raw_descriptor, options=both_options
        )
        snapshot_concepts = scoped_snapshot.resolve_metadata_concepts(
            raw_descriptor, options=both_options
        )
        assert document_concepts["semantic_work_used"] == (
            snapshot_concepts["semantic_work_used"]
        )
        assert len(document_concepts["concepts"]) == len(
            snapshot_concepts["concepts"]
        )

        document_preflight = scoped_document.transfer_concept_diagnostics(
            openmeta.TransferSafetyMode.RenderedImage,
            options=both_options,
        )
        snapshot_preflight = scoped_snapshot.transfer_concept_diagnostics(
            openmeta.TransferSafetyMode.RenderedImage,
            raw_descriptor,
            both_options,
        )
        assert document_preflight["semantic_work_used"] <= (
            both_options.max_semantic_work
        )
        assert snapshot_preflight["semantic_work_used"] <= (
            both_options.max_semantic_work
        )

        counts_only = openmeta.MetadataQueryOptions()
        counts_only.scope = both_scope
        counts_only.include_numeric_values = False
        counts_only_result = scoped_snapshot.metadata_query(
            openmeta.MetadataQueryKind.RawProcessing,
            options=counts_only,
        )
        assert counts_only_result["total_numeric_values"] > 0
        assert counts_only_result["returned_numeric_values"] == 0


if __name__ == "__main__":
    main()
