#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <gtest/gtest.h>

#include "TestShaderCompiler.hpp"

namespace VOG::Tests
{
class VulkanFixture : public testing::Test
{
public:
    VulkanFixture()
        : VulkanInstance{Graphics::Vulkan::Instance::create({
              .appName    = "VOG Test",
              .engineName = "Test",
              .layers     = {},
              .extensions = {},
          })}
        , VulkanDevice{VulkanInstance->makeDevice()}
    {
    }

    Graphics::Vulkan::ShaderPtr
    createShader(Graphics::Vulkan::Shader::ShadingStage stage, const std::string& glslCode)
    {
        return VulkanDevice->createShader(stage, compileGLSL(stage, glslCode));
    }

protected:
    Graphics::Vulkan::InstancePtr VulkanInstance;
    Graphics::Vulkan::DevicePtr   VulkanDevice;
};
} // namespace VOG::Tests