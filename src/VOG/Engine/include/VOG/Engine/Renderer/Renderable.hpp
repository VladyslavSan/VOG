#pragma once

#include <VOG/Engine/Renderer/CameraView.hpp>
#include <VOG/Engine/Renderer/FrameContext.hpp>
#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Math/Math.hpp>

#include <utility>
#include <vector>

namespace VOG::Engine
{
class Renderer;

/**
 * The draws and the camera of one frame, filled by Renderable::onDraw(). Each draw is one mesh
 * drawn with one material; the camera and the world matrix place it.
 */
class RenderQueue
{
public:
    /**
     * Adds a draw. A draw without a mesh or without a material is removed when the frame is
     * submitted.
     */
    void
    submit(MeshPtr mesh, MaterialPtr material, const Math::Matrix4x4f& world)
    {
        mDraws.push_back(
            {.mesh = std::move(mesh), .material = std::move(material), .world = world});
    }

    /**
     * Sets the view the frame is drawn with. The last call of the frame wins; a frame without a
     * call uses a default CameraView.
     */
    void
    setCamera(const CameraView& camera)
    {
        mCamera = camera;
    }

private:
    friend class Renderer;
    friend struct RenderQueueTestAccess;

    struct Draw
    {
        MeshPtr          mesh;
        MaterialPtr      material;
        Math::Matrix4x4f world{1.0f};
    };

    std::vector<Draw> mDraws;
    CameraView        mCamera;
};

/**
 * Anything that contributes draws to a frame. Renderer::submitFrame() calls onDraw() on the
 * submitting thread, the main thread, with no renderer lock held, so it may call
 * Renderer::addRenderable().
 *
 * It must not call Renderer::submitFrame() or Renderer::trySubmitFrame(): that would change the
 * renderables while they are being iterated. It may run inside an SDL event watch on the main
 * thread (Application does that while the OS resizes the window): it must not wait for a thread
 * that pushes SDL events.
 */
class Renderable
{
public:
    virtual ~Renderable() = default;

    /**
     * Called for every frame that is submitted; append the draws for this frame to @p queue. The
     * frame holds the meshes and materials of the draws until the render thread has recorded it.
     *
     * A submitted frame may be dropped without being drawn (the window is minimized, no image was
     * free, or the swapchain was out of date); its draws are dropped then. Derive per-frame state
     * from @p frameContext, not from counting calls.
     */
    virtual void onDraw(const FrameContext& frameContext, RenderQueue& queue) = 0;
};
} // namespace VOG::Engine
