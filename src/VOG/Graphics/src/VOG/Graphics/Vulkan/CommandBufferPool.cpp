#include "VOG/Graphics/Vulkan/CommandBufferPool.hpp"

#include <VOG/Common/Assert.hpp>

#include "VOG/Graphics/GraphicsProvider.hpp"

namespace VOG::Graphics::Vulkan
{
namespace
{
const std::uint32_t kGrowSize         = 10u;
const auto          kGraphicsPoolType = GraphicsProvider::CommandPoolType::eGraphics;
} // namespace

CommandBufferHandle::~CommandBufferHandle()
{
    if (mPool != nullptr)
    {
        mCommandBuffer.reset();
        mPool->returnToCache(std::move(mCommandBuffer));
    }
}

CommandBufferPool::CommandBufferPool(const GraphicsProvider& graphicsProvider)
    : mGraphicsProvider{graphicsProvider}
    , mVulkanPool{mGraphicsProvider.makeCommandPool(kGraphicsPoolType)}
{
}

CommandBufferHandle
CommandBufferPool::get(vk::CommandBufferLevel type)
{
    auto& pool = type == vk::CommandBufferLevel::ePrimary ? mPrimaryBuffers : mSecondaryBuffers;
    if (pool.empty())
    {
        auto newBuffers = Vulkan::CommandBuffer::create(
            mGraphicsProvider.getDevice(), mVulkanPool, type, kGrowSize);
        for (auto& buffer : newBuffers)
        {
            pool.push_back(std::move(buffer));
        }
    }

    Vulkan::CommandBuffer result = std::move(pool.back());
    pool.pop_back();

    return {this, std::move(result)};
}

void
CommandBufferPool::returnToCache(Vulkan::CommandBuffer commandBuffer)
{
    auto& pool = commandBuffer.getType() == vk::CommandBufferLevel::ePrimary ? mPrimaryBuffers
                                                                             : mSecondaryBuffers;
    pool.push_back(std::move(commandBuffer));
}

void
CommandBufferPool::reset()
{
    mVulkanPool.reset();
}
} // namespace VOG::Graphics::Vulkan
