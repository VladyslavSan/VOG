#pragma once

#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <cstdint>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Buffer);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
class Mesh
{
public:
    /** Vertices at offset 0, then the indices at indexOffset. */
    Graphics::Vulkan::BufferPtr  buffer;
    std::uint32_t                vertexCount = 0u;
    std::uint32_t                stride      = 0u;
    std::vector<VertexAttribute> attributes;
    /** Zero draws the vertices in order. */
    std::uint32_t  indexCount  = 0u;
    vk::DeviceSize indexOffset = 0u;
};

/** Size of one attribute in bytes. */
constexpr std::uint32_t
sizeOf(VertexFormat format)
{
    switch (format)
    {
    case VertexFormat::eFloat2:
        return 2u * sizeof(float);
    case VertexFormat::eFloat4:
        return 4u * sizeof(float);
    }

    return 0u;
}

constexpr vk::Format
toVk(VertexFormat format)
{
    switch (format)
    {
    case VertexFormat::eFloat2:
        return vk::Format::eR32G32Sfloat;
    case VertexFormat::eFloat4:
        return vk::Format::eR32G32B32A32Sfloat;
    }

    return vk::Format::eUndefined;
}
} // namespace VOG::Engine
