#pragma once

#include <d3d11.h>
#include <cstdint>
#include <functional>

struct EncoderConfig {
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t frameRateNum{60};
    uint32_t frameRateDen{1};
    uint32_t bitRate{15'000'000};
    uint32_t maxBitRate{15'000'000};
    uint32_t vbvBufferSize{1'500'000};
    uint32_t gopLength{60};
    bool enableIntraRefresh{true};
    uint32_t intraRefreshPeriod{60};
    uint32_t intraRefreshDuration{10};
};

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() noexcept = default;

    virtual bool Initialize(ID3D11Device* device, const EncoderConfig& config) = 0;
    virtual void Shutdown() noexcept = 0;

    virtual bool EncodeFrame(ID3D11Texture2D* pTexture, bool forceIDR) = 0;
    virtual void SetEncodedFrameCallback(std::function<void(const uint8_t* data, size_t size)> callback) = 0;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
};