#pragma once

#include <VOG/Graphics/Typedefs.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace vk
{
struct QueueFamilyProperties;
namespace raii
{
class Context;
class CommandPool;
class CommandBuffer;
class Device;
class Instance;
class PipelineCache;
class PhysicalDevice;
class Queue;
} // namespace raii
} // namespace vk

namespace VOG::Common
{
class JSONContainer;
}

namespace VOG::Graphics
{
namespace Api
{
/**
 * Class to hold many of the essential Vulkan objects like instance, physical device, device,
 * queues etc.
 *
 * @note Will be as a std::shared_ptr member in many other vulkan related objects
 */
class GraphicsProvider
{
public:
    using QueueFamilyInfo = std::pair<std::uint32_t, std::unique_ptr<vk::QueueFamilyProperties>>;

    enum class CommandPoolType
    {
        Graphics,
        Transfer,
        Present
    };

    ~GraphicsProvider();

    /**
     * Constructor which initialises the Vulkan API
     *
     * @param options Dictionary with initialisation values
     *
     * @throw std::runtime_error in case of any error during construction
     *
     * @note @p options may or may not contain "app_name", "engine_name", "layers", "extensions"
     * options
     */
    GraphicsProvider(const Common::JSONContainer& options);

    const std::shared_ptr<vk::raii::Context>& GetContext() const;

    const std::shared_ptr<vk::raii::Instance>& GetInstance() const;

    const std::shared_ptr<vk::raii::PhysicalDevice>& GetPhysicalDevice() const;

    const std::shared_ptr<vk::raii::Device>& GetDevice() const;

    const QueueFamilyInfo& GetGraphicsQueueInfo() const;

    const std::shared_ptr<vk::raii::Queue>& GetGraphicsQueue() const;

    const QueueFamilyInfo& GetTransferQueueInfo() const;

    const std::shared_ptr<vk::raii::Queue>& GetTransferQueue() const;

    std::shared_ptr<vk::raii::CommandPool> MakeCommandPool(CommandPoolType commandPoolType) const;

protected:
    // Generic
    std::shared_ptr<vk::raii::Context> m_context;
    std::shared_ptr<vk::raii::Instance> m_instance;
    std::shared_ptr<vk::raii::PhysicalDevice> m_physicalDevice;

    // Queues
    QueueFamilyInfo m_graphicsQueueInfo;
    QueueFamilyInfo m_transferQueueInfo;

    std::shared_ptr<vk::raii::Device> m_device;
    std::shared_ptr<vk::raii::Queue> m_graphicsQueue;
    std::shared_ptr<vk::raii::Queue> m_transferQueue;
};

VOG_DECLARE_PTR(GraphicsProvider);
} // namespace Api
} // namespace VOG::Graphics
