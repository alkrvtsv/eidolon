#pragma once

#include <cstdint>
#include <functional>

class IAudioCapturer {
public:
    virtual ~IAudioCapturer() noexcept = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() noexcept = 0;

    virtual bool Start() = 0;
    virtual void Stop() noexcept = 0;

    virtual void SetEncodedAudioCallback(std::function<void(const uint8_t* data, size_t size)> callback) = 0;
};