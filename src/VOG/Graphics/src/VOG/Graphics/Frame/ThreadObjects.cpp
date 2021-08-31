#include "VOG/Graphics/Frame/ThreadObjects.hpp"

#include <vector>

namespace VOG::Graphics::Frame
{
ThreadObjects::ThreadObjects(const GraphicsProvider& graphicsProvider)
    : mCommandPool{graphicsProvider}
{
}

Vulkan::CommandBufferHandle
ThreadObjects::getCommandBuffer(Vulkan::CommandBufferType type)
{
    return mCommandPool.get(type);
}

void
ThreadObjects::reset()
{
    mCommandPool.reset();
}
} // namespace VOG::Graphics::Frame
