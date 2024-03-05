#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 6) in vec3 v_color;

layout(location = 0) out vec4 outColor;

void main() {
	outColor = vec4(v_color, 1.0);
}