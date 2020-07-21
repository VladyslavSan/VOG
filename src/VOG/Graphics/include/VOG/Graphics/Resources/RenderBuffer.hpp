#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Resources/Attachment.hpp>

#include <memory>

namespace vk::raii
{
class Image;
class DeviceMemory;
} // namespace vk::raii

namespace VOG::Graphics::Api
{
class GraphicsProvider;
}

namespace VOG::Graphics::Resources
{
class RenderBuffer : public Attachment
{
public:
    RenderBuffer(const std::shared_ptr<Api::GraphicsProvider>& graphicsProvider,
                 AttachmentUsage usage, AttachmentFormat desiredFormat, AttachmentExtent extent,
                 SampleCount sampleCount, std::uint32_t mipLevels, std::uint32_t arrayLevels,
                 vk::ImageTiling imageTiling, vk::ImageLayout initialLayout,
                 vk::MemoryPropertyFlags memoryProperties);

    const std::shared_ptr<vk::raii::ImageView>& GetImageView() const override;

protected:
    std::shared_ptr<Api::GraphicsProvider> m_graphicsProvider;
    std::shared_ptr<vk::raii::Image> m_image;
    std::shared_ptr<vk::raii::ImageView> m_imageView;
    std::shared_ptr<vk::raii::DeviceMemory> m_memory;
};
} // namespace VOG::Graphics::Resources
