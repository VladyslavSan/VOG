#include "VOG/Graphics/Api/GraphicsProvider.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <stdexcept>

namespace VOG::Graphics::Api
{
namespace
{
std::pair<GraphicsProvider::QueueFamilyInfo, GraphicsProvider::QueueFamilyInfo>
FindGraphicsTransferQueues(vk::raii::PhysicalDevice& device)
{
    auto queueFamilies = device.getQueueFamilyProperties();
    for (std::uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        const auto& queue = queueFamilies[i];
        if ((queue.queueFlags & vk::QueueFlagBits::eGraphics) &&
            (queue.queueFlags & (vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eCompute)))
            return std::make_pair(
                std::make_pair(i, std::make_unique<vk::QueueFamilyProperties>(queue)),
                std::make_pair(i, std::make_unique<vk::QueueFamilyProperties>(queue)));
    }

    std::uint32_t graphicsIndex = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t transferIndex = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        const auto& queue = queueFamilies[i];
        if (graphicsIndex == std::numeric_limits<std::uint32_t>::max() &&
            (queue.queueFlags & vk::QueueFlagBits::eGraphics))
            graphicsIndex = i;

        if (transferIndex == std::numeric_limits<std::uint32_t>::max() &&
            (queue.queueFlags & vk::QueueFlagBits::eTransfer))
            transferIndex = i;

        if (graphicsIndex != std::numeric_limits<std::uint32_t>::max() &&
            transferIndex != std::numeric_limits<std::uint32_t>::max())
            break;
    }

    if (graphicsIndex == std::numeric_limits<std::uint32_t>::max() ||
        transferIndex == std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error{"Could not find the Graphics and transfer queues"};

    return std::make_pair(
        std::make_pair(graphicsIndex,
                       std::make_unique<vk::QueueFamilyProperties>(queueFamilies[graphicsIndex])),
        std::make_pair(transferIndex,
                       std::make_unique<vk::QueueFamilyProperties>(queueFamilies[transferIndex])));
}
} // namespace

GraphicsProvider::~GraphicsProvider() {}

GraphicsProvider::GraphicsProvider(const Common::JSONContainer& options)
    : m_context{std::make_shared<vk::raii::Context>()}
{
    const auto appName = options["app_name"]->GetValueOr<Common::JSONContainer::StringType>({});
    const auto engineName =
        options["engine_name"]->GetValueOr<Common::JSONContainer::StringType>({});
    vk::ApplicationInfo appInfo = vk::ApplicationInfo()
                                      .setPApplicationName(appName.c_str())
                                      .setApplicationVersion(1)
                                      .setPEngineName(engineName.c_str())
                                      .setEngineVersion(1)
                                      .setApiVersion(VK_API_VERSION_1_0);

    const auto layers = options["layers"]->GetArrayOfType<Common::JSONContainer::StringType>();
    std::vector<const char*> layersAll(layers.size());
    std::transform(layers.begin(), layers.end(), layersAll.begin(),
                   [](auto& e) { return e.c_str(); });
    const auto extensions =
        options["extensions"]->GetArrayOfType<Common::JSONContainer::StringType>();
    std::vector<const char*> extensionsAll(extensions.size());
    std::transform(extensions.begin(), extensions.end(), extensionsAll.begin(),
                   [](auto& e) { return e.c_str(); });

    vk::InstanceCreateInfo instanceCreateInfo{{},
                                              &appInfo,
                                              static_cast<uint32_t>(layersAll.size()),
                                              layersAll.data(),
                                              static_cast<uint32_t>(extensionsAll.size()),
                                              extensionsAll.data()};

    m_instance = std::make_shared<vk::raii::Instance>(*m_context, instanceCreateInfo);

    vk::raii::PhysicalDevices physicalDevices{*m_instance};
    if (physicalDevices.empty())
        throw std::runtime_error(
            "GraphicsProvider construction failed. Could not resolve physical devices.");

    m_physicalDevice = std::make_shared<vk::raii::PhysicalDevice>(std::move(physicalDevices[0]));

    std::tie(m_graphicsQueueInfo, m_transferQueueInfo) =
        FindGraphicsTransferQueues(*m_physicalDevice);

    float priority = 0.0;
    vk::DeviceQueueCreateInfo queueInfo{};

    queueInfo.setQueueCount(1)
        .setQueueFamilyIndex(m_graphicsQueueInfo.first)
        .setPQueuePriorities(&priority);

    std::array<const char*, 1> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    vk::DeviceCreateInfo deviceCreateInfo{{}, 1, &queueInfo};
    deviceCreateInfo.setQueueCreateInfoCount(1)
        .setPQueueCreateInfos(&queueInfo)
        .setEnabledExtensionCount(1)
        .setPpEnabledExtensionNames(deviceExtensions.data());

    m_device = std::make_shared<vk::raii::Device>(*m_physicalDevice, deviceCreateInfo);

    m_graphicsQueue = std::make_shared<vk::raii::Queue>(*m_device, m_graphicsQueueInfo.first, 0);
    m_transferQueue = std::make_shared<vk::raii::Queue>(*m_device, m_transferQueueInfo.first, 0);
}

const std::shared_ptr<vk::raii::Context>&
GraphicsProvider::GetContext() const
{
    return m_context;
}

const std::shared_ptr<vk::raii::Instance>&
GraphicsProvider::GetInstance() const
{
    return m_instance;
}

const std::shared_ptr<vk::raii::PhysicalDevice>&
GraphicsProvider::GetPhysicalDevice() const
{
    return m_physicalDevice;
}

const std::shared_ptr<vk::raii::Device>&
GraphicsProvider::GetDevice() const
{
    return m_device;
}

const GraphicsProvider::QueueFamilyInfo&
GraphicsProvider::GetGraphicsQueueInfo() const
{
    return m_graphicsQueueInfo;
}

const std::shared_ptr<vk::raii::Queue>&
GraphicsProvider::GetGraphicsQueue() const
{
    return m_graphicsQueue;
}

const GraphicsProvider::QueueFamilyInfo&
GraphicsProvider::GetTransferQueueInfo() const
{
    return m_transferQueueInfo;
}

const std::shared_ptr<vk::raii::Queue>&
GraphicsProvider::GetTransferQueue() const
{
    return m_transferQueue;
}

std::shared_ptr<vk::raii::CommandPool>
GraphicsProvider::MakeCommandPool(CommandPoolType commandPoolType) const
{
    vk::CommandPoolCreateInfo ci{{}, m_graphicsQueueInfo.first};
    switch (commandPoolType)
    {
    case CommandPoolType::Graphics:
        ci.setQueueFamilyIndex(m_graphicsQueueInfo.first);
        break;
    case CommandPoolType::Transfer:
        ci.setQueueFamilyIndex(m_graphicsQueueInfo.first);
        break;
    case CommandPoolType::Present:
        ci.setQueueFamilyIndex(m_transferQueueInfo.first);
        break;
    }
    return std::make_shared<vk::raii::CommandPool>(*m_device, ci);
}
} // namespace VOG::Graphics::Api
