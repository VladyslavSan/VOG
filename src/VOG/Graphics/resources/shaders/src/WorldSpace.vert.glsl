#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

//push constants block
layout( push_constant ) uniform constants
{
	mat4 MVP;
} PushConstants;

void main() {
    gl_Position = PushConstants.MVP * vec4(inPosition, 1.0);
    outColor = inColor;
}