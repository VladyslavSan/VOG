#include "VOG/Graphics/Vulkan/Buffer.hpp"

namespace VOG::Graphics::Vulkan
{
Buffer::Buffer(MemoryAllocator::Allocation allocation, vk::raii::Buffer buffer)
    : MemoryAllocator::Allocation{std::move(allocation)}
    , vk::raii::Buffer{std::move(buffer)}
{
}

Buffer::MemoryMapping<std::byte*>
Buffer::mapForWrite()
{
    std::byte* pData = reinterpret_cast<std::byte*>(info.pMappedData);
    const bool isHostCoherent =
        static_cast<bool>(memoryFlags & vk::MemoryPropertyFlagBits::eHostCoherent);

    if (!isPersistentlyMapped)
    {
        void* data   = nullptr;
        auto  result = vmaMapMemory(allocator, allocation, &data);
        if (result != VK_SUCCESS) [[unlikely]]
        {
            throw std::runtime_error{"Buffer::map: vmaMapMemory failed."};
        }
        pData = reinterpret_cast<std::byte*>(data);
    }

    return {pData, info.size, this, !isPersistentlyMapped, !isHostCoherent};
}

Buffer::MemoryMapping<const std::byte*>
Buffer::mapForRead()
{
    std::byte* pData = reinterpret_cast<std::byte*>(info.pMappedData);
    if (!isPersistentlyMapped)
    {
        void* data   = nullptr;
        auto  result = vmaMapMemory(allocator, allocation, &data);
        if (result != VK_SUCCESS) [[unlikely]]
        {
            throw std::runtime_error{"Buffer::map: vmaMapMemory failed."};
        }
        pData = reinterpret_cast<std::byte*>(data);

        vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
    }

    return {pData, info.size, this, !isPersistentlyMapped, false};
}
} // namespace VOG::Graphics::Vulkan
