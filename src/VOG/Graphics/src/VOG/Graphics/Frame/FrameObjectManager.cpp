#include "VOG/Graphics/Frame/FrameObjectManager.hpp"

#include <VOG/Common/Assert.hpp>

#include <algorithm>

namespace VOG::Graphics::Frame
{
namespace
{
constexpr std::size_t gDefaultPoolSize = 32;
}

FrameObjectManager::FrameObjectManager(const Vulkan::DevicePtr& device,
                                       std::size_t              frameCount,
                                       std::size_t              threadCount)
    : mDevice{device}
    , mRenderFrame{0u}
{
    mFrameObjects.reserve(frameCount);
    std::generate_n(std::back_inserter(mFrameObjects),
                    frameCount,
                    [this, threadCount]()
                    {
                        return std::make_shared<FrameObjects>(mDevice, threadCount);
                    });
}

FrameObjectsPtr
FrameObjectManager::prepareNextFrame()
{
    const FrameObjectsPtr& frame = mFrameObjects[(mRenderFrame + 1u) % mFrameObjects.size()];
    frame->onFrameStart();

    return frame;
}

void
FrameObjectManager::advance()
{
    ++mRenderFrame;
}
} // namespace VOG::Graphics::Frame
