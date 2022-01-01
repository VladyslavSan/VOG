#include "VOG/Graphics/Frame/FrameObjectManager.hpp"

#include <VOG/Common/Assert.hpp>

#include "VOG/Graphics/GraphicsProvider.hpp"

namespace VOG::Graphics::Frame
{
namespace
{
constexpr std::size_t gDefaultPoolSize = 32;
}

FrameObjectManager::FrameObjectManager(const GraphicsProviderPtr& graphicsProvider,
                                       std::size_t                frameCount,
                                       std::size_t                threadCount)
    : mGraphicsProvider{graphicsProvider}
{
    mFrameObjects.reserve(frameCount);
    for (std::size_t i = 0; i < frameCount; ++i)
    {
        mFrameObjects.emplace_back(*mGraphicsProvider, threadCount);
    }
}

const GraphicsProviderPtr&
FrameObjectManager::getGraphicsProvider()
{
    return mGraphicsProvider;
}

FrameObjects&
FrameObjectManager::getFrameObjects(std::size_t frameId)
{
    VOG_ASSERT_MSG(frameId < mFrameObjects.size(), "Invalid frameId requested.");

    return mFrameObjects[frameId];
}
} // namespace VOG::Graphics::Frame
