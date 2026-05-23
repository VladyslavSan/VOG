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

    FrameObjects& getCurrentFrame();

    FrameObjects& acquireNextFrame();

private:
    std::size_t getCurrentFrameIndex() const;

protected:
    /** Hard reference to graphics provider*/
    Vulkan::DevicePtr mDevice;

    /** Frame counter */
    std::size_t mRenderFrame;

    /** Per each frame in-flight objects */
    std::vector<FrameObjects> mFrameObjects;
};
} // namespace VOG::Graphics::Frame
