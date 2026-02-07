#include <vnm_plot/core/vertex_layout.h>

#include <glatter/glatter.h>

#include <cstdint>

namespace vnm::plot {
namespace {

constexpr std::uint64_t k_fnv_offset_basis = 1469598103934665603ULL;
constexpr std::uint64_t k_fnv_prime = 1099511628211ULL;

std::uint64_t hash_bytes(std::uint64_t h, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        h ^= static_cast<std::uint64_t>(bytes[i]);
        h *= k_fnv_prime;
    }
    return h;
}

void* offset_ptr(std::size_t offset)
{
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(offset));
}

} // anonymous namespace

uint64_t layout_key_for(const Vertex_layout& layout)
{
    std::uint64_t h = k_fnv_offset_basis;
    h = hash_bytes(h, &layout.stride, sizeof(layout.stride));
    const std::size_t count = layout.attributes.size();
    h = hash_bytes(h, &count, sizeof(count));
    for (const auto& attr : layout.attributes) {
        h = hash_bytes(h, &attr.location, sizeof(attr.location));
        h = hash_bytes(h, &attr.type, sizeof(attr.type));
        h = hash_bytes(h, &attr.components, sizeof(attr.components));
        h = hash_bytes(h, &attr.offset, sizeof(attr.offset));
        h = hash_bytes(h, &attr.normalized, sizeof(attr.normalized));
    }
    if (h == 0) {
        h = 1;
    }
    return h;
}

void setup_vertex_attributes_for_layout(const Vertex_layout& layout)
{
    const GLsizei stride = static_cast<GLsizei>(layout.stride);
    for (const auto& attr : layout.attributes) {
        const GLint components = static_cast<GLint>(attr.components);
        const auto offset = offset_ptr(attr.offset);

        switch (attr.type) {
            case Vertex_attrib_type::FLOAT64:
                glVertexAttribLPointer(attr.location, components, GL_DOUBLE, stride, offset);
                glEnableVertexAttribArray(attr.location);
                break;
            case Vertex_attrib_type::FLOAT32:
                glVertexAttribPointer(attr.location, components, GL_FLOAT,
                    attr.normalized ? GL_TRUE : GL_FALSE, stride, offset);
                glEnableVertexAttribArray(attr.location);
                break;
            case Vertex_attrib_type::INT32:
                glVertexAttribIPointer(attr.location, components, GL_INT, stride, offset);
                glEnableVertexAttribArray(attr.location);
                break;
            case Vertex_attrib_type::UINT32:
                glVertexAttribIPointer(attr.location, components, GL_UNSIGNED_INT, stride, offset);
                glEnableVertexAttribArray(attr.location);
                break;
            default:
                break;
        }
    }
}

} // namespace vnm::plot
