#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <compare>
#include <cstdint>
#include <deque>
#include <map>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(GraphicsPipeline);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
namespace Detail
{
class MeshData;
class MaterialData;
} // namespace Detail

/**
 * The graphics pipelines of the draws seen so far, one per shader program, vertex layout and color
 * format. Everything else about a pipeline is fixed. A pipeline is found by the value of its key,
 * never by the address of an object, and it gets an index that does not change: the draws of a
 * frame refer to their pipelines by it. The table never evicts; the pipelines live until the table
 * does.
 */
class PipelineTable
{
public:
    struct Key
    {
        std::uint32_t programId;
        std::uint32_t layoutId;
        vk::Format    colorFormat;

        auto operator<=>(const Key&) const = default;
    };

    explicit PipelineTable(Graphics::Vulkan::DevicePtr device);

    /**
     * Render thread only. The index of the pipeline for @p mesh drawn with @p material into a
     * target of @p colorFormat; builds the pipeline at the first call for the key.
     * @throws std::invalid_argument if the mesh cannot feed the shader. Draws that went through
     *         RenderQueue::submit() have passed that check already.
     */
    [[nodiscard]] std::uint32_t resolve(const Detail::MeshData&     mesh,
                                        const Detail::MaterialData& material,
                                        vk::Format                  colorFormat);

    /** The pipeline of an index that resolve() returned. The reference is valid for the life of
     *  the table: no call changes or moves a pipeline. */
    [[nodiscard]] const Graphics::Vulkan::GraphicsPipelinePtr& at(std::uint32_t index) const;

private:
    /** Lets the tests count the pipelines. */
    friend struct PipelineTableTestAccess;
    friend struct RendererTestAccess;

    Graphics::Vulkan::DevicePtr mDevice;

    std::map<Key, std::uint32_t>                      mIndices;
    std::deque<Graphics::Vulkan::GraphicsPipelinePtr> mPipelines;
};
} // namespace VOG::Engine
