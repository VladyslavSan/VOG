#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>

namespace VOG::Graphics
{
class GraphicsProvider;
}

namespace VOG::Graphics::Vulkan
{
class RenderBuffer : public AttachmentInterface
{
public:
    RenderBuffer(const std::shared_ptr<GraphicsProvider>& graphicsProvider,
                 AttachmentUsage                          usage,
                 vk::Format                               desiredFormat,
                 vk::Extent2D                             extent,
                 SampleCount                              sampleCount,
                 std::uint32_t                            mipLevels,
                 std::uint32_t                            arrayLevels,
                 vk::ImageTiling                          imageTiling,
                 vk::ImageLayout                          initialLayout,
                 vk::MemoryPropertyFlags                  memoryProperties);

    const std::shared_ptr<vk::raii::ImageView>& getImageView() const override;

protected:
    std::shared_ptr<GraphicsProvider>       mGraphicsProvider;
    std::shared_ptr<vk::raii::Image>        mImage;
    std::shared_ptr<vk::raii::ImageView>    mImageView;
    std::shared_ptr<vk::raii::DeviceMemory> mMemory;
};
} // namespace VOG::Graphics::Vulkan
