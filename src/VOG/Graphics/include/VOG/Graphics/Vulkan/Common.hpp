#pragma once
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

namespace VOG::Graphics::Vulkan
{
struct VertexLayout
{
    constexpr static auto kMaxNVertexBuffers = Limits::gMaxNumVertexBuffers;
    constexpr static auto kMaxNVertexAttrs   = Limits::gMaxNumStageAttributes;

    StaticVector<vk::VertexInputBindingDescription, kMaxNVertexBuffers> bindingDescription;
    StaticVector<vk::VertexInputAttributeDescription, kMaxNVertexAttrs> attributeDescription;
};

struct RenderpassDescription
{
    using ColorFormats = StaticVector<vk::Format, Limits::gMaxNumAttachments - 1>;

    ColorFormats colorAttachmentFormats;
    vk::Format   depthAttachmentFormat   = vk::Format::eUndefined;
    vk::Format   stencilAttachmentFormat = vk::Format::eUndefined;

    auto operator<=>(const RenderpassDescription&) const = default;
};

bool isDepthFormat(vk::Format format);
bool isStencilFormat(vk::Format format);
} // namespace VOG::Graphics::Vulkan