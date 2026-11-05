#include "VOG/Engine/Renderer/ResourceFactory.hpp"

#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/ShaderDefinition.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"
#include "VOG/Engine/Renderer/UploadQueue.hpp"
#include "VOG/Engine/Renderer/UploadTicket.hpp"
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/HeapBuffer.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>
#include <VOG/Graphics/Vulkan/Texture.hpp>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace VOG::Engine
{
static_assert(gMaxVertexAttributes == Graphics::Vulkan::Limits::gMaxNumVertexInputs,
              "A vertex layout holds as many attributes as a pipeline takes inputs.");

ResourceFactory::ResourceFactory(Graphics::Vulkan::DevicePtr         device,
                                 Graphics::Vulkan::DescriptorHeapPtr heap,
                                 UploadQueue&                        uploads)
    : mDevice{std::move(device)}
    , mHeap{std::move(heap)}
    , mUploads{uploads}
{
}

std::uint32_t
ResourceFactory::internLayout(const VertexLayout& layout)
{
    const std::scoped_lock lock{mLayoutMutex};

    // The next id is the number of layouts seen, plus one: ids start at 1, so that 0 is none.
    return mLayoutIds.try_emplace(layout, static_cast<std::uint32_t>(mLayoutIds.size() + 1u))
        .first->second;
}

Mesh
ResourceFactory::createMesh(const MeshDescription& description)
{
    const VertexLayout& layout = description.layout;

    const std::string context =
        description.name.empty() ? std::string{} : std::format("Mesh \"{}\": ", description.name);
    const auto fail = [&context](const std::string& message)
    {
        return std::invalid_argument{context + message};
    };

    if (description.vertices.empty() || layout.stride == 0u ||
        description.vertices.size() % layout.stride != 0u)
    {
        throw fail(
            std::format("A mesh needs vertices, a non-zero stride and a size that is a multiple of "
                        "it; got {} bytes with stride {}.",
                        description.vertices.size(),
                        layout.stride));
    }
    if (layout.attributeCount > gMaxVertexAttributes)
    {
        throw fail(std::format("A mesh takes at most {} attributes; got {}.",
                               gMaxVertexAttributes,
                               layout.attributeCount));
    }
    const vk::PhysicalDeviceLimits& limits = mDevice->limits;

    constexpr std::uint32_t kComponentAlignment = 4u;
    if (layout.stride % kComponentAlignment != 0u ||
        layout.stride > limits.maxVertexInputBindingStride)
    {
        throw fail(std::format("A mesh stride must be a multiple of {} and at most {}; got {}.",
                               kComponentAlignment,
                               limits.maxVertexInputBindingStride,
                               layout.stride));
    }
    for (const VertexAttribute& attribute : layout.inputs())
    {
        if (attribute.offset % kComponentAlignment != 0u ||
            attribute.offset > limits.maxVertexInputAttributeOffset)
        {
            throw fail(std::format(
                "A vertex attribute offset must be a multiple of {} and at most {}; got {}.",
                kComponentAlignment,
                limits.maxVertexInputAttributeOffset,
                attribute.offset));
        }
        if (std::uint64_t{attribute.offset} + sizeOf(attribute.format) > layout.stride)
        {
            throw fail(std::format(
                "A vertex attribute at offset {} does not fit in the stride of {} bytes.",
                attribute.offset,
                layout.stride));
        }
    }

    const auto vertexCount =
        static_cast<std::uint32_t>(description.vertices.size() / layout.stride);
    if (description.indices.size() % 3u != 0u)
    {
        throw fail(std::format(
            "A mesh draws triangles, so its index count must be a multiple of 3; got {}.",
            description.indices.size()));
    }
    for (std::size_t i = 0u; i < description.indices.size(); ++i)
    {
        if (description.indices[i] >= vertexCount)
        {
            throw fail(std::format("Index {} is {}, but the mesh has {} vertices.",
                                   i,
                                   description.indices[i],
                                   vertexCount));
        }
    }

    // The indices follow the vertices. The stride is a multiple of 4, so they are aligned to the
    // size of an index.
    const std::size_t indexOffset = description.vertices.size_bytes();

    const std::size_t size = indexOffset + description.indices.size_bytes();

    // The staging buffer takes the bytes on this thread; the render thread copies them into the
    // buffer of the mesh, preferably device-local, which only the GPU reads.
    Graphics::Vulkan::BufferPtr staging =
        mDevice->createBuffer({.size = size, .usage = vk::BufferUsageFlagBits::eTransferSrc},
                              {.flags = Graphics::Vulkan::AllocationFlag::eHostSequentialWrite});
    {
        auto mapping = staging->mapForWrite();
        std::ranges::copy(description.vertices, mapping.bytes().begin());
        std::ranges::copy(std::as_bytes(description.indices),
                          mapping.bytes().subspan(indexOffset).begin());
    }

    Graphics::Vulkan::BufferPtr buffer = mDevice->createBuffer(
        {.size  = size,
         .usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
                  vk::BufferUsageFlagBits::eTransferDst},
        {});

    auto ticket      = std::make_shared<UploadTicket>();
    ticket->timeline = mUploads.timeline();

    // The job holds a second reference to the buffer until the copy is recorded, and the handle the
    // first for as long as it lives: a job that holds the last one is dead (see UploadQueue).
    // After the buffer exists, so that a frame which draws the mesh finds its upload.
    mUploads.push({.staging = std::move(staging), .destination = buffer, .ticket = ticket});

    return Mesh{std::make_shared<const Detail::MeshData>(
        Detail::MeshData{.name        = description.name,
                         .buffer      = std::move(buffer),
                         .vertexCount = vertexCount,
                         .layout      = layout,
                         .layoutId    = internLayout(layout),
                         .indexCount  = static_cast<std::uint32_t>(description.indices.size()),
                         .indexOffset = indexOffset,
                         .ticket      = std::move(ticket)})};
}

Texture
ResourceFactory::createTexture(const TextureDescription& description)
{
    if (description.width == 0u || description.height == 0u)
    {
        throw std::invalid_argument{std::format("A texture needs a non-zero extent; got {}x{}.",
                                                description.width,
                                                description.height)};
    }
    constexpr std::uint64_t kBytesPerPixel = 4u;
    const std::uint64_t     size =
        std::uint64_t{description.width} * description.height * kBytesPerPixel;
    if (description.pixels.size() != size)
    {
        throw std::invalid_argument{std::format("A {}x{} texture takes {} bytes of pixels; got {}.",
                                                description.width,
                                                description.height,
                                                size,
                                                description.pixels.size())};
    }

    // The texture first: it validates the extent against the device, and an extent that no image
    // can have must not reach the (possibly huge) staging allocation.
    Graphics::Vulkan::TexturePtr texture = mDevice->createTexture(
        mHeap, {description.width, description.height}, toVk(description.format));

    Graphics::Vulkan::BufferPtr staging =
        mDevice->createBuffer({.size = size, .usage = vk::BufferUsageFlagBits::eTransferSrc},
                              {.flags = Graphics::Vulkan::AllocationFlag::eHostSequentialWrite});
    {
        auto mapping = staging->mapForWrite();
        std::ranges::copy(description.pixels, mapping.bytes().begin());
    }

    auto ticket      = std::make_shared<UploadTicket>();
    ticket->timeline = mUploads.timeline();

    // After the texture exists, so that a frame which draws it finds its upload.
    mUploads.push({.staging = std::move(staging), .destination = texture, .ticket = ticket});

    return Texture{std::make_shared<const Detail::TextureData>(Detail::TextureData{
        .texture = std::move(texture), .filter = description.filter, .ticket = std::move(ticket)})};
}

Material
ResourceFactory::createMaterial(MaterialDescription description)
{
    const std::string context = description.name.empty()
                                  ? std::string{}
                                  : std::format("Material \"{}\": ", description.name);

    if (!description.shader)
    {
        throw std::invalid_argument{std::format(
            "{}A material needs a shader; get it from Renderer::getShader().", context)};
    }
    const std::shared_ptr<const Detail::ShaderInfo>& shader = description.shader.mData;

    const std::vector<std::byte> bytes =
        packMaterialBlock(*shader, description.parameters, context);

    // The parameters are written once, here: nothing writes the block after the material exists.
    Graphics::Vulkan::HeapBufferPtr block = mDevice->createHeapBuffer(mHeap, bytes.size());
    {
        auto mapping = block->mapForWrite();
        std::ranges::copy(bytes, mapping.bytes().begin());
    }

    std::vector<Texture> textures;
    for (const MaterialParameter& parameter : description.parameters)
    {
        if (const auto* texture = std::get_if<Texture>(&parameter.value))
        {
            textures.push_back(*texture);
        }
    }

    return Material{std::make_shared<const Detail::MaterialData>(
        Detail::MaterialData{.shader     = shader,
                             .block      = std::move(block),
                             .textures   = std::move(textures),
                             .parameters = std::move(description.parameters),
                             .state      = description.state})};
}
} // namespace VOG::Engine
