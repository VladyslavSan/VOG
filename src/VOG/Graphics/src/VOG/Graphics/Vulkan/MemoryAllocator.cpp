#include "VOG/Graphics/Vulkan/MemoryAllocator.hpp"

#include <VOG/Graphics/GraphicsProvider.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>

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

MemoryAllocator::Allocation::Allocation(const MemoryAllocator*  _allocator,
                                        const VmaAllocation     _allocation,
                                        const VmaAllocationInfo _info,
                                        const AllocationInfo    _createInfo)
    : allocator{_allocator}
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
    : allocator{other.allocator}
    , allocation{std::exchange(other.allocation, nullptr)}
    , info{other.info}
    , createInfo{other.createInfo}
    , isPersistentlyMapped{other.isPersistentlyMapped}
    , memoryFlags{other.memoryFlags}
{
}

MemoryAllocator::MemoryAllocator(const GraphicsProvider& graphicsProvider)
    : mGraphicsProvider{graphicsProvider}
    , mAllocator{nullptr}
{

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice         = *mGraphicsProvider.getPhysicalDevice();
    allocatorInfo.device                 = *mGraphicsProvider.getDevice();
    allocatorInfo.instance               = *mGraphicsProvider.getInstance();

    VmaVulkanFunctions vulkanFunctions;
    {
        const auto* callDispatcherDevice = mGraphicsProvider.getDevice().getDispatcher();

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
        const auto* callDispatcherInstance = mGraphicsProvider.getInstance().getDispatcher();
        vulkanFunctions.vkGetPhysicalDeviceMemoryProperties =
            callDispatcherInstance->vkGetPhysicalDeviceMemoryProperties;
        vulkanFunctions.vkGetPhysicalDeviceProperties =
            callDispatcherInstance->vkGetPhysicalDeviceProperties;
    }

    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    auto result = vmaCreateAllocator(&allocatorInfo, &mAllocator);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("MemoryAllocator creation failed.");
    }
}

MemoryAllocator::~MemoryAllocator() { vmaDestroyAllocator(mAllocator); }

std::unique_ptr<Buffer>
MemoryAllocator::makeBuffer(const vk::BufferCreateInfo& createInfo,
                            const AllocationInfo&       allocationCreateInfo)
{
    VkBuffer                buffer;
    VmaAllocation           allocation;
    VmaAllocationInfo       allocationInfo;
    VmaAllocationCreateInfo vmaAllocationCreateInfo{
        .flags     = allocationCreateInfo.flags,
        .usage     = allocationCreateInfo.usage,
        .pUserData = const_cast<char*>(allocationCreateInfo.tag)};

    vk::Result result =
        static_cast<vk::Result>(vmaCreateBuffer(mAllocator,
                                                &static_cast<const VkBufferCreateInfo&>(createInfo),
                                                &vmaAllocationCreateInfo,
                                                &buffer,
                                                &allocation,
                                                &allocationInfo));

    if (result != vk::Result::eSuccess || buffer == VK_NULL_HANDLE)
    {
        throw std::runtime_error{""};
    }

    return std::make_unique<Buffer>(
        Allocation{this, allocation, allocationInfo, allocationCreateInfo},
        vk::raii::Buffer{mGraphicsProvider.getDevice(), buffer});
}

MemoryAllocator::operator VmaAllocator() const { return mAllocator; }
} // namespace VOG::Graphics::Vulkan
