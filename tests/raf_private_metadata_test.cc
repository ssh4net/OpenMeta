// SPDX-License-Identifier: Apache-2.0
#include "../src/openmeta/raf_decode_internal.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_key.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <vector>

namespace openmeta {
namespace {
static void u16(std::span<std::byte> b, size_t p, uint16_t v)
{ b[p] = std::byte(v & 255U); b[p + 1] = std::byte(v >> 8U); }
static void u32(std::span<std::byte> b, size_t p, uint32_t v, bool be = false)
{ for (size_t i = 0; i < 4; ++i) b[p + i] = std::byte((v >> (8U * (be ? 3U - i : i))) & 255U); }
static void field(std::span<std::byte> b, size_t p, uint16_t tag, uint16_t type,
                  uint32_t count, uint32_t value)
{ u16(b,p,tag); u16(b,p+2,type); u32(b,p+4,count); u32(b,p+8,value); }
static std::vector<std::byte> fixture()
{
    std::vector<std::byte> b(8192U);
    std::memcpy(b.data(), "FUJIFILMCCD-RAW ", 16U);
    u32(b,0x64,256U,true); u32(b,0x68,uint32_t(b.size()-256U),true);
    const auto t = std::span<std::byte>(b).subspan(256U);
    t[0]=std::byte{'I'}; t[1]=std::byte{'I'}; u16(t,2,42); u32(t,4,8);
    u16(t,8,1); field(t,10,0xf000,13,1,26);
    u16(t,26,8);
    field(t,28,0xf001,4,1,4992); field(t,40,0xf002,4,1,3296);
    field(t,52,0xf003,4,1,14); field(t,64,0xf006,4,1,0);
    field(t,76,0xf007,4,1,512); field(t,88,0xf008,4,1,7424);
    field(t,100,0xf00a,3,4,176); field(t,112,0xf00e,4,3,184);
    for (uint16_t i=0; i<4; ++i) u16(t,176+2U*i,512U+i);
    u32(t,184,256); u32(t,188,485); u32(t,192,396);
    return b;
}
struct Input { std::span<const std::byte> bytes; uint64_t end; bool bad=false; };
static RandomAccessIoResult read_at(void* p, uint64_t at, std::span<std::byte> out) noexcept
{
    Input& in = *static_cast<Input*>(p);
    if (at>in.end || out.size()>in.end-at) { in.bad=true; return {RandomAccessIoCode::IoError,0}; }
    if (at>in.bytes.size() || out.size()>in.bytes.size()-at) return {RandomAccessIoCode::Ok,0};
    std::memcpy(out.data(),in.bytes.data()+at,out.size()); return {RandomAccessIoCode::Ok,out.size()};
}
static ExifRandomAccessDecodeResult decode(std::span<const std::byte> bytes, MetaStore& store,
    bool callback, const ExifDecodeLimits& limits={}, size_t value_bytes=256U, size_t window_bytes=4096U)
{
    if (!callback) {
        ExifRandomAccessDecodeResult r;
        r.decode=raf_internal::decode_raf_native(bytes,store,limits); return r;
    }
    constexpr size_t prefix=37U;
    std::vector<std::byte> prefixed(prefix+bytes.size());
    std::memcpy(prefixed.data()+prefix,bytes.data(),bytes.size());
    Input in{prefixed,prefix+768U};
    RandomAccessSource source; source.size=prefixed.size(); source.context=&in; source.read_at=read_at;
    std::vector<std::byte> window(window_bytes), value(value_bytes);
    ExifRandomAccessScratch scratch; scratch.read_window=window; scratch.value=value;
    scratch.window_options.minimum_read_bytes=4096U;
    const auto r=raf_internal::decode_raf_native_random_access(
        make_random_access_source_range(source,prefix,bytes.size()),store,scratch,limits,{});
    EXPECT_FALSE(in.bad); return r;
}
static const Entry* find(const MetaStore& s, uint16_t tag)
{
    const auto ids=s.find_all(make_exif_tag_key_view("raf_tiff_0_0",tag));
    return ids.empty()?nullptr:&s.entry(ids[0]);
}
}
TEST(RafPrivateMetadata, TypedFieldsMatchMemoryAndBoundedPrefixedCallback)
{
    const auto b=fixture(); const auto original=b;
    for (bool callback:{false,true}) {
        MetaStore store; const auto r=decode(b,store,callback);
        EXPECT_EQ(r.decode.status,ExifDecodeStatus::Ok); EXPECT_TRUE(r.input.ok()); store.finalize();
        for (const auto pair: {std::pair<uint16_t,uint64_t>{0xf001,4992},{0xf002,3296},{0xf003,14},
                              {0xf006,0},{0xf007,512},{0xf008,7424}}) {
            const Entry* e=find(store,pair.first); ASSERT_NE(e,nullptr);
            EXPECT_EQ(e->origin.wire_type.code,4U); EXPECT_EQ(e->origin.wire_count,1U);
            EXPECT_EQ(e->value.data.u64,pair.second);
        }
        const Entry* black=find(store,0xf00a); ASSERT_NE(black,nullptr);
        EXPECT_EQ(black->origin.wire_type.code,3U); EXPECT_EQ(black->origin.wire_count,4U);
        const auto raw=store.arena().span(black->value.data.span); ASSERT_EQ(raw.size(),8U);
        for (uint16_t i=0;i<4;++i) { uint16_t v=0; std::memcpy(&v,raw.data()+2U*i,2); EXPECT_EQ(v,512U+i); }
        EXPECT_EQ(exif_entry_name(store,*black,ExifTagNamePolicy::Canonical),"BlackLevel");
        const Entry* wb=find(store,0xf00e); ASSERT_NE(wb,nullptr);
        EXPECT_EQ(wb->origin.wire_type.code,4U); EXPECT_EQ(wb->origin.wire_count,3U);
        EXPECT_EQ(exif_entry_name(store,*wb,ExifTagNamePolicy::Canonical),"WB_GRBLevels");
        const auto values=store.arena().span(wb->value.data.span); ASSERT_EQ(values.size(),12U);
        const uint32_t expected[]={256,485,396};
        for (size_t i=0;i<3;++i) { uint32_t v=0; std::memcpy(&v,values.data()+4U*i,4); EXPECT_EQ(v,expected[i]); }
    }
    EXPECT_EQ(b,original);
}
TEST(RafPrivateMetadata, RejectsCyclesInvalidEdgesAndOutsideOffsets)
{
    for (bool callback:{false,true}) for (uint32_t variant=0;variant<4;++variant) {
        auto b=fixture(); auto t=std::span<std::byte>(b).subspan(256);
        if (variant==0) field(t,28,0xf000,13,1,26);
        if (variant==1) field(t,10,0xf000,3,1,26);
        if (variant==2) field(t,10,0xf000,13,2,26);
        if (variant==3) field(t,10,0xf000,13,1,9000);
        MetaStore store; EXPECT_EQ(decode(b,store,callback).decode.status,ExifDecodeStatus::Malformed);
    }
}
TEST(RafPrivateMetadata, EnforcesIfdEntryValueAndStoreBudgets)
{
    for (bool callback:{false,true}) for (uint32_t variant=0;variant<4;++variant) {
        ExifDecodeLimits limits;
        if (variant==0) limits.max_ifds=1;
        if (variant==1) limits.max_entries_per_ifd=7;
        if (variant==2) limits.max_value_bytes=8;
        if (variant==3) limits.max_total_entries=2;
        MetaStore store; EXPECT_EQ(decode(fixture(),store,callback,limits).decode.status,ExifDecodeStatus::LimitExceeded);
        if (variant==3) EXPECT_LE(store.entries().size(),2U);
    }
}
TEST(RafPrivateMetadata, ReportsExactValueScratchNeed)
{
    auto b=fixture(); auto t=std::span<std::byte>(b).subspan(256);
    field(t,112,0xf00e,4,6,184);
    MetaStore store; const auto r=decode(b,store,true,{},8U,16U);
    EXPECT_EQ(r.decode.status,ExifDecodeStatus::OutputTruncated);
    EXPECT_EQ(r.value_scratch_needed,24U);
}
} // namespace openmeta
