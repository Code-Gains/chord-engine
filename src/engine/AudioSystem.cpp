#include "AudioSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

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

void AudioSystem::Update()
{
    if (!initialized_) {
        return;
    }

    for (auto it = sources_.begin(); it != sources_.end();) {
        ALint state = AL_STOPPED;
        alGetSourcei(*it, AL_SOURCE_STATE, &state);
        if (state == AL_STOPPED) {
            ALint buffer = 0;
            alGetSourcei(*it, AL_BUFFER, &buffer);
            alDeleteSources(1, &(*it));
            if (buffer != 0) {
                const ALuint bufferId = static_cast<ALuint>(buffer);
                alDeleteBuffers(1, &bufferId);
                std::erase(buffers_, bufferId);
            }
            it = sources_.erase(it);
        }
        else {
            ++it;
        }
    }
}

bool AudioSystem::PlayTestTone(float frequency, float durationSeconds, float gain)
{
    if (!initialized_) {
        return false;
    }

    constexpr int sampleRate = 48000;
    const int sampleCount =
        std::max(1, static_cast<int>(static_cast<float>(sampleRate) * durationSeconds));
    std::vector<int16_t> samples(static_cast<size_t>(sampleCount));

    constexpr float pi = 3.14159265358979323846f;
    const float clampedGain = std::clamp(gain, 0.0f, 1.0f);
    const float angularFrequency =
        2.0f * pi * std::max(1.0f, frequency);
    for (int sample = 0; sample < sampleCount; ++sample) {
        const float t = static_cast<float>(sample) / static_cast<float>(sampleRate);
        const float fadeIn = std::min(1.0f, t / 0.015f);
        const float fadeOut = std::min(1.0f, (durationSeconds - t) / 0.035f);
        const float envelope = std::clamp(std::min(fadeIn, fadeOut), 0.0f, 1.0f);
        const float value = std::sin(angularFrequency * t) * clampedGain * envelope;
        samples[static_cast<size_t>(sample)] =
            static_cast<int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767.0f);
    }

    ALuint buffer = 0;
    alGenBuffers(1, &buffer);
    alBufferData(
        buffer,
        AL_FORMAT_MONO16,
        samples.data(),
        static_cast<ALsizei>(samples.size() * sizeof(int16_t)),
        sampleRate);

    ALuint source = 0;
    alGenSources(1, &source);
    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcef(source, AL_GAIN, 1.0f);
    alSourcePlay(source);

    buffers_.push_back(buffer);
    sources_.push_back(source);
    return alGetError() == AL_NO_ERROR;
}

void AudioSystem::Shutdown() {
    if (!initialized_) {
        return;
    }

    for (ALuint source : sources_) {
        alSourceStop(source);
        alDeleteSources(1, &source);
    }
    sources_.clear();

    for (ALuint buffer : buffers_) {
        alDeleteBuffers(1, &buffer);
    }
    buffers_.clear();

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
