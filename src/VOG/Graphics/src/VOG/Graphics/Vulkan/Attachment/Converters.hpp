#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>

namespace VOG::Graphics::Vulkan
{
vk::ImageUsageFlags
toUsageFlags(AttachmentUsage usage)
{
    switch (usage)
    {
    case AttachmentUsage::eColor:
        return vk::ImageUsageFlagBits::eColorAttachment;
    case AttachmentUsage::eDepth:
        return vk::ImageUsageFlagBits::eDepthStencilAttachment;
    case AttachmentUsage::eStencil:
        return vk::ImageUsageFlagBits::eDepthStencilAttachment;
    case AttachmentUsage::eDepthStencil:
        return vk::ImageUsageFlagBits::eDepthStencilAttachment;
    case AttachmentUsage::eSampled:
        return vk::ImageUsageFlagBits::eSampled;
    }

    return vk::ImageUsageFlagBits::eStorage;
}

vk::ImageAspectFlags
toAspectFlags(AttachmentUsage usage)
{
    switch (usage)
    {
    case AttachmentUsage::eColor:
        return vk::ImageAspectFlagBits::eColor;
    case AttachmentUsage::eDepth:
        return vk::ImageAspectFlagBits::eDepth;
    case AttachmentUsage::eStencil:
        return vk::ImageAspectFlagBits::eStencil;
    case AttachmentUsage::eDepthStencil:
        return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    case AttachmentUsage::eSampled:
        return vk::ImageAspectFlagBits::eColor;
    }

    return vk::ImageAspectFlagBits::eMetadata;
}
} // namespace VOG::Graphics::Vulkan
