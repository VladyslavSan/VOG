#pragma once

#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/DescriptorHeap.hpp>

#include <cstdint>

namespace VOG::Graphics::Vulkan
{
/**
 * A host-written buffer that shaders read as a uniform block through the descriptor heap: its
 * descriptor sits in the heap slot that heapIndex() names, and was written when the buffer was
 * made. Made by Device::createHeapBuffer(). The buffer keeps its Buffer and its heap, and through
 * them the device, alive.
 *
 * The contents can change at any time through mapForWrite(). A write is only safe while no command
 * buffer that was submitted and is still running reads the buffer: Graphics adds no barrier and no
 * copy, and the caller keeps writer and readers apart, for example by writing a buffer for each
 * frame slot.
 */
class HeapBuffer
{
    friend class Device;

public:
    /** Index of the buffer's descriptor in the resource heap; what a shader reads it by. */
    [[nodiscard]] std::uint32_t
    heapIndex() const noexcept
    {
        return mSlot.index();
    }

    /**
     * Maps the buffer for writing; see Buffer::mapForWrite(). The mapping holds the whole
     * allocation, which can be larger than the size the buffer was made with.
     */
    [[nodiscard]] Buffer::MemoryMapping
    mapForWrite()
    {
        return mBuffer->mapForWrite();
    }

private:
    HeapBuffer(DescriptorHeap::Slot slot, BufferPtr buffer);

    DescriptorHeap::Slot mSlot;
    BufferPtr            mBuffer;
};
} // namespace VOG::Graphics::Vulkan
