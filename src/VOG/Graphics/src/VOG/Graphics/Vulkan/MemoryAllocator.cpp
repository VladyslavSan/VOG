#include "VOG/Graphics/Vulkan/MemoryAllocator.hpp"

#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/Texture.hpp>
#include <VOG/Graphics/Vulkan/VmaConfig.hpp>

#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
namespace
{
// VMA_VULKAN_VERSION encodes major * 1000000 + minor * 1000 + patch.
constexpr std::uint32_t gVmaMajorScale = 1000000u;
constexpr std::uint32_t gVmaMinorScale = 1000u;

static_assert(VK_API_VERSION_MAJOR(gRequiredApiVersion) * gVmaMajorScale +
                      VK_API_VERSION_MINOR(gRequiredApiVersion) * gVmaMinorScale <=
                  VMA_VULKAN_VERSION,
              "VMA is compiled for an older Vulkan than the instance requests; raise "
              "VMA_VULKAN_VERSION in Vulkan/VmaConfig.hpp.");

vk::MemoryPropertyFlags
getMemoryFlags(VmaAllocator allocator, std::uint32_t memoryIndex)
{
    VkMemoryPropertyFlags flags{};
    vmaGetMemoryTypeProperties(allocator, memoryIndex, &flags);

    return vk::MemoryPropertyFlags{flags};
}

VmaAllocationCreateInfo
toVma(const MemoryAllocator::AllocationParameters& parameters)
{
    VmaAllocationCreateFlags flags = 0u;
    if (parameters.flags & AllocationFlag::eHostSequentialWrite)
    {
        flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    return {.flags = flags, .usage = VMA_MEMORY_USAGE_AUTO};
}
} // namespace

MemoryAllocator::Allocation::Allocation(DevicePtr               _device,
                                        VmaAllocator_T*         _allocator,
                                        VmaAllocation_T*        _allocation,
                                        const vk::DeviceSize    _size,
                                        vk::MemoryPropertyFlags _memoryFlags,
                                        const AllocationFlags   _flags)
    : device{std::move(_device)}
    , allocator{_allocator}
    , allocation{_allocation}
    , size{_size}
    , flags{_flags}
    , memoryFlags{_memoryFlags}
{
}

MemoryAllocator::Allocation::~Allocation()
{
    if (allocation != nullptr)
    {
        vmaFreeMemory(allocator, allocation);
    }
}

MemoryAllocator::Allocation::Allocation(MemoryAllocator::Allocation&& other) noexcept
    : device{std::move(other.device)}
    , allocator{other.allocator}
    , allocation{std::exchange(other.allocation, nullptr)}
    , size{other.size}
    , flags{other.flags}
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
        .flags            = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice   = *static_cast<const PhysicalDevice&>(mDevice),
        .device           = *mDevice,
        .pVulkanFunctions = &vulkanFunctions,
        .instance         = **mDevice.instance,
        .vulkanApiVersion = gRequiredApiVersion,
    };

    auto result = vmaCreateAllocator(&allocatorInfo, &mAllocator);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("MemoryAllocator creation failed.");
    }
}

MemoryAllocator::~MemoryAllocator() { vmaDestroyAllocator(mAllocator); }

std::unique_ptr<Buffer>
MemoryAllocator::makeBuffer(const vk::BufferCreateInfo& createInfo,
                            const AllocationParameters& allocationCreateInfo)
{
    // Taken before vmaCreateBuffer, so nothing between the creation and the wrapping below can
    // throw.
    DevicePtr device = mDevice.shared_from_this();

    VkBuffer                buffer;
    VmaAllocation           allocation;
    VmaAllocationInfo       allocationInfo;
    VmaAllocationCreateInfo vmaAllocationCreateInfo = toVma(allocationCreateInfo);

    vk::Result result = static_cast<vk::Result>(
        vmaCreateBufferWithAlignment(mAllocator,
                                     &static_cast<const VkBufferCreateInfo&>(createInfo),
                                     &vmaAllocationCreateInfo,
                                     allocationCreateInfo.alignment,
                                     &buffer,
                                     &allocation,
                                     &allocationInfo));

    if (result != vk::Result::eSuccess || buffer == VK_NULL_HANDLE)
    {
        throw std::runtime_error{"Buffer creation failed."};
    }

    // Own both handles before anything else can throw, in the order Buffer's bases are declared:
    // the allocation first, the buffer second. If `new Buffer` throws (out of memory), the locals
    // destroy the buffer and then free the allocation, like a normal teardown, instead of leaking
    // the VkBuffer. Untested: only an allocation failure reaches it.
    Allocation       owningAllocation{std::move(device),
                                      mAllocator,
                                      allocation,
                                      allocationInfo.size,
                                      getMemoryFlags(mAllocator, allocationInfo.memoryType),
                                      allocationCreateInfo.flags};
    vk::raii::Buffer owningBuffer{mDevice.raii(), buffer};

    return std::unique_ptr<Buffer>(
        new Buffer{std::move(owningAllocation), std::move(owningBuffer), createInfo.size});
}

std::unique_ptr<Texture>
MemoryAllocator::makeTexture(const vk::ImageCreateInfo& createInfo, DescriptorHeap::Slot slot)
{
    // Taken before vmaCreateImage, like in makeBuffer.
    DevicePtr device = mDevice.shared_from_this();

    VkImage                 image;
    VmaAllocation           allocation;
    VmaAllocationInfo       allocationInfo;
    VmaAllocationCreateInfo vmaAllocationCreateInfo = toVma({});

    vk::Result result =
        static_cast<vk::Result>(vmaCreateImage(mAllocator,
                                               &static_cast<const VkImageCreateInfo&>(createInfo),
                                               &vmaAllocationCreateInfo,
                                               &image,
                                               &allocation,
                                               &allocationInfo));

    if (result != vk::Result::eSuccess || image == VK_NULL_HANDLE)
    {
        throw std::runtime_error{"Texture creation failed."};
    }

    // Owned in the order Texture's bases are declared, like in makeBuffer.
    Allocation      owningAllocation{std::move(device),
                                     mAllocator,
                                     allocation,
                                     allocationInfo.size,
                                     getMemoryFlags(mAllocator, allocationInfo.memoryType),
                                     AllocationFlags{}};
    vk::raii::Image owningImage{mDevice.raii(), image};

    return std::unique_ptr<Texture>(
        new Texture{std::move(owningAllocation),
                    std::move(owningImage),
                    std::move(slot),
                    {createInfo.extent.width, createInfo.extent.height}});
}
} // namespace VOG::Graphics::Vulkan
