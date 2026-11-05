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

    /** Scope guard for a mapped buffer: the destructor flushes (if needed) and unmaps. */
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
         * The mapped memory, for writing: all the memory behind the buffer, which may be larger
         * than Buffer::byteSize(). Use it only while the mapping is alive.
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
