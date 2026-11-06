#include "VOG/Graphics/Vulkan/Buffer.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/VmaConfig.hpp>

#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
Buffer::Buffer(MemoryAllocator::Allocation allocation,
               vk::raii::Buffer            buffer,
               vk::DeviceSize              byteSize)
    : MemoryAllocator::Allocation{std::move(allocation)}
    , vk::raii::Buffer{std::move(buffer)}
    , mByteSize{byteSize}
{
}

Buffer::MemoryMapping::~MemoryMapping()
{
    if (mShouldFlush)
    {
        vmaFlushAllocation(mBuffer.allocator, mBuffer.allocation, 0, VK_WHOLE_SIZE);
    }

    vmaUnmapMemory(mBuffer.allocator, mBuffer.allocation);
}

Buffer::MemoryMapping
Buffer::mapForWrite()
{
    VOG_ASSERT_MSG(
        flags & AllocationFlag::eHostSequentialWrite,
        "Only a buffer created with AllocationFlag::eHostSequentialWrite can be mapped.");

    void* data   = nullptr;
    auto  result = vmaMapMemory(allocator, allocation, &data);
    if (result != VK_SUCCESS) [[unlikely]]
    {
        throw std::runtime_error{"Buffer::mapForWrite: vmaMapMemory failed."};
    }

    const bool isHostCoherent =
        static_cast<bool>(memoryFlags & vk::MemoryPropertyFlagBits::eHostCoherent);

    return MemoryMapping{{static_cast<std::byte*>(data), size}, *this, !isHostCoherent};
}

Buffer::ReadMapping
Buffer::mapForRead()
{
    VOG_ASSERT_MSG(flags & AllocationFlag::eHostRandomAccess,
                   "Only a buffer created with AllocationFlag::eHostRandomAccess can be mapped for "
                   "reading.");

    void* data   = nullptr;
    auto  result = vmaMapMemory(allocator, allocation, &data);
    if (result != VK_SUCCESS) [[unlikely]]
    {
        throw std::runtime_error{"Buffer::mapForRead: vmaMapMemory failed."};
    }

    if (!(memoryFlags & vk::MemoryPropertyFlagBits::eHostCoherent))
    {
        result = vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
        if (result != VK_SUCCESS) [[unlikely]]
        {
            vmaUnmapMemory(allocator, allocation);

            throw std::runtime_error{"Buffer::mapForRead: vmaInvalidateAllocation failed."};
        }
    }

    // Nothing was written, so nothing is flushed.
    return ReadMapping{{static_cast<std::byte*>(data), size}, *this};
}

vk::DeviceAddress
Buffer::deviceAddress() const
{
    return device->getBufferAddress({.buffer = **this});
}
} // namespace VOG::Graphics::Vulkan
