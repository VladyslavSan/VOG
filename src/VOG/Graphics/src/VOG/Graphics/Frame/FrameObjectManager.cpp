#include "VOG/Graphics/Frame/FrameObjectManager.hpp"

#include <VOG/Common/Assert.hpp>

#include <algorithm>

namespace VOG::Graphics::Frame
{
namespace
{
constexpr std::size_t gDefaultPoolSize = 32;
}

FrameObjectManager::FrameObjectManager(const Vulkan::DevicePtr& device, Counts counts)
    : mDevice{device}
    , mRenderFrame{0u}
{
    mFrameObjects.reserve(counts.frames);
    for (std::size_t index = 0u; index < counts.frames; ++index)
    {
        auto& frame =
            mFrameObjects.emplace_back(std::make_shared<FrameObjects>(mDevice, counts.threads));
        frame->mIndex = index;
    }
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
