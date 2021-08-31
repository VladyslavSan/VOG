#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

namespace VOG::Graphics
{
class GraphicsProvider;
}

namespace VOG::Graphics::Vulkan
{
class Device : public vk::raii::Device
{
public:
    Device(const GraphicsProvider& graphicsProvider, vk::raii::Device device)
        : mGraphicsProvider{graphicsProvider}
        , vk::raii::Device{std::move(device)}
    {
    }

    const GraphicsProvider&
    getGraphicsProvider() const
    {
        return mGraphicsProvider;
    }

protected:
    const GraphicsProvider& mGraphicsProvider;
};
} // namespace VOG::Graphics::Vulkan
