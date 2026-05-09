#include "CameraSystem.h"
#include "InputSystem.h"
#include "Camera.h"

// void CameraSystem::Update(float deltaTime) {
//     auto view = _registry.view<InputState>();
//     if (!view.empty()) {
// }

CameraSystem::CameraSystem(entt::registry& registry) : System(registry) {

}


void CameraSystem::Update(float deltaTime)
{
    auto inputView = _registry.view<InputState>();
    if (inputView.empty())
        return;

    auto inputEntity = *inputView.begin();
    auto& input = inputView.get<InputState>(inputEntity);

    auto cameraView = _registry.view<Camera, Transform>();

    for (auto entity : cameraView) {
        auto& camera = cameraView.get<Camera>(entity);
        auto& transform = cameraView.get<Transform>(entity);

        float speed = camera.speed;

        if (input.mouseButtons[GLFW_MOUSE_BUTTON_RIGHT].held) {
            float sensitivity = 0.5f;

            camera.yaw   += input.deltaX * sensitivity;
            camera.pitch -= input.deltaY * sensitivity;
            camera.pitch = glm::clamp(camera.pitch, -89.0f, 89.0f);

            glm::vec3 dir;
            dir.x = cos(glm::radians(camera.yaw)) * cos(glm::radians(camera.pitch));
            dir.y = sin(glm::radians(camera.pitch));
            dir.z = sin(glm::radians(camera.yaw)) * cos(glm::radians(camera.pitch));

            camera.direction = glm::normalize(dir);

            // Make transform rotation match the camera look
            transform.rotation = glm::quatLookAtRH(camera.direction, camera.up);
        }

        glm::vec3 forward = glm::normalize(camera.direction);
        glm::vec3 right   = glm::normalize(glm::cross(forward, camera.up));

        if (input.keys[GLFW_KEY_W].held)
            transform.position += forward * speed * deltaTime;

        if (input.keys[GLFW_KEY_S].held)
            transform.position -= forward * speed * deltaTime;

        if (input.keys[GLFW_KEY_A].held)
            transform.position -= right * speed * deltaTime;

        if (input.keys[GLFW_KEY_D].held)
            transform.position += right * speed * deltaTime;

        if (input.keys[GLFW_KEY_SPACE].held)
            transform.position += camera.up * speed * deltaTime;
        if (input.keys[GLFW_KEY_LEFT_SHIFT].held)
            transform.position -= camera.up * speed * deltaTime;
    }
}

// void CameraSystem::Update(float deltaTime)
// {
//     auto inputView = _registry.view<InputState>();
//     if (inputView.empty())
//         return;

//     auto inputEntity = *inputView.begin();
//     auto& input = inputView.get<InputState>(inputEntity);

//     auto cameraView = _registry.view<Camera, Transform>();

//     for (auto entity : cameraView) {
//         auto& camera = cameraView.get<Camera>(entity);
//         auto& transform = cameraView.get<Transform>(entity);

//         float speed = camera.speed;
//         float sensitivity = 0.5f;
//         if (input.mouseButtons[GLFW_MOUSE_BUTTON_RIGHT].held) {
//             float sensitivity = 0.5f;

//             camera.yaw   += input.deltaX * sensitivity;
//             camera.pitch -= input.deltaY * sensitivity; // invert Y
//             camera.pitch = glm::clamp(camera.pitch, -89.0f, 89.0f);

//             glm::vec3 dir;
//             dir.x = cos(glm::radians(camera.yaw)) * cos(glm::radians(camera.pitch));
//             dir.y = sin(glm::radians(camera.pitch));
//             dir.z = sin(glm::radians(camera.yaw)) * cos(glm::radians(camera.pitch));

//             camera.direction = glm::normalize(dir);
//         }

//         // glm::vec3 dir;
//         // dir.x = cos(glm::radians(camera.yaw)) * cos(glm::radians(camera.pitch));
//         // dir.y = sin(glm::radians(camera.pitch));
//         // dir.z = sin(glm::radians(camera.yaw)) * cos(glm::radians(camera.pitch));
//         // camera.direction = glm::normalize(dir);

//         glm::vec3 forward = glm::normalize(camera.direction);
//         glm::vec3 right = glm::normalize(glm::cross(forward, camera.up));

//         if (input.keys[GLFW_KEY_W].held)
//             transform.position += forward * speed * deltaTime;

//         if (input.keys[GLFW_KEY_S].held)
//             transform.position -= forward * speed * deltaTime;

//         if (input.keys[GLFW_KEY_A].held)
//             transform.position -= right * speed * deltaTime;

//         if (input.keys[GLFW_KEY_D].held)
//             transform.position += right * speed * deltaTime;

//         // Vertical movement
//         if (input.keys[GLFW_KEY_SPACE].held)
//             transform.position += camera.up * speed * deltaTime;
//         if (input.keys[GLFW_KEY_LEFT_SHIFT].held)
//             transform.position -= camera.up * speed * deltaTime;
//     }
// }
