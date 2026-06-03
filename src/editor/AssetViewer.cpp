#include "AssetViewer.h"

#include <ImGuiWindowRegistry.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "MeshComponent.h"
#include "WorldSerializer.h"

#include <algorithm>

void AssetViewer::Update(float deltaTime)
{
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

        if (!_statusText.empty())
        {
            ImGui::Separator();
            ImGui::TextWrapped("%s", _statusText.c_str());
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
}

void AssetViewer::RefreshAssetList()
{
    _assetFiles.clear();

    if (!_core) {
        _statusText = "Asset viewer has no core.";
        return;
    }

    const auto assetsPath = _core->ResolveProjectPath("assets");

    if (!std::filesystem::exists(assetsPath)) {
        _statusText = "Assets folder not found.";
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

    _statusText = "Found " + std::to_string(_assetFiles.size()) + " assets.";
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
        _statusText = "Failed to load " + key;
        return nullptr;
    }

    auto [inserted, wasInserted] = _loadedMeshes.emplace(key, std::move(meshes.value()));
    _statusText = "Loaded " + std::to_string(inserted->second.size()) + " meshes from " + key;
    return &inserted->second;
}

void AssetViewer::AssignMeshToSelectedEntity(const std::shared_ptr<MeshAsset>& mesh)
{
    if (!_registryViewerPtr || !mesh) {
        return;
    }

    auto selectedEntity = _registryViewerPtr->GetSelectedEntity();

    if (selectedEntity == entt::null || !_registry.valid(selectedEntity)) {
        _statusText = "No entity selected.";
        return;
    }

    auto& meshComponent = _registry.get_or_emplace<MeshComponent>(selectedEntity);
    meshComponent.mesh = mesh;
    meshComponent.source = mesh->source;

    _statusText = "Assigned mesh " +
        (mesh->name.empty() ? std::to_string(mesh->source.meshIndex) : mesh->name) +
        " to selected entity.";
}

void AssetViewer::LoadSelectedWorld()
{
    if (!_core || _selectedAssetFile.empty()) {
        return;
    }

    Engine::WorldSerializer serializer;
    const auto worldPath = _core->ResolveProjectPath(_selectedAssetFile);

    if (serializer.LoadWorld(*_core, worldPath)) {
        _statusText = "Loaded world " + _selectedAssetFile;
    }
    else {
        _statusText = "Failed to load world " + _selectedAssetFile;
    }
}
