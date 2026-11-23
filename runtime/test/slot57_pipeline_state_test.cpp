// src/slot57_pipeline_state.h on hand-built blobs: the pipeline blob's head and a vertex pipeline's state blob, read
// into D3D12-level values, and the refusals for state the reading does not cover. No game data: every word below is
// written here from the register layouts the header names.
#include "../src/slot57_pipeline_state.h"
#include "../src/rect_list_geometry.h"
#include "slot57_fixture.h"

#include <cstdio>
#include <cstring>
#include <vector>

#define CHECK(condition) do {                                        \
    if (!(condition)) {                                              \
        std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
        return 1;                                                    \
    }                                                                \
} while (0)

// Topology type 19 is passed on as the rectangle-list type the Vulkan device builds (ASSUMPTION in the header).
static_assert(leonida::slot57::kTopologyTypeRectList == leonida::topology_type_rect_list);

namespace {

using leonida::slot57::PipelineState;
using slot57test::Bytes;
using slot57test::put;

Bytes head(std::initializer_list<std::uint8_t> formats, std::uint8_t dsv, std::uint8_t topology = 3) {
    return slot57test::pipeline_blob(formats, dsv, topology);
}
Bytes state() { return slot57test::state_blob(); }

}  // namespace

int main() {
    using namespace leonida::slot57;
    const char* why = nullptr;

    // The head: two targets (R8G8B8A8_UNORM, R16G16B16A16_FLOAT), D32_FLOAT_S8X24_UINT, triangles.
    {
        PipelineState s;
        CHECK(decode_pipeline_head(head({28, 10}, 20), s, &why));
        CHECK(s.render_targets == 2 && s.rtv_formats[0] == 28 && s.rtv_formats[1] == 10 && s.rtv_formats[2] == 0);
        CHECK(s.dsv_format == 20 && s.topology_type == 3 && s.sample_count == 1 && s.sample_mask == 0xffffffffu);
        // Elements after the head do not change it; the console depth format 118 is passed through as a number.
        Bytes longer = head({}, 118, 2);
        longer.resize(152, 0x41);
        CHECK(decode_pipeline_head(longer, s, &why));
        CHECK(s.render_targets == 0 && s.dsv_format == 118 && s.topology_type == 2);
        // The console's topology type 19 (408 vertex records of 1.0.2.1, the title's first slot-57 calls among them)
        // is the rectangle-list PSO type, passed on as it is.
        CHECK(decode_pipeline_head(head({}, 55, 19), s, &why));
        CHECK(s.topology_type == 19 && s.render_targets == 0 && s.dsv_format == 55);
    }
    // Head refusals: too short, a count that is not the number of formats, a gap, topology values that are neither
    // D3D12 types nor the console's rectangle list, the other sample form, and the words whose other values are not
    // read.
    {
        PipelineState s;
        const Bytes good = head({28}, 0);
        CHECK(!decode_pipeline_head(std::span(good).first(76), s, &why) && why);
        Bytes bad = good;
        bad[0x25] = 2;
        CHECK(!decode_pipeline_head(bad, s, &why));
        bad = good;
        bad[0x12] = 28;  // target 2 without target 1
        bad[0x25] = 2;
        CHECK(!decode_pipeline_head(bad, s, &why));
        for (const std::uint8_t topology : {std::uint8_t(0), std::uint8_t(4), std::uint8_t(18), std::uint8_t(20)}) {
            bad = good;
            bad[0x26] = topology;
            CHECK(!decode_pipeline_head(bad, s, &why));
        }
        bad = good;
        put(bad, 0x28, 0x800001u);
        CHECK(!decode_pipeline_head(bad, s, &why));
        bad = good;
        put(bad, 0x30, 0x04040404u);
        CHECK(!decode_pipeline_head(bad, s, &why));
        bad = good;
        put(bad, 0x0c, 0x10fu);
        CHECK(!decode_pipeline_head(bad, s, &why));
        bad = good;
        put(bad, 0x0c, 1u);  // the value blobs with elements have
        CHECK(decode_pipeline_head(bad, s, &why));
        bad = good;
        put(bad, 0x04, 0x2000u);
        CHECK(!decode_pipeline_head(bad, s, &why));
        bad = good;
        put(bad, 0x1c, 0x0000ffffu);
        CHECK(!decode_pipeline_head(bad, s, &why));
    }

    // The state blob with everything off.
    {
        PipelineState s;
        s.render_targets = 1;
        CHECK(decode_state_blob(state(), s, &why));
        CHECK(s.cull_mode == 1 && s.front_counter_clockwise && s.depth_clip);
        CHECK(s.depth_bias == 0 && s.depth_bias_clamp == 0.0f && s.slope_scaled_depth_bias == 0.0f);
        CHECK(!s.depth_enable && !s.depth_write && s.depth_func == 8 && !s.stencil_enable);
        CHECK(s.front.func == 8 && s.back.func == 8 && s.front.pass == 1 && s.back.fail == 1);
        CHECK(s.stencil_read_mask == 0xff && s.stencil_write_mask == 0x00);
        CHECK(!s.blend[0].enable && s.blend[0].write_mask == 15 && s.blend[0].src == 2 && s.blend[0].dest == 1);
        CHECK(s.blend[1].write_mask == 0);  // beyond the render-target count
    }
    // Rasterizer: cull bits, the front-face bit, depth clip off and a depth bias.
    {
        PipelineState s;
        Bytes b = state();
        put(b, 0x0a0, 0x201a42u);
        CHECK(decode_state_blob(b, s, &why) && s.cull_mode == 3 && s.front_counter_clockwise);
        put(b, 0x0a0, 0x201a41u);
        CHECK(decode_state_blob(b, s, &why) && s.cull_mode == 2);
        put(b, 0x0a0, 0x201a46u);
        CHECK(decode_state_blob(b, s, &why) && s.cull_mode == 3 && !s.front_counter_clockwise);
        put(b, 0x0a0, 0x201a43u);  // both faces
        CHECK(!decode_state_blob(b, s, &why));
        put(b, 0x0a0, 0x201a48u);  // a polygon-mode bit
        CHECK(!decode_state_blob(b, s, &why));
        b = state();
        put(b, 0x09c, 0xc080000u);
        put(b, 0x0ac, 0.0025f);
        put(b, 0x0b0, 24.0f);
        put(b, 0x0b4, 5.0f);
        CHECK(decode_state_blob(b, s, &why) && !s.depth_clip);
        CHECK(s.depth_bias == 5 && s.depth_bias_clamp == 0.0025f && s.slope_scaled_depth_bias == 1.5f);
        put(b, 0x0b4, 0.5f);  // not a whole number of depth-bias units
        CHECK(!decode_state_blob(b, s, &why));
        b = state();
        put(b, 0x09c, 0x90000u);
        CHECK(!decode_state_blob(b, s, &why));
        b = state();
        put(b, 0x120, 0xe00441u);
        CHECK(!decode_state_blob(b, s, &why));
    }
    // Depth and stencil: depth on with writes and GREATER_EQUAL; stencil on, front EQUAL with REPLACE on pass, back
    // NOT_EQUAL with INCR on depth fail; read mask 0x0f, write mask 0xf0.
    {
        PipelineState s;
        Bytes b = state();
        put(b, 0x0b8, 1u | 2u | 4u | (6u << 4) | 0x80u | (2u << 8) | (5u << 20));
        put(b, 0x0bc, (3u << 4) | (8u << 20));
        put(b, 0x0c0, 0x01f00f00u);
        put(b, 0x0c4, 0x01f00f00u);
        CHECK(decode_state_blob(b, s, &why));
        CHECK(s.depth_enable && s.depth_write && s.depth_func == 7 && s.stencil_enable);
        CHECK(s.front.func == 3 && s.front.pass == 3 && s.front.fail == 1 && s.front.depth_fail == 1);
        CHECK(s.back.func == 6 && s.back.depth_fail == 7 && s.back.pass == 1);
        CHECK(s.stencil_read_mask == 0x0f && s.stencil_write_mask == 0xf0);
        // Without BACKFACE_ENABLE the back face tests with the front function.
        put(b, 0x0b8, 1u | (2u << 8) | (5u << 20));
        CHECK(decode_state_blob(b, s, &why) && s.back.func == 3);
        put(b, 0x0b8, 2u | 8u | 0x80u);  // depth bounds
        CHECK(!decode_state_blob(b, s, &why));
        b = state();
        put(b, 0x0bc, 2u);  // ONES: no D3D12 operation
        CHECK(!decode_state_blob(b, s, &why));
        b = state();
        put(b, 0x0c4, 0x01ffff00u);  // back-face masks of their own
        CHECK(!decode_state_blob(b, s, &why));
    }
    // Blend: target 0 SRC_ALPHA / INV_SRC_ALPHA with separate alpha ONE / INV_SRC_ALPHA, target 1 dual-source
    // (ONE, SRC1_COLOR with alpha ONE, SRC1_ALPHA), target 2 MAX without separate alpha on colour factors; masks
    // RGBA, RGB and R.
    {
        PipelineState s;
        s.render_targets = 3;
        Bytes b = state();
        put(b, 0x0cc, 0x0000017fu);
        put(b, 0x0d0, 0xe5010504u);
        put(b, 0x0d4, 0xf1010f01u);
        put(b, 0x0d8, 0xc0000862u);  // src SRC_COLOR, MAX, dest DEST_COLOR, colour factors for alpha too
        CHECK(decode_state_blob(b, s, &why));
        CHECK(s.blend[0].enable && s.blend[0].src == 5 && s.blend[0].dest == 6 && s.blend[0].op == 1);
        CHECK(s.blend[0].src_alpha == 2 && s.blend[0].dest_alpha == 6 && s.blend[0].op_alpha == 1);
        CHECK(s.blend[0].write_mask == 15);
        CHECK(s.blend[1].enable && s.blend[1].src == 2 && s.blend[1].dest == 16 && s.blend[1].src_alpha == 2 &&
              s.blend[1].dest_alpha == 18 && s.blend[1].write_mask == 7);
        CHECK(s.blend[2].enable && s.blend[2].src == 3 && s.blend[2].dest == 9 && s.blend[2].op == 5);
        CHECK(s.blend[2].src_alpha == 5 && s.blend[2].dest_alpha == 7 && s.blend[2].op_alpha == 5);
        CHECK(s.blend[2].write_mask == 1 && !s.blend[3].enable);
        put(b, 0x0d0, 0xc000000bu);  // BOTH_SRC_ALPHA: no D3D12 factor
        CHECK(!decode_state_blob(b, s, &why));
        put(b, 0x0d0, 0xc00000a1u);  // function 5: not one of the five
        CHECK(!decode_state_blob(b, s, &why));
        // The same word on a target the pipeline does not have is not read.
        s.render_targets = 0;
        CHECK(decode_state_blob(b, s, &why) && !s.blend[0].enable && s.blend[0].write_mask == 0);
    }
    // Layouts: the mesh layout and unknown layout words are refused, as are short and unaligned blobs.
    {
        PipelineState s;
        Bytes b = state();
        put(b, 0x004, 6u);
        CHECK(!decode_state_blob(b, s, &why));
        put(b, 0x004, 5u);
        CHECK(!decode_state_blob(b, s, &why));
        b = state();
        CHECK(!decode_state_blob(std::span(b).first(0x120), s, &why));
        CHECK(!decode_state_blob(std::span(b).first(0x126), s, &why));
        CHECK(decode_state_blob(std::span(b).first(0x124), s, &why));
    }
    return 0;
}
