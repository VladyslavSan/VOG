#pragma once

#include "VulkanFixture.hpp"

namespace VOG::Tests
{
/**
 * VulkanFixture with validation and VK_EXT_headless_surface: tests can create a real Swapchain
 * without a window. Tests that need it skip themselves when headlessSurfaceActive() is false.
 */
class VulkanHeadlessFixture : public VulkanFixture
{
public:
    VulkanHeadlessFixture()
        : VulkanFixture{true, true}
    {
    }

protected:
    static constexpr std::uint8_t kFramesInFlight = 2u;

    /** Parameters of a swapchain on a headless surface. */
    static Graphics::Vulkan::Swapchain::SwapchainParameters
    headlessSwapchainParameters()
    {
        return {.framesInFlight = kFramesInFlight,
                .surface        = {.platform = Common::SurfaceHandle::Platform::eHeadless},
                .fallbackExtent = {.width = 64u, .height = 64u}};
    }
};
} // namespace VOG::Tests
