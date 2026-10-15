#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/DeviceChild.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Semaphore);

/**
 * Binary semaphore. It exists only as shared_ptr, so that Queue::submit can hand it to the command
 * buffers it submits: they retain it until their fence signals, the same way recording retains
 * resources.
 */
class Semaphore
    : public DeviceChild
    , private vk::raii::Semaphore
{
    friend class Device;

    explicit Semaphore(DevicePtr device);

public:
    using vk::raii::Semaphore::operator*;

    Semaphore(const Semaphore&) = delete;
    Semaphore(Semaphore&&)      = delete;
};
} // namespace VOG::Graphics::Vulkan
