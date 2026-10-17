#version 450
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : require

// Slots are 128 bytes apart in the descriptor heap; see DescriptorHeapLayout::kSlotSize.
layout(descriptor_heap, descriptor_stride = 128) uniform texture2D gTextures[];
layout(descriptor_heap, descriptor_stride = 128) uniform Material
{
    vec4 tint;
} gMaterials[];
layout(descriptor_heap) uniform sampler gSamplers[];

// The push data of every draw of the renderer; see MaterialDefinition.hpp.
layout(push_constant) uniform PushData
{
    mat4 mvp;
    uint material;
    uint textures[4];
} pushData;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(sampler2D(gTextures[pushData.textures[0]], gSamplers[0]), inUv) *
               gMaterials[pushData.material].tint;
}
