#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Buffer);

/**
 * GPU buffer. The native handle is reachable via operator*; the buffer and its backing memory are
 * hidden behind a pImpl so this header stays VMA-free. The allocation retains a DevicePtr and both
 * the VkBuffer and its memory are released together (vmaDestroyBuffer) while the device is alive.
 */
class Buffer
{
    friend class MemoryAllocator;

public:
    /** Where the allocation should prefer to live. Maps to a VMA memory usage internally. */
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

    /**
     * VOG-owned allocation intent. Deliberately free of any VMA type so callers do not
     * transitively include the allocator headers; the translation of these fields into VMA
     * structs happens entirely inside the Graphics implementation.
     */
    struct AllocationParameters
    {
        MemoryPreference memory             = MemoryPreference::eAuto;
        HostAccess       hostAccess         = HostAccess::eNone;
        bool             persistentlyMapped = false;
        const char*      tag                = nullptr;
    };

    template <class PtrType = std::byte*>
    struct MemoryMapping
    {
        /** Pointer for read/write operations. */
        PtrType data;

        /** Size that defines max number bytes that can be accessed by the @var data pointer. */
        const std::size_t size;

        /** Buffer pointer used to unmap and flush the allocation. */
        const Buffer* buffer;

        /** If true, destructor will unmap the allocation. */
        const bool shouldUnmap;

        /** If true, destructor will flush to commit changes and make them visible for GPU. */
        const bool shouldFlush;

        MemoryMapping(PtrType       data,
                      std::size_t   size,
                      const Buffer* buffer,
                      bool          shouldUnmap,
                      bool          shouldFlush);

        ~MemoryMapping();

        MemoryMapping(const MemoryMapping&) = delete;
        MemoryMapping(MemoryMapping&&)      = delete;
    };

    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer(Buffer&&)      = delete;

    /** Native buffer handle, for recording into command buffers. */
    [[nodiscard]] vk::Buffer operator*() const;

    [[nodiscard]] MemoryMapping<std::byte*> mapForWrite();

    [[nodiscard]] MemoryMapping<const std::byte*> mapForRead();

private:
    class Allocation;

    explicit Buffer(std::unique_ptr<Allocation> allocation);

    /** Flush the whole allocation so host writes become visible to the GPU. */
    void flushMapping() const;

    /** Unmap a non-persistent mapping. */
    void unmapMapping() const;

    std::unique_ptr<Allocation> mAllocation;
};

template <class PtrType>
inline Buffer::MemoryMapping<PtrType>::MemoryMapping(const PtrType     _data,
                                                     const std::size_t _size,
                                                     const Buffer*     _buffer,
                                                     const bool        _shouldUnmap,
                                                     const bool        _shouldFlush)
    : data{_data}
    , size{_size}
    , buffer{_buffer}
    , shouldUnmap{_shouldUnmap}
    , shouldFlush{_shouldFlush}
{
}

template <class PtrType>
inline Buffer::MemoryMapping<PtrType>::~MemoryMapping()
{
    if (shouldFlush)
    {
        buffer->flushMapping();
    }

    if (shouldUnmap)
    {
        buffer->unmapMapping();
    }
}
} // namespace VOG::Graphics::Vulkan
