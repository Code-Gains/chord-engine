#include "RegistryViewer.h"
#include <string>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "Core.h"
#include "Camera.h"
#include "EditorSelection.h"
#include "EditorInteractionState.h"
#include "EditorHistory.h"
#include "EntityState.h"
#include "InputSystem.h"
#include "MeshComponent.h"
#include "NameComponent.h"
#include "Transform.h"
#include "HierarchyComponent.h"
#include "HierarchySystem.h"
#include "WorldSerializer.h"
#include <ImGuiWindowRegistry.h>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>

namespace {
    bool IntersectRaySphere(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDirection,
        const glm::vec3& center,
        float radius,
        float& distance)
    {
        const glm::vec3 offset = rayOrigin - center;
        const float projectedOffset = glm::dot(offset, rayDirection);
        const float discriminant =
            projectedOffset * projectedOffset -
            (glm::dot(offset, offset) - radius * radius);
        if (discriminant < 0.0f) {
            return false;
        }

        const float root = std::sqrt(discriminant);
        distance = -projectedOffset - root;
        if (distance < 0.0f) {
            distance = -projectedOffset + root;
        }

        return distance >= 0.0f;
    }

    bool IntersectRayTriangles(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDirection,
        const MeshAsset& mesh,
        float& nearestDistance)
    {
        constexpr double epsilon = 0.000000000001;
        bool hit = false;

        const glm::dvec3 preciseRayOrigin(rayOrigin);
        const glm::dvec3 preciseRayDirection(rayDirection);

        for (size_t index = 0; index + 2 < mesh.pickingIndices.size(); index += 3) {
            const uint32_t index0 = mesh.pickingIndices[index];
            const uint32_t index1 = mesh.pickingIndices[index + 1];
            const uint32_t index2 = mesh.pickingIndices[index + 2];
            if (index0 >= mesh.pickingPositions.size() ||
                index1 >= mesh.pickingPositions.size() ||
                index2 >= mesh.pickingPositions.size()) {
                continue;
            }

            const glm::dvec3 vertex0(mesh.pickingPositions[index0]);
            const glm::dvec3 vertex1(mesh.pickingPositions[index1]);
            const glm::dvec3 vertex2(mesh.pickingPositions[index2]);
            const glm::dvec3 edge1 = vertex1 - vertex0;
            const glm::dvec3 edge2 = vertex2 - vertex0;
            const glm::dvec3 perpendicular = glm::cross(preciseRayDirection, edge2);
            const double determinant = glm::dot(edge1, perpendicular);
            if (std::abs(determinant) < epsilon) {
                continue;
            }

            const double inverseDeterminant = 1.0 / determinant;
            const glm::dvec3 originOffset = preciseRayOrigin - vertex0;
            const double u = glm::dot(originOffset, perpendicular) * inverseDeterminant;
            if (u < 0.0 || u > 1.0) {
                continue;
            }

            const glm::dvec3 crossOffset = glm::cross(originOffset, edge1);
            const double v = glm::dot(preciseRayDirection, crossOffset) * inverseDeterminant;
            if (v < 0.0 || u + v > 1.0) {
                continue;
            }

            const double distance = glm::dot(edge2, crossOffset) * inverseDeterminant;
            if (distance >= 0.0 && distance < static_cast<double>(nearestDistance)) {
                nearestDistance = static_cast<float>(distance);
                hit = true;
            }
        }

        return hit;
    }

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

    EditorSelection& GetEditorSelection(entt::registry& registry)
    {
        if (!registry.ctx().contains<EditorSelection>()) {
            registry.ctx().emplace<EditorSelection>();
        }

        return registry.ctx().get<EditorSelection>();
    }

    void ClearSelectedEntity(entt::registry& registry, entt::entity& selectedEntity)
    {
        selectedEntity = entt::null;
        GetEditorSelection(registry).selectedEntity = entt::null;
    }

    bool CanDeleteSelectedEntity(entt::registry& registry, entt::entity selectedEntity)
    {
        return selectedEntity != entt::null &&
            registry.valid(selectedEntity) &&
            !registry.all_of<Engine::CoreOwnedTag>(selectedEntity);
    }

    bool IsAncestorOf(
        entt::registry& registry,
        entt::entity ancestor,
        entt::entity entity)
    {
        const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
        entt::entity current = hierarchy ? hierarchy->parent : entt::null;
        while (current != entt::null && registry.valid(current)) {
            if (current == ancestor) {
                return true;
            }
            const auto* parentHierarchy = registry.try_get<HierarchyComponent>(current);
            current = parentHierarchy ? parentHierarchy->parent : entt::null;
        }
        return false;
    }

    void CollectDescendants(
        entt::registry& registry,
        entt::entity parent,
        std::vector<entt::entity>& descendants)
    {
        auto view = registry.view<HierarchyComponent>();
        for (auto entity : view) {
            const auto& hierarchy = view.get<HierarchyComponent>(entity);
            if (hierarchy.parent != parent) {
                continue;
            }

            CollectDescendants(registry, entity, descendants);
            descendants.push_back(entity);
        }
    }

    void DestroySelectedEntity(entt::registry& registry, entt::entity& selectedEntity)
    {
        if (!CanDeleteSelectedEntity(registry, selectedEntity)) {
            return;
        }

        std::vector<entt::entity> descendants;
        CollectDescendants(registry, selectedEntity, descendants);

        for (auto entity : descendants) {
            if (registry.valid(entity) && !registry.all_of<Engine::CoreOwnedTag>(entity)) {
                registry.destroy(entity);
            }
        }

        registry.destroy(selectedEntity);
        ClearSelectedEntity(registry, selectedEntity);
    }

    bool HasVisibleParent(entt::registry& registry, entt::entity entity, bool showCoreOwnedEntities)
    {
        auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
        if (!hierarchy ||
            hierarchy->parent == entt::null ||
            !registry.valid(hierarchy->parent) ||
            !registry.all_of<Transform>(hierarchy->parent)) {
            return false;
        }

        if (!showCoreOwnedEntities &&
            registry.all_of<Engine::CoreOwnedTag>(hierarchy->parent)) {
            return false;
        }

        return true;
    }

    bool NameExists(entt::registry& registry, const std::string& name)
    {
        auto view = registry.view<NameComponent>();
        for (auto entity : view) {
            if (view.get<NameComponent>(entity).name == name) {
                return true;
            }
        }

        return false;
    }

    std::string CopyNameBase(const std::string& name)
    {
        size_t end = name.size();
        while (end > 0 && std::isdigit(static_cast<unsigned char>(name[end - 1]))) {
            --end;
        }

        if (end == name.size()) {
            return name;
        }

        while (end > 0 && std::isspace(static_cast<unsigned char>(name[end - 1]))) {
            --end;
        }

        return name.substr(0, end);
    }

    std::string MakeUniqueCopyName(entt::registry& registry, const std::string& sourceName)
    {
        const std::string baseName = CopyNameBase(sourceName);
        for (int suffix = 2; suffix < 100000; ++suffix) {
            const std::string candidate = baseName + " " + std::to_string(suffix);
            if (!NameExists(registry, candidate)) {
                return candidate;
            }
        }

        return baseName + " Copy";
    }
}

void RegistryViewer::Update(float deltaTime)
{
    auto inputView = _registry.view<InputState>();
    if (inputView.empty()) {
        return;
    }

    auto inputEntity = *inputView.begin();
    auto& input = inputView.get<InputState>(inputEntity);

    if (input.keys[GLFW_KEY_ESCAPE].pressed) {
        ClearSelectedEntity(_registry, _selectedEntity);
    }
}

void RegistryViewer::DrawUi()
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();
    auto& editorSelection = GetEditorSelection(_registry);
    if (editorSelection.selectedEntity != _selectedEntity) {
        _selectedEntity = editorSelection.selectedEntity;
        if (_selectedEntity != entt::null && !_registry.valid(_selectedEntity)) {
            ClearSelectedEntity(_registry, _selectedEntity);
        }
        else {
            _revealSelectedEntity = _selectedEntity != entt::null;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
        !ImGui::GetIO().WantTextInput &&
        !ImGui::IsAnyItemActive())
    {
        DeleteSelectedEntityWithHistory();
    }

    const ImGuiIO& io = ImGui::GetIO();
    const auto* interaction = _registry.ctx().find<EditorInteractionState>();
    const bool gizmoOwnsMouse = interaction &&
        (interaction->gizmoHovered || interaction->gizmoUsing);
    if (_core &&
        !_core->IsPlayMode() &&
        !io.WantCaptureMouse &&
        !gizmoOwnsMouse &&
        io.DisplaySize.x > 0.0f &&
        io.DisplaySize.y > 0.0f &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left, false)) {
        const entt::entity cameraEntity = ResolveEditorRenderCamera(_registry);
        if (cameraEntity != entt::null) {
            const auto& camera = _registry.get<Camera>(cameraEntity);
            const auto& cameraTransform = _registry.get<Transform>(cameraEntity);
            const float aspectRatio = io.DisplaySize.x / io.DisplaySize.y;
            const glm::mat4 viewMatrix = camera.GetViewMatrix(cameraTransform);
            const glm::mat4 projectionMatrix = camera.GetProjectionMatrix(aspectRatio);
            const glm::mat4 inverseViewProjection = glm::inverse(
                projectionMatrix * viewMatrix);
            const float ndcX = (2.0f * io.MousePos.x / io.DisplaySize.x) - 1.0f;
            const float ndcY = (2.0f * io.MousePos.y / io.DisplaySize.y) - 1.0f;
            glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);

            if (std::abs(nearPoint.w) > 0.000001f &&
                std::abs(farPoint.w) > 0.000001f) {
                nearPoint /= nearPoint.w;
                farPoint /= farPoint.w;
                const glm::vec3 rayOrigin = glm::vec3(nearPoint);
                const glm::vec3 rayVector = glm::vec3(farPoint - nearPoint);
                const float rayLengthSquared = glm::dot(rayVector, rayVector);
                if (rayLengthSquared > 0.000001f) {
                    const glm::vec3 rayDirection =
                        rayVector / std::sqrt(rayLengthSquared);

                    entt::entity nearestEntity = entt::null;
                    float nearestDistance = std::numeric_limits<float>::max();
                    auto meshView = _registry.view<MeshComponent, Transform>(
                        entt::exclude<DisabledEntityTag, EffectMeshComponent, Engine::CoreOwnedTag>);

                    for (const entt::entity entity : meshView) {
                        if (IsEntityDisabled(_registry, entity)) {
                            continue;
                        }

                        const auto& mesh = meshView.get<MeshComponent>(entity);
                        if (!mesh.mesh) {
                            continue;
                        }

                        const auto& transform = meshView.get<Transform>(entity);
                        const glm::vec3 absoluteScale = glm::abs(transform.scale);
                        const float maximumScale = std::max({
                            absoluteScale.x,
                            absoluteScale.y,
                            absoluteScale.z
                        });
                        const glm::vec3 worldCenter =
                            transform.position +
                            transform.rotation * (mesh.mesh->boundsCenter * transform.scale);
                        const float worldRadius = std::max(
                            mesh.mesh->boundsRadius * maximumScale,
                            0.0001f);

                        float hitDistance = 0.0f;
                        if (IntersectRaySphere(
                                rayOrigin,
                                rayDirection,
                                worldCenter,
                                worldRadius,
                                hitDistance) &&
                            !mesh.mesh->pickingIndices.empty()) {
                            const glm::mat4 inverseModel = glm::inverse(transform.GetModelMatrix());
                            const glm::vec3 localRayOrigin = glm::vec3(
                                inverseModel * glm::vec4(rayOrigin, 1.0f));
                            const glm::vec3 localRayDirection = glm::vec3(
                                inverseModel * glm::vec4(rayDirection, 0.0f));
                            float triangleDistance = nearestDistance;
                            if (IntersectRayTriangles(
                                    localRayOrigin,
                                    localRayDirection,
                                    *mesh.mesh,
                                    triangleDistance)) {
                                nearestDistance = triangleDistance;
                                nearestEntity = entity;
                            }
                        }
                    }

                    SetSelectedEntity(nearestEntity);
                }
            }
        }
    }

    if (!io.WantTextInput && !ImGui::IsAnyItemActive()) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            CopySelectedEntity();
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            PasteCopiedEntity();
        }
    }

    if (!windowRegistry.IsWindowOpen("Registry Viewer"))
        return;

    bool open = true;

    if (ImGui::Begin("Registry Viewer", &open))
    {
        if (ImGui::Button("+ Entity"))
        {
            auto entity = _registry.create();
            _registry.emplace<Transform>(entity);
            _registry.emplace<NameComponent>(
                entity,
                "Entity " + std::to_string((int)entt::to_integral(entity))
            );

            _selectedEntity = entity;
            editorSelection.selectedEntity = _selectedEntity;

            if (_core) {
                auto serializer = _core->CreateWorldSerializer();
                if (auto hierarchy = serializer.SerializeEntityHierarchy(*_core, entity)) {
                    auto& history = GetEditorHistory(_registry);
                    history.PushEntityLifecycle(EditorEntityLifecycleCommand {
                        std::move(*hierarchy),
                        false,
                        true,
                        "Create " + _registry.get<NameComponent>(entity).name
                    });
                }
            }
        }

        ImGui::SameLine();

        const bool canDeleteSelectedEntity = CanDeleteSelectedEntity(_registry, _selectedEntity);

        if (!canDeleteSelectedEntity) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("- Entity"))
        {
            DeleteSelectedEntityWithHistory();
        }

        if (!canDeleteSelectedEntity) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        const bool canCopySelectedEntity = CanCopySelectedEntity();
        if (!canCopySelectedEntity) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Copy"))
        {
            CopySelectedEntity();
        }

        if (!canCopySelectedEntity) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        const bool canPasteEntity = CanPasteEntity();
        if (!canPasteEntity) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Paste"))
        {
            PasteCopiedEntity();
        }

        if (!canPasteEntity) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        const bool hasSelectedEntity =
            _selectedEntity != entt::null &&
            _registry.valid(_selectedEntity);

        if (!hasSelectedEntity) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Clear"))
        {
            ClearSelectedEntity(_registry, _selectedEntity);
        }

        if (!hasSelectedEntity) {
            ImGui::EndDisabled();
        }

        ImGui::Checkbox("Show editor entities", &_showCoreOwnedEntities);
        ImGui::SameLine();
        ImGui::Checkbox("Show entity ids", &_showEntityIds);

        ImGui::Separator();
        ImGui::TextDisabled("Drag entity onto another to attach. Hold Shift to organize only.");

        auto view = _registry.view<Transform>();
        for (auto entity : view) {
            if (!_showCoreOwnedEntities &&
                _registry.all_of<Engine::CoreOwnedTag>(entity))
            {
                if (_selectedEntity == entity) {
                    ClearSelectedEntity(_registry, _selectedEntity);
                }

                continue;
            }

            if (!HasVisibleParent(_registry, entity, _showCoreOwnedEntities)) {
                DrawEntityNode(entity);
            }
        }

        ImVec2 emptyDropSize = ImGui::GetContentRegionAvail();
        emptyDropSize.x = std::max(emptyDropSize.x, 1.0f);
        emptyDropSize.y = std::max(emptyDropSize.y, 36.0f);

        ImGui::InvisibleButton("##RegistryEmptyDropTarget", emptyDropSize);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REGISTRY_ENTITY")) {
                const auto entityId = *static_cast<const uint32_t*>(payload->Data);
                const auto child = static_cast<entt::entity>(entityId);
                if (child != entt::null &&
                    _registry.valid(child) &&
                    _registry.all_of<Transform>(child)) {
                    if (_registry.all_of<HierarchyComponent>(child)) {
                        _registry.remove<HierarchyComponent>(child);
                    }
                    SetSelectedEntity(child);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    ImGui::End();

    windowRegistry.SetWindowOpen("Registry Viewer", open);
}

RegistryViewer::RegistryViewer(entt::registry &registry, Engine::Core* core)
    : System(registry)
    , _core(core)
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();
    GetEditorSelection(_registry);

    windowRegistry.RegisterWindow(
        "Registry Viewer",
        true
    );
}

const entt::entity& RegistryViewer::GetSelectedEntity() const
{
    return _selectedEntity;
}

void RegistryViewer::SetSelectedEntity(entt::entity entity)
{
    if (entity != entt::null && !_registry.valid(entity)) {
        ClearSelectedEntity(_registry, _selectedEntity);
        return;
    }

    _selectedEntity = entity;
    GetEditorSelection(_registry).selectedEntity = entity;
    _revealSelectedEntity = entity != entt::null;
}

bool RegistryViewer::CanCopySelectedEntity() const
{
    return _core &&
        _selectedEntity != entt::null &&
        _registry.valid(_selectedEntity) &&
        !_registry.all_of<Engine::CoreOwnedTag>(_selectedEntity);
}

bool RegistryViewer::CanPasteEntity() const
{
    return _core && _copiedEntity.has_value();
}

bool RegistryViewer::CopySelectedEntity()
{
    if (!CanCopySelectedEntity()) {
        return false;
    }

    auto serializer = _core->CreateWorldSerializer();
    _copiedEntity = serializer.SerializeEntityHierarchy(*_core, _selectedEntity);
    return _copiedEntity.has_value();
}

bool RegistryViewer::PasteCopiedEntity()
{
    if (!CanPasteEntity()) {
        return false;
    }

    auto serializer = _core->CreateWorldSerializer();
    const auto pastedRoot = serializer.InstantiateEntityHierarchy(
        *_core,
        _copiedEntity.value(),
        false,
        true);
    const entt::entity pastedEntity = pastedRoot.value_or(entt::null);
    if (_registry.valid(pastedEntity)) {
        if (auto* name = _registry.try_get<NameComponent>(pastedEntity)) {
            name->name = MakeUniqueCopyName(_registry, name->name);
        }

        if (auto hierarchy = serializer.SerializeEntityHierarchy(*_core, pastedEntity)) {
            auto& history = GetEditorHistory(_registry);
            history.PushEntityLifecycle(EditorEntityLifecycleCommand {
                std::move(*hierarchy),
                false,
                true,
                "Duplicate " + (_registry.all_of<NameComponent>(pastedEntity)
                    ? _registry.get<NameComponent>(pastedEntity).name
                    : std::string("entity"))
            });
        }
    }

    SetSelectedEntity(pastedEntity);
    return _registry.valid(pastedEntity);
}

bool RegistryViewer::DeleteSelectedEntityWithHistory()
{
    if (!_core || !CanDeleteSelectedEntity(_registry, _selectedEntity)) {
        return false;
    }

    auto serializer = _core->CreateWorldSerializer();
    const std::string entityName = _registry.all_of<NameComponent>(_selectedEntity)
        ? _registry.get<NameComponent>(_selectedEntity).name
        : std::string("entity");
    auto hierarchy = serializer.SerializeEntityHierarchy(*_core, _selectedEntity);
    if (!hierarchy) {
        return false;
    }

    DestroySelectedEntity(_registry, _selectedEntity);
    auto& history = GetEditorHistory(_registry);
    history.PushEntityLifecycle(EditorEntityLifecycleCommand {
        std::move(*hierarchy),
        true,
        false,
        "Delete " + entityName
    });
    return true;
}

void RegistryViewer::DrawEntityNode(entt::entity entity)
{
    if (!_registry.valid(entity) || !_registry.all_of<Transform>(entity)) {
        return;
    }

    std::string label;
    if (auto* name = _registry.try_get<NameComponent>(entity)) {
        label = name->name;
    }
    else {
        label = "Entity " + std::to_string((int)entt::to_integral(entity));
    }

    if (_showEntityIds) {
        label += " [" + std::to_string((int)entt::to_integral(entity)) + "]";
    }

    const bool disabled = IsEntityDisabled(_registry, entity);

    bool hasChildren = false;
    auto hierarchyView = _registry.view<HierarchyComponent>();
    for (auto child : hierarchyView) {
        const auto& hierarchy = hierarchyView.get<HierarchyComponent>(child);
        if (hierarchy.parent == entity &&
            _registry.valid(child) &&
            _registry.all_of<Transform>(child) &&
            (_showCoreOwnedEntities || !_registry.all_of<Engine::CoreOwnedTag>(child))) {
            hasChildren = true;
            break;
        }
    }

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;

    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    if (_selectedEntity == entity) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (_revealSelectedEntity &&
        hasChildren &&
        IsAncestorOf(_registry, entity, _selectedEntity)) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    ImGui::PushID((int)entt::to_integral(entity));
    if (disabled) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.45f, 0.52f, 0.48f, 1.0f });
    }
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (disabled) {
        ImGui::PopStyleColor();
    }

    if (_revealSelectedEntity && _selectedEntity == entity) {
        ImGui::SetScrollHereY(0.5f);
        _revealSelectedEntity = false;
    }

    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        !ImGui::IsItemToggledOpen() &&
        ImGui::GetDragDropPayload() == nullptr) {
        SetSelectedEntity(entity);
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        const uint32_t entityId = static_cast<uint32_t>(entt::to_integral(entity));
        ImGui::SetDragDropPayload("REGISTRY_ENTITY", &entityId, sizeof(entityId));
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REGISTRY_ENTITY")) {
            const auto entityId = *static_cast<const uint32_t*>(payload->Data);
            const auto child = static_cast<entt::entity>(entityId);
            if (child != entity &&
                child != entt::null &&
                _registry.valid(child) &&
                _registry.all_of<Transform>(child)) {
                if (ImGui::GetIO().KeyShift) {
                    Engine::SetHierarchyParentOrganizational(_registry, child, entity);
                }
                else {
                    Engine::SetHierarchyParent(_registry, child, entity, true);
                }

                SetSelectedEntity(child);
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (hasChildren && open) {
        for (auto child : hierarchyView) {
            const auto& hierarchy = hierarchyView.get<HierarchyComponent>(child);
            if (hierarchy.parent == entity &&
                (_showCoreOwnedEntities || !_registry.all_of<Engine::CoreOwnedTag>(child))) {
                DrawEntityNode(child);
            }
        }

        ImGui::TreePop();
    }

    ImGui::PopID();
}
