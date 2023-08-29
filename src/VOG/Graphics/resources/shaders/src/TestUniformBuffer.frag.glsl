#version 450

layout(location = 0) in vec4 color;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 2) uniform UniformBufer
{
    float value;
} ubo;

void main() {
    outColor = color;
}