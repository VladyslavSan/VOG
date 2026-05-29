#include "VOG/Graphics/Vulkan/Queue.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>

#include <algorithm>
#include <ranges>

namespace VOG::Graphics::Vulkan
{
namespace
{
template <std::ranges::range R>
auto
toVector(R&& r)
{
    std::vector<std::ranges::range_value_t<R>> v;

    // if we can get a size, reserve that much
    if constexpr (requires { std::ranges::size(r); })
    {
        v.reserve(std::ranges::size(r));
    }

    // push all the elements
    for (auto&& e : r)
    {
        v.push_back(static_cast<decltype(e)&&>(e));
    }

    return v;
}
} // namespace
Queue::Queue(const Device&             _device,
             std::uint32_t             _familyIndex,
             vk::QueueFamilyProperties _familyProperties)
    : vk::raii::Queue{_device, _familyIndex, 0u}
    , device{_device}
    , familyIndex{_familyIndex}
    , familyProperties{_familyProperties}
{
}

std::shared_ptr<FencePool::FenceHandle>
Queue::getFenceHandle(const Device& device)
{
    return device.fencePool->getShared();
}
} // namespace VOG::Graphics::Vulkan
