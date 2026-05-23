#pragma once
#include "Transform.h"

struct Camera {
    float fov = 90.0f;
    float aspectRatio = 16.0f / 9.0f; // TODO 
    float nearPlane = 0.1f;
    float farPlane = 100000.0f;
    glm::vec4 clearColor {0.1f, 0.1f, 0.5f, 1.0f};

    // looking along -Z initially
    glm::vec3 direction { 0.0f, 0.0f, -1.0f };
    float yaw = -90.0f;

    // horizontal view
    glm::vec3 up { 0.0f, 1.0f, 0.0f };
    float pitch = 0.0f;

    // Compute view matrix from Transform
    // glm::mat4 GetViewMatrix(const Transform& transform) const {
    //     return glm::lookAt(transform.position, transform.position + direction, up);
    // }

    glm::mat4 GetProjectionMatrix() const {
        auto projectionMatrix = glm::perspective(glm::radians(fov), aspectRatio, farPlane, nearPlane);
        projectionMatrix[1][1] *= -1; // Vulkan Y coordinate correction
        return projectionMatrix;
    }
    
    glm::mat4 GetViewMatrix(const Transform& transform) const {
        glm::vec3 forward = glm::normalize(transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
        glm::vec3 up      = glm::normalize(transform.rotation * glm::vec3(0.0f, 1.0f,  0.0f));

        return glm::lookAt(transform.position, transform.position + forward, up);
    }
    

    // controls
    float speed = 20.0f;
    bool screenshotRequested = false;
};