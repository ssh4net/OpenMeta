// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_value_names.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

namespace openmeta {
namespace {

    TEST(ExifValueNames, MapsTiffImageLayoutEnums)
    {
        EXPECT_STREQ(tiff_compression_name(1U), "Uncompressed");
        EXPECT_STREQ(tiff_compression_name(8U), "Adobe Deflate");
        EXPECT_STREQ(tiff_compression_name(9U), "JBIG B&W or VC-5");
        EXPECT_STREQ(tiff_compression_name(34713U), "Nikon NEF Compressed");
        EXPECT_STREQ(tiff_compression_name(34892U), "Lossy JPEG");
        EXPECT_STREQ(tiff_compression_name(32767U), "Sony ARW Compressed");
        EXPECT_STREQ(tiff_compression_name(52546U), "JPEG XL");
        EXPECT_STREQ(tiff_photometric_interpretation_name(2U), "RGB");
        EXPECT_STREQ(tiff_photometric_interpretation_name(32803U),
                     "Color Filter Array");
        EXPECT_STREQ(tiff_photometric_interpretation_name(32892U),
                     "Sequential Color Filter");
        EXPECT_STREQ(tiff_planar_configuration_name(1U), "Chunky");
        EXPECT_STREQ(tiff_planar_configuration_name(2U), "Planar");
        EXPECT_STREQ(tiff_resolution_unit_name(3U), "cm");
        EXPECT_STREQ(tiff_compression_name(65534U), "");
    }

    TEST(ExifValueNames, MapsStandardExifEnums)
    {
        EXPECT_STREQ(exif_exposure_program_name(3U), "Aperture-priority AE");
        EXPECT_STREQ(exif_exposure_mode_name(1U), "Manual");
        EXPECT_STREQ(exif_exposure_mode_name(2U), "Auto bracket");
        EXPECT_STREQ(exif_metering_mode_name(5U), "Multi-segment");
        EXPECT_STREQ(exif_light_source_name(21U), "D65");
        EXPECT_STREQ(exif_light_source_name(30U), "Daylight LED");
        EXPECT_STREQ(exif_light_source_name(34U), "Warm white LED");
        EXPECT_STREQ(exif_flash_name(25U), "Auto, fired");
        EXPECT_STREQ(exif_flash_name(20U),
                     "Off, did not fire, return not detected");
        EXPECT_STREQ(exif_flash_name(88U),
                     "Auto, did not fire, red-eye reduction");
        EXPECT_STREQ(exif_color_space_name(1U), "sRGB");
        EXPECT_STREQ(exif_color_space_name(0xFFFFU), "Uncalibrated");
        EXPECT_STREQ(exif_white_balance_name(1U), "Manual");
        EXPECT_STREQ(exif_exposure_program_name(6U), "Action (High speed)");
        EXPECT_STREQ(exif_scene_capture_type_name(2U), "Portrait");
        EXPECT_STREQ(exif_scene_capture_type_name(3U), "Night");
        EXPECT_STREQ(exif_gain_control_name(2U), "High gain up");
        EXPECT_STREQ(tiff_ycbcr_positioning_name(2U), "Co-sited");
        EXPECT_STREQ(exif_sensitivity_type_name(3U), "ISO speed");
        EXPECT_STREQ(exif_focal_plane_resolution_unit_name(5U), "micrometers");
        EXPECT_STREQ(exif_sensing_method_name(2U), "One-chip color area");
        EXPECT_STREQ(exif_file_source_name(3U), "Digital camera");
        EXPECT_STREQ(exif_scene_type_name(1U), "Directly photographed");
        EXPECT_STREQ(exif_custom_rendered_name(1U), "Custom");
        EXPECT_STREQ(exif_contrast_name(1U), "Low");
        EXPECT_STREQ(exif_saturation_name(2U), "High");
        EXPECT_STREQ(exif_sharpness_name(1U), "Soft");
        EXPECT_STREQ(exif_subject_distance_range_name(3U), "Distant");
        EXPECT_STREQ(exif_exposure_mode_name(42U), "");
        EXPECT_STREQ(exif_metering_mode_name(42U), "");
    }

    TEST(ExifValueNames, MapsDngEnums)
    {
        EXPECT_STREQ(dng_cfa_layout_name(1U), "Rectangular");
        EXPECT_STREQ(dng_cfa_layout_name(4U),
                     "Even rows offset right 1/2 column");
        EXPECT_STREQ(dng_calibration_illuminant_name(23U), "D50");
    }

    TEST(ExifValueNames, DispatchesByTag)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("ifd0", 0x0106U, 2U), "RGB");
        EXPECT_STREQ(exif_tag_numeric_value_name("exififd", 0x9208U, 4U),
                     "Flash");
        EXPECT_STREQ(exif_tag_numeric_value_name("exififd", 0xA402U, 2U),
                     "Auto bracket");
        EXPECT_STREQ(exif_tag_numeric_value_name("exififd", 0xA40FU, 1U),
                     "Applied");
        EXPECT_STREQ(exif_tag_numeric_value_name("exififd", 0xA412U, 3U),
                     "High strength");
        EXPECT_STREQ(exif_tag_numeric_value_name("ifd0", 0xC617U, 1U),
                     "Rectangular");
        EXPECT_STREQ(exif_tag_numeric_value_name("ifd0", 0x0213U, 1U),
                     "Centered");
        EXPECT_STREQ(exif_tag_numeric_value_name("exififd", 0x8830U, 6U),
                     "Recommended exposure index and ISO speed");
        EXPECT_STREQ(exif_tag_numeric_value_name("exififd", 0xA217U, 8U),
                     "Color sequential linear");
        EXPECT_STREQ(exif_tag_numeric_value_name("exififd", 0xA401U, 0U),
                     "Normal");
        EXPECT_STREQ(exif_tag_numeric_value_name("exififd", 0xA40AU, 2U),
                     "Hard");
        EXPECT_STREQ(exif_tag_numeric_value_name("ifd0", 0x9999U, 1U), "");
    }

    TEST(ExifValueNames, FormatsVersionLikeNumericValues)
    {
        char out[32];
        EXPECT_TRUE(exif_tag_numeric_value_format("mk_nikon_shotinfo_0",
                                                  0x0004U, 0x01020304U, out,
                                                  sizeof(out)));
        EXPECT_STREQ(out, "1.2.3.4");

        EXPECT_TRUE(exif_tag_numeric_value_format("mk_nikon_lensdata0800_0",
                                                  0x0034U, 0x0102U, out,
                                                  sizeof(out)));
        EXPECT_STREQ(out, "1.2");

        EXPECT_TRUE(exif_tag_numeric_value_format("mk_olympus_equipment_0",
                                                  0x0104U, 0x1005U, out,
                                                  sizeof(out)));
        EXPECT_STREQ(out, "1.005");

        EXPECT_FALSE(exif_tag_numeric_value_format("mk_nikon0", 0x0103U, 1U,
                                                   out, sizeof(out)));
        EXPECT_STREQ(out, "");

        char tiny[4];
        EXPECT_FALSE(exif_tag_numeric_value_format("mk_nikon_shotinfo_0",
                                                   0x0004U, 0x01020304U, tiny,
                                                   sizeof(tiny)));
        EXPECT_STREQ(tiny, "");
    }

    TEST(ExifValueNames, FormatsVersionLikeByteValues)
    {
        char out[32];
        const std::array<std::byte, 5> ascii_version {
            std::byte { static_cast<unsigned char>('1') },
            std::byte { static_cast<unsigned char>('.') },
            std::byte { static_cast<unsigned char>('0') },
            std::byte { static_cast<unsigned char>('2') },
            std::byte { static_cast<unsigned char>(0U) }
        };
        EXPECT_TRUE(exif_tag_byte_value_format("exififd", 0x9000U,
                                               ascii_version, out,
                                               sizeof(out)));
        EXPECT_STREQ(out, "1.02");

        const std::array<std::byte, 4> dotted_version {
            std::byte { static_cast<unsigned char>(1U) },
            std::byte { static_cast<unsigned char>(2U) },
            std::byte { static_cast<unsigned char>(3U) },
            std::byte { static_cast<unsigned char>(4U) }
        };
        EXPECT_TRUE(exif_tag_byte_value_format("mk_nikon_shotinfo_0", 0x0004U,
                                               dotted_version, out,
                                               sizeof(out)));
        EXPECT_STREQ(out, "1.2.3.4");

        EXPECT_FALSE(exif_tag_byte_value_format("mk_nikon0", 0x0103U,
                                                dotted_version, out,
                                                sizeof(out)));
        EXPECT_STREQ(out, "");

        EXPECT_FALSE(exif_tag_byte_value_format("mk_olympus_equipment_0",
                                                0x0104U, dotted_version, out,
                                                sizeof(out)));
        EXPECT_STREQ(out, "");

        char tiny[4];
        EXPECT_FALSE(exif_tag_byte_value_format("mk_nikon_shotinfo_0", 0x0004U,
                                                dotted_version, tiny,
                                                sizeof(tiny)));
        EXPECT_STREQ(tiny, "");
    }

    TEST(ExifValueNames, FormatsStandardByteEnums)
    {
        const std::array<std::byte, 1> file_source = { std::byte { 3U } };
        const std::array<std::byte, 1> scene_type  = { std::byte { 1U } };
        char out[64];

        EXPECT_TRUE(exif_tag_byte_value_format("exififd", 0xA300U, file_source,
                                               out, sizeof(out)));
        EXPECT_STREQ(out, "Digital camera");
        EXPECT_TRUE(exif_tag_byte_value_format("exififd", 0xA301U, scene_type,
                                               out, sizeof(out)));
        EXPECT_STREQ(out, "Directly photographed");
    }

    TEST(ExifValueNames, DispatchesCanonMakerNoteCameraSettingsEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0001U, 2U),
                     "Normal");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0002U, 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0003U, 3U),
                     "Fine");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0004U, 16U),
                     "External flash");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0005U, 0U),
                     "Single");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0007U, 0U),
                     "One-shot AF");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0009U, 1U),
                     "JPEG");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x000AU, 0U),
                     "Large");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x000BU, 1U),
                     "Manual");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x000CU, 0U),
                     "None");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x000DU, 0U),
                     "Normal");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x000EU, 0U),
                     "Normal");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0011U, 3U),
                     "Evaluative");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0012U, 1U),
                     "Auto");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0014U, 4U),
                     "Manual");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x001DU, 0U),
                     "(none)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0020U, 1U),
                     "Continuous");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0021U, 0U),
                     "Normal AE");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0027U, 1U),
                     "AF Point");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0028U, 5U),
                     "B&W");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0029U, 0U),
                     "n/a");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0103U, 1U),
                     "");
        EXPECT_STREQ(exif_tag_numeric_value_name(
                         "makernote:canon:camerasettings", 0x0014U, 8U),
                     "Flexible-priority AE");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerainfo5d_0",
                                                 0x0015U, 4U),
                     "External Auto");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:canon:camerainfo7d",
                                                 0x0015U, 6U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_shotinfo_0", 0x0007U,
                                                 0U),
                     "Auto");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_shotinfo_0", 0x0008U,
                                                 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_shotinfo_0", 0x0010U,
                                                 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_shotinfo_0", 0x0012U,
                                                 1U),
                     "Camera Local Control");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_shotinfo_0", 0x001AU,
                                                 250U),
                     "Compact");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_shotinfo_0", 0x001BU,
                                                 0U),
                     "None");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_shotinfo_0", 0x001CU,
                                                 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon0", 0x001CU, 1U),
                     "Date");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_mycolors_0", 0x0002U,
                                                 15U),
                     "B&W");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_focallength_0",
                                                 0x0000U, 2U),
                     "Zoom");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_afinfo2_0", 0x0001U,
                                                 2U),
                     "Single-point AF");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_aspectinfo_0",
                                                 0x0000U, 2U),
                     "4:3");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_fileinfo_0", 0x0003U,
                                                 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_fileinfo_0", 0x003DU,
                                                 329U),
                     "Canon RF 20-50mm F4 L IS USM PZ");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_fileinfo_0", 0x003DU,
                                                 332U),
                     "Canon RF 14mm F1.4 L VCM");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_processing_0",
                                                 0x0001U, 0U),
                     "Standard");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_lightingopt_0",
                                                 0x0002U, 3U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_timeinfo_0", 0x0003U,
                                                 60U),
                     "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canoncustom_functions2_0",
                                                 0x0108U, 1U),
                     "Enable");
    }

    TEST(ExifValueNames, DispatchesNikonMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon0", 0x0087U, 9U),
                     "Fired, TTL Mode");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:nikon:main",
                                                 0x0087U, 18U),
                     "LED Light");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_bracketinginfod810_0",
                                                 0x0017U, 3U),
                     "Highlight");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_otherinfod500_0",
                                                 0x0214U, 6U),
                     "Spot");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz8_0",
                                                 0x033EU, 1U),
                     "Center");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz9_0",
                                                 0x02C2U, 0U),
                     "Matrix");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz9v4_0",
                                                 0x02EAU, 3U),
                     "Highlight");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz6iii_0",
                                                 0x02D2U, 2U),
                     "Spot");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz8_0",
                                                 0x0340U, 4U),
                     "AF-F");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz9v3_0",
                                                 0x02ECU, 2U),
                     "AF-C");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz9_0",
                                                 0x008CU, 2U),
                     "On (Series)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz6iii_0",
                                                 0x01BCU, 1U),
                     "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon0", 0x009DU, 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon0", 0x0022U, 3U),
                     "Normal");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_afinfo_0", 0x0000U,
                                                 0U),
                     "Single Area");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_hdrinfo_0", 0x0004U,
                                                 48U),
                     "Auto");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_vrinfo_0", 0x0004U,
                                                 1U),
                     "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_lensdata0800_0",
                                                 0x0030U, 50U),
                     "Nikkor Z 24-70mm f/2.8 S II");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_lensdata0800_0",
                                                 0x0030U, 54U),
                     "Nikkor Z 70-200mm f/2.8 VR S II");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_lensdata0800_0",
                                                 0x0030U, 57U),
                     "Nikkor Z 24-105mm f/4-7.1");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikonsettings_main_0",
                                                 0x0003U, 3U),
                     "Auto");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikonsettings_main_0",
                                                 0x0026U, 1U),
                     "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikonsettings_main_0",
                                                 0x0046U, 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikonsettings_main_0",
                                                 0x0046U, 1U),
                     "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikonsettings_main_0",
                                                 0x0090U, 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikonsettings_main_0",
                                                 0x00DAU, 1U),
                     "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikonsettings_main_0",
                                                 0x009CU, 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon0", 0x0103U, 1U), "");
    }

    TEST(ExifValueNames, DispatchesFujifilmMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fujifilm0", 0x1010U,
                                                 0xE920U),
                     "High Speed Sync (HSS)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fuji0", 0x1001U, 3U),
                     "0 (normal)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fuji0", 0x1002U, 0x100U),
                     "Daylight");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fuji0", 0x1020U, 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fuji0", 0x1021U, 0U),
                     "Auto");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fuji0", 0x1030U, 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fuji0", 0x1100U, 6U),
                     "Pixel Shift");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fuji0", 0x1210U, 0x30U),
                     "B & W");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fuji0", 0x1300U, 0U),
                     "None");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fuji0", 0x1400U, 3U),
                     "Wide");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:fujifilm:main",
                                                 0x1021U, 1U),
                     "Manual");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fujifilm_main_0", 0x1022U,
                                                 512U),
                     "Wide/Tracking");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fujifilm0", 0x1031U,
                                                 0x0100U),
                     "Aperture-priority AE");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fujifilm0", 0x1037U, 2U),
                     "Average");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fujifilm0", 0x1301U, 1U),
                     "Out of focus");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fujifilm0", 0x1302U, 1U),
                     "Bad exposure");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fuji0", 0x1003U, 0U),
                     "0 (normal)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fuji0", 0x1401U, 0U),
                     "F0/Standard (Provia)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fuji0", 0x1436U, 0U),
                     "Original Image");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_fujifilm0", 0x1022U, 9U),
                     "");
    }

    TEST(ExifValueNames, DispatchesSonyMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony0", 0xB041U, 7U),
                     "Aperture-priority AE");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:sony:main", 0x202CU,
                                                 0x500U),
                     "Highlight");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony0", 0xB049U, 6U),
                     "White Balance Bracketing");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony_camerasettings_0",
                                                 0x0004U, 0x1107U),
                     "Continuous Bracketing");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony_camerasettings_0",
                                                 0x0013U, 4U),
                     "Fill-flash");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony_camerasettings_0",
                                                 0x0015U, 4U),
                     "Spot");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony_camerasettings2_0",
                                                 0x007EU, 0x800BU),
                     "Continuous Self-timer");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony_camerasettings3_0",
                                                 0x0005U, 56U),
                     "Handheld Night Shot");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony_camerasettings3_0",
                                                 0x0034U, 0xD5U),
                     "Continuous - Sweep Panorama");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:sony:moresettings",
                                                 0x0003U, 3U),
                     "Spot");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony_tag2010i_0", 0x024CU,
                                                 5U),
                     "iAuto");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony_tag2010g_0", 0x0210U,
                                                 23U),
                     "Single-frame - Exposure Bracketing");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony_tag2010b_0", 0x1174U,
                                                 0U),
                     "JPEG");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony_tag2010f_0", 0x1024U,
                                                 6U),
                     "Wireless");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sony0", 0xB043U, 14U), "");
    }

    TEST(ExifValueNames, DispatchesPentaxMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax0", 0x000BU, 3U),
                     "Manual");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:pentax:main",
                                                 0x000CU, 0x102U),
                     "On, Fired");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax0", 0x000DU, 0x0111U),
                     "AF-C (Release-priority)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax0", 0x0017U, 6U),
                     "Highlight");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax0", 0x0019U, 14U),
                     "Multi Auto");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax0", 0x001FU, 1U),
                     "0 (normal)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax0", 0x0026U, 0U),
                     "No");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax0", 0x0023U, 56U),
                     "Tokyo");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax0", 0x004FU, 1U),
                     "Bright");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax_aeinfo_0", 0x0006U,
                                                 216U),
                     "HDR");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax_aeinfo_0", 0x000CU,
                                                 32U),
                     "Spot");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax_flashinfo_0",
                                                 0x0002U, 0xC4U),
                     "On, P-TTL Auto");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax_type2_0", 0x0004U,
                                                 6U),
                     "Red-eye reduction");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax_lenscorr_0",
                                                 0x0003U, 16U),
                     "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax_awbinfo_0", 0x0001U,
                                                 1U),
                     "Strong Correction");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax_srinfo2_0", 0x0001U,
                                                 7U),
                     "On (AA simulation off)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax_lensrec_0", 0x0003U,
                                                 0U),
                     "Not attached");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax_lensinfo_0",
                                                 0x0000U, 0x032EU),
                     "Sigma/Samsung/Tokina Lens");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_pentax0", 0x0034U, 1U),
                     "");
    }

    TEST(ExifValueNames, DispatchesOlympusMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_camerasettings_0",
                                                 0x0200U, 3U),
                     "Aperture-priority AE");
        EXPECT_STREQ(exif_tag_numeric_value_name(
                         "makernote:olympus:camerasettings", 0x0202U, 515U),
                     "Spot+Highlight control");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_camerasettings_0",
                                                 0x0301U, 10U),
                     "MF");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_camerasettings_0",
                                                 0x0400U, 4U),
                     "Red-eye");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_camerasettings_0",
                                                 0x0500U, 23U),
                     "5500K (Flash)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_camerasettings_0",
                                                 0x0509U, 154U),
                     "HDR");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_camerasettings_0",
                                                 0x0520U, 5U),
                     "i-Enhance");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_rawdevelopment_0",
                                                 0x0101U, 2U),
                     "Gray Point");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_rawdevelopment2_0",
                                                 0x010CU, 512U),
                     "Sepia");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_imageprocessing_0",
                                                 0x101CU, 1U),
                     "Live Composite");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus0", 0x0203U, 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_camerasettings_0",
                                                 0x0100U, 1U),
                     "Yes");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_camerasettings_0",
                                                 0x050AU, 8U),
                     "Auto");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_focusinfo_0",
                                                 0x1204U, 0U),
                     "Bounce or Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_equipment_0",
                                                 0x0104U, 4097U),
                     "1.001");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_olympus_camerasettings_0",
                                                 0x0600U, 1U),
                     "");
    }

    TEST(ExifValueNames, DispatchesCasioMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_casio_type2_0", 0x3000U,
                                                 2U),
                     "Program AE");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_casio_type2_0", 0x3003U,
                                                 3U),
                     "Single-Area Auto Focus");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_casio_type2_0", 0x3008U,
                                                 2U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_casio_type2_0", 0x2012U,
                                                 12U),
                     "Flash");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_casio_type2_0", 0x302AU,
                                                 5U),
                     "Shadow Enhance Low");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_casio_type2_0", 0x3103U,
                                                 1U),
                     "Continuous Shooting");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_casio_type2_0", 0x4000U,
                                                 1U),
                     "");
    }

    TEST(ExifValueNames, DispatchesPanasonicMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0001U, 2U),
                     "High");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0003U, 19U),
                     "Auto (cool)");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:panasonic:main",
                                                 0x0007U, 8U),
                     "AF-F");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x001AU, 2U),
                     "On, Optical");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x001CU,
                                                 0x0201U),
                     "Macro Zoom");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x001FU, 51U),
                     "HDR");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0020U, 2U),
                     "No");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0027U, 0U),
                     "n/a");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0028U, 1U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x002AU, 18U),
                     "Aperture Bracketing");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x002CU, 0U),
                     "Normal");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x002CU, 2U),
                     "");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x002DU, 0U),
                     "Standard");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x002EU, 1U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0030U, 1U),
                     "Horizontal (normal)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0031U, 2U),
                     "Enabled but Not Used");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0035U, 1U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0036U,
                                                 65535U),
                     "n/a");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0038U, 1U),
                     "Full");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x003AU, 1U),
                     "Home");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x003BU, 1U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x003EU, 2U),
                     "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0042U, 11U),
                     "Vibrant");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0048U, 2U),
                     "2nd");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x005DU, 3U),
                     "High");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0062U, 1U),
                     "Yes (flash required but disabled)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0070U, 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x007CU, 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x0089U, 1U),
                     "Standard or Custom");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x009FU, 2U),
                     "Hybrid");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x00B3U, 1U),
                     "Off or 4K");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x00CAU, 0U),
                     "Multi-aspect");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x00BBU,
                                                 0x408U),
                     "Focus Stacking");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x00BEU, 1U),
                     "Yes");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x8001U, 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x8002U, 1U),
                     "No");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x8008U, 1U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x8009U, 2U),
                     "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic_subdir_0",
                                                 0x3033U, 16U),
                     "AWBc");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_panasonic0", 0x000FU, 1U),
                     "");
    }

    TEST(ExifValueNames, DispatchesPhaseOneKodakMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_phaseone0", 0x0100U, 5U),
                     "Rotate 90 CW");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_phaseone0", 0x010EU, 6U),
                     "IIQ Sv2");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_phaseone0", 0x0263U, 5U),
                     "HDR");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak0", 0x0009U, 1U),
                     "Fine");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak0", 0x001CU, 2U),
                     "Spot");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak0", 0x0038U, 2U),
                     "Macro");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak0", 0x0040U, 2U),
                     "Tungsten");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak0", 0x005CU, 0x20U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak0", 0x005DU, 1U),
                     "Yes");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak_type5_0", 0x001AU,
                                                 3U),
                     "Tungsten");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak_type5_0", 0x0027U,
                                                 3U),
                     "Red-Eye");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak_type11_0", 0x0203U,
                                                 9U),
                     "Kodachrome");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak_subifd0_0", 0xFA02U,
                                                 34U),
                     "High ISO");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak_subifd2_0", 0x6002U,
                                                 22U),
                     "Sunset");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak_kdc_ifd_0", 0xFA0DU,
                                                 6U),
                     "Shade");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_kodak_ifd_0", 0x03F2U, 1U),
                     "");
    }

    TEST(ExifValueNames, DispatchesMinoltaSigmaMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_minolta_camerasettings_0",
                                                 0x0001U, 2U),
                     "Shutter Priority");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_minolta_camerasettings_0",
                                                 0x0003U, 0x1800000U),
                     "Daylight");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_minolta_camerasettings_0",
                                                 0x0024U, 4U),
                     "Auto");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_minolta0", 0x0100U, 33U),
                     "HDR");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_minolta0", 0x0115U, 0x50U),
                     "Flash");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_minolta0", 0x0103U, 6U),
                     "");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_minolta_camerasettings7d_0",
                                                 0x000EU, 4U),
                     "AF-A");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_minolta_camerasettings7d_0",
                                                 0x0016U, 2U),
                     "Rear flash sync");
        EXPECT_STREQ(exif_tag_numeric_value_name(
                         "mk_minolta_camerasettingsa100_0", 0x0000U, 0x1053U),
                     "Landscape");
        EXPECT_STREQ(exif_tag_numeric_value_name(
                         "mk_minolta_camerasettingsa100_0", 0x000AU, 0x009U),
                     "White Balance Bracketing High");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sigma0", 0x0008U,
                                                 static_cast<uint64_t>('A')),
                     "Aperture-priority AE");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sigma0", 0x0009U,
                                                 static_cast<uint64_t>('C')),
                     "Center-weighted average");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sigma0", 0x002CU, 7U),
                     "Landscape");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sigma0", 0x0008U, 1U), "");
    }

    TEST(ExifValueNames, DispatchesSamsungRicohMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_samsung_ifd_0", 0x0002U,
                                                 0x2000U),
                     "High-end NX Camera");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_samsung_ifd_0", 0x0040U,
                                                 1U),
                     "Big-endian (Motorola, MM)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_samsung_type2_0", 0x0041U,
                                                 1U),
                     "Manual");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_samsung_type2_0", 0xA011U,
                                                 1U),
                     "Adobe RGB");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_samsung_picturewizard_0",
                                                 0x0000U, 8U),
                     "Classic");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh0", 0x1001U, 5U),
                     "Shutter/aperture priority AE");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh0", 0x1002U, 8U),
                     "AF-priority Continuous");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh0", 0x1003U, 12U),
                     "");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh0", 0x1006U, 9U),
                     "Pinpoint AF");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh0", 0x100AU, 4U),
                     "Slow Sync");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh0", 0x100FU, 1U),
                     "Weak");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh0", 0x1010U, 11U),
                     "Positive Film");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh0", 0x1018U, 2U),
                     "On (47mm)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh0", 0x1205U, 2U),
                     "Manual");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh_imageinfo_0",
                                                 0x0020U, 2U),
                     "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh_imageinfo_0",
                                                 0x0026U, 9U),
                     "Multi-pattern Auto");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh_imageinfo_0",
                                                 0x0027U, 11U),
                     "100 (Low)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh_imageinfo_0",
                                                 0x0028U, 9U),
                     "Vivid");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ricoh0", 0x1012U, 0U), "");
    }

    TEST(ExifValueNames, DispatchesAppleFlirJvcGeMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_apple0", 0x0004U, 1U),
                     "Yes");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_apple0", 0x0007U, 0U),
                     "No");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_apple0", 0x000AU, 3U),
                     "HDR Image");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_apple0", 0x000AU, 4U),
                     "Original Image");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_apple0", 0x0014U, 1U),
                     "ProRAW");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_apple0", 0x0014U, 10U),
                     "Photo");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_apple0", 0x0014U, 11U),
                     "Manual Focus");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:apple:main",
                                                 0x0014U, 12U),
                     "Scene");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_apple0", 0x002EU, 0U),
                     "Back Wide Angle");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_apple0", 0x002EU, 6U),
                     "Front");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_apple0", 0x000FU, 2U), "");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_apple0", 0x0025U, 1U), "");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_apple0", 0x0045U, 1U), "");

        EXPECT_STREQ(exif_tag_numeric_value_name("mk_flir_fff_gpsinfo_0",
                                                 0x0000U, 1U),
                     "Yes");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:flir:fff_gpsinfo",
                                                 0x0000U, 0U),
                     "No");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_flir_fff_rawdata_0",
                                                 0x0010U, 1U),
                     "");

        EXPECT_STREQ(exif_tag_numeric_value_name("mk_jvc0", 0x0003U, 0U),
                     "Low");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_jvc0", 0x0003U, 2U),
                     "Fine");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_jvc0", 0x0004U, 1U), "");

        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ge0", 0x0202U, 1U), "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:ge:main", 0x0202U,
                                                 0U),
                     "Off");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_ge0", 0x0203U, 0U), "");
    }

    TEST(ExifValueNames, DispatchesReconyxMicrosoftMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:microsoft:stitch",
                                                 0x0001U, 4U),
                     "3D Rotation");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_microsoft_stitch_0",
                                                 0x0002U, 258U),
                     "Vertical Spherical");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_microsoft_stitch_0",
                                                 0x0003U, 1U),
                     "");

        EXPECT_STREQ(exif_tag_numeric_value_name("mk_motorola0", 0x6420U, 0U),
                     "Normal");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:motorola:main",
                                                 0x6420U, 1U),
                     "Custom");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_motorola0", 0x64D0U, 1U),
                     "");

        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_hyperfire_0",
                                                 0x0006U,
                                                 static_cast<uint64_t>('M')),
                     "Motion Detection");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_hyperfire_0",
                                                 0x0012U, 4U),
                     "Full");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_hyperfire_0",
                                                 0x0028U, 1U),
                     "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_hyperfire_0",
                                                 0x0029U, 1U),
                     "");

        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_ultrafire_0",
                                                 0x0034U,
                                                 static_cast<uint64_t>('P')),
                     "Point and Shoot");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_ultrafire_0",
                                                 0x0042U, 0U),
                     "Sunday");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_ultrafire_0",
                                                 0x0048U, 0U),
                     "Off");

        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_hyperfire2_0",
                                                 0x0034U,
                                                 static_cast<uint64_t>('T')),
                     "Time Lapse");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:reconyx:hyperfire2",
                                                 0x004AU, 6U),
                     "Saturday");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_hyperfire2_0",
                                                 0x005AU, 1U),
                     "On");

        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_microfire_0",
                                                 0x0044U,
                                                 static_cast<uint64_t>('M')),
                     "Motion Sensor");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_microfire_0",
                                                 0x005AU, 1U),
                     "Sunday");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_microfire_0",
                                                 0x0074U, 3U),
                     "Lead Acid");

        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_hyperfire4k_0",
                                                 0x0028U,
                                                 static_cast<uint64_t>('L')),
                     "Cell Live View");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_hyperfire4k_0",
                                                 0x0036U, 7U),
                     "Saturday");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_hyperfire4k_0",
                                                 0x004FU, 4U),
                     "SC10 Solar");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_reconyx_hyperfire4k_0",
                                                 0x004DU, 1U),
                     "");
    }

    TEST(ExifValueNames, DispatchesNintendoSanyoMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nintendo_camerainfo_0",
                                                 0x0030U, 0x1000U),
                     "Mii");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:nintendo:camerainfo",
                                                 0x0030U, 0x4000U),
                     "Woman");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nintendo_camerainfo_0",
                                                 0x0028U, 0U),
                     "");

        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sanyo0", 0x0201U, 0x0105U),
                     "Fine/High");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:sanyo:main",
                                                 0x0201U, 0x0207U),
                     "Super Fine/Super High");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sanyo0", 0x0202U, 1U),
                     "Macro");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sanyo0", 0x020EU, 3U),
                     "Adjust Exposure");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sanyo0", 0x020FU, 1U),
                     "On");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sanyo0", 0x0217U, 1U),
                     "Press start, press stop");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sanyo0", 0x021EU, 0U),
                     "No");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sanyo0", 0x021FU, 6U),
                     "Lamp");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sanyo0", 0x0224U, 2U),
                     "15 frames/s");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sanyo0", 0x0225U, 3U),
                     "Red eye");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sanyo_mov_0", 0x0044U, 4U),
                     "Tungsten");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_sanyo0", 0x0204U, 1U), "");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:sanyo:mp4", 0x006AU,
                                                 100U),
                     "");
    }

}  // namespace
}  // namespace openmeta
