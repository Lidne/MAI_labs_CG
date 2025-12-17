#version 450

layout (location = 0) in vec3 f_position;
layout (location = 1) in vec3 f_normal;
layout (location = 2) in vec2 f_uv;

layout (location = 0) out vec4 final_color;

layout (binding = 1, std140) uniform ModelUniforms {
	mat4 model;
	vec3 albedo_color;
};

layout (location = 0) in vec3 f_color;
layout (location = 1) in vec3 f_bc;

void main() {
	vec3 col = normalize(max(f_color, vec3(0.0))) ;
	final_color = vec4(col, 1.0f);
}
