#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>

#include <memory>
#include <unordered_set>

namespace VOG::Graphics::Vulkan
{
/**
 * Vulkan command buffer that keeps everything recorded into it alive. Inherits
 * vk::raii::CommandBuffer privately: recording goes through CommandBufferRecorder, which retains
 * what it records, and submission goes through Queue.
 *
 * Never copied or moved: the pool, the recorder and the executable command buffer pass it around
 * as unique_ptr<CommandBuffer>.
 */
class CommandBuffer : private vk::raii::CommandBuffer
{
    friend class CommandBufferPool;
    friend class CommandBufferRecorder;
    friend class Queue;

    explicit CommandBuffer(vk::raii::CommandBuffer commandBuffer);

public:
    constexpr static std::size_t kMaxNumVertexBufferBind = 8u;

    struct BufferBinding
    {
        std::shared_ptr<Buffer> buffer;
        const vk::DeviceSize    offset;
    };

    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&&)      = delete;

private:
    void begin(const vk::CommandBufferBeginInfo& beginInfo);
    void end();

    /** Raw Vulkan handle for submit. */
    vk::CommandBuffer vkHandle() const;

    /** Keeps @p resource alive until releaseRetained(); the one entry point for retention. */
    void retain(std::shared_ptr<const void> resource);

    /** Drops everything retained; call once the GPU is done with the command buffer. */
    void releaseRetained() noexcept;

    /** A set: binding the same pipeline 1000 times holds one reference. */
    std::unordered_set<std::shared_ptr<const void>> mRetained;
};
} // namespace VOG::Graphics::Vulkan
