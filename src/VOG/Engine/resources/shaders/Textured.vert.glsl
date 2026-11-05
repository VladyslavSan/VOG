#version 450
#extension GL_GOOGLE_include_directive : require
#include "VOG/Draw.glsl"
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 0) out vec2 outUv;
void main() { gl_Position = gFrame.viewProjection * VOG_MODEL * vec4(inPosition, 1.0); outUv = inUv; }
