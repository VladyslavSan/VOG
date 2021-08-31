#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <memory>
#include <vector>

namespace VOG::Graphics::Vulkan
{
using CommandBufferType = vk::CommandBufferLevel;
class CommandBuffer : public vk::raii::CommandBuffer
{
public:
    static std::vector<CommandBuffer> create(const vk::raii::Device&      device,
                                             const vk::raii::CommandPool& commandPool,
                                             CommandBufferType            level,
                                             std::uint32_t                count);

    CommandBuffer(vk::raii::CommandBuffer commandBuffer, CommandBufferType type);

    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&&)      = default;

    CommandBufferType getType() const;

    void addBoundResource(const std::shared_ptr<void>& resource);

    void reset();

protected:
    const CommandBufferType mType;

    std::vector<std::shared_ptr<void>> mBoundResources;
};
} // namespace VOG::Graphics::Vulkan
