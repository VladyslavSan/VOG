#pragma once

#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Math/Math.hpp>

#include <glm/ext/matrix_transform.hpp>

namespace VOG::Engine
{
/** Where an object is in the world. Every GameObject has one. */
struct Transform
{
    Math::Vector3f position{0.0f};
    /** Must be a unit quaternion: the camera and the matrix read it as is. */
    Math::Quaternionf rotation = glm::identity<Math::Quaternionf>();
    Math::Vector3f    scale{1.0f};

    /** Local to world: translate * rotate * scale. */
    Math::Matrix4x4f
    matrix() const
    {
        return glm::translate(Math::Matrix4x4f{1.0f}, position) * glm::mat4_cast(rotation) *
               glm::scale(Math::Matrix4x4f{1.0f}, scale);
    }
};

/** Draws a mesh with a material at the Transform of its object. A null mesh or material draws
 *  nothing. */
struct MeshRenderer
{
    MeshPtr     mesh;
    MaterialPtr material;
};

/**
 * Looks along the +Z axis of its object's Transform with +Y up, orthographic, one world unit per
 * pixel (see CameraView). A Scene draws with at most one Camera: GameObject::addComponent<Camera>()
 * throws while another exists. A camera of a destroyed object exists until the next
 * Scene::update() ends, so to switch cameras within one frame, call removeComponent<Camera>() on
 * the old one first. Without a Camera the Scene draws with a default CameraView.
 */
struct Camera
{
    float nearPlane = 1.0f;
    float farPlane  = 200.0f; // NOLINT(readability-magic-numbers)
};
} // namespace VOG::Engine
