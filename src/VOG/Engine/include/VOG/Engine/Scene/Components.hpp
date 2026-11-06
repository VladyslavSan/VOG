#pragma once

#include <VOG/Engine/Renderer/CameraView.hpp>
#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Math/Math.hpp>
#include <VOG/Math/Projections.hpp>

#include <string>

namespace VOG::Engine
{
/** Where an object is in the world. Every GameObject has one. */
struct Transform
{
    Math::Vector3f position{0.0f};
    /** Must be a unit quaternion: the camera and the matrix read it as is. */
    Math::Quaternionf rotation = Math::identityQuaternion();
    Math::Vector3f    scale{1.0f};

    /** Local to world: translate * rotate * scale. */
    Math::Matrix4x4f
    matrix() const
    {
        return Math::trs(position, rotation, scale);
    }
};

/** What an object is called, for `Scene::find` and for the error that names a draw. Every
 *  GameObject has one, like its Transform; it may be empty. */
struct Name
{
    std::string value;
};

/** Draws a mesh with a material at the Transform of its object. An empty mesh or material draws
 *  nothing. */
struct MeshRenderer
{
    Mesh     mesh;
    Material material;
};

/**
 * Looks along the +Z axis of its object's Transform with +Y up (see CameraView). A Scene draws with
 * at most one Camera: GameObject::addComponent<Camera>() throws while another exists. A camera of a
 * destroyed object exists until the next Scene::update() ends, so to switch cameras within one
 * frame, call removeComponent<Camera>() on the old one first. Without a Camera the Scene draws with
 * a default CameraView. The default projection shows 10 world units from the bottom of the window
 * to the top (Orthographic::height): a game that thinks in pixels sets the height of its window.
 */
struct Camera
{
    Projection     projection = Orthographic{};
    Math::Vector4f clearColor{0.0f, 0.0f, 0.0f, 1.0f};
};
} // namespace VOG::Engine
