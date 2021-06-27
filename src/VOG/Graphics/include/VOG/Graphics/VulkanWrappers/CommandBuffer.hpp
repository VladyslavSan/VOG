#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <vector>

namespace VOG::Graphics::VulkanWrappers
{
class CommandBuffer
{
public:
    CommandBuffer(vk::raii::CommandBuffer commandBuffer, bool freeOnDestroy = false)
        : mCommandBuffer{std::move(commandBuffer)}
        , mFreeOnDestroy{freeOnDestroy}
    {
    }

    CommandBuffer(CommandBuffer&&) = default;

    ~CommandBuffer()
    {
        // Hack to not explicitly destroy the command buffer
        if (!mFreeOnDestroy)
        {
            const_cast<vk::CommandBuffer&>(*mCommandBuffer) = vk::CommandBuffer{};
        }
    }

    void
    AddBoundResource(std::shared_ptr<void> resource)
    {
        mBoundResources.push_back(resource);
    }

    const vk::CommandBuffer& operator*() const { return *mCommandBuffer; }

    inline void
    begin(const vk::CommandBufferBeginInfo& beginInfo)
    {
        mCommandBuffer.begin(beginInfo);
    }
    inline void
    beginRenderPass(const vk::RenderPassBeginInfo& beginInfo,
                    vk::SubpassContents subpassContents = vk::SubpassContents::eInline)
    {
        mCommandBuffer.beginRenderPass(beginInfo, subpassContents);
    }
    inline void
    endRenderPass()
    {
        mCommandBuffer.endRenderPass();
    }
    inline void
    end()
    {
        mCommandBuffer.end();
    }

protected:
    vk::raii::CommandBuffer mCommandBuffer;
    const bool mFreeOnDestroy;

    std::vector<std::shared_ptr<void>> mBoundResources;
};
} // namespace VOG::Graphics::VulkanWrappers