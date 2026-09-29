#pragma once

#include "HierarchyComponent.h"
#include "Transform.h"
#include "Core.h"
#include "WorldSerializer.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct EditorTransformSnapshot {
    Transform world;
    std::optional<Transform> local;
};

struct EditorTransformCommand {
    entt::entity entity{ entt::null };
    EditorTransformSnapshot before;
    EditorTransformSnapshot after;
    std::string label = "Transform";
};

struct EditorEntityLifecycleCommand {
    Engine::Serialization::SerializedEntityHierarchy hierarchy;
    bool existedBefore = false;
    bool existedAfter = false;
    std::string label = "Entity lifecycle";
};

struct EditorComponentDelta {
    entt::entity entity{ entt::null };
    std::string type;
    std::optional<Engine::Serialization::SerializedComponent> before;
    std::optional<Engine::Serialization::SerializedComponent> after;
};

struct EditorComponentCommand {
    entt::entity selectedEntity{ entt::null };
    std::vector<EditorComponentDelta> deltas;
    std::string label = "Edit components";
};

using EditorCommand = std::variant<
    EditorTransformCommand,
    EditorEntityLifecycleCommand,
    EditorComponentCommand>;

class EditorHistory {
public:
    static constexpr size_t MaximumCommands = 256;

    static EditorTransformSnapshot CaptureTransform(
        entt::registry& registry,
        entt::entity entity)
    {
        EditorTransformSnapshot snapshot;
        snapshot.world = registry.get<Transform>(entity);
        if (const auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
            snapshot.local = hierarchy->localTransform;
        }
        return snapshot;
    }

    static bool IsDifferent(
        const EditorTransformSnapshot& left,
        const EditorTransformSnapshot& right)
    {
        const auto transformDifferent = [](const Transform& a, const Transform& b) {
            constexpr float epsilon = 0.00001f;
            return glm::length(a.position - b.position) > epsilon ||
                glm::length(a.scale - b.scale) > epsilon ||
                1.0f - std::abs(glm::dot(a.rotation, b.rotation)) > epsilon;
        };

        if (transformDifferent(left.world, right.world) ||
            left.local.has_value() != right.local.has_value()) {
            return true;
        }

        return left.local && transformDifferent(*left.local, *right.local);
    }

    void PushTransform(EditorTransformCommand command)
    {
        if (!IsDifferent(command.before, command.after)) {
            return;
        }

        Push(std::move(command));
    }

    void PushEntityLifecycle(EditorEntityLifecycleCommand command)
    {
        if (command.existedBefore == command.existedAfter ||
            command.hierarchy.entities.empty()) {
            return;
        }

        Push(std::move(command));
    }

    void PushComponents(EditorComponentCommand command)
    {
        if (command.deltas.empty()) {
            return;
        }
        Push(std::move(command));
    }

    entt::entity Undo(Engine::Core& core)
    {
        if (!CanUndo()) {
            return entt::null;
        }

        --_cursor;
        const entt::entity selectedEntity = Apply(core, _commands[_cursor], false);
        ++_revision;
        return selectedEntity;
    }

    entt::entity Redo(Engine::Core& core)
    {
        if (!CanRedo()) {
            return entt::null;
        }

        const entt::entity selectedEntity = Apply(core, _commands[_cursor], true);
        ++_cursor;
        ++_revision;
        return selectedEntity;
    }

private:
    void Push(EditorCommand command)
    {
        _commands.erase(_commands.begin() + static_cast<std::ptrdiff_t>(_cursor), _commands.end());
        _commands.push_back(std::move(command));
        _cursor = _commands.size();
        ++_revision;

        if (_commands.size() > MaximumCommands) {
            const size_t excess = _commands.size() - MaximumCommands;
            _commands.erase(_commands.begin(), _commands.begin() + static_cast<std::ptrdiff_t>(excess));
            _cursor -= std::min(_cursor, excess);
        }
    }

public:
    bool CanUndo() const
    {
        return _cursor > 0;
    }

    bool CanRedo() const
    {
        return _cursor < _commands.size();
    }

    size_t Cursor() const
    {
        return _cursor;
    }

    uint64_t Revision() const
    {
        return _revision;
    }

    const std::vector<EditorCommand>& Commands() const
    {
        return _commands;
    }

    static const std::string& Label(const EditorCommand& command)
    {
        return std::visit([](const auto& typedCommand) -> const std::string& {
            return typedCommand.label;
        }, command);
    }

private:
    static void ApplyTransform(
        entt::registry& registry,
        entt::entity entity,
        const EditorTransformSnapshot& snapshot)
    {
        if (entity == entt::null ||
            !registry.valid(entity) ||
            !registry.all_of<Transform>(entity)) {
            return;
        }

        registry.replace<Transform>(entity, snapshot.world);
        if (snapshot.local) {
            if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
                hierarchy->localTransform = *snapshot.local;
            }
        }
    }

    static void DestroyHierarchy(
        entt::registry& registry,
        const Engine::Serialization::SerializedEntityHierarchy& hierarchy)
    {
        for (auto it = hierarchy.entities.rbegin(); it != hierarchy.entities.rend(); ++it) {
            const entt::entity entity = static_cast<entt::entity>(it->id);
            if (registry.valid(entity) && !registry.all_of<Engine::CoreOwnedTag>(entity)) {
                registry.destroy(entity);
            }
        }
    }

    static entt::entity Apply(
        Engine::Core& core,
        EditorCommand& command,
        bool useAfter)
    {
        auto& registry = core.GetRegistry();
        if (auto* transform = std::get_if<EditorTransformCommand>(&command)) {
            const auto& snapshot = useAfter ? transform->after : transform->before;
            ApplyTransform(registry, transform->entity, snapshot);
            return registry.valid(transform->entity) ? transform->entity : entt::null;
        }

        if (auto* state = std::get_if<EditorComponentCommand>(&command)) {
            auto serializer = core.CreateWorldSerializer();
            for (const auto& delta : state->deltas) {
                const auto& value = useAfter ? delta.after : delta.before;
                if (value) {
                    serializer.RestoreComponent(core, delta.entity, *value);
                }
                else {
                    serializer.RemoveComponent(core, delta.entity, delta.type);
                }
            }
            return registry.valid(state->selectedEntity)
                ? state->selectedEntity
                : entt::null;
        }

        auto& lifecycle = std::get<EditorEntityLifecycleCommand>(command);
        const bool shouldExist = useAfter
            ? lifecycle.existedAfter
            : lifecycle.existedBefore;
        if (!shouldExist) {
            DestroyHierarchy(registry, lifecycle.hierarchy);
            return entt::null;
        }

        auto serializer = core.CreateWorldSerializer();
        const auto root = serializer.InstantiateEntityHierarchy(
            core,
            lifecycle.hierarchy,
            true);
        return root.value_or(entt::null);
    }

    std::vector<EditorCommand> _commands;
    size_t _cursor = 0;
    uint64_t _revision = 0;
};

inline EditorHistory& GetEditorHistory(entt::registry& registry)
{
    if (!registry.ctx().contains<EditorHistory>()) {
        registry.ctx().emplace<EditorHistory>();
    }
    return registry.ctx().get<EditorHistory>();
}
