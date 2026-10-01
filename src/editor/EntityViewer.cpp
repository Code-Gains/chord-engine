#include "EntityViewer.h"
#include <ImGuiWindowRegistry.h>
#include "Camera.h"
#include "Core.h"
#include "NameComponent.h"
#include "PrefabComponents.h"
#include "SunlightComponent.h"
#include "Transform.h"
#include "MeshComponent.h"
#include "GravityComponents.h"
#include "HierarchyComponent.h"
#include "EntityState.h"
#include "EnvironmentComponent.h"
#include "LineComponent.h"

#include <algorithm>
#include <cctype>
#include <glm/gtx/quaternion.hpp>
#include <string>
#include <string_view>

namespace {

bool MatchesComponentSearch(std::string_view candidate, std::string_view query)
{
    if (query.empty()) {
        return true;
    }

    size_t queryIndex = 0;
    for (const unsigned char character : candidate) {
        if (queryIndex < query.size() &&
            std::tolower(character) ==
                std::tolower(static_cast<unsigned char>(query[queryIndex]))) {
            ++queryIndex;
        }
    }

    return queryIndex == query.size();
}

std::string DefaultComponentCategory(std::string_view label)
{
    if (label == "Name" || label == "Transform") {
        return "Core";
    }
    if (label == "Single Render Tag" ||
        label == "Prefab Placement Anchor" ||
        label == "Active Camera") {
        return "Tags";
    }
    if (label == "Environment" ||
        label == "Sunlight" ||
        label == "Screen Post Process" ||
        label == "Screen Post Process Source" ||
        label == "Camera" ||
        label == "Cinematic Camera Shot" ||
        label == "Mesh" ||
        label == "Mesh Corruption" ||
        label == "Effect Mesh") {
        return "Rendering";
    }
    if (label == "Velocity" ||
        label == "Gravity Body" ||
        label == "Gravity Particle" ||
        label == "Jolt Collider" ||
        label.ends_with("Collider") ||
        label.ends_with("Collider Root")) {
        return "Physics";
    }
    return "Gameplay";
}

bool SerializedComponentsEqual(
    const Engine::Serialization::SerializedEntity& left,
    const Engine::Serialization::SerializedEntity& right)
{
    if (left.id != right.id || left.components.size() != right.components.size()) {
        return false;
    }

    for (size_t index = 0; index < left.components.size(); ++index) {
        if (left.components[index].type != right.components[index].type ||
            left.components[index].data != right.components[index].data) {
            return false;
        }
    }
    return true;
}

bool SerializedStatesEqual(
    const std::vector<Engine::Serialization::SerializedEntity>& left,
    const std::vector<Engine::Serialization::SerializedEntity>& right)
{
    if (left.size() != right.size()) {
        return false;
    }

    for (size_t index = 0; index < left.size(); ++index) {
        if (!SerializedComponentsEqual(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

void AddUniqueEntity(std::vector<entt::entity>& entities, entt::entity entity)
{
    if (entity != entt::null &&
        std::find(entities.begin(), entities.end(), entity) == entities.end()) {
        entities.push_back(entity);
    }
}

std::vector<entt::entity> CollectTrackedEntities(
    entt::registry& registry,
    entt::entity selectedEntity)
{
    std::vector<entt::entity> entities;
    AddUniqueEntity(entities, selectedEntity);

    auto activeCameraView = registry.view<ActiveCameraTag>(
        entt::exclude<Engine::CoreOwnedTag>);
    for (const entt::entity entity : activeCameraView) {
        AddUniqueEntity(entities, entity);
    }
    return entities;
}

std::vector<Engine::Serialization::SerializedEntity> CaptureEntityStates(
    Engine::WorldSerializer& serializer,
    Engine::Core& core,
    const std::vector<entt::entity>& entities)
{
    std::vector<Engine::Serialization::SerializedEntity> snapshots;
    snapshots.reserve(entities.size());
    for (const entt::entity entity : entities) {
        if (auto snapshot = serializer.SerializeEntity(core, entity)) {
            snapshots.push_back(std::move(*snapshot));
        }
    }
    return snapshots;
}

const Engine::Serialization::SerializedEntity* FindSerializedEntity(
    const std::vector<Engine::Serialization::SerializedEntity>& states,
    entt::entity entity)
{
    const uint64_t id = static_cast<uint64_t>(entt::to_integral(entity));
    const auto found = std::find_if(states.begin(), states.end(), [&](const auto& state) {
        return state.id == id;
    });
    return found == states.end() ? nullptr : &*found;
}

const Engine::Serialization::SerializedComponent* FindSerializedComponent(
    const Engine::Serialization::SerializedEntity* entity,
    std::string_view type)
{
    if (!entity) {
        return nullptr;
    }

    const auto found = std::find_if(
        entity->components.begin(),
        entity->components.end(),
        [&](const auto& component) { return component.type == type; });
    return found == entity->components.end() ? nullptr : &*found;
}

EditorComponentCommand BuildComponentCommand(
    entt::entity selectedEntity,
    const std::vector<Engine::Serialization::SerializedEntity>& before,
    const std::vector<Engine::Serialization::SerializedEntity>& after,
    std::string label)
{
    EditorComponentCommand command;
    command.selectedEntity = selectedEntity;
    command.label = std::move(label);

    for (const auto& beforeEntity : before) {
        const entt::entity entity = static_cast<entt::entity>(beforeEntity.id);
        const auto* afterEntity = FindSerializedEntity(after, entity);
        for (const auto& beforeComponent : beforeEntity.components) {
            const auto* afterComponent = FindSerializedComponent(
                afterEntity,
                beforeComponent.type);
            if (!afterComponent || beforeComponent.data != afterComponent->data) {
                command.deltas.push_back(EditorComponentDelta {
                    entity,
                    beforeComponent.type,
                    beforeComponent,
                    afterComponent
                        ? std::optional<Engine::Serialization::SerializedComponent>(*afterComponent)
                        : std::nullopt
                });
            }
        }
    }

    for (const auto& afterEntity : after) {
        const entt::entity entity = static_cast<entt::entity>(afterEntity.id);
        const auto* beforeEntity = FindSerializedEntity(before, entity);
        for (const auto& afterComponent : afterEntity.components) {
            if (!FindSerializedComponent(beforeEntity, afterComponent.type)) {
                command.deltas.push_back(EditorComponentDelta {
                    entity,
                    afterComponent.type,
                    std::nullopt,
                    afterComponent
                });
            }
        }
    }

    return command;
}

std::string ComponentEditLabel(
    entt::registry& registry,
    entt::entity selectedEntity,
    const std::vector<Engine::Serialization::SerializedEntity>& before,
    const std::vector<Engine::Serialization::SerializedEntity>& after)
{
    const auto* beforeEntity = FindSerializedEntity(before, selectedEntity);
    const auto* afterEntity = FindSerializedEntity(after, selectedEntity);
    if (beforeEntity && afterEntity) {
        for (const auto& component : afterEntity->components) {
            const bool existed = std::any_of(
                beforeEntity->components.begin(),
                beforeEntity->components.end(),
                [&](const auto& previous) { return previous.type == component.type; });
            if (!existed) {
                return "Add " + component.type;
            }
        }

        for (const auto& component : beforeEntity->components) {
            const bool remains = std::any_of(
                afterEntity->components.begin(),
                afterEntity->components.end(),
                [&](const auto& current) { return current.type == component.type; });
            if (!remains) {
                return "Remove " + component.type;
            }
        }
    }

    const std::string entityName = registry.all_of<NameComponent>(selectedEntity)
        ? registry.get<NameComponent>(selectedEntity).name
        : std::string("entity");
    return "Edit " + entityName;
}

glm::vec3 CatmullRom(
    const glm::vec3& p0,
    const glm::vec3& p1,
    const glm::vec3& p2,
    const glm::vec3& p3,
    float value)
{
    const float t = std::clamp(value, 0.0f, 1.0f);
    const float t2 = t * t;
    const float t3 = t2 * t;

    return 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * t +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
    );
}

float SmoothStep(float value)
{
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool IsClosedCameraShotPath(const CinematicCameraShotComponent& shot)
{
    if (!shot.loop || shot.keyframes.size() < 4) {
        return false;
    }

    const glm::vec3 delta =
        shot.keyframes.front().position - shot.keyframes.back().position;
    return glm::dot(delta, delta) < 0.0001f;
}

std::size_t PreviousControlIndex(
    const CinematicCameraShotComponent& shot,
    std::size_t previousIndex,
    bool closedPath)
{
    if (previousIndex > 0) {
        return previousIndex - 1;
    }

    return closedPath ? shot.keyframes.size() - 2 : previousIndex;
}

std::size_t NextControlIndex(
    const CinematicCameraShotComponent& shot,
    std::size_t nextIndex,
    bool closedPath)
{
    if (nextIndex + 1 < shot.keyframes.size()) {
        return nextIndex + 1;
    }

    return closedPath ? 1 : nextIndex;
}

glm::vec3 EvaluateCameraShotPosition(
    const CinematicCameraShotComponent& shot,
    std::size_t nextIndex,
    float value)
{
    const auto& keyframes = shot.keyframes;
    const std::size_t previousIndex = nextIndex - 1;
    const auto& previous = keyframes[previousIndex];
    const auto& next = keyframes[nextIndex];
    const float linearT = std::clamp(value, 0.0f, 1.0f);

    if (next.interpolationMode == CameraShotInterpolationMode::CatmullRom) {
        const bool closedPath = IsClosedCameraShotPath(shot);
        const std::size_t firstIndex = PreviousControlIndex(shot, previousIndex, closedPath);
        const std::size_t lastIndex = NextControlIndex(shot, nextIndex, closedPath);

        return CatmullRom(
            keyframes[firstIndex].position,
            previous.position,
            next.position,
            keyframes[lastIndex].position,
            linearT);
    }

    const float segmentT = next.interpolationMode == CameraShotInterpolationMode::Smoothstep
        ? SmoothStep(linearT)
        : linearT;

    return glm::mix(previous.position, next.position, segmentT);
}

void AddDebugLine(
    entt::registry& registry,
    const glm::vec3& start,
    const glm::vec3& end,
    const glm::vec4& color)
{
    auto entity = registry.create();
    registry.emplace<LineComponent>(
        entity,
        start,
        end,
        color,
        0.0f,
        false);
    registry.emplace<Engine::CoreOwnedTag>(entity);
}

void AddKeyframeMarker(
    entt::registry& registry,
    const CameraShotKeyframe& keyframe,
    float size,
    const glm::vec4& color)
{
    const glm::vec3 position = keyframe.position;
    AddDebugLine(registry, position - glm::vec3{ size, 0.0f, 0.0f }, position + glm::vec3{ size, 0.0f, 0.0f }, color);
    AddDebugLine(registry, position - glm::vec3{ 0.0f, size, 0.0f }, position + glm::vec3{ 0.0f, size, 0.0f }, color);
    AddDebugLine(registry, position - glm::vec3{ 0.0f, 0.0f, size }, position + glm::vec3{ 0.0f, 0.0f, size }, color);

    const glm::vec3 forward =
        glm::normalize(keyframe.rotation * glm::vec3{ 0.0f, 0.0f, -1.0f });
    AddDebugLine(
        registry,
        position,
        position + forward * size * 2.0f,
        glm::vec4{ 0.25f, 0.55f, 1.0f, 1.0f });
}

float ResolveMarkerSize(const CinematicCameraShotComponent& shot)
{
    if (shot.keyframes.empty()) {
        return 0.25f;
    }

    glm::vec3 minPosition = shot.keyframes.front().position;
    glm::vec3 maxPosition = shot.keyframes.front().position;

    for (const auto& keyframe : shot.keyframes) {
        minPosition = glm::min(minPosition, keyframe.position);
        maxPosition = glm::max(maxPosition, keyframe.position);
    }

    return std::max(0.25f, glm::length(maxPosition - minPosition) * 0.015f);
}

void DrawCameraShotPath(entt::registry& registry, const CinematicCameraShotComponent& shot)
{
    if (!shot.showPath || shot.keyframes.empty()) {
        return;
    }

    constexpr glm::vec4 pathColor{ 1.0f, 0.78f, 0.2f, 1.0f };
    constexpr glm::vec4 firstColor{ 0.2f, 1.0f, 0.45f, 1.0f };
    constexpr glm::vec4 middleColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    constexpr glm::vec4 lastColor{ 1.0f, 0.25f, 0.85f, 1.0f };
    const float markerSize = ResolveMarkerSize(shot);

    for (std::size_t index = 0; index < shot.keyframes.size(); ++index) {
        const glm::vec4 color = index == 0
            ? firstColor
            : (index + 1 == shot.keyframes.size() ? lastColor : middleColor);
        AddKeyframeMarker(registry, shot.keyframes[index], markerSize, color);
    }

    if (shot.keyframes.size() < 2) {
        return;
    }

    constexpr int samplesPerSegment = 16;
    for (std::size_t nextIndex = 1; nextIndex < shot.keyframes.size(); ++nextIndex) {
        glm::vec3 previousPosition = shot.keyframes[nextIndex - 1].position;
        for (int sample = 1; sample <= samplesPerSegment; ++sample) {
            const float t = static_cast<float>(sample) / static_cast<float>(samplesPerSegment);
            const glm::vec3 position = EvaluateCameraShotPosition(shot, nextIndex, t);
            AddDebugLine(registry, previousPosition, position, pathColor);
            previousPosition = position;
        }
    }
}

} // namespace

void EntityViewer::Update(float deltaTime)
{
    const auto selectedEntity = _registryViewerPtr->GetSelectedEntity();
    if (selectedEntity == entt::null ||
        !_registry.valid(selectedEntity) ||
        !_registry.all_of<CinematicCameraShotComponent>(selectedEntity)) {
        return;
    }

    DrawCameraShotPath(
        _registry,
        _registry.get<CinematicCameraShotComponent>(selectedEntity));
}

void EntityViewer::DrawUi()
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();

    if (!windowRegistry.IsWindowOpen("Entity Viewer"))
        return;

    bool open = true;

    if (ImGui::Begin("Entity Viewer", &open))
    {
        auto& selectedEntity = _registryViewerPtr->GetSelectedEntity();

        if (selectedEntity != entt::null && _registry.valid(selectedEntity))
        {
            auto& history = GetEditorHistory(_registry);
            if (_core) {
                const bool selectionChanged = _componentHistoryEntity != selectedEntity;
                const bool historyChanged = _observedHistoryRevision != history.Revision();
                if (selectionChanged || (historyChanged && !_componentInteractionActive)) {
                    auto serializer = _core->CreateWorldSerializer();
                    _componentHistoryBaseline = CaptureEntityStates(
                        serializer,
                        *_core,
                        CollectTrackedEntities(_registry, selectedEntity));
                    _componentHistoryEntity = selectedEntity;
                    _observedHistoryRevision = history.Revision();
                    _componentInteractionActive = false;
                }
            }

            bool enabled = !_registry.all_of<DisabledEntityTag>(selectedEntity);
            if (ImGui::Checkbox("Enabled", &enabled)) {
                if (enabled) {
                    _registry.remove<DisabledEntityTag>(selectedEntity);
                }
                else {
                    _registry.emplace_or_replace<DisabledEntityTag>(selectedEntity);
                }
            }

            if (ImGui::Button("+ Component")) {
                _componentSearchBuffer.fill('\0');
                _componentPickerSelection = 0;
                ImGui::OpenPopup("AddComponentPopup");
            }

            DrawAddComponentPopup(selectedEntity);

            ImGui::Separator();

            for (auto& ui : _componentUis)
            {
                ui->Draw(_registry, selectedEntity);
            }

            if (_core) {
                const bool popupOpen = ImGui::IsPopupOpen(
                    nullptr,
                    ImGuiPopupFlags_AnyPopupId);
                const bool itemActive = ImGui::IsAnyItemActive();
                const bool clickedInViewer =
                    ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                    (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                        ImGui::IsMouseClicked(ImGuiMouseButton_Right));

                if (itemActive || popupOpen || clickedInViewer) {
                    _componentInteractionActive = true;
                }
                else if (_componentInteractionActive) {
                    auto serializer = _core->CreateWorldSerializer();
                    std::vector<entt::entity> trackedEntities;
                    trackedEntities.reserve(_componentHistoryBaseline.size() + 2);
                    for (const auto& entity : _componentHistoryBaseline) {
                        AddUniqueEntity(
                            trackedEntities,
                            static_cast<entt::entity>(entity.id));
                    }
                    for (const entt::entity entity :
                        CollectTrackedEntities(_registry, selectedEntity)) {
                        AddUniqueEntity(trackedEntities, entity);
                    }

                    auto afterStates = CaptureEntityStates(
                        serializer,
                        *_core,
                        trackedEntities);
                    if (!SerializedStatesEqual(_componentHistoryBaseline, afterStates)) {
                        history.PushComponents(BuildComponentCommand(
                            selectedEntity,
                            _componentHistoryBaseline,
                            afterStates,
                            ComponentEditLabel(
                                _registry,
                                selectedEntity,
                                _componentHistoryBaseline,
                                afterStates)));
                    }

                    _componentHistoryBaseline = std::move(afterStates);
                    _componentHistoryEntity = selectedEntity;
                    _observedHistoryRevision = history.Revision();
                    _componentInteractionActive = false;
                }
            }
        }
    }

    ImGui::End();

    windowRegistry.SetWindowOpen("Entity Viewer", open);
}

void EntityViewer::DrawAddComponentPopup(entt::entity selectedEntity)
{
    ImGui::SetNextWindowSize(ImVec2(420.0f, 460.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("AddComponentPopup")) {
        return;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }

    const std::string previousSearch = _componentSearchBuffer.data();
    ImGui::SetNextItemWidth(-1.0f);
    const bool submitSearch = ImGui::InputTextWithHint(
        "##ComponentSearch",
        "Search components...",
        _componentSearchBuffer.data(),
        _componentSearchBuffer.size(),
        ImGuiInputTextFlags_EnterReturnsTrue);
    const std::string search = _componentSearchBuffer.data();
    if (search != previousSearch) {
        _componentPickerSelection = 0;
    }

    std::vector<const ComponentMenuEntry*> availableEntries;
    availableEntries.reserve(_componentMenuEntries.size());
    for (const auto& entry : _componentMenuEntries) {
        if (entry.canAdd(_registry, selectedEntity) &&
            MatchesComponentSearch(entry.label, search)) {
            availableEntries.push_back(&entry);
        }
    }

    if (search.empty()) {
        std::vector<std::string_view> categories;
        for (const auto* entry : availableEntries) {
            if (std::find(categories.begin(), categories.end(), entry->category) ==
                categories.end()) {
                categories.emplace_back(entry->category);
            }
        }

        std::vector<const ComponentMenuEntry*> categorizedEntries;
        categorizedEntries.reserve(availableEntries.size());
        for (const std::string_view category : categories) {
            for (const auto* entry : availableEntries) {
                if (entry->category == category) {
                    categorizedEntries.push_back(entry);
                }
            }
        }
        availableEntries = std::move(categorizedEntries);
    }

    if (availableEntries.empty()) {
        _componentPickerSelection = 0;
    }
    else {
        _componentPickerSelection = std::clamp(
            _componentPickerSelection,
            0,
            static_cast<int>(availableEntries.size()) - 1);

        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            _componentPickerSelection = std::min(
                _componentPickerSelection + 1,
                static_cast<int>(availableEntries.size()) - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            _componentPickerSelection = std::max(_componentPickerSelection - 1, 0);
        }
    }

    const auto addEntry = [&](const ComponentMenuEntry& entry) {
        entry.add(_registry, selectedEntity);
        ImGui::CloseCurrentPopup();
    };

    if (submitSearch && !availableEntries.empty()) {
        addEntry(*availableEntries[_componentPickerSelection]);
        ImGui::EndPopup();
        return;
    }

    ImGui::Separator();
    ImGui::BeginChild(
        "ComponentPickerResults",
        ImVec2(0.0f, 0.0f),
        false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (availableEntries.empty()) {
        ImGui::TextDisabled(search.empty()
            ? "All available components have been added."
            : "No matching components.");
    }
    else if (!search.empty()) {
        for (size_t index = 0; index < availableEntries.size(); ++index) {
            const auto& entry = *availableEntries[index];
            const bool selected = static_cast<int>(index) == _componentPickerSelection;
            if (ImGui::Selectable(entry.label.c_str(), selected)) {
                addEntry(entry);
                break;
            }
            if (selected &&
                (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false) ||
                    ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))) {
                ImGui::SetScrollHereY(0.5f);
            }
        }
    }
    else {
        std::vector<std::string_view> categories;
        for (const auto* entry : availableEntries) {
            if (std::find(categories.begin(), categories.end(), entry->category) ==
                categories.end()) {
                categories.emplace_back(entry->category);
            }
        }

        size_t flatIndex = 0;
        for (const std::string_view category : categories) {
            ImGui::SeparatorText(category.data());
            for (const auto* entry : availableEntries) {
                if (entry->category != category) {
                    continue;
                }

                const bool selected = static_cast<int>(flatIndex) == _componentPickerSelection;
                if (ImGui::Selectable(entry->label.c_str(), selected)) {
                    addEntry(*entry);
                    break;
                }
                if (selected &&
                    (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))) {
                    ImGui::SetScrollHereY(0.5f);
                }
                ++flatIndex;
            }
        }
    }

    ImGui::EndChild();
    ImGui::EndPopup();
}

EntityViewer::EntityViewer(entt::registry &registry, RegistryViewer* registryViewerPtr, Engine::Core* core)
    : System(registry)
    , _registryViewerPtr(registryViewerPtr)
    , _core(core)
{
    _componentUis.push_back(std::make_unique<NameComponentUi>());
    _componentUis.push_back(std::make_unique<TransformComponentUi>());
    _componentUis.push_back(std::make_unique<HierarchyComponentUi>());
    _componentUis.push_back(std::make_unique<EnvironmentComponentUi>());
    _componentUis.push_back(std::make_unique<SunlightComponentUI>());
    _componentUis.push_back(std::make_unique<ScreenPostProcessComponentUi>());
    _componentUis.push_back(std::make_unique<ScreenPostProcessSourceComponentUi>());
    _componentUis.push_back(std::make_unique<CameraComponentUi>());
    _componentUis.push_back(std::make_unique<CinematicCameraShotComponentUi>());
    _componentUis.push_back(std::make_unique<MeshComponentUi>(core));
    _componentUis.push_back(std::make_unique<MeshCorruptionComponentUi>());
    _componentUis.push_back(std::make_unique<EffectMeshComponentUi>());
    _componentUis.push_back(std::make_unique<SingleRenderTagUi>());
    _componentUis.push_back(std::make_unique<PrefabPlacementAnchorComponentUi>());
    _componentUis.push_back(std::make_unique<ActiveCameraTagUi>());
    _componentUis.push_back(std::make_unique<VelocityComponentUi>());
    _componentUis.push_back(std::make_unique<GravityBodyComponentUi>());
    _componentUis.push_back(std::make_unique<GravityParticleComponentUi>());
    _componentUis.push_back(std::make_unique<JoltColliderComponentUi>());

    AddComponentMenuItem(
        "Name",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<NameComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<NameComponent>(
                entity,
                "Entity " + std::to_string((int)entt::to_integral(entity))
            );
        }
    );

    AddComponentMenuItem(
        "Transform",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<Transform>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<Transform>(entity);
        }
    );

    AddComponentMenuItem(
        "Environment",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<EnvironmentComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<EnvironmentComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Sunlight",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<SunlightComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<SunlightComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Screen Post Process",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<ScreenPostProcessComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<ScreenPostProcessComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Screen Post Process Source",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<MeshComponent>(entity) &&
                   !registry.all_of<ScreenPostProcessSourceComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<ScreenPostProcessSourceComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Camera",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<Camera>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<Camera>(entity);

            auto activeCameraView =
                registry.view<ActiveCameraTag>(entt::exclude<Engine::CoreOwnedTag>);

            if (activeCameraView.begin() == activeCameraView.end()) {
                registry.emplace<ActiveCameraTag>(entity);
            }
        }
    );

    AddComponentMenuItem(
        "Cinematic Camera Shot",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<Camera, Transform>(entity) &&
                   !registry.all_of<CinematicCameraShotComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<CinematicCameraShotComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Single Render Tag",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<SingleRenderTag>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<SingleRenderTag>(entity);
        }
    );

    AddComponentMenuItem(
        "Prefab Placement Anchor",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<Transform>(entity) &&
                !registry.all_of<PrefabPlacementAnchorComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<PrefabPlacementAnchorComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Active Camera",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<Camera>(entity) &&
                   !registry.all_of<ActiveCameraTag>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            auto activeCameraView =
                registry.view<ActiveCameraTag>(entt::exclude<Engine::CoreOwnedTag>);

            for (auto activeCameraEntity : activeCameraView) {
                registry.remove<ActiveCameraTag>(activeCameraEntity);
            }

            registry.emplace<ActiveCameraTag>(entity);
        }
    );

    AddComponentMenuItem(
        "Mesh",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<MeshComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<MeshComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Mesh Corruption",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<MeshComponent>(entity) &&
                   !registry.all_of<MeshCorruptionComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<MeshCorruptionComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Effect Mesh",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<MeshComponent>(entity) &&
                   !registry.all_of<EffectMeshComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            auto& effect = registry.emplace<EffectMeshComponent>(entity);
            effect.destroyOnComplete = false;
            effect.lifetime = 100000.0f;
        }
    );

    AddComponentMenuItem(
        "Velocity",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<VelocityComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<VelocityComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Gravity Body",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<GravityBodyComponent>(entity) &&
                   !registry.all_of<GravityParticleComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<GravityBodyComponent>(entity);
            if (!registry.all_of<VelocityComponent>(entity)) {
                registry.emplace<VelocityComponent>(entity);
            }
        }
    );

    AddComponentMenuItem(
        "Gravity Particle",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<GravityParticleComponent>(entity) &&
                   !registry.all_of<GravityBodyComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<GravityParticleComponent>(entity);
            if (!registry.all_of<VelocityComponent>(entity)) {
                registry.emplace<VelocityComponent>(entity);
            }
        }
    );

    AddComponentMenuItem(
        "Jolt Collider",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<Transform>(entity) &&
                   !registry.all_of<Engine::JoltColliderComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<Engine::JoltColliderComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Static Box Collider",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<Transform>(entity) &&
                   !registry.all_of<Engine::JoltColliderComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            auto& collider = registry.emplace<Engine::JoltColliderComponent>(entity);
            collider.shape = Engine::JoltColliderShape::Box;
            collider.motion = Engine::JoltBodyMotion::Static;
            collider.halfExtents = glm::vec3{ 0.5f };
        }
    );

    AddComponentMenuItem(
        "Static Capsule Collider",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<Transform>(entity) &&
                   !registry.all_of<Engine::JoltColliderComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            auto& collider = registry.emplace<Engine::JoltColliderComponent>(entity);
            collider.shape = Engine::JoltColliderShape::Capsule;
            collider.motion = Engine::JoltBodyMotion::Static;
            collider.radius = 0.5f;
            collider.capsuleHalfHeight = 0.5f;
        }
    );

    AddComponentMenuItem(
        "Static Cylinder Collider",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<Transform>(entity) &&
                   !registry.all_of<Engine::JoltColliderComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            auto& collider = registry.emplace<Engine::JoltColliderComponent>(entity);
            collider.shape = Engine::JoltColliderShape::Cylinder;
            collider.motion = Engine::JoltBodyMotion::Static;
            collider.radius = 0.5f;
            collider.cylinderHalfHeight = 0.5f;
        }
    );

    AddComponentMenuItem(
        "Static Cone Collider",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<Transform>(entity) &&
                   !registry.all_of<Engine::JoltColliderComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            auto& collider = registry.emplace<Engine::JoltColliderComponent>(entity);
            collider.shape = Engine::JoltColliderShape::Cone;
            collider.motion = Engine::JoltBodyMotion::Static;
            collider.radius = 0.5f;
            collider.coneHalfHeight = 0.5f;
        }
    );

    AddComponentMenuItem(
        "Static Convex Hull Collider",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<Transform, MeshComponent>(entity) &&
                   !registry.all_of<Engine::JoltColliderComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            auto& collider = registry.emplace<Engine::JoltColliderComponent>(entity);
            collider.shape = Engine::JoltColliderShape::ConvexHull;
            collider.motion = Engine::JoltBodyMotion::Static;
            const auto& mesh = registry.get<MeshComponent>(entity);
            if (mesh.mesh && !mesh.mesh->pickingPositions.empty()) {
                collider.convexHullPoints = Engine::BuildJoltConvexHullPoints(
                    mesh.mesh->pickingPositions);
            }
        }
    );

    AddComponentMenuItem(
        "Static Compound Collider Root",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<Transform>(entity) &&
                   !registry.all_of<Engine::JoltColliderComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            auto& collider = registry.emplace<Engine::JoltColliderComponent>(entity);
            collider.shape = Engine::JoltColliderShape::Compound;
            collider.motion = Engine::JoltBodyMotion::Static;
        }
    );

    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();

    windowRegistry.RegisterWindow(
        "Entity Viewer",
        true
    );
}

void EntityViewer::AddComponentUi(std::unique_ptr<ViewerComponentUi> componentUi)
{
    _componentUis.push_back(std::move(componentUi));
}

void EntityViewer::AddComponentMenuItem(
    std::string label,
    std::function<bool(entt::registry&, entt::entity)> canAdd,
    std::function<void(entt::registry&, entt::entity)> add,
    std::string category)
{
    if (category.empty()) {
        category = DefaultComponentCategory(label);
    }

    _componentMenuEntries.push_back(ComponentMenuEntry {
        std::move(label),
        std::move(category),
        std::move(canAdd),
        std::move(add)
    });
}
