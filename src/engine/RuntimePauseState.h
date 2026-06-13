#pragma once

#include <entt/entt.hpp>

#include <cstdint>

namespace Engine {

enum class RuntimePauseReason : std::uint32_t {
    None = 0,
    Crafting = 1u << 0u,
    Menu = 1u << 1u,
    WaveBreak = 1u << 2u
};

struct RuntimePauseState {
    std::uint32_t reasons = 0u;
};

inline std::uint32_t PauseReasonMask(RuntimePauseReason reason)
{
    return static_cast<std::uint32_t>(reason);
}

inline RuntimePauseState& GetRuntimePauseState(entt::registry& registry)
{
    if (auto* state = registry.ctx().find<RuntimePauseState>()) {
        return *state;
    }

    return registry.ctx().emplace<RuntimePauseState>();
}

inline bool IsGameplayPaused(entt::registry& registry)
{
    const auto* state = registry.ctx().find<RuntimePauseState>();
    return state && state->reasons != 0u;
}

inline bool HasPauseReason(entt::registry& registry, RuntimePauseReason reason)
{
    const auto* state = registry.ctx().find<RuntimePauseState>();
    return state && (state->reasons & PauseReasonMask(reason)) != 0u;
}

inline void SetPauseReason(entt::registry& registry, RuntimePauseReason reason, bool enabled)
{
    auto& state = GetRuntimePauseState(registry);
    const std::uint32_t mask = PauseReasonMask(reason);
    if (enabled) {
        state.reasons |= mask;
    }
    else {
        state.reasons &= ~mask;
    }
}

inline void ClearRuntimePause(entt::registry& registry)
{
    GetRuntimePauseState(registry).reasons = 0u;
}

}
