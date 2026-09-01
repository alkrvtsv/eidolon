#include "audio/wasapi_opus_capturer.h"
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

#pragma comment(lib, "winmm.lib")

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
    bitsPerSample_ = mixFormat->wBitsPerSample;

    if (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mixFormat);
        isFloat_ = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    } else {
        isFloat_ = (mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    }

    std::cout << "[Host Audio] WASAPI Format: " << sampleRate_ << "Hz, " 
              << channels_ << " channels, " << bitsPerSample_ << " bits, " 
              << (isFloat_ ? "Float" : "PCM") << std::endl;

    // Выделяем буфер на 200мс в драйвере для надежности
    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        2000000, 
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

    opus_encoder_ctl(opusEncoder_, OPUS_SET_BITRATE(160000)); // 160 kbps (высокое качество)
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
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        timeBeginPeriod(1);

        const int OPUS_FRAME_SIZE = 960; // 20мс @ 48000 Гц Стерео
        std::vector<float> inputStereoBuffer;
        std::vector<float> resampled48kBuffer;
        inputStereoBuffer.reserve(48000);
        resampled48kBuffer.reserve(48000);
        std::vector<uint8_t> opusPacket(4000);

        double resampleRatio = 48000.0 / static_cast<double>(sampleRate_);
        double resamplePos = 0.0;

        while (running_) {
            UINT32 packetLength = 0;
            HRESULT hrPacket = captureClient_->GetNextPacketSize(&packetLength);

            if (FAILED(hrPacket) || packetLength == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }

            while (packetLength > 0 && running_) {
                BYTE* data = nullptr;
                UINT32 numFrames = 0;
                DWORD flags = 0;

                hrPacket = captureClient_->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
                if (FAILED(hrPacket)) break;

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    inputStereoBuffer.insert(inputStereoBuffer.end(), numFrames * 2, 0.0f);
                } else if (data) {
                    if (isFloat_) {
                        const float* src = reinterpret_cast<const float*>(data);
                        for (UINT32 i = 0; i < numFrames; ++i) {
                            float left = std::clamp(src[i * channels_ + 0], -1.0f, 1.0f);
                            float right = std::clamp(src[i * channels_ + (channels_ > 1 ? 1 : 0)], -1.0f, 1.0f);
                            inputStereoBuffer.push_back(left);
                            inputStereoBuffer.push_back(right);
                        }
                    } else if (bitsPerSample_ == 16) {
                        const int16_t* src = reinterpret_cast<const int16_t*>(data);
                        for (UINT32 i = 0; i < numFrames; ++i) {
                            float left = std::clamp(src[i * channels_ + 0] / 32768.0f, -1.0f, 1.0f);
                            float right = std::clamp(src[i * channels_ + (channels_ > 1 ? 1 : 0)] / 32768.0f, -1.0f, 1.0f);
                            inputStereoBuffer.push_back(left);
                            inputStereoBuffer.push_back(right);
                        }
                    } else if (bitsPerSample_ == 24) {
                        const uint8_t* src = data;
                        for (UINT32 i = 0; i < numFrames; ++i) {
                            int32_t valL = (src[(i * channels_ + 0) * 3 + 0] << 8) |
                                           (src[(i * channels_ + 0) * 3 + 1] << 16) |
                                           (src[(i * channels_ + 0) * 3 + 2] << 24);
                            int32_t valR = (channels_ > 1) ?
                                           ((src[(i * channels_ + 1) * 3 + 0] << 8) |
                                            (src[(i * channels_ + 1) * 3 + 1] << 16) |
                                            (src[(i * channels_ + 1) * 3 + 2] << 24)) : valL;
                            inputStereoBuffer.push_back(std::clamp(valL / 2147483648.0f, -1.0f, 1.0f));
                            inputStereoBuffer.push_back(std::clamp(valR / 2147483648.0f, -1.0f, 1.0f));
                        }
                    }
                }

                captureClient_->ReleaseBuffer(numFrames);

                // Ресемплинг в 48000 Гц
                if (sampleRate_ == 48000) {
                    resampled48kBuffer.insert(resampled48kBuffer.end(), inputStereoBuffer.begin(), inputStereoBuffer.end());
                    inputStereoBuffer.clear();
                } else {
                    size_t inFrames = inputStereoBuffer.size() / 2;
                    while (resamplePos + 1.0 < inFrames) {
                        size_t idx = static_cast<size_t>(resamplePos);
                        double frac = resamplePos - idx;

                        float l0 = inputStereoBuffer[idx * 2 + 0];
                        float r0 = inputStereoBuffer[idx * 2 + 1];
                        float l1 = inputStereoBuffer[(idx + 1) * 2 + 0];
                        float r1 = inputStereoBuffer[(idx + 1) * 2 + 1];

                        resampled48kBuffer.push_back(static_cast<float>(l0 + frac * (l1 - l0)));
                        resampled48kBuffer.push_back(static_cast<float>(r0 + frac * (r1 - r0)));

                        resamplePos += (1.0 / resampleRatio);
                    }

                    size_t consumed = static_cast<size_t>(resamplePos);
                    if (consumed > 0 && inFrames > consumed) {
                        inputStereoBuffer.erase(inputStereoBuffer.begin(), inputStereoBuffer.begin() + (consumed * 2));
                        resamplePos -= consumed;
                    }
                }

                // Кодирование ровными пачками по 960 сэмплов (20 мс)
                while (resampled48kBuffer.size() >= static_cast<size_t>(OPUS_FRAME_SIZE * 2)) {
                    opus_int32 encodedBytes = opus_encode_float(
                        opusEncoder_,
                        resampled48kBuffer.data(),
                        OPUS_FRAME_SIZE,
                        opusPacket.data(),
                        static_cast<opus_int32>(opusPacket.size())
                    );

                    if (encodedBytes > 0 && encodedAudioCallback_) {
                        encodedAudioCallback_(opusPacket.data(), static_cast<size_t>(encodedBytes));
                    }

                    resampled48kBuffer.erase(resampled48kBuffer.begin(), resampled48kBuffer.begin() + (OPUS_FRAME_SIZE * 2));
                }

                captureClient_->GetNextPacketSize(&packetLength);
            }
        }

        timeEndPeriod(1);
        CoUninitialize();
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