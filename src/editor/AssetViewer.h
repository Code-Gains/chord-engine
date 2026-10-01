#pragma once

#include <engine/Core.h>
#include <engine/SoundCueAsset.h>
#include <engine/System.h>
#include "RegistryViewer.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class AssetViewer : public System {
public:
    AssetViewer(entt::registry& registry, Engine::Core* core, RegistryViewer* registryViewerPtr);

    void Update(float deltaTime) override;
    void DrawUi() override;

private:
    enum class AssetKind {
        Mesh,
        World,
        Prefab,
        Skybox,
        AudioClip,
        SoundCue
    };

    struct AssetFileEntry {
        AssetKind kind;
        std::filesystem::path projectPath;
        std::string displayName;
    };

    Engine::Core* _core = nullptr;
    RegistryViewer* _registryViewerPtr = nullptr;
    std::vector<AssetFileEntry> _assetFiles;
    std::vector<std::filesystem::path> _assetFolders;
    std::unordered_map<std::string, std::vector<std::shared_ptr<MeshAsset>>> _loadedMeshes;
    std::filesystem::path _selectedFolder{ "assets" };
    std::array<char, 256> _searchBuffer{};
    AssetKind _selectedAssetKind = AssetKind::Mesh;
    std::string _selectedAssetFile;
    std::string _statusText;
    float _statusTimer = 0.0f;
    bool _statusSucceeded = true;
    std::array<char, 512> _prefabPathBuffer {};
    bool _overwritePrefabConfirmationActive = false;
    std::filesystem::path _deleteCandidateProjectPath;
    std::vector<std::filesystem::path> _deleteCandidateTargets;
    bool _openDeleteConfirmation = false;
    SoundCueAsset _editedSoundCue;
    bool _soundCueLoaded = false;
    bool _soundCueDirty = false;

    void RefreshAssetList(bool updateStatus = true);
    void DrawFolderTree(const std::filesystem::path& folder);
    void DrawAssetList();
    void DrawAssetDragSource(const AssetFileEntry& file);
    void SelectAsset(const AssetFileEntry& file);
    void ActivateAsset(const AssetFileEntry& file);
    bool AssetMatchesSearch(const AssetFileEntry& file) const;
    std::vector<std::shared_ptr<MeshAsset>>* GetOrLoadMeshes(const std::filesystem::path& projectPath);
    void AssignMeshToSelectedEntity(const std::shared_ptr<MeshAsset>& mesh);
    void AssignSkyboxToSelectedEntity(const std::filesystem::path& projectPath);
    void CreateSoundCueFromClip(const std::filesystem::path& clipPath);
    void LoadSelectedSoundCue();
    void SaveSelectedSoundCue();
    void DrawSoundCueEditor();
    void LoadSelectedWorld();
    void InstantiateSelectedPrefab();
    void SaveSelectedEntityAsPrefab(bool overwriteConfirmed = false);
    void RequestDeleteAsset(const std::filesystem::path& projectPath);
    void DrawDeleteConfirmationModal();
    void ConfirmDeleteAsset();
    std::vector<std::filesystem::path> BuildDeleteTargets(const std::filesystem::path& projectPath) const;
    bool IsProjectAssetPath(const std::filesystem::path& projectPath) const;
    bool IsSkyboxFolder(const std::filesystem::path& path) const;
    void SetPrefabPathBuffer(const std::filesystem::path& projectPath);
    void SetStatus(std::string text, bool succeeded);
};
