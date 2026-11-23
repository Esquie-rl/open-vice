// Console device slot 57 over the user's own pipeline library (docs/shader_library.md): every vertex pipeline record
// is handed to the slot the way the title's description carries it (state blob, pipeline blob, pixel program and
// stage blob, vertex program and stage blob), through the Xbox-ABI proxy over the Vulkan device.
// Package-gated: it needs LEONIDA_PSO_LIBRARY (the file `tools/shader_library.py <shaders.rpf> --save <file>` writes)
// and LEONIDA_SHADERS (the translated work folder, with the library's vertex and pixel programs under library/), and
// prints SKIP and exits 77 when either is unset. No game data is in the repository, and the output is counts only.
//   LEONIDA_SLOT57_LIMIT=<n>  stop after n records (default: all)
// It fails only when a record that the readers accept and whose programs are both translated cannot be handed to
// the device at all (a crash or E_POINTER); refusals and device failures are counted and printed by HRESULT.
// Set LEONIDA_VK_VALIDATION=1 to run under VK_LAYER_KHRONOS_validation.
#include "vk_test_device.h"
#include "d3d12_test_shaders.h"
#include "slot57_fixture.h"
#include "../shims/d3d12_com.h"
#include "../shims/d3d12_xs_device.h"
#include "../src/shader_library.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
#include <vector>

#define EXPECT(condition) do { \
    if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); return 1; } \
} while (0)

using namespace d3d12test;
using Slot57Fn = HRESULT(WINAPI*)(void* device, const void* desc, const IID* riid, void** out);

namespace {

// The library file: header, graphics records (0x70 bytes), compute records (24), blob table, program table, blob
// section, code section (docs/shader_library.md section 2).
struct Library {
    std::vector<std::uint8_t> bytes;
    std::uint32_t graphics = 0, compute = 0, blobs = 0, programs = 0, blob_bytes = 0, code_bytes = 0;
    std::size_t records = 0, blob_table = 0, program_table = 0, blob_data = 0, code = 0;

    std::uint32_t u32(std::size_t at) const {
        std::uint32_t v;
        std::memcpy(&v, bytes.data() + at, 4);
        return v;
    }
    bool parse() {
        if (bytes.size() < 0x80 || std::memcmp(bytes.data(), "PS\0\x09", 4) != 0) return false;
        graphics = u32(0x50), compute = u32(0x54), blobs = u32(0x58), programs = u32(0x5c);
        blob_bytes = u32(0x60), code_bytes = u32(0x64);
        records = 0x80;
        blob_table = records + std::size_t(graphics) * 0x70 + std::size_t(compute) * 24;
        program_table = blob_table + std::size_t(blobs) * 8;
        blob_data = program_table + std::size_t(programs) * 8;
        code = blob_data + blob_bytes;
        return code + code_bytes == bytes.size();
    }
    // An entry of the blob table (`program` false) or the program table, or an empty span for 0xFFFFFFFF or a
    // range outside its section.
    std::span<const std::uint8_t> part(std::uint32_t index, bool program) const {
        if (index == 0xffffffffu || index >= (program ? programs : blobs)) return {};
        const std::size_t entry = (program ? program_table : blob_table) + std::size_t(index) * 8;
        const std::uint64_t offset = u32(entry), size = u32(entry + 4);
        if (offset + size > (program ? code_bytes : blob_bytes)) return {};
        return {bytes.data() + (program ? code : blob_data) + offset, std::size_t(size)};
    }
};

}  // namespace

int main() {
    const char* library_env = std::getenv("LEONIDA_PSO_LIBRARY");
    const char* shaders_env = std::getenv("LEONIDA_SHADERS");
    if (!library_env || !*library_env || !shaders_env || !*shaders_env) {
        std::printf("SKIP: LEONIDA_PSO_LIBRARY (shader_library.py --save) and LEONIDA_SHADERS are not both set\n");
        return 77;
    }
    vktest::Device d;
    const auto device_status = d.initialize();
    if (device_status == vktest::DeviceStatus::unavailable) {
        std::fprintf(stderr, "SKIP: no usable Vulkan 1.3 device\n");
        return 77;
    }
    EXPECT(device_status == vktest::DeviceStatus::ready);

    Library lib;
    {
        std::ifstream in(library_env, std::ios::binary);
        lib.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    EXPECT(lib.parse());
    const char* limit_env = std::getenv("LEONIDA_SLOT57_LIMIT");
    const unsigned long limit = limit_env && *limit_env ? std::strtoul(limit_env, nullptr, 10) : ~0ul;

    leonida::ShaderLibrary index;
    EXPECT(index.load(shaders_env));

    unsigned long vertex_records = 0, created = 0, translated_pairs = 0;
    std::map<HRESULT, unsigned long> results, translated_results;
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
        info.bindless = d.bindless;
        info.bindless_partially_bound = d.partially_bound;
        info.image_view_min_lod = d.image_view_min_lod;
        info.features = d.enabled;
        info.vertex_attribute_divisor = d.vertex_attribute_divisor;
        info.shader_dir = shaders_env;
        ID3D12Device* inner = nullptr;
        EXPECT(SUCCEEDED(rt::d3d12::create_vulkan_device(info, IID_PPV_ARGS(&inner))));
        Ref<ID3D12Device> device;
        EXPECT(SUCCEEDED(rt::d3d12_xs::create_device_proxy(inner, __uuidof(ID3D12Device),
                                                           reinterpret_cast<void**>(&device.p), /*console_lists=*/false)));
        rt::d3d12::set_device_outer(inner, device.p);

        // The title's root signatures are not in the library; an empty one with the input-assembler flag stands in.
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
        root_desc.Desc_1_0 = {0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        const auto root_blob = serialize_root_signature(root_desc);
        EXPECT(!root_blob.empty());
        Ref<ID3D12RootSignature> root;
        EXPECT(SUCCEEDED(device->CreateRootSignature(0, root_blob.data(), root_blob.size(), IID_PPV_ARGS(&root.p))));

        const auto slot57 = reinterpret_cast<Slot57Fn>(slot(device.p, 57));
        const IID iid_pso = __uuidof(ID3D12PipelineState);
        for (std::uint32_t i = 0; i < lib.graphics && vertex_records < limit; ++i) {
            const std::size_t record = lib.records + std::size_t(i) * 0x70;
            const std::uint32_t ps_program = lib.u32(record + 56), vs_program = lib.u32(record + 64);
            if (vs_program == 0xffffffffu || lib.u32(record + 72) != 0xffffffffu) continue;  // no VS, or a mesh program
            ++vertex_records;
            const std::span<const std::uint8_t> parts[6] = {
                lib.part(lib.u32(record + 8), false),  lib.part(lib.u32(record + 12), false),
                lib.part(ps_program, true),            lib.part(lib.u32(record + 60), false),
                lib.part(vs_program, true),            lib.part(lib.u32(record + 68), false)};
            slot57test::Desc desc;
            desc.root_signature = root.p;
            for (int k = 0; k < 6; ++k)
                if (!parts[k].empty()) desc.pairs[k] = {parts[k].data(), parts[k].size()};
            const auto translated = [&](const char* stage, std::span<const std::uint8_t> code) {
                return code.empty() || !index.fspx_candidates(stage, leonida::sha256_hex(code)).empty();
            };
            const bool both = translated("vs", parts[4]) && translated("ps", parts[2]);
            translated_pairs += both;
            ID3D12PipelineState* pso = nullptr;
            const HRESULT hr = slot57(device.p, &desc, &iid_pso, reinterpret_cast<void**>(&pso));
            EXPECT(SUCCEEDED(hr) == (pso != nullptr));
            EXPECT(hr != E_POINTER);
            ++results[hr];
            if (both) ++translated_results[hr];
            if (pso) {
                ++created;
                pso->Release();
            }
        }
        root.reset();
        EXPECT(rt::d3d12_xs::device_proxy_ref_count(device.p) == 1);
    }

    std::printf("slot 57 over the library: %lu vertex records, %lu with every program translated, %lu pipelines created\n",
                vertex_records, translated_pairs, created);
    for (const auto& [hr, count] : results)
        std::printf("  0x%08lx: %lu records (%lu of them with every program translated)\n",
                    static_cast<unsigned long>(hr), count, translated_results[hr]);
    return 0;
}
