// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_translation.h"
#include "openmeta/validate.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    constexpr std::string_view kTiff = "http://ns.adobe.com/tiff/1.0/";
    constexpr std::string_view kExif = "http://ns.adobe.com/exif/1.0/";
    using Status = MetadataCaptureTranslationStatus;

    void xmp(MetaStore& store, std::string_view ns, std::string_view path,
             std::string_view value,
             EntryFlags flags = EntryFlags::Dirty)
    {
        Entry entry;
        entry.key = make_xmp_property_key(store.arena(), ns, path);
        entry.value = make_text(store.arena(), value, TextEncoding::Utf8);
        entry.flags = flags;
        (void)store.add_entry(entry);
    }

    const Entry* find(const MetaStore& store, std::string_view ifd,
                      uint16_t tag)
    {
        const auto ids = store.find_all(make_exif_tag_key_view(ifd, tag));
        return ids.size() == 1U ? &store.entry(ids.front()) : nullptr;
    }

    std::string text(const MetaStore& store, const Entry& entry)
    {
        const auto raw = store.arena().span(entry.value.data.span);
        return { reinterpret_cast<const char*>(raw.data()), raw.size() };
    }

    TEST(MetadataProfileTranslation, FullBatchAndPortableOutput)
    {
        MetaStore source;
        xmp(source, kTiff, "ImageDescription", "A scene");
        xmp(source, kTiff, "Artist", "A. Photographer");
        xmp(source, kTiff, "Copyright", "Copyright 2026");
        xmp(source, kExif, "ColorSpace", "1");
        xmp(source, kExif, "RelatedSoundFile", "SOUND.WAV");
        source.finalize();

        const auto result = translate_xmp_profile_metadata(source, {}, &source);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.source_properties, 5U);
        EXPECT_EQ(result.entries_added, 5U);
        EXPECT_EQ(text(source, *find(source, "ifd0", 0x010eU)), "A scene");
        EXPECT_EQ(text(source, *find(source, "ifd0", 0x013bU)),
                  "A. Photographer");
        EXPECT_EQ(text(source, *find(source, "ifd0", 0x8298U)),
                  "Copyright 2026");
        EXPECT_EQ(find(source, "exififd", 0xa001U)->value.data.u64, 1U);
        EXPECT_EQ(text(source, *find(source, "exififd", 0xa004U)),
                  "SOUND.WAV");
        EXPECT_EQ(find(source, "exififd", 0xa004U)->origin.wire_count, 10U);
        EXPECT_TRUE(validate_store(source).ok());

        std::vector<std::byte> bytes(256U * 1024U);
        const auto dumped = dump_xmp_portable(source, bytes, {});
        ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
        const std::string packet(reinterpret_cast<const char*>(bytes.data()),
                                 static_cast<size_t>(dumped.written));
        EXPECT_NE(packet.find("<tiff:ImageDescription>A scene"),
                  std::string::npos);
        EXPECT_NE(packet.find("<tiff:Artist>A. Photographer"),
                  std::string::npos);
        EXPECT_NE(packet.find("<tiff:Copyright>Copyright 2026"),
                  std::string::npos);
        EXPECT_NE(packet.find("<exif:ColorSpace>sRGB</exif:ColorSpace>"),
                  std::string::npos);
    }

    TEST(MetadataProfileTranslation, BoundsConflictsAndAtomicRemoval)
    {
        MetaStore unsupported;
        xmp(unsupported, kTiff, "Artist[1]", "A. Photographer");
        unsupported.finalize();
        EXPECT_EQ(translate_xmp_profile_metadata(unsupported, {}, &unsupported)
                      .status,
                  Status::UnsupportedSourceShape);

        MetaStore invalid;
        xmp(invalid, kExif, "ColorSpace", "2");
        invalid.finalize();
        EXPECT_EQ(translate_xmp_profile_metadata(invalid, {}, &invalid).status,
                  Status::ValueOutOfRange);
        EXPECT_EQ(invalid.find_all(
                      make_exif_tag_key_view("exififd", 0xa001U))
                      .size(),
                  0U);

        MetaStore sound;
        xmp(sound, kExif, "RelatedSoundFile", "../SOUND.WAV");
        sound.finalize();
        EXPECT_EQ(translate_xmp_profile_metadata(sound, {}, &sound).status,
                  Status::InvalidSourceValue);

        MetaStore legacy;
        Entry old;
        old.key = make_exif_tag_key(legacy.arena(), "ifd0", 0x8298U);
        const std::string two_part("Owner\0Editor", 12U);
        old.value = make_bytes(
            legacy.arena(), std::as_bytes(std::span(two_part.data(),
                                                    two_part.size())));
        old.origin.wire_type = { WireFamily::Tiff, 2U };
        old.origin.wire_count = 13U;
        (void)legacy.add_entry(old);
        xmp(legacy, kTiff, "Copyright", "New owner");
        legacy.finalize();
        EXPECT_EQ(translate_xmp_profile_metadata(legacy, {}, &legacy).status,
                  Status::NativeConflict);
        MetadataProfileTranslationOptions replace;
        replace.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        ASSERT_EQ(translate_xmp_profile_metadata(legacy, replace, &legacy)
                      .status,
                  Status::Ok);
        EXPECT_EQ(text(legacy, *find(legacy, "ifd0", 0x8298U)), "New owner");

        MetaStore remove;
        xmp(remove, kTiff, "Artist", "", EntryFlags::Dirty
                                             | EntryFlags::Deleted);
        Entry existing;
        existing.key = make_exif_tag_key(remove.arena(), "ifd0", 0x013bU);
        existing.value = make_text(remove.arena(), "Old", TextEncoding::Ascii);
        existing.origin.wire_type = { WireFamily::Tiff, 2U };
        existing.origin.wire_count = 4U;
        (void)remove.add_entry(existing);
        remove.finalize();
        replace.artist_to_exif = true;
        EXPECT_EQ(translate_xmp_profile_metadata(remove, replace, &remove)
                      .status,
                  Status::Ok);
        EXPECT_EQ(remove.find_all(make_exif_tag_key_view("ifd0", 0x013bU))
                      .size(),
                  0U);
    }

}  // namespace
}  // namespace openmeta
