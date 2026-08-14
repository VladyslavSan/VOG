#pragma once

#include <VOG/Engine/Renderer/Renderable.hpp>

#include <cstdint>
#include <vector>

/**
 * Two colored quads orbiting the screen center — the demo that used to be hardcoded
 * inside Renderer::render().
 */
class SpinningQuadsRenderable final : public VOG::Engine::Renderable
{
public:
    void prepare(VOG::Engine::ResourceContext& resourceContext) override;

    void collect(const VOG::Engine::FrameContext&      frameContext,
                 std::vector<VOG::Engine::RenderItem>& renderItems) override;

private:
    VOG::Graphics::Vulkan::GraphicsPipelinePtr mPipeline;
    VOG::Graphics::Vulkan::BufferPtr           mVertexBuffer;
    std::uint32_t                              mVertexCount = 0u;
};
