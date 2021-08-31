#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>

namespace VOG::Graphics
{
class GraphicsProvider;
}

namespace VOG::Graphics::Frame
{
class ThreadObjects
{
public:
    ThreadObjects(const GraphicsProvider& graphicsProvider);

    Vulkan::CommandBufferHandle getCommandBuffer(Vulkan::CommandBufferType type);

    void reset();

private:
    Vulkan::CommandBufferPool mCommandPool;
};
} // namespace VOG::Graphics::Frame
