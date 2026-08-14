#pragma once

#include <VOG/Graphics/Config/VmaConfig.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <memory>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Buffer);
VOG_DECLARE_PTR(MemoryAllocator);
VOG_DECLARE_PTR(Device);

class MemoryAllocator : public std::enable_shared_from_this<MemoryAllocator>
{
    friend class Device;
    explicit MemoryAllocator(Device& device);

public:
    class Allocation;
    struct AllocationParameters;

    ~MemoryAllocator();

    operator VmaAllocator() const;

    Device& mDevice;

protected:
    VmaAllocator mAllocator;

private:
    std::shared_ptr<Buffer> makeBuffer(const vk::BufferCreateInfo& createInfo,
                                       const AllocationParameters& allocationCreateInfo);
};

struct MemoryAllocator::AllocationParameters
{
    VmaAllocationCreateFlags flags;
    VmaMemoryUsage           usage = VMA_MEMORY_USAGE_AUTO;
    vk::MemoryPropertyFlags  requiredFlags;
    vk::MemoryPropertyFlags  preferredFlags;
    const char*              tag = nullptr;

    operator VmaAllocationCreateInfo() const noexcept;
};

class MemoryAllocator::Allocation
{
public:
    ~Allocation();

    Allocation(MemoryAllocatorPtr   allocator,
               VmaAllocation        allocation,
               VmaAllocationInfo    info,
               AllocationParameters createInfo);

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
    const AllocationParameters createInfo;

    /** Is this allocation is mapped persistently. If yes there - no need to call map/unmap */
    const bool isPersistentlyMapped;

    /** Vulkan memory property flags */
    const vk::MemoryPropertyFlags memoryFlags;
};
} // namespace VOG::Graphics::Vulkan
