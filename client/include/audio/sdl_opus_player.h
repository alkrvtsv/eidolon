#pragma once

#include "audio/audio_player.h"
#include <SDL2/SDL.h>
#include <opus/opus.h>

class SDLOpusPlayer final : public IAudioPlayer {
public:
    SDLOpusPlayer();
    ~SDLOpusPlayer() noexcept override;

    bool Initialize() override;
    void Shutdown() noexcept override;

    bool DecodeAndPlay(const uint8_t* data, size_t size) override;

private:
    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 2;
    static constexpr int kMaxFrameSize = 5760; // 120ms буфер
    static constexpr uint32_t kMaxLatencyBytes = (kSampleRate * kChannels * sizeof(float) * 100) / 1000; // 100ms джиттер-буфер

    OpusDecoder* decoder_{nullptr};
    SDL_AudioDeviceID deviceId_{0};
    float pcmOutBuffer_[kMaxFrameSize * kChannels]{};
};