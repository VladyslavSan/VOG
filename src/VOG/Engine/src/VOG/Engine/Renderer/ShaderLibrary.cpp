#include "VOG/Engine/Renderer/ShaderLibrary.hpp"

#include "VOG/Engine/Renderer/DrawPushData.hpp"
#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/ShaderDefinition.hpp"
#include <VOG/Graphics/Vulkan/BlockLayout.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/GLSLCompiler.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <utility>

namespace VOG::Engine
{
namespace
{
using Detail::MaterialMember;
using Detail::ParameterType;
using Graphics::Vulkan::BlockLayout;
using Graphics::Vulkan::Shader;
using Graphics::Vulkan::ShaderProgram;
using Member    = Shader::Reflection::BlockMember;
using HeapBlock = Shader::Reflection::HeapBlock;
using BaseType  = Shader::Reflection::BaseType;

constexpr std::string_view gVertexSuffix     = ".vert.glsl";
constexpr std::string_view gFragmentSuffix   = ".frag.glsl";
constexpr std::string_view gIncludeDirectory = "include";

constexpr std::string_view gFrameBlock    = "Frame";
constexpr std::string_view gMaterialBlock = "Material";
constexpr std::string_view gTextureRef    = "TextureRef";

/** The members of DrawPushData as VOG/Draw.glsl names them. */
const BlockLayout&
drawPushLayout()
{
    static const BlockLayout kLayout =
        BlockLayout::of<DrawPushData>({{"model", &DrawPushData::model},
                                       {"material", &DrawPushData::material},
                                       {"frame", &DrawPushData::frame}});

    return kLayout;
}

const BlockLayout&
frameLayout()
{
    static const BlockLayout kLayout =
        BlockLayout::of<FrameBlock>({{"view", &FrameBlock::view},
                                     {"projection", &FrameBlock::projection},
                                     {"viewProjection", &FrameBlock::viewProjection},
                                     {"cameraPosition", &FrameBlock::cameraPosition},
                                     {"viewport", &FrameBlock::viewport},
                                     {"timeSeconds", &FrameBlock::timeSeconds},
                                     {"deltaSeconds", &FrameBlock::deltaSeconds}});

    return kLayout;
}

std::string
readFile(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    if (!file)
    {
        throw std::runtime_error{std::format("Cannot read the shader file {}.", path.string())};
    }

    return {std::istreambuf_iterator<char>{file}, {}};
}

/** The name of a shader file of the directory, without the suffix, or empty. */
std::string
shaderNameOf(const std::filesystem::path& file, std::string_view suffix)
{
    const std::string name = file.filename().string();

    return name.ends_with(suffix) ? name.substr(0u, name.size() - suffix.size()) : std::string{};
}

/** A member of the Material block with the type of its parameter, or the error that says why not.
 */
MaterialMember
materialMemberOf(std::string_view shader, const Member& member)
{
    const auto block = [shader](const std::string& what)
    {
        return std::runtime_error{
            std::format("The Material block of shader \"{}\" has the member {}", shader, what)};
    };

    if (member.arraySize != 0u || member.arrayStride != 0u)
    {
        throw block(std::format(
            "\"{}\", which is an array; a material parameter cannot be an array.", member.name));
    }
    if (member.rowMajor)
    {
        throw block(
            std::format("\"{}\", a row_major matrix; declare material matrices "
                        "column-major (the default).",
                        member.name));
    }

    if (!member.structName.empty())
    {
        if (member.structName != gTextureRef)
        {
            throw block(
                std::format("\"{}\" of type struct {}; the only struct a Material block "
                            "takes is TextureRef.",
                            member.name,
                            member.structName));
        }

        const auto isUInt = [](const Member& part, std::string_view name, std::uint32_t offset)
        {
            return part.name == name && part.offset == offset && part.arraySize == 0u &&
                   part.structName.empty() &&
                   part.type ==
                       BaseType{.type = BaseType::Type::eUInt, .components = 1u, .columns = 1u};
        };
        if (member.members.size() != 2u || !isUInt(member.members[0], "index", 0u) ||
            !isUInt(member.members[1], "samplerIndex", sizeof(std::uint32_t)))
        {
            throw std::runtime_error{std::format(
                "The struct TextureRef of shader \"{}\" is not {{ uint index; uint samplerIndex; "
                "}}.",
                shader)};
        }

        return {.name         = member.name,
                .type         = ParameterType::eTexture,
                .offset       = member.offset,
                .matrixStride = 0u};
    }

    using Kind          = BaseType::Type;
    const BaseType type = member.type;
    const auto     pick = [&](ParameterType first)
    {
        // The types of one kind are consecutive: the scalar, then two, three and four components.
        return static_cast<ParameterType>(static_cast<std::uint8_t>(first) + type.components - 1u);
    };

    if (type.columns == 1u && type.components >= 1u && type.components <= 4u)
    {
        if (type.type == Kind::eFloat)
        {
            return {.name         = member.name,
                    .type         = pick(ParameterType::eFloat),
                    .offset       = member.offset,
                    .matrixStride = 0u};
        }
        if (type.type == Kind::eInt)
        {
            return {.name         = member.name,
                    .type         = pick(ParameterType::eInt),
                    .offset       = member.offset,
                    .matrixStride = 0u};
        }
        if (type.type == Kind::eUInt)
        {
            return {.name         = member.name,
                    .type         = pick(ParameterType::eUInt),
                    .offset       = member.offset,
                    .matrixStride = 0u};
        }
    }
    if (type.type == Kind::eFloat && type.components == type.columns &&
        (type.columns == 3u || type.columns == 4u))
    {
        return {.name   = member.name,
                .type   = type.columns == 3u ? ParameterType::eFloat3x3 : ParameterType::eFloat4x4,
                .offset = member.offset,
                .matrixStride = member.matrixStride};
    }

    throw block(
        std::format("\"{}\" of type {}, which a material cannot set; the types are float, "
                    "vec2-4, int, ivec2-4, uint, uvec2-4, mat3, mat4 and TextureRef.",
                    member.name,
                    Shader::describe(type)));
}

/** Checks the heap blocks of @p program, and fills @p info with the Material block. */
void
checkHeapBlocks(const ShaderProgram& program, Detail::ShaderInfo& info)
{
    for (const HeapBlock& block : program.heapBlocks)
    {
        if (block.name == gFrameBlock)
        {
            if (block.storage != Shader::Reflection::HeapBlockStorage::eUniform)
            {
                throw std::runtime_error{std::format(
                    "The Frame block of shader \"{}\" is a storage (buffer) block; the renderer "
                    "backs it with a uniform buffer, so declare it uniform.",
                    info.name)};
            }
            try
            {
                program.requireHeapBlock(block.name, frameLayout());
            }
            catch (const std::invalid_argument&)
            {
                std::throw_with_nested(std::runtime_error{std::format(
                    "The Frame block of shader \"{}\" is not the block that the renderer writes "
                    "(FrameBlock).",
                    info.name)});
            }
        }
        else if (block.name == gMaterialBlock)
        {
            if (block.storage != Shader::Reflection::HeapBlockStorage::eUniform)
            {
                throw std::runtime_error{std::format(
                    "The Material block of shader \"{}\" is a storage (buffer) block; the "
                    "renderer backs it with a uniform buffer, so declare it uniform "
                    "(VOG_MATERIAL does).",
                    info.name)};
            }
            if (block.size > gMaxMaterialBlockSize)
            {
                throw std::runtime_error{std::format(
                    "The Material block of shader \"{}\" is {} bytes, more than the {} a uniform "
                    "block holds.",
                    info.name,
                    block.size,
                    gMaxMaterialBlockSize)};
            }

            for (const Member& member : block.members)
            {
                info.members.push_back(materialMemberOf(info.name, member));
                info.parameters.push_back({.name = member.name});
            }
            info.blockSize = block.size;
        }
        else
        {
            throw std::runtime_error{std::format(
                "Shader \"{}\" reads the heap block \"{}\"; the renderer fills only the blocks "
                "named \"{}\" and \"{}\".",
                info.name,
                block.name,
                gFrameBlock,
                gMaterialBlock)};
        }
    }
}

std::shared_ptr<const Detail::ShaderInfo>
load(const Graphics::Vulkan::DevicePtr& device,
     const std::filesystem::path&       directory,
     const std::string&                 name,
     std::uint32_t                      programId)
{
    const std::filesystem::path vertexPath   = directory / (name + std::string{gVertexSuffix});
    const std::filesystem::path fragmentPath = directory / (name + std::string{gFragmentSuffix});

    try
    {
        const std::array includeDirectories{directory / gIncludeDirectory};
        const auto compile = [&](Shader::ShadingStage stage, const std::filesystem::path& path)
        {
            try
            {
                return std::shared_ptr<Shader>{device->createShader(
                    stage,
                    Graphics::Vulkan::compileGLSL(stage, readFile(path), includeDirectories))};
            }
            catch (const std::exception&)
            {
                std::throw_with_nested(
                    std::runtime_error{std::format("Failed to compile {}.", path.string())});
            }
        };

        auto info       = std::make_shared<Detail::ShaderInfo>();
        info->name      = name;
        info->programId = programId;
        info->program   = ShaderProgram::create(
            {.vertex   = {.shader = compile(Shader::ShadingStage::eVertex, vertexPath)},
             .fragment = {.shader = compile(Shader::ShadingStage::eFragment, fragmentPath)}});

        try
        {
            info->program->requirePushBlock(drawPushLayout());
        }
        catch (const std::invalid_argument&)
        {
            std::throw_with_nested(std::runtime_error{std::format(
                "Shader \"{}\" does not take the renderer's push data (mat4 model at 0, uint "
                "material at 64, uint frame at 68).",
                name)});
        }
        checkHeapBlocks(*info->program, *info);
        for (const auto& input : info->program->stages.vertex.shader->reflection.inAttributes)
        {
            info->inputs.push_back({.location      = input.location,
                                    .locationCount = input.locationCount,
                                    .name          = input.name,
                                    .type          = input.format});
        }

        return info;
    }
    catch (const std::exception&)
    {
        std::throw_with_nested(
            std::runtime_error{std::format("Failed to load shader \"{}\" ({}, {}).",
                                           name,
                                           vertexPath.string(),
                                           fragmentPath.string())});
    }
}
} // namespace

ShaderLibrary::ShaderLibrary(const Graphics::Vulkan::DevicePtr& device,
                             const std::filesystem::path&       directory)
    : mDirectory{directory}
{
    if (!std::filesystem::is_directory(directory))
    {
        throw std::runtime_error{
            std::format("The shader path {} is not a directory.", directory.string())};
    }

    std::set<std::string> vertexNames;
    std::set<std::string> fragmentNames;
    for (const auto& entry : std::filesystem::directory_iterator{directory})
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        if (std::string name = shaderNameOf(entry.path(), gVertexSuffix); !name.empty())
        {
            vertexNames.insert(std::move(name));
        }
        else if (name = shaderNameOf(entry.path(), gFragmentSuffix); !name.empty())
        {
            fragmentNames.insert(std::move(name));
        }
    }

    for (const std::string& name : vertexNames)
    {
        if (!fragmentNames.contains(name))
        {
            throw std::runtime_error{
                std::format("The shader \"{}\" has a vertex stage but no {}{} in {}.",
                            name,
                            name,
                            gFragmentSuffix,
                            directory.string())};
        }
    }
    for (const std::string& name : fragmentNames)
    {
        if (!vertexNames.contains(name))
        {
            throw std::runtime_error{
                std::format("The shader \"{}\" has a fragment stage but no {}{} in {}.",
                            name,
                            name,
                            gVertexSuffix,
                            directory.string())};
        }
        mShaders.emplace(name, load(device, directory, name, ++mLastProgramId));
    }
}

std::shared_ptr<const Detail::ShaderInfo>
ShaderLibrary::get(std::string_view name) const
{
    const auto found = mShaders.find(name);
    if (found == mShaders.end())
    {
        throw std::invalid_argument{
            std::format("No shader \"{}\" in {}.", name, mDirectory.string())};
    }

    return found->second;
}
} // namespace VOG::Engine
