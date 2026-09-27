#pragma once

#include "EditorHistory.h"
#include <engine/System.h>

#include <optional>

namespace Engine {
class Core;
}

class ViewportGizmoSystem : public System {
public:
    ViewportGizmoSystem(entt::registry& registry, Engine::Core& core);

    void DrawUi() override;

private:
    enum class Operation {
        None,
        Translate,
        Rotate,
        Scale
    };

    Engine::Core& _core;
    Operation _operation = Operation::Translate;
    bool _localMode = false;
    bool _snapEnabled = false;
    float _translationSnap = 1.0f;
    float _rotationSnap = 15.0f;
    float _scaleSnap = 0.1f;
    bool _wasUsing = false;
    entt::entity _transactionEntity{ entt::null };
    std::optional<EditorTransformSnapshot> _transactionBefore;
};
