#include "VOG/Engine/Renderer/Renderer.hpp"

#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/RendererImpl.hpp"
#include "VOG/Engine/Renderer/ShaderDefinition.hpp"

#include <spdlog/spdlog.h>

#include <exception>
#include <iterator>
#include <utility>

namespace VOG::Engine
{
Renderer::Impl::Impl(const Common::SurfaceHandle& surfaceHandle, const Config& config)
    : gpu{surfaceHandle, config}
    , shaders{gpu.device, config.shaderPath}
    , uploads{gpu.device,
              gpu.fencePool,
              gpu.uploadTimeline,
              packets,
              config.backgroundUploadBytesPerFrame}
    , resources{gpu.device, gpu.heap, uploads}
    , loop{gpu, uploads, packets, config.extent}
{
}

void
Renderer::Impl::adoptPendingRenderables()
{
    const std::scoped_lock lock{pendingMutex};
    renderables.insert(renderables.end(),
                       std::make_move_iterator(pending.begin()),
                       std::make_move_iterator(pending.end()));
    pending.clear();
}

FramePacket
Renderer::Impl::makePacket(const FrameContext& frameContext)
{
    // Renderables that onDraw() adds land in pending and join the next frame.
    adoptPendingRenderables();

    // The vector of the draws of an earlier frame, if the render thread gave it back: its
    // capacity is the size of a frame.
    RenderQueue queue{drawCheck, packets.takeRecycled()};
    for (const std::shared_ptr<Renderable>& renderable : renderables)
    {
        renderable->onDraw(frameContext, queue);
    }

    return {.draws = std::move(queue.mDraws), .camera = queue.mCamera, .frame = frameContext};
}

Renderer::Renderer(const Common::SurfaceHandle& surfaceHandle, const Config& config)
    : mImpl{std::make_unique<Impl>(surfaceHandle, config)}
{
}

Renderer::~Renderer()
{
    // Joined here, so that a failure that comes up while the thread ends is not missed.
    mImpl->loop.stop();

    if (mImpl->loop.state() == State::eFailed && !mImpl->loop.failureCollected())
    {
        try
        {
            std::rethrow_exception(mImpl->loop.failure());
        }
        catch (const std::exception& exception)
        {
            spdlog::error("The render thread failed and nobody collected it: {}", exception.what());
        }
        catch (...)
        {
            spdlog::error("The render thread failed with an unknown exception.");
        }
    }
}

void
Renderer::addRenderable(std::shared_ptr<Renderable> renderable)
{
    if (!renderable)
    {
        return;
    }

    const std::scoped_lock lock{mImpl->pendingMutex};
    mImpl->pending.push_back(std::move(renderable));
}

void
Renderer::resize(Extent extent) noexcept
{
    mImpl->loop.resize(extent);
}

Mesh
Renderer::createMesh(const MeshDescription& description)
{
    return mImpl->resources.createMesh(description);
}

Texture
Renderer::createTexture(const TextureDescription& description)
{
    return mImpl->resources.createTexture(description);
}

Shader
Renderer::getShader(std::string_view name) const
{
    return Shader{mImpl->shaders.get(name)};
}

Material
Renderer::createMaterial(MaterialDescription description)
{
    return mImpl->resources.createMaterial(std::move(description));
}

// Public API: an instance question, like the rest of the any-thread members. It reads the handles,
// not the renderer, because the compatibility of a pair does not depend on the renderer.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
std::expected<void, std::string>
Renderer::checkDraw(const Mesh& mesh, const Material& material) const
{
    if (!mesh || !material)
    {
        return {};
    }

    const Detail::MeshData&   meshData   = *mesh.mData;
    const Detail::ShaderInfo& shaderInfo = *material.mData->shader;
    if (const auto missing = Detail::DrawCheck::evaluate(meshData.layout, shaderInfo))
    {
        return std::unexpected{Detail::DrawCheck::describe(meshData, shaderInfo, *missing)};
    }

    return {};
}

// NOLINTEND(readability-convert-member-functions-to-static)

Renderer::UploadStatus
Renderer::uploads() const noexcept
{
    return mImpl->uploads.status();
}

void
Renderer::submitFrame(const FrameContext& frameContext)
{
    // The stop token of the thread wakes a push that waits, if the thread ends before a consumer
    // takes the packet.
    mImpl->packets.push(mImpl->makePacket(frameContext), mImpl->loop.stopToken());
}

void
Renderer::trySubmitFrame(const FrameContext& frameContext)
{
    if (!mImpl->packets.empty())
    {
        return;
    }

    mImpl->packets.tryPush(mImpl->makePacket(frameContext));
}

Renderer::State
Renderer::getState() const noexcept
{
    return mImpl->loop.state();
}

std::exception_ptr
Renderer::failure() noexcept
{
    return mImpl->loop.failure();
}
} // namespace VOG::Engine
