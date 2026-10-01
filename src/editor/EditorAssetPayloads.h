#pragma once

#include <cstdint>

struct EditorMeshAssetPayload {
    char projectPath[512]{};
    uint32_t meshIndex = 0;
};
