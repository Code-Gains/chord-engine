#pragma once

#include <AL/al.h>
#include <AL/alc.h>

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

    bool IsInitialized() const { return initialized_; }

private:
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;

    bool initialized_ = false;
};