#include "VOG/Graphics/Vulkan/CommandBuffer.hpp"

#include "VOG/Graphics/GraphicsProvider.hpp"

namespace VOG::Graphics::Vulkan
{
CommandBuffer::CommandBuffer(vk::raii::CommandBuffer commandBuffer, CommandBufferType type)
    : vk::raii::CommandBuffer{std::move(commandBuffer)}
    , mType{type}
    , mBoundResources{}
{
}

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

CommandBufferType
CommandBuffer::getType() const
{
    return mType;
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
}
} // namespace VOG::Graphics::Vulkan
