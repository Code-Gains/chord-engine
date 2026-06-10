#include "AudioSystem.h"

bool AudioSystem::Init() {
    if (initialized_) {
        return true;
    }

    device_ = alcOpenDevice(nullptr);
    if (!device_) {
        return false;
    }

    context_ = alcCreateContext(device_, nullptr);
    if (!context_) {
        alcCloseDevice(device_);
        device_ = nullptr;
        return false;
    }

    if (!alcMakeContextCurrent(context_)) {
        alcDestroyContext(context_);
        alcCloseDevice(device_);

        context_ = nullptr;
        device_ = nullptr;

        return false;
    }

    initialized_ = true;
    return true;
}

void AudioSystem::Shutdown() {
    if (!initialized_) {
        return;
    }

    alcMakeContextCurrent(nullptr);

    if (context_) {
        alcDestroyContext(context_);
        context_ = nullptr;
    }

    if (device_) {
        alcCloseDevice(device_);
        device_ = nullptr;
    }

    initialized_ = false;
}

AudioSystem::~AudioSystem() {
    Shutdown();
}