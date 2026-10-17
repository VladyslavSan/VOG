#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/DeviceChild.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(TimelineSemaphore);

/**
 * Timeline semaphore. It exists only as shared_ptr, so that Queue::submit can hand it to the
 * command buffers it submits: they retain it until their fence signals, like a binary Semaphore.
 *
 * The semaphore holds no counter. Whoever chains work on it owns the value, because the value
 * that a submit waits or signals has to be committed in step with the submit.
 */
class TimelineSemaphore
    : public DeviceChild
    , private vk::raii::Semaphore
{
    friend class Device;

    explicit TimelineSemaphore(DevicePtr device);

public:
    using vk::raii::Semaphore::operator*;

    TimelineSemaphore(const TimelineSemaphore&) = delete;
    TimelineSemaphore(TimelineSemaphore&&)      = delete;

    [[nodiscard("Wait result can end with success, fail and timeout.")]]
    vk::Result waitOnCPU(std::uint64_t value, std::uint64_t timeout) const;
};
} // namespace VOG::Graphics::Vulkan
