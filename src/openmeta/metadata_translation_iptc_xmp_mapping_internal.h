// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/metadata_translation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace openmeta::detail {

enum class IptcXmpPropertyShape : uint8_t {
    Scalar,
    LangAlt,
    Seq,
    Bag,
};

struct IptcXmpMappingDescriptor final {
    MetadataDescriptiveTranslationMapping mapping
        = MetadataDescriptiveTranslationMapping::None;
    std::string_view schema_ns;
    std::string_view prefix;
    std::string_view property_root;
    std::string_view property_path;
    uint16_t iptc_dataset      = 0U;
    uint16_t max_bytes         = 0U;
    IptcXmpPropertyShape shape = IptcXmpPropertyShape::Scalar;
    bool repeated              = false;
};

inline constexpr std::string_view kIptcXmpNsDc
    = "http://purl.org/dc/elements/1.1/";
inline constexpr std::string_view kIptcXmpNsPhotoshop
    = "http://ns.adobe.com/photoshop/1.0/";
inline constexpr std::string_view kIptcXmpNsIptcCore
    = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";

inline constexpr std::array<IptcXmpMappingDescriptor, 20U> kIptcXmpMappings = { {
    { MetadataDescriptiveTranslationMapping::DcTitle, kIptcXmpNsDc, "dc",
      "title", "title[@xml:lang=x-default]", 5U, 64U,
      IptcXmpPropertyShape::LangAlt, false },
    { MetadataDescriptiveTranslationMapping::DcDescription, kIptcXmpNsDc, "dc",
      "description", "description[@xml:lang=x-default]", 120U, 2000U,
      IptcXmpPropertyShape::LangAlt, false },
    { MetadataDescriptiveTranslationMapping::DcCreator, kIptcXmpNsDc, "dc",
      "creator", "creator", 80U, 32U, IptcXmpPropertyShape::Seq, true },
    { MetadataDescriptiveTranslationMapping::DcSubject, kIptcXmpNsDc, "dc",
      "subject", "subject", 25U, 64U, IptcXmpPropertyShape::Bag, true },
    { MetadataDescriptiveTranslationMapping::DcRights, kIptcXmpNsDc, "dc",
      "rights", "rights[@xml:lang=x-default]", 116U, 128U,
      IptcXmpPropertyShape::LangAlt, false },
    { MetadataDescriptiveTranslationMapping::PhotoshopCredit,
      kIptcXmpNsPhotoshop, "photoshop", "Credit", "Credit", 110U, 32U,
      IptcXmpPropertyShape::Scalar, false },
    { MetadataDescriptiveTranslationMapping::PhotoshopSource,
      kIptcXmpNsPhotoshop, "photoshop", "Source", "Source", 115U, 32U,
      IptcXmpPropertyShape::Scalar, false },
    { MetadataDescriptiveTranslationMapping::PhotoshopCity, kIptcXmpNsPhotoshop,
      "photoshop", "City", "City", 90U, 32U, IptcXmpPropertyShape::Scalar,
      false },
    { MetadataDescriptiveTranslationMapping::IptcLocation, kIptcXmpNsIptcCore,
      "Iptc4xmpCore", "Location", "Location", 92U, 32U,
      IptcXmpPropertyShape::Scalar, false },
    { MetadataDescriptiveTranslationMapping::PhotoshopState,
      kIptcXmpNsPhotoshop, "photoshop", "State", "State", 95U, 32U,
      IptcXmpPropertyShape::Scalar, false },
    { MetadataDescriptiveTranslationMapping::PhotoshopCountry,
      kIptcXmpNsPhotoshop, "photoshop", "Country", "Country", 101U, 64U,
      IptcXmpPropertyShape::Scalar, false },
    { MetadataDescriptiveTranslationMapping::IptcCountryCode,
      kIptcXmpNsIptcCore, "Iptc4xmpCore", "CountryCode", "CountryCode", 100U,
      3U, IptcXmpPropertyShape::Scalar, false },
    { MetadataDescriptiveTranslationMapping::PhotoshopHeadline,
      kIptcXmpNsPhotoshop, "photoshop", "Headline", "Headline", 105U, 256U,
      IptcXmpPropertyShape::Scalar, false },
    { MetadataDescriptiveTranslationMapping::PhotoshopInstructions,
      kIptcXmpNsPhotoshop, "photoshop", "Instructions", "Instructions", 40U,
      256U, IptcXmpPropertyShape::Scalar, false },
    { MetadataDescriptiveTranslationMapping::PhotoshopTransmissionReference,
      kIptcXmpNsPhotoshop, "photoshop", "TransmissionReference",
      "TransmissionReference", 103U, 32U, IptcXmpPropertyShape::Scalar, false },
    { MetadataDescriptiveTranslationMapping::PhotoshopAuthorsPosition,
      kIptcXmpNsPhotoshop, "photoshop", "AuthorsPosition", "AuthorsPosition",
      85U, 32U, IptcXmpPropertyShape::Scalar, false },
    { MetadataDescriptiveTranslationMapping::PhotoshopCaptionWriter,
      kIptcXmpNsPhotoshop, "photoshop", "CaptionWriter", "CaptionWriter", 122U,
      32U, IptcXmpPropertyShape::Scalar, false },
    { MetadataDescriptiveTranslationMapping::PhotoshopCategory,
      kIptcXmpNsPhotoshop, "photoshop", "Category", "Category", 15U, 3U,
      IptcXmpPropertyShape::Scalar, false },
    { MetadataDescriptiveTranslationMapping::PhotoshopSupplementalCategories,
      kIptcXmpNsPhotoshop, "photoshop", "SupplementalCategories",
      "SupplementalCategories", 20U, 32U, IptcXmpPropertyShape::Bag, true },
    { MetadataDescriptiveTranslationMapping::PhotoshopUrgency,
      kIptcXmpNsPhotoshop, "photoshop", "Urgency", "Urgency", 10U, 1U,
      IptcXmpPropertyShape::Scalar, false },
} };

inline constexpr std::span<const IptcXmpMappingDescriptor, 7U>
    kIptcXmpPrimaryMappings { kIptcXmpMappings.data(), 7U };
inline constexpr std::span<const IptcXmpMappingDescriptor, 5U>
    kIptcXmpLocationMappings { kIptcXmpMappings.data() + 7U, 5U };
inline constexpr std::span<const IptcXmpMappingDescriptor, 3U>
    kIptcXmpEditorialMappings { kIptcXmpMappings.data() + 12U, 3U };
inline constexpr std::span<const IptcXmpMappingDescriptor, 5U>
    kIptcXmpWorkflowMappings { kIptcXmpMappings.data() + 15U, 5U };

}  // namespace openmeta::detail
