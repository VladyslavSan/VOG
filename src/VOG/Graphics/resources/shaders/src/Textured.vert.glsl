#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec2 outUv;

// The push data of every draw of the renderer; see MaterialDefinition.hpp.
layout(push_constant) uniform PushData
{
    mat4 mvp;
    uint material;
    uint textures[4];
} pushData;

void main() {
    gl_Position = pushData.mvp * vec4(inPosition, 0.0, 1.0);
    outUv = inUv;
}
