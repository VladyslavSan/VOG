#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Buffer);
VOG_DECLARE_PTR(MemoryAllocator);
VOG_DECLARE_PTR(Device);

/**
 * Owns the GPU memory allocator. Private to Device: holds a bare Device& (through its
 * implementation) so it cannot pin the device, while resources handed out retain a DevicePtr.
 *
 * This header is deliberately free of allocator-backend types; every backend struct and handle
 * lives in the implementation translation unit, so callers never include them transitively.
 */
class MemoryAllocator
{
    friend class Device;

    explicit MemoryAllocator(Device& device);

public:
    /**
     * Identity of a single allocation inside this allocator. Opaque on purpose: it carries the
     * backend token without naming the backend, and without degrading to a bare void*.
     */
    struct AllocationHandle
    {
        std::uintptr_t value = 0u;

        [[nodiscard]] explicit
        operator bool() const noexcept
        {
            return value != 0u;
        }
    };

    /** Where the allocation should prefer to live. */
    enum class MemoryPreference : std::uint8_t
    {
        /** Let the allocator pick based on usage flags and host-access hints. */
        eAuto,
        /** Prefer device-local memory (fast GPU access, staging needed for host writes). */
        eDevice,
        /** Prefer host-visible memory (directly mappable, slower GPU access). */
        eHost,
    };

    /** How the CPU intends to touch the mapping. Drives host-visible placement and coherency. */
    enum class HostAccess : std::uint8_t
    {
        /** GPU-only; no host mapping expected. */
        eNone,
        /** CPU writes sequentially (e.g. upload). */
        eSequentialWrite,
        /** CPU reads and/or writes randomly. */
        eRandom,
    };

    /** VOG-owned allocation intent, translated to backend flags inside the implementation. */
    struct AllocationParameters
    {
        MemoryPreference memory             = MemoryPreference::eAuto;
        HostAccess       hostAccess         = HostAccess::eNone;
        bool             persistentlyMapped = false;
        const char*      tag                = nullptr;
    };

    class Allocation;

    ~MemoryAllocator();

    MemoryAllocator(const MemoryAllocator&) = delete;
    MemoryAllocator(MemoryAllocator&&)      = delete;

    /** Releases the memory behind @p allocation. The caller must have destroyed its resource. */
    void free(const Allocation& allocation) noexcept;

    /** Maps a non-persistent allocation and returns the host pointer. */
    [[nodiscard]] std::byte* map(const Allocation& allocation);

    void unmap(const Allocation& allocation) noexcept;

    /** Commits host writes so they become visible to the GPU. */
    void flush(const Allocation& allocation) noexcept;

    /** Makes GPU writes visible to the host before reading. */
    void invalidate(const Allocation& allocation) noexcept;

private:
    std::unique_ptr<Buffer> makeBuffer(const vk::BufferCreateInfo& createInfo,
                                       const AllocationParameters& parameters);

    class Implementation;

    std::unique_ptr<Implementation> mImplementation;
};

/**
 * A single allocation, as a plain value: it identifies memory but owns nothing, so copying or
 * destroying it never touches the GPU. Freeing is the resource's job, which is why Buffer derives
 * from this and releases it explicitly while it still holds a DevicePtr.
 */
class MemoryAllocator::Allocation
{
    friend class MemoryAllocator;

public:
    Allocation() = default;

    Allocation(AllocationHandle        handle,
               vk::DeviceSize          size,
               std::byte*              mappedData,
               vk::MemoryPropertyFlags memoryFlags);

protected:
    /** Identity within the owning allocator; falsy once released. */
    AllocationHandle mHandle{};

    /** Allocation size in bytes. */
    vk::DeviceSize mSize = 0u;

    /** Non-null only when the allocation is persistently mapped, making map/unmap unnecessary. */
    std::byte* mMappedData = nullptr;

    /** Memory property flags of the backing memory type. */
    vk::MemoryPropertyFlags mMemoryFlags{};
};
} // namespace VOG::Graphics::Vulkan
