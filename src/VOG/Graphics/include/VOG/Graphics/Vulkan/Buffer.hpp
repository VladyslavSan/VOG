#pragma once

#include <VOG/Graphics/Vulkan/MemoryAllocator.hpp>

namespace VOG::Graphics::Vulkan
{
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

        /** If true, destructor will call flush to commit changes and make them visible for GPU. */
        const bool shouldUnmap;

        /** If true, destructor will call flush to commit changes and make them visible for GPU. */
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

    [[nodiscard]] MemoryMapping<std::byte*> mapForWrite();

    [[nodiscard]] MemoryMapping<const std::byte*> mapForRead();

private:
    Buffer(MemoryAllocator::Allocation allocation, vk::raii::Buffer buffer);
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
    , shouldFlush{_shouldFlush}
    , shouldUnmap{_shouldUnmap}
{
}

template <class PtrType>
inline Buffer::MemoryMapping<PtrType>::~MemoryMapping()
{
    if (shouldFlush)
    {
        vmaFlushAllocation(buffer->allocator, buffer->allocation, 0, VK_WHOLE_SIZE);
    }

    if (shouldUnmap)
    {
        vmaUnmapMemory(buffer->allocator, buffer->allocation);
    }
}
} // namespace VOG::Graphics::Vulkan
