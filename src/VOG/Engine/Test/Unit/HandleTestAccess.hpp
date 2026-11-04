#pragma once

#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"

#include <memory>
#include <utility>

namespace VOG::Engine
{
/**
 * Test-only window into the handles, befriended by Mesh, Material and Texture. Defined here once:
 * every test links into one executable.
 */
struct HandleTestAccess
{
    /** A mesh without a Renderer. */
    static Mesh
    makeMesh()
    {
        return Mesh{std::make_shared<const Detail::MeshData>()};
    }

    /** A material without a Renderer. */
    static Material
    makeMaterial()
    {
        return Material{std::make_shared<const Detail::MaterialData>()};
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

    static std::weak_ptr<const Detail::MeshData>
    weak(const Mesh& mesh)
    {
        return mesh.mData;
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
