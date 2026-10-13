#pragma once

#include <VOG/Graphics/Typedefs.hpp>

#include <utility>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

/**
 * Keeps the Device alive for a wrapper around a vk::raii handle. List it before the handle in the
 * base list: bases are destroyed in reverse order, so the handle is destroyed while the device
 * still exists. A member would not do, because members are destroyed before bases.
 */
class DeviceChild
{
public:
    const DevicePtr&
    device() const
    {
        return mDevice;
    }

protected:
    explicit DeviceChild(DevicePtr device)
        : mDevice{std::move(device)}
    {
    }

private:
    DevicePtr mDevice;
};
} // namespace VOG::Graphics::Vulkan
