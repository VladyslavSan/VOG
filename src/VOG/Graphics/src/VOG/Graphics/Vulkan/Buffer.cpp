#include "VOG/Graphics/Vulkan/Buffer.hpp"

#include "VOG/Graphics/Vulkan/MemoryAllocator.hpp"
#include <VOG/Graphics/Config/VmaConfig.hpp>

#include <stdexcept>
#include <utility>

namespace VOG::Graphics::Vulkan
{
Buffer::Buffer(std::unique_ptr<Allocation> allocation, vk::raii::Buffer buffer)
    : vk::raii::Buffer{std::move(buffer)}
    , mAllocation{std::move(allocation)}
{
}

Buffer::~Buffer()
{
    // Destroy the VkBuffer while the allocation — and therefore the device — is still alive, then
    // let mAllocation free the backing memory as it is destroyed.
    vk::raii::Buffer::clear();
}

void
Buffer::flushMapping() const
{
    vmaFlushAllocation(mAllocation->allocator, mAllocation->allocation, 0, VK_WHOLE_SIZE);
}

void
Buffer::unmapMapping() const
{
    vmaUnmapMemory(mAllocation->allocator, mAllocation->allocation);
}

Buffer::MemoryMapping<std::byte*>
Buffer::mapForWrite()
{
    std::byte* pData = mAllocation->mappedData;
    const bool isHostCoherent =
        static_cast<bool>(mAllocation->memoryFlags & vk::MemoryPropertyFlagBits::eHostCoherent);

    if (!mAllocation->isPersistentlyMapped)
    {
        void* data   = nullptr;
        auto  result = vmaMapMemory(mAllocation->allocator, mAllocation->allocation, &data);
        if (result != VK_SUCCESS) [[unlikely]]
        {
            throw std::runtime_error{"Buffer::map: vmaMapMemory failed."};
        }
        pData = reinterpret_cast<std::byte*>(data);
    }

    return {pData, mAllocation->size, this, !mAllocation->isPersistentlyMapped, !isHostCoherent};
}

Buffer::MemoryMapping<const std::byte*>
Buffer::mapForRead()
{
    std::byte* pData = mAllocation->mappedData;
    if (!mAllocation->isPersistentlyMapped)
    {
        void* data   = nullptr;
        auto  result = vmaMapMemory(mAllocation->allocator, mAllocation->allocation, &data);
        if (result != VK_SUCCESS) [[unlikely]]
        {
            throw std::runtime_error{"Buffer::map: vmaMapMemory failed."};
        }
        pData = reinterpret_cast<std::byte*>(data);

        vmaInvalidateAllocation(mAllocation->allocator, mAllocation->allocation, 0, VK_WHOLE_SIZE);
    }

    return {pData, mAllocation->size, this, !mAllocation->isPersistentlyMapped, false};
}
} // namespace VOG::Graphics::Vulkan
