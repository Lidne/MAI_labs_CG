#version 450

const int MAX_POINT_LIGHTS = 4;

struct DirectionalLight {
	vec3 dir;
	float _pad0;
	vec3 color;
	float _pad1;
	int enable;
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

layout (location = 0) in vec3 f_position;
layout (location = 1) in vec3 f_normal;
layout (location = 2) in vec2 f_uv;

layout (location = 0) out vec4 final_color;

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

layout (binding = 2) uniform sampler2D texSampler;

vec3 evaluateDirectionalLight(vec3 normal, vec3 view_dir, vec3 material_color) {
	if (scene.directional_light.enable == 0) {
		return vec3(0.0);
	}

	vec3 light_dir = normalize(-scene.directional_light.dir);
	float diff_intensity = max(dot(normal, light_dir), 0.0);

	vec3 half_dir = normalize(light_dir + view_dir);
	const float shininess = 32.0;
	float spec_intensity = pow(max(dot(normal, half_dir), 0.0), shininess);

	vec3 diffuse = diff_intensity * scene.directional_light.color * material_color;
	vec3 specular = spec_intensity * scene.directional_light.color;

	return diffuse + specular;
}

vec3 evaluatePointLight(PointLight light, vec3 normal, vec3 view_dir, vec3 material_color) {
	if (light.enable == 0) {
		return vec3(0.0);
	}

	vec3 light_vector = light.position - f_position;
	float distance_to_light = length(light_vector);
	vec3 light_dir = light_vector / max(distance_to_light, 0.0001);

	float attenuation = 1.0 / max(
		light.constant +
		light.linear * distance_to_light +
		light.quadratic * distance_to_light * distance_to_light,
		0.0001);

	float diff_intensity = max(dot(normal, light_dir), 0.0);
	vec3 half_dir = normalize(light_dir + view_dir);
	const float shininess = 32.0;
	float spec_intensity = pow(max(dot(normal, half_dir), 0.0), shininess);

	vec3 diffuse = diff_intensity * light.color * material_color;
	vec3 specular = spec_intensity * light.color;

	return (diffuse + specular) * attenuation;
}

void main() {
	vec3 normal = normalize(f_normal);
	vec3 view_dir = normalize(scene.camera_position.xyz - f_position);
	vec3 base_color = texture(texSampler, f_uv).rgb * albedo_color;

	vec3 color = scene.ambient_color.rgb * base_color;
	color += evaluateDirectionalLight(normal, view_dir, base_color);

	for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
		color += evaluatePointLight(scene.point_lights[i], normal, view_dir, base_color);
	}

	final_color = vec4(max(color, 0.0), 1.0);
}
