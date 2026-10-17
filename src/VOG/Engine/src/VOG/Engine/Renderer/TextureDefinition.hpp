#pragma once

#include <VOG/Engine/Renderer/Texture.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Texture);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
class Texture
{
public:
    /** Its descriptor sits in the renderer's heap; a draw pushes its heap index. */
    Graphics::Vulkan::TexturePtr texture;
};

constexpr vk::Format
toVk(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::eRGBA8Srgb:
        return vk::Format::eR8G8B8A8Srgb;
    case TextureFormat::eRGBA8Unorm:
        return vk::Format::eR8G8B8A8Unorm;
    }

    return vk::Format::eUndefined;
}
} // namespace VOG::Engine
