#pragma once

#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <cstdint>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(GraphicsPipeline);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
class Mesh;
class Material;

/**
 * The graphics pipelines of the draws seen so far, one per vertex layout, shader program and color
 * format. Everything else about a pipeline is fixed. It never evicts: they live until the cache
 * does.
 */
class PipelineCache
{
public:
    explicit PipelineCache(Graphics::Vulkan::DevicePtr device);

    /**
     * Render thread only. Builds the pipeline on first use. The reference stays valid until the
     * next call: copy the pointer to keep it.
     */
    const Graphics::Vulkan::GraphicsPipelinePtr&
    get(const Mesh& mesh, const Material& material, vk::Format colorFormat);

private:
    /** Lets RendererTest count the entries. */
    friend struct RendererTestAccess;

    struct Entry
    {
        // The shader program is keyed by pipeline->program: the pipeline holds it, so its address
        // cannot be reused while the entry exists.
        std::uint32_t                         stride;
        std::vector<VertexAttribute>          attributes;
        vk::Format                            colorFormat;
        Graphics::Vulkan::GraphicsPipelinePtr pipeline;
    };

    Graphics::Vulkan::DevicePtr mDevice;

    // Linear find, switch to a map when there are many shaders.
    std::vector<Entry> mEntries;
};
} // namespace VOG::Engine
