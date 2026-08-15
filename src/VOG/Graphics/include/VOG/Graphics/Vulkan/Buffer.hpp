#pragma once

#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/MemoryAllocator.hpp>

#include <cstddef>

namespace VOG::Graphics::Vulkan
{
/**
 * GPU buffer. Publicly a vk::raii::Buffer so the native handle is reachable via operator*, and an
 * allocation so it knows the memory backing it.
 *
 * The buffer owns its own teardown: it holds a DevicePtr for its entire lifetime, and the
 * destructor destroys the VkBuffer and then returns the allocation to the device-owned allocator,
 * both while the device is guaranteed alive.
 */
class Buffer
    : protected MemoryAllocator::Allocation
    , public vk::raii::Buffer
{
    friend class MemoryAllocator;

public:
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

    [[nodiscard]] MemoryMapping<std::byte*> mapForWrite();

    [[nodiscard]] MemoryMapping<const std::byte*> mapForRead();

private:
    Buffer(DevicePtr device, MemoryAllocator::Allocation allocation, vk::raii::Buffer buffer);

    /** Allocator owning this buffer's memory; reached through the retained device. */
    [[nodiscard]] MemoryAllocator& allocator() const;

    /** Flush the whole allocation so host writes become visible to the GPU. */
    void flushMapping() const;

    /** Unmap a non-persistent mapping. */
    void unmapMapping() const;

    /** Keeps the device — and with it the allocator — alive for this buffer's whole lifetime. */
    DevicePtr mDevice;
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
