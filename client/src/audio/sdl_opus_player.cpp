#include "audio/sdl_opus_player.h"
#include <iostream>

SDLOpusPlayer::SDLOpusPlayer() = default;

SDLOpusPlayer::~SDLOpusPlayer() noexcept {
    Shutdown();
}

bool SDLOpusPlayer::Initialize() {
    Shutdown();

    int error = OPUS_OK;
    decoder_ = opus_decoder_create(kSampleRate, kChannels, &error);
    if (error != OPUS_OK || !decoder_) {
        std::cerr << "[Client ERROR] Failed to create Opus decoder: " << error << std::endl;
        return false;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        Shutdown();
        return false;
    }

    SDL_AudioSpec desiredSpec = {};
    desiredSpec.freq = kSampleRate;
    desiredSpec.format = AUDIO_F32SYS;
    desiredSpec.channels = kChannels;
    desiredSpec.samples = 960; // 20ms буфер SDL
    desiredSpec.callback = nullptr;

    SDL_AudioSpec obtainedSpec = {};
    deviceId_ = SDL_OpenAudioDevice(nullptr, 0, &desiredSpec, &obtainedSpec, 0);
    if (deviceId_ == 0) {
        std::cerr << "[Client ERROR] SDL_OpenAudioDevice failed: " << SDL_GetError() << std::endl;
        Shutdown();
        return false;
    }

    SDL_PauseAudioDevice(deviceId_, 0);
    return true;
}

void SDLOpusPlayer::Shutdown() noexcept {
    if (deviceId_ != 0) {
        SDL_CloseAudioDevice(deviceId_);
        deviceId_ = 0;
    }

    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool SDLOpusPlayer::DecodeAndPlay(const uint8_t* data, size_t size) {
    if (!decoder_ || deviceId_ == 0 || !data || size == 0) {
        return false;
    }

    int samplesDecoded = opus_decode_float(
        decoder_,
        data,
        static_cast<opus_int32>(size),
        pcmOutBuffer_,
        kMaxFrameSize,
        0
    );

    if (samplesDecoded <= 0) {
        return false;
    }

    // Если в очереди скопилось более 100 мс (джиттер сети), очищаем излишек для удержания низкой задержки
    uint32_t currentQueuedBytes = SDL_GetQueuedAudioSize(deviceId_);
    if (currentQueuedBytes > kMaxLatencyBytes) {
        SDL_ClearQueuedAudio(deviceId_);
    }

    uint32_t bytesToWrite = static_cast<uint32_t>(samplesDecoded * kChannels * sizeof(float));
    return SDL_QueueAudio(deviceId_, pcmOutBuffer_, bytesToWrite) == 0;
}