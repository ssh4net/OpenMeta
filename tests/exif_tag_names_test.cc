// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_store.h"

#include <gtest/gtest.h>

#include <string_view>

TEST(ExifTagNames, MapsCommonTags)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("ifd0", 0x010F), std::string_view("Make"));
    EXPECT_EQ(exif_tag_name("ifd1", 0x0201),
              std::string_view("JPEGInterchangeFormat"));
    EXPECT_EQ(exif_tag_name("subifd0", 0x0100), std::string_view("ImageWidth"));

    EXPECT_EQ(exif_tag_name("exififd", 0x9003),
              std::string_view("DateTimeOriginal"));
    EXPECT_EQ(exif_tag_name("exififd", 0x927C), std::string_view("MakerNote"));
    EXPECT_EQ(exif_tag_name("exififd", 0x9287),
              std::string_view("LearningOptOutIn"));
    EXPECT_EQ(exif_tag_name("exififd", 0xA40D),
              std::string_view("DevelopmentType"));
    EXPECT_EQ(exif_tag_name("exififd", 0xA40F),
              std::string_view("DistortionCorrection"));
    EXPECT_EQ(exif_tag_name("exififd", 0xA410),
              std::string_view("ChromaticAberrationCorrection"));
    EXPECT_EQ(exif_tag_name("exififd", 0xA411),
              std::string_view("ShadingCorrection"));
    EXPECT_EQ(exif_tag_name("exififd", 0xA412),
              std::string_view("NoiseReduction"));

    EXPECT_EQ(exif_tag_name("gpsifd", 0x0001),
              std::string_view("GPSLatitudeRef"));
    EXPECT_EQ(exif_tag_name("gpsifd", 0x0002), std::string_view("GPSLatitude"));
    EXPECT_EQ(exif_tag_name("gpsifd", 0x0011),
              std::string_view("GPSImgDirection"));

    EXPECT_EQ(exif_tag_name("ifd0", 0xC4A5), std::string_view("PrintIM"));
    EXPECT_EQ(exif_tag_name("mpf0", 0xB001),
              std::string_view("NumberOfImages"));

    EXPECT_EQ(exif_tag_name("mk_nikon0", 0x0002), std::string_view("ISO"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod300a_0", 0x025c),
              std::string_view("ISO2"));
    EXPECT_TRUE(exif_tag_name("mk_nikon_shotinfod300a_0", 0x0265).empty());
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod300b_0", 0x0265),
              std::string_view("ISO2"));
    EXPECT_EQ(exif_tag_name("mk_canon0", 0x0003),
              std::string_view("CanonFlashInfo"));
    EXPECT_EQ(exif_tag_name("mk_fuji0", 0x1000), std::string_view("Quality"));
    EXPECT_EQ(exif_tag_name("mk_fuji0", 0x2410),
              std::string_view("WB_GRGBLevelsFlash"));
    EXPECT_EQ(exif_tag_name("mk_kodak0", 0x0028),
              std::string_view("Distance1"));
    EXPECT_EQ(exif_tag_name("mk_fuji0", 0x1200),
              std::string_view("FujiFilm_0x1200"));
}


TEST(ExifTagNames, MapsNativeCrwCiffTags)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("ciff_root", 0x2005), std::string_view("RawData"));
    EXPECT_EQ(exif_tag_name("ciff_2804_0", 0x0805),
              std::string_view("CanonFileDescription"));
    EXPECT_EQ(exif_tag_name("ciff_2807_1", 0x080A),
              std::string_view("MakeModel"));
    EXPECT_EQ(exif_tag_name("ciff_2807_1", 0x0810),
              std::string_view("OwnerName"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2", 0x0816),
              std::string_view("OriginalFileName"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2", 0x1803),
              std::string_view("ImageFormat"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2", 0x1806),
              std::string_view("SelfTimerTime"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2", 0x1810),
              std::string_view("ImageInfo"));
    EXPECT_EQ(exif_tag_name("ciff_3003_3", 0x1814),
              std::string_view("MeasuredEV"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3", 0x101C),
              std::string_view("BaseISO"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3", 0x080C),
              std::string_view("ComponentVersion"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3", 0x1834),
              std::string_view("CanonModelID"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3", 0x1835),
              std::string_view("DecoderTable"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3", 0x183B),
              std::string_view("SerialNumberFormat"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4", 0x1028),
              std::string_view("CanonFlashInfo"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4", 0x1029),
              std::string_view("FocalLength"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4", 0x102A),
              std::string_view("CanonShotInfo"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4", 0x1030),
              std::string_view("WhiteSample"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4", 0x10B5),
              std::string_view("RawJpgInfo"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2_timestamp", 0x0001),
              std::string_view("TimeZoneCode"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2_imageformat", 0x0001),
              std::string_view("TargetCompressionRatio"));
    EXPECT_EQ(exif_tag_name("ciff_2807_1_makemodel", 0x0000),
              std::string_view("Make"));
    EXPECT_EQ(exif_tag_name("ciff_2807_1_makemodel", 0x0006),
              std::string_view("Model"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2_imageinfo", 0x0002),
              std::string_view("PixelAspectRatio"));
    EXPECT_EQ(exif_tag_name("ciff_3002_1", 0x1813),
              std::string_view("FlashInfo"));
    EXPECT_EQ(exif_tag_name("ciff_3002_1_exposureinfo", 0x0001),
              std::string_view("ShutterSpeedValue"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3", 0x1014),
              std::string_view("CanonRaw_0x1014"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4", 0x1026),
              std::string_view("CanonRaw_0x1026"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3", 0x1812),
              std::string_view("CanonRaw_0x1812"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4", 0x1819),
              std::string_view("CanonRaw_0x1819"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2", 0x1805),
              std::string_view("CanonRaw_0x1805"));
    EXPECT_EQ(exif_tag_name("ciff_3002_1_flashinfo", 0x0000),
              std::string_view("FlashGuideNumber"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4_focallength", 0x0002),
              std::string_view("FocalPlaneXSize"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4_shotinfo", 0x0005),
              std::string_view("TargetExposureTime"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3_decodertable", 0x0002),
              std::string_view("CompressedDataOffset"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4_rawjpginfo", 0x0003),
              std::string_view("RawJpgWidth"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4_whitesample", 0x0005),
              std::string_view("WhiteSampleBits"));
}


TEST(ExifTagNames, MapsFlirFffSubtables)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_flir_fff_header_0", 0x0004),
              std::string_view("CreatorSoftware"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_camerainfo_0", 0x0020),
              std::string_view("Emissivity"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_camerainfo_0", 0x00D4),
              std::string_view("CameraModel"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_paletteinfo_0", 0x0050),
              std::string_view("PaletteName"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_rawdata_0", 0x0010),
              std::string_view("RawThermalImageType"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_embeddedimage_0", 0x0010),
              std::string_view("EmbeddedImageType"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_pip_0", 0x0004),
              std::string_view("PiPX1"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_gpsinfo_0", 0x0010),
              std::string_view("GPSLatitude"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_meterlink_0", 0x0060),
              std::string_view("Reading1Value"));
}


TEST(ExifTagNames, MapsHpMainAndTypedSubtables)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_hp0", 0x0200), std::string_view("HP_0x0200"));
    EXPECT_EQ(exif_tag_name("mk_hp0", 0x0300), std::string_view("HP_0x0300"));
    EXPECT_EQ(exif_tag_name("mk_hp_type4_0", 0x000c),
              std::string_view("MaxAperture"));
    EXPECT_EQ(exif_tag_name("mk_hp_type6_0", 0x0058),
              std::string_view("SerialNumber"));
}

TEST(ExifTagNames, MapsGeAndJvcMainPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_ge0", 0x0202), std::string_view("Macro"));
    EXPECT_EQ(exif_tag_name("mk_ge0", 0x0104), std::string_view("GE_0x0104"));
    EXPECT_EQ(exif_tag_name("mk_ge0", 0x0203), std::string_view("GE_0x0203"));
    EXPECT_EQ(exif_tag_name("mk_ge0", 0x0600), std::string_view("GE_0x0600"));

    EXPECT_EQ(exif_tag_name("mk_jvc0", 0x0002),
              std::string_view("CPUVersions"));
    EXPECT_EQ(exif_tag_name("mk_jvc0", 0x0001), std::string_view("JVC_0x0001"));
    EXPECT_EQ(exif_tag_name("mk_jvc0", 0x0004), std::string_view("JVC_0x0004"));
    EXPECT_EQ(exif_tag_name("mk_jvc0", 0x0005), std::string_view("JVC_0x0005"));
}

TEST(ExifTagNames, MapsNintendoMainAndCameraInfoTags)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_nintendo0", 0x1000),
              std::string_view("Nintendo_0x1000"));
    EXPECT_EQ(exif_tag_name("mk_nintendo0", 0x1001),
              std::string_view("Nintendo_0x1001"));
    EXPECT_EQ(exif_tag_name("mk_nintendo0", 0x1101),
              std::string_view("CameraInfo"));
    EXPECT_EQ(exif_tag_name("mk_nintendo_camerainfo_0", 0x0000),
              std::string_view("ModelID"));
}


TEST(ExifTagNames, MapsKodakTypedTablesAndPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_kodak_type2_0", 0x0028),
              std::string_view("KodakModel"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type3_0", 0x0038),
              std::string_view("ExposureTime"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type5_0", 0x0014),
              std::string_view("ExposureTime"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type7_0", 0x0000),
              std::string_view("SerialNumber"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type10_0", 0x0002),
              std::string_view("PreviewImageSize"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type10_0", 0x0015),
              std::string_view("Kodak_Type10_0x0015"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type8_0", 0x0200),
              std::string_view("Kodak_Type8_0x0200"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type11_0", 0x0207),
              std::string_view("KodakModel"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type11_0", 0x0200),
              std::string_view("Kodak_Type11_0x0200"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd1_0", 0x0001),
              std::string_view("Kodak_SubIFD1_0x0001"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd1_0", 0x1007),
              std::string_view("Kodak_SubIFD1_0x1007"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd0_0", 0xFA04),
              std::string_view("Kodak_SubIFD0_0xfa04"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd2_0", 0x6002),
              std::string_view("SceneModeUsed"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd2_0", 0x0001),
              std::string_view("Kodak_SubIFD2_0x0001"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd5_0", 0x0033),
              std::string_view("Kodak_SubIFD5_0x0033"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd5_0", 0x0047),
              std::string_view("Kodak_SubIFD5_0x0047"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd255_0", 0xFA75),
              std::string_view("Kodak_SubIFD0_0xfa75"));
    EXPECT_EQ(exif_tag_name("mk_kodak_camerainfo_0", 0xFA01),
              std::string_view("Kodak_CameraInfo_0xfa01"));
    EXPECT_EQ(exif_tag_name("mk_kodak_camerainfo_0", 0xFF00),
              std::string_view("Kodak_CameraInfo_0xff00"));
}

TEST(ExifTagNames, MapsAppleMainTagsAndPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_apple0", 0x0001),
              std::string_view("MakerNoteVersion"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x000D),
              std::string_view("Apple_0x000d"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x001F),
              std::string_view("Apple_0x001f"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x0023),
              std::string_view("Apple_0x0023"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x002D),
              std::string_view("Apple_0x002d"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x002E),
              std::string_view("Apple_0x002e"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x0045),
              std::string_view("FrontFacingCamera"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x0050),
              std::string_view("Apple_0x0050"));
}

TEST(ExifTagNames, MapsFujifilmMainPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_fuji0", 0x1026),
              std::string_view("FujiFilm_0x1026"));
    EXPECT_EQ(exif_tag_name("mk_fuji0", 0x1303),
              std::string_view("FujiFilm_0x1303"));
    EXPECT_EQ(exif_tag_name("mk_fuji0", 0x1430),
              std::string_view("FujiFilm_0x1430"));
}

TEST(ExifTagNames, MapsNativeRafTags)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("raf_header", 0x003c),
              std::string_view("FirmwareVersion"));
    EXPECT_EQ(exif_tag_name("raf_header", 0x0064),
              std::string_view("FujiIFDOffset"));
    EXPECT_EQ(exif_tag_name("raf_0", 0x0100),
              std::string_view("RawImageFullSize"));
    EXPECT_EQ(exif_tag_name("raf_0", 0x0110),
              std::string_view("RawImageCropTopLeft"));
    EXPECT_EQ(exif_tag_name("raf_0", 0xc000), std::string_view("RAFData"));
    EXPECT_EQ(exif_tag_name("raf_0", 0x9999),
              std::string_view("FujiFilmRAF_0x9999"));
}

TEST(ExifTagNames, MapsNativeX3fTags)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("x3f_header", 0x0001),
              std::string_view("FileVersion"));
    EXPECT_EQ(exif_tag_name("x3f_header", 0x0007),
              std::string_view("ImageWidth"));
    EXPECT_EQ(exif_tag_name("x3f_header", 0x000a),
              std::string_view("WhiteBalance"));
    EXPECT_EQ(exif_tag_name("x3f_header_ext", 0x0001),
              std::string_view("ExposureAdjust"));
    EXPECT_EQ(exif_tag_name("x3f_header_ext", 0x000a),
              std::string_view("X3FillLight"));
    EXPECT_EQ(exif_tag_name("x3f_prop", 0x0007), std::string_view("Make"));
    EXPECT_EQ(exif_tag_name("x3f_prop", 0x0015),
              std::string_view("SensorTemperature"));
    EXPECT_EQ(exif_tag_name("x3f_prop", 0x9999),
              std::string_view("SigmaX3F_0x9999"));
}

TEST(ExifTagNames, MapsDjiMainAndThermalTables)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_dji0", 0x0001), std::string_view("Make"));
    EXPECT_EQ(exif_tag_name("mk_dji0", 0x0002), std::string_view("DJI_0x0002"));
    EXPECT_EQ(exif_tag_name("mk_dji0", 0x0010), std::string_view("DJI_0x0010"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams_0", 0x0044),
              std::string_view("ObjectDistance"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams2_0", 0x0000),
              std::string_view("AmbientTemperature"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams2_0", 0x0004),
              std::string_view("ObjectDistance"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams2_0", 0x000C),
              std::string_view("RelativeHumidity"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams2_0", 0x0010),
              std::string_view("ReflectedTemperature"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams2_0", 0x0065),
              std::string_view("IDString"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams3_0", 0x0004),
              std::string_view("RelativeHumidity"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams3_0", 0x0006),
              std::string_view("ObjectDistance"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams3_0", 0x0008),
              std::string_view("Emissivity"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams3_0", 0x000A),
              std::string_view("ReflectedTemperature"));
}

TEST(ExifTagNames, MapsPhaseOneMainAndSensorCalibrationPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_phaseone0", 0x0100),
              std::string_view("CameraOrientation"));
    EXPECT_EQ(exif_tag_name("mk_phaseone0", 0x0101),
              std::string_view("PhaseOne_0x0101"));
    EXPECT_EQ(exif_tag_name("mk_phaseone_sensorcalibration_0", 0x0400),
              std::string_view("SensorDefects"));
    EXPECT_EQ(exif_tag_name("mk_phaseone_sensorcalibration_0", 0x0402),
              std::string_view("SensorCalibration_0x0402"));
}


TEST(ExifTagNames, MapsMotorolaMainAndPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x5500),
              std::string_view("BuildNumber"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x5501),
              std::string_view("SerialNumber"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x6420),
              std::string_view("CustomRendered"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x64D0),
              std::string_view("DriveMode"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x665E),
              std::string_view("Sensor"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x6705),
              std::string_view("ManufactureDate"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x5540),
              std::string_view("Motorola_0x5540"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x6400),
              std::string_view("Motorola_0x6400"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x6703),
              std::string_view("Motorola_0x6703"));
}

TEST(ExifTagNames, MapsSonyMainPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_sony0", 0x1003),
              std::string_view("Sony_0x1003"));
    EXPECT_EQ(exif_tag_name("mk_sony0", 0x2000),
              std::string_view("Sony_0x2000"));
    EXPECT_EQ(exif_tag_name("mk_sony0", 0x2003),
              std::string_view("Sony_0x2003"));
    EXPECT_EQ(exif_tag_name("mk_sony0", 0x2025),
              std::string_view("Sony_0x2025"));
    EXPECT_EQ(exif_tag_name("mk_sony0", 0x5002),
              std::string_view("Sony_0x5002"));
}

TEST(ExifTagNames, MapsRicohMainAndSubtablePlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_ricoh0", 0x0006),
              std::string_view("Ricoh_0x0006"));
    EXPECT_EQ(exif_tag_name("mk_ricoh0", 0x1002),
              std::string_view("DriveMode"));
    EXPECT_EQ(exif_tag_name("mk_ricoh_subdir_0", 0x0007),
              std::string_view("Ricoh_Subdir_0x0007"));
    EXPECT_EQ(exif_tag_name("mk_ricoh_imageinfo_0", 0x0003),
              std::string_view("Ricoh_ImageInfo_0x0003"));
    EXPECT_EQ(exif_tag_name("mk_ricoh_thetasubdir_0", 0x0003),
              std::string_view("Accelerometer"));
    EXPECT_EQ(exif_tag_name("mk_ricoh_thetasubdir_0", 0x1001),
              std::string_view("Ricoh_ThetaSubdir_0x1001"));
    EXPECT_EQ(exif_tag_name("mk_ricoh_type2_0", 0x0104),
              std::string_view("Ricoh_Type2_0x0104"));
}

TEST(ExifTagNames, MapsPentaxMainAndSubtablePlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_pentax0", 0x003E),
              std::string_view("PreviewImageBorders"));
    EXPECT_EQ(exif_tag_name("mk_pentax0", 0x000B),
              std::string_view("PictureMode"));
    EXPECT_EQ(exif_tag_name("mk_pentax0", 0x005E),
              std::string_view("Pentax_0x005e"));
    EXPECT_EQ(exif_tag_name("mk_pentax0", 0x0227),
              std::string_view("Pentax_0x0227"));
    EXPECT_EQ(exif_tag_name("mk_pentax_aeinfo_0", 0x0006),
              std::string_view("AEProgramMode"));
    EXPECT_EQ(exif_tag_name("mk_pentax_aeinfo_0", 0x000C),
              std::string_view("AEMeteringMode"));
    EXPECT_EQ(exif_tag_name("mk_pentax_flashinfo_0", 0x0002),
              std::string_view("ExternalFlashMode"));
    EXPECT_EQ(exif_tag_name("mk_pentax_type2_0", 0x0005),
              std::string_view("Pentax_Type2_0x0005"));
    EXPECT_EQ(exif_tag_name("mk_pentax_type2_0", 0x0007),
              std::string_view("WhiteBalance"));
    EXPECT_EQ(exif_tag_name("mk_pentax_faceinfo_0", 0x0001),
              std::string_view("Pentax_0x0001"));
    EXPECT_EQ(exif_tag_name("mk_pentax_facepos_0", 0x0011),
              std::string_view("Pentax_0x0011"));
    EXPECT_EQ(exif_tag_name("mk_pentax_colorinfo_0", 0x000A),
              std::string_view("Pentax_0x000a"));
}

TEST(ExifTagNames, MapsCasioType2Placeholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_casio0", 0x0E00), std::string_view("PrintIM"));
    EXPECT_EQ(exif_tag_name("mk_casio_type2_0", 0x2002),
              std::string_view("Casio_Type2_0x2002"));
    EXPECT_EQ(exif_tag_name("mk_casio_type2_0", 0x2003),
              std::string_view("Casio_Type2_0x2003"));
    EXPECT_EQ(exif_tag_name("mk_casio_type2_0", 0x3005),
              std::string_view("Casio_Type2_0x3005"));
}

TEST(ExifTagNames, MapsSamsungIfdAndType2Placeholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_samsung_ifd_0", 0x0002),
              std::string_view("Samsung_IFD_0x0002"));
    EXPECT_EQ(exif_tag_name("mk_samsung_ifd_0", 0x0006),
              std::string_view("Samsung_IFD_0x0006"));
    EXPECT_EQ(exif_tag_name("mk_samsung_type2_0", 0x0060),
              std::string_view("Samsung_Type2_0x0060"));
    EXPECT_EQ(exif_tag_name("mk_samsung_type2_0", 0x00E1),
              std::string_view("Samsung_Type2_0x00e1"));
    EXPECT_EQ(exif_tag_name("mk_samsung_type2_0", 0xA002),
              std::string_view("SerialNumber"));
}

TEST(ExifTagNames, MapsMinoltaMainPlaceholdersAndImageSizeAlias)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_minolta0", 0x0103),
              std::string_view("MinoltaImageSize"));
    EXPECT_EQ(exif_tag_name("mk_minolta0", 0x0106),
              std::string_view("Minolta_0x0106"));
    EXPECT_EQ(exif_tag_name("mk_minolta0", 0x010D),
              std::string_view("Minolta_0x010d"));
    EXPECT_EQ(exif_tag_name("mk_minolta0", 0x0200),
              std::string_view("Minolta_0x0200"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectCasioLegacyType2Compat)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry quality;
    quality.key = make_exif_tag_key(store.arena(), "mk_casio_type2_0", 0x0002);
    quality.origin.block = block;
    quality.flags |= EntryFlags::ContextualName;
    quality.origin.name_context_kind = EntryNameContextKind::CasioType2Legacy;
    quality.origin.name_context_variant = 1U;
    const EntryId quality_id            = store.add_entry(quality);
    ASSERT_NE(quality_id, openmeta::kInvalidEntryId);

    Entry placeholder;
    placeholder.key = make_exif_tag_key(store.arena(), "mk_casio_type2_0",
                                        0x0008);
    placeholder.origin.block = block;
    placeholder.flags |= EntryFlags::ContextualName;
    placeholder.origin.name_context_kind
        = EntryNameContextKind::CasioType2Legacy;
    placeholder.origin.name_context_variant = 1U;
    const EntryId placeholder_id            = store.add_entry(placeholder);
    ASSERT_NE(placeholder_id, openmeta::kInvalidEntryId);

    Entry printim;
    printim.key = make_exif_tag_key(store.arena(), "mk_casio_type2_0", 0x0E00);
    printim.origin.block = block;
    printim.flags |= EntryFlags::ContextualName;
    printim.origin.name_context_kind = EntryNameContextKind::CasioType2Legacy;
    printim.origin.name_context_variant = 1U;
    const EntryId printim_id            = store.add_entry(printim);
    ASSERT_NE(printim_id, openmeta::kInvalidEntryId);

    Entry placeholder_001c;
    placeholder_001c.key = make_exif_tag_key(store.arena(), "mk_casio_type2_0",
                                             0x001C);
    placeholder_001c.origin.block = block;
    placeholder_001c.flags |= EntryFlags::ContextualName;
    placeholder_001c.origin.name_context_kind
        = EntryNameContextKind::CasioType2Legacy;
    placeholder_001c.origin.name_context_variant = 1U;
    const EntryId placeholder_001c_id = store.add_entry(placeholder_001c);
    ASSERT_NE(placeholder_001c_id, openmeta::kInvalidEntryId);

    Entry placeholder_001d;
    placeholder_001d.key = make_exif_tag_key(store.arena(), "mk_casio_type2_0",
                                             0x001D);
    placeholder_001d.origin.block = block;
    placeholder_001d.flags |= EntryFlags::ContextualName;
    placeholder_001d.origin.name_context_kind
        = EntryNameContextKind::CasioType2Legacy;
    placeholder_001d.origin.name_context_variant = 1U;
    const EntryId placeholder_001d_id = store.add_entry(placeholder_001d);
    ASSERT_NE(placeholder_001d_id, openmeta::kInvalidEntryId);

    Entry placeholder_001a;
    placeholder_001a.key = make_exif_tag_key(store.arena(), "mk_casio_type2_0",
                                             0x001A);
    placeholder_001a.origin.block = block;
    placeholder_001a.flags |= EntryFlags::ContextualName;
    placeholder_001a.origin.name_context_kind
        = EntryNameContextKind::CasioType2Legacy;
    placeholder_001a.origin.name_context_variant = 1U;
    const EntryId placeholder_001a_id = store.add_entry(placeholder_001a);
    ASSERT_NE(placeholder_001a_id, openmeta::kInvalidEntryId);

    Entry placeholder_001e;
    placeholder_001e.key = make_exif_tag_key(store.arena(), "mk_casio_type2_0",
                                             0x001E);
    placeholder_001e.origin.block = block;
    placeholder_001e.flags |= EntryFlags::ContextualName;
    placeholder_001e.origin.name_context_kind
        = EntryNameContextKind::CasioType2Legacy;
    placeholder_001e.origin.name_context_variant = 1U;
    const EntryId placeholder_001e_id = store.add_entry(placeholder_001e);
    ASSERT_NE(placeholder_001e_id, openmeta::kInvalidEntryId);

    Entry placeholder_2023;
    placeholder_2023.key = make_exif_tag_key(store.arena(), "mk_casio_type2_0",
                                             0x2023);
    placeholder_2023.origin.block = block;
    placeholder_2023.flags |= EntryFlags::ContextualName;
    placeholder_2023.origin.name_context_kind
        = EntryNameContextKind::CasioType2Legacy;
    placeholder_2023.origin.name_context_variant = 1U;
    const EntryId placeholder_2023_id = store.add_entry(placeholder_2023);
    ASSERT_NE(placeholder_2023_id, openmeta::kInvalidEntryId);

    const Entry& quality_entry = store.entry(quality_id);
    EXPECT_EQ(exif_entry_name(store, quality_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("PreviewImageSize"));
    EXPECT_EQ(exif_entry_name(store, quality_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Quality"));

    const Entry& placeholder_entry = store.entry(placeholder_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("QualityMode"));
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Casio_0x0008"));

    const Entry& printim_entry = store.entry(printim_id);
    EXPECT_EQ(exif_entry_name(store, printim_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Casio_Type2_0x0e00"));
    EXPECT_EQ(exif_entry_name(store, printim_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("PrintIM"));

    const Entry& placeholder_001c_entry = store.entry(placeholder_001c_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_001c_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Casio_Type2_0x001c"));
    EXPECT_EQ(exif_entry_name(store, placeholder_001c_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Casio_0x001c"));

    const Entry& placeholder_001d_entry = store.entry(placeholder_001d_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_001d_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("FocalLength"));
    EXPECT_EQ(exif_entry_name(store, placeholder_001d_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Casio_0x001d"));

    const Entry& placeholder_001a_entry = store.entry(placeholder_001a_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_001a_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Casio_Type2_0x001a"));
    EXPECT_EQ(exif_entry_name(store, placeholder_001a_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Casio_0x001a"));

    const Entry& placeholder_001e_entry = store.entry(placeholder_001e_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_001e_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Casio_Type2_0x001e"));
    EXPECT_EQ(exif_entry_name(store, placeholder_001e_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Casio_0x001e"));

    const Entry& placeholder_2023_entry = store.entry(placeholder_2023_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_2023_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Casio_Type2_0x2023"));
    EXPECT_EQ(exif_entry_name(store, placeholder_2023_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Casio_0x2023"));
}

TEST(ExifTagNames, MapsNikonSettingsMainAndPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_nikonsettings_main_0", 0x0001),
              std::string_view("ISOAutoHiLimit"));
    EXPECT_EQ(exif_tag_name("mk_nikonsettings_main_0", 0x0046),
              std::string_view("OpticalVR"));
    EXPECT_EQ(exif_tag_name("mk_nikonsettings_main_0", 0x0016),
              std::string_view("NikonSettings_0x0016"));
    EXPECT_EQ(exif_tag_name("mk_nikonsettings_main_0", 0x0107),
              std::string_view("NikonSettings_0x0107"));
    EXPECT_EQ(exif_tag_name("mk_nikonsettings_main_0", 0x00FA),
              std::string_view("NikonSettings_0x00fa"));
}

TEST(ExifTagNames, MapsNikonMainPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_nikon0", 0x0002), std::string_view("ISO"));
    EXPECT_EQ(exif_tag_name("mk_nikon0", 0x0025), std::string_view("ISOInfo"));
    EXPECT_EQ(exif_tag_name("mk_nikon0", 0x008A),
              std::string_view("Nikon_0x008a"));
    EXPECT_EQ(exif_tag_name("mk_nikon0", 0x00A4),
              std::string_view("Nikon_0x00a4"));
    EXPECT_EQ(exif_tag_name("mk_nikon0", 0x00C0),
              std::string_view("Nikon_0x00c0"));
}

TEST(ExifTagNames, MapsNikonLensdata0800CompatNames)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_nikon_lensdata0100_0", 0x000A),
              std::string_view("MaxApertureAtMinFocal"));
    EXPECT_EQ(exif_tag_name("mk_nikon_lensdata0100_0", 0x000B),
              std::string_view("MaxApertureAtMaxFocal"));
    EXPECT_EQ(exif_tag_name("mk_nikon_lensdata0100_0", 0x000C),
              std::string_view("MCUVersion"));
    EXPECT_EQ(exif_tag_name("mk_nikon_lensdata0101_0", 0x000B),
              std::string_view("LensIDNumber"));
    EXPECT_EQ(exif_tag_name("mk_nikon_lensdata0101_0", 0x0010),
              std::string_view("MaxApertureAtMaxFocal"));
    EXPECT_EQ(exif_tag_name("mk_nikon_lensdata0201_0", 0x000B),
              std::string_view("LensIDNumber"));
    EXPECT_EQ(exif_tag_name("mk_nikon_lensdata0201_0", 0x0012),
              std::string_view("EffectiveMaxAperture"));
    EXPECT_EQ(exif_tag_name("mk_nikon_lensdata0800_0", 0x0035),
              std::string_view("LensMountType"));
}

TEST(ExifTagNames, MapsNikonCustomDslrTables)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_nikoncustom_settingsd4_0", 0x002A),
              std::string_view("VerticalMultiSelector"));
    EXPECT_EQ(exif_tag_name("mk_nikoncustom_settingsd5_0", 0x0043),
              std::string_view("VerticalFuncButton"));
    EXPECT_EQ(exif_tag_name("mk_nikoncustom_settingsd500_0", 0x0043),
              std::string_view("AssignMB-D17FuncButton"));
    EXPECT_EQ(exif_tag_name("mk_nikoncustom_settingsd610_0", 0x0013),
              std::string_view("SelfTimerTime"));
    EXPECT_EQ(exif_tag_name("mk_nikoncustom_settingsd7000_0", 0x0017),
              std::string_view("FlashControlBuilt-in"));
    EXPECT_EQ(exif_tag_name("mk_nikoncustom_settingsd800_0", 0x001C),
              std::string_view("CommanderGroupAMode"));
    EXPECT_EQ(exif_tag_name("mk_nikoncustom_settingsd810_0", 0x0028),
              std::string_view("MovieAELockButtonAssignment"));
    EXPECT_EQ(exif_tag_name("mk_nikoncustom_settingsd850_0", 0x0043),
              std::string_view("AssignMB-D18FuncButton"));
    EXPECT_EQ(exif_tag_name("mk_nikon0", 0x0024),
              std::string_view("WorldTime"));
    EXPECT_EQ(exif_tag_name("mk_nikon_menusettingsd850_0", 0x06DD),
              std::string_view("PhotoShootingMenuBankImageArea"));
    EXPECT_EQ(exif_tag_name("mk_nikon_moresettingsd850_0", 0x0025),
              std::string_view("PrimarySlot"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod850_0", 0x0024),
              std::string_view("PhotoShootingMenuBank"));
    EXPECT_EQ(exif_tag_name("mk_nikoncustom_settingsz8_0", 0x0001),
              std::string_view("CustomSettingsBank"));
    EXPECT_EQ(exif_tag_name("mk_nikoncustom_settingsz8_0", 0x0203),
              std::string_view("MovieAFAreaMode"));
    EXPECT_EQ(exif_tag_name("mk_nikon_menusettingsz8_0", 0x011E),
              std::string_view("PhotoShootingMenuBank"));
    EXPECT_EQ(exif_tag_name("mk_nikon_menusettingsz8_0", 0x0152),
              std::string_view("AutoISO"));
    EXPECT_EQ(exif_tag_name("mk_nikon_menusettingsz8_0", 0x022E),
              std::string_view("BracketIncrement"));
    EXPECT_EQ(exif_tag_name("mk_nikon_menusettingsz8v1_0", 0x0692),
              std::string_view("Language"));
    EXPECT_EQ(exif_tag_name("mk_nikon_menusettingsz8v1_0", 0x0710),
              std::string_view("HDMIOutputResolution"));
    EXPECT_EQ(exif_tag_name("mk_nikon_colorbalancec_0", 0x0020),
              std::string_view("BlackLevel"));
    EXPECT_EQ(exif_tag_name("mk_nikon_colorbalancec_0", 0x0114),
              std::string_view("WB_RGGBLevelsAuto"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod40_0", 0x0246),
              std::string_view("ShutterCount"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod40_0", 0x024A),
              std::string_view("VibrationReduction"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod80_0", 0x024A),
              std::string_view("ShutterCount"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod80_0", 0x024E),
              std::string_view("VibrationReduction"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod300s_0", 0x0265),
              std::string_view("ISO2"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod300s_0", 0x0286),
              std::string_view("ShutterCount"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod3x_0", 0x0280),
              std::string_view("ShutterCount"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod4s_0", 0x01D0),
              std::string_view("SecondarySlotFunction"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod7000_0", 0x0320),
              std::string_view("ShutterCount"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod810_0", 0x013C),
              std::string_view("SecondarySlotFunction"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shotinfod6_0", 0x0024),
              std::string_view("NumberOffsets"));
    EXPECT_EQ(exif_tag_name("mk_nikon_seqinfoz9_0", 0x0020),
              std::string_view("FocusShiftShooting"));
    EXPECT_EQ(exif_tag_name("mk_nikon_seqinfod6_0", 0x0024),
              std::string_view("IntervalShooting"));
    EXPECT_EQ(exif_tag_name("mk_nikon_intervalinfod6_0", 0x01A8),
              std::string_view("FocusShiftNumberShots"));
    EXPECT_EQ(exif_tag_name("mk_nikon_intervalinfod6_0", 0x0214),
              std::string_view("FlashControlMode"));
    EXPECT_EQ(exif_tag_name("mk_nikon_rotationinfod500_0", 0x001A),
              std::string_view("Rotation"));
    EXPECT_EQ(exif_tag_name("mk_nikon_rotationinfod500_0", 0x0532),
              std::string_view("FlickerReductionIndicator"));
    EXPECT_EQ(exif_tag_name("mk_nikon_jpginfod500_0", 0x0024),
              std::string_view("JPGCompression"));
    EXPECT_EQ(exif_tag_name("mk_nikon_bracketinginfod500_0", 0x0017),
              std::string_view("ADLBracketingStep"));
    EXPECT_EQ(exif_tag_name("mk_nikon_shootingmenud500_0", 0x0004),
              std::string_view("ISOAutoShutterTime"));
    EXPECT_EQ(exif_tag_name("mk_nikon_otherinfod500_0", 0x0214),
              std::string_view("NikonMeteringMode"));
    EXPECT_EQ(exif_tag_name("mk_nikon_nefinfo_0", 0x0005),
              std::string_view("DistortionInfo"));
    EXPECT_EQ(exif_tag_name("mk_nikon_nefinfo_0", 0x0006),
              std::string_view("VignetteInfo"));
    EXPECT_EQ(exif_tag_name("mk_nikon_nefinfo_0", 0x000B),
              std::string_view("Nikon_NEFInfo_0x000b"));
    EXPECT_EQ(exif_tag_name("mk_nikon_nefinfo_0", 0x0013),
              std::string_view("Nikon_NEFInfo_0x0013"));
}

TEST(ExifTagNames, MapsNikonFlashInfoCompatAliases)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_nikon_flashinfo0100_0", 0x0027),
              std::string_view("FlashOutput"));
    EXPECT_EQ(exif_tag_name("mk_nikon_flashinfo0100_0", 0x0028),
              std::string_view("FlashGroupAOutput"));
    EXPECT_EQ(exif_tag_name("mk_nikon_flashinfo0100_0", 0x0029),
              std::string_view("FlashGroupBOutput"));
    EXPECT_EQ(exif_tag_name("mk_nikon_flashinfo0106_0", 0x0013),
              std::string_view("FlashGroupAOutput"));
    EXPECT_EQ(exif_tag_name("mk_nikon_flashinfo0106_0", 0x0014),
              std::string_view("FlashGroupBOutput"));
    EXPECT_EQ(exif_tag_name("mk_nikon_flashinfo0106_0", 0x0015),
              std::string_view("FlashGroupCOutput"));
}

TEST(ExifTagNames, MapsPanasonicMainPlaceholdersAndLegacyFallbacks)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_panasonic0", 0x0022),
              std::string_view("Panasonic_0x0022"));
    EXPECT_EQ(exif_tag_name("mk_panasonic0", 0x0037),
              std::string_view("Panasonic_0x0037"));
    EXPECT_EQ(exif_tag_name("mk_panasonic0", 0x0058),
              std::string_view("ThumbnailWidth"));
    EXPECT_EQ(exif_tag_name("mk_panasonic0", 0x00DE),
              std::string_view("AFAreaSize"));
}


TEST(ExifTagNames, FlirMainUnknownTagsUseStablePlaceholderNames)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_flir0", 0x0007),
              std::string_view("FLIR_0x0007"));
    EXPECT_EQ(exif_tag_name("mk_flir0", 0x000A),
              std::string_view("FLIR_0x000a"));
    EXPECT_EQ(exif_tag_name("mk_flir0", 0x0112),
              std::string_view("FLIR_0x0112"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_gpsinfo_0", 0x0008),
              std::string_view("GPSLatitudeRef"));
}


TEST(ExifTagNames, UnknownReturnsEmpty)
{
    using openmeta::exif_tag_name;

    EXPECT_TRUE(exif_tag_name("gpsifd", 0xFFFF).empty());
    EXPECT_TRUE(exif_tag_name("unknown_ifd", 0x010F).empty());
}


TEST(ExifTagNames, MakerNoteSubtableFallsBackToVendorMain)
{
    using openmeta::exif_tag_name;

    // This tag is known in Olympus main table but not all Olympus subtables.
    EXPECT_EQ(exif_tag_name("mk_olympus_equipment0", 0x0040),
              std::string_view("CompressedImageSize"));
}


TEST(ExifTagNames, OlympusFeTagsFallBackToKnownOlympusSubtables)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x0101),
              std::string_view("Olympus_FETags_0x0101"));
    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x0201),
              std::string_view("Olympus_FETags_0x0201"));
    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x020A),
              std::string_view("Olympus_FETags_0x020A"));
    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x0306),
              std::string_view("Olympus_FETags_0x0306"));
    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x030A),
              std::string_view("Olympus_FETags_0x030A"));
    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x0311),
              std::string_view("CoringValues"));
    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x1204),
              std::string_view("ExternalFlashBounce"));
}


TEST(ExifTagNames, OlympusMissingSubtableTagsUseStablePlaceholderNames)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_olympus_camerasettings_0", 0x0402),
              std::string_view("Olympus_CameraSettings_0x0402"));
    EXPECT_EQ(exif_tag_name("mk_olympus_focusinfo_0", 0x020B),
              std::string_view("Olympus_FocusInfo_0x020B"));
    EXPECT_EQ(exif_tag_name("mk_olympus_focusinfo_0", 0x0213),
              std::string_view("Olympus_FocusInfo_0x0213"));
    EXPECT_EQ(exif_tag_name("mk_olympus_focusinfo_0", 0x2100),
              std::string_view("Olympus_FocusInfo_0x2100"));
    EXPECT_EQ(exif_tag_name("mk_olympus_unknowninfo_0", 0x0401),
              std::string_view("Olympus_UnknownInfo_0x0401"));
    EXPECT_EQ(exif_tag_name("mk_olympus_camerasettings_0", 0x030A),
              std::string_view("Olympus_CameraSettings_0x030A"));
    EXPECT_EQ(exif_tag_name("mk_olympus_camerasettings_0", 0x030B),
              std::string_view("Olympus_CameraSettings_0x030B"));
    EXPECT_EQ(exif_tag_name("mk_olympus_camerasettings_0", 0x0821),
              std::string_view("Olympus_CameraSettings_0x0821"));
    EXPECT_EQ(exif_tag_name("mk_olympus_imageprocessing_0", 0x1000),
              std::string_view("Olympus_ImageProcessing_0x1000"));
    EXPECT_EQ(exif_tag_name("mk_olympus_imageprocessing_0", 0x2110),
              std::string_view("Olympus_ImageProcessing_0x2110"));
    EXPECT_EQ(exif_tag_name("mk_olympus_imageprocessing_0", 0x1115),
              std::string_view("Olympus_ImageProcessing_0x1115"));
    EXPECT_EQ(exif_tag_name("mk_olympus_rawdevelopment2_0", 0x0114),
              std::string_view("Olympus_RawDevelopment2_0x0114"));
    EXPECT_EQ(exif_tag_name("mk_olympus0", 0x0225),
              std::string_view("Olympus_0x0225"));
    EXPECT_EQ(exif_tag_name("mk_olympus0", 0x0400),
              std::string_view("Olympus_0x0400"));
    EXPECT_EQ(exif_tag_name("mk_olympus_unknowninfo_0", 0x2100),
              std::string_view("Olympus_UnknownInfo_0x2100"));
}


TEST(ExifTagNames, CanonMainUnknownTagsUseStablePlaceholderNames)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_canon0", 0x001F),
              std::string_view("Canon_0x001f"));
    EXPECT_EQ(exif_tag_name("mk_canon0", 0x0033),
              std::string_view("Canon_0x0033"));
    EXPECT_EQ(exif_tag_name("mk_canon0", 0x4017),
              std::string_view("Canon_0x4017"));
}


TEST(ExifTagNames, CanonModelFamiliesDoNotFallBackToMainTableNames)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_canon_colordata7_0", 0x0080),
              std::string_view("WB_RGGBLevelsDaylight"));
    EXPECT_TRUE(exif_tag_name("mk_canon_colordata7_0", 0x0095).empty());
    EXPECT_EQ(exif_tag_name("mk_canon_colordata7_0", 0x00D0),
              std::string_view("WB_RGGBLevelsUnknown20"));

    EXPECT_EQ(exif_tag_name("mk_canon_colordata12_0", 0x0073),
              std::string_view("WB_RGGBLevelsMeasured"));
    EXPECT_EQ(exif_tag_name("mk_canon_colordata12_0", 0x016B),
              std::string_view("PerChannelBlackLevel"));
    EXPECT_EQ(exif_tag_name("mk_canon_colordata12_0", 0x0280),
              std::string_view("NormalWhiteLevel"));
    EXPECT_TRUE(exif_tag_name("mk_canon_colordata12_0", 0x0083).empty());
    EXPECT_TRUE(exif_tag_name("mk_canon_colordata12_0", 0x010C).empty());
    EXPECT_EQ(exif_tag_name("mk_canon_camerainfo1100d_0", 0x019B),
              std::string_view("FirmwareVersion"));
}


TEST(ExifTagNames, CanonCustomFunctions2UnknownTagsUseStablePlaceholderNames)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_canoncustom_functions2_0", 0x0115),
              std::string_view("CanonCustom_Functions2_0x0115"));
    EXPECT_EQ(exif_tag_name("mk_canoncustom_functions2_0", 0x081A),
              std::string_view("CanonCustom_Functions2_0x081a"));
    EXPECT_EQ(exif_tag_name("mk_canoncustom_functions5d_0", 0x0015),
              std::string_view("CanonCustom_Functions5D_0x0015"));
    EXPECT_EQ(exif_tag_name("mk_canoncustom_functionsd30_0", 0x0000),
              std::string_view("CanonCustom_FunctionsD30_0x0000"));
}


TEST(ExifTagNames, ContextualEntryNamesKeepCanonicalKeysStable)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry focus_placeholder;
    focus_placeholder.key          = make_exif_tag_key(store.arena(),
                                                       "mk_olympus_focusinfo_0", 0x1600);
    focus_placeholder.origin.block = block;
    focus_placeholder.flags |= EntryFlags::ContextualName;
    focus_placeholder.origin.name_context_kind
        = EntryNameContextKind::OlympusFocusInfo1600;
    focus_placeholder.origin.name_context_variant = 2U;
    const EntryId placeholder_id = store.add_entry(focus_placeholder);
    ASSERT_NE(placeholder_id, openmeta::kInvalidEntryId);

    const Entry& placeholder = store.entry(placeholder_id);
    EXPECT_EQ(exif_entry_name(store, placeholder, ExifTagNamePolicy::Canonical),
              std::string_view("ImageStabilization"));
    EXPECT_EQ(exif_entry_name(store, placeholder,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Olympus_FocusInfo_0x1600"));

    Entry focus_semantic;
    focus_semantic.key          = make_exif_tag_key(store.arena(),
                                                    "mk_olympus_focusinfo_0", 0x1600);
    focus_semantic.origin.block = block;
    focus_semantic.flags |= EntryFlags::ContextualName;
    focus_semantic.origin.name_context_kind
        = EntryNameContextKind::OlympusFocusInfo1600;
    focus_semantic.origin.name_context_variant = 1U;
    const EntryId semantic_id = store.add_entry(focus_semantic);
    ASSERT_NE(semantic_id, openmeta::kInvalidEntryId);

    const Entry& semantic = store.entry(semantic_id);
    EXPECT_EQ(exif_entry_name(store, semantic, ExifTagNamePolicy::Canonical),
              std::string_view("ImageStabilization"));
    EXPECT_EQ(exif_entry_name(store, semantic,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ImageStabilization"));
}


TEST(ExifTagNames, ContextualEntryNamesSelectCanonCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry canon_main;
    canon_main.key = make_exif_tag_key(store.arena(), "mk_canon0", 0x0038);
    canon_main.origin.block = block;
    canon_main.flags |= EntryFlags::ContextualName;
    canon_main.origin.name_context_kind = EntryNameContextKind::CanonMain0038;
    canon_main.origin.name_context_variant = 1U;
    const EntryId canon_main_id            = store.add_entry(canon_main);
    ASSERT_NE(canon_main_id, openmeta::kInvalidEntryId);

    const Entry& canon_main_entry = store.entry(canon_main_id);
    EXPECT_EQ(exif_entry_name(store, canon_main_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("BatteryType"));
    EXPECT_EQ(exif_entry_name(store, canon_main_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Canon_0x0038"));

    const struct {
        std::string_view ifd;
        uint16_t tag;
        EntryNameContextKind kind;
        std::string_view canonical;
        std::string_view compat;
    } canon_compat_cases[] = {
        { "mk_canon_shotinfo_0", 0x000E,
          EntryNameContextKind::CanonShotInfo000E, "AFPointsInFocus",
          "MinFocalLength" },
        { "mk_canon_camerasettings_0", 0x0021,
          EntryNameContextKind::CanonCameraSettings0021, "AESetting",
          "WB_RGGBLevelsKelvin" },
        { "mk_canon_colordata4_0", 0x00DA,
          EntryNameContextKind::CanonColorData4PSInfo, "",
          "UserDef2PictureStyle" },
        { "mk_canon_colordata7_0", 0x00E4,
          EntryNameContextKind::CanonColorData7PSInfo2, "",
          "ColorToneUserDef3" },
        { "mk_canon_colordata7_0", 0x00E8,
          EntryNameContextKind::CanonColorData7PSInfo2, "",
          "FilterEffectUserDef3" },
        { "mk_canon_colordata7_0", 0x00EC,
          EntryNameContextKind::CanonColorData7PSInfo2, "",
          "ToningEffectUserDef3" },
        { "mk_canon_colordata7_0", 0x00F0,
          EntryNameContextKind::CanonColorData7PSInfo2, "",
          "UserDef1PictureStyle" },
        { "mk_canon_colordata7_0", 0x00F2,
          EntryNameContextKind::CanonColorData7PSInfo2, "",
          "UserDef2PictureStyle" },
        { "mk_canon_colordata4_0", 0x00EA,
          EntryNameContextKind::CanonColorData400EA, "",
          "WB_RGGBLevelsUnknown7" },
        { "mk_canon_colordata4_0", 0x00EE,
          EntryNameContextKind::CanonColorData400EE, "", "MaxFocalLength" },
        { "mk_canon_colordata4_0", 0x02CF,
          EntryNameContextKind::CanonColorData402CF, "NormalWhiteLevel",
          "PerChannelBlackLevel" },
        { "mk_canon_colorcalib_0", 0x0038,
          EntryNameContextKind::CanonColorCalib0038, "CameraColorCalibration15",
          "BatteryType" },
        { "mk_canon_camerainfo1d_0", 0x0048,
          EntryNameContextKind::CanonCameraInfo1D0048, "ColorTemperature",
          "Sharpness" },
        { "mk_canon_camerainfo600d_0", 0x00EA,
          EntryNameContextKind::CanonCameraInfo600D00EA, "LensType",
          "MinFocalLength" },
    };

    for (size_t i = 0;
         i < sizeof(canon_compat_cases) / sizeof(canon_compat_cases[0]); ++i) {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), canon_compat_cases[i].ifd,
                                      canon_compat_cases[i].tag);
        entry.origin.block = block;
        entry.flags |= EntryFlags::ContextualName;
        entry.origin.name_context_kind = canon_compat_cases[i].kind;
        if (canon_compat_cases[i].kind
            == EntryNameContextKind::CanonColorData7PSInfo2) {
            switch (canon_compat_cases[i].tag) {
            case 0x00E4U: entry.origin.name_context_variant = 1U; break;
            case 0x00E8U: entry.origin.name_context_variant = 2U; break;
            case 0x00ECU: entry.origin.name_context_variant = 3U; break;
            case 0x00F0U: entry.origin.name_context_variant = 4U; break;
            case 0x00F2U: entry.origin.name_context_variant = 5U; break;
            default: entry.origin.name_context_variant = 1U; break;
            }
        } else {
            entry.origin.name_context_variant = 1U;
        }
        const EntryId id = store.add_entry(entry);
        ASSERT_NE(id, openmeta::kInvalidEntryId);

        const Entry& stored = store.entry(id);
        EXPECT_EQ(exif_entry_name(store, stored, ExifTagNamePolicy::Canonical),
                  canon_compat_cases[i].canonical);
        EXPECT_EQ(exif_entry_name(store, stored,
                                  ExifTagNamePolicy::ExifToolCompat),
                  canon_compat_cases[i].compat);
    }

    Entry iso_expansion;
    iso_expansion.key          = make_exif_tag_key(store.arena(),
                                                   "mk_canoncustom_functions2_0",
                                                   0x0103);
    iso_expansion.origin.block = block;
    iso_expansion.flags |= EntryFlags::ContextualName;
    iso_expansion.origin.name_context_kind
        = EntryNameContextKind::CanonCustomFunctions20103;
    iso_expansion.origin.name_context_variant = 1U;
    const EntryId iso_expansion_id            = store.add_entry(iso_expansion);
    ASSERT_NE(iso_expansion_id, openmeta::kInvalidEntryId);

    const Entry& iso_expansion_entry = store.entry(iso_expansion_id);
    EXPECT_EQ(exif_entry_name(store, iso_expansion_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ISOSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, iso_expansion_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ISOExpansion"));

    Entry iso_speed_range;
    iso_speed_range.key          = make_exif_tag_key(store.arena(),
                                                     "mk_canoncustom_functions2_0",
                                                     0x0103);
    iso_speed_range.origin.block = block;
    iso_speed_range.flags |= EntryFlags::ContextualName;
    iso_speed_range.origin.name_context_kind
        = EntryNameContextKind::CanonCustomFunctions20103;
    iso_speed_range.origin.name_context_variant = 2U;
    const EntryId iso_speed_range_id = store.add_entry(iso_speed_range);
    ASSERT_NE(iso_speed_range_id, openmeta::kInvalidEntryId);

    const Entry& iso_speed_range_entry = store.entry(iso_speed_range_id);
    EXPECT_EQ(exif_entry_name(store, iso_speed_range_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ISOSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, iso_speed_range_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ISOSpeedRange"));

    Entry shutter_speed_range;
    shutter_speed_range.key          = make_exif_tag_key(store.arena(),
                                                         "mk_canoncustom_functions2_0",
                                                         0x010C);
    shutter_speed_range.origin.block = block;
    shutter_speed_range.flags |= EntryFlags::ContextualName;
    shutter_speed_range.origin.name_context_kind
        = EntryNameContextKind::CanonCustomFunctions2010C;
    shutter_speed_range.origin.name_context_variant = 1U;
    const EntryId shutter_speed_range_id = store.add_entry(shutter_speed_range);
    ASSERT_NE(shutter_speed_range_id, openmeta::kInvalidEntryId);

    const Entry& shutter_speed_range_entry = store.entry(
        shutter_speed_range_id);
    EXPECT_EQ(exif_entry_name(store, shutter_speed_range_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ShutterSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, shutter_speed_range_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("CanonCustom_Functions2_0x010c"));

    Entry superimposed_display;
    superimposed_display.key          = make_exif_tag_key(store.arena(),
                                                          "mk_canoncustom_functions2_0",
                                                          0x0510);
    superimposed_display.origin.block = block;
    superimposed_display.flags |= EntryFlags::ContextualName;
    superimposed_display.origin.name_context_kind
        = EntryNameContextKind::CanonCustomFunctions20510;
    superimposed_display.origin.name_context_variant = 1U;
    const EntryId superimposed_display_id            = store.add_entry(
        superimposed_display);
    ASSERT_NE(superimposed_display_id, openmeta::kInvalidEntryId);

    const Entry& superimposed_display_entry = store.entry(
        superimposed_display_id);
    EXPECT_EQ(exif_entry_name(store, superimposed_display_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("VFDisplayIllumination"));
    EXPECT_EQ(exif_entry_name(store, superimposed_display_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("SuperimposedDisplay"));

    Entry shutter_button;
    shutter_button.key          = make_exif_tag_key(store.arena(),
                                                    "mk_canoncustom_functions2_0",
                                                    0x0701);
    shutter_button.origin.block = block;
    shutter_button.flags |= EntryFlags::ContextualName;
    shutter_button.origin.name_context_kind
        = EntryNameContextKind::CanonCustomFunctions20701;
    shutter_button.origin.name_context_variant = 1U;
    const EntryId shutter_button_id = store.add_entry(shutter_button);
    ASSERT_NE(shutter_button_id, openmeta::kInvalidEntryId);

    const Entry& shutter_button_entry = store.entry(shutter_button_id);
    EXPECT_EQ(exif_entry_name(store, shutter_button_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Shutter-AELock"));
    EXPECT_EQ(exif_entry_name(store, shutter_button_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ShutterButtonAFOnButton"));

    Entry af_and_metering;
    af_and_metering.key          = make_exif_tag_key(store.arena(),
                                                     "mk_canoncustom_functions2_0",
                                                     0x0701);
    af_and_metering.origin.block = block;
    af_and_metering.flags |= EntryFlags::ContextualName;
    af_and_metering.origin.name_context_kind
        = EntryNameContextKind::CanonCustomFunctions20701;
    af_and_metering.origin.name_context_variant = 2U;
    const EntryId af_and_metering_id = store.add_entry(af_and_metering);
    ASSERT_NE(af_and_metering_id, openmeta::kInvalidEntryId);

    const Entry& af_and_metering_entry = store.entry(af_and_metering_id);
    EXPECT_EQ(exif_entry_name(store, af_and_metering_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Shutter-AELock"));
    EXPECT_EQ(exif_entry_name(store, af_and_metering_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("AFAndMeteringButtons"));
}


TEST(ExifTagNames, ContextualEntryNamesSelectKodakCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry kodak_model;
    kodak_model.key = make_exif_tag_key(store.arena(), "mk_kodak0", 0x0028);
    kodak_model.origin.block = block;
    kodak_model.flags |= EntryFlags::ContextualName;
    kodak_model.origin.name_context_kind = EntryNameContextKind::KodakMain0028;
    kodak_model.origin.name_context_variant = 1U;
    const EntryId kodak_model_id            = store.add_entry(kodak_model);
    ASSERT_NE(kodak_model_id, openmeta::kInvalidEntryId);

    const Entry& kodak_model_entry = store.entry(kodak_model_id);
    EXPECT_EQ(exif_entry_name(store, kodak_model_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Distance1"));
    EXPECT_EQ(exif_entry_name(store, kodak_model_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("KodakModel"));
}


TEST(ExifTagNames, ContextualEntryNamesSelectMotorolaCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry placeholder;
    placeholder.key = make_exif_tag_key(store.arena(), "mk_motorola0", 0x6420);
    placeholder.origin.block = block;
    placeholder.flags |= EntryFlags::ContextualName;
    placeholder.origin.name_context_kind
        = EntryNameContextKind::MotorolaMain6420;
    placeholder.origin.name_context_variant = 1U;
    const EntryId placeholder_id            = store.add_entry(placeholder);
    ASSERT_NE(placeholder_id, openmeta::kInvalidEntryId);

    const Entry& placeholder_entry = store.entry(placeholder_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("CustomRendered"));
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Motorola_0x6420"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectSonyCompatPlaceholders)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    static constexpr struct {
        uint16_t tag;
        std::string_view canonical;
        std::string_view compat;
    } kCases[] = {
        { 0x201BU, "FocusMode", "Sony_0x201b" },
        { 0x201CU, "AFAreaModeSetting", "Sony_0x201c" },
        { 0x201DU, "FlexibleSpotPosition", "Sony_0x201d" },
        { 0x201EU, "AFPointSelected", "Sony_0x201e" },
        { 0x2020U, "AFPointsUsed", "Sony_0x2020" },
        { 0x2021U, "AFTracking", "Sony_0x2021" },
        { 0x2022U, "FocalPlaneAFPointsUsed", "Sony_0x2022" },
        { 0x205CU, "StepCropShooting", "Sony_0x205c" },
        { 0xB042U, "FocusMode", "Sony_0xb042" },
        { 0xB043U, "AFAreaMode", "Sony_0xb043" },
        { 0xB050U, "HighISONoiseReduction2", "Sony_0xb050" },
    };

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i) {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), "mk_sony0", kCases[i].tag);
        entry.origin.block = block;
        entry.flags |= EntryFlags::ContextualName;
        entry.origin.name_context_kind = EntryNameContextKind::SonyMainCompat;
        entry.origin.name_context_variant = 1U;
        const EntryId id                  = store.add_entry(entry);
        ASSERT_NE(id, openmeta::kInvalidEntryId);

        const Entry& stored = store.entry(id);
        EXPECT_EQ(exif_entry_name(store, stored, ExifTagNamePolicy::Canonical),
                  kCases[i].canonical);
        EXPECT_EQ(exif_entry_name(store, stored,
                                  ExifTagNamePolicy::ExifToolCompat),
                  kCases[i].compat);
    }
}

TEST(ExifTagNames, UsesNikonMenuSettingsZ8CompatNames)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryId;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry entry;
    entry.key = make_exif_tag_key(store.arena(), "mk_nikon_menusettingsz8_0",
                                  0x027C);
    entry.origin.block = block;
    const EntryId id   = store.add_entry(entry);
    ASSERT_NE(id, openmeta::kInvalidBlockId);

    const Entry& stored = store.entry(id);
    EXPECT_EQ(exif_entry_name(store, stored, ExifTagNamePolicy::Canonical),
              std::string_view("HighFrequencyFlickerReduction"));
    EXPECT_EQ(exif_entry_name(store, stored, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("HighFrequencyFlickerReductionShooting"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectFujifilmCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry placeholder;
    placeholder.key = make_exif_tag_key(store.arena(), "mk_fuji0", 0x1304);
    placeholder.origin.block = block;
    placeholder.flags |= EntryFlags::ContextualName;
    placeholder.origin.name_context_kind
        = EntryNameContextKind::FujifilmMain1304;
    placeholder.origin.name_context_variant = 1U;
    const EntryId placeholder_id            = store.add_entry(placeholder);
    ASSERT_NE(placeholder_id, openmeta::kInvalidEntryId);

    const Entry& placeholder_entry = store.entry(placeholder_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("GEImageSize"));
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FujiFilm_0x1304"));

    Entry wb_placeholder;
    wb_placeholder.key = make_exif_tag_key(store.arena(), "mk_fuji0", 0x144A);
    wb_placeholder.origin.block = block;
    wb_placeholder.flags |= EntryFlags::ContextualName;
    wb_placeholder.origin.name_context_kind
        = EntryNameContextKind::FujifilmMain1304;
    wb_placeholder.origin.name_context_variant = 1U;
    const EntryId wb_placeholder_id = store.add_entry(wb_placeholder);
    ASSERT_NE(wb_placeholder_id, openmeta::kInvalidEntryId);

    const Entry& wb_placeholder_entry = store.entry(wb_placeholder_id);
    EXPECT_EQ(exif_entry_name(store, wb_placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("WBRed"));
    EXPECT_EQ(exif_entry_name(store, wb_placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FujiFilm_0x144a"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectSonyTag9406CompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry entry;
    entry.key = make_exif_tag_key(store.arena(), "mk_sony_tag9406_0", 0x0005);
    entry.origin.block = block;
    entry.flags |= EntryFlags::ContextualName;
    entry.origin.name_context_kind    = EntryNameContextKind::SonyTag94060005;
    entry.origin.name_context_variant = 1U;
    const EntryId id                  = store.add_entry(entry);
    ASSERT_NE(id, openmeta::kInvalidEntryId);

    const Entry& stored = store.entry(id);
    EXPECT_EQ(exif_entry_name(store, stored, ExifTagNamePolicy::Canonical),
              std::string_view("BatteryTemperature"));
    EXPECT_EQ(exif_entry_name(store, stored, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("BatteryLevel"));
}


TEST(ExifTagNames, ContextualEntryNamesSelectRicohCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry placeholder;
    placeholder.key = make_exif_tag_key(store.arena(), "mk_ricoh0", 0x1002);
    placeholder.origin.block = block;
    placeholder.flags |= EntryFlags::ContextualName;
    placeholder.origin.name_context_kind = EntryNameContextKind::RicohMainCompat;
    placeholder.origin.name_context_variant = 1U;
    const EntryId placeholder_id            = store.add_entry(placeholder);
    ASSERT_NE(placeholder_id, openmeta::kInvalidEntryId);

    Entry white_balance;
    white_balance.key = make_exif_tag_key(store.arena(), "mk_ricoh0", 0x1003);
    white_balance.origin.block = block;
    white_balance.flags |= EntryFlags::ContextualName;
    white_balance.origin.name_context_kind
        = EntryNameContextKind::RicohMainCompat;
    white_balance.origin.name_context_variant = 2U;
    const EntryId white_balance_id            = store.add_entry(white_balance);
    ASSERT_NE(white_balance_id, openmeta::kInvalidEntryId);

    const Entry& placeholder_entry = store.entry(placeholder_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("DriveMode"));
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Ricoh_0x1002"));

    const Entry& white_balance_entry = store.entry(white_balance_id);
    EXPECT_EQ(exif_entry_name(store, white_balance_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Sharpness"));
    EXPECT_EQ(exif_entry_name(store, white_balance_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("WhiteBalance"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectPentaxCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry raw_development_process;
    raw_development_process.key = make_exif_tag_key(store.arena(), "mk_pentax0",
                                                    0x0062);
    raw_development_process.origin.block = block;
    raw_development_process.flags |= EntryFlags::ContextualName;
    raw_development_process.origin.name_context_kind
        = EntryNameContextKind::PentaxMain0062;
    raw_development_process.origin.name_context_variant = 1U;
    const EntryId raw_development_process_id            = store.add_entry(
        raw_development_process);
    ASSERT_NE(raw_development_process_id, openmeta::kInvalidEntryId);

    const Entry& raw_development_process_entry = store.entry(
        raw_development_process_id);
    EXPECT_EQ(exif_entry_name(store, raw_development_process_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("RawDevelopmentProcess"));
    EXPECT_EQ(exif_entry_name(store, raw_development_process_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Pentax_0x0062"));
}

TEST(ExifTagNames, EntryNamesSelectSamsungType2A002CompatWithPeerEntries)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryId;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry serial_number;
    serial_number.key = make_exif_tag_key(store.arena(), "mk_samsung_type2_0",
                                          0xA002);
    serial_number.origin.block     = block;
    const EntryId serial_number_id = store.add_entry(serial_number);
    ASSERT_NE(serial_number_id, openmeta::kInvalidEntryId);

    Entry lens_type;
    lens_type.key = make_exif_tag_key(store.arena(), "mk_samsung_type2_0",
                                      0xA003);
    lens_type.origin.block     = block;
    const EntryId lens_type_id = store.add_entry(lens_type);
    ASSERT_NE(lens_type_id, openmeta::kInvalidEntryId);

    const Entry& serial_number_entry = store.entry(serial_number_id);
    EXPECT_EQ(exif_entry_name(store, serial_number_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("SerialNumber"));
    EXPECT_EQ(exif_entry_name(store, serial_number_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Samsung_Type2_0xa002"));

    const Entry& lens_type_entry = store.entry(lens_type_id);
    EXPECT_EQ(exif_entry_name(store, lens_type_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("LensType"));
}

TEST(ExifTagNames, EntryNamesSelectSamsungType2A002CompatWithoutPeerEntries)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryId;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry serial_number;
    serial_number.key = make_exif_tag_key(store.arena(), "mk_samsung_type2_0",
                                          0xA002);
    serial_number.origin.block     = block;
    const EntryId serial_number_id = store.add_entry(serial_number);
    ASSERT_NE(serial_number_id, openmeta::kInvalidEntryId);

    const Entry& serial_number_entry = store.entry(serial_number_id);
    EXPECT_EQ(exif_entry_name(store, serial_number_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("SerialNumber"));
    EXPECT_EQ(exif_entry_name(store, serial_number_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Samsung_Type2_0xa002"));
}

TEST(ExifTagNames, EntryNamesSelectMinoltaMainCompatPlaceholders)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry first;
    first.key = make_exif_tag_key(store.arena(), "mk_minolta0", 0x0018);
    first.origin.block     = block;
    const EntryId first_id = store.add_entry(first);
    ASSERT_NE(first_id, openmeta::kInvalidEntryId);

    Entry second;
    second.key = make_exif_tag_key(store.arena(), "mk_minolta0", 0x0113);
    second.origin.block     = block;
    const EntryId second_id = store.add_entry(second);
    ASSERT_NE(second_id, openmeta::kInvalidEntryId);

    const Entry& first_entry = store.entry(first_id);
    EXPECT_EQ(exif_entry_name(store, first_entry, ExifTagNamePolicy::Canonical),
              std::string_view("ImageStabilization"));
    EXPECT_EQ(exif_entry_name(store, first_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Minolta_0x0018"));

    const Entry& second_entry = store.entry(second_id);
    EXPECT_EQ(exif_entry_name(store, second_entry, ExifTagNamePolicy::Canonical),
              std::string_view("ImageStabilization"));
    EXPECT_EQ(exif_entry_name(store, second_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Minolta_0x0113"));

    Entry third;
    third.key = make_exif_tag_key(store.arena(), "mk_minolta0", 0x0103);
    third.origin.block                = block;
    third.flags                       = EntryFlags::ContextualName;
    third.origin.name_context_kind    = EntryNameContextKind::MinoltaMainCompat;
    third.origin.name_context_variant = 1U;
    const EntryId third_id            = store.add_entry(third);
    ASSERT_NE(third_id, openmeta::kInvalidEntryId);

    const Entry& third_entry = store.entry(third_id);
    EXPECT_EQ(exif_entry_name(store, third_entry, ExifTagNamePolicy::Canonical),
              std::string_view("MinoltaImageSize"));
    EXPECT_EQ(exif_entry_name(store, third_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Minolta_0x0103"));

    Entry fourth;
    fourth.key = make_exif_tag_key(store.arena(), "mk_minolta0", 0x0103);
    fourth.origin.block             = block;
    fourth.flags                    = EntryFlags::ContextualName;
    fourth.origin.name_context_kind = EntryNameContextKind::MinoltaMainCompat;
    fourth.origin.name_context_variant = 2U;
    const EntryId fourth_id            = store.add_entry(fourth);
    ASSERT_NE(fourth_id, openmeta::kInvalidEntryId);

    const Entry& fourth_entry = store.entry(fourth_id);
    EXPECT_EQ(exif_entry_name(store, fourth_entry, ExifTagNamePolicy::Canonical),
              std::string_view("MinoltaImageSize"));
    EXPECT_EQ(exif_entry_name(store, fourth_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("MinoltaQuality"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectNikonSettingsCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry placeholder;
    placeholder.key          = make_exif_tag_key(store.arena(),
                                                 "mk_nikonsettings_main_0", 0x0001);
    placeholder.origin.block = block;
    placeholder.flags |= EntryFlags::ContextualName;
    placeholder.origin.name_context_kind
        = EntryNameContextKind::NikonSettingsMain;
    placeholder.origin.name_context_variant = 1U;
    const EntryId placeholder_id            = store.add_entry(placeholder);
    ASSERT_NE(placeholder_id, openmeta::kInvalidEntryId);

    const Entry& placeholder_entry = store.entry(placeholder_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ISOAutoHiLimit"));
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("NikonSettings_0x0001"));

    Entry movie_placeholder;
    movie_placeholder.key
        = make_exif_tag_key(store.arena(), "mk_nikonsettings_main_0", 0x00B1);
    movie_placeholder.origin.block = block;
    movie_placeholder.flags |= EntryFlags::ContextualName;
    movie_placeholder.origin.name_context_kind
        = EntryNameContextKind::NikonSettingsMain;
    movie_placeholder.origin.name_context_variant = 1U;
    const EntryId movie_placeholder_id = store.add_entry(movie_placeholder);
    ASSERT_NE(movie_placeholder_id, openmeta::kInvalidEntryId);

    const Entry& movie_placeholder_entry = store.entry(movie_placeholder_id);
    EXPECT_EQ(exif_entry_name(store, movie_placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("MoviePreviewButton"));
    EXPECT_EQ(exif_entry_name(store, movie_placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("NikonSettings_0x00b1"));

    Entry movie_func_1;
    movie_func_1.key          = make_exif_tag_key(store.arena(),
                                                  "mk_nikonsettings_main_0", 0x00B1);
    movie_func_1.origin.block = block;
    movie_func_1.flags |= EntryFlags::ContextualName;
    movie_func_1.origin.name_context_kind
        = EntryNameContextKind::NikonSettingsMain;
    movie_func_1.origin.name_context_variant = 2U;
    const EntryId movie_func_1_id            = store.add_entry(movie_func_1);
    ASSERT_NE(movie_func_1_id, openmeta::kInvalidEntryId);

    const Entry& movie_func_1_entry = store.entry(movie_func_1_id);
    EXPECT_EQ(exif_entry_name(store, movie_func_1_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("MoviePreviewButton"));
    EXPECT_EQ(exif_entry_name(store, movie_func_1_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("MovieFunc1Button"));

    Entry movie_func_2;
    movie_func_2.key          = make_exif_tag_key(store.arena(),
                                                  "mk_nikonsettings_main_0", 0x00B3);
    movie_func_2.origin.block = block;
    movie_func_2.flags |= EntryFlags::ContextualName;
    movie_func_2.origin.name_context_kind
        = EntryNameContextKind::NikonSettingsMain;
    movie_func_2.origin.name_context_variant = 3U;
    const EntryId movie_func_2_id            = store.add_entry(movie_func_2);
    ASSERT_NE(movie_func_2_id, openmeta::kInvalidEntryId);

    const Entry& movie_func_2_entry = store.entry(movie_func_2_id);
    EXPECT_EQ(exif_entry_name(store, movie_func_2_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("MovieFunc1Button"));
    EXPECT_EQ(exif_entry_name(store, movie_func_2_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("MovieFunc2Button"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectNikonMainZCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry image_area;
    image_area.key = make_exif_tag_key(store.arena(), "mk_nikon0", 0x002B);
    image_area.origin.block = block;
    image_area.flags |= EntryFlags::ContextualName;
    image_area.origin.name_context_kind    = EntryNameContextKind::NikonMainZ;
    image_area.origin.name_context_variant = 1U;
    const EntryId image_area_id            = store.add_entry(image_area);
    ASSERT_NE(image_area_id, openmeta::kInvalidEntryId);

    const Entry& image_area_entry = store.entry(image_area_id);
    EXPECT_EQ(exif_entry_name(store, image_area_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ImageArea"));

    Entry lens_mount;
    lens_mount.key = make_exif_tag_key(store.arena(), "mk_nikon0", 0x0035);
    lens_mount.origin.block = block;
    lens_mount.flags |= EntryFlags::ContextualName;
    lens_mount.origin.name_context_kind    = EntryNameContextKind::NikonMainZ;
    lens_mount.origin.name_context_variant = 1U;
    const EntryId lens_mount_id            = store.add_entry(lens_mount);
    ASSERT_NE(lens_mount_id, openmeta::kInvalidEntryId);

    const Entry& lens_mount_entry = store.entry(lens_mount_id);
    EXPECT_EQ(exif_entry_name(store, lens_mount_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("LensMountType"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectNikonMainCompactType2Variants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry unknown_iso;
    unknown_iso.key = make_exif_tag_key(store.arena(), "mk_nikon0", 0x0002);
    unknown_iso.origin.block = block;
    unknown_iso.flags |= EntryFlags::ContextualName;
    unknown_iso.origin.name_context_kind
        = EntryNameContextKind::NikonMainCompactType2;
    unknown_iso.origin.name_context_variant = 1U;
    const EntryId unknown_iso_id            = store.add_entry(unknown_iso);
    ASSERT_NE(unknown_iso_id, openmeta::kInvalidEntryId);

    const Entry& unknown_iso_entry = store.entry(unknown_iso_id);
    EXPECT_EQ(exif_entry_name(store, unknown_iso_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ISO"));
    EXPECT_EQ(exif_entry_name(store, unknown_iso_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Nikon_Type2_0x0002"));

    Entry digital_zoom;
    digital_zoom.key = make_exif_tag_key(store.arena(), "mk_nikon0", 0x000A);
    digital_zoom.origin.block = block;
    digital_zoom.flags |= EntryFlags::ContextualName;
    digital_zoom.origin.name_context_kind
        = EntryNameContextKind::NikonMainCompactType2;
    digital_zoom.origin.name_context_variant = 1U;
    const EntryId digital_zoom_id            = store.add_entry(digital_zoom);
    ASSERT_NE(digital_zoom_id, openmeta::kInvalidEntryId);

    const Entry& digital_zoom_entry = store.entry(digital_zoom_id);
    EXPECT_EQ(exif_entry_name(store, digital_zoom_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Nikon_0x000a"));
    EXPECT_EQ(exif_entry_name(store, digital_zoom_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("DigitalZoom"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectNikonShotInfoZ8CompatTables)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry number_offsets;
    number_offsets.key = make_exif_tag_key(store.arena(), "mk_nikon_shotinfo_0",
                                           0x0024);
    number_offsets.origin.block = block;
    number_offsets.flags |= EntryFlags::ContextualName;
    number_offsets.origin.name_context_kind
        = EntryNameContextKind::NikonShotInfoZ8;
    number_offsets.origin.name_context_variant = 1U;
    const EntryId number_offsets_id = store.add_entry(number_offsets);
    ASSERT_NE(number_offsets_id, openmeta::kInvalidEntryId);

    const Entry& number_offsets_entry = store.entry(number_offsets_id);
    EXPECT_TRUE(exif_entry_name(store, number_offsets_entry,
                                ExifTagNamePolicy::Canonical)
                    .empty());
    EXPECT_EQ(exif_entry_name(store, number_offsets_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("NumberOffsets"));

    Entry menu_bank;
    menu_bank.key = make_exif_tag_key(store.arena(), "mk_nikon_shotinfo_0",
                                      0x011E);
    menu_bank.origin.block = block;
    menu_bank.flags |= EntryFlags::ContextualName;
    menu_bank.origin.name_context_kind = EntryNameContextKind::NikonShotInfoZ8;
    menu_bank.origin.name_context_variant = 1U;
    const EntryId menu_bank_id            = store.add_entry(menu_bank);
    ASSERT_NE(menu_bank_id, openmeta::kInvalidEntryId);

    const Entry& menu_bank_entry = store.entry(menu_bank_id);
    EXPECT_TRUE(
        exif_entry_name(store, menu_bank_entry, ExifTagNamePolicy::Canonical)
            .empty());
    EXPECT_EQ(exif_entry_name(store, menu_bank_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("PhotoShootingMenuBank"));

    Entry bracket_increment;
    bracket_increment.key          = make_exif_tag_key(store.arena(),
                                                       "mk_nikon_shotinfo_0", 0x022E);
    bracket_increment.origin.block = block;
    bracket_increment.flags |= EntryFlags::ContextualName;
    bracket_increment.origin.name_context_kind
        = EntryNameContextKind::NikonShotInfoZ8;
    bracket_increment.origin.name_context_variant = 1U;
    const EntryId bracket_increment_id = store.add_entry(bracket_increment);
    ASSERT_NE(bracket_increment_id, openmeta::kInvalidEntryId);

    const Entry& bracket_increment_entry = store.entry(bracket_increment_id);
    EXPECT_TRUE(exif_entry_name(store, bracket_increment_entry,
                                ExifTagNamePolicy::Canonical)
                    .empty());
    EXPECT_EQ(exif_entry_name(store, bracket_increment_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("BracketIncrement"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectNikonShotInfoD800CompatTables)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry sequence_number;
    sequence_number.key          = make_exif_tag_key(store.arena(),
                                                     "mk_nikon_shotinfo_0", 0x051C);
    sequence_number.origin.block = block;
    sequence_number.flags |= EntryFlags::ContextualName;
    sequence_number.origin.name_context_kind
        = EntryNameContextKind::NikonShotInfoD800;
    sequence_number.origin.name_context_variant = 1U;
    const EntryId sequence_number_id = store.add_entry(sequence_number);
    ASSERT_NE(sequence_number_id, openmeta::kInvalidEntryId);

    const Entry& sequence_number_entry = store.entry(sequence_number_id);
    EXPECT_TRUE(exif_entry_name(store, sequence_number_entry,
                                ExifTagNamePolicy::Canonical)
                    .empty());
    EXPECT_EQ(exif_entry_name(store, sequence_number_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("SequenceNumber"));

    Entry shutter_count;
    shutter_count.key = make_exif_tag_key(store.arena(), "mk_nikon_shotinfo_0",
                                          0x05FB);
    shutter_count.origin.block = block;
    shutter_count.flags |= EntryFlags::ContextualName;
    shutter_count.origin.name_context_kind
        = EntryNameContextKind::NikonShotInfoD800;
    shutter_count.origin.name_context_variant = 1U;
    const EntryId shutter_count_id            = store.add_entry(shutter_count);
    ASSERT_NE(shutter_count_id, openmeta::kInvalidEntryId);

    const Entry& shutter_count_entry = store.entry(shutter_count_id);
    EXPECT_TRUE(exif_entry_name(store, shutter_count_entry,
                                ExifTagNamePolicy::Canonical)
                    .empty());
    EXPECT_EQ(exif_entry_name(store, shutter_count_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ShutterCount"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectNikonShotInfoD850CompatTables)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry image_area;
    image_area.key = make_exif_tag_key(store.arena(), "mk_nikon_shotinfo_0",
                                       0x06DD);
    image_area.origin.block = block;
    image_area.flags |= EntryFlags::ContextualName;
    image_area.origin.name_context_kind
        = EntryNameContextKind::NikonShotInfoD850;
    image_area.origin.name_context_variant = 1U;
    const EntryId image_area_id            = store.add_entry(image_area);
    ASSERT_NE(image_area_id, openmeta::kInvalidEntryId);

    const Entry& image_area_entry = store.entry(image_area_id);
    EXPECT_TRUE(
        exif_entry_name(store, image_area_entry, ExifTagNamePolicy::Canonical)
            .empty());
    EXPECT_EQ(exif_entry_name(store, image_area_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("PhotoShootingMenuBankImageArea"));

    Entry menu_bank;
    menu_bank.key = make_exif_tag_key(store.arena(), "mk_nikon_shotinfo_0",
                                      0x0024);
    menu_bank.origin.block = block;
    menu_bank.flags |= EntryFlags::ContextualName;
    menu_bank.origin.name_context_kind = EntryNameContextKind::NikonShotInfoD850;
    menu_bank.origin.name_context_variant = 1U;
    const EntryId menu_bank_id            = store.add_entry(menu_bank);
    ASSERT_NE(menu_bank_id, openmeta::kInvalidEntryId);

    const Entry& menu_bank_entry = store.entry(menu_bank_id);
    EXPECT_TRUE(
        exif_entry_name(store, menu_bank_entry, ExifTagNamePolicy::Canonical)
            .empty());
    EXPECT_EQ(exif_entry_name(store, menu_bank_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("PhotoShootingMenuBank"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectNikonFlashInfoGroupVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry group_a_comp;
    group_a_comp.key          = make_exif_tag_key(store.arena(),
                                                  "mk_nikon_flashinfo0107_0", 0x0028);
    group_a_comp.origin.block = block;
    group_a_comp.flags |= EntryFlags::ContextualName;
    group_a_comp.origin.name_context_kind
        = EntryNameContextKind::NikonFlashInfoGroups;
    group_a_comp.origin.name_context_variant = 1U;
    const EntryId group_a_comp_id            = store.add_entry(group_a_comp);
    ASSERT_NE(group_a_comp_id, openmeta::kInvalidEntryId);

    const Entry& group_a_comp_entry = store.entry(group_a_comp_id);
    EXPECT_EQ(exif_entry_name(store, group_a_comp_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("FlashGroupAOutput"));
    EXPECT_EQ(exif_entry_name(store, group_a_comp_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FlashGroupACompensation"));

    Entry group_b_out;
    group_b_out.key          = make_exif_tag_key(store.arena(),
                                                 "mk_nikon_flashinfo0300_0", 0x0029);
    group_b_out.origin.block = block;
    group_b_out.flags |= EntryFlags::ContextualName;
    group_b_out.origin.name_context_kind
        = EntryNameContextKind::NikonFlashInfoGroups;
    group_b_out.origin.name_context_variant = 2U;
    const EntryId group_b_out_id            = store.add_entry(group_b_out);
    ASSERT_NE(group_b_out_id, openmeta::kInvalidEntryId);

    const Entry& group_b_out_entry = store.entry(group_b_out_id);
    EXPECT_EQ(exif_entry_name(store, group_b_out_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("FlashGroupBOutput"));
    EXPECT_EQ(exif_entry_name(store, group_b_out_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FlashGroupBOutput"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectNikonFlashInfoLegacyVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry flash_comp;
    flash_comp.key          = make_exif_tag_key(store.arena(),
                                                "mk_nikon_flashinfo0106_0", 0x0027);
    flash_comp.origin.block = block;
    flash_comp.flags |= EntryFlags::ContextualName;
    flash_comp.origin.name_context_kind
        = EntryNameContextKind::NikonFlashInfoLegacy;
    flash_comp.origin.name_context_variant = 1U;
    const EntryId flash_comp_id            = store.add_entry(flash_comp);
    ASSERT_NE(flash_comp_id, openmeta::kInvalidEntryId);

    const Entry& flash_comp_entry = store.entry(flash_comp_id);
    EXPECT_EQ(exif_entry_name(store, flash_comp_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("FlashOutput"));
    EXPECT_EQ(exif_entry_name(store, flash_comp_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FlashCompensation"));

    Entry group_b_comp;
    group_b_comp.key          = make_exif_tag_key(store.arena(),
                                                  "mk_nikon_flashinfo0102_0", 0x0013);
    group_b_comp.origin.block = block;
    group_b_comp.flags |= EntryFlags::ContextualName;
    group_b_comp.origin.name_context_kind
        = EntryNameContextKind::NikonFlashInfoLegacy;
    group_b_comp.origin.name_context_variant = 3U;
    const EntryId group_b_comp_id            = store.add_entry(group_b_comp);
    ASSERT_NE(group_b_comp_id, openmeta::kInvalidEntryId);

    const Entry& group_b_comp_entry = store.entry(group_b_comp_id);
    EXPECT_EQ(exif_entry_name(store, group_b_comp_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("FlashGroupBOutput"));
    EXPECT_EQ(exif_entry_name(store, group_b_comp_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FlashGroupBCompensation"));

    Entry flash_comp_0102;
    flash_comp_0102.key          = make_exif_tag_key(store.arena(),
                                                     "mk_nikon_flashinfo0102_0", 0x000a);
    flash_comp_0102.origin.block = block;
    flash_comp_0102.flags |= EntryFlags::ContextualName;
    flash_comp_0102.origin.name_context_kind
        = EntryNameContextKind::NikonFlashInfoLegacy;
    flash_comp_0102.origin.name_context_variant = 8U;
    const EntryId flash_comp_0102_id = store.add_entry(flash_comp_0102);
    ASSERT_NE(flash_comp_0102_id, openmeta::kInvalidEntryId);

    const Entry& flash_comp_0102_entry = store.entry(flash_comp_0102_id);
    EXPECT_EQ(exif_entry_name(store, flash_comp_0102_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FlashCompensation"));

    Entry flash_comp_0101;
    flash_comp_0101.key          = make_exif_tag_key(store.arena(),
                                                     "mk_nikon_flashinfo0100_0", 0x000a);
    flash_comp_0101.origin.block = block;
    flash_comp_0101.flags |= EntryFlags::ContextualName;
    flash_comp_0101.origin.name_context_kind
        = EntryNameContextKind::NikonFlashInfoLegacy;
    flash_comp_0101.origin.name_context_variant = 8U;
    const EntryId flash_comp_0101_id = store.add_entry(flash_comp_0101);
    ASSERT_NE(flash_comp_0101_id, openmeta::kInvalidEntryId);

    const Entry& flash_comp_0101_entry = store.entry(flash_comp_0101_id);
    EXPECT_EQ(exif_entry_name(store, flash_comp_0101_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FlashCompensation"));

    Entry group_a_comp_0101;
    group_a_comp_0101.key
        = make_exif_tag_key(store.arena(), "mk_nikon_flashinfo0100_0", 0x0011);
    group_a_comp_0101.origin.block = block;
    group_a_comp_0101.flags |= EntryFlags::ContextualName;
    group_a_comp_0101.origin.name_context_kind
        = EntryNameContextKind::NikonFlashInfoLegacy;
    group_a_comp_0101.origin.name_context_variant = 2U;
    const EntryId group_a_comp_0101_id = store.add_entry(group_a_comp_0101);
    ASSERT_NE(group_a_comp_0101_id, openmeta::kInvalidEntryId);

    const Entry& group_a_comp_0101_entry = store.entry(group_a_comp_0101_id);
    EXPECT_EQ(exif_entry_name(store, group_a_comp_0101_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("FlashGroupAOutput"));
    EXPECT_EQ(exif_entry_name(store, group_a_comp_0101_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FlashGroupACompensation"));

    Entry group_b_comp_0101;
    group_b_comp_0101.key
        = make_exif_tag_key(store.arena(), "mk_nikon_flashinfo0100_0", 0x0012);
    group_b_comp_0101.origin.block = block;
    group_b_comp_0101.flags |= EntryFlags::ContextualName;
    group_b_comp_0101.origin.name_context_kind
        = EntryNameContextKind::NikonFlashInfoLegacy;
    group_b_comp_0101.origin.name_context_variant = 3U;
    const EntryId group_b_comp_0101_id = store.add_entry(group_b_comp_0101);
    ASSERT_NE(group_b_comp_0101_id, openmeta::kInvalidEntryId);

    const Entry& group_b_comp_0101_entry = store.entry(group_b_comp_0101_id);
    EXPECT_EQ(exif_entry_name(store, group_b_comp_0101_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("FlashGroupBOutput"));
    EXPECT_EQ(exif_entry_name(store, group_b_comp_0101_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FlashGroupBCompensation"));

    Entry group_a_ctrl;
    group_a_ctrl.key          = make_exif_tag_key(store.arena(),
                                                  "mk_nikon_flashinfo0102_0", 0x0010);
    group_a_ctrl.origin.block = block;
    group_a_ctrl.flags |= EntryFlags::ContextualName;
    group_a_ctrl.origin.name_context_kind
        = EntryNameContextKind::NikonFlashInfoLegacy;
    group_a_ctrl.origin.name_context_variant = 5U;
    const EntryId group_a_ctrl_id            = store.add_entry(group_a_ctrl);
    ASSERT_NE(group_a_ctrl_id, openmeta::kInvalidEntryId);

    const Entry& group_a_ctrl_entry = store.entry(group_a_ctrl_id);
    EXPECT_EQ(exif_entry_name(store, group_a_ctrl_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FlashGroupAControlMode"));

    Entry group_c_ctrl;
    group_c_ctrl.key          = make_exif_tag_key(store.arena(),
                                                  "mk_nikon_flashinfo0102_0", 0x0011);
    group_c_ctrl.origin.block = block;
    group_c_ctrl.flags |= EntryFlags::ContextualName;
    group_c_ctrl.origin.name_context_kind
        = EntryNameContextKind::NikonFlashInfoLegacy;
    group_c_ctrl.origin.name_context_variant = 7U;
    const EntryId group_c_ctrl_id            = store.add_entry(group_c_ctrl);
    ASSERT_NE(group_c_ctrl_id, openmeta::kInvalidEntryId);

    const Entry& group_c_ctrl_entry = store.entry(group_c_ctrl_id);
    EXPECT_EQ(exif_entry_name(store, group_c_ctrl_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view(""));
    EXPECT_EQ(exif_entry_name(store, group_c_ctrl_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FlashGroupCControlMode"));
}

TEST(ExifTagNames, EntryNamesSelectPanasonicMainCompatPlaceholders)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryId;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::make_u16;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry thumbnail_width;
    thumbnail_width.key = make_exif_tag_key(store.arena(), "mk_panasonic0",
                                            0x0058);
    thumbnail_width.origin.block     = block;
    const EntryId thumbnail_width_id = store.add_entry(thumbnail_width);
    ASSERT_NE(thumbnail_width_id, openmeta::kInvalidEntryId);

    Entry af_area_size;
    af_area_size.key = make_exif_tag_key(store.arena(), "mk_panasonic0",
                                         0x00DE);
    af_area_size.origin.block     = block;
    const EntryId af_area_size_id = store.add_entry(af_area_size);
    ASSERT_NE(af_area_size_id, openmeta::kInvalidEntryId);

    Entry lens_type;
    lens_type.key = make_exif_tag_key(store.arena(), "mk_panasonic0", 0x0051);
    lens_type.origin.block     = block;
    const EntryId lens_type_id = store.add_entry(lens_type);
    ASSERT_NE(lens_type_id, openmeta::kInvalidEntryId);

    Entry lens_type_make_placeholder;
    lens_type_make_placeholder.key          = make_exif_tag_key(store.arena(),
                                                                "mk_panasonic0", 0x00C4);
    lens_type_make_placeholder.value        = make_u16(65535U);
    lens_type_make_placeholder.origin.block = block;
    const EntryId lens_type_make_placeholder_id = store.add_entry(
        lens_type_make_placeholder);
    ASSERT_NE(lens_type_make_placeholder_id, openmeta::kInvalidEntryId);

    Entry lens_type_make_named;
    lens_type_make_named.key = make_exif_tag_key(store.arena(), "mk_panasonic0",
                                                 0x00C4);
    lens_type_make_named.value            = make_u16(0U);
    lens_type_make_named.origin.block     = block;
    const EntryId lens_type_make_named_id = store.add_entry(
        lens_type_make_named);
    ASSERT_NE(lens_type_make_named_id, openmeta::kInvalidEntryId);

    const Entry& thumbnail_width_entry = store.entry(thumbnail_width_id);
    EXPECT_EQ(exif_entry_name(store, thumbnail_width_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ThumbnailWidth"));
    EXPECT_EQ(exif_entry_name(store, thumbnail_width_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Panasonic_0x0058"));

    const Entry& af_area_size_entry = store.entry(af_area_size_id);
    EXPECT_EQ(exif_entry_name(store, af_area_size_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("AFAreaSize"));
    EXPECT_EQ(exif_entry_name(store, af_area_size_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Panasonic_0x00de"));

    const Entry& lens_type_entry = store.entry(lens_type_id);
    EXPECT_EQ(exif_entry_name(store, lens_type_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("LensType"));
    EXPECT_EQ(exif_entry_name(store, lens_type_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("LensType"));

    const Entry& lens_type_make_placeholder_entry = store.entry(
        lens_type_make_placeholder_id);
    EXPECT_EQ(exif_entry_name(store, lens_type_make_placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("LensTypeMake"));
    EXPECT_EQ(exif_entry_name(store, lens_type_make_placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Panasonic_0x00c4"));

    const Entry& lens_type_make_named_entry = store.entry(
        lens_type_make_named_id);
    EXPECT_EQ(exif_entry_name(store, lens_type_make_named_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("LensTypeMake"));
    EXPECT_EQ(exif_entry_name(store, lens_type_make_named_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("LensTypeMake"));

    Entry legacy_model_placeholder;
    legacy_model_placeholder.key              = make_exif_tag_key(store.arena(),
                                                                  "mk_panasonic0", 0x0004);
    legacy_model_placeholder.value            = make_u16(1U);
    legacy_model_placeholder.origin.block     = block;
    const EntryId legacy_model_placeholder_id = store.add_entry(
        legacy_model_placeholder);
    ASSERT_NE(legacy_model_placeholder_id, openmeta::kInvalidEntryId);

    const Entry& legacy_model_placeholder_entry = store.entry(
        legacy_model_placeholder_id);
    EXPECT_EQ(exif_entry_name(store, legacy_model_placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Model"));
    EXPECT_EQ(exif_entry_name(store, legacy_model_placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Panasonic_0x0004"));
}

TEST(ExifTagNames, MapsSigmaMainNamesAndPlaceholders)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry exposure_adjust;
    exposure_adjust.key = make_exif_tag_key(store.arena(), "mk_sigma0", 0x000C);
    exposure_adjust.origin.block     = block;
    const EntryId exposure_adjust_id = store.add_entry(exposure_adjust);
    ASSERT_NE(exposure_adjust_id, openmeta::kInvalidEntryId);

    Entry sigma_placeholder;
    sigma_placeholder.key = make_exif_tag_key(store.arena(), "mk_sigma0",
                                              0x0020);
    sigma_placeholder.origin.block     = block;
    const EntryId sigma_placeholder_id = store.add_entry(sigma_placeholder);
    ASSERT_NE(sigma_placeholder_id, openmeta::kInvalidEntryId);

    Entry wb_auto;
    wb_auto.key = make_exif_tag_key(store.arena(), "mk_sigma_wbsettings_0",
                                    0x0000);
    wb_auto.origin.block     = block;
    const EntryId wb_auto_id = store.add_entry(wb_auto);
    ASSERT_NE(wb_auto_id, openmeta::kInvalidEntryId);

    Entry wb_unknown9;
    wb_unknown9.key = make_exif_tag_key(store.arena(), "mk_sigma_wbsettings2_0",
                                        0x001B);
    wb_unknown9.origin.block     = block;
    const EntryId wb_unknown9_id = store.add_entry(wb_unknown9);
    ASSERT_NE(wb_unknown9_id, openmeta::kInvalidEntryId);

    const Entry& exposure_adjust_entry = store.entry(exposure_adjust_id);
    EXPECT_EQ(exif_entry_name(store, exposure_adjust_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ExposureAdjust"));
    EXPECT_EQ(exif_entry_name(store, exposure_adjust_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ExposureAdjust"));

    const Entry& sigma_placeholder_entry = store.entry(sigma_placeholder_id);
    EXPECT_EQ(exif_entry_name(store, sigma_placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Sigma_0x0020"));

    const Entry& wb_auto_entry = store.entry(wb_auto_id);
    EXPECT_EQ(exif_entry_name(store, wb_auto_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("WB_RGBLevelsAuto"));

    const Entry& wb_unknown9_entry = store.entry(wb_unknown9_id);
    EXPECT_EQ(exif_entry_name(store, wb_unknown9_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("WB_RGBLevelsUnknown9"));

    Entry sigma_compat_placeholder;
    sigma_compat_placeholder.key = make_exif_tag_key(store.arena(), "mk_sigma0",
                                                     0x001A);
    sigma_compat_placeholder.origin.block     = block;
    const EntryId sigma_compat_placeholder_id = store.add_entry(
        sigma_compat_placeholder);
    ASSERT_NE(sigma_compat_placeholder_id, openmeta::kInvalidEntryId);

    const Entry& sigma_compat_placeholder_entry = store.entry(
        sigma_compat_placeholder_id);
    EXPECT_EQ(exif_entry_name(store, sigma_compat_placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("PreviewImageStart"));
    EXPECT_EQ(exif_entry_name(store, sigma_compat_placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Sigma_0x001a"));

    Entry sigma_fixed_compat;
    sigma_fixed_compat.key = make_exif_tag_key(store.arena(), "mk_sigma0",
                                               0x001C);
    sigma_fixed_compat.origin.block     = block;
    const EntryId sigma_fixed_compat_id = store.add_entry(sigma_fixed_compat);
    ASSERT_NE(sigma_fixed_compat_id, openmeta::kInvalidEntryId);

    const Entry& sigma_fixed_compat_entry = store.entry(sigma_fixed_compat_id);
    EXPECT_EQ(exif_entry_name(store, sigma_fixed_compat_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("PreviewImageSize"));
    EXPECT_EQ(exif_entry_name(store, sigma_fixed_compat_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("PreviewImageStart"));

    Entry sigma_model_placeholder;
    sigma_model_placeholder.key = make_exif_tag_key(store.arena(), "mk_sigma0",
                                                    0x0033);
    sigma_model_placeholder.origin.block = block;
    sigma_model_placeholder.flags |= EntryFlags::ContextualName;
    sigma_model_placeholder.origin.name_context_kind
        = EntryNameContextKind::SigmaMainCompat;
    sigma_model_placeholder.origin.name_context_variant = 1U;
    const EntryId sigma_model_placeholder_id            = store.add_entry(
        sigma_model_placeholder);
    ASSERT_NE(sigma_model_placeholder_id, openmeta::kInvalidEntryId);

    const Entry& sigma_model_placeholder_entry = store.entry(
        sigma_model_placeholder_id);
    EXPECT_EQ(exif_entry_name(store, sigma_model_placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ExposureTime2"));
    EXPECT_EQ(exif_entry_name(store, sigma_model_placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Sigma_0x0033"));
}
