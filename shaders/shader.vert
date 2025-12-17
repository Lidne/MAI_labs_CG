#version 450

layout (location = 0) in vec3 v_position;
layout (location = 1) in vec3 v_normal;
layout (location = 2) in vec2 v_uv;

layout (location = 0) out vec3 f_position;
layout (location = 1) out vec3 f_normal;
layout (location = 2) out vec2 f_uv;

layout (binding = 0, std140) uniform SceneUniforms {
	mat4 view_projection;
	vec3 viewPos;
	float time;
};

layout (binding = 1, std140) uniform ModelUniforms {
	mat4 model;
	vec3 albedo_color; 
	float _pad0;
	int use_albedo_tex;
	float _pad1;
	int use_specular_tex;
	float _pad2;
	int use_emissive_tex;
	float _pad3;
	int use_sine_distortion;
	float _pad4;
};

void main() {

	vec4 worldPosition = model * vec4(v_position, 1.0f);





	vec3 normal = normalize(mat3(model) * v_normal);


	gl_Position = view_projection * worldPosition;


	f_position = worldPosition.xyz;
	f_normal = normal;
	f_uv = v_uv;
}