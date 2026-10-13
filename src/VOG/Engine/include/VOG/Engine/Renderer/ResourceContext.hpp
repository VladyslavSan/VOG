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
 * Everything a Renderable needs to build its GPU resources in prepare(). It holds shared
 * references, so a Renderable may keep the device or the shader cache past the call.
 */
struct ResourceContext
{
    Graphics::Vulkan::DevicePtr                   device;
    std::shared_ptr<Graphics::ShaderProgramCache> shaderProgramCache;

    /** Format of the color target the renderer draws into; pipelines must be built against it. */
    vk::Format colorAttachmentFormat = vk::Format::eUndefined;
};
} // namespace VOG::Engine
