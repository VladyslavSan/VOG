#pragma once

#include <VOG/Graphics/Vulkan/Shader.hpp>

namespace VOG::Graphics::Vulkan
{
class ShaderProgram
{
public:
    struct ShadingStageInfo
    {
        ShaderPtr   shader     = nullptr;
        const char* entryPoint = "main";
    };

    struct ShadingStages
    {
        ShadingStageInfo vertex;
        ShadingStageInfo fragment;
    };

    ShaderProgram(ShadingStages stages);

public:
    const ShadingStages stages;
};
} // namespace VOG::Graphics::Vulkan
