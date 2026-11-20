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
        std::cerr << "[Client ERROR] SDL_InitSubSystem(AUDIO) failed: " << SDL_GetError() << std::endl;
        Shutdown();
        return false;
    }
    audioSubsystemReady_ = true;

    if (!TryOpenDevice()) {
        std::cerr << "[Client WARNING] No audio output device found, waiting for connection..." << std::endl;
    }

    return true;
}

bool SDLOpusPlayer::TryOpenDevice() {
    if (deviceId_.load(std::memory_order_acquire) != 0) {
        return true;
    }
    if (!audioSubsystemReady_) {
        return false;
    }

    SDL_AudioSpec desiredSpec = {};
    desiredSpec.freq = kSampleRate;
    desiredSpec.format = AUDIO_F32SYS;
    desiredSpec.channels = kChannels;
    desiredSpec.samples = 960;
    desiredSpec.callback = nullptr;

    SDL_AudioSpec obtainedSpec = {};
    SDL_AudioDeviceID newDeviceId = SDL_OpenAudioDevice(nullptr, 0, &desiredSpec, &obtainedSpec, 0);
    if (newDeviceId == 0) {
        return false;
    }

    deviceId_.store(newDeviceId, std::memory_order_release);
    SDL_PauseAudioDevice(newDeviceId, 0);
    std::cout << "[Client] Audio device connected, playback started" << std::endl;
    return true;
}

void SDLOpusPlayer::Shutdown() noexcept {
    SDL_AudioDeviceID dev = deviceId_.exchange(0, std::memory_order_acq_rel);
    if (dev != 0) {
        SDL_CloseAudioDevice(dev);
    }
    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
    if (audioSubsystemReady_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audioSubsystemReady_ = false;
    }
}

bool SDLOpusPlayer::DecodeAndPlay(const uint8_t* data, size_t size) {
    SDL_AudioDeviceID dev = deviceId_.load(std::memory_order_acquire);
    if (!decoder_ || dev == 0 || !data || size == 0) {
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

    uint32_t currentQueuedBytes = SDL_GetQueuedAudioSize(dev);
    if (currentQueuedBytes > kMaxLatencyBytes) {
        SDL_ClearQueuedAudio(dev);
    }

    uint32_t bytesToWrite = static_cast<uint32_t>(samplesDecoded * kChannels * sizeof(float));
    return SDL_QueueAudio(dev, pcmOutBuffer_, bytesToWrite) == 0;
}