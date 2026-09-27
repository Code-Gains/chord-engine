#include "CameraSystem.h"
#include "InputSystem.h"
#include "Camera.h"
#include "Core.h"
#include "EditorSelection.h"
#include "EditorInteractionState.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <imgui.h>

// void CameraSystem::Update(float deltaTime) {
//     auto view = _registry.view<InputState>();
//     if (!view.empty()) {
// }

namespace {
    struct EditorCameraOrbitState {
        bool hasPivot = false;
        glm::vec3 pivot{ 0.0f };
        float pivotRadius = 1.0f;
    };

    std::unordered_map<entt::id_type, EditorCameraOrbitState> gOrbitStates;

    glm::vec3 DirectionFromYawPitch(float yaw, float pitch)
    {
        glm::vec3 direction;
        direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        direction.y = sin(glm::radians(pitch));
        direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        return glm::normalize(direction);
    }

    float MaxScaleAxis(const Transform& transform)
    {
        return std::max({
            std::abs(transform.scale.x),
            std::abs(transform.scale.y),
            std::abs(transform.scale.z),
            1.0f
        });
    }

    void SyncCameraRotation(Camera& camera, Transform& transform)
    {
        camera.direction = DirectionFromYawPitch(camera.yaw, camera.pitch);
        transform.rotation = glm::quatLookAtRH(camera.direction, camera.up);
    }

    void SyncYawPitchFromDirection(Camera& camera, const glm::vec3& direction)
    {
        const glm::vec3 normalizedDirection = glm::normalize(direction);
        camera.pitch = glm::degrees(asin(glm::clamp(normalizedDirection.y, -1.0f, 1.0f)));
        camera.yaw = glm::degrees(atan2(normalizedDirection.z, normalizedDirection.x));
        camera.direction = normalizedDirection;
    }

    void SyncYawPitchFromTransform(Camera& camera, const Transform& transform)
    {
        SyncYawPitchFromDirection(camera, transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
    }

    bool TryResolveSelectedTransform(
        entt::registry& registry,
        entt::entity cameraEntity,
        const Transform*& selectedTransform
    )
    {
        if (!registry.ctx().contains<EditorSelection>()) {
            return false;
        }

        const auto& selection = registry.ctx().get<EditorSelection>();
        if (selection.selectedEntity == entt::null ||
            selection.selectedEntity == cameraEntity ||
            !registry.valid(selection.selectedEntity))
        {
            return false;
        }

        auto* resolvedTransform = registry.try_get<Transform>(selection.selectedEntity);
        if (!resolvedTransform) {
            return false;
        }

        selectedTransform = resolvedTransform;
        return true;
    }

    void UpdateEditorCamera(
        entt::registry& registry,
        entt::entity entity,
        Camera& camera,
        Transform& transform,
        InputState& input,
        float deltaTime,
        bool mouseControlsAllowed,
        bool keyboardShortcutsAllowed)
    {
        auto& orbitState = gOrbitStates[entt::to_integral(entity)];

        if (mouseControlsAllowed && input.mouseButtons[GLFW_MOUSE_BUTTON_RIGHT].held) {
            if (input.mouseButtons[GLFW_MOUSE_BUTTON_RIGHT].pressed) {
                SyncYawPitchFromTransform(camera, transform);
            }

            const bool mouseMoved =
                std::abs(input.deltaX) > 0.0 ||
                std::abs(input.deltaY) > 0.0;

            if (mouseMoved) {
                camera.yaw   += static_cast<float>(input.deltaX) * camera.orbitSensitivity;
                camera.pitch -= static_cast<float>(input.deltaY) * camera.orbitSensitivity;
                camera.pitch = glm::clamp(camera.pitch, -89.0f, 89.0f);

                camera.direction = DirectionFromYawPitch(camera.yaw, camera.pitch);

                if (orbitState.hasPivot) {
                    const float orbitDistance =
                        std::max(glm::length(transform.position - orbitState.pivot), camera.nearPlane * 2.0f);
                    transform.position = orbitState.pivot - camera.direction * orbitDistance;
                }

                transform.rotation = glm::quatLookAtRH(camera.direction, camera.up);
            }
        }

        glm::vec3 forward = glm::normalize(transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
        glm::vec3 right = glm::normalize(transform.rotation * glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 viewUp = glm::normalize(transform.rotation * glm::vec3(0.0f, 1.0f, 0.0f));

        const float pivotDistance = orbitState.hasPivot
            ? std::max(glm::length(transform.position - orbitState.pivot), 1.0f)
            : 10.0f;

        if (mouseControlsAllowed && input.mouseButtons[GLFW_MOUSE_BUTTON_MIDDLE].held) {
            const float panSpeed = pivotDistance * camera.panSensitivity;
            const glm::vec3 delta =
                (-right * static_cast<float>(input.deltaX) +
                  viewUp * static_cast<float>(input.deltaY)) * panSpeed;
            transform.position += delta;
            if (orbitState.hasPivot) {
                orbitState.pivot += delta;
            }
        }

        if (mouseControlsAllowed && std::abs(input.scrollY) > 0.0) {
            if (orbitState.hasPivot) {
                const float zoomStep = std::max(pivotDistance * camera.zoomSensitivity, 1.0f);
                const float minDistance = camera.nearPlane * 2.0f;
                const float nextDistance = glm::clamp(
                    pivotDistance - static_cast<float>(input.scrollY) * zoomStep,
                    minDistance,
                    camera.farPlane * 0.5f
                );

                transform.position = orbitState.pivot - forward * nextDistance;
            }
            else {
                const float zoomStep = std::max(camera.speed * camera.zoomSensitivity, 1.0f);
                transform.position += forward * static_cast<float>(input.scrollY) * zoomStep;
            }
        }

        const Transform* selectedTransform = nullptr;
        if (keyboardShortcutsAllowed &&
            input.keys[GLFW_KEY_F].pressed &&
            TryResolveSelectedTransform(registry, entity, selectedTransform))
        {
            orbitState.hasPivot = true;
            orbitState.pivot = selectedTransform->position;
            orbitState.pivotRadius = MaxScaleAxis(*selectedTransform);

            const float focusDistance =
                std::max(orbitState.pivotRadius * camera.focusDistanceScale, 5.0f);
            transform.position = orbitState.pivot - forward * focusDistance;
        }

        if (mouseControlsAllowed &&
            keyboardShortcutsAllowed &&
            input.mouseButtons[GLFW_MOUSE_BUTTON_RIGHT].held)
        {
            glm::vec3 movement{ 0.0f };
            if (input.keys[GLFW_KEY_W].held)
                movement += forward * camera.speed * deltaTime;

            if (input.keys[GLFW_KEY_S].held)
                movement -= forward * camera.speed * deltaTime;

            if (input.keys[GLFW_KEY_A].held)
                movement -= right * camera.speed * deltaTime;

            if (input.keys[GLFW_KEY_D].held)
                movement += right * camera.speed * deltaTime;

            if (input.keys[GLFW_KEY_SPACE].held)
                movement += camera.up * camera.speed * deltaTime;

            if (input.keys[GLFW_KEY_LEFT_SHIFT].held)
                movement -= camera.up * camera.speed * deltaTime;

            transform.position += movement;
            if (orbitState.hasPivot) {
                orbitState.pivot += movement;
            }
        }
    }
}

CameraSystem::CameraSystem(entt::registry& registry, Engine::Core* core) : System(registry), _core(core) {

}


void CameraSystem::Update(float deltaTime)
{
    if (_core && _core->IsPlayMode()) {
        return;
    }

    auto inputView = _registry.view<InputState>();
    if (inputView.empty())
        return;

    auto inputEntity = *inputView.begin();
    auto& input = inputView.get<InputState>(inputEntity);
    const ImGuiIO& io = ImGui::GetIO();
    const auto* interaction = _registry.ctx().find<EditorInteractionState>();
    const bool gizmoOwnsMouse = interaction &&
        (interaction->gizmoHovered || interaction->gizmoUsing);
    const bool mouseControlsAllowed = !io.WantCaptureMouse && !gizmoOwnsMouse;
    const bool keyboardShortcutsAllowed = !io.WantCaptureKeyboard;
    if (auto* mutableInteraction = _registry.ctx().find<EditorInteractionState>()) {
        mutableInteraction->cameraNavigating =
            mouseControlsAllowed &&
            (input.mouseButtons[GLFW_MOUSE_BUTTON_RIGHT].held ||
             input.mouseButtons[GLFW_MOUSE_BUTTON_MIDDLE].held);
    }

    auto pilotCameraView =
        _registry.view<Camera, Transform, EditorCameraPilotTag>(entt::exclude<Engine::CoreOwnedTag>);

    if (pilotCameraView.begin() != pilotCameraView.end()) {
        for (auto entity : pilotCameraView) {
            auto& camera = pilotCameraView.get<Camera>(entity);
            auto& transform = pilotCameraView.get<Transform>(entity);
            UpdateEditorCamera(
                _registry,
                entity,
                camera,
                transform,
                input,
                deltaTime,
                mouseControlsAllowed,
                keyboardShortcutsAllowed);
        }

        return;
    }

    auto editorCameraView = _registry.view<Camera, Transform, ActiveCameraTag, Engine::CoreOwnedTag>();
    if (editorCameraView.begin() != editorCameraView.end()) {
        for (auto entity : editorCameraView) {
            auto& camera = editorCameraView.get<Camera>(entity);
            auto& transform = editorCameraView.get<Transform>(entity);
            UpdateEditorCamera(
                _registry,
                entity,
                camera,
                transform,
                input,
                deltaTime,
                mouseControlsAllowed,
                keyboardShortcutsAllowed);
        }

        return;
    }

    auto sceneCameraView =
        _registry.view<Camera, Transform, ActiveCameraTag>(entt::exclude<Engine::CoreOwnedTag>);

    for (auto entity : sceneCameraView) {
        auto& camera = sceneCameraView.get<Camera>(entity);
        auto& transform = sceneCameraView.get<Transform>(entity);
        UpdateEditorCamera(
            _registry,
            entity,
            camera,
            transform,
            input,
            deltaTime,
            mouseControlsAllowed,
            keyboardShortcutsAllowed);
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
