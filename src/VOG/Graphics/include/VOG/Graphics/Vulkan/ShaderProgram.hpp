#pragma once

#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

    struct ShadingStagesChecked : ShadingStages
    {
        ShadingStagesChecked(ShadingStages shadingStages);
    };

    /** The push data the stages read, merged over both stages; ranges[i] is named names[i]. */
    struct PushConstants
    {
        struct Range
        {
            std::uint32_t offset;
            std::uint32_t size;
        };

        std::vector<std::string> names;
        std::vector<Range>       ranges;
    };

    /**
     * Links vertex and fragment shaders into a shader program. The push data ranges are derived
     * through shader reflection.
     *
     * @throws std::runtime_error if a stage is missing or the stages disagree on push data.
     */
    static std::unique_ptr<ShaderProgram> create(ShadingStages stages);

private:
    explicit ShaderProgram(ShadingStagesChecked stages);

public:
    /** The shaders in the stages keep the device alive; the program owns no Vulkan object. */
    const ShadingStages stages;
    const PushConstants pushConstants;
};
} // namespace VOG::Graphics::Vulkan
