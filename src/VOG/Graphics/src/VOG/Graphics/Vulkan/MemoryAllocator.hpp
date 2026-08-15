#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>

#include <cstddef>
#include <memory>

// Opaque VMA handles, mirroring VK_DEFINE_HANDLE. The full VMA types are only ever pulled into the
// .cpp translation units that talk to the allocator; this private header — and anything that
// includes it — stays free of <vk_mem_alloc.h>. A duplicate identical typedef in the VMA header is
// well-formed, so the same names resolve seamlessly once VmaConfig.hpp is included alongside.
VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(MemoryAllocator);
VOG_DECLARE_PTR(Device);

/**
 * Thin wrapper around a VmaAllocator. Private to Device: holds a bare Device& so it cannot pin the
 * device, while the buffers it hands out each retain a DevicePtr and free through this allocator
 * for as long as the device lives.
 */
class MemoryAllocator
{
    friend class Device;

    explicit MemoryAllocator(Device& device);

public:
    ~MemoryAllocator();

    MemoryAllocator(const MemoryAllocator&) = delete;
    MemoryAllocator(MemoryAllocator&&)      = delete;

private:
    std::unique_ptr<Buffer> makeBuffer(const vk::BufferCreateInfo&       createInfo,
                                       const BufferAllocationParameters& parameters);

    Device&      mDevice;
    VmaAllocator mAllocator;
};

/**
 * The VkBuffer and its backing memory for a Buffer. Hidden as a pImpl so Buffer.hpp stays VMA-free.
 * Buffer and memory are created and destroyed together through VMA (vmaCreateBuffer /
 * vmaDestroyBuffer) so the native handle never outlives its allocation.
 */
class Buffer::Allocation
{
public:
    Allocation(DevicePtr               device,
               VmaAllocator            allocator,
               vk::Buffer              buffer,
               VmaAllocation           allocation,
               std::byte*              mappedData,
               std::size_t             size,
               bool                    persistentlyMapped,
               vk::MemoryPropertyFlags memoryFlags);

    ~Allocation();

    Allocation(Allocation&&) noexcept;

    Allocation(const Allocation&) = delete;

    /**
     * Device that owns the allocator. Held by shared_ptr so a resource outliving every other
     * DevicePtr still keeps the VkDevice (and with it the allocator) alive until freed.
     */
    DevicePtr device;

    /** Allocator that created the allocation; owned by @var device. */
    VmaAllocator allocator;

    /** Native buffer handle, owned jointly with @var allocation. */
    vk::Buffer buffer;

    /** Underlying VMA allocation. */
    VmaAllocation allocation;

    /** Persistent mapping pointer, if the allocation was created persistently mapped. */
    std::byte* mappedData;

    /** Allocation size in bytes. */
    std::size_t size;

    /** If true the allocation stays mapped, so map/unmap are no-ops. */
    bool isPersistentlyMapped;

    /** Vulkan memory property flags of the backing memory type. */
    vk::MemoryPropertyFlags memoryFlags;
};
} // namespace VOG::Graphics::Vulkan
