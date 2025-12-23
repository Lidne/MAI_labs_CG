#include <climits>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

#include "veekay/input.hpp"

#define _USE_MATH_DEFINES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <lodepng.h>
#include <math.h>
#include <veekay/Camera.h>
#include <veekay/Lights.h>
#include <vulkan/vulkan_core.h>

#include <veekay/veekay.hpp>

namespace {

bool g_UseLookAt = true;
bool g_FpsMode = false;
bool g_UseSpecular = true;
bool g_UseEmissive = false;
bool g_UseSineDistortion = false;

constexpr uint32_t max_models = 1024;

struct Vertex {
    veekay::vec3 position;
    veekay::vec3 normal;
    veekay::vec2 uv;
    // NOTE: You can add more attributes
};

struct SceneUniforms {
    glm::mat4 view_projection;
    glm::vec3 viewPos;
    float time;
};

struct ModelUniforms {
    veekay::mat4 model;
    veekay::vec3 albedo_color;
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

struct ShadowUniforms {
    glm::mat4 light_view_projection;
    // .x = bias
    // .y = pcf (множитель размера)
    // .z, .w = паддинг (не используется)
    glm::vec4 params;
};

struct Mesh {
    veekay::graphics::Buffer* vertex_buffer;
    veekay::graphics::Buffer* index_buffer;
    uint32_t indices;
};

struct Transform {
    veekay::vec3 position = {};
    veekay::vec3 scale = {1.0f, 1.0f, 1.0f};
    veekay::vec3 rotation = {};

    // NOTE: Model matrix (translation, rotation and scaling)
    veekay::mat4 matrix() const {
        auto t = veekay::mat4::translation(position);

        return t;
    }
};

struct Model {
    Mesh mesh;
    Transform transform;
    veekay::vec3 albedo_color;
    veekay::graphics::Texture* albedo_tex = nullptr;
    veekay::graphics::Texture* specular_tex = nullptr;
    veekay::graphics::Texture* emissive_tex = nullptr;
    VkSampler albedo_sampler = VK_NULL_HANDLE;
    VkSampler specular_sampler = VK_NULL_HANDLE;
    VkSampler emissive_sampler = VK_NULL_HANDLE;
};

// NOTE: Scene objects
inline namespace {
Camera camera(60.0f, 0.01f, 100.0f, 1280, 720, g_UseLookAt);

DirectionalLight dirLight;
PointLight pointLight;
SpotLight spotLight;

std::vector<Model> models;
}  // namespace

// NOTE: Vulkan objects
inline namespace {
VkShaderModule vertex_shader_module;
VkShaderModule fragment_shader_module;

VkDescriptorPool descriptor_pool;
VkDescriptorSetLayout descriptor_set_layout;
VkDescriptorSet descriptor_set;

VkPipelineLayout pipeline_layout;
VkPipeline pipeline;

veekay::graphics::Buffer* scene_uniforms_buffer;
veekay::graphics::Buffer* model_uniforms_buffer;
veekay::graphics::Buffer* dirlight_uniforms_buffer;
veekay::graphics::Buffer* pointlight_uniforms_buffer;
veekay::graphics::Buffer* spotlight_uniforms_buffer;

Mesh plane_mesh;
Mesh cube_mesh;

veekay::graphics::Texture* missing_texture;
VkSampler missing_texture_sampler;

veekay::graphics::Texture* albedo_texture = nullptr;
veekay::graphics::Texture* specular_texture = nullptr;
veekay::graphics::Texture* emissive_texture = nullptr;

VkSampler albedo_sampler = VK_NULL_HANDLE;
VkSampler specular_sampler = VK_NULL_HANDLE;
VkSampler emissive_sampler = VK_NULL_HANDLE;

veekay::graphics::Texture* shadow_map = nullptr;
VkSampler shadow_sampler = VK_NULL_HANDLE;
veekay::graphics::Buffer* shadow_uniforms_buffer = nullptr;

VkRenderPass shadow_render_pass = VK_NULL_HANDLE;
VkFramebuffer shadow_framebuffer = VK_NULL_HANDLE;
VkPipelineLayout shadow_pipeline_layout = VK_NULL_HANDLE;
VkPipeline shadow_pipeline = VK_NULL_HANDLE;
VkShaderModule shadow_vertex_shader = VK_NULL_HANDLE;
VkShaderModule shadow_fragment_shader = VK_NULL_HANDLE;
VkDescriptorSetLayout shadow_descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorPool shadow_descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSet shadow_descriptor_set = VK_NULL_HANDLE;

bool g_UseShadows = true;
float g_ShadowBias = 0.005f;
float g_ShadowPCF = 2.0f;
}  // namespace

uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkDevice device = veekay::app.vk_device;
    VkPhysicalDevice physicalDevice = veekay::app.vk_physical_device;

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

veekay::graphics::Texture* createShadowMap(VkCommandBuffer cmd, uint32_t width, uint32_t height) {
    VkDevice device = veekay::app.vk_device;

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.compareEnable = VK_TRUE;  // Важно для PCF
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &shadow_sampler) != VK_SUCCESS) {
        std::cerr << "Failed to create shadow sampler\n";
        return nullptr;
    }

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image;
    vkCreateImage(device, &imageInfo, nullptr, &image);

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, image, &memReqs);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory memory;
    vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    vkBindImageMemory(device, image, memory, 0);

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view;
    vkCreateImageView(device, &viewInfo, nullptr, &view);

    return new veekay::graphics::Texture(width, height, VK_FORMAT_D32_SFLOAT, image, view, memory);
}

float toRadians(float degrees) {
    return degrees * float(M_PI) / 180.0f;
}

// NOTE: Loads shader byte code from file
// NOTE: Your shaders are compiled via CMake with this code too, look it up
VkShaderModule loadShaderModule(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    size_t size = file.tellg();
    std::vector<uint32_t> buffer(size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    file.close();

    VkShaderModuleCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = buffer.data(),
    };

    VkShaderModule result;
    if (vkCreateShaderModule(veekay::app.vk_device, &info, nullptr, &result) != VK_SUCCESS) {
        return nullptr;
    }

    return result;
}

void initialize(VkCommandBuffer cmd) {
    VkDevice& device = veekay::app.vk_device;
    VkPhysicalDevice& physical_device = veekay::app.vk_physical_device;

    {  // NOTE: Build graphics pipeline
        vertex_shader_module = loadShaderModule("./shaders/shader.vert.spv");
        if (!vertex_shader_module) {
            std::cerr << "Failed to load Vulkan vertex shader from file\n";
            veekay::app.running = false;
            return;
        }

        fragment_shader_module = loadShaderModule("./shaders/shader.frag.spv");
        if (!fragment_shader_module) {
            std::cerr << "Failed to load Vulkan fragment shader from file\n";
            veekay::app.running = false;
            return;
        }

        VkPipelineShaderStageCreateInfo stage_infos[2];

        // NOTE: Vertex shader stage
        stage_infos[0] = VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader_module,
            .pName = "main",
        };

        // NOTE: Fragment shader stage
        stage_infos[1] = VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shader_module,
            .pName = "main",
        };

        // NOTE: How many bytes does a vertex take?
        VkVertexInputBindingDescription buffer_binding{
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        // NOTE: Declare vertex attributes
        VkVertexInputAttributeDescription attributes[] = {
            {
                .location = 0,                         // NOTE: First attribute
                .binding = 0,                          // NOTE: First vertex buffer
                .format = VK_FORMAT_R32G32B32_SFLOAT,  // NOTE: 3-component vector of floats
                .offset = offsetof(Vertex, position),  // NOTE: Offset of "position" field in a Vertex struct
            },
            {
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, normal),
            },
            {
                .location = 2,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(Vertex, uv),
            },
        };

        // NOTE: Describe inputs
        VkPipelineVertexInputStateCreateInfo input_state_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &buffer_binding,
            .vertexAttributeDescriptionCount = sizeof(attributes) / sizeof(attributes[0]),
            .pVertexAttributeDescriptions = attributes,
        };

        // NOTE: Every three vertices make up a triangle,
        //       so our vertex buffer contains a "list of triangles"
        VkPipelineInputAssemblyStateCreateInfo assembly_state_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        // NOTE: Declare clockwise triangle order as front-facing
        //       Discard triangles that are facing away
        //       Fill triangles, don't draw lines instaed
        VkPipelineRasterizationStateCreateInfo raster_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f,
        };

        // NOTE: Use 1 sample per pixel
        VkPipelineMultisampleStateCreateInfo sample_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = false,
            .minSampleShading = 1.0f,
        };

        VkViewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(veekay::app.window_width),
            .height = static_cast<float>(veekay::app.window_height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        VkRect2D scissor{
            .offset = {0, 0},
            .extent = {veekay::app.window_width, veekay::app.window_height},
        };

        // NOTE: Let rasterizer draw on the entire window
        VkPipelineViewportStateCreateInfo viewport_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,

            .viewportCount = 1,
            .pViewports = &viewport,

            .scissorCount = 1,
            .pScissors = &scissor,
        };

        // NOTE: Let rasterizer perform depth-testing and overwrite depth values on condition pass
        VkPipelineDepthStencilStateCreateInfo depth_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = true,
            .depthWriteEnable = true,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        };

        // NOTE: Let fragment shader write all the color channels
        VkPipelineColorBlendAttachmentState attachment_info{
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                              VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
        };

        // NOTE: Let rasterizer just copy resulting pixels onto a buffer, don't blend
        VkPipelineColorBlendStateCreateInfo blend_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,

            .logicOpEnable = false,
            .logicOp = VK_LOGIC_OP_COPY,

            .attachmentCount = 1,
            .pAttachments = &attachment_info};

        {
            VkDescriptorPoolSize pools[] = {
                {
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 16,
                },
                {
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                    .descriptorCount = 8,
                },
                {
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 16,
                }};

            VkDescriptorPoolCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = 1,
                .poolSizeCount = sizeof(pools) / sizeof(pools[0]),
                .pPoolSizes = pools,
            };

            if (vkCreateDescriptorPool(device, &info, nullptr,
                                       &descriptor_pool) != VK_SUCCESS) {
                std::cerr << "Failed to create Vulkan descriptor pool\n";
                veekay::app.running = false;
                return;
            }
        }

        // NOTE: Descriptor set layout specification
        {
            VkDescriptorSetLayoutBinding bindings[] = {
                {
                    .binding = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 2,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 3,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 4,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 5,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 6,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 7,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 8,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 9,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
            };

            VkDescriptorSetLayoutCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = sizeof(bindings) / sizeof(bindings[0]),
                .pBindings = bindings,
            };

            if (vkCreateDescriptorSetLayout(device, &info, nullptr,
                                            &descriptor_set_layout) != VK_SUCCESS) {
                std::cerr << "Failed to create Vulkan descriptor set layout\n";
                veekay::app.running = false;
                return;
            }
        }

        {
            VkDescriptorSetAllocateInfo info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &descriptor_set_layout,
            };

            if (vkAllocateDescriptorSets(device, &info, &descriptor_set) != VK_SUCCESS) {
                std::cerr << "Failed to create Vulkan descriptor set\n";
                veekay::app.running = false;
                return;
            }
        }

        // NOTE: Declare external data sources, only push constants this time
        VkPipelineLayoutCreateInfo layout_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &descriptor_set_layout,
        };

        // NOTE: Create pipeline layout
        if (vkCreatePipelineLayout(device, &layout_info,
                                   nullptr, &pipeline_layout) != VK_SUCCESS) {
            std::cerr << "Failed to create Vulkan pipeline layout\n";
            veekay::app.running = false;
            return;
        }

        VkGraphicsPipelineCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2,
            .pStages = stage_infos,
            .pVertexInputState = &input_state_info,
            .pInputAssemblyState = &assembly_state_info,
            .pViewportState = &viewport_info,
            .pRasterizationState = &raster_info,
            .pMultisampleState = &sample_info,
            .pDepthStencilState = &depth_info,
            .pColorBlendState = &blend_info,
            .layout = pipeline_layout,
            .renderPass = veekay::app.vk_render_pass,
        };

        // NOTE: Create graphics pipeline
        if (vkCreateGraphicsPipelines(device, nullptr,
                                      1, &info, nullptr, &pipeline) != VK_SUCCESS) {
            std::cerr << "Failed to create Vulkan pipeline\n";
            veekay::app.running = false;
            return;
        }
    }

    scene_uniforms_buffer = new veekay::graphics::Buffer(
        sizeof(SceneUniforms),
        nullptr,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    model_uniforms_buffer = new veekay::graphics::Buffer(
        max_models * sizeof(ModelUniforms),
        nullptr,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    dirlight_uniforms_buffer = new veekay::graphics::Buffer(
        sizeof(DirectionalLight),
        nullptr,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    pointlight_uniforms_buffer = new veekay::graphics::Buffer(
        sizeof(PointLight),
        nullptr,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    spotlight_uniforms_buffer = new veekay::graphics::Buffer(
        sizeof(SpotLight),
        nullptr,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    shadow_uniforms_buffer = new veekay::graphics::Buffer(
        sizeof(ShadowUniforms),
        nullptr,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    // NOTE: This texture and sampler is used when texture could not be loaded
    {
        VkSamplerCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        };

        if (vkCreateSampler(device, &info, nullptr, &missing_texture_sampler) != VK_SUCCESS) {
            std::cerr << "Failed to create Vulkan texture sampler\n";
            veekay::app.running = false;
            return;
        }

        uint32_t pixels[] = {
            0xff000000,
            0xffff00ff,
            0xffff00ff,
            0xff000000,
        };

        missing_texture = new veekay::graphics::Texture(cmd, 2, 2,
                                                        VK_FORMAT_B8G8R8A8_UNORM,
                                                        pixels);
    }

    {
        auto loadTexture = [&](const char* path) -> veekay::graphics::Texture* {
            unsigned w, h;
            std::vector<unsigned char> pixels;
            unsigned error = lodepng::decode(pixels, w, h, path);
            if (error) {
                std::cerr << "Failed to load texture " << path << ": " << lodepng_error_text(error) << "\n";
                return nullptr;
            }
            return new veekay::graphics::Texture(cmd, w, h, VK_FORMAT_R8G8B8A8_UNORM, pixels.data());
        };

        auto createSampler = [&](VkSampler& sampler) {
            VkSamplerCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                        .magFilter = VK_FILTER_LINEAR,
                                        .minFilter = VK_FILTER_LINEAR,
                                        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                        .anisotropyEnable = VK_TRUE,
                                        .maxAnisotropy = 16.0f};
            vkCreateSampler(device, &info, nullptr, &sampler);
        };

        albedo_texture = loadTexture("assets/gorilla.png");
        specular_texture = loadTexture("assets/specular.png");
        emissive_texture = loadTexture("assets/cube.png");

        if (!albedo_texture) albedo_texture = missing_texture;
        if (!specular_texture) specular_texture = missing_texture;
        if (!emissive_texture) emissive_texture = missing_texture;

        createSampler(albedo_sampler);
        createSampler(specular_sampler);
        createSampler(emissive_sampler);

        VkDescriptorImageInfo image_infos[] = {
            {.sampler = albedo_sampler, .imageView = albedo_texture->view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {.sampler = specular_sampler, .imageView = specular_texture->view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {.sampler = emissive_sampler, .imageView = emissive_texture->view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        };

        VkWriteDescriptorSet image_writes[] = {
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
             .dstSet = descriptor_set,
             .dstBinding = 5,
             .descriptorCount = 1,
             .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .pImageInfo = &image_infos[0]},
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
             .dstSet = descriptor_set,
             .dstBinding = 6,
             .descriptorCount = 1,
             .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .pImageInfo = &image_infos[1]},
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
             .dstSet = descriptor_set,
             .dstBinding = 7,
             .descriptorCount = 1,
             .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .pImageInfo = &image_infos[2]},
        };

        vkUpdateDescriptorSets(device, 3, image_writes, 0, nullptr);
    }

    {
        VkDescriptorBufferInfo buffer_infos[] = {
            {
                .buffer = scene_uniforms_buffer->buffer,
                .offset = 0,
                .range = sizeof(SceneUniforms),
            },
            {
                .buffer = model_uniforms_buffer->buffer,
                .offset = 0,
                .range = sizeof(ModelUniforms),
            },
            {
                .buffer = dirlight_uniforms_buffer->buffer,
                .offset = 0,
                .range = sizeof(DirectionalLight),
            },
            {
                .buffer = pointlight_uniforms_buffer->buffer,
                .offset = 0,
                .range = sizeof(PointLight),
            },
            {
                .buffer = spotlight_uniforms_buffer->buffer,
                .offset = 0,
                .range = sizeof(SpotLight),
            },
            {
                .buffer = shadow_uniforms_buffer->buffer,
                .offset = 0,
                .range = sizeof(ShadowUniforms),
            },
        };

        VkWriteDescriptorSet write_infos[] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_infos[0],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set,
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .pBufferInfo = &buffer_infos[1],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set,
                .dstBinding = 2,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_infos[2],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set,
                .dstBinding = 3,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_infos[3],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set,
                .dstBinding = 4,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_infos[4],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set,
                .dstBinding = 9,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_infos[5],
            },
        };

        vkUpdateDescriptorSets(device, sizeof(write_infos) / sizeof(write_infos[0]),
                               write_infos, 0, nullptr);
    }

    // NOTE: Plane mesh initialization
    {
        // (v0)------(v1)
        //  |  \       |
        //  |   `--,   |
        //  |       \  |
        // (v3)------(v2)
        std::vector<Vertex> vertices = {
            {{-5.0f, 0.0f, 5.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
            {{5.0f, 0.0f, 5.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
            {{5.0f, 0.0f, -5.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
            {{-5.0f, 0.0f, -5.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        };

        std::vector<uint32_t> indices = {
            0, 1, 2, 2, 3, 0};

        plane_mesh.vertex_buffer = new veekay::graphics::Buffer(
            vertices.size() * sizeof(Vertex), vertices.data(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

        plane_mesh.index_buffer = new veekay::graphics::Buffer(
            indices.size() * sizeof(uint32_t), indices.data(),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

        plane_mesh.indices = uint32_t(indices.size());
    }

    // NOTE: Cube mesh initialization
    {
        std::vector<Vertex> vertices = {
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
            {{+0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
            {{+0.5f, +0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
            {{-0.5f, +0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},

            {{+0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
            {{+0.5f, -0.5f, +0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
            {{+0.5f, +0.5f, +0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
            {{+0.5f, +0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},

            {{+0.5f, -0.5f, +0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{-0.5f, -0.5f, +0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            {{-0.5f, +0.5f, +0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
            {{+0.5f, +0.5f, +0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},

            {{-0.5f, -0.5f, +0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
            {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
            {{-0.5f, +0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
            {{-0.5f, +0.5f, +0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},

            {{-0.5f, -0.5f, +0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
            {{+0.5f, -0.5f, +0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
            {{+0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},

            {{-0.5f, +0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
            {{+0.5f, +0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
            {{+0.5f, +0.5f, +0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
            {{-0.5f, +0.5f, +0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        };

        std::vector<uint32_t> indices = {
            0,
            1,
            2,
            2,
            3,
            0,
            4,
            5,
            6,
            6,
            7,
            4,
            8,
            9,
            10,
            10,
            11,
            8,
            12,
            13,
            14,
            14,
            15,
            12,
            16,
            17,
            18,
            18,
            19,
            16,
            20,
            21,
            22,
            22,
            23,
            20,
        };

        cube_mesh.vertex_buffer = new veekay::graphics::Buffer(
            vertices.size() * sizeof(Vertex), vertices.data(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

        cube_mesh.index_buffer = new veekay::graphics::Buffer(
            indices.size() * sizeof(uint32_t), indices.data(),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

        cube_mesh.indices = uint32_t(indices.size());
    }

    // NOTE: Add models to scene
    models.emplace_back(Model{
        .mesh = plane_mesh,
        .transform = Transform{},
        .albedo_color = veekay::vec3{1.0f, 1.0f, 1.0f}});

    models.emplace_back(Model{
        .mesh = cube_mesh,
        .transform = Transform{
            .position = {-2.0f, -0.5f, -1.5f},
        },
        .albedo_color = veekay::vec3{1.0f, 0.0f, 0.0f}});

    models.emplace_back(Model{
        .mesh = cube_mesh,
        .transform = Transform{
            .position = {1.5f, -0.5f, -0.5f},
        },
        .albedo_color = veekay::vec3{0.0f, 1.0f, 0.0f}});

    models.emplace_back(Model{
        .mesh = cube_mesh,
        .transform = Transform{
            .position = {0.0f, -0.5f, 0.5f},
        },
        .albedo_color = veekay::vec3{0.0f, 0.0f, 1.0f}});

    Model model = Model{
        .mesh = cube_mesh,
        .transform = Transform{.position = {0.0f, -0.5f, 2.0f}},
        .albedo_color = veekay::vec3{1.0f, 1.0f, 1.0f},
        .albedo_tex = albedo_texture,
        .albedo_sampler = albedo_sampler,
    };

    if (g_UseSpecular) {
        model.specular_tex = specular_texture;
        model.specular_sampler = specular_sampler;
    }

    if (g_UseEmissive) {
        model.emissive_tex = emissive_texture;
        model.emissive_sampler = emissive_sampler;
    }

    models.push_back(model);

    shadow_map = createShadowMap(cmd, 2048, 2048);
    if (!shadow_map) {
        std::cerr << "Failed to create shadow map\n";
        veekay::app.running = false;
        return;
    }

    VkAttachmentDescription2 depthAttachment = {};
    depthAttachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference2 depthRef = {};
    depthRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // --- ДОБАВЛЕНА СИНХРОНИЗАЦИЯ ---
    VkSubpassDependency2 dependencies[2] = {};

    // 1. Перед началом прохода: ждем, пока предыдущие операции чтения закончатся
    dependencies[0].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // 2. После окончания прохода: ждем записи глубины перед тем, как читать в шейдере
    dependencies[1].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    // -------------------------------

    VkSubpassDescription2 subpass = {};
    subpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfo2 renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;  // Указываем кол-во зависимостей
    renderPassInfo.pDependencies = dependencies;

    vkCreateRenderPass2(veekay::app.vk_device, &renderPassInfo, nullptr, &shadow_render_pass);

    {
        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = shadow_render_pass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &shadow_map->view;
        fbInfo.width = 2048;
        fbInfo.height = 2048;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &shadow_framebuffer) != VK_SUCCESS) {
            std::cerr << "Failed to create shadow framebuffer!\n";
        }
    }

    {
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1}};

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        poolInfo.maxSets = 1;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &shadow_descriptor_pool) != VK_SUCCESS) {
            std::cerr << "Failed to create shadow descriptor pool\n";
            veekay::app.running = false;
            return;
        }
    }

    // Shadow pipeline shaders
    shadow_vertex_shader = loadShaderModule("./shaders/shadow.vert.spv");
    shadow_fragment_shader = loadShaderModule("./shaders/shadow.frag.spv");

    VkDescriptorSetLayoutBinding shadow_bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo shadow_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = sizeof(shadow_bindings) / sizeof(shadow_bindings[0]),
        .pBindings = shadow_bindings,
    };
    vkCreateDescriptorSetLayout(device, &shadow_layout_info, nullptr, &shadow_descriptor_set_layout);

    {
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = shadow_descriptor_pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &shadow_descriptor_set_layout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &shadow_descriptor_set) != VK_SUCCESS) {
            std::cerr << "Failed to allocate shadow descriptor set\n";
            veekay::app.running = false;
            return;
        }
    }

    VkDescriptorBufferInfo shadow_buffer_infos[] = {
        {.buffer = shadow_uniforms_buffer->buffer, .offset = 0, .range = sizeof(ShadowUniforms)},
        {.buffer = model_uniforms_buffer->buffer, .offset = 0, .range = sizeof(ModelUniforms)},
    };

    VkWriteDescriptorSet shadow_writes[] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = shadow_descriptor_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &shadow_buffer_infos[0],
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = shadow_descriptor_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo = &shadow_buffer_infos[1],
        },
    };
    vkUpdateDescriptorSets(device, 2, shadow_writes, 0, nullptr);

    VkPipelineLayoutCreateInfo shadow_pipe_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &shadow_descriptor_set_layout,
    };
    vkCreatePipelineLayout(device, &shadow_pipe_layout_info, nullptr, &shadow_pipeline_layout);

    VkPipelineShaderStageCreateInfo shadow_stages[2];
    shadow_stages[0] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = shadow_vertex_shader,
        .pName = "main",
    };
    shadow_stages[1] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = shadow_fragment_shader,
        .pName = "main",
    };

    // 1. Описание вершин (копируем логику из Vertex struct)
    VkVertexInputBindingDescription shadowBindingDescription = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};

    VkVertexInputAttributeDescription shadowAttributeDescription = {
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(Vertex, position)
        // Normal и UV не нужны для shadow map
    };

    VkPipelineVertexInputStateCreateInfo shadowVertexInputInfo = {};
    shadowVertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    shadowVertexInputInfo.vertexBindingDescriptionCount = 1;
    shadowVertexInputInfo.pVertexBindingDescriptions = &shadowBindingDescription;
    shadowVertexInputInfo.vertexAttributeDescriptionCount = 1;  // Только 1 атрибут!
    shadowVertexInputInfo.pVertexAttributeDescriptions = &shadowAttributeDescription;

    // 2. Сборка примитивов
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // 3. Растеризация (С настройками Bias для теней)
    VkPipelineRasterizationStateCreateInfo shadow_raster = {};
    shadow_raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    shadow_raster.polygonMode = VK_POLYGON_MODE_FILL;
    shadow_raster.cullMode = VK_CULL_MODE_NONE;
    shadow_raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    shadow_raster.lineWidth = 1.0f;
    shadow_raster.depthBiasEnable = VK_TRUE;  // Включаем Bias
    shadow_raster.depthBiasConstantFactor = 1.25f;
    shadow_raster.depthBiasSlopeFactor = 1.75f;
    shadow_raster.depthBiasClamp = 0.0f;

    // 4. Мультисэмплинг (отключен)
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;

    // 5. Тест глубины
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // 6. Смешивание цветов (отключено, так как пишем только в depth)
    VkPipelineColorBlendStateCreateInfo shadow_blend = {};
    shadow_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    shadow_blend.attachmentCount = 0;  // Нет цветовых вложений!
    shadow_blend.pAttachments = nullptr;

    // 7. Вьюпорт (2048x2048)
    VkViewport shadowViewport = {};
    shadowViewport.x = 0.0f;
    shadowViewport.y = 0.0f;
    shadowViewport.width = 2048.0f;
    shadowViewport.height = 2048.0f;
    shadowViewport.minDepth = 0.0f;
    shadowViewport.maxDepth = 1.0f;

    VkRect2D shadowScissor = {};
    shadowScissor.offset = {0, 0};
    shadowScissor.extent = {2048, 2048};

    VkPipelineViewportStateCreateInfo shadowViewportState = {};
    shadowViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    shadowViewportState.viewportCount = 1;
    shadowViewportState.pViewports = &shadowViewport;
    shadowViewportState.scissorCount = 1;
    shadowViewportState.pScissors = &shadowScissor;

    // 8. Финальная сборка пайплайна
    VkGraphicsPipelineCreateInfo shadow_pipe_info = {};
    shadow_pipe_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    shadow_pipe_info.stageCount = 2;
    shadow_pipe_info.pStages = shadow_stages;
    shadow_pipe_info.pVertexInputState = &shadowVertexInputInfo;
    shadow_pipe_info.pInputAssemblyState = &inputAssembly;
    shadow_pipe_info.pViewportState = &shadowViewportState;
    shadow_pipe_info.pRasterizationState = &shadow_raster;
    shadow_pipe_info.pMultisampleState = &multisampling;
    shadow_pipe_info.pDepthStencilState = &depthStencil;
    shadow_pipe_info.pColorBlendState = &shadow_blend;
    shadow_pipe_info.layout = shadow_pipeline_layout;
    shadow_pipe_info.renderPass = shadow_render_pass;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &shadow_pipe_info, nullptr, &shadow_pipeline) != VK_SUCCESS) {
        std::cerr << "Failed to create shadow pipeline!\n";
    }

    {
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.sampler = shadow_sampler;
        imageInfo.imageView = shadow_map->view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptor_set;
        write.dstBinding = 8;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(veekay::app.vk_device, 1, &write, 0, nullptr);
    }
}

// NOTE: Destroy resources here, do not cause leaks in your program!
void shutdown() {
    VkDevice& device = veekay::app.vk_device;

    vkDestroySampler(device, missing_texture_sampler, nullptr);
    delete missing_texture;

    delete cube_mesh.index_buffer;
    delete cube_mesh.vertex_buffer;

    delete plane_mesh.index_buffer;
    delete plane_mesh.vertex_buffer;

    delete model_uniforms_buffer;
    delete scene_uniforms_buffer;
    delete dirlight_uniforms_buffer;
    delete pointlight_uniforms_buffer;
    delete spotlight_uniforms_buffer;

    vkDestroySampler(device, albedo_sampler, nullptr);
    vkDestroySampler(device, specular_sampler, nullptr);
    vkDestroySampler(device, emissive_sampler, nullptr);
    delete albedo_texture;
    delete specular_texture;
    delete emissive_texture;

    vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);

    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    vkDestroyShaderModule(device, fragment_shader_module, nullptr);
    vkDestroyShaderModule(device, vertex_shader_module, nullptr);

    vkDestroySampler(device, shadow_sampler, nullptr);
    delete shadow_map;
    delete shadow_uniforms_buffer;
    vkDestroyPipeline(device, shadow_pipeline, nullptr);
    vkDestroyPipelineLayout(device, shadow_pipeline_layout, nullptr);
    vkDestroyShaderModule(device, shadow_vertex_shader, nullptr);
    vkDestroyShaderModule(device, shadow_fragment_shader, nullptr);
    vkDestroyDescriptorSetLayout(device, shadow_descriptor_set_layout, nullptr);
    vkDestroyDescriptorPool(device, shadow_descriptor_pool, nullptr);
    vkDestroyFramebuffer(device, shadow_framebuffer, nullptr);
    vkDestroyRenderPass(device, shadow_render_pass, nullptr);
}

auto getLastPosition(glm::vec3 currentPos) {
    static auto lastSave = camera.GetPosition();
    return std::exchange(lastSave, std::move(currentPos));
}

auto getLastOrientation(glm::vec2 currentOr) {
    static auto lastSave = camera.GetOrientation();
    return std::exchange(lastSave, std::move(currentOr));
}

auto getLastForward(glm::vec3 currentForward) {
    static auto lastSave = camera.GetDirection();
    return std::exchange(lastSave, std::move(currentForward));
}

void CameraUi() {
    ImGui::Begin("Camera:");

    ImGui::DragFloat3("Camera Position", &camera.GetPosition().x, 0.1f);

    if (ImGui::Checkbox("Use LookAt Matrix", &g_UseLookAt)) {
        camera.SyncBeforeSwitch(!g_UseLookAt);
    }

    ImGui::End();
}

void DirectionalLightUi() {
    ImGui::Begin("Directional Light:");

    ImGui::DragFloat3("Light Direction", &dirLight.dir.x, 0.1f);
    ImGui::ColorEdit3("Light Color", &dirLight.color.x, 0.1f);

    ImGui::Checkbox("Enable Directional Light", reinterpret_cast<bool*>(&dirLight.enable));

    ImGui::End();
}

void PointLightUi() {
    ImGui::Begin("Point Light:");

    ImGui::DragFloat3("PointLight Position", &pointLight.position.x, 0.1f);
    ImGui::ColorEdit3("PointLight Color", &pointLight.color.x, 0.1f);
    ImGui::DragFloat("PointLight Constant", &pointLight.constant, 0.1f);
    ImGui::DragFloat("PointLight Linear", &pointLight.linear, 0.1f);
    ImGui::DragFloat("PointLight Quadratic", &pointLight.quadratic, 0.1f);
    ImGui::Checkbox("Enable Point Light", reinterpret_cast<bool*>(&pointLight.enable));

    ImGui::End();
}

void SpotLightUi() {
    ImGui::Begin("Spot Light:");

    ImGui::DragFloat3("SpotLight Position", &spotLight.position.x, 0.1f);
    ImGui::DragFloat3("SpotLight Direction", &spotLight.direction.x, 0.1f);
    ImGui::ColorEdit3("SpotLight Color", &spotLight.color.x, 0.1f);
    ImGui::DragFloat("SpotLight Constant", &spotLight.constant, 0.1f);
    ImGui::DragFloat("SpotLight Linear", &spotLight.linear, 0.1f);
    ImGui::DragFloat("SpotLight Quadratic", &spotLight.quadratic, 0.1f);
    ImGui::DragFloat("SpotLight Cutoff(cosine of the angle)", &spotLight.cutoff, 0.01f, 0.0f, 1.0f);
    ImGui::Checkbox("Enable Spot Light", reinterpret_cast<bool*>(&spotLight.enable));

    ImGui::End();
}

void ShadowUi() {
    ImGui::Begin("Shadows:");
    ImGui::Checkbox("Enable Shadows", &g_UseShadows);
    ImGui::DragFloat("Shadow Bias", &g_ShadowBias, 0.0001f, 0.0f, 0.1f);
    ImGui::DragFloat("PCF Size", &g_ShadowPCF, 0.1f, 1.0f, 5.0f);
    ImGui::End();
}

void update(double time) {
    camera.OnResize(veekay::app.window_width, veekay::app.window_height);
    camera.OnUpdate(g_UseLookAt, g_FpsMode, time);

    if (!g_FpsMode) {
        CameraUi();
        DirectionalLightUi();
        PointLightUi();
        SpotLightUi();
        ShadowUi();
    }

    SceneUniforms scene_uniforms{
        .view_projection = camera.GetProjection() * camera.GetView(),
        .viewPos = camera.GetPosition(),
        .time = static_cast<float>(time)};

    std::vector<ModelUniforms> model_uniforms(models.size());
    for (size_t i = 0, n = models.size(); i < n; ++i) {
        const Model& model = models[i];
        ModelUniforms& uniforms = model_uniforms[i];

        uniforms.model = model.transform.matrix();
        uniforms.albedo_color = model.albedo_color;
        uniforms.use_albedo_tex = (model.albedo_tex != nullptr) ? 1 : 0;
        uniforms.use_specular_tex = (model.specular_tex != nullptr && g_UseSpecular) ? 1 : 0;
        uniforms.use_emissive_tex = (model.emissive_tex != nullptr && g_UseEmissive) ? 1 : 0;
        uniforms.use_sine_distortion = g_UseSineDistortion ? 1 : 0;
    }

    *(SceneUniforms*)scene_uniforms_buffer->mapped_region = scene_uniforms;
    std::copy(model_uniforms.begin(),
              model_uniforms.end(),
              static_cast<ModelUniforms*>(model_uniforms_buffer->mapped_region));

    *(DirectionalLight*)dirlight_uniforms_buffer->mapped_region = dirLight;

    *(PointLight*)pointlight_uniforms_buffer->mapped_region = pointLight;

    *(SpotLight*)spotlight_uniforms_buffer->mapped_region = spotLight;

    ShadowUniforms shadowUniforms;

    // Инициализируем матрицу единичной (на случай если тени выключены)
    shadowUniforms.light_view_projection = glm::mat4(1.0f);

    // По умолчанию флаг выключен (0.0)
    float shadowEnabled = 0.0f;

    if (g_UseShadows) {
        float orthoSize = 10.0f;
        glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, 50.0f);
        lightProjection[1][1] *= -1;

        glm::vec3 dir(dirLight.dir.x, dirLight.dir.y, dirLight.dir.z);
        glm::vec3 lightDir = glm::normalize(dir);
        glm::vec3 lightPos = -lightDir * 15.0f;

        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if (glm::abs(lightDir.y) > 0.99f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        glm::mat4 lightView = glm::lookAt(lightPos, lightPos + lightDir, up);

        shadowUniforms.light_view_projection = lightProjection * lightView;
        shadowEnabled = 1.0f;  // Включаем флаг
    }

    // Передаем параметры: Bias, PCF, ENABLE_FLAG (в params.z)
    shadowUniforms.params = glm::vec4(g_ShadowBias, g_ShadowPCF, shadowEnabled, 0.0f);

    // ОБНОВЛЯЕМ БУФЕР ВСЕГДА, даже если тени выключены
    *(ShadowUniforms*)shadow_uniforms_buffer->mapped_region = shadowUniforms;
}

void renderShadowMap(VkCommandBuffer cmd) {
    if (!g_UseShadows) return;

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = shadow_render_pass;
    renderPassInfo.framebuffer = shadow_framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {2048, 2048};

    VkClearValue clearValue = {};
    clearValue.depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline);

    for (size_t i = 0, n = models.size(); i < n; ++i) {
        const Model& model = models[i];
        const Mesh& mesh = model.mesh;

        VkBuffer vertexBuffers[] = {mesh.vertex_buffer->buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

        vkCmdBindIndexBuffer(cmd, mesh.index_buffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        uint32_t offset = i * sizeof(ModelUniforms);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_layout,
                                0, 1, &shadow_descriptor_set, 1, &offset);

        vkCmdDrawIndexed(cmd, mesh.indices, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void render(VkCommandBuffer cmd, VkFramebuffer framebuffer) {
    // --- ИСПРАВЛЕНИЕ: Сброс и начало записи буфера команд ДО вызова renderShadowMap ---
    vkResetCommandBuffer(cmd, 0);

    {  // NOTE: Start recording rendering commands
        VkCommandBufferBeginInfo info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };

        vkBeginCommandBuffer(cmd, &info);
    }

    // Теперь renderShadowMap запишет свои команды в уже открытый буфер
    if (g_UseShadows)
        renderShadowMap(cmd);

    {  // NOTE: Use current swapchain framebuffer and clear it
        VkClearValue clear_color{.color = {{0.1f, 0.1f, 0.1f, 1.0f}}};
        VkClearValue clear_depth{.depthStencil = {1.0f, 0}};

        VkClearValue clear_values[] = {clear_color, clear_depth};

        VkRenderPassBeginInfo info{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = veekay::app.vk_render_pass,
            .framebuffer = framebuffer,
            .renderArea = {
                .extent = {
                    veekay::app.window_width,
                    veekay::app.window_height},
            },
            .clearValueCount = 2,
            .pClearValues = clear_values,
        };

        vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDeviceSize zero_offset = 0;

    VkBuffer current_vertex_buffer = VK_NULL_HANDLE;
    VkBuffer current_index_buffer = VK_NULL_HANDLE;

    for (size_t i = 0, n = models.size(); i < n; ++i) {
        const Model& model = models[i];
        const Mesh& mesh = model.mesh;

        if (current_vertex_buffer != mesh.vertex_buffer->buffer) {
            current_vertex_buffer = mesh.vertex_buffer->buffer;
            vkCmdBindVertexBuffers(cmd, 0, 1, &current_vertex_buffer, &zero_offset);
        }

        if (current_index_buffer != mesh.index_buffer->buffer) {
            current_index_buffer = mesh.index_buffer->buffer;
            vkCmdBindIndexBuffer(cmd, current_index_buffer, zero_offset, VK_INDEX_TYPE_UINT32);
        }

        uint32_t offset = i * sizeof(ModelUniforms);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
                                0, 1, &descriptor_set, 1, &offset);

        vkCmdDrawIndexed(cmd, mesh.indices, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

}  // namespace

int main() {
    return veekay::run({
        .init = initialize,
        .shutdown = shutdown,
        .update = update,
        .render = render,
    });
}