#version 450
#extension GL_GOOGLE_include_directive : require
#include "VOG/Draw.glsl"
VOG_MATERIAL { vec4 tint; TextureRef albedo; } gMaterials[];
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
void main() { outColor = vogSample(gMaterial.albedo, inUv) * gMaterial.tint; }
