#pragma once

#include <VOG/Engine/Renderer/FrameContext.hpp>
#include <VOG/Engine/Renderer/RenderItem.hpp>
#include <VOG/Engine/Renderer/ResourceContext.hpp>

#include <vector>

namespace VOG::Engine
{
/**
 * Anything that contributes draws to a frame. Both callbacks run on the render thread with no
 * lock held, so they may call Renderer::addRenderable().
 */
class Renderable
{
public:
    virtual ~Renderable() = default;

    /** Called once on the render thread before the first collect(); build GPU resources here. */
    virtual void prepare(const ResourceContext& resourceContext) = 0;

    /**
     * Called every frame; append the draws for this frame to @p renderItems. Every item needs a
     * pipeline, a vertex buffer and a vertex count: a malformed one fails the render thread.
     *
     * It can also run for an iteration that draws nothing (no image was free, or the swapchain
     * was out of date); its items are dropped then. Derive per-frame state from @p frameContext,
     * not from counting calls.
     */
    virtual void collect(const FrameContext&      frameContext,
                         std::vector<RenderItem>& renderItems) = 0;
};
} // namespace VOG::Engine
