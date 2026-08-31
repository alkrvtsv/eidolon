#include "audio/wasapi_opus_capturer.h"
#include "mmcss.h"
#include <algorithm>

WasapiOpusCapturer::WasapiOpusCapturer() = default;

WasapiOpusCapturer::~WasapiOpusCapturer() noexcept {
    Shutdown();
}

bool WasapiOpusCapturer::Initialize() {
    Shutdown();

    if (!InitializeWasapi()) {
        Shutdown();
        return false;
    }

    if (!InitializeOpus()) {
        Shutdown();
        return false;
    }

    pcmAccumulator_.reserve(kSamplesPerFrame * kChannels * 4);
    return true;
}

void WasapiOpusCapturer::Shutdown() noexcept {
    Stop();

    if (opusEncoder_) {
        opus_encoder_destroy(opusEncoder_);
        opusEncoder_ = nullptr;
    }

    if (waveFormat_) {
        CoTaskMemFree(waveFormat_);
        waveFormat_ = nullptr;
    }

    if (audioEvent_) {
        CloseHandle(audioEvent_);
        audioEvent_ = nullptr;
    }

    captureClient_.Reset();
    audioClient_.Reset();
    device_.Reset();
    enumerator_.Reset();
    pcmAccumulator_.clear();
}

bool WasapiOpusCapturer::InitializeWasapi() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator_)
    );
    if (FAILED(hr)) {
        return false;
    }

    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (FAILED(hr)) {
        return false;
    }

    hr = device_->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        &audioClient_
    );
    if (FAILED(hr)) {
        return false;
    }

    hr = audioClient_->GetMixFormat(&waveFormat_);
    if (FAILED(hr)) {
        return false;
    }

    const REFERENCE_TIME hnsRequestedDuration = 100000;
    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        hnsRequestedDuration,
        0,
        waveFormat_,
        nullptr
    );
    if (FAILED(hr)) {
        return false;
    }

    audioEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!audioEvent_) {
        return false;
    }

    hr = audioClient_->SetEventHandle(audioEvent_);
    if (FAILED(hr)) {
        return false;
    }

    hr = audioClient_->GetService(IID_PPV_ARGS(&captureClient_));
    return SUCCEEDED(hr);
}

bool WasapiOpusCapturer::InitializeOpus() {
    int error = OPUS_OK;
    opusEncoder_ = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &error);
    if (error != OPUS_OK || !opusEncoder_) {
        return false;
    }

    opus_encoder_ctl(opusEncoder_, OPUS_SET_BITRATE(128000));
    opus_encoder_ctl(opusEncoder_, OPUS_SET_COMPLEXITY(10));
    opus_encoder_ctl(opusEncoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));

    return true;
}

bool WasapiOpusCapturer::Start() {
    if (!audioClient_ || running_) {
        return false;
    }

    HRESULT hr = audioClient_->Start();
    if (FAILED(hr)) {
        return false;
    }

    running_ = true;
    captureThread_ = std::thread(&WasapiOpusCapturer::CaptureThreadProc, this);
    return true;
}

void WasapiOpusCapturer::Stop() noexcept {
    if (!running_) {
        return;
    }

    running_ = false;
    if (audioEvent_) {
        SetEvent(audioEvent_);
    }

    if (captureThread_.joinable()) {
        captureThread_.join();
    }

    if (audioClient_) {
        audioClient_->Stop();
    }
}

void WasapiOpusCapturer::CaptureThreadProc() {
    MMCSSScopedTask mmcss(L"Pro Audio");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while (running_) {
        DWORD waitResult = WaitForSingleObject(audioEvent_, 20);
        if (!running_) {
            break;
        }

        if (waitResult != WAIT_OBJECT_0) {
            continue;
        }

        UINT32 packetLength = 0;
        HRESULT hr = captureClient_->GetNextPacketSize(&packetLength);

        while (SUCCEEDED(hr) && packetLength > 0) {
            BYTE* data = nullptr;
            UINT32 numFramesAvailable = 0;
            DWORD flags = 0;

            hr = captureClient_->GetBuffer(&data, &numFramesAvailable, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                break;
            }

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                size_t samplesToAdd = numFramesAvailable * kChannels;
                size_t oldSize = pcmAccumulator_.size();
                pcmAccumulator_.resize(oldSize + samplesToAdd, 0.0f);
            } else {
                const float* floatData = reinterpret_cast<const float*>(data);
                size_t samplesToAdd = numFramesAvailable * waveFormat_->nChannels;
                
                if (waveFormat_->nChannels == 2) {
                    pcmAccumulator_.insert(pcmAccumulator_.end(), floatData, floatData + samplesToAdd);
                } else if (waveFormat_->nChannels == 1) {
                    for (UINT32 i = 0; i < numFramesAvailable; ++i) {
                        pcmAccumulator_.push_back(floatData[i]);
                        pcmAccumulator_.push_back(floatData[i]);
                    }
                } else {
                    for (UINT32 i = 0; i < numFramesAvailable; ++i) {
                        pcmAccumulator_.push_back(floatData[i * waveFormat_->nChannels]);
                        pcmAccumulator_.push_back(floatData[i * waveFormat_->nChannels + 1]);
                    }
                }
            }

            captureClient_->ReleaseBuffer(numFramesAvailable);

            while (pcmAccumulator_.size() >= kSamplesPerFrame * kChannels) {
                opus_int32 encodedBytes = opus_encode_float(
                    opusEncoder_,
                    pcmAccumulator_.data(),
                    kSamplesPerFrame,
                    opusOutBuffer_,
                    sizeof(opusOutBuffer_)
                );

                if (encodedBytes > 0 && encodedAudioCallback_) {
                    encodedAudioCallback_(opusOutBuffer_, static_cast<size_t>(encodedBytes));
                }

                pcmAccumulator_.erase(pcmAccumulator_.begin(), pcmAccumulator_.begin() + (kSamplesPerFrame * kChannels));
            }

            hr = captureClient_->GetNextPacketSize(&packetLength);
        }
    }
}