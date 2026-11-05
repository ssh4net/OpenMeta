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


if __name__ == "__main__":
    main()
