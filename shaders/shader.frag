#version 450

layout (location = 0) in vec3 f_position;
layout (location = 1) in vec3 f_normal;
layout (location = 2) in vec2 f_uv;

layout (location = 0) out vec4 final_color;

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

layout (binding = 2, std140) uniform Directional {
    vec3 dir;
    float _pad0;
    vec3 color;
    float _pad1;
    int enable;
} direct;

layout (binding = 3, std140) uniform PointLight {
    vec3 position;
    float _pad0;
    vec3 color;
    float _pad1;
    float constant;
    float linear;
    float quadratic;
    float _pad4;
    int enable;
} point;

layout (binding = 4, std140) uniform SpotLight {
    vec3 position;
    float _pad0;
    vec3 color;
    float _pad1;
    vec3 direction;
    float pad_2;
    float constant;
    float linear;
    float quadratic;
    float cutoff;
    int enable;
} spot;

layout(binding = 5) uniform sampler2D albedoMap;
layout(binding = 6) uniform sampler2D specularMap;
layout(binding = 7) uniform sampler2D emissiveMap;
layout(binding = 8) uniform sampler2DShadow shadowMap;
layout(binding = 9, std140) uniform ShadowUniforms {
    mat4 lightSpaceMatrix;
    vec4 params;
};

float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if(projCoords.z > 1.0)
        return 0.0;
        
    float currentBias = max(params.x * (1.0 - dot(normal, lightDir)), params.x * 0.1);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    
    vec2 sampleOffset = texelSize * params.y; 
    
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            shadow += texture(shadowMap, vec3(projCoords.xy + vec2(x, y) * sampleOffset, projCoords.z - currentBias)); 
        }
    }
    shadow /= 9.0;
    
    return (1.0 - shadow); 
}

vec3 computeDirectionalLight(vec3 normal, vec3 viewDir, vec3 albedo, vec3 specStrength, float shadow) {
    if (direct.enable == 0) return vec3(0.0);

    vec3 lightDir = normalize(-direct.dir);
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);

    vec3 ambient = 0.1 * direct.color * albedo;
    vec3 diffuse = direct.color * diff * albedo;
    vec3 specular = direct.color * spec * specStrength;

    return ambient + (1.0 - shadow) * (diffuse + specular);
}

vec3 computePointLight(vec3 normal, vec3 viewDir, vec3 fragPos, vec3 albedo, vec3 specStrength) {
    if (point.enable == 0) return vec3(0.0);

    vec3 lightDir = normalize(point.position - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);

    float distance = length(point.position - fragPos);
    float attenuation = 1.0 / (point.constant + point.linear * distance + point.quadratic * (distance * distance));

    vec3 ambient = 0.1 * point.color * albedo;
    vec3 diffuse = point.color * diff * albedo;
    vec3 specular = point.color * spec * specStrength;

    ambient *= attenuation;
    diffuse *= attenuation;
    specular *= attenuation;

    return ambient + diffuse + specular;
}

vec3 computeSpotLight(vec3 normal, vec3 viewDir, vec3 fragPos, vec3 albedo, vec3 specStrength) {
    if (spot.enable == 0) return vec3(0.0);

    vec3 lightDir = normalize(spot.position - fragPos);
    float spotFactor = dot(lightDir, normalize(-spot.direction));

    if(spotFactor > spot.cutoff)
    {
        float diff = max(dot(normal, lightDir), 0.0);

        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);

        float distance = length(spot.position - fragPos);
        float attenuation = 1.0 / (spot.constant + spot.linear * distance + spot.quadratic * (distance * distance));

        vec3 ambient = 0.1 * spot.color * albedo;
        vec3 diffuse = spot.color * diff * albedo;
        vec3 specular = spot.color * spec * specStrength;

        ambient *= attenuation;
        diffuse *= attenuation;
        specular *= attenuation;

        float spotIntensity = (1.0 - (1.0 - spotFactor) / (1.0 - spot.cutoff));
        return (ambient + diffuse + specular) * spotIntensity;    
    }
    else
    {
        return vec3(0.0);
    }
}

void main() {
    vec3 normal = normalize(f_normal);
    vec3 viewDir = normalize(viewPos - f_position);

    vec3 albedo;
    if (use_albedo_tex != 0) {
        vec2 final_uv = f_uv;
        
        if (use_sine_distortion != 0) {
            float amplitude = 0.1;
            float frequency = 5.0;
            float time_factor = time * 2.0;
            vec2 distortion = amplitude * sin(f_uv.yx * frequency + time_factor);
            final_uv = f_uv + distortion;
        }
        
        albedo = texture(albedoMap, final_uv).rgb;
    } else {
        albedo = albedo_color;
    }

    vec3 specStrength = (use_specular_tex != 0) ? texture(specularMap, f_uv).rgb : vec3(0.0);
    vec3 emission = (use_emissive_tex != 0) ? texture(emissiveMap, f_uv).rgb : vec3(0.0);

    float shadow = 0.0;
    if (direct.enable != 0 && params.z > 0.5) {
        vec4 fragPosLightSpace = lightSpaceMatrix * vec4(f_position, 1.0);
        shadow = ShadowCalculation(fragPosLightSpace, normal, normalize(-direct.dir));
    }

    vec3 result = vec3(0.0);

    if (direct.enable != 0) {
        result += computeDirectionalLight(normal, viewDir, albedo, specStrength, shadow);
    }

    if (point.enable != 0) {
        result += computePointLight(normal, viewDir, f_position, albedo, specStrength);
    }

    if (spot.enable != 0) {
        result += computeSpotLight(normal, viewDir, f_position, albedo, specStrength);
    }

    if (direct.enable == 0 && point.enable == 0 && spot.enable == 0) {
        result = 0.1 * albedo;
    }

    vec3 finalRGB = result + emission;

    final_color = vec4(finalRGB, 1.0);
}