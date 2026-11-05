#pragma once

#include <VOG/Common/SurfaceHandle.hpp>
#include "VOG/Engine/Renderer/FrameBlocks.hpp"
#include <VOG/Engine/Renderer/Extent.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace VOG::Graphics::Frame
{
class FrameObjectManager;
} // namespace VOG::Graphics::Frame

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(DescriptorHeap);
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(FencePool);
VOG_DECLARE_PTR(Instance);
VOG_DECLARE_PTR(TimelineSemaphore);
class Queue;
class Swapchain;
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
inline vk::Extent2D
toVk(Extent extent)
{
    return vk::Extent2D{.width = extent.width, .height = extent.height};
}

/**
 * The Vulkan objects of one window that the other components share: the instance and the device,
 * the queues, the fence pool, the descriptor heap, the swapchain, the frame slots with the buffer
 * of the Frame block of each, and the timeline semaphore that the copies signal. It makes them in
 * the constructor, in that order, and holds no logic of its own.
 *
 * Who uses what: the swapchain, the frame slots and their Frame blocks belong to the render
 * thread; the device, the heap and the queues are used by any thread that creates a resource (the
 * allocator and the heap lock inside), and by the render thread.
 */
class GpuContext
{
public:
    /**
     * @throws std::runtime_error if the descriptor heap has no room for the Frame block of each
     *         frame slot (Config::resourceSlots).
     */
    GpuContext(const Common::SurfaceHandle& surfaceHandle, const Renderer::Config& config);

    ~GpuContext();

    GpuContext(const GpuContext&)            = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    /** min(framesInFlight, imageCount); logs when it clamps. */
    static std::uint8_t frameSlotCount(std::uint8_t framesInFlight, std::size_t imageCount);

    const std::uint8_t framesInFlight;

    Graphics::Vulkan::InstancePtr                  instance;
    Graphics::Vulkan::DevicePtr                    device;
    std::shared_ptr<const Graphics::Vulkan::Queue> graphicsQueue;
    Graphics::Vulkan::FencePoolPtr                 fencePool;
    Graphics::Vulkan::DescriptorHeapPtr            heap;
    std::unique_ptr<Graphics::Vulkan::Swapchain>   swapchain;
    /** The number of frame slots: framesInFlight, or the number of images of the swapchain if that
     *  is less. */
    const std::uint8_t                                   frameSlots;
    std::unique_ptr<Graphics::Frame::FrameObjectManager> frames;
    FrameBlocks                                          frameBlocks;
    Graphics::Vulkan::TimelineSemaphorePtr               uploadTimeline;
};
} // namespace VOG::Engine
