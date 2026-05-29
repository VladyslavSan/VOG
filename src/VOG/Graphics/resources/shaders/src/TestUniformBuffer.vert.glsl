#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;

layout(location = 1) out vec4 out_color;

layout(set = 0, binding = 0) uniform Scene
{
    mat4 mvp;
} scene;

layout(set = 1, binding = 2) uniform UniformBufer
{
    float value;
} ubo;

void main() {
    out_color = in_color;
    gl_Position = vec4(in_position, 1.0);
}