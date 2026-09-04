#include "audio/wasapi_opus_capturer.h"
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

#pragma warning(push)
#pragma warning(disable: 4996)
extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}
#pragma warning(pop)

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

    AVSampleFormat inSampleFmt = AV_SAMPLE_FMT_FLT;
    if (!isFloat_) {
        if (bitsPerSample_ == 16) {
            inSampleFmt = AV_SAMPLE_FMT_S16;
        } else if (bitsPerSample_ == 32) {
            inSampleFmt = AV_SAMPLE_FMT_S32;
        }
    }

#if LIBAVUTIL_VERSION_MAJOR >= 57
    AVChannelLayout inLayout;
    av_channel_layout_default(&inLayout, static_cast<int>(channels_));
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;

    int ret = swr_alloc_set_opts2(
        &swrCtx_,
        &outLayout,
        AV_SAMPLE_FMT_FLT,
        48000,
        &inLayout,
        inSampleFmt,
        static_cast<int>(sampleRate_),
        0,
        nullptr
    );
    av_channel_layout_uninit(&inLayout);
    if (ret < 0 || !swrCtx_) {
        Shutdown();
        return false;
    }
#else
    int64_t inLayout = av_get_default_channel_layout(static_cast<int>(channels_));
    int64_t outLayout = AV_CH_LAYOUT_STEREO;

    swrCtx_ = swr_alloc_set_opts(
        nullptr,
        outLayout,
        AV_SAMPLE_FMT_FLT,
        48000,
        inLayout,
        inSampleFmt,
        static_cast<int>(sampleRate_),
        0,
        nullptr
    );
    if (!swrCtx_) {
        Shutdown();
        return false;
    }
#endif

    if (swr_init(swrCtx_) < 0) {
        Shutdown();
        return false;
    }

    int opusError = 0;
    opusEncoder_ = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &opusError);
    if (opusError != OPUS_OK || !opusEncoder_) {
        Shutdown();
        return false;
    }

    opus_encoder_ctl(opusEncoder_, OPUS_SET_BITRATE(160000));
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
    if (swrCtx_) {
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
    }
    captureClient_.Reset();
    audioClient_.Reset();
}

bool WasapiOpusCapturer::Start() {
    if (running_.exchange(true)) return true;
    if (!audioClient_ || !captureClient_ || !opusEncoder_ || !swrCtx_) {
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

        const int OPUS_FRAME_SIZE = 960;
        std::vector<float> pcmBuffer;
        pcmBuffer.reserve(48000);
        std::vector<uint8_t> opusPacket(4000);

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
                    int64_t dstSamples = av_rescale_rnd(numFrames, 48000, sampleRate_, AV_ROUND_UP);
                    pcmBuffer.insert(pcmBuffer.end(), static_cast<size_t>(dstSamples * 2), 0.0f);
                } else if (data && swrCtx_) {
                    int maxOutSamples = swr_get_out_samples(swrCtx_, static_cast<int>(numFrames));
                    if (maxOutSamples > 0) {
                        std::vector<float> converted(static_cast<size_t>(maxOutSamples * 2));
                        uint8_t* outData[1] = { reinterpret_cast<uint8_t*>(converted.data()) };
                        const uint8_t* inData[1] = { data };

                        int convertedSamples = swr_convert(
                            swrCtx_,
                            outData,
                            maxOutSamples,
                            inData,
                            static_cast<int>(numFrames)
                        );

                        if (convertedSamples > 0) {
                            pcmBuffer.insert(
                                pcmBuffer.end(),
                                converted.data(),
                                converted.data() + (convertedSamples * 2)
                            );
                        }
                    }
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