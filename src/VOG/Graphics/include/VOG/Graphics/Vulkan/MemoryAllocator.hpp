#pragma once

#include <VOG/Graphics/Config/VmaConfig.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <cstdint>
#include <memory>

namespace VOG::Graphics::Vulkan
{
class Buffer;
VOG_DECLARE_PTR(MemoryAllocator);
VOG_DECLARE_PTR(Device);
class MemoryAllocator : public std::enable_shared_from_this<MemoryAllocator>
{
    friend class Device;
    MemoryAllocator(const DevicePtr& device);

public:
    class Allocation;
    struct AllocationInfo;

    ~MemoryAllocator();

    operator VmaAllocator() const;

    std::unique_ptr<Buffer> makeBuffer(const vk::BufferCreateInfo& createInfo,
                                       const AllocationInfo&       allocationInfo);

public:
    const DevicePtr mDevice;

protected:
    VmaAllocator mAllocator;
};

struct MemoryAllocator::AllocationInfo
{
    VmaAllocationCreateFlags flags;
    VmaMemoryUsage           usage          = VMA_MEMORY_USAGE_AUTO;
    vk::MemoryPropertyFlags  requiredFlags  = {};
    vk::MemoryPropertyFlags  preferredFlags = {};
    const char*              tag            = nullptr;

    operator VmaAllocationCreateInfo() const noexcept;
};

class MemoryAllocator::Allocation
{
public:
    ~Allocation();

    Allocation(MemoryAllocatorPtr allocator,
               VmaAllocation      allocation,
               VmaAllocationInfo  info,
               AllocationInfo     createInfo);

    Allocation(Allocation&&) noexcept;

    Allocation(const Allocation&) = delete;

protected:
    /** Allocator that created allocation */
    MemoryAllocatorPtr allocator;

    /** Allocation struct */
    VmaAllocation allocation;

    /** Allocation info */
    const VmaAllocationInfo info;

    /** Creation info*/
    const AllocationInfo createInfo;

    /** Is this allocation is mapped persistently. If yes there - no need to call map/unmap */
    const bool isPersistentlyMapped;

    /** Vulkan memory property flags */
    const vk::MemoryPropertyFlags memoryFlags;
};
} // namespace VOG::Graphics::Vulkan
