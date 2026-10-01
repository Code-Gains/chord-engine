#pragma once
#include <engine/System.h>
#include "RegistryViewer.h"
#include "ViewerComponentUi.h"
#include "EditorHistory.h"

#include <array>
#include <functional>
#include <optional>
#include <string>

class EntityViewer : public System {
    void Update(float deltaTime) override;
    virtual void DrawUi() override;

    RegistryViewer* _registryViewerPtr;
    Engine::Core* _core = nullptr;
    entt::entity _componentHistoryEntity{ entt::null };
    std::vector<Engine::Serialization::SerializedEntity> _componentHistoryBaseline;
    uint64_t _observedHistoryRevision = 0;
    bool _componentInteractionActive = false;
    std::vector<std::unique_ptr<ViewerComponentUi>> _componentUis;
    struct ComponentMenuEntry {
        std::string label;
        std::string category;
        std::function<bool(entt::registry&, entt::entity)> canAdd;
        std::function<void(entt::registry&, entt::entity)> add;
    };

    std::vector<ComponentMenuEntry> _componentMenuEntries;
    std::array<char, 128> _componentSearchBuffer{};
    int _componentPickerSelection = 0;

    void DrawAddComponentPopup(entt::entity selectedEntity);

public:
    EntityViewer(entt::registry& registry, RegistryViewer* registryViewerPtr, Engine::Core* core = nullptr);
    void AddComponentUi(std::unique_ptr<ViewerComponentUi> componentUi);
    void AddComponentMenuItem(
        std::string label,
        std::function<bool(entt::registry&, entt::entity)> canAdd,
        std::function<void(entt::registry&, entt::entity)> add,
        std::string category = {}
    );
};
