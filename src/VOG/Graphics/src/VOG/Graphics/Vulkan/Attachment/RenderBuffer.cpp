#include "VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp"

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Converters.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Utilities.hpp>

namespace VOG::Graphics::Vulkan
{
RenderBuffer::RenderBuffer(DevicePtr               device,
                           AttachmentUsage         usage,
                           vk::Format              desiredFormat,
                           vk::Extent2D            extent,
                           SampleCount             sampleCount,
                           std::uint32_t           mipLevels,
                           std::uint32_t           arrayLevels,
                           vk::ImageTiling         imageTiling,
                           vk::ImageLayout         initialLayout,
                           vk::MemoryPropertyFlags memoryProperties)
    : AttachmentInterface{usage, desiredFormat, {extent.width, extent.height, 1u}, sampleCount}
    , mDevice{std::move(device)}
{
    const vk::ImageCreateInfo imageCreateInfo{
        .imageType     = vk::ImageType::e2D,
        .format        = mFormat,
        .extent        = mExtent,
        .mipLevels     = mipLevels,
        .arrayLayers   = arrayLevels,
        .samples       = sampleCount,
        .tiling        = imageTiling,
        .usage         = toUsageFlags(usage),
        .sharingMode   = vk::SharingMode::eExclusive,
        .initialLayout = initialLayout,
    };

    mImage                       = std::make_shared<vk::raii::Image>(*mDevice, imageCreateInfo);
    auto imageMemoryRequirements = mImage->getMemoryRequirements();

    std::uint32_t memoryTypeIndex =
        FindMemoryType(mDevice->getPhysicalDevice().getMemoryProperties(),
                       imageMemoryRequirements.memoryTypeBits,
                       memoryProperties);
    vk::MemoryAllocateInfo memoryAllocateInfo{.allocationSize  = imageMemoryRequirements.size,
                                              .memoryTypeIndex = memoryTypeIndex};
    mMemory = std::make_shared<vk::raii::DeviceMemory>(*mDevice, memoryAllocateInfo);

    mImage->bindMemory(**mMemory, 0);

    vk::ImageViewCreateInfo imageViewCreateInfo{.image            = **mImage,
                                                .viewType         = vk::ImageViewType::e2D,
                                                .format           = mFormat,
                                                .components       = {.r = vk::ComponentSwizzle::eR,
                                                                     .g = vk::ComponentSwizzle::eG,
                                                                     .b = vk::ComponentSwizzle::eB,
                                                                     .a = vk::ComponentSwizzle::eA},
                                                .subresourceRange = {
                                                    .aspectMask     = toAspectFlags(usage),
                                                    .baseMipLevel   = 0u,
                                                    .levelCount     = 1u,
                                                    .baseArrayLayer = 0u,
                                                    .layerCount     = 1u,
                                                }};
    mImageView = std::make_shared<vk::raii::ImageView>(*mDevice, imageViewCreateInfo);
}

const vk::Image&
RenderBuffer::getImage() const
{
    return **mImage;
}

const std::shared_ptr<vk::raii::ImageView>&
RenderBuffer::getImageView() const
{
    return mImageView;
}
} // namespace VOG::Graphics::Vulkan
