// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_value_names.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace openmeta {

const char*
tiff_compression_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Uncompressed";
    case 2U: return "CCITT 1D";
    case 3U: return "T4/Group 3 Fax";
    case 4U: return "T6/Group 4 Fax";
    case 5U: return "LZW";
    case 6U: return "JPEG (old-style)";
    case 7U: return "JPEG";
    case 8U: return "Adobe Deflate";
    case 9U: return "JBIG B&W or VC-5";
    case 10U: return "JBIG Color";
    case 32766U: return "NeXT or Sony ARW Compressed 2";
    case 32767U: return "Sony ARW Compressed";
    case 32769U: return "Packed RAW";
    case 32770U: return "Samsung SRW Compressed";
    case 32772U: return "Samsung SRW Compressed 2";
    case 32773U: return "PackBits";
    case 32867U: return "Kodak KDC Compressed";
    case 32946U: return "Deflate";
    case 34712U: return "JPEG 2000";
    case 34713U: return "Nikon NEF Compressed";
    case 34892U: return "Lossy JPEG";
    case 50000U: return "Zstd";
    case 50001U: return "WebP";
    case 50002U: return "JPEG XL (old)";
    case 52546U: return "JPEG XL";
    case 65000U: return "Kodak DCR Compressed";
    case 65535U: return "Pentax PEF Compressed";
    default: return "";
    }
}

const char*
tiff_photometric_interpretation_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "WhiteIsZero";
    case 1U: return "BlackIsZero";
    case 2U: return "RGB";
    case 3U: return "RGB Palette";
    case 4U: return "Transparency Mask";
    case 5U: return "CMYK";
    case 6U: return "YCbCr";
    case 8U: return "CIELab";
    case 9U: return "ICCLab";
    case 10U: return "ITULab";
    case 32803U: return "Color Filter Array";
    case 32844U: return "Pixar LogL";
    case 32845U: return "Pixar LogLuv";
    case 32892U: return "Sequential Color Filter";
    case 34892U: return "Linear Raw";
    case 51177U: return "Depth Map";
    case 52527U: return "Semantic Mask";
    default: return "";
    }
}

const char*
tiff_planar_configuration_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Chunky";
    case 2U: return "Planar";
    default: return "";
    }
}

const char*
tiff_resolution_unit_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "None";
    case 2U: return "inches";
    case 3U: return "cm";
    default: return "";
    }
}

const char*
tiff_ycbcr_positioning_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Centered";
    case 2U: return "Co-sited";
    default: return "";
    }
}

const char*
exif_exposure_program_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Not defined";
    case 1U: return "Manual";
    case 2U: return "Program AE";
    case 3U: return "Aperture-priority AE";
    case 4U: return "Shutter speed priority AE";
    case 5U: return "Creative (Slow speed)";
    case 6U: return "Action (High speed)";
    case 7U: return "Portrait";
    case 8U: return "Landscape";
    case 9U: return "Bulb";
    default: return "";
    }
}

const char*
exif_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Manual";
    case 2U: return "Auto bracket";
    default: return "";
    }
}

const char*
exif_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Unknown";
    case 1U: return "Average";
    case 2U: return "Center-weighted average";
    case 3U: return "Spot";
    case 4U: return "Multi-spot";
    case 5U: return "Multi-segment";
    case 6U: return "Partial";
    case 255U: return "Other";
    default: return "";
    }
}

const char*
exif_light_source_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Unknown";
    case 1U: return "Daylight";
    case 2U: return "Fluorescent";
    case 3U: return "Tungsten (incandescent)";
    case 4U: return "Flash";
    case 9U: return "Fine weather";
    case 10U: return "Cloudy";
    case 11U: return "Shade";
    case 12U: return "Daylight fluorescent";
    case 13U: return "Day white fluorescent";
    case 14U: return "Cool white fluorescent";
    case 15U: return "White fluorescent";
    case 16U: return "Warm white fluorescent";
    case 17U: return "Standard light A";
    case 18U: return "Standard light B";
    case 19U: return "Standard light C";
    case 20U: return "D55";
    case 21U: return "D65";
    case 22U: return "D75";
    case 23U: return "D50";
    case 24U: return "ISO studio tungsten";
    case 25U: return "Daylight";
    case 26U: return "Day white";
    case 27U: return "Cool white";
    case 28U: return "White";
    case 29U: return "Warm white";
    case 30U: return "Daylight LED";
    case 31U: return "Day white LED";
    case 32U: return "Cool white LED";
    case 33U: return "White LED";
    case 34U: return "Warm white LED";
    case 255U: return "Other";
    default: return "";
    }
}

const char*
exif_flash_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "No flash";
    case 1U: return "Fired";
    case 5U: return "Fired, return not detected";
    case 7U: return "Fired, return detected";
    case 8U: return "On, did not fire";
    case 9U: return "On, fired";
    case 13U: return "On, return not detected";
    case 15U: return "On, return detected";
    case 16U: return "Off, did not fire";
    case 20U: return "Off, did not fire, return not detected";
    case 24U: return "Auto, did not fire";
    case 25U: return "Auto, fired";
    case 29U: return "Auto, fired, return not detected";
    case 31U: return "Auto, fired, return detected";
    case 32U: return "No flash function";
    case 48U: return "Off, no flash function";
    case 65U: return "Fired, red-eye reduction";
    case 69U: return "Fired, red-eye reduction, return not detected";
    case 71U: return "Fired, red-eye reduction, return detected";
    case 73U: return "On, red-eye reduction";
    case 77U: return "On, red-eye reduction, return not detected";
    case 79U: return "On, red-eye reduction, return detected";
    case 80U: return "Off, red-eye reduction";
    case 88U: return "Auto, did not fire, red-eye reduction";
    case 89U: return "Auto, fired, red-eye reduction";
    case 93U: return "Auto, fired, red-eye reduction, return not detected";
    case 95U: return "Auto, fired, red-eye reduction, return detected";
    default: return "";
    }
}

const char*
exif_color_space_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "sRGB";
    case 2U: return "Adobe RGB";
    case 0xFFFFU: return "Uncalibrated";
    default: return "";
    }
}

const char*
exif_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Manual";
    default: return "";
    }
}

const char*
exif_scene_capture_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Landscape";
    case 2U: return "Portrait";
    case 3U: return "Night";
    default: return "";
    }
}

const char*
exif_gain_control_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "None";
    case 1U: return "Low gain up";
    case 2U: return "High gain up";
    case 3U: return "Low gain down";
    case 4U: return "High gain down";
    default: return "";
    }
}

const char*
exif_sensitivity_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Unknown";
    case 1U: return "Standard output sensitivity";
    case 2U: return "Recommended exposure index";
    case 3U: return "ISO speed";
    case 4U:
        return "Standard output sensitivity and recommended exposure index";
    case 5U: return "Standard output sensitivity and ISO speed";
    case 6U: return "Recommended exposure index and ISO speed";
    case 7U:
        return "Standard output sensitivity, recommended exposure index, and "
               "ISO speed";
    default: return "";
    }
}

const char*
exif_focal_plane_resolution_unit_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "None";
    case 2U: return "inches";
    case 3U: return "cm";
    case 4U: return "mm";
    case 5U: return "micrometers";
    default: return "";
    }
}

const char*
exif_sensing_method_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Not defined";
    case 2U: return "One-chip color area";
    case 3U: return "Two-chip color area";
    case 4U: return "Three-chip color area";
    case 5U: return "Color sequential area";
    case 7U: return "Trilinear";
    case 8U: return "Color sequential linear";
    default: return "";
    }
}

const char*
exif_file_source_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Film scanner";
    case 2U: return "Reflection print scanner";
    case 3U: return "Digital camera";
    default: return "";
    }
}

const char*
exif_scene_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Directly photographed";
    default: return "";
    }
}

const char*
exif_custom_rendered_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Custom";
    default: return "";
    }
}

const char*
exif_contrast_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Low";
    case 2U: return "High";
    default: return "";
    }
}

const char*
exif_saturation_name(uint64_t value) noexcept
{
    return exif_contrast_name(value);
}

const char*
exif_sharpness_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Soft";
    case 2U: return "Hard";
    default: return "";
    }
}

const char*
exif_subject_distance_range_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Unknown";
    case 1U: return "Macro";
    case 2U: return "Close";
    case 3U: return "Distant";
    default: return "";
    }
}

static const char*
exif_lens_correction_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Not applied";
    case 1U: return "Applied";
    default: return "";
    }
}

static const char*
exif_noise_reduction_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Not applied";
    case 1U: return "Low strength";
    case 2U: return "Normal strength";
    case 3U: return "High strength";
    default: return "";
    }
}

const char*
dng_cfa_layout_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Rectangular";
    case 2U: return "Even columns offset down 1/2 row";
    case 3U: return "Even columns offset up 1/2 row";
    case 4U: return "Even rows offset right 1/2 column";
    case 5U: return "Even rows offset left 1/2 column";
    default: return "";
    }
}

const char*
dng_calibration_illuminant_name(uint64_t value) noexcept
{
    return exif_light_source_name(value);
}

static bool
ifd_has_prefix(std::string_view ifd, std::string_view prefix) noexcept
{
    return ifd.size() >= prefix.size()
           && ifd.substr(0U, prefix.size()) == prefix;
}

static bool
ifd_matches_context(std::string_view ifd, std::string_view decoded_prefix,
                    std::string_view registry_ifd) noexcept
{
    return ifd_has_prefix(ifd, decoded_prefix) || ifd == registry_ifd;
}

static bool
ifd_contains(std::string_view ifd, std::string_view token) noexcept
{
    return ifd.find(token) != std::string_view::npos;
}

static bool
is_makernote_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_") || ifd_has_prefix(ifd, "makernote:");
}

static const char*
off_on_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    default: return "";
    }
}

static const char*
no_yes_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "No";
    case 1U: return "Yes";
    default: return "";
    }
}

static const char*
disable_enable_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Disable";
    case 1U: return "Enable";
    default: return "";
    }
}

static bool
is_canon_camera_settings_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_camerasettings")
           || ifd == "makernote:canon:camerasettings";
}

static bool
is_canon_camera_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_camerainfo")
           || ifd_has_prefix(ifd, "makernote:canon:camerainfo");
}

static bool
is_canon_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_canon0" || ifd_has_prefix(ifd, "mk_canon_main")
           || ifd == "makernote:canon:main";
}

static bool
is_canon_shot_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_shotinfo_")
           || ifd == "makernote:canon:shotinfo";
}

static bool
is_canon_mycolors_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_mycolors_")
           || ifd == "makernote:canon:mycolors";
}

static bool
is_canon_focal_length_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_focallength_")
           || ifd == "makernote:canon:focallength";
}

static bool
is_canon_af_info2_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_afinfo2_")
           || ifd == "makernote:canon:afinfo2";
}

static bool
is_canon_aspect_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_aspectinfo_")
           || ifd == "makernote:canon:aspectinfo";
}

static bool
is_canon_file_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_fileinfo_")
           || ifd == "makernote:canon:fileinfo";
}

static bool
is_canon_processing_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_processing_")
           || ifd == "makernote:canon:processing";
}

static bool
is_canon_lighting_opt_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_lightingopt_")
           || ifd == "makernote:canon:lightingopt";
}

static bool
is_canon_vignetting_corr_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_vignettingcorr_")
           || ifd == "makernote:canon:vignettingcorr";
}

static bool
is_canon_vignetting_corr2_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_vignettingcorr2_")
           || ifd == "makernote:canon:vignettingcorr2";
}

static bool
is_canon_time_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_timeinfo_")
           || ifd == "makernote:canon:timeinfo";
}

static bool
is_canon_filter_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_filterinfo_")
           || ifd == "makernote:canon:filterinfo";
}

static bool
is_canon_hdr_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_hdrinfo_")
           || ifd == "makernote:canon:hdrinfo";
}

static bool
is_canon_custom_functions2_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canoncustom_functions2_")
           || ifd == "makernote:canoncustom:functions2";
}

static const char*
canon_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Auto";
    case 2U: return "On";
    case 3U: return "Red-eye reduction";
    case 4U: return "Slow-sync";
    case 5U: return "Red-eye reduction (Auto)";
    case 6U: return "Red-eye reduction (On)";
    case 16U: return "External flash";
    default: return "";
    }
}

static const char*
canon_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "One-shot AF";
    case 1U: return "AI Servo AF";
    case 2U: return "AI Focus AF";
    case 3U: return "Manual Focus (3)";
    case 4U: return "Single";
    case 5U: return "Continuous";
    case 6U: return "Manual Focus (6)";
    case 16U: return "Pan Focus";
    case 256U: return "One-shot AF (Live View)";
    case 257U: return "AI Servo AF (Live View)";
    case 258U: return "AI Focus AF (Live View)";
    case 512U: return "Movie Snap Focus";
    case 519U: return "Movie Servo AF";
    default: return "";
    }
}

static const char*
canon_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Default";
    case 1U: return "Spot";
    case 2U: return "Average";
    case 3U: return "Evaluative";
    case 4U: return "Partial";
    case 5U: return "Center-weighted average";
    default: return "";
    }
}

static const char*
canon_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Easy";
    case 1U: return "Program AE";
    case 2U: return "Shutter speed priority AE";
    case 3U: return "Aperture-priority AE";
    case 4U: return "Manual";
    case 5U: return "Depth-of-field AE";
    case 6U: return "M-Dep";
    case 7U: return "Bulb";
    case 8U: return "Flexible-priority AE";
    default: return "";
    }
}

static const char*
canon_spot_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Center";
    case 1U: return "AF Point";
    default: return "";
    }
}

static const char*
canon_quality_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Economy";
    case 2U: return "Normal";
    case 3U: return "Fine";
    case 4U: return "RAW";
    case 5U: return "Superfine";
    case 7U: return "CRAW";
    case 130U: return "Light (RAW)";
    case 131U: return "Standard (RAW)";
    default: return "";
    }
}

static const char*
canon_image_size_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Large";
    case 1U: return "Medium";
    case 2U: return "Small";
    case 5U: return "Medium 1";
    case 6U: return "Medium 2";
    case 7U: return "Medium 3";
    case 8U: return "Postcard";
    case 9U: return "Widescreen";
    case 10U: return "Medium Widescreen";
    case 14U: return "Small 1";
    case 15U: return "Small 2";
    case 16U: return "Small 3";
    case 128U: return "640x480 Movie";
    case 129U: return "Medium Movie";
    case 130U: return "Small Movie";
    case 137U: return "1280x720 Movie";
    case 142U: return "1920x1080 Movie";
    case 143U: return "4096x2160 Movie";
    default: return "";
    }
}

static const char*
canon_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Cloudy";
    case 3U: return "Tungsten";
    case 4U: return "Fluorescent";
    case 5U: return "Flash";
    case 6U: return "Custom";
    case 7U: return "Black & White";
    case 8U: return "Shade";
    case 9U: return "Manual Temperature (Kelvin)";
    case 10U: return "PC Set1";
    case 11U: return "PC Set2";
    case 12U: return "PC Set3";
    case 14U: return "Daylight Fluorescent";
    case 15U: return "Custom 1";
    case 16U: return "Custom 2";
    case 17U: return "Underwater";
    case 18U: return "Custom 3";
    case 19U: return "Custom 4";
    case 20U: return "PC Set4";
    case 21U: return "PC Set5";
    case 23U: return "Auto (ambience priority)";
    default: return "";
    }
}

static const char*
canon_macro_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Macro";
    case 2U: return "Normal";
    default: return "";
    }
}

static const char*
canon_self_timer_name(uint64_t value) noexcept
{
    return value == 0U ? "Off" : "";
}

static const char*
canon_continuous_drive_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single";
    case 1U: return "Continuous";
    case 2U: return "Movie";
    case 3U: return "Continuous, Speed Priority";
    case 4U: return "Continuous, Low";
    case 5U: return "Continuous, High";
    case 6U: return "Silent Single";
    case 8U: return "Continuous, High+";
    case 9U: return "Single, Silent";
    case 10U: return "Continuous, Silent";
    default: return "";
    }
}

static const char*
canon_record_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "JPEG";
    case 2U: return "CRW+THM";
    case 3U: return "AVI+THM";
    case 4U: return "TIF";
    case 5U: return "TIF+JPEG";
    case 6U: return "CR2";
    case 7U: return "CR2+JPEG";
    case 9U: return "MOV";
    case 10U: return "MP4";
    case 11U: return "CRM";
    case 12U: return "CR3";
    case 13U: return "CR3+JPEG";
    case 14U: return "HIF";
    case 15U: return "CR3+HIF";
    default: return "";
    }
}

static const char*
canon_easy_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Full auto";
    case 1U: return "Manual";
    case 2U: return "Landscape";
    case 3U: return "Fast shutter";
    case 4U: return "Slow shutter";
    case 5U: return "Night";
    case 8U: return "Portrait";
    case 9U: return "Sports";
    case 10U: return "Macro";
    case 15U: return "Flash Off";
    case 51U: return "High Dynamic Range";
    case 59U: return "Scene Intelligent Auto";
    default: return "";
    }
}

static const char*
canon_digital_zoom_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "None";
    case 1U: return "2x";
    case 2U: return "4x";
    case 3U: return "Other";
    default: return "";
    }
}

static const char*
canon_parameter_name(uint64_t value) noexcept
{
    return value == 0U ? "Normal" : "";
}

static const char*
canon_focus_range_name(uint64_t value) noexcept
{
    switch (value & 0x7FU) {
    case 0U: return "Manual";
    case 1U: return "Auto";
    case 2U: return "Not Known";
    case 3U: return "Macro";
    case 4U: return "Very Close";
    case 5U: return "Close";
    case 6U: return "Middle Range";
    case 7U: return "Far Range";
    case 8U: return "Pan Focus";
    case 9U: return "Super Macro";
    case 10U: return "Infinity";
    default: return "";
    }
}

static const char*
canon_flash_bits_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "(none)";
    case 8U: return "E-TTL";
    case 8200U: return "E-TTL, Built-in";
    default: return "";
    }
}

static const char*
canon_focus_continuous_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single";
    case 1U: return "Continuous";
    case 8U: return "Manual";
    default: return "";
    }
}

static const char*
canon_ae_setting_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal AE";
    case 1U: return "Exposure Compensation";
    case 2U: return "AE Lock";
    case 3U: return "AE Lock + Exposure Comp.";
    case 4U: return "No AE";
    default: return "";
    }
}

static const char*
canon_photo_effect_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Vivid";
    case 2U: return "Neutral";
    case 3U: return "Smooth";
    case 4U: return "Sepia";
    case 5U: return "B&W";
    case 6U: return "Custom";
    case 100U: return "My Color Data";
    default: return "";
    }
}

static const char*
canon_af_point_name(uint64_t value) noexcept
{
    switch (value) {
    case 12289U:
    case 16385U: return "Auto AF point selection";
    case 8197U: return "Manual AF point selection";
    case 16390U: return "Face Detect";
    default: return "";
    }
}

static const char*
canon_lens_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 10U: return "Canon EF 50mm f/2.5 Macro or Sigma Lens";
    case 48U: return "Canon EF-S 18-55mm f/3.5-5.6 IS";
    case 61182U: return "Canon RF 50mm F1.2L USM or other Canon RF Lens";
    case 65535U: return "n/a";
    default: return "";
    }
}

static const char*
canon_rf_lens_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 329U: return "Canon RF 20-50mm F4 L IS USM PZ";
    case 330U: return "Canon RF 45mm F1.2 STM";
    case 331U: return "Canon RF 7-14mm F2.8-3.5 L FISHEYE STM";
    case 332U: return "Canon RF 14mm F1.4 L VCM";
    default: return "";
    }
}

static const char*
canon_focal_units_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "1/mm";
    case 32U: return "32/mm";
    case 100U: return "100/mm";
    case 1000U: return "1000/mm";
    default: return "";
    }
}

static const char*
canon_image_stabilization_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    case 256U: return "Off (2)";
    case 257U: return "On (2)";
    default: return "";
    }
}

static const char*
canon_sraw_quality_name(uint64_t value) noexcept
{
    return value == 0U ? "n/a" : "";
}

static const char*
canon_manual_flash_output_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 0x500U: return "Full";
    case 0x502U: return "Medium";
    case 0x504U: return "Low";
    case 0x7FFFU: return "n/a";
    default: return "";
    }
}

static const char*
canon_slow_shutter_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Night Scene";
    case 2U: return "On";
    case 3U: return "None";
    default: return "";
    }
}

static const char*
canon_auto_exposure_bracketing_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On (shot 1)";
    case 2U: return "On (shot 2)";
    case 3U: return "On (shot 3)";
    default: return "";
    }
}

static const char*
canon_control_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 1U: return "Camera Local Control";
    case 3U: return "Computer Remote Control";
    default: return "";
    }
}

static const char*
canon_camera_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 248U: return "EOS High-end";
    case 250U: return "Compact";
    case 252U: return "EOS Mid-range";
    case 255U: return "DV Camera";
    default: return "";
    }
}

static const char*
canon_auto_rotate_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "None";
    case 1U: return "Rotate 90 CW";
    case 2U: return "Rotate 180";
    case 3U: return "Rotate 270 CW";
    default: return "";
    }
}

static const char*
canon_nd_filter_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    default: return "";
    }
}

static const char*
canon_focal_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Fixed";
    case 2U: return "Zoom";
    default: return "";
    }
}

static const char*
canon_af_area_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off (Manual Focus)";
    case 2U: return "Single-point AF";
    case 4U: return "Auto";
    case 5U: return "Face Detect AF";
    case 6U: return "Face + Tracking";
    case 13U: return "Flexizone Single";
    default: return "";
    }
}

static const char*
canon_af_points_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "0";
    case 16U: return "4";
    case 511U: return "0,1,2,3,4,5,6,7,8";
    case 12295U: return "All";
    default: return "";
    }
}

static const char*
canon_aspect_ratio_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "3:2";
    case 1U: return "1:1";
    case 2U: return "4:3";
    case 7U: return "16:9";
    case 8U: return "4:5";
    default: return "";
    }
}

static const char*
canon_bracket_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "AEB";
    case 2U: return "FEB";
    case 3U: return "ISO";
    case 4U: return "WB";
    default: return "";
    }
}

static const char*
canon_long_exposure_noise_reduction2_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On (1D)";
    case 3U: return "On";
    case 4U: return "Auto";
    default: return "";
    }
}

static const char*
canon_wb_bracket_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On (shift AB)";
    case 2U: return "On (shift GM)";
    default: return "";
    }
}

static const char*
canon_filter_effect_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "None";
    case 1U: return "Yellow";
    case 2U: return "Orange";
    case 3U: return "Red";
    case 4U: return "Green";
    default: return "";
    }
}

static const char*
canon_toning_effect_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "None";
    case 1U: return "Sepia";
    case 2U: return "Blue";
    case 3U: return "Purple";
    case 4U: return "Green";
    default: return "";
    }
}

static const char*
canon_live_view_shooting_name(uint64_t value) noexcept
{
    return off_on_name(value);
}

static const char*
canon_shutter_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Mechanical";
    case 1U: return "Electronic First Curtain";
    default: return "";
    }
}

static const char*
canon_focus_distance_lower_name(uint64_t value) noexcept
{
    return value == 0U ? "0 m" : "";
}

static const char*
canon_optical_zoom_code_name(uint64_t value) noexcept
{
    return value == 8U ? "n/a" : "";
}

static const char*
canon_color_space_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "sRGB";
    case 2U: return "Adobe RGB";
    default: return "";
    }
}

static const char*
canon_super_macro_name(uint64_t value) noexcept
{
    return value == 0U ? "Off" : "";
}

static const char*
canon_serial_number_format_name(uint64_t value) noexcept
{
    return value == 2684354560ULL ? "Format 2" : "";
}

static const char*
canon_date_stamp_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Date";
    case 2U: return "Date & Time";
    default: return "";
    }
}

static const char*
canon_my_color_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Positive Film";
    case 2U: return "Light Skin Tone";
    case 3U: return "Dark Skin Tone";
    case 4U: return "Vivid Blue";
    case 5U: return "Vivid Green";
    case 6U: return "Vivid Red";
    case 7U: return "Color Accent";
    case 8U: return "Color Swap";
    case 9U: return "Custom";
    case 12U: return "Vivid";
    case 13U: return "Neutral";
    case 14U: return "Sepia";
    case 15U: return "B&W";
    default: return "";
    }
}

static const char*
canon_picture_style_name(uint64_t value) noexcept
{
    switch (value) {
    case 129U: return "Standard";
    case 131U: return "Landscape";
    case 135U: return "Auto";
    default: return "";
    }
}

static const char*
canon_digital_lens_optimizer_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 2U: return "High";
    default: return "";
    }
}

static const char*
canon_auto_lighting_optimizer_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Low";
    case 2U: return "Strong";
    case 3U: return "Off";
    default: return "";
    }
}

static const char*
canon_noise_reduction_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Low";
    case 2U: return "Strong";
    case 3U: return "Off";
    default: return "";
    }
}

static const char*
canon_long_exposure_noise_reduction_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Auto";
    case 2U: return "On";
    default: return "";
    }
}

static const char*
canon_highlight_tone_priority_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    case 2U: return "Enhanced";
    default: return "";
    }
}

static const char*
canon_time_zone_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "+00:00";
    case 60U: return "+01:00";
    case 540U: return "+09:00";
    default: return "";
    }
}

static const char*
canon_time_zone_city_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 6U: return "Tokyo";
    case 20U: return "London";
    case 27U: return "New York";
    case 30U: return "Los Angeles";
    case 32766U: return "(not set)";
    default: return "";
    }
}

static const char*
canon_daylight_savings_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 60U: return "On";
    default: return "";
    }
}

static const char*
canon_custom_functions2_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0103U: return off_on_name(value);
    case 0x0104U: return value == 0U ? "On" : "";
    case 0x0108U: return disable_enable_name(value);
    case 0x010FU: return value == 0U ? "Auto" : "";
    case 0x0112U: return disable_enable_name(value);
    case 0x0113U: return value == 0U ? "Enable" : "";
    case 0x0114U: return value == 1U ? "Evaluative" : "";
    case 0x0203U: return value == 0U ? "Disable" : "";
    case 0x0502U: return value == 0U ? "Standard" : "";
    case 0x0505U: return value == 0U ? "Focus search on" : "";
    case 0x050EU: return value == 0U ? "Emits" : "";
    case 0x0516U: return value == 0U ? "Same for vertical and horizontal" : "";
    case 0x051BU: return value == 0U ? "AF area selection button" : "";
    case 0x051CU: return value == 0U ? "On-Shot AF only" : "";
    case 0x051DU: return value == 0U ? "Auto" : "";
    case 0x060FU: return disable_enable_name(value);
    case 0x0702U:
    case 0x0707U:
    case 0x0711U: return disable_enable_name(value);
    case 0x0706U: return value == 0U ? "Normal" : "";
    case 0x070EU: return value == 0U ? "Raise built-in flash" : "";
    case 0x0815U: return value == 0U ? "Disable" : "";
    case 0x080FU: return value == 0U ? "Off" : "";
    case 0x0811U: return value == 0U ? "Display" : "";
    case 0x0813U: return value == 0U ? "Cancel selected" : "";
    case 0x0816U: return value == 0U ? "Enable" : "";
    default: return "";
    }
}

static const char*
canon_flash_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "E-TTL";
    case 3U: return "TTL";
    case 4U: return "External Auto";
    case 5U: return "External Manual";
    case 6U: return "Off";
    default: return "";
    }
}

static const char*
canon_camera_settings_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return canon_macro_mode_name(value);
    case 0x0002U: return canon_self_timer_name(value);
    case 0x0003U: return canon_quality_name(value);
    case 0x0004U: return canon_flash_mode_name(value);
    case 0x0005U: return canon_continuous_drive_name(value);
    case 0x0007U: return canon_focus_mode_name(value);
    case 0x0009U: return canon_record_mode_name(value);
    case 0x000AU: return canon_image_size_name(value);
    case 0x000BU: return canon_easy_mode_name(value);
    case 0x000CU: return canon_digital_zoom_name(value);
    case 0x000DU: return canon_parameter_name(value);
    case 0x000EU: return canon_parameter_name(value);
    case 0x0011U: return canon_metering_mode_name(value);
    case 0x0012U: return canon_focus_range_name(value);
    case 0x0013U: return canon_af_point_name(value);
    case 0x0014U: return canon_exposure_mode_name(value);
    case 0x0016U: return canon_lens_type_name(value);
    case 0x0019U: return canon_focal_units_name(value);
    case 0x001DU: return canon_flash_bits_name(value);
    case 0x0020U: return canon_focus_continuous_name(value);
    case 0x0021U: return canon_ae_setting_name(value);
    case 0x0022U: return canon_image_stabilization_name(value);
    case 0x0027U: return canon_spot_metering_mode_name(value);
    case 0x0028U: return canon_photo_effect_name(value);
    case 0x0029U: return canon_manual_flash_output_name(value);
    case 0x002AU: return canon_parameter_name(value);
    case 0x002EU: return canon_sraw_quality_name(value);
    default: return "";
    }
}

static const char*
canon_camera_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0015U: return canon_flash_metering_mode_name(value);
    default: return "";
    }
}

static const char*
canon_shot_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0007U: return canon_white_balance_name(value);
    case 0x0008U: return canon_slow_shutter_name(value);
    case 0x000AU: return canon_optical_zoom_code_name(value);
    case 0x000EU: return canon_af_points_name(value);
    case 0x0010U: return canon_auto_exposure_bracketing_name(value);
    case 0x0012U: return canon_control_mode_name(value);
    case 0x0014U: return canon_focus_distance_lower_name(value);
    case 0x001AU: return canon_camera_type_name(value);
    case 0x001BU: return canon_auto_rotate_name(value);
    case 0x001CU: return canon_nd_filter_name(value);
    default: return "";
    }
}

static const char*
canon_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0015U: return canon_serial_number_format_name(value);
    case 0x001AU: return canon_super_macro_name(value);
    case 0x001CU: return canon_date_stamp_mode_name(value);
    case 0x00B4U: return canon_color_space_name(value);
    default: return "";
    }
}

static const char*
canon_mycolors_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0002U: return canon_my_color_mode_name(value);
    default: return "";
    }
}

static const char*
canon_focal_length_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0000U: return canon_focal_type_name(value);
    default: return "";
    }
}

static const char*
canon_af_info2_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return canon_af_area_mode_name(value);
    case 0x000CU: return canon_af_points_name(value);
    case 0x000DU: return canon_af_points_name(value);
    default: return "";
    }
}

static const char*
canon_aspect_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0000U: return canon_aspect_ratio_name(value);
    default: return "";
    }
}

static const char*
canon_file_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0003U: return canon_bracket_mode_name(value);
    case 0x0007U: return canon_image_size_name(value);
    case 0x0008U: return canon_long_exposure_noise_reduction2_name(value);
    case 0x0009U: return canon_wb_bracket_mode_name(value);
    case 0x000EU: return canon_filter_effect_name(value);
    case 0x000FU: return canon_toning_effect_name(value);
    case 0x0013U: return canon_live_view_shooting_name(value);
    case 0x0017U: return canon_shutter_mode_name(value);
    case 0x0019U: return off_on_name(value);
    case 0x003DU: return canon_rf_lens_type_name(value);
    default: return "";
    }
}

static const char*
canon_processing_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return value == 0U ? "Standard" : "";
    case 0x0003U: return value == 0U ? "n/a" : "";
    case 0x000AU: return canon_picture_style_name(value);
    default: return "";
    }
}

static const char*
canon_lighting_opt_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return off_on_name(value);
    case 0x0002U: return canon_auto_lighting_optimizer_name(value);
    case 0x0003U: return canon_highlight_tone_priority_name(value);
    case 0x0004U: return canon_long_exposure_noise_reduction_name(value);
    case 0x0005U: return canon_noise_reduction_name(value);
    case 0x000AU: return canon_digital_lens_optimizer_name(value);
    case 0x000BU: return off_on_name(value);
    default: return "";
    }
}

static const char*
canon_vignetting_corr_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0002U:
    case 0x0003U:
    case 0x0004U:
    case 0x0005U: return off_on_name(value);
    default: return "";
    }
}

static const char*
canon_vignetting_corr2_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0005U:
    case 0x0006U:
    case 0x0007U:
    case 0x0009U: return off_on_name(value);
    default: return "";
    }
}

static const char*
canon_time_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return canon_time_zone_name(value);
    case 0x0002U: return canon_time_zone_city_name(value);
    case 0x0003U: return canon_daylight_savings_name(value);
    default: return "";
    }
}

static const char*
canon_filter_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0402U:
        switch (value) {
        case 0U: return "Horizontal";
        case 1U: return "Vertical";
        default: return "";
        }
    default: return "";
    }
}

static const char*
canon_hdr_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return off_on_name(value);
    case 0x0002U:
        switch (value) {
        case 0U: return "Natural";
        default: return "";
        }
    default: return "";
    }
}

static bool
is_nikon_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_nikon0" || ifd_has_prefix(ifd, "mk_nikon_main")
           || ifd == "makernote:nikon:main";
}

static bool
is_nikon_af_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_afinfo_")
           || ifd == "makernote:nikon:afinfo";
}

static bool
is_nikon_af_info2v0100_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_afinfo2v0100_")
           || ifd == "makernote:nikon:afinfo2v0100";
}

static bool
is_nikon_iso_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_isoinfo_")
           || ifd == "makernote:nikon:isoinfo";
}

static bool
is_nikon_hdr_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_hdrinfo_")
           || ifd == "makernote:nikon:hdrinfo";
}

static bool
is_nikon_vr_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_vrinfo_")
           || ifd == "makernote:nikon:vrinfo";
}

static bool
is_nikon_flash_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_flashinfo")
           || ifd_has_prefix(ifd, "makernote:nikon:flashinfo");
}

static bool
is_nikon_picture_control_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_picturecontrol")
           || ifd_has_prefix(ifd, "makernote:nikon:picturecontrol");
}

static bool
is_nikon_multi_exposure_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_multiexposure_")
           || ifd == "makernote:nikon:multiexposure";
}

static bool
is_nikon_world_time_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_worldtime_")
           || ifd == "makernote:nikon:worldtime";
}

static bool
is_nikon_distort_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_distortinfo_")
           || ifd == "makernote:nikon:distortinfo";
}

static bool
is_nikon_af_tune_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_aftune_")
           || ifd == "makernote:nikon:aftune";
}

static bool
is_nikon_lens_data0800_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_lensdata0800_")
           || ifd == "makernote:nikon:lensdata0800";
}

static bool
is_nikon_lens_data_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_lensdata")
           || ifd_has_prefix(ifd, "makernote:nikon:lensdata");
}

static bool
is_nikon_shot_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_shotinfo")
           || ifd_has_prefix(ifd, "makernote:nikon:shotinfo");
}

static bool
is_nikon_makernotes_firmware_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_makernotes0x")
           || ifd_has_prefix(ifd, "makernote:nikon:makernotes0x");
}

static bool
is_nikon_location_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikon_locationinfo_")
           || ifd == "makernote:nikon:locationinfo";
}

static bool
is_nikon_settings_main_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_nikonsettings_main_")
           || ifd == "makernote:nikonsettings:main";
}

static const char*
nikon_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Did Not Fire";
    case 1U: return "Fired, Manual";
    case 3U: return "Not Ready";
    case 7U: return "Fired, External";
    case 8U: return "Fired, Commander Mode";
    case 9U: return "Fired, TTL Mode";
    case 18U: return "LED Light";
    default: return "";
    }
}

static const char*
nikon_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Matrix";
    case 1U: return "Center";
    case 2U: return "Spot";
    case 3U: return "Highlight";
    default: return "";
    }
}

static const char*
nikon_movie_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Manual";
    case 1U: return "AF-S";
    case 2U: return "AF-C";
    case 4U: return "AF-F";
    default: return "";
    }
}

static const char*
nikon_menu_multiple_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    case 2U: return "On (Series)";
    default: return "";
    }
}

static const char*
nikon_date_stamp_mode_name(uint64_t value) noexcept
{
    return value == 0U ? "Off" : "";
}

static const char*
nikon_active_d_lighting_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Low";
    case 3U: return "Normal";
    case 5U: return "High";
    case 65535U: return "Auto";
    default: return "";
    }
}

static const char*
nikon_high_iso_noise_reduction_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 4U: return "Normal";
    default: return "";
    }
}

static const char*
nikon_lens_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "AF";
    case 6U: return "G";
    case 14U: return "G VR";
    default: return "";
    }
}

static const char*
nikon_shooting_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single-Frame";
    case 1U: return "Continuous";
    case 2U: return "Delay";
    case 32U: return "Single-Frame, Auto ISO";
    default: return "";
    }
}

static const char*
nikon_vignette_control_name(uint64_t value) noexcept
{
    return value == 3U ? "Normal" : "";
}

static const char*
nikon_shutter_mode_name(uint64_t value) noexcept
{
    return value == 16U ? "Electronic" : "";
}

static const char*
nikon_image_size_raw_name(uint64_t value) noexcept
{
    return value == 1U ? "Large" : "";
}

static const char*
nikon_jpg_compression_name(uint64_t value) noexcept
{
    return value == 3U ? "Optimal Quality" : "";
}

static const char*
nikon_af_area_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single Area";
    case 1U: return "Dynamic Area";
    default: return "";
    }
}

static const char*
nikon_af_point_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Center";
    case 1U: return "Top";
    case 3U: return "Mid-left";
    case 4U: return "Mid-right";
    default: return "";
    }
}

static const char*
nikon_af_points_in_focus_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "(none)";
    case 1U: return "Center";
    default: return "";
    }
}

static const char*
nikon_hdr_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 48U: return "Auto";
    default: return "";
    }
}

static const char*
nikon_hdr_level_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 255U: return "n/a";
    default: return "";
    }
}

static const char*
nikon_vibration_reduction_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "On";
    case 2U: return "Off";
    default: return "";
    }
}

static const char*
nikon_flash_source_name(uint64_t value) noexcept
{
    return value == 0U ? "None" : "";
}

static const char*
nikon_external_flash_flags_name(uint64_t value) noexcept
{
    return value == 0U ? "(none)" : "";
}

static const char*
nikon_picture_control_filter_name(uint64_t value) noexcept
{
    return value == 255U ? "n/a" : "";
}

static const char*
nikon_picture_control_adjust_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Default Settings";
    case 1U: return "Quick Adjust";
    default: return "";
    }
}

static const char*
nikon_date_display_format_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Y/M/D";
    case 2U: return "D/M/Y";
    default: return "";
    }
}

static const char*
nikon_lens_mount_type_name(uint64_t value) noexcept
{
    return value == 1U ? "Z-mount Lens" : "";
}

static const char*
nikon_z_lens_id_name(uint64_t value) noexcept
{
    switch (value) {
    case 50U: return "Nikkor Z 24-70mm f/2.8 S II";
    case 54U: return "Nikkor Z 70-200mm f/2.8 VR S II";
    case 57U: return "Nikkor Z 24-105mm f/4-7.1";
    default: return "";
    }
}

static const char*
nikon_settings_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0003U: return value == 3U ? "Auto" : "";
    case 0x000BU: return value == 2U ? "Disable" : "";
    case 0x000CU: return off_on_name(value);
    case 0x000EU: return off_on_name(value);
    case 0x000FU: return value == 1U ? "Yes" : "";
    case 0x001EU: return value == 2U ? "Focus" : "";
    case 0x0022U: return value == 1U ? "Shutter/AF-On" : "";
    case 0x0023U: return value == 2U ? "No Wrap" : "";
    case 0x0025U: return value == 1U ? "On" : "";
    case 0x0026U: return off_on_name(value);
    case 0x0027U: return value == 1U ? "On" : "";
    case 0x002AU: return value == 1U ? "1/3 EV" : "";
    case 0x002BU: return value == 3U ? "Off" : "";
    case 0x002CU: return off_on_name(value);
    case 0x0033U: return value == 3U ? "Off" : "";
    case 0x0035U: return value == 3U ? "10 s" : "";
    case 0x0037U: return value == 1U ? "0.5 s" : "";
    case 0x0038U: return value == 2U ? "10 s" : "";
    case 0x0039U: return value == 4U ? "1 min" : "";
    case 0x003BU: return value == 2U ? "4 s" : "";
    case 0x0040U: return value == 6U ? "Off" : "";
    case 0x0041U: return off_on_name(value);
    case 0x0042U: return value == 1U ? "On" : "";
    case 0x0043U: return value == 2U ? "Off" : "";
    case 0x0045U: return off_on_name(value);
    case 0x0046U: return off_on_name(value);
    case 0x0048U: return value == 1U ? "1/60 s" : "";
    case 0x0049U: return value == 1U ? "Entire Frame" : "";
    case 0x004AU: return value == 1U ? "Subject and Background" : "";
    case 0x005AU: return value == 2U ? "Autofocus Off, Exposure Off" : "";
    case 0x005BU: return value == 3U ? "Off" : "";
    case 0x005CU: return value == 1U ? "10 Frames" : "";
    case 0x005DU: return value == 2U ? "No" : "";
    case 0x005EU: return value == 2U ? "- 0 +" : "";
    case 0x0062U: return value == 1U ? "Take Photo" : "";
    case 0x008BU: return value == 1U ? "No" : "";
    case 0x008DU: return value == 1U ? "Red" : "";
    case 0x008EU: return value == 1U ? "On" : "";
    case 0x008FU: return off_on_name(value);
    case 0x0090U: return off_on_name(value);
    case 0x0091U: return value == 2U ? "248" : "";
    case 0x0092U: return off_on_name(value);
    case 0x0093U: return value == 3U ? "3 (Normal)" : "";
    case 0x0095U: return off_on_name(value);
    case 0x0099U: return value == 1U ? "On" : "";
    case 0x009BU: return value == 1U ? "Sync" : "";
    case 0x009CU: return off_on_name(value);
    case 0x00ABU: return off_on_name(value);
    case 0x00DAU: return off_on_name(value);
    case 0x00DFU: return off_on_name(value);
    case 0x00E0U: return value == 4U ? "Off" : "";
    case 0x00F1U: return value == 3U ? "Off" : "";
    case 0x00F2U:
    case 0x00F3U:
    case 0x00F4U:
    case 0x00F5U:
    case 0x00F6U: return value == 2U ? "Yes" : "";
    case 0x00F7U: return value == 1U ? "No" : "";
    default: return "";
    }
}

static bool
is_nikon_metering_ifd_tag(std::string_view ifd, uint16_t tag) noexcept
{
    if (tag == 0x0017U
        && ifd_matches_context(ifd, "mk_nikon_bracketinginfod810_",
                               "makernote:nikon:bracketinginfod810")) {
        return true;
    }
    if (tag == 0x0214U
        && ifd_matches_context(ifd, "mk_nikon_otherinfod500_",
                               "makernote:nikon:otherinfod500")) {
        return true;
    }
    if (tag == 0x02D2U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz6iii_",
                               "makernote:nikon:menusettingsz6iii")) {
        return true;
    }
    if (tag == 0x0146U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz7ii_",
                               "makernote:nikon:menusettingsz7ii")) {
        return true;
    }
    if (tag == 0x033EU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz8_",
                               "makernote:nikon:menusettingsz8")) {
        return true;
    }
    if (tag == 0x02C2U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9_",
                               "makernote:nikon:menusettingsz9")) {
        return true;
    }
    if (tag == 0x02EAU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9v3_",
                               "makernote:nikon:menusettingsz9v3")) {
        return true;
    }
    if (tag == 0x02EAU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9v4_",
                               "makernote:nikon:menusettingsz9v4")) {
        return true;
    }
    return false;
}

static bool
is_nikon_movie_focus_ifd_tag(std::string_view ifd, uint16_t tag) noexcept
{
    if (tag == 0x0248U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz7ii_",
                               "makernote:nikon:menusettingsz7ii")) {
        return true;
    }
    if (tag == 0x0340U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz8_",
                               "makernote:nikon:menusettingsz8")) {
        return true;
    }
    if (tag == 0x02C4U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9_",
                               "makernote:nikon:menusettingsz9")) {
        return true;
    }
    if (tag == 0x02ECU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9v3_",
                               "makernote:nikon:menusettingsz9v3")) {
        return true;
    }
    if (tag == 0x02ECU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9v4_",
                               "makernote:nikon:menusettingsz9v4")) {
        return true;
    }
    return false;
}

static bool
is_nikon_menu_multiple_exposure_ifd_tag(std::string_view ifd,
                                        uint16_t tag) noexcept
{
    if (tag == 0x01BCU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz6iii_",
                               "makernote:nikon:menusettingsz6iii")) {
        return true;
    }
    if (tag == 0x0098U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz8_",
                               "makernote:nikon:menusettingsz8")) {
        return true;
    }
    if (tag == 0x008CU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9_",
                               "makernote:nikon:menusettingsz9")) {
        return true;
    }
    if (tag == 0x009AU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9v3_",
                               "makernote:nikon:menusettingsz9v3")) {
        return true;
    }
    if (tag == 0x009AU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9v4_",
                               "makernote:nikon:menusettingsz9v4")) {
        return true;
    }
    return false;
}

static const char*
nikon_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_nikon_settings_main_ifd(ifd)) {
        return nikon_settings_value_name(tag, value);
    }
    if (is_nikon_main_ifd(ifd)) {
        switch (tag) {
        case 0x001EU: return canon_color_space_name(value);
        case 0x0022U: return nikon_active_d_lighting_name(value);
        case 0x002AU: return nikon_vignette_control_name(value);
        case 0x0034U: return nikon_shutter_mode_name(value);
        case 0x003EU: return nikon_image_size_raw_name(value);
        case 0x0044U: return nikon_jpg_compression_name(value);
        case 0x0083U: return nikon_lens_type_name(value);
        case 0x0087U: return nikon_flash_mode_name(value);
        case 0x0089U: return nikon_shooting_mode_name(value);
        case 0x009DU: return nikon_date_stamp_mode_name(value);
        case 0x00B1U: return nikon_high_iso_noise_reduction_name(value);
        case 0x00BFU: return value == 0U ? "Off" : "";
        default: break;
        }
    }
    if (is_nikon_af_info_ifd(ifd)) {
        switch (tag) {
        case 0x0000U: return nikon_af_area_mode_name(value);
        case 0x0001U: return nikon_af_point_name(value);
        case 0x0002U: return nikon_af_points_in_focus_name(value);
        default: return "";
        }
    }
    if (is_nikon_af_info2v0100_ifd(ifd)) {
        switch (tag) {
        case 0x0005U: return value == 0U ? "Single Area" : "";
        case 0x0007U:
            switch (value) {
            case 0U: return "(none)";
            default: return "";
            }
        case 0x001CU: return no_yes_name(value);
        default: return "";
        }
    }
    if (is_nikon_iso_info_ifd(ifd)) {
        switch (tag) {
        case 0x0004U:
        case 0x000AU: return value == 0U ? "Off" : "";
        default: return "";
        }
    }
    if (is_nikon_hdr_info_ifd(ifd)) {
        switch (tag) {
        case 0x0004U: return nikon_hdr_name(value);
        case 0x0005U:
        case 0x0007U: return nikon_hdr_level_name(value);
        case 0x0006U: return value == 0U ? "Off" : "";
        default: return "";
        }
    }
    if (is_nikon_vr_info_ifd(ifd)) {
        switch (tag) {
        case 0x0004U: return nikon_vibration_reduction_name(value);
        default: return "";
        }
    }
    if (is_nikon_flash_info_ifd(ifd)) {
        switch (tag) {
        case 0x0004U: return nikon_flash_source_name(value);
        case 0x0008U: return nikon_external_flash_flags_name(value);
        case 0x000FU: return value == 0U ? "Off" : "";
        case 0x0025U: return value == 0U ? "Standard" : "";
        default: return "";
        }
    }
    if (is_nikon_picture_control_ifd(ifd)) {
        switch (tag) {
        case 0x0030U:
        case 0x0036U: return nikon_picture_control_adjust_name(value);
        case 0x0037U:
        case 0x0038U:
        case 0x003FU:
        case 0x0040U:
        case 0x0047U:
        case 0x0048U: return nikon_picture_control_filter_name(value);
        default: return "";
        }
    }
    if (is_nikon_multi_exposure_ifd(ifd)) {
        switch (tag) {
        case 0x0001U: return nikon_menu_multiple_exposure_mode_name(value);
        case 0x0003U: return value == 0U ? "Off" : "";
        default: return "";
        }
    }
    if (is_nikon_world_time_ifd(ifd)) {
        switch (tag) {
        case 0x0002U: return no_yes_name(value);
        case 0x0003U: return nikon_date_display_format_name(value);
        default: return "";
        }
    }
    if (is_nikon_distort_info_ifd(ifd)) {
        switch (tag) {
        case 0x0004U: return off_on_name(value);
        default: return "";
        }
    }
    if (is_nikon_af_tune_ifd(ifd)) {
        switch (tag) {
        case 0x0000U:
            if (value == 0U) {
                return "Off";
            }
            return value == 1U ? "On (1)" : "";
        case 0x0001U: return value == 255U ? "n/a" : "";
        default: return "";
        }
    }
    if (is_nikon_lens_data0800_ifd(ifd)) {
        switch (tag) {
        case 0x0030U: return nikon_z_lens_id_name(value);
        case 0x0035U: return nikon_lens_mount_type_name(value);
        default: return "";
        }
    }
    if (is_nikon_location_info_ifd(ifd)) {
        switch (tag) {
        case 0x0004U: return value == 0U ? "n/a" : "";
        default: return "";
        }
    }
    if (is_nikon_metering_ifd_tag(ifd, tag)) {
        return nikon_metering_mode_name(value & 0x03U);
    }
    if (is_nikon_movie_focus_ifd_tag(ifd, tag)) {
        return nikon_movie_focus_mode_name(value);
    }
    if (is_nikon_menu_multiple_exposure_ifd_tag(ifd, tag)) {
        return nikon_menu_multiple_exposure_mode_name(value);
    }
    return "";
}

static bool
is_fujifilm_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_fujifilm0" || ifd == "mk_fuji0"
           || ifd_has_prefix(ifd, "mk_fujifilm_main")
           || ifd_has_prefix(ifd, "mk_fuji_main")
           || ifd == "makernote:fujifilm:main";
}

static const char*
fujifilm_on_off_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    default: return "";
    }
}

static const char*
fujifilm_sharpness_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "-4 (softest)";
    case 1U: return "-3 (very soft)";
    case 2U: return "-2 (soft)";
    case 3U: return "0 (normal)";
    case 4U: return "+2 (hard)";
    case 5U: return "+3 (very hard)";
    case 6U: return "+4 (hardest)";
    case 0x82U: return "-1 (medium soft)";
    case 0x84U: return "+1 (medium hard)";
    case 0x8000U: return "Film Simulation";
    case 0xFFFFU: return "n/a";
    default: return "";
    }
}

static const char*
fujifilm_parameter_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "0 (normal)";
    case 0x82U: return "-1 (medium low)";
    case 0x84U: return "+1 (medium high)";
    default: return "";
    }
}

static const char*
fujifilm_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Auto (white priority)";
    case 2U: return "Auto (ambiance priority)";
    case 0x100U: return "Daylight";
    case 0x200U: return "Cloudy";
    case 0x300U: return "Daylight Fluorescent";
    case 0x301U: return "Day White Fluorescent";
    case 0x302U: return "White Fluorescent";
    case 0x303U: return "Warm White Fluorescent";
    case 0x304U: return "Living Room Warm White Fluorescent";
    case 0x400U: return "Incandescent";
    case 0x500U: return "Flash";
    case 0x600U: return "Underwater";
    case 0xF00U: return "Custom";
    case 0xF01U: return "Custom2";
    case 0xF02U: return "Custom3";
    case 0xF03U: return "Custom4";
    case 0xF04U: return "Custom5";
    case 0xFF0U: return "Kelvin";
    default: return "";
    }
}

static const char*
fujifilm_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "On";
    case 2U: return "Off";
    case 3U: return "Red-eye reduction";
    case 4U: return "External";
    case 16U: return "Commander";
    case 0x8000U: return "Not Attached";
    case 0x8120U: return "TTL";
    case 0x8320U: return "TTL Auto - Did not fire";
    case 0x9840U: return "Manual";
    case 0x9860U: return "Flash Commander";
    case 0x9880U: return "Multi-flash";
    case 0xA920U: return "1st Curtain (front)";
    case 0xAA20U: return "TTL Slow - 1st Curtain (front)";
    case 0xAB20U: return "TTL Auto - 1st Curtain (front)";
    case 0xAD20U: return "TTL - Red-eye Flash - 1st Curtain (front)";
    case 0xAE20U: return "TTL Slow - Red-eye Flash - 1st Curtain (front)";
    case 0xAF20U: return "TTL Auto - Red-eye Flash - 1st Curtain (front)";
    case 0xC920U: return "2nd Curtain (rear)";
    case 0xCA20U: return "TTL Slow - 2nd Curtain (rear)";
    case 0xCB20U: return "TTL Auto - 2nd Curtain (rear)";
    case 0xCD20U: return "TTL - Red-eye Flash - 2nd Curtain (rear)";
    case 0xCE20U: return "TTL Slow - Red-eye Flash - 2nd Curtain (rear)";
    case 0xCF20U: return "TTL Auto - Red-eye Flash - 2nd Curtain (rear)";
    case 0xE920U: return "High Speed Sync (HSS)";
    default: return "";
    }
}

static const char*
fujifilm_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Manual";
    case 65535U: return "Movie";
    default: return "";
    }
}

static const char*
fujifilm_af_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "No";
    case 1U: return "Single Point";
    case 256U: return "Zone";
    case 512U: return "Wide/Tracking";
    default: return "";
    }
}

static const char*
fujifilm_picture_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x0000U: return "Auto";
    case 0x0001U: return "Portrait";
    case 0x0002U: return "Landscape";
    case 0x0003U: return "Macro";
    case 0x0004U: return "Sports";
    case 0x0005U: return "Night Scene";
    case 0x0006U: return "Program AE";
    case 0x0007U: return "Natural Light";
    case 0x0008U: return "Anti-blur";
    case 0x0009U: return "Beach & Snow";
    case 0x000AU: return "Sunset";
    case 0x000BU: return "Museum";
    case 0x000CU: return "Party";
    case 0x000DU: return "Flower";
    case 0x000EU: return "Text";
    case 0x000FU: return "Natural Light & Flash";
    case 0x0010U: return "Beach";
    case 0x0011U: return "Snow";
    case 0x0012U: return "Fireworks";
    case 0x0013U: return "Underwater";
    case 0x0014U: return "Portrait with Skin Correction";
    case 0x0016U: return "Panorama";
    case 0x0017U: return "Night (tripod)";
    case 0x0018U: return "Pro Low-light";
    case 0x0019U: return "Pro Focus";
    case 0x001AU: return "Portrait 2";
    case 0x001BU: return "Dog Face Detection";
    case 0x001CU: return "Cat Face Detection";
    case 0x0030U: return "HDR";
    case 0x0040U: return "Advanced Filter";
    case 0x0100U: return "Aperture-priority AE";
    case 0x0200U: return "Shutter speed priority AE";
    case 0x0300U: return "Manual";
    default: return "";
    }
}

static const char*
fujifilm_multiple_exposure_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Additive";
    case 2U: return "Average";
    case 3U: return "Light";
    case 4U: return "Dark";
    default: return "";
    }
}

static const char*
fujifilm_focus_warning_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Good";
    case 1U: return "Out of focus";
    default: return "";
    }
}

static const char*
fujifilm_exposure_warning_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Good";
    case 1U: return "Bad exposure";
    default: return "";
    }
}

static const char*
fujifilm_color_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 0x10U: return "Chrome";
    case 0x30U: return "B & W";
    default: return "";
    }
}

static const char*
fujifilm_blur_warning_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "None";
    case 1U: return "Blur Warning";
    default: return "";
    }
}

static const char*
fujifilm_dynamic_range_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Standard";
    case 3U: return "Wide";
    default: return "";
    }
}

static const char*
fujifilm_film_mode_name(uint64_t value) noexcept
{
    return value == 0U ? "F0/Standard (Provia)" : "";
}

static const char*
fujifilm_dynamic_range_setting_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Manual";
    default: return "";
    }
}

static const char*
fujifilm_shutter_type_name(uint64_t value) noexcept
{
    return value == 0U ? "Mechanical" : "";
}

static const char*
fujifilm_image_generation_name(uint64_t value) noexcept
{
    return value == 0U ? "Original Image" : "";
}

static const char*
fujifilm_scene_recognition_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Unrecognized";
    case 512U: return "Landscape Image";
    default: return "";
    }
}

static const char*
fujifilm_auto_bracketing_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    case 2U: return "No flash & flash";
    case 6U: return "Pixel Shift";
    default: return "";
    }
}

static const char*
fujifilm_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (!is_fujifilm_main_ifd(ifd)) {
        return "";
    }

    switch (tag) {
    case 0x1001U: return fujifilm_sharpness_name(value);
    case 0x1002U: return fujifilm_white_balance_name(value);
    case 0x1003U: return fujifilm_parameter_name(value);
    case 0x1004U: return value == 0U ? "Normal" : "";
    case 0x100EU: return fujifilm_parameter_name(value);
    case 0x1010U: return fujifilm_flash_mode_name(value);
    case 0x1020U: return fujifilm_on_off_name(value);
    case 0x1021U: return fujifilm_focus_mode_name(value);
    case 0x1022U: return fujifilm_af_mode_name(value);
    case 0x1030U: return fujifilm_on_off_name(value);
    case 0x1031U: return fujifilm_picture_mode_name(value);
    case 0x1037U: return fujifilm_multiple_exposure_name(value);
    case 0x1045U: return fujifilm_on_off_name(value);
    case 0x104CU: return value == 0U ? "Off" : "";
    case 0x104DU: return value == 0U ? "n/a" : "";
    case 0x1050U: return fujifilm_shutter_type_name(value);
    case 0x1100U: return fujifilm_auto_bracketing_name(value);
    case 0x1210U: return fujifilm_color_mode_name(value);
    case 0x1300U: return fujifilm_blur_warning_name(value);
    case 0x1301U: return fujifilm_focus_warning_name(value);
    case 0x1302U: return fujifilm_exposure_warning_name(value);
    case 0x1400U: return fujifilm_dynamic_range_name(value);
    case 0x1401U: return fujifilm_film_mode_name(value);
    case 0x1402U: return fujifilm_dynamic_range_setting_name(value);
    case 0x1425U: return fujifilm_scene_recognition_name(value);
    case 0x1436U: return fujifilm_image_generation_name(value);
    case 0x4201U: return value == 1U ? "Face" : "";
    default: return "";
    }
}

static bool
is_sony_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_sony0" || ifd_has_prefix(ifd, "mk_sony_main")
           || ifd == "makernote:sony:main";
}

static bool
is_sony_camera_settings_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_sony_camerasettings_")
           || ifd == "makernote:sony:camerasettings";
}

static bool
is_sony_camera_settings2_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_sony_camerasettings2_")
           || ifd == "makernote:sony:camerasettings2";
}

static bool
is_sony_camera_settings3_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_sony_camerasettings3_")
           || ifd == "makernote:sony:camerasettings3";
}

static bool
is_sony_more_settings_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_sony_moresettings_")
           || ifd == "makernote:sony:moresettings";
}

static bool
is_sony_tag2010_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_sony_tag2010")
           || ifd_has_prefix(ifd, "makernote:sony:tag2010");
}

static const char*
sony_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program AE";
    case 1U: return "Portrait";
    case 2U: return "Beach";
    case 3U: return "Sports";
    case 4U: return "Snow";
    case 5U: return "Landscape";
    case 6U: return "Auto";
    case 7U: return "Aperture-priority AE";
    case 8U: return "Shutter speed priority AE";
    case 9U: return "Night Scene / Twilight";
    case 10U: return "Hi-Speed Shutter";
    case 11U: return "Twilight Portrait";
    case 12U: return "Soft Snap/Portrait";
    case 13U: return "Fireworks";
    case 14U: return "Smile Shutter";
    case 15U: return "Manual";
    case 18U: return "High Sensitivity";
    case 19U: return "Macro";
    case 20U: return "Advanced Sports Shooting";
    case 29U: return "Underwater";
    case 33U: return "Food";
    case 34U: return "Sweep Panorama";
    case 35U: return "Handheld Night Shot";
    case 36U: return "Anti Motion Blur";
    case 37U: return "Pet";
    case 38U: return "Backlight Correction HDR";
    case 39U: return "Superior Auto";
    case 40U: return "Background Defocus";
    case 41U: return "Soft Skin";
    case 42U: return "3D Image";
    case 65535U: return "n/a";
    default: return "";
    }
}

static const char*
sony_exposure_program_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Manual";
    case 2U: return "Program AE";
    case 3U: return "Aperture-priority AE";
    case 4U: return "Shutter speed priority AE";
    case 8U: return "Program Shift A";
    case 9U: return "Program Shift S";
    case 16U: return "Portrait";
    case 17U: return "Sports";
    case 18U: return "Sunset";
    case 19U: return "Night Portrait";
    case 20U: return "Landscape";
    case 21U: return "Macro";
    case 35U: return "Auto No Flash";
    default: return "";
    }
}

static const char*
sony_exposure_program2_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Program AE";
    case 2U: return "Aperture-priority AE";
    case 3U: return "Shutter speed priority AE";
    case 4U: return "Manual";
    case 5U: return "Cont. Priority AE";
    case 16U: return "Auto";
    case 17U: return "Auto (no flash)";
    case 18U: return "Auto+";
    case 49U: return "Portrait";
    case 50U: return "Landscape";
    case 51U: return "Macro";
    case 52U: return "Sports";
    case 53U: return "Sunset";
    case 54U: return "Night view";
    case 55U: return "Night view/portrait";
    case 56U: return "Handheld Night Shot";
    case 57U: return "3D Sweep Panorama";
    case 64U: return "Auto 2";
    case 65U: return "Auto 2 (no flash)";
    case 80U: return "Sweep Panorama";
    case 96U: return "Anti Motion Blur";
    case 128U: return "Toy Camera";
    case 129U: return "Pop Color";
    case 130U: return "Posterization";
    case 131U: return "Posterization B/W";
    case 132U: return "Retro Photo";
    case 133U: return "High-key";
    case 134U: return "Partial Color Red";
    case 135U: return "Partial Color Green";
    case 136U: return "Partial Color Blue";
    case 137U: return "Partial Color Yellow";
    case 138U: return "High Contrast Monochrome";
    default: return "";
    }
}

static const char*
sony_exposure_program3_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program AE";
    case 1U: return "Aperture-priority AE";
    case 2U: return "Shutter speed priority AE";
    case 3U: return "Manual";
    case 4U: return "Auto";
    case 5U: return "iAuto";
    case 6U: return "Superior Auto";
    case 7U: return "iAuto+";
    case 8U: return "Portrait";
    case 9U: return "Landscape";
    case 10U: return "Twilight";
    case 11U: return "Twilight Portrait";
    case 12U: return "Sunset";
    case 14U: return "Action (High speed)";
    case 16U: return "Sports";
    case 17U: return "Handheld Night Shot";
    case 18U: return "Anti Motion Blur";
    case 19U: return "High Sensitivity";
    case 21U: return "Beach";
    case 22U: return "Snow";
    case 23U: return "Fireworks";
    case 26U: return "Underwater";
    case 27U: return "Gourmet";
    case 28U: return "Pet";
    case 29U: return "Macro";
    case 30U: return "Backlight Correction HDR";
    case 33U: return "Sweep Panorama";
    case 36U: return "Background Defocus";
    case 37U: return "Soft Skin";
    case 42U: return "3D Image";
    case 43U: return "Cont. Priority AE";
    case 45U: return "Document";
    case 46U: return "Party";
    default: return "";
    }
}

static const char*
sony_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Multi-segment";
    case 2U: return "Center-weighted average";
    case 4U: return "Spot";
    default: return "";
    }
}

static const char*
sony_metering_mode3_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Multi-segment";
    case 2U: return "Center-weighted average";
    case 3U: return "Spot";
    default: return "";
    }
}

static const char*
sony_metering_mode2_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x100U: return "Multi-segment";
    case 0x200U: return "Center-weighted average";
    case 0x301U: return "Spot (Standard)";
    case 0x302U: return "Spot (Large)";
    case 0x400U: return "Average";
    case 0x500U: return "Highlight";
    default: return "";
    }
}

static const char*
sony_metering_mode2010_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Multi-segment";
    case 2U: return "Center-weighted average";
    case 3U: return "Spot";
    case 4U: return "Average";
    case 5U: return "Highlight";
    default: return "";
    }
}

static const char*
sony_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Manual";
    case 1U: return "AF-S";
    case 2U: return "AF-C";
    case 3U: return "AF-A";
    default: return "";
    }
}

static const char*
sony_focus_mode_main_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Manual";
    case 2U: return "AF-S";
    case 3U: return "AF-C";
    case 4U: return "AF-A";
    case 6U: return "DMF";
    case 7U: return "AF-D";
    default: return "";
    }
}

static const char*
sony_focus_mode_setting_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Manual";
    case 1U: return "AF-S";
    case 2U: return "AF-C";
    case 3U: return "AF-A";
    case 4U: return "DMF";
    default: return "";
    }
}

static const char*
sony_focus_mode_setting_basic_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Manual";
    case 1U: return "AF-S";
    case 2U: return "AF-C";
    case 3U: return "AF-A";
    default: return "";
    }
}

static const char*
sony_focus_mode_setting2_name(uint64_t value) noexcept
{
    switch (value) {
    case 17U: return "AF-S";
    case 18U: return "AF-C";
    case 19U: return "AF-A";
    case 32U: return "Manual";
    case 48U: return "DMF";
    default: return "";
    }
}

static const char*
sony_af_area_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Wide";
    case 1U: return "Local";
    case 2U: return "Spot";
    default: return "";
    }
}

static const char*
sony_af_area_mode2_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Wide";
    case 2U: return "Spot";
    case 3U: return "Local";
    case 4U: return "Flexible";
    default: return "";
    }
}

static const char*
sony_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Autoflash";
    case 2U: return "Rear Sync";
    case 3U: return "Wireless";
    case 4U: return "Fill-flash";
    case 5U: return "Flash Off";
    case 6U: return "Slow Sync";
    default: return "";
    }
}

static const char*
sony_flash_mode2_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Flash Off";
    case 16U: return "Autoflash";
    case 17U: return "Fill-flash";
    case 18U: return "Slow Sync";
    case 19U: return "Rear Sync";
    case 20U: return "Wireless";
    default: return "";
    }
}

static const char*
sony_flash_mode2010_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Autoflash";
    case 1U: return "Fill-flash";
    case 2U: return "Flash Off";
    case 3U: return "Slow Sync";
    case 4U: return "Rear Sync";
    case 6U: return "Wireless";
    default: return "";
    }
}

static const char*
sony_drive_mode_name(uint64_t value) noexcept
{
    switch (value & 0xFFU) {
    case 0x01U: return "Single Frame";
    case 0x02U: return "Continuous High";
    case 0x04U: return "Self-timer 10 sec";
    case 0x05U: return "Self-timer 2 sec, Mirror Lock-up";
    case 0x06U: return "Single-frame Bracketing";
    case 0x07U: return "Continuous Bracketing";
    case 0x0AU: return "Remote Commander";
    case 0x0BU: return "Mirror Lock-up";
    case 0x12U: return "Continuous Low";
    case 0x18U: return "White Balance Bracketing Low";
    case 0x19U: return "D-Range Optimizer Bracketing Low";
    case 0x28U: return "White Balance Bracketing High";
    case 0x29U: return "D-Range Optimizer Bracketing High";
    default: return "";
    }
}

static const char*
sony_drive_mode2_name(uint64_t value) noexcept
{
    switch (value & 0xFFU) {
    case 0x01U: return "Single Frame";
    case 0x02U: return "Continuous High";
    case 0x04U: return "Self-timer 10 sec";
    case 0x05U: return "Self-timer 2 sec, Mirror Lock-up";
    case 0x07U: return "Continuous Bracketing";
    case 0x0AU: return "Remote Commander";
    case 0x0BU: return "Continuous Self-timer";
    default: return "";
    }
}

static const char*
sony_drive_mode3_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x10U: return "Single Frame";
    case 0x21U: return "Continuous High";
    case 0x22U: return "Continuous Low";
    case 0x30U: return "Speed Priority Continuous";
    case 0x51U: return "Self-timer 10 sec";
    case 0x52U: return "Self-timer 2 sec, Mirror Lock-up";
    case 0x71U: return "Continuous Bracketing 0.3 EV";
    case 0x75U: return "Continuous Bracketing 0.7 EV";
    case 0x91U: return "White Balance Bracketing Low";
    case 0x92U: return "White Balance Bracketing High";
    case 0xC0U: return "Remote Commander";
    case 0xD1U: return "Continuous - HDR";
    case 0xD2U: return "Continuous - Multi Frame NR";
    case 0xD3U: return "Continuous - Handheld Night Shot";
    case 0xD4U: return "Continuous - Anti Motion Blur";
    case 0xD5U: return "Continuous - Sweep Panorama";
    case 0xD6U: return "Continuous - 3D Sweep Panorama";
    default: return "";
    }
}

static const char*
sony_release_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Continuous";
    case 2U: return "Continuous - Exposure Bracketing";
    case 3U: return "DRO or White Balance Bracketing";
    case 5U: return "Continuous - Burst";
    case 6U: return "Single Frame - Capture During Movie";
    case 7U: return "Continuous - Sweep Panorama";
    case 8U: return "Continuous - Anti-Motion Blur, Hand-held Twilight";
    case 9U: return "Continuous - HDR";
    case 10U: return "Continuous - Background defocus";
    case 13U: return "Continuous - 3D Sweep Panorama";
    case 15U: return "Continuous - High Resolution Sweep Panorama";
    case 16U: return "Continuous - 3D Image";
    case 17U: return "Continuous - Burst 2";
    case 18U: return "Normal - iAuto+";
    case 19U: return "Continuous - Speed/Advance Priority";
    case 20U: return "Continuous - Multi Frame NR";
    case 23U: return "Single-frame - Exposure Bracketing";
    case 26U: return "Continuous Low";
    case 27U: return "Continuous - High Sensitivity";
    case 28U: return "Smile Shutter";
    case 29U: return "Continuous - Tele-zoom Advance Priority";
    case 146U: return "Single Frame - Movie Capture";
    default: return "";
    }
}

static const char*
sony_release_mode_main_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 2U: return "Continuous";
    case 5U: return "Exposure Bracketing";
    case 6U: return "White Balance Bracketing";
    case 8U: return "DRO Bracketing";
    case 65535U: return "n/a";
    default: return "";
    }
}

static const char*
sony_release_mode3_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Continuous";
    case 2U: return "Bracketing";
    case 4U: return "Continuous - Burst";
    case 5U: return "Continuous - Speed/Advance Priority";
    case 6U: return "Normal - Self-timer";
    case 9U: return "Single Burst Shooting";
    default: return "";
    }
}

static bool
is_sony_tag2010b_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_sony_tag2010b_")
           || ifd == "makernote:sony:tag2010b";
}

static const char*
sony_quality2010_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "JPEG";
    case 1U: return "RAW";
    case 2U: return "RAW + JPEG";
    default: return "";
    }
}

static bool
is_sony_tag2010_release_mode(uint16_t tag) noexcept
{
    switch (tag) {
    case 0x0004U:
    case 0x0008U:
    case 0x0208U:
    case 0x0210U:
    case 0x1018U:
    case 0x1108U:
    case 0x112CU:
    case 0x1160U:
    case 0x1184U: return true;
    default: return false;
    }
}

static bool
is_sony_tag2010_release_mode3(uint16_t tag) noexcept
{
    switch (tag) {
    case 0x0204U:
    case 0x020CU:
    case 0x1014U:
    case 0x1104U:
    case 0x1128U:
    case 0x115CU:
    case 0x1180U: return true;
    default: return false;
    }
}

static bool
is_sony_tag2010_flash_mode(uint16_t tag) noexcept
{
    switch (tag) {
    case 0x0211U:
    case 0x021CU:
    case 0x1024U:
    case 0x1114U:
    case 0x1138U:
    case 0x116CU:
    case 0x1190U: return true;
    default: return false;
    }
}

static bool
is_sony_tag2010_metering_mode(uint16_t tag) noexcept
{
    switch (tag) {
    case 0x024BU:
    case 0x025CU:
    case 0x1064U:
    case 0x1154U:
    case 0x1174U:
    case 0x1178U:
    case 0x11ACU:
    case 0x11D0U: return true;
    default: return false;
    }
}

static bool
is_sony_tag2010_exposure_program(uint16_t tag) noexcept
{
    switch (tag) {
    case 0x024CU:
    case 0x025DU:
    case 0x1065U:
    case 0x1155U:
    case 0x1175U:
    case 0x1179U:
    case 0x11ADU:
    case 0x11D1U: return true;
    default: return false;
    }
}

static const char*
sony_tag2010_value_name(std::string_view ifd, uint16_t tag,
                        uint64_t value) noexcept
{
    if (is_sony_tag2010b_ifd(ifd) && tag == 0x1174U) {
        return sony_quality2010_name(value);
    }
    if (is_sony_tag2010_release_mode(tag)) {
        return sony_release_mode_name(value);
    }
    if (is_sony_tag2010_release_mode3(tag)) {
        return sony_release_mode3_name(value);
    }
    if (is_sony_tag2010_flash_mode(tag)) {
        return sony_flash_mode2010_name(value);
    }
    if (is_sony_tag2010_metering_mode(tag)) {
        return sony_metering_mode2010_name(value);
    }
    if (is_sony_tag2010_exposure_program(tag)) {
        return sony_exposure_program3_name(value);
    }
    return "";
}

static const char*
sony_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x201BU: return sony_focus_mode_main_name(value);
    case 0x202CU: return sony_metering_mode2_name(value);
    case 0xB041U: return sony_exposure_mode_name(value);
    case 0xB049U: return sony_release_mode_main_name(value);
    default: return "";
    }
}

static const char*
sony_camera_settings_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0004U: return sony_drive_mode_name(value);
    case 0x0010U: return sony_focus_mode_setting_name(value);
    case 0x0011U: return sony_af_area_mode_name(value);
    case 0x0013U: return sony_flash_mode_name(value);
    case 0x0015U: return sony_metering_mode_name(value);
    case 0x003CU: return sony_exposure_program_name(value);
    case 0x004DU: return sony_focus_mode_name(value);
    default: return "";
    }
}

static const char*
sony_camera_settings2_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x000FU: return sony_focus_mode_setting_basic_name(value);
    case 0x0010U: return sony_af_area_mode_name(value);
    case 0x0013U: return sony_metering_mode_name(value);
    case 0x003CU: return sony_exposure_program_name(value);
    case 0x004DU: return sony_focus_mode_name(value);
    case 0x007EU: return sony_drive_mode2_name(value);
    case 0x007FU: return sony_flash_mode_name(value);
    default: return "";
    }
}

static const char*
sony_camera_settings3_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0004U: return sony_drive_mode3_name(value);
    case 0x0005U: return sony_exposure_program2_name(value);
    case 0x0006U: return sony_focus_mode_setting2_name(value);
    case 0x0007U: return sony_metering_mode3_name(value);
    case 0x0020U: return sony_flash_mode2_name(value);
    case 0x0024U: return sony_af_area_mode2_name(value);
    case 0x0034U: return sony_drive_mode3_name(value);
    default: return "";
    }
}

static const char*
sony_more_settings_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return sony_drive_mode3_name(value);
    case 0x0002U: return sony_exposure_program2_name(value);
    case 0x0003U: return sony_metering_mode3_name(value);
    case 0x0010U: return sony_flash_mode2_name(value);
    case 0x0013U: return sony_focus_mode_setting2_name(value);
    default: return "";
    }
}

static const char*
sony_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_sony_main_ifd(ifd)) {
        return sony_main_value_name(tag, value);
    }
    if (is_sony_camera_settings_ifd(ifd)) {
        return sony_camera_settings_value_name(tag, value);
    }
    if (is_sony_camera_settings2_ifd(ifd)) {
        return sony_camera_settings2_value_name(tag, value);
    }
    if (is_sony_camera_settings3_ifd(ifd)) {
        return sony_camera_settings3_value_name(tag, value);
    }
    if (is_sony_more_settings_ifd(ifd)) {
        return sony_more_settings_value_name(tag, value);
    }
    if (is_sony_tag2010_ifd(ifd)) {
        return sony_tag2010_value_name(ifd, tag, value);
    }
    return "";
}

static bool
is_pentax_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_pentax0" || ifd_has_prefix(ifd, "mk_pentax_main")
           || ifd == "makernote:pentax:main";
}

static bool
is_pentax_ae_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_pentax_aeinfo_")
           || ifd == "makernote:pentax:aeinfo";
}

static bool
is_pentax_flash_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_pentax_flashinfo_")
           || ifd == "makernote:pentax:flashinfo";
}

static bool
is_pentax_type2_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_pentax_type2_")
           || ifd == "makernote:pentax:type2";
}

static bool
is_pentax_lens_corr_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_pentax_lenscorr_")
           || ifd == "makernote:pentax:lenscorr";
}

static bool
is_pentax_awb_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_pentax_awbinfo_")
           || ifd == "makernote:pentax:awbinfo";
}

static bool
is_pentax_sr_info2_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_pentax_srinfo2_")
           || ifd == "makernote:pentax:srinfo2";
}

static bool
is_pentax_lens_rec_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_pentax_lensrec_")
           || ifd == "makernote:pentax:lensrec";
}

static bool
is_pentax_time_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_pentax_timeinfo_")
           || ifd == "makernote:pentax:timeinfo";
}

static const char*
pentax_picture_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program";
    case 1U: return "Shutter Speed Priority";
    case 2U: return "Program AE";
    case 3U: return "Manual";
    case 5U: return "Portrait";
    case 6U: return "Landscape";
    case 8U: return "Sport";
    case 9U: return "Night Scene";
    case 11U: return "Soft";
    case 12U: return "Surf & Snow";
    case 13U: return "Candlelight";
    case 14U: return "Autumn";
    case 15U: return "Macro";
    case 17U: return "Fireworks";
    case 18U: return "Text";
    case 19U: return "Panorama";
    case 20U: return "3-D";
    case 21U: return "Black & White";
    case 22U: return "Sepia";
    case 30U: return "Self Portrait";
    case 35U: return "Night Scene Portrait";
    case 37U: return "Museum";
    case 38U: return "Food";
    case 39U: return "Underwater";
    case 40U: return "Green Mode";
    case 58U: return "Frame Composite";
    case 60U: return "Kids";
    case 61U: return "Blur Reduction";
    case 63U: return "Panorama 2";
    case 65U: return "Half-length Portrait";
    case 66U: return "Portrait 2";
    case 74U: return "Digital Microscope";
    case 75U: return "Blue Sky";
    case 80U: return "Miniature";
    case 81U: return "HDR";
    case 83U: return "Fisheye";
    case 85U: return "Digital Filter 4";
    default: return "";
    }
}

static const char*
pentax_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x000U: return "Auto, Did not fire";
    case 0x001U: return "Off, Did not fire";
    case 0x002U: return "On, Did not fire";
    case 0x003U: return "Auto, Did not fire, Red-eye reduction";
    case 0x005U: return "On, Did not fire, Wireless (Master)";
    case 0x100U: return "Auto, Fired";
    case 0x102U: return "On, Fired";
    case 0x103U: return "Auto, Fired, Red-eye reduction";
    case 0x104U: return "On, Red-eye reduction";
    case 0x105U: return "On, Wireless (Master)";
    case 0x106U: return "On, Wireless (Control)";
    case 0x108U: return "On, Soft";
    case 0x109U: return "On, Slow-sync";
    case 0x10AU: return "On, Slow-sync, Red-eye reduction";
    case 0x10BU: return "On, Trailing-curtain Sync";
    case 0x300U: return "External, Manual";
    case 0x304U: return "External, P-TTL Auto";
    case 0x306U: return "External, High-speed Sync";
    case 0x30CU: return "External, Wireless";
    default: return "";
    }
}

static const char*
pentax_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x0000U: return "Normal";
    case 0x0001U: return "Macro";
    case 0x0002U: return "Infinity";
    case 0x0003U: return "Manual";
    case 0x0004U: return "Super Macro";
    case 0x0005U: return "Pan Focus";
    case 0x0006U: return "Auto-area";
    case 0x0007U: return "Zone Select";
    case 0x0008U: return "Select";
    case 0x0009U: return "Pinpoint";
    case 0x000AU: return "Tracking";
    case 0x000BU: return "Continuous";
    case 0x000CU: return "Snap";
    case 0x0010U: return "AF-S (Focus-priority)";
    case 0x0011U: return "AF-C (Focus-priority)";
    case 0x0012U: return "AF-A (Focus-priority)";
    case 0x0020U: return "Contrast-detect (Focus-priority)";
    case 0x0021U: return "Tracking Contrast-detect (Focus-priority)";
    case 0x0110U: return "AF-S (Release-priority)";
    case 0x0111U: return "AF-C (Release-priority)";
    case 0x0112U: return "AF-A (Release-priority)";
    case 0x0120U: return "Contrast-detect (Release-priority)";
    case 0x8003U: return "Manual (Macro)";
    case 0x8006U: return "Auto-area (Macro)";
    default: return "";
    }
}

static const char*
pentax_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Multi-segment";
    case 1U: return "Center-weighted average";
    case 2U: return "Spot";
    case 6U: return "Highlight";
    default: return "";
    }
}

static const char*
pentax_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Shade";
    case 3U: return "Fluorescent";
    case 4U: return "Tungsten";
    case 5U: return "Manual";
    case 6U: return "Daylight Fluorescent";
    case 7U: return "Day White Fluorescent";
    case 8U: return "White Fluorescent";
    case 9U: return "Flash";
    case 10U: return "Cloudy";
    case 11U: return "Warm White Fluorescent";
    case 14U: return "Multi Auto";
    case 15U: return "Color Temperature Enhancement";
    case 17U: return "Kelvin";
    case 0xFFFEU: return "Unknown";
    case 0xFFFFU: return "User-Selected";
    default: return "";
    }
}

static const char*
pentax_ae_program_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "M, P or TAv";
    case 1U: return "Av, B or X";
    case 2U: return "Tv";
    case 3U: return "Sv or Green Mode";
    case 8U: return "Hi-speed Program";
    case 11U: return "Hi-speed Program (P-Shift)";
    case 16U: return "DOF Program";
    case 19U: return "DOF Program (P-Shift)";
    case 24U: return "MTF Program";
    case 27U: return "MTF Program (P-Shift)";
    case 35U: return "Standard";
    case 43U: return "Portrait";
    case 51U: return "Landscape";
    case 59U: return "Macro";
    case 67U: return "Sport";
    case 75U: return "Night Scene Portrait";
    case 83U: return "No Flash";
    case 91U: return "Night Scene";
    case 99U: return "Surf & Snow";
    case 104U: return "Night Snap";
    case 107U: return "Text";
    case 115U: return "Sunset";
    case 123U: return "Kids";
    case 131U: return "Pet";
    case 139U: return "Candlelight";
    case 144U: return "SCN";
    case 147U: return "Museum";
    case 160U: return "Program";
    case 184U: return "Shallow DOF Program";
    case 216U: return "HDR";
    default: return "";
    }
}

static const char*
pentax_ae_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Multi-segment";
    case 16U: return "Center-weighted average";
    case 32U: return "Spot";
    default: return "";
    }
}

static const char*
pentax_flash_status_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x00U: return "Off";
    case 0x01U: return "Off (1)";
    case 0x02U: return "External, Did not fire";
    case 0x06U: return "External, Fired";
    case 0x08U: return "Internal, Did not fire (0x08)";
    case 0x09U: return "Internal, Did not fire";
    case 0x0DU: return "Internal, Fired";
    default: return "";
    }
}

static const char*
pentax_internal_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x00U: return "n/a - Off-Auto-Aperture";
    case 0x86U: return "Fired, Wireless (Control)";
    case 0x95U: return "Fired, Wireless (Master)";
    case 0xC0U: return "Fired";
    case 0xC1U: return "Fired, Red-eye reduction";
    case 0xC2U: return "Fired, Auto";
    case 0xC3U: return "Fired, Auto, Red-eye reduction";
    case 0xC8U: return "Fired, Slow-sync";
    case 0xC9U: return "Fired, Slow-sync, Red-eye reduction";
    case 0xCAU: return "Fired, Trailing-curtain Sync";
    case 0xF0U: return "Did not fire, Normal";
    case 0xF1U: return "Did not fire, Red-eye reduction";
    case 0xF2U: return "Did not fire, Auto";
    case 0xF3U: return "Did not fire, Auto, Red-eye reduction";
    case 0xF5U: return "Did not fire, Wireless (Master)";
    default: return "";
    }
}

static const char*
pentax_external_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x00U: return "n/a - Off-Auto-Aperture";
    case 0x3FU: return "Off";
    case 0x40U: return "On, Auto";
    case 0xBFU: return "On, Flash Problem";
    case 0xC0U: return "On, Manual";
    case 0xC4U: return "On, P-TTL Auto";
    case 0xC5U: return "On, Contrast-control Sync";
    case 0xC6U: return "On, High-speed Sync";
    case 0xCCU: return "On, Wireless";
    case 0xCDU: return "On, Wireless, High-speed Sync";
    case 0xF0U: return "Not Connected";
    default: return "";
    }
}

static const char*
pentax_type2_recording_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Night Scene";
    case 2U: return "Manual";
    default: return "";
    }
}

static const char*
pentax_type2_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 2U: return "Custom";
    case 3U: return "Auto";
    default: return "";
    }
}

static const char*
pentax_type2_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Auto";
    case 2U: return "On";
    case 4U: return "Off";
    case 6U: return "Red-eye reduction";
    default: return "";
    }
}

static const char*
pentax_type2_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Shade";
    case 3U: return "Tungsten";
    case 4U: return "Fluorescent";
    case 5U: return "Manual";
    default: return "";
    }
}

static const char*
pentax_model_id_name(uint64_t value) noexcept
{
    return value == 76720U ? "Optio T10/T20" : "";
}

static const char*
pentax_quality_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Better";
    case 2U: return "Best";
    default: return "";
    }
}

static const char*
pentax_image_size_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "640x480";
    case 10U: return "3264x2448";
    case 29U: return "4000x3000";
    case 31U: return "4608x3456";
    default: return "";
    }
}

static const char*
pentax_iso_name(uint64_t value) noexcept
{
    switch (value) {
    case 3U: return "50";
    case 4U: return "64";
    case 5U: return "80";
    case 6U: return "100";
    case 7U: return "125";
    case 9U: return "200";
    default: return "";
    }
}

static const char*
pentax_af_point_selected_name(uint64_t value) noexcept
{
    switch (value) {
    case 65534U: return "Fixed Center";
    case 65535U: return "Auto";
    default: return "";
    }
}

static const char*
pentax_af_points_in_focus_name(uint64_t value) noexcept
{
    switch (value) {
    case 5U: return "Center";
    default: return "";
    }
}

static const char*
pentax_white_balance_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Auto (Daylight)";
    case 65535U: return "User-Selected";
    default: return "";
    }
}

static const char*
pentax_parameter_name(uint64_t value) noexcept
{
    return value == 1U ? "0 (normal)" : "";
}

static const char*
pentax_contrast_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "0 (normal)";
    case 4U: return "+1 (medium high)";
    default: return "";
    }
}

static const char*
pentax_sharpness_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "0 (normal)";
    case 4U: return "+1 (medium hard)";
    default: return "";
    }
}

static const char*
pentax_world_time_location_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Hometown";
    case 1U: return "Destination";
    default: return "";
    }
}

static const char*
pentax_city_name(uint64_t value) noexcept
{
    switch (value) {
    case 12U: return "New York";
    case 20U: return "London";
    case 56U: return "Tokyo";
    default: return "";
    }
}

static const char*
pentax_color_space_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "sRGB";
    case 1U: return "Adobe RGB";
    default: return "";
    }
}

static const char*
pentax_image_tone_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Natural";
    case 1U: return "Bright";
    default: return "";
    }
}

static const char*
pentax_hue_name(uint64_t value) noexcept
{
    return value == 1U ? "Normal" : "";
}

static const char*
pentax_monochrome_effect_name(uint64_t value) noexcept
{
    return value == 65535U ? "None" : "";
}

static const char*
pentax_bleach_bypass_toning_name(uint64_t value) noexcept
{
    return value == 65535U ? "n/a" : "";
}

static const char*
pentax_aspect_ratio_name(uint64_t value) noexcept
{
    return value == 1U ? "3:2" : "";
}

static const char*
pentax_shutter_type_name(uint64_t value) noexcept
{
    return value == 0U ? "Normal" : "";
}

static const char*
pentax_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0005U: return pentax_model_id_name(value);
    case 0x0008U: return pentax_quality_name(value);
    case 0x0009U: return pentax_image_size_name(value);
    case 0x000BU: return pentax_picture_mode_name(value);
    case 0x000CU: return pentax_flash_mode_name(value);
    case 0x000DU: return pentax_focus_mode_name(value);
    case 0x000EU: return pentax_af_point_selected_name(value);
    case 0x000FU: return pentax_af_points_in_focus_name(value);
    case 0x0014U: return pentax_iso_name(value);
    case 0x0017U: return pentax_metering_mode_name(value);
    case 0x0019U: return pentax_white_balance_name(value);
    case 0x001AU: return pentax_white_balance_mode_name(value);
    case 0x001FU: return pentax_parameter_name(value);
    case 0x0020U: return pentax_contrast_name(value);
    case 0x0021U: return pentax_sharpness_name(value);
    case 0x0022U: return pentax_world_time_location_name(value);
    case 0x0023U: return pentax_city_name(value);
    case 0x0024U: return pentax_city_name(value);
    case 0x0025U: return no_yes_name(value);
    case 0x0026U: return no_yes_name(value);
    case 0x0037U: return pentax_color_space_name(value);
    case 0x0048U: return off_on_name(value);
    case 0x0049U: return off_on_name(value);
    case 0x004FU: return pentax_image_tone_name(value);
    case 0x0067U: return pentax_hue_name(value);
    case 0x006FU: return off_on_name(value);
    case 0x0073U: return pentax_monochrome_effect_name(value);
    case 0x0074U: return pentax_monochrome_effect_name(value);
    case 0x0079U: return off_on_name(value);
    case 0x007BU: return off_on_name(value);
    case 0x007FU: return pentax_bleach_bypass_toning_name(value);
    case 0x0080U: return pentax_aspect_ratio_name(value);
    case 0x0087U: return pentax_shutter_type_name(value);
    default: return "";
    }
}

static const char*
pentax_ae_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0006U: return pentax_ae_program_mode_name(value);
    case 0x000CU: return pentax_ae_metering_mode_name(value);
    default: return "";
    }
}

static const char*
pentax_flash_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0000U: return pentax_flash_status_name(value);
    case 0x0001U: return pentax_internal_flash_mode_name(value);
    case 0x0002U: return pentax_external_flash_mode_name(value);
    default: return "";
    }
}

static const char*
pentax_type2_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return pentax_type2_recording_mode_name(value);
    case 0x0003U: return pentax_type2_focus_mode_name(value);
    case 0x0004U: return pentax_type2_flash_mode_name(value);
    case 0x0007U: return pentax_type2_white_balance_name(value);
    default: return "";
    }
}

static const char*
pentax_lens_corr_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0000U:
    case 0x0001U:
    case 0x0002U: return off_on_name(value);
    case 0x0003U:
        if (value == 0U) {
            return "Off";
        }
        return value == 16U ? "On" : "";
    default: return "";
    }
}

static const char*
pentax_lens_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x032EU: return "Sigma/Samsung/Tokina Lens";
    default: return "";
    }
}

static const char*
pentax_awb_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0000U: return off_on_name(value);
    case 0x0001U:
        switch (value) {
        case 0U: return "Subtle Correction";
        case 1U: return "Strong Correction";
        default: return "";
        }
    default: return "";
    }
}

static const char*
pentax_sr_info2_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0000U: return value == 1U ? "[0]" : "";
    case 0x0001U: return value == 7U ? "On (AA simulation off)" : "";
    default: return "";
    }
}

static const char*
pentax_lens_rec_value_name(uint16_t tag, uint64_t value) noexcept
{
    if (tag == 0x0003U && value == 0U) {
        return "Not attached";
    }
    return "";
}

static const char*
pentax_lens_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0000U:
    case 0x0001U: return pentax_lens_type_name(value);
    default: return "";
    }
}

static const char*
pentax_time_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0002U:
    case 0x0003U: return pentax_city_name(value);
    default: return "";
    }
}

static const char*
pentax_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_pentax_main_ifd(ifd)) {
        return pentax_main_value_name(tag, value);
    }
    if (is_pentax_ae_info_ifd(ifd)) {
        return pentax_ae_info_value_name(tag, value);
    }
    if (is_pentax_flash_info_ifd(ifd)) {
        return pentax_flash_info_value_name(tag, value);
    }
    if (is_pentax_type2_ifd(ifd)) {
        return pentax_type2_value_name(tag, value);
    }
    if (is_pentax_lens_corr_ifd(ifd)) {
        return pentax_lens_corr_value_name(tag, value);
    }
    if (is_pentax_awb_info_ifd(ifd)) {
        return pentax_awb_info_value_name(tag, value);
    }
    if (is_pentax_sr_info2_ifd(ifd)) {
        return pentax_sr_info2_value_name(tag, value);
    }
    if (is_pentax_lens_rec_ifd(ifd)) {
        return pentax_lens_rec_value_name(tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_pentax_lensinfo")
        || ifd_has_prefix(ifd, "makernote:pentax:lensinfo")) {
        return pentax_lens_info_value_name(tag, value);
    }
    if (is_pentax_time_info_ifd(ifd)) {
        return pentax_time_info_value_name(tag, value);
    }
    return "";
}

static bool
is_olympus_camera_settings_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_olympus_camerasettings_")
           || ifd == "makernote:olympus:camerasettings";
}

static bool
is_olympus_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_olympus0" || ifd_has_prefix(ifd, "mk_olympus_main")
           || ifd == "makernote:olympus:main";
}

static bool
is_olympus_focus_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_olympus_focusinfo_")
           || ifd == "makernote:olympus:focusinfo";
}

static bool
is_olympus_equipment_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_olympus_equipment_")
           || ifd == "makernote:olympus:equipment";
}

static bool
is_olympus_raw_development_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_olympus_rawdevelopment_")
           || ifd == "makernote:olympus:rawdevelopment";
}

static bool
is_olympus_raw_development2_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_olympus_rawdevelopment2_")
           || ifd == "makernote:olympus:rawdevelopment2";
}

static bool
is_olympus_image_processing_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_olympus_imageprocessing_")
           || ifd == "makernote:olympus:imageprocessing";
}

static const char*
olympus_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Manual";
    case 2U: return "Program";
    case 3U: return "Aperture-priority AE";
    case 4U: return "Shutter speed priority AE";
    case 5U: return "Program-shift";
    default: return "";
    }
}

static const char*
olympus_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 2U: return "Center-weighted average";
    case 3U: return "Spot";
    case 5U: return "ESP";
    case 261U: return "Pattern+AF";
    case 515U: return "Spot+Highlight control";
    case 1027U: return "Spot+Shadow control";
    default: return "";
    }
}

static const char*
olympus_macro_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    case 2U: return "Super Macro";
    default: return "";
    }
}

static const char*
olympus_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single AF";
    case 1U: return "Sequential shooting AF";
    case 2U: return "Continuous AF";
    case 3U: return "Multi AF";
    case 4U: return "Face Detect";
    case 10U: return "MF";
    default: return "";
    }
}

static const char*
olympus_focus_process_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "AF Not Used";
    case 1U: return "AF Used";
    default: return "";
    }
}

static const char*
olympus_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    case 2U: return "Fill-in";
    case 3U: return "On, Fill-in";
    case 4U: return "Red-eye";
    case 8U: return "Slow-sync";
    case 16U: return "Forced On";
    case 32U: return "2nd Curtain";
    default: return "";
    }
}

static const char*
olympus_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Auto (Keep Warm Color Off)";
    case 16U: return "7500K (Fine Weather with Shade)";
    case 17U: return "6000K (Cloudy)";
    case 18U: return "5300K (Fine Weather)";
    case 20U: return "3000K (Tungsten light)";
    case 21U: return "3600K (Tungsten light-like)";
    case 22U: return "Auto Setup";
    case 23U: return "5500K (Flash)";
    case 33U: return "6600K (Daylight fluorescent)";
    case 34U: return "4500K (Neutral white fluorescent)";
    case 35U: return "4000K (Cool white fluorescent)";
    case 36U: return "White Fluorescent";
    case 48U: return "3600K (Tungsten light-like)";
    default: return "";
    }
}

static const char*
olympus_scene_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Standard";
    case 6U: return "Auto";
    case 7U: return "Sport";
    case 8U: return "Portrait";
    case 9U: return "Landscape+Portrait";
    case 10U: return "Landscape";
    case 11U: return "Night Scene";
    case 12U: return "Self Portrait";
    case 13U: return "Panorama";
    case 14U: return "2 in 1";
    case 15U: return "Movie";
    case 16U: return "Landscape+Portrait";
    case 17U: return "Night+Portrait";
    case 18U: return "Indoor";
    case 19U: return "Fireworks";
    case 20U: return "Sunset";
    case 21U: return "Beauty Skin";
    case 22U: return "Macro";
    case 23U: return "Super Macro";
    case 24U: return "Food";
    case 25U: return "Documents";
    case 26U: return "Museum";
    case 27U: return "Shoot & Select";
    case 28U: return "Beach & Snow";
    case 30U: return "Candle";
    case 31U: return "Available Light";
    case 34U: return "Pet";
    case 35U: return "Underwater Wide1";
    case 36U: return "Underwater Macro";
    case 39U: return "High Key";
    case 40U: return "Digital Image Stabilization";
    case 42U: return "Beach";
    case 43U: return "Snow";
    case 44U: return "Underwater Wide2";
    case 45U: return "Low Key";
    case 46U: return "Children";
    case 48U: return "Nature Macro";
    case 57U: return "Bulb";
    case 65U: return "Multiple Exposure";
    case 66U: return "e-Portrait";
    case 142U: return "Hand-held Starlight";
    case 154U: return "HDR";
    case 197U: return "Panning";
    case 203U: return "Light Trails";
    case 204U: return "Backlight HDR";
    case 205U: return "Silent";
    case 206U: return "Multi Focus Shot";
    default: return "";
    }
}

static const char*
olympus_picture_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Vivid";
    case 2U: return "Natural";
    case 3U: return "Muted";
    case 4U: return "Portrait";
    case 5U: return "i-Enhance";
    case 6U: return "e-Portrait";
    case 7U: return "Color Creator";
    case 8U: return "Underwater";
    case 9U: return "Color Profile 1";
    case 10U: return "Color Profile 2";
    case 11U: return "Color Profile 3";
    case 12U: return "Monochrome Profile 1";
    case 13U: return "Monochrome Profile 2";
    case 14U: return "Monochrome Profile 3";
    case 17U: return "Art Mode";
    case 18U: return "Monochrome Profile 4";
    case 256U: return "Monotone";
    case 512U: return "Sepia";
    default: return "";
    }
}

static const char*
olympus_raw_dev_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Color Temperature";
    case 2U: return "Gray Point";
    default: return "";
    }
}

static const char*
olympus_multiple_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Live Composite";
    case 2U: return "On (2 frames)";
    case 3U: return "On (3 frames)";
    default: return "";
    }
}

static const char*
olympus_bw_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    case 6U: return "(none)";
    default: return "";
    }
}

static const char*
olympus_sharpness_name(uint64_t value) noexcept
{
    return value == 0U ? "Normal" : "";
}

static const char*
olympus_main_focus_mode_name(uint64_t value) noexcept
{
    return value == 0U ? "Auto" : "";
}

static const char*
olympus_main_scene_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Standard";
    case 6U: return "Landscape";
    case 7U: return "Night Scene";
    case 31U: return "Digital Image Stabilization";
    default: return "";
    }
}

static const char*
olympus_contrast_name(uint64_t value) noexcept
{
    return value == 1U ? "Normal" : "";
}

static const char*
olympus_white_balance_temperature_name(uint64_t value) noexcept
{
    return value == 0U ? "Auto" : "";
}

static const char*
olympus_color_space_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "sRGB";
    case 1U: return "Adobe RGB";
    case 2U: return "Pro Photo RGB";
    default: return "";
    }
}

static const char*
olympus_noise_reduction_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "(none)";
    case 8U: return "Auto";
    default: return "";
    }
}

static const char*
olympus_af_search_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Not Ready";
    case 1U: return "Ready";
    default: return "";
    }
}

static const char*
olympus_image_quality2_name(uint64_t value) noexcept
{
    return value == 3U ? "SHQ" : "";
}

static const char*
olympus_image_stabilization_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On, Mode 1";
    case 4U: return "On, Mode 4";
    default: return "";
    }
}

static const char*
olympus_raw_dev_engine_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "High Speed";
    case 1U: return "High Function";
    default: return "";
    }
}

static const char*
olympus_raw_dev_edit_status_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Original";
    case 1U: return "Edited (Landscape)";
    case 6U:
    case 8U: return "Edited (Portrait)";
    default: return "";
    }
}

static const char*
olympus_focus_info_af_point_name(uint64_t value) noexcept
{
    return value == 0U ? "Left (or n/a)" : "";
}

static const char*
olympus_external_flash_bounce_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Bounce or Off";
    case 1U: return "Direct";
    default: return "";
    }
}

static const char*
olympus_main_external_flash_bounce_name(uint64_t value) noexcept
{
    return value == 0U ? "No" : "";
}

static const char*
olympus_flash_none_name(uint64_t value) noexcept
{
    return value == 0U ? "None" : "";
}

static const char*
olympus_body_firmware_name(uint64_t value) noexcept
{
    switch (value) {
    case 4096U: return "1.000";
    case 4097U: return "1.001";
    case 4098U: return "1.002";
    case 4099U: return "1.003";
    case 4100U: return "1.004";
    case 4101U: return "1.005";
    default: return "";
    }
}

static const char*
olympus_lens_properties_name(uint64_t value) noexcept
{
    return value == 49472U ? "0xc140" : "";
}

static const char*
olympus_manometer_pressure_name(uint64_t value) noexcept
{
    return value == 0U ? "0 kPa" : "";
}

static const char*
olympus_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0202U: return olympus_macro_mode_name(value);
    case 0x0203U: return olympus_bw_mode_name(value);
    case 0x0302U: return off_on_name(value);
    case 0x0403U: return olympus_main_scene_mode_name(value);
    case 0x100AU: return value == 0U ? "Normal" : "";
    case 0x100BU: return olympus_main_focus_mode_name(value);
    case 0x100FU: return olympus_sharpness_name(value);
    case 0x1026U: return olympus_main_external_flash_bounce_name(value);
    case 0x1029U: return olympus_contrast_name(value);
    default: return "";
    }
}

static const char*
olympus_camera_settings_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0100U: return no_yes_name(value);
    case 0x0200U: return olympus_exposure_mode_name(value);
    case 0x0201U: return off_on_name(value);
    case 0x0202U: return olympus_metering_mode_name(value);
    case 0x0300U: return olympus_macro_mode_name(value);
    case 0x0301U: return olympus_focus_mode_name(value);
    case 0x0302U: return olympus_focus_process_name(value);
    case 0x0303U: return olympus_af_search_name(value);
    case 0x0306U: return off_on_name(value);
    case 0x0400U: return olympus_flash_mode_name(value);
    case 0x0403U: return off_on_name(value);
    case 0x0500U: return olympus_white_balance_name(value);
    case 0x0501U: return olympus_white_balance_temperature_name(value);
    case 0x0504U: return off_on_name(value);
    case 0x0507U: return olympus_color_space_name(value);
    case 0x0509U: return olympus_scene_mode_name(value);
    case 0x050AU: return olympus_noise_reduction_name(value);
    case 0x050BU: return off_on_name(value);
    case 0x050CU: return off_on_name(value);
    case 0x0520U: return olympus_picture_mode_name(value);
    case 0x0538U: return off_on_name(value);
    case 0x0603U: return olympus_image_quality2_name(value);
    case 0x0604U: return olympus_image_stabilization_name(value);
    case 0x0900U: return olympus_manometer_pressure_name(value);
    case 0x0902U: return off_on_name(value);
    default: return "";
    }
}

static const char*
olympus_raw_development_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0101U: return olympus_raw_dev_white_balance_name(value);
    case 0x0108U: return olympus_color_space_name(value);
    case 0x0109U: return olympus_raw_dev_engine_name(value);
    case 0x010AU: return olympus_noise_reduction_name(value);
    case 0x010BU: return olympus_raw_dev_edit_status_name(value);
    case 0x010CU: return olympus_noise_reduction_name(value);
    default: return "";
    }
}

static const char*
olympus_raw_development2_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x010CU: return olympus_picture_mode_name(value);
    default: return "";
    }
}

static const char*
olympus_image_processing_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x1010U: return olympus_noise_reduction_name(value);
    case 0x1011U: return off_on_name(value);
    case 0x1012U: return off_on_name(value);
    case 0x101CU: return olympus_multiple_exposure_mode_name(value);
    default: return "";
    }
}

static const char*
olympus_focus_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0209U: return off_on_name(value);
    case 0x0308U: return olympus_focus_info_af_point_name(value);
    case 0x1204U: return olympus_external_flash_bounce_name(value);
    case 0x1208U: return off_on_name(value);
    case 0x120AU: return off_on_name(value);
    default: return "";
    }
}

static const char*
olympus_equipment_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0104U: return olympus_body_firmware_name(value);
    case 0x020BU: return olympus_lens_properties_name(value);
    case 0x1000U: return olympus_flash_none_name(value);
    case 0x1001U: return olympus_flash_none_name(value);
    default: return "";
    }
}

static const char*
olympus_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_olympus_main_ifd(ifd)) {
        return olympus_main_value_name(tag, value);
    }
    if (is_olympus_camera_settings_ifd(ifd)) {
        return olympus_camera_settings_value_name(tag, value);
    }
    if (is_olympus_focus_info_ifd(ifd)) {
        return olympus_focus_info_value_name(tag, value);
    }
    if (is_olympus_equipment_ifd(ifd)) {
        return olympus_equipment_value_name(tag, value);
    }
    if (is_olympus_raw_development_ifd(ifd)) {
        return olympus_raw_development_value_name(tag, value);
    }
    if (is_olympus_raw_development2_ifd(ifd)) {
        return olympus_raw_development2_value_name(tag, value);
    }
    if (is_olympus_image_processing_ifd(ifd)) {
        return olympus_image_processing_value_name(tag, value);
    }
    return "";
}

static const char*
casio_record_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 2U: return "Program AE";
    case 6U: return "Best Shot";
    default: return "";
    }
}

static const char*
casio_recording_mode_name(uint64_t value) noexcept
{
    return value == 1U ? "Single Shutter" : "";
}

static const char*
casio_release_mode_name(uint64_t value) noexcept
{
    return value == 1U ? "Normal" : "";
}

static const char*
casio_quality_name(uint64_t value) noexcept
{
    switch (value) {
    case 2U: return "Normal";
    case 3U: return "Fine";
    default: return "";
    }
}

static const char*
casio_focus_mode_name(uint64_t value) noexcept
{
    return value == 3U ? "Single-Area Auto Focus" : "";
}

static const char*
casio_legacy_focus_mode_name(uint64_t value) noexcept
{
    return value == 3U ? "Auto" : "";
}

static const char*
casio_best_shot_mode_name(uint64_t value) noexcept
{
    return value == 0U ? "Off" : "";
}

static const char*
casio_auto_iso_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "On";
    case 2U: return "Off";
    default: return "";
    }
}

static const char*
casio_af_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Spot";
    case 2U: return "Multi";
    default: return "";
    }
}

static const char*
casio_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Daylight";
    case 6U: return "Fluorescent";
    case 12U: return "Flash";
    default: return "";
    }
}

static const char*
casio_legacy_white_balance_name(uint64_t value) noexcept
{
    return value == 1U ? "Auto" : "";
}

static const char*
casio_lighting_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 5U: return "Shadow Enhance Low";
    default: return "";
    }
}

static const char*
casio_image_stabilization_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 2U: return "Best Shot";
    default: return "";
    }
}

static const char*
casio_drive_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single Shot";
    case 1U: return "Continuous Shooting";
    default: return "";
    }
}

static const char*
casio_legacy_digital_zoom_name(uint64_t value) noexcept
{
    return value == 65536U ? "Off" : "";
}

static const char*
casio_special_effect_setting_name(uint64_t value) noexcept
{
    return value == 0U ? "Off" : "";
}

static const char*
casio_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return casio_recording_mode_name(value);
    case 0x0002U: return casio_quality_name(value);
    case 0x0003U: return casio_legacy_focus_mode_name(value);
    case 0x0005U: return value == 13U ? "Normal" : "";
    case 0x0007U: return casio_legacy_white_balance_name(value);
    case 0x000AU: return casio_legacy_digital_zoom_name(value);
    case 0x000BU: return value == 0U ? "Normal" : "";
    case 0x000CU: return value == 0U ? "Normal" : "";
    case 0x000DU: return value == 0U ? "Normal" : "";
    case 0x2012U: return casio_white_balance_name(value);
    case 0x3000U: return casio_record_mode_name(value);
    case 0x3001U: return casio_release_mode_name(value);
    case 0x3002U: return casio_quality_name(value);
    case 0x3003U: return casio_focus_mode_name(value);
    case 0x3007U: return casio_best_shot_mode_name(value);
    case 0x3008U: return casio_auto_iso_name(value);
    case 0x3009U: return casio_af_mode_name(value);
    case 0x3015U: return off_on_name(value);
    case 0x3016U: return off_on_name(value);
    case 0x3017U: return off_on_name(value);
    case 0x301BU: return value == 0U ? "Normal" : "";
    case 0x3020U: return casio_image_stabilization_name(value);
    case 0x302AU: return casio_lighting_mode_name(value);
    case 0x302BU: return off_on_name(value);
    case 0x3031U: return casio_special_effect_setting_name(value);
    case 0x3103U: return casio_drive_mode_name(value);
    default: return "";
    }
}

static bool
is_panasonic_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_panasonic0" || ifd_has_prefix(ifd, "mk_panasonic_main")
           || ifd == "makernote:panasonic:main";
}

static bool
is_panasonic_subdir_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_panasonic_subdir_")
           || ifd == "makernote:panasonic:subdir";
}

static const char*
panasonic_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Auto";
    case 2U: return "Daylight";
    case 3U: return "Cloudy";
    case 4U: return "Incandescent";
    case 5U: return "Manual";
    case 8U: return "Flash";
    case 10U: return "Black & White";
    case 11U: return "Manual 2";
    case 12U: return "Shade";
    case 13U: return "Kelvin";
    case 14U: return "Manual 3";
    case 15U: return "Manual 4";
    case 19U: return "Auto (cool)";
    default: return "";
    }
}

static const char*
panasonic_raw_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Cloudy";
    case 3U: return "Tungsten";
    case 4U: return "n/a";
    case 5U: return "Flash";
    case 6U: return "n/a";
    case 7U: return "n/a";
    case 8U: return "Custom#1";
    case 9U: return "Custom#2";
    case 10U: return "Custom#3";
    case 11U: return "Custom#4";
    case 12U: return "Shade";
    case 13U: return "Kelvin";
    case 16U: return "AWBc";
    default: return "";
    }
}

static const char*
panasonic_image_quality_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "TIFF";
    case 2U: return "High";
    case 3U: return "Normal";
    case 6U: return "Very High";
    case 7U: return "RAW";
    case 9U: return "Motion Picture";
    case 11U: return "Full HD Movie";
    case 12U: return "4k Movie";
    default: return "";
    }
}

static const char*
panasonic_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Auto";
    case 2U: return "Manual";
    case 4U: return "Auto, Focus button";
    case 5U: return "Auto, Continuous";
    case 6U: return "AF-S";
    case 7U: return "AF-C";
    case 8U: return "AF-F";
    default: return "";
    }
}

static const char*
panasonic_image_stabilization_name(uint64_t value) noexcept
{
    switch (value) {
    case 2U: return "On, Optical";
    case 3U: return "Off";
    case 4U: return "On, Mode 2";
    case 5U: return "On, Optical Panning";
    case 6U: return "On, Body-only";
    case 7U: return "On, Body-only Panning";
    case 9U: return "Dual IS";
    case 10U: return "Dual IS Panning";
    case 11U: return "Dual2 IS";
    case 12U: return "Dual2 IS Panning";
    default: return "";
    }
}

static const char*
panasonic_macro_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "On";
    case 2U: return "Off";
    case 0x101U: return "Tele-Macro";
    case 0x201U: return "Macro Zoom";
    default: return "";
    }
}

static const char*
panasonic_shooting_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Normal";
    case 2U: return "Portrait";
    case 3U: return "Scenery";
    case 4U: return "Sports";
    case 5U: return "Night Portrait";
    case 6U: return "Program";
    case 7U: return "Aperture Priority";
    case 8U: return "Shutter Priority";
    case 9U: return "Macro";
    case 10U: return "Spot";
    case 11U: return "Manual";
    case 12U: return "Movie Preview";
    case 13U: return "Panning";
    case 14U: return "Simple";
    case 15U: return "Color Effects";
    case 16U: return "Self Portrait";
    case 17U: return "Economy";
    case 18U: return "Fireworks";
    case 19U: return "Party";
    case 20U: return "Snow";
    case 21U: return "Night Scenery";
    case 22U: return "Food";
    case 23U: return "Baby";
    case 24U: return "Soft Skin";
    case 25U: return "Candlelight";
    case 26U: return "Starry Night";
    case 27U: return "High Sensitivity";
    case 28U: return "Panorama Assist";
    case 29U: return "Underwater";
    case 30U: return "Beach";
    case 31U: return "Aerial Photo";
    case 32U: return "Sunset";
    case 33U: return "Pet";
    case 34U: return "Intelligent ISO";
    case 35U: return "Clipboard";
    case 36U: return "High Speed Continuous Shooting";
    case 37U: return "Intelligent Auto";
    case 39U: return "Multi-aspect";
    case 41U: return "Transform";
    case 42U: return "Flash Burst";
    case 43U: return "Pin Hole";
    case 44U: return "Film Grain";
    case 45U: return "My Color";
    case 46U: return "Photo Frame";
    case 48U: return "Movie";
    case 51U: return "HDR";
    case 52U: return "Peripheral Defocus";
    case 55U: return "Handheld Night Shot";
    case 57U: return "3D";
    case 59U: return "Creative Control";
    case 60U: return "Intelligent Auto Plus";
    case 62U: return "Panorama";
    case 63U: return "Glass Through";
    default: return "";
    }
}

static const char*
panasonic_audio_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Yes";
    case 2U: return "No";
    case 3U: return "Stereo";
    default: return "";
    }
}

static const char*
panasonic_video_frame_rate_name(uint64_t value) noexcept
{
    return value == 0U ? "n/a" : "";
}

static const char*
panasonic_color_effect_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Off";
    case 2U: return "Warm";
    case 3U: return "Cool";
    case 4U: return "Black & White";
    case 5U: return "Sepia";
    case 6U: return "Happy";
    case 8U: return "Vivid";
    default: return "";
    }
}

static const char*
panasonic_burst_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    case 2U: return "Auto Exposure Bracketing (AEB)";
    case 3U: return "Focus Bracketing";
    case 4U: return "Unlimited";
    case 8U: return "White Balance Bracketing";
    case 17U: return "On (with flash)";
    case 18U: return "Aperture Bracketing";
    default: return "";
    }
}

static const char*
panasonic_contrast_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    default: return "";
    }
}

static const char*
panasonic_noise_reduction_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Low (-1)";
    case 2U: return "High (+1)";
    case 3U: return "Lowest (-2)";
    case 4U: return "Highest (+2)";
    case 5U: return "+5";
    case 6U: return "+6";
    case 65531U: return "-5";
    case 65532U: return "-4";
    case 65533U: return "-3";
    case 65534U: return "-2";
    case 65535U: return "-1";
    default: return "";
    }
}

static const char*
panasonic_self_timer_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off (0)";
    case 1U: return "Off";
    case 2U: return "10 s";
    case 3U: return "2 s";
    case 4U: return "10 s / 3 pictures";
    case 258U: return "2 s after shutter pressed";
    case 266U: return "10 s after shutter pressed";
    case 778U: return "3 photos after 10 s";
    default: return "";
    }
}

static const char*
panasonic_rotation_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Horizontal (normal)";
    case 3U: return "Rotate 180";
    case 6U: return "Rotate 90 CW";
    case 8U: return "Rotate 270 CW";
    default: return "";
    }
}

static const char*
panasonic_af_assist_lamp_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Fired";
    case 2U: return "Enabled but Not Used";
    case 3U: return "Disabled but Required";
    case 4U: return "Disabled and Not Required";
    default: return "";
    }
}

static const char*
panasonic_color_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Natural";
    case 2U: return "Vivid";
    default: return "";
    }
}

static const char*
panasonic_conversion_lens_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Off";
    case 2U: return "Wide";
    case 3U: return "Telephoto";
    case 4U: return "Macro";
    default: return "";
    }
}

static const char*
panasonic_travel_day_name(uint64_t value) noexcept
{
    return value == 65535U ? "n/a" : "";
}

static const char*
panasonic_battery_level_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Full";
    case 2U: return "Medium";
    case 3U: return "Low";
    case 4U: return "Near Empty";
    case 7U: return "Near Full";
    case 8U: return "Medium Low";
    case 256U: return "n/a";
    default: return "";
    }
}

static const char*
panasonic_world_time_location_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Home";
    case 2U: return "Destination";
    default: return "";
    }
}

static const char*
panasonic_text_stamp_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Off";
    case 2U: return "On";
    default: return "";
    }
}

static const char*
panasonic_optical_zoom_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Standard";
    case 2U: return "Extended";
    default: return "";
    }
}

static const char*
panasonic_film_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 1U: return "Standard (color)";
    case 2U: return "Dynamic (color)";
    case 3U: return "Nature (color)";
    case 4U: return "Smooth (color)";
    case 5U: return "Standard (B&W)";
    case 6U: return "Dynamic (B&W)";
    case 7U: return "Smooth (B&W)";
    case 10U: return "Nostalgic";
    case 11U: return "Vibrant";
    default: return "";
    }
}

static const char*
panasonic_flash_curtain_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 1U: return "1st";
    case 2U: return "2nd";
    default: return "";
    }
}

static const char*
panasonic_intelligent_exposure_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Low";
    case 2U: return "Standard";
    case 3U: return "High";
    default: return "";
    }
}

static const char*
panasonic_flash_warning_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "No";
    case 1U: return "Yes (flash required but disabled)";
    default: return "";
    }
}

static const char*
panasonic_multi_exposure_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 1U: return "Off";
    case 2U: return "On";
    default: return "";
    }
}

static const char*
panasonic_video_burst_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x01U: return "Off";
    case 0x04U: return "Post Focus";
    case 0x18U: return "4K Burst";
    case 0x28U: return "4K Burst (Start/Stop)";
    case 0x48U: return "4K Pre-burst";
    case 0x108U: return "Loop Recording";
    case 0x408U: return "Focus Stacking";
    case 0x810U: return "6K Burst";
    case 0x820U: return "6K Burst (Start/Stop)";
    case 0x1001U: return "High Resolution Mode";
    default: return "";
    }
}

static const char*
panasonic_long_exposure_nr_used_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "No";
    case 1U: return "Yes";
    default: return "";
    }
}

static const char*
panasonic_scene_mode_name(uint64_t value) noexcept
{
    if (value == 0U) {
        return "Off";
    }
    return panasonic_shooting_mode_name(value);
}

static const char*
panasonic_dark_focus_environment_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "No";
    case 2U: return "Yes";
    default: return "";
    }
}

static const char*
panasonic_intelligent_resolution_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Low";
    case 2U: return "Standard";
    case 3U: return "High";
    case 4U: return "Extended";
    default: return "";
    }
}

static const char*
panasonic_highlight_warning_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Disabled";
    case 1U: return "No";
    case 2U: return "Yes";
    default: return "";
    }
}

static const char*
panasonic_normal_name(uint64_t value) noexcept
{
    return value == 0U ? "Normal" : "";
}

static const char*
panasonic_clear_retouch_name(uint64_t value) noexcept
{
    return value == 0U ? "Off" : "";
}

static const char*
panasonic_jpeg_quality_name(uint64_t value) noexcept
{
    switch (value) {
    case 2U: return "High";
    case 3U: return "Standard";
    default: return "";
    }
}

static const char*
panasonic_bracket_settings_name(uint64_t value) noexcept
{
    return value == 0U ? "No Bracket" : "";
}

static const char*
panasonic_program_iso_name(uint64_t value) noexcept
{
    switch (value) {
    case 65534U: return "Intelligent ISO";
    case 65535U: return "n/a";
    default: return "";
    }
}

static const char*
panasonic_long_exposure_noise_reduction_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Off";
    case 2U: return "On";
    default: return "";
    }
}

static const char*
panasonic_photo_style_name(uint64_t value) noexcept
{
    return value == 1U ? "Standard or Custom" : "";
}

static const char*
panasonic_intelligent_d_range_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Low";
    case 2U: return "Standard";
    case 3U: return "High";
    default: return "";
    }
}

static const char*
panasonic_camera_orientation_name(uint64_t value) noexcept
{
    return value == 0U ? "Normal" : "";
}

static const char*
panasonic_hdr_name(uint64_t value) noexcept
{
    return value == 0U ? "Off" : "";
}

static const char*
panasonic_shutter_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Mechanical";
    case 2U: return "Hybrid";
    default: return "";
    }
}

static const char*
panasonic_monochrome_filter_effect_name(uint64_t value) noexcept
{
    return value == 0U ? "Off" : "";
}

static const char*
panasonic_video_burst_resolution_name(uint64_t value) noexcept
{
    return value == 1U ? "Off or 4K" : "";
}

static const char*
panasonic_video_preburst_name(uint64_t value) noexcept
{
    return value == 0U ? "No" : "";
}

static const char*
panasonic_diffraction_correction_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Auto";
    default: return "";
    }
}

static const char*
panasonic_sensor_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Multi-aspect";
    case 1U: return "Standard";
    default: return "";
    }
}

static const char*
panasonic_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return panasonic_image_quality_name(value);
    case 0x0003U: return panasonic_white_balance_name(value);
    case 0x0007U: return panasonic_focus_mode_name(value);
    case 0x001AU: return panasonic_image_stabilization_name(value);
    case 0x001CU: return panasonic_macro_mode_name(value);
    case 0x001FU: return panasonic_shooting_mode_name(value);
    case 0x0020U: return panasonic_audio_name(value);
    case 0x0027U: return panasonic_video_frame_rate_name(value);
    case 0x0028U: return panasonic_color_effect_name(value);
    case 0x002AU: return panasonic_burst_mode_name(value);
    case 0x002CU: return panasonic_contrast_mode_name(value);
    case 0x002DU: return panasonic_noise_reduction_name(value);
    case 0x002EU: return panasonic_self_timer_name(value);
    case 0x0030U: return panasonic_rotation_name(value);
    case 0x0031U: return panasonic_af_assist_lamp_name(value);
    case 0x0032U: return panasonic_color_mode_name(value);
    case 0x0034U: return panasonic_optical_zoom_mode_name(value);
    case 0x0035U: return panasonic_conversion_lens_name(value);
    case 0x0036U: return panasonic_travel_day_name(value);
    case 0x0038U: return panasonic_battery_level_name(value);
    case 0x0039U: return panasonic_normal_name(value);
    case 0x003AU: return panasonic_world_time_location_name(value);
    case 0x003BU: return panasonic_text_stamp_name(value);
    case 0x003CU: return panasonic_program_iso_name(value);
    case 0x003EU: return panasonic_text_stamp_name(value);
    case 0x0040U: return panasonic_normal_name(value);
    case 0x0041U: return panasonic_normal_name(value);
    case 0x0042U: return panasonic_film_mode_name(value);
    case 0x0043U: return panasonic_jpeg_quality_name(value);
    case 0x0045U: return panasonic_bracket_settings_name(value);
    case 0x0048U: return panasonic_flash_curtain_name(value);
    case 0x0049U: return panasonic_long_exposure_noise_reduction_name(value);
    case 0x005DU: return panasonic_intelligent_exposure_name(value);
    case 0x0062U: return panasonic_flash_warning_name(value);
    case 0x0070U: return panasonic_intelligent_resolution_name(value);
    case 0x0079U: return panasonic_intelligent_d_range_name(value);
    case 0x007CU: return panasonic_clear_retouch_name(value);
    case 0x0089U: return panasonic_photo_style_name(value);
    case 0x008AU: return off_on_name(value);
    case 0x008FU: return panasonic_camera_orientation_name(value);
    case 0x0093U: return off_on_name(value);
    case 0x0096U: return off_on_name(value);
    case 0x009EU: return panasonic_hdr_name(value);
    case 0x009FU: return panasonic_shutter_type_name(value);
    case 0x00ABU: return off_on_name(value);
    case 0x00ACU: return panasonic_monochrome_filter_effect_name(value);
    case 0x00B4U: return panasonic_multi_exposure_name(value);
    case 0x00B3U: return panasonic_video_burst_resolution_name(value);
    case 0x00B9U: return off_on_name(value);
    case 0x00BBU: return panasonic_video_burst_mode_name(value);
    case 0x00BCU: return panasonic_diffraction_correction_name(value);
    case 0x00BEU: return panasonic_long_exposure_nr_used_name(value);
    case 0x00C1U: return panasonic_video_preburst_name(value);
    case 0x00CAU: return panasonic_sensor_type_name(value);
    case 0x00D2U: return off_on_name(value);
    case 0x8001U: return panasonic_scene_mode_name(value);
    case 0x8002U: return panasonic_highlight_warning_name(value);
    case 0x8003U: return panasonic_dark_focus_environment_name(value);
    case 0x8008U: return panasonic_text_stamp_name(value);
    case 0x8009U: return panasonic_text_stamp_name(value);
    default: return "";
    }
}

static const char*
panasonic_subdir_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x3033U: return panasonic_raw_white_balance_name(value);
    default: return "";
    }
}

static const char*
panasonic_value_name(std::string_view ifd, uint16_t tag,
                     uint64_t value) noexcept
{
    if (is_panasonic_main_ifd(ifd)) {
        return panasonic_main_value_name(tag, value);
    }
    if (is_panasonic_subdir_ifd(ifd)) {
        return panasonic_subdir_value_name(tag, value);
    }
    return "";
}

static bool
is_phaseone_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_phaseone0" || ifd_has_prefix(ifd, "mk_phaseone_main")
           || ifd == "makernote:phaseone:main";
}

static const char*
phaseone_camera_orientation_name(uint64_t value) noexcept
{
    switch (value & 0x03U) {
    case 0U: return "Horizontal (normal)";
    case 1U: return "Rotate 90 CW";
    case 2U: return "Rotate 270 CW";
    case 3U: return "Rotate 180";
    default: return "";
    }
}

static const char*
phaseone_raw_format_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Uncompressed";
    case 1U: return "RAW 1";
    case 2U: return "RAW 2";
    case 3U: return "IIQ L";
    case 5U: return "IIQ S";
    case 6U: return "IIQ Sv2";
    case 8U: return "IIQ L16";
    default: return "";
    }
}

static const char*
phaseone_sequence_kind_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Bracketing: Shutter Speed";
    case 1U: return "Bracketing: Aperture";
    case 2U: return "Bracketing: ISO";
    case 3U: return "Hyperfocal";
    case 4U: return "Time Lapse";
    case 5U: return "HDR";
    case 6U: return "Focus Stacking";
    default: return "";
    }
}

static const char*
phaseone_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (!is_phaseone_main_ifd(ifd)) {
        return "";
    }
    switch (tag) {
    case 0x0100U: return phaseone_camera_orientation_name(value);
    case 0x010EU: return phaseone_raw_format_name(value);
    case 0x0263U: return phaseone_sequence_kind_name(value);
    default: return "";
    }
}

static bool
is_kodak_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_kodak0" || ifd_has_prefix(ifd, "mk_kodak_main")
           || ifd == "makernote:kodak:main";
}

static bool
is_kodak_type5_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_kodak_type5_")
           || ifd == "makernote:kodak:type5";
}

static bool
is_kodak_type11_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_kodak_type11_")
           || ifd == "makernote:kodak:type11";
}

static bool
is_kodak_subifd0_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_kodak_subifd0_")
           || ifd == "makernote:kodak:subifd0";
}

static bool
is_kodak_subifd2_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_kodak_subifd2_")
           || ifd == "makernote:kodak:subifd2";
}

static bool
is_kodak_kdc_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_kodak_kdc_ifd_")
           || ifd == "makernote:kodak:kdc_ifd";
}

static const char*
kodak_quality_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Fine";
    case 2U: return "Normal";
    default: return "";
    }
}

static const char*
kodak_shutter_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 8U: return "Aperture Priority";
    case 32U: return "Manual?";
    default: return "";
    }
}

static const char*
kodak_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Multi-segment";
    case 1U: return "Center-weighted average";
    case 2U: return "Spot";
    default: return "";
    }
}

static const char*
kodak_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 2U: return "Macro";
    default: return "";
    }
}

static const char*
kodak_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Flash?";
    case 2U: return "Tungsten";
    case 3U: return "Daylight";
    default: return "";
    }
}

static const char*
kodak_type5_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Daylight";
    case 2U: return "Flash";
    case 3U: return "Tungsten";
    default: return "";
    }
}

static const char*
kodak_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x00U: return "Auto";
    case 0x01U: return "Fill Flash";
    case 0x02U: return "Off";
    case 0x03U: return "Red-Eye";
    case 0x10U: return "Fill Flash";
    case 0x20U: return "Off";
    case 0x40U: return "Red-Eye?";
    default: return "";
    }
}

static const char*
kodak_color_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x01U: return "B&W";
    case 0x02U: return "Sepia";
    case 0x03U: return "B&W Yellow Filter";
    case 0x04U: return "B&W Red Filter";
    case 0x20U: return "Saturated Color";
    case 0x40U: return "Neutral Color";
    case 0x100U: return "Saturated Color";
    case 0x200U: return "Neutral Color";
    case 0x2000U: return "B&W";
    case 0x4000U: return "Sepia";
    default: return "";
    }
}

static const char*
kodak_scene_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Sport";
    case 3U: return "Portrait";
    case 4U: return "Landscape";
    case 6U: return "Beach";
    case 7U: return "Night Portrait";
    case 8U: return "Night Landscape";
    case 9U: return "Snow";
    case 10U: return "Text";
    case 11U: return "Fireworks";
    case 12U: return "Macro";
    case 13U: return "Museum";
    case 16U: return "Children";
    case 17U: return "Program";
    case 18U: return "Aperture Priority";
    case 19U: return "Shutter Priority";
    case 20U: return "Manual";
    case 25U: return "Back Light";
    case 28U: return "Candlelight";
    case 29U: return "Sunset";
    case 31U: return "Panorama Left-right";
    case 32U: return "Panorama Right-left";
    case 33U: return "Smart Scene";
    case 34U: return "High ISO";
    default: return "";
    }
}

static const char*
kodak_scene_mode_used_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program";
    case 2U: return "Aperture Priority";
    case 3U: return "Shutter Priority";
    case 4U: return "Manual";
    case 5U: return "Portrait";
    case 6U: return "Sport";
    case 7U: return "Children";
    case 8U: return "Museum";
    case 10U: return "High ISO";
    case 11U: return "Text";
    case 12U: return "Macro";
    case 13U: return "Back Light";
    case 16U: return "Landscape";
    case 17U: return "Night Landscape";
    case 18U: return "Night Portrait";
    case 19U: return "Snow";
    case 20U: return "Beach";
    case 21U: return "Fireworks";
    case 22U: return "Sunset";
    default: return "";
    }
}

static const char*
kodak_kdc_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Fluorescent";
    case 2U: return "Tungsten";
    case 3U: return "Daylight";
    case 6U: return "Shade";
    default: return "";
    }
}

static const char*
kodak_picture_effect_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "None";
    case 3U: return "Monochrome";
    case 9U: return "Kodachrome";
    default: return "";
    }
}

static const char*
kodak_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0009U: return kodak_quality_name(value);
    case 0x000AU: return off_on_name(value);
    case 0x001BU: return kodak_shutter_mode_name(value);
    case 0x001CU: return kodak_metering_mode_name(value);
    case 0x0038U: return kodak_focus_mode_name(value);
    case 0x0040U: return kodak_white_balance_name(value);
    case 0x005CU: return kodak_flash_mode_name(value);
    case 0x005DU: return no_yes_name(value);
    case 0x005EU: return value == 0U ? "Auto" : "";
    case 0x0066U: return kodak_color_mode_name(value);
    default: return "";
    }
}

static const char*
kodak_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_kodak_main_ifd(ifd)) {
        return kodak_main_value_name(tag, value);
    }
    if (is_kodak_type5_ifd(ifd)) {
        switch (tag) {
        case 0x001AU: return kodak_type5_white_balance_name(value);
        case 0x0027U: return kodak_flash_mode_name(value);
        case 0x002BU: return value == 0U ? "On" : value == 1U ? "Off" : "";
        default: return "";
        }
    }
    if (is_kodak_type11_ifd(ifd) && tag == 0x0203U) {
        return kodak_picture_effect_name(value);
    }
    if (is_kodak_subifd0_ifd(ifd) && tag == 0xFA02U) {
        return kodak_scene_mode_name(value);
    }
    if (is_kodak_subifd2_ifd(ifd) && (tag == 0x6002U || tag == 0xF002U)) {
        return kodak_scene_mode_used_name(value);
    }
    if (is_kodak_kdc_ifd(ifd) && tag == 0xFA0DU) {
        return kodak_kdc_white_balance_name(value);
    }
    return "";
}

static bool
is_minolta_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_minolta0" || ifd_has_prefix(ifd, "mk_minolta_main")
           || ifd == "makernote:minolta:main";
}

static bool
is_minolta_camera_settings_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_minolta_camerasettings_")
           || ifd == "makernote:minolta:camerasettings";
}

static bool
is_minolta_camera_settings5d_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_minolta_camerasettings5d_")
           || ifd == "makernote:minolta:camerasettings5d";
}

static bool
is_minolta_camera_settings7d_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_minolta_camerasettings7d_")
           || ifd == "makernote:minolta:camerasettings7d";
}

static bool
is_minolta_camera_settingsa100_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_minolta_camerasettingsa100_")
           || ifd == "makernote:minolta:camerasettingsa100";
}

static const char*
minolta_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program";
    case 1U: return "Aperture Priority";
    case 2U: return "Shutter Priority";
    case 3U: return "Manual";
    default: return "";
    }
}

static const char*
minolta_exposure_mode7d_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program";
    case 1U: return "Aperture Priority";
    case 2U: return "Shutter Priority";
    case 3U: return "Manual";
    case 4U: return "Auto";
    case 5U: return "Program-shift A";
    case 6U: return "Program-shift S";
    default: return "";
    }
}

static const char*
minolta_exposure_mode_a100_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program";
    case 1U: return "Aperture Priority";
    case 2U: return "Shutter Priority";
    case 3U: return "Manual";
    case 4U: return "Auto";
    case 5U: return "Program Shift A";
    case 6U: return "Program Shift S";
    case 0x1013U: return "Portrait";
    case 0x1023U: return "Sports";
    case 0x1033U: return "Sunset";
    case 0x1043U: return "Night View/Portrait";
    case 0x1053U: return "Landscape";
    case 0x1083U: return "Macro";
    default: return "";
    }
}

static const char*
minolta_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Fill flash";
    case 1U: return "Red-eye reduction";
    case 2U: return "Rear flash sync";
    case 3U: return "Wireless";
    case 4U: return "Off?";
    default: return "";
    }
}

static const char*
minolta_flash_mode_basic_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Red-eye reduction";
    case 2U: return "Rear flash sync";
    default: return "";
    }
}

static const char*
minolta_flash_mode_a100_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 2U: return "Rear Sync";
    case 3U: return "Wireless";
    case 4U: return "Fill Flash";
    default: return "";
    }
}

static const char*
minolta_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Cloudy";
    case 3U: return "Tungsten";
    case 5U: return "Custom";
    case 7U: return "Fluorescent";
    case 8U: return "Fluorescent 2";
    case 11U: return "Custom 2";
    case 12U: return "Custom 3";
    case 0x0800000U: return "Auto";
    case 0x1800000U: return "Daylight";
    case 0x2800000U: return "Cloudy";
    case 0x3800000U: return "Tungsten";
    case 0x4800000U: return "Flash";
    case 0x5800000U: return "Fluorescent";
    case 0x6800000U: return "Shade";
    case 0x7800000U: return "Custom1";
    case 0x8800000U: return "Custom2";
    case 0x9800000U: return "Custom3";
    default: return "";
    }
}

static const char*
minolta_white_balance_dslr_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Cloudy";
    case 3U: return "Shade";
    case 4U: return "Tungsten";
    case 5U: return "Fluorescent";
    case 6U: return "Flash";
    case 0x100U: return "Kelvin";
    case 0x200U: return "Manual";
    default: return "";
    }
}

static const char*
minolta_image_size_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Full";
    case 1U: return "1600x1200";
    case 2U: return "1280x960";
    case 3U: return "640x480";
    case 6U: return "2080x1560";
    case 7U: return "2560x1920";
    case 8U: return "3264x2176";
    default: return "";
    }
}

static const char*
minolta_image_size_basic_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Large";
    case 1U: return "Medium";
    case 2U: return "Small";
    default: return "";
    }
}

static const char*
minolta_quality_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Raw";
    case 1U: return "Super Fine";
    case 2U: return "Fine";
    case 3U: return "Standard";
    case 4U: return "Economy";
    case 5U: return "Extra Fine";
    case 16U: return "Fine";
    case 32U: return "Normal";
    case 34U: return "RAW+JPEG";
    case 48U: return "Economy";
    default: return "";
    }
}

static const char*
minolta_drive_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single";
    case 1U: return "Continuous";
    case 2U: return "Self-timer";
    case 4U: return "Bracketing";
    case 5U: return "Interval";
    case 6U: return "UHS continuous";
    case 7U: return "HS continuous";
    default: return "";
    }
}

static const char*
minolta_drive_mode_a100_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single Frame";
    case 1U: return "Continuous";
    case 2U: return "Self-timer";
    case 3U: return "Continuous Bracketing";
    case 4U: return "Single-Frame Bracketing";
    case 5U: return "White Balance Bracketing";
    default: return "";
    }
}

static const char*
minolta_drive_mode2_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x000U: return "Self-timer 10 sec";
    case 0x001U: return "Continuous";
    case 0x302U: return "Single-frame Bracketing Low";
    case 0x702U: return "Single-frame Bracketing High";
    case 0x303U: return "Continous Bracketing Low";
    case 0x703U: return "Continuous Bracketing High";
    case 0x004U: return "Self-timer 2 sec";
    case 0x005U: return "Single Frame";
    case 0x008U: return "White Balance Bracketing Low";
    case 0x009U: return "White Balance Bracketing High";
    default: return "";
    }
}

static const char*
minolta_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Multi-segment";
    case 1U: return "Center-weighted average";
    case 2U: return "Spot";
    default: return "";
    }
}

static const char*
minolta_macro_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    default: return "";
    }
}

static const char*
minolta_sharpness_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Hard";
    case 1U: return "Normal";
    case 2U: return "Soft";
    default: return "";
    }
}

static const char*
minolta_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "AF";
    case 1U: return "MF";
    default: return "";
    }
}

static const char*
minolta_focus_mode7d_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "AF-S";
    case 1U: return "AF-C";
    case 3U: return "Manual";
    case 4U: return "AF-A";
    default: return "";
    }
}

static const char*
minolta_focus_mode_a100_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "AF-S";
    case 1U: return "AF-C";
    case 4U: return "AF-A";
    case 5U: return "Manual";
    case 6U: return "DMF";
    default: return "";
    }
}

static const char*
minolta_focus_area_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Wide Focus (normal)";
    case 1U: return "Spot Focus";
    default: return "";
    }
}

static const char*
minolta_af_area_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Wide";
    case 1U: return "Local";
    case 2U: return "Spot";
    default: return "";
    }
}

static const char*
minolta_color_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Natural color";
    case 1U: return "Black & White";
    case 2U: return "Vivid color";
    case 3U: return "Solarization";
    case 4U: return "Adobe RGB";
    case 5U: return "Sepia";
    case 9U: return "Natural";
    case 12U: return "Portrait";
    case 13U: return "Natural sRGB";
    case 14U: return "Natural+ sRGB";
    case 15U: return "Landscape";
    case 16U: return "Evening";
    case 17U: return "Night Scene";
    case 18U: return "Night Portrait";
    case 0x84U: return "Embed Adobe RGB";
    default: return "";
    }
}

static const char*
minolta_color_mode_a100_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Vivid";
    case 2U: return "Portrait";
    case 3U: return "Landscape";
    case 4U: return "Sunset";
    case 5U: return "Night Scene";
    case 7U: return "B&W";
    case 8U: return "Adobe RGB";
    default: return "";
    }
}

static const char*
minolta_color_space_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Natural sRGB";
    case 1U: return "Natural+ sRGB";
    case 2U: return "Monochrome";
    case 4U: return "Adobe RGB (ICC)";
    case 5U: return "Adobe RGB";
    default: return "";
    }
}

static const char*
minolta_color_space_a100_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "sRGB";
    case 2U: return "B&W";
    case 5U: return "Adobe RGB";
    default: return "";
    }
}

static const char*
minolta_scene_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Portrait";
    case 2U: return "Text";
    case 3U: return "Night Scene";
    case 4U: return "Sunset";
    case 5U: return "Sports";
    case 6U: return "Landscape";
    case 7U: return "Night Portrait";
    case 8U: return "Macro";
    case 9U: return "Super Macro";
    case 16U: return "Auto";
    case 17U: return "Night View/Portrait";
    case 18U: return "Sweep Panorama";
    case 19U: return "Handheld Night Shot";
    case 20U: return "Anti Motion Blur";
    case 21U: return "Cont. Priority AE";
    case 22U: return "Auto+";
    case 23U: return "3D Sweep Panorama";
    case 24U: return "Superior Auto";
    case 25U: return "High Sensitivity";
    case 26U: return "Fireworks";
    case 27U: return "Food";
    case 28U: return "Pet";
    case 33U: return "HDR";
    case 0xFFFFU: return "n/a";
    default: return "";
    }
}

static const char*
minolta_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0100U: return minolta_scene_mode_name(value);
    case 0x0101U: return minolta_color_mode_name(value);
    case 0x0102U: return minolta_quality_name(value);
    case 0x0107U: return value == 1U ? "Off" : value == 5U ? "On" : "";
    case 0x0109U: return off_on_name(value);
    case 0x010AU:
        return value == 0U   ? "ISO Setting Used"
               : value == 1U ? "High Key"
               : value == 2U ? "Low Key"
                             : "";
    case 0x0113U: return off_on_name(value);
    case 0x0115U:
        switch (value) {
        case 0x00U: return "Auto";
        case 0x01U: return "Color Temperature/Color Filter";
        case 0x10U: return "Daylight";
        case 0x20U: return "Cloudy";
        case 0x30U: return "Shade";
        case 0x40U: return "Tungsten";
        case 0x50U: return "Flash";
        case 0x60U: return "Fluorescent";
        default: return "";
        }
    default: return "";
    }
}

static const char*
minolta_camera_settings_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return minolta_exposure_mode_name(value);
    case 0x0002U: return minolta_flash_mode_name(value);
    case 0x0003U: return minolta_white_balance_name(value);
    case 0x0004U: return minolta_image_size_name(value);
    case 0x0005U: return minolta_quality_name(value);
    case 0x0006U: return minolta_drive_mode_name(value);
    case 0x0007U: return minolta_metering_mode_name(value);
    case 0x000BU: return minolta_macro_mode_name(value);
    case 0x0014U: return no_yes_name(value);
    case 0x0021U: return minolta_sharpness_name(value);
    case 0x0024U:
        switch (value) {
        case 0U: return "100";
        case 1U: return "200";
        case 2U: return "400";
        case 3U: return "800";
        case 4U: return "Auto";
        case 5U: return "64";
        default: return "";
        }
    case 0x0028U: return minolta_color_mode_name(value);
    case 0x002BU: return value == 0U ? "No" : value == 1U ? "Fired" : "";
    case 0x0030U: return minolta_focus_mode_name(value);
    case 0x0031U: return minolta_focus_area_name(value);
    case 0x003FU:
        switch (value) {
        case 0U: return "ADI (Advanced Distance Integration)";
        case 1U: return "Pre-flash TTL";
        case 2U: return "Manual flash control";
        default: return "";
        }
    default: return "";
    }
}

static const char*
minolta_camera_settings5d_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x000AU: return minolta_exposure_mode_name(value);
    case 0x000CU: return minolta_image_size_basic_name(value);
    case 0x000DU: return minolta_quality_name(value);
    case 0x000EU: return minolta_white_balance_dslr_name(value);
    case 0x001FU:
        return value == 0U ? "Did not fire" : value == 1U ? "Fired" : "";
    case 0x0020U: return minolta_flash_mode_basic_name(value);
    case 0x0025U: return minolta_metering_mode_name(value);
    case 0x0026U:
        switch (value) {
        case 0U: return "Auto";
        case 1U: return "100";
        case 3U: return "200";
        case 4U: return "400";
        case 5U: return "800";
        case 6U: return "1600";
        case 7U: return "3200";
        case 8U: return "200 (Zone Matching High)";
        case 10U: return "80 (Zone Matching Low)";
        default: return "";
        }
    case 0x002FU: return minolta_color_space_name(value);
    default: return "";
    }
}

static const char*
minolta_camera_settings7d_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0000U: return minolta_exposure_mode7d_name(value);
    case 0x0002U: return minolta_image_size_basic_name(value);
    case 0x0003U: return minolta_quality_name(value);
    case 0x0004U: return minolta_white_balance_dslr_name(value);
    case 0x000EU: return minolta_focus_mode7d_name(value);
    case 0x0015U: return off_on_name(value);
    case 0x0016U: return minolta_flash_mode_basic_name(value);
    case 0x001CU: return minolta_camera_settings5d_value_name(0x0026U, value);
    case 0x0025U: return minolta_color_space_name(value);
    default: return "";
    }
}

static const char*
minolta_camera_settingsa100_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0000U: return minolta_exposure_mode_a100_name(value);
    case 0x000AU: return minolta_drive_mode2_name(value);
    case 0x000BU: return minolta_white_balance_dslr_name(value);
    case 0x000CU: return minolta_focus_mode_a100_name(value);
    case 0x000EU: return minolta_af_area_mode_name(value);
    case 0x000FU: return minolta_flash_mode_a100_name(value);
    case 0x0012U: return minolta_metering_mode_name(value);
    case 0x0016U: return minolta_color_mode_a100_name(value);
    case 0x0017U: return minolta_color_space_a100_name(value);
    case 0x001CU:
        return value == 0U   ? "ADI (Advanced Distance Integration)"
               : value == 1U ? "Pre-flash TTL"
                             : "";
    case 0x001EU: return minolta_drive_mode_a100_name(value);
    default: return "";
    }
}

static const char*
minolta_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_minolta_main_ifd(ifd)) {
        return minolta_main_value_name(tag, value);
    }
    if (is_minolta_camera_settings_ifd(ifd)) {
        return minolta_camera_settings_value_name(tag, value);
    }
    if (is_minolta_camera_settings5d_ifd(ifd)) {
        return minolta_camera_settings5d_value_name(tag, value);
    }
    if (is_minolta_camera_settings7d_ifd(ifd)) {
        return minolta_camera_settings7d_value_name(tag, value);
    }
    if (is_minolta_camera_settingsa100_ifd(ifd)) {
        return minolta_camera_settingsa100_value_name(tag, value);
    }
    return "";
}

static bool
is_sigma_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_sigma0" || ifd_has_prefix(ifd, "mk_sigma_main")
           || ifd == "makernote:sigma:main";
}

static const char*
sigma_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 'A': return "Aperture-priority AE";
    case 'M': return "Manual";
    case 'P': return "Program AE";
    case 'S': return "Shutter speed priority AE";
    default: return "";
    }
}

static const char*
sigma_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 'A': return "Average";
    case 'C': return "Center-weighted average";
    case '8': return "Multi-segment";
    case 8U: return "Multi-segment";
    default: return "";
    }
}

static const char*
sigma_color_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 1U: return "Sepia";
    case 2U: return "B&W";
    case 3U: return "Standard";
    case 4U: return "Vivid";
    case 5U: return "Neutral";
    case 6U: return "Portrait";
    case 7U: return "Landscape";
    case 8U: return "FOV Classic Blue";
    default: return "";
    }
}

static const char*
sigma_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (!is_sigma_main_ifd(ifd)) {
        return "";
    }
    switch (tag) {
    case 0x0008U: return sigma_exposure_mode_name(value);
    case 0x0009U: return sigma_metering_mode_name(value);
    case 0x002CU: return sigma_color_mode_name(value);
    default: return "";
    }
}

static bool
is_samsung_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_samsung0" || ifd_has_prefix(ifd, "mk_samsung_ifd_")
           || ifd_has_prefix(ifd, "mk_samsung_type2_")
           || ifd == "makernote:samsung:ifd"
           || ifd == "makernote:samsung:type2";
}

static bool
is_samsung_picture_wizard_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_samsung_picturewizard_")
           || ifd == "makernote:samsung:picturewizard";
}

static const char*
samsung_device_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x1000U: return "Compact Digital Camera";
    case 0x2000U: return "High-end NX Camera";
    case 0x3000U: return "HXM Video Camera";
    case 0x12000U: return "Cell Phone";
    case 0x300000U: return "SMX Video Camera";
    default: return "";
    }
}

static const char*
samsung_raw_data_byte_order_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Little-endian (Intel, II)";
    case 1U: return "Big-endian (Motorola, MM)";
    default: return "";
    }
}

static const char*
samsung_raw_data_cfa_pattern_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Unchanged";
    case 1U: return "Swap";
    case 65535U: return "Roll";
    default: return "";
    }
}

static const char*
samsung_color_space_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "sRGB";
    case 1U: return "Adobe RGB";
    default: return "";
    }
}

static const char*
samsung_picture_wizard_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Vivid";
    case 2U: return "Portrait";
    case 3U: return "Landscape";
    case 4U: return "Forest";
    case 5U: return "Retro";
    case 6U: return "Cool";
    case 7U: return "Calm";
    case 8U: return "Classic";
    case 9U: return "Custom1";
    case 10U: return "Custom2";
    case 11U: return "Custom3";
    case 255U: return "n/a";
    default: return "";
    }
}

static const char*
samsung_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_samsung_picture_wizard_ifd(ifd)) {
        switch (tag) {
        case 0x0000U: return samsung_picture_wizard_mode_name(value);
        default: return "";
        }
    }
    if (!is_samsung_ifd(ifd)) {
        return "";
    }
    switch (tag) {
    case 0x0002U: return samsung_device_type_name(value);
    case 0x0040U: return samsung_raw_data_byte_order_name(value);
    case 0x0041U: return value == 0U ? "Auto" : value == 1U ? "Manual" : "";
    case 0x0050U: return samsung_raw_data_cfa_pattern_name(value);
    case 0x0100U: return off_on_name(value);
    case 0x0120U: return off_on_name(value);
    case 0xA011U: return samsung_color_space_name(value);
    case 0xA012U: return off_on_name(value);
    default: return "";
    }
}

static bool
is_ricoh_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_ricoh0" || ifd_has_prefix(ifd, "mk_ricoh_main")
           || ifd == "makernote:ricoh:main";
}

static bool
is_ricoh_image_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_ricoh_imageinfo_")
           || ifd == "makernote:ricoh:imageinfo";
}

static const char*
ricoh_exposure_program_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Auto";
    case 2U: return "Program AE";
    case 3U: return "Aperture-priority AE";
    case 4U: return "Shutter speed priority AE";
    case 5U: return "Shutter/aperture priority AE";
    case 6U: return "Manual";
    case 7U: return "Movie";
    default: return "";
    }
}

static const char*
ricoh_drive_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single-frame";
    case 1U: return "Continuous";
    case 8U: return "AF-priority Continuous";
    default: return "";
    }
}

static const char*
ricoh_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Manual";
    case 2U: return "Multi AF";
    case 3U: return "Spot AF";
    case 4U: return "Snap";
    case 5U: return "Infinity";
    case 7U: return "Face Detect";
    case 8U: return "Subject Tracking";
    case 9U: return "Pinpoint AF";
    case 10U: return "Movie";
    default: return "";
    }
}

static const char*
ricoh_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Auto, Fired";
    case 2U: return "On";
    case 3U: return "Auto, Fired, Red-eye reduction";
    case 4U: return "Slow Sync";
    case 5U: return "Manual";
    case 6U: return "On, Red-eye reduction";
    case 7U: return "Synchro, Red-eye reduction";
    case 8U: return "Auto, Did not fire";
    default: return "";
    }
}

static const char*
ricoh_manual_flash_output_name(uint64_t value) noexcept
{
    return value == 0U ? "Full" : "";
}

static const char*
ricoh_image_effects_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Vivid";
    case 3U: return "Black & White";
    case 5U: return "B&W Toning Effect";
    case 6U: return "Setting 1";
    case 7U: return "Setting 2";
    case 9U: return "High-contrast B&W";
    case 10U: return "Cross Process";
    case 11U: return "Positive Film";
    case 12U: return "Bleach Bypass";
    case 13U: return "Retro";
    case 15U: return "Miniature";
    case 17U: return "High Key";
    default: return "";
    }
}

static const char*
ricoh_level_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Low";
    case 2U: return "Medium";
    case 3U: return "High";
    default: return "";
    }
}

static const char*
ricoh_dynamic_range_expansion_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 3U: return "Weak";
    case 4U: return "Medium";
    case 5U: return "Strong";
    default: return "";
    }
}

static const char*
ricoh_noise_reduction_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Weak";
    case 2U: return "Medium";
    case 3U: return "Strong";
    default: return "";
    }
}

static const char*
ricoh_crop_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On (35mm)";
    case 2U: return "On (47mm)";
    default: return "";
    }
}

static const char*
ricoh_image_info_sharpness_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Sharp";
    case 1U: return "Normal";
    case 2U: return "Soft";
    default: return "";
    }
}

static const char*
ricoh_image_info_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Cloudy";
    case 3U: return "Tungsten";
    case 4U: return "Fluorescent";
    case 5U: return "Manual";
    case 7U: return "Detail";
    case 9U: return "Multi-pattern Auto";
    default: return "";
    }
}

static const char*
ricoh_iso_setting_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "64";
    case 2U: return "100";
    case 4U: return "200";
    case 6U: return "400";
    case 7U: return "800";
    case 8U: return "1600";
    case 9U: return "Auto";
    case 10U: return "3200";
    case 11U: return "100 (Low)";
    default: return "";
    }
}

static const char*
ricoh_saturation_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "High";
    case 1U: return "Normal";
    case 2U: return "Low";
    case 3U: return "B&W";
    case 6U: return "Toning Effect";
    case 9U: return "Vivid";
    case 10U: return "Natural";
    default: return "";
    }
}

static const char*
ricoh_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x1001U: return ricoh_exposure_program_name(value);
    case 0x1002U: return ricoh_drive_mode_name(value);
    case 0x1006U: return ricoh_focus_mode_name(value);
    case 0x1009U: return off_on_name(value);
    case 0x100AU: return ricoh_flash_mode_name(value);
    case 0x100CU: return ricoh_manual_flash_output_name(value);
    case 0x100DU: return off_on_name(value);
    case 0x100EU: return ricoh_dynamic_range_expansion_name(value);
    case 0x100FU: return ricoh_noise_reduction_name(value);
    case 0x1010U: return ricoh_image_effects_name(value);
    case 0x1011U: return ricoh_level_name(value);
    case 0x1018U: return ricoh_crop_mode_name(value);
    case 0x1019U: return off_on_name(value);
    case 0x1205U: return value == 0U ? "Auto" : value == 2U ? "Manual" : "";
    default: return "";
    }
}

static const char*
ricoh_image_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0020U:
        return value == 0U   ? "Off"
               : value == 1U ? "Auto"
               : value == 2U ? "On"
                             : "";
    case 0x0022U: return ricoh_image_info_sharpness_name(value);
    case 0x0026U: return ricoh_image_info_white_balance_name(value);
    case 0x0027U: return ricoh_iso_setting_name(value);
    case 0x0028U: return ricoh_saturation_name(value);
    default: return "";
    }
}

static const char*
ricoh_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_ricoh_main_ifd(ifd)) {
        return ricoh_main_value_name(tag, value);
    }
    if (is_ricoh_image_info_ifd(ifd)) {
        return ricoh_image_info_value_name(tag, value);
    }
    return "";
}

static bool
is_apple_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_apple0" || ifd_has_prefix(ifd, "mk_apple_main")
           || ifd == "makernote:apple:main";
}

static const char*
apple_hdr_image_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 3U: return "HDR Image";
    case 4U: return "Original Image";
    default: return "";
    }
}

static const char*
apple_image_capture_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "ProRAW";
    case 2U: return "Portrait";
    case 10U: return "Photo";
    case 11U: return "Manual Focus";
    case 12U: return "Scene";
    default: return "";
    }
}

static const char*
apple_camera_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Back Wide Angle";
    case 1U: return "Back Normal";
    case 6U: return "Front";
    default: return "";
    }
}

static const char*
apple_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (!is_apple_main_ifd(ifd)) {
        return "";
    }
    switch (tag) {
    case 0x0004U: return no_yes_name(value);
    case 0x0007U: return no_yes_name(value);
    case 0x000AU: return apple_hdr_image_type_name(value);
    case 0x0014U: return apple_image_capture_type_name(value);
    case 0x002EU: return apple_camera_type_name(value);
    default: return "";
    }
}

static bool
is_flir_gpsinfo_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_flir_fff_gpsinfo")
           || ifd_has_prefix(ifd, "makernote:flir:fff_gpsinfo");
}

static const char*
flir_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_flir_gpsinfo_ifd(ifd) && tag == 0x0000U) {
        return no_yes_name(value);
    }
    return "";
}

static bool
is_jvc_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_jvc0" || ifd_has_prefix(ifd, "mk_jvc_main")
           || ifd == "makernote:jvc:main";
}

static const char*
jvc_quality_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Low";
    case 1U: return "Normal";
    case 2U: return "Fine";
    default: return "";
    }
}

static const char*
jvc_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_jvc_main_ifd(ifd) && tag == 0x0003U) {
        return jvc_quality_name(value);
    }
    return "";
}

static bool
is_ge_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_ge0" || ifd_has_prefix(ifd, "mk_ge_main")
           || ifd == "makernote:ge:main";
}

static const char*
ge_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_ge_main_ifd(ifd) && tag == 0x0202U) {
        return off_on_name(value);
    }
    return "";
}

static bool
is_microsoft_stitch_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_microsoft_stitch_0"
           || ifd_has_prefix(ifd, "mk_microsoft_stitch")
           || ifd == "makernote:microsoft:stitch";
}

static const char*
microsoft_stitch_camera_motion_name(uint64_t value) noexcept
{
    switch (value) {
    case 2U: return "Rigid Scale";
    case 3U: return "Affine";
    case 4U: return "3D Rotation";
    case 5U: return "Homography";
    default: return "";
    }
}

static const char*
microsoft_stitch_map_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Perspective";
    case 1U: return "Horizontal Cylindrical";
    case 2U: return "Horizontal Spherical";
    case 257U: return "Vertical Cylindrical";
    case 258U: return "Vertical Spherical";
    default: return "";
    }
}

static const char*
microsoft_value_name(std::string_view ifd, uint16_t tag,
                     uint64_t value) noexcept
{
    if (!is_microsoft_stitch_ifd(ifd)) {
        return "";
    }
    switch (tag) {
    case 0x0001U: return microsoft_stitch_camera_motion_name(value);
    case 0x0002U: return microsoft_stitch_map_type_name(value);
    default: return "";
    }
}

static bool
is_motorola_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_motorola0" || ifd_has_prefix(ifd, "mk_motorola_main")
           || ifd == "makernote:motorola:main";
}

static const char*
motorola_custom_rendered_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Custom";
    default: return "";
    }
}

static const char*
motorola_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (!is_motorola_main_ifd(ifd)) {
        return "";
    }
    switch (tag) {
    case 0x6420U: return motorola_custom_rendered_name(value);
    default: return "";
    }
}

static bool
is_reconyx_hyperfire_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_reconyx_hyperfire_0"
           || ifd_has_prefix(ifd, "mk_reconyx_hyperfire_")
           || ifd == "makernote:reconyx:hyperfire";
}

static bool
is_reconyx_hyperfire2_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_reconyx_hyperfire2_0"
           || ifd_has_prefix(ifd, "mk_reconyx_hyperfire2_")
           || ifd == "makernote:reconyx:hyperfire2";
}

static bool
is_reconyx_hyperfire4k_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_reconyx_hyperfire4k_0"
           || ifd_has_prefix(ifd, "mk_reconyx_hyperfire4k_")
           || ifd == "makernote:reconyx:hyperfire4k";
}

static bool
is_reconyx_microfire_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_reconyx_microfire_0"
           || ifd_has_prefix(ifd, "mk_reconyx_microfire_")
           || ifd == "makernote:reconyx:microfire";
}

static bool
is_reconyx_ultrafire_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_reconyx_ultrafire_0"
           || ifd_has_prefix(ifd, "mk_reconyx_ultrafire_")
           || ifd == "makernote:reconyx:ultrafire";
}

static const char*
reconyx_trigger_mode_name(uint64_t value, bool point_and_shoot,
                          bool live_view) noexcept
{
    switch (value) {
    case static_cast<uint64_t>('C'): return "CodeLoc Not Entered";
    case static_cast<uint64_t>('E'): return "External Sensor";
    case static_cast<uint64_t>('M'): return "Motion Detection";
    case static_cast<uint64_t>('T'): return "Time Lapse";
    case static_cast<uint64_t>('P'):
        return point_and_shoot ? "Point and Shoot" : "";
    case static_cast<uint64_t>('S'): return live_view ? "Cell Status" : "";
    case static_cast<uint64_t>('L'): return live_view ? "Cell Live View" : "";
    default: return "";
    }
}

static const char*
reconyx_trigger_mode_microfire_name(uint64_t value) noexcept
{
    if (value == static_cast<uint64_t>('M')) {
        return "Motion Sensor";
    }
    return reconyx_trigger_mode_name(value, false, false);
}

static const char*
reconyx_trigger_mode_hyperfire4k_name(uint64_t value) noexcept
{
    if (value == static_cast<uint64_t>('M')) {
        return "Motion Sensor";
    }
    return reconyx_trigger_mode_name(value, false, true);
}

static const char*
reconyx_moon_phase_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "New";
    case 1U: return "New Crescent";
    case 2U: return "First Quarter";
    case 3U: return "Waxing Gibbous";
    case 4U: return "Full";
    case 5U: return "Waning Gibbous";
    case 6U: return "Last Quarter";
    case 7U: return "Old Crescent";
    default: return "";
    }
}

static const char*
reconyx_day_sunday0_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Sunday";
    case 1U: return "Monday";
    case 2U: return "Tuesday";
    case 3U: return "Wednesday";
    case 4U: return "Thursday";
    case 5U: return "Friday";
    case 6U: return "Saturday";
    default: return "";
    }
}

static const char*
reconyx_day_sunday1_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Sunday";
    case 2U: return "Monday";
    case 3U: return "Tuesday";
    case 4U: return "Wednesday";
    case 5U: return "Thursday";
    case 6U: return "Friday";
    case 7U: return "Saturday";
    default: return "";
    }
}

static const char*
reconyx_microfire_battery_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Lithium";
    case 1U: return "NiMH";
    case 2U: return "Alkaline";
    case 3U: return "Lead Acid";
    default: return "";
    }
}

static const char*
reconyx_hyperfire4k_battery_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "NiMH";
    case 2U: return "Lithium";
    case 3U: return "External";
    case 4U: return "SC10 Solar";
    default: return "";
    }
}

static const char*
reconyx_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_reconyx_hyperfire_ifd(ifd)) {
        switch (tag) {
        case 0x0006U: return reconyx_trigger_mode_name(value, false, false);
        case 0x0012U: return reconyx_moon_phase_name(value);
        case 0x0028U: return off_on_name(value);
        default: return "";
        }
    }
    if (is_reconyx_ultrafire_ifd(ifd)) {
        switch (tag) {
        case 0x0034U: return reconyx_trigger_mode_name(value, true, false);
        case 0x0042U: return reconyx_day_sunday0_name(value);
        case 0x0043U: return reconyx_moon_phase_name(value);
        case 0x0048U: return off_on_name(value);
        default: return "";
        }
    }
    if (is_reconyx_hyperfire2_ifd(ifd)) {
        switch (tag) {
        case 0x0034U: return reconyx_trigger_mode_name(value, true, false);
        case 0x004AU: return reconyx_day_sunday0_name(value);
        case 0x004CU: return reconyx_moon_phase_name(value);
        case 0x005AU: return off_on_name(value);
        default: return "";
        }
    }
    if (is_reconyx_microfire_ifd(ifd)) {
        switch (tag) {
        case 0x0044U: return reconyx_trigger_mode_microfire_name(value);
        case 0x005AU: return reconyx_day_sunday1_name(value);
        case 0x005CU: return reconyx_moon_phase_name(value);
        case 0x006AU: return off_on_name(value);
        case 0x0074U: return reconyx_microfire_battery_type_name(value);
        default: return "";
        }
    }
    if (is_reconyx_hyperfire4k_ifd(ifd)) {
        switch (tag) {
        case 0x0028U: return reconyx_trigger_mode_hyperfire4k_name(value);
        case 0x0036U: return reconyx_day_sunday1_name(value);
        case 0x0037U: return reconyx_moon_phase_name(value);
        case 0x0044U: return off_on_name(value);
        case 0x004FU: return reconyx_hyperfire4k_battery_type_name(value);
        default: return "";
        }
    }
    return "";
}

static bool
is_nintendo_camerainfo_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_nintendo_camerainfo_0"
           || ifd_has_prefix(ifd, "mk_nintendo_camerainfo_")
           || ifd == "makernote:nintendo:camerainfo";
}

static const char*
nintendo_category_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x0000U: return "(none)";
    case 0x1000U: return "Mii";
    case 0x2000U: return "Man";
    case 0x4000U: return "Woman";
    default: return "";
    }
}

static const char*
nintendo_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_nintendo_camerainfo_ifd(ifd) && tag == 0x0030U) {
        return nintendo_category_name(value);
    }
    return "";
}

static bool
is_sanyo_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_sanyo0" || ifd_has_prefix(ifd, "mk_sanyo_main")
           || ifd == "makernote:sanyo:main";
}

static bool
is_sanyo_mov_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_sanyo_mov") || ifd == "makernote:sanyo:mov";
}

static const char*
sanyo_quality_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x0000U: return "Normal/Very Low";
    case 0x0001U: return "Normal/Low";
    case 0x0002U: return "Normal/Medium Low";
    case 0x0003U: return "Normal/Medium";
    case 0x0004U: return "Normal/Medium High";
    case 0x0005U: return "Normal/High";
    case 0x0006U: return "Normal/Very High";
    case 0x0007U: return "Normal/Super High";
    case 0x0100U: return "Fine/Very Low";
    case 0x0101U: return "Fine/Low";
    case 0x0102U: return "Fine/Medium Low";
    case 0x0103U: return "Fine/Medium";
    case 0x0104U: return "Fine/Medium High";
    case 0x0105U: return "Fine/High";
    case 0x0106U: return "Fine/Very High";
    case 0x0107U: return "Fine/Super High";
    case 0x0200U: return "Super Fine/Very Low";
    case 0x0201U: return "Super Fine/Low";
    case 0x0202U: return "Super Fine/Medium Low";
    case 0x0203U: return "Super Fine/Medium";
    case 0x0204U: return "Super Fine/Medium High";
    case 0x0205U: return "Super Fine/High";
    case 0x0206U: return "Super Fine/Very High";
    case 0x0207U: return "Super Fine/Super High";
    default: return "";
    }
}

static const char*
sanyo_macro_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Macro";
    case 2U: return "View";
    case 3U: return "Manual";
    default: return "";
    }
}

static const char*
sanyo_sequential_shot_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "None";
    case 1U: return "Standard";
    case 2U: return "Best";
    case 3U: return "Adjust Exposure";
    default: return "";
    }
}

static const char*
sanyo_record_shutter_release_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Record while down";
    case 1U: return "Press start, press stop";
    default: return "";
    }
}

static const char*
sanyo_scene_select_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Sport";
    case 2U: return "TV";
    case 3U: return "Night";
    case 4U: return "User 1";
    case 5U: return "User 2";
    case 6U: return "Lamp";
    default: return "";
    }
}

static const char*
sanyo_sequence_shot_interval_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "5 frames/s";
    case 1U: return "10 frames/s";
    case 2U: return "15 frames/s";
    case 3U: return "20 frames/s";
    default: return "";
    }
}

static const char*
sanyo_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Force";
    case 2U: return "Disabled";
    case 3U: return "Red eye";
    default: return "";
    }
}

static const char*
sanyo_mov_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Shade";
    case 3U: return "Fluorescent";
    case 4U: return "Tungsten";
    case 5U: return "Manual";
    default: return "";
    }
}

static const char*
sanyo_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_sanyo_main_ifd(ifd)) {
        switch (tag) {
        case 0x0201U: return sanyo_quality_name(value);
        case 0x0202U: return sanyo_macro_name(value);
        case 0x020EU: return sanyo_sequential_shot_name(value);
        case 0x020FU:
        case 0x0210U:
        case 0x0213U:
        case 0x0214U:
        case 0x0216U:
        case 0x0218U:
        case 0x0219U:
        case 0x021BU:
        case 0x021DU: return off_on_name(value);
        case 0x0217U: return sanyo_record_shutter_release_name(value);
        case 0x021EU: return no_yes_name(value);
        case 0x021FU: return sanyo_scene_select_name(value);
        case 0x0224U: return sanyo_sequence_shot_interval_name(value);
        case 0x0225U: return sanyo_flash_mode_name(value);
        default: return "";
        }
    }
    if (is_sanyo_mov_ifd(ifd) && tag == 0x0044U) {
        return sanyo_mov_white_balance_name(value);
    }
    return "";
}

static const char*
makernote_tag_numeric_value_name(std::string_view ifd, uint16_t tag,
                                 uint64_t value) noexcept
{
    if (is_canon_camera_settings_ifd(ifd)) {
        return canon_camera_settings_value_name(tag, value);
    }
    if (is_canon_camera_info_ifd(ifd)) {
        return canon_camera_info_value_name(tag, value);
    }
    if (is_canon_shot_info_ifd(ifd)) {
        return canon_shot_info_value_name(tag, value);
    }
    if (is_canon_main_ifd(ifd)) {
        return canon_main_value_name(tag, value);
    }
    if (is_canon_mycolors_ifd(ifd)) {
        return canon_mycolors_value_name(tag, value);
    }
    if (is_canon_focal_length_ifd(ifd)) {
        return canon_focal_length_value_name(tag, value);
    }
    if (is_canon_af_info2_ifd(ifd)) {
        return canon_af_info2_value_name(tag, value);
    }
    if (is_canon_aspect_info_ifd(ifd)) {
        return canon_aspect_info_value_name(tag, value);
    }
    if (is_canon_file_info_ifd(ifd)) {
        return canon_file_info_value_name(tag, value);
    }
    if (is_canon_processing_ifd(ifd)) {
        return canon_processing_value_name(tag, value);
    }
    if (is_canon_lighting_opt_ifd(ifd)) {
        return canon_lighting_opt_value_name(tag, value);
    }
    if (is_canon_vignetting_corr2_ifd(ifd)) {
        return canon_vignetting_corr2_value_name(tag, value);
    }
    if (is_canon_vignetting_corr_ifd(ifd)) {
        return canon_vignetting_corr_value_name(tag, value);
    }
    if (is_canon_time_info_ifd(ifd)) {
        return canon_time_info_value_name(tag, value);
    }
    if (is_canon_filter_info_ifd(ifd)) {
        return canon_filter_info_value_name(tag, value);
    }
    if (is_canon_hdr_info_ifd(ifd)) {
        return canon_hdr_info_value_name(tag, value);
    }
    if (is_canon_custom_functions2_ifd(ifd)) {
        return canon_custom_functions2_value_name(tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_nikon")
        || ifd_has_prefix(ifd, "makernote:nikon:")) {
        return nikon_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_fujifilm") || ifd_has_prefix(ifd, "mk_fuji")
        || ifd_has_prefix(ifd, "makernote:fujifilm:")) {
        return fujifilm_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_sony")
        || ifd_has_prefix(ifd, "makernote:sony:")) {
        return sony_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_pentax")
        || ifd_has_prefix(ifd, "makernote:pentax:")) {
        return pentax_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_olympus")
        || ifd_has_prefix(ifd, "makernote:olympus:")) {
        return olympus_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_casio")
        || ifd_has_prefix(ifd, "makernote:casio:")) {
        return casio_value_name(tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_panasonic")
        || ifd_has_prefix(ifd, "makernote:panasonic:")) {
        return panasonic_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_phaseone")
        || ifd_has_prefix(ifd, "makernote:phaseone:")) {
        return phaseone_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_kodak")
        || ifd_has_prefix(ifd, "makernote:kodak:")) {
        return kodak_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_minolta")
        || ifd_has_prefix(ifd, "makernote:minolta:")) {
        return minolta_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_sigma")
        || ifd_has_prefix(ifd, "makernote:sigma:")) {
        return sigma_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_samsung")
        || ifd_has_prefix(ifd, "makernote:samsung:")) {
        return samsung_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_ricoh")
        || ifd_has_prefix(ifd, "makernote:ricoh:")) {
        return ricoh_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_apple")
        || ifd_has_prefix(ifd, "makernote:apple:")) {
        return apple_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_flir")
        || ifd_has_prefix(ifd, "makernote:flir:")) {
        return flir_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_jvc")
        || ifd_has_prefix(ifd, "makernote:jvc:")) {
        return jvc_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_ge") || ifd_has_prefix(ifd, "makernote:ge:")) {
        return ge_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_microsoft")
        || ifd_has_prefix(ifd, "makernote:microsoft:")) {
        return microsoft_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_motorola")
        || ifd_has_prefix(ifd, "makernote:motorola:")) {
        return motorola_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_reconyx")
        || ifd_has_prefix(ifd, "makernote:reconyx:")) {
        return reconyx_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_nintendo")
        || ifd_has_prefix(ifd, "makernote:nintendo:")) {
        return nintendo_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_sanyo")
        || ifd_has_prefix(ifd, "makernote:sanyo:")) {
        return sanyo_value_name(ifd, tag, value);
    }
    return "";
}

static void
clear_format_output(char* out, std::size_t out_size) noexcept
{
    if (out != nullptr && out_size > 0U) {
        out[0] = '\0';
    }
}

static bool
append_char(char* out, std::size_t out_size, std::size_t* pos,
            char value) noexcept
{
    if (out == nullptr || pos == nullptr || out_size == 0U) {
        return false;
    }
    if ((*pos + 1U) >= out_size) {
        return false;
    }
    out[*pos] = value;
    ++(*pos);
    out[*pos] = '\0';
    return true;
}

static bool
copy_format_text(std::string_view value, char* out,
                 std::size_t out_size) noexcept
{
    clear_format_output(out, out_size);
    if (out == nullptr || value.empty() || value.size() >= out_size) {
        return false;
    }
    for (std::size_t i = 0U; i < value.size(); ++i) {
        out[i] = value[i];
    }
    out[value.size()] = '\0';
    return true;
}

static bool
append_decimal(char* out, std::size_t out_size, std::size_t* pos,
               uint64_t value, unsigned min_digits) noexcept
{
    char digits[20];
    std::size_t digit_count = 0U;
    uint64_t current        = value;
    do {
        digits[digit_count] = static_cast<char>('0' + (current % 10U));
        ++digit_count;
        current /= 10U;
    } while (current != 0U && digit_count < sizeof(digits));

    const std::size_t total_digits = digit_count < min_digits ? min_digits
                                                              : digit_count;
    if (out == nullptr || pos == nullptr || out_size == 0U) {
        return false;
    }
    if ((*pos + total_digits) >= out_size) {
        return false;
    }
    for (std::size_t i = digit_count; i < total_digits; ++i) {
        out[*pos] = '0';
        ++(*pos);
    }
    for (std::size_t i = 0U; i < digit_count; ++i) {
        out[*pos] = digits[digit_count - 1U - i];
        ++(*pos);
    }
    out[*pos] = '\0';
    return true;
}

static bool
format_olympus_packed_firmware(uint64_t value, char* out,
                               std::size_t out_size) noexcept
{
    std::size_t pos = 0U;
    clear_format_output(out, out_size);
    if (!append_decimal(out, out_size, &pos, value >> 12U, 0U)) {
        clear_format_output(out, out_size);
        return false;
    }
    if (!append_char(out, out_size, &pos, '.')) {
        clear_format_output(out, out_size);
        return false;
    }
    if (!append_decimal(out, out_size, &pos, value & 0x0FFFU, 3U)) {
        clear_format_output(out, out_size);
        return false;
    }
    return true;
}

static bool
format_dotted_integer_bytes(uint64_t value, unsigned byte_count, char* out,
                            std::size_t out_size) noexcept
{
    std::size_t pos = 0U;
    clear_format_output(out, out_size);
    if (byte_count == 0U || byte_count > 8U) {
        return false;
    }
    for (unsigned i = 0U; i < byte_count; ++i) {
        const unsigned shift = (byte_count - 1U - i) * 8U;
        const uint64_t part  = (value >> shift) & 0xFFU;
        if (i != 0U && !append_char(out, out_size, &pos, '.')) {
            clear_format_output(out, out_size);
            return false;
        }
        if (!append_decimal(out, out_size, &pos, part, 0U)) {
            clear_format_output(out, out_size);
            return false;
        }
    }
    return true;
}

static bool
format_dotted_payload_bytes(std::span<const std::byte> value, char* out,
                            std::size_t out_size) noexcept
{
    std::size_t pos = 0U;
    clear_format_output(out, out_size);
    if (value.empty() || value.size() > 16U) {
        return false;
    }
    for (std::size_t i = 0U; i < value.size(); ++i) {
        const uint64_t part = static_cast<uint64_t>(
            std::to_integer<unsigned char>(value[i]));
        if (i != 0U && !append_char(out, out_size, &pos, '.')) {
            clear_format_output(out, out_size);
            return false;
        }
        if (!append_decimal(out, out_size, &pos, part, 0U)) {
            clear_format_output(out, out_size);
            return false;
        }
    }
    return true;
}

static bool
format_ascii_payload_bytes(std::span<const std::byte> value, char* out,
                           std::size_t out_size) noexcept
{
    bool has_digit       = false;
    std::size_t text_end = 0U;
    clear_format_output(out, out_size);
    while (text_end < value.size()) {
        const unsigned char byte = std::to_integer<unsigned char>(
            value[text_end]);
        if (byte == 0U) {
            break;
        }
        if (byte < 0x20U || byte > 0x7EU) {
            return false;
        }
        if (byte >= static_cast<unsigned char>('0')
            && byte <= static_cast<unsigned char>('9')) {
            has_digit = true;
        }
        ++text_end;
    }
    while (text_end > 0U) {
        const unsigned char byte = std::to_integer<unsigned char>(
            value[text_end - 1U]);
        if (byte != static_cast<unsigned char>(' ')) {
            break;
        }
        --text_end;
    }
    if (text_end == 0U || !has_digit) {
        return false;
    }
    if (out == nullptr || out_size == 0U || text_end >= out_size) {
        return false;
    }
    for (std::size_t i = 0U; i < text_end; ++i) {
        out[i] = static_cast<char>(std::to_integer<unsigned char>(value[i]));
    }
    out[text_end] = '\0';
    return true;
}

static bool
is_standard_byte_version_tag(std::string_view ifd, uint16_t tag) noexcept
{
    if (tag == 0x9000U || tag == 0xA000U) {
        return true;
    }
    if (tag == 0x0000U
        && (ifd_contains(ifd, "gps") || ifd_contains(ifd, "GPS"))) {
        return true;
    }
    return false;
}

static bool
is_nikon_version_like_tag(std::string_view ifd, uint16_t tag) noexcept
{
    if (is_nikon_main_ifd(ifd) && tag == 0x0001U) {
        return true;
    }
    if (is_nikon_shot_info_ifd(ifd)
        && (tag == 0x0004U || tag == 0x000EU || tag == 0x0018U)) {
        return true;
    }
    if (is_nikon_lens_data_ifd(ifd) && (tag == 0x0000U || tag == 0x0034U)) {
        return true;
    }
    if (is_nikon_flash_info_ifd(ifd) && tag == 0x0006U) {
        return true;
    }
    if (is_nikon_makernotes_firmware_ifd(ifd) && tag == 0x0000U) {
        return true;
    }
    return false;
}

static bool
is_olympus_packed_firmware_tag(std::string_view ifd, uint16_t tag) noexcept
{
    if (!is_olympus_equipment_ifd(ifd)) {
        return false;
    }
    return tag == 0x0104U || tag == 0x0204U || tag == 0x0304U || tag == 0x1002U;
}

static unsigned
numeric_version_byte_count(uint64_t value) noexcept
{
    if (value <= 0xFFU) {
        return 1U;
    }
    if (value <= 0xFFFFU) {
        return 2U;
    }
    if (value <= 0xFFFFFFU) {
        return 3U;
    }
    return 4U;
}

bool
exif_tag_numeric_value_format(std::string_view ifd, uint16_t tag,
                              uint64_t value, char* out,
                              std::size_t out_size) noexcept
{
    clear_format_output(out, out_size);
    if (is_olympus_packed_firmware_tag(ifd, tag)) {
        return format_olympus_packed_firmware(value, out, out_size);
    }
    if (is_nikon_version_like_tag(ifd, tag)) {
        const unsigned byte_count = numeric_version_byte_count(value);
        return format_dotted_integer_bytes(value, byte_count, out, out_size);
    }
    return false;
}

bool
exif_tag_byte_value_format(std::string_view ifd, uint16_t tag,
                           std::span<const std::byte> value, char* out,
                           std::size_t out_size) noexcept
{
    clear_format_output(out, out_size);
    if (value.size() == 1U && !is_makernote_ifd(ifd)) {
        const uint64_t numeric = static_cast<uint64_t>(
            std::to_integer<unsigned char>(value[0]));
        const std::string_view label = exif_tag_numeric_value_name(ifd, tag,
                                                                   numeric);
        if (!label.empty()) {
            return copy_format_text(label, out, out_size);
        }
    }
    if (!is_standard_byte_version_tag(ifd, tag)
        && !is_nikon_version_like_tag(ifd, tag)) {
        return false;
    }
    if (format_ascii_payload_bytes(value, out, out_size)) {
        return true;
    }
    return format_dotted_payload_bytes(value, out, out_size);
}

const char*
exif_tag_numeric_value_name(std::string_view ifd, uint16_t tag,
                            uint64_t value) noexcept
{
    if (is_makernote_ifd(ifd)) {
        return makernote_tag_numeric_value_name(ifd, tag, value);
    }
    switch (tag) {
    case 0x0103U: return tiff_compression_name(value);
    case 0x0106U: return tiff_photometric_interpretation_name(value);
    case 0x011CU: return tiff_planar_configuration_name(value);
    case 0x0128U: return tiff_resolution_unit_name(value);
    case 0x0213U: return tiff_ycbcr_positioning_name(value);
    case 0x8822U: return exif_exposure_program_name(value);
    case 0x8830U: return exif_sensitivity_type_name(value);
    case 0x9207U: return exif_metering_mode_name(value);
    case 0x9208U: return exif_light_source_name(value);
    case 0x9209U: return exif_flash_name(value);
    case 0x9210U:
    case 0xA210U: return exif_focal_plane_resolution_unit_name(value);
    case 0x9217U:
    case 0xA217U: return exif_sensing_method_name(value);
    case 0xA001U: return exif_color_space_name(value);
    case 0xA300U: return exif_file_source_name(value);
    case 0xA301U: return exif_scene_type_name(value);
    case 0xA401U: return exif_custom_rendered_name(value);
    case 0xA402U: return exif_exposure_mode_name(value);
    case 0xA403U: return exif_white_balance_name(value);
    case 0xA406U: return exif_scene_capture_type_name(value);
    case 0xA407U: return exif_gain_control_name(value);
    case 0xA408U: return exif_contrast_name(value);
    case 0xA409U: return exif_saturation_name(value);
    case 0xA40AU: return exif_sharpness_name(value);
    case 0xA40CU: return exif_subject_distance_range_name(value);
    case 0xA40FU:
    case 0xA410U:
    case 0xA411U: return exif_lens_correction_name(value);
    case 0xA412U: return exif_noise_reduction_name(value);
    case 0xC617U: return dng_cfa_layout_name(value);
    case 0xC65AU:
    case 0xC65BU:
    case 0xCD31U: return dng_calibration_illuminant_name(value);
    default: return "";
    }
}

}  // namespace openmeta
