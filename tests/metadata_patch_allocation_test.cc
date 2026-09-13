// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_patch.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#if defined(_MSC_VER)
#    include <malloc.h>
#    if defined(_DEBUG)
#        include <crtdbg.h>
#    endif
#endif

namespace {
thread_local bool counting        = false;
thread_local uint64_t allocations = 0U;

void
note_allocation() noexcept
{
    if (counting)
        ++allocations;
}
}  // namespace

#if defined(OPENMETA_WRAP_HEAP)
extern "C" {
void*
__real_malloc(size_t);
void*
__real_calloc(size_t, size_t);
void*
__real_realloc(void*, size_t);
void*
__real_aligned_alloc(size_t, size_t);
int
__real_posix_memalign(void**, size_t, size_t);
void*
__wrap_malloc(size_t n)
{
    note_allocation();
    return __real_malloc(n);
}
void*
__wrap_calloc(size_t n, size_t size)
{
    note_allocation();
    return __real_calloc(n, size);
}
void*
__wrap_realloc(void* p, size_t n)
{
    note_allocation();
    return __real_realloc(p, n);
}
void*
__wrap_aligned_alloc(size_t alignment, size_t n)
{
    note_allocation();
    return __real_aligned_alloc(alignment, n);
}
int
__wrap_posix_memalign(void** p, size_t alignment, size_t n)
{
    note_allocation();
    return __real_posix_memalign(p, alignment, n);
}
}
#endif

namespace {
void*
allocate(size_t size, size_t alignment) noexcept
{
    note_allocation();
    if (size == 0U)
        size = 1U;
    if (alignment == 0U)
        return std::malloc(size);
#if defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
#else
    void* result = nullptr;
    return posix_memalign(&result, alignment, size) == 0 ? result : nullptr;
#endif
}
void
deallocate(void* p, bool aligned) noexcept
{
#if defined(_MSC_VER)
    if (aligned) {
        _aligned_free(p);
        return;
    }
#else
    (void)aligned;
#endif
    std::free(p);
}
void*
require_memory(void* p) noexcept
{
    if (!p)
        std::abort();
    return p;
}
}  // namespace

void*
operator new(size_t n)
{
    return require_memory(allocate(n, 0U));
}
void*
operator new[](size_t n)
{
    return require_memory(allocate(n, 0U));
}
void*
operator new(size_t n, const std::nothrow_t&) noexcept
{
    return allocate(n, 0U);
}
void*
operator new[](size_t n, const std::nothrow_t&) noexcept
{
    return allocate(n, 0U);
}
void*
operator new(size_t n, std::align_val_t a)
{
    return require_memory(allocate(n, static_cast<size_t>(a)));
}
void*
operator new[](size_t n, std::align_val_t a)
{
    return require_memory(allocate(n, static_cast<size_t>(a)));
}
void*
operator new(size_t n, std::align_val_t a, const std::nothrow_t&) noexcept
{
    return allocate(n, static_cast<size_t>(a));
}
void*
operator new[](size_t n, std::align_val_t a, const std::nothrow_t&) noexcept
{
    return allocate(n, static_cast<size_t>(a));
}
void
operator delete(void* p) noexcept
{
    deallocate(p, false);
}
void
operator delete[](void* p) noexcept
{
    deallocate(p, false);
}
void
operator delete(void* p, size_t) noexcept
{
    deallocate(p, false);
}
void
operator delete[](void* p, size_t) noexcept
{
    deallocate(p, false);
}
void
operator delete(void* p, const std::nothrow_t&) noexcept
{
    deallocate(p, false);
}
void
operator delete[](void* p, const std::nothrow_t&) noexcept
{
    deallocate(p, false);
}
void
operator delete(void* p, std::align_val_t) noexcept
{
    deallocate(p, true);
}
void
operator delete[](void* p, std::align_val_t) noexcept
{
    deallocate(p, true);
}
void
operator delete(void* p, size_t, std::align_val_t) noexcept
{
    deallocate(p, true);
}
void
operator delete[](void* p, size_t, std::align_val_t) noexcept
{
    deallocate(p, true);
}
void
operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept
{
    deallocate(p, true);
}
void
operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept
{
    deallocate(p, true);
}

namespace {
#if defined(_MSC_VER) && defined(_DEBUG)
int __cdecl crt_allocation_hook(int type, void*, size_t, int, long,
                                const unsigned char*, int)
{
    if (type == _HOOK_ALLOC || type == _HOOK_REALLOC)
        note_allocation();
    return 1;
}
#endif

bool
probe_allocators()
{
    counting           = true;
    void* p            = ::operator new(32U, std::nothrow);
    const bool saw_new = allocations != 0U;
    ::operator delete(p, std::nothrow);
    allocations = 0U;
    p           = ::operator new[](64U, std::align_val_t(64U), std::nothrow);
    const bool saw_aligned_new = allocations != 0U;
    ::operator delete[](p, std::align_val_t(64U), std::nothrow);
    counting   = false;
    bool saw_c = true;
#if defined(OPENMETA_WRAP_HEAP) || (defined(_MSC_VER) && defined(_DEBUG))
    void* (*volatile malloc_call)(size_t)         = std::malloc;
    void* (*volatile calloc_call)(size_t, size_t) = std::calloc;
    void* (*volatile realloc_call)(void*, size_t) = std::realloc;
    allocations                                   = 0U;
    counting                                      = true;
    p                                             = malloc_call(32U);
    saw_c                                         = saw_c && allocations != 0U;
    allocations                                   = 0U;
    p                                             = realloc_call(p, 64U);
    saw_c                                         = saw_c && allocations != 0U;
    std::free(p);
    allocations = 0U;
    p           = calloc_call(2U, 32U);
    saw_c       = saw_c && allocations != 0U;
    std::free(p);
    allocations = 0U;
#    if defined(_MSC_VER)
    void* (*volatile aligned_call)(size_t, size_t) = _aligned_malloc;
    p                                              = aligned_call(64U, 64U);
    saw_c                                          = saw_c && allocations != 0U;
    _aligned_free(p);
#    else
    void* (*volatile aligned_call)(size_t, size_t) = std::aligned_alloc;
    p                                              = aligned_call(64U, 64U);
    saw_c                                          = saw_c && allocations != 0U;
    std::free(p);
    allocations                                        = 0U;
    int (*volatile posix_call)(void**, size_t, size_t) = posix_memalign;
    const int aligned_status = posix_call(&p, 64U, 64U);
    saw_c = saw_c && allocations != 0U && aligned_status == 0;
    std::free(p);
#    endif
    counting = false;
#endif
    return saw_new && saw_aligned_new && saw_c;
}

openmeta::MetadataAuthoringEntry
entry(const openmeta::MetaKeyView& key, const openmeta::MetaValueView& value)
{
    openmeta::MetadataAuthoringEntry result;
    result.key   = key;
    result.value = value;
    return result;
}

struct ReplayCount {
    uint32_t calls = 0U;
    size_t bytes   = 0U;
};
bool
replay(void* user, openmeta::MetadataPatchPayload,
       std::span<const std::byte> payload) noexcept
{
    ReplayCount* count = static_cast<ReplayCount*>(user);
    ++count->calls;
    count->bytes += payload.size();
    return true;
}
bool
reject_replay(void*, openmeta::MetadataPatchPayload,
              std::span<const std::byte>) noexcept
{
    return false;
}
}  // namespace

int
main()
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(crt_allocation_hook);
#endif
    if (!probe_allocators()) {
        std::fprintf(stderr, "allocation probe failed\n");
        return 1;
    }
    using namespace openmeta;
    const std::string padding(48U * 1024U, 's');
    constexpr std::string_view ns = "https://example.test/patch/1.0/";
    const std::array entries      = {
        entry(make_exif_tag_key_view("exififd", 0x9211U),
                   make_value_view_u32(1U)),
        entry(make_exif_tag_key_view("exififd", 0x9291U),
                   make_value_view_text("000000001", TextEncoding::Ascii)),
        entry(make_xmp_property_key_view(ns, "Frame"),
                   make_value_view_text("0000000001", TextEncoding::Utf8)),
        entry(make_xmp_property_key_view(ns, "Clock"),
                   make_value_view_text("00000000000000000001", TextEncoding::Utf8)),
        entry(make_xmp_property_key_view(ns, "Calibration"),
                   make_value_view_text(padding, TextEncoding::Utf8)),
    };
    MetaStore store;
    if (!create_metadata_store(entries, &store, {}).ok())
        return 2;
    std::array<MetadataPatchRequest, 4> requests;
    requests[0].key                = entries[0].key;
    requests[0].expected.elem_type = MetaElementType::U32;
    requests[1].key                = entries[1].key;
    requests[1].expected           = { MetaValueKind::Text, MetaElementType::U8,
                                       TextEncoding::Ascii, 9U };
    requests[2].key                = entries[2].key;
    requests[2].escaped_width      = 10U;
    requests[3].key                = entries[3].key;
    requests[3].escaped_width      = 20U;
    std::array<MetadataPatchHandle, 4> handles;
    PreparedMetadataPatchPlan plan;
    if (!prepare_metadata_patch_plan(store, requests, { .plan_id = 1U },
                                     handles, &plan)
             .ok())
        return 3;
    std::array<PreparedMetadataPatchInstance, 64> workers;
    for (PreparedMetadataPatchInstance& worker : workers)
        if (!create_prepared_metadata_patch_instance(plan, &worker).ok())
            return 4;
    plan.reset();
    const std::array<MetadataPatchUpdate, 4> good = { {
        { handles[0], make_value_view_u32(UINT32_MAX) },
        { handles[1], make_value_view_text("123456789", TextEncoding::Ascii) },
        { handles[2], make_value_view_text("4294967295", TextEncoding::Utf8) },
        { handles[3],
          make_value_view_text("00000000000000000042", TextEncoding::Utf8) },
    } };
    std::array bad                                = good;
    bad.back().value = make_value_view_text("short", TextEncoding::Utf8);
    allocations      = 0U;
    counting         = true;
    bool ok          = true;
    for (uint32_t i = 0; i < 10000U; ++i) {
        PreparedMetadataPatchInstance& worker = workers[i % workers.size()];
        const std::span<const std::byte> exif = worker.payload(
            MetadataPatchPayload::ExifTiff);
        const std::span<const std::byte> xmp = worker.payload(
            MetadataPatchPayload::Xmp);
        ok = ok && patch_prepared_metadata_instance(&worker, good).ok();
        ok = ok
             && patch_prepared_metadata_instance(&worker, bad).code
                    == MetadataPatchCode::WidthMismatch;
        ReplayCount count;
        ok = ok
             && replay_prepared_metadata_instance(worker, replay, &count).ok();
        ok = ok && count.calls == 2U && count.bytes == exif.size() + xmp.size();
        ok = ok
             && replay_prepared_metadata_instance(worker, reject_replay,
                                                  nullptr)
                        .code
                    == MetadataPatchCode::ReplayFailed;
        ok = ok
             && worker.payload(MetadataPatchPayload::ExifTiff).data()
                    == exif.data();
        ok = ok
             && worker.payload(MetadataPatchPayload::Xmp).data() == xmp.data();
    }
    counting = false;
    if (!ok || allocations != 0U) {
        std::fprintf(stderr, "patch/replay result=%d allocations=%llu\n", ok,
                     static_cast<unsigned long long>(allocations));
        return 5;
    }
#if defined(OPENMETA_WRAP_HEAP)
    std::puts(
        "zero allocations: C++ new plus malloc/calloc/realloc/aligned_alloc/posix_memalign wrappers");
#elif defined(_MSC_VER) && defined(_DEBUG)
    std::puts(
        "zero allocations: C++ new plus static debug CRT allocation hook");
#else
    std::puts(
        "zero allocations: C++ new overrides; C heap interception unavailable in this configuration");
#endif
    std::puts(
        "64 owning instances; 10000 successful and 10000 rejected batches; successful and rejected replay");
    return 0;
}
