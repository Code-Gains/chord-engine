#pragma once

#include <AL/al.h>
#include <AL/alc.h>

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

    bool IsInitialized() const { return initialized_; }

private:
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;
    std::vector<ALuint> buffers_;
    std::vector<ALuint> sources_;

    bool initialized_ = false;
};
