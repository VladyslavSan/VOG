#include "VOG/Graphics/Vulkan/MemoryAllocator.hpp"

#include <VOG/Graphics/Config/VMAConfig.hpp>
#include <VOG/Graphics/GraphicsProvider.hpp>

#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
namespace
{
template <typename T>
T
as(std::uintptr_t handle)
{
    return reinterpret_cast<T>(handle);
}
} // namespace

Allocation::Allocation(MemoryAllocator* allocator,
                       std::uintptr_t   allocationHandle,
                       std::uintptr_t   allocationInfo)
    : mAllocator(allocator)
    , mAllocationHandle{allocationHandle}
    , mAllocationInfo{allocationInfo}
{
}

Allocation::~Allocation()
{
    if (mAllocationHandle != 0u)
    {
        vmaFreeMemory(as<VmaAllocator>(mAllocator->getAllocatorHandle()),
                      as<VmaAllocation>(mAllocationHandle));
    }
}

std::uint32_t
Allocation::vulkanMemoryType() const
{
    return as<VmaAllocationInfo*>(mAllocationInfo)->memoryType;
}

VkDeviceMemory
Allocation::vulkanDeviceMemory() const
{
    return as<VmaAllocationInfo*>(mAllocationInfo)->deviceMemory;
}

std::uint64_t
Allocation::size() const
{
    return as<VmaAllocationInfo*>(mAllocationInfo)->size;
}

std::uint64_t
Allocation::offset() const
{
    return as<VmaAllocationInfo*>(mAllocationInfo)->offset;
}

std::byte*
Allocation::mappedMemory() const
{
    return reinterpret_cast<std::byte*>(as<VmaAllocationInfo*>(mAllocationInfo)->pMappedData);
}

BufferAllocation::~BufferAllocation() {}

MemoryAllocator::MemoryAllocator(const GraphicsProvider& graphicsProvider)
    : mGraphicsProvider{graphicsProvider}
    , mAllocatorHandle{0u}
{

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice         = *mGraphicsProvider.getPhysicalDevice();
    allocatorInfo.device                 = *mGraphicsProvider.getDevice();

    VmaVulkanFunctions vulkanFunctions;
    {
        auto callDispatcherDevice = mGraphicsProvider.getDevice().getDispatcher();

        vulkanFunctions.vkAllocateMemory   = callDispatcherDevice->vkAllocateMemory;
        vulkanFunctions.vkBindBufferMemory = callDispatcherDevice->vkBindBufferMemory;
        vulkanFunctions.vkBindImageMemory  = callDispatcherDevice->vkBindImageMemory;

        vulkanFunctions.vkCmdCopyBuffer           = callDispatcherDevice->vkCmdCopyBuffer;
        vulkanFunctions.vkCreateBuffer            = callDispatcherDevice->vkCreateBuffer;
        vulkanFunctions.vkCreateImage             = callDispatcherDevice->vkCreateImage;
        vulkanFunctions.vkDestroyBuffer           = callDispatcherDevice->vkDestroyBuffer;
        vulkanFunctions.vkDestroyImage            = callDispatcherDevice->vkDestroyImage;
        vulkanFunctions.vkFlushMappedMemoryRanges = callDispatcherDevice->vkFlushMappedMemoryRanges;
        vulkanFunctions.vkFreeMemory              = callDispatcherDevice->vkFreeMemory;
        vulkanFunctions.vkGetBufferMemoryRequirements =
            callDispatcherDevice->vkGetBufferMemoryRequirements;
        vulkanFunctions.vkGetImageMemoryRequirements =
            callDispatcherDevice->vkGetImageMemoryRequirements;
        vulkanFunctions.vkMapMemory   = callDispatcherDevice->vkMapMemory;
        vulkanFunctions.vkUnmapMemory = callDispatcherDevice->vkUnmapMemory;
        vulkanFunctions.vkInvalidateMappedMemoryRanges =
            callDispatcherDevice->vkInvalidateMappedMemoryRanges;
    }

    {
        auto callDispatcherInstance = mGraphicsProvider.getInstance().getDispatcher();
        vulkanFunctions.vkGetPhysicalDeviceMemoryProperties =
            callDispatcherInstance->vkGetPhysicalDeviceMemoryProperties;
        vulkanFunctions.vkGetPhysicalDeviceProperties =
            callDispatcherInstance->vkGetPhysicalDeviceProperties;
    }

    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    VmaAllocator allocator;
    auto         result = vmaCreateAllocator(&allocatorInfo, &allocator);
    if (result != VK_SUCCESS)
        throw std::runtime_error("MemoryAllocator creation failed.");

    static_assert(sizeof(VmaAllocator) == sizeof(decltype(mAllocatorHandle)), "");
    mAllocatorHandle = reinterpret_cast<std::uintptr_t>(allocator);
}

MemoryAllocator::~MemoryAllocator() { vmaDestroyAllocator(as<VmaAllocator>(mAllocatorHandle)); }

std::uintptr_t
MemoryAllocator::getAllocatorHandle() const
{
    return mAllocatorHandle;
}
} // namespace VOG::Graphics::Vulkan
