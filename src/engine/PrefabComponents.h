#pragma once

enum class PrefabPlacementRotationMode {
    Automatic,
    Preserve,
    AlignToPlacementUp
};

struct PrefabPlacementAnchorComponent {
    PrefabPlacementRotationMode rotationMode = PrefabPlacementRotationMode::Automatic;
    float footprintRadius = 0.0f;
};
