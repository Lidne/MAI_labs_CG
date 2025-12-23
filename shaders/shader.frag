#version 450

// NOTE: out attributes of vertex shader must be in's
// layout (location = 0) in type name;

// NOTE: Pixel color
layout (location = 0) out vec4 final_color;

// NOTE: Must match declaration order of a C struct
layout (push_constant, std430) uniform ShaderConstants {
	mat4 projection;
	mat4 transform;
	vec3 color;
};

layout (location = 0) in vec3 f_color;
layout (location = 1) in vec3 f_bc;

void main() {
	vec3 col = normalize(max(f_color, vec3(0.0))) ;
	final_color = vec4(col, 1.0f);
}
