#pragma once

#include "audio/audio_capturer.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <opus/opus.h>
#include <thread>
#include <atomic>
#include <vector>

using Microsoft::WRL::ComPtr;

class WasapiOpusCapturer final : public IAudioCapturer {
public:
    WasapiOpusCapturer();
    ~WasapiOpusCapturer() noexcept override;

    bool Initialize() override;
    void Shutdown() noexcept override;

    bool Start() override;
    void Stop() noexcept override;

    void SetEncodedAudioCallback(std::function<void(const uint8_t* data, size_t size)> callback) override {
        encodedAudioCallback_ = std::move(callback);
    }

private:
    void CaptureThreadProc();
    bool InitializeWasapi();
    bool InitializeOpus();

    ComPtr<IMMDeviceEnumerator> enumerator_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> audioClient_;
    ComPtr<IAudioCaptureClient> captureClient_;
    
    WAVEFORMATEX* waveFormat_{nullptr};
    OpusEncoder* opusEncoder_{nullptr};

    std::thread captureThread_;
    std::atomic<bool> running_{false};
    HANDLE audioEvent_{nullptr};

    static constexpr uint32_t kSampleRate = 48000;
    static constexpr uint32_t kChannels = 2;
    static constexpr uint32_t kFrameDurationMs = 10;
    static constexpr uint32_t kSamplesPerFrame = (kSampleRate * kFrameDurationMs) / 1000;

    std::vector<float> pcmAccumulator_;
    uint8_t opusOutBuffer_[1275]{};

    std::function<void(const uint8_t* data, size_t size)> encodedAudioCallback_;
};