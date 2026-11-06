#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

/** Receives a diagnostic message of Vulkan. See Instance::InstanceParameters::messageSink. */
using MessageSink = std::function<void(std::string_view)>;

namespace Detail
{
/** The sink an Instance's messengers call. A base of Instance, see the order there. */
struct MessageSinkHolder
{
    MessageSink callback;
};
} // namespace Detail

class Instance
    // Must stay the first base: it is destroyed last. The messenger that vkCreateInstance took from
    // pNext also reports during vkDestroyInstance, which runs in the destructor of the
    // vk::raii::Instance base, after every member of Instance is gone. A member would dangle.
    : private Detail::MessageSinkHolder
    , private vk::raii::Context
    , private vk::raii::Instance
    , public std::enable_shared_from_this<Instance>
{
public:
    struct InstanceParameters
    {
        std::string              appName;
        std::string              engineName;
        std::vector<std::string> layers;
        std::vector<std::string> extensions;

        /**
         * Optional. Called with every message of ERROR severity, of the types general,
         * validation and performance (validation layer, loader, driver), from vkCreateInstance
         * until the Instance is destroyed. When set, VK_EXT_debug_utils is enabled; the Instance
         * throws std::runtime_error if the instance does not offer it.
         *
         * Any thread that makes a Vulkan call may call it, and calls may overlap, so it must be
         * thread-safe. It must not call Vulkan or throw (an exception is logged and dropped), and
         * the view is valid only during the call. The Instance keeps a copy until it is destroyed,
         * which is after the last object made from its devices is: whatever the sink captures must
         * live that long.
         */
        MessageSink messageSink;
    };

private:
    Instance(const InstanceParameters& parameters);

public:
    Instance(const Instance&) = delete;
    Instance(Instance&&)      = delete;

    /**
     * @throws std::runtime_error if @p parameters has a messageSink and the instance does not offer
     *         VK_EXT_debug_utils.
     */
    [[nodiscard]] static std::shared_ptr<Instance>
    create(const InstanceParameters& parameters)
    {
        return std::shared_ptr<Instance>(new Instance{parameters});
    }

    using vk::raii::Instance::getDispatcher;
    using vk::raii::Instance::operator*;

    /** For the raii objects that take an instance; the base itself stays private. */
    const vk::raii::Instance& raii() const noexcept;

    [[nodiscard]] DevicePtr makeDevice();

private:
    /** Only with a message sink: reports from the end of vkCreateInstance on. */
    vk::raii::DebugUtilsMessengerEXT mMessenger;
};
} // namespace VOG::Graphics::Vulkan