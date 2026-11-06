// Include right after `#version 450` and `#extension GL_GOOGLE_include_directive : require`.
// Do not declare a `bool` (or a bvec) member in a heap block such as VOG_MATERIAL: the compiler crashes
// on one that the shader reads (GLSLCompiler.hpp); use a uint.
// Pass a TextureRef to vogSample by reference, as `gMaterial.albedo`, never copy it into a variable
// or a function parameter first: a copy of it is invalid SPIR-V.
#ifndef VOG_DRAW_GLSL
#define VOG_DRAW_GLSL
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : require
#define VOG_HEAP descriptor_heap, descriptor_stride = 128        // DescriptorHeapLayout::kSlotSize; ShaderProgram rejects another stride on a heap block, an image array is not checked

layout(VOG_HEAP) uniform texture2D gTextures[];
layout(descriptor_heap) uniform sampler gSamplers[];           // 0 linear, 1 nearest

layout(push_constant) uniform Draw { mat4 model; uint material; uint frame; } gDraw;   // 72 bytes

layout(VOG_HEAP) uniform Frame {                                // written once per frame by the renderer
    mat4  view; mat4 projection; mat4 viewProjection;
    vec4  cameraPosition;                                       // xyz world
    vec2  viewport;                                             // pixels
    float timeSeconds; float deltaSeconds;
} gFrames[];

struct TextureRef { uint index; uint samplerIndex; };   // `sampler` is a GLSL keyword
#define VOG_MATERIAL        layout(VOG_HEAP) uniform Material    // the shader writes: VOG_MATERIAL { ... } gMaterials[];
#define VOG_MODEL           gDraw.model                         // where the model matrix comes from
#define VOG_MATERIAL_INDEX  gDraw.material                      // where the material index comes from
#define gFrame              gFrames[gDraw.frame]
#define gMaterial           gMaterials[VOG_MATERIAL_INDEX]
#define vogSample(ref, uv)  texture(sampler2D(gTextures[(ref).index], gSamplers[(ref).samplerIndex]), (uv))
#endif
