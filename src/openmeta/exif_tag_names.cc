// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_store.h"

#include <cstdint>
#include <span>

namespace openmeta::exif_internal {
std::string_view sony_private_field_name(uint8_t index) noexcept;
std::string_view nikon_capture_field_name(uint8_t index) noexcept;
}

namespace openmeta {
std::string_view
makernote_tag_name(std::string_view ifd, uint16_t tag) noexcept;

namespace {

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool parse_ciff_dir_id(std::string_view ifd, uint16_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (ifd == "ciff_root") {
            *out = 0xFFFFU;
            return true;
        }
        if (!ifd.starts_with("ciff_") || ifd.size() < 10U || ifd[9] != '_') {
            return false;
        }

        uint16_t dir_id = 0U;
        for (size_t i = 5U; i < 9U; ++i) {
            const char c    = ifd[i];
            uint16_t nibble = 0U;
            if (c >= '0' && c <= '9') {
                nibble = static_cast<uint16_t>(c - '0');
            } else if (c >= 'A' && c <= 'F') {
                nibble = static_cast<uint16_t>(10 + (c - 'A'));
            } else if (c >= 'a' && c <= 'f') {
                nibble = static_cast<uint16_t>(10 + (c - 'a'));
            } else {
                return false;
            }
            dir_id = static_cast<uint16_t>((dir_id << 4U) | nibble);
        }

        *out = dir_id;
        return true;
    }


    static std::string_view
    ciff_synthetic_table_suffix(std::string_view ifd) noexcept
    {
        if (!ifd.starts_with("ciff_") || ifd.size() < 12U) {
            return {};
        }
        size_t pos = 10U;
        while (pos < ifd.size() && ifd[pos] != '_') {
            const char c = ifd[pos];
            if (c < '0' || c > '9') {
                return {};
            }
            ++pos;
        }
        if (pos >= ifd.size() || ifd[pos] != '_') {
            return {};
        }
        return ifd.substr(pos + 1U);
    }

    static std::string_view
    synthesize_fujifilm_main_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[20];
        static constexpr std::string_view kPrefix = "FujiFilm_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[kPrefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[kPrefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[kPrefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[kPrefix.size() + 4U] = '\0';
        return std::string_view(buf, kPrefix.size() + 4U);
    }

    static std::string_view
    synthesize_raf_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[24];
        static constexpr std::string_view kPrefix = "FujiFilmRAF_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[kPrefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[kPrefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[kPrefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[kPrefix.size() + 4U] = '\0';
        return std::string_view(buf, kPrefix.size() + 4U);
    }

    static bool is_raf_ifd(std::string_view ifd) noexcept
    {
        if (!ifd.starts_with("raf_") || ifd.size() <= 4U) {
            return false;
        }
        for (size_t i = 4U; i < ifd.size(); ++i) {
            if (ifd[i] < '0' || ifd[i] > '9') {
                return false;
            }
        }
        return true;
    }

    static std::string_view raf_header_tag_name(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x003cU: return "FirmwareVersion";
        case 0x0048U: return "MRawHeaderOffset";
        case 0x004cU: return "MRawHeaderLength";
        case 0x0054U: return "PreviewImageStart";
        case 0x0058U: return "PreviewImageLength";
        case 0x005cU: return "RAFDirectoryOffset";
        case 0x0060U: return "RAFDirectoryLength";
        case 0x0064U: return "FujiIFDOffset";
        case 0x0068U: return "FujiIFDLength";
        case 0x006cU: return "RAFCompression";
        case 0x0078U: return "RAFDirectory1Offset";
        case 0x007cU: return "RAFDirectory1Length";
        case 0x0080U: return "FujiIFD1Offset";
        case 0x0084U: return "FujiIFD1Length";
        default: return {};
        }
    }

    static std::string_view raf_tag_name(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x0100U: return "RawImageFullSize";
        case 0x0110U: return "RawImageCropTopLeft";
        case 0x0111U: return "RawImageCroppedSize";
        case 0x0115U: return "RawImageAspectRatio";
        case 0x0117U: return "RawZoomActive";
        case 0x0118U: return "RawZoomTopLeft";
        case 0x0119U: return "RawZoomSize";
        case 0x0121U: return "RawImageSize";
        case 0x0130U: return "FujiLayout";
        case 0x0131U: return "XTransLayout";
        case 0x9200U: return "RelativeExposure";
        case 0x9650U: return "RawExposureBias";
        case 0xc000U: return "RAFData";
        default: return synthesize_raf_placeholder_name(tag);
        }
    }

    static std::string_view
    synthesize_x3f_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[20];
        static constexpr std::string_view kPrefix = "SigmaX3F_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[kPrefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[kPrefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[kPrefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[kPrefix.size() + 4U] = '\0';
        return std::string_view(buf, kPrefix.size() + 4U);
    }

    static std::string_view x3f_header_tag_name(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x0001U: return "FileVersion";
        case 0x0002U: return "ImageUniqueID";
        case 0x0006U: return "MarkBits";
        case 0x0007U: return "ImageWidth";
        case 0x0008U: return "ImageHeight";
        case 0x0009U: return "Rotation";
        case 0x000aU: return "WhiteBalance";
        case 0x0012U: return "SceneCaptureType";
        default: return synthesize_x3f_placeholder_name(tag);
        }
    }

    static std::string_view x3f_header_ext_tag_name(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x0001U: return "ExposureAdjust";
        case 0x0002U: return "Contrast";
        case 0x0003U: return "Shadow";
        case 0x0004U: return "Highlight";
        case 0x0005U: return "Saturation";
        case 0x0006U: return "Sharpness";
        case 0x0007U: return "RedAdjust";
        case 0x0008U: return "GreenAdjust";
        case 0x0009U: return "BlueAdjust";
        case 0x000aU: return "X3FillLight";
        default: return synthesize_x3f_placeholder_name(tag);
        }
    }

    static std::string_view x3f_prop_tag_name(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x0001U: return "MeteringMode";
        case 0x0002U: return "AFArea";
        case 0x0003U: return "AFInFocus";
        case 0x0004U: return "FocusMode";
        case 0x0005U: return "ApertureDisplayed";
        case 0x0006U: return "FNumber";
        case 0x0007U: return "Make";
        case 0x0008U: return "Model";
        case 0x0009U: return "CameraName";
        case 0x000aU: return "SerialNumber";
        case 0x000bU: return "ColorSpace";
        case 0x000cU: return "DriveMode";
        case 0x000dU: return "ExposureCompensation";
        case 0x000eU: return "NetExposureCompensation";
        case 0x000fU: return "IntegrationTime";
        case 0x0010U: return "FirmwareVersion";
        case 0x0011U: return "FlashMode";
        case 0x0012U: return "FocalLength";
        case 0x0013U: return "FocalLengthIn35mmFormat";
        case 0x0014U: return "Focus";
        case 0x0015U: return "SensorTemperature";
        case 0x0016U: return "ISO";
        case 0x0017U: return "LensType";
        case 0x0018U: return "ExposureProgram";
        case 0x0019U: return "Quality";
        case 0x001aU: return "DateTimeOriginal";
        case 0x001bU: return "WhiteBalance";
        case 0x001cU: return "SceneCaptureType";
        case 0x001dU: return "ExposureTime";
        case 0x001eU: return "ShutterSpeedDisplayed";
        case 0x001fU: return "LensApertureRange";
        case 0x0020U: return "LensFocalRange";
        case 0x0021U: return "BurstShot";
        case 0x0022U: return "BracketShot";
        case 0x0023U: return "EvalState";
        case 0x0024U: return "ImagerBoardID";
        case 0x0025U: return "ImageBoardID";
        case 0x0026U: return "SensorID";
        default: return synthesize_x3f_placeholder_name(tag);
        }
    }

    static std::string_view
    synthesize_casio_main_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[16];
        static constexpr std::string_view kPrefix = "Casio_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[kPrefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[kPrefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[kPrefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[kPrefix.size() + 4U] = '\0';
        return std::string_view(buf, kPrefix.size() + 4U);
    }

    static std::string_view
    synthesize_nikonsettings_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[25];
        static constexpr std::string_view kPrefix = "NikonSettings_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[kPrefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[kPrefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[kPrefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[kPrefix.size() + 4U] = '\0';
        return std::string_view(buf, kPrefix.size() + 4U);
    }

    static std::string_view
    nikon_main_compact_type2_compat_name(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x0002U: return "Nikon_Type2_0x0002";
        case 0x0003U: return "Quality";
        case 0x0004U: return "ColorMode";
        case 0x0005U: return "ImageAdjustment";
        case 0x0006U: return "CCDSensitivity";
        case 0x0007U: return "WhiteBalance";
        case 0x0008U: return "Focus";
        case 0x0009U: return "Nikon_Type2_0x0009";
        case 0x000AU: return "DigitalZoom";
        case 0x000BU: return "Converter";
        case 0x0F00U: return "Nikon_Type2_0x0f00";
        default: return {};
        }
    }

    static std::string_view
    find_first_makernote_name(std::span<const std::string_view> ifds,
                              uint16_t tag) noexcept
    {
        for (size_t i = 0; i < ifds.size(); ++i) {
            const std::string_view name = makernote_tag_name(ifds[i], tag);
            if (!name.empty()) {
                return name;
            }
        }
        return {};
    }

    static std::string_view
    synthesize_minolta_main_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[18];
        static constexpr std::string_view kPrefix = "Minolta_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[kPrefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[kPrefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[kPrefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[kPrefix.size() + 4U] = '\0';
        return std::string_view(buf, kPrefix.size() + 4U);
    }

    static std::string_view
    synthesize_panasonic_main_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[21];
        static constexpr std::string_view kPrefix = "Panasonic_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[kPrefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[kPrefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[kPrefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[kPrefix.size() + 4U] = '\0';
        return std::string_view(buf, kPrefix.size() + 4U);
    }

    static std::string_view
    synthesize_sigma_main_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[16];
        static constexpr std::string_view kPrefix = "Sigma_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[kPrefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[kPrefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[kPrefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[kPrefix.size() + 4U] = '\0';
        return std::string_view(buf, kPrefix.size() + 4U);
    }

    static std::string_view
    synthesize_canonraw_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[19];
        static constexpr std::string_view kPrefix = "CanonRaw_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[kPrefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[kPrefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[kPrefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[kPrefix.size() + 4U] = '\0';
        return std::string_view(buf, kPrefix.size() + 4U);
    }

    static bool sigma_main_prefers_placeholder(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x001AU:
        case 0x001BU:
        case 0x0022U:
        case 0x0024U:
        case 0x002CU:
        case 0x0031U:
        case 0x0032U:
        case 0x0035U:
        case 0x0039U:
        case 0x003AU:
        case 0x003BU:
        case 0x003CU:
        case 0x0047U:
        case 0x0113U: return true;
        default: return false;
        }
    }

    static std::string_view sigma_main_fixed_compat_name(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x001CU: return "PreviewImageStart";
        case 0x001DU: return "PreviewImageLength";
        case 0x001FU: return "MakerNoteVersion";
        case 0x0030U: return "Calibration";
        default: return {};
        }
    }

    static std::string_view
    synthesize_ricoh_main_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[16];
        static constexpr std::string_view kPrefix = "Ricoh_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[kPrefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[kPrefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[kPrefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[kPrefix.size() + 4U] = '\0';
        return std::string_view(buf, kPrefix.size() + 4U);
    }

    static std::string_view
    synthesize_sony_main_placeholder_name(uint16_t tag) noexcept
    {
        static thread_local char buf[15];
        static constexpr std::string_view kPrefix = "Sony_0x";
        static constexpr char kHex[]              = "0123456789abcdef";
        if (kPrefix.size() + 4U >= sizeof(buf)) {
            return {};
        }

        for (size_t i = 0; i < kPrefix.size(); ++i) {
            buf[i] = kPrefix[i];
        }
        buf[kPrefix.size() + 0U] = kHex[(tag >> 12U) & 0xFU];
        buf[kPrefix.size() + 1U] = kHex[(tag >> 8U) & 0xFU];
        buf[kPrefix.size() + 2U] = kHex[(tag >> 4U) & 0xFU];
        buf[kPrefix.size() + 3U] = kHex[(tag >> 0U) & 0xFU];
        buf[kPrefix.size() + 4U] = '\0';
        return std::string_view(buf, kPrefix.size() + 4U);
    }

    enum class ExifIfdGroup : uint8_t {
        TiffIfd,
        ExifIfd,
        GpsIfd,
        InteropIfd,
        MpfIfd,
        Unknown,
    };

    static ExifIfdGroup exif_ifd_group(std::string_view ifd) noexcept
    {
        if (ifd == "exififd" || ifd.ends_with("_exififd")) {
            return ExifIfdGroup::ExifIfd;
        }
        if (ifd == "gpsifd" || ifd.ends_with("_gpsifd")) {
            return ExifIfdGroup::GpsIfd;
        }
        if (ifd == "interopifd" || ifd.ends_with("_interopifd")) {
            return ExifIfdGroup::InteropIfd;
        }
        if (ifd.starts_with("ifd") || ifd.starts_with("subifd")) {
            return ExifIfdGroup::TiffIfd;
        }
        if (ifd.starts_with("mkifd") || ifd.starts_with("mk_subifd")) {
            return ExifIfdGroup::TiffIfd;
        }
        if (ifd.starts_with("mpf")) {
            return ExifIfdGroup::MpfIfd;
        }
        return ExifIfdGroup::Unknown;
    }

    struct StandardTagNameEntry final {
        uint16_t tag     = 0;
        const char* name = nullptr;
    };

#include "exif_standard_tag_names_generated.inc"

    static std::string_view find_tag_name(const StandardTagNameEntry* entries,
                                          uint32_t count, uint16_t tag) noexcept
    {
        if (!entries || count == 0) {
            return {};
        }

        uint32_t lo = 0;
        uint32_t hi = count;
        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2;
            const uint16_t cur = entries[mid].tag;
            if (cur < tag) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        if (lo < count && entries[lo].tag == tag && entries[lo].name) {
            return entries[lo].name;
        }
        return {};
    }


    static std::string_view tiff_ifd_tag_name(uint16_t tag) noexcept
    {
        return find_tag_name(kStandardIfdTags,
                             sizeof(kStandardIfdTags)
                                 / sizeof(kStandardIfdTags[0]),
                             tag);
    }


    static std::string_view exif_ifd_tag_name(uint16_t tag) noexcept
    {
        return find_tag_name(kStandardExifIfdTags,
                             sizeof(kStandardExifIfdTags)
                                 / sizeof(kStandardExifIfdTags[0]),
                             tag);
    }


    static std::string_view gps_ifd_tag_name(uint16_t tag) noexcept
    {
        return find_tag_name(kStandardGpsIfdTags,
                             sizeof(kStandardGpsIfdTags)
                                 / sizeof(kStandardGpsIfdTags[0]),
                             tag);
    }


    static std::string_view interop_ifd_tag_name(uint16_t tag) noexcept
    {
        return find_tag_name(kStandardInteropIfdTags,
                             sizeof(kStandardInteropIfdTags)
                                 / sizeof(kStandardInteropIfdTags[0]),
                             tag);
    }


    static std::string_view mpf_ifd_tag_name(uint16_t tag) noexcept
    {
        return find_tag_name(kStandardMpfTags,
                             sizeof(kStandardMpfTags)
                                 / sizeof(kStandardMpfTags[0]),
                             tag);
    }


    static std::string_view ciff_tag_name(std::string_view ifd,
                                          uint16_t tag) noexcept
    {
        uint16_t dir_id = 0U;
        if (!parse_ciff_dir_id(ifd, &dir_id)) {
            return {};
        }
        const std::string_view synthetic = ciff_synthetic_table_suffix(ifd);

        if (synthetic == "timestamp") {
            switch (tag) {
            case 0x0000U: return "DateTimeOriginal";
            case 0x0001U: return "TimeZoneCode";
            case 0x0002U: return "TimeZoneInfo";
            default: return {};
            }
        }

        if (synthetic == "imageinfo") {
            switch (tag) {
            case 0x0000U: return "ImageWidth";
            case 0x0001U: return "ImageHeight";
            case 0x0002U: return "PixelAspectRatio";
            case 0x0003U: return "Rotation";
            case 0x0004U: return "ComponentBitDepth";
            case 0x0005U: return "ColorBitDepth";
            case 0x0006U: return "ColorBW";
            default: return {};
            }
        }

        if (synthetic == "imageformat") {
            switch (tag) {
            case 0x0000U: return "FileFormat";
            case 0x0001U: return "TargetCompressionRatio";
            default: return {};
            }
        }

        if (synthetic == "makemodel") {
            switch (tag) {
            case 0x0000U: return "Make";
            case 0x0006U: return "Model";
            default: return {};
            }
        }

        if (synthetic == "exposureinfo") {
            switch (tag) {
            case 0x0000U: return "ExposureCompensation";
            case 0x0001U: return "ShutterSpeedValue";
            case 0x0002U: return "ApertureValue";
            default: return {};
            }
        }

        if (synthetic == "flashinfo") {
            switch (tag) {
            case 0x0000U: return "FlashGuideNumber";
            case 0x0001U: return "FlashThreshold";
            default: return {};
            }
        }

        if (synthetic == "focallength") {
            switch (tag) {
            case 0x0000U: return "FocalType";
            case 0x0001U: return "FocalLength";
            case 0x0002U: return "FocalPlaneXSize";
            case 0x0003U: return "FocalPlaneYSize";
            default: return {};
            }
        }

        if (synthetic == "shotinfo") {
            switch (tag) {
            case 0x0001U: return "AutoISO";
            case 0x0002U: return "BaseISO";
            case 0x0003U: return "MeasuredEV";
            case 0x0004U: return "TargetAperture";
            case 0x0005U: return "TargetExposureTime";
            case 0x0006U: return "ExposureCompensation";
            case 0x0007U: return "WhiteBalance";
            case 0x0008U: return "SlowShutter";
            case 0x0009U: return "SequenceNumber";
            case 0x000AU: return "OpticalZoomCode";
            default: return {};
            }
        }

        if (synthetic == "decodertable") {
            switch (tag) {
            case 0x0000U: return "DecoderTableNumber";
            case 0x0002U: return "CompressedDataOffset";
            case 0x0003U: return "CompressedDataLength";
            default: return {};
            }
        }

        if (synthetic == "rawjpginfo") {
            switch (tag) {
            case 0x0001U: return "RawJpgQuality";
            case 0x0002U: return "RawJpgSize";
            case 0x0003U: return "RawJpgWidth";
            case 0x0004U: return "RawJpgHeight";
            default: return {};
            }
        }

        if (synthetic == "whitesample") {
            switch (tag) {
            case 0x0001U: return "WhiteSampleWidth";
            case 0x0002U: return "WhiteSampleHeight";
            case 0x0003U: return "WhiteSampleLeftBorder";
            case 0x0004U: return "WhiteSampleTopBorder";
            case 0x0005U: return "WhiteSampleBits";
            default: return {};
            }
        }

        std::string_view name;
        if (dir_id == 0xFFFFU) {
            switch (tag) {
            case 0x2005U: name = "RawData"; break;
            default: break;
            }
            return name;
        }

        switch (dir_id) {
        case 0x2804U:
            switch (tag) {
            case 0x0805U: name = "CanonFileDescription"; break;
            case 0x0815U: name = "CanonImageType"; break;
            default: break;
            }
            break;
        case 0x2807U:
            switch (tag) {
            case 0x080AU: name = "MakeModel"; break;
            case 0x0810U: name = "OwnerName"; break;
            default: break;
            }
            break;
        case 0x3002U:
            switch (tag) {
            case 0x1010U: name = "ShutterReleaseMethod"; break;
            case 0x1011U: name = "ShutterReleaseTiming"; break;
            case 0x1016U: name = "ReleaseSetting"; break;
            case 0x1807U: name = "TargetDistanceSetting"; break;
            case 0x1813U: name = "FlashInfo"; break;
            default: break;
            }
            break;
        case 0x3003U:
            switch (tag) {
            case 0x1814U: name = "MeasuredEV"; break;
            default: break;
            }
            break;
        case 0x3004U:
            switch (tag) {
            case 0x080BU: name = "CanonFirmwareVersion"; break;
            case 0x080CU: name = "ComponentVersion"; break;
            case 0x080DU: name = "ROMOperationMode"; break;
            case 0x101CU: name = "BaseISO"; break;
            case 0x180BU: name = "UnknownNumber"; break;
            case 0x1834U: name = "CanonModelID"; break;
            case 0x1835U: name = "DecoderTable"; break;
            case 0x183BU: name = "SerialNumberFormat"; break;
            default: break;
            }
            break;
        case 0x300AU:
            switch (tag) {
            case 0x0816U: name = "OriginalFileName"; break;
            case 0x0817U: name = "ThumbnailFileName"; break;
            case 0x100AU: name = "TargetImageType"; break;
            case 0x1803U: name = "ImageFormat"; break;
            case 0x1804U: name = "RecordID"; break;
            case 0x1806U: name = "SelfTimerTime"; break;
            case 0x180EU: name = "TimeStamp"; break;
            case 0x1810U: name = "ImageInfo"; break;
            case 0x1817U: name = "FileNumber"; break;
            default: break;
            }
            break;
        case 0x300BU:
            switch (tag) {
            case 0x1030U: name = "WhiteSample"; break;
            case 0x1028U: name = "CanonFlashInfo"; break;
            case 0x1029U: name = "FocalLength"; break;
            case 0x102AU: name = "CanonShotInfo"; break;
            case 0x10B5U: name = "RawJpgInfo"; break;
            default: break;
            }
            break;
        default: break;
        }
        if (!name.empty()) {
            return name;
        }
        return synthesize_canonraw_placeholder_name(tag);
    }


    static std::string_view
    contextual_exif_entry_name(const MetaStore& store, const Entry& entry,
                               ExifTagNamePolicy policy,
                               std::string_view canonical) noexcept
    {
        if (any(entry.flags, EntryFlags::ContextualName)
            && entry.origin.name_context_kind
                   == EntryNameContextKind::CanonColorBalance001D)
            return "BlackLevels";
        if (any(entry.flags, EntryFlags::ContextualName)
            && entry.origin.name_context_kind
                   == EntryNameContextKind::CanonAfInfo0011)
            return "Canon_AFInfo_0x000b";
        if (any(entry.flags, EntryFlags::ContextualName)
            && entry.origin.name_context_kind
                   == EntryNameContextKind::SonyPrivateField) {
            const std::string_view name = exif_internal::sony_private_field_name(
                entry.origin.name_context_variant);
            if (!name.empty())
                return name;
        }
        if (any(entry.flags, EntryFlags::ContextualName)
            && entry.origin.name_context_kind
                   == EntryNameContextKind::NikonCaptureField) {
            const std::string_view name
                = exif_internal::nikon_capture_field_name(
                    entry.origin.name_context_variant);
            if (!name.empty())
                return name;
        }
        if (entry.key.kind == MetaKeyKind::ExifTag
            && entry.key.data.exif_tag.tag == 0x0004U
            && arena_string(store.arena(), entry.key.data.exif_tag.ifd)
                   .starts_with("mk_minoltaraw_wbg_")) {
            for (const Entry& model_entry : store.entries()) {
                if (model_entry.key.kind != MetaKeyKind::ExifTag
                    || model_entry.key.data.exif_tag.tag != 0x0110U
                    || model_entry.value.kind != MetaValueKind::Text
                    || arena_string(store.arena(),
                                    model_entry.key.data.exif_tag.ifd)
                           != "ifd0")
                    continue;
                const auto model                = arena_string(store.arena(),
                                                               model_entry.value.data.span);
                constexpr std::string_view name = "DiMAGE A200";
                const size_t position           = model.find(name);
                if (position != std::string_view::npos) {
                    const size_t end = position + name.size();
                    const char next  = end < model.size() ? model[end] : '\0';
                    const bool word  = (next >= 'a' && next <= 'z')
                                      || (next >= 'A' && next <= 'Z')
                                      || (next >= '0' && next <= '9')
                                      || next == '_';
                    if (!word)
                        return "WB_GBRGLevels";
                }
                break;
            }
        }
        if (policy != ExifTagNamePolicy::ExifToolCompat) {
            return canonical;
        }

        const ByteArena& arena = store.arena();
        if (canonical == "SerialNumber"
            && entry.key.kind == MetaKeyKind::ExifTag
            && entry.key.data.exif_tag.tag == 0xA002U
            && arena_string(arena, entry.key.data.exif_tag.ifd)
                   == "mk_samsung_type2_0") {
            return "Samsung_Type2_0xa002";
        }
        if (canonical == "ImageStabilization"
            && entry.key.kind == MetaKeyKind::ExifTag) {
            const std::string_view ifd
                = arena_string(arena, entry.key.data.exif_tag.ifd);
            const uint16_t tag = entry.key.data.exif_tag.tag;
            if (ifd == "mk_minolta0" && (tag == 0x0018U || tag == 0x0113U)) {
                return synthesize_minolta_main_placeholder_name(tag);
            }
        }
        if (entry.key.kind == MetaKeyKind::ExifTag) {
            const std::string_view ifd
                = arena_string(arena, entry.key.data.exif_tag.ifd);
            const uint16_t tag = entry.key.data.exif_tag.tag;
            if (ifd == "mk_sigma0") {
                const std::string_view compat = sigma_main_fixed_compat_name(
                    tag);
                if (!compat.empty()) {
                    return compat;
                }
                if (sigma_main_prefers_placeholder(tag)) {
                    return synthesize_sigma_main_placeholder_name(tag);
                }
            }
            if (ifd == "mk_panasonic0") {
                if (canonical == "Model"
                    && (tag == 0x0004U || tag == 0x000CU || tag == 0x0016U)
                    && entry.value.kind != MetaValueKind::Text) {
                    return synthesize_panasonic_main_placeholder_name(tag);
                }
                switch (tag) {
                case 0x0058U:
                case 0x005AU:
                case 0x005CU:
                case 0x00DEU:
                case 0x00E9U:
                case 0x00F1U:
                case 0x00F3U:
                case 0x00F4U:
                case 0x00F5U:
                    return synthesize_panasonic_main_placeholder_name(tag);
                case 0x00C4U:
                    if (entry.value.kind == MetaValueKind::Scalar
                        && entry.value.data.u64 == 65535ULL) {
                        return synthesize_panasonic_main_placeholder_name(tag);
                    }
                    break;
                default: break;
                }
            }
            if (ifd == "mk_nikon_menusettingsz8_0" && tag == 0x027CU
                && canonical == "HighFrequencyFlickerReduction") {
                return "HighFrequencyFlickerReductionShooting";
            }
        }

        if (!any(entry.flags, EntryFlags::ContextualName)) {
            return canonical;
        }

        switch (entry.origin.name_context_kind) {
        case EntryNameContextKind::None: return canonical;
        case EntryNameContextKind::CanonColorBalance001D:
        case EntryNameContextKind::CanonAfInfo0011:
        case EntryNameContextKind::NikonCaptureField:
        case EntryNameContextKind::SonyPrivateField: return canonical;
        case EntryNameContextKind::CasioType2Legacy:
            switch (entry.origin.name_context_variant) {
            case 1: {
                const std::string_view legacy
                    = makernote_tag_name("mk_casio0",
                                         entry.key.data.exif_tag.tag);
                if (!legacy.empty()) {
                    return legacy;
                }
                if (entry.key.data.exif_tag.tag == 0x0E00U) {
                    return "PrintIM";
                }
                return synthesize_casio_main_placeholder_name(
                    entry.key.data.exif_tag.tag);
            }
            default: return canonical;
            }
        case EntryNameContextKind::FujifilmMain1304:
            switch (entry.origin.name_context_variant) {
            case 1:
                return synthesize_fujifilm_main_placeholder_name(
                    entry.key.data.exif_tag.tag);
            default: return canonical;
            }
        case EntryNameContextKind::OlympusFocusInfo1600:
            switch (entry.origin.name_context_variant) {
            case 1: return "ImageStabilization";
            case 2: return "Olympus_FocusInfo_0x1600";
            default: return canonical;
            }
        case EntryNameContextKind::KodakMain0028:
            switch (entry.origin.name_context_variant) {
            case 1: return "KodakModel";
            default: return canonical;
            }
        case EntryNameContextKind::MinoltaMainCompat:
            switch (entry.origin.name_context_variant) {
            case 1:
                return synthesize_minolta_main_placeholder_name(
                    entry.key.data.exif_tag.tag);
            case 2: return "MinoltaQuality";
            default: return canonical;
            }
        case EntryNameContextKind::MotorolaMain6420:
            switch (entry.origin.name_context_variant) {
            case 1: return "Motorola_0x6420";
            default: return canonical;
            }
        case EntryNameContextKind::SonyMainCompat:
            switch (entry.origin.name_context_variant) {
            case 1:
                return synthesize_sony_main_placeholder_name(
                    entry.key.data.exif_tag.tag);
            default: return canonical;
            }
        case EntryNameContextKind::SonyTag94060005:
            switch (entry.origin.name_context_variant) {
            case 1: return "BatteryLevel";
            default: return canonical;
            }
        case EntryNameContextKind::SigmaMainCompat:
            switch (entry.origin.name_context_variant) {
            case 1:
                return synthesize_sigma_main_placeholder_name(
                    entry.key.data.exif_tag.tag);
            default: return canonical;
            }
        case EntryNameContextKind::RicohMainCompat:
            switch (entry.origin.name_context_variant) {
            case 1:
                return synthesize_ricoh_main_placeholder_name(
                    entry.key.data.exif_tag.tag);
            case 2: return "WhiteBalance";
            default: return canonical;
            }
        case EntryNameContextKind::NikonSettingsMain:
            switch (entry.origin.name_context_variant) {
            case 1:
                return synthesize_nikonsettings_placeholder_name(
                    entry.key.data.exif_tag.tag);
            case 2: return "MovieFunc1Button";
            case 3: return "MovieFunc2Button";
            default: return canonical;
            }
        case EntryNameContextKind::NikonMainZ:
            switch (entry.key.data.exif_tag.tag) {
            case 0x002BU: return "ImageArea";
            case 0x002CU: return "AFImageHeight";
            case 0x002EU: return "AFAreaXPosition";
            case 0x002FU: return "FocusPositionHorizontal";
            case 0x0031U: return "FocusPositionVertical";
            case 0x0032U: return "AFAreaWidth";
            case 0x0035U: return "LensMountType";
            default: return canonical;
            }
        case EntryNameContextKind::NikonMainCompactType2: {
            const std::string_view compat = nikon_main_compact_type2_compat_name(
                entry.key.data.exif_tag.tag);
            return compat.empty() ? canonical : compat;
        }
        case EntryNameContextKind::NikonFlashInfoGroups:
            switch (entry.key.data.exif_tag.tag) {
            case 0x0011U:
                return (entry.origin.name_context_variant == 5U)
                           ? std::string_view("FlashGroupAControlMode")
                           : canonical;
            case 0x0012U:
                if (entry.origin.name_context_variant == 6U) {
                    return "FlashGroupBControlMode";
                }
                if (entry.origin.name_context_variant == 7U) {
                    return "FlashGroupCControlMode";
                }
                return canonical;
            case 0x0028U:
                return (entry.origin.name_context_variant == 1U)
                           ? std::string_view("FlashGroupACompensation")
                           : std::string_view("FlashGroupAOutput");
            case 0x0029U:
                return (entry.origin.name_context_variant == 1U)
                           ? std::string_view("FlashGroupBCompensation")
                           : std::string_view("FlashGroupBOutput");
            case 0x002AU:
                return (entry.origin.name_context_variant == 1U)
                           ? std::string_view("FlashGroupCCompensation")
                           : std::string_view("FlashGroupCOutput");
            default: return canonical;
            }
        case EntryNameContextKind::NikonFlashInfoLegacy:
            switch (entry.origin.name_context_variant) {
            case 1U:
                if (entry.key.data.exif_tag.tag == 0x0027U) {
                    return "FlashCompensation";
                }
                return canonical;
            case 2U: return "FlashGroupACompensation";
            case 3U: return "FlashGroupBCompensation";
            case 4U: return "FlashGroupCCompensation";
            case 5U: return "FlashGroupAControlMode";
            case 6U: return "FlashGroupBControlMode";
            case 7U: return "FlashGroupCControlMode";
            case 8U: return "FlashCompensation";
            default: return canonical;
            }
        case EntryNameContextKind::NikonShotInfoD800:
            switch (entry.origin.name_context_variant) {
            case 1: {
                static constexpr std::string_view kCompatIfds[] = {
                    "mk_nikon_shotinfod800_0",
                };
                const std::string_view compat = find_first_makernote_name(
                    std::span<const std::string_view>(kCompatIfds),
                    entry.key.data.exif_tag.tag);
                if (!compat.empty()) {
                    return compat;
                }
                return canonical;
            }
            default: return canonical;
            }
        case EntryNameContextKind::NikonShotInfoZ8:
            switch (entry.origin.name_context_variant) {
            case 1: {
                static constexpr std::string_view kCompatIfds[] = {
                    "mk_nikon_shotinfoz8_0",
                    "mk_nikon_menusettingsz8_0",
                    "mk_nikon_menusettingsz8v1_0",
                    "mk_nikon_menusettingsz8v2_0",
                };
                const std::string_view compat = find_first_makernote_name(
                    std::span<const std::string_view>(kCompatIfds),
                    entry.key.data.exif_tag.tag);
                if (!compat.empty()) {
                    return compat;
                }
                return canonical;
            }
            default: return canonical;
            }
        case EntryNameContextKind::NikonShotInfoD850:
            switch (entry.origin.name_context_variant) {
            case 1: {
                static constexpr std::string_view kCompatIfds[] = {
                    "mk_nikon_shotinfod850_0",
                    "mk_nikon_menusettingsd850_0",
                    "mk_nikon_moresettingsd850_0",
                };
                const std::string_view compat = find_first_makernote_name(
                    std::span<const std::string_view>(kCompatIfds),
                    entry.key.data.exif_tag.tag);
                if (!compat.empty()) {
                    return compat;
                }
                return canonical;
            }
            default: return canonical;
            }
        case EntryNameContextKind::PentaxMain0062:
            switch (entry.origin.name_context_variant) {
            case 1: return "Pentax_0x0062";
            default: return canonical;
            }
        case EntryNameContextKind::CanonMain0038:
            switch (entry.origin.name_context_variant) {
            case 1: return "Canon_0x0038";
            default: return canonical;
            }
        case EntryNameContextKind::CanonMain0000:
            switch (entry.origin.name_context_variant) {
            case 1: return "FocalType";
            case 2: return "AFInfoSize";
            default: return canonical;
            }
        case EntryNameContextKind::CanonShotInfo000E:
            switch (entry.origin.name_context_variant) {
            case 1: return "MinFocalLength";
            default: return canonical;
            }
        case EntryNameContextKind::CanonCameraSettings0021:
            switch (entry.origin.name_context_variant) {
            case 1: return "WB_RGGBLevelsKelvin";
            default: return canonical;
            }
        case EntryNameContextKind::CanonColorData4PSInfo:
            switch (entry.origin.name_context_variant) {
            case 1: return "UserDef2PictureStyle";
            default: return canonical;
            }
        case EntryNameContextKind::CanonColorData7PSInfo2:
            switch (entry.origin.name_context_variant) {
            case 1: return "ColorToneUserDef3";
            case 2: return "FilterEffectUserDef3";
            case 3: return "ToningEffectUserDef3";
            case 4: return "UserDef1PictureStyle";
            case 5: return "UserDef2PictureStyle";
            default: return canonical;
            }
        case EntryNameContextKind::CanonColorData400EA:
            switch (entry.origin.name_context_variant) {
            case 1: return "WB_RGGBLevelsUnknown7";
            default: return canonical;
            }
        case EntryNameContextKind::CanonColorData400EE:
            switch (entry.origin.name_context_variant) {
            case 1: return "MaxFocalLength";
            default: return canonical;
            }
        case EntryNameContextKind::CanonColorData402CF:
            switch (entry.origin.name_context_variant) {
            case 1: return "PerChannelBlackLevel";
            default: return canonical;
            }
        case EntryNameContextKind::CanonColorCalib0038:
            switch (entry.origin.name_context_variant) {
            case 1: return "BatteryType";
            default: return canonical;
            }
        case EntryNameContextKind::CanonCameraInfo1D0048:
            switch (entry.origin.name_context_variant) {
            case 1: return "Sharpness";
            default: return canonical;
            }
        case EntryNameContextKind::CanonCameraInfo600D00EA:
            switch (entry.origin.name_context_variant) {
            case 1: return "MinFocalLength";
            default: return canonical;
            }
        case EntryNameContextKind::CanonCustomFunctions20103:
            switch (entry.origin.name_context_variant) {
            case 1: return "ISOExpansion";
            case 2: return "ISOSpeedRange";
            default: return canonical;
            }
        case EntryNameContextKind::CanonCustomFunctions2010C:
            switch (entry.origin.name_context_variant) {
            case 1: return "CanonCustom_Functions2_0x010c";
            default: return canonical;
            }
        case EntryNameContextKind::CanonCustomFunctions20510:
            switch (entry.origin.name_context_variant) {
            case 1: return "SuperimposedDisplay";
            default: return canonical;
            }
        case EntryNameContextKind::CanonCustomFunctions20701:
            switch (entry.origin.name_context_variant) {
            case 1: return "ShutterButtonAFOnButton";
            case 2: return "AFAndMeteringButtons";
            default: return canonical;
            }
        }
        return canonical;
    }

}  // namespace

std::string_view
exif_tag_name(std::string_view ifd, uint16_t tag) noexcept
{
    if (ifd.starts_with("mk_minoltaraw_wbg_") && tag == 0x0004U) {
        // The generated ExifTool registry records the conditional A200 name.
        // Use the common RGGB name here; entry-aware naming applies GBRG
        // when the TIFF model identifies a DiMAGE A200.
        return "WB_RGGBLevels";
    }
    const ExifIfdGroup group = exif_ifd_group(ifd);
    if (group == ExifIfdGroup::Unknown) {
        if (ifd == "ciff_root" || ifd.starts_with("ciff_")) {
            return ciff_tag_name(ifd, tag);
        }
        if (ifd == "raf_header") {
            return raf_header_tag_name(tag);
        }
        if (is_raf_ifd(ifd)) {
            return raf_tag_name(tag);
        }
        if (ifd == "x3f_header") {
            return x3f_header_tag_name(tag);
        }
        if (ifd == "x3f_header_ext") {
            return x3f_header_ext_tag_name(tag);
        }
        if (ifd == "x3f_prop") {
            return x3f_prop_tag_name(tag);
        }
        if (ifd.starts_with("mk_")) {
            return makernote_tag_name(ifd, tag);
        }
        return {};
    }

    switch (group) {
    case ExifIfdGroup::TiffIfd: {
        std::string_view name = tiff_ifd_tag_name(tag);
        if (name.empty()) {
            name = exif_ifd_tag_name(tag);
        }
        return name;
    }
    case ExifIfdGroup::ExifIfd: {
        std::string_view name = exif_ifd_tag_name(tag);
        if (name.empty()) {
            name = tiff_ifd_tag_name(tag);
        }
        return name;
    }
    case ExifIfdGroup::GpsIfd: return gps_ifd_tag_name(tag);
    case ExifIfdGroup::InteropIfd: return interop_ifd_tag_name(tag);
    case ExifIfdGroup::MpfIfd: return mpf_ifd_tag_name(tag);
    case ExifIfdGroup::Unknown: return {};
    }
    return {};
}


std::string_view
exif_entry_name(const MetaStore& store, const Entry& entry,
                ExifTagNamePolicy policy) noexcept
{
    if (entry.key.kind != MetaKeyKind::ExifTag) {
        return {};
    }

    const std::string_view ifd = arena_string(store.arena(),
                                              entry.key.data.exif_tag.ifd);
    const std::string_view canonical
        = exif_tag_name(ifd, entry.key.data.exif_tag.tag);
    return contextual_exif_entry_name(store, entry, policy, canonical);
}

}  // namespace openmeta
