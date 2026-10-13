#include "VOG/Graphics/Vulkan/CommandBuffer.hpp"

namespace VOG::Graphics::Vulkan
{
CommandBuffer::CommandBuffer(vk::raii::CommandBuffer commandBuffer, CommandBufferType _type)
    : vk::raii::CommandBuffer{std::move(commandBuffer)}
    , type{_type}
{
}

CommandBuffer::
operator bool() const
{
    return static_cast<bool>(vkHandle());
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
