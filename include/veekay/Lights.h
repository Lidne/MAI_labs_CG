#include "types.hpp"

struct alignas(16) DirectionalLight {
    veekay::vec3 dir{1.0f, 1.0f, -1.0f};   float _pad0;
    veekay::vec3 color{1.0f, 1.0f, 1.0f};  float _pad1;
    int enable = 1;          int _pad2[3];
};

struct alignas(16) PointLight {
    veekay::vec3 position{0.0f, -3.0f, 0.0f};   float _pad0;
    veekay::vec3 color{1.0f, 1.0f, 1.0f};  float _pad1;
    float constant = 1.0;
    float linear = 0.35;
    float quadratic = 0.44;
    float _pad4;
    int enable;
};

struct alignas(16) SpotLight {
    veekay::vec3 position{0.0f, -3.0f, 0.0f};   float _pad0;
    veekay::vec3 color{1.0f, 1.0f, 1.0f};  float _pad1;
    veekay::vec3 direction{0.0f, 1.0f, 0.0f};  float _pad2;
    float constant = 1.0;
    float linear = 0.35;
    float quadratic = 0.44;
    float cutoff = 0.4f;
    int enable;
};
