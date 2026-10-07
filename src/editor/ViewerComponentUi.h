#pragma once

#include <entt/entt.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <string>

#include "Transform.h"
#include "Core.h"
#include "EnvironmentComponent.h"
#include "NameComponent.h"
#include "ScreenPostProcessComponent.h"
#include "SunlightComponent.h"
#include "Camera.h"
#include "MeshComponent.h"
#include "GravityComponents.h"
#include "HierarchyComponent.h"
#include "HierarchySystem.h"
#include "ImGuiWindowRegistry.h"
#include "JoltPhysicsComponents.h"
#include "CameraShotEditorUi.h"
#include "EditorUiLayout.h"
#include "EditorAssetPayloads.h"

namespace EditorUi {

inline bool DrawSoundCueField(const char* label, std::string& cuePath)
{
    char pathBuffer[512] {};
    std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", cuePath.c_str());

    bool changed = false;
    if (ImGui::InputText(label, pathBuffer, sizeof(pathBuffer))) {
        cuePath = pathBuffer;
        changed = true;
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENGINE_SOUND_CUE_ASSET")) {
            cuePath.assign(
                static_cast<const char*>(payload->Data),
                static_cast<std::size_t>(payload->DataSize) > 0
                    ? static_cast<std::size_t>(payload->DataSize) - 1
                    : 0);
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }

    return changed;
}

inline bool DrawAudioClipField(const char* label, std::string& clipPath)
{
    char pathBuffer[512] {};
    std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", clipPath.c_str());

    bool changed = false;
    if (ImGui::InputText(label, pathBuffer, sizeof(pathBuffer))) {
        clipPath = pathBuffer;
        changed = true;
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENGINE_AUDIO_CLIP_ASSET")) {
            clipPath.assign(
                static_cast<const char*>(payload->Data),
                static_cast<std::size_t>(payload->DataSize) > 0
                    ? static_cast<std::size_t>(payload->DataSize) - 1
                    : 0);
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }

    return changed;
}

}

class ViewerComponentUi
{
public:
    virtual ~ViewerComponentUi() = default;
    virtual void Draw(entt::registry& registry, entt::entity entity) = 0;

protected:
    bool DrawComponentHeader(const char* label, const char* id)
    {
        const std::string headerId = std::string(label) + "##" + id;
        return ImGui::CollapsingHeader(headerId.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    }

    template<typename Component>
    bool DrawRemovableComponentHeader(
        entt::registry& registry,
        entt::entity entity,
        const char* label,
        const char* id)
    {
        const std::string headerId = std::string(label) + "##" + id;
        bool componentVisible = true;
        const bool open = ImGui::CollapsingHeader(
            headerId.c_str(),
            &componentVisible,
            ImGuiTreeNodeFlags_DefaultOpen
        );

        if (!componentVisible) {
            registry.remove<Component>(entity);
            return false;
        }

        return open;
    }
};

class NameComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* nameComponent = registry.try_get<NameComponent>(entity);
        if (!nameComponent)
            return;

        if (DrawRemovableComponentHeader<NameComponent>(registry, entity, "Name", "NameComponent"))
        {
            char buffer[256] {};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "%s",
                nameComponent->name.c_str()
            );

            EditorUi::ScopedItemWidth width{ 240.0f };
            if (ImGui::InputText("Name##NameComponentValue", buffer, sizeof(buffer))) {
                nameComponent->name = buffer;
            }
        }
    }
};

class TransformComponentUi : public ViewerComponentUi {
public:
    glm::vec3 rotationEulerDegrees{0.0f};
    entt::entity rotationEntity{entt::null};
    bool rotationEditing = false;

    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* transform = registry.try_get<Transform>(entity);
        if (!transform)
            return;

        if (DrawComponentHeader("Transform", "TransformComponent"))
        {
            ImGui::TextUnformatted("Required");
            if (registry.all_of<HierarchyComponent>(entity)) {
                ImGui::TextDisabled("World transform. Edit local transform in Node.");
            }

            EditorUi::ScopedItemWidth width{ 240.0f };
            ImGui::DragFloat3("Position", &transform->position.x, 0.1f);

            if (rotationEntity != entity || !rotationEditing) {
                rotationEulerDegrees = glm::degrees(glm::eulerAngles(transform->rotation));
                rotationEntity = entity;
            }

            if (ImGui::DragFloat3("Rotation", &rotationEulerDegrees.x, 0.1f))
            {
                glm::vec3 radians = glm::radians(rotationEulerDegrees);

                transform->rotation =
                    glm::normalize(glm::quat(radians));
            }
            rotationEditing = ImGui::IsItemActive();

            ImGui::DragFloat3("Scale", &transform->scale.x, 0.1f);
        }
    }
};

class SunlightComponentUI : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* sunlight = registry.try_get<SunlightComponent>(entity);
        if (!sunlight)
            return;

        if (DrawRemovableComponentHeader<SunlightComponent>(registry, entity, "Sunlight", "SunlightComponent"))
        {
            EditorUi::ScopedItemWidth width{ 300.0f };
            ImGui::DragFloat3("Direction", &sunlight->direction.x, 0.01f, -1.0f, 1.0f);

            if (glm::length(sunlight->direction) > 0.001f) {
                sunlight->direction = glm::normalize(sunlight->direction);
            }

            ImGui::DragFloat("Intensity", &sunlight->intensity, 0.05f, 0.0f, 20.0f);

            ImGui::ColorEdit3("Color", &sunlight->color.x);

            ImGui::DragFloat("Ambient", &sunlight->ambient, 0.01f, 0.0f, 1.0f);
        }
    }
};

class HierarchyComponentUi : public ViewerComponentUi {
public:
    glm::vec3 localRotationEulerDegrees{0.0f};
    entt::entity editedEntity{ entt::null };

    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
        if (!hierarchy)
            return;

        if (DrawRemovableComponentHeader<HierarchyComponent>(registry, entity, "Hierarchy", "HierarchyComponent"))
        {
            const int parentId = hierarchy->parent == entt::null
                ? -1
                : static_cast<int>(entt::to_integral(hierarchy->parent));

            ImGui::Text("Parent: %d", parentId);
            ImGui::TextDisabled(
                hierarchy->inheritTransform
                    ? "Mode: Attached"
                    : "Mode: Organizational");

            if (editedEntity != entity) {
                editedEntity = entity;
            }

            if (hierarchy->parent != entt::null && registry.valid(hierarchy->parent)) {
                if (hierarchy->inheritTransform) {
                    if (ImGui::Button("Make Organizational##HierarchyParent")) {
                        Engine::SetHierarchyParentOrganizational(registry, entity, hierarchy->parent);
                    }
                }
                else {
                    if (ImGui::Button("Make Attached##HierarchyParent")) {
                        Engine::SetHierarchyParent(registry, entity, hierarchy->parent, true);
                    }
                }
            }

            ImGui::SeparatorText("Local Transform");
            ImGui::TextDisabled("Edited relative to the parent when attached.");

            EditorUi::ScopedItemWidth width{ 320.0f };
            ImGui::DragFloat3("Local Position", &hierarchy->localTransform.position.x, 0.1f);

            if (ImGui::DragFloat3("Local Rotation", &localRotationEulerDegrees.x, 0.1f))
            {
                glm::vec3 radians = glm::radians(localRotationEulerDegrees);
                hierarchy->localTransform.rotation =
                    glm::normalize(glm::quat(radians));
            }

            ImGui::DragFloat3("Local Scale", &hierarchy->localTransform.scale.x, 0.1f);
        }
    }
};

class ScreenPostProcessComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* effect = registry.try_get<ScreenPostProcessComponent>(entity);
        if (!effect)
            return;

        if (DrawRemovableComponentHeader<ScreenPostProcessComponent>(registry, entity, "Screen Post Process", "ScreenPostProcessComponent"))
        {
            EditorUi::ScopedItemWidth width{ 300.0f };
            ImGui::Checkbox("Enabled##ScreenPostProcessEnabled", &effect->enabled);
            ImGui::Checkbox("Debug Overlay##ScreenPostProcessDebugOverlay", &effect->debugOverlay);
            ImGui::Checkbox("Use Source Mask##ScreenPostProcessUseSourceMask", &effect->useSourceMask);
            ImGui::ColorEdit3("Color##ScreenPostProcessColor", &effect->color.x);
            ImGui::DragFloat("Scale##ScreenPostProcessScale", &effect->scale, 0.01f, 0.001f, 100000.0f);
            ImGui::DragFloat("Softness##ScreenPostProcessSoftness", &effect->softness, 0.001f, 0.001f, 1.0f);
            ImGui::DragFloat("Intensity##ScreenPostProcessIntensity", &effect->intensity, 0.01f, 0.0f, 100.0f);
            ImGui::DragFloat("Amount##ScreenPostProcessAmount", &effect->amount, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Displacement##ScreenPostProcessDisplacement", &effect->displacement, 0.001f, 0.0f, 1.0f);
            ImGui::DragFloat("Chromatic Aberration##ScreenPostProcessChromaticAberration", &effect->chromaticAberration, 0.001f, 0.0f, 1.0f);
            ImGui::DragFloat("Block Size##ScreenPostProcessBlockSize", &effect->blockSize, 0.001f, 0.0001f, 1.0f);
            ImGui::DragFloat("Mask Feather##ScreenPostProcessMaskFeather", &effect->maskFeather, 0.001f, 0.0f, 1.0f);
            ImGui::DragFloat("Speed##ScreenPostProcessSpeed", &effect->speed, 0.01f, 0.0f, 100.0f);
            ImGui::DragFloat("Age##ScreenPostProcessAge", &effect->age, 0.01f, 0.0f, 100000.0f);
        }
    }
};

class EnvironmentComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* environment = registry.try_get<EnvironmentComponent>(entity);
        if (!environment)
            return;

        if (DrawRemovableComponentHeader<EnvironmentComponent>(
            registry,
            entity,
            "Environment",
            "EnvironmentComponent"))
        {
            ImGui::TextUnformatted("Skybox");
            if (environment->skyboxPath.empty()) {
                ImGui::TextDisabled("No skybox assigned.");
            }
            else {
                ImGui::TextWrapped("%s", environment->skyboxPath.c_str());
            }

            ImGui::Button("Drop Skybox Folder Here", ImVec2(-1.0f, 0.0f));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENGINE_SKYBOX_ASSET")) {
                    environment->skyboxPath.assign(
                        static_cast<const char*>(payload->Data),
                        static_cast<std::size_t>(payload->DataSize) > 0
                            ? static_cast<std::size_t>(payload->DataSize) - 1
                            : 0);
                }
                ImGui::EndDragDropTarget();
            }
        }
    }
};

class ScreenPostProcessSourceComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* source = registry.try_get<ScreenPostProcessSourceComponent>(entity);
        if (!source)
            return;

        if (DrawRemovableComponentHeader<ScreenPostProcessSourceComponent>(
            registry,
            entity,
            "Screen Post Process Source",
            "ScreenPostProcessSourceComponent"))
        {
            EditorUi::ScopedItemWidth width{ 300.0f };
            ImGui::Checkbox("Enabled##ScreenPostProcessSourceEnabled", &source->enabled);
            ImGui::DragFloat(
                "World Radius Extension##ScreenPostProcessSourceWorldRadiusExtension",
                &source->worldRadiusExtension,
                0.01f,
                0.0f,
                100000.0f);
        }
    }
};

class CameraComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* camera = registry.try_get<Camera>(entity);
        if (!camera)
            return;

        if (DrawRemovableComponentHeader<Camera>(registry, entity, "Camera", "CameraComponent"))
        {
            const bool isEditorOwned = registry.all_of<Engine::CoreOwnedTag>(entity);
            if (!isEditorOwned) {
                if (registry.all_of<EditorCameraPilotTag>(entity)) {
                    if (ImGui::Button("Stop Piloting##CameraPilot")) {
                        registry.remove<EditorCameraPilotTag>(entity);
                    }
                }
                else {
                    if (ImGui::Button("Pilot Camera##CameraPilot")) {
                        auto pilotView = registry.view<EditorCameraPilotTag>();
                        for (auto pilotEntity : pilotView) {
                            registry.remove<EditorCameraPilotTag>(pilotEntity);
                        }

                        registry.emplace<EditorCameraPilotTag>(entity);
                    }
                }
            }

            EditorUi::ScopedItemWidth width{ 300.0f };
            ImGui::DragFloat("FOV", &camera->fov, 0.5f, 1.0f, 179.0f);
            ImGui::DragFloat("Near Plane", &camera->nearPlane, 0.01f, 0.001f, 1000.0f);
            ImGui::DragFloat("Far Plane", &camera->farPlane, 10.0f, 1.0f, 1000000.0f);
            ImGui::DragFloat("Speed", &camera->speed, 0.5f, 0.0f, 10000.0f);

            const char* backgroundLabels[] = { "None", "Solid Color", "Skybox" };
            int backgroundMode = static_cast<int>(camera->backgroundMode);
            if (ImGui::Combo("Background", &backgroundMode, backgroundLabels, IM_ARRAYSIZE(backgroundLabels))) {
                camera->backgroundMode = static_cast<CameraBackgroundMode>(backgroundMode);
            }

            if (camera->backgroundMode == CameraBackgroundMode::SolidColor) {
                ImGui::ColorEdit4("Clear Color", &camera->clearColor.x);
            }
        }
    }
};

class CinematicCameraShotComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* shot = registry.try_get<CinematicCameraShotComponent>(entity);
        if (!shot)
            return;

        if (DrawRemovableComponentHeader<CinematicCameraShotComponent>(
                registry,
                entity,
                "Cinematic Camera Shot",
                "CinematicCameraShotComponent"))
        {
            shot->duration = CameraShotEditorUi::ResolveDuration(*shot);
            if (ImGui::Button("Open Animation Editor##CinematicShotOpenAnimationEditor")) {
                auto& windowRegistry = registry.ctx().get<ImGuiWindowRegistry>();
                windowRegistry.SetWindowOpen("Animation Editor", true);
            }

            ImGui::Text("Total Duration: %.2f", shot->duration);
            ImGui::Text("Keyframes: %d", static_cast<int>(shot->keyframes.size()));
            ImGui::TextDisabled(shot->playing ? "Playing" : "Stopped");
        }
    }
};

class MeshComponentUi : public ViewerComponentUi {
public:
    explicit MeshComponentUi(Engine::Core* core = nullptr) : _core(core) {}

    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* mesh = registry.try_get<MeshComponent>(entity);
        if (!mesh)
            return;

        if (DrawRemovableComponentHeader<MeshComponent>(registry, entity, "Mesh", "MeshComponent"))
        {
            MeshAssetReference source = mesh->source;
            if (!source.IsValid() && mesh->mesh) {
                source = mesh->mesh->source;
            }

            ImGui::Text("Path: %s", source.path.c_str());
            ImGui::Text("Mesh Index: %u", source.meshIndex);
            ImGui::Button("Drop Mesh Here", ImVec2(-1.0f, 0.0f));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("ENGINE_MESH_ASSET")) {
                    const auto& meshPayload =
                        *static_cast<const EditorMeshAssetPayload*>(payload->Data);
                    if (_core) {
                        auto meshes = _core->LoadGltfMeshes(_core, meshPayload.projectPath);
                        if (meshes && meshPayload.meshIndex < meshes->size()) {
                            mesh->mesh = meshes->at(meshPayload.meshIndex);
                            mesh->source = mesh->mesh->source;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            EditorUi::ScopedItemWidth width{ 320.0f };

            const char* currentMaterial =
                mesh->materialOverride.empty()
                    ? "Mesh Material"
                    : mesh->materialOverride.c_str();

            if (ImGui::BeginCombo("Material##MeshComponentMaterial", currentMaterial)) {
                const bool useMeshMaterial = mesh->materialOverride.empty();
                if (ImGui::Selectable("Mesh Material", useMeshMaterial)) {
                    mesh->materialOverride.clear();
                }

                if (_core) {
                    for (const auto& material : _core->GetMaterialAssets()) {
                        const bool selected = mesh->materialOverride == material.name;
                        if (ImGui::Selectable(material.name.c_str(), selected)) {
                            mesh->materialOverride = material.name;
                        }
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::ColorEdit4("Base Color Factor", &mesh->baseColorFactor.x);
        }
    }

private:
    Engine::Core* _core = nullptr;
};

class MeshCorruptionComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* corruption = registry.try_get<MeshCorruptionComponent>(entity);
        if (!corruption)
            return;

        if (DrawRemovableComponentHeader<MeshCorruptionComponent>(registry, entity, "Mesh Corruption", "MeshCorruptionComponent"))
        {
            EditorUi::ScopedItemWidth width{ 320.0f };
            ImGui::ColorEdit4("Color##MeshCorruptionColor", &corruption->color.x);
            ImGui::DragFloat("Scale##MeshCorruptionScale", &corruption->scale, 0.01f, 0.001f, 100000.0f);
            ImGui::DragFloat("Softness##MeshCorruptionSoftness", &corruption->softness, 0.001f, 0.001f, 1.0f);
            ImGui::DragFloat("Intensity##MeshCorruptionIntensity", &corruption->intensity, 0.01f, 0.0f, 1000.0f);
            ImGui::DragFloat("Amount##MeshCorruptionAmount", &corruption->amount, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Speed##MeshCorruptionSpeed", &corruption->speed, 0.01f, 0.0f, 100.0f);
            ImGui::DragFloat("Age##MeshCorruptionAge", &corruption->age, 0.01f, 0.0f, 100000.0f);
        }
    }
};

class EffectMeshComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* effect = registry.try_get<EffectMeshComponent>(entity);
        if (!effect)
            return;

        if (DrawRemovableComponentHeader<EffectMeshComponent>(registry, entity, "Effect Mesh", "EffectMeshComponent"))
        {
            EditorUi::ScopedItemWidth width{ 320.0f };
            ImGui::ColorEdit4("Color##EffectMeshColor", &effect->color.x);
            ImGui::ColorEdit4("Corruption Color##EffectMeshCorruptionColor", &effect->corruptionColor.x);
            ImGui::DragFloat3("Velocity##EffectMeshVelocity", &effect->velocity.x, 0.1f);
            ImGui::DragFloat3("Angular Velocity##EffectMeshAngularVelocity", &effect->angularVelocity.x, 0.1f);
            ImGui::DragFloat("Lifetime##EffectMeshLifetime", &effect->lifetime, 0.01f, 0.001f, 100000.0f);
            ImGui::DragFloat("Age##EffectMeshAge", &effect->age, 0.01f, 0.0f, 100000.0f);
            ImGui::DragFloat("Start Scale##EffectMeshStartScale", &effect->startScale, 0.01f, 0.0f, 100000.0f);
            ImGui::DragFloat("End Scale##EffectMeshEndScale", &effect->endScale, 0.01f, 0.0f, 100000.0f);
            ImGui::DragFloat("Fresnel Power##EffectMeshFresnelPower", &effect->fresnelPower, 0.01f, 0.0f, 100.0f);
            ImGui::DragFloat("Fresnel Intensity##EffectMeshFresnelIntensity", &effect->fresnelIntensity, 0.01f, 0.0f, 1000.0f);
            ImGui::DragFloat("Base Intensity##EffectMeshBaseIntensity", &effect->baseIntensity, 0.01f, 0.0f, 1000.0f);
            ImGui::DragFloat("Corruption Scale##EffectMeshCorruptionScale", &effect->corruptionScale, 0.01f, 0.001f, 100000.0f);
            ImGui::DragFloat("Corruption Softness##EffectMeshCorruptionSoftness", &effect->corruptionSoftness, 0.001f, 0.001f, 10.0f);
            ImGui::DragFloat("Corruption Intensity##EffectMeshCorruptionIntensity", &effect->corruptionIntensity, 0.01f, 0.0f, 1000.0f);
            ImGui::DragFloat("Corruption Amount##EffectMeshCorruptionAmount", &effect->corruptionAmount, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("Destroy On Complete##EffectMeshDestroyOnComplete", &effect->destroyOnComplete);
        }
    }
};

class SingleRenderTagUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        if (!registry.all_of<SingleRenderTag>(entity))
            return;

        if (DrawRemovableComponentHeader<SingleRenderTag>(registry, entity, "Single Render", "SingleRenderTag"))
        {
            ImGui::TextUnformatted("Enabled");
        }
    }
};

class ActiveCameraTagUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        if (!registry.all_of<ActiveCameraTag>(entity))
            return;

        if (DrawRemovableComponentHeader<ActiveCameraTag>(registry, entity, "Active Camera", "ActiveCameraTag"))
        {
            ImGui::TextUnformatted("Used for rendering in Play Mode");
        }
    }
};

class VelocityComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* velocity = registry.try_get<VelocityComponent>(entity);
        if (!velocity)
            return;

        if (DrawRemovableComponentHeader<VelocityComponent>(registry, entity, "Velocity", "VelocityComponent"))
        {
            EditorUi::ScopedItemWidth width{ 320.0f };
            ImGui::DragFloat3("Linear", &velocity->linear.x, 0.1f);
            ImGui::DragFloat3("Angular", &velocity->angular.x, 0.1f);
        }
    }
};

class GravityBodyComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* gravityBody = registry.try_get<GravityBodyComponent>(entity);
        if (!gravityBody)
            return;

        if (DrawRemovableComponentHeader<GravityBodyComponent>(registry, entity, "Gravity Body", "GravityBodyComponent"))
        {
            EditorUi::ScopedItemWidth width{ 260.0f };
            ImGui::DragFloat("Mass", &gravityBody->mass, 0.1f, 0.0f, 1000000000.0f);
            ImGui::Checkbox("Affected By Gravity", &gravityBody->affectedByGravity);

            if (auto* velocity = registry.try_get<VelocityComponent>(entity)) {
                const float linearSpeed = glm::length(velocity->linear);
                ImGui::TextDisabled("Linear speed: %.3f", linearSpeed);
            }

            if (auto* gravityState = registry.try_get<GravityStateComponent>(entity)) {
                const float acceleration = glm::length(gravityState->acceleration);
                ImGui::TextDisabled("Gravity accel: %.3f", acceleration);
                if (gravityState->dominantSource != entt::null) {
                    ImGui::TextDisabled("Dominant source: %u", entt::to_integral(gravityState->dominantSource));
                } else {
                    ImGui::TextDisabled("Dominant source: none");
                }
            }
        }
    }
};

class GravityParticleComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* gravityParticle = registry.try_get<GravityParticleComponent>(entity);
        if (!gravityParticle)
            return;

        if (DrawRemovableComponentHeader<GravityParticleComponent>(registry, entity, "Gravity Particle", "GravityParticleComponent"))
        {
            EditorUi::ScopedItemWidth width{ 260.0f };
            ImGui::DragFloat("Gravity Scale", &gravityParticle->gravityScale, 0.01f, 0.0f, 1000.0f);
        }
    }
};

class JoltColliderComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* collider = registry.try_get<Engine::JoltColliderComponent>(entity);
        if (!collider)
            return;

        if (DrawRemovableComponentHeader<Engine::JoltColliderComponent>(
                registry,
                entity,
                "Jolt Collider",
                "JoltColliderComponent"))
        {
            EditorUi::ScopedItemWidth width{ 260.0f };

            int shapeIndex = static_cast<int>(collider->shape);
            constexpr const char* shapeLabels[] { "Sphere", "Box", "Capsule", "Cylinder", "Cone", "Convex Hull", "Compound" };
            if (ImGui::Combo("Shape##JoltColliderShape", &shapeIndex, shapeLabels, IM_ARRAYSIZE(shapeLabels))) {
                collider->shape = static_cast<Engine::JoltColliderShape>(shapeIndex);
                if (collider->shape == Engine::JoltColliderShape::ConvexHull &&
                    collider->convexHullPoints.empty()) {
                    const auto* mesh = registry.try_get<MeshComponent>(entity);
                    if (mesh && mesh->mesh && !mesh->mesh->pickingPositions.empty()) {
                        collider->convexHullPoints = Engine::BuildJoltConvexHullPoints(
                            mesh->mesh->pickingPositions);
                    }
                }
            }

            int motionIndex = static_cast<int>(collider->motion);
            constexpr const char* motionLabels[] { "Static", "Kinematic" };
            if (ImGui::Combo("Motion##JoltColliderMotion", &motionIndex, motionLabels, IM_ARRAYSIZE(motionLabels))) {
                collider->motion = static_cast<Engine::JoltBodyMotion>(motionIndex);
            }

            ImGui::Checkbox("Sensor##JoltColliderSensor", &collider->sensor);
            ImGui::DragFloat3("Center##JoltColliderCenter", &collider->center.x, 0.01f);

            switch (collider->shape) {
            case Engine::JoltColliderShape::Sphere:
                ImGui::DragFloat("Radius##JoltColliderRadius", &collider->radius, 0.01f, 0.001f, 100000.0f);
                break;
            case Engine::JoltColliderShape::Box:
                ImGui::DragFloat3("Half Extents##JoltColliderHalfExtents", &collider->halfExtents.x, 0.01f, 0.001f, 100000.0f);
                break;
            case Engine::JoltColliderShape::Capsule:
                ImGui::DragFloat("Radius##JoltColliderCapsuleRadius", &collider->radius, 0.01f, 0.001f, 100000.0f);
                ImGui::DragFloat("Cylinder Half Height##JoltColliderCapsuleHalfHeight", &collider->capsuleHalfHeight, 0.01f, 0.0f, 100000.0f);
                break;
            case Engine::JoltColliderShape::Cylinder:
                ImGui::DragFloat("Radius##JoltColliderCylinderRadius", &collider->radius, 0.01f, 0.001f, 100000.0f);
                ImGui::DragFloat("Half Height##JoltColliderCylinderHalfHeight", &collider->cylinderHalfHeight, 0.01f, 0.001f, 100000.0f);
                break;
            case Engine::JoltColliderShape::Cone:
                ImGui::DragFloat("Radius##JoltColliderConeRadius", &collider->radius, 0.01f, 0.001f, 100000.0f);
                ImGui::DragFloat("Half Height##JoltColliderConeHalfHeight", &collider->coneHalfHeight, 0.01f, 0.001f, 100000.0f);
                break;
            case Engine::JoltColliderShape::ConvexHull: {
                const auto* mesh = registry.try_get<MeshComponent>(entity);
                const bool canGenerate =
                    mesh && mesh->mesh && !mesh->mesh->pickingPositions.empty();
                ImGui::TextDisabled(
                    "Hull points: %zu",
                    collider->convexHullPoints.size());
                ImGui::BeginDisabled(!canGenerate);
                if (ImGui::Button("Generate From Mesh##JoltColliderConvexHullGenerate")) {
                    collider->convexHullPoints = Engine::BuildJoltConvexHullPoints(
                        mesh->mesh->pickingPositions);
                }
                ImGui::EndDisabled();
                break;
            }
            case Engine::JoltColliderShape::Compound:
                ImGui::TextDisabled("Uses primitive collider descendants");
                break;
            }

            ImGui::DragFloat("Friction##JoltColliderFriction", &collider->friction, 0.01f, 0.0f, 100.0f);
            ImGui::DragFloat("Restitution##JoltColliderRestitution", &collider->restitution, 0.01f, 0.0f, 100.0f);

            const auto* body = registry.try_get<Engine::JoltBodyComponent>(entity);
            ImGui::TextDisabled(
                body ? "Runtime body: created" : "Runtime body: pending");
            if (body) {
                const glm::vec3 boxSize = body->halfExtents * body->transformScale * 2.0f;
                ImGui::TextDisabled(
                    "Runtime box size: %.3f %.3f %.3f",
                    boxSize.x,
                    boxSize.y,
                    boxSize.z);
            }
        }
    }
};
