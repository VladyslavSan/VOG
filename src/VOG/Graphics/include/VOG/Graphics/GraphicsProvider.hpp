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

    /**
     * Create method that just calls constructor.
     *
     * @param options Dictionary with initialisation values
     *
     * @return shared pointer to GraphicsProvider
     */
    static std::shared_ptr<GraphicsProvider> create(const Common::JSONContainer& options);

    const vk::raii::Context& getContext() const;

    const vk::raii::Instance& getInstance() const;

    const vk::raii::PhysicalDevice& getPhysicalDevice() const;

    const Vulkan::Device& getDevice() const;

    const Vulkan::Queue& getGraphicsQueue() const;

    const Vulkan::Queue& getTransferQueue() const;

    const std::shared_ptr<Vulkan::MemoryAllocator>& getMemoryAllocator() const;

    vk::raii::CommandPool makeCommandPool(CommandPoolType commandPoolType) const;

protected:
    const vk::raii::Context        mContext;
    const vk::raii::Instance       mInstance;
    const vk::raii::PhysicalDevice mPhysicalDevice;
    const QueueInfos               mQueueInfos;
    Vulkan::Device                 mDevice;
    Vulkan::Queue                  mGraphicsQueue;
    Vulkan::Queue                  mTransferQueue;

    std::shared_ptr<Vulkan::MemoryAllocator> mMemoryAllocator;
};
VOG_DECLARE_PTR(GraphicsProvider);

inline const vk::raii::Context&
GraphicsProvider::getContext() const
{
    return mContext;
}

inline const vk::raii::Instance&
GraphicsProvider::getInstance() const
{
    return mInstance;
}

inline std::shared_ptr<GraphicsProvider>
GraphicsProvider::create(const Common::JSONContainer& options)
{
    return std::make_shared<GraphicsProvider>(options);
}

inline const vk::raii::PhysicalDevice&
GraphicsProvider::getPhysicalDevice() const
{
    return mPhysicalDevice;
}

inline const Vulkan::Device&
GraphicsProvider::getDevice() const
{
    return mDevice;
}

inline const Vulkan::Queue&
GraphicsProvider::getGraphicsQueue() const
{
    return mGraphicsQueue;
}

inline const Vulkan::Queue&
GraphicsProvider::getTransferQueue() const
{
    return mTransferQueue;
}

inline const std::shared_ptr<Vulkan::MemoryAllocator>&
GraphicsProvider::getMemoryAllocator() const
{
    return mMemoryAllocator;
}
} // namespace VOG::Graphics
