#include "VOG/Graphics/Vulkan/CommandBuffer.hpp"

namespace VOG::Graphics::Vulkan
{
std::vector<CommandBuffer>
CommandBuffer::create(const vk::raii::Device&      device,
                      const vk::raii::CommandPool& commandPool,
                      CommandBufferType            level,
                      std::uint32_t                count)
{
    vk::raii::CommandBuffers commandBuffers{
        device, {.commandPool = *commandPool, .level = level, .commandBufferCount = count}};
    std::vector<CommandBuffer> result{};
    result.reserve(count);
    for (auto& commandBuffer : commandBuffers)
    {
        result.emplace_back(std::move(commandBuffer), level);
    }

    return result;
}

CommandBuffer::CommandBuffer(vk::raii::CommandBuffer commandBuffer, CommandBufferType _type)
    : vk::raii::CommandBuffer{std::move(commandBuffer)}
    , type{_type}
{
}

CommandBuffer::
operator bool() const
{
    return static_cast<bool>(**this);
}

void
CommandBuffer::addBoundResource(const std::shared_ptr<void>& resource)
{
    mBoundResources.push_back(resource);
}

void
CommandBuffer::reset()
{
    mBoundResources.clear();
    mBoundVertexBuffers.clear();
}
} // namespace VOG::Graphics::Vulkan
