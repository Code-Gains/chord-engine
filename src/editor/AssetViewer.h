#pragma once

#include <engine/Core.h>
#include <engine/System.h>
#include "RegistryViewer.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class AssetViewer : public System {
public:
    AssetViewer(entt::registry& registry, Engine::Core* core, RegistryViewer* registryViewerPtr);

    void Update(float deltaTime) override;
    void FixedUpdate(float deltaTime) override {};
    void DrawUi() override;
    void Draw() override {};

private:
    enum class AssetKind {
        Mesh,
        World
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

    void RefreshAssetList();
    std::vector<std::shared_ptr<MeshAsset>>* GetOrLoadMeshes(const std::filesystem::path& projectPath);
    void AssignMeshToSelectedEntity(const std::shared_ptr<MeshAsset>& mesh);
    void LoadSelectedWorld();
};
