#pragma once

#include <VOG/Common/Concepts.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>

#include <memory>
#include <ranges>
#include <unordered_set>
#include <vector>

namespace VOG::Graphics::Vulkan
{
using CommandBufferType = vk::CommandBufferLevel;

class CommandBufferState
{
public:
    enum class State
    {
        eEmpty,
        eRecording,
        eRecordingEnded,
        eSubmitted,
        // eFinishedExecution
    };

    State
    getState() const
    {
        return mState;
    }

protected:
    State mState = State::eEmpty;
};

class CommandBuffer : public vk::raii::CommandBuffer
{
    friend class CommandBufferPool;

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

    operator bool() const;

    void
    bindVertexBuffers(std::uint32_t                                              firstBinding,
                      StaticVectorStrict<BufferBinding, kMaxNumVertexBufferBind> bindings)
    {
        std::array<vk::Buffer, kMaxNumVertexBufferBind>     bufferHandles;
        std::array<vk::DeviceSize, kMaxNumVertexBufferBind> offsets;
        for (std::size_t i = 0; i < bindings.size(); ++i)
        {
            bufferHandles[i] = **bindings[i].buffer;
            offsets[i]       = bindings[i].offset;
            mBoundVertexBuffers.insert(std::move(bindings[i].buffer));
        }

        vk::raii::CommandBuffer::bindVertexBuffers(
            firstBinding,
            {static_cast<std::uint32_t>(bindings.size()), bufferHandles.data()},
            {static_cast<std::uint32_t>(bindings.size()), offsets.data()});
    }

    void addBoundResource(const std::shared_ptr<void>& resource);

    void reset();

public:
    const CommandBufferType type;

protected:
    std::vector<std::shared_ptr<void>>          mBoundResources;
    std::unordered_set<std::shared_ptr<Buffer>> mBoundVertexBuffers;
};
} // namespace VOG::Graphics::Vulkan
