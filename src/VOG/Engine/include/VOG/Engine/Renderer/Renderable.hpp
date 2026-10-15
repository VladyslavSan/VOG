#pragma once

#include <VOG/Engine/Renderer/FrameContext.hpp>
#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Math/Math.hpp>

#include <vector>

namespace VOG::Engine
{
/** One mesh drawn with one material; the camera and the world matrix place it. */
struct Draw
{
    MeshPtr          mesh;
    MaterialPtr      material;
    Math::Matrix4x4f world{1.0f};
};

using DrawList = std::vector<Draw>;

/**
 * Anything that contributes draws to a frame. collect() runs on the render thread with no lock
 * held, so it may call Renderer::addRenderable().
 */
class Renderable
{
public:
    virtual ~Renderable() = default;

    /**
     * Called every frame; append the draws for this frame to @p draws. A draw without a mesh or a
     * material fails the render thread. The renderer keeps the draws, and so the meshes and
     * materials they hold, until the next collect().
     *
     * It can also run for an iteration that draws nothing (no image was free, or the swapchain
     * was out of date); its draws are dropped then. Derive per-frame state from @p frameContext,
     * not from counting calls.
     */
    virtual void collect(const FrameContext& frameContext, DrawList& draws) = 0;
};
} // namespace VOG::Engine
