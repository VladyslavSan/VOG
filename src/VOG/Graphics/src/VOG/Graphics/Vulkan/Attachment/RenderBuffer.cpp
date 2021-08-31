#include "VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp"

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/GraphicsProvider.hpp>

#include "VOG/Graphics/Vulkan/Attachment/Converters.hpp"
#include "VOG/Graphics/Vulkan/Utilities.hpp"

namespace VOG::Graphics::Vulkan
{
RenderBuffer::RenderBuffer(const std::shared_ptr<GraphicsProvider>& graphicsProvider,
                           AttachmentUsage                          usage,
                           vk::Format                               desiredFormat,
                           vk::Extent2D                             extent,
                           SampleCount                              sampleCount,
                           std::uint32_t                            mipLevels,
                           std::uint32_t                            arrayLevels,
                           vk::ImageTiling                          imageTiling,
                           vk::ImageLayout                          initialLayout,
                           vk::MemoryPropertyFlags                  memoryProperties)
    : AttachmentInterface{usage, desiredFormat, {extent.width, extent.height, 1u}, sampleCount}
    , mGraphicsProvider{graphicsProvider}
{
    vk::ImageCreateInfo imageCreateInfo{};
    imageCreateInfo.setImageType(vk::ImageType::e2D)
        .setFormat(mFormat)
        .setExtent(mExtent)
        .setMipLevels(mipLevels)
        .setArrayLayers(arrayLevels)
        .setSamples(sampleCount)
        .setTiling(imageTiling)
        .setUsage(toUsageFlags(usage))
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(initialLayout);

    mImage = std::make_shared<vk::raii::Image>(mGraphicsProvider->getDevice(), imageCreateInfo);
    auto imageMemoryRequirements = mImage->getMemoryRequirements();

    std::uint32_t memoryTypeIndex =
        FindMemoryType(mGraphicsProvider->getPhysicalDevice().getMemoryProperties(),
                       imageMemoryRequirements.memoryTypeBits,
                       memoryProperties);
    vk::MemoryAllocateInfo memoryAllocateInfo{.allocationSize  = imageMemoryRequirements.size,
                                              .memoryTypeIndex = memoryTypeIndex};
    mMemory = std::make_shared<vk::raii::DeviceMemory>(mGraphicsProvider->getDevice(),
                                                       memoryAllocateInfo);

    mImage->bindMemory(**mMemory, 0);

    vk::ComponentMapping      componentMapping{vk::ComponentSwizzle::eR,
                                          vk::ComponentSwizzle::eG,
                                          vk::ComponentSwizzle::eB,
                                          vk::ComponentSwizzle::eA};
    vk::ImageSubresourceRange imageSubresourceRange{toAspectFlags(usage), 0, 1, 0, 1};
    vk::ImageViewCreateInfo   imageViewCreateInfo{.image            = **mImage,
                                                  .viewType         = vk::ImageViewType::e2D,
                                                  .format           = mFormat,
                                                  .components       = componentMapping,
                                                  .subresourceRange = imageSubresourceRange};
    mImageView =
        std::make_shared<vk::raii::ImageView>(mGraphicsProvider->getDevice(), imageViewCreateInfo);
}
const std::shared_ptr<vk::raii::ImageView>&
RenderBuffer::getImageView() const
{
    return mImageView;
}
} // namespace VOG::Graphics::Vulkan
