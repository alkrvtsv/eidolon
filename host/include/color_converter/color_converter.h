#pragma once

#include <d3d11.h>
#include <cstdint>

class IColorConverter {
public:
    virtual ~IColorConverter() = default;

    virtual bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width, uint32_t height) = 0;
    virtual void Shutdown() = 0;

    virtual bool Convert(ID3D11Texture2D* pInputTexture, ID3D11Texture2D** ppOutputTexture) = 0;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
};