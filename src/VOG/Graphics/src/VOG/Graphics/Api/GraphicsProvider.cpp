#include "VOG/Graphics/Api/GraphicsProvider.hpp"

#include <VOG/Common/JSONContainer.hpp>

#include <stdexcept>

namespace VOG::Graphics::Api
{
namespace
{
vk::raii::Instance
MakeInstance(vk::raii::Context& context, const Common::JSONContainer& options)
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

    return {context, instanceCreateInfo};
}

GraphicsProvider::QueueInfos
FindGraphicsTransferQueues(vk::raii::PhysicalDevice& device)
{
    auto queueFamilies = device.getQueueFamilyProperties();
    for (std::uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        const auto& queue = queueFamilies[i];
        if ((queue.queueFlags & vk::QueueFlagBits::eGraphics) &&
            (queue.queueFlags & (vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eCompute)))
            return {.graphics = {i, queue}, .transfer = {i, queue}};
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

    return {.graphics = {graphicsIndex, queueFamilies[graphicsIndex]},
            .transfer = {transferIndex, queueFamilies[transferIndex]}};
}

vk::raii::Device
MakeDevice(vk::raii::PhysicalDevice& physicalDevice, const GraphicsProvider::QueueInfos& infos)
{
    float priority = 0.0;
    vk::DeviceQueueCreateInfo queueInfo{};

    queueInfo.setQueueCount(1)
        .setQueueFamilyIndex(infos.graphics.familyIndex)
        .setPQueuePriorities(&priority);

    std::array<const char*, 1> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    vk::DeviceCreateInfo deviceCreateInfo{{}, 1, &queueInfo};
    deviceCreateInfo.setQueueCreateInfoCount(1)
        .setPQueueCreateInfos(&queueInfo)
        .setEnabledExtensionCount(1)
        .setPpEnabledExtensionNames(deviceExtensions.data());

    return {physicalDevice, deviceCreateInfo};
}
} // namespace

GraphicsProvider::~GraphicsProvider() {}

GraphicsProvider::GraphicsProvider(const Common::JSONContainer& options)
    : mContext{}
    , mInstance{MakeInstance(mContext, options)}
    , mPhysicalDevice{std::move(vk::raii::PhysicalDevices{mInstance}.at(0))}
    , mQueueInfos{FindGraphicsTransferQueues(mPhysicalDevice)}
    , mDevice{MakeDevice(mPhysicalDevice, mQueueInfos)}
    , mGraphicsQueue{mDevice, mQueueInfos.graphics.familyIndex, 0}
    , mTransferQueue{mDevice, mQueueInfos.transfer.familyIndex, 0}
{
}

std::shared_ptr<vk::raii::CommandPool>
GraphicsProvider::MakeCommandPool(CommandPoolType commandPoolType) const
{
    vk::CommandPoolCreateInfo ci{{}, mQueueInfos.graphics.familyIndex};
    switch (commandPoolType)
    {
    case CommandPoolType::Graphics:
        ci.setQueueFamilyIndex(mQueueInfos.graphics.familyIndex);
        break;
    case CommandPoolType::Transfer:
        ci.setQueueFamilyIndex(mQueueInfos.transfer.familyIndex);
        break;
    }
    return std::make_shared<vk::raii::CommandPool>(mDevice, ci);
}
} // namespace VOG::Graphics::Api
