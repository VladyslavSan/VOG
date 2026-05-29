#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <memory>
#include <string>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

class Instance
    : private vk::raii::Context
    , public vk::raii::Instance
    , public std::enable_shared_from_this<Instance>
{
public:
    struct InstanceParameters
    {
        std::string              appName;
        std::string              engineName;
        std::vector<std::string> layers;
        std::vector<std::string> extensions;
    };

private:
    Instance(const InstanceParameters& parameters);

public:
    static std::shared_ptr<Instance>
    create(const InstanceParameters& parameters)
    {
        return std::shared_ptr<Instance>(new Instance{parameters});
    }

    using vk::raii::Instance::getDispatcher;
    using vk::raii::Instance::operator*;

    DevicePtr makeDevice();
};
} // namespace VOG::Graphics::Vulkan