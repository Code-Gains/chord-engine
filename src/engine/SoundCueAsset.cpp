#include "SoundCueAsset.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

const char* SoundCueSelectionModeToString(SoundCueSelectionMode mode)
{
    switch (mode) {
    case SoundCueSelectionMode::Sequential:
        return "Sequential";
    case SoundCueSelectionMode::Random:
    default:
        return "Random";
    }
}

SoundCueSelectionMode SoundCueSelectionModeFromString(const std::string& value)
{
    if (value == "Sequential") {
        return SoundCueSelectionMode::Sequential;
    }

    return SoundCueSelectionMode::Random;
}

std::optional<SoundCueAsset> LoadSoundCueAsset(
    const std::filesystem::path& path,
    std::string* errorMessage)
{
    try {
        std::ifstream file(path);
        if (!file) {
            if (errorMessage) {
                *errorMessage = "Could not open sound cue.";
            }
            return std::nullopt;
        }

        nlohmann::json data;
        file >> data;
        if (data.value("assetType", std::string{}) != "SoundCue") {
            if (errorMessage) {
                *errorMessage = "JSON file is not a SoundCue asset.";
            }
            return std::nullopt;
        }

        SoundCueAsset cue;
        cue.selectionMode = SoundCueSelectionModeFromString(
            data.value("selectionMode", std::string{ "Random" }));
        cue.gain = std::max(0.0f, data.value("gain", 1.0f));
        cue.gainVariation = std::max(0.0f, data.value("gainVariation", 0.0f));
        cue.pitch = std::max(0.01f, data.value("pitch", 1.0f));
        cue.pitchVariation = std::max(0.0f, data.value("pitchVariation", 0.0f));
        cue.cooldown = std::max(0.0f, data.value("cooldown", 0.0f));

        if (const auto clips = data.find("clips"); clips != data.end() && clips->is_array()) {
            for (const auto& clip : *clips) {
                if (clip.is_string()) {
                    cue.clips.emplace_back(clip.get<std::string>());
                }
            }
        }

        return cue;
    }
    catch (const std::exception& exception) {
        if (errorMessage) {
            *errorMessage = exception.what();
        }
        return std::nullopt;
    }
}

bool SaveSoundCueAsset(
    const std::filesystem::path& path,
    const SoundCueAsset& cue,
    std::string* errorMessage)
{
    try {
        nlohmann::json clips = nlohmann::json::array();
        for (const auto& clip : cue.clips) {
            clips.push_back(clip.generic_string());
        }

        const nlohmann::json data {
            {"assetType", "SoundCue"},
            {"selectionMode", SoundCueSelectionModeToString(cue.selectionMode)},
            {"clips", clips},
            {"gain", std::max(0.0f, cue.gain)},
            {"gainVariation", std::max(0.0f, cue.gainVariation)},
            {"pitch", std::max(0.01f, cue.pitch)},
            {"pitchVariation", std::max(0.0f, cue.pitchVariation)},
            {"cooldown", std::max(0.0f, cue.cooldown)}
        };

        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream file(path);
        if (!file) {
            if (errorMessage) {
                *errorMessage = "Could not create sound cue file.";
            }
            return false;
        }

        file << data.dump(2) << '\n';
        return true;
    }
    catch (const std::exception& exception) {
        if (errorMessage) {
            *errorMessage = exception.what();
        }
        return false;
    }
}
