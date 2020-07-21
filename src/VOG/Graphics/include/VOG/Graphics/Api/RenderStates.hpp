#pragma once

#include <cstdint>
#include <optional>

namespace VOG::Graphics::Api
{
namespace RenderStates
{
using EnumStateType = std::uint8_t;
enum class PolygonMode : EnumStateType
{
    Point = 0,
    Line,
    Fill
};

enum class CullMode : EnumStateType
{
    None,
    Front,
    Back,
    FrontAndBack,
};

enum class FrontFace : EnumStateType
{
    CW,
    CCW
};

enum class CompareOp : EnumStateType
{
    Always = 0,
    Less,
    LessOrEqual,
    Greater,
    GreaterOrEqual,
    Equal,
    NotEqual,
    Never
};

enum class StencilOp : EnumStateType
{
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementAndWrap,
    DecrementAndWrap
};

enum class ColorWriteMask : EnumStateType
{
    R = (1 << 0),
    G = (1 << 1),
    B = (1 << 2),
    A = (1 << 3),
    RG = R | G,
    RGB = R | G | B,
    RGBA = R | G | B | A
};

enum class BlendFactor : EnumStateType
{
    Zero,
    One,
    SrcColor,
    InvSrcColor,
    DstColor,
    InvDstColor,
    SrcAlpha,
    InvSrcAlpha,
    DstAlpha,
    InvDstAlpha,
    Saturate
};

enum class BlendOp : EnumStateType
{
    Add,
    Sub,
    RevSub,
    Min,
    Max
};

struct DepthBias
{
    float constant;
    float clamp;
    float slope;
};

struct DepthTest
{
    bool enabled = false;
    bool writeEnabled = false;
    CompareOp compareOp = CompareOp::Always;
};

struct StencilFaceOp
{
    StencilOp stencilFailOp = StencilOp::Keep;
    StencilOp stencilPassOp = StencilOp::Keep;
    StencilOp depthFailOp = StencilOp::Keep;
};

struct StencilTest
{
    bool enabled = false;
    StencilFaceOp front = {.stencilFailOp = StencilOp::Keep,
                           .stencilPassOp = StencilOp::Keep,
                           .depthFailOp = StencilOp::Keep};
    StencilFaceOp back = {.stencilFailOp = StencilOp::Keep,
                          .stencilPassOp = StencilOp::Keep,
                          .depthFailOp = StencilOp::Keep};
};

struct ColorBlend
{
    ColorWriteMask colorWriteMask = ColorWriteMask::RGBA;
    bool blendEnabled = false;
    BlendFactor srcColorBlendFactor = BlendFactor::One;
    BlendFactor dstColorBlendFactor = BlendFactor::Zero;
    BlendOp colorBlendOp = BlendOp::Add;
    BlendFactor srcAlphaBlendFactor = BlendFactor::One;
    BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
    BlendOp alphaBlendOp = BlendOp::Add;
};

struct RenderState
{
    PolygonMode polygonMode;
    CullMode cullMode;
    FrontFace frontFace;
    std::optional<DepthBias> depthBias;
    std::optional<DepthTest> depthTest;
    std::optional<StencilTest> stencilTest;
    std::optional<ColorBlend> colorBlend;
};

constexpr RenderState DefaultRenderStates{
    .polygonMode = PolygonMode::Fill,
    .cullMode = CullMode::None,
    .frontFace = FrontFace::CCW,
    .depthBias = {},
    .depthTest = DepthTest{.enabled = false, .writeEnabled = false, .compareOp = CompareOp::Always},
    .stencilTest = StencilTest{.enabled = false,
                               .front = {.stencilFailOp = StencilOp::Keep,
                                         .stencilPassOp = StencilOp::Keep,
                                         .depthFailOp = StencilOp::Keep},
                               .back = {.stencilFailOp = StencilOp::Keep,
                                        .stencilPassOp = StencilOp::Keep,
                                        .depthFailOp = StencilOp::Keep}},
    .colorBlend = ColorBlend{
        .colorWriteMask = ColorWriteMask::RGBA,
        .blendEnabled = false,
        .srcColorBlendFactor = BlendFactor::One,
        .dstColorBlendFactor = BlendFactor::One,
        .colorBlendOp = BlendOp::Add,
        .srcAlphaBlendFactor = BlendFactor::One,
        .dstAlphaBlendFactor = BlendFactor::One,
        .alphaBlendOp = BlendOp::Add,
    }};

struct Viewport
{
};
} // namespace RenderStates
} // namespace VOG::Graphics::Api
