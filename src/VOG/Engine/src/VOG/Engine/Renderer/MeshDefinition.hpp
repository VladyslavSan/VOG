#pragma once

#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Buffer);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
struct UploadTicket;

namespace Detail
{
class MeshData
{
public:
    /** For error messages. */
    std::string name;

    /** Vertices at offset 0, then the indices at indexOffset. */
    Graphics::Vulkan::BufferPtr buffer;
    std::uint32_t               vertexCount = 0u;
    VertexLayout                layout;
    /** The same for every mesh of one layout, and different for every other: it keys the tables
     *  of the renderer that hold one entry per layout. Made by the renderer, from 1 up. */
    std::uint32_t layoutId = 0u;
    /** Zero draws the vertices in order. */
    std::uint32_t  indexCount  = 0u;
    vk::DeviceSize indexOffset = 0u;

    /** Whether the copy of the buffer has finished; see UploadTicket. */
    std::shared_ptr<UploadTicket> ticket;
};
} // namespace Detail

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

/** How a shader declares the type: "float2" for eFloat2. */
constexpr std::string_view
describe(VertexFormat format)
{
    switch (format)
    {
    case VertexFormat::eFloat2:
        return "float2";
    case VertexFormat::eFloat4:
        return "float4";
    }

    return "unknown";
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
