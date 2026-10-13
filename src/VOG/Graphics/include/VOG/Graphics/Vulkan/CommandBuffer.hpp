#pragma once

#include <VOG/Common/Concepts.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>

#include <memory>
#include <unordered_set>
#include <vector>

namespace VOG::Graphics::Vulkan
{
using CommandBufferType = vk::CommandBufferLevel;

/**
 * Tracked Vulkan command buffer. Inherits vk::raii::CommandBuffer privately so recording
 * that needs retention goes through CommandBufferRecorder (or the few public begin/end APIs).
 */
class CommandBuffer : private vk::raii::CommandBuffer
{
    friend class CommandBufferPool;
    friend class CommandBufferRecorder;
    friend class CommandBufferHandle;

    CommandBuffer(vk::raii::CommandBuffer commandBuffer, CommandBufferType type);

public:
    constexpr static std::size_t kMaxNumVertexBufferBind = 8u;

    struct BufferBinding
    {
        std::shared_ptr<Buffer> buffer;
        const vk::DeviceSize    offset;
    };

    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&&)      = default;

    explicit operator bool() const;

    void begin(const vk::CommandBufferBeginInfo& beginInfo);
    void end();

    /** Raw Vulkan handle for submit; does not bypass retention on recording paths. */
    vk::CommandBuffer vkHandle() const;

    void addBoundResource(const std::shared_ptr<void>& resource);

    void reset();

    const CommandBufferType type;

protected:
    std::vector<std::shared_ptr<void>>          mBoundResources;
    std::unordered_set<std::shared_ptr<Buffer>> mBoundVertexBuffers;
};
} // namespace VOG::Graphics::Vulkan
