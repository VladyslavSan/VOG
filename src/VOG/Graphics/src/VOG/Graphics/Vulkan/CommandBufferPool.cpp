#include "VOG/Graphics/Vulkan/CommandBufferPool.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/InDestructor.hpp>

#include <limits>

namespace VOG::Graphics::Vulkan
{
namespace
{
constexpr std::uint32_t gGrowSize = 10u;
} // namespace

ExecutableCommandBuffer::ExecutableCommandBuffer(CommandBufferPoolPtr           pool,
                                                 std::unique_ptr<CommandBuffer> commandBuffer)
    : mPool{std::move(pool)}
    , mCommandBuffer{std::move(commandBuffer)}
{
}

ExecutableCommandBuffer::~ExecutableCommandBuffer() noexcept
{
    if (mCommandBuffer)
    {
        mPool->discard(std::move(mCommandBuffer));
    }
}

CommandBufferPool::CommandBufferPool(const DevicePtr& device, std::uint32_t familyIndex)
    : mDevice{device}
    , mFamilyIndex{familyIndex}
    , mVulkanPool{mDevice->raii(), vk::CommandPoolCreateInfo{.queueFamilyIndex = mFamilyIndex}}
{
}

CommandBufferPool::~CommandBufferPool() noexcept
{
    for (Submitted& submitted : mSubmitted)
    {
        inDestructor("Waiting for a submitted command buffer",
                     [&submitted]
                     {
                         [[maybe_unused]] const vk::Result result =
                             submitted.fence->wait(std::numeric_limits<std::uint64_t>::max());
                     });
    }
}

CommandBufferRecorder
CommandBufferPool::record()
{
    // Everything that can throw comes before the buffer leaves mFree. The lease is counted by the
    // recorder once the buffer began recording, so a throw never leaks one.
    CommandBufferPoolPtr self = shared_from_this();
    if (mFree.empty())
    {
        grow();
    }

    std::unique_ptr<CommandBuffer> commandBuffer = std::move(mFree.back());
    mFree.pop_back();

    return CommandBufferRecorder{std::move(self), std::move(commandBuffer)};
}

void
CommandBufferPool::grow()
{
    // Reserve first: every vector can hold all the buffers that exist, so putting a buffer back
    // never allocates. mAllocated counts the buffers as they are made, so a failure in the middle
    // leaves it equal to the real number, which is what the reserve above is sized for.
    mFree.reserve(mAllocated + gGrowSize);
    mDirty.reserve(mAllocated + gGrowSize);
    mSubmitted.reserve(mAllocated + gGrowSize);

    vk::raii::CommandBuffers rawBuffers{mDevice->raii(),
                                        {.commandPool        = *mVulkanPool,
                                         .level              = vk::CommandBufferLevel::ePrimary,
                                         .commandBufferCount = gGrowSize}};
    for (auto& buffer : rawBuffers)
    {
        mFree.push_back(std::unique_ptr<CommandBuffer>(new CommandBuffer{std::move(buffer)}));
        ++mAllocated;
    }
}

// mDirty is reserved for every buffer the pool allocated, so the push_back cannot allocate.
void
// NOLINTNEXTLINE(bugprone-exception-escape)
CommandBufferPool::discard(std::unique_ptr<CommandBuffer> commandBuffer) noexcept
{
    abandon(std::move(commandBuffer));
    --mLeased;
}

void
// NOLINTNEXTLINE(bugprone-exception-escape)
CommandBufferPool::abandon(std::unique_ptr<CommandBuffer> commandBuffer) noexcept
{
    commandBuffer->releaseRetained();
    mDirty.push_back(std::move(commandBuffer));
}

void
CommandBufferPool::park(std::unique_ptr<CommandBuffer> commandBuffer,
                        FencePool::FenceHandlePtr      fence)
{
    mSubmitted.push_back(
        Submitted{.commandBuffer = std::move(commandBuffer), .fence = std::move(fence)});
    --mLeased;
}

void
CommandBufferPool::reset()
{
    VOG_ASSERT_MSG(mLeased == 0u, "A recorder or an executable command buffer outlived reset().");

    for (Submitted& submitted : mSubmitted)
    {
        // Throws on a failed wait, so there is no result to check.
        [[maybe_unused]] const vk::Result result =
            submitted.fence->wait(std::numeric_limits<std::uint64_t>::max());
    }

    mVulkanPool.reset();

    // Nothing below allocates: mFree is reserved for every buffer the pool made.
    for (Submitted& submitted : mSubmitted)
    {
        submitted.commandBuffer->releaseRetained();
        mFree.push_back(std::move(submitted.commandBuffer));
    }
    mSubmitted.clear();

    for (std::unique_ptr<CommandBuffer>& commandBuffer : mDirty)
    {
        mFree.push_back(std::move(commandBuffer));
    }
    mDirty.clear();
}
} // namespace VOG::Graphics::Vulkan
