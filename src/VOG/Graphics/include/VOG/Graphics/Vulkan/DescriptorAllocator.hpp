#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

class DescriptorAllocator
{
    friend class Device;

public:
    /**
     * Struct that configures construction of the DescriptorAllocator.
     */
    struct ConstructionParameters
    {
        constexpr static std::uint32_t kDefaultNum = 1000u;

        /** Whether to support update after bind. */
        bool updateAfterBind = true;

        /** Number of pools to allocated at the DescriptorAllocator construction. */
        std::uint32_t numPoolsPreallocate = 0u;

        /** Maximum number of descriptor sets that can be allocated from the pool. */
        std::uint32_t numMaxSets = 3u * kDefaultNum;

        /** Number of sampler descriptors to be allocated in the pool. */
        std::uint32_t numSamplers = kDefaultNum;

        /** Number of images descriptors to be allocated in the pool. */
        std::uint32_t numImages = kDefaultNum;

        /** Number of uniform buffer descriptors to be allocated in the pool. */
        std::uint32_t numUniformBuffers = kDefaultNum;
    };

    void reset();

private:
    DescriptorAllocator(DevicePtr device, const ConstructionParameters& params);

protected:
    using Pool = vk::raii::DescriptorPool;

    /** Device handle used to allocate objects. */
    DevicePtr mDevice;

    /** Currently active pool used to allocate objects. */
    Pool mCurrentPool;

    /** Allocated pools that are not used yet since last reset. */
    std::vector<Pool> mPools;

    /** Pools used since last reset. Will be freed on reset() call. */
    std::vector<Pool> mUsedPools;
};
} // namespace VOG::Graphics::Vulkan
