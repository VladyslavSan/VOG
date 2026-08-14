#pragma once

#include <VOG/Engine/Renderer/FrameContext.hpp>
#include <VOG/Engine/Renderer/RenderItem.hpp>
#include <VOG/Engine/Renderer/ResourceContext.hpp>

#include <vector>

namespace VOG::Engine
{
/** Anything that contributes draws to a frame. */
class Renderable
{
public:
    virtual ~Renderable() = default;

    /** Called once on the render thread before the first collect(); build GPU resources here. */
    virtual void prepare(ResourceContext& resourceContext) = 0;

    /** Called every frame; append the draws for this frame to @p renderItems. */
    virtual void collect(const FrameContext&      frameContext,
                         std::vector<RenderItem>& renderItems) = 0;
};
} // namespace VOG::Engine
