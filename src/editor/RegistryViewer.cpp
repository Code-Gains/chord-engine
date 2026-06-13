#include "RegistryViewer.h"
#include <string>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "Core.h"
#include "EditorSelection.h"
#include "InputSystem.h"
#include "NameComponent.h"
#include "HierarchyComponent.h"
#include "WorldSerializer.h"
#include <ImGuiWindowRegistry.h>
#include <cctype>
#include <vector>

namespace {
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

    void DeleteSelectedEntity(entt::registry& registry, entt::entity& selectedEntity)
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

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
        !ImGui::GetIO().WantTextInput &&
        !ImGui::IsAnyItemActive())
    {
        DeleteSelectedEntity(_registry, _selectedEntity);
    }

    const ImGuiIO& io = ImGui::GetIO();
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
        }

        ImGui::SameLine();

        const bool canDeleteSelectedEntity = CanDeleteSelectedEntity(_registry, _selectedEntity);

        if (!canDeleteSelectedEntity) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("- Entity"))
        {
            DeleteSelectedEntity(_registry, _selectedEntity);
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
    _copiedEntity = serializer.SerializeEntity(*_core, _selectedEntity);
    return _copiedEntity.has_value();
}

bool RegistryViewer::PasteCopiedEntity()
{
    if (!CanPasteEntity()) {
        return false;
    }

    auto serializer = _core->CreateWorldSerializer();
    const entt::entity pastedEntity = serializer.InstantiateEntity(*_core, _copiedEntity.value());
    if (_registry.valid(pastedEntity)) {
        if (auto* name = _registry.try_get<NameComponent>(pastedEntity)) {
            name->name = MakeUniqueCopyName(_registry, name->name);
        }
    }

    SetSelectedEntity(pastedEntity);
    return _registry.valid(pastedEntity);
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

    ImGui::PushID((int)entt::to_integral(entity));
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        SetSelectedEntity(entity);
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
