#pragma once

#include "protocol.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <functional>
#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;

enum class CaptureStatus {
    Success,
    Timeout,
    AccessLost,
    Error
};

class DXGICapturer {
public:
    DXGICapturer();
    ~DXGICapturer() noexcept;

    bool Initialize();
    void Shutdown() noexcept;

    CaptureStatus AcquireFrame(ID3D11Texture2D** outTexture, uint32_t timeoutMs = 16);
    void ReleaseFrame();

    void ResendCursorState();

    void SetCursorShapeCallback(std::function<void(const CursorShapeMessage&, const uint8_t*)> callback) {
        cursorShapeCallback_ = std::move(callback);
    }
    void SetCursorPositionCallback(std::function<void(const CursorPositionMessage&)> callback) {
        cursorPositionCallback_ = std::move(callback);
    }

    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    ID3D11Device* GetDevice() const { return device_.Get(); }
    ID3D11DeviceContext* GetContext() const { return context_.Get(); }

private:
    void ProcessCursor(const DXGI_OUTDUPL_FRAME_INFO& frameInfo);

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<ID3D11Texture2D> currentFrameTexture_;

    uint32_t width_{0};
    uint32_t height_{0};
    bool frameAcquired_{false};

    std::vector<uint8_t> shapeBuffer_;
    std::vector<uint8_t> convertedShapeBuffer_;

    CursorShapeMessage cachedShape_{};
    std::vector<uint8_t> cachedShapeData_;
    CursorPositionMessage cachedPos_{};
    bool hasCachedShape_{false};
    bool hasCachedPos_{false};

    std::function<void(const CursorShapeMessage&, const uint8_t*)> cursorShapeCallback_;
    std::function<void(const CursorPositionMessage&)> cursorPositionCallback_;
};