// xg_xs.dll: Xbox Series texture-swizzling and texture-computer APIs.
//
// XGCreateTextureComputer provides a "texture computer" that reformats texture data
// between tiled and linear layouts. XGSuggestSwizzleMode suggests the optimal swizzle
// mode for a texture description. Both are bounded by the console's GPU tiling hardware.
//
// On a PC there is no such hardware and no corresponding API. The shim returns
// E_NOTIMPL for the texture computer (the title must fall back to a CPU path or
// skip this optimization) and suggests the simplest swizzle mode for the swizzle query.
//
// Signatures from the public GDK reference (docs.microsoft.com/xbox/gdk):
//   XGCreateTextureComputer / XGSuggestSwizzleMode are documented in the Xbox GDK.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "shims.h"
#include "startup_trace.h"

namespace {

// ---- XGSuggestSwizzleMode ----
//
// HRESULT XGSuggestSwizzleMode(const XG_TEXTURE_DESC* desc,
//                               XG_SWIZZLE_MODE* mode)
//
// Suggests the optimal swizzle mode for a given texture description.
// Without Xbox tiling hardware, we suggest the linear mode.
//
// XG_SWIZZLE_MODE values (from the GDK header xg.h / xg_xs.h):
//   This is a partially-documented enum. Common values include:
//     0 = XG_SWIZZLE_STRIP   (strip/linear, available on all series)
//   Higher values select hardware-specific tiling modes.

HRESULT __stdcall XGSuggestSwizzleMode(const void* desc, uint32_t* mode) {
    if (!desc || !mode) return E_POINTER;
    *mode = 0;  // XG_SWIZZLE_STRIP (linear, no tiling)
    S_OK;
    // Log the call once for diagnostics
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr,
                     "[rt] xg_xs: XGSuggestSwizzleMode returning linear mode 0; "
                     "the title's texture tiling is not applied on the PC path (logged once)\n");
    }
    if (rt::startup_trace::enabled()) {
        static std::mutex m;
        static bool first = true;
        std::lock_guard l(m);
        if (first) {
            first = false;
            rt::startup_trace::event("xg_xs",
                                     "\"api\":\"XGSuggestSwizzleMode\",\"mode\":0");
        }
    }
    return S_OK;
}

// ---- XGCreateTextureComputer ----
//
// HRESULT XGCreateTextureComputer(XG_TEXTURE_COMPUTER_DESC* desc,
//                                  ID3D12Device* device,
//                                  REFIID riid,
//                                  void** textureComputer)
//
// Creates a texture computer that can reformat texture data (tile/detile).
// On a PC without the console's display hardware, this is not supported.
// We return E_NOTIMPL so the title falls back to its CPU path.

HRESULT __stdcall XGCreateTextureComputer(const void* /*desc*/,
                                           void* /*device*/,
                                           const IID& /*riid*/,
                                           void** textureComputer) {
    if (!textureComputer) return E_POINTER;
    *textureComputer = nullptr;

    static std::atomic<bool> warned{false};
    if (!warned.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr,
                     "[rt] xg_xs: XGCreateTextureComputer not supported on PC; "
                     "the title should fall back to its CPU reformatting path (logged once)\n");
    }
    return E_NOTIMPL;
}

}  // namespace

SHIM_TABLE(xg_xs,
    {"xg_xs.dll", "XGCreateTextureComputer", reinterpret_cast<void*>(&XGCreateTextureComputer)},
    {"xg_xs.dll", "XGSuggestSwizzleMode", reinterpret_cast<void*>(&XGSuggestSwizzleMode)},
);