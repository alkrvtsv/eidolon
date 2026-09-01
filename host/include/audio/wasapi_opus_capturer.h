#pragma once

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <opus/opus.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

using Microsoft::WRL::ComPtr;

class WasapiOpusCapturer {
public:
    WasapiOpusCapturer();
    ~WasapiOpusCapturer() noexcept;

    bool Initialize();
    void Shutdown() noexcept;

    bool Start();
    void Stop() noexcept;

    void SetEncodedAudioCallback(std::function<void(const uint8_t*, size_t)> callback) {
        encodedAudioCallback_ = std::move(callback);
    }

private:
    ComPtr<IAudioClient> audioClient_;
    ComPtr<IAudioCaptureClient> captureClient_;
    OpusEncoder* opusEncoder_{nullptr};

    uint32_t channels_{2};
    uint32_t sampleRate_{48000};

    std::atomic<bool> running_{false};
    std::thread captureThread_;
    std::function<void(const uint8_t*, size_t)> encodedAudioCallback_;
};