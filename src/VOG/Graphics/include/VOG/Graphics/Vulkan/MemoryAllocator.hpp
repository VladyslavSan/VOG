#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <cstddef>
#include <cstdint>

namespace VOG::Graphics
{
class GraphicsProvider;
}

namespace VOG::Graphics::Vulkan
{
class MemoryAllocator;
class Allocation
{
public:
    Allocation(MemoryAllocator* allocator,
               std::uintptr_t   allocationHandle,
               std::uintptr_t   allocationInfo);

    ~Allocation();

    std::uint32_t vulkanMemoryType() const;

    VkDeviceMemory vulkanDeviceMemory() const;

    std::uint64_t size() const;

    std::uint64_t offset() const;

    std::byte* mappedMemory() const;

protected:
    /** Allocator that created allocation */
    MemoryAllocator* mAllocator;

    /** In VMA terms this corresponds to VmaAllocation handle */
    std::uintptr_t mAllocationHandle;

    /** In VMA terms this corresponds to VmaAllocationInfo pointer */
    std::uintptr_t mAllocationInfo;
};

class BufferAllocation : public Allocation
{
public:
    using Allocation::Allocation;

    ~BufferAllocation();
};

class MemoryAllocator
{
    friend class Allocation;

public:
    MemoryAllocator(const GraphicsProvider& GraphicsProvider);

    ~MemoryAllocator();

    Allocation allocateMemoryForBuffer(const vk::raii::Buffer& buffer);

    Allocation allocateMemoryForImage(const vk::raii::Image& image);

protected:
    std::uintptr_t getAllocatorHandle() const;

protected:
    const GraphicsProvider& mGraphicsProvider;
    std::uintptr_t          mAllocatorHandle;
};
} // namespace VOG::Graphics::Vulkan
