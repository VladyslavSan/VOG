#pragma once

#include <VOG/Graphics/Config/VmaConfig.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <cstdint>
#include <memory>

namespace VOG::Graphics
{
class GraphicsProvider;
}

namespace VOG::Graphics::Vulkan
{
class Buffer;
class MemoryAllocator : public std::enable_shared_from_this<MemoryAllocator>
{
    MemoryAllocator(const GraphicsProvider& GraphicsProvider);

public:
    class Allocation;
    struct AllocationInfo;

    [[nodiscard]] static std::shared_ptr<MemoryAllocator>
    create(const GraphicsProvider& graphicsProvider)
    {
        return std::shared_ptr<MemoryAllocator>{new MemoryAllocator{graphicsProvider}};
    }

    ~MemoryAllocator();

    operator VmaAllocator() const;

    std::unique_ptr<Buffer> makeBuffer(const vk::BufferCreateInfo& createInfo,
                                       const AllocationInfo&       allocationInfo);

protected:
    const GraphicsProvider& mGraphicsProvider;
    VmaAllocator            mAllocator;
};

struct MemoryAllocator::AllocationInfo
{
    VmaAllocationCreateFlags flags;
    VmaMemoryUsage           usage;
    const char*              tag = nullptr;
};

class MemoryAllocator::Allocation
{
public:
    ~Allocation();

    Allocation(const MemoryAllocator* _allocator,
               VmaAllocation          _allocation,
               VmaAllocationInfo      _info,
               AllocationInfo         _createInfo);

    Allocation(Allocation&&) noexcept;

    Allocation(const Allocation&) = delete;

protected:
    /** Allocator that created allocation */
    const MemoryAllocator* allocator;

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
