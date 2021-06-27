#include "VOG/Graphics/Resources/RenderBuffer.hpp"

#include <VOG/Graphics/Api/GraphicsProvider.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Resources/Converters.hpp>
#include <VOG/Graphics/Resources/Utilities.hpp>

namespace VOG::Graphics::Resources
{
RenderBuffer::RenderBuffer(const std::shared_ptr<Api::GraphicsProvider>& graphicsProvider,
                           AttachmentUsage usage, AttachmentFormat desiredFormat,
                           AttachmentExtent extent, SampleCount sampleCount,
                           std::uint32_t mipLevels, std::uint32_t arrayLevels,
                           vk::ImageTiling imageTiling, vk::ImageLayout initialLayout,
                           vk::MemoryPropertyFlags memoryProperties)
    : Attachment{usage, desiredFormat, extent, sampleCount}
    , m_graphicsProvider{graphicsProvider}
{
    vk::ImageCreateInfo imageCreateInfo{};
    imageCreateInfo.setImageType(vk::ImageType::e2D)
        .setFormat(ConvertTo<vk::Format>(m_format))
        .setExtent(ConvertTo<vk::Extent3D>(m_extent))
        .setMipLevels(mipLevels)
        .setArrayLayers(arrayLevels)
        .setSamples(ConvertTo<vk::SampleCountFlagBits>(sampleCount))
        .setTiling(imageTiling)
        .setUsage(ConvertTo<vk::ImageUsageFlagBits>(usage))
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(initialLayout);

    m_image = std::make_shared<vk::raii::Image>(m_graphicsProvider->GetDevice(), imageCreateInfo);
    auto imageMemoryRequirements = m_image->getMemoryRequirements();

    std::uint32_t memoryTypeIndex =
        FindMemoryType(m_graphicsProvider->GetPhysicalDevice().getMemoryProperties(),
                       imageMemoryRequirements.memoryTypeBits, memoryProperties);
    vk::MemoryAllocateInfo memoryAllocateInfo(imageMemoryRequirements.size, memoryTypeIndex);
    m_memory = std::make_shared<vk::raii::DeviceMemory>(m_graphicsProvider->GetDevice(),
                                                        memoryAllocateInfo);

    m_image->bindMemory(**m_memory, 0);

    vk::ComponentMapping componentMapping(vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG,
                                          vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA);
    vk::ImageSubresourceRange imageSubresourceRange(ConvertTo<vk::ImageAspectFlags>(usage), 0, 1, 0,
                                                    1);
    vk::ImageViewCreateInfo imageViewCreateInfo({}, **m_image, vk::ImageViewType::e2D,
                                                ConvertTo<vk::Format>(m_format), componentMapping,
                                                imageSubresourceRange);
    m_imageView =
        std::make_shared<vk::raii::ImageView>(m_graphicsProvider->GetDevice(), imageViewCreateInfo);
}
const std::shared_ptr<vk::raii::ImageView>&
RenderBuffer::GetImageView() const
{
    return m_imageView;
}
} // namespace VOG::Graphics::Resources
