#pragma once

#include <VOG/Graphics/Vulkan/MemoryAllocator.hpp>

#include <cstddef>
#include <span>

namespace VOG::Graphics::Vulkan
{
class Buffer
    : protected MemoryAllocator::Allocation
    , private vk::raii::Buffer
{
    friend class MemoryAllocator;

public:
    using vk::raii::Buffer::operator*;

    /** Scope guard for a mapped buffer: the destructor flushes (if it was mapped for writing, and
     *  the memory needs it) and unmaps. */
    class MemoryMapping
    {
        friend class Buffer;

        MemoryMapping(std::span<std::byte> bytes, const Buffer& buffer, bool shouldFlush) noexcept
            : mBytes{bytes}
            , mBuffer{buffer}
            , mShouldFlush{shouldFlush}
        {
        }

    public:
        ~MemoryMapping();

        MemoryMapping(const MemoryMapping&)            = delete;
        MemoryMapping(MemoryMapping&&)                 = delete;
        MemoryMapping& operator=(const MemoryMapping&) = delete;
        MemoryMapping& operator=(MemoryMapping&&)      = delete;

        /**
         * The mapped memory, for writing or, after mapForRead(), for reading: all the memory behind
         * the buffer, which may be larger than Buffer::byteSize(). Use it only while the mapping
         * is alive.
         */
        [[nodiscard]] std::span<std::byte>
        bytes() const noexcept
        {
            return mBytes;
        }

    private:
        std::span<std::byte> mBytes;
        const Buffer&        mBuffer;

        /** If true, the destructor flushes to commit the writes and make them visible to the GPU.
         */
        bool mShouldFlush;
    };

    /**
     * Maps the buffer for writing. Only for a buffer created with
     * AllocationFlag::eHostSequentialWrite: without it VMA may place the buffer in memory the CPU
     * cannot map.
     */
    [[nodiscard]] MemoryMapping mapForWrite();

    /** A mapping for reading: the memory can be read and not written. */
    class ReadMapping
    {
        friend class Buffer;

        ReadMapping(std::span<std::byte> bytes, const Buffer& buffer) noexcept
            : mMapping{bytes, buffer, false}
        {
        }

    public:
        [[nodiscard]] std::span<const std::byte>
        bytes() const noexcept
        {
            return mMapping.bytes();
        }

    private:
        MemoryMapping mMapping;
    };

    /**
     * Maps the buffer for reading, after the GPU finished writing it and a barrier made the writes
     * visible to the host (stage eHost, access eHostRead). Only for a buffer created with
     * AllocationFlag::eHostRandomAccess. Where the memory is not host-coherent, the mapping
     * invalidates the CPU's caches first, so the reads see what the GPU wrote.
     *
     * @throws std::runtime_error if the memory cannot be mapped or invalidated.
     */
    [[nodiscard]] ReadMapping mapForRead();

    /** The size the buffer was created with; the memory behind it may be larger. */
    vk::DeviceSize
    byteSize() const noexcept
    {
        return mByteSize;
    }

    /** GPU address of the buffer. Only for a buffer created with eShaderDeviceAddress usage. */
    [[nodiscard]] vk::DeviceAddress deviceAddress() const;

private:
    Buffer(MemoryAllocator::Allocation allocation,
           vk::raii::Buffer            buffer,
           vk::DeviceSize              byteSize);

    vk::DeviceSize mByteSize;
};
} // namespace VOG::Graphics::Vulkan
