#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

enum class SoundCueSelectionMode {
    Random,
    Sequential
};

struct SoundCueAsset {
    std::vector<std::filesystem::path> clips;
    SoundCueSelectionMode selectionMode = SoundCueSelectionMode::Random;
    float gain = 1.0f;
    float gainVariation = 0.0f;
    float pitch = 1.0f;
    float pitchVariation = 0.0f;
    float cooldown = 0.0f;
};

const char* SoundCueSelectionModeToString(SoundCueSelectionMode mode);
SoundCueSelectionMode SoundCueSelectionModeFromString(const std::string& value);

std::optional<SoundCueAsset> LoadSoundCueAsset(
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);

bool SaveSoundCueAsset(
    const std::filesystem::path& path,
    const SoundCueAsset& cue,
    std::string* errorMessage = nullptr);
