// xapu.dll: Audio Processing Unit (XAPU). Hardware-accelerated Opus decoding and
// sample-rate conversion on Xbox Series.
//
// On a PC there is no such hardware. This shim accepts connections and commands,
// and reports results as if they completed immediately with silence/identity.
//
// Signatures from the public GDK reference (devdocs.xbox.com):
//   XAPU function reference, structures XApuConnectInputParameters,
//   XApuConnectOutputParameters, XApuCommandId, XApuResult.
//
// ASSUMPTION: XApuHandle is an opaque pointer.
// ASSUMPTION: XApuResult::outputData/outputDataLength are set to zeros and zero.
// ASSUMPTION: XApuConnectOutputParameters::baseData is a block of VirtualAlloc'd memory,
//   since the title reads and writes this buffer directly.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

#include "shims.h"
#include "startup_trace.h"

namespace {

// ---- Error codes from the GDK reference ----

constexpr HRESULT XAPU_E_PENDING_RESULTS = 0x8004E801;
constexpr HRESULT XAPU_E_QUEUE_FULL = 0x8004E802;
constexpr HRESULT XAPU_E_DEVICE_FATAL_ERROR = 0x8004E803;
constexpr HRESULT XAPU_E_CLIENT_NOT_INITIALIZED = 0x8004E804;
constexpr HRESULT XAPU_E_STREAM_IN_USE = 0x8004E805;
constexpr HRESULT XAPU_E_INVALID_STATE = 0x8004E806;

// ---- Known process type values (from the GDK) ----

// Not all values are known publicly; these are the ones the reference names.
enum XApuProcessType : uint32_t {
    XApuProcessType_DecodeOpus = 0,
    XApuProcessType_DecodeConvertOpus = 1,
    XApuProcessType_ConvertPCM = 2,
};

// ---- Known command types (from the GDK) ----

enum XApuCommandType : uint32_t {
    XApuCommandType_Activate = 0,
    XApuCommandType_Deactivate = 1,
    XApuCommandType_Process = 2,
    XApuCommandType_Notify = 3,
    XApuCommandType_Reset = 4,
};

// ---- Opaque handle ----

struct XApuClient {
    uint32_t baseDataLength;
    void* baseData;
    HANDLE resultReadyEvent;
    bool hasEvent;
};

// ---- Startup trace helper ----

void trace_call(const char* api, const std::string& detail) {
    if (!rt::startup_trace::enabled()) return;
    static std::mutex mutex;
    static std::set<std::string> seen;
    {
        std::lock_guard lock(mutex);
        if (!seen.insert(std::string(api) + detail).second) return;
    }
    rt::startup_trace::event("xapu",
                             std::string("\"api\":\"") + api + "\"" + detail);
}

// ---- XApuConnect ----
//
// HRESULT XApuConnect(XApuConnectInputParameters* params,
//                      XApuConnectOutputParameters* data,
//                      XApuHandle* handle)

// Input parameters (from the GDK reference)
struct XApuConnectInputParameters {
    XApuProcessType processType;
    uint32_t maxStreamCount;
    uint32_t maxQueuedCommandsPerStream;
    uint32_t baseDataLength;
    uint32_t maxFrameCountPerOutput;
    uint32_t options;  // XApuConnectOptions
};

// Output parameters (from the GDK reference)
struct XApuConnectOutputParameters {
    void* baseData;
    HANDLE resultReadyEvent;
};

// Options
constexpr uint32_t kXApuConnectOptionEnableResultReadyEvent = 1;

HRESULT __stdcall XApuConnect(XApuConnectInputParameters* params,
                               XApuConnectOutputParameters* data,
                               void** handle) {
    if (!params || !data || !handle) return E_POINTER;
    *handle = nullptr;
    data->baseData = nullptr;
    data->resultReadyEvent = nullptr;

    if (params->baseDataLength == 0 || params->maxStreamCount == 0 ||
        params->maxQueuedCommandsPerStream == 0)
        return E_INVALIDARG;

    // Allocate the base data buffer that the title uses directly.
    void* baseData = VirtualAlloc(nullptr, params->baseDataLength,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!baseData) return E_OUTOFMEMORY;
    std::memset(baseData, 0, params->baseDataLength);

    HANDLE eventHandle = nullptr;
    if (params->options & kXApuConnectOptionEnableResultReadyEvent) {
        eventHandle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!eventHandle) {
            VirtualFree(baseData, 0, MEM_RELEASE);
            return E_OUTOFMEMORY;
        }
    }

    auto* client = new (std::nothrow) XApuClient{};
    if (!client) {
        if (eventHandle) CloseHandle(eventHandle);
        VirtualFree(baseData, 0, MEM_RELEASE);
        return E_OUTOFMEMORY;
    }
    client->baseDataLength = params->baseDataLength;
    client->baseData = baseData;
    client->resultReadyEvent = eventHandle;
    client->hasEvent = (eventHandle != nullptr);

    data->baseData = baseData;
    data->resultReadyEvent = eventHandle;
    *handle = client;

    trace_call("XApuConnect",
               ",\"processType\":" + std::to_string(static_cast<uint32_t>(params->processType)) +
               ",\"baseDataLength\":" + std::to_string(params->baseDataLength));
    return S_OK;
}

// ---- XApuDequeueResult ----
//
// HRESULT XApuDequeueResult(XApuHandle handle, XApuResult* commandResult)
//
// Without hardware there are no results to dequeue. Return PENDING_RESULTS so the
// title retries (and will never get a result). A title that polls in a loop would
// spin forever, but a title that checks between commands and times out will proceed.
// If the title signals completion before dequeue, we pretend there's one result.

HRESULT __stdcall XApuDequeueResult(void* handle, void* commandResult) {
    if (!handle || !commandResult) return E_POINTER;
    return XAPU_E_PENDING_RESULTS;
}

// ---- XApuDisconnect ----
//
// HRESULT XApuDisconnect(XApuHandle handle)

HRESULT __stdcall XApuDisconnect(void* handle) {
    if (!handle) return E_INVALIDARG;
    auto* client = static_cast<XApuClient*>(handle);
    if (client->resultReadyEvent) CloseHandle(client->resultReadyEvent);
    VirtualFree(client->baseData, 0, MEM_RELEASE);
    delete client;
    trace_call("XApuDisconnect", "");
    return S_OK;
}

// ---- XApuEnqueueCommand ----
//
// HRESULT XApuEnqueueCommand(XApuHandle handle, XApuCommandId* command,
//                             uint32_t* totalCommandCount)

struct XApuCommandIdRaw {
    XApuCommandType type;
    uint32_t streamIndex;
    uint32_t sequence;
    uint32_t options;  // XApuCommandOptions
    void* context;
};

HRESULT __stdcall XApuEnqueueCommand(void* handle, XApuCommandIdRaw* command,
                                      uint32_t* totalCommandCount) {
    if (!handle || !command) return E_INVALIDARG;
    static std::atomic<uint32_t> globalCommandCount{0};
    uint32_t count = ++globalCommandCount;
    if (totalCommandCount) *totalCommandCount = count;
    trace_call("XApuEnqueueCommand",
               ",\"type\":" + std::to_string(static_cast<uint32_t>(command->type)));
    return S_OK;
}

// ---- XApuGetResultCount ----
//
// uint32_t XApuGetResultCount(XApuHandle handle, uint32_t* totalResultCount)

uint32_t __stdcall XApuGetResultCount(void* handle, uint32_t* totalResultCount) {
    if (!handle) return 0;
    if (totalResultCount) *totalResultCount = 0;
    return 0;  // no results ready
}

}  // namespace

SHIM_TABLE(xapu,
    {"xapu.dll", "XApuConnect", reinterpret_cast<void*>(&XApuConnect)},
    {"xapu.dll", "XApuDequeueResult", reinterpret_cast<void*>(&XApuDequeueResult)},
    {"xapu.dll", "XApuDisconnect", reinterpret_cast<void*>(&XApuDisconnect)},
    {"xapu.dll", "XApuEnqueueCommand", reinterpret_cast<void*>(&XApuEnqueueCommand)},
    {"xapu.dll", "XApuGetResultCount", reinterpret_cast<void*>(&XApuGetResultCount)},
);
