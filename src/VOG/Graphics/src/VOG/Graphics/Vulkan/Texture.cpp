#include "VOG/Graphics/Vulkan/Texture.hpp"

namespace VOG::Graphics::Vulkan
{
Texture::Texture(MemoryAllocator::Allocation allocation,
                 vk::raii::Image             image,
                 DescriptorHeap::Slot        slot,
                 vk::Extent2D                extent,
                 vk::Format                  format)
    : MemoryAllocator::Allocation{std::move(allocation)}
    , vk::raii::Image{std::move(image)}
    , mSlot{std::move(slot)}
    , mExtent{extent}
    , mFormat{format}
{
}
} // namespace VOG::Graphics::Vulkan
