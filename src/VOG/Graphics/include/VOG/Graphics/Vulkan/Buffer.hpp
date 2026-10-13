#pragma once

#include <VOG/Graphics/Vulkan/MemoryAllocator.hpp>

#include <cstddef>

namespace VOG::Graphics::Vulkan
{
class Buffer
    : protected MemoryAllocator::Allocation
    , private vk::raii::Buffer
{
    friend class MemoryAllocator;

public:
    using vk::raii::Buffer::operator*;

    /** Scope guard for a mapped buffer: the destructor flushes (if needed) and unmaps. */
    struct MemoryMapping
    {
        /** Pointer for writing. */
        std::byte* data;

        /** Size that defines max number bytes that can be accessed by the @var data pointer. */
        const std::size_t size;

        /** Buffer pointer used to unmap and flush the allocation. */
        const Buffer* buffer;

        /** If true, the destructor flushes to commit the writes and make them visible to the GPU.
         */
        const bool shouldFlush;

        MemoryMapping(std::byte* _data, std::size_t _size, const Buffer* _buffer, bool _shouldFlush)
            : data{_data}
            , size{_size}
            , buffer{_buffer}
            , shouldFlush{_shouldFlush}
        {
        }

        ~MemoryMapping();

        MemoryMapping(const MemoryMapping&) = delete;
        MemoryMapping(MemoryMapping&&)      = delete;
    };

    /**
     * Maps the buffer for writing. Only for a buffer created with
     * AllocationFlag::eHostSequentialWrite: without it VMA may place the buffer in memory the CPU
     * cannot map.
     */
    [[nodiscard]] MemoryMapping mapForWrite();

private:
    Buffer(MemoryAllocator::Allocation allocation, vk::raii::Buffer buffer);
};
} // namespace VOG::Graphics::Vulkan
