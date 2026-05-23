#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

struct SunlightComponent {
    glm::vec3 direction = glm::vec3(-0.3f, 1.0f, -0.6f);
    float intensity = 3.0f;
    glm::vec3 color = glm::vec3(1.0f);
    float ambient = 0.05f;
};