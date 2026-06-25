#pragma once

#include <engine/Core.h>
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
        Prefab
    };

    struct AssetFileEntry {
        AssetKind kind;
        std::filesystem::path projectPath;
        std::string displayName;
    };

    Engine::Core* _core = nullptr;
    RegistryViewer* _registryViewerPtr = nullptr;
    std::vector<AssetFileEntry> _assetFiles;
    std::unordered_map<std::string, std::vector<std::shared_ptr<MeshAsset>>> _loadedMeshes;
    AssetKind _selectedAssetKind = AssetKind::Mesh;
    std::string _selectedAssetFile;
    std::string _statusText;
    float _statusTimer = 0.0f;
    bool _statusSucceeded = true;
    std::array<char, 512> _prefabPathBuffer {};
    bool _overwritePrefabConfirmationActive = false;

    void RefreshAssetList(bool updateStatus = true);
    std::vector<std::shared_ptr<MeshAsset>>* GetOrLoadMeshes(const std::filesystem::path& projectPath);
    void AssignMeshToSelectedEntity(const std::shared_ptr<MeshAsset>& mesh);
    void LoadSelectedWorld();
    void InstantiateSelectedPrefab();
    void SaveSelectedEntityAsPrefab(bool overwriteConfirmed = false);
    void SetPrefabPathBuffer(const std::filesystem::path& projectPath);
    void SetStatus(std::string text, bool succeeded);
};
