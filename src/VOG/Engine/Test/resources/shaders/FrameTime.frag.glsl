#version 450
#extension GL_GOOGLE_include_directive : require
#include "VOG/Draw.glsl"

layout(location = 0) out vec4 outColor;

// The Frame block as a color: the time, the time between frames, and the width of the viewport.
void main() {
    outColor = vec4(gFrame.timeSeconds, gFrame.deltaSeconds, gFrame.viewport.x / 32.0, 1.0);
}
