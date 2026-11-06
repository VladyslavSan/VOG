#pragma once

#include <VOG/Math/Math.hpp>

#include <cstddef>
#include <cstdint>

namespace VOG::Engine
{
/**
 * The push data of every draw: the include VOG/Draw.glsl declares the same block, `Draw`. The
 * indices are the heap indices of the Material block and of the Frame block that the draw reads.
 */
struct DrawPushData
{
    Math::Matrix4x4f model;
    std::uint32_t    material;
    std::uint32_t    frame;
};

// The numbers are the layout that VOG/Draw.glsl declares.
// NOLINTBEGIN(readability-magic-numbers)
static_assert(sizeof(DrawPushData) == 72u);
static_assert(offsetof(DrawPushData, model) == 0u);
static_assert(offsetof(DrawPushData, material) == 64u);
static_assert(offsetof(DrawPushData, frame) == 68u);

/**
 * The block `Frame` of VOG/Draw.glsl, written once per frame into the heap buffer of the frame
 * slot, at std140 offsets.
 */
struct FrameBlock
{
    Math::Matrix4x4f view;
    Math::Matrix4x4f projection;
    Math::Matrix4x4f viewProjection;
    /** The camera in world space; w is 1. */
    Math::Vector4f cameraPosition;
    /** The size of the color target in pixels. */
    Math::Vector2f viewport;
    float          timeSeconds;
    float          deltaSeconds;
};

static_assert(sizeof(FrameBlock) == 224u);
static_assert(offsetof(FrameBlock, cameraPosition) == 192u);
static_assert(offsetof(FrameBlock, viewport) == 208u);
static_assert(offsetof(FrameBlock, timeSeconds) == 216u);
static_assert(offsetof(FrameBlock, deltaSeconds) == 220u);

/** The struct `TextureRef` of VOG/Draw.glsl, as a Material block holds it. */
struct TextureRef
{
    std::uint32_t index;
    std::uint32_t samplerIndex;
};

static_assert(sizeof(TextureRef) == 8u);
// NOLINTEND(readability-magic-numbers)
} // namespace VOG::Engine
