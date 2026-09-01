#pragma once

#include "decoder/video_decoder.h"
#include "protocol.h"
#include <windows.h>
#include <cstdint>

class IVideoRenderer {
public:
    virtual ~IVideoRenderer() noexcept = default;

    virtual bool Initialize(HWND hwnd, uint32_t width, uint32_t height) = 0;
    virtual void Shutdown() noexcept = 0;

    virtual void Resize(uint32_t width, uint32_t height) = 0;
    virtual bool RenderFrame(const DecodedFrame& frame) = 0;

    virtual void UpdateCursorShape(const CursorShapeMessage& shape, const uint8_t* data) = 0;
    virtual void UpdateCursorPosition(const CursorPositionMessage& pos) = 0;

    virtual ID3D11Device* GetDevice() const = 0;
    virtual ID3D11DeviceContext* GetContext() const = 0;
};