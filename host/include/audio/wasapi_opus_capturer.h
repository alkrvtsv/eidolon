#pragma once

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <opus/opus.h>
#include <wrl/client.h>
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

struct SwrContext;
using Microsoft::WRL::ComPtr;

class WasapiOpusCapturer : public IMMNotificationClient {
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

    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override;

private:
    bool InitializeCodecs();
    bool InitializeDevice();
    void ShutdownDevice() noexcept;

    ComPtr<IMMDeviceEnumerator> deviceEnumerator_;
    ComPtr<IAudioClient> audioClient_;
    ComPtr<IAudioCaptureClient> captureClient_;
    OpusEncoder* opusEncoder_{nullptr};
    SwrContext* swrCtx_{nullptr};

    uint32_t channels_{2};
    uint32_t sampleRate_{48000};
    uint16_t bitsPerSample_{32};
    bool isFloat_{true};
    bool comInitialized_{false};
    bool deviceReady_{false};

    std::atomic<bool> running_{false};
    std::thread captureThread_;
    HANDLE deviceChangedEvent_{nullptr};
    std::atomic<ULONG> refCount_{1};

    std::function<void(const uint8_t*, size_t)> encodedAudioCallback_;
};