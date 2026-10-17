#pragma once

#include <VOG/Common/ThreadsafeWrapper.hpp>
#include <VOG/Graphics/Frame/FrameObjects.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
}

namespace VOG::Graphics::Frame
{
class FrameObjectManager
{
public:
    FrameObjectManager(const Vulkan::DevicePtr& device,
                       std::size_t              frameCount,
                       std::size_t              threadCount);

    /**
     * The slot the next frame renders into, ready to record: its pools are reset and its previous
     * presentation is dropped. Both wait for fences (submit and present) without a timeout, so the
     * GPU and the presentation engine must make progress. This is what makes the slot's
     * semaphores safe to signal again, so call it before the swapchain acquire.
     *
     * Does not advance: a second call before advance() returns the same slot and changes nothing.
     * A frame that is dropped (skipped or out-of-date acquire) therefore does not use up a slot.
     */
    FrameObjectsPtr prepareNextFrame();

    /** Makes the slot prepareNextFrame() returned the current one. Call it once per frame. */
    void advance();

protected:
    /** Hard reference to graphics provider*/
    Vulkan::DevicePtr mDevice;

    /** Frame counter */
    std::size_t mRenderFrame;

    /** Per each frame in-flight objects */
    std::vector<std::shared_ptr<FrameObjects>> mFrameObjects;
};
} // namespace VOG::Graphics::Frame
