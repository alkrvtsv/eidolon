#include "audio/wasapi_opus_capturer.h"
#include <chrono>
#include <iostream>
#include <vector>

WasapiOpusCapturer::WasapiOpusCapturer() = default;

WasapiOpusCapturer::~WasapiOpusCapturer() noexcept {
    Shutdown();
}

bool WasapiOpusCapturer::Initialize() {
    Shutdown();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return false;

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) return false;

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audioClient_);
    if (FAILED(hr)) return false;

    WAVEFORMATEX* mixFormat = nullptr;
    hr = audioClient_->GetMixFormat(&mixFormat);
    if (FAILED(hr)) return false;

    channels_ = mixFormat->nChannels;
    sampleRate_ = mixFormat->nSamplesPerSec;

    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        1000000, // 100ms буфер
        0,
        mixFormat,
        nullptr
    );

    CoTaskMemFree(mixFormat);
    if (FAILED(hr)) return false;

    hr = audioClient_->GetService(IID_PPV_ARGS(&captureClient_));
    if (FAILED(hr)) return false;

    int opusError = 0;
    opusEncoder_ = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &opusError);
    if (opusError != OPUS_OK || !opusEncoder_) return false;

    opus_encoder_ctl(opusEncoder_, OPUS_SET_BITRATE(128000));
    opus_encoder_ctl(opusEncoder_, OPUS_SET_COMPLEXITY(10));
    opus_encoder_ctl(opusEncoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));

    return true;
}

void WasapiOpusCapturer::Shutdown() noexcept {
    Stop();
    if (opusEncoder_) {
        opus_encoder_destroy(opusEncoder_);
        opusEncoder_ = nullptr;
    }
    captureClient_.Reset();
    audioClient_.Reset();
}

bool WasapiOpusCapturer::Start() {
    if (running_.exchange(true)) return true;
    if (!audioClient_ || !captureClient_ || !opusEncoder_) {
        running_ = false;
        return false;
    }

    HRESULT hr = audioClient_->Start();
    if (FAILED(hr)) {
        running_ = false;
        return false;
    }

    captureThread_ = std::thread([this]() {
        const int OPUS_FRAME_SIZE = 960; // 20ms @ 48kHz stereo
        std::vector<float> pcmBuffer;
        pcmBuffer.reserve(OPUS_FRAME_SIZE * 2 * 4);
        std::vector<uint8_t> opusPacket(4000);

        while (running_) {
            UINT32 packetLength = 0;
            HRESULT hrPacket = captureClient_->GetNextPacketSize(&packetLength);

            if (FAILED(hrPacket) || packetLength == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            BYTE* data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;

            hrPacket = captureClient_->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
            if (SUCCEEDED(hrPacket)) {
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    pcmBuffer.insert(pcmBuffer.end(), numFrames * 2, 0.0f);
                } else {
                    const float* floatData = reinterpret_cast<const float*>(data);
                    pcmBuffer.insert(pcmBuffer.end(), floatData, floatData + (numFrames * 2));
                }
                captureClient_->ReleaseBuffer(numFrames);

                while (pcmBuffer.size() >= static_cast<size_t>(OPUS_FRAME_SIZE * 2)) {
                    opus_int32 encodedBytes = opus_encode_float(
                        opusEncoder_,
                        pcmBuffer.data(),
                        OPUS_FRAME_SIZE,
                        opusPacket.data(),
                        static_cast<opus_int32>(opusPacket.size())
                    );

                    if (encodedBytes > 0 && encodedAudioCallback_) {
                        encodedAudioCallback_(opusPacket.data(), static_cast<size_t>(encodedBytes));
                    }

                    pcmBuffer.erase(pcmBuffer.begin(), pcmBuffer.begin() + (OPUS_FRAME_SIZE * 2));
                }
            }
        }
    });

    return true;
}

void WasapiOpusCapturer::Stop() noexcept {
    if (!running_.exchange(false)) return;
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    if (audioClient_) {
        audioClient_->Stop();
    }
}