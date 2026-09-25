#include "AudioSystem.h"

#include "Log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>

namespace {

struct WavData {
    ALenum format = 0;
    ALsizei sampleRate = 0;
    std::vector<int16_t> samples;
};

uint16_t ReadU16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
        (static_cast<uint16_t>(bytes[1]) << 8u);
}

uint32_t ReadU32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8u) |
        (static_cast<uint32_t>(bytes[2]) << 16u) |
        (static_cast<uint32_t>(bytes[3]) << 24u);
}

bool IsChunk(const uint8_t* bytes, const char (&name)[5])
{
    return std::memcmp(bytes, name, 4) == 0;
}

std::optional<WavData> LoadWavFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }

    const std::streamsize fileSize = file.tellg();
    if (fileSize < 44) {
        return std::nullopt;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), fileSize)) {
        return std::nullopt;
    }

    if (!IsChunk(bytes.data(), "RIFF") || !IsChunk(bytes.data() + 8, "WAVE")) {
        return std::nullopt;
    }

    uint16_t audioFormat = 0;
    uint16_t channelCount = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    const uint8_t* sampleBytes = nullptr;
    size_t sampleByteCount = 0;

    size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const uint8_t* chunk = bytes.data() + offset;
        const size_t chunkSize = ReadU32(chunk + 4);
        const size_t chunkDataOffset = offset + 8;
        if (chunkDataOffset > bytes.size() || chunkSize > bytes.size() - chunkDataOffset) {
            return std::nullopt;
        }

        if (IsChunk(chunk, "fmt ") && chunkSize >= 16) {
            const uint8_t* format = bytes.data() + chunkDataOffset;
            audioFormat = ReadU16(format);
            channelCount = ReadU16(format + 2);
            sampleRate = ReadU32(format + 4);
            bitsPerSample = ReadU16(format + 14);
        }
        else if (IsChunk(chunk, "data")) {
            sampleBytes = bytes.data() + chunkDataOffset;
            sampleByteCount = chunkSize;
        }

        offset = chunkDataOffset + chunkSize + (chunkSize & 1u);
    }

    if (!sampleBytes || sampleRate == 0 || (channelCount != 1 && channelCount != 2)) {
        return std::nullopt;
    }

    const size_t bytesPerSample = bitsPerSample / 8u;
    if (bytesPerSample == 0 || sampleByteCount % bytesPerSample != 0) {
        return std::nullopt;
    }

    WavData wav;
    wav.format = channelCount == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    wav.sampleRate = static_cast<ALsizei>(sampleRate);
    wav.samples.resize(sampleByteCount / bytesPerSample);

    for (size_t sampleIndex = 0; sampleIndex < wav.samples.size(); ++sampleIndex) {
        const uint8_t* sample = sampleBytes + sampleIndex * bytesPerSample;

        if (audioFormat == 1 && bitsPerSample == 8) {
            wav.samples[sampleIndex] = static_cast<int16_t>(
                (static_cast<int>(sample[0]) - 128) << 8);
        }
        else if (audioFormat == 1 && bitsPerSample == 16) {
            wav.samples[sampleIndex] = static_cast<int16_t>(ReadU16(sample));
        }
        else if (audioFormat == 1 && bitsPerSample == 24) {
            int32_t value = static_cast<int32_t>(sample[0]) |
                (static_cast<int32_t>(sample[1]) << 8) |
                (static_cast<int32_t>(sample[2]) << 16);
            if ((value & 0x00800000) != 0) {
                value |= static_cast<int32_t>(0xff000000);
            }
            wav.samples[sampleIndex] = static_cast<int16_t>(value >> 8);
        }
        else if (audioFormat == 1 && bitsPerSample == 32) {
            wav.samples[sampleIndex] = static_cast<int16_t>(
                static_cast<int32_t>(ReadU32(sample)) >> 16);
        }
        else if (audioFormat == 3 && bitsPerSample == 32) {
            float value = 0.0f;
            std::memcpy(&value, sample, sizeof(value));
            wav.samples[sampleIndex] = static_cast<int16_t>(
                std::clamp(value, -1.0f, 1.0f) * 32767.0f);
        }
        else {
            return std::nullopt;
        }
    }

    return wav;
}

}

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
        alGetSourcei(it->source, AL_SOURCE_STATE, &state);
        if (state == AL_STOPPED) {
            alDeleteSources(1, &it->source);
            if (it->transientBuffer != 0) {
                alDeleteBuffers(1, &it->transientBuffer);
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

    sources_.push_back(ActiveSource{ source, buffer });
    return alGetError() == AL_NO_ERROR;
}

bool AudioSystem::PlayWavOneShot(
    const std::filesystem::path& path,
    float gain,
    float pitch)
{
    if (!initialized_) {
        return false;
    }

    const std::string cacheKey = path.lexically_normal().generic_string();
    if (failedWavPaths_.contains(cacheKey)) {
        return false;
    }

    ALuint buffer = 0;
    if (const auto bufferIt = wavBuffers_.find(cacheKey); bufferIt != wavBuffers_.end()) {
        buffer = bufferIt->second;
    }
    else {
        const auto wav = LoadWavFile(path);
        if (!wav) {
            ENGINE_LOG_ERROR("Failed to load WAV audio: " + cacheKey);
            failedWavPaths_.insert(cacheKey);
            return false;
        }

        alGenBuffers(1, &buffer);
        alBufferData(
            buffer,
            wav->format,
            wav->samples.data(),
            static_cast<ALsizei>(wav->samples.size() * sizeof(int16_t)),
            wav->sampleRate);
        if (alGetError() != AL_NO_ERROR) {
            alDeleteBuffers(1, &buffer);
            ENGINE_LOG_ERROR("OpenAL failed to create WAV buffer: " + cacheKey);
            failedWavPaths_.insert(cacheKey);
            return false;
        }

        wavBuffers_.emplace(cacheKey, buffer);
    }

    ALuint source = 0;
    alGenSources(1, &source);
    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcef(source, AL_GAIN, std::clamp(gain, 0.0f, 4.0f));
    alSourcef(source, AL_PITCH, std::clamp(pitch, 0.25f, 4.0f));
    alSourcePlay(source);
    if (alGetError() != AL_NO_ERROR) {
        alDeleteSources(1, &source);
        return false;
    }

    sources_.push_back(ActiveSource{ source, 0 });
    return true;
}

bool AudioSystem::PlaySoundCue(
    const std::filesystem::path& cuePath,
    const std::filesystem::path& projectRoot,
    float gainScale,
    float pitchScale)
{
    if (!initialized_) {
        return false;
    }

    const std::string cacheKey = cuePath.lexically_normal().generic_string();
    if (failedSoundCuePaths_.contains(cacheKey)) {
        return false;
    }

    auto cueIt = soundCues_.find(cacheKey);
    if (cueIt == soundCues_.end()) {
        std::string errorMessage;
        auto cue = LoadSoundCueAsset(cuePath, &errorMessage);
        if (!cue || cue->clips.empty()) {
            ENGINE_LOG_ERROR(
                "Failed to load sound cue " + cacheKey +
                (errorMessage.empty() ? ": cue contains no clips." : ": " + errorMessage));
            failedSoundCuePaths_.insert(cacheKey);
            return false;
        }

        cueIt = soundCues_.emplace(
            cacheKey,
            CachedSoundCue{ .asset = std::move(*cue) }).first;
    }

    auto& cachedCue = cueIt->second;
    const auto now = std::chrono::steady_clock::now();
    if (cachedCue.hasPlayed && cachedCue.asset.cooldown > 0.0f) {
        const float elapsed = std::chrono::duration<float>(now - cachedCue.lastPlayed).count();
        if (elapsed < cachedCue.asset.cooldown) {
            return false;
        }
    }

    std::size_t clipIndex = 0;
    if (cachedCue.asset.selectionMode == SoundCueSelectionMode::Sequential) {
        clipIndex = cachedCue.nextClip % cachedCue.asset.clips.size();
        cachedCue.nextClip = (clipIndex + 1) % cachedCue.asset.clips.size();
    }
    else {
        std::uniform_int_distribution<std::size_t> distribution(
            0,
            cachedCue.asset.clips.size() - 1);
        clipIndex = distribution(randomGenerator_);
    }

    std::uniform_real_distribution<float> variation(-1.0f, 1.0f);
    const float gain = std::max(
        0.0f,
        cachedCue.asset.gain + variation(randomGenerator_) * cachedCue.asset.gainVariation) *
        std::max(0.0f, gainScale);
    const float pitch = std::max(
        0.01f,
        cachedCue.asset.pitch + variation(randomGenerator_) * cachedCue.asset.pitchVariation) *
        std::max(0.01f, pitchScale);

    const auto& clipPath = cachedCue.asset.clips[clipIndex];
    const auto resolvedClipPath = clipPath.is_absolute() ? clipPath : projectRoot / clipPath;
    if (!PlayWavOneShot(resolvedClipPath, gain, pitch)) {
        return false;
    }

    cachedCue.lastPlayed = now;
    cachedCue.hasPlayed = true;
    return true;
}

void AudioSystem::InvalidateSoundCue(const std::filesystem::path& cuePath)
{
    const std::string cacheKey = cuePath.lexically_normal().generic_string();
    soundCues_.erase(cacheKey);
    failedSoundCuePaths_.erase(cacheKey);
}

void AudioSystem::Shutdown() {
    if (!initialized_) {
        return;
    }

    for (ActiveSource& activeSource : sources_) {
        alSourceStop(activeSource.source);
        alDeleteSources(1, &activeSource.source);
        if (activeSource.transientBuffer != 0) {
            alDeleteBuffers(1, &activeSource.transientBuffer);
        }
    }
    sources_.clear();

    for (const auto& [path, buffer] : wavBuffers_) {
        alDeleteBuffers(1, &buffer);
    }
    wavBuffers_.clear();
    soundCues_.clear();
    failedWavPaths_.clear();
    failedSoundCuePaths_.clear();

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
