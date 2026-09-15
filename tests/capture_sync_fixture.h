// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openmeta/metadata_translation.h"
#include "openmeta/xmp_decode.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

namespace openmeta::test {

inline constexpr std::string_view kCaptureSyncXml = R"xml(
<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
<rdf:Description xmlns:e="http://ns.adobe.com/exif/1.0/" xmlns:x="http://cipa.jp/exif/1.0/">
<e:ExposureTime>1/3</e:ExposureTime><e:FNumber>17/6</e:FNumber>
<e:ISO>400</e:ISO><e:FocalLength>50/3</e:FocalLength>
<e:ExposureBiasValue>-1/3</e:ExposureBiasValue>
<e:ExposureProgram>3</e:ExposureProgram><e:MeteringMode>5</e:MeteringMode>
<e:SensingMethod>2</e:SensingMethod><e:CustomRendered>0</e:CustomRendered>
<e:ExposureMode>1</e:ExposureMode><e:WhiteBalance>1</e:WhiteBalance>
<e:SceneCaptureType>2</e:SceneCaptureType><e:GainControl>1</e:GainControl>
<e:Contrast>1</e:Contrast><e:Saturation>2</e:Saturation>
<e:Sharpness>1</e:Sharpness><e:SubjectDistanceRange>2</e:SubjectDistanceRange>
<e:SubjectDistance>1/3</e:SubjectDistance><e:DigitalZoomRatio>1/3</e:DigitalZoomRatio>
<e:ExposureIndex>100/3</e:ExposureIndex><e:FlashEnergy>1/3</e:FlashEnergy>
<e:Flash>95</e:Flash><e:LightSource>25</e:LightSource>
<x:SensitivityType>7</x:SensitivityType><x:StandardOutputSensitivity>400</x:StandardOutputSensitivity>
<x:RecommendedExposureIndex>400</x:RecommendedExposureIndex><x:ISOSpeed>400</x:ISOSpeed>
<x:ISOSpeedLatitudeyyy>100</x:ISOSpeedLatitudeyyy><x:ISOSpeedLatitudezzz>800</x:ISOSpeedLatitudezzz>
<e:SpectralSensitivity>visible</e:SpectralSensitivity><x:CameraOwnerName>Owner</x:CameraOwnerName>
<x:BodySerialNumber>001</x:BodySerialNumber><x:LensMake>Lens make</x:LensMake>
<x:LensModel>Lens model</x:LensModel><x:LensSerialNumber>002</x:LensSerialNumber>
<x:LensSpecification><rdf:Seq><rdf:li>24</rdf:li><rdf:li>70</rdf:li><rdf:li>14/5</rdf:li><rdf:li>4</rdf:li></rdf:Seq></x:LensSpecification>
<e:ImageUniqueID>00112233445566778899aAbBcCdDeEfF</e:ImageUniqueID>
<e:ShutterSpeedValue>7</e:ShutterSpeedValue><e:ApertureValue>3</e:ApertureValue>
<e:BrightnessValue>2</e:BrightnessValue><e:MaxApertureValue>2</e:MaxApertureValue>
<e:FocalPlaneXResolution>10000/3</e:FocalPlaneXResolution><e:FocalPlaneYResolution>11000/7</e:FocalPlaneYResolution>
<e:FocalPlaneResolutionUnit>3</e:FocalPlaneResolutionUnit>
<e:SubjectArea><rdf:Seq><rdf:li>3</rdf:li><rdf:li>4</rdf:li><rdf:li>2</rdf:li><rdf:li>0</rdf:li></rdf:Seq></e:SubjectArea>
<e:SubjectLocation><rdf:Seq><rdf:li>1</rdf:li><rdf:li>2</rdf:li></rdf:Seq></e:SubjectLocation>
</rdf:Description></rdf:RDF>)xml";

inline bool
capture_sync_translate_step(MetaStore& store, unsigned step)
{
    constexpr auto all = MetadataCaptureTranslationSourceMode::All;
    constexpr auto replace
        = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
    constexpr auto ok = MetadataCaptureTranslationStatus::Ok;
    switch (step) {
    case 0:
        return translate_xmp_capture_metadata(store,
                                              { .source_mode     = all,
                                                .conflict_policy = replace },
                                              &store)
                   .status
               == ok;
    case 1:
        return translate_xmp_capture_settings_metadata(
                   store, { .source_mode = all, .conflict_policy = replace },
                   &store)
                   .status
               == ok;
    case 2:
        return translate_xmp_capture_rational_metadata(
                   store, { .source_mode = all, .conflict_policy = replace },
                   &store)
                   .status
               == ok;
    case 3:
        return translate_xmp_flash_metadata(store,
                                            { .source_mode     = all,
                                              .conflict_policy = replace },
                                            &store)
                   .status
               == ok;
    case 4:
        return translate_xmp_light_source_metadata(
                   store, { .source_mode = all, .conflict_policy = replace },
                   &store)
                   .status
               == ok;
    case 5:
        return translate_xmp_sensitivity_metadata(
                   store, { .source_mode = all, .conflict_policy = replace },
                   &store)
                   .status
               == ok;
    case 6:
        return translate_xmp_camera_text_metadata(
                   store,
                   { .source_mode = MetadataTechnicalTranslationSourceMode::All,
                     .conflict_policy
                     = MetadataTechnicalTranslationConflictPolicy::
                         ReplaceExisting },
                   &store)
                   .status
               == MetadataTechnicalTranslationStatus::Ok;
    case 7:
        return translate_xmp_identity_metadata(store,
                                               { .source_mode     = all,
                                                 .conflict_policy = replace },
                                               &store)
                   .status
               == ok;
    case 8:
        return translate_xmp_apex_metadata(store,
                                           { .source_mode     = all,
                                             .conflict_policy = replace },
                                           &store)
                   .status
               == ok;
    case 9:
        return translate_xmp_capture_spatial_metadata(
                   store, { .source_mode = all, .conflict_policy = replace },
                   &store)
                   .status
               == ok;
    default: return false;
    }
}

inline bool
capture_sync_translate(MetaStore& store, bool reverse = false)
{
    for (unsigned step = 0; step < 10U; ++step)
        if (!capture_sync_translate_step(store, reverse ? 9U - step : step))
            return false;
    return true;
}

inline void
capture_sync_expect_native(const MetaStore& actual, const MetaStore& expected)
{
    unsigned count = 0U;
    for (const Entry& entry : expected.entries()) {
        if (entry.key.kind != MetaKeyKind::ExifTag
            || any(entry.flags, EntryFlags::Deleted))
            continue;
        const auto ifd = expected.arena().span(entry.key.data.exif_tag.ifd);
        if (std::string_view(reinterpret_cast<const char*>(ifd.data()),
                             ifd.size())
            != "exififd")
            continue;
        const uint16_t tag = entry.key.data.exif_tag.tag;
        SCOPED_TRACE(tag);
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = "exififd";
        key.data.exif_tag.tag = tag;
        const auto ids        = actual.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const MetaValue& a = actual.entry(ids[0]).value;
        const MetaValue& b = entry.value;
        ASSERT_EQ(a.kind, b.kind);
        ASSERT_EQ(a.elem_type, b.elem_type);
        if (a.kind == MetaValueKind::Text) {
            EXPECT_EQ(a.text_encoding, b.text_encoding);
            const auto as = actual.arena().span(a.data.span);
            const auto bs = expected.arena().span(b.data.span);
            std::string_view av(reinterpret_cast<const char*>(as.data()),
                                as.size());
            std::string_view bv(reinterpret_cast<const char*>(bs.data()),
                                bs.size());
            if (!av.empty() && av.back() == '\0')
                av.remove_suffix(1U);
            if (!bv.empty() && bv.back() == '\0')
                bv.remove_suffix(1U);
            EXPECT_EQ(av, bv);
        } else {
            ASSERT_EQ(a.count, b.count);
            if (a.kind == MetaValueKind::Array) {
                const auto av = actual.arena().span(a.data.span);
                const auto bv = expected.arena().span(b.data.span);
                ASSERT_EQ(av.size(), bv.size());
                EXPECT_EQ(std::memcmp(av.data(), bv.data(), av.size()), 0);
            } else if (a.elem_type == MetaElementType::URational) {
                EXPECT_EQ(a.data.ur.numer, b.data.ur.numer);
                EXPECT_EQ(a.data.ur.denom, b.data.ur.denom);
            } else if (a.elem_type == MetaElementType::SRational) {
                EXPECT_EQ(a.data.sr.numer, b.data.sr.numer);
                EXPECT_EQ(a.data.sr.denom, b.data.sr.denom);
            } else {
                EXPECT_EQ(a.data.u64, b.data.u64);
            }
        }
        ++count;
    }
    EXPECT_EQ(count, 46U);
}

}  // namespace openmeta::test
