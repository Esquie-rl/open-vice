#pragma once

// The fixed state of a graphics pipeline that console device slot 57 creates (docs/shader_library.md sections 2 and 7,
// docs/fspx.md "Slot 57"): the description's first two (pointer, size) pairs are the pipeline library's state blob
// and "pipeline" blob. This header reads both into D3D12-level values. It needs no Windows or Vulkan header: enum
// values are the D3D12 numbers, kept as integers.
//
// What is read, and how it was established (counts over the 25,471 graphics records of the 1.0.2.1 library, a
// reading of the data; nothing here is measured on a console):
//   pipeline blob (the 80-byte head before the input elements src/slot57_input_layout.h decodes)
//     +0x10  eight bytes, one DXGI_FORMAT per render target, non-zero from target 0 upwards in every record. Each
//            agrees with the state blob's export format and down-convert class for that target in every (record,
//            target) pair that exports: R16G16B16A16_FLOAT with FP16_ABGR, R10G10B10A2_UNORM with 2_10_10_10,
//            R11G11B10_FLOAT with 10_11_11, R9G9B9E5 with 9_9_9_E5, the 8-bit formats with 8_8_8_8, and so on.
//     +0x24  the depth-stencil DXGI_FORMAT, one byte: 0, D32_FLOAT_S8X24_UINT, D32_FLOAT, D16_UNORM or the console's
//            118 (D16_UNORM_S8_UINT). Stencil is enabled only with the two formats that have stencil, bar 12 records.
//     +0x25  the render-target count, equal to the number of non-zero format bytes in every record.
//     +0x26  the primitive topology type: 3 (triangle) in 25,062 records, 2 (line) in one, and 19 in 408 vertex
//            pipelines. 19 is not a D3D12_PRIMITIVE_TOPOLOGY_TYPE: it is the console's rectangle-list PSO type, which
//            the Vulkan device already builds (src/rect_list_geometry.h, docs/d3d12_vulkan.md "Console rectangle and
//            quad lists"), so it is passed on as it is. ASSUMPTION: the library byte uses the same value space as the
//            PSO type the title's PC-form descriptions carry; that 19 here means a rectangle list is not measured.
//     +0x28  four words, (1, 0, 0, 0) in all but 21 distinct blobs. HYPOTHESIS: the sample description; the other
//            form is refused.
//     +0x04, +0x0c, +0x1c: 0x1000, 0 or 1, and 0xffffffff in all (0x0c has two other values in 6 records). Their
//            meaning is not established (0x1c is probably the sample mask); a blob with other values is refused.
//   state blob (GPU context register values, word 1 = 4 for a vertex pipeline; identified in #8507, checked there
//   over all 5,680 distinct blobs)
//     +0x0b8 DB_DEPTH_CONTROL, +0x0bc DB_STENCIL_CONTROL, +0x0c0 DB_STENCILREFMASK: depth and stencil state.
//     +0x0cc CB_TARGET_MASK, +0x0d0.. CB_BLEND0..7_CONTROL: write masks and blend state.
//   and three more words read here by the same method (their RDNA2 field layouts, as Mesa's sid.h names them):
//     +0x0a0 PA_SU_SC_MODE_CNTL: 0x201a40 plus CULL_FRONT (bit 0), CULL_BACK (bit 1) and FACE (bit 2) in every
//            record; mesh pipelines (the same word 0x54 bytes later) cull back in 11,126 of 12,062.
//     +0x09c PA_CL_CLIP_CNTL: 0x80000, or 0xc080000 (ZCLIP_NEAR_DISABLE and ZCLIP_FAR_DISABLE) in 960 records.
//     +0x0ac PA_SU_POLY_OFFSET_CLAMP, +0x0b0 _SCALE, +0x0b4 _OFFSET as floats: all zero in 23,987 records, else a
//            clamp near 0.003, a scale of 16 x a slope of 0.2 to 2.5, and an offset of 0, 1 or 5.
// ASSUMED, not established: FACE = 0 means a counter-clockwise front face (the convention of Mesa's Vulkan driver,
// which shares D3D's y-down window space); alpha-to-coverage is off (its register is not identified); the strip cut
// value is disabled; fill mode is solid (the poly-mode bits are 0 in every record). The word at +0x120 is 0x100000 in
// all but 94 records; the other value is refused as unread state.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace leonida::slot57 {

struct BlendTarget {
    bool enable = false;
    // D3D12_BLEND and D3D12_BLEND_OP values; ONE, ZERO, ADD when blending is off.
    std::uint32_t src = 2, dest = 1, op = 1, src_alpha = 2, dest_alpha = 1, op_alpha = 1;
    std::uint8_t write_mask = 0;  // D3D12_COLOR_WRITE_ENABLE bits
};

struct StencilFace {
    // D3D12_STENCIL_OP and D3D12_COMPARISON_FUNC values.
    std::uint32_t fail = 1, depth_fail = 1, pass = 1, func = 8;
};

struct PipelineState {
    // From the pipeline blob.
    std::uint32_t render_targets = 0;
    std::uint32_t rtv_formats[8] = {};  // DXGI_FORMAT
    std::uint32_t dsv_format = 0;       // DXGI_FORMAT
    std::uint32_t topology_type = 0;    // D3D12_PRIMITIVE_TOPOLOGY_TYPE
    std::uint32_t sample_count = 1;
    std::uint32_t sample_mask = 0xffffffffu;
    // From the state blob.
    BlendTarget blend[8];
    bool depth_enable = false, depth_write = false;
    std::uint32_t depth_func = 8;  // D3D12_COMPARISON_FUNC
    bool stencil_enable = false;
    std::uint8_t stencil_read_mask = 0xff, stencil_write_mask = 0xff;
    StencilFace front, back;
    std::uint32_t cull_mode = 1;  // D3D12_CULL_MODE: 1 none, 2 front, 3 back
    bool front_counter_clockwise = true;
    bool depth_clip = true;
    std::int32_t depth_bias = 0;
    float depth_bias_clamp = 0.0f, slope_scaled_depth_bias = 0.0f;
};

inline constexpr std::size_t kPipelineHeadBytes = 80;
inline constexpr std::size_t kStateBlobMinBytes = 0x124;  // through +0x120, the last word checked
inline constexpr std::uint32_t kVertexLayout = 4, kMeshLayout = 6;
// The console's rectangle-list PSO topology type (leonida::topology_type_rect_list in src/rect_list_geometry.h).
inline constexpr std::uint32_t kTopologyTypeRectList = 19;

namespace detail {

inline std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t v;
    std::memcpy(&v, bytes.data() + at, 4);
    return v;
}

inline float f32(std::span<const std::uint8_t> bytes, std::size_t at) {
    float v;
    std::memcpy(&v, bytes.data() + at, 4);
    return v;
}

// RDNA2 BLEND_* -> D3D12_BLEND, 0 where D3D12 has no such factor (BOTH_SRC_ALPHA, BOTH_INV_SRC_ALPHA).
inline std::uint32_t blend_factor(std::uint32_t v) {
    static constexpr std::uint8_t table[21] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 0, 14, 15, 16, 17, 18, 19, 20, 21};
    return v < 21 ? table[v] : 0;
}

// A D3D12_BLEND colour factor as the alpha factor that equals its alpha channel (SRC_COLOR -> SRC_ALPHA, ...).
inline std::uint32_t alpha_factor(std::uint32_t d3d12_blend) {
    switch (d3d12_blend) {
    case 3: return 5;
    case 4: return 6;
    case 9: return 7;
    case 10: return 8;
    case 16: return 18;
    case 17: return 19;
    default: return d3d12_blend;
    }
}

// RDNA2 COMB_* -> D3D12_BLEND_OP: DST_PLUS_SRC, SRC_MINUS_DST, MIN_DST_SRC, MAX_DST_SRC, DST_MINUS_SRC.
inline std::uint32_t blend_op(std::uint32_t v) {
    static constexpr std::uint8_t table[5] = {1, 2, 4, 5, 3};
    return v < 5 ? table[v] : 0;
}

// RDNA2 STENCIL_* -> D3D12_STENCIL_OP: KEEP, ZERO, (ONES), REPLACE_TEST, (REPLACE_OP), ADD_CLAMP, SUB_CLAMP, INVERT,
// ADD_WRAP, SUB_WRAP; 0 for the ones D3D12 has no operation for.
inline std::uint32_t stencil_op(std::uint32_t v) {
    static constexpr std::uint8_t table[10] = {1, 2, 0, 3, 0, 4, 5, 6, 7, 8};
    return v < 10 ? table[v] : 0;
}

inline bool fail(const char** why, const char* text) {
    if (why) *why = text;
    return false;
}

}  // namespace detail

// The head of the pipeline blob: render-target and depth formats, topology type and sample count. False, with `why`
// naming the reason, for a blob that is too short or holds a value this reading does not cover; `out` is then not
// to be used.
inline bool decode_pipeline_head(std::span<const std::uint8_t> blob, PipelineState& out, const char** why = nullptr) {
    using namespace detail;
    if (blob.size() < kPipelineHeadBytes || blob.size() % 4) return fail(why, "the pipeline blob is not 80 bytes or more, in whole words");
    if (u32(blob, 0x04) != 0x1000 || u32(blob, 0x08) != 0 || u32(blob, 0x18) != 0 || u32(blob, 0x20) != 0)
        return fail(why, "the pipeline blob's constant words differ from the measured ones");
    if (u32(blob, 0x0c) > 1) return fail(why, "the pipeline blob's word 3 is not 0 or 1 (unread state)");
    out.sample_mask = u32(blob, 0x1c);
    if (out.sample_mask != 0xffffffffu) return fail(why, "the pipeline blob's word 7 is not 0xffffffff (unread state)");
    std::uint32_t formats = 0;
    while (formats < 8 && blob[0x10 + formats]) ++formats;
    for (std::uint32_t i = 0; i < 8; ++i) {
        if (i >= formats && blob[0x10 + i]) return fail(why, "a render-target format follows an empty one");
        out.rtv_formats[i] = blob[0x10 + i];
    }
    out.dsv_format = blob[0x24];
    out.render_targets = blob[0x25];
    if (out.render_targets != formats) return fail(why, "the render-target count is not the number of formats");
    const std::uint32_t topology = blob[0x26];
    // ASSUMPTION (see the header comment): 19 is the console's rectangle-list type, topology_type_rect_list.
    if ((topology < 1 || topology > 3) && topology != kTopologyTypeRectList)
        return fail(why, "the topology type is not point, line, triangle or the console's rectangle list (19)");
    out.topology_type = topology;
    if (blob[0x27]) return fail(why, "the byte after the topology type is not 0 (unread state)");
    if (u32(blob, 0x28) != 1 || u32(blob, 0x2c) || u32(blob, 0x30) || u32(blob, 0x34))
        return fail(why, "the sample words are not (1, 0, 0, 0): a multisampled pipeline is not read yet");
    out.sample_count = 1;
    return true;
}

// The state blob of a vertex pipeline: blend, depth, stencil and rasterizer state. `render_targets` is the pipeline
// blob's count; targets at or above it are left with blending off and no write mask. False, with `why`, for a blob
// of another layout or with state this reading does not cover.
inline bool decode_state_blob(std::span<const std::uint8_t> blob, PipelineState& out, const char** why = nullptr) {
    using namespace detail;
    if (blob.size() < kStateBlobMinBytes || blob.size() % 4) return fail(why, "the state blob is too short or not whole words");
    const std::uint32_t layout = u32(blob, 0x04);
    if (layout == kMeshLayout) return fail(why, "the state blob has the mesh-pipeline layout, which slot 57 is not known to take");
    if (layout != kVertexLayout) return fail(why, "the state blob's layout word is not 4");

    // Rasterizer.
    const std::uint32_t mode = u32(blob, 0x0a0);
    if ((mode & ~7u) != 0x201a40) return fail(why, "PA_SU_SC_MODE_CNTL has bits outside the measured value (unread state)");
    if ((mode & 3) == 3) return fail(why, "PA_SU_SC_MODE_CNTL culls both faces, which D3D12 cannot ask for");
    out.cull_mode = (mode & 1) ? 2 : (mode & 2) ? 3 : 1;
    out.front_counter_clockwise = !(mode & 4);  // ASSUMED: FACE = 0 is counter-clockwise (see the header comment)
    const std::uint32_t clip = u32(blob, 0x09c);
    if (clip != 0x80000 && clip != 0xc080000) return fail(why, "PA_CL_CLIP_CNTL is not one of the two measured values");
    out.depth_clip = clip == 0x80000;
    const float clamp = f32(blob, 0x0ac), scale = f32(blob, 0x0b0), offset = f32(blob, 0x0b4);
    // Written so that a NaN fails the range checks.
    if (!(clamp >= 0.0f && clamp <= 1.0f) || !(scale >= -1024.0f && scale <= 1024.0f) ||
        !(offset >= -65536.0f && offset <= 65536.0f) || offset != float(std::int32_t(offset)))
        return fail(why, "the polygon-offset words are not a clamp, a scale and a whole-number offset");
    out.depth_bias = std::int32_t(offset);
    out.depth_bias_clamp = clamp;
    out.slope_scaled_depth_bias = scale / 16.0f;  // the register holds 16 x the slope factor
    if (u32(blob, 0x120) != 0x100000) return fail(why, "the word at +0x120 is not the common value (unread state)");

    // Depth and stencil.
    const std::uint32_t depth = u32(blob, 0x0b8);
    if (depth & ~0x007007f7u) return fail(why, "DB_DEPTH_CONTROL has bits outside stencil, depth and their functions");
    if (depth & 8) return fail(why, "DB_DEPTH_CONTROL enables the depth-bounds test, which this description cannot carry");
    out.stencil_enable = depth & 1;
    out.depth_enable = depth & 2;
    out.depth_write = depth & 4;
    out.depth_func = ((depth >> 4) & 7) + 1;
    out.front.func = ((depth >> 8) & 7) + 1;
    out.back.func = (depth & 0x80) ? ((depth >> 20) & 7) + 1 : out.front.func;
    const std::uint32_t ops = u32(blob, 0x0bc);
    if (ops >> 24) return fail(why, "DB_STENCIL_CONTROL has bits above its six operations");
    StencilFace* const faces[2] = {&out.front, &out.back};
    for (int face = 0; face < 2; ++face) {
        const std::uint32_t v = ops >> (12 * face);
        faces[face]->fail = stencil_op(v & 15);
        faces[face]->pass = stencil_op((v >> 4) & 15);
        faces[face]->depth_fail = stencil_op((v >> 8) & 15);
        if (!faces[face]->fail || !faces[face]->pass || !faces[face]->depth_fail)
            return fail(why, "a stencil operation has no D3D12 equivalent");
    }
    const std::uint32_t ref = u32(blob, 0x0c0);
    if (ref != u32(blob, 0x0c4)) return fail(why, "the back-face stencil masks differ from the front-face ones");
    out.stencil_read_mask = std::uint8_t(ref >> 8);
    out.stencil_write_mask = std::uint8_t(ref >> 16);

    // Blend and write masks.
    const std::uint32_t masks = u32(blob, 0x0cc);
    for (std::uint32_t i = 0; i < 8; ++i) {
        BlendTarget target;
        if (i < out.render_targets) {
            target.write_mask = std::uint8_t((masks >> (4 * i)) & 15);
            const std::uint32_t v = u32(blob, 0x0d0 + 4 * i);
            if (v & 0x0000e000u) return fail(why, "CB_BLEND_CONTROL has bits outside its factors and functions");
            if (v & (1u << 30)) {
                target.enable = true;
                target.src = blend_factor(v & 31);
                target.op = blend_op((v >> 5) & 7);
                target.dest = blend_factor((v >> 8) & 31);
                if (v & (1u << 29)) {
                    target.src_alpha = blend_factor((v >> 16) & 31);
                    target.op_alpha = blend_op((v >> 21) & 7);
                    target.dest_alpha = blend_factor((v >> 24) & 31);
                } else {
                    // SEPARATE_ALPHA_BLEND clear: alpha blends with the colour factors. D3D12 takes no colour factor
                    // for alpha, and a colour factor's alpha channel is the matching alpha factor.
                    target.src_alpha = alpha_factor(target.src);
                    target.op_alpha = target.op;
                    target.dest_alpha = alpha_factor(target.dest);
                }
                if (!target.src || !target.dest || !target.op || !target.src_alpha || !target.dest_alpha ||
                    !target.op_alpha)
                    return fail(why, "a blend factor or function has no D3D12 equivalent");
            }
        }
        out.blend[i] = target;
    }
    return true;
}

}  // namespace leonida::slot57
