#include "VOG/Engine/Renderer/DrawCheck.hpp"
#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/ShaderDefinition.hpp"
#include <VOG/Engine/Renderer/Renderable.hpp>

#include <utility>

namespace VOG::Engine
{
void
RenderQueue::submit(Mesh                    mesh,
                    Material                material,
                    const Math::Matrix4x4f& world,
                    std::string_view        debugName)
{
    if (!mesh || !material)
    {
        return;
    }

    mCheck->require(*mesh.mData, *material.mData->shader, debugName);
    mDraws.push_back({.mesh = std::move(mesh), .material = std::move(material), .world = world});
}
} // namespace VOG::Engine
