// Console device slot 57 (a graphics pipeline from the pipeline library's native records, docs/fspx.md "Slot 57")
// through the Xbox-ABI proxy over the Vulkan device, with synthetic inputs:
//   - the description's two programs are bare kind-6 programs, found in <shaders>/library/index.json by the SHA-256
//     of their bytes (fspx_code_sha256) under their stage, as slot 67 finds a compute program;
//   - the pipeline blob gives the render-target format and the input elements (POSITION, COLOR), matched to the vertex
//     shader's input signature by semantic; the state blob gives the write mask, culling, front face and blending;
//   - three pipelines draw the same two triangles, one wound counter-clockwise (left) and one clockwise (right),
//     green over a blue clear, and the image is read back:
//       A  cull back, counter-clockwise front, write mask red|green: the left triangle only, and blue survives under
//          it (cyan);
//       B  the same with the front-face bit set: the right triangle only;
//       C  no culling, additive blending, all channels: both triangles, green added to blue (cyan);
//   - a program with no translation, and a pixel program passed as the vertex program, create nothing.
// The two modules are SPIR-V written word by word below (no shader compiler is needed). No game data.
// Set LEONIDA_VK_VALIDATION=1 to run under VK_LAYER_KHRONOS_validation.
#include "test_temp_dir.h"
#include "vk_test_device.h"
#include "d3d12_test_shaders.h"
#include "slot57_fixture.h"
#include "../shims/d3d12_com.h"
#include "../shims/d3d12_xs_device.h"
#include "../src/shader_library.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <vector>

#define EXPECT(condition) do { \
    if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); return 1; } \
} while (0)

using namespace d3d12test;
using XboxHeapHandleFn = std::uint64_t(WINAPI*)(void* heap);  // heap slot 9: handle in RAX
using Slot57Fn = HRESULT(WINAPI*)(void* device, const void* desc, const IID* riid, void** out);

namespace {

struct Vertex {
    float x, y;
    std::uint8_t r, g, b, a;
};
static_assert(sizeof(Vertex) == 12);

// A SPIR-V module, one instruction at a time.
struct Module {
    std::vector<std::uint32_t> words{0x07230203, 0x00010000, 0, 0, 0};
    void op(std::uint32_t opcode, std::initializer_list<std::uint32_t> operands) {
        words.push_back(std::uint32_t((operands.size() + 1) << 16) | opcode);
        words.insert(words.end(), operands);
    }
};

constexpr std::uint32_t kMain0 = 0x6e69616d, kMain1 = 0;  // "main"

// in vec4 color (location 0), in vec2 position (location 1); out vec4 v_color (location 0);
// gl_Position = vec4(position, 0, 1); v_color = color.
std::vector<std::uint32_t> vertex_module() {
    Module m;
    m.words[3] = 28;                                           // id bound
    m.op(17, {1});                                             // OpCapability Shader
    m.op(14, {0, 1});                                          // OpMemoryModel Logical GLSL450
    m.op(15, {0, 4, kMain0, kMain1, 10, 12, 14, 15});          // OpEntryPoint Vertex %4 "main"
    m.op(71, {10, 30, 0});                                     // OpDecorate %color Location 0
    m.op(71, {12, 30, 1});                                     // OpDecorate %position Location 1
    m.op(71, {14, 30, 0});                                     // OpDecorate %v_color Location 0
    m.op(71, {15, 11, 0});                                     // OpDecorate %gl_Position BuiltIn Position
    m.op(19, {2});                                             // %2 = OpTypeVoid
    m.op(33, {3, 2});                                          // %3 = OpTypeFunction %2
    m.op(22, {6, 32});                                         // %6 = OpTypeFloat 32
    m.op(23, {7, 6, 4});                                       // %7 = OpTypeVector %6 4
    m.op(23, {8, 6, 2});                                       // %8 = OpTypeVector %6 2
    m.op(32, {9, 1, 7});                                       // %9 = OpTypePointer Input %7
    m.op(59, {9, 10, 1});                                      // %10 = OpVariable Input (color)
    m.op(32, {11, 1, 8});                                      // %11 = OpTypePointer Input %8
    m.op(59, {11, 12, 1});                                     // %12 = OpVariable Input (position)
    m.op(32, {13, 3, 7});                                      // %13 = OpTypePointer Output %7
    m.op(59, {13, 14, 3});                                     // %14 = OpVariable Output (v_color)
    m.op(59, {13, 15, 3});                                     // %15 = OpVariable Output (gl_Position)
    m.op(43, {6, 20, 0x00000000});                             // %20 = OpConstant 0.0
    m.op(43, {6, 21, 0x3f800000});                             // %21 = OpConstant 1.0
    m.op(54, {2, 4, 0, 3});                                    // %4 = OpFunction
    m.op(248, {22});                                           // OpLabel
    m.op(61, {8, 23, 12});                                     // %23 = OpLoad %position
    m.op(81, {6, 24, 23, 0});                                  // %24 = OpCompositeExtract x
    m.op(81, {6, 25, 23, 1});                                  // %25 = OpCompositeExtract y
    m.op(80, {7, 26, 24, 25, 20, 21});                         // %26 = OpCompositeConstruct
    m.op(62, {15, 26});                                        // OpStore gl_Position
    m.op(61, {7, 27, 10});                                     // %27 = OpLoad %color
    m.op(62, {14, 27});                                        // OpStore v_color
    m.op(253, {});                                             // OpReturn
    m.op(56, {});                                              // OpFunctionEnd
    return m.words;
}

// in vec4 v_color (location 0); out vec4 target (location 0) = v_color.
std::vector<std::uint32_t> fragment_module() {
    Module m;
    m.words[3] = 14;
    m.op(17, {1});                                             // OpCapability Shader
    m.op(14, {0, 1});                                          // OpMemoryModel Logical GLSL450
    m.op(15, {4, 4, kMain0, kMain1, 9, 11});                   // OpEntryPoint Fragment %4 "main"
    m.op(16, {4, 7});                                          // OpExecutionMode OriginUpperLeft
    m.op(71, {9, 30, 0});                                      // OpDecorate %v_color Location 0
    m.op(71, {11, 30, 0});                                     // OpDecorate %target Location 0
    m.op(19, {2});
    m.op(33, {3, 2});
    m.op(22, {6, 32});
    m.op(23, {7, 6, 4});
    m.op(32, {8, 1, 7});                                       // %8 = OpTypePointer Input %7
    m.op(59, {8, 9, 1});                                       // %9 = OpVariable Input
    m.op(32, {10, 3, 7});                                      // %10 = OpTypePointer Output %7
    m.op(59, {10, 11, 3});                                     // %11 = OpVariable Output
    m.op(54, {2, 4, 0, 3});
    m.op(248, {12});
    m.op(61, {7, 13, 9});
    m.op(62, {11, 13});
    m.op(253, {});
    m.op(56, {});
    return m.words;
}

bool write_module(const std::filesystem::path& path, const std::vector<std::uint32_t>& words) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(words.data()), std::streamsize(words.size() * 4));
    return bool(out);
}

// A kind-6 program: the 36-byte prologue (three VOP3 words with opcode 0x180, bits 31..16 = 0xD580, and literals),
// a filler word and s_endpgm. `marker` tells the programs apart.
slot57test::Bytes kind6(std::uint32_t marker) {
    slot57test::Bytes out;
    for (std::uint32_t i = 0; i < 3; ++i) {
        put32(out, 0xd5800000u | i);
        put32(out, i == 0 ? marker : 0xffu);
        put32(out, 0x1000u + i);
    }
    put32(out, 0x7e000280u);
    put32(out, 0xbf810000u);
    return out;
}

using Rgba = std::array<std::uint8_t, 4>;

}  // namespace

int main() {
    vktest::Device d;
    const auto device_status = d.initialize();
    if (device_status == vktest::DeviceStatus::unavailable) {
        std::fprintf(stderr, "SKIP: no usable Vulkan 1.3 device\n");
        return 77;
    }
    EXPECT(device_status == vktest::DeviceStatus::ready);

    // The programs, and the index that maps their hashes to the modules: an empty main index and a pipeline-library
    // side index, where make_game.py puts the library's translations.
    const auto vs_code = kind6(0x56530057), ps_code = kind6(0x50530057), untranslated = kind6(0x4e4f4e45);
    const testfiles::ScratchDir scratch("leonida_slot57_draw_test");
    const auto& library = scratch.path();
    std::filesystem::create_directories(library / "library");
    EXPECT(write_module(library / "library" / "lib_vs_00001.spv", vertex_module()));
    EXPECT(write_module(library / "library" / "lib_ps_00002.spv", fragment_module()));
    {
        std::ofstream index(library / "index.json", std::ios::binary);
        index << "{}";
        std::ofstream sub(library / "library" / "index.json", std::ios::binary);
        sub << "{\"lib_vs_00001\": {\"stage\": \"vs\", \"fspx_code_sha256\": \"" << leonida::sha256_hex(vs_code)
            << "\", \"bindings\": [], \"push_constant_bytes\": 144, \"input_signature\": ["
               "{\"semantic\": \"COLOR\", \"semantic_index\": 0, \"register\": 0, \"mask\": 15, \"rw_mask\": 15, "
               "\"system_value\": 0, \"component_type\": \"float\"}, "
               "{\"semantic\": \"POSITION\", \"semantic_index\": 0, \"register\": 1, \"mask\": 3, \"rw_mask\": 3, "
               "\"system_value\": 0, \"component_type\": \"float\"}]},\n"
               " \"lib_ps_00002\": {\"stage\": \"ps\", \"fspx_code_sha256\": \"" << leonida::sha256_hex(ps_code)
            << "\", \"bindings\": [], \"push_constant_bytes\": 144, \"outputs\": [0]}}";
    }

    constexpr UINT size = 64;
    std::vector<std::uint8_t> images[3];
    {
        rt::d3d12::VulkanDeviceInfo info{};
        info.get_instance_proc = d.instance_proc;
        info.instance = d.instance;
        info.physical = d.physical;
        info.device = d.device;
        info.queue = d.queue;
        info.queue_family = d.family;
        info.buffer_device_address = true;
        info.subgroup = d.subgroup;
        info.shader_dir = library.string();
        ID3D12Device* inner = nullptr;
        EXPECT(SUCCEEDED(rt::d3d12::create_vulkan_device(info, IID_PPV_ARGS(&inner))));
        Ref<ID3D12Device> device;
        EXPECT(SUCCEEDED(rt::d3d12_xs::create_device_proxy(inner, __uuidof(ID3D12Device),
                                                           reinterpret_cast<void**>(&device.p), /*console_lists=*/false)));
        rt::d3d12::set_device_outer(inner, device.p);

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
        root_desc.Desc_1_0 = {0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        const auto root_blob = serialize_root_signature(root_desc);
        EXPECT(!root_blob.empty());
        Ref<ID3D12RootSignature> root;
        EXPECT(SUCCEEDED(device->CreateRootSignature(0, root_blob.data(), root_blob.size(), IID_PPV_ARGS(&root.p))));

        // The pipeline blob: one R8G8B8A8_UNORM target, no depth, triangles; POSITION R32G32_FLOAT at 0 and COLOR
        // R8G8B8A8_UNORM at 8 of slot 0, in the opposite order of the shader's registers.
        using namespace slot57test;
        const Bytes pipeline = pipeline_blob({28}, 0, 3, {{"POSITION", 0, 16, 0, 0, 0, 0}, {"COLOR", 0, 28, 0, 8, 0, 0}});
        Bytes state_a = state_blob();
        put(state_a, kRasterMode, 0x201a40u | kCullBack);
        put(state_a, kTargetMask, 0x3u);  // red | green
        Bytes state_b = state_a;
        put(state_b, kRasterMode, 0x201a40u | kCullBack | kFrontClockwise);
        Bytes state_c = state_blob();
        put(state_c, kBlend0, 0xe1010101u);  // ONE + ONE, colour and alpha

        const auto slot57 = reinterpret_cast<Slot57Fn>(slot(device.p, 57));
        const IID iid_pso = __uuidof(ID3D12PipelineState);
        // The counters count from their first read.
        const std::uint64_t hits_before = rt::d3d12_xs::device_slot_hits(57);
        Ref<ID3D12PipelineState> pso[3];
        const Bytes* const states[3] = {&state_a, &state_b, &state_c};
        for (int i = 0; i < 3; ++i) {
            const Desc d57 = desc(root.p, *states[i], pipeline, ps_code, vs_code);
            EXPECT(slot57(device.p, &d57, &iid_pso, reinterpret_cast<void**>(&pso[i].p)) == S_OK && pso[i].p);
        }
        EXPECT(rt::d3d12_xs::device_slot_hits(57) == hits_before + 3);
        {
            // No translation for the vertex program; the pixel program as the vertex program (found by stage as well
            // as hash); bytes that are no kind-6 program. Nothing is created.
            void* out = reinterpret_cast<void*>(0x1111);
            Desc bad = desc(root.p, state_a, pipeline, ps_code, untranslated);
            EXPECT(FAILED(slot57(device.p, &bad, &iid_pso, &out)) && out == nullptr);
            out = reinterpret_cast<void*>(0x1111);
            bad = desc(root.p, state_a, pipeline, ps_code, ps_code);
            EXPECT(FAILED(slot57(device.p, &bad, &iid_pso, &out)) && out == nullptr);
            out = reinterpret_cast<void*>(0x1111);
            const Bytes junk(64, 0x5a);
            bad = desc(root.p, state_a, pipeline, ps_code, junk);
            EXPECT(slot57(device.p, &bad, &iid_pso, &out) == E_INVALIDARG && out == nullptr);
            // The same programs through the PC method: a bare kind-6 stage is taken there too.
            const D3D12_INPUT_ELEMENT_DESC elements[2] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pc{};
            pc.pRootSignature = root.p;
            pc.VS = {vs_code.data(), vs_code.size()};
            pc.PS = {ps_code.data(), ps_code.size()};
            pc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            pc.SampleMask = 0xffffffff;
            pc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            pc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            pc.RasterizerState.DepthClipEnable = TRUE;
            pc.InputLayout = {elements, 2};
            pc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pc.NumRenderTargets = 1;
            pc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            pc.SampleDesc.Count = 1;
            Ref<ID3D12PipelineState> direct;
            EXPECT(SUCCEEDED(device->CreateGraphicsPipelineState(&pc, IID_PPV_ARGS(&direct.p))) && direct.p);
        }

        // Two green triangles in D3D clip space (y up): the left one counter-clockwise, the right one clockwise.
        const Vertex vertices[6] = {{-0.9f, -0.9f, 0, 255, 0, 255}, {-0.1f, -0.9f, 0, 255, 0, 255},
                                    {-0.5f, 0.9f, 0, 255, 0, 255},  {0.1f, -0.9f, 0, 255, 0, 255},
                                    {0.5f, 0.9f, 0, 255, 0, 255},   {0.9f, -0.9f, 0, 255, 0, 255}};
        const D3D12_HEAP_PROPERTIES default_heap{D3D12_HEAP_TYPE_DEFAULT};
        const D3D12_HEAP_PROPERTIES upload_heap{D3D12_HEAP_TYPE_UPLOAD};
        const D3D12_HEAP_PROPERTIES readback_heap{D3D12_HEAP_TYPE_READBACK};
        Ref<ID3D12Resource> upload, readback, target;
        const auto upload_desc = buffer_desc(4096);
        EXPECT(SUCCEEDED(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                         IID_PPV_ARGS(&upload.p))));
        const auto readback_desc = buffer_desc(size * 256);
        EXPECT(SUCCEEDED(device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&readback.p))));
        D3D12_RESOURCE_DESC rt_desc{};
        rt_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rt_desc.Width = rt_desc.Height = size;
        rt_desc.DepthOrArraySize = 1;
        rt_desc.MipLevels = 1;
        rt_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rt_desc.SampleDesc.Count = 1;
        rt_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        EXPECT(SUCCEEDED(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &rt_desc,
                                                         D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&target.p))));
        std::uint8_t* up = nullptr;
        EXPECT(SUCCEEDED(upload->Map(0, nullptr, reinterpret_cast<void**>(&up))));
        std::memcpy(up, vertices, sizeof(vertices));

        const D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        Ref<ID3D12DescriptorHeap> rtv_heap;
        EXPECT(SUCCEEDED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap.p))));
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv{
            SIZE_T(reinterpret_cast<XboxHeapHandleFn>(slot(rtv_heap.p, 9))(rtv_heap.p))};
        device->CreateRenderTargetView(target.p, nullptr, rtv);

        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        Ref<ID3D12CommandQueue> queue;
        Ref<ID3D12CommandAllocator> allocator;
        Ref<ID3D12GraphicsCommandList> list;
        Ref<ID3D12Fence> fence;
        EXPECT(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue.p))));
        EXPECT(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.p))));
        EXPECT(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.p, pso[0].p,
                                                   IID_PPV_ARGS(&list.p))));
        EXPECT(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.p))));
        void* mapped = nullptr;
        EXPECT(SUCCEEDED(readback->Map(0, nullptr, &mapped)));

        D3D12_RESOURCE_STATES target_state = D3D12_RESOURCE_STATE_COMMON;
        for (int pass = 0; pass < 3; ++pass) {
            if (pass) EXPECT(SUCCEEDED(allocator->Reset()) && SUCCEEDED(list->Reset(allocator.p, pso[pass].p)));
            D3D12_RESOURCE_BARRIER to_target = transition(target.p, target_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
            list->ResourceBarrier(1, &to_target);
            const FLOAT blue[4] = {0, 0, 1, 1};
            list->ClearRenderTargetView(rtv, blue, 0, nullptr);
            list->SetGraphicsRootSignature(root.p);
            list->SetPipelineState(pso[pass].p);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            const D3D12_VERTEX_BUFFER_VIEW vbv{upload->GetGPUVirtualAddress(), sizeof(vertices), sizeof(Vertex)};
            list->IASetVertexBuffers(0, 1, &vbv);
            const D3D12_VIEWPORT viewport{0, 0, float(size), float(size), 0, 1};
            const D3D12_RECT scissor{0, 0, LONG(size), LONG(size)};
            list->RSSetViewports(1, &viewport);
            list->RSSetScissorRects(1, &scissor);
            list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            list->DrawInstanced(6, 1, 0, 0);
            D3D12_RESOURCE_BARRIER to_copy = transition(target.p, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                        D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->ResourceBarrier(1, &to_copy);
            target_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = readback.p;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = {0, {DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 256}};
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = target.p;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            EXPECT(list->Close() == S_OK);
            ID3D12CommandList* lists[] = {list.p};
            queue->ExecuteCommandLists(1, lists);
            EXPECT(rt::d3d12::last_execute_result(queue.p) == leonida::ReplayResult::success);
            EXPECT(SUCCEEDED(queue->Signal(fence.p, UINT64(pass) + 1)));
            EXPECT(SUCCEEDED(fence->SetEventOnCompletion(UINT64(pass) + 1, nullptr)));
            images[pass].assign(static_cast<const std::uint8_t*>(mapped),
                                static_cast<const std::uint8_t*>(mapped) + size * 256);
        }

        for (auto* r : {&upload, &readback, &target}) r->reset();
        for (auto& p : pso) p.reset();
        root.reset();
        rtv_heap.reset();
        list.reset();
        allocator.reset();
        queue.reset();
        fence.reset();
        EXPECT(rt::d3d12_xs::device_proxy_ref_count(device.p) == 1);
    }

    // Probes: inside the left triangle, inside the right one, and a corner neither covers.
    const auto at = [&](int image, UINT x, UINT y) {
        const std::uint8_t* p = images[image].data() + y * 256 + x * 4;
        return Rgba{p[0], p[1], p[2], p[3]};
    };
    const Rgba blue{0, 0, 255, 255}, cyan{0, 255, 255, 255};
    const char* const names[3] = {"A (cull back, counter-clockwise front, mask RG)", "B (clockwise front)",
                                  "C (no culling, additive)"};
    const Rgba want[3][3] = {{cyan, blue, blue}, {blue, cyan, blue}, {cyan, cyan, blue}};
    int failures = 0;
    for (int i = 0; i < 3; ++i) {
        const Rgba got[3] = {at(i, 16, 32), at(i, 48, 32), at(i, 2, 2)};
        for (int probe = 0; probe < 3; ++probe) {
            if (got[probe] == want[i][probe]) continue;
            ++failures;
            std::fprintf(stderr, "pipeline %s, probe %d: %u %u %u %u, expected %u %u %u %u\n", names[i], probe,
                         got[probe][0], got[probe][1], got[probe][2], got[probe][3], want[i][probe][0],
                         want[i][probe][1], want[i][probe][2], want[i][probe][3]);
        }
    }
    EXPECT(failures == 0);
    std::printf("slot 57: three pipelines drew as their state blobs say\n");
    return 0;
}
