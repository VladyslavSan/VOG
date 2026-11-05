#pragma once

#include <memory>
#include <span>
#include <string>
#include <utility>

namespace VOG::Engine
{
/** A parameter of a shader's Material block. */
struct ShaderParameter
{
    std::string name;
};

namespace Detail
{
class ShaderInfo;
} // namespace Detail

class Renderer;

/**
 * A program of the shader directory, compiled and reflected when the Renderer was constructed;
 * made by Renderer::getShader(). A copy shares the shader with the original. A default-constructed
 * Shader is empty: it is no shader, and Renderer::createMaterial() rejects it.
 */
class Shader
{
public:
    Shader() = default;

    /** True for a shader that getShader() made. */
    explicit
    operator bool() const noexcept
    {
        return static_cast<bool>(mData);
    }

    bool operator==(const Shader&) const = default;

    /**
     * The members of its Material block, in block order: what Renderer::createMaterial() needs a
     * value for. Empty for a shader that reads no Material block, and for an empty Shader.
     */
    [[nodiscard]] std::span<const ShaderParameter> parameters() const noexcept;

private:
    friend class Renderer;
    friend class ResourceFactory;
    friend struct HandleTestAccess;

    explicit Shader(std::shared_ptr<const Detail::ShaderInfo> data)
        : mData{std::move(data)}
    {
    }

    std::shared_ptr<const Detail::ShaderInfo> mData;
};
} // namespace VOG::Engine
