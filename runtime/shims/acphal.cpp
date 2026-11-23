// acphal.dll: Audio Control Processor HAL. Scalable Hardware Audio Processing Engine
// (SHAPE) and APU (Audio Processing Unit) physical memory management for Xbox Series.
//
// On a PC there is no SHAPE/ACP hardware and APU physical memory does not exist.
// This shim creates the APU heap from process memory and answers queries from it.
// The SHAPE context arrays are allocated on the process heap; the ACP interface
// accepts and discards commands.
//
// Signatures from the public GDK reference (devdocs.xbox.com):
//   AcpHal, APU reference, SHAPE_CONTEXTS, APU_ADDRESS.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "shims.h"
#include "startup_trace.h"

namespace {

// ---- APU_ADDRESS (from the GDK) ----

// The console APU uses a 40-bit physical address space.
// On a PC we translate to a host pointer.
struct APU_ADDRESS {
    uint64_t value;  // 40-bit physical address
};

// ---- APU heap state (from the GDK) ----

struct ApuHeapState {
    uint32_t version;
    uint32_t flags;
    uint64_t cachedUsed;
    uint64_t cachedTotal;
    uint64_t nonCachedUsed;
    uint64_t nonCachedTotal;
};

// ---- ACP Command structure (from the GDK, simplified) ----

// We only need to accept and drop them.

// ---- APU heap state ----

std::mutex g_apu_mutex;
bool g_apu_heap_created = false;
uint64_t g_apu_cached_size = 0;
void* g_apu_cached_base = nullptr;
uint64_t g_apu_noncached_size = 0;
void* g_apu_noncached_base = nullptr;

// ---- SHAPE context structures (from the GDK) ----

// These are opaque context types. We allocate memory for them and track the
// pointers so the title can use them.
struct SHAPE_SRC_CONTEXT {};
struct SHAPE_EQCOMP_CONTEXT {};
struct SHAPE_FILTVOL_CONTEXT {};
struct SHAPE_DMA_CONTEXT {};
struct SHAPE_XMA_CONTEXT {};
struct SHAPE_PCM_CONTEXT {};

// The SHAPE_CONTEXTS structure (from the GDK)
struct SHAPE_CONTEXTS {
    uint32_t numSrcContexts;
    uint32_t numEqCompContexts;
    uint32_t numFiltVolContexts;
    uint32_t numDmaContexts;
    uint32_t numXmaContexts;
    uint32_t numPcmContexts;
    SHAPE_SRC_CONTEXT* srcContextArray;
    SHAPE_EQCOMP_CONTEXT* eqCompContextArray;
    SHAPE_FILTVOL_CONTEXT* filtVolContextArray;
    SHAPE_DMA_CONTEXT* dmaContextArray;
    SHAPE_XMA_CONTEXT* xmaContextArray;
    SHAPE_PCM_CONTEXT* pcmContextArray;
    APU_ADDRESS apuSrcContextArray;
    APU_ADDRESS apuEqCompContextArray;
    APU_ADDRESS apuFiltVolContextArray;
    APU_ADDRESS apuDmaContextArray;
    APU_ADDRESS apuXmaContextArray;
    APU_ADDRESS apuPcmContextArray;
};

static_assert(sizeof(SHAPE_CONTEXTS) == 9 * 8 + 6 * 8, "SHAPE_CONTEXTS size check");

// ---- Startup trace helper ----

void trace_call(const char* api, const std::string& detail) {
    if (!rt::startup_trace::enabled()) return;
    static std::mutex mutex;
    static std::set<std::string> seen;
    {
        std::lock_guard lock(mutex);
        if (!seen.insert(std::string(api) + detail).second) return;
    }
    rt::startup_trace::event("acphal",
                             std::string("\"api\":\"") + api + "\"" + detail);
}

// ---- ACP implementation methods ----

// ---- AcpHalAllocateShapeContexts ----
//
// HRESULT AcpHalAllocateShapeContexts(SHAPE_CONTEXTS* contextArrays)

HRESULT __stdcall AcpHalAllocateShapeContexts(SHAPE_CONTEXTS* ctx) {
    if (!ctx) return E_POINTER;

    // Validate: SRC count must equal PCM + XMA (GDK requirement)
    if (ctx->numSrcContexts != ctx->numPcmContexts + ctx->numXmaContexts)
        return E_INVALIDARG;

    // Allocate arrays on the host heap
    auto alloc = [](uint32_t count, size_t elemSize) -> void* {
        if (count == 0) return nullptr;
        return std::calloc(count, elemSize);
    };

    ctx->srcContextArray = static_cast<SHAPE_SRC_CONTEXT*>(
        alloc(ctx->numSrcContexts, sizeof(SHAPE_SRC_CONTEXT)));
    ctx->eqCompContextArray = static_cast<SHAPE_EQCOMP_CONTEXT*>(
        alloc(ctx->numEqCompContexts, sizeof(SHAPE_EQCOMP_CONTEXT)));
    ctx->filtVolContextArray = static_cast<SHAPE_FILTVOL_CONTEXT*>(
        alloc(ctx->numFiltVolContexts, sizeof(SHAPE_FILTVOL_CONTEXT)));
    ctx->dmaContextArray = static_cast<SHAPE_DMA_CONTEXT*>(
        alloc(ctx->numDmaContexts, sizeof(SHAPE_DMA_CONTEXT)));
    ctx->xmaContextArray = static_cast<SHAPE_XMA_CONTEXT*>(
        alloc(ctx->numXmaContexts, sizeof(SHAPE_XMA_CONTEXT)));
    ctx->pcmContextArray = static_cast<SHAPE_PCM_CONTEXT*>(
        alloc(ctx->numPcmContexts, sizeof(SHAPE_PCM_CONTEXT)));

    // APU physical addresses: not meaningful on PC, zero them
    ctx->apuSrcContextArray = {};
    ctx->apuEqCompContextArray = {};
    ctx->apuFiltVolContextArray = {};
    ctx->apuDmaContextArray = {};
    ctx->apuXmaContextArray = {};
    ctx->apuPcmContextArray = {};

    trace_call("AcpHalAllocateShapeContexts",
               ",\"src\":" + std::to_string(ctx->numSrcContexts) +
               ",\"pcm\":" + std::to_string(ctx->numPcmContexts) +
               ",\"xma\":" + std::to_string(ctx->numXmaContexts));
    return S_OK;
}

// ---- AcpHalReleaseShapeContexts ----
//
// HRESULT AcpHalReleaseShapeContexts(SHAPE_CONTEXTS* contextArrays)

HRESULT __stdcall AcpHalReleaseShapeContexts(SHAPE_CONTEXTS* ctx) {
    if (!ctx) return E_POINTER;

    auto free_array = [](void* ptr) {
        if (ptr) std::free(ptr);
    };
    free_array(ctx->srcContextArray);
    free_array(ctx->eqCompContextArray);
    free_array(ctx->filtVolContextArray);
    free_array(ctx->dmaContextArray);
    free_array(ctx->xmaContextArray);
    free_array(ctx->pcmContextArray);

    std::memset(ctx, 0, sizeof(*ctx));

    trace_call("AcpHalReleaseShapeContexts", "");
    return S_OK;
}

// ---- AcpHalCreate ----
//
// HRESULT AcpHalCreate(IAcpHal** acp)

HRESULT __stdcall AcpHalCreate(void** acp) {
    if (!acp) return E_POINTER;
    *acp = nullptr;

    static std::atomic<uint32_t> next_id{1};
    uint32_t id = next_id++;

    // Return an opaque handle pointer. The title passes it to IAcpHal methods,
    // which we implement as individual exported functions. On the console the
    // ACP interface is COM-like; the exports handle the dispatch internally.
    // Since we don't implement any methods beyond create/release, the handle
    // is just a unique id cast to a pointer.
    void* handle = reinterpret_cast<void*>(static_cast<uintptr_t>(id) << 4);

    *acp = handle;

    trace_call("AcpHalCreate", "");
    return S_OK;
}

// ---- ApuCreateHeap ----
//
// HRESULT ApuCreateHeap(UINT32 cachedSizeInBytes, UINT32 nonCachedSizeInBytes)

HRESULT __stdcall ApuCreateHeap(uint32_t cachedSizeInBytes,
                                 uint32_t nonCachedSizeInBytes) {
    std::lock_guard lock(g_apu_mutex);
    if (g_apu_heap_created)
        return 0x8004BA01;  // APU_E_HEAP_ALREADY_ALLOCATED

    if (cachedSizeInBytes == 0)
        return E_INVALIDARG;

    // Allocate the APU heap from VirtualAlloc
    g_apu_cached_size = cachedSizeInBytes;
    g_apu_cached_base = VirtualAlloc(nullptr, cachedSizeInBytes,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_apu_cached_base) return E_OUTOFMEMORY;

    if (nonCachedSizeInBytes > 0) {
        g_apu_noncached_size = nonCachedSizeInBytes;
        g_apu_noncached_base = VirtualAlloc(nullptr, nonCachedSizeInBytes,
                                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!g_apu_noncached_base) {
            VirtualFree(g_apu_cached_base, 0, MEM_RELEASE);
            g_apu_cached_base = nullptr;
            g_apu_cached_size = 0;
            return E_OUTOFMEMORY;
        }
    }

    g_apu_heap_created = true;
    trace_call("ApuCreateHeap",
               ",\"cachedSize\":" + std::to_string(cachedSizeInBytes));
    return S_OK;
}

// ---- ApuAlloc ----
//
// HRESULT ApuAlloc(void** virtualAddress, APU_ADDRESS* physicalAddress,
//                   UINT32 sizeInBytes, UINT32 alignmentInBytes, UINT32 flags)

// APU allocation flags
constexpr uint32_t APU_ALLOC_CACHED = 0;
constexpr uint32_t APU_ALLOC_NONCACHED = 1;

HRESULT __stdcall ApuAlloc(void** virtualAddress, APU_ADDRESS* physicalAddress,
                            uint32_t sizeInBytes, uint32_t alignmentInBytes,
                            uint32_t flags) {
    if (!virtualAddress) return E_POINTER;
    *virtualAddress = nullptr;
    if (physicalAddress) *physicalAddress = {};

    if (sizeInBytes == 0 || alignmentInBytes == 0 ||
        (alignmentInBytes & 3) != 0 ||
        (alignmentInBytes & (alignmentInBytes - 1)) != 0 ||
        (flags != APU_ALLOC_CACHED && flags != APU_ALLOC_NONCACHED))
        return E_INVALIDARG;

    std::lock_guard lock(g_apu_mutex);

    bool cached = (flags == APU_ALLOC_CACHED);
    void* heap_base = cached ? g_apu_cached_base : g_apu_noncached_base;
    uint64_t heap_size = cached ? g_apu_cached_size : g_apu_noncached_size;

    if (!heap_base) {
        // Create a default heap
        uint32_t default_cached = 64 * 1024 * 1024;
        uint32_t default_noncached = 1024 * 1024;
        cached = true;
        heap_base = VirtualAlloc(nullptr, default_cached,
                                 MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!heap_base) return E_OUTOFMEMORY;
        g_apu_cached_base = heap_base;
        g_apu_cached_size = default_cached;
        g_apu_noncached_base = VirtualAlloc(nullptr, default_noncached,
                                             MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (g_apu_noncached_base) {
            g_apu_noncached_size = default_noncached;
        }
        g_apu_heap_created = true;
        heap_size = default_cached;
    }

    // Best-effort small bump allocator from the cached heap
    // This is a simplified version; the real console uses a proper heap.
    static uint64_t bump_offset = 0;
    uint64_t aligned = (bump_offset + alignmentInBytes - 1) & ~(static_cast<uint64_t>(alignmentInBytes) - 1);
    if (aligned + sizeInBytes > heap_size) return E_OUTOFMEMORY;
    bump_offset = aligned + sizeInBytes;

    *virtualAddress = static_cast<uint8_t*>(heap_base) + aligned;
    if (physicalAddress) {
        physicalAddress->value = reinterpret_cast<uint64_t>(*virtualAddress);
    }

    return S_OK;
}

// ---- ApuFree ----
//
// void ApuFree(void* virtualAddress)

void __stdcall ApuFree(void* virtualAddress) {
    if (!virtualAddress) return;
    // The bump allocator doesn't support free. No-op. The real APU heap
    // lives for the process lifetime anyway.
    trace_call("ApuFree", "");
}

// ---- ApuIsVirtualAddressValid ----
//
// BOOL ApuIsVirtualAddressValid(void* virtualAddress)

BOOL __stdcall ApuIsVirtualAddressValid(void* virtualAddress) {
    if (!virtualAddress) return FALSE;
    std::lock_guard lock(g_apu_mutex);
    // Check if the address falls within either APU heap
    uintptr_t addr = reinterpret_cast<uintptr_t>(virtualAddress);
    uintptr_t cached_start = reinterpret_cast<uintptr_t>(g_apu_cached_base);
    uintptr_t noncached_start = reinterpret_cast<uintptr_t>(g_apu_noncached_base);

    if (cached_start && addr >= cached_start &&
        addr < cached_start + g_apu_cached_size) return TRUE;
    if (noncached_start && addr >= noncached_start &&
        addr < noncached_start + g_apu_noncached_size) return TRUE;
    return FALSE;
}

// ---- ApuMapApuAddress ----
//
// void* ApuMapApuAddress(APU_ADDRESS address)

void* __stdcall ApuMapApuAddress(APU_ADDRESS address) {
    if (address.value == 0) return nullptr;
    // On the console this translates a physical APU address to a virtual one.
    // On PC the APU_ADDRESS was set to the virtual address itself, so return it.
    return reinterpret_cast<void*>(static_cast<uintptr_t>(address.value));
}

// ---- ApuMapVirtualAddress ----
//
// APU_ADDRESS ApuMapVirtualAddress(void* virtualAddress)

APU_ADDRESS __stdcall ApuMapVirtualAddress(void* virtualAddress) {
    APU_ADDRESS result{};
    if (!virtualAddress) return result;
    result.value = reinterpret_cast<uint64_t>(virtualAddress);
    return result;
}

}  // namespace

SHIM_TABLE(acphal,
    {"acphal.dll", "AcpHalAllocateShapeContexts", reinterpret_cast<void*>(&AcpHalAllocateShapeContexts)},
    {"acphal.dll", "AcpHalCreate", reinterpret_cast<void*>(&AcpHalCreate)},
    {"acphal.dll", "AcpHalReleaseShapeContexts", reinterpret_cast<void*>(&AcpHalReleaseShapeContexts)},
    {"acphal.dll", "ApuAlloc", reinterpret_cast<void*>(&ApuAlloc)},
    {"acphal.dll", "ApuCreateHeap", reinterpret_cast<void*>(&ApuCreateHeap)},
    {"acphal.dll", "ApuFree", reinterpret_cast<void*>(&ApuFree)},
    {"acphal.dll", "ApuIsVirtualAddressValid", reinterpret_cast<void*>(&ApuIsVirtualAddressValid)},
    {"acphal.dll", "ApuMapApuAddress", reinterpret_cast<void*>(&ApuMapApuAddress)},
    {"acphal.dll", "ApuMapVirtualAddress", reinterpret_cast<void*>(&ApuMapVirtualAddress)},
);