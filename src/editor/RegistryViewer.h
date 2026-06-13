#pragma once
#include <engine/System.h>
#include <entt/entt.hpp>
#include <optional>
#include "ViewerComponentUi.h"
#include "WorldSerializer.h"

namespace Engine {
class Core;
}

class RegistryViewer : public System {
    void Update(float deltaTime) override;
    void FixedUpdate(float deltaTime) override {};
    virtual void DrawUi() override;
    virtual void Draw() override {};

    entt::entity _selectedEntity{ entt::null };
    bool _showCoreOwnedEntities = false;
    bool _showEntityIds = false;
    Engine::Core* _core = nullptr;
    std::optional<Engine::Serialization::SerializedEntity> _copiedEntity;

    bool CanCopySelectedEntity() const;
    bool CanPasteEntity() const;
    bool CopySelectedEntity();
    bool PasteCopiedEntity();
    void DrawEntityNode(entt::entity entity);


public:
    RegistryViewer(entt::registry& registry, Engine::Core* core = nullptr);
    const entt::entity& GetSelectedEntity() const;
    void SetSelectedEntity(entt::entity entity);
};
