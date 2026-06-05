#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

class RenderBuffer : public AttachmentInterface
{
    friend class Device;

private:
    RenderBuffer(DevicePtr               device,
                 AttachmentUsage         usage,
                 vk::Format              desiredFormat,
                 vk::Extent2D            extent,
                 SampleCount             sampleCount,
                 std::uint32_t           mipLevels,
                 std::uint32_t           arrayLevels,
                 vk::ImageTiling         imageTiling,
                 vk::ImageLayout         initialLayout,
                 vk::MemoryPropertyFlags memoryProperties);

public:
    const vk::Image&                            getImage() const override;
    const std::shared_ptr<vk::raii::ImageView>& getImageView() const override;

protected:
    DevicePtr                               mDevice;
    std::shared_ptr<vk::raii::Image>        mImage;
    std::shared_ptr<vk::raii::ImageView>    mImageView;
    std::shared_ptr<vk::raii::DeviceMemory> mMemory;
};
} // namespace VOG::Graphics::Vulkan
