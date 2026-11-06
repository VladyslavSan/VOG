#include "VOG/Graphics/Vulkan/UploadBarriers.hpp"

namespace VOG::Graphics::Vulkan
{
namespace
{
constexpr vk::ImageSubresourceRange gColor{
    .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1u, .layerCount = 1u};

constexpr vk::PipelineStageFlags2 gShaderStages =
    vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader;
} // namespace

BufferUploadBarriers
bufferUploadBarriers(vk::Buffer buffer, std::uint32_t uploadFamily, std::uint32_t usageFamily)
{
    if (uploadFamily == usageFamily)
    {
        return {};
    }

    return {
        .release =
            vk::BufferMemoryBarrier2{
                .srcStageMask        = vk::PipelineStageFlagBits2::eCopy,
                .srcAccessMask       = vk::AccessFlagBits2::eTransferWrite,
                .srcQueueFamilyIndex = uploadFamily,
                .dstQueueFamilyIndex = usageFamily,
                .buffer              = buffer,
                .size                = vk::WholeSize,
            },
        .acquire =
            vk::BufferMemoryBarrier2{
                .dstStageMask = vk::PipelineStageFlagBits2::eVertexAttributeInput |
                                vk::PipelineStageFlagBits2::eIndexInput,
                .dstAccessMask =
                    vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eIndexRead,
                .srcQueueFamilyIndex = uploadFamily,
                .dstQueueFamilyIndex = usageFamily,
                .buffer              = buffer,
                .size                = vk::WholeSize,
            },
    };
}

TextureUploadBarriers
textureUploadBarriers(vk::Image image, std::uint32_t uploadFamily, std::uint32_t usageFamily)
{
    if (uploadFamily == usageFamily)
    {
        return {.release = {.srcStageMask        = vk::PipelineStageFlagBits2::eCopy,
                            .srcAccessMask       = vk::AccessFlagBits2::eTransferWrite,
                            .dstStageMask        = gShaderStages,
                            .dstAccessMask       = vk::AccessFlagBits2::eShaderSampledRead,
                            .oldLayout           = vk::ImageLayout::eTransferDstOptimal,
                            .newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal,
                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .image               = image,
                            .subresourceRange    = gColor},
                .acquire = std::nullopt};
    }

    return {
        .release =
            {
                .srcStageMask        = vk::PipelineStageFlagBits2::eCopy,
                .srcAccessMask       = vk::AccessFlagBits2::eTransferWrite,
                .oldLayout           = vk::ImageLayout::eTransferDstOptimal,
                .newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal,
                .srcQueueFamilyIndex = uploadFamily,
                .dstQueueFamilyIndex = usageFamily,
                .image               = image,
                .subresourceRange    = gColor,
            },
        .acquire =
            vk::ImageMemoryBarrier2{
                .dstStageMask        = gShaderStages,
                .dstAccessMask       = vk::AccessFlagBits2::eShaderSampledRead,
                .oldLayout           = vk::ImageLayout::eTransferDstOptimal,
                .newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal,
                .srcQueueFamilyIndex = uploadFamily,
                .dstQueueFamilyIndex = usageFamily,
                .image               = image,
                .subresourceRange    = gColor,
            },
    };
}
} // namespace VOG::Graphics::Vulkan
