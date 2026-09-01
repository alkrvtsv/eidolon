#pragma once

#include <cstdint>
#include <cstddef>

class IAudioPlayer {
public:
    virtual ~IAudioPlayer() noexcept = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() noexcept = 0;

    virtual bool DecodeAndPlay(const uint8_t* data, size_t size) = 0;
};