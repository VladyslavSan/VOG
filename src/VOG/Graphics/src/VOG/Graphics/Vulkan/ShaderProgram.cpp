#include "VOG/Graphics/Vulkan/ShaderProgram.hpp"

#include <VOG/Common/Assert.hpp>

#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
ShaderProgram::ShaderProgram(ShaderProgram::ShadingStages _stages)
    : stages{std::move(_stages)}
{
    VOG_ASSERT(stages.vertex.shader != nullptr &&
               stages.vertex.shader->stage == Shader::ShadingStage::eVertex);
    VOG_ASSERT(stages.fragment.shader != nullptr &&
               stages.fragment.shader->stage == Shader::ShadingStage::eFragment);

    if (!stages.vertex.shader || !stages.fragment.shader)
    {
        throw std::runtime_error{""};
    }
}
} // namespace VOG::Graphics::Vulkan