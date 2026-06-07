#include "AssetViewer.h"

#include <ImGuiWindowRegistry.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "MeshComponent.h"
#include "WorldSerializer.h"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <utility>

void AssetViewer::Update(float deltaTime)
{
    if (_statusTimer > 0.0f) {
        _statusTimer = std::max(0.0f, _statusTimer - deltaTime);
    }
}

void AssetViewer::DrawUi()
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();

    if (!windowRegistry.IsWindowOpen("Asset Viewer"))
        return;

    bool open = true;

    if (ImGui::Begin("Asset Viewer", &open))
    {
        if (ImGui::Button("Refresh")) {
            RefreshAssetList();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Prefab");
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText(
            "Path##SavePrefabPath",
            _prefabPathBuffer.data(),
            _prefabPathBuffer.size()
        );
        if (ImGui::IsItemEdited()) {
            _overwritePrefabConfirmationActive = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Selected")) {
            SaveSelectedEntityAsPrefab();
        }

        if (_overwritePrefabConfirmationActive) {
            ImGui::SameLine();
            if (ImGui::Button("Overwrite Prefab")) {
                SaveSelectedEntityAsPrefab(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel##CancelPrefabOverwrite")) {
                _overwritePrefabConfirmationActive = false;
                SetStatus("Prefab overwrite cancelled.", true);
            }
        }

        if ((_statusTimer > 0.0f || !_statusSucceeded) && !_statusText.empty()) {
            const ImVec4 color = _statusSucceeded
                ? ImVec4{ 0.35f, 0.85f, 0.45f, 1.0f }
                : ImVec4{ 1.0f, 0.35f, 0.25f, 1.0f };
            ImGui::TextColored(color, "%s", _statusText.c_str());
        }

        ImGui::Separator();

        ImGui::BeginChild(
            "AssetFileList",
            ImVec2(420.0f, 0.0f),
            true,
            ImGuiWindowFlags_HorizontalScrollbar
        );

        for (const auto& file : _assetFiles)
        {
            const std::string projectPath = file.projectPath.generic_string();
            ImGui::PushID(projectPath.c_str());

            if (ImGui::Selectable(file.displayName.c_str(), _selectedAssetFile == projectPath))
            {
                _selectedAssetFile = projectPath;
                _selectedAssetKind = file.kind;

                if (file.kind == AssetKind::Mesh) {
                    GetOrLoadMeshes(file.projectPath);
                }
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("AssetMeshList", ImVec2(0.0f, 0.0f), true);

        if (_selectedAssetFile.empty())
        {
            ImGui::TextUnformatted("Select an asset.");
        }
        else if (_selectedAssetKind == AssetKind::Mesh)
        {
            ImGui::Text("Mesh: %s", _selectedAssetFile.c_str());
            ImGui::Separator();

            auto* meshes = GetOrLoadMeshes(_selectedAssetFile);

            if (meshes)
            {
                for (size_t meshIndex = 0; meshIndex < meshes->size(); meshIndex++)
                {
                    const auto& mesh = meshes->at(meshIndex);
                    std::string meshName = mesh->name.empty()
                        ? "Mesh " + std::to_string(meshIndex)
                        : mesh->name;

                    ImGui::PushID(static_cast<int>(meshIndex));

                    ImGui::Text("%s", meshName.c_str());
                    ImGui::SameLine();

                    if (ImGui::Button("Assign")) {
                        AssignMeshToSelectedEntity(mesh);
                    }

                    ImGui::PopID();
                }
            }
        }
        else if (_selectedAssetKind == AssetKind::World)
        {
            ImGui::Text("World: %s", _selectedAssetFile.c_str());
            ImGui::Separator();

            if (ImGui::Button("Load World")) {
                LoadSelectedWorld();
            }
        }
        else if (_selectedAssetKind == AssetKind::Prefab)
        {
            ImGui::Text("Prefab: %s", _selectedAssetFile.c_str());
            ImGui::Separator();

            if (ImGui::Button("Instantiate")) {
                InstantiateSelectedPrefab();
            }
            ImGui::SameLine();
            if (ImGui::Button("Use For Save Path")) {
                SetPrefabPathBuffer(_selectedAssetFile);
                _overwritePrefabConfirmationActive = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy Path")) {
                ImGui::SetClipboardText(_selectedAssetFile.c_str());
                SetStatus("Copied prefab path " + _selectedAssetFile, true);
            }
        }

        ImGui::EndChild();
    }

    ImGui::End();

    windowRegistry.SetWindowOpen("Asset Viewer", open);
}

AssetViewer::AssetViewer(entt::registry& registry, Engine::Core* core, RegistryViewer* registryViewerPtr)
    : System(registry),
      _core(core),
      _registryViewerPtr(registryViewerPtr)
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();

    windowRegistry.RegisterWindow(
        "Asset Viewer",
        true
    );

    RefreshAssetList();
    constexpr std::string_view defaultPrefabPath = "assets/prefabs/new_prefab.json";
    SetPrefabPathBuffer(defaultPrefabPath.data());
}

void AssetViewer::RefreshAssetList(bool updateStatus)
{
    _assetFiles.clear();

    if (!_core) {
        if (updateStatus) {
            SetStatus("Asset viewer has no core.", false);
        }
        return;
    }

    const auto assetsPath = _core->ResolveProjectPath("assets");

    if (!std::filesystem::exists(assetsPath)) {
        if (updateStatus) {
            SetStatus("Assets folder not found.", false);
        }
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsPath))
    {
        if (!entry.is_regular_file())
            continue;

        const auto extension = entry.path().extension().string();
        const auto projectPath = _core->MakeProjectRelative(entry.path());
        const auto projectPathString = projectPath.generic_string();

        AssetKind kind;
        std::string displayPrefix;

        if (extension == ".gltf" || extension == ".glb") {
            kind = AssetKind::Mesh;
            displayPrefix = "[Mesh] ";
        }
        else if (extension == ".json" && projectPathString.starts_with("assets/worlds/")) {
            kind = AssetKind::World;
            displayPrefix = "[World] ";
        }
        else if (extension == ".json" && projectPathString.starts_with("assets/prefabs/")) {
            kind = AssetKind::Prefab;
            displayPrefix = "[Prefab] ";
        }
        else {
            continue;
        }

        _assetFiles.push_back(AssetFileEntry {
            kind,
            projectPath,
            displayPrefix + projectPathString
        });
    }

    std::sort(
        _assetFiles.begin(),
        _assetFiles.end(),
        [](const AssetFileEntry& left, const AssetFileEntry& right) {
            return left.displayName < right.displayName;
        }
    );

    if (updateStatus) {
        SetStatus("Found " + std::to_string(_assetFiles.size()) + " assets.", true);
    }
}

std::vector<std::shared_ptr<MeshAsset>>* AssetViewer::GetOrLoadMeshes(const std::filesystem::path& projectPath)
{
    if (!_core)
        return nullptr;

    const std::string key = projectPath.generic_string();

    auto loaded = _loadedMeshes.find(key);
    if (loaded != _loadedMeshes.end()) {
        return &loaded->second;
    }

    auto meshes = _core->LoadGltfMeshes(_core, projectPath);
    if (!meshes.has_value()) {
        SetStatus("Failed to load " + key, false);
        return nullptr;
    }

    auto [inserted, wasInserted] = _loadedMeshes.emplace(key, std::move(meshes.value()));
    SetStatus("Loaded " + std::to_string(inserted->second.size()) + " meshes from " + key, true);
    return &inserted->second;
}

void AssetViewer::AssignMeshToSelectedEntity(const std::shared_ptr<MeshAsset>& mesh)
{
    if (!_registryViewerPtr || !mesh) {
        return;
    }

    auto selectedEntity = _registryViewerPtr->GetSelectedEntity();

    if (selectedEntity == entt::null || !_registry.valid(selectedEntity)) {
        SetStatus("No entity selected.", false);
        return;
    }

    auto& meshComponent = _registry.get_or_emplace<MeshComponent>(selectedEntity);
    meshComponent.mesh = mesh;
    meshComponent.source = mesh->source;

    SetStatus("Assigned mesh " +
        (mesh->name.empty() ? std::to_string(mesh->source.meshIndex) : mesh->name) +
        " to selected entity.", true);
}

void AssetViewer::LoadSelectedWorld()
{
    if (!_core || _selectedAssetFile.empty()) {
        return;
    }

    auto serializer = _core->CreateWorldSerializer();
    const auto worldPath = _core->ResolveProjectPath(_selectedAssetFile);

    if (serializer.LoadWorld(*_core, worldPath)) {
        _core->SetCurrentWorldPath(_selectedAssetFile);
        SetStatus("Loaded world " + _selectedAssetFile, true);
    }
    else {
        SetStatus("Failed to load world " + _selectedAssetFile, false);
    }
}

void AssetViewer::InstantiateSelectedPrefab()
{
    if (!_core || _selectedAssetFile.empty()) {
        return;
    }

    auto serializer = _core->CreateWorldSerializer();
    const auto prefabPath = _core->ResolveProjectPath(_selectedAssetFile);
    auto entity = serializer.InstantiatePrefab(*_core, prefabPath);

    if (entity.has_value()) {
        if (_registryViewerPtr) {
            _registryViewerPtr->SetSelectedEntity(entity.value());
        }

        SetStatus("Instantiated prefab " + _selectedAssetFile, true);
    }
    else {
        SetStatus("Failed to instantiate prefab " + _selectedAssetFile, false);
    }
}

void AssetViewer::SaveSelectedEntityAsPrefab(bool overwriteConfirmed)
{
    if (!_core || !_registryViewerPtr) {
        return;
    }

    const auto selectedEntity = _registryViewerPtr->GetSelectedEntity();
    if (selectedEntity == entt::null || !_registry.valid(selectedEntity)) {
        SetStatus("No entity selected.", false);
        return;
    }

    const std::filesystem::path projectPath = _prefabPathBuffer.data();
    if (projectPath.empty()) {
        SetStatus("Prefab path is empty.", false);
        return;
    }

    const auto prefabPath = _core->ResolveProjectPath(projectPath);

    if (std::filesystem::exists(prefabPath) && !overwriteConfirmed) {
        _overwritePrefabConfirmationActive = true;
        SetStatus(
            "Prefab already exists. Click Overwrite Prefab to replace " + projectPath.generic_string(),
            false);
        return;
    }

    std::filesystem::create_directories(prefabPath.parent_path());

    auto serializer = _core->CreateWorldSerializer();
    if (serializer.SavePrefab(*_core, selectedEntity, prefabPath)) {
        _overwritePrefabConfirmationActive = false;
        SetStatus("Saved prefab " + projectPath.generic_string(), true);
        RefreshAssetList(false);
    }
    else {
        SetStatus("Failed to save prefab " + projectPath.generic_string(), false);
    }
}

void AssetViewer::SetPrefabPathBuffer(const std::filesystem::path& projectPath)
{
    const auto pathString = projectPath.generic_string();
    std::fill(_prefabPathBuffer.begin(), _prefabPathBuffer.end(), '\0');
    std::copy_n(
        pathString.data(),
        std::min(pathString.size(), _prefabPathBuffer.size() - 1),
        _prefabPathBuffer.data()
    );
}

void AssetViewer::SetStatus(std::string text, bool succeeded)
{
    _statusText = std::move(text);
    _statusSucceeded = succeeded;
    _statusTimer = succeeded ? 2.0f : -1.0f;
}
