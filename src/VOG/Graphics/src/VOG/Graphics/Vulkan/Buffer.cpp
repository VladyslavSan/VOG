#include "VOG/Graphics/Vulkan/Buffer.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>

#include <utility>

namespace VOG::Graphics::Vulkan
{
Buffer::Buffer(DevicePtr device, MemoryAllocator::Allocation allocation, vk::raii::Buffer buffer)
    : MemoryAllocator::Allocation{std::move(allocation)}
    , vk::raii::Buffer{std::move(buffer)}
    , mDevice{std::move(device)}
{
}

Buffer::~Buffer()
{
    // Sequence teardown explicitly while mDevice is still held: destroy the VkBuffer first, then
    // hand the memory back to the allocator that produced it.
    vk::raii::Buffer::clear();

    allocator().free(*this);
    mHandle = {};
}

MemoryAllocator&
Buffer::allocator() const
{
    return *mDevice->mMemoryAllocator;
}

void
Buffer::flushMapping() const
{
    allocator().flush(*this);
}

void
Buffer::unmapMapping() const
{
    allocator().unmap(*this);
}

Buffer::MemoryMapping<std::byte*>
Buffer::mapForWrite()
{
    const bool isPersistentlyMapped = mMappedData != nullptr;
    const bool isHostCoherent =
        static_cast<bool>(mMemoryFlags & vk::MemoryPropertyFlagBits::eHostCoherent);

    std::byte* pData = isPersistentlyMapped ? mMappedData : allocator().map(*this);

    return {pData, mSize, this, !isPersistentlyMapped, !isHostCoherent};
}

Buffer::MemoryMapping<const std::byte*>
Buffer::mapForRead()
{
    const bool isPersistentlyMapped = mMappedData != nullptr;

    std::byte* pData = mMappedData;
    if (!isPersistentlyMapped)
    {
        pData = allocator().map(*this);
        allocator().invalidate(*this);
    }

    return {pData, mSize, this, !isPersistentlyMapped, false};
}
} // namespace VOG::Graphics::Vulkan
