#pragma once

#include "capture/frame_capturer.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <functional>

using Microsoft::WRL::ComPtr;

class DXGICapturer final : public IFrameCapturer {
public:
    DXGICapturer();
    ~DXGICapturer() noexcept override;

    bool Initialize() override;
    void Shutdown() noexcept override;

    CaptureStatus AcquireFrame(ID3D11Texture2D** ppTexture, uint32_t timeoutMs) override;
    void ReleaseFrame() override;

    ID3D11Device* GetDevice() const override { return device_.Get(); }
    ID3D11DeviceContext* GetContext() const override { return context_.Get(); }
    uint32_t GetWidth() const override { return width_; }
    uint32_t GetHeight() const override { return height_; }

    void SetCursorShapeCallback(std::function<void(const CursorShapeMessage&, const uint8_t*)> callback) override {
        cursorShapeCallback_ = std::move(callback);
    }
    void SetCursorPositionCallback(std::function<void(const CursorPositionMessage&)> callback) override {
        cursorPositionCallback_ = std::move(callback);
    }

private:
    bool CreateD3DDevice();
    bool InitializeDuplication();
    void ProcessCursor(const DXGI_OUTDUPL_FRAME_INFO& frameInfo);

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    
    uint32_t width_{0};
    uint32_t height_{0};
    bool frameLocked_{false};

    std::vector<uint8_t> cursorShapeBuffer_;

    std::function<void(const CursorShapeMessage&, const uint8_t*)> cursorShapeCallback_;
    std::function<void(const CursorPositionMessage&)> cursorPositionCallback_;
};