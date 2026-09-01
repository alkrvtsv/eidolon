#pragma once

#include <d3d11.h>
#include <cstdint>
#include <functional>

struct DecodedFrame {
    ID3D11Texture2D* texture{nullptr};
    uint32_t subresourceIndex{0};
    uint32_t width{0};
    uint32_t height{0};
};

class IVideoDecoder {
public:
    virtual ~IVideoDecoder() noexcept = default;

    virtual bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context) = 0;
    virtual void Shutdown() noexcept = 0;

    virtual bool Decode(const uint8_t* data, size_t size) = 0;
    virtual void SetFrameCallback(std::function<void(const DecodedFrame&)> callback) = 0;
};