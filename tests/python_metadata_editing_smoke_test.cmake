cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED OPENMETA_PYTHON_EXECUTABLE OR OPENMETA_PYTHON_EXECUTABLE STREQUAL "")
  message(FATAL_ERROR "OPENMETA_PYTHON_EXECUTABLE is required")
endif()
if(NOT EXISTS "${OPENMETA_PYTHON_EXECUTABLE}")
  message(FATAL_ERROR "Python executable not found: ${OPENMETA_PYTHON_EXECUTABLE}")
endif()
if(NOT DEFINED OPENMETA_PYTHONPATH OR OPENMETA_PYTHONPATH STREQUAL "")
  message(FATAL_ERROR "OPENMETA_PYTHONPATH is required")
endif()

set(_py_code
"import openmeta

K = openmeta.MetadataCreationFieldKind
source = openmeta.create_metadata([
    openmeta.metadata_creation_text(K.Title, 'Before'),
    openmeta.metadata_creation_text(K.Creator, 'Alice'),
    openmeta.metadata_creation_text(K.Creator, 'Bob'),
    openmeta.metadata_creation_text(K.Keyword, 'night'),
    openmeta.metadata_creation_i32(K.Rating, 1),
])
edited = source.edit_metadata([
    openmeta.metadata_edit_set(
        openmeta.metadata_creation_text(K.Title, 'After')),
    openmeta.metadata_edit_set(
        openmeta.metadata_creation_text(K.Creator, 'Carol'), 1),
    openmeta.metadata_edit_add(
        openmeta.metadata_creation_text(K.Keyword, 'city')),
    openmeta.metadata_edit_remove(K.Creator, 0),
    openmeta.metadata_edit_set(
        openmeta.metadata_creation_i32(K.Rating, 5)),
])

assert openmeta.METADATA_EDITING_CONTRACT_VERSION == 1
assert source.entry_count == 5
assert edited.entry_count == 6
assert edited.path == ''
assert edited.build_transfer_source_snapshot().entry_count == 6

source_packet, _ = source.dump_xmp_portable(
    include_exif=False,
    include_iptc=False,
    include_existing_xmp=True,
)
edited_packet, _ = edited.dump_xmp_portable(
    include_exif=False,
    include_iptc=False,
    include_existing_xmp=True,
)
assert b'Before' in source_packet
assert b'Alice' in source_packet
assert b'After' in edited_packet
assert b'Carol' in edited_packet
assert b'city' in edited_packet
assert b'<xmp:Rating>5</xmp:Rating>' in edited_packet
assert b'Before' not in edited_packet
assert b'Alice' not in edited_packet

try:
    source.edit_metadata([
        openmeta.metadata_edit_add(
            openmeta.metadata_creation_text(K.Title, 'duplicate')),
    ])
except ValueError as exc:
    assert 'singleton_already_exists at operation 0' in str(exc)
else:
    raise AssertionError('duplicate singleton edit was accepted')

replaced = source.edit_metadata([
    openmeta.metadata_edit_remove_all(K.Title),
    openmeta.metadata_edit_add(
        openmeta.metadata_creation_text(K.Title, 'Replacement')),
])
replaced_packet, _ = replaced.dump_xmp_portable(
    include_exif=False,
    include_iptc=False,
    include_existing_xmp=True,
)
assert b'Replacement' in replaced_packet
assert b'Before' not in replaced_packet

dates = openmeta.create_metadata([
    openmeta.metadata_creation_text(
        K.CreateDate, '2024-08-30T01:02:03-02:30'),
    openmeta.metadata_creation_text(
        K.DateTimeOriginal, '2024-08-28T10:11:12.500Z'),
])
translated_dates = dates.translate_creation_dates(
    date_created_to_iptc_created=False)
assert openmeta.METADATA_DATE_TRANSLATION_CONTRACT_VERSION == 1
assert translated_dates.entry_count == dates.entry_count + 7
assert dates.entry_count == 2
translated_packet, _ = translated_dates.dump_xmp_portable(
    include_existing_xmp=True,
    conflict_policy=openmeta.XmpConflictPolicy.ExistingWins,
)
assert b'2024-08-30T01:02:03-02:30' in translated_packet
assert b'2024-08-28T10:11:12.500Z' in translated_packet

fractional = openmeta.create_metadata([
    openmeta.metadata_creation_text(
        K.CreateDate, '2024-08-30T01:02:03.125Z'),
])
try:
    fractional.translate_creation_dates(
        date_created_to_iptc_created=False)
except ValueError as exc:
    assert 'unsupported_precision for xmp_create_date' in str(exc)
else:
    raise AssertionError('lossy IPTC date translation was accepted')
fractional_exif = fractional.translate_creation_dates(
    create_date_to_iptc_digital_creation=False,
    date_created_to_iptc_created=False,
    date_time_original_to_exif_original=False,
)
assert fractional_exif.entry_count == fractional.entry_count + 3

technical = openmeta.create_metadata([
    openmeta.metadata_creation_text(
        K.ModifyDate, '2026-08-31T12:34:56.125+09:00'),
    openmeta.metadata_creation_text(K.CameraMake, 'OpenMeta Camera'),
    openmeta.metadata_creation_text(K.CameraModel, 'OM-1'),
    openmeta.metadata_creation_text(K.Software, 'OpenMeta Python'),
])
translated_technical = technical.translate_technical_metadata()
assert openmeta.METADATA_TECHNICAL_TRANSLATION_CONTRACT_VERSION == 1
assert translated_technical.entry_count == technical.entry_count + 6
assert technical.entry_count == 4

non_ascii_technical = openmeta.create_metadata([
    openmeta.metadata_creation_text(K.CameraMake, 'M' + chr(0xe4) + 'ke'),
])
try:
    non_ascii_technical.translate_technical_metadata()
except ValueError as exc:
    assert 'non_ascii_source for tiff_make' in str(exc)
else:
    raise AssertionError('non-ASCII EXIF technical translation was accepted')

capture = openmeta.create_metadata([
    openmeta.metadata_creation_urational(K.ExposureTime, 1, 125),
    openmeta.metadata_creation_urational(K.FNumber, 28, 10),
    openmeta.metadata_creation_u32(K.IsoSensitivity, 400),
    openmeta.metadata_creation_urational(K.FocalLength, 50, 1),
])
translated_capture = capture.translate_capture_metadata()
assert openmeta.METADATA_CAPTURE_TRANSLATION_CONTRACT_VERSION == 1
assert translated_capture.entry_count == capture.entry_count + 4
assert capture.entry_count == 4

oversized_iso = openmeta.create_metadata([
    openmeta.metadata_creation_u32(K.IsoSensitivity, 70000),
])
try:
    oversized_iso.translate_capture_metadata()
except ValueError as exc:
    assert 'value_out_of_range for xmp_iso' in str(exc)
else:
    raise AssertionError('out-of-range native EXIF ISO was accepted')

geometry = openmeta.create_metadata([
    openmeta.metadata_creation_u32(K.Orientation, 6),
    openmeta.metadata_creation_u32(K.PixelWidth, 640),
    openmeta.metadata_creation_u32(K.PixelHeight, 480),
])
target = openmeta.TransferTargetImageSpec()
target.has_dimensions = True
target.width = 640
target.height = 480
target.has_orientation = True
target.orientation = 6
translated_geometry = geometry.translate_image_geometry(target)
assert openmeta.METADATA_GEOMETRY_TRANSLATION_CONTRACT_VERSION == 1
assert translated_geometry.entry_count == geometry.entry_count + 5
assert geometry.entry_count == 3

mismatch = openmeta.TransferTargetImageSpec()
mismatch.has_dimensions = True
mismatch.width = 480
mismatch.height = 640
mismatch.has_orientation = True
mismatch.orientation = 6
try:
    geometry.translate_image_geometry(mismatch)
except ValueError as exc:
    assert 'target_image_spec_mismatch for xmp_dimensions' in str(exc)
else:
    raise AssertionError('mismatched target image geometry was accepted')

descriptive = openmeta.create_metadata([
    openmeta.metadata_creation_text(K.Title, 'Night ' + chr(0x666f)),
    openmeta.metadata_creation_text(K.Creator, 'Alice'),
    openmeta.metadata_creation_text(K.Creator, 'Bob'),
    openmeta.metadata_creation_text(K.Keyword, 'night'),
])
translated_descriptive = descriptive.translate_descriptive_metadata()
assert openmeta.METADATA_DESCRIPTIVE_TRANSLATION_CONTRACT_VERSION == 1
assert translated_descriptive.entry_count == descriptive.entry_count + 5
assert descriptive.entry_count == 4

oversized = openmeta.create_metadata([
    openmeta.metadata_creation_text(K.Title, 'x' * 65),
])
try:
    oversized.translate_descriptive_metadata()
except ValueError as exc:
    assert 'value_too_long for dc_title' in str(exc)
else:
    raise AssertionError('oversized IPTC title translation was accepted')

import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory() as temporary:
    location_path = Path(temporary) / 'location.jpg'
    xml = b'''<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">
<rdf:Description xmlns:p=\"http://ns.adobe.com/photoshop/1.0/\"
xmlns:i=\"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/\">
<p:City>Kyoto</p:City><i:Location>Garden</i:Location><p:State>Kyoto</p:State>
<p:Country>Japan</p:Country><i:CountryCode>JP</i:CountryCode>
</rdf:Description></rdf:RDF>'''
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    location_path.write_bytes(bytes.fromhex('ffd8ffe1') +
        (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    location = openmeta.read(str(location_path))
    clean = location.translate_location_metadata()
    assert clean.entry_count == location.entry_count
    translated_location = location.translate_location_metadata(
        source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All)
    assert openmeta.METADATA_LOCATION_TRANSLATION_CONTRACT_VERSION == 1
    assert translated_location.entry_count == location.entry_count + 5
    native_packet, _ = translated_location.dump_xmp_portable(
        include_existing_xmp=False, include_exif=False, include_iptc=True)
    assert b'Kyoto' in native_packet and b'Garden' in native_packet
    assert b'Japan' in native_packet and b'>JP<' in native_packet
    try:
        location.translate_location_metadata(
            source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
            max_added_entries=4)
    except ValueError as exc:
        assert 'entry_limit_exceeded' in str(exc)
    else:
        raise AssertionError('location entry limit was ignored')

with tempfile.TemporaryDirectory() as temporary:
    editorial_path = Path(temporary) / 'editorial.jpg'
    xml = b'''<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">
<rdf:Description xmlns:p=\"http://ns.adobe.com/photoshop/1.0/\">
<p:Headline>Garden opens</p:Headline><p:Instructions>Contact the editor</p:Instructions>
<p:TransmissionReference>JOB-42</p:TransmissionReference>
</rdf:Description></rdf:RDF>'''
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    editorial_path.write_bytes(bytes.fromhex('ffd8ffe1') +
        (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    editorial = openmeta.read(str(editorial_path))
    original_count = editorial.entry_count
    assert editorial.translate_editorial_metadata().entry_count == original_count
    translated_editorial = editorial.translate_editorial_metadata(
        source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All)
    assert openmeta.METADATA_EDITORIAL_TRANSLATION_CONTRACT_VERSION == 1
    assert openmeta.METADATA_EDITORIAL_TRANSLATION_MAX_ADDED_ENTRIES == 4
    assert translated_editorial.entry_count == original_count + 3
    native_packet, _ = translated_editorial.dump_xmp_portable(
        include_existing_xmp=False, include_exif=False, include_iptc=True)
    assert b'Garden opens' in native_packet and b'Contact the editor' in native_packet
    assert b'JOB-42' in native_packet
    assert editorial.entry_count == original_count
    for selected in range(3):
        one = editorial.translate_editorial_metadata(
            source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
            headline_to_iptc=selected == 0, instructions_to_iptc=selected == 1,
            transmission_reference_to_iptc=selected == 2)
        assert one.entry_count == original_count + 1
        packet, _ = one.dump_xmp_portable(
            include_existing_xmp=False, include_exif=False, include_iptc=True)
        for index, value in enumerate((b'Garden opens', b'Contact the editor', b'JOB-42')):
            assert (value in packet) == (index == selected)
    try:
        editorial.translate_editorial_metadata(
            source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
            max_added_entries=2)
    except ValueError as exc:
        assert 'entry_limit_exceeded' in str(exc)
    else:
        raise AssertionError('editorial entry limit was ignored')
    assert editorial.entry_count == original_count
    xml = xml.replace(b'JOB-42', b'R' * 33)
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    editorial_path.write_bytes(bytes.fromhex('ffd8ffe1') +
        (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    oversized_editorial = openmeta.read(str(editorial_path))
    try:
        oversized_editorial.translate_editorial_metadata(
            source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All)
    except ValueError as exc:
        assert 'value_too_long for photoshop_transmission_reference' in str(exc)
    else:
        raise AssertionError('oversized editorial identifier was accepted')

with tempfile.TemporaryDirectory() as temporary:
    batch_path = Path(temporary) / 'iptc.jpg'
    xml = b'''<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">
<rdf:Description xmlns:p=\"http://ns.adobe.com/photoshop/1.0/\"
xmlns:dc=\"http://purl.org/dc/elements/1.1/\"
xmlns:i=\"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/\">
<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">Title</rdf:li></rdf:Alt></dc:title>
<dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">Caption</rdf:li></rdf:Alt></dc:description>
<dc:rights><rdf:Alt><rdf:li xml:lang=\"x-default\">Copyright</rdf:li></rdf:Alt></dc:rights>
<dc:creator><rdf:Seq><rdf:li>Alice</rdf:li></rdf:Seq></dc:creator>
<dc:subject><rdf:Bag><rdf:li>Garden</rdf:li></rdf:Bag></dc:subject>
<p:Credit>Credit</p:Credit><p:Source>Source</p:Source>
<p:City>Kyoto</p:City><i:Location>Garden</i:Location><p:State>Kyoto</p:State>
<p:Country>Japan</p:Country><i:CountryCode>JP</i:CountryCode>
<p:Headline>Garden opens</p:Headline><p:Instructions>Contact editor</p:Instructions>
<p:TransmissionReference>JOB-42</p:TransmissionReference>
<p:AuthorsPosition>Photographer</p:AuthorsPosition><p:CaptionWriter>Editor</p:CaptionWriter>
<p:Category>ART</p:Category><p:Urgency>5</p:Urgency>
<p:SupplementalCategories><rdf:Bag><rdf:li>Painting</rdf:li><rdf:li>Gallery</rdf:li></rdf:Bag></p:SupplementalCategories>
</rdf:Description></rdf:RDF>'''
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    batch_path.write_bytes(bytes.fromhex('ffd8ffe1') +
        (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    batch = openmeta.read(str(batch_path))
    count = batch.entry_count
    assert batch.translate_iptc_metadata().entry_count == count
    translated = batch.translate_iptc_metadata(
        source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All)
    assert openmeta.METADATA_IPTC_TRANSLATION_CONTRACT_VERSION == 1
    assert openmeta.METADATA_IPTC_TRANSLATION_MAX_ADDED_ENTRIES == 1025
    assert translated.entry_count == count + 21
    assert batch.entry_count == count
    native, _ = translated.dump_xmp_portable(
        include_existing_xmp=False, include_exif=False, include_iptc=True)
    assert b'Photographer' in native and b'Editor' in native and b'ART' in native
    assert b'Painting' in native and b'Gallery' in native
    assert b'<photoshop:Urgency>5</photoshop:Urgency>' in native
    assert translated.translate_iptc_metadata(
        source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All).entry_count == count + 21
    flags = ('title_to_iptc_object_name', 'description_to_iptc_caption',
        'creators_to_iptc_bylines', 'keywords_to_iptc_keywords',
        'copyright_to_iptc_copyright', 'credit_to_iptc_credit', 'source_to_iptc_source',
        'city_to_iptc', 'sublocation_to_iptc', 'state_to_iptc', 'country_to_iptc',
        'country_code_to_iptc', 'headline_to_iptc', 'instructions_to_iptc',
        'transmission_reference_to_iptc', 'authors_position_to_iptc',
        'caption_writer_to_iptc', 'category_to_iptc', 'supplemental_categories_to_iptc',
        'urgency_to_iptc')
    for selected in flags:
        one = batch.translate_iptc_metadata(
            source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
            **{flag: flag == selected for flag in flags})
        assert one.entry_count == count + (2 if selected == 'supplemental_categories_to_iptc' else 1), selected
    try:
        batch.translate_iptc_metadata(
            source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
            max_added_entries=20)
    except ValueError as exc:
        assert 'entry_limit_exceeded' in str(exc)
    else:
        raise AssertionError('combined IPTC entry limit was ignored')
    xml = xml.replace(b'<p:Urgency>5</p:Urgency>', b'<p:Urgency>9</p:Urgency>')
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    batch_path.write_bytes(bytes.fromhex('ffd8ffe1') +
        (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    invalid = openmeta.read(str(batch_path))
    try:
        invalid.translate_iptc_metadata(source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All)
    except ValueError as exc:
        assert 'invalid_source_value for photoshop_urgency' in str(exc)
    else:
        raise AssertionError('invalid priority was accepted')
    assert batch.entry_count == count

with tempfile.TemporaryDirectory() as temporary:
    gps_path = Path(temporary) / 'gps.jpg'
    xml = b'''<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">
<rdf:Description xmlns:e=\"http://ns.adobe.com/exif/1.0/\">
<e:GPSLatitude>35,48.125N</e:GPSLatitude><e:GPSLongitude>139,34,55.25W</e:GPSLongitude>
<e:GPSAltitude>2469/20</e:GPSAltitude><e:GPSAltitudeRef>1</e:GPSAltitudeRef>
</rdf:Description></rdf:RDF>'''
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    gps_path.write_bytes(bytes.fromhex('ffd8ffe1') +
        (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    gps = openmeta.read(str(gps_path))
    count = gps.entry_count
    assert openmeta.METADATA_GPS_TRANSLATION_CONTRACT_VERSION == 1
    assert openmeta.METADATA_GPS_TRANSLATION_MAX_ADDED_ENTRIES == 7
    assert gps.translate_gps_metadata().entry_count == count
    translated = gps.translate_gps_metadata(source_mode=openmeta.MetadataGpsTranslationSourceMode.All)
    assert translated.entry_count == count + 7
    native, _ = translated.dump_xmp_portable(include_existing_xmp=False, include_iptc=False)
    assert b'<exif:GPSLatitude>35,48.125N</exif:GPSLatitude>' in native
    assert b'<exif:GPSVersionID>2.3.0.0</exif:GPSVersionID>' in native
    assert translated.translate_gps_metadata(
        source_mode=openmeta.MetadataGpsTranslationSourceMode.All).entry_count == count + 7
    for selected in ('latitude_to_exif', 'longitude_to_exif', 'altitude_to_exif'):
        one = gps.translate_gps_metadata(source_mode=openmeta.MetadataGpsTranslationSourceMode.All,
            **{name: name == selected for name in ('latitude_to_exif', 'longitude_to_exif', 'altitude_to_exif')})
        assert one.entry_count == count + 3
    try:
        gps.translate_gps_metadata(source_mode=openmeta.MetadataGpsTranslationSourceMode.All,
            max_added_entries=6)
    except ValueError as exc:
        assert 'entry_limit_exceeded' in str(exc)
    else:
        raise AssertionError('GPS version was omitted from entry accounting')
    xml = xml.replace(b'35,48.125N', b'91,0N')
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    gps_path.write_bytes(bytes.fromhex('ffd8ffe1') +
        (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    invalid = openmeta.read(str(gps_path))
    try:
        invalid.translate_gps_metadata(source_mode=openmeta.MetadataGpsTranslationSourceMode.All)
    except ValueError as exc:
        assert 'value_out_of_range for exif_gps_latitude' in str(exc)
    else:
        raise AssertionError('invalid GPS latitude accepted')
    assert gps.entry_count == count

with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / 'structured.jpg'
    xml = b'''<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">
<rdf:Description xmlns:l=\"http://iptc.org/std/Iptc4xmpExt/2008-02-29/\">
<l:LocationShown><rdf:Bag><rdf:li rdf:parseType=\"Resource\">
<l:City>Kyoto</l:City><l:Sublocation>Garden</l:Sublocation><l:ProvinceState>Kyoto</l:ProvinceState>
<l:CountryName>Japan</l:CountryName><l:CountryCode>JPN</l:CountryCode></rdf:li>
<rdf:li rdf:parseType=\"Resource\"><l:City>Osaka</l:City></rdf:li></rdf:Bag></l:LocationShown>
<l:LocationCreated rdf:parseType=\"Resource\"><l:City>Nara</l:City></l:LocationCreated>
</rdf:Description></rdf:RDF>'''
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    path.write_bytes(bytes.fromhex('ffd8ffe1') + (len(packet) + 2).to_bytes(2, 'big') +
        packet + bytes.fromhex('ffd9'))
    document = openmeta.read(str(path))
    count = document.entry_count
    assert openmeta.METADATA_STRUCTURED_LOCATION_TRANSLATION_CONTRACT_VERSION == 1
    assert openmeta.METADATA_STRUCTURED_LOCATION_TRANSLATION_MAX_ADDED_ENTRIES == 11
    mode = openmeta.MetadataDescriptiveTranslationSourceMode.All
    try:
        document.translate_structured_location_metadata(source_mode=mode)
    except ValueError as exc:
        assert 'ambiguous_location' in str(exc)
    else:
        raise AssertionError('multiple structured locations silently selected')
    assert document.translate_structured_location_metadata(location_index=1).entry_count == count
    translated = document.translate_structured_location_metadata(location_index=1, source_mode=mode)
    assert translated.entry_count == count + 10
    assert translated.translate_structured_location_metadata(location_index=1, source_mode=mode).entry_count == count + 10
    payload, _ = translated.dump_xmp_portable()
    assert b'<photoshop:City>Kyoto</photoshop:City>' in payload
    for selected in ('city', 'sublocation', 'state', 'country', 'country_code'):
        one = document.translate_structured_location_metadata(location_index=1, source_mode=mode,
            **{name: name == selected for name in ('city', 'sublocation', 'state', 'country', 'country_code')})
        assert one.entry_count == count + 2
    created = document.translate_structured_location_metadata(
        location_kind=openmeta.MetadataStructuredLocationKind.Created, source_mode=mode)
    payload, _ = created.dump_xmp_portable()
    assert b'<photoshop:City>Nara</photoshop:City>' in payload
    try:
        document.translate_structured_location_metadata(location_index=9, source_mode=mode)
    except ValueError as exc:
        assert 'location_not_found' in str(exc)
    else:
        raise AssertionError('missing location index accepted')
    assert document.entry_count == count

with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / 'gps_navigation.jpg'
    xml = b'''<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">
<rdf:Description xmlns:e=\"http://ns.adobe.com/exif/1.0/\">
<e:GPSTimeStamp>2024-03-01T00:30:12.125+01:00</e:GPSTimeStamp>
<e:GPSSpeedRef>knots</e:GPSSpeedRef><e:GPSSpeed>12345/100</e:GPSSpeed>
<e:GPSTrackRef>True North</e:GPSTrackRef><e:GPSTrack>359.99</e:GPSTrack>
<e:GPSImgDirectionRef>M</e:GPSImgDirectionRef><e:GPSImgDirection>45.5</e:GPSImgDirection>
</rdf:Description></rdf:RDF>'''
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    path.write_bytes(bytes.fromhex('ffd8ffe1') + (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    document = openmeta.read(str(path))
    count = document.entry_count
    assert openmeta.METADATA_GPS_NAVIGATION_TRANSLATION_CONTRACT_VERSION == 1
    assert openmeta.METADATA_GPS_NAVIGATION_TRANSLATION_MAX_ADDED_ENTRIES == 9
    assert openmeta.METADATA_GPS_NAVIGATION_TRANSLATION_MAX_TOTAL_TEXT_BYTES == 896
    assert document.translate_gps_navigation_metadata().entry_count == count
    mode = openmeta.MetadataGpsTranslationSourceMode.All
    translated = document.translate_gps_navigation_metadata(source_mode=mode)
    assert translated.entry_count == count + 9
    assert translated.translate_gps_navigation_metadata(source_mode=mode).entry_count == count + 9
    payload, _ = translated.dump_xmp_portable()
    assert b'2024-02-29T23:30:12.125Z' in payload
    for selected in ('timestamp_to_exif', 'speed_to_exif', 'track_to_exif', 'image_direction_to_exif'):
        one = document.translate_gps_navigation_metadata(source_mode=mode,
            **{name: name == selected for name in ('timestamp_to_exif', 'speed_to_exif', 'track_to_exif', 'image_direction_to_exif')})
        assert one.entry_count == count + 3
    try:
        document.translate_gps_navigation_metadata(source_mode=mode, max_added_entries=8)
    except ValueError as exc:
        assert 'entry_limit_exceeded' in str(exc)
    else:
        raise AssertionError('GPS navigation addition budget ignored')
    assert document.entry_count == count

with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / 'gps_destination.jpg'
    xml = b'''<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">
<rdf:Description xmlns:e=\"http://ns.adobe.com/exif/1.0/\">
<e:GPSDestLatitude>35,48.125S</e:GPSDestLatitude>
<e:GPSDestLongitude>139,34,55.25E</e:GPSDestLongitude>
<e:GPSDestBearingRef>T</e:GPSDestBearingRef><e:GPSDestBearing>359.99</e:GPSDestBearing>
<e:GPSDestDistanceRef>Nautical miles</e:GPSDestDistanceRef><e:GPSDestDistance>12345/100</e:GPSDestDistance>
</rdf:Description></rdf:RDF>'''
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    path.write_bytes(bytes.fromhex('ffd8ffe1') + (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    document = openmeta.read(str(path))
    count = document.entry_count
    assert openmeta.METADATA_GPS_DESTINATION_TRANSLATION_CONTRACT_VERSION == 1
    assert openmeta.METADATA_GPS_DESTINATION_TRANSLATION_MAX_ADDED_ENTRIES == 9
    assert openmeta.METADATA_GPS_DESTINATION_TRANSLATION_MAX_TOTAL_TEXT_BYTES == 768
    assert document.translate_gps_destination_metadata().entry_count == count
    mode = openmeta.MetadataGpsTranslationSourceMode.All
    translated = document.translate_gps_destination_metadata(source_mode=mode)
    assert translated.entry_count == count + 9
    assert translated.translate_gps_destination_metadata(source_mode=mode).entry_count == count + 9
    payload, _ = translated.dump_xmp_portable()
    assert b'<exif:GPSDestDistanceRef>Nautical miles</exif:GPSDestDistanceRef>' in payload
    for selected in ('latitude_to_exif', 'longitude_to_exif', 'bearing_to_exif', 'distance_to_exif'):
        one = document.translate_gps_destination_metadata(source_mode=mode,
            **{name: name == selected for name in ('latitude_to_exif', 'longitude_to_exif', 'bearing_to_exif', 'distance_to_exif')})
        assert one.entry_count == count + 3
    try:
        document.translate_gps_destination_metadata(source_mode=mode, max_added_entries=8)
    except ValueError as exc:
        assert 'entry_limit_exceeded' in str(exc)
    else:
        raise AssertionError('GPS destination addition budget ignored')
    assert document.entry_count == count

with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / 'gps_quality.jpg'
    xml = b'''<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">
<rdf:Description xmlns:e=\"http://ns.adobe.com/exif/1.0/\">
<e:GPSStatus>V</e:GPSStatus><e:GPSMeasureMode>3</e:GPSMeasureMode><e:GPSDOP>1.25</e:GPSDOP>
<e:GPSDifferential>1</e:GPSDifferential><e:GPSHPositioningError>4.5</e:GPSHPositioningError>
</rdf:Description></rdf:RDF>'''
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    path.write_bytes(bytes.fromhex('ffd8ffe1') + (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    document = openmeta.read(str(path))
    count = document.entry_count
    assert openmeta.METADATA_GPS_QUALITY_TRANSLATION_CONTRACT_VERSION == 1
    assert openmeta.METADATA_GPS_QUALITY_TRANSLATION_MAX_ADDED_ENTRIES == 6
    assert openmeta.METADATA_GPS_QUALITY_TRANSLATION_MAX_TOTAL_TEXT_BYTES == 640
    assert document.translate_gps_quality_metadata().entry_count == count
    mode = openmeta.MetadataGpsTranslationSourceMode.All
    translated = document.translate_gps_quality_metadata(source_mode=mode)
    assert translated.entry_count == count + 6
    assert translated.translate_gps_quality_metadata(source_mode=mode).entry_count == count + 6
    payload, _ = translated.dump_xmp_portable()
    assert b'<exif:GPSStatus>Measurement Void</exif:GPSStatus>' in payload
    for selected in ('status_to_exif', 'measure_mode_to_exif', 'dop_to_exif', 'differential_to_exif', 'horizontal_error_to_exif'):
        one = document.translate_gps_quality_metadata(source_mode=mode,
            **{name: name == selected for name in ('status_to_exif', 'measure_mode_to_exif', 'dop_to_exif', 'differential_to_exif', 'horizontal_error_to_exif')})
        assert one.entry_count == count + 2
    try:
        document.translate_gps_quality_metadata(source_mode=mode, max_added_entries=5)
    except ValueError as exc:
        assert 'entry_limit_exceeded' in str(exc)
    else:
        raise AssertionError('GPS quality addition budget ignored')
    assert document.entry_count == count

with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / 'gps_text.jpg'
    xml = b'''<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">
<rdf:Description xmlns:e=\"http://ns.adobe.com/exif/1.0/\">
<e:GPSSatellites>04 07 12</e:GPSSatellites><e:GPSMapDatum>WGS-84</e:GPSMapDatum>
<e:GPSProcessingMethod>GPS WLAN</e:GPSProcessingMethod><e:GPSAreaInformation>Tokyo</e:GPSAreaInformation>
</rdf:Description></rdf:RDF>'''
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    path.write_bytes(bytes.fromhex('ffd8ffe1') + (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    document = openmeta.read(str(path))
    count = document.entry_count
    assert openmeta.METADATA_GPS_TEXT_TRANSLATION_CONTRACT_VERSION == 1
    assert openmeta.METADATA_GPS_TEXT_TRANSLATION_MAX_ADDED_ENTRIES == 5
    assert openmeta.METADATA_GPS_TEXT_TRANSLATION_MAX_TOTAL_TEXT_BYTES == 16384
    assert document.translate_gps_text_metadata().entry_count == count
    mode = openmeta.MetadataGpsTranslationSourceMode.All
    translated = document.translate_gps_text_metadata(source_mode=mode)
    assert translated.entry_count == count + 5
    assert translated.translate_gps_text_metadata(source_mode=mode).entry_count == count + 5
    payload, _ = translated.dump_xmp_portable()
    assert b'<exif:GPSProcessingMethod>GPS WLAN</exif:GPSProcessingMethod>' in payload
    for selected in ('satellites_to_exif', 'map_datum_to_exif', 'processing_method_to_exif', 'area_information_to_exif'):
        one = document.translate_gps_text_metadata(source_mode=mode,
            **{name: name == selected for name in ('satellites_to_exif', 'map_datum_to_exif', 'processing_method_to_exif', 'area_information_to_exif')})
        assert one.entry_count == count + 2
    try:
        document.translate_gps_text_metadata(source_mode=mode, max_added_entries=4)
    except ValueError as exc:
        assert 'entry_limit_exceeded' in str(exc)
    else:
        raise AssertionError('GPS text addition budget ignored')
    assert document.entry_count == count

with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / 'capture_locations.jpg'
    setting_names = ('ExposureProgram', 'MeteringMode', 'SensingMethod', 'CustomRendered', 'ExposureMode', 'WhiteBalance', 'SceneCaptureType', 'GainControl', 'Contrast', 'Saturation', 'Sharpness', 'SubjectDistanceRange')
    codes = (3, 5, 2, 1, 2, 1, 3, 4, 2, 1, 2, 3)
    flag_names = ('exposure_program', 'metering_mode', 'sensing_method', 'custom_rendered', 'exposure_mode', 'white_balance', 'scene_capture_type', 'gain_control', 'contrast', 'saturation', 'sharpness', 'subject_distance_range')
    xml = ('<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"><rdf:Description xmlns:e=\"http://ns.adobe.com/exif/1.0/\" xmlns:p=\"http://ns.adobe.com/photoshop/1.0/\" xmlns:c=\"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/\">' + ''.join('<e:' + name + '>' + str(code) + '</e:' + name + '>' for name, code in zip(setting_names, codes)) + '<p:City>Kyoto</p:City><c:Location>Garden</c:Location><p:State>Kyoto</p:State><p:Country>Japan</p:Country><c:CountryCode>JPN</c:CountryCode></rdf:Description></rdf:RDF>').encode()
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    path.write_bytes(bytes.fromhex('ffd8ffe1') + (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    document = openmeta.read(str(path))
    count = document.entry_count
    assert openmeta.METADATA_CAPTURE_SETTINGS_TRANSLATION_CONTRACT_VERSION == 1
    assert document.translate_capture_settings_metadata().entry_count == count
    translated = document.translate_capture_settings_metadata(source_mode=openmeta.MetadataCaptureTranslationSourceMode.All)
    assert translated.entry_count == count + 12
    assert translated.translate_capture_settings_metadata(source_mode=openmeta.MetadataCaptureTranslationSourceMode.All).entry_count == count + 12
    for selected in flag_names:
        one = document.translate_capture_settings_metadata(source_mode=openmeta.MetadataCaptureTranslationSourceMode.All, **{name + '_to_exif': name == selected for name in flag_names})
        assert one.entry_count == count + 1
    payload, _ = translated.dump_xmp_portable()
    for name in setting_names:
        assert ('<exif:' + name + '>').encode() in payload
    assert openmeta.METADATA_LOCATION_CREATION_TRANSLATION_CONTRACT_VERSION == 1
    for kind, root in ((openmeta.MetadataStructuredLocationKind.Shown, b'LocationShown'), (openmeta.MetadataStructuredLocationKind.Created, b'LocationCreated')):
        location = document.translate_location_to_structured_metadata(kind, 1, source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All)
        assert location.entry_count == count + 5
        payload, _ = location.dump_xmp_portable(include_existing_xmp=True)
        assert b'<Iptc4xmpExt:' + root + b'>' in payload
        assert b'<rdf:Bag>' in payload
        assert b'<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>' in payload
    try:
        document.translate_location_to_structured_metadata()
    except TypeError:
        pass
    else:
        raise AssertionError('structured destination was inferred')
    assert document.entry_count == count

with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / 'capture_rationals.jpg'
    xml = b'<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"><rdf:Description xmlns:e=\"http://ns.adobe.com/exif/1.0/\"><e:SubjectDistance>4294967295/3</e:SubjectDistance><e:DigitalZoomRatio>0</e:DigitalZoomRatio><e:ExposureIndex>200</e:ExposureIndex><e:FlashEnergy>7/3</e:FlashEnergy></rdf:Description></rdf:RDF>'
    packet = b'http://ns.adobe.com/xap/1.0/' + bytes([0]) + xml
    path.write_bytes(bytes.fromhex('ffd8ffe1') + (len(packet) + 2).to_bytes(2, 'big') + packet + bytes.fromhex('ffd9'))
    document = openmeta.read(str(path))
    count = document.entry_count
    assert openmeta.METADATA_CAPTURE_RATIONAL_TRANSLATION_CONTRACT_VERSION == 1
    assert document.translate_capture_rational_metadata().entry_count == count
    translated = document.translate_capture_rational_metadata(source_mode=openmeta.MetadataCaptureTranslationSourceMode.All)
    assert translated.entry_count == count + 4
    flags = ('subject_distance', 'digital_zoom_ratio', 'exposure_index', 'flash_energy')
    for selected in flags:
        one = document.translate_capture_rational_metadata(source_mode=openmeta.MetadataCaptureTranslationSourceMode.All, **{name + '_to_exif': name == selected for name in flags})
        assert one.entry_count == count + 1
    assert translated.translate_capture_rational_metadata(source_mode=openmeta.MetadataCaptureTranslationSourceMode.All).entry_count == count + 4
    assert document.entry_count == count

print('openmeta metadata editing smoke ok')
")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${OPENMETA_PYTHONPATH}"
          "${OPENMETA_PYTHON_EXECUTABLE}" -c "${_py_code}"
  RESULT_VARIABLE _rv
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)
if(NOT _rv EQUAL 0)
  message(FATAL_ERROR
    "python metadata editing smoke failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

message(STATUS "python metadata editing smoke gate passed")
