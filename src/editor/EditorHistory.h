#pragma once

#include "HierarchyComponent.h"
#include "Transform.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

struct EditorTransformSnapshot {
    Transform world;
    std::optional<Transform> local;
};

struct EditorTransformCommand {
    entt::entity entity{ entt::null };
    EditorTransformSnapshot before;
    EditorTransformSnapshot after;
};

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

        _commands.erase(_commands.begin() + static_cast<std::ptrdiff_t>(_cursor), _commands.end());
        _commands.push_back(std::move(command));
        _cursor = _commands.size();

        if (_commands.size() > MaximumCommands) {
            const size_t excess = _commands.size() - MaximumCommands;
            _commands.erase(_commands.begin(), _commands.begin() + static_cast<std::ptrdiff_t>(excess));
            _cursor -= std::min(_cursor, excess);
        }
    }

    bool CanUndo() const
    {
        return _cursor > 0;
    }

    bool CanRedo() const
    {
        return _cursor < _commands.size();
    }

    void Undo(entt::registry& registry)
    {
        if (!CanUndo()) {
            return;
        }

        --_cursor;
        Apply(registry, _commands[_cursor].entity, _commands[_cursor].before);
    }

    void Redo(entt::registry& registry)
    {
        if (!CanRedo()) {
            return;
        }

        Apply(registry, _commands[_cursor].entity, _commands[_cursor].after);
        ++_cursor;
    }

private:
    static void Apply(
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

    std::vector<EditorTransformCommand> _commands;
    size_t _cursor = 0;
};
