#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Buffer);
VOG_DECLARE_PTR(GraphicsPipeline);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
/**
 * A single draw described as data: what to bind and what to push.
 * Renderables produce these, the Renderer is the only place that records them.
 */
struct RenderItem
{
    static constexpr std::size_t kMaxPushConstantsSize = 128u;

    using PushConstantsBlob = Graphics::Vulkan::StaticVector<std::byte, kMaxPushConstantsSize>;

    Graphics::Vulkan::GraphicsPipelinePtr pipeline;
    Graphics::Vulkan::BufferPtr           vertexBuffer;
    std::uint32_t                         vertexCount         = 0u;
    vk::ShaderStageFlags                  pushConstantsStages = vk::ShaderStageFlagBits::eVertex;
    PushConstantsBlob                     pushConstants;

    /** Copies @p value into the push-constant blob at offset zero. */
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void
    setPushConstants(const T& value)
    {
        static_assert(sizeof(T) <= kMaxPushConstantsSize, "Push constant blob is too small.");

        const auto* bytes = reinterpret_cast<const std::byte*>(&value);
        pushConstants.assign(bytes, bytes + sizeof(T));
    }
};
} // namespace VOG::Engine
