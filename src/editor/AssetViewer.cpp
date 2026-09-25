#include "AssetViewer.h"

#include <ImGuiWindowRegistry.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "MeshComponent.h"
#include "EnvironmentComponent.h"
#include "WorldSerializer.h"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <system_error>
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

        if ((_statusTimer > 0.0f || !_statusSucceeded) && !_statusText.empty()) {
            const ImVec4 color = _statusSucceeded
                ? ImVec4{ 0.35f, 0.85f, 0.45f, 1.0f }
                : ImVec4{ 1.0f, 0.35f, 0.25f, 1.0f };
            ImGui::TextColored(color, "%s", _statusText.c_str());
        }

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
                else if (file.kind == AssetKind::Prefab) {
                    SetPrefabPathBuffer(file.projectPath);
                    _overwritePrefabConfirmationActive = false;
                }
                else if (file.kind == AssetKind::SoundCue) {
                    LoadSelectedSoundCue();
                }
                else {
                    _soundCueLoaded = false;
                    _soundCueDirty = false;
                }
            }

            if (file.kind == AssetKind::Skybox && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(
                    "ENGINE_SKYBOX_ASSET",
                    projectPath.c_str(),
                    projectPath.size() + 1);
                ImGui::TextUnformatted(projectPath.c_str());
                ImGui::EndDragDropSource();
            }

            if (file.kind == AssetKind::AudioClip && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(
                    "ENGINE_AUDIO_CLIP_ASSET",
                    projectPath.c_str(),
                    projectPath.size() + 1);
                ImGui::TextUnformatted(projectPath.c_str());
                ImGui::EndDragDropSource();
            }

            if (file.kind == AssetKind::SoundCue && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(
                    "ENGINE_SOUND_CUE_ASSET",
                    projectPath.c_str(),
                    projectPath.size() + 1);
                ImGui::TextUnformatted(projectPath.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginPopupContextItem("AssetFileContextMenu")) {
                if (file.kind != AssetKind::Skybox && ImGui::MenuItem("Delete File")) {
                    RequestDeleteAsset(file.projectPath);
                }
                ImGui::EndPopup();
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
            if (ImGui::Button("Delete File##DeleteSelectedMeshAsset")) {
                RequestDeleteAsset(_selectedAssetFile);
            }
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
            if (ImGui::Button("Delete File##DeleteSelectedWorldAsset")) {
                RequestDeleteAsset(_selectedAssetFile);
            }
            ImGui::Separator();

            if (ImGui::Button("Load World")) {
                LoadSelectedWorld();
            }
        }
        else if (_selectedAssetKind == AssetKind::Prefab)
        {
            ImGui::Text("Prefab: %s", _selectedAssetFile.c_str());
            if (ImGui::Button("Delete File##DeleteSelectedPrefabAsset")) {
                RequestDeleteAsset(_selectedAssetFile);
            }

            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputText(
                "Save Path##SavePrefabPath",
                _prefabPathBuffer.data(),
                _prefabPathBuffer.size()
            );
            if (ImGui::IsItemEdited()) {
                _overwritePrefabConfirmationActive = false;
            }

            if (ImGui::Button("Instantiate")) {
                InstantiateSelectedPrefab();
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

            ImGui::SameLine();
            if (ImGui::Button("Copy Path")) {
                ImGui::SetClipboardText(_selectedAssetFile.c_str());
                SetStatus("Copied prefab path " + _selectedAssetFile, true);
            }
        }
        else if (_selectedAssetKind == AssetKind::Skybox)
        {
            ImGui::Text("Skybox: %s", _selectedAssetFile.c_str());
            ImGui::Separator();

            if (ImGui::Button("Assign to Selected Entity")) {
                AssignSkyboxToSelectedEntity(_selectedAssetFile);
            }

            ImGui::SameLine();
            if (ImGui::Button("Copy Path##CopySkyboxPath")) {
                ImGui::SetClipboardText(_selectedAssetFile.c_str());
                SetStatus("Copied skybox path " + _selectedAssetFile, true);
            }

            ImGui::TextDisabled("Drag this skybox onto an Environment component slot.");
        }
        else if (_selectedAssetKind == AssetKind::AudioClip)
        {
            ImGui::Text("Audio Clip: %s", _selectedAssetFile.c_str());
            if (ImGui::Button("Preview")) {
                if (_core->PlayProjectAudioOneShot(_selectedAssetFile)) {
                    SetStatus("Playing " + _selectedAssetFile, true);
                }
                else {
                    SetStatus("Failed to play " + _selectedAssetFile, false);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Create Sound Cue")) {
                CreateSoundCueFromClip(_selectedAssetFile);
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy Path##CopyAudioClipPath")) {
                ImGui::SetClipboardText(_selectedAssetFile.c_str());
                SetStatus("Copied audio clip path " + _selectedAssetFile, true);
            }

            ImGui::Separator();
            ImGui::TextDisabled("Drag this WAV into a Sound Cue clip list.");
        }
        else if (_selectedAssetKind == AssetKind::SoundCue)
        {
            DrawSoundCueEditor();
        }

        ImGui::EndChild();

        if (_openDeleteConfirmation) {
            ImGui::OpenPopup("Delete Asset File");
            _openDeleteConfirmation = false;
        }

        DrawDeleteConfirmationModal();
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
        if (entry.is_directory() && IsSkyboxFolder(entry.path())) {
            const auto projectPath = _core->MakeProjectRelative(entry.path());
            const auto projectPathString = projectPath.generic_string();
            _assetFiles.push_back(AssetFileEntry {
                AssetKind::Skybox,
                projectPath,
                "[Skybox] " + projectPathString
            });
            continue;
        }

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
        else if (extension == ".wav") {
            kind = AssetKind::AudioClip;
            displayPrefix = "[Audio] ";
        }
        else if (extension == ".json" && projectPathString.starts_with("assets/audio/cues/")) {
            kind = AssetKind::SoundCue;
            displayPrefix = "[Sound Cue] ";
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

void AssetViewer::AssignSkyboxToSelectedEntity(const std::filesystem::path& projectPath)
{
    if (!_core || !_registryViewerPtr) {
        return;
    }

    auto selectedEntity = _registryViewerPtr->GetSelectedEntity();

    if (selectedEntity == entt::null || !_registry.valid(selectedEntity)) {
        SetStatus("No entity selected.", false);
        return;
    }

    auto& environment = _registry.get_or_emplace<EnvironmentComponent>(selectedEntity);
    environment.skyboxPath = projectPath.generic_string();

    if (_core->SetSkyboxPath(environment.skyboxPath)) {
        SetStatus("Assigned skybox " + environment.skyboxPath + " to selected entity.", true);
    }
    else {
        SetStatus("Assigned skybox path, but failed to load " + environment.skyboxPath, false);
    }
}

void AssetViewer::CreateSoundCueFromClip(const std::filesystem::path& clipPath)
{
    if (!_core) {
        return;
    }

    const std::filesystem::path cueDirectory = "assets/audio/cues";
    const std::string stem = clipPath.stem().string();
    std::filesystem::path cuePath = cueDirectory / (stem + ".json");
    for (int suffix = 2; std::filesystem::exists(_core->ResolveProjectPath(cuePath)); ++suffix) {
        cuePath = cueDirectory / (stem + "-" + std::to_string(suffix) + ".json");
    }

    SoundCueAsset cue;
    cue.clips.push_back(clipPath);

    std::string errorMessage;
    if (!SaveSoundCueAsset(_core->ResolveProjectPath(cuePath), cue, &errorMessage)) {
        SetStatus("Failed to create sound cue: " + errorMessage, false);
        return;
    }

    _selectedAssetFile = cuePath.generic_string();
    _selectedAssetKind = AssetKind::SoundCue;
    _core->InvalidateProjectSoundCue(cuePath);
    RefreshAssetList(false);
    LoadSelectedSoundCue();
    SetStatus("Created sound cue " + _selectedAssetFile, true);
}

void AssetViewer::LoadSelectedSoundCue()
{
    _soundCueLoaded = false;
    _soundCueDirty = false;
    if (!_core || _selectedAssetFile.empty()) {
        return;
    }

    std::string errorMessage;
    auto cue = LoadSoundCueAsset(
        _core->ResolveProjectPath(_selectedAssetFile),
        &errorMessage);
    if (!cue) {
        SetStatus("Failed to load sound cue: " + errorMessage, false);
        return;
    }

    _editedSoundCue = std::move(*cue);
    _soundCueLoaded = true;
}

void AssetViewer::SaveSelectedSoundCue()
{
    if (!_core || !_soundCueLoaded || _selectedAssetFile.empty()) {
        return;
    }
    if (_editedSoundCue.clips.empty()) {
        SetStatus("A sound cue needs at least one clip.", false);
        return;
    }

    std::string errorMessage;
    if (!SaveSoundCueAsset(
            _core->ResolveProjectPath(_selectedAssetFile),
            _editedSoundCue,
            &errorMessage)) {
        SetStatus("Failed to save sound cue: " + errorMessage, false);
        return;
    }

    _core->InvalidateProjectSoundCue(_selectedAssetFile);
    _soundCueDirty = false;
    SetStatus("Saved sound cue " + _selectedAssetFile, true);
}

void AssetViewer::DrawSoundCueEditor()
{
    ImGui::Text("Sound Cue: %s", _selectedAssetFile.c_str());
    if (!_soundCueLoaded) {
        ImGui::TextDisabled("Sound cue could not be loaded.");
        return;
    }

    if (ImGui::Button("Preview")) {
        if (_soundCueDirty) {
            SaveSelectedSoundCue();
        }
        if (!_soundCueDirty && !_core->PlayProjectSoundCue(_selectedAssetFile)) {
            SetStatus("Failed to preview sound cue " + _selectedAssetFile, false);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(_soundCueDirty ? "Save *" : "Save")) {
        SaveSelectedSoundCue();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy Path##CopySoundCuePath")) {
        ImGui::SetClipboardText(_selectedAssetFile.c_str());
        SetStatus("Copied sound cue path " + _selectedAssetFile, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete File##DeleteSelectedSoundCue")) {
        RequestDeleteAsset(_selectedAssetFile);
    }

    ImGui::Separator();

    int selectionMode = static_cast<int>(_editedSoundCue.selectionMode);
    constexpr const char* selectionModes[] { "Random", "Sequential" };
    if (ImGui::Combo("Selection", &selectionMode, selectionModes, 2)) {
        _editedSoundCue.selectionMode = static_cast<SoundCueSelectionMode>(selectionMode);
        _soundCueDirty = true;
    }

    _soundCueDirty |= ImGui::DragFloat("Gain", &_editedSoundCue.gain, 0.01f, 0.0f, 4.0f);
    _soundCueDirty |= ImGui::DragFloat("Gain Variation", &_editedSoundCue.gainVariation, 0.01f, 0.0f, 4.0f);
    _soundCueDirty |= ImGui::DragFloat("Pitch", &_editedSoundCue.pitch, 0.01f, 0.01f, 4.0f);
    _soundCueDirty |= ImGui::DragFloat("Pitch Variation", &_editedSoundCue.pitchVariation, 0.01f, 0.0f, 4.0f);
    _soundCueDirty |= ImGui::DragFloat("Cooldown", &_editedSoundCue.cooldown, 0.01f, 0.0f, 60.0f);

    ImGui::SeparatorText("Clips");
    for (std::size_t index = 0; index < _editedSoundCue.clips.size();) {
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::SmallButton("X")) {
            _editedSoundCue.clips.erase(_editedSoundCue.clips.begin() + index);
            _soundCueDirty = true;
            ImGui::PopID();
            continue;
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s", _editedSoundCue.clips[index].generic_string().c_str());
        ImGui::PopID();
        ++index;
    }

    ImGui::Button("Drop WAV Here", ImVec2(-1.0f, 0.0f));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENGINE_AUDIO_CLIP_ASSET")) {
            std::filesystem::path clipPath{ static_cast<const char*>(payload->Data) };
            if (std::find(_editedSoundCue.clips.begin(), _editedSoundCue.clips.end(), clipPath) ==
                _editedSoundCue.clips.end()) {
                _editedSoundCue.clips.push_back(std::move(clipPath));
                _soundCueDirty = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
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

void AssetViewer::RequestDeleteAsset(const std::filesystem::path& projectPath)
{
    if (!IsProjectAssetPath(projectPath)) {
        SetStatus("Can only delete files under assets/.", false);
        return;
    }

    _deleteCandidateProjectPath = projectPath;
    _deleteCandidateTargets = BuildDeleteTargets(projectPath);

    if (_deleteCandidateTargets.empty()) {
        SetStatus("No existing file found for " + projectPath.generic_string(), false);
        return;
    }

    _openDeleteConfirmation = true;
}

void AssetViewer::DrawDeleteConfirmationModal()
{
    if (ImGui::BeginPopupModal("Delete Asset File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Delete asset file?");
        ImGui::Separator();
        ImGui::TextWrapped("%s", _deleteCandidateProjectPath.generic_string().c_str());

        if (_deleteCandidateTargets.size() > 1) {
            ImGui::Spacing();
            ImGui::TextUnformatted("This will also delete:");
            for (std::size_t index = 1; index < _deleteCandidateTargets.size(); ++index) {
                ImGui::BulletText("%s", _core->MakeProjectRelative(_deleteCandidateTargets[index]).generic_string().c_str());
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Delete##ConfirmDeleteAsset")) {
            ConfirmDeleteAsset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##CancelDeleteAsset")) {
            _deleteCandidateProjectPath.clear();
            _deleteCandidateTargets.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void AssetViewer::ConfirmDeleteAsset()
{
    if (!_core || _deleteCandidateTargets.empty()) {
        return;
    }

    std::size_t deletedCount = 0;
    for (const auto& target : _deleteCandidateTargets) {
        std::error_code error;
        if (std::filesystem::remove(target, error)) {
            ++deletedCount;
        }
        else if (error) {
            SetStatus("Failed to delete " + _core->MakeProjectRelative(target).generic_string() + ": " + error.message(), false);
            return;
        }
    }

    const auto deletedPath = _deleteCandidateProjectPath.generic_string();
    _loadedMeshes.erase(deletedPath);
    if (_selectedAssetFile == deletedPath) {
        _selectedAssetFile.clear();
    }

    _deleteCandidateProjectPath.clear();
    _deleteCandidateTargets.clear();
    RefreshAssetList(false);
    SetStatus("Deleted " + std::to_string(deletedCount) + " file(s).", true);
}

std::vector<std::filesystem::path> AssetViewer::BuildDeleteTargets(const std::filesystem::path& projectPath) const
{
    std::vector<std::filesystem::path> targets;
    if (!_core || !IsProjectAssetPath(projectPath)) {
        return targets;
    }

    const auto resolvedPath = _core->ResolveProjectPath(projectPath);
    if (std::filesystem::is_regular_file(resolvedPath)) {
        targets.push_back(resolvedPath);
    }

    if (projectPath.extension() == ".gltf") {
        auto sidecarPath = resolvedPath;
        sidecarPath.replace_extension(".bin");
        if (std::filesystem::is_regular_file(sidecarPath)) {
            targets.push_back(sidecarPath);
        }
    }

    return targets;
}

bool AssetViewer::IsProjectAssetPath(const std::filesystem::path& projectPath) const
{
    const auto path = projectPath.generic_string();
    return path == "assets" || path.starts_with("assets/");
}

bool AssetViewer::IsSkyboxFolder(const std::filesystem::path& path) const
{
    static constexpr std::array<std::string_view, 6> faceNames {
        "right.png",
        "left.png",
        "top.png",
        "bottom.png",
        "front.png",
        "back.png"
    };

    for (const auto faceName : faceNames) {
        if (!std::filesystem::is_regular_file(path / faceName)) {
            return false;
        }
    }

    return true;
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
