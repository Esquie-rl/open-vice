#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace leonida {

struct Slot57InputElement {
    std::string semantic_name;
    std::uint32_t semantic_index;
    std::uint32_t format;
    std::uint32_t input_slot;
    std::uint32_t aligned_byte_offset;
    std::uint32_t input_slot_class;
    std::uint32_t instance_data_step_rate;
};

namespace slot57_input_layout {

inline constexpr std::size_t header_bytes = 80;
inline constexpr std::size_t element_count_offset = 76;
inline constexpr std::size_t element_fields_bytes = 24;

inline std::uint32_t read_u32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) |
           (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
}

}  // namespace slot57_input_layout

// Decode the vertex input elements in slot 57's second blob. The preceding pipeline fields are opaque here.
// Return an engaged empty vector for a layout with zero elements. Enum values stay raw for PSO validation.
inline std::optional<std::vector<Slot57InputElement>>
parse_slot57_input_layout(std::span<const std::uint8_t> bytes) {
    using namespace slot57_input_layout;
    if (bytes.size() < header_bytes || bytes.size() % 4 != 0)
        return std::nullopt;

    const auto count = read_u32(bytes.data() + element_count_offset);
    auto data = bytes.subspan(header_bytes);
    constexpr std::size_t min_element_bytes = 4 + 2 + element_fields_bytes;
    if (count > data.size() / min_element_bytes)
        return std::nullopt;

    std::vector<Slot57InputElement> elements;
    elements.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (data.size() < min_element_bytes)
            return std::nullopt;
        const auto name_bytes = read_u32(data.data());
        data = data.subspan(4);
        if (name_bytes < 2 || name_bytes > data.size() - element_fields_bytes)
            return std::nullopt;

        const auto name = data.first(name_bytes);
        const auto text = name.first(name.size() - 1);
        if (name.back() != 0 || std::find(text.begin(), text.end(), 0) != text.end())
            return std::nullopt;

        data = data.subspan(name_bytes);
        const auto* fields = data.data();
        elements.push_back({
            std::string(reinterpret_cast<const char*>(text.data()), text.size()),
            read_u32(fields), read_u32(fields + 4), read_u32(fields + 8),
            read_u32(fields + 12), read_u32(fields + 16), read_u32(fields + 20),
        });
        data = data.subspan(element_fields_bytes);
    }
    if (data.size() > 3 || std::any_of(data.begin(), data.end(), [](auto byte) { return byte != 0; }))
        return std::nullopt;
    return elements;
}

}  // namespace leonida
