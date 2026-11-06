#include "VOG/Graphics/Vulkan/GLSLCompiler.hpp"

#include <glslang/Include/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <system_error>
#include <vector>

namespace VOG::Graphics::Vulkan
{
namespace
{

struct GlslangProcess
{
    GlslangProcess() { glslang::InitializeProcess(); }

    ~GlslangProcess() { glslang::FinalizeProcess(); }
};

constexpr TBuiltInResource gDefaultTBuiltInResource = {
    .maxLights                                 = 32,
    .maxClipPlanes                             = 6,
    .maxTextureUnits                           = 32,
    .maxTextureCoords                          = 32,
    .maxVertexAttribs                          = 64,
    .maxVertexUniformComponents                = 4096,
    .maxVaryingFloats                          = 64,
    .maxVertexTextureImageUnits                = 32,
    .maxCombinedTextureImageUnits              = 80,
    .maxTextureImageUnits                      = 32,
    .maxFragmentUniformComponents              = 4096,
    .maxDrawBuffers                            = 32,
    .maxVertexUniformVectors                   = 128,
    .maxVaryingVectors                         = 8,
    .maxFragmentUniformVectors                 = 16,
    .maxVertexOutputVectors                    = 16,
    .maxFragmentInputVectors                   = 15,
    .minProgramTexelOffset                     = -8,
    .maxProgramTexelOffset                     = 7,
    .maxClipDistances                          = 8,
    .maxComputeWorkGroupCountX                 = 65535,
    .maxComputeWorkGroupCountY                 = 65535,
    .maxComputeWorkGroupCountZ                 = 65535,
    .maxComputeWorkGroupSizeX                  = 1024,
    .maxComputeWorkGroupSizeY                  = 1024,
    .maxComputeWorkGroupSizeZ                  = 64,
    .maxComputeUniformComponents               = 1024,
    .maxComputeTextureImageUnits               = 16,
    .maxComputeImageUniforms                   = 8,
    .maxComputeAtomicCounters                  = 8,
    .maxComputeAtomicCounterBuffers            = 1,
    .maxVaryingComponents                      = 60,
    .maxVertexOutputComponents                 = 64,
    .maxGeometryInputComponents                = 64,
    .maxGeometryOutputComponents               = 128,
    .maxFragmentInputComponents                = 128,
    .maxImageUnits                             = 8,
    .maxCombinedImageUnitsAndFragmentOutputs   = 8,
    .maxCombinedShaderOutputResources          = 8,
    .maxImageSamples                           = 0,
    .maxVertexImageUniforms                    = 0,
    .maxTessControlImageUniforms               = 0,
    .maxTessEvaluationImageUniforms            = 0,
    .maxGeometryImageUniforms                  = 0,
    .maxFragmentImageUniforms                  = 8,
    .maxCombinedImageUniforms                  = 8,
    .maxGeometryTextureImageUnits              = 16,
    .maxGeometryOutputVertices                 = 256,
    .maxGeometryTotalOutputComponents          = 1024,
    .maxGeometryUniformComponents              = 1024,
    .maxGeometryVaryingComponents              = 64,
    .maxTessControlInputComponents             = 128,
    .maxTessControlOutputComponents            = 128,
    .maxTessControlTextureImageUnits           = 16,
    .maxTessControlUniformComponents           = 1024,
    .maxTessControlTotalOutputComponents       = 4096,
    .maxTessEvaluationInputComponents          = 128,
    .maxTessEvaluationOutputComponents         = 128,
    .maxTessEvaluationTextureImageUnits        = 16,
    .maxTessEvaluationUniformComponents        = 1024,
    .maxTessPatchComponents                    = 120,
    .maxPatchVertices                          = 32,
    .maxTessGenLevel                           = 64,
    .maxViewports                              = 16,
    .maxVertexAtomicCounters                   = 0,
    .maxTessControlAtomicCounters              = 0,
    .maxTessEvaluationAtomicCounters           = 0,
    .maxGeometryAtomicCounters                 = 0,
    .maxFragmentAtomicCounters                 = 8,
    .maxCombinedAtomicCounters                 = 8,
    .maxAtomicCounterBindings                  = 1,
    .maxVertexAtomicCounterBuffers             = 0,
    .maxTessControlAtomicCounterBuffers        = 0,
    .maxTessEvaluationAtomicCounterBuffers     = 0,
    .maxGeometryAtomicCounterBuffers           = 0,
    .maxFragmentAtomicCounterBuffers           = 1,
    .maxCombinedAtomicCounterBuffers           = 1,
    .maxAtomicCounterBufferSize                = 16384,
    .maxTransformFeedbackBuffers               = 4,
    .maxTransformFeedbackInterleavedComponents = 64,
    .maxCullDistances                          = 8,
    .maxCombinedClipAndCullDistances           = 8,
    .maxSamples                                = 4,
    .maxMeshOutputVerticesNV                   = 256,
    .maxMeshOutputPrimitivesNV                 = 512,
    .maxMeshWorkGroupSizeX_NV                  = 32,
    .maxMeshWorkGroupSizeY_NV                  = 1,
    .maxMeshWorkGroupSizeZ_NV                  = 1,
    .maxTaskWorkGroupSizeX_NV                  = 32,
    .maxTaskWorkGroupSizeY_NV                  = 1,
    .maxTaskWorkGroupSizeZ_NV                  = 1,
    .maxMeshViewCountNV                        = 4,
    .maxMeshOutputVerticesEXT                  = 256,
    .maxMeshOutputPrimitivesEXT                = 256,
    .maxMeshWorkGroupSizeX_EXT                 = 128,
    .maxMeshWorkGroupSizeY_EXT                 = 128,
    .maxMeshWorkGroupSizeZ_EXT                 = 128,
    .maxTaskWorkGroupSizeX_EXT                 = 128,
    .maxTaskWorkGroupSizeY_EXT                 = 128,
    .maxTaskWorkGroupSizeZ_EXT                 = 128,
    .maxMeshViewCountEXT                       = 4,
    .maxDualSourceDrawBuffersEXT               = 1,

    .limits = {
        .nonInductiveForLoops                 = true,
        .whileLoops                           = true,
        .doWhileLoops                         = true,
        .generalUniformIndexing               = true,
        .generalAttributeMatrixVectorIndexing = true,
        .generalVaryingIndexing               = true,
        .generalSamplerIndexing               = true,
        .generalVariableIndexing              = true,
        .generalConstantMatrixVectorIndexing  = true,
    }};

EShLanguage
toEShLanguage(Shader::ShadingStage stage)
{
    switch (stage)
    {
    case Shader::ShadingStage::eVertex:
        return EShLanguage::EShLangVertex;
    case Shader::ShadingStage::eFragment:
        return EShLanguage::EShLangFragment;
    default:
        return EShLangVertex;
    }
}

/**
 * Resolves the name of an include against the include directories, in order. Both kinds of include
 * search the same directories. An include that is not found, is deeper than gMaxIncludeDepth, names
 * a root path, or leaves its directory (a name that starts with ".." once "." and ".." are folded)
 * gets nullptr, which glslang reports as an error that names the header.
 */
class DirectoryIncluder final : public glslang::TShader::Includer
{
public:
    explicit DirectoryIncluder(std::span<const std::filesystem::path> directories)
        : mDirectories{directories}
    {
    }

    IncludeResult*
    includeLocal(const char* headerName,
                 const char* /*includerName*/,
                 size_t inclusionDepth) override
    {
        return resolve(headerName, inclusionDepth);
    }

    IncludeResult*
    includeSystem(const char* headerName,
                  const char* /*includerName*/,
                  size_t inclusionDepth) override
    {
        return resolve(headerName, inclusionDepth);
    }

    /** glslang also calls this with the nullptr of an include that failed. */
    void
    releaseInclude(IncludeResult* result) override
    {
        std::erase_if(mLive,
                      [result](const std::unique_ptr<Included>& included)
                      {
                          return &included->result == result;
                      });
    }

private:
    /** The text of a file and the result that points into it. */
    struct Included
    {
        Included(const std::filesystem::path& path, std::string content)
            : text{std::move(content)}
            , result{path.generic_string(), text.data(), text.size(), nullptr}
        {
        }

        std::string   text;
        IncludeResult result;
    };

    IncludeResult*
    resolve(const char* headerName, size_t inclusionDepth)
    {
        const std::filesystem::path name = std::filesystem::path{headerName}.lexically_normal();
        if (inclusionDepth > gMaxIncludeDepth || name.has_root_path() ||
            (!name.empty() && *name.begin() == ".."))
        {
            return nullptr;
        }

        for (const std::filesystem::path& directory : mDirectories)
        {
            const std::filesystem::path path = directory / name;
            std::error_code             error;
            if (!std::filesystem::is_regular_file(path, error))
            {
                continue;
            }

            std::ifstream file{path, std::ios::binary};
            if (!file)
            {
                continue;
            }
            std::string content{std::istreambuf_iterator<char>{file}, {}};

            return &mLive.emplace_back(std::make_unique<Included>(path, std::move(content)))
                        ->result;
        }

        return nullptr;
    }

    std::span<const std::filesystem::path> mDirectories;
    std::vector<std::unique_ptr<Included>> mLive;
};
} // namespace

std::vector<std::uint32_t>
compileGLSL(Shader::ShadingStage                   stage,
            const std::string&                     glslCode,
            std::span<const std::filesystem::path> includeDirectories)
{
    static GlslangProcess sGlslangProcess;

    const auto shadingStage   = toEShLanguage(stage);
    const auto messagesFilter = EShMessages::EShMsgDefault;

    glslang::TShader shader{shadingStage};
    shader.setDebugInfo(true);

    const char* str    = glslCode.c_str();
    const int   length = static_cast<int>(glslCode.size());
    shader.setStringsWithLengths(&str, &length, 1);
    shader.setEntryPoint("main");
    shader.setSourceEntryPoint("main");

    shader.setEnvInput(glslang::EShSource::EShSourceGlsl,
                       shadingStage,
                       glslang::EShClient::EShClientVulkan,
                       glslang::EShTargetClientVersion::EShTargetVulkan_1_2);

    shader.setEnvClient(glslang::EShClient::EShClientVulkan,
                        glslang::EShTargetClientVersion::EShTargetVulkan_1_2);

    shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv,
                        glslang::EShTargetLanguageVersion::EShTargetSpv_1_5);

    DirectoryIncluder includer{includeDirectories};
    if (!shader.parse(&gDefaultTBuiltInResource,
                      glslang::EShTargetClientVersion::EShTargetOpenGL_450,
                      false,
                      messagesFilter,
                      includer))
    {
        throw Shader::CompilationError{
            std::format("Shader info log:\n{}"
                        "Debug log:\n{}",
                        shader.getInfoLog(),
                        shader.getInfoDebugLog())};
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messagesFilter))
    {
        throw Shader::CompilationError{std::format("Shader info log:\n{}", program.getInfoLog())};
    }

    std::vector<std::uint32_t> spirv{};
    glslang::GlslangToSpv(*program.getIntermediate(shadingStage), spirv);

    return spirv;
}

} // namespace VOG::Graphics::Vulkan
