#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Queue.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace VOG::Common
{
class JSONContainer;
}

namespace VOG::Graphics::Vulkan
{
class MemoryAllocator;
}

namespace VOG::Graphics
{
/**
 * Class to hold many of the essential Vulkan objects like instance, physical
 * device, device, queues etc.
 *
 * @note Will be as a std::shared_ptr member in many other vulkan related
 * objects
 */
class GraphicsProvider
{
public:
    struct QueueFamilyInfo
    {
        std::uint32_t             familyIndex = 0u;
        vk::QueueFamilyProperties familyProperties{};
    };

    struct QueueInfos
    {
        QueueFamilyInfo graphics;
        QueueFamilyInfo transfer;
    };

    enum class CommandPoolType
    {
        eGraphics,
        eTransfer
    };

    ~GraphicsProvider();

    /**
     * Constructor which initialises the Vulkan Core
     *
     * @param options Dictionary with initialisation values
     *
     * @throw std::runtime_error in case of any error during construction
     *
     * @note @p options may or may not contain "app_name", "engine_name",
     * "layers", "extensions" options
     */
    GraphicsProvider(const Common::JSONContainer& options);

    vk::raii::Context&
    getContext()
    {
        return mContext;
    }
    const vk::raii::Context&
    getContext() const
    {
        return mContext;
    }

    vk::raii::Instance&
    getInstance()
    {
        return mInstance;
    }
    const vk::raii::Instance&
    getInstance() const
    {
        return mInstance;
    }

    vk::raii::PhysicalDevice&
    getPhysicalDevice()
    {
        return mPhysicalDevice;
    }
    const vk::raii::PhysicalDevice&
    getPhysicalDevice() const
    {
        return mPhysicalDevice;
    }

    Vulkan::Device&
    getDevice()
    {
        return mDevice;
    }
    const Vulkan::Device&
    getDevice() const
    {
        return mDevice;
    }

    Vulkan::Queue&
    getGraphicsQueue()
    {
        return mGraphicsQueue;
    }
    const Vulkan::Queue&
    getGraphicsQueue() const
    {
        return mGraphicsQueue;
    }

    Vulkan::Queue&
    getTransferQueue()
    {
        return mTransferQueue;
    }
    const Vulkan::Queue&
    getTransferQueue() const
    {
        return mTransferQueue;
    }

    const std::shared_ptr<Vulkan::MemoryAllocator>&
    getMemoryAllocator() const
    {
        return mMemoryAllocator;
    }

    vk::raii::CommandPool makeCommandPool(CommandPoolType commandPoolType) const;

protected:
    vk::raii::Context        mContext;
    vk::raii::Instance       mInstance;
    vk::raii::PhysicalDevice mPhysicalDevice;
    const QueueInfos         mQueueInfos;
    Vulkan::Device           mDevice;
    Vulkan::Queue            mGraphicsQueue;
    Vulkan::Queue            mTransferQueue;

    std::shared_ptr<Vulkan::MemoryAllocator> mMemoryAllocator;
};

VOG_DECLARE_PTR(GraphicsProvider);
} // namespace VOG::Graphics
