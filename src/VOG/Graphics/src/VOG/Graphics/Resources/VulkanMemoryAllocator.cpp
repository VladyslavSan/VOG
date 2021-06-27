#include "VulkanMemoryAllocator.hpp"

#include <VOG/Graphics/Api/GraphicsProvider.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <stdexcept>
#include <vma/vma_configure.h>

namespace VOG::Graphics::Resources
{
VulkanMemoryAllocator::VulkanMemoryAllocator(const Api::GraphicsProviderPtr& graphicsProvider)
    : m_graphicsProvider{graphicsProvider}
    , m_vmaHandle{nullptr}
{
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = *m_graphicsProvider->GetPhysicalDevice();
    allocatorInfo.device = *m_graphicsProvider->GetDevice();

    VmaVulkanFunctions vulkanFunctions;
    {
        auto callDispatcherDevice = m_graphicsProvider->GetDevice().getDispatcher();

        vulkanFunctions.vkAllocateMemory = callDispatcherDevice->vkAllocateMemory;
        vulkanFunctions.vkBindBufferMemory = callDispatcherDevice->vkBindBufferMemory;
        vulkanFunctions.vkBindImageMemory = callDispatcherDevice->vkBindImageMemory;

        vulkanFunctions.vkCmdCopyBuffer = callDispatcherDevice->vkCmdCopyBuffer;
        vulkanFunctions.vkCreateBuffer = callDispatcherDevice->vkCreateBuffer;
        vulkanFunctions.vkCreateImage = callDispatcherDevice->vkCreateImage;
        vulkanFunctions.vkDestroyBuffer = callDispatcherDevice->vkDestroyBuffer;
        vulkanFunctions.vkDestroyImage = callDispatcherDevice->vkDestroyImage;
        vulkanFunctions.vkFlushMappedMemoryRanges = callDispatcherDevice->vkFlushMappedMemoryRanges;
        vulkanFunctions.vkFreeMemory = callDispatcherDevice->vkFreeMemory;
        vulkanFunctions.vkGetBufferMemoryRequirements =
            callDispatcherDevice->vkGetBufferMemoryRequirements;
        vulkanFunctions.vkGetImageMemoryRequirements =
            callDispatcherDevice->vkGetImageMemoryRequirements;
        vulkanFunctions.vkMapMemory = callDispatcherDevice->vkMapMemory;
        vulkanFunctions.vkUnmapMemory = callDispatcherDevice->vkUnmapMemory;
        vulkanFunctions.vkInvalidateMappedMemoryRanges =
            callDispatcherDevice->vkInvalidateMappedMemoryRanges;
    }

    {
        auto callDispatcherInstance = m_graphicsProvider->GetInstance().getDispatcher();
        vulkanFunctions.vkGetPhysicalDeviceMemoryProperties =
            callDispatcherInstance->vkGetPhysicalDeviceMemoryProperties;
        vulkanFunctions.vkGetPhysicalDeviceProperties =
            callDispatcherInstance->vkGetPhysicalDeviceProperties;
    }

    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    VmaAllocator vmaAllocator;
    auto result = vmaCreateAllocator(&allocatorInfo, &vmaAllocator);
    if (result != VK_SUCCESS)
        throw std::runtime_error("VulkanMemoryAllocator creation failed.");

    static_assert(sizeof(VmaAllocator) == sizeof(void*));
    void* ptr = nullptr;
    std::memcpy(&ptr, &vmaAllocator, sizeof(VmaAllocator));
    m_vmaHandle = std::shared_ptr<void>(ptr, [](void* ptr) -> void {
        if (ptr)
            vmaDestroyAllocator(reinterpret_cast<VmaAllocator>(ptr));
    });
}
} // namespace VOG::Graphics::Resources
