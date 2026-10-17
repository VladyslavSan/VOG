#pragma once

#include <VOG/Graphics/Vulkan/DescriptorHeap.hpp>

#include <cstdint>

namespace VOG::Graphics::Vulkan
{
/**
 * An immutable block of bytes that shaders read as a uniform block through the descriptor heap:
 * its descriptor sits in the heap slot that heapIndex() names. Made by
 * Device::createUniformBlock(). The block keeps its buffer and its heap, and through them the
 * device, alive.
 */
class UniformBlock
{
    friend class Device;

public:
    /** Index of the block's descriptor in the resource heap; what a shader reads it by. */
    std::uint32_t
    heapIndex() const noexcept
    {
        return mSlot.index();
    }

private:
    UniformBlock(DescriptorHeap::Slot slot, BufferPtr buffer);

    DescriptorHeap::Slot mSlot;
    BufferPtr            mBuffer;
};
} // namespace VOG::Graphics::Vulkan
