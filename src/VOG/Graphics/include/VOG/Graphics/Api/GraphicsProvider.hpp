#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <memory>
#include <utility>
#include <vector>

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
    struct QueueFamilyInfo
    {
        std::uint32_t familyIndex = 0u;
        vk::QueueFamilyProperties familyProperties{};
    };

    struct QueueInfos
    {
        QueueFamilyInfo graphics;
        QueueFamilyInfo transfer;
    };

    enum class CommandPoolType
    {
        Graphics,
        Transfer
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

    // clang-format off
    vk::raii::Context& GetContext() { return mContext; }
    const vk::raii::Context& GetContext() const { return mContext; }

    vk::raii::Instance& GetInstance() { return mInstance; }
    const vk::raii::Instance& GetInstance() const { return mInstance; }

    vk::raii::PhysicalDevice& GetPhysicalDevice() { return mPhysicalDevice; }
    const vk::raii::PhysicalDevice& GetPhysicalDevice() const { return mPhysicalDevice; }

    vk::raii::Device& GetDevice() { return mDevice; }
    const vk::raii::Device& GetDevice() const { return mDevice; }

    const QueueInfos& GetQueueInfos() const { return mQueueInfos; }

    vk::raii::Queue& GetGraphicsQueue() { return mGraphicsQueue; }
    const vk::raii::Queue& GetGraphicsQueue() const { return mGraphicsQueue; }

    vk::raii::Queue& GetTransferQueue() { return mTransferQueue; }
    const vk::raii::Queue& GetTransferQueue() const { return mTransferQueue; }
    // clang-format on

    std::shared_ptr<vk::raii::CommandPool> MakeCommandPool(CommandPoolType commandPoolType) const;

protected:
    vk::raii::Context mContext;
    vk::raii::Instance mInstance;
    vk::raii::PhysicalDevice mPhysicalDevice;
    QueueInfos mQueueInfos;
    vk::raii::Device mDevice;
    vk::raii::Queue mGraphicsQueue;
    vk::raii::Queue mTransferQueue;
};

VOG_DECLARE_PTR(GraphicsProvider);
} // namespace Api
} // namespace VOG::Graphics
