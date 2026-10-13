#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <memory>

namespace VOG::Graphics
{
class ShaderProgramCache;
} // namespace VOG::Graphics

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
/**
 * Everything a Renderable needs to build its GPU resources in prepare().
 * Handed out by the Renderer and only valid for the duration of that call.
 */
class ResourceContext
{
public:
    ResourceContext(Graphics::Vulkan::DevicePtr                   device,
                    std::shared_ptr<Graphics::ShaderProgramCache> shaderProgramCache,
                    vk::Format                                    colorAttachmentFormat)
        : mDevice{std::move(device)}
        , mShaderProgramCache{std::move(shaderProgramCache)}
        , mColorAttachmentFormat{colorAttachmentFormat}
    {
    }

    const Graphics::Vulkan::DevicePtr&
    getDevice() const
    {
        return mDevice;
    }

    const std::shared_ptr<Graphics::ShaderProgramCache>&
    getShaderProgramCache() const
    {
        return mShaderProgramCache;
    }

    /** Format of the color target the renderer draws into; pipelines must be built against it. */
    vk::Format
    getColorAttachmentFormat() const
    {
        return mColorAttachmentFormat;
    }

private:
    Graphics::Vulkan::DevicePtr                   mDevice;
    std::shared_ptr<Graphics::ShaderProgramCache> mShaderProgramCache;
    vk::Format                                    mColorAttachmentFormat;
};
} // namespace VOG::Engine
