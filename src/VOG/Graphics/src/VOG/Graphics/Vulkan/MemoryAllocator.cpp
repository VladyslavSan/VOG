#include "VOG/Graphics/Vulkan/MemoryAllocator.hpp"

#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>

#include <stdexcept>

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
} // namespace

MemoryAllocator::AllocationParameters::
operator VmaAllocationCreateInfo() const noexcept
{
    return {
        .flags          = flags,
        .usage          = usage,
        .requiredFlags  = static_cast<VkMemoryPropertyFlags>(requiredFlags),
        .preferredFlags = static_cast<VkMemoryPropertyFlags>(preferredFlags),
        .pUserData      = const_cast<char*>(tag),
    };
}

MemoryAllocator::Allocation::Allocation(MemoryAllocatorPtr         _allocator,
                                        const VmaAllocation        _allocation,
                                        const VmaAllocationInfo    _info,
                                        const AllocationParameters _createInfo)
    : allocator{std::move(_allocator)}
    , allocation{_allocation}
    , info{_info}
    , createInfo{_createInfo}
    , isPersistentlyMapped{(_createInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) != 0}
    , memoryFlags{getMemoryFlags(*allocator, info.memoryType)}
{
}

MemoryAllocator::Allocation::~Allocation()
{
    if (allocation != nullptr)
    {
        vmaFreeMemory(*allocator, allocation);
    }
}

MemoryAllocator::Allocation::Allocation(MemoryAllocator::Allocation&& other) noexcept
    : allocator{std::move(other.allocator)}
    , allocation{std::exchange(other.allocation, nullptr)}
    , info{other.info}
    , createInfo{other.createInfo}
    , isPersistentlyMapped{other.isPersistentlyMapped}
    , memoryFlags{other.memoryFlags}
{
}

MemoryAllocator::MemoryAllocator(const DevicePtr& device)
    : mDevice{device}
    , mAllocator{nullptr}
{
    const auto* instanceD = mDevice->instance->getDispatcher();
    const auto* deviceD   = mDevice->getDispatcher();

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
        .physicalDevice   = *mDevice->getPhysicalDevice(),
        .device           = **mDevice,
        .pVulkanFunctions = &vulkanFunctions,
        .instance         = **mDevice->instance,
        .vulkanApiVersion = mDevice->getPhysicalDevice().getProperties2().properties.apiVersion,
    };

    auto result = vmaCreateAllocator(&allocatorInfo, &mAllocator);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("MemoryAllocator creation failed.");
    }
} // namespace VOG::Graphics::Vulkan

MemoryAllocator::~MemoryAllocator() { vmaDestroyAllocator(mAllocator); }

std::unique_ptr<Buffer>
MemoryAllocator::makeBuffer(const vk::BufferCreateInfo& createInfo,
                            const AllocationParameters& allocationCreateInfo)
{
    VkBuffer                buffer;
    VmaAllocation           allocation;
    VmaAllocationInfo       allocationInfo;
    VmaAllocationCreateInfo vmaAllocationCreateInfo = allocationCreateInfo;

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

    return std::unique_ptr<Buffer>(
        new Buffer{Allocation{shared_from_this(), allocation, allocationInfo, allocationCreateInfo},
                   vk::raii::Buffer{*mDevice, buffer}});
}

MemoryAllocator::
operator VmaAllocator() const
{
    return mAllocator;
}
} // namespace VOG::Graphics::Vulkan
