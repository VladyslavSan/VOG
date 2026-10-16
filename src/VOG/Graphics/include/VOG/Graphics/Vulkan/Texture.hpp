#pragma once

#include <VOG/Graphics/Vulkan/DescriptorHeap.hpp>
#include <VOG/Graphics/Vulkan/MemoryAllocator.hpp>

#include <cstdint>

namespace VOG::Graphics::Vulkan
{
/**
 * A 2D, single-mip, device-local image that shaders sample through the descriptor heap: its
 * descriptor sits in the heap slot that heapIndex() names. Made by Device::createTexture(); fill
 * it with CommandBufferRecorder::copyBufferToTexture(). The texture keeps its heap, and through
 * it the device, alive.
 */
class Texture
    : protected MemoryAllocator::Allocation
    , private vk::raii::Image
{
    friend class Device;
    friend class MemoryAllocator;

public:
    using vk::raii::Image::operator*;

    /** Index of the texture's descriptor in the resource heap; what a shader reads it by. */
    std::uint32_t
    heapIndex() const noexcept
    {
        return mSlot.index();
    }

    vk::Extent2D
    extent() const noexcept
    {
        return mExtent;
    }

private:
    Texture(MemoryAllocator::Allocation allocation,
            vk::raii::Image             image,
            DescriptorHeap::Slot        slot,
            vk::Extent2D                extent);

    DescriptorHeap::Slot mSlot;
    vk::Extent2D         mExtent;
};
} // namespace VOG::Graphics::Vulkan
