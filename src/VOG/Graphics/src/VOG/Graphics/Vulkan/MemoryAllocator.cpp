#include "VOG/Graphics/Vulkan/MemoryAllocator.hpp"

#include <VOG/Graphics/Config/VmaConfig.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace VOG::Graphics::Vulkan
{
namespace
{
vk::MemoryPropertyFlags
getMemoryFlags(VmaAllocator allocator, std::uint32_t memoryIndex)
{
    VkMemoryPropertyFlags flags{};
    vmaGetMemoryTypeProperties(allocator, memoryIndex, &flags);

    return vk::MemoryPropertyFlags{flags};
}

VmaMemoryUsage
toVmaMemoryUsage(Buffer::MemoryPreference preference)
{
    switch (preference)
    {
    case Buffer::MemoryPreference::eDevice:
        return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    case Buffer::MemoryPreference::eHost:
        return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    case Buffer::MemoryPreference::eAuto:
        break;
    }

    return VMA_MEMORY_USAGE_AUTO;
}

VmaAllocationCreateFlags
toVmaAllocationFlags(const Buffer::AllocationParameters& parameters)
{
    VmaAllocationCreateFlags flags = 0;

    switch (parameters.hostAccess)
    {
    case Buffer::HostAccess::eSequentialWrite:
        flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        break;
    case Buffer::HostAccess::eRandom:
        flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        break;
    case Buffer::HostAccess::eNone:
        break;
    }

    if (parameters.persistentlyMapped)
    {
        flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    return flags;
}
} // namespace

Buffer::Allocation::Allocation(DevicePtr                     _device,
                               const VmaAllocator            _allocator,
                               const vk::Buffer              _buffer,
                               const VmaAllocation           _allocation,
                               std::byte* const              _mappedData,
                               const std::size_t             _size,
                               const bool                    _persistentlyMapped,
                               const vk::MemoryPropertyFlags _memoryFlags)
    : device{std::move(_device)}
    , allocator{_allocator}
    , buffer{_buffer}
    , allocation{_allocation}
    , mappedData{_mappedData}
    , size{_size}
    , isPersistentlyMapped{_persistentlyMapped}
    , memoryFlags{_memoryFlags}
{
}

Buffer::Allocation::~Allocation()
{
    if (allocation != nullptr)
    {
        vmaDestroyBuffer(allocator, static_cast<VkBuffer>(buffer), allocation);
    }
}

Buffer::Allocation::Allocation(Buffer::Allocation&& other) noexcept
    : device{std::move(other.device)}
    , allocator{other.allocator}
    , buffer{other.buffer}
    , allocation{std::exchange(other.allocation, nullptr)}
    , mappedData{other.mappedData}
    , size{other.size}
    , isPersistentlyMapped{other.isPersistentlyMapped}
    , memoryFlags{other.memoryFlags}
{
}

MemoryAllocator::MemoryAllocator(Device& device)
    : mDevice{device}
    , mAllocator{nullptr}
{
    const auto* instanceD = mDevice.instance->getDispatcher();
    const auto* deviceD   = mDevice.getDispatcher();

    VmaVulkanFunctions vulkanFunctions = {
        .vkGetPhysicalDeviceProperties           = instanceD->vkGetPhysicalDeviceProperties,
        .vkGetPhysicalDeviceMemoryProperties     = instanceD->vkGetPhysicalDeviceMemoryProperties,
        .vkAllocateMemory                        = deviceD->vkAllocateMemory,
        .vkFreeMemory                            = deviceD->vkFreeMemory,
        .vkMapMemory                             = deviceD->vkMapMemory,
        .vkUnmapMemory                           = deviceD->vkUnmapMemory,
        .vkFlushMappedMemoryRanges               = deviceD->vkFlushMappedMemoryRanges,
        .vkInvalidateMappedMemoryRanges          = deviceD->vkInvalidateMappedMemoryRanges,
        .vkBindBufferMemory                      = deviceD->vkBindBufferMemory,
        .vkBindImageMemory                       = deviceD->vkBindImageMemory,
        .vkGetBufferMemoryRequirements           = deviceD->vkGetBufferMemoryRequirements,
        .vkGetImageMemoryRequirements            = deviceD->vkGetImageMemoryRequirements,
        .vkCreateBuffer                          = deviceD->vkCreateBuffer,
        .vkDestroyBuffer                         = deviceD->vkDestroyBuffer,
        .vkCreateImage                           = deviceD->vkCreateImage,
        .vkDestroyImage                          = deviceD->vkDestroyImage,
        .vkCmdCopyBuffer                         = deviceD->vkCmdCopyBuffer,
        .vkGetBufferMemoryRequirements2KHR       = deviceD->vkGetBufferMemoryRequirements2,
        .vkGetImageMemoryRequirements2KHR        = deviceD->vkGetImageMemoryRequirements2,
        .vkBindBufferMemory2KHR                  = deviceD->vkBindBufferMemory2,
        .vkBindImageMemory2KHR                   = deviceD->vkBindImageMemory2,
        .vkGetPhysicalDeviceMemoryProperties2KHR = instanceD->vkGetPhysicalDeviceMemoryProperties2,
        .vkGetDeviceBufferMemoryRequirements     = deviceD->vkGetDeviceBufferMemoryRequirements,
        .vkGetDeviceImageMemoryRequirements      = deviceD->vkGetDeviceImageMemoryRequirements,
    };

    VmaAllocatorCreateInfo allocatorInfo = {
        .physicalDevice   = *static_cast<const PhysicalDevice&>(mDevice),
        .device           = *mDevice,
        .pVulkanFunctions = &vulkanFunctions,
        .instance         = **mDevice.instance,
        .vulkanApiVersion = mDevice.getProperties2().properties.apiVersion,
    };

    auto result = vmaCreateAllocator(&allocatorInfo, &mAllocator);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("MemoryAllocator creation failed.");
    }
}

MemoryAllocator::~MemoryAllocator() { vmaDestroyAllocator(mAllocator); }

std::unique_ptr<Buffer>
MemoryAllocator::makeBuffer(const vk::BufferCreateInfo&         createInfo,
                            const Buffer::AllocationParameters& parameters)
{
    VmaAllocationCreateInfo vmaAllocationCreateInfo = {
        .flags     = toVmaAllocationFlags(parameters),
        .usage     = toVmaMemoryUsage(parameters.memory),
        .pUserData = const_cast<char*>(parameters.tag),
    };

    VkBuffer          buffer;
    VmaAllocation     allocation;
    VmaAllocationInfo allocationInfo;

    vk::Result result =
        static_cast<vk::Result>(vmaCreateBuffer(mAllocator,
                                                &static_cast<const VkBufferCreateInfo&>(createInfo),
                                                &vmaAllocationCreateInfo,
                                                &buffer,
                                                &allocation,
                                                &allocationInfo));

    if (result != vk::Result::eSuccess || buffer == VK_NULL_HANDLE)
    {
        throw std::runtime_error{"Buffer creation failed."};
    }

    const bool isPersistentlyMapped =
        (vmaAllocationCreateInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) != 0;

    return std::unique_ptr<Buffer>(new Buffer{std::make_unique<Buffer::Allocation>(
        mDevice.shared_from_this(),
        mAllocator,
        vk::Buffer{buffer},
        allocation,
        static_cast<std::byte*>(allocationInfo.pMappedData),
        allocationInfo.size,
        isPersistentlyMapped,
        getMemoryFlags(mAllocator, allocationInfo.memoryType))});
}
} // namespace VOG::Graphics::Vulkan
