#pragma once

#include <d3d11.h>
#include <cstdint>
#include <functional>
#include "protocol.h"

enum class CaptureStatus {
    Success,
    Timeout,
    AccessLost,
    Error
};

class IFrameCapturer {
public:
    virtual ~IFrameCapturer() = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    
    virtual CaptureStatus AcquireFrame(ID3D11Texture2D** ppTexture, uint32_t timeoutMs) = 0;
    virtual void ReleaseFrame() = 0;

    virtual ID3D11Device* GetDevice() const = 0;
    virtual ID3D11DeviceContext* GetContext() const = 0;
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;

    virtual void SetCursorShapeCallback(std::function<void(const CursorShapeMessage&, const uint8_t*)> callback) = 0;
    virtual void SetCursorPositionCallback(std::function<void(const CursorPositionMessage&)> callback) = 0;
};