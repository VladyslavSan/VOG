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

    /**
     * The value the semaphore has now (vkGetSemaphoreCounterValue). Any thread, while another
     * thread submits work that signals the semaphore: the entry point takes no external
     * synchronization of the semaphore. The value is a snapshot, which a pending submit can
     * already have made out of date. It reads the device's counter; the semaphore owns none.
     *
     * @throws vk::SystemError if the device is lost.
     */
    [[nodiscard]] std::uint64_t counterValue() const;
};
} // namespace VOG::Graphics::Vulkan
