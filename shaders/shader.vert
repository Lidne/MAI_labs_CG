#version 450

const int MAX_POINT_LIGHTS = 4;

struct DirectionalLight {
	vec3 dir;
	float _pad0;
	vec3 color;
	float _pad1;
	int enable;
	int _pad2[3];
};

struct PointLight {
	vec3 position;
	float _pad0;
	vec3 color;
	float _pad1;
	float constant;
	float linear;
	float quadratic;
	float _pad4;
	int enable;
};

layout (location = 0) in vec3 v_position;
layout (location = 1) in vec3 v_normal;
layout (location = 2) in vec2 v_uv;

layout (location = 0) out vec3 f_position;
layout (location = 1) out vec3 f_normal;
layout (location = 2) out vec2 f_uv;

layout (binding = 0, std140) uniform SceneUniforms {
	mat4 view_projection;
	vec4 camera_position;
	vec4 ambient_color;
	DirectionalLight directional_light;
	PointLight point_lights[MAX_POINT_LIGHTS];
} scene;

layout (binding = 1, std140) uniform ModelUniforms {
	mat4 model;
	vec3 albedo_color;
};

void main() {
	vec4 world_position = model * vec4(v_position, 1.0f);
	mat3 normal_matrix = mat3(transpose(inverse(model)));
	vec3 world_normal = normalize(normal_matrix * v_normal);

	gl_Position = scene.view_projection * world_position;

	f_position = world_position.xyz;
	f_normal = world_normal;
	f_uv = v_uv;
}
