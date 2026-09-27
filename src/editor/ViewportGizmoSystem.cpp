#include "ViewportGizmoSystem.h"

#include "Camera.h"
#include "Core.h"
#include "EditorInteractionState.h"
#include "EditorSelection.h"
#include "EntityState.h"
#include "HierarchyComponent.h"
#include "HierarchySystem.h"
#include "Transform.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <cmath>

namespace {
entt::entity ResolveEditorRenderCamera(entt::registry& registry)
{
    auto pilotView = registry.view<Camera, Transform, EditorCameraPilotTag>(
        entt::exclude<Engine::CoreOwnedTag, DisabledEntityTag>);
    for (const entt::entity entity : pilotView) {
        if (!IsEntityDisabled(registry, entity)) {
            return entity;
        }
    }

    auto editorView = registry.view<Camera, Transform, ActiveCameraTag, Engine::CoreOwnedTag>();
    if (editorView.begin() != editorView.end()) {
        return *editorView.begin();
    }

    auto sceneView = registry.view<Camera, Transform, ActiveCameraTag>(
        entt::exclude<Engine::CoreOwnedTag, DisabledEntityTag>);
    for (const entt::entity entity : sceneView) {
        if (!IsEntityDisabled(registry, entity)) {
            return entity;
        }
    }

    return entt::null;
}

Transform TransformFromMatrix(const glm::mat4& matrix)
{
    Transform transform;
    glm::vec3 skew;
    glm::vec4 perspective;

    if (!glm::decompose(
            matrix,
            transform.scale,
            transform.rotation,
            transform.position,
            skew,
            perspective)) {
        return transform;
    }

    transform.rotation = glm::normalize(transform.rotation);
    return transform;
}

void DrawModeButton(const char* label, bool active, bool& value)
{
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button(label)) {
        value = true;
    }
    if (active) {
        ImGui::PopStyleColor();
    }
}
}

ViewportGizmoSystem::ViewportGizmoSystem(entt::registry& registry, Engine::Core& core)
    : System(registry), _core(core)
{
    if (!_registry.ctx().contains<EditorInteractionState>()) {
        _registry.ctx().emplace<EditorInteractionState>();
    }
    if (!_registry.ctx().contains<EditorHistory>()) {
        _registry.ctx().emplace<EditorHistory>();
    }
}

void ViewportGizmoSystem::DrawUi()
{
    ImGuizmo::BeginFrame();

    auto& interaction = _registry.ctx().get<EditorInteractionState>();
    interaction.gizmoHovered = false;
    interaction.gizmoUsing = false;

    if (_core.IsPlayMode() || !_registry.ctx().contains<EditorSelection>()) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    auto& history = _registry.ctx().get<EditorHistory>();
    if (!interaction.cameraNavigating &&
        !io.WantTextInput &&
        !ImGui::IsAnyItemActive()) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (io.KeyShift) {
                history.Redo(_registry);
            }
            else {
                history.Undo(_registry);
            }
        }
        else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            history.Redo(_registry);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
            _operation = Operation::None;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            _operation = Operation::Translate;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            _operation = Operation::Rotate;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            _operation = Operation::Scale;
        }
    }

    const entt::entity selectedEntity =
        _registry.ctx().get<EditorSelection>().selectedEntity;
    if (selectedEntity == entt::null ||
        !_registry.valid(selectedEntity) ||
        !_registry.all_of<Transform>(selectedEntity) ||
        _registry.all_of<Engine::CoreOwnedTag>(selectedEntity)) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 38.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    constexpr ImGuiWindowFlags toolbarFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("Viewport Gizmo", nullptr, toolbarFlags)) {
        const auto operationButton = [&](const char* label, Operation operation) {
            const bool active = _operation == operation;
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::Button(label)) {
                _operation = operation;
            }
            if (active) {
                ImGui::PopStyleColor();
            }
        };

        operationButton("Q", Operation::None);
        ImGui::SameLine();
        operationButton("W", Operation::Translate);
        ImGui::SameLine();
        operationButton("E", Operation::Rotate);
        ImGui::SameLine();
        operationButton("R", Operation::Scale);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        ImGui::BeginDisabled(!history.CanUndo());
        if (ImGui::Button("Undo")) {
            history.Undo(_registry);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!history.CanRedo());
        if (ImGui::Button("Redo")) {
            history.Redo(_registry);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        bool worldRequested = false;
        DrawModeButton("World", !_localMode, worldRequested);
        if (worldRequested) {
            _localMode = false;
        }
        ImGui::SameLine();
        bool localRequested = false;
        DrawModeButton("Local", _localMode, localRequested);
        if (localRequested) {
            _localMode = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &_snapEnabled);
        ImGui::SameLine();

        ImGui::SetNextItemWidth(64.0f);
        if (_operation == Operation::Rotate) {
            ImGui::DragFloat("##RotationSnap", &_rotationSnap, 1.0f, 0.1f, 180.0f, "%.1f deg");
        }
        else if (_operation == Operation::Scale) {
            ImGui::DragFloat("##ScaleSnap", &_scaleSnap, 0.01f, 0.001f, 100.0f, "%.3f");
        }
        else {
            ImGui::DragFloat("##TranslationSnap", &_translationSnap, 0.1f, 0.001f, 10000.0f, "%.2f");
        }
    }
    ImGui::End();

    if (_operation == Operation::None) {
        return;
    }

    const entt::entity cameraEntity = ResolveEditorRenderCamera(_registry);
    if (cameraEntity == entt::null) {
        return;
    }

    const auto& camera = _registry.get<Camera>(cameraEntity);
    const auto& cameraTransform = _registry.get<Transform>(cameraEntity);
    const float aspectRatio = io.DisplaySize.x / io.DisplaySize.y;
    const glm::mat4 view = camera.GetViewMatrix(cameraTransform);
    glm::mat4 projection = camera.GetProjectionMatrix(aspectRatio);
    // ImGuizmo performs its own top-left screen-space Y conversion.
    // Remove the Vulkan projection flip before handing the matrix to it.
    projection[1][1] *= -1.0f;
    glm::mat4 model = _registry.get<Transform>(selectedEntity).GetModelMatrix();

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);

    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    if (_operation == Operation::Rotate) {
        operation = ImGuizmo::ROTATE;
    }
    else if (_operation == Operation::Scale) {
        operation = ImGuizmo::SCALE;
    }

    float snap[3] { 0.0f, 0.0f, 0.0f };
    if (_operation == Operation::Rotate) {
        snap[0] = _rotationSnap;
    }
    else if (_operation == Operation::Scale) {
        snap[0] = snap[1] = snap[2] = _scaleSnap;
    }
    else {
        snap[0] = snap[1] = snap[2] = _translationSnap;
    }
    const bool useSnap = _snapEnabled || io.KeyCtrl;

    const bool changed = ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(projection),
        operation,
        _localMode ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
        glm::value_ptr(model),
        nullptr,
        useSnap ? snap : nullptr);

    interaction.gizmoHovered =
        !interaction.cameraNavigating && ImGuizmo::IsOver();
    interaction.gizmoUsing =
        !interaction.cameraNavigating && ImGuizmo::IsUsing();

    if (interaction.gizmoUsing && !_wasUsing) {
        _transactionEntity = selectedEntity;
        _transactionBefore = EditorHistory::CaptureTransform(_registry, selectedEntity);
    }

    if (changed || interaction.gizmoUsing) {
        Transform worldTransform = TransformFromMatrix(model);
        _registry.replace<Transform>(selectedEntity, worldTransform);

        auto* hierarchy = _registry.try_get<HierarchyComponent>(selectedEntity);
        if (hierarchy &&
            hierarchy->inheritTransform &&
            hierarchy->parent != entt::null &&
            _registry.valid(hierarchy->parent) &&
            _registry.all_of<Transform>(hierarchy->parent)) {
            hierarchy->localTransform = Engine::ComputeHierarchyLocalTransform(
                _registry,
                hierarchy->parent,
                worldTransform);
        }
    }

    if (!interaction.gizmoUsing &&
        _wasUsing &&
        _transactionBefore &&
        _transactionEntity != entt::null &&
        _registry.valid(_transactionEntity) &&
        _registry.all_of<Transform>(_transactionEntity)) {
        history.PushTransform(EditorTransformCommand {
            _transactionEntity,
            *_transactionBefore,
            EditorHistory::CaptureTransform(_registry, _transactionEntity)
        });
        _transactionEntity = entt::null;
        _transactionBefore.reset();
    }

    _wasUsing = interaction.gizmoUsing;
}
