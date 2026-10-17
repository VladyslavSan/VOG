#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/DescriptorHeap.hpp>

#include <cstdint>
#include <memory>

// VMA's own handle types, declared here so that no public header includes vk_mem_alloc.h.
struct VmaAllocator_T;
struct VmaAllocation_T;

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Buffer);
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(Texture);

enum class AllocationFlag : std::uint8_t
{
    /** The CPU writes the memory in order and never reads it: map with Buffer::mapForWrite(). */
    eHostSequentialWrite = 1u << 0u,
};
} // namespace VOG::Graphics::Vulkan

namespace vk
{
template <>
struct FlagTraits<VOG::Graphics::Vulkan::AllocationFlag>
{
    // The names are required by vk::FlagTraits.
    // NOLINTBEGIN(readability-identifier-naming)
    static constexpr bool                                             isBitmask = true;
    static constexpr vk::Flags<VOG::Graphics::Vulkan::AllocationFlag> allFlags =
        VOG::Graphics::Vulkan::AllocationFlag::eHostSequentialWrite;
    // NOLINTEND(readability-identifier-naming)
};
} // namespace vk

namespace VOG::Graphics::Vulkan
{
using AllocationFlags = vk::Flags<AllocationFlag>;

class MemoryAllocator
{
    friend class Device;

    /**
     * @param device Owning device. The allocator is owned by the device, so a plain
     *               reference is enough; resources handed out retain a DevicePtr instead.
     */
    explicit MemoryAllocator(Device& device);

public:
    class Allocation;
    struct AllocationParameters;

    ~MemoryAllocator();

    MemoryAllocator(const MemoryAllocator&)            = delete;
    MemoryAllocator& operator=(const MemoryAllocator&) = delete;

private:
    std::unique_ptr<Buffer> makeBuffer(const vk::BufferCreateInfo& createInfo,
                                       const AllocationParameters& allocationCreateInfo);

    /** Device-local image; the texture owns @p slot, the descriptor of the image. */
    std::unique_ptr<Texture> makeTexture(const vk::ImageCreateInfo& createInfo,
                                         DescriptorHeap::Slot       slot);

    Device&         mDevice;
    VmaAllocator_T* mAllocator;
};

struct MemoryAllocator::AllocationParameters
{
    /** The memory type is always picked by VMA for the buffer's usage. */
    AllocationFlags flags;

    /** Minimum address alignment of the buffer; 0 takes what the buffer's usage needs. */
    vk::DeviceSize alignment = 0u;
};

class MemoryAllocator::Allocation
{
public:
    ~Allocation();

    Allocation(DevicePtr               device,
               VmaAllocator_T*         allocator,
               VmaAllocation_T*        allocation,
               vk::DeviceSize          size,
               vk::MemoryPropertyFlags memoryFlags,
               AllocationFlags         flags);

    Allocation(Allocation&& other) noexcept;

    Allocation(const Allocation&) = delete;

protected:
    /**
     * Device that owns the allocator. Held by shared_ptr so that a resource outliving every
     * other DevicePtr still keeps the VkDevice (and with it the allocator) alive until freed.
     */
    DevicePtr device;

    /** Allocator that created allocation; owned by @var device */
    VmaAllocator_T* allocator;

    /** Allocation struct */
    VmaAllocation_T* allocation;

    /** Size of the allocation in bytes. */
    const vk::DeviceSize size;

    /** What the allocation was created with. */
    const AllocationFlags flags;

    /** Vulkan memory property flags */
    const vk::MemoryPropertyFlags memoryFlags;
};
} // namespace VOG::Graphics::Vulkan
