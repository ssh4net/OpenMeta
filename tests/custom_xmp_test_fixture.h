// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/metadata_authoring.h"

#include <string_view>
#include <vector>

namespace openmeta::custom_xmp_test {

inline constexpr std::string_view kSchema      = "urn:openmeta:test:custom:1";
inline constexpr std::string_view kOtherSchema = "urn:openmeta:test:custom:2";
inline constexpr std::string_view kXapSchema   = "http://ns.adobe.com/xap/1.0/";
inline constexpr std::string_view kOtherSchemaLocalPath
    = "Record/nsu_75726e3a6f70656e6d6574613a746573743a637573746f6d3a32:Local";

static MetadataAuthoringEntry
text_entry(std::string_view schema, std::string_view path,
           std::string_view text)
{
    MetadataAuthoringEntry entry;
    entry.key   = make_xmp_property_key_view(schema, path);
    entry.value = make_value_view_text(text, TextEncoding::Utf8);
    return entry;
}

inline std::vector<MetadataAuthoringEntry>
initial_entries()
{
    std::vector<MetadataAuthoringEntry> entries;
    entries.reserve(31U);
    entries.push_back(
        make_xmp_container_entry(kSchema, "Record", XmpContainerKind::Struct));
    entries.push_back(text_entry(kSchema, "Record/Title", "old"));

    entries.push_back(make_xmp_container_entry(kSchema, "Record/Values",
                                               XmpContainerKind::Bag));
    entries.push_back(text_entry(kSchema, "Record/Values[1]", "one"));
    entries.push_back(text_entry(kSchema, "Record/Values[3]", "three"));
    entries.push_back(text_entry(kSchema, "Record/Values[10]", "ten"));

    entries.push_back(make_xmp_container_entry(kSchema, "Record/Steps",
                                               XmpContainerKind::Seq));
    entries.push_back(text_entry(kSchema, "Record/Steps[1]", "1"));
    entries.push_back(text_entry(kSchema, "Record/Steps[2]", "2"));

    entries.push_back(make_xmp_container_entry(kSchema, "Record/Labels",
                                               XmpContainerKind::LangAlt));
    entries.push_back(text_entry(kSchema, "Record/Labels[@xml:lang=x-default]",
                                 "Default label"));
    entries.push_back(
        text_entry(kSchema, "Record/Labels[@xml:lang=en-US]", "English label"));

    entries.push_back(make_xmp_container_entry(kSchema, "Record/Frames",
                                               XmpContainerKind::Seq));
    entries.push_back(make_xmp_container_entry(kSchema, "Record/Frames[1]",
                                               XmpContainerKind::Struct));
    entries.push_back(text_entry(kSchema, "Record/Frames[1]/Name", "one"));
    entries.push_back(make_xmp_container_entry(kSchema, "Record/Frames[10]",
                                               XmpContainerKind::Struct));
    entries.push_back(text_entry(kSchema, "Record/Frames[10]/Name", "ten"));

    entries.push_back(make_xmp_container_entry(kSchema, "Record/EmptyStruct",
                                               XmpContainerKind::Struct));
    entries.push_back(make_xmp_container_entry(kSchema, "Record/EmptyBag",
                                               XmpContainerKind::Bag));

    entries.push_back(make_xmp_container_entry(kSchema, "Record/Matrix",
                                               XmpContainerKind::Seq));
    entries.push_back(make_xmp_container_entry(kSchema, "Record/Matrix[1]",
                                               XmpContainerKind::Seq));
    entries.push_back(text_entry(kSchema, "Record/Matrix[1][1]", "m11"));
    entries.push_back(text_entry(kSchema, "Record/Matrix[1][2]", "m12"));
    entries.push_back(make_xmp_container_entry(kSchema, "Record/Matrix[3]",
                                               XmpContainerKind::Seq));
    entries.push_back(text_entry(kSchema, "Record/Matrix[3][1]", "m31"));

    entries.push_back(text_entry(kSchema, "Record/Local", "same-local-name"));
    entries.push_back(
        text_entry(kSchema, kOtherSchemaLocalPath, "other-child-namespace"));
    entries.push_back(text_entry(kSchema, "Record/BlackLevel", "0.125"));

    entries.push_back(make_xmp_container_entry(kOtherSchema, "Record",
                                               XmpContainerKind::Struct));
    entries.push_back(
        text_entry(kOtherSchema, "Record/Local", "other-root-namespace"));
    entries.push_back(text_entry(kXapSchema, "CreatorTool", "old tool"));
    return entries;
}

}  // namespace openmeta::custom_xmp_test
