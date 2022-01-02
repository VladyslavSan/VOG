#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/CommandBuffer.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

namespace VOG::Graphics
{
class GraphicsProvider;
}

namespace VOG::Graphics::Vulkan
{
class CommandBufferPool;

class CommandBufferHandle
{
public:
    inline CommandBufferHandle(CommandBufferPool* pool, CommandBuffer commandBuffer) noexcept
        : mPool{pool}
        , mCommandBuffer{std::move(commandBuffer)}
    {
    }

    inline CommandBufferHandle(CommandBufferHandle&& handle) noexcept
        : mPool{handle.mPool}
        , mCommandBuffer{std::move(handle.mCommandBuffer)}
    {
        handle.mPool = nullptr;
    }

    ~CommandBufferHandle();

    inline CommandBuffer*
    operator*() noexcept
    {
        return std::addressof(mCommandBuffer);
    }

    inline CommandBuffer*
    operator->() noexcept
    {
        return std::addressof(mCommandBuffer);
    }

protected:
    CommandBufferPool* mPool;
    CommandBuffer      mCommandBuffer;
};

/**
 * @class CommandBufferPool
 */
class CommandBufferPool
{
public:
    CommandBufferPool(const GraphicsProvider& graphicsProvider);

    /**
     * Retrieves clean command buffer from cache or allocates new if cache is exhausted.
     *
     * @param type Type of the command buffer to return - primary or secondary.
     *
     * @returns RenderCommandRecorder instance
     */
    CommandBufferHandle get(CommandBufferType type);

    void returnToCache(CommandBuffer commandBuffer);

    void reset();

protected:
    const GraphicsProvider& mGraphicsProvider;
    vk::raii::CommandPool   mVulkanPool;

    std::vector<CommandBuffer> mPrimaryBuffers;
    std::vector<CommandBuffer> mSecondaryBuffers;
};
} // namespace VOG::Graphics::Vulkan
