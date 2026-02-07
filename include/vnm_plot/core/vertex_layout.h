#pragma once

// VNM Plot Library - Vertex Layout Utilities
// Describes vertex attribute layouts and provides layout hashing helpers.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vnm::plot {

enum class Vertex_attrib_type
{
    FLOAT32,
    FLOAT64,
    INT32,
    UINT32
};

struct vertex_attribute_t
{
    int location = 0;
    Vertex_attrib_type type = Vertex_attrib_type::FLOAT32;
    int components = 1;
    std::size_t offset = 0;
    bool normalized = false;
};

struct Vertex_layout
{
    std::size_t stride = 0;
    std::vector<vertex_attribute_t> attributes;
};

uint64_t layout_key_for(const Vertex_layout& layout);
void setup_vertex_attributes_for_layout(const Vertex_layout& layout);

} // namespace vnm::plot
