#include "VOG/Graphics/Vulkan/Buffer.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/VmaConfig.hpp>

#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
Buffer::Buffer(MemoryAllocator::Allocation allocation, vk::raii::Buffer buffer)
    : MemoryAllocator::Allocation{std::move(allocation)}
    , vk::raii::Buffer{std::move(buffer)}
{
}

Buffer::MemoryMapping::~MemoryMapping()
{
    if (shouldFlush)
    {
        vmaFlushAllocation(buffer->allocator, buffer->allocation, 0, VK_WHOLE_SIZE);
    }

    vmaUnmapMemory(buffer->allocator, buffer->allocation);
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

    return {static_cast<std::byte*>(data), size, this, !isHostCoherent};
}
} // namespace VOG::Graphics::Vulkan
