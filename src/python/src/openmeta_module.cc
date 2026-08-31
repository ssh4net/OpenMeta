// SPDX-License-Identifier: Apache-2.0

#include "openmeta/build_info.h"
#include "openmeta/ccm_query.h"
#include "openmeta/compatibility_dump.h"
#include "openmeta/console_format.h"
#include "openmeta/container_payload.h"
#include "openmeta/dng_sdk_adapter.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/exif_value_names.h"
#include "openmeta/exr_adapter.h"
#include "openmeta/geotiff_key_names.h"
#include "openmeta/icc_interpret.h"
#include "openmeta/interop_export.h"
#include "openmeta/libraw_adapter.h"
#include "openmeta/mapped_file.h"
#include "openmeta/metadata_capabilities.h"
#include "openmeta/metadata_concepts.h"
#include "openmeta/metadata_creation.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_fuzzy_search.h"
#include "openmeta/metadata_interpretation.h"
#include "openmeta/metadata_query.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_translation.h"
#include "openmeta/ocio_adapter.h"
#include "openmeta/orientation.h"
#include "openmeta/phaseone_geometry.h"
#include "openmeta/resource_policy.h"
#include "openmeta/simple_meta.h"
#include "openmeta/validate.h"
#include "openmeta/vendor_raw_processing.h"
#include "openmeta/xmp_dump.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace openmeta {
namespace {

    static nb::str sv_to_py(std::string_view s)
    {
        PyObject* decoded
            = PyUnicode_DecodeUTF8(s.data(), static_cast<Py_ssize_t>(s.size()),
                                   "surrogateescape");
        if (!decoded) {
            throw nb::python_error();
        }
        return nb::steal<nb::str>(nb::handle(decoded));
    }

    static const char*
    exif_tag_numeric_value_name_python(const std::string& ifd, uint16_t tag,
                                       uint64_t value) noexcept
    {
        return exif_tag_numeric_value_name(ifd, tag, value);
    }

    static std::string
    exif_tag_numeric_value_format_python(const std::string& ifd, uint16_t tag,
                                         uint64_t value)
    {
        char out[128];
        if (!exif_tag_numeric_value_format(ifd, tag, value, out, sizeof(out))) {
            return std::string();
        }
        return std::string(out);
    }


    static std::string exif_tag_byte_value_format_python(const std::string& ifd,
                                                         uint16_t tag,
                                                         nb::bytes value)
    {
        char* bytes       = nullptr;
        Py_ssize_t length = 0;
        if (PyBytes_AsStringAndSize(value.ptr(), &bytes, &length) != 0
            || bytes == nullptr || length < 0) {
            return std::string();
        }
        const std::span<const std::byte> span(
            reinterpret_cast<const std::byte*>(bytes),
            static_cast<std::size_t>(length));
        char out[128];
        if (!exif_tag_byte_value_format(ifd, tag, span, out, sizeof(out))) {
            return std::string();
        }
        return std::string(out);
    }


    static std::pair<std::string, std::string> info_lines()
    {
        std::string line1;
        std::string line2;
        format_build_info_lines(&line1, &line2);
        return { std::move(line1), std::move(line2) };
    }


    static std::pair<nb::bytes, XmpDumpResult>
    dump_xmp_sidecar_to_python(const MetaStore& store,
                               const XmpSidecarRequest& request)
    {
        std::vector<std::byte> out;
        XmpDumpResult res;
        {
            nb::gil_scoped_release gil_release;
            res = dump_xmp_sidecar(store, &out, request);
        }

        if (res.status != XmpDumpStatus::Ok) {
            throw std::runtime_error("XMP dump failed");
        }

        const size_t n = out.size();
        nb::bytes b(reinterpret_cast<const char*>(out.data()), n);
        return std::make_pair(b, res);
    }


    static XmpSidecarRequest make_xmp_sidecar_request(
        XmpSidecarFormat format, uint64_t max_output_bytes,
        uint32_t max_entries, bool include_exif, bool include_iptc,
        bool include_existing_xmp, bool portable_exiftool_gpsdatetime_alias,
        XmpExistingNamespacePolicy portable_existing_namespace_policy,
        XmpExistingStandardNamespacePolicy
            portable_existing_standard_namespace_policy,
        XmpConflictPolicy portable_conflict_policy, bool include_origin,
        bool include_wire, bool include_flags, bool include_names)
    {
        XmpSidecarRequest request;
        request.format                  = format;
        request.limits.max_output_bytes = max_output_bytes;
        request.limits.max_entries      = max_entries;
        request.include_exif            = include_exif;
        request.include_iptc            = include_iptc;
        request.include_existing_xmp    = include_existing_xmp;
        request.portable_existing_namespace_policy
            = portable_existing_namespace_policy;
        request.portable_existing_standard_namespace_policy
            = portable_existing_standard_namespace_policy;
        request.portable_conflict_policy = portable_conflict_policy;
        request.portable_exiftool_gpsdatetime_alias
            = portable_exiftool_gpsdatetime_alias;
        request.include_origin = include_origin;
        request.include_wire   = include_wire;
        request.include_flags  = include_flags;
        request.include_names  = include_names;
        return request;
    }


    static std::string python_info_line()
    {
        const char* ver = Py_GetVersion();
        size_t n        = 0;
        while (ver && ver[n] && ver[n] != ' ') {
            n += 1;
        }

        std::string out;
        out.reserve(96);
        out.append("Python ");
        if (ver && n != 0U) {
            out.append(ver, n);
        } else {
            out.append("unknown");
        }
        out.append(" nanobind ");

        char buf[64];
#if defined(NB_VERSION_DEV) && (NB_VERSION_DEV > 0)
        std::snprintf(buf, sizeof(buf), "%d.%d.%d-dev%d", NB_VERSION_MAJOR,
                      NB_VERSION_MINOR, NB_VERSION_PATCH, NB_VERSION_DEV);
#else
        std::snprintf(buf, sizeof(buf), "%d.%d.%d", NB_VERSION_MAJOR,
                      NB_VERSION_MINOR, NB_VERSION_PATCH);
#endif
        out.append(buf);
        return out;
    }


    static std::string arena_string(const ByteArena& arena,
                                    ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string(reinterpret_cast<const char*>(bytes.data()),
                           bytes.size());
    }


    static const char* exr_adapter_status_name(ExrAdapterStatus status) noexcept
    {
        switch (status) {
        case ExrAdapterStatus::Ok: return "ok";
        case ExrAdapterStatus::InvalidArgument: return "invalid_argument";
        case ExrAdapterStatus::Unsupported: return "unsupported";
        }
        return "unknown";
    }


    static const char*
    dng_sdk_adapter_status_name(DngSdkAdapterStatus status) noexcept
    {
        switch (status) {
        case DngSdkAdapterStatus::Ok: return "ok";
        case DngSdkAdapterStatus::InvalidArgument: return "invalid_argument";
        case DngSdkAdapterStatus::Unsupported: return "unsupported";
        case DngSdkAdapterStatus::Malformed: return "malformed";
        case DngSdkAdapterStatus::InternalError: return "internal_error";
        }
        return "unknown";
    }


    static const char*
    exif_orientation_status_name(ExifOrientationStatus status) noexcept
    {
        switch (status) {
        case ExifOrientationStatus::Ok: return "ok";
        case ExifOrientationStatus::InvalidArgument: return "invalid_argument";
        }
        return "unknown";
    }


    static const char*
    libraw_orientation_status_name(LibRawOrientationStatus status) noexcept
    {
        switch (status) {
        case LibRawOrientationStatus::Ok: return "ok";
        case LibRawOrientationStatus::InvalidArgument:
            return "invalid_argument";
        case LibRawOrientationStatus::Unsupported: return "unsupported";
        }
        return "unknown";
    }


    static const char*
    libraw_orientation_code_name(LibRawOrientationCode code) noexcept
    {
        switch (code) {
        case LibRawOrientationCode::None: return "none";
        case LibRawOrientationCode::PreviewPassThrough:
            return "preview_pass_through";
        case LibRawOrientationCode::MissingExifOrientationAssumedDefault:
            return "missing_exif_orientation_assumed_default";
        case LibRawOrientationCode::InvalidExifOrientation:
            return "invalid_exif_orientation";
        case LibRawOrientationCode::UnsupportedMirroredOrientation:
            return "unsupported_mirrored_orientation";
        case LibRawOrientationCode::MirroredOrientationDropped:
            return "mirrored_orientation_dropped";
        }
        return "unknown";
    }


    static const char*
    libraw_orientation_source_name(LibRawOrientationSource source) noexcept
    {
        switch (source) {
        case LibRawOrientationSource::ExplicitInput: return "explicit_input";
        case LibRawOrientationSource::AssumedDefault: return "assumed_default";
        case LibRawOrientationSource::ExifIfd0: return "exif_ifd0";
        case LibRawOrientationSource::XmpTiffOrientation:
            return "xmp_tiff_orientation";
        }
        return "unknown";
    }


    static const char*
    libraw_flip_to_exif_code_name(LibRawFlipToExifCode code) noexcept
    {
        switch (code) {
        case LibRawFlipToExifCode::None: return "none";
        case LibRawFlipToExifCode::PreviewPassThrough:
            return "preview_pass_through";
        case LibRawFlipToExifCode::InvalidLibRawFlip:
            return "invalid_libraw_flip";
        }
        return "unknown";
    }


    static const char* libraw_orientation_file_status_name(
        LibRawOrientationFileStatus status) noexcept
    {
        switch (status) {
        case LibRawOrientationFileStatus::Ok: return "ok";
        case LibRawOrientationFileStatus::InvalidArgument:
            return "invalid_argument";
        case LibRawOrientationFileStatus::OpenFailed: return "open_failed";
        case LibRawOrientationFileStatus::StatFailed: return "stat_failed";
        case LibRawOrientationFileStatus::TooLarge: return "too_large";
        case LibRawOrientationFileStatus::MapFailed: return "map_failed";
        case LibRawOrientationFileStatus::DecodeFailed: return "decode_failed";
        }
        return "unknown";
    }


    static const char* mapped_file_status_name(MappedFileStatus status) noexcept
    {
        switch (status) {
        case MappedFileStatus::Ok: return "ok";
        case MappedFileStatus::OpenFailed: return "open_failed";
        case MappedFileStatus::StatFailed: return "stat_failed";
        case MappedFileStatus::TooLarge: return "too_large";
        case MappedFileStatus::MapFailed: return "map_failed";
        }
        return "unknown";
    }


    static const char* scan_status_name(ScanStatus status) noexcept
    {
        switch (status) {
        case ScanStatus::Ok: return "ok";
        case ScanStatus::OutputTruncated: return "output_truncated";
        case ScanStatus::Unsupported: return "unsupported";
        case ScanStatus::Malformed: return "malformed";
        }
        return "unknown";
    }


    static const char* payload_status_name(PayloadStatus status) noexcept
    {
        switch (status) {
        case PayloadStatus::Ok: return "ok";
        case PayloadStatus::OutputTruncated: return "output_truncated";
        case PayloadStatus::Unsupported: return "unsupported";
        case PayloadStatus::Malformed: return "malformed";
        case PayloadStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static const char* exif_decode_status_name(ExifDecodeStatus status) noexcept
    {
        switch (status) {
        case ExifDecodeStatus::Ok: return "ok";
        case ExifDecodeStatus::OutputTruncated: return "output_truncated";
        case ExifDecodeStatus::Unsupported: return "unsupported";
        case ExifDecodeStatus::Malformed: return "malformed";
        case ExifDecodeStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static const char* xmp_decode_status_name(XmpDecodeStatus status) noexcept
    {
        switch (status) {
        case XmpDecodeStatus::Ok: return "ok";
        case XmpDecodeStatus::OutputTruncated: return "output_truncated";
        case XmpDecodeStatus::Unsupported: return "unsupported";
        case XmpDecodeStatus::Malformed: return "malformed";
        case XmpDecodeStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static const char* transfer_block_kind_name(TransferBlockKind kind) noexcept
    {
        switch (kind) {
        case TransferBlockKind::Exif: return "exif";
        case TransferBlockKind::Xmp: return "xmp";
        case TransferBlockKind::IptcIim: return "iptc_iim";
        case TransferBlockKind::PhotoshopIrb: return "photoshop_irb";
        case TransferBlockKind::Icc: return "icc";
        case TransferBlockKind::Jumbf: return "jumbf";
        case TransferBlockKind::C2pa: return "c2pa";
        case TransferBlockKind::ExrAttribute: return "exr_attribute";
        case TransferBlockKind::Other: return "other";
        }
        return "unknown";
    }


    static std::vector<std::byte> bytes_object_to_vector(nb::object obj)
    {
        std::vector<std::byte> out;
        if (obj.is_none()) {
            return out;
        }
        const nb::bytes value = nb::cast<nb::bytes>(obj);
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(value.data()), value.size());
        out.assign(bytes.begin(), bytes.end());
        return out;
    }


    static std::pair<std::string, bool> console_text(nb::bytes data,
                                                     uint32_t max_bytes)
    {
        const std::string_view s(reinterpret_cast<const char*>(data.data()),
                                 data.size());
        std::string out;
        const bool dangerous = append_console_escaped_ascii(s, max_bytes, &out);
        return { std::move(out), dangerous };
    }


    static std::string hex_bytes(nb::bytes data, uint32_t max_bytes)
    {
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(data.data()), data.size());
        std::string out;
        out.append("0x");
        append_hex_bytes(bytes, max_bytes, &out);
        return out;
    }


    static nb::str unsafe_text(nb::bytes data, uint32_t max_bytes)
    {
        size_t n = data.size();
        if (max_bytes != 0U && n > static_cast<size_t>(max_bytes)) {
            n = static_cast<size_t>(max_bytes);
        }
        PyObject* s
            = PyUnicode_DecodeLatin1(reinterpret_cast<const char*>(data.data()),
                                     static_cast<Py_ssize_t>(n), nullptr);
        if (!s) {
            nb::raise_python_error();
        }
        return nb::steal<nb::str>(nb::handle(s));
    }


    static std::vector<std::byte> read_file_bytes(const char* path,
                                                  uint64_t max_file_bytes)
    {
        if (!path || !*path) {
            throw std::runtime_error("empty path");
        }
        std::FILE* f = std::fopen(path, "rb");
        if (!f) {
            throw std::runtime_error("failed to open file");
        }

        std::vector<std::byte> bytes;
        if (std::fseek(f, 0, SEEK_END) != 0) {
            std::fclose(f);
            throw std::runtime_error("failed to seek file");
        }
        const long size_long = std::ftell(f);
        if (size_long < 0) {
            std::fclose(f);
            throw std::runtime_error("failed to stat file");
        }
        const uint64_t size = static_cast<uint64_t>(size_long);
        if (max_file_bytes != 0U && size > max_file_bytes) {
            std::fclose(f);
            throw std::runtime_error("file too large");
        }
        if (std::fseek(f, 0, SEEK_SET) != 0) {
            std::fclose(f);
            throw std::runtime_error("failed to rewind file");
        }

        bytes.resize(static_cast<size_t>(size));
        const size_t n = std::fread(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);

        if (n != bytes.size()) {
            throw std::runtime_error("failed to read file");
        }
        return bytes;
    }


    static nb::object scalar_to_python(const MetaValue& v)
    {
        switch (v.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64: return nb::int_(v.data.u64);
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64: return nb::int_(v.data.i64);
        case MetaElementType::F32: {
            const float f = std::bit_cast<float>(v.data.f32_bits);
            return nb::float_(static_cast<double>(f));
        }
        case MetaElementType::F64: {
            const double f = std::bit_cast<double>(v.data.f64_bits);
            return nb::float_(f);
        }
        case MetaElementType::URational:
            return nb::make_tuple(nb::int_(v.data.ur.numer),
                                  nb::int_(v.data.ur.denom));
        case MetaElementType::SRational:
            return nb::make_tuple(nb::int_(v.data.sr.numer),
                                  nb::int_(v.data.sr.denom));
        }
        return nb::none();
    }


    template<typename T>
    static std::span<const T> array_span(const ByteArena& arena,
                                         const MetaValue& v)
    {
        const std::span<const std::byte> bytes = arena.span(v.data.span);
        if (bytes.size() != static_cast<size_t>(v.count) * sizeof(T)) {
            return {};
        }
        return std::span<const T>(reinterpret_cast<const T*>(bytes.data()),
                                  static_cast<size_t>(v.count));
    }


    static nb::object value_to_python(const ByteArena& arena,
                                      const MetaValue& v, uint32_t max_elements,
                                      uint32_t max_bytes)
    {
        switch (v.kind) {
        case MetaValueKind::Empty: return nb::none();
        case MetaValueKind::Scalar: return scalar_to_python(v);
        case MetaValueKind::Text:
        case MetaValueKind::Bytes: {
            const std::span<const std::byte> bytes = arena.span(v.data.span);
            const size_t n = (max_bytes != 0U && bytes.size() > max_bytes)
                                 ? static_cast<size_t>(max_bytes)
                                 : bytes.size();
            return nb::bytes(reinterpret_cast<const char*>(bytes.data()), n);
        }
        case MetaValueKind::Array: break;
        }

        const uint32_t n = (max_elements != 0U && v.count > max_elements)
                               ? max_elements
                               : v.count;
        nb::list out;

        switch (v.elem_type) {
        case MetaElementType::U8: {
            const std::span<const uint8_t> s = array_span<uint8_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::I8: {
            const std::span<const int8_t> s = array_span<int8_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::U16: {
            const std::span<const uint16_t> s = array_span<uint16_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::I16: {
            const std::span<const int16_t> s = array_span<int16_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::U32: {
            const std::span<const uint32_t> s = array_span<uint32_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::I32: {
            const std::span<const int32_t> s = array_span<int32_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::U64: {
            const std::span<const uint64_t> s = array_span<uint64_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::I64: {
            const std::span<const int64_t> s = array_span<int64_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::F32: {
            const std::span<const uint32_t> s = array_span<uint32_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::float_(
                    static_cast<double>(std::bit_cast<float>(s[i]))));
            }
            break;
        }
        case MetaElementType::F64: {
            const std::span<const uint64_t> s = array_span<uint64_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::float_(std::bit_cast<double>(s[i])));
            }
            break;
        }
        case MetaElementType::URational: {
            const std::span<const URational> s = array_span<URational>(arena,
                                                                       v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(
                    nb::make_tuple(nb::int_(s[i].numer), nb::int_(s[i].denom)));
            }
            break;
        }
        case MetaElementType::SRational: {
            const std::span<const SRational> s = array_span<SRational>(arena,
                                                                       v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(
                    nb::make_tuple(nb::int_(s[i].numer), nb::int_(s[i].denom)));
            }
            break;
        }
        }

        return out;
    }


    class NameCollectSink final : public MetadataSink {
    public:
        explicit NameCollectSink(std::vector<std::string>* out) noexcept
            : out_(out)
        {
        }

        void on_item(const ExportItem& item) noexcept override
        {
            if (!out_) {
                return;
            }
            out_->emplace_back(item.name.data(), item.name.size());
        }

    private:
        std::vector<std::string>* out_ = nullptr;
    };


    static std::vector<std::string> export_names(const MetaStore& store,
                                                 const ExportOptions& options)
    {
        std::vector<std::string> out;
        NameCollectSink sink(&out);
        visit_metadata(store, options, sink);
        return out;
    }

    static std::string metadata_compatibility_dump_from_store(
        const MetaStore& store, ExportNameStyle style,
        ExportNamePolicy name_policy, bool include_values, bool include_origins,
        bool include_flags, uint32_t max_value_bytes)
    {
        MetadataCompatibilityDumpOptions options;
        options.style           = style;
        options.name_policy     = name_policy;
        options.include_values  = include_values;
        options.include_origins = include_origins;
        options.include_flags   = include_flags;
        options.max_value_bytes = max_value_bytes;

        std::string out;
        (void)dump_metadata_compatibility(store, options, &out);
        return out;
    }

    static std::string snapshot_compatibility_dump(
        const TransferSourceSnapshot& snapshot, ExportNameStyle style,
        ExportNamePolicy name_policy, bool include_values, bool include_origins,
        bool include_flags, uint32_t max_value_bytes)
    {
        return metadata_compatibility_dump_from_store(
            snapshot.store, style, name_policy, include_values, include_origins,
            include_flags, max_value_bytes);
    }

    static nb::dict ccm_field_to_python(const CcmField& field)
    {
        nb::dict d;
        d["name"] = nb::str(field.name.c_str(), field.name.size());
        d["ifd"]  = nb::str(field.ifd.c_str(), field.ifd.size());
        d["tag"]  = nb::int_(field.tag);
        d["rows"] = nb::int_(field.rows);
        d["cols"] = nb::int_(field.cols);
        nb::list values;
        for (size_t i = 0; i < field.values.size(); ++i) {
            values.append(nb::float_(field.values[i]));
        }
        d["values"] = std::move(values);
        return d;
    }

    static nb::dict ccm_issue_to_python(const CcmIssue& issue)
    {
        nb::dict d;
        d["severity"] = issue.severity;
        d["code"]     = issue.code;
        d["ifd"]      = nb::str(issue.ifd.c_str(), issue.ifd.size());
        d["name"]     = nb::str(issue.name.c_str(), issue.name.size());
        d["tag"]      = nb::int_(issue.tag);
        d["message"]  = nb::str(issue.message.c_str(), issue.message.size());
        return d;
    }

    static nb::dict collect_dng_ccm_to_python(const MetaStore& store,
                                              bool require_dng_context,
                                              bool include_reduction_matrices,
                                              uint32_t max_fields,
                                              uint32_t max_values_per_field,
                                              CcmValidationMode validation_mode)
    {
        CcmQueryOptions options;
        options.require_dng_context         = require_dng_context;
        options.include_reduction_matrices  = include_reduction_matrices;
        options.validation_mode             = validation_mode;
        options.limits.max_fields           = max_fields;
        options.limits.max_values_per_field = max_values_per_field;

        std::vector<CcmField> fields;
        std::vector<CcmIssue> issues;
        const CcmQueryResult result = collect_dng_ccm_fields(store, &fields,
                                                             options, &issues);

        nb::list out_fields;
        for (size_t i = 0; i < fields.size(); ++i) {
            out_fields.append(ccm_field_to_python(fields[i]));
        }
        nb::list out_issues;
        for (size_t i = 0; i < issues.size(); ++i) {
            out_issues.append(ccm_issue_to_python(issues[i]));
        }

        nb::dict out;
        out["status"]          = result.status;
        out["fields_found"]    = nb::int_(result.fields_found);
        out["fields_dropped"]  = nb::int_(result.fields_dropped);
        out["issues_reported"] = nb::int_(result.issues_reported);
        out["fields"]          = std::move(out_fields);
        out["issues"]          = std::move(out_issues);
        return out;
    }

    static nb::list phaseone_double_array_to_python(const double* values,
                                                    uint32_t count)
    {
        nb::list out;
        if (!values) {
            return out;
        }
        for (uint32_t i = 0U; i < count; ++i) {
            out.append(nb::float_(values[i]));
        }
        return out;
    }

    static nb::object
    phaseone_optional_double_array_to_python(bool present, const double* values,
                                             uint32_t count)
    {
        if (!present) {
            return nb::none();
        }
        return phaseone_double_array_to_python(values, count);
    }

    static nb::dict phaseone_raw_geometry_to_python(const MetaStore& store)
    {
        const PhaseOneRawGeometryResult result
            = phaseone_raw_geometry_from_store(store);
        const PhaseOneRawGeometry& geometry = result.geometry;

        nb::dict out;
        out["status"]      = result.status;
        out["status_name"] = nb::str(
            phaseone_raw_geometry_status_name(result.status));
        out["sensor_width"]       = nb::int_(geometry.sensor_width);
        out["sensor_height"]      = nb::int_(geometry.sensor_height);
        out["sensor_left_margin"] = nb::int_(geometry.sensor_left_margin);
        out["sensor_top_margin"]  = nb::int_(geometry.sensor_top_margin);
        out["image_width"]        = nb::int_(geometry.image_width);
        out["image_height"]       = nb::int_(geometry.image_height);
        out["active_x"]           = nb::int_(geometry.active_x);
        out["active_y"]           = nb::int_(geometry.active_y);
        out["active_width"]       = nb::int_(geometry.active_width);
        out["active_height"]      = nb::int_(geometry.active_height);
        out["right_margin"]       = nb::int_(geometry.right_margin);
        out["bottom_margin"]      = nb::int_(geometry.bottom_margin);
        return out;
    }

    static nb::dict phaseone_raw_processing_to_python(const MetaStore& store)
    {
        const PhaseOneRawProcessingResult result
            = phaseone_raw_processing_from_store(store);
        const PhaseOneRawProcessingInfo& info = result.info;

        nb::dict out;
        out["status"]      = result.status;
        out["status_name"] = nb::str(
            phaseone_raw_processing_status_name(result.status));
        out["fields_seen"]    = nb::int_(result.fields_seen);
        out["fields_decoded"] = nb::int_(result.fields_decoded);
        out["invalid_fields"] = nb::int_(result.invalid_fields);

        out["has_color_matrix1"] = nb::bool_(info.has_color_matrix1);
        out["color_matrix1"]
            = phaseone_optional_double_array_to_python(info.has_color_matrix1,
                                                       info.color_matrix1, 9U);
        out["has_color_matrix2"] = nb::bool_(info.has_color_matrix2);
        out["color_matrix2"]
            = phaseone_optional_double_array_to_python(info.has_color_matrix2,
                                                       info.color_matrix2, 9U);
        out["has_wb_rgb_levels"] = nb::bool_(info.has_wb_rgb_levels);
        out["wb_rgb_levels"]
            = phaseone_optional_double_array_to_python(info.has_wb_rgb_levels,
                                                       info.wb_rgb_levels, 3U);

        out["has_black_level"]          = nb::bool_(info.has_black_level);
        out["black_level"]              = nb::int_(info.black_level);
        out["has_sensor_temperature_c"] = nb::bool_(
            info.has_sensor_temperature_c);
        out["sensor_temperature_c"] = nb::float_(info.sensor_temperature_c);
        out["has_sensor_temperature2_c"] = nb::bool_(
            info.has_sensor_temperature2_c);
        out["sensor_temperature2_c"]  = nb::float_(info.sensor_temperature2_c);
        out["has_raw_format"]         = nb::bool_(info.has_raw_format);
        out["raw_format"]             = nb::int_(info.raw_format);
        out["has_raw_data"]           = nb::bool_(info.has_raw_data);
        out["raw_data_bytes"]         = nb::int_(info.raw_data_bytes);
        out["has_strip_offsets"]      = nb::bool_(info.has_strip_offsets);
        out["strip_offsets_bytes"]    = nb::int_(info.strip_offsets_bytes);
        out["has_black_level_data"]   = nb::bool_(info.has_black_level_data);
        out["black_level_data_bytes"] = nb::int_(info.black_level_data_bytes);
        out["has_sensor_calibration"] = nb::bool_(info.has_sensor_calibration);
        out["sensor_calibration_entry_count"] = nb::int_(
            info.sensor_calibration_entry_count);
        out["sensor_calibration_payload_bytes"] = nb::int_(
            info.sensor_calibration_payload_bytes);
        out["has_sensor_defects"]   = nb::bool_(info.has_sensor_defects);
        out["sensor_defects_bytes"] = nb::int_(info.sensor_defects_bytes);
        out["has_flat_field"]       = nb::bool_(info.has_flat_field);
        out["flat_field_bytes"]     = nb::int_(info.flat_field_bytes);
        out["has_linearization_coefficients"] = nb::bool_(
            info.has_linearization_coefficients);
        out["linearization_coefficients_count"] = nb::int_(
            info.linearization_coefficients_count);
        return out;
    }

    static nb::dict vendor_raw_processing_summary_to_python(
        VendorRawProcessingFamily family,
        const VendorRawProcessingSummary& summary)
    {
        nb::dict out;
        out["family"]      = family;
        out["family_name"] = nb::str(vendor_raw_processing_family_name(family));
        out["fields_seen"] = nb::int_(summary.fields_seen);
        out["color_fields"]           = nb::int_(summary.color_fields);
        out["white_balance_fields"]   = nb::int_(summary.white_balance_fields);
        out["geometry_fields"]        = nb::int_(summary.geometry_fields);
        out["storage_fields"]         = nb::int_(summary.storage_fields);
        out["lens_correction_fields"] = nb::int_(
            summary.lens_correction_fields);
        out["raw_data_fields"]      = nb::int_(summary.raw_data_fields);
        out["sensor_fields"]        = nb::int_(summary.sensor_fields);
        out["private_table_fields"] = nb::int_(summary.private_table_fields);
        out["preview_fields"]       = nb::int_(summary.preview_fields);
        out["face_geometry_fields"] = nb::int_(summary.face_geometry_fields);
        out["computational_fields"] = nb::int_(summary.computational_fields);
        out["thermal_fields"]       = nb::int_(summary.thermal_fields);
        out["stitch_fields"]        = nb::int_(summary.stitch_fields);
        return out;
    }

    static nb::dict
    vendor_raw_processing_to_python(const MetaStore& store,
                                    VendorRawProcessingFamily family)
    {
        const VendorRawProcessingSummary summary
            = vendor_raw_processing_from_store(store, family);
        return vendor_raw_processing_summary_to_python(family, summary);
    }

    static nb::list
    metadata_query_entry_ids_to_python(const std::vector<EntryId>& entries)
    {
        nb::list out;
        for (size_t i = 0U; i < entries.size(); ++i) {
            out.append(nb::int_(entries[i]));
        }
        return out;
    }

    static nb::list
    metadata_query_values_to_python(const std::vector<double>& values)
    {
        nb::list out;
        for (size_t i = 0U; i < values.size(); ++i) {
            out.append(nb::float_(values[i]));
        }
        return out;
    }

    static nb::object
    metadata_query_optional_pair_to_python(bool present, const double* values)
    {
        if (!present || !values) {
            return nb::none();
        }
        nb::list out;
        out.append(nb::float_(values[0]));
        out.append(nb::float_(values[1]));
        return out;
    }

    static nb::object
    metadata_query_optional_rect_to_python(bool present, const double* values)
    {
        if (!present || !values) {
            return nb::none();
        }
        nb::list out;
        for (uint32_t i = 0U; i < 4U; ++i) {
            out.append(nb::float_(values[i]));
        }
        return out;
    }

    static nb::object
    metadata_query_optional_margins_to_python(bool present,
                                              const double* values)
    {
        if (!present || !values) {
            return nb::none();
        }
        nb::list out;
        for (uint32_t i = 0U; i < 4U; ++i) {
            out.append(nb::float_(values[i]));
        }
        return out;
    }

    static nb::dict
    metadata_query_match_to_python(const MetadataQueryMatch& match)
    {
        nb::dict out;
        out["entry_id"]      = nb::int_(match.entry_id);
        out["key_kind"]      = match.key_kind;
        out["semantic"]      = match.semantic;
        out["semantic_name"] = nb::str(
            metadata_query_semantic_kind_name(match.semantic));
        out["shape"]      = match.shape;
        out["shape_name"] = nb::str(
            metadata_query_value_shape_name(match.shape));
        out["confidence"]    = nb::int_(match.confidence);
        out["matched_terms"] = nb::int_(match.matched_terms);
        out["exact_match"]   = nb::bool_(match.exact_match);
        out["fuzzy_match"]   = nb::bool_(match.fuzzy_match);
        out["fuzzy_score"]   = nb::int_(match.fuzzy_score);
        out["exif_tag"]      = nb::int_(match.exif_tag);
        out["group"]         = sv_to_py(match.group);
        out["name"]          = sv_to_py(match.name);
        return out;
    }

    static nb::dict
    metadata_query_candidate_to_python(const MetadataQueryCandidate& candidate)
    {
        nb::dict out;
        out["semantic"]      = candidate.semantic;
        out["semantic_name"] = nb::str(
            metadata_query_semantic_kind_name(candidate.semantic));
        out["normalized_shape"]      = candidate.normalized_shape;
        out["normalized_shape_name"] = nb::str(
            metadata_query_value_shape_name(candidate.normalized_shape));
        out["confidence"]     = nb::int_(candidate.confidence);
        out["source_entries"] = metadata_query_entry_ids_to_python(
            candidate.source_entries);
        out["has_origin"] = nb::bool_(candidate.has_origin);
        out["origin"]
            = metadata_query_optional_pair_to_python(candidate.has_origin,
                                                     candidate.origin);
        out["has_size"] = nb::bool_(candidate.has_size);
        out["size"] = metadata_query_optional_pair_to_python(candidate.has_size,
                                                             candidate.size);
        out["has_rect"] = nb::bool_(candidate.has_rect);
        out["rect"] = metadata_query_optional_rect_to_python(candidate.has_rect,
                                                             candidate.rect);
        out["has_margins"] = nb::bool_(candidate.has_margins);
        out["margins"]
            = metadata_query_optional_margins_to_python(candidate.has_margins,
                                                        candidate.margins);
        out["has_values"] = nb::bool_(candidate.has_values);
        if (candidate.has_values) {
            out["values"] = metadata_query_values_to_python(candidate.values);
        } else {
            out["values"] = nb::none();
        }
        return out;
    }

    static nb::dict
    metadata_query_result_to_python(const MetadataQueryResult& result)
    {
        nb::list matches;
        for (size_t i = 0U; i < result.matches.size(); ++i) {
            matches.append(metadata_query_match_to_python(result.matches[i]));
        }
        nb::list candidates;
        for (size_t i = 0U; i < result.candidates.size(); ++i) {
            candidates.append(
                metadata_query_candidate_to_python(result.candidates[i]));
        }

        nb::dict out;
        out["kind"]       = result.kind;
        out["kind_name"]  = nb::str(metadata_query_kind_name(result.kind));
        out["matches"]    = std::move(matches);
        out["candidates"] = std::move(candidates);
        return out;
    }

    static nb::dict metadata_query_to_python(const MetaStore& store,
                                             MetadataQueryKind kind)
    {
        const MetadataQueryResult result = query_metadata(store, kind);
        return metadata_query_result_to_python(result);
    }

    static nb::dict metadata_fuzzy_search_to_python(const MetaStore& store,
                                                    const std::string& query,
                                                    uint8_t minimum_score,
                                                    uint32_t max_results)
    {
        MetadataFuzzySearchOptions options;
        options.minimum_score = minimum_score;
        options.max_results   = max_results;
        const MetadataFuzzySearchResult result
            = fuzzy_search_metadata(store, query, options);

        nb::list matches;
        for (size_t i = 0U; i < result.matches.size(); ++i) {
            const MetadataFuzzySearchMatch& match = result.matches[i];
            nb::dict item;
            item["entry_id"]        = nb::int_(match.entry_id);
            item["key_kind"]        = match.key_kind;
            item["match_kind"]      = match.match_kind;
            item["match_kind_name"] = nb::str(
                metadata_fuzzy_search_match_kind_name(match.match_kind));
            item["score"]           = nb::int_(match.score);
            item["group_truncated"] = nb::bool_(match.group_truncated);
            item["name_truncated"]  = nb::bool_(match.name_truncated);
            item["group"]           = sv_to_py(match.group);
            item["name"]            = sv_to_py(match.name);
            matches.append(std::move(item));
        }

        nb::dict out;
        out["status"]      = result.status;
        out["status_name"] = nb::str(
            metadata_fuzzy_search_status_name(result.status));
        out["examined_entry_count"]  = nb::int_(result.examined_entry_count);
        out["qualified_match_count"] = nb::int_(result.qualified_match_count);
        out["truncated"]             = nb::bool_(result.truncated);
        out["matches"]               = std::move(matches);
        return out;
    }

    static nb::dict metadata_interpretation_record_to_python(
        const MetadataInterpretationRecord& record)
    {
        nb::dict out;
        out["query_kind"]      = record.query_kind;
        out["query_kind_name"] = nb::str(
            metadata_query_kind_name(record.query_kind));
        out["semantic"]      = record.semantic;
        out["semantic_name"] = nb::str(
            metadata_query_semantic_kind_name(record.semantic));
        out["shape"]      = record.shape;
        out["shape_name"] = nb::str(
            metadata_query_value_shape_name(record.shape));
        out["confidence"]     = nb::int_(record.confidence);
        out["source_entries"] = metadata_query_entry_ids_to_python(
            record.source_entries);
        out["has_origin"] = nb::bool_(record.has_origin);
        out["origin"] = metadata_query_optional_pair_to_python(record.has_origin,
                                                               record.origin);
        out["has_size"] = nb::bool_(record.has_size);
        out["size"] = metadata_query_optional_pair_to_python(record.has_size,
                                                             record.size);
        out["has_rect"] = nb::bool_(record.has_rect);
        out["rect"] = metadata_query_optional_rect_to_python(record.has_rect,
                                                             record.rect);
        out["has_margins"] = nb::bool_(record.has_margins);
        out["margins"]
            = metadata_query_optional_margins_to_python(record.has_margins,
                                                        record.margins);
        out["has_values"] = nb::bool_(record.has_values);
        if (record.has_values) {
            out["values"] = metadata_query_values_to_python(record.values);
        } else {
            out["values"] = nb::none();
        }
        return out;
    }

    static nb::dict metadata_interpretation_result_to_python(
        const MetadataInterpretationResult& result)
    {
        nb::list records;
        for (size_t i = 0U; i < result.records.size(); ++i) {
            records.append(
                metadata_interpretation_record_to_python(result.records[i]));
        }
        nb::dict out;
        out["records"] = std::move(records);
        return out;
    }

    static nb::dict metadata_interpretation_to_python(const MetaStore& store)
    {
        const MetadataInterpretationResult result = interpret_metadata(store);
        return metadata_interpretation_result_to_python(result);
    }

    static nb::dict
    metadata_interpretation_query_to_python(const MetaStore& store,
                                            MetadataQueryKind kind)
    {
        const MetadataInterpretationResult result
            = interpret_metadata_query(store, kind);
        return metadata_interpretation_result_to_python(result);
    }

    static nb::list metadata_concept_numeric_to_python(
        const MetadataConceptCandidate& candidate)
    {
        nb::list out;
        for (uint8_t i = 0U; i < candidate.numeric_count; ++i) {
            out.append(nb::float_(candidate.numeric[i]));
        }
        return out;
    }

    static nb::object metadata_concept_datetime_to_python(
        const MetadataConceptCandidate& candidate)
    {
        if (!candidate.has_date_time) {
            return nb::none();
        }
        nb::dict out;
        out["year"]           = nb::int_(candidate.date_time_year);
        out["month"]          = nb::int_(candidate.date_time_month);
        out["day"]            = nb::int_(candidate.date_time_day);
        out["has_time"]       = nb::bool_(candidate.date_time_has_time);
        out["precision"]      = candidate.date_time_precision;
        out["precision_name"] = nb::str(
            metadata_concept_datetime_precision_name(
                candidate.date_time_precision));
        out["timezone"]      = candidate.date_time_zone;
        out["timezone_name"] = nb::str(
            metadata_concept_timezone_kind_name(candidate.date_time_zone));
        if (candidate.date_time_has_time) {
            out["hour"]   = nb::int_(candidate.date_time_hour);
            out["minute"] = nb::int_(candidate.date_time_minute);
            out["second"] = nb::int_(candidate.date_time_second);
        } else {
            out["hour"]   = nb::none();
            out["minute"] = nb::none();
            out["second"] = nb::none();
        }
        out["has_subsecond"] = nb::bool_(candidate.date_time_has_subsecond);
        if (candidate.date_time_has_subsecond) {
            out["subsecond"] = sv_to_py(candidate.date_time_subsecond);
        } else {
            out["subsecond"] = nb::none();
        }
        out["has_utc_offset"] = nb::bool_(candidate.date_time_has_utc_offset);
        if (candidate.date_time_has_utc_offset) {
            out["utc_offset_minutes"] = nb::int_(
                candidate.date_time_utc_offset_min);
        } else {
            out["utc_offset_minutes"] = nb::none();
        }
        return out;
    }

    static nb::dict metadata_concept_candidate_to_python(
        const MetadataConceptCandidate& candidate)
    {
        nb::dict out;
        out["kind"]      = candidate.kind;
        out["kind_name"] = nb::str(metadata_concept_kind_name(candidate.kind));
        out["role"]      = candidate.role;
        out["role_name"] = nb::str(metadata_concept_role_name(candidate.role));
        out["record_kind"]      = candidate.record_kind;
        out["record_kind_name"] = nb::str(
            metadata_concept_record_kind_name(candidate.record_kind));
        out["image_region_shape"]      = candidate.image_region_shape;
        out["image_region_shape_name"] = nb::str(
            metadata_image_region_shape_name(candidate.image_region_shape));
        out["image_region_coordinate_unit"]
            = candidate.image_region_coordinate_unit;
        out["image_region_coordinate_unit_name"] = nb::str(
            metadata_image_region_coordinate_unit_name(
                candidate.image_region_coordinate_unit));
        out["sensitivity"]      = candidate.sensitivity;
        out["sensitivity_name"] = nb::str(
            metadata_concept_sensitivity_name(candidate.sensitivity));
        out["family"]      = candidate.family;
        out["family_name"] = nb::str(
            metadata_concept_source_family_name(candidate.family));
        out["semantic"]      = candidate.semantic;
        out["semantic_name"] = nb::str(
            metadata_query_semantic_kind_name(candidate.semantic));
        out["shape"]      = candidate.shape;
        out["shape_name"] = nb::str(
            metadata_query_value_shape_name(candidate.shape));
        out["entry_id"]       = nb::int_(candidate.entry_id);
        out["source_entries"] = metadata_query_entry_ids_to_python(
            candidate.source_entries);
        if (candidate.location_scope.empty()) {
            out["location_scope"] = nb::none();
        } else {
            out["location_scope"] = nb::str(candidate.location_scope.c_str(),
                                            candidate.location_scope.size());
        }
        if (candidate.record_scope.empty()) {
            out["record_scope"] = nb::none();
        } else {
            out["record_scope"] = nb::str(candidate.record_scope.c_str(),
                                          candidate.record_scope.size());
        }
        if (candidate.language.empty()) {
            out["language"] = nb::none();
        } else {
            out["language"] = nb::str(candidate.language.c_str(),
                                      candidate.language.size());
        }
        out["priority"]           = nb::int_(candidate.priority);
        out["preferred"]          = nb::bool_(candidate.preferred);
        out["conflict"]           = nb::bool_(candidate.conflict);
        out["transfer_hint"]      = candidate.transfer_hint;
        out["transfer_hint_name"] = nb::str(
            metadata_concept_transfer_hint_name(candidate.transfer_hint));
        out["compatible_file_safe"] = nb::bool_(candidate.compatible_file_safe);
        out["rendered_image_safe"]  = nb::bool_(candidate.rendered_image_safe);
        out["requires_target_image_spec"] = nb::bool_(
            candidate.requires_target_image_spec);
        out["source_bound"]           = nb::bool_(candidate.source_bound);
        out["raw_applicability"]      = candidate.raw_applicability;
        out["raw_applicability_name"] = nb::str(
            metadata_raw_applicability_state_name(candidate.raw_applicability));
        out["raw_applicability_requires_storage_context"] = nb::bool_(
            candidate.raw_applicability_requires_storage_context);
        out["raw_applicability_can_affect_decode"] = nb::bool_(
            candidate.raw_applicability_can_affect_decode);
        out["has_numeric"]   = nb::bool_(candidate.has_numeric);
        out["numeric_count"] = nb::int_(candidate.numeric_count);
        if (candidate.has_numeric) {
            out["numeric"] = metadata_concept_numeric_to_python(candidate);
        } else {
            out["numeric"] = nb::none();
        }
        out["has_values"] = nb::bool_(candidate.has_values);
        if (candidate.has_values) {
            out["values"] = metadata_query_values_to_python(candidate.values);
        } else {
            out["values"] = nb::none();
        }
        out["has_origin"] = nb::bool_(candidate.has_origin);
        out["origin"]
            = metadata_query_optional_pair_to_python(candidate.has_origin,
                                                     candidate.origin);
        out["has_size"] = nb::bool_(candidate.has_size);
        out["size"] = metadata_query_optional_pair_to_python(candidate.has_size,
                                                             candidate.size);
        out["has_rect"] = nb::bool_(candidate.has_rect);
        out["rect"] = metadata_query_optional_rect_to_python(candidate.has_rect,
                                                             candidate.rect);
        out["has_margins"] = nb::bool_(candidate.has_margins);
        out["margins"]
            = metadata_query_optional_margins_to_python(candidate.has_margins,
                                                        candidate.margins);
        out["text"]          = sv_to_py(candidate.text);
        out["value_key"]     = sv_to_py(candidate.value_key);
        out["has_date_time"] = nb::bool_(candidate.has_date_time);
        out["date_time"]     = metadata_concept_datetime_to_python(candidate);
        out["has_gps_altitude_reference"] = nb::bool_(
            candidate.has_gps_altitude_reference);
        out["gps_altitude_below_sea_level"] = nb::bool_(
            candidate.gps_altitude_below_sea_level);
        out["gps_altitude_reference_code"] = nb::int_(
            candidate.gps_altitude_reference_code);
        out["gps_altitude_reference_name"] = nb::str(
            metadata_concept_gps_altitude_reference_name(
                candidate.gps_altitude_reference_code));
        return out;
    }

    static nb::dict metadata_concept_resolution_to_python(
        const MetadataConceptResolution& resolution)
    {
        nb::list candidates;
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            candidates.append(
                metadata_concept_candidate_to_python(resolution.candidates[i]));
        }
        nb::dict out;
        out["kind"]      = resolution.kind;
        out["kind_name"] = nb::str(metadata_concept_kind_name(resolution.kind));
        out["found"]     = nb::bool_(resolution.found);
        out["conflict"]  = nb::bool_(resolution.conflict);
        out["preferred_entry"] = nb::int_(resolution.preferred_entry);
        out["source_entries"]  = metadata_query_entry_ids_to_python(
            resolution.source_entries);
        out["candidates"] = std::move(candidates);
        return out;
    }

    static nb::dict
    metadata_concept_result_to_python(const MetadataConceptResult& result)
    {
        nb::list concepts;
        for (size_t i = 0U; i < result.concepts.size(); ++i) {
            concepts.append(
                metadata_concept_resolution_to_python(result.concepts[i]));
        }
        nb::dict out;
        out["concepts"] = std::move(concepts);
        return out;
    }

    static nb::dict metadata_concepts_to_python(const MetaStore& store)
    {
        const MetadataConceptResult result = resolve_metadata_concepts(store);
        return metadata_concept_result_to_python(result);
    }

    static nb::dict
    metadata_concepts_to_python(const MetaStore& store,
                                const MetadataRawDataDescriptor& raw_descriptor)
    {
        const MetadataConceptResult result
            = resolve_metadata_concepts(store, raw_descriptor);
        return metadata_concept_result_to_python(result);
    }

    static nb::dict metadata_concept_to_python(const MetaStore& store,
                                               MetadataConceptKind kind)
    {
        const MetadataConceptResolution result = resolve_metadata_concept(store,
                                                                          kind);
        return metadata_concept_resolution_to_python(result);
    }

    static nb::dict
    metadata_concept_to_python(const MetaStore& store, MetadataConceptKind kind,
                               const MetadataRawDataDescriptor& raw_descriptor)
    {
        const MetadataConceptResolution result
            = resolve_metadata_concept(store, kind, raw_descriptor);
        return metadata_concept_resolution_to_python(result);
    }

    static const char* transfer_raw_carrier_passthrough_reason_name(
        TransferRawCarrierPassthroughReason reason) noexcept;

    static const char*
    transfer_makernote_trust_name(TransferMakerNoteTrust trust) noexcept
    {
        switch (trust) {
        case TransferMakerNoteTrust::NotPresent: return "not_present";
        case TransferMakerNoteTrust::DecodedOnlyNotSerializable:
            return "decoded_only_not_serializable";
        case TransferMakerNoteTrust::OpaquePreservationUnverified:
            return "opaque_preservation_unverified";
        }
        return "unknown";
    }

    static nb::dict
    makernote_transfer_audit_to_python(const TransferMakerNoteAudit& audit)
    {
        nb::dict out;
        out["trust"]      = audit.trust;
        out["trust_name"] = nb::str(transfer_makernote_trust_name(audit.trust));
        out["raw_payload_count"]        = nb::int_(audit.raw_payload_count);
        out["decoded_only_entry_count"] = nb::int_(
            audit.decoded_only_entry_count);
        out["opaque_payload_available"] = nb::bool_(
            audit.opaque_payload_available);
        out["decoded_rewrite_available"] = nb::bool_(
            audit.decoded_rewrite_available);
        out["internal_offset_relocation_available"] = nb::bool_(
            audit.internal_offset_relocation_available);
        out["vendor_checksum_recalculation_available"] = nb::bool_(
            audit.vendor_checksum_recalculation_available);
        out["semantic_validation_available"] = nb::bool_(
            audit.semantic_validation_available);
        out["raw_carrier_passthrough_available"] = nb::bool_(
            audit.raw_carrier_passthrough_available);
        return out;
    }

    static const char*
    transfer_makernote_vendor_name(TransferMakerNoteVendor vendor) noexcept
    {
        switch (vendor) {
        case TransferMakerNoteVendor::Unknown: return "unknown";
        case TransferMakerNoteVendor::Nikon: return "nikon";
        case TransferMakerNoteVendor::Canon: return "canon";
        }
        return "unknown";
    }

    static const char*
    transfer_makernote_layout_name(TransferMakerNoteLayout layout) noexcept
    {
        switch (layout) {
        case TransferMakerNoteLayout::UnknownOrMixed: return "unknown_or_mixed";
        case TransferMakerNoteLayout::NikonType1OuterTiff:
            return "nikon_type1_outer_tiff";
        case TransferMakerNoteLayout::NikonType3EmbeddedTiff:
            return "nikon_type3_embedded_tiff";
        case TransferMakerNoteLayout::CanonSourceDependentIfd:
            return "canon_source_dependent_ifd";
        }
        return "unknown";
    }

    static const char* transfer_makernote_layout_trust_name(
        TransferMakerNoteLayoutTrust trust) noexcept
    {
        switch (trust) {
        case TransferMakerNoteLayoutTrust::NotPresent: return "not_present";
        case TransferMakerNoteLayoutTrust::UnrecognizedOrMixed:
            return "unrecognized_or_mixed";
        case TransferMakerNoteLayoutTrust::OuterTiffOffsetsUnsafe:
            return "outer_tiff_offsets_unsafe";
        case TransferMakerNoteLayoutTrust::SourceOffsetBasisAmbiguous:
            return "source_offset_basis_ambiguous";
        case TransferMakerNoteLayoutTrust::EmbeddedTiffStructureUnverified:
            return "embedded_tiff_structure_unverified";
        case TransferMakerNoteLayoutTrust::EmbeddedTiffStructureVerified:
            return "embedded_tiff_structure_verified";
        }
        return "unknown";
    }

    static nb::dict makernote_layout_transfer_audit_to_python(
        const TransferMakerNoteLayoutAudit& audit)
    {
        nb::dict out;
        out["trust"]      = audit.trust;
        out["trust_name"] = nb::str(
            transfer_makernote_layout_trust_name(audit.trust));
        out["vendor"]      = audit.vendor;
        out["vendor_name"] = nb::str(
            transfer_makernote_vendor_name(audit.vendor));
        out["layout"]      = audit.layout;
        out["layout_name"] = nb::str(
            transfer_makernote_layout_name(audit.layout));
        out["raw_payload_count"]        = nb::int_(audit.raw_payload_count);
        out["recognized_payload_count"] = nb::int_(
            audit.recognized_payload_count);
        out["structurally_valid_payload_count"] = nb::int_(
            audit.structurally_valid_payload_count);
        out["offset_basis_known"] = nb::bool_(audit.offset_basis_known);
        out["embedded_tiff_validation_available"] = nb::bool_(
            audit.embedded_tiff_validation_available);
        out["embedded_tiff_validation_passed"] = nb::bool_(
            audit.embedded_tiff_validation_passed);
        out["embedded_tiff_offsets_self_contained"] = nb::bool_(
            audit.embedded_tiff_offsets_self_contained);
        out["outer_tiff_offset_relocation_required"] = nb::bool_(
            audit.outer_tiff_offset_relocation_required);
        out["source_offset_context_required"] = nb::bool_(
            audit.source_offset_context_required);
        out["vendor_private_offsets_verified"] = nb::bool_(
            audit.vendor_private_offsets_verified);
        out["vendor_checksum_validation_available"] = nb::bool_(
            audit.vendor_checksum_validation_available);
        out["semantic_roundtrip_validation_available"] = nb::bool_(
            audit.semantic_roundtrip_validation_available);
        return out;
    }

    static nb::dict
    transfer_safety_audit_to_python(const TransferSafetyAudit& audit)
    {
        nb::dict out;
        out["safety"]                  = audit.safety;
        out["source_image_properties"] = nb::int_(
            audit.source_image_properties);
        out["source_raw_color_calibration"] = nb::int_(
            audit.source_raw_color_calibration);
        out["source_camera_raw_settings"] = nb::int_(
            audit.source_camera_raw_settings);
        out["source_icc_profiles"]   = nb::int_(audit.source_icc_profiles);
        out["source_makernotes"]     = nb::int_(audit.source_makernotes);
        out["source_non_c2pa_jumbf"] = nb::int_(audit.source_non_c2pa_jumbf);
        out["source_c2pa"]           = nb::int_(audit.source_c2pa);
        out["filtered_image_properties"] = nb::int_(
            audit.filtered_image_properties);
        out["filtered_raw_color_calibration"] = nb::int_(
            audit.filtered_raw_color_calibration);
        out["filtered_camera_raw_settings"] = nb::int_(
            audit.filtered_camera_raw_settings);
        out["filtered_icc_profiles"]   = nb::int_(audit.filtered_icc_profiles);
        out["filtered_makernotes"]     = nb::int_(audit.filtered_makernotes);
        out["filtered_non_c2pa_jumbf"] = nb::int_(
            audit.filtered_non_c2pa_jumbf);
        out["invalidated_c2pa"]    = nb::int_(audit.invalidated_c2pa);
        out["sony_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Sony, audit.sony_raw_processing);
        out["canon_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Canon, audit.canon_raw_processing);
        out["nikon_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Nikon, audit.nikon_raw_processing);
        out["fujifilm_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Fujifilm, audit.fujifilm_raw_processing);
        out["pentax_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Pentax, audit.pentax_raw_processing);
        out["panasonic_raw_processing"]
            = vendor_raw_processing_summary_to_python(
                VendorRawProcessingFamily::Panasonic,
                audit.panasonic_raw_processing);
        out["olympus_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Olympus, audit.olympus_raw_processing);
        out["kodak_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Kodak, audit.kodak_raw_processing);
        out["minolta_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Minolta, audit.minolta_raw_processing);
        out["sigma_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Sigma, audit.sigma_raw_processing);
        out["samsung_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Samsung, audit.samsung_raw_processing);
        out["ricoh_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Ricoh, audit.ricoh_raw_processing);
        out["apple_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Apple, audit.apple_raw_processing);
        out["dji_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Dji, audit.dji_raw_processing);
        out["google_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Google, audit.google_raw_processing);
        out["flir_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Flir, audit.flir_raw_processing);
        out["casio_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Casio, audit.casio_raw_processing);
        out["sanyo_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Sanyo, audit.sanyo_raw_processing);
        out["kyocera_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::KyoceraRaw,
            audit.kyocera_raw_processing);
        out["reconyx_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Reconyx, audit.reconyx_raw_processing);
        out["hp_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Hp, audit.hp_raw_processing);
        out["jvc_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Jvc, audit.jvc_raw_processing);
        out["ge_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Ge, audit.ge_raw_processing);
        out["motorola_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Motorola, audit.motorola_raw_processing);
        out["nintendo_raw_processing"] = vendor_raw_processing_summary_to_python(
            VendorRawProcessingFamily::Nintendo, audit.nintendo_raw_processing);
        out["microsoft_raw_processing"]
            = vendor_raw_processing_summary_to_python(
                VendorRawProcessingFamily::Microsoft,
                audit.microsoft_raw_processing);
        return out;
    }

    static nb::dict transfer_concept_diagnostic_to_python(
        const TransferConceptDiagnostic& diagnostic)
    {
        nb::dict out;
        out["kind"]      = diagnostic.kind;
        out["kind_name"] = nb::str(metadata_concept_kind_name(diagnostic.kind));
        out["role"]      = diagnostic.role;
        out["role_name"] = nb::str(metadata_concept_role_name(diagnostic.role));
        out["record_kind"]      = diagnostic.record_kind;
        out["record_kind_name"] = nb::str(
            metadata_concept_record_kind_name(diagnostic.record_kind));
        out["sensitivity"]      = diagnostic.sensitivity;
        out["sensitivity_name"] = nb::str(
            metadata_concept_sensitivity_name(diagnostic.sensitivity));
        out["hint"]      = diagnostic.hint;
        out["hint_name"] = nb::str(
            metadata_concept_transfer_hint_name(diagnostic.hint));
        out["action"]      = diagnostic.action;
        out["action_name"] = nb::str(
            transfer_concept_diagnostic_action_name(diagnostic.action));
        out["reason"]      = diagnostic.reason;
        out["reason_name"] = nb::str(
            transfer_concept_diagnostic_reason_name(diagnostic.reason));
        const TransferConceptDiagnosticSeverity severity
            = transfer_concept_diagnostic_severity(diagnostic);
        out["severity"]      = severity;
        out["severity_name"] = nb::str(
            transfer_concept_diagnostic_severity_name(severity));
        out["message"] = nb::str(
            transfer_concept_diagnostic_message(diagnostic));
        const std::string message_token
            = transfer_concept_diagnostic_message_token(diagnostic);
        out["message_token"] = nb::str(message_token.c_str(),
                                       message_token.size());
        const std::vector<std::string> message_arguments
            = transfer_concept_diagnostic_message_arguments(diagnostic);
        nb::list args;
        for (size_t i = 0U; i < message_arguments.size(); ++i) {
            args.append(nb::str(message_arguments[i].c_str(),
                                message_arguments[i].size()));
        }
        out["message_arguments"] = std::move(args);
        const std::string token = transfer_concept_diagnostic_token(diagnostic);
        out["token"]            = nb::str(token.c_str(), token.size());
        out["entry_id"]         = nb::int_(diagnostic.entry_id);
        out["source_entries"]   = metadata_query_entry_ids_to_python(
            diagnostic.source_entries);
        if (diagnostic.location_scope.empty()) {
            out["location_scope"] = nb::none();
        } else {
            out["location_scope"] = nb::str(diagnostic.location_scope.c_str(),
                                            diagnostic.location_scope.size());
        }
        if (diagnostic.record_scope.empty()) {
            out["record_scope"] = nb::none();
        } else {
            out["record_scope"] = nb::str(diagnostic.record_scope.c_str(),
                                          diagnostic.record_scope.size());
        }
        if (diagnostic.language.empty()) {
            out["language"] = nb::none();
        } else {
            out["language"] = nb::str(diagnostic.language.c_str(),
                                      diagnostic.language.size());
        }
        out["preferred"]            = nb::bool_(diagnostic.preferred);
        out["conflict"]             = nb::bool_(diagnostic.conflict);
        out["compatible_file_safe"] = nb::bool_(
            diagnostic.compatible_file_safe);
        out["rendered_image_safe"] = nb::bool_(diagnostic.rendered_image_safe);
        out["requires_target_image_spec"] = nb::bool_(
            diagnostic.requires_target_image_spec);
        out["source_bound"]           = nb::bool_(diagnostic.source_bound);
        out["raw_applicability"]      = diagnostic.raw_applicability;
        out["raw_applicability_name"] = nb::str(
            metadata_raw_applicability_state_name(diagnostic.raw_applicability));
        out["raw_applicability_requires_storage_context"] = nb::bool_(
            diagnostic.raw_applicability_requires_storage_context);
        out["raw_applicability_can_affect_decode"] = nb::bool_(
            diagnostic.raw_applicability_can_affect_decode);
        out["has_gps_altitude_reference"] = nb::bool_(
            diagnostic.has_gps_altitude_reference);
        out["gps_altitude_below_sea_level"] = nb::bool_(
            diagnostic.gps_altitude_below_sea_level);
        out["gps_altitude_reference_code"] = nb::int_(
            diagnostic.gps_altitude_reference_code);
        out["gps_altitude_reference_name"] = nb::str(
            metadata_concept_gps_altitude_reference_name(
                diagnostic.gps_altitude_reference_code));
        return out;
    }

    static nb::dict transfer_concept_diagnostics_to_python(
        const TransferConceptDiagnostics& diagnostics)
    {
        nb::list entries;
        for (size_t i = 0U; i < diagnostics.diagnostics.size(); ++i) {
            entries.append(transfer_concept_diagnostic_to_python(
                diagnostics.diagnostics[i]));
        }
        nb::dict out;
        out["safety"]          = diagnostics.safety;
        out["candidate_count"] = nb::int_(diagnostics.candidate_count);
        out["kept_count"]      = nb::int_(diagnostics.kept_count);
        out["dropped_count"]   = nb::int_(diagnostics.dropped_count);
        out["requires_target_image_spec_count"] = nb::int_(
            diagnostics.requires_target_image_spec_count);
        out["rendered_unsafe_count"] = nb::int_(
            diagnostics.rendered_unsafe_count);
        out["source_bound_count"] = nb::int_(diagnostics.source_bound_count);
        out["conflict_count"]     = nb::int_(diagnostics.conflict_count);
        out["diagnostics"]        = std::move(entries);
        return out;
    }

    static nb::dict raw_carrier_passthrough_decision_to_python(
        const TransferRawCarrierPassthroughDecision& decision)
    {
        nb::dict out;
        out["carrier_index"]       = nb::int_(decision.carrier_index);
        out["decoded_entry_count"] = nb::int_(decision.decoded_entry_count);
        out["semantic_kind"]       = decision.semantic_kind;
        out["semantic_kind_name"]  = nb::str(
            transfer_block_kind_name(decision.semantic_kind));
        out["eligible"]    = nb::bool_(decision.eligible);
        out["reason"]      = decision.reason;
        out["reason_name"] = nb::str(
            transfer_raw_carrier_passthrough_reason_name(decision.reason));
        out["source_route"] = nb::str(decision.source_route.c_str(),
                                      decision.source_route.size());
        out["target_route"] = nb::str(decision.target_route.c_str(),
                                      decision.target_route.size());
        return out;
    }

    static nb::dict raw_carrier_passthrough_audit_to_python(
        const TransferRawCarrierPassthroughAudit& audit)
    {
        nb::list decisions;
        for (size_t i = 0U; i < audit.decisions.size(); ++i) {
            decisions.append(
                raw_carrier_passthrough_decision_to_python(audit.decisions[i]));
        }

        nb::dict out;
        out["target_format"]           = audit.target_format;
        out["safety"]                  = audit.safety;
        out["carrier_count"]           = nb::int_(audit.carrier_count);
        out["eligible_count"]          = nb::int_(audit.eligible_count);
        out["blocked_missing_payload"] = nb::int_(
            audit.blocked_missing_payload);
        out["blocked_target_incompatible"] = nb::int_(
            audit.blocked_target_incompatible);
        out["blocked_safety_filtered"] = nb::int_(
            audit.blocked_safety_filtered);
        out["blocked_content_bound_metadata"] = nb::int_(
            audit.blocked_content_bound_metadata);
        out["blocked_policy"]                  = nb::int_(audit.blocked_policy);
        out["blocked_decode_link_unavailable"] = nb::int_(
            audit.blocked_decode_link_unavailable);
        out["blocked_unsupported_kind"] = nb::int_(
            audit.blocked_unsupported_kind);
        out["decisions"] = std::move(decisions);
        return out;
    }

    static nb::dict validate_issue_to_python(const ValidateIssue& issue)
    {
        nb::dict d;
        d["severity"] = issue.severity;
        d["category"] = nb::str(issue.category.c_str(), issue.category.size());
        d["code"]     = nb::str(issue.code.c_str(), issue.code.size());
        d["ifd"]      = nb::str(issue.ifd.c_str(), issue.ifd.size());
        d["name"]     = nb::str(issue.name.c_str(), issue.name.size());
        d["tag"]      = nb::int_(issue.tag);
        d["message"]  = nb::str(issue.message.c_str(), issue.message.size());
        return d;
    }

    static nb::dict validate_file_to_python(
        const std::string& path, bool include_pointer_tags,
        bool decode_makernote, bool decode_printim, bool decompress,
        bool include_xmp_sidecar, bool verify_c2pa,
        C2paVerifyBackend verify_backend, bool verify_require_trusted_chain,
        bool verify_require_resolved_references, bool warnings_as_errors,
        bool ccm_require_dng_context, bool ccm_include_reduction_matrices,
        uint32_t ccm_max_fields, uint32_t ccm_max_values_per_field,
        CcmValidationMode ccm_validation_mode, uint64_t max_file_bytes,
        nb::object policy_obj)
    {
        ValidateOptions options;
        options.include_pointer_tags         = include_pointer_tags;
        options.decode_makernote             = decode_makernote;
        options.decode_printim               = decode_printim;
        options.decompress                   = decompress;
        options.include_xmp_sidecar          = include_xmp_sidecar;
        options.verify_c2pa                  = verify_c2pa;
        options.verify_backend               = verify_backend;
        options.verify_require_trusted_chain = verify_require_trusted_chain;
        options.verify_require_resolved_references
            = verify_require_resolved_references;
        options.warnings_as_errors             = warnings_as_errors;
        options.ccm.require_dng_context        = ccm_require_dng_context;
        options.ccm.include_reduction_matrices = ccm_include_reduction_matrices;
        options.ccm.limits.max_fields          = ccm_max_fields;
        options.ccm.limits.max_values_per_field = ccm_max_values_per_field;
        options.ccm.validation_mode             = ccm_validation_mode;
        options.policy.max_file_bytes           = max_file_bytes;
        if (!policy_obj.is_none()) {
            options.policy = nb::cast<OpenMetaResourcePolicy>(policy_obj);
            if (max_file_bytes != 0U) {
                options.policy.max_file_bytes = max_file_bytes;
            }
        }

        ValidateResult result;
        {
            nb::gil_scoped_release gil_release;
            result = validate_file(path.c_str(), options);
        }

        nb::list issues;
        for (size_t i = 0; i < result.issues.size(); ++i) {
            issues.append(validate_issue_to_python(result.issues[i]));
        }

        nb::dict out;
        out["status"]               = result.status;
        out["file_size"]            = nb::int_(result.file_size);
        out["scan_status"]          = result.read.scan.status;
        out["payload_status"]       = result.read.payload.status;
        out["exif_status"]          = result.read.exif.status;
        out["xmp_status"]           = result.read.xmp.status;
        out["exr_status"]           = result.read.exr.status;
        out["jumbf_status"]         = result.read.jumbf.status;
        out["jumbf_verify_status"]  = result.read.jumbf.verify_status;
        out["jumbf_verify_backend"] = result.read.jumbf.verify_backend_selected;
        out["jumbf_verify_require_trusted_chain"] = nb::bool_(
            verify_require_trusted_chain);
        out["jumbf_verify_require_resolved_references"] = nb::bool_(
            verify_require_resolved_references);
        out["entries"]             = nb::int_(result.entries);
        out["ccm_status"]          = result.ccm.status;
        out["ccm_fields"]          = nb::int_(result.ccm_fields);
        out["ccm_fields_found"]    = nb::int_(result.ccm.fields_found);
        out["ccm_fields_dropped"]  = nb::int_(result.ccm.fields_dropped);
        out["ccm_issues_reported"] = nb::int_(result.ccm.issues_reported);
        out["warning_count"]       = nb::int_(result.warning_count);
        out["error_count"]         = nb::int_(result.error_count);
        out["failed"]              = nb::bool_(result.failed);
        out["issues"]              = std::move(issues);
        return out;
    }

    static const char* transfer_status_name(TransferStatus status) noexcept
    {
        switch (status) {
        case TransferStatus::Ok: return "ok";
        case TransferStatus::InvalidArgument: return "invalid_argument";
        case TransferStatus::Unsupported: return "unsupported";
        case TransferStatus::LimitExceeded: return "limit_exceeded";
        case TransferStatus::Malformed: return "malformed";
        case TransferStatus::UnsafeData: return "unsafe_data";
        case TransferStatus::InternalError: return "internal_error";
        }
        return "unknown";
    }

    static const char*
    transfer_file_status_name(TransferFileStatus status) noexcept
    {
        switch (status) {
        case TransferFileStatus::Ok: return "ok";
        case TransferFileStatus::InvalidArgument: return "invalid_argument";
        case TransferFileStatus::OpenFailed: return "open_failed";
        case TransferFileStatus::StatFailed: return "stat_failed";
        case TransferFileStatus::TooLarge: return "too_large";
        case TransferFileStatus::MapFailed: return "map_failed";
        case TransferFileStatus::ReadFailed: return "read_failed";
        }
        return "unknown";
    }

    static const char*
    prepare_transfer_code_name(PrepareTransferCode code) noexcept
    {
        switch (code) {
        case PrepareTransferCode::None: return "none";
        case PrepareTransferCode::NullOutBundle: return "null_out_bundle";
        case PrepareTransferCode::UnsupportedTargetFormat:
            return "unsupported_target_format";
        case PrepareTransferCode::ExifPackFailed: return "exif_pack_failed";
        case PrepareTransferCode::XmpPackFailed: return "xmp_pack_failed";
        case PrepareTransferCode::IccPackFailed: return "icc_pack_failed";
        case PrepareTransferCode::IptcPackFailed: return "iptc_pack_failed";
        case PrepareTransferCode::RequestedMetadataNotSerializable:
            return "requested_metadata_not_serializable";
        }
        return "unknown";
    }

    static const char* emit_transfer_code_name(EmitTransferCode code) noexcept
    {
        switch (code) {
        case EmitTransferCode::None: return "none";
        case EmitTransferCode::InvalidArgument: return "invalid_argument";
        case EmitTransferCode::BundleTargetNotJpeg:
            return "bundle_target_not_jpeg";
        case EmitTransferCode::UnsupportedRoute: return "unsupported_route";
        case EmitTransferCode::InvalidPayload: return "invalid_payload";
        case EmitTransferCode::ContentBoundPayloadUnsupported:
            return "content_bound_payload_unsupported";
        case EmitTransferCode::BackendWriteFailed:
            return "backend_write_failed";
        case EmitTransferCode::PlanMismatch: return "plan_mismatch";
        }
        return "unknown";
    }

    static const char*
    prepare_transfer_file_code_name(PrepareTransferFileCode code) noexcept
    {
        switch (code) {
        case PrepareTransferFileCode::None: return "none";
        case PrepareTransferFileCode::EmptyPath: return "empty_path";
        case PrepareTransferFileCode::MapFailed: return "map_failed";
        case PrepareTransferFileCode::PayloadBufferPlatformLimit:
            return "payload_buffer_platform_limit";
        case PrepareTransferFileCode::DecodeFailed: return "decode_failed";
        }
        return "unknown";
    }

    static const char*
    transfer_file_status_message(TransferFileStatus status) noexcept
    {
        switch (status) {
        case TransferFileStatus::Ok: return "";
        case TransferFileStatus::InvalidArgument:
            return "invalid transfer file input";
        case TransferFileStatus::OpenFailed:
            return "failed to open transfer file input";
        case TransferFileStatus::StatFailed:
            return "failed to stat transfer file input";
        case TransferFileStatus::TooLarge:
            return "transfer file input exceeded configured size limit";
        case TransferFileStatus::MapFailed:
            return "failed to map transfer file input";
        case TransferFileStatus::ReadFailed:
            return "failed to decode transfer file input";
        }
        return "unknown transfer file error";
    }

    static const char* read_transfer_source_snapshot_file_code_name(
        ReadTransferSourceSnapshotFileCode code) noexcept
    {
        switch (code) {
        case ReadTransferSourceSnapshotFileCode::None: return "none";
        case ReadTransferSourceSnapshotFileCode::EmptyPath: return "empty_path";
        case ReadTransferSourceSnapshotFileCode::MapFailed: return "map_failed";
        case ReadTransferSourceSnapshotFileCode::PayloadBufferPlatformLimit:
            return "payload_buffer_platform_limit";
        case ReadTransferSourceSnapshotFileCode::DecodeFailed:
            return "decode_failed";
        }
        return "unknown";
    }

    static const char* read_transfer_source_snapshot_bytes_code_name(
        ReadTransferSourceSnapshotBytesCode code) noexcept
    {
        switch (code) {
        case ReadTransferSourceSnapshotBytesCode::None: return "none";
        case ReadTransferSourceSnapshotBytesCode::PayloadBufferPlatformLimit:
            return "payload_buffer_platform_limit";
        case ReadTransferSourceSnapshotBytesCode::DecodeFailed:
            return "decode_failed";
        }
        return "unknown";
    }

    static const char*
    transfer_policy_subject_name(TransferPolicySubject subject) noexcept
    {
        switch (subject) {
        case TransferPolicySubject::MakerNote: return "makernote";
        case TransferPolicySubject::Jumbf: return "jumbf";
        case TransferPolicySubject::C2pa: return "c2pa";
        case TransferPolicySubject::XmpExifProjection:
            return "xmp_exif_projection";
        case TransferPolicySubject::XmpIptcProjection:
            return "xmp_iptc_projection";
        case TransferPolicySubject::ImageProperties: return "image_properties";
        case TransferPolicySubject::IccProfile: return "icc_profile";
        case TransferPolicySubject::RawColorCalibration:
            return "raw_color_calibration";
        case TransferPolicySubject::CameraRawSettings:
            return "camera_raw_settings";
        }
        return "unknown";
    }

    static const char*
    transfer_policy_action_name(TransferPolicyAction action) noexcept
    {
        switch (action) {
        case TransferPolicyAction::Keep: return "keep";
        case TransferPolicyAction::Drop: return "drop";
        case TransferPolicyAction::Invalidate: return "invalidate";
        case TransferPolicyAction::Rewrite: return "rewrite";
        }
        return "unknown";
    }

    static const char*
    transfer_policy_reason_name(TransferPolicyReason reason) noexcept
    {
        switch (reason) {
        case TransferPolicyReason::Default: return "default";
        case TransferPolicyReason::NotPresent: return "not_present";
        case TransferPolicyReason::ExplicitDrop: return "explicit_drop";
        case TransferPolicyReason::CarrierDisabled: return "carrier_disabled";
        case TransferPolicyReason::ProjectedPayload: return "projected_payload";
        case TransferPolicyReason::DraftInvalidationPayload:
            return "draft_invalidation_payload";
        case TransferPolicyReason::ExternalSignedPayload:
            return "external_signed_payload";
        case TransferPolicyReason::ContentBoundTransferUnavailable:
            return "content_bound_transfer_unavailable";
        case TransferPolicyReason::SignedRewriteUnavailable:
            return "signed_rewrite_unavailable";
        case TransferPolicyReason::PortableInvalidationUnavailable:
            return "portable_invalidation_unavailable";
        case TransferPolicyReason::RewriteUnavailablePreservedRaw:
            return "rewrite_unavailable_preserved_raw";
        case TransferPolicyReason::TargetSerializationUnavailable:
            return "target_serialization_unavailable";
        case TransferPolicyReason::TargetImageProperties:
            return "target_image_properties";
        case TransferPolicyReason::SafetyModeFiltered:
            return "safety_mode_filtered";
        case TransferPolicyReason::RawDataDescriptorFiltered:
            return "raw_data_descriptor_filtered";
        case TransferPolicyReason::OpaquePayloadPreservedUnverified:
            return "opaque_payload_preserved_unverified";
        case TransferPolicyReason::RewriteUnavailableDropped:
            return "rewrite_unavailable_dropped";
        }
        return "unknown";
    }

    static const char* transfer_raw_carrier_passthrough_reason_name(
        TransferRawCarrierPassthroughReason reason) noexcept
    {
        switch (reason) {
        case TransferRawCarrierPassthroughReason::Candidate: return "candidate";
        case TransferRawCarrierPassthroughReason::MissingPayload:
            return "missing_payload";
        case TransferRawCarrierPassthroughReason::TargetIncompatible:
            return "target_incompatible";
        case TransferRawCarrierPassthroughReason::SafetyFiltered:
            return "safety_filtered";
        case TransferRawCarrierPassthroughReason::ContentBoundMetadata:
            return "content_bound_metadata";
        case TransferRawCarrierPassthroughReason::PolicyBlocked:
            return "policy_blocked";
        case TransferRawCarrierPassthroughReason::DecodeLinkUnavailable:
            return "decode_link_unavailable";
        case TransferRawCarrierPassthroughReason::UnsupportedKind:
            return "unsupported_kind";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_mode_name(TransferC2paMode mode) noexcept
    {
        switch (mode) {
        case TransferC2paMode::NotApplicable: return "not_applicable";
        case TransferC2paMode::NotPresent: return "not_present";
        case TransferC2paMode::Drop: return "drop";
        case TransferC2paMode::DraftUnsignedInvalidation:
            return "draft_unsigned_invalidation";
        case TransferC2paMode::PreserveRaw: return "preserve_raw";
        case TransferC2paMode::SignedRewrite: return "signed_rewrite";
        }
        return "unknown";
    }

    static const char*
    transfer_c2pa_source_kind_name(TransferC2paSourceKind kind) noexcept
    {
        switch (kind) {
        case TransferC2paSourceKind::NotApplicable: return "not_applicable";
        case TransferC2paSourceKind::NotPresent: return "not_present";
        case TransferC2paSourceKind::DecodedOnly: return "decoded_only";
        case TransferC2paSourceKind::ContentBound: return "content_bound";
        case TransferC2paSourceKind::DraftUnsignedInvalidation:
            return "draft_unsigned_invalidation";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_prepared_output_name(
        TransferC2paPreparedOutput output) noexcept
    {
        switch (output) {
        case TransferC2paPreparedOutput::NotApplicable: return "not_applicable";
        case TransferC2paPreparedOutput::NotPresent: return "not_present";
        case TransferC2paPreparedOutput::Dropped: return "dropped";
        case TransferC2paPreparedOutput::PreservedRaw: return "preserved_raw";
        case TransferC2paPreparedOutput::GeneratedDraftUnsignedInvalidation:
            return "generated_draft_unsigned_invalidation";
        case TransferC2paPreparedOutput::SignedRewrite: return "signed_rewrite";
        }
        return "unknown";
    }

    static const char*
    transfer_c2pa_rewrite_state_name(TransferC2paRewriteState state) noexcept
    {
        switch (state) {
        case TransferC2paRewriteState::NotApplicable: return "not_applicable";
        case TransferC2paRewriteState::NotRequested: return "not_requested";
        case TransferC2paRewriteState::SigningMaterialRequired:
            return "signing_material_required";
        case TransferC2paRewriteState::Ready: return "ready";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_rewrite_chunk_kind_name(
        TransferC2paRewriteChunkKind kind) noexcept
    {
        switch (kind) {
        case TransferC2paRewriteChunkKind::SourceRange: return "source_range";
        case TransferC2paRewriteChunkKind::PreparedJpegSegment:
            return "prepared_jpeg_segment";
        case TransferC2paRewriteChunkKind::PreparedJxlBox:
            return "prepared_jxl_box";
        case TransferC2paRewriteChunkKind::PreparedBmffMetaBox:
            return "prepared_bmff_meta_box";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_signed_payload_kind_name(
        TransferC2paSignedPayloadKind kind) noexcept
    {
        switch (kind) {
        case TransferC2paSignedPayloadKind::NotApplicable:
            return "not_applicable";
        case TransferC2paSignedPayloadKind::GenericJumbf:
            return "generic_jumbf";
        case TransferC2paSignedPayloadKind::DraftUnsignedInvalidation:
            return "draft_unsigned_invalidation";
        case TransferC2paSignedPayloadKind::ContentBound:
            return "content_bound";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_semantic_status_name(
        TransferC2paSemanticStatus status) noexcept
    {
        switch (status) {
        case TransferC2paSemanticStatus::NotChecked: return "not_checked";
        case TransferC2paSemanticStatus::Ok: return "ok";
        case TransferC2paSemanticStatus::Invalid: return "invalid";
        }
        return "unknown";
    }

    static const char*
    transfer_target_format_name(TransferTargetFormat format) noexcept
    {
        switch (format) {
        case TransferTargetFormat::Jpeg: return "jpeg";
        case TransferTargetFormat::Tiff: return "tiff";
        case TransferTargetFormat::Dng: return "dng";
        case TransferTargetFormat::Jxl: return "jxl";
        case TransferTargetFormat::Webp: return "webp";
        case TransferTargetFormat::Heif: return "heif";
        case TransferTargetFormat::Avif: return "avif";
        case TransferTargetFormat::Cr3: return "cr3";
        case TransferTargetFormat::Exr: return "exr";
        case TransferTargetFormat::Png: return "png";
        case TransferTargetFormat::Jp2: return "jp2";
        }
        return "unknown";
    }

    static std::vector<uint16_t>
    transfer_target_image_spec_bits(const TransferTargetImageSpec& spec)
    {
        std::vector<uint16_t> out;
        out.reserve(spec.bits_per_sample_count);
        for (uint16_t i = 0U; i < spec.bits_per_sample_count; ++i) {
            out.push_back(spec.bits_per_sample[i]);
        }
        return out;
    }

    static void
    transfer_target_image_spec_set_bits(TransferTargetImageSpec& spec,
                                        const std::vector<uint16_t>& values)
    {
        if (values.size() > kTransferTargetImageSpecMaxSamples) {
            throw std::runtime_error("bits_per_sample has too many values");
        }
        spec.bits_per_sample_count = static_cast<uint16_t>(values.size());
        spec.bits_per_sample.fill(0U);
        for (size_t i = 0; i < values.size(); ++i) {
            spec.bits_per_sample[i] = values[i];
        }
    }

    static std::vector<uint16_t> transfer_target_image_spec_sample_format(
        const TransferTargetImageSpec& spec)
    {
        std::vector<uint16_t> out;
        out.reserve(spec.sample_format_count);
        for (uint16_t i = 0U; i < spec.sample_format_count; ++i) {
            out.push_back(spec.sample_format[i]);
        }
        return out;
    }

    static void transfer_target_image_spec_set_sample_format(
        TransferTargetImageSpec& spec, const std::vector<uint16_t>& values)
    {
        if (values.size() > kTransferTargetImageSpecMaxSamples) {
            throw std::runtime_error("sample_format has too many values");
        }
        spec.sample_format_count = static_cast<uint16_t>(values.size());
        spec.sample_format.fill(0U);
        for (size_t i = 0; i < values.size(); ++i) {
            spec.sample_format[i] = values[i];
        }
    }

    static TransferTargetImageSpec
    transfer_target_image_spec_from_python(nb::object obj)
    {
        if (obj.is_none()) {
            return TransferTargetImageSpec();
        }
        return nb::cast<TransferTargetImageSpec>(obj);
    }

    static nb::dict metadata_capability_to_python(const MetadataCapability& cap)
    {
        nb::dict out;
        out["contract_version"] = nb::int_(
            kMetadataCapabilitiesContractVersion);
        out["format"]      = cap.format;
        out["format_name"] = nb::str(transfer_target_format_name(cap.format));
        out["family"]      = cap.family;
        out["family_name"] = nb::str(
            metadata_capability_family_name(cap.family));
        out["read"]      = cap.read;
        out["read_name"] = nb::str(metadata_capability_support_name(cap.read));
        out["read_available"] = nb::bool_(
            metadata_capability_available(cap.read));
        out["structured_decode"]      = cap.structured_decode;
        out["structured_decode_name"] = nb::str(
            metadata_capability_support_name(cap.structured_decode));
        out["structured_decode_available"] = nb::bool_(
            metadata_capability_available(cap.structured_decode));
        out["transfer_prepare"]      = cap.transfer_prepare;
        out["transfer_prepare_name"] = nb::str(
            metadata_capability_support_name(cap.transfer_prepare));
        out["transfer_prepare_available"] = nb::bool_(
            metadata_capability_available(cap.transfer_prepare));
        out["target_edit"]      = cap.target_edit;
        out["target_edit_name"] = nb::str(
            metadata_capability_support_name(cap.target_edit));
        out["target_edit_available"] = nb::bool_(
            metadata_capability_available(cap.target_edit));
        out["raw_preservation"]      = cap.raw_preservation;
        out["raw_preservation_name"] = nb::str(
            metadata_capability_support_name(cap.raw_preservation));
        out["raw_preservation_available"] = nb::bool_(
            metadata_capability_available(cap.raw_preservation));
        return out;
    }

    static nb::dict
    metadata_capability_query_to_python(TransferTargetFormat format,
                                        MetadataCapabilityFamily family)
    {
        const MetadataCapability cap = metadata_capability(format, family);
        return metadata_capability_to_python(cap);
    }

    static const char*
    transfer_adapter_op_kind_name(TransferAdapterOpKind kind) noexcept
    {
        switch (kind) {
        case TransferAdapterOpKind::JpegMarker: return "jpeg_marker";
        case TransferAdapterOpKind::TiffTagBytes: return "tiff_tag";
        case TransferAdapterOpKind::JxlBox: return "jxl_box";
        case TransferAdapterOpKind::JxlIccProfile: return "jxl_icc_profile";
        case TransferAdapterOpKind::WebpChunk: return "webp_chunk";
        case TransferAdapterOpKind::PngChunk: return "png_chunk";
        case TransferAdapterOpKind::Jp2Box: return "jp2_box";
        case TransferAdapterOpKind::ExrAttribute: return "exr_attribute";
        case TransferAdapterOpKind::BmffItem: return "bmff_item";
        case TransferAdapterOpKind::BmffProperty: return "bmff_property";
        }
        return "unknown";
    }

    static TransferStatus
    transfer_status_from_file_status(TransferFileStatus status) noexcept
    {
        switch (status) {
        case TransferFileStatus::Ok: return TransferStatus::Ok;
        case TransferFileStatus::InvalidArgument:
            return TransferStatus::InvalidArgument;
        case TransferFileStatus::TooLarge: return TransferStatus::LimitExceeded;
        case TransferFileStatus::OpenFailed:
        case TransferFileStatus::StatFailed:
        case TransferFileStatus::MapFailed:
        case TransferFileStatus::ReadFailed: return TransferStatus::Unsupported;
        }
        return TransferStatus::InternalError;
    }

    static std::string canonical_time_patch_name(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (std::isalnum(c) == 0) {
                continue;
            }
            const int lower = std::tolower(c);
            out.push_back(static_cast<char>(lower));
        }
        return out;
    }

    static bool parse_time_patch_field(std::string_view name,
                                       TimePatchField* out)
    {
        if (!out) {
            return false;
        }
        const std::string n = canonical_time_patch_name(name);
        if (n == "datetime") {
            *out = TimePatchField::DateTime;
            return true;
        }
        if (n == "datetimeoriginal") {
            *out = TimePatchField::DateTimeOriginal;
            return true;
        }
        if (n == "datetimedigitized") {
            *out = TimePatchField::DateTimeDigitized;
            return true;
        }
        if (n == "subsectime") {
            *out = TimePatchField::SubSecTime;
            return true;
        }
        if (n == "subsectimeoriginal") {
            *out = TimePatchField::SubSecTimeOriginal;
            return true;
        }
        if (n == "subsectimedigitized") {
            *out = TimePatchField::SubSecTimeDigitized;
            return true;
        }
        if (n == "offsettime") {
            *out = TimePatchField::OffsetTime;
            return true;
        }
        if (n == "offsettimeoriginal") {
            *out = TimePatchField::OffsetTimeOriginal;
            return true;
        }
        if (n == "offsettimedigitized") {
            *out = TimePatchField::OffsetTimeDigitized;
            return true;
        }
        if (n == "gpsdatestamp") {
            *out = TimePatchField::GpsDateStamp;
            return true;
        }
        if (n == "gpstimestamp") {
            *out = TimePatchField::GpsTimeStamp;
            return true;
        }
        return false;
    }

    static std::vector<TransferTimePatchInput>
    parse_time_patches_object(nb::object time_patches_obj)
    {
        std::vector<TransferTimePatchInput> out;
        if (time_patches_obj.is_none()) {
            return out;
        }

        nb::dict patch_dict = nb::cast<nb::dict>(time_patches_obj);
        PyObject* dict_obj  = patch_dict.ptr();
        if (!dict_obj || !PyDict_Check(dict_obj)) {
            throw std::runtime_error(
                "time_patches must be a dict[str, str|bytes]");
        }

        PyObject* py_key   = nullptr;
        PyObject* py_value = nullptr;
        Py_ssize_t pos     = 0;
        while (PyDict_Next(dict_obj, &pos, &py_key, &py_value) != 0) {
            if (!PyUnicode_Check(py_key)) {
                throw std::runtime_error(
                    "time_patches key must be str field name");
            }
            Py_ssize_t key_n  = 0;
            const char* key_s = PyUnicode_AsUTF8AndSize(py_key, &key_n);
            if (!key_s || key_n <= 0) {
                throw std::runtime_error("invalid time patch field name");
            }

            TransferTimePatchInput one;
            if (!parse_time_patch_field(
                    std::string_view(key_s, static_cast<size_t>(key_n)),
                    &one.field)) {
                throw std::runtime_error("unknown time patch field");
            }

            if (PyUnicode_Check(py_value)) {
                Py_ssize_t n  = 0;
                const char* s = PyUnicode_AsUTF8AndSize(py_value, &n);
                if (!s || n < 0) {
                    throw std::runtime_error(
                        "failed to encode time patch text value as UTF-8");
                }
                one.text_value = true;
                one.value.resize(static_cast<size_t>(n));
                for (size_t i = 0; i < one.value.size(); ++i) {
                    one.value[i] = static_cast<std::byte>(
                        static_cast<unsigned char>(s[i]));
                }
            } else if (PyBytes_Check(py_value)) {
                char* s      = nullptr;
                Py_ssize_t n = 0;
                if (PyBytes_AsStringAndSize(py_value, &s, &n) != 0) {
                    throw std::runtime_error(
                        "failed to read bytes time patch value");
                }
                if (!s || n < 0) {
                    throw std::runtime_error("invalid bytes time patch value");
                }
                one.text_value = false;
                one.value.resize(static_cast<size_t>(n));
                for (size_t i = 0; i < one.value.size(); ++i) {
                    one.value[i] = static_cast<std::byte>(
                        static_cast<unsigned char>(s[i]));
                }
            } else {
                throw std::runtime_error(
                    "time_patches value must be str or bytes");
            }

            out.push_back(std::move(one));
        }
        return out;
    }

    static nb::dict
    inspect_transfer_payload_batch_to_python(nb::object payload_batch_obj,
                                             bool include_payloads,
                                             bool unsafe_payload_access)
    {
        if (payload_batch_obj.is_none()) {
            throw std::runtime_error(
                "transfer_payload_batch must be bytes-like");
        }

        const std::vector<std::byte> payload_batch_bytes
            = bytes_object_to_vector(payload_batch_obj);
        PreparedTransferPayloadBatch batch;
        const PreparedTransferPayloadIoResult batch_io
            = deserialize_prepared_transfer_payload_batch(
                std::span<const std::byte>(payload_batch_bytes.data(),
                                           payload_batch_bytes.size()),
                &batch);

        std::vector<PreparedTransferPayloadView> payload_views;
        EmitTransferResult inspect_result;
        if (batch_io.status == TransferStatus::Ok) {
            inspect_result
                = collect_prepared_transfer_payload_views(batch,
                                                          &payload_views);
        } else {
            inspect_result.status  = batch_io.status;
            inspect_result.code    = batch_io.code;
            inspect_result.errors  = batch_io.errors;
            inspect_result.message = batch_io.message;
        }

        nb::dict out;
        out["requested_payload_bytes"] = nb::bool_(include_payloads);
        out["status"]                  = batch_io.status;
        out["status_name"]   = nb::str(transfer_status_name(batch_io.status));
        out["code"]          = batch_io.code;
        out["code_name"]     = nb::str(emit_transfer_code_name(batch_io.code));
        out["bytes_read"]    = nb::int_(batch_io.bytes);
        out["errors"]        = nb::int_(batch_io.errors);
        out["message"]       = nb::str(batch_io.message.c_str(),
                                       batch_io.message.size());
        out["target_format"] = batch.target_format;
        out["target_format_name"] = nb::str(
            transfer_target_format_name(batch.target_format));
        out["payload_count"] = nb::int_(payload_views.size());

        const bool allow_payload_bytes = include_payloads
                                         && unsafe_payload_access;
        nb::list payloads;
        for (size_t i = 0; i < payload_views.size(); ++i) {
            const PreparedTransferPayloadView& view = payload_views[i];
            nb::dict one;
            one["index"]         = nb::int_(static_cast<uint32_t>(i));
            one["semantic_kind"] = nb::int_(
                static_cast<uint32_t>(view.semantic_kind));
            one["semantic_name"] = nb::str(view.semantic_name.data(),
                                           view.semantic_name.size());
            one["route"] = nb::str(view.route.data(), view.route.size());
            one["size"]  = nb::int_(static_cast<uint64_t>(view.payload.size()));
            one["op_kind"]      = nb::int_(static_cast<uint32_t>(view.op.kind));
            one["op_kind_name"] = nb::str(
                transfer_adapter_op_kind_name(view.op.kind));
            one["jpeg_marker_code"]      = nb::int_(view.op.jpeg_marker_code);
            one["tiff_tag"]              = nb::int_(view.op.tiff_tag);
            one["box_type"]              = nb::str(view.op.box_type.data(),
                                                   view.op.box_type.size());
            one["chunk_type"]            = nb::str(view.op.chunk_type.data(),
                                                   view.op.chunk_type.size());
            one["bmff_item_type"]        = nb::int_(view.op.bmff_item_type);
            one["bmff_property_type"]    = nb::int_(view.op.bmff_property_type);
            one["bmff_property_subtype"] = nb::int_(
                view.op.bmff_property_subtype);
            one["bmff_mime_xmp"] = nb::bool_(view.op.bmff_mime_xmp);
            one["compress"]      = nb::bool_(view.op.compress);
            if (allow_payload_bytes) {
                one["payload"] = nb::bytes(reinterpret_cast<const char*>(
                                               view.payload.data()),
                                           view.payload.size());
            } else {
                one["payload"] = nb::none();
            }
            payloads.append(std::move(one));
        }
        out["payloads"] = std::move(payloads);

        TransferStatus overall_status = TransferStatus::Ok;
        std::string error_stage       = "none";
        std::string error_code        = "none";
        std::string error_message;
        if (include_payloads && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_payloads_forbidden";
            error_message
                = "safe inspect_transfer_payload_batch forbids payload bytes; "
                  "use unsafe_inspect_transfer_payload_batch("
                  "include_payloads=True)";
        } else if (batch_io.status != TransferStatus::Ok) {
            overall_status = batch_io.status;
            error_stage    = "payload_batch";
            error_code     = emit_transfer_code_name(batch_io.code);
            error_message  = batch_io.message;
        } else if (inspect_result.status != TransferStatus::Ok) {
            overall_status = inspect_result.status;
            error_stage    = "inspect";
            error_code     = emit_transfer_code_name(inspect_result.code);
            error_message  = inspect_result.message;
        }
        out["overall_status"]      = overall_status;
        out["overall_status_name"] = nb::str(
            transfer_status_name(overall_status));
        out["error_stage"]   = nb::str(error_stage.c_str(), error_stage.size());
        out["error_code"]    = nb::str(error_code.c_str(), error_code.size());
        out["error_message"] = nb::str(error_message.c_str(),
                                       error_message.size());
        return out;
    }

    static const char*
    transfer_package_chunk_kind_name(TransferPackageChunkKind kind) noexcept
    {
        switch (kind) {
        case TransferPackageChunkKind::SourceRange: return "source_range";
        case TransferPackageChunkKind::InlineBytes: return "inline_bytes";
        case TransferPackageChunkKind::PreparedTransferBlock:
            return "prepared_transfer_block";
        case TransferPackageChunkKind::PreparedJpegSegment:
            return "prepared_jpeg_segment";
        }
        return "unknown";
    }

    static nb::dict
    inspect_transfer_package_batch_to_python(nb::object package_batch_obj,
                                             bool include_chunk_bytes,
                                             bool unsafe_payload_access)
    {
        if (package_batch_obj.is_none()) {
            throw std::runtime_error(
                "transfer_package_batch must be bytes-like");
        }

        const std::vector<std::byte> package_batch_bytes
            = bytes_object_to_vector(package_batch_obj);
        PreparedTransferPackageBatch batch;
        const PreparedTransferPackageIoResult batch_io
            = deserialize_prepared_transfer_package_batch(
                std::span<const std::byte>(package_batch_bytes.data(),
                                           package_batch_bytes.size()),
                &batch);

        std::vector<PreparedTransferPackageView> chunk_views;
        EmitTransferResult inspect_result;
        if (batch_io.status == TransferStatus::Ok) {
            inspect_result
                = collect_prepared_transfer_package_views(batch, &chunk_views);
        } else {
            inspect_result.status  = batch_io.status;
            inspect_result.code    = batch_io.code;
            inspect_result.errors  = batch_io.errors;
            inspect_result.message = batch_io.message;
        }

        nb::dict out;
        out["requested_chunk_bytes"] = nb::bool_(include_chunk_bytes);
        out["status"]                = batch_io.status;
        out["status_name"]   = nb::str(transfer_status_name(batch_io.status));
        out["code"]          = batch_io.code;
        out["code_name"]     = nb::str(emit_transfer_code_name(batch_io.code));
        out["bytes_read"]    = nb::int_(batch_io.bytes);
        out["errors"]        = nb::int_(batch_io.errors);
        out["message"]       = nb::str(batch_io.message.c_str(),
                                       batch_io.message.size());
        out["target_format"] = batch.target_format;
        out["target_format_name"] = nb::str(
            transfer_target_format_name(batch.target_format));
        out["chunk_count"] = nb::int_(chunk_views.size());

        const bool allow_chunk_bytes = include_chunk_bytes
                                       && unsafe_payload_access;
        nb::list chunks;
        for (size_t i = 0; i < chunk_views.size(); ++i) {
            const PreparedTransferPackageView& view = chunk_views[i];
            nb::dict one;
            one["index"]         = nb::int_(static_cast<uint32_t>(i));
            one["semantic_kind"] = nb::int_(
                static_cast<uint32_t>(view.semantic_kind));
            const std::string_view semantic_name = transfer_semantic_name(
                view.semantic_kind);
            one["semantic_name"] = nb::str(semantic_name.data(),
                                           semantic_name.size());
            one["route"]        = nb::str(view.route.data(), view.route.size());
            one["package_kind"] = nb::int_(
                static_cast<uint32_t>(view.package_kind));
            one["package_kind_name"] = nb::str(
                transfer_package_chunk_kind_name(view.package_kind));
            one["output_offset"]    = nb::int_(view.output_offset);
            one["jpeg_marker_code"] = nb::int_(view.jpeg_marker_code);
            one["size"] = nb::int_(static_cast<uint64_t>(view.bytes.size()));
            if (allow_chunk_bytes) {
                one["bytes"] = nb::bytes(reinterpret_cast<const char*>(
                                             view.bytes.data()),
                                         view.bytes.size());
            } else {
                one["bytes"] = nb::none();
            }
            chunks.append(std::move(one));
        }
        out["chunks"] = std::move(chunks);

        TransferStatus overall_status = TransferStatus::Ok;
        std::string error_stage       = "none";
        std::string error_code        = "none";
        std::string error_message;
        if (include_chunk_bytes && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_chunk_bytes_forbidden";
            error_message
                = "safe inspect_transfer_package_batch forbids chunk bytes; "
                  "use unsafe_inspect_transfer_package_batch("
                  "include_chunk_bytes=True)";
        } else if (batch_io.status != TransferStatus::Ok) {
            overall_status = batch_io.status;
            error_stage    = "package_batch";
            error_code     = emit_transfer_code_name(batch_io.code);
            error_message  = batch_io.message;
        } else if (inspect_result.status != TransferStatus::Ok) {
            overall_status = inspect_result.status;
            error_stage    = "inspect";
            error_code     = emit_transfer_code_name(inspect_result.code);
            error_message  = inspect_result.message;
        }
        out["overall_status"]      = overall_status;
        out["overall_status_name"] = nb::str(
            transfer_status_name(overall_status));
        out["error_stage"]   = nb::str(error_stage.c_str(), error_stage.size());
        out["error_code"]    = nb::str(error_code.c_str(), error_code.size());
        out["error_message"] = nb::str(error_message.c_str(),
                                       error_message.size());
        return out;
    }

    static nb::dict
    inspect_jxl_encoder_handoff_to_python(nb::object handoff_obj,
                                          bool include_icc_profile,
                                          bool unsafe_payload_access)
    {
        if (handoff_obj.is_none()) {
            throw std::runtime_error("jxl_encoder_handoff must be bytes-like");
        }

        const std::vector<std::byte> handoff_bytes = bytes_object_to_vector(
            handoff_obj);
        PreparedJxlEncoderHandoff handoff;
        const PreparedJxlEncoderHandoffIoResult handoff_io
            = deserialize_prepared_jxl_encoder_handoff(
                std::span<const std::byte>(handoff_bytes.data(),
                                           handoff_bytes.size()),
                &handoff);

        nb::dict out;
        out["requested_icc_profile_bytes"] = nb::bool_(include_icc_profile);
        out["status"]                      = handoff_io.status;
        out["status_name"] = nb::str(transfer_status_name(handoff_io.status));
        out["code"]        = handoff_io.code;
        out["code_name"]   = nb::str(emit_transfer_code_name(handoff_io.code));
        out["bytes_read"]  = nb::int_(handoff_io.bytes);
        out["errors"]      = nb::int_(handoff_io.errors);
        out["message"]     = nb::str(handoff_io.message.c_str(),
                                     handoff_io.message.size());
        out["has_icc_profile"]   = nb::bool_(handoff.has_icc_profile);
        out["icc_block_index"]   = nb::int_(handoff.icc_block_index);
        out["icc_profile_bytes"] = nb::int_(
            static_cast<uint64_t>(handoff.icc_profile.size()));
        out["box_count"]         = nb::int_(handoff.box_count);
        out["box_payload_bytes"] = nb::int_(handoff.box_payload_bytes);
        if (include_icc_profile && unsafe_payload_access
            && handoff_io.status == TransferStatus::Ok) {
            out["icc_profile"] = nb::bytes(reinterpret_cast<const char*>(
                                               handoff.icc_profile.data()),
                                           handoff.icc_profile.size());
        } else {
            out["icc_profile"] = nb::none();
        }

        TransferStatus overall_status = TransferStatus::Ok;
        std::string error_stage       = "none";
        std::string error_code        = "none";
        std::string error_message;
        if (include_icc_profile && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_icc_profile_forbidden";
            error_message
                = "safe inspect_jxl_encoder_handoff forbids icc profile "
                  "bytes; use unsafe_inspect_jxl_encoder_handoff("
                  "include_icc_profile=True)";
        } else if (handoff_io.status != TransferStatus::Ok) {
            overall_status = handoff_io.status;
            error_stage    = "jxl_encoder_handoff";
            error_code     = emit_transfer_code_name(handoff_io.code);
            error_message  = handoff_io.message;
        }
        out["overall_status"]      = overall_status;
        out["overall_status_name"] = nb::str(
            transfer_status_name(overall_status));
        out["error_stage"]   = nb::str(error_stage.c_str(), error_stage.size());
        out["error_code"]    = nb::str(error_code.c_str(), error_code.size());
        out["error_message"] = nb::str(error_message.c_str(),
                                       error_message.size());
        return out;
    }

    static nb::dict inspect_transfer_artifact_to_python(nb::object artifact_obj)
    {
        if (artifact_obj.is_none()) {
            throw std::runtime_error("transfer_artifact must be bytes-like");
        }

        const std::vector<std::byte> artifact_bytes = bytes_object_to_vector(
            artifact_obj);
        PreparedTransferArtifactInfo info;
        const PreparedTransferArtifactIoResult artifact_io
            = inspect_prepared_transfer_artifact(
                std::span<const std::byte>(artifact_bytes.data(),
                                           artifact_bytes.size()),
                &info);

        nb::dict out;
        out["status"]      = artifact_io.status;
        out["status_name"] = nb::str(transfer_status_name(artifact_io.status));
        out["code"]        = artifact_io.code;
        out["code_name"]   = nb::str(emit_transfer_code_name(artifact_io.code));
        out["bytes_read"]  = nb::int_(artifact_io.bytes);
        out["errors"]      = nb::int_(artifact_io.errors);
        out["message"]     = nb::str(artifact_io.message.c_str(),
                                     artifact_io.message.size());
        out["kind"]        = nb::int_(static_cast<uint32_t>(info.kind));
        {
            const std::string_view kind_name
                = prepared_transfer_artifact_kind_name(info.kind);
            out["kind_name"] = nb::str(kind_name.data(), kind_name.size());
        }
        out["has_contract_version"] = nb::bool_(info.has_contract_version);
        out["contract_version"]     = nb::int_(info.contract_version);
        out["has_target_format"]    = nb::bool_(info.has_target_format);
        if (info.has_target_format) {
            out["target_format"]      = info.target_format;
            out["target_format_name"] = nb::str(
                transfer_target_format_name(info.target_format));
        } else {
            out["target_format"]      = nb::none();
            out["target_format_name"] = nb::none();
        }
        out["entry_count"]          = nb::int_(info.entry_count);
        out["payload_bytes"]        = nb::int_(info.payload_bytes);
        out["binding_bytes"]        = nb::int_(info.binding_bytes);
        out["signed_payload_bytes"] = nb::int_(info.signed_payload_bytes);
        out["has_icc_profile"]      = nb::bool_(info.has_icc_profile);
        out["icc_block_index"]      = nb::int_(info.icc_block_index);
        out["icc_profile_bytes"]    = nb::int_(info.icc_profile_bytes);
        out["box_payload_bytes"]    = nb::int_(info.box_payload_bytes);
        out["carrier_route"]        = nb::str(info.carrier_route.c_str(),
                                              info.carrier_route.size());
        out["manifest_label"]       = nb::str(info.manifest_label.c_str(),
                                              info.manifest_label.size());
        out["overall_status"]       = artifact_io.status;
        out["overall_status_name"]  = nb::str(
            transfer_status_name(artifact_io.status));
        out["error_stage"] = nb::str("artifact");
        out["error_code"]  = nb::str(emit_transfer_code_name(artifact_io.code));
        out["error_message"] = nb::str(artifact_io.message.c_str(),
                                       artifact_io.message.size());
        return out;
    }

    static void append_persist_result_to_python(
        const PersistPreparedTransferFileResult& persisted,
        bool persist_requested, nb::dict* out)
    {
        if (!out) {
            return;
        }
        (*out)["persist_requested"]   = nb::bool_(persist_requested);
        (*out)["persist_status"]      = persisted.status;
        (*out)["persist_status_name"] = nb::str(
            transfer_status_name(persisted.status));
        (*out)["persist_message"] = nb::str(persisted.message.c_str(),
                                            persisted.message.size());

        (*out)["persist_output_status"]      = persisted.output_status;
        (*out)["persist_output_status_name"] = nb::str(
            transfer_status_name(persisted.output_status));
        (*out)["persist_output_message"]
            = nb::str(persisted.output_message.c_str(),
                      persisted.output_message.size());
        (*out)["persist_output_path"]  = nb::str(persisted.output_path.c_str(),
                                                 persisted.output_path.size());
        (*out)["persist_output_bytes"] = nb::int_(persisted.output_bytes);

        (*out)["persist_xmp_sidecar_status"] = persisted.xmp_sidecar_status;
        (*out)["persist_xmp_sidecar_status_name"] = nb::str(
            transfer_status_name(persisted.xmp_sidecar_status));
        (*out)["persist_xmp_sidecar_message"]
            = nb::str(persisted.xmp_sidecar_message.c_str(),
                      persisted.xmp_sidecar_message.size());
        (*out)["persist_xmp_sidecar_path"]
            = nb::str(persisted.xmp_sidecar_path.c_str(),
                      persisted.xmp_sidecar_path.size());
        (*out)["persist_xmp_sidecar_bytes"] = nb::int_(
            persisted.xmp_sidecar_bytes);

        (*out)["persist_xmp_sidecar_cleanup_status"]
            = persisted.xmp_sidecar_cleanup_status;
        (*out)["persist_xmp_sidecar_cleanup_status_name"] = nb::str(
            transfer_status_name(persisted.xmp_sidecar_cleanup_status));
        (*out)["persist_xmp_sidecar_cleanup_message"]
            = nb::str(persisted.xmp_sidecar_cleanup_message.c_str(),
                      persisted.xmp_sidecar_cleanup_message.size());
        (*out)["persist_xmp_sidecar_cleanup_path"]
            = nb::str(persisted.xmp_sidecar_cleanup_path.c_str(),
                      persisted.xmp_sidecar_cleanup_path.size());
        (*out)["persist_xmp_sidecar_cleanup_removed"] = nb::bool_(
            persisted.xmp_sidecar_cleanup_removed);
    }

    static uint64_t transfer_source_snapshot_raw_carrier_count(
        const TransferSourceSnapshot& snapshot) noexcept
    {
        return static_cast<uint64_t>(snapshot.raw_carriers.size());
    }

    static uint64_t transfer_source_snapshot_raw_carrier_bytes(
        const TransferSourceSnapshot& snapshot) noexcept
    {
        return snapshot.raw_carrier_bytes;
    }

    static bool transfer_source_snapshot_raw_carrier_bytes_truncated(
        const TransferSourceSnapshot& snapshot) noexcept
    {
        return snapshot.raw_carrier_bytes_truncated;
    }

    static nb::dict transfer_source_raw_carrier_to_python(
        const TransferSourceRawCarrier& carrier, bool include_payload)
    {
        nb::dict out;
        out["order"] = nb::int_(carrier.order);
        out["route"] = nb::str(carrier.route.c_str(), carrier.route.size());
        out["semantic_kind"]      = carrier.semantic_kind;
        out["semantic_kind_name"] = nb::str(
            transfer_block_kind_name(carrier.semantic_kind));
        out["format"]              = carrier.block.format;
        out["kind"]                = carrier.block.kind;
        out["compression"]         = carrier.block.compression;
        out["chunking"]            = carrier.block.chunking;
        out["outer_offset"]        = nb::int_(carrier.block.outer_offset);
        out["outer_size"]          = nb::int_(carrier.block.outer_size);
        out["data_offset"]         = nb::int_(carrier.block.data_offset);
        out["data_size"]           = nb::int_(carrier.block.data_size);
        out["id"]                  = nb::int_(carrier.block.id);
        out["part_index"]          = nb::int_(carrier.block.part_index);
        out["part_count"]          = nb::int_(carrier.block.part_count);
        out["logical_offset"]      = nb::int_(carrier.block.logical_offset);
        out["logical_size"]        = nb::int_(carrier.block.logical_size);
        out["group"]               = nb::int_(carrier.block.group);
        out["aux_u32"]             = nb::int_(carrier.block.aux_u32);
        out["payload_preserved"]   = nb::bool_(carrier.payload_preserved);
        out["payload_size"]        = nb::int_(carrier.payload.size());
        out["decoded_entry_count"] = nb::int_(carrier.decoded_entry_ids.size());
        nb::list decoded_entry_ids;
        for (EntryId id : carrier.decoded_entry_ids) {
            decoded_entry_ids.append(nb::int_(id));
        }
        out["decoded_entry_ids"] = std::move(decoded_entry_ids);
        if (include_payload && carrier.payload_preserved) {
            out["payload"] = nb::bytes(reinterpret_cast<const char*>(
                                           carrier.payload.data()),
                                       carrier.payload.size());
        } else {
            out["payload"] = nb::none();
        }
        return out;
    }

    static nb::list transfer_source_snapshot_raw_carriers_to_python(
        const TransferSourceSnapshot& snapshot, bool include_payload)
    {
        nb::list out;
        for (const TransferSourceRawCarrier& carrier : snapshot.raw_carriers) {
            out.append(transfer_source_raw_carrier_to_python(carrier,
                                                             include_payload));
        }
        return out;
    }

    static nb::dict read_transfer_source_snapshot_file_to_python(
        const std::string& path,
        const ReadTransferSourceSnapshotFileResult& result)
    {
        nb::dict out;
        out["path"]             = nb::str(path.c_str(), path.size());
        out["file_status"]      = result.file_status;
        out["file_status_name"] = nb::str(
            transfer_file_status_name(result.file_status));
        out["code"]      = result.code;
        out["code_name"] = nb::str(
            read_transfer_source_snapshot_file_code_name(result.code));
        out["file_size"]                   = nb::int_(result.file_size);
        out["entry_count"]                 = nb::int_(result.entry_count);
        out["raw_carrier_count"]           = nb::int_(result.raw_carrier_count);
        out["raw_carrier_bytes"]           = nb::int_(result.raw_carrier_bytes);
        out["raw_carrier_bytes_truncated"] = nb::bool_(
            result.raw_carrier_bytes_truncated);
        out["scan_status"]    = result.read.scan.status;
        out["payload_status"] = result.read.payload.status;
        out["exif_status"]    = result.read.exif.status;
        out["xmp_status"]     = result.read.xmp.status;
        out["exr_status"]     = result.read.exr.status;
        out["jumbf_status"]   = result.read.jumbf.status;
        if (result.file_status == TransferFileStatus::Ok) {
            out["snapshot"]            = result.snapshot;
            out["overall_status"]      = TransferStatus::Ok;
            out["overall_status_name"] = nb::str("ok");
            out["error_stage"]         = nb::str("none");
            out["error_code"]          = nb::str("none");
            out["error_message"]       = nb::str("");
        } else {
            const TransferStatus overall_status
                = transfer_status_from_file_status(result.file_status);
            out["snapshot"]            = nb::none();
            out["overall_status"]      = overall_status;
            out["overall_status_name"] = nb::str(
                transfer_status_name(overall_status));
            out["error_stage"] = nb::str("read_snapshot");
            out["error_code"]  = nb::str(
                read_transfer_source_snapshot_file_code_name(result.code));
            out["error_message"] = nb::str(
                transfer_file_status_message(result.file_status));
        }
        return out;
    }

    static nb::dict read_transfer_source_snapshot_bytes_to_python(
        const ReadTransferSourceSnapshotBytesResult& result)
    {
        nb::dict out;
        out["status"]      = result.status;
        out["status_name"] = nb::str(transfer_status_name(result.status));
        out["code"]        = result.code;
        out["code_name"]   = nb::str(
            read_transfer_source_snapshot_bytes_code_name(result.code));
        out["input_size"]                  = nb::int_(result.input_size);
        out["entry_count"]                 = nb::int_(result.entry_count);
        out["raw_carrier_count"]           = nb::int_(result.raw_carrier_count);
        out["raw_carrier_bytes"]           = nb::int_(result.raw_carrier_bytes);
        out["raw_carrier_bytes_truncated"] = nb::bool_(
            result.raw_carrier_bytes_truncated);
        out["scan_status"]    = result.read.scan.status;
        out["payload_status"] = result.read.payload.status;
        out["exif_status"]    = result.read.exif.status;
        out["xmp_status"]     = result.read.xmp.status;
        out["exr_status"]     = result.read.exr.status;
        out["jumbf_status"]   = result.read.jumbf.status;
        if (result.status == TransferStatus::Ok) {
            out["snapshot"]            = result.snapshot;
            out["overall_status"]      = TransferStatus::Ok;
            out["overall_status_name"] = nb::str("ok");
            out["error_stage"]         = nb::str("none");
            out["error_code"]          = nb::str("none");
            out["error_message"]       = nb::str("");
        } else {
            out["snapshot"]            = nb::none();
            out["overall_status"]      = result.status;
            out["overall_status_name"] = nb::str(
                transfer_status_name(result.status));
            out["error_stage"] = nb::str("read_snapshot");
            out["error_code"]  = nb::str(
                read_transfer_source_snapshot_bytes_code_name(result.code));
            if (result.code
                == ReadTransferSourceSnapshotBytesCode::
                    PayloadBufferPlatformLimit) {
                out["error_message"] = nb::str(
                    "snapshot decode payload buffer exceeded platform span "
                    "limits");
            } else {
                out["error_message"] = nb::str("snapshot decode failed");
            }
        }
        return out;
    }

    static nb::dict transfer_snapshot_result_to_python(
        const ExecutePreparedTransferFileResult& result,
        const PersistPreparedTransferFileResult& persisted,
        bool persist_requested, bool edit_do_apply, bool include_edited_bytes,
        bool unsafe_edited_bytes_access)
    {
        const PrepareTransferFileResult& prepared = result.prepared;
        const ExecutePreparedTransferResult& exec = result.execute;

        nb::dict out;
        out["path"]             = nb::none();
        out["file_status"]      = prepared.file_status;
        out["file_status_name"] = nb::str(
            transfer_file_status_name(prepared.file_status));
        out["file_code"]      = prepared.code;
        out["file_code_name"] = nb::str(
            prepare_transfer_file_code_name(prepared.code));
        out["file_size"]           = nb::int_(prepared.file_size);
        out["entry_count"]         = nb::int_(prepared.entry_count);
        out["scan_status"]         = prepared.read.scan.status;
        out["payload_status"]      = prepared.read.payload.status;
        out["exif_status"]         = prepared.read.exif.status;
        out["xmp_status"]          = prepared.read.xmp.status;
        out["exr_status"]          = prepared.read.exr.status;
        out["jumbf_status"]        = prepared.read.jumbf.status;
        out["prepare_status"]      = prepared.prepare.status;
        out["prepare_status_name"] = nb::str(
            transfer_status_name(prepared.prepare.status));
        out["prepare_code"]      = prepared.prepare.code;
        out["prepare_code_name"] = nb::str(
            prepare_transfer_code_name(prepared.prepare.code));
        out["prepare_warnings"] = nb::int_(prepared.prepare.warnings);
        out["prepare_errors"]   = nb::int_(prepared.prepare.errors);
        out["prepare_message"]  = nb::str(prepared.prepare.message.c_str(),
                                          prepared.prepare.message.size());
        out["xmp_existing_sidecar_loaded"] = nb::bool_(
            prepared.xmp_existing_sidecar_loaded);
        out["xmp_existing_sidecar_status"]
            = prepared.xmp_existing_sidecar_status;
        out["xmp_existing_sidecar_status_name"] = nb::str(
            transfer_status_name(prepared.xmp_existing_sidecar_status));
        out["xmp_existing_sidecar_message"]
            = nb::str(prepared.xmp_existing_sidecar_message.c_str(),
                      prepared.xmp_existing_sidecar_message.size());
        out["xmp_existing_sidecar_path"]
            = nb::str(prepared.xmp_existing_sidecar_path.c_str(),
                      prepared.xmp_existing_sidecar_path.size());
        out["xmp_existing_destination_embedded_loaded"] = nb::bool_(
            result.xmp_existing_destination_embedded_loaded);
        out["xmp_existing_destination_embedded_status"]
            = result.xmp_existing_destination_embedded_status;
        out["xmp_existing_destination_embedded_status_name"] = nb::str(
            transfer_status_name(
                result.xmp_existing_destination_embedded_status));
        out["xmp_existing_destination_embedded_message"]
            = nb::str(result.xmp_existing_destination_embedded_message.c_str(),
                      result.xmp_existing_destination_embedded_message.size());
        out["xmp_existing_destination_embedded_path"]
            = nb::str(result.xmp_existing_destination_embedded_path.c_str(),
                      result.xmp_existing_destination_embedded_path.size());
        out["xmp_sidecar_requested"] = nb::bool_(result.xmp_sidecar_requested);
        out["xmp_sidecar_status"]    = result.xmp_sidecar_status;
        out["xmp_sidecar_status_name"] = nb::str(
            transfer_status_name(result.xmp_sidecar_status));
        out["xmp_sidecar_message"] = nb::str(result.xmp_sidecar_message.c_str(),
                                             result.xmp_sidecar_message.size());
        out["xmp_sidecar_path"]    = nb::str(result.xmp_sidecar_path.c_str(),
                                             result.xmp_sidecar_path.size());
        out["xmp_sidecar_bytes"]   = nb::int_(
            static_cast<uint64_t>(result.xmp_sidecar_output.size()));
        out["xmp_sidecar_cleanup_requested"] = nb::bool_(
            result.xmp_sidecar_cleanup_requested);
        out["xmp_sidecar_cleanup_status"] = result.xmp_sidecar_cleanup_status;
        out["xmp_sidecar_cleanup_status_name"] = nb::str(
            transfer_status_name(result.xmp_sidecar_cleanup_status));
        out["xmp_sidecar_cleanup_message"]
            = nb::str(result.xmp_sidecar_cleanup_message.c_str(),
                      result.xmp_sidecar_cleanup_message.size());
        out["xmp_sidecar_cleanup_path"]
            = nb::str(result.xmp_sidecar_cleanup_path.c_str(),
                      result.xmp_sidecar_cleanup_path.size());
        if (!result.xmp_sidecar_output.empty()) {
            out["xmp_sidecar_output"] = nb::bytes(
                reinterpret_cast<const char*>(result.xmp_sidecar_output.data()),
                result.xmp_sidecar_output.size());
        } else {
            out["xmp_sidecar_output"] = nb::none();
        }
        append_persist_result_to_python(persisted, persist_requested, &out);

        out["time_patch_status"]      = exec.time_patch.status;
        out["time_patch_status_name"] = nb::str(
            transfer_status_name(exec.time_patch.status));
        out["time_patch_patched_slots"] = nb::int_(
            exec.time_patch.patched_slots);
        out["time_patch_skipped_slots"] = nb::int_(
            exec.time_patch.skipped_slots);
        out["time_patch_errors"]      = nb::int_(exec.time_patch.errors);
        out["time_patch_message"]     = nb::str(exec.time_patch.message.c_str(),
                                                exec.time_patch.message.size());
        out["c2pa_stage_requested"]   = nb::bool_(exec.c2pa_stage_requested);
        out["c2pa_stage_status"]      = exec.c2pa_stage.status;
        out["c2pa_stage_status_name"] = nb::str(
            transfer_status_name(exec.c2pa_stage.status));
        out["c2pa_stage_code"]      = exec.c2pa_stage.code;
        out["c2pa_stage_code_name"] = nb::str(
            emit_transfer_code_name(exec.c2pa_stage.code));
        out["c2pa_stage_emitted"] = nb::int_(exec.c2pa_stage.emitted);
        out["c2pa_stage_skipped"] = nb::int_(exec.c2pa_stage.skipped);
        out["c2pa_stage_errors"]  = nb::int_(exec.c2pa_stage.errors);
        out["c2pa_stage_message"] = nb::str(exec.c2pa_stage.message.c_str(),
                                            exec.c2pa_stage.message.size());
        out["c2pa_stage_validation_status"] = exec.c2pa_stage_validation.status;
        out["c2pa_stage_validation_status_name"] = nb::str(
            transfer_status_name(exec.c2pa_stage_validation.status));
        out["c2pa_stage_validation_code"] = exec.c2pa_stage_validation.code;
        out["c2pa_stage_validation_code_name"] = nb::str(
            emit_transfer_code_name(exec.c2pa_stage_validation.code));
        out["c2pa_stage_validation_message"]
            = nb::str(exec.c2pa_stage_validation.message.c_str(),
                      exec.c2pa_stage_validation.message.size());

        nb::list blocks;
        for (size_t i = 0; i < prepared.bundle.blocks.size(); ++i) {
            const PreparedTransferBlock& block = prepared.bundle.blocks[i];
            nb::dict one;
            one["index"]     = nb::int_(static_cast<uint32_t>(i));
            one["kind"]      = block.kind;
            one["kind_name"] = nb::str(transfer_block_kind_name(block.kind));
            one["order"]     = nb::int_(block.order);
            one["route"]     = nb::str(block.route.c_str(), block.route.size());
            one["size"] = nb::int_(static_cast<uint64_t>(block.payload.size()));
            one["payload"] = nb::none();
            blocks.append(std::move(one));
        }
        out["blocks"] = std::move(blocks);

        nb::list policy_decisions;
        for (size_t i = 0; i < prepared.bundle.policy_decisions.size(); ++i) {
            const PreparedTransferPolicyDecision& decision
                = prepared.bundle.policy_decisions[i];
            nb::dict one;
            one["subject"]      = decision.subject;
            one["subject_name"] = nb::str(
                transfer_policy_subject_name(decision.subject));
            one["requested"]      = decision.requested;
            one["requested_name"] = nb::str(
                transfer_policy_action_name(decision.requested));
            one["effective"]      = decision.effective;
            one["effective_name"] = nb::str(
                transfer_policy_action_name(decision.effective));
            one["reason"]      = decision.reason;
            one["reason_name"] = nb::str(
                transfer_policy_reason_name(decision.reason));
            one["c2pa_mode"]      = decision.c2pa_mode;
            one["c2pa_mode_name"] = nb::str(
                transfer_c2pa_mode_name(decision.c2pa_mode));
            one["c2pa_source_kind"]      = decision.c2pa_source_kind;
            one["c2pa_source_kind_name"] = nb::str(
                transfer_c2pa_source_kind_name(decision.c2pa_source_kind));
            one["c2pa_prepared_output"]      = decision.c2pa_prepared_output;
            one["c2pa_prepared_output_name"] = nb::str(
                transfer_c2pa_prepared_output_name(
                    decision.c2pa_prepared_output));
            one["matched_entries"] = nb::int_(decision.matched_entries);
            one["message"]         = nb::str(decision.message.c_str(),
                                             decision.message.size());
            policy_decisions.append(std::move(one));
        }
        out["policy_decisions"] = std::move(policy_decisions);

        const PreparedTransferC2paRewriteRequirements& rewrite
            = prepared.bundle.c2pa_rewrite;
        nb::dict rewrite_dict;
        rewrite_dict["state"]      = rewrite.state;
        rewrite_dict["state_name"] = nb::str(
            transfer_c2pa_rewrite_state_name(rewrite.state));
        rewrite_dict["target_format"]      = rewrite.target_format;
        rewrite_dict["target_format_name"] = nb::str(
            transfer_target_format_name(rewrite.target_format));
        rewrite_dict["source_kind"]      = rewrite.source_kind;
        rewrite_dict["source_kind_name"] = nb::str(
            transfer_c2pa_source_kind_name(rewrite.source_kind));
        rewrite_dict["matched_entries"] = nb::int_(rewrite.matched_entries);
        rewrite_dict["message"]         = nb::str(rewrite.message.c_str(),
                                                  rewrite.message.size());
        out["c2pa_rewrite"]             = std::move(rewrite_dict);

        out["compile_status"]      = exec.compile.status;
        out["compile_status_name"] = nb::str(
            transfer_status_name(exec.compile.status));
        out["compile_code"]      = exec.compile.code;
        out["compile_code_name"] = nb::str(
            emit_transfer_code_name(exec.compile.code));
        out["compile_emitted"] = nb::int_(exec.compile.emitted);
        out["compile_skipped"] = nb::int_(exec.compile.skipped);
        out["compile_errors"]  = nb::int_(exec.compile.errors);
        out["compile_message"] = nb::str(exec.compile.message.c_str(),
                                         exec.compile.message.size());
        out["compiled_ops"]    = nb::int_(exec.compiled_ops);

        out["emit_status"]      = exec.emit.status;
        out["emit_status_name"] = nb::str(
            transfer_status_name(exec.emit.status));
        out["emit_code"]      = exec.emit.code;
        out["emit_code_name"] = nb::str(
            emit_transfer_code_name(exec.emit.code));
        out["emit_emitted"]     = nb::int_(exec.emit.emitted);
        out["emit_skipped"]     = nb::int_(exec.emit.skipped);
        out["emit_errors"]      = nb::int_(exec.emit.errors);
        out["emit_message"]     = nb::str(exec.emit.message.c_str(),
                                          exec.emit.message.size());
        out["emit_output_size"] = nb::int_(exec.emit_output_size);

        nb::list marker_summary;
        for (size_t i = 0; i < exec.marker_summary.size(); ++i) {
            nb::dict one;
            one["marker"] = nb::int_(exec.marker_summary[i].marker);
            one["count"]  = nb::int_(exec.marker_summary[i].count);
            one["bytes"]  = nb::int_(exec.marker_summary[i].bytes);
            marker_summary.append(std::move(one));
        }
        out["marker_summary"] = std::move(marker_summary);

        nb::list tiff_tag_summary;
        for (size_t i = 0; i < exec.tiff_tag_summary.size(); ++i) {
            nb::dict one;
            one["tag"]   = nb::int_(exec.tiff_tag_summary[i].tag);
            one["count"] = nb::int_(exec.tiff_tag_summary[i].count);
            one["bytes"] = nb::int_(exec.tiff_tag_summary[i].bytes);
            tiff_tag_summary.append(std::move(one));
        }
        out["tiff_tag_summary"] = std::move(tiff_tag_summary);

        nb::list jxl_box_summary;
        for (size_t i = 0; i < exec.jxl_box_summary.size(); ++i) {
            nb::dict one;
            one["type"]  = nb::str(exec.jxl_box_summary[i].type.data(),
                                   exec.jxl_box_summary[i].type.size());
            one["count"] = nb::int_(exec.jxl_box_summary[i].count);
            one["bytes"] = nb::int_(exec.jxl_box_summary[i].bytes);
            jxl_box_summary.append(std::move(one));
        }
        out["jxl_box_summary"] = std::move(jxl_box_summary);

        nb::list webp_chunk_summary;
        for (size_t i = 0; i < exec.webp_chunk_summary.size(); ++i) {
            nb::dict one;
            one["type"]  = nb::str(exec.webp_chunk_summary[i].type.data(),
                                   exec.webp_chunk_summary[i].type.size());
            one["count"] = nb::int_(exec.webp_chunk_summary[i].count);
            one["bytes"] = nb::int_(exec.webp_chunk_summary[i].bytes);
            webp_chunk_summary.append(std::move(one));
        }
        out["webp_chunk_summary"] = std::move(webp_chunk_summary);

        nb::list png_chunk_summary;
        for (size_t i = 0; i < exec.png_chunk_summary.size(); ++i) {
            nb::dict one;
            one["type"]  = nb::str(exec.png_chunk_summary[i].type.data(),
                                   exec.png_chunk_summary[i].type.size());
            one["count"] = nb::int_(exec.png_chunk_summary[i].count);
            one["bytes"] = nb::int_(exec.png_chunk_summary[i].bytes);
            png_chunk_summary.append(std::move(one));
        }
        out["png_chunk_summary"] = std::move(png_chunk_summary);

        nb::list jp2_box_summary;
        for (size_t i = 0; i < exec.jp2_box_summary.size(); ++i) {
            nb::dict one;
            one["type"]  = nb::str(exec.jp2_box_summary[i].type.data(),
                                   exec.jp2_box_summary[i].type.size());
            one["count"] = nb::int_(exec.jp2_box_summary[i].count);
            one["bytes"] = nb::int_(exec.jp2_box_summary[i].bytes);
            jp2_box_summary.append(std::move(one));
        }
        out["jp2_box_summary"] = std::move(jp2_box_summary);

        nb::list exr_attribute_summary;
        for (size_t i = 0; i < exec.exr_attribute_summary.size(); ++i) {
            nb::dict one;
            one["name"] = nb::str(exec.exr_attribute_summary[i].name.c_str(),
                                  exec.exr_attribute_summary[i].name.size());
            one["type_name"]
                = nb::str(exec.exr_attribute_summary[i].type_name.c_str(),
                          exec.exr_attribute_summary[i].type_name.size());
            one["count"] = nb::int_(exec.exr_attribute_summary[i].count);
            one["bytes"] = nb::int_(exec.exr_attribute_summary[i].bytes);
            exr_attribute_summary.append(std::move(one));
        }
        out["exr_attribute_summary"] = std::move(exr_attribute_summary);

        nb::list bmff_item_summary;
        for (size_t i = 0; i < exec.bmff_item_summary.size(); ++i) {
            nb::dict one;
            one["item_type"] = nb::int_(exec.bmff_item_summary[i].item_type);
            one["count"]     = nb::int_(exec.bmff_item_summary[i].count);
            one["bytes"]     = nb::int_(exec.bmff_item_summary[i].bytes);
            one["mime_xmp"]  = nb::bool_(exec.bmff_item_summary[i].mime_xmp);
            bmff_item_summary.append(std::move(one));
        }
        out["bmff_item_summary"] = std::move(bmff_item_summary);

        nb::list bmff_property_summary;
        for (size_t i = 0; i < exec.bmff_property_summary.size(); ++i) {
            nb::dict one;
            one["property_type"] = nb::int_(
                exec.bmff_property_summary[i].property_type);
            one["property_subtype"] = nb::int_(
                exec.bmff_property_summary[i].property_subtype);
            one["count"] = nb::int_(exec.bmff_property_summary[i].count);
            one["bytes"] = nb::int_(exec.bmff_property_summary[i].bytes);
            bmff_property_summary.append(std::move(one));
        }
        out["bmff_property_summary"] = std::move(bmff_property_summary);

        out["edit_requested"]        = nb::bool_(exec.edit_requested);
        out["edit_plan_status"]      = exec.edit_plan_status;
        out["edit_plan_status_name"] = nb::str(
            transfer_status_name(exec.edit_plan_status));
        out["edit_plan_message"]      = nb::str(exec.edit_plan_message.c_str(),
                                                exec.edit_plan_message.size());
        out["edit_apply_status"]      = exec.edit_apply.status;
        out["edit_apply_status_name"] = nb::str(
            transfer_status_name(exec.edit_apply.status));
        out["edit_apply_code"]      = exec.edit_apply.code;
        out["edit_apply_code_name"] = nb::str(
            emit_transfer_code_name(exec.edit_apply.code));
        out["edit_apply_emitted"] = nb::int_(exec.edit_apply.emitted);
        out["edit_apply_skipped"] = nb::int_(exec.edit_apply.skipped);
        out["edit_apply_errors"]  = nb::int_(exec.edit_apply.errors);
        out["edit_apply_message"] = nb::str(exec.edit_apply.message.c_str(),
                                            exec.edit_apply.message.size());
        out["edit_input_size"]    = nb::int_(exec.edit_input_size);
        out["edit_output_size"]   = nb::int_(exec.edit_output_size);

        if (include_edited_bytes && unsafe_edited_bytes_access
            && exec.edit_apply.status == TransferStatus::Ok) {
            out["edited_bytes"] = nb::bytes(reinterpret_cast<const char*>(
                                                exec.edited_output.data()),
                                            exec.edited_output.size());
        } else {
            out["edited_bytes"] = nb::none();
        }

        TransferStatus overall_status = TransferStatus::Ok;
        std::string error_stage       = "none";
        std::string error_code        = "none";
        std::string error_message;

        if (include_edited_bytes && !unsafe_edited_bytes_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_edited_bytes_forbidden";
            error_message  = "safe transfer_snapshot_probe forbids edited "
                             "bytes; use unsafe_transfer_snapshot_probe("
                             "include_edited_bytes=True)";
        } else if (exec.c2pa_stage_requested
                   && exec.c2pa_stage.status != TransferStatus::Ok) {
            overall_status = exec.c2pa_stage.status;
            error_stage    = "c2pa_stage";
            error_code     = emit_transfer_code_name(exec.c2pa_stage.code);
            error_message  = exec.c2pa_stage.message;
        } else if (exec.time_patch.status != TransferStatus::Ok) {
            overall_status = exec.time_patch.status;
            error_stage    = "time_patch";
            error_code     = "apply_time_patches_failed";
            error_message  = exec.time_patch.message;
        } else if (prepared.file_status != TransferFileStatus::Ok) {
            overall_status = transfer_status_from_file_status(
                prepared.file_status);
            error_stage   = "file";
            error_code    = prepare_transfer_file_code_name(prepared.code);
            error_message = prepared.prepare.message;
        } else if (prepared.prepare.status != TransferStatus::Ok
                   && (!exec.c2pa_stage_requested
                       || exec.c2pa_stage.status != TransferStatus::Ok)) {
            overall_status = prepared.prepare.status;
            error_stage    = "prepare";
            error_code     = prepare_transfer_code_name(prepared.prepare.code);
            error_message  = prepared.prepare.message;
        } else if (exec.compile.status != TransferStatus::Ok) {
            overall_status = exec.compile.status;
            error_stage    = "compile";
            error_code     = emit_transfer_code_name(exec.compile.code);
            error_message  = exec.compile.message;
        } else if (exec.edit_requested
                   && exec.edit_plan_status != TransferStatus::Ok) {
            overall_status = exec.edit_plan_status;
            error_stage    = "edit_plan";
            error_code     = "edit_plan_failed";
            error_message  = exec.edit_plan_message;
        } else if (exec.edit_requested && edit_do_apply
                   && exec.edit_apply.status != TransferStatus::Ok) {
            overall_status = exec.edit_apply.status;
            error_stage    = "edit_apply";
            error_code     = emit_transfer_code_name(exec.edit_apply.code);
            error_message  = exec.edit_apply.message;
        } else if (exec.emit.status != TransferStatus::Ok) {
            overall_status = exec.emit.status;
            error_stage    = "emit";
            error_code     = emit_transfer_code_name(exec.emit.code);
            error_message  = exec.emit.message;
        } else if (persist_requested
                   && persisted.output_status != TransferStatus::Ok) {
            overall_status = persisted.output_status;
            error_stage    = "persist_output";
            error_code     = "persist_output_failed";
            error_message  = persisted.output_message;
        } else if (persist_requested && result.xmp_sidecar_requested
                   && persisted.xmp_sidecar_status != TransferStatus::Ok) {
            overall_status = persisted.xmp_sidecar_status;
            error_stage    = "persist_xmp_sidecar";
            error_code     = "persist_xmp_sidecar_failed";
            error_message  = persisted.xmp_sidecar_message;
        } else if (persist_requested && result.xmp_sidecar_cleanup_requested
                   && persisted.xmp_sidecar_cleanup_status
                          != TransferStatus::Ok) {
            overall_status = persisted.xmp_sidecar_cleanup_status;
            error_stage    = "persist_xmp_sidecar_cleanup";
            error_code     = "persist_xmp_sidecar_cleanup_failed";
            error_message  = persisted.xmp_sidecar_cleanup_message;
        }

        out["overall_status"]      = overall_status;
        out["overall_status_name"] = nb::str(
            transfer_status_name(overall_status));
        out["error_stage"]   = nb::str(error_stage.c_str(), error_stage.size());
        out["error_code"]    = nb::str(error_code.c_str(), error_code.size());
        out["error_message"] = nb::str(error_message.c_str(),
                                       error_message.size());
        return out;
    }

    static nb::dict build_exr_attribute_batch_from_file_to_python(
        const std::string& path, XmpSidecarFormat format,
        bool include_pointer_tags, bool decode_makernote,
        bool decode_embedded_containers, bool decompress,
        bool include_exif_app1, bool include_xmp_app1, bool include_icc_app2,
        bool include_iptc_app13, bool xmp_include_existing,
        XmpExistingNamespacePolicy xmp_existing_namespace_policy,
        XmpExistingStandardNamespacePolicy xmp_existing_standard_namespace_policy,
        bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
        bool xmp_project_iptc, TransferPolicyAction makernote_policy,
        TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
        uint64_t max_file_bytes, nb::object policy_obj, bool include_values)
    {
        BuildExrAttributeBatchFileOptions options;
        options.prepare.include_pointer_tags       = include_pointer_tags;
        options.prepare.decode_makernote           = decode_makernote;
        options.prepare.decode_embedded_containers = decode_embedded_containers;
        options.prepare.decompress                 = decompress;
        options.prepare.prepare.xmp_portable       = (format
                                                == XmpSidecarFormat::Portable);
        options.prepare.prepare.include_exif_app1  = include_exif_app1;
        options.prepare.prepare.include_xmp_app1   = include_xmp_app1;
        options.prepare.prepare.include_icc_app2   = include_icc_app2;
        options.prepare.prepare.include_iptc_app13 = include_iptc_app13;
        options.prepare.prepare.xmp_include_existing = xmp_include_existing;
        options.prepare.prepare.xmp_existing_namespace_policy
            = xmp_existing_namespace_policy;
        options.prepare.prepare.xmp_existing_standard_namespace_policy
            = xmp_existing_standard_namespace_policy;
        options.prepare.prepare.xmp_exiftool_gpsdatetime_alias
            = xmp_exiftool_gpsdatetime_alias;
        options.prepare.prepare.xmp_project_exif  = xmp_project_exif;
        options.prepare.prepare.xmp_project_iptc  = xmp_project_iptc;
        options.prepare.prepare.profile.makernote = makernote_policy;
        options.prepare.prepare.profile.jumbf     = jumbf_policy;
        options.prepare.prepare.profile.c2pa      = c2pa_policy;
        options.prepare.policy.max_file_bytes     = max_file_bytes;
        if (!policy_obj.is_none()) {
            options.prepare.policy = nb::cast<OpenMetaResourcePolicy>(
                policy_obj);
            if (max_file_bytes != 0U) {
                options.prepare.policy.max_file_bytes = max_file_bytes;
            }
        }

        ExrAdapterBatch batch;
        BuildExrAttributeBatchFileResult result;
        {
            nb::gil_scoped_release gil_release;
            result = build_exr_attribute_batch_from_file(path.c_str(), &batch,
                                                         options);
        }

        nb::dict out;
        out["path"]             = nb::str(path.c_str(), path.size());
        out["file_status"]      = result.prepared.file_status;
        out["file_status_name"] = nb::str(
            transfer_file_status_name(result.prepared.file_status));
        out["file_code"]      = result.prepared.code;
        out["file_code_name"] = nb::str(
            prepare_transfer_file_code_name(result.prepared.code));
        out["file_size"]           = nb::int_(result.prepared.file_size);
        out["entry_count"]         = nb::int_(result.prepared.entry_count);
        out["prepare_status"]      = result.prepared.prepare.status;
        out["prepare_status_name"] = nb::str(
            transfer_status_name(result.prepared.prepare.status));
        out["prepare_code"]      = result.prepared.prepare.code;
        out["prepare_code_name"] = nb::str(
            prepare_transfer_code_name(result.prepared.prepare.code));
        out["prepare_warnings"] = nb::int_(result.prepared.prepare.warnings);
        out["prepare_errors"]   = nb::int_(result.prepared.prepare.errors);
        out["prepare_message"]
            = nb::str(result.prepared.prepare.message.c_str(),
                      result.prepared.prepare.message.size());
        out["exr_attribute_batch_status"]      = result.adapter.status;
        out["exr_attribute_batch_status_name"] = nb::str(
            exr_adapter_status_name(result.adapter.status));
        out["exr_attribute_batch_exported"] = nb::int_(result.adapter.exported);
        out["exr_attribute_batch_skipped"]  = nb::int_(result.adapter.skipped);
        out["exr_attribute_batch_errors"]   = nb::int_(result.adapter.errors);
        out["exr_attribute_batch_message"]
            = nb::str(result.adapter.message.c_str(),
                      result.adapter.message.size());
        out["exr_attribute_values_requested"] = nb::bool_(include_values);

        TransferStatus values_status = TransferStatus::Ok;
        std::string values_message;
        if (include_values && result.adapter.status == ExrAdapterStatus::Ok) {
            values_status = TransferStatus::Ok;
        } else if (result.adapter.status == ExrAdapterStatus::InvalidArgument) {
            values_status  = TransferStatus::InvalidArgument;
            values_message = result.adapter.message;
        } else if (result.adapter.status == ExrAdapterStatus::Unsupported) {
            values_status  = TransferStatus::Unsupported;
            values_message = result.adapter.message;
        }
        out["exr_attribute_values_status"]      = values_status;
        out["exr_attribute_values_status_name"] = nb::str(
            transfer_status_name(values_status));
        out["exr_attribute_values_message"] = nb::str(values_message.c_str(),
                                                      values_message.size());

        nb::list exr_attribute_batch;
        if (result.adapter.status == ExrAdapterStatus::Ok) {
            for (size_t i = 0; i < batch.attributes.size(); ++i) {
                const ExrAdapterAttribute& attr = batch.attributes[i];
                nb::dict one;
                one["part_index"] = nb::int_(attr.part_index);
                one["name"]      = nb::str(attr.name.c_str(), attr.name.size());
                one["type_name"] = nb::str(attr.type_name.c_str(),
                                           attr.type_name.size());
                one["is_opaque"] = nb::bool_(attr.is_opaque);
                one["bytes"]     = nb::int_(
                    static_cast<uint64_t>(attr.value.size()));
                if (include_values) {
                    one["value"] = nb::bytes(reinterpret_cast<const char*>(
                                                 attr.value.data()),
                                             attr.value.size());
                } else {
                    one["value"] = nb::none();
                }
                exr_attribute_batch.append(std::move(one));
            }
            out["exr_attribute_batch"] = std::move(exr_attribute_batch);
        } else {
            out["exr_attribute_batch"] = nb::none();
        }

        TransferStatus overall_status = TransferStatus::Ok;
        std::string error_stage       = "none";
        std::string error_code        = "none";
        std::string error_message;
        if (result.prepared.file_status != TransferFileStatus::Ok) {
            overall_status = transfer_status_from_file_status(
                result.prepared.file_status);
            error_stage = "file";
            error_code  = prepare_transfer_file_code_name(result.prepared.code);
            error_message = result.prepared.prepare.message;
        } else if (result.prepared.prepare.status != TransferStatus::Ok) {
            overall_status = result.prepared.prepare.status;
            error_stage    = "prepare";
            error_code     = prepare_transfer_code_name(
                result.prepared.prepare.code);
            error_message = result.prepared.prepare.message;
        } else if (result.adapter.status != ExrAdapterStatus::Ok) {
            if (result.adapter.status == ExrAdapterStatus::InvalidArgument) {
                overall_status = TransferStatus::InvalidArgument;
            } else {
                overall_status = TransferStatus::Unsupported;
            }
            error_stage   = "exr_attribute_batch";
            error_code    = exr_adapter_status_name(result.adapter.status);
            error_message = result.adapter.message;
        }
        out["overall_status"]      = overall_status;
        out["overall_status_name"] = nb::str(
            transfer_status_name(overall_status));
        out["error_stage"]   = nb::str(error_stage.c_str(), error_stage.size());
        out["error_code"]    = nb::str(error_code.c_str(), error_code.size());
        out["error_message"] = nb::str(error_message.c_str(),
                                       error_message.size());
        return out;
    }

    static nb::dict map_libraw_orientation_from_file_to_python(
        const std::string& path, LibRawOrientationTarget target,
        bool preserve_embedded_preview_orientation,
        LibRawMirrorPolicy mirror_policy, uint64_t max_file_bytes)
    {
        LibRawOrientationFileOptions options;
        options.max_file_bytes     = max_file_bytes;
        options.orientation.target = target;
        options.orientation.preserve_embedded_preview_orientation
            = preserve_embedded_preview_orientation;
        options.orientation.mirror_policy = mirror_policy;

        LibRawOrientationFileResult result;
        {
            nb::gil_scoped_release gil_release;
            result = map_meta_orientation_to_libraw_flip_from_file(path.c_str(),
                                                                   options);
        }

        nb::dict out;
        out["path"]             = nb::str(path.c_str(), path.size());
        out["file_status"]      = result.file_status;
        out["file_status_name"] = nb::str(
            libraw_orientation_file_status_name(result.file_status));
        out["mapped_file_status"] = nb::int_(
            static_cast<uint32_t>(result.mapped_file_status));
        out["mapped_file_status_name"] = nb::str(
            mapped_file_status_name(result.mapped_file_status));
        out["file_size"]        = nb::int_(result.file_size);
        out["scan_status"]      = result.read.scan.status;
        out["scan_status_name"] = nb::str(
            scan_status_name(result.read.scan.status));
        out["payload_status"]      = result.read.payload.status;
        out["payload_status_name"] = nb::str(
            payload_status_name(result.read.payload.status));
        out["exif_status"]      = result.read.exif.status;
        out["exif_status_name"] = nb::str(
            exif_decode_status_name(result.read.exif.status));
        out["xmp_status"]      = result.read.xmp.status;
        out["xmp_status_name"] = nb::str(
            xmp_decode_status_name(result.read.xmp.status));
        out["orientation_status"]      = result.orientation.status;
        out["orientation_status_name"] = nb::str(
            libraw_orientation_status_name(result.orientation.status));
        out["orientation_code"]      = result.orientation.code;
        out["orientation_code_name"] = nb::str(
            libraw_orientation_code_name(result.orientation.code));
        out["orientation_source"]      = result.orientation.source;
        out["orientation_source_name"] = nb::str(
            libraw_orientation_source_name(result.orientation.source));
        out["exif_orientation"] = nb::int_(result.orientation.exif_orientation);
        out["libraw_flip"]      = nb::int_(result.orientation.libraw_flip);
        out["apply_flip"]       = nb::bool_(result.orientation.apply_flip);
        out["mirrored"]         = nb::bool_(result.orientation.mirrored);
        out["preview_passthrough"] = nb::bool_(
            result.orientation.preview_passthrough);
        out["has_exif_ifd0_orientation"] = nb::bool_(
            result.orientation.has_exif_ifd0_orientation);
        out["has_xmp_tiff_orientation"] = nb::bool_(
            result.orientation.has_xmp_tiff_orientation);
        out["exif_ifd0_orientation"] = nb::int_(
            result.orientation.exif_ifd0_orientation);
        out["xmp_tiff_orientation"] = nb::int_(
            result.orientation.xmp_tiff_orientation);
        out["orientation_conflict"] = nb::bool_(
            result.orientation.orientation_conflict);

        std::string overall = "ok";
        if (result.file_status != LibRawOrientationFileStatus::Ok) {
            overall = "file_error";
        } else if (result.orientation.status != LibRawOrientationStatus::Ok) {
            overall = "orientation_error";
        }
        out["overall_status_name"] = nb::str(overall.c_str(), overall.size());
        return out;
    }

    static nb::dict interpret_exif_orientation_to_python(uint16_t orientation)
    {
        const ExifOrientationInterpretation result = interpret_exif_orientation(
            orientation);

        nb::dict out;
        out["status"]      = result.status;
        out["status_name"] = nb::str(
            exif_orientation_status_name(result.status));
        out["orientation"]               = nb::int_(result.orientation);
        out["rotation_degrees_cw"]       = nb::int_(result.rotation_degrees_cw);
        out["rotation_only_orientation"] = nb::int_(
            result.rotation_only_orientation);
        out["mirrored"]           = nb::bool_(result.mirrored);
        out["swaps_width_height"] = nb::bool_(result.swaps_width_height);
        out["name"]               = nb::str(result.name);
        return out;
    }

    static nb::dict
    exif_orientation_rotation_degrees_to_python(uint16_t orientation)
    {
        bool valid = false;
        const uint16_t result
            = exif_orientation_rotation_degrees_cw(orientation, &valid);

        nb::dict out;
        out["valid"]               = nb::bool_(valid);
        out["rotation_degrees_cw"] = nb::int_(result);
        return out;
    }

    static nb::dict map_libraw_flip_to_exif_to_python(
        uint32_t libraw_flip, LibRawOrientationTarget target,
        bool preserve_embedded_preview_orientation)
    {
        LibRawFlipToExifOptions options;
        options.target = target;
        options.preserve_embedded_preview_orientation
            = preserve_embedded_preview_orientation;

        const LibRawFlipToExifResult result
            = map_libraw_flip_to_exif_orientation(libraw_flip, options);

        nb::dict out;
        out["libraw_flip"]             = nb::int_(result.libraw_flip);
        out["orientation_status"]      = result.status;
        out["orientation_status_name"] = nb::str(
            libraw_orientation_status_name(result.status));
        out["orientation_code"]      = result.code;
        out["orientation_code_name"] = nb::str(
            libraw_flip_to_exif_code_name(result.code));
        out["exif_orientation"]    = nb::int_(result.exif_orientation);
        out["preview_passthrough"] = nb::bool_(result.preview_passthrough);
        return out;
    }

    static nb::dict update_dng_sdk_file_from_file_to_python(
        const std::string& source_path, const std::string& target_path,
        DngTargetMode dng_target_mode, XmpSidecarFormat format,
        bool include_pointer_tags, bool decode_makernote,
        bool decode_embedded_containers, bool decompress,
        bool include_exif_app1, bool include_xmp_app1, bool include_icc_app2,
        bool include_iptc_app13, bool xmp_include_existing,
        XmpExistingNamespacePolicy xmp_existing_namespace_policy,
        XmpExistingStandardNamespacePolicy xmp_existing_standard_namespace_policy,
        bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
        bool xmp_project_iptc, TransferPolicyAction makernote_policy,
        TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
        uint64_t max_file_bytes, nb::object policy_obj, bool apply_exif,
        bool apply_xmp, bool apply_iptc, bool synchronize_metadata,
        bool cleanup_for_update)
    {
        ApplyDngSdkMetadataFileOptions options;
        options.prepare.include_pointer_tags       = include_pointer_tags;
        options.prepare.decode_makernote           = decode_makernote;
        options.prepare.decode_embedded_containers = decode_embedded_containers;
        options.prepare.decompress                 = decompress;
        options.prepare.prepare.target_format      = TransferTargetFormat::Dng;
        options.prepare.prepare.dng_target_mode    = dng_target_mode;
        options.prepare.prepare.xmp_portable       = (format
                                                == XmpSidecarFormat::Portable);
        options.prepare.prepare.include_exif_app1  = include_exif_app1;
        options.prepare.prepare.include_xmp_app1   = include_xmp_app1;
        options.prepare.prepare.include_icc_app2   = include_icc_app2;
        options.prepare.prepare.include_iptc_app13 = include_iptc_app13;
        options.prepare.prepare.xmp_include_existing = xmp_include_existing;
        options.prepare.prepare.xmp_existing_namespace_policy
            = xmp_existing_namespace_policy;
        options.prepare.prepare.xmp_existing_standard_namespace_policy
            = xmp_existing_standard_namespace_policy;
        options.prepare.prepare.xmp_exiftool_gpsdatetime_alias
            = xmp_exiftool_gpsdatetime_alias;
        options.prepare.prepare.xmp_project_exif  = xmp_project_exif;
        options.prepare.prepare.xmp_project_iptc  = xmp_project_iptc;
        options.prepare.prepare.profile.makernote = makernote_policy;
        options.prepare.prepare.profile.jumbf     = jumbf_policy;
        options.prepare.prepare.profile.c2pa      = c2pa_policy;
        options.prepare.policy.max_file_bytes     = max_file_bytes;
        if (!policy_obj.is_none()) {
            options.prepare.policy = nb::cast<OpenMetaResourcePolicy>(
                policy_obj);
            if (max_file_bytes != 0U) {
                options.prepare.policy.max_file_bytes = max_file_bytes;
            }
        }
        options.adapter.apply_exif           = apply_exif;
        options.adapter.apply_xmp            = apply_xmp;
        options.adapter.apply_iptc           = apply_iptc;
        options.adapter.synchronize_metadata = synchronize_metadata;
        options.adapter.cleanup_for_update   = cleanup_for_update;

        ApplyDngSdkMetadataFileResult result;
        {
            nb::gil_scoped_release gil_release;
            result = update_dng_sdk_file_from_file(source_path.c_str(),
                                                   target_path.c_str(),
                                                   options);
        }

        nb::dict out;
        out["source_path"] = nb::str(source_path.c_str(), source_path.size());
        out["target_path"] = nb::str(target_path.c_str(), target_path.size());
        out["dng_sdk_adapter_available"] = nb::bool_(
            dng_sdk_adapter_available());
        out["file_status"]      = result.prepared.file_status;
        out["file_status_name"] = nb::str(
            transfer_file_status_name(result.prepared.file_status));
        out["file_code"]      = result.prepared.code;
        out["file_code_name"] = nb::str(
            prepare_transfer_file_code_name(result.prepared.code));
        out["file_size"]           = nb::int_(result.prepared.file_size);
        out["entry_count"]         = nb::int_(result.prepared.entry_count);
        out["prepare_status"]      = result.prepared.prepare.status;
        out["prepare_status_name"] = nb::str(
            transfer_status_name(result.prepared.prepare.status));
        out["prepare_code"]      = result.prepared.prepare.code;
        out["prepare_code_name"] = nb::str(
            prepare_transfer_code_name(result.prepared.prepare.code));
        out["prepare_warnings"] = nb::int_(result.prepared.prepare.warnings);
        out["prepare_errors"]   = nb::int_(result.prepared.prepare.errors);
        out["prepare_message"]
            = nb::str(result.prepared.prepare.message.c_str(),
                      result.prepared.prepare.message.size());
        out["adapter_status"]      = result.adapter.status;
        out["adapter_status_name"] = nb::str(
            dng_sdk_adapter_status_name(result.adapter.status));
        out["applied_blocks"]        = nb::int_(result.adapter.applied_blocks);
        out["skipped_blocks"]        = nb::int_(result.adapter.skipped_blocks);
        out["exif_applied"]          = nb::bool_(result.adapter.exif_applied);
        out["xmp_applied"]           = nb::bool_(result.adapter.xmp_applied);
        out["iptc_applied"]          = nb::bool_(result.adapter.iptc_applied);
        out["synchronized_metadata"] = nb::bool_(
            result.adapter.synchronized_metadata);
        out["cleaned_for_update"] = nb::bool_(
            result.adapter.cleaned_for_update);
        out["updated_stream"]   = nb::bool_(result.adapter.updated_stream);
        out["failed_kind"]      = result.adapter.failed_kind;
        out["failed_kind_name"] = nb::str(
            transfer_block_kind_name(result.adapter.failed_kind));
        out["adapter_message"] = nb::str(result.adapter.message.c_str(),
                                         result.adapter.message.size());

        TransferStatus overall_status = TransferStatus::Ok;
        std::string error_stage       = "none";
        std::string error_code        = "none";
        std::string error_message;
        if (result.prepared.file_status != TransferFileStatus::Ok) {
            overall_status = transfer_status_from_file_status(
                result.prepared.file_status);
            error_stage = "file";
            error_code  = prepare_transfer_file_code_name(result.prepared.code);
            error_message = result.prepared.prepare.message;
        } else if (result.prepared.prepare.status != TransferStatus::Ok) {
            overall_status = result.prepared.prepare.status;
            error_stage    = "prepare";
            error_code     = prepare_transfer_code_name(
                result.prepared.prepare.code);
            error_message = result.prepared.prepare.message;
        } else if (result.adapter.status != DngSdkAdapterStatus::Ok) {
            switch (result.adapter.status) {
            case DngSdkAdapterStatus::InvalidArgument:
                overall_status = TransferStatus::InvalidArgument;
                break;
            case DngSdkAdapterStatus::Unsupported:
                overall_status = TransferStatus::Unsupported;
                break;
            case DngSdkAdapterStatus::Malformed:
                overall_status = TransferStatus::Malformed;
                break;
            case DngSdkAdapterStatus::InternalError:
                overall_status = TransferStatus::InternalError;
                break;
            case DngSdkAdapterStatus::Ok: break;
            }
            error_stage   = "dng_sdk_adapter";
            error_code    = dng_sdk_adapter_status_name(result.adapter.status);
            error_message = result.adapter.message;
        }
        out["overall_status"]      = overall_status;
        out["overall_status_name"] = nb::str(
            transfer_status_name(overall_status));
        out["error_stage"]   = nb::str(error_stage.c_str(), error_stage.size());
        out["error_code"]    = nb::str(error_code.c_str(), error_code.size());
        out["error_message"] = nb::str(error_message.c_str(),
                                       error_message.size());
        return out;
    }

    static nb::dict transfer_probe_to_python(
        const std::string& path, TransferTargetFormat target_format,
        DngTargetMode dng_target_mode, XmpSidecarFormat format,
        bool include_pointer_tags, bool decode_makernote,
        bool decode_embedded_containers, bool decompress,
        bool include_exif_app1, bool include_xmp_app1, bool include_icc_app2,
        bool include_iptc_app13, bool xmp_include_existing,
        XmpExistingNamespacePolicy xmp_existing_namespace_policy,
        XmpExistingStandardNamespacePolicy xmp_existing_standard_namespace_policy,
        bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
        bool xmp_project_iptc, TransferPolicyAction makernote_policy,
        TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
        uint64_t max_file_bytes, nb::object policy_obj,
        nb::object c2pa_signed_package_obj,
        nb::object c2pa_signed_logical_payload_obj,
        nb::object c2pa_certificate_chain_obj,
        nb::object c2pa_private_key_reference_obj,
        nb::object c2pa_signing_time_obj,
        nb::object c2pa_manifest_builder_output_obj, bool include_payloads,
        bool unsafe_payload_access, nb::object time_patches_obj,
        bool time_patch_strict_width, bool time_patch_require_slot,
        bool time_patch_auto_nul, nb::object edit_target_path_obj,
        nb::object xmp_existing_sidecar_base_path_obj,
        nb::object xmp_sidecar_base_path_obj,
        XmpExistingSidecarMode xmp_existing_sidecar_mode,
        XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
        nb::object xmp_existing_destination_embedded_path_obj,
        XmpExistingDestinationEmbeddedMode xmp_existing_destination_embedded_mode,
        XmpExistingDestinationEmbeddedPrecedence
            xmp_existing_destination_embedded_precedence,
        XmpExistingDestinationCarrierPrecedence
            xmp_existing_destination_carrier_precedence,
        XmpExistingDestinationSidecarState xmp_existing_destination_sidecar_state,
        bool edit_do_apply, bool include_edited_bytes,
        bool unsafe_edited_bytes_access, bool include_c2pa_binding_bytes,
        bool unsafe_c2pa_binding_access, bool include_c2pa_handoff_bytes,
        bool include_c2pa_signed_package_bytes,
        bool include_jxl_encoder_handoff_bytes,
        bool include_exr_attribute_values,
        bool include_transfer_payload_batch_bytes,
        bool include_transfer_package_batch_bytes,
        bool unsafe_c2pa_package_access, XmpConflictPolicy xmp_conflict_policy,
        XmpWritebackMode xmp_writeback_mode,
        XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
        XmpDestinationSidecarMode xmp_destination_sidecar_mode,
        nb::object persist_output_path_obj, bool persist_overwrite_output,
        bool persist_overwrite_xmp_sidecar,
        bool persist_remove_destination_xmp_sidecar,
        nb::object target_image_spec_obj,
        nb::object source_raw_data_descriptor_obj,
        TransferSafetyMode transfer_safety)
    {
        PrepareTransferFileOptions prepare_options;
        prepare_options.include_pointer_tags       = include_pointer_tags;
        prepare_options.decode_makernote           = decode_makernote;
        prepare_options.decode_embedded_containers = decode_embedded_containers;
        prepare_options.decompress                 = decompress;
        prepare_options.prepare.target_format      = target_format;
        prepare_options.prepare.dng_target_mode    = dng_target_mode;
        prepare_options.prepare.xmp_portable       = (format
                                                == XmpSidecarFormat::Portable);
        prepare_options.prepare.include_exif_app1  = include_exif_app1;
        prepare_options.prepare.include_xmp_app1   = include_xmp_app1;
        prepare_options.prepare.include_icc_app2   = include_icc_app2;
        prepare_options.prepare.include_iptc_app13 = include_iptc_app13;
        prepare_options.prepare.xmp_project_exif   = xmp_project_exif;
        prepare_options.prepare.xmp_project_iptc   = xmp_project_iptc;
        prepare_options.prepare.xmp_include_existing = xmp_include_existing;
        prepare_options.prepare.xmp_existing_namespace_policy
            = xmp_existing_namespace_policy;
        prepare_options.prepare.xmp_existing_standard_namespace_policy
            = xmp_existing_standard_namespace_policy;
        prepare_options.prepare.xmp_conflict_policy = xmp_conflict_policy;
        prepare_options.prepare.xmp_exiftool_gpsdatetime_alias
            = xmp_exiftool_gpsdatetime_alias;
        prepare_options.prepare.target_image_spec
            = transfer_target_image_spec_from_python(target_image_spec_obj);
        if (!source_raw_data_descriptor_obj.is_none()) {
            prepare_options.prepare.has_source_raw_data_descriptor = true;
            prepare_options.prepare.source_raw_data_descriptor
                = nb::cast<MetadataRawDataDescriptor>(
                    source_raw_data_descriptor_obj);
        }
        prepare_options.prepare.profile.makernote = makernote_policy;
        prepare_options.prepare.profile.jumbf     = jumbf_policy;
        prepare_options.prepare.profile.c2pa      = c2pa_policy;
        prepare_options.prepare.profile.safety    = transfer_safety;
        prepare_options.policy.max_file_bytes     = max_file_bytes;
        prepare_options.xmp_existing_sidecar_mode = xmp_existing_sidecar_mode;
        prepare_options.xmp_existing_sidecar_precedence
            = xmp_existing_sidecar_precedence;
        prepare_options.xmp_existing_destination_carrier_precedence
            = xmp_existing_destination_carrier_precedence;
        if (!xmp_existing_destination_embedded_path_obj.is_none()) {
            prepare_options.xmp_existing_destination_embedded_path
                = nb::cast<std::string>(
                    xmp_existing_destination_embedded_path_obj);
        }

        if (!policy_obj.is_none()) {
            prepare_options.policy = nb::cast<OpenMetaResourcePolicy>(
                policy_obj);
            if (max_file_bytes != 0U) {
                prepare_options.policy.max_file_bytes = max_file_bytes;
            }
        }

        std::vector<TransferTimePatchInput> parsed_updates
            = parse_time_patches_object(time_patches_obj);

        ExecutePreparedTransferFileOptions file_options;
        file_options.prepare              = prepare_options;
        file_options.execute.time_patches = std::move(parsed_updates);
        file_options.execute.time_patch.strict_width = time_patch_strict_width;
        file_options.execute.time_patch.require_slot = time_patch_require_slot;
        file_options.execute.time_patch_auto_nul     = time_patch_auto_nul;
        file_options.execute.edit_apply              = edit_do_apply;
        file_options.execute.edit_requested          = false;
        file_options.xmp_existing_destination_embedded_mode
            = xmp_existing_destination_embedded_mode;
        file_options.xmp_existing_destination_embedded_precedence
            = xmp_existing_destination_embedded_precedence;
        file_options.xmp_writeback_mode = xmp_writeback_mode;
        file_options.xmp_destination_embedded_mode
            = xmp_destination_embedded_mode;
        file_options.xmp_destination_sidecar_mode = xmp_destination_sidecar_mode;
        file_options.xmp_existing_destination_sidecar_state
            = xmp_existing_destination_sidecar_state;

        if (!edit_target_path_obj.is_none()) {
            file_options.edit_target_path = nb::cast<std::string>(
                edit_target_path_obj);
            if (!file_options.edit_target_path.empty()) {
                file_options.execute.edit_requested = true;
            }
        }
        std::string sidecar_base_path;
        if (!xmp_sidecar_base_path_obj.is_none()) {
            sidecar_base_path = nb::cast<std::string>(
                xmp_sidecar_base_path_obj);
            file_options.xmp_sidecar_base_path = sidecar_base_path;
        }
        if (!xmp_existing_sidecar_base_path_obj.is_none()) {
            prepare_options.xmp_existing_sidecar_base_path
                = nb::cast<std::string>(xmp_existing_sidecar_base_path_obj);
        } else if (!sidecar_base_path.empty()) {
            prepare_options.xmp_existing_sidecar_base_path = sidecar_base_path;
        }
        file_options.prepare = prepare_options;

        const bool have_c2pa_signed_package = !c2pa_signed_package_obj.is_none();
        const bool have_individual_c2pa_inputs
            = !c2pa_signed_logical_payload_obj.is_none()
              || !c2pa_certificate_chain_obj.is_none()
              || !c2pa_private_key_reference_obj.is_none()
              || !c2pa_signing_time_obj.is_none()
              || !c2pa_manifest_builder_output_obj.is_none();
        if (have_c2pa_signed_package && have_individual_c2pa_inputs) {
            throw std::runtime_error(
                "c2pa_signed_package is mutually exclusive with individual "
                "signed c2pa inputs");
        }

        if (have_c2pa_signed_package) {
            const std::vector<std::byte> package_bytes = bytes_object_to_vector(
                c2pa_signed_package_obj);
            PreparedTransferC2paSignedPackage package;
            const PreparedTransferC2paPackageIoResult package_io
                = deserialize_prepared_c2pa_signed_package(
                    std::span<const std::byte>(package_bytes.data(),
                                               package_bytes.size()),
                    &package);
            if (package_io.status != TransferStatus::Ok) {
                throw std::runtime_error(
                    package_io.message.empty()
                        ? "failed to deserialize c2pa signed package"
                        : package_io.message);
            }
            file_options.c2pa_signed_package_provided = true;
            file_options.c2pa_signed_package          = std::move(package);
            file_options.prepare.prepare.profile.c2pa
                = TransferPolicyAction::Rewrite;
        } else if (have_individual_c2pa_inputs) {
            file_options.c2pa_stage_requested = true;
            file_options.prepare.prepare.profile.c2pa
                = TransferPolicyAction::Rewrite;
            file_options.c2pa_signer_input.signed_c2pa_logical_payload
                = bytes_object_to_vector(c2pa_signed_logical_payload_obj);
            file_options.c2pa_signer_input.certificate_chain_bytes
                = bytes_object_to_vector(c2pa_certificate_chain_obj);
            if (!c2pa_private_key_reference_obj.is_none()) {
                file_options.c2pa_signer_input.private_key_reference
                    = nb::cast<std::string>(c2pa_private_key_reference_obj);
            }
            if (!c2pa_signing_time_obj.is_none()) {
                file_options.c2pa_signer_input.signing_time
                    = nb::cast<std::string>(c2pa_signing_time_obj);
            }
            file_options.c2pa_signer_input.manifest_builder_output
                = bytes_object_to_vector(c2pa_manifest_builder_output_obj);
        }

        const bool persist_requested = !persist_output_path_obj.is_none();
        std::string persist_output_path;
        if (persist_requested) {
            persist_output_path = nb::cast<std::string>(
                persist_output_path_obj);
        }

        ExecutePreparedTransferFileResult executed;
        PersistPreparedTransferFileResult persisted;
        {
            nb::gil_scoped_release gil_release;
            executed = execute_prepared_transfer_file(path.c_str(),
                                                      file_options);
            if (persist_requested) {
                PersistPreparedTransferFileOptions persist_options;
                persist_options.output_path      = persist_output_path;
                persist_options.overwrite_output = persist_overwrite_output;
                persist_options.overwrite_xmp_sidecar
                    = persist_overwrite_xmp_sidecar;
                persist_options.remove_destination_xmp_sidecar
                    = persist_remove_destination_xmp_sidecar;
                persisted
                    = persist_prepared_transfer_file_result(executed,
                                                            persist_options);
            }
        }
        const PrepareTransferFileResult& prepared = executed.prepared;
        const ExecutePreparedTransferResult& exec = executed.execute;

        nb::dict out;
        out["path"]             = nb::str(path.c_str(), path.size());
        out["file_status"]      = prepared.file_status;
        out["file_status_name"] = nb::str(
            transfer_file_status_name(prepared.file_status));
        out["file_code"]      = prepared.code;
        out["file_code_name"] = nb::str(
            prepare_transfer_file_code_name(prepared.code));
        out["file_size"]           = nb::int_(prepared.file_size);
        out["entry_count"]         = nb::int_(prepared.entry_count);
        out["scan_status"]         = prepared.read.scan.status;
        out["payload_status"]      = prepared.read.payload.status;
        out["exif_status"]         = prepared.read.exif.status;
        out["xmp_status"]          = prepared.read.xmp.status;
        out["exr_status"]          = prepared.read.exr.status;
        out["jumbf_status"]        = prepared.read.jumbf.status;
        out["prepare_status"]      = prepared.prepare.status;
        out["prepare_status_name"] = nb::str(
            transfer_status_name(prepared.prepare.status));
        out["prepare_code"]      = prepared.prepare.code;
        out["prepare_code_name"] = nb::str(
            prepare_transfer_code_name(prepared.prepare.code));
        out["prepare_warnings"] = nb::int_(prepared.prepare.warnings);
        out["prepare_errors"]   = nb::int_(prepared.prepare.errors);
        out["prepare_message"]  = nb::str(prepared.prepare.message.c_str(),
                                          prepared.prepare.message.size());
        out["xmp_existing_sidecar_loaded"] = nb::bool_(
            prepared.xmp_existing_sidecar_loaded);
        out["xmp_existing_sidecar_status"]
            = prepared.xmp_existing_sidecar_status;
        out["xmp_existing_sidecar_status_name"] = nb::str(
            transfer_status_name(prepared.xmp_existing_sidecar_status));
        out["xmp_existing_sidecar_message"]
            = nb::str(prepared.xmp_existing_sidecar_message.c_str(),
                      prepared.xmp_existing_sidecar_message.size());
        out["xmp_existing_sidecar_path"]
            = nb::str(prepared.xmp_existing_sidecar_path.c_str(),
                      prepared.xmp_existing_sidecar_path.size());
        out["xmp_existing_destination_embedded_loaded"] = nb::bool_(
            executed.xmp_existing_destination_embedded_loaded);
        out["xmp_existing_destination_embedded_status"]
            = executed.xmp_existing_destination_embedded_status;
        out["xmp_existing_destination_embedded_status_name"] = nb::str(
            transfer_status_name(
                executed.xmp_existing_destination_embedded_status));
        out["xmp_existing_destination_embedded_message"] = nb::str(
            executed.xmp_existing_destination_embedded_message.c_str(),
            executed.xmp_existing_destination_embedded_message.size());
        out["xmp_existing_destination_embedded_path"]
            = nb::str(executed.xmp_existing_destination_embedded_path.c_str(),
                      executed.xmp_existing_destination_embedded_path.size());
        out["xmp_sidecar_requested"] = nb::bool_(
            executed.xmp_sidecar_requested);
        out["xmp_sidecar_status"]      = executed.xmp_sidecar_status;
        out["xmp_sidecar_status_name"] = nb::str(
            transfer_status_name(executed.xmp_sidecar_status));
        out["xmp_sidecar_message"]
            = nb::str(executed.xmp_sidecar_message.c_str(),
                      executed.xmp_sidecar_message.size());
        out["xmp_sidecar_path"]  = nb::str(executed.xmp_sidecar_path.c_str(),
                                           executed.xmp_sidecar_path.size());
        out["xmp_sidecar_bytes"] = nb::int_(
            static_cast<uint64_t>(executed.xmp_sidecar_output.size()));
        out["xmp_sidecar_cleanup_requested"] = nb::bool_(
            executed.xmp_sidecar_cleanup_requested);
        out["xmp_sidecar_cleanup_status"] = executed.xmp_sidecar_cleanup_status;
        out["xmp_sidecar_cleanup_status_name"] = nb::str(
            transfer_status_name(executed.xmp_sidecar_cleanup_status));
        out["xmp_sidecar_cleanup_message"]
            = nb::str(executed.xmp_sidecar_cleanup_message.c_str(),
                      executed.xmp_sidecar_cleanup_message.size());
        out["xmp_sidecar_cleanup_path"]
            = nb::str(executed.xmp_sidecar_cleanup_path.c_str(),
                      executed.xmp_sidecar_cleanup_path.size());
        if (!executed.xmp_sidecar_output.empty()) {
            out["xmp_sidecar_output"]
                = nb::bytes(reinterpret_cast<const char*>(
                                executed.xmp_sidecar_output.data()),
                            executed.xmp_sidecar_output.size());
        } else {
            out["xmp_sidecar_output"] = nb::none();
        }
        append_persist_result_to_python(persisted, persist_requested, &out);

        out["time_patch_status"]      = exec.time_patch.status;
        out["time_patch_status_name"] = nb::str(
            transfer_status_name(exec.time_patch.status));
        out["time_patch_patched_slots"] = nb::int_(
            exec.time_patch.patched_slots);
        out["time_patch_skipped_slots"] = nb::int_(
            exec.time_patch.skipped_slots);
        out["time_patch_errors"]      = nb::int_(exec.time_patch.errors);
        out["time_patch_message"]     = nb::str(exec.time_patch.message.c_str(),
                                                exec.time_patch.message.size());
        out["c2pa_stage_requested"]   = nb::bool_(exec.c2pa_stage_requested);
        out["c2pa_stage_status"]      = exec.c2pa_stage.status;
        out["c2pa_stage_status_name"] = nb::str(
            transfer_status_name(exec.c2pa_stage.status));
        out["c2pa_stage_code"]      = exec.c2pa_stage.code;
        out["c2pa_stage_code_name"] = nb::str(
            emit_transfer_code_name(exec.c2pa_stage.code));
        out["c2pa_stage_emitted"] = nb::int_(exec.c2pa_stage.emitted);
        out["c2pa_stage_skipped"] = nb::int_(exec.c2pa_stage.skipped);
        out["c2pa_stage_errors"]  = nb::int_(exec.c2pa_stage.errors);
        out["c2pa_stage_message"] = nb::str(exec.c2pa_stage.message.c_str(),
                                            exec.c2pa_stage.message.size());
        out["c2pa_stage_validation_status"] = exec.c2pa_stage_validation.status;
        out["c2pa_stage_validation_status_name"] = nb::str(
            transfer_status_name(exec.c2pa_stage_validation.status));
        out["c2pa_stage_validation_code"] = exec.c2pa_stage_validation.code;
        out["c2pa_stage_validation_code_name"] = nb::str(
            emit_transfer_code_name(exec.c2pa_stage_validation.code));
        out["c2pa_stage_validation_payload_kind"]
            = exec.c2pa_stage_validation.payload_kind;
        out["c2pa_stage_validation_payload_kind_name"] = nb::str(
            transfer_c2pa_signed_payload_kind_name(
                exec.c2pa_stage_validation.payload_kind));
        out["c2pa_stage_validation_semantic_status"]
            = exec.c2pa_stage_validation.semantic_status;
        out["c2pa_stage_validation_semantic_status_name"] = nb::str(
            transfer_c2pa_semantic_status_name(
                exec.c2pa_stage_validation.semantic_status));
        out["c2pa_stage_validation_semantic_reason"]
            = nb::str(exec.c2pa_stage_validation.semantic_reason.c_str(),
                      exec.c2pa_stage_validation.semantic_reason.size());
        out["c2pa_stage_validation_logical_payload_bytes"] = nb::int_(
            exec.c2pa_stage_validation.logical_payload_bytes);
        out["c2pa_stage_validation_staged_payload_bytes"] = nb::int_(
            exec.c2pa_stage_validation.staged_payload_bytes);
        out["c2pa_stage_validation_semantic_manifest_present"] = nb::int_(
            exec.c2pa_stage_validation.semantic_manifest_present);
        out["c2pa_stage_validation_semantic_manifest_count"] = nb::int_(
            exec.c2pa_stage_validation.semantic_manifest_count);
        out["c2pa_stage_validation_semantic_claim_generator_present"] = nb::int_(
            exec.c2pa_stage_validation.semantic_claim_generator_present);
        out["c2pa_stage_validation_semantic_assertion_count"] = nb::int_(
            exec.c2pa_stage_validation.semantic_assertion_count);
        out["c2pa_stage_validation_semantic_primary_claim_assertion_count"]
            = nb::int_(exec.c2pa_stage_validation
                           .semantic_primary_claim_assertion_count);
        out["c2pa_stage_validation_semantic_primary_claim_referenced_by_signature_count"]
            = nb::int_(
                exec.c2pa_stage_validation
                    .semantic_primary_claim_referenced_by_signature_count);
        out["c2pa_stage_validation_semantic_primary_signature_linked_claim_count"]
            = nb::int_(exec.c2pa_stage_validation
                           .semantic_primary_signature_linked_claim_count);
        out["c2pa_stage_validation_semantic_primary_signature_reference_key_hits"]
            = nb::int_(exec.c2pa_stage_validation
                           .semantic_primary_signature_reference_key_hits);
        out["c2pa_stage_validation_semantic_primary_signature_explicit_reference_present"]
            = nb::int_(
                exec.c2pa_stage_validation
                    .semantic_primary_signature_explicit_reference_present);
        out["c2pa_stage_validation_semantic_primary_signature_explicit_reference_resolved_claim_count"]
            = nb::int_(
                exec.c2pa_stage_validation
                    .semantic_primary_signature_explicit_reference_resolved_claim_count);
        out["c2pa_stage_validation_semantic_claim_count"] = nb::int_(
            exec.c2pa_stage_validation.semantic_claim_count);
        out["c2pa_stage_validation_semantic_signature_count"] = nb::int_(
            exec.c2pa_stage_validation.semantic_signature_count);
        out["c2pa_stage_validation_semantic_signature_linked"] = nb::int_(
            exec.c2pa_stage_validation.semantic_signature_linked);
        out["c2pa_stage_validation_semantic_signature_orphan"] = nb::int_(
            exec.c2pa_stage_validation.semantic_signature_orphan);
        out["c2pa_stage_validation_semantic_explicit_reference_signature_count"]
            = nb::int_(exec.c2pa_stage_validation
                           .semantic_explicit_reference_signature_count);
        out["c2pa_stage_validation_semantic_explicit_reference_unresolved_signature_count"]
            = nb::int_(
                exec.c2pa_stage_validation
                    .semantic_explicit_reference_unresolved_signature_count);
        out["c2pa_stage_validation_semantic_explicit_reference_ambiguous_signature_count"]
            = nb::int_(
                exec.c2pa_stage_validation
                    .semantic_explicit_reference_ambiguous_signature_count);
        out["c2pa_stage_validation_staged_segments"] = nb::int_(
            exec.c2pa_stage_validation.staged_segments);
        out["c2pa_stage_validation_errors"] = nb::int_(
            exec.c2pa_stage_validation.errors);
        out["c2pa_stage_validation_message"]
            = nb::str(exec.c2pa_stage_validation.message.c_str(),
                      exec.c2pa_stage_validation.message.size());

        const bool allow_payload_bytes = include_payloads
                                         && unsafe_payload_access;

        nb::list blocks;
        for (size_t i = 0; i < prepared.bundle.blocks.size(); ++i) {
            const PreparedTransferBlock& b = prepared.bundle.blocks[i];
            nb::dict one;
            one["index"] = nb::int_(static_cast<uint32_t>(i));
            one["kind"]  = b.kind;
            one["order"] = nb::int_(b.order);
            one["route"] = nb::str(b.route.c_str(), b.route.size());
            one["size"]  = nb::int_(static_cast<uint64_t>(b.payload.size()));
            if (allow_payload_bytes) {
                one["payload"]
                    = nb::bytes(reinterpret_cast<const char*>(b.payload.data()),
                                b.payload.size());
            } else {
                one["payload"] = nb::none();
            }
            blocks.append(std::move(one));
        }
        out["blocks"] = std::move(blocks);

        nb::list policy_decisions;
        for (size_t i = 0; i < prepared.bundle.policy_decisions.size(); ++i) {
            const PreparedTransferPolicyDecision& d
                = prepared.bundle.policy_decisions[i];
            nb::dict one;
            one["subject"]      = d.subject;
            one["subject_name"] = nb::str(
                transfer_policy_subject_name(d.subject));
            one["requested"]      = d.requested;
            one["requested_name"] = nb::str(
                transfer_policy_action_name(d.requested));
            one["effective"]      = d.effective;
            one["effective_name"] = nb::str(
                transfer_policy_action_name(d.effective));
            one["reason"]      = d.reason;
            one["reason_name"] = nb::str(transfer_policy_reason_name(d.reason));
            one["c2pa_mode"]   = d.c2pa_mode;
            one["c2pa_mode_name"] = nb::str(
                transfer_c2pa_mode_name(d.c2pa_mode));
            one["c2pa_source_kind"]      = d.c2pa_source_kind;
            one["c2pa_source_kind_name"] = nb::str(
                transfer_c2pa_source_kind_name(d.c2pa_source_kind));
            one["c2pa_prepared_output"]      = d.c2pa_prepared_output;
            one["c2pa_prepared_output_name"] = nb::str(
                transfer_c2pa_prepared_output_name(d.c2pa_prepared_output));
            one["matched_entries"] = nb::int_(d.matched_entries);
            one["message"] = nb::str(d.message.c_str(), d.message.size());
            policy_decisions.append(std::move(one));
        }
        out["policy_decisions"] = std::move(policy_decisions);

        const PreparedTransferC2paRewriteRequirements& rewrite
            = prepared.bundle.c2pa_rewrite;
        nb::dict rewrite_dict;
        rewrite_dict["state"]      = rewrite.state;
        rewrite_dict["state_name"] = nb::str(
            transfer_c2pa_rewrite_state_name(rewrite.state));
        rewrite_dict["target_format"]      = rewrite.target_format;
        rewrite_dict["target_format_name"] = nb::str(
            transfer_target_format_name(rewrite.target_format));
        rewrite_dict["source_kind"]      = rewrite.source_kind;
        rewrite_dict["source_kind_name"] = nb::str(
            transfer_c2pa_source_kind_name(rewrite.source_kind));
        rewrite_dict["matched_entries"] = nb::int_(rewrite.matched_entries);
        rewrite_dict["existing_carrier_segments"] = nb::int_(
            rewrite.existing_carrier_segments);
        rewrite_dict["target_carrier_available"] = nb::bool_(
            rewrite.target_carrier_available);
        rewrite_dict["content_change_invalidates_existing"] = nb::bool_(
            rewrite.content_change_invalidates_existing);
        rewrite_dict["requires_manifest_builder"] = nb::bool_(
            rewrite.requires_manifest_builder);
        rewrite_dict["requires_content_binding"] = nb::bool_(
            rewrite.requires_content_binding);
        rewrite_dict["requires_certificate_chain"] = nb::bool_(
            rewrite.requires_certificate_chain);
        rewrite_dict["requires_private_key"] = nb::bool_(
            rewrite.requires_private_key);
        rewrite_dict["requires_signing_time"] = nb::bool_(
            rewrite.requires_signing_time);
        rewrite_dict["content_binding_bytes"] = nb::int_(
            rewrite.content_binding_bytes);
        nb::list binding_chunks;
        for (size_t i = 0; i < rewrite.content_binding_chunks.size(); ++i) {
            const PreparedTransferC2paRewriteChunk& chunk
                = rewrite.content_binding_chunks[i];
            nb::dict one;
            one["index"]     = nb::int_(static_cast<uint32_t>(i));
            one["kind"]      = chunk.kind;
            one["kind_name"] = nb::str(
                transfer_c2pa_rewrite_chunk_kind_name(chunk.kind));
            one["source_offset"]    = nb::int_(chunk.source_offset);
            one["size"]             = nb::int_(chunk.size);
            one["block_index"]      = nb::int_(chunk.block_index);
            one["jpeg_marker_code"] = nb::int_(chunk.jpeg_marker_code);
            if (chunk.block_index < prepared.bundle.blocks.size()) {
                one["route"] = nb::str(
                    prepared.bundle.blocks[chunk.block_index].route.c_str(),
                    prepared.bundle.blocks[chunk.block_index].route.size());
            } else {
                one["route"] = nb::none();
            }
            binding_chunks.append(std::move(one));
        }
        rewrite_dict["content_binding_chunks"] = std::move(binding_chunks);
        rewrite_dict["message"] = nb::str(rewrite.message.c_str(),
                                          rewrite.message.size());
        out["c2pa_rewrite"]     = std::move(rewrite_dict);

        PreparedTransferC2paSignRequest sign_request;
        const TransferStatus sign_request_status
            = build_prepared_c2pa_sign_request(prepared.bundle, &sign_request);
        nb::dict sign_request_dict;
        sign_request_dict["status"]      = sign_request_status;
        sign_request_dict["status_name"] = nb::str(
            transfer_status_name(sign_request_status));
        sign_request_dict["rewrite_state"]      = sign_request.rewrite_state;
        sign_request_dict["rewrite_state_name"] = nb::str(
            transfer_c2pa_rewrite_state_name(sign_request.rewrite_state));
        sign_request_dict["target_format"]      = sign_request.target_format;
        sign_request_dict["target_format_name"] = nb::str(
            transfer_target_format_name(sign_request.target_format));
        sign_request_dict["source_kind"]      = sign_request.source_kind;
        sign_request_dict["source_kind_name"] = nb::str(
            transfer_c2pa_source_kind_name(sign_request.source_kind));
        sign_request_dict["carrier_route"]
            = nb::str(sign_request.carrier_route.c_str(),
                      sign_request.carrier_route.size());
        sign_request_dict["manifest_label"]
            = nb::str(sign_request.manifest_label.c_str(),
                      sign_request.manifest_label.size());
        sign_request_dict["existing_carrier_segments"] = nb::int_(
            sign_request.existing_carrier_segments);
        sign_request_dict["source_range_chunks"] = nb::int_(
            sign_request.source_range_chunks);
        sign_request_dict["prepared_segment_chunks"] = nb::int_(
            sign_request.prepared_segment_chunks);
        sign_request_dict["content_binding_bytes"] = nb::int_(
            sign_request.content_binding_bytes);
        sign_request_dict["requires_manifest_builder"] = nb::bool_(
            sign_request.requires_manifest_builder);
        sign_request_dict["requires_content_binding"] = nb::bool_(
            sign_request.requires_content_binding);
        sign_request_dict["requires_certificate_chain"] = nb::bool_(
            sign_request.requires_certificate_chain);
        sign_request_dict["requires_private_key"] = nb::bool_(
            sign_request.requires_private_key);
        sign_request_dict["requires_signing_time"] = nb::bool_(
            sign_request.requires_signing_time);
        nb::list sign_request_chunks;
        for (size_t i = 0; i < sign_request.content_binding_chunks.size();
             ++i) {
            const PreparedTransferC2paRewriteChunk& chunk
                = sign_request.content_binding_chunks[i];
            nb::dict one;
            one["index"]     = nb::int_(static_cast<uint32_t>(i));
            one["kind"]      = chunk.kind;
            one["kind_name"] = nb::str(
                transfer_c2pa_rewrite_chunk_kind_name(chunk.kind));
            one["source_offset"]    = nb::int_(chunk.source_offset);
            one["size"]             = nb::int_(chunk.size);
            one["block_index"]      = nb::int_(chunk.block_index);
            one["jpeg_marker_code"] = nb::int_(chunk.jpeg_marker_code);
            if (chunk.block_index < prepared.bundle.blocks.size()) {
                one["route"] = nb::str(
                    prepared.bundle.blocks[chunk.block_index].route.c_str(),
                    prepared.bundle.blocks[chunk.block_index].route.size());
            } else {
                one["route"] = nb::none();
            }
            sign_request_chunks.append(std::move(one));
        }
        sign_request_dict["content_binding_chunks"] = std::move(
            sign_request_chunks);
        sign_request_dict["message"] = nb::str(sign_request.message.c_str(),
                                               sign_request.message.size());
        out["c2pa_sign_request"]     = std::move(sign_request_dict);

        PreparedTransferC2paHandoffPackage c2pa_handoff;
        PreparedTransferC2paSignedPackage c2pa_signed_package;
        PreparedTransferC2paPackageIoResult c2pa_handoff_io;
        PreparedTransferC2paPackageIoResult c2pa_signed_package_io;
        std::vector<std::byte> c2pa_handoff_bytes;
        std::vector<std::byte> c2pa_signed_package_bytes;
        out["c2pa_binding_requested"] = nb::bool_(include_c2pa_binding_bytes);
        if (include_c2pa_binding_bytes && !unsafe_c2pa_binding_access) {
            c2pa_handoff.binding.status = TransferStatus::UnsafeData;
            c2pa_handoff.binding.code   = EmitTransferCode::InvalidArgument;
            c2pa_handoff.binding.errors = 1U;
            c2pa_handoff.binding.message
                = "safe transfer_probe forbids c2pa binding bytes; use "
                  "unsafe_transfer_probe(include_c2pa_binding_bytes=True)";
        } else if (include_c2pa_binding_bytes) {
            const std::string binding_path
                = !file_options.edit_target_path.empty()
                      ? file_options.edit_target_path
                      : path;
            try {
                std::vector<std::byte> binding_input;
                {
                    nb::gil_scoped_release gil_release;
                    binding_input = read_file_bytes(
                        binding_path.c_str(),
                        file_options.prepare.policy.max_file_bytes);
                }
                build_prepared_c2pa_handoff_package(
                    prepared.bundle,
                    std::span<const std::byte>(binding_input.data(),
                                               binding_input.size()),
                    &c2pa_handoff);
            } catch (const std::exception& ex) {
                c2pa_handoff.binding.status = TransferStatus::InvalidArgument;
                c2pa_handoff.binding.code   = EmitTransferCode::InvalidArgument;
                c2pa_handoff.binding.errors = 1U;
                c2pa_handoff.binding.message = ex.what();
            }
        }
        out["c2pa_binding_status"]      = c2pa_handoff.binding.status;
        out["c2pa_binding_status_name"] = nb::str(
            transfer_status_name(c2pa_handoff.binding.status));
        out["c2pa_binding_code"]      = c2pa_handoff.binding.code;
        out["c2pa_binding_code_name"] = nb::str(
            emit_transfer_code_name(c2pa_handoff.binding.code));
        out["c2pa_binding_bytes_written"] = nb::int_(
            c2pa_handoff.binding.written);
        out["c2pa_binding_errors"] = nb::int_(c2pa_handoff.binding.errors);
        out["c2pa_binding_message"]
            = nb::str(c2pa_handoff.binding.message.c_str(),
                      c2pa_handoff.binding.message.size());
        if (include_c2pa_binding_bytes && unsafe_c2pa_binding_access
            && c2pa_handoff.binding.status == TransferStatus::Ok) {
            out["c2pa_binding_bytes"]
                = nb::bytes(reinterpret_cast<const char*>(
                                c2pa_handoff.binding_bytes.data()),
                            c2pa_handoff.binding_bytes.size());
        } else {
            out["c2pa_binding_bytes"] = nb::none();
        }

        out["c2pa_handoff_requested"] = nb::bool_(include_c2pa_handoff_bytes);
        if (include_c2pa_handoff_bytes && !unsafe_c2pa_package_access) {
            c2pa_handoff_io.status = TransferStatus::UnsafeData;
            c2pa_handoff_io.code   = EmitTransferCode::InvalidArgument;
            c2pa_handoff_io.errors = 1U;
            c2pa_handoff_io.message
                = "safe transfer_probe forbids c2pa handoff bytes; use "
                  "unsafe_transfer_probe(include_c2pa_handoff_bytes=True)";
        } else if (include_c2pa_handoff_bytes) {
            const std::string binding_path
                = !file_options.edit_target_path.empty()
                      ? file_options.edit_target_path
                      : path;
            try {
                std::vector<std::byte> binding_input;
                {
                    nb::gil_scoped_release gil_release;
                    binding_input = read_file_bytes(
                        binding_path.c_str(),
                        file_options.prepare.policy.max_file_bytes);
                }
                build_prepared_c2pa_handoff_package(
                    prepared.bundle,
                    std::span<const std::byte>(binding_input.data(),
                                               binding_input.size()),
                    &c2pa_handoff);
                if (c2pa_handoff.binding.status == TransferStatus::Ok) {
                    c2pa_handoff_io = serialize_prepared_c2pa_handoff_package(
                        c2pa_handoff, &c2pa_handoff_bytes);
                } else {
                    c2pa_handoff_io.status  = c2pa_handoff.binding.status;
                    c2pa_handoff_io.code    = c2pa_handoff.binding.code;
                    c2pa_handoff_io.bytes   = 0U;
                    c2pa_handoff_io.errors  = c2pa_handoff.binding.errors;
                    c2pa_handoff_io.message = c2pa_handoff.binding.message;
                }
            } catch (const std::exception& ex) {
                c2pa_handoff_io.status  = TransferStatus::InvalidArgument;
                c2pa_handoff_io.code    = EmitTransferCode::InvalidArgument;
                c2pa_handoff_io.errors  = 1U;
                c2pa_handoff_io.message = ex.what();
            }
        }
        out["c2pa_handoff_status"]      = c2pa_handoff_io.status;
        out["c2pa_handoff_status_name"] = nb::str(
            transfer_status_name(c2pa_handoff_io.status));
        out["c2pa_handoff_code"]      = c2pa_handoff_io.code;
        out["c2pa_handoff_code_name"] = nb::str(
            emit_transfer_code_name(c2pa_handoff_io.code));
        out["c2pa_handoff_bytes_written"] = nb::int_(c2pa_handoff_io.bytes);
        out["c2pa_handoff_errors"]        = nb::int_(c2pa_handoff_io.errors);
        out["c2pa_handoff_message"] = nb::str(c2pa_handoff_io.message.c_str(),
                                              c2pa_handoff_io.message.size());
        if (include_c2pa_handoff_bytes && unsafe_c2pa_package_access
            && c2pa_handoff_io.status == TransferStatus::Ok) {
            out["c2pa_handoff_bytes"] = nb::bytes(
                reinterpret_cast<const char*>(c2pa_handoff_bytes.data()),
                c2pa_handoff_bytes.size());
        } else {
            out["c2pa_handoff_bytes"] = nb::none();
        }

        out["c2pa_signed_package_requested"] = nb::bool_(
            include_c2pa_signed_package_bytes);
        if (include_c2pa_signed_package_bytes && !unsafe_c2pa_package_access) {
            c2pa_signed_package_io.status = TransferStatus::UnsafeData;
            c2pa_signed_package_io.code   = EmitTransferCode::InvalidArgument;
            c2pa_signed_package_io.errors = 1U;
            c2pa_signed_package_io.message
                = "safe transfer_probe forbids c2pa signed package bytes; "
                  "use unsafe_transfer_probe("
                  "include_c2pa_signed_package_bytes=True)";
        } else if (include_c2pa_signed_package_bytes) {
            if (have_c2pa_signed_package) {
                c2pa_signed_package    = file_options.c2pa_signed_package;
                c2pa_signed_package_io = serialize_prepared_c2pa_signed_package(
                    c2pa_signed_package, &c2pa_signed_package_bytes);
            } else {
                const TransferStatus signed_package_status
                    = build_prepared_c2pa_signed_package(
                        prepared.bundle, file_options.c2pa_signer_input,
                        &c2pa_signed_package);
                if (signed_package_status != TransferStatus::Ok) {
                    c2pa_signed_package_io.status = signed_package_status;
                    c2pa_signed_package_io.code
                        = EmitTransferCode::InvalidArgument;
                    c2pa_signed_package_io.errors = 1U;
                    c2pa_signed_package_io.message
                        = c2pa_signed_package.request.message.empty()
                              ? "failed to build c2pa signed package"
                              : c2pa_signed_package.request.message;
                } else {
                    c2pa_signed_package_io
                        = serialize_prepared_c2pa_signed_package(
                            c2pa_signed_package, &c2pa_signed_package_bytes);
                }
            }
        }
        out["c2pa_signed_package_status"]      = c2pa_signed_package_io.status;
        out["c2pa_signed_package_status_name"] = nb::str(
            transfer_status_name(c2pa_signed_package_io.status));
        out["c2pa_signed_package_code"]      = c2pa_signed_package_io.code;
        out["c2pa_signed_package_code_name"] = nb::str(
            emit_transfer_code_name(c2pa_signed_package_io.code));
        out["c2pa_signed_package_bytes_written"] = nb::int_(
            c2pa_signed_package_io.bytes);
        out["c2pa_signed_package_errors"] = nb::int_(
            c2pa_signed_package_io.errors);
        out["c2pa_signed_package_message"]
            = nb::str(c2pa_signed_package_io.message.c_str(),
                      c2pa_signed_package_io.message.size());
        if (include_c2pa_signed_package_bytes && unsafe_c2pa_package_access
            && c2pa_signed_package_io.status == TransferStatus::Ok) {
            out["c2pa_signed_package_bytes"] = nb::bytes(
                reinterpret_cast<const char*>(c2pa_signed_package_bytes.data()),
                c2pa_signed_package_bytes.size());
        } else {
            out["c2pa_signed_package_bytes"] = nb::none();
        }

        PreparedTransferPayloadBatch transfer_payload_batch;
        PreparedTransferPayloadIoResult transfer_payload_batch_io;
        std::vector<std::byte> transfer_payload_batch_bytes;
        out["transfer_payload_batch_requested"] = nb::bool_(
            include_transfer_payload_batch_bytes);
        if (include_transfer_payload_batch_bytes && !unsafe_payload_access) {
            transfer_payload_batch_io.status = TransferStatus::UnsafeData;
            transfer_payload_batch_io.code = EmitTransferCode::InvalidArgument;
            transfer_payload_batch_io.errors = 1U;
            transfer_payload_batch_io.message
                = "safe transfer_probe forbids transfer payload batch bytes; "
                  "use unsafe_transfer_probe("
                  "include_transfer_payload_batch_bytes=True)";
        } else if (include_transfer_payload_batch_bytes) {
            const EmitTransferResult payload_batch_status
                = build_prepared_transfer_payload_batch(prepared.bundle,
                                                        &transfer_payload_batch);
            if (payload_batch_status.status != TransferStatus::Ok) {
                transfer_payload_batch_io.status = payload_batch_status.status;
                transfer_payload_batch_io.code   = payload_batch_status.code;
                transfer_payload_batch_io.errors = payload_batch_status.errors;
                transfer_payload_batch_io.message = payload_batch_status.message;
            } else {
                transfer_payload_batch_io
                    = serialize_prepared_transfer_payload_batch(
                        transfer_payload_batch, &transfer_payload_batch_bytes);
            }
        }
        out["transfer_payload_batch_status"] = transfer_payload_batch_io.status;
        out["transfer_payload_batch_status_name"] = nb::str(
            transfer_status_name(transfer_payload_batch_io.status));
        out["transfer_payload_batch_code"] = transfer_payload_batch_io.code;
        out["transfer_payload_batch_code_name"] = nb::str(
            emit_transfer_code_name(transfer_payload_batch_io.code));
        out["transfer_payload_batch_bytes_written"] = nb::int_(
            transfer_payload_batch_io.bytes);
        out["transfer_payload_batch_errors"] = nb::int_(
            transfer_payload_batch_io.errors);
        out["transfer_payload_batch_message"]
            = nb::str(transfer_payload_batch_io.message.c_str(),
                      transfer_payload_batch_io.message.size());
        if (include_transfer_payload_batch_bytes && unsafe_payload_access
            && transfer_payload_batch_io.status == TransferStatus::Ok) {
            out["transfer_payload_batch_bytes"]
                = nb::bytes(reinterpret_cast<const char*>(
                                transfer_payload_batch_bytes.data()),
                            transfer_payload_batch_bytes.size());
        } else {
            out["transfer_payload_batch_bytes"] = nb::none();
        }

        PreparedTransferPackageBatch transfer_package_batch;
        PreparedTransferPackageIoResult transfer_package_batch_io;
        std::vector<std::byte> transfer_package_batch_bytes;
        out["transfer_package_batch_requested"] = nb::bool_(
            include_transfer_package_batch_bytes);
        if (include_transfer_package_batch_bytes && !unsafe_payload_access) {
            transfer_package_batch_io.status = TransferStatus::UnsafeData;
            transfer_package_batch_io.code = EmitTransferCode::InvalidArgument;
            transfer_package_batch_io.errors = 1U;
            transfer_package_batch_io.message
                = "safe transfer_probe forbids transfer package batch bytes; "
                  "use unsafe_transfer_probe("
                  "include_transfer_package_batch_bytes=True)";
        } else if (include_transfer_package_batch_bytes) {
            try {
                if (prepared.file_status != TransferFileStatus::Ok
                    || (prepared.prepare.status != TransferStatus::Ok
                        && (!exec.c2pa_stage_requested
                            || exec.c2pa_stage.status != TransferStatus::Ok))) {
                    transfer_package_batch_io.status
                        = TransferStatus::InvalidArgument;
                    transfer_package_batch_io.code
                        = EmitTransferCode::InvalidArgument;
                    transfer_package_batch_io.errors = 1U;
                    transfer_package_batch_io.message
                        = "transfer execution is not available for package batch";
                } else {
                    std::vector<std::byte> package_input;
                    if (exec.edit_requested) {
                        const std::string package_path
                            = !file_options.edit_target_path.empty()
                                  ? file_options.edit_target_path
                                  : path;
                        {
                            nb::gil_scoped_release gil_release;
                            package_input = read_file_bytes(
                                package_path.c_str(),
                                file_options.prepare.policy.max_file_bytes);
                        }
                    }
                    const EmitTransferResult package_batch_status
                        = build_executed_transfer_package_batch(
                            std::span<const std::byte>(package_input.data(),
                                                       package_input.size()),
                            prepared.bundle, exec, &transfer_package_batch);
                    if (package_batch_status.status != TransferStatus::Ok) {
                        transfer_package_batch_io.status
                            = package_batch_status.status;
                        transfer_package_batch_io.code
                            = package_batch_status.code;
                        transfer_package_batch_io.errors
                            = package_batch_status.errors;
                        transfer_package_batch_io.message
                            = package_batch_status.message;
                    } else {
                        transfer_package_batch_io
                            = serialize_prepared_transfer_package_batch(
                                transfer_package_batch,
                                &transfer_package_batch_bytes);
                    }
                }
            } catch (const std::exception& ex) {
                transfer_package_batch_io.status
                    = TransferStatus::InvalidArgument;
                transfer_package_batch_io.code
                    = EmitTransferCode::InvalidArgument;
                transfer_package_batch_io.errors  = 1U;
                transfer_package_batch_io.message = ex.what();
            }
        }
        out["transfer_package_batch_status"] = transfer_package_batch_io.status;
        out["transfer_package_batch_status_name"] = nb::str(
            transfer_status_name(transfer_package_batch_io.status));
        out["transfer_package_batch_code"] = transfer_package_batch_io.code;
        out["transfer_package_batch_code_name"] = nb::str(
            emit_transfer_code_name(transfer_package_batch_io.code));
        out["transfer_package_batch_bytes_written"] = nb::int_(
            transfer_package_batch_io.bytes);
        out["transfer_package_batch_errors"] = nb::int_(
            transfer_package_batch_io.errors);
        out["transfer_package_batch_message"]
            = nb::str(transfer_package_batch_io.message.c_str(),
                      transfer_package_batch_io.message.size());
        if (include_transfer_package_batch_bytes && unsafe_payload_access
            && transfer_package_batch_io.status == TransferStatus::Ok) {
            out["transfer_package_batch_bytes"]
                = nb::bytes(reinterpret_cast<const char*>(
                                transfer_package_batch_bytes.data()),
                            transfer_package_batch_bytes.size());
        } else {
            out["transfer_package_batch_bytes"] = nb::none();
        }

        out["compile_status"]      = exec.compile.status;
        out["compile_status_name"] = nb::str(
            transfer_status_name(exec.compile.status));
        out["compile_code"]      = exec.compile.code;
        out["compile_code_name"] = nb::str(
            emit_transfer_code_name(exec.compile.code));
        out["compile_ops"]     = nb::int_(exec.compiled_ops);
        out["compile_message"] = nb::str(exec.compile.message.c_str(),
                                         exec.compile.message.size());

        out["emit_status"]      = exec.emit.status;
        out["emit_status_name"] = nb::str(
            transfer_status_name(exec.emit.status));
        out["emit_code"]      = exec.emit.code;
        out["emit_code_name"] = nb::str(
            emit_transfer_code_name(exec.emit.code));
        out["emit_emitted"]            = nb::int_(exec.emit.emitted);
        out["emit_skipped"]            = nb::int_(exec.emit.skipped);
        out["emit_errors"]             = nb::int_(exec.emit.errors);
        out["emit_failed_block_index"] = nb::int_(exec.emit.failed_block_index);
        out["emit_message"]            = nb::str(exec.emit.message.c_str(),
                                                 exec.emit.message.size());

        nb::list marker_summary;
        for (size_t i = 0; i < exec.marker_summary.size(); ++i) {
            nb::dict one;
            one["marker"] = nb::int_(exec.marker_summary[i].marker);
            one["count"]  = nb::int_(exec.marker_summary[i].count);
            one["bytes"]  = nb::int_(exec.marker_summary[i].bytes);
            marker_summary.append(std::move(one));
        }
        out["marker_summary"] = std::move(marker_summary);

        nb::list tiff_tag_summary;
        for (size_t i = 0; i < exec.tiff_tag_summary.size(); ++i) {
            nb::dict one;
            one["tag"]   = nb::int_(exec.tiff_tag_summary[i].tag);
            one["count"] = nb::int_(exec.tiff_tag_summary[i].count);
            one["bytes"] = nb::int_(exec.tiff_tag_summary[i].bytes);
            tiff_tag_summary.append(std::move(one));
        }
        out["tiff_tag_summary"] = std::move(tiff_tag_summary);
        out["tiff_commit"]      = nb::bool_(exec.tiff_commit);

        nb::list jxl_box_summary;
        for (size_t i = 0; i < exec.jxl_box_summary.size(); ++i) {
            nb::dict one;
            one["type"]  = nb::str(exec.jxl_box_summary[i].type.data(),
                                   exec.jxl_box_summary[i].type.size());
            one["count"] = nb::int_(exec.jxl_box_summary[i].count);
            one["bytes"] = nb::int_(exec.jxl_box_summary[i].bytes);
            jxl_box_summary.append(std::move(one));
        }
        out["jxl_box_summary"] = std::move(jxl_box_summary);
        if (prepared.bundle.target_format == TransferTargetFormat::Jxl) {
            PreparedJxlEncoderHandoffView jxl_handoff;
            const EmitTransferResult jxl_handoff_result
                = build_prepared_jxl_encoder_handoff_view(prepared.bundle,
                                                          &jxl_handoff);
            nb::dict one;
            one["status"]      = jxl_handoff_result.status;
            one["status_name"] = nb::str(
                transfer_status_name(jxl_handoff_result.status));
            one["code"]      = jxl_handoff_result.code;
            one["code_name"] = nb::str(
                emit_transfer_code_name(jxl_handoff_result.code));
            one["errors"]          = nb::int_(jxl_handoff_result.errors);
            one["message"]         = nb::str(jxl_handoff_result.message.c_str(),
                                             jxl_handoff_result.message.size());
            one["has_icc_profile"] = nb::bool_(jxl_handoff.has_icc_profile);
            one["icc_block_index"] = nb::int_(jxl_handoff.icc_block_index);
            one["icc_profile_bytes"] = nb::int_(jxl_handoff.icc_profile_bytes);
            one["box_count"]         = nb::int_(jxl_handoff.box_count);
            one["box_payload_bytes"] = nb::int_(jxl_handoff.box_payload_bytes);
            out["jxl_encoder_handoff"] = std::move(one);
        } else {
            out["jxl_encoder_handoff"] = nb::none();
        }
        PreparedJxlEncoderHandoff jxl_encoder_handoff_owned;
        PreparedJxlEncoderHandoffIoResult jxl_encoder_handoff_io;
        std::vector<std::byte> jxl_encoder_handoff_bytes;
        out["jxl_encoder_handoff_requested"] = nb::bool_(
            include_jxl_encoder_handoff_bytes);
        if (include_jxl_encoder_handoff_bytes && !unsafe_payload_access) {
            jxl_encoder_handoff_io.status = TransferStatus::UnsafeData;
            jxl_encoder_handoff_io.code   = EmitTransferCode::InvalidArgument;
            jxl_encoder_handoff_io.errors = 1U;
            jxl_encoder_handoff_io.message
                = "safe transfer_probe forbids jxl encoder handoff bytes; "
                  "use unsafe_transfer_probe("
                  "include_jxl_encoder_handoff_bytes=True)";
        } else if (include_jxl_encoder_handoff_bytes) {
            if (prepared.bundle.target_format != TransferTargetFormat::Jxl) {
                jxl_encoder_handoff_io.status = TransferStatus::InvalidArgument;
                jxl_encoder_handoff_io.code = EmitTransferCode::InvalidArgument;
                jxl_encoder_handoff_io.errors = 1U;
                jxl_encoder_handoff_io.message
                    = "jxl encoder handoff bytes require target_format=Jxl";
            } else {
                const EmitTransferResult handoff_status
                    = build_prepared_jxl_encoder_handoff(
                        prepared.bundle, &jxl_encoder_handoff_owned);
                if (handoff_status.status != TransferStatus::Ok) {
                    jxl_encoder_handoff_io.status  = handoff_status.status;
                    jxl_encoder_handoff_io.code    = handoff_status.code;
                    jxl_encoder_handoff_io.errors  = handoff_status.errors;
                    jxl_encoder_handoff_io.message = handoff_status.message;
                } else {
                    jxl_encoder_handoff_io
                        = serialize_prepared_jxl_encoder_handoff(
                            jxl_encoder_handoff_owned,
                            &jxl_encoder_handoff_bytes);
                }
            }
        }
        out["jxl_encoder_handoff_status"]      = jxl_encoder_handoff_io.status;
        out["jxl_encoder_handoff_status_name"] = nb::str(
            transfer_status_name(jxl_encoder_handoff_io.status));
        out["jxl_encoder_handoff_code"]      = jxl_encoder_handoff_io.code;
        out["jxl_encoder_handoff_code_name"] = nb::str(
            emit_transfer_code_name(jxl_encoder_handoff_io.code));
        out["jxl_encoder_handoff_bytes_written"] = nb::int_(
            jxl_encoder_handoff_io.bytes);
        out["jxl_encoder_handoff_errors"] = nb::int_(
            jxl_encoder_handoff_io.errors);
        out["jxl_encoder_handoff_message"]
            = nb::str(jxl_encoder_handoff_io.message.c_str(),
                      jxl_encoder_handoff_io.message.size());
        if (include_jxl_encoder_handoff_bytes && unsafe_payload_access
            && jxl_encoder_handoff_io.status == TransferStatus::Ok) {
            out["jxl_encoder_handoff_bytes"] = nb::bytes(
                reinterpret_cast<const char*>(jxl_encoder_handoff_bytes.data()),
                jxl_encoder_handoff_bytes.size());
        } else {
            out["jxl_encoder_handoff_bytes"] = nb::none();
        }

        nb::list webp_chunk_summary;
        for (size_t i = 0; i < exec.webp_chunk_summary.size(); ++i) {
            nb::dict one;
            one["type"]  = nb::str(exec.webp_chunk_summary[i].type.data(),
                                   exec.webp_chunk_summary[i].type.size());
            one["count"] = nb::int_(exec.webp_chunk_summary[i].count);
            one["bytes"] = nb::int_(exec.webp_chunk_summary[i].bytes);
            webp_chunk_summary.append(std::move(one));
        }
        out["webp_chunk_summary"] = std::move(webp_chunk_summary);

        nb::list png_chunk_summary;
        for (size_t i = 0; i < exec.png_chunk_summary.size(); ++i) {
            nb::dict one;
            one["type"]  = nb::str(exec.png_chunk_summary[i].type.data(),
                                   exec.png_chunk_summary[i].type.size());
            one["count"] = nb::int_(exec.png_chunk_summary[i].count);
            one["bytes"] = nb::int_(exec.png_chunk_summary[i].bytes);
            png_chunk_summary.append(std::move(one));
        }
        out["png_chunk_summary"] = std::move(png_chunk_summary);

        nb::list jp2_box_summary;
        for (size_t i = 0; i < exec.jp2_box_summary.size(); ++i) {
            nb::dict one;
            one["type"]  = nb::str(exec.jp2_box_summary[i].type.data(),
                                   exec.jp2_box_summary[i].type.size());
            one["count"] = nb::int_(exec.jp2_box_summary[i].count);
            one["bytes"] = nb::int_(exec.jp2_box_summary[i].bytes);
            jp2_box_summary.append(std::move(one));
        }
        out["jp2_box_summary"] = std::move(jp2_box_summary);

        nb::list exr_attribute_summary;
        for (size_t i = 0; i < exec.exr_attribute_summary.size(); ++i) {
            nb::dict one;
            one["name"] = nb::str(exec.exr_attribute_summary[i].name.c_str(),
                                  exec.exr_attribute_summary[i].name.size());
            one["type_name"]
                = nb::str(exec.exr_attribute_summary[i].type_name.c_str(),
                          exec.exr_attribute_summary[i].type_name.size());
            one["count"] = nb::int_(exec.exr_attribute_summary[i].count);
            one["bytes"] = nb::int_(exec.exr_attribute_summary[i].bytes);
            exr_attribute_summary.append(std::move(one));
        }
        out["exr_attribute_summary"] = std::move(exr_attribute_summary);

        ExrAdapterBatch exr_attribute_batch_owned;
        ExrAdapterResult exr_attribute_batch_result;
        const bool exr_target = (prepared.bundle.target_format
                                 == TransferTargetFormat::Exr);
        if (exr_target) {
            exr_attribute_batch_result = build_prepared_exr_attribute_batch(
                prepared.bundle, &exr_attribute_batch_owned);
        } else if (include_exr_attribute_values) {
            exr_attribute_batch_result.status
                = ExrAdapterStatus::InvalidArgument;
            exr_attribute_batch_result.errors = 1U;
            exr_attribute_batch_result.message
                = "exr attribute values require target_format=Exr";
        }

        TransferStatus exr_attribute_values_status = TransferStatus::Ok;
        std::string exr_attribute_values_message;
        if (include_exr_attribute_values && !unsafe_payload_access) {
            exr_attribute_values_status = TransferStatus::UnsafeData;
            exr_attribute_values_message
                = "safe transfer_probe forbids exr attribute values; use "
                  "unsafe_transfer_probe(include_exr_attribute_values=True)";
        } else if (include_exr_attribute_values
                   && exr_attribute_batch_result.status
                          == ExrAdapterStatus::InvalidArgument) {
            exr_attribute_values_status  = TransferStatus::InvalidArgument;
            exr_attribute_values_message = exr_attribute_batch_result.message;
        } else if (include_exr_attribute_values
                   && exr_attribute_batch_result.status
                          == ExrAdapterStatus::Unsupported) {
            exr_attribute_values_status  = TransferStatus::Unsupported;
            exr_attribute_values_message = exr_attribute_batch_result.message;
        }

        nb::list exr_attribute_batch;
        if (exr_target
            && exr_attribute_batch_result.status == ExrAdapterStatus::Ok) {
            const bool include_values = include_exr_attribute_values
                                        && unsafe_payload_access;
            for (size_t i = 0; i < exr_attribute_batch_owned.attributes.size();
                 ++i) {
                const ExrAdapterAttribute& attr
                    = exr_attribute_batch_owned.attributes[i];
                nb::dict one;
                one["part_index"] = nb::int_(attr.part_index);
                one["name"]      = nb::str(attr.name.c_str(), attr.name.size());
                one["type_name"] = nb::str(attr.type_name.c_str(),
                                           attr.type_name.size());
                one["is_opaque"] = nb::bool_(attr.is_opaque);
                one["bytes"]     = nb::int_(
                    static_cast<uint64_t>(attr.value.size()));
                if (include_values) {
                    one["value"] = nb::bytes(reinterpret_cast<const char*>(
                                                 attr.value.data()),
                                             attr.value.size());
                } else {
                    one["value"] = nb::none();
                }
                exr_attribute_batch.append(std::move(one));
            }
            out["exr_attribute_batch"] = std::move(exr_attribute_batch);
        } else {
            out["exr_attribute_batch"] = nb::none();
        }
        if (exr_target || include_exr_attribute_values) {
            out["exr_attribute_batch_status"]
                = exr_attribute_batch_result.status;
            out["exr_attribute_batch_status_name"] = nb::str(
                exr_adapter_status_name(exr_attribute_batch_result.status));
        } else {
            out["exr_attribute_batch_status"]      = nb::none();
            out["exr_attribute_batch_status_name"] = nb::none();
        }
        out["exr_attribute_batch_exported"] = nb::int_(
            exr_attribute_batch_result.exported);
        out["exr_attribute_batch_skipped"] = nb::int_(
            exr_attribute_batch_result.skipped);
        out["exr_attribute_batch_errors"] = nb::int_(
            exr_attribute_batch_result.errors);
        out["exr_attribute_batch_message"]
            = nb::str(exr_attribute_batch_result.message.c_str(),
                      exr_attribute_batch_result.message.size());
        out["exr_attribute_values_requested"] = nb::bool_(
            include_exr_attribute_values);
        out["exr_attribute_values_status"]      = exr_attribute_values_status;
        out["exr_attribute_values_status_name"] = nb::str(
            transfer_status_name(exr_attribute_values_status));
        out["exr_attribute_values_message"]
            = nb::str(exr_attribute_values_message.c_str(),
                      exr_attribute_values_message.size());

        nb::list bmff_item_summary;
        for (size_t i = 0; i < exec.bmff_item_summary.size(); ++i) {
            nb::dict one;
            one["item_type"] = nb::int_(exec.bmff_item_summary[i].item_type);
            one["count"]     = nb::int_(exec.bmff_item_summary[i].count);
            one["bytes"]     = nb::int_(exec.bmff_item_summary[i].bytes);
            one["mime_xmp"]  = nb::bool_(exec.bmff_item_summary[i].mime_xmp);
            bmff_item_summary.append(std::move(one));
        }
        out["bmff_item_summary"] = std::move(bmff_item_summary);

        nb::list bmff_property_summary;
        for (size_t i = 0; i < exec.bmff_property_summary.size(); ++i) {
            nb::dict one;
            one["property_type"] = nb::int_(
                exec.bmff_property_summary[i].property_type);
            one["property_subtype"] = nb::int_(
                exec.bmff_property_summary[i].property_subtype);
            one["count"] = nb::int_(exec.bmff_property_summary[i].count);
            one["bytes"] = nb::int_(exec.bmff_property_summary[i].bytes);
            bmff_property_summary.append(std::move(one));
        }
        out["bmff_property_summary"] = std::move(bmff_property_summary);

        out["edit_requested"]        = nb::bool_(exec.edit_requested);
        out["edit_plan_status"]      = exec.edit_plan_status;
        out["edit_plan_status_name"] = nb::str(
            transfer_status_name(exec.edit_plan_status));
        out["edit_plan_message"]      = nb::str(exec.edit_plan_message.c_str(),
                                                exec.edit_plan_message.size());
        out["edit_apply_status"]      = exec.edit_apply.status;
        out["edit_apply_status_name"] = nb::str(
            transfer_status_name(exec.edit_apply.status));
        out["edit_apply_code"]      = exec.edit_apply.code;
        out["edit_apply_code_name"] = nb::str(
            emit_transfer_code_name(exec.edit_apply.code));
        out["edit_apply_emitted"] = nb::int_(exec.edit_apply.emitted);
        out["edit_apply_skipped"] = nb::int_(exec.edit_apply.skipped);
        out["edit_apply_errors"]  = nb::int_(exec.edit_apply.errors);
        out["edit_apply_message"] = nb::str(exec.edit_apply.message.c_str(),
                                            exec.edit_apply.message.size());
        out["edit_input_size"]    = nb::int_(exec.edit_input_size);
        out["edit_output_size"]   = nb::int_(exec.edit_output_size);
        out["edit_removed_existing_segments"] = nb::int_(
            exec.jpeg_edit_plan.removed_existing_segments);
        out["edit_removed_existing_jumbf_segments"] = nb::int_(
            exec.jpeg_edit_plan.removed_existing_jumbf_segments);
        out["edit_removed_existing_c2pa_segments"] = nb::int_(
            exec.jpeg_edit_plan.removed_existing_c2pa_segments);

        const bool allow_edited_bytes = include_edited_bytes
                                        && unsafe_edited_bytes_access;
        if (allow_edited_bytes
            && exec.edit_apply.status == TransferStatus::Ok) {
            out["edited_bytes"] = nb::bytes(reinterpret_cast<const char*>(
                                                exec.edited_output.data()),
                                            exec.edited_output.size());
        } else {
            out["edited_bytes"] = nb::none();
        }

        TransferStatus overall_status = TransferStatus::Ok;
        std::string error_stage       = "none";
        std::string error_code        = "none";
        std::string error_message;

        if (include_payloads && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_payloads_forbidden";
            error_message  = "safe transfer_probe forbids payload bytes; use "
                             "unsafe_transfer_probe(include_payloads=True)";
        } else if (include_transfer_payload_batch_bytes
                   && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_transfer_payload_batch_forbidden";
            error_message  = "safe transfer_probe forbids transfer payload "
                             "batch bytes; use unsafe_transfer_probe("
                             "include_transfer_payload_batch_bytes=True)";
        } else if (include_transfer_package_batch_bytes
                   && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_transfer_package_batch_forbidden";
            error_message  = "safe transfer_probe forbids transfer package "
                             "batch bytes; use unsafe_transfer_probe("
                             "include_transfer_package_batch_bytes=True)";
        } else if (include_exr_attribute_values && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_exr_attribute_values_forbidden";
            error_message  = "safe transfer_probe forbids exr attribute "
                             "values; use unsafe_transfer_probe("
                             "include_exr_attribute_values=True)";
        } else if (include_edited_bytes && !unsafe_edited_bytes_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_edited_bytes_forbidden";
            error_message  = "safe transfer_probe forbids edited bytes; use "
                             "unsafe_transfer_probe(include_edited_bytes=True)";
        } else if (include_c2pa_binding_bytes && !unsafe_c2pa_binding_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_c2pa_binding_bytes_forbidden";
            error_message  = "safe transfer_probe forbids c2pa binding bytes; "
                             "use unsafe_transfer_probe("
                             "include_c2pa_binding_bytes=True)";
        } else if ((include_c2pa_handoff_bytes
                    || include_c2pa_signed_package_bytes)
                   && !unsafe_c2pa_package_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_c2pa_package_bytes_forbidden";
            error_message  = "safe transfer_probe forbids c2pa package bytes; "
                             "use unsafe_transfer_probe("
                             "include_c2pa_handoff_bytes=True or "
                             "include_c2pa_signed_package_bytes=True)";
        } else if (exec.c2pa_stage_requested
                   && exec.c2pa_stage.status != TransferStatus::Ok) {
            overall_status = exec.c2pa_stage.status;
            error_stage    = "c2pa_stage";
            error_code     = emit_transfer_code_name(exec.c2pa_stage.code);
            error_message  = exec.c2pa_stage.message;
        } else if (include_c2pa_signed_package_bytes
                   && c2pa_signed_package_io.status != TransferStatus::Ok) {
            overall_status = c2pa_signed_package_io.status;
            error_stage    = "c2pa_signed_package";
            error_code = emit_transfer_code_name(c2pa_signed_package_io.code);
            error_message = c2pa_signed_package_io.message;
        } else if (include_c2pa_handoff_bytes
                   && c2pa_handoff_io.status != TransferStatus::Ok) {
            overall_status = c2pa_handoff_io.status;
            error_stage    = "c2pa_handoff";
            error_code     = emit_transfer_code_name(c2pa_handoff_io.code);
            error_message  = c2pa_handoff_io.message;
        } else if (include_c2pa_binding_bytes
                   && c2pa_handoff.binding.status != TransferStatus::Ok) {
            overall_status = c2pa_handoff.binding.status;
            error_stage    = "c2pa_binding";
            error_code     = emit_transfer_code_name(c2pa_handoff.binding.code);
            error_message  = c2pa_handoff.binding.message;
        } else if (include_transfer_payload_batch_bytes
                   && transfer_payload_batch_io.status != TransferStatus::Ok) {
            overall_status = transfer_payload_batch_io.status;
            error_stage    = "transfer_payload_batch";
            error_code     = emit_transfer_code_name(
                transfer_payload_batch_io.code);
            error_message = transfer_payload_batch_io.message;
        } else if (include_transfer_package_batch_bytes
                   && transfer_package_batch_io.status != TransferStatus::Ok) {
            overall_status = transfer_package_batch_io.status;
            error_stage    = "transfer_package_batch";
            error_code     = emit_transfer_code_name(
                transfer_package_batch_io.code);
            error_message = transfer_package_batch_io.message;
        } else if (exr_target
                   && exr_attribute_batch_result.status
                          != ExrAdapterStatus::Ok) {
            if (exr_attribute_batch_result.status
                == ExrAdapterStatus::InvalidArgument) {
                overall_status = TransferStatus::InvalidArgument;
            } else if (exr_attribute_batch_result.status
                       == ExrAdapterStatus::Unsupported) {
                overall_status = TransferStatus::Unsupported;
            } else {
                overall_status = TransferStatus::InternalError;
            }
            error_stage = "exr_attribute_batch";
            error_code  = exr_adapter_status_name(
                exr_attribute_batch_result.status);
            error_message = exr_attribute_batch_result.message;
        } else if (include_exr_attribute_values
                   && exr_attribute_values_status != TransferStatus::Ok) {
            overall_status = exr_attribute_values_status;
            error_stage    = "exr_attribute_values";
            error_code     = transfer_status_name(exr_attribute_values_status);
            error_message  = exr_attribute_values_message;
        } else if (exec.time_patch.status != TransferStatus::Ok) {
            overall_status = exec.time_patch.status;
            error_stage    = "time_patch";
            error_code     = "apply_time_patches_failed";
            error_message  = exec.time_patch.message;
        } else if (prepared.file_status != TransferFileStatus::Ok) {
            overall_status = transfer_status_from_file_status(
                prepared.file_status);
            error_stage   = "file";
            error_code    = prepare_transfer_file_code_name(prepared.code);
            error_message = prepared.prepare.message;
        } else if (prepared.prepare.status != TransferStatus::Ok
                   && (!exec.c2pa_stage_requested
                       || exec.c2pa_stage.status != TransferStatus::Ok)) {
            overall_status = prepared.prepare.status;
            error_stage    = "prepare";
            error_code     = prepare_transfer_code_name(prepared.prepare.code);
            error_message  = prepared.prepare.message;
        } else if (exec.compile.status != TransferStatus::Ok) {
            overall_status = exec.compile.status;
            error_stage    = "compile";
            error_code     = emit_transfer_code_name(exec.compile.code);
            error_message  = exec.compile.message;
        } else if (exec.edit_requested
                   && exec.edit_plan_status != TransferStatus::Ok) {
            overall_status = exec.edit_plan_status;
            error_stage    = "edit_plan";
            error_code     = "edit_plan_failed";
            error_message  = exec.edit_plan_message;
        } else if (exec.edit_requested && edit_do_apply
                   && exec.edit_apply.status != TransferStatus::Ok) {
            overall_status = exec.edit_apply.status;
            error_stage    = "edit_apply";
            error_code     = emit_transfer_code_name(exec.edit_apply.code);
            error_message  = exec.edit_apply.message;
        } else if (exec.emit.status != TransferStatus::Ok) {
            overall_status = exec.emit.status;
            error_stage    = "emit";
            error_code     = emit_transfer_code_name(exec.emit.code);
            error_message  = exec.emit.message;
        } else if (persist_requested
                   && persisted.output_status != TransferStatus::Ok) {
            overall_status = persisted.output_status;
            error_stage    = "persist_output";
            error_code     = "persist_output_failed";
            error_message  = persisted.output_message;
        } else if (persist_requested && executed.xmp_sidecar_requested
                   && persisted.xmp_sidecar_status != TransferStatus::Ok) {
            overall_status = persisted.xmp_sidecar_status;
            error_stage    = "persist_xmp_sidecar";
            error_code     = "persist_xmp_sidecar_failed";
            error_message  = persisted.xmp_sidecar_message;
        } else if (persist_requested && executed.xmp_sidecar_cleanup_requested
                   && persisted.xmp_sidecar_cleanup_status
                          != TransferStatus::Ok) {
            overall_status = persisted.xmp_sidecar_cleanup_status;
            error_stage    = "persist_xmp_sidecar_cleanup";
            error_code     = "persist_xmp_sidecar_cleanup_failed";
            error_message  = persisted.xmp_sidecar_cleanup_message;
        }

        out["overall_status"]      = overall_status;
        out["overall_status_name"] = nb::str(
            transfer_status_name(overall_status));
        out["error_stage"]   = nb::str(error_stage.c_str(), error_stage.size());
        out["error_code"]    = nb::str(error_code.c_str(), error_code.size());
        out["error_message"] = nb::str(error_message.c_str(),
                                       error_message.size());
        return out;
    }

    static nb::dict transfer_snapshot_to_python(
        const TransferSourceSnapshot& snapshot,
        TransferTargetFormat target_format, DngTargetMode dng_target_mode,
        XmpSidecarFormat format, bool include_exif_app1, bool include_xmp_app1,
        bool include_icc_app2, bool include_iptc_app13,
        bool xmp_include_existing,
        XmpExistingNamespacePolicy xmp_existing_namespace_policy,
        XmpExistingStandardNamespacePolicy xmp_existing_standard_namespace_policy,
        XmpConflictPolicy xmp_conflict_policy,
        bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
        bool xmp_project_iptc, TransferPolicyAction makernote_policy,
        TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
        uint64_t max_file_bytes, nb::object policy_obj,
        nb::object time_patches_obj, bool time_patch_strict_width,
        bool time_patch_require_slot, bool time_patch_auto_nul,
        nb::object edit_target_path_obj, nb::object target_bytes_obj,
        nb::object xmp_existing_sidecar_base_path_obj,
        nb::object xmp_sidecar_base_path_obj,
        XmpExistingSidecarMode xmp_existing_sidecar_mode,
        XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
        nb::object xmp_existing_destination_embedded_path_obj,
        XmpExistingDestinationEmbeddedMode xmp_existing_destination_embedded_mode,
        XmpExistingDestinationEmbeddedPrecedence
            xmp_existing_destination_embedded_precedence,
        XmpExistingDestinationCarrierPrecedence
            xmp_existing_destination_carrier_precedence,
        XmpExistingDestinationSidecarState xmp_existing_destination_sidecar_state,
        bool edit_do_apply, bool include_edited_bytes,
        bool unsafe_edited_bytes_access, XmpWritebackMode xmp_writeback_mode,
        XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
        XmpDestinationSidecarMode xmp_destination_sidecar_mode,
        nb::object persist_output_path_obj, bool persist_overwrite_output,
        bool persist_overwrite_xmp_sidecar,
        bool persist_remove_destination_xmp_sidecar,
        nb::object target_image_spec_obj,
        nb::object source_raw_data_descriptor_obj,
        TransferSafetyMode transfer_safety,
        TransferRawCarrierPassthroughMode raw_carrier_passthrough_mode)
    {
        ExecutePreparedTransferSnapshotOptions options;
        options.prepare.target_format   = target_format;
        options.prepare.dng_target_mode = dng_target_mode;
        options.prepare.xmp_portable = (format == XmpSidecarFormat::Portable);
        options.prepare.include_exif_app1    = include_exif_app1;
        options.prepare.include_xmp_app1     = include_xmp_app1;
        options.prepare.include_icc_app2     = include_icc_app2;
        options.prepare.include_iptc_app13   = include_iptc_app13;
        options.prepare.xmp_include_existing = xmp_include_existing;
        options.prepare.xmp_existing_namespace_policy
            = xmp_existing_namespace_policy;
        options.prepare.xmp_existing_standard_namespace_policy
            = xmp_existing_standard_namespace_policy;
        options.prepare.xmp_conflict_policy = xmp_conflict_policy;
        options.prepare.xmp_exiftool_gpsdatetime_alias
            = xmp_exiftool_gpsdatetime_alias;
        options.prepare.xmp_project_exif = xmp_project_exif;
        options.prepare.xmp_project_iptc = xmp_project_iptc;
        options.prepare.target_image_spec
            = transfer_target_image_spec_from_python(target_image_spec_obj);
        if (!source_raw_data_descriptor_obj.is_none()) {
            options.prepare.has_source_raw_data_descriptor = true;
            options.prepare.source_raw_data_descriptor
                = nb::cast<MetadataRawDataDescriptor>(
                    source_raw_data_descriptor_obj);
        }
        options.prepare.profile.makernote = makernote_policy;
        options.prepare.profile.jumbf     = jumbf_policy;
        options.prepare.profile.c2pa      = c2pa_policy;
        options.prepare.profile.safety    = transfer_safety;
        options.prepare.raw_carrier_passthrough_mode
            = raw_carrier_passthrough_mode;
        options.policy.max_file_bytes     = max_file_bytes;
        options.xmp_existing_sidecar_mode = xmp_existing_sidecar_mode;
        options.xmp_existing_sidecar_precedence
            = xmp_existing_sidecar_precedence;
        options.xmp_existing_destination_embedded_mode
            = xmp_existing_destination_embedded_mode;
        options.xmp_existing_destination_embedded_precedence
            = xmp_existing_destination_embedded_precedence;
        options.xmp_existing_destination_carrier_precedence
            = xmp_existing_destination_carrier_precedence;
        options.xmp_existing_destination_sidecar_state
            = xmp_existing_destination_sidecar_state;
        options.xmp_writeback_mode            = xmp_writeback_mode;
        options.xmp_destination_embedded_mode = xmp_destination_embedded_mode;
        options.xmp_destination_sidecar_mode  = xmp_destination_sidecar_mode;

        if (!policy_obj.is_none()) {
            options.policy = nb::cast<OpenMetaResourcePolicy>(policy_obj);
            if (max_file_bytes != 0U) {
                options.policy.max_file_bytes = max_file_bytes;
            }
        }

        options.execute.time_patches = parse_time_patches_object(
            time_patches_obj);
        options.execute.time_patch.strict_width = time_patch_strict_width;
        options.execute.time_patch.require_slot = time_patch_require_slot;
        options.execute.time_patch_auto_nul     = time_patch_auto_nul;
        options.execute.edit_apply              = edit_do_apply;
        options.execute.edit_requested          = false;

        if (!edit_target_path_obj.is_none()) {
            options.edit_target_path = nb::cast<std::string>(
                edit_target_path_obj);
            if (!options.edit_target_path.empty()) {
                options.execute.edit_requested = true;
            }
        }
        if (!xmp_existing_sidecar_base_path_obj.is_none()) {
            options.xmp_existing_sidecar_base_path = nb::cast<std::string>(
                xmp_existing_sidecar_base_path_obj);
        }
        if (!xmp_sidecar_base_path_obj.is_none()) {
            options.xmp_sidecar_base_path = nb::cast<std::string>(
                xmp_sidecar_base_path_obj);
        }
        if (!xmp_existing_destination_embedded_path_obj.is_none()) {
            options.xmp_existing_destination_embedded_path
                = nb::cast<std::string>(
                    xmp_existing_destination_embedded_path_obj);
        }

        const bool have_target_bytes = !target_bytes_obj.is_none();
        std::vector<std::byte> target_bytes;
        if (have_target_bytes) {
            target_bytes = bytes_object_to_vector(target_bytes_obj);
            options.execute.edit_requested = true;
        }

        const bool persist_requested = !persist_output_path_obj.is_none();
        std::string persist_output_path;
        if (persist_requested) {
            persist_output_path = nb::cast<std::string>(
                persist_output_path_obj);
        }

        ExecutePreparedTransferFileResult executed;
        PersistPreparedTransferFileResult persisted;
        {
            nb::gil_scoped_release gil_release;
            if (have_target_bytes) {
                executed = execute_prepared_transfer_snapshot(
                    snapshot,
                    std::span<const std::byte>(target_bytes.data(),
                                               target_bytes.size()),
                    options);
            } else {
                executed = execute_prepared_transfer_snapshot(snapshot,
                                                              options);
            }
            if (persist_requested) {
                PersistPreparedTransferFileOptions persist_options;
                persist_options.output_path      = persist_output_path;
                persist_options.overwrite_output = persist_overwrite_output;
                persist_options.overwrite_xmp_sidecar
                    = persist_overwrite_xmp_sidecar;
                persist_options.remove_destination_xmp_sidecar
                    = persist_remove_destination_xmp_sidecar;
                persisted
                    = persist_prepared_transfer_file_result(executed,
                                                            persist_options);
            }
        }

        return transfer_snapshot_result_to_python(executed, persisted,
                                                  persist_requested,
                                                  edit_do_apply,
                                                  include_edited_bytes,
                                                  unsafe_edited_bytes_access);
    }

    static std::string
    format_safety_error_message(const InteropSafetyError& error)
    {
        std::string msg = error.message.empty() ? "unsafe metadata value"
                                                : error.message;
        if (!error.field_name.empty()) {
            msg.append(" [field=");
            msg.append(error.field_name);
            msg.push_back(']');
        }
        if (!error.key_path.empty()) {
            msg.append(" [key=");
            msg.append(error.key_path);
            msg.push_back(']');
        }
        return msg;
    }

    static void throw_safety_error(const InteropSafetyError& error)
    {
        throw std::runtime_error(format_safety_error_message(error));
    }

    static nb::str decode_text_safe_for_python(std::span<const std::byte> bytes,
                                               TextEncoding encoding)
    {
        PyObject* decoded = nullptr;
        switch (encoding) {
        case TextEncoding::Ascii:
            decoded = PyUnicode_DecodeASCII(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<Py_ssize_t>(bytes.size()), "strict");
            break;
        case TextEncoding::Utf8:
        case TextEncoding::Unknown:
            decoded = PyUnicode_DecodeUTF8(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<Py_ssize_t>(bytes.size()), "strict");
            break;
        case TextEncoding::Utf16LE: {
            int byteorder = -1;
            decoded       = PyUnicode_DecodeUTF16(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<Py_ssize_t>(bytes.size()), "strict", &byteorder);
            break;
        }
        case TextEncoding::Utf16BE: {
            int byteorder = 1;
            decoded       = PyUnicode_DecodeUTF16(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<Py_ssize_t>(bytes.size()), "strict", &byteorder);
            break;
        }
        }

        if (!decoded) {
            PyErr_Clear();
            throw std::runtime_error(
                "unsafe text value: invalid or unsupported encoding");
        }
        return nb::steal<nb::str>(nb::handle(decoded));
    }

    static nb::dict icc_interpret_to_python(uint32_t signature,
                                            nb::bytes tag_bytes,
                                            uint32_t max_values,
                                            uint32_t max_text_bytes)
    {
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(tag_bytes.data()),
            tag_bytes.size());
        IccTagInterpretOptions options;
        options.limits.max_values     = max_values;
        options.limits.max_text_bytes = max_text_bytes;

        IccTagInterpretation interpretation;
        const IccTagInterpretStatus status
            = interpret_icc_tag(signature, bytes, &interpretation, options);

        nb::dict out;
        out["status"]    = status;
        out["signature"] = nb::int_(interpretation.signature);
        if (!interpretation.name.empty()) {
            out["name"] = nb::str(interpretation.name.data(),
                                  interpretation.name.size());
        } else {
            out["name"] = nb::none();
        }
        out["type"] = nb::str(interpretation.type.c_str(),
                              interpretation.type.size());

        if (!interpretation.text.empty()) {
            const std::span<const std::byte> text_bytes(
                reinterpret_cast<const std::byte*>(interpretation.text.data()),
                interpretation.text.size());
            out["text"] = decode_text_safe_for_python(text_bytes,
                                                      TextEncoding::Ascii);
        } else {
            out["text"] = nb::none();
        }

        nb::list values;
        for (size_t i = 0; i < interpretation.values.size(); ++i) {
            values.append(nb::float_(interpretation.values[i]));
        }
        out["values"] = std::move(values);
        out["rows"]   = nb::int_(interpretation.rows);
        out["cols"]   = nb::int_(interpretation.cols);
        return out;
    }

    static nb::object icc_render_value_to_python(uint32_t signature,
                                                 nb::bytes tag_bytes,
                                                 uint32_t max_values,
                                                 uint32_t max_text_bytes)
    {
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(tag_bytes.data()),
            tag_bytes.size());
        std::string rendered;
        if (!format_icc_tag_display_value(signature, bytes, max_values,
                                          max_text_bytes, &rendered)) {
            return nb::none();
        }
        const std::span<const std::byte> text_bytes(
            reinterpret_cast<const std::byte*>(rendered.data()),
            rendered.size());
        return decode_text_safe_for_python(text_bytes, TextEncoding::Utf8);
    }

    static nb::dict ocio_node_to_python(const OcioMetadataNode& node)
    {
        nb::dict out;
        out["name"]  = nb::str(node.name.c_str(), node.name.size());
        out["value"] = nb::str(node.value.c_str(), node.value.size());
        nb::list children;
        for (size_t i = 0; i < node.children.size(); ++i) {
            children.append(ocio_node_to_python(node.children[i]));
        }
        out["children"] = std::move(children);
        return out;
    }

    static nb::dict unsafe_ocio_metadata_tree_to_python(
        const MetaStore& store, ExportNameStyle style,
        ExportNamePolicy name_policy, uint32_t max_value_bytes,
        bool include_makernotes, bool include_empty)
    {
        OcioAdapterRequest request;
        request.style              = style;
        request.name_policy        = name_policy;
        request.max_value_bytes    = max_value_bytes;
        request.include_makernotes = include_makernotes;
        request.include_empty      = include_empty;

        OcioMetadataNode root;
        build_ocio_metadata_tree(store, &root, request);
        return ocio_node_to_python(root);
    }

    static nb::dict
    ocio_tree_to_python(const MetaStore& store, ExportNameStyle style,
                        ExportNamePolicy name_policy, uint32_t max_value_bytes,
                        bool include_makernotes, bool include_empty)
    {
        OcioAdapterRequest request;
        request.style              = style;
        request.name_policy        = name_policy;
        request.max_value_bytes    = max_value_bytes;
        request.include_makernotes = include_makernotes;
        request.include_empty      = include_empty;

        OcioMetadataNode root;
        InteropSafetyError error;
        const InteropSafetyStatus status
            = build_ocio_metadata_tree_safe(store, &root, request, &error);
        if (status != InteropSafetyStatus::Ok) {
            throw_safety_error(error);
        }

        return ocio_node_to_python(root);
    }

}  // namespace

struct PyDocument final {
    std::string path;
    MappedFile file;
    std::span<const std::byte> file_bytes;
    std::vector<ContainerBlockRef> blocks;
    std::vector<ExifIfdRef> ifds;
    std::vector<std::byte> payload;
    std::vector<uint32_t> payload_parts;
    MetaStore store;
    SimpleMetaResult result;
};

struct PyMetadataCreationField final {
    MetadataCreationFieldKind kind       = MetadataCreationFieldKind::Title;
    MetadataCreationValueKind value_kind = MetadataCreationValueKind::Text;
    std::string text;
    uint32_t unsigned_value = 0U;
    int32_t signed_value    = 0;
    uint32_t numer          = 0U;
    uint32_t denom          = 1U;
};

struct PyMetadataEditingOperation final {
    MetadataEditingOperationKind kind = MetadataEditingOperationKind::Add;
    PyMetadataCreationField field;
    uint32_t occurrence = 0U;
};

static MetadataCreationField
metadata_creation_field_view(const PyMetadataCreationField& source) noexcept
{
    MetadataCreationField field;
    field.kind           = source.kind;
    field.value_kind     = source.value_kind;
    field.text           = source.text;
    field.unsigned_value = source.unsigned_value;
    field.signed_value   = source.signed_value;
    field.rational.numer = source.numer;
    field.rational.denom = source.denom;
    return field;
}

static PyMetadataCreationField
metadata_creation_text_python(MetadataCreationFieldKind kind,
                              const std::string& value)
{
    PyMetadataCreationField field;
    field.kind       = kind;
    field.value_kind = MetadataCreationValueKind::Text;
    field.text       = value;
    return field;
}


static PyMetadataCreationField
metadata_creation_u32_python(MetadataCreationFieldKind kind, uint32_t value)
{
    PyMetadataCreationField field;
    field.kind           = kind;
    field.value_kind     = MetadataCreationValueKind::UnsignedInteger;
    field.unsigned_value = value;
    return field;
}


static PyMetadataCreationField
metadata_creation_i32_python(MetadataCreationFieldKind kind, int32_t value)
{
    PyMetadataCreationField field;
    field.kind         = kind;
    field.value_kind   = MetadataCreationValueKind::SignedInteger;
    field.signed_value = value;
    return field;
}


static PyMetadataCreationField
metadata_creation_urational_python(MetadataCreationFieldKind kind,
                                   uint32_t numer, uint32_t denom)
{
    PyMetadataCreationField field;
    field.kind       = kind;
    field.value_kind = MetadataCreationValueKind::UnsignedRational;
    field.numer      = numer;
    field.denom      = denom;
    return field;
}


static PyMetadataEditingOperation
metadata_edit_add_python(const PyMetadataCreationField& field)
{
    PyMetadataEditingOperation operation;
    operation.kind  = MetadataEditingOperationKind::Add;
    operation.field = field;
    return operation;
}


static PyMetadataEditingOperation
metadata_edit_set_python(const PyMetadataCreationField& field,
                         uint32_t occurrence)
{
    PyMetadataEditingOperation operation;
    operation.kind       = MetadataEditingOperationKind::Set;
    operation.field      = field;
    operation.occurrence = occurrence;
    return operation;
}


static PyMetadataEditingOperation
metadata_edit_remove_python(MetadataCreationFieldKind kind, uint32_t occurrence)
{
    PyMetadataEditingOperation operation;
    operation.kind       = MetadataEditingOperationKind::Remove;
    operation.field.kind = kind;
    operation.occurrence = occurrence;
    return operation;
}


static PyMetadataEditingOperation
metadata_edit_remove_all_python(MetadataCreationFieldKind kind)
{
    return metadata_edit_remove_python(kind, kMetadataEditingAllOccurrences);
}


static std::shared_ptr<PyDocument>
create_metadata_document(const std::vector<PyMetadataCreationField>& fields,
                         uint32_t max_fields, uint32_t max_text_bytes_per_field,
                         uint64_t max_total_text_bytes)
{
    std::vector<MetadataCreationField> request_fields;
    request_fields.reserve(fields.size());
    for (const PyMetadataCreationField& source : fields) {
        request_fields.push_back(metadata_creation_field_view(source));
    }

    MetadataCreationRequest request;
    request.fields                          = request_fields;
    request.limits.max_fields               = max_fields;
    request.limits.max_text_bytes_per_field = max_text_bytes_per_field;
    request.limits.max_total_text_bytes     = max_total_text_bytes;

    MetaStore created;
    MetadataCreationResult result;
    {
        nb::gil_scoped_release gil_release;
        result = create_metadata(request, &created);
    }
    if (result.status != MetadataCreationStatus::Ok) {
        std::string message = "metadata creation failed: ";
        message += metadata_creation_status_name(result.status);
        if (result.failed_field_index != kInvalidMetadataCreationFieldIndex) {
            message += " at field ";
            message += std::to_string(result.failed_field_index);
        }
        throw std::invalid_argument(message);
    }

    auto document                        = std::make_shared<PyDocument>();
    document->store                      = std::move(created);
    document->result.xmp.entries_decoded = result.entries_created;
    return document;
}

static uint32_t
active_xmp_entry_count(const MetaStore& store) noexcept
{
    uint32_t count                       = 0U;
    const std::span<const Entry> entries = store.entries();
    for (size_t i = 0U; i < entries.size(); ++i) {
        if (entries[i].key.kind != MetaKeyKind::XmpProperty
            || any(entries[i].flags, EntryFlags::Deleted)) {
            continue;
        }
        if (count != 0xffffffffU) {
            ++count;
        }
    }
    return count;
}

static std::shared_ptr<PyDocument>
edit_metadata_document(std::shared_ptr<PyDocument> source,
                       const std::vector<PyMetadataEditingOperation>& operations,
                       uint32_t max_operations,
                       uint32_t max_text_bytes_per_operation,
                       uint64_t max_total_text_bytes)
{
    std::vector<MetadataEditingOperation> request_operations;
    request_operations.reserve(operations.size());
    for (const PyMetadataEditingOperation& source_operation : operations) {
        MetadataEditingOperation operation;
        operation.kind  = source_operation.kind;
        operation.field = metadata_creation_field_view(source_operation.field);
        operation.occurrence = source_operation.occurrence;
        request_operations.push_back(operation);
    }

    MetadataEditingRequest request;
    request.operations                          = request_operations;
    request.limits.max_operations               = max_operations;
    request.limits.max_text_bytes_per_operation = max_text_bytes_per_operation;
    request.limits.max_total_text_bytes         = max_total_text_bytes;

    MetaStore edited;
    MetadataEditingResult result;
    {
        nb::gil_scoped_release gil_release;
        result = edit_metadata(source->store, request, &edited);
    }
    if (result.status != MetadataEditingStatus::Ok) {
        std::string message = "metadata editing failed: ";
        message += metadata_editing_status_name(result.status);
        if (result.failed_operation_index
            != kInvalidMetadataEditingOperationIndex) {
            message += " at operation ";
            message += std::to_string(result.failed_operation_index);
        }
        throw std::invalid_argument(message);
    }

    auto document                        = std::make_shared<PyDocument>();
    document->store                      = std::move(edited);
    document->result.xmp.entries_decoded = active_xmp_entry_count(
        document->store);
    return document;
}

static std::shared_ptr<PyDocument>
translate_creation_dates_document(
    std::shared_ptr<PyDocument> source,
    MetadataDateTranslationSourceMode source_mode,
    MetadataDateTranslationConflictPolicy conflict_policy,
    bool create_date_to_exif_digitized,
    bool create_date_to_iptc_digital_creation,
    bool date_created_to_iptc_created, bool date_time_original_to_exif_original,
    uint32_t max_added_entries, uint32_t max_operations)
{
    MetadataDateTranslationOptions options;
    options.source_mode                   = source_mode;
    options.conflict_policy               = conflict_policy;
    options.create_date_to_exif_digitized = create_date_to_exif_digitized;
    options.create_date_to_iptc_digital_creation
        = create_date_to_iptc_digital_creation;
    options.date_created_to_iptc_created = date_created_to_iptc_created;
    options.date_time_original_to_exif_original
        = date_time_original_to_exif_original;
    options.max_added_entries = max_added_entries;
    options.max_operations    = max_operations;

    MetaStore translated;
    MetadataDateTranslationResult result;
    {
        nb::gil_scoped_release gil_release;
        result = translate_xmp_creation_dates(source->store, options,
                                              &translated);
    }
    if (result.status != MetadataDateTranslationStatus::Ok) {
        std::string message = "metadata date translation failed: ";
        message += metadata_date_translation_status_name(result.status);
        if (result.failed_mapping != MetadataDateTranslationMapping::None) {
            message += " for ";
            message += metadata_date_translation_mapping_name(
                result.failed_mapping);
        }
        if (result.failed_source_entry != kInvalidEntryId) {
            message += " at source entry ";
            message += std::to_string(result.failed_source_entry);
        }
        throw std::invalid_argument(message);
    }

    auto document                        = std::make_shared<PyDocument>();
    document->store                      = std::move(translated);
    document->result.xmp.entries_decoded = active_xmp_entry_count(
        document->store);
    return document;
}

static std::shared_ptr<PyDocument>
translate_technical_metadata_document(
    std::shared_ptr<PyDocument> source,
    MetadataTechnicalTranslationSourceMode source_mode,
    MetadataTechnicalTranslationConflictPolicy conflict_policy,
    bool modify_date_to_exif_datetime, bool make_to_exif_make,
    bool model_to_exif_model, bool creator_tool_to_exif_software,
    uint32_t max_added_entries, uint32_t max_operations,
    uint32_t max_text_bytes_per_property, uint64_t max_total_text_bytes)
{
    MetadataTechnicalTranslationOptions options;
    options.source_mode                   = source_mode;
    options.conflict_policy               = conflict_policy;
    options.modify_date_to_exif_datetime  = modify_date_to_exif_datetime;
    options.make_to_exif_make             = make_to_exif_make;
    options.model_to_exif_model           = model_to_exif_model;
    options.creator_tool_to_exif_software = creator_tool_to_exif_software;
    options.max_added_entries             = max_added_entries;
    options.max_operations                = max_operations;
    options.max_text_bytes_per_property   = max_text_bytes_per_property;
    options.max_total_text_bytes          = max_total_text_bytes;

    MetaStore translated;
    MetadataTechnicalTranslationResult result;
    {
        nb::gil_scoped_release gil_release;
        result = translate_xmp_technical_metadata(source->store, options,
                                                  &translated);
    }
    if (result.status != MetadataTechnicalTranslationStatus::Ok) {
        std::string message = "metadata technical translation failed: ";
        message += metadata_technical_translation_status_name(result.status);
        if (result.failed_mapping
            != MetadataTechnicalTranslationMapping::None) {
            message += " for ";
            message += metadata_technical_translation_mapping_name(
                result.failed_mapping);
        }
        if (result.failed_source_entry != kInvalidEntryId) {
            message += " at source entry ";
            message += std::to_string(result.failed_source_entry);
        }
        throw std::invalid_argument(message);
    }

    auto document                        = std::make_shared<PyDocument>();
    document->store                      = std::move(translated);
    document->result.xmp.entries_decoded = active_xmp_entry_count(
        document->store);
    return document;
}

static std::shared_ptr<PyDocument>
translate_capture_metadata_document(
    std::shared_ptr<PyDocument> source,
    MetadataCaptureTranslationSourceMode source_mode,
    MetadataCaptureTranslationConflictPolicy conflict_policy,
    bool exposure_time_to_exif, bool f_number_to_exif, bool iso_to_exif,
    bool focal_length_to_exif, bool exposure_compensation_to_exif,
    uint32_t max_added_entries, uint32_t max_operations,
    uint32_t max_text_bytes_per_property, uint64_t max_total_text_bytes)
{
    MetadataCaptureTranslationOptions options;
    options.source_mode                   = source_mode;
    options.conflict_policy               = conflict_policy;
    options.exposure_time_to_exif         = exposure_time_to_exif;
    options.f_number_to_exif              = f_number_to_exif;
    options.iso_to_exif                   = iso_to_exif;
    options.focal_length_to_exif          = focal_length_to_exif;
    options.exposure_compensation_to_exif = exposure_compensation_to_exif;
    options.max_added_entries             = max_added_entries;
    options.max_operations                = max_operations;
    options.max_text_bytes_per_property   = max_text_bytes_per_property;
    options.max_total_text_bytes          = max_total_text_bytes;

    MetaStore translated;
    MetadataCaptureTranslationResult result;
    {
        nb::gil_scoped_release gil_release;
        result = translate_xmp_capture_metadata(source->store, options,
                                                &translated);
    }
    if (result.status != MetadataCaptureTranslationStatus::Ok) {
        std::string message = "metadata capture translation failed: ";
        message += metadata_capture_translation_status_name(result.status);
        if (result.failed_mapping != MetadataCaptureTranslationMapping::None) {
            message += " for ";
            message += metadata_capture_translation_mapping_name(
                result.failed_mapping);
        }
        if (result.failed_source_entry != kInvalidEntryId) {
            message += " at source entry ";
            message += std::to_string(result.failed_source_entry);
        }
        throw std::invalid_argument(message);
    }

    auto document                        = std::make_shared<PyDocument>();
    document->store                      = std::move(translated);
    document->result.xmp.entries_decoded = active_xmp_entry_count(
        document->store);
    return document;
}

static std::shared_ptr<PyDocument>
translate_image_geometry_document(
    std::shared_ptr<PyDocument> source,
    const TransferTargetImageSpec& target_image_spec,
    MetadataGeometryTranslationSourceMode source_mode,
    MetadataGeometryTranslationConflictPolicy conflict_policy,
    bool orientation_to_exif, bool dimensions_to_exif,
    uint32_t max_added_entries, uint32_t max_operations,
    uint32_t max_text_bytes_per_property, uint64_t max_total_text_bytes)
{
    MetadataGeometryTranslationOptions options;
    options.source_mode                 = source_mode;
    options.conflict_policy             = conflict_policy;
    options.orientation_to_exif         = orientation_to_exif;
    options.dimensions_to_exif          = dimensions_to_exif;
    options.max_added_entries           = max_added_entries;
    options.max_operations              = max_operations;
    options.max_text_bytes_per_property = max_text_bytes_per_property;
    options.max_total_text_bytes        = max_total_text_bytes;

    MetaStore translated;
    MetadataGeometryTranslationResult result;
    {
        nb::gil_scoped_release gil_release;
        result = translate_xmp_image_geometry(source->store, target_image_spec,
                                              options, &translated);
    }
    if (result.status != MetadataGeometryTranslationStatus::Ok) {
        std::string message = "metadata image-geometry translation failed: ";
        message += metadata_geometry_translation_status_name(result.status);
        if (result.failed_mapping != MetadataGeometryTranslationMapping::None) {
            message += " for ";
            message += metadata_geometry_translation_mapping_name(
                result.failed_mapping);
        }
        if (result.failed_source_entry != kInvalidEntryId) {
            message += " at source entry ";
            message += std::to_string(result.failed_source_entry);
        }
        throw std::invalid_argument(message);
    }

    auto document                        = std::make_shared<PyDocument>();
    document->store                      = std::move(translated);
    document->result.xmp.entries_decoded = active_xmp_entry_count(
        document->store);
    return document;
}

static std::shared_ptr<PyDocument>
translate_descriptive_metadata_document(
    std::shared_ptr<PyDocument> source,
    MetadataDescriptiveTranslationSourceMode source_mode,
    MetadataDescriptiveTranslationConflictPolicy conflict_policy,
    bool title_to_iptc_object_name, bool description_to_iptc_caption,
    bool creators_to_iptc_bylines, bool keywords_to_iptc_keywords,
    bool copyright_to_iptc_copyright, bool credit_to_iptc_credit,
    bool source_to_iptc_source, uint32_t max_source_properties,
    uint32_t max_added_entries, uint32_t max_operations,
    uint64_t max_total_text_bytes)
{
    MetadataDescriptiveTranslationOptions options;
    options.source_mode                 = source_mode;
    options.conflict_policy             = conflict_policy;
    options.title_to_iptc_object_name   = title_to_iptc_object_name;
    options.description_to_iptc_caption = description_to_iptc_caption;
    options.creators_to_iptc_bylines    = creators_to_iptc_bylines;
    options.keywords_to_iptc_keywords   = keywords_to_iptc_keywords;
    options.copyright_to_iptc_copyright = copyright_to_iptc_copyright;
    options.credit_to_iptc_credit       = credit_to_iptc_credit;
    options.source_to_iptc_source       = source_to_iptc_source;
    options.max_source_properties       = max_source_properties;
    options.max_added_entries           = max_added_entries;
    options.max_operations              = max_operations;
    options.max_total_text_bytes        = max_total_text_bytes;

    MetaStore translated;
    MetadataDescriptiveTranslationResult result;
    {
        nb::gil_scoped_release gil_release;
        result = translate_xmp_descriptive_metadata(source->store, options,
                                                    &translated);
    }
    if (result.status != MetadataDescriptiveTranslationStatus::Ok) {
        std::string message = "metadata descriptive translation failed: ";
        message += metadata_descriptive_translation_status_name(result.status);
        if (result.failed_mapping
            != MetadataDescriptiveTranslationMapping::None) {
            message += " for ";
            message += metadata_descriptive_translation_mapping_name(
                result.failed_mapping);
        }
        if (result.failed_source_entry != kInvalidEntryId) {
            message += " at source entry ";
            message += std::to_string(result.failed_source_entry);
        }
        throw std::invalid_argument(message);
    }

    auto document                        = std::make_shared<PyDocument>();
    document->store                      = std::move(translated);
    document->result.xmp.entries_decoded = active_xmp_entry_count(
        document->store);
    return document;
}

static std::string
document_compatibility_dump(std::shared_ptr<PyDocument> d,
                            ExportNameStyle style, ExportNamePolicy name_policy,
                            bool include_values, bool include_origins,
                            bool include_flags, uint32_t max_value_bytes)
{
    return metadata_compatibility_dump_from_store(d->store, style, name_policy,
                                                  include_values,
                                                  include_origins,
                                                  include_flags,
                                                  max_value_bytes);
}

static nb::dict
snapshot_phaseone_raw_geometry(const TransferSourceSnapshot& snapshot)
{
    return phaseone_raw_geometry_to_python(snapshot.store);
}

static nb::dict
snapshot_phaseone_raw_processing(const TransferSourceSnapshot& snapshot)
{
    return phaseone_raw_processing_to_python(snapshot.store);
}

static nb::dict
snapshot_vendor_raw_processing(const TransferSourceSnapshot& snapshot,
                               VendorRawProcessingFamily family)
{
    return vendor_raw_processing_to_python(snapshot.store, family);
}

static nb::dict
snapshot_metadata_query(const TransferSourceSnapshot& snapshot,
                        MetadataQueryKind kind)
{
    return metadata_query_to_python(snapshot.store, kind);
}

static nb::dict
snapshot_fuzzy_search(const TransferSourceSnapshot& snapshot,
                      const std::string& query, uint8_t minimum_score,
                      uint32_t max_results)
{
    return metadata_fuzzy_search_to_python(snapshot.store, query, minimum_score,
                                           max_results);
}

static nb::dict
snapshot_query_crop_metadata(const TransferSourceSnapshot& snapshot)
{
    return metadata_query_to_python(snapshot.store, MetadataQueryKind::Crop);
}

static nb::dict
snapshot_query_exposure_gain_metadata(const TransferSourceSnapshot& snapshot)
{
    return metadata_query_to_python(snapshot.store,
                                    MetadataQueryKind::ExposureGain);
}

static nb::dict
snapshot_query_white_balance_metadata(const TransferSourceSnapshot& snapshot)
{
    return metadata_query_to_python(snapshot.store,
                                    MetadataQueryKind::WhiteBalance);
}

static nb::dict
snapshot_query_color_metadata(const TransferSourceSnapshot& snapshot)
{
    return metadata_query_to_python(snapshot.store, MetadataQueryKind::Color);
}

static nb::dict
snapshot_query_lens_correction_metadata(const TransferSourceSnapshot& snapshot)
{
    return metadata_query_to_python(snapshot.store,
                                    MetadataQueryKind::LensCorrection);
}

static nb::dict
snapshot_query_orientation_metadata(const TransferSourceSnapshot& snapshot)
{
    return metadata_query_to_python(snapshot.store,
                                    MetadataQueryKind::Orientation);
}

static nb::dict
snapshot_query_raw_processing_metadata(const TransferSourceSnapshot& snapshot)
{
    return metadata_query_to_python(snapshot.store,
                                    MetadataQueryKind::RawProcessing);
}

static nb::dict
snapshot_interpret_metadata(const TransferSourceSnapshot& snapshot)
{
    return metadata_interpretation_to_python(snapshot.store);
}

static nb::dict
snapshot_interpret_metadata_query(const TransferSourceSnapshot& snapshot,
                                  MetadataQueryKind kind)
{
    return metadata_interpretation_query_to_python(snapshot.store, kind);
}

static nb::dict
snapshot_resolve_metadata_concepts(const TransferSourceSnapshot& snapshot)
{
    return metadata_concepts_to_python(snapshot.store);
}

static nb::dict
snapshot_resolve_metadata_concepts_with_raw_descriptor(
    const TransferSourceSnapshot& snapshot,
    const MetadataRawDataDescriptor& raw_descriptor)
{
    return metadata_concepts_to_python(snapshot.store, raw_descriptor);
}

static nb::dict
snapshot_resolve_metadata_concept(const TransferSourceSnapshot& snapshot,
                                  MetadataConceptKind kind)
{
    return metadata_concept_to_python(snapshot.store, kind);
}

static nb::dict
snapshot_resolve_metadata_concept_with_raw_descriptor(
    const TransferSourceSnapshot& snapshot, MetadataConceptKind kind,
    const MetadataRawDataDescriptor& raw_descriptor)
{
    return metadata_concept_to_python(snapshot.store, kind, raw_descriptor);
}

static nb::dict
snapshot_transfer_safety_audit(const TransferSourceSnapshot& snapshot,
                               TransferSafetyMode safety)
{
    const TransferSafetyAudit audit
        = transfer_safety_audit_from_store(snapshot.store, safety);
    return transfer_safety_audit_to_python(audit);
}

static nb::dict
snapshot_makernote_transfer_audit(const TransferSourceSnapshot& snapshot)
{
    return makernote_transfer_audit_to_python(
        makernote_transfer_audit_from_store(snapshot.store));
}

static nb::dict
snapshot_makernote_layout_transfer_audit(const TransferSourceSnapshot& snapshot)
{
    return makernote_layout_transfer_audit_to_python(
        makernote_layout_transfer_audit_from_store(snapshot.store));
}

static nb::dict
snapshot_transfer_concept_diagnostics(const TransferSourceSnapshot& snapshot,
                                      TransferSafetyMode safety)
{
    const TransferConceptDiagnostics diagnostics
        = transfer_concept_diagnostics_from_store(snapshot.store, safety);
    return transfer_concept_diagnostics_to_python(diagnostics);
}

static nb::dict
snapshot_transfer_concept_diagnostics_with_raw_descriptor(
    const TransferSourceSnapshot& snapshot, TransferSafetyMode safety,
    const MetadataRawDataDescriptor& raw_descriptor)
{
    const TransferConceptDiagnostics diagnostics
        = transfer_concept_diagnostics_from_store(snapshot.store, safety,
                                                  raw_descriptor);
    return transfer_concept_diagnostics_to_python(diagnostics);
}

static nb::dict
snapshot_raw_carrier_passthrough_audit(const TransferSourceSnapshot& snapshot,
                                       TransferTargetFormat target_format,
                                       TransferSafetyMode safety,
                                       TransferPolicyAction makernote_policy,
                                       TransferPolicyAction jumbf_policy,
                                       TransferPolicyAction c2pa_policy,
                                       bool require_decoded_entry_links)
{
    TransferRawCarrierPassthroughAuditOptions options;
    options.target_format               = target_format;
    options.profile.safety              = safety;
    options.profile.makernote           = makernote_policy;
    options.profile.jumbf               = jumbf_policy;
    options.profile.c2pa                = c2pa_policy;
    options.require_decoded_entry_links = require_decoded_entry_links;
    const TransferRawCarrierPassthroughAudit audit
        = raw_carrier_passthrough_audit_from_snapshot(snapshot, options);
    return raw_carrier_passthrough_audit_to_python(audit);
}

static nb::dict
document_phaseone_raw_geometry(std::shared_ptr<PyDocument> d)
{
    return phaseone_raw_geometry_to_python(d->store);
}

static nb::dict
document_phaseone_raw_processing(std::shared_ptr<PyDocument> d)
{
    return phaseone_raw_processing_to_python(d->store);
}

static nb::dict
document_vendor_raw_processing(std::shared_ptr<PyDocument> d,
                               VendorRawProcessingFamily family)
{
    return vendor_raw_processing_to_python(d->store, family);
}

static nb::dict
document_metadata_query(std::shared_ptr<PyDocument> d, MetadataQueryKind kind)
{
    return metadata_query_to_python(d->store, kind);
}

static nb::dict
document_fuzzy_search(std::shared_ptr<PyDocument> d, const std::string& query,
                      uint8_t minimum_score, uint32_t max_results)
{
    return metadata_fuzzy_search_to_python(d->store, query, minimum_score,
                                           max_results);
}

static nb::dict
document_query_crop_metadata(std::shared_ptr<PyDocument> d)
{
    return metadata_query_to_python(d->store, MetadataQueryKind::Crop);
}

static nb::dict
document_query_exposure_gain_metadata(std::shared_ptr<PyDocument> d)
{
    return metadata_query_to_python(d->store, MetadataQueryKind::ExposureGain);
}

static nb::dict
document_query_white_balance_metadata(std::shared_ptr<PyDocument> d)
{
    return metadata_query_to_python(d->store, MetadataQueryKind::WhiteBalance);
}

static nb::dict
document_query_color_metadata(std::shared_ptr<PyDocument> d)
{
    return metadata_query_to_python(d->store, MetadataQueryKind::Color);
}

static nb::dict
document_query_lens_correction_metadata(std::shared_ptr<PyDocument> d)
{
    return metadata_query_to_python(d->store,
                                    MetadataQueryKind::LensCorrection);
}

static nb::dict
document_query_orientation_metadata(std::shared_ptr<PyDocument> d)
{
    return metadata_query_to_python(d->store, MetadataQueryKind::Orientation);
}

static nb::dict
document_query_raw_processing_metadata(std::shared_ptr<PyDocument> d)
{
    return metadata_query_to_python(d->store, MetadataQueryKind::RawProcessing);
}

static nb::dict
document_interpret_metadata(std::shared_ptr<PyDocument> d)
{
    return metadata_interpretation_to_python(d->store);
}

static nb::dict
document_interpret_metadata_query(std::shared_ptr<PyDocument> d,
                                  MetadataQueryKind kind)
{
    return metadata_interpretation_query_to_python(d->store, kind);
}

static nb::dict
document_resolve_metadata_concepts(std::shared_ptr<PyDocument> d)
{
    return metadata_concepts_to_python(d->store);
}

static nb::dict
document_resolve_metadata_concepts_with_raw_descriptor(
    std::shared_ptr<PyDocument> d,
    const MetadataRawDataDescriptor& raw_descriptor)
{
    return metadata_concepts_to_python(d->store, raw_descriptor);
}

static nb::dict
document_resolve_metadata_concept(std::shared_ptr<PyDocument> d,
                                  MetadataConceptKind kind)
{
    return metadata_concept_to_python(d->store, kind);
}

static nb::dict
document_resolve_metadata_concept_with_raw_descriptor(
    std::shared_ptr<PyDocument> d, MetadataConceptKind kind,
    const MetadataRawDataDescriptor& raw_descriptor)
{
    return metadata_concept_to_python(d->store, kind, raw_descriptor);
}

static nb::dict
document_transfer_safety_audit(std::shared_ptr<PyDocument> d,
                               TransferSafetyMode safety)
{
    const TransferSafetyAudit audit = transfer_safety_audit_from_store(d->store,
                                                                       safety);
    return transfer_safety_audit_to_python(audit);
}

static nb::dict
document_makernote_transfer_audit(std::shared_ptr<PyDocument> d)
{
    return makernote_transfer_audit_to_python(
        makernote_transfer_audit_from_store(d->store));
}

static nb::dict
document_makernote_layout_transfer_audit(std::shared_ptr<PyDocument> d)
{
    return makernote_layout_transfer_audit_to_python(
        makernote_layout_transfer_audit_from_store(d->store));
}

static nb::dict
document_transfer_concept_diagnostics(std::shared_ptr<PyDocument> d,
                                      TransferSafetyMode safety)
{
    const TransferConceptDiagnostics diagnostics
        = transfer_concept_diagnostics_from_store(d->store, safety);
    return transfer_concept_diagnostics_to_python(diagnostics);
}

static nb::dict
document_transfer_concept_diagnostics_with_raw_descriptor(
    std::shared_ptr<PyDocument> d, TransferSafetyMode safety,
    const MetadataRawDataDescriptor& raw_descriptor)
{
    const TransferConceptDiagnostics diagnostics
        = transfer_concept_diagnostics_from_store(d->store, safety,
                                                  raw_descriptor);
    return transfer_concept_diagnostics_to_python(diagnostics);
}

struct PyEntry final {
    std::shared_ptr<PyDocument> doc;
    EntryId id = kInvalidEntryId;
};

static std::shared_ptr<PyDocument>
read_document(const std::string& path, bool include_pointer_tags,
              bool decode_makernote, bool decompress, bool include_xmp_sidecar,
              bool verify_c2pa, C2paVerifyBackend verify_backend,
              bool verify_require_trusted_chain,
              bool verify_require_resolved_references, uint64_t max_file_bytes,
              const OpenMetaResourcePolicy* policy_ptr)
{
    auto doc  = std::make_shared<PyDocument>();
    doc->path = path;

    OpenMetaResourcePolicy policy;
    policy.max_file_bytes = max_file_bytes;
    if (policy_ptr) {
        policy = *policy_ptr;
        if (max_file_bytes != 0U) {
            policy.max_file_bytes = max_file_bytes;
        }
    }

    SimpleMetaDecodeOptions decode_options;
    apply_resource_policy(policy, &decode_options.exif,
                          &decode_options.payload);
    apply_resource_policy(policy, &decode_options.xmp, &decode_options.exr,
                          &decode_options.jumbf, &decode_options.icc,
                          &decode_options.iptc, &decode_options.photoshop_irb);
    decode_options.xmp.malformed_mode = XmpDecodeMalformedMode::OutputTruncated;
    decode_options.exif.include_pointer_tags       = include_pointer_tags;
    decode_options.exif.decode_makernote           = decode_makernote;
    decode_options.exif.decode_embedded_containers = true;
    decode_options.payload.decompress              = decompress;
    decode_options.jumbf.verify_c2pa               = verify_c2pa;
    decode_options.jumbf.verify_backend            = verify_backend;
    decode_options.jumbf.verify_require_trusted_chain
        = verify_require_trusted_chain;
    decode_options.jumbf.verify_require_resolved_references
        = verify_require_resolved_references;

    // Release the GIL while performing file I/O and metadata decoding so callers
    // (and internal comparison tools) can read in parallel from multiple Python
    // threads. All work below this point is pure C/C++ and does not touch the
    // Python C API.
    nb::gil_scoped_release gil_release;

    const MappedFileStatus st = doc->file.open(path.c_str(),
                                               policy.max_file_bytes);
    if (st != MappedFileStatus::Ok) {
        if (st == MappedFileStatus::TooLarge) {
            throw std::runtime_error("file too large");
        }
        if (st == MappedFileStatus::OpenFailed) {
            throw std::runtime_error("failed to open file");
        }
        if (st == MappedFileStatus::StatFailed) {
            throw std::runtime_error("failed to stat file");
        }
        throw std::runtime_error("failed to map file");
    }
    doc->file_bytes = doc->file.bytes();

    doc->blocks.resize(128);
    doc->ifds.resize(256);
    doc->payload.resize(1024 * 1024);
    doc->payload_parts.resize(16384);

    auto merge_xmp_status = [](XmpDecodeStatus* out,
                               XmpDecodeStatus in) noexcept {
        if (!out) {
            return;
        }
        if (*out == XmpDecodeStatus::LimitExceeded) {
            return;
        }
        if (in == XmpDecodeStatus::LimitExceeded) {
            *out = in;
            return;
        }
        if (*out == XmpDecodeStatus::Malformed) {
            return;
        }
        if (in == XmpDecodeStatus::Malformed) {
            *out = in;
            return;
        }
        if (*out == XmpDecodeStatus::OutputTruncated) {
            return;
        }
        if (in == XmpDecodeStatus::OutputTruncated) {
            *out = in;
            return;
        }
        if (*out == XmpDecodeStatus::Ok) {
            return;
        }
        if (in == XmpDecodeStatus::Ok) {
            *out = in;
            return;
        }
    };

    for (;;) {
        doc->store  = MetaStore();
        doc->result = simple_meta_read(
            doc->file_bytes, doc->store,
            std::span<ContainerBlockRef>(doc->blocks.data(), doc->blocks.size()),
            std::span<ExifIfdRef>(doc->ifds.data(), doc->ifds.size()),
            std::span<std::byte>(doc->payload.data(), doc->payload.size()),
            std::span<uint32_t>(doc->payload_parts.data(),
                                doc->payload_parts.size()),
            decode_options);

        if (doc->result.scan.status == ScanStatus::OutputTruncated
            && doc->result.scan.needed > doc->blocks.size()) {
            doc->blocks.resize(doc->result.scan.needed);
            continue;
        }
        if (doc->result.payload.status == PayloadStatus::OutputTruncated
            && doc->result.payload.needed > doc->payload.size()) {
            doc->payload.resize(
                static_cast<size_t>(doc->result.payload.needed));
            continue;
        }
        break;
    }

    if (include_xmp_sidecar) {
        std::string sidecar_a;
        std::string sidecar_b;
        {
            const std::string s(path);
            sidecar_b = s + ".xmp";

            const size_t sep = s.find_last_of("/\\");
            const size_t dot = s.find_last_of('.');
            if (dot != std::string::npos
                && (sep == std::string::npos || dot > sep)) {
                sidecar_a = s.substr(0, dot) + ".xmp";
            } else {
                sidecar_a = sidecar_b;
            }
            if (sidecar_a == sidecar_b) {
                sidecar_b.clear();
            }
        }

        auto try_read = [&](const std::string& p) -> bool {
            if (p.empty()) {
                return false;
            }
            std::FILE* f = std::fopen(p.c_str(), "rb");
            if (!f) {
                return false;
            }
            std::fclose(f);
            return true;
        };

        const std::string* candidates[2] = { &sidecar_a, &sidecar_b };
        for (int i = 0; i < 2; ++i) {
            const std::string& sp = *candidates[i];
            if (sp.empty() || !try_read(sp)) {
                continue;
            }
            const std::vector<std::byte> xmp_bytes
                = read_file_bytes(sp.c_str(), policy.max_file_bytes);
            const XmpDecodeResult one = decode_xmp_packet(xmp_bytes, doc->store,
                                                          EntryFlags::None,
                                                          decode_options.xmp);
            merge_xmp_status(&doc->result.xmp.status, one.status);
            doc->result.xmp.entries_decoded += one.entries_decoded;
        }
    }

    doc->blocks.resize(doc->result.scan.written);
    const uint32_t ifds_written = doc->result.exif.ifds_written;
    if (ifds_written < doc->ifds.size()) {
        doc->ifds.resize(ifds_written);
    }

    doc->store.finalize();
    return doc;
}

}  // namespace openmeta

NB_MODULE(_openmeta, m)
{
    using namespace openmeta;

    m.doc()               = "OpenMeta metadata reading bindings (nanobind).";
    m.attr("__version__") = OPENMETA_VERSION_STRING;
    m.attr("INTEROP_EXPORT_CONTRACT_VERSION") = nb::int_(
        kInteropExportContractVersion);
    m.attr("FLAT_HOST_EXPORT_CONTRACT_VERSION") = nb::int_(
        kFlatHostExportContractVersion);
    m.attr("COMPATIBILITY_DUMP_CONTRACT_VERSION") = nb::int_(
        kCompatibilityDumpContractVersion);
    m.attr("METADATA_CREATION_CONTRACT_VERSION") = nb::int_(
        kMetadataCreationContractVersion);
    m.attr("METADATA_EDITING_CONTRACT_VERSION") = nb::int_(
        kMetadataEditingContractVersion);
    m.attr("METADATA_DATE_TRANSLATION_CONTRACT_VERSION") = nb::int_(
        kMetadataDateTranslationContractVersion);
    m.attr("METADATA_TECHNICAL_TRANSLATION_CONTRACT_VERSION") = nb::int_(
        kMetadataTechnicalTranslationContractVersion);
    m.attr("METADATA_CAPTURE_TRANSLATION_CONTRACT_VERSION") = nb::int_(
        kMetadataCaptureTranslationContractVersion);
    m.attr("METADATA_GEOMETRY_TRANSLATION_CONTRACT_VERSION") = nb::int_(
        kMetadataGeometryTranslationContractVersion);
    m.attr("METADATA_DESCRIPTIVE_TRANSLATION_CONTRACT_VERSION") = nb::int_(
        kMetadataDescriptiveTranslationContractVersion);

    nb::enum_<ScanStatus>(m, "ScanStatus")
        .value("Ok", ScanStatus::Ok)
        .value("OutputTruncated", ScanStatus::OutputTruncated)
        .value("Unsupported", ScanStatus::Unsupported)
        .value("Malformed", ScanStatus::Malformed);

    nb::enum_<PayloadStatus>(m, "PayloadStatus")
        .value("Ok", PayloadStatus::Ok)
        .value("OutputTruncated", PayloadStatus::OutputTruncated)
        .value("Unsupported", PayloadStatus::Unsupported)
        .value("Malformed", PayloadStatus::Malformed)
        .value("LimitExceeded", PayloadStatus::LimitExceeded);

    nb::enum_<ExifDecodeStatus>(m, "ExifDecodeStatus")
        .value("Ok", ExifDecodeStatus::Ok)
        .value("OutputTruncated", ExifDecodeStatus::OutputTruncated)
        .value("Unsupported", ExifDecodeStatus::Unsupported)
        .value("Malformed", ExifDecodeStatus::Malformed)
        .value("LimitExceeded", ExifDecodeStatus::LimitExceeded);

    nb::enum_<PhaseOneRawGeometryStatus>(m, "PhaseOneRawGeometryStatus")
        .value("Ok", PhaseOneRawGeometryStatus::Ok)
        .value("MissingField", PhaseOneRawGeometryStatus::MissingField)
        .value("InvalidValue", PhaseOneRawGeometryStatus::InvalidValue)
        .value("OutOfBounds", PhaseOneRawGeometryStatus::OutOfBounds);

    nb::enum_<PhaseOneRawProcessingStatus>(m, "PhaseOneRawProcessingStatus")
        .value("Ok", PhaseOneRawProcessingStatus::Ok)
        .value("MissingField", PhaseOneRawProcessingStatus::MissingField)
        .value("InvalidValue", PhaseOneRawProcessingStatus::InvalidValue)
        .value("Partial", PhaseOneRawProcessingStatus::Partial);

    nb::enum_<VendorRawProcessingFamily>(m, "VendorRawProcessingFamily")
        .value("Sony", VendorRawProcessingFamily::Sony)
        .value("Canon", VendorRawProcessingFamily::Canon)
        .value("Nikon", VendorRawProcessingFamily::Nikon)
        .value("Fujifilm", VendorRawProcessingFamily::Fujifilm)
        .value("Pentax", VendorRawProcessingFamily::Pentax)
        .value("Panasonic", VendorRawProcessingFamily::Panasonic)
        .value("Olympus", VendorRawProcessingFamily::Olympus)
        .value("Kodak", VendorRawProcessingFamily::Kodak)
        .value("Minolta", VendorRawProcessingFamily::Minolta)
        .value("Sigma", VendorRawProcessingFamily::Sigma)
        .value("Samsung", VendorRawProcessingFamily::Samsung)
        .value("Ricoh", VendorRawProcessingFamily::Ricoh)
        .value("Apple", VendorRawProcessingFamily::Apple)
        .value("Dji", VendorRawProcessingFamily::Dji)
        .value("Google", VendorRawProcessingFamily::Google)
        .value("Flir", VendorRawProcessingFamily::Flir)
        .value("Casio", VendorRawProcessingFamily::Casio)
        .value("Sanyo", VendorRawProcessingFamily::Sanyo)
        .value("KyoceraRaw", VendorRawProcessingFamily::KyoceraRaw)
        .value("Reconyx", VendorRawProcessingFamily::Reconyx)
        .value("Hp", VendorRawProcessingFamily::Hp)
        .value("Jvc", VendorRawProcessingFamily::Jvc)
        .value("Ge", VendorRawProcessingFamily::Ge)
        .value("Motorola", VendorRawProcessingFamily::Motorola)
        .value("Nintendo", VendorRawProcessingFamily::Nintendo)
        .value("Microsoft", VendorRawProcessingFamily::Microsoft);

    nb::enum_<VendorRawProcessingGroup>(m, "VendorRawProcessingGroup")
        .value("None_", VendorRawProcessingGroup::None)
        .value("Color", VendorRawProcessingGroup::Color)
        .value("WhiteBalance", VendorRawProcessingGroup::WhiteBalance)
        .value("Geometry", VendorRawProcessingGroup::Geometry)
        .value("Storage", VendorRawProcessingGroup::Storage)
        .value("LensCorrection", VendorRawProcessingGroup::LensCorrection)
        .value("RawData", VendorRawProcessingGroup::RawData)
        .value("Sensor", VendorRawProcessingGroup::Sensor)
        .value("PrivateTable", VendorRawProcessingGroup::PrivateTable)
        .value("Preview", VendorRawProcessingGroup::Preview)
        .value("FaceGeometry", VendorRawProcessingGroup::FaceGeometry)
        .value("Computational", VendorRawProcessingGroup::Computational)
        .value("Thermal", VendorRawProcessingGroup::Thermal)
        .value("Stitch", VendorRawProcessingGroup::Stitch);

    nb::enum_<MetadataQueryKind>(m, "MetadataQueryKind")
        .value("Crop", MetadataQueryKind::Crop)
        .value("ExposureGain", MetadataQueryKind::ExposureGain)
        .value("WhiteBalance", MetadataQueryKind::WhiteBalance)
        .value("Color", MetadataQueryKind::Color)
        .value("LensCorrection", MetadataQueryKind::LensCorrection)
        .value("Orientation", MetadataQueryKind::Orientation)
        .value("RawProcessing", MetadataQueryKind::RawProcessing)
        .value("Descriptive", MetadataQueryKind::Descriptive);

    nb::enum_<MetadataQuerySemanticKind>(m, "MetadataQuerySemanticKind")
        .value("Unknown", MetadataQuerySemanticKind::Unknown)
        .value("Crop", MetadataQuerySemanticKind::Crop)
        .value("Border", MetadataQuerySemanticKind::Border)
        .value("ActiveArea", MetadataQuerySemanticKind::ActiveArea)
        .value("Exposure", MetadataQuerySemanticKind::Exposure)
        .value("Gain", MetadataQuerySemanticKind::Gain)
        .value("Color", MetadataQuerySemanticKind::Color)
        .value("ColorProfile", MetadataQuerySemanticKind::ColorProfile)
        .value("WhiteBalance", MetadataQuerySemanticKind::WhiteBalance)
        .value("ColorMatrix", MetadataQuerySemanticKind::ColorMatrix)
        .value("LensCorrection", MetadataQuerySemanticKind::LensCorrection)
        .value("Orientation", MetadataQuerySemanticKind::Orientation)
        .value("ExposureGain", MetadataQuerySemanticKind::ExposureGain)
        .value("BlackLevel", MetadataQuerySemanticKind::BlackLevel)
        .value("WhiteLevel", MetadataQuerySemanticKind::WhiteLevel)
        .value("Linearization", MetadataQuerySemanticKind::Linearization)
        .value("CfaLayout", MetadataQuerySemanticKind::CfaLayout)
        .value("SensorGeometry", MetadataQuerySemanticKind::SensorGeometry)
        .value("RawStorage", MetadataQuerySemanticKind::RawStorage)
        .value("SourceProcessing", MetadataQuerySemanticKind::SourceProcessing)
        .value("ComputationalProcessing",
               MetadataQuerySemanticKind::ComputationalProcessing)
        .value("ThermalProcessing",
               MetadataQuerySemanticKind::ThermalProcessing)
        .value("StitchProcessing", MetadataQuerySemanticKind::StitchProcessing)
        .value("Title", MetadataQuerySemanticKind::Title)
        .value("Description", MetadataQuerySemanticKind::Description)
        .value("Creator", MetadataQuerySemanticKind::Creator)
        .value("Keywords", MetadataQuerySemanticKind::Keywords)
        .value("SourceColorTransform",
               MetadataQuerySemanticKind::SourceColorTransform)
        .value("RawValueCurve", MetadataQuerySemanticKind::RawValueCurve)
        .value("RawLinearityLimit",
               MetadataQuerySemanticKind::RawLinearityLimit)
        .value("RawCalibrationCurve",
               MetadataQuerySemanticKind::RawCalibrationCurve)
        .value("RawCurveControlPoints",
               MetadataQuerySemanticKind::RawCurveControlPoints)
        .value("Rights", MetadataQuerySemanticKind::Rights)
        .value("License", MetadataQuerySemanticKind::License)
        .value("Credit", MetadataQuerySemanticKind::Credit)
        .value("Source", MetadataQuerySemanticKind::Source)
        .value("Contact", MetadataQuerySemanticKind::Contact)
        .value("Event", MetadataQuerySemanticKind::Event)
        .value("Person", MetadataQuerySemanticKind::Person)
        .value("Organization", MetadataQuerySemanticKind::Organization)
        .value("Product", MetadataQuerySemanticKind::Product)
        .value("Artwork", MetadataQuerySemanticKind::Artwork)
        .value("RightsExpression", MetadataQuerySemanticKind::RightsExpression)
        .value("Release", MetadataQuerySemanticKind::Release)
        .value("Editorial", MetadataQuerySemanticKind::Editorial)
        .value("Accessibility", MetadataQuerySemanticKind::Accessibility)
        .value("Taxonomy", MetadataQuerySemanticKind::Taxonomy)
        .value("DocumentIdentity", MetadataQuerySemanticKind::DocumentIdentity)
        .value("Registry", MetadataQuerySemanticKind::Registry)
        .value("ImageRegion", MetadataQuerySemanticKind::ImageRegion)
        .value("DocumentLineage", MetadataQuerySemanticKind::DocumentLineage)
        .value("DocumentHistory", MetadataQuerySemanticKind::DocumentHistory)
        .value("TechnicalImage", MetadataQuerySemanticKind::TechnicalImage)
        .value("Audio", MetadataQuerySemanticKind::Audio)
        .value("Preview", MetadataQuerySemanticKind::Preview);

    nb::enum_<MetadataQueryValueShape>(m, "MetadataQueryValueShape")
        .value("Unknown", MetadataQueryValueShape::Unknown)
        .value("Scalar", MetadataQueryValueShape::Scalar)
        .value("Vec2", MetadataQueryValueShape::Vec2)
        .value("Vec3", MetadataQueryValueShape::Vec3)
        .value("Vec4", MetadataQueryValueShape::Vec4)
        .value("Rect", MetadataQueryValueShape::Rect)
        .value("Matrix3x3", MetadataQueryValueShape::Matrix3x3)
        .value("VectorSet", MetadataQueryValueShape::VectorSet)
        .value("MatrixSet", MetadataQueryValueShape::MatrixSet)
        .value("Table", MetadataQueryValueShape::Table)
        .value("Array", MetadataQueryValueShape::Array)
        .value("Blob", MetadataQueryValueShape::Blob)
        .value("Text", MetadataQueryValueShape::Text);

    nb::enum_<MetadataQueryMatchTerm>(m, "MetadataQueryMatchTerm")
        .value("None_", MetadataQueryMatchTerm::None)
        .value("Crop", MetadataQueryMatchTerm::Crop)
        .value("Border", MetadataQueryMatchTerm::Border)
        .value("Margin", MetadataQueryMatchTerm::Margin)
        .value("Padding", MetadataQueryMatchTerm::Padding)
        .value("ActiveArea", MetadataQueryMatchTerm::ActiveArea)
        .value("Origin", MetadataQueryMatchTerm::Origin)
        .value("Offset", MetadataQueryMatchTerm::Offset)
        .value("Size", MetadataQueryMatchTerm::Size)
        .value("Sensor", MetadataQueryMatchTerm::Sensor)
        .value("Image", MetadataQueryMatchTerm::Image)
        .value("Exposure", MetadataQueryMatchTerm::Exposure)
        .value("Bias", MetadataQueryMatchTerm::Bias)
        .value("Gain", MetadataQueryMatchTerm::Gain)
        .value("WhiteBalance", MetadataQueryMatchTerm::WhiteBalance)
        .value("Color", MetadataQueryMatchTerm::Color)
        .value("Matrix", MetadataQueryMatchTerm::Matrix)
        .value("Calibration", MetadataQueryMatchTerm::Calibration)
        .value("Profile", MetadataQueryMatchTerm::Profile)
        .value("Lens", MetadataQueryMatchTerm::Lens)
        .value("Correction", MetadataQueryMatchTerm::Correction)
        .value("Orientation", MetadataQueryMatchTerm::Orientation)
        .value("BlackLevel", MetadataQueryMatchTerm::BlackLevel)
        .value("WhiteLevel", MetadataQueryMatchTerm::WhiteLevel)
        .value("Linearization", MetadataQueryMatchTerm::Linearization)
        .value("Cfa", MetadataQueryMatchTerm::Cfa)
        .value("Raw", MetadataQueryMatchTerm::Raw)
        .value("Storage", MetadataQueryMatchTerm::Storage);

    nb::enum_<MetadataConceptKind>(m, "MetadataConceptKind")
        .value("Orientation", MetadataConceptKind::Orientation)
        .value("DateTime", MetadataConceptKind::DateTime)
        .value("ColorProfile", MetadataConceptKind::ColorProfile)
        .value("Gps", MetadataConceptKind::Gps)
        .value("Geometry", MetadataConceptKind::Geometry)
        .value("LensCorrection", MetadataConceptKind::LensCorrection)
        .value("RawProcessing", MetadataConceptKind::RawProcessing)
        .value("Exposure", MetadataConceptKind::Exposure)
        .value("ContainerGraph", MetadataConceptKind::ContainerGraph)
        .value("Descriptive", MetadataConceptKind::Descriptive);

    nb::enum_<MetadataConceptSourceFamily>(m, "MetadataConceptSourceFamily")
        .value("Unknown", MetadataConceptSourceFamily::Unknown)
        .value("Exif", MetadataConceptSourceFamily::Exif)
        .value("Xmp", MetadataConceptSourceFamily::Xmp)
        .value("Iptc", MetadataConceptSourceFamily::Iptc)
        .value("Icc", MetadataConceptSourceFamily::Icc)
        .value("PngText", MetadataConceptSourceFamily::PngText)
        .value("InterpretationRecord",
               MetadataConceptSourceFamily::InterpretationRecord);

    nb::enum_<MetadataConceptRole>(m, "MetadataConceptRole")
        .value("Primary", MetadataConceptRole::Primary)
        .value("Orientation", MetadataConceptRole::Orientation)
        .value("Created", MetadataConceptRole::Created)
        .value("Digitized", MetadataConceptRole::Digitized)
        .value("Modified", MetadataConceptRole::Modified)
        .value("MetadataDate", MetadataConceptRole::MetadataDate)
        .value("DateCreated", MetadataConceptRole::DateCreated)
        .value("ColorSpace", MetadataConceptRole::ColorSpace)
        .value("IccProfile", MetadataConceptRole::IccProfile)
        .value("ColorMatrix", MetadataConceptRole::ColorMatrix)
        .value("WhiteBalance", MetadataConceptRole::WhiteBalance)
        .value("Latitude", MetadataConceptRole::Latitude)
        .value("Longitude", MetadataConceptRole::Longitude)
        .value("Altitude", MetadataConceptRole::Altitude)
        .value("Timestamp", MetadataConceptRole::Timestamp)
        .value("Crop", MetadataConceptRole::Crop)
        .value("ActiveArea", MetadataConceptRole::ActiveArea)
        .value("Border", MetadataConceptRole::Border)
        .value("SensorGeometry", MetadataConceptRole::SensorGeometry)
        .value("LensCorrection", MetadataConceptRole::LensCorrection)
        .value("BlackLevel", MetadataConceptRole::BlackLevel)
        .value("WhiteLevel", MetadataConceptRole::WhiteLevel)
        .value("Linearization", MetadataConceptRole::Linearization)
        .value("CfaLayout", MetadataConceptRole::CfaLayout)
        .value("RawStorage", MetadataConceptRole::RawStorage)
        .value("SourceProcessing", MetadataConceptRole::SourceProcessing)
        .value("ComputationalProcessing",
               MetadataConceptRole::ComputationalProcessing)
        .value("ThermalProcessing", MetadataConceptRole::ThermalProcessing)
        .value("StitchProcessing", MetadataConceptRole::StitchProcessing)
        .value("ExposureTime", MetadataConceptRole::ExposureTime)
        .value("Aperture", MetadataConceptRole::Aperture)
        .value("IsoSensitivity", MetadataConceptRole::IsoSensitivity)
        .value("ExposureBias", MetadataConceptRole::ExposureBias)
        .value("ExposureProgram", MetadataConceptRole::ExposureProgram)
        .value("Gain", MetadataConceptRole::Gain)
        .value("RawExposureAdjustment",
               MetadataConceptRole::RawExposureAdjustment)
        .value("SourceColorTransform",
               MetadataConceptRole::SourceColorTransform)
        .value("RawValueCurve", MetadataConceptRole::RawValueCurve)
        .value("RawLinearityLimit", MetadataConceptRole::RawLinearityLimit)
        .value("RawCalibrationCurve", MetadataConceptRole::RawCalibrationCurve)
        .value("RawCurveControlPoints",
               MetadataConceptRole::RawCurveControlPoints)
        .value("ContentBoundMetadata",
               MetadataConceptRole::ContentBoundMetadata)
        .value("MultiImageScene", MetadataConceptRole::MultiImageScene)
        .value("DerivedImageConstruction",
               MetadataConceptRole::DerivedImageConstruction)
        .value("TiledImageConfiguration",
               MetadataConceptRole::TiledImageConfiguration)
        .value("DestinationLatitude", MetadataConceptRole::DestinationLatitude)
        .value("DestinationLongitude",
               MetadataConceptRole::DestinationLongitude)
        .value("LocationShownLatitude",
               MetadataConceptRole::LocationShownLatitude)
        .value("LocationShownLongitude",
               MetadataConceptRole::LocationShownLongitude)
        .value("LocationShownAltitude",
               MetadataConceptRole::LocationShownAltitude)
        .value("LocationCreatedLatitude",
               MetadataConceptRole::LocationCreatedLatitude)
        .value("LocationCreatedLongitude",
               MetadataConceptRole::LocationCreatedLongitude)
        .value("LocationCreatedAltitude",
               MetadataConceptRole::LocationCreatedAltitude)
        .value("Title", MetadataConceptRole::Title)
        .value("Headline", MetadataConceptRole::Headline)
        .value("Description", MetadataConceptRole::Description)
        .value("Creator", MetadataConceptRole::Creator)
        .value("Keywords", MetadataConceptRole::Keywords)
        .value("LocationName", MetadataConceptRole::LocationName)
        .value("Sublocation", MetadataConceptRole::Sublocation)
        .value("City", MetadataConceptRole::City)
        .value("ProvinceState", MetadataConceptRole::ProvinceState)
        .value("CountryName", MetadataConceptRole::CountryName)
        .value("CountryCode", MetadataConceptRole::CountryCode)
        .value("WorldRegion", MetadataConceptRole::WorldRegion)
        .value("LocationIdentifier", MetadataConceptRole::LocationIdentifier)
        .value("CopyrightNotice", MetadataConceptRole::CopyrightNotice)
        .value("CopyrightStatus", MetadataConceptRole::CopyrightStatus)
        .value("RightsUsageTerms", MetadataConceptRole::RightsUsageTerms)
        .value("RightsWebStatement", MetadataConceptRole::RightsWebStatement)
        .value("RightsCertificate", MetadataConceptRole::RightsCertificate)
        .value("RightsMarked", MetadataConceptRole::RightsMarked)
        .value("RightsHolderName", MetadataConceptRole::RightsHolderName)
        .value("RightsHolderIdentifier",
               MetadataConceptRole::RightsHolderIdentifier)
        .value("LicenseIdentifier", MetadataConceptRole::LicenseIdentifier)
        .value("LicenseTermsUrl", MetadataConceptRole::LicenseTermsUrl)
        .value("LicensorName", MetadataConceptRole::LicensorName)
        .value("LicensorIdentifier", MetadataConceptRole::LicensorIdentifier)
        .value("CreditLine", MetadataConceptRole::CreditLine)
        .value("CreditLineRequired", MetadataConceptRole::CreditLineRequired)
        .value("Source", MetadataConceptRole::Source)
        .value("DigitalSourceType", MetadataConceptRole::DigitalSourceType)
        .value("Name", MetadataConceptRole::Name)
        .value("Identifier", MetadataConceptRole::Identifier)
        .value("Address", MetadataConceptRole::Address)
        .value("PostalCode", MetadataConceptRole::PostalCode)
        .value("Email", MetadataConceptRole::Email)
        .value("Telephone", MetadataConceptRole::Telephone)
        .value("Url", MetadataConceptRole::Url)
        .value("Characteristic", MetadataConceptRole::Characteristic)
        .value("Gtin", MetadataConceptRole::Gtin)
        .value("InventoryNumber", MetadataConceptRole::InventoryNumber)
        .value("StylePeriod", MetadataConceptRole::StylePeriod)
        .value("CreatorIdentifier", MetadataConceptRole::CreatorIdentifier)
        .value("Age", MetadataConceptRole::Age)
        .value("ContentDescription", MetadataConceptRole::ContentDescription)
        .value("ContributionDescription",
               MetadataConceptRole::ContributionDescription)
        .value("PhysicalDescription", MetadataConceptRole::PhysicalDescription)
        .value("RightsExpression", MetadataConceptRole::RightsExpression)
        .value("RightsExpressionEncoding",
               MetadataConceptRole::RightsExpressionEncoding)
        .value("RightsExpressionLanguage",
               MetadataConceptRole::RightsExpressionLanguage)
        .value("LicenseStartDate", MetadataConceptRole::LicenseStartDate)
        .value("LicenseEndDate", MetadataConceptRole::LicenseEndDate)
        .value("MediaConstraint", MetadataConceptRole::MediaConstraint)
        .value("RegionConstraint", MetadataConceptRole::RegionConstraint)
        .value("ProductOrServiceConstraint",
               MetadataConceptRole::ProductOrServiceConstraint)
        .value("ImageFileConstraint", MetadataConceptRole::ImageFileConstraint)
        .value("ImageAlterationConstraint",
               MetadataConceptRole::ImageAlterationConstraint)
        .value("OtherLicenseRequirement",
               MetadataConceptRole::OtherLicenseRequirement)
        .value("OtherCondition", MetadataConceptRole::OtherCondition)
        .value("LicenseeTransactionIdentifier",
               MetadataConceptRole::LicenseeTransactionIdentifier)
        .value("LicensorTransactionIdentifier",
               MetadataConceptRole::LicensorTransactionIdentifier)
        .value("LicenseeProjectReference",
               MetadataConceptRole::LicenseeProjectReference)
        .value("LicenseTransactionDate",
               MetadataConceptRole::LicenseTransactionDate)
        .value("ReleaseStatus", MetadataConceptRole::ReleaseStatus)
        .value("ReleaseIdentifier", MetadataConceptRole::ReleaseIdentifier)
        .value("Urgency", MetadataConceptRole::Urgency)
        .value("Category", MetadataConceptRole::Category)
        .value("SupplementalCategory",
               MetadataConceptRole::SupplementalCategory)
        .value("Instructions", MetadataConceptRole::Instructions)
        .value("CreatorTitle", MetadataConceptRole::CreatorTitle)
        .value("TransmissionReference",
               MetadataConceptRole::TransmissionReference)
        .value("CaptionWriter", MetadataConceptRole::CaptionWriter)
        .value("AccessibilityAltText",
               MetadataConceptRole::AccessibilityAltText)
        .value("AccessibilityExtendedDescription",
               MetadataConceptRole::AccessibilityExtendedDescription)
        .value("IntellectualGenre", MetadataConceptRole::IntellectualGenre)
        .value("SceneCode", MetadataConceptRole::SceneCode)
        .value("SubjectCode", MetadataConceptRole::SubjectCode)
        .value("ResourceIdentifier", MetadataConceptRole::ResourceIdentifier)
        .value("DerivedFromIdentifier",
               MetadataConceptRole::DerivedFromIdentifier)
        .value("DocumentIdentifier", MetadataConceptRole::DocumentIdentifier)
        .value("InstanceIdentifier", MetadataConceptRole::InstanceIdentifier)
        .value("OriginalDocumentIdentifier",
               MetadataConceptRole::OriginalDocumentIdentifier)
        .value("RenditionClass", MetadataConceptRole::RenditionClass)
        .value("ImageIdentifier", MetadataConceptRole::ImageIdentifier)
        .value("Notes", MetadataConceptRole::Notes)
        .value("MediaSummaryCode", MetadataConceptRole::MediaSummaryCode)
        .value("ImageDuplicationConstraint",
               MetadataConceptRole::ImageDuplicationConstraint)
        .value("MinorModelAgeDisclosure",
               MetadataConceptRole::MinorModelAgeDisclosure)
        .value("AdultContentWarning", MetadataConceptRole::AdultContentWarning)
        .value("DeliveredImageType", MetadataConceptRole::DeliveredImageType)
        .value("DeliveredFileName", MetadataConceptRole::DeliveredFileName)
        .value("DeliveredFileFormat", MetadataConceptRole::DeliveredFileFormat)
        .value("DeliveredFileSize", MetadataConceptRole::DeliveredFileSize)
        .value("CopyrightRegistrationNumber",
               MetadataConceptRole::CopyrightRegistrationNumber)
        .value("FirstPublicationDate",
               MetadataConceptRole::FirstPublicationDate)
        .value("OtherImageInformation",
               MetadataConceptRole::OtherImageInformation)
        .value("Reuse", MetadataConceptRole::Reuse)
        .value("DataMining", MetadataConceptRole::DataMining)
        .value("OtherLicenseDocument",
               MetadataConceptRole::OtherLicenseDocument)
        .value("OtherLicenseInformation",
               MetadataConceptRole::OtherLicenseInformation)
        .value("VocabularyIdentifier",
               MetadataConceptRole::VocabularyIdentifier)
        .value("TermIdentifier", MetadataConceptRole::TermIdentifier)
        .value("TermName", MetadataConceptRole::TermName)
        .value("RefinedAbout", MetadataConceptRole::RefinedAbout)
        .value("RegistryItemIdentifier",
               MetadataConceptRole::RegistryItemIdentifier)
        .value("RegistryOrganizationIdentifier",
               MetadataConceptRole::RegistryOrganizationIdentifier)
        .value("RegistryEntryRole", MetadataConceptRole::RegistryEntryRole)
        .value("RegionIdentifier", MetadataConceptRole::RegionIdentifier)
        .value("RegionName", MetadataConceptRole::RegionName)
        .value("RegionContentTypeIdentifier",
               MetadataConceptRole::RegionContentTypeIdentifier)
        .value("RegionContentTypeName",
               MetadataConceptRole::RegionContentTypeName)
        .value("RegionRoleIdentifier",
               MetadataConceptRole::RegionRoleIdentifier)
        .value("RegionRoleName", MetadataConceptRole::RegionRoleName)
        .value("VersionIdentifier", MetadataConceptRole::VersionIdentifier)
        .value("RenditionParameters", MetadataConceptRole::RenditionParameters)
        .value("FilePath", MetadataConceptRole::FilePath)
        .value("FromPart", MetadataConceptRole::FromPart)
        .value("ToPart", MetadataConceptRole::ToPart)
        .value("Manager", MetadataConceptRole::Manager)
        .value("ManagerVariant", MetadataConceptRole::ManagerVariant)
        .value("ManageTo", MetadataConceptRole::ManageTo)
        .value("ManageUi", MetadataConceptRole::ManageUi)
        .value("AlternatePath", MetadataConceptRole::AlternatePath)
        .value("LastModifiedDate", MetadataConceptRole::LastModifiedDate)
        .value("MaskMarkers", MetadataConceptRole::MaskMarkers)
        .value("PartMapping", MetadataConceptRole::PartMapping)
        .value("LastUrl", MetadataConceptRole::LastUrl)
        .value("LinkForm", MetadataConceptRole::LinkForm)
        .value("LinkCategory", MetadataConceptRole::LinkCategory)
        .value("PlacedXResolution", MetadataConceptRole::PlacedXResolution)
        .value("PlacedYResolution", MetadataConceptRole::PlacedYResolution)
        .value("PlacedResolutionUnit",
               MetadataConceptRole::PlacedResolutionUnit)
        .value("EventAction", MetadataConceptRole::EventAction)
        .value("EventParameters", MetadataConceptRole::EventParameters)
        .value("SoftwareAgent", MetadataConceptRole::SoftwareAgent)
        .value("EventWhen", MetadataConceptRole::EventWhen)
        .value("ChangedParts", MetadataConceptRole::ChangedParts)
        .value("Format", MetadataConceptRole::Format)
        .value("RegionBoundary", MetadataConceptRole::RegionBoundary)
        .value("RegionBoundaryShape", MetadataConceptRole::RegionBoundaryShape)
        .value("RegionBoundaryUnit", MetadataConceptRole::RegionBoundaryUnit)
        .value("RegionBoundaryX", MetadataConceptRole::RegionBoundaryX)
        .value("RegionBoundaryY", MetadataConceptRole::RegionBoundaryY)
        .value("RegionBoundaryWidth", MetadataConceptRole::RegionBoundaryWidth)
        .value("RegionBoundaryHeight",
               MetadataConceptRole::RegionBoundaryHeight)
        .value("RegionBoundaryRadius",
               MetadataConceptRole::RegionBoundaryRadius)
        .value("RegionBoundaryVertexX",
               MetadataConceptRole::RegionBoundaryVertexX)
        .value("RegionBoundaryVertexY",
               MetadataConceptRole::RegionBoundaryVertexY)
        .value("RegionBoundaryVertex",
               MetadataConceptRole::RegionBoundaryVertex)
        .value("VersionComments", MetadataConceptRole::VersionComments)
        .value("VersionModifier", MetadataConceptRole::VersionModifier)
        .value("ObjectTypeReference", MetadataConceptRole::ObjectTypeReference)
        .value("ObjectAttributeReference",
               MetadataConceptRole::ObjectAttributeReference)
        .value("EditStatus", MetadataConceptRole::EditStatus)
        .value("EditorialUpdate", MetadataConceptRole::EditorialUpdate)
        .value("SubjectReference", MetadataConceptRole::SubjectReference)
        .value("FixtureIdentifier", MetadataConceptRole::FixtureIdentifier)
        .value("EditorialReleaseDate",
               MetadataConceptRole::EditorialReleaseDate)
        .value("EditorialExpirationDate",
               MetadataConceptRole::EditorialExpirationDate)
        .value("ActionAdvised", MetadataConceptRole::ActionAdvised)
        .value("ReferenceService", MetadataConceptRole::ReferenceService)
        .value("ReferenceDate", MetadataConceptRole::ReferenceDate)
        .value("ReferenceNumber", MetadataConceptRole::ReferenceNumber)
        .value("ObjectCycle", MetadataConceptRole::ObjectCycle)
        .value("LanguageIdentifier", MetadataConceptRole::LanguageIdentifier)
        .value("Contact", MetadataConceptRole::Contact)
        .value("RasterizedCaption", MetadataConceptRole::RasterizedCaption)
        .value("ImageType", MetadataConceptRole::ImageType)
        .value("ImageComponentCount", MetadataConceptRole::ImageComponentCount)
        .value("ImageColorComponentCode",
               MetadataConceptRole::ImageColorComponentCode)
        .value("ImageLayout", MetadataConceptRole::ImageLayout)
        .value("AudioType", MetadataConceptRole::AudioType)
        .value("AudioChannelCount", MetadataConceptRole::AudioChannelCount)
        .value("AudioContentCode", MetadataConceptRole::AudioContentCode)
        .value("AudioSamplingRate", MetadataConceptRole::AudioSamplingRate)
        .value("AudioSamplingResolution",
               MetadataConceptRole::AudioSamplingResolution)
        .value("AudioDuration", MetadataConceptRole::AudioDuration)
        .value("AudioOutcue", MetadataConceptRole::AudioOutcue)
        .value("PreviewFormat", MetadataConceptRole::PreviewFormat)
        .value("PreviewVersion", MetadataConceptRole::PreviewVersion)
        .value("PreviewData", MetadataConceptRole::PreviewData);

    nb::enum_<MetadataConceptRecordKind>(m, "MetadataConceptRecordKind")
        .value("None_", MetadataConceptRecordKind::None)
        .value("CreatorContact", MetadataConceptRecordKind::CreatorContact)
        .value("Event", MetadataConceptRecordKind::Event)
        .value("Person", MetadataConceptRecordKind::Person)
        .value("Organization", MetadataConceptRecordKind::Organization)
        .value("Product", MetadataConceptRecordKind::Product)
        .value("ArtworkOrObject", MetadataConceptRecordKind::ArtworkOrObject)
        .value("RightsExpression", MetadataConceptRecordKind::RightsExpression)
        .value("RightsHolder", MetadataConceptRecordKind::RightsHolder)
        .value("Licensor", MetadataConceptRecordKind::Licensor)
        .value("Licensee", MetadataConceptRecordKind::Licensee)
        .value("License", MetadataConceptRecordKind::License)
        .value("Release", MetadataConceptRecordKind::Release)
        .value("EndUser", MetadataConceptRecordKind::EndUser)
        .value("ImageCreator", MetadataConceptRecordKind::ImageCreator)
        .value("ImageSupplier", MetadataConceptRecordKind::ImageSupplier)
        .value("ImageAsset", MetadataConceptRecordKind::ImageAsset)
        .value("ControlledVocabularyTerm",
               MetadataConceptRecordKind::ControlledVocabularyTerm)
        .value("RegistryEntry", MetadataConceptRecordKind::RegistryEntry)
        .value("ImageRegion", MetadataConceptRecordKind::ImageRegion)
        .value("ResourceReference",
               MetadataConceptRecordKind::ResourceReference)
        .value("ResourceEvent", MetadataConceptRecordKind::ResourceEvent)
        .value("PantryItem", MetadataConceptRecordKind::PantryItem)
        .value("ImageRegionBoundary",
               MetadataConceptRecordKind::ImageRegionBoundary)
        .value("ManifestItem", MetadataConceptRecordKind::ManifestItem)
        .value("Version", MetadataConceptRecordKind::Version)
        .value("EditorialWorkflow",
               MetadataConceptRecordKind::EditorialWorkflow)
        .value("SourceSoftware", MetadataConceptRecordKind::SourceSoftware)
        .value("EditorialContact", MetadataConceptRecordKind::EditorialContact)
        .value("TechnicalImage", MetadataConceptRecordKind::TechnicalImage)
        .value("AudioAsset", MetadataConceptRecordKind::AudioAsset)
        .value("PreviewAsset", MetadataConceptRecordKind::PreviewAsset);

    nb::enum_<MetadataImageRegionShape>(m, "MetadataImageRegionShape")
        .value("Unknown", MetadataImageRegionShape::Unknown)
        .value("Rectangle", MetadataImageRegionShape::Rectangle)
        .value("Circle", MetadataImageRegionShape::Circle)
        .value("Polygon", MetadataImageRegionShape::Polygon);

    nb::enum_<MetadataImageRegionCoordinateUnit>(
        m, "MetadataImageRegionCoordinateUnit")
        .value("Unknown", MetadataImageRegionCoordinateUnit::Unknown)
        .value("Pixel", MetadataImageRegionCoordinateUnit::Pixel)
        .value("Relative", MetadataImageRegionCoordinateUnit::Relative);

    nb::enum_<MetadataConceptSensitivity>(m, "MetadataConceptSensitivity")
        .value("None_", MetadataConceptSensitivity::None)
        .value("PersonalContact", MetadataConceptSensitivity::PersonalContact)
        .value("PersonIdentity", MetadataConceptSensitivity::PersonIdentity)
        .value("Location", MetadataConceptSensitivity::Location)
        .value("LegalRights", MetadataConceptSensitivity::LegalRights);

    nb::enum_<MetadataConceptDateTimePrecision>(
        m, "MetadataConceptDateTimePrecision")
        .value("Unknown", MetadataConceptDateTimePrecision::Unknown)
        .value("Date", MetadataConceptDateTimePrecision::Date)
        .value("DateTime", MetadataConceptDateTimePrecision::DateTime)
        .value("DateTimeSubsecond",
               MetadataConceptDateTimePrecision::DateTimeSubsecond);

    nb::enum_<MetadataConceptTimeZoneKind>(m, "MetadataConceptTimeZoneKind")
        .value("Unknown", MetadataConceptTimeZoneKind::Unknown)
        .value("Local", MetadataConceptTimeZoneKind::Local)
        .value("Utc", MetadataConceptTimeZoneKind::Utc)
        .value("Offset", MetadataConceptTimeZoneKind::Offset);

    nb::enum_<MetadataConceptTransferHint>(m, "MetadataConceptTransferHint")
        .value("Unknown", MetadataConceptTransferHint::Unknown)
        .value("Safe", MetadataConceptTransferHint::Safe)
        .value("SourceBound", MetadataConceptTransferHint::SourceBound)
        .value("RenderedUnsafe", MetadataConceptTransferHint::RenderedUnsafe)
        .value("RequiresTargetImageSpec",
               MetadataConceptTransferHint::RequiresTargetImageSpec);

    nb::enum_<MetadataRawDataEncoding>(m, "MetadataRawDataEncoding")
        .value("Unknown", MetadataRawDataEncoding::Unknown)
        .value("Uncompressed", MetadataRawDataEncoding::Uncompressed)
        .value("Packed", MetadataRawDataEncoding::Packed)
        .value("LosslessCompressed",
               MetadataRawDataEncoding::LosslessCompressed)
        .value("LossyCompressed", MetadataRawDataEncoding::LossyCompressed)
        .value("Rendered", MetadataRawDataEncoding::Rendered);

    nb::enum_<MetadataRawApplicabilityState>(m, "MetadataRawApplicabilityState")
        .value("Unknown", MetadataRawApplicabilityState::Unknown)
        .value("AppliesToStoredRaw",
               MetadataRawApplicabilityState::AppliesToStoredRaw)
        .value("ConditionalOnRawEncoding",
               MetadataRawApplicabilityState::ConditionalOnRawEncoding)
        .value("NotApplicableToStoredRaw",
               MetadataRawApplicabilityState::NotApplicableToStoredRaw);

    nb::class_<MetadataRawDataDescriptor>(m, "MetadataRawDataDescriptor")
        .def(nb::init<>())
        .def_rw("encoding", &MetadataRawDataDescriptor::encoding)
        .def_rw("has_dimensions", &MetadataRawDataDescriptor::has_dimensions)
        .def_rw("width", &MetadataRawDataDescriptor::width)
        .def_rw("height", &MetadataRawDataDescriptor::height)
        .def_rw("has_channel_count",
                &MetadataRawDataDescriptor::has_channel_count)
        .def_rw("channel_count", &MetadataRawDataDescriptor::channel_count)
        .def_rw("has_bits_per_sample",
                &MetadataRawDataDescriptor::has_bits_per_sample)
        .def_rw("bits_per_sample", &MetadataRawDataDescriptor::bits_per_sample)
        .def_rw("has_compression_code",
                &MetadataRawDataDescriptor::has_compression_code)
        .def_rw("compression_code",
                &MetadataRawDataDescriptor::compression_code)
        .def_rw("has_plane_index", &MetadataRawDataDescriptor::has_plane_index)
        .def_rw("plane_index", &MetadataRawDataDescriptor::plane_index)
        .def_rw("requires_compressed_raw_encoding",
                &MetadataRawDataDescriptor::requires_compressed_raw_encoding)
        .def_rw("requires_primary_raw_plane",
                &MetadataRawDataDescriptor::requires_primary_raw_plane);

    nb::enum_<CcmQueryStatus>(m, "CcmQueryStatus")
        .value("Ok", CcmQueryStatus::Ok)
        .value("LimitExceeded", CcmQueryStatus::LimitExceeded);

    nb::enum_<CcmValidationMode>(m, "CcmValidationMode")
        .value("None", CcmValidationMode::None)
        .value("DngSpecWarnings", CcmValidationMode::DngSpecWarnings);

    nb::enum_<CcmIssueSeverity>(m, "CcmIssueSeverity")
        .value("Warning", CcmIssueSeverity::Warning)
        .value("Error", CcmIssueSeverity::Error);

    nb::enum_<CcmIssueCode>(m, "CcmIssueCode")
        .value("DecodeFailed", CcmIssueCode::DecodeFailed)
        .value("NonFiniteValue", CcmIssueCode::NonFiniteValue)
        .value("UnexpectedCount", CcmIssueCode::UnexpectedCount)
        .value("MatrixCountNotDivisibleBy3",
               CcmIssueCode::MatrixCountNotDivisibleBy3)
        .value("NonPositiveValue", CcmIssueCode::NonPositiveValue)
        .value("AsShotConflict", CcmIssueCode::AsShotConflict)
        .value("MissingCompanionTag", CcmIssueCode::MissingCompanionTag)
        .value("TripleIlluminantRule", CcmIssueCode::TripleIlluminantRule)
        .value("CalibrationSignatureMismatch",
               CcmIssueCode::CalibrationSignatureMismatch)
        .value("MissingIlluminantData", CcmIssueCode::MissingIlluminantData)
        .value("InvalidIlluminantCode", CcmIssueCode::InvalidIlluminantCode)
        .value("WhiteXYOutOfRange", CcmIssueCode::WhiteXYOutOfRange);

    nb::enum_<ValidateStatus>(m, "ValidateStatus")
        .value("Ok", ValidateStatus::Ok)
        .value("OpenFailed", ValidateStatus::OpenFailed)
        .value("TooLarge", ValidateStatus::TooLarge)
        .value("ReadFailed", ValidateStatus::ReadFailed);

    nb::enum_<ValidateIssueSeverity>(m, "ValidateIssueSeverity")
        .value("Warning", ValidateIssueSeverity::Warning)
        .value("Error", ValidateIssueSeverity::Error);

    nb::enum_<IccTagInterpretStatus>(m, "IccTagInterpretStatus")
        .value("Ok", IccTagInterpretStatus::Ok)
        .value("Unsupported", IccTagInterpretStatus::Unsupported)
        .value("Malformed", IccTagInterpretStatus::Malformed)
        .value("LimitExceeded", IccTagInterpretStatus::LimitExceeded);

    nb::enum_<ExifLimitReason>(m, "ExifLimitReason")
        .value("None_", ExifLimitReason::None)
        .value("MaxIfds", ExifLimitReason::MaxIfds)
        .value("MaxEntriesPerIfd", ExifLimitReason::MaxEntriesPerIfd)
        .value("MaxTotalEntries", ExifLimitReason::MaxTotalEntries)
        .value("ValueCountTooLarge", ExifLimitReason::ValueCountTooLarge)
        .value("MaxArenaBytes", ExifLimitReason::MaxArenaBytes);

    nb::enum_<ExrDecodeStatus>(m, "ExrDecodeStatus")
        .value("Ok", ExrDecodeStatus::Ok)
        .value("Unsupported", ExrDecodeStatus::Unsupported)
        .value("Malformed", ExrDecodeStatus::Malformed)
        .value("LimitExceeded", ExrDecodeStatus::LimitExceeded)
        .value("OutputTruncated", ExrDecodeStatus::OutputTruncated);

    nb::enum_<ExrAdapterStatus>(m, "ExrAdapterStatus")
        .value("Ok", ExrAdapterStatus::Ok)
        .value("InvalidArgument", ExrAdapterStatus::InvalidArgument)
        .value("Unsupported", ExrAdapterStatus::Unsupported);

    nb::enum_<ExifOrientationStatus>(m, "ExifOrientationStatus")
        .value("Ok", ExifOrientationStatus::Ok)
        .value("InvalidArgument", ExifOrientationStatus::InvalidArgument);

    nb::enum_<LibRawOrientationStatus>(m, "LibRawOrientationStatus")
        .value("Ok", LibRawOrientationStatus::Ok)
        .value("InvalidArgument", LibRawOrientationStatus::InvalidArgument)
        .value("Unsupported", LibRawOrientationStatus::Unsupported);

    nb::enum_<LibRawOrientationCode>(m, "LibRawOrientationCode")
        .value("None", LibRawOrientationCode::None)
        .value("PreviewPassThrough", LibRawOrientationCode::PreviewPassThrough)
        .value("MissingExifOrientationAssumedDefault",
               LibRawOrientationCode::MissingExifOrientationAssumedDefault)
        .value("InvalidExifOrientation",
               LibRawOrientationCode::InvalidExifOrientation)
        .value("UnsupportedMirroredOrientation",
               LibRawOrientationCode::UnsupportedMirroredOrientation)
        .value("MirroredOrientationDropped",
               LibRawOrientationCode::MirroredOrientationDropped);

    nb::enum_<LibRawFlipToExifCode>(m, "LibRawFlipToExifCode")
        .value("None", LibRawFlipToExifCode::None)
        .value("PreviewPassThrough", LibRawFlipToExifCode::PreviewPassThrough)
        .value("InvalidLibRawFlip", LibRawFlipToExifCode::InvalidLibRawFlip);

    nb::enum_<LibRawOrientationSource>(m, "LibRawOrientationSource")
        .value("ExplicitInput", LibRawOrientationSource::ExplicitInput)
        .value("AssumedDefault", LibRawOrientationSource::AssumedDefault)
        .value("ExifIfd0", LibRawOrientationSource::ExifIfd0)
        .value("XmpTiffOrientation",
               LibRawOrientationSource::XmpTiffOrientation);

    nb::enum_<LibRawOrientationTarget>(m, "LibRawOrientationTarget")
        .value("RawImage", LibRawOrientationTarget::RawImage)
        .value("EmbeddedPreview", LibRawOrientationTarget::EmbeddedPreview);

    nb::enum_<LibRawMirrorPolicy>(m, "LibRawMirrorPolicy")
        .value("Reject", LibRawMirrorPolicy::Reject)
        .value("DropMirror", LibRawMirrorPolicy::DropMirror);

    nb::enum_<LibRawOrientationFileStatus>(m, "LibRawOrientationFileStatus")
        .value("Ok", LibRawOrientationFileStatus::Ok)
        .value("InvalidArgument", LibRawOrientationFileStatus::InvalidArgument)
        .value("OpenFailed", LibRawOrientationFileStatus::OpenFailed)
        .value("StatFailed", LibRawOrientationFileStatus::StatFailed)
        .value("TooLarge", LibRawOrientationFileStatus::TooLarge)
        .value("MapFailed", LibRawOrientationFileStatus::MapFailed)
        .value("DecodeFailed", LibRawOrientationFileStatus::DecodeFailed);

    nb::enum_<DngSdkAdapterStatus>(m, "DngSdkAdapterStatus")
        .value("Ok", DngSdkAdapterStatus::Ok)
        .value("InvalidArgument", DngSdkAdapterStatus::InvalidArgument)
        .value("Unsupported", DngSdkAdapterStatus::Unsupported)
        .value("Malformed", DngSdkAdapterStatus::Malformed)
        .value("InternalError", DngSdkAdapterStatus::InternalError);

    nb::enum_<XmpDecodeStatus>(m, "XmpDecodeStatus")
        .value("Ok", XmpDecodeStatus::Ok)
        .value("OutputTruncated", XmpDecodeStatus::OutputTruncated)
        .value("Unsupported", XmpDecodeStatus::Unsupported)
        .value("Malformed", XmpDecodeStatus::Malformed)
        .value("LimitExceeded", XmpDecodeStatus::LimitExceeded);

    nb::enum_<ContainerFormat>(m, "ContainerFormat")
        .value("Unknown", ContainerFormat::Unknown)
        .value("Jpeg", ContainerFormat::Jpeg)
        .value("Png", ContainerFormat::Png)
        .value("Webp", ContainerFormat::Webp)
        .value("Gif", ContainerFormat::Gif)
        .value("Tiff", ContainerFormat::Tiff)
        .value("Crw", ContainerFormat::Crw)
        .value("Raf", ContainerFormat::Raf)
        .value("X3f", ContainerFormat::X3f)
        .value("Jp2", ContainerFormat::Jp2)
        .value("Jxl", ContainerFormat::Jxl)
        .value("Heif", ContainerFormat::Heif)
        .value("Avif", ContainerFormat::Avif)
        .value("Cr3", ContainerFormat::Cr3)
        .value("Exr", ContainerFormat::Exr);

    nb::enum_<ContainerBlockKind>(m, "ContainerBlockKind")
        .value("Unknown", ContainerBlockKind::Unknown)
        .value("Exif", ContainerBlockKind::Exif)
        .value("Ciff", ContainerBlockKind::Ciff)
        .value("MakerNote", ContainerBlockKind::MakerNote)
        .value("Xmp", ContainerBlockKind::Xmp)
        .value("XmpExtended", ContainerBlockKind::XmpExtended)
        .value("Jumbf", ContainerBlockKind::Jumbf)
        .value("Icc", ContainerBlockKind::Icc)
        .value("IptcIim", ContainerBlockKind::IptcIim)
        .value("PhotoshopIrB", ContainerBlockKind::PhotoshopIrB)
        .value("Mpf", ContainerBlockKind::Mpf)
        .value("Comment", ContainerBlockKind::Comment)
        .value("Text", ContainerBlockKind::Text)
        .value("CompressedMetadata", ContainerBlockKind::CompressedMetadata);

    nb::enum_<BlockCompression>(m, "BlockCompression")
        .value("None", BlockCompression::None)
        .value("Deflate", BlockCompression::Deflate)
        .value("Brotli", BlockCompression::Brotli);

    nb::enum_<BlockChunking>(m, "BlockChunking")
        .value("None", BlockChunking::None)
        .value("JpegApp2SeqTotal", BlockChunking::JpegApp2SeqTotal)
        .value("JpegXmpExtendedGuidOffset",
               BlockChunking::JpegXmpExtendedGuidOffset)
        .value("GifSubBlocks", BlockChunking::GifSubBlocks)
        .value("BmffExifTiffOffsetU32Be",
               BlockChunking::BmffExifTiffOffsetU32Be)
        .value("BrobU32BeRealTypePrefix",
               BlockChunking::BrobU32BeRealTypePrefix)
        .value("Jp2UuidPayload", BlockChunking::Jp2UuidPayload)
        .value("PsIrB8Bim", BlockChunking::PsIrB8Bim);

    nb::enum_<MetaKeyKind>(m, "MetaKeyKind")
        .value("ExifTag", MetaKeyKind::ExifTag)
        .value("Comment", MetaKeyKind::Comment)
        .value("ExrAttribute", MetaKeyKind::ExrAttribute)
        .value("IptcDataset", MetaKeyKind::IptcDataset)
        .value("XmpProperty", MetaKeyKind::XmpProperty)
        .value("IccHeaderField", MetaKeyKind::IccHeaderField)
        .value("IccTag", MetaKeyKind::IccTag)
        .value("PhotoshopIrb", MetaKeyKind::PhotoshopIrb)
        .value("PhotoshopIrbField", MetaKeyKind::PhotoshopIrbField)
        .value("GeotiffKey", MetaKeyKind::GeotiffKey)
        .value("PrintImField", MetaKeyKind::PrintImField)
        .value("BmffField", MetaKeyKind::BmffField)
        .value("JumbfField", MetaKeyKind::JumbfField)
        .value("JumbfCborKey", MetaKeyKind::JumbfCborKey)
        .value("PngText", MetaKeyKind::PngText);

    nb::enum_<WireFamily>(m, "WireFamily")
        .value("None", WireFamily::None)
        .value("Tiff", WireFamily::Tiff)
        .value("Other", WireFamily::Other);

    nb::enum_<MetaValueKind>(m, "MetaValueKind")
        .value("Empty", MetaValueKind::Empty)
        .value("Scalar", MetaValueKind::Scalar)
        .value("Array", MetaValueKind::Array)
        .value("Bytes", MetaValueKind::Bytes)
        .value("Text", MetaValueKind::Text);

    nb::enum_<MetaElementType>(m, "MetaElementType")
        .value("U8", MetaElementType::U8)
        .value("I8", MetaElementType::I8)
        .value("U16", MetaElementType::U16)
        .value("I16", MetaElementType::I16)
        .value("U32", MetaElementType::U32)
        .value("I32", MetaElementType::I32)
        .value("U64", MetaElementType::U64)
        .value("I64", MetaElementType::I64)
        .value("F32", MetaElementType::F32)
        .value("F64", MetaElementType::F64)
        .value("URational", MetaElementType::URational)
        .value("SRational", MetaElementType::SRational);

    nb::enum_<TextEncoding>(m, "TextEncoding")
        .value("Unknown", TextEncoding::Unknown)
        .value("Ascii", TextEncoding::Ascii)
        .value("Utf8", TextEncoding::Utf8)
        .value("Utf16LE", TextEncoding::Utf16LE)
        .value("Utf16BE", TextEncoding::Utf16BE);

    nb::enum_<ExportNameStyle>(m, "ExportNameStyle")
        .value("Canonical", ExportNameStyle::Canonical)
        .value("XmpPortable", ExportNameStyle::XmpPortable)
        .value("FlatHost", ExportNameStyle::FlatHost);
    nb::enum_<ExportNamePolicy>(m, "ExportNamePolicy")
        .value("Spec", ExportNamePolicy::Spec)
        .value("ExifToolAlias", ExportNamePolicy::ExifToolAlias);

    nb::enum_<XmpDumpStatus>(m, "XmpDumpStatus")
        .value("Ok", XmpDumpStatus::Ok)
        .value("OutputTruncated", XmpDumpStatus::OutputTruncated)
        .value("LimitExceeded", XmpDumpStatus::LimitExceeded);

    nb::enum_<JumbfDecodeStatus>(m, "JumbfDecodeStatus")
        .value("Ok", JumbfDecodeStatus::Ok)
        .value("Unsupported", JumbfDecodeStatus::Unsupported)
        .value("Malformed", JumbfDecodeStatus::Malformed)
        .value("LimitExceeded", JumbfDecodeStatus::LimitExceeded);

    nb::enum_<C2paVerifyStatus>(m, "C2paVerifyStatus")
        .value("NotRequested", C2paVerifyStatus::NotRequested)
        .value("DisabledByBuild", C2paVerifyStatus::DisabledByBuild)
        .value("BackendUnavailable", C2paVerifyStatus::BackendUnavailable)
        .value("NoSignatures", C2paVerifyStatus::NoSignatures)
        .value("InvalidSignature", C2paVerifyStatus::InvalidSignature)
        .value("VerificationFailed", C2paVerifyStatus::VerificationFailed)
        .value("Verified", C2paVerifyStatus::Verified)
        .value("NotImplemented", C2paVerifyStatus::NotImplemented)
        .value("SignatureVerifiedOnly",
               C2paVerifyStatus::SignatureVerifiedOnly);

    nb::enum_<C2paVerifyBackend>(m, "C2paVerifyBackend")
        .value("None", C2paVerifyBackend::None)
        .value("Auto", C2paVerifyBackend::Auto)
        .value("Native", C2paVerifyBackend::Native)
        .value("OpenSsl", C2paVerifyBackend::OpenSsl);

    nb::enum_<XmpSidecarFormat>(m, "XmpSidecarFormat")
        .value("Lossless", XmpSidecarFormat::Lossless)
        .value("Portable", XmpSidecarFormat::Portable);

    nb::enum_<XmpConflictPolicy>(m, "XmpConflictPolicy")
        .value("CurrentBehavior", XmpConflictPolicy::CurrentBehavior)
        .value("ExistingWins", XmpConflictPolicy::ExistingWins)
        .value("GeneratedWins", XmpConflictPolicy::GeneratedWins);

    nb::enum_<XmpExistingNamespacePolicy>(m, "XmpExistingNamespacePolicy")
        .value("KnownPortableOnly",
               XmpExistingNamespacePolicy::KnownPortableOnly)
        .value("PreserveCustom", XmpExistingNamespacePolicy::PreserveCustom);

    nb::enum_<XmpExistingStandardNamespacePolicy>(
        m, "XmpExistingStandardNamespacePolicy")
        .value("PreserveAll", XmpExistingStandardNamespacePolicy::PreserveAll)
        .value("CanonicalizeManaged",
               XmpExistingStandardNamespacePolicy::CanonicalizeManaged);

    nb::enum_<XmpWritebackMode>(m, "XmpWritebackMode")
        .value("EmbeddedOnly", XmpWritebackMode::EmbeddedOnly)
        .value("SidecarOnly", XmpWritebackMode::SidecarOnly)
        .value("EmbeddedAndSidecar", XmpWritebackMode::EmbeddedAndSidecar);

    nb::enum_<XmpDestinationEmbeddedMode>(m, "XmpDestinationEmbeddedMode")
        .value("PreserveExisting", XmpDestinationEmbeddedMode::PreserveExisting)
        .value("StripExisting", XmpDestinationEmbeddedMode::StripExisting);

    nb::enum_<XmpDestinationSidecarMode>(m, "XmpDestinationSidecarMode")
        .value("PreserveExisting", XmpDestinationSidecarMode::PreserveExisting)
        .value("StripExisting", XmpDestinationSidecarMode::StripExisting);

    nb::enum_<XmpExistingDestinationSidecarState>(
        m, "XmpExistingDestinationSidecarState")
        .value("Unknown", XmpExistingDestinationSidecarState::Unknown)
        .value("NotPresent", XmpExistingDestinationSidecarState::NotPresent)
        .value("Present", XmpExistingDestinationSidecarState::Present);

    nb::enum_<XmpExistingSidecarMode>(m, "XmpExistingSidecarMode")
        .value("Ignore", XmpExistingSidecarMode::Ignore)
        .value("MergeIfPresent", XmpExistingSidecarMode::MergeIfPresent);

    nb::enum_<XmpExistingSidecarPrecedence>(m, "XmpExistingSidecarPrecedence")
        .value("SidecarWins", XmpExistingSidecarPrecedence::SidecarWins)
        .value("SourceWins", XmpExistingSidecarPrecedence::SourceWins);

    nb::enum_<XmpExistingDestinationEmbeddedMode>(
        m, "XmpExistingDestinationEmbeddedMode")
        .value("Ignore", XmpExistingDestinationEmbeddedMode::Ignore)
        .value("MergeIfPresent",
               XmpExistingDestinationEmbeddedMode::MergeIfPresent);

    nb::enum_<XmpExistingDestinationEmbeddedPrecedence>(
        m, "XmpExistingDestinationEmbeddedPrecedence")
        .value("DestinationWins",
               XmpExistingDestinationEmbeddedPrecedence::DestinationWins)
        .value("SourceWins",
               XmpExistingDestinationEmbeddedPrecedence::SourceWins);

    nb::enum_<XmpExistingDestinationCarrierPrecedence>(
        m, "XmpExistingDestinationCarrierPrecedence")
        .value("SidecarWins",
               XmpExistingDestinationCarrierPrecedence::SidecarWins)
        .value("EmbeddedWins",
               XmpExistingDestinationCarrierPrecedence::EmbeddedWins);

    nb::enum_<DngTargetMode>(m, "DngTargetMode")
        .value("ExistingTarget", DngTargetMode::ExistingTarget)
        .value("TemplateTarget", DngTargetMode::TemplateTarget)
        .value("MinimalFreshScaffold", DngTargetMode::MinimalFreshScaffold);

    nb::enum_<TransferTargetFormat>(m, "TransferTargetFormat")
        .value("Jpeg", TransferTargetFormat::Jpeg)
        .value("Tiff", TransferTargetFormat::Tiff)
        .value("Dng", TransferTargetFormat::Dng)
        .value("Jxl", TransferTargetFormat::Jxl)
        .value("Webp", TransferTargetFormat::Webp)
        .value("Heif", TransferTargetFormat::Heif)
        .value("Avif", TransferTargetFormat::Avif)
        .value("Cr3", TransferTargetFormat::Cr3)
        .value("Exr", TransferTargetFormat::Exr)
        .value("Png", TransferTargetFormat::Png)
        .value("Jp2", TransferTargetFormat::Jp2);

    nb::enum_<TransferSafetyMode>(m, "TransferSafetyMode")
        .value("CompatibleFile", TransferSafetyMode::CompatibleFile)
        .value("RenderedImage", TransferSafetyMode::RenderedImage);

    nb::enum_<TransferConceptDiagnosticAction>(m,
                                               "TransferConceptDiagnosticAction")
        .value("Keep", TransferConceptDiagnosticAction::Keep)
        .value("Drop", TransferConceptDiagnosticAction::Drop)
        .value("RequiresTargetImageSpec",
               TransferConceptDiagnosticAction::RequiresTargetImageSpec);

    nb::enum_<TransferConceptDiagnosticReason>(m,
                                               "TransferConceptDiagnosticReason")
        .value("Unknown", TransferConceptDiagnosticReason::Unknown)
        .value("Safe", TransferConceptDiagnosticReason::Safe)
        .value("SourceBound", TransferConceptDiagnosticReason::SourceBound)
        .value("RenderedUnsafe",
               TransferConceptDiagnosticReason::RenderedUnsafe)
        .value("TargetImageSpecRequired",
               TransferConceptDiagnosticReason::TargetImageSpecRequired)
        .value("RawApplicabilityConditional",
               TransferConceptDiagnosticReason::RawApplicabilityConditional)
        .value("RawApplicabilityNotApplicable",
               TransferConceptDiagnosticReason::RawApplicabilityNotApplicable);

    nb::enum_<TransferConceptDiagnosticSeverity>(
        m, "TransferConceptDiagnosticSeverity")
        .value("Info", TransferConceptDiagnosticSeverity::Info)
        .value("Warning", TransferConceptDiagnosticSeverity::Warning);

    m.attr("TRANSFER_TARGET_IMAGE_SPEC_MAX_SAMPLES") = nb::int_(
        kTransferTargetImageSpecMaxSamples);

    nb::class_<TransferTargetImageSpec>(m, "TransferTargetImageSpec")
        .def(nb::init<>())
        .def_rw("has_dimensions", &TransferTargetImageSpec::has_dimensions)
        .def_rw("width", &TransferTargetImageSpec::width)
        .def_rw("height", &TransferTargetImageSpec::height)
        .def_rw("has_orientation", &TransferTargetImageSpec::has_orientation)
        .def_rw("orientation", &TransferTargetImageSpec::orientation)
        .def_rw("has_samples_per_pixel",
                &TransferTargetImageSpec::has_samples_per_pixel)
        .def_rw("samples_per_pixel",
                &TransferTargetImageSpec::samples_per_pixel)
        .def_prop_rw("bits_per_sample", &transfer_target_image_spec_bits,
                     &transfer_target_image_spec_set_bits)
        .def_prop_rw("sample_format", &transfer_target_image_spec_sample_format,
                     &transfer_target_image_spec_set_sample_format)
        .def_rw("has_photometric_interpretation",
                &TransferTargetImageSpec::has_photometric_interpretation)
        .def_rw("photometric_interpretation",
                &TransferTargetImageSpec::photometric_interpretation)
        .def_rw("has_planar_configuration",
                &TransferTargetImageSpec::has_planar_configuration)
        .def_rw("planar_configuration",
                &TransferTargetImageSpec::planar_configuration)
        .def_rw("has_compression", &TransferTargetImageSpec::has_compression)
        .def_rw("compression", &TransferTargetImageSpec::compression)
        .def_rw("has_exif_color_space",
                &TransferTargetImageSpec::has_exif_color_space)
        .def_rw("exif_color_space", &TransferTargetImageSpec::exif_color_space);

    nb::enum_<MetadataCapabilityFamily>(m, "MetadataCapabilityFamily")
        .value("Exif", MetadataCapabilityFamily::Exif)
        .value("Xmp", MetadataCapabilityFamily::Xmp)
        .value("Icc", MetadataCapabilityFamily::Icc)
        .value("Iptc", MetadataCapabilityFamily::Iptc)
        .value("MakerNote", MetadataCapabilityFamily::MakerNote)
        .value("PhotoshopIrb", MetadataCapabilityFamily::PhotoshopIrb)
        .value("Jumbf", MetadataCapabilityFamily::Jumbf)
        .value("C2pa", MetadataCapabilityFamily::C2pa)
        .value("BmffFields", MetadataCapabilityFamily::BmffFields)
        .value("GeoTiff", MetadataCapabilityFamily::GeoTiff)
        .value("ExrAttribute", MetadataCapabilityFamily::ExrAttribute);

    nb::enum_<MetadataCapabilitySupport>(m, "MetadataCapabilitySupport")
        .value("Unsupported", MetadataCapabilitySupport::Unsupported)
        .value("Supported", MetadataCapabilitySupport::Supported)
        .value("Bounded", MetadataCapabilitySupport::Bounded)
        .value("Disabled", MetadataCapabilitySupport::Disabled);

    nb::enum_<MetadataCreationFieldKind>(m, "MetadataCreationFieldKind")
        .value("Title", MetadataCreationFieldKind::Title)
        .value("Description", MetadataCreationFieldKind::Description)
        .value("Creator", MetadataCreationFieldKind::Creator)
        .value("Keyword", MetadataCreationFieldKind::Keyword)
        .value("Copyright", MetadataCreationFieldKind::Copyright)
        .value("RightsUsageTerms", MetadataCreationFieldKind::RightsUsageTerms)
        .value("Credit", MetadataCreationFieldKind::Credit)
        .value("Source", MetadataCreationFieldKind::Source)
        .value("CreateDate", MetadataCreationFieldKind::CreateDate)
        .value("ModifyDate", MetadataCreationFieldKind::ModifyDate)
        .value("Rating", MetadataCreationFieldKind::Rating)
        .value("Label", MetadataCreationFieldKind::Label)
        .value("CameraMake", MetadataCreationFieldKind::CameraMake)
        .value("CameraModel", MetadataCreationFieldKind::CameraModel)
        .value("Software", MetadataCreationFieldKind::Software)
        .value("DateTimeOriginal", MetadataCreationFieldKind::DateTimeOriginal)
        .value("Orientation", MetadataCreationFieldKind::Orientation)
        .value("PixelWidth", MetadataCreationFieldKind::PixelWidth)
        .value("PixelHeight", MetadataCreationFieldKind::PixelHeight)
        .value("ColorSpace", MetadataCreationFieldKind::ColorSpace)
        .value("ExposureTime", MetadataCreationFieldKind::ExposureTime)
        .value("FNumber", MetadataCreationFieldKind::FNumber)
        .value("IsoSensitivity", MetadataCreationFieldKind::IsoSensitivity)
        .value("FocalLength", MetadataCreationFieldKind::FocalLength);

    nb::enum_<MetadataCreationValueKind>(m, "MetadataCreationValueKind")
        .value("Text", MetadataCreationValueKind::Text)
        .value("UnsignedInteger", MetadataCreationValueKind::UnsignedInteger)
        .value("SignedInteger", MetadataCreationValueKind::SignedInteger)
        .value("UnsignedRational", MetadataCreationValueKind::UnsignedRational);

    nb::enum_<MetadataCreationStatus>(m, "MetadataCreationStatus")
        .value("Ok", MetadataCreationStatus::Ok)
        .value("NullOutput", MetadataCreationStatus::NullOutput)
        .value("InvalidLimits", MetadataCreationStatus::InvalidLimits)
        .value("TooManyFields", MetadataCreationStatus::TooManyFields)
        .value("WrongValueKind", MetadataCreationStatus::WrongValueKind)
        .value("EmptyText", MetadataCreationStatus::EmptyText)
        .value("TextTooLong", MetadataCreationStatus::TextTooLong)
        .value("TotalTextTooLong", MetadataCreationStatus::TotalTextTooLong)
        .value("InvalidText", MetadataCreationStatus::InvalidText)
        .value("InvalidValue", MetadataCreationStatus::InvalidValue)
        .value("DuplicateSingleton", MetadataCreationStatus::DuplicateSingleton)
        .value("InternalError", MetadataCreationStatus::InternalError);

    nb::class_<PyMetadataCreationField>(m, "MetadataCreationField")
        .def_ro("kind", &PyMetadataCreationField::kind)
        .def_ro("value_kind", &PyMetadataCreationField::value_kind)
        .def_ro("text", &PyMetadataCreationField::text)
        .def_ro("unsigned_value", &PyMetadataCreationField::unsigned_value)
        .def_ro("signed_value", &PyMetadataCreationField::signed_value)
        .def_ro("numer", &PyMetadataCreationField::numer)
        .def_ro("denom", &PyMetadataCreationField::denom);

    m.attr("METADATA_CREATION_MAX_FIELDS") = nb::int_(
        kMetadataCreationMaxFields);
    m.attr("METADATA_CREATION_MAX_TEXT_BYTES_PER_FIELD") = nb::int_(
        kMetadataCreationMaxTextBytesPerField);
    m.attr("METADATA_CREATION_MAX_TOTAL_TEXT_BYTES") = nb::int_(
        kMetadataCreationMaxTotalTextBytes);

    m.def("metadata_creation_text", &metadata_creation_text_python, "kind"_a,
          "value"_a);
    m.def("metadata_creation_u32", &metadata_creation_u32_python, "kind"_a,
          "value"_a);
    m.def("metadata_creation_i32", &metadata_creation_i32_python, "kind"_a,
          "value"_a);
    m.def("metadata_creation_urational", &metadata_creation_urational_python,
          "kind"_a, "numer"_a, "denom"_a);
    m.def("metadata_creation_field_kind_name",
          &metadata_creation_field_kind_name, "kind"_a);
    m.def("metadata_creation_status_name", &metadata_creation_status_name,
          "status"_a);

    nb::enum_<MetadataEditingOperationKind>(m, "MetadataEditingOperationKind")
        .value("Add", MetadataEditingOperationKind::Add)
        .value("Set", MetadataEditingOperationKind::Set)
        .value("Remove", MetadataEditingOperationKind::Remove);

    nb::enum_<MetadataEditingStatus>(m, "MetadataEditingStatus")
        .value("Ok", MetadataEditingStatus::Ok)
        .value("NullOutput", MetadataEditingStatus::NullOutput)
        .value("BaseNotFinalized", MetadataEditingStatus::BaseNotFinalized)
        .value("InvalidLimits", MetadataEditingStatus::InvalidLimits)
        .value("TooManyOperations", MetadataEditingStatus::TooManyOperations)
        .value("InvalidOperationKind",
               MetadataEditingStatus::InvalidOperationKind)
        .value("InvalidOccurrence", MetadataEditingStatus::InvalidOccurrence)
        .value("WrongValueKind", MetadataEditingStatus::WrongValueKind)
        .value("EmptyText", MetadataEditingStatus::EmptyText)
        .value("TextTooLong", MetadataEditingStatus::TextTooLong)
        .value("TotalTextTooLong", MetadataEditingStatus::TotalTextTooLong)
        .value("InvalidText", MetadataEditingStatus::InvalidText)
        .value("InvalidValue", MetadataEditingStatus::InvalidValue)
        .value("SingletonAlreadyExists",
               MetadataEditingStatus::SingletonAlreadyExists)
        .value("TargetNotFound", MetadataEditingStatus::TargetNotFound)
        .value("AmbiguousTarget", MetadataEditingStatus::AmbiguousTarget)
        .value("EntryLimitExceeded", MetadataEditingStatus::EntryLimitExceeded)
        .value("InternalError", MetadataEditingStatus::InternalError);

    nb::class_<PyMetadataEditingOperation>(m, "MetadataEditingOperation")
        .def_ro("kind", &PyMetadataEditingOperation::kind)
        .def_ro("field", &PyMetadataEditingOperation::field)
        .def_ro("occurrence", &PyMetadataEditingOperation::occurrence);

    m.attr("METADATA_EDITING_MAX_OPERATIONS") = nb::int_(
        kMetadataEditingMaxOperations);
    m.attr("METADATA_EDITING_MAX_TEXT_BYTES_PER_OPERATION") = nb::int_(
        kMetadataEditingMaxTextBytesPerOperation);
    m.attr("METADATA_EDITING_MAX_TOTAL_TEXT_BYTES") = nb::int_(
        kMetadataEditingMaxTotalTextBytes);
    m.attr("METADATA_EDITING_ALL_OCCURRENCES") = nb::int_(
        kMetadataEditingAllOccurrences);

    m.def("metadata_edit_add", &metadata_edit_add_python, "field"_a);
    m.def("metadata_edit_set", &metadata_edit_set_python, "field"_a,
          "occurrence"_a = 0U);
    m.def("metadata_edit_remove", &metadata_edit_remove_python, "kind"_a,
          "occurrence"_a = 0U);
    m.def("metadata_edit_remove_all", &metadata_edit_remove_all_python,
          "kind"_a);
    m.def("metadata_editing_operation_kind_name",
          &metadata_editing_operation_kind_name, "kind"_a);
    m.def("metadata_editing_status_name", &metadata_editing_status_name,
          "status"_a);

    nb::enum_<MetadataDateTranslationSourceMode>(
        m, "MetadataDateTranslationSourceMode")
        .value("DirtyOnly", MetadataDateTranslationSourceMode::DirtyOnly)
        .value("All", MetadataDateTranslationSourceMode::All);

    nb::enum_<MetadataDateTranslationConflictPolicy>(
        m, "MetadataDateTranslationConflictPolicy")
        .value("PreserveExisting",
               MetadataDateTranslationConflictPolicy::PreserveExisting)
        .value("FailOnConflict",
               MetadataDateTranslationConflictPolicy::FailOnConflict)
        .value("ReplaceExisting",
               MetadataDateTranslationConflictPolicy::ReplaceExisting);

    nb::enum_<MetadataDateTranslationMapping>(m,
                                              "MetadataDateTranslationMapping")
        .value("None", MetadataDateTranslationMapping::None)
        .value("XmpCreateDate", MetadataDateTranslationMapping::XmpCreateDate)
        .value("PhotoshopDateCreated",
               MetadataDateTranslationMapping::PhotoshopDateCreated)
        .value("XmpDateTimeOriginal",
               MetadataDateTranslationMapping::XmpDateTimeOriginal);

    nb::enum_<MetadataDateTranslationStatus>(m, "MetadataDateTranslationStatus")
        .value("Ok", MetadataDateTranslationStatus::Ok)
        .value("NullOutput", MetadataDateTranslationStatus::NullOutput)
        .value("SourceNotFinalized",
               MetadataDateTranslationStatus::SourceNotFinalized)
        .value("InvalidOptions", MetadataDateTranslationStatus::InvalidOptions)
        .value("AmbiguousSource",
               MetadataDateTranslationStatus::AmbiguousSource)
        .value("InvalidSourceValue",
               MetadataDateTranslationStatus::InvalidSourceValue)
        .value("InvalidDateTime",
               MetadataDateTranslationStatus::InvalidDateTime)
        .value("UnsupportedPrecision",
               MetadataDateTranslationStatus::UnsupportedPrecision)
        .value("NativeConflict", MetadataDateTranslationStatus::NativeConflict)
        .value("EntryLimitExceeded",
               MetadataDateTranslationStatus::EntryLimitExceeded)
        .value("OperationLimitExceeded",
               MetadataDateTranslationStatus::OperationLimitExceeded)
        .value("InternalError", MetadataDateTranslationStatus::InternalError);

    m.attr("METADATA_DATE_TRANSLATION_MAX_ADDED_ENTRIES") = nb::int_(
        kMetadataDateTranslationMaxAddedEntries);
    m.attr("METADATA_DATE_TRANSLATION_MAX_OPERATIONS") = nb::int_(
        kMetadataDateTranslationMaxOperations);
    m.def("metadata_date_translation_status_name",
          &metadata_date_translation_status_name, "status"_a);
    m.def("metadata_date_translation_mapping_name",
          &metadata_date_translation_mapping_name, "mapping"_a);

    nb::enum_<MetadataTechnicalTranslationSourceMode>(
        m, "MetadataTechnicalTranslationSourceMode")
        .value("DirtyOnly", MetadataTechnicalTranslationSourceMode::DirtyOnly)
        .value("All", MetadataTechnicalTranslationSourceMode::All);

    nb::enum_<MetadataTechnicalTranslationConflictPolicy>(
        m, "MetadataTechnicalTranslationConflictPolicy")
        .value("PreserveExisting",
               MetadataTechnicalTranslationConflictPolicy::PreserveExisting)
        .value("FailOnConflict",
               MetadataTechnicalTranslationConflictPolicy::FailOnConflict)
        .value("ReplaceExisting",
               MetadataTechnicalTranslationConflictPolicy::ReplaceExisting);

    nb::enum_<MetadataTechnicalTranslationMapping>(
        m, "MetadataTechnicalTranslationMapping")
        .value("None", MetadataTechnicalTranslationMapping::None)
        .value("XmpModifyDate",
               MetadataTechnicalTranslationMapping::XmpModifyDate)
        .value("TiffMake", MetadataTechnicalTranslationMapping::TiffMake)
        .value("TiffModel", MetadataTechnicalTranslationMapping::TiffModel)
        .value("XmpCreatorTool",
               MetadataTechnicalTranslationMapping::XmpCreatorTool);

    nb::enum_<MetadataTechnicalTranslationStatus>(
        m, "MetadataTechnicalTranslationStatus")
        .value("Ok", MetadataTechnicalTranslationStatus::Ok)
        .value("NullOutput", MetadataTechnicalTranslationStatus::NullOutput)
        .value("SourceNotFinalized",
               MetadataTechnicalTranslationStatus::SourceNotFinalized)
        .value("InvalidOptions",
               MetadataTechnicalTranslationStatus::InvalidOptions)
        .value("AmbiguousSource",
               MetadataTechnicalTranslationStatus::AmbiguousSource)
        .value("InvalidSourceValue",
               MetadataTechnicalTranslationStatus::InvalidSourceValue)
        .value("InvalidDateTime",
               MetadataTechnicalTranslationStatus::InvalidDateTime)
        .value("UnsupportedPrecision",
               MetadataTechnicalTranslationStatus::UnsupportedPrecision)
        .value("NonAsciiSource",
               MetadataTechnicalTranslationStatus::NonAsciiSource)
        .value("ValueTooLong", MetadataTechnicalTranslationStatus::ValueTooLong)
        .value("SourceLimitExceeded",
               MetadataTechnicalTranslationStatus::SourceLimitExceeded)
        .value("NativeConflict",
               MetadataTechnicalTranslationStatus::NativeConflict)
        .value("EntryLimitExceeded",
               MetadataTechnicalTranslationStatus::EntryLimitExceeded)
        .value("OperationLimitExceeded",
               MetadataTechnicalTranslationStatus::OperationLimitExceeded)
        .value("InternalError",
               MetadataTechnicalTranslationStatus::InternalError);

    m.attr("METADATA_TECHNICAL_TRANSLATION_MAX_ADDED_ENTRIES") = nb::int_(
        kMetadataTechnicalTranslationMaxAddedEntries);
    m.attr("METADATA_TECHNICAL_TRANSLATION_MAX_OPERATIONS") = nb::int_(
        kMetadataTechnicalTranslationMaxOperations);
    m.attr("METADATA_TECHNICAL_TRANSLATION_MAX_TEXT_BYTES_PER_PROPERTY")
        = nb::int_(kMetadataTechnicalTranslationMaxTextBytesPerProperty);
    m.attr("METADATA_TECHNICAL_TRANSLATION_MAX_TOTAL_TEXT_BYTES") = nb::int_(
        kMetadataTechnicalTranslationMaxTotalTextBytes);
    m.def("metadata_technical_translation_status_name",
          &metadata_technical_translation_status_name, "status"_a);
    m.def("metadata_technical_translation_mapping_name",
          &metadata_technical_translation_mapping_name, "mapping"_a);

    nb::enum_<MetadataCaptureTranslationSourceMode>(
        m, "MetadataCaptureTranslationSourceMode")
        .value("DirtyOnly", MetadataCaptureTranslationSourceMode::DirtyOnly)
        .value("All", MetadataCaptureTranslationSourceMode::All);

    nb::enum_<MetadataCaptureTranslationConflictPolicy>(
        m, "MetadataCaptureTranslationConflictPolicy")
        .value("PreserveExisting",
               MetadataCaptureTranslationConflictPolicy::PreserveExisting)
        .value("FailOnConflict",
               MetadataCaptureTranslationConflictPolicy::FailOnConflict)
        .value("ReplaceExisting",
               MetadataCaptureTranslationConflictPolicy::ReplaceExisting);

    nb::enum_<MetadataCaptureTranslationMapping>(
        m, "MetadataCaptureTranslationMapping")
        .value("None", MetadataCaptureTranslationMapping::None)
        .value("XmpExposureTime",
               MetadataCaptureTranslationMapping::XmpExposureTime)
        .value("XmpFNumber", MetadataCaptureTranslationMapping::XmpFNumber)
        .value("XmpIso", MetadataCaptureTranslationMapping::XmpIso)
        .value("XmpFocalLength",
               MetadataCaptureTranslationMapping::XmpFocalLength)
        .value("XmpExposureCompensation",
               MetadataCaptureTranslationMapping::XmpExposureCompensation);

    nb::enum_<MetadataCaptureTranslationStatus>(
        m, "MetadataCaptureTranslationStatus")
        .value("Ok", MetadataCaptureTranslationStatus::Ok)
        .value("NullOutput", MetadataCaptureTranslationStatus::NullOutput)
        .value("SourceNotFinalized",
               MetadataCaptureTranslationStatus::SourceNotFinalized)
        .value("InvalidOptions",
               MetadataCaptureTranslationStatus::InvalidOptions)
        .value("AmbiguousSource",
               MetadataCaptureTranslationStatus::AmbiguousSource)
        .value("InvalidSourceValue",
               MetadataCaptureTranslationStatus::InvalidSourceValue)
        .value("InvalidNumericValue",
               MetadataCaptureTranslationStatus::InvalidNumericValue)
        .value("ValueOutOfRange",
               MetadataCaptureTranslationStatus::ValueOutOfRange)
        .value("ValueTooLong", MetadataCaptureTranslationStatus::ValueTooLong)
        .value("SourceLimitExceeded",
               MetadataCaptureTranslationStatus::SourceLimitExceeded)
        .value("NativeConflict",
               MetadataCaptureTranslationStatus::NativeConflict)
        .value("EntryLimitExceeded",
               MetadataCaptureTranslationStatus::EntryLimitExceeded)
        .value("OperationLimitExceeded",
               MetadataCaptureTranslationStatus::OperationLimitExceeded)
        .value("InternalError",
               MetadataCaptureTranslationStatus::InternalError);

    m.attr("METADATA_CAPTURE_TRANSLATION_MAX_ADDED_ENTRIES") = nb::int_(
        kMetadataCaptureTranslationMaxAddedEntries);
    m.attr("METADATA_CAPTURE_TRANSLATION_MAX_OPERATIONS") = nb::int_(
        kMetadataCaptureTranslationMaxOperations);
    m.attr("METADATA_CAPTURE_TRANSLATION_MAX_TEXT_BYTES_PER_PROPERTY")
        = nb::int_(kMetadataCaptureTranslationMaxTextBytesPerProperty);
    m.attr("METADATA_CAPTURE_TRANSLATION_MAX_TOTAL_TEXT_BYTES") = nb::int_(
        kMetadataCaptureTranslationMaxTotalTextBytes);
    m.def("metadata_capture_translation_status_name",
          &metadata_capture_translation_status_name, "status"_a);
    m.def("metadata_capture_translation_mapping_name",
          &metadata_capture_translation_mapping_name, "mapping"_a);

    nb::enum_<MetadataGeometryTranslationSourceMode>(
        m, "MetadataGeometryTranslationSourceMode")
        .value("DirtyOnly", MetadataGeometryTranslationSourceMode::DirtyOnly)
        .value("All", MetadataGeometryTranslationSourceMode::All);

    nb::enum_<MetadataGeometryTranslationConflictPolicy>(
        m, "MetadataGeometryTranslationConflictPolicy")
        .value("PreserveExisting",
               MetadataGeometryTranslationConflictPolicy::PreserveExisting)
        .value("FailOnConflict",
               MetadataGeometryTranslationConflictPolicy::FailOnConflict)
        .value("ReplaceExisting",
               MetadataGeometryTranslationConflictPolicy::ReplaceExisting);

    nb::enum_<MetadataGeometryTranslationMapping>(
        m, "MetadataGeometryTranslationMapping")
        .value("None", MetadataGeometryTranslationMapping::None)
        .value("XmpOrientation",
               MetadataGeometryTranslationMapping::XmpOrientation)
        .value("XmpDimensions",
               MetadataGeometryTranslationMapping::XmpDimensions);

    nb::enum_<MetadataGeometryTranslationStatus>(
        m, "MetadataGeometryTranslationStatus")
        .value("Ok", MetadataGeometryTranslationStatus::Ok)
        .value("NullOutput", MetadataGeometryTranslationStatus::NullOutput)
        .value("SourceNotFinalized",
               MetadataGeometryTranslationStatus::SourceNotFinalized)
        .value("InvalidOptions",
               MetadataGeometryTranslationStatus::InvalidOptions)
        .value("InvalidTargetImageSpec",
               MetadataGeometryTranslationStatus::InvalidTargetImageSpec)
        .value("TargetImageSpecRequired",
               MetadataGeometryTranslationStatus::TargetImageSpecRequired)
        .value("TargetImageSpecMismatch",
               MetadataGeometryTranslationStatus::TargetImageSpecMismatch)
        .value("AmbiguousSource",
               MetadataGeometryTranslationStatus::AmbiguousSource)
        .value("IncompleteSourceGroup",
               MetadataGeometryTranslationStatus::IncompleteSourceGroup)
        .value("InvalidSourceValue",
               MetadataGeometryTranslationStatus::InvalidSourceValue)
        .value("InvalidNumericValue",
               MetadataGeometryTranslationStatus::InvalidNumericValue)
        .value("ValueOutOfRange",
               MetadataGeometryTranslationStatus::ValueOutOfRange)
        .value("ValueTooLong", MetadataGeometryTranslationStatus::ValueTooLong)
        .value("SourceLimitExceeded",
               MetadataGeometryTranslationStatus::SourceLimitExceeded)
        .value("NativeConflict",
               MetadataGeometryTranslationStatus::NativeConflict)
        .value("EntryLimitExceeded",
               MetadataGeometryTranslationStatus::EntryLimitExceeded)
        .value("OperationLimitExceeded",
               MetadataGeometryTranslationStatus::OperationLimitExceeded)
        .value("InternalError",
               MetadataGeometryTranslationStatus::InternalError);

    m.attr("METADATA_GEOMETRY_TRANSLATION_MAX_ADDED_ENTRIES") = nb::int_(
        kMetadataGeometryTranslationMaxAddedEntries);
    m.attr("METADATA_GEOMETRY_TRANSLATION_MAX_OPERATIONS") = nb::int_(
        kMetadataGeometryTranslationMaxOperations);
    m.attr("METADATA_GEOMETRY_TRANSLATION_MAX_TEXT_BYTES_PER_PROPERTY")
        = nb::int_(kMetadataGeometryTranslationMaxTextBytesPerProperty);
    m.attr("METADATA_GEOMETRY_TRANSLATION_MAX_TOTAL_TEXT_BYTES") = nb::int_(
        kMetadataGeometryTranslationMaxTotalTextBytes);
    m.def("metadata_geometry_translation_status_name",
          &metadata_geometry_translation_status_name, "status"_a);
    m.def("metadata_geometry_translation_mapping_name",
          &metadata_geometry_translation_mapping_name, "mapping"_a);

    nb::enum_<MetadataDescriptiveTranslationSourceMode>(
        m, "MetadataDescriptiveTranslationSourceMode")
        .value("DirtyOnly", MetadataDescriptiveTranslationSourceMode::DirtyOnly)
        .value("All", MetadataDescriptiveTranslationSourceMode::All);

    nb::enum_<MetadataDescriptiveTranslationConflictPolicy>(
        m, "MetadataDescriptiveTranslationConflictPolicy")
        .value("PreserveExisting",
               MetadataDescriptiveTranslationConflictPolicy::PreserveExisting)
        .value("FailOnConflict",
               MetadataDescriptiveTranslationConflictPolicy::FailOnConflict)
        .value("ReplaceExisting",
               MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting);

    nb::enum_<MetadataDescriptiveTranslationMapping>(
        m, "MetadataDescriptiveTranslationMapping")
        .value("None", MetadataDescriptiveTranslationMapping::None)
        .value("DcTitle", MetadataDescriptiveTranslationMapping::DcTitle)
        .value("DcDescription",
               MetadataDescriptiveTranslationMapping::DcDescription)
        .value("DcCreator", MetadataDescriptiveTranslationMapping::DcCreator)
        .value("DcSubject", MetadataDescriptiveTranslationMapping::DcSubject)
        .value("DcRights", MetadataDescriptiveTranslationMapping::DcRights)
        .value("PhotoshopCredit",
               MetadataDescriptiveTranslationMapping::PhotoshopCredit)
        .value("PhotoshopSource",
               MetadataDescriptiveTranslationMapping::PhotoshopSource);

    nb::enum_<MetadataDescriptiveTranslationStatus>(
        m, "MetadataDescriptiveTranslationStatus")
        .value("Ok", MetadataDescriptiveTranslationStatus::Ok)
        .value("NullOutput", MetadataDescriptiveTranslationStatus::NullOutput)
        .value("SourceNotFinalized",
               MetadataDescriptiveTranslationStatus::SourceNotFinalized)
        .value("InvalidOptions",
               MetadataDescriptiveTranslationStatus::InvalidOptions)
        .value("SourceLimitExceeded",
               MetadataDescriptiveTranslationStatus::SourceLimitExceeded)
        .value("AmbiguousSource",
               MetadataDescriptiveTranslationStatus::AmbiguousSource)
        .value("InvalidSourceValue",
               MetadataDescriptiveTranslationStatus::InvalidSourceValue)
        .value("ValueTooLong",
               MetadataDescriptiveTranslationStatus::ValueTooLong)
        .value("NativeConflict",
               MetadataDescriptiveTranslationStatus::NativeConflict)
        .value("NativeEncodingConflict",
               MetadataDescriptiveTranslationStatus::NativeEncodingConflict)
        .value("EntryLimitExceeded",
               MetadataDescriptiveTranslationStatus::EntryLimitExceeded)
        .value("OperationLimitExceeded",
               MetadataDescriptiveTranslationStatus::OperationLimitExceeded)
        .value("InternalError",
               MetadataDescriptiveTranslationStatus::InternalError);

    m.attr("METADATA_DESCRIPTIVE_TRANSLATION_MAX_SOURCE_PROPERTIES") = nb::int_(
        kMetadataDescriptiveTranslationMaxSourceProperties);
    m.attr("METADATA_DESCRIPTIVE_TRANSLATION_MAX_ADDED_ENTRIES") = nb::int_(
        kMetadataDescriptiveTranslationMaxAddedEntries);
    m.attr("METADATA_DESCRIPTIVE_TRANSLATION_MAX_OPERATIONS") = nb::int_(
        kMetadataDescriptiveTranslationMaxOperations);
    m.attr("METADATA_DESCRIPTIVE_TRANSLATION_MAX_TOTAL_TEXT_BYTES") = nb::int_(
        kMetadataDescriptiveTranslationMaxTotalTextBytes);
    m.def("metadata_descriptive_translation_status_name",
          &metadata_descriptive_translation_status_name, "status"_a);
    m.def("metadata_descriptive_translation_mapping_name",
          &metadata_descriptive_translation_mapping_name, "mapping"_a);

    nb::enum_<MetadataFuzzySearchStatus>(m, "MetadataFuzzySearchStatus")
        .value("Ok", MetadataFuzzySearchStatus::Ok)
        .value("FeatureUnavailable",
               MetadataFuzzySearchStatus::FeatureUnavailable)
        .value("EmptyQuery", MetadataFuzzySearchStatus::EmptyQuery)
        .value("QueryTooShort", MetadataFuzzySearchStatus::QueryTooShort)
        .value("QueryTooLong", MetadataFuzzySearchStatus::QueryTooLong)
        .value("UnsupportedQueryText",
               MetadataFuzzySearchStatus::UnsupportedQueryText)
        .value("InvalidOptions", MetadataFuzzySearchStatus::InvalidOptions);

    nb::enum_<MetadataFuzzySearchMatchKind>(m, "MetadataFuzzySearchMatchKind")
        .value("Exact", MetadataFuzzySearchMatchKind::Exact)
        .value("Alias", MetadataFuzzySearchMatchKind::Alias)
        .value("Fuzzy", MetadataFuzzySearchMatchKind::Fuzzy);

    m.attr("METADATA_FUZZY_SEARCH_MAX_RESULTS") = nb::int_(
        kMetadataFuzzySearchMaxResults);
    m.attr("METADATA_FUZZY_SEARCH_MAX_QUERY_BYTES") = nb::int_(
        kMetadataFuzzySearchMaxQueryBytes);
    m.attr("METADATA_FUZZY_SEARCH_MAX_CANDIDATE_BYTES") = nb::int_(
        kMetadataFuzzySearchMaxCandidateBytes);

    m.def("metadata_capability_family_name", &metadata_capability_family_name,
          "family"_a);
    m.def("metadata_capability_support_name", &metadata_capability_support_name,
          "support"_a);
    m.def("metadata_capability_available", &metadata_capability_available,
          "support"_a);
    m.def("metadata_capability", &metadata_capability_query_to_python,
          "format"_a, "family"_a);
    m.def("metadata_query_fuzzy_search_available",
          &metadata_query_fuzzy_search_available);
    m.def("metadata_fuzzy_search_available", &metadata_fuzzy_search_available);
    m.def("metadata_fuzzy_search_status_name",
          &metadata_fuzzy_search_status_name, "status"_a);
    m.def("metadata_fuzzy_search_match_kind_name",
          &metadata_fuzzy_search_match_kind_name, "kind"_a);
    m.def("tiff_compression_name", &tiff_compression_name, "value"_a);
    m.def("tiff_photometric_interpretation_name",
          &tiff_photometric_interpretation_name, "value"_a);
    m.def("tiff_planar_configuration_name", &tiff_planar_configuration_name,
          "value"_a);
    m.def("tiff_resolution_unit_name", &tiff_resolution_unit_name, "value"_a);
    m.def("tiff_ycbcr_positioning_name", &tiff_ycbcr_positioning_name,
          "value"_a);
    m.def("exif_exposure_program_name", &exif_exposure_program_name, "value"_a);
    m.def("exif_exposure_mode_name", &exif_exposure_mode_name, "value"_a);
    m.def("exif_metering_mode_name", &exif_metering_mode_name, "value"_a);
    m.def("exif_light_source_name", &exif_light_source_name, "value"_a);
    m.def("exif_flash_name", &exif_flash_name, "value"_a);
    m.def("exif_color_space_name", &exif_color_space_name, "value"_a);
    m.def("exif_white_balance_name", &exif_white_balance_name, "value"_a);
    m.def("exif_scene_capture_type_name", &exif_scene_capture_type_name,
          "value"_a);
    m.def("exif_gain_control_name", &exif_gain_control_name, "value"_a);
    m.def("exif_sensitivity_type_name", &exif_sensitivity_type_name, "value"_a);
    m.def("exif_focal_plane_resolution_unit_name",
          &exif_focal_plane_resolution_unit_name, "value"_a);
    m.def("exif_sensing_method_name", &exif_sensing_method_name, "value"_a);
    m.def("exif_file_source_name", &exif_file_source_name, "value"_a);
    m.def("exif_scene_type_name", &exif_scene_type_name, "value"_a);
    m.def("exif_custom_rendered_name", &exif_custom_rendered_name, "value"_a);
    m.def("exif_contrast_name", &exif_contrast_name, "value"_a);
    m.def("exif_saturation_name", &exif_saturation_name, "value"_a);
    m.def("exif_sharpness_name", &exif_sharpness_name, "value"_a);
    m.def("exif_subject_distance_range_name", &exif_subject_distance_range_name,
          "value"_a);
    m.def("dng_cfa_layout_name", &dng_cfa_layout_name, "value"_a);
    m.def("dng_calibration_illuminant_name", &dng_calibration_illuminant_name,
          "value"_a);
    m.def("exif_tag_numeric_value_name", &exif_tag_numeric_value_name_python,
          "ifd"_a, "tag"_a, "value"_a);
    m.def("exif_tag_numeric_value_format",
          &exif_tag_numeric_value_format_python, "ifd"_a, "tag"_a, "value"_a);
    m.def("exif_tag_byte_value_format", &exif_tag_byte_value_format_python,
          "ifd"_a, "tag"_a, "value"_a);

    nb::enum_<TransferPolicySubject>(m, "TransferPolicySubject")
        .value("MakerNote", TransferPolicySubject::MakerNote)
        .value("Jumbf", TransferPolicySubject::Jumbf)
        .value("C2pa", TransferPolicySubject::C2pa)
        .value("XmpExifProjection", TransferPolicySubject::XmpExifProjection)
        .value("XmpIptcProjection", TransferPolicySubject::XmpIptcProjection)
        .value("ImageProperties", TransferPolicySubject::ImageProperties)
        .value("IccProfile", TransferPolicySubject::IccProfile)
        .value("RawColorCalibration",
               TransferPolicySubject::RawColorCalibration)
        .value("CameraRawSettings", TransferPolicySubject::CameraRawSettings);

    nb::enum_<TransferPolicyAction>(m, "TransferPolicyAction")
        .value("Keep", TransferPolicyAction::Keep)
        .value("Drop", TransferPolicyAction::Drop)
        .value("Invalidate", TransferPolicyAction::Invalidate)
        .value("Rewrite", TransferPolicyAction::Rewrite);

    nb::enum_<TransferPolicyReason>(m, "TransferPolicyReason")
        .value("Default", TransferPolicyReason::Default)
        .value("NotPresent", TransferPolicyReason::NotPresent)
        .value("ExplicitDrop", TransferPolicyReason::ExplicitDrop)
        .value("CarrierDisabled", TransferPolicyReason::CarrierDisabled)
        .value("ProjectedPayload", TransferPolicyReason::ProjectedPayload)
        .value("DraftInvalidationPayload",
               TransferPolicyReason::DraftInvalidationPayload)
        .value("ExternalSignedPayload",
               TransferPolicyReason::ExternalSignedPayload)
        .value("ContentBoundTransferUnavailable",
               TransferPolicyReason::ContentBoundTransferUnavailable)
        .value("SignedRewriteUnavailable",
               TransferPolicyReason::SignedRewriteUnavailable)
        .value("PortableInvalidationUnavailable",
               TransferPolicyReason::PortableInvalidationUnavailable)
        .value("RewriteUnavailablePreservedRaw",
               TransferPolicyReason::RewriteUnavailablePreservedRaw)
        .value("TargetSerializationUnavailable",
               TransferPolicyReason::TargetSerializationUnavailable)
        .value("TargetImageProperties",
               TransferPolicyReason::TargetImageProperties)
        .value("SafetyModeFiltered", TransferPolicyReason::SafetyModeFiltered)
        .value("RawDataDescriptorFiltered",
               TransferPolicyReason::RawDataDescriptorFiltered)
        .value("OpaquePayloadPreservedUnverified",
               TransferPolicyReason::OpaquePayloadPreservedUnverified)
        .value("RewriteUnavailableDropped",
               TransferPolicyReason::RewriteUnavailableDropped);

    nb::enum_<TransferMakerNoteTrust>(m, "TransferMakerNoteTrust")
        .value("NotPresent", TransferMakerNoteTrust::NotPresent)
        .value("DecodedOnlyNotSerializable",
               TransferMakerNoteTrust::DecodedOnlyNotSerializable)
        .value("OpaquePreservationUnverified",
               TransferMakerNoteTrust::OpaquePreservationUnverified);

    nb::enum_<TransferMakerNoteVendor>(m, "TransferMakerNoteVendor")
        .value("Unknown", TransferMakerNoteVendor::Unknown)
        .value("Nikon", TransferMakerNoteVendor::Nikon)
        .value("Canon", TransferMakerNoteVendor::Canon);

    nb::enum_<TransferMakerNoteLayout>(m, "TransferMakerNoteLayout")
        .value("UnknownOrMixed", TransferMakerNoteLayout::UnknownOrMixed)
        .value("NikonType1OuterTiff",
               TransferMakerNoteLayout::NikonType1OuterTiff)
        .value("NikonType3EmbeddedTiff",
               TransferMakerNoteLayout::NikonType3EmbeddedTiff)
        .value("CanonSourceDependentIfd",
               TransferMakerNoteLayout::CanonSourceDependentIfd);

    nb::enum_<TransferMakerNoteLayoutTrust>(m, "TransferMakerNoteLayoutTrust")
        .value("NotPresent", TransferMakerNoteLayoutTrust::NotPresent)
        .value("UnrecognizedOrMixed",
               TransferMakerNoteLayoutTrust::UnrecognizedOrMixed)
        .value("OuterTiffOffsetsUnsafe",
               TransferMakerNoteLayoutTrust::OuterTiffOffsetsUnsafe)
        .value("SourceOffsetBasisAmbiguous",
               TransferMakerNoteLayoutTrust::SourceOffsetBasisAmbiguous)
        .value("EmbeddedTiffStructureUnverified",
               TransferMakerNoteLayoutTrust::EmbeddedTiffStructureUnverified)
        .value("EmbeddedTiffStructureVerified",
               TransferMakerNoteLayoutTrust::EmbeddedTiffStructureVerified);

    nb::enum_<TransferRawCarrierPassthroughReason>(
        m, "TransferRawCarrierPassthroughReason")
        .value("Candidate", TransferRawCarrierPassthroughReason::Candidate)
        .value("MissingPayload",
               TransferRawCarrierPassthroughReason::MissingPayload)
        .value("TargetIncompatible",
               TransferRawCarrierPassthroughReason::TargetIncompatible)
        .value("SafetyFiltered",
               TransferRawCarrierPassthroughReason::SafetyFiltered)
        .value("ContentBoundMetadata",
               TransferRawCarrierPassthroughReason::ContentBoundMetadata)
        .value("PolicyBlocked",
               TransferRawCarrierPassthroughReason::PolicyBlocked)
        .value("DecodeLinkUnavailable",
               TransferRawCarrierPassthroughReason::DecodeLinkUnavailable)
        .value("UnsupportedKind",
               TransferRawCarrierPassthroughReason::UnsupportedKind);

    nb::enum_<TransferRawCarrierPassthroughMode>(
        m, "TransferRawCarrierPassthroughMode")
        .value("Disabled", TransferRawCarrierPassthroughMode::Disabled)
        .value("WhenSafe", TransferRawCarrierPassthroughMode::WhenSafe);

    nb::enum_<TransferC2paMode>(m, "TransferC2paMode")
        .value("NotApplicable", TransferC2paMode::NotApplicable)
        .value("NotPresent", TransferC2paMode::NotPresent)
        .value("Drop", TransferC2paMode::Drop)
        .value("DraftUnsignedInvalidation",
               TransferC2paMode::DraftUnsignedInvalidation)
        .value("PreserveRaw", TransferC2paMode::PreserveRaw)
        .value("SignedRewrite", TransferC2paMode::SignedRewrite);

    nb::enum_<TransferC2paSourceKind>(m, "TransferC2paSourceKind")
        .value("NotApplicable", TransferC2paSourceKind::NotApplicable)
        .value("NotPresent", TransferC2paSourceKind::NotPresent)
        .value("DecodedOnly", TransferC2paSourceKind::DecodedOnly)
        .value("ContentBound", TransferC2paSourceKind::ContentBound)
        .value("DraftUnsignedInvalidation",
               TransferC2paSourceKind::DraftUnsignedInvalidation);

    nb::enum_<TransferC2paPreparedOutput>(m, "TransferC2paPreparedOutput")
        .value("NotApplicable", TransferC2paPreparedOutput::NotApplicable)
        .value("NotPresent", TransferC2paPreparedOutput::NotPresent)
        .value("Dropped", TransferC2paPreparedOutput::Dropped)
        .value("PreservedRaw", TransferC2paPreparedOutput::PreservedRaw)
        .value("GeneratedDraftUnsignedInvalidation",
               TransferC2paPreparedOutput::GeneratedDraftUnsignedInvalidation)
        .value("SignedRewrite", TransferC2paPreparedOutput::SignedRewrite);

    nb::enum_<TransferC2paRewriteState>(m, "TransferC2paRewriteState")
        .value("NotApplicable", TransferC2paRewriteState::NotApplicable)
        .value("NotRequested", TransferC2paRewriteState::NotRequested)
        .value("SigningMaterialRequired",
               TransferC2paRewriteState::SigningMaterialRequired)
        .value("Ready", TransferC2paRewriteState::Ready);

    nb::enum_<TransferC2paRewriteChunkKind>(m, "TransferC2paRewriteChunkKind")
        .value("SourceRange", TransferC2paRewriteChunkKind::SourceRange)
        .value("PreparedJpegSegment",
               TransferC2paRewriteChunkKind::PreparedJpegSegment)
        .value("PreparedJxlBox", TransferC2paRewriteChunkKind::PreparedJxlBox)
        .value("PreparedBmffMetaBox",
               TransferC2paRewriteChunkKind::PreparedBmffMetaBox);

    nb::enum_<TransferC2paSignedPayloadKind>(m, "TransferC2paSignedPayloadKind")
        .value("NotApplicable", TransferC2paSignedPayloadKind::NotApplicable)
        .value("GenericJumbf", TransferC2paSignedPayloadKind::GenericJumbf)
        .value("DraftUnsignedInvalidation",
               TransferC2paSignedPayloadKind::DraftUnsignedInvalidation)
        .value("ContentBound", TransferC2paSignedPayloadKind::ContentBound);

    nb::enum_<TransferC2paSemanticStatus>(m, "TransferC2paSemanticStatus")
        .value("NotChecked", TransferC2paSemanticStatus::NotChecked)
        .value("Ok", TransferC2paSemanticStatus::Ok)
        .value("Invalid", TransferC2paSemanticStatus::Invalid);

    nb::enum_<TransferStatus>(m, "TransferStatus")
        .value("Ok", TransferStatus::Ok)
        .value("InvalidArgument", TransferStatus::InvalidArgument)
        .value("Unsupported", TransferStatus::Unsupported)
        .value("LimitExceeded", TransferStatus::LimitExceeded)
        .value("Malformed", TransferStatus::Malformed)
        .value("UnsafeData", TransferStatus::UnsafeData)
        .value("InternalError", TransferStatus::InternalError);

    nb::enum_<PrepareTransferCode>(m, "PrepareTransferCode")
        .value("None", PrepareTransferCode::None)
        .value("NullOutBundle", PrepareTransferCode::NullOutBundle)
        .value("UnsupportedTargetFormat",
               PrepareTransferCode::UnsupportedTargetFormat)
        .value("ExifPackFailed", PrepareTransferCode::ExifPackFailed)
        .value("XmpPackFailed", PrepareTransferCode::XmpPackFailed)
        .value("IccPackFailed", PrepareTransferCode::IccPackFailed)
        .value("IptcPackFailed", PrepareTransferCode::IptcPackFailed)
        .value("RequestedMetadataNotSerializable",
               PrepareTransferCode::RequestedMetadataNotSerializable);

    nb::enum_<EmitTransferCode>(m, "EmitTransferCode")
        .value("None", EmitTransferCode::None)
        .value("InvalidArgument", EmitTransferCode::InvalidArgument)
        .value("BundleTargetNotJpeg", EmitTransferCode::BundleTargetNotJpeg)
        .value("UnsupportedRoute", EmitTransferCode::UnsupportedRoute)
        .value("InvalidPayload", EmitTransferCode::InvalidPayload)
        .value("ContentBoundPayloadUnsupported",
               EmitTransferCode::ContentBoundPayloadUnsupported)
        .value("BackendWriteFailed", EmitTransferCode::BackendWriteFailed)
        .value("PlanMismatch", EmitTransferCode::PlanMismatch);

    nb::enum_<PrepareTransferFileCode>(m, "PrepareTransferFileCode")
        .value("None", PrepareTransferFileCode::None)
        .value("EmptyPath", PrepareTransferFileCode::EmptyPath)
        .value("MapFailed", PrepareTransferFileCode::MapFailed)
        .value("PayloadBufferPlatformLimit",
               PrepareTransferFileCode::PayloadBufferPlatformLimit)
        .value("DecodeFailed", PrepareTransferFileCode::DecodeFailed);

    nb::enum_<ReadTransferSourceSnapshotFileCode>(
        m, "ReadTransferSourceSnapshotFileCode")
        .value("None_", ReadTransferSourceSnapshotFileCode::None)
        .value("EmptyPath", ReadTransferSourceSnapshotFileCode::EmptyPath)
        .value("MapFailed", ReadTransferSourceSnapshotFileCode::MapFailed)
        .value("PayloadBufferPlatformLimit",
               ReadTransferSourceSnapshotFileCode::PayloadBufferPlatformLimit)
        .value("DecodeFailed",
               ReadTransferSourceSnapshotFileCode::DecodeFailed);

    nb::enum_<ReadTransferSourceSnapshotBytesCode>(
        m, "ReadTransferSourceSnapshotBytesCode")
        .value("None_", ReadTransferSourceSnapshotBytesCode::None)
        .value("PayloadBufferPlatformLimit",
               ReadTransferSourceSnapshotBytesCode::PayloadBufferPlatformLimit)
        .value("DecodeFailed",
               ReadTransferSourceSnapshotBytesCode::DecodeFailed);

    nb::enum_<TransferFileStatus>(m, "TransferFileStatus")
        .value("Ok", TransferFileStatus::Ok)
        .value("InvalidArgument", TransferFileStatus::InvalidArgument)
        .value("OpenFailed", TransferFileStatus::OpenFailed)
        .value("StatFailed", TransferFileStatus::StatFailed)
        .value("TooLarge", TransferFileStatus::TooLarge)
        .value("MapFailed", TransferFileStatus::MapFailed)
        .value("ReadFailed", TransferFileStatus::ReadFailed);

    nb::enum_<TransferBlockKind>(m, "TransferBlockKind")
        .value("Exif", TransferBlockKind::Exif)
        .value("Xmp", TransferBlockKind::Xmp)
        .value("IptcIim", TransferBlockKind::IptcIim)
        .value("PhotoshopIrb", TransferBlockKind::PhotoshopIrb)
        .value("Icc", TransferBlockKind::Icc)
        .value("Jumbf", TransferBlockKind::Jumbf)
        .value("C2pa", TransferBlockKind::C2pa)
        .value("ExrAttribute", TransferBlockKind::ExrAttribute)
        .value("Other", TransferBlockKind::Other);

    nb::class_<PayloadLimits>(m, "PayloadLimits")
        .def(nb::init<>())
        .def_rw("max_parts", &PayloadLimits::max_parts)
        .def_rw("max_output_bytes", &PayloadLimits::max_output_bytes);

    nb::class_<ExifDecodeLimits>(m, "ExifDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_ifds", &ExifDecodeLimits::max_ifds)
        .def_rw("max_entries_per_ifd", &ExifDecodeLimits::max_entries_per_ifd)
        .def_rw("max_total_entries", &ExifDecodeLimits::max_total_entries)
        .def_rw("max_value_bytes", &ExifDecodeLimits::max_value_bytes)
        .def_rw("max_arena_bytes", &ExifDecodeLimits::max_arena_bytes);

    nb::class_<XmpDecodeLimits>(m, "XmpDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_depth", &XmpDecodeLimits::max_depth)
        .def_rw("max_properties", &XmpDecodeLimits::max_properties)
        .def_rw("max_input_bytes", &XmpDecodeLimits::max_input_bytes)
        .def_rw("max_path_bytes", &XmpDecodeLimits::max_path_bytes)
        .def_rw("max_namespace_bytes", &XmpDecodeLimits::max_namespace_bytes)
        .def_rw("max_value_bytes", &XmpDecodeLimits::max_value_bytes)
        .def_rw("max_total_value_bytes",
                &XmpDecodeLimits::max_total_value_bytes)
        .def_rw("max_arena_bytes", &XmpDecodeLimits::max_arena_bytes);

    nb::class_<ExrDecodeLimits>(m, "ExrDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_parts", &ExrDecodeLimits::max_parts)
        .def_rw("max_attributes_per_part",
                &ExrDecodeLimits::max_attributes_per_part)
        .def_rw("max_attributes", &ExrDecodeLimits::max_attributes)
        .def_rw("max_name_bytes", &ExrDecodeLimits::max_name_bytes)
        .def_rw("max_type_name_bytes", &ExrDecodeLimits::max_type_name_bytes)
        .def_rw("max_attribute_bytes", &ExrDecodeLimits::max_attribute_bytes)
        .def_rw("max_total_attribute_bytes",
                &ExrDecodeLimits::max_total_attribute_bytes);

    nb::class_<JumbfDecodeLimits>(m, "JumbfDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_input_bytes", &JumbfDecodeLimits::max_input_bytes)
        .def_rw("max_box_depth", &JumbfDecodeLimits::max_box_depth)
        .def_rw("max_boxes", &JumbfDecodeLimits::max_boxes)
        .def_rw("max_entries", &JumbfDecodeLimits::max_entries)
        .def_rw("max_cbor_depth", &JumbfDecodeLimits::max_cbor_depth)
        .def_rw("max_cbor_items", &JumbfDecodeLimits::max_cbor_items)
        .def_rw("max_cbor_key_bytes", &JumbfDecodeLimits::max_cbor_key_bytes)
        .def_rw("max_cbor_text_bytes", &JumbfDecodeLimits::max_cbor_text_bytes)
        .def_rw("max_cbor_bytes_bytes",
                &JumbfDecodeLimits::max_cbor_bytes_bytes);

    nb::class_<IccDecodeLimits>(m, "IccDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_tags", &IccDecodeLimits::max_tags)
        .def_rw("max_tag_bytes", &IccDecodeLimits::max_tag_bytes)
        .def_rw("max_total_tag_bytes", &IccDecodeLimits::max_total_tag_bytes);

    nb::class_<IptcIimDecodeLimits>(m, "IptcIimDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_datasets", &IptcIimDecodeLimits::max_datasets)
        .def_rw("max_dataset_bytes", &IptcIimDecodeLimits::max_dataset_bytes)
        .def_rw("max_total_bytes", &IptcIimDecodeLimits::max_total_bytes);

    nb::class_<PhotoshopIrbDecodeLimits>(m, "PhotoshopIrbDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_resources", &PhotoshopIrbDecodeLimits::max_resources)
        .def_rw("max_total_bytes", &PhotoshopIrbDecodeLimits::max_total_bytes)
        .def_rw("max_resource_len",
                &PhotoshopIrbDecodeLimits::max_resource_len);

    nb::class_<PreviewScanLimits>(m, "PreviewScanLimits")
        .def(nb::init<>())
        .def_rw("max_ifds", &PreviewScanLimits::max_ifds)
        .def_rw("max_total_entries", &PreviewScanLimits::max_total_entries)
        .def_rw("max_preview_bytes", &PreviewScanLimits::max_preview_bytes);

    nb::class_<XmpDumpLimits>(m, "XmpDumpLimits")
        .def(nb::init<>())
        .def_rw("max_output_bytes", &XmpDumpLimits::max_output_bytes)
        .def_rw("max_entries", &XmpDumpLimits::max_entries);

    nb::class_<OpenMetaResourcePolicy>(m, "ResourcePolicy")
        .def(nb::init<>())
        .def_rw("max_file_bytes", &OpenMetaResourcePolicy::max_file_bytes)
        .def_rw("payload_limits", &OpenMetaResourcePolicy::payload_limits)
        .def_rw("exif_limits", &OpenMetaResourcePolicy::exif_limits)
        .def_rw("xmp_limits", &OpenMetaResourcePolicy::xmp_limits)
        .def_rw("exr_limits", &OpenMetaResourcePolicy::exr_limits)
        .def_rw("jumbf_limits", &OpenMetaResourcePolicy::jumbf_limits)
        .def_rw("icc_limits", &OpenMetaResourcePolicy::icc_limits)
        .def_rw("iptc_limits", &OpenMetaResourcePolicy::iptc_limits)
        .def_rw("photoshop_irb_limits",
                &OpenMetaResourcePolicy::photoshop_irb_limits)
        .def_rw("preview_scan_limits",
                &OpenMetaResourcePolicy::preview_scan_limits)
        .def_rw("max_preview_output_bytes",
                &OpenMetaResourcePolicy::max_preview_output_bytes)
        .def_rw("xmp_dump_limits", &OpenMetaResourcePolicy::xmp_dump_limits)
        .def_rw("max_decode_millis", &OpenMetaResourcePolicy::max_decode_millis)
        .def_rw("max_decompression_ratio",
                &OpenMetaResourcePolicy::max_decompression_ratio)
        .def_rw("max_total_decode_work_bytes",
                &OpenMetaResourcePolicy::max_total_decode_work_bytes);

    nb::class_<XmpDumpResult>(m, "XmpDumpResult")
        .def_ro("status", &XmpDumpResult::status)
        .def_ro("written", &XmpDumpResult::written)
        .def_ro("needed", &XmpDumpResult::needed)
        .def_ro("entries", &XmpDumpResult::entries);

    nb::class_<ContainerBlockRef>(m, "BlockRef")
        .def(nb::init<>())
        .def_ro("format", &ContainerBlockRef::format)
        .def_ro("kind", &ContainerBlockRef::kind)
        .def_ro("compression", &ContainerBlockRef::compression)
        .def_ro("chunking", &ContainerBlockRef::chunking)
        .def_ro("outer_offset", &ContainerBlockRef::outer_offset)
        .def_ro("outer_size", &ContainerBlockRef::outer_size)
        .def_ro("data_offset", &ContainerBlockRef::data_offset)
        .def_ro("data_size", &ContainerBlockRef::data_size)
        .def_ro("id", &ContainerBlockRef::id)
        .def_ro("part_index", &ContainerBlockRef::part_index)
        .def_ro("part_count", &ContainerBlockRef::part_count)
        .def_ro("logical_offset", &ContainerBlockRef::logical_offset)
        .def_ro("logical_size", &ContainerBlockRef::logical_size)
        .def_ro("group", &ContainerBlockRef::group)
        .def_ro("aux_u32", &ContainerBlockRef::aux_u32);

    nb::class_<TransferSourceSnapshot>(m, "TransferSourceSnapshot")
        .def_prop_ro("entry_count",
                     [](const TransferSourceSnapshot& snapshot) {
                         return static_cast<uint64_t>(
                             snapshot.store.entries().size());
                     })
        .def_prop_ro("raw_carrier_count",
                     &transfer_source_snapshot_raw_carrier_count)
        .def_prop_ro("raw_carrier_bytes",
                     &transfer_source_snapshot_raw_carrier_bytes)
        .def_prop_ro("raw_carrier_bytes_truncated",
                     &transfer_source_snapshot_raw_carrier_bytes_truncated)
        .def("raw_carriers", &transfer_source_snapshot_raw_carriers_to_python,
             "include_payload"_a = false)
        .def(
            "export_names",
            [](const TransferSourceSnapshot& snapshot, ExportNameStyle style,
               ExportNamePolicy name_policy, bool include_makernotes) {
                ExportOptions options;
                options.style              = style;
                options.name_policy        = name_policy;
                options.include_makernotes = include_makernotes;
                return export_names(snapshot.store, options);
            },
            "style"_a              = ExportNameStyle::Canonical,
            "name_policy"_a        = ExportNamePolicy::ExifToolAlias,
            "include_makernotes"_a = true)
        .def("compatibility_dump", &snapshot_compatibility_dump,
             "style"_a          = ExportNameStyle::FlatHost,
             "name_policy"_a    = ExportNamePolicy::ExifToolAlias,
             "include_values"_a = true, "include_origins"_a = true,
             "include_flags"_a = true, "max_value_bytes"_a = 256U)
        .def("phaseone_raw_geometry", &snapshot_phaseone_raw_geometry)
        .def("phaseone_raw_processing", &snapshot_phaseone_raw_processing)
        .def("vendor_raw_processing", &snapshot_vendor_raw_processing)
        .def("metadata_query", &snapshot_metadata_query,
             "kind"_a = MetadataQueryKind::Crop)
        .def("fuzzy_search", &snapshot_fuzzy_search, "query"_a,
             "minimum_score"_a = 80U, "max_results"_a = 16U)
        .def("query_crop_metadata", &snapshot_query_crop_metadata)
        .def("query_exposure_gain_metadata",
             &snapshot_query_exposure_gain_metadata)
        .def("query_white_balance_metadata",
             &snapshot_query_white_balance_metadata)
        .def("query_color_metadata", &snapshot_query_color_metadata)
        .def("query_lens_correction_metadata",
             &snapshot_query_lens_correction_metadata)
        .def("query_orientation_metadata", &snapshot_query_orientation_metadata)
        .def("query_raw_processing_metadata",
             &snapshot_query_raw_processing_metadata)
        .def("interpret_metadata", &snapshot_interpret_metadata)
        .def("interpret_metadata_query", &snapshot_interpret_metadata_query,
             "kind"_a = MetadataQueryKind::Crop)
        .def("resolve_metadata_concepts", &snapshot_resolve_metadata_concepts)
        .def("resolve_metadata_concepts",
             &snapshot_resolve_metadata_concepts_with_raw_descriptor,
             "raw_descriptor"_a)
        .def("resolve_metadata_concept", &snapshot_resolve_metadata_concept,
             "kind"_a = MetadataConceptKind::Orientation)
        .def("resolve_metadata_concept",
             &snapshot_resolve_metadata_concept_with_raw_descriptor, "kind"_a,
             "raw_descriptor"_a)
        .def("transfer_safety_audit", &snapshot_transfer_safety_audit,
             "safety"_a = TransferSafetyMode::RenderedImage)
        .def("makernote_transfer_audit", &snapshot_makernote_transfer_audit)
        .def("makernote_layout_transfer_audit",
             &snapshot_makernote_layout_transfer_audit)
        .def("transfer_concept_diagnostics",
             &snapshot_transfer_concept_diagnostics,
             "safety"_a = TransferSafetyMode::RenderedImage)
        .def("transfer_concept_diagnostics",
             &snapshot_transfer_concept_diagnostics_with_raw_descriptor,
             "safety"_a, "raw_descriptor"_a)
        .def("raw_carrier_passthrough_audit",
             &snapshot_raw_carrier_passthrough_audit,
             "target_format"_a    = TransferTargetFormat::Jpeg,
             "safety"_a           = TransferSafetyMode::CompatibleFile,
             "makernote_policy"_a = TransferPolicyAction::Keep,
             "jumbf_policy"_a     = TransferPolicyAction::Keep,
             "c2pa_policy"_a      = TransferPolicyAction::Keep,
             "require_decoded_entry_links"_a = true)
        .def("__repr__", [](const TransferSourceSnapshot& snapshot) {
            std::string text = "TransferSourceSnapshot(entry_count=";
            text.append(std::to_string(static_cast<unsigned long long>(
                snapshot.store.entries().size())));
            text.push_back(')');
            return text;
        });

    nb::class_<PyDocument>(m, "Document")
        .def_prop_ro("path", [](const PyDocument& d) { return d.path; })
        .def_prop_ro("file_size",
                     [](const PyDocument& d) {
                         return static_cast<uint64_t>(d.file_bytes.size());
                     })
        .def_prop_ro("scan_status",
                     [](const PyDocument& d) { return d.result.scan.status; })
        .def_prop_ro("scan_written",
                     [](const PyDocument& d) { return d.result.scan.written; })
        .def_prop_ro("scan_needed",
                     [](const PyDocument& d) { return d.result.scan.needed; })
        .def_prop_ro("payload_status",
                     [](const PyDocument& d) { return d.result.payload.status; })
        .def_prop_ro("payload_written",
                     [](const PyDocument& d) {
                         return static_cast<uint64_t>(d.result.payload.written);
                     })
        .def_prop_ro("payload_needed",
                     [](const PyDocument& d) {
                         return static_cast<uint64_t>(d.result.payload.needed);
                     })
        .def_prop_ro("xmp_status",
                     [](const PyDocument& d) { return d.result.xmp.status; })
        .def_prop_ro("xmp_entries_decoded",
                     [](const PyDocument& d) {
                         return d.result.xmp.entries_decoded;
                     })
        .def_prop_ro("jumbf_status",
                     [](const PyDocument& d) { return d.result.jumbf.status; })
        .def_prop_ro("jumbf_boxes_decoded",
                     [](const PyDocument& d) {
                         return d.result.jumbf.boxes_decoded;
                     })
        .def_prop_ro("jumbf_cbor_items",
                     [](const PyDocument& d) {
                         return d.result.jumbf.cbor_items;
                     })
        .def_prop_ro("jumbf_entries_decoded",
                     [](const PyDocument& d) {
                         return d.result.jumbf.entries_decoded;
                     })
        .def_prop_ro("jumbf_verify_status",
                     [](const PyDocument& d) {
                         return d.result.jumbf.verify_status;
                     })
        .def_prop_ro("jumbf_verify_backend",
                     [](const PyDocument& d) {
                         return d.result.jumbf.verify_backend_selected;
                     })
        .def_prop_ro("exif_status",
                     [](const PyDocument& d) { return d.result.exif.status; })
        .def_prop_ro("exif_ifds_decoded",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(
                             d.result.exif.ifds_written);
                     })
        .def_prop_ro("exif_ifds_needed",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(
                             d.result.exif.ifds_needed);
                     })
        .def_prop_ro("exif_entries_decoded",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(
                             d.result.exif.entries_decoded);
                     })
        .def_prop_ro("exif_limit_reason",
                     [](const PyDocument& d) {
                         return d.result.exif.limit_reason;
                     })
        .def_prop_ro("exif_limit_ifd_offset",
                     [](const PyDocument& d) {
                         return static_cast<uint64_t>(
                             d.result.exif.limit_ifd_offset);
                     })
        .def_prop_ro("exif_limit_tag",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(d.result.exif.limit_tag);
                     })
        .def_prop_ro("exr_status",
                     [](const PyDocument& d) { return d.result.exr.status; })
        .def_prop_ro("exr_parts_decoded",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(
                             d.result.exr.parts_decoded);
                     })
        .def_prop_ro("exr_entries_decoded",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(
                             d.result.exr.entries_decoded);
                     })
        .def_prop_ro("entry_count",
                     [](const PyDocument& d) {
                         return static_cast<uint64_t>(d.store.entries().size());
                     })
        .def_prop_ro("block_count",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(d.store.block_count());
                     })
        .def_prop_ro("blocks", [](const PyDocument& d) { return d.blocks; })
        .def(
            "export_names",
            [](std::shared_ptr<PyDocument> d, ExportNameStyle style,
               ExportNamePolicy name_policy, bool include_makernotes) {
                ExportOptions options;
                options.style              = style;
                options.name_policy        = name_policy;
                options.include_makernotes = include_makernotes;
                return export_names(d->store, options);
            },
            "style"_a              = ExportNameStyle::Canonical,
            "name_policy"_a        = ExportNamePolicy::ExifToolAlias,
            "include_makernotes"_a = true)
        .def("compatibility_dump", &document_compatibility_dump,
             "style"_a          = ExportNameStyle::FlatHost,
             "name_policy"_a    = ExportNamePolicy::ExifToolAlias,
             "include_values"_a = true, "include_origins"_a = true,
             "include_flags"_a = true, "max_value_bytes"_a = 256U)
        .def("edit_metadata", &edit_metadata_document, "operations"_a,
             "max_operations"_a = kMetadataEditingMaxOperations,
             "max_text_bytes_per_operation"_a
             = kMetadataEditingMaxTextBytesPerOperation,
             "max_total_text_bytes"_a = kMetadataEditingMaxTotalTextBytes)
        .def("translate_creation_dates", &translate_creation_dates_document,
             "source_mode"_a = MetadataDateTranslationSourceMode::DirtyOnly,
             "conflict_policy"_a
             = MetadataDateTranslationConflictPolicy::FailOnConflict,
             "create_date_to_exif_digitized"_a        = true,
             "create_date_to_iptc_digital_creation"_a = true,
             "date_created_to_iptc_created"_a         = true,
             "date_time_original_to_exif_original"_a  = true,
             "max_added_entries"_a = kMetadataDateTranslationMaxAddedEntries,
             "max_operations"_a    = kMetadataDateTranslationMaxOperations)
        .def("translate_technical_metadata",
             &translate_technical_metadata_document,
             "source_mode"_a = MetadataTechnicalTranslationSourceMode::DirtyOnly,
             "conflict_policy"_a
             = MetadataTechnicalTranslationConflictPolicy::FailOnConflict,
             "modify_date_to_exif_datetime"_a = true,
             "make_to_exif_make"_a = true, "model_to_exif_model"_a = true,
             "creator_tool_to_exif_software"_a = true,
             "max_added_entries"_a
             = kMetadataTechnicalTranslationMaxAddedEntries,
             "max_operations"_a = kMetadataTechnicalTranslationMaxOperations,
             "max_text_bytes_per_property"_a
             = kMetadataTechnicalTranslationMaxTextBytesPerProperty,
             "max_total_text_bytes"_a
             = kMetadataTechnicalTranslationMaxTotalTextBytes)
        .def("translate_capture_metadata", &translate_capture_metadata_document,
             "source_mode"_a = MetadataCaptureTranslationSourceMode::DirtyOnly,
             "conflict_policy"_a
             = MetadataCaptureTranslationConflictPolicy::FailOnConflict,
             "exposure_time_to_exif"_a = true, "f_number_to_exif"_a = true,
             "iso_to_exif"_a = true, "focal_length_to_exif"_a = true,
             "exposure_compensation_to_exif"_a = true,
             "max_added_entries"_a = kMetadataCaptureTranslationMaxAddedEntries,
             "max_operations"_a    = kMetadataCaptureTranslationMaxOperations,
             "max_text_bytes_per_property"_a
             = kMetadataCaptureTranslationMaxTextBytesPerProperty,
             "max_total_text_bytes"_a
             = kMetadataCaptureTranslationMaxTotalTextBytes)
        .def("translate_image_geometry", &translate_image_geometry_document,
             "target_image_spec"_a,
             "source_mode"_a = MetadataGeometryTranslationSourceMode::DirtyOnly,
             "conflict_policy"_a
             = MetadataGeometryTranslationConflictPolicy::FailOnConflict,
             "orientation_to_exif"_a = true, "dimensions_to_exif"_a = true,
             "max_added_entries"_a = kMetadataGeometryTranslationMaxAddedEntries,
             "max_operations"_a = kMetadataGeometryTranslationMaxOperations,
             "max_text_bytes_per_property"_a
             = kMetadataGeometryTranslationMaxTextBytesPerProperty,
             "max_total_text_bytes"_a
             = kMetadataGeometryTranslationMaxTotalTextBytes)
        .def("translate_descriptive_metadata",
             &translate_descriptive_metadata_document,
             "source_mode"_a
             = MetadataDescriptiveTranslationSourceMode::DirtyOnly,
             "conflict_policy"_a
             = MetadataDescriptiveTranslationConflictPolicy::FailOnConflict,
             "title_to_iptc_object_name"_a   = true,
             "description_to_iptc_caption"_a = true,
             "creators_to_iptc_bylines"_a    = true,
             "keywords_to_iptc_keywords"_a   = true,
             "copyright_to_iptc_copyright"_a = true,
             "credit_to_iptc_credit"_a = true, "source_to_iptc_source"_a = true,
             "max_source_properties"_a
             = kMetadataDescriptiveTranslationMaxSourceProperties,
             "max_added_entries"_a
             = kMetadataDescriptiveTranslationMaxAddedEntries,
             "max_operations"_a = kMetadataDescriptiveTranslationMaxOperations,
             "max_total_text_bytes"_a
             = kMetadataDescriptiveTranslationMaxTotalTextBytes)
        .def("phaseone_raw_geometry", &document_phaseone_raw_geometry)
        .def("phaseone_raw_processing", &document_phaseone_raw_processing)
        .def("vendor_raw_processing", &document_vendor_raw_processing)
        .def("metadata_query", &document_metadata_query,
             "kind"_a = MetadataQueryKind::Crop)
        .def("fuzzy_search", &document_fuzzy_search, "query"_a,
             "minimum_score"_a = 80U, "max_results"_a = 16U)
        .def("query_crop_metadata", &document_query_crop_metadata)
        .def("query_exposure_gain_metadata",
             &document_query_exposure_gain_metadata)
        .def("query_white_balance_metadata",
             &document_query_white_balance_metadata)
        .def("query_color_metadata", &document_query_color_metadata)
        .def("query_lens_correction_metadata",
             &document_query_lens_correction_metadata)
        .def("query_orientation_metadata", &document_query_orientation_metadata)
        .def("query_raw_processing_metadata",
             &document_query_raw_processing_metadata)
        .def("interpret_metadata", &document_interpret_metadata)
        .def("interpret_metadata_query", &document_interpret_metadata_query,
             "kind"_a = MetadataQueryKind::Crop)
        .def("resolve_metadata_concepts", &document_resolve_metadata_concepts)
        .def("resolve_metadata_concepts",
             &document_resolve_metadata_concepts_with_raw_descriptor,
             "raw_descriptor"_a)
        .def("resolve_metadata_concept", &document_resolve_metadata_concept,
             "kind"_a = MetadataConceptKind::Orientation)
        .def("resolve_metadata_concept",
             &document_resolve_metadata_concept_with_raw_descriptor, "kind"_a,
             "raw_descriptor"_a)
        .def("transfer_safety_audit", &document_transfer_safety_audit,
             "safety"_a = TransferSafetyMode::RenderedImage)
        .def("makernote_transfer_audit", &document_makernote_transfer_audit)
        .def("makernote_layout_transfer_audit",
             &document_makernote_layout_transfer_audit)
        .def("transfer_concept_diagnostics",
             &document_transfer_concept_diagnostics,
             "safety"_a = TransferSafetyMode::RenderedImage)
        .def("transfer_concept_diagnostics",
             &document_transfer_concept_diagnostics_with_raw_descriptor,
             "safety"_a, "raw_descriptor"_a)
        .def(
            "dng_ccm_fields",
            [](std::shared_ptr<PyDocument> d, bool require_dng_context,
               bool include_reduction_matrices, uint32_t max_fields,
               uint32_t max_values_per_field,
               CcmValidationMode validation_mode) {
                return collect_dng_ccm_to_python(d->store, require_dng_context,
                                                 include_reduction_matrices,
                                                 max_fields,
                                                 max_values_per_field,
                                                 validation_mode);
            },
            "require_dng_context"_a        = true,
            "include_reduction_matrices"_a = true, "max_fields"_a = 128U,
            "max_values_per_field"_a = 256U,
            "validation_mode"_a      = CcmValidationMode::DngSpecWarnings)
        .def(
            "ocio_metadata_tree",
            [](std::shared_ptr<PyDocument> d, ExportNameStyle style,
               ExportNamePolicy name_policy, uint32_t max_value_bytes,
               bool include_makernotes, bool include_empty) {
                return ocio_tree_to_python(d->store, style, name_policy,
                                           max_value_bytes, include_makernotes,
                                           include_empty);
            },
            "style"_a           = ExportNameStyle::XmpPortable,
            "name_policy"_a     = ExportNamePolicy::ExifToolAlias,
            "max_value_bytes"_a = 1024U, "include_makernotes"_a = false,
            "include_empty"_a = false)
        .def(
            "unsafe_ocio_metadata_tree",
            [](std::shared_ptr<PyDocument> d, ExportNameStyle style,
               ExportNamePolicy name_policy, uint32_t max_value_bytes,
               bool include_makernotes, bool include_empty) {
                return unsafe_ocio_metadata_tree_to_python(d->store, style,
                                                           name_policy,
                                                           max_value_bytes,
                                                           include_makernotes,
                                                           include_empty);
            },
            "style"_a           = ExportNameStyle::XmpPortable,
            "name_policy"_a     = ExportNamePolicy::ExifToolAlias,
            "max_value_bytes"_a = 1024U, "include_makernotes"_a = false,
            "include_empty"_a = false)
        .def(
            "dump_xmp_lossless",
            [](std::shared_ptr<PyDocument> d, uint64_t max_output_bytes,
               uint32_t max_entries, bool include_origin, bool include_wire,
               bool include_flags, bool include_names) {
                const XmpSidecarRequest request = make_xmp_sidecar_request(
                    XmpSidecarFormat::Lossless, max_output_bytes, max_entries,
                    true, true, false, false,
                    XmpExistingNamespacePolicy::KnownPortableOnly,
                    XmpExistingStandardNamespacePolicy::PreserveAll,
                    XmpConflictPolicy::CurrentBehavior, include_origin,
                    include_wire, include_flags, include_names);
                return dump_xmp_sidecar_to_python(d->store, request);
            },
            "max_output_bytes"_a = 0ULL, "max_entries"_a = 0U,
            "include_origin"_a = true, "include_wire"_a = true,
            "include_flags"_a = true, "include_names"_a = true)
        .def(
            "dump_xmp_portable",
            [](std::shared_ptr<PyDocument> d, uint64_t max_output_bytes,
               uint32_t max_entries, bool include_exif,
               bool include_existing_xmp, bool exiftool_gpsdatetime_alias,
               bool include_iptc,
               XmpExistingNamespacePolicy existing_namespace_policy,
               XmpExistingStandardNamespacePolicy
                   existing_standard_namespace_policy,
               XmpConflictPolicy conflict_policy) {
                const XmpSidecarRequest request = make_xmp_sidecar_request(
                    XmpSidecarFormat::Portable, max_output_bytes, max_entries,
                    include_exif, include_iptc, include_existing_xmp,
                    exiftool_gpsdatetime_alias, existing_namespace_policy,
                    existing_standard_namespace_policy, conflict_policy, true,
                    true, true, true);
                return dump_xmp_sidecar_to_python(d->store, request);
            },
            "max_output_bytes"_a = 0ULL, "max_entries"_a = 0U,
            "include_exif"_a = true, "include_existing_xmp"_a = false,
            "exiftool_gpsdatetime_alias"_a = false, "include_iptc"_a = true,
            "existing_namespace_policy"_a
            = XmpExistingNamespacePolicy::KnownPortableOnly,
            "existing_standard_namespace_policy"_a
            = XmpExistingStandardNamespacePolicy::PreserveAll,
            "conflict_policy"_a = XmpConflictPolicy::CurrentBehavior)
        .def(
            "dump_xmp_sidecar",
            [](std::shared_ptr<PyDocument> d, XmpSidecarFormat format,
               uint64_t max_output_bytes, uint32_t max_entries,
               bool include_exif, bool include_existing_xmp,
               bool portable_exiftool_gpsdatetime_alias, bool include_origin,
               bool include_wire, bool include_flags, bool include_names,
               bool include_iptc,
               XmpExistingNamespacePolicy existing_namespace_policy,
               XmpExistingStandardNamespacePolicy
                   existing_standard_namespace_policy,
               XmpConflictPolicy conflict_policy) {
                const XmpSidecarRequest request = make_xmp_sidecar_request(
                    format, max_output_bytes, max_entries, include_exif,
                    include_iptc, include_existing_xmp,
                    portable_exiftool_gpsdatetime_alias,
                    existing_namespace_policy,
                    existing_standard_namespace_policy, conflict_policy,
                    include_origin, include_wire, include_flags, include_names);
                return dump_xmp_sidecar_to_python(d->store, request);
            },
            "format"_a           = XmpSidecarFormat::Lossless,
            "max_output_bytes"_a = 0ULL, "max_entries"_a = 0U,
            "include_exif"_a = true, "include_existing_xmp"_a = false,
            "portable_exiftool_gpsdatetime_alias"_a = false,
            "include_origin"_a = true, "include_wire"_a = true,
            "include_flags"_a = true, "include_names"_a = true,
            "include_iptc"_a = true,
            "existing_namespace_policy"_a
            = XmpExistingNamespacePolicy::KnownPortableOnly,
            "existing_standard_namespace_policy"_a
            = XmpExistingStandardNamespacePolicy::PreserveAll,
            "conflict_policy"_a = XmpConflictPolicy::CurrentBehavior)
        .def("build_transfer_source_snapshot",
             [](std::shared_ptr<PyDocument> d) {
                 return build_transfer_source_snapshot(d->store);
             })
        .def(
            "extract_payload",
            [](PyDocument& d, uint32_t block_index, bool decompress,
               uint64_t max_output_bytes) {
                if (block_index >= d.blocks.size()) {
                    throw std::runtime_error("block_index out of range");
                }
                PayloadOptions options;
                options.decompress              = decompress;
                options.limits.max_output_bytes = max_output_bytes;
                options.limits.max_parts        = 1U << 14;

                std::vector<uint32_t> indices(options.limits.max_parts);

                if (d.payload.empty()) {
                    d.payload.resize(1024 * 1024);
                }
                for (;;) {
                    const PayloadResult r = extract_payload(
                        d.file_bytes, d.blocks, block_index,
                        std::span<std::byte>(d.payload.data(), d.payload.size()),
                        std::span<uint32_t>(indices.data(), indices.size()),
                        options);
                    if (r.status == PayloadStatus::OutputTruncated
                        && r.needed > d.payload.size()) {
                        d.payload.resize(static_cast<size_t>(r.needed));
                        continue;
                    }
                    if (r.status != PayloadStatus::Ok) {
                        throw std::runtime_error("payload extraction failed");
                    }
                    return nb::bytes(reinterpret_cast<const char*>(
                                         d.payload.data()),
                                     static_cast<size_t>(r.written));
                }
            },
            "block_index"_a, "decompress"_a = true,
            "max_output_bytes"_a = 64ULL * 1024ULL * 1024ULL)
        .def("__len__",
             [](const PyDocument& d) {
                 return static_cast<uint64_t>(d.store.entries().size());
             })
        .def(
            "find_exif",
            [](std::shared_ptr<PyDocument> d, const std::string& ifd,
               uint16_t tag) {
                MetaKeyView key;
                key.kind              = MetaKeyKind::ExifTag;
                key.data.exif_tag.ifd = ifd;
                key.data.exif_tag.tag = tag;

                const std::span<const EntryId> ids = d->store.find_all(key);
                std::vector<PyEntry> out;
                out.reserve(ids.size());
                for (const EntryId id : ids) {
                    PyEntry e;
                    e.doc = d;
                    e.id  = id;
                    out.push_back(std::move(e));
                }
                return out;
            },
            "ifd"_a, "tag"_a)
        .def(
            "find_exr",
            [](std::shared_ptr<PyDocument> d, uint32_t part_index,
               const std::string& name) {
                MetaKeyView key;
                key.kind                          = MetaKeyKind::ExrAttribute;
                key.data.exr_attribute.part_index = part_index;
                key.data.exr_attribute.name       = name;

                const std::span<const EntryId> ids = d->store.find_all(key);
                std::vector<PyEntry> out;
                out.reserve(ids.size());
                for (const EntryId id : ids) {
                    PyEntry e;
                    e.doc = d;
                    e.id  = id;
                    out.push_back(std::move(e));
                }
                return out;
            },
            "part_index"_a, "name"_a)
        .def("__getitem__", [](std::shared_ptr<PyDocument> d, int64_t index) {
            const size_t n = d->store.entries().size();
            int64_t i      = index;
            if (i < 0) {
                i += static_cast<int64_t>(n);
            }
            if (i < 0 || static_cast<size_t>(i) >= n) {
                throw std::out_of_range("entry index out of range");
            }
            PyEntry e;
            e.doc = std::move(d);
            e.id  = static_cast<EntryId>(i);
            return e;
        });

    m.def("create_metadata", &create_metadata_document, "fields"_a,
          "max_fields"_a               = kMetadataCreationMaxFields,
          "max_text_bytes_per_field"_a = kMetadataCreationMaxTextBytesPerField,
          "max_total_text_bytes"_a     = kMetadataCreationMaxTotalTextBytes);

    nb::class_<PyEntry>(m, "Entry")
        .def_prop_ro("key_kind",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).key.kind;
                     })
        .def_prop_ro("ifd",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::ExifTag) {
                             return nb::none();
                         }
                         const std::string ifd
                             = arena_string(e.doc->store.arena(),
                                            en.key.data.exif_tag.ifd);
                         return nb::str(ifd.c_str(), ifd.size());
                     })
        .def_prop_ro("tag",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::ExifTag) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.exif_tag.tag);
                     })
        .def_prop_ro("exr_part",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::ExrAttribute) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.exr_attribute.part_index);
                     })
        .def_prop_ro("exr_name",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::ExrAttribute) {
                             return nb::none();
                         }
                         const std::string s
                             = arena_string(e.doc->store.arena(),
                                            en.key.data.exr_attribute.name);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro("geotiff_key_id",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::GeotiffKey) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.geotiff_key.key_id);
                     })
        .def_prop_ro("iptc_record",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::IptcDataset) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.iptc_dataset.record);
                     })
        .def_prop_ro("iptc_dataset",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::IptcDataset) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.iptc_dataset.dataset);
                     })
        .def_prop_ro("photoshop_resource_id",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind == MetaKeyKind::PhotoshopIrb) {
                             return nb::int_(
                                 en.key.data.photoshop_irb.resource_id);
                         }
                         if (en.key.kind == MetaKeyKind::PhotoshopIrbField) {
                             return nb::int_(
                                 en.key.data.photoshop_irb_field.resource_id);
                         }
                         return nb::none();
                     })
        .def_prop_ro("photoshop_field",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::PhotoshopIrbField) {
                             return nb::none();
                         }
                         const std::string s = arena_string(
                             e.doc->store.arena(),
                             en.key.data.photoshop_irb_field.field);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro("icc_header_offset",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::IccHeaderField) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.icc_header_field.offset);
                     })
        .def_prop_ro("icc_tag_signature",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::IccTag) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.icc_tag.signature);
                     })
        .def_prop_ro("xmp_schema_ns",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::XmpProperty) {
                             return nb::none();
                         }
                         const std::string s
                             = arena_string(e.doc->store.arena(),
                                            en.key.data.xmp_property.schema_ns);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro("xmp_path",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::XmpProperty) {
                             return nb::none();
                         }
                         const std::string s = arena_string(
                             e.doc->store.arena(),
                             en.key.data.xmp_property.property_path);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro("png_text_keyword",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::PngText) {
                             return nb::none();
                         }
                         const std::string s
                             = arena_string(e.doc->store.arena(),
                                            en.key.data.png_text.keyword);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro("png_text_field",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::PngText) {
                             return nb::none();
                         }
                         const std::string s
                             = arena_string(e.doc->store.arena(),
                                            en.key.data.png_text.field);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro(
            "name",
            [](const PyEntry& e) -> nb::object {
                const Entry& en = e.doc->store.entry(e.id);
                if (en.key.kind == MetaKeyKind::ExifTag) {
                    const std::string_view n
                        = exif_entry_name(e.doc->store, en,
                                          ExifTagNamePolicy::ExifToolCompat);
                    if (n.empty()) {
                        return nb::none();
                    }
                    return nb::str(n.data(), n.size());
                }
                if (en.key.kind == MetaKeyKind::Comment) {
                    return nb::str("comment");
                }
                if (en.key.kind == MetaKeyKind::GeotiffKey) {
                    const std::string_view n = geotiff_key_name(
                        en.key.data.geotiff_key.key_id);
                    if (n.empty()) {
                        return nb::none();
                    }
                    return nb::str(n.data(), n.size());
                }
                if (en.key.kind == MetaKeyKind::IccTag) {
                    const std::string_view n = icc_tag_name(
                        en.key.data.icc_tag.signature);
                    if (n.empty()) {
                        return nb::none();
                    }
                    return nb::str(n.data(), n.size());
                }
                if (en.key.kind == MetaKeyKind::ExrAttribute) {
                    const std::string s
                        = arena_string(e.doc->store.arena(),
                                       en.key.data.exr_attribute.name);
                    return nb::str(s.c_str(), s.size());
                }
                if (en.key.kind == MetaKeyKind::PhotoshopIrbField) {
                    const std::string s
                        = arena_string(e.doc->store.arena(),
                                       en.key.data.photoshop_irb_field.field);
                    return nb::str(s.c_str(), s.size());
                }
                if (en.key.kind == MetaKeyKind::BmffField) {
                    const std::string s
                        = arena_string(e.doc->store.arena(),
                                       en.key.data.bmff_field.field);
                    return nb::str(s.c_str(), s.size());
                }
                if (en.key.kind == MetaKeyKind::JumbfField) {
                    const std::string s
                        = arena_string(e.doc->store.arena(),
                                       en.key.data.jumbf_field.field);
                    return nb::str(s.c_str(), s.size());
                }
                if (en.key.kind == MetaKeyKind::JumbfCborKey) {
                    const std::string s
                        = arena_string(e.doc->store.arena(),
                                       en.key.data.jumbf_cbor_key.key);
                    return nb::str(s.c_str(), s.size());
                }
                if (en.key.kind == MetaKeyKind::PngText) {
                    const std::string s
                        = arena_string(e.doc->store.arena(),
                                       en.key.data.png_text.keyword);
                    return nb::str(s.c_str(), s.size());
                }
                return nb::none();
            })
        .def_prop_ro("value_kind",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).value.kind;
                     })
        .def_prop_ro("elem_type",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).value.elem_type;
                     })
        .def_prop_ro("count",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).value.count;
                     })
        .def_prop_ro("text_encoding",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).value.text_encoding;
                     })
        .def_prop_ro("origin_block",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).origin.block;
                     })
        .def_prop_ro("origin_order",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).origin.order_in_block;
                     })
        .def_prop_ro("wire_family",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).origin.wire_type.family;
                     })
        .def_prop_ro("wire_type_code",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).origin.wire_type.code;
                     })
        .def_prop_ro("wire_type_name",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.origin.wire_type_name.size == 0U) {
                             return nb::none();
                         }
                         const std::string s
                             = arena_string(e.doc->store.arena(),
                                            en.origin.wire_type_name);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro("wire_count",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).origin.wire_count;
                     })
        .def(
            "value",
            [](const PyEntry& e, uint32_t max_elements, uint32_t max_bytes) {
                const Entry& en = e.doc->store.entry(e.id);
                return value_to_python(e.doc->store.arena(), en.value,
                                       max_elements, max_bytes);
            },
            "max_elements"_a = 256, "max_bytes"_a = 4096)
        .def("__repr__", [](const PyEntry& e) {
            const Entry& en = e.doc->store.entry(e.id);
            std::string s;
            s.reserve(128);
            s.append("Entry(");
            if (en.key.kind == MetaKeyKind::ExifTag) {
                const std::string ifd = arena_string(e.doc->store.arena(),
                                                     en.key.data.exif_tag.ifd);
                s.append("ifd=\"");
                append_console_escaped_ascii(ifd, 64, &s);
                s.append("\", tag=0x");
                char tag_buf[8];
                std::snprintf(tag_buf, sizeof(tag_buf), "%04X",
                              static_cast<unsigned>(en.key.data.exif_tag.tag));
                s.append(tag_buf);
            } else if (en.key.kind == MetaKeyKind::ExrAttribute) {
                s.append("part=");
                s.append(std::to_string(static_cast<unsigned>(
                    en.key.data.exr_attribute.part_index)));
                s.append(", name=\"");
                const std::string name
                    = arena_string(e.doc->store.arena(),
                                   en.key.data.exr_attribute.name);
                append_console_escaped_ascii(name, 64, &s);
                s.append("\"");
            } else if (en.key.kind == MetaKeyKind::PhotoshopIrbField) {
                s.append("psirb=0x");
                char tag_buf[8];
                std::snprintf(tag_buf, sizeof(tag_buf), "%04X",
                              static_cast<unsigned>(
                                  en.key.data.photoshop_irb_field.resource_id));
                s.append(tag_buf);
                s.append(", field=\"");
                const std::string field
                    = arena_string(e.doc->store.arena(),
                                   en.key.data.photoshop_irb_field.field);
                append_console_escaped_ascii(field, 64, &s);
                s.append("\"");
            } else if (en.key.kind == MetaKeyKind::JumbfField) {
                s.append("jumbf=\"");
                const std::string field
                    = arena_string(e.doc->store.arena(),
                                   en.key.data.jumbf_field.field);
                append_console_escaped_ascii(field, 64, &s);
                s.append("\"");
            } else if (en.key.kind == MetaKeyKind::JumbfCborKey) {
                s.append("jumbf_cbor=\"");
                const std::string key
                    = arena_string(e.doc->store.arena(),
                                   en.key.data.jumbf_cbor_key.key);
                append_console_escaped_ascii(key, 64, &s);
                s.append("\"");
            } else if (en.key.kind == MetaKeyKind::PngText) {
                s.append("png_text=\"");
                const std::string keyword
                    = arena_string(e.doc->store.arena(),
                                   en.key.data.png_text.keyword);
                const std::string field
                    = arena_string(e.doc->store.arena(),
                                   en.key.data.png_text.field);
                append_console_escaped_ascii(keyword, 64, &s);
                s.append(".");
                append_console_escaped_ascii(field, 32, &s);
                s.append("\"");
            } else if (en.key.kind == MetaKeyKind::Comment) {
                s.append("comment");
            } else {
                s.append("kind=");
                s.append(std::to_string(static_cast<unsigned>(en.key.kind)));
            }
            s.append(", kind=");
            s.append(std::to_string(static_cast<unsigned>(en.value.kind)));
            s.append(", count=");
            s.append(std::to_string(static_cast<unsigned>(en.value.count)));
            s.append(")");
            return s;
        });

    m.def(
        "read",
        [](const std::string& path, bool include_pointer_tags,
           bool decode_makernote, bool decompress, bool include_xmp_sidecar,
           bool verify_c2pa, C2paVerifyBackend verify_backend,
           bool verify_require_trusted_chain,
           bool verify_require_resolved_references, uint64_t max_file_bytes,
           nb::object policy_obj) {
            OpenMetaResourcePolicy policy;
            const OpenMetaResourcePolicy* policy_ptr = nullptr;
            if (!policy_obj.is_none()) {
                policy     = nb::cast<OpenMetaResourcePolicy>(policy_obj);
                policy_ptr = &policy;
            }
            return read_document(path, include_pointer_tags, decode_makernote,
                                 decompress, include_xmp_sidecar, verify_c2pa,
                                 verify_backend, verify_require_trusted_chain,
                                 verify_require_resolved_references,
                                 max_file_bytes, policy_ptr);
        },
        "path"_a, "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decompress"_a = true, "include_xmp_sidecar"_a = false,
        "verify_c2pa"_a = false, "verify_backend"_a = C2paVerifyBackend::Auto,
        "verify_require_trusted_chain"_a       = false,
        "verify_require_resolved_references"_a = false,
        "max_file_bytes"_a = 0ULL, "policy"_a = nb::none());

    m.def(
        "validate",
        [](const std::string& path, bool include_pointer_tags,
           bool decode_makernote, bool decode_printim, bool decompress,
           bool include_xmp_sidecar, bool verify_c2pa,
           C2paVerifyBackend verify_backend, bool verify_require_trusted_chain,
           bool verify_require_resolved_references, bool warnings_as_errors,
           bool ccm_require_dng_context, bool ccm_include_reduction_matrices,
           uint32_t ccm_max_fields, uint32_t ccm_max_values_per_field,
           CcmValidationMode ccm_validation_mode, uint64_t max_file_bytes,
           nb::object policy_obj) {
            return validate_file_to_python(
                path, include_pointer_tags, decode_makernote, decode_printim,
                decompress, include_xmp_sidecar, verify_c2pa, verify_backend,
                verify_require_trusted_chain,
                verify_require_resolved_references, warnings_as_errors,
                ccm_require_dng_context, ccm_include_reduction_matrices,
                ccm_max_fields, ccm_max_values_per_field, ccm_validation_mode,
                max_file_bytes, policy_obj);
        },
        "path"_a, "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_printim"_a = true, "decompress"_a = true,
        "include_xmp_sidecar"_a = false, "verify_c2pa"_a = false,
        "verify_backend"_a                     = C2paVerifyBackend::Auto,
        "verify_require_trusted_chain"_a       = false,
        "verify_require_resolved_references"_a = false,
        "warnings_as_errors"_a = false, "ccm_require_dng_context"_a = true,
        "ccm_include_reduction_matrices"_a = true, "ccm_max_fields"_a = 128U,
        "ccm_max_values_per_field"_a = 256U,
        "ccm_validation_mode"_a      = CcmValidationMode::DngSpecWarnings,
        "max_file_bytes"_a = 0ULL, "policy"_a = nb::none());

    m.def(
        "read_transfer_source_snapshot_file",
        [](const std::string& path, bool include_pointer_tags,
           bool decode_makernote, bool decode_embedded_containers,
           bool decompress, uint64_t max_file_bytes, nb::object policy_obj,
           bool preserve_raw_carriers, uint64_t max_raw_carrier_bytes) {
            ReadTransferSourceSnapshotFileOptions options;
            options.include_pointer_tags       = include_pointer_tags;
            options.decode_makernote           = decode_makernote;
            options.decode_embedded_containers = decode_embedded_containers;
            options.decompress                 = decompress;
            options.preserve_raw_carriers      = preserve_raw_carriers;
            options.max_raw_carrier_bytes      = max_raw_carrier_bytes;
            options.policy.max_file_bytes      = max_file_bytes;
            if (!policy_obj.is_none()) {
                options.policy = nb::cast<OpenMetaResourcePolicy>(policy_obj);
                if (max_file_bytes != 0U) {
                    options.policy.max_file_bytes = max_file_bytes;
                }
            }
            ReadTransferSourceSnapshotFileResult result;
            {
                nb::gil_scoped_release gil_release;
                result = read_transfer_source_snapshot_file(path.c_str(),
                                                            options);
            }
            return read_transfer_source_snapshot_file_to_python(path, result);
        },
        "path"_a, "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_embedded_containers"_a = true, "decompress"_a = true,
        "max_file_bytes"_a = 0ULL, "policy"_a = nb::none(),
        "preserve_raw_carriers"_a = false,
        "max_raw_carrier_bytes"_a = 64ULL * 1024ULL * 1024ULL);

    m.def(
        "read_transfer_source_snapshot_bytes",
        [](nb::object bytes_obj, bool include_pointer_tags,
           bool decode_makernote, bool decode_embedded_containers,
           bool decompress, uint64_t max_file_bytes, nb::object policy_obj,
           bool preserve_raw_carriers, uint64_t max_raw_carrier_bytes) {
            ReadTransferSourceSnapshotOptions options;
            options.include_pointer_tags       = include_pointer_tags;
            options.decode_makernote           = decode_makernote;
            options.decode_embedded_containers = decode_embedded_containers;
            options.decompress                 = decompress;
            options.preserve_raw_carriers      = preserve_raw_carriers;
            options.max_raw_carrier_bytes      = max_raw_carrier_bytes;
            options.policy.max_file_bytes      = max_file_bytes;
            if (!policy_obj.is_none()) {
                options.policy = nb::cast<OpenMetaResourcePolicy>(policy_obj);
                if (max_file_bytes != 0U) {
                    options.policy.max_file_bytes = max_file_bytes;
                }
            }
            const std::vector<std::byte> bytes = bytes_object_to_vector(
                bytes_obj);
            ReadTransferSourceSnapshotBytesResult result;
            {
                nb::gil_scoped_release gil_release;
                result = read_transfer_source_snapshot_bytes(
                    std::span<const std::byte>(bytes.data(), bytes.size()),
                    options);
            }
            return read_transfer_source_snapshot_bytes_to_python(result);
        },
        "bytes"_a, "include_pointer_tags"_a = true,
        "decode_makernote"_a = false, "decode_embedded_containers"_a = true,
        "decompress"_a = true, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "preserve_raw_carriers"_a = false,
        "max_raw_carrier_bytes"_a = 64ULL * 1024ULL * 1024ULL);

    m.def(
        "build_transfer_source_snapshot",
        [](std::shared_ptr<PyDocument> document) {
            return build_transfer_source_snapshot(document->store);
        },
        "document"_a);

    m.def(
        "transfer_probe",
        [](const std::string& path, TransferTargetFormat target_format,
           DngTargetMode dng_target_mode, XmpSidecarFormat format,
           bool include_pointer_tags, bool decode_makernote,
           bool decode_embedded_containers, bool decompress,
           bool include_exif_app1, bool include_xmp_app1, bool include_icc_app2,
           bool include_iptc_app13, bool xmp_include_existing,
           XmpExistingNamespacePolicy xmp_existing_namespace_policy,
           XmpExistingStandardNamespacePolicy
               xmp_existing_standard_namespace_policy,
           bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
           bool xmp_project_iptc, TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           nb::object c2pa_signed_package,
           nb::object c2pa_signed_logical_payload,
           nb::object c2pa_certificate_chain,
           nb::object c2pa_private_key_reference, nb::object c2pa_signing_time,
           nb::object c2pa_manifest_builder_output, bool include_payloads,
           nb::object time_patches, bool time_patch_strict_width,
           bool time_patch_require_slot, bool time_patch_auto_nul,
           nb::object edit_target_path,
           nb::object xmp_existing_sidecar_base_path,
           nb::object xmp_sidecar_base_path,
           XmpExistingSidecarMode xmp_existing_sidecar_mode,
           XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
           nb::object xmp_existing_destination_embedded_path,
           XmpExistingDestinationEmbeddedMode
               xmp_existing_destination_embedded_mode,
           XmpExistingDestinationEmbeddedPrecedence
               xmp_existing_destination_embedded_precedence,
           XmpExistingDestinationCarrierPrecedence
               xmp_existing_destination_carrier_precedence,
           XmpExistingDestinationSidecarState
               xmp_existing_destination_sidecar_state,
           bool edit_apply, bool include_edited_bytes,
           bool include_c2pa_binding_bytes, bool include_c2pa_handoff_bytes,
           bool include_c2pa_signed_package_bytes,
           bool include_jxl_encoder_handoff_bytes,
           bool include_exr_attribute_values,
           bool include_transfer_payload_batch_bytes,
           bool include_transfer_package_batch_bytes,
           XmpConflictPolicy xmp_conflict_policy,
           XmpWritebackMode xmp_writeback_mode,
           XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
           XmpDestinationSidecarMode xmp_destination_sidecar_mode,
           nb::object target_image_spec, nb::object source_raw_data_descriptor,
           TransferSafetyMode transfer_safety) {
            return transfer_probe_to_python(
                path, target_format, dng_target_mode, format,
                include_pointer_tags, decode_makernote,
                decode_embedded_containers, decompress, include_exif_app1,
                include_xmp_app1, include_icc_app2, include_iptc_app13,
                xmp_include_existing, xmp_existing_namespace_policy,
                xmp_existing_standard_namespace_policy,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy, c2pa_policy,
                max_file_bytes, policy_obj, c2pa_signed_package,
                c2pa_signed_logical_payload, c2pa_certificate_chain,
                c2pa_private_key_reference, c2pa_signing_time,
                c2pa_manifest_builder_output, include_payloads, false,
                time_patches, time_patch_strict_width, time_patch_require_slot,
                time_patch_auto_nul, edit_target_path,
                xmp_existing_sidecar_base_path, xmp_sidecar_base_path,
                xmp_existing_sidecar_mode, xmp_existing_sidecar_precedence,
                xmp_existing_destination_embedded_path,
                xmp_existing_destination_embedded_mode,
                xmp_existing_destination_embedded_precedence,
                xmp_existing_destination_carrier_precedence,
                xmp_existing_destination_sidecar_state, edit_apply,
                include_edited_bytes, false, include_c2pa_binding_bytes, false,
                include_c2pa_handoff_bytes, include_c2pa_signed_package_bytes,
                include_jxl_encoder_handoff_bytes, include_exr_attribute_values,
                include_transfer_payload_batch_bytes,
                include_transfer_package_batch_bytes, false,
                xmp_conflict_policy, xmp_writeback_mode,
                xmp_destination_embedded_mode, xmp_destination_sidecar_mode,
                nb::none(), false, false, true, target_image_spec,
                source_raw_data_descriptor, transfer_safety);
        },
        "path"_a, "target_format"_a = TransferTargetFormat::Jpeg,
        "dng_target_mode"_a      = DngTargetMode::MinimalFreshScaffold,
        "format"_a               = XmpSidecarFormat::Portable,
        "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_embedded_containers"_a = true, "decompress"_a = true,
        "include_exif_app1"_a = true, "include_xmp_app1"_a = true,
        "include_icc_app2"_a = true, "include_iptc_app13"_a = true,
        "xmp_include_existing"_a = false,
        "xmp_existing_namespace_policy"_a
        = XmpExistingNamespacePolicy::KnownPortableOnly,
        "xmp_existing_standard_namespace_policy"_a
        = XmpExistingStandardNamespacePolicy::PreserveAll,
        "xmp_exiftool_gpsdatetime_alias"_a = false, "xmp_project_exif"_a = true,
        "xmp_project_iptc"_a = true,
        "makernote_policy"_a = TransferPolicyAction::Keep,
        "jumbf_policy"_a     = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "c2pa_signed_package"_a = nb::none(),
        "c2pa_signed_logical_payload"_a  = nb::none(),
        "c2pa_certificate_chain"_a       = nb::none(),
        "c2pa_private_key_reference"_a   = nb::none(),
        "c2pa_signing_time"_a            = nb::none(),
        "c2pa_manifest_builder_output"_a = nb::none(),
        "include_payloads"_a = false, "time_patches"_a = nb::none(),
        "time_patch_strict_width"_a = true, "time_patch_require_slot"_a = false,
        "time_patch_auto_nul"_a = true, "edit_target_path"_a = nb::none(),
        "xmp_existing_sidecar_base_path"_a = nb::none(),
        "xmp_sidecar_base_path"_a          = nb::none(),
        "xmp_existing_sidecar_mode"_a      = XmpExistingSidecarMode::Ignore,
        "xmp_existing_sidecar_precedence"_a
        = XmpExistingSidecarPrecedence::SidecarWins,
        "xmp_existing_destination_embedded_path"_a = nb::none(),
        "xmp_existing_destination_embedded_mode"_a
        = XmpExistingDestinationEmbeddedMode::Ignore,
        "xmp_existing_destination_embedded_precedence"_a
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
        "xmp_existing_destination_carrier_precedence"_a
        = XmpExistingDestinationCarrierPrecedence::SidecarWins,
        "xmp_existing_destination_sidecar_state"_a
        = XmpExistingDestinationSidecarState::Unknown,
        "edit_apply"_a = true, "include_edited_bytes"_a = false,
        "include_c2pa_binding_bytes"_a           = false,
        "include_c2pa_handoff_bytes"_a           = false,
        "include_c2pa_signed_package_bytes"_a    = false,
        "include_jxl_encoder_handoff_bytes"_a    = false,
        "include_exr_attribute_values"_a         = false,
        "include_transfer_payload_batch_bytes"_a = false,
        "include_transfer_package_batch_bytes"_a = false,
        "xmp_conflict_policy"_a = XmpConflictPolicy::CurrentBehavior,
        "xmp_writeback_mode"_a  = XmpWritebackMode::EmbeddedOnly,
        "xmp_destination_embedded_mode"_a
        = XmpDestinationEmbeddedMode::PreserveExisting,
        "xmp_destination_sidecar_mode"_a
        = XmpDestinationSidecarMode::PreserveExisting,
        "target_image_spec"_a          = nb::none(),
        "source_raw_data_descriptor"_a = nb::none(),
        "transfer_safety"_a            = TransferSafetyMode::CompatibleFile);

    m.def(
        "unsafe_transfer_probe",
        [](const std::string& path, TransferTargetFormat target_format,
           DngTargetMode dng_target_mode, XmpSidecarFormat format,
           bool include_pointer_tags, bool decode_makernote,
           bool decode_embedded_containers, bool decompress,
           bool include_exif_app1, bool include_xmp_app1, bool include_icc_app2,
           bool include_iptc_app13, bool xmp_include_existing,
           XmpExistingNamespacePolicy xmp_existing_namespace_policy,
           XmpExistingStandardNamespacePolicy
               xmp_existing_standard_namespace_policy,
           bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
           bool xmp_project_iptc, TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           nb::object c2pa_signed_package,
           nb::object c2pa_signed_logical_payload,
           nb::object c2pa_certificate_chain,
           nb::object c2pa_private_key_reference, nb::object c2pa_signing_time,
           nb::object c2pa_manifest_builder_output, bool include_payloads,
           nb::object time_patches, bool time_patch_strict_width,
           bool time_patch_require_slot, bool time_patch_auto_nul,
           nb::object edit_target_path,
           nb::object xmp_existing_sidecar_base_path,
           nb::object xmp_sidecar_base_path,
           XmpExistingSidecarMode xmp_existing_sidecar_mode,
           XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
           nb::object xmp_existing_destination_embedded_path,
           XmpExistingDestinationEmbeddedMode
               xmp_existing_destination_embedded_mode,
           XmpExistingDestinationEmbeddedPrecedence
               xmp_existing_destination_embedded_precedence,
           XmpExistingDestinationCarrierPrecedence
               xmp_existing_destination_carrier_precedence,
           XmpExistingDestinationSidecarState
               xmp_existing_destination_sidecar_state,
           bool edit_apply, bool include_edited_bytes,
           bool include_c2pa_binding_bytes, bool include_c2pa_handoff_bytes,
           bool include_c2pa_signed_package_bytes,
           bool include_jxl_encoder_handoff_bytes,
           bool include_exr_attribute_values,
           bool include_transfer_payload_batch_bytes,
           bool include_transfer_package_batch_bytes,
           XmpConflictPolicy xmp_conflict_policy,
           XmpWritebackMode xmp_writeback_mode,
           XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
           XmpDestinationSidecarMode xmp_destination_sidecar_mode,
           nb::object target_image_spec, nb::object source_raw_data_descriptor,
           TransferSafetyMode transfer_safety) {
            return transfer_probe_to_python(
                path, target_format, dng_target_mode, format,
                include_pointer_tags, decode_makernote,
                decode_embedded_containers, decompress, include_exif_app1,
                include_xmp_app1, include_icc_app2, include_iptc_app13,
                xmp_include_existing, xmp_existing_namespace_policy,
                xmp_existing_standard_namespace_policy,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy, c2pa_policy,
                max_file_bytes, policy_obj, c2pa_signed_package,
                c2pa_signed_logical_payload, c2pa_certificate_chain,
                c2pa_private_key_reference, c2pa_signing_time,
                c2pa_manifest_builder_output, include_payloads, true,
                time_patches, time_patch_strict_width, time_patch_require_slot,
                time_patch_auto_nul, edit_target_path,
                xmp_existing_sidecar_base_path, xmp_sidecar_base_path,
                xmp_existing_sidecar_mode, xmp_existing_sidecar_precedence,
                xmp_existing_destination_embedded_path,
                xmp_existing_destination_embedded_mode,
                xmp_existing_destination_embedded_precedence,
                xmp_existing_destination_carrier_precedence,
                xmp_existing_destination_sidecar_state, edit_apply,
                include_edited_bytes, true, include_c2pa_binding_bytes, true,
                include_c2pa_handoff_bytes, include_c2pa_signed_package_bytes,
                include_jxl_encoder_handoff_bytes, include_exr_attribute_values,
                include_transfer_payload_batch_bytes,
                include_transfer_package_batch_bytes, true, xmp_conflict_policy,
                xmp_writeback_mode, xmp_destination_embedded_mode,
                xmp_destination_sidecar_mode, nb::none(), false, false, true,
                target_image_spec, source_raw_data_descriptor, transfer_safety);
        },
        "path"_a, "target_format"_a = TransferTargetFormat::Jpeg,
        "dng_target_mode"_a      = DngTargetMode::MinimalFreshScaffold,
        "format"_a               = XmpSidecarFormat::Portable,
        "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_embedded_containers"_a = true, "decompress"_a = true,
        "include_exif_app1"_a = true, "include_xmp_app1"_a = true,
        "include_icc_app2"_a = true, "include_iptc_app13"_a = true,
        "xmp_include_existing"_a = false,
        "xmp_existing_namespace_policy"_a
        = XmpExistingNamespacePolicy::KnownPortableOnly,
        "xmp_existing_standard_namespace_policy"_a
        = XmpExistingStandardNamespacePolicy::PreserveAll,
        "xmp_exiftool_gpsdatetime_alias"_a = false, "xmp_project_exif"_a = true,
        "xmp_project_iptc"_a = true,
        "makernote_policy"_a = TransferPolicyAction::Keep,
        "jumbf_policy"_a     = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "c2pa_signed_package"_a = nb::none(),
        "c2pa_signed_logical_payload"_a  = nb::none(),
        "c2pa_certificate_chain"_a       = nb::none(),
        "c2pa_private_key_reference"_a   = nb::none(),
        "c2pa_signing_time"_a            = nb::none(),
        "c2pa_manifest_builder_output"_a = nb::none(),
        "include_payloads"_a = false, "time_patches"_a = nb::none(),
        "time_patch_strict_width"_a = true, "time_patch_require_slot"_a = false,
        "time_patch_auto_nul"_a = true, "edit_target_path"_a = nb::none(),
        "xmp_existing_sidecar_base_path"_a = nb::none(),
        "xmp_sidecar_base_path"_a          = nb::none(),
        "xmp_existing_sidecar_mode"_a      = XmpExistingSidecarMode::Ignore,
        "xmp_existing_sidecar_precedence"_a
        = XmpExistingSidecarPrecedence::SidecarWins,
        "xmp_existing_destination_embedded_path"_a = nb::none(),
        "xmp_existing_destination_embedded_mode"_a
        = XmpExistingDestinationEmbeddedMode::Ignore,
        "xmp_existing_destination_embedded_precedence"_a
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
        "xmp_existing_destination_carrier_precedence"_a
        = XmpExistingDestinationCarrierPrecedence::SidecarWins,
        "xmp_existing_destination_sidecar_state"_a
        = XmpExistingDestinationSidecarState::Unknown,
        "edit_apply"_a = true, "include_edited_bytes"_a = false,
        "include_c2pa_binding_bytes"_a           = false,
        "include_c2pa_handoff_bytes"_a           = false,
        "include_c2pa_signed_package_bytes"_a    = false,
        "include_jxl_encoder_handoff_bytes"_a    = false,
        "include_exr_attribute_values"_a         = false,
        "include_transfer_payload_batch_bytes"_a = false,
        "include_transfer_package_batch_bytes"_a = false,
        "xmp_conflict_policy"_a = XmpConflictPolicy::CurrentBehavior,
        "xmp_writeback_mode"_a  = XmpWritebackMode::EmbeddedOnly,
        "xmp_destination_embedded_mode"_a
        = XmpDestinationEmbeddedMode::PreserveExisting,
        "xmp_destination_sidecar_mode"_a
        = XmpDestinationSidecarMode::PreserveExisting,
        "target_image_spec"_a          = nb::none(),
        "source_raw_data_descriptor"_a = nb::none(),
        "transfer_safety"_a            = TransferSafetyMode::CompatibleFile);

    m.def(
        "transfer_file",
        [](const std::string& path, TransferTargetFormat target_format,
           DngTargetMode dng_target_mode, XmpSidecarFormat format,
           bool include_pointer_tags, bool decode_makernote,
           bool decode_embedded_containers, bool decompress,
           bool include_exif_app1, bool include_xmp_app1, bool include_icc_app2,
           bool include_iptc_app13, bool xmp_include_existing,
           XmpExistingNamespacePolicy xmp_existing_namespace_policy,
           XmpExistingStandardNamespacePolicy
               xmp_existing_standard_namespace_policy,
           bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
           bool xmp_project_iptc, TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           nb::object c2pa_signed_package,
           nb::object c2pa_signed_logical_payload,
           nb::object c2pa_certificate_chain,
           nb::object c2pa_private_key_reference, nb::object c2pa_signing_time,
           nb::object c2pa_manifest_builder_output, bool include_payloads,
           nb::object time_patches, bool time_patch_strict_width,
           bool time_patch_require_slot, bool time_patch_auto_nul,
           nb::object edit_target_path,
           nb::object xmp_existing_sidecar_base_path,
           nb::object xmp_sidecar_base_path,
           XmpExistingSidecarMode xmp_existing_sidecar_mode,
           XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
           nb::object xmp_existing_destination_embedded_path,
           XmpExistingDestinationEmbeddedMode
               xmp_existing_destination_embedded_mode,
           XmpExistingDestinationEmbeddedPrecedence
               xmp_existing_destination_embedded_precedence,
           XmpExistingDestinationCarrierPrecedence
               xmp_existing_destination_carrier_precedence,
           XmpExistingDestinationSidecarState
               xmp_existing_destination_sidecar_state,
           bool edit_apply, bool include_edited_bytes,
           bool include_c2pa_binding_bytes, bool include_c2pa_handoff_bytes,
           bool include_c2pa_signed_package_bytes,
           bool include_jxl_encoder_handoff_bytes,
           bool include_exr_attribute_values,
           bool include_transfer_payload_batch_bytes,
           bool include_transfer_package_batch_bytes,
           XmpConflictPolicy xmp_conflict_policy,
           XmpWritebackMode xmp_writeback_mode,
           XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
           XmpDestinationSidecarMode xmp_destination_sidecar_mode,
           const std::string& output_path, bool overwrite_output,
           bool overwrite_xmp_sidecar, bool remove_destination_xmp_sidecar,
           nb::object target_image_spec, nb::object source_raw_data_descriptor,
           TransferSafetyMode transfer_safety) {
            return transfer_probe_to_python(
                path, target_format, dng_target_mode, format,
                include_pointer_tags, decode_makernote,
                decode_embedded_containers, decompress, include_exif_app1,
                include_xmp_app1, include_icc_app2, include_iptc_app13,
                xmp_include_existing, xmp_existing_namespace_policy,
                xmp_existing_standard_namespace_policy,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy, c2pa_policy,
                max_file_bytes, policy_obj, c2pa_signed_package,
                c2pa_signed_logical_payload, c2pa_certificate_chain,
                c2pa_private_key_reference, c2pa_signing_time,
                c2pa_manifest_builder_output, include_payloads, false,
                time_patches, time_patch_strict_width, time_patch_require_slot,
                time_patch_auto_nul, edit_target_path,
                xmp_existing_sidecar_base_path, xmp_sidecar_base_path,
                xmp_existing_sidecar_mode, xmp_existing_sidecar_precedence,
                xmp_existing_destination_embedded_path,
                xmp_existing_destination_embedded_mode,
                xmp_existing_destination_embedded_precedence,
                xmp_existing_destination_carrier_precedence,
                xmp_existing_destination_sidecar_state, edit_apply,
                include_edited_bytes, false, include_c2pa_binding_bytes, false,
                include_c2pa_handoff_bytes, include_c2pa_signed_package_bytes,
                include_jxl_encoder_handoff_bytes, include_exr_attribute_values,
                include_transfer_payload_batch_bytes,
                include_transfer_package_batch_bytes, false,
                xmp_conflict_policy, xmp_writeback_mode,
                xmp_destination_embedded_mode, xmp_destination_sidecar_mode,
                nb::str(output_path.c_str(), output_path.size()),
                overwrite_output, overwrite_xmp_sidecar,
                remove_destination_xmp_sidecar, target_image_spec,
                source_raw_data_descriptor, transfer_safety);
        },
        "path"_a, "target_format"_a = TransferTargetFormat::Jpeg,
        "dng_target_mode"_a      = DngTargetMode::MinimalFreshScaffold,
        "format"_a               = XmpSidecarFormat::Portable,
        "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_embedded_containers"_a = true, "decompress"_a = true,
        "include_exif_app1"_a = true, "include_xmp_app1"_a = true,
        "include_icc_app2"_a = true, "include_iptc_app13"_a = true,
        "xmp_include_existing"_a = false,
        "xmp_existing_namespace_policy"_a
        = XmpExistingNamespacePolicy::KnownPortableOnly,
        "xmp_existing_standard_namespace_policy"_a
        = XmpExistingStandardNamespacePolicy::PreserveAll,
        "xmp_exiftool_gpsdatetime_alias"_a = false, "xmp_project_exif"_a = true,
        "xmp_project_iptc"_a = true,
        "makernote_policy"_a = TransferPolicyAction::Keep,
        "jumbf_policy"_a     = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "c2pa_signed_package"_a = nb::none(),
        "c2pa_signed_logical_payload"_a  = nb::none(),
        "c2pa_certificate_chain"_a       = nb::none(),
        "c2pa_private_key_reference"_a   = nb::none(),
        "c2pa_signing_time"_a            = nb::none(),
        "c2pa_manifest_builder_output"_a = nb::none(),
        "include_payloads"_a = false, "time_patches"_a = nb::none(),
        "time_patch_strict_width"_a = true, "time_patch_require_slot"_a = false,
        "time_patch_auto_nul"_a = true, "edit_target_path"_a = nb::none(),
        "xmp_existing_sidecar_base_path"_a = nb::none(),
        "xmp_sidecar_base_path"_a          = nb::none(),
        "xmp_existing_sidecar_mode"_a      = XmpExistingSidecarMode::Ignore,
        "xmp_existing_sidecar_precedence"_a
        = XmpExistingSidecarPrecedence::SidecarWins,
        "xmp_existing_destination_embedded_path"_a = nb::none(),
        "xmp_existing_destination_embedded_mode"_a
        = XmpExistingDestinationEmbeddedMode::Ignore,
        "xmp_existing_destination_embedded_precedence"_a
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
        "xmp_existing_destination_carrier_precedence"_a
        = XmpExistingDestinationCarrierPrecedence::SidecarWins,
        "xmp_existing_destination_sidecar_state"_a
        = XmpExistingDestinationSidecarState::Unknown,
        "edit_apply"_a = true, "include_edited_bytes"_a = false,
        "include_c2pa_binding_bytes"_a           = false,
        "include_c2pa_handoff_bytes"_a           = false,
        "include_c2pa_signed_package_bytes"_a    = false,
        "include_jxl_encoder_handoff_bytes"_a    = false,
        "include_exr_attribute_values"_a         = false,
        "include_transfer_payload_batch_bytes"_a = false,
        "include_transfer_package_batch_bytes"_a = false,
        "xmp_conflict_policy"_a = XmpConflictPolicy::CurrentBehavior,
        "xmp_writeback_mode"_a  = XmpWritebackMode::EmbeddedOnly,
        "xmp_destination_embedded_mode"_a
        = XmpDestinationEmbeddedMode::PreserveExisting,
        "xmp_destination_sidecar_mode"_a
        = XmpDestinationSidecarMode::PreserveExisting,
        "output_path"_a, "overwrite_output"_a = false,
        "overwrite_xmp_sidecar"_a          = false,
        "remove_destination_xmp_sidecar"_a = true,
        "target_image_spec"_a              = nb::none(),
        "source_raw_data_descriptor"_a     = nb::none(),
        "transfer_safety"_a = TransferSafetyMode::CompatibleFile);

    m.def(
        "unsafe_transfer_file",
        [](const std::string& path, TransferTargetFormat target_format,
           DngTargetMode dng_target_mode, XmpSidecarFormat format,
           bool include_pointer_tags, bool decode_makernote,
           bool decode_embedded_containers, bool decompress,
           bool include_exif_app1, bool include_xmp_app1, bool include_icc_app2,
           bool include_iptc_app13, bool xmp_include_existing,
           XmpExistingNamespacePolicy xmp_existing_namespace_policy,
           XmpExistingStandardNamespacePolicy
               xmp_existing_standard_namespace_policy,
           bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
           bool xmp_project_iptc, TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           nb::object c2pa_signed_package,
           nb::object c2pa_signed_logical_payload,
           nb::object c2pa_certificate_chain,
           nb::object c2pa_private_key_reference, nb::object c2pa_signing_time,
           nb::object c2pa_manifest_builder_output, bool include_payloads,
           nb::object time_patches, bool time_patch_strict_width,
           bool time_patch_require_slot, bool time_patch_auto_nul,
           nb::object edit_target_path,
           nb::object xmp_existing_sidecar_base_path,
           nb::object xmp_sidecar_base_path,
           XmpExistingSidecarMode xmp_existing_sidecar_mode,
           XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
           nb::object xmp_existing_destination_embedded_path,
           XmpExistingDestinationEmbeddedMode
               xmp_existing_destination_embedded_mode,
           XmpExistingDestinationEmbeddedPrecedence
               xmp_existing_destination_embedded_precedence,
           XmpExistingDestinationCarrierPrecedence
               xmp_existing_destination_carrier_precedence,
           XmpExistingDestinationSidecarState
               xmp_existing_destination_sidecar_state,
           bool edit_apply, bool include_edited_bytes,
           bool include_c2pa_binding_bytes, bool include_c2pa_handoff_bytes,
           bool include_c2pa_signed_package_bytes,
           bool include_jxl_encoder_handoff_bytes,
           bool include_exr_attribute_values,
           bool include_transfer_payload_batch_bytes,
           bool include_transfer_package_batch_bytes,
           XmpConflictPolicy xmp_conflict_policy,
           XmpWritebackMode xmp_writeback_mode,
           XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
           XmpDestinationSidecarMode xmp_destination_sidecar_mode,
           const std::string& output_path, bool overwrite_output,
           bool overwrite_xmp_sidecar, bool remove_destination_xmp_sidecar,
           nb::object target_image_spec, nb::object source_raw_data_descriptor,
           TransferSafetyMode transfer_safety) {
            return transfer_probe_to_python(
                path, target_format, dng_target_mode, format,
                include_pointer_tags, decode_makernote,
                decode_embedded_containers, decompress, include_exif_app1,
                include_xmp_app1, include_icc_app2, include_iptc_app13,
                xmp_include_existing, xmp_existing_namespace_policy,
                xmp_existing_standard_namespace_policy,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy, c2pa_policy,
                max_file_bytes, policy_obj, c2pa_signed_package,
                c2pa_signed_logical_payload, c2pa_certificate_chain,
                c2pa_private_key_reference, c2pa_signing_time,
                c2pa_manifest_builder_output, include_payloads, true,
                time_patches, time_patch_strict_width, time_patch_require_slot,
                time_patch_auto_nul, edit_target_path,
                xmp_existing_sidecar_base_path, xmp_sidecar_base_path,
                xmp_existing_sidecar_mode, xmp_existing_sidecar_precedence,
                xmp_existing_destination_embedded_path,
                xmp_existing_destination_embedded_mode,
                xmp_existing_destination_embedded_precedence,
                xmp_existing_destination_carrier_precedence,
                xmp_existing_destination_sidecar_state, edit_apply,
                include_edited_bytes, true, include_c2pa_binding_bytes, true,
                include_c2pa_handoff_bytes, include_c2pa_signed_package_bytes,
                include_jxl_encoder_handoff_bytes, include_exr_attribute_values,
                include_transfer_payload_batch_bytes,
                include_transfer_package_batch_bytes, true, xmp_conflict_policy,
                xmp_writeback_mode, xmp_destination_embedded_mode,
                xmp_destination_sidecar_mode,
                nb::str(output_path.c_str(), output_path.size()),
                overwrite_output, overwrite_xmp_sidecar,
                remove_destination_xmp_sidecar, target_image_spec,
                source_raw_data_descriptor, transfer_safety);
        },
        "path"_a, "target_format"_a = TransferTargetFormat::Jpeg,
        "dng_target_mode"_a      = DngTargetMode::MinimalFreshScaffold,
        "format"_a               = XmpSidecarFormat::Portable,
        "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_embedded_containers"_a = true, "decompress"_a = true,
        "include_exif_app1"_a = true, "include_xmp_app1"_a = true,
        "include_icc_app2"_a = true, "include_iptc_app13"_a = true,
        "xmp_include_existing"_a = false,
        "xmp_existing_namespace_policy"_a
        = XmpExistingNamespacePolicy::KnownPortableOnly,
        "xmp_existing_standard_namespace_policy"_a
        = XmpExistingStandardNamespacePolicy::PreserveAll,
        "xmp_exiftool_gpsdatetime_alias"_a = false, "xmp_project_exif"_a = true,
        "xmp_project_iptc"_a = true,
        "makernote_policy"_a = TransferPolicyAction::Keep,
        "jumbf_policy"_a     = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "c2pa_signed_package"_a = nb::none(),
        "c2pa_signed_logical_payload"_a  = nb::none(),
        "c2pa_certificate_chain"_a       = nb::none(),
        "c2pa_private_key_reference"_a   = nb::none(),
        "c2pa_signing_time"_a            = nb::none(),
        "c2pa_manifest_builder_output"_a = nb::none(),
        "include_payloads"_a = false, "time_patches"_a = nb::none(),
        "time_patch_strict_width"_a = true, "time_patch_require_slot"_a = false,
        "time_patch_auto_nul"_a = true, "edit_target_path"_a = nb::none(),
        "xmp_existing_sidecar_base_path"_a = nb::none(),
        "xmp_sidecar_base_path"_a          = nb::none(),
        "xmp_existing_sidecar_mode"_a      = XmpExistingSidecarMode::Ignore,
        "xmp_existing_sidecar_precedence"_a
        = XmpExistingSidecarPrecedence::SidecarWins,
        "xmp_existing_destination_embedded_path"_a = nb::none(),
        "xmp_existing_destination_embedded_mode"_a
        = XmpExistingDestinationEmbeddedMode::Ignore,
        "xmp_existing_destination_embedded_precedence"_a
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
        "xmp_existing_destination_carrier_precedence"_a
        = XmpExistingDestinationCarrierPrecedence::SidecarWins,
        "xmp_existing_destination_sidecar_state"_a
        = XmpExistingDestinationSidecarState::Unknown,
        "edit_apply"_a = true, "include_edited_bytes"_a = false,
        "include_c2pa_binding_bytes"_a           = false,
        "include_c2pa_handoff_bytes"_a           = false,
        "include_c2pa_signed_package_bytes"_a    = false,
        "include_jxl_encoder_handoff_bytes"_a    = false,
        "include_exr_attribute_values"_a         = false,
        "include_transfer_payload_batch_bytes"_a = false,
        "include_transfer_package_batch_bytes"_a = false,
        "xmp_conflict_policy"_a = XmpConflictPolicy::CurrentBehavior,
        "xmp_writeback_mode"_a  = XmpWritebackMode::EmbeddedOnly,
        "xmp_destination_embedded_mode"_a
        = XmpDestinationEmbeddedMode::PreserveExisting,
        "xmp_destination_sidecar_mode"_a
        = XmpDestinationSidecarMode::PreserveExisting,
        "output_path"_a, "overwrite_output"_a = false,
        "overwrite_xmp_sidecar"_a          = false,
        "remove_destination_xmp_sidecar"_a = true,
        "target_image_spec"_a              = nb::none(),
        "source_raw_data_descriptor"_a     = nb::none(),
        "transfer_safety"_a = TransferSafetyMode::CompatibleFile);

    m.def(
        "transfer_snapshot_probe",
        [](const TransferSourceSnapshot& snapshot,
           TransferTargetFormat target_format, DngTargetMode dng_target_mode,
           XmpSidecarFormat format, bool include_exif_app1,
           bool include_xmp_app1, bool include_icc_app2,
           bool include_iptc_app13, bool xmp_include_existing,
           XmpExistingNamespacePolicy xmp_existing_namespace_policy,
           XmpExistingStandardNamespacePolicy
               xmp_existing_standard_namespace_policy,
           XmpConflictPolicy xmp_conflict_policy,
           bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
           bool xmp_project_iptc, TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           nb::object time_patches, bool time_patch_strict_width,
           bool time_patch_require_slot, bool time_patch_auto_nul,
           nb::object edit_target_path, nb::object target_bytes,
           nb::object xmp_existing_sidecar_base_path,
           nb::object xmp_sidecar_base_path,
           XmpExistingSidecarMode xmp_existing_sidecar_mode,
           XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
           nb::object xmp_existing_destination_embedded_path,
           XmpExistingDestinationEmbeddedMode
               xmp_existing_destination_embedded_mode,
           XmpExistingDestinationEmbeddedPrecedence
               xmp_existing_destination_embedded_precedence,
           XmpExistingDestinationCarrierPrecedence
               xmp_existing_destination_carrier_precedence,
           XmpExistingDestinationSidecarState
               xmp_existing_destination_sidecar_state,
           bool edit_apply, bool include_edited_bytes,
           XmpWritebackMode xmp_writeback_mode,
           XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
           XmpDestinationSidecarMode xmp_destination_sidecar_mode,
           nb::object target_image_spec, nb::object source_raw_data_descriptor,
           TransferSafetyMode transfer_safety,
           TransferRawCarrierPassthroughMode raw_carrier_passthrough_mode) {
            return transfer_snapshot_to_python(
                snapshot, target_format, dng_target_mode, format,
                include_exif_app1, include_xmp_app1, include_icc_app2,
                include_iptc_app13, xmp_include_existing,
                xmp_existing_namespace_policy,
                xmp_existing_standard_namespace_policy, xmp_conflict_policy,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy, c2pa_policy,
                max_file_bytes, policy_obj, time_patches,
                time_patch_strict_width, time_patch_require_slot,
                time_patch_auto_nul, edit_target_path, target_bytes,
                xmp_existing_sidecar_base_path, xmp_sidecar_base_path,
                xmp_existing_sidecar_mode, xmp_existing_sidecar_precedence,
                xmp_existing_destination_embedded_path,
                xmp_existing_destination_embedded_mode,
                xmp_existing_destination_embedded_precedence,
                xmp_existing_destination_carrier_precedence,
                xmp_existing_destination_sidecar_state, edit_apply,
                include_edited_bytes, false, xmp_writeback_mode,
                xmp_destination_embedded_mode, xmp_destination_sidecar_mode,
                nb::none(), false, false, true, target_image_spec,
                source_raw_data_descriptor, transfer_safety,
                raw_carrier_passthrough_mode);
        },
        "snapshot"_a, "target_format"_a = TransferTargetFormat::Jpeg,
        "dng_target_mode"_a = DngTargetMode::MinimalFreshScaffold,
        "format"_a = XmpSidecarFormat::Portable, "include_exif_app1"_a = true,
        "include_xmp_app1"_a = true, "include_icc_app2"_a = true,
        "include_iptc_app13"_a = true, "xmp_include_existing"_a = false,
        "xmp_existing_namespace_policy"_a
        = XmpExistingNamespacePolicy::KnownPortableOnly,
        "xmp_existing_standard_namespace_policy"_a
        = XmpExistingStandardNamespacePolicy::PreserveAll,
        "xmp_conflict_policy"_a            = XmpConflictPolicy::CurrentBehavior,
        "xmp_exiftool_gpsdatetime_alias"_a = false, "xmp_project_exif"_a = true,
        "xmp_project_iptc"_a = true,
        "makernote_policy"_a = TransferPolicyAction::Keep,
        "jumbf_policy"_a     = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "time_patches"_a = nb::none(),
        "time_patch_strict_width"_a = true, "time_patch_require_slot"_a = false,
        "time_patch_auto_nul"_a = true, "edit_target_path"_a = nb::none(),
        "target_bytes"_a                   = nb::none(),
        "xmp_existing_sidecar_base_path"_a = nb::none(),
        "xmp_sidecar_base_path"_a          = nb::none(),
        "xmp_existing_sidecar_mode"_a      = XmpExistingSidecarMode::Ignore,
        "xmp_existing_sidecar_precedence"_a
        = XmpExistingSidecarPrecedence::SidecarWins,
        "xmp_existing_destination_embedded_path"_a = nb::none(),
        "xmp_existing_destination_embedded_mode"_a
        = XmpExistingDestinationEmbeddedMode::Ignore,
        "xmp_existing_destination_embedded_precedence"_a
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
        "xmp_existing_destination_carrier_precedence"_a
        = XmpExistingDestinationCarrierPrecedence::SidecarWins,
        "xmp_existing_destination_sidecar_state"_a
        = XmpExistingDestinationSidecarState::Unknown,
        "edit_apply"_a = true, "include_edited_bytes"_a = false,
        "xmp_writeback_mode"_a = XmpWritebackMode::EmbeddedOnly,
        "xmp_destination_embedded_mode"_a
        = XmpDestinationEmbeddedMode::PreserveExisting,
        "xmp_destination_sidecar_mode"_a
        = XmpDestinationSidecarMode::PreserveExisting,
        "target_image_spec"_a          = nb::none(),
        "source_raw_data_descriptor"_a = nb::none(),
        "transfer_safety"_a            = TransferSafetyMode::CompatibleFile,
        "raw_carrier_passthrough_mode"_a
        = TransferRawCarrierPassthroughMode::Disabled);

    m.def(
        "unsafe_transfer_snapshot_probe",
        [](const TransferSourceSnapshot& snapshot,
           TransferTargetFormat target_format, DngTargetMode dng_target_mode,
           XmpSidecarFormat format, bool include_exif_app1,
           bool include_xmp_app1, bool include_icc_app2,
           bool include_iptc_app13, bool xmp_include_existing,
           XmpExistingNamespacePolicy xmp_existing_namespace_policy,
           XmpExistingStandardNamespacePolicy
               xmp_existing_standard_namespace_policy,
           XmpConflictPolicy xmp_conflict_policy,
           bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
           bool xmp_project_iptc, TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           nb::object time_patches, bool time_patch_strict_width,
           bool time_patch_require_slot, bool time_patch_auto_nul,
           nb::object edit_target_path, nb::object target_bytes,
           nb::object xmp_existing_sidecar_base_path,
           nb::object xmp_sidecar_base_path,
           XmpExistingSidecarMode xmp_existing_sidecar_mode,
           XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
           nb::object xmp_existing_destination_embedded_path,
           XmpExistingDestinationEmbeddedMode
               xmp_existing_destination_embedded_mode,
           XmpExistingDestinationEmbeddedPrecedence
               xmp_existing_destination_embedded_precedence,
           XmpExistingDestinationCarrierPrecedence
               xmp_existing_destination_carrier_precedence,
           XmpExistingDestinationSidecarState
               xmp_existing_destination_sidecar_state,
           bool edit_apply, bool include_edited_bytes,
           XmpWritebackMode xmp_writeback_mode,
           XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
           XmpDestinationSidecarMode xmp_destination_sidecar_mode,
           nb::object target_image_spec, nb::object source_raw_data_descriptor,
           TransferSafetyMode transfer_safety,
           TransferRawCarrierPassthroughMode raw_carrier_passthrough_mode) {
            return transfer_snapshot_to_python(
                snapshot, target_format, dng_target_mode, format,
                include_exif_app1, include_xmp_app1, include_icc_app2,
                include_iptc_app13, xmp_include_existing,
                xmp_existing_namespace_policy,
                xmp_existing_standard_namespace_policy, xmp_conflict_policy,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy, c2pa_policy,
                max_file_bytes, policy_obj, time_patches,
                time_patch_strict_width, time_patch_require_slot,
                time_patch_auto_nul, edit_target_path, target_bytes,
                xmp_existing_sidecar_base_path, xmp_sidecar_base_path,
                xmp_existing_sidecar_mode, xmp_existing_sidecar_precedence,
                xmp_existing_destination_embedded_path,
                xmp_existing_destination_embedded_mode,
                xmp_existing_destination_embedded_precedence,
                xmp_existing_destination_carrier_precedence,
                xmp_existing_destination_sidecar_state, edit_apply,
                include_edited_bytes, true, xmp_writeback_mode,
                xmp_destination_embedded_mode, xmp_destination_sidecar_mode,
                nb::none(), false, false, true, target_image_spec,
                source_raw_data_descriptor, transfer_safety,
                raw_carrier_passthrough_mode);
        },
        "snapshot"_a, "target_format"_a = TransferTargetFormat::Jpeg,
        "dng_target_mode"_a = DngTargetMode::MinimalFreshScaffold,
        "format"_a = XmpSidecarFormat::Portable, "include_exif_app1"_a = true,
        "include_xmp_app1"_a = true, "include_icc_app2"_a = true,
        "include_iptc_app13"_a = true, "xmp_include_existing"_a = false,
        "xmp_existing_namespace_policy"_a
        = XmpExistingNamespacePolicy::KnownPortableOnly,
        "xmp_existing_standard_namespace_policy"_a
        = XmpExistingStandardNamespacePolicy::PreserveAll,
        "xmp_conflict_policy"_a            = XmpConflictPolicy::CurrentBehavior,
        "xmp_exiftool_gpsdatetime_alias"_a = false, "xmp_project_exif"_a = true,
        "xmp_project_iptc"_a = true,
        "makernote_policy"_a = TransferPolicyAction::Keep,
        "jumbf_policy"_a     = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "time_patches"_a = nb::none(),
        "time_patch_strict_width"_a = true, "time_patch_require_slot"_a = false,
        "time_patch_auto_nul"_a = true, "edit_target_path"_a = nb::none(),
        "target_bytes"_a                   = nb::none(),
        "xmp_existing_sidecar_base_path"_a = nb::none(),
        "xmp_sidecar_base_path"_a          = nb::none(),
        "xmp_existing_sidecar_mode"_a      = XmpExistingSidecarMode::Ignore,
        "xmp_existing_sidecar_precedence"_a
        = XmpExistingSidecarPrecedence::SidecarWins,
        "xmp_existing_destination_embedded_path"_a = nb::none(),
        "xmp_existing_destination_embedded_mode"_a
        = XmpExistingDestinationEmbeddedMode::Ignore,
        "xmp_existing_destination_embedded_precedence"_a
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
        "xmp_existing_destination_carrier_precedence"_a
        = XmpExistingDestinationCarrierPrecedence::SidecarWins,
        "xmp_existing_destination_sidecar_state"_a
        = XmpExistingDestinationSidecarState::Unknown,
        "edit_apply"_a = true, "include_edited_bytes"_a = false,
        "xmp_writeback_mode"_a = XmpWritebackMode::EmbeddedOnly,
        "xmp_destination_embedded_mode"_a
        = XmpDestinationEmbeddedMode::PreserveExisting,
        "xmp_destination_sidecar_mode"_a
        = XmpDestinationSidecarMode::PreserveExisting,
        "target_image_spec"_a          = nb::none(),
        "source_raw_data_descriptor"_a = nb::none(),
        "transfer_safety"_a            = TransferSafetyMode::CompatibleFile,
        "raw_carrier_passthrough_mode"_a
        = TransferRawCarrierPassthroughMode::Disabled);

    m.def(
        "transfer_snapshot_file",
        [](const TransferSourceSnapshot& snapshot,
           TransferTargetFormat target_format, DngTargetMode dng_target_mode,
           XmpSidecarFormat format, bool include_exif_app1,
           bool include_xmp_app1, bool include_icc_app2,
           bool include_iptc_app13, bool xmp_include_existing,
           XmpExistingNamespacePolicy xmp_existing_namespace_policy,
           XmpExistingStandardNamespacePolicy
               xmp_existing_standard_namespace_policy,
           XmpConflictPolicy xmp_conflict_policy,
           bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
           bool xmp_project_iptc, TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           nb::object time_patches, bool time_patch_strict_width,
           bool time_patch_require_slot, bool time_patch_auto_nul,
           nb::object edit_target_path, nb::object target_bytes,
           nb::object xmp_existing_sidecar_base_path,
           nb::object xmp_sidecar_base_path,
           XmpExistingSidecarMode xmp_existing_sidecar_mode,
           XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
           nb::object xmp_existing_destination_embedded_path,
           XmpExistingDestinationEmbeddedMode
               xmp_existing_destination_embedded_mode,
           XmpExistingDestinationEmbeddedPrecedence
               xmp_existing_destination_embedded_precedence,
           XmpExistingDestinationCarrierPrecedence
               xmp_existing_destination_carrier_precedence,
           XmpExistingDestinationSidecarState
               xmp_existing_destination_sidecar_state,
           bool edit_apply, bool include_edited_bytes,
           XmpWritebackMode xmp_writeback_mode,
           XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
           XmpDestinationSidecarMode xmp_destination_sidecar_mode,
           const std::string& output_path, bool overwrite_output,
           bool overwrite_xmp_sidecar, bool remove_destination_xmp_sidecar,
           nb::object target_image_spec, nb::object source_raw_data_descriptor,
           TransferSafetyMode transfer_safety,
           TransferRawCarrierPassthroughMode raw_carrier_passthrough_mode) {
            return transfer_snapshot_to_python(
                snapshot, target_format, dng_target_mode, format,
                include_exif_app1, include_xmp_app1, include_icc_app2,
                include_iptc_app13, xmp_include_existing,
                xmp_existing_namespace_policy,
                xmp_existing_standard_namespace_policy, xmp_conflict_policy,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy, c2pa_policy,
                max_file_bytes, policy_obj, time_patches,
                time_patch_strict_width, time_patch_require_slot,
                time_patch_auto_nul, edit_target_path, target_bytes,
                xmp_existing_sidecar_base_path, xmp_sidecar_base_path,
                xmp_existing_sidecar_mode, xmp_existing_sidecar_precedence,
                xmp_existing_destination_embedded_path,
                xmp_existing_destination_embedded_mode,
                xmp_existing_destination_embedded_precedence,
                xmp_existing_destination_carrier_precedence,
                xmp_existing_destination_sidecar_state, edit_apply,
                include_edited_bytes, false, xmp_writeback_mode,
                xmp_destination_embedded_mode, xmp_destination_sidecar_mode,
                nb::str(output_path.c_str(), output_path.size()),
                overwrite_output, overwrite_xmp_sidecar,
                remove_destination_xmp_sidecar, target_image_spec,
                source_raw_data_descriptor, transfer_safety,
                raw_carrier_passthrough_mode);
        },
        "snapshot"_a, "target_format"_a = TransferTargetFormat::Jpeg,
        "dng_target_mode"_a = DngTargetMode::MinimalFreshScaffold,
        "format"_a = XmpSidecarFormat::Portable, "include_exif_app1"_a = true,
        "include_xmp_app1"_a = true, "include_icc_app2"_a = true,
        "include_iptc_app13"_a = true, "xmp_include_existing"_a = false,
        "xmp_existing_namespace_policy"_a
        = XmpExistingNamespacePolicy::KnownPortableOnly,
        "xmp_existing_standard_namespace_policy"_a
        = XmpExistingStandardNamespacePolicy::PreserveAll,
        "xmp_conflict_policy"_a            = XmpConflictPolicy::CurrentBehavior,
        "xmp_exiftool_gpsdatetime_alias"_a = false, "xmp_project_exif"_a = true,
        "xmp_project_iptc"_a = true,
        "makernote_policy"_a = TransferPolicyAction::Keep,
        "jumbf_policy"_a     = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "time_patches"_a = nb::none(),
        "time_patch_strict_width"_a = true, "time_patch_require_slot"_a = false,
        "time_patch_auto_nul"_a = true, "edit_target_path"_a = nb::none(),
        "target_bytes"_a                   = nb::none(),
        "xmp_existing_sidecar_base_path"_a = nb::none(),
        "xmp_sidecar_base_path"_a          = nb::none(),
        "xmp_existing_sidecar_mode"_a      = XmpExistingSidecarMode::Ignore,
        "xmp_existing_sidecar_precedence"_a
        = XmpExistingSidecarPrecedence::SidecarWins,
        "xmp_existing_destination_embedded_path"_a = nb::none(),
        "xmp_existing_destination_embedded_mode"_a
        = XmpExistingDestinationEmbeddedMode::Ignore,
        "xmp_existing_destination_embedded_precedence"_a
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
        "xmp_existing_destination_carrier_precedence"_a
        = XmpExistingDestinationCarrierPrecedence::SidecarWins,
        "xmp_existing_destination_sidecar_state"_a
        = XmpExistingDestinationSidecarState::Unknown,
        "edit_apply"_a = true, "include_edited_bytes"_a = false,
        "xmp_writeback_mode"_a = XmpWritebackMode::EmbeddedOnly,
        "xmp_destination_embedded_mode"_a
        = XmpDestinationEmbeddedMode::PreserveExisting,
        "xmp_destination_sidecar_mode"_a
        = XmpDestinationSidecarMode::PreserveExisting,
        "output_path"_a, "overwrite_output"_a = false,
        "overwrite_xmp_sidecar"_a          = false,
        "remove_destination_xmp_sidecar"_a = true,
        "target_image_spec"_a              = nb::none(),
        "source_raw_data_descriptor"_a     = nb::none(),
        "transfer_safety"_a                = TransferSafetyMode::CompatibleFile,
        "raw_carrier_passthrough_mode"_a
        = TransferRawCarrierPassthroughMode::Disabled);

    m.def(
        "unsafe_transfer_snapshot_file",
        [](const TransferSourceSnapshot& snapshot,
           TransferTargetFormat target_format, DngTargetMode dng_target_mode,
           XmpSidecarFormat format, bool include_exif_app1,
           bool include_xmp_app1, bool include_icc_app2,
           bool include_iptc_app13, bool xmp_include_existing,
           XmpExistingNamespacePolicy xmp_existing_namespace_policy,
           XmpExistingStandardNamespacePolicy
               xmp_existing_standard_namespace_policy,
           XmpConflictPolicy xmp_conflict_policy,
           bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
           bool xmp_project_iptc, TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           nb::object time_patches, bool time_patch_strict_width,
           bool time_patch_require_slot, bool time_patch_auto_nul,
           nb::object edit_target_path, nb::object target_bytes,
           nb::object xmp_existing_sidecar_base_path,
           nb::object xmp_sidecar_base_path,
           XmpExistingSidecarMode xmp_existing_sidecar_mode,
           XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
           nb::object xmp_existing_destination_embedded_path,
           XmpExistingDestinationEmbeddedMode
               xmp_existing_destination_embedded_mode,
           XmpExistingDestinationEmbeddedPrecedence
               xmp_existing_destination_embedded_precedence,
           XmpExistingDestinationCarrierPrecedence
               xmp_existing_destination_carrier_precedence,
           XmpExistingDestinationSidecarState
               xmp_existing_destination_sidecar_state,
           bool edit_apply, bool include_edited_bytes,
           XmpWritebackMode xmp_writeback_mode,
           XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
           XmpDestinationSidecarMode xmp_destination_sidecar_mode,
           const std::string& output_path, bool overwrite_output,
           bool overwrite_xmp_sidecar, bool remove_destination_xmp_sidecar,
           nb::object target_image_spec, nb::object source_raw_data_descriptor,
           TransferSafetyMode transfer_safety,
           TransferRawCarrierPassthroughMode raw_carrier_passthrough_mode) {
            return transfer_snapshot_to_python(
                snapshot, target_format, dng_target_mode, format,
                include_exif_app1, include_xmp_app1, include_icc_app2,
                include_iptc_app13, xmp_include_existing,
                xmp_existing_namespace_policy,
                xmp_existing_standard_namespace_policy, xmp_conflict_policy,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy, c2pa_policy,
                max_file_bytes, policy_obj, time_patches,
                time_patch_strict_width, time_patch_require_slot,
                time_patch_auto_nul, edit_target_path, target_bytes,
                xmp_existing_sidecar_base_path, xmp_sidecar_base_path,
                xmp_existing_sidecar_mode, xmp_existing_sidecar_precedence,
                xmp_existing_destination_embedded_path,
                xmp_existing_destination_embedded_mode,
                xmp_existing_destination_embedded_precedence,
                xmp_existing_destination_carrier_precedence,
                xmp_existing_destination_sidecar_state, edit_apply,
                include_edited_bytes, true, xmp_writeback_mode,
                xmp_destination_embedded_mode, xmp_destination_sidecar_mode,
                nb::str(output_path.c_str(), output_path.size()),
                overwrite_output, overwrite_xmp_sidecar,
                remove_destination_xmp_sidecar, target_image_spec,
                source_raw_data_descriptor, transfer_safety,
                raw_carrier_passthrough_mode);
        },
        "snapshot"_a, "target_format"_a = TransferTargetFormat::Jpeg,
        "dng_target_mode"_a = DngTargetMode::MinimalFreshScaffold,
        "format"_a = XmpSidecarFormat::Portable, "include_exif_app1"_a = true,
        "include_xmp_app1"_a = true, "include_icc_app2"_a = true,
        "include_iptc_app13"_a = true, "xmp_include_existing"_a = false,
        "xmp_existing_namespace_policy"_a
        = XmpExistingNamespacePolicy::KnownPortableOnly,
        "xmp_existing_standard_namespace_policy"_a
        = XmpExistingStandardNamespacePolicy::PreserveAll,
        "xmp_conflict_policy"_a            = XmpConflictPolicy::CurrentBehavior,
        "xmp_exiftool_gpsdatetime_alias"_a = false, "xmp_project_exif"_a = true,
        "xmp_project_iptc"_a = true,
        "makernote_policy"_a = TransferPolicyAction::Keep,
        "jumbf_policy"_a     = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "time_patches"_a = nb::none(),
        "time_patch_strict_width"_a = true, "time_patch_require_slot"_a = false,
        "time_patch_auto_nul"_a = true, "edit_target_path"_a = nb::none(),
        "target_bytes"_a                   = nb::none(),
        "xmp_existing_sidecar_base_path"_a = nb::none(),
        "xmp_sidecar_base_path"_a          = nb::none(),
        "xmp_existing_sidecar_mode"_a      = XmpExistingSidecarMode::Ignore,
        "xmp_existing_sidecar_precedence"_a
        = XmpExistingSidecarPrecedence::SidecarWins,
        "xmp_existing_destination_embedded_path"_a = nb::none(),
        "xmp_existing_destination_embedded_mode"_a
        = XmpExistingDestinationEmbeddedMode::Ignore,
        "xmp_existing_destination_embedded_precedence"_a
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
        "xmp_existing_destination_carrier_precedence"_a
        = XmpExistingDestinationCarrierPrecedence::SidecarWins,
        "xmp_existing_destination_sidecar_state"_a
        = XmpExistingDestinationSidecarState::Unknown,
        "edit_apply"_a = true, "include_edited_bytes"_a = false,
        "xmp_writeback_mode"_a = XmpWritebackMode::EmbeddedOnly,
        "xmp_destination_embedded_mode"_a
        = XmpDestinationEmbeddedMode::PreserveExisting,
        "xmp_destination_sidecar_mode"_a
        = XmpDestinationSidecarMode::PreserveExisting,
        "output_path"_a, "overwrite_output"_a = false,
        "overwrite_xmp_sidecar"_a          = false,
        "remove_destination_xmp_sidecar"_a = true,
        "target_image_spec"_a              = nb::none(),
        "source_raw_data_descriptor"_a     = nb::none(),
        "transfer_safety"_a                = TransferSafetyMode::CompatibleFile,
        "raw_carrier_passthrough_mode"_a
        = TransferRawCarrierPassthroughMode::Disabled);

    m.def("dng_sdk_adapter_available",
          []() { return dng_sdk_adapter_available(); });

    m.def(
        "build_exr_attribute_batch_from_file",
        [](const std::string& path, XmpSidecarFormat format,
           bool include_pointer_tags, bool decode_makernote,
           bool decode_embedded_containers, bool decompress,
           bool include_exif_app1, bool include_xmp_app1, bool include_icc_app2,
           bool include_iptc_app13, bool xmp_include_existing,
           XmpExistingNamespacePolicy xmp_existing_namespace_policy,
           XmpExistingStandardNamespacePolicy
               xmp_existing_standard_namespace_policy,
           bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
           bool xmp_project_iptc, TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           bool include_values) {
            return build_exr_attribute_batch_from_file_to_python(
                path, format, include_pointer_tags, decode_makernote,
                decode_embedded_containers, decompress, include_exif_app1,
                include_xmp_app1, include_icc_app2, include_iptc_app13,
                xmp_include_existing, xmp_existing_namespace_policy,
                xmp_existing_standard_namespace_policy,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy, c2pa_policy,
                max_file_bytes, policy_obj, include_values);
        },
        "path"_a, "format"_a = XmpSidecarFormat::Portable,
        "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_embedded_containers"_a = true, "decompress"_a = true,
        "include_exif_app1"_a = true, "include_xmp_app1"_a = true,
        "include_icc_app2"_a = true, "include_iptc_app13"_a = true,
        "xmp_include_existing"_a = false,
        "xmp_existing_namespace_policy"_a
        = XmpExistingNamespacePolicy::KnownPortableOnly,
        "xmp_existing_standard_namespace_policy"_a
        = XmpExistingStandardNamespacePolicy::PreserveAll,
        "xmp_exiftool_gpsdatetime_alias"_a = false, "xmp_project_exif"_a = true,
        "xmp_project_iptc"_a = true,
        "makernote_policy"_a = TransferPolicyAction::Keep,
        "jumbf_policy"_a     = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "include_values"_a = false);

    m.def("exif_orientation_is_valid", &exif_orientation_is_valid,
          "orientation"_a);
    m.def("exif_orientation_is_mirrored", &exif_orientation_is_mirrored,
          "orientation"_a);
    m.def("exif_orientation_swaps_width_height",
          &exif_orientation_swaps_width_height, "orientation"_a);
    m.def("exif_orientation_rotation_degrees_cw",
          &exif_orientation_rotation_degrees_to_python, "orientation"_a);
    m.def("exif_orientation_rotation_only", &exif_orientation_rotation_only,
          "orientation"_a);
    m.def("exif_orientation_name", &exif_orientation_name, "orientation"_a);
    m.def("interpret_exif_orientation", &interpret_exif_orientation_to_python,
          "orientation"_a);

    m.def(
        "map_meta_orientation_to_libraw_flip_from_file",
        [](const std::string& path, LibRawOrientationTarget target,
           bool preserve_embedded_preview_orientation,
           LibRawMirrorPolicy mirror_policy, uint64_t max_file_bytes) {
            return map_libraw_orientation_from_file_to_python(
                path, target, preserve_embedded_preview_orientation,
                mirror_policy, max_file_bytes);
        },
        "path"_a, "target"_a = LibRawOrientationTarget::RawImage,
        "preserve_embedded_preview_orientation"_a = true,
        "mirror_policy"_a                         = LibRawMirrorPolicy::Reject,
        "max_file_bytes"_a                        = 0ULL);

    m.def(
        "map_libraw_flip_to_exif_orientation",
        [](uint32_t libraw_flip, LibRawOrientationTarget target,
           bool preserve_embedded_preview_orientation) {
            return map_libraw_flip_to_exif_to_python(
                libraw_flip, target, preserve_embedded_preview_orientation);
        },
        "libraw_flip"_a, "target"_a = LibRawOrientationTarget::RawImage,
        "preserve_embedded_preview_orientation"_a = true);

    m.def(
        "update_dng_sdk_file_from_file",
        [](const std::string& source_path, const std::string& target_path,
           DngTargetMode dng_target_mode, XmpSidecarFormat format,
           bool include_pointer_tags, bool decode_makernote,
           bool decode_embedded_containers, bool decompress,
           bool include_exif_app1, bool include_xmp_app1, bool include_icc_app2,
           bool include_iptc_app13, bool xmp_include_existing,
           XmpExistingNamespacePolicy xmp_existing_namespace_policy,
           XmpExistingStandardNamespacePolicy
               xmp_existing_standard_namespace_policy,
           bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
           bool xmp_project_iptc, TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj, bool apply_exif,
           bool apply_xmp, bool apply_iptc, bool synchronize_metadata,
           bool cleanup_for_update) {
            return update_dng_sdk_file_from_file_to_python(
                source_path, target_path, dng_target_mode, format,
                include_pointer_tags, decode_makernote,
                decode_embedded_containers, decompress, include_exif_app1,
                include_xmp_app1, include_icc_app2, include_iptc_app13,
                xmp_include_existing, xmp_existing_namespace_policy,
                xmp_existing_standard_namespace_policy,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy, c2pa_policy,
                max_file_bytes, policy_obj, apply_exif, apply_xmp, apply_iptc,
                synchronize_metadata, cleanup_for_update);
        },
        "source_path"_a, "target_path"_a,
        "dng_target_mode"_a      = DngTargetMode::MinimalFreshScaffold,
        "format"_a               = XmpSidecarFormat::Portable,
        "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_embedded_containers"_a = true, "decompress"_a = true,
        "include_exif_app1"_a = true, "include_xmp_app1"_a = true,
        "include_icc_app2"_a = true, "include_iptc_app13"_a = true,
        "xmp_include_existing"_a = false,
        "xmp_existing_namespace_policy"_a
        = XmpExistingNamespacePolicy::KnownPortableOnly,
        "xmp_existing_standard_namespace_policy"_a
        = XmpExistingStandardNamespacePolicy::PreserveAll,
        "xmp_exiftool_gpsdatetime_alias"_a = false, "xmp_project_exif"_a = true,
        "xmp_project_iptc"_a = true,
        "makernote_policy"_a = TransferPolicyAction::Keep,
        "jumbf_policy"_a     = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "apply_exif"_a = true, "apply_xmp"_a = true,
        "apply_iptc"_a = true, "synchronize_metadata"_a = true,
        "cleanup_for_update"_a = true);

    m.def(
        "inspect_transfer_payload_batch",
        [](nb::object payload_batch, bool include_payloads) {
            return inspect_transfer_payload_batch_to_python(payload_batch,
                                                            include_payloads,
                                                            false);
        },
        "payload_batch"_a, "include_payloads"_a = false);

    m.def(
        "unsafe_inspect_transfer_payload_batch",
        [](nb::object payload_batch, bool include_payloads) {
            return inspect_transfer_payload_batch_to_python(payload_batch,
                                                            include_payloads,
                                                            true);
        },
        "payload_batch"_a, "include_payloads"_a = false);

    m.def(
        "inspect_transfer_package_batch",
        [](nb::object package_batch, bool include_chunk_bytes) {
            return inspect_transfer_package_batch_to_python(package_batch,
                                                            include_chunk_bytes,
                                                            false);
        },
        "package_batch"_a, "include_chunk_bytes"_a = false);

    m.def(
        "unsafe_inspect_transfer_package_batch",
        [](nb::object package_batch, bool include_chunk_bytes) {
            return inspect_transfer_package_batch_to_python(package_batch,
                                                            include_chunk_bytes,
                                                            true);
        },
        "package_batch"_a, "include_chunk_bytes"_a = false);

    m.def(
        "inspect_jxl_encoder_handoff",
        [](nb::object handoff, bool include_icc_profile) {
            return inspect_jxl_encoder_handoff_to_python(handoff,
                                                         include_icc_profile,
                                                         false);
        },
        "handoff"_a, "include_icc_profile"_a = false);

    m.def(
        "unsafe_inspect_jxl_encoder_handoff",
        [](nb::object handoff, bool include_icc_profile) {
            return inspect_jxl_encoder_handoff_to_python(handoff,
                                                         include_icc_profile,
                                                         true);
        },
        "handoff"_a, "include_icc_profile"_a = false);

    m.def(
        "inspect_transfer_artifact",
        [](nb::object artifact) {
            return inspect_transfer_artifact_to_python(artifact);
        },
        "artifact"_a);

    m.def("console_text", &console_text, "data"_a, "max_bytes"_a = 4096U);
    m.def("hex_bytes", &hex_bytes, "data"_a, "max_bytes"_a = 4096U);
    m.def("unsafe_text", &unsafe_text, "data"_a, "max_bytes"_a = 4096U);
    m.def("unsafe_test", &unsafe_text, "data"_a, "max_bytes"_a = 4096U);

    m.def("build_info", []() {
        const BuildInfo& bi = build_info();
        nb::dict d;
        d["version"]                 = sv_to_py(bi.version);
        d["build_timestamp_utc"]     = sv_to_py(bi.build_timestamp_utc);
        d["build_type"]              = sv_to_py(bi.build_type);
        d["cmake_generator"]         = sv_to_py(bi.cmake_generator);
        d["system_name"]             = sv_to_py(bi.system_name);
        d["system_processor"]        = sv_to_py(bi.system_processor);
        d["cxx_compiler_id"]         = sv_to_py(bi.cxx_compiler_id);
        d["cxx_compiler_version"]    = sv_to_py(bi.cxx_compiler_version);
        d["cxx_compiler"]            = sv_to_py(bi.cxx_compiler);
        d["linkage_static"]          = nb::bool_(bi.linkage_static);
        d["linkage_shared"]          = nb::bool_(bi.linkage_shared);
        d["option_with_zlib"]        = nb::bool_(bi.option_with_zlib);
        d["option_with_brotli"]      = nb::bool_(bi.option_with_brotli);
        d["option_with_expat"]       = nb::bool_(bi.option_with_expat);
        d["option_enable_rapidfuzz"] = nb::bool_(bi.option_enable_rapidfuzz);
        d["has_zlib"]                = nb::bool_(bi.has_zlib);
        d["has_brotli"]              = nb::bool_(bi.has_brotli);
        d["has_expat"]               = nb::bool_(bi.has_expat);
        d["has_rapidfuzz"]           = nb::bool_(bi.has_rapidfuzz);
        return d;
    });

    m.def("info_lines", &info_lines);
    m.def("python_info_line", &python_info_line);

    m.def(
        "icc_tag_name",
        [](uint32_t signature) -> nb::object {
            const std::string_view n = icc_tag_name(signature);
            if (n.empty()) {
                return nb::none();
            }
            return nb::str(n.data(), n.size());
        },
        "signature"_a);

    m.def(
        "icc_interpret",
        [](uint32_t signature, nb::bytes tag_bytes, uint32_t max_values,
           uint32_t max_text_bytes) {
            return icc_interpret_to_python(signature, tag_bytes, max_values,
                                           max_text_bytes);
        },
        "signature"_a, "tag_bytes"_a, "max_values"_a = 512U,
        "max_text_bytes"_a = 4096U);

    m.def(
        "icc_render_value",
        [](uint32_t signature, nb::bytes tag_bytes, uint32_t max_values,
           uint32_t max_text_bytes) {
            return icc_render_value_to_python(signature, tag_bytes, max_values,
                                              max_text_bytes);
        },
        "signature"_a, "tag_bytes"_a, "max_values"_a = 512U,
        "max_text_bytes"_a = 4096U);

    m.def(
        "exif_tag_name",
        [](const std::string& ifd, uint16_t tag) -> nb::object {
            const std::string_view n = exif_tag_name(ifd, tag);
            if (n.empty()) {
                return nb::none();
            }
            return nb::str(n.data(), n.size());
        },
        "ifd"_a, "tag"_a);
}
