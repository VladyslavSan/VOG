#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <cstdint>
#include <optional>

// The barriers that follow an upload copy, as plain values. CommandBufferRecorder records them:
// release after the copy, in the command buffer of the queue that copied, and acquire in the
// command buffer of the queue that uses the resource. They are public functions so that tests can
// check the values of the cross-family case on a driver that has one queue family.
//
// With equal families there is no ownership transfer: one barrier after the copy is all there is,
// and acquire is empty. With two families, release and acquire are the two halves of a queue family
// ownership transfer: they name the same families and the same layouts, and the release names no
// destination stage, because a transfer-only queue does not support the stages of the user. The
// submit that carries the acquire must wait for the submit that carries the release, at the
// all-commands stage.
namespace VOG::Graphics::Vulkan
{
/**
 * Buffer after a copy into it, read as vertices and indices. Both are empty for equal families:
 * the semaphore that orders the two submits is the whole dependency.
 */
struct BufferUploadBarriers
{
    std::optional<vk::BufferMemoryBarrier2> release;
    std::optional<vk::BufferMemoryBarrier2> acquire;
};

/**
 * Image after a copy into it, from transfer-dst to shader-read-only. For equal families the
 * release is the only barrier, and it names the shader stages itself.
 */
struct TextureUploadBarriers
{
    vk::ImageMemoryBarrier2                release;
    std::optional<vk::ImageMemoryBarrier2> acquire;
};

BufferUploadBarriers
bufferUploadBarriers(vk::Buffer buffer, std::uint32_t uploadFamily, std::uint32_t usageFamily);

TextureUploadBarriers
textureUploadBarriers(vk::Image image, std::uint32_t uploadFamily, std::uint32_t usageFamily);
} // namespace VOG::Graphics::Vulkan
