#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Resources/Attachment.hpp>
#include <VOG/Graphics/Utilities/Converters.hpp>

namespace VOG
{
template <>
vk::ImageUsageFlags inline ConvertTo<vk::ImageUsageFlags, Graphics::Resources::AttachmentUsage>(
    Graphics::Resources::AttachmentUsage usage)
{
    switch (usage)
    {
    case Graphics::Resources::AttachmentUsage::Color:
        return vk::ImageUsageFlagBits::eColorAttachment;
    case Graphics::Resources::AttachmentUsage::Depth:
        return vk::ImageUsageFlagBits::eDepthStencilAttachment;
    case Graphics::Resources::AttachmentUsage::Stencil:
        return vk::ImageUsageFlagBits::eDepthStencilAttachment;
    case Graphics::Resources::AttachmentUsage::DepthStencil:
        return vk::ImageUsageFlagBits::eDepthStencilAttachment;
    case Graphics::Resources::AttachmentUsage::Sampled:
        return vk::ImageUsageFlagBits::eSampled;
    }

    return vk::ImageUsageFlagBits::eStorage;
}

template <>
vk::ImageAspectFlags inline ConvertTo<vk::ImageAspectFlags, Graphics::Resources::AttachmentUsage>(
    Graphics::Resources::AttachmentUsage usage)
{
    switch (usage)
    {
    case Graphics::Resources::AttachmentUsage::Color:
        return vk::ImageAspectFlagBits::eColor;
    case Graphics::Resources::AttachmentUsage::Depth:
        return vk::ImageAspectFlagBits::eDepth;
    case Graphics::Resources::AttachmentUsage::Stencil:
        return vk::ImageAspectFlagBits::eStencil;
    case Graphics::Resources::AttachmentUsage::DepthStencil:
        return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    case Graphics::Resources::AttachmentUsage::Sampled:
        return vk::ImageAspectFlagBits::eColor;
    }

    return vk::ImageAspectFlagBits::eMetadata;
}

template <>
vk::Format inline ConvertTo<vk::Format, Graphics::Resources::AttachmentFormat>(
    Graphics::Resources::AttachmentFormat format)
{
    return static_cast<vk::Format>(format);
}

template <>
vk::Extent2D inline ConvertTo<vk::Extent2D, Graphics::Resources::AttachmentExtent>(
    Graphics::Resources::AttachmentExtent extent)
{
    vk::Extent2D converted;
    converted.width = extent.width;
    converted.height = extent.height;
    return converted;
}

template <>
vk::Extent3D inline ConvertTo<vk::Extent3D, Graphics::Resources::AttachmentExtent>(
    Graphics::Resources::AttachmentExtent extent)
{
    vk::Extent3D converted;
    converted.width = extent.width;
    converted.height = extent.height;
    converted.depth = extent.depth;
    return converted;
}

template <>
vk::SampleCountFlagBits inline ConvertTo<vk::SampleCountFlagBits, Graphics::Resources::SampleCount>(
    Graphics::Resources::SampleCount sampleCount)
{
    switch (sampleCount)
    {
    case Graphics::Resources::SampleCount::e1:
        return vk::SampleCountFlagBits::e1;
    case Graphics::Resources::SampleCount::e2:
        return vk::SampleCountFlagBits::e2;
    case Graphics::Resources::SampleCount::e4:
        return vk::SampleCountFlagBits::e4;
    case Graphics::Resources::SampleCount::e8:
        return vk::SampleCountFlagBits::e8;
    case Graphics::Resources::SampleCount::e16:
        return vk::SampleCountFlagBits::e16;
    }

    return vk::SampleCountFlagBits::e1;
}
} // namespace VOG
