#pragma once

#include <AL/al.h>
#include <AL/alc.h>

#include "SoundCueAsset.h"

#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    AudioSystem(AudioSystem&&) = delete;
    AudioSystem& operator=(AudioSystem&&) = delete;

    bool Init();
    void Shutdown();
    void Update();

    bool PlayTestTone(float frequency = 440.0f, float durationSeconds = 0.35f, float gain = 0.25f);
    bool PlayWavOneShot(
        const std::filesystem::path& path,
        float gain = 1.0f,
        float pitch = 1.0f);
    bool PlaySoundCue(
        const std::filesystem::path& cuePath,
        const std::filesystem::path& projectRoot,
        float gainScale = 1.0f,
        float pitchScale = 1.0f);
    void InvalidateSoundCue(const std::filesystem::path& cuePath);

    bool IsInitialized() const { return initialized_; }

private:
    struct ActiveSource {
        ALuint source = 0;
        ALuint transientBuffer = 0;
    };

    struct CachedSoundCue {
        SoundCueAsset asset;
        std::size_t nextClip = 0;
        std::chrono::steady_clock::time_point lastPlayed {};
        bool hasPlayed = false;
    };

    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;
    std::unordered_map<std::string, ALuint> wavBuffers_;
    std::unordered_map<std::string, CachedSoundCue> soundCues_;
    std::unordered_set<std::string> failedWavPaths_;
    std::unordered_set<std::string> failedSoundCuePaths_;
    std::vector<ActiveSource> sources_;
    std::mt19937 randomGenerator_ { std::random_device{}() };

    bool initialized_ = false;
};
