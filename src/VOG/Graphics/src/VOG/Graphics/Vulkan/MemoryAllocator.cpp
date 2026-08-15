#include "VOG/Graphics/Vulkan/MemoryAllocator.hpp"

#include <VOG/Graphics/Config/VmaConfig.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace VOG::Graphics::Vulkan
{
namespace
{
VmaAllocation
toVmaAllocation(MemoryAllocator::AllocationHandle handle)
{
    return reinterpret_cast<VmaAllocation>(handle.value);
}

MemoryAllocator::AllocationHandle
toAllocationHandle(VmaAllocation allocation)
{
    return {reinterpret_cast<std::uintptr_t>(allocation)};
}

VmaMemoryUsage
toVmaMemoryUsage(MemoryAllocator::MemoryPreference preference)
{
    switch (preference)
    {
    case MemoryAllocator::MemoryPreference::eDevice:
        return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    case MemoryAllocator::MemoryPreference::eHost:
        return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    case MemoryAllocator::MemoryPreference::eAuto:
        break;
    }

    return VMA_MEMORY_USAGE_AUTO;
}

VmaAllocationCreateFlags
toVmaAllocationFlags(const MemoryAllocator::AllocationParameters& parameters)
{
    VmaAllocationCreateFlags flags = 0;

    switch (parameters.hostAccess)
    {
    case MemoryAllocator::HostAccess::eSequentialWrite:
        flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        break;
    case MemoryAllocator::HostAccess::eRandom:
        flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        break;
    case MemoryAllocator::HostAccess::eNone:
        break;
    }

    if (parameters.persistentlyMapped)
    {
        flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    return flags;
}
} // namespace

/** Holds every allocator-backend type, so no backend header escapes this translation unit. */
class MemoryAllocator::Implementation
{
public:
    explicit Implementation(Device& device);

    ~Implementation();

    Implementation(const Implementation&) = delete;
    Implementation(Implementation&&)      = delete;

    [[nodiscard]] vk::MemoryPropertyFlags getMemoryFlags(std::uint32_t memoryIndex) const;

    Device&      mDevice;
    VmaAllocator mAllocator;
};

MemoryAllocator::Implementation::Implementation(Device& device)
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

MemoryAllocator::Implementation::~Implementation() { vmaDestroyAllocator(mAllocator); }

vk::MemoryPropertyFlags
MemoryAllocator::Implementation::getMemoryFlags(const std::uint32_t memoryIndex) const
{
    VkMemoryPropertyFlags flags{};
    vmaGetMemoryTypeProperties(mAllocator, memoryIndex, &flags);

    return vk::MemoryPropertyFlags{flags};
}

MemoryAllocator::Allocation::Allocation(const AllocationHandle        handle,
                                        const vk::DeviceSize          size,
                                        std::byte* const              mappedData,
                                        const vk::MemoryPropertyFlags memoryFlags)
    : mHandle{handle}
    , mSize{size}
    , mMappedData{mappedData}
    , mMemoryFlags{memoryFlags}
{
}

MemoryAllocator::MemoryAllocator(Device& device)
    : mImplementation{std::make_unique<Implementation>(device)}
{
}

MemoryAllocator::~MemoryAllocator() = default;

void
MemoryAllocator::free(const Allocation& allocation) noexcept
{
    if (allocation.mHandle)
    {
        vmaFreeMemory(mImplementation->mAllocator, toVmaAllocation(allocation.mHandle));
    }
}

std::byte*
MemoryAllocator::map(const Allocation& allocation)
{
    void*      data = nullptr;
    const auto result =
        vmaMapMemory(mImplementation->mAllocator, toVmaAllocation(allocation.mHandle), &data);
    if (result != VK_SUCCESS) [[unlikely]]
    {
        throw std::runtime_error{"MemoryAllocator::map: vmaMapMemory failed."};
    }

    return static_cast<std::byte*>(data);
}

void
MemoryAllocator::unmap(const Allocation& allocation) noexcept
{
    vmaUnmapMemory(mImplementation->mAllocator, toVmaAllocation(allocation.mHandle));
}

void
MemoryAllocator::flush(const Allocation& allocation) noexcept
{
    vmaFlushAllocation(
        mImplementation->mAllocator, toVmaAllocation(allocation.mHandle), 0, VK_WHOLE_SIZE);
}

void
MemoryAllocator::invalidate(const Allocation& allocation) noexcept
{
    vmaInvalidateAllocation(
        mImplementation->mAllocator, toVmaAllocation(allocation.mHandle), 0, VK_WHOLE_SIZE);
}

std::unique_ptr<Buffer>
MemoryAllocator::makeBuffer(const vk::BufferCreateInfo& createInfo,
                            const AllocationParameters& parameters)
{
    VmaAllocationCreateInfo vmaAllocationCreateInfo = {
        .flags     = toVmaAllocationFlags(parameters),
        .usage     = toVmaMemoryUsage(parameters.memory),
        .pUserData = const_cast<char*>(parameters.tag),
    };

    VkBuffer          buffer;
    VmaAllocation     allocation;
    VmaAllocationInfo allocationInfo;

    Device& device = mImplementation->mDevice;

    vk::Result result =
        static_cast<vk::Result>(vmaCreateBuffer(mImplementation->mAllocator,
                                                &static_cast<const VkBufferCreateInfo&>(createInfo),
                                                &vmaAllocationCreateInfo,
                                                &buffer,
                                                &allocation,
                                                &allocationInfo));

    if (result != vk::Result::eSuccess || buffer == VK_NULL_HANDLE)
    {
        throw std::runtime_error{"Buffer creation failed."};
    }

    return std::unique_ptr<Buffer>(
        new Buffer{device.shared_from_this(),
                   Allocation{toAllocationHandle(allocation),
                              allocationInfo.size,
                              static_cast<std::byte*>(allocationInfo.pMappedData),
                              mImplementation->getMemoryFlags(allocationInfo.memoryType)},
                   vk::raii::Buffer{device, buffer}});
}
} // namespace VOG::Graphics::Vulkan
