#include "../src/slot57_input_layout.h"

#include <cstdio>
#include <initializer_list>
#include <string_view>

#define CHECK(condition) do {                                        \
    if (!(condition)) {                                              \
        std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
        return 1;                                                    \
    }                                                                \
} while (0)

namespace {

using Bytes = std::vector<std::uint8_t>;

void append_u32(Bytes& bytes, std::uint32_t word) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(word >> shift));
}

void append_element(Bytes& bytes, std::string_view name, std::initializer_list<std::uint32_t> fields) {
    append_u32(bytes, static_cast<std::uint32_t>(name.size() + 1));
    bytes.insert(bytes.end(), name.begin(), name.end());
    bytes.push_back(0);
    for (const auto value : fields)
        append_u32(bytes, value);
}

}  // namespace

int main() {
    using leonida::parse_slot57_input_layout;

    Bytes bytes(80, 0);
    const auto empty = parse_slot57_input_layout(bytes);
    CHECK(empty && empty->empty());
    CHECK(!parse_slot57_input_layout(std::span(bytes).first(76)));

    bytes[76] = 2;
    append_element(bytes, "ATTR", {3, 16, 2, 28, 0, 0});
    const auto second = bytes.size();
    append_element(bytes, "CUSTOM_0", {7, 41, 5, 0xffffffff, 1, 3});
    const auto last_field_end = bytes.size();
    bytes.resize((bytes.size() + 3) & ~std::size_t{3});

    const auto layout = parse_slot57_input_layout(bytes);
    CHECK(layout && layout->size() == 2);
    const auto& a = layout->front();
    const auto& b = layout->back();
    CHECK(a.semantic_name == "ATTR" && a.semantic_index == 3 && a.format == 16 &&
          a.input_slot == 2 && a.aligned_byte_offset == 28 && a.input_slot_class == 0 &&
          a.instance_data_step_rate == 0);
    CHECK(b.semantic_name == "CUSTOM_0" && b.semantic_index == 7 && b.format == 41 &&
          b.input_slot == 5 && b.aligned_byte_offset == 0xffffffff && b.input_slot_class == 1 &&
          b.instance_data_step_rate == 3);
    for (std::size_t cut = 0; cut < bytes.size(); ++cut)
        CHECK(!parse_slot57_input_layout(std::span(bytes).first(cut)));

    auto bad = bytes;
    bad[76] = 0xff;
    CHECK(!parse_slot57_input_layout(bad));
    bad = bytes;
    bad[84] = 0;
    CHECK(!parse_slot57_input_layout(bad));
    bad = bytes;
    bad[second + 4 + std::string_view("CUSTOM_0").size()] = 'X';
    CHECK(!parse_slot57_input_layout(bad));
    bad = bytes;
    bad[second + 3] = 0xff;
    CHECK(!parse_slot57_input_layout(bad));
    CHECK(last_field_end < bytes.size());
    bad = bytes;
    bad.back() = 1;
    CHECK(!parse_slot57_input_layout(bad));
    bad = bytes;
    append_u32(bad, 0);
    CHECK(!parse_slot57_input_layout(bad));
    return 0;
}
