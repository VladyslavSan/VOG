#pragma once

#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Engine/Renderer/Texture.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(DescriptorHeap);
VOG_DECLARE_PTR(Device);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
class UploadQueue;

namespace Detail
{
class ShaderInfo;
struct MaterialMember;
} // namespace Detail

/**
 * Makes the meshes, textures and materials of a Renderer, from any thread: it checks what the
 * caller gave, makes the staging and the GPU memory, takes the heap slots, packs the parameters,
 * numbers the vertex layouts, and queues the copies in the UploadQueue. The allocator and the
 * heap lock inside, and the only state of its own, the table of layouts, has a mutex.
 */
class ResourceFactory
{
public:
    ResourceFactory(Graphics::Vulkan::DevicePtr         device,
                    Graphics::Vulkan::DescriptorHeapPtr heap,
                    UploadQueue&                        uploads);

    /** See Renderer::createMesh(). */
    [[nodiscard]] Mesh createMesh(const MeshDescription& description);

    /** See Renderer::createTexture(). */
    [[nodiscard]] Texture createTexture(const TextureDescription& description);

    /** See Renderer::createMaterial(). */
    [[nodiscard]] Material createMaterial(MaterialDescription description);

private:
    /** The bytes of the Material block of @p shader holding @p parameters, which are checked first;
     *  @p context starts an error message. See createMaterial(). */
    static std::vector<std::byte> packMaterialBlock(const Detail::ShaderInfo&          shader,
                                                    std::span<const MaterialParameter> parameters,
                                                    std::string_view                   context);

    /** Writes @p value at the offset of @p member in @p block. */
    static void writeMaterialValue(std::span<std::byte>          block,
                                   const Detail::MaterialMember& member,
                                   const MaterialValue&          value);

    /** The id of @p layout: the same for equal layouts, another for every other, from 1 up. */
    std::uint32_t internLayout(const VertexLayout& layout);

    Graphics::Vulkan::DevicePtr         mDevice;
    Graphics::Vulkan::DescriptorHeapPtr mHeap;
    UploadQueue&                        mUploads;

    std::atomic<std::uint32_t> mLastMeshId     = 0u;
    std::atomic<std::uint32_t> mLastMaterialId = 0u;

    std::mutex                            mLayoutMutex;
    std::map<VertexLayout, std::uint32_t> mLayoutIds;
};
} // namespace VOG::Engine
