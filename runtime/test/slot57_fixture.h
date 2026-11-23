// Hand-built inputs for console device slot 57 (docs/fspx.md "Slot 57", docs/shader_library.md sections 2 and 7): a
// pipeline blob, a vertex-layout state blob and the description that carries them. Every word is written here from
// the layouts src/slot57_pipeline_state.h and src/slot57_input_layout.h name. No game data.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace slot57test {

using Bytes = std::vector<std::uint8_t>;

inline void put(Bytes& bytes, std::size_t at, std::uint32_t word) { std::memcpy(bytes.data() + at, &word, 4); }
inline void put(Bytes& bytes, std::size_t at, float value) { std::memcpy(bytes.data() + at, &value, 4); }
inline void append(Bytes& bytes, std::uint32_t word) {
    for (int i = 0; i < 4; ++i) bytes.push_back(std::uint8_t(word >> (8 * i)));
}

struct Element {
    std::string_view semantic;
    std::uint32_t semantic_index, format, slot, offset, slot_class, step_rate;
};

// A pipeline blob: the 80-byte head (render-target formats, depth format, topology type, one sample) and the input
// elements, padded to whole words.
inline Bytes pipeline_blob(std::initializer_list<std::uint8_t> formats, std::uint8_t dsv, std::uint8_t topology = 3,
                           std::initializer_list<Element> elements = {}) {
    Bytes out(80, 0);
    put(out, 0x00, 0x12345678u);  // a per-blob word the readers ignore
    put(out, 0x04, 0x1000u);
    put(out, 0x0c, elements.size() ? 1u : 0u);
    put(out, 0x1c, 0xffffffffu);
    std::size_t i = 0;
    for (const std::uint8_t format : formats) out[0x10 + i++] = format;
    out[0x24] = dsv;
    out[0x25] = std::uint8_t(formats.size());
    out[0x26] = topology;
    put(out, 0x28, 1u);
    put(out, 0x3c, 0xcu);
    put(out, 0x4c, std::uint32_t(elements.size()));
    for (const Element& e : elements) {
        append(out, std::uint32_t(e.semantic.size() + 1));
        out.insert(out.end(), e.semantic.begin(), e.semantic.end());
        out.push_back(0);
        for (const std::uint32_t v : {e.semantic_index, e.format, e.slot, e.offset, e.slot_class, e.step_rate})
            append(out, v);
    }
    put(out, 0x38, std::uint32_t(out.size() - 76));  // the element list's bytes, its count word included
    out.resize((out.size() + 3) & ~std::size_t(3), 0);
    return out;
}

// A 388-byte vertex-layout state blob with everything off: no culling, depth clip on, depth and stencil off (all
// functions ALWAYS), blending off on every target and all channels written.
inline Bytes state_blob() {
    Bytes out(388, 0);
    put(out, 0x004, 4u);
    put(out, 0x09c, 0x80000u);
    put(out, 0x0a0, 0x201a40u);
    put(out, 0x0b8, 0x700770u | 0x80u);
    put(out, 0x0c0, 0x0100ff00u);
    put(out, 0x0c4, 0x0100ff00u);
    put(out, 0x0cc, 0xffffffffu);
    for (std::size_t i = 0; i < 8; ++i) put(out, 0x0d0 + 4 * i, 0x80000000u);
    put(out, 0x120, 0x100000u);
    return out;
}

// Offsets of the state words the tests change, and values for them.
inline constexpr std::size_t kClip = 0x09c, kRasterMode = 0x0a0, kOffsetClamp = 0x0ac, kOffsetScale = 0x0b0,
                             kOffset = 0x0b4, kDepth = 0x0b8, kStencilOps = 0x0bc, kStencilMasks = 0x0c0,
                             kStencilMasksBack = 0x0c4, kTargetMask = 0x0cc, kBlend0 = 0x0d0, kUnread120 = 0x120;
inline constexpr std::uint32_t kCullFront = 1, kCullBack = 2, kFrontClockwise = 4;

// The description device slot 57 takes: the root signature, then six (pointer, size) pairs.
struct Desc {
    void* root_signature = nullptr;
    struct Pair {
        const void* data = nullptr;
        std::uint64_t size = 0;
    } pairs[6];
};
static_assert(sizeof(Desc) == 0x68);

inline Desc desc(void* root, const Bytes& state, const Bytes& pipeline, const Bytes& ps, const Bytes& vs) {
    Desc out;
    out.root_signature = root;
    const Bytes* const parts[6] = {&state, &pipeline, &ps, nullptr, &vs, nullptr};
    for (int i = 0; i < 6; ++i)
        if (parts[i] && !parts[i]->empty()) out.pairs[i] = {parts[i]->data(), parts[i]->size()};
    return out;
}

}  // namespace slot57test
