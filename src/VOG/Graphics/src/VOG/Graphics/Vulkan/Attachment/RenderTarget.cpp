#include "VOG/Graphics/Vulkan/Attachment/RenderTarget.hpp"

namespace VOG::Graphics::Vulkan
{
RenderTarget::RenderTarget(MemoryAllocator::Allocation allocation,
                           vk::raii::Image             image,
                           vk::raii::ImageView         view,
                           vk::Format                  format,
                           vk::Extent2D                extent)
    : AttachmentInterface{format,
                          vk::Extent3D{.width = extent.width, .height = extent.height, .depth = 1u},
                          SampleCount::e1}
    , MemoryAllocator::Allocation{std::move(allocation)}
    , vk::raii::Image{std::move(image)}
    , mView{std::move(view)}
{
}

const vk::Image&
RenderTarget::getImage() const
{
    return vk::raii::Image::operator*();
}

const vk::raii::ImageView&
RenderTarget::getImageView() const
{
    return mView;
}
} // namespace VOG::Graphics::Vulkan
