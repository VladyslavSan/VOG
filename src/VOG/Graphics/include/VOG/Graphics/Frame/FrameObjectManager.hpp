#pragma once

#include <VOG/Common/ThreadsafeWrapper.hpp>
#include <VOG/Graphics/Frame/FrameObjects.hpp>
#include <VOG/Graphics/Frame/ThreadObjects.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <vector>

namespace VOG::Graphics
{
VOG_DECLARE_PTR(GraphicsProvider);
}

namespace VOG::Graphics::Frame
{
class FrameObjectManager
{
public:
    FrameObjectManager(const GraphicsProviderPtr& graphicsProvider,
                       std::size_t                frameCount,
                       std::size_t                threadCount);

    const GraphicsProviderPtr& getGraphicsProvider();

    FrameObjects& getFrameObjects(std::size_t frameId);

protected:
    /** Hard reference to graphics provider*/
    const GraphicsProviderPtr mGraphicsProvider;

    /** Per each frame in-flight objects */
    std::vector<FrameObjects> mFrameObjects;
};
} // namespace VOG::Graphics::Frame
