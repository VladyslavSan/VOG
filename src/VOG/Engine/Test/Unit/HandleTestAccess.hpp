#pragma once

#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/ShaderDefinition.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace VOG::Engine
{
/**
 * Test-only window into the handles, befriended by Mesh, Material and Texture. Defined here once:
 * every test links into one executable.
 */
struct HandleTestAccess
{
    /** A mesh without a Renderer, with the layout @p layout, which has the id @p layoutId. */
    static Mesh
    makeMesh(const VertexLayout& layout = {}, std::uint32_t layoutId = 1u)
    {
        return Mesh{std::make_shared<const Detail::MeshData>(
            Detail::MeshData{.name = "mesh", .layout = layout, .layoutId = layoutId})};
    }

    /** A material without a Renderer, of a shader that reads @p inputs and has the id
     *  @p programId. */
    static Material
    makeMaterial(std::vector<Detail::VertexInput> inputs = {}, std::uint32_t programId = 1u)
    {
        auto shader       = std::make_shared<Detail::ShaderInfo>();
        shader->name      = "shader";
        shader->programId = programId;
        shader->inputs    = std::move(inputs);

        return Material{std::make_shared<const Detail::MaterialData>(
            Detail::MaterialData{.shader = std::move(shader)})};
    }

    /** The handle must not be empty. */
    static const Detail::MeshData&
    data(const Mesh& mesh)
    {
        return *mesh.mData;
    }

    static const Detail::MaterialData&
    data(const Material& material)
    {
        return *material.mData;
    }

    static const Detail::TextureData&
    data(const Texture& texture)
    {
        return *texture.mData;
    }

    static const Detail::ShaderInfo&
    data(const Shader& shader)
    {
        return *shader.mData;
    }

    static std::weak_ptr<const Detail::MeshData>
    weak(const Mesh& mesh)
    {
        return mesh.mData;
    }

    static std::weak_ptr<const Detail::MaterialData>
    weak(const Material& material)
    {
        return material.mData;
    }

    static std::weak_ptr<const Detail::TextureData>
    weak(const Texture& texture)
    {
        return texture.mData;
    }

    /** How many handles share the mesh. */
    static long
    useCount(const Mesh& mesh)
    {
        return mesh.mData.use_count();
    }
};
} // namespace VOG::Engine
