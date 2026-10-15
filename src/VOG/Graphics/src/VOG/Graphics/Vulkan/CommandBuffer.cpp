#include "VOG/Graphics/Vulkan/CommandBuffer.hpp"

namespace VOG::Graphics::Vulkan
{
CommandBuffer::CommandBuffer(vk::raii::CommandBuffer commandBuffer)
    : vk::raii::CommandBuffer{std::move(commandBuffer)}
{
}

void
CommandBuffer::begin(const vk::CommandBufferBeginInfo& beginInfo)
{
    vk::raii::CommandBuffer::begin(beginInfo);
}

void
CommandBuffer::end()
{
    vk::raii::CommandBuffer::end();
}

vk::CommandBuffer
CommandBuffer::vkHandle() const
{
    return *static_cast<const vk::raii::CommandBuffer&>(*this);
}

void
CommandBuffer::retain(std::shared_ptr<const void> resource)
{
    mRetained.insert(std::move(resource));
}

void
CommandBuffer::releaseRetained() noexcept
{
    mRetained.clear();
}
} // namespace VOG::Graphics::Vulkan
