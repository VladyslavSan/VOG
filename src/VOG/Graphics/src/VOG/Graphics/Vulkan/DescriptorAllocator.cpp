#include "VOG/Graphics/Vulkan/DescriptorAllocator.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>

namespace VOG::Graphics::Vulkan
{
namespace
{
vk::raii::DescriptorPool
makeDescriptorPool(const Device& device, const DescriptorAllocator::ConstructionParameters& params)
{
    std::array sizes = {vk::DescriptorPoolSize{.type            = vk::DescriptorType::eSampler,
                                               .descriptorCount = params.numSamplers},
                        vk::DescriptorPoolSize{.type            = vk::DescriptorType::eSampledImage,
                                               .descriptorCount = params.numImages},
                        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer,
                                               .descriptorCount = params.numUniformBuffers}};
    vk::DescriptorPoolCreateInfo createInfo{.maxSets = params.numMaxSets,
                                            .poolSizeCount =
                                                static_cast<std::uint32_t>(sizes.size()),
                                            .pPoolSizes = sizes.data()};
    if (params.updateAfterBind)
    {
        createInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
    }

    return vk::raii::DescriptorPool{device, createInfo, nullptr};
}
} // namespace

DescriptorAllocator::DescriptorAllocator(const Device&                                      device,
                                         const DescriptorAllocator::ConstructionParameters& params)
    : mDevice{device}
    , mCurrentPool{nullptr}
{
    mPools.reserve(params.numPoolsPreallocate);
    for (std::uint32_t i = 0; i < params.numPoolsPreallocate; ++i)
    {
        mPools.push_back(makeDescriptorPool(mDevice, params));
    }
}

void
DescriptorAllocator::reset()
{
    for (auto curr = mUsedPools.rbegin(); curr != mUsedPools.rend(); ++curr)
    {
        curr->reset();
        mPools.push_back(std::move(*curr));
        mUsedPools.pop_back();
    }
}
} // namespace VOG::Graphics::Vulkan